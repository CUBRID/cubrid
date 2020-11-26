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
 * cub_ddl_log.h -
 */

#ifndef	_CUB_DDL_LOG_H_
#define	_CUB_DDL_LOG_H_

#ident "$Id$"

#include "cas_handle.h"

#define DDL_LOG_RUN_EXECUTE_FUNC              (1)
#define DDL_LOG_RUN_EXECUTE_BATCH_FUNC        (2)

#define CUB_DDL_LOG_MSG_AUTO_COMMIT       "auto_commit"
#define CUB_DDL_LOG_MSG_AUTO_ROLLBACK     "auto_rollback"

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
  APP_NAME_CAS,
  APP_NAME_CSQL,
  APP_NAME_LOADDB
} T_APP_NAME;

extern void cub_ddl_log_init ();
extern void cub_ddl_log_free (bool all_free);
extern void cub_ddl_log_destroy ();
extern void cub_ddl_log_set_app_name (T_APP_NAME app_name);
extern void cub_ddl_log_set_db_name (const char *db_name);
extern void cub_ddl_log_set_user_name (const char *user_name);
extern void cub_ddl_log_set_ip (const char *ip_addr);
extern void cub_ddl_log_set_pid (const int pid);
extern void cub_ddl_log_set_br_name (const char *br_name);
extern void cub_ddl_log_set_br_index (const int index);
extern void cub_ddl_log_set_sql_text (char *sql_text, int len);
extern void cub_ddl_log_set_stmt_type (int stmt_type);
extern void cub_ddl_log_set_loaddb_file_type (T_LOADDB_FILE_TYPE file_type);
extern void cub_ddl_log_set_file_name (const char *file_name);
extern void cub_ddl_log_set_file_line (int file_line);
extern void cub_ddl_log_set_err_msg (char *msg);
extern void cub_ddl_log_set_err_code (int err_number);
extern void cub_ddl_log_set_start_time (struct timeval *time_val);
extern void cub_ddl_log_set_msg (const char *fmt, ...);
extern void cub_ddl_log_set_execute_type (char type);
extern void cub_ddl_log_set_commit_count (int count);
extern void cub_ddl_log_write ();
extern void cub_ddl_log_write_end ();
extern bool cub_ddl_log_is_ddl_type (int node_type);
extern void cub_ddl_log_set_commit_mode (bool mode);
extern void cub_ddl_log_write_tran_str (const char *fmt, ...);

#endif /* _CUB_DDL_LOG_H_ */
