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

static char *make_error_log_filename (char *filename_buf, size_t buf_size, const char *br_name);
static void cas_error_log_backup (void);
static void cas_log_write_internal (const char *fmt, ...);

static FILE *error_log_fp = NULL;
static char error_log_filepath[BROKER_PATH_MAX];
static long saved_error_log_fpos = 0;

static int eid = 0;

static char *
make_error_log_filename (char *filename_buf, size_t buf_size, const char *br_name)
{
  char dirname[BROKER_PATH_MAX];

  assert (filename_buf != NULL);

  get_cubrid_file (FID_CUBRID_ERR_DIR, dirname, BROKER_PATH_MAX);

  if (cas_shard_flag == ON)
    {
      snprintf (filename_buf, buf_size, "%s%s_CAS_%d_%d_%d.err", dirname, br_name, shm_proxy_id + 1, shm_shard_id,
		shm_shard_cas_id + 1);
    }
  else
    {
      snprintf (filename_buf, buf_size, "%s%s_CAS_%d.err", dirname, br_name, shm_as_index + 1);
    }
  return filename_buf;
}


void
cas_error_log_open (char *br_name)
{
  if (error_log_fp != NULL)
    {
      cas_error_log_close (true);
    }

  if (br_name != NULL)
    {
      make_error_log_filename (error_log_filepath, BROKER_PATH_MAX, br_name);
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
  char backup_filepath[BROKER_PATH_MAX];

  assert (error_log_filepath[0] != '\0');

  snprintf (backup_filepath, BROKER_PATH_MAX, "%s.bak", error_log_filepath);

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
  n = ut_time_string (p, NULL);
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
  cas_log_write_internal (" DBMS ERROR [ERR_CODE : %d, ERR_MSG : %s] EID = %d", dbms_errno,
			  (dbms_errmsg != NULL) ? dbms_errmsg : "-", ++eid);
  fputc ('\n', error_log_fp);

  saved_error_log_fpos = ftell (error_log_fp);

  if ((saved_error_log_fpos / 1000) > shm_appl->sql_log_max_size)
    {
      cas_error_log_close (true);
      cas_error_log_backup ();
      cas_error_log_open (NULL);
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
