/*
 * Copyright (C) 2008 Search Solution Corporation
 * Copyright (C) 2016 CUBRID Corporation
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
extern void logddl_set_file_name (const char *file_name);
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
extern void logddl_set_logging_enabled (bool enable);
extern void logddl_set_jsp_mode (bool mode);
extern bool logddl_get_jsp_mode ();

#endif /* _DDL_LOG_H_ */
