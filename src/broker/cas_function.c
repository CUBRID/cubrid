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

#include "cas_common.h"
#include "cas.h"
#include "cas_function.h"
#include "cas_network.h"
#include "cas_net_buf.h"
#include "cas_log.h"
#include "cas_handle.h"
#include "cas_util.h"
#include "cas_execute.h"

#if defined(CAS_FOR_MYSQL)
#include "cas_mysql.h"
#endif /* CAS_FOR_MYSQL */

#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
#include "perf_monitor.h"
#endif /* !CAS_FOR_ORACLE && !CAS_FOR_MYSQL */

#include "broker_filename.h"
#include "cas_sql_log2.h"

typedef struct t_glo_cmd_info T_GLO_CMD_INFO;
struct t_glo_cmd_info
{
  char num_args;
  char start_pos_index;
  char data_index;
  const char *method_name;
};

static const char *get_schema_type_str (int schema_type);
static const char *get_tran_type_str (int tran_type);
static void bind_value_print (char type, void *net_value);
static char *get_error_log_eids (int err);
#ifndef LIBCAS_FOR_JSP
static void bind_value_log (int start, int argc, void **argv, int, char *,
			    unsigned int);
#endif /* !LIBCAS_FOR_JSP */

static const char *tran_type_str[] = { "COMMIT", "ROLLBACK" };
static const char *schema_type_str[] = {
  "CLASS",
  "VCLASS",
  "QUERY_SPEC",
  "ATTRIBUTE",
  "CLASS_ATTRIBUTE",
  "METHOD",
  "CLASS_METHOD",
  "METHOD_FILE",
  "SUPERCLASS",
  "SUBCLASS",
  "CONSTRAINT",
  "TRIGGER",
  "CLASS_PRIVILEGE",
  "ATTR_PRIVILEGE",
  "DIRECT_SUPER_CLASS",
  "PRIMARY_KEY"
};

static T_GLO_CMD_INFO glo_cmd_info[13] = {
  {0, -1, -1, NULL},		/* dummy info */
  {4, 2, -1, "read_data"},	/* GLO_CMD_READ_DATA */
  {4, 2, 3, "write_data"},	/* GLO_CMD_WRITE_DATA */
  {4, 2, 3, "insert_data"},	/* GLO_CMD_INSERT_DATA */
  {4, 2, -1, "delete_data"},	/* GLO_CMD_DELETE_DATA */
  {3, 2, -1, "truncate_data"},	/* GLO_CMD_TRUNCATE_DATA */
  {3, -1, 2, "append_data"},	/* GLO_CMD_APPEND_DATA */
  {2, -1, -1, "data_size"},	/* GLO_CMD_DATA_SIZE */
  {2, -1, -1, "compress_data"},	/* GLO_CMD_COMPRESS_DATA */
  {2, -1, -1, "destroy_data"},	/* GLO_CMD_DESTROY_DATA */
  {4, 2, -1, "like_search"},	/* GLO_CMD_LIKE_SEARCH */
  {4, 2, -1, "reg_search"},	/* GLO_CMD_REG_SEARCH */
  {4, 2, -1, "binary_search"}	/* GLO_CMD_BINARY_SEARCH */
};

static const char *type_str_tbl[] = {
  "NULL",			/* CCI_U_TYPE_NULL */
  "CHAR",			/* CCI_U_TYPE_CHAR */
  "VARCHAR",			/* CCI_U_TYPE_STRING */
  "NCHAR",			/* CCI_U_TYPE_NCHAR */
  "VARNCHAR",			/* CCI_U_TYPE_VARNCHAR */
  "BIT",			/* CCI_U_TYPE_BIT */
  "VARBIT",			/* CCI_U_TYPE_VARBIT */
  "NUMERIC",			/* CCI_U_TYPE_NUMERIC */
  "INT",			/* CCI_U_TYPE_INT */
  "SHORT",			/* CCI_U_TYPE_SHORT */
  "MONETARY",			/* CCI_U_TYPE_MONETARY */
  "FLOAT",			/* CCI_U_TYPE_FLOAT */
  "DOUBLE",			/* CCI_U_TYPE_DOUBLE */
  "DATE",			/* CCI_U_TYPE_DATE */
  "TIME",			/* CCI_U_TYPE_TIME */
  "TIMESTAMP",			/* CCI_U_TYPE_TIMESTAMP */
  "SET",			/* CCI_U_TYPE_SET */
  "MULTISET",			/* CCI_U_TYPE_MULTISET */
  "SEQUENCE",			/* CCI_U_TYPE_SEQUENCE */
  "OBJECT",			/* CCI_U_TYPE_OBJECT */
  "RESULTSET",			/* CCI_U_TYPE_RESULTSET */
  "BIGINT",			/* CCI_U_TYPE_BIGINT */
  "DATETIME"			/* CCI_U_TYPE_DATETIME */
};

static int query_sequence_num = 0;
#define QUERY_SEQ_NUM_NEXT_VALUE()      ++query_sequence_num
#define QUERY_SEQ_NUM_CURRENT_VALUE()   query_sequence_num

int
fn_end_tran (SOCKET CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
	     void **CAS_FN_ARG_ARGV, T_NET_BUF * CAS_FN_ARG_NET_BUF,
	     T_REQ_INFO * CAS_FN_ARG_REQ_INFO)
{
  int tran_type;
  int err_code;
  int elapsed_sec = 0, elapsed_msec = 0;
  struct timeval end_tran_begin, end_tran_end;
#ifndef LIBCAS_FOR_JSP
  int timeout;
#endif /* !LIBCAS_FOR_JSP */

  if (CAS_FN_ARG_ARGC < 1)
    {
#if defined(CAS_FOR_DBMS)
      ERROR_INFO_SET (CAS_ER_ARGS, CAS_ERROR_INDICATOR);
      NET_BUF_ERR_SET (CAS_FN_ARG_NET_BUF);
#else /* CAS_FOR_DBMS */
      net_buf_cp_int (CAS_FN_ARG_NET_BUF, CAS_ER_ARGS, NULL);
#endif /* CAS_FOR_DBMS */
      return 0;
    }

  NET_ARG_GET_CHAR (tran_type, CAS_FN_ARG_ARGV[0]);
  if (tran_type != CCI_TRAN_COMMIT && tran_type != CCI_TRAN_ROLLBACK)
    {
#if defined(CAS_FOR_DBMS)
      ERROR_INFO_SET (CAS_ER_TRAN_TYPE, CAS_ERROR_INDICATOR);
      NET_BUF_ERR_SET (CAS_FN_ARG_NET_BUF);
#else /* CAS_FOR_DBMS */
      net_buf_cp_int (CAS_FN_ARG_NET_BUF, CAS_ER_TRAN_TYPE, NULL);
#endif /* CAS_FOR_DBMS */
      return 0;
    }

  cas_log_write (0, false, "end_tran %s", get_tran_type_str (tran_type));

  gettimeofday (&end_tran_begin, NULL);

  err_code = ux_end_tran ((char) tran_type, false);

  if ((tran_type == CCI_TRAN_ROLLBACK) &&
      (CAS_FN_ARG_REQ_INFO->client_version < CAS_MAKE_VER (8, 2, 0)))
    {
      /* For backward compatibility */
      cas_send_result_flag = FALSE;
    }

  gettimeofday (&end_tran_end, NULL);
  ut_timeval_diff (&end_tran_begin, &end_tran_end, &elapsed_sec,
		   &elapsed_msec);

#if defined(CAS_FOR_DBMS)
  cas_log_write (0, false, "end_tran %s%d time %d.%03d%s",
		 err_code < 0 ? "error:" : "",
		 err_info.err_number, elapsed_sec, elapsed_msec,
		 get_error_log_eids (err_info.err_number));
#else /* CAS_FOR_DBMS */
  cas_log_write (0, false, "end_tran %s%d time %d.%03d%s",
		 err_code < 0 ? "error:" : "",
		 err_code, elapsed_sec, elapsed_msec,
		 get_error_log_eids (err_code));
#endif /* CAS_FOR_DBMS */

#if defined(CAS_FOR_DBMS)
  if (err_code < 0)
    {
      NET_BUF_ERR_SET (CAS_FN_ARG_NET_BUF);
      CAS_FN_ARG_REQ_INFO->need_rollback = TRUE;
    }
#else /* CAS_FOR_DBMS */
  if (err_code < 0)
    {
      DB_ERR_MSG_SET (CAS_FN_ARG_NET_BUF, err_code);
      CAS_FN_ARG_REQ_INFO->need_rollback = TRUE;
    }
#endif /* CAS_FOR_DBMS */
  else
    {
      net_buf_cp_int (CAS_FN_ARG_NET_BUF, 0, NULL);
      CAS_FN_ARG_REQ_INFO->need_rollback = FALSE;
    }

#ifndef LIBCAS_FOR_JSP
  timeout = ut_check_timeout (&tran_start_time,
			      shm_appl->long_transaction_time,
			      &elapsed_sec, &elapsed_msec);
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

  if (as_info->cur_keep_con != KEEP_CON_OFF)
    {
      as_info->con_status = CON_STATUS_OUT_TRAN;
      if (as_info->cas_log_reset)
	{
	  cas_log_reset (broker_name, shm_as_index);
	}
      if (shm_appl->sql_log2 != as_info->cur_sql_log2)
	{
	  sql_log2_end (false);
	  as_info->cur_sql_log2 = shm_appl->sql_log2;
	  sql_log2_init (broker_name, shm_as_index, as_info->cur_sql_log2,
			 true);
	}

      if (!ux_is_database_connected () || restart_is_needed ()
	  || as_info->reset_flag == TRUE)
	{
	  cas_log_debug (ARG_FILE_LINE,
			 "fn_end_tran: !ux_is_database_connected()"
			 " || restart_is_needed() || reset_flag");
	  return -1;
	}
      return 0;
    }
#endif /* !LIBCAS_FOR_JSP */

  return -1;
}

int
fn_prepare (SOCKET CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
	    void **CAS_FN_ARG_ARGV, T_NET_BUF * CAS_FN_ARG_NET_BUF,
	    T_REQ_INFO * CAS_FN_ARG_REQ_INFO)
{
  char *sql_stmt;
  char flag;
  char auto_commit_mode;
  int sql_size;
  int srv_h_id;
  int deferred_close_handle = -1;
  T_SRV_HANDLE *srv_handle = NULL;
  int i;

  if (CAS_FN_ARG_ARGC < 2)
    {
#if defined(CAS_FOR_DBMS)
      ERROR_INFO_SET (CAS_ER_ARGS, CAS_ERROR_INDICATOR);
      NET_BUF_ERR_SET (CAS_FN_ARG_NET_BUF);
#else /* CAS_FOR_DBMS */
      net_buf_cp_int (CAS_FN_ARG_NET_BUF, CAS_ER_ARGS, NULL);
#endif /* CAS_FOR_DBMS */
      return 0;
    }

  NET_ARG_GET_STR (sql_stmt, sql_size, CAS_FN_ARG_ARGV[0]);
  NET_ARG_GET_CHAR (flag, CAS_FN_ARG_ARGV[1]);
  if (CAS_FN_ARG_ARGC > 2)
    {
      NET_ARG_GET_CHAR (auto_commit_mode, CAS_FN_ARG_ARGV[2]);

      for (i = 3; i < CAS_FN_ARG_ARGC; i++)
	{
	  NET_ARG_GET_INT (deferred_close_handle, CAS_FN_ARG_ARGV[i]);
	  cas_log_write (0, false, "close_req_handle srv_h_id %d",
			 deferred_close_handle);
	  hm_srv_handle_free (deferred_close_handle);
	}
    }
  else
    {
      auto_commit_mode = FALSE;
    }

#if 0
  ut_trim (sql_stmt);
#endif

#ifndef LIBCAS_FOR_JSP
  gettimeofday (&query_start_time, NULL);
  query_timeout = 0;
#endif /* !LIBCAS_FOR_JSP */
  cas_log_write_nonl (QUERY_SEQ_NUM_NEXT_VALUE (),
		      false, "prepare %d ", flag);
  cas_log_write_query_string (sql_stmt, sql_size - 1);

#ifndef LIBCAS_FOR_JSP
  SQL_LOG2_COMPILE_BEGIN (as_info->cur_sql_log2, ((const char *) sql_stmt));

  /* append query string to as_info->log_msg */
  if (sql_stmt)
    {
      char *s, *t;
      size_t l;

      for (s = as_info->log_msg, l = 0;
	   *s && l < SHM_LOG_MSG_SIZE - 1; s++, l++)
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
#endif /* !LIBCAS_FOR_JSP */

  srv_h_id = ux_prepare (sql_stmt, flag, auto_commit_mode,
			 CAS_FN_ARG_NET_BUF, CAS_FN_ARG_REQ_INFO,
			 QUERY_SEQ_NUM_CURRENT_VALUE ());

  srv_handle = hm_find_srv_handle (srv_h_id);

#if defined(CAS_FOR_DBMS)
  cas_log_write (QUERY_SEQ_NUM_CURRENT_VALUE (), false,
		 "prepare srv_h_id %s%d%s%s",
		 (srv_h_id < 0) ? "error:" : "", err_info.err_number,
		 " (PC)", get_error_log_eids (err_info.err_number));
#else /* CAS_FOR_DBMS */
  cas_log_write (QUERY_SEQ_NUM_CURRENT_VALUE (), false,
		 "prepare srv_h_id %s%d%s%s",
		 (srv_h_id < 0) ? "error:" : "", srv_h_id,
		 (srv_handle != NULL
		  && srv_handle->use_plan_cache) ? " (PC)" : "",
		 get_error_log_eids (srv_h_id));
#endif /* CAS_FOR_DBMS */

#ifndef LIBCAS_FOR_JSP
  if (shm_appl->select_auto_commit == ON)
    {
      (void) hm_srv_handle_append_active (srv_handle);
    }
#endif /* !LIBCAS_FOR_JSP */

  return 0;
}

int
fn_execute (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf,
	    T_REQ_INFO * req_info)
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
  int (*ux_exec_func) (T_SRV_HANDLE *, char, int, int, int, void **,
		       T_NET_BUF *, T_REQ_INFO *, CACHE_TIME *, int *);
  char fetch_flag = 0;
  CACHE_TIME clt_cache_time, *clt_cache_time_ptr;
  int client_cache_reusable = FALSE;
  int elapsed_sec = 0, elapsed_msec = 0;
  struct timeval exec_begin, exec_end;

  bind_value_index = 9;
  argc_mod_2 = 1;

  if ((CAS_FN_ARG_ARGC < bind_value_index)
      || (CAS_FN_ARG_ARGC % 2 != argc_mod_2))
    {
#if defined(CAS_FOR_DBMS)
      ERROR_INFO_SET (CAS_ER_ARGS, CAS_ERROR_INDICATOR);
      NET_BUF_ERR_SET (CAS_FN_ARG_NET_BUF);
#else /* CAS_FOR_DBMS */
      net_buf_cp_int (CAS_FN_ARG_NET_BUF, CAS_ER_ARGS, NULL);
#endif /* CAS_FOR_DBMS */
      return 0;
    }

  NET_ARG_GET_INT (srv_h_id, CAS_FN_ARG_ARGV[0]);
  NET_ARG_GET_CHAR (flag, CAS_FN_ARG_ARGV[1]);
  NET_ARG_GET_INT (max_col_size, CAS_FN_ARG_ARGV[2]);
  NET_ARG_GET_INT (max_row, CAS_FN_ARG_ARGV[3]);
  NET_ARG_GET_STR (param_mode, param_mode_size, CAS_FN_ARG_ARGV[4]);
  NET_ARG_GET_CHAR (fetch_flag, CAS_FN_ARG_ARGV[5]);

  NET_ARG_GET_CHAR (auto_commit_mode, CAS_FN_ARG_ARGV[6]);
  NET_ARG_GET_CHAR (forward_only_cursor, CAS_FN_ARG_ARGV[7]);

  clt_cache_time_ptr = &clt_cache_time;
  NET_ARG_GET_CACHE_TIME (clt_cache_time_ptr, CAS_FN_ARG_ARGV[8]);

#ifndef LIBCAS_FOR_JSP
  if (shm_appl->max_string_length >= 0)
    {
      if (max_col_size <= 0 || max_col_size > shm_appl->max_string_length)
	max_col_size = shm_appl->max_string_length;
    }
#endif /* LIBCAS_FOR_JSP */

  srv_handle = hm_find_srv_handle (srv_h_id);
#if defined(CAS_FOR_ORACLE) || defined(CAS_FOR_MYSQL)
  if (srv_handle == NULL)
    {
      ERROR_INFO_SET (CAS_ER_SRV_HANDLE, CAS_ERROR_INDICATOR);
      NET_BUF_ERR_SET (CAS_FN_ARG_NET_BUF);
      return 0;
    }
#else /* CAS_FOR_ORACLE || CAS_FOR_MYSQL */
  if (srv_handle == NULL || srv_handle->schema_type >= CCI_SCH_FIRST)
    {
#if defined(CAS_FOR_DBMS)
      ERROR_INFO_SET (CAS_ER_SRV_HANDLE, CAS_ERROR_INDICATOR);
      NET_BUF_ERR_SET (CAS_FN_ARG_NET_BUF);
#else /* CAS_FOR_DBMS */
      net_buf_cp_int (CAS_FN_ARG_NET_BUF, CAS_ER_SRV_HANDLE, NULL);
#endif /* CAS_FOR_DBMS */
      return 0;
    }
#endif /* CAS_FOR_ORACLE || CAS_FOR_MYSQL */
  srv_handle->auto_commit_mode = auto_commit_mode;
  srv_handle->forward_only_cursor = forward_only_cursor;

  if (srv_handle->prepare_flag & CCI_PREPARE_CALL)
    {
      exec_func_name = "execute_call";
      ux_exec_func = ux_execute_call;
#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
      if (param_mode)
	ux_call_info_cp_param_mode (srv_handle, param_mode, param_mode_size);
#endif /* !CAS_FOR_ORACLE && !CAS_FOR_MYSQL */
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

#ifndef LIBCAS_FOR_JSP
  if (srv_handle->is_pooled)
    {
      gettimeofday (&query_start_time, NULL);
      query_timeout = 0;
    }
#endif /* !LIBCAS_FOR_JSP */

  cas_log_write_nonl (SRV_HANDLE_QUERY_SEQ_NUM (srv_handle), false,
		      "%s srv_h_id %d ", exec_func_name, srv_h_id);
  cas_log_write_query_string (srv_handle->sql_stmt,
			      strlen (srv_handle->sql_stmt));
  cas_log_debug (ARG_FILE_LINE, "%s%s",
		 auto_commit_mode ? "auto_commit_mode " : "",
		 forward_only_cursor ? "forward_only_cursor " : "");

#ifndef LIBCAS_FOR_JSP
  bind_value_log (bind_value_index, CAS_FN_ARG_ARGC, CAS_FN_ARG_ARGV,
		  param_mode_size, param_mode,
		  SRV_HANDLE_QUERY_SEQ_NUM (srv_handle));
#endif /* !LIBCAS_FOR_JSP */

#ifndef LIBCAS_FOR_JSP
  /* append query string to as_info->log_msg */
  if (srv_handle->sql_stmt)
    {
      char *s, *t;
      size_t l;

      for (s = as_info->log_msg, l = 0;
	   *s && l < SHM_LOG_MSG_SIZE - 1; s++, l++)
	{
	  /* empty body */
	}
      *s++ = ' ';
      l++;
      for (t = srv_handle->sql_stmt; *t && l < SHM_LOG_MSG_SIZE - 1;
	   s++, t++, l++)
	{
	  *s = *t;
	}
      *s = '\0';
    }

  if (shm_appl->select_auto_commit == ON)
    {
      (void) hm_srv_handle_append_active (srv_handle);
    }
#endif /* !LIBCAS_FOR_JSP */

  gettimeofday (&exec_begin, NULL);

  ret_code = (*ux_exec_func) (srv_handle, flag, max_col_size, max_row,
			      CAS_FN_ARG_ARGC - bind_value_index,
			      CAS_FN_ARG_ARGV + bind_value_index,
			      CAS_FN_ARG_NET_BUF, CAS_FN_ARG_REQ_INFO,
			      clt_cache_time_ptr, &client_cache_reusable);
  gettimeofday (&exec_end, NULL);
  ut_timeval_diff (&exec_begin, &exec_end, &elapsed_sec, &elapsed_msec);
#if defined(CAS_FOR_DBMS)
#if defined(CAS_FOR_ORACLE)
#elif defined(CAS_FOR_MYSQL)
  cas_log_write (SRV_HANDLE_QUERY_SEQ_NUM (srv_handle), false,
		 "%s %s%d tuple %d time %d.%03d%s%s%s",
		 exec_func_name, (ret_code < 0) ? "error:" : "",
		 err_info.err_number,
		 (srv_handle->
		  session) ? cas_mysql_stmt_num_fields (srv_handle->
							session) : 0,
		 elapsed_sec, elapsed_msec,
		 (client_cache_reusable == TRUE) ? " (CC)" : "",
		 (srv_handle->use_query_cache == true) ? " (QC)" : "",
		 get_error_log_eids (err_info.err_number));
#else /* CAS_FOR_MYSQL */
  cas_log_write (SRV_HANDLE_QUERY_SEQ_NUM (srv_handle), false,
		 "%s %s%d tuple %d time %d.%03d%s%s%s",
		 exec_func_name, (ret_code < 0) ? "error:" : "",
		 err_info.err_number, srv_handle->q_result->tuple_count,
		 elapsed_sec, elapsed_msec,
		 (client_cache_reusable == TRUE) ? " (CC)" : "",
		 (srv_handle->use_query_cache == true) ? " (QC)" : "",
		 get_error_log_eids (err_info.err_number));
#endif /* CAS_FOR_MYSQL */
#else /* CAS_FOR_DBMS */
  cas_log_write (SRV_HANDLE_QUERY_SEQ_NUM (srv_handle), false,
		 "%s %s%d tuple %d time %d.%03d%s%s%s",
		 exec_func_name, (ret_code < 0) ? "error:" : "", ret_code,
		 srv_handle->q_result->tuple_count,
		 elapsed_sec, elapsed_msec,
		 (client_cache_reusable == TRUE) ? " (CC)" : "",
		 (srv_handle->use_query_cache == true) ? " (QC)" : "",
		 get_error_log_eids (ret_code));
#endif /* CAS_FOR_DBMS */

#ifndef LIBCAS_FOR_JSP
  query_timeout = ut_check_timeout (&query_start_time,
				    shm_appl->long_query_time,
				    &elapsed_sec, &elapsed_msec);
  if (query_timeout >= 0)
    {
      as_info->num_long_queries %= MAX_DIAG_DATA_VALUE;
      as_info->num_long_queries++;
    }
  if (ret_code < 0)
    {
      as_info->num_error_queries %= MAX_DIAG_DATA_VALUE;
      as_info->num_error_queries++;
    }
#endif /* !LIBCAS_FOR_JSP */

  if (fetch_flag && ret_code >= 0 && client_cache_reusable == FALSE)
    {
      ux_fetch (srv_handle, 1, 50, 0, 0, CAS_FN_ARG_NET_BUF,
		CAS_FN_ARG_REQ_INFO);
    }

#ifndef LIBCAS_FOR_JSP
  /* set is_pooled */
  if (as_info->cur_statement_pooling)
    {
      srv_handle->is_pooled = TRUE;
    }
#endif /* !LIBCAS_FOR_JSP */

  return 0;
}

#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
int
fn_get_db_parameter (SOCKET CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
		     void **CAS_FN_ARG_ARGV,
		     T_NET_BUF * CAS_FN_ARG_NET_BUF,
		     T_REQ_INFO * CAS_FN_ARG_REQ_INFO)
{
  int param_name;

  if (CAS_FN_ARG_ARGC < 1)
    {
#if defined(CAS_FOR_DBMS)
      ERROR_INFO_SET (CAS_ER_ARGS, CAS_ERROR_INDICATOR);
      NET_BUF_ERR_SET (CAS_FN_ARG_NET_BUF);
#else /* CAS_FOR_DBMS */
      net_buf_cp_int (CAS_FN_ARG_NET_BUF, CAS_ER_ARGS, NULL);
#endif /* CAS_FOR_DBMS */
      return 0;
    }

  NET_ARG_GET_INT (param_name, CAS_FN_ARG_ARGV[0]);

  if (param_name == CCI_PARAM_ISOLATION_LEVEL)
    {
      int isol_level;

      cas_log_write (0, true, "get_db_parameter isolation_level");

      ux_get_tran_setting (NULL, &isol_level);
      net_buf_cp_int (CAS_FN_ARG_NET_BUF, 0, NULL);	/* res code */
      net_buf_cp_int (CAS_FN_ARG_NET_BUF, isol_level, NULL);	/* res msg */
    }
  else if (param_name == CCI_PARAM_LOCK_TIMEOUT)
    {
      int lock_timeout;

      cas_log_write (0, true, "get_db_parameter lock_timeout");

      ux_get_tran_setting (&lock_timeout, NULL);
      net_buf_cp_int (CAS_FN_ARG_NET_BUF, 0, NULL);
      net_buf_cp_int (CAS_FN_ARG_NET_BUF, lock_timeout, NULL);
    }
  else if (param_name == CCI_PARAM_MAX_STRING_LENGTH)
    {
#ifndef LIBCAS_FOR_JSP
      int max_str_len = shm_appl->max_string_length;
#else /* !LIBCAS_FOR_JSP */
      int max_str_len = DB_MAX_STRING_LENGTH;
#endif /* !LIBCAS_FOR_JSP */

      net_buf_cp_int (CAS_FN_ARG_NET_BUF, 0, NULL);
      if (max_str_len <= 0 || max_str_len > DB_MAX_STRING_LENGTH)
	max_str_len = DB_MAX_STRING_LENGTH;
      net_buf_cp_int (CAS_FN_ARG_NET_BUF, max_str_len, NULL);
    }
  else
    {
#if defined(CAS_FOR_DBMS)
      ERROR_INFO_SET (CAS_ER_PARAM_NAME, CAS_ERROR_INDICATOR);
      NET_BUF_ERR_SET (CAS_FN_ARG_NET_BUF);
#else /* CAS_FOR_DBMS */
      net_buf_cp_int (CAS_FN_ARG_NET_BUF, CAS_ER_PARAM_NAME, NULL);
#endif /* CAS_FOR_DBMS */
      return 0;
    }

  return 0;
}

int
fn_set_db_parameter (SOCKET CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
		     void **CAS_FN_ARG_ARGV,
		     T_NET_BUF * CAS_FN_ARG_NET_BUF,
		     T_REQ_INFO * CAS_FN_ARG_REQ_INFO)
{
  int param_name;

  if (CAS_FN_ARG_ARGC < 2)
    {
#if defined(CAS_FOR_DBMS)
      ERROR_INFO_SET (CAS_ER_ARGS, CAS_ERROR_INDICATOR);
      NET_BUF_ERR_SET (CAS_FN_ARG_NET_BUF);
#else /* CAS_FOR_DBMS */
      net_buf_cp_int (CAS_FN_ARG_NET_BUF, CAS_ER_ARGS, NULL);
#endif /* CAS_FOR_DBMS */
      return 0;
    }

  NET_ARG_GET_INT (param_name, CAS_FN_ARG_ARGV[0]);

  if (param_name == CCI_PARAM_ISOLATION_LEVEL)
    {
      int isol_level;

      NET_ARG_GET_INT (isol_level, CAS_FN_ARG_ARGV[1]);

      cas_log_write (0, true, "set_db_parameter isolation_level %d",
		     isol_level);

      if (ux_set_isolation_level (isol_level, CAS_FN_ARG_NET_BUF) < 0)
	return 0;

      net_buf_cp_int (CAS_FN_ARG_NET_BUF, 0, NULL);	/* res code */
    }
  else if (param_name == CCI_PARAM_LOCK_TIMEOUT)
    {
      int lock_timeout;

      NET_ARG_GET_INT (lock_timeout, CAS_FN_ARG_ARGV[1]);

      if (lock_timeout == -2)
	lock_timeout = cas_default_lock_timeout;

      ux_set_lock_timeout (lock_timeout);

      cas_log_write (0, true, "set_db_parameter lock_timeout %d",
		     lock_timeout);

      net_buf_cp_int (CAS_FN_ARG_NET_BUF, 0, NULL);
    }
  else if (param_name == CCI_PARAM_AUTO_COMMIT)
    {
      int auto_commit;

      NET_ARG_GET_INT (auto_commit, CAS_FN_ARG_ARGV[1]);

#ifndef LIBCAS_FOR_JSP
      if (auto_commit)
	as_info->auto_commit_mode = TRUE;
      else
	as_info->auto_commit_mode = FALSE;

      cas_log_write (0, true, "set_db_parameter auto_commit %d", auto_commit);
#endif /* !LIBCAS_FOR_JSP */

      net_buf_cp_int (CAS_FN_ARG_NET_BUF, 0, NULL);
    }
  else
    {
#if defined(CAS_FOR_DBMS)
      ERROR_INFO_SET (CAS_ER_PARAM_NAME, CAS_ERROR_INDICATOR);
      NET_BUF_ERR_SET (CAS_FN_ARG_NET_BUF);
#else /* CAS_FOR_DBMS */
      net_buf_cp_int (CAS_FN_ARG_NET_BUF, CAS_ER_PARAM_NAME, NULL);
#endif /* CAS_FOR_DBMS */
      return 0;
    }

  return 0;
}
#endif /* !CAS_FOR_ORACLE && !CAS_FOR_MYSQL */

int
fn_close_req_handle (SOCKET CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
		     void **CAS_FN_ARG_ARGV,
		     T_NET_BUF * CAS_FN_ARG_NET_BUF,
		     T_REQ_INFO * CAS_FN_ARG_REQ_INFO)
{
  int srv_h_id;
  T_SRV_HANDLE *srv_handle;

  if (CAS_FN_ARG_ARGC < 1)
    {
#if defined(CAS_FOR_DBMS)
      ERROR_INFO_SET (CAS_ER_ARGS, CAS_ERROR_INDICATOR);
      NET_BUF_ERR_SET (CAS_FN_ARG_NET_BUF);
#else /* CAS_FOR_DBMS */
      net_buf_cp_int (CAS_FN_ARG_NET_BUF, CAS_ER_ARGS, NULL);
#endif /* CAS_FOR_DBMS */
      return 0;
    }

  NET_ARG_GET_INT (srv_h_id, CAS_FN_ARG_ARGV[0]);

#ifndef LIBCAS_FOR_JSP
  srv_handle = hm_find_srv_handle (srv_h_id);
  if (srv_handle && srv_handle->auto_commit_mode == TRUE)
    {
      req_info->need_auto_commit = TRAN_AUTOCOMMIT;
    }
#else /* !LIBCAS_FOR_JSP */
  srv_handle = NULL;
#endif /* !LIBCAS_FOR_JSP */

  cas_log_write (SRV_HANDLE_QUERY_SEQ_NUM (srv_handle), false,
		 "close_req_handle srv_h_id %d", srv_h_id);

  hm_srv_handle_free (srv_h_id);

  net_buf_cp_int (CAS_FN_ARG_NET_BUF, 0, NULL);	/* res code */

  return 0;
}

#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
int
fn_cursor (SOCKET CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
	   void **CAS_FN_ARG_ARGV, T_NET_BUF * CAS_FN_ARG_NET_BUF,
	   T_REQ_INFO * CAS_FN_ARG_REQ_INFO)
{
  int srv_h_id;
  int offset;
  char origin;

  if (CAS_FN_ARG_ARGC < 3)
    {
#if defined(CAS_FOR_DBMS)
      ERROR_INFO_SET (CAS_ER_ARGS, CAS_ERROR_INDICATOR);
      NET_BUF_ERR_SET (CAS_FN_ARG_NET_BUF);
#else /* CAS_FOR_DBMS */
      net_buf_cp_int (CAS_FN_ARG_NET_BUF, CAS_ER_ARGS, NULL);
#endif /* CAS_FOR_DBMS */
      return 0;
    }

  NET_ARG_GET_INT (srv_h_id, CAS_FN_ARG_ARGV[0]);
  NET_ARG_GET_INT (offset, CAS_FN_ARG_ARGV[1]);
  NET_ARG_GET_CHAR (origin, CAS_FN_ARG_ARGV[2]);

  ux_cursor (srv_h_id, offset, origin, CAS_FN_ARG_NET_BUF);

  return 0;
}
#endif /* !CAS_FOR_ORACLE && !CAS_FOR_MYSQL */

int
fn_fetch (SOCKET CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
	  void **CAS_FN_ARG_ARGV, T_NET_BUF * CAS_FN_ARG_NET_BUF,
	  T_REQ_INFO * CAS_FN_ARG_REQ_INFO)
{
  int srv_h_id;
  int cursor_pos;
  int fetch_count;
  int func_args;
  char fetch_flag;
  int result_set_index;
  T_SRV_HANDLE *srv_handle;

  func_args = 5;

  if (CAS_FN_ARG_ARGC < func_args)
    {
#if defined(CAS_FOR_DBMS)
      ERROR_INFO_SET (CAS_ER_ARGS, CAS_ERROR_INDICATOR);
      NET_BUF_ERR_SET (CAS_FN_ARG_NET_BUF);
#else /* CAS_FOR_DBMS */
      net_buf_cp_int (CAS_FN_ARG_NET_BUF, CAS_ER_ARGS, NULL);
#endif /* CAS_FOR_DBMS */
      return 0;
    }

  NET_ARG_GET_INT (srv_h_id, CAS_FN_ARG_ARGV[0]);
  NET_ARG_GET_INT (cursor_pos, CAS_FN_ARG_ARGV[1]);
  NET_ARG_GET_INT (fetch_count, CAS_FN_ARG_ARGV[2]);
  NET_ARG_GET_CHAR (fetch_flag, CAS_FN_ARG_ARGV[3]);
  NET_ARG_GET_INT (result_set_index, CAS_FN_ARG_ARGV[4]);

  srv_handle = hm_find_srv_handle (srv_h_id);

  cas_log_write (SRV_HANDLE_QUERY_SEQ_NUM (srv_handle), false,
		 "fetch srv_h_id %d cursor_pos %d fetch_count %d",
		 srv_h_id, cursor_pos, fetch_count);

  ux_fetch (srv_handle, cursor_pos, fetch_count, fetch_flag,
	    result_set_index, CAS_FN_ARG_NET_BUF, CAS_FN_ARG_REQ_INFO);

  return 0;
}

#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
int
fn_schema_info (SOCKET CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
		void **CAS_FN_ARG_ARGV, T_NET_BUF * CAS_FN_ARG_NET_BUF,
		T_REQ_INFO * CAS_FN_ARG_REQ_INFO)
{
  int schema_type;
  char *class_name, *attr_name;
  char flag;
  int class_name_size, attr_name_size;
  int srv_h_id;

  if (CAS_FN_ARG_ARGC < 4)
    {
#if defined(CAS_FOR_DBMS)
      ERROR_INFO_SET (CAS_ER_ARGS, CAS_ERROR_INDICATOR);
      NET_BUF_ERR_SET (CAS_FN_ARG_NET_BUF);
#else /* CAS_FOR_DBMS */
      net_buf_cp_int (CAS_FN_ARG_NET_BUF, CAS_ER_ARGS, NULL);
#endif /* CAS_FOR_DBMS */
      return 0;
    }

  NET_ARG_GET_INT (schema_type, CAS_FN_ARG_ARGV[0]);
  NET_ARG_GET_STR (class_name, class_name_size, CAS_FN_ARG_ARGV[1]);
  NET_ARG_GET_STR (attr_name, attr_name_size, CAS_FN_ARG_ARGV[2]);
  NET_ARG_GET_CHAR (flag, CAS_FN_ARG_ARGV[3]);

  cas_log_write (QUERY_SEQ_NUM_NEXT_VALUE (), true,
		 "schema_info %s %s %s %d",
		 get_schema_type_str (schema_type),
		 (class_name ? class_name : "NULL"),
		 (attr_name ? attr_name : "NULL"), flag);

  srv_h_id =
    ux_schema_info (schema_type, class_name, attr_name, flag,
		    CAS_FN_ARG_NET_BUF, CAS_FN_ARG_REQ_INFO,
		    QUERY_SEQ_NUM_CURRENT_VALUE ());

  cas_log_write (QUERY_SEQ_NUM_CURRENT_VALUE (), false,
		 "schema_info srv_h_id %d", srv_h_id);

  return 0;
}

int
fn_oid_get (SOCKET CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
	    void **CAS_FN_ARG_ARGV, T_NET_BUF * CAS_FN_ARG_NET_BUF,
	    T_REQ_INFO * CAS_FN_ARG_REQ_INFO)
{
  int pageid;
  short slotid, volid;
  int ret;

  if (CAS_FN_ARG_ARGC < 1)
    {
#if defined(CAS_FOR_DBMS)
      ERROR_INFO_SET (CAS_ER_ARGS, CAS_ERROR_INDICATOR);
      NET_BUF_ERR_SET (CAS_FN_ARG_NET_BUF);
#else /* CAS_FOR_DBMS */
      net_buf_cp_int (CAS_FN_ARG_NET_BUF, CAS_ER_ARGS, NULL);
#endif /* CAS_FOR_DBMS */
      return 0;
    }

  NET_ARG_GET_CCI_OBJECT (pageid, slotid, volid, CAS_FN_ARG_ARGV[0]);

  ret = ux_oid_get (CAS_FN_ARG_ARGC, CAS_FN_ARG_ARGV, CAS_FN_ARG_NET_BUF);

  cas_log_write (0, true, "oid_get @%d|%d|%d %s", pageid, slotid, volid,
		 (ret < 0 ? "ERR" : ""));
  return 0;
}

int
fn_oid_put (SOCKET CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
	    void **CAS_FN_ARG_ARGV, T_NET_BUF * CAS_FN_ARG_NET_BUF,
	    T_REQ_INFO * CAS_FN_ARG_REQ_INFO)
{
  if (CAS_FN_ARG_ARGC < 3 || CAS_FN_ARG_ARGC % 3 != 1)
    {
#if defined(CAS_FOR_DBMS)
      ERROR_INFO_SET (CAS_ER_ARGS, CAS_ERROR_INDICATOR);
      NET_BUF_ERR_SET (CAS_FN_ARG_NET_BUF);
#else /* CAS_FOR_DBMS */
      net_buf_cp_int (CAS_FN_ARG_NET_BUF, CAS_ER_ARGS, NULL);
#endif /* CAS_FOR_DBMS */
      return 0;
    }

  cas_log_write (0, true, "oid_put");

  ux_oid_put (CAS_FN_ARG_ARGC, CAS_FN_ARG_ARGV, CAS_FN_ARG_NET_BUF);

  return 0;
}

int
fn_glo_new (SOCKET CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
	    void **CAS_FN_ARG_ARGV, T_NET_BUF * CAS_FN_ARG_NET_BUF,
	    T_REQ_INFO * CAS_FN_ARG_REQ_INFO)
{
  char *class_name;
  int file_size;
  char *filename;
  char flag;
  int err_code;
  char tmp_file[PATH_MAX], dirname[PATH_MAX];
  int filename_size, class_name_size;

  if (CAS_FN_ARG_ARGC < 4)
    {
#if defined(CAS_FOR_DBMS)
      ERROR_INFO_SET (CAS_ER_ARGS, CAS_ERROR_INDICATOR);
      NET_BUF_ERR_SET (CAS_FN_ARG_NET_BUF);
#else /* CAS_FOR_DBMS */
      net_buf_cp_int (CAS_FN_ARG_NET_BUF, CAS_ER_ARGS, NULL);
#endif /* CAS_FOR_DBMS */
      return 0;
    }

  NET_ARG_GET_STR (class_name, class_name_size, CAS_FN_ARG_ARGV[0]);
  if (class_name_size < 1 || class_name[class_name_size - 1] != '\0')
    {
#if defined(CAS_FOR_DBMS)
      ERROR_INFO_SET (CAS_ER_ARGS, CAS_ERROR_INDICATOR);
      NET_BUF_ERR_SET (CAS_FN_ARG_NET_BUF);
#else /* CAS_FOR_DBMS */
      net_buf_cp_int (CAS_FN_ARG_NET_BUF, CAS_ER_ARGS, NULL);
#endif /* CAS_FOR_DBMS */
      return 0;
    }

  NET_ARG_GET_CHAR (flag, CAS_FN_ARG_ARGV[1]);

  if (flag == 2)
    {
      char glo_type;
      NET_ARG_GET_CHAR (glo_type, CAS_FN_ARG_ARGV[2]);
      NET_ARG_GET_STR (filename, filename_size, CAS_FN_ARG_ARGV[3]);
      if (glo_type != CAS_GLO_NEW_LO && glo_type != CAS_GLO_NEW_FBO)
	{
#if defined(CAS_FOR_DBMS)
	  ERROR_INFO_SET (CAS_ER_ARGS, CAS_ERROR_INDICATOR);
	  NET_BUF_ERR_SET (CAS_FN_ARG_NET_BUF);
#else /* CAS_FOR_DBMS */
	  net_buf_cp_int (CAS_FN_ARG_NET_BUF, CAS_ER_ARGS, NULL);
#endif /* CAS_FOR_DBMS */
	  return 0;
	}
      ux_glo_new2 (class_name, glo_type, filename, CAS_FN_ARG_NET_BUF);
      return 0;
    }

  NET_ARG_GET_STR (filename, filename_size, CAS_FN_ARG_ARGV[2]);
  NET_ARG_GET_INT (file_size, CAS_FN_ARG_ARGV[3]);

  cas_log_write (0, true, "glo_new");

  if (flag == 1)
    {
      if (file_size < 0)
	{
#if defined(CAS_FOR_DBMS)
	  ERROR_INFO_SET (CAS_ER_ARGS, CAS_ERROR_INDICATOR);
	  NET_BUF_ERR_SET (CAS_FN_ARG_NET_BUF);
#else /* CAS_FOR_DBMS */
	  net_buf_cp_int (CAS_FN_ARG_NET_BUF, CAS_ER_ARGS, NULL);
#endif /* CAS_FOR_DBMS */
	  return 0;
	}
#ifndef LIBCAS_FOR_JSP
#if defined(WINDOWS)
      as_info->glo_read_size = file_size;
#endif /* WINDOWS */
#endif /* !LIBCAS_FOR_JSP */
      get_cubrid_file (FID_CAS_TMPGLO_DIR, dirname);
#ifdef LIBCAS_FOR_JSP
      snprintf (tmp_file, sizeof (tmp_file) - 1, "%s%d.glo", dirname,
		(int) getpid ());
#else /* LIBCAS_FOR_JSP */
      snprintf (tmp_file, sizeof (tmp_file) - 1, "%s%s_%d.glo", dirname,
		broker_name, shm_as_index + 1);
#endif /* LIBCAS_FOR_JSP */
      filename = tmp_file;
      err_code = net_read_to_file (CAS_FN_ARG_SOCK_FD, file_size, filename);
      if (err_code < 0)
	{
#if defined(CAS_FOR_DBMS)
	  NET_BUF_ERR_SET (CAS_FN_ARG_NET_BUF);
#else /* CAS_FOR_DBMS */
	  net_buf_cp_int (CAS_FN_ARG_NET_BUF, err_code, NULL);
#endif /* CAS_FOR_DBMS */
	  return err_code;
	}
    }
  else if (filename != NULL)
    {
      if (filename_size < 1 || filename[filename_size - 1] != '\0')
	{
#if defined(CAS_FOR_DBMS)
	  ERROR_INFO_SET (CAS_ER_ARGS, CAS_ERROR_INDICATOR);
	  NET_BUF_ERR_SET (CAS_FN_ARG_NET_BUF);
#else /* CAS_FOR_DBMS */
	  net_buf_cp_int (CAS_FN_ARG_NET_BUF, CAS_ER_ARGS, NULL);
#endif /* CAS_FOR_DBMS */
	  return 0;
	}
    }

  ux_glo_new (class_name, filename, CAS_FN_ARG_NET_BUF);

  if (flag == 1)
    {
      unlink (filename);
    }

  return 0;
}

int
fn_glo_save (SOCKET CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
	     void **CAS_FN_ARG_ARGV, T_NET_BUF * CAS_FN_ARG_NET_BUF,
	     T_REQ_INFO * CAS_FN_ARG_REQ_INFO)
{
  DB_OBJECT *obj;
  char flag;
  char *filename;
  int file_size;
  int err_code;
  char tmp_file[PATH_MAX], dirname[PATH_MAX];
  int filename_size;

  if (CAS_FN_ARG_ARGC < 4)
    {
#if defined(CAS_FOR_DBMS)
      ERROR_INFO_SET (CAS_ER_ARGS, CAS_ERROR_INDICATOR);
      NET_BUF_ERR_SET (CAS_FN_ARG_NET_BUF);
#else /* CAS_FOR_DBMS */
      net_buf_cp_int (CAS_FN_ARG_NET_BUF, CAS_ER_ARGS, NULL);
#endif /* CAS_FOR_DBMS */
      return 0;
    }

  NET_ARG_GET_OBJECT (obj, CAS_FN_ARG_ARGV[0]);
  NET_ARG_GET_CHAR (flag, CAS_FN_ARG_ARGV[1]);
  NET_ARG_GET_STR (filename, filename_size, CAS_FN_ARG_ARGV[2]);
  NET_ARG_GET_INT (file_size, CAS_FN_ARG_ARGV[3]);

  cas_log_write (0, true, "glo_save");

  if (flag == 1)
    {
      if (file_size < 0)
	{
#if defined(CAS_FOR_DBMS)
	  ERROR_INFO_SET (CAS_ER_ARGS, CAS_ERROR_INDICATOR);
	  NET_BUF_ERR_SET (CAS_FN_ARG_NET_BUF);
#else /* CAS_FOR_DBMS */
	  net_buf_cp_int (CAS_FN_ARG_NET_BUF, CAS_ER_ARGS, NULL);
#endif /* CAS_FOR_DBMS */
	  return 0;
	}
#ifndef LIBCAS_FOR_JSP
#if defined(WINDOWS)
      as_info->glo_read_size = file_size;
#endif /* WINDOWS */
#endif /* !LIBCAS_FOR_JSP */
      get_cubrid_file (FID_CAS_TMPGLO_DIR, dirname);
#ifdef LIBCAS_FOR_JSP
      snprintf (tmp_file, sizeof (tmp_file) - 1, "%s%d.glo", dirname,
		(int) getpid ());
#else /* LIBCAS_FOR_JSP */
      snprintf (tmp_file, sizeof (tmp_file) - 1, "%s%s_%d.glo", dirname,
		broker_name, shm_as_index + 1);
#endif /* LIBCAS_FOR_JSP */
      filename = tmp_file;
      err_code = net_read_to_file (CAS_FN_ARG_SOCK_FD, file_size, filename);
      if (err_code < 0)
	{
#if defined(CAS_FOR_DBMS)
	  NET_BUF_ERR_SET (CAS_FN_ARG_NET_BUF);
#else /* CAS_FOR_DBMS */
	  net_buf_cp_int (CAS_FN_ARG_NET_BUF, err_code, NULL);
#endif /* CAS_FOR_DBMS */
	  return err_code;
	}
    }
  else
    {
      if (filename_size < 1 || filename[filename_size - 1] != '\0')
	{
#if defined(CAS_FOR_DBMS)
	  ERROR_INFO_SET (CAS_ER_ARGS, CAS_ERROR_INDICATOR);
	  NET_BUF_ERR_SET (CAS_FN_ARG_NET_BUF);
#else /* CAS_FOR_DBMS */
	  net_buf_cp_int (CAS_FN_ARG_NET_BUF, CAS_ER_ARGS, NULL);
#endif /* CAS_FOR_DBMS */
	  return 0;
	}
    }

  if ((err_code = ux_check_object (obj, CAS_FN_ARG_NET_BUF)) < 0)
    {
#if defined(CAS_FOR_DBMS)
      NET_BUF_ERR_SET (CAS_FN_ARG_NET_BUF);
#else /* CAS_FOR_DBMS */
      if (err_code != CAS_ER_DBMS)
	{
	  net_buf_cp_int (CAS_FN_ARG_NET_BUF, err_code, NULL);
	}
#endif /* CAS_FOR_DBMS */
      return 0;
    }

  ux_glo_save (obj, filename, CAS_FN_ARG_NET_BUF);

  if (flag == 1)
    {
      unlink (filename);
    }

  obj = NULL;

  return 0;
}

int
fn_glo_load (SOCKET CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
	     void **CAS_FN_ARG_ARGV, T_NET_BUF * CAS_FN_ARG_NET_BUF,
	     T_REQ_INFO * CAS_FN_ARG_REQ_INFO)
{
  DB_OBJECT *obj;
  int err_code;

  if (CAS_FN_ARG_ARGC < 1)
    {
#if defined(CAS_FOR_DBMS)
      ERROR_INFO_SET (CAS_ER_ARGS, CAS_ERROR_INDICATOR);
      NET_BUF_ERR_SET (CAS_FN_ARG_NET_BUF);
#else /* CAS_FOR_DBMS */
      net_buf_cp_int (CAS_FN_ARG_NET_BUF, CAS_ER_ARGS, NULL);
#endif /* CAS_FOR_DBMS */
      return 0;
    }

  NET_ARG_GET_OBJECT (obj, CAS_FN_ARG_ARGV[0]);
  if ((err_code = ux_check_object (obj, CAS_FN_ARG_NET_BUF)) < 0)
    {
#if defined(CAS_FOR_DBMS)
      NET_BUF_ERR_SET (CAS_FN_ARG_NET_BUF);
#else /* CAS_FOR_DBMS */
      if (err_code != CAS_ER_DBMS)
	{
	  net_buf_cp_int (CAS_FN_ARG_NET_BUF, err_code, NULL);
	}
#endif /* CAS_FOR_DBMS */
      return 0;
    }

  cas_log_write (0, true, "glo_load");

  ux_glo_load (CAS_FN_ARG_SOCK_FD, obj, CAS_FN_ARG_NET_BUF);
  obj = NULL;

  return 0;
}
#endif /* !CAS_FOR_ORACLE && !CAS_FOR_MYSQL */

int
fn_get_db_version (SOCKET CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
		   void **CAS_FN_ARG_ARGV, T_NET_BUF * CAS_FN_ARG_NET_BUF,
		   T_REQ_INFO * CAS_FN_ARG_REQ_INFO)
{
  char auto_commit_mode;
  cas_log_write (0, true, "get_version");

  if (CAS_FN_ARG_ARGC < 1)
    {
#if defined(CAS_FOR_DBMS)
      ERROR_INFO_SET (CAS_ER_ARGS, CAS_ERROR_INDICATOR);
      NET_BUF_ERR_SET (CAS_FN_ARG_NET_BUF);
#else /* CAS_FOR_DBMS */
      net_buf_cp_int (CAS_FN_ARG_NET_BUF, CAS_ER_ARGS, NULL);
#endif /* CAS_FOR_DBMS */
      return 0;
    }

  NET_ARG_GET_CHAR (auto_commit_mode, CAS_FN_ARG_ARGV[0]);

  ux_get_db_version (CAS_FN_ARG_NET_BUF, CAS_FN_ARG_REQ_INFO);

#ifndef LIBCAS_FOR_JSP
  if (auto_commit_mode == TRUE)
    {
      req_info->need_auto_commit = TRAN_AUTOCOMMIT;
    }
#endif /* !LIBCAS_FOR_JSP */

  return 0;
}

#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
int
fn_get_class_num_objs (SOCKET CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
		       void **CAS_FN_ARG_ARGV,
		       T_NET_BUF * CAS_FN_ARG_NET_BUF,
		       T_REQ_INFO * CAS_FN_ARG_REQ_INFO)
{
  char *class_name;
  char flag;
  int class_name_size;

  if (CAS_FN_ARG_ARGC < 2)
    {
#if defined(CAS_FOR_DBMS)
      ERROR_INFO_SET (CAS_ER_ARGS, CAS_ERROR_INDICATOR);
      NET_BUF_ERR_SET (CAS_FN_ARG_NET_BUF);
#else /* CAS_FOR_DBMS */
      net_buf_cp_int (CAS_FN_ARG_NET_BUF, CAS_ER_ARGS, NULL);
#endif /* CAS_FOR_DBMS */
      return 0;
    }

  cas_log_write (0, true, "get_class_num_objs");

  NET_ARG_GET_STR (class_name, class_name_size, CAS_FN_ARG_ARGV[0]);
  NET_ARG_GET_CHAR (flag, CAS_FN_ARG_ARGV[1]);

  ux_get_class_num_objs (class_name, flag, CAS_FN_ARG_NET_BUF);

  return 0;
}

int
fn_oid (SOCKET CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
	void **CAS_FN_ARG_ARGV, T_NET_BUF * CAS_FN_ARG_NET_BUF,
	T_REQ_INFO * CAS_FN_ARG_REQ_INFO)
{
  DB_OBJECT *obj;
  char cmd;
  int err_code;
  char *res_msg = NULL;

  if (CAS_FN_ARG_ARGC < 2)
    {
#if defined(CAS_FOR_DBMS)
      ERROR_INFO_SET (CAS_ER_ARGS, CAS_ERROR_INDICATOR);
      goto fn_oid_error;
#else /* CAS_FOR_DBMS */
      net_buf_cp_int (CAS_FN_ARG_NET_BUF, CAS_ER_ARGS, NULL);
      return 0;
#endif /* CAS_FOR_DBMS */
    }

  NET_ARG_GET_CHAR (cmd, CAS_FN_ARG_ARGV[0]);
  NET_ARG_GET_OBJECT (obj, CAS_FN_ARG_ARGV[1]);
  if (cmd != CCI_OID_IS_INSTANCE)
    {
      if ((err_code = ux_check_object (obj, CAS_FN_ARG_NET_BUF)) < 0)
	{
#if defined(CAS_FOR_DBMS)
	  goto fn_oid_error;
#else /* CAS_FOR_DBMS */
	  if (err_code != CAS_ER_DBMS)
	    net_buf_cp_int (CAS_FN_ARG_NET_BUF, err_code, NULL);
	  return 0;
#endif /* CAS_FOR_DBMS */
	}
    }

  if (cmd == CCI_OID_DROP)
    {
      cas_log_write (0, true, "oid drop");
      if (obj == NULL)
	{
#if defined(CAS_FOR_DBMS)
	  ERROR_INFO_SET (CAS_ER_OBJECT, CAS_ERROR_INDICATOR);
	  goto fn_oid_error;
#else /* CAS_FOR_DBMS */
	  net_buf_cp_int (CAS_FN_ARG_NET_BUF, CAS_ER_OBJECT, NULL);
	  return 0;
#endif /* CAS_FOR_DBMS */
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
	  if (db_is_instance (obj))
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
#if defined(CAS_FOR_DBMS)
	  ERROR_INFO_SET (CAS_ER_OBJECT, CAS_ERROR_INDICATOR);
	  goto fn_oid_error;
#else /* CAS_FOR_DBMS */
	  net_buf_cp_int (CAS_FN_ARG_NET_BUF, CAS_ER_OBJECT, NULL);
	  return 0;
#endif /* CAS_FOR_DBMS */
	}
      err_code = db_lock_read (obj);
    }
  else if (cmd == CCI_OID_LOCK_WRITE)
    {
      cas_log_write (0, true, "oid lock_write");
      if (obj == NULL)
	{
#if defined(CAS_FOR_DBMS)
	  ERROR_INFO_SET (CAS_ER_OBJECT, CAS_ERROR_INDICATOR);
	  goto fn_oid_error;
#else /* CAS_FOR_DBMS */
	  net_buf_cp_int (CAS_FN_ARG_NET_BUF, CAS_ER_OBJECT, NULL);
	  return 0;
#endif /* CAS_FOR_DBMS */
	}
      err_code = db_lock_write (obj);
    }
  else if (cmd == CCI_OID_CLASS_NAME)
    {
      cas_log_write (0, true, "oid get_class_name");
      if (obj == NULL)
	{
#if defined(CAS_FOR_DBMS)
	  ERROR_INFO_SET (CAS_ER_OBJECT, CAS_ERROR_INDICATOR);
	  goto fn_oid_error;
#else /* CAS_FOR_DBMS */
	  net_buf_cp_int (CAS_FN_ARG_NET_BUF, CAS_ER_OBJECT, NULL);
	  return 0;
#endif /* CAS_FOR_DBMS */
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
  else if (cmd == CCI_OID_IS_GLO_INSTANCE)
    {
      DB_OBJECT *class_obj, *glo_class_obj;

      cas_log_write (0, true, "oid is_glo_instance");

      class_obj = db_get_class (obj);
      glo_class_obj = db_find_class ("glo");
      err_code = 0;
      if ((class_obj != NULL) && (glo_class_obj != NULL)
	  && (db_is_subclass (class_obj, glo_class_obj)))
	{
	  err_code = 1;
	}
    }
  else
    {
#if defined(CAS_FOR_DBMS)
      ERROR_INFO_SET (CAS_ER_INTERNAL, CAS_ERROR_INDICATOR);
      goto fn_oid_error;
#else /* CAS_FOR_DBMS */
      net_buf_cp_int (CAS_FN_ARG_NET_BUF, CAS_ER_INTERNAL, NULL);
      return 0;
#endif /* CAS_FOR_DBMS */
    }

  if (err_code < 0)
    {
#if defined(CAS_FOR_DBMS)
      ERROR_INFO_SET (err_code, DBMS_ERROR_INDICATOR);
      goto fn_oid_error;
#else /* CAS_FOR_DBMS */
      DB_ERR_MSG_SET (CAS_FN_ARG_NET_BUF, err_code);
#endif /* CAS_FOR_DBMS */
    }
  else
    {
      net_buf_cp_int (CAS_FN_ARG_NET_BUF, err_code, NULL);
      if (cmd == CCI_OID_CLASS_NAME)
	{
	  net_buf_cp_str (CAS_FN_ARG_NET_BUF, res_msg, strlen (res_msg) + 1);
	}
    }

  return 0;

#if defined(CAS_FOR_DBMS)
fn_oid_error:
  NET_BUF_ERR_SET (CAS_FN_ARG_NET_BUF);
  return 0;
#endif /* CAS_FOR_DBMS */
}

int
fn_collection (SOCKET CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
	       void **CAS_FN_ARG_ARGV, T_NET_BUF * CAS_FN_ARG_NET_BUF,
	       T_REQ_INFO * CAS_FN_ARG_REQ_INFO)
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

  if (CAS_FN_ARG_ARGC < 3)
    {
#if defined(CAS_FOR_DBMS)
      ERROR_INFO_SET (CAS_ER_ARGS, CAS_ERROR_INDICATOR);
      NET_BUF_ERR_SET (CAS_FN_ARG_NET_BUF);
#else /* CAS_FOR_DBMS */
      net_buf_cp_int (CAS_FN_ARG_NET_BUF, CAS_ER_ARGS, NULL);
#endif /* CAS_FOR_DBMS */
      return 0;
    }

  NET_ARG_GET_CHAR (cmd, CAS_FN_ARG_ARGV[0]);
  NET_ARG_GET_OBJECT (obj, CAS_FN_ARG_ARGV[1]);

  if ((err_code = ux_check_object (obj, CAS_FN_ARG_NET_BUF)) < 0)
    {
#if defined(CAS_FOR_DBMS)
      NET_BUF_ERR_SET (CAS_FN_ARG_NET_BUF);
#else /* CAS_FOR_DBMS */
      if (err_code != CAS_ER_DBMS)
	net_buf_cp_int (CAS_FN_ARG_NET_BUF, err_code, NULL);
#endif /* CAS_FOR_DBMS */
      return 0;
    }

  err_code = 0;
  value_index = 0;

  switch (cmd)
    {
    case CCI_COL_GET:
    case CCI_COL_SIZE:
      NET_ARG_GET_STR (attr_name, attr_name_size, CAS_FN_ARG_ARGV[2]);
      break;
    case CCI_COL_SET_DROP:
    case CCI_COL_SET_ADD:
      if (CAS_FN_ARG_ARGC < 5)
	{
#if defined(CAS_FOR_DBMS)
	  err_code = ERROR_INFO_SET (CAS_ER_ARGS, CAS_ERROR_INDICATOR);
#else /* CAS_FOR_DBMS */
	  err_code = CAS_ER_ARGS;
#endif /* CAS_FOR_DBMS */
	}

      else
	{
	  NET_ARG_GET_STR (attr_name, attr_name_size, CAS_FN_ARG_ARGV[2]);
	}
      value_index = 3;
      break;
    case CCI_COL_SEQ_DROP:
      if (CAS_FN_ARG_ARGC < 4)
	{
#if defined(CAS_FOR_DBMS)
	  err_code = ERROR_INFO_SET (CAS_ER_ARGS, CAS_ERROR_INDICATOR);
#else /* CAS_FOR_DBMS */
	  err_code = CAS_ER_ARGS;
#endif /* CAS_FOR_DBMS */
	}
      else
	{
	  NET_ARG_GET_INT (seq_index, CAS_FN_ARG_ARGV[2]);
	  NET_ARG_GET_STR (attr_name, attr_name_size, CAS_FN_ARG_ARGV[3]);
	}
      break;
    case CCI_COL_SEQ_INSERT:
    case CCI_COL_SEQ_PUT:
      if (CAS_FN_ARG_ARGC < 6)
	{
#if defined(CAS_FOR_DBMS)
	  err_code = ERROR_INFO_SET (CAS_ER_ARGS, CAS_ERROR_INDICATOR);
#else /* CAS_FOR_DBMS */
	  err_code = CAS_ER_ARGS;
#endif /* CAS_FOR_DBMS */
	}
      else
	{
	  NET_ARG_GET_INT (seq_index, CAS_FN_ARG_ARGV[2]);
	  NET_ARG_GET_STR (attr_name, attr_name_size, CAS_FN_ARG_ARGV[3]);
	}

      value_index = 4;
      break;
    default:
#if defined(CAS_FOR_DBMS)
      err_code = ERROR_INFO_SET (CAS_ER_INTERNAL, CAS_ERROR_INDICATOR);
#else /* CAS_FOR_DBMS */
      err_code = CAS_ER_INTERNAL;
#endif /* CAS_FOR_DBMS */
    }

  if (err_code < 0)
    {
#if defined(CAS_FOR_DBMS)
      NET_BUF_ERR_SET (CAS_FN_ARG_NET_BUF);
#else /* CAS_FOR_DBMS */
      net_buf_cp_int (CAS_FN_ARG_NET_BUF, err_code, NULL);
#endif /* CAS_FOR_DBMS */
      return 0;
    }

  if (attr_name_size < 1)
    {
#if defined(CAS_FOR_DBMS)
      ERROR_INFO_SET (CAS_ER_ARGS, CAS_ERROR_INDICATOR);
#else /* CAS_FOR_DBMS */
      net_buf_cp_int (CAS_FN_ARG_NET_BUF, CAS_ER_ARGS, NULL);
#endif /* CAS_FOR_DBMS */
      goto fn_col_finale;
    }

  attr = db_get_attribute (obj, attr_name);
  if (attr == NULL)
    {
#if defined(CAS_FOR_DBMS)
      ERROR_INFO_SET (db_error_code (), DBMS_ERROR_INDICATOR);
#else /* CAS_FOR_DBMS */
      DB_ERR_MSG_SET (CAS_FN_ARG_NET_BUF, db_error_code ());
#endif /* CAS_FOR_DBMS */
      goto fn_col_finale;
    }
  domain = db_attribute_domain (attr);
  col_type = ux_db_type_to_cas_type (db_domain_type (domain));
  if (col_type != CCI_U_TYPE_SET
      && col_type != CCI_U_TYPE_MULTISET && col_type != CCI_U_TYPE_SEQUENCE)
    {
#if defined(CAS_FOR_DBMS)
      ERROR_INFO_SET (CAS_ER_NOT_COLLECTION, CAS_ERROR_INDICATOR);
#else /* CAS_FOR_DBMS */
      net_buf_cp_int (CAS_FN_ARG_NET_BUF, CAS_ER_NOT_COLLECTION, NULL);
#endif /* CAS_FOR_DBMS */
      goto fn_col_finale;
    }
  ele_type = get_set_domain (domain, NULL, NULL, &db_type);
  ele_domain = db_domain_set (domain);
  if (ele_type <= 0)
    {
#if defined(CAS_FOR_DBMS)
      ERROR_INFO_SET (CAS_ER_COLLECTION_DOMAIN, CAS_ERROR_INDICATOR);
#else /* CAS_FOR_DBMS */
      net_buf_cp_int (CAS_FN_ARG_NET_BUF, CAS_ER_COLLECTION_DOMAIN, NULL);
#endif /* CAS_FOR_DBMS */
      goto fn_col_finale;
    }

  err_code = db_get (obj, attr_name, &val);
  if (err_code < 0)
    {
#if defined(CAS_FOR_DBMS)
      ERROR_INFO_SET (err_code, DBMS_ERROR_INDICATOR);
#else /* CAS_FOR_DBMS */
      DB_ERR_MSG_SET (CAS_FN_ARG_NET_BUF, err_code);
#endif /* CAS_FOR_DBMS */
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
      err_code =
	make_bind_value (1, 2, CAS_FN_ARG_ARGV + value_index, &ele_val,
			 CAS_FN_ARG_NET_BUF, db_type);
      if (err_code < 0)
	{
#if !defined(CAS_FOR_DBMS)
	  if (err_code == CAS_ER_DBMS)
	    goto fn_col_finale;
	  net_buf_cp_int (CAS_FN_ARG_NET_BUF, err_code, NULL);
#endif /* !CAS_FOR_DBMS */
	  goto fn_col_finale;
	}
    }

  err_code = 0;
  switch (cmd)
    {
    case CCI_COL_GET:
      ux_col_get (collection, col_type, ele_type, ele_domain,
		  CAS_FN_ARG_NET_BUF);
      break;
    case CCI_COL_SIZE:
      ux_col_size (collection, CAS_FN_ARG_NET_BUF);
      break;
    case CCI_COL_SET_DROP:
      err_code = ux_col_set_drop (collection, ele_val, CAS_FN_ARG_NET_BUF);
      break;
    case CCI_COL_SET_ADD:
      err_code = ux_col_set_add (collection, ele_val, CAS_FN_ARG_NET_BUF);
      break;
    case CCI_COL_SEQ_DROP:
      err_code = ux_col_seq_drop (collection, seq_index, CAS_FN_ARG_NET_BUF);
      break;
    case CCI_COL_SEQ_INSERT:
      err_code =
	ux_col_seq_insert (collection, seq_index, ele_val,
			   CAS_FN_ARG_NET_BUF);
      break;
    case CCI_COL_SEQ_PUT:
      err_code =
	ux_col_seq_put (collection, seq_index, ele_val, CAS_FN_ARG_NET_BUF);
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

fn_col_finale:
#if defined(CAS_FOR_DBMS)
  NET_BUF_ERR_SET (CAS_FN_ARG_NET_BUF);
#endif /* CAS_FOR_DBMS */
  return 0;
}

/* MYSQL : NOT SUPPORT MULTIPLE STATEMENT */
int
fn_next_result (SOCKET CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
		void **CAS_FN_ARG_ARGV, T_NET_BUF * CAS_FN_ARG_NET_BUF,
		T_REQ_INFO * CAS_FN_ARG_REQ_INFO)
{
  int srv_h_id;
  char flag;
  int func_args;
  T_SRV_HANDLE *srv_handle;

  func_args = 2;

  if (CAS_FN_ARG_ARGC < func_args)
    {
#if defined(CAS_FOR_DBMS)
      ERROR_INFO_SET (CAS_ER_ARGS, CAS_ERROR_INDICATOR);
      NET_BUF_ERR_SET (CAS_FN_ARG_NET_BUF);
#else /* CAS_FOR_DBMS */
      net_buf_cp_int (CAS_FN_ARG_NET_BUF, CAS_ER_ARGS, NULL);
#endif /* CAS_FOR_DBMS */
      return 0;
    }

  NET_ARG_GET_INT (srv_h_id, CAS_FN_ARG_ARGV[0]);
  NET_ARG_GET_CHAR (flag, CAS_FN_ARG_ARGV[1]);

  srv_handle = hm_find_srv_handle (srv_h_id);
  if (srv_handle == NULL)
    {
      return -1;
    }

  cas_log_write (SRV_HANDLE_QUERY_SEQ_NUM (srv_handle), false,
		 "next_result %d %s", srv_h_id,
		 (srv_handle->use_query_cache == true) ? "(QC)" : "");

  ux_next_result (srv_handle, flag, CAS_FN_ARG_NET_BUF, CAS_FN_ARG_REQ_INFO);

  return 0;
}
#endif /* !CAS_FOR_ORACLE && !CAS_FOR_MYSQL */

#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
int
fn_execute_batch (SOCKET CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
		  void **CAS_FN_ARG_ARGV, T_NET_BUF * CAS_FN_ARG_NET_BUF,
		  T_REQ_INFO * CAS_FN_ARG_REQ_INFO)
{
  /* argv[0] : auto commit flag */
  cas_log_write (0, true, "execute_batch %d", CAS_FN_ARG_ARGC - 1);

  ux_execute_batch (CAS_FN_ARG_ARGC, CAS_FN_ARG_ARGV, CAS_FN_ARG_NET_BUF,
		    CAS_FN_ARG_REQ_INFO);

  cas_log_write (0, true, "execute_batch end");

  return 0;
}

int
fn_execute_array (SOCKET CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
		  void **CAS_FN_ARG_ARGV, T_NET_BUF * CAS_FN_ARG_NET_BUF,
		  T_REQ_INFO * CAS_FN_ARG_REQ_INFO)
{
  int srv_h_id;
  T_SRV_HANDLE *srv_handle;

  /* argv[0] : service handle
     argv[1] : auto commit flag */
  if (CAS_FN_ARG_ARGC < 2)
    {
      net_buf_cp_int (CAS_FN_ARG_NET_BUF, CAS_ER_ARGS, NULL);
      return 0;
    }

  NET_ARG_GET_INT (srv_h_id, CAS_FN_ARG_ARGV[0]);

  srv_handle = hm_find_srv_handle (srv_h_id);

#ifndef LIBCAS_FOR_JSP
  bind_value_log (2, CAS_FN_ARG_ARGC - 1, CAS_FN_ARG_ARGV, 0, NULL,
		  SRV_HANDLE_QUERY_SEQ_NUM (srv_handle));
#endif /* !LIBCAS_FOR_JSP */

  cas_log_write (SRV_HANDLE_QUERY_SEQ_NUM (srv_handle), false,
		 "execute_array : srv_h_id %d %d",
		 srv_h_id, (CAS_FN_ARG_ARGC - 2) / 2);

  ux_execute_array (srv_handle, CAS_FN_ARG_ARGC, CAS_FN_ARG_ARGV,
		    CAS_FN_ARG_NET_BUF, CAS_FN_ARG_REQ_INFO);

  return 0;
}

int
fn_cursor_update (SOCKET CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
		  void **CAS_FN_ARG_ARGV, T_NET_BUF * CAS_FN_ARG_NET_BUF,
		  T_REQ_INFO * CAS_FN_ARG_REQ_INFO)
{
  int srv_h_id;
  int cursor_pos;
  T_SRV_HANDLE *srv_handle;

  if (CAS_FN_ARG_ARGC < 2)
    {
#if defined(CAS_FOR_DBMS)
      ERROR_INFO_SET (CAS_ER_ARGS, CAS_ERROR_INDICATOR);
      NET_BUF_ERR_SET (CAS_FN_ARG_NET_BUF);
#else /* CAS_FOR_DBMS */
      net_buf_cp_int (CAS_FN_ARG_NET_BUF, CAS_ER_ARGS, NULL);
#endif /* CAS_FOR_DBMS */
      return 0;
    }

  NET_ARG_GET_INT (srv_h_id, CAS_FN_ARG_ARGV[0]);
  NET_ARG_GET_INT (cursor_pos, CAS_FN_ARG_ARGV[1]);

  srv_handle = hm_find_srv_handle (srv_h_id);

  cas_log_write (SRV_HANDLE_QUERY_SEQ_NUM (srv_handle), false,
		 "cursor_update srv_h_id %d, cursor %d",
		 srv_h_id, cursor_pos);

  ux_cursor_update (srv_handle, cursor_pos, CAS_FN_ARG_ARGC,
		    CAS_FN_ARG_ARGV, CAS_FN_ARG_NET_BUF);

  return 0;
}

int
fn_get_attr_type_str (SOCKET CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
		      void **CAS_FN_ARG_ARGV,
		      T_NET_BUF * CAS_FN_ARG_NET_BUF,
		      T_REQ_INFO * CAS_FN_ARG_REQ_INFO)
{
  int size;
  char *class_name;
  char *attr_name;

  if (CAS_FN_ARG_ARGC < 2)
    {
#if defined(CAS_FOR_DBMS)
      ERROR_INFO_SET (CAS_ER_ARGS, CAS_ERROR_INDICATOR);
      NET_BUF_ERR_SET (CAS_FN_ARG_NET_BUF);
#else /* CAS_FOR_DBMS */
      net_buf_cp_int (CAS_FN_ARG_NET_BUF, CAS_ER_ARGS, NULL);
#endif /* CAS_FOR_DBMS */
      return 0;
    }

  NET_ARG_GET_STR (class_name, size, CAS_FN_ARG_ARGV[0]);
  NET_ARG_GET_STR (attr_name, size, CAS_FN_ARG_ARGV[1]);

  ux_get_attr_type_str (class_name, attr_name, CAS_FN_ARG_NET_BUF,
			CAS_FN_ARG_REQ_INFO);

  return 0;
}

int
fn_get_query_info (SOCKET CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
		   void **CAS_FN_ARG_ARGV, T_NET_BUF * CAS_FN_ARG_NET_BUF,
		   T_REQ_INFO * CAS_FN_ARG_REQ_INFO)
{
  char info_type;
  T_SRV_HANDLE *srv_handle = NULL;
  int srv_h_id, size, stmt_id;
  char *sql_stmt = NULL;
  DB_SESSION *session;
  DB_QUERY_RESULT *result;

  if (CAS_FN_ARG_ARGC < 2)
    {
#if defined(CAS_FOR_DBMS)
      ERROR_INFO_SET (CAS_ER_ARGS, CAS_ERROR_INDICATOR);
      NET_BUF_ERR_SET (CAS_FN_ARG_NET_BUF);
#else /* CAS_FOR_DBMS */
      net_buf_cp_int (CAS_FN_ARG_NET_BUF, CAS_ER_ARGS, NULL);
#endif /* CAS_FOR_DBMS */
      return 0;
    }

  NET_ARG_GET_INT (srv_h_id, CAS_FN_ARG_ARGV[0]);
  NET_ARG_GET_CHAR (info_type, CAS_FN_ARG_ARGV[1]);
  if (CAS_FN_ARG_ARGC >= 3)
    NET_ARG_GET_STR (sql_stmt, size, CAS_FN_ARG_ARGV[2]);

  if (sql_stmt != NULL)
    {
      srv_h_id = hm_new_srv_handle (&srv_handle, QUERY_SEQ_NUM_NEXT_VALUE ());
      if (srv_h_id < 0)
	{
	  net_buf_cp_byte (CAS_FN_ARG_NET_BUF, '\0');
	  goto end;
	}
      srv_handle->query_info_flag = TRUE;

      if (cas_log_query_info_init (srv_h_id) < 0)
	{
	  net_buf_cp_byte (CAS_FN_ARG_NET_BUF, '\0');
	  goto end;
	}

      set_optimization_level (514);

      if (!(session = db_open_buffer (sql_stmt)))
	{
#if defined(CAS_FOR_DBMS)
	  ERROR_INFO_SET (db_error_code (), DBMS_ERROR_INDICATOR);
	  NET_BUF_ERR_SET (CAS_FN_ARG_NET_BUF);
#else /* CAS_FOR_DBMS */
	  DB_ERR_MSG_SET (CAS_FN_ARG_NET_BUF, db_error_code ());
#endif /* CAS_FOR_DBMS */
	  goto end;
	}

      if ((stmt_id = db_compile_statement (session)) < 0)
	{
#if defined(CAS_FOR_DBMS)
	  ERROR_INFO_SET (stmt_id, DBMS_ERROR_INDICATOR);
	  NET_BUF_ERR_SET (CAS_FN_ARG_NET_BUF);
#else /* CAS_FOR_DBMS */
	  DB_ERR_MSG_SET (CAS_FN_ARG_NET_BUF, stmt_id);
#endif /* CAS_FOR_DBMS */
	  db_close_session (session);
	  goto end;
	}

      if (db_execute_statement (session, stmt_id, &result) < 0)
	{
#if defined(CAS_FOR_DBMS)
	  ERROR_INFO_SET (db_error_code (), DBMS_ERROR_INDICATOR);
	  NET_BUF_ERR_SET (CAS_FN_ARG_NET_BUF);
#else /* CAS_FOR_DBMS */
	  DB_ERR_MSG_SET (CAS_FN_ARG_NET_BUF, db_error_code ());
#endif /* CAS_FOR_DBMS */
	  db_close_session (session);
	  goto end;
	}

      db_query_end (result);
      db_close_session (session);

#if !defined(WINDOWS)
      cas_log_query_info_next ();
      histo_print (NULL);
      cas_log_query_info_end ();
#endif /* !WINDOWS */
    }

  ux_get_query_info (srv_h_id, info_type, CAS_FN_ARG_NET_BUF);

end:
  if (sql_stmt != NULL)
    {
      set_optimization_level (1);
      hm_srv_handle_free (srv_h_id);
    }

  return 0;
}

int
fn_savepoint (SOCKET CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
	      void **CAS_FN_ARG_ARGV, T_NET_BUF * CAS_FN_ARG_NET_BUF,
	      T_REQ_INFO * CAS_FN_ARG_REQ_INFO)
{
  int err_code;
  char cmd;
  char *savepoint_name;
  int savepoint_name_size;

  if (CAS_FN_ARG_ARGC < 2)
    {
#if defined(CAS_FOR_DBMS)
      ERROR_INFO_SET (CAS_ER_ARGS, CAS_ERROR_INDICATOR);
      NET_BUF_ERR_SET (CAS_FN_ARG_NET_BUF);
#else /* CAS_FOR_DBMS */
      net_buf_cp_int (CAS_FN_ARG_NET_BUF, CAS_ER_ARGS, NULL);
#endif /* CAS_FOR_DBMS */
      return 0;
    }

  NET_ARG_GET_CHAR (cmd, CAS_FN_ARG_ARGV[0]);
  NET_ARG_GET_STR (savepoint_name, savepoint_name_size, CAS_FN_ARG_ARGV[1]);
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
#if defined(CAS_FOR_DBMS)
      ERROR_INFO_SET (CAS_ER_INTERNAL, CAS_ERROR_INDICATOR);
      NET_BUF_ERR_SET (CAS_FN_ARG_NET_BUF);
#else /* CAS_FOR_DBMS */
      net_buf_cp_int (CAS_FN_ARG_NET_BUF, CAS_ER_INTERNAL, NULL);
#endif /* CAS_FOR_DBMS */
      return 0;
    }

  if (err_code < 0)
    {
#if defined(CAS_FOR_DBMS)
      ERROR_INFO_SET (err_code, DBMS_ERROR_INDICATOR);
      NET_BUF_ERR_SET (CAS_FN_ARG_NET_BUF);
#else /* CAS_FOR_DBMS */
      DB_ERR_MSG_SET (CAS_FN_ARG_NET_BUF, err_code);
#endif /* CAS_FOR_DBMS */
    }
  else
    {
      net_buf_cp_int (CAS_FN_ARG_NET_BUF, 0, NULL);
    }

  return 0;
}

int
fn_parameter_info (SOCKET CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
		   void **CAS_FN_ARG_ARGV, T_NET_BUF * CAS_FN_ARG_NET_BUF,
		   T_REQ_INFO * CAS_FN_ARG_REQ_INFO)
{
  int srv_h_id;

  if (CAS_FN_ARG_ARGC < 1)
    {
#if defined(CAS_FOR_DBMS)
      ERROR_INFO_SET (CAS_ER_ARGS, CAS_ERROR_INDICATOR);
      NET_BUF_ERR_SET (CAS_FN_ARG_NET_BUF);
#else /* CAS_FOR_DBMS */
      net_buf_cp_int (CAS_FN_ARG_NET_BUF, CAS_ER_ARGS, NULL);
#endif /* CAS_FOR_DBMS */
      return 0;
    }

  NET_ARG_GET_INT (srv_h_id, CAS_FN_ARG_ARGV[0]);

  ux_get_parameter_info (srv_h_id, CAS_FN_ARG_NET_BUF);

  return 0;
}

int
fn_glo_cmd (SOCKET CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
	    void **CAS_FN_ARG_ARGV, T_NET_BUF * CAS_FN_ARG_NET_BUF,
	    T_REQ_INFO * CAS_FN_ARG_REQ_INFO)
{
  int glo_cmd;
  DB_OBJECT *glo_obj;
  int start_pos_index;
  DB_VALUE ret_val, arg1, arg2;
  int err_code;
  char *data_buf = NULL;
  DB_VALUE *args[2];

  if (CAS_FN_ARG_ARGC < 2)
    {
#if defined(CAS_FOR_DBMS)
      ERROR_INFO_SET (CAS_ER_ARGS, CAS_ERROR_INDICATOR);
      NET_BUF_ERR_SET (CAS_FN_ARG_NET_BUF);
#else /* CAS_FOR_DBMS */
      net_buf_cp_int (CAS_FN_ARG_NET_BUF, CAS_ER_ARGS, NULL);
#endif /* CAS_FOR_DBMS */
      return 0;
    }

  NET_ARG_GET_CHAR (glo_cmd, CAS_FN_ARG_ARGV[0]);
  if (glo_cmd < GLO_CMD_FIRST || glo_cmd > GLO_CMD_LAST)
    {
#if defined(CAS_FOR_DBMS)
      ERROR_INFO_SET (CAS_ER_GLO_CMD, CAS_ERROR_INDICATOR);
      NET_BUF_ERR_SET (CAS_FN_ARG_NET_BUF);
#else /* CAS_FOR_DBMS */
      net_buf_cp_int (CAS_FN_ARG_NET_BUF, CAS_ER_GLO_CMD, NULL);
#endif /* CAS_FOR_DBMS */
      return 0;
    }

  if (CAS_FN_ARG_ARGC < glo_cmd_info[glo_cmd].num_args)
    {
#if defined(CAS_FOR_DBMS)
      ERROR_INFO_SET (CAS_ER_ARGS, CAS_ERROR_INDICATOR);
      NET_BUF_ERR_SET (CAS_FN_ARG_NET_BUF);
#else /* CAS_FOR_DBMS */
      net_buf_cp_int (CAS_FN_ARG_NET_BUF, CAS_ER_ARGS, NULL);
#endif /* CAS_FOR_DBMS */
      return 0;
    }

  NET_ARG_GET_OBJECT (glo_obj, CAS_FN_ARG_ARGV[1]);
  if ((err_code = ux_check_object (glo_obj, CAS_FN_ARG_NET_BUF)) < 0)
    {
#if defined(CAS_FOR_DBMS)
      NET_BUF_ERR_SET (CAS_FN_ARG_NET_BUF);
#else /* CAS_FOR_DBMS */
      if (err_code != CAS_ER_DBMS)
	{
	  net_buf_cp_int (CAS_FN_ARG_NET_BUF, err_code, NULL);
	}
#endif /* CAS_FOR_DBMS */
      return 0;
    }

  cas_log_write (0, false, "glo_cmd");
#ifndef LIBCAS_FOR_JSP
  cas_log_write2_nonl ("%18s", glo_cmd_info[glo_cmd].method_name);
  if (glo_cmd_info[glo_cmd].start_pos_index >= 0)
    {
      int start_pos;
      NET_ARG_GET_INT (start_pos,
		       CAS_FN_ARG_ARGV[(int) glo_cmd_info[glo_cmd].
				       start_pos_index]);
      cas_log_write2_nonl (" %d", start_pos);
    }
  cas_log_write2 ("");
#endif /* !LIBCAS_FOR_JSP */

  db_make_null (&ret_val);
  db_make_null (&arg1);
  db_make_null (&arg2);
  args[0] = &arg1;
  args[1] = &arg2;

  start_pos_index = glo_cmd_info[glo_cmd].start_pos_index;
  if (start_pos_index >= 0)
    {
      int start_pos;
      NET_ARG_GET_INT (start_pos, CAS_FN_ARG_ARGV[start_pos_index]);
      if ((err_code = db_make_int (&arg1, start_pos)) < 0)
	{
#if defined(CAS_FOR_DBMS)
	  ERROR_INFO_SET (err_code, DBMS_ERROR_INDICATOR);
	  NET_BUF_ERR_SET (CAS_FN_ARG_NET_BUF);
#else /* CAS_FOR_DBMS */
	  DB_ERR_MSG_SET (CAS_FN_ARG_NET_BUF, err_code);
#endif /* CAS_FOR_DBMS */
	  goto glo_cmd_end;
	}

      err_code = ux_glo_method_call (CAS_FN_ARG_NET_BUF, 1, glo_obj,
				     "data_seek", &ret_val, args);
      if (err_code < 0)
	{
	  goto glo_cmd_end;
	}
      db_value_clear (&ret_val);
      db_value_clear (&arg1);
    }

  if (glo_cmd == GLO_CMD_READ_DATA || glo_cmd == GLO_CMD_WRITE_DATA
      || glo_cmd == GLO_CMD_INSERT_DATA || glo_cmd == GLO_CMD_APPEND_DATA)
    {
      int length = 0;

      if (glo_cmd == GLO_CMD_READ_DATA)
	{
	  NET_ARG_GET_INT (length, CAS_FN_ARG_ARGV[3]);
	  if (length > 0)
	    {
	      data_buf = (char *) malloc (length);
	      if (data_buf == NULL)
		{
#if defined(CAS_FOR_DBMS)
		  ERROR_INFO_SET (CAS_ER_NO_MORE_MEMORY, CAS_ERROR_INDICATOR);
		  NET_BUF_ERR_SET (CAS_FN_ARG_NET_BUF);
#else /* CAS_FOR_DBMS */
		  net_buf_cp_int (CAS_FN_ARG_NET_BUF, CAS_ER_NO_MORE_MEMORY,
				  NULL);
#endif /* CAS_FOR_DBMS */
		  goto glo_cmd_end;
		}
	    }
	}
      else
	{
	  int data_index;
	  data_index = glo_cmd_info[glo_cmd].data_index;
	  NET_ARG_GET_STR (data_buf, length, CAS_FN_ARG_ARGV[data_index]);
	}

      err_code = db_make_int (&arg1, length);
      if (err_code < 0)
	{
#if defined(CAS_FOR_DBMS)
	  ERROR_INFO_SET (err_code, DBMS_ERROR_INDICATOR);
	  NET_BUF_ERR_SET (CAS_FN_ARG_NET_BUF);
#else /* CAS_FOR_DBMS */
	  DB_ERR_MSG_SET (CAS_FN_ARG_NET_BUF, err_code);
#endif /* CAS_FOR_DBMS */
	  goto glo_cmd_end;
	}

      err_code = db_make_varchar (&arg2, DB_MAX_VARCHAR_PRECISION,
				  data_buf, length);
      if (err_code < 0)
	{
#if defined(CAS_FOR_DBMS)
	  ERROR_INFO_SET (err_code, DBMS_ERROR_INDICATOR);
	  NET_BUF_ERR_SET (CAS_FN_ARG_NET_BUF);
#else /* CAS_FOR_DBMS */
	  DB_ERR_MSG_SET (CAS_FN_ARG_NET_BUF, err_code);
#endif /* CAS_FOR_DBMS */
	  goto glo_cmd_end;
	}

      err_code = ux_glo_method_call (CAS_FN_ARG_NET_BUF, 1, glo_obj,
				     glo_cmd_info[glo_cmd].method_name,
				     &ret_val, args);

      if (err_code < 0 && glo_cmd == GLO_CMD_READ_DATA)
	{
	  db_value_clear (&ret_val);
	  db_make_int (&ret_val, -1);
	  if (ux_glo_method_call (NULL, 0, glo_obj, "data_size",
				  &ret_val, args) < 0)
	    {
	      goto glo_cmd_end;
	    }
	  if (db_get_int (&ret_val) == 0)
	    {
	      net_buf_clear (CAS_FN_ARG_NET_BUF);
	      length = 0;
	      err_code = 0;
	    }
	}

      if (err_code < 0)
	{
	  goto glo_cmd_end;
	}

      length = db_get_int (&ret_val);

      net_buf_cp_int (CAS_FN_ARG_NET_BUF, length, NULL);
      if (glo_cmd == GLO_CMD_READ_DATA && data_buf != NULL)
	{
	  net_buf_cp_str (CAS_FN_ARG_NET_BUF, data_buf, length);
	}
    }
  else if (glo_cmd == GLO_CMD_LIKE_SEARCH
	   || glo_cmd == GLO_CMD_REG_SEARCH
	   || glo_cmd == GLO_CMD_BINARY_SEARCH)
    {
      char *search_str;
      int length;
      int offset, cur_pos;

      NET_ARG_GET_STR (search_str, length, CAS_FN_ARG_ARGV[3]);

      if (glo_cmd == GLO_CMD_BINARY_SEARCH)
	{
	  err_code = db_make_varchar (&arg1, DB_MAX_VARCHAR_PRECISION,
				      search_str, length);
	  if (err_code < 0)
	    {
#if defined(CAS_FOR_DBMS)
	      ERROR_INFO_SET (err_code, DBMS_ERROR_INDICATOR);
	      NET_BUF_ERR_SET (CAS_FN_ARG_NET_BUF);
#else /* CAS_FOR_DBMS */
	      DB_ERR_MSG_SET (CAS_FN_ARG_NET_BUF, err_code);
#endif /* CAS_FOR_DBMS */
	      goto glo_cmd_end;
	    }

	  err_code = db_make_int (&arg2, length);
	  if (err_code < 0)
	    {
#if defined(CAS_FOR_DBMS)
	      ERROR_INFO_SET (err_code, DBMS_ERROR_INDICATOR);
	      NET_BUF_ERR_SET (CAS_FN_ARG_NET_BUF);
#else /* CAS_FOR_DBMS */
	      DB_ERR_MSG_SET (CAS_FN_ARG_NET_BUF, err_code);
#endif /* CAS_FOR_DBMS */
	      goto glo_cmd_end;
	    }
	}
      else
	{
	  err_code = db_make_string (&arg1, search_str);
	  if (err_code < 0)
	    {
#if defined(CAS_FOR_DBMS)
	      ERROR_INFO_SET (err_code, DBMS_ERROR_INDICATOR);
	      NET_BUF_ERR_SET (CAS_FN_ARG_NET_BUF);
#else /* CAS_FOR_DBMS */
	      DB_ERR_MSG_SET (CAS_FN_ARG_NET_BUF, err_code);
#endif /* CAS_FOR_DBMS */
	      goto glo_cmd_end;
	    }
	}

      err_code = ux_glo_method_call (CAS_FN_ARG_NET_BUF, 0, glo_obj,
				     glo_cmd_info[glo_cmd].method_name,
				     &ret_val, args);
      if (err_code < 0)
	{
	  goto glo_cmd_end;
	}
      offset = db_get_int (&ret_val);

      db_value_clear (&ret_val);
      err_code = ux_glo_method_call (CAS_FN_ARG_NET_BUF, 1, glo_obj,
				     "data_pos", &ret_val, args);
      if (err_code < 0)
	{
	  goto glo_cmd_end;
	}
      cur_pos = db_get_int (&ret_val);

      net_buf_cp_int (CAS_FN_ARG_NET_BUF, 0, NULL);	/* result code */
      net_buf_cp_int (CAS_FN_ARG_NET_BUF, offset, NULL);
      net_buf_cp_int (CAS_FN_ARG_NET_BUF, cur_pos, NULL);
    }
  else
    {
      /* LO_CMD_DATA_SIZE, GLO_CMD_DELETE_DATA, GLO_CMD_TRUNCATE_DATA,
         GLO_CMD_COMPRESS_DATA, GLO_CMD_DESTROY_DATA */
      int int_val;
      if (glo_cmd == GLO_CMD_DELETE_DATA)
	{
	  NET_ARG_GET_INT (int_val, CAS_FN_ARG_ARGV[3]);	/* length */
	  err_code = db_make_int (&arg1, int_val);
	  if (err_code < 0)
	    {
#if defined(CAS_FOR_DBMS)
	      ERROR_INFO_SET (err_code, DBMS_ERROR_INDICATOR);
	      NET_BUF_ERR_SET (CAS_FN_ARG_NET_BUF);
#else /* CAS_FOR_DBMS */
	      DB_ERR_MSG_SET (CAS_FN_ARG_NET_BUF, err_code);
#endif /* CAS_FOR_DBMS */
	      goto glo_cmd_end;
	    }
	}
      err_code = ux_glo_method_call (CAS_FN_ARG_NET_BUF, 1, glo_obj,
				     glo_cmd_info[glo_cmd].method_name,
				     &ret_val, args);
      if (err_code < 0)
	{
	  goto glo_cmd_end;
	}

      int_val = db_get_int (&ret_val);
      net_buf_cp_int (CAS_FN_ARG_NET_BUF, int_val, NULL);
    }

glo_cmd_end:
  db_value_clear (&ret_val);
  db_value_clear (&arg1);
  db_value_clear (&arg2);
  if (glo_cmd == GLO_CMD_READ_DATA)
    {
      FREE_MEM (data_buf);
    }
  cas_log_write (0, true, "glo_cmd end");
  return 0;
}
#endif /* !CAS_FOR_ORACLE && !CAS_FOR_MYSQL */

int
fn_con_close (SOCKET CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
	      void **CAS_FN_ARG_ARGV, T_NET_BUF * CAS_FN_ARG_NET_BUF,
	      T_REQ_INFO * CAS_FN_ARG_REQ_INFO)
{
  cas_log_write (0, true, "con_close");
  net_buf_cp_int (CAS_FN_ARG_NET_BUF, 0, NULL);
  return -1;
}

int
fn_check_cas (SOCKET CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
	      void **CAS_FN_ARG_ARGV, T_NET_BUF * CAS_FN_ARG_NET_BUF,
	      T_REQ_INFO * CAS_FN_ARG_REQ_INFO)
{
  int retcode = 0;

  if (CAS_FN_ARG_ARGC == 1)
    {
      char *msg;
      int msg_size;
      NET_ARG_GET_STR (msg, msg_size, CAS_FN_ARG_ARGV[0]);
      cas_log_write (0, true, "client_msg:%s", msg);
    }
  else
    {
      retcode = ux_check_connection ();
      cas_log_write (0, true, "check_cas %d", retcode);
    }

  if (retcode)
    {
#if defined(CAS_FOR_DBMS)
      ERROR_INFO_SET (retcode, CAS_ERROR_INDICATOR);
      NET_BUF_ERR_SET (CAS_FN_ARG_NET_BUF);
#else /* CAS_FOR_DBMS */
      net_buf_cp_int (CAS_FN_ARG_NET_BUF, retcode, NULL);
#endif /* CAS_FOR_DBMS */
    }
  return retcode;
}

#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
int
fn_make_out_rs (SOCKET CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
		void **CAS_FN_ARG_ARGV, T_NET_BUF * CAS_FN_ARG_NET_BUF,
		T_REQ_INFO * CAS_FN_ARG_REQ_INFO)
{
  int srv_h_id;

  if (CAS_FN_ARG_ARGC < 1)
    {
#if defined(CAS_FOR_DBMS)
      ERROR_INFO_SET (CAS_ER_ARGS, CAS_ERROR_INDICATOR);
      NET_BUF_ERR_SET (CAS_FN_ARG_NET_BUF);
#else /* CAS_FOR_DBMS */
      net_buf_cp_int (CAS_FN_ARG_NET_BUF, CAS_ER_ARGS, NULL);
#endif /* CAS_FOR_DBMS */
      return 0;
    }

  NET_ARG_GET_INT (srv_h_id, CAS_FN_ARG_ARGV[0]);
  ux_make_out_rs (srv_h_id, CAS_FN_ARG_NET_BUF, CAS_FN_ARG_REQ_INFO);

  return 0;
}

int
fn_get_generated_keys (SOCKET CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
		       void **CAS_FN_ARG_ARGV,
		       T_NET_BUF * CAS_FN_ARG_NET_BUF,
		       T_REQ_INFO * CAS_FN_ARG_REQ_INFO)
{
  int srv_h_id;
  T_SRV_HANDLE *srv_handle;

  if (CAS_FN_ARG_ARGC < 1)
    {
#if defined(CAS_FOR_DBMS)
      ERROR_INFO_SET (CAS_ER_ARGS, CAS_ERROR_INDICATOR);
      NET_BUF_ERR_SET (CAS_FN_ARG_NET_BUF);
#else /* CAS_FOR_DBMS */
      net_buf_cp_int (CAS_FN_ARG_NET_BUF, CAS_ER_ARGS, NULL);
#endif /* CAS_FOR_DBMS */
      return 0;
    }

  NET_ARG_GET_INT (srv_h_id, CAS_FN_ARG_ARGV[0]);
  srv_handle = hm_find_srv_handle (srv_h_id);

  if (srv_handle == NULL)
    {
#if defined(CAS_FOR_DBMS)
      ERROR_INFO_SET (CAS_ER_SRV_HANDLE, CAS_ERROR_INDICATOR);
      NET_BUF_ERR_SET (CAS_FN_ARG_NET_BUF);
#else /* CAS_FOR_DBMS */
      net_buf_cp_int (CAS_FN_ARG_NET_BUF, CAS_ER_SRV_HANDLE, NULL);
#endif /* CAS_FOR_DBMS */
      return 0;
    }

  cas_log_write (SRV_HANDLE_QUERY_SEQ_NUM (srv_handle), false,
		 "get_generated_keys %d", srv_h_id);

  ux_get_generated_keys (srv_handle, CAS_FN_ARG_NET_BUF);

  return 0;
}
#endif /* !CAS_FOR_ORACLE && !CAS_FOR_MYSQL */

static const char *
get_schema_type_str (int schema_type)
{
  if (schema_type < 1 || schema_type > CCI_SCH_LAST)
    return "";

  return (schema_type_str[schema_type - 1]);
}

static const char *
get_tran_type_str (int tran_type)
{
  if (tran_type < 1 || tran_type > 2)
    return "";

  return (tran_type_str[tran_type - 1]);
}

#ifndef LIBCAS_FOR_JSP
static void
bind_value_log (int start, int argc, void **argv, int param_size,
		char *param_mode, unsigned int query_seq_num)
{
  int idx;
  char type;
  int num_bind;
  void *net_value;
  const char *param_mode_str;

  if (shm_appl->sql_log_mode != SQL_LOG_MODE_NONE)
    {
      num_bind = 1;
      idx = start;
      while (idx < argc)
	{
	  NET_ARG_GET_CHAR (type, argv[idx++]);
	  net_value = argv[idx++];

	  param_mode_str = "";
	  if (param_mode != NULL && param_size >= num_bind)
	    {
	      if (param_mode[num_bind - 1] == CCI_PARAM_MODE_IN)
		param_mode_str = "(IN) ";
	      else if (param_mode[num_bind - 1] == CCI_PARAM_MODE_OUT)
		param_mode_str = "(OUT) ";
	      else if (param_mode[num_bind - 1] == CCI_PARAM_MODE_INOUT)
		param_mode_str = "(INOUT) ";
	    }

	  cas_log_write_nonl (query_seq_num, false, "bind %d %s: ",
			      num_bind++, param_mode_str);
	  if (type > CCI_U_TYPE_FIRST && type <= CCI_U_TYPE_LAST)
	    {
	      cas_log_write2_nonl ("%s ", type_str_tbl[(int) type]);
	      bind_value_print (type, net_value);
	    }
	  else
	    {
	      cas_log_write2_nonl ("NULL");
	    }
	  cas_log_write2 ("");
	}
    }
}
#endif /* !LIBCAS_FOR_JSP */

static void
bind_value_print (char type, void *net_value)
{
  int data_size;

  NET_ARG_GET_SIZE (data_size, net_value);
  if (data_size <= 0)
    {
      type = CCI_U_TYPE_NULL;
      data_size = 0;
    }

  switch (type)
    {
    case CCI_U_TYPE_CHAR:
    case CCI_U_TYPE_STRING:
    case CCI_U_TYPE_NCHAR:
    case CCI_U_TYPE_VARNCHAR:
    case CCI_U_TYPE_BIT:
    case CCI_U_TYPE_VARBIT:
    case CCI_U_TYPE_NUMERIC:
      {
	char *str_val;
	int val_size;
	NET_ARG_GET_STR (str_val, val_size, net_value);
	cas_log_write_value_string (str_val, val_size - 1);
      }
      break;
    case CCI_U_TYPE_BIGINT:
      {
	DB_BIGINT bi_val;
	NET_ARG_GET_BIGINT (bi_val, net_value);
	cas_log_write2_nonl ("%lld", (long long) bi_val);
      }
      break;
    case CCI_U_TYPE_INT:
      {
	int i_val;
	NET_ARG_GET_INT (i_val, net_value);
	cas_log_write2_nonl ("%d", i_val);
      }
      break;
    case CCI_U_TYPE_SHORT:
      {
	short s_val;
	NET_ARG_GET_SHORT (s_val, net_value);
	cas_log_write2_nonl ("%d", s_val);
      }
      break;
    case CCI_U_TYPE_MONETARY:
    case CCI_U_TYPE_DOUBLE:
      {
	double d_val;
	NET_ARG_GET_DOUBLE (d_val, net_value);
	cas_log_write2_nonl ("%.15e", d_val);
      }
      break;
    case CCI_U_TYPE_FLOAT:
      {
	float f_val;
	NET_ARG_GET_FLOAT (f_val, net_value);
	cas_log_write2_nonl ("%.6e", f_val);
      }
      break;
    case CCI_U_TYPE_DATE:
    case CCI_U_TYPE_TIME:
    case CCI_U_TYPE_TIMESTAMP:
    case CCI_U_TYPE_DATETIME:
      {
	int yr, mon, day, hh, mm, ss, ms;
	NET_ARG_GET_DATETIME (yr, mon, day, hh, mm, ss, ms, net_value);
	if (type == CCI_U_TYPE_DATE)
	  cas_log_write2_nonl ("%d/%d/%d", yr, mon, day);
	else if (type == CCI_U_TYPE_TIME)
	  cas_log_write2_nonl ("%d:%d:%d", hh, mm, ss);
	else if (type == CCI_U_TYPE_TIMESTAMP)
	  cas_log_write2_nonl ("%d/%d/%d %d:%d:%d", yr, mon, day, hh, mm, ss);
	else
	  cas_log_write2_nonl ("%d/%d/%d %d:%d:%d.%03d",
			       yr, mon, day, hh, mm, ss, ms);
      }
      break;
    case CCI_U_TYPE_SET:
    case CCI_U_TYPE_MULTISET:
    case CCI_U_TYPE_SEQUENCE:
      {
	int remain_size = data_size;
	int ele_size;
	char ele_type;
	char *cur_p = (char *) net_value;
	char print_comma = 0;

	cur_p += 4;
	ele_type = *cur_p;
	cur_p++;
	remain_size--;

	if (ele_type <= CCI_U_TYPE_FIRST || ele_type > CCI_U_TYPE_LAST)
	  break;

	cas_log_write2_nonl ("(%s) {", type_str_tbl[(int) ele_type]);

	while (remain_size > 0)
	  {
	    NET_ARG_GET_SIZE (ele_size, cur_p);
	    if (ele_size + 4 > remain_size)
	      break;
	    if (print_comma)
	      cas_log_write2_nonl (", ");
	    else
	      print_comma = 1;
	    bind_value_print (ele_type, cur_p);
	    ele_size += 4;
	    cur_p += ele_size;
	    remain_size -= ele_size;
	  }

	cas_log_write2_nonl ("}");
      }
      break;
    case CCI_U_TYPE_OBJECT:
      {
	int pageid;
	short slotid, volid;

	NET_ARG_GET_CCI_OBJECT (pageid, slotid, volid, net_value);
	cas_log_write2_nonl ("%d|%d|%d", pageid, slotid, volid);
      }
      break;
    default:
      cas_log_write2_nonl ("NULL");
      break;
    }
}

/*
 * get_error_log_eids - get error identifier string
 *    return: pointer to internal buffer
 * NOTE:
 * this function is not MT safe. Rreturned address is guaranteed to be valid
 * until next get_error_log_eids() call.
 *
 */
static char *
get_error_log_eids (int err)
{
  static char *pending_alloc = NULL;
  static char buffer[512];
  char *buf;

#if defined(CAS_FOR_ORACLE)
  return (char *) "";
#elif defined(CAS_FOR_MYSQL)
  return (char *) "";
#else /* CAS_FOR_MYSQL */
  if (err >= 0)
    {
      return (char *) "";
    }

  if (pending_alloc != NULL)
    {
      free (pending_alloc);
      pending_alloc = NULL;
    }

  buf = cas_log_error_handler_asprint (buffer, sizeof (buffer), true);
  if (buf != buffer)
    {
      pending_alloc = buf;
    }

  return buf;
#endif /* CAS_FOR_MYSQL */
}

#if defined(CAS_FOR_ORACLE) || defined(CAS_FOR_MYSQL)
int
fn_not_supported (SOCKET CAS_FN_ARG_SOCK_FD, int CAS_FN_ARG_ARGC,
		  void **CAS_FN_ARG_ARGV,
		  T_NET_BUF * CAS_FN_ARG_NET_BUF,
		  T_REQ_INFO * CAS_FN_ARG_REQ_INFO)
{
  ERROR_INFO_SET (CAS_ER_NOT_IMPLEMENTED, CAS_ERROR_INDICATOR);
  NET_BUF_ERR_SET (CAS_FN_ARG_NET_BUF);
  return 0;
}
#endif /* CAS_FOR_ORACLE || CAS_FOR_MYSQL */
