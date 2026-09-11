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

#ident "$Id$"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(WINDOWS)
#include <winsock2.h>
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include <process.h>
#else /* WINDOWS */
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#endif /* WINDOWS */
#include <assert.h>


#include "cas_util.h"
#include "cas_log.h"
#include "perf_monitor.h"
#include "transaction_cl.h"
#include "ddl_log.h"

#include "cas_cgw_execute.h"
#include "cas_cgw_odbc.h"
#include "cas_schema_info.h"
#include "cas_str_like.h"


#define DBLINK_HINT                     "DBLINK"

/* ========================================================================
 * Type Definitions
 * ======================================================================== */
typedef int (*T_FETCH_FUNC) (T_SRV_HANDLE *, int, int, char, int, T_NET_BUF *, T_REQ_INFO *);

/* ========================================================================
 * Forward Function Declarations
 * ======================================================================== */
static int cgw_fetch_result (T_SRV_HANDLE *, int, int, char, int, T_NET_BUF *, T_REQ_INFO *);
static int cgw_prepare_column_list_info_set (SQLHSTMT hstmt, char prepare_flag, char stmt_type,
					     T_BROKER_VERSION client_version, T_NET_BUF * net_buf);
static char ux_cgw_get_stmt_type (char *stmt);
static int fetch_not_supported (T_SRV_HANDLE *, int, int, char, int, T_NET_BUF *, T_REQ_INFO *);
static int fetch_call (T_SRV_HANDLE * srv_handle, T_NET_BUF * net_buf, T_REQ_INFO * req_info);
static bool do_commit_after_execute (const t_srv_handle & server_handle);
static void cgw_cleanup_col_bindings (T_SRV_HANDLE * srv_handle);

/* ========================================================================
 * Static Variable Definitions
 * ======================================================================== */
static T_FETCH_FUNC fetch_func[] = {
  cgw_fetch_result,		/* query */
  fetch_not_supported,		/* SCH_CLASS */
  fetch_not_supported,		/* SCH_VCLASS */
  fetch_not_supported,		/* SCH_QUERY_SPEC */
  fetch_not_supported,		/* SCH_ATTRIBUTE */
  fetch_not_supported,		/* SCH_CLASS_ATTRIBUTE */
  fetch_not_supported,		/* SCH_METHOD */
  fetch_not_supported,		/* SCH_CLASS_METHOD */
  fetch_not_supported,		/* SCH_METHOD_FILE */
  fetch_not_supported,		/* SCH_SUPERCLASS */
  fetch_not_supported,		/* SCH_SUBCLASS */
  fetch_not_supported,		/* SCH_CONSTRAINT */
  fetch_not_supported,		/* SCH_TRIGGER */
  fetch_not_supported,		/* SCH_CLASS_PRIVILEGE */
  fetch_not_supported,		/* SCH_ATTR_PRIVILEGE */
  fetch_not_supported,		/* SCH_DIRECT_SUPER_CLASS */
  fetch_not_supported,		/* SCH_PRIMARY_KEY */
  fetch_not_supported,		/* SCH_IMPORTED_KEYS */
  fetch_not_supported,		/* SCH_EXPORTED_KEYS */
  fetch_not_supported,		/* SCH_CROSS_REFERENCE */
};

/* ========================================================================
 * Function Definitions
 * ======================================================================== */
int
ux_cgw_check_connection (void)
{
  return cgw_is_database_connected ();
}

int
ux_cgw_prepare (char *sql_stmt, int flag, char auto_commit_mode, T_NET_BUF * net_buf, T_REQ_INFO * req_info,
		unsigned int query_seq_num)
{
  T_SRV_HANDLE *srv_handle = NULL;
  int srv_h_id = -1;
  int err_code;
  int num_markers;
  T_BROKER_VERSION client_version = req_info->client_version;
  int result_cache_lifetime;
  T_CGW_HANDLE *cgw_handle = NULL;

  if ((flag & CCI_PREPARE_UPDATABLE) && (flag & CCI_PREPARE_HOLDABLE))
    {
      /* do not allow updatable, holdable results */
      err_code = ERROR_INFO_SET (CAS_ER_HOLDABLE_NOT_ALLOWED, CAS_ERROR_INDICATOR);
      goto prepare_error;
    }

  srv_h_id = hm_new_srv_handle (&srv_handle, query_seq_num);

  if (srv_h_id < 0)
    {
      err_code = srv_h_id;
      goto prepare_error;
    }

  err_code = cgw_get_handle (&cgw_handle);
  if (err_code < 0)
    {
      err_code = ERROR_INFO_SET (db_error_code (), DBMS_ERROR_INDICATOR);
      goto prepare_error;
    }

  srv_handle->schema_type = -1;
  srv_handle->auto_commit_mode = auto_commit_mode;

  ALLOC_COPY_STRLEN (srv_handle->sql_stmt, sql_stmt);
  if (srv_handle->sql_stmt == NULL)
    {
      err_code = ERROR_INFO_SET (CAS_ER_NO_MORE_MEMORY, CAS_ERROR_INDICATOR);
      goto prepare_error;
    }

  sql_stmt = srv_handle->sql_stmt;

  if (flag & CCI_PREPARE_QUERY_INFO)
    {
      srv_handle->query_info_flag = TRUE;
    }
  else
    {
      srv_handle->query_info_flag = FALSE;
    }

  if (flag & CCI_PREPARE_UPDATABLE)
    {
      srv_handle->is_updatable = TRUE;
    }
  else
    {
      srv_handle->is_updatable = FALSE;
    }

  num_markers = get_num_markers (sql_stmt);
  srv_handle->num_markers = num_markers;
  srv_handle->prepare_flag = flag;

  err_code = cgw_sql_prepare (cgw_handle->hdbc, srv_handle, (SQLCHAR *) sql_stmt);
  if (err_code < 0)
    {
      err_code = ERROR_INFO_SET (db_error_code (), DBMS_ERROR_INDICATOR);
      goto prepare_error;
    }

  net_buf_cp_int (net_buf, srv_h_id, NULL);

  result_cache_lifetime = -1;
  net_buf_cp_int (net_buf, result_cache_lifetime, NULL);

  srv_handle->stmt_type = (int) ux_cgw_get_stmt_type (sql_stmt);
  if (srv_handle->stmt_type == CUBRID_STMT_NONE || srv_handle->stmt_type == CUBRID_MAX_STMT_TYPE)
    {
      err_code = ERROR_INFO_SET (db_error_code (), DBMS_ERROR_INDICATOR);
      goto prepare_error;
    }

  net_buf_cp_byte (net_buf, srv_handle->stmt_type);

  net_buf_cp_int (net_buf, num_markers, NULL);

  err_code =
    cgw_prepare_column_list_info_set (srv_handle->cgw_hstmt, flag, srv_handle->stmt_type, client_version, net_buf);

  if (err_code < 0)
    {
      err_code = ERROR_INFO_SET (db_error_code (), DBMS_ERROR_INDICATOR);
      goto prepare_error;
    }

  srv_handle->is_prepared = TRUE;
  srv_handle->num_q_result = 1;
  srv_handle->cur_result = NULL;
  srv_handle->cur_result_index = 0;

  if (flag & CCI_PREPARE_HOLDABLE)
    {
      srv_handle->is_holdable = true;
    }

  return srv_h_id;

prepare_error:
  NET_BUF_ERR_SET (net_buf);

  if (auto_commit_mode == TRUE)
    {
      req_info->need_auto_commit = TRAN_AUTOROLLBACK;
    }

  errors_in_transaction++;

  if (srv_handle)
    {
      hm_srv_handle_free (srv_h_id);
    }
  return err_code;
}

int
ux_cgw_end_tran (int tran_type, bool reset_con_status, bool ddl_audit_log)
{
  int err_code = 0;

  ux_end_tran_cleanup (tran_type);

  T_CGW_HANDLE *cgw_handle = NULL;
  cgw_get_handle (&cgw_handle);
  if (cgw_handle)
    {
      err_code = cgw_endtran (cgw_handle->hdbc, tran_type);
    }

  if (ddl_audit_log && tran_type != CCI_TRAN_COMMIT)
    {
      logddl_write_tran_str (LOGDDL_TRAN_TYPE_ABORT);
    }

  return err_code;
}

int
ux_cgw_auto_commit (T_NET_BUF * net_buf, T_REQ_INFO * req_info)
{
  int err_code;
  int elapsed_sec = 0, elapsed_msec = 0;

  if (req_info->need_auto_commit == TRAN_AUTOCOMMIT)
    {
      cas_log_write (0, false, "auto_commit %s", tran_was_latest_query_committed ()? "(server)" : "(local)");
      err_code = ux_cgw_end_tran (CCI_TRAN_COMMIT, true, false);
      cas_log_write (0, false, "auto_commit %d", err_code);
      logddl_set_msg ("auto_commit %d", err_code);
    }
  else if (req_info->need_auto_commit == TRAN_AUTOROLLBACK)
    {
      cas_log_write (0, false, "auto_commit %s", tran_was_latest_query_aborted ()? "(local)" : "(server)");
      err_code = ux_cgw_end_tran (CCI_TRAN_ROLLBACK, true, false);
      cas_log_write (0, false, "auto_rollback %d", err_code);
      logddl_set_msg ("auto_rollback %d", err_code);
    }
  else
    {
      err_code = ERROR_INFO_SET (CAS_ER_INTERNAL, CAS_ERROR_INDICATOR);
    }

  if (err_code < 0)
    {
      NET_BUF_ERR_SET (net_buf);
      req_info->need_rollback = TRUE;
      errors_in_transaction++;
    }
  else
    {
      req_info->need_rollback = FALSE;
    }

  tran_timeout =
    ut_check_timeout (&tran_start_time, NULL, shm_appl->long_transaction_time, &elapsed_sec, &elapsed_msec);
  if (tran_timeout >= 0)
    {
      as_info->num_long_transactions %= MAX_DIAG_DATA_VALUE;
      as_info->num_long_transactions++;
    }
  if (err_code < 0 || errors_in_transaction > 0)
    {
      cas_log_end (SQL_LOG_MODE_ERROR, elapsed_sec, elapsed_msec);
      errors_in_transaction = 0;
    }
  else
    {
      if (tran_timeout >= 0 || query_timeout >= 0)
	{
	  cas_log_end (SQL_LOG_MODE_TIMEOUT, elapsed_sec, elapsed_msec);
	}
      else
	{
	  cas_log_end (SQL_LOG_MODE_NONE, elapsed_sec, elapsed_msec);
	}
    }
  gettimeofday (&tran_start_time, NULL);
  gettimeofday (&query_start_time, NULL);
  tran_timeout = 0;
  query_timeout = 0;

  return err_code;

  return -1;
}

int
ux_cgw_execute (T_SRV_HANDLE * srv_handle, char flag, int max_col_size, int max_row, int argc, void **argv,
		T_NET_BUF * net_buf, T_REQ_INFO * req_info, CACHE_TIME * clt_cache_time, int *clt_cache_reusable)
{
  int err_code = 0;
  int num_bind = 0;
  SQLLEN row_count = 0;
  T_BROKER_VERSION client_version = req_info->client_version;
  ODBC_BIND_INFO *bind_data_list = NULL;
  T_CGW_HANDLE *cgw_handle = NULL;

  err_code = cgw_get_handle (&cgw_handle);
  if (err_code < 0)
    {
      err_code = ERROR_INFO_SET (db_error_code (), DBMS_ERROR_INDICATOR);
      goto execute_error;
    }

  if (srv_handle->is_prepared == FALSE)
    {
      err_code = cgw_sql_prepare (cgw_handle->hdbc, srv_handle, (SQLCHAR *) srv_handle->sql_stmt);

      if (err_code < 0)
	{
	  err_code = ERROR_INFO_SET (db_error_code (), DBMS_ERROR_INDICATOR);
	  goto execute_error;
	}
    }

  num_bind = srv_handle->num_markers;

  if (num_bind > 0)
    {
      err_code = cgw_make_bind_value (cgw_handle->hdbc, srv_handle, num_bind, argc, argv, &bind_data_list);
      if (err_code < 0)
	{
	  err_code = ERROR_INFO_SET (db_error_code (), DBMS_ERROR_INDICATOR);
	  goto execute_error;
	}
    }

  srv_handle->is_from_current_transaction = true;

  err_code = cgw_set_commit_mode (cgw_handle->hdbc, srv_handle->auto_commit_mode);
  if (err_code != NO_ERROR)
    {
      err_code = ERROR_INFO_SET (db_error_code (), DBMS_ERROR_INDICATOR);
      goto execute_error;
    }

  err_code = cgw_execute (cgw_handle->hdbc, srv_handle, &row_count);
  if (err_code < 0)
    {
      err_code = ERROR_INFO_SET (db_error_code (), DBMS_ERROR_INDICATOR);
      goto execute_error;
    }

  update_query_execution_count (as_info, srv_handle->stmt_type);

  srv_handle->max_col_size = max_col_size;
  srv_handle->num_q_result = 1;
  srv_handle->cur_result_index = 1;
  srv_handle->max_row = max_row;
  /* A fresh execute opens a new result cursor at the beginning.  Reset the fetched-position
   * tracker so the prefetch that immediately follows does not mis-detect a mid-scan rewind
   * (cgw_fetch_result: cursor_pos == 1 && srv_handle->cursor_pos > 1) and close+re-execute the
   * statement with the bind buffers this function frees just below. */
  srv_handle->cursor_pos = 0;
  if (srv_handle->stmt_type == CUBRID_STMT_SELECT)
    {
      srv_handle->total_tuple_count = INT_MAX;	// ODBC does not provide the number of query results, so set to int_max.
    }
  else
    {
      if (row_count == -1)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CGW_UNKNOWN_AFFECTED_ROWS, 0);
	  err_code = ERROR_INFO_SET (db_error_code (), DBMS_ERROR_INDICATOR);
	  goto execute_error;
	}
      else
	{
	  srv_handle->total_tuple_count = (int) row_count;
	}
    }

  if (do_commit_after_execute (*srv_handle))
    {
      req_info->need_auto_commit = TRAN_AUTOCOMMIT;
    }

  if (bind_data_list)
    {
      for (int i = 0; i < num_bind; i++)
	{
	  if (bind_data_list[i].wchar_val)
	    {
	      FREE_MEM (bind_data_list[i].wchar_val);
	    }
	}
      FREE_MEM (bind_data_list);
    }

  err_code = cgw_set_execute_info (srv_handle, net_buf, srv_handle->stmt_type);
  if (err_code != NO_ERROR)
    {
      goto execute_error;
    }

  if (DOES_CLIENT_UNDERSTAND_THE_PROTOCOL (client_version, PROTOCOL_V2))
    {
      int result_cache_lifetime = -1;
      char include_column_info;

      if (srv_handle->num_q_result == 1)
	{
	  include_column_info = 0;
	}
      else
	{
	  include_column_info = 1;
	}

      net_buf_cp_byte (net_buf, include_column_info);

      if (include_column_info == 1)
	{
	  net_buf_cp_int (net_buf, result_cache_lifetime, NULL);
	  net_buf_cp_byte (net_buf, srv_handle->stmt_type);
	  net_buf_cp_int (net_buf, srv_handle->num_markers, NULL);

	  err_code =
	    cgw_prepare_column_list_info_set (srv_handle->cgw_hstmt, flag, srv_handle->stmt_type,
					      client_version, net_buf);
	  if (err_code != NO_ERROR)
	    {
	      err_code = ERROR_INFO_SET (db_error_code (), DBMS_ERROR_INDICATOR);
	      goto execute_error;
	    }
	}
    }

  if (DOES_CLIENT_UNDERSTAND_THE_PROTOCOL (client_version, PROTOCOL_V5))
    {
      net_buf_cp_int (net_buf, shm_shard_id, NULL);
    }

  return err_code;

execute_error:
  NET_BUF_ERR_SET (net_buf);

  if (srv_handle->auto_commit_mode)
    {
      req_info->need_auto_commit = TRAN_AUTOROLLBACK;
    }

  errors_in_transaction++;

  if (bind_data_list)
    {
      FREE_MEM (bind_data_list);
    }
  return err_code;
}

int
ux_cgw_fetch (T_SRV_HANDLE * srv_handle, int cursor_pos, int fetch_count, char fetch_flag, int result_set_index,
	      T_NET_BUF * net_buf, T_REQ_INFO * req_info)
{
  int err_code;
  int fetch_func_index;

  if (srv_handle == NULL)
    {
      err_code = ERROR_INFO_SET (CAS_ER_SRV_HANDLE, CAS_ERROR_INDICATOR);

      cas_log_write (SRV_HANDLE_QUERY_SEQ_NUM (srv_handle), false, "fetch %s%d", "error:", err_info.err_number);

      goto fetch_error;
    }

  if (srv_handle->schema_type < 0)
    {
      fetch_func_index = 0;
    }
  else if (srv_handle->schema_type >= CCI_SCH_FIRST && srv_handle->schema_type <= CCI_SCH_LAST)
    {
      fetch_func_index = srv_handle->schema_type;
    }
  else
    {
      err_code = ERROR_INFO_SET (CAS_ER_SCHEMA_TYPE, CAS_ERROR_INDICATOR);
      goto fetch_error;
    }

  net_buf_cp_int (net_buf, 0, NULL);	/* result code */

  if (fetch_count <= 0)
    {
      fetch_count = 100;
    }

  err_code =
    (*(fetch_func[fetch_func_index])) (srv_handle, cursor_pos, fetch_count, fetch_flag, result_set_index, net_buf,
				       req_info);

  if (err_code < 0)
    {
      goto fetch_error;
    }

  return 0;

fetch_error:
  NET_BUF_ERR_SET (net_buf);

  if (cas_shard_flag == ON
      && (srv_handle != NULL && srv_handle->auto_commit_mode == TRUE && srv_handle->forward_only_cursor == TRUE))
    {
      req_info->need_auto_commit = TRAN_AUTOROLLBACK;
    }

  errors_in_transaction++;
  return err_code;
}

static void
cgw_cleanup_col_bindings (T_SRV_HANDLE * srv_handle)
{
  T_COL_BINDER *col_binding;
  T_COL_BINDER *col_binding_buff;

  if (srv_handle == NULL)
    {
      return;
    }

  if (srv_handle->cgw_hstmt != NULL)
    {
      (void) SQLFreeStmt ((SQLHSTMT) srv_handle->cgw_hstmt, SQL_UNBIND);
    }

  col_binding = (T_COL_BINDER *) srv_handle->cgw_col_binding;
  col_binding_buff = (T_COL_BINDER *) srv_handle->cgw_col_binding_buff;

  if (col_binding != NULL)
    {
      cgw_cleanup_binder (col_binding);
      srv_handle->cgw_col_binding = NULL;
    }

  if (col_binding_buff != NULL)
    {
      cgw_cleanup_binder (col_binding_buff);
      srv_handle->cgw_col_binding_buff = NULL;
    }
}

static int
cgw_fetch_result (T_SRV_HANDLE * srv_handle, int cursor_pos, int fetch_count, char fetch_flag, int result_set_idx,
		  T_NET_BUF * net_buf, T_REQ_INFO * req_info)
{
  T_OBJECT tuple_obj;
  int err_code = 0;
  int num_tuple_msg_offset;
  int num_tuple = 0;
  int net_buf_size;
  SQLLEN row_count = 0;
  char fetch_end_flag = 0;
  SQLSMALLINT num_cols;
  SQLLEN total_row_count = 0;
  T_BROKER_VERSION client_version = req_info->client_version;
  T_CGW_HANDLE *cgw_handle = NULL;
  T_COL_BINDER *col_binding;
  T_COL_BINDER *col_binding_buff;

  if (result_set_idx < 0 || result_set_idx > 1)
    {
      return ERROR_INFO_SET (CAS_ER_NO_MORE_RESULT_SET, CAS_ERROR_INDICATOR);
    }

  err_code = cgw_get_handle (&cgw_handle);
  if (err_code < 0)
    {
      err_code = ERROR_INFO_SET (db_error_code (), DBMS_ERROR_INDICATOR);
      goto fetch_error;
    }

  if (srv_handle->is_cursor_open == false)
    {
      cgw_cleanup_col_bindings (srv_handle);

      err_code = cgw_execute (cgw_handle->hdbc, srv_handle, &row_count);
      if (err_code < 0)
	{
	  err_code = ERROR_INFO_SET (db_error_code (), DBMS_ERROR_INDICATOR);
	  goto fetch_error;
	}
    }
  else if (srv_handle->is_cursor_open && cursor_pos == 1 && srv_handle->cursor_pos > 1)
    {
      err_code = cgw_cursor_close (srv_handle);
      if (err_code < 0)
	{
	  err_code = ERROR_INFO_SET (db_error_code (), DBMS_ERROR_INDICATOR);
	  goto fetch_error;
	}

      cgw_cleanup_col_bindings (srv_handle);

      err_code = cgw_execute (cgw_handle->hdbc, srv_handle, &row_count);
      if (err_code < 0)
	{
	  err_code = ERROR_INFO_SET (db_error_code (), DBMS_ERROR_INDICATOR);
	  goto fetch_error;
	}
    }

  net_buf_cp_int (net_buf, (int) total_row_count, &num_tuple_msg_offset);

  err_code = cgw_get_num_cols (srv_handle->cgw_hstmt, &num_cols);

  if (err_code < 0)
    {
      err_code = ERROR_INFO_SET (db_error_code (), DBMS_ERROR_INDICATOR);
      goto fetch_error;
    }

  col_binding = (T_COL_BINDER *) srv_handle->cgw_col_binding;
  col_binding_buff = (T_COL_BINDER *) srv_handle->cgw_col_binding_buff;

  if (col_binding == NULL)
    {
      err_code = cgw_col_bindings (srv_handle->cgw_hstmt, num_cols,
				   (T_COL_BINDER **) & srv_handle->cgw_col_binding,
				   (T_COL_BINDER **) & srv_handle->cgw_col_binding_buff);
      if (err_code < 0)
	{
	  err_code = ERROR_INFO_SET (db_error_code (), DBMS_ERROR_INDICATOR);
	  goto fetch_error;
	}

      col_binding = (T_COL_BINDER *) srv_handle->cgw_col_binding;
      col_binding_buff = (T_COL_BINDER *) srv_handle->cgw_col_binding_buff;
    }

  if (cas_shard_flag == ON)
    {
      net_buf_size = SHARD_NET_BUF_SIZE;
    }
  else
    {
      net_buf_size = NET_BUF_SIZE;
    }

  num_tuple = 0;

  memset ((char *) &tuple_obj, 0, sizeof (T_OBJECT));

  while (CHECK_NET_BUF_SIZE (net_buf, net_buf_size))
    {				/* currently, don't check fetch_count */

      if (col_binding_buff->is_exist_col_data)
	{
	  err_code = cgw_cur_tuple (net_buf, col_binding_buff, cursor_pos);
	  if (err_code < 0)
	    {
	      goto fetch_error;
	    }
	}
      else
	{
	  err_code = cgw_row_data (srv_handle->cgw_hstmt);
	  if (err_code < 0)
	    {
	      err_code = ERROR_INFO_SET (db_error_code (), DBMS_ERROR_INDICATOR);
	      goto fetch_error;
	    }

	  if (err_code == SQL_NO_DATA_FOUND)
	    {
	      fetch_end_flag = 1;

	      err_code = cgw_cursor_close (srv_handle);
	      if (err_code < 0)
		{
		  err_code = ERROR_INFO_SET (db_error_code (), DBMS_ERROR_INDICATOR);
		  goto fetch_error;
		}

	      cgw_cleanup_col_bindings (srv_handle);

	      if (check_auto_commit_after_getting_result (srv_handle) == true)
		{
		  req_info->need_auto_commit = TRAN_AUTOCOMMIT;
		}
	      break;
	    }

	  err_code = cgw_cur_tuple (net_buf, col_binding, cursor_pos);
	  if (err_code < 0)
	    {
	      goto fetch_error;
	    }
	}

      num_tuple++;
      cursor_pos++;
      if (srv_handle->max_row > 0 && cursor_pos > srv_handle->max_row)
	{
	  if (check_auto_commit_after_getting_result (srv_handle) == true)
	    {
	      err_code = cgw_cursor_close (srv_handle);
	      if (err_code < 0)
		{
		  err_code = ERROR_INFO_SET (db_error_code (), DBMS_ERROR_INDICATOR);
		  goto fetch_error;
		}
	      req_info->need_auto_commit = TRAN_AUTOCOMMIT;
	    }
	  break;
	}

      err_code = cgw_row_data (srv_handle->cgw_hstmt);
      if (err_code < 0)
	{
	  err_code = ERROR_INFO_SET (db_error_code (), DBMS_ERROR_INDICATOR);
	  goto fetch_error;
	}

      if (err_code == SQL_NO_DATA_FOUND)
	{
	  fetch_end_flag = 1;

	  err_code = cgw_cursor_close (srv_handle);
	  if (err_code < 0)
	    {
	      err_code = ERROR_INFO_SET (db_error_code (), DBMS_ERROR_INDICATOR);
	      goto fetch_error;
	    }

	  cgw_cleanup_col_bindings (srv_handle);

	  if (check_auto_commit_after_getting_result (srv_handle) == true)
	    {
	      req_info->need_auto_commit = TRAN_AUTOCOMMIT;
	    }
	  break;
	}

      err_code = cgw_copy_tuple (col_binding, col_binding_buff);
      if (err_code < 0)
	{
	  err_code = ERROR_INFO_SET (db_error_code (), DBMS_ERROR_INDICATOR);
	  goto fetch_error;
	}
    }

  /* Be sure that cursor is closed, if query executed with commit and not holdable. */
  assert (!tran_was_latest_query_committed () || srv_handle->is_holdable == true || err_code == DB_CURSOR_END);

  if (DOES_CLIENT_UNDERSTAND_THE_PROTOCOL (client_version, PROTOCOL_V5))
    {
      net_buf_cp_byte (net_buf, fetch_end_flag);
    }

  net_buf_overwrite_int (net_buf, num_tuple_msg_offset, num_tuple);
  srv_handle->total_tuple_count = num_tuple;
  srv_handle->cursor_pos = cursor_pos;

  return 0;

fetch_error:
  if (srv_handle->is_cursor_open)
    {
      (void) cgw_cursor_close (srv_handle);
    }

  cgw_cleanup_col_bindings (srv_handle);

  return err_code;
}

static int
cgw_prepare_column_list_info_set (SQLHSTMT hstmt, char prepare_flag, char stmt_type,
				  T_BROKER_VERSION client_version, T_NET_BUF * net_buf)
{
  int err_code;
  char updatable_flag = prepare_flag & CCI_PREPARE_UPDATABLE;
  SQLSMALLINT num_cols = 0;
  int num_col_offset = 0;
  int i = 1;
  T_ODBC_COL_INFO col_info;

  if (stmt_type == CUBRID_STMT_SELECT)
    {
      if (updatable_flag)
	{
	  updatable_flag = TRUE;
	}

      net_buf_cp_byte (net_buf, updatable_flag);
      net_buf_cp_int (net_buf, (int) num_cols, &num_col_offset);

      err_code = cgw_get_num_cols (hstmt, &num_cols);
      if (err_code < 0)
	{
	  return ERROR_INFO_SET (db_error_code (), DBMS_ERROR_INDICATOR);
	}

      for (i = 1; i <= num_cols; i++)
	{
	  err_code = cgw_get_col_info (hstmt, i, &col_info);
	  if (err_code < 0)
	    {
	      return ERROR_INFO_SET (db_error_code (), DBMS_ERROR_INDICATOR);
	    }

	  prepare_column_info_set (net_buf, col_info.data_type, col_info.scale, col_info.precision,
				   col_info.charset, col_info.col_name, col_info.default_value,
				   col_info.is_auto_increment, col_info.is_unique_key, col_info.is_primary_key,
				   col_info.is_reverse_index, col_info.is_reverse_unique, col_info.is_foreign_key,
				   col_info.is_shared, col_info.attr_name, col_info.class_name, col_info.is_not_null,
				   client_version);
	}

      net_buf_overwrite_int (net_buf, num_col_offset, (int) num_cols);
    }
  else if (stmt_type == CUBRID_STMT_CALL || stmt_type == CUBRID_STMT_GET_STATS || stmt_type == CUBRID_STMT_EVALUATE)
    {
      updatable_flag = 0;
      net_buf_cp_byte (net_buf, updatable_flag);
      net_buf_cp_int (net_buf, 1, NULL);
      prepare_column_info_set (net_buf, 0, 0, 0, CAS_SCHEMA_DEFAULT_CHARSET, "", "", 0, 0, 0, 0, 0, 0, 0, "", "", 0,
			       client_version);
    }
  else
    {
      updatable_flag = 0;
      net_buf_cp_byte (net_buf, updatable_flag);
      net_buf_cp_int (net_buf, 0, NULL);
    }
  return 0;
}

int
ux_cgw_cursor (int srv_h_id, int offset, int origin, T_NET_BUF * net_buf)
{
  T_SRV_HANDLE *srv_handle;
  int err_code;
  int count;
  char *err_str = NULL;

  srv_handle = hm_find_srv_handle (srv_h_id);
  if (srv_handle == NULL || srv_handle->schema_type >= CCI_SCH_FIRST)
    {
      err_code = ERROR_INFO_SET (CAS_ER_SRV_HANDLE, CAS_ERROR_INDICATOR);

      cas_log_write (SRV_HANDLE_QUERY_SEQ_NUM (srv_handle), false, "cursor srv_h_id %d %s%d", srv_h_id, "error:",
		     err_info.err_number);

      goto cursor_error;
    }

  if (srv_handle->cgw_schema_info != NULL)
    {
      /* schema_info rows are cached on the handle; the position is carried by fetch */
      net_buf_cp_int (net_buf, 0, NULL);	/* result code */
      net_buf_cp_int (net_buf, srv_handle->cgw_schema_info_count, NULL);	/* total row count */
      return 0;
    }

  count = srv_handle->total_tuple_count;

  net_buf_cp_int (net_buf, 0, NULL);	/* result code */
  net_buf_cp_int (net_buf, (int) count, NULL);	/* result msg */

  return 0;

cursor_error:
  NET_BUF_ERR_SET (net_buf);
  errors_in_transaction++;
  FREE_MEM (err_str);
  return err_code;
}

void
ux_cgw_cursor_close (T_SRV_HANDLE * srv_handle)
{
  int idx = 0;

  if (srv_handle == NULL)
    {
      return;
    }

  idx = srv_handle->cur_result_index - 1;
  if (idx < 0)
    {
      return;
    }

  cgw_cursor_close (srv_handle);
  cgw_cleanup_col_bindings (srv_handle);
}

void
ux_cgw_free_stmt (T_SRV_HANDLE * srv_handle)
{
  if (srv_handle == NULL)
    {
      return;
    }

  if (srv_handle->is_cursor_open)
    {
      (void) cgw_cursor_close (srv_handle);
    }

  cgw_cleanup_col_bindings (srv_handle);
  cgw_free_stmt (srv_handle);
}

static char
ux_cgw_get_stmt_type (char *stmt)
{
  char stmt_type = CUBRID_STMT_NONE;
  const char *comment_start = strstr (stmt, "/*");
  const char *comment_end = NULL;

  while (comment_start)
    {
      const char *comment_end = strstr (comment_start, "*/");
      if (comment_end)
	{
	  char *comment_str = NULL;
	  const char *dblink_start = NULL;

	  comment_str = (char *) MALLOC ((comment_end - comment_start) + 1);
	  if (comment_str == NULL)
	    {
	      ERROR_INFO_SET (CAS_ER_NO_MORE_MEMORY, CAS_ERROR_INDICATOR);
	      return CUBRID_STMT_NONE;
	    }

	  strncpy (comment_str, comment_start + 2, comment_end - comment_start - 2);
	  comment_str[comment_end - comment_start - 2] = '\0';

	  dblink_start = strstr (comment_str, DBLINK_HINT);
	  if (dblink_start)
	    {
	      char *type_name = NULL;
	      size_t dblink_hint_len = 0;

	      dblink_hint_len = (strlen (dblink_start) - strlen (DBLINK_HINT));

	      type_name = (char *) MALLOC (dblink_hint_len + 1);
	      if (type_name == NULL)
		{
		  ERROR_INFO_SET (CAS_ER_NO_MORE_MEMORY, CAS_ERROR_INDICATOR);
		  FREE_MEM (comment_str);
		  return CUBRID_STMT_NONE;
		}

	      strncpy (type_name, dblink_start + strlen (DBLINK_HINT), dblink_hint_len);
	      type_name[dblink_hint_len] = '\0';

	      ut_trim (type_name);
	      ut_tolower (type_name);

	      stmt_type = get_stmt_type (type_name);

	      FREE_MEM (comment_str);
	      FREE_MEM (type_name);
	      break;
	    }

	  FREE_MEM (comment_str);

	  comment_start = strstr (comment_end, "/*");
	}
    }

  if (stmt_type == CUBRID_STMT_NONE || stmt_type == CUBRID_MAX_STMT_TYPE)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CGW_INVALID_DBLINT_HINT, 1, stmt);
    }
  return stmt_type;
}

int
get_cgw_tuple_count (T_SRV_HANDLE * srv_handle)
{
  return srv_handle->total_tuple_count;
}

static bool
do_commit_after_execute (const t_srv_handle & server_handle)
{
  if (server_handle.auto_commit_mode != TRUE)
    {
      return false;
    }

  if (!has_stmt_result_set (server_handle.stmt_type))
    {
      return true;
    }

  return false;
}

static int
fetch_not_supported (T_SRV_HANDLE * srv_handle, int cursor_pos, int fetch_count, char fetch_flag, int result_set_idx,
		     T_NET_BUF * net_buf, T_REQ_INFO * req_info)
{
  return ERROR_INFO_SET (CAS_ER_NOT_IMPLEMENTED, CAS_ERROR_INDICATOR);
}

int
ux_cgw_is_database_connected (void)
{
  return cgw_is_database_connected () == 0 ? 1 : 0;
}

/* schema_info row value writers - the value layout of add_res_data_* with no ext type */
static void
cgw_schema_res_string (T_NET_BUF * net_buf, const char *str)
{
  int len = (str == NULL) ? 0 : (int) strlen (str);

  net_buf_cp_int (net_buf, len + 1, NULL);	/* NULL terminator */
  net_buf_cp_str (net_buf, (str != NULL) ? str : "", len);
  net_buf_cp_byte (net_buf, '\0');
}

static void
cgw_schema_res_short (T_NET_BUF * net_buf, short value)
{
  net_buf_cp_int (net_buf, NET_SIZE_SHORT, NULL);
  net_buf_cp_short (net_buf, value);
}

static void
cgw_schema_res_int (T_NET_BUF * net_buf, int value)
{
  net_buf_cp_int (net_buf, NET_SIZE_INT, NULL);
  net_buf_cp_int (net_buf, value, NULL);
}

/*
 * cgw_schema_attr_filter () - keep only the attributes the client asked for
 *   return: number of rows left at the head of attrs
 *   attrs(in/out): attribute rows, compacted in place
 *   count(in): number of rows
 *   attr_name(in): requested attribute name or pattern, NULL when not given
 *   flag(in): CCI_ATTR_NAME_PATTERN_MATCH selects pattern matching
 *
 * Note: mirrors sch_attr_info () of the regular CAS - with the pattern flag the name
 *   is a LIKE pattern and no name means no filter at all, otherwise the match is
 *   exact and a missing name matches nothing.  Remote identifiers keep their own
 *   case, so the exact match is case insensitive like the LIKE one.
 */
static int
cgw_schema_attr_filter (T_CGW_SCHEMA_ATTR * attrs, int count, char *attr_name, char flag)
{
  bool is_pattern = (flag & CCI_ATTR_NAME_PATTERN_MATCH) != 0;
  int i, kept = 0;

  if (is_pattern && attr_name == NULL)
    {
      return count;
    }

  for (i = 0; i < count; i++)
    {
      bool match;

      if (is_pattern)
	{
	  match = (str_like (attrs[i].attr_name, attr_name, '\\') == 1);
	}
      else
	{
	  match = (attr_name != NULL && strcasecmp (attrs[i].attr_name, attr_name) == 0);
	}

      if (match)
	{
	  if (kept != i)
	    {
	      attrs[kept] = attrs[i];
	    }
	  kept++;
	}
    }

  return kept;
}

/*
 * ux_cgw_schema_info () - CAS_FC_SCHEMA_INFO for the gateway
 *   return: srv handle id, or an error code with the error already on net_buf
 *
 * Note: only CCI_SCH_ATTRIBUTE is served (the DBLink remote-table schema, invisible
 *   columns included).  The response mirrors ux_schema_info (): srv_h_id, row count,
 *   schema_attr_meta () column meta; rows are served by ux_cgw_fetch_schema_attribute ().
 */
int
ux_cgw_schema_info (int schema_type, char *table_name, char *attr_name, char flag, T_NET_BUF * net_buf,
		    T_REQ_INFO * req_info, unsigned int query_seq_num)
{
  int srv_h_id = -1, err_code;
  T_SRV_HANDLE *srv_handle = NULL;
  T_CGW_HANDLE *cgw_handle = NULL;
  T_CGW_SCHEMA_ATTR *attrs = NULL;
  int num_attrs = 0;

  if (schema_type != CCI_SCH_ATTRIBUTE)
    {
      err_code = ERROR_INFO_SET (CAS_ER_NOT_IMPLEMENTED, CAS_ERROR_INDICATOR);
      goto schema_info_error;
    }

  if (table_name == NULL || (flag & CCI_CLASS_NAME_PATTERN_MATCH))
    {
      /* the ODBC catalog lookup below matches one table literally - it escapes the
       * wildcards of the name it is given - so a class-name pattern, or no name at all,
       * cannot be answered.  Report that instead of returning the columns of whatever
       * single table happens to match the pattern text. */
      err_code = ERROR_INFO_SET (CAS_ER_NOT_IMPLEMENTED, CAS_ERROR_INDICATOR);
      goto schema_info_error;
    }

  if (cgw_get_handle (&cgw_handle) < 0)
    {
      err_code = ERROR_INFO_SET (db_error_code (), DBMS_ERROR_INDICATOR);
      goto schema_info_error;
    }

  srv_h_id = hm_new_srv_handle (&srv_handle, query_seq_num);
  if (srv_h_id < 0)
    {
      err_code = ERROR_INFO_SET (srv_h_id, CAS_ERROR_INDICATOR);
      goto schema_info_error;
    }

  if (cgw_schema_info_attribute (cgw_handle->hdbc, table_name, &attrs, &num_attrs) < 0)
    {
      err_code = ERROR_INFO_SET (CAS_ER_INTERNAL, CAS_ERROR_INDICATOR);
      goto schema_info_error;
    }

  num_attrs = cgw_schema_attr_filter (attrs, num_attrs, attr_name, flag);

  srv_handle->schema_type = -1;	/* not a CAS schema session; freed via cgw_schema_info */
  srv_handle->cgw_schema_info = (void *) attrs;
  srv_handle->cgw_schema_info_count = num_attrs;
  srv_handle->cgw_schema_class_name = strdup (table_name);
  if (srv_handle->cgw_schema_class_name == NULL)
    {
      err_code = ERROR_INFO_SET (CAS_ER_NO_MORE_MEMORY, CAS_ERROR_INDICATOR);
      goto schema_info_error;
    }

  net_buf_cp_int (net_buf, srv_h_id, NULL);	/* result code */
  net_buf_cp_int (net_buf, num_attrs, NULL);	/* result count */
  schema_attr_meta (net_buf);

  return srv_h_id;

schema_info_error:
  NET_BUF_ERR_SET (net_buf);
  if (srv_handle)
    {
      hm_srv_handle_free (srv_h_id);
    }
  return err_code;
}

/*
 * ux_cgw_fetch_schema_attribute () - serve rows collected by ux_cgw_schema_info ()
 *   in the CCI_SCH_ATTRIBUTE tuple layout of fetch_attribute ()
 */
int
ux_cgw_fetch_schema_attribute (T_SRV_HANDLE * srv_handle, int cursor_pos, int fetch_count, T_NET_BUF * net_buf,
			       T_REQ_INFO * req_info)
{
  T_CGW_SCHEMA_ATTR *attrs = (T_CGW_SCHEMA_ATTR *) srv_handle->cgw_schema_info;
  T_BROKER_VERSION client_version = req_info->client_version;
  T_OBJECT dummy_obj;
  int num_tuple_msg_offset;
  int num_tuple = 0;
  char fetch_end_flag = 0;
  short domain;
  int i;

  if (attrs == NULL || cursor_pos <= 0)
    {
      int err_code = ERROR_INFO_SET (CAS_ER_INTERNAL, CAS_ERROR_INDICATOR);
      NET_BUF_ERR_SET (net_buf);
      return err_code;
    }

  if (fetch_count <= 0)
    {
      fetch_count = 100;
    }

  net_buf_cp_int (net_buf, 0, NULL);	/* result code */
  net_buf_cp_int (net_buf, 0, &num_tuple_msg_offset);	/* tuple count, back-patched */
  memset (&dummy_obj, 0, sizeof (dummy_obj));

  for (i = cursor_pos; i <= srv_handle->cgw_schema_info_count && num_tuple < fetch_count; i++, num_tuple++)
    {
      T_CGW_SCHEMA_ATTR *at = &attrs[i - 1];

      net_buf_cp_int (net_buf, i, NULL);	/* tuple index */
      net_buf_cp_object (net_buf, &dummy_obj);

      /* 1. attr name */
      cgw_schema_res_string (net_buf, at->attr_name);
      /* 2. domain - encode_ext_type_to_short () layout, no collection bits */
      if (DOES_CLIENT_UNDERSTAND_THE_PROTOCOL (client_version, PROTOCOL_V7))
	{
	  domain = (short) (((short) CAS_TYPE_FIRST_BYTE_PROTOCOL_MASK << 8) | (unsigned char) at->cci_type);
	}
      else
	{
	  domain = (short) at->cci_type;
	}
      cgw_schema_res_short (net_buf, domain);
      /* 3. scale */
      cgw_schema_res_short (net_buf, at->scale);
      /* 4. precision */
      cgw_schema_res_int (net_buf, at->precision);
      /* 5. indexed, 6. non_null, 7. shared, 8. unique - not provided by the remote */
      cgw_schema_res_short (net_buf, 0);
      cgw_schema_res_short (net_buf, 0);
      cgw_schema_res_short (net_buf, 0);
      cgw_schema_res_short (net_buf, 0);
      /* 9. default */
      cgw_schema_res_string (net_buf, "");
      /* 10. order - the remote definition order, which the attribute filter does not
       * renumber; the row index would report a different ordinal than a regular CAS */
      cgw_schema_res_int (net_buf, at->attr_order);
      /* 11. class name, 12. source class - the remote table has no superclass, so the
       * source class is the table itself, as fetch_attribute () reports for a
       * non-inherited attribute */
      cgw_schema_res_string (net_buf, srv_handle->cgw_schema_class_name);
      cgw_schema_res_string (net_buf, srv_handle->cgw_schema_class_name);
      /* 13. is_key */
      cgw_schema_res_short (net_buf, 0);
      /* 14. remarks */
      cgw_schema_res_string (net_buf, "");

      if (DOES_CLIENT_UNDERSTAND_THE_PROTOCOL (client_version, PROTOCOL_V13))
	{
	  /* 15. is invisible */
	  cgw_schema_res_short (net_buf, (short) at->is_invisible);
	  /* 16. ext domain - a remote DBMS has no collection type that ODBC can express
	   * (cgw_odbc_type_to_cci_u_type () never returns one), so this is DOMAIN itself */
	  cgw_schema_res_short (net_buf, (short) at->cci_type);
	  /* 17. codeset - the same value the prepare path reports via T_ODBC_COL_INFO */
	  cgw_schema_res_short (net_buf, (short) at->charset);
	}
    }

  if (i > srv_handle->cgw_schema_info_count)
    {
      fetch_end_flag = 1;
    }

  net_buf_overwrite_int (net_buf, num_tuple_msg_offset, num_tuple);

  if (DOES_CLIENT_UNDERSTAND_THE_PROTOCOL (client_version, PROTOCOL_V5))
    {
      net_buf_cp_byte (net_buf, fetch_end_flag);
    }

  return 0;
}
