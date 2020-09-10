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
 * jsp_file.c - Java Stored Procedure Server Module Header
 *
 * Note:
 */

#include "jsp_file.h"

#include "porting.h"
#include "environment_variable.h"

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>

bool
javasp_get_info_dir ()
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

bool
javasp_get_info_file (char *buf, size_t len, const char *db_name)
{
  char javasp_vardir[PATH_MAX];
  envvar_vardir_file (javasp_vardir, sizeof (javasp_vardir), "javasp");

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
  envvar_logdir_file (javasp_logdir, sizeof (javasp_logdir), "javasp");

  if (snprintf (buf, len, "%s/javasp_%s.err", javasp_logdir, db_name) < 0)
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
  envvar_logdir_file (javasp_logdir, sizeof (javasp_logdir), "javasp");

  if (snprintf (buf, len, "%s/javasp_%s.log", javasp_logdir, db_name) < 0)
    {
      assert (false);
      buf[0] = '\0';
      return false;
    }
  return true;
}

JAVASP_SERVER_INFO
javasp_read_info (const char *info_path)
{
  FILE *fp;
  JAVASP_SERVER_INFO info = { -1, -1 };

  fp = fopen (info_path, "r");
  if (fp)
    {
      fscanf (fp, "%d %d", &info.pid, &info.port);
      fclose (fp);
    }
  return info;
}

bool
javasp_write_info (const char *info_path, JAVASP_SERVER_INFO info)
{
  FILE *fp;
  fp = fopen (info_path, "w");
  if (fp)
    {
      fprintf (fp, "%d %d", info.pid, info.port);
      fclose (fp);
      return true;
    }
  return false;
}
