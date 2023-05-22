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
 * broker_config.h - broker configuration utilities
 */

#ifndef _BROKER_CONFIG_H_
#define _BROKER_CONFIG_H_

#include "config.h"
#include "cas_protocol.h"
#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
#include "environment_variable.h"
#endif /* !CAS_FOR_ORACLE && !CAS_FOR_MYSQL */
#include "porting.h"

#define	APPL_SERVER_CAS           0
#define	APPL_SERVER_CAS_ORACLE    1
#define APPL_SERVER_CAS_MYSQL51   2
#define APPL_SERVER_CAS_MYSQL     3
#define APPL_SERVER_CAS_CGW       4

#define IS_APPL_SERVER_TYPE_CAS(x)	\
        ((x == APPL_SERVER_CAS) || (x == APPL_SERVER_CAS_ORACLE) || \
            (x == APPL_SERVER_CAS_MYSQL51) || (x == APPL_SERVER_CAS_MYSQL) || \
            (x == APPL_SERVER_CAS_CGW))

#define IS_NOT_APPL_SERVER_TYPE_CAS(x)	!IS_APPL_SERVER_TYPE_CAS(x)

#define APPL_SERVER_CAS_TYPE_NAME               "CAS"
#define APPL_SERVER_CAS_ORACLE_TYPE_NAME        "CAS_ORACLE"
#define APPL_SERVER_CAS_MYSQL_TYPE_NAME         "CAS_MYSQL"
#define APPL_SERVER_CAS_MYSQL51_TYPE_NAME       "CAS_MYSQL51"
#define APPL_SERVER_CAS_MYSQL61_TYPE_NAME       "CAS_MYSQL61"
#define APPL_SERVER_CAS_CGW_TYPE_NAME           "CAS_CGW"

#define MAX_BROKER_NUM          50

#define	CONF_LOG_FILE_LEN	128

#define	DEFAULT_AS_MIN_NUM	5
#define	DEFAULT_AS_MAX_NUM	40

#if defined (WINDOWS)
#if __WORDSIZE == 64
#define	DEFAULT_SERVER_MAX_SIZE	"80M"
#elif __WORDSIZE == 32
#define DEFAULT_SERVER_MAX_SIZE "40M"
#else
#error "Error __WORDSIZE"
#endif
#else
#define	DEFAULT_SERVER_MAX_SIZE	"0"
#endif

#define	DEFAULT_SERVER_HARD_LIMIT	"1G"

#define	DEFAULT_TIME_TO_KILL	"2min"
#define SQL_LOG_TIME_MAX	-1

#define CONF_ERR_LOG_NONE       0x00
#define CONF_ERR_LOG_LOGFILE    0x01
#define CONF_ERR_LOG_BROWSER    0x02
#define CONF_ERR_LOG_BOTH       (CONF_ERR_LOG_LOGFILE | CONF_ERR_LOG_BROWSER)

#define DEFAULT_SQL_LOG_MAX_SIZE	"10M"
#define DEFAULT_LONG_QUERY_TIME         "1min"
#define DEFAULT_LONG_TRANSACTION_TIME   "1min"
#define DEFAULT_ACCESS_LOG_MAX_SIZE     "10M"
#define MAX_SQL_LOG_MAX_SIZE            2097152	/* 2G */
#define MAX_ACCESS_LOG_MAX_SIZE         2097152	/* 2G */
#define DEFAULT_MAX_NUM_DELAYED_HOSTS_LOOKUP    -1

#define BROKER_NAME_LEN		64
#define BROKER_LOG_MSG_SIZE	64

#if !defined(BROKER_PATH_MAX)
#define BROKER_PATH_MAX       (PATH_MAX)
#endif

#define DEFAULT_SHARD_NUM_PROXY	 1
#define DEFAULT_SHARD_MAX_CLIENTS 	 256

#define DEFAULT_SHARD_PROXY_LOG_MAX_SIZE	"100M"
#define MAX_PROXY_LOG_MAX_SIZE		        1048576	/* about 1G */

#define SHARD_CONN_STAT_SIZE_LIMIT       256
#define SHARD_KEY_STAT_SIZE_LIMIT        2
#define CLIENT_INFO_SIZE_LIMIT           10000
#define SHARD_INFO_SIZE_LIMIT            256

#define BROKER_INFO_PATH_MAX             (PATH_MAX)
#define BROKER_INFO_NAME_MAX             (BROKER_INFO_PATH_MAX)

#define DEFAULT_SSL_MODE                 "OFF"

#define CGW_LINK_SERVER_NAME_LEN	 256
#define CGW_LINK_SERVER_IP_LEN		 32
#define CGW_LINK_SERVER_PORT_LEN	 10
#define CGW_LINK_ODBC_DRIVER_NAME_LEN	 256
#define CGW_LINK_CONNECT_URL_PROPERTY_LEN	512
#define CGW_LINK_STRING_FORMAT_LEN 128
#define CGW_LINK_URL_MAX_LEN	         (CGW_LINK_SERVER_IP_LEN                 \
                                          + CGW_LINK_SERVER_PORT_LEN             \
                                          + CGW_LINK_ODBC_DRIVER_NAME_LEN        \
                                          + CGW_LINK_CONNECT_URL_PROPERTY_LEN    \
                                          + SRV_CON_DBNAME_SIZE                  \
                                          + SRV_CON_DBUSER_SIZE                  \
                                          + SRV_CON_DBPASSWD_SIZE)               \
                                          + CGW_LINK_STRING_FORMAT_LEN           \


enum t_sql_log_mode_value
{
  SQL_LOG_MODE_NONE = 0,
  SQL_LOG_MODE_ERROR = 1,
  SQL_LOG_MODE_TIMEOUT = 2,
  SQL_LOG_MODE_NOTICE = 3,
  SQL_LOG_MODE_ALL = 4,
  SQL_LOG_MODE_DEFAULT = SQL_LOG_MODE_ERROR
};
typedef enum t_sql_log_mode_value T_SQL_LOG_MODE_VALUE;

enum t_slow_log_value
{
  SLOW_LOG_MODE_OFF = 0,
  SLOW_LOG_MODE_ON = 1,
  SLOW_LOG_MODE_DEFAULT = SLOW_LOG_MODE_ON
};
typedef enum t_slow_log_value T_SLOW_LOG_VALUE;

enum t_keep_con_value
{
  KEEP_CON_ON = 1,
  KEEP_CON_AUTO = 2,
  KEEP_CON_DEFAULT = KEEP_CON_AUTO
};
typedef enum t_keep_con_value T_KEEP_CON_VALUE;

enum t_access_mode_value
{
  READ_WRITE_ACCESS_MODE = 0,
  READ_ONLY_ACCESS_MODE = 1,
  SLAVE_ONLY_ACCESS_MODE = 2,
};
typedef enum t_access_mode_value T_ACCESS_MODE_VALUE;

/* dbi.h must be updated when a new order is added */
enum t_connect_order_value
{
  CONNECT_ORDER_SEQ = 0,
  CONNECT_ORDER_RANDOM = 1,
  CONNECT_ORDER_DEFAULT = CONNECT_ORDER_SEQ
};
typedef enum t_connect_order_value T_CONNECT_ORDER_VALUE;

enum t_proxy_log_mode_value
{
  PROXY_LOG_MODE_NONE = 0,
  PROXY_LOG_MODE_ERROR = 1,
  PROXY_LOG_MODE_TIMEOUT = 2,
  PROXY_LOG_MODE_NOTICE = 3,
  PROXY_LOG_MODE_SHARD_DETAIL = 4,
  PROXY_LOG_MODE_SCHEDULE_DETAIL = 5,
  PROXY_LOG_MODE_DEBUG = 6,
  PROXY_LOG_MODE_ALL = 7,
  PROXY_LOG_MODE_DEFAULT = SQL_LOG_MODE_ERROR
};

typedef enum t_proxy_log_mode_value T_PROXY_LOG_MODE_VALUE;

typedef struct t_broker_info T_BROKER_INFO;
struct t_broker_info
{
  char service_flag;
  char appl_server;
  char auto_add_appl_server;
  char log_backup;
  char access_log;
  char sql_log_mode;
  char slow_log_mode;
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
  int appl_server_hard_limit;
  int session_timeout;
  int query_timeout;
  int mysql_read_timeout;
  int mysql_keepalive_interval;
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
  int max_prepared_stmt_count;
  int access_log_max_size;	/* kbytes */
  char log_dir[CONF_LOG_FILE_LEN];
  char slow_log_dir[CONF_LOG_FILE_LEN];
  char err_log_dir[CONF_LOG_FILE_LEN];
  char access_log_dir[CONF_LOG_FILE_LEN];
  char access_log_file[CONF_LOG_FILE_LEN];
  char error_log_file[CONF_LOG_FILE_LEN];
  char source_env[CONF_LOG_FILE_LEN];
  char acl_file[CONF_LOG_FILE_LEN];
  char preferred_hosts[BROKER_INFO_NAME_MAX];
  char db_connection_file[BROKER_INFO_PATH_MAX];

  char jdbc_cache;
  char jdbc_cache_only_hint;
  char cci_pconnect;
  int jdbc_cache_life_time;
  char ready_to_service;
  char cci_default_autocommit;

  int monitor_hang_interval;
  int hang_timeout;
  int reject_client_count;

  char monitor_server_flag;
  char monitor_hang_flag;
  char reject_client_flag;	/* reject clients due to hanging cas/proxy */

  int connect_order;
  int replica_only_flag;
  int max_num_delayed_hosts_lookup;	/* max num of HA delayed hosts to lookup */

  int cas_rctime;		/* sec */

  char trigger_action_flag;	/* enable or disable trigger action */

  char shard_flag;
  /* from here, these are used only in shard */
  int proxy_shm_id;

  char proxy_log_mode;

  char shard_db_name[SRV_CON_DBNAME_SIZE];
  char shard_db_user[SRV_CON_DBUSER_SIZE];
  char shard_db_password[SRV_CON_DBPASSWD_SIZE];

  int num_proxy;
  char proxy_log_dir[CONF_LOG_FILE_LEN];
  int max_client;

  char shard_connection_file[BROKER_INFO_PATH_MAX];
  char shard_key_file[BROKER_INFO_PATH_MAX];

  /* SHARD SHARD_KEY_ID */
  int shard_key_modular;
  char shard_key_library_name[BROKER_INFO_NAME_MAX];
  char shard_key_function_name[BROKER_INFO_NAME_MAX];

  int proxy_log_max_size;
  int proxy_max_prepared_stmt_count;
  int proxy_conn_wait_timeout;

  char ignore_shard_hint;
  int proxy_timeout;
  /* to here, these are used only in shard */

  char use_SSL;

  char cgw_link_server[CGW_LINK_SERVER_NAME_LEN];
  char cgw_link_server_ip[CGW_LINK_SERVER_IP_LEN];
  char cgw_link_server_port[CGW_LINK_SERVER_PORT_LEN];
  char cgw_link_odbc_driver_name[CGW_LINK_ODBC_DRIVER_NAME_LEN];
  char cgw_link_connect_url_property[CGW_LINK_CONNECT_URL_PROPERTY_LEN];
};

extern int broker_config_read (const char *conf_file, T_BROKER_INFO * br_info, int *num_broker, int *br_shm_id,
			       char *admin_log_file, char admin_flag, bool * acl_flag, char *acl_file,
			       char *admin_err_msg);
extern void broker_config_dump (FILE * fp, const T_BROKER_INFO * br_info, int num_broker, int br_shm_id);

extern int conf_get_value_table_on_off (const char *value);
extern int conf_get_value_sql_log_mode (const char *value);
extern int conf_get_value_keep_con (const char *value);
extern int conf_get_value_access_mode (const char *value);
extern int conf_get_value_connect_order (const char *value);

extern int conf_get_value_proxy_log_mode (const char *value);

extern void dir_repath (char *path, size_t path_len);

#if defined(SHARD_VERBOSE_DEBUG)
#if defined (WINDOWS)
#define SHARD_ERR(f, ...) do { \
fprintf(stdout, "[%-35s:%05d] <ERR> " f, __FILE__, __LINE__, _VA_ARGS_); \
} while (0);

#define SHARD_INF(f, ...)	do { \
fprintf(stdout, "[%-35s:%05d] <INF> " f, __FILE__, __LINE__, _VA_ARGS_); \
} while (0);
#else /* WINDOWS */
#define SHARD_ERR(f, a...) do { \
fprintf(stdout, "[%-35s:%05d] <ERR> " f, __FILE__, __LINE__, ##a); \
} while (0);

#define SHARD_INF(f, a...)	do { \
fprintf(stdout, "[%-35s:%05d] <INF> " f, __FILE__, __LINE__, ##a); \
} while (0);
#endif /* !WINDOWS */
#else /* SHARD_VERBOSE_DEBUG */
#if defined (WINDOWS)
#define SHARD_ERR(f, ...)
#define SHARD_INF(f, ...)
#else /* WINDOWS */
#define SHARD_ERR(f, a...)
#define SHARD_INF(f, a...)
#endif /* !WINDOWS */
#endif

#endif /* _BROKER_CONFIG_H_ */
