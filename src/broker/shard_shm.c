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

#include "dbi.h"

#include "cas_common.h"
#include "broker_env_def.h"
#include "broker_filename.h"
#include "broker_shm.h"
#include "broker_admin_pub.h"
#include "shard_metadata.h"
#include "shard_shm.h"

#define BLANK_3 	"     "
#define BLANK_6		"          "
#define BLANK_9		"               "

#define LINE_70		"========================================" \
			"========================================"

static void shard_shm_set_shm_as (T_SHM_APPL_SERVER * shm_as_cp,
				  T_BROKER_INFO * br_info_p);
static void shard_shm_set_shm_proxy (T_SHM_PROXY * shm_proxy_p,
				     T_BROKER_INFO * br_info_p);

static void shard_shm_set_as_info (T_APPL_SERVER_INFO * as_info_p,
				   T_BROKER_INFO * br_info_p);
static int shard_shm_get_shard_info_offset (T_PROXY_INFO * proxy_info_p);
static int shard_shm_get_client_info_offset (T_PROXY_INFO * proxy_info_p);
static int shard_shm_get_shard_stat_offset (T_PROXY_INFO * proxy_info_p);
static void shard_shm_init_key_stat (T_SHM_SHARD_KEY_STAT * key_stat_p,
				     T_SHARD_KEY * shard_key);
static void shard_shm_init_shard_stat (T_SHM_SHARD_CONN_STAT * shard_stat_p,
				       T_SHARD_CONN * shard_conn);

void
shard_shm_set_shm_as (T_SHM_APPL_SERVER * shm_as_p, T_BROKER_INFO * br_info_p)
{
  char **env;
  int env_num;

  assert (shm_as_p);
  assert (br_info_p);

  shm_as_p->cci_default_autocommit = br_info_p->cci_default_autocommit;
  shm_as_p->suspend_mode = SUSPEND_NONE;
  shm_as_p->job_queue_size = br_info_p->job_queue_size;
  shm_as_p->job_queue[0].id = 0;	/* initialize max heap */
  shm_as_p->max_prepared_stmt_count = br_info_p->max_prepared_stmt_count;
  strcpy (shm_as_p->log_dir, br_info_p->log_dir);
  strcpy (shm_as_p->slow_log_dir, br_info_p->slow_log_dir);
  strcpy (shm_as_p->err_log_dir, br_info_p->err_log_dir);
  strcpy (shm_as_p->broker_name, br_info_p->name);

#if defined(WINDOWS)
  shm_appl->use_pdh_flag = FALSE;
  br_info_p->pdh_workset = 0;
  br_info_p->pdh_pct_cpu = 0;
#endif /* WINDOWS */

  env = make_env (br_info_p->source_env, &env_num);
  strncpy (shm_as_p->appl_server_name,
	   get_appl_server_name (br_info_p->appl_server, env, env_num),
	   APPL_SERVER_NAME_MAX_SIZE);
  shm_as_p->appl_server_name[APPL_SERVER_NAME_MAX_SIZE - 1] = '\0';

  shm_as_p->num_appl_server = br_info_p->appl_server_num;
  shm_as_p->sql_log_mode = br_info_p->sql_log_mode;
  shm_as_p->sql_log_max_size = br_info_p->sql_log_max_size;
  shm_as_p->long_query_time = br_info_p->long_query_time;
  shm_as_p->long_transaction_time = br_info_p->long_transaction_time;
  shm_as_p->appl_server_max_size = br_info_p->appl_server_max_size;
  shm_as_p->appl_server_hard_limit = br_info_p->appl_server_hard_limit;
  shm_as_p->session_timeout = br_info_p->session_timeout;
  shm_as_p->sql_log2 = br_info_p->sql_log2;
  shm_as_p->slow_log_mode = br_info_p->slow_log_mode;
#if defined(WINDOWS)
  shm_as_p->as_port = br_info_p->appl_server_port;
#endif /* WINDOWS */
  shm_as_p->query_timeout = br_info_p->query_timeout;
  shm_as_p->max_string_length = br_info_p->max_string_length;
  shm_as_p->stripped_column_name = br_info_p->stripped_column_name;
  shm_as_p->keep_connection = br_info_p->keep_connection;
  shm_as_p->cache_user_info = br_info_p->cache_user_info;
  shm_as_p->statement_pooling = br_info_p->statement_pooling;
  shm_as_p->access_mode = br_info_p->access_mode;
  shm_as_p->cci_pconnect = br_info_p->cci_pconnect;
  shm_as_p->select_auto_commit = br_info_p->select_auto_commit;

  shm_as_p->access_log = br_info_p->access_log;

  shm_as_p->jdbc_cache = br_info_p->jdbc_cache;
  shm_as_p->jdbc_cache_only_hint = br_info_p->jdbc_cache_only_hint;
  shm_as_p->jdbc_cache_life_time = br_info_p->jdbc_cache_life_time;

  strcpy (shm_as_p->preferred_hosts, br_info_p->preferred_hosts);
  strncpy (shm_as_p->source_env, br_info_p->source_env,
	   sizeof (shm_as_p->source_env) - 1);
  strncpy (shm_as_p->error_log_file, br_info_p->error_log_file,
	   sizeof (shm_as_p->error_log_file) - 1);
  strncpy (shm_as_p->proxy_log_dir, br_info_p->proxy_log_dir,
	   sizeof (shm_as_p->proxy_log_dir) - 1);

  shm_as_p->proxy_log_max_size = br_info_p->proxy_log_max_size;

  FREE_MEM (env);
  return;
}

static void
shard_shm_set_shm_proxy (T_SHM_PROXY * shm_proxy_p, T_BROKER_INFO * br_info_p)
{
  assert (shm_proxy_p);
  assert (br_info_p);

  shm_proxy_p->metadata_shm_id = br_info_p->metadata_shm_id;

  shm_proxy_p->min_num_proxy = br_info_p->min_num_proxy;
  shm_proxy_p->max_num_proxy = br_info_p->max_num_proxy;

  shm_proxy_p->max_client =
    (br_info_p->max_client / br_info_p->max_num_proxy);
  if (shm_proxy_p->max_client == 0)
    {
      shm_proxy_p->max_client = 1;	/* at least one client */
    }

  /* SHARD SHARD_KEY_ID */
  shm_proxy_p->shard_key_modular = br_info_p->shard_key_modular;
  strncpy (shm_proxy_p->shard_key_library_name,
	   br_info_p->shard_key_library_name,
	   sizeof (br_info_p->shard_key_library_name));
  strncpy (shm_proxy_p->shard_key_function_name,
	   br_info_p->shard_key_function_name,
	   sizeof (br_info_p->shard_key_function_name));

  return;
}

void
shard_shm_set_as_info (T_APPL_SERVER_INFO * as_info_p,
		       T_BROKER_INFO * br_info_p)
{
  /* SHARD TODO : will delete, duplicated code shard_as_activate() */
#if 0
  char port_name[PATH_MAX], dirname[PATH_MAX];
#if !defined(WINDOWS)
  char process_name[128];
#endif /* !WINDOWS */

  get_cubrid_file (FID_SOCK_DIR, dirname);
  snprintf (port_name, sizeof (port_name) - 1, "%s/%s.%d", dirname,
	    br_info_p->name, as_index);

#if !defined(WINDOWS)
  unlink (port_name);
#endif /* !WINDOWS */
#endif

  /* broker/broker_admin_pub.c" 2750 lines */

  /* mutex variable initialize */
  as_info_p->mutex_flag[SHM_MUTEX_BROKER] = FALSE;
  as_info_p->mutex_flag[SHM_MUTEX_ADMIN] = FALSE;
  as_info_p->mutex_turn = SHM_MUTEX_BROKER;
  CON_STATUS_LOCK_INIT (as_info_p);

  /* SHARD TODO : will delete, duplicated code shard_as_activate() */
#if 0
  as_info_p->num_request = 0;
  as_info_p->session_id = 0;
  as_info_p->uts_status = UTS_STATUS_START;
  as_info_p->reset_flag = FALSE;
  as_info_p->clt_appl_name[0] = '\0';
  as_info_p->clt_req_path_info[0] = '\0';
  as_info_p->clt_ip_addr[0] = '\0';
#if 0
  as_info_p->cur_sql_log_mode = shm_appl->sql_log_mode;
#else
  as_info_p->cur_sql_log_mode = 0;
#endif

#if defined(WINDOWS)
  as_info_p->pdh_pid = 0;
  as_info_p->pdh_workset = 0;
  as_info_p->pdh_pct_cpu = 0;
#endif /* WINDOWS */


  as_info_p->service_ready_flag = FALSE;
#endif

  /* FORK AND EXECUTE */


  /* SHARD TODO : will delete, duplicated code shard_as_activate() */
#if 0
  SERVICE_READY_WAIT (as_info_p->service_ready_flag);

  as_info_p->pid = pid;
  as_info_p->last_access_time = time (NULL);
  as_info_p->psize_time = time (NULL);
  as_info_p->psize = getsize (as_info_p->pid);
  as_info_p->uts_status = UTS_STATUS_IDLE;
  as_info_p->service_flag = SERVICE_OFF;
#endif

  as_info_p->database_name[0] = '\0';
  as_info_p->database_host[0] = '\0';

  return;
}

char *
shard_shm_initialize (T_BROKER_INFO * br_info_p, char *shm_metadata_cp)
{
  int shm_size;

  int shard_info_size;
  int proxy_info_size;
  int proxy_size;
  int shm_as_info_size;
  int client_info_size;
  int key_stat_size;
  int shard_stat_size;

  int max_client;
  int num_shard;
  int num_key;

  int appl_server_min_num, appl_server_max_num;

  char *shm_as_cp;

  int i, j, k, proxy_offset, shard_offset, client_offset, key_offset,
    shard_conn_offset;

  T_SHM_APPL_SERVER *shm_as_p;
  T_SHM_PROXY *shm_proxy_p;
  T_PROXY_INFO *proxy_info_p = NULL, *first_proxy_info_p = NULL;
  T_SHM_SHARD_KEY_STAT *key_stat_p, *first_key_stat_p;
  T_SHM_SHARD_CONN_STAT *shard_stat_p, *first_shard_stat_p;
  T_SHARD_INFO *shard_info_p, *first_shard_info_p;
  T_CLIENT_INFO *client_info_p, *first_client_info_p;

  T_SHM_SHARD_CONN *shm_conn_p;
  T_SHM_SHARD_USER *shm_user_p;
  T_SHM_SHARD_KEY *shm_key_p;
  T_SHARD_USER *user_p;
  T_SHARD_CONN *conn_p;
  T_APPL_SERVER_INFO *as_info_p;

  assert (shm_metadata_cp);

  shm_conn_p = shard_metadata_get_conn (shm_metadata_cp);
  if (shm_conn_p == NULL)
    {
      return NULL;
    }

  shm_key_p = shard_metadata_get_key (shm_metadata_cp);
  if (shm_key_p == NULL)
    {
      return NULL;
    }

  shard_info_size =
    sizeof (T_SHARD_INFO) - sizeof (T_APPL_SERVER_INFO) +
    (sizeof (T_APPL_SERVER_INFO) * br_info_p->appl_server_max_num);

  num_shard = shm_conn_p->num_shard_conn;
  if (num_shard <= 0)
    {
      return NULL;
    }

  num_key = shm_key_p->num_shard_key;
  if (num_key < 0)
    {
      return NULL;
    }
  key_stat_size = sizeof (T_SHM_SHARD_KEY_STAT);
  shard_stat_size = sizeof (T_SHM_SHARD_CONN_STAT);

  client_info_size = sizeof (T_CLIENT_INFO);
  max_client = br_info_p->max_client;

  proxy_info_size =
    sizeof (T_PROXY_INFO)
    - sizeof (T_SHM_SHARD_KEY_STAT) + (key_stat_size * num_key)
    - sizeof (T_SHM_SHARD_CONN_STAT) + (shard_stat_size * num_shard)
    - sizeof (T_CLIENT_INFO) + (client_info_size * max_client)
    - sizeof (T_SHARD_INFO) + (shard_info_size * num_shard);

  proxy_size =
    sizeof (T_SHM_PROXY) - sizeof (T_PROXY_INFO) +
    (proxy_info_size * br_info_p->max_num_proxy);

  shm_as_info_size =
    sizeof (T_SHM_APPL_SERVER) - sizeof (T_SHM_PROXY) + proxy_size;

  shm_as_cp =
    (char *) uw_shm_create (br_info_p->appl_server_shm_id, shm_as_info_size,
			    SHM_APPL_SERVER);
  if (shm_as_cp == NULL)
    {
      return NULL;
    }

  /* initialize shm appl server */
  shm_as_p = shard_shm_get_appl_server (shm_as_cp);
  if (shm_as_p == NULL)
    {
      return NULL;
    }
  shard_shm_set_shm_as (shm_as_p, br_info_p);

  shm_proxy_p = shard_shm_get_proxy (shm_as_cp);
  if (shm_proxy_p == NULL)
    {
      return NULL;
    }
  memset (shm_proxy_p, 0, sizeof (*shm_proxy_p));
  shard_shm_set_shm_proxy (shm_proxy_p, br_info_p);

  first_proxy_info_p = shard_shm_get_first_proxy_info (shm_proxy_p);
  if (first_proxy_info_p == NULL)
    {
      /* SET ERROR */
      return NULL;
    }

  shm_user_p = shard_metadata_get_user (shm_metadata_cp);
  if (shm_user_p)
    {
      user_p = shard_metadata_get_shard_user (shm_user_p);
      if (user_p == NULL)
	{
	  /* SET ERROR */
	  return NULL;
	}
    }
  else
    {
      /* SET ERROR */
      return NULL;
    }

  appl_server_min_num =
    (br_info_p->appl_server_min_num / br_info_p->max_num_proxy / num_shard);
  appl_server_min_num = (appl_server_min_num == 0) ? 1 : appl_server_min_num;

  appl_server_max_num =
    (br_info_p->appl_server_max_num / br_info_p->max_num_proxy / num_shard);
  appl_server_max_num = (appl_server_max_num == 0) ? 1 : appl_server_max_num;

  for (i = 0, proxy_offset = 0; i < shm_proxy_p->max_num_proxy;
       i++, proxy_offset += proxy_info_size)
    {
      /* 
       * SHARD TODO : what to do when min_num_proxy is different 
       *              from max_num_proxy ?
       */
      proxy_info_p =
	(T_PROXY_INFO *) ((char *) first_proxy_info_p + proxy_offset);
      memset (proxy_info_p, 0, proxy_info_size);

      proxy_info_p->next = proxy_info_size;
      proxy_info_p->proxy_id = i;
      proxy_info_p->pid = -1;
      proxy_info_p->service_flag = SERVICE_ON;
      proxy_info_p->status = PROXY_STATUS_START;
      proxy_info_p->max_shard = num_shard;
      proxy_info_p->max_client = shm_proxy_p->max_client;
      proxy_info_p->cur_client = 0;
      proxy_info_p->max_prepared_stmt_count =
	br_info_p->proxy_max_prepared_stmt_count;
      proxy_info_p->proxy_log_reset = 0;
      proxy_info_p->proxy_access_log_reset = 0;

      proxy_info_p->appl_server = br_info_p->appl_server;

      proxy_info_p->num_shard_key = num_key;
      proxy_info_p->num_shard_conn = num_shard;

      first_key_stat_p = shard_shm_get_first_key_stat (proxy_info_p);
      for (j = 0, key_offset = 0; j < num_key;
	   j++, key_offset += key_stat_size)
	{
	  key_stat_p =
	    (T_SHM_SHARD_KEY_STAT *) ((char *) first_key_stat_p + key_offset);
	  shard_shm_init_key_stat (key_stat_p, &(shm_key_p->shard_key[j]));
	}

      first_shard_stat_p = shard_shm_get_first_shard_stat (proxy_info_p);
      for (j = 0, shard_conn_offset = 0; j < num_shard;
	   j++, shard_conn_offset += shard_stat_size)
	{
	  shard_stat_p =
	    (T_SHM_SHARD_CONN_STAT *) ((char *) first_shard_stat_p +
				       shard_conn_offset);
	  shard_shm_init_shard_stat (shard_stat_p,
				     &(shm_conn_p->shard_conn[j]));
	}

      first_client_info_p = shard_shm_get_first_client_info (proxy_info_p);
      for (j = 0, client_offset = 0; j < max_client;
	   j++, client_offset += client_info_size)
	{
	  client_info_p =
	    (T_CLIENT_INFO *) ((char *) first_client_info_p + client_offset);
	  shard_shm_init_client_info (client_info_p);
	}

      first_shard_info_p = shard_shm_get_first_shard_info (proxy_info_p);
      for (j = 0, shard_offset = 0; j < proxy_info_p->max_shard;
	   j++, shard_offset += shard_info_size)
	{
	  shard_info_p =
	    (T_SHARD_INFO *) ((char *) first_shard_info_p + shard_offset);
	  memset (shard_info_p, 0, sizeof (*shard_info_p));
	  conn_p = &(shm_conn_p->shard_conn[j]);

	  shard_info_p->next = shard_info_size;
	  shard_info_p->shard_id = shm_conn_p->shard_conn[j].shard_id;
	  shard_info_p->status = 0;	/* SHARD TODO : not defined yet */
	  shard_info_p->service_ready_flag = false;

	  shard_info_p->min_appl_server = appl_server_min_num;
	  shard_info_p->max_appl_server = appl_server_max_num;
	  shard_info_p->num_appl_server = shard_info_p->min_appl_server;

	  strncpy (shard_info_p->db_name, shm_conn_p->shard_conn[j].db_name,
		   sizeof (shard_info_p->db_name) - 1);
	  strncpy (shard_info_p->db_user, user_p->db_user,
		   sizeof (shard_info_p->db_user) - 1);
	  strncpy (shard_info_p->db_password, user_p->db_password,
		   sizeof (shard_info_p->db_password) - 1);

	  strncpy (shard_info_p->db_conn_info, conn_p->db_conn_info,
		   sizeof (shard_info_p->db_conn_info) - 1);

	  for (k = 0; k < shard_info_p->max_appl_server; k++)
	    {
	      as_info_p = &(shard_info_p->as_info[k]);
	      memset (as_info_p, 0, sizeof (*as_info_p));
	      shard_shm_set_as_info (as_info_p, br_info_p);
	    }
	}
      shard_info_p->next = 0;

    }

  if (proxy_info_p != NULL)
    {
      proxy_info_p->next = 0;
    }

  /* SHARD TODO : will delete */
#if 0
  uw_shm_detach (shm_as_cp);
#endif

  return shm_as_cp;
}

T_SHM_APPL_SERVER *
shard_shm_get_appl_server (char *shm_as_cp)
{
  assert (shm_as_cp);

  return (T_SHM_APPL_SERVER *) (shm_as_cp);
}

T_SHM_PROXY *
shard_shm_get_proxy (char *shm_as_cp)
{
  T_SHM_APPL_SERVER *shm_as_p;
  T_SHM_PROXY *shm_proxy_p = NULL;

  assert (shm_as_cp);

  shm_as_p = shard_shm_get_appl_server (shm_as_cp);
  if (shm_as_p)
    {
      shm_proxy_p = &(shm_as_p->shard_proxy);
    }

  return shm_proxy_p;
}

T_PROXY_INFO *
shard_shm_get_first_proxy_info (T_SHM_PROXY * shm_proxy_p)
{
  T_PROXY_INFO *proxy_info_p;

  assert (shm_proxy_p);

  proxy_info_p = &(shm_proxy_p->proxy_info[0]);

  return proxy_info_p;
}

T_PROXY_INFO *
shard_shm_get_next_proxy_info (T_PROXY_INFO * curr_proxy_info_p)
{
  T_PROXY_INFO *proxy_info_p = NULL;

  assert (curr_proxy_info_p);

  if (curr_proxy_info_p->next)
    {
      proxy_info_p =
	(T_PROXY_INFO *) ((char *) curr_proxy_info_p +
			  curr_proxy_info_p->next);
    }

  return proxy_info_p;
}

T_PROXY_INFO *
shard_shm_find_proxy_info (T_SHM_PROXY * proxy_p, int proxy_id)
{
  T_PROXY_INFO *proxy_info_p = NULL;

  assert (proxy_p);
  assert (proxy_id >= 0);

  for (proxy_info_p = shard_shm_get_first_proxy_info (proxy_p);
       proxy_info_p;
       proxy_info_p = shard_shm_get_next_proxy_info (proxy_info_p))
    {
      if (proxy_info_p->proxy_id == proxy_id)
	{
	  return proxy_info_p;
	}
    }

  return proxy_info_p;
}

T_SHARD_INFO *
shard_shm_get_first_shard_info (T_PROXY_INFO * proxy_info_p)
{
  int offset = -1;

  T_SHARD_INFO *shard_info_p;

  offset = shard_shm_get_shard_info_offset (proxy_info_p);

  shard_info_p = (T_SHARD_INFO *) ((char *) proxy_info_p + offset);

  return shard_info_p;
}

T_SHARD_INFO *
shard_shm_get_next_shard_info (T_SHARD_INFO * curr_shard_p)
{
  T_SHARD_INFO *shard_info_p = NULL;

  assert (curr_shard_p);

  if (curr_shard_p->next)
    {
      shard_info_p =
	(T_SHARD_INFO *) ((char *) curr_shard_p + curr_shard_p->next);
    }

  return shard_info_p;
}

T_SHARD_INFO *
shard_shm_find_shard_info (T_PROXY_INFO * proxy_info_p, int shard_id)
{
  T_SHARD_INFO *shard_info_p = NULL;

  assert (proxy_info_p);
  assert (shard_id >= 0);

  for (shard_info_p = shard_shm_get_first_shard_info (proxy_info_p);
       shard_info_p;
       shard_info_p = shard_shm_get_next_shard_info (shard_info_p))
    {
      if (shard_info_p->shard_id == shard_id)
	{
	  return shard_info_p;
	}
    }

  return shard_info_p;
}

T_CLIENT_INFO *
shard_shm_get_first_client_info (T_PROXY_INFO * proxy_info_p)
{
  int offset = -1;
  T_CLIENT_INFO *client_info_p = NULL;

  assert (proxy_info_p);

  offset = shard_shm_get_client_info_offset (proxy_info_p);

  client_info_p = (T_CLIENT_INFO *) ((char *) proxy_info_p + offset);

  return (client_info_p);
}

T_CLIENT_INFO *
shard_shm_get_next_client_info (T_CLIENT_INFO * curr_client_p)
{
  T_CLIENT_INFO *client_info_p = NULL;

  assert (curr_client_p);

  client_info_p =
    (T_CLIENT_INFO *) ((char *) curr_client_p + sizeof (T_CLIENT_INFO));

  return (client_info_p);
}

T_CLIENT_INFO *
shard_shm_get_client_info (T_PROXY_INFO * proxy_info_p, int idx)
{
  T_CLIENT_INFO *tmp_client_info_p = NULL, *client_info_p = NULL;

  assert (proxy_info_p);

  tmp_client_info_p = shard_shm_get_first_client_info (proxy_info_p);
  client_info_p =
    (T_CLIENT_INFO *) ((char *) tmp_client_info_p +
		       (sizeof (T_CLIENT_INFO) * idx));

  return (client_info_p);
}

T_SHM_SHARD_CONN_STAT *
shard_shm_get_first_shard_stat (T_PROXY_INFO * proxy_info_p)
{
  int offset = -1;
  T_SHM_SHARD_CONN_STAT *shard_stat_p = NULL;

  assert (proxy_info_p);

  offset = shard_shm_get_shard_stat_offset (proxy_info_p);

  shard_stat_p = (T_SHM_SHARD_CONN_STAT *) ((char *) proxy_info_p + offset);

  return (shard_stat_p);
}

T_SHM_SHARD_CONN_STAT *
shard_shm_get_shard_stat (T_PROXY_INFO * proxy_info_p, int idx)
{
  T_SHM_SHARD_CONN_STAT *tmp_shard_stat_p = NULL, *shard_stat_p = NULL;

  assert (proxy_info_p);

  tmp_shard_stat_p = shard_shm_get_first_shard_stat (proxy_info_p);
  shard_stat_p =
    (T_SHM_SHARD_CONN_STAT *) ((char *) tmp_shard_stat_p +
			       (sizeof (T_SHM_SHARD_CONN_STAT) * idx));

  return (shard_stat_p);
}

T_SHM_SHARD_KEY_STAT *
shard_shm_get_first_key_stat (T_PROXY_INFO * proxy_info_p)
{
  T_SHM_SHARD_KEY_STAT *key_stat_p = NULL;

  assert (proxy_info_p);
  key_stat_p = &(proxy_info_p->key_stat[0]);

  return (key_stat_p);
}

T_SHM_SHARD_KEY_STAT *
shard_shm_get_key_stat (T_PROXY_INFO * proxy_info_p, int idx)
{
  T_SHM_SHARD_KEY_STAT *tmp_key_stat_p = NULL, *key_stat_p = NULL;

  assert (proxy_info_p);

  tmp_key_stat_p = shard_shm_get_first_key_stat (proxy_info_p);
  key_stat_p =
    (T_SHM_SHARD_KEY_STAT *) ((char *) tmp_key_stat_p +
			      (sizeof (T_SHM_SHARD_KEY_STAT) * idx));

  return (key_stat_p);
}

static void
shard_shm_dump_shard_appl_server (FILE * fp, T_APPL_SERVER_INFO * as_info_p)
{
  struct tm *at_tm_p = NULL;
  struct tm *ct_tm_p = NULL;
  char last_access_time[128];
  char last_connect_time[128];

  fprintf (fp, BLANK_9 "%-30s = %-30d \n", "NUM_REQUEST",
	   as_info_p->num_request);
  fprintf (fp, BLANK_9 "%-30s = %-30d \n", "PID", as_info_p->pid);
  fprintf (fp, BLANK_9 "%-30s = %-30d \n", "SESSION_ID",
	   as_info_p->session_id);
  fprintf (fp, BLANK_9 "%-30s = %-30d \n", "SERVICE_FLAG",
	   as_info_p->service_flag);
  fprintf (fp, BLANK_9 "%-30s = %-30d \n", "RESET_FLAG",
	   as_info_p->reset_flag);
  fprintf (fp, BLANK_9 "%-30s = %-30d \n", "UTS_STATUS",
	   as_info_p->uts_status);
  fprintf (fp, BLANK_9 "%-30s = %-30d \n", "CAS_CLIENT_TYPE",
	   as_info_p->cas_client_type);
  fprintf (fp, BLANK_9 "%-30s = %-30d \n", "SERVICE_READY_FLAG",
	   as_info_p->service_ready_flag);
  fprintf (fp, BLANK_9 "%-30s = %-30d \n", "CON_STATUS",
	   as_info_p->con_status);
  fprintf (fp, BLANK_9 "%-30s = %-30d \n", "CUR_KEEP_CON",
	   as_info_p->cur_keep_con);
  fprintf (fp, BLANK_9 "%-30s = %-30d \n", "CUR_SQL_LOG_MODE",
	   as_info_p->cur_sql_log_mode);
  fprintf (fp, BLANK_9 "%-30s = %-30d \n", "CUR_STATEMENT_POOLING",
	   as_info_p->cur_statement_pooling);
  if (as_info_p->last_access_time)
    {
      at_tm_p = localtime (&as_info_p->last_access_time);
      if (at_tm_p == NULL)
	{
	  return;
	}
      sprintf (last_access_time, "%d/%02d/%02d %02d:%02d:%02d",
	       at_tm_p->tm_year + 1900, at_tm_p->tm_mon + 1,
	       at_tm_p->tm_mday, at_tm_p->tm_hour, at_tm_p->tm_min,
	       at_tm_p->tm_sec);
      fprintf (fp, BLANK_9 "%-30s = %-30s \n", "LAST_ACCESS_TIME",
	       last_access_time);
    }
  fprintf (fp, BLANK_9 "%-30s = %-30s \n", "COOKIE_STR",
	   as_info_p->cookie_str);
  fprintf (fp, BLANK_9 "%-30s = %-30lld \n", "NUM_REQUESTS_RECEIVED",
	   (long long int) as_info_p->num_requests_received);
  fprintf (fp, BLANK_9 "%-30s = %-30lld \n", "NUM_TRANSACTIONS_PROCESSED",
	   (long long int) as_info_p->num_transactions_processed);
  fprintf (fp, BLANK_9 "%-30s = %-30lld \n", "NUM_QUERIES_PROCESSED",
	   (long long int) as_info_p->num_queries_processed);
  fprintf (fp, BLANK_9 "%-30s = %-30lld \n", "NUM_LONG_QUERIES",
	   (long long int) as_info_p->num_long_queries);
  fprintf (fp, BLANK_9 "%-30s = %-30lld \n", "NUM_LONG_TRANSACTIONS",
	   (long long int) as_info_p->num_long_transactions);
  fprintf (fp, BLANK_9 "%-30s = %-30lld \n", "NUM_ERROR_QUERIES",
	   (long long int) as_info_p->num_error_queries);
  fprintf (fp, BLANK_9 "%-30s = %-30d \n", "AUTO_COMMIT_MODE",
	   as_info_p->auto_commit_mode);
  fprintf (fp, BLANK_9 "%-30s = %-30s \n", "DATABASE_NAME",
	   as_info_p->database_name);
  fprintf (fp, BLANK_9 "%-30s = %-30s \n", "DATABASE_HOST",
	   as_info_p->database_host);
  if (as_info_p->last_connect_time)
    {
      ct_tm_p = localtime (&as_info_p->last_connect_time);
      if (ct_tm_p == NULL)
	{
	  return;
	}
      sprintf (last_connect_time, "%d/%02d/%02d %02d:%02d:%02d",
	       ct_tm_p->tm_year + 1900, ct_tm_p->tm_mon + 1,
	       ct_tm_p->tm_mday, ct_tm_p->tm_hour, ct_tm_p->tm_min,
	       ct_tm_p->tm_sec);
      fprintf (fp, BLANK_9 "%-30s = %-30s \n", "LAST_CONNECT_TIME",
	       last_connect_time);
    }
}

static void
shard_shm_dump_shard (FILE * fp, T_SHARD_INFO * shard_info_p)
{
  int i = 0;

  assert (shard_info_p);

  fprintf (fp, BLANK_6 "%-30s = %-30d \n", "NEXT", shard_info_p->next);
  fprintf (fp, BLANK_6 "%-30s = %-30d \n", "SHARD_ID",
	   shard_info_p->shard_id);
  fprintf (fp, BLANK_6 "%-30s = %-30d \n", "STATUS", shard_info_p->status);
  fprintf (fp, BLANK_6 "%-30s = %-30d \n", "MIN_APPL_SERVER",
	   shard_info_p->min_appl_server);
  fprintf (fp, BLANK_6 "%-30s = %-30d \n", "MAX_APPL_SERVER",
	   shard_info_p->max_appl_server);
  fprintf (fp, BLANK_6 "%-30s = %-30d \n", "NUM_APPL_SERVER",
	   shard_info_p->num_appl_server);
  fprintf (fp, BLANK_6 "%-30s = %-30s \n", "DB_NAME", shard_info_p->db_name);
  fprintf (fp, BLANK_6 "%-30s = %-30s \n", "DB_USER", shard_info_p->db_user);
  fprintf (fp, BLANK_6 "%-30s = %-30s \n", "DB_PASSWORD",
	   shard_info_p->db_password);
  fprintf (fp, BLANK_6 "%-30s = %-30s \n", "DB_CONN_INFO",
	   shard_info_p->db_conn_info);


  for (; i < shard_info_p->num_appl_server; i++)
    {
      fprintf (fp, "\n");
      fprintf (fp, BLANK_9 "<CAS %d>\n", i);
      fprintf (fp, BLANK_9 "%s\n", LINE_70);
      shard_shm_dump_shard_appl_server (fp, &(shard_info_p->as_info[i]));
    }

  return;
}

static void
shard_shm_dump_proxy_info (FILE * fp, T_PROXY_INFO * proxy_info_p)
{
  int i = 0;
  T_SHARD_INFO *shard_info_p;

  assert (proxy_info_p);

  fprintf (fp, BLANK_3 "%-30s = %-30d \n", "NEXT", proxy_info_p->next);
  fprintf (fp, BLANK_3 "%-30s = %-30d \n", "PROXY_ID",
	   proxy_info_p->proxy_id);
  fprintf (fp, BLANK_3 "%-30s = %-30d \n", "PID", proxy_info_p->pid);
  fprintf (fp, BLANK_3 "%-30s = %-30d \n", "STATUS", proxy_info_p->status);

  fprintf (fp, BLANK_3 "%-30s = %-30s \n", "PORT_NAME",
	   proxy_info_p->port_name);
  fprintf (fp, BLANK_3 "%-30s = %-30d \n", "MAX_SHARD",
	   proxy_info_p->max_shard);
  fprintf (fp, BLANK_3 "%-30s = %-30s \n", "ACCESS_LOG_FILE",
	   proxy_info_p->access_log_file);

  for (i = 0, shard_info_p = shard_shm_get_first_shard_info (proxy_info_p);
       shard_info_p;
       i++, shard_info_p = shard_shm_get_next_shard_info (shard_info_p))
    {
      fprintf (fp, "\n");
      fprintf (fp, BLANK_6 "<SHARD %d>\n", i);
      fprintf (fp, BLANK_6 "%s\n", LINE_70);
      shard_shm_dump_shard (fp, shard_info_p);
    }

  return;
}

static void
shard_shm_dump_proxy (FILE * fp, char *shm_as_cp)
{
  int i = 0;
  T_SHM_PROXY *shm_proxy_p;
  T_PROXY_INFO *proxy_info_p;

  assert (fp);
  assert (shm_as_cp);

  shm_proxy_p = shard_shm_get_proxy (shm_as_cp);
  if (shm_proxy_p == NULL)
    {
      return;
    }

  fprintf (fp, "\n");
  fprintf (fp, "<PROXY COMMON>\n");
  fprintf (fp, "%-30s = %-30d \n", "MIN_NUM_PROXY",
	   shm_proxy_p->min_num_proxy);
  fprintf (fp, "%-30s = %-30d \n", "MAX_NUM_PROXY",
	   shm_proxy_p->max_num_proxy);
  fprintf (fp, "%-30s = %-30d \n", "MAX_CLIENT", shm_proxy_p->max_client);
  fprintf (fp, "%-30s = %-30x \n", "METADATA_SHM_ID",
	   shm_proxy_p->metadata_shm_id);

  fprintf (fp, "%-30s = %-30d \n", "SHARD_KEY_MODULE",
	   shm_proxy_p->shard_key_modular);
  fprintf (fp, "%-30s = %-30s \n", "SHARD_KEY_LIBRARY_NAME",
	   shm_proxy_p->shard_key_library_name);
  fprintf (fp, "%-30s = %-30s \n", "SHARD_KEY_FUNCTION_NAME",
	   shm_proxy_p->shard_key_function_name);

  for (i = 0, proxy_info_p = shard_shm_get_first_proxy_info (shm_proxy_p);
       proxy_info_p;
       i++, proxy_info_p = shard_shm_get_next_proxy_info (proxy_info_p))
    {
      fprintf (fp, "\n");
      fprintf (fp, BLANK_3 "<PROXY %d>\n", i);
      fprintf (fp, BLANK_3 "%s\n", LINE_70);
      shard_shm_dump_proxy_info (fp, proxy_info_p);
    }

  return;
}

void
shard_shm_dump_appl_server_internal (FILE * fp, char *shm_as_cp)
{
  assert (fp);
  assert (shm_as_cp);

  shard_shm_dump_proxy (fp, shm_as_cp);
  return;
}

void
shard_shm_dump_appl_server (FILE * fp, int shmid)
{
  char *shm_as_cp = NULL;

#if 0
  shm_as_cp = (char *) uw_shm_open (shmid, SHM_BROKER, SHM_MODE_MONITOR);
#else
  shm_as_cp = (char *) uw_shm_open (shmid, SHM_APPL_SERVER, SHM_MODE_MONITOR);
#endif
  if (shm_as_cp == NULL)
    {
      SHARD_ERR ("failed to uw_shm_open(shmid:%x). \n", shmid);
      return;
    }

  shard_shm_dump_appl_server_internal (fp, shm_as_cp);

  uw_shm_detach (shm_as_cp);

  return;
}

T_APPL_SERVER_INFO *
shard_shm_get_as_info (T_PROXY_INFO * proxy_info_p, int shard_id, int as_id)
{
  T_SHARD_INFO *shard_info_p = NULL;
  T_APPL_SERVER_INFO *as_info_p = NULL;

  assert (as_id >= 0);

  shard_info_p = shard_shm_find_shard_info (proxy_info_p, shard_id);
  if (shard_info_p != NULL)
    {
      as_info_p = &shard_info_p->as_info[as_id];
    }

  return as_info_p;
}

T_PROXY_INFO *
shard_shm_get_proxy_info (char *shm_as_cp, int proxy_id)
{
  T_SHM_PROXY *shm_proxy_p = NULL;
  T_PROXY_INFO *proxy_info_p = NULL;

  assert (proxy_id >= 0);

  shm_proxy_p = shard_shm_get_proxy (shm_as_cp);
  if (shm_proxy_p == NULL)
    {
      return NULL;
    }

  proxy_info_p = shard_shm_get_first_proxy_info (shm_proxy_p);
  if (proxy_info_p != NULL && proxy_id > 0)
    {
      proxy_info_p = (T_PROXY_INFO *) ((char *) proxy_info_p +
				       proxy_info_p->next * proxy_id);
    }

  return proxy_info_p;
}

/* SHARD TODO : move it other c-file */
char **
make_env (char *env_file, int *env_num)
{
  char **env = NULL;
  int num, read_num;
  FILE *env_fp;
  char read_buf[BUFSIZ], col1[128], col2[128];

  *env_num = 0;

  if (env_file[0] == '\0')
    return NULL;

  env_fp = fopen (env_file, "r");
  if (env_fp == NULL)
    return NULL;

  num = 0;

  while (fgets (read_buf, BUFSIZ, env_fp) != NULL)
    {
      if (read_buf[0] == '#')
	continue;
      read_num = sscanf (read_buf, "%127s%127s", col1, col2);
      if (read_num != 2)
	continue;

      if (env == NULL)
	env = (char **) malloc (sizeof (char *));
      else
	env = (char **) realloc (env, sizeof (char *) * (num + 1));
      if (env == NULL)
	break;

      env[num] = (char *) malloc (strlen (col1) + strlen (col2) + 2);
      if (env[num] == NULL)
	{
	  for (num--; num >= 0; num--)
	    FREE_MEM (env[num]);
	  FREE_MEM (env);
	  env = NULL;
	  break;
	}

      sprintf (env[num], "%s=%s", col1, col2);
      num++;
    }

  fclose (env_fp);

  *env_num = num;
  return env;
}

const char *
get_appl_server_name (int appl_server_type, char **env, int env_num)
{
#if !defined(WINDOWS)
  int dbms_version;
#endif /* !WINDOWS */

  if (env != NULL)
    {
      const char *p = "UC_APPL_SERVER_EXE_NAME=";
      int i;
      for (i = 0; i < env_num; i++)
	{
	  if (strncmp (env[i], p, strlen (p)) == 0)
	    {
	      return (env[i] + strlen (p));
	    }
	}
    }
  if (appl_server_type == APPL_SERVER_CAS_ORACLE)
    return APPL_SERVER_CAS_ORACLE_NAME;
  if (appl_server_type == APPL_SERVER_CAS_MYSQL)
    return APPL_SERVER_CAS_MYSQL_NAME;
  return APPL_SERVER_CAS_NAME;
}

void
shard_shm_init_client_info (T_CLIENT_INFO * client_info_p)
{
  assert (client_info_p);

  client_info_p->client_id = -1;
  client_info_p->client_ip = 0;
  client_info_p->connect_time = 0;

  client_info_p->func_code = 0;
  client_info_p->req_time = 0;
  client_info_p->res_time = 0;

  client_info_p->clt_appl_name[0] = 0;
  client_info_p->clt_req_path_info[0] = 0;
  client_info_p->clt_ip_addr[0] = 0;
}

void
shard_shm_init_key_stat (T_SHM_SHARD_KEY_STAT * key_stat_p,
			 T_SHARD_KEY * shard_key)
{
  int i;
  assert (key_stat_p);
  assert (shard_key);

  strcpy (key_stat_p->key_column, shard_key->key_column);
  key_stat_p->num_key_range = shard_key->num_key_range;

  for (i = 0; i < shard_key->num_key_range; i++)
    {
      key_stat_p->stat[i].min = shard_key->range[i].min;
      key_stat_p->stat[i].max = shard_key->range[i].max;
      key_stat_p->stat[i].shard_id = shard_key->range[i].shard_id;

      key_stat_p->stat[i].num_range_queries_requested = 0;
    }
}

void
shard_shm_init_shard_stat (T_SHM_SHARD_CONN_STAT * shard_stat_p,
			   T_SHARD_CONN * shard_conn)
{
  assert (shard_stat_p);
  assert (shard_conn);

  shard_stat_p->shard_id = shard_conn->shard_id;

  shard_stat_p->num_hint_key_queries_requested = 0;
  shard_stat_p->num_hint_id_queries_requested = 0;
  shard_stat_p->num_hint_all_queries_requested = 0;
}

void
shard_shm_init_client_info_request (T_CLIENT_INFO * client_info_p)
{
  assert (client_info_p);

  client_info_p->func_code = 0;
  client_info_p->req_time = 0;
  client_info_p->res_time = 0;
}

void
shard_shm_set_client_info_request (T_CLIENT_INFO * client_info_p,
				   int func_code)
{
  assert (client_info_p);

  client_info_p->func_code = func_code;
  client_info_p->req_time = time (NULL);
  client_info_p->res_time = 0;
}

void
shard_shm_set_client_info_response (T_CLIENT_INFO * client_info_p)
{
  assert (client_info_p);

  client_info_p->res_time = time (NULL);
}

static int
shard_shm_get_shard_info_offset (T_PROXY_INFO * proxy_info_p)
{
  int offset = 0;
  int num_shard_key, num_shard_conn, max_client;

  assert (proxy_info_p);
  num_shard_key = proxy_info_p->num_shard_key;
  num_shard_conn = proxy_info_p->num_shard_conn;
  max_client = proxy_info_p->max_client;

  offset += offsetof (T_PROXY_INFO, key_stat);
  offset += (sizeof (T_SHM_SHARD_KEY_STAT) * num_shard_key);	/* T_SHM_SHARD_KEY_STAT[1] */
  offset += (sizeof (T_SHM_SHARD_CONN_STAT) * num_shard_conn);	/* T_SHM_SHARD_CONN_STAT[1] */
  offset += (sizeof (T_CLIENT_INFO) * max_client);	/* T_CLIENT_INFO[1] */
  return offset;
}

static int
shard_shm_get_client_info_offset (T_PROXY_INFO * proxy_info_p)
{
  int offset = 0;
  int num_shard_key, num_shard_conn;

  assert (proxy_info_p);

  num_shard_key = proxy_info_p->num_shard_key;
  num_shard_conn = proxy_info_p->num_shard_conn;

  offset += offsetof (T_PROXY_INFO, key_stat);
  offset += (sizeof (T_SHM_SHARD_KEY_STAT) * num_shard_key);	/* T_SHM_SHARD_KEY_STAT[1] */
  offset += (sizeof (T_SHM_SHARD_CONN_STAT) * num_shard_conn);	/* T_SHM_SHARD_CONN_STAT[1] */

  return offset;
}

static int
shard_shm_get_shard_stat_offset (T_PROXY_INFO * proxy_info_p)
{
  int offset = 0;
  int num_shard_key, num_shard_conn;

  assert (proxy_info_p);

  num_shard_key = proxy_info_p->num_shard_key;

  offset += offsetof (T_PROXY_INFO, key_stat);
  offset += (sizeof (T_SHM_SHARD_KEY_STAT) * num_shard_key);	/* T_SHM_SHARD_KEY_STAT[1] */

  return offset;
}
