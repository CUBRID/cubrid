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
 * broker_shm.h -
 */

#ifndef _BROKER_SHM_H_
#define _BROKER_SHM_H_

#ident "$Id$"

#include <sys/types.h>
#if defined(WINDOWS)
#include <winsock2.h>
#include <windows.h>
#else
#include <semaphore.h>
#endif

#include "porting.h"
#include "broker_env_def.h"
#include "broker_config.h"
#include "broker_max_heap.h"
#include "cas_protocol.h"

#define 	STATE_KEEP_TRUE		1
#define		STATE_KEEP_FALSE	0

#define		UTS_STATUS_BUSY		1
#define		UTS_STATUS_IDLE		0
#define		UTS_STATUS_RESTART	2
#define 	UTS_STATUS_START	3
#if defined(WINDOWS)
#define		UTS_STATUS_BUSY_WAIT	4
#endif
#define         UTS_STATUS_CON_WAIT     5
#define 	UTS_STATUS_STOP		6

#define 	PROXY_STATUS_BUSY	1
#define		PROXY_STATUS_RESTART	2
#define		PROXY_STATUS_START	3

#define 	MAX_NUM_UTS_ADMIN	10

#define         DEFAULT_SHM_KEY         0x3f5d1c0a

#define        SHM_APPL_SERVER      0
#define        SHM_BROKER           1
#define        SHM_PROXY            2

/* definition for mutex variable */
#define		SHM_MUTEX_BROKER	0
#define		SHM_MUTEX_ADMIN	1

/* con_status lock/unlock */
#define		CON_STATUS_LOCK_BROKER		0
#define		CON_STATUS_LOCK_CAS		1
#if defined(WINDOWS)
#define		CON_STATUS_LOCK_INIT(AS_INFO)				\
		do {							\
		  (AS_INFO)->con_status_lock[CON_STATUS_LOCK_BROKER] = FALSE;  \
		  (AS_INFO)->con_status_lock[CON_STATUS_LOCK_CAS] = FALSE;   \
		  (AS_INFO)->con_status_lock_turn = CON_STATUS_LOCK_BROKER;  \
                }							\
		while (0)

#define		CON_STATUS_LOCK_DESTROY(AS_INFO)

#define		CON_STATUS_LOCK(AS_INFO, LOCK_OWNER)			\
		do {							\
		  int LOCK_WAITER = (LOCK_OWNER == CON_STATUS_LOCK_BROKER) ?  CON_STATUS_LOCK_CAS : CON_STATUS_LOCK_BROKER;				\
		  (AS_INFO)->con_status_lock[LOCK_OWNER] = TRUE;		\
		  (AS_INFO)->con_status_lock_turn = LOCK_WAITER;		\
		  while (((AS_INFO)->con_status_lock[LOCK_WAITER] == TRUE) &&  \
		  	 ((AS_INFO)->con_status_lock_turn == LOCK_WAITER))  \
		  {							\
		    SLEEP_MILISEC(0, 10);                                \
		  }	\
		} while (0)

#define		CON_STATUS_UNLOCK(AS_INFO, LOCK_OWNER)	\
		(AS_INFO)->con_status_lock[LOCK_OWNER] = FALSE
#else /* WINDOWS */
#define		CON_STATUS_LOCK_INIT(AS_INFO)	\
		  uw_sem_init (&((AS_INFO)->con_status_sem));

#define		CON_STATUS_LOCK_DESTROY(AS_INFO)	\
		  uw_sem_destroy (&((AS_INFO)->con_status_sem));

#define		CON_STATUS_LOCK(AS_INFO, LOCK_OWNER)	\
		  uw_sem_wait (&(AS_INFO)->con_status_sem);

#define		CON_STATUS_UNLOCK(AS_INFO, LOCK_OWNER)	\
		  uw_sem_post (&(AS_INFO)->con_status_sem);
#endif /* WINDOWS */
#define		SHM_LOG_MSG_SIZE	256

#define		APPL_NAME_LENGTH	128

#define		JOB_QUEUE_MAX_SIZE	2048

#define MAX_CRYPT_STR_LENGTH            32

#define APPL_SERVER_NAME_MAX_SIZE	32

#define CAS_LOG_RESET_REOPEN          0x01
#define CAS_LOG_RESET_REMOVE            0x02
#define PROXY_LOG_RESET_REOPEN 		0x01

#define MAX_DBNAME_LENGTH       (64)	/* maximum length of mysql database name and '\0' */
#define MAX_CONN_INFO_LENGTH    ((CUB_MAXHOSTNAMELEN + 1) * 2)	/* host1:host2 */

#define IP_BYTE_COUNT           5
#define ACL_MAX_ITEM_COUNT      50
#define ACL_MAX_IP_COUNT        256
#define ACL_MAX_DBNAME_LENGTH   (SRV_CON_DBNAME_SIZE)
#define ACL_MAX_DBUSER_LENGTH   (SRV_CON_DBUSER_SIZE)

#define MAX_PROXY_NUM            8

#define APPL_SERVER_NUM_LIMIT    4096

#define SHM_BROKER_PATH_MAX      (PATH_MAX)
#define SHM_PROXY_NAME_MAX       (SHM_BROKER_PATH_MAX)
#define SHM_APPL_SERVER_NAME_MAX (SHM_BROKER_PATH_MAX)

#define MAX_SHARD_USER           (4)
#define MAX_SHARD_KEY            (2)
#define MAX_SHARD_CONN           (256)

#define SHARD_KEY_COLUMN_LEN     (32)
#define SHARD_KEY_RANGE_MAX      (256)

#define UNUSABLE_DATABASE_MAX    (200)
#define PAIR_LIST                (2)

/*
 * proxy need to reserve FD for
 * broker, cas, log etc..
 */
#if defined(LINUX)
#define PROXY_RESERVED_FD 	(256)
#else /* LINUX */
#define PROXY_RESERVED_FD 	(128)
#endif /* !LINUX */

#define MAX_QUERY_TIMEOUT_LIMIT         86400	/* seconds; 1 day */
#define LONG_QUERY_TIME_LIMIT           (MAX_QUERY_TIMEOUT_LIMIT)
#define LONG_TRANSACTION_TIME_LIMIT     (MAX_QUERY_TIMEOUT_LIMIT)
#define MAX_PROXY_TIMEOUT_LIMIT         (MAX_QUERY_TIMEOUT_LIMIT)

#if defined (WINDOWS)
#define MAKE_ACL_SEM_NAME(BUF, BROKER_NAME)  \
  snprintf(BUF, BROKER_NAME_LEN, "%s_acl_sem", BROKER_NAME)
#endif

#define MIN_MYSQL_KEEPALIVE_INTERVAL		60	/* 60s */

#define         SEQ_NUMBER              1
#define         MAGIC_NUMBER            (MAJOR_VERSION * 1000000 + MINOR_VERSION * 10000 + SEQ_NUMBER)

typedef enum
{
  SERVICE_OFF = 0,
  SERVICE_ON = 1,
  SERVICE_OFF_ACK = 2,
  SERVICE_UNKNOWN = 3
} T_BROKER_SERVICE_STATUS;

enum t_con_status
{
  CON_STATUS_OUT_TRAN = 0,
  CON_STATUS_IN_TRAN = 1,
  CON_STATUS_CLOSE = 2,
  CON_STATUS_CLOSE_AND_CONNECT = 3
};
typedef enum t_con_status T_CON_STATUS;

#if defined(WINDOWS)
typedef INT64 int64_t;
#endif

typedef struct ip_info IP_INFO;
struct ip_info
{
  unsigned char address_list[ACL_MAX_IP_COUNT * IP_BYTE_COUNT];
  time_t last_access_time[ACL_MAX_IP_COUNT];
  int num_list;
};

typedef struct access_list ACCESS_INFO;
struct access_list
{
  char dbname[ACL_MAX_DBNAME_LENGTH];
  char dbuser[ACL_MAX_DBUSER_LENGTH];
  char ip_files[LINE_MAX + 1];	/* reserve buffer for '\0' */
  IP_INFO ip_info;
};

typedef struct t_shard_conn_info T_SHARD_CONN_INFO;
struct t_shard_conn_info
{
  char db_name[MAX_DBNAME_LENGTH];
  char db_host[MAX_CONN_INFO_LENGTH];
  char db_user[SRV_CON_DBUSER_SIZE];
  char db_password[SRV_CON_DBPASSWD_SIZE];
};

/* NOTE: Be sure not to include any pointer type in shared memory segment
 * since the processes will not care where the shared memory segment is
 * attached
 */

/* SHARD USER */
typedef struct t_shard_user T_SHARD_USER;
struct t_shard_user
{
  char db_name[SRV_CON_DBNAME_SIZE];
  char db_user[SRV_CON_DBUSER_SIZE];
  char db_password[SRV_CON_DBPASSWD_SIZE];
};

typedef struct t_shm_shard_user T_SHM_SHARD_USER;
struct t_shm_shard_user
{
  int num_shard_user;
  T_SHARD_USER shard_user[1];
};

/* SHARD KEY */
typedef struct t_shard_key_range T_SHARD_KEY_RANGE;
struct t_shard_key_range
{
  int key_index;
  int range_index;

  int min;
  int max;
  int shard_id;
};

typedef struct t_shard_key T_SHARD_KEY;
struct t_shard_key
{
  char key_column[SHARD_KEY_COLUMN_LEN];
  int num_key_range;
  T_SHARD_KEY_RANGE range[SHARD_KEY_RANGE_MAX];
};

typedef struct t_shm_shard_key T_SHM_SHARD_KEY;
struct t_shm_shard_key
{
  int num_shard_key;
  T_SHARD_KEY shard_key[MAX_SHARD_KEY];
};

/* SHARD CONN */
typedef struct t_shard_conn T_SHARD_CONN;
struct t_shard_conn
{
  int shard_id;
  char db_name[MAX_DBNAME_LENGTH];
  char db_conn_info[MAX_CONN_INFO_LENGTH];
};

typedef struct t_shm_shard_conn T_SHM_SHARD_CONN;
struct t_shm_shard_conn
{
  int num_shard_conn;
  T_SHARD_CONN shard_conn[MAX_SHARD_CONN];
};

/* appl_server information */
typedef struct t_appl_server_info T_APPL_SERVER_INFO;
struct t_appl_server_info
{
  int num_request;		/* number of request */
  int pid;			/* the process id */
  int psize;
  time_t psize_time;
  int cas_log_reset;
  int cas_slow_log_reset;
  int cas_err_log_reset;
  char service_flag;
  char reset_flag;
  char uts_status;		/* flag whether the uts is busy or idle */
  T_BROKER_VERSION clt_version;
  char driver_info[SRV_CON_CLIENT_INFO_SIZE];
  char driver_version[SRV_CON_VER_STR_MAX_SIZE];
  char cas_client_type;
  char service_ready_flag;
  char con_status;
  char cur_keep_con;
  char cur_sql_log_mode;
  char cur_sql_log2;
  char cur_slow_log_mode;
  char cur_statement_pooling;
#if defined(WINDOWS)
  char close_flag;
#endif
  time_t last_access_time;	/* last access time */
  time_t transaction_start_time;
  time_t claimed_alive_time;	/* to check if the cas hangs */
#ifdef UNIXWARE711
  int clt_sock_fd;
#endif
  unsigned char cas_clt_ip[4];
  unsigned short cas_clt_port;
#if defined(WINDOWS)
  int as_port;
  int pdh_pid;
  int pdh_workset;
  float pdh_pct_cpu;
  int cpu_time;
#endif
  char clt_appl_name[APPL_NAME_LENGTH];
  char clt_req_path_info[APPL_NAME_LENGTH];
  char mutex_flag[2];		/* for mutex */
  char mutex_turn;
#if defined (WINDOWS)
  char con_status_lock[2];
  char con_status_lock_turn;
#else
  sem_t con_status_sem;
#endif
  char log_msg[SHM_LOG_MSG_SIZE];
  INT64 num_requests_received;
  INT64 num_transactions_processed;
  INT64 num_queries_processed;
  INT64 num_long_queries;
  INT64 num_long_transactions;
  INT64 num_error_queries;
  INT64 num_interrupts;
  char auto_commit_mode;
  bool fixed_shard_user;
  char database_name[SRV_CON_DBNAME_SIZE];
  char database_host[CUB_MAXHOSTNAMELEN];
  char database_user[SRV_CON_DBUSER_SIZE];
  char database_passwd[SRV_CON_DBPASSWD_SIZE];
  char cci_default_autocommit;
  time_t last_connect_time;
  INT64 num_connect_requests;
  INT64 num_connect_rejected;
  INT64 num_restarts;
  int num_holdable_results;
  int cas_change_mode;

  INT64 num_select_queries;
  INT64 num_insert_queries;
  INT64 num_update_queries;
  INT64 num_delete_queries;

  INT64 num_unique_error_queries;
  int isolation_level;
  int lock_timeout;

  short proxy_id;
  short shard_id;
  short shard_cas_id;
  short as_id;
  unsigned int session_id;
  int fn_status;

  int advance_activate_flag;	/* it is used only in shard */
  int proxy_conn_wait_timeout;	/* it is used only in shard */
  bool force_reconnect;		/* it is used only in shard */
};

typedef struct t_client_info T_CLIENT_INFO;
struct t_client_info
{
  int client_id;		/* client id */
  int client_ip;		/* client ip address */
  time_t connect_time;		/* first connect time */
  char driver_info[SRV_CON_CLIENT_INFO_SIZE];
  char driver_version[SRV_CON_VER_STR_MAX_SIZE];

  int func_code;		/* current request function code */

  /* SHARD TODO : not implemented yet */
#if 0
  int shard_id;			/* scheduled shard id */
  int cas_id;			/* scheduled cas id */
#endif

  time_t req_time;		/* current request receive from client time */
  time_t res_time;		/* current response receive from cas time */

  /* TODO : MORE STATISTICS INFOMATION per Client INT64 num_queries_processed; INT64 num_error_queries; */
  int isolation_level;
  int lock_timeout;
};

typedef struct t_shard_info T_SHARD_INFO;
struct t_shard_info
{
  int shard_id;

  int min_appl_server;
  int max_appl_server;
  int num_appl_server;

  /* shard queue stat */
  INT64 waiter_count;

  int as_info_index_base;
};

typedef struct t_shm_shard_conn_stat T_SHM_SHARD_CONN_STAT;
struct t_shm_shard_conn_stat
{
  int shard_id;

  INT64 num_hint_key_queries_requested;
  INT64 num_hint_id_queries_requested;
  INT64 num_no_hint_queries_requested;
};

typedef struct t_shm_shard_key_range_stat T_SHM_SHARD_KEY_RANGE_STAT;
struct t_shm_shard_key_range_stat
{
  int min;
  int max;
  int shard_id;

  INT64 num_range_queries_requested;
};

typedef struct t_shm_shard_key_stat T_SHM_SHARD_KEY_STAT;
struct t_shm_shard_key_stat
{
  int num_key_range;

  T_SHM_SHARD_KEY_RANGE_STAT stat[SHARD_KEY_RANGE_MAX];
};

/* NOTICE :
 *  If you want to add struct member,
 *  you must modify shard_shm_get_shard_info_offset too.
 */
typedef struct t_proxy_info T_PROXY_INFO;
struct t_proxy_info
{
  int proxy_id;
  int pid;
  int service_flag;		/* SERVICE_OFF | SERVICE_ON */
  int status;			/* SHARD TODO : not defined yet */
  int cur_proxy_log_mode;
  int max_shard;
  int max_client;
  int cur_client;
  int max_context;

  char appl_server;		/* APPL_SERVER_CAS | APPL_SERVER_CAS_MYSQL | APPL_SERVER_CAS_ORACLE */

  /* MOVE FROM T_APPL_SERVER_INFO */
#if defined(WINDOWS)
  int proxy_port;
#else
  int dummy;			/* for align */
#endif

  int wait_timeout;
  INT64 stmt_waiter_count;

  int max_prepared_stmt_count;
  char ignore_shard_hint;

  int proxy_log_reset;
  int proxy_access_log_reset;

  char port_name[SHM_PROXY_NAME_MAX];
  bool fixed_shard_user;

  /* MOVE FROM T_BROKER_INFO */
  char access_log_file[CONF_LOG_FILE_LEN];

  /* statistics information */
  INT64 num_hint_none_queries_processed;
  INT64 num_hint_key_queries_processed;
  INT64 num_hint_id_queries_processed;
  INT64 num_hint_all_queries_processed;
  INT64 num_hint_err_queries_processed;

  INT64 num_proxy_error_processed;

  INT64 num_request_stmt;
  INT64 num_request_stmt_in_pool;

  INT64 num_connect_requests;
  INT64 num_connect_rejected;
  INT64 num_restarts;

  /* hang check info */
  time_t claimed_alive_time;

  int num_shard_key;		/* size : T_SHM_SHARD_KEY->num_shard_key */
  int num_shard_conn;		/* size : T_SHM_SHARD_CONN->num_shard_conn */

  int appl_server_shm_id;
  T_SHM_SHARD_KEY_STAT key_stat[SHARD_KEY_STAT_SIZE_LIMIT];
  T_SHM_SHARD_CONN_STAT shard_stat[SHARD_CONN_STAT_SIZE_LIMIT];

  T_SHARD_INFO shard_info[SHARD_INFO_SIZE_LIMIT];
  T_CLIENT_INFO client_info[CLIENT_INFO_SIZE_LIMIT + PROXY_RESERVED_FD];
};

typedef struct t_shm_proxy T_SHM_PROXY;
struct t_shm_proxy
{
#ifdef USE_MUTEX
  int lock;
#endif				/* USE_MUTEX */
  int magic;

  int num_proxy;
  int max_client;
  int max_context;

  /* SHARD SHARD_KEY_ID */
  int shard_key_modular;
  char shard_key_library_name[SHM_PROXY_NAME_MAX];
  char shard_key_function_name[SHM_PROXY_NAME_MAX];

  T_SHM_SHARD_USER shm_shard_user;
  T_SHM_SHARD_CONN shm_shard_conn;
  T_SHM_SHARD_KEY shm_shard_key;

  T_PROXY_INFO proxy_info[MAX_PROXY_NUM];
};

/* database server */
typedef struct t_db_server T_DB_SERVER;
struct t_db_server
{
  char database_name[SRV_CON_DBNAME_SIZE];
  char database_host[CUB_MAXHOSTNAMELEN];
  int state;
};

typedef struct t_shm_appl_server T_SHM_APPL_SERVER;
struct t_shm_appl_server
{
  char access_log;
  char sql_log_mode;
  char slow_log_mode;
  char stripped_column_name;
  char keep_connection;
  char cache_user_info;
  char sql_log2;
  char statement_pooling;
  char access_mode;
  char jdbc_cache;
  char jdbc_cache_only_hint;
  char cci_pconnect;
  char cci_default_autocommit;
  char shard_flag;
  bool access_control;
  int jdbc_cache_life_time;
  int connect_order;
  int replica_only_flag;
  int max_num_delayed_hosts_lookup;	/* max num of HA delayed hosts to lookup */
  char trigger_action_flag;	/* enable or disable trigger action */

#if defined(WINDOWS)
  int as_port;
  int use_pdh_flag;
#endif
  char log_dir[CONF_LOG_FILE_LEN];
  char slow_log_dir[CONF_LOG_FILE_LEN];
  char err_log_dir[CONF_LOG_FILE_LEN];
  char broker_name[BROKER_NAME_LEN];
  char appl_server_name[APPL_SERVER_NAME_MAX_SIZE];
  char preferred_hosts[SHM_APPL_SERVER_NAME_MAX];

  char access_log_file[CONF_LOG_FILE_LEN];
  char db_connection_file[BROKER_INFO_PATH_MAX];
  unsigned char local_ip_addr[4];

  /* from br_info */
  /* from here, these are used only in shard */
  char source_env[CONF_LOG_FILE_LEN];
  char error_log_file[CONF_LOG_FILE_LEN];
  char proxy_log_dir[CONF_LOG_FILE_LEN];
  char port_name[SHM_APPL_SERVER_NAME_MAX];
  int proxy_log_max_size;
  int access_log_max_size;
  /* to here, these are used only in shard */

#ifdef USE_MUTEX
  int lock;
#endif				/* USE_MUTEX */
  int magic;
  int broker_port;
  int appl_server_max_size;
  int appl_server_hard_limit;
  int session_timeout;
  int query_timeout;
  int mysql_read_timeout;
  int mysql_keepalive_interval;
  int num_appl_server;
  int max_string_length;
  int job_queue_size;
  int sql_log_max_size;
  int long_query_time;		/* msec */
  int long_transaction_time;	/* msec */
  int max_prepared_stmt_count;
  int num_access_info;
  int acl_chn;
  int cas_rctime;		/* sec */
  int unusable_databases_cnt[PAIR_LIST];
  unsigned int unusable_databases_seq;
  bool monitor_hang_flag;
  bool monitor_server_flag;
#if !defined(WINDOWS)
  sem_t acl_sem;
#endif

  ACCESS_INFO access_info[ACL_MAX_ITEM_COUNT];

  T_MAX_HEAP_NODE job_queue[JOB_QUEUE_MAX_SIZE + 1];

  T_SHARD_CONN_INFO shard_conn_info[SHARD_INFO_SIZE_LIMIT];	/* it is used only in shard */

  T_APPL_SERVER_INFO as_info[APPL_SERVER_NUM_LIMIT];

  T_DB_SERVER unusable_databases[PAIR_LIST][UNUSABLE_DATABASE_MAX];
};

/* shared memory information */
typedef struct t_shm_broker T_SHM_BROKER;
struct t_shm_broker
{
#ifdef USE_MUTEX
  int lock;			/* shared variable for mutual excl */
#endif				/* USE_MUTEX */
  int magic;			/* the magic number */
  unsigned char my_ip_addr[4];
#if !defined(WINDOWS)
  uid_t owner_uid;
#endif				/* !WINDOWS */
  int num_broker;		/* number of broker */
  char access_control_file[SHM_BROKER_PATH_MAX];
  bool access_control;
  T_BROKER_INFO br_info[1];
};

enum t_shm_mode
{
  SHM_MODE_ADMIN = 0,
  SHM_MODE_MONITOR = 1
};
typedef enum t_shm_mode T_SHM_MODE;

void *uw_shm_open (int shm_key, int which_shm, T_SHM_MODE shm_mode);
void *uw_shm_create (int shm_key, int size, int which_shm);
int uw_shm_destroy (int shm_key);
void uw_shm_detach (void *);
#if defined(WINDOWS)
int uw_shm_get_magic_number ();
#endif

#if defined(WINDOWS)
int uw_sem_init (char *sem_name);
int uw_sem_wait (char *sem_name);
int uw_sem_post (char *sem_name);
int uw_sem_destroy (char *sem_name);
#else
int uw_sem_init (sem_t * sem_t);
int uw_sem_wait (sem_t * sem_t);
int uw_sem_post (sem_t * sem_t);
int uw_sem_destroy (sem_t * sem_t);
#endif
T_SHM_BROKER *broker_shm_initialize_shm_broker (int master_shm_id, T_BROKER_INFO * br_info, int br_num, int acl_flag,
						char *acl_file);
T_SHM_APPL_SERVER *broker_shm_initialize_shm_as (T_BROKER_INFO * br_info_p, T_SHM_PROXY * shm_proxy_p);

#endif /* _BROKER_SHM_H_ */
