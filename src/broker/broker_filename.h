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
 * broker_filename.h -
 */

#ifndef _BROKER_FILENAME_H_
#define _BROKER_FILENAME_H_

#ident "$Id$"

#include "porting.h"

#define APPL_SERVER_CAS_NAME            "cub_cas"
#define APPL_SERVER_CAS_ORACLE_NAME     "cub_cas_oracle"
#define APPL_SERVER_CAS_MYSQL51_NAME    "cub_cas_mysql51"
#define APPL_SERVER_CAS_MYSQL_NAME      "cub_cas_mysql"
#define APPL_SERVER_CAS_CGW_NAME        "cub_cas_cgw"

#define NAME_BROKER			"Tbroker"
#define NAME_PROXY			"cub_proxy"
#define NAME_CAS_BROKER			"cub_broker"
#if defined(WINDOWS)
#define NAME_CAS_BROKER2		"Cbroker2"
#define NAME_UC_SHM			"broker_shm"
#endif

#define CUBRID_BASE_DIR                 "log/broker/"

#ifdef DISPATCHER
#define ERROR_MSG_FILE			"uw_er.msg"
#endif

#define BROKER_PATH_MAX             (PATH_MAX)

/* default values */
#define DEFAULT_LOG_DIR               "log/broker/sql_log/"
#define DEFAULT_SLOW_LOG_DIR          DEFAULT_LOG_DIR
#define DEFAULT_ERR_DIR               "log/broker/error_log/"
#define DEFAULT_SHARD_PROXY_LOG_DIR   "log/broker/proxy_log/"
#define DEFAULT_ACCESS_LOG_DIR        "log/broker/"

#define ACCESS_LOG_DENIED_FILENAME_POSTFIX ".denied"

enum t_cubrid_file_id
{
  FID_CUBRID_BROKER_CONF,
  FID_UV_ERR_MSG,
  FID_V3_OUTFILE_DIR,
  FID_CAS_TMPGLO_DIR,
  FID_CAS_TMP_DIR,
  FID_VAR_DIR,
  FID_SOCK_DIR,
  FID_AS_PID_DIR,
  FID_ADMIND_PID,
  FID_SQL_LOG_DIR,
  FID_SQL_LOG2_DIR,
  FID_ADMIND_LOG,
  FID_MONITORD_LOG,
  FID_ER_HTML,
  FID_CUBRID_ERR_DIR,
  FID_CAS_FOR_ORACLE_DBINFO,
  FID_CAS_FOR_MYSQL_DBINFO,
  FID_ACCESS_CONTROL_FILE,
  FID_SLOW_LOG_DIR,
  FID_SHARD_DBINFO,
  FID_SHARD_PROXY_LOG_DIR,
  MAX_CUBRID_FILE
};
typedef enum t_cubrid_file_id T_CUBRID_FILE_ID;

typedef struct t_cubrid_file_info T_CUBRID_FILE_INFO;
struct t_cubrid_file_info
{
  T_CUBRID_FILE_ID fid;
  char file_name[BROKER_PATH_MAX];
};

extern void set_cubrid_home (void);
extern void set_cubrid_file (T_CUBRID_FILE_ID fid, char *value);
extern char *get_cubrid_file (T_CUBRID_FILE_ID fid, char *buf, size_t len);
extern char *get_cubrid_file_ptr (T_CUBRID_FILE_ID fid);
extern char *get_cubrid_home (void);
extern const char *getenv_cubrid_broker (void);

#endif /* _BROKER_FILENAME_H_ */
