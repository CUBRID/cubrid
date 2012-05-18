/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */


/*
 * shard_proxy_log.c -
 */

#ident "$Id$"

#include <assert.h>

#include "shard_proxy_log.h"
#include "broker_util.h"
#include "cas_util.h"

static char *make_proxy_log_filename (char *filename_buf,
				      size_t buf_size, const char *br_name,
				      int proxy_index);
static void proxy_log_backup (void);
static void proxy_log_write_internal (int level, char *svc_code,
				      bool do_flush, const char *fmt,
				      va_list ap);
static void proxy_log_reset (void);
static void proxy_access_log_backup (void);
static void proxy_access_log_end (void);

static FILE *log_open (char *log_file_name);

#ifdef CAS_ERROR_LOG
static int error_file_offset;
static char cas_log_error_flag;
#endif
static FILE *log_fp = NULL;
static FILE *access_log_fp = NULL;
static char log_filepath[PATH_MAX];

extern int proxy_id;
extern T_SHM_APPL_SERVER *shm_as_p;
extern T_SHM_PROXY *shm_proxy_p;
extern T_PROXY_INFO *proxy_info_p;

static const char *proxy_log_level_str[] = {
  "NUL",
  "ERR",
  "TMO",
  "NTC",
  "SRD",
  "SCH",
  "DBG"
};

static char *
make_proxy_log_filename (char *filename_buf,
			 size_t buf_size, const char *br_name,
			 int proxy_index)
{
  char dirname[PATH_MAX];

  assert (filename_buf != NULL);

  strcpy (dirname, shm_as_p->proxy_log_dir);

  snprintf (filename_buf, buf_size, "%s%s_%d.log", dirname, br_name,
	    proxy_index + 1);
  return filename_buf;
}

void
proxy_log_open (char *br_name, int proxy_index)
{
  if (log_fp != NULL)
    {
      proxy_log_close ();
    }

  if (proxy_info_p->cur_proxy_log_mode != PROXY_LOG_MODE_NONE)
    {
      if (br_name != NULL)
	{
	  make_proxy_log_filename (log_filepath, PATH_MAX, br_name, proxy_id);
	}

      /* note: in "a+" mode, output is always appended */
      log_fp = fopen (log_filepath, "r+");
      if (log_fp != NULL)
	{
	  fseek (log_fp, 0, SEEK_END);
	}
      else
	{
	  log_fp = fopen (log_filepath, "w");
	}
    }
  else
    {
      log_fp = NULL;
    }

  proxy_info_p->proxy_log_reset = 0;
}

static void
proxy_log_reset (void)
{
  if (proxy_info_p->proxy_log_reset)
    {
      if (log_fp != NULL)
	{
	  proxy_log_close ();
	}

      proxy_log_open (shm_as_p->broker_name, proxy_id);
    }
}

void
proxy_log_close (void)
{
  if (log_fp != NULL)
    {
      fclose (log_fp);
      log_fp = NULL;
    }
}

static void
proxy_log_backup (void)
{
  char backup_filepath[PATH_MAX];

  assert (log_filepath[0] != '\0');

  snprintf (backup_filepath, PATH_MAX, "%s.bak", log_filepath);

  unlink (backup_filepath);
  rename (log_filepath, backup_filepath);
}

void
proxy_log_end (void)
{
  long log_fpos;
  long log_size_max;

  log_size_max = shm_as_p->proxy_log_max_size;

  log_fpos = ftell (log_fp);
  if ((log_fpos / 1000) > log_size_max)
    {
      proxy_log_close ();
      proxy_log_backup ();
      proxy_log_open (shm_as_p->broker_name, proxy_id);
    }

  return;
}

static void
proxy_log_write_internal (int level, char *svc_code, bool do_flush,
			  const char *fmt, va_list ap)
{
  char buf[LINE_MAX], *p;
  int len, n;

  p = buf;
  len = LINE_MAX;
  n = ut_time_string (p, NULL);
  len -= n;
  p += n;
  if (len > 0)
    {
      if (svc_code == NULL)
	{
	  n = snprintf (p, len, " [%s] ", proxy_log_level_str[level]);
	}
      else
	{
	  n =
	    snprintf (p, len, " [%s][%s] ", proxy_log_level_str[level],
		      svc_code);
	}
      len -= n;
      p += n;
      if (len > 0)
	{
	  n = vsnprintf (p, len, fmt, ap);
	  len -= n;
	  p += n;
	}
    }
  fwrite (buf, (p - buf), 1, log_fp);
  fputc ('\n', log_fp);

  if (do_flush == true)
    {
      fflush (log_fp);
    }
}

void
proxy_log_write (int level, char *svc_code, const char *fmt, ...)
{
  if (log_fp == NULL)
    {
      proxy_log_open (shm_as_p->broker_name, proxy_id);
    }

  if (level <= PROXY_LOG_MODE_NONE || level > PROXY_LOG_MODE_ALL)
    {
      return;
    }

  if (level > proxy_info_p->cur_proxy_log_mode)
    {
      return;
    }

  proxy_log_reset ();

  if (log_fp != NULL)
    {
      va_list ap;

      va_start (ap, fmt);
      proxy_log_write_internal (level, svc_code, true, fmt, ap);
      va_end (ap);

      proxy_log_end ();
    }
}

int
proxy_log_get_level (void)
{
  return proxy_info_p->cur_proxy_log_mode;
}

int
proxy_access_log (struct timeval *start_time, int client_ip_addr,
		  char *dbname, char *dbuser, bool accepted)
{
  char *access_log_file;
  char *script = NULL;
  char *clt_ip;
  char *clt_appl = NULL;
  struct tm ct1, ct2;
  time_t t1, t2;
  char *p;
  char err_str[4];
  struct timeval end_time;

  access_log_file = proxy_info_p->access_log_file;

  gettimeofday (&end_time, NULL);

  t1 = start_time->tv_sec;
  t2 = end_time.tv_sec;
#if defined (WINDOWS)
  if (localtime_s (&ct1, &t1) != 0 || localtime_s (&ct2, &t2) != 0)
#else /* !WINDOWS */
  if (localtime_r (&t1, &ct1) == NULL || localtime_r (&t2, &ct2) == NULL)
#endif /* !WINDOWS */
    {
      return -1;
    }
  ct1.tm_year += 1900;
  ct2.tm_year += 1900;

  if (proxy_info_p->proxy_access_log_reset)
    {
      if (access_log_fp != NULL)
	{
	  fclose (access_log_fp);
	  access_log_fp = NULL;
	}
      proxy_info_p->proxy_access_log_reset = 0;
    }

  if (access_log_fp == NULL)
    {
      access_log_fp = log_open (access_log_file);
      if (access_log_fp == NULL)
	{
	  return -1;
	}
      proxy_info_p->proxy_access_log_reset = 0;
    }

  if (script == NULL)
    script = (char *) "-";
  if (clt_appl == NULL || clt_appl[0] == '\0')
    clt_appl = (char *) "-";

  clt_ip = ut_uchar2ipstr ((unsigned char *) (&client_ip_addr));

  for (p = clt_appl; *p; p++)
    {
      if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
	*p = '_';
    }

#ifdef CAS_ERROR_LOG
  if (error_file_offset >= 0)
    sprintf (err_str, "ERR");
  else
#endif
    sprintf (err_str, "-");

  fprintf (access_log_fp,
	   "%s %s %s %d.%03d %d.%03d %02d/%02d/%02d %02d:%02d:%02d ~ "
	   "%02d/%02d/%02d %02d:%02d:%02d %d %s %d %s %s %s\n",
	   clt_ip, clt_appl, script,
	   (int) start_time->tv_sec, (int) (start_time->tv_usec / 1000),
	   (int) end_time.tv_sec, (int) (end_time.tv_usec / 1000),
	   ct1.tm_year, ct1.tm_mon + 1, ct1.tm_mday, ct1.tm_hour, ct1.tm_min,
	   ct1.tm_sec, ct2.tm_year, ct2.tm_mon + 1, ct2.tm_mday, ct2.tm_hour,
	   ct2.tm_min, ct2.tm_sec, (int) getpid (), err_str, -1, dbname,
	   dbuser, ((accepted) ? "" : " : rejected"));
  fflush (access_log_fp);

  return (end_time.tv_sec - start_time->tv_sec);
}

static FILE *
log_open (char *log_file_name)
{
  FILE *fp;
  int log_file_len = 0;
  int ret;
  int tmp_dirlen = 0;
  char *tmp_dirname;
  char *tmp_filename;

  if (log_file_name == NULL)
    return NULL;

  fp = fopen (log_file_name, "a");
  if (fp == NULL)
    {
      if (errno == ENOENT)
	{
	  tmp_filename = strdup (log_file_name);
	  if (tmp_filename == NULL)
	    {
	      return NULL;
	    }
	  tmp_dirname = dirname (tmp_filename);
	  ret = mkdir (tmp_dirname, 0777);
	  free (tmp_filename);
	  if (ret == 0)
	    {
	      fp = fopen (log_file_name, "a");
	      if (fp == NULL)
		{
		  return NULL;
		}
	    }
	  else
	    {
	      return NULL;
	    }
	}
      else
	{
	  return NULL;
	}
    }
  return fp;
}
