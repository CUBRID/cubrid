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

#if defined(WINDOWS)
#include <winsock2.h>
#include <windows.h>
#include <sys/timeb.h>
#include <process.h>
#else
#include <unistd.h>
#include <sys/time.h>
#endif
#include <assert.h>

#include "cas_common.h"
#include "cas_common_main.h"
#include "cas_common_vars.h"
#include "cas_net_buf.h"
#include "cas_log.h"
#include "cas_handle.h"
#include "cas_util.h"
#include "cas_common_function.h"
#include "perf_monitor.h"
#include "cas_sql_log2.h"
#include "ddl_log.h"
#include "cas_error.h"

#include "cas_cgw_function.h"
#include "cas_cgw_execute.h"
#include "cas_cgw_odbc.h"

/* ========================================================================
 * Forward Function Declarations
 * ======================================================================== */
static FN_RETURN fn_cgw_prepare_internal (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf,
					  T_REQ_INFO * req_info, int *ret_srv_h_id);
static FN_RETURN fn_cgw_execute_internal (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf,
					  T_REQ_INFO * req_info, int *prepared_srv_h_id);


FN_RETURN
fn_cgw_end_tran (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf, T_REQ_INFO * req_info)
{
  int tran_type;
  int err_code;
  int elapsed_sec = 0, elapsed_msec = 0;
  struct timeval end_tran_begin, end_tran_end;

  int timeout;

  if (argc < 1)
    {
      ERROR_INFO_SET (CAS_ER_ARGS, CAS_ERROR_INDICATOR);
      NET_BUF_ERR_SET (net_buf);
      return FN_KEEP_CONN;
    }

  net_arg_get_char (tran_type, argv[0]);
  if (tran_type != CCI_TRAN_COMMIT && tran_type != CCI_TRAN_ROLLBACK)
    {
      ERROR_INFO_SET (CAS_ER_TRAN_TYPE, CAS_ERROR_INDICATOR);
      NET_BUF_ERR_SET (net_buf);
      return FN_KEEP_CONN;
    }

  cas_log_write (0, false, "end_tran %s", get_tran_type_str (tran_type));

  gettimeofday (&end_tran_begin, NULL);

  err_code = ux_cgw_end_tran ((char) tran_type, false, false);

  if ((tran_type == CCI_TRAN_ROLLBACK) && (req_info->client_version < CAS_MAKE_VER (8, 2, 0)))
    {
      /* For backward compatibility */
      cas_send_result_flag = FALSE;
    }

  gettimeofday (&end_tran_end, NULL);
  ut_timeval_diff (&end_tran_begin, &end_tran_end, &elapsed_sec, &elapsed_msec);

  cas_log_write (0, false, "end_tran %s%d time %d.%03d%s", err_code < 0 ? "error:" : "", err_info.err_number,
		 elapsed_sec, elapsed_msec, get_error_log_eids (err_info.err_number));

  logddl_write_tran_str ("end_tran %s%d %s", err_code < 0 ? "error:" : "", err_info.err_number,
			 get_tran_type_str (tran_type));

  if (err_code < 0)
    {
      NET_BUF_ERR_SET (net_buf);
      req_info->need_rollback = TRUE;
    }
  else
    {
      net_buf_cp_int (net_buf, 0, NULL);
      req_info->need_rollback = FALSE;
    }

  timeout =
    ut_check_timeout (&tran_start_time, &end_tran_end, shm_appl->long_transaction_time, &elapsed_sec, &elapsed_msec);
  if (timeout >= 0)
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
      if (timeout >= 0 || query_timeout >= 0)
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

  assert (as_info->con_status == CON_STATUS_IN_TRAN);
  as_info->con_status = CON_STATUS_OUT_TRAN;
  as_info->transaction_start_time = (time_t) 0;
  if (as_info->cas_log_reset)
    {
      cas_log_reset (broker_name);
    }
  if (as_info->cas_slow_log_reset)
    {
      cas_slow_log_reset (broker_name);
    }
  if (shm_appl->sql_log2 != as_info->cur_sql_log2)
    {
      sql_log2_end (false);
      as_info->cur_sql_log2 = shm_appl->sql_log2;
      sql_log2_init (broker_name, shm_as_index, as_info->cur_sql_log2, true);
    }

  if (cgw_is_database_connected () < 0)
    {
      cas_log_debug (ARG_FILE_LINE, "fn_end_tran: cgw_is_database_connected()");
      return FN_CLOSE_CONN;
    }
  else if (restart_is_needed () || as_info->reset_flag == TRUE)
    {
      cas_log_debug (ARG_FILE_LINE, "fn_end_tran: restart_is_needed() || reset_flag");
      return FN_KEEP_SESS;
    }
  return FN_KEEP_CONN;
}

FN_RETURN
fn_cgw_prepare (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf, T_REQ_INFO * req_info)
{
  return (fn_cgw_prepare_internal (sock_fd, argc, argv, net_buf, req_info, NULL));
}

static FN_RETURN
fn_cgw_prepare_internal (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf, T_REQ_INFO * req_info,
			 int *ret_srv_h_id)
{
  char *sql_stmt;
  char flag;
  char auto_commit_mode;
  int sql_size;
  int srv_h_id;
  T_SRV_HANDLE *srv_handle;
  int i;

  if (argc < 2)
    {
      ERROR_INFO_SET (CAS_ER_ARGS, CAS_ERROR_INDICATOR);
      NET_BUF_ERR_SET (net_buf);
      return FN_KEEP_CONN;
    }

  net_arg_get_str (&sql_stmt, &sql_size, argv[0]);

  net_arg_get_char (flag, argv[1]);
  if (argc > 2)
    {
      net_arg_get_char (auto_commit_mode, argv[2]);
      if (cas_shard_flag == OFF)
	{
	  for (i = 3; i < argc; i++)
	    {
	      int deferred_close_handle;
	      net_arg_get_int (&deferred_close_handle, argv[i]);
	      cas_log_write (0, true, "close_req_handle srv_h_id %d", deferred_close_handle);
	      hm_srv_handle_free (deferred_close_handle);
	    }
	}
    }
  else
    {
      auto_commit_mode = FALSE;
    }

  logddl_set_commit_mode (auto_commit_mode);

  gettimeofday (&query_start_time, NULL);
  query_timeout = 0;

  cas_log_write_nonl (query_seq_num_next_value (), false, "prepare %d ", flag);
  cas_log_compile_begin_write_query_string (sql_stmt, sql_size - 1, NULL);

  SQL_LOG2_COMPILE_BEGIN (as_info->cur_sql_log2, ((const char *) sql_stmt));

  /* append query string to as_info->log_msg */
  if (sql_stmt)
    {
      char *s, *t;
      size_t l;

      for (s = as_info->log_msg, l = 0; *s && l < SHM_LOG_MSG_SIZE - 1; s++, l++)
	{
	  /* empty body */
	}
      *s++ = ' ';
      l++;
      for (t = sql_stmt; *t && l < SHM_LOG_MSG_SIZE - 1; s++, t++, l++)
	{
	  *s = *t;
	}
      *s = '\0';
    }

  srv_h_id = ux_cgw_prepare (sql_stmt, flag, auto_commit_mode, net_buf, req_info, query_seq_num_current_value ());

  if (ret_srv_h_id != NULL)
    {
      /* this ret_srv_h_id used by following fn_execute_internal() function */
      *ret_srv_h_id = srv_h_id;
    }

  srv_handle = hm_find_srv_handle (srv_h_id);
  cas_log_compile_end_write_query_string (sql_stmt, sql_size - 1, NULL);
  cas_log_write (query_seq_num_current_value (), false, "prepare srv_h_id %s%d%s%s", (srv_h_id < 0) ? "error:" : "",
		 (srv_h_id < 0) ? err_info.err_number : srv_h_id, (srv_handle != NULL
								   && srv_handle->use_plan_cache) ? " (PC)" : "",
		 get_error_log_eids (err_info.err_number));
  logddl_set_err_code (err_info.err_number);


  if (srv_h_id < 0)
    {
      update_error_query_count (as_info, &err_info);
    }


  return FN_KEEP_CONN;
}

FN_RETURN
fn_cgw_execute (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf, T_REQ_INFO * req_info)
{
  FN_RETURN ret = fn_cgw_execute_internal (sock_fd, argc, argv, net_buf, req_info, NULL);

  return ret;
}

static FN_RETURN
fn_cgw_execute_internal (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf, T_REQ_INFO * req_info,
			 int *prepared_srv_h_id)
{
  int srv_h_id;
  char flag;
  int max_col_size;
  int bind_value_index;
  int max_row = 0;
  int ret_code;
  int param_mode_size = 0;
  char forward_only_cursor = 0;
  char auto_commit_mode = 0;
  char *param_mode = NULL;
  T_SRV_HANDLE *srv_handle;
  const char *exec_func_name;
  int argc_mod_2;
  int (*ux_exec_func) (T_SRV_HANDLE *, char, int, int, int, void **, T_NET_BUF *, T_REQ_INFO *, CACHE_TIME *, int *);
  char fetch_flag = 0;
  CACHE_TIME clt_cache_time, *clt_cache_time_ptr;
  int client_cache_reusable = FALSE;
  int elapsed_sec = 0, elapsed_msec = 0;
  struct timeval exec_begin, exec_end;
  int app_query_timeout;
  bool client_supports_query_timeout = false;
  char *eid_string;
  int err_number_execute;
  int arg_idx = 0;

  bind_value_index = 9;
  /*
   * query timeout is transferred from a driver only if protocol version 1
   * or above.
   */
  if (req_info->client_version >= CAS_PROTO_MAKE_VER (PROTOCOL_V1))
    {
      client_supports_query_timeout = true;
      bind_value_index++;
    }

  argc_mod_2 = bind_value_index % 2;

  if ((argc < bind_value_index) || (argc % 2 != argc_mod_2))
    {
      ERROR_INFO_SET (CAS_ER_ARGS, CAS_ERROR_INDICATOR);
      NET_BUF_ERR_SET (net_buf);
      return FN_KEEP_CONN;
    }

  if (prepared_srv_h_id != NULL)
    {
      srv_h_id = *prepared_srv_h_id;
    }
  else
    {
      net_arg_get_int (&srv_h_id, argv[arg_idx++]);
    }
  srv_handle = hm_find_srv_handle (srv_h_id);

  if (srv_handle == NULL)
    {
      ERROR_INFO_SET (CAS_ER_SRV_HANDLE, CAS_ERROR_INDICATOR);

      cas_log_write (SRV_HANDLE_QUERY_SEQ_NUM (srv_handle), false, "execute_internal srv_h_id %d %s%d",
		     srv_h_id, "error:", err_info.err_number);

      NET_BUF_ERR_SET (net_buf);
      return FN_KEEP_CONN;
    }

  net_arg_get_char (flag, argv[arg_idx++]);
  net_arg_get_int (&max_col_size, argv[arg_idx++]);
  net_arg_get_int (&max_row, argv[arg_idx++]);
  net_arg_get_str (&param_mode, &param_mode_size, argv[arg_idx++]);
  if (prepared_srv_h_id != NULL)
    {
      if (srv_handle->stmt_type == CUBRID_STMT_SELECT)
	{
	  fetch_flag = 1;
	}
      else
	{
	  fetch_flag = 0;
	}

      if (srv_handle->auto_commit_mode != 0)
	{
	  auto_commit_mode = true;
	  forward_only_cursor = true;
	}
      else
	{
	  auto_commit_mode = false;
	  forward_only_cursor = false;
	}
    }
  else
    {
      /* PROTOCOL_V2 is used only 9.0.0 */
      if (DOES_CLIENT_MATCH_THE_PROTOCOL (req_info->client_version, PROTOCOL_V2))
	{
	  if (srv_handle->stmt_type == CUBRID_STMT_SELECT)
	    {
	      fetch_flag = 1;
	    }
	  else
	    {
	      fetch_flag = 0;
	    }

	  arg_idx++;		/* skip fetch_flag from driver */
	}
      else
	{
	  net_arg_get_char (fetch_flag, argv[arg_idx++]);
	}

      net_arg_get_char (auto_commit_mode, argv[arg_idx++]);
      net_arg_get_char (forward_only_cursor, argv[arg_idx++]);
    }

  clt_cache_time_ptr = &clt_cache_time;
  net_arg_get_cache_time (clt_cache_time_ptr, argv[arg_idx++]);

  if (client_supports_query_timeout == true)
    {
      net_arg_get_int (&app_query_timeout, argv[arg_idx++]);

      if (!DOES_CLIENT_UNDERSTAND_THE_PROTOCOL (req_info->client_version, PROTOCOL_V2))
	{
	  /* protocol version v1 driver send query timeout in second */
	  app_query_timeout *= 1000;
	}
    }
  else
    {
      app_query_timeout = 0;
    }


  if (shm_appl->max_string_length >= 0)
    {
      if (max_col_size <= 0 || max_col_size > shm_appl->max_string_length)
	max_col_size = shm_appl->max_string_length;
    }

  srv_handle->auto_commit_mode = auto_commit_mode;
  srv_handle->forward_only_cursor = forward_only_cursor;
  logddl_set_commit_mode (auto_commit_mode);

  exec_func_name = "execute";
  ux_exec_func = ux_cgw_execute;

  if (srv_handle->is_pooled)
    {
      gettimeofday (&query_start_time, NULL);
      query_timeout = 0;
    }

  cas_log_write_nonl (SRV_HANDLE_QUERY_SEQ_NUM (srv_handle), false, "%s srv_h_id %d ", exec_func_name, srv_h_id);
  if (srv_handle->sql_stmt != NULL)
    {
      if (srv_handle->session == NULL)
	{
	  cas_log_write_query_string (srv_handle->sql_stmt, (int) strlen (srv_handle->sql_stmt), NULL);
	}
      else
	{
	  HIDE_PWD_INFO t_pwd_info;
	  INIT_HIDE_PASSWORD_INFO (&t_pwd_info);
	  cas_log_write_query_string (srv_handle->sql_stmt, (int) strlen (srv_handle->sql_stmt), &t_pwd_info);
	}
    }
  cas_log_debug (ARG_FILE_LINE, "%s%s", auto_commit_mode ? "auto_commit_mode " : "",
		 forward_only_cursor ? "forward_only_cursor " : "");

  if (as_info->cur_sql_log_mode != SQL_LOG_MODE_NONE)
    {
      cas_common_bind_value_log (NULL, bind_value_index, argc, argv, param_mode_size, param_mode,
				 SRV_HANDLE_QUERY_SEQ_NUM (srv_handle), false, INTL_CODESET_UTF8);
    }

  /* append query string to as_info->log_msg */
  if (srv_handle->sql_stmt)
    {
      char *s, *t;
      size_t l;

      for (s = as_info->log_msg, l = 0; *s && l < SHM_LOG_MSG_SIZE - 1; s++, l++)
	{
	  /* empty body */
	}
      *s++ = ' ';
      l++;
      for (t = srv_handle->sql_stmt; *t && l < SHM_LOG_MSG_SIZE - 1; s++, t++, l++)
	{
	  *s = *t;
	}
      *s = '\0';
    }

  gettimeofday (&exec_begin, NULL);

  ret_code =
    (*ux_exec_func) (srv_handle, flag, max_col_size, max_row, argc - bind_value_index, argv + bind_value_index, net_buf,
		     req_info, clt_cache_time_ptr, &client_cache_reusable);
  gettimeofday (&exec_end, NULL);
  ut_timeval_diff (&exec_begin, &exec_end, &elapsed_sec, &elapsed_msec);
  eid_string = get_error_log_eids (err_info.err_number);
  err_number_execute = err_info.err_number;
  logddl_set_err_code (err_info.err_number);

  if (fetch_flag && ret_code >= 0 && client_cache_reusable == FALSE)
    {
      if (srv_handle->stmt_type == CUBRID_STMT_SELECT)
	{
	  ux_cgw_fetch (srv_handle, 1, 50, 0, 0, net_buf, req_info);
	}
    }

  cas_log_write (SRV_HANDLE_QUERY_SEQ_NUM (srv_handle), false, "%s %s%d tuple %d time %d.%03d%s%s%s", exec_func_name,
		 (ret_code < 0) ? "error:" : "", err_number_execute,
		 (get_cgw_tuple_count (srv_handle) == INT_MAX) ? -1 : get_cgw_tuple_count (srv_handle), elapsed_sec,
		 elapsed_msec, (client_cache_reusable == TRUE) ? " (CC)" : "",
		 (srv_handle->use_query_cache == true) ? " (QC)" : "", eid_string);

  query_timeout =
    ut_check_timeout (&query_start_time, &exec_end, shm_appl->long_query_time, &elapsed_sec, &elapsed_msec);
  if (query_timeout >= 0 || ret_code < 0)
    {
      if (query_timeout >= 0)
	{
	  as_info->num_long_queries %= MAX_DIAG_DATA_VALUE;
	  as_info->num_long_queries++;
	}

      if (ret_code < 0)
	{
	  update_error_query_count (as_info, &err_info);
	}

      if (as_info->cur_slow_log_mode == SLOW_LOG_MODE_ON)
	{
	  cas_slow_log_write (&query_start_time, SRV_HANDLE_QUERY_SEQ_NUM (srv_handle), false, "%s srv_h_id %d ",
			      exec_func_name, srv_h_id);
	  if (srv_handle->sql_stmt != NULL)
	    {
	      HIDE_PWD_INFO t_pwd_info;
	      INIT_HIDE_PASSWORD_INFO (&t_pwd_info);
	      cas_slow_log_write_query_string (srv_handle->sql_stmt, (int) strlen (srv_handle->sql_stmt), &t_pwd_info);

	      cas_common_bind_value_log (&query_start_time, bind_value_index, argc, argv, param_mode_size, param_mode,
					 SRV_HANDLE_QUERY_SEQ_NUM (srv_handle), true, INTL_CODESET_UTF8);
	    }
	  cas_slow_log_write (NULL, SRV_HANDLE_QUERY_SEQ_NUM (srv_handle), false,
			      "%s %s%d tuple %d time %d.%03d%s%s%s\n", exec_func_name, (ret_code < 0) ? "error:" : "",
			      err_number_execute,
			      (get_cgw_tuple_count (srv_handle) == INT_MAX) ? -1 : get_cgw_tuple_count (srv_handle),
			      elapsed_sec, elapsed_msec, (client_cache_reusable == TRUE) ? " (CC)" : "",
			      (srv_handle->use_query_cache == true) ? " (QC)" : "", eid_string);
	  cas_slow_log_end ();
	}
    }

  /* set is_pooled */
  if (as_info->cur_statement_pooling)
    {
      srv_handle->is_pooled = TRUE;
    }

  return FN_KEEP_CONN;
}

FN_RETURN
fn_cgw_close_req_handle (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf, T_REQ_INFO * req_info)
{
  int srv_h_id;
  T_SRV_HANDLE *srv_handle;
  char auto_commit_mode = FALSE;

  if (argc < 1)
    {
      ERROR_INFO_SET (CAS_ER_ARGS, CAS_ERROR_INDICATOR);
      NET_BUF_ERR_SET (net_buf);
      return FN_KEEP_CONN;
    }

  net_arg_get_int (&srv_h_id, argv[0]);

  if (argc > 1)
    {
      net_arg_get_char (auto_commit_mode, argv[1]);
    }

  srv_handle = hm_find_srv_handle (srv_h_id);
  if (auto_commit_mode == TRUE)
    {
      req_info->need_auto_commit = TRAN_AUTOCOMMIT;
    }

  cas_log_write (SRV_HANDLE_QUERY_SEQ_NUM (srv_handle), false, "close_req_handle srv_h_id %d", srv_h_id);

  hm_srv_handle_free (srv_h_id);

  net_buf_cp_int (net_buf, 0, NULL);	/* res code */

  return FN_KEEP_CONN;
}

FN_RETURN
fn_cgw_cursor (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf, T_REQ_INFO * req_info)
{
  int srv_h_id;
  int offset;
  char origin;

  if (argc < 3)
    {
      ERROR_INFO_SET (CAS_ER_ARGS, CAS_ERROR_INDICATOR);
      NET_BUF_ERR_SET (net_buf);
      return FN_KEEP_CONN;
    }

  net_arg_get_int (&srv_h_id, argv[0]);
  net_arg_get_int (&offset, argv[1]);
  net_arg_get_char (origin, argv[2]);

  ux_cgw_cursor (srv_h_id, offset, origin, net_buf);

  return FN_KEEP_CONN;
}

FN_RETURN
fn_cgw_get_fetch (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf, T_REQ_INFO * req_info)
{
  int srv_h_id;
  int cursor_pos;
  int fetch_count;
  int func_args;
  char fetch_flag;
  int result_set_index;
  T_SRV_HANDLE *srv_handle;

  func_args = 5;

  if (argc < func_args)
    {
      ERROR_INFO_SET (CAS_ER_ARGS, CAS_ERROR_INDICATOR);
      NET_BUF_ERR_SET (net_buf);
      return FN_KEEP_CONN;
    }

  net_arg_get_int (&srv_h_id, argv[0]);
  net_arg_get_int (&cursor_pos, argv[1]);
  net_arg_get_int (&fetch_count, argv[2]);
  net_arg_get_char (fetch_flag, argv[3]);
  net_arg_get_int (&result_set_index, argv[4]);

  srv_handle = hm_find_srv_handle (srv_h_id);

  if (srv_handle == NULL)
    {
      ERROR_INFO_SET (CAS_ER_SRV_HANDLE, CAS_ERROR_INDICATOR);

      cas_log_write (SRV_HANDLE_QUERY_SEQ_NUM (srv_handle), false, "fn_fetch srv_h_id %d %s%d",
		     srv_h_id, "error:", err_info.err_number);

      NET_BUF_ERR_SET (net_buf);
      return FN_KEEP_CONN;
    }

  cas_log_write (SRV_HANDLE_QUERY_SEQ_NUM (srv_handle), false, "fetch srv_h_id %d cursor_pos %d fetch_count %d",
		 srv_h_id, cursor_pos, fetch_count);

  ux_cgw_fetch (srv_handle, cursor_pos, fetch_count, fetch_flag, result_set_index, net_buf, req_info);

  return FN_KEEP_CONN;
}

FN_RETURN
fn_cgw_get_db_version (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf, T_REQ_INFO * req_info)
{
  char auto_commit_mode;
  cas_log_write (0, true, "get_version");

  if (argc < 1)
    {
      ERROR_INFO_SET (CAS_ER_ARGS, CAS_ERROR_INDICATOR);
      NET_BUF_ERR_SET (net_buf);
      return FN_KEEP_CONN;
    }

  net_arg_get_char (auto_commit_mode, argv[0]);

  ux_get_db_version (net_buf, req_info);

  if (auto_commit_mode == TRUE)
    {
      req_info->need_auto_commit = TRAN_AUTOCOMMIT;
    }

  return FN_KEEP_CONN;
}

FN_RETURN
fn_cgw_con_close (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf, T_REQ_INFO * req_info)
{
  cas_log_write (0, true, "con_close");
  net_buf_cp_int (net_buf, 0, NULL);

  return FN_CLOSE_CONN;
}

FN_RETURN
fn_cgw_check_cas (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf, T_REQ_INFO * req_info)
{
  int err_code = 0;

  if (argc == 1)
    {
      char *msg;
      int msg_size;
      net_arg_get_str (&msg, &msg_size, argv[0]);
      cas_log_write (0, true, "client_msg:%s", msg);
    }
  else
    {
      err_code = ux_cgw_check_connection ();
      cas_log_write (0, true, "check_cas %d", err_code);
    }

  if (err_code < 0)
    {
      ERROR_INFO_SET (err_code, CAS_ERROR_INDICATOR);
      NET_BUF_ERR_SET (net_buf);
      return FN_KEEP_SESS;
    }
  else
    {
      return FN_KEEP_CONN;
    }
}

FN_RETURN
fn_cgw_cursor_close (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf, T_REQ_INFO * req_info)
{
  int srv_h_id;
  T_SRV_HANDLE *srv_handle;

  net_arg_get_int (&srv_h_id, argv[0]);

  srv_handle = hm_find_srv_handle (srv_h_id);
  if (srv_handle == NULL || srv_handle->num_q_result < 1)
    {
      /* has already been closed */
      return FN_KEEP_CONN;
    }

  cas_log_write (SRV_HANDLE_QUERY_SEQ_NUM (srv_handle), false, "cursor_close srv_h_id %d", srv_h_id);

  ux_cgw_cursor_close (srv_handle);

  return FN_KEEP_CONN;
}

FN_RETURN
fn_fetch (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf, T_REQ_INFO * req_info)
{
  int srv_h_id;
  int cursor_pos;
  int fetch_count;
  int func_args;
  char fetch_flag;
  int result_set_index;
  T_SRV_HANDLE *srv_handle;

  func_args = 5;

  if (argc < func_args)
    {
      ERROR_INFO_SET (CAS_ER_ARGS, CAS_ERROR_INDICATOR);
      NET_BUF_ERR_SET (net_buf);
      return FN_KEEP_CONN;
    }

  net_arg_get_int (&srv_h_id, argv[0]);
  net_arg_get_int (&cursor_pos, argv[1]);
  net_arg_get_int (&fetch_count, argv[2]);
  net_arg_get_char (fetch_flag, argv[3]);
  net_arg_get_int (&result_set_index, argv[4]);

  srv_handle = hm_find_srv_handle (srv_h_id);

  if (srv_handle == NULL)
    {
      ERROR_INFO_SET (CAS_ER_SRV_HANDLE, CAS_ERROR_INDICATOR);

      cas_log_write (SRV_HANDLE_QUERY_SEQ_NUM (srv_handle), false, "fn_fetch srv_h_id %d %s%d",
		     srv_h_id, "error:", err_info.err_number);

      NET_BUF_ERR_SET (net_buf);
      return FN_KEEP_CONN;
    }

  cas_log_write (SRV_HANDLE_QUERY_SEQ_NUM (srv_handle), false, "fetch srv_h_id %d cursor_pos %d fetch_count %d",
		 srv_h_id, cursor_pos, fetch_count);

  ux_cgw_fetch (srv_handle, cursor_pos, fetch_count, fetch_flag, result_set_index, net_buf, req_info);

  return FN_KEEP_CONN;
}
