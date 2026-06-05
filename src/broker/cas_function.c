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


/*
 * cas_function.c -
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

#include "cas_common_main.h"
#include "cas_common_vars.h"
#include "cas_net_buf.h"
#include "cas_log.h"
#include "cas.h"
#include "cas_handle.h"
#include "cas_util.h"
#include "cas_common_function.h"
#include "perf_monitor.h"
#include "cas_sql_log2.h"
#include "parse_tree.h"
#include "ddl_log.h"
#include "error_manager.h"

#include "cas_function.h"
#include "cas_execute.h"
#include "broker_filename.h"
#include "dbtype.h"
#include "db_session.h"
#include "object_primitive.h"

/* ========================================================================
 * Forward Function Declarations
 * ======================================================================== */
static FN_RETURN fn_prepare_internal (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf, T_REQ_INFO * req_info,
				      int *ret_srv_h_id);
static FN_RETURN fn_execute_internal (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf, T_REQ_INFO * req_info,
				      int *prepared_srv_h_id);

void set_query_timeout (T_SRV_HANDLE * srv_handle, int query_timeout);

extern void tran_set_query_timeout (int);
extern bool tran_is_in_libcas (void);

FN_RETURN
fn_end_tran (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf, T_REQ_INFO * req_info)
{
  int tran_type;
  int err_code = 0;
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

#ifdef CCI_XA
  if (is_xa_prepared ())
    {
      /*
       * In the 2PC Prepare state, end_tran (commit or rollback) must not be processed.
       * In this state, commit and rollback must be processed
       * through the commit decision or abort decision from the coordinator.
       * In other words, it must be processed with fn_xa_end_tran.
       */
      ;
    }
  else
#endif
    {
      err_code = ux_end_tran ((char) tran_type, false, false);
    }

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

  if (!ux_is_database_connected ())
    {
      cas_log_debug (ARG_FILE_LINE, "fn_end_tran: !ux_is_database_connected()");
      return FN_CLOSE_CONN;
    }
  else if (restart_is_needed () || as_info->reset_flag == TRUE)
    {
      cas_log_debug (ARG_FILE_LINE, "fn_end_tran: restart_is_needed() || reset_flag");
      db_set_keep_session (true);
      return FN_KEEP_SESS;
    }
  return FN_KEEP_CONN;
}

FN_RETURN
fn_end_session (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf, T_REQ_INFO * req_info)
{
  /* ignore all request to close session from drivers */
  net_buf_cp_int (net_buf, NO_ERROR, NULL);

  return FN_KEEP_CONN;
}

FN_RETURN
fn_get_row_count (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf, T_REQ_INFO * req_info)
{
  ux_get_row_count (net_buf);
  return FN_KEEP_CONN;
}

FN_RETURN
fn_get_last_insert_id (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf, T_REQ_INFO * req_info)
{
  ux_get_last_insert_id (net_buf);
  return FN_KEEP_CONN;
}


FN_RETURN
fn_prepare (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf, T_REQ_INFO * req_info)
{
  return (fn_prepare_internal (sock_fd, argc, argv, net_buf, req_info, NULL));
}


static FN_RETURN
fn_prepare_internal (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf, T_REQ_INFO * req_info,
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

  srv_h_id = ux_prepare (sql_stmt, flag, auto_commit_mode, net_buf, req_info, query_seq_num_current_value ());

  if (ret_srv_h_id != NULL)
    {
      /* this ret_srv_h_id used by following fn_execute_internal() function */
      *ret_srv_h_id = srv_h_id;
    }

  srv_handle = hm_find_srv_handle (srv_h_id);

  if (srv_handle && srv_handle->session)
    {
      assert (((DB_SESSION *) srv_handle->session)->parser);
      PARSER_CONTEXT *psr = ((DB_SESSION *) srv_handle->session)->parser;
      cas_log_compile_end_write_query_string (sql_stmt, sql_size - 1, &psr->hide_pwd_info);
    }
  else
    {
      cas_log_compile_end_write_query_string (sql_stmt, sql_size - 1, NULL);
    }

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
fn_execute (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf, T_REQ_INFO * req_info)
{
  FN_RETURN ret = fn_execute_internal (sock_fd, argc, argv, net_buf, req_info, NULL);

  return ret;
}

static FN_RETURN
fn_execute_internal (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf, T_REQ_INFO * req_info,
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
  char stmt_type = -1;
  char *plan = NULL;

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

  if (srv_handle == NULL || srv_handle->schema_type >= CCI_SCH_FIRST)
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
      if (srv_handle->q_result->stmt_type == CUBRID_STMT_SELECT)
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
	  if (srv_handle->q_result->stmt_type == CUBRID_STMT_SELECT)
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

  set_query_timeout (srv_handle, app_query_timeout);

  srv_handle->auto_commit_mode = auto_commit_mode;
  srv_handle->forward_only_cursor = forward_only_cursor;
  logddl_set_commit_mode (auto_commit_mode);

  if (srv_handle->prepare_flag & CCI_PREPARE_CALL)
    {
      exec_func_name = "execute_call";
      ux_exec_func = ux_execute_call;

      if (param_mode)
	{
	  ux_call_info_cp_param_mode (srv_handle, param_mode, param_mode_size);
	}

    }
  else if (flag & CCI_EXEC_QUERY_ALL)
    {
      exec_func_name = "execute_all";
      ux_exec_func = ux_execute_all;
    }
  else
    {
      exec_func_name = "execute";
      ux_exec_func = ux_execute;
    }

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
	  assert (((DB_SESSION *) srv_handle->session)->parser);
	  PARSER_CONTEXT *psr = ((DB_SESSION *) srv_handle->session)->parser;
	  cas_log_write_query_string (srv_handle->sql_stmt, (int) strlen (srv_handle->sql_stmt), &psr->hide_pwd_info);
	}
    }
  cas_log_debug (ARG_FILE_LINE, "%s%s", auto_commit_mode ? "auto_commit_mode " : "",
		 forward_only_cursor ? "forward_only_cursor " : "");

  if (as_info->cur_sql_log_mode != SQL_LOG_MODE_NONE)
    {
      cas_common_bind_value_log (NULL, bind_value_index, argc, argv, param_mode_size, param_mode,
				 SRV_HANDLE_QUERY_SEQ_NUM (srv_handle), false, CAS_SCHEMA_DEFAULT_CHARSET);
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
      ux_fetch (srv_handle, 1, 50, 0, 0, net_buf, req_info);
    }

  cas_log_write (SRV_HANDLE_QUERY_SEQ_NUM (srv_handle), false, "%s %s%d tuple %d time %d.%03d%s%s%s", exec_func_name,
		 (ret_code < 0) ? "error:" : "", err_number_execute, get_tuple_count (srv_handle), elapsed_sec,
		 elapsed_msec, (client_cache_reusable == TRUE) ? " (CC)" : "",
		 (srv_handle->use_query_cache == true) ? " (QC)" : "", eid_string);

  plan = db_get_execution_plan ();

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
	      assert (srv_handle->session);
	      assert (((DB_SESSION *) srv_handle->session)->parser);
	      PARSER_CONTEXT *psr = ((DB_SESSION *) srv_handle->session)->parser;

	      cas_slow_log_write_query_string (srv_handle->sql_stmt, (int) strlen (srv_handle->sql_stmt),
					       &psr->hide_pwd_info);
	      cas_common_bind_value_log (&query_start_time, bind_value_index, argc, argv, param_mode_size, param_mode,
					 SRV_HANDLE_QUERY_SEQ_NUM (srv_handle), true, CAS_SCHEMA_DEFAULT_CHARSET);
	    }

	  cas_slow_log_write (NULL, SRV_HANDLE_QUERY_SEQ_NUM (srv_handle), false,
			      "%s %s%d tuple %d time %d.%03d%s%s%s\n", exec_func_name, (ret_code < 0) ? "error:" : "",
			      err_number_execute, get_tuple_count (srv_handle), elapsed_sec, elapsed_msec,
			      (client_cache_reusable == TRUE) ? " (CC)" : "",
			      (srv_handle->use_query_cache == true) ? " (QC)" : "", eid_string);

	  if (plan != NULL && plan[0] != '\0')
	    {
	      cas_slow_log_write (NULL, 0, false, "slow query plan\n%s", plan);
	    }
	  cas_slow_log_end ();
	}
    }

  /* set is_pooled */
  if (as_info->cur_statement_pooling)
    {
      srv_handle->is_pooled = TRUE;
    }

  if (plan != NULL && plan[0] != '\0')
    {
      cas_log_write (0, true, "slow query plan\n%s", plan);

      /* reset global plan buffer */
      db_set_execution_plan (NULL, 0);
    }

  return FN_KEEP_CONN;
}

FN_RETURN
fn_prepare_and_execute (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf, T_REQ_INFO * req_info)
{
  int prepare_argc_count;
  int srv_h_id;
  T_SRV_HANDLE *srv_handle;

  net_arg_get_int (&prepare_argc_count, argv[0]);

  fn_prepare_internal (sock_fd, prepare_argc_count, argv + 1, net_buf, req_info, &srv_h_id);
  if (IS_ERROR_INFO_SET ())
    {
      goto prepare_and_execute_end;
    }

  /* execute argv begins at prepare argv + 1 + prepare_argc_count */
  fn_execute_internal (sock_fd, 10, argv + 1 + prepare_argc_count, net_buf, req_info, &srv_h_id);
  if (IS_ERROR_INFO_SET ())
    {
      srv_handle = hm_find_srv_handle (srv_h_id);
      cas_log_write (SRV_HANDLE_QUERY_SEQ_NUM (srv_handle), false, "close_req_handle srv_h_id %d", srv_h_id);
      hm_srv_handle_free (srv_h_id);
    }

prepare_and_execute_end:
  return FN_KEEP_CONN;
}

FN_RETURN
fn_get_db_parameter (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf, T_REQ_INFO * req_info)
{
  int param_name;

  if (argc < 1)
    {
      ERROR_INFO_SET (CAS_ER_ARGS, CAS_ERROR_INDICATOR);
      NET_BUF_ERR_SET (net_buf);
      return FN_KEEP_CONN;
    }

  net_arg_get_int (&param_name, argv[0]);

  if (param_name == CCI_PARAM_ISOLATION_LEVEL)
    {
      int isol_level;

      ux_get_tran_setting (NULL, &isol_level);
      cas_log_write (0, true, "get_db_parameter isolation_level %d", isol_level);

      net_buf_cp_int (net_buf, 0, NULL);	/* res code */
      net_buf_cp_int (net_buf, isol_level, NULL);	/* res msg */
    }
  else if (param_name == CCI_PARAM_LOCK_TIMEOUT)
    {
      int lock_timeout;
      char lock_timeout_string[32];

      ux_get_tran_setting (&lock_timeout, NULL);

      if (lock_timeout == 0)
	{
	  sprintf (lock_timeout_string, "0 (No wait)");
	}
      else if (lock_timeout == -1)
	{
	  sprintf (lock_timeout_string, "-1 (Infinite wait)");
	}
      else
	{
	  sprintf (lock_timeout_string, "%d ms", lock_timeout);
	}

      cas_log_write (0, true, "get_db_parameter lock_timeout %s", lock_timeout_string);

      net_buf_cp_int (net_buf, 0, NULL);
      if (!DOES_CLIENT_UNDERSTAND_THE_PROTOCOL (req_info->client_version, PROTOCOL_V2))
	{
	  if (lock_timeout > 0)
	    {
	      /* protocol version v1 driver receive lock timeout in second */
	      lock_timeout /= 1000;
	    }
	}
      net_buf_cp_int (net_buf, lock_timeout, NULL);
    }
  else if (param_name == CCI_PARAM_MAX_STRING_LENGTH)
    {

      int max_str_len = shm_appl->max_string_length;

      net_buf_cp_int (net_buf, 0, NULL);
      if (max_str_len <= 0 || max_str_len > DB_MAX_STRING_LENGTH)
	max_str_len = DB_MAX_STRING_LENGTH;
      net_buf_cp_int (net_buf, max_str_len, NULL);
    }
  else if (param_name == CCI_PARAM_NO_BACKSLASH_ESCAPES)
    {
      int no_backslash_escapes;

      no_backslash_escapes =
	((cas_default_no_backslash_escapes == true) ? CCI_NO_BACKSLASH_ESCAPES_TRUE : CCI_NO_BACKSLASH_ESCAPES_FALSE);

      cas_log_write (0, true, "get_db_parameter no_backslash_escapes %s",
		     (cas_default_no_backslash_escapes ? "true" : "false"));

      net_buf_cp_int (net_buf, 0, NULL);
      net_buf_cp_int (net_buf, no_backslash_escapes, NULL);
    }
  else
    {
      ERROR_INFO_SET (CAS_ER_PARAM_NAME, CAS_ERROR_INDICATOR);
      NET_BUF_ERR_SET (net_buf);
      return FN_KEEP_CONN;
    }

  return FN_KEEP_CONN;
}

FN_RETURN
fn_set_db_parameter (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf, T_REQ_INFO * req_info)
{
  int param_name;

  if (argc < 2)
    {
      ERROR_INFO_SET (CAS_ER_ARGS, CAS_ERROR_INDICATOR);
      NET_BUF_ERR_SET (net_buf);
      return FN_KEEP_CONN;
    }

  net_arg_get_int (&param_name, argv[0]);

  if (param_name == CCI_PARAM_ISOLATION_LEVEL)
    {
      int isol_level;

      net_arg_get_int (&isol_level, argv[1]);

      cas_log_write (0, true, "set_db_parameter isolation_level %d", isol_level);

      if (ux_set_isolation_level (isol_level, net_buf) < 0)
	return FN_KEEP_CONN;

      net_buf_cp_int (net_buf, 0, NULL);	/* res code */
    }
  else if (param_name == CCI_PARAM_LOCK_TIMEOUT)
    {
      int lock_timeout;
      char lock_timeout_string[32];

      net_arg_get_int (&lock_timeout, argv[1]);

      if (lock_timeout == -2)
	{
	  lock_timeout = cas_default_lock_timeout;
	}

      if (lock_timeout < -1)
	{
	  ERROR_INFO_SET (CAS_ER_ARGS, CAS_ERROR_INDICATOR);
	  NET_BUF_ERR_SET (net_buf);
	  return FN_KEEP_CONN;
	}

      ux_set_lock_timeout (lock_timeout);

      if (lock_timeout == 0)
	{
	  sprintf (lock_timeout_string, "0 (No wait)");
	}
      else if (lock_timeout == -1)
	{
	  sprintf (lock_timeout_string, "-1 (Infinite wait)");
	}
      else
	{
	  sprintf (lock_timeout_string, "%d ms", lock_timeout);
	}

      cas_log_write (0, true, "set_db_parameter lock_timeout %s", lock_timeout_string);

      net_buf_cp_int (net_buf, 0, NULL);
    }
  else if (param_name == CCI_PARAM_AUTO_COMMIT)
    {
      int auto_commit;

      net_arg_get_int (&auto_commit, argv[1]);


      if (auto_commit)
	as_info->auto_commit_mode = TRUE;
      else
	as_info->auto_commit_mode = FALSE;

      cas_log_write (0, true, "set_db_parameter auto_commit %d", auto_commit);


      net_buf_cp_int (net_buf, 0, NULL);
    }
  else
    {
      ERROR_INFO_SET (CAS_ER_PARAM_NAME, CAS_ERROR_INDICATOR);
      NET_BUF_ERR_SET (net_buf);
      return FN_KEEP_CONN;
    }

  return FN_KEEP_CONN;
}

FN_RETURN
fn_set_cas_change_mode (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf, T_REQ_INFO * req_info)
{
  int mode;

  if (argc < 1)
    {
      ERROR_INFO_SET (CAS_ER_ARGS, CAS_ERROR_INDICATOR);
      NET_BUF_ERR_SET (net_buf);
      return FN_KEEP_CONN;
    }

  net_arg_get_int (&mode, argv[0]);

  if (mode != CAS_CHANGE_MODE_AUTO && mode != CAS_CHANGE_MODE_KEEP)
    {
      ERROR_INFO_SET (CAS_ER_ARGS, CAS_ERROR_INDICATOR);
      NET_BUF_ERR_SET (net_buf);
      return FN_KEEP_CONN;
    }

  cas_log_write (0, true, "set_cas_change_mode %s", mode == CAS_CHANGE_MODE_AUTO ? "AUTO" : "KEEP");

  ux_set_cas_change_mode (mode, net_buf);

  return FN_KEEP_CONN;
}

FN_RETURN
fn_close_req_handle (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf, T_REQ_INFO * req_info)
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
fn_cursor (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf, T_REQ_INFO * req_info)
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

  ux_cursor (srv_h_id, offset, origin, net_buf);

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

  ux_fetch (srv_handle, cursor_pos, fetch_count, fetch_flag, result_set_index, net_buf, req_info);

  return FN_KEEP_CONN;
}

FN_RETURN
fn_schema_info (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf, T_REQ_INFO * req_info)
{
  int schema_type;
  char *arg1, *arg2;
  char flag;
  int arg1_size, arg2_size;
  int shard_id;
  int srv_h_id;

  if (argc < 4)
    {
      ERROR_INFO_SET (CAS_ER_ARGS, CAS_ERROR_INDICATOR);
      NET_BUF_ERR_SET (net_buf);
      return FN_KEEP_CONN;
    }

  net_arg_get_int (&schema_type, argv[0]);
  net_arg_get_str (&arg1, &arg1_size, argv[1]);
  net_arg_get_str (&arg2, &arg2_size, argv[2]);
  net_arg_get_char (flag, argv[3]);
  if (DOES_CLIENT_UNDERSTAND_THE_PROTOCOL (req_info->client_version, PROTOCOL_V5))
    {
      net_arg_get_int (&shard_id, argv[4]);
    }
  else
    {
      shard_id = 0;
    }

  cas_log_write (query_seq_num_next_value (), true, "schema_info %s %s %s %d", get_schema_type_str (schema_type),
		 (arg1 ? arg1 : "NULL"), (arg2 ? arg2 : "NULL"), flag);

  srv_h_id = ux_schema_info (schema_type, arg1, arg2, flag, net_buf, req_info, query_seq_num_current_value ());

  cas_log_write (query_seq_num_current_value (), false, "schema_info srv_h_id %d", srv_h_id);

  return FN_KEEP_CONN;
}

FN_RETURN
fn_oid_get (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf, T_REQ_INFO * req_info)
{
  int pageid;
  short slotid, volid;
  int ret;

  if (argc < 1)
    {
      ERROR_INFO_SET (CAS_ER_ARGS, CAS_ERROR_INDICATOR);
      NET_BUF_ERR_SET (net_buf);
      return FN_KEEP_CONN;
    }

  net_arg_get_cci_object (&pageid, &slotid, &volid, argv[0]);

  ret = ux_oid_get (argc, argv, net_buf);

  cas_log_write (0, true, "oid_get @%d|%d|%d %s", pageid, slotid, volid, (ret < 0 ? "ERR" : ""));
  return FN_KEEP_CONN;
}

FN_RETURN
fn_oid_put (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf, T_REQ_INFO * req_info)
{
  if (argc < 3 || argc % 3 != 1)
    {
      ERROR_INFO_SET (CAS_ER_ARGS, CAS_ERROR_INDICATOR);
      NET_BUF_ERR_SET (net_buf);
      return FN_KEEP_CONN;
    }

  cas_log_write (0, true, "oid_put");

  ux_oid_put (argc, argv, net_buf);

  return FN_KEEP_CONN;
}

FN_RETURN
fn_get_db_version (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf, T_REQ_INFO * req_info)
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
fn_get_class_num_objs (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf, T_REQ_INFO * req_info)
{
  char *class_name;
  char flag;
  int class_name_size;

  if (argc < 2)
    {
      ERROR_INFO_SET (CAS_ER_ARGS, CAS_ERROR_INDICATOR);
      NET_BUF_ERR_SET (net_buf);
      return FN_KEEP_CONN;
    }

  cas_log_write (0, true, "get_class_num_objs");

  net_arg_get_str (&class_name, &class_name_size, argv[0]);
  net_arg_get_char (flag, argv[1]);

  ux_get_class_num_objs (class_name, flag, net_buf);

  return FN_KEEP_CONN;
}

FN_RETURN
fn_oid (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf, T_REQ_INFO * req_info)
{
  DB_OBJECT *obj;
  char cmd;
  int err_code;
  char *res_msg = NULL;

  if (argc < 2)
    {
      ERROR_INFO_SET (CAS_ER_ARGS, CAS_ERROR_INDICATOR);
      goto fn_oid_error;
    }

  net_arg_get_char (cmd, argv[0]);
  net_arg_get_dbobject (&obj, argv[1]);
  if (cmd != CCI_OID_IS_INSTANCE)
    {
      if ((err_code = ux_check_object (obj, net_buf)) < 0)
	{
	  goto fn_oid_error;
	}
    }

  if (cmd == CCI_OID_DROP)
    {
      cas_log_write (0, true, "oid drop");
      if (obj == NULL)
	{
	  ERROR_INFO_SET (CAS_ER_OBJECT, CAS_ERROR_INDICATOR);
	  goto fn_oid_error;
	}
      err_code = db_drop (obj);
    }
  else if (cmd == CCI_OID_IS_INSTANCE)
    {
      cas_log_write (0, true, "oid is_instance");
      if (obj == NULL)
	{
	  err_code = 0;
	}
      else
	{
	  er_clear ();
	  if (db_is_instance (obj) > 0)
	    {
	      err_code = 1;
	    }
	  else
	    {
	      err_code = db_error_code ();
	      if (err_code == -48)
		err_code = 0;
	    }
	}
    }
  else if (cmd == CCI_OID_LOCK_READ)
    {
      cas_log_write (0, true, "oid lock_read");
      if (obj == NULL)
	{
	  ERROR_INFO_SET (CAS_ER_OBJECT, CAS_ERROR_INDICATOR);
	  goto fn_oid_error;
	}
      err_code = db_lock_read (obj);
    }
  else if (cmd == CCI_OID_LOCK_WRITE)
    {
      cas_log_write (0, true, "oid lock_write");
      if (obj == NULL)
	{
	  ERROR_INFO_SET (CAS_ER_OBJECT, CAS_ERROR_INDICATOR);
	  goto fn_oid_error;
	}
      err_code = db_lock_write (obj);
    }
  else if (cmd == CCI_OID_CLASS_NAME)
    {
      cas_log_write (0, true, "oid get_class_name");
      if (obj == NULL)
	{
	  ERROR_INFO_SET (CAS_ER_OBJECT, CAS_ERROR_INDICATOR);
	  goto fn_oid_error;
	}
      res_msg = (char *) db_get_class_name (obj);
      if (res_msg == NULL)
	{
	  err_code = db_error_code ();
	  res_msg = (char *) "";
	}
      else
	{
	  err_code = 0;
	}
    }
  else
    {
      ERROR_INFO_SET (CAS_ER_INTERNAL, CAS_ERROR_INDICATOR);
      goto fn_oid_error;
    }

  if (err_code < 0)
    {
      ERROR_INFO_SET (err_code, DBMS_ERROR_INDICATOR);
      goto fn_oid_error;
    }
  else
    {
      net_buf_cp_int (net_buf, err_code, NULL);
      if (cmd == CCI_OID_CLASS_NAME)
	{
	  net_buf_cp_str (net_buf, res_msg, (int) strlen (res_msg) + 1);
	}
    }

  return FN_KEEP_CONN;

fn_oid_error:
  NET_BUF_ERR_SET (net_buf);
  return FN_KEEP_CONN;
}

FN_RETURN
fn_collection (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf, T_REQ_INFO * req_info)
{
  char cmd;
  DB_OBJECT *obj;
  char *attr_name = (char *) "";
  int attr_name_size = 0, seq_index = 0;
  int err_code;
  DB_VALUE val;
  DB_COLLECTION *collection;
  DB_ATTRIBUTE *attr;
  DB_DOMAIN *domain, *ele_domain;
  char col_type, ele_type, db_type;
  int value_index;
  DB_VALUE *ele_val = NULL;

  if (argc < 3)
    {
      ERROR_INFO_SET (CAS_ER_ARGS, CAS_ERROR_INDICATOR);
      NET_BUF_ERR_SET (net_buf);
      return FN_KEEP_CONN;
    }

  net_arg_get_char (cmd, argv[0]);
  net_arg_get_dbobject (&obj, argv[1]);

  if ((err_code = ux_check_object (obj, net_buf)) < 0)
    {
      NET_BUF_ERR_SET (net_buf);
      return FN_KEEP_CONN;
    }

  err_code = 0;
  value_index = 0;

  switch (cmd)
    {
    case CCI_COL_GET:
    case CCI_COL_SIZE:
      net_arg_get_str (&attr_name, &attr_name_size, argv[2]);
      break;
    case CCI_COL_SET_DROP:
    case CCI_COL_SET_ADD:
      if (argc < 5)
	{
	  err_code = ERROR_INFO_SET (CAS_ER_ARGS, CAS_ERROR_INDICATOR);
	}

      else
	{
	  net_arg_get_str (&attr_name, &attr_name_size, argv[2]);
	}
      value_index = 3;
      break;
    case CCI_COL_SEQ_DROP:
      if (argc < 4)
	{
	  err_code = ERROR_INFO_SET (CAS_ER_ARGS, CAS_ERROR_INDICATOR);
	}
      else
	{
	  net_arg_get_int (&seq_index, argv[2]);
	  net_arg_get_str (&attr_name, &attr_name_size, argv[3]);
	}
      break;
    case CCI_COL_SEQ_INSERT:
    case CCI_COL_SEQ_PUT:
      if (argc < 6)
	{
	  err_code = ERROR_INFO_SET (CAS_ER_ARGS, CAS_ERROR_INDICATOR);
	}
      else
	{
	  net_arg_get_int (&seq_index, argv[2]);
	  net_arg_get_str (&attr_name, &attr_name_size, argv[3]);
	}

      value_index = 4;
      break;
    default:
      err_code = ERROR_INFO_SET (CAS_ER_INTERNAL, CAS_ERROR_INDICATOR);
    }

  if (err_code < 0)
    {
      NET_BUF_ERR_SET (net_buf);
      return FN_KEEP_CONN;
    }

  if (attr_name_size < 1)
    {
      ERROR_INFO_SET (CAS_ER_ARGS, CAS_ERROR_INDICATOR);
      goto fn_col_finale;
    }

  attr = db_get_attribute (obj, attr_name);
  if (attr == NULL)
    {
      ERROR_INFO_SET (db_error_code (), DBMS_ERROR_INDICATOR);
      goto fn_col_finale;
    }
  domain = db_attribute_domain (attr);
  col_type = ux_db_type_to_cas_type (TP_DOMAIN_TYPE (domain));
  if (col_type != CCI_U_TYPE_SET && col_type != CCI_U_TYPE_MULTISET && col_type != CCI_U_TYPE_SEQUENCE)
    {
      ERROR_INFO_SET (CAS_ER_NOT_COLLECTION, CAS_ERROR_INDICATOR);
      goto fn_col_finale;
    }
  ele_type = get_set_domain (domain, NULL, NULL, &db_type, NULL);
  ele_domain = db_domain_set (domain);
  if (ele_type <= 0)
    {
      ERROR_INFO_SET (CAS_ER_COLLECTION_DOMAIN, CAS_ERROR_INDICATOR);
      goto fn_col_finale;
    }

  err_code = db_get (obj, attr_name, &val);
  if (err_code < 0)
    {
      ERROR_INFO_SET (err_code, DBMS_ERROR_INDICATOR);
      goto fn_col_finale;
    }
  if (db_value_type (&val) == DB_TYPE_NULL)
    {
      collection = NULL;
    }
  else
    {
      collection = db_get_collection (&val);
    }

  if (value_index > 0)
    {
      err_code = make_bind_value (1, 2, argv + value_index, &ele_val, net_buf, db_type);
      if (err_code < 0)
	{
	  if (err_info.err_number == CAS_ER_SRV_HANDLE || err_info.err_number == CAS_ER_NUM_BIND)
	    {
	      cas_log_write (0, false, "fn_collection %s%d", "error:", err_info.err_number);
	    }
	  goto fn_col_finale;
	}
    }

  err_code = 0;
  switch (cmd)
    {
    case CCI_COL_GET:
      ux_col_get (collection, col_type, ele_type, ele_domain, net_buf);
      break;
    case CCI_COL_SIZE:
      ux_col_size (collection, net_buf);
      break;
    case CCI_COL_SET_DROP:
      err_code = ux_col_set_drop (collection, ele_val, net_buf);
      break;
    case CCI_COL_SET_ADD:
      err_code = ux_col_set_add (collection, ele_val, net_buf);
      break;
    case CCI_COL_SEQ_DROP:
      err_code = ux_col_seq_drop (collection, seq_index, net_buf);
      break;
    case CCI_COL_SEQ_INSERT:
      err_code = ux_col_seq_insert (collection, seq_index, ele_val, net_buf);
      break;
    case CCI_COL_SEQ_PUT:
      err_code = ux_col_seq_put (collection, seq_index, ele_val, net_buf);
      break;
    default:
      break;
    }

  switch (cmd)
    {
    case CCI_COL_SET_DROP:
    case CCI_COL_SET_ADD:
    case CCI_COL_SEQ_DROP:
    case CCI_COL_SEQ_INSERT:
    case CCI_COL_SEQ_PUT:
      if (err_code >= 0)
	db_put (obj, attr_name, &val);
    default:
      break;
    }

  if (ele_val)
    {
      db_value_clear (ele_val);
      FREE_MEM (ele_val);
    }

  db_col_free (collection);
  db_value_clear (&val);

  return FN_KEEP_CONN;

fn_col_finale:
  NET_BUF_ERR_SET (net_buf);
  return FN_KEEP_CONN;
}

FN_RETURN
fn_next_result (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf, T_REQ_INFO * req_info)
{
  int srv_h_id;
  char flag;
  int func_args;
  T_SRV_HANDLE *srv_handle;

  func_args = 2;

  if (argc < func_args)
    {
      ERROR_INFO_SET (CAS_ER_ARGS, CAS_ERROR_INDICATOR);
      NET_BUF_ERR_SET (net_buf);
      return FN_KEEP_CONN;
    }

  net_arg_get_int (&srv_h_id, argv[0]);
  net_arg_get_char (flag, argv[1]);

  srv_handle = hm_find_srv_handle (srv_h_id);
  if (srv_handle == NULL)
    {
      ERROR_INFO_SET (CAS_ER_SRV_HANDLE, CAS_ERROR_INDICATOR);

      cas_log_write (SRV_HANDLE_QUERY_SEQ_NUM (srv_handle), false, "fn_next_result srv_h_id %d %s%d",
		     srv_h_id, "error:", err_info.err_number);

      NET_BUF_ERR_SET (net_buf);
      return FN_KEEP_CONN;
    }

  cas_log_write (SRV_HANDLE_QUERY_SEQ_NUM (srv_handle), false, "next_result %d %s", srv_h_id,
		 (srv_handle->use_query_cache == true) ? "(QC)" : "");

  ux_next_result (srv_handle, flag, net_buf, req_info);

  return FN_KEEP_CONN;
}

FN_RETURN
fn_execute_batch (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf, T_REQ_INFO * req_info)
{
  int arg_index = 0;
  char auto_commit_mode;
  int query_timeout;

  net_arg_get_char (auto_commit_mode, argv[arg_index]);

  logddl_set_commit_mode (auto_commit_mode);

  arg_index++;

  if (DOES_CLIENT_UNDERSTAND_THE_PROTOCOL (req_info->client_version, PROTOCOL_V4))
    {
      net_arg_get_int (&query_timeout, argv[arg_index]);
      arg_index++;
    }
  else
    {
      query_timeout = 0;
    }
  /* does not support query timeout for execute_batch yet */
  set_query_timeout (NULL, query_timeout);

  cas_log_write (0, true, "execute_batch %d", argc - arg_index);
  ux_execute_batch (argc - arg_index, argv + arg_index, net_buf, req_info, auto_commit_mode);

  cas_log_write (0, true, "execute_batch end");

  return FN_KEEP_CONN;
}

FN_RETURN
fn_execute_array (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf, T_REQ_INFO * req_info)
{
  int srv_h_id;
  T_SRV_HANDLE *srv_handle;
  int ret_code;
  int elapsed_sec = 0, elapsed_msec = 0;
  struct timeval exec_begin, exec_end;
  char *eid_string;
  char *plan = NULL;
  int driver_query_timeout;
  int arg_index = 0;
  char auto_commit_mode;
  int min_argc;

  /* argv[0] : service handle argv[1] : auto commit flag */
  min_argc = 2;
  if (DOES_CLIENT_UNDERSTAND_THE_PROTOCOL (req_info->client_version, PROTOCOL_V4))
    {
      /* argv[2] : driver_query_timeout */
      min_argc++;
    }

  if (argc < min_argc)
    {
      net_buf_cp_int (net_buf, CAS_ER_ARGS, NULL);
      return FN_KEEP_CONN;
    }

  net_arg_get_int (&srv_h_id, argv[arg_index]);
  arg_index++;

  srv_handle = hm_find_srv_handle (srv_h_id);
  if (srv_handle == NULL)
    {
      net_buf_cp_int (net_buf, CAS_ER_ARGS, NULL);
      return FN_KEEP_CONN;
    }


  if (srv_handle->is_pooled)
    {
      gettimeofday (&query_start_time, NULL);
      query_timeout = 0;
    }

  if (DOES_CLIENT_UNDERSTAND_THE_PROTOCOL (req_info->client_version, PROTOCOL_V4))
    {
      net_arg_get_int (&driver_query_timeout, argv[arg_index]);
      arg_index++;
    }
  else
    {
      driver_query_timeout = 0;
    }

  /* does not support query timeout for execute_array yet */
  set_query_timeout (srv_handle, driver_query_timeout);

  net_arg_get_char (auto_commit_mode, argv[arg_index]);
  arg_index++;

  if (auto_commit_mode == TRUE)
    {
      req_info->need_auto_commit = TRAN_AUTOCOMMIT;
    }
  srv_handle->auto_commit_mode = auto_commit_mode;

  cas_log_write_nonl (SRV_HANDLE_QUERY_SEQ_NUM (srv_handle), false, "execute_array srv_h_id %d %d ", srv_h_id,
		      (argc - arg_index) / 2);
  if (srv_handle->sql_stmt != NULL)
    {
      if (srv_handle->session == NULL)
	{
	  cas_log_write_query_string (srv_handle->sql_stmt, (int) strlen (srv_handle->sql_stmt), NULL);
	}
      else
	{
	  assert (((DB_SESSION *) srv_handle->session)->parser);
	  PARSER_CONTEXT *psr = ((DB_SESSION *) srv_handle->session)->parser;
	  cas_log_write_query_string (srv_handle->sql_stmt, (int) strlen (srv_handle->sql_stmt), &psr->hide_pwd_info);
	}
    }

  if (as_info->cur_sql_log_mode != SQL_LOG_MODE_NONE)
    {
      cas_common_bind_value_log (NULL, arg_index, argc - 1, argv, 0, NULL, SRV_HANDLE_QUERY_SEQ_NUM (srv_handle), false,
				 CAS_SCHEMA_DEFAULT_CHARSET);
    }


  gettimeofday (&exec_begin, NULL);

  ret_code = ux_execute_array (srv_handle, argc - arg_index, argv + arg_index, net_buf, req_info);

  gettimeofday (&exec_end, NULL);
  ut_timeval_diff (&exec_begin, &exec_end, &elapsed_sec, &elapsed_msec);

  eid_string = get_error_log_eids (err_info.err_number);
  cas_log_write (SRV_HANDLE_QUERY_SEQ_NUM (srv_handle), false, "execute_array %s%d tuple %d time %d.%03d%s%s%s",
		 (ret_code < 0) ? "error:" : "", err_info.err_number, get_tuple_count (srv_handle), elapsed_sec,
		 elapsed_msec, "", (srv_handle->use_query_cache == true) ? " (QC)" : "", eid_string);


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
	  cas_slow_log_write (&query_start_time, SRV_HANDLE_QUERY_SEQ_NUM (srv_handle), false,
			      "execute_array srv_h_id %d %d ", srv_h_id, (argc - 2) / 2);
	  if (srv_handle->sql_stmt != NULL)
	    {
	      assert (srv_handle->session);
	      assert (((DB_SESSION *) srv_handle->session)->parser);
	      PARSER_CONTEXT *psr = ((DB_SESSION *) srv_handle->session)->parser;

	      cas_slow_log_write_query_string (srv_handle->sql_stmt, (int) strlen (srv_handle->sql_stmt),
					       &psr->hide_pwd_info);
	      cas_common_bind_value_log (&query_start_time, 2, argc - 1, argv, 0, NULL,
					 SRV_HANDLE_QUERY_SEQ_NUM (srv_handle), true, CAS_SCHEMA_DEFAULT_CHARSET);
	    }
	  cas_slow_log_write (NULL, SRV_HANDLE_QUERY_SEQ_NUM (srv_handle), false,
			      "execute_array %s%d tuple %d time %d.%03d%s%s%s\n", (ret_code < 0) ? "error:" : "",
			      err_info.err_number, get_tuple_count (srv_handle), elapsed_sec, elapsed_msec, "",
			      (srv_handle->use_query_cache == true) ? " (QC)" : "", eid_string);

	  plan = db_get_execution_plan ();

	  if (plan != NULL && plan[0] != '\0')
	    {
	      cas_slow_log_write (NULL, 0, false, "slow query plan\n%s", plan);

	      /* reset global plan buffer */
	      db_set_execution_plan (NULL, 0);
	    }
	  cas_slow_log_end ();
	}
    }


  return FN_KEEP_CONN;
}

FN_RETURN
fn_cursor_close (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf, T_REQ_INFO * req_info)
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

  ux_cursor_close (srv_handle);

  return FN_KEEP_CONN;
}

FN_RETURN
fn_cursor_update (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf, T_REQ_INFO * req_info)
{
  int srv_h_id;
  int cursor_pos;
  T_SRV_HANDLE *srv_handle;

  if (argc < 2)
    {
      ERROR_INFO_SET (CAS_ER_ARGS, CAS_ERROR_INDICATOR);
      NET_BUF_ERR_SET (net_buf);
      return FN_KEEP_CONN;
    }

  net_arg_get_int (&srv_h_id, argv[0]);
  net_arg_get_int (&cursor_pos, argv[1]);

  srv_handle = hm_find_srv_handle (srv_h_id);

  cas_log_write (SRV_HANDLE_QUERY_SEQ_NUM (srv_handle), false, "cursor_update srv_h_id %d, cursor %d", srv_h_id,
		 cursor_pos);

  ux_cursor_update (srv_handle, cursor_pos, argc, argv, net_buf);

  return FN_KEEP_CONN;
}

FN_RETURN
fn_get_attr_type_str (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf, T_REQ_INFO * req_info)
{
  int size;
  char *class_name;
  char *attr_name;

  if (argc < 2)
    {
      ERROR_INFO_SET (CAS_ER_ARGS, CAS_ERROR_INDICATOR);
      NET_BUF_ERR_SET (net_buf);
      return FN_KEEP_CONN;
    }

  net_arg_get_str (&class_name, &size, argv[0]);
  net_arg_get_str (&attr_name, &size, argv[1]);

  ux_get_attr_type_str (class_name, attr_name, net_buf, req_info);

  return FN_KEEP_CONN;
}

FN_RETURN
fn_get_query_info (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf, T_REQ_INFO * req_info)
{
  char info_type;
  T_SRV_HANDLE *srv_handle = NULL;
  int srv_h_id, size, stmt_id, err;
  char *sql_stmt = NULL;
  DB_SESSION *session;
  DB_QUERY_RESULT *result = NULL;

  if (argc < 2)
    {
      ERROR_INFO_SET (CAS_ER_ARGS, CAS_ERROR_INDICATOR);
      NET_BUF_ERR_SET (net_buf);
      return FN_KEEP_CONN;
    }

  net_arg_get_int (&srv_h_id, argv[0]);
  net_arg_get_char (info_type, argv[1]);
  if (argc >= 3)
    {
      net_arg_get_str (&sql_stmt, &size, argv[2]);
    }

  if (sql_stmt != NULL)
    {
      srv_h_id = hm_new_srv_handle (&srv_handle, query_seq_num_next_value ());
      if (srv_h_id < 0)
	{
	  net_buf_cp_byte (net_buf, '\0');
	  goto end;
	}

      cas_log_query_info_init (srv_h_id, TRUE);
      srv_handle->query_info_flag = TRUE;

      db_init_lexer_lineno ();
      session = db_open_buffer (sql_stmt);
      if (!session)
	{
	  ERROR_INFO_SET (db_error_code (), DBMS_ERROR_INDICATOR);
	  NET_BUF_ERR_SET (net_buf);
	  goto end;
	}

      if ((stmt_id = db_compile_statement (session)) < 0)
	{
	  ERROR_INFO_SET (stmt_id, DBMS_ERROR_INDICATOR);
	  NET_BUF_ERR_SET (net_buf);
	  db_close_session (session);
	  goto end;
	}

      /* Generally, it is not necessary to execute a statement in order to get a plan. But, if a statement has a term,
       * which cannot be prepared, such as serial, we should execute it to get its plan. And, currently, we cannot see
       * a plan for a statement which includes both "cannot be prepared" term and host variable. This limitation also
       * exists in csql. */
      err = db_execute_statement (session, stmt_id, &result);
      if (err < 0 && err != ER_UCI_TOO_FEW_HOST_VARS)
	{
	  /* We will ignore an error "too few host variables are given" to return a plan for a statement including host
	   * variables. */
	  ERROR_INFO_SET (db_error_code (), DBMS_ERROR_INDICATOR);
	  NET_BUF_ERR_SET (net_buf);
	  db_close_session (session);
	  goto end;
	}
      if (result != NULL)
	{
	  db_query_end (result);
	}

      db_close_session (session);
    }

  ux_get_query_info (srv_h_id, info_type, net_buf);

end:
  if (sql_stmt != NULL)
    {
      reset_optimization_level_as_saved ();
      hm_srv_handle_free (srv_h_id);
    }

  return FN_KEEP_CONN;
}

FN_RETURN
fn_savepoint (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf, T_REQ_INFO * req_info)
{
  int err_code;
  char cmd;
  char *savepoint_name;
  int savepoint_name_size;

  if (argc < 2)
    {
      ERROR_INFO_SET (CAS_ER_ARGS, CAS_ERROR_INDICATOR);
      NET_BUF_ERR_SET (net_buf);
      return FN_KEEP_CONN;
    }

  net_arg_get_char (cmd, argv[0]);
  net_arg_get_str (&savepoint_name, &savepoint_name_size, argv[1]);
  if (savepoint_name == NULL)
    savepoint_name = (char *) "";

  if (cmd == 1)
    {				/* set */
      cas_log_write (0, true, "savepoint %s", savepoint_name);
      err_code = db_savepoint_transaction (savepoint_name);
    }
  else if (cmd == 2)
    {				/* rollback */
      cas_log_write (0, true, "rollback_savepoint %s", savepoint_name);
      err_code = db_abort_to_savepoint (savepoint_name);
    }
  else
    {
      ERROR_INFO_SET (CAS_ER_INTERNAL, CAS_ERROR_INDICATOR);
      NET_BUF_ERR_SET (net_buf);
      return FN_KEEP_CONN;
    }

  if (err_code < 0)
    {
      ERROR_INFO_SET (err_code, DBMS_ERROR_INDICATOR);
      NET_BUF_ERR_SET (net_buf);
    }
  else
    {
      net_buf_cp_int (net_buf, 0, NULL);
    }

  return FN_KEEP_CONN;
}

FN_RETURN
fn_parameter_info (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf, T_REQ_INFO * req_info)
{
  int srv_h_id;

  if (argc < 1)
    {
      ERROR_INFO_SET (CAS_ER_ARGS, CAS_ERROR_INDICATOR);
      NET_BUF_ERR_SET (net_buf);
      return FN_KEEP_CONN;
    }

  net_arg_get_int (&srv_h_id, argv[0]);

  ux_get_parameter_info (srv_h_id, net_buf);

  return FN_KEEP_CONN;
}

FN_RETURN
fn_con_close (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf, T_REQ_INFO * req_info)
{
  cas_log_write (0, true, "con_close");
  net_buf_cp_int (net_buf, 0, NULL);

  return FN_CLOSE_CONN;
}

FN_RETURN
fn_check_cas (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf, T_REQ_INFO * req_info)
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
      err_code = ux_check_connection ();
      cas_log_write (0, true, "check_cas %d", err_code);
    }

  if (err_code < 0)
    {
      ERROR_INFO_SET (err_code, CAS_ERROR_INDICATOR);
      NET_BUF_ERR_SET (net_buf);
      db_set_keep_session (true);
      return FN_KEEP_SESS;
    }
  else
    {
      return FN_KEEP_CONN;
    }
}

FN_RETURN
fn_make_out_rs (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf, T_REQ_INFO * req_info)
{
  if (argc < 1)
    {
      ERROR_INFO_SET (CAS_ER_ARGS, CAS_ERROR_INDICATOR);
      NET_BUF_ERR_SET (net_buf);
      return FN_KEEP_CONN;
    }

  if (DOES_CLIENT_UNDERSTAND_THE_PROTOCOL (req_info->client_version, PROTOCOL_V11))
    {
      DB_BIGINT query_id;
      net_arg_get_bigint (&query_id, argv[0]);
      ux_make_out_rs (query_id, net_buf, req_info);
    }
  else
    {
      fn_not_supported (sock_fd, argc, argv, net_buf, req_info);
    }

  return FN_KEEP_CONN;
}

FN_RETURN
fn_get_generated_keys (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf, T_REQ_INFO * req_info)
{
  int srv_h_id;
  T_SRV_HANDLE *srv_handle;

  if (argc < 1)
    {
      ERROR_INFO_SET (CAS_ER_ARGS, CAS_ERROR_INDICATOR);
      NET_BUF_ERR_SET (net_buf);
      return FN_KEEP_CONN;
    }

  net_arg_get_int (&srv_h_id, argv[0]);
  srv_handle = hm_find_srv_handle (srv_h_id);

  if (srv_handle == NULL)
    {
      ERROR_INFO_SET (CAS_ER_SRV_HANDLE, CAS_ERROR_INDICATOR);

      cas_log_write (SRV_HANDLE_QUERY_SEQ_NUM (srv_handle), false, "fn_get_generated_keys srv_h_id %d %s%d",
		     srv_h_id, "error:", err_info.err_number);

      NET_BUF_ERR_SET (net_buf);
      return FN_KEEP_CONN;
    }

  cas_log_write (SRV_HANDLE_QUERY_SEQ_NUM (srv_handle), false, "get_generated_keys %d", srv_h_id);

  ux_get_generated_keys (srv_handle, net_buf);

  return FN_KEEP_CONN;
}

FN_RETURN
fn_lob_new (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf, T_REQ_INFO * req_info)
{
  int lob_type, err_code;
  int elapsed_sec = 0, elapsed_msec = 0;
  struct timeval lob_new_begin, lob_new_end;

  if (argc != 1)
    {
#if defined(CAS_FOR_DBMS)
      ERROR_INFO_SET (CAS_ER_ARGS, CAS_ERROR_INDICATOR);
      NET_BUF_ERR_SET (net_buf);
#else /* CAS_FOR_DBMS */
      net_buf_cp_int (net_buf, CAS_ER_ARGS, NULL);
#endif /* CAS_FOR_DBMS */
      return FN_KEEP_CONN;
    }

  net_arg_get_int (&lob_type, argv[0]);
  if (lob_type != CCI_U_TYPE_BLOB && lob_type != CCI_U_TYPE_CLOB)
    {
#if defined(CAS_FOR_DBMS)
      ERROR_INFO_SET (CAS_ER_ARGS, CAS_ERROR_INDICATOR);
      NET_BUF_ERR_SET (net_buf);
#else /* CAS_FOR_DBMS */
      net_buf_cp_int (net_buf, CAS_ER_ARGS, NULL);
#endif /* CAS_FOR_DBMS */
      return FN_KEEP_CONN;
    }

  cas_log_write (0, false, "lob_new lob_type=%d", lob_type);
  gettimeofday (&lob_new_begin, NULL);

  err_code = ux_lob_new (lob_type, net_buf);

  gettimeofday (&lob_new_end, NULL);
  ut_timeval_diff (&lob_new_begin, &lob_new_end, &elapsed_sec, &elapsed_msec);

  cas_log_write (0, false, "lob_new %s%d time %d.%03d%s", err_code < 0 ? "error:" : "", err_info.err_number,
		 elapsed_sec, elapsed_msec, get_error_log_eids (err_info.err_number));

  return FN_KEEP_CONN;
}

FN_RETURN
fn_lob_write (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf, T_REQ_INFO * req_info)
{
  DB_VALUE lob_dbval;
  INT64 offset;
  char *data_buf = NULL;
  int err_code, data_length = 0;
  int elapsed_sec = 0, elapsed_msec = 0;
  struct timeval lob_new_begin, lob_new_end;
  DB_ELO *elo_debug;

  if (argc != 3)
    {
#if defined(CAS_FOR_DBMS)
      ERROR_INFO_SET (CAS_ER_ARGS, CAS_ERROR_INDICATOR);
      NET_BUF_ERR_SET (net_buf);
#else /* CAS_FOR_DBMS */
      net_buf_cp_int (net_buf, CAS_ER_ARGS, NULL);
#endif /* CAS_FOR_DBMS */
      return FN_KEEP_CONN;
    }

  net_arg_get_lob_value (&lob_dbval, argv[0]);
  net_arg_get_bigint (&offset, argv[1]);
  net_arg_get_str (&data_buf, &data_length, argv[2]);

  elo_debug = db_get_elo (&lob_dbval);
  cas_log_write (0, false, "lob_write lob_type=%d offset=%lld, length=%d", elo_debug->type, offset, data_length);
  gettimeofday (&lob_new_begin, NULL);

  err_code = ux_lob_write (&lob_dbval, offset, data_length, data_buf, net_buf);

  gettimeofday (&lob_new_end, NULL);
  ut_timeval_diff (&lob_new_begin, &lob_new_end, &elapsed_sec, &elapsed_msec);

  cas_log_write (0, false, "lob_write %s%d time %d.%03d%s", err_code < 0 ? "error:" : "", err_info.err_number,
		 elapsed_sec, elapsed_msec, get_error_log_eids (err_info.err_number));

  db_value_clear (&lob_dbval);
  return FN_KEEP_CONN;
}

FN_RETURN
fn_lob_read (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf, T_REQ_INFO * req_info)
{
  DB_VALUE lob_dbval;
  INT64 offset;
  int err_code, data_length = 0;
  int elapsed_sec = 0, elapsed_msec = 0;
  struct timeval lob_new_begin, lob_new_end;
  DB_ELO *elo_debug;

  if (argc != 3)
    {
#if defined(CAS_FOR_DBMS)
      ERROR_INFO_SET (CAS_ER_ARGS, CAS_ERROR_INDICATOR);
      NET_BUF_ERR_SET (net_buf);
#else /* CAS_FOR_DBMS */
      net_buf_cp_int (net_buf, CAS_ER_ARGS, NULL);
#endif /* CAS_FOR_DBMS */
      return FN_KEEP_CONN;
    }

  net_arg_get_lob_value (&lob_dbval, argv[0]);
  net_arg_get_bigint (&offset, argv[1]);
  net_arg_get_int (&data_length, argv[2]);

  elo_debug = db_get_elo (&lob_dbval);
  cas_log_write (0, false, "lob_read lob_type=%d offset=%lld, length=%d", elo_debug->type, offset, data_length);
  gettimeofday (&lob_new_begin, NULL);

  err_code = ux_lob_read (&lob_dbval, offset, data_length, net_buf);

  gettimeofday (&lob_new_end, NULL);
  ut_timeval_diff (&lob_new_begin, &lob_new_end, &elapsed_sec, &elapsed_msec);

  cas_log_write (0, false, "lob_read %s%d time %d.%03d%s", err_code < 0 ? "error:" : "", err_info.err_number,
		 elapsed_sec, elapsed_msec, get_error_log_eids (err_info.err_number));

  db_value_clear (&lob_dbval);
  return FN_KEEP_CONN;
}


void
set_query_timeout (T_SRV_HANDLE * srv_handle, int query_timeout)
{
  int broker_timeout_in_millis = shm_appl->query_timeout * 1000;

  if (tran_is_in_libcas () == false)
    {
      if (query_timeout == 0 || broker_timeout_in_millis == 0)
	{
	  tran_set_query_timeout (query_timeout + broker_timeout_in_millis);

	  if (query_timeout == 0 && broker_timeout_in_millis == 0)
	    {
	      cas_log_write (SRV_HANDLE_QUERY_SEQ_NUM (srv_handle), false, "set query timeout to 0 (no limit)");
	    }
	  else
	    {
	      cas_log_write (SRV_HANDLE_QUERY_SEQ_NUM (srv_handle), false, "set query timeout to %d ms (from %s)",
			     (query_timeout + broker_timeout_in_millis), (query_timeout > 0 ? "app" : "broker"));
	    }
	}
      else if (query_timeout > broker_timeout_in_millis)
	{
	  tran_set_query_timeout (broker_timeout_in_millis);
	  cas_log_write (SRV_HANDLE_QUERY_SEQ_NUM (srv_handle), false, "set query timeout to %d ms (from broker)",
			 broker_timeout_in_millis);
	}
      else
	{
	  tran_set_query_timeout (query_timeout);
	  cas_log_write (SRV_HANDLE_QUERY_SEQ_NUM (srv_handle), false, "set query timeout to %d ms (from app)",
			 query_timeout);
	}
    }
}
