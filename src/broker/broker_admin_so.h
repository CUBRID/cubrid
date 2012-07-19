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
 * broker_admin_so.h -
 */

#ifndef _BROKER_ADMIN_SO_H_
#define _BROKER_ADMIN_SO_H_

#ident "$Id$"

#include <time.h>

#if defined(WINDOWS)
#define DLL_EXPORT	__declspec(dllexport)
#else /* WINDOWS */
#define DLL_EXPORT
#endif /* WINDOWS */

#define FLAG_ON		1
#define FLAG_OFF	0
#define FLAG_SUSPEND	2
#define FLAG_READ_WRITE  0
#define FLAG_READ_ONLY 1
#define FLAG_SLAVE_ONLY 2

#if defined(CUBRID_SHARD)
#if !defined(MAX_HA_DBNAME_LENGTH)
#define MAX_HA_DBNAME_LENGTH		128
#endif /* !MAX_HA_DBNAME_LENGTH */
#endif /* CUBRID_SHARD */

typedef enum t_as_status T_AS_STATUS;
enum t_as_status
{
  AS_STATUS_IDLE,
  AS_STATUS_BUSY,
  AS_STATUS_RESTART,
  AS_STATUS_CLIENT_WAIT,
  AS_STATUS_CLOSE_WAIT
};

typedef struct t_job_info T_JOB_INFO;
struct t_job_info
{
  int id;
  int priority;
  unsigned char ip[4];
  time_t recv_time;
  char script[32];
  char prgname[32];
};

typedef struct t_as_info T_AS_INFO;
struct t_as_info
{
  char service_flag;
  int pid;
  int num_request;
#if !defined(CUBRID_SHARD)
  int as_port;
#endif				/* !CUBRID_SHARD */
  T_AS_STATUS status;
  time_t last_access_time;
  int psize;
  int num_thr;
  int cpu_time;
  float pcpu;
#if !defined(CUBRID_SHARD)
  char clt_ip_addr[20];
  char clt_appl_name[32];
  char request_file[64];
#endif				/* !CUBRID_SHARD */
  char log_msg[64];
#if defined(CUBRID_SHARD)
  char database_name[MAX_HA_DBNAME_LENGTH];
#else				/* CUBRID_SHARD */
  char database_name[32];
#endif				/* !CUBRID_SHARD */
  char database_host[MAXHOSTNAMELEN + 1];
  time_t last_connect_time;
  INT64 num_requests_received;
  INT64 num_transactions_processed;
  INT64 num_queries_processed;
  INT64 num_long_queries;
  INT64 num_long_transactions;
  INT64 num_error_queries;
  INT64 num_interrupts;
};

typedef struct t_br_info T_BR_INFO;
struct t_br_info
{
  char name[32];
  char as_type[4];
  int pid;
  int shm_id;
  int port;
  int num_as;
  int max_as;
  int min_as;
  int num_job_q;
  int num_thr;
  float pcpu;
  int cpu_time;
  int num_busy_count;
  int num_req;
  INT64 num_tran;
  INT64 num_query;
  INT64 num_long_query;
  INT64 num_long_tran;
  INT64 num_error_query;
  INT64 num_interrupts;
  int session_timeout;
  int as_max_size;
  int as_hard_limit;
  int time_to_kill;
  int long_query_time;		/* msec */
  int long_transaction_time;	/* msec */
  char keep_connection;
  char status;
  char auto_add_flag;
  char sql_log_mode;
  char slow_log_mode;
  char access_mode;
  char log_backup_flag;
  char source_env_flag;
  char access_list_flag;
  char log_dir[128];
  char slow_log_dir[128];
};

typedef struct t_uc_conf_item T_UC_CONF_ITEM;
struct t_uc_conf_item
{
  const char *name;
  char *value;
};

typedef struct t_br_conf T_BR_CONF;
struct t_br_conf
{
  int num;
  T_UC_CONF_ITEM *item;
};

typedef struct t_uc_conf T_UC_CONF;
struct t_uc_conf
{
  int num_header;
  T_UC_CONF_ITEM *header_conf;
  int num_broker;
  T_BR_CONF *br_conf;
};

typedef char *(*T_UC_VERSION_F) (void);
typedef int (*T_UC_START_F) (char *);
typedef int (*T_UC_STOP_F) (char *);
typedef int (*T_UC_ADD_F) (char *, char *);
typedef int (*T_UC_RESTART_F) (char *, int, char *);
typedef int (*T_UC_DROP_F) (char *, char *);
typedef int (*T_UC_ON_F) (char *, char *);
typedef int (*T_UC_OFF_F) (char *, char *);
typedef int (*T_UC_SUSPEND_F) (char *, char *);
typedef int (*T_UC_RESUME_F) (char *, char *);
typedef int (*T_UC_JOB_FIRST_F) (char *, int, char *);
typedef int (*T_UC_JOB_QUEUE_F) (char *, char *);
typedef int (*T_UC_AS_INFO_F) (char *, T_AS_INFO **, T_JOB_INFO **, int *,
			       char *);
typedef int (*T_UC_BR_INFO_F) (T_BR_INFO **, char *);
typedef void (*T_UC_INFO_FREE_F) (void *);
typedef int (*T_UC_UNICAS_CONF_F) (T_UC_CONF *, int *, char *);
typedef void (*T_UC_UNICAS_CONF_FREE_F) (T_UC_CONF *);
typedef int (*T_UC_CONF_BROKER_ADD_F) (T_UC_CONF *, char *, char *);
typedef void (*T_UC_CHANGE_CONFIG_F) (T_UC_CONF *, const char *, const char *,
				      const char *);
typedef int (*T_UC_CHANGER_F) (char *, char *, char *, char *);
typedef int (*T_UC_DEL_CAS_LOG_F) (char *, int, char *);
typedef int (*T_UC_GET_ACTIVE_SESSION_WITH_OPENED_SHM) (void *, char *);
typedef void *(*T_UCA_BROKER_SHM_OPEN) (char *err_msg);
typedef int (*T_UCA_GET_BR_NUM_WITH_OPENED_SHM) (void *shm_br, char *err_msg);
typedef int (*T_UCA_GET_BR_NAME_WITH_OPENED_SHM) (void *shm_br, int br_index,
						  char *name, int buffer_size,
						  char *err_msg);
typedef void *(*T_UCA_AS_SHM_OPEN) (void *shm_br, int br_index,
				    char *err_msg);
typedef int (*T_UCA_GET_AS_NUM_WITH_OPENED_SHM) (void *shm_br, int br_index,
						 char *err_msg);
typedef int (*T_UCA_GET_AS_REQS_RECEIVED_WITH_OPENED_SHM) (void *shm_as,
							   long long array[],
							   int array_size,
							   char *err_msg);
typedef int (*T_UCA_GET_AS_TRAN_PROCESSED_WITH_OPENED_SHM) (void *shm_as,
							    long long array[],
							    int array_size,
							    char *err_msg);
typedef int (*T_UCA_GET_AS_QUERY_PROCESSED_WITH_OPENED_SHM) (void *shm_as,
							     long long
							     array[],
							     int array_size,
							     char *err_msg);
typedef int (*T_UCA_SHM_DETACH) (void *p);

DLL_EXPORT const char *uc_version (void);
DLL_EXPORT int uc_start (char *err_msg);
DLL_EXPORT int uc_stop (char *err_msg);
DLL_EXPORT int uc_add (const char *br_name, char *err_msg);
DLL_EXPORT int uc_restart (const char *br_name, int as_index, char *err_msg);
DLL_EXPORT int uc_drop (const char *br_name, char *err_msg);
DLL_EXPORT int uc_on (const char *br_name, char *err_msg);
DLL_EXPORT int uc_off (const char *br_name, char *err_msg);
DLL_EXPORT int uc_suspend (const char *br_name, char *err_msg);
DLL_EXPORT int uc_resume (const char *br_name, char *err_msg);
DLL_EXPORT int uc_job_first (const char *br_name, int job_id, char *err_msg);
DLL_EXPORT int uc_job_queue (const char *br_name, char *err_msg);
DLL_EXPORT int uc_as_info (const char *br_name, T_AS_INFO ** ret_as_info,
			   T_JOB_INFO ** job_info, int *num_job,
			   char *err_msg);
DLL_EXPORT void uc_info_free (void *info);
DLL_EXPORT int uc_br_info (T_BR_INFO ** ret_br_info, char *err_msg);
DLL_EXPORT int uc_unicas_conf (T_UC_CONF * unicas_conf, int *ret_mst_shmid,
			       char *err_msg);
DLL_EXPORT void uc_unicas_conf_free (T_UC_CONF * unicas_conf);
DLL_EXPORT int uc_conf_broker_add (T_UC_CONF * unicas_conf,
				   const char *br_name, char *err_msg);
DLL_EXPORT void uc_change_config (T_UC_CONF * unicas_conf,
				  const char *br_name, const char *name,
				  const char *value);
DLL_EXPORT int uc_changer (const char *br_name, const char *name,
			   const char *value, char *err_msg);
DLL_EXPORT int uc_cas_changer (const char *br_name, const char *name,
			       const char *value, int as_number,
			       char *err_msg);
DLL_EXPORT int uc_del_cas_log (const char *br_name, int asid, char *errmsg);
DLL_EXPORT int uc_get_active_session_with_opened_shm (void *, char *);
DLL_EXPORT void *uc_broker_shm_open (char *err_msg);

DLL_EXPORT int uc_get_br_num_with_opened_shm (void *shm_br, char *err_msg);
DLL_EXPORT int uc_get_br_name_with_opened_shm (void *shm_br, int br_index,
					       char *name, int buffer_size,
					       char *err_msg);
DLL_EXPORT void *uc_as_shm_open (void *shm_br, int br_index, char *err_msg);
DLL_EXPORT int uc_get_as_num_with_opened_shm (void *shm_br, int br_index,
					      char *err_msg);
DLL_EXPORT int uc_get_as_reqs_received_with_opened_shm (void *shm_as,
							long long array[],
							int array_size,
							char *err_msg);
DLL_EXPORT int uc_get_as_tran_processed_with_opened_shm (void *shm_as,
							 long long array[],
							 int array_size,
							 char *err_msg);
DLL_EXPORT int uc_get_as_query_processed_with_opened_shm (void *shm_as,
							  long long array[],
							  int array_size,
							  char *err_msg);
DLL_EXPORT void uc_shm_detach (void *p);

#endif /* _BROKER_ADMIN_SO_H_ */
