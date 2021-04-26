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
 * cci_query_execute.c -
 */

#ident "$Id$"

/************************************************************************
 * IMPORTED SYSTEM HEADER FILES						*
 ************************************************************************/
#include "config.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <assert.h>

#if defined(WINDOWS)
#include <winsock2.h>
#include <windows.h>
#else
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#endif

/************************************************************************
 * OTHER IMPORTED HEADER FILES						*
 ************************************************************************/

#include "cas_protocol.h"
#include "cci_common.h"
#include "cci_query_execute.h"
#include "cci_network.h"
#include "cci_net_buf.h"
#include "cci_handle_mng.h"
#include "cci_util.h"
#include "cci_t_set.h"
#include "cci_t_lob.h"
#include "cci_map.h"

/************************************************************************
 * PRIVATE DEFINITIONS							*
 ************************************************************************/
#define ALLOC_COPY_BIGINT(PTR, VALUE)      \
        do {                            \
          PTR = MALLOC(sizeof(INT64));  \
          if (PTR != NULL) {            \
            *((INT64*) (PTR)) = VALUE;  \
          }                             \
        } while (0)

#define ALLOC_COPY_UBIGINT(PTR, VALUE)      \
        do {                            \
          PTR = MALLOC(sizeof(UINT64)); \
          if (PTR != NULL) {            \
            *((UINT64*) (PTR)) = VALUE; \
          }                             \
        } while (0)

#define ALLOC_COPY_INT(PTR, VALUE)	\
	do {				\
	  PTR = MALLOC(sizeof(int));	\
	  if (PTR != NULL) {		\
	    *((int*) (PTR)) = VALUE;	\
	  }				\
	} while (0)

#define ALLOC_COPY_UINT(PTR, VALUE)	\
	do {				        \
	  PTR = MALLOC(sizeof(unsigned int));	\
	  if (PTR != NULL) {		        \
	    *((unsigned int*) (PTR)) = VALUE;	\
	  }				        \
	} while (0)

#define ALLOC_COPY_FLOAT(PTR, VALUE)	\
	do {				\
	  PTR = MALLOC(sizeof(float));	\
	  if (PTR != NULL) {		\
	    *((float*) (PTR)) = VALUE;	\
	  }				\
	} while (0)

#define ALLOC_COPY_DOUBLE(PTR, VALUE)	\
	do {				\
	  PTR = MALLOC(sizeof(double));	\
	  if (PTR != NULL) {		\
	    *((double*) (PTR)) = VALUE;	\
	  }				\
	} while (0)

#define ALLOC_COPY_DATE(PTR, VALUE)		\
	do {					\
	  PTR = MALLOC(sizeof(T_CCI_DATE));	\
	  if (PTR != NULL) {			\
	    *((T_CCI_DATE*) (PTR)) = VALUE;	\
	  }					\
	} while (0)

#define ALLOC_COPY_DATE_TZ(PTR, VALUE)		\
	do {					\
	  PTR = MALLOC(sizeof(T_CCI_DATE_TZ));	\
	  if (PTR != NULL) {			\
	    *((T_CCI_DATE_TZ*) (PTR)) = VALUE;	\
	  }					\
	} while (0)

#define ALLOC_COPY_OBJECT(PTR, VALUE)		\
	do {					\
	  PTR = MALLOC(sizeof(T_OBJECT));	\
	  if (PTR != NULL) {			\
	    *((T_OBJECT*) (PTR)) = VALUE;	\
	  }					\
	} while (0)

#define ALLOC_COPY_BIT(DEST, SRC, SIZE)		\
	do {					\
	  DEST = MALLOC(SIZE);			\
	  if (DEST != NULL) {			\
	    memcpy(DEST, SRC, SIZE);		\
	  }					\
	} while (0)

#define EXECUTE_ARRAY	0
#define EXECUTE_BATCH	1
#define EXECUTE_EXEC	2

/************************************************************************
 * PRIVATE TYPE DEFINITIONS						*
 ************************************************************************/

typedef enum
{
  FETCH_FETCH,
  FETCH_OID_GET,
  FETCH_COL_GET
} T_FETCH_TYPE;

/************************************************************************
 * PRIVATE FUNCTION PROTOTYPES						*
 ************************************************************************/

static int prepare_info_decode (char *buf, int *size, T_REQ_HANDLE * req_handle);
static int out_rs_info_decode (char *buf, int *size, T_REQ_HANDLE * req_handle);
static int get_cursor_pos (T_REQ_HANDLE * req_handle, int offset, char origin);
static int fetch_info_decode (char *buf, int size, int num_cols, T_TUPLE_VALUE ** tuple_value, T_FETCH_TYPE fetch_type,
			      T_REQ_HANDLE * req_handle, T_CON_HANDLE * con_handle);
static void stream_to_obj (char *buf, T_OBJECT * obj);

static int get_data_set (T_CCI_U_EXT_TYPE u_ext_type, char *col_value_p, T_SET ** value, int data_size);
#if defined (ENABLE_UNUSED_FUNCTION)
static int get_file_size (char *filename);
#endif
static int get_column_info (char *buf_p, int *remain_size, T_CCI_COL_INFO ** ret_col_info, char **next_buf_p,
			    bool is_prepare);
static int oid_get_info_decode (char *buf_p, int remain_size, T_REQ_HANDLE * req_handle, T_CON_HANDLE * con_handle);
static int schema_info_decode (char *buf_p, int size, T_REQ_HANDLE * req_handle);
static int col_get_info_decode (char *buf_p, int remain_size, int *col_size, int *col_type, T_REQ_HANDLE * req_handle,
				T_CON_HANDLE * con_handle);

static int next_result_info_decode (char *buf, int size, T_REQ_HANDLE * req_handle);
static int bind_value_conversion (T_CCI_A_TYPE a_type, T_CCI_U_TYPE u_type, char flag, void *value, int length,
				  T_BIND_VALUE * bind_value);
static int bind_value_to_net_buf (T_NET_BUF * net_buf, T_CCI_U_TYPE u_type, void *value, int size, char *charset,
				  bool set_default_value);
static int execute_array_info_decode (char *buf, int size, char flag, T_CCI_QUERY_RESULT ** qr, int *res_remain_size);
static T_CCI_U_TYPE get_basic_utype (T_CCI_U_EXT_TYPE u_ext_type);
static int parameter_info_decode (char *buf, int size, int num_param, T_CCI_PARAM_INFO ** res_param);
static int decode_fetch_result (T_CON_HANDLE * con_handle, T_REQ_HANDLE * req_handle, char *result_msg_org,
				char *result_msg_start, int result_msg_size);
static int qe_close_req_handle_internal (T_REQ_HANDLE * req_handle, T_CON_HANDLE * con_handle, bool force_close);
static int qe_send_close_handle_msg (T_CON_HANDLE * con_handle, int server_handle_id);
#if defined(WINDOWS)
static int get_windows_charset_code (char *str);
#endif
#ifdef CCI_XA
static void add_arg_xid (T_NET_BUF * net_buf, XID * xid);
static int xa_prepare_info_decode (char *buf, int buf_size, int count, XID * xid, int num_xid_buf);
static void net_str_to_xid (char *buf, XID * xid);
#endif
static int shard_info_decode (char *buf_p, int size, int num_shard, T_CCI_SHARD_INFO ** shard_info);
static bool is_set_default_value_if_null (T_CON_HANDLE * con_handle, T_CCI_CUBRID_STMT stmt_type, char bind_mode);
static T_CCI_U_EXT_TYPE get_ext_utype_from_net_bytes (T_CCI_U_TYPE basic_type, T_CCI_U_TYPE set_type);
static void confirm_schema_type_info (T_REQ_HANDLE * req_handle, int col_no, T_CCI_U_TYPE u_type, char *col_value_p,
				      int data_size);


/************************************************************************
 * INTERFACE VARIABLES							*
 ************************************************************************/

/************************************************************************
 * PUBLIC VARIABLES							*
 ************************************************************************/

/************************************************************************
 * PRIVATE VARIABLES							*
 ************************************************************************/

/************************************************************************
 * IMPLEMENTATION OF INTERFACE FUNCTIONS 				*
 ************************************************************************/

/************************************************************************
 * IMPLEMENTATION OF PUBLIC FUNCTIONS	 				*
 ************************************************************************/

int
qe_con_close (T_CON_HANDLE * con_handle)
{
  T_NET_BUF net_buf;
  char func_code = CAS_FC_CON_CLOSE;

  if (IS_INVALID_SOCKET (con_handle->sock_fd))
    return 0;

  net_buf_init (&net_buf);
  net_buf_cp_str (&net_buf, &func_code, 1);

  if (net_buf.err_code < 0)
    goto con_close_end;

  if (net_send_msg (con_handle, net_buf.data, net_buf.data_size) < 0)
    goto con_close_end;

  net_recv_msg (con_handle, NULL, NULL, NULL);

con_close_end:

  net_buf_clear (&net_buf);
  hm_ssl_free (con_handle);
  CLOSE_SOCKET (con_handle->sock_fd);
  con_handle->sock_fd = INVALID_SOCKET;
  return 0;
}

int
qe_end_tran (T_CON_HANDLE * con_handle, char type, T_CCI_ERROR * err_buf)
{
  T_NET_BUF net_buf;
  char func_code = CAS_FC_END_TRAN;
  int err_code;
  bool keep_connection;
  time_t cur_time, failure_time;
#ifdef END_TRAN2
  char type_str[2];
#endif

  if (IS_INVALID_SOCKET (con_handle->sock_fd))
    {
      con_handle->con_status = CCI_CON_STATUS_OUT_TRAN;
      return CCI_ER_COMMUNICATION;
    }

  net_buf_init (&net_buf);

  net_buf_cp_str (&net_buf, &func_code, 1);

#ifdef END_TRAN2
  type_str[0] = type_str[1] = type;
  ADD_ARG_BYTES (&net_buf, type_str, 2);
#else
  ADD_ARG_BYTES (&net_buf, &type, 1);
#endif

  if (net_buf.err_code < 0)
    {
      err_code = net_buf.err_code;
      net_buf_clear (&net_buf);
      hm_force_close_connection (con_handle);
      return err_code;
    }

  err_code = net_send_msg (con_handle, net_buf.data, net_buf.data_size);
  net_buf_clear (&net_buf);
  if (err_code < 0)
    {
      hm_force_close_connection (con_handle);
      return err_code;
    }

  err_code = net_recv_msg (con_handle, NULL, NULL, err_buf);

  if (con_handle->broker_info[BROKER_INFO_STATEMENT_POOLING] != CAS_STATEMENT_POOLING_ON)
    {
      if (type == CCI_TRAN_ROLLBACK)
	{
	  hm_req_handle_free_all (con_handle);
	}
      else
	{
	  hm_req_handle_free_all_unholdable (con_handle);
	}
    }
  else
    {
      if (type == CCI_TRAN_ROLLBACK)
	{
	  /* close all results sets */
	  hm_req_handle_close_all_resultsets (con_handle);
	}
      else
	{
	  /* close only unholdable results sets */
	  hm_req_handle_close_all_unholdable_resultsets (con_handle);
	}
    }

  keep_connection = true;

  if (con_handle->alter_host_id > 0 && con_handle->rc_time > 0)
    {
      cur_time = time (NULL);
      failure_time = con_handle->last_failure_time;

      if (failure_time > 0 && (cur_time - failure_time) > con_handle->rc_time)
	{
	  if (hm_is_host_reachable (con_handle, 0))
	    {
	      keep_connection = false;
	      con_handle->alter_host_id = -1;
	      con_handle->force_failback = 0;
	      con_handle->last_failure_time = 0;
	    }
	}
    }

  if (keep_connection == false)
    {
      hm_force_close_connection (con_handle);
    }

  con_handle->con_status = CCI_CON_STATUS_OUT_TRAN;
  return err_code;
}

int
qe_end_session (T_CON_HANDLE * con_handle, T_CCI_ERROR * err_buf)
{
  T_NET_BUF net_buf;
  char func_code = CAS_FC_END_SESSION;
  int err_code;
  if (hm_is_empty_session (&con_handle->session_id))
    {
      return 0;
    }
  net_buf_init (&net_buf);

  net_buf_cp_str (&net_buf, &func_code, 1);

  if (net_buf.err_code < 0)
    {
      err_code = net_buf.err_code;
      net_buf_clear (&net_buf);
      return err_code;
    }

  err_code = net_send_msg (con_handle, net_buf.data, net_buf.data_size);
  net_buf_clear (&net_buf);
  if (err_code < 0)
    {
      return err_code;
    }

  err_code = net_recv_msg (con_handle, NULL, NULL, err_buf);

  hm_make_empty_session (&con_handle->session_id);
  return err_code;
}

int
qe_prepare (T_REQ_HANDLE * req_handle, T_CON_HANDLE * con_handle, const char *sql_stmt, char flag,
	    T_CCI_ERROR * err_buf, int reuse)
{
  T_NET_BUF net_buf;
  char func_code = CAS_FC_PREPARE;
  char autocommit_flag;
  int sql_stmt_size;
  int err_code;
  char *result_msg = NULL;
  int result_msg_size;
  int result_code;
  int remaining_time = 0;

  if (!reuse)
    {
      FREE_MEM (req_handle->sql_text);
      ALLOC_COPY (req_handle->sql_text, sql_stmt);
    }

  if (req_handle->sql_text == NULL)
    {
      return CCI_ER_NO_MORE_MEMORY;
    }

  sql_stmt_size = (int) strlen (req_handle->sql_text) + 1;

  net_buf_init (&net_buf);

  net_buf_cp_str (&net_buf, &func_code, 1);

  ADD_ARG_STR (&net_buf, req_handle->sql_text, sql_stmt_size, con_handle->charset);

  if (hm_get_con_handle_holdable (con_handle))
    {
      /* make sure statement is holdable */
      flag |= CCI_PREPARE_HOLDABLE;
    }
  ADD_ARG_BYTES (&net_buf, &flag, 1);

  autocommit_flag = (char) con_handle->autocommit_mode;
  ADD_ARG_BYTES (&net_buf, &autocommit_flag, 1);

  while (con_handle->deferred_close_handle_count > 0)
    {
      ADD_ARG_INT (&net_buf, con_handle->deferred_close_handle_list[--con_handle->deferred_close_handle_count]);
    }

  if (net_buf.err_code < 0)
    {
      err_code = net_buf.err_code;
      net_buf_clear (&net_buf);
      return err_code;
    }

  if (TIMEOUT_IS_SET (con_handle))
    {
      remaining_time = con_handle->current_timeout;
      remaining_time -= get_elapsed_time (&con_handle->start_time);
      if (remaining_time <= 0)
	{
	  net_buf_clear (&net_buf);
	  return CCI_ER_QUERY_TIMEOUT;
	}
    }

  err_code = net_send_msg (con_handle, net_buf.data, net_buf.data_size);
  net_buf_clear (&net_buf);
  if (err_code < 0)
    {
      return err_code;
    }

  result_code = net_recv_msg_timeout (con_handle, &result_msg, &result_msg_size, err_buf, remaining_time);
  if (result_code < 0)
    {
      return result_code;
    }

  result_msg_size -= 4;
  err_code = prepare_info_decode (result_msg + 4, &result_msg_size, req_handle);
  if (err_code < 0)
    {
      FREE_MEM (result_msg);
      return err_code;
    }

  FREE_MEM (result_msg);

  req_handle->handle_type = HANDLE_PREPARE;
  req_handle->handle_sub_type = 0;
  req_handle->server_handle_id = result_code;
  req_handle->cur_fetch_tuple_index = -1;
  if ((flag & CCI_PREPARE_UPDATABLE) != 0)
    {
      flag |= CCI_PREPARE_INCLUDE_OID;
    }
  req_handle->prepare_flag = flag;
  req_handle->cursor_pos = 0;
  req_handle->is_closed = 0;
  req_handle->valid = 1;
  req_handle->is_from_current_transaction = 1;

  if (!reuse)
    {
      if (req_handle->num_bind > 0)
	{
	  req_handle->bind_value = (T_BIND_VALUE *) MALLOC (sizeof (T_BIND_VALUE) * req_handle->num_bind);

	  if (req_handle->bind_value == NULL)
	    {
	      return CCI_ER_NO_MORE_MEMORY;
	    }

	  memset (req_handle->bind_value, 0, sizeof (T_BIND_VALUE) * req_handle->num_bind);

	  req_handle->bind_mode = (char *) MALLOC (req_handle->num_bind);
	  if (req_handle->bind_mode == NULL)
	    {
	      return CCI_ER_NO_MORE_MEMORY;
	    }

	  memset (req_handle->bind_mode, CCI_PARAM_MODE_UNKNOWN, req_handle->num_bind);
	}
    }

  return CCI_ER_NO_ERROR;
}

int
qe_bind_param (T_REQ_HANDLE * req_handle, int index, T_CCI_A_TYPE a_type, void *value, int length, T_CCI_U_TYPE u_type,
	       char flag)
{
  int err_code;

  assert (u_type >= CCI_U_TYPE_FIRST && u_type <= CCI_U_TYPE_LAST);

  index--;

  if (index < 0 || index >= req_handle->num_bind)
    {
      return CCI_ER_BIND_INDEX;
    }

  if (req_handle->bind_value[index].flag == BIND_PTR_DYNAMIC)
    {
      FREE_MEM (req_handle->bind_value[index].value);
      memset (&(req_handle->bind_value[index]), 0, sizeof (T_BIND_VALUE));
    }

  req_handle->bind_mode[index] = CCI_PARAM_MODE_IN;

  if (value == NULL || u_type == CCI_U_TYPE_NULL)
    {
      req_handle->bind_value[index].u_type = CCI_U_TYPE_NULL;
      return 0;
    }

  err_code = bind_value_conversion (a_type, u_type, flag, value, length, &(req_handle->bind_value[index]));

  return err_code;
}

int
qe_execute (T_REQ_HANDLE * req_handle, T_CON_HANDLE * con_handle, char flag, int max_col_size, T_CCI_ERROR * err_buf)
{
  T_NET_BUF net_buf;
  char func_code = CAS_FC_EXECUTE;
  char autocommit_flag;
  int i;
  int err_code = 0;
  int res_count;
  char *result_msg = NULL, *msg;
  int result_msg_size;
  T_CCI_QUERY_RESULT *qr = NULL;
  char fetch_flag;
  char forward_only_cursor;
  char include_column_info;
  int remain_msg_size = 0;
  int remaining_time = 0;
  bool use_server_query_cancel = false;
  int shard_id;
  T_BROKER_VERSION broker_ver;

  req_handle->is_fetch_completed = 0;
  QUERY_RESULT_FREE (req_handle);

  net_buf_init (&net_buf);

  net_buf_cp_str (&net_buf, &func_code, 1);

  ADD_ARG_INT (&net_buf, req_handle->server_handle_id);
  ADD_ARG_BYTES (&net_buf, &flag, 1);
  ADD_ARG_INT (&net_buf, max_col_size);
  ADD_ARG_INT (&net_buf, req_handle->max_row);
  if (req_handle->first_stmt_type == CUBRID_STMT_CALL_SP)
    {
      ADD_ARG_BYTES (&net_buf, req_handle->bind_mode, req_handle->num_bind);
    }
  else
    {
      ADD_ARG_BYTES (&net_buf, NULL, 0);
    }

  if (req_handle->first_stmt_type == CUBRID_STMT_SELECT)
    {
      fetch_flag = 1;
    }
  else
    {
      fetch_flag = 0;
    }
  ADD_ARG_BYTES (&net_buf, &fetch_flag, 1);

  if (con_handle->autocommit_mode == CCI_AUTOCOMMIT_TRUE)
    {
      forward_only_cursor = true;
    }
  else
    {
      forward_only_cursor = false;
    }
  autocommit_flag = (char) con_handle->autocommit_mode;
  ADD_ARG_BYTES (&net_buf, &autocommit_flag, 1);
  ADD_ARG_BYTES (&net_buf, &forward_only_cursor, 1);

  ADD_ARG_CACHE_TIME (&net_buf, 0, 0);

  if (TIMEOUT_IS_SET (con_handle))
    {
      remaining_time = con_handle->current_timeout;
      remaining_time -= get_elapsed_time (&con_handle->start_time);
      if (remaining_time <= 0)
	{
	  err_code = CCI_ER_QUERY_TIMEOUT;
	  goto execute_error;
	}
    }

  broker_ver = hm_get_broker_version (con_handle);
  if (hm_broker_understand_the_protocol (broker_ver, PROTOCOL_V2))
    {
      /* In PROTOCOL_V2, cci driver use server query timeout feature, when disconnect_on_query_timeout is false. */
      if (TIMEOUT_IS_SET (con_handle) && con_handle->disconnect_on_query_timeout == false)
	{
	  use_server_query_cancel = true;
	}
      ADD_ARG_INT (&net_buf, remaining_time);
    }
  else if (hm_broker_understand_the_protocol (broker_ver, PROTOCOL_V1))
    {
      /* cci does not use server query timeout in PROTOCOL_V1 */
      ADD_ARG_INT (&net_buf, 0);
    }

  for (i = 0; i < req_handle->num_bind; i++)
    {
      bind_value_to_net_buf (&net_buf, req_handle->bind_value[i].u_type, req_handle->bind_value[i].value,
			     req_handle->bind_value[i].size, con_handle->charset,
			     is_set_default_value_if_null (con_handle, req_handle->stmt_type,
							   req_handle->bind_mode[i]));
    }

  if (net_buf.err_code < 0)
    {
      err_code = net_buf.err_code;
      goto execute_error;
    }

  err_code = net_send_msg (con_handle, net_buf.data, net_buf.data_size);
  if (err_code < 0)
    {
      goto execute_error;
    }

  net_buf_clear (&net_buf);

  res_count =
    net_recv_msg_timeout (con_handle, &result_msg, &result_msg_size, err_buf,
			  ((use_server_query_cancel) ? 0 : remaining_time));

  if (res_count < 0)
    {
      return res_count;
    }

  err_code = execute_array_info_decode (result_msg + 4, result_msg_size - 4, EXECUTE_EXEC, &qr, &remain_msg_size);
  if (err_code < 0 || remain_msg_size < 0)
    {
      FREE_MEM (result_msg);
      if (err_code == CCI_ER_NO_ERROR)
	{
	  return CCI_ER_COMMUNICATION;
	}

      return err_code;
    }

  if (qr == NULL)
    {
      req_handle->num_query_res = 0;
      req_handle->qr = NULL;
    }
  else
    {
      req_handle->num_query_res = err_code;
      req_handle->qr = qr;
    }

  if (req_handle->first_stmt_type == CUBRID_STMT_SELECT || req_handle->first_stmt_type == CUBRID_STMT_GET_STATS
      || req_handle->first_stmt_type == CUBRID_STMT_CALL || req_handle->first_stmt_type == CUBRID_STMT_EVALUATE)
    {
      req_handle->num_tuple = res_count;
    }
  else if (req_handle->first_stmt_type == CUBRID_STMT_CALL_SP)
    {
      req_handle->num_tuple = res_count;
    }
  else
    {
      req_handle->num_tuple = -1;
    }

  req_handle->execute_flag = flag;

  hm_req_handle_fetch_buf_free (req_handle);
  req_handle->cursor_pos = 0;

  if (hm_broker_understand_the_protocol (broker_ver, PROTOCOL_V2))
    {
      msg = result_msg + (result_msg_size - remain_msg_size);

      NET_STR_TO_BYTE (include_column_info, msg);
      remain_msg_size -= NET_SIZE_BYTE;
      msg += NET_SIZE_BYTE;

      if (include_column_info == 1)
	{
	  err_code = prepare_info_decode (msg, &remain_msg_size, req_handle);
	  if (err_code < 0)
	    {
	      FREE_MEM (result_msg);
	      return err_code;
	    }
	}
    }

  if (hm_broker_understand_the_protocol (broker_ver, PROTOCOL_V5))
    {
      msg = result_msg + (result_msg_size - remain_msg_size);

      NET_STR_TO_INT (shard_id, msg);
      remain_msg_size -= NET_SIZE_INT;
      msg += NET_SIZE_INT;

      con_handle->shard_id = shard_id;
      req_handle->shard_id = shard_id;
    }

  /* If fetch_flag is 1, executing query and fetching data is processed together. So, fetching results are included in
   * result_msg. */
  if (req_handle->first_stmt_type == CUBRID_STMT_SELECT)
    {
      int num_tuple;

      req_handle->cursor_pos = 1;
      num_tuple =
	decode_fetch_result (con_handle, req_handle, result_msg, result_msg + (result_msg_size - remain_msg_size) + 4,
			     remain_msg_size - 4);
      req_handle->cursor_pos = 0;
      if (num_tuple < 0)
	{
	  FREE_MEM (result_msg);
	  return num_tuple;
	}
    }
  else
    {
      FREE_MEM (result_msg);
    }

  req_handle->is_closed = 0;
  req_handle->is_from_current_transaction = 1;

  return res_count;

execute_error:
  net_buf_clear (&net_buf);
  return err_code;
}

int
qe_prepare_and_execute (T_REQ_HANDLE * req_handle, T_CON_HANDLE * con_handle, char *sql_stmt, int max_col_size,
			T_CCI_ERROR * err_buf)
{
  T_NET_BUF net_buf;
  char func_code = CAS_FC_PREPARE_AND_EXECUTE;
  char autocommit_flag;
  int sql_stmt_size;
  int err_code;
  int result_code;
  int execute_res_count;
  char *result_msg = NULL;
  char *msg;
  char *result_msg_org;
  int result_msg_size;
  T_CCI_QUERY_RESULT *qr = NULL;
  char fetch_flag;
  char include_column_info;
  int remain_msg_size = 0;
  int remaining_time = 0;
  int num_tuple = 0;
  char prepare_flag = 0;
  char execute_flag = CCI_EXEC_QUERY_ALL;
  int prepare_argc_count = 3;
  bool use_server_query_cancel = false;
  int shard_id;
  T_BROKER_VERSION broker_ver;

  QUERY_RESULT_FREE (req_handle);

  ALLOC_COPY (req_handle->sql_text, sql_stmt);

  if (req_handle->sql_text == NULL)
    {
      return CCI_ER_NO_MORE_MEMORY;
    }

  sql_stmt_size = (int) strlen (req_handle->sql_text) + 1;

  net_buf_init (&net_buf);

  /* prepare info */
  broker_ver = hm_get_broker_version (con_handle);
  if (hm_broker_match_the_protocol (broker_ver, PROTOCOL_V2))
    {
      func_code = CAS_FC_PREPARE_AND_EXECUTE_FOR_PROTO_V2;
    }
  net_buf_cp_str (&net_buf, &func_code, 1);
  prepare_argc_count += con_handle->deferred_close_handle_count;
  ADD_ARG_INT (&net_buf, prepare_argc_count);
  ADD_ARG_STR (&net_buf, req_handle->sql_text, sql_stmt_size, con_handle->charset);
  ADD_ARG_BYTES (&net_buf, &prepare_flag, 1);
  autocommit_flag = (char) con_handle->autocommit_mode;
  ADD_ARG_BYTES (&net_buf, &autocommit_flag, 1);

  while (con_handle->deferred_close_handle_count > 0)
    {
      ADD_ARG_INT (&net_buf, con_handle->deferred_close_handle_list[--con_handle->deferred_close_handle_count]);
    }

  /* execute info */
  ADD_ARG_BYTES (&net_buf, &execute_flag, 1);
  ADD_ARG_INT (&net_buf, max_col_size);
  ADD_ARG_INT (&net_buf, req_handle->max_row);
  ADD_ARG_BYTES (&net_buf, NULL, 0);

  ADD_ARG_CACHE_TIME (&net_buf, 0, 0);

  if (TIMEOUT_IS_SET (con_handle))
    {
      remaining_time = con_handle->current_timeout;
      remaining_time -= get_elapsed_time (&con_handle->start_time);
      if (remaining_time <= 0)
	{
	  err_code = CCI_ER_QUERY_TIMEOUT;
	  goto prepare_and_execute_error;
	}
    }

  if (hm_broker_understand_the_protocol (broker_ver, PROTOCOL_V2))
    {
      /* In PROTOCOL_V2, cci driver use server query timeout feature, when disconnect_on_query_timeout is false. */
      if (TIMEOUT_IS_SET (con_handle) && con_handle->disconnect_on_query_timeout == false)
	{
	  use_server_query_cancel = true;
	}
      ADD_ARG_INT (&net_buf, remaining_time);
    }
  else if (hm_broker_understand_the_protocol (broker_ver, PROTOCOL_V1))
    {
      /* cci does not use server query timeout in PROTOCOL_V1 */
      ADD_ARG_INT (&net_buf, 0);
    }

  if (net_buf.err_code < 0)
    {
      err_code = net_buf.err_code;
      goto prepare_and_execute_error;
    }

  err_code = net_send_msg (con_handle, net_buf.data, net_buf.data_size);
  if (err_code < 0)
    {
      goto prepare_and_execute_error;
    }

  net_buf_clear (&net_buf);


  /* prepare result */
  result_code =
    net_recv_msg_timeout (con_handle, &result_msg, &result_msg_size, err_buf,
			  ((use_server_query_cancel) ? 0 : remaining_time));

  if (result_code < 0)
    {
      return result_code;
    }

  result_msg_org = result_msg;
  result_msg += 4;
  result_msg_size -= 4;
  remain_msg_size = result_msg_size;

  err_code = prepare_info_decode (result_msg, &result_msg_size, req_handle);

  if (err_code < 0)
    {
      FREE_MEM (result_msg_org);
      return err_code;
    }

  req_handle->handle_type = HANDLE_PREPARE;
  req_handle->handle_sub_type = 0;
  req_handle->server_handle_id = result_code;
  req_handle->cur_fetch_tuple_index = -1;
  if ((prepare_flag & CCI_PREPARE_UPDATABLE) != 0)
    {
      prepare_flag |= CCI_PREPARE_INCLUDE_OID;
    }
  req_handle->prepare_flag = prepare_flag;
  req_handle->cursor_pos = 0;
  req_handle->is_closed = 0;
  req_handle->valid = 1;
  req_handle->is_from_current_transaction = 1;

  if (req_handle->stmt_type == CUBRID_STMT_SELECT)
    {
      fetch_flag = 1;
    }
  else
    {
      fetch_flag = 0;
    }

  /* execute result */
  result_msg += (remain_msg_size - result_msg_size);

  memcpy ((char *) &execute_res_count, result_msg + CAS_PROTOCOL_ERR_INDICATOR_INDEX, CAS_PROTOCOL_ERR_INDICATOR_SIZE);
  execute_res_count = ntohl (execute_res_count);

  result_msg += 4;
  result_msg_size -= 4;

  err_code = execute_array_info_decode (result_msg, result_msg_size, EXECUTE_EXEC, &qr, &remain_msg_size);
  if (err_code < 0 || remain_msg_size < 0)
    {
      FREE_MEM (result_msg_org);
      if (err_code == CCI_ER_NO_ERROR)
	{
	  return CCI_ER_COMMUNICATION;
	}

      return err_code;
    }

  if (qr == NULL)
    {
      req_handle->num_query_res = 0;
      req_handle->qr = NULL;
    }
  else
    {
      req_handle->num_query_res = err_code;
      req_handle->qr = qr;
    }

  if (req_handle->stmt_type == CUBRID_STMT_SELECT || req_handle->stmt_type == CUBRID_STMT_GET_STATS
      || req_handle->stmt_type == CUBRID_STMT_CALL || req_handle->stmt_type == CUBRID_STMT_EVALUATE)
    {
      req_handle->num_tuple = execute_res_count;
    }
  else if (req_handle->stmt_type == CUBRID_STMT_CALL_SP)
    {
      req_handle->num_tuple = execute_res_count;
    }
  else
    {
      req_handle->num_tuple = -1;
    }

  req_handle->execute_flag = execute_flag;

  hm_req_handle_fetch_buf_free (req_handle);
  req_handle->cursor_pos = 0;

  if (hm_broker_understand_the_protocol (broker_ver, PROTOCOL_V2))
    {
      msg = result_msg + (result_msg_size - remain_msg_size);

      NET_STR_TO_BYTE (include_column_info, msg);
      remain_msg_size -= NET_SIZE_BYTE;
      msg += NET_SIZE_BYTE;

      if (include_column_info == 1)
	{
	  err_code = prepare_info_decode (msg, &remain_msg_size, req_handle);
	  if (err_code < 0)
	    {
	      FREE_MEM (result_msg_org);
	      return err_code;
	    }
	}
    }

  if (hm_broker_understand_the_protocol (broker_ver, PROTOCOL_V5))
    {
      msg = result_msg + (result_msg_size - remain_msg_size);

      NET_STR_TO_INT (shard_id, msg);
      remain_msg_size -= NET_SIZE_INT;
      msg += NET_SIZE_INT;

      con_handle->shard_id = shard_id;
      req_handle->shard_id = shard_id;
    }

  /* If fetch_flag is 1, executing query and fetching data is processed together. So, fetching results are included in
   * result_msg. */
  if (fetch_flag)
    {
      req_handle->cursor_pos = 1;
      num_tuple =
	decode_fetch_result (con_handle, req_handle, result_msg_org,
			     result_msg + (result_msg_size - remain_msg_size) + 4, remain_msg_size - 4);
      req_handle->cursor_pos = 0;
      if (num_tuple < 0)
	{
	  FREE_MEM (result_msg_org);
	  return num_tuple;
	}
    }
  else
    {
      FREE_MEM (result_msg_org);
    }

  return execute_res_count;

prepare_and_execute_error:
  net_buf_clear (&net_buf);
  return err_code;
}


void
qe_bind_value_free (T_REQ_HANDLE * req_handle)
{
  int i;

  if (req_handle->bind_value == NULL)
    {
      return;
    }

  for (i = 0; i < req_handle->num_bind; i++)
    {
      if (req_handle->bind_value[i].flag == BIND_PTR_DYNAMIC)
	{
	  FREE_MEM (req_handle->bind_value[i].value);
	  req_handle->bind_value[i].flag = BIND_PTR_STATIC;
	}
    }
}

int
qe_get_db_parameter (T_CON_HANDLE * con_handle, T_CCI_DB_PARAM param_name, void *ret_val, T_CCI_ERROR * err_buf)
{
  T_NET_BUF net_buf;
  char func_code = CAS_FC_GET_DB_PARAMETER;
  char *result_msg = NULL;
  int result_msg_size;
  int err_code = CCI_ER_NO_ERROR;
  int val;
  T_BROKER_VERSION broker_ver;

  if (ret_val == NULL)
    {
      return CCI_ER_NO_ERROR;
    }

  net_buf_init (&net_buf);

  net_buf_cp_str (&net_buf, &func_code, 1);

  ADD_ARG_INT (&net_buf, param_name);

  if (net_buf.err_code < 0)
    {
      err_code = net_buf.err_code;
      net_buf_clear (&net_buf);
      return err_code;
    }

  err_code = net_send_msg (con_handle, net_buf.data, net_buf.data_size);
  net_buf_clear (&net_buf);
  if (err_code < 0)
    {
      return err_code;
    }

  err_code = net_recv_msg (con_handle, &result_msg, &result_msg_size, err_buf);
  result_msg_size -= NET_SIZE_INT;

  if (err_code >= 0)
    {
      if (result_msg_size < NET_SIZE_INT)
	{
	  err_code = CCI_ER_COMMUNICATION;
	}
      else
	{
	  NET_STR_TO_INT (val, result_msg + NET_SIZE_INT);
	  if (param_name == CCI_PARAM_LOCK_TIMEOUT)
	    {
	      broker_ver = hm_get_broker_version (con_handle);
	      if (!hm_broker_understand_the_protocol (broker_ver, PROTOCOL_V2))
		{
		  if (val > 0)
		    {
		      val = val * 1000;	/* second --> millisecond */
		    }
		}
	    }
	  memcpy (ret_val, (char *) &val, sizeof (int));
	}
    }

  FREE_MEM (result_msg);

  return err_code;
}

int
qe_set_db_parameter (T_CON_HANDLE * con_handle, T_CCI_DB_PARAM param_name, void *value, T_CCI_ERROR * err_buf)
{
  T_NET_BUF net_buf;
  char func_code = CAS_FC_SET_DB_PARAMETER;
  int err_code;

  net_buf_init (&net_buf);

  net_buf_cp_str (&net_buf, &func_code, 1);
  ADD_ARG_INT (&net_buf, param_name);

  switch (param_name)
    {
    case CCI_PARAM_ISOLATION_LEVEL:
    case CCI_PARAM_LOCK_TIMEOUT:
      {
	int i_val;
	i_val = *((int *) value);
	ADD_ARG_INT (&net_buf, i_val);
      }
      break;
#if 0
      /* CCI AUTO COMMIT MODE SUPPORT */
    case CCI_PARAM_AUTO_COMMIT:
      {
	int i_val;
	i_val = *((int *) value);
	ADD_ARG_INT (&net_buf, i_val);

	if (i_val == 0)
	  {
	    con_handle->autocommit_mode = CCI_AUTOCOMMIT_FALSE;
	  }
	else
	  {
	    con_handle->autocommit_mode = CCI_AUTOCOMMIT_TRUE;
	  }
      }
      break;
#endif
    default:
      net_buf_clear (&net_buf);
      return CCI_ER_PARAM_NAME;
    }

  if (net_buf.err_code < 0)
    {
      err_code = net_buf.err_code;
      net_buf_clear (&net_buf);
      return err_code;
    }

  err_code = net_send_msg (con_handle, net_buf.data, net_buf.data_size);
  net_buf_clear (&net_buf);
  if (err_code < 0)
    return err_code;

  err_code = net_recv_msg (con_handle, NULL, NULL, err_buf);

  return err_code;
}

int
qe_set_cas_change_mode (T_CON_HANDLE * con_handle, int mode, T_CCI_ERROR * err_buf)
{
  T_NET_BUF net_buf;
  char func_code = CAS_FC_CAS_CHANGE_MODE;
  int err_code;
  char *result_msg, *cur_p;
  int result_msg_size;
  int prev_mode = 0;

  assert (mode == CAS_CHANGE_MODE_AUTO || mode == CAS_CHANGE_MODE_KEEP);

  net_buf_init (&net_buf);

  net_buf_cp_str (&net_buf, &func_code, 1);

  ADD_ARG_INT (&net_buf, mode);

  err_code = net_send_msg (con_handle, net_buf.data, net_buf.data_size);
  net_buf_clear (&net_buf);
  if (err_code < 0)
    {
      return err_code;
    }

  result_msg = NULL;
  result_msg_size = 0;
  err_code = net_recv_msg (con_handle, &result_msg, &result_msg_size, err_buf);
  if (err_code < 0)
    {
      return err_code;
    }
  if (result_msg == NULL)
    {
      return CCI_ER_COMMUNICATION;
    }

  cur_p = result_msg + NET_SIZE_INT;
  result_msg_size -= NET_SIZE_INT;

  if (result_msg_size < NET_SIZE_INT)
    {
      FREE_MEM (result_msg);
      return CCI_ER_COMMUNICATION;
    }
  NET_STR_TO_INT (prev_mode, cur_p);
  FREE_MEM (result_msg);

  return prev_mode;
}

int
qe_close_query_result (T_REQ_HANDLE * req_handle, T_CON_HANDLE * con_handle)
{
  int err_code = 0;
  T_NET_BUF net_buf;
  char func_code = CAS_FC_CURSOR_CLOSE;
  T_BROKER_VERSION broker_ver;

  if (!hm_get_con_handle_holdable (con_handle))
    {
      return err_code;
    }

  if (is_connected_to_cubrid (con_handle) == false)
    {
      return err_code;
    }

  net_buf_init (&net_buf);

  broker_ver = hm_get_broker_version (con_handle);
  if (hm_broker_match_the_protocol (broker_ver, PROTOCOL_V2))
    {
      func_code = CAS_FC_CURSOR_CLOSE_FOR_PROTO_V2;
    }

  net_buf_cp_str (&net_buf, &func_code, 1);
  ADD_ARG_INT (&net_buf, req_handle->server_handle_id);

  if (net_buf.err_code < 0)
    {
      err_code = net_buf.err_code;
      net_buf_clear (&net_buf);
      return err_code;
    }

  err_code = net_send_msg (con_handle, net_buf.data, net_buf.data_size);
  net_buf_clear (&net_buf);
  if (err_code < 0)
    {
      return err_code;
    }

  err_code = net_recv_msg (con_handle, NULL, NULL, NULL);

  return err_code;
}

int
qe_close_req_handle (T_REQ_HANDLE * req_handle, T_CON_HANDLE * con_handle)
{
  return qe_close_req_handle_internal (req_handle, con_handle, false);
}

static int
qe_close_req_handle_internal (T_REQ_HANDLE * req_handle, T_CON_HANDLE * con_handle, bool force_close)
{
  int err_code = 0;
  int *new_deferred_close_handle_list = NULL;
  int new_deferred_max_close_handle_count;

  /* same to qe_close_con. when statement pool is on, (cci_end_tran -> cci_close_req_handle) can be appeared. If
   * connection was closed in cci_end_tran (keep_connection=off), failure(using closed socket) returns while sending a
   * message in this function. So, if sockect is closed at this point, messages must not be sent to server. */
  if (IS_INVALID_SOCKET (con_handle->sock_fd) || !req_handle->valid)
    {
      return 0;
    }

  if (req_handle->stmt_type == CUBRID_STMT_SELECT || req_handle->stmt_type == CUBRID_STMT_GET_STATS
      || req_handle->stmt_type == CUBRID_STMT_CALL || req_handle->stmt_type == CUBRID_STMT_CALL_SP
      || req_handle->stmt_type == CUBRID_STMT_EVALUATE || force_close)
    {
      goto send_close_handle_msg;
    }

  if (con_handle->deferred_close_handle_count == 0
      && con_handle->deferred_max_close_handle_count != DEFERRED_CLOSE_HANDLE_ALLOC_SIZE)
    {
      /* shrink the list size */
      new_deferred_close_handle_list =
	(int *) REALLOC (con_handle->deferred_close_handle_list, sizeof (int) * DEFERRED_CLOSE_HANDLE_ALLOC_SIZE);
      if (new_deferred_close_handle_list == NULL)
	{
	  goto send_close_handle_msg;
	}
      con_handle->deferred_max_close_handle_count = DEFERRED_CLOSE_HANDLE_ALLOC_SIZE;
      con_handle->deferred_close_handle_list = new_deferred_close_handle_list;
    }
  else if (con_handle->deferred_close_handle_count + 1 > con_handle->deferred_max_close_handle_count)
    {
      /* grow the list size */
      new_deferred_max_close_handle_count =
	con_handle->deferred_max_close_handle_count + DEFERRED_CLOSE_HANDLE_ALLOC_SIZE;
      new_deferred_close_handle_list =
	(int *) REALLOC (con_handle->deferred_close_handle_list, sizeof (int) * new_deferred_max_close_handle_count);
      if (new_deferred_close_handle_list == NULL)
	{
	  goto send_close_handle_msg;
	}
      con_handle->deferred_max_close_handle_count = new_deferred_max_close_handle_count;
      con_handle->deferred_close_handle_list = new_deferred_close_handle_list;
    }

  con_handle->deferred_close_handle_list[con_handle->deferred_close_handle_count++] = req_handle->server_handle_id;

  return err_code;

send_close_handle_msg:

  err_code = qe_send_close_handle_msg (con_handle, req_handle->server_handle_id);

  return err_code;
}

void
qe_close_req_handle_all (T_CON_HANDLE * con_handle)
{
  int i;
  T_REQ_HANDLE *req_handle;

  /* close handle in req handle table */
  for (i = 0; i < con_handle->max_req_handle; i++)
    {
      if (con_handle->req_handle_table[i] == NULL)
	{
	  continue;
	}
      req_handle = con_handle->req_handle_table[i];

      map_close_ots (req_handle->mapped_stmt_id);
      req_handle->mapped_stmt_id = -1;
      qe_close_req_handle_internal (req_handle, con_handle, false);
    }
  hm_req_handle_free_all (con_handle);
}

static int
qe_send_close_handle_msg (T_CON_HANDLE * con_handle, int server_handle_id)
{
  int err_code = 0;
  T_NET_BUF net_buf;
  char func_code = CAS_FC_CLOSE_REQ_HANDLE;
  char autocommit_flag;

  net_buf_init (&net_buf);

  net_buf_cp_str (&net_buf, &func_code, 1);
  ADD_ARG_INT (&net_buf, server_handle_id);
  autocommit_flag = (char) con_handle->autocommit_mode;
  ADD_ARG_BYTES (&net_buf, &autocommit_flag, 1);

  if (net_buf.err_code < 0)
    {
      err_code = net_buf.err_code;
      net_buf_clear (&net_buf);
      return err_code;
    }

  err_code = net_send_msg (con_handle, net_buf.data, net_buf.data_size);
  net_buf_clear (&net_buf);
  if (err_code < 0)
    {
      return err_code;
    }

  err_code = net_recv_msg (con_handle, NULL, NULL, NULL);

  return err_code;
}

int
qe_cursor (T_REQ_HANDLE * req_handle, T_CON_HANDLE * con_handle, int offset, char origin, T_CCI_ERROR * err_buf)
{
  char func_code = CAS_FC_CURSOR;
  int err_code;
  char *result_msg = NULL;
  int result_msg_size;
  T_NET_BUF net_buf;
  char *cur_p;
  int tuple_num;
  int cursor_pos;

  if (req_handle->is_closed)
    {
      return CCI_ER_RESULT_SET_CLOSED;
    }

  if (req_handle->handle_type == HANDLE_PREPARE)
    {
      if (req_handle->stmt_type == CUBRID_STMT_SELECT || req_handle->stmt_type == CUBRID_STMT_GET_STATS
	  || req_handle->stmt_type == CUBRID_STMT_CALL || req_handle->stmt_type == CUBRID_STMT_CALL_SP
	  || req_handle->stmt_type == CUBRID_STMT_EVALUATE)
	{
	  if (req_handle->num_tuple >= 0)
	    {
	      cursor_pos = get_cursor_pos (req_handle, offset, origin);
	      if (cursor_pos <= 0)
		{
		  req_handle->cursor_pos = 0;
		  return CCI_ER_NO_MORE_DATA;
		}
	      else if (cursor_pos > req_handle->num_tuple)
		{
		  req_handle->cursor_pos = req_handle->num_tuple + 1;
		  return CCI_ER_NO_MORE_DATA;
		}

	      if (is_connected_to_oracle (con_handle) && cursor_pos > req_handle->fetched_tuple_end
		  && req_handle->is_fetch_completed)
		{
		  return CCI_ER_NO_MORE_DATA;
		}

	      req_handle->cursor_pos = cursor_pos;
	      return 0;
	    }
	  else
	    {			/* async query */
	      if (origin != CCI_CURSOR_LAST)
		{
		  cursor_pos = get_cursor_pos (req_handle, offset, origin);
		  req_handle->cursor_pos = cursor_pos;
		  if (req_handle->fetched_tuple_begin > 0 && cursor_pos >= req_handle->fetched_tuple_begin
		      && cursor_pos <= req_handle->fetched_tuple_end)
		    {
		      return 0;
		    }
		  if (cursor_pos <= 0)
		    {
		      req_handle->cursor_pos = 0;
		      return CCI_ER_NO_MORE_DATA;
		    }
		}
	    }
	}
      else
	{
	  return CCI_ER_NO_MORE_DATA;
	}
    }
  else if (req_handle->handle_type == HANDLE_SCHEMA_INFO || req_handle->handle_type == HANDLE_COL_GET)
    {
      cursor_pos = get_cursor_pos (req_handle, offset, origin);
      if (cursor_pos <= 0 || cursor_pos > req_handle->num_tuple)
	{
	  if (cursor_pos <= 0)
	    req_handle->cursor_pos = 0;
	  else if (cursor_pos > req_handle->num_tuple)
	    req_handle->cursor_pos = req_handle->num_tuple + 1;

	  return CCI_ER_NO_MORE_DATA;
	}
      req_handle->cursor_pos = cursor_pos;
      return 0;
    }
  else if (req_handle->handle_type == HANDLE_OID_GET)
    return 0;

  if (origin == CCI_CURSOR_FIRST || origin == CCI_CURSOR_LAST)
    {
      cursor_pos = offset;
    }
  else if (origin == CCI_CURSOR_CURRENT)
    {
      cursor_pos = req_handle->cursor_pos;
      origin = CCI_CURSOR_FIRST;
    }
  else
    return CCI_ER_INVALID_CURSOR_POS;

  net_buf_init (&net_buf);
  net_buf_cp_str (&net_buf, &func_code, 1);
  ADD_ARG_INT (&net_buf, req_handle->server_handle_id);
  ADD_ARG_INT (&net_buf, cursor_pos);
  ADD_ARG_BYTES (&net_buf, &origin, 1);

  if (net_buf.err_code < 0)
    {
      err_code = net_buf.err_code;
      goto cursor_error;
    }

  err_code = net_send_msg (con_handle, net_buf.data, net_buf.data_size);
  if (err_code < 0)
    {
      goto cursor_error;
    }
  net_buf_clear (&net_buf);

  err_code = net_recv_msg (con_handle, &result_msg, &result_msg_size, err_buf);
  if (err_code < 0)
    {
      return err_code;
    }
  if (result_msg == NULL)
    {
      return CCI_ER_COMMUNICATION;
    }

  cur_p = result_msg + NET_SIZE_INT;
  result_msg_size -= NET_SIZE_INT;

  if (result_msg_size < NET_SIZE_INT)
    {
      FREE_MEM (result_msg);
      return CCI_ER_COMMUNICATION;
    }
  NET_STR_TO_INT (tuple_num, cur_p);
  req_handle->num_tuple = tuple_num;
  FREE_MEM (result_msg);

  if (origin == CCI_CURSOR_FIRST)
    {
      if (req_handle->num_tuple >= 0 && req_handle->cursor_pos > req_handle->num_tuple)
	{
	  req_handle->cursor_pos = req_handle->num_tuple + 1;
	  return CCI_ER_NO_MORE_DATA;
	}
      req_handle->cursor_pos = cursor_pos;
    }
  else
    {
      if (req_handle->num_tuple <= 0 || req_handle->num_tuple - cursor_pos + 1 <= 0)
	{
	  req_handle->cursor_pos = 0;
	  return CCI_ER_NO_MORE_DATA;
	}
      req_handle->cursor_pos = req_handle->num_tuple - cursor_pos + 1;
    }

  return 0;

cursor_error:
  net_buf_clear (&net_buf);
  return err_code;
}

int
qe_fetch (T_REQ_HANDLE * req_handle, T_CON_HANDLE * con_handle, char flag, int result_set_index, T_CCI_ERROR * err_buf)
{
  T_NET_BUF net_buf;
  int err_code;
  char func_code = CAS_FC_FETCH;
  char *result_msg = NULL;
  int result_msg_size;
  int num_tuple;

  if (req_handle->cursor_pos <= 0)
    {
      return CCI_ER_NO_MORE_DATA;
    }

  if (req_handle->is_closed)
    {
      return CCI_ER_RESULT_SET_CLOSED;
    }

  if (req_handle->fetched_tuple_begin > 0 && req_handle->cursor_pos >= req_handle->fetched_tuple_begin
      && req_handle->cursor_pos <= req_handle->fetched_tuple_end)
    {
      req_handle->cur_fetch_tuple_index = req_handle->cursor_pos - req_handle->fetched_tuple_begin;
      if (flag)
	{
	  if (ut_is_deleted_oid (&(req_handle->tuple_value[req_handle->cur_fetch_tuple_index].tuple_oid)))
	    {
	      return CCI_ER_DELETED_TUPLE;
	    }
	}
      return 0;
    }

  hm_req_handle_fetch_buf_free (req_handle);

  net_buf_init (&net_buf);
  net_buf_cp_str (&net_buf, &func_code, 1);
  ADD_ARG_INT (&net_buf, req_handle->server_handle_id);
  ADD_ARG_INT (&net_buf, req_handle->cursor_pos);
  ADD_ARG_INT (&net_buf, req_handle->fetch_size);
  ADD_ARG_BYTES (&net_buf, &flag, 1);
  ADD_ARG_INT (&net_buf, result_set_index);

  if (net_buf.err_code < 0)
    {
      err_code = net_buf.err_code;
      net_buf_clear (&net_buf);
      return err_code;
    }

  err_code = net_send_msg (con_handle, net_buf.data, net_buf.data_size);
  net_buf_clear (&net_buf);
  if (err_code < 0)
    return err_code;

  err_code = net_recv_msg (con_handle, &result_msg, &result_msg_size, err_buf);
  if (err_code < 0)
    {
      return err_code;
    }

  num_tuple = decode_fetch_result (con_handle, req_handle, result_msg, result_msg + 4, result_msg_size - 4);
  if (num_tuple < 0)
    {
      FREE_MEM (result_msg);
      return num_tuple;
    }

  if (num_tuple != 0)
    {
      if (flag)
	{
	  if (ut_is_deleted_oid (&(req_handle->tuple_value[0].tuple_oid)))
	    {
	      return CCI_ER_DELETED_TUPLE;
	    }
	}
    }
  else
    {
      if (is_connected_to_oracle (con_handle) && req_handle->cursor_pos > req_handle->fetched_tuple_end
	  && req_handle->is_fetch_completed)
	{
	  return CCI_ER_NO_MORE_DATA;
	}
    }

  return 0;
}

int
qe_get_data (T_CON_HANDLE * con_handle, T_REQ_HANDLE * req_handle, int col_no, int a_type, void *value, int *indicator)
{
  char *col_value_p;
  T_CCI_U_EXT_TYPE u_ext_type;
  T_CCI_U_TYPE u_type;
  int data_size;
  int err_code;
  int num_cols;

  if (req_handle->is_closed)
    {
      return CCI_ER_RESULT_SET_CLOSED;
    }

  if (req_handle->stmt_type == CUBRID_STMT_CALL_SP)
    {
      num_cols = req_handle->num_bind + 1;
      col_no++;
    }
  else
    {
      num_cols = req_handle->num_col_info;
    }

  if (col_no <= 0 || col_no > num_cols)
    {
      return CCI_ER_COLUMN_INDEX;
    }

  if (req_handle->cur_fetch_tuple_index < 0)
    {
      return CCI_ER_INVALID_CURSOR_POS;
    }

  col_value_p = req_handle->tuple_value[req_handle->cur_fetch_tuple_index].column_ptr[col_no - 1];

  if (req_handle->stmt_type == CUBRID_STMT_CALL_SP)
    {
      u_ext_type = CCI_U_TYPE_NULL;
    }
  else
    {
      u_ext_type = CCI_GET_RESULT_INFO_TYPE (req_handle->col_info, col_no);
    }

  NET_STR_TO_INT (data_size, col_value_p);
  if (data_size <= 0)
    {
      *indicator = -1;
      if (a_type == CCI_A_TYPE_STR || a_type == CCI_A_TYPE_SET || a_type == CCI_A_TYPE_BLOB
	  || a_type == CCI_A_TYPE_CLOB)
	{
	  *((void **) value) = NULL;
	}
      return 0;
    }

  col_value_p += NET_SIZE_INT;
  if (u_ext_type == CCI_U_TYPE_NULL)
    {
      char basic_type, set_type;

      if (data_size <= NET_SIZE_BYTE)
	{
	  *indicator = -1;
	  if (a_type == CCI_A_TYPE_STR || a_type == CCI_A_TYPE_SET || a_type == CCI_A_TYPE_BLOB
	      || a_type == CCI_A_TYPE_CLOB)
	    {
	      *((void **) value) = NULL;
	    }
	  return 0;
	}

      NET_STR_TO_BYTE (basic_type, col_value_p);
      data_size -= NET_SIZE_BYTE;
      col_value_p += NET_SIZE_BYTE;

      if (CCI_NET_TYPE_HAS_2BYTES (basic_type))
	{
	  /* type encoded on 2 bytes */
	  if (data_size <= NET_SIZE_BYTE)
	    {
	      *indicator = -1;
	      if (a_type == CCI_A_TYPE_STR || a_type == CCI_A_TYPE_SET || a_type == CCI_A_TYPE_BLOB
		  || a_type == CCI_A_TYPE_CLOB)
		{
		  *((void **) value) = NULL;
		}
	      return 0;
	    }

	  NET_STR_TO_BYTE (set_type, col_value_p);
	  data_size -= NET_SIZE_BYTE;
	  col_value_p += NET_SIZE_BYTE;

	  u_ext_type = get_ext_utype_from_net_bytes ((T_CCI_U_TYPE) basic_type, (T_CCI_U_TYPE) set_type);
	}
      else
	{
	  /* legacy server */
	  u_ext_type = basic_type;
	}
    }

  u_type = get_basic_utype (u_ext_type);

  *indicator = 0;

  confirm_schema_type_info (req_handle, col_no, u_type, col_value_p, data_size);

  switch (a_type)
    {
    case CCI_A_TYPE_STR:
      err_code = qe_get_data_str (&(req_handle->conv_value_buffer), u_type, col_value_p, data_size, value, indicator);
      break;
    case CCI_A_TYPE_BIGINT:
      err_code = qe_get_data_bigint (u_type, col_value_p, value);
      break;
    case CCI_A_TYPE_UBIGINT:
      err_code = qe_get_data_ubigint (u_type, col_value_p, value);
      break;
    case CCI_A_TYPE_INT:
      err_code = qe_get_data_int (u_type, col_value_p, value);
      break;
    case CCI_A_TYPE_UINT:
      err_code = qe_get_data_uint (u_type, col_value_p, value);
      break;
    case CCI_A_TYPE_FLOAT:
      err_code = qe_get_data_float (u_type, col_value_p, value);
      break;
    case CCI_A_TYPE_DOUBLE:
      err_code = qe_get_data_double (u_type, col_value_p, value);
      break;
    case CCI_A_TYPE_BIT:
      err_code = qe_get_data_bit (u_type, col_value_p, data_size, value);
      break;
    case CCI_A_TYPE_DATE:
      err_code = qe_get_data_date (u_type, col_value_p, value);
      break;
    case CCI_A_TYPE_SET:
      err_code = get_data_set (u_type, col_value_p, (T_SET **) value, data_size);
      break;
    case CCI_A_TYPE_BLOB:
    case CCI_A_TYPE_CLOB:
      err_code = qe_get_data_lob (u_type, col_value_p, data_size, value);
      break;
    case CCI_A_TYPE_REQ_HANDLE:
      err_code = qe_get_data_req_handle (con_handle, req_handle, col_value_p, value);
      break;
    case CCI_A_TYPE_DATE_TZ:
      err_code = qe_get_data_date_tz (u_type, col_value_p, value, data_size);
      break;
    default:
      return CCI_ER_ATYPE;
    }

  return err_code;
}

int
qe_get_cur_oid (T_REQ_HANDLE * req_handle, char *oid_str_buf)
{
  int index;

  if (req_handle->cur_fetch_tuple_index < 0)
    {
      return CCI_ER_INVALID_CURSOR_POS;
    }

  if (req_handle->prepare_flag & CCI_PREPARE_INCLUDE_OID)
    {
      index = req_handle->cur_fetch_tuple_index;
      ut_oid_to_str (&(req_handle->tuple_value[index].tuple_oid), oid_str_buf);
    }
  else
    {
      strcpy (oid_str_buf, "");
    }

  return 0;
}

int
qe_schema_info (T_REQ_HANDLE * req_handle, T_CON_HANDLE * con_handle, int type, char *arg1, char *arg2, char flag,
		int shard_id, T_CCI_ERROR * err_buf)
{
  T_NET_BUF net_buf;
  char func_code = CAS_FC_SCHEMA_INFO;
  int err_code;
  int result_code;
  char *result_msg;
  int result_msg_size;
  T_BROKER_VERSION broker_ver;

  net_buf_init (&net_buf);
  net_buf_cp_str (&net_buf, &func_code, 1);
  ADD_ARG_INT (&net_buf, type);

  if (arg1 == NULL)
    {
      ADD_ARG_BYTES (&net_buf, NULL, 0);
    }
  else
    {
      ADD_ARG_STR (&net_buf, arg1, (int) strlen (arg1) + 1, con_handle->charset);
    }
  if (arg2 == NULL)
    {
      ADD_ARG_BYTES (&net_buf, NULL, 0);
    }
  else
    {
      ADD_ARG_STR (&net_buf, arg2, (int) strlen (arg2) + 1, con_handle->charset);
    }
  ADD_ARG_BYTES (&net_buf, &flag, 1);

  broker_ver = hm_get_broker_version (con_handle);
  if (hm_broker_understand_the_protocol (broker_ver, PROTOCOL_V5))
    {
      ADD_ARG_INT (&net_buf, shard_id);
    }

  if (net_buf.err_code < 0)
    {
      err_code = net_buf.err_code;
      net_buf_clear (&net_buf);
      return err_code;
    }

  err_code = net_send_msg (con_handle, net_buf.data, net_buf.data_size);
  net_buf_clear (&net_buf);
  if (err_code < 0)
    return err_code;

  result_code = net_recv_msg (con_handle, &result_msg, &result_msg_size, err_buf);
  if (result_code < 0)
    return result_code;

  err_code = schema_info_decode (result_msg + 4, result_msg_size - 4, req_handle);
  FREE_MEM (result_msg);
  if (err_code < 0)
    return err_code;

  req_handle->handle_type = HANDLE_SCHEMA_INFO;
  req_handle->handle_sub_type = type;
  req_handle->server_handle_id = result_code;
  req_handle->cur_fetch_tuple_index = -1;
  req_handle->cursor_pos = 0;
  req_handle->valid = 1;

  return 0;
}

int
qe_oid_get (T_REQ_HANDLE * req_handle, T_CON_HANDLE * con_handle, char *oid_str, char **attr_name,
	    T_CCI_ERROR * err_buf)
{
  T_NET_BUF net_buf;
  char func_code = CAS_FC_OID_GET;
  int err_code;
  char *result_msg = NULL;
  int result_msg_size;
  int result_code;
  int i;
  T_OBJECT oid;

  if (ut_str_to_oid (oid_str, &oid) < 0)
    return CCI_ER_OBJECT;

  net_buf_init (&net_buf);
  net_buf_cp_str (&net_buf, &func_code, 1);

  ADD_ARG_OBJECT (&net_buf, &oid);
  if (attr_name)
    {
      for (i = 0; attr_name[i] != NULL; i++)
	{
	  ADD_ARG_STR (&net_buf, attr_name[i], (int) strlen (attr_name[i]) + 1, con_handle->charset);
	}
    }

  if (net_buf.err_code < 0)
    {
      err_code = net_buf.err_code;
      net_buf_clear (&net_buf);
      return err_code;
    }

  err_code = net_send_msg (con_handle, net_buf.data, net_buf.data_size);
  net_buf_clear (&net_buf);
  if (err_code < 0)
    return err_code;

  result_code = net_recv_msg (con_handle, &result_msg, &result_msg_size, err_buf);
  if (result_code < 0)
    return result_code;

  err_code = oid_get_info_decode (result_msg + 4, result_msg_size - 4, req_handle, con_handle);
  if (err_code < 0)
    {
      FREE_MEM (result_msg);
      return err_code;
    }

  req_handle->handle_type = HANDLE_OID_GET;
  req_handle->handle_sub_type = 0;
  req_handle->msg_buf = result_msg;
  return 0;
}

int
qe_oid_put (T_CON_HANDLE * con_handle, char *oid_str, char **attr_name, char **new_val, T_CCI_ERROR * err_buf)
{
  T_NET_BUF net_buf;
  char func_code = CAS_FC_OID_PUT;
  int err_code;
  int i;
  T_OBJECT oid;
  char u_type = CCI_U_TYPE_STRING;

  if (ut_str_to_oid (oid_str, &oid) < 0)
    return CCI_ER_OBJECT;

  net_buf_init (&net_buf);
  net_buf_cp_str (&net_buf, &func_code, 1);

  ADD_ARG_OBJECT (&net_buf, &oid);
  for (i = 0; attr_name[i] != NULL; i++)
    {
      ADD_ARG_STR (&net_buf, attr_name[i], (int) strlen (attr_name[i]) + 1, con_handle->charset);
      ADD_ARG_BYTES (&net_buf, &u_type, 1);
      if (new_val[i] == NULL)
	{
	  ADD_ARG_BYTES (&net_buf, NULL, 0);
	}
      else
	{
	  ADD_ARG_STR (&net_buf, new_val[i], (int) strlen (new_val[i]) + 1, con_handle->charset);
	}
    }

  if (net_buf.err_code < 0)
    {
      err_code = net_buf.err_code;
      net_buf_clear (&net_buf);
      return err_code;
    }

  err_code = net_send_msg (con_handle, net_buf.data, net_buf.data_size);
  net_buf_clear (&net_buf);
  if (err_code < 0)
    return err_code;

  err_code = net_recv_msg (con_handle, NULL, NULL, err_buf);

  return err_code;
}

int
qe_oid_put2 (T_CON_HANDLE * con_handle, char *oid_str, char **attr_name, void **new_val, int *a_type,
	     T_CCI_ERROR * err_buf)
{
  T_NET_BUF net_buf;
  char func_code = CAS_FC_OID_PUT;
  int err_code;
  int i, data_size = 0;
  T_OBJECT oid;
  T_CCI_U_TYPE u_type;
  void *value;
  T_BIND_VALUE tmp_cell;

  if (ut_str_to_oid (oid_str, &oid) < 0)
    {
      return CCI_ER_OBJECT;
    }

  net_buf_init (&net_buf);
  net_buf_cp_str (&net_buf, &func_code, 1);

  ADD_ARG_OBJECT (&net_buf, &oid);
  for (i = 0; attr_name[i] != NULL; i++)
    {
      tmp_cell.flag = BIND_PTR_STATIC;
      tmp_cell.value = NULL;

      ADD_ARG_STR (&net_buf, attr_name[i], (int) strlen (attr_name[i]) + 1, con_handle->charset);

      value = NULL;

      if (new_val[i] == NULL)
	{
	  u_type = CCI_U_TYPE_NULL;
	}
      else
	{
	  switch (a_type[i])
	    {
	    case CCI_A_TYPE_STR:
	      data_size = (int) strlen ((char *) (new_val[i])) + 1;
	      u_type = CCI_U_TYPE_STRING;
	      value = new_val[i];
	      break;
	    case CCI_A_TYPE_BIGINT:
	      data_size = NET_SIZE_BIGINT;
	      u_type = CCI_U_TYPE_BIGINT;
	      value = new_val[i];
	      break;
	    case CCI_A_TYPE_INT:
	      data_size = NET_SIZE_INT;
	      u_type = CCI_U_TYPE_INT;
	      value = new_val[i];
	      break;
	    case CCI_A_TYPE_FLOAT:
	      data_size = NET_SIZE_FLOAT;
	      u_type = CCI_U_TYPE_FLOAT;
	      value = new_val[i];
	      break;
	    case CCI_A_TYPE_DOUBLE:
	      data_size = NET_SIZE_DOUBLE;
	      u_type = CCI_U_TYPE_DOUBLE;
	      value = new_val[i];
	      break;
	    case CCI_A_TYPE_BIT:
	      data_size = ((T_CCI_BIT *) (new_val[i]))->size;
	      u_type = CCI_U_TYPE_BIT;
	      value = ((T_CCI_BIT *) (new_val[i]))->buf;
	      break;
	    case CCI_A_TYPE_DATE:
	      data_size = NET_SIZE_DATETIME;
	      u_type = CCI_U_TYPE_DATETIME;
	      value = new_val[i];
	      break;
	    case CCI_A_TYPE_DATE_TZ:
	      u_type = CCI_U_TYPE_DATETIMETZ;
	      value = new_val[i];
	      data_size = NET_SIZE_DATETIME + NET_SIZE_TZ (value);
	      break;
	    case CCI_A_TYPE_SET:
	      err_code =
		bind_value_conversion (CCI_A_TYPE_SET, CCI_U_TYPE_SEQUENCE, CCI_BIND_PTR, new_val[i], UNMEASURED_LENGTH,
				       &tmp_cell);
	      if (err_code < 0)
		{
		  net_buf_clear (&net_buf);
		  return err_code;
		}
	      data_size = tmp_cell.size;
	      value = tmp_cell.value;
	      u_type = CCI_U_TYPE_SEQUENCE;
	      break;

	    case CCI_A_TYPE_UINT:
	    case CCI_A_TYPE_UBIGINT:
	    default:
	      net_buf_clear (&net_buf);
	      return CCI_ER_ATYPE;
	    }
	}

      bind_value_to_net_buf (&net_buf, u_type, value, data_size, con_handle->charset, false);

      if (tmp_cell.flag == BIND_PTR_DYNAMIC)
	{
	  FREE_MEM (tmp_cell.value);
	}
    }

  if (net_buf.err_code < 0)
    {
      err_code = net_buf.err_code;
      net_buf_clear (&net_buf);
      return err_code;
    }

  err_code = net_send_msg (con_handle, net_buf.data, net_buf.data_size);
  net_buf_clear (&net_buf);
  if (err_code < 0)
    return err_code;

  err_code = net_recv_msg (con_handle, NULL, NULL, err_buf);

  return err_code;
}

int
qe_get_db_version (T_CON_HANDLE * con_handle, char *out_buf, int buf_size)
{
  T_NET_BUF net_buf;
  char func_code = CAS_FC_GET_DB_VERSION;
  char autocommit_flag;
  char *result_msg = NULL;
  int result_msg_size;
  int err_code, remaining_time = 0;

  net_buf_init (&net_buf);
  net_buf_cp_str (&net_buf, &func_code, 1);
  autocommit_flag = (char) con_handle->autocommit_mode;
  ADD_ARG_BYTES (&net_buf, &autocommit_flag, 1);

  if (net_buf.err_code < 0)
    {
      err_code = net_buf.err_code;
      net_buf_clear (&net_buf);
      return err_code;
    }

  if (TIMEOUT_IS_SET (con_handle))
    {
      remaining_time = con_handle->current_timeout;
      remaining_time -= get_elapsed_time (&con_handle->start_time);
      if (remaining_time <= 0)
	{
	  net_buf_clear (&net_buf);
	  return CCI_ER_QUERY_TIMEOUT;
	}
    }

  err_code = net_send_msg (con_handle, net_buf.data, net_buf.data_size);
  net_buf_clear (&net_buf);
  if (err_code < 0)
    {
      return err_code;
    }


  err_code = net_recv_msg_timeout (con_handle, &result_msg, &result_msg_size, NULL, remaining_time);

  result_msg_size -= 4;

  if (result_msg_size <= 0)
    {
      err_code = CCI_ER_COMMUNICATION;
    }
  else if (err_code >= 0)
    {
      if (out_buf)
	{
	  buf_size = MIN (buf_size - 1, result_msg_size);
	  strncpy (out_buf, result_msg + 4, buf_size);
	  out_buf[buf_size] = '\0';
	}
    }

  FREE_MEM (result_msg);

  return err_code;
}

int
qe_get_class_num_objs (T_CON_HANDLE * con_handle, char *class_name, char flag, int *num_objs, int *num_pages,
		       T_CCI_ERROR * err_buf)
{
  T_NET_BUF net_buf;
  char func_code = CAS_FC_GET_CLASS_NUM_OBJS;
  char *result_msg = NULL;
  int result_msg_size;
  int err_code = 0;

  net_buf_init (&net_buf);
  net_buf_cp_str (&net_buf, &func_code, 1);
  ADD_ARG_STR (&net_buf, class_name, (int) strlen (class_name) + 1, con_handle->charset);
  ADD_ARG_BYTES (&net_buf, &flag, 1);

  if (net_buf.err_code < 0)
    {
      err_code = net_buf.err_code;
      net_buf_clear (&net_buf);
      return err_code;
    }

  err_code = net_send_msg (con_handle, net_buf.data, net_buf.data_size);
  net_buf_clear (&net_buf);
  if (err_code < 0)
    return err_code;

  err_code = net_recv_msg (con_handle, &result_msg, &result_msg_size, err_buf);
  if (err_code >= 0)
    {
      int tmp_i_value;

      result_msg_size -= 4;
      if (result_msg_size < 4)
	err_code = CCI_ER_COMMUNICATION;
      else
	{
	  NET_STR_TO_INT (tmp_i_value, result_msg + 4);
	  if (num_objs)
	    {
	      *num_objs = tmp_i_value;
	    }
	  result_msg_size -= 4;
	}

      if (result_msg_size < 4)
	err_code = CCI_ER_COMMUNICATION;
      else
	{
	  NET_STR_TO_INT (tmp_i_value, result_msg + 8);
	  if (num_pages)
	    {
	      *num_pages = tmp_i_value;
	    }
	}
    }

  FREE_MEM (result_msg);

  return err_code;
}

int
qe_oid_cmd (T_CON_HANDLE * con_handle, char cmd, char *oid_str, char *out_buf, int out_buf_size, T_CCI_ERROR * err_buf)
{
  T_NET_BUF net_buf;
  T_OBJECT oid;
  int err_code;
  char func_code = CAS_FC_OID_CMD;
  char *result_msg = NULL;
  int result_msg_size;

  if (cmd < CCI_OID_CMD_FIRST || cmd > CCI_OID_CMD_LAST)
    return CCI_ER_OID_CMD;
  if (ut_str_to_oid (oid_str, &oid) < 0)
    return CCI_ER_OBJECT;

  net_buf_init (&net_buf);

  net_buf_cp_str (&net_buf, &func_code, 1);

  ADD_ARG_BYTES (&net_buf, &cmd, 1);
  ADD_ARG_OBJECT (&net_buf, &oid);

  if (net_buf.err_code < 0)
    {
      err_code = net_buf.err_code;
      net_buf_clear (&net_buf);
      return err_code;
    }

  err_code = net_send_msg (con_handle, net_buf.data, net_buf.data_size);
  net_buf_clear (&net_buf);
  if (err_code < 0)
    return err_code;

  err_code = net_recv_msg (con_handle, &result_msg, &result_msg_size, err_buf);
  if (err_code < 0)
    return err_code;

  if (cmd == CCI_OID_CLASS_NAME)
    {
      if (out_buf)
	{
	  out_buf_size = MIN (out_buf_size - 1, result_msg_size - 4);
	  strncpy (out_buf, result_msg + 4, out_buf_size);
	  out_buf[out_buf_size] = '\0';
	}
    }

  FREE_MEM (result_msg);

  return err_code;
}

int
qe_col_get (T_REQ_HANDLE * req_handle, T_CON_HANDLE * con_handle, char *oid_str, const char *col_attr, int *col_size,
	    int *col_type, T_CCI_ERROR * err_buf)
{
  T_NET_BUF net_buf;
  char func_code = CAS_FC_COLLECTION;
  char col_cmd = CCI_COL_GET;
  int err_code;
  T_OBJECT oid;
  char *result_msg = NULL;
  int result_msg_size;

  if (ut_str_to_oid (oid_str, &oid) < 0)
    {
      return CCI_ER_OBJECT;
    }

  if (col_attr == NULL)
    {
      col_attr = "";
    }

  net_buf_init (&net_buf);

  net_buf_cp_str (&net_buf, &func_code, 1);

  ADD_ARG_BYTES (&net_buf, &col_cmd, 1);
  ADD_ARG_OBJECT (&net_buf, &oid);
  ADD_ARG_STR (&net_buf, col_attr, (int) strlen (col_attr) + 1, con_handle->charset);

  if (net_buf.err_code < 0)
    {
      err_code = net_buf.err_code;
      net_buf_clear (&net_buf);
      return err_code;
    }

  err_code = net_send_msg (con_handle, net_buf.data, net_buf.data_size);
  net_buf_clear (&net_buf);
  if (err_code < 0)
    return err_code;

  err_code = net_recv_msg (con_handle, &result_msg, &result_msg_size, err_buf);
  if (err_code < 0)
    return err_code;

  err_code = col_get_info_decode (result_msg + 4, result_msg_size - 4, col_size, col_type, req_handle, con_handle);
  if (err_code < 0)
    {
      FREE_MEM (result_msg);
      return err_code;
    }

  req_handle->handle_type = HANDLE_COL_GET;
  req_handle->handle_sub_type = 0;
  req_handle->msg_buf = result_msg;
  req_handle->cursor_pos = 0;

  return 0;
}

int
qe_get_row_count (T_REQ_HANDLE * req_handle, T_CON_HANDLE * con_handle, int *row_count, T_CCI_ERROR * err_buf)
{
  T_NET_BUF net_buf;
  char func_code = CAS_FC_GET_ROW_COUNT;
  int err_code;
  char *result_msg = NULL;
  int result_msg_size;
  int tmp;
  net_buf_init (&net_buf);

  net_buf_cp_str (&net_buf, &func_code, 1);

  if (net_buf.err_code < 0)
    {
      err_code = net_buf.err_code;
      net_buf_clear (&net_buf);
      return err_code;
    }

  err_code = net_send_msg (con_handle, net_buf.data, net_buf.data_size);
  net_buf_clear (&net_buf);
  if (err_code < 0)
    return err_code;

  err_code = net_recv_msg (con_handle, &result_msg, &result_msg_size, err_buf);
  if (err_code < 0)
    return err_code;

  if (result_msg_size < 4)
    {
      FREE_MEM (result_msg);
      return CCI_ER_COMMUNICATION;
    }

  NET_STR_TO_INT (tmp, result_msg + 4);

  if (row_count)
    {
      *row_count = tmp;
    }

  return 0;
}

int
qe_get_last_insert_id (T_REQ_HANDLE * req_handle, T_CON_HANDLE * con_handle, void *value, T_CCI_ERROR * err_buf)
{
  T_NET_BUF net_buf;
  char func_code = CAS_FC_GET_LAST_INSERT_ID;
  int err_code;
  char *result_msg = NULL;
  int result_msg_size;
  int valsize = 0;
  unsigned char type = 0;
  char *ptr = NULL;
  net_buf_init (&net_buf);

  net_buf_cp_str (&net_buf, &func_code, 1);

  /* initialize */
  assert (value != NULL);
  *(char **) value = NULL;

  if (net_buf.err_code < 0)
    {
      err_code = net_buf.err_code;
      net_buf_clear (&net_buf);
      return err_code;
    }

  err_code = net_send_msg (con_handle, net_buf.data, net_buf.data_size);
  net_buf_clear (&net_buf);

  if (err_code < 0)
    {
      return err_code;
    }

  err_code = net_recv_msg (con_handle, &result_msg, &result_msg_size, err_buf);

  if (err_code < 0)
    {
      return err_code;
    }

  if (result_msg_size < 4)
    {
      FREE_MEM (result_msg);
      return CCI_ER_COMMUNICATION;
    }

  NET_STR_TO_INT (err_code, result_msg);
  ptr = result_msg + NET_SIZE_INT;

  /* start decoding numeric value */
  NET_STR_TO_INT (valsize, ptr);
  if (valsize == -1)
    {
      /*
       * CCI_ER_NO_ERROR with NULL value
       * means DB NULL
       */
      FREE_MEM (result_msg);
      return CCI_ER_NO_ERROR;
    }

  ptr = ptr + NET_SIZE_INT;
  valsize--;			/* skip type byte */

  /* decode type */
  type = *ptr;

  if (CCI_NET_TYPE_HAS_2BYTES (type))
    {
      unsigned char set_type;
      T_CCI_U_EXT_TYPE u_ext_type;

      /* type encoded on 2 bytes */
      if (valsize <= NET_SIZE_BYTE)
	{
	  FREE_MEM (result_msg);
	  return CCI_ER_NO_ERROR;
	}

      ptr++;
      valsize--;

      NET_STR_TO_BYTE (set_type, ptr);
      u_ext_type = get_ext_utype_from_net_bytes ((T_CCI_U_TYPE) type, (T_CCI_U_TYPE) set_type);
      type = CCI_GET_COLLECTION_DOMAIN (u_ext_type);
    }
  else
    {
      /* legacy server, type byte already read */
    }

  if (type != CCI_U_TYPE_NUMERIC)
    {
      FREE_MEM (result_msg);
      return CCI_ER_COMMUNICATION;
    }

  ptr = ptr + NET_SIZE_BYTE;

  /* copy to con_handle->last_insert_id */
  if (con_handle->last_insert_id == NULL)
    {
      /* 2 for sign & null termination */
      con_handle->last_insert_id = (char *) MALLOC (MAX_NUMERIC_PRECISION + 2);
      if (con_handle->last_insert_id == NULL)
	{
	  err_code = CCI_ER_NO_MORE_MEMORY;
	  FREE_MEM (result_msg);
	  return err_code;
	}
    }

  /* valsize include null terminate byte */
  assert (valsize < MAX_NUMERIC_PRECISION + 2);

  strncpy (con_handle->last_insert_id, ptr, valsize - 1);
  con_handle->last_insert_id[valsize - 1] = '\0';

  *((char **) value) = con_handle->last_insert_id;

  FREE_MEM (result_msg);
  return err_code;
}


int
qe_col_size (T_CON_HANDLE * con_handle, char *oid_str, const char *col_attr, int *set_size, T_CCI_ERROR * err_buf)
{
  T_NET_BUF net_buf;
  char func_code = CAS_FC_COLLECTION;
  char col_cmd = CCI_COL_SIZE;
  int err_code;
  T_OBJECT oid;
  char *result_msg = NULL;
  int result_msg_size;

  if (ut_str_to_oid (oid_str, &oid) < 0)
    {
      return CCI_ER_OBJECT;
    }

  if (col_attr == NULL)
    {
      col_attr = "";
    }

  net_buf_init (&net_buf);

  net_buf_cp_str (&net_buf, &func_code, 1);

  ADD_ARG_BYTES (&net_buf, &col_cmd, 1);
  ADD_ARG_OBJECT (&net_buf, &oid);
  ADD_ARG_STR (&net_buf, (char *) col_attr, (int) strlen (col_attr) + 1, con_handle->charset);

  if (net_buf.err_code < 0)
    {
      err_code = net_buf.err_code;
      net_buf_clear (&net_buf);
      return err_code;
    }

  err_code = net_send_msg (con_handle, net_buf.data, net_buf.data_size);
  net_buf_clear (&net_buf);
  if (err_code < 0)
    return err_code;

  err_code = net_recv_msg (con_handle, &result_msg, &result_msg_size, err_buf);
  if (err_code < 0)
    return err_code;

  if (result_msg_size < 4)
    {
      FREE_MEM (result_msg);
      return CCI_ER_COMMUNICATION;
    }

  if (set_size)
    {
      NET_STR_TO_INT (*set_size, result_msg + 4);
    }
  FREE_MEM (result_msg);

  return 0;
}

int
qe_col_set_add_drop (T_CON_HANDLE * con_handle, char col_cmd, char *oid_str, const char *col_attr, char *value,
		     T_CCI_ERROR * err_buf)
{
  T_NET_BUF net_buf;
  char func_code = CAS_FC_COLLECTION;
  int err_code;
  T_OBJECT oid;
  char u_type = CCI_U_TYPE_STRING;

  if (ut_str_to_oid (oid_str, &oid) < 0)
    {
      return CCI_ER_OBJECT;
    }

  if (col_attr == NULL)
    {
      col_attr = "";
    }

  net_buf_init (&net_buf);

  net_buf_cp_str (&net_buf, &func_code, 1);

  ADD_ARG_BYTES (&net_buf, &col_cmd, 1);
  ADD_ARG_OBJECT (&net_buf, &oid);
  ADD_ARG_STR (&net_buf, (char *) col_attr, (int) strlen (col_attr) + 1, con_handle->charset);
  ADD_ARG_BYTES (&net_buf, &u_type, 1);
  if (value == NULL)
    {
      ADD_ARG_BYTES (&net_buf, NULL, 0);
    }
  else
    {
      ADD_ARG_STR (&net_buf, value, (int) strlen (value) + 1, con_handle->charset);
    }

  if (net_buf.err_code < 0)
    {
      err_code = net_buf.err_code;
      net_buf_clear (&net_buf);
      return err_code;
    }

  err_code = net_send_msg (con_handle, net_buf.data, net_buf.data_size);
  net_buf_clear (&net_buf);
  if (err_code < 0)
    return err_code;

  err_code = net_recv_msg (con_handle, NULL, NULL, err_buf);
  return err_code;
}

int
qe_col_seq_op (T_CON_HANDLE * con_handle, char col_cmd, char *oid_str, const char *col_attr, int index,
	       const char *value, T_CCI_ERROR * err_buf)
{
  T_NET_BUF net_buf;
  char func_code = CAS_FC_COLLECTION;
  int err_code;
  T_OBJECT oid;
  char u_type = CCI_U_TYPE_STRING;

  if (ut_str_to_oid (oid_str, &oid) < 0)
    {
      return CCI_ER_OBJECT;
    }

  if (col_attr == NULL)
    {
      col_attr = "";
    }

  net_buf_init (&net_buf);

  net_buf_cp_str (&net_buf, &func_code, 1);

  ADD_ARG_BYTES (&net_buf, &col_cmd, 1);
  ADD_ARG_OBJECT (&net_buf, &oid);
  ADD_ARG_INT (&net_buf, index);
  ADD_ARG_STR (&net_buf, (char *) col_attr, (int) strlen (col_attr) + 1, con_handle->charset);
  if (col_cmd == CCI_COL_SEQ_INSERT || col_cmd == CCI_COL_SEQ_PUT)
    {
      ADD_ARG_BYTES (&net_buf, &u_type, 1);
      if (value == NULL)
	{
	  ADD_ARG_BYTES (&net_buf, NULL, 0);
	}
      else
	{
	  ADD_ARG_STR (&net_buf, (char *) value, (int) strlen (value) + 1, con_handle->charset);
	}
    }

  if (net_buf.err_code < 0)
    {
      err_code = net_buf.err_code;
      net_buf_clear (&net_buf);
      return err_code;
    }

  err_code = net_send_msg (con_handle, net_buf.data, net_buf.data_size);
  net_buf_clear (&net_buf);
  if (err_code < 0)
    return err_code;

  err_code = net_recv_msg (con_handle, NULL, NULL, err_buf);
  return err_code;
}

int
qe_next_result (T_REQ_HANDLE * req_handle, char flag, T_CON_HANDLE * con_handle, T_CCI_ERROR * err_buf)
{
  T_NET_BUF net_buf;
  char func_code = CAS_FC_NEXT_RESULT;
  int err_code;
  char *result_msg = NULL;
  int result_msg_size;

  net_buf_init (&net_buf);

  net_buf_cp_str (&net_buf, &func_code, 1);

  ADD_ARG_INT (&net_buf, req_handle->server_handle_id);
  ADD_ARG_BYTES (&net_buf, &flag, 1);

  if (net_buf.err_code < 0)
    {
      err_code = net_buf.err_code;
      net_buf_clear (&net_buf);
      return err_code;
    }

  err_code = net_send_msg (con_handle, net_buf.data, net_buf.data_size);
  net_buf_clear (&net_buf);
  if (err_code < 0)
    {
      return err_code;
    }

  err_code = net_recv_msg (con_handle, &result_msg, &result_msg_size, err_buf);
  if (err_code < 0)
    {
      return err_code;
    }

  hm_req_handle_fetch_buf_free (req_handle);

  err_code = next_result_info_decode (result_msg + 4, result_msg_size - 4, req_handle);

  FREE_MEM (result_msg);

  req_handle->cursor_pos = 0;

  return err_code;
}

int
qe_execute_array (T_REQ_HANDLE * req_handle, T_CON_HANDLE * con_handle, T_CCI_QUERY_RESULT ** qr, T_CCI_ERROR * err_buf)
{
  T_NET_BUF net_buf;
  char func_code = CAS_FC_EXECUTE_ARRAY;
  char autocommit_flag;
  int err_code = 0;
  T_BIND_VALUE cur_cell;
  int row, idx;
  char *result_msg = NULL;
  char *msg;
  int result_msg_size;
  int remain_size;
  int remaining_time = 0;
  int shard_id;
  T_BROKER_VERSION broker_ver;

  net_buf_init (&net_buf);

  net_buf_cp_str (&net_buf, &func_code, 1);

  ADD_ARG_INT (&net_buf, req_handle->server_handle_id);

  broker_ver = hm_get_broker_version (con_handle);
  if (hm_broker_understand_the_protocol (broker_ver, PROTOCOL_V4))
    {
      if (TIMEOUT_IS_SET (con_handle))
	{
	  remaining_time = con_handle->current_timeout;
	  remaining_time -= get_elapsed_time (&con_handle->start_time);
	  if (remaining_time <= 0)
	    {
	      net_buf_clear (&net_buf);
	      return CCI_ER_QUERY_TIMEOUT;
	    }
	}
      ADD_ARG_INT (&net_buf, remaining_time);
    }

  autocommit_flag = (char) con_handle->autocommit_mode;
  ADD_ARG_BYTES (&net_buf, &autocommit_flag, 1);

  for (row = 0; row < req_handle->bind_array_size; row++)
    {
      for (idx = 0; idx < req_handle->num_bind; idx++)
	{
	  cur_cell.flag = BIND_PTR_STATIC;
	  cur_cell.value = NULL;
	  cur_cell.size = 0;

	  if (req_handle->bind_value[idx].value == NULL)
	    {
	      cur_cell.u_type = CCI_U_TYPE_NULL;
	    }
	  else if (req_handle->bind_value[idx].null_ind[row])
	    {
	      cur_cell.u_type = CCI_U_TYPE_NULL;
	    }
	  else
	    {
	      char a_type;
	      T_CCI_U_TYPE u_type;

	      a_type = req_handle->bind_value[idx].size;
	      u_type = req_handle->bind_value[idx].u_type;
	      err_code = 0;

	      switch (a_type)
		{
		case CCI_A_TYPE_STR:
		  {
		    char **value;
		    value = (char **) req_handle->bind_value[idx].value;
		    err_code =
		      bind_value_conversion ((T_CCI_A_TYPE) a_type, u_type, CCI_BIND_PTR, value[row], UNMEASURED_LENGTH,
					     &cur_cell);
		  }
		  break;
		case CCI_A_TYPE_BIGINT:
		  {
		    INT64 *value;
		    value = (INT64 *) req_handle->bind_value[idx].value;
		    err_code =
		      bind_value_conversion ((T_CCI_A_TYPE) a_type, u_type, CCI_BIND_PTR, &(value[row]),
					     UNMEASURED_LENGTH, &cur_cell);
		  }
		  break;
		case CCI_A_TYPE_UBIGINT:
		  {
		    UINT64 *value;
		    value = (UINT64 *) req_handle->bind_value[idx].value;
		    err_code =
		      bind_value_conversion ((T_CCI_A_TYPE) a_type, u_type, CCI_BIND_PTR, &(value[row]),
					     UNMEASURED_LENGTH, &cur_cell);
		  }
		  break;
		case CCI_A_TYPE_INT:
		  {
		    int *value;
		    value = (int *) req_handle->bind_value[idx].value;
		    err_code =
		      bind_value_conversion ((T_CCI_A_TYPE) a_type, u_type, CCI_BIND_PTR, &(value[row]),
					     UNMEASURED_LENGTH, &cur_cell);
		  }
		  break;
		case CCI_A_TYPE_UINT:
		  {
		    unsigned int *value;
		    value = (unsigned int *) req_handle->bind_value[idx].value;
		    err_code =
		      bind_value_conversion ((T_CCI_A_TYPE) a_type, u_type, CCI_BIND_PTR, &(value[row]),
					     UNMEASURED_LENGTH, &cur_cell);
		  }
		  break;
		case CCI_A_TYPE_FLOAT:
		  {
		    float *value;
		    value = (float *) req_handle->bind_value[idx].value;
		    err_code =
		      bind_value_conversion ((T_CCI_A_TYPE) a_type, u_type, CCI_BIND_PTR, &(value[row]),
					     UNMEASURED_LENGTH, &cur_cell);
		  }
		  break;
		case CCI_A_TYPE_DOUBLE:
		  {
		    double *value;
		    value = (double *) req_handle->bind_value[idx].value;
		    err_code =
		      bind_value_conversion ((T_CCI_A_TYPE) a_type, u_type, CCI_BIND_PTR, &(value[row]),
					     UNMEASURED_LENGTH, &cur_cell);
		  }
		  break;
		case CCI_A_TYPE_BIT:
		  {
		    T_CCI_BIT *value;
		    value = (T_CCI_BIT *) req_handle->bind_value[idx].value;
		    err_code =
		      bind_value_conversion ((T_CCI_A_TYPE) a_type, u_type, CCI_BIND_PTR, &(value[row]),
					     UNMEASURED_LENGTH, &cur_cell);
		  }
		  break;
		case CCI_A_TYPE_DATE:
		  {
		    T_CCI_DATE *value;
		    value = (T_CCI_DATE *) req_handle->bind_value[idx].value;
		    err_code =
		      bind_value_conversion ((T_CCI_A_TYPE) a_type, u_type, CCI_BIND_PTR, &(value[row]),
					     UNMEASURED_LENGTH, &cur_cell);
		  }
		  break;
		case CCI_A_TYPE_DATE_TZ:
		  {
		    T_CCI_DATE_TZ *value;
		    value = (T_CCI_DATE_TZ *) req_handle->bind_value[idx].value;
		    err_code =
		      bind_value_conversion ((T_CCI_A_TYPE) a_type, u_type, CCI_BIND_PTR, &(value[row]),
					     UNMEASURED_LENGTH, &cur_cell);
		  }
		  break;
		case CCI_A_TYPE_SET:
		  {
		    T_SET **value;
		    value = (T_SET **) req_handle->bind_value[idx].value;
		    err_code =
		      bind_value_conversion ((T_CCI_A_TYPE) a_type, u_type, CCI_BIND_PTR, value[row], UNMEASURED_LENGTH,
					     &cur_cell);
		  }
		  break;
		case CCI_A_TYPE_BLOB:
		case CCI_A_TYPE_CLOB:
		  {
		    T_LOB **value;
		    value = (T_LOB **) req_handle->bind_value[idx].value;
		    err_code =
		      bind_value_conversion ((T_CCI_A_TYPE) a_type, u_type, CCI_BIND_PTR, value[row], UNMEASURED_LENGTH,
					     &cur_cell);
		  }
		  break;
		default:
		  err_code = CCI_ER_ATYPE;
		}		/* end of switch */
	      if (err_code < 0)
		{
		  net_buf_clear (&net_buf);
		  return err_code;
		}
	    }
	  bind_value_to_net_buf (&net_buf, cur_cell.u_type, cur_cell.value, cur_cell.size, con_handle->charset, false);
	  if (cur_cell.flag == BIND_PTR_DYNAMIC)
	    {
	      FREE_MEM (cur_cell.value);
	    }
	}			/* end of for (idx) */
    }				/* end of for (row) */

  if (net_buf.err_code < 0)
    {
      err_code = net_buf.err_code;
      net_buf_clear (&net_buf);
      return err_code;
    }

  if (TIMEOUT_IS_SET (con_handle))
    {
      remaining_time = con_handle->current_timeout;
      remaining_time -= get_elapsed_time (&con_handle->start_time);
      if (remaining_time <= 0)
	{
	  net_buf_clear (&net_buf);
	  return CCI_ER_QUERY_TIMEOUT;
	}
    }

  err_code = net_send_msg (con_handle, net_buf.data, net_buf.data_size);
  net_buf_clear (&net_buf);
  if (err_code < 0)
    {
      return err_code;
    }

  err_code = net_recv_msg_timeout (con_handle, &result_msg, &result_msg_size, err_buf, 0);
  if (err_code < 0)
    {
      return err_code;
    }

  err_code = execute_array_info_decode (result_msg + 4, result_msg_size - 4, EXECUTE_ARRAY, qr, &remain_size);

  if (err_code >= 0 && hm_broker_understand_the_protocol (broker_ver, PROTOCOL_V5))
    {
      msg = result_msg + (result_msg_size - remain_size);

      NET_STR_TO_INT (shard_id, msg);
      remain_size -= NET_SIZE_INT;
      msg += NET_SIZE_INT;

      con_handle->shard_id = shard_id;
      req_handle->shard_id = shard_id;
    }

  FREE_MEM (result_msg);
  return err_code;
}

void
qe_query_result_free (int num_q, T_CCI_QUERY_RESULT * qr)
{
  int i;

  if (qr)
    {
      for (i = 0; i < num_q; i++)
	FREE_MEM (qr[i].err_msg);
      FREE_MEM (qr);
    }
}

int
qe_query_result_copy (T_REQ_HANDLE * req_handle, T_CCI_QUERY_RESULT ** res_qr)
{
  T_CCI_QUERY_RESULT *qr = NULL;
  int num_query = req_handle->num_query_res;
  int i;

  *res_qr = NULL;

  if (req_handle->qr == NULL || num_query == 0)
    {
      return 0;
    }

  qr = (T_CCI_QUERY_RESULT *) MALLOC (sizeof (T_CCI_QUERY_RESULT) * num_query);
  if (qr == NULL)
    {
      return CCI_ER_NO_MORE_MEMORY;
    }

  for (i = 0; i < num_query; i++)
    {
      qr[i].result_count = req_handle->qr[i].result_count;
      qr[i].stmt_type = req_handle->qr[i].stmt_type;
      qr[i].err_no = req_handle->qr[i].err_no;
      ALLOC_COPY (qr[i].err_msg, req_handle->qr[i].err_msg);
      strcpy (qr[i].oid, req_handle->qr[i].oid);
    }

  *res_qr = qr;

  return num_query;
}

int
qe_cursor_update (T_REQ_HANDLE * req_handle, T_CON_HANDLE * con_handle, int cursor_pos, int index, T_CCI_A_TYPE a_type,
		  void *value, T_CCI_ERROR * err_buf)
{
  T_NET_BUF net_buf;
  char func_code = CAS_FC_CURSOR_UPDATE;
  int err_code;
  T_BIND_VALUE bind_value;
  char u_type;

  net_buf_init (&net_buf);

  net_buf_cp_str (&net_buf, &func_code, 1);

  ADD_ARG_INT (&net_buf, req_handle->server_handle_id);
  ADD_ARG_INT (&net_buf, cursor_pos);
  ADD_ARG_INT (&net_buf, index);

  if (value == NULL)
    {
      bind_value.flag = BIND_PTR_STATIC;
      bind_value.value = NULL;
      bind_value.size = 0;
      bind_value.u_type = CCI_U_TYPE_NULL;
    }
  else
    {
      if (req_handle->col_info == NULL)
	{
	  net_buf_clear (&net_buf);
	  return CCI_ER_TYPE_CONVERSION;
	}

      u_type = get_basic_utype (req_handle->col_info[index - 1].ext_type);
      if (u_type <= CCI_U_TYPE_FIRST || u_type > CCI_U_TYPE_LAST)
	{
	  net_buf_clear (&net_buf);
	  return CCI_ER_TYPE_CONVERSION;
	}

      bind_value.flag = BIND_PTR_STATIC;
      bind_value.value = NULL;

      err_code =
	bind_value_conversion (a_type, (T_CCI_U_TYPE) u_type, CCI_BIND_PTR, value, UNMEASURED_LENGTH, &bind_value);
      if (err_code < 0)
	{
	  net_buf_clear (&net_buf);
	  return err_code;
	}
    }

  bind_value_to_net_buf (&net_buf, bind_value.u_type, bind_value.value, bind_value.size, con_handle->charset, false);
  if (bind_value.flag == BIND_PTR_DYNAMIC)
    {
      FREE_MEM (bind_value.value);
    }

  if (net_buf.err_code < 0)
    {
      err_code = net_buf.err_code;
      net_buf_clear (&net_buf);
      return err_code;
    }

  err_code = net_send_msg (con_handle, net_buf.data, net_buf.data_size);
  net_buf_clear (&net_buf);
  if (err_code < 0)
    {
      return err_code;
    }

  err_code = net_recv_msg (con_handle, NULL, NULL, err_buf);

  return err_code;
}

int
qe_execute_batch (T_CON_HANDLE * con_handle, int num_query, char **sql_stmt, T_CCI_QUERY_RESULT ** qr,
		  T_CCI_ERROR * err_buf)
{
  T_NET_BUF net_buf;
  char func_code = CAS_FC_EXECUTE_BATCH;
  char autocommit_flag;
  int err_code;
  char *result_msg = NULL;
  char *msg;
  int result_msg_size;
  int sql_len;
  int i;
  int remain_size;
  int remaining_time = 0;
  int shard_id;
  T_BROKER_VERSION broker_ver;

  net_buf_init (&net_buf);

  net_buf_cp_str (&net_buf, &func_code, 1);

  /* set AutoCommitMode is FALSE */
  autocommit_flag = (char) con_handle->autocommit_mode;
  ADD_ARG_BYTES (&net_buf, &autocommit_flag, 1);

  broker_ver = hm_get_broker_version (con_handle);
  if (hm_broker_understand_the_protocol (broker_ver, PROTOCOL_V4))
    {
      if (TIMEOUT_IS_SET (con_handle))
	{
	  remaining_time = con_handle->current_timeout;
	  remaining_time -= get_elapsed_time (&con_handle->start_time);
	  if (remaining_time <= 0)
	    {
	      net_buf_clear (&net_buf);
	      return CCI_ER_QUERY_TIMEOUT;
	    }
	}
      ADD_ARG_INT (&net_buf, remaining_time);
    }

  for (i = 0; i < num_query; i++)
    {
      sql_len = (int) strlen (sql_stmt[i]) + 1;
      ADD_ARG_STR (&net_buf, sql_stmt[i], sql_len, con_handle->charset);
    }

  if (net_buf.err_code < 0)
    {
      err_code = net_buf.err_code;
      net_buf_clear (&net_buf);
      return err_code;
    }

  if (TIMEOUT_IS_SET (con_handle))
    {
      remaining_time = con_handle->current_timeout;
      remaining_time -= get_elapsed_time (&con_handle->start_time);
      if (remaining_time <= 0)
	{
	  net_buf_clear (&net_buf);
	  return CCI_ER_QUERY_TIMEOUT;
	}
    }

  err_code = net_send_msg (con_handle, net_buf.data, net_buf.data_size);
  net_buf_clear (&net_buf);
  if (err_code < 0)
    {
      return err_code;
    }

  err_code = net_recv_msg_timeout (con_handle, &result_msg, &result_msg_size, err_buf, 0);
  if (err_code < 0)
    {
      return err_code;
    }

  err_code = execute_array_info_decode (result_msg + 4, result_msg_size - 4, EXECUTE_BATCH, qr, &remain_size);

  if (err_code >= 0 && hm_broker_understand_the_protocol (broker_ver, PROTOCOL_V5))
    {
      msg = result_msg + (result_msg_size - remain_size);

      NET_STR_TO_INT (shard_id, msg);
      remain_size -= NET_SIZE_INT;
      msg += NET_SIZE_INT;

      con_handle->shard_id = shard_id;
    }

  FREE_MEM (result_msg);

  return err_code;
}

int
qe_get_data_str (T_VALUE_BUF * conv_val_buf, T_CCI_U_TYPE u_type, char *col_value_p, int col_val_size, void *value,
		 int *indicator)
{
  assert (u_type >= CCI_U_TYPE_FIRST && u_type <= CCI_U_TYPE_LAST);

  switch (u_type)
    {
    case CCI_U_TYPE_CHAR:
    case CCI_U_TYPE_STRING:
    case CCI_U_TYPE_NCHAR:
    case CCI_U_TYPE_VARNCHAR:
    case CCI_U_TYPE_NUMERIC:
    case CCI_U_TYPE_ENUM:
    case CCI_U_TYPE_JSON:
      {
	*((char **) value) = col_value_p;
	*indicator = col_val_size - 1;
      }
      return 0;
    case CCI_U_TYPE_BIT:
    case CCI_U_TYPE_VARBIT:
      {
	if (hm_conv_value_buf_alloc (conv_val_buf, col_val_size * 2 + 2) < 0)
	  {
	    return CCI_ER_NO_MORE_MEMORY;
	  }
	ut_bit_to_str (col_value_p, col_val_size, (char *) conv_val_buf->data, col_val_size * 2 + 2);
      }
      break;
    case CCI_U_TYPE_BIGINT:
      {
	INT64 data = 0LL;

	qe_get_data_bigint (u_type, col_value_p, &data);

	if (hm_conv_value_buf_alloc (conv_val_buf, 128) < 0)
	  {
	    return CCI_ER_NO_MORE_MEMORY;
	  }
	ut_int_to_str (data, (char *) conv_val_buf->data, 128);
      }
      break;
    case CCI_U_TYPE_UBIGINT:
      {
	UINT64 data = 0ULL;

	qe_get_data_ubigint (u_type, col_value_p, &data);

	if (hm_conv_value_buf_alloc (conv_val_buf, 128) < 0)
	  {
	    return CCI_ER_NO_MORE_MEMORY;
	  }
	ut_uint_to_str (data, (char *) conv_val_buf->data, 128);
      }
      break;
    case CCI_U_TYPE_INT:
    case CCI_U_TYPE_SHORT:
      {
	int data = 0;

	qe_get_data_int (u_type, col_value_p, &data);

	if (hm_conv_value_buf_alloc (conv_val_buf, 128) < 0)
	  {
	    return CCI_ER_NO_MORE_MEMORY;
	  }
	ut_int_to_str (data, (char *) conv_val_buf->data, 128);
      }
      break;
    case CCI_U_TYPE_UINT:
    case CCI_U_TYPE_USHORT:
      {
	unsigned int data = 0u;

	qe_get_data_uint (u_type, col_value_p, &data);

	if (hm_conv_value_buf_alloc (conv_val_buf, 128) < 0)
	  {
	    return CCI_ER_NO_MORE_MEMORY;
	  }
	ut_uint_to_str (data, (char *) conv_val_buf->data, 128);
      }
      break;
    case CCI_U_TYPE_MONETARY:
    case CCI_U_TYPE_DOUBLE:
      {
	double data = 0.0;

	qe_get_data_double (u_type, col_value_p, &data);

	if (hm_conv_value_buf_alloc (conv_val_buf, 512) < 0)
	  {
	    return CCI_ER_NO_MORE_MEMORY;
	  }
	ut_double_to_str (data, (char *) conv_val_buf->data, 512);
      }
      break;
    case CCI_U_TYPE_FLOAT:
      {
	float data = .0f;

	qe_get_data_float (u_type, col_value_p, &data);

	if (hm_conv_value_buf_alloc (conv_val_buf, 128) < 0)
	  {
	    return CCI_ER_NO_MORE_MEMORY;
	  }
	ut_float_to_str (data, (char *) conv_val_buf->data, 128);
      }
      break;
    case CCI_U_TYPE_DATE:
    case CCI_U_TYPE_TIME:
    case CCI_U_TYPE_TIMESTAMP:
    case CCI_U_TYPE_DATETIME:
      {
	T_CCI_DATE data = { 0, 0, 0, 0, 0, 0, 0 };

	qe_get_data_date (u_type, col_value_p, &data);

	if (hm_conv_value_buf_alloc (conv_val_buf, 128) < 0)
	  {
	    return CCI_ER_NO_MORE_MEMORY;
	  }
	ut_date_to_str (&data, u_type, (char *) conv_val_buf->data, 128);
      }
      break;
    case CCI_U_TYPE_TIMESTAMPTZ:
    case CCI_U_TYPE_TIMESTAMPLTZ:
    case CCI_U_TYPE_DATETIMETZ:
    case CCI_U_TYPE_DATETIMELTZ:
      {
	T_CCI_DATE_TZ data_tz = { 0, 0, 0, 0, 0, 0, 0, "" };

	qe_get_data_date_tz (u_type, col_value_p, &data_tz, col_val_size);

	if (hm_conv_value_buf_alloc (conv_val_buf, 128) < 0)
	  {
	    return CCI_ER_NO_MORE_MEMORY;
	  }
	ut_date_tz_to_str (&data_tz, u_type, (char *) conv_val_buf->data, 128);
      }
      break;
    case CCI_U_TYPE_SET:
    case CCI_U_TYPE_MULTISET:
    case CCI_U_TYPE_SEQUENCE:
      {
	int err_code;
	T_SET *set;

	err_code = get_data_set (u_type, col_value_p, &set, col_val_size);
	if (err_code < 0)
	  {
	    return err_code;
	  }

	err_code = t_set_to_str (set, conv_val_buf);
	t_set_free (set);
	if (err_code < 0)
	  {
	    return err_code;
	  }
      }
      break;
    case CCI_U_TYPE_OBJECT:
      {
	T_OBJECT data;
	if (hm_conv_value_buf_alloc (conv_val_buf, 128) < 0)
	  {
	    return CCI_ER_NO_MORE_MEMORY;
	  }
	NET_STR_TO_OBJECT (data, col_value_p);
	ut_oid_to_str (&data, (char *) conv_val_buf->data);
      }
      break;
    case CCI_U_TYPE_BLOB:
    case CCI_U_TYPE_CLOB:
      {
	int err_code;
	T_LOB *lob = NULL;

	err_code = qe_get_data_lob (u_type, col_value_p, col_val_size, (void *) &lob);
	if (err_code < 0)
	  {
	    return err_code;
	  }
	if (hm_conv_value_buf_alloc (conv_val_buf, col_val_size) < 0)
	  {
	    FREE_MEM (lob);
	    return CCI_ER_NO_MORE_MEMORY;
	  }
	ut_lob_to_str (lob, (char *) conv_val_buf->data, col_val_size);
	FREE_MEM (lob);
      }
      break;
    default:
      return CCI_ER_TYPE_CONVERSION;
    }

  *((char **) value) = (char *) conv_val_buf->data;
  *indicator = (int) strlen ((char *) conv_val_buf->data);

  return 0;
}

int
qe_get_data_bigint (T_CCI_U_TYPE u_type, char *col_value_p, void *value)
{
  INT64 data = 0LL;

  assert (u_type >= CCI_U_TYPE_FIRST && u_type <= CCI_U_TYPE_LAST);

  switch (u_type)
    {
    case CCI_U_TYPE_CHAR:
    case CCI_U_TYPE_STRING:
    case CCI_U_TYPE_NCHAR:
    case CCI_U_TYPE_VARNCHAR:
    case CCI_U_TYPE_NUMERIC:
    case CCI_U_TYPE_ENUM:
      if (ut_str_to_bigint (col_value_p, &data) < 0)
	return CCI_ER_TYPE_CONVERSION;
      break;
    case CCI_U_TYPE_BIGINT:
    case CCI_U_TYPE_UBIGINT:
      NET_STR_TO_BIGINT (data, col_value_p);
      break;
    case CCI_U_TYPE_INT:
      {
	int i_val;
	NET_STR_TO_INT (i_val, col_value_p);
	data = (INT64) i_val;
	break;
      }
    case CCI_U_TYPE_UINT:
      {
	unsigned int ui_val;
	NET_STR_TO_UINT (ui_val, col_value_p);
	data = (INT64) ui_val;
	break;
      }
      break;
    case CCI_U_TYPE_SHORT:
      {
	short s_val;
	NET_STR_TO_SHORT (s_val, col_value_p);
	data = (INT64) s_val;
      }
      break;
    case CCI_U_TYPE_USHORT:
      {
	unsigned short us_val;
	NET_STR_TO_USHORT (us_val, col_value_p);
	data = (INT64) us_val;
      }
      break;
    case CCI_U_TYPE_MONETARY:
    case CCI_U_TYPE_DOUBLE:
      {
	double d_val;
	NET_STR_TO_DOUBLE (d_val, col_value_p);
	data = (INT64) d_val;
      }
      break;
    case CCI_U_TYPE_FLOAT:
      {
	float f_val;
	NET_STR_TO_FLOAT (f_val, col_value_p);
	data = (INT64) f_val;
      }
      break;
    default:
      return CCI_ER_TYPE_CONVERSION;
    }

  *((INT64 *) value) = data;

  return 0;
}

int
qe_get_data_ubigint (T_CCI_U_TYPE u_type, char *col_value_p, void *value)
{
  UINT64 data = 0ULL;

  assert (u_type >= CCI_U_TYPE_FIRST && u_type <= CCI_U_TYPE_LAST);

  switch (u_type)
    {
    case CCI_U_TYPE_CHAR:
    case CCI_U_TYPE_STRING:
    case CCI_U_TYPE_NCHAR:
    case CCI_U_TYPE_VARNCHAR:
    case CCI_U_TYPE_NUMERIC:
    case CCI_U_TYPE_ENUM:
      if (ut_str_to_ubigint (col_value_p, &data) < 0)
	return CCI_ER_TYPE_CONVERSION;
      break;
    case CCI_U_TYPE_BIGINT:
    case CCI_U_TYPE_UBIGINT:
      NET_STR_TO_UBIGINT (data, col_value_p);
      break;
    case CCI_U_TYPE_INT:
      {
	int i_val;
	NET_STR_TO_INT (i_val, col_value_p);
	data = (UINT64) i_val;
	break;
      }
    case CCI_U_TYPE_UINT:
      {
	unsigned int ui_val;
	NET_STR_TO_UINT (ui_val, col_value_p);
	data = (UINT64) ui_val;
	break;
      }
      break;
    case CCI_U_TYPE_SHORT:
      {
	short s_val;
	NET_STR_TO_SHORT (s_val, col_value_p);
	data = (UINT64) s_val;
      }
      break;
    case CCI_U_TYPE_USHORT:
      {
	unsigned short us_val;
	NET_STR_TO_USHORT (us_val, col_value_p);
	data = (UINT64) us_val;
      }
      break;
    case CCI_U_TYPE_MONETARY:
    case CCI_U_TYPE_DOUBLE:
      {
	double d_val;
	NET_STR_TO_DOUBLE (d_val, col_value_p);
	data = (UINT64) d_val;
      }
      break;
    case CCI_U_TYPE_FLOAT:
      {
	float f_val;
	NET_STR_TO_FLOAT (f_val, col_value_p);
	data = (UINT64) f_val;
      }
      break;
    default:
      return CCI_ER_TYPE_CONVERSION;
    }

  *((UINT64 *) value) = data;

  return 0;
}

int
qe_get_data_int (T_CCI_U_TYPE u_type, char *col_value_p, void *value)
{
  int data = 0;
  int *temp_value = (int *) value;

  assert (u_type >= CCI_U_TYPE_FIRST && u_type <= CCI_U_TYPE_LAST);

  switch (u_type)
    {
    case CCI_U_TYPE_CHAR:
    case CCI_U_TYPE_STRING:
    case CCI_U_TYPE_NCHAR:
    case CCI_U_TYPE_VARNCHAR:
    case CCI_U_TYPE_NUMERIC:
    case CCI_U_TYPE_ENUM:
      if (ut_str_to_int (col_value_p, &data) < 0)
	return CCI_ER_TYPE_CONVERSION;
      break;
    case CCI_U_TYPE_BIGINT:
      {
	INT64 bi_val;
	NET_STR_TO_BIGINT (bi_val, col_value_p);
	data = (int) bi_val;
      }
      break;
    case CCI_U_TYPE_UBIGINT:
      {
	UINT64 ubi_val;
	NET_STR_TO_UBIGINT (ubi_val, col_value_p);
	data = (int) ubi_val;
      }
      break;
    case CCI_U_TYPE_INT:
    case CCI_U_TYPE_UINT:
      NET_STR_TO_INT (data, col_value_p);
      break;
    case CCI_U_TYPE_SHORT:
      {
	short s_val;
	NET_STR_TO_SHORT (s_val, col_value_p);
	data = (int) s_val;
      }
      break;
    case CCI_U_TYPE_USHORT:
      {
	unsigned short us_val;
	NET_STR_TO_USHORT (us_val, col_value_p);
	data = (int) us_val;
      }
      break;
    case CCI_U_TYPE_MONETARY:
    case CCI_U_TYPE_DOUBLE:
      {
	double d_val;
	NET_STR_TO_DOUBLE (d_val, col_value_p);
	data = (int) d_val;
      }
      break;
    case CCI_U_TYPE_FLOAT:
      {
	float f_val;
	NET_STR_TO_FLOAT (f_val, col_value_p);
	data = (int) f_val;
      }
      break;
    default:
      return CCI_ER_TYPE_CONVERSION;
    }

  *((int *) temp_value) = data;
  return 0;
}

int
qe_get_data_uint (T_CCI_U_TYPE u_type, char *col_value_p, void *value)
{
  unsigned int data = 0u;
  unsigned int *temp_value = (unsigned int *) value;

  assert (u_type >= CCI_U_TYPE_FIRST && u_type <= CCI_U_TYPE_LAST);

  switch (u_type)
    {
    case CCI_U_TYPE_CHAR:
    case CCI_U_TYPE_STRING:
    case CCI_U_TYPE_NCHAR:
    case CCI_U_TYPE_VARNCHAR:
    case CCI_U_TYPE_NUMERIC:
    case CCI_U_TYPE_ENUM:
      if (ut_str_to_uint (col_value_p, &data) < 0)
	return CCI_ER_TYPE_CONVERSION;
      break;
    case CCI_U_TYPE_BIGINT:
      {
	INT64 bi_val;
	NET_STR_TO_BIGINT (bi_val, col_value_p);
	data = (unsigned int) bi_val;
      }
      break;
    case CCI_U_TYPE_UBIGINT:
      {
	UINT64 ubi_val;
	NET_STR_TO_UBIGINT (ubi_val, col_value_p);
	data = (unsigned int) ubi_val;
      }
      break;
    case CCI_U_TYPE_INT:
    case CCI_U_TYPE_UINT:
      NET_STR_TO_UINT (data, col_value_p);
      break;
    case CCI_U_TYPE_SHORT:
      {
	short s_val;
	NET_STR_TO_SHORT (s_val, col_value_p);
	data = (unsigned int) s_val;
      }
      break;
    case CCI_U_TYPE_USHORT:
      {
	unsigned short us_val;
	NET_STR_TO_USHORT (us_val, col_value_p);
	data = (unsigned int) us_val;
      }
      break;
    case CCI_U_TYPE_MONETARY:
    case CCI_U_TYPE_DOUBLE:
      {
	double d_val;
	NET_STR_TO_DOUBLE (d_val, col_value_p);
	data = (unsigned int) d_val;
      }
      break;
    case CCI_U_TYPE_FLOAT:
      {
	float f_val;
	NET_STR_TO_FLOAT (f_val, col_value_p);
	data = (unsigned int) f_val;
      }
      break;
    default:
      return CCI_ER_TYPE_CONVERSION;
    }

  *((unsigned int *) temp_value) = data;
  return 0;
}

int
qe_get_data_float (T_CCI_U_TYPE u_type, char *col_value_p, void *value)
{
  float data = 0.F;
  float *temp_value = (float *) value;

  assert (u_type >= CCI_U_TYPE_FIRST && u_type <= CCI_U_TYPE_LAST);

  switch (u_type)
    {
    case CCI_U_TYPE_CHAR:
    case CCI_U_TYPE_STRING:
    case CCI_U_TYPE_NCHAR:
    case CCI_U_TYPE_VARNCHAR:
    case CCI_U_TYPE_NUMERIC:
    case CCI_U_TYPE_ENUM:
      if (ut_str_to_float (col_value_p, &data) < 0)
	return CCI_ER_TYPE_CONVERSION;
      break;
    case CCI_U_TYPE_BIGINT:
      {
	INT64 bi_val;
	NET_STR_TO_BIGINT (bi_val, col_value_p);
	data = (float) bi_val;
      }
      break;
    case CCI_U_TYPE_UBIGINT:
      {
	UINT64 ubi_val;
	NET_STR_TO_UBIGINT (ubi_val, col_value_p);
	data = (float) ubi_val;
      }
      break;
    case CCI_U_TYPE_INT:
      {
	int i_val;
	NET_STR_TO_INT (i_val, col_value_p);
	data = (float) i_val;
      }
      break;
    case CCI_U_TYPE_UINT:
      {
	unsigned int ui_val;
	NET_STR_TO_UINT (ui_val, col_value_p);
	data = (float) ui_val;
      }

      break;
    case CCI_U_TYPE_SHORT:
      {
	short s_val;
	NET_STR_TO_SHORT (s_val, col_value_p);
	data = (float) s_val;
      }
      break;
    case CCI_U_TYPE_USHORT:
      {
	unsigned short us_val;
	NET_STR_TO_USHORT (us_val, col_value_p);
	data = (float) us_val;
      }
      break;
    case CCI_U_TYPE_MONETARY:
    case CCI_U_TYPE_DOUBLE:
      {
	double d_val;
	NET_STR_TO_DOUBLE (d_val, col_value_p);
	data = (float) d_val;
      }
      break;
    case CCI_U_TYPE_FLOAT:
      {
	NET_STR_TO_FLOAT (data, col_value_p);
      }
      break;
    default:
      return CCI_ER_TYPE_CONVERSION;
    }

  *((float *) temp_value) = data;
  return 0;
}

int
qe_get_data_double (T_CCI_U_TYPE u_type, char *col_value_p, void *value)
{
  double data = 0.;
  double *temp_value = (double *) value;

  assert (u_type >= CCI_U_TYPE_FIRST && u_type <= CCI_U_TYPE_LAST);

  switch (u_type)
    {
    case CCI_U_TYPE_CHAR:
    case CCI_U_TYPE_STRING:
    case CCI_U_TYPE_NCHAR:
    case CCI_U_TYPE_VARNCHAR:
    case CCI_U_TYPE_NUMERIC:
    case CCI_U_TYPE_ENUM:
      if (ut_str_to_double (col_value_p, &data) < 0)
	return CCI_ER_TYPE_CONVERSION;
      break;
    case CCI_U_TYPE_BIGINT:
      {
	INT64 bi_val;
	NET_STR_TO_BIGINT (bi_val, col_value_p);
	data = (double) bi_val;
      }
      break;
    case CCI_U_TYPE_UBIGINT:
      {
	UINT64 ubi_val;
	NET_STR_TO_UBIGINT (ubi_val, col_value_p);
	data = (double) ubi_val;
      }
      break;
    case CCI_U_TYPE_INT:
      {
	int i_val;
	NET_STR_TO_INT (i_val, col_value_p);
	data = (double) i_val;
      }
      break;
    case CCI_U_TYPE_UINT:
      {
	unsigned int ui_val;
	NET_STR_TO_UINT (ui_val, col_value_p);
	data = (double) ui_val;
      }
      break;
    case CCI_U_TYPE_SHORT:
      {
	short s_val;
	NET_STR_TO_SHORT (s_val, col_value_p);
	data = (double) s_val;
      }
      break;
    case CCI_U_TYPE_USHORT:
      {
	unsigned short us_val;
	NET_STR_TO_USHORT (us_val, col_value_p);
	data = (double) us_val;
      }
      break;
    case CCI_U_TYPE_MONETARY:
    case CCI_U_TYPE_DOUBLE:
      {
	NET_STR_TO_DOUBLE (data, col_value_p);
      }
      break;
    case CCI_U_TYPE_FLOAT:
      {
	float f_val;
	NET_STR_TO_FLOAT (f_val, col_value_p);
	data = (double) f_val;
      }
      break;
    default:
      return CCI_ER_TYPE_CONVERSION;
    }

  *((double *) temp_value) = data;
  return 0;
}

int
qe_get_data_date (T_CCI_U_TYPE u_type, char *col_value_p, void *value)
{
  T_CCI_DATE data = { 0, 0, 0, 0, 0, 0, 0 };
  T_CCI_DATE *temp_value = (T_CCI_DATE *) value;

  assert (u_type >= CCI_U_TYPE_FIRST && u_type <= CCI_U_TYPE_LAST);

  memset ((char *) &data, 0, sizeof (T_CCI_DATE));

  switch (u_type)
    {
    case CCI_U_TYPE_DATE:
      NET_STR_TO_DATE (data, col_value_p);
      break;
    case CCI_U_TYPE_TIME:
      NET_STR_TO_TIME (data, col_value_p);
      break;
    case CCI_U_TYPE_TIMESTAMP:
      NET_STR_TO_TIMESTAMP (data, col_value_p);
      break;
    case CCI_U_TYPE_DATETIME:
      NET_STR_TO_DATETIME (data, col_value_p);
      break;
    default:
      return CCI_ER_TYPE_CONVERSION;
    }

  *((T_CCI_DATE *) temp_value) = data;
  return 0;
}

int
qe_get_data_date_tz (T_CCI_U_TYPE u_type, char *col_value_p, void *value, int total_size)
{
  T_CCI_DATE_TZ data = { 0, 0, 0, 0, 0, 0, 0, "" };
  T_CCI_DATE_TZ *temp_value = (T_CCI_DATE_TZ *) value;

  assert (u_type >= CCI_U_TYPE_FIRST && u_type <= CCI_U_TYPE_LAST);

  memset ((char *) &data, 0, sizeof (data));

  switch (u_type)
    {
    case CCI_U_TYPE_TIMESTAMPTZ:
    case CCI_U_TYPE_TIMESTAMPLTZ:
      NET_STR_TO_TIMESTAMPTZ (data, col_value_p, total_size);
      break;
    case CCI_U_TYPE_DATETIMETZ:
    case CCI_U_TYPE_DATETIMELTZ:
      NET_STR_TO_DATETIMETZ (data, col_value_p, total_size);
      break;
    default:
      return CCI_ER_TYPE_CONVERSION;
    }

  *((T_CCI_DATE_TZ *) temp_value) = data;
  return 0;
}

int
qe_get_data_lob (T_CCI_U_TYPE u_type, char *col_value_p, int col_val_size, void *value)
{
  T_LOB *lob;

  assert (u_type >= CCI_U_TYPE_FIRST && u_type <= CCI_U_TYPE_LAST);

  if (u_type != CCI_U_TYPE_BLOB && u_type != CCI_U_TYPE_CLOB)
    {
      return CCI_ER_TYPE_CONVERSION;
    }

  lob = (T_LOB *) MALLOC (sizeof (T_LOB));
  if (lob == NULL)
    {
      return CCI_ER_NO_MORE_MEMORY;
    }

  lob->type = u_type;
  lob->handle_size = col_val_size;
  lob->handle = (char *) MALLOC (col_val_size);
  if (lob->handle == NULL)
    {
      FREE_MEM (lob);
      return CCI_ER_NO_MORE_MEMORY;
    }
  memcpy ((char *) lob->handle, col_value_p, col_val_size);

  *((T_LOB **) value) = lob;
  return 0;
}

int
qe_get_data_req_handle (T_CON_HANDLE * con_handle, T_REQ_HANDLE * req_handle, char *col_value_p, void *value)
{
  T_REQ_HANDLE *out_req_handle = NULL;
  T_NET_BUF net_buf;
  char func_code = CAS_FC_MAKE_OUT_RS;
  int error = CCI_ER_NO_ERROR;
  int statement_id = -1;
  int srv_handle = 0;
  char *result_msg = NULL;
  int result_msg_size;

  if (req_handle == NULL)
    {
      return CCI_ER_REQ_HANDLE;
    }

  if (req_handle->stmt_type != CUBRID_STMT_CALL_SP)
    {
      return CCI_ER_TYPE_CONVERSION;
    }

  statement_id = hm_req_handle_alloc (con_handle, &out_req_handle);
  if (statement_id < 0)
    {
      error = statement_id;
      goto get_data_error;
    }

  NET_STR_TO_INT (srv_handle, col_value_p);

  net_buf_init (&net_buf);
  net_buf_cp_str (&net_buf, &func_code, 1);
  ADD_ARG_INT (&net_buf, srv_handle);

  error = net_send_msg (con_handle, net_buf.data, net_buf.data_size);
  net_buf_clear (&net_buf);
  if (error < 0)
    {
      error = CCI_ER_COMMUNICATION;
      goto get_data_error;
    }

  error = net_recv_msg_timeout (con_handle, &result_msg, &result_msg_size, NULL, 0);
  if (error < 0)
    {
      goto get_data_error;
    }

  result_msg_size -= 4;
  error = out_rs_info_decode (result_msg + 4, &result_msg_size, out_req_handle);
  if (error < 0)
    {
      goto get_data_error;
    }

  if (result_msg != NULL)
    {
      FREE_MEM (result_msg);
    }

  map_open_ots (statement_id, &out_req_handle->mapped_stmt_id);
  *((int *) value) = out_req_handle->mapped_stmt_id;

  return 0;

get_data_error:
  if (result_msg != NULL)
    {
      FREE_MEM (result_msg);
    }

  if (out_req_handle != NULL)
    {
      hm_req_handle_free (con_handle, out_req_handle);
      out_req_handle = NULL;
    }

  return error;
}

int
qe_get_data_bit (T_CCI_U_TYPE u_type, char *col_value_p, int col_val_size, void *value)
{
  if (u_type == CCI_U_TYPE_BIT || u_type == CCI_U_TYPE_VARBIT)
    {
      ((T_CCI_BIT *) value)->size = col_val_size;
      ((T_CCI_BIT *) value)->buf = col_value_p;
      return 0;
    }

  return CCI_ER_TYPE_CONVERSION;
}

int
qe_get_attr_type_str (T_CON_HANDLE * con_handle, char *class_name, char *attr_name, char *buf, int buf_size,
		      T_CCI_ERROR * err_buf)
{
  T_NET_BUF net_buf;
  int err_code;
  char func_code = CAS_FC_GET_ATTR_TYPE_STR;
  char *result_msg = NULL;
  int result_msg_size;

  net_buf_init (&net_buf);

  net_buf_cp_str (&net_buf, &func_code, 1);

  ADD_ARG_STR (&net_buf, class_name, (int) strlen (class_name) + 1, con_handle->charset);
  ADD_ARG_STR (&net_buf, attr_name, (int) strlen (attr_name) + 1, con_handle->charset);

  if (net_buf.err_code < 0)
    {
      err_code = net_buf.err_code;
      net_buf_clear (&net_buf);
      return err_code;
    }

  err_code = net_send_msg (con_handle, net_buf.data, net_buf.data_size);
  net_buf_clear (&net_buf);
  if (err_code < 0)
    return err_code;

  err_code = net_recv_msg (con_handle, &result_msg, &result_msg_size, err_buf);
  if (err_code < 0)
    return err_code;

  if (buf)
    {
      buf_size = MIN (buf_size - 1, result_msg_size - 4);
      strncpy (buf, result_msg + 4, buf_size);
      buf[buf_size] = '\0';
    }

  FREE_MEM (result_msg);

  return err_code;
}

int
qe_get_query_info (T_REQ_HANDLE * req_handle, T_CON_HANDLE * con_handle, char log_type, char **out_buf)
{
  T_NET_BUF net_buf;
  char func_code = CAS_FC_GET_QUERY_INFO;
  int err_code;
  char *result_msg = NULL;
  int result_msg_size;

  net_buf_init (&net_buf);
  net_buf_cp_str (&net_buf, &func_code, 1);
  ADD_ARG_INT (&net_buf, req_handle->server_handle_id);
  ADD_ARG_BYTES (&net_buf, &log_type, 1);

  if (req_handle->sql_text)
    {
      int sql_stmt_size;

      sql_stmt_size = (int) strlen (req_handle->sql_text) + 1;

      ADD_ARG_STR (&net_buf, req_handle->sql_text, sql_stmt_size, con_handle->charset);
    }

  if (net_buf.err_code < 0)
    {
      err_code = net_buf.err_code;
      net_buf_clear (&net_buf);
      return err_code;
    }

  err_code = net_send_msg (con_handle, net_buf.data, net_buf.data_size);
  net_buf_clear (&net_buf);
  if (err_code < 0)
    return err_code;

  err_code = net_recv_msg (con_handle, &result_msg, &result_msg_size, NULL);
  if (err_code < 0)
    return err_code;

  if (out_buf)
    {
      char *tmp_buf;
      tmp_buf = (char *) MALLOC (result_msg_size - 4);
      if (tmp_buf == NULL)
	{
	  FREE_MEM (result_msg);
	  return CCI_ER_NO_MORE_MEMORY;
	}
      memcpy (tmp_buf, result_msg + 4, result_msg_size - 4);
      *out_buf = tmp_buf;
    }

  FREE_MEM (result_msg);

  return err_code;
}

int
qe_savepoint_cmd (T_CON_HANDLE * con_handle, char cmd, const char *savepoint_name, T_CCI_ERROR * err_buf)
{
  T_NET_BUF net_buf;
  int err_code;
  char func_code = CAS_FC_SAVEPOINT;

  if (savepoint_name == NULL)
    {
      savepoint_name = "";
    }

  if (cmd < CCI_SP_CMD_FIRST || cmd > CCI_SP_CMD_LAST)
    return CCI_ER_SAVEPOINT_CMD;

  net_buf_init (&net_buf);

  net_buf_cp_str (&net_buf, &func_code, 1);

  ADD_ARG_BYTES (&net_buf, &cmd, 1);
  ADD_ARG_STR (&net_buf, (char *) savepoint_name, (int) strlen (savepoint_name) + 1, con_handle->charset);

  if (net_buf.err_code < 0)
    {
      err_code = net_buf.err_code;
      net_buf_clear (&net_buf);
      return err_code;
    }

  err_code = net_send_msg (con_handle, net_buf.data, net_buf.data_size);
  net_buf_clear (&net_buf);
  if (err_code < 0)
    return err_code;

  err_code = net_recv_msg (con_handle, NULL, NULL, err_buf);

  return err_code;
}

int
qe_get_param_info (T_REQ_HANDLE * req_handle, T_CON_HANDLE * con_handle, T_CCI_PARAM_INFO ** res_param,
		   T_CCI_ERROR * err_buf)
{
  T_NET_BUF net_buf;
  char func_code = CAS_FC_PARAMETER_INFO;
  char *result_msg = NULL;
  int result_msg_size;
  int err_code;
  int num_param;

  if (res_param)
    *res_param = NULL;

  net_buf_init (&net_buf);

  net_buf_cp_str (&net_buf, &func_code, 1);

  ADD_ARG_INT (&net_buf, req_handle->server_handle_id);

  if (net_buf.err_code < 0)
    {
      err_code = net_buf.err_code;
      net_buf_clear (&net_buf);
      return err_code;
    }

  err_code = net_send_msg (con_handle, net_buf.data, net_buf.data_size);
  net_buf_clear (&net_buf);
  if (err_code < 0)
    {
      return err_code;
    }

  num_param = net_recv_msg (con_handle, &result_msg, &result_msg_size, err_buf);
  if (num_param < 0)
    {
      return num_param;
    }

  if (res_param)
    {
      err_code = parameter_info_decode (result_msg + 4, result_msg_size - 4, num_param, res_param);
      if (err_code < 0)
	{
	  num_param = err_code;
	}
    }

  FREE_MEM (result_msg);

  return num_param;
}

void
qe_param_info_free (T_CCI_PARAM_INFO * param)
{
  FREE_MEM (param);
}

#if defined(WINDOWS)
int
qe_set_charset (T_CON_HANDLE * con_handle, char *charset)
{
  if (con_handle->charset)
    {
      FREE (con_handle->charset);
    }
  con_handle->charset = strdup (charset);

  if (con_handle->charset == NULL)
    {
      return CCI_ER_NO_MORE_MEMORY;
    }
  return 0;
}
#endif

#ifdef CCI_XA
int
qe_xa_prepare (T_CON_HANDLE * con_handle, XID * xid, T_CCI_ERROR * err_buf)
{
  T_NET_BUF net_buf;
  char func_code = CAS_FC_XA_PREPARE;
  int err_code;

  net_buf_init (&net_buf);

  net_buf_cp_str (&net_buf, &func_code, 1);
  add_arg_xid (&net_buf, xid);

  if (net_buf.err_code < 0)
    {
      err_code = net_buf.err_code;
      net_buf_clear (&net_buf);
      return err_code;
    }

  err_code = net_send_msg (con_handle, net_buf.data, net_buf.data_size);
  net_buf_clear (&net_buf);
  if (err_code < 0)
    return err_code;

  err_code = net_recv_msg (con_handle, NULL, NULL, err_buf);

  return err_code;
}

int
qe_xa_recover (T_CON_HANDLE * con_handle, XID * xid, int num_xid, T_CCI_ERROR * err_buf)
{
  T_NET_BUF net_buf;
  char func_code = CAS_FC_XA_RECOVER;
  int res;
  char *result_msg = NULL;
  int result_msg_size = 0;

  net_buf_init (&net_buf);

  net_buf_cp_str (&net_buf, &func_code, 1);

  if (net_buf.err_code < 0)
    {
      res = net_buf.err_code;
      net_buf_clear (&net_buf);
      return res;
    }

  res = net_send_msg (con_handle, net_buf.data, net_buf.data_size);
  net_buf_clear (&net_buf);
  if (res < 0)
    return res;

  res = net_recv_msg (con_handle, &result_msg, &result_msg_size, err_buf);
  if (res < 0)
    return res;

  res = xa_prepare_info_decode (result_msg + 4, result_msg_size - 4, res, xid, num_xid);
  FREE_MEM (result_msg);
  return res;
}

int
qe_xa_end_tran (T_CON_HANDLE * con_handle, XID * xid, char type, T_CCI_ERROR * err_buf)
{
  T_NET_BUF net_buf;
  char func_code = CAS_FC_XA_END_TRAN;
  int err_code;

  net_buf_init (&net_buf);

  net_buf_cp_str (&net_buf, &func_code, 1);

  add_arg_xid (&net_buf, xid);
  ADD_ARG_BYTES (&net_buf, &type, 1);

  if (net_buf.err_code < 0)
    {
      err_code = net_buf.err_code;
      net_buf_clear (&net_buf);
      return err_code;
    }

  err_code = net_send_msg (con_handle, net_buf.data, net_buf.data_size);
  net_buf_clear (&net_buf);
  if (err_code < 0)
    return err_code;

  err_code = net_recv_msg (con_handle, NULL, NULL, err_buf);

  CLOSE_SOCKET (con_handle->sock_fd);
  con_handle->sock_fd = INVALID_SOCKET;

  return err_code;
}
#endif

int
qe_lob_new (T_CON_HANDLE * con_handle, T_LOB ** lob, T_CCI_U_TYPE type, T_CCI_ERROR * err_buf)
{
  T_NET_BUF net_buf;
  char func_code = CAS_FC_LOB_NEW;
  int err_code = 0;
  char *result_msg = NULL;
  int result_msg_size;
  char *cur_p;
  T_LOB *new_lob;
  int handle_size;

  net_buf_init (&net_buf);
  net_buf_cp_str (&net_buf, &func_code, 1);

  ADD_ARG_INT (&net_buf, type);
  if (net_buf.err_code < 0)
    {
      err_code = net_buf.err_code;
      net_buf_clear (&net_buf);
      return err_code;
    }

  err_code = net_send_msg (con_handle, net_buf.data, net_buf.data_size);
  net_buf_clear (&net_buf);
  if (err_code < 0)
    {
      return err_code;
    }

  handle_size = net_recv_msg (con_handle, &result_msg, &result_msg_size, err_buf);
  if (handle_size < 0)
    {
      return handle_size;
    }
  else if (handle_size == 0)
    {
      return CCI_ER_COMMUNICATION;
    }

  cur_p = result_msg;
  cur_p += 4;

  new_lob = (T_LOB *) MALLOC (sizeof (T_LOB));
  if (new_lob == NULL)
    {
      FREE_MEM (result_msg);
      return CCI_ER_NO_MORE_MEMORY;
    }

  new_lob->type = type;
  new_lob->handle_size = handle_size;
  new_lob->handle = (char *) MALLOC (new_lob->handle_size);
  if (new_lob->handle == NULL)
    {
      FREE_MEM (result_msg);
      FREE_MEM (new_lob);
      return CCI_ER_NO_MORE_MEMORY;
    }

  memcpy (new_lob->handle, cur_p, new_lob->handle_size);

  if (result_msg_size < NET_SIZE_INT + new_lob->handle_size)
    {
      FREE_MEM (result_msg);
      FREE_MEM (new_lob->handle);
      FREE_MEM (new_lob);
      return CCI_ER_COMMUNICATION;
    }

  *lob = new_lob;

  FREE_MEM (result_msg);
  return CCI_ER_NO_ERROR;
}

int
qe_lob_write (T_CON_HANDLE * con_handle, T_LOB * lob, INT64 start_pos, int length, const char *buf,
	      T_CCI_ERROR * err_buf)
{
  T_NET_BUF net_buf;
  char func_code = CAS_FC_LOB_WRITE;
  int err_code = 0;
  char *result_msg = NULL;
  int result_msg_size;
  INT64 lob_size;
  int bytes_written;

  net_buf_init (&net_buf);
  net_buf_cp_str (&net_buf, &func_code, 1);

  ADD_ARG_BYTES (&net_buf, lob->handle, lob->handle_size);
  ADD_ARG_INT64 (&net_buf, start_pos);
  ADD_ARG_BYTES (&net_buf, buf, length);
  if (net_buf.err_code < 0)
    {
      err_code = net_buf.err_code;
      net_buf_clear (&net_buf);
      return err_code;
    }

  err_code = net_send_msg (con_handle, net_buf.data, net_buf.data_size);
  net_buf_clear (&net_buf);
  if (err_code < 0)
    {
      return err_code;
    }

  bytes_written = net_recv_msg (con_handle, &result_msg, &result_msg_size, err_buf);
  if (bytes_written < 0)
    {
      return bytes_written;
    }

  if (result_msg_size < NET_SIZE_INT || bytes_written > length)
    {
      FREE_MEM (result_msg);
      return CCI_ER_COMMUNICATION;
    }

  lob_size = t_lob_get_size (lob->handle);
  if (start_pos + bytes_written > lob_size)
    {
      t_lob_set_size (lob, start_pos + bytes_written);
    }

  FREE_MEM (result_msg);
  return bytes_written;
}

int
qe_lob_read (T_CON_HANDLE * con_handle, T_LOB * lob, INT64 start_pos, int length, char *buf, T_CCI_ERROR * err_buf)
{
  T_NET_BUF net_buf;
  char func_code = CAS_FC_LOB_READ;
  int err_code = 0;
  char *result_msg = NULL;
  int result_msg_size;
  int bytes_read;

  net_buf_init (&net_buf);
  net_buf_cp_str (&net_buf, &func_code, 1);

  ADD_ARG_BYTES (&net_buf, lob->handle, lob->handle_size);
  ADD_ARG_INT64 (&net_buf, start_pos);
  ADD_ARG_INT (&net_buf, length);
  if (net_buf.err_code < 0)
    {
      err_code = net_buf.err_code;
      net_buf_clear (&net_buf);
      return err_code;
    }

  err_code = net_send_msg (con_handle, net_buf.data, net_buf.data_size);
  net_buf_clear (&net_buf);
  if (err_code < 0)
    {
      return err_code;
    }

  bytes_read = net_recv_msg (con_handle, &result_msg, &result_msg_size, err_buf);
  if (bytes_read < 0)
    {
      return bytes_read;
    }

  if (result_msg_size < NET_SIZE_INT || bytes_read > length)
    {
      FREE_MEM (result_msg);
      return CCI_ER_COMMUNICATION;
    }

  if (bytes_read > 0)
    {
      memcpy (buf, result_msg + 4, bytes_read);
    }

  FREE_MEM (result_msg);
  return bytes_read;
}


int
qe_get_shard_info (T_CON_HANDLE * con_handle, T_CCI_SHARD_INFO ** shard_info, T_CCI_ERROR * err_buf)
{
  T_NET_BUF net_buf;
  char func_code = CAS_FC_GET_SHARD_INFO;
  char *result_msg = NULL;
  int result_msg_size;
  int err_code;
  int num_shard;

  if (shard_info)
    {
      *shard_info = NULL;
    }

  net_buf_init (&net_buf);
  net_buf_cp_str (&net_buf, &func_code, 1);
  if (net_buf.err_code < 0)
    {
      err_code = net_buf.err_code;
      net_buf_clear (&net_buf);
      return err_code;
    }

  err_code = net_send_msg (con_handle, net_buf.data, net_buf.data_size);
  net_buf_clear (&net_buf);
  if (err_code < 0)
    {
      return err_code;
    }

  num_shard = net_recv_msg (con_handle, &result_msg, &result_msg_size, err_buf);
  if (num_shard < 0)
    {
      FREE_MEM (result_msg);
      return num_shard;
    }

  assert (num_shard != 0);
  if (num_shard)
    {
      err_code = shard_info_decode (result_msg + 4, result_msg_size - 4, num_shard, shard_info);
      if (err_code < 0)
	{
	  num_shard = err_code;
	}
    }

  FREE_MEM (result_msg);

  return num_shard;
}

int
qe_shard_info_free (T_CCI_SHARD_INFO * shard_info)
{
  T_CCI_SHARD_INFO *cur_shard_info;
  int prev_shard_id = SHARD_ID_INVALID;

  for (cur_shard_info = shard_info; cur_shard_info->shard_id != SHARD_ID_INVALID; cur_shard_info++)
    {
      if (cur_shard_info->shard_id <= prev_shard_id)
	{
	  assert (cur_shard_info->shard_id == SHARD_ID_INVALID);	/* fence */
	  break;
	}

      FREE_MEM (cur_shard_info->db_name);
      FREE_MEM (cur_shard_info->db_server);

      if (cur_shard_info != shard_info)
	{
	  prev_shard_id = cur_shard_info->shard_id;
	}
    }

  FREE_MEM (shard_info);
  return CCI_ER_NO_ERROR;
}

int
qe_is_shard (T_CON_HANDLE * con_handle)
{
  int type;

  type = con_handle->broker_info[BROKER_INFO_DBMS_TYPE];
  if (IS_CONNECTED_TO_PROXY (type))
    {
      return 1;
    }
  else
    {
      return 0;
    }
}

/************************************************************************
 * IMPLEMENTATION OF PRIVATE FUNCTIONS	 				*
 ************************************************************************/

#if defined(WINDOWS)
int
encode_string (const char *str, int size, char **target, char *charset)
{
  int nLength;
  char *tmp_string;
  BSTR bstrCode;
  int wincode;

  wincode = get_windows_charset_code (charset);

  if (wincode == 0)
    {
      return 0;
    }

  nLength = MultiByteToWideChar (CP_ACP, 0, (LPCSTR) str, size, NULL, 0);
  bstrCode = SysAllocStringLen (NULL, nLength);
  if (bstrCode == NULL)
    {
      return CCI_ER_NO_MORE_MEMORY;
    }
  MultiByteToWideChar (CP_ACP, 0, (LPCSTR) str, size, bstrCode, nLength);

  nLength = WideCharToMultiByte (wincode, 0, bstrCode, -1, NULL, 0, NULL, NULL);
  tmp_string = (char *) MALLOC (sizeof (char) * (nLength + 1));
  if (tmp_string == NULL)
    {
      return CCI_ER_NO_MORE_MEMORY;
    }

  WideCharToMultiByte (wincode, 0, bstrCode, -1, tmp_string, nLength, NULL, NULL);
  if (target)
    {
      *target = tmp_string;
    }

  SysFreeString (bstrCode);

  return nLength;
}

int
decode_result_col (char *column_p, int size, char **target, char *charset)
{
  int nLength;
  char *new_column_p;
  char *str = (column_p + 4);
  BSTR bstrCode;
  int wincode;

  wincode = get_windows_charset_code (charset);

  if (wincode == 0)
    {
      return 0;
    }

  nLength = MultiByteToWideChar (wincode, 0, (LPCSTR) str, size, NULL, 0);
  bstrCode = SysAllocStringLen (NULL, nLength);
  if (bstrCode == NULL)
    {
      return CCI_ER_NO_MORE_MEMORY;
    }
  MultiByteToWideChar (wincode, 0, (LPCSTR) str, size, bstrCode, nLength);

  nLength = WideCharToMultiByte (CP_ACP, 0, bstrCode, -1, NULL, 0, NULL, NULL);
  new_column_p = (char *) MALLOC (sizeof (char) * (nLength + 4));
  if (new_column_p == NULL)
    {
      return CCI_ER_NO_MORE_MEMORY;
    }

  memcpy (new_column_p, column_p, 4);
  str = new_column_p + 4;

  WideCharToMultiByte (CP_ACP, 0, bstrCode, -1, str, nLength, NULL, NULL);
  if (target)
    {
      *target = new_column_p;
    }

  SysFreeString (bstrCode);

  return nLength;
}

static int
get_windows_charset_code (char *str)
{
  if (str == NULL)
    {
      return 0;
    }

  if (strcasecmp (str, "utf-8") == 0)
    {
      return CP_UTF8;
    }

  return 0;
}
#endif

static int
prepare_info_decode (char *buf, int *size, T_REQ_HANDLE * req_handle)
{
  int num_bind_info;
  int num_col_info;
  int remain_size = *size;
  char stmt_type;
  char updatable_flag = 0;
  char *cur_p = buf;
  T_CCI_COL_INFO *col_info = NULL;
  int result_cache_lifetime;

  if (remain_size < NET_SIZE_INT)
    {
      return CCI_ER_COMMUNICATION;
    }

  NET_STR_TO_INT (result_cache_lifetime, cur_p);
  remain_size -= NET_SIZE_INT;
  cur_p += NET_SIZE_INT;

  if (remain_size < NET_SIZE_BYTE)
    {
      return CCI_ER_COMMUNICATION;
    }

  NET_STR_TO_BYTE (stmt_type, cur_p);
  remain_size -= NET_SIZE_BYTE;
  cur_p += NET_SIZE_BYTE;

  if (remain_size < NET_SIZE_INT)
    {
      return CCI_ER_COMMUNICATION;
    }

  NET_STR_TO_INT (num_bind_info, cur_p);
  remain_size -= NET_SIZE_INT;
  cur_p += NET_SIZE_INT;

  if (remain_size < NET_SIZE_BYTE)
    {
      return CCI_ER_COMMUNICATION;
    }

  NET_STR_TO_BYTE (updatable_flag, cur_p);
  remain_size -= NET_SIZE_BYTE;
  cur_p += NET_SIZE_BYTE;

  num_col_info = get_column_info (cur_p, &remain_size, &col_info, NULL, true);
  if (num_col_info < 0)
    {
      assert (col_info == NULL);
      return num_col_info;
    }

  req_handle_col_info_free (req_handle);

  req_handle->num_bind = num_bind_info;
  req_handle->num_col_info = num_col_info;
  req_handle->col_info = col_info;
  req_handle->stmt_type = (T_CCI_CUBRID_STMT) stmt_type;
  req_handle->first_stmt_type = req_handle->stmt_type;
  req_handle->updatable_flag = updatable_flag;

  *size = remain_size;
  return 0;
}

int
out_rs_info_decode (char *buf, int *size, T_REQ_HANDLE * req_handle)
{
  char *cur_p = buf;
  int remain_size = *size;
  int out_srv_handle_id;
  char stmt_type;
  int max_row;
  char updatable_flag;
  int num_col_info;
  T_CCI_COL_INFO *col_info = NULL;

  if (remain_size < NET_SIZE_INT)
    {
      return CCI_ER_COMMUNICATION;
    }

  NET_STR_TO_INT (out_srv_handle_id, cur_p);
  remain_size -= NET_SIZE_INT;
  cur_p += NET_SIZE_INT;
  if (remain_size < NET_SIZE_BYTE)
    {
      return CCI_ER_COMMUNICATION;
    }

  NET_STR_TO_BYTE (stmt_type, cur_p);
  remain_size -= NET_SIZE_BYTE;
  cur_p += NET_SIZE_BYTE;
  if (remain_size < NET_SIZE_INT)
    {
      return CCI_ER_COMMUNICATION;
    }

  NET_STR_TO_INT (max_row, cur_p);
  remain_size -= NET_SIZE_INT;
  cur_p += NET_SIZE_INT;
  if (remain_size < NET_SIZE_BYTE)
    {
      return CCI_ER_COMMUNICATION;
    }

  NET_STR_TO_BYTE (updatable_flag, cur_p);
  remain_size -= NET_SIZE_BYTE;
  cur_p += NET_SIZE_BYTE;

  num_col_info = get_column_info (cur_p, &remain_size, &col_info, NULL, true);
  if (num_col_info < 0)
    {
      return num_col_info;
    }

  req_handle_col_info_free (req_handle);

  req_handle->server_handle_id = out_srv_handle_id;
  req_handle->handle_type = HANDLE_PREPARE;
  req_handle->handle_sub_type = 0;
  req_handle->num_tuple = max_row;
  req_handle->num_col_info = num_col_info;
  req_handle->col_info = col_info;
  req_handle->stmt_type = (T_CCI_CUBRID_STMT) stmt_type;
  req_handle->first_stmt_type = req_handle->stmt_type;
  req_handle->updatable_flag = updatable_flag;
  req_handle->cursor_pos = 0;
  /**
   * dummy. actually we don't need below information.
   * if we don't make this, cci_close_query_result() will raise error.
   */
  req_handle->num_query_res = 1;
  req_handle->qr = (T_CCI_QUERY_RESULT *) MALLOC (sizeof (T_CCI_QUERY_RESULT));
  if (req_handle->qr == NULL)
    {
      return CCI_ER_NO_MORE_MEMORY;
    }

  req_handle->qr[0].result_count = 1;
  req_handle->qr[0].stmt_type = stmt_type;
  req_handle->qr[0].err_no = 0;
  req_handle->qr[0].err_msg = NULL;

  return 0;
}

static int
get_cursor_pos (T_REQ_HANDLE * req_handle, int offset, char origin)
{
  if (origin == CCI_CURSOR_FIRST)
    {
      return offset;
    }
  else if (origin == CCI_CURSOR_LAST)
    {
      return (req_handle->num_tuple - offset + 1);
    }
  return (req_handle->cursor_pos + offset);
}

static int
fetch_info_decode (char *buf, int size, int num_cols, T_TUPLE_VALUE ** tuple_value, T_FETCH_TYPE fetch_type,
		   T_REQ_HANDLE * req_handle, T_CON_HANDLE * con_handle)
{
  int remain_size = size;
  char *cur_p = buf;
  int err_code = 0;
  int num_tuple, i, j;
  T_TUPLE_VALUE *tmp_tuple_value = NULL;
#if defined (WINDOWS)
  char *charset = con_handle->charset;
#endif

  if (fetch_type == FETCH_FETCH || fetch_type == FETCH_COL_GET)
    {
      if (remain_size < 4)
	{
	  return CCI_ER_COMMUNICATION;
	}

      NET_STR_TO_INT (num_tuple, cur_p);
      remain_size -= 4;
      cur_p += 4;
    }
  else
    {
      num_tuple = 1;
    }

  if (num_tuple < 0)
    {
      return 0;
    }
  else if (num_tuple == 0)
    {
      if (fetch_type == FETCH_FETCH && hm_get_broker_version (con_handle) >= CAS_PROTO_MAKE_VER (PROTOCOL_V5))
	{
	  if (remain_size < NET_SIZE_BYTE)
	    {
	      return CCI_ER_COMMUNICATION;
	    }

	  NET_STR_TO_BYTE (req_handle->is_fetch_completed, cur_p);
	  remain_size -= NET_SIZE_BYTE;
	  cur_p += NET_SIZE_BYTE;
	}

      return 0;
    }

  tmp_tuple_value = (T_TUPLE_VALUE *) MALLOC (sizeof (T_TUPLE_VALUE) * num_tuple);
  if (tmp_tuple_value == NULL)
    return CCI_ER_NO_MORE_MEMORY;
  memset ((char *) tmp_tuple_value, 0, sizeof (T_TUPLE_VALUE) * num_tuple);

  for (i = 0; i < num_tuple; i++)
    {
      if (fetch_type == FETCH_FETCH)
	{
	  if (remain_size < 4)
	    {
	      err_code = CCI_ER_COMMUNICATION;
	      goto fetch_info_decode_error;
	    }
	  NET_STR_TO_INT (tmp_tuple_value[i].tuple_index, cur_p);
	  cur_p += 4;
	  remain_size -= 4;

	  if (remain_size < NET_SIZE_OBJECT)
	    {
	      err_code = CCI_ER_COMMUNICATION;
	      goto fetch_info_decode_error;
	    }
	  stream_to_obj (cur_p, &(tmp_tuple_value[i].tuple_oid));
	  cur_p += NET_SIZE_OBJECT;
	  remain_size -= NET_SIZE_OBJECT;
	}

      tmp_tuple_value[i].column_ptr = (char **) MALLOC (sizeof (char *) * num_cols);
      if (tmp_tuple_value[i].column_ptr == NULL)
	{
	  err_code = CCI_ER_NO_MORE_MEMORY;
	  goto fetch_info_decode_error;
	}
      memset (tmp_tuple_value[i].column_ptr, '\0', sizeof (char *) * num_cols);

#if defined(WINDOWS)
      tmp_tuple_value[i].decoded_ptr = (char **) MALLOC (sizeof (char *) * num_cols);
      if (tmp_tuple_value[i].decoded_ptr == NULL)
	{
	  err_code = CCI_ER_NO_MORE_MEMORY;
	  goto fetch_info_decode_error;
	}
      memset (tmp_tuple_value[i].decoded_ptr, '\0', sizeof (char *) * num_cols);
#endif

      for (j = 0; j < num_cols; j++)
	{
	  int data_size;
	  char *col_p;
#if defined (WINDOWS)
	  T_CCI_U_TYPE u_type;
#endif

	  col_p = cur_p;
	  if (remain_size < 4)
	    {
	      err_code = CCI_ER_COMMUNICATION;
	      goto fetch_info_decode_error;
	    }
	  NET_STR_TO_INT (data_size, cur_p);
	  remain_size -= 4;
	  cur_p += 4;

	  if (remain_size < data_size)
	    {
	      err_code = CCI_ER_COMMUNICATION;
	      goto fetch_info_decode_error;
	    }

#if defined (WINDOWS)
	  if (charset != NULL)
	    {
	      u_type = get_basic_utype (req_handle->col_info[j].ext_type);
	    }

	  if (charset != NULL
	      && (u_type == CCI_U_TYPE_CHAR || u_type == CCI_U_TYPE_STRING || u_type == CCI_U_TYPE_NCHAR
		  || u_type == CCI_U_TYPE_VARNCHAR || u_type == CCI_U_TYPE_ENUM || u_type == CCI_U_TYPE_JSON))
	    {
	      err_code = decode_result_col (col_p, data_size, &(tmp_tuple_value[i].column_ptr[j]), charset);

	      if (err_code < 0)
		{
		  goto fetch_info_decode_error;
		}
	      else if (err_code == 0)
		{
		  /* invalid character set. do not convert string */
		  tmp_tuple_value[i].column_ptr[j] = col_p;
		}
	      else
		{
		  tmp_tuple_value[i].decoded_ptr[j] = tmp_tuple_value[i].column_ptr[j];
		}
	    }
	  else
	    {
	      tmp_tuple_value[i].column_ptr[j] = col_p;
	    }
#else
	  tmp_tuple_value[i].column_ptr[j] = col_p;
#endif

	  if (data_size > 0)
	    {
	      cur_p += data_size;
	      remain_size -= data_size;
	    }
	}			/* end of for j */
    }				/* end of for i */

  if (fetch_type == FETCH_FETCH && hm_get_broker_version (con_handle) >= CAS_PROTO_MAKE_VER (PROTOCOL_V5))
    {
      if (remain_size < NET_SIZE_BYTE)
	{
	  return CCI_ER_COMMUNICATION;
	}

      NET_STR_TO_BYTE (req_handle->is_fetch_completed, cur_p);
      remain_size -= NET_SIZE_BYTE;
      cur_p += NET_SIZE_BYTE;
    }

  *tuple_value = tmp_tuple_value;

  return num_tuple;

fetch_info_decode_error:
  if (tmp_tuple_value)
    {
      for (i = 0; i < num_tuple; i++)
	{
#if defined (WINDOWS)
	  if (tmp_tuple_value[i].decoded_ptr)
	    {
	      for (j = 0; j < num_cols; j++)
		{
		  FREE_MEM (tmp_tuple_value[i].decoded_ptr[j]);
		}

	      FREE_MEM (tmp_tuple_value[i].decoded_ptr);
	    }
#endif
	  FREE_MEM (tmp_tuple_value[i].column_ptr);
	}
      FREE_MEM (tmp_tuple_value);
    }
  return err_code;
}

static void
stream_to_obj (char *buf, T_OBJECT * obj)
{
  int pageid;
  short volid, slotid;

  NET_STR_TO_INT (pageid, buf);
  NET_STR_TO_SHORT (slotid, buf + 4);
  NET_STR_TO_SHORT (volid, buf + 6);

  obj->pageid = pageid;
  obj->slotid = slotid;
  obj->volid = volid;
}

static int
get_data_set (T_CCI_U_EXT_TYPE u_ext_type, char *col_value_p, T_SET ** value, int data_size)
{
  T_SET *set;
  int ele_type;
  int num_element;
  int err_code;

  if ((err_code = t_set_alloc (&set)) < 0)
    {
      return err_code;
    }

  if (!(CCI_IS_COLLECTION_TYPE (u_ext_type)))
    {
      return CCI_ER_TYPE_CONVERSION;
    }

  ele_type = *col_value_p;
  col_value_p++;
  data_size--;
  NET_STR_TO_INT (num_element, col_value_p);
  col_value_p += 4;
  data_size -= 4;

  set->type = ele_type;
  set->num_element = num_element;

  set->data_buf = MALLOC (data_size);
  if (set->data_buf == NULL)
    {
      t_set_free (set);
      return CCI_ER_NO_MORE_MEMORY;
    }
  memcpy (set->data_buf, col_value_p, data_size);
  set->data_size = data_size;
  if ((err_code = t_set_decode (set)) < 0)
    {
      t_set_free (set);
      return err_code;
    }

  *value = set;
  return 0;
}

#if defined (ENABLE_UNUSED_FUNCTION)
static int
get_file_size (char *filename)
{
  struct stat stat_buf;

  if (stat (filename, &stat_buf) >= 0)
    {
      return (stat_buf.st_size);
    }

  return -1;
}
#endif

static int
get_column_info (char *buf_p, int *size, T_CCI_COL_INFO ** ret_col_info, char **next_buf_p, bool is_prepare)
{
  char *cur_p = buf_p;
  int remain_size = *size;
  int num_col_info = 0;
  T_CCI_COL_INFO *col_info;
  int i;

  if (ret_col_info)
    {
      *ret_col_info = NULL;
    }

  if (remain_size < NET_SIZE_INT)
    {
      return CCI_ER_COMMUNICATION;
    }

  if (cur_p == NULL)
    {
      return CCI_ER_COMMUNICATION;
    }

  NET_STR_TO_INT (num_col_info, cur_p);
  if (num_col_info < 0)
    {
      return CCI_ER_COMMUNICATION;
    }

  remain_size -= NET_SIZE_INT;
  cur_p += NET_SIZE_INT;

  col_info = NULL;
  if (num_col_info > 0)
    {
      col_info = (T_CCI_COL_INFO *) MALLOC (sizeof (T_CCI_COL_INFO) * num_col_info);
      if (col_info == NULL)
	{
	  return CCI_ER_NO_MORE_MEMORY;
	}

      memset ((char *) col_info, 0, sizeof (T_CCI_COL_INFO) * num_col_info);

      for (i = 0; i < num_col_info; i++)
	{
	  int name_size;
	  char type, set_type;

	  if (remain_size < NET_SIZE_BYTE)
	    {
	      goto get_column_info_error;
	    }
	  NET_STR_TO_BYTE (type, cur_p);
	  remain_size -= NET_SIZE_BYTE;
	  cur_p += NET_SIZE_BYTE;

	  if (CCI_NET_TYPE_HAS_2BYTES (type))
	    {
	      /* read two bytes for byte */
	      if (remain_size < NET_SIZE_BYTE)
		{
		  goto get_column_info_error;
		}
	      NET_STR_TO_BYTE (set_type, cur_p);
	      col_info[i].ext_type = get_ext_utype_from_net_bytes ((T_CCI_U_TYPE) type, (T_CCI_U_TYPE) set_type);
	      remain_size -= NET_SIZE_BYTE;
	      cur_p += NET_SIZE_BYTE;
	    }
	  else
	    {
	      /* legacy server : one byte */
	      col_info[i].ext_type = type;
	    }

	  if (remain_size < NET_SIZE_SHORT)
	    {
	      goto get_column_info_error;
	    }
	  NET_STR_TO_SHORT (col_info[i].scale, cur_p);
	  remain_size -= NET_SIZE_SHORT;
	  cur_p += NET_SIZE_SHORT;

	  if (remain_size < NET_SIZE_INT)
	    {
	      goto get_column_info_error;
	    }
	  NET_STR_TO_INT (col_info[i].precision, cur_p);
	  remain_size -= NET_SIZE_INT;
	  cur_p += NET_SIZE_INT;

	  if (remain_size < NET_SIZE_INT)
	    {
	      goto get_column_info_error;
	    }
	  NET_STR_TO_INT (name_size, cur_p);
	  remain_size -= NET_SIZE_INT;
	  cur_p += NET_SIZE_INT;

	  if (remain_size < name_size)
	    {
	      goto get_column_info_error;
	    }
	  ALLOC_N_COPY (col_info[i].col_name, cur_p, name_size, char *);
	  if (col_info[i].col_name == NULL)
	    {
	      goto get_column_info_error;
	    }
	  remain_size -= name_size;
	  cur_p += name_size;

	  if (is_prepare == false)
	    {
	      continue;
	    }

	  if (remain_size < NET_SIZE_INT)
	    {
	      goto get_column_info_error;
	    }
	  NET_STR_TO_INT (name_size, cur_p);
	  remain_size -= NET_SIZE_INT;
	  cur_p += NET_SIZE_INT;

	  if (remain_size < name_size)
	    {
	      goto get_column_info_error;
	    }
	  ALLOC_N_COPY (col_info[i].real_attr, cur_p, name_size, char *);
	  remain_size -= name_size;
	  cur_p += name_size;

	  if (remain_size < NET_SIZE_INT)
	    {
	      goto get_column_info_error;
	    }
	  NET_STR_TO_INT (name_size, cur_p);
	  remain_size -= NET_SIZE_INT;
	  cur_p += NET_SIZE_INT;

	  if (remain_size < name_size)
	    {
	      goto get_column_info_error;
	    }
	  ALLOC_N_COPY (col_info[i].class_name, cur_p, name_size, char *);
	  remain_size -= name_size;
	  cur_p += name_size;

	  if (remain_size < NET_SIZE_BYTE)
	    {
	      goto get_column_info_error;
	    }
	  NET_STR_TO_BYTE (col_info[i].is_non_null, cur_p);
	  remain_size -= NET_SIZE_BYTE;
	  cur_p += NET_SIZE_BYTE;

	  if (remain_size < NET_SIZE_INT)
	    {
	      goto get_column_info_error;
	    }
	  NET_STR_TO_INT (name_size, cur_p);
	  remain_size -= NET_SIZE_INT;
	  cur_p += NET_SIZE_INT;

	  if (remain_size < name_size)
	    {
	      goto get_column_info_error;
	    }
	  ALLOC_N_COPY (col_info[i].default_value, cur_p, name_size, char *);
	  if (col_info[i].default_value == NULL)
	    {
	      goto get_column_info_error;
	    }
	  remain_size -= name_size;
	  cur_p += name_size;

	  if (remain_size < NET_SIZE_BYTE)
	    {
	      goto get_column_info_error;
	    }
	  col_info[i].is_auto_increment = *cur_p;
	  remain_size -= NET_SIZE_BYTE;
	  cur_p += NET_SIZE_BYTE;

	  if (remain_size < NET_SIZE_BYTE)
	    {
	      goto get_column_info_error;
	    }
	  NET_STR_TO_BYTE (col_info[i].is_unique_key, cur_p);
	  remain_size -= NET_SIZE_BYTE;
	  cur_p += NET_SIZE_BYTE;

	  if (remain_size < NET_SIZE_BYTE)
	    {
	      goto get_column_info_error;
	    }
	  NET_STR_TO_BYTE (col_info[i].is_primary_key, cur_p);
	  remain_size -= NET_SIZE_BYTE;
	  cur_p += NET_SIZE_BYTE;

	  if (remain_size < NET_SIZE_BYTE)
	    {
	      goto get_column_info_error;
	    }
	  NET_STR_TO_BYTE (col_info[i].is_reverse_index, cur_p);
	  remain_size -= NET_SIZE_BYTE;
	  cur_p += NET_SIZE_BYTE;

	  if (remain_size < NET_SIZE_BYTE)
	    {
	      goto get_column_info_error;
	    }
	  NET_STR_TO_BYTE (col_info[i].is_reverse_unique, cur_p);
	  remain_size -= NET_SIZE_BYTE;
	  cur_p += NET_SIZE_BYTE;

	  if (remain_size < NET_SIZE_BYTE)
	    {
	      goto get_column_info_error;
	    }
	  NET_STR_TO_BYTE (col_info[i].is_foreign_key, cur_p);
	  remain_size -= NET_SIZE_BYTE;
	  cur_p += NET_SIZE_BYTE;

	  if (remain_size < NET_SIZE_BYTE)
	    {
	      goto get_column_info_error;
	    }
	  NET_STR_TO_BYTE (col_info[i].is_shared, cur_p);
	  remain_size -= NET_SIZE_BYTE;
	  cur_p += NET_SIZE_BYTE;
	}
    }

  if (ret_col_info)
    {
      *ret_col_info = col_info;
    }

  if (next_buf_p)
    {
      *next_buf_p = cur_p;
    }

  *size = remain_size;
  return num_col_info;

get_column_info_error:
  if (col_info)
    {
      for (i = 0; i < num_col_info; i++)
	{
	  FREE_MEM (col_info[i].col_name);
	  FREE_MEM (col_info[i].real_attr);
	  FREE_MEM (col_info[i].class_name);
	  FREE_MEM (col_info[i].default_value);
	}
      FREE_MEM (col_info);
    }

  return CCI_ER_COMMUNICATION;
}

static int
oid_get_info_decode (char *buf_p, int remain_size, T_REQ_HANDLE * req_handle, T_CON_HANDLE * con_handle)
{
  int num_col_info;
  int class_name_size;
  int err_code;
  char *class_name;
  char *cur_p = buf_p;
  int size;
  char *next_buf_p;
  T_CCI_COL_INFO *col_info = NULL;

  if (remain_size < NET_SIZE_INT)
    return CCI_ER_COMMUNICATION;
  NET_STR_TO_INT (class_name_size, cur_p);
  remain_size -= NET_SIZE_INT;
  cur_p += NET_SIZE_INT;

  if (remain_size < class_name_size || class_name_size <= 0)
    {
      return CCI_ER_COMMUNICATION;
    }

  class_name = cur_p;
  remain_size -= class_name_size;
  cur_p += class_name_size;

  size = remain_size;

  num_col_info = get_column_info (cur_p, &size, &col_info, &next_buf_p, false);
  if (num_col_info < 0)
    {
      assert (col_info == NULL);
      return num_col_info;
    }

  req_handle->col_info = col_info;
  req_handle->num_col_info = num_col_info;

  remain_size -= CAST_STRLEN (next_buf_p - cur_p);
  err_code =
    fetch_info_decode (next_buf_p, remain_size, num_col_info, &(req_handle->tuple_value), FETCH_OID_GET, req_handle,
		       con_handle);
  if (err_code < 0)
    {
      return err_code;
    }

  req_handle->fetched_tuple_end = req_handle->fetched_tuple_begin = 1;
  req_handle->cur_fetch_tuple_index = 0;
  req_handle->cursor_pos = 1;
  return 0;
}

static int
schema_info_decode (char *buf_p, int size, T_REQ_HANDLE * req_handle)
{
  T_CCI_COL_INFO *col_info = NULL;
  int remain_size = size;
  char *cur_p = buf_p;
  int num_tuple;
  int num_col_info;

  if (remain_size < 4)
    {
      return CCI_ER_COMMUNICATION;
    }

  NET_STR_TO_INT (num_tuple, cur_p);
  remain_size -= 4;
  cur_p += 4;

  size = remain_size;
  num_col_info = get_column_info (cur_p, &size, &col_info, NULL, false);
  if (num_col_info < 0)
    {
      assert (col_info == NULL);
      return num_col_info;
    }

  req_handle->num_col_info = num_col_info;
  req_handle->col_info = col_info;
  req_handle->num_tuple = num_tuple;

  return 0;
}

static int
col_get_info_decode (char *buf_p, int remain_size, int *col_size, int *col_type, T_REQ_HANDLE * req_handle,
		     T_CON_HANDLE * con_handle)
{
  int num_col_info;
  char *cur_p = buf_p;
  char *next_buf_p;
  T_CCI_COL_INFO *col_info = NULL;
  int num_tuple;
  int size;

  if (remain_size < 1)
    return CCI_ER_COMMUNICATION;
  if (col_type)
    {
      *col_type = *cur_p;
    }
  remain_size -= 1;
  cur_p += 1;

  size = remain_size;
  num_col_info = get_column_info (cur_p, &size, &col_info, &next_buf_p, false);
  if (num_col_info < 0)
    {
      assert (col_info == NULL);
      return num_col_info;
    }

  req_handle->col_info = col_info;
  req_handle->num_col_info = num_col_info;

  remain_size -= CAST_STRLEN (next_buf_p - cur_p);
  cur_p = next_buf_p;

  num_tuple =
    fetch_info_decode (cur_p, remain_size, 1, &(req_handle->tuple_value), FETCH_COL_GET, req_handle, con_handle);
  if (num_tuple < 0)
    return num_tuple;

  if (col_size)
    {
      *col_size = num_tuple;
    }
  req_handle->num_tuple = num_tuple;
  if (num_tuple == 0)
    {
      req_handle->fetched_tuple_begin = 0;
      req_handle->fetched_tuple_end = 0;
      req_handle->cur_fetch_tuple_index = -1;
    }
  else
    {
      req_handle->fetched_tuple_begin = 1;
      req_handle->fetched_tuple_end = num_tuple;
      req_handle->cur_fetch_tuple_index = 0;
    }

  return 0;
}

static int
next_result_info_decode (char *buf, int size, T_REQ_HANDLE * req_handle)
{
  int remain_size = size;
  char *cur_p = buf;
  int result_count;
  int num_col_info;
  char stmt_type;
  char updatable_flag;
  T_CCI_COL_INFO *col_info = NULL;

  if (remain_size < 4)
    {
      return CCI_ER_COMMUNICATION;
    }

  NET_STR_TO_INT (result_count, cur_p);
  remain_size -= 4;
  cur_p += 4;

  if (remain_size < 1)
    {
      return CCI_ER_COMMUNICATION;
    }

  stmt_type = *cur_p;
  remain_size -= 1;
  cur_p += 1;

  if (remain_size < 1)
    {
      return CCI_ER_COMMUNICATION;
    }

  updatable_flag = *cur_p;
  remain_size -= 1;
  cur_p += 1;

  size = remain_size;
  num_col_info = get_column_info (cur_p, &size, &col_info, NULL, true);
  if (num_col_info < 0)
    {
      assert (col_info == NULL);
      return num_col_info;
    }

  req_handle_col_info_free (req_handle);

  req_handle->num_tuple = result_count;
  req_handle->num_col_info = num_col_info;
  req_handle->col_info = col_info;
  req_handle->stmt_type = (T_CCI_CUBRID_STMT) stmt_type;
  req_handle->updatable_flag = updatable_flag;

  return result_count;
}

static int
bind_value_conversion (T_CCI_A_TYPE a_type, T_CCI_U_TYPE u_type, char flag, void *value, int length,
		       T_BIND_VALUE * bind_value)
{
  int err_code;

  assert (u_type >= CCI_U_TYPE_FIRST && u_type <= CCI_U_TYPE_LAST);

  if (a_type == CCI_A_TYPE_STR)
    {
      switch (u_type)
	{
	case CCI_U_TYPE_CHAR:
	case CCI_U_TYPE_STRING:
	case CCI_U_TYPE_NCHAR:
	case CCI_U_TYPE_VARNCHAR:
	case CCI_U_TYPE_NUMERIC:
	case CCI_U_TYPE_ENUM:
	case CCI_U_TYPE_JSON:
	  if (length == UNMEASURED_LENGTH)
	    {
	      bind_value->size = (int) strlen ((const char *) value);
	    }
	  else
	    {
	      bind_value->size = length;
	    }

	  if (flag == CCI_BIND_PTR)
	    {
	      bind_value->flag = BIND_PTR_STATIC;
	      bind_value->value = value;
	    }
	  else
	    {
	      bind_value->flag = BIND_PTR_DYNAMIC;
	      ALLOC_COPY_BIT (bind_value->value, value, bind_value->size);
	      if (bind_value->value == NULL)
		{
		  return CCI_ER_NO_MORE_MEMORY;
		}
	    }

	  bind_value->size += 1;	/* protocol with cas */
	  break;
	case CCI_U_TYPE_BIGINT:
	  {
	    INT64 bi_val;

	    err_code = ut_str_to_bigint ((char *) value, &bi_val);
	    if (err_code < 0)
	      {
		return err_code;
	      }
	    ALLOC_COPY_BIGINT (bind_value->value, bi_val);
	    if (bind_value->value == NULL)
	      {
		return CCI_ER_NO_MORE_MEMORY;
	      }
	    bind_value->size = sizeof (INT64);
	    bind_value->flag = BIND_PTR_DYNAMIC;
	  }
	  break;
	case CCI_U_TYPE_UBIGINT:
	  {
	    UINT64 ubi_val;

	    err_code = ut_str_to_ubigint ((char *) value, &ubi_val);
	    if (err_code < 0)
	      {
		return err_code;
	      }
	    ALLOC_COPY_UBIGINT (bind_value->value, ubi_val);
	    if (bind_value->value == NULL)
	      {
		return CCI_ER_NO_MORE_MEMORY;
	      }
	    bind_value->size = sizeof (UINT64);
	    bind_value->flag = BIND_PTR_DYNAMIC;
	  }
	  break;
	case CCI_U_TYPE_INT:
	case CCI_U_TYPE_SHORT:
	  {
	    int i_val;

	    err_code = ut_str_to_int ((char *) value, &i_val);
	    if (err_code < 0)
	      {
		return err_code;
	      }
	    ALLOC_COPY_INT (bind_value->value, i_val);
	    if (bind_value->value == NULL)
	      {
		return CCI_ER_NO_MORE_MEMORY;
	      }
	    bind_value->size = sizeof (int);
	    bind_value->flag = BIND_PTR_DYNAMIC;
	  }
	  break;
	case CCI_U_TYPE_UINT:
	case CCI_U_TYPE_USHORT:
	  {
	    unsigned int ui_val;

	    err_code = ut_str_to_uint ((char *) value, &ui_val);
	    if (err_code < 0)
	      {
		return err_code;
	      }
	    ALLOC_COPY_UINT (bind_value->value, ui_val);
	    if (bind_value->value == NULL)
	      {
		return CCI_ER_NO_MORE_MEMORY;
	      }
	    bind_value->size = sizeof (unsigned int);
	    bind_value->flag = BIND_PTR_DYNAMIC;
	  }
	  break;
	case CCI_U_TYPE_FLOAT:
	  {
	    float f_val;

	    err_code = ut_str_to_float ((char *) value, &f_val);
	    if (err_code < 0)
	      {
		return err_code;
	      }
	    ALLOC_COPY_FLOAT (bind_value->value, f_val);
	    if (bind_value->value == NULL)
	      {
		return CCI_ER_NO_MORE_MEMORY;
	      }
	    bind_value->size = sizeof (float);
	    bind_value->flag = BIND_PTR_DYNAMIC;
	  }
	  break;
	case CCI_U_TYPE_MONETARY:
	case CCI_U_TYPE_DOUBLE:
	  {
	    double d_val;

	    err_code = ut_str_to_double ((char *) value, &d_val);
	    if (err_code < 0)
	      {
		return err_code;
	      }
	    ALLOC_COPY_DOUBLE (bind_value->value, d_val);
	    if (bind_value->value == NULL)
	      {
		return CCI_ER_NO_MORE_MEMORY;
	      }
	    bind_value->size = sizeof (double);
	    bind_value->flag = BIND_PTR_DYNAMIC;
	  }
	  break;
	case CCI_U_TYPE_DATE:
	  {
	    T_CCI_DATE date = { 0, 0, 0, 0, 0, 0, 0 };

	    err_code = ut_str_to_date ((char *) value, &date);
	    if (err_code < 0)
	      return err_code;
	    ALLOC_COPY_DATE (bind_value->value, date);
	    if (bind_value->value == NULL)
	      return CCI_ER_NO_MORE_MEMORY;
	    bind_value->size = sizeof (T_CCI_DATE);
	    bind_value->flag = BIND_PTR_DYNAMIC;
	  }
	  break;
	case CCI_U_TYPE_TIME:
	  {
	    T_CCI_DATE date = { 0, 0, 0, 0, 0, 0, 0 };

	    err_code = ut_str_to_time ((char *) value, &date);
	    if (err_code < 0)
	      return err_code;
	    ALLOC_COPY_DATE (bind_value->value, date);
	    if (bind_value->value == NULL)
	      return CCI_ER_NO_MORE_MEMORY;
	    bind_value->size = sizeof (T_CCI_DATE);
	    bind_value->flag = BIND_PTR_DYNAMIC;
	  }
	  break;
	case CCI_U_TYPE_TIMESTAMP:
	  {
	    T_CCI_DATE date = { 0, 0, 0, 0, 0, 0, 0 };

	    err_code = ut_str_to_timestamp ((char *) value, &date);
	    if (err_code < 0)
	      {
		return err_code;
	      }
	    ALLOC_COPY_DATE (bind_value->value, date);
	    if (bind_value->value == NULL)
	      {
		return CCI_ER_NO_MORE_MEMORY;
	      }
	    bind_value->size = sizeof (T_CCI_DATE);
	    bind_value->flag = BIND_PTR_DYNAMIC;
	  }
	  break;
	case CCI_U_TYPE_TIMESTAMPTZ:
	case CCI_U_TYPE_TIMESTAMPLTZ:
	  {
	    T_CCI_DATE_TZ date_tz = { 0, 0, 0, 0, 0, 0, 0, "" };

	    err_code = ut_str_to_timestamptz ((char *) value, &date_tz);
	    if (err_code < 0)
	      {
		return err_code;
	      }

	    ALLOC_COPY_DATE_TZ (bind_value->value, date_tz);
	    if (bind_value->value == NULL)
	      {
		return CCI_ER_NO_MORE_MEMORY;
	      }
	    bind_value->size = (int) (sizeof (T_CCI_DATE) + strlen (date_tz.tz));
	    bind_value->flag = BIND_PTR_DYNAMIC;
	  }
	  break;
	case CCI_U_TYPE_DATETIME:
	  {
	    T_CCI_DATE date = { 0, 0, 0, 0, 0, 0, 0 };

	    err_code = ut_str_to_datetime ((char *) value, &date);
	    if (err_code < 0)
	      {
		return err_code;
	      }
	    ALLOC_COPY_DATE (bind_value->value, date);
	    if (bind_value->value == NULL)
	      {
		return CCI_ER_NO_MORE_MEMORY;
	      }
	    bind_value->size = sizeof (T_CCI_DATE);
	    bind_value->flag = BIND_PTR_DYNAMIC;
	  }
	  break;
	case CCI_U_TYPE_DATETIMETZ:
	case CCI_U_TYPE_DATETIMELTZ:
	  {
	    T_CCI_DATE_TZ date_tz = { 0, 0, 0, 0, 0, 0, 0, "" };

	    err_code = ut_str_to_datetimetz ((char *) value, &date_tz);
	    if (err_code < 0)
	      {
		return err_code;
	      }

	    ALLOC_COPY_DATE_TZ (bind_value->value, date_tz);
	    if (bind_value->value == NULL)
	      {
		return CCI_ER_NO_MORE_MEMORY;
	      }
	    bind_value->size = (int) (sizeof (T_CCI_DATE) + strlen (date_tz.tz));
	    bind_value->flag = BIND_PTR_DYNAMIC;
	  }
	  break;
	case CCI_U_TYPE_OBJECT:
	  {
	    T_OBJECT obj;

	    err_code = ut_str_to_oid ((char *) value, &obj);
	    if (err_code < 0)
	      {
		return err_code;
	      }
	    ALLOC_COPY_OBJECT (bind_value->value, obj);
	    if (bind_value->value == NULL)
	      {
		return CCI_ER_NO_MORE_MEMORY;
	      }
	    bind_value->flag = BIND_PTR_DYNAMIC;
	  }
	  break;
	case CCI_U_TYPE_SET:
	case CCI_U_TYPE_MULTISET:
	case CCI_U_TYPE_SEQUENCE:
	  /* NOT IMPLEMENTED */
	default:
	  return CCI_ER_TYPE_CONVERSION;
	}
    }
  else if (a_type == CCI_A_TYPE_INT)
    {
      int i_value;

      memcpy ((char *) &i_value, value, sizeof (int));

      switch (u_type)
	{
	case CCI_U_TYPE_CHAR:
	case CCI_U_TYPE_STRING:
	case CCI_U_TYPE_NCHAR:
	case CCI_U_TYPE_VARNCHAR:
	case CCI_U_TYPE_NUMERIC:
	case CCI_U_TYPE_ENUM:
	  {
	    char buf[64];

	    ut_int_to_str (i_value, buf, 64);
	    ALLOC_COPY (bind_value->value, buf);
	    if (bind_value->value == NULL)
	      {
		return CCI_ER_NO_MORE_MEMORY;
	      }
	    bind_value->size = (int) strlen (buf) + 1;
	  }
	  break;
	case CCI_U_TYPE_BIGINT:
	  ALLOC_COPY_BIGINT (bind_value->value, (INT64) i_value);
	  if (bind_value->value == NULL)
	    {
	      return CCI_ER_NO_MORE_MEMORY;
	    }
	  break;
	case CCI_U_TYPE_UBIGINT:
	  ALLOC_COPY_UBIGINT (bind_value->value, (UINT64) i_value);
	  if (bind_value->value == NULL)
	    {
	      return CCI_ER_NO_MORE_MEMORY;
	    }
	  break;
	case CCI_U_TYPE_INT:
	case CCI_U_TYPE_SHORT:
	  ALLOC_COPY_INT (bind_value->value, i_value);
	  if (bind_value->value == NULL)
	    {
	      return CCI_ER_NO_MORE_MEMORY;
	    }
	  break;
	case CCI_U_TYPE_UINT:
	case CCI_U_TYPE_USHORT:
	  ALLOC_COPY_UINT (bind_value->value, (unsigned int) i_value);
	  if (bind_value->value == NULL)
	    {
	      return CCI_ER_NO_MORE_MEMORY;
	    }
	  break;
	case CCI_U_TYPE_MONETARY:
	case CCI_U_TYPE_DOUBLE:
	  ALLOC_COPY_DOUBLE (bind_value->value, (double) i_value);
	  if (bind_value->value == NULL)
	    {
	      return CCI_ER_NO_MORE_MEMORY;
	    }
	  break;
	case CCI_U_TYPE_FLOAT:
	  ALLOC_COPY_FLOAT (bind_value->value, (float) i_value);
	  if (bind_value->value == NULL)
	    {
	      return CCI_ER_NO_MORE_MEMORY;
	    }
	  break;
	default:
	  return CCI_ER_TYPE_CONVERSION;
	}
      bind_value->flag = BIND_PTR_DYNAMIC;
    }

  else if (a_type == CCI_A_TYPE_UINT)
    {
      unsigned int ui_value;

      memcpy ((char *) &ui_value, value, sizeof (unsigned int));

      switch (u_type)
	{
	case CCI_U_TYPE_CHAR:
	case CCI_U_TYPE_STRING:
	case CCI_U_TYPE_NCHAR:
	case CCI_U_TYPE_VARNCHAR:
	case CCI_U_TYPE_NUMERIC:
	case CCI_U_TYPE_ENUM:
	  {
	    char buf[64];

	    ut_uint_to_str ((unsigned int) ui_value, buf, 64);
	    ALLOC_COPY (bind_value->value, buf);
	    if (bind_value->value == NULL)
	      {
		return CCI_ER_NO_MORE_MEMORY;
	      }
	    bind_value->size = (int) strlen (buf) + 1;
	  }
	  break;
	case CCI_U_TYPE_BIGINT:
	  ALLOC_COPY_BIGINT (bind_value->value, (INT64) ui_value);
	  if (bind_value->value == NULL)
	    {
	      return CCI_ER_NO_MORE_MEMORY;
	    }
	  break;
	case CCI_U_TYPE_UBIGINT:
	  ALLOC_COPY_UBIGINT (bind_value->value, (UINT64) ui_value);
	  if (bind_value->value == NULL)
	    {
	      return CCI_ER_NO_MORE_MEMORY;
	    }
	  break;
	case CCI_U_TYPE_INT:
	case CCI_U_TYPE_SHORT:
	  ALLOC_COPY_INT (bind_value->value, (int) ui_value);
	  if (bind_value->value == NULL)
	    {
	      return CCI_ER_NO_MORE_MEMORY;
	    }
	  break;
	case CCI_U_TYPE_UINT:
	case CCI_U_TYPE_USHORT:
	  ALLOC_COPY_UINT (bind_value->value, ui_value);
	  if (bind_value->value == NULL)
	    {
	      return CCI_ER_NO_MORE_MEMORY;
	    }
	  break;
	case CCI_U_TYPE_MONETARY:
	case CCI_U_TYPE_DOUBLE:
	  ALLOC_COPY_DOUBLE (bind_value->value, (double) ui_value);
	  if (bind_value->value == NULL)
	    {
	      return CCI_ER_NO_MORE_MEMORY;
	    }
	  break;
	case CCI_U_TYPE_FLOAT:
	  ALLOC_COPY_FLOAT (bind_value->value, (float) ui_value);
	  if (bind_value->value == NULL)
	    return CCI_ER_NO_MORE_MEMORY;
	  break;
	default:
	  return CCI_ER_TYPE_CONVERSION;
	}
      bind_value->flag = BIND_PTR_DYNAMIC;
    }
  else if (a_type == CCI_A_TYPE_BIGINT)
    {
      INT64 bi_value;

      memcpy ((char *) &bi_value, value, sizeof (INT64));

      switch (u_type)
	{
	case CCI_U_TYPE_CHAR:
	case CCI_U_TYPE_STRING:
	case CCI_U_TYPE_NCHAR:
	case CCI_U_TYPE_VARNCHAR:
	case CCI_U_TYPE_NUMERIC:
	case CCI_U_TYPE_ENUM:
	  {
	    char buf[64];

	    ut_int_to_str (bi_value, buf, 64);
	    ALLOC_COPY (bind_value->value, buf);
	    if (bind_value->value == NULL)
	      {
		return CCI_ER_NO_MORE_MEMORY;
	      }
	    bind_value->size = (int) strlen (buf) + 1;
	  }
	  break;
	case CCI_U_TYPE_BIGINT:
	  ALLOC_COPY_BIGINT (bind_value->value, bi_value);
	  if (bind_value->value == NULL)
	    {
	      return CCI_ER_NO_MORE_MEMORY;
	    }
	  break;
	case CCI_U_TYPE_UBIGINT:
	  ALLOC_COPY_UBIGINT (bind_value->value, (UINT64) bi_value);
	  if (bind_value->value == NULL)
	    {
	      return CCI_ER_NO_MORE_MEMORY;
	    }
	  break;
	case CCI_U_TYPE_INT:
	case CCI_U_TYPE_SHORT:
	  ALLOC_COPY_INT (bind_value->value, (int) bi_value);
	  if (bind_value->value == NULL)
	    {
	      return CCI_ER_NO_MORE_MEMORY;
	    }
	  break;
	case CCI_U_TYPE_UINT:
	case CCI_U_TYPE_USHORT:
	  ALLOC_COPY_UINT (bind_value->value, (unsigned int) bi_value);
	  if (bind_value->value == NULL)
	    {
	      return CCI_ER_NO_MORE_MEMORY;
	    }
	  break;
	case CCI_U_TYPE_MONETARY:
	case CCI_U_TYPE_DOUBLE:
	  ALLOC_COPY_DOUBLE (bind_value->value, (double) bi_value);
	  if (bind_value->value == NULL)
	    return CCI_ER_NO_MORE_MEMORY;
	  break;
	case CCI_U_TYPE_FLOAT:
	  ALLOC_COPY_FLOAT (bind_value->value, (float) bi_value);
	  if (bind_value->value == NULL)
	    return CCI_ER_NO_MORE_MEMORY;
	  break;
	default:
	  return CCI_ER_TYPE_CONVERSION;
	}
      bind_value->flag = BIND_PTR_DYNAMIC;
    }

  else if (a_type == CCI_A_TYPE_UBIGINT)
    {
      UINT64 ubi_value;

      memcpy ((char *) &ubi_value, value, sizeof (UINT64));

      switch (u_type)
	{
	case CCI_U_TYPE_CHAR:
	case CCI_U_TYPE_STRING:
	case CCI_U_TYPE_NCHAR:
	case CCI_U_TYPE_VARNCHAR:
	case CCI_U_TYPE_NUMERIC:
	case CCI_U_TYPE_ENUM:
	  {
	    char buf[64];

	    ut_uint_to_str ((UINT64) ubi_value, buf, 64);
	    ALLOC_COPY (bind_value->value, buf);
	    if (bind_value->value == NULL)
	      {
		return CCI_ER_NO_MORE_MEMORY;
	      }
	    bind_value->size = (int) strlen (buf) + 1;
	  }
	  break;
	case CCI_U_TYPE_BIGINT:
	  ALLOC_COPY_BIGINT (bind_value->value, (INT64) ubi_value);
	  if (bind_value->value == NULL)
	    {
	      return CCI_ER_NO_MORE_MEMORY;
	    }
	  break;
	case CCI_U_TYPE_UBIGINT:
	  ALLOC_COPY_UBIGINT (bind_value->value, ubi_value);
	  if (bind_value->value == NULL)
	    {
	      return CCI_ER_NO_MORE_MEMORY;
	    }
	  break;
	case CCI_U_TYPE_INT:
	case CCI_U_TYPE_SHORT:
	  ALLOC_COPY_INT (bind_value->value, (int) ubi_value);
	  if (bind_value->value == NULL)
	    {
	      return CCI_ER_NO_MORE_MEMORY;
	    }
	  break;
	case CCI_U_TYPE_UINT:
	case CCI_U_TYPE_USHORT:
	  ALLOC_COPY_UINT (bind_value->value, (unsigned int) ubi_value);
	  if (bind_value->value == NULL)
	    {
	      return CCI_ER_NO_MORE_MEMORY;
	    }
	  break;
	case CCI_U_TYPE_MONETARY:
	case CCI_U_TYPE_DOUBLE:
	  ALLOC_COPY_DOUBLE (bind_value->value, (double) ubi_value);
	  if (bind_value->value == NULL)
	    {
	      return CCI_ER_NO_MORE_MEMORY;
	    }
	  break;
	case CCI_U_TYPE_FLOAT:
	  ALLOC_COPY_FLOAT (bind_value->value, (float) ubi_value);
	  if (bind_value->value == NULL)
	    {
	      return CCI_ER_NO_MORE_MEMORY;
	    }
	  break;
	default:
	  return CCI_ER_TYPE_CONVERSION;
	}
      bind_value->flag = BIND_PTR_DYNAMIC;
    }
  else if (a_type == CCI_A_TYPE_FLOAT)
    {
      float f_value;

      memcpy ((char *) &f_value, value, sizeof (float));

      switch (u_type)
	{
	case CCI_U_TYPE_CHAR:
	case CCI_U_TYPE_STRING:
	case CCI_U_TYPE_NCHAR:
	case CCI_U_TYPE_VARNCHAR:
	case CCI_U_TYPE_NUMERIC:
	case CCI_U_TYPE_ENUM:
	  {
	    char buf[256];
	    ut_float_to_str (f_value, buf, 256);
	    ALLOC_COPY (bind_value->value, buf);
	    if (bind_value->value == NULL)
	      return CCI_ER_NO_MORE_MEMORY;
	    bind_value->size = (int) strlen (buf) + 1;
	  }
	  break;
	case CCI_U_TYPE_BIGINT:
	  ALLOC_COPY_BIGINT (bind_value->value, (INT64) f_value);
	  if (bind_value->value == NULL)
	    {
	      return CCI_ER_NO_MORE_MEMORY;
	    }
	  break;
	case CCI_U_TYPE_UBIGINT:
	  ALLOC_COPY_UBIGINT (bind_value->value, (UINT64) f_value);
	  if (bind_value->value == NULL)
	    {
	      return CCI_ER_NO_MORE_MEMORY;
	    }
	  break;
	case CCI_U_TYPE_INT:
	case CCI_U_TYPE_SHORT:
	  ALLOC_COPY_INT (bind_value->value, (int) f_value);
	  if (bind_value->value == NULL)
	    {
	      return CCI_ER_NO_MORE_MEMORY;
	    }
	  break;
	case CCI_U_TYPE_UINT:
	case CCI_U_TYPE_USHORT:
	  ALLOC_COPY_UINT (bind_value->value, (unsigned int) f_value);
	  if (bind_value->value == NULL)
	    {
	      return CCI_ER_NO_MORE_MEMORY;
	    }
	  break;
	case CCI_U_TYPE_MONETARY:
	case CCI_U_TYPE_DOUBLE:
	  ALLOC_COPY_DOUBLE (bind_value->value, (double) f_value);
	  if (bind_value->value == NULL)
	    return CCI_ER_NO_MORE_MEMORY;
	  break;
	case CCI_U_TYPE_FLOAT:
	  ALLOC_COPY_FLOAT (bind_value->value, f_value);
	  if (bind_value->value == NULL)
	    return CCI_ER_NO_MORE_MEMORY;
	  break;
	default:
	  return CCI_ER_TYPE_CONVERSION;
	}
      bind_value->flag = BIND_PTR_DYNAMIC;
    }
  else if (a_type == CCI_A_TYPE_DOUBLE)
    {
      double d_value;
      memcpy ((char *) &d_value, value, sizeof (double));
      switch (u_type)
	{
	case CCI_U_TYPE_CHAR:
	case CCI_U_TYPE_STRING:
	case CCI_U_TYPE_NCHAR:
	case CCI_U_TYPE_VARNCHAR:
	case CCI_U_TYPE_NUMERIC:
	case CCI_U_TYPE_ENUM:
	  {
	    char buf[512];
	    ut_double_to_str (d_value, buf, 512);
	    ALLOC_COPY (bind_value->value, buf);
	    if (bind_value->value == NULL)
	      return CCI_ER_NO_MORE_MEMORY;
	    bind_value->size = (int) strlen (buf) + 1;
	  }
	  break;
	case CCI_U_TYPE_BIGINT:
	  ALLOC_COPY_BIGINT (bind_value->value, (INT64) d_value);
	  if (bind_value->value == NULL)
	    {
	      return CCI_ER_NO_MORE_MEMORY;
	    }
	  break;
	case CCI_U_TYPE_UBIGINT:
	  ALLOC_COPY_UBIGINT (bind_value->value, (UINT64) d_value);
	  if (bind_value->value == NULL)
	    {
	      return CCI_ER_NO_MORE_MEMORY;
	    }
	  break;
	case CCI_U_TYPE_INT:
	case CCI_U_TYPE_SHORT:
	  ALLOC_COPY_INT (bind_value->value, (int) d_value);
	  if (bind_value->value == NULL)
	    {
	      return CCI_ER_NO_MORE_MEMORY;
	    }
	  break;
	case CCI_U_TYPE_UINT:
	case CCI_U_TYPE_USHORT:
	  ALLOC_COPY_UINT (bind_value->value, (unsigned int) d_value);
	  if (bind_value->value == NULL)
	    {
	      return CCI_ER_NO_MORE_MEMORY;
	    }
	  break;
	case CCI_U_TYPE_MONETARY:
	case CCI_U_TYPE_DOUBLE:
	  ALLOC_COPY_DOUBLE (bind_value->value, d_value);
	  if (bind_value->value == NULL)
	    return CCI_ER_NO_MORE_MEMORY;
	  break;
	case CCI_U_TYPE_FLOAT:
	  ALLOC_COPY_FLOAT (bind_value->value, (float) d_value);
	  if (bind_value->value == NULL)
	    return CCI_ER_NO_MORE_MEMORY;
	  break;
	default:
	  return CCI_ER_TYPE_CONVERSION;
	}
      bind_value->flag = BIND_PTR_DYNAMIC;
    }
  else if (a_type == CCI_A_TYPE_BIT)
    {
      switch (u_type)
	{
	case CCI_U_TYPE_BIT:
	case CCI_U_TYPE_VARBIT:
	  {
	    T_CCI_BIT *bit_value = (T_CCI_BIT *) value;

	    if (flag == CCI_BIND_PTR)
	      {
		bind_value->value = bit_value->buf;
		bind_value->flag = BIND_PTR_STATIC;
	      }
	    else
	      {
		ALLOC_COPY_BIT (bind_value->value, bit_value->buf, bit_value->size);
		if (bind_value->value == NULL)
		  return CCI_ER_NO_MORE_MEMORY;
		bind_value->flag = BIND_PTR_DYNAMIC;
	      }
	    bind_value->size = bit_value->size;
	  }
	  break;
	default:
	  return CCI_ER_TYPE_CONVERSION;
	}
    }
  else if (a_type == CCI_A_TYPE_SET)
    {
      switch (u_type)
	{
	case CCI_U_TYPE_SET:
	case CCI_U_TYPE_MULTISET:
	case CCI_U_TYPE_SEQUENCE:
	  {
	    T_SET *set;
	    set = (T_SET *) value;
	    bind_value->value = MALLOC (set->data_size + 1);
	    if (bind_value->value == NULL)
	      {
		return CCI_ER_NO_MORE_MEMORY;
	      }
	    *((char *) (bind_value->value)) = set->type;
	    memcpy ((char *) (bind_value->value) + 1, set->data_buf, set->data_size);
	    bind_value->size = set->data_size + 1;
	    bind_value->flag = BIND_PTR_DYNAMIC;
	  }
	  break;
	default:
	  return CCI_ER_TYPE_CONVERSION;
	}
    }
  else if (a_type == CCI_A_TYPE_DATE)
    {
      switch (u_type)
	{
	case CCI_U_TYPE_DATE:
	case CCI_U_TYPE_TIME:
	case CCI_U_TYPE_TIMESTAMP:
	case CCI_U_TYPE_DATETIME:
	  {
	    ALLOC_COPY_DATE (bind_value->value, *((T_CCI_DATE *) value));
	    if (bind_value->value == NULL)
	      return CCI_ER_NO_MORE_MEMORY;
	    bind_value->size = sizeof (T_CCI_DATE);
	    bind_value->flag = BIND_PTR_DYNAMIC;
	  }
	  break;
	default:
	  return CCI_ER_TYPE_CONVERSION;
	}
    }
  else if (a_type == CCI_A_TYPE_DATE_TZ)
    {
      switch (u_type)
	{
	case CCI_U_TYPE_TIMESTAMPTZ:
	case CCI_U_TYPE_TIMESTAMPLTZ:
	case CCI_U_TYPE_DATETIMETZ:
	case CCI_U_TYPE_DATETIMELTZ:
	  {
	    ALLOC_COPY_DATE_TZ (bind_value->value, *((T_CCI_DATE_TZ *) value));
	    if (bind_value->value == NULL)
	      {
		return CCI_ER_NO_MORE_MEMORY;
	      }
	    bind_value->size = (int) (sizeof (T_CCI_DATE) + NET_SIZE_TZ (value));
	    bind_value->flag = BIND_PTR_DYNAMIC;
	  }
	  break;
	default:
	  return CCI_ER_TYPE_CONVERSION;
	}
    }
  else if (a_type == CCI_A_TYPE_BLOB || a_type == CCI_A_TYPE_CLOB)
    {
      switch (u_type)
	{
	case CCI_U_TYPE_BLOB:
	case CCI_U_TYPE_CLOB:
	  {
	    ALLOC_COPY_BIT (bind_value->value, (T_LOB *) value, sizeof (T_LOB));
	    bind_value->size = sizeof (T_LOB);
	    bind_value->flag = BIND_PTR_DYNAMIC;
	  }
	  break;
	default:
	  return CCI_ER_TYPE_CONVERSION;
	}
    }
  else
    {
      return CCI_ER_ATYPE;
    }

  bind_value->u_type = u_type;
  if (u_type == CCI_U_TYPE_SHORT)
    {
      bind_value->u_type = CCI_U_TYPE_INT;
    }
  else if (u_type == CCI_U_TYPE_USHORT)
    {
      bind_value->u_type = CCI_U_TYPE_UINT;
    }

  switch (u_type)
    {
    case CCI_U_TYPE_SHORT:
    case CCI_U_TYPE_INT:
    case CCI_U_TYPE_USHORT:
    case CCI_U_TYPE_UINT:
      bind_value->size = NET_SIZE_INT;
      break;
    case CCI_U_TYPE_BIGINT:
    case CCI_U_TYPE_UBIGINT:
      bind_value->size = NET_SIZE_BIGINT;
      break;
    case CCI_U_TYPE_MONETARY:
    case CCI_U_TYPE_DOUBLE:
      bind_value->size = NET_SIZE_DOUBLE;
      break;
    case CCI_U_TYPE_FLOAT:
      bind_value->size = NET_SIZE_FLOAT;
      break;
    case CCI_U_TYPE_DATE:
      bind_value->size = NET_SIZE_DATE;
      break;
    case CCI_U_TYPE_TIME:
      bind_value->size = NET_SIZE_TIME;
      break;
    case CCI_U_TYPE_TIMESTAMP:
      bind_value->size = NET_SIZE_TIMESTAMP;
      break;
    case CCI_U_TYPE_DATETIME:
      bind_value->size = NET_SIZE_DATETIME;
      break;
    case CCI_U_TYPE_OBJECT:
      bind_value->size = NET_SIZE_OBJECT;
      break;
    case CCI_U_TYPE_TIMESTAMPTZ:
    case CCI_U_TYPE_TIMESTAMPLTZ:
      bind_value->size = NET_SIZE_TIMESTAMP + NET_SIZE_TZ (bind_value->value);
      break;
    case CCI_U_TYPE_DATETIMETZ:
    case CCI_U_TYPE_DATETIMELTZ:
      bind_value->size = NET_SIZE_DATETIME + NET_SIZE_TZ (bind_value->value);
      break;
    default:
      break;
    }

  return 0;
}

static int
bind_value_to_net_buf (T_NET_BUF * net_buf, T_CCI_U_TYPE u_type, void *value, int size, char *charset,
		       bool set_default_value)
{
  if (u_type < CCI_U_TYPE_FIRST || u_type > CCI_U_TYPE_LAST)
    {
      u_type = CCI_U_TYPE_NULL;
    }

  if (u_type != CCI_U_TYPE_NULL && value == NULL && set_default_value == false)
    {
      assert (false);
      u_type = CCI_U_TYPE_NULL;
      ADD_ARG_BYTES (net_buf, &u_type, 1);
      ADD_ARG_BYTES (net_buf, NULL, 0);
      return 0;
    }

  ADD_ARG_BYTES (net_buf, &u_type, 1);

  switch (u_type)
    {
    case CCI_U_TYPE_CHAR:
    case CCI_U_TYPE_STRING:
    case CCI_U_TYPE_NCHAR:
    case CCI_U_TYPE_VARNCHAR:
    case CCI_U_TYPE_ENUM:
    case CCI_U_TYPE_JSON:
      if (value == NULL)
	{
	  ADD_ARG_BIND_STR (net_buf, "", 1, charset);
	}
      else
	{
	  ADD_ARG_BIND_STR (net_buf, (char *) value, size, charset);
	}
      break;
    case CCI_U_TYPE_NUMERIC:
      if (value == NULL)
	{
	  ADD_ARG_BYTES (net_buf, NULL, 0);
	}
      else
	{
	  ADD_ARG_BIND_STR (net_buf, (char *) value, size, charset);
	}
      break;
    case CCI_U_TYPE_SET:
    case CCI_U_TYPE_MULTISET:
    case CCI_U_TYPE_SEQUENCE:
      if (value == NULL)
	{
	  ADD_ARG_BYTES (net_buf, NULL, 0);
	}
      else
	{
	  ADD_ARG_BYTES (net_buf, value, size);
	}
      break;
    case CCI_U_TYPE_BIT:
    case CCI_U_TYPE_VARBIT:
      if (value == NULL)
	{
	  char empty_byte = 0;
	  ADD_ARG_BYTES (net_buf, &empty_byte, 1);
	}
      else
	{
	  ADD_ARG_BYTES (net_buf, value, size);
	}
      break;
    case CCI_U_TYPE_BIGINT:
    case CCI_U_TYPE_UBIGINT:
      if (value == NULL)
	{
	  ADD_ARG_BIGINT (net_buf, 0LL);
	}
      else
	{
	  ADD_ARG_BIGINT (net_buf, *((INT64 *) value));
	}
      break;
    case CCI_U_TYPE_INT:
    case CCI_U_TYPE_SHORT:
    case CCI_U_TYPE_UINT:
    case CCI_U_TYPE_USHORT:
      if (value == NULL)
	{
	  ADD_ARG_INT (net_buf, 0);
	}
      else
	{
	  ADD_ARG_INT (net_buf, *((int *) value));
	}
      break;
    case CCI_U_TYPE_FLOAT:
      if (value == NULL)
	{
	  ADD_ARG_FLOAT (net_buf, 0.0f);
	}
      else
	{
	  ADD_ARG_FLOAT (net_buf, *((float *) value));
	}
      break;
    case CCI_U_TYPE_MONETARY:
    case CCI_U_TYPE_DOUBLE:
      if (value == NULL)
	{
	  ADD_ARG_DOUBLE (net_buf, 0.0l);
	}
      else
	{
	  ADD_ARG_DOUBLE (net_buf, *((double *) value));
	}
      break;
    case CCI_U_TYPE_DATE:
    case CCI_U_TYPE_TIME:
    case CCI_U_TYPE_TIMESTAMP:
    case CCI_U_TYPE_DATETIME:
      if (value == NULL)
	{
	  T_CCI_DATE default_value;
	  default_value.yr = 1970;
	  default_value.mon = 1;
	  default_value.day = 1;
	  default_value.hh = 0;
	  default_value.mm = 0;
	  default_value.ss = 0;
	  default_value.ms = 0;
	  ADD_ARG_DATETIME (net_buf, &default_value);
	}
      else
	{
	  ADD_ARG_DATETIME (net_buf, value);
	}
      break;
    case CCI_U_TYPE_TIMESTAMPTZ:
    case CCI_U_TYPE_TIMESTAMPLTZ:
    case CCI_U_TYPE_DATETIMETZ:
    case CCI_U_TYPE_DATETIMELTZ:
      if (value == NULL)
	{
	  T_CCI_DATE_TZ default_value;
	  default_value.yr = 1970;
	  default_value.mon = 1;
	  default_value.day = 1;
	  default_value.hh = 0;
	  default_value.mm = 0;
	  default_value.ss = 0;
	  default_value.tz[0] = '\0';
	  ADD_ARG_DATETIMETZ (net_buf, &default_value);
	}
      else
	{
	  ADD_ARG_DATETIMETZ (net_buf, value);
	}
      break;
    case CCI_U_TYPE_OBJECT:
      if (value == NULL)
	{
	  ADD_ARG_BYTES (net_buf, NULL, 0);
	}
      else
	{
	  ADD_ARG_OBJECT (net_buf, value);
	}
      break;
    case CCI_U_TYPE_BLOB:
    case CCI_U_TYPE_CLOB:
      if (value == NULL)
	{
	  ADD_ARG_BYTES (net_buf, NULL, 0);
	}
      else
	{
	  ADD_ARG_LOB (net_buf, value);
	}
      break;
    default:
      ADD_ARG_BYTES (net_buf, NULL, 0);
      break;
    }

  return 0;
}

static int
execute_array_info_decode (char *buf, int size, char flag, T_CCI_QUERY_RESULT ** res_qr, int *res_remain_size)
{
  int remain_size = size;
  char *cur_p = buf;
  int num_query;
  int res_count;
  int i;
  T_OBJECT oid;
  T_CCI_QUERY_RESULT *qr;
  char client_cache_reusable;

  if (flag == EXECUTE_EXEC)
    {
      if (remain_size < 1)
	{
	  return CCI_ER_COMMUNICATION;
	}
      client_cache_reusable = *cur_p;
      remain_size -= 1;
      cur_p += 1;
    }

  if (remain_size < 4)
    {
      return CCI_ER_COMMUNICATION;
    }

  if (cur_p == NULL)
    {
      return CCI_ER_COMMUNICATION;
    }

  NET_STR_TO_INT (num_query, cur_p);
  if (num_query < 0)
    {
      assert (0);
      return CCI_ER_COMMUNICATION;
    }

  remain_size -= 4;
  cur_p += 4;

  if (num_query == 0)
    {
      assert (0);
      *res_qr = NULL;
      *res_remain_size = remain_size;

      return num_query;
    }

  qr = (T_CCI_QUERY_RESULT *) MALLOC (sizeof (T_CCI_QUERY_RESULT) * num_query);
  if (qr == NULL)
    {
      return CCI_ER_NO_MORE_MEMORY;
    }

  memset (qr, 0, sizeof (T_CCI_QUERY_RESULT) * num_query);

  for (i = 0; i < num_query; i++)
    {
      if (flag == EXECUTE_BATCH || flag == EXECUTE_EXEC)
	{
	  if (remain_size < 1)
	    {
	      qe_query_result_free (i, qr);
	      return CCI_ER_COMMUNICATION;
	    }

	  qr[i].stmt_type = *cur_p;
	  remain_size -= 1;
	  cur_p += 1;
	}

      if (remain_size < 4)
	{
	  qe_query_result_free (i, qr);
	  return CCI_ER_COMMUNICATION;
	}

      NET_STR_TO_INT (res_count, cur_p);
      remain_size -= 4;
      cur_p += 4;

      qr[i].result_count = res_count;

      if (res_count < 0)
	{
	  int err_code;
	  int err_msg_size;

	  if (remain_size < 4)
	    {
	      qe_query_result_free (i, qr);
	      return CCI_ER_COMMUNICATION;
	    }

	  NET_STR_TO_INT (err_code, cur_p);
	  remain_size -= 4;
	  cur_p += 4;

	  qr[i].err_no = err_code;

	  if (remain_size < 4)
	    {
	      qe_query_result_free (i, qr);
	      return CCI_ER_COMMUNICATION;
	    }

	  NET_STR_TO_INT (err_msg_size, cur_p);
	  remain_size -= 4;
	  cur_p += 4;

	  if (err_msg_size > 0)
	    {
	      if (remain_size < err_msg_size)
		{
		  qe_query_result_free (i, qr);
		  return CCI_ER_COMMUNICATION;
		}

	      ALLOC_N_COPY (qr[i].err_msg, cur_p, err_msg_size, char *);
	      remain_size -= err_msg_size;
	      cur_p += err_msg_size;
	    }
	}
      else
	{
	  if (remain_size < NET_SIZE_OBJECT)
	    {
	      qe_query_result_free (i, qr);
	      return CCI_ER_COMMUNICATION;
	    }

	  NET_STR_TO_OBJECT (oid, cur_p);
	  ut_oid_to_str (&oid, qr[i].oid);
	  cur_p += NET_SIZE_OBJECT;
	  remain_size -= NET_SIZE_OBJECT;

	  if (flag == EXECUTE_EXEC)
	    {
	      int cache_time_sec, cache_time_usec;
	      if (remain_size < 8)
		{
		  qe_query_result_free (i, qr);
		  return CCI_ER_COMMUNICATION;
		}

	      NET_STR_TO_INT (cache_time_sec, cur_p);
	      NET_STR_TO_INT (cache_time_usec, cur_p + 4);
	      remain_size -= 8;
	      cur_p += 8;
	    }
	}
    }

  *res_qr = qr;
  *res_remain_size = remain_size;

  return num_query;
}

static T_CCI_U_TYPE
get_basic_utype (T_CCI_U_EXT_TYPE u_ext_type)
{
  if (CCI_IS_SET_TYPE (u_ext_type))
    {
      return CCI_U_TYPE_SET;
    }
  else if (CCI_IS_MULTISET_TYPE (u_ext_type))
    {
      return CCI_U_TYPE_MULTISET;
    }
  else if (CCI_IS_SEQUENCE_TYPE (u_ext_type))
    {
      return CCI_U_TYPE_SEQUENCE;
    }
  else
    {
      return (T_CCI_U_TYPE) CCI_GET_COLLECTION_DOMAIN (u_ext_type);
    }
}

static int
parameter_info_decode (char *buf, int size, int num_param, T_CCI_PARAM_INFO ** res_param)
{
  T_CCI_PARAM_INFO *param;
  int i;

  param = (T_CCI_PARAM_INFO *) MALLOC (sizeof (T_CCI_PARAM_INFO) * num_param);
  if (param == NULL)
    {
      return CCI_ER_NO_MORE_MEMORY;
    }
  memset (param, 0, sizeof (T_CCI_PARAM_INFO) * num_param);

  for (i = 0; i < num_param; i++)
    {
      T_CCI_U_TYPE basic_type, set_type;

      if (size < 1)
	{
	  goto param_decode_error;
	}
      param[i].mode = (T_CCI_PARAM_MODE) (*buf);
      size -= 1;
      buf += 1;

      if (size < 1)
	{
	  goto param_decode_error;
	}
      basic_type = (T_CCI_U_TYPE) (*buf);
      size -= 1;
      buf += 1;

      if (CCI_NET_TYPE_HAS_2BYTES (basic_type))
	{
	  if (size < 1)
	    {
	      goto param_decode_error;
	    }

	  /* type is encoded with 2 bytes */
	  set_type = (T_CCI_U_TYPE) * buf;
	  size -= 1;
	  buf += 1;

	  param[i].ext_type = get_ext_utype_from_net_bytes (basic_type, set_type);
	}
      else
	{
	  /* legacy server : one byte */
	  param[i].ext_type = basic_type;
	}

      if (size < 2)
	{
	  goto param_decode_error;
	}
      NET_STR_TO_SHORT (param[i].scale, buf);
      size -= 2;
      buf += 2;

      if (size < 4)
	{
	  goto param_decode_error;
	}
      NET_STR_TO_INT (param[i].precision, buf);
      size -= 4;
      buf += 4;
    }

  *res_param = param;

  return 0;

param_decode_error:
  qe_param_info_free (param);
  return CCI_ER_COMMUNICATION;
}

static int
decode_fetch_result (T_CON_HANDLE * con_handle, T_REQ_HANDLE * req_handle, char *result_msg_org, char *result_msg_start,
		     int result_msg_size)
{
  int num_cols;
  int num_tuple;

  if (req_handle->stmt_type == CUBRID_STMT_CALL_SP)
    {
      num_cols = req_handle->num_bind + 1;
    }
  else
    {
      num_cols = req_handle->num_col_info;
    }

  num_tuple =
    fetch_info_decode (result_msg_start, result_msg_size, num_cols, &(req_handle->tuple_value), FETCH_FETCH, req_handle,
		       con_handle);
  if (num_tuple < 0)
    {
      return num_tuple;
    }

  if (num_tuple == 0)
    {
      req_handle->fetched_tuple_begin = 0;
      req_handle->fetched_tuple_end = 0;
      req_handle->msg_buf = result_msg_org;
      req_handle->cur_fetch_tuple_index = -1;
    }
  else
    {
      req_handle->fetched_tuple_begin = req_handle->cursor_pos;
      req_handle->fetched_tuple_end = req_handle->cursor_pos + num_tuple - 1;
      req_handle->msg_buf = result_msg_org;
      req_handle->cur_fetch_tuple_index = 0;
    }

  return num_tuple;
}

#ifdef CCI_XA
static void
add_arg_xid (T_NET_BUF * net_buf, XID * xid)
{
  net_buf_cp_int (net_buf, 12 + xid->gtrid_length + xid->bqual_length);
  net_buf_cp_int (net_buf, xid->formatID);
  net_buf_cp_int (net_buf, xid->gtrid_length);
  net_buf_cp_int (net_buf, xid->bqual_length);
  net_buf_cp_str (net_buf, xid->data, xid->gtrid_length + xid->bqual_length);
}

static int
xa_prepare_info_decode (char *buf, int buf_size, int count, XID * xid, int num_xid_buf)
{
  int xid_data_size;
  int i;

  if (count > num_xid_buf)
    count = num_xid_buf;

  for (i = 0; i < count; i++)
    {
      if (buf_size < 4)
	return CCI_ER_COMMUNICATION;
      NET_STR_TO_INT (xid_data_size, buf);
      buf += 4;
      buf_size -= 4;
      if (buf_size < xid_data_size)
	return CCI_ER_COMMUNICATION;
      net_str_to_xid (buf, &(xid[i]));
      buf += xid_data_size;
      buf_size -= xid_data_size;
    }

  return count;
}

static void
net_str_to_xid (char *buf, XID * xid)
{
  memset (xid, 0, sizeof (XID));
  NET_STR_TO_INT (xid->formatID, buf);
  buf += 4;
  NET_STR_TO_INT (xid->gtrid_length, buf);
  buf += 4;
  NET_STR_TO_INT (xid->bqual_length, buf);
  buf += 4;
  memcpy (xid->data, buf, xid->gtrid_length + xid->bqual_length);
}
#endif

static int
shard_info_decode (char *buf_p, int size, int num_shard, T_CCI_SHARD_INFO ** res_shard_info)
{
  T_CCI_SHARD_INFO *shard_info = NULL;
  char *cur_p = buf_p;
  int remain_size = size;
  int num_shard_tmp;
  int str_size;
  int i;

  num_shard_tmp = num_shard + 1 /* fence */ ;

  if (res_shard_info == NULL)
    {
      return CCI_ER_INVALID_ARGS;
    }

  shard_info = (T_CCI_SHARD_INFO *) MALLOC (sizeof (T_CCI_SHARD_INFO) * num_shard_tmp);
  if (shard_info == NULL)
    {
      return CCI_ER_NO_MORE_MEMORY;
    }

  for (i = 0; i < num_shard_tmp; i++)
    {
      shard_info[i].shard_id = SHARD_ID_INVALID;
      shard_info[i].db_name = NULL;
      shard_info[i].db_server = NULL;
    }

  for (i = 0; i < num_shard; i++)
    {
      if (remain_size < 4)
	{
	  goto shard_info_decode_error;
	}

      /* shard id */
      NET_STR_TO_INT (shard_info[i].shard_id, cur_p);
      remain_size -= NET_SIZE_INT;
      cur_p += NET_SIZE_INT;

      /* shard real db name */
      NET_STR_TO_INT (str_size, cur_p);
      remain_size -= NET_SIZE_INT;
      cur_p += NET_SIZE_INT;
      if (remain_size < str_size)
	{
	  goto shard_info_decode_error;
	}
      ALLOC_N_COPY (shard_info[i].db_name, cur_p, str_size, char *);
      if (shard_info[i].db_name == NULL)
	{
	  goto shard_info_decode_error;
	}
      remain_size -= str_size;
      cur_p += str_size;

      /* shard read db server */
      NET_STR_TO_INT (str_size, cur_p);
      remain_size -= NET_SIZE_INT;
      cur_p += NET_SIZE_INT;
      if (remain_size < str_size)
	{
	  goto shard_info_decode_error;
	}
      ALLOC_N_COPY (shard_info[i].db_server, cur_p, str_size, char *);
      if (shard_info[i].db_server == NULL)
	{
	  goto shard_info_decode_error;
	}
      remain_size -= str_size;
      cur_p += str_size;
    }

  assert (remain_size == 0);

  *(res_shard_info) = shard_info;
  return CCI_ER_NO_ERROR;

shard_info_decode_error:
  if (shard_info)
    {
      qe_shard_info_free (shard_info);
      shard_info = NULL;
    }

  return CCI_ER_COMMUNICATION;
}

bool
is_connected_to_cubrid (T_CON_HANDLE * con_handle)
{
  return ((con_handle->broker_info[BROKER_INFO_DBMS_TYPE] == CAS_DBMS_CUBRID)
	  || (con_handle->broker_info[BROKER_INFO_DBMS_TYPE] == CAS_PROXY_DBMS_CUBRID));
}

bool
is_connected_to_oracle (T_CON_HANDLE * con_handle)
{
  return ((con_handle->broker_info[BROKER_INFO_DBMS_TYPE] == CAS_DBMS_ORACLE)
	  || (con_handle->broker_info[BROKER_INFO_DBMS_TYPE] == CAS_PROXY_DBMS_ORACLE));
}

static bool
is_set_default_value_if_null (T_CON_HANDLE * con_handle, T_CCI_CUBRID_STMT stmt_type, char bind_mode)
{
  return (is_connected_to_oracle (con_handle) && (stmt_type == CUBRID_STMT_CALL_SP && bind_mode == CCI_PARAM_MODE_OUT));
}

/* get_ext_utype_from_net_bytes - creates an internal CCI extended type from
 *				  bytes received from network
 *				  input bits: basic_type : 0CCR RRRR
 *					      set_type   : TTTT TTTT
 *				  output bits            : TCCT TTTT
 *				  Only 6 bits from set_type are retained
 *				  (enough for 63 values)
 *
 */
static T_CCI_U_EXT_TYPE
get_ext_utype_from_net_bytes (T_CCI_U_TYPE basic_type, T_CCI_U_TYPE set_type)
{
  T_CCI_U_EXT_TYPE ext_type_lsb, ext_type_msb, ext_type_cc, ext_type;

  ext_type_lsb = set_type & 0x1f;
  ext_type_msb = (set_type & 0x20) << 2;
  ext_type_cc = basic_type & CCI_CODE_COLLECTION;

  ext_type = ext_type_msb | ext_type_cc | ext_type_lsb;

  return ext_type;
}

static void
confirm_schema_type_info (T_REQ_HANDLE * req_handle, int col_no, T_CCI_U_TYPE u_type, char *col_value_p, int data_size)
{
#define SCHEMA_INFO_TYPE_COL_INDEX 2

  if (req_handle->handle_type == HANDLE_SCHEMA_INFO
      && (req_handle->handle_sub_type == CCI_SCH_ATTRIBUTE || req_handle->handle_sub_type == CCI_SCH_CLASS_ATTRIBUTE)
      && col_no == SCHEMA_INFO_TYPE_COL_INDEX)
    {
      unsigned short value, net_val;
      assert (u_type == CCI_U_TYPE_SHORT);
      assert (data_size == NET_SIZE_SHORT);

      NET_STR_TO_USHORT (value, col_value_p);

      value = value & ~(CCI_CODE_COLLECTION << 8);
      value = value & ~(CAS_TYPE_FIRST_BYTE_PROTOCOL_MASK << 8);

      net_val = htons (value);
      memcpy (col_value_p, (char *) &net_val, NET_SIZE_SHORT);
    }

#undef SCHEMA_INFO_TYPE_COL_INDEX
}
