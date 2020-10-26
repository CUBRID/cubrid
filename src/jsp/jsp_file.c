/*
 * Copyright (C) 2008 Search Solution Corporation
 * Copyright (C) 2016 CUBRID Corporation
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
 * jsp_file.c - Functions to manage files related to Java Stored Procedure Server
 *
 * Note:
 */

#if defined (WINDOWS)
#include <io.h>
#endif

#include "jsp_file.h"

#include "porting.h"
#include "environment_variable.h"

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>

bool
javasp_open_info_dir ()
{
  char javasp_dir[PATH_MAX];
  envvar_vardir_file (javasp_dir, sizeof (javasp_dir), "javasp");

  if (access (javasp_dir, F_OK) < 0)
    {
      /* create directory if not exist */
      if (mkdir (javasp_dir, 0777) < 0 && errno == ENOENT)
	{
	  char pdir[PATH_MAX];

	  if (cub_dirname_r (javasp_dir, pdir, PATH_MAX) > 0 && access (pdir, F_OK) < 0)
	    {
	      mkdir (pdir, 0777);
	    }
	}
    }

  if (access (javasp_dir, F_OK) < 0)
    {
      if (mkdir (javasp_dir, 0777) < 0)
	{
	  return false;
	}
    }

  return true;
}

FILE *
javasp_open_info (const char *db_name, const char *mode)
{
  FILE *fp = NULL;
  char file_name[PATH_MAX] = { 0 };
  char file_path[PATH_MAX] = { 0 };

  snprintf (file_name, PATH_MAX, "javasp/javasp_%s.info", db_name);
  envvar_vardir_file (file_path, PATH_MAX, file_name);

  fp = fopen (file_path, mode);

  return fp;
}

bool
javasp_get_info_file (char *buf, size_t len, const char *db_name)
{
  char javasp_vardir[PATH_MAX];
  envvar_vardir_file (javasp_vardir, PATH_MAX, "javasp");

  if (snprintf (buf, len, "%s/javasp_%s.info", javasp_vardir, db_name) < 0)
    {
      assert (false);
      buf[0] = '\0';
      return false;
    }
  return true;
}

bool
javasp_get_error_file (char *buf, size_t len, const char *db_name)
{
  char javasp_logdir[PATH_MAX];
  envvar_logdir_file (javasp_logdir, sizeof (javasp_logdir), "");

  if (snprintf (buf, len, "%s/%s_java.err", javasp_logdir, db_name) < 0)
    {
      assert (false);
      buf[0] = '\0';
      return false;
    }
  return true;
}

bool
javasp_get_log_file (char *buf, size_t len, const char *db_name)
{
  char javasp_logdir[PATH_MAX];
  envvar_logdir_file (javasp_logdir, sizeof (javasp_logdir), "");

  if (snprintf (buf, len, "%s/%s_java.log", javasp_logdir, db_name) < 0)
    {
      assert (false);
      buf[0] = '\0';
      return false;
    }
  return true;
}

bool
javasp_read_info (const char *db_name, JAVASP_SERVER_INFO & info)
{
  FILE *fp = NULL;

  fp = javasp_open_info (db_name, "r");
  if (fp)
    {
      fscanf (fp, "%d %d", &info.pid, &info.port);
      fclose (fp);
      return true;
    }

  return false;
}

bool
javasp_write_info (const char *db_name, JAVASP_SERVER_INFO info)
{
  FILE *fp = NULL;

  fp = javasp_open_info (db_name, "w");
  if (fp)
    {
      fprintf (fp, "%d %d", info.pid, info.port);
      fclose (fp);
      return true;
    }
  return false;
}

bool
javasp_reset_info (const char *db_name)
{
  JAVASP_SERVER_INFO reset_info
  {
  -1, -1};
  return javasp_write_info (db_name, reset_info);
}
