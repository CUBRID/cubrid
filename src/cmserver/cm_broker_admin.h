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
 * cm_broker_admin.h -
 */

#ifndef _CM_BROKER_ADMIN_H_
#define _CM_BROKER_ADMIN_H_

#ident "$Id$"

#define BROKER_LOG_DIR  "log/broker"
#define UNICAS_CONF_DIR	"conf"
#define UNICAS_SQL_LOG_DIR "sql_log"

#define SQL_LOG_MODE_OFF                0x00
#define SQL_LOG_MODE_ON                 0x01
#define SQL_LOG_MODE_APPEND             0x02
#define SQL_LOG_MODE_BIND_VALUE         0x04

typedef enum
{
  UC_FID_ADMIN_LOG,
  UC_FID_UNICAS_CONF,
  UC_FID_CUBRID_CAS_CONF,
  UC_FID_CUBRID_BROKER_CONF
} T_UNICAS_FILE_ID;

typedef struct
{
  char *name;
  char *value;
} T_DM_UC_CONF_ITEM;

typedef struct
{
  int num;
  T_DM_UC_CONF_ITEM *item;
} T_DM_UC_BR_CONF;

typedef struct
{
  int num_header;
  T_DM_UC_CONF_ITEM *header_conf;
  int num_broker;
  T_DM_UC_BR_CONF *br_conf;
} T_DM_UC_CONF;

typedef struct
{
  int id;
  int pid;
  int num_request;
  int as_port;
  const char *status;
  time_t last_access_time;
  int psize;
  int num_thr;
  int cpu_time;
  float pcpu;
  char *clt_ip_addr;
  char *clt_appl_name;
  char *request_file;
  char *log_msg;
} T_DM_UC_AS_INFO;

typedef struct
{
  int id;
  int priority;
  unsigned char ip[4];
  time_t recv_time;
  char script[32];
  char prgname[32];
} T_DM_UC_JOB_INFO;

typedef struct
{
  char *name;
  char as_type[4];
  int pid;
  int port;
  int shm_id;
  int num_as;
  int max_as;
  int min_as;
  int num_job_q;
  int num_thr;
  float pcpu;
  int cpu_time;
  int num_req;
  char *session_timeout;
  int as_max_size;
  char log_backup;
  char sql_log_on_off;
  char sql_log_append_mode;
  char sql_log_bind_value;
  char source_env_flag;
  char access_list_flag;
  int sql_log_time;
  int time_to_kill;
  char status[16];
  char auto_add[4];
  char *log_dir;
} T_DM_UC_BR_INFO;

typedef struct
{
  int num_info;
  union
  {
    T_DM_UC_AS_INFO *as_info;
    T_DM_UC_BR_INFO *br_info;
    T_DM_UC_JOB_INFO *job_info;
  } info;
} T_DM_UC_INFO;

char *uca_version (void);
int uca_init (char *err_msg);
int uca_start (char *err_msg);
int uca_stop (char *err_msg);
int uca_add (char *br_name, char *err_msg);
int uca_restart (char *br_name, int as_index, char *err_msg);
int uca_drop (char *br_name, char *err_msg);
int uca_on (char *br_name, char *err_msg);
int uca_off (char *br_name, char *err_msg);
int uca_suspend (char *br_name, char *err_msg);
int uca_resume (char *br_name, char *err_msg);
int uca_job_first (char *br_name, int job_id, char *err_msg);
int uca_job_q_size (char *br_name, char *err_msg);
int uca_as_info (char *br_name, T_DM_UC_INFO * br_info,
		 T_DM_UC_INFO * job_info, char *err_msg);
void uca_as_info_free (T_DM_UC_INFO * br_info, T_DM_UC_INFO * job_info);
int uca_br_info (T_DM_UC_INFO * uc_info, char *err_msg);
void uca_br_info_free (T_DM_UC_INFO * uc_info);
int uca_unicas_conf (T_DM_UC_CONF * dm_uc_conf, int *ret_mst_shmid,
		     char *err_msg);
void uca_unicas_conf_free (T_DM_UC_CONF * dm_uc_conf);
int uca_conf_broker_add (T_DM_UC_CONF * dm_uc_conf, char *br_name,
			 char *err_msg);
int uca_change_config (T_DM_UC_CONF * dm_uc_conf, const char *br_name,
		       const char *name, const char *value);
int uca_changer (char *br_name, char *name, char *value, char *err_msg);
char *uca_get_file (T_UNICAS_FILE_ID uc_fid, char *buf);
char *uca_cpu_time_str (int t, char *buf);
char *uca_conf_find (T_DM_UC_BR_CONF * br_conf, const char *name);
T_DM_UC_BR_CONF *uca_conf_find_broker (T_DM_UC_CONF * uc_conf, char *br_name);
char *uca_get_conf_path (const char *filename, char *buf);
int uca_conf_write (T_DM_UC_CONF * uc_conf, char *del_broekr,
		    char *_dbmt_error);
int uca_del_cas_log (char *br_name, int as_id, char *_dbmt_error);
#ifdef DIAG_DEVEL
int uca_get_active_session_with_opened_shm (void *br_shm, char *err_msg);
void *uca_broker_shm_open (char *err_msg);
#if 0				/* ACTIVITY PROFILE */
int uca_get_br_diagconfig_with_opened_shm (void *shm_br, void *c_config,
					   char *err_msg);
int uca_set_br_diagconfig_with_opened_shm (void *shm_br, void *c_config,
					   char *err_msg);
#endif
int uca_get_br_num_with_opened_shm (void *shm_br, char *err_msg);
int uca_get_br_name_with_opened_shm (void *shm_br, int br_index, char *name,
				     int buffer_size, char *err_msg);
void *uca_as_shm_open (void *shm_br, int br_index, char *err_msg);
int uca_get_as_num_with_opened_shm (void *shm_br, int br_index,
				    char *err_msg);
int uca_get_as_reqs_received_with_opened_shm (void *shm_as, long long array[],
					      int array_size, char *err_msg);
int uca_get_as_tran_processed_with_opened_shm (void *shm_as,
					       long long array[],
					       int array_size, char *err_msg);
int uca_shm_detach (void *p);
#endif

#endif /* _CM_BROKER_ADMIN_H_ */
