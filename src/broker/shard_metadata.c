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
 * shard_metadata.c -
 */

#ident "$Id$"


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <assert.h>

#if defined(WINDOWS)
#include <direct.h>
#endif /* WINDOWS */

#include "dbi.h"

#if !defined(WINDOWS)
#include "dlfcn.h"
#endif /* WINDOWS */

#include "cas_common.h"
#include "broker_filename.h"
#include "broker_admin_pub.h"
#include "broker_util.h"
#include "shard_metadata.h"

#define SHARD_QUERY_BUFFER_SIZE			1024

#define SHARD_METADATA_USER_TABLE_NAME 		"shard_user"
#define SHARD_METADATA_KEY_RANGE_TABLE_NAME 	"shard_range"
#define SHARD_METADATA_CONN_TABLE_NAME 		"shard_conn"

#define DEFAULT_NUM_USER		1
#define DEFAULT_NUM_KEY			1
#define DEFAULT_NUM_CONN		4


FN_GET_SHARD_KEY fn_get_shard_key = NULL;

#if defined(WINDOWS)
HMODULE handle = NULL;
#else
void *handle = NULL;
#endif

#if defined(CAS_FOR_ORACLE) || defined(CAS_FOR_MYSQL)
extern char *envvar_confdir_file (char *path, size_t size, const char *filename);
#endif /* CAS_FOR_ORACLE || CAS_FOR_MYSQL */

static void shard_println_1 (FILE * fp);
static void shard_println_2 (FILE * fp);

static int shard_metadata_read_user (T_SHM_PROXY * shm_proxy_p, char *db_name, char *db_user, char *db_password);
static int shard_metadata_read_key (const char *filename, T_SHM_PROXY * shm_proxy_p);
static int shard_metadata_read_conn (const char *filename, T_SHM_PROXY * shm_proxy_p);
static int shard_metadata_key_range_comp (const void *p1, const void *p2);
static int shard_metadata_conn_comp (const void *p1, const void *p2);
static void shard_metadata_sort_key (T_SHM_SHARD_KEY * shm_key_p);
static void shard_metadata_sort_conn (T_SHM_SHARD_CONN * shm_conn_p);


static void shard_metadata_dump_user (FILE * fp, T_SHM_SHARD_USER * shm_user_p);
static void shard_metadata_dump_key (FILE * fp, T_SHM_SHARD_KEY * shm_key_p);
static void shard_metadata_dump_conn (FILE * fp, T_SHM_SHARD_CONN * shm_conn_p);
static int shard_metadata_validate (T_BROKER_INFO * br_info_p, T_SHM_PROXY * shm_proxy_p);
static int shard_metadata_validate_user (T_SHM_SHARD_USER * shm_user_p);
static int shard_metadata_validate_key_range_internal (T_SHARD_KEY * key_p, T_SHM_SHARD_CONN * shm_conn_p, int modular);
static int shard_metadata_validate_key (T_SHM_SHARD_KEY * shm_key_p, T_SHM_SHARD_CONN * shm_conn_p, int modular);
static int shard_metadata_validate_conn (T_SHM_SHARD_CONN * shm_conn_p);

static int shard_metadata_validate_key_function (const char *library_name, const char *function_name);

static void
shard_println_1 (FILE * fp)
{
  assert (fp);

  fprintf (fp, "========================================" "========================================\n");
}

static void
shard_println_2 (FILE * fp)
{
  assert (fp);

  fprintf (fp, "----------------------------------------" "----------------------------------------\n");
}

static int
shard_metadata_read_user (T_SHM_PROXY * shm_proxy_p, char *db_name, char *db_user, char *db_password)
{
  int max_user;

  T_SHM_SHARD_USER *shm_user_p = NULL;
  T_SHARD_USER *user_p = NULL;

  shm_user_p = shard_metadata_get_user (shm_proxy_p);

  max_user = 1;			/* only one user */
  shm_user_p->num_shard_user = max_user;

  user_p = &(shm_user_p->shard_user[0]);
  strncpy_bufsize (user_p->db_name, db_name);
  strncpy_bufsize (user_p->db_user, db_user);
  strncpy_bufsize (user_p->db_password, db_password);

  SHARD_INF ("<USERINFO> [%d] db_name:[%s], " "db_user:[%s], db_password:[%s]\n", 0, user_p->db_name, user_p->db_user,
	     user_p->db_password);

  return 0;
}

/*
 * return :key_column count
 */
static int
shard_metadata_read_key (const char *filename, T_SHM_PROXY * shm_proxy_p)
{
  int nargs;
  int idx_key, idx_range, max_key;
  char path[BROKER_PATH_MAX];
  char line[LINE_MAX], *p;
  char section[LINE_MAX];
  int len;
  FILE *file = NULL;

  char key_column[SHARD_KEY_COLUMN_LEN];

  T_SHM_SHARD_KEY *shm_key_p = NULL;
  T_SHARD_KEY *key_p = NULL;
  T_SHARD_KEY_RANGE *range_p = NULL;

  shm_key_p = shard_metadata_get_key (shm_proxy_p);

  envvar_confdir_file (path, BROKER_PATH_MAX, filename);

  file = fopen (path, "r");
  if (file == NULL)
    {
      goto error_return;
    }

  max_key = DEFAULT_NUM_KEY;

  shm_key_p->num_shard_key = 0;

  idx_key = idx_range = 0;
  key_column[0] = '\0';
  while (fgets (line, LINE_MAX - 1, file) != NULL)
    {
      trim (line);

      p = strchr (line, '#');
      if (p)
	{
	  *p = '\0';
	}

      len = (int) strlen (line);
      if (line[0] == '\0' || len <= 0)
	{
	  continue;
	}
      else if (line[0] == '[' && line[len - 1] == ']')
	{
	  nargs = sscanf (line, "[%%%[^]]", section);
	  if (nargs == 1)
	    {
	      trim (section);
	      if (strncasecmp (section, key_column, SHARD_KEY_COLUMN_LEN) != 0)
		{
		  strncpy_bufsize (key_column, section);

		  shm_key_p->num_shard_key++;
		  idx_key = shm_key_p->num_shard_key - 1;
		  idx_range = 0;
		}
	    }
	  continue;
	}

      if (shm_key_p->num_shard_key > max_key)
	{
#if 0				/* multiple shard key hint : not implemented yet */
	  max_key += DEFAULT_NUM_KEY;
	  shard_metadata_key_resize (&shm_key_p, max_key);
	  if (shm_key_p == NULL)
	    {
	      goto error_return;
	    }
#else
	  shm_key_p->num_shard_key = max_key;
	  break;
#endif
	}

      if (key_column[0] == '\0')
	{
	  continue;
	}

      key_p = &(shm_key_p->shard_key[idx_key]);
      strncpy_bufsize (key_p->key_column, key_column);

      if (idx_range >= SHARD_KEY_RANGE_MAX)
	{
	  continue;
	}

      assert (idx_range >= 0 && idx_range < SHARD_KEY_RANGE_MAX);
      range_p = (T_SHARD_KEY_RANGE *) (&key_p->range[idx_range]);
      nargs = sscanf (line, "%d %d %d", &range_p->min, &range_p->max, &range_p->shard_id);

      range_p->key_index = idx_key;
      range_p->range_index = idx_range;

      if (nargs != 3)
	{
	  continue;
	}

      SHARD_INF ("<KEYINFO> [%d:%d] key_column:%s, " "min:%d, max:%d, shard_id:%d. \n", idx_key, idx_range,
		 key_p->key_column, range_p->min, range_p->max, range_p->shard_id);

      key_p->num_key_range = ++idx_range;
    }

  return 0;

error_return:
  if (file != NULL)
    {
      fclose (file);
    }

  return -1;
}

static int
shard_metadata_read_conn (const char *filename, T_SHM_PROXY * shm_proxy_p)
{
  int nargs;
  int idx_conn, max_conn;
  char line[LINE_MAX], *p;
  char path[BROKER_PATH_MAX];
  int len;
  FILE *file = NULL;

  T_SHM_SHARD_CONN *shm_conn_p = NULL;
  T_SHARD_CONN *conn_p = NULL;

  shm_conn_p = shard_metadata_get_conn (shm_proxy_p);

  envvar_confdir_file (path, BROKER_PATH_MAX, filename);

  file = fopen (path, "r");
  if (file == NULL)
    {
      goto error_return;
    }

  max_conn = DEFAULT_NUM_CONN;

  shm_conn_p->num_shard_conn = 0;

  idx_conn = 0;
  while (fgets (line, LINE_MAX - 1, file) != NULL)
    {
      trim (line);

      p = strchr (line, '#');
      if (p)
	{
	  *p = '\0';
	}

      len = (int) strlen (line);
      if (line[0] == '\0')
	{
	  continue;
	}

      if (idx_conn >= MAX_SHARD_CONN)
	{
	  goto error_return;
	}

      assert (idx_conn >= 0);
      conn_p = &(shm_conn_p->shard_conn[idx_conn]);
      nargs = sscanf (line, "%d %s %[^\n]", &conn_p->shard_id, conn_p->db_name, conn_p->db_conn_info);
      if (nargs != 3)
	{
	  continue;
	}

      trim (conn_p->db_conn_info);

      SHARD_INF ("<CONNINFO> [%d] shard_id:%d, db_name:<%s>, db_conn_info:<%s>.\n", idx_conn, conn_p->shard_id,
		 conn_p->db_name, conn_p->db_conn_info);

      shm_conn_p->num_shard_conn = ++idx_conn;
    }

  return 0;

error_return:
  if (file != NULL)
    {
      fclose (file);
    }

  return -1;
}

static int
shard_metadata_key_range_comp (const void *p1, const void *p2)
{
  T_SHARD_KEY_RANGE *arg1 = (T_SHARD_KEY_RANGE *) p1;
  T_SHARD_KEY_RANGE *arg2 = (T_SHARD_KEY_RANGE *) p2;

  if (arg1->min > arg2->min)
    {
      return 1;
    }
  else if (arg1->min == arg2->min)
    {
      return 0;
    }
  else
    {
      return -1;
    }
}

static int
shard_metadata_conn_comp (const void *p1, const void *p2)
{
  T_SHARD_CONN *arg1 = (T_SHARD_CONN *) p1;
  T_SHARD_CONN *arg2 = (T_SHARD_CONN *) p2;

  if (arg1->shard_id > arg2->shard_id)
    {
      return 1;
    }
  else if (arg1->shard_id == arg2->shard_id)
    {
      return 0;
    }
  else
    {
      return -1;
    }
}

static void
shard_metadata_sort_key (T_SHM_SHARD_KEY * shm_key_p)
{
  int i;
  T_SHARD_KEY *key_p;

  for (i = 0; i < shm_key_p->num_shard_key; i++)
    {
      key_p = &shm_key_p->shard_key[i];
      qsort ((void *) key_p->range, key_p->num_key_range, sizeof (T_SHARD_KEY_RANGE), shard_metadata_key_range_comp);
    }
  return;
}

static void
shard_metadata_sort_conn (T_SHM_SHARD_CONN * shm_conn_p)
{
  qsort ((void *) shm_conn_p->shard_conn, shm_conn_p->num_shard_conn, sizeof (T_SHARD_CONN), shard_metadata_conn_comp);
  return;
}


T_SHM_SHARD_USER *
shard_metadata_get_user (T_SHM_PROXY * shm_proxy_p)
{
  T_SHM_SHARD_USER *shm_user_p;

  assert (shm_proxy_p);

  shm_user_p = &shm_proxy_p->shm_shard_user;

  return (shm_user_p);
}

T_SHM_SHARD_KEY *
shard_metadata_get_key (T_SHM_PROXY * shm_proxy_p)
{
  T_SHM_SHARD_KEY *shm_key_p;

  assert (shm_proxy_p);

  shm_key_p = &shm_proxy_p->shm_shard_key;

  return (shm_key_p);
}

T_SHM_SHARD_CONN *
shard_metadata_get_conn (T_SHM_PROXY * shm_proxy_p)
{
  T_SHM_SHARD_CONN *shm_conn_p;

  assert (shm_proxy_p);

  shm_conn_p = &shm_proxy_p->shm_shard_conn;

  return (shm_conn_p);
}

int
shard_metadata_initialize (T_BROKER_INFO * br_info, T_SHM_PROXY * shm_proxy_p)
{
  int res = 0;

  assert (br_info);
  assert (shm_proxy_p);

  res =
    shard_metadata_read_user (shm_proxy_p, br_info->shard_db_name, br_info->shard_db_user, br_info->shard_db_password);

  res = shard_metadata_read_key (br_info->shard_key_file, shm_proxy_p);
  if (res < 0)
    {
      fprintf (stderr, "failed to read metadata key [%s]\n", br_info->name);
      return res;
    }

  shard_metadata_sort_key (&shm_proxy_p->shm_shard_key);
#if defined(SHARD_VERBOSE_DEBUG)
  shard_metadata_dump_key (stdout, &shm_proxy_p->shm_shard_key);
#endif

  res = shard_metadata_read_conn (br_info->shard_connection_file, shm_proxy_p);
  if (res < 0)
    {
      fprintf (stderr, "failed to read metadata connection [%s]\n", br_info->name);
      return res;
    }

  shard_metadata_sort_conn (&shm_proxy_p->shm_shard_conn);
#if defined(SHARD_VERBOSE_DEBUG)
  shard_metadata_dump_conn (stdout, &shm_proxy_p->shm_shard_conn);
#endif

  SHARD_INF ("num USER:[%d], num KEY:[%d], num CONN:[%d].\n", shm_user_p->num_shard_user, shm_key_p->num_shard_key,
	     shm_conn_p->num_shard_conn);

  res = shard_metadata_validate (br_info, shm_proxy_p);
  if (res < 0)
    {
      fprintf (stderr, "failed to metadata validate check [%s]\n", br_info->name);
      return res;
    }

  return res;
}

static void
shard_metadata_dump_user (FILE * fp, T_SHM_SHARD_USER * shm_user_p)
{
  int i;
  T_SHARD_USER *user_p;

  assert (fp);
  assert (shm_user_p);

  fprintf (fp, "%s=%d\n", "NUM_SHARD_USER", shm_user_p->num_shard_user);
  shard_println_1 (fp);
  fprintf (fp, "      %-15s %-20s\n", "DB_NAME", "DB_USER");
  shard_println_2 (fp);

  for (i = 0; i < shm_user_p->num_shard_user; i++)
    {
      user_p = (T_SHARD_USER *) (&(shm_user_p->shard_user[i]));
      fprintf (fp, "[%-3d] %-15s %-20s\n", i, user_p->db_name, user_p->db_user);
    }
  shard_println_1 (fp);
  fprintf (fp, "\n\n");

  return;
}

static void
shard_metadata_dump_key (FILE * fp, T_SHM_SHARD_KEY * shm_key_p)
{
  int i, j;
  T_SHARD_KEY *key_p;
  T_SHARD_KEY_RANGE *range_p;

  assert (fp);
  assert (shm_key_p);

  fprintf (fp, "%s=%d\n", "NUM_SHARD_KEY", shm_key_p->num_shard_key);
  shard_println_1 (fp);
  fprintf (fp, "          %-30s %-5s %-5s %-10s\n", "KEY", "MIN", "MAX", "SHARD_ID");
  shard_println_2 (fp);
  for (i = 0; i < shm_key_p->num_shard_key; i++)
    {
      key_p = (T_SHARD_KEY *) (&(shm_key_p->shard_key[i]));
      for (j = 0; j < key_p->num_key_range; j++)
	{
	  range_p = (T_SHARD_KEY_RANGE *) (&(key_p->range[j]));

	  fprintf (fp, "[%-3d|%-3d] %-30s %-5d %-5d %-10d\n", i, j, key_p->key_column, range_p->min, range_p->max,
		   range_p->shard_id);
	}
    }
  shard_println_1 (fp);
  fprintf (fp, "\n\n");

  return;
}

static void
shard_metadata_dump_conn (FILE * fp, T_SHM_SHARD_CONN * shm_conn_p)
{
  int i;
  T_SHARD_CONN *conn_p;

  assert (fp);
  assert (shm_conn_p);

  fprintf (fp, "%s=%d\n", "NUM_SHARD_CONN", shm_conn_p->num_shard_conn);
  shard_println_1 (fp);
  fprintf (fp, "      %-10s %-20s %-30s\n", "SHARD_ID", "DB_NAME", "DB_CONN_INFO");
  shard_println_2 (fp);
  for (i = 0; i < shm_conn_p->num_shard_conn; i++)
    {
      conn_p = (T_SHARD_CONN *) (&(shm_conn_p->shard_conn[i]));
      fprintf (fp, "[%-3d] %-10d %-20s %-30s\n", i, conn_p->shard_id, conn_p->db_name, conn_p->db_conn_info);
    }
  shard_println_1 (fp);
  fprintf (fp, "\n\n");

  return;
}

void
shard_metadata_dump_internal (FILE * fp, T_SHM_PROXY * shm_proxy_p)
{
  T_SHM_SHARD_USER *shm_user_p;
  T_SHM_SHARD_KEY *shm_key_p;
  T_SHM_SHARD_CONN *shm_conn_p;

  assert (shm_proxy_p);

  shm_user_p = shard_metadata_get_user (shm_proxy_p);
  if (shm_user_p)
    {
      shard_metadata_dump_user (fp, shm_user_p);
    }

  shm_key_p = shard_metadata_get_key (shm_proxy_p);
  if (shm_key_p)
    {
      shard_metadata_dump_key (fp, shm_key_p);
    }

  shm_conn_p = shard_metadata_get_conn (shm_proxy_p);
  if (shm_conn_p)
    {
      shard_metadata_dump_conn (fp, shm_conn_p);
    }

  return;
}

void
shard_metadata_dump (FILE * fp, int shmid)
{
  T_SHM_PROXY *shm_proxy_p = NULL;

  shm_proxy_p = (T_SHM_PROXY *) uw_shm_open (shmid, SHM_PROXY, SHM_MODE_MONITOR);
  if (shm_proxy_p == NULL)
    {
      SHARD_ERR ("failed to uw_shm_open(shmid:%x). \n", shmid);
      return;
    }

  shard_metadata_dump_internal (fp, shm_proxy_p);

  uw_shm_detach (shm_proxy_p);

  return;
}

static int
shard_metadata_validate (T_BROKER_INFO * br_info_p, T_SHM_PROXY * shm_proxy_p)
{
  int error = 0;
  int modular = 0;

  error = shard_metadata_validate_user (shard_metadata_get_user (shm_proxy_p));
  if (error < 0)
    {
      return error;
    }

  error = shard_metadata_validate_conn (shard_metadata_get_conn (shm_proxy_p));
  if (error < 0)
    {
      return error;
    }


  if (br_info_p->shard_key_library_name[0] == 0)
    {
      modular = br_info_p->shard_key_modular;
    }
  else
    {
      modular = 0;

      error =
	shard_metadata_validate_key_function (br_info_p->shard_key_library_name, br_info_p->shard_key_function_name);
      if (error < 0)
	{
	  return error;
	}
    }
  error =
    shard_metadata_validate_key (shard_metadata_get_key (shm_proxy_p), shard_metadata_get_conn (shm_proxy_p), modular);

  return error;
}

static int
shard_metadata_validate_user (T_SHM_SHARD_USER * shm_user_p)
{
  if (shm_user_p == NULL)
    {
      assert (false);
      return -1;
    }

  return 0;
}

static int
shard_metadata_validate_key_range_internal (T_SHARD_KEY * key_p, T_SHM_SHARD_CONN * shm_conn_p, int modular)
{
  int i = 0, j = 0;
  int prv_range_max = -1;
  int num_shard_conn;
  T_SHARD_KEY_RANGE *range_p = NULL;

  assert (key_p);
  assert (shm_conn_p);

  num_shard_conn = shm_conn_p->num_shard_conn;
  if (num_shard_conn < 0)
    {
      SHARD_ERR ("%s: num shard connection is invalid.\n", key_p->key_column);
      return -1;
    }

  for (; i < key_p->num_key_range; i++)
    {
      range_p = &(key_p->range[i]);
      if (range_p->min > range_p->max)
	{
	  SHARD_ERR ("%s : shard range (%d, %d) is invalid.\n", key_p->key_column, range_p->min, range_p->max);
	  return -1;
	}

      if (range_p->min != prv_range_max + 1)
	{
	  SHARD_ERR ("%s : shard range (%d, %d) is invalid.\n", key_p->key_column, range_p->min, range_p->max);
	  return -1;
	}

      for (j = 0; j < num_shard_conn; j++)
	{
	  if (range_p->shard_id == shm_conn_p->shard_conn[j].shard_id)
	    {
	      break;
	    }
	}
      if (j >= num_shard_conn)
	{
	  SHARD_ERR ("%s: shard range shard_id (%d) is invalid.\n", key_p->key_column, range_p->shard_id);
	  return -1;
	}

      prv_range_max = range_p->max;
    }

  if ((modular >= 1) && (prv_range_max > modular - 1))
    {
      SHARD_ERR ("%s: shard range max (%d, modular %d) is invalid.\n", key_p->key_column, range_p->max, modular);
      return -1;
    }

  return 0;
}

static int
shard_metadata_validate_key (T_SHM_SHARD_KEY * shm_key_p, T_SHM_SHARD_CONN * shm_conn_p, int modular)
{
  int error, i;
  T_SHARD_KEY *curr_key_p;
  T_SHARD_KEY *prev_key_p = NULL;

  if (shm_key_p == NULL)
    {
      assert (false);
      return -1;
    }

  for (i = 0; i < shm_key_p->num_shard_key; i++)
    {
      curr_key_p = &(shm_key_p->shard_key[i]);
      if (prev_key_p && strcasecmp (curr_key_p->key_column, prev_key_p->key_column) == 0)
	{
	  SHARD_ERR ("key column [%s] is duplicated.\n", curr_key_p->key_column);
	  return -1;
	}

      error = shard_metadata_validate_key_range_internal (curr_key_p, shm_conn_p, modular);
      if (error < 0)
	{
	  return error;
	}

      prev_key_p = curr_key_p;
    }

  return 0;
}

static int
shard_metadata_validate_conn (T_SHM_SHARD_CONN * shm_conn_p)
{
  int i;

  if (shm_conn_p == NULL)
    {
      assert (false);
      return -1;
    }

  for (i = 0; i < shm_conn_p->num_shard_conn; i++)
    {
      if (shm_conn_p->shard_conn[i].shard_id != i)
	{
	  SHARD_ERR ("shard id (%d, %d) is invalid.\n", shm_conn_p->shard_conn[i].shard_id, i);
	  return -1;
	}
    }

  return 0;
}

static int
shard_metadata_validate_key_function (const char *library_name, const char *function_name)
{
  int ret;

  ret = load_shard_key_function (library_name, function_name);
  if (ret < 0)
    {
      SHARD_ERR ("user defined function [%s:%s] is invalid.\n", library_name, function_name);
      close_shard_key_function ();
      return -1;
    }
  close_shard_key_function ();
  return 0;
}

T_SHARD_KEY *
shard_metadata_bsearch_key (T_SHM_SHARD_KEY * shm_key_p, const char *keycolumn)
{
  int min, mid, max;
  int result;
  T_SHARD_KEY *key_p;

  min = 0;
  max = shm_key_p->num_shard_key - 1;

  do
    {
      mid = (min + max) / 2;
      key_p = &(shm_key_p->shard_key[mid]);
      result = strcasecmp (keycolumn, key_p->key_column);

      if (result < 0)
	{
	  max = mid - 1;
	}
      else if (result > 0)
	{
	  min = mid + 1;
	}
      else
	{
	  return key_p;
	}
    }
  while (min <= max);

  return NULL;
}

T_SHARD_KEY_RANGE *
shard_metadata_bsearch_range (T_SHARD_KEY * key_p, unsigned int hash_res)
{
  int min, mid, max;
  T_SHARD_KEY_RANGE *range_p;

  min = 0;
  max = key_p->num_key_range - 1;

  do
    {
      mid = (min + max) / 2;
      range_p = &(key_p->range[mid]);

      /* SHARD TODO : if min=-1, max=-1 ??? */
      if ((int) hash_res < range_p->min)
	{
	  max = mid - 1;
	}
      else if (hash_res > (unsigned int) range_p->max)
	{
	  min = mid + 1;
	}
      else
	{
	  return range_p;
	}
    }
  while (min <= max);

  return NULL;
}

T_SHARD_KEY_RANGE *
shard_metadata_find_shard_range (T_SHM_SHARD_KEY * shm_key_p, const char *key, unsigned int hash_res)
{
  T_SHARD_KEY *key_p = shard_metadata_bsearch_key (shm_key_p, key);
  if (key_p == NULL)
    {
      return NULL;
    }

  return shard_metadata_bsearch_range (key_p, hash_res);
}

T_SHARD_USER *
shard_metadata_get_shard_user (T_SHM_SHARD_USER * shm_user_p)
{
  T_SHARD_USER *shard_user_p = NULL;

  assert (shm_user_p);
  assert (shm_user_p->num_shard_user == 1);

  shard_user_p = &(shm_user_p->shard_user[0]);
  return shard_user_p;
}

T_SHARD_USER *
shard_metadata_get_shard_user_from_shm (T_SHM_PROXY * shm_proxy_p)
{
  T_SHM_SHARD_USER *shm_user_p = NULL;
  T_SHARD_USER *shard_user_p = NULL;

  assert (shm_proxy_p);

  shm_user_p = shard_metadata_get_user (shm_proxy_p);

  if (shm_user_p == NULL)
    {
      assert (false);
      return NULL;
    }

  shard_user_p = &(shm_user_p->shard_user[0]);
  return shard_user_p;
}

int
load_shard_key_function (const char *library_name, const char *function_name)
{
#if defined(WINDOWS)
  handle = LoadLibrary (library_name);
#else
  handle = dlopen (library_name, RTLD_NOW | RTLD_GLOBAL);
#endif
  if (handle == NULL)
    {
      return -1;
    }
#if defined(WINDOWS)
  fn_get_shard_key = (FN_GET_SHARD_KEY) GetProcAddress ((HMODULE) handle, function_name);
#else
  dlerror ();
  fn_get_shard_key = (FN_GET_SHARD_KEY) dlsym (handle, function_name);
#endif
  if (fn_get_shard_key == NULL)
    {
      return -1;
    }
  return 0;
}

void
close_shard_key_function (void)
{
  fn_get_shard_key = NULL;
  if (handle != NULL)
    {
#if defined(WINDOWS)
      FreeLibrary (handle);
#else
      dlclose (handle);
#endif
    }
  handle = NULL;
}
