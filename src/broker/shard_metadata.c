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

#include "dlfcn.h"
#include "cas_common.h"
#include "broker_filename.h"
#include "broker_admin_pub.h"
#include "broker_shm.h"
#include "broker_util.h"
#include "shard_metadata.h"

#define SHARD_QUERY_BUFFER_SIZE			1024

#define SHARD_METADATA_USER_TABLE_NAME 		"shard_user"
#define SHARD_METADATA_KEY_RANGE_TABLE_NAME 	"shard_range"
#define SHARD_METADATA_CONN_TABLE_NAME 		"shard_conn"

#define DEFAULT_NUM_USER		1
#define DEFAULT_NUM_KEY			1
#define DEFAULT_NUM_CONN		4

#define MAGIC_NUMBER			0x20081209

FN_GET_SHARD_KEY fn_get_shard_key = NULL;

#if defined(WINDOWS)
HMODULE handle = NULL;
#else
void *handle = NULL;
#endif

#if defined(CAS_FOR_ORACLE) || defined(CAS_FOR_MYSQL)
extern char *envvar_confdir_file (char *path, size_t size,
				  const char *filename);
#endif /* CAS_FOR_ORACLE || CAS_FOR_MYSQL */

static void shard_println_1 (FILE * fp);
static void shard_println_2 (FILE * fp);

static void shard_metadata_user_resize (T_SHM_SHARD_USER ** shm_user_pp,
					int max_user);
static void shard_metadata_key_resize (T_SHM_SHARD_KEY ** shm_key_pp,
				       int max_key);
static void shard_metadata_conn_resize (T_SHM_SHARD_CONN ** shm_conn_pp,
					int max_conn);
static T_SHM_SHARD_USER *shard_metadata_read_user (char *db_name,
						   char *db_user,
						   char *db_password);
static T_SHM_SHARD_KEY *shard_metadata_read_key (const char *filename);
static T_SHM_SHARD_CONN *shard_metadata_read_conn (const char *filename);

static int shard_metadata_key_range_comp (const void *p1, const void *p2);
static int shard_metadata_conn_comp (const void *p1, const void *p2);
static void shard_metadata_sort_key (T_SHM_SHARD_KEY * shm_key_p);
static void shard_metadata_sort_conn (T_SHM_SHARD_CONN * shm_conn_p);


static int shard_metadata_db_conn (const char *dbname, const char *hosts);
static char *shard_metadata_shm_initialize (int shmid,
					    T_SHM_SHARD_USER * shm_user_p,
					    T_SHM_SHARD_KEY * shm_key_p,
					    T_SHM_SHARD_CONN * shm_con_p);
static void shard_metadata_dump_user (FILE * fp,
				      T_SHM_SHARD_USER * shm_user_p);
static void shard_metadata_dump_key (FILE * fp, T_SHM_SHARD_KEY * shm_key_p);
static void shard_metadata_dump_conn (FILE * fp,
				      T_SHM_SHARD_CONN * shm_conn_p);
static int shard_metadata_validate (T_BROKER_INFO * br_info_p,
				    char *shm_metadata_cp);
static int shard_metadata_validate_user (T_SHM_SHARD_USER * shm_user_p);
static int shard_metadata_validate_key_range_internal (T_SHARD_KEY * key_p,
						       T_SHM_SHARD_CONN *
						       shm_conn_p,
						       int modular);
static int shard_metadata_validate_key (T_SHM_SHARD_KEY * shm_key_p,
					T_SHM_SHARD_CONN * shm_conn_p,
					int modular);
static int shard_metadata_validate_conn (T_SHM_SHARD_CONN * shm_conn_p);

static int shard_metadata_validate_key_function (const char *library_name,
						 const char *function_name);

static void
shard_println_1 (FILE * fp)
{
  assert (fp);

  fprintf (fp, "========================================"
	   "========================================\n");
}

static void
shard_println_2 (FILE * fp)
{
  assert (fp);

  fprintf (fp, "----------------------------------------"
	   "----------------------------------------\n");
}

static void
shard_metadata_user_resize (T_SHM_SHARD_USER ** shm_user_pp, int max_user)
{
  int mem_size = sizeof (int) + (max_user * sizeof (T_SHARD_USER));

  assert (shm_user_pp);

  if (*shm_user_pp)
    {
      (*shm_user_pp) =
	(T_SHM_SHARD_USER *) realloc ((*shm_user_pp), mem_size);
    }
  else
    {
      (*shm_user_pp) = (T_SHM_SHARD_USER *) malloc (mem_size);
    }

  return;
}

static void
shard_metadata_key_resize (T_SHM_SHARD_KEY ** shm_key_pp, int max_key)
{
  int mem_size = sizeof (int) + (max_key * sizeof (T_SHARD_KEY));

  assert (shm_key_pp);

  if (*shm_key_pp)
    {
      (*shm_key_pp) = (T_SHM_SHARD_KEY *) realloc ((*shm_key_pp), mem_size);
    }
  else
    {
      (*shm_key_pp) = (T_SHM_SHARD_KEY *) malloc (mem_size);
    }

  return;
}

static void
shard_metadata_conn_resize (T_SHM_SHARD_CONN ** shm_conn_pp, int max_conn)
{
  int mem_size = sizeof (int) + (max_conn * sizeof (T_SHARD_CONN));

  assert (shm_conn_pp);

  if (*shm_conn_pp)
    {
      (*shm_conn_pp) =
	(T_SHM_SHARD_CONN *) realloc ((*shm_conn_pp), mem_size);
    }
  else
    {
      (*shm_conn_pp) = (T_SHM_SHARD_CONN *) malloc (mem_size);
    }
  return;
}

static T_SHM_SHARD_USER *
shard_metadata_read_user (char *db_name, char *db_user, char *db_password)
{
  int error = NO_ERROR;
  int max_user;

  T_SHM_SHARD_USER *shm_user_p = NULL;
  T_SHARD_USER *user_p = NULL;

  max_user = 1;			/* only one user */
  shard_metadata_user_resize (&shm_user_p, max_user);
  if (shm_user_p == NULL)
    {
      goto error_return;
    }
  shm_user_p->num_shard_user = max_user;

  user_p = &(shm_user_p->shard_user[0]);
  strncpy (user_p->db_name, db_name, sizeof (user_p->db_name) - 1);
  strncpy (user_p->db_user, db_user, sizeof (user_p->db_user) - 1);
  strncpy (user_p->db_password, db_password,
	   sizeof (user_p->db_password) - 1);

  SHARD_INF ("<USERINFO> [%d] db_name:[%s], "
	     "db_user:[%s], db_password:[%s]\n",
	     0, user_p->db_name, user_p->db_user, user_p->db_password);

  return shm_user_p;

error_return:
  if (shm_user_p)
    {
      free_and_init (shm_user_p);
    }

  return NULL;
}

/*
 * return :key_column count 
 */
static T_SHM_SHARD_KEY *
shard_metadata_read_key (const char *filename)
{
  int error = NO_ERROR;
  int nargs;
  int idx_key, idx_range, max_key;
  char path[PATH_MAX];
  char line[LINE_MAX], *p;
  char section[LINE_MAX];
  int len;
  FILE *file;

  char key_column[SHARD_KEY_COLUMN_LEN];

  T_SHM_SHARD_KEY *shm_key_p = NULL;
  T_SHARD_KEY *key_p = NULL;
  T_SHARD_KEY_RANGE *range_p = NULL;

  envvar_confdir_file (path, PATH_MAX, filename);

  file = fopen (path, "r");
  if (file == NULL)
    {
      goto error_return;
    }

  max_key = DEFAULT_NUM_KEY;
  shard_metadata_key_resize (&shm_key_p, max_key);
  if (shm_key_p == NULL)
    {
      goto error_return;
    }
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

      len = strlen (line);
      if (line[0] == '\0')
	{
	  continue;
	}
      else if (line[0] == '[' && line[len - 1] == ']')
	{
	  nargs = sscanf (line, "[%%%[^]]", section);
	  if (nargs == 1)
	    {
	      trim (section);
	      if (strcasecmp (section, key_column) != 0)
		{
		  strncpy (key_column, section, sizeof (key_column) - 1);

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
      strncpy (key_p->key_column, key_column, sizeof (key_p->key_column) - 1);

      if (idx_range >= SHARD_KEY_RANGE_MAX)
	{
	  continue;
	}

      assert (idx_range >= 0 && idx_range < SHARD_KEY_RANGE_MAX);
      range_p = (T_SHARD_KEY_RANGE *) & ((key_p->range[idx_range]));
      nargs = sscanf (line, "%d %d %d", &range_p->min,
		      &range_p->max, &range_p->shard_id);

      range_p->key_index = idx_key;
      range_p->range_index = idx_range;

      if (nargs != 3)
	{
	  continue;
	}

      SHARD_INF ("<KEYINFO> [%d:%d] key_column:%s, "
		 "min:%d, max:%d, shard_id:%d. \n",
		 idx_key, idx_range, key_p->key_column,
		 range_p->min, range_p->max, range_p->shard_id);

      key_p->num_key_range = ++idx_range;
    }

  return shm_key_p;

error_return:
  if (shm_key_p)
    {
      free_and_init (shm_key_p);
    }

  return NULL;
}

static T_SHM_SHARD_CONN *
shard_metadata_read_conn (const char *filename)
{
  int error = NO_ERROR;
  int nargs;
  int idx_conn, max_conn;
  char line[LINE_MAX], *p;
  char path[PATH_MAX];
  int len;
  FILE *file;

  T_SHM_SHARD_CONN *shm_conn_p = NULL;
  T_SHARD_CONN *conn_p = NULL;

  envvar_confdir_file (path, PATH_MAX, filename);

  file = fopen (path, "r");
  if (file == NULL)
    {
      goto error_return;
    }

  max_conn = DEFAULT_NUM_CONN;
  shard_metadata_conn_resize (&shm_conn_p, max_conn);
  if (shm_conn_p == NULL)
    {
      goto error_return;
    }
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

      len = strlen (line);
      if (line[0] == '\0')
	{
	  continue;
	}

      if (idx_conn >= max_conn)
	{
	  max_conn += DEFAULT_NUM_CONN;
	  shard_metadata_conn_resize (&shm_conn_p, max_conn);
	  if (shm_conn_p == NULL)
	    {
	      goto error_return;
	    }
	}

      assert (idx_conn >= 0);
      conn_p = &(shm_conn_p->shard_conn[idx_conn]);
      nargs = sscanf (line, "%d %s %[^\n]", &conn_p->shard_id,
		      conn_p->db_name, conn_p->db_conn_info);
      if (nargs != 3)
	{
	  continue;
	}

      trim (conn_p->db_conn_info);

      SHARD_INF
	("<CONNINFO> [%d] shard_id:%d, db_name:<%s>, db_conn_info:<%s>.\n",
	 idx_conn, conn_p->shard_id, conn_p->db_name, conn_p->db_conn_info);

      shm_conn_p->num_shard_conn = ++idx_conn;
    }

  return shm_conn_p;

error_return:
  if (shm_conn_p)
    {
      free_and_init (shm_conn_p);
    }

  return NULL;
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
      qsort ((void *) key_p->range, key_p->num_key_range,
	     sizeof (T_SHARD_KEY_RANGE), shard_metadata_key_range_comp);
    }
  return;
}

static void
shard_metadata_sort_conn (T_SHM_SHARD_CONN * shm_conn_p)
{
  qsort ((void *) shm_conn_p->shard_conn, shm_conn_p->num_shard_conn,
	 sizeof (T_SHARD_CONN), shard_metadata_conn_comp);
  return;
}


T_SHM_SHARD_USER *
shard_metadata_get_user (char *shm_metadata_cp)
{
  int offset = 0;
  T_SHM_SHARD_USER *shm_user_p;

  assert (shm_metadata_cp);

  offset += sizeof (int);	/* MAGIC */
  shm_user_p = (T_SHM_SHARD_USER *) (shm_metadata_cp + offset);

  return (shm_user_p);
}

T_SHM_SHARD_KEY *
shard_metadata_get_key (char *shm_metadata_cp)
{
  int offset = 0;
  T_SHM_SHARD_USER *shm_user_p;
  T_SHM_SHARD_KEY *shm_key_p;

  assert (shm_metadata_cp);

  offset += sizeof (int);	/* MAGIC */
  shm_user_p = (T_SHM_SHARD_USER *) (shm_metadata_cp + offset);

  offset +=
    sizeof (int) + (shm_user_p->num_shard_user * sizeof (T_SHARD_USER));
  shm_key_p = (T_SHM_SHARD_KEY *) (shm_metadata_cp + offset);

  return (shm_key_p);
}

T_SHM_SHARD_CONN *
shard_metadata_get_conn (char *shm_metadata_cp)
{
  int offset = 0;
  T_SHM_SHARD_USER *shm_user_p;
  T_SHM_SHARD_KEY *shm_key_p;
  T_SHM_SHARD_CONN *shm_conn_p;

  assert (shm_metadata_cp);

  offset += sizeof (int);	/* MAGIC */
  shm_user_p = (T_SHM_SHARD_USER *) (shm_metadata_cp + offset);

  offset +=
    sizeof (int) + (shm_user_p->num_shard_user * sizeof (T_SHARD_USER));
  shm_key_p = (T_SHM_SHARD_KEY *) (shm_metadata_cp + offset);

  offset += sizeof (int) + (shm_key_p->num_shard_key * sizeof (T_SHARD_KEY));
  shm_conn_p = (T_SHM_SHARD_CONN *) (shm_metadata_cp + offset);

  return (shm_conn_p);
}

int
shard_metadata_conn_count (char *shm_metadata_cp)
{
  T_SHM_SHARD_CONN *shm_conn_p;
  int conn_count = 0;

  assert (shm_metadata_cp);

  shm_conn_p = shard_metadata_get_conn (shm_metadata_cp);
  if (shm_conn_p)
    {
      conn_count = shm_conn_p->num_shard_conn;
    }

  return conn_count;
}

static char *
shard_metadata_shm_initialize (int shmid, T_SHM_SHARD_USER * shm_user_p,
			       T_SHM_SHARD_KEY * shm_key_p,
			       T_SHM_SHARD_CONN * shm_conn_p)
{
  int shm_size = 0;
  int shm_user_size, shm_key_size, shm_conn_size;
  int offset;
  int magic = MAGIC_NUMBER;
  char *shm_metadata_cp;

  assert (shmid > 0);
  assert (shm_user_p);
  assert (shm_key_p);
  assert (shm_conn_p);

  SHARD_INF ("<SHM> META_SHMID:[%x].\n", shmid);

  shm_user_size =
    sizeof (int) + (shm_user_p->num_shard_user * sizeof (T_SHARD_USER));
  shm_key_size =
    sizeof (int) + (shm_key_p->num_shard_key * sizeof (T_SHARD_KEY));
  shm_conn_size =
    sizeof (int) + (shm_conn_p->num_shard_conn * sizeof (T_SHARD_CONN));

  shm_size = sizeof (magic) + shm_user_size + shm_key_size + shm_conn_size;
  SHARD_INF ("<SHM> META_SHMSIZE:[%d][%d][%d] TOTAL:[%d].\n", shm_user_size,
	     shm_key_size, shm_conn_size, shm_size);

  shm_metadata_cp = (char *) uw_shm_create (shmid, shm_size, SHM_BROKER);
  if (shm_metadata_cp == NULL)
    {
      return NULL;
    }

  offset = 0;
  memcpy (shm_metadata_cp + offset, &magic, sizeof (magic));
  offset += sizeof (magic);
  memcpy (shm_metadata_cp + offset, shm_user_p, shm_user_size);
  offset += shm_user_size;
  memcpy (shm_metadata_cp + offset, shm_key_p, shm_key_size);
  offset += shm_key_size;
  memcpy (shm_metadata_cp + offset, shm_conn_p, shm_conn_size);

  return shm_metadata_cp;
}

char *
shard_metadata_initialize (T_BROKER_INFO * br_info)
{
  int error;
  int metainfo_shm_size;
  char *shm_metadata_cp = NULL;
  T_SHM_SHARD_USER *shm_user_p = NULL;
  T_SHM_SHARD_KEY *shm_key_p = NULL;
  T_SHM_SHARD_CONN *shm_conn_p = NULL;

  assert (br_info);

  shm_user_p = shard_metadata_read_user (br_info->shard_db_name,
					 br_info->shard_db_user,
					 br_info->shard_db_password);
  if (shm_user_p == NULL)
    {
      sprintf (admin_err_msg, "failed to read metadata user [%s]",
	       br_info->name);
      goto end;
    }
  shm_key_p = shard_metadata_read_key (br_info->shard_key_file);
  if (shm_key_p == NULL)
    {
      sprintf (admin_err_msg, "failed to read metadata key [%s]",
	       br_info->name);
      goto end;
    }
  shard_metadata_sort_key (shm_key_p);
#if defined(SHARD_VERBOSE_DEBUG)
  shard_metadata_dump_key (stdout, shm_key_p);
#endif

  shm_conn_p = shard_metadata_read_conn (br_info->shard_connection_file);
  if (shm_conn_p == NULL)
    {
      sprintf (admin_err_msg, "failed to read metadata connection [%s]",
	       br_info->name);
      goto end;
    }
  shard_metadata_sort_conn (shm_conn_p);
#if defined(SHARD_VERBOSE_DEBUG)
  shard_metadata_dump_conn (stdout, shm_conn_p);
#endif

  SHARD_INF ("num USER:[%d], num KEY:[%d], num CONN:[%d].\n",
	     shm_user_p->num_shard_user, shm_key_p->num_shard_key,
	     shm_conn_p->num_shard_conn);

  shm_metadata_cp =
    shard_metadata_shm_initialize (br_info->metadata_shm_id, shm_user_p,
				   shm_key_p, shm_conn_p);
  if (shm_metadata_cp == NULL)
    {
      sprintf (admin_err_msg,
	       "failed to initialize metadata shared memory [%s]",
	       br_info->name);
      goto end;
    }

  error = shard_metadata_validate (br_info, shm_metadata_cp);
  if (error < 0)
    {
      shm_metadata_cp = NULL;
      sprintf (admin_err_msg, "failed to metadata validate check [%s]",
	       br_info->name);
      goto end;
    }

end:
  if (shm_user_p)
    {
      free_and_init (shm_user_p);
    }
  if (shm_key_p)
    {
      free_and_init (shm_key_p);
    }
  if (shm_conn_p)
    {
      free_and_init (shm_conn_p);
    }

  return shm_metadata_cp;
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
  fprintf (fp, "      %-15s %-20s %-20s\n", "DB_NAME",
	   "DB_USER", "DB_PASSWORD");
  shard_println_2 (fp);

  for (i = 0; i < shm_user_p->num_shard_user; i++)
    {
      user_p = (T_SHARD_USER *) (&(shm_user_p->shard_user[i]));
      fprintf (fp, "[%-3d] %-15s %-20s %-20s\n", i,
	       user_p->db_name, user_p->db_user, user_p->db_password);
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
  fprintf (fp, "          %-30s %-5s %-5s %-10s\n",
	   "KEY", "MIN", "MAX", "SHARD_ID");
  shard_println_2 (fp);
  for (i = 0; i < shm_key_p->num_shard_key; i++)
    {
      key_p = (T_SHARD_KEY *) (&(shm_key_p->shard_key[i]));
      for (j = 0; j < key_p->num_key_range; j++)
	{
	  range_p = (T_SHARD_KEY_RANGE *) (&(key_p->range[j]));

	  fprintf (fp, "[%-3d|%-3d] %-30s %-5d %-5d %-10d\n", i, j,
		   key_p->key_column, range_p->min, range_p->max,
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
  fprintf (fp, "      %-10s %-20s %-30s\n", "SHARD_ID", "DB_NAME",
	   "DB_CONN_INFO");
  shard_println_2 (fp);
  for (i = 0; i < shm_conn_p->num_shard_conn; i++)
    {
      conn_p = (T_SHARD_CONN *) (&(shm_conn_p->shard_conn[i]));
      fprintf (fp, "[%-3d] %-10d %-20s %-30s\n", i, conn_p->shard_id,
	       conn_p->db_name, conn_p->db_conn_info);
    }
  shard_println_1 (fp);
  fprintf (fp, "\n\n");

  return;
}

void
shard_metadata_dump_internal (FILE * fp, char *shm_metadata_cp)
{
  T_SHM_SHARD_USER *shm_user_p;
  T_SHM_SHARD_KEY *shm_key_p;
  T_SHM_SHARD_CONN *shm_conn_p;

  assert (shm_metadata_cp);

  shm_user_p = shard_metadata_get_user (shm_metadata_cp);
  if (shm_user_p)
    {
      shard_metadata_dump_user (fp, shm_user_p);
    }

  shm_key_p = shard_metadata_get_key (shm_metadata_cp);
  if (shm_key_p)
    {
      shard_metadata_dump_key (fp, shm_key_p);
    }

  shm_conn_p = shard_metadata_get_conn (shm_metadata_cp);
  if (shm_conn_p)
    {
      shard_metadata_dump_conn (fp, shm_conn_p);
    }

  return;
}

void
shard_metadata_dump (FILE * fp, int shmid)
{
  char *shm_metadata_cp = NULL;

  shm_metadata_cp =
    (char *) uw_shm_open (shmid, SHM_BROKER, SHM_MODE_MONITOR);
  if (shm_metadata_cp == NULL)
    {
      SHARD_ERR ("failed to uw_shm_open(shmid:%x). \n", shmid);
      return;
    }

  shard_metadata_dump_internal (fp, shm_metadata_cp);

  uw_shm_detach (shm_metadata_cp);

  return;
}

static int
shard_metadata_validate (T_BROKER_INFO * br_info_p, char *shm_metadata_cp)
{
  int error = 0;
  int modular = 0;

  error =
    shard_metadata_validate_user (shard_metadata_get_user (shm_metadata_cp));
  if (error < 0)
    {
      return error;
    }

  error =
    shard_metadata_validate_conn (shard_metadata_get_conn (shm_metadata_cp));
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
	shard_metadata_validate_key_function (br_info_p->
					      shard_key_library_name,
					      br_info_p->
					      shard_key_function_name);
      if (error < 0)
	{
	  return error;
	}
    }
  error =
    shard_metadata_validate_key (shard_metadata_get_key (shm_metadata_cp),
				 shard_metadata_get_conn (shm_metadata_cp),
				 modular);

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
shard_metadata_validate_key_range_internal (T_SHARD_KEY * key_p,
					    T_SHM_SHARD_CONN * shm_conn_p,
					    int modular)
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
      SHARD_ERR ("%s: num shard connection is invalid.\n");
      return -1;
    }

  for (; i < key_p->num_key_range; i++)
    {
      range_p = &(key_p->range[i]);
      if (range_p->min > range_p->max)
	{
	  SHARD_ERR ("%s : shard range (%d, %d) is invalid.\n",
		     key_p->key_column, range_p->min, range_p->max);
	  return -1;
	}

      if (range_p->min != prv_range_max + 1)
	{
	  SHARD_ERR ("%s : shard range (%d, %d) is invalid.\n",
		     key_p->key_column, range_p->min, range_p->max);
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
	  SHARD_ERR ("%s: shard range shard_id (%d) is invalid.\n",
		     range_p->shard_id);
	  return -1;
	}

      prv_range_max = range_p->max;
    }

  if ((modular >= 1) && (prv_range_max > modular))
    {
      SHARD_ERR ("%s: shard range max (%d, modular %d) is invalid.\n",
		 range_p->max, modular);
      return -1;
    }

  return 0;
}

static int
shard_metadata_validate_key (T_SHM_SHARD_KEY * shm_key_p,
			     T_SHM_SHARD_CONN * shm_conn_p, int modular)
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
      if (prev_key_p
	  && strcasecmp (curr_key_p->key_column, prev_key_p->key_column) == 0)
	{
	  SHARD_ERR ("key column [%s] is duplicated.\n",
		     curr_key_p->key_column);
	  return -1;
	}

      error =
	shard_metadata_validate_key_range_internal (curr_key_p, shm_conn_p,
						    modular);
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
	  SHARD_ERR ("shard id (%d, %d) is invalid.\n",
		     shm_conn_p->shard_conn[i].shard_id, i);
	  return -1;
	}
    }

  return 0;
}

static int
shard_metadata_validate_key_function (const char *library_name,
				      const char *function_name)
{
  int ret;

  ret = load_shard_key_function (library_name, function_name);
  if (ret < 0)
    {
      SHARD_ERR ("user defined function [%s:%s] is invalid.\n", library_name,
		 function_name);
      close_shard_key_function ();
      return -1;
    }
  close_shard_key_function ();
  return 0;
}

T_SHARD_KEY *
shard_metadata_bsearch_key (T_SHM_SHARD_KEY * shm_key_p,
			    const char *keycolumn)
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
  int result;
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
shard_metadata_find_shard_range (T_SHM_SHARD_KEY * shm_key_p, const char *key,
				 unsigned int hash_res)
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
  int i;
  T_SHARD_USER *shard_user_p = NULL;

  assert (shm_user_p);
  assert (shm_user_p->num_shard_user == 1);

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
  dlerror ();
#if defined(WINDOWS)
  fn_get_shard_key = GetProcAddress ((HMODULE) handle, function_name);
#else
  fn_get_shard_key = dlsym (handle, function_name);
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
