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

#if defined(WINDOWS)
#define SLEEP_SEC(X)                    Sleep((X) * 1000)
#define SLEEP_MILISEC(sec, msec)        Sleep((sec) * 1000 + (msec))
#else
#define SLEEP_SEC(X)                    sleep(X)
#define SLEEP_MILISEC(sec, msec)        \
	do {            \
		struct timeval sleep_time_val;                \
		sleep_time_val.tv_sec = sec;                  \
		sleep_time_val.tv_usec = (msec) * 1000;       \
		select(0, 0, 0, 0, &sleep_time_val);          \
	} while(0)
#endif

static FILE *fopen_and_lock (const char *path, const char *mode);

/*
 * fopen_and_lock ()
 * path (in) :
 *
 */
static FILE *
fopen_and_lock (const char *path, const char *mode)
{
#define MAX_RETRY_COUNT 10

  int retry_count = 0;
  FILE *fp;

retry:
  fp = fopen (path, mode);
  if (fp != NULL)
    {
      if (lockf (fileno (fp), F_TLOCK, 0) < 0)
	{
	  fclose (fp);

	  if (retry_count < MAX_RETRY_COUNT)
	    {
	      SLEEP_MILISEC (0, 100);
	      retry_count++;
	      goto retry;
	    }

	  return NULL;
	}
    }

  return fp;
}

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
  char file_name[PATH_MAX] = { 0 };
  char file_path[PATH_MAX] = { 0 };

  snprintf (file_name, PATH_MAX, "javasp/javasp_%s.info", db_name);
  envvar_vardir_file (file_path, PATH_MAX, file_name);

  FILE *fp = fopen_and_lock (file_path, mode);
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
// *INDENT-OFF*
  JAVASP_SERVER_INFO reset_info {-1, -1};
// *INDENT-ON*
  return javasp_write_info (db_name, reset_info);
}
