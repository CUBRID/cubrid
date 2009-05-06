/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * - Neither the name of the <ORGANIZATION> nor the names of its contributors
 *   may be used to endorse or promote products derived from this software without
 *   specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 */

#include		<windows.h>
#include		<stdio.h>

#include		"odbc_portable.h"
#include		"sqlext.h"
#include		"cas_cci.h"
#include		"odbc_connection.h"
#include		"odbc_diag_record.h"
#include		"odbc_util.h"
#include		"odbc_type.h"
#include		"odbcinst.h"

#define		SMALL_BUF_SIZE			256

static const char *CUBRID_DRIVER_ODBC_VER = "03.51";
static const char *CUBRID_DRIVER_VER = "01.01.0000";
static const char *CUBRID_DRIVER_NAME = "cubrid_odbc.dll";
static const char *CUBRID_DBMS_NAME = "CUBRID";
static const char *CUBRID_SPECIAL_CHARACTERS = "#%";
static const char *CUBRID_TABLE_TERM = "table";
static const char *CUBRID_KEYWORDS = "";


#define SQL_FUNC_SET(pfExists, uwAPI) \
				( *(((short*) (pfExists)) + ((uwAPI) >> 4)) \
				|= 1 << ((uwAPI) & 0x000F)  )

PRIVATE int get_server_setting (ODBC_CONNECTION * conn);
PRIVATE RETCODE set_isolation_level (ODBC_CONNECTION * conn);
PRIVATE RETCODE get_db_version (ODBC_CONNECTION * conn);

/************************************************************************
* name: odbc_alloc_connection
* arguments:
*		ODBC_ENV *env
*		ODBC_CONNECTION **conptr
* returns/side-effects:
* description:
* NOTE:
************************************************************************/

PUBLIC RETCODE
odbc_alloc_connection (ODBC_ENV * env, ODBC_CONNECTION ** connptr)
{
  ODBC_CONNECTION *conn;

  conn = UT_ALLOC (sizeof (ODBC_CONNECTION));
  if (conn == NULL)
    {
      odbc_set_diag (env->diag, "HY001", 0, NULL);
      *connptr = SQL_NULL_HDBC;
      return ODBC_ERROR;
    }

  memset (conn, 0, sizeof (ODBC_CONNECTION));

  conn->handle_type = SQL_HANDLE_DBC;
  conn->diag = odbc_alloc_diag ();

  /* attribute init */
  conn->attr_access_mode = SQL_MODE_DEFAULT;
  conn->attr_async_enable = SQL_ASYNC_ENABLE_ON;
  conn->attr_auto_ipd = SQL_FALSE;
  conn->attr_autocommit = SQL_AUTOCOMMIT_ON;
  conn->attr_connection_timeout = 0;
  conn->attr_current_catalog = NULL;
  conn->attr_login_timeout = SQL_LOGIN_TIMEOUT_DEFAULT;	/* but... driver-specific */
  conn->attr_odbc_cursors = SQL_CUR_DEFAULT;
  conn->attr_packet_size = 1024;	/* ??? */
  conn->attr_quiet_mode = NULL;	/* NOT implemented */
  conn->attr_trace = SQL_OPT_TRACE_DEFAULT;
  conn->attr_tracefile = UT_MAKE_STRING (SQL_OPT_TRACE_FILE_DEFAULT, -1);
  conn->attr_translate_lib = NULL;
  conn->attr_translate_option = SQL_NULL_DATA;
  //conn->attr_txn_isolation = SQL_TXN_SERIALIZABLE;
  conn->attr_txn_isolation = SQL_TXN_READ_COMMITTED;

  conn->attr_max_rows = 0;
  conn->attr_query_timeout = 0;

  conn->old_txn_isolation = SQL_TXN_READ_COMMITTED;
  conn->fetch_size = 0;

  conn->env = env;
  conn->next = env->conn;
  env->conn = conn;

  *connptr = conn;

  return ODBC_SUCCESS;
}

/************************************************************************
* name: odbc_free_connection
* arguments:
*		ODBC_CONNECTION *con
* returns/side-effects:
*		SQLRETCODE
* description:
* NOTE:
*		statement handle과 explicitly allocated desc handle은
*		SQLDisconnect(odbc_disconnect)시에 free된다.
************************************************************************/

PUBLIC RETCODE
odbc_free_connection (ODBC_CONNECTION * conn)
{
  ODBC_CONNECTION *c, *prev;

  if (conn->env != NULL)
    {
      for (c = conn->env->conn, prev = NULL;
	   c != NULL && c != conn; c = c->next)
	prev = c;
      if (c == conn)
	{
	  if (prev != NULL)
	    prev->next = conn->next;
	  else
	    conn->env->conn = conn->next;
	}
    }

  odbc_free_diag (conn->diag, FREE_ALL);

  NC_FREE (conn->data_source);
  NC_FREE (conn->db_name);
  NC_FREE (conn->server);
  NC_FREE (conn->user);
  NC_FREE (conn->password);
  NC_FREE (conn->attr_tracefile);
  NC_FREE (conn->attr_current_catalog);
  NC_FREE (conn->attr_translate_lib);
  UT_FREE (conn);

  return ODBC_SUCCESS;
}


/************************************************************************
* name: odbc_set_connect_attr
* arguments:
*		ODBC_CONNECTION *con
*		long attribute
*		void* valueptr - generic value pointer
*		long stringlength
* returns/side-effects:
* description:
* NOTE:
*	attribute가 SQL_ATTR_ACCESS_MODE일 경우,
*		내부적으로 isolation level이 TRAN_COMMIT_CLASS_COMMIT_INSTANCE로
*		설정된다.  이 때 기존의 isolation level을 사용하기 위해서
*		(ODBC_CONNECTION).old_txn_isolation이 사용된다.
************************************************************************/
PUBLIC RETCODE
odbc_set_connect_attr (ODBC_CONNECTION * conn,
		       long attribute, void *valueptr, long stringlength)
{

  switch (attribute)
    {
    case SQL_ATTR_ACCESS_MODE:
      switch ((unsigned long) valueptr)
	{
	case SQL_MODE_READ_WRITE:
	  conn->attr_access_mode = (unsigned long) valueptr;
	  conn->attr_txn_isolation = conn->old_txn_isolation;
	  if (set_isolation_level (conn) < 0)
	    {
	      goto error;
	    }
	  break;
	case SQL_MODE_READ_ONLY:
	  conn->attr_access_mode = (unsigned long) valueptr;
	  conn->old_txn_isolation = conn->attr_txn_isolation;
	  conn->attr_txn_isolation = SQL_TXN_READ_COMMITTED;
	  if (set_isolation_level (conn) < 0)
	    {
	      goto error;
	    }
	  break;
	default:
	  /* HY024 - invalid value */
	  odbc_set_diag (conn->diag, "HY024", 0, NULL);
	  goto error;
	  break;
	}
      break;

    case SQL_ATTR_ASYNC_ENABLE:
      switch ((unsigned long) valueptr)
	{
	case SQL_ASYNC_ENABLE_OFF:
	case SQL_ASYNC_ENABLE_ON:
	  conn->attr_async_enable = (unsigned long) valueptr;
	  break;

	default:
	  /* HY024 - invalid value */
	  odbc_set_diag (conn->diag, "HY024", 0, NULL);
	  goto error;
	  break;
	}
      break;

    case SQL_ATTR_AUTO_IPD:
      /* (DM) HY092 - read-only value */
      odbc_set_diag (conn->diag, "HY092", 0, NULL);
      goto error;
      break;

    case SQL_ATTR_AUTOCOMMIT:
      switch ((unsigned long) valueptr)
	{
	case SQL_AUTOCOMMIT_OFF:
	case SQL_AUTOCOMMIT_ON:
	  conn->attr_autocommit = (unsigned long) valueptr;
	  break;
	default:
	  /* HY024 - invalid value */
	  odbc_set_diag (conn->diag, "HY024", 0, NULL);
	  goto error;
	  break;
	}

      break;

    case SQL_ATTR_CONNECTION_DEAD:
      // 아무일도 일어나지 않음. connection 상황은 connection handle의
      // connhd로 부터 알아낼 수 있음.
      break;

    case SQL_ATTR_CONNECTION_TIMEOUT:
      if ((unsigned long) valueptr < 0)
	{
	  odbc_set_diag (conn->diag, "HY024", 0, NULL);
	  goto error;
	}
      conn->attr_connection_timeout = (unsigned long) valueptr;
      break;

    case SQL_ATTR_CURRENT_CATALOG:
      NA_FREE (conn->attr_current_catalog);
      conn->attr_current_catalog = UT_MAKE_STRING (valueptr, stringlength);
      break;

    case SQL_ATTR_LOGIN_TIMEOUT:
      if ((unsigned long) valueptr < 0)
	{
	  /* HY024 - invalid value */
	  odbc_set_diag (conn->diag, "HY024", 0, NULL);
	  goto error;
	}

      conn->attr_login_timeout = (unsigned long) valueptr;
      break;

    case SQL_ATTR_METADATA_ID:
      switch ((unsigned long) valueptr)
	{
	case SQL_TRUE:
	case SQL_FALSE:
	  conn->attr_metadata_id = (unsigned long) valueptr;
	  break;
	default:
	  /* HY024 - invalid value */
	  odbc_set_diag (conn->diag, "HY024", 0, NULL);
	  goto error;
	  break;
	}
      break;

    case SQL_ATTR_ODBC_CURSORS:
      switch ((unsigned long) valueptr)
	{
	case SQL_CUR_USE_IF_NEEDED:
	case SQL_CUR_USE_ODBC:
	case SQL_CUR_USE_DRIVER:
	  conn->attr_odbc_cursors = (unsigned long) valueptr;
	  break;
	default:
	  odbc_set_diag (conn->diag, "HY024", 0, NULL);
	  goto error;
	  break;
	}
      break;

    case SQL_ATTR_PACKET_SIZE:
      if ((unsigned long) valueptr < 0)
	{
	  odbc_set_diag (conn->diag, "HY024", 0, NULL);
	  goto error;
	}
      conn->attr_packet_size = (unsigned long) valueptr;
      break;

    case SQL_ATTR_QUIET_MODE:
      conn->attr_quiet_mode = valueptr;
      break;

    case SQL_ATTR_TRACE:
      switch ((unsigned long) valueptr)
	{
	case SQL_OPT_TRACE_OFF:
	case SQL_OPT_TRACE_ON:
	  conn->attr_trace = (unsigned long) valueptr;
	  break;
	default:
	  /* HY024 - invalid value */
	  odbc_set_diag (conn->diag, "HY024", 0, NULL);
	  goto error;
	  break;
	}

      break;

    case SQL_ATTR_TRACEFILE:
      NC_FREE (conn->attr_tracefile);
      conn->attr_tracefile = UT_MAKE_STRING (valueptr, stringlength);
      break;

    case SQL_ATTR_TRANSLATE_LIB:
      /* HYC00 */
      /*Yet not implemented */
      odbc_set_diag (conn->diag, "HYC00", 0, NULL);
      goto error;

      NC_FREE (conn->attr_translate_lib);
      conn->attr_translate_lib = UT_MAKE_STRING (valueptr, stringlength);
      break;

    case SQL_ATTR_TRANSLATE_OPTION:
      /* HYC00 */
      /*Yet not implemented */
      odbc_set_diag (conn->diag, "HYC00", 0, NULL);
      goto error;
      break;

    case SQL_ATTR_TXN_ISOLATION:
      conn->attr_txn_isolation = (unsigned long) valueptr;
      if (set_isolation_level (conn) < 0)
	{
	  goto error;
	}
      break;

      // stmt attributes
    case SQL_ATTR_MAX_ROWS:
      conn->attr_max_rows = (unsigned long) valueptr;
      break;

    case SQL_ATTR_QUERY_TIMEOUT:
      conn->attr_query_timeout = (unsigned long) valueptr;
      break;

    default:
      /* (DM) HY092 - */
      odbc_set_diag (conn->diag, "HY092", 0, NULL);
      goto error;
      break;
    }

  return ODBC_SUCCESS;

error:
  return ODBC_ERROR;
}

/************************************************************************
* name: odbc_get_connect_attr
* arguments:
* returns/side-effects:
* description:
* NOTE:
************************************************************************/
PUBLIC RETCODE
odbc_get_connect_attr (ODBC_CONNECTION * conn,
		       SQLINTEGER attribute,
		       SQLPOINTER value_ptr,
		       SQLINTEGER buffer_length, SQLINTEGER *string_length_ptr)
{
  RETCODE rc = ODBC_SUCCESS;
  SQLLEN tmp_length;

  switch (attribute)
    {
    case SQL_ATTR_ACCESS_MODE:
      if (value_ptr != NULL)
	*((unsigned long *) value_ptr) = conn->attr_access_mode;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;

    case SQL_ATTR_ASYNC_ENABLE:
      if (value_ptr != NULL)
	*((unsigned long *) value_ptr) = conn->attr_async_enable;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;

    case SQL_ATTR_AUTO_IPD:
      /* HYC00 */
      /* Yet not implemented */
      odbc_set_diag (conn->diag, "HYC00", 0, NULL);
      goto error;

      if (value_ptr != NULL)
	*((unsigned long *) value_ptr) = conn->attr_auto_ipd;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;

    case SQL_ATTR_AUTOCOMMIT:
      if (value_ptr != NULL)
	*((unsigned long *) value_ptr) = conn->attr_autocommit;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;

    case SQL_ATTR_CONNECTION_DEAD:
      if (value_ptr != NULL)
	{
	  if (conn->connhd > 0)
	    {
	      *((unsigned long *) value_ptr) = SQL_CD_FALSE;
	    }
	  else
	    {
	      *((unsigned long *) value_ptr) = SQL_CD_TRUE;
	    }
	}

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;

    case SQL_ATTR_CONNECTION_TIMEOUT:
      /* HYC00 */
      /* Yet not implemented */
      odbc_set_diag (conn->diag, "HYC00", 0, NULL);
      goto error;

      if (value_ptr != NULL)
	*((unsigned long *) value_ptr) = conn->attr_connection_timeout;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;

    case SQL_ATTR_CURRENT_CATALOG:

      /* CHECK : test */
      rc =
	str_value_assign (conn->data_source, value_ptr, buffer_length,
			  &tmp_length);
      *string_length_ptr = (SQLINTEGER) tmp_length;

      if (rc == ODBC_SUCCESS_WITH_INFO)
	{
	  odbc_set_diag (conn->diag, "01004", 0, NULL);
	}
      break;

      /* HYC00 */
      /* Yet not implemented */
      odbc_set_diag (conn->diag, "HYC00", 0, NULL);
      goto error;

      rc =
	str_value_assign (conn->attr_current_catalog, value_ptr,
			  buffer_length, &tmp_length);
      *string_length_ptr = (SQLINTEGER) tmp_length;

      if (rc == ODBC_SUCCESS_WITH_INFO)
	{
	  odbc_set_diag (conn->diag, "01004", 0, NULL);
	}

      break;

    case SQL_ATTR_LOGIN_TIMEOUT:
      /* HYC00 */
      /* Yet not implemented */
      odbc_set_diag (conn->diag, "HYC00", 0, NULL);
      goto error;

      if (value_ptr != NULL)
	*((unsigned long *) value_ptr) = conn->attr_login_timeout;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);

      break;

    case SQL_ATTR_METADATA_ID:
      if (value_ptr != NULL)
	*((unsigned long *) value_ptr) = conn->attr_metadata_id;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);

      break;

    case SQL_ATTR_ODBC_CURSORS:
      /* HYC00 */
      /* Yet not implemented */
      odbc_set_diag (conn->diag, "HYC00", 0, NULL);
      goto error;

      if (value_ptr != NULL)
	*((unsigned long *) value_ptr) = conn->attr_odbc_cursors;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);

      break;

    case SQL_ATTR_PACKET_SIZE:
      /* HYC00 */
      /* Yet not implemented */
      odbc_set_diag (conn->diag, "HYC00", 0, NULL);
      goto error;

      if (value_ptr != NULL)
	*((unsigned long *) value_ptr) = conn->attr_packet_size;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);

      break;

    case SQL_ATTR_QUIET_MODE:
      if (value_ptr != NULL)
	*((void **) value_ptr) = conn->attr_quiet_mode;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (void *);
      break;

    case SQL_ATTR_TRACE:
      if (value_ptr != NULL)
	*((unsigned long *) value_ptr) = conn->attr_trace;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);

      break;

    case SQL_ATTR_TRACEFILE:
      rc =
	str_value_assign (conn->attr_tracefile, value_ptr, buffer_length,
			  &tmp_length);
      *string_length_ptr = (SQLINTEGER) tmp_length;

      if (rc == ODBC_SUCCESS_WITH_INFO)
	{
	  odbc_set_diag (conn->diag, "01004", 0, NULL);
	}
      break;

    case SQL_ATTR_TRANSLATE_LIB:
      /* HYC00 */
      /* Yet not implemented */
      odbc_set_diag (conn->diag, "HYC00", 0, NULL);
      goto error;

      rc =
	str_value_assign (conn->attr_translate_lib, value_ptr, buffer_length,
			  &tmp_length);
      *string_length_ptr = (SQLINTEGER) tmp_length;

      if (rc == ODBC_SUCCESS_WITH_INFO)
	{
	  odbc_set_diag (conn->diag, "01004", 0, NULL);
	}
      break;

    case SQL_ATTR_TRANSLATE_OPTION:
      /* HYC00 */
      /* Yet not implemented */
      odbc_set_diag (conn->diag, "HYC00", 0, NULL);
      goto error;

    case SQL_ATTR_TXN_ISOLATION:
      if (value_ptr != NULL)
	*(unsigned long *) value_ptr = conn->attr_txn_isolation;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);

      break;

      // stmt attributes
    case SQL_ATTR_MAX_ROWS:
      if (value_ptr != NULL)
	*(unsigned long *) value_ptr = conn->attr_max_rows;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;

    case SQL_ATTR_QUERY_TIMEOUT:
      if (value_ptr != NULL)
	*(unsigned long *) value_ptr = conn->attr_query_timeout;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;

    default:
      /* (DM) HY092 - */
      odbc_set_diag (conn->diag, "HY092", 0, NULL);
      goto error;
    }

  return rc;

error:
  return ODBC_ERROR;
}


/************************************************************************
* name: odbc_connect
* arguments:
* returns/side-effects:
* description:
* NOTE:
* CHECK : error check
************************************************************************/
PUBLIC RETCODE
odbc_connect (ODBC_CONNECTION * conn,
	      const char *data_source, const char *user, const char *password)
{
  const char *odbc_state = "";
  int connhd;
  RETCODE rc;
  int cci_rc;
  T_CCI_ERROR cci_err_buf;

  conn->data_source = UT_MAKE_STRING (data_source, -1);
  conn->user = UT_MAKE_STRING (user, -1);
  conn->password = UT_MAKE_STRING (password, -1);

  get_server_setting (conn);

  connhd = cci_connect (conn->server, conn->port, conn->db_name,
			conn->user, conn->password);

  if (connhd < 0)
    goto error;

  conn->connhd = connhd;

  rc = get_db_version (conn);
  ERROR_GOTO (rc, error);

  // disconnect with cas
  if (conn->connhd > 0)
    {
      cci_rc = cci_end_tran (conn->connhd, CCI_TRAN_ROLLBACK, &cci_err_buf);
      if (cci_rc < 0)
	{
	  odbc_set_diag_by_cci (conn->diag, cci_rc, cci_err_buf.err_msg);
	  return ODBC_ERROR;
	}
    }

  /*
     rc = set_isolation_level(conn);
     ERROR_GOTO(rc, error);
   */

  return ODBC_SUCCESS;

error:
  // CHECK : error messaging
  return ODBC_ERROR;
}

/************************************************************************
* name: odbc_connect_by_filedsn
* arguments:
* returns/side-effects:
* description:
* NOTE:
* CHECK : error check
************************************************************************/
PUBLIC RETCODE
odbc_connect_by_filedsn (ODBC_CONNECTION * conn,
			 const char *file_dsn,
			 const char *db_name,
			 const char *user,
			 const char *password,
			 const char *server, const char *port)
{
  int connhd;
  RETCODE rc;
  int cci_rc;
  T_CCI_ERROR cci_err_buf;

  conn->data_source = UT_MAKE_STRING (file_dsn, -1);
  conn->db_name = UT_MAKE_STRING (db_name, -1);
  conn->user = UT_MAKE_STRING (user, -1);
  conn->password = UT_MAKE_STRING (password, -1);
  conn->server = UT_MAKE_STRING (server, -1);
  conn->port = atoi (port);

  connhd = cci_connect (conn->server, conn->port, conn->db_name,
			conn->user, conn->password);

  if (connhd < 0)
    goto error;

  conn->connhd = connhd;

  rc = get_db_version (conn);
  ERROR_GOTO (rc, error);

  // disconnect with cas
  if (conn->connhd > 0)
    {
      cci_rc = cci_end_tran (conn->connhd, CCI_TRAN_ROLLBACK, &cci_err_buf);
      if (cci_rc < 0)
	{
	  odbc_set_diag_by_cci (conn->diag, cci_rc, cci_err_buf.err_msg);
	  return ODBC_ERROR;
	}
    }

  /*
     rc = set_isolation_level(conn);
     ERROR_GOTO(rc, error);
   */

  return ODBC_SUCCESS;

error:
  // CHECK : error messaging
  return ODBC_ERROR;
}

/************************************************************************
* name: odbc_connect_new
* arguments:
* returns/side-effects:
* description:
* NOTE:
* CHECK : error check
************************************************************************/
PUBLIC RETCODE
odbc_connect_new (ODBC_CONNECTION * conn,
		  const char *data_source,
		  const char *db_name,
		  const char *user,
		  const char *password,
		  const char *server, int port, int fetch_size)
{
  int connhd;
  RETCODE rc;
  int cci_rc;
  T_CCI_ERROR cci_err_buf;
  char *pt;

  NA_FREE (conn->data_source);
  NA_FREE (conn->db_name);
  NA_FREE (conn->user);
  NA_FREE (conn->password);
  NA_FREE (conn->server);

  pt = data_source == NULL ? "" : data_source;
  conn->data_source = UT_MAKE_STRING (pt, -1);

  pt = db_name == NULL ? "" : db_name;
  conn->db_name = UT_MAKE_STRING (pt, -1);

  pt = user == NULL ? "" : user;
  conn->user = UT_MAKE_STRING (pt, -1);

  pt = password == NULL ? "" : password;
  conn->password = UT_MAKE_STRING (pt, -1);

  pt = server == NULL ? "" : server;
  conn->server = UT_MAKE_STRING (pt, -1);

  if (port > 0)
    {
      conn->port = port;
    }
  if (fetch_size > 0)
    {
      conn->fetch_size = fetch_size;
    }

  connhd = cci_connect (conn->server, conn->port, conn->db_name,
			conn->user, conn->password);

  if (connhd < 0)
    goto error;

  conn->connhd = connhd;

  rc = get_db_version (conn);
  ERROR_GOTO (rc, error);

  // disconnect with cas
  if (conn->connhd > 0)
    {
      cci_rc = cci_end_tran (conn->connhd, CCI_TRAN_ROLLBACK, &cci_err_buf);
      if (cci_rc < 0)
	{
	  odbc_set_diag_by_cci (conn->diag, cci_rc, cci_err_buf.err_msg);
	  return ODBC_ERROR;
	}
    }

  /*
     rc = set_isolation_level(conn);
     ERROR_GOTO(rc, error);
   */

  return ODBC_SUCCESS;

error:
  // CHECK : error messaging
  return ODBC_ERROR;
}

/************************************************************************
 * name: odbc_disconnect
 * arguments:
 * returns/side-effects:
 * description:
 *		Data source(CUBRID의 경우 CAS)와의 연결을 끊고, connection handle
 *		에 딸려있는 statement handle, descriptor handle을 free한다.
 * NOTE:
 ************************************************************************/
PUBLIC RETCODE
odbc_disconnect (ODBC_CONNECTION * conn)
{
  int cci_rc;
  T_CCI_ERROR cci_err_buf;
  ODBC_STATEMENT *stmt;
  ODBC_STATEMENT *del_stmt;
  ODBC_DESC *desc;
  ODBC_DESC *del_desc;

  if (conn->connhd <= 0)
    {
      odbc_set_diag (conn->diag, "08003", 0, NULL);
      goto error;
    }

  cci_rc = cci_disconnect (conn->connhd, &cci_err_buf);
  if (cci_rc < 0)
    goto error;

  conn->connhd = -1;

  for (stmt = conn->statements; stmt;)
    {
      del_stmt = stmt;
      stmt = stmt->next;
      odbc_free_statement (del_stmt);
      del_stmt = NULL;
    }

  for (desc = conn->descriptors; desc;)
    {
      del_desc = desc;
      desc = desc->next;
      odbc_free_desc (del_desc);
      del_desc = NULL;
    }

  return ODBC_SUCCESS;

error:
  odbc_set_diag_by_cci (conn->diag, cci_rc, cci_err_buf.err_msg);
  return ODBC_ERROR;
}



/************************************************************************
 * name: odbc_auto_commit
 * arguments:
 * returns/side-effects:
 * description:
 * auto commit mode가 ON이면 end tran을 실행한다.
 * NOTE:
 ************************************************************************/
PUBLIC RETCODE
odbc_auto_commit (ODBC_CONNECTION * conn)
{
  int cci_rc;
  T_CCI_ERROR cci_err_buf;

  if (conn->attr_autocommit == SQL_AUTOCOMMIT_ON)
    {
      if (conn->connhd > 0)
	{
	  cci_rc = cci_end_tran (conn->connhd, CCI_TRAN_COMMIT, &cci_err_buf);
	  if (cci_rc < 0)
	    {
	      odbc_set_diag_by_cci (conn->diag, cci_rc, cci_err_buf.err_msg);
	      return ODBC_ERROR;
	    }
	}
    }
  return ODBC_SUCCESS;
}


/************************************************************************
 * name: odbc_native_sql
 * arguments:
 * returns/side-effects:
 * description:
 *	input stmt text를 return 해준다. ODBC SQL to CUBRID SQL의 처리과정이 없다.
 * NOTE:
 ************************************************************************/
PUBLIC RETCODE
odbc_native_sql (ODBC_CONNECTION * conn,
		 SQLCHAR *in_stmt_text,
		 SQLCHAR *out_stmt_text,
		 SQLINTEGER buffer_length, SQLINTEGER *out_stmt_length)
{
  RETCODE rc = ODBC_SUCCESS;
  SQLLEN tmp_length;

  rc = str_value_assign (in_stmt_text, out_stmt_text,
			 buffer_length, &tmp_length);
  *out_stmt_length = (SQLINTEGER) tmp_length;

  if (rc == ODBC_SUCCESS_WITH_INFO)
    {
      odbc_set_diag (conn->diag, "01004", 0, NULL);
    }

  return rc;
}


/************************************************************************
* name: odbc_get_functions
* arguments:
* returns/side-effects:
* description:
* NOTE:
************************************************************************/
PUBLIC RETCODE
odbc_get_functions (ODBC_CONNECTION * conn,
		    unsigned short function_id, unsigned short *supported_ptr)
{

  switch (function_id)
    {
    case SQL_API_ALL_FUNCTIONS:
      /* Yet not implemented */
      memset (supported_ptr, 0, 100);
      break;
    case SQL_API_ODBC3_ALL_FUNCTIONS:
      memset (supported_ptr, 0, SQL_API_ODBC3_ALL_FUNCTIONS_SIZE);

      SQL_FUNC_SET (supported_ptr, SQL_API_SQLALLOCHANDLE);
      SQL_FUNC_SET (supported_ptr, SQL_API_SQLBINDCOL);
      SQL_FUNC_SET (supported_ptr, SQL_API_SQLBINDPARAMETER);
      SQL_FUNC_SET (supported_ptr, SQL_API_SQLCANCEL);
      SQL_FUNC_SET (supported_ptr, SQL_API_SQLCLOSECURSOR);
      SQL_FUNC_SET (supported_ptr, SQL_API_SQLCOLATTRIBUTE);
      SQL_FUNC_SET (supported_ptr, SQL_API_SQLCOLATTRIBUTES);
      SQL_FUNC_SET (supported_ptr, SQL_API_SQLCONNECT);
      SQL_FUNC_SET (supported_ptr, SQL_API_SQLCOPYDESC);
      // (DM) SQL_FUNC_SET(supported_ptr,SQL_API_SQLDATASOURCES );
      // (DM) SQL_FUNC_SET(supported_ptr,SQL_API_SQLDRIVERS );
      SQL_FUNC_SET (supported_ptr, SQL_API_SQLDESCRIBECOL);
      SQL_FUNC_SET (supported_ptr, SQL_API_SQLDISCONNECT);
      SQL_FUNC_SET (supported_ptr, SQL_API_SQLDRIVERCONNECT);
      SQL_FUNC_SET (supported_ptr, SQL_API_SQLENDTRAN);
      SQL_FUNC_SET (supported_ptr, SQL_API_SQLEXECDIRECT);
      SQL_FUNC_SET (supported_ptr, SQL_API_SQLEXECUTE);
      SQL_FUNC_SET (supported_ptr, SQL_API_SQLEXTENDEDFETCH);	// for 2.x backward compatibility
      SQL_FUNC_SET (supported_ptr, SQL_API_SQLFETCH);
      SQL_FUNC_SET (supported_ptr, SQL_API_SQLFETCHSCROLL);
      SQL_FUNC_SET (supported_ptr, SQL_API_SQLFREEHANDLE);
      SQL_FUNC_SET (supported_ptr, SQL_API_SQLFREESTMT);
      SQL_FUNC_SET (supported_ptr, SQL_API_SQLGETCONNECTATTR);
      SQL_FUNC_SET (supported_ptr, SQL_API_SQLGETCURSORNAME);
      SQL_FUNC_SET (supported_ptr, SQL_API_SQLGETDATA);
      SQL_FUNC_SET (supported_ptr, SQL_API_SQLGETDESCFIELD);
      SQL_FUNC_SET (supported_ptr, SQL_API_SQLGETDESCREC);
      SQL_FUNC_SET (supported_ptr, SQL_API_SQLGETDIAGFIELD);
      SQL_FUNC_SET (supported_ptr, SQL_API_SQLGETDIAGREC);
      SQL_FUNC_SET (supported_ptr, SQL_API_SQLGETENVATTR);
      SQL_FUNC_SET (supported_ptr, SQL_API_SQLGETFUNCTIONS);
      SQL_FUNC_SET (supported_ptr, SQL_API_SQLGETINFO);
      SQL_FUNC_SET (supported_ptr, SQL_API_SQLGETSTMTATTR);
      SQL_FUNC_SET (supported_ptr, SQL_API_SQLGETTYPEINFO);
      SQL_FUNC_SET (supported_ptr, SQL_API_SQLNUMPARAMS);
      SQL_FUNC_SET (supported_ptr, SQL_API_SQLNUMRESULTCOLS);
      SQL_FUNC_SET (supported_ptr, SQL_API_SQLPARAMDATA);
      SQL_FUNC_SET (supported_ptr, SQL_API_SQLPREPARE);
      SQL_FUNC_SET (supported_ptr, SQL_API_SQLPUTDATA);
      SQL_FUNC_SET (supported_ptr, SQL_API_SQLROWCOUNT);
      SQL_FUNC_SET (supported_ptr, SQL_API_SQLSETCONNECTATTR);
      SQL_FUNC_SET (supported_ptr, SQL_API_SQLSETCURSORNAME);
      SQL_FUNC_SET (supported_ptr, SQL_API_SQLSETDESCFIELD);
      SQL_FUNC_SET (supported_ptr, SQL_API_SQLSETDESCREC);
      SQL_FUNC_SET (supported_ptr, SQL_API_SQLSETENVATTR);
      SQL_FUNC_SET (supported_ptr, SQL_API_SQLSETSTMTATTR);
      SQL_FUNC_SET (supported_ptr, SQL_API_SQLCOLUMNS);
      SQL_FUNC_SET (supported_ptr, SQL_API_SQLSPECIALCOLUMNS);
      SQL_FUNC_SET (supported_ptr, SQL_API_SQLTABLES);
      SQL_FUNC_SET (supported_ptr, SQL_API_SQLTRANSACT);
      SQL_FUNC_SET (supported_ptr, SQL_API_SQLSTATISTICS);
      SQL_FUNC_SET (supported_ptr, SQL_API_SQLNATIVESQL);
      SQL_FUNC_SET (supported_ptr, SQL_API_SQLDRIVERCONNECT);

      SQL_FUNC_SET (supported_ptr, SQL_API_SQLMORERESULTS);
      SQL_FUNC_SET (supported_ptr, SQL_API_SQLBULKOPERATIONS);
      SQL_FUNC_SET (supported_ptr, SQL_API_SQLSETPOS);

      // Not supported
      //SQL_FUNC_SET(supported_ptr,SQL_API_SQLBROWSECONNECT );
      //SQL_FUNC_SET(supported_ptr,SQL_API_SQLCOLUMNPRIVILEGES );
      //SQL_FUNC_SET(supported_ptr,SQL_API_SQLDESCRIBEPARAM );
      //SQL_FUNC_SET(supported_ptr,SQL_API_SQLFOREIGNKEYS );
      //SQL_FUNC_SET(supported_ptr,SQL_API_SQLPRIMARYKEYS );
      //SQL_FUNC_SET(supported_ptr,SQL_API_SQLPROCEDURECOLUMNS );
      //SQL_FUNC_SET(supported_ptr,SQL_API_SQLPROCEDURES );

      //SQL_FUNC_SET(supported_ptr,SQL_API_SQLTABLEPRIVILEGES );
      break;

    case SQL_API_SQLALLOCHANDLE:
      *supported_ptr = SQL_TRUE;
      break;
    case SQL_API_SQLBINDCOL:
      *supported_ptr = SQL_TRUE;
      break;
    case SQL_API_SQLCANCEL:
      *supported_ptr = SQL_TRUE;
      break;
    case SQL_API_SQLCLOSECURSOR:
      *supported_ptr = SQL_TRUE;
      break;
    case SQL_API_SQLCOLATTRIBUTE:
      *supported_ptr = SQL_TRUE;
      break;
    case SQL_API_SQLCONNECT:
      *supported_ptr = SQL_TRUE;
      break;
    case SQL_API_SQLCOPYDESC:
      *supported_ptr = SQL_TRUE;
      break;
    case SQL_API_SQLDATASOURCES:
      *supported_ptr = SQL_TRUE;
      break;
    case SQL_API_SQLDESCRIBECOL:
      *supported_ptr = SQL_TRUE;
      break;
    case SQL_API_SQLDISCONNECT:
      *supported_ptr = SQL_TRUE;
      break;
    case SQL_API_SQLDRIVERS:
      *supported_ptr = SQL_TRUE;
      break;
    case SQL_API_SQLENDTRAN:
      *supported_ptr = SQL_TRUE;
      break;
    case SQL_API_SQLEXECDIRECT:
      *supported_ptr = SQL_TRUE;
      break;
    case SQL_API_SQLEXECUTE:
      *supported_ptr = SQL_TRUE;
      break;
    case SQL_API_SQLEXTENDEDFETCH:	// for 2.x backward compatibility
      *supported_ptr = SQL_TRUE;
      *supported_ptr = SQL_FALSE;
      break;
    case SQL_API_SQLFETCH:
      *supported_ptr = SQL_TRUE;
      break;
    case SQL_API_SQLFETCHSCROLL:
      *supported_ptr = SQL_TRUE;
      break;
    case SQL_API_SQLFREEHANDLE:
      *supported_ptr = SQL_TRUE;
      break;
    case SQL_API_SQLFREESTMT:
      *supported_ptr = SQL_TRUE;
      break;
    case SQL_API_SQLGETCONNECTATTR:
      *supported_ptr = SQL_TRUE;
      break;
    case SQL_API_SQLGETCURSORNAME:
      *supported_ptr = SQL_TRUE;
      break;
    case SQL_API_SQLGETDATA:
      *supported_ptr = SQL_TRUE;
      break;
    case SQL_API_SQLGETDESCFIELD:
      *supported_ptr = SQL_TRUE;
      break;
    case SQL_API_SQLGETDESCREC:
      *supported_ptr = SQL_TRUE;
      break;
    case SQL_API_SQLGETDIAGFIELD:
      *supported_ptr = SQL_TRUE;
      break;
    case SQL_API_SQLGETDIAGREC:
      *supported_ptr = SQL_TRUE;
      break;
    case SQL_API_SQLGETENVATTR:
      *supported_ptr = SQL_TRUE;
      break;
    case SQL_API_SQLGETFUNCTIONS:
      *supported_ptr = SQL_TRUE;
      break;
    case SQL_API_SQLGETINFO:
      *supported_ptr = SQL_TRUE;
      break;
    case SQL_API_SQLGETSTMTATTR:
      *supported_ptr = SQL_TRUE;
      break;
    case SQL_API_SQLGETTYPEINFO:
      *supported_ptr = SQL_TRUE;
      break;
    case SQL_API_SQLNUMRESULTCOLS:
      *supported_ptr = SQL_TRUE;
      break;
    case SQL_API_SQLPARAMDATA:
      *supported_ptr = SQL_TRUE;
      break;
    case SQL_API_SQLPREPARE:
      *supported_ptr = SQL_TRUE;
      break;
    case SQL_API_SQLPUTDATA:
      *supported_ptr = SQL_TRUE;
      break;
    case SQL_API_SQLROWCOUNT:
      *supported_ptr = SQL_TRUE;
      break;
    case SQL_API_SQLSETCONNECTATTR:
      *supported_ptr = SQL_TRUE;
      break;
    case SQL_API_SQLSETCURSORNAME:
      *supported_ptr = SQL_TRUE;
      break;
    case SQL_API_SQLSETDESCFIELD:
      *supported_ptr = SQL_TRUE;
      break;
    case SQL_API_SQLSETDESCREC:
      *supported_ptr = SQL_TRUE;
      break;
    case SQL_API_SQLSETENVATTR:
      *supported_ptr = SQL_TRUE;
      break;
    case SQL_API_SQLSETSTMTATTR:
      *supported_ptr = SQL_TRUE;
      break;
    case SQL_API_SQLCOLUMNS:
      *supported_ptr = SQL_TRUE;
      break;
    case SQL_API_SQLSPECIALCOLUMNS:
      *supported_ptr = SQL_TRUE;
      break;
    case SQL_API_SQLTABLES:
      *supported_ptr = SQL_TRUE;
      break;
    case SQL_API_SQLSTATISTICS:
      *supported_ptr = SQL_TRUE;
      break;

    case SQL_API_SQLBINDPARAMETER:
      *supported_ptr = SQL_TRUE;
      break;
    case SQL_API_SQLDRIVERCONNECT:
      *supported_ptr = SQL_TRUE;
      break;
    case SQL_API_SQLNATIVESQL:
      *supported_ptr = SQL_TRUE;
      break;
    case SQL_API_SQLNUMPARAMS:
      *supported_ptr = SQL_TRUE;
      break;

    case SQL_API_SQLMORERESULTS:
      *supported_ptr = SQL_TRUE;
      break;
    case SQL_API_SQLBULKOPERATIONS:
      *supported_ptr = SQL_TRUE;
      break;
    case SQL_API_SQLSETPOS:
      *supported_ptr = SQL_TRUE;
      break;

      // FALSE
    case SQL_API_SQLBROWSECONNECT:
      *supported_ptr = SQL_FALSE;
      break;
    case SQL_API_SQLCOLUMNPRIVILEGES:
      *supported_ptr = SQL_FALSE;
      break;
    case SQL_API_SQLDESCRIBEPARAM:
      *supported_ptr = SQL_FALSE;
      break;
    case SQL_API_SQLFOREIGNKEYS:
      *supported_ptr = SQL_FALSE;
      break;

    case SQL_API_SQLPRIMARYKEYS:
      *supported_ptr = SQL_FALSE;
      break;
    case SQL_API_SQLPROCEDURECOLUMNS:
      *supported_ptr = SQL_FALSE;
      break;
    case SQL_API_SQLPROCEDURES:
      *supported_ptr = SQL_FALSE;
      break;

    case SQL_API_SQLTABLEPRIVILEGES:
      *supported_ptr = SQL_FALSE;
      break;
    default:
      *supported_ptr = SQL_FALSE;
      break;
    }
  return ODBC_SUCCESS;
}


/************************************************************************
* name: odbc_get_info
* arguments:
* returns/side-effects:
* description:
* NOTE:
************************************************************************/
PUBLIC RETCODE
odbc_get_info (ODBC_CONNECTION * conn,
	       SQLUSMALLINT info_type,
	       SQLPOINTER info_value_ptr,
	       SQLSMALLINT buffer_length, SQLLEN *string_length_ptr)
{
  RETCODE rc = ODBC_SUCCESS;
  char buf[1024];

  switch (info_type)
    {

      /* Character String - "Y" or "N" */
    case SQL_ACCESSIBLE_PROCEDURES:
      rc =
	str_value_assign ("N", info_value_ptr, buffer_length,
			  string_length_ptr);
      if (rc == ODBC_SUCCESS_WITH_INFO)
	{
	  odbc_set_diag (conn->diag, "01004", 0, NULL);
	}
      break;

    case SQL_ACCESSIBLE_TABLES:
      rc =
	str_value_assign ("Y", info_value_ptr, buffer_length,
			  string_length_ptr);
      if (rc == ODBC_SUCCESS_WITH_INFO)
	{
	  odbc_set_diag (conn->diag, "01004", 0, NULL);
	}
      break;

    case SQL_ACTIVE_ENVIRONMENTS:
      if (info_value_ptr != NULL)
	*(unsigned short *) info_value_ptr = 0;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned short);

      break;

    case SQL_AGGREGATE_FUNCTIONS:
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr =
	  SQL_AF_ALL | SQL_AF_AVG | SQL_AF_COUNT | SQL_AF_DISTINCT |
	  SQL_AF_MAX | SQL_AF_MIN | SQL_AF_SUM;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);

      break;

    case SQL_ALTER_DOMAIN:
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr = SQL_AD_ADD_DOMAIN_CONSTRAINT |
	  SQL_AD_ADD_DOMAIN_DEFAULT | SQL_AD_DROP_DOMAIN_CONSTRAINT;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;

    case SQL_ALTER_TABLE:	/* SQL_AT_DROP_COLUMN  - ODBC 2.0 */
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr =
	  SQL_AT_ADD_COLUMN | SQL_AT_ADD_COLUMN_DEFAULT |
	  SQL_AT_ADD_COLUMN_SINGLE | SQL_AT_ADD_CONSTRAINT |
	  SQL_AT_ADD_TABLE_CONSTRAINT | SQL_AT_DROP_COLUMN;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;

    case SQL_ASYNC_MODE:
      if (info_value_ptr != NULL)
	/* FIXME :  SQL_AM_STATEMENT , SQL_AM_CONNECTION 둘 중에 하나로
	 * fix해야 한다. */
	*(unsigned long *) info_value_ptr = SQL_AM_STATEMENT;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;

    case SQL_BATCH_ROW_COUNT:
      /* CHECK : YET Not supported */
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr = 0;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;

    case SQL_BATCH_SUPPORT:
      /* CHECK : YET Not supported */
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr = 0;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;

    case SQL_BOOKMARK_PERSISTENCE:
      /* CHECK : Never supported */
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr = 0;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;

    case SQL_CATALOG_LOCATION:
      if (info_value_ptr != NULL)
	*(unsigned short *) info_value_ptr = 0;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned short);
      break;

    case SQL_CATALOG_NAME:
      rc =
	str_value_assign ("N", info_value_ptr, buffer_length,
			  string_length_ptr);
      if (rc == ODBC_SUCCESS_WITH_INFO)
	{
	  odbc_set_diag (conn->diag, "01004", 0, NULL);
	}
      break;

    case SQL_CATALOG_NAME_SEPARATOR:
      rc =
	str_value_assign ("", info_value_ptr, buffer_length,
			  string_length_ptr);
      if (rc == ODBC_SUCCESS_WITH_INFO)
	{
	  odbc_set_diag (conn->diag, "01004", 0, NULL);
	}
      break;

    case SQL_CATALOG_TERM:
      rc =
	str_value_assign ("", info_value_ptr, buffer_length,
			  string_length_ptr);
      if (rc == ODBC_SUCCESS_WITH_INFO)
	{
	  odbc_set_diag (conn->diag, "01004", 0, NULL);
	}
      break;

    case SQL_CATALOG_USAGE:
      /* CHECK : Never supported */
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr = 0;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;

    case SQL_COLLATION_SEQ:
      rc =
	str_value_assign ("", info_value_ptr, buffer_length,
			  string_length_ptr);
      if (rc == ODBC_SUCCESS_WITH_INFO)
	{
	  odbc_set_diag (conn->diag, "01004", 0, NULL);
	}
      break;

    case SQL_COLUMN_ALIAS:
      rc =
	str_value_assign ("N", info_value_ptr, buffer_length,
			  string_length_ptr);
      if (rc == ODBC_SUCCESS_WITH_INFO)
	{
	  odbc_set_diag (conn->diag, "01004", 0, NULL);
	}
      break;

    case SQL_CONCAT_NULL_BEHAVIOR:
      if (info_value_ptr != NULL)
	*(unsigned short *) info_value_ptr = SQL_CB_NULL;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned short);
      break;

    case SQL_CONVERT_BIGINT:
      if (info_value_ptr != NULL)
        *(unsigned long *) info_value_ptr = SQL_CVT_DECIMAL | SQL_CVT_DOUBLE |
                SQL_CVT_FLOAT | SQL_CVT_INTEGER | SQL_CVT_NUMERIC | SQL_CVT_REAL |
                SQL_CVT_SMALLINT | SQL_CVT_CHAR | SQL_CVT_LONGVARCHAR |
                SQL_CVT_VARCHAR | SQL_CONVERT_BIGINT;
      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;
    case SQL_CONVERT_BINARY:
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr =
	  SQL_CVT_BINARY | SQL_CVT_LONGVARBINARY | SQL_CVT_VARBINARY |
	  SQL_CVT_CHAR | SQL_CVT_VARCHAR | SQL_CVT_LONGVARCHAR;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;
    case SQL_CONVERT_BIT:
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr = 0;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;
    case SQL_CONVERT_CHAR:
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr = SQL_CVT_BINARY | SQL_CVT_CHAR |
	  SQL_CVT_DATE | SQL_CVT_DECIMAL | SQL_CVT_DOUBLE | SQL_CVT_FLOAT |
	  SQL_CVT_INTEGER | SQL_CVT_LONGVARBINARY | SQL_CVT_LONGVARCHAR |
	  SQL_CVT_NUMERIC | SQL_CVT_REAL | SQL_CVT_SMALLINT | SQL_CVT_TIME |
	  SQL_CVT_TIMESTAMP | SQL_CVT_VARBINARY | SQL_CVT_VARCHAR;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;
    case SQL_CONVERT_DATE:
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr =
	  SQL_CVT_CHAR | SQL_CVT_LONGVARCHAR | SQL_CVT_VARCHAR | SQL_CVT_DATE
	  | SQL_CVT_TIMESTAMP;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;
    case SQL_CONVERT_DECIMAL:
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr = SQL_CVT_DECIMAL | SQL_CVT_DOUBLE |
	  SQL_CVT_FLOAT | SQL_CVT_INTEGER | SQL_CVT_NUMERIC | SQL_CVT_REAL |
	  SQL_CVT_SMALLINT | SQL_CVT_CHAR | SQL_CVT_LONGVARCHAR |
	  SQL_CVT_VARCHAR;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;
    case SQL_CONVERT_DOUBLE:
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr = SQL_CVT_DECIMAL | SQL_CVT_DOUBLE |
	  SQL_CVT_FLOAT | SQL_CVT_INTEGER | SQL_CVT_NUMERIC | SQL_CVT_REAL |
	  SQL_CVT_SMALLINT | SQL_CVT_CHAR | SQL_CVT_LONGVARCHAR |
	  SQL_CVT_VARCHAR;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;
    case SQL_CONVERT_FLOAT:
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr = SQL_CVT_DECIMAL | SQL_CVT_DOUBLE |
	  SQL_CVT_FLOAT | SQL_CVT_INTEGER | SQL_CVT_NUMERIC | SQL_CVT_REAL |
	  SQL_CVT_SMALLINT | SQL_CVT_CHAR | SQL_CVT_LONGVARCHAR |
	  SQL_CVT_VARCHAR;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;
    case SQL_CONVERT_INTEGER:
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr = SQL_CVT_DECIMAL | SQL_CVT_DOUBLE |
	  SQL_CVT_FLOAT | SQL_CVT_INTEGER | SQL_CVT_NUMERIC | SQL_CVT_REAL |
	  SQL_CVT_SMALLINT | SQL_CVT_CHAR | SQL_CVT_LONGVARCHAR |
	  SQL_CVT_VARCHAR;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;
    case SQL_CONVERT_INTERVAL_YEAR_MONTH:
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr = 0;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;
    case SQL_CONVERT_INTERVAL_DAY_TIME:
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr = 0;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;
    case SQL_CONVERT_LONGVARBINARY:
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr =
	  SQL_CVT_BINARY | SQL_CVT_LONGVARBINARY | SQL_CVT_VARBINARY |
	  SQL_CVT_CHAR | SQL_CVT_VARCHAR | SQL_CVT_LONGVARCHAR;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;
    case SQL_CONVERT_LONGVARCHAR:
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr = SQL_CVT_BINARY | SQL_CVT_CHAR |
	  SQL_CVT_DATE | SQL_CVT_DECIMAL | SQL_CVT_DOUBLE | SQL_CVT_FLOAT |
	  SQL_CVT_INTEGER | SQL_CVT_LONGVARBINARY | SQL_CVT_LONGVARCHAR |
	  SQL_CVT_NUMERIC | SQL_CVT_REAL | SQL_CVT_SMALLINT | SQL_CVT_TIME |
	  SQL_CVT_TIMESTAMP | SQL_CVT_VARBINARY | SQL_CVT_VARCHAR;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;
    case SQL_CONVERT_NUMERIC:
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr = SQL_CVT_DECIMAL | SQL_CVT_DOUBLE |
	  SQL_CVT_FLOAT | SQL_CVT_INTEGER | SQL_CVT_NUMERIC | SQL_CVT_REAL |
	  SQL_CVT_SMALLINT | SQL_CVT_CHAR | SQL_CVT_LONGVARCHAR |
	  SQL_CVT_VARCHAR;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;
    case SQL_CONVERT_REAL:
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr = SQL_CVT_DECIMAL | SQL_CVT_DOUBLE |
	  SQL_CVT_FLOAT | SQL_CVT_INTEGER | SQL_CVT_NUMERIC | SQL_CVT_REAL |
	  SQL_CVT_SMALLINT | SQL_CVT_CHAR | SQL_CVT_LONGVARCHAR |
	  SQL_CVT_VARCHAR;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;
    case SQL_CONVERT_SMALLINT:
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr = SQL_CVT_DECIMAL | SQL_CVT_DOUBLE |
	  SQL_CVT_FLOAT | SQL_CVT_INTEGER | SQL_CVT_NUMERIC | SQL_CVT_REAL |
	  SQL_CVT_SMALLINT | SQL_CVT_CHAR | SQL_CVT_LONGVARCHAR |
	  SQL_CVT_VARCHAR;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;
    case SQL_CONVERT_TIME:
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr =
	  SQL_CVT_CHAR | SQL_CVT_LONGVARCHAR | SQL_CVT_VARCHAR | SQL_CVT_TIME;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;
    case SQL_CONVERT_TIMESTAMP:
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr =
	  SQL_CVT_CHAR | SQL_CVT_LONGVARCHAR | SQL_CVT_VARCHAR | SQL_CVT_TIME
	  | SQL_CVT_DATE | SQL_CVT_TIMESTAMP;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;
    case SQL_CONVERT_TINYINT:
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr = 0;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;
    case SQL_CONVERT_VARBINARY:
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr =
	  SQL_CVT_BINARY | SQL_CVT_LONGVARBINARY | SQL_CVT_VARBINARY |
	  SQL_CVT_CHAR | SQL_CVT_VARCHAR | SQL_CVT_LONGVARCHAR;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;
    case SQL_CONVERT_VARCHAR:
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr = SQL_CVT_BINARY | SQL_CVT_CHAR |
	  SQL_CVT_DATE | SQL_CVT_DECIMAL | SQL_CVT_DOUBLE | SQL_CVT_FLOAT |
	  SQL_CVT_INTEGER | SQL_CVT_LONGVARBINARY | SQL_CVT_LONGVARCHAR |
	  SQL_CVT_NUMERIC | SQL_CVT_REAL | SQL_CVT_SMALLINT | SQL_CVT_TIME |
	  SQL_CVT_TIMESTAMP | SQL_CVT_VARBINARY | SQL_CVT_VARCHAR;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;

    case SQL_CONVERT_FUNCTIONS:
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr = SQL_FN_CVT_CAST;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;

    case SQL_CORRELATION_NAME:
      if (info_value_ptr != NULL)
	*(unsigned short *) info_value_ptr = SQL_CN_ANY;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned short);
      break;

    case SQL_CREATE_ASSERTION:
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr = 0;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;

    case SQL_CREATE_CHARACTER_SET:
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr = 0;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;

    case SQL_CREATE_COLLATION:
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr = 0;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;
    case SQL_CREATE_DOMAIN:
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr = 0;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;
    case SQL_CREATE_SCHEMA:
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr = 0;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;
    case SQL_CREATE_TABLE:
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr = SQL_CT_CREATE_TABLE;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;
    case SQL_CREATE_TRANSLATION:
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr = 0;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;
    case SQL_CREATE_VIEW:
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr = SQL_CV_CREATE_VIEW |
	  SQL_CV_CHECK_OPTION;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;

    case SQL_CURSOR_COMMIT_BEHAVIOR:
      // SQL_CB_DELETE 사용시 ADO에서 memory access violation이 발생한다.
      // 이를 피하기 위해서 SQL_CB_CLOSE를 사용했고,
      // SQL_CB_CLOSE에 맞게 동작하기 위해서 emulation 시켰다.
      // 참고, SQL_CB_CLOSE는 CUBRID에서 지원하지 않는다.
      if (info_value_ptr != NULL)
	*(unsigned short *) info_value_ptr = SQL_CB_CLOSE;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned short);
      break;

    case SQL_CURSOR_ROLLBACK_BEHAVIOR:
      // SQL_CB_DELETE 사용시 ADO에서 memory access violation이 발생한다.
      // 이를 피하기 위해서 SQL_CB_CLOSE를 사용했고,
      // SQL_CB_CLOSE에 맞게 동작하기 위해서 emulation 시켰다.
      // 참고, SQL_CB_CLOSE는 CUBRID에서 지원하지 않는다.
      if (info_value_ptr != NULL)
	*(unsigned short *) info_value_ptr = SQL_CB_CLOSE;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned short);
      break;

    case SQL_CURSOR_SENSITIVITY:
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr = SQL_UNSPECIFIED;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;

    case SQL_DATA_SOURCE_NAME:
      if (conn->data_source == NULL)
	{
	  buf[0] = '\0';
	}
      else
	{
	  strcpy (buf, conn->data_source);
	}

      rc =
	str_value_assign (buf, info_value_ptr, buffer_length,
			  string_length_ptr);
      if (rc == ODBC_SUCCESS_WITH_INFO)
	{
	  odbc_set_diag (conn->diag, "01004", 0, NULL);
	}
      break;

    case SQL_DATA_SOURCE_READ_ONLY:
      if (conn->attr_access_mode == SQL_MODE_READ_ONLY)
	{
	  strcpy (buf, "Y");
	}
      else
	{
	  strcpy (buf, "N");
	}
      rc =
	str_value_assign (buf, info_value_ptr, buffer_length,
			  string_length_ptr);
      if (rc == ODBC_SUCCESS_WITH_INFO)
	{
	  odbc_set_diag (conn->diag, "01004", 0, NULL);
	}
      break;

    case SQL_DATABASE_NAME:
      if (conn->db_name == NULL)
	{
	  buf[0] = '\0';
	}
      else
	{
	  strcpy (buf, conn->db_name);
	}

      rc =
	str_value_assign (buf, info_value_ptr, buffer_length,
			  string_length_ptr);
      if (rc == ODBC_SUCCESS_WITH_INFO)
	{
	  odbc_set_diag (conn->diag, "01004", 0, NULL);
	}
      break;

    case SQL_DATETIME_LITERALS:
      *(unsigned long *) info_value_ptr =
	SQL_DL_SQL92_DATE | SQL_DL_SQL92_TIME | SQL_DL_SQL92_TIMESTAMP;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;

    case SQL_DBMS_NAME:
      rc =
	str_value_assign (CUBRID_DBMS_NAME, info_value_ptr, buffer_length,
			  string_length_ptr);
      if (rc == ODBC_SUCCESS_WITH_INFO)
	{
	  odbc_set_diag (conn->diag, "01004", 0, NULL);
	}
      break;

    case SQL_DBMS_VER:
      rc =
	str_value_assign (conn->db_ver, info_value_ptr, buffer_length,
			  string_length_ptr);
      if (rc == ODBC_SUCCESS_WITH_INFO)
	{
	  odbc_set_diag (conn->diag, "01004", 0, NULL);
	}
      break;

    case SQL_DDL_INDEX:
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr = SQL_DI_CREATE_INDEX |
	  SQL_DI_DROP_INDEX;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;

    case SQL_DEFAULT_TXN_ISOLATION:
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr = SQL_TXN_READ_COMMITTED;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;

    case SQL_DESCRIBE_PARAMETER:
      rc =
	str_value_assign ("N", info_value_ptr, buffer_length,
			  string_length_ptr);
      if (rc == ODBC_SUCCESS_WITH_INFO)
	{
	  odbc_set_diag (conn->diag, "01004", 0, NULL);
	}
      break;

    case SQL_DRIVER_NAME:
      rc =
	str_value_assign (CUBRID_DRIVER_NAME, info_value_ptr, buffer_length,
			  string_length_ptr);
      if (rc == ODBC_SUCCESS_WITH_INFO)
	{
	  odbc_set_diag (conn->diag, "01004", 0, NULL);
	}
      break;

    case SQL_DRIVER_ODBC_VER:
      rc =
	str_value_assign (CUBRID_DRIVER_ODBC_VER, info_value_ptr,
			  buffer_length, string_length_ptr);
      if (rc == ODBC_SUCCESS_WITH_INFO)
	{
	  odbc_set_diag (conn->diag, "01004", 0, NULL);
	}
      break;

    case SQL_DRIVER_VER:
      rc =
	str_value_assign (CUBRID_DRIVER_VER, info_value_ptr, buffer_length,
			  string_length_ptr);
      if (rc == ODBC_SUCCESS_WITH_INFO)
	{
	  odbc_set_diag (conn->diag, "01004", 0, NULL);
	}
      break;

    case SQL_DROP_ASSERTION:
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr = 0;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;

    case SQL_DROP_CHARACTER_SET:
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr = 0;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;

    case SQL_DROP_COLLATION:
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr = 0;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;

    case SQL_DROP_DOMAIN:
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr = 0;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;

    case SQL_DROP_SCHEMA:
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr = 0;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;

    case SQL_DROP_TABLE:
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr = SQL_DT_DROP_TABLE;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;

    case SQL_DROP_TRANSLATION:
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr = 0;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;

    case SQL_DROP_VIEW:
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr = SQL_DV_DROP_VIEW;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;

    case SQL_DYNAMIC_CURSOR_ATTRIBUTES1:
      /* CHECK : YET not supported */
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr = SQL_CA1_NEXT;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;
    case SQL_DYNAMIC_CURSOR_ATTRIBUTES2:
      /* CHECK : YET not supported */
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr = SQL_CA2_READ_ONLY_CONCURRENCY;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;

    case SQL_EXPRESSIONS_IN_ORDERBY:
      rc =
	str_value_assign ("Y", info_value_ptr, buffer_length,
			  string_length_ptr);
      if (rc == ODBC_SUCCESS_WITH_INFO)
	{
	  odbc_set_diag (conn->diag, "01004", 0, NULL);
	}
      break;

    case SQL_FILE_USAGE:
      if (info_value_ptr != NULL)
	if (info_value_ptr != NULL)
	  *(unsigned short *) info_value_ptr = SQL_FILE_NOT_SUPPORTED;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned short);
      break;

    case SQL_FORWARD_ONLY_CURSOR_ATTRIBUTES1:
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr = SQL_CA1_NEXT;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;
    case SQL_FORWARD_ONLY_CURSOR_ATTRIBUTES2:
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr = SQL_CA2_READ_ONLY_CONCURRENCY |
	  SQL_CA2_LOCK_CONCURRENCY | SQL_CA2_CRC_EXACT |
	  SQL_CA2_SIMULATE_NON_UNIQUE;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;

    case SQL_GETDATA_EXTENSIONS:
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr = SQL_GD_ANY_COLUMN |
	  SQL_GD_ANY_ORDER | SQL_GD_BOUND;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;

    case SQL_GROUP_BY:
      if (info_value_ptr != NULL)
	*(unsigned short *) info_value_ptr = SQL_GB_GROUP_BY_CONTAINS_SELECT;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned short);
      break;

    case SQL_IDENTIFIER_CASE:
      if (info_value_ptr != NULL)
	*(unsigned short *) info_value_ptr = SQL_IC_MIXED;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned short);
      break;

    case SQL_IDENTIFIER_QUOTE_CHAR:
      /* YET not exactly implemented */
      rc =
	str_value_assign ("\"", info_value_ptr, buffer_length,
			  string_length_ptr);
      if (rc == ODBC_SUCCESS_WITH_INFO)
	{
	  odbc_set_diag (conn->diag, "01004", 0, NULL);
	}
      break;

    case SQL_INDEX_KEYWORDS:
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr = SQL_IK_ALL;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;

    case SQL_INFO_SCHEMA_VIEWS:
      /* CHECK : YET not exactly implemented */
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr = 0;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;

    case SQL_INSERT_STATEMENT:
      /* CHECK : YET not exactly implemented */
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr = 0;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;

    case SQL_INTEGRITY:
      rc =
	str_value_assign ("N", info_value_ptr, buffer_length,
			  string_length_ptr);
      if (rc == ODBC_SUCCESS_WITH_INFO)
	{
	  odbc_set_diag (conn->diag, "01004", 0, NULL);
	}
      break;

    case SQL_KEYSET_CURSOR_ATTRIBUTES1:
      /* CHECK : YET not exactly implemented */
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr = 0;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;
    case SQL_KEYSET_CURSOR_ATTRIBUTES2:
      /* CHECK : YET not exactly implemented */
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr = 0;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;

    case SQL_KEYWORDS:
      rc =
	str_value_assign (CUBRID_KEYWORDS, info_value_ptr, buffer_length,
			  string_length_ptr);
      if (rc == ODBC_SUCCESS_WITH_INFO)
	{
	  odbc_set_diag (conn->diag, "01004", 0, NULL);
	}
      break;

    case SQL_LIKE_ESCAPE_CLAUSE:
      rc =
	str_value_assign ("Y", info_value_ptr, buffer_length,
			  string_length_ptr);
      if (rc == ODBC_SUCCESS_WITH_INFO)
	{
	  odbc_set_diag (conn->diag, "01004", 0, NULL);
	}
      break;

    case SQL_MAX_ASYNC_CONCURRENT_STATEMENTS:
      /* CHECK : Unknown */
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr = 0;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;

    case SQL_MAX_BINARY_LITERAL_LEN:
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr = MAX_CUBRID_CHAR_LEN / 8;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;

    case SQL_MAX_CATALOG_NAME_LEN:
      if (info_value_ptr != NULL)
	*(unsigned short *) info_value_ptr = 0;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned short);
      break;

    case SQL_MAX_CHAR_LITERAL_LEN:
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr = MAX_CUBRID_CHAR_LEN;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;

    case SQL_MAX_COLUMN_NAME_LEN:
      if (info_value_ptr != NULL)
	*(unsigned short *) info_value_ptr = 255;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned short);
      break;

    case SQL_MAX_COLUMNS_IN_GROUP_BY:
      if (info_value_ptr != NULL)
	*(unsigned short *) info_value_ptr = 0;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned short);
      break;

    case SQL_MAX_COLUMNS_IN_INDEX:
      if (info_value_ptr != NULL)
	*(unsigned short *) info_value_ptr = 0;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned short);
      break;

    case SQL_MAX_COLUMNS_IN_ORDER_BY:
      if (info_value_ptr != NULL)
	*(unsigned short *) info_value_ptr = 0;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned short);
      break;

    case SQL_MAX_COLUMNS_IN_SELECT:
      if (info_value_ptr != NULL)
	*(unsigned short *) info_value_ptr = 0;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned short);
      break;

    case SQL_MAX_COLUMNS_IN_TABLE:
      if (info_value_ptr != NULL)
	*(unsigned short *) info_value_ptr = 0;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned short);
      break;

    case SQL_MAX_CONCURRENT_ACTIVITIES:
      /* CHECK : Unknown */
      if (info_value_ptr != NULL)
	*(unsigned short *) info_value_ptr = 1;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned short);
      break;

    case SQL_MAX_CURSOR_NAME_LEN:
      if (info_value_ptr != NULL)
	*(unsigned short *) info_value_ptr = 0;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned short);
      break;

    case SQL_MAX_DRIVER_CONNECTIONS:
      /* CHECK : Unknown */
      if (info_value_ptr != NULL)
	*(unsigned short *) info_value_ptr = 0;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned short);
      break;

    case SQL_MAX_IDENTIFIER_LEN:
      if (info_value_ptr != NULL)
	*(unsigned short *) info_value_ptr = 255;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned short);
      break;

    case SQL_MAX_INDEX_SIZE:
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr = 0;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;

    case SQL_MAX_PROCEDURE_NAME_LEN:
      if (info_value_ptr != NULL)
	*(unsigned short *) info_value_ptr = 0;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned short);
      break;

    case SQL_MAX_ROW_SIZE:
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr = 0;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;

    case SQL_MAX_ROW_SIZE_INCLUDES_LONG:
      rc =
	str_value_assign ("N", info_value_ptr, buffer_length,
			  string_length_ptr);
      if (rc == ODBC_SUCCESS_WITH_INFO)
	{
	  odbc_set_diag (conn->diag, "01004", 0, NULL);
	}
      break;

    case SQL_MAX_SCHEMA_NAME_LEN:
      if (info_value_ptr != NULL)
	*(unsigned short *) info_value_ptr = 0;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned short);
      break;

    case SQL_MAX_STATEMENT_LEN:
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr = 0;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;

    case SQL_MAX_TABLE_NAME_LEN:
      if (info_value_ptr != NULL)
	*(unsigned short *) info_value_ptr = 255;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned short);
      break;

    case SQL_MAX_TABLES_IN_SELECT:
      if (info_value_ptr != NULL)
	*(unsigned short *) info_value_ptr = 0;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned short);
      break;

    case SQL_MAX_USER_NAME_LEN:
      if (info_value_ptr != NULL)
	*(unsigned short *) info_value_ptr = 0;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned short);
      break;

    case SQL_MULT_RESULT_SETS:
      /* CHECK : array bind parameter와 batch execution, SQLMoreResults       */
      /* 등이 지원되어야 한다.                                                                                        */
      rc =
	str_value_assign ("Y", info_value_ptr, buffer_length,
			  string_length_ptr);
      if (rc == ODBC_SUCCESS_WITH_INFO)
	{
	  odbc_set_diag (conn->diag, "01004", 0, NULL);
	}
      break;

    case SQL_MULTIPLE_ACTIVE_TXN:
      rc =
	str_value_assign ("Y", info_value_ptr, buffer_length,
			  string_length_ptr);
      if (rc == ODBC_SUCCESS_WITH_INFO)
	{
	  odbc_set_diag (conn->diag, "01004", 0, NULL);
	}
      break;

    case SQL_NEED_LONG_DATA_LEN:
      rc =
	str_value_assign ("N", info_value_ptr, buffer_length,
			  string_length_ptr);
      if (rc == ODBC_SUCCESS_WITH_INFO)
	{
	  odbc_set_diag (conn->diag, "01004", 0, NULL);
	}
      break;

    case SQL_NON_NULLABLE_COLUMNS:
      if (info_value_ptr != NULL)
	*(unsigned short *) info_value_ptr = SQL_NNC_NON_NULL;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned short);
      break;

    case SQL_NULL_COLLATION:
      if (info_value_ptr != NULL)
	*(unsigned short *) info_value_ptr = SQL_NC_LOW;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned short);
      break;

    case SQL_NUMERIC_FUNCTIONS:
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr = 0;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;

    case SQL_ODBC_INTERFACE_CONFORMANCE:
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr = SQL_OIC_CORE;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;

    case SQL_OJ_CAPABILITIES:
    case SQL_OUTER_JOINS:
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr = 0;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;

    case SQL_ORDER_BY_COLUMNS_IN_SELECT:
      rc =
	str_value_assign ("Y", info_value_ptr, buffer_length,
			  string_length_ptr);
      if (rc == ODBC_SUCCESS_WITH_INFO)
	{
	  odbc_set_diag (conn->diag, "01004", 0, NULL);
	}
      break;

    case SQL_PARAM_ARRAY_ROW_COUNTS:
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr = SQL_PARC_NO_BATCH;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;

    case SQL_PARAM_ARRAY_SELECTS:
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr = SQL_PAS_NO_SELECT;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;

    case SQL_PROCEDURE_TERM:
      /* CUBRID does not support procedure */
      rc =
	str_value_assign ("", info_value_ptr, buffer_length,
			  string_length_ptr);
      if (rc == ODBC_SUCCESS_WITH_INFO)
	{
	  odbc_set_diag (conn->diag, "01004", 0, NULL);
	}
      break;

    case SQL_PROCEDURES:
      /* CUBRID does not support procedure */
      rc =
	str_value_assign ("N", info_value_ptr, buffer_length,
			  string_length_ptr);
      if (rc == ODBC_SUCCESS_WITH_INFO)
	{
	  odbc_set_diag (conn->diag, "01004", 0, NULL);
	}
      break;

    case SQL_QUOTED_IDENTIFIER_CASE:
      /* check : driver hasn't exactly implemented this attribute yet */
      *(unsigned short *) info_value_ptr = SQL_IC_MIXED;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned short);
      break;

    case SQL_ROW_UPDATES:
      rc =
	str_value_assign ("N", info_value_ptr, buffer_length,
			  string_length_ptr);
      if (rc == ODBC_SUCCESS_WITH_INFO)
	{
	  odbc_set_diag (conn->diag, "01004", 0, NULL);
	}
      break;

    case SQL_SCHEMA_TERM:
      /* CUBRID does not support schema */
      rc =
	str_value_assign ("", info_value_ptr, buffer_length,
			  string_length_ptr);
      if (rc == ODBC_SUCCESS_WITH_INFO)
	{
	  odbc_set_diag (conn->diag, "01004", 0, NULL);
	}
      break;

    case SQL_SCHEMA_USAGE:
      /* CUBRID does not support schema */
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr = 0;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;

    case SQL_SCROLL_OPTIONS:
      //*(unsigned long*)info_value_ptr = SQL_SO_FORWARD_ONLY | SQL_SO_STATIC;
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr =
	  SQL_SO_FORWARD_ONLY | SQL_SO_KEYSET_DRIVEN | SQL_SO_DYNAMIC |
	  SQL_SO_STATIC;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;

    case SQL_SEARCH_PATTERN_ESCAPE:
      rc =
	str_value_assign ("\\", info_value_ptr, buffer_length,
			  string_length_ptr);
      if (rc == ODBC_SUCCESS_WITH_INFO)
	{
	  odbc_set_diag (conn->diag, "01004", 0, NULL);
	}
      break;

    case SQL_SERVER_NAME:
      if (conn->server == NULL)
	{
	  buf[0] = '\0';
	}
      else
	{
	  strcpy (buf, conn->server);
	}
      rc =
	str_value_assign (buf, info_value_ptr, buffer_length,
			  string_length_ptr);
      if (rc == ODBC_SUCCESS_WITH_INFO)
	{
	  odbc_set_diag (conn->diag, "01004", 0, NULL);
	}
      break;

    case SQL_SPECIAL_CHARACTERS:
      rc =
	str_value_assign (CUBRID_SPECIAL_CHARACTERS, info_value_ptr,
			  buffer_length, string_length_ptr);
      if (rc == ODBC_SUCCESS_WITH_INFO)
	{
	  odbc_set_diag (conn->diag, "01004", 0, NULL);
	}
      break;

    case SQL_SQL_CONFORMANCE:
      // 정확한 정보는 아니고, SQL_SC_SQL92_ENTRY가 가장 작은 spec이다.
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr = SQL_SC_SQL92_ENTRY;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;

    case SQL_SQL92_DATETIME_FUNCTIONS:
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr = SQL_SDF_CURRENT_DATE |
	  SQL_SDF_CURRENT_TIME | SQL_SDF_CURRENT_TIMESTAMP;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;

    case SQL_SQL92_FOREIGN_KEY_DELETE_RULE:
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr = 0;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;

    case SQL_SQL92_FOREIGN_KEY_UPDATE_RULE:
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr = 0;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;

    case SQL_SQL92_GRANT:
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr = SQL_SG_DELETE_TABLE |
	  SQL_SG_INSERT_COLUMN | SQL_SG_INSERT_TABLE |
	  SQL_SG_SELECT_TABLE | SQL_SG_UPDATE_COLUMN |
	  SQL_SG_UPDATE_TABLE | SQL_SG_WITH_GRANT_OPTION;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;

    case SQL_SQL92_NUMERIC_VALUE_FUNCTIONS:
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr = SQL_SNVF_BIT_LENGTH |
	  SQL_SNVF_CHAR_LENGTH | SQL_SNVF_OCTET_LENGTH | SQL_SNVF_POSITION;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;

    case SQL_SQL92_PREDICATES:
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr = SQL_SP_BETWEEN |
	  SQL_SP_COMPARISON | SQL_SP_EXISTS | SQL_SP_IN |
	  SQL_SP_ISNOTNULL | SQL_SP_ISNULL | SQL_SP_LIKE |
	  SQL_SP_QUANTIFIED_COMPARISON | SQL_SP_UNIQUE;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;

    case SQL_SQL92_RELATIONAL_JOIN_OPERATORS:
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr = SQL_SRJO_CORRESPONDING_CLAUSE |
	  SQL_SRJO_CROSS_JOIN | SQL_SRJO_EXCEPT_JOIN |
	  SQL_SRJO_INTERSECT_JOIN | SQL_SRJO_NATURAL_JOIN |
	  SQL_SRJO_UNION_JOIN;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;

    case SQL_SQL92_REVOKE:
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr = SQL_SR_DELETE_TABLE |
	  SQL_SR_GRANT_OPTION_FOR | SQL_SR_INSERT_COLUMN |
	  SQL_SR_INSERT_TABLE | SQL_SR_SELECT_TABLE |
	  SQL_SR_UPDATE_COLUMN | SQL_SR_UPDATE_TABLE;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;

    case SQL_SQL92_ROW_VALUE_CONSTRUCTOR:
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr = SQL_SRVC_VALUE_EXPRESSION |
	  SQL_SRVC_NULL | SQL_SRVC_ROW_SUBQUERY;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;

    case SQL_SQL92_STRING_FUNCTIONS:
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr = SQL_SSF_CONVERT |
	  SQL_SSF_LOWER | SQL_SSF_UPPER | SQL_SSF_SUBSTRING |
	  SQL_SSF_TRANSLATE | SQL_SSF_TRIM_BOTH | SQL_SSF_TRIM_LEADING |
	  SQL_SSF_TRIM_TRAILING;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;

    case SQL_SQL92_VALUE_EXPRESSIONS:
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr = SQL_SVE_CASE |
	  SQL_SVE_CAST | SQL_SVE_COALESCE | SQL_SVE_NULLIF;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;

    case SQL_STANDARD_CLI_CONFORMANCE:
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr = SQL_SCC_ISO92_CLI;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;

    case SQL_STATIC_CURSOR_ATTRIBUTES1:
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr = SQL_CA1_NEXT | SQL_CA1_ABSOLUTE |
	  SQL_CA1_RELATIVE;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;

    case SQL_STATIC_CURSOR_ATTRIBUTES2:
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr = SQL_CA2_READ_ONLY_CONCURRENCY |
	  SQL_CA2_LOCK_CONCURRENCY | SQL_CA2_CRC_EXACT |
	  SQL_CA2_SIMULATE_NON_UNIQUE;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;

    case SQL_STRING_FUNCTIONS:
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr = SQL_FN_STR_BIT_LENGTH |
	  SQL_FN_STR_CHAR | SQL_FN_STR_CHAR_LENGTH |
	  SQL_FN_STR_LCASE | SQL_FN_STR_LTRIM |
	  SQL_FN_STR_OCTET_LENGTH | SQL_FN_STR_POSITION |
	  SQL_FN_STR_REPLACE | SQL_FN_STR_RTRIM |
	  SQL_FN_STR_SUBSTRING | SQL_FN_STR_UCASE;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;

    case SQL_SUBQUERIES:
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr = SQL_SQ_CORRELATED_SUBQUERIES |
	  SQL_SQ_COMPARISON | SQL_SQ_EXISTS | SQL_SQ_IN | SQL_SQ_QUANTIFIED;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;

    case SQL_SYSTEM_FUNCTIONS:
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr = 0;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;

    case SQL_TABLE_TERM:
      rc =
	str_value_assign (CUBRID_TABLE_TERM, info_value_ptr, buffer_length,
			  string_length_ptr);
      if (rc == ODBC_SUCCESS_WITH_INFO)
	{
	  odbc_set_diag (conn->diag, "01004", 0, NULL);
	}
      break;

    case SQL_TIMEDATE_ADD_INTERVALS:
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr = 0;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;

    case SQL_TIMEDATE_DIFF_INTERVALS:
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr = 0;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;

    case SQL_TIMEDATE_FUNCTIONS:
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr = SQL_FN_TD_CURRENT_DATE |
	  SQL_FN_TD_CURRENT_TIME | SQL_FN_TD_CURRENT_TIMESTAMP;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;

    case SQL_TXN_CAPABLE:
      if (info_value_ptr != NULL)
	*(unsigned short *) info_value_ptr = SQL_TC_ALL;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned short);
      break;

    case SQL_TXN_ISOLATION_OPTION:
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr = SQL_TXN_READ_UNCOMMITTED |
	  SQL_TXN_READ_COMMITTED | SQL_TXN_REPEATABLE_READ |
	  SQL_TXN_SERIALIZABLE;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;

    case SQL_UNION:
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr = SQL_U_UNION;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;

    case SQL_USER_NAME:
      if (conn->user == NULL)
	{
	  buf[0] = '\0';
	}
      else
	{
	  strcpy (buf, conn->user);
	}
      rc =
	str_value_assign (buf, info_value_ptr, buffer_length,
			  string_length_ptr);
      if (rc == ODBC_SUCCESS_WITH_INFO)
	{
	  odbc_set_diag (conn->diag, "01004", 0, NULL);
	}
      break;

    case SQL_XOPEN_CLI_YEAR:
      /* CHECK : YET not exactly implemented */
      rc =
	str_value_assign ("", info_value_ptr, buffer_length,
			  string_length_ptr);
      if (rc == ODBC_SUCCESS_WITH_INFO)
	{
	  odbc_set_diag (conn->diag, "01004", 0, NULL);
	}
      break;





	/*-------------------------------------------------------------
	 *				For backward compatibility
	 *------------------------------------------------------------*/
    case SQL_FETCH_DIRECTION:
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr = SQL_FD_FETCH_NEXT;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;

    case SQL_LOCK_TYPES:
      if (info_value_ptr != NULL)
	*(unsigned long *) info_value_ptr = SQL_LCK_NO_CHANGE;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (unsigned long);
      break;

    case SQL_ODBC_API_CONFORMANCE:
      if (info_value_ptr != NULL)
	*(short *) info_value_ptr = SQL_OAC_NONE;
#if 1
      /* MS ACCESS에서 데이타베이스 연결시 SQL_OAC_NONE일 경우 연결실패 */
      if (info_value_ptr != NULL)
	*(short *) info_value_ptr = SQL_OAC_LEVEL1;
#endif
      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (short);
      break;
    case SQL_ODBC_SQL_CONFORMANCE:
      if (info_value_ptr != NULL)
	*(short *) info_value_ptr = SQL_OSC_CORE;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (short);
      break;
    case SQL_POS_OPERATIONS:
      if (info_value_ptr != NULL)
	*(long *) info_value_ptr = 0;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (long);
      break;
    case SQL_POSITIONED_STATEMENTS:
      if (info_value_ptr != NULL)
	*(long *) info_value_ptr = 0;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (long);
      break;
    case SQL_SCROLL_CONCURRENCY:
      if (info_value_ptr != NULL)
	*(long *) info_value_ptr = SQL_SCCO_READ_ONLY;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (long);
      break;
    case SQL_STATIC_SENSITIVITY:
      if (info_value_ptr != NULL)
	*(long *) info_value_ptr = 0;

      if (string_length_ptr != NULL)
	*string_length_ptr = sizeof (long);
      break;


    default:
      odbc_set_diag (conn->diag, "HY096", 0, NULL);
      return ODBC_ERROR;
    }

  return rc;

}

/************************************************************************
* name: get_dsn_info
* arguments:
* returns/side-effects:
* description:
* NOTE:
*	1. SQLSetConfigMode(ODBC_BOTH_DSN)에 의해서 ODBC_USER_DSN에
*	먼저 접근하게 된다.
*	2. char* length의 max size는 1024bytes이다.
************************************************************************/
PUBLIC int
get_dsn_info (const char *dsn,
	      char *db_name, int db_name_len,
	      char *user, int user_len,
	      char *pwd, int pwd_len,
	      char *server, int server_len, int *port, int *fetch_size)
{
  char buf[1024];
  int rcn;			// return char number

  if (dsn == NULL)
    {
      return -1;
    }

  SQLSetConfigMode (ODBC_BOTH_DSN);

  // Get DB name entry
  if (db_name != NULL)
    {
      rcn =
	SQLGetPrivateProfileString (dsn, KEYWORD_DBNAME, "", buf,
				    sizeof (buf), "ODBC.INI");
      if (rcn == 0)
	buf[0] = '\0';
      str_value_assign (buf, db_name, db_name_len, NULL);
    }

  // Get user entry
  if (user != NULL)
    {
      rcn =
	SQLGetPrivateProfileString (dsn, KEYWORD_USER, "", buf, sizeof (buf),
				    "ODBC.INI");
      if (rcn == 0)
	buf[0] = '\0';
      str_value_assign (buf, user, user_len, NULL);
    }

  // Get password entry
  if (pwd != NULL)
    {
      rcn =
	SQLGetPrivateProfileString (dsn, KEYWORD_PASSWORD, "", buf,
				    sizeof (buf), "ODBC.INI");
      if (rcn == 0)
	buf[0] = '\0';
      str_value_assign (buf, pwd, pwd_len, NULL);
    }

  // Get server entry
  if (server != NULL)
    {
      rcn =
	SQLGetPrivateProfileString (dsn, KEYWORD_SERVER, "", buf,
				    sizeof (buf), "ODBC.INI");
      if (rcn == 0)
	buf[0] = '\0';
      str_value_assign (buf, server, server_len, NULL);
    }

  // Get port entry
  if (port != NULL)
    {
      rcn =
	SQLGetPrivateProfileString (dsn, KEYWORD_PORT, "", buf, sizeof (buf),
				    "ODBC.INI");
      if (rcn == 0)
	*port = 0;
      else
	*port = atoi (buf);
    }

  // Get fetch size entry
  if (fetch_size != NULL)
    {
      rcn =
	SQLGetPrivateProfileString (dsn, KEYWORD_FETCH_SIZE, "", buf,
				    sizeof (buf), "ODBC.INI");
      if (rcn == 0)
	*fetch_size = 0;
      else
	*fetch_size = atoi (buf);
    }

  return 0;
}

/************************************************************************
* name: get_server_setting
* arguments:
* returns/side-effects:
* description:
*		DSN(registry)에서 ip address, port num, db name을 얻어온다.
* NOTE:
*		SQLSetConfigMode(ODBC_BOTH_DSN)에 의해서 ODBC_USER_DSN에
*		먼저 접근하게 된다.
************************************************************************/

PRIVATE int
get_server_setting (ODBC_CONNECTION * conn)
{
  char buf[256];
  int rcn;			// return char number

  if (conn->data_source == NULL)
    {
      return -1;
    }

  SQLSetConfigMode (ODBC_BOTH_DSN);

  // Get server entry
  rcn =
    SQLGetPrivateProfileString ("CUBRID", KEYWORD_SERVER, "", buf,
				sizeof (buf), "ODBC.INI");
  if (rcn == 0)
    buf[0] = '\0';
  conn->server = UT_MAKE_STRING (buf, -1);

  // Get port entry
  rcn =
    SQLGetPrivateProfileString ("CUBRID", KEYWORD_PORT, "", buf, sizeof (buf),
				"ODBC.INI");
  if (rcn == 0)
    buf[0] = '\0';
  conn->port = atoi (buf);

  // Get DB name entry
  rcn =
    SQLGetPrivateProfileString ("CUBRID", KEYWORD_DBNAME, "", buf,
				sizeof (buf), "ODBC.INI");
  if (rcn == 0)
    buf[0] = '\0';
  conn->db_name = UT_MAKE_STRING (buf, -1);

  // Get fetch size entry
  rcn =
    SQLGetPrivateProfileString ("CUBRID", KEYWORD_FETCH_SIZE, "", buf,
				sizeof (buf), "ODBC.INI");
  if (rcn == 0)
    buf[0] = '\0';
  conn->fetch_size = atoi (buf);

  return 0;
}



/************************************************************************
 * name:  set_isloation_level
 * arguments:
 * returns/side-effects:
 * description:
 * NOTE:
 ************************************************************************/
PRIVATE RETCODE
set_isolation_level (ODBC_CONNECTION * conn)
{
  int cci_rc;
  T_CCI_ERROR cci_err_buf;
  int isolation_level;

  switch (conn->attr_txn_isolation)
    {
    case SQL_TXN_SERIALIZABLE:
      isolation_level = TRAN_SERIALIZABLE;
      break;
    case SQL_TXN_REPEATABLE_READ:
      isolation_level = TRAN_REP_CLASS_REP_INSTANCE;
      break;
    case SQL_TXN_READ_COMMITTED:
      isolation_level = TRAN_COMMIT_CLASS_COMMIT_INSTANCE;
      break;
    case SQL_TXN_READ_UNCOMMITTED:
      isolation_level = TRAN_COMMIT_CLASS_UNCOMMIT_INSTANCE;
      break;
    default:			// serializable
      isolation_level = TRAN_REP_CLASS_REP_INSTANCE;
      break;
    }

  cci_rc = cci_set_db_parameter (conn->connhd, CCI_PARAM_ISOLATION_LEVEL,
				 &isolation_level, &cci_err_buf);
  if (cci_rc < 0)
    {
      odbc_set_diag_by_cci (conn->diag, cci_rc, cci_err_buf.err_msg);
      return ODBC_ERROR;
    }

  return ODBC_SUCCESS;
}


/************************************************************************
 * name: get_db_version
 * arguments:
 * returns/side-effects:
 * description:
 * NOTE:
 ************************************************************************/
PRIVATE RETCODE
get_db_version (ODBC_CONNECTION * conn)
{
  int cci_rc;
  T_CCI_ERROR error;

  cci_rc =
    cci_get_db_version (conn->connhd, conn->db_ver, sizeof (conn->db_ver));

  if (cci_rc < 0)
    {
      odbc_set_diag_by_cci (conn->diag, cci_rc, NULL);
      return ODBC_ERROR;
    }

  cci_rc = cci_get_db_parameter (conn->connhd,
				 CCI_PARAM_MAX_STRING_LENGTH,
				 &(conn->max_string_length), &error);

  if (cci_rc < 0)
    {
      odbc_set_diag_by_cci (conn->diag, cci_rc, NULL);
      return ODBC_ERROR;
    }

  return ODBC_SUCCESS;
}
