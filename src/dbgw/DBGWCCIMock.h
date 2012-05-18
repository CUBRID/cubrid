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
#ifndef DBGWCCIMOCK_H_
#define DBGWCCIMOCK_H_

namespace dbgw
{

  typedef enum
  {
    CCI_U_TYPE_FIRST = 0,
    CCI_U_TYPE_UNKNOWN = 0,
    CCI_U_TYPE_NULL = 0,

    CCI_U_TYPE_CHAR = 1,
    CCI_U_TYPE_STRING = 2,
    CCI_U_TYPE_NCHAR = 3,
    CCI_U_TYPE_VARNCHAR = 4,
    CCI_U_TYPE_BIT = 5,
    CCI_U_TYPE_VARBIT = 6,
    CCI_U_TYPE_NUMERIC = 7,
    CCI_U_TYPE_INT = 8,
    CCI_U_TYPE_SHORT = 9,
    CCI_U_TYPE_MONETARY = 10,
    CCI_U_TYPE_FLOAT = 11,
    CCI_U_TYPE_DOUBLE = 12,
    CCI_U_TYPE_DATE = 13,
    CCI_U_TYPE_TIME = 14,
    CCI_U_TYPE_TIMESTAMP = 15,
    CCI_U_TYPE_SET = 16,
    CCI_U_TYPE_MULTISET = 17,
    CCI_U_TYPE_SEQUENCE = 18,
    CCI_U_TYPE_OBJECT = 19,
    CCI_U_TYPE_RESULTSET = 20,
    CCI_U_TYPE_BIGINT = 21,
    CCI_U_TYPE_DATETIME = 22,
    CCI_U_TYPE_BLOB = 23,
    CCI_U_TYPE_CLOB = 24,

    CCI_U_TYPE_LAST = CCI_U_TYPE_CLOB
  } T_CCI_U_TYPE;

  typedef void *T_CCI_SET;

  typedef enum
  {
    CCI_A_TYPE_FIRST = 1,
    CCI_A_TYPE_STR = 1,
    CCI_A_TYPE_INT,
    CCI_A_TYPE_FLOAT,
    CCI_A_TYPE_DOUBLE,
    CCI_A_TYPE_BIT,
    CCI_A_TYPE_DATE,
    CCI_A_TYPE_SET,
    CCI_A_TYPE_BIGINT,
    CCI_A_TYPE_BLOB,
    CCI_A_TYPE_CLOB,
    CCI_A_TYPE_LAST = CCI_A_TYPE_CLOB,

    CCI_A_TYTP_LAST = CCI_A_TYPE_LAST
  } T_CCI_A_TYPE;

  typedef enum
  {
    CCI_ER_NO_ERROR = 0,
    CCI_ER_DBMS = -1,
    CCI_ER_CON_HANDLE = -2,
    CCI_ER_NO_MORE_MEMORY = -3,
    CCI_ER_COMMUNICATION = -4,
    CCI_ER_NO_MORE_DATA = -5,
    CCI_ER_TRAN_TYPE = -6,
    CCI_ER_STRING_PARAM = -7,
    CCI_ER_TYPE_CONVERSION = -8,
    CCI_ER_BIND_INDEX = -9,
    CCI_ER_ATYPE = -10,
    CCI_ER_NOT_BIND = -11,
    CCI_ER_PARAM_NAME = -12,
    CCI_ER_COLUMN_INDEX = -13,
    CCI_ER_SCHEMA_TYPE = -14,
    CCI_ER_FILE = -15,
    CCI_ER_CONNECT = -16,

    CCI_ER_ALLOC_CON_HANDLE = -17,
    CCI_ER_REQ_HANDLE = -18,
    CCI_ER_INVALID_CURSOR_POS = -19,
    CCI_ER_OBJECT = -20,
    CCI_ER_CAS = -21,
    CCI_ER_HOSTNAME = -22,
    CCI_ER_OID_CMD = -23,

    CCI_ER_BIND_ARRAY_SIZE = -24,
    CCI_ER_ISOLATION_LEVEL = -25,

    CCI_ER_SET_INDEX = -26,
    CCI_ER_DELETED_TUPLE = -27,

    CCI_ER_SAVEPOINT_CMD = -28,
    CCI_ER_THREAD_RUNNING = -29,
    CCI_ER_INVALID_URL = -30,
    CCI_ER_INVALID_LOB_READ_POS = -31,
    CCI_ER_INVALID_LOB_HANDLE = -32,

    CCI_ER_NO_PROPERTY = -33,
    CCI_ER_PROPERTY_TYPE = -34,
    CCI_ER_INVALID_DATASOURCE = -35,
    CCI_ER_DATASOURCE_TIMEOUT = -36,
    CCI_ER_DATASOURCE_TIMEDWAIT = -37,

    CCI_ER_LOGIN_TIMEOUT = -38,
    CCI_ER_QUERY_TIMEOUT = -39,

    CCI_ER_NOT_IMPLEMENTED = -99
  } T_CCI_ERROR_CODE;

  typedef struct
  {
    int err_code;
    char err_msg[1024];
  } T_CCI_ERROR;

  typedef enum
  {
    CCI_AUTOCOMMIT_FALSE = 0, CCI_AUTOCOMMIT_TRUE
  } CCI_AUTOCOMMIT_MODE;

  typedef enum
  {
    CCI_CURSOR_FIRST = 0, CCI_CURSOR_CURRENT = 1, CCI_CURSOR_LAST = 2
  } T_CCI_CURSOR_POS;

#define CCI_GET_RESULT_INFO_NAME(RES_INFO, INDEX)	\
		(((T_CCI_COL_INFO*) (RES_INFO))[(INDEX) - 1].col_name)
#define CCI_GET_RESULT_INFO_TYPE(RES_INFO, INDEX)	\
		(((T_CCI_COL_INFO*) (RES_INFO))[(INDEX) - 1].type)

  typedef struct
  {
    T_CCI_U_TYPE type;
    char is_non_null;
    short scale;
    int precision;
    char *col_name;
    char *real_attr;
    char *class_name;
    char *default_value;
    char is_auto_increment;
    char is_unique_key;
    char is_primary_key;
    char is_foreign_key;
    char is_reverse_index;
    char is_reverse_unique;
    char is_shared;
  } T_CCI_COL_INFO;

#define T_CCI_SQLX_CMD T_CCI_CUBRID_STMT

#define CCI_TRAN_COMMIT			1
#define CCI_TRAN_ROLLBACK		2
  typedef int T_CCI_CUBRID_STMT;

  typedef enum
  {
    CCI_MOCK_STATUS_CONNECT_FAIL = 0,
    CCI_MOCK_STATUS_PREPARE_FAIL,
    CCI_MOCK_STATUS_EXECUTE_FAIL,
    CCI_MOCK_STATUS_RESULT_FAIL,
    CCI_MOCK_STATUS_DISCONNECT_FAIL
  } CCI_MOCK_STATUS;

  extern int changeMockDatabaseStatus(CCI_MOCK_STATUS status);
  extern int cci_connect_with_url(char *url, char *user, char *password);
  extern int cci_disconnect(int con_h_id, T_CCI_ERROR *err_buf);
  extern int
  cci_set_autocommit(int con_h_id, CCI_AUTOCOMMIT_MODE autocommit_mode);
  extern int cci_end_tran(int con_h_id, char type, T_CCI_ERROR *err_buf);
  extern int cci_close_req_handle(int req_h_id);
  extern int cci_prepare(int con_id, char *sql_stmt, char flag,
      T_CCI_ERROR *err_buf);
  extern int cci_bind_param(int req_h_id, int index, T_CCI_A_TYPE a_type,
      void *value, T_CCI_U_TYPE u_type, char flag);
  extern int
  cci_cursor(int req_h_id, int offset, T_CCI_CURSOR_POS origin,
      T_CCI_ERROR *err_buf);
  extern int cci_fetch(int req_h_id, T_CCI_ERROR *err_buf);
  extern int cci_get_err_msg(int err_code, char *buf, int bufsize);
  extern int cci_execute(int req_h_id, char flag, int max_col_size,
      T_CCI_ERROR *err_buf);
  extern T_CCI_COL_INFO *cci_get_result_info(int req_h_id,
      T_CCI_CUBRID_STMT *cmd_type, int *num);
  extern int cci_get_data(int req_h_id, int col_no, int a_type, void *value,
      int *indicator);

}

#endif
