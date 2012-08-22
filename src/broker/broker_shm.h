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
 * broker_shm.h -
 */

#ifndef	_BROKER_SHM_H_
#define	_BROKER_SHM_H_

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
#if defined(CUBRID_SHARD)
#include "shard_metadata.h"
#endif /* CUBRID_SHARD */

#define 	STATE_KEEP_TRUE		1
#define		STATE_KEEP_FALSE	0

#define		UTS_STATUS_BUSY		1
#define		UTS_STATUS_IDLE		0
#define		UTS_STATUS_RESTART	2
#define 	UTS_STATUS_START	3
#if defined(WINDOWS)
#define		UTS_STATUS_BUSY_WAIT	4
#endif
#if defined(CUBRID_SHARD)
#define         UTS_STATUS_CON_WAIT     5
#endif /* CUBRID_SHARD */

#define 	PROXY_STATUS_BUSY	1
#define		PROXY_STATUS_RESTART	2
#define		PROXY_STATUS_START	3

#define 	MAX_NUM_UTS_ADMIN	10

#define         DEFAULT_SHM_KEY         0x3f5d1c0a

#define		SHM_APPL_SERVER		0
#define		SHM_BROKER		1

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

#define		SERVICE_OFF		0
#define		SERVICE_ON		1
#define		SERVICE_OFF_ACK		2

#define		JOB_QUEUE_MAX_SIZE	511

/* SHM_APPL_SERVER->suspend_mode */
#define		SUSPEND_NONE			0
#define		SUSPEND_REQ			1
#define		SUSPEND				2
#define		SUSPEND_CHANGE_PRIORITY_REQ	3
#define		SUSPEND_CHANGE_PRIORITY		4
#define		SUSPEND_END_CHANGE_PRIORITY	5

#define MAX_CRYPT_STR_LENGTH            32

#define APPL_SERVER_NAME_MAX_SIZE	32

#define		SERVICE_READY_WAIT_COUNT	3000
#define		SERVICE_READY_WAIT(SERVICE_READY_FLAG)			\
		do {							\
		  int	i;						\
		  for (i=0 ; i < SERVICE_READY_WAIT_COUNT ; i++) {	\
		    if ((SERVICE_READY_FLAG) == TRUE) {			\
		      break;						\
		    }							\
		    else {						\
		      SLEEP_MILISEC(0, 10);				\
		    }							\
		  }							\
		} while (0)

#define CAS_LOG_RESET_REOPEN          0x01
#define CAS_LOG_RESET_REMOVE            0x02
#if defined(CUBRID_SHARD)
#define PROXY_LOG_RESET_REOPEN 		0x01
#endif /* CUBRID_SHARD */

#define IP_BYTE_COUNT           5
#define ACL_MAX_ITEM_COUNT      50
#define ACL_MAX_IP_COUNT        100
#define ACL_MAX_DBNAME_LENGTH	32
#define ACL_MAX_DBUSER_LENGTH	32

#define MAX_QUERY_TIMEOUT_LIMIT         86400	/* seconds; 1 day */

#if defined (WINDOWS)
#define MAKE_ACL_SEM_NAME(BUF, BROKER_NAME)  \
  snprintf(BUF, BROKER_NAME_LEN, "%s_acl_sem", BROKER_NAME)
#endif

#if defined(CUBRID_SHARD)
#if !defined(MAX_HA_DBNAME_LENGTH)
#define MAX_HA_DBNAME_LENGTH 		128
#endif /* !MAX_HA_DBNAME_LENGTH */
#endif /* CUBRID_SHARD */

typedef enum t_con_status T_CON_STATUS;
enum t_con_status
{
  CON_STATUS_OUT_TRAN = 0,
  CON_STATUS_IN_TRAN = 1,
  CON_STATUS_CLOSE = 2,
  CON_STATUS_CLOSE_AND_CONNECT = 3,
  CON_STATUS_OUT_TRAN_HOLDABLE = 4	/* OUT_TRAN with holdable result sets */
};

#if defined(WINDOWS)
typedef INT64 int64_t;
#endif

typedef struct ip_info IP_INFO;
struct ip_info
{
  unsigned char address_list[ACL_MAX_IP_COUNT * IP_BYTE_COUNT];
  int num_list;
};

typedef struct access_list ACCESS_INFO;
struct access_list
{
  char dbname[ACL_MAX_DBNAME_LENGTH];
  char dbuser[ACL_MAX_DBUSER_LENGTH];
  char ip_files[LINE_MAX];
  IP_INFO ip_info;
};

/* NOTE: Be sure not to include any pointer type in shared memory segment
 * since the processes will not care where the shared memory segment is
 * attached
 */

/* appl_server information */
typedef struct t_appl_server_info T_APPL_SERVER_INFO;
struct t_appl_server_info
{
  int num_request;		/* number of request */
  int pid;			/* the process id */
  int psize;
  time_t psize_time;
#if defined(CUBRID_SHARD)
  int session_id;		/* the session id (uw,v3) */
#endif
  int cas_log_reset;
  int cas_slow_log_reset;
  char service_flag;
  char reset_flag;
  char uts_status;		/* flag whether the uts is busy or idle */
  T_BROKER_VERSION clt_version;
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
  char cookie_str[MAX_CRYPT_STR_LENGTH];
  char log_msg[SHM_LOG_MSG_SIZE];
  INT64 num_requests_received;
  INT64 num_transactions_processed;
  INT64 num_queries_processed;
  INT64 num_long_queries;
  INT64 num_long_transactions;
  INT64 num_error_queries;
  INT64 num_interrupts;
  char auto_commit_mode;
#if defined(CUBRID_SHARD)
  char database_name[MAX_HA_DBNAME_LENGTH];
#else				/* CUBRID_SHARD */
  char database_name[32];
#endif				/* !CUBRID_SHARD */
  char database_host[MAXHOSTNAMELEN + 1];
  char cci_default_autocommit;
  time_t last_connect_time;
  INT64 num_connect_requests;
  INT64 num_restarts;
};

#if defined(CUBRID_SHARD)
typedef struct t_client_info T_CLIENT_INFO;
struct t_client_info
{
  int client_id;		/* client id */
  int client_ip;		/* client ip address */
  time_t connect_time;		/* first connect time */

  int func_code;		/* current request function code */

  /* SHARD TODO : not implemented yet */
#if 0
  int shard_id;			/* scheduled shard id */
  int cas_id;			/* scheduled cas id */
#endif

  time_t req_time;		/* current request receive from client time */
  time_t res_time;		/* current response receive from cas time */

  /* TODO : MORE STATISTICS INFOMATION per Client 
   *  INT64 num_queries_processed;
   *  INT64 num_error_queries;
   */

  /* CLIENT INFO. MOVE FROM T_APPL_SERVER_INFO */
  char clt_appl_name[APPL_NAME_LENGTH];
  char clt_req_path_info[APPL_NAME_LENGTH];
  char clt_ip_addr[20];
};

typedef struct t_shard_info T_SHARD_INFO;
struct t_shard_info
{
  int next;

  int shard_id;
  int status;			/* SHARD TODO : not defined yet */
  char service_ready_flag;

  int min_appl_server;
  int max_appl_server;
  int num_appl_server;

  /* connection info */
  char db_name[SRV_CON_DBNAME_SIZE];
  char db_user[SRV_CON_DBUSER_SIZE];
  char db_password[SRV_CON_DBPASSWD_SIZE];

  char db_conn_info[LINE_MAX];	/* cubrid - hostname(ip) 
				 * mysql  - hostname(ip):port
				 * oracle - tns 
				 */
  T_APPL_SERVER_INFO as_info[1];
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
  char key_column[SHARD_KEY_COLUMN_LEN];
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
  int next;

  int proxy_id;
  int pid;
  int service_flag;		/* SERVICE_OFF | SERVICE_ON */
  int status;			/* SHARD TODO : not defined yet */
  int cur_proxy_log_mode;
  int max_shard;
  int max_client;
  int cur_client;

  char appl_server;		/* APPL_SERVER_CAS | APPL_SERVER_CAS_MYSQL | APPL_SERVER_CAS_ORACLE */

  /* MOVE FROM T_APPL_SERVER_INFO */
#if defined(WINDOWS)
  int proxy_port;		/* as_port */
#else
  int dummy;			/* for align */
#endif

  int max_prepared_stmt_count;
  char ignore_shard_hint;

  int proxy_log_reset;
  int proxy_access_log_reset;

  char port_name[PATH_MAX];

  /* MOVE FROM T_BROKER_INFO */
  char access_log_file[CONF_LOG_FILE_LEN];

  /* statistics information */
  INT64 num_hint_none_queries_processed;
  INT64 num_hint_key_queries_processed;
  INT64 num_hint_id_queries_processed;
  INT64 num_hint_all_queries_processed;

  int num_shard_key;		/* size : T_SHM_SHARD_KEY->num_shard_key */
  int num_shard_conn;		/* size : T_SHM_SHARD_CONN->num_shard_conn */
  T_SHM_SHARD_KEY_STAT key_stat[1];
  T_SHM_SHARD_CONN_STAT shard_stat[1];

  T_CLIENT_INFO client_info[1];
  T_SHARD_INFO shard_info[1];
};

typedef struct t_shm_proxy T_SHM_PROXY;
struct t_shm_proxy
{
  char shard_name[SHARD_NAME_LEN];
  int metadata_shm_id;

  int min_num_proxy;
  int max_num_proxy;
  int max_client;

  /* SHARD SHARD_KEY_ID */
  int shard_key_modular;
  char shard_key_library_name[PATH_MAX];
  char shard_key_function_name[PATH_MAX];

  T_PROXY_INFO proxy_info[1];
};
#endif /* CUBRID_SHARD */

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
  char sql_log_single_line;
  char access_mode;
  char jdbc_cache;
  char jdbc_cache_only_hint;
  char cci_pconnect;
  char select_auto_commit;
  char cci_default_autocommit;
  bool access_control;
  int jdbc_cache_life_time;

#if defined(WINDOWS)
  int as_port;
  int use_pdh_flag;
#endif
  char log_dir[PATH_MAX];
  char slow_log_dir[PATH_MAX];
  char err_log_dir[PATH_MAX];
  char broker_name[BROKER_NAME_LEN];
  char appl_server_name[APPL_SERVER_NAME_MAX_SIZE];
  char preferred_hosts[LINE_MAX];

#if defined(CUBRID_SHARD)
  /* from br_info */
  char source_env[CONF_LOG_FILE_LEN];
  char error_log_file[CONF_LOG_FILE_LEN];
  char proxy_log_dir[CONF_LOG_FILE_LEN];
  char port_name[PATH_MAX];
  int proxy_log_max_size;
#endif				/* CUBRID_SHARD */

#ifdef USE_MUTEX
  int lock;
#endif				/* USE_MUTEX */
  int magic;
  int appl_server_max_size;
  int appl_server_hard_limit;
  int session_timeout;
  int query_timeout;
  int num_appl_server;
  int suspend_mode;
  int max_string_length;
  int job_queue_size;
  int sql_log_max_size;
  int long_query_time;		/* msec */
  int long_transaction_time;	/* msec */
  int max_prepared_stmt_count;
  int num_access_info;
  int acl_chn;
#if !defined(WINDOWS)
  sem_t acl_sem;
#endif

  ACCESS_INFO access_info[ACL_MAX_ITEM_COUNT];

  INT64 dummy1;
  T_MAX_HEAP_NODE job_queue[JOB_QUEUE_MAX_SIZE + 1];
  INT64 dummy2;

  /* SHARD TODO : will delete - it it only for build */
  T_APPL_SERVER_INFO as_info[1];

#if defined(CUBRID_SHARD)
  T_SHM_PROXY shard_proxy;
#endif				/* CUBRID_SHARD */
};

/* shared memory information */
typedef struct t_shm_broker T_SHM_BROKER;
struct t_shm_broker
{
#ifdef USE_MUTEX
  int lock;			/* shared variable for mutual excl */
#endif				/* USE_MUTEX */
  int magic;			/* the magic number */
#if defined(WINDOWS)
  unsigned char my_ip_addr[4];
#else
  uid_t owner_uid;
#endif
  int num_broker;		/* number of broker */
  char access_control_file[PATH_MAX];
  bool access_control;
  T_BROKER_INFO br_info[1];
};

typedef enum t_shm_mode T_SHM_MODE;
enum t_shm_mode
{
  SHM_MODE_ADMIN = 0,
  SHM_MODE_MONITOR = 1
};

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

#endif /* _BROKER_SHM_H_ */
