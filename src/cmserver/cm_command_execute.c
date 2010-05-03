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
 * cmd_exec.c -
 */

#ident "$Id$"

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <sys/stat.h>
#include <config.h>

#if defined(WINDOWS)
#include <process.h>
#else
#include <unistd.h>
#endif

#include "cm_command_execute.h"
#include "cm_server_util.h"
#include "cm_stat.h"

#ifdef	_DEBUG_
#include "deb.h"
#endif

#define new_servstat_result()		(T_SERVER_STATUS_RESULT*) new_cmd_result()
#define new_csql_result()		(T_CSQL_RESULT*) new_cmd_result()


static T_CMD_RESULT *new_cmd_result (void);
static T_SPACEDB_RESULT *new_spacedb_result (void);
static const char *get_cubrid_mode_opt (T_CUBRID_MODE mode);
static void read_server_status_output (T_SERVER_STATUS_RESULT * res,
				       char *out_file);
static void read_spacedb_output (T_SPACEDB_RESULT * res, char *out_file);
static void set_spacedb_info (T_SPACEDB_INFO * vol_info, int volid,
			      char *purpose, int total_page, int free_page,
			      char *vol_name);
static int parse_volume_line (T_SPACEDB_INFO * vol_info, char *str_buf);
static int read_start_server_output (char *stdout_log_file,
				     char *stderr_log_file,
				     char *_dbmt_error);

char *
cubrid_cmd_name (char *buf)
{
  buf[0] = '\0';
#if !defined (DO_NOT_USE_CUBRIDENV)
  sprintf (buf, "%s/%s%s", sco.szCubrid, CUBRID_DIR_BIN, UTIL_CUBRID);
#else
  sprintf (buf, "%s/%s", CUBRID_BINDIR, UTIL_CUBRID);
#endif
  return buf;
}

T_CSQL_RESULT *
cmd_csql (char *dbname, char *uid, char *passwd, T_CUBRID_MODE mode,
	  char *infile, char *command)
{
  char *cubrid_err_file;
  char out_file[512];
  T_CSQL_RESULT *res;
  char cmd_name[CUBRID_CMD_NAME_LEN];
  const char *argv[15];
  int argc = 0;

  cmd_name[0] = '\0';
#if !defined (DO_NOT_USE_CUBRIDENV)
  sprintf (cmd_name, "%s/%s%s", sco.szCubrid, CUBRID_DIR_BIN, UTIL_CSQL_NAME);
#else
  sprintf (cmd_name, "%s/%s", CUBRID_BINDIR, UTIL_CSQL_NAME);
#endif
  argv[argc++] = cmd_name;
  argv[argc++] = get_cubrid_mode_opt (mode);
  if (uid)
    {
      argv[argc++] = "--" CSQL_USER_L;
      argv[argc++] = uid;

      if (passwd)
	{
	  argv[argc++] = "--" CSQL_PASSWORD_L;
	  argv[argc++] = passwd;
	}
    }
  if (infile)
    {
      argv[argc++] = "--" CSQL_INPUT_FILE_L;
      argv[argc++] = infile;
    }
  else if (command)
    {
      argv[argc++] = "--" CSQL_COMMAND_L;
      argv[argc++] = command;
    }
  else
    {
      return NULL;
    }
  argv[argc++] = dbname;
  argv[argc++] = NULL;

#if !defined (DO_NOT_USE_CUBRIDENV)
  sprintf (out_file, "%s/tmp/DBMT_util_003.%d", sco.szCubrid,
	   (int) getpid ());
#else
  sprintf (out_file, "%s/DBMT_util_003.%d", CUBRID_TMPDIR, (int) getpid ());
#endif
  INIT_CUBRID_ERROR_FILE (cubrid_err_file);
  SET_TRANSACTION_NO_WAIT_MODE_ENV ();

  run_child (argv, 1, NULL, NULL, cubrid_err_file, NULL);	/* csql */

  res = new_csql_result ();
  if (res == NULL)
    return NULL;

  read_error_file (cubrid_err_file, res->err_msg, ERR_MSG_SIZE);

  unlink (out_file);
  return res;
}

void
cmd_spacedb_result_free (T_SPACEDB_RESULT * res)
{
  if (res)
    {
      if (res->vol_info)
	free (res->vol_info);
      if (res->tmp_vol_info)
	free (res->tmp_vol_info);
      free (res);
    }
}


T_SPACEDB_RESULT *
cmd_spacedb (const char *dbname, T_CUBRID_MODE mode)
{
  T_SPACEDB_RESULT *res;
  char out_file[128];
  char *cubrid_err_file;
  char cmd_name[CUBRID_CMD_NAME_LEN];
  const char *argv[10];

  res = new_spacedb_result ();
  if (res == NULL)
    {
      return NULL;
    }

  sprintf (out_file, "%s/DBMT_util_002.%d", sco.dbmt_tmp_dir,
	   (int) getpid ());
  cubrid_cmd_name (cmd_name);
  argv[0] = cmd_name;
  argv[1] = UTIL_OPTION_SPACEDB;
  argv[2] = get_cubrid_mode_opt (mode);
  argv[3] = "--" SPACE_OUTPUT_FILE_L;
  argv[4] = out_file;
  argv[5] = dbname;
  argv[6] = NULL;

  INIT_CUBRID_ERROR_FILE (cubrid_err_file);
  run_child (argv, 1, NULL, NULL, cubrid_err_file, NULL);	/* spacedb */
  read_error_file (cubrid_err_file, res->err_msg, ERR_MSG_SIZE);
  read_spacedb_output (res, out_file);

  unlink (out_file);
  return res;
}


int
cmd_start_server (char *dbname, char *err_buf, int err_buf_size)
{
  char stdout_log_file[512];
  char stderr_log_file[512];
  int pid;
  int ret_val;
  char cmd_name[CUBRID_CMD_NAME_LEN];
  const char *argv[5];

#ifdef HPUX
  char jvm_env_string[32];
#endif

  cmd_start_master ();
  sprintf (stdout_log_file, "%s/cmserverstart.%d.err", sco.dbmt_tmp_dir,
	   (int) getpid ());
  sprintf (stderr_log_file, "%s/cmserverstart2.%d.err", sco.dbmt_tmp_dir,
	   (int) getpid ());


/* unset CUBRID_ERROR_LOG environment variable, using default value */
#if defined(WINDOWS)
  putenv ("CUBRID_ERROR_LOG=");
#else
  unsetenv ("CUBRID_ERROR_LOG");
#endif

  cmd_name[0] = '\0';
#if !defined (DO_NOT_USE_CUBRIDENV)
  sprintf (cmd_name, "%s/%s%s", sco.szCubrid, CUBRID_DIR_BIN, UTIL_CUBRID);
#else
  sprintf (cmd_name, "%s/%s", CUBRID_BINDIR, UTIL_CUBRID);
#endif

  argv[0] = cmd_name;
  argv[1] = PRINT_CMD_SERVER;
  argv[2] = PRINT_CMD_START;
  argv[3] = dbname;
  argv[4] = NULL;

#ifdef HPUX
#ifdef HPUX_IA64
  strcpy (jvm_env_string, "LD_PRELOAD=libjvm.so");
#else /* pa-risc */
  strcpy (jvm_env_string, "LD_PRELOAD=libjvm.sl");
#endif
  putenv (jvm_env_string);
#endif

  pid = run_child (argv, 1, NULL, stdout_log_file, stderr_log_file, NULL);	/* start server */

#ifdef HPUX
  putenv ("LD_PRELOAD=");
#endif

  if (pid < 0)
    {
      if (err_buf)
	sprintf (err_buf, "system error : %s %s %s %s", cmd_name,
		 PRINT_CMD_SERVER, PRINT_CMD_START, dbname);
      unlink (stdout_log_file);
      unlink (stderr_log_file);
      return -1;
    }

  ret_val =
    read_start_server_output (stdout_log_file, stderr_log_file, err_buf);
  unlink (stdout_log_file);
  unlink (stderr_log_file);

  return ret_val;
}





int
cmd_stop_server (char *dbname, char *err_buf, int err_buf_size)
{
  char strbuf[1024];
  int t, timeout = 30, interval = 3;	/* sec */
  char cmd_name[CUBRID_CMD_NAME_LEN];
  const char *argv[5];

  if (err_buf)
    memset (err_buf, 0, err_buf_size);

  cmd_name[0] = '\0';
#if !defined (DO_NOT_USE_CUBRIDENV)
  sprintf (cmd_name, "%s/%s%s", sco.szCubrid, CUBRID_DIR_BIN, UTIL_CUBRID);
#else
  sprintf (cmd_name, "%s/%s", CUBRID_BINDIR, UTIL_CUBRID);
#endif

  argv[0] = cmd_name;
  argv[1] = PRINT_CMD_SERVER;
  argv[2] = PRINT_CMD_STOP;
  argv[3] = dbname;
  argv[4] = NULL;
  if (run_child (argv, 1, NULL, NULL, NULL, NULL) < 0)
    {				/* stop_server */
      if (err_buf)
	{
	  sprintf (strbuf, "Command returned error : %s %s %s %s", cmd_name,
		   PRINT_CMD_SERVER, PRINT_CMD_STOP, dbname);
	  strncpy (err_buf, strbuf, err_buf_size - 1);
	}
      return -1;
    }

  for (t = timeout; t > 0; t -= interval)
    {
      SLEEP_MILISEC (interval, 0);
      if (!uIsDatabaseActive (dbname))
	return 0;
    }
  if (err_buf)
    {
      sprintf (strbuf, "%s server hasn't shut down after %d seconds", dbname,
	       timeout);
      strncpy (err_buf, strbuf, err_buf_size - 1);
    }
  return -1;
}

void
cmd_start_master (void)
{
  int pid;
  char cmd_name[CUBRID_CMD_NAME_LEN];
  const char *argv[2];

  cmd_name[0] = '\0';
#if !defined (DO_NOT_USE_CUBRIDENV)
  sprintf (cmd_name, "%s/%s%s", sco.szCubrid,
	   CUBRID_DIR_BIN, UTIL_MASTER_NAME);
#else
  sprintf (cmd_name, "%s/%s", CUBRID_BINDIR, UTIL_MASTER_NAME);
#endif
  argv[0] = cmd_name;
  argv[1] = NULL;

  pid = run_child (argv, 0, NULL, NULL, NULL, NULL);	/* cub_master */
  SLEEP_MILISEC (0, 500);
}


int
read_csql_error_file (char *err_file, char *err_buf, int err_buf_size)
{
  FILE *fp;
  char buf[1024];
  int msg_size = 0;

  if (err_buf)
    memset (err_buf, 0, err_buf_size);

  if (err_file == NULL || err_file[0] == '\0')
    return 0;

  fp = fopen (err_file, "r");
  if (fp == NULL)
    return 0;

  while (1)
    {
      memset (buf, 0, sizeof (buf));
      if (fgets (buf, sizeof (buf) - 1, fp) == NULL)
	break;

      ut_trim (buf);

      if ((strncasecmp (buf, "ERROR", 5) == 0))
	{
	  if (err_buf != NULL)
	    {
	      snprintf (err_buf, err_buf_size - 1, "%s", buf + 6);
	    }
	  msg_size = strlen (buf + 6);
	  break;
	}
      else if (strstr (buf, "*** ERROR") != NULL)
	{
	  memset (buf, 0, sizeof (buf));
	  if (fgets (buf, sizeof (buf) - 1, fp) == NULL)
	    break;
	  if (err_buf != NULL)
	    {
	      snprintf (err_buf, err_buf_size - 1, "%s", buf);
	    }
	  msg_size = strlen (buf);
	  break;
	}
    }

  fclose (fp);

  return (msg_size > 0 ? -1 : 0);
}

int
read_error_file (const char *err_file, char *err_buf, int err_buf_size)
{
  FILE *fp;
  char buf[1024];
  int msg_size = 0;
  char rm_prev_flag = 0;
  char is_debug = 0;
  size_t i;

  if (err_buf)
    memset (err_buf, 0, err_buf_size);

  if (err_file == NULL || err_file[0] == '\0')
    return 0;

  fp = fopen (err_file, "r");
  if (fp == NULL)
    return 0;

  while (1)
    {
      memset (buf, 0, sizeof (buf));
      if (fgets (buf, sizeof (buf) - 1, fp) == NULL)
	break;
      for (i = 0; i < sizeof (buf) - 2; i++)
	{
	  if (buf[i] == '\0')
	    {
	      if (buf[i + 1] == '\0')
		break;

	      buf[i] = ' ';
	    }
	}
      ut_trim (buf);
      if (buf[0] == '\0')
	continue;
      if (strncmp (buf, "---", 3) == 0 ||
	  strncmp (buf, "***", 3) == 0 ||
	  strncmp (buf, "<<<", 3) == 0 || strncmp (buf, "Time:", 5) == 0)
	{
	  if (strstr (buf, "- DEBUG") != NULL)
	    {
	      is_debug = 1;
	    }
	  else
	    {
	      is_debug = 0;
	      rm_prev_flag = 1;
	    }
	  continue;
	}
      /* ignore all the debug information, until find new line start with "---"|"***"|"<<<"|"Time:". */
      if (is_debug != 0)
	{
	  continue;
	}

      if (rm_prev_flag != 0)
	{
	  msg_size = 0;
	}
      strcat (buf, "\n");
      if ((err_buf_size - msg_size - 1) > 0)
	{
	  strncpy (err_buf + msg_size, buf, err_buf_size - msg_size - 1);
	}
      else
	{
	  break;
	}
      msg_size += strlen (buf);
      rm_prev_flag = 0;
    }
  err_buf[err_buf_size - 1] = '\0';
  fclose (fp);
  return (msg_size > 0 ? -1 : 0);
}

int
read_error_file2 (char *err_file, char *err_buf, int err_buf_size,
		  int *err_code)
{
  FILE *fp;
  char buf[1024];
  int msg_size = 0;
  char rm_prev_flag = 0;
  int found = 0;
  int success = 1;

  if (err_buf == NULL)
    return 0;

  err_buf[0] = 0;

  fp = fopen (err_file, "r");
  if (fp == NULL)
    {
      *err_code = 0;
      return 0;			/* not found error file */
    }

  while (1)
    {
      char *p = NULL;
      size_t len;
      if (fgets (buf, sizeof (buf), fp) == NULL)
	{
	  break;
	}

      /* start with "ERROR: " */
      len = strlen (buf);
      if (len > 7 && memcmp (buf, "ERROR: ", 7) == 0)
	{
	  /* ignore a newline character if it exists */
	  if (buf[len - 1] == '\n')
	    {
	      len--;
	    }
	  len -= 7;

	  if (len >= (size_t) err_buf_size)
	    {
	      len = (size_t) err_buf_size - 1;
	    }

	  memcpy (err_buf, buf + 7, len);
	  err_buf[len] = 0;

	  success = 0;
	  continue;
	}

      /* find "CODE = " */
      p = strstr (buf, "CODE = ");
      if (p != NULL)
	{
	  if (sscanf (p, "CODE = %d", err_code) != 1)
	    {
	      continue;
	    }

	  success = 0;
	  found = 1;

	  /* read error description */
	  if (fgets (buf, sizeof (buf), fp) == NULL)
	    {
	      break;
	    }

	  len = strlen (buf);
	  if (len > 0 && buf[len - 1] == '\n')
	    {
	      len--;
	    }

	  if (len >= (size_t) err_buf_size)
	    {
	      len = (size_t) err_buf_size - 1;
	    }

	  memcpy (err_buf, buf, len);
	  err_buf[len] = 0;
	}
    }

  if (success != 0)
    {
      *err_code = 0;
      return 0;
    }
  else if (found == 0)
    {
      *err_code = -1;
    }

  return -1;
}

static T_SPACEDB_RESULT *
new_spacedb_result (void)
{
  T_SPACEDB_RESULT *res;

  res = (T_SPACEDB_RESULT *) malloc (sizeof (T_SPACEDB_RESULT));
  if (res == NULL)
    return NULL;
  memset (res, 0, sizeof (T_SPACEDB_RESULT));
  return res;
}

static T_CMD_RESULT *
new_cmd_result (void)
{
  T_CMD_RESULT *res;

  res = (T_CMD_RESULT *) malloc (sizeof (T_CMD_RESULT));
  if (res == NULL)
    return NULL;
  memset (res, 0, sizeof (T_CMD_RESULT));
  return res;
}

static const char *
get_cubrid_mode_opt (T_CUBRID_MODE mode)
{
  if (mode == CUBRID_MODE_SA)
    return ("--" CSQL_SA_MODE_L);

  return ("--" CSQL_CS_MODE_L);
}

static int
parse_volume_line (T_SPACEDB_INFO * vol_info, char *str_buf)
{
  int volid, total_page, free_page;
  char purpose[128], vol_name[PATH_MAX];
  char *token = NULL;

  volid = total_page = free_page = 0;
  purpose[0] = vol_name[0] = '\0';

  token = strtok (str_buf, " ");
  if (token == NULL)
    {
      return FALSE;
    }
  volid = atoi (token);

  token = strtok (NULL, " ");
  if (token == NULL)
    {
      return FALSE;
    }
  strcpy (purpose, token);

  if (strcmp (purpose, "GENERIC") != 0 && strcmp (purpose, "DATA") != 0
      && strcmp (purpose, "INDEX") != 0 && strcmp (purpose, "TEMP") != 0)
    {
      return FALSE;
    }

  token = strtok (NULL, " ");
  if (token == NULL)
    {
      return FALSE;
    }
  total_page = atoi (token);

  token = strtok (NULL, " ");
  if (token == NULL)
    {
      return FALSE;
    }
  free_page = atoi (token);

  token = strtok (NULL, "\n");
  if (token == NULL)
    {
      return FALSE;
    }
  strcpy (vol_name, token + 1);

  set_spacedb_info (vol_info, volid, purpose, total_page,
		    free_page, vol_name);

  return TRUE;

}

static void
read_spacedb_output (T_SPACEDB_RESULT * res, char *out_file)
{
  FILE *fp;
  char str_buf[1024];
  int db_page_size = 0, log_page_size = 0;
  int num_vol = 0, num_tmp_vol = 0;
  T_SPACEDB_INFO *vol_info = NULL, *tmp_vol_info = NULL;

  fp = fopen (out_file, "r");
  if (fp == NULL)
    return;

  vol_info = (T_SPACEDB_INFO *) malloc (sizeof (T_SPACEDB_INFO));
  tmp_vol_info = (T_SPACEDB_INFO *) malloc (sizeof (T_SPACEDB_INFO));
  if (vol_info == NULL || tmp_vol_info == NULL)
    goto spacedb_error;

  while (fgets (str_buf, sizeof (str_buf), fp))
    {
      char *tmp_p;

      ut_trim (str_buf);
      if (strncmp (str_buf, "Space", 5) == 0)
	{
	  tmp_p = strstr (str_buf, "pagesize");
	  if (tmp_p == NULL)
	    {
	      goto spacedb_error;
	    }
	  db_page_size = atoi (tmp_p + 9);

	  tmp_p = strstr (str_buf, "log pagesize:");
	  if (tmp_p != NULL)
	    {
	      log_page_size = atoi (tmp_p + 13);
	    }
	  else
	    {
	      /* log pagesize default value */
	      log_page_size = 4096;
	    }
	}

      else if (strncmp (str_buf, "Volid", 5) == 0)
	{
	  break;
	}
    }

  while (fgets (str_buf, sizeof (str_buf), fp))
    {
      ut_trim (str_buf);
      if (str_buf[0] == '\0' || str_buf[0] == '-')
	{
	  continue;
	}
      if (strncmp (str_buf, "Volid", 5) == 0)
	{
	  break;
	}

      if (strncmp (str_buf, "Space", 5) == 0)
	{
	  continue;
	}

      if (!parse_volume_line (&(vol_info[num_vol]), str_buf))
	{
	  continue;
	}

      num_vol++;
      vol_info =
	(T_SPACEDB_INFO *) realloc (vol_info,
				    sizeof (T_SPACEDB_INFO) * (num_vol + 1));
      if (vol_info == NULL)
	goto spacedb_error;
    }

  while (fgets (str_buf, sizeof (str_buf), fp))
    {
      ut_trim (str_buf);
      if (str_buf[0] == '\0' || str_buf[0] == '-')
	{
	  continue;
	}
      if (strncmp (str_buf, "Volid", 5) == 0)
	{
	  break;
	}

      if (!parse_volume_line (&(tmp_vol_info[num_tmp_vol]), str_buf))
	{
	  continue;
	}

      num_tmp_vol++;
      tmp_vol_info =
	(T_SPACEDB_INFO *) realloc (tmp_vol_info,
				    sizeof (T_SPACEDB_INFO) * (num_tmp_vol +
							       1));
      if (tmp_vol_info == NULL)
	goto spacedb_error;
    }

  fclose (fp);

  res->page_size = db_page_size;
  res->log_page_size = log_page_size;
  res->num_vol = num_vol;
  res->num_tmp_vol = num_tmp_vol;
  res->vol_info = vol_info;
  res->tmp_vol_info = tmp_vol_info;

  return;

spacedb_error:
  fclose (fp);
  if (tmp_vol_info)
    free (tmp_vol_info);
  if (vol_info)
    free (vol_info);
}

static void
set_spacedb_info (T_SPACEDB_INFO * vol_info, int volid, char *purpose,
		  int total_page, int free_page, char *vol_name)
{
  char *p;
  struct stat statbuf;

  vol_info->volid = volid;
  strcpy (vol_info->purpose, purpose);
  vol_info->total_page = total_page;
  vol_info->free_page = free_page;

#if defined(WINDOWS)
  unix_style_path (vol_name);
#endif

  p = strrchr (vol_name, '/');
  if (p == NULL)
    {
      vol_info->location[0] = '\0';
      vol_info->vol_name[0] = '\0';
    }
  else
    {
      *p = '\0';
      snprintf (vol_info->location, sizeof (vol_info->location) - 1, "%s",
		vol_name);
      snprintf (vol_info->vol_name, sizeof (vol_info->vol_name) - 1, "%s",
		p + 1);
      *p = '/';
    }

  stat (vol_name, &statbuf);
  vol_info->date = statbuf.st_mtime;
}

static int
read_start_server_output (char *stdout_file, char *stderr_file,
			  char *_dbmt_error)
{
  FILE *fp, *fp2;
  char buf[1024];
  char *strp;
  int retval = 0;

  if (access (stdout_file, F_OK) == 0)
    {
      fp = fopen (stdout_file, "r");
      if (fp != NULL)
	{
	  while (fgets (buf, sizeof (buf), fp) != NULL)
	    {
	      if (strncmp (buf, "++", 2) == 0)
		{
		  if ((strp = strchr (buf, ':')) && strstr (strp, "fail"))
		    {
		      retval = -1;
		      break;
		    }
		}
	    }
	  fclose (fp);
	}
    }

  if (access (stderr_file, F_OK) == 0)
    {
      fp2 = fopen (stderr_file, "r");
      if (fp2 != NULL)
	{
	  int len = 0;
	  while (fgets (buf, sizeof (buf), fp2) != NULL)
	    {
	      ut_trim (buf);
	      len += strlen (buf);
	      if (len < (DBMT_ERROR_MSG_SIZE - 1))
		{
		  strcpy (_dbmt_error, buf);
		  _dbmt_error += len;
		}
	      else
		{
		  strcpy_limit (_dbmt_error, buf, DBMT_ERROR_MSG_SIZE);
		  strcpy_limit (_dbmt_error + DBMT_ERROR_MSG_SIZE - 4, "...",
				4);
		  break;
		}
	    }

	  if (len != 0 && retval != -1)
	    retval = 1;
	  fclose (fp2);
	}
    }

  return retval;
}
