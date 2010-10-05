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
 * cas_error_log.c -
 */

#ident "$Id$"

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <errno.h>
#if defined(WINDOWS)
#include <sys/timeb.h>
#include <process.h>
#include <io.h>
#else
#include <unistd.h>
#include <sys/time.h>
#endif
#include <assert.h>

#include "cas_common.h"
#include "cas_error_log.h"
#include "cas_util.h"
#include "broker_config.h"
#include "cas.h"
#include "cas_execute.h"

#include "broker_env_def.h"
#include "broker_filename.h"
#include "broker_util.h"

static char *make_error_log_filename (char *filename_buf, size_t buf_size,
				      const char *br_name, int as_index);
static void cas_error_log_backup (void);
static void cas_log_write_internal (const char *fmt, ...);

static FILE *error_log_fp = NULL;
static char error_log_filepath[PATH_MAX];
static long saved_error_log_fpos = 0;

static int eid = 0;

static char *
make_error_log_filename (char *filename_buf,
			 size_t buf_size, const char *br_name, int as_index)
{
  char dirname[PATH_MAX];

  assert (filename_buf != NULL);

  get_cubrid_file (FID_CUBRID_ERR_DIR, dirname);
  snprintf (filename_buf, buf_size, "%s%s_CAS_%d.err", dirname, br_name,
	    (as_index) + 1);
  return filename_buf;
}


void
cas_error_log_open (char *br_name, int as_index)
{
  if (error_log_fp != NULL)
    {
      cas_error_log_close (true);
    }

  if (br_name != NULL)
    {
      make_error_log_filename (error_log_filepath, PATH_MAX, br_name,
			       as_index);
    }

  /* note: in "a+" mode, output is always appended */
  error_log_fp = fopen (error_log_filepath, "r+");
  if (error_log_fp != NULL)
    {
      fseek (error_log_fp, 0, SEEK_END);
      saved_error_log_fpos = ftell (error_log_fp);
    }
  else
    {
      error_log_fp = fopen (error_log_filepath, "w");
      saved_error_log_fpos = 0;
    }
}

void
cas_error_log_close (bool flag)
{
  if (error_log_fp != NULL)
    {
      if (flag)
	{
	  fseek (error_log_fp, saved_error_log_fpos, SEEK_SET);
	  ftruncate (fileno (error_log_fp), saved_error_log_fpos);
	}
      fclose (error_log_fp);
      error_log_fp = NULL;
      saved_error_log_fpos = 0;
    }
}

static void
cas_error_log_backup (void)
{
  char backup_filepath[PATH_MAX];

  assert (error_log_filepath[0] != '\0');

  snprintf (backup_filepath, PATH_MAX, "%s.bak", error_log_filepath);

  unlink (backup_filepath);
  rename (error_log_filepath, backup_filepath);
}

static void
cas_log_write_internal (const char *fmt, ...)
{
  va_list ap;
  char buf[LINE_MAX], *p;
  int len, n;

  va_start (ap, fmt);
  p = buf;
  len = LINE_MAX;
  n = ut_time_string (p);
  len -= n;
  p += n;
  if (len > 0)
    {
      n = vsnprintf (p, len, fmt, ap);
      len -= n;
      p += n;
    }
  fwrite (buf, (p - buf), 1, error_log_fp);

  fflush (error_log_fp);
  va_end (ap);
}

void
cas_error_log_write (int dbms_errno, const char *dbms_errmsg)
{
  if (error_log_fp == NULL)
    {
      return;
    }

  if (eid >= INT_MAX)
    {
      eid = 0;
    }
  cas_log_write_internal
    (" DBMS ERROR [ERR_CODE : %d, ERR_MSG : %s] EID = %d",
     dbms_errno, (dbms_errmsg != NULL) ? dbms_errmsg : "-", ++eid);
  fputc ('\n', error_log_fp);

  saved_error_log_fpos = ftell (error_log_fp);

  if ((saved_error_log_fpos / 1000) > shm_appl->sql_log_max_size)
    {
      cas_error_log_close (true);
      cas_error_log_backup ();
      cas_error_log_open (NULL, 0);
    }
}

char *
cas_error_log_get_eid (char *buf, size_t bufsz)
{
  char *buf_p;

  if (buf == NULL || bufsz <= 0)
    {
      return NULL;
    }

  if (bufsz < 32)
    {
      buf_p = malloc (32);

      if (buf_p == NULL)
	{
	  return NULL;
	}
    }
  else
    {
      buf_p = buf;
    }

  /* actual print */
  snprintf (buf_p, 32, ", EID = %u", eid);

  return buf_p;
}
