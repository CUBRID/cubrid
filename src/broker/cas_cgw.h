/*
 *
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
  * cas_cgw.h -
  */

#ifndef	_CAS_CGW_H_
#define	_CAS_CGW_H_

#ident "$Id$"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(WINDOWS)
#include <winsock2.h>
#include <windows.h>
#include <process.h>
#else /* WINDOWS */
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#include <dlfcn.h>
#endif /* WINDOWS */

#include <sqltypes.h>
#include <sql.h>
#include <sqlext.h>

//#include "odbc_type_def.h"
#include "cas_handle.h"
#include "cas_net_buf.h"
#include "cas_execute.h"
#include "dbtype.h"
#include "unicode_support.h"
#include "tz_support.h"


#define COL_NAME_LEN               (255)
#define DEFAULT_VALUE_LEN          (255)
#define ATTR_NAME_LEN              (255)
#define CLASS_NAME_LEN             (255)


typedef struct t_col_binder T_COL_BINDER;
struct t_col_binder
{
  void *data_buffer;		/* display buffer   */
  SQLLEN indPtr;		/* size or null     */
  SQLLEN col_data_type;		/* type of column   */
  SQLULEN col_size;
  struct t_col_binder *next;	/* linked list      */
};

typedef enum
{
  MINUS,
  PLUS
} sign;

typedef struct t_odbc_col_info T_ODBC_COL_INFO;
struct t_odbc_col_info
{
  char data_type;
  SQLLEN scale;
  SQLLEN precision;
  char charset;
  char col_name[COL_NAME_LEN + 1];
  char default_value[DEFAULT_VALUE_LEN + 1];
  int default_value_length;
  SQLLEN is_auto_increment;
  char is_unique_key;
  char is_primary_key;
  char is_reverse_index;
  char is_reverse_unique;
  char is_foreign_key;
  char is_shared;
  char attr_name[ATTR_NAME_LEN + 1];
  char class_name[CLASS_NAME_LEN + 1];
  SQLLEN is_not_null;
};

typedef union odbc_bind_info ODBC_BIND_INFO;
union odbc_bind_info
{
  char *string_val;
  SQLSMALLINT smallInt_val;
  SQLINTEGER integer_val;
  SQLREAL real_val;
  SQLDOUBLE double_val;		/* bit type of column   */
  SQLBIGINT bigint_val;		/* _int64 of column */
  char time_stemp_str_val[50];

#if (ODBCVER >= 0x0300)
  SQL_DATE_STRUCT ds_val;	/* DATE Type        */
  SQL_TIME_STRUCT ts_val;	/* TIME Type        */
  SQL_TIMESTAMP_STRUCT tss_val;	/* TIMESTAMP Type   */
  SQL_NUMERIC_STRUCT ns_val;
#endif
};

extern void test_log (char *fmt, ...);


extern int cgw_col_bindings (SQLHSTMT hstmt, SQLSMALLINT num_col, T_COL_BINDER ** col_binding);
extern void cgw_cleanup_binder (T_COL_BINDER * pFirstBinding);

extern int cgw_init_odbc_handle (void);
extern int cgw_get_handle (T_CGW_HANDLE ** cgw_handle, bool valid_handle);
extern int cgw_get_driver_info (SQLHDBC hdbc, int info_type, void *driver_info, int size);

// db connection functions
extern int cgw_database_connect (const char *dsn, const char *connect_url);
extern void cgw_database_disconnect (void);
extern int cgw_is_database_connected (void);

// Prepare funtions
extern int cgw_sql_prepare (SQLHSTMT hstmt, SQLCHAR * sql_stmt);
extern int cgw_get_num_col (SQLHSTMT hstmt, SQLSMALLINT * num_col);
extern int cgw_get_col_info (SQLHSTMT hstmt, T_NET_BUF * net_buf, int col_num, T_ODBC_COL_INFO * col_info);

// Execute funtions
extern int cgw_set_commit_mode (SQLHDBC hdbc, bool auto_commit);
extern int cgw_execute (T_SRV_HANDLE * srv_handle);
extern int cgw_set_execute_info (T_SRV_HANDLE * srv_handle, T_NET_BUF * net_buf, int stmt_type);
extern int cgw_make_bind_value (T_CGW_HANDLE * handle, int num_bind, int argc, void **argv, ODBC_BIND_INFO ** ret_val,
				T_NET_BUF * net_buf);

// Resultset funtions
extern int cgw_cursor_close (SQLHSTMT hstmt);
extern INT64 cgw_get_row_count (SQLHSTMT hstmt);
extern int cgw_handle_free (SQLHSTMT hstmt);
extern INT64 cgw_row_count (SQLHSTMT hstmt);
extern int cgw_row_data (SQLHSTMT hstmt, int cursor_pos);
extern int cgw_set_stmt_attr (SQLHSTMT hstmt, SQLINTEGER attr, SQLPOINTER val, SQLINTEGER len);
extern int cgw_cur_tuple (T_NET_BUF * net_buf, T_COL_BINDER * pFirstBinding, int cursor_pos);

extern int cgw_free_stmt (SQLHSTMT hstmt, int option);
extern int cgw_endtran (SQLHDBC hdbc, int tran_type);
#endif /* _CAS_CGW_H_ */
