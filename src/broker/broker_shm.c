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
 * broker_shm.c -
 */

#ident "$Id$"

#if defined(WINDOWS)
#include <winsock2.h>
#include <windows.h>
#include <aclapi.h>
#endif

#include <stdio.h>
#include <sys/types.h>
#include <errno.h>
#include <assert.h>

#if defined(WINDOWS)
#include <windows.h>
#else
#include <sys/ipc.h>
#include <sys/shm.h>
#include <netdb.h>
#endif

#include "cas_common.h"
#include "broker_shm.h"
#include "broker_error.h"
#include "broker_filename.h"
#include "broker_util.h"

#if defined(WINDOWS)
#include "broker_list.h"
#endif

#define 	SHMODE			0644


#if defined(WINDOWS)
static int shm_id_cmp_func (void *key1, void *key2);
static int shm_info_assign_func (T_LIST * node, void *key, void *value);
static char *shm_id_to_name (int shm_key);
#endif
static int get_host_ip (unsigned char *ip_addr);

#if defined(WINDOWS)
T_LIST *shm_id_list_header = NULL;
#endif

static void broker_shm_set_as_info (T_SHM_APPL_SERVER * shm_appl, T_APPL_SERVER_INFO * as_info_p,
				    T_BROKER_INFO * br_info_p, int as_index);
static void shard_shm_set_shard_conn_info (T_SHM_APPL_SERVER * shm_as_p, T_SHM_PROXY * shm_proxy_p);

static void get_access_log_file_name (char *access_log_file, char *access_log_path, char *broker_name, int len);
static void get_error_log_file_name (char *access_log_file, char *error_log_path, char *broker_name, int len);

static const char *get_appl_server_name (int appl_server_type);

/*
 * name:        uw_shm_open
 *
 * arguments:
 *		key - caller module defined shmared memory key.
 *		      if 'key' value is 0, this module set key value
 *		      from shm key file.
 *
 * returns/side-effects:
 *              attached shared memory pointer if no error
 *		NULL if error shm open error.
 *
 * description: get and attach shared memory
 *
 */
#if defined(WINDOWS)
void *
uw_shm_open (int shm_key, int which_shm, T_SHM_MODE shm_mode)
{
  LPVOID lpvMem = NULL;		/* address of shared memory */
  HANDLE hMapObject = NULL;
  DWORD dwAccessRight;
  char *shm_name;

  shm_name = shm_id_to_name (shm_key);

  if (shm_mode == SHM_MODE_MONITOR)
    {
      dwAccessRight = FILE_MAP_READ;
    }
  else
    {
      dwAccessRight = FILE_MAP_WRITE;
    }

  hMapObject = OpenFileMapping (dwAccessRight, FALSE,	/* inherit flag */
				shm_name);	/* name of map object */

  if (hMapObject == NULL)
    {
      return NULL;
    }

  /* Get a pointer to the file-mapped shared memory. */
  lpvMem = MapViewOfFile (hMapObject,	/* object to map view of */
			  dwAccessRight, 0,	/* high offset: map from */
			  0,	/* low offset: beginning */
			  0);	/* default: map entire file */

  if (lpvMem == NULL)
    {
      CloseHandle (hMapObject);
      return NULL;
    }

  link_list_add (&shm_id_list_header, (void *) lpvMem, (void *) hMapObject, shm_info_assign_func);

  return lpvMem;
}
#else
void *
uw_shm_open (int shm_key, int which_shm, T_SHM_MODE shm_mode)
{
  int mid;
  void *p;

  if (shm_key < 0)
    {
      UW_SET_ERROR_CODE (UW_ER_SHM_OPEN, 0);
      return NULL;
    }
  mid = shmget (shm_key, 0, SHMODE);

  if (mid == -1)
    {
      UW_SET_ERROR_CODE (UW_ER_SHM_OPEN, errno);
      return NULL;
    }
  p = shmat (mid, (char *) 0, ((shm_mode == SHM_MODE_ADMIN) ? 0 : SHM_RDONLY));
  if (p == (void *) -1)
    {
      UW_SET_ERROR_CODE (UW_ER_SHM_OPEN, errno);
      return NULL;
    }
  if (which_shm == SHM_APPL_SERVER)
    {
      if (((T_SHM_APPL_SERVER *) p)->magic == MAGIC_NUMBER)
	{
	  return p;
	}
    }
  else if (which_shm == SHM_BROKER)
    {
      if (((T_SHM_BROKER *) p)->magic == MAGIC_NUMBER)
	{
	  return p;
	}
    }
  else if (which_shm == SHM_PROXY)
    {
      if (((T_SHM_PROXY *) p)->magic == MAGIC_NUMBER)
	{
	  return p;
	}
    }
  UW_SET_ERROR_CODE (UW_ER_SHM_OPEN_MAGIC, 0);
  return NULL;
}
#endif

/*
 * name:        uw_shm_create
 *
 * arguments:
 *		NONE
 *
 * returns/side-effects:
 *              created shared memory ptr if no error
 *		NULL if error
 *
 * description: create and attach shared memory
 *		unless shared memory is already created with same key
 *
 */

#if defined(WINDOWS)
void *
uw_shm_create (int shm_key, int size, int which_shm)
{
  LPVOID lpvMem = NULL;
  HANDLE hMapObject = NULL;
  char *shm_name;
  DWORD dwRes;
  PSID pEveryoneSID = NULL;
  PSID pAdminSID = NULL;
  PACL pACL = NULL;
  PSECURITY_DESCRIPTOR pSD = NULL;
  EXPLICIT_ACCESS ea[2];
  SID_IDENTIFIER_AUTHORITY SIDAuthWorld = SECURITY_WORLD_SID_AUTHORITY;
  SID_IDENTIFIER_AUTHORITY SIDAuthNT = SECURITY_NT_AUTHORITY;
  SECURITY_ATTRIBUTES sa;

  /* Create a well-known SID for the Everyone group. */
  if (!AllocateAndInitializeSid (&SIDAuthWorld, 1, SECURITY_WORLD_RID, 0, 0, 0, 0, 0, 0, 0, &pEveryoneSID))
    {
      goto error_exit;
    }

  /* Initialize an EXPLICIT_ACCESS structure for an ACE. The ACE will allow Everyone read access to the shared memory. */
  memset (ea, '\0', 2 * sizeof (EXPLICIT_ACCESS));
  ea[0].grfAccessPermissions = GENERIC_READ;
  ea[0].grfAccessMode = SET_ACCESS;
  ea[0].grfInheritance = NO_INHERITANCE;
  ea[0].Trustee.TrusteeForm = TRUSTEE_IS_SID;
  ea[0].Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
  ea[0].Trustee.ptstrName = (LPTSTR) pEveryoneSID;

  /* Create a SID for the BUILTIN\Administrators group. */
  if (!AllocateAndInitializeSid
      (&SIDAuthNT, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &pAdminSID))
    {
      goto error_exit;
    }

  /* Initialize an EXPLICIT_ACCESS structure for an ACE. The ACE will allow the Administrators group full access to the
   * shared memory */
  ea[1].grfAccessPermissions = GENERIC_ALL;
  ea[1].grfAccessMode = SET_ACCESS;
  ea[1].grfInheritance = NO_INHERITANCE;
  ea[1].Trustee.TrusteeForm = TRUSTEE_IS_SID;
  ea[1].Trustee.TrusteeType = TRUSTEE_IS_GROUP;
  ea[1].Trustee.ptstrName = (LPTSTR) pAdminSID;

  /* Create a new ACL that contains the new ACEs. */
  dwRes = SetEntriesInAcl (2, ea, NULL, &pACL);
  if (ERROR_SUCCESS != dwRes)
    {
      goto error_exit;
    }

  /* Initialize a security descriptor. */
  pSD = (PSECURITY_DESCRIPTOR) LocalAlloc (LPTR, SECURITY_DESCRIPTOR_MIN_LENGTH);

  if (NULL == pSD)
    {
      goto error_exit;
    }

  if (!InitializeSecurityDescriptor (pSD, SECURITY_DESCRIPTOR_REVISION))
    {
      goto error_exit;
    }

  /* Add the ACL to the security descriptor. */
  if (!SetSecurityDescriptorDacl (pSD, TRUE, pACL, FALSE))
    {
      goto error_exit;
    }

  /* Initialize a security attributes structure. */
  sa.nLength = sizeof (SECURITY_ATTRIBUTES);
  sa.lpSecurityDescriptor = pSD;
  sa.bInheritHandle = FALSE;

  shm_name = shm_id_to_name (shm_key);

  hMapObject = CreateFileMapping (INVALID_HANDLE_VALUE, &sa, PAGE_READWRITE, 0, size, shm_name);

  if (hMapObject == NULL)
    {
      goto error_exit;
    }

  if (GetLastError () == ERROR_ALREADY_EXISTS)
    {
      CloseHandle (hMapObject);
      goto error_exit;
    }

  lpvMem = MapViewOfFile (hMapObject, FILE_MAP_WRITE, 0, 0, 0);

  if (lpvMem == NULL)
    {
      CloseHandle (hMapObject);
      goto error_exit;
    }

  link_list_add (&shm_id_list_header, (void *) lpvMem, (void *) hMapObject, shm_info_assign_func);

  return lpvMem;

error_exit:
  if (pEveryoneSID)
    {
      FreeSid (pEveryoneSID);
    }

  if (pAdminSID)
    {
      FreeSid (pAdminSID);
    }

  if (pACL)
    {
      LocalFree (pACL);
    }

  if (pSD)
    {
      LocalFree (pSD);
    }

  return NULL;
}
#else
void *
uw_shm_create (int shm_key, int size, int which_shm)
{
  int mid;
  void *p;

  if (size <= 0 || shm_key <= 0)
    return NULL;

  mid = shmget (shm_key, size, IPC_CREAT | IPC_EXCL | SHMODE);

  if (mid == -1)
    return NULL;
  p = shmat (mid, (char *) 0, 0);

  if (p == (void *) -1)
    return NULL;
  else
    {
      if (which_shm == SHM_APPL_SERVER)
	{
#ifdef USE_MUTEX
	  me_reset_sv (&(((T_SHM_APPL_SERVER *) p)->lock));
#endif /* USE_MUTEX */
	  ((T_SHM_APPL_SERVER *) p)->magic = MAGIC_NUMBER;
	}
      else if (which_shm == SHM_BROKER)
	{
#ifdef USE_MUTEX
	  me_reset_sv (&(((T_SHM_BROKER *) p)->lock));
#endif
	  ((T_SHM_BROKER *) p)->magic = MAGIC_NUMBER;
	}
      else if (which_shm == SHM_PROXY)
	{
#ifdef USE_MUTEX
	  me_reset_sv (&(((T_SHM_PROXY *) p)->lock));
#endif
	  ((T_SHM_PROXY *) p)->magic = MAGIC_NUMBER;
	}
    }
  return p;
}
#endif

/*
 * name:        uw_shm_destroy
 *
 * arguments:
 *		NONE
 *
 * returns/side-effects:
 *              void
 *
 * description: destroy shared memory
 *
 */
int
uw_shm_destroy (int shm_key)
{
#if defined(WINDOWS)
  return 0;
#else
  int mid;

  mid = shmget (shm_key, 0, SHMODE);

  if (mid == -1)
    {
      return -1;
    }

  if (shmctl (mid, IPC_RMID, 0) == -1)
    {
      return -1;
    }

  return 0;

#endif
}

T_SHM_BROKER *
broker_shm_initialize_shm_broker (int master_shm_id, T_BROKER_INFO * br_info, int br_num, int acl_flag, char *acl_file)
{
  int i, shm_size;
  T_SHM_BROKER *shm_br = NULL;
  unsigned char ip_addr[4];

  if (get_host_ip (ip_addr) < 0)
    {
      return NULL;
    }

  shm_size = sizeof (T_SHM_BROKER) + (br_num - 1) * sizeof (T_BROKER_INFO);

  shm_br = (T_SHM_BROKER *) uw_shm_create (master_shm_id, shm_size, SHM_BROKER);

  if (shm_br == NULL)
    {
      return NULL;
    }

  memcpy (shm_br->my_ip_addr, ip_addr, 4);
#if defined(WINDOWS)
  shm_br->magic = uw_shm_get_magic_number ();
#else /* WINDOWS */
  shm_br->owner_uid = getuid ();

  /* create a new session */
  setsid ();
#endif /* WINDOWS */

  shm_br->num_broker = br_num;
  shm_br->access_control = acl_flag;

  if (acl_file != NULL)
    {
      strncpy (shm_br->access_control_file, acl_file, sizeof (shm_br->access_control_file) - 1);
    }

  for (i = 0; i < br_num; i++)
    {
      shm_br->br_info[i] = br_info[i];

      if (br_info[i].shard_flag == OFF)
	{
	  get_access_log_file_name (shm_br->br_info[i].access_log_file, br_info[i].access_log_file, br_info[i].name,
				    CONF_LOG_FILE_LEN);
	}
      get_error_log_file_name (shm_br->br_info[i].error_log_file, br_info[i].error_log_file, br_info[i].name,
			       CONF_LOG_FILE_LEN);
    }

  return shm_br;
}

T_SHM_APPL_SERVER *
broker_shm_initialize_shm_as (T_BROKER_INFO * br_info_p, T_SHM_PROXY * shm_proxy_p)
{
  int as_index;
  T_SHM_APPL_SERVER *shm_as_p = NULL;
  T_APPL_SERVER_INFO *as_info_p = NULL;

  T_PROXY_INFO *proxy_info_p = NULL;
  T_SHARD_INFO *shard_info_p = NULL;
  T_SHARD_CONN_INFO *shard_conn_info_p = NULL;
  short proxy_id, shard_id, shard_cas_id;

  shm_as_p =
    (T_SHM_APPL_SERVER *) uw_shm_create (br_info_p->appl_server_shm_id, sizeof (T_SHM_APPL_SERVER), SHM_APPL_SERVER);

  if (shm_as_p == NULL)
    {
      return NULL;
    }

  shm_as_p->cci_default_autocommit = br_info_p->cci_default_autocommit;
  shm_as_p->job_queue_size = br_info_p->job_queue_size;
  shm_as_p->job_queue[0].id = 0;	/* initialize max heap */
  shm_as_p->max_prepared_stmt_count = br_info_p->max_prepared_stmt_count;

  shm_as_p->monitor_hang_flag = br_info_p->monitor_hang_flag;
  shm_as_p->monitor_server_flag = br_info_p->monitor_server_flag;
  memset (shm_as_p->unusable_databases_cnt, 0, sizeof (shm_as_p->unusable_databases_cnt));

  strcpy (shm_as_p->log_dir, br_info_p->log_dir);
  strcpy (shm_as_p->slow_log_dir, br_info_p->slow_log_dir);
  strcpy (shm_as_p->err_log_dir, br_info_p->err_log_dir);
  strcpy (shm_as_p->broker_name, br_info_p->name);

  shm_as_p->broker_port = br_info_p->port;
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
  shm_as_p->mysql_read_timeout = br_info_p->mysql_read_timeout;
  shm_as_p->mysql_keepalive_interval = br_info_p->mysql_keepalive_interval;
  shm_as_p->max_string_length = br_info_p->max_string_length;
  shm_as_p->stripped_column_name = br_info_p->stripped_column_name;
  shm_as_p->keep_connection = br_info_p->keep_connection;
  shm_as_p->cache_user_info = br_info_p->cache_user_info;
  shm_as_p->statement_pooling = br_info_p->statement_pooling;
  shm_as_p->access_mode = br_info_p->access_mode;
  shm_as_p->cci_pconnect = br_info_p->cci_pconnect;
  shm_as_p->access_log = br_info_p->access_log;
  shm_as_p->access_log_max_size = br_info_p->access_log_max_size;
  shm_as_p->jdbc_cache = br_info_p->jdbc_cache;
  shm_as_p->jdbc_cache_only_hint = br_info_p->jdbc_cache_only_hint;
  shm_as_p->jdbc_cache_life_time = br_info_p->jdbc_cache_life_time;

  shm_as_p->connect_order = br_info_p->connect_order;
  shm_as_p->replica_only_flag = br_info_p->replica_only_flag;

  shm_as_p->max_num_delayed_hosts_lookup = br_info_p->max_num_delayed_hosts_lookup;

  shm_as_p->trigger_action_flag = br_info_p->trigger_action_flag;

  shm_as_p->cas_rctime = br_info_p->cas_rctime;

  strcpy (shm_as_p->preferred_hosts, br_info_p->preferred_hosts);
  strcpy (shm_as_p->error_log_file, br_info_p->error_log_file);
  strcpy (shm_as_p->access_log_file, br_info_p->access_log_file);
  strcpy (shm_as_p->appl_server_name, get_appl_server_name (br_info_p->appl_server));
  ut_get_broker_port_name (shm_as_p->port_name, br_info_p->name, SHM_PROXY_NAME_MAX);
  strcpy (shm_as_p->db_connection_file, br_info_p->db_connection_file);

  for (as_index = 0; as_index < br_info_p->appl_server_max_num; as_index++)
    {
      as_info_p = &(shm_as_p->as_info[as_index]);
      broker_shm_set_as_info (shm_as_p, as_info_p, br_info_p, as_index);
    }

  shm_as_p->shard_flag = br_info_p->shard_flag;

#if defined (FOR_ODBC_GATEWAY)
  strcpy (shm_as_p->cgw_link_server, br_info_p->cgw_link_server);
  strcpy (shm_as_p->cgw_link_server_ip, br_info_p->cgw_link_server_ip);
  strcpy (shm_as_p->cgw_link_server_port, br_info_p->cgw_link_server_port);
  strcpy (shm_as_p->cgw_link_odbc_driver_name, br_info_p->cgw_link_odbc_driver_name);
  strcpy (shm_as_p->cgw_link_connect_url_property, br_info_p->cgw_link_connect_url_property);
#endif

  if (shm_as_p->shard_flag == OFF)
    {
      assert (shm_proxy_p == NULL);
      return shm_as_p;
    }

  strncpy_bufsize (shm_as_p->proxy_log_dir, br_info_p->proxy_log_dir);

  shm_as_p->proxy_log_max_size = br_info_p->proxy_log_max_size;

  shard_shm_set_shard_conn_info (shm_as_p, shm_proxy_p);

  for (proxy_id = 0; proxy_id < shm_proxy_p->num_proxy; proxy_id++)
    {
      proxy_info_p = &shm_proxy_p->proxy_info[proxy_id];

      for (shard_id = 0; shard_id < proxy_info_p->num_shard_conn; shard_id++)
	{
	  shard_info_p = &proxy_info_p->shard_info[shard_id];
	  shard_conn_info_p = &shm_as_p->shard_conn_info[shard_id];

	  proxy_info_p->fixed_shard_user = (shard_conn_info_p->db_user[0] != '\0') ? true : false;

	  for (shard_cas_id = 0; shard_cas_id < shard_info_p->max_appl_server; shard_cas_id++)
	    {
	      as_info_p = &(shm_as_p->as_info[shard_cas_id + shard_info_p->as_info_index_base]);

	      as_info_p->proxy_id = proxy_id;
	      as_info_p->shard_id = shard_id;
	      as_info_p->shard_cas_id = shard_cas_id;
	      as_info_p->fixed_shard_user = proxy_info_p->fixed_shard_user;
	      if (proxy_info_p->fixed_shard_user == true)
		{
		  strcpy (as_info_p->database_user, shard_conn_info_p->db_user);
		  strcpy (as_info_p->database_passwd, shard_conn_info_p->db_password);
		}

	      if (shard_cas_id < shard_info_p->min_appl_server)
		{
		  as_info_p->advance_activate_flag = 1;
		}
	      else
		{
		  as_info_p->advance_activate_flag = 0;
		}

	      as_info_p->proxy_conn_wait_timeout = br_info_p->proxy_conn_wait_timeout;
	      as_info_p->force_reconnect = false;
	    }
	}
    }

  return shm_as_p;
}

static void
broker_shm_set_as_info (T_SHM_APPL_SERVER * shm_appl, T_APPL_SERVER_INFO * as_info_p, T_BROKER_INFO * br_info_p,
			int as_index)
{
  as_info_p->service_flag = SERVICE_OFF;
  as_info_p->last_access_time = time (NULL);
  as_info_p->transaction_start_time = (time_t) 0;

  as_info_p->mutex_flag[SHM_MUTEX_BROKER] = FALSE;
  as_info_p->mutex_flag[SHM_MUTEX_ADMIN] = FALSE;
  as_info_p->mutex_turn = SHM_MUTEX_BROKER;

  as_info_p->num_request = 0;
  as_info_p->num_requests_received = 0;
  as_info_p->num_transactions_processed = 0;
  as_info_p->num_queries_processed = 0;
  as_info_p->num_long_queries = 0;
  as_info_p->num_long_transactions = 0;
  as_info_p->num_error_queries = 0;
  as_info_p->num_interrupts = 0;
  as_info_p->num_connect_requests = 0;
  as_info_p->num_connect_rejected = 0;
  as_info_p->num_restarts = 0;
  as_info_p->auto_commit_mode = FALSE;
  as_info_p->database_name[0] = '\0';
  as_info_p->database_host[0] = '\0';
  as_info_p->database_user[0] = '\0';
  as_info_p->database_passwd[0] = '\0';
  as_info_p->last_connect_time = 0;
  as_info_p->num_holdable_results = 0;
  as_info_p->cas_change_mode = CAS_CHANGE_MODE_DEFAULT;
  as_info_p->cur_sql_log_mode = br_info_p->sql_log_mode;
  as_info_p->cur_slow_log_mode = br_info_p->slow_log_mode;

  as_info_p->as_id = as_index;

  as_info_p->fn_status = -1;
  as_info_p->session_id = 0;
  return;
}

static void
shard_shm_set_shard_conn_info (T_SHM_APPL_SERVER * shm_as_p, T_SHM_PROXY * shm_proxy_p)
{
  T_SHARD_CONN_INFO *shard_conn_info_p;
  T_SHM_SHARD_CONN *shm_conn_p = NULL;
  T_SHM_SHARD_USER *shm_user_p = NULL;
  T_SHARD_CONN *conn_p = NULL;
  T_SHARD_USER *user_p = NULL;
  int i;


  shm_user_p = &shm_proxy_p->shm_shard_user;
  shm_conn_p = &shm_proxy_p->shm_shard_conn;

  user_p = &shm_user_p->shard_user[0];

  for (i = 0; i < shm_conn_p->num_shard_conn; i++)
    {
      conn_p = &shm_conn_p->shard_conn[i];
      shard_conn_info_p = &shm_as_p->shard_conn_info[i];

      strncpy_bufsize (shard_conn_info_p->db_user, user_p->db_user);
      strncpy_bufsize (shard_conn_info_p->db_name, conn_p->db_name);
      strncpy_bufsize (shard_conn_info_p->db_host, conn_p->db_conn_info);
      strncpy_bufsize (shard_conn_info_p->db_password, user_p->db_password);
    }
}


#if defined(WINDOWS)
void
uw_shm_detach (void *p)
{
  HANDLE hMapObject;

  SLEEP_MILISEC (0, 10);
  LINK_LIST_FIND_VALUE (hMapObject, shm_id_list_header, p, shm_id_cmp_func);
  if (hMapObject == NULL)
    return;

  UnmapViewOfFile (p);
  CloseHandle (hMapObject);
  link_list_node_delete (&shm_id_list_header, p, shm_id_cmp_func, NULL);
}
#else
void
uw_shm_detach (void *p)
{
  shmdt (p);
}
#endif

#if defined(WINDOWS)
int
uw_shm_get_magic_number ()
{
  return MAGIC_NUMBER;
}
#endif

#if defined(WINDOWS)
static int
shm_id_cmp_func (void *key1, void *key2)
{
  if (key1 == key2)
    return 1;
  return 0;
}

static int
shm_info_assign_func (T_LIST * node, void *key, void *value)
{
  node->key = key;
  node->value = value;
  return 0;
}

static char *
shm_id_to_name (int shm_key)
{
  static char shm_name[32];

  sprintf (shm_name, "Global\\v3mapfile%d", shm_key);
  return shm_name;
}
#endif /* WINDOWS */

static int
get_host_ip (unsigned char *ip_addr)
{
  char hostname[CUB_MAXHOSTNAMELEN];
  struct hostent *hp;

  if (gethostname (hostname, sizeof (hostname)) < 0)
    {
      fprintf (stderr, "gethostname error\n");
      return -1;
    }
  if ((hp = gethostbyname (hostname)) == NULL)
    {
      fprintf (stderr, "unknown host : %s\n", hostname);
      return -1;
    }
  memcpy ((void *) ip_addr, (void *) hp->h_addr_list[0], 4);

  return 0;
}


#if 0
static void
bs_log_msg (LPTSTR lpszMsg)
{
  char lpDisplayBuf[4096];
  sprintf (lpDisplayBuf, "%s", lpszMsg);
  MessageBox (NULL, (LPCTSTR) lpDisplayBuf, TEXT ("Error"), MB_OK);
}

static void
bs_last_error_msg (LPTSTR lpszFunction)
{
  LPVOID lpDisplayBuf;
  LPVOID lpMsgBuf;
  DWORD dw;

  dw = GetLastError ();
  FormatMessage (FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, dw,
		 MAKELANGID (LANG_NEUTRAL, SUBLANG_DEFAULT), (LPTSTR) & lpMsgBuf, 0, NULL);
  lpDisplayBuf =
    (LPVOID) LocalAlloc (LMEM_ZEROINIT,
			 (lstrlen ((LPCTSTR) lpMsgBuf) + lstrlen ((LPCTSTR) lpszFunction) + 40) * sizeof (TCHAR));
  sprintf ((LPTSTR) lpDisplayBuf, "%s failed with error %d: %s", lpszFunction, dw, lpMsgBuf);
  MessageBox (NULL, (LPCTSTR) lpDisplayBuf, TEXT ("Error"), MB_OK);
  LocalFree (lpMsgBuf);
  LocalFree (lpDisplayBuf);
}
#endif

#if defined (WINDOWS)
static int
uw_sem_open (char *sem_name, HANDLE * sem_handle)
{
  HANDLE new_handle;

  new_handle = OpenMutex (SYNCHRONIZE, FALSE, sem_name);
  if (new_handle == NULL)
    {
      return -1;
    }

  if (sem_handle)
    {
      *sem_handle = new_handle;
    }

  return 0;
}

int
uw_sem_init (char *sem_name)
{
  HANDLE sem_handle;

  sem_handle = CreateMutex (NULL, FALSE, sem_name);
  if (sem_handle == NULL && GetLastError () == ERROR_ALREADY_EXISTS)
    {
      sem_handle = OpenMutex (SYNCHRONIZE, FALSE, sem_name);
    }

  if (sem_handle == NULL)
    {
      return -1;
    }
  else
    {
      return 0;
    }
}
#else
int
uw_sem_init (sem_t * sem)
{
  return sem_init (sem, 1, 1);
}
#endif

#if defined (WINDOWS)
int
uw_sem_wait (char *sem_name)
{
  DWORD dwWaitResult;
  HANDLE sem_handle;

  if (uw_sem_open (sem_name, &sem_handle) != 0)
    {
      return -1;
    }

  dwWaitResult = WaitForSingleObject (sem_handle, INFINITE);
  switch (dwWaitResult)
    {
    case WAIT_OBJECT_0:
      return 0;
    case WAIT_TIMEOUT:
    case WAIT_FAILED:
    case WAIT_ABANDONED:
      return -1;
    }

  return -1;
}
#else
int
uw_sem_wait (sem_t * sem)
{
  return sem_wait (sem);
}
#endif

#if defined (WINDOWS)
int
uw_sem_post (char *sem_name)
{
  HANDLE sem_handle;

  if (uw_sem_open (sem_name, &sem_handle) != 0)
    {
      return -1;
    }

  if (ReleaseMutex (sem_handle) != 0)
    {
      return 0;
    }

  return -1;
}
#else
int
uw_sem_post (sem_t * sem)
{
  return sem_post (sem);
}
#endif

#if defined (WINDOWS)
int
uw_sem_destroy (char *sem_name)
{
  HANDLE sem_handle;

  if (uw_sem_open (sem_name, &sem_handle) != 0)
    {
      return -1;
    }

  if (sem_handle != NULL && CloseHandle (sem_handle) != 0)
    {
      return 0;
    }

  return -1;
}
#else
int
uw_sem_destroy (sem_t * sem)
{
  return sem_destroy (sem);
}
#endif

static void
get_access_log_file_name (char *access_log_file, char *access_log_path, char *broker_name, int len)
{
  snprintf (access_log_file, len, "%s/%s.access", access_log_path, broker_name);
}

static void
get_error_log_file_name (char *access_log_file, char *error_log_path, char *broker_name, int len)
{
  snprintf (access_log_file, len, "%s/%s.err", error_log_path, broker_name);
}

static const char *
get_appl_server_name (int appl_server_type)
{
  if (appl_server_type == APPL_SERVER_CAS_ORACLE)
    {
      return APPL_SERVER_CAS_ORACLE_NAME;
    }
  else if (appl_server_type == APPL_SERVER_CAS_MYSQL51)
    {
      return APPL_SERVER_CAS_MYSQL51_NAME;
    }
  else if (appl_server_type == APPL_SERVER_CAS_MYSQL)
    {
      return APPL_SERVER_CAS_MYSQL_NAME;
    }
  else if (appl_server_type == APPL_SERVER_CAS_CGW)
    {
      return APPL_SERVER_CAS_CGW_NAME;
    }
  return APPL_SERVER_CAS_NAME;
}
