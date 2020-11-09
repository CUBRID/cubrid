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

#define DDL_LOG_RUN_EXECUTE_FUNC              0x01
#define DDL_LOG_RUN_EXECUTE_BATCH_FUNC        0x02

#define LOADDB_FILE_TYPE_NONE           (0)
#define LOADDB_FILE_TYPE_INPUT          (1)
#define LOADDB_FILE_TYPE_INDEX          (2)
#define LOADDB_FILE_TYPE_TRIGGER        (3)
#define LOADDB_FILE_TYPE_OBJECT         (4)
#define LOADDB_FILE_TYPE_SCHEMA         (5)

typedef enum
{
  APP_NAME_CAS,
  APP_NAME_CSQL,
  APP_NAME_LOADDB
} T_APP_NAME;

extern void cub_ddl_log_init ();
extern void cub_ddl_log_free ();
extern void cub_ddl_log_destroy ();
extern void cub_ddl_log_app_name (T_APP_NAME app_name);
extern void cub_ddl_log_db_name (const char *db_name);
extern void cub_ddl_log_user_name (const char *user_name);
extern void cub_ddl_log_ip (const char *ip_addr);
extern void cub_ddl_log_pid (const int pid);
extern void cub_ddl_log_br_name (const char *br_name);
extern void cub_ddl_log_br_index (const int index);
extern void cub_ddl_log_sql_text (char *sql_text, int len);
extern void cub_ddl_log_stmt_type (int stmt_type);
extern void cub_ddl_log_loaddb_file_type (char file_type);
extern void cub_ddl_log_file_name (const char *file_name);
extern void cub_ddl_log_file_line (int file_line);
extern void cub_ddl_log_err_msg (char *msg);
extern void cub_ddl_log_err_code (int err_number);
extern void cub_ddl_log_start_time (struct timeval *time_val);
extern void cub_ddl_log_msg (const char *fmt, ...);
extern void cub_ddl_log_execute_type (char type);
extern void cub_ddl_log_commit_count (int count);
extern void cub_ddl_log_execute_result (T_SRV_HANDLE * srv_handle);
extern void cub_ddl_log_write ();
extern void cub_ddl_log_write_end ();
extern int cub_is_ddl_type (int node_type);

#endif /* _CUB_DDL_LOG_H_ */
