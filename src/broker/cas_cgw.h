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
#include <math.h>

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

/* 
* If SIZEOF_LONG_INT is not defined in sqltypes.h, build including unixodbc_conf.h.
* When building including unixodbc_conf.h, "warning: "PACKAGE_STRING" is displayed.
* So I added the following code before including sqltypes.h to remove of the build warning.
*/
#if !defined (SIZEOF_LONG_INT)
#define SIZEOF_LONG_INT 8
#endif

#include <sqltypes.h>
#include <sql.h>
#include <sqlext.h>

#include "cas_handle.h"
#include "cas_net_buf.h"
#include "cas_execute.h"
#include "dbtype.h"
#include "tz_support.h"

#define COL_NAME_LEN               (255)
#define DEFAULT_VALUE_LEN          (255)
#define ATTR_NAME_LEN              (255)
#define CLASS_NAME_LEN             (255)

#define DECIMAL_DIGIT_MAX_LEN      (20)	/* 9223372036854775807 (7FFF FFFF FFFF FFFF) */
#define MYSQL_CONNECT_URL_FORMAT    "DRIVER={%s};SERVER=%s;Port=%s;DATABASE=%s;USER=%s;PASSWORD=%s;%s"
#define ORACLE_CONNECT_URL_FORMAT    "DRIVER={%s};DBQ=%s;Server=%s/%s;Uid=%s;Pwd=%s;%s"

#define REWRITE_DELIMITER_CUBLINK        ") cublink("
#define REWRITE_DELIMITER_FROM           " FROM "
#define REWRITE_DELIMITER_WHERE          "WHERE "
#define REWRITE_DELIMITER_CUBLINK_LEN    10
#define REWRITE_DELIMITER_FROM_LEN        6
#define REWRITE_DELIMITER_WHERE_LEN       6
#define REWRITE_SELECT_FROM_LEN          17

typedef struct t_col_binder T_COL_BINDER;
struct t_col_binder
{
  void *data_buffer;		/* display buffer   */
  SQLLEN indPtr;		/* size or null     */
  SQLLEN col_data_type;		/* type of column   */
  SQLULEN col_size;
  SQLLEN col_unsigned_type;
  bool is_exist_col_data;
  struct t_col_binder *next;	/* linked list      */
};

typedef enum
{
  MINUS,
  PLUS
} SIGN;

typedef enum
{
  SUPPORTED_DBMS_ORACLE = 0,
  SUPPORTED_DBMS_MYSQL,
  NOT_SUPPORTED_DBMS
} SUPPORTED_DBMS_TYPE;

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

extern int cgw_init ();
extern void cgw_cleanup ();
extern int cgw_col_bindings (SQLHSTMT hstmt, SQLSMALLINT num_cols, T_COL_BINDER ** col_binding,
			     T_COL_BINDER ** col_binding_buff);
extern void cgw_cleanup_binder (T_COL_BINDER * first_col_binding);

extern int cgw_init_odbc_handle (void);
extern int cgw_get_handle (T_CGW_HANDLE ** cgw_handle);
extern int cgw_get_stmt_handle (SQLHDBC hdbc, SQLHSTMT * stmt);
extern int cgw_get_driver_info (SQLHDBC hdbc, SQLUSMALLINT info_type, void *driver_info, SQLSMALLINT size);

// db connection functions
extern int cgw_database_connect (SUPPORTED_DBMS_TYPE dbms_type, const char *connect_url, char *db_name, char *db_user,
				 char *db_passwd);
extern void cgw_database_disconnect (void);
extern int cgw_is_database_connected (void);

// Prepare funtions
extern int cgw_sql_prepare (SQLCHAR * sql_stmt);
extern int cgw_get_num_cols (SQLHSTMT hstmt, SQLSMALLINT * num_cols);
extern int cgw_get_col_info (SQLHSTMT hstmt, int col_num, T_ODBC_COL_INFO * col_info);

// Execute funtions
extern int cgw_set_commit_mode (SQLHDBC hdbc, bool auto_commit);
extern int cgw_execute (T_SRV_HANDLE * srv_handle);
extern int cgw_set_execute_info (T_SRV_HANDLE * srv_handle, T_NET_BUF * net_buf, int stmt_type);
extern int cgw_make_bind_value (T_CGW_HANDLE * handle, int num_bind, int argc, void **argv, ODBC_BIND_INFO ** ret_val);

// Resultset funtions
extern int cgw_cursor_close (T_SRV_HANDLE * srv_handle);
extern int cgw_row_data (SQLHSTMT hstmt);
extern int cgw_set_stmt_attr (SQLHSTMT hstmt, SQLINTEGER attr, SQLPOINTER val, SQLINTEGER len);
extern int cgw_cur_tuple (T_NET_BUF * net_buf, T_COL_BINDER * first_col_binding, int cursor_pos);
extern int cgw_copy_tuple (T_COL_BINDER * src_col_binding, T_COL_BINDER * dst_col_binding);

extern int cgw_endtran (SQLHDBC hdbc, int tran_type);
extern SUPPORTED_DBMS_TYPE cgw_is_supported_dbms (char *dbms);
extern void cgw_set_dbms_type (SUPPORTED_DBMS_TYPE dbms_type);
extern int cgw_get_dbms_type ();
extern char *cgw_rewrite_query (char *src_query, char *cublink_pos);
#endif /* _CAS_CGW_H_ */
