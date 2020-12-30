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
 * ddl_log.h -
 */

#ifndef	_DDL_LOG_H_
#define	_DDL_LOG_H_

#ident "$Id$"

#define LOGDDL_RUN_EXECUTE_FUNC              (1)
#define LOGDDL_RUN_EXECUTE_BATCH_FUNC        (2)

#define LOGDDL_MSG_AUTO_COMMIT              "auto_commit"
#define LOGDDL_MSG_AUTO_ROLLBACK            "auto_rollback"

#define LOGDDL_TRAN_TYPE_COMMIT             "COMMIT"
#define LOGDDL_TRAN_TYPE_ROLLBACK           "ROLLBACK"

#define DDL_LOG_BUFFER_SIZE                 (8192)

typedef enum
{
  LOADDB_FILE_TYPE_NONE,
  LOADDB_FILE_TYPE_INPUT,
  LOADDB_FILE_TYPE_INDEX,
  LOADDB_FILE_TYPE_TRIGGER,
  LOADDB_FILE_TYPE_OBJECT,
  LOADDB_FILE_TYPE_SCHEMA
} T_LOADDB_FILE_TYPE;

typedef enum
{
  CSQL_INPUT_TYPE_NONE = -1,
  CSQL_INPUT_TYPE_FILE,
  CSQL_INPUT_TYPE_STRING,
  CSQL_INPUT_TYPE_EDITOR
} T_CSQL_INPUT_TYPE;

typedef enum
{
  APP_NAME_NONE,
  APP_NAME_CAS,
  APP_NAME_CSQL,
  APP_NAME_LOADDB
} T_APP_NAME;

extern void logddl_init ();
extern void logddl_free (bool all_free);
extern void logddl_destroy ();
extern void logddl_set_app_name (T_APP_NAME app_name);
extern void logddl_set_db_name (const char *db_name);
extern void logddl_set_user_name (const char *user_name);
extern void logddl_set_ip (const char *ip_addr);
extern void logddl_set_pid (const int pid);
extern void logddl_set_br_name (const char *br_name);
extern void logddl_set_br_index (const int index);
extern void logddl_set_sql_text (char *sql_text, int len);
extern void logddl_set_stmt_type (int stmt_type);
extern void logddl_set_loaddb_file_type (T_LOADDB_FILE_TYPE file_type);
extern void logddl_set_csql_input_type (T_CSQL_INPUT_TYPE input_type);
extern void logddl_set_load_filename (const char *load_filename);
extern void logddl_set_file_line (int file_line);
extern void logddl_set_err_msg (char *msg);
extern void logddl_set_err_code (int err_number);
extern void logddl_set_start_time (struct timeval *time_val);
extern void logddl_set_msg (const char *fmt, ...);
extern void logddl_set_execute_type (char type);
extern void logddl_set_commit_count (int count);
extern void logddl_write ();
extern void logddl_write_end ();
extern bool logddl_is_ddl_type (int node_type);
extern void logddl_set_commit_mode (bool mode);
extern void logddl_write_tran_str (const char *fmt, ...);
extern void logddl_write_end_for_csql_fileinput (const char *fmt, ...);
extern void logddl_set_logging_enabled (bool enable);
extern void logddl_set_jsp_mode (bool mode);
extern bool logddl_get_jsp_mode ();
#endif /* _DDL_LOG_H_ */
