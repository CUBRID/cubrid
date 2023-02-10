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
  * cas_cgw.c -
  */

#ident "$Id$"

#include "cas_cgw.h"
#include "cas.h"
#include "cas_util.h"
#include "cas_log.h"

#ifndef WINDOWS
#include <iconv.h>
#endif

#define STRING_MAX_SIZE         (16*1024*1024)
#define LOGIN_TIME_OUT          5
#define NUM_OF_DIGITS(NUMBER)   (int)log10(NUMBER) + 1
#define DISCONNECTED_STATE      -1
#define CONNECTED_STATE         0

#define ODBC_SQLSUCCESS(rc) ((rc == SQL_SUCCESS) || (rc == SQL_SUCCESS_WITH_INFO) )
#define SQL_CHK_ERR(h, ht, x)   {   RETCODE rc = x;\
                                if (rc != SQL_SUCCESS) \
                                { \
                                    cgw_error_msg (h, ht, rc); \
                                    goto ODBC_ERROR;  \
                                } \
                            }

#define UNICODE_CODE_PAGE       "UCS-2"
#define UTF8_CODE_PAGE          "UTF-8"
#define CONV_STRING_BUF_SIZE    (STRING_MAX_SIZE)

#if defined (WINDOWS)
#define CONV_M_TO_W(M, W, LEN)	MultiByteToWideChar(CP_ACP, 0, M, -1, W, LEN)
#else
#define CONV_M_TO_W(M, W, LEN)      mbstowcs(W, M, LEN)
#endif
#define CONV_WCS_TO_SQLWCS(string, len) cgw_wchar_to_sqlwchar((string), (len))

typedef struct t_supported_dbms T_SUPPORTED_DBMS;
struct t_supported_dbms
{
  char *dbms_name;
  SUPPORTED_DBMS_TYPE dbms_type;
};

static INTL_CODESET client_charset = INTL_CODESET_UTF8;
static char conv_out_string[CONV_STRING_BUF_SIZE + 1];
static T_SUPPORTED_DBMS supported_dbms_list[] =
  { {"oracle", SUPPORTED_DBMS_ORACLE}, {"mysql", SUPPORTED_DBMS_MYSQL}, {"mariadb", SUPPORTED_DBMS_MARIADB} };

static int supported_dbms_max_num = sizeof (supported_dbms_list) / sizeof (T_SUPPORTED_DBMS);
static SUPPORTED_DBMS_TYPE curr_dbms_type = NOT_SUPPORTED_DBMS;

T_CGW_HANDLE *local_odbc_handle = NULL;

static char connected_db_name[SRV_CON_DBNAME_SIZE] = { 0, };
static char connected_db_user[SRV_CON_DBUSER_SIZE] = { 0, };
static char connected_db_passwd[SRV_CON_DBPASSWD_SIZE] = { 0, };

static void cgw_error_msg (SQLHANDLE hHandle, SQLSMALLINT hType, RETCODE retcode);
static int numeric_string_adjust (SQL_NUMERIC_STRUCT * numeric, char *string);
static int hex_to_numeric_val (SQL_NUMERIC_STRUCT * numeric, char *hexstr);
static int hex_to_char (char c, unsigned char *result);
static int cgw_get_stmt_Info (T_SRV_HANDLE * srv_handle, SQLHSTMT hstmt, T_NET_BUF * net_buf, int stmt_type);
static int cgw_set_bindparam (T_CGW_HANDLE * handle, int bind_num, void *net_type, void *net_value,
			      ODBC_BIND_INFO * value_list);
static char cgw_odbc_type_to_cci_u_type (SQLLEN odbc_type, SQLLEN is_unsigned_type);
static char cgw_odbc_type_to_charset (SQLLEN odbc_type, SQLLEN is_unsigned_type);
static void cgw_cleanup_handle (T_CGW_HANDLE * handle);
static void cgw_set_charset (char charset);
static char cgw_get_charset (void);
static void cgw_link_server_info (SQLHDBC hdbc);
static bool cgw_is_support_datatype (SQLSMALLINT data_type, SQLLEN type_size);
static int cgw_count_number_of_digits (int num_bits);

static SQLSMALLINT get_c_type (SQLSMALLINT s_type, SQLLEN is_unsigned_type);
static SQLULEN get_datatype_size (SQLSMALLINT s_type, SQLULEN chars, SQLLEN precision, SQLLEN scale);
static char *cgw_datatype_to_string (SQLLEN type);
static char *cgw_utype_to_string (int type);
static void cgw_free_string_array (char **array);
static char **cgw_split_string (const char *str, const char *delim, int *num);
static int cgw_unicode_to_utf8 (wchar_t * in_src, int in_size, char **out_target, int *out_length);
static int cgw_conv_mtow (wchar_t * destStr, char *sourStr);
static int cgw_uint32_to_uni16 (uint32_t i, uint16_t * u);
static SQLWCHAR *cgw_wchar_to_sqlwchar (wchar_t * src, size_t len);


int
cgw_init ()
{
  SQLRETURN err_code;
  err_code = cgw_init_odbc_handle ();
  if (err_code < 0)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INTERFACE_NO_MORE_MEMORY, 0);
      goto ODBC_ERROR;
    }

  if (SQLAllocHandle (SQL_HANDLE_ENV, SQL_NULL_HANDLE, &local_odbc_handle->henv) == SQL_ERROR)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CGW_NOT_ALLOCATE_ENV_HANDLE, 0);
      goto ODBC_ERROR;
    }

  SQL_CHK_ERR (local_odbc_handle->henv,
	       SQL_HANDLE_ENV,
	       err_code = SQLSetEnvAttr (local_odbc_handle->henv, SQL_ATTR_ODBC_VERSION, (SQLPOINTER) SQL_OV_ODBC3, 0));

  return NO_ERROR;
ODBC_ERROR:
  return ER_FAILED;
}

void
cgw_cleanup ()
{
  cgw_cleanup_handle (local_odbc_handle);
}

int
cgw_get_handle (T_CGW_HANDLE ** cgw_handle)
{
  if (local_odbc_handle == NULL || local_odbc_handle->henv == NULL || local_odbc_handle->hdbc == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CGW_INVALID_HANDLE, 0);
      return ER_CGW_INVALID_HANDLE;
    }
  else
    {
      *cgw_handle = local_odbc_handle;
    }

  return NO_ERROR;
}

int
cgw_database_connect (SUPPORTED_DBMS_TYPE dbms_type, const char *connect_url, char *db_name, char *db_user,
		      char *db_passwd)
{
  SQLRETURN err_code;
  wchar_t wcs_url[(CGW_LINK_URL_MAX_LEN + 1) * sizeof (wchar_t)] = { 0, };
  SQLWCHAR *wconn_url = NULL;
  SQLWCHAR out_connect_str[(CGW_LINK_URL_MAX_LEN + 1) * sizeof (SQLWCHAR)];
  SQLSMALLINT out_connect_str_len;
  bool is_conneted = false;

  if (cgw_is_database_connected () == CONNECTED_STATE)
    {
      if (strcmp (db_name, connected_db_name) == 0 && strcmp (db_user, connected_db_user) == 0
	  && strcmp (db_passwd, connected_db_passwd) == 0)
	{
	  return NO_ERROR;
	}
      cgw_database_disconnect ();
    }

  SQL_CHK_ERR (local_odbc_handle->henv,
	       SQL_HANDLE_ENV,
	       err_code = SQLAllocHandle (SQL_HANDLE_DBC, local_odbc_handle->henv, &local_odbc_handle->hdbc));

  if (dbms_type == SUPPORTED_DBMS_MYSQL || dbms_type == SUPPORTED_DBMS_MARIADB)
    {
      SQL_CHK_ERR (local_odbc_handle->hdbc,
		   SQL_HANDLE_ENV,
		   err_code =
		   SQLSetConnectAttrW (local_odbc_handle->hdbc, SQL_ATTR_LOGIN_TIMEOUT, (SQLPOINTER) LOGIN_TIME_OUT,
				       0));
    }

  if (connect_url != NULL && strlen (connect_url) > 0)
    {
      err_code = cgw_conv_mtow ((wchar_t *) wcs_url, (char *) connect_url);
      if (err_code < 0)
	{
	  goto ODBC_ERROR;
	}

      wconn_url = CONV_WCS_TO_SQLWCS (wcs_url, wcslen (wcs_url));

      if (wconn_url == NULL)
	{
	  goto ODBC_ERROR;
	}

      SQL_CHK_ERR (local_odbc_handle->hdbc,
		   SQL_HANDLE_DBC,
		   err_code = SQLDriverConnectW (local_odbc_handle->hdbc,
						 NULL,
						 wconn_url,
						 SQL_NTS,
						 (SQLWCHAR *) out_connect_str,
						 (SQLSMALLINT) sizeof (out_connect_str),
						 &out_connect_str_len, SQL_DRIVER_NOPROMPT));

      FREE_MEM (wconn_url);
      is_conneted = true;
    }
  else
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CGW_NOT_EXIST_LINK_NAME, 0);
      goto ODBC_ERROR;
    }

  cgw_link_server_info (local_odbc_handle->hdbc);

  SQL_CHK_ERR (local_odbc_handle->hdbc,
	       SQL_HANDLE_DBC,
	       err_code = SQLAllocHandle (SQL_HANDLE_STMT, local_odbc_handle->hdbc, &local_odbc_handle->hstmt));

  strncpy (connected_db_name, db_name, sizeof (connected_db_name) - 1);
  strncpy (connected_db_user, db_user, sizeof (connected_db_user) - 1);
  strncpy (connected_db_passwd, db_passwd, sizeof (connected_db_passwd) - 1);

  return NO_ERROR;

ODBC_ERROR:
  FREE_MEM (wconn_url);

  if (local_odbc_handle->hdbc)
    {
      if (is_conneted)
	{
	  SQLDisconnect (local_odbc_handle->hdbc);
	}
      SQLFreeHandle (SQL_HANDLE_DBC, local_odbc_handle->hdbc);
      local_odbc_handle->hdbc = NULL;
    }
  return ER_FAILED;
}

int
cgw_execute (T_SRV_HANDLE * srv_handle)
{
  SQLRETURN err_code;

  if (srv_handle->num_markers > 0)
    {
      if (srv_handle->cgw_handle->hdbc == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CGW_INVALID_DBC_HANDLE, 0);
	  goto ODBC_ERROR;
	}

      if (srv_handle->cgw_handle->hstmt == NULL)
	{
	  SQL_CHK_ERR (srv_handle->cgw_handle->hdbc,
		       SQL_HANDLE_DBC, err_code =
		       SQLAllocHandle (SQL_HANDLE_STMT, local_odbc_handle->hdbc, &srv_handle->cgw_handle->hstmt));
	}
    }

  SQL_CHK_ERR (srv_handle->cgw_handle->hstmt, SQL_HANDLE_STMT, err_code = SQLExecute (srv_handle->cgw_handle->hstmt));

  srv_handle->is_cursor_open = true;

  return NO_ERROR;

ODBC_ERROR:
  if (srv_handle->cgw_handle->hstmt)
    {
      SQLFreeHandle (SQL_HANDLE_STMT, srv_handle->cgw_handle->hstmt);
      srv_handle->cgw_handle->hstmt = NULL;
    }

  return ER_FAILED;
}

int
cgw_row_data (SQLHSTMT hstmt)
{
  SQLRETURN err_code;
  int no_data = 0;

  if (hstmt == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CGW_INVALID_STMT_HANDLE, 0);
      goto ODBC_ERROR;
    }

  err_code = SQLFetchScroll (hstmt, SQL_FETCH_NEXT, 0);

  if (err_code < 0)
    {
      cgw_error_msg (hstmt, SQL_HANDLE_STMT, err_code);
      goto ODBC_ERROR;
    }

  if (err_code == SQL_NO_DATA_FOUND)
    {
      no_data = SQL_NO_DATA_FOUND;
    }

  return no_data;

ODBC_ERROR:
  return ER_FAILED;
}

int
cgw_cursor_close (T_SRV_HANDLE * srv_handle)
{
  SQLRETURN err_code = 0;

  if (srv_handle->cgw_handle->hstmt == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CGW_INVALID_STMT_HANDLE, 0);
      goto ODBC_ERROR;
    }

  SQL_CHK_ERR (srv_handle->cgw_handle->hstmt, SQL_HANDLE_STMT, err_code =
	       SQLFreeStmt (srv_handle->cgw_handle->hstmt, SQL_CLOSE));

  srv_handle->is_cursor_open = false;

  return err_code;

ODBC_ERROR:
  return ER_FAILED;
}

int
cgw_copy_tuple (T_COL_BINDER * src_col_binding, T_COL_BINDER * dst_col_binding)
{
  SQLRETURN err_code = 0;
  T_COL_BINDER *src_binder;
  T_COL_BINDER *dst_binder;

  if (src_col_binding == NULL || dst_col_binding == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CGW_NULL_COL_BINDER, 0);
      return ER_FAILED;
    }

  dst_binder = dst_col_binding;
  for (src_binder = src_col_binding; src_binder; src_binder = src_binder->next)
    {
      memcpy (dst_binder->data_buffer, src_binder->data_buffer, src_binder->col_size);
      dst_binder->indPtr = src_binder->indPtr;
      dst_binder->is_exist_col_data = true;
      dst_binder = dst_binder->next;
    }

  return err_code;
}

int
cgw_cur_tuple (T_NET_BUF * net_buf, T_COL_BINDER * first_col_binding, int cursor_pos)
{
  DB_BIGINT bigint = 0;
  T_OBJECT tuple_obj;
  T_COL_BINDER *this_col_binding;
  int str_len;
  SQL_DATE_STRUCT *date;
  SQL_TIME_STRUCT *time;
  SQL_TIMESTAMP_STRUCT *timestamp;
  char *conv_string;
  int conv_string_len = 0;
  int conv_ret = 0;

  net_buf_cp_int (net_buf, cursor_pos, NULL);

  memset ((char *) &tuple_obj, 0, sizeof (T_OBJECT));
  net_buf_cp_object (net_buf, &tuple_obj);

  for (this_col_binding = first_col_binding; this_col_binding; this_col_binding = this_col_binding->next)
    {
      if (this_col_binding->indPtr == SQL_NULL_DATA)
	{
	  net_buf_cp_int (net_buf, -1, NULL);
	}
      else
	{
	  str_len = this_col_binding->indPtr;

	  switch (this_col_binding->col_data_type)
	    {
	    case SQL_UNKNOWN_TYPE:
	      net_buf_cp_int (net_buf, -1, NULL);
	      break;
	    case SQL_CHAR:
	    case SQL_VARCHAR:
	    case SQL_LONGVARCHAR:
	    case SQL_WCHAR:
	    case SQL_WVARCHAR:
	    case SQL_WLONGVARCHAR:
	    case SQL_NUMERIC:
	    case SQL_DECIMAL:
	      conv_ret =
		cgw_unicode_to_utf8 ((wchar_t *) this_col_binding->data_buffer, str_len, &conv_string,
				     &conv_string_len);
	      if (conv_ret < 0)
		{
		  net_buf_cp_int (net_buf, -1, NULL);
		  continue;
		}

	      net_buf_cp_int (net_buf, (int) conv_string_len, NULL);
	      net_buf_cp_str (net_buf, (char *) conv_string, conv_string_len - 1);
	      net_buf_cp_byte (net_buf, 0);
	      break;
	    case SQL_INTEGER:
	      if (this_col_binding->col_unsigned_type)
		{
		  conv_ret =
		    cgw_unicode_to_utf8 ((wchar_t *) this_col_binding->data_buffer, str_len, &conv_string,
					 &conv_string_len);
		  if (conv_ret < 0)
		    {
		      net_buf_cp_int (net_buf, -1, NULL);
		      continue;
		    }

		  net_buf_cp_int (net_buf, (int) conv_string_len, NULL);
		  net_buf_cp_str (net_buf, (char *) conv_string, conv_string_len - 1);
		  net_buf_cp_byte (net_buf, 0);
		}
	      else
		{
		  net_buf_cp_int (net_buf, NET_SIZE_INT, NULL);
		  net_buf_cp_int (net_buf, *((int *) this_col_binding->data_buffer), NULL);
		}
	      break;
	    case SQL_SMALLINT:
	      if (this_col_binding->col_unsigned_type)
		{
		  conv_ret =
		    cgw_unicode_to_utf8 ((wchar_t *) this_col_binding->data_buffer, str_len, &conv_string,
					 &conv_string_len);
		  if (conv_ret < 0)
		    {
		      net_buf_cp_int (net_buf, -1, NULL);
		      continue;
		    }

		  net_buf_cp_int (net_buf, (int) conv_string_len, NULL);
		  net_buf_cp_str (net_buf, (char *) conv_string, conv_string_len - 1);
		  net_buf_cp_byte (net_buf, 0);
		}
	      else
		{
		  net_buf_cp_int (net_buf, NET_SIZE_SHORT, NULL);
		  net_buf_cp_short (net_buf, *((short *) this_col_binding->data_buffer));
		}
	      break;
	    case SQL_TINYINT:
	      if (this_col_binding->col_unsigned_type)
		{
		  conv_ret =
		    cgw_unicode_to_utf8 ((wchar_t *) this_col_binding->data_buffer, str_len, &conv_string,
					 &conv_string_len);
		  if (conv_ret < 0)
		    {
		      net_buf_cp_int (net_buf, -1, NULL);
		      continue;
		    }

		  net_buf_cp_int (net_buf, (int) conv_string_len, NULL);
		  net_buf_cp_str (net_buf, (char *) conv_string, conv_string_len - 1);
		  net_buf_cp_byte (net_buf, 0);
		}
	      else
		{
		  net_buf_cp_int (net_buf, NET_SIZE_SHORT, NULL);
		  net_buf_cp_short (net_buf, *((char *) this_col_binding->data_buffer));
		}
	      break;
	    case SQL_FLOAT:
	    case SQL_REAL:
	      net_buf_cp_int (net_buf, NET_SIZE_FLOAT, NULL);
	      net_buf_cp_float (net_buf, *((float *) this_col_binding->data_buffer));
	      break;
	    case SQL_DOUBLE:
	      net_buf_cp_int (net_buf, NET_SIZE_DOUBLE, NULL);
	      net_buf_cp_double (net_buf, *((double *) this_col_binding->data_buffer));
	      break;
	    case SQL_BIGINT:
	      if (this_col_binding->col_unsigned_type)
		{
		  conv_ret =
		    cgw_unicode_to_utf8 ((wchar_t *) this_col_binding->data_buffer, str_len, &conv_string,
					 &conv_string_len);
		  if (conv_ret < 0)
		    {
		      net_buf_cp_int (net_buf, -1, NULL);
		      continue;
		    }

		  net_buf_cp_int (net_buf, conv_string_len, NULL);
		  net_buf_cp_str (net_buf, (char *) conv_string, conv_string_len - 1);
		  net_buf_cp_byte (net_buf, 0);
		}
	      else
		{
		  net_buf_cp_int (net_buf, NET_SIZE_BIGINT, NULL);
		  bigint = *((DB_BIGINT *) (this_col_binding->data_buffer));
		  net_buf_cp_bigint (net_buf, bigint, NULL);
		}
	      break;
#if (ODBCVER >= 0x0300)
	    case SQL_DATETIME:
	      timestamp = (SQL_TIMESTAMP_STRUCT *) (this_col_binding->data_buffer);
	      net_buf_cp_int (net_buf, NET_SIZE_DATETIME, NULL);
	      net_buf_cp_short (net_buf, (short) timestamp->year);
	      net_buf_cp_short (net_buf, (short) timestamp->month);
	      net_buf_cp_short (net_buf, (short) timestamp->day);
	      net_buf_cp_short (net_buf, (short) timestamp->hour);
	      net_buf_cp_short (net_buf, (short) timestamp->minute);
	      net_buf_cp_short (net_buf, (short) timestamp->second);
	      if (timestamp->fraction > 0)
		{
		  net_buf_cp_short (net_buf, (short) (timestamp->fraction / 1000000));
		}
	      else
		{
		  net_buf_cp_short (net_buf, 0);
		}
	      break;
#else
	    case SQL_DATE:
	      {
		date = (SQL_DATE_STRUCT *) (this_col_binding->data_buffer);
		net_buf_cp_int (net_buf, NET_SIZE_DATE, NULL);
		net_buf_cp_short (net_buf, (short) date->year);
		net_buf_cp_short (net_buf, (short) date->month);
		net_buf_cp_short (net_buf, (short) date->day);
		break;
	      }
#endif

#if (ODBCVER >= 0x0300)
	    case SQL_TYPE_DATE:
	      {
		date = (SQL_DATE_STRUCT *) (this_col_binding->data_buffer);
		net_buf_cp_int (net_buf, NET_SIZE_DATE, NULL);
		net_buf_cp_short (net_buf, (short) date->year);
		net_buf_cp_short (net_buf, (short) date->month);
		net_buf_cp_short (net_buf, (short) date->day);
		break;
	      }
	    case SQL_TYPE_TIME:
	      {
		time = (SQL_TIME_STRUCT *) (this_col_binding->data_buffer);
		net_buf_cp_int (net_buf, NET_SIZE_TIME, NULL);
		net_buf_cp_short (net_buf, (short) time->hour);
		net_buf_cp_short (net_buf, (short) time->minute);
		net_buf_cp_short (net_buf, (short) time->second);
		break;
	      }
	    case SQL_TYPE_TIMESTAMP:
	      {
		timestamp = (SQL_TIMESTAMP_STRUCT *) (this_col_binding->data_buffer);
		net_buf_cp_int (net_buf, NET_SIZE_DATETIME, NULL);
		net_buf_cp_short (net_buf, (short) timestamp->year);
		net_buf_cp_short (net_buf, (short) timestamp->month);
		net_buf_cp_short (net_buf, (short) timestamp->day);
		net_buf_cp_short (net_buf, (short) timestamp->hour);
		net_buf_cp_short (net_buf, (short) timestamp->minute);
		net_buf_cp_short (net_buf, (short) timestamp->second);
		if (timestamp->fraction > 0)
		  {
		    net_buf_cp_short (net_buf, (short) (timestamp->fraction / 1000000));
		  }
		else
		  {
		    net_buf_cp_short (net_buf, 0);
		  }
		break;
	      }
#if (ODBCVER >= 0x0300)
	    case SQL_INTERVAL:
	      net_buf_cp_int (net_buf, -1, NULL);
	      break;
#else
	    case SQL_TIME:
	      {
		time = (SQL_TIME_STRUCT *) (this_col_binding->data_buffer);
		net_buf_cp_int (net_buf, NET_SIZE_TIME, NULL);
		net_buf_cp_short (net_buf, (short) time->hour);
		net_buf_cp_short (net_buf, (short) time->minute);
		net_buf_cp_short (net_buf, (short) time->second);
		break;
	      }
#endif /* ODBCVER >= 0x0300 */
	    case SQL_TIMESTAMP:
	      {
		timestamp = (SQL_TIMESTAMP_STRUCT *) (this_col_binding->data_buffer);
		net_buf_cp_int (net_buf, NET_SIZE_DATETIME, NULL);
		net_buf_cp_short (net_buf, (short) timestamp->year);
		net_buf_cp_short (net_buf, (short) timestamp->month);
		net_buf_cp_short (net_buf, (short) timestamp->day);
		net_buf_cp_short (net_buf, (short) timestamp->hour);
		net_buf_cp_short (net_buf, (short) timestamp->minute);
		net_buf_cp_short (net_buf, (short) timestamp->second);
		if (timestamp->fraction > 0)
		  {
		    net_buf_cp_short (net_buf, (short) (timestamp->fraction / 1000000));
		  }
		else
		  {
		    net_buf_cp_short (net_buf, 0);
		  }

		break;
	      }
	    case SQL_BIT:
	    case SQL_BINARY:
	    case SQL_VARBINARY:
	    case SQL_LONGVARBINARY:
#if (ODBCVER >= 0x0350)
	    case SQL_GUID:
	      net_buf_cp_int (net_buf, -1, NULL);
	      break;
#endif
	    default:
	      net_buf_cp_int (net_buf, -1, NULL);
	      break;
#endif
	    }
	}
    }

  return NO_ERROR;
}

int
cgw_get_col_info (SQLHSTMT hstmt, int col_num, T_ODBC_COL_INFO * col_info)
{
  SQLRETURN err_code;
  SQLSMALLINT col_name_length = 0;
  SQLSMALLINT class_name_length = 0;
  SQLLEN col_data_type = 0;
  SQLLEN col_unsigned_type = 0;

  memset (col_info, 0x0, sizeof (T_ODBC_COL_INFO));

  if (hstmt == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CGW_INVALID_STMT_HANDLE, 0);
      goto ODBC_ERROR;
    }

  SQL_CHK_ERR (hstmt,
	       SQL_HANDLE_STMT,
	       err_code = SQLColAttribute (hstmt,
					   col_num,
					   SQL_DESC_NAME,
					   col_info->col_name, sizeof (col_info->col_name), &col_name_length, NULL));
  SQL_CHK_ERR (hstmt,
	       SQL_HANDLE_STMT,
	       err_code = SQLColAttribute (hstmt, col_num, SQL_DESC_CONCISE_TYPE, NULL, 0, NULL, &col_data_type));

  SQL_CHK_ERR (hstmt,
	       SQL_HANDLE_STMT,
	       err_code = SQLColAttribute (hstmt, col_num, SQL_DESC_SCALE, NULL, 0, NULL, &col_info->scale));

  SQL_CHK_ERR (hstmt,
	       SQL_HANDLE_STMT,
	       err_code = SQLColAttribute (hstmt, col_num, SQL_DESC_PRECISION, NULL, 0, NULL, &col_info->precision));

  SQL_CHK_ERR (hstmt,
	       SQL_HANDLE_STMT,
	       err_code = SQLColAttribute (hstmt, col_num, SQL_DESC_UNSIGNED, NULL, 0, NULL, &col_unsigned_type));

  if (col_data_type == SQL_REAL || col_data_type == SQL_FLOAT || col_data_type == SQL_DOUBLE)
    {
      int num = cgw_count_number_of_digits (col_info->precision);
      if (num < 0)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CGW_INVALID_PRECISION_VALUE, 1, col_info->col_name);
	  goto ODBC_ERROR;
	}
      col_info->precision = num;
    }
  SQL_CHK_ERR (hstmt,
	       SQL_HANDLE_STMT,
	       err_code = SQLColAttribute (hstmt,
					   col_num,
					   SQL_DESC_NAME,
					   col_info->col_name, sizeof (col_info->col_name), &col_name_length, NULL));

  SQL_CHK_ERR (hstmt,
	       SQL_HANDLE_STMT,
	       err_code = SQLColAttribute (hstmt,
					   col_num,
					   SQL_DESC_TABLE_NAME,
					   col_info->class_name, sizeof (col_info->class_name), &class_name_length,
					   NULL));

  SQL_CHK_ERR (hstmt, SQL_HANDLE_STMT, err_code = SQLColAttribute (hstmt, col_num, SQL_DESC_NULLABLE, NULL, 0,	// Note count of bytes!
								   NULL, &col_info->is_not_null));

  SQL_CHK_ERR (hstmt,
	       SQL_HANDLE_STMT,
	       err_code = SQLColAttribute (hstmt,
					   col_num,
					   SQL_DESC_AUTO_UNIQUE_VALUE, NULL, 0, NULL, &col_info->is_auto_increment));

  col_info->data_type = cgw_odbc_type_to_cci_u_type (col_data_type, col_unsigned_type);
  col_info->charset = cgw_odbc_type_to_charset (col_data_type, col_unsigned_type);

  return err_code;

ODBC_ERROR:
  return ER_FAILED;
}

static char
cgw_odbc_type_to_cci_u_type (SQLLEN odbc_type, SQLLEN is_unsigned_type)
{
  char data_type = CCI_U_TYPE_UNKNOWN;
  switch (odbc_type)
    {
    case SQL_CHAR:
    case SQL_WCHAR:
      data_type = CCI_U_TYPE_CHAR;
      break;
    case SQL_NUMERIC:
    case SQL_DECIMAL:
      data_type = CCI_U_TYPE_NUMERIC;
      break;
    case SQL_INTEGER:
      data_type = (is_unsigned_type) ? CCI_U_TYPE_CHAR : CCI_U_TYPE_INT;
      break;
    case SQL_TINYINT:
    case SQL_SMALLINT:
      data_type = (is_unsigned_type) ? CCI_U_TYPE_CHAR : CCI_U_TYPE_SHORT;
      break;
    case SQL_FLOAT:
    case SQL_REAL:
      data_type = CCI_U_TYPE_FLOAT;
      break;
    case SQL_DOUBLE:
      data_type = CCI_U_TYPE_DOUBLE;
      break;
#if (ODBCVER >= 0x0300)
    case SQL_DATETIME:
      data_type = CCI_U_TYPE_DATETIME;
      break;
#else
    case SQL_DATE:
      data_type = CCI_U_TYPE_DATE;
      break;
#endif
#if (ODBCVER >= 0x0300)
    case SQL_TYPE_TIMESTAMP:
      data_type = CCI_U_TYPE_DATETIME;
      break;
    case SQL_TYPE_DATE:
      data_type = CCI_U_TYPE_DATE;
      break;

    case SQL_TYPE_TIME:
      data_type = CCI_U_TYPE_TIME;
      break;
#endif
    case SQL_VARCHAR:
    case SQL_LONGVARCHAR:
    case SQL_WVARCHAR:
    case SQL_WLONGVARCHAR:
      data_type = CCI_U_TYPE_STRING;
      break;
#if (ODBCVER >= 0x0300)
    case SQL_INTERVAL:
      data_type = CCI_U_TYPE_UNKNOWN;
      break;
#else /* ODBCVER >= 0x0300 */
    case SQL_TIME:
      data_type = CCI_U_TYPE_TIME;
      break;
#endif
    case SQL_TIMESTAMP:
      data_type = CCI_U_TYPE_DATETIME;
      break;
    case SQL_BIGINT:
      data_type = (is_unsigned_type) ? CCI_U_TYPE_CHAR : CCI_U_TYPE_BIGINT;
      break;
#if (ODBCVER >= 0x0350)
    case SQL_GUID:
      data_type = CCI_U_TYPE_UNKNOWN;
      break;
#endif
    case SQL_BIT:
    case SQL_BINARY:
    case SQL_VARBINARY:
    case SQL_LONGVARBINARY:
      data_type = CCI_U_TYPE_UNKNOWN;
      break;
    default:
      data_type = CCI_U_TYPE_UNKNOWN;
    }

  return data_type;
}

static char
cgw_odbc_type_to_charset (SQLLEN odbc_type, SQLLEN is_unsigned_type)
{
  char code_set = INTL_CODESET_NONE;

  switch (odbc_type)
    {
    case SQL_CHAR:
    case SQL_WCHAR:
      code_set = cgw_get_charset ();
      break;
    case SQL_NUMERIC:
    case SQL_DECIMAL:
      code_set = INTL_CODESET_ASCII;
      break;
    case SQL_INTEGER:
      code_set = (is_unsigned_type) ? cgw_get_charset () : INTL_CODESET_ASCII;
      break;
    case SQL_TINYINT:
    case SQL_SMALLINT:
      code_set = (is_unsigned_type) ? cgw_get_charset () : INTL_CODESET_ASCII;
      break;
    case SQL_FLOAT:
      code_set = INTL_CODESET_ASCII;
      break;
    case SQL_REAL:
      code_set = INTL_CODESET_ASCII;
      break;
    case SQL_DOUBLE:
      code_set = INTL_CODESET_ASCII;
      break;
#if (ODBCVER >= 0x0300)
    case SQL_DATETIME:
      code_set = INTL_CODESET_ASCII;
      break;
#else
    case SQL_DATE:
      code_set = INTL_CODESET_ASCII;
      break;
#endif
#if (ODBCVER >= 0x0300)
    case SQL_TYPE_TIMESTAMP:
      code_set = INTL_CODESET_ASCII;
      break;
    case SQL_TYPE_DATE:
      code_set = INTL_CODESET_ASCII;
      break;

    case SQL_TYPE_TIME:
      code_set = INTL_CODESET_ASCII;
      break;
#endif
    case SQL_VARCHAR:
    case SQL_LONGVARCHAR:
    case SQL_WVARCHAR:
    case SQL_WLONGVARCHAR:
      code_set = cgw_get_charset ();
      break;
#if (ODBCVER >= 0x0300)
    case SQL_INTERVAL:
      break;
#else /* ODBCVER >= 0x0300 */
    case SQL_TIME:
      code_set = INTL_CODESET_ASCII;
      break;
#endif
    case SQL_TIMESTAMP:
      code_set = INTL_CODESET_ASCII;
      break;
    case SQL_BIGINT:
      code_set = (is_unsigned_type) ? cgw_get_charset () : INTL_CODESET_ASCII;
      break;
#if (ODBCVER >= 0x0350)
    case SQL_GUID:
#endif
    case SQL_BIT:
    case SQL_BINARY:
    case SQL_VARBINARY:
    case SQL_LONGVARBINARY:
      code_set = INTL_CODESET_NONE;
      break;
    default:
      code_set = INTL_CODESET_NONE;
    }

  return code_set;
}

static void
cgw_set_charset (char charset)
{
  client_charset = (INTL_CODESET) charset;
}


static char
cgw_get_charset (void)
{
  return (char) client_charset;
}

int
cgw_set_execute_info (T_SRV_HANDLE * srv_handle, T_NET_BUF * net_buf, int stmt_type)
{
  SQLRETURN err_code;
  char cache_reusable = 0;

  net_buf_cp_int (net_buf, srv_handle->total_tuple_count, NULL);
  net_buf_cp_byte (net_buf, cache_reusable);
  net_buf_cp_int (net_buf, (int) srv_handle->num_q_result, NULL);

  for (int i = 0; i < srv_handle->num_q_result; i++)
    {
      err_code = cgw_get_stmt_Info (srv_handle, srv_handle->cgw_handle->hstmt, net_buf, stmt_type);
      if (err_code < 0)
	{
	  goto ODBC_ERROR;
	}
    }

  return NO_ERROR;

ODBC_ERROR:
  return ER_FAILED;
}

int
cgw_set_commit_mode (SQLHDBC hdbc, bool auto_commit)
{
  SQLRETURN err_code = 0;

  if (hdbc == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CGW_INVALID_STMT_HANDLE, 0);
      goto ODBC_ERROR;
    }

  if (auto_commit)
    {
      SQL_CHK_ERR (hdbc, SQL_HANDLE_DBC,
		   err_code = SQLSetConnectAttr (hdbc, SQL_ATTR_AUTOCOMMIT, (SQLPOINTER *) SQL_AUTOCOMMIT_ON, SQL_NTS));
    }
  else
    {
      SQL_CHK_ERR (hdbc, SQL_HANDLE_DBC,
		   err_code =
		   SQLSetConnectAttr (hdbc, SQL_ATTR_AUTOCOMMIT, (SQLPOINTER *) SQL_AUTOCOMMIT_OFF, SQL_NTS));
    }

  return err_code;

ODBC_ERROR:
  return ER_FAILED;
}

static int
cgw_get_stmt_Info (T_SRV_HANDLE * srv_handle, SQLHSTMT hstmt, T_NET_BUF * net_buf, int stmt_type)
{
  T_OBJECT ins_oid;
  CACHE_TIME srv_cache_time;
  char statement_type = (char) stmt_type;

  if (hstmt == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CGW_INVALID_STMT_HANDLE, 0);
      goto ODBC_ERROR;
    }

  net_buf_cp_byte (net_buf, statement_type);
  net_buf_cp_int (net_buf, srv_handle->total_tuple_count, NULL);

  memset (&ins_oid, 0, sizeof (T_OBJECT));
  net_buf_cp_object (net_buf, &ins_oid);

  CACHE_TIME_RESET (&srv_cache_time);
  net_buf_cp_int (net_buf, srv_cache_time.sec, NULL);
  net_buf_cp_int (net_buf, srv_cache_time.usec, NULL);

  return NO_ERROR;

ODBC_ERROR:
  return ER_FAILED;
}


int
cgw_col_bindings (SQLHSTMT hstmt, SQLSMALLINT num_cols, T_COL_BINDER ** col_binding, T_COL_BINDER ** col_binding_buff)
{
  SQLRETURN err_code;
  SQLSMALLINT col;
  T_COL_BINDER *this_col_binding, *last_col_binding = NULL;
  T_COL_BINDER *this_col_binding_buff, *last_col_binding_buff = NULL;
  SQLSMALLINT col_name_len;
  SQLSMALLINT col_data_type, col_decimal_digits, nullable;
  SQLULEN col_size, bind_col_size;
  SQLLEN precision, scale;
  SQLCHAR col_name[COL_NAME_LEN];
  SQLLEN col_unsigned_type = 0;

  if (hstmt == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CGW_INVALID_STMT_HANDLE, 0);
      goto ODBC_ERROR;
    }

  for (col = 1; col <= num_cols; col++)
    {
      this_col_binding = (T_COL_BINDER *) (MALLOC (sizeof (T_COL_BINDER)));
      if (!(this_col_binding))
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INTERFACE_NO_MORE_MEMORY, 0);
	  goto ODBC_ERROR;
	}

      this_col_binding_buff = (T_COL_BINDER *) (MALLOC (sizeof (T_COL_BINDER)));
      if (!(this_col_binding_buff))
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INTERFACE_NO_MORE_MEMORY, 0);
	  goto ODBC_ERROR;
	}

      memset (this_col_binding, 0x0, sizeof (T_COL_BINDER));
      memset (this_col_binding_buff, 0x0, sizeof (T_COL_BINDER));

      if (col == 1)
	{
	  *col_binding = this_col_binding;
	  *col_binding_buff = this_col_binding_buff;
	}
      else
	{
	  last_col_binding->next = this_col_binding;
	  last_col_binding_buff->next = this_col_binding_buff;
	}
      last_col_binding = this_col_binding;
      last_col_binding_buff = this_col_binding_buff;

      SQL_CHK_ERR (hstmt,
		   SQL_HANDLE_STMT,
		   err_code = SQLDescribeCol (hstmt,
					      col,
					      col_name,
					      sizeof (col_name),
					      &col_name_len, &col_data_type, &col_size, &col_decimal_digits,
					      &nullable));

      SQL_CHK_ERR (hstmt, SQL_HANDLE_STMT, SQLColAttribute (hstmt, col, SQL_DESC_SCALE, NULL, 0, NULL, &scale));

      SQL_CHK_ERR (hstmt, SQL_HANDLE_STMT, SQLColAttribute (hstmt, col, SQL_DESC_PRECISION, NULL, 0, NULL, &precision));

      SQL_CHK_ERR (hstmt,
		   SQL_HANDLE_STMT,
		   err_code = SQLColAttribute (hstmt, col, SQL_DESC_UNSIGNED, NULL, 0, NULL, &col_unsigned_type));

      this_col_binding->col_unsigned_type = col_unsigned_type;
      this_col_binding_buff->col_unsigned_type = col_unsigned_type;

      if (col_unsigned_type)
	{
	  bind_col_size = col_size + 1;
	}
      else
	{
	  bind_col_size = get_datatype_size (col_data_type, col_size, precision, scale);
	}

      if (cgw_is_support_datatype (col_data_type, bind_col_size))
	{
	  bind_col_size = bind_col_size * 2;

	  this_col_binding->col_data_type = col_data_type;
	  this_col_binding->col_size = bind_col_size;
	  this_col_binding->next = NULL;

	  this_col_binding_buff->col_data_type = col_data_type;
	  this_col_binding_buff->col_size = bind_col_size;
	  this_col_binding_buff->next = NULL;
	  this_col_binding_buff->is_exist_col_data = false;

	  this_col_binding->data_buffer = MALLOC (bind_col_size);
	  if (!(this_col_binding->data_buffer))
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INTERFACE_NO_MORE_MEMORY, 0);
	      goto ODBC_ERROR;
	    }
	  this_col_binding_buff->data_buffer = (wchar_t *) MALLOC (bind_col_size);
	  if (!(this_col_binding_buff->data_buffer))
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INTERFACE_NO_MORE_MEMORY, 0);
	      goto ODBC_ERROR;
	    }

	  SQL_CHK_ERR (hstmt,
		       SQL_HANDLE_STMT,
		       err_code = SQLBindCol (hstmt,
					      col,
					      get_c_type (col_data_type, col_unsigned_type),
					      (SQLPOINTER) this_col_binding->data_buffer, bind_col_size,
					      &this_col_binding->indPtr));
	}
      else
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CGW_NOT_SUPPORTED_TYPE, 2,
		  cgw_datatype_to_string (col_data_type), col_data_type);
	  goto ODBC_ERROR;
	}
    }

  return NO_ERROR;

ODBC_ERROR:
  return ER_FAILED;
}


void
cgw_cleanup_binder (T_COL_BINDER * first_col_binding)
{
  T_COL_BINDER *this_col_binding;

  while (first_col_binding)
    {
      this_col_binding = first_col_binding->next;
      FREE_MEM (first_col_binding->data_buffer);
      FREE_MEM (first_col_binding);
      first_col_binding = this_col_binding;
    }
}

void
cgw_error_msg (SQLHANDLE hHandle, SQLSMALLINT hType, RETCODE retcode)
{
  SQLSMALLINT iRec = 0;
  SQLINTEGER iError;
  char szMessage[SQL_MAX_MESSAGE_LENGTH + 1];
  char szState[SQL_SQLSTATE_SIZE + 1];

  if (retcode == SQL_INVALID_HANDLE)
    {
      return;
    }

  while (SQLGetDiagRec (hType,
			hHandle,
			++iRec,
			(SQLCHAR *) szState,
			&iError,
			(SQLCHAR *) szMessage,
			(SQLSMALLINT) (sizeof (szMessage) / sizeof (char)), (SQLSMALLINT *) NULL) == SQL_SUCCESS)
    {
      if (iRec == 1)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CGW_NATIVE_ODBC, 3, szState, iError, szMessage);
	}
    }
}

void
cgw_database_disconnect ()
{
  if (local_odbc_handle == NULL)
    {
      return;
    }

  if (local_odbc_handle->hstmt)
    {
      SQLFreeHandle (SQL_HANDLE_STMT, local_odbc_handle->hstmt);
      local_odbc_handle->hstmt = NULL;
    }

  if (local_odbc_handle->hdbc)
    {
      SQLDisconnect (local_odbc_handle->hdbc);
      SQLFreeHandle (SQL_HANDLE_DBC, local_odbc_handle->hdbc);
      local_odbc_handle->hdbc = NULL;
    }

  connected_db_name[0] = '\0';
  connected_db_user[0] = '\0';
  connected_db_passwd[0] = '\0';
}

static int
cgw_set_bindparam (T_CGW_HANDLE * handle, int bind_num, void *net_type, void *net_value, ODBC_BIND_INFO * value_list)
{
  char type;
  int err_code = 0;
  int data_size;
  SQLLEN indPtr = 0;
  SQLSMALLINT c_data_type;
  SQLSMALLINT sql_bind_type;

  if (handle == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CGW_INVALID_HANDLE, 0);
      return ER_CGW_INVALID_HANDLE;
    }

  if (handle->hstmt == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CGW_INVALID_STMT_HANDLE, 0);
      return ER_CGW_INVALID_STMT_HANDLE;
    }

  net_arg_get_char (type, net_type);
  net_arg_get_size (&data_size, net_value);

  if (data_size <= 0)
    {
      type = CCI_U_TYPE_NULL;
      data_size = 0;
    }

  switch (type)
    {
    case CCI_U_TYPE_CHAR:
    case CCI_U_TYPE_STRING:
      {
	char *value;
	int val_size;

	net_arg_get_str (&value, &val_size, net_value);

	c_data_type = SQL_C_CHAR;
	sql_bind_type = SQL_CHAR;

	value_list->string_val = value;

	SQL_CHK_ERR (handle->hstmt,
		     SQL_HANDLE_STMT,
		     err_code = SQLBindParameter (handle->hstmt,
						  bind_num,
						  SQL_PARAM_INPUT,
						  c_data_type,
						  sql_bind_type, val_size + 1, 0, value_list->string_val, 0, NULL));
      }
      break;
      /* Not Support Type */
    case CCI_U_TYPE_NCHAR:
    case CCI_U_TYPE_VARNCHAR:
      {
	char *value;
	int val_size;

	net_arg_get_str (&value, &val_size, net_value);

	c_data_type = SQL_C_CHAR;
	sql_bind_type = SQL_CHAR;

	value_list->string_val = value;

	SQL_CHK_ERR (handle->hstmt,
		     SQL_HANDLE_STMT,
		     err_code = SQLBindParameter (handle->hstmt,
						  bind_num,
						  SQL_PARAM_INPUT,
						  c_data_type,
						  sql_bind_type, val_size + 1, 0, value_list->string_val, 0, NULL));
      }
      break;

    case CCI_U_TYPE_NUMERIC:
      {
	char *value, *p;
	int val_size;
	size_t precision, scale;
	char num_str[64];
	char tmp[64];
	SQLHDESC hdesc = NULL;

	SQL_CHK_ERR (handle->hdbc, SQL_HANDLE_DBC, err_code = SQLAllocHandle (SQL_HANDLE_DESC, handle->hdbc, &hdesc));

	memset (&value_list->ns_val, 0x00, sizeof (SQL_NUMERIC_STRUCT));

	net_arg_get_str (&value, &val_size, net_value);
	if (value != NULL)
	  {
	    strcpy (tmp, value);
	  }
	tmp[val_size] = '\0';
	ut_trim (tmp);
	precision = strlen (tmp);
	p = strchr (tmp, '.');
	if (p == NULL)
	  {
	    scale = 0;
	  }
	else
	  {
	    scale = strlen (p + 1);
	    precision--;
	  }

	if (tmp[0] == '-')
	  {
	    precision--;
	    value_list->ns_val.sign = MINUS;
	  }
	else
	  {
	    value_list->ns_val.sign = PLUS;
	  }

	value_list->ns_val.precision = precision;
	value_list->ns_val.scale = scale;

	if (value_list->ns_val.sign == MINUS)
	  {
	    strcpy (num_str, &tmp[1]);
	  }
	else
	  {
	    strcpy (num_str, &tmp[0]);
	  }

	err_code = numeric_string_adjust (&value_list->ns_val, num_str);
	if (err_code < 0)
	  {
	    goto ODBC_ERROR;
	  }


	c_data_type = SQL_C_NUMERIC;
	sql_bind_type = SQL_DECIMAL;

	indPtr = sizeof (value_list->ns_val);
	SQL_CHK_ERR (handle->hstmt,
		     SQL_HANDLE_STMT,
		     err_code = SQLBindParameter (handle->hstmt,
						  bind_num,
						  SQL_PARAM_INPUT,
						  c_data_type,
						  sql_bind_type,
						  value_list->ns_val.precision,
						  value_list->ns_val.scale, &value_list->ns_val, 0,
						  (SQLLEN *) & indPtr));

	SQL_CHK_ERR (hdesc,
		     SQL_HANDLE_DESC,
		     err_code = SQLSetDescField (hdesc, bind_num, SQL_DESC_TYPE, (SQLPOINTER) SQL_C_NUMERIC, SQL_NTS));

	SQL_CHK_ERR (hdesc,
		     SQL_HANDLE_DESC,
		     err_code =
		     SQLSetDescField (hdesc, bind_num, SQL_DESC_PRECISION,
				      (SQLPOINTER) value_list->ns_val.precision, 0));

	SQL_CHK_ERR (hdesc,
		     SQL_HANDLE_DESC,
		     err_code =
		     SQLSetDescField (hdesc, bind_num, SQL_DESC_SCALE, (SQLPOINTER) value_list->ns_val.scale, 0));

	SQL_CHK_ERR (hdesc,
		     SQL_HANDLE_DESC,
		     err_code =
		     SQLSetDescField (hdesc, bind_num, SQL_DESC_DATA_PTR, (SQLPOINTER) & value_list->ns_val, 0));

	SQLFreeHandle (SQL_HANDLE_DESC, hdesc);
      }
      break;
    case CCI_U_TYPE_BIGINT:
    case CCI_U_TYPE_UBIGINT:
      {
	DB_BIGINT bi_val;
	net_arg_get_bigint (&bi_val, net_value);

	c_data_type = SQL_C_UBIGINT;
	sql_bind_type = SQL_BIGINT;

	value_list->bigint_val = bi_val;

	SQL_CHK_ERR (handle->hstmt,
		     SQL_HANDLE_STMT,
		     err_code = SQLBindParameter (handle->hstmt,
						  bind_num,
						  SQL_PARAM_INPUT,
						  c_data_type, sql_bind_type, 0, 0, &value_list->bigint_val, 0,
						  &indPtr));
      }
      break;
    case CCI_U_TYPE_INT:
    case CCI_U_TYPE_UINT:
      {
	int i_val;

	net_arg_get_int (&i_val, net_value);

	c_data_type = SQL_C_LONG;
	sql_bind_type = SQL_INTEGER;

	value_list->integer_val = i_val;


	SQL_CHK_ERR (handle->hstmt,
		     SQL_HANDLE_STMT,
		     err_code = SQLBindParameter (handle->hstmt,
						  bind_num,
						  SQL_PARAM_INPUT,
						  c_data_type, sql_bind_type, 0, 0, &value_list->integer_val, 0,
						  &indPtr));

      }
      break;
    case CCI_U_TYPE_SHORT:
    case CCI_U_TYPE_USHORT:
      {
	short s_val;

	net_arg_get_short (&s_val, net_value);

	c_data_type = SQL_C_SHORT;
	sql_bind_type = SQL_SMALLINT;
	value_list->smallInt_val = s_val;

	SQL_CHK_ERR (handle->hstmt,
		     SQL_HANDLE_STMT,
		     err_code = SQLBindParameter (handle->hstmt,
						  bind_num,
						  SQL_PARAM_INPUT,
						  c_data_type, sql_bind_type, 0, 0, &value_list->smallInt_val, 0,
						  &indPtr));
      }
      break;
    case CCI_U_TYPE_FLOAT:
      {
	float f_val;

	net_arg_get_float (&f_val, net_value);

	c_data_type = SQL_C_FLOAT;
	sql_bind_type = SQL_REAL;
	value_list->real_val = f_val;

	SQL_CHK_ERR (handle->hstmt,
		     SQL_HANDLE_STMT,
		     err_code = SQLBindParameter (handle->hstmt,
						  bind_num,
						  SQL_PARAM_INPUT,
						  c_data_type, sql_bind_type, 0, 0, &value_list->real_val, 0, &indPtr));
      }
      break;
    case CCI_U_TYPE_DOUBLE:
      {
	double d_val;
	net_arg_get_double (&d_val, net_value);

	c_data_type = SQL_C_DOUBLE;
	sql_bind_type = SQL_DOUBLE;
	value_list->double_val = d_val;

	SQL_CHK_ERR (handle->hstmt,
		     SQL_HANDLE_STMT,
		     err_code = SQLBindParameter (handle->hstmt,
						  bind_num,
						  SQL_PARAM_INPUT,
						  c_data_type, sql_bind_type, 0, 0, &value_list->double_val, 0,
						  &indPtr));
      }
      break;
    case CCI_U_TYPE_DATE:
      {
	short month, day, year;
	net_arg_get_date (&year, &month, &day, net_value);

	c_data_type = SQL_C_TYPE_DATE;
	sql_bind_type = SQL_TYPE_DATE;

	value_list->ds_val.year = year;
	value_list->ds_val.month = month;
	value_list->ds_val.day = day;

	SQL_CHK_ERR (handle->hstmt,
		     SQL_HANDLE_STMT,
		     err_code = SQLBindParameter (handle->hstmt,
						  bind_num,
						  SQL_PARAM_INPUT,
						  c_data_type,
						  sql_bind_type,
						  sizeof (SQL_DATE_STRUCT), 0, &value_list->ds_val, 0, &indPtr));
      }
      break;
    case CCI_U_TYPE_TIME:
      {
	short hh, mm, ss;
	net_arg_get_time (&hh, &mm, &ss, net_value);

	c_data_type = SQL_C_TYPE_TIME;
	sql_bind_type = SQL_TYPE_TIME;

	value_list->ts_val.hour = hh;
	value_list->ts_val.minute = mm;
	value_list->ts_val.second = ss;

	SQL_CHK_ERR (handle->hstmt,
		     SQL_HANDLE_STMT,
		     err_code = SQLBindParameter (handle->hstmt,
						  bind_num,
						  SQL_PARAM_INPUT,
						  c_data_type,
						  sql_bind_type,
						  sizeof (SQL_TIME_STRUCT), 0, &value_list->ts_val, 0, &indPtr));
      }
      break;
    case CCI_U_TYPE_TIMESTAMP:
      {
	short yr, mon, day, hh, mm, ss;
	DB_DATE date;
	DB_TIME time;
	DB_TIMESTAMP ts;

	net_arg_get_timestamp (&yr, &mon, &day, &hh, &mm, &ss, net_value);
	err_code = db_date_encode (&date, mon, day, yr);
	if (err_code != NO_ERROR)
	  {
	    break;
	  }
	err_code = db_time_encode (&time, hh, mm, ss);
	if (err_code != NO_ERROR)
	  {
	    break;
	  }
	err_code = db_timestamp_encode_ses (&date, &time, &ts, NULL);
	if (err_code != NO_ERROR)
	  {
	    break;
	  }

	c_data_type = SQL_C_CHAR;
	sql_bind_type = SQL_TYPE_TIMESTAMP;

	sprintf (value_list->time_stemp_str_val, "%d-%d-%d %d:%d:%d", yr, mon, day, hh, mm, ss);

	SQL_CHK_ERR (handle->hstmt,
		     SQL_HANDLE_STMT,
		     err_code = SQLBindParameter (handle->hstmt,
						  bind_num,
						  SQL_PARAM_INPUT,
						  c_data_type,
						  sql_bind_type,
						  0,
						  0,
						  (SQLPOINTER) (&value_list->time_stemp_str_val),
						  sizeof (value_list->time_stemp_str_val), NULL));
      }
      break;
    case CCI_U_TYPE_DATETIME:
      {
	short yr, mon, day, hh, mm, ss, ms;
	DB_DATETIME dt;

	net_arg_get_datetime (&yr, &mon, &day, &hh, &mm, &ss, &ms, net_value);
	err_code = db_datetime_encode (&dt, mon, day, yr, hh, mm, ss, ms);
	if (err_code != NO_ERROR)
	  {
	    break;
	  }

	if (curr_dbms_type == SUPPORTED_DBMS_MYSQL || curr_dbms_type == SUPPORTED_DBMS_MARIADB)
	  {
	    c_data_type = SQL_C_CHAR;
	    sql_bind_type = SQL_TYPE_TIMESTAMP;

	    sprintf (value_list->time_stemp_str_val, "%d-%d-%d %d:%d:%d.%d", yr, mon, day, hh, mm, ss, ms);

	    SQL_CHK_ERR (handle->hstmt,
			 SQL_HANDLE_STMT,
			 err_code = SQLBindParameter (handle->hstmt,
						      bind_num,
						      SQL_PARAM_INPUT,
						      c_data_type,
						      sql_bind_type,
						      0,
						      0,
						      (SQLPOINTER) (&value_list->time_stemp_str_val),
						      sizeof (value_list->time_stemp_str_val), NULL));
	  }
	else if (curr_dbms_type == SUPPORTED_DBMS_ORACLE)
	  {
	    c_data_type = SQL_C_TYPE_TIMESTAMP;
	    sql_bind_type = SQL_TYPE_TIMESTAMP;

	    value_list->tss_val.year = yr;
	    value_list->tss_val.month = mon;
	    value_list->tss_val.day = day;
	    value_list->tss_val.hour = hh;
	    value_list->tss_val.minute = mm;
	    value_list->tss_val.second = ss;
	    value_list->tss_val.fraction = ms;

	    SQL_CHK_ERR (handle->hstmt,
			 SQL_HANDLE_STMT,
			 err_code = SQLBindParameter (handle->hstmt,
						      bind_num,
						      SQL_PARAM_INPUT,
						      c_data_type,
						      sql_bind_type,
						      0,
						      0,
						      (SQLPOINTER) (&value_list->tss_val),
						      sizeof (value_list->tss_val), NULL));
	  }
	else
	  {
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CGW_NOT_SUPPORTED_DBMS, 0);
	    return ER_CGW_NOT_SUPPORTED_DBMS;
	  }
      }
      break;
      /* Not Support Type  */
    case CCI_U_TYPE_NULL:
    case CCI_U_TYPE_BIT:
    case CCI_U_TYPE_VARBIT:
    case CCI_U_TYPE_MONETARY:
    case CCI_U_TYPE_SET:
    case CCI_U_TYPE_MULTISET:
    case CCI_U_TYPE_SEQUENCE:
    case CCI_U_TYPE_OBJECT:
    case CCI_U_TYPE_BLOB:
    case CCI_U_TYPE_CLOB:
    case CCI_U_TYPE_JSON:
    case CCI_U_TYPE_ENUM:
    case CCI_U_TYPE_DATETIMELTZ:
    case CCI_U_TYPE_DATETIMETZ:
    case CCI_U_TYPE_TIMESTAMPTZ:
    case CCI_U_TYPE_TIMESTAMPLTZ:
    default:
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CGW_NOT_SUPPORTED_TYPE, 2, cgw_utype_to_string (type), type);
	return ER_CGW_NOT_SUPPORTED_TYPE;
      }
    }

  if (err_code < 0)
    {
      return ERROR_INFO_SET (err_code, DBMS_ERROR_INDICATOR);
    }

  return data_size;

ODBC_ERROR:
  return ER_FAILED;
}

int
cgw_sql_prepare (SQLCHAR * sql_stmt)
{
  SQLRETURN err_code;

  if (local_odbc_handle->hstmt == NULL)
    {
      SQL_CHK_ERR (local_odbc_handle->hdbc,
		   SQL_HANDLE_DBC,
		   err_code = SQLAllocHandle (SQL_HANDLE_STMT, local_odbc_handle->hdbc, &local_odbc_handle->hstmt));
    }

  SQL_CHK_ERR (local_odbc_handle->hstmt, SQL_HANDLE_STMT, err_code =
	       SQLPrepare (local_odbc_handle->hstmt, sql_stmt, SQL_NTS));

  return (int) err_code;

ODBC_ERROR:
  return ER_FAILED;
}

int
cgw_get_num_cols (SQLHSTMT hstmt, SQLSMALLINT * num_cols)
{
  SQLRETURN err_code;

  if (hstmt == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CGW_INVALID_STMT_HANDLE, 0);
      goto ODBC_ERROR;
    }

  SQL_CHK_ERR (hstmt, SQL_HANDLE_STMT, err_code = SQLNumResultCols (hstmt, num_cols));

  return (int) err_code;

ODBC_ERROR:
  return ER_FAILED;
}

int
cgw_make_bind_value (T_CGW_HANDLE * handle, int num_bind, int argc, void **argv, ODBC_BIND_INFO ** ret_val)
{
  int i, type_idx, val_idx;
  int err_code;
  ODBC_BIND_INFO *bind_value_list = NULL;

  *ret_val = NULL;

  if (handle == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CGW_INVALID_HANDLE, 0);
      return ER_CGW_INVALID_HANDLE;
    }

  if (num_bind != (argc / 2))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CGW_NUM_BIND, 0);
      return ER_CGW_NUM_BIND;
    }

  bind_value_list = (ODBC_BIND_INFO *) MALLOC (sizeof (ODBC_BIND_INFO) * num_bind);
  if (bind_value_list == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INTERFACE_NO_MORE_MEMORY, 0);
      return ER_INTERFACE_NO_MORE_MEMORY;
    }

  for (i = 0; i < num_bind; i++)
    {
      type_idx = 2 * i;
      val_idx = 2 * i + 1;
      err_code = cgw_set_bindparam (handle, i + 1, argv[type_idx], argv[val_idx], &(bind_value_list[i]));
      if (err_code < 0)
	{
	  FREE_MEM (bind_value_list);
	  return err_code;
	}
    }

  *ret_val = bind_value_list;

  return NO_ERROR;
}

int
cgw_is_database_connected ()
{
  SQLRETURN err_code;
  SQLINTEGER is_conn_dead = DISCONNECTED_STATE;

  if (local_odbc_handle == NULL || local_odbc_handle->hdbc == NULL)
    {
      return DISCONNECTED_STATE;
    }

  err_code =
    SQLGetConnectAttr (local_odbc_handle->hdbc, SQL_ATTR_CONNECTION_DEAD, (SQLPOINTER) & is_conn_dead, SQL_IS_INTEGER,
		       0);

  if (err_code < 0)
    {
      return DISCONNECTED_STATE;
    }

  return (is_conn_dead == SQL_CD_FALSE) ? CONNECTED_STATE : DISCONNECTED_STATE;
}

static int
numeric_string_adjust (SQL_NUMERIC_STRUCT * numeric, char *string)
{
  char *pt, *pt2;
  char hexstr[SQL_MAX_NUMERIC_LEN + 1] = { 0 };
  char num_val[DECIMAL_DIGIT_MAX_LEN + 1] = { 0 };
  short i;
  size_t num_add_zero;
  UINT64 number = 0;
  char *endptr = NULL;
  int error;

  pt = string;

  pt2 = strchr (pt, '.');
  if (pt2 != NULL)
    {
      strncpy (num_val, pt, pt2 - pt);
      num_val[pt2 - pt] = '\0';
      ++pt2;
      strcat (num_val, pt2);

      num_add_zero = numeric->scale - strlen (pt2);
      if (num_add_zero < 0)
	{
	  num_val[strlen (num_val) + num_add_zero] = '\0';
	}
      else
	{
	  // add additional '0' for scale
	  for (pt = num_val + strlen (num_val), i = 1; i <= num_add_zero; ++pt, ++i)
	    {
	      *pt = '0';
	    }
	  *pt = '\0';
	}
    }
  else
    {
      strcpy (num_val, pt);
    }

  error = str_to_uint64 (&number, &endptr, num_val, 10);
  if (error < 0)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CGW_INVALID_NUMERIC_VALUE, 0);
      return ER_CGW_INVALID_NUMERIC_VALUE;
    }

  sprintf (hexstr, "%llX", number);

  error = hex_to_numeric_val (numeric, hexstr);
  if (error < 0)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CGW_INVALID_NUMERIC_VALUE, 0);
      return ER_CGW_INVALID_NUMERIC_VALUE;
    }

  return NO_ERROR;
}

static int
hex_to_numeric_val (SQL_NUMERIC_STRUCT * numeric, char *hexstr)
{
  size_t slen = 0;
  size_t loop = 0;
  int error;
  unsigned char ms_val = 0, ls_val = 0;

  slen = strlen (hexstr);
  if (slen < 1)
    {
      return ER_FAILED;
    }

  loop = (slen % 2) + (slen / 2);

  for (size_t i = 0; i < loop; i++)
    {
      if (slen == 1)
	{
	  error = hex_to_char (*(hexstr), &ms_val);
	  if (error < 0)
	    {
	      return ER_FAILED;
	    }
	  numeric->val[i] = ms_val;
	  break;
	}
      else
	{
	  error = hex_to_char (*(hexstr + (slen - 2)), &ms_val);
	  if (error < 0)
	    {
	      return ER_FAILED;
	    }

	  error = hex_to_char (*(hexstr + slen - 1), &ls_val);
	  if (error < 0)
	    {
	      return ER_FAILED;
	    }

	  numeric->val[i] = (ms_val << 4) | (ls_val);
	}
      slen -= 2;
    }

  return NO_ERROR;
}

static int
hex_to_char (char c, unsigned char *result)
{
  int error = NO_ERROR;

  if (c >= '0' && c <= '9')
    *result = c - '0';
  else if (c >= 'a' && c <= 'f')
    *result = c - 'a' + 10;
  else if (c >= 'A' && c <= 'F')
    *result = c - 'A' + 10;
  else
    error = ER_FAILED;

  return error;
}

static void
cgw_cleanup_handle (T_CGW_HANDLE * handle)
{
  if (handle == NULL)
    {
      return;
    }

  if (handle->hstmt)
    {
      SQLFreeHandle (SQL_HANDLE_STMT, handle->hstmt);
      handle->hstmt = NULL;
    }

  if (handle->hdbc)
    {
      SQLDisconnect (handle->hdbc);
      SQLFreeHandle (SQL_HANDLE_DBC, handle->hdbc);
      handle->hdbc = NULL;
    }

  if (handle->henv)
    {
      SQLFreeHandle (SQL_HANDLE_ENV, handle->henv);
      handle->henv = NULL;
    }

  FREE_MEM (handle);

  local_odbc_handle = NULL;
}

int
cgw_endtran (SQLHDBC hdbc, int tran_type)
{
  SQLRETURN err_code;
  if (hdbc == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CGW_INVALID_DBC_HANDLE, 0);
      goto ODBC_ERROR;
    }

  if (tran_type == CCI_TRAN_COMMIT)
    {
      SQL_CHK_ERR (hdbc, SQL_HANDLE_DBC, err_code = SQLEndTran (SQL_HANDLE_DBC, hdbc, SQL_COMMIT));
    }
  else
    {
      SQL_CHK_ERR (hdbc, SQL_HANDLE_DBC, err_code = SQLEndTran (SQL_HANDLE_DBC, hdbc, SQL_ROLLBACK));
    }

  return err_code;

ODBC_ERROR:
  return ER_FAILED;
}

int
cgw_init_odbc_handle (void)
{
  local_odbc_handle = (T_CGW_HANDLE *) MALLOC (sizeof (T_CGW_HANDLE));

  if (local_odbc_handle == NULL)
    {
      return ER_FAILED;
    }

  memset (local_odbc_handle, 0x0, sizeof (T_CGW_HANDLE));

  return NO_ERROR;
}

int
cgw_get_driver_info (SQLHDBC hdbc, SQLUSMALLINT info_type, void *driver_info, SQLSMALLINT size)
{
  SQLRETURN err_code;
  SQLSMALLINT len;

  if (hdbc == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CGW_INVALID_DBC_HANDLE, 0);
      goto ODBC_ERROR;
    }

  SQL_CHK_ERR (hdbc, SQL_HANDLE_DBC, err_code = SQLGetInfo (hdbc, info_type, driver_info, size, &len));

  return NO_ERROR;

ODBC_ERROR:
  return ER_FAILED;
}


SQLULEN
get_datatype_size (SQLSMALLINT s_type, SQLULEN chars, SQLLEN precision, SQLLEN scale)
{
  switch (s_type)
    {
    case SQL_CHAR:
    case SQL_VARCHAR:
    case SQL_LONGVARCHAR:
    case SQL_WCHAR:
    case SQL_WVARCHAR:
    case SQL_WLONGVARCHAR:
      chars++;
      break;
#if (ODBCVER >= 0x0350)
    case SQL_GUID:
#endif
    case SQL_BINARY:
    case SQL_VARBINARY:
    case SQL_LONGVARBINARY:
      break;
    case SQL_DECIMAL:
    case SQL_NUMERIC:
      if (precision > scale)
	{
	  chars += 3;		// sign symbol and decimal symbol
	}
      else
	{
	  chars = scale + 3;	// if the scale is larger. ex)number(4, 6)
	}
      break;
    case SQL_BIT:
      chars = sizeof (unsigned char);
      break;
    case SQL_TINYINT:
      chars = sizeof (char);
      break;
    case SQL_SMALLINT:
      chars = sizeof (short);
      break;
    case SQL_INTEGER:
      chars = sizeof (int);
      break;
    case SQL_BIGINT:
      chars = sizeof (INT64);
      break;
    case SQL_REAL:
    case SQL_FLOAT:
      chars = sizeof (float);
      break;
    case SQL_DOUBLE:
      chars = sizeof (double);
      break;
#if (ODBCVER >= 0x0300)
    case SQL_DATETIME:
      chars = sizeof (TIMESTAMP_STRUCT);
      break;
#else
    case SQL_DATE:
      chars = sizeof (DATE_STRUCT);
      break;
#endif
    case SQL_TIME:
      chars = sizeof (TIME_STRUCT);
      break;
    case SQL_TIMESTAMP:
      chars = sizeof (TIMESTAMP_STRUCT);
      break;
#if (ODBCVER >= 0x0300)
    case SQL_TYPE_DATE:
      chars = sizeof (SQL_DATE_STRUCT);
      break;
    case SQL_TYPE_TIME:
      chars = sizeof (TIME_STRUCT);
      break;
    case SQL_TYPE_TIMESTAMP:
      chars = sizeof (TIMESTAMP_STRUCT);
      break;

    case SQL_INTERVAL_YEAR:
    case SQL_INTERVAL_MONTH:
    case SQL_INTERVAL_YEAR_TO_MONTH:
    case SQL_INTERVAL_DAY:
    case SQL_INTERVAL_HOUR:
    case SQL_INTERVAL_MINUTE:
    case SQL_INTERVAL_SECOND:
    case SQL_INTERVAL_DAY_TO_HOUR:
    case SQL_INTERVAL_DAY_TO_MINUTE:
    case SQL_INTERVAL_DAY_TO_SECOND:
    case SQL_INTERVAL_HOUR_TO_MINUTE:
    case SQL_INTERVAL_HOUR_TO_SECOND:
    case SQL_INTERVAL_MINUTE_TO_SECOND:
      chars = sizeof (SQL_INTERVAL_STRUCT);
      break;
#endif
    default:
      chars = 0;
      break;
    }
  return chars;
}

static SQLSMALLINT
get_c_type (SQLSMALLINT s_type, SQLLEN is_unsigned_type)
{
  SQLSMALLINT c_type;
  switch (s_type)
    {
#if (ODBCVER >= 0x0350)
    case SQL_GUID:
#endif
    case SQL_CHAR:
    case SQL_VARCHAR:
    case SQL_LONGVARCHAR:
      c_type = SQL_C_WCHAR;
      break;
    case SQL_WCHAR:
    case SQL_WVARCHAR:
    case SQL_WLONGVARCHAR:
      c_type = SQL_C_WCHAR;
      break;
    case SQL_BINARY:
    case SQL_VARBINARY:
    case SQL_LONGVARBINARY:
      c_type = SQL_C_BINARY;
      break;
    case SQL_DECIMAL:
    case SQL_NUMERIC:
      c_type = SQL_C_WCHAR;
      break;
    case SQL_BIT:
      c_type = SQL_C_BIT;
      break;
    case SQL_TINYINT:
      c_type = (is_unsigned_type) ? SQL_C_WCHAR : SQL_C_TINYINT;
      break;
    case SQL_SMALLINT:
      c_type = (is_unsigned_type) ? SQL_C_WCHAR : SQL_C_SHORT;
      break;
    case SQL_INTEGER:
      c_type = (is_unsigned_type) ? SQL_C_WCHAR : SQL_C_LONG;
      break;
    case SQL_BIGINT:
      c_type = (is_unsigned_type) ? SQL_C_WCHAR : SQL_C_SBIGINT;
      break;
    case SQL_REAL:
    case SQL_FLOAT:
      c_type = SQL_C_FLOAT;
      break;
    case SQL_DOUBLE:
      c_type = SQL_C_DOUBLE;
      break;
    case SQL_DATE:
      c_type = SQL_C_DATE;
      break;
    case SQL_TIME:
      c_type = SQL_C_TIME;
      break;
    case SQL_TIMESTAMP:
      c_type = SQL_C_TIMESTAMP;
      break;
#if (ODBCVER >= 0x0300)
    case SQL_TYPE_DATE:
      c_type = SQL_C_TYPE_DATE;
      break;
    case SQL_TYPE_TIME:
      c_type = SQL_C_TYPE_TIME;
      break;
    case SQL_TYPE_TIMESTAMP:
      c_type = SQL_C_TYPE_TIMESTAMP;
      break;
    case SQL_INTERVAL_YEAR:
      c_type = SQL_C_INTERVAL_YEAR;
      break;
    case SQL_INTERVAL_MONTH:
      c_type = SQL_C_INTERVAL_MONTH;
      break;
    case SQL_INTERVAL_YEAR_TO_MONTH:
      c_type = SQL_C_INTERVAL_YEAR_TO_MONTH;
      break;
    case SQL_INTERVAL_DAY:
      c_type = SQL_C_INTERVAL_DAY;
      break;
    case SQL_INTERVAL_HOUR:
      c_type = SQL_C_INTERVAL_HOUR;
      break;
    case SQL_INTERVAL_MINUTE:
      c_type = SQL_C_INTERVAL_MINUTE;
      break;
    case SQL_INTERVAL_SECOND:
      c_type = SQL_C_INTERVAL_SECOND;
      break;
    case SQL_INTERVAL_DAY_TO_HOUR:
      c_type = SQL_C_INTERVAL_DAY_TO_HOUR;
      break;
    case SQL_INTERVAL_DAY_TO_MINUTE:
      c_type = SQL_C_INTERVAL_DAY_TO_MINUTE;
      break;
    case SQL_INTERVAL_DAY_TO_SECOND:
      c_type = SQL_C_INTERVAL_DAY_TO_SECOND;
      break;
    case SQL_INTERVAL_HOUR_TO_MINUTE:
      c_type = SQL_C_INTERVAL_HOUR_TO_MINUTE;
      break;
    case SQL_INTERVAL_HOUR_TO_SECOND:
      c_type = SQL_C_INTERVAL_HOUR_TO_SECOND;
      break;
    case SQL_INTERVAL_MINUTE_TO_SECOND:
      c_type = SQL_C_INTERVAL_MINUTE_TO_SECOND;
      break;
#endif
    default:
      c_type = SQL_UNKNOWN_TYPE;
      break;
    }
  return c_type;
}

int
cgw_set_stmt_attr (SQLHSTMT hstmt, SQLINTEGER attr, SQLPOINTER val, SQLINTEGER len)
{
  SQLRETURN err_code;

  if (hstmt == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CGW_INVALID_STMT_HANDLE, 0);
      goto ODBC_ERROR;
    }

  SQL_CHK_ERR (hstmt, SQL_HANDLE_STMT, err_code = SQLSetStmtAttr (hstmt, attr, val, len));

  return err_code;

ODBC_ERROR:
  return ER_FAILED;
}

static void
cgw_link_server_info (SQLHDBC hdbc)
{
  SQLCHAR dbms_name[CGW_LINK_SERVER_NAME_LEN] = { 0, };
  SQLCHAR dbms_ver[CGW_LINK_SERVER_NAME_LEN] = { 0, };
  SQLCHAR driver_name[CGW_LINK_SERVER_NAME_LEN] = { 0, };
  SQLCHAR driver_version[CGW_LINK_SERVER_NAME_LEN] = { 0, };
  SQLCHAR odbc_version[CGW_LINK_SERVER_NAME_LEN] = { 0, };

  if (hdbc != NULL)
    {
      cas_log_write_and_end (0, false, "Link Server Info.");

      cgw_get_driver_info (hdbc, SQL_DBMS_NAME, dbms_name, sizeof (dbms_name));
      cas_log_write_and_end (0, false, "DBMS Name : %s", dbms_name);

      cgw_get_driver_info (hdbc, SQL_DBMS_VER, dbms_ver, sizeof (dbms_ver));
      cas_log_write_and_end (0, false, "DBMS Version : %s", dbms_ver);

      cgw_get_driver_info (hdbc, SQL_DRIVER_NAME, driver_name, sizeof (driver_name));
      cas_log_write_and_end (0, false, "Driver Name : %s", driver_name);

      cgw_get_driver_info (hdbc, SQL_DRIVER_VER, driver_version, sizeof (driver_version));
      cas_log_write_and_end (0, false, "Driver Version : %s", driver_version);

      cgw_get_driver_info (hdbc, SQL_ODBC_VER, odbc_version, sizeof (odbc_version));
      cas_log_write_and_end (0, false, "ODBC Version : %s", odbc_version);
    }
}

static bool
cgw_is_support_datatype (SQLSMALLINT data_type, SQLLEN type_size)
{
  bool support_data_type = true;

  switch (data_type)
    {
    case SQL_CHAR:
    case SQL_VARCHAR:
    case SQL_LONGVARCHAR:
    case SQL_WCHAR:
    case SQL_WVARCHAR:
    case SQL_WLONGVARCHAR:
    case SQL_DECIMAL:
    case SQL_REAL:
    case SQL_NUMERIC:
    case SQL_INTEGER:
    case SQL_SMALLINT:
    case SQL_TINYINT:
    case SQL_FLOAT:
    case SQL_DOUBLE:
    case SQL_BIGINT:
#if (ODBCVER >= 0x0300)
    case SQL_DATETIME:
#else
    case SQL_DATE:
#endif
    case SQL_TIMESTAMP:
#if (ODBCVER >= 0x0300)
    case SQL_TYPE_TIMESTAMP:
    case SQL_TYPE_DATE:
    case SQL_TYPE_TIME:
#endif
      if ((data_type == SQL_LONGVARCHAR || data_type == SQL_WLONGVARCHAR) && type_size > STRING_MAX_SIZE)
	{
	  support_data_type = false;
	}
      break;
    case SQL_VARBINARY:
    case SQL_BINARY:
    case SQL_BIT:
    case SQL_UNKNOWN_TYPE:
    case SQL_LONGVARBINARY:
#if (ODBCVER >= 0x0350)
    case SQL_GUID:
#endif
      support_data_type = false;
      break;
    default:
      support_data_type = false;
      break;
    }

  return support_data_type;
}

static int
cgw_count_number_of_digits (int num_bits)
{

  if (num_bits < 0 || num_bits > 64)
    {
      return ER_FAILED;
    }

  return (num_bits == 0) ? 0 : NUM_OF_DIGITS (pow (2, num_bits) - 1);
}

SUPPORTED_DBMS_TYPE
cgw_is_supported_dbms (char *dbms)
{
  if (dbms == NULL)
    return NOT_SUPPORTED_DBMS;

  ut_tolower (dbms);

  for (int i = 0; i < supported_dbms_max_num; i++)
    {
      if (strcmp (dbms, supported_dbms_list[i].dbms_name) == 0)
	{
	  return supported_dbms_list[i].dbms_type;
	}
    }
  return NOT_SUPPORTED_DBMS;
}

int
cgw_get_stmt_handle (SQLHDBC hdbc, SQLHSTMT * stmt)
{
  if (hdbc == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CGW_INVALID_DBC_HANDLE, 0);
      goto ODBC_ERROR;
    }

  SQL_CHK_ERR (hdbc, SQL_HANDLE_DBC, SQLAllocHandle (SQL_HANDLE_STMT, hdbc, stmt));

  return NO_ERROR;

ODBC_ERROR:
  return ER_FAILED;
}

void
cgw_set_dbms_type (SUPPORTED_DBMS_TYPE dbms_type)
{
  curr_dbms_type = dbms_type;
}

int
cgw_get_dbms_type ()
{
  return curr_dbms_type;
}

static char *
cgw_datatype_to_string (SQLLEN type)
{
  switch (type)
    {
    case SQL_UNKNOWN_TYPE:
      return (char *) "SQL_UNKNOWN_TYPE";
      break;
    case SQL_CHAR:
      return (char *) "SQL_CHAR";
      break;
    case SQL_NUMERIC:
      return (char *) "SQL_NUMERIC";
      break;
    case SQL_DECIMAL:
      return (char *) "SQL_DECIMAL";
      break;
    case SQL_INTEGER:
      return (char *) "SQL_INTEGER";
      break;
    case SQL_SMALLINT:
      return (char *) "SQL_SMALLINT";
      break;
    case SQL_FLOAT:
      return (char *) "SQL_FLOAT";
      break;
    case SQL_REAL:
      return (char *) "SQL_REAL";
      break;
    case SQL_DOUBLE:
      return (char *) "SQL_DOUBLE";
      break;
#if (ODBCVER >= 0x0300)
    case SQL_DATETIME:
      return (char *) "SQL_DATETIME";
      break;
#else
    case SQL_DATE:
      return (char *) "SQL_DATE";
      break;
#endif
    case SQL_VARCHAR:
      return (char *) "SQL_VARCHAR";
      break;
#if (ODBCVER >= 0x0300)
    case SQL_TYPE_DATE:
      return (char *) "SQL_TYPE_DATE";
      break;
    case SQL_TYPE_TIME:
      return (char *) "SQL_TYPE_TIME";
      break;
    case SQL_TYPE_TIMESTAMP:
      return (char *) "SQL_TYPE_TIMESTAMP";
      break;
#endif

#if (ODBCVER >= 0x0300)
    case SQL_INTERVAL:
      return (char *) "SQL_INTERVAL";
      break;
#else
    case SQL_TIME:
      return (char *) "SQL_TIME";
      break;
#endif
    case SQL_TIMESTAMP:
      return (char *) "SQL_TIMESTAMP";
      break;
    case SQL_LONGVARCHAR:
      return (char *) "SQL_LONGVARCHAR";
      break;
    case SQL_BINARY:
      return (char *) "SQL_BINARY";
      break;
    case SQL_VARBINARY:
      return (char *) "SQL_VARBINARY";
      break;
    case SQL_LONGVARBINARY:
      return (char *) "SQL_LONGVARBINARY";
      break;
    case SQL_BIGINT:
      return (char *) "SQL_BIGINT";
      break;
    case SQL_TINYINT:
      return (char *) "SQL_TINYINT";
      break;
    case SQL_BIT:
      return (char *) "SQL_BIT";
      break;
#if (ODBCVER >= 0x0350)
    case SQL_GUID:
      return (char *) "SQL_GUID";
      break;
#endif
    case SQL_WCHAR:
      return (char *) "SQL_WCHAR";
    case SQL_WVARCHAR:
      return (char *) "SQL_WVARCHAR";
    case SQL_WLONGVARCHAR:
      return (char *) "SQL_WLONGVARCHAR";
    default:
      return (char *) "Unknown type";
    }
}

static char *
cgw_utype_to_string (int type)
{
  switch (type)
    {
    case CCI_U_TYPE_CHAR:
      return (char *) "char";
    case CCI_U_TYPE_STRING:
      return (char *) "char";
    case CCI_U_TYPE_NCHAR:
      return (char *) "nchar";
    case CCI_U_TYPE_VARNCHAR:
      return (char *) "nchar varying";
    case CCI_U_TYPE_NUMERIC:
      return (char *) "numeric";
    case CCI_U_TYPE_BIGINT:
      return (char *) "bigint";
    case CCI_U_TYPE_INT:
      return (char *) "integer";
    case CCI_U_TYPE_SHORT:
      return (char *) "smallint";
    case CCI_U_TYPE_FLOAT:
      return (char *) "float";
    case CCI_U_TYPE_DOUBLE:
      return (char *) "double";
    case CCI_U_TYPE_DATE:
      return (char *) "date";
    case CCI_U_TYPE_TIME:
      return (char *) "time";
    case CCI_U_TYPE_TIMESTAMP:
      return (char *) "timestamp";
    case CCI_U_TYPE_DATETIME:
      return (char *) "datetime";
    case CCI_U_TYPE_NULL:
      return (char *) "null";
    case CCI_U_TYPE_BIT:
      return (char *) "bit";
    case CCI_U_TYPE_VARBIT:
      return (char *) "bit varying";
    case CCI_U_TYPE_MONETARY:
      return (char *) "monetary";
    case CCI_U_TYPE_SET:
      return (char *) "set";
    case CCI_U_TYPE_MULTISET:
      return (char *) "multiset";
    case CCI_U_TYPE_SEQUENCE:
      return (char *) "sequence";
    case CCI_U_TYPE_OBJECT:
      return (char *) "object";
    case CCI_U_TYPE_BLOB:
      return (char *) "blob";
    case CCI_U_TYPE_CLOB:
      return (char *) "clob";
    case CCI_U_TYPE_JSON:
      return (char *) "json";
    case CCI_U_TYPE_ENUM:
      return (char *) "enum";
    case CCI_U_TYPE_DATETIMELTZ:
      return (char *) "datetimeltz";
    case CCI_U_TYPE_DATETIMETZ:
      return (char *) "datetimetz";
    case CCI_U_TYPE_TIMESTAMPTZ:
      return (char *) "timestamptz";
    case CCI_U_TYPE_TIMESTAMPLTZ:
      return (char *) "timestampltz";
    default:
      return (char *) "";
    }
}

int
cgw_rewrite_query (char *src_query, char **sql)
{
  char *source = NULL;
  char *rewrite_query = NULL;
  char *select_from = NULL;
  char *inline_view = NULL;
  char *new_inline_view = NULL;
  char *cols_type = NULL;
  char **cols_list = NULL;
  char *where = NULL;

  char *start = NULL;
  char *end = NULL;

  SQLSMALLINT odbc_num_cols = 0;
  int src_num_cols = 0;
  int err_code = 0;

  size_t select_from_len = 0;
  size_t inline_view_len = 0;
  size_t new_inline_view_len = 0;
  size_t cols_type_len = 0;
  size_t where_len = 0;

  char col_name[COL_NAME_LEN + 1];
  SQLSMALLINT col_name_length = 0;
  SQLHSTMT hstmt = NULL;

  ALLOC_COPY_STRLEN (source, src_query);

  end = strstr (source, REWRITE_DELIMITER_FROM);
  if (end == NULL)
    {
      err_code = ERR_REWRITE_FAILED;
      goto ODBC_ERROR;
    }

  select_from = source;
  select_from_len = (end + REWRITE_DELIMITER_FROM_LEN) - source;
  start = end + REWRITE_DELIMITER_FROM_LEN + 1;


  end = strstr (source, REWRITE_DELIMITER_CUBLINK);
  if (end == NULL)
    {
      err_code = ERR_REWRITE_FAILED;
      goto ODBC_ERROR;
    }

  inline_view = select_from + select_from_len + 1;
  inline_view_len = (end + 1) - (start);
  start = end + REWRITE_DELIMITER_CUBLINK_LEN + 3;

  end = strstr (inline_view + inline_view_len, "WHERE");
  if (end == NULL)
    {
      err_code = ERR_REWRITE_FAILED;
      goto ODBC_ERROR;
    }

  where = end;
  where_len = strlen (end);

  cols_type = inline_view + inline_view_len + REWRITE_DELIMITER_CUBLINK_LEN + 2;
  cols_type_len = (end - 2) - (start);
  *(cols_type + cols_type_len) = '\0';

  cols_list = cgw_split_string (cols_type, ",", &src_num_cols);

  if (cols_list == NULL || src_num_cols == 0)
    {
      err_code = ERR_REWRITE_FAILED;
      goto ODBC_ERROR;
    }

  SQL_CHK_ERR (local_odbc_handle->hdbc,
	       SQL_HANDLE_DBC, err_code = SQLAllocHandle (SQL_HANDLE_STMT, local_odbc_handle->hdbc, &hstmt));

  *(inline_view + inline_view_len) = '\0';

  SQL_CHK_ERR (hstmt, SQL_HANDLE_STMT, err_code = SQLPrepare (hstmt, (SQLCHAR *) inline_view, SQL_NTS));

  err_code = cgw_get_num_cols (hstmt, &odbc_num_cols);
  if (err_code < 0)
    {
      err_code = ER_FAILED;
      goto ODBC_ERROR;
    }

  if (odbc_num_cols == 0)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_PT_SEMANTIC, 2, "Invalid column name.", "");
      err_code = ER_FAILED;
      goto ODBC_ERROR;
    }

  if (src_num_cols != odbc_num_cols)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CGW_INVALID_COLUMNS_SPECIFIED, 0);
      err_code = ER_FAILED;
      goto ODBC_ERROR;
    }

  new_inline_view_len += REWRITE_SELECT_LEN;
  new_inline_view_len += (int) (strlen (" AS ") * odbc_num_cols);
  new_inline_view_len += inline_view_len;
  new_inline_view_len += cols_type_len;
  new_inline_view_len += odbc_num_cols;
  new_inline_view_len += REWRITE_FROM_LEN;

  for (int col_num = 1; col_num <= odbc_num_cols; col_num++)
    {
      SQL_CHK_ERR (hstmt,
		   SQL_HANDLE_STMT,
		   err_code = SQLColAttribute (hstmt,
					       col_num,
					       SQL_DESC_NAME, col_name, sizeof (col_name), &col_name_length, NULL));

      new_inline_view_len += col_name_length;
    }

  new_inline_view = (char *) MALLOC (new_inline_view_len + 1);
  strcpy (new_inline_view, "(SELECT ");

  for (int col_num = 1; col_num <= odbc_num_cols; col_num++)
    {
      SQL_CHK_ERR (hstmt,
		   SQL_HANDLE_STMT,
		   err_code = SQLColAttribute (hstmt,
					       col_num,
					       SQL_DESC_NAME, col_name, sizeof (col_name), &col_name_length, NULL));

      if (cols_list[col_num - 1] == NULL || strcmp (cols_list[col_num - 1], "") == 0)
	{
	  err_code = ERR_REWRITE_FAILED;
	  goto ODBC_ERROR;
	}

      strcat (new_inline_view, col_name);
      strcat (new_inline_view, " AS ");
      strcat (new_inline_view, cols_list[col_num - 1]);

      if (col_num <= odbc_num_cols - 1)
	{
	  strcat (new_inline_view, ", ");
	}
      else
	{
	  strcat (new_inline_view, " FROM ");
	  strcat (new_inline_view, inline_view);
	  strcat (new_inline_view, ")");
	}
    }

  rewrite_query = (char *) MALLOC (REWRITE_SELECT_FROM_LEN + new_inline_view_len + where_len + 1);
  memset (rewrite_query, 0x0, REWRITE_SELECT_FROM_LEN + new_inline_view_len + where_len + 1);

  strncpy (rewrite_query, select_from, select_from_len);
  strcat (rewrite_query, " ");
  strcat (rewrite_query, new_inline_view);
  strcat (rewrite_query, " ");
  strcat (rewrite_query, where);

  *sql = rewrite_query;

  if (hstmt)
    {
      SQLFreeHandle (SQL_HANDLE_STMT, hstmt);
    }

  FREE_MEM (source);

  if (cols_list)
    {
      cgw_free_string_array (cols_list);
    }

  if (new_inline_view)
    {
      FREE_MEM (new_inline_view);
    }

  return err_code;

ODBC_ERROR:
  if (hstmt)
    {
      SQLFreeHandle (SQL_HANDLE_STMT, hstmt);
    }

  FREE_MEM (source);

  if (cols_list)
    {
      cgw_free_string_array (cols_list);
    }

  if (new_inline_view)
    {
      FREE_MEM (new_inline_view);
    }

  FREE_MEM (rewrite_query);

  if (err_code == SQL_INVALID_HANDLE)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CGW_INVALID_HANDLE, 0);
      err_code = ER_FAILED;
    }

  return err_code;
}

static void
cgw_free_string_array (char **array)
{
  int i;

  for (i = 0; array[i] != NULL; i++)
    {
      FREE_MEM (array[i]);
    }
  FREE_MEM (array);
}

static char **
cgw_split_string (const char *str, const char *delim, int *num)
{
  char *t, *o;
  char *v;
  char **r = NULL;
  int count = 1;

  if (str == NULL)
    {
      return NULL;
    }

  o = strdup (str);
  if (o == NULL)
    {
      return NULL;
    }

  for (t = o;; t = NULL)
    {
      v = strtok (t, delim);
      if (v == NULL)
	{
	  break;
	}
      char **const realloc_r = (char **) realloc (r, sizeof (char *) * (count + 1));
      if (realloc_r == NULL)
	{
	  FREE_MEM (o);
	  return NULL;
	}
      else
	{
	  r = realloc_r;
	}
      r[count - 1] = strdup (v);
      r[count] = NULL;
      count++;
    }

  *num = (count - 1);
  FREE_MEM (o);
  return r;
}

static int
cgw_unicode_to_utf8 (wchar_t * in_src, int in_size, char **out_target, int *out_length)
{
#if defined(WINDOWS)
  int length;
  unsigned char *in_string = (unsigned char *) in_src;

  if (in_string == NULL || out_length == NULL || out_length == NULL)
    {
      return (-1);
    }

  if (in_size <= 0)
    {
      return (-1);
    }


  length = WideCharToMultiByte (CP_UTF8, 0, in_src, -1, NULL, 0, NULL, NULL);
  if (length <= 0)
    {
      return -1;
    }

  length = WideCharToMultiByte (CP_UTF8, 0, in_src, -1, conv_out_string, length, NULL, NULL);
  if (length <= 0)
    {
      return -1;
    }

  if (out_target)
    {
      *out_target = conv_out_string;
    }

  if (out_length)
    {
      *out_length = length;
    }

#else /* WINDOWS */

  iconv_t cd;
  size_t ret = 0;

  uint16_t *iconv_in = (uint16_t *) in_src;
  char *iconv_out = conv_out_string;

  size_t inlen = in_size;
  size_t outlen_org = CONV_STRING_BUF_SIZE;
  size_t outlen = CONV_STRING_BUF_SIZE;

  if (iconv_in == NULL || out_length == NULL || out_length == NULL)
    {
      return (-1);
    }

  cd = iconv_open (UTF8_CODE_PAGE, UNICODE_CODE_PAGE);
  if (cd == (iconv_t) (-1))
    {
      return -1;
    }

  ret = iconv (cd, (char **) &iconv_in, &inlen, &iconv_out, &outlen);

  if (ret == -1)
    {
      iconv_close (cd);
      return (-1);
    }

  iconv_close (cd);

  if (out_target)
    {
      conv_out_string[outlen_org - outlen] = '\0';
      *out_target = conv_out_string;
    }

  if (out_length)
    {
      *out_length = (outlen_org - outlen) + 1;
    }

#endif
  return 0;
}

static int
cgw_conv_mtow (wchar_t * dest_wc, char *src_mbc)
{
  size_t length;

  if (src_mbc == NULL || dest_wc == NULL)
    {
      return -1;
    }

  length = strlen (src_mbc);
  if (length == 0)
    {
      return -1;
    }

  CONV_M_TO_W (src_mbc, dest_wc, length);

  dest_wc[length] = L'\0';

  return 0;
}

static int
cgw_uint32_to_uni16 (uint32_t i, uint16_t * u)
{
  if (i < 0xffff)
    {
      *u = (uint16_t) (i & 0xffff);
      return 1;
    }

  return 0;
}

static SQLWCHAR *
cgw_wchar_to_sqlwchar (wchar_t * src, size_t len)
{
  SQLWCHAR *dest;
  SQLWCHAR *sqlwchar_string;
  size_t i;

  if (sizeof (wchar_t) == sizeof (SQLWCHAR))
    {
      dest = (SQLWCHAR *) malloc (len * sizeof (SQLWCHAR));
      memcpy (dest, src, len * sizeof (wchar_t));
      return dest;
    }
  else
    {
      dest = (SQLWCHAR *) malloc (2 * len * sizeof (SQLWCHAR));
      sqlwchar_string = dest;

      for (i = 0; i < len; i++)
	{
	  dest += cgw_uint32_to_uni16 ((uint32_t) src[i], (uint16_t *) dest);
	}
      *dest = 0;
      return sqlwchar_string;
    }

  return NULL;
}
