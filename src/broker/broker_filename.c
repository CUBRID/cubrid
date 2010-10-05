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
 * broker_filename.c -
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(WINDOWS)
#include <direct.h>
#else
#include <unistd.h>
#endif

#include "broker_filename.h"
#include "environment_variable.h"

#ifndef PATH_MAX
#define PATH_MAX 256
#endif

static char cubrid_Dir[PATH_MAX] = "";

#define NUM_CUBRID_FILE		17
static T_CUBRID_FILE_INFO cubrid_file[NUM_CUBRID_FILE] = {
  {FID_CUBRID_BROKER_CONF, ""},
  {FID_UV_ERR_MSG, ""},
  {FID_V3_OUTFILE_DIR, ""},
  {FID_CAS_TMPGLO_DIR, ""},
  {FID_CAS_TMP_DIR, ""},
  {FID_VAR_DIR, ""},
  {FID_SOCK_DIR, ""},
  {FID_AS_PID_DIR, ""},
  {FID_ADMIND_PID, ""},
  {FID_SQL_LOG_DIR, ""},
  {FID_SQL_LOG2_DIR, ""},
  {FID_ADMIND_LOG, ""},
  {FID_MONITORD_LOG, ""},
  {FID_ER_HTML, ""},
  {FID_CUBRID_ERR_DIR, ""},
  {FID_CAS_FOR_ORACLE_DBINFO, ""},
  {FID_CAS_FOR_MYSQL_DBINFO, ""}
};

void
set_cubrid_home ()
{
  char dirname[PATH_MAX];
  const char *p;

  p = getenv_cubrid_broker ();
  if (p)
    {
      strcpy (cubrid_Dir, p);
      return;
    }
  getcwd (dirname, sizeof (dirname));
  snprintf (cubrid_Dir, sizeof (cubrid_Dir) - 1, "%s/..", dirname);
}

char *
get_cubrid_home ()
{
  if (cubrid_Dir[0] == '\0')
    {
      set_cubrid_home ();
    }
  return cubrid_Dir;
}


void
set_cubrid_file (T_CUBRID_FILE_ID fid, char *value)
{
  int value_len;
  bool repath = true;

  if (value == NULL)
    {
      return;
    }

  if (value[0] == '/')
    {
      repath = false;
    }
#if defined (WINDOWS)
  if (isalpha (value[0]) && value[1] == ':')
    {
      repath = false;
    }
#endif


  switch (fid)
    {
    case FID_V3_OUTFILE_DIR:
    case FID_CAS_TMPGLO_DIR:
    case FID_CAS_TMP_DIR:
    case FID_VAR_DIR:
    case FID_SOCK_DIR:
    case FID_AS_PID_DIR:
    case FID_SQL_LOG_DIR:
    case FID_SQL_LOG2_DIR:
    case FID_CUBRID_ERR_DIR:
      value_len = strlen (value);
      if (value[value_len] == '/' || value[value_len] == '\\')
	{
	  if (repath)
	    {
	      snprintf (cubrid_file[fid].file_name, PATH_MAX, "%s/%s",
			get_cubrid_home (), value);
	    }
	  else
	    {
	      snprintf (cubrid_file[fid].file_name, PATH_MAX, value);
	    }
	}
      else
	{
	  if (repath)
	    {
	      snprintf (cubrid_file[fid].file_name, PATH_MAX, "%s/%s/",
			get_cubrid_home (), value);
	    }
	  else
	    {
	      snprintf (cubrid_file[fid].file_name, PATH_MAX, "%s/", value);
	    }
	}
      break;
    default:
      if (repath)
	{
	  snprintf (cubrid_file[fid].file_name, PATH_MAX, "%s/%s",
		    get_cubrid_home (), value);
	}
      else
	{
	  snprintf (cubrid_file[fid].file_name, PATH_MAX, value);
	}
      break;
    }
}

char *
get_cubrid_file (T_CUBRID_FILE_ID fid, char *buf)
{
  buf[0] = '\0';

  if (cubrid_Dir[0] == '\0')
    {
      set_cubrid_home ();
    }

  if (strlen (cubrid_file[fid].file_name) > 0)
    {
      snprintf (buf, PATH_MAX, cubrid_file[fid].file_name);
      return buf;
    }

  switch (fid)
    {
    case FID_CUBRID_BROKER_CONF:
      envvar_confdir_file (buf, PATH_MAX, "cubrid_broker.conf");
      snprintf (cubrid_file[fid].file_name, PATH_MAX, buf);
      break;
    case FID_UV_ERR_MSG:
      envvar_confdir_file (buf, PATH_MAX, "uv_er.msg");
      snprintf (cubrid_file[fid].file_name, PATH_MAX, buf);
      break;
    case FID_V3_OUTFILE_DIR:
      envvar_tmpdir_file (buf, PATH_MAX, "");
      snprintf (cubrid_file[fid].file_name, PATH_MAX, buf);
      break;
    case FID_CAS_TMPGLO_DIR:
      envvar_tmpdir_file (buf, PATH_MAX, "");
      snprintf (cubrid_file[fid].file_name, PATH_MAX, buf);
      break;
    case FID_CAS_TMP_DIR:
      envvar_tmpdir_file (buf, PATH_MAX, "");
      snprintf (cubrid_file[fid].file_name, PATH_MAX, buf);
      break;
    case FID_VAR_DIR:
      envvar_vardir_file (buf, PATH_MAX, "");
      snprintf (cubrid_file[fid].file_name, PATH_MAX, buf);
      break;
    case FID_SOCK_DIR:
      envvar_vardir_file (buf, PATH_MAX, "CUBRID_SOCK/");
      snprintf (cubrid_file[fid].file_name, PATH_MAX, buf);
      break;
    case FID_AS_PID_DIR:
      envvar_vardir_file (buf, PATH_MAX, "as_pid/");
      snprintf (cubrid_file[fid].file_name, PATH_MAX, buf);
      break;
    case FID_ADMIND_PID:
      envvar_vardir_file (buf, PATH_MAX, "as_pid/casd.pid");
      snprintf (cubrid_file[fid].file_name, PATH_MAX, buf);
      break;
    case FID_SQL_LOG_DIR:
      envvar_logdir_file (buf, PATH_MAX, "broker/sql_log/");
      snprintf (cubrid_file[fid].file_name, PATH_MAX, buf);
      break;
    case FID_SQL_LOG2_DIR:
      envvar_logdir_file (buf, PATH_MAX, "broker/sql_log/query/");
      snprintf (cubrid_file[fid].file_name, PATH_MAX, buf);
      break;
    case FID_ADMIND_LOG:
      envvar_logdir_file (buf, PATH_MAX, "broker/sql_log/cas_admind.log");
      snprintf (cubrid_file[fid].file_name, PATH_MAX, buf);
      break;
    case FID_MONITORD_LOG:
      envvar_logdir_file (buf, PATH_MAX, "broker/sql_log/cas_monitord.log");
      snprintf (cubrid_file[fid].file_name, PATH_MAX, buf);
      break;
    case FID_ER_HTML:
      envvar_confdir_file (buf, PATH_MAX, "uw_er.html");
      snprintf (cubrid_file[fid].file_name, PATH_MAX, buf);
      break;
    case FID_CUBRID_ERR_DIR:
      envvar_logdir_file (buf, PATH_MAX, "broker/error_log/");
      snprintf (cubrid_file[fid].file_name, PATH_MAX, buf);
      break;
    case FID_CAS_FOR_ORACLE_DBINFO:
      envvar_confdir_file (buf, PATH_MAX, "databases_oracle.txt");
      snprintf (cubrid_file[fid].file_name, PATH_MAX, buf);
      break;
    case FID_CAS_FOR_MYSQL_DBINFO:
      envvar_confdir_file (buf, PATH_MAX, "databases_mysql.txt");
      snprintf (cubrid_file[fid].file_name, PATH_MAX, buf);
      break;
    default:
      break;
    }

  return buf;
}

const char *
getenv_cubrid_broker ()
{
  char *p;

  p = envvar_root ();

  return p;
}
