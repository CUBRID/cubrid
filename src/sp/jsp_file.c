/*
 * Copyright 2008 Search Solution Corporation
 * Copyright 2016 CUBRID Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
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
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

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

void
javasp_unlink_info (const char *db_name)
{
  char file_name[PATH_MAX] = { 0 };
  char file_path[PATH_MAX] = { 0 };

  snprintf (file_name, PATH_MAX, "javasp/javasp_%s.info", db_name);
  envvar_vardir_file (file_path, PATH_MAX, file_name);

  unlink (file_path);
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
  bool result = false;
  FILE *fp = NULL;

  fp = javasp_open_info (db_name, "w+");
  if (fp)
    {
      fprintf (fp, "%d %d", info.pid, info.port);
      fclose (fp);
      result = true;
    }
  return result;
}

bool
javasp_reset_info (const char *db_name)
{
  JAVASP_SERVER_INFO reset_info = JAVASP_SERVER_INFO_INITIALIZER;
  return javasp_write_info (db_name, reset_info);
}
