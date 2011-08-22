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
#include "broker_config.h"
#include "broker_max_heap.h"

#define 	STATE_KEEP_TRUE		1
#define		STATE_KEEP_FALSE	0

#define		UTS_STATUS_BUSY		1
#define		UTS_STATUS_IDLE		0
#define		UTS_STATUS_RESTART	2
#define 	UTS_STATUS_START	3
#if defined(WINDOWS)
#define		UTS_STATUS_BUSY_WAIT	4
#endif

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

#define IP_BYTE_COUNT           5
#define ACL_MAX_ITEM_COUNT      50
#define ACL_MAX_IP_COUNT        100
#define ACL_MAX_DBNAME_LENGTH	32
#define ACL_MAX_DBUSER_LENGTH	32

#if defined (WINDOWS)
#define MAKE_ACL_SEM_NAME(BUF, BROKER_NAME)  \
  snprintf(BUF, BROKER_NAME_LEN, "%s_acl_sem", BROKER_NAME)
#endif

typedef enum t_con_status T_CON_STATUS;
enum t_con_status
{
  CON_STATUS_OUT_TRAN = 0,
  CON_STATUS_IN_TRAN = 1,
  CON_STATUS_CLOSE = 2,
  CON_STATUS_CLOSE_AND_CONNECT = 3
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
  int session_id;		/* the session id (uw,v3) */
  int cas_log_reset;
  int cas_slow_log_reset;
  char service_flag;
  char reset_flag;
  char uts_status;		/* flag whether the uts is busy or idle */
  char clt_major_version;
  char clt_minor_version;
  char clt_patch_version;
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
#ifdef UNIXWARE711
  int clt_sock_fd;
#endif
#if defined(WINDOWS)
  unsigned char cas_clt_ip[4];
  int as_port;
  int pdh_pid;
  int pdh_workset;
  float pdh_pct_cpu;
  int cpu_time;
#endif
  char clt_appl_name[APPL_NAME_LENGTH];
  char clt_req_path_info[APPL_NAME_LENGTH];
  char clt_ip_addr[20];
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
  char database_name[32];
  char database_host[MAXHOSTNAMELEN + 1];
  char cci_default_autocommit;
  time_t last_connect_time;
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
#ifdef USE_MUTEX
  int lock;
#endif				/* USE_MUTEX */
  int magic;
  int appl_server_max_size;
  int session_timeout;
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
  T_APPL_SERVER_INFO as_info[1];
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
