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
 * broker_config.h - broker configuration utilities
 */

#ifndef	_BROKER_CONFIG_H_
#define	_BROKER_CONFIG_H_

#include "config.h"

#define	APPL_SERVER_CAS		0
#define	APPL_SERVER_CAS_ORACLE	1
#define	APPL_SERVER_CAS_MYSQL	2

#define IS_APPL_SERVER_TYPE_CAS(x)	\
	((x == APPL_SERVER_CAS) || (x == APPL_SERVER_CAS_ORACLE) || (x == APPL_SERVER_CAS_MYSQL))
#define IS_NOT_APPL_SERVER_TYPE_CAS(x)	!IS_APPL_SERVER_TYPE_CAS(x)

#define APPL_SERVER_CAS_TYPE_NAME			"CAS"
#define APPL_SERVER_CAS_ORACLE_TYPE_NAME	"CAS_ORACLE"
#define APPL_SERVER_CAS_MYSQL_TYPE_NAME		"CAS_MYSQL"

#define MAX_BROKER_NUM          100

#define	CONF_LOG_FILE_LEN	128

#define	DEFAULT_AS_MIN_NUM	5
#define	DEFAULT_AS_MAX_NUM	40

#if defined (WINDOWS)
#if __WORDSIZE == 64
#define	DEFAULT_SERVER_MAX_SIZE	80
#elif __WORDSIZE == 32
#define DEFAULT_SERVER_MAX_SIZE 40
#else
#error "Error __WORDSIZE"
#endif
#else
#define	DEFAULT_SERVER_MAX_SIZE	0
#endif

#define	DEFAULT_TIME_TO_KILL	120	/* seconds */
#define SQL_LOG_TIME_MAX	-1

#define CONF_ERR_LOG_NONE       0x00
#define CONF_ERR_LOG_LOGFILE    0x01
#define CONF_ERR_LOG_BROWSER    0x02
#define CONF_ERR_LOG_BOTH       (CONF_ERR_LOG_LOGFILE | CONF_ERR_LOG_BROWSER)

#define DEFAULT_SQL_LOG_MAX_SIZE	100000	/* 100M */
#define DEFAULT_LONG_QUERY_TIME         60
#define DEFAULT_LONG_TRANSACTION_TIME   60
#define MAX_SQL_LOG_MAX_SIZE            2000000

#define BROKER_NAME_LEN		64
#define BROKER_LOG_MSG_SIZE	64


typedef enum t_sql_log_value T_SQL_LOG_MODE_VALUE;
enum t_sql_log_mode_value
{
  SQL_LOG_MODE_NONE = 0,
  SQL_LOG_MODE_ERROR = 1,
  SQL_LOG_MODE_TIMEOUT = 2,
  SQL_LOG_MODE_NOTICE = 3,
  SQL_LOG_MODE_ALL = 4,
  SQL_LOG_MODE_DEFAULT = SQL_LOG_MODE_ERROR
};

typedef enum t_keep_con_value T_KEEP_CON_VALUE;
enum t_keep_con_value
{
  KEEP_CON_OFF = 0,
  KEEP_CON_ON = 1,
  KEEP_CON_AUTO = 2,
  KEEP_CON_DEFAULT = KEEP_CON_AUTO
};

typedef enum t_access_mode_value T_ACCESS_MODE_VALUE;
enum t_access_mode_value
{
  READ_WRITE_ACCESS_MODE = 0,
  READ_ONLY_ACCESS_MODE = 1,
  SLAVE_ONLY_ACCESS_MODE = 2
};

typedef struct t_broker_info T_BROKER_INFO;
struct t_broker_info
{
  char service_flag;
  char appl_server;
  char auto_add_appl_server;
  char log_backup;
  char access_log;
  char sql_log_mode;
  char stripped_column_name;
  char keep_connection;
  char cache_user_info;
  char sql_log2;
  char statement_pooling;
  char access_mode;
  char name[BROKER_NAME_LEN];
  int pid;
  int port;
  int appl_server_num;
  int appl_server_min_num;
  int appl_server_max_num;
#if defined (WINDOWS)
  int appl_server_port;
#endif
  int appl_server_shm_id;
  int appl_server_max_size;
  int session_timeout;
  int job_queue_size;
  int time_to_kill;
  int err_code;
  int os_err_code;
  int sql_log_max_size;
  int long_query_time;		/* msec */
  int long_transaction_time;	/* msec */
#if defined (WINDOWS)
  int pdh_workset;
  float pdh_pct_cpu;
  int cpu_time;
  int pdh_num_thr;
#endif
  int max_string_length;
  int num_busy_count;
  char log_dir[CONF_LOG_FILE_LEN];
  char err_log_dir[CONF_LOG_FILE_LEN];
  char access_log_file[CONF_LOG_FILE_LEN];
  char error_log_file[CONF_LOG_FILE_LEN];
  char source_env[CONF_LOG_FILE_LEN];
  char acl_file[CONF_LOG_FILE_LEN];

  char jdbc_cache;
  char jdbc_cache_only_hint;
  char cci_pconnect;
  char select_auto_commit;
  int jdbc_cache_life_time;
  char ready_to_service;
};

extern int broker_config_read (const char *conf_file, T_BROKER_INFO * br_info,
			       int *num_broker, int *br_shm_id,
			       char *admin_log_file, char admin_flag,
			       char *admin_err_msg);
extern void broker_config_dump (FILE * fp, const T_BROKER_INFO * br_info,
				int num_broker, int br_shm_id);

extern int conf_get_value_table_on_off (const char *value);
extern int conf_get_value_sql_log_mode (const char *value);
extern int conf_get_value_keep_con (const char *value);

#endif /* _BROKER_CONFIG_H_ */
