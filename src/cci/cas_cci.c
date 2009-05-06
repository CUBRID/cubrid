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


/*
 * cas_cci.c -
 */

#ident "$Id$"

/************************************************************************
 * IMPORTED SYSTEM HEADER FILES						*
 ************************************************************************/
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef CCI_DEBUG
#include <stdarg.h>
#endif

#if defined(WINDOWS)
#include <winsock2.h>
#include <windows.h>
#include <process.h>
#else
#include <unistd.h>
#include <pthread.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <signal.h>
#endif

#if !defined(WINDOWS)
#include <sys/socket.h>
#endif

/************************************************************************
 * OTHER IMPORTED HEADER FILES						*
 ************************************************************************/

#if defined(WINDOWS)
#include "version.h"
#endif
#include "cci_common.h"
#include "cas_cci.h"
#include "cci_handle_mng.h"
#include "cci_network.h"
#include "cci_query_execute.h"
#include "cci_t_set.h"
#include "cas_protocol.h"
#include "cci_net_buf.h"
#include "cci_util.h"

#if defined(WINDOWS)
#include "cm_wsa_init.h"
#endif

#ifdef CCI_XA
#include "cci_xa.h"
#endif


/************************************************************************
 * PRIVATE DEFINITIONS							*
 ************************************************************************/

#ifdef CCI_DEBUG
#define CCI_DEBUG_PRINT(DEBUG_MSG_FUNC)		DEBUG_MSG_FUNC
#define DEBUG_STR(STR)			((STR) == NULL ? "NULL" : (STR))
#else
#define CCI_DEBUG_PRINT(DEBUG_MSG_FUNC)
#endif

#if defined(WINDOWS)
#define snprintf	_snprintf
#endif

#define IS_OUT_TRAN_STATUS(CON_HANDLE) \
        (IS_INVALID_SOCKET((CON_HANDLE)->sock_fd) || \
         ((CON_HANDLE)->con_status == CCI_CON_STATUS_OUT_TRAN))

#define NEED_TO_CONNECT(CON_HANDLE) \
        (IS_INVALID_SOCKET((CON_HANDLE)->sock_fd) &&     \
         ((CON_HANDLE)->tran_status == CCI_TRAN_STATUS_START))


/************************************************************************
 * PRIVATE FUNCTION PROTOTYPES						*
 ************************************************************************/

static void err_buf_reset (T_CCI_ERROR * err_buf);
static int col_set_add_drop (int con_h_id, char col_cmd, char *oid_str,
			     char *col_attr, char *value,
			     T_CCI_ERROR * err_buf);
static int col_seq_op (int con_h_id, char col_cmd, char *oid_str,
		       char *col_attr, int index, const char *value,
		       T_CCI_ERROR * err_buf);
static int fetch_cmd (int reg_h_id, char flag, T_CCI_ERROR * err_buf);
static int cas_connect (T_CON_HANDLE * con_handle, T_CCI_ERROR * err_buf);
static int cas_connect_with_ret (T_CON_HANDLE * con_handle,
				 T_CCI_ERROR * err_buf, int *connect);
static int cas_connect_low (T_CON_HANDLE * con_handle,
			    T_CCI_ERROR * err_buf, int *connect);
static int get_query_info (int req_h_id, char log_type, char **out_buf);
static int next_result_cmd (int req_h_id, char flag, T_CCI_ERROR * err_buf);

static int glo_cmd_init (T_NET_BUF * net_buf, char glo_cmd, char *oid_str,
			 T_CCI_ERROR * err_buf);
static int glo_cmd_common (int con_h_id, T_NET_BUF * net_buf,
			   char **result_msg, int *result_msg_size,
			   T_CCI_ERROR * err_buf);
static int glo_cmd_ex (SOCKET sock_fd, T_NET_BUF * net_buf, char **result_msg,
		       int *result_msg_size, T_CCI_ERROR * err_buf);


#ifdef CCI_DEBUG
static void print_debug_msg (const char *format, ...);
static char *dbg_tran_type_str (char type);
static char *dbg_a_type_str (T_CCI_A_TYPE);
static char *dbg_u_type_str (T_CCI_U_TYPE);
static char *dbg_db_param_str (T_CCI_DB_PARAM db_param);
static char *dbg_cursor_pos_str (T_CCI_CURSOR_POS cursor_pos);
static char *dbg_sch_type_str (T_CCI_SCH_TYPE sch_type);
static char *dbg_oid_cmd_str (T_CCI_OID_CMD oid_cmd);
static char *dbg_isolation_str (T_CCI_TRAN_ISOLATION isol_level);
#endif

static THREAD_FUNC execute_thread (void *arg);
static int get_thread_result (T_CON_HANDLE * con_handle,
			      T_CCI_ERROR * err_buf);
static int connect_prepare_again (T_CON_HANDLE * con_handle,
				  T_REQ_HANDLE * req_handle,
				  T_CCI_ERROR * err_buf);
static const char *cci_get_err_msg_low (int err_code);

/************************************************************************
 * INTERFACE VARIABLES							*
 ************************************************************************/

/************************************************************************
 * PUBLIC VARIABLES							*
 ************************************************************************/

/************************************************************************
 * PRIVATE VARIABLES							*
 ************************************************************************/

#if defined(WINDOWS)
static HANDLE con_handle_table_mutex;
#else
static T_MUTEX con_handle_table_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

static char init_flag = 0;
static char cci_init_flag = 1;
#if !defined(WINDOWS)
static int cci_SIGPIPE_ignore = 0;
#endif


/************************************************************************
 * IMPLEMENTATION OF INTERFACE FUNCTIONS 				*
 ************************************************************************/

#if defined(WINDOWS) && defined(CAS_CCI_DL)
BOOL APIENTRY
DllMain (HANDLE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
  if (ul_reason_for_call == DLL_PROCESS_ATTACH)
    {
      cci_init ();
    }
  return TRUE;
}
#endif

void
cci_init ()
{
#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("cci_init"));
#endif

  if (cci_init_flag)
    {
      cci_init_flag = 0;
#if defined(WINDOWS)
      MUTEX_INIT (con_handle_table_mutex);
#endif
    }
}

void
cci_end ()
{
}

int
cci_get_version (int *major, int *minor, int *patch)
{
#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("cci_get_version:%d.%d.%d", MAJOR_VERSION,
		    MINOR_VERSION, PATCH_VERSION));
#endif

  if (major)
    {
      *major = MAJOR_VERSION;
    }
  if (minor)
    {
      *minor = MINOR_VERSION;
    }
  if (patch)
    {
      *patch = PATCH_VERSION;
    }
  return 0;
}

int
CCI_CONNECT_INTERNAL_FUNC_NAME (char *ip, int port, char *db_name,
				char *db_user, char *dbpasswd)
{
  int con_handle_id;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("cci_connect %s %d %s %s %s", ip, port,
		    DEBUG_STR (db_name), DEBUG_STR (db_name),
		    DEBUG_STR (dbpasswd)));
#endif


#if defined(WINDOWS)
  if (wsa_initialize () < 0)
    return CCI_ER_CONNECT;
#endif

  MUTEX_LOCK (con_handle_table_mutex);

  if (init_flag == 0)
    {
      hm_con_handle_table_init ();
      init_flag = 1;
    }

  con_handle_id = hm_con_handle_alloc (ip, port, db_name, db_user, dbpasswd);

  MUTEX_UNLOCK (con_handle_table_mutex);


#if !defined(WINDOWS)
  if (!cci_SIGPIPE_ignore)
    {
      signal (SIGPIPE, SIG_IGN);
      cci_SIGPIPE_ignore = 1;
    }
#endif

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("cci_connect return %d", con_handle_id));
#endif

  return con_handle_id;
}

int
cci_disconnect (int con_h_id, T_CCI_ERROR * err_buf)
{
  int err_code = 0;
  T_CON_HANDLE *con_handle;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("cci_disconnect %d", con_h_id));
#endif

  err_buf_reset (err_buf);

  while (1)
    {
      MUTEX_LOCK (con_handle_table_mutex);

      con_handle = hm_find_con_handle (con_h_id);
      if (con_handle == NULL)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  return CCI_ER_CON_HANDLE;
	}

      if (con_handle->ref_count > 0)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  SLEEP_MILISEC (0, 100);
	}
      else
	{
	  con_handle->ref_count = 1;
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  break;
	}
    }


  err_code = qe_con_close (con_handle);
  if (err_code >= 0)
    {
      MUTEX_LOCK (con_handle_table_mutex);
      con_handle->ref_count = 0;
      err_code = hm_con_handle_free (con_h_id);
      MUTEX_UNLOCK (con_handle_table_mutex);
    }
  else
    {
      con_handle->ref_count = 0;
    }

  return err_code;
}

int
cci_cancel (int con_h_id)
{
  T_CON_HANDLE *con_handle;
  unsigned char ip_addr[4];
  int port;
  int cas_pid;
  int ref_count;

  MUTEX_LOCK (con_handle_table_mutex);

  con_handle = hm_find_con_handle (con_h_id);
  if (con_handle == NULL)
    {
      MUTEX_UNLOCK (con_handle_table_mutex);
      return CCI_ER_CON_HANDLE;
    }

  memcpy (ip_addr, con_handle->ip_addr, 4);
  port = con_handle->port;
  cas_pid = con_handle->cas_pid;
  ref_count = con_handle->ref_count;

  MUTEX_UNLOCK (con_handle_table_mutex);

  if (ref_count <= 0)
    return 0;

  return (net_cancel_request (ip_addr, port, cas_pid));
}

int
cci_end_tran (int con_h_id, char type, T_CCI_ERROR * err_buf)
{
  int err_code = 0;
  T_CON_HANDLE *con_handle;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("cci_end_tran %d %s", con_h_id,
		    dbg_tran_type_str (type)));
#endif

  err_buf_reset (err_buf);

  while (1)
    {
      MUTEX_LOCK (con_handle_table_mutex);

      con_handle = hm_find_con_handle (con_h_id);
      if (con_handle == NULL)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  return CCI_ER_CON_HANDLE;
	}

      if (con_handle->ref_count > 0)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  SLEEP_MILISEC (0, 100);
	}
      else
	{
	  con_handle->ref_count = 1;
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  break;
	}
    }

  if (con_handle->tran_status != CCI_TRAN_STATUS_START)
    {
      err_code = qe_end_tran (con_handle, type, err_buf);
      con_handle->tran_status = CCI_TRAN_STATUS_START;
    }
  con_handle->ref_count = 0;

  return err_code;
}

int
cci_prepare (int con_id, char *sql_stmt, char flag, T_CCI_ERROR * err_buf)
{
  T_CON_HANDLE *con_handle = NULL;
  T_REQ_HANDLE *req_handle = NULL;
  int req_handle_id = -1;
  int err_code = 0;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("cci_prepare %d %d %s", con_id, flag,
		    DEBUG_STR (sql_stmt)));
#endif

  err_buf_reset (err_buf);

  if (sql_stmt == NULL)
    return CCI_ER_STRING_PARAM;

  while (1)
    {
      MUTEX_LOCK (con_handle_table_mutex);

      con_handle = hm_find_con_handle (con_id);
      if (con_handle == NULL)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  return CCI_ER_CON_HANDLE;
	}

      if (con_handle->ref_count > 0)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  SLEEP_MILISEC (0, 100);
	}
      else
	{
	  con_handle->ref_count = 1;
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  break;
	}
    }

  if (NEED_TO_CONNECT (con_handle))
    {
      err_code = cas_connect (con_handle, err_buf);
      if (err_code < 0)
	{
	  goto prepare_error;
	}
    }

  req_handle_id = hm_req_handle_alloc (con_id, &req_handle);
  if (req_handle_id < 0)
    {
      err_code = req_handle_id;
      goto prepare_error;
    }

  err_code =
    qe_prepare (req_handle, con_handle->sock_fd, sql_stmt, flag, err_buf, 0);

  if (err_code < 0)
    {
      if (con_handle->tran_status == CCI_TRAN_STATUS_START)
	{
	  int con_err_code = 0;
	  int connect_done;

	  con_err_code = cas_connect_with_ret (con_handle, err_buf,
					       &connect_done);

	  if (con_err_code < 0)
	    {
	      err_code = con_err_code;
	      goto prepare_error;
	    }
	  if (!connect_done)
	    {
	      /* connection is no problem */
	      goto prepare_error;
	    }

	  req_handle_content_free (req_handle, 0);
	  err_code = qe_prepare (req_handle, con_handle->sock_fd, sql_stmt,
				 flag, err_buf, 0);

	  if (err_code < 0)
	    {
	      goto prepare_error;
	    }
	}
      else
	{
	  goto prepare_error;
	}
    }

  con_handle->ref_count = 0;
  con_handle->con_status = CCI_CON_STATUS_IN_TRAN;

  return req_handle_id;

prepare_error:
  if (req_handle)
    hm_req_handle_free (con_handle, req_handle_id, req_handle);
  con_handle->ref_count = 0;
  return (err_code);
}

int
cci_get_bind_num (int req_h_id)
{
  T_REQ_HANDLE *req_handle;
  int num_bind;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("cci_get_bind_num %d", req_h_id));
#endif

  MUTEX_LOCK (con_handle_table_mutex);

  req_handle = hm_find_req_handle (req_h_id, NULL);
  if (req_handle == NULL)
    {
      num_bind = CCI_ER_REQ_HANDLE;
    }
  else
    {
      num_bind = req_handle->num_bind;
    }

  MUTEX_UNLOCK (con_handle_table_mutex);

  return num_bind;
}

int
cci_is_updatable (int req_h_id)
{
  T_REQ_HANDLE *req_handle;
  int updatable_flag;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("cci_is_updatable %d", req_h_id));
#endif

  MUTEX_LOCK (con_handle_table_mutex);

  req_handle = hm_find_req_handle (req_h_id, NULL);
  if (req_handle == NULL)
    {
      updatable_flag = CCI_ER_REQ_HANDLE;
    }
  else
    {
      updatable_flag = (int) (req_handle->updatable_flag);
    }

  MUTEX_UNLOCK (con_handle_table_mutex);

  return updatable_flag;
}

T_CCI_COL_INFO *
cci_get_result_info (int req_h_id, T_CCI_CUBRID_STMT * cmd_type, int *num)
{
  T_REQ_HANDLE *req_handle;
  T_CCI_COL_INFO *col_info;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("cci_get_result_info %d", req_h_id));
#endif

  if (cmd_type)
    *cmd_type = (T_CCI_CUBRID_STMT) - 1;
  if (num)
    *num = 0;
  col_info = NULL;

  MUTEX_LOCK (con_handle_table_mutex);

  req_handle = hm_find_req_handle (req_h_id, NULL);
  if (req_handle != NULL)
    {
      if (cmd_type)
	{
	  *cmd_type = req_handle->stmt_type;
	}

      switch (req_handle->handle_type)
	{
	case HANDLE_PREPARE:
	  if (req_handle->stmt_type == CUBRID_STMT_SELECT
	      || req_handle->stmt_type == CUBRID_STMT_GET_STATS
	      || req_handle->stmt_type == CUBRID_STMT_EVALUATE
	      || req_handle->stmt_type == CUBRID_STMT_CALL
	      || req_handle->stmt_type == CUBRID_STMT_CALL_SP)
	    {
	      if (num)
		{
		  *num = req_handle->num_col_info;
		}
	      col_info = req_handle->col_info;
	    }
	  break;
	case HANDLE_OID_GET:
	case HANDLE_SCHEMA_INFO:
	case HANDLE_COL_GET:
	  if (num)
	    {
	      *num = req_handle->num_col_info;
	    }
	  col_info = req_handle->col_info;
	  break;
	default:
	  break;
	}
    }

  MUTEX_UNLOCK (con_handle_table_mutex);

  return col_info;
}

int
cci_bind_param (int req_h_id, int index, T_CCI_A_TYPE a_type, void *value,
		T_CCI_U_TYPE u_type, char flag)
{
  int err_code = 0;
  T_REQ_HANDLE *req_handle;
  T_CON_HANDLE *con_handle;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("cci_bind_param %d %d %s %p %s %d", req_h_id, index,
		    dbg_a_type_str (a_type), value, dbg_u_type_str (u_type),
		    flag));
#endif

  while (1)
    {
      MUTEX_LOCK (con_handle_table_mutex);

      req_handle = hm_find_req_handle (req_h_id, &con_handle);
      if (req_handle == NULL)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  return CCI_ER_REQ_HANDLE;
	}

      if (con_handle->ref_count > 0)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  SLEEP_MILISEC (0, 100);
	}
      else
	{
	  con_handle->ref_count = 1;
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  break;
	}
    }

  err_code = qe_bind_param (req_handle, index, a_type, value, u_type, flag);

  con_handle->ref_count = 0;

  return err_code;
}

int
cci_register_out_param (int req_h_id, int index)
{
  T_REQ_HANDLE *req_handle;
  int err_code = 0;

  MUTEX_LOCK (con_handle_table_mutex);

  req_handle = hm_find_req_handle (req_h_id, NULL);
  if (req_handle == NULL)
    {
      err_code = CCI_ER_REQ_HANDLE;
    }
  else
    {
      if (index <= 0 || index > req_handle->num_bind)
	{
	  err_code = CCI_ER_BIND_INDEX;
	}
      else
	{
	  req_handle->bind_mode[index - 1] |= CCI_PARAM_MODE_OUT;
	}
    }

  MUTEX_UNLOCK (con_handle_table_mutex);

  return err_code;
}

int
cci_bind_param_array_size (int req_h_id, int array_size)
{
  T_REQ_HANDLE *req_handle;
  int err_code = 0;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("cci_bind_param_array_size %d %d", req_h_id, array_size));
#endif

  MUTEX_LOCK (con_handle_table_mutex);

  req_handle = hm_find_req_handle (req_h_id, NULL);
  if (req_handle == NULL)
    {
      err_code = CCI_ER_REQ_HANDLE;
    }
  else
    {
      req_handle->bind_array_size = array_size;
    }

  MUTEX_UNLOCK (con_handle_table_mutex);

  return err_code;
}

int
cci_bind_param_array (int req_h_id, int index, T_CCI_A_TYPE a_type,
		      void *value, int *null_ind, T_CCI_U_TYPE u_type)
{
  T_REQ_HANDLE *req_handle;
  int err_code = 0;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("cci_bind_param_array %d %d %s %p %p %s", req_h_id, index,
		    dbg_a_type_str (a_type), value, null_ind,
		    dbg_u_type_str (u_type)));
#endif

  MUTEX_LOCK (con_handle_table_mutex);

  req_handle = hm_find_req_handle (req_h_id, NULL);
  if (req_handle == NULL)
    {
      err_code = CCI_ER_REQ_HANDLE;
    }
  else
    {
      if (req_handle->bind_array_size <= 0)
	{
	  err_code = CCI_ER_BIND_ARRAY_SIZE;
	}
      else if (index <= 0 || index > req_handle->num_bind)
	{
	  err_code = CCI_ER_BIND_INDEX;
	}
      else
	{
	  index--;
	  req_handle->bind_value[index].u_type = u_type;
	  req_handle->bind_value[index].size = a_type;
	  req_handle->bind_value[index].value = value;
	  req_handle->bind_value[index].null_ind = null_ind;
	  req_handle->bind_value[index].flag = BIND_PTR_STATIC;
	}
    }

  MUTEX_UNLOCK (con_handle_table_mutex);

  return err_code;
}

int
cci_execute (int req_h_id, char flag, int max_col_size, T_CCI_ERROR * err_buf)
{
  T_REQ_HANDLE *req_handle;
  T_CON_HANDLE *con_handle;
  int err_code = 0;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("cci_execute %d %d %d", req_h_id, flag, max_col_size));
#endif

  err_buf_reset (err_buf);

  while (1)
    {
      MUTEX_LOCK (con_handle_table_mutex);

      req_handle = hm_find_req_handle (req_h_id, &con_handle);
      if (req_handle == NULL)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  return CCI_ER_REQ_HANDLE;
	}

      if (con_handle->ref_count > 0)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  SLEEP_MILISEC (0, 100);
	}
      else
	{
	  con_handle->ref_count = 1;
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  break;
	}
    }

  if (BROKER_INFO_STATEMENT_POOLING (con_handle->broker_info) ==
      CAS_STATEMENT_POOLING_ON)
    {

      err_code = connect_prepare_again (con_handle, req_handle, err_buf);
      if (err_code < 0)
	{
	  goto execute_end;
	}
    }

  if (flag & CCI_EXEC_THREAD)
    {
      T_THREAD thrid;
      err_buf_reset (&(con_handle->thr_arg.err_buf));
      con_handle->thr_arg.req_handle = req_handle;
      con_handle->thr_arg.sock_fd = con_handle->sock_fd;
      con_handle->thr_arg.flag = flag ^ CCI_EXEC_THREAD;
      con_handle->thr_arg.max_col_size = max_col_size;
      con_handle->thr_arg.ref_count_ptr = &(con_handle->ref_count);
      con_handle->thr_arg.con_handle = con_handle;
      THREAD_BEGIN (thrid, execute_thread, &(con_handle->thr_arg));
      return CCI_ER_THREAD_RUNNING;
    }

  err_code =
    qe_execute (req_handle, con_handle->sock_fd, flag, max_col_size, err_buf);

  if (err_code < 0 && con_handle->tran_status == CCI_TRAN_STATUS_START)
    {
      int con_err_code = 0;
      int connect_done;

      con_err_code =
	cas_connect_with_ret (con_handle, err_buf, &connect_done);
      if (con_err_code < 0)
	{
	  err_code = con_err_code;
	  goto execute_end;
	}

      if (connect_done)
	{
	  /* error is caused by connection fail */
	  req_handle_content_free (req_handle, 1);
	  err_code = qe_prepare (req_handle,
				 con_handle->sock_fd,
				 req_handle->sql_text,
				 req_handle->prepare_flag, err_buf, 1);
	  if (err_code < 0)
	    {
	      goto execute_end;
	    }
	  err_code =
	    qe_execute (req_handle, con_handle->sock_fd, flag, max_col_size,
			err_buf);
	}
    }

  /* If prepared plan is invalidated while using plan cache,
     the error, CAS_ER_STMT_POOLING, is returned.
     In this case, prepare and execute have to be executed again.
   */
  if (BROKER_INFO_STATEMENT_POOLING (con_handle->broker_info) ==
      CAS_STATEMENT_POOLING_ON)
    {
      while (err_code == CAS_ER_STMT_POOLING)
	{
	  req_handle_content_free (req_handle, 1);
	  err_code =
	    qe_prepare (req_handle, con_handle->sock_fd, req_handle->sql_text,
			req_handle->prepare_flag, err_buf, 1);
	  if (err_code < 0)
	    {
	      goto execute_end;
	    }
	  err_code =
	    qe_execute (req_handle, con_handle->sock_fd, flag, max_col_size,
			err_buf);
	}
    }

  if (con_handle->tran_status == CCI_TRAN_STATUS_START)
    {
      con_handle->tran_status = CCI_TRAN_STATUS_RUNNING;
    }

  con_handle->con_status = CCI_CON_STATUS_IN_TRAN;

execute_end:
  con_handle->ref_count = 0;

  return err_code;
}

int
cci_get_thread_result (int con_id, T_CCI_ERROR * err_buf)
{
  int err_code;
  T_CON_HANDLE *con_handle;

  err_buf_reset (err_buf);

  MUTEX_LOCK (con_handle_table_mutex);

  con_handle = hm_find_con_handle (con_id);
  if (con_handle == NULL)
    {
      MUTEX_UNLOCK (con_handle_table_mutex);
      return CCI_ER_CON_HANDLE;
    }

  err_code = get_thread_result (con_handle, err_buf);

  MUTEX_UNLOCK (con_handle_table_mutex);

  return err_code;
}

int
cci_next_result (int req_h_id, T_CCI_ERROR * err_buf)
{
#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("cci_next_result %d", req_h_id));
#endif

  return (next_result_cmd (req_h_id, CCI_CLOSE_CURRENT_RESULT, err_buf));
}

int
cci_execute_array (int req_h_id, T_CCI_QUERY_RESULT ** qr,
		   T_CCI_ERROR * err_buf)
{
  T_REQ_HANDLE *req_handle;
  T_CON_HANDLE *con_handle;
  int err_code = 0;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("cci_execute_array %d", req_h_id));
#endif

  *qr = NULL;
  err_buf_reset (err_buf);

  while (1)
    {
      MUTEX_LOCK (con_handle_table_mutex);

      req_handle = hm_find_req_handle (req_h_id, &con_handle);
      if (req_handle == NULL)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  return CCI_ER_REQ_HANDLE;
	}

      if (con_handle->ref_count > 0)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  SLEEP_MILISEC (0, 100);
	}
      else
	{
	  con_handle->ref_count = 1;
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  break;
	}
    }

  if (BROKER_INFO_STATEMENT_POOLING (con_handle->broker_info) ==
      CAS_STATEMENT_POOLING_ON)
    {

      err_code = connect_prepare_again (con_handle, req_handle, err_buf);
      if (err_code < 0)
	{
	  goto execute_end;
	}
    }

  err_code = qe_execute_array (req_handle, con_handle->sock_fd, qr, err_buf);

  if (err_code < 0 && con_handle->tran_status == CCI_TRAN_STATUS_START)
    {
      int con_err_code = 0;
      int connect_done;

      con_err_code =
	cas_connect_with_ret (con_handle, err_buf, &connect_done);
      if (con_err_code < 0)
	{
	  err_code = con_err_code;
	  goto execute_end;
	}

      if (connect_done)
	{
	  req_handle_content_free (req_handle, 1);
	  err_code = qe_prepare (req_handle,
				 con_handle->sock_fd,
				 req_handle->sql_text,
				 req_handle->prepare_flag, err_buf, 1);
	  if (err_code < 0)
	    {
	      goto execute_end;
	    }
	  err_code =
	    qe_execute_array (req_handle, con_handle->sock_fd, qr, err_buf);
	}
    }

  if (BROKER_INFO_STATEMENT_POOLING (con_handle->broker_info) ==
      CAS_STATEMENT_POOLING_ON)
    {

      while (err_code == CAS_ER_STMT_POOLING)
	{
	  req_handle_content_free (req_handle, 1);
	  err_code =
	    qe_prepare (req_handle, con_handle->sock_fd, req_handle->sql_text,
			req_handle->prepare_flag, err_buf, 1);
	  if (err_code < 0)
	    {
	      goto execute_end;
	    }
	  err_code =
	    qe_execute_array (req_handle, con_handle->sock_fd, qr, err_buf);
	}
    }

  if (con_handle->tran_status == CCI_TRAN_STATUS_START)
    {
      con_handle->tran_status = CCI_TRAN_STATUS_RUNNING;
    }

  con_handle->con_status = CCI_CON_STATUS_IN_TRAN;

execute_end:
  con_handle->ref_count = 0;
  return err_code;
}

int
cci_get_db_parameter (int con_h_id, T_CCI_DB_PARAM param_name, void *value,
		      T_CCI_ERROR * err_buf)
{
  T_CON_HANDLE *con_handle;
  int err_code = 0;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("cci_get_db_parameter %d %s", con_h_id,
		    dbg_db_param_str (param_name)));
#endif

  err_buf_reset (err_buf);

  if (param_name < CCI_PARAM_FIRST || param_name > CCI_PARAM_LAST)
    return CCI_ER_PARAM_NAME;

  while (1)
    {
      MUTEX_LOCK (con_handle_table_mutex);

      con_handle = hm_find_con_handle (con_h_id);
      if (con_handle == NULL)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  return CCI_ER_CON_HANDLE;
	}

      if (con_handle->ref_count > 0)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  SLEEP_MILISEC (0, 100);
	}
      else
	{
	  con_handle->ref_count = 1;
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  break;
	}
    }

  if (IS_OUT_TRAN_STATUS (con_handle))
    {
      err_code = cas_connect (con_handle, err_buf);
    }

  if (err_code >= 0)
    err_code = qe_get_db_parameter (con_handle, param_name, value, err_buf);

  con_handle->ref_count = 0;
  con_handle->con_status = CCI_CON_STATUS_IN_TRAN;

  return err_code;
}

int
cci_set_db_parameter (int con_h_id, T_CCI_DB_PARAM param_name, void *value,
		      T_CCI_ERROR * err_buf)
{
  T_CON_HANDLE *con_handle;
  int err_code = 0;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("cci_set_db_parameter %d %s", con_h_id,
		    dbg_db_param_str (param_name)));
#endif

  err_buf_reset (err_buf);

  if (param_name < CCI_PARAM_FIRST || param_name > CCI_PARAM_LAST)
    return CCI_ER_PARAM_NAME;

  while (1)
    {
      MUTEX_LOCK (con_handle_table_mutex);

      con_handle = hm_find_con_handle (con_h_id);
      if (con_handle == NULL)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  return CCI_ER_CON_HANDLE;
	}

      if (con_handle->ref_count > 0)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  SLEEP_MILISEC (0, 100);
	}
      else
	{
	  con_handle->ref_count = 1;
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  break;
	}
    }

  if (IS_OUT_TRAN_STATUS (con_handle))
    {
      err_code = cas_connect (con_handle, err_buf);
    }

  if (err_code >= 0)
    err_code = qe_set_db_parameter (con_handle, param_name, value, err_buf);

  con_handle->ref_count = 0;
  con_handle->con_status = CCI_CON_STATUS_IN_TRAN;

  return err_code;
}

int
cci_close_req_handle (int req_h_id)
{
  T_REQ_HANDLE *req_handle;
  T_CON_HANDLE *con_handle;
  int err_code = 0;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("cci_close_req_handle %d", req_h_id));
#endif

  while (1)
    {
      MUTEX_LOCK (con_handle_table_mutex);

      req_handle = hm_find_req_handle (req_h_id, &con_handle);
      if (req_handle == NULL)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  return CCI_ER_REQ_HANDLE;
	}

      if (con_handle->ref_count > 0)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  SLEEP_MILISEC (0, 100);
	}
      else
	{
	  con_handle->ref_count = 1;
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  break;
	}
    }

  if (req_handle->handle_type == HANDLE_PREPARE
      || req_handle->handle_type == HANDLE_SCHEMA_INFO)
    {
      err_code = qe_close_req_handle (req_handle, con_handle->sock_fd);
    }
  hm_req_handle_free (con_handle, req_h_id, req_handle);

  con_handle->ref_count = 0;

  return err_code;
}

int
cci_cursor (int req_h_id, int offset, T_CCI_CURSOR_POS origin,
	    T_CCI_ERROR * err_buf)
{
  T_REQ_HANDLE *req_handle;
  T_CON_HANDLE *con_handle;
  int err_code = 0;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("cci_cursor %d %d %s", req_h_id, offset,
		    dbg_cursor_pos_str (origin)));
#endif

  err_buf_reset (err_buf);

  while (1)
    {
      MUTEX_LOCK (con_handle_table_mutex);

      req_handle = hm_find_req_handle (req_h_id, &con_handle);
      if (req_handle == NULL)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  return CCI_ER_REQ_HANDLE;
	}

      if (con_handle->ref_count > 0)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  SLEEP_MILISEC (0, 100);
	}
      else
	{
	  con_handle->ref_count = 1;
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  break;
	}
    }

  err_code =
    qe_cursor (req_handle, con_handle->sock_fd, offset, (char) origin,
	       err_buf);

  con_handle->ref_count = 0;

  return err_code;
}

int
cci_fetch_size (int req_h_id, int fetch_size)
{
  T_REQ_HANDLE *req_handle;
  int err_code = 0;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("cci_fetch_size %d %d", req_h_id, fetch_size));
#endif

  MUTEX_LOCK (con_handle_table_mutex);

  req_handle = hm_find_req_handle (req_h_id, NULL);
  if (req_handle == NULL)
    {
      err_code = CCI_ER_REQ_HANDLE;
    }
  else
    {
      req_handle->fetch_size = fetch_size;
    }

  MUTEX_UNLOCK (con_handle_table_mutex);

  return err_code;
}

int
cci_fetch (int req_h_id, T_CCI_ERROR * err_buf)
{
#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("cci_fetch %d", req_h_id));
#endif

  return (fetch_cmd (req_h_id, 0, err_buf));
}

int
cci_fetch_sensitive (int req_h_id, T_CCI_ERROR * err_buf)
{
#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("cci_fetch_sensitive %d", req_h_id));
#endif

  return (fetch_cmd (req_h_id, CCI_FETCH_SENSITIVE, err_buf));
}

int
cci_get_data (int req_h_id, int col_no, int a_type, void *value,
	      int *indicator)
{
  T_REQ_HANDLE *req_handle;
  T_CON_HANDLE *con_handle;
  int err_code = 0;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("cci_get_data %d %d %s", req_h_id, col_no,
		    dbg_a_type_str (a_type)));
#endif

  while (1)
    {
      MUTEX_LOCK (con_handle_table_mutex);

      req_handle = hm_find_req_handle (req_h_id, &con_handle);
      if (req_handle == NULL)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  return CCI_ER_REQ_HANDLE;
	}

      if (con_handle->ref_count > 0)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  SLEEP_MILISEC (0, 100);
	}
      else
	{
	  con_handle->ref_count = 1;
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  break;
	}
    }

  err_code = qe_get_data (req_handle, col_no, a_type, value, indicator);

  con_handle->ref_count = 0;

  return err_code;
}

int
cci_schema_info (int con_h_id, T_CCI_SCH_TYPE type, char *class_name,
		 char *attr_name, char flag, T_CCI_ERROR * err_buf)
{
  T_CON_HANDLE *con_handle;
  T_REQ_HANDLE *req_handle = NULL;
  int req_handle_id = -1;
  int err_code = 0;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("cci_schema_info %d %s %s %s %d", con_h_id,
		    dbg_sch_type_str (type), DEBUG_STR (class_name),
		    DEBUG_STR (attr_name), flag));
#endif

  err_buf_reset (err_buf);

  while (1)
    {
      MUTEX_LOCK (con_handle_table_mutex);

      con_handle = hm_find_con_handle (con_h_id);
      if (con_handle == NULL)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  return CCI_ER_CON_HANDLE;
	}

      if (con_handle->ref_count > 0)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  SLEEP_MILISEC (0, 100);
	}
      else
	{
	  con_handle->ref_count = 1;
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  break;
	}
    }

  err_code = 0;
  if (IS_OUT_TRAN_STATUS (con_handle))
    {
      err_code = cas_connect (con_handle, err_buf);
    }

  if (err_code < 0)
    {
      req_handle_id = err_code;
    }
  else
    {
      req_handle_id = hm_req_handle_alloc (con_h_id, &req_handle);
      if (req_handle_id >= 0)
	{
	  err_code = qe_schema_info (req_handle, con_handle->sock_fd,
				     (int) type, class_name, attr_name,
				     flag, err_buf);
	  if (err_code < 0)
	    {
	      hm_req_handle_free (con_handle, req_handle_id, req_handle);
	      req_handle_id = err_code;
	    }
	}
    }

  con_handle->ref_count = 0;
  con_handle->con_status = CCI_CON_STATUS_IN_TRAN;

  return req_handle_id;
}

int
cci_get_cur_oid (int req_h_id, char *oid_str_buf)
{
  T_REQ_HANDLE *req_handle;
  T_CON_HANDLE *con_handle;
  int err_code = 0;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("cci_get_cur_oid %d", req_h_id));
#endif

  while (1)
    {
      MUTEX_LOCK (con_handle_table_mutex);

      req_handle = hm_find_req_handle (req_h_id, &con_handle);
      if (req_handle == NULL)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  return CCI_ER_REQ_HANDLE;
	}

      if (con_handle->ref_count > 0)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  SLEEP_MILISEC (0, 100);
	}
      else
	{
	  con_handle->ref_count = 1;
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  break;
	}
    }

  err_code = qe_get_cur_oid (req_handle, oid_str_buf);

  con_handle->ref_count = 0;

  return err_code;
}

int
cci_oid_get (int con_h_id, char *oid_str, char **attr_name,
	     T_CCI_ERROR * err_buf)
{
  T_CON_HANDLE *con_handle;
  T_REQ_HANDLE *req_handle = NULL;
  int req_handle_id = -1;
  int err_code = 0;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("cci_oid_get %d", con_h_id));
#endif

  err_buf_reset (err_buf);

  while (1)
    {
      MUTEX_LOCK (con_handle_table_mutex);

      con_handle = hm_find_con_handle (con_h_id);
      if (con_handle == NULL)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  return CCI_ER_CON_HANDLE;
	}

      if (con_handle->ref_count > 0)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  SLEEP_MILISEC (0, 100);
	}
      else
	{
	  con_handle->ref_count = 1;
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  break;
	}
    }

  err_code = 0;
  if (IS_OUT_TRAN_STATUS (con_handle))
    {
      err_code = cas_connect (con_handle, err_buf);
    }

  if (err_code < 0)
    {
      req_handle_id = err_code;
    }
  else
    {
      req_handle_id = hm_req_handle_alloc (con_h_id, &req_handle);
      if (req_handle_id >= 0)
	{
	  err_code = qe_oid_get (req_handle, con_handle->sock_fd,
				 oid_str, attr_name, err_buf);
	  if (err_code < 0)
	    {
	      hm_req_handle_free (con_handle, req_handle_id, req_handle);
	      req_handle_id = err_code;
	    }
	}
    }

  con_handle->ref_count = 0;
  con_handle->con_status = CCI_CON_STATUS_IN_TRAN;

  return req_handle_id;
}

int
cci_oid_put (int con_h_id, char *oid_str, char **attr_name, char **new_val,
	     T_CCI_ERROR * err_buf)
{
  T_CON_HANDLE *con_handle;
  int err_code = 0;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("cci_oid_put %d", con_h_id));
#endif

  err_buf_reset (err_buf);

  while (1)
    {
      MUTEX_LOCK (con_handle_table_mutex);

      con_handle = hm_find_con_handle (con_h_id);
      if (con_handle == NULL)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  return CCI_ER_CON_HANDLE;
	}

      if (con_handle->ref_count > 0)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  SLEEP_MILISEC (0, 100);
	}
      else
	{
	  con_handle->ref_count = 1;
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  break;
	}
    }

  if (IS_OUT_TRAN_STATUS (con_handle))
    {
      err_code = cas_connect (con_handle, err_buf);
    }

  if (err_code >= 0)
    err_code =
      qe_oid_put (con_handle->sock_fd, oid_str, attr_name, new_val, err_buf);

  con_handle->ref_count = 0;
  con_handle->con_status = CCI_CON_STATUS_IN_TRAN;

  return err_code;
}

int
cci_oid_put2 (int con_h_id, char *oid_str, char **attr_name, void **new_val,
	      int *a_type, T_CCI_ERROR * err_buf)
{
  T_CON_HANDLE *con_handle;
  int err_code = 0;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("cci_oid_put2 %d", con_h_id));
#endif

  err_buf_reset (err_buf);

  while (1)
    {
      MUTEX_LOCK (con_handle_table_mutex);

      con_handle = hm_find_con_handle (con_h_id);
      if (con_handle == NULL)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  return CCI_ER_CON_HANDLE;
	}

      if (con_handle->ref_count > 0)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  SLEEP_MILISEC (0, 100);
	}
      else
	{
	  con_handle->ref_count = 1;
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  break;
	}
    }

  if (IS_OUT_TRAN_STATUS (con_handle))
    {
      err_code = cas_connect (con_handle, err_buf);
    }

  if (err_code >= 0)
    {
      err_code =
	qe_oid_put2 (con_handle->sock_fd, oid_str, attr_name, new_val, a_type,
		     err_buf);
    }

  con_handle->ref_count = 0;
  con_handle->con_status = CCI_CON_STATUS_IN_TRAN;

  return err_code;
}

int
cci_glo_new (int con_h_id, char *class_name, char *filename, char *oid_str,
	     T_CCI_ERROR * err_buf)
{
  T_CON_HANDLE *con_handle;
  int err_code = 0;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("cci_glo_new %d", con_h_id));
#endif

  err_buf_reset (err_buf);

  while (1)
    {
      MUTEX_LOCK (con_handle_table_mutex);

      con_handle = hm_find_con_handle (con_h_id);
      if (con_handle == NULL)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  return CCI_ER_CON_HANDLE;
	}

      if (con_handle->ref_count > 0)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  SLEEP_MILISEC (0, 100);
	}
      else
	{
	  con_handle->ref_count = 1;
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  break;
	}
    }

  if (IS_OUT_TRAN_STATUS (con_handle))
    {
      err_code = cas_connect (con_handle, err_buf);
    }

  if (err_code >= 0)
    err_code =
      qe_glo_new (con_handle, class_name, filename, oid_str, err_buf);

  con_handle->ref_count = 0;
  con_handle->con_status = CCI_CON_STATUS_IN_TRAN;

  return err_code;
}

int
cci_glo_save (int con_h_id, char *oid_str, char *filename,
	      T_CCI_ERROR * err_buf)
{
  T_CON_HANDLE *con_handle;
  int err_code = 0;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("cci_glo_save %d", con_h_id));
#endif

  err_buf_reset (err_buf);

  while (1)
    {
      MUTEX_LOCK (con_handle_table_mutex);

      con_handle = hm_find_con_handle (con_h_id);
      if (con_handle == NULL)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  return CCI_ER_CON_HANDLE;
	}

      if (con_handle->ref_count > 0)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  SLEEP_MILISEC (0, 100);
	}
      else
	{
	  con_handle->ref_count = 1;
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  break;
	}
    }

  if (IS_OUT_TRAN_STATUS (con_handle))
    {
      err_code = cas_connect (con_handle, err_buf);
    }

  if (err_code >= 0)
    err_code = qe_glo_save (con_handle, oid_str, filename, err_buf);

  con_handle->ref_count = 0;
  con_handle->con_status = CCI_CON_STATUS_IN_TRAN;

  return err_code;
}

int
cci_glo_load (int con_h_id, char *oid_str, int out_fd, T_CCI_ERROR * err_buf)
{
  T_CON_HANDLE *con_handle;
  int err_code = 0;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("cci_glo_load %d", con_h_id));
#endif

  err_buf_reset (err_buf);

  while (1)
    {
      MUTEX_LOCK (con_handle_table_mutex);

      con_handle = hm_find_con_handle (con_h_id);
      if (con_handle == NULL)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  return CCI_ER_CON_HANDLE;
	}

      if (con_handle->ref_count > 0)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  SLEEP_MILISEC (0, 100);
	}
      else
	{
	  con_handle->ref_count = 1;
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  break;
	}
    }

  if (IS_OUT_TRAN_STATUS (con_handle))
    {
      err_code = cas_connect (con_handle, err_buf);
    }

  if (err_code >= 0)
    err_code = qe_glo_load (con_handle, oid_str, out_fd, err_buf);

  con_handle->ref_count = 0;
  con_handle->con_status = CCI_CON_STATUS_IN_TRAN;

  return err_code;
}

int
cci_get_db_version (int con_h_id, char *out_buf, int buf_size)
{
  T_CON_HANDLE *con_handle;
  int err_code = 0;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("cci_get_db_version %d", con_h_id));
#endif

  if (out_buf && buf_size >= 1)
    out_buf[0] = '\0';

  while (1)
    {
      MUTEX_LOCK (con_handle_table_mutex);

      con_handle = hm_find_con_handle (con_h_id);
      if (con_handle == NULL)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  return CCI_ER_CON_HANDLE;
	}

      if (con_handle->ref_count > 0)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  SLEEP_MILISEC (0, 100);
	}
      else
	{
	  con_handle->ref_count = 1;
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  break;
	}
    }

  if (IS_OUT_TRAN_STATUS (con_handle))
    {
      err_code = cas_connect (con_handle, NULL);
    }

  if (err_code >= 0)
    {
      err_code = qe_get_db_version (con_handle, out_buf, buf_size);
    }

  con_handle->ref_count = 0;
  con_handle->con_status = CCI_CON_STATUS_IN_TRAN;

  return err_code;
}

int
cci_get_class_num_objs (int con_h_id, char *class_name, int flag,
			int *num_objs, int *num_pages, T_CCI_ERROR * err_buf)
{
  T_CON_HANDLE *con_handle;
  int err_code = 0;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("cci_get_class_num_objs %d", con_h_id));
#endif

  while (1)
    {
      MUTEX_LOCK (con_handle_table_mutex);

      con_handle = hm_find_con_handle (con_h_id);
      if (con_handle == NULL)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  return CCI_ER_CON_HANDLE;
	}

      if (con_handle->ref_count > 0)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  SLEEP_MILISEC (0, 100);
	}
      else
	{
	  con_handle->ref_count = 1;
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  break;
	}
    }

  if (IS_OUT_TRAN_STATUS (con_handle))
    {
      err_code = cas_connect (con_handle, err_buf);
    }

  if (err_code >= 0)
    {
      err_code = qe_get_class_num_objs (con_handle, class_name, (char) flag,
					num_objs, num_pages, err_buf);
    }

  con_handle->ref_count = 0;
  con_handle->con_status = CCI_CON_STATUS_IN_TRAN;

  return err_code;
}

int
cci_oid (int con_h_id, T_CCI_OID_CMD cmd, char *oid_str,
	 T_CCI_ERROR * err_buf)
{
  T_CON_HANDLE *con_handle;
  int err_code = 0;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("cci_oid %d %s", con_h_id, dbg_oid_cmd_str (cmd)));
#endif

  err_buf_reset (err_buf);

  while (1)
    {
      MUTEX_LOCK (con_handle_table_mutex);

      con_handle = hm_find_con_handle (con_h_id);
      if (con_handle == NULL)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  return CCI_ER_CON_HANDLE;
	}

      if (con_handle->ref_count > 0)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  SLEEP_MILISEC (0, 100);
	}
      else
	{
	  con_handle->ref_count = 1;
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  break;
	}
    }

  if (IS_OUT_TRAN_STATUS (con_handle))
    {
      err_code = cas_connect (con_handle, err_buf);
    }

  if (err_code >= 0)
    {
      err_code =
	qe_oid_cmd (con_handle->sock_fd, (char) cmd, oid_str, NULL, 0,
		    err_buf);
    }

  con_handle->ref_count = 0;
  con_handle->con_status = CCI_CON_STATUS_IN_TRAN;

  return err_code;
}

int
cci_oid_get_class_name (int con_h_id, char *oid_str, char *out_buf,
			int out_buf_size, T_CCI_ERROR * err_buf)
{
  T_CON_HANDLE *con_handle;
  int err_code = 0;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("cci_oid_get_class_name %d", con_h_id));
#endif

  err_buf_reset (err_buf);

  while (1)
    {
      MUTEX_LOCK (con_handle_table_mutex);

      con_handle = hm_find_con_handle (con_h_id);
      if (con_handle == NULL)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  return CCI_ER_CON_HANDLE;
	}

      if (con_handle->ref_count > 0)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  SLEEP_MILISEC (0, 100);
	}
      else
	{
	  con_handle->ref_count = 1;
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  break;
	}
    }

  if (IS_OUT_TRAN_STATUS (con_handle))
    {
      err_code = cas_connect (con_handle, err_buf);
    }

  if (err_code >= 0)
    {
      err_code =
	qe_oid_cmd (con_handle->sock_fd, (char) CCI_OID_CLASS_NAME, oid_str,
		    out_buf, out_buf_size, err_buf);
    }

  con_handle->ref_count = 0;
  con_handle->con_status = CCI_CON_STATUS_IN_TRAN;

  return err_code;
}

int
cci_col_get (int con_h_id, char *oid_str, char *col_attr, int *col_size,
	     int *col_type, T_CCI_ERROR * err_buf)
{
  int err_code = 0;
  int req_handle_id = -1;
  T_CON_HANDLE *con_handle = NULL;
  T_REQ_HANDLE *req_handle = NULL;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("cci_col_get %d", con_h_id));
#endif

  err_buf_reset (err_buf);

  while (1)
    {
      MUTEX_LOCK (con_handle_table_mutex);

      con_handle = hm_find_con_handle (con_h_id);
      if (con_handle == NULL)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  return CCI_ER_CON_HANDLE;
	}

      if (con_handle->ref_count > 0)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  SLEEP_MILISEC (0, 100);
	}
      else
	{
	  con_handle->ref_count = 1;
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  break;
	}
    }

  if (IS_OUT_TRAN_STATUS (con_handle))
    {
      err_code = cas_connect (con_handle, err_buf);
    }

  if (err_code >= 0)
    {
      req_handle_id = hm_req_handle_alloc (con_h_id, &req_handle);
      if (req_handle_id < 0)
	err_code = req_handle_id;
      else
	{
	  err_code = qe_col_get (req_handle, con_handle->sock_fd, oid_str,
				 col_attr, col_size, col_type, err_buf);
	  if (err_code < 0)
	    hm_req_handle_free (con_handle, req_handle_id, req_handle);
	  else
	    err_code = req_handle_id;
	}
    }

  con_handle->ref_count = 0;
  con_handle->con_status = CCI_CON_STATUS_IN_TRAN;

  return err_code;
}

int
cci_col_size (int con_h_id, char *oid_str, char *col_attr, int *col_size,
	      T_CCI_ERROR * err_buf)
{
  int err_code = 0;
  T_CON_HANDLE *con_handle = NULL;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("cci_col_size %d", con_h_id));
#endif

  err_buf_reset (err_buf);

  while (1)
    {
      MUTEX_LOCK (con_handle_table_mutex);

      con_handle = hm_find_con_handle (con_h_id);
      if (con_handle == NULL)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  return CCI_ER_CON_HANDLE;
	}

      if (con_handle->ref_count > 0)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  SLEEP_MILISEC (0, 100);
	}
      else
	{
	  con_handle->ref_count = 1;
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  break;
	}
    }

  if (IS_OUT_TRAN_STATUS (con_handle))
    {
      err_code = cas_connect (con_handle, err_buf);
    }

  if (err_code >= 0)
    {
      err_code =
	qe_col_size (con_handle->sock_fd, oid_str, col_attr, col_size,
		     err_buf);
    }

  con_handle->ref_count = 0;
  con_handle->con_status = CCI_CON_STATUS_IN_TRAN;

  return err_code;
}

int
cci_col_set_drop (int con_h_id, char *oid_str, char *col_attr, char *value,
		  T_CCI_ERROR * err_buf)
{
#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("cci_col_set_drop %d", con_h_id));
#endif

  return (col_set_add_drop
	  (con_h_id, CCI_COL_SET_DROP, oid_str, col_attr, value, err_buf));
}

int
cci_col_set_add (int con_h_id, char *oid_str, char *col_attr, char *value,
		 T_CCI_ERROR * err_buf)
{
#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("cci_col_set_add %d", con_h_id));
#endif

  return (col_set_add_drop
	  (con_h_id, CCI_COL_SET_ADD, oid_str, col_attr, value, err_buf));
}

int
cci_col_seq_drop (int con_h_id, char *oid_str, char *col_attr, int index,
		  T_CCI_ERROR * err_buf)
{
#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("cci_coil_seq_drop %d", con_h_id));
#endif

  return (col_seq_op
	  (con_h_id, CCI_COL_SEQ_DROP, oid_str, col_attr, index, "",
	   err_buf));
}

int
cci_col_seq_insert (int con_h_id, char *oid_str, char *col_attr, int index,
		    char *value, T_CCI_ERROR * err_buf)
{
#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("cci_col_seq_insert %d", con_h_id));
#endif

  return (col_seq_op
	  (con_h_id, CCI_COL_SEQ_INSERT, oid_str, col_attr, index, value,
	   err_buf));
}

int
cci_col_seq_put (int con_h_id, char *oid_str, char *col_attr, int index,
		 char *value, T_CCI_ERROR * err_buf)
{
#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("cci_col_seq_put %d", con_h_id));
#endif

  return (col_seq_op
	  (con_h_id, CCI_COL_SEQ_PUT, oid_str, col_attr, index, value,
	   err_buf));
}

int
cci_query_result_free (T_CCI_QUERY_RESULT * qr, int num_q)
{
#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("cci_query_result_free"));
#endif

  qe_query_result_free (num_q, qr);
  return 0;
}

int
cci_cursor_update (int req_h_id, int cursor_pos, int index,
		   T_CCI_A_TYPE a_type, void *value, T_CCI_ERROR * err_buf)
{
  int err_code = 0;
  T_REQ_HANDLE *req_handle;
  T_CON_HANDLE *con_handle;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("cci_cursor_update %d %d %d %s %p", req_h_id, cursor_pos,
		    index, dbg_a_type_str (a_type), value));
#endif

  err_buf_reset (err_buf);

  while (1)
    {
      MUTEX_LOCK (con_handle_table_mutex);

      req_handle = hm_find_req_handle (req_h_id, &con_handle);
      if (req_handle == NULL)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  return CCI_ER_REQ_HANDLE;
	}

      if (con_handle->ref_count > 0)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  SLEEP_MILISEC (0, 100);
	}
      else
	{
	  con_handle->ref_count = 1;
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  break;
	}
    }

  err_code =
    qe_cursor_update (req_handle, con_handle->sock_fd, cursor_pos, index,
		      a_type, value, err_buf);

  con_handle->ref_count = 0;

  return err_code;
}

int
cci_execute_batch (int con_h_id, int num_query, char **sql_stmt,
		   T_CCI_QUERY_RESULT ** qr, T_CCI_ERROR * err_buf)
{
  T_CON_HANDLE *con_handle = NULL;
  int err_code = 0;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("cci_execute_batch %d %d", con_h_id, num_query));
#endif

  err_buf_reset (err_buf);
  *qr = NULL;

  if (num_query <= 0)
    return 0;

  while (1)
    {
      MUTEX_LOCK (con_handle_table_mutex);

      con_handle = hm_find_con_handle (con_h_id);
      if (con_handle == NULL)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  return CCI_ER_CON_HANDLE;
	}

      if (con_handle->ref_count > 0)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  SLEEP_MILISEC (0, 100);
	}
      else
	{
	  con_handle->ref_count = 1;
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  break;
	}
    }

  if (IS_OUT_TRAN_STATUS (con_handle))
    {
      err_code = cas_connect (con_handle, err_buf);
    }

  if (err_code >= 0)
    {
      err_code =
	qe_execute_batch (con_handle->sock_fd, num_query, sql_stmt, qr,
			  err_buf);
    }

  con_handle->ref_count = 0;
  con_handle->con_status = CCI_CON_STATUS_IN_TRAN;

  return err_code;
}

int
cci_fetch_buffer_clear (int req_h_id)
{
  T_REQ_HANDLE *req_handle;
  T_CON_HANDLE *con_handle;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("cci_fetch_buffer_clear %d", req_h_id));
#endif

  while (1)
    {
      MUTEX_LOCK (con_handle_table_mutex);

      req_handle = hm_find_req_handle (req_h_id, &con_handle);

      if (req_handle == NULL)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  return CCI_ER_REQ_HANDLE;
	}

      if (con_handle->ref_count > 0)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  SLEEP_MILISEC (0, 100);
	}
      else
	{
	  con_handle->ref_count = 1;
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  break;
	}
    }

  hm_req_handle_fetch_buf_free (req_handle);

  con_handle->ref_count = 0;

  return 0;
}

int
cci_execute_result (int req_h_id, T_CCI_QUERY_RESULT ** qr,
		    T_CCI_ERROR * err_buf)
{
  T_REQ_HANDLE *req_handle = NULL;
  T_CON_HANDLE *con_handle;
  int err_code = 0;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("cci_execute_result %d", req_h_id));
#endif

  err_buf_reset (err_buf);
  *qr = NULL;

  while (1)
    {
      MUTEX_LOCK (con_handle_table_mutex);

      req_handle = hm_find_req_handle (req_h_id, &con_handle);
      if (req_handle == NULL)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  return CCI_ER_REQ_HANDLE;
	}

      if (con_handle->ref_count > 0)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  SLEEP_MILISEC (0, 100);
	}
      else
	{
	  con_handle->ref_count = 1;
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  break;
	}
    }

  err_code = qe_query_result_copy (req_handle, qr);

  con_handle->ref_count = 0;

  return err_code;
}

int
cci_set_isolation_level (int con_id, T_CCI_TRAN_ISOLATION val,
			 T_CCI_ERROR * err_buf)
{
  T_CON_HANDLE *con_handle;
  int err_code = 0;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("cci_set_isolation_level %d %s", con_id,
		    dbg_isolation_str));
#endif

  err_buf_reset (err_buf);

  if (val < TRAN_ISOLATION_MIN || val > TRAN_ISOLATION_MAX)
    return CCI_ER_ISOLATION_LEVEL;

  while (1)
    {
      MUTEX_LOCK (con_handle_table_mutex);

      con_handle = hm_find_con_handle (con_id);
      if (con_handle == NULL)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  return CCI_ER_CON_HANDLE;
	}

      if (con_handle->ref_count > 0)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  SLEEP_MILISEC (0, 100);
	}
      else
	{
	  con_handle->ref_count = 1;
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  break;
	}
    }

  con_handle->default_isolation_level = val;
  if (!IS_INVALID_SOCKET (con_handle->sock_fd))
    {
      err_code = qe_set_db_parameter (con_handle,
				      CCI_PARAM_ISOLATION_LEVEL,
				      &(con_handle->default_isolation_level),
				      err_buf);
    }

  con_handle->ref_count = 0;

  return err_code;
}

void
cci_set_free (T_CCI_SET set)
{
#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("cci_set_free %p", set));
#endif

  t_set_free ((T_SET *) set);
}

int
cci_set_size (T_CCI_SET set)
{
#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("cci_set_size %p", set));
#endif

  return (t_set_size ((T_SET *) set));
}

int
cci_set_element_type (T_CCI_SET set)
{
#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("cci_set_element_type %p", set));
#endif

  return (t_set_element_type ((T_SET *) set));
}

int
cci_set_get (T_CCI_SET set, int index, T_CCI_A_TYPE a_type, void *value,
	     int *ind)
{
#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("cci_set_get %p", set));
#endif

  return (t_set_get ((T_SET *) set, index, a_type, value, ind));
}

int
cci_set_make (T_CCI_SET * set, T_CCI_U_TYPE u_type, int size, void *value,
	      int *ind)
{
  T_SET *tmp_set;
  int err_code;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("cci_set_make"));
#endif

  if ((err_code = t_set_alloc (&tmp_set)) < 0)
    {
      return err_code;
    }

  if ((err_code = t_set_make (tmp_set, (char) u_type, size, value, ind)) < 0)
    {
      return err_code;
    }

  *set = tmp_set;
  return 0;
}

int
cci_get_attr_type_str (int con_h_id, char *class_name, char *attr_name,
		       char *buf, int buf_size, T_CCI_ERROR * err_buf)
{
  T_CON_HANDLE *con_handle;
  int err_code = 0;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("cci_get_attr_type_str %d", con_h_id));
#endif

  err_buf_reset (err_buf);

  while (1)
    {
      MUTEX_LOCK (con_handle_table_mutex);

      con_handle = hm_find_con_handle (con_h_id);
      if (con_handle == NULL)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  return CCI_ER_CON_HANDLE;
	}

      if (con_handle->ref_count > 0)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  SLEEP_MILISEC (0, 100);
	}
      else
	{
	  con_handle->ref_count = 1;
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  break;
	}
    }

  if (IS_OUT_TRAN_STATUS (con_handle))
    {
      err_code = cas_connect (con_handle, err_buf);
    }

  if (err_code >= 0)
    {
      err_code =
	qe_get_attr_type_str (con_handle->sock_fd, class_name, attr_name, buf,
			      buf_size, err_buf);
    }

  con_handle->ref_count = 0;
  con_handle->con_status = CCI_CON_STATUS_IN_TRAN;

  return err_code;
}

int
cci_get_query_plan (int req_h_id, char **out_buf)
{
#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("cci_get_query_plan %d", req_h_id));
#endif

  return (get_query_info (req_h_id, CAS_GET_QUERY_INFO_PLAN, out_buf));
}

int
cci_get_query_histogram (int req_h_id, char **out_buf)
{
#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("cci_get_query_histogram %d", req_h_id));
#endif

  return (get_query_info (req_h_id, CAS_GET_QUERY_INFO_HISTOGRAM, out_buf));
}

int
cci_query_info_free (char *out_buf)
{
#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("cci_query_info_free"));
#endif

  FREE_MEM (out_buf);
  return 0;
}

int
cci_set_max_row (int req_h_id, int max_row)
{
  T_REQ_HANDLE *req_handle;
  int err_code = 0;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("cci_set_max_row %d %d", req_h_id, max_row));
#endif

  MUTEX_LOCK (con_handle_table_mutex);

  req_handle = hm_find_req_handle (req_h_id, NULL);
  if (req_handle == NULL)
    err_code = CCI_ER_REQ_HANDLE;
  else
    req_handle->max_row = max_row;

  MUTEX_UNLOCK (con_handle_table_mutex);

  return err_code;
}

int
cci_savepoint (int con_h_id, T_CCI_SAVEPOINT_CMD cmd, char *savepoint_name,
	       T_CCI_ERROR * err_buf)
{
  T_CON_HANDLE *con_handle;
  int err_code = 0;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("cci_savepoint %d", con_h_id));
#endif

  err_buf_reset (err_buf);

  while (1)
    {
      MUTEX_LOCK (con_handle_table_mutex);

      con_handle = hm_find_con_handle (con_h_id);
      if (con_handle == NULL)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  return CCI_ER_CON_HANDLE;
	}

      if (con_handle->ref_count > 0)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  SLEEP_MILISEC (0, 100);
	}
      else
	{
	  con_handle->ref_count = 1;
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  break;
	}
    }

  if (IS_INVALID_SOCKET (con_handle->sock_fd))
    {
      err_code = 0;
    }
  else
    {
      err_code =
	qe_savepoint_cmd (con_handle->sock_fd, (char) cmd, savepoint_name,
			  err_buf);
    }

  con_handle->ref_count = 0;

  return err_code;
}

int
cci_get_param_info (int req_h_id, T_CCI_PARAM_INFO ** param,
		    T_CCI_ERROR * err_buf)
{
  T_REQ_HANDLE *req_handle;
  T_CON_HANDLE *con_handle;
  int err_code = 0;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("cci_get_param_info %d", req_h_id));
#endif

  err_buf_reset (err_buf);

  while (1)
    {
      MUTEX_LOCK (con_handle_table_mutex);

      req_handle = hm_find_req_handle (req_h_id, &con_handle);
      if (req_handle == NULL)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  return CCI_ER_REQ_HANDLE;
	}

      if (con_handle->ref_count > 0)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  SLEEP_MILISEC (0, 100);
	}
      else
	{
	  con_handle->ref_count = 1;
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  break;
	}
    }

  if (param)
    *param = NULL;

  err_code =
    qe_get_param_info (req_handle, con_handle->sock_fd, param, err_buf);

  con_handle->ref_count = 0;

  return err_code;
}

int
cci_param_info_free (T_CCI_PARAM_INFO * param)
{
#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("cci_param_info_free"));
#endif

  qe_param_info_free (param);
  return 0;
}

int
cci_glo_read_data (int con_h_id, char *oid_str, int start_pos, int length,
		   char *buf, T_CCI_ERROR * err_buf)
{
  char glo_cmd = GLO_CMD_READ_DATA;
  int ret_val;
  T_NET_BUF net_buf;
  char *result_msg = NULL;
  int result_msg_size = 0;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("cci_glo_read_data %d", con_h_id));
#endif

  if ((ret_val = glo_cmd_init (&net_buf, glo_cmd, oid_str, err_buf)) < 0)
    return ret_val;

  ADD_ARG_INT (&net_buf, start_pos);
  ADD_ARG_INT (&net_buf, length);

  ret_val =
    glo_cmd_common (con_h_id, &net_buf, &result_msg, &result_msg_size,
		    err_buf);
  net_buf_clear (&net_buf);
  if (ret_val < 0)
    return ret_val;

  if (result_msg_size - 4 < ret_val)
    {
      FREE_MEM (result_msg);
      return CCI_ER_COMMUNICATION;
    }

  if (buf)
    {
      memcpy (buf, result_msg + 4, ret_val);
    }

  FREE_MEM (result_msg);
  return ret_val;
}

int
cci_glo_write_data (int con_h_id, char *oid_str, int start_pos, int length,
		    char *buf, T_CCI_ERROR * err_buf)
{
  char glo_cmd = GLO_CMD_WRITE_DATA;
  int ret_val;
  T_NET_BUF net_buf;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("cci_glo_write_data %d", con_h_id));
#endif

  if ((ret_val = glo_cmd_init (&net_buf, glo_cmd, oid_str, err_buf)) < 0)
    return ret_val;

  ADD_ARG_INT (&net_buf, start_pos);
  ADD_ARG_BYTES (&net_buf, buf, length);

  ret_val = glo_cmd_common (con_h_id, &net_buf, NULL, NULL, err_buf);
  net_buf_clear (&net_buf);
  return ret_val;
}

int
cci_glo_insert_data (int con_h_id, char *oid_str, int start_pos, int length,
		     char *buf, T_CCI_ERROR * err_buf)
{
  char glo_cmd = GLO_CMD_INSERT_DATA;
  int ret_val;
  T_NET_BUF net_buf;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("cci_glo_insert_data %d", con_h_id));
#endif

  if ((ret_val = glo_cmd_init (&net_buf, glo_cmd, oid_str, err_buf)) < 0)
    return ret_val;

  ADD_ARG_INT (&net_buf, start_pos);
  ADD_ARG_BYTES (&net_buf, buf, length);

  ret_val = glo_cmd_common (con_h_id, &net_buf, NULL, NULL, err_buf);
  net_buf_clear (&net_buf);
  return ret_val;
}

int
cci_glo_delete_data (int con_h_id, char *oid_str, int start_pos, int length,
		     T_CCI_ERROR * err_buf)
{
  char glo_cmd = GLO_CMD_DELETE_DATA;
  int ret_val;
  T_NET_BUF net_buf;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("cci_glo_delete_data %d", con_h_id));
#endif

  if ((ret_val = glo_cmd_init (&net_buf, glo_cmd, oid_str, err_buf)) < 0)
    return ret_val;

  ADD_ARG_INT (&net_buf, start_pos);
  ADD_ARG_INT (&net_buf, length);

  ret_val = glo_cmd_common (con_h_id, &net_buf, NULL, NULL, err_buf);
  net_buf_clear (&net_buf);
  return ret_val;
}

int
cci_glo_truncate_data (int con_h_id, char *oid_str, int start_pos,
		       T_CCI_ERROR * err_buf)
{
  char glo_cmd = GLO_CMD_TRUNCATE_DATA;
  int ret_val;
  T_NET_BUF net_buf;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("cci_glo_truncate_data %d", con_h_id));
#endif

  if ((ret_val = glo_cmd_init (&net_buf, glo_cmd, oid_str, err_buf)) < 0)
    return ret_val;

  ADD_ARG_INT (&net_buf, start_pos);

  ret_val = glo_cmd_common (con_h_id, &net_buf, NULL, NULL, err_buf);
  net_buf_clear (&net_buf);
  return ret_val;
}

int
cci_glo_append_data (int con_h_id, char *oid_str, int length, char *buf,
		     T_CCI_ERROR * err_buf)
{
  char glo_cmd = GLO_CMD_APPEND_DATA;
  int ret_val;
  T_NET_BUF net_buf;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("cci_glo_append_data %d", con_h_id));
#endif

  if ((ret_val = glo_cmd_init (&net_buf, glo_cmd, oid_str, err_buf)) < 0)
    return ret_val;

  ADD_ARG_BYTES (&net_buf, buf, length);

  ret_val = glo_cmd_common (con_h_id, &net_buf, NULL, NULL, err_buf);
  net_buf_clear (&net_buf);
  return ret_val;
}

int
cci_glo_data_size (int con_h_id, char *oid_str, T_CCI_ERROR * err_buf)
{
  char glo_cmd = GLO_CMD_DATA_SIZE;
  int ret_val;
  T_NET_BUF net_buf;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("cci_glo_data_size %d", con_h_id));
#endif

  if ((ret_val = glo_cmd_init (&net_buf, glo_cmd, oid_str, err_buf)) < 0)
    return ret_val;

  ret_val = glo_cmd_common (con_h_id, &net_buf, NULL, NULL, err_buf);
  net_buf_clear (&net_buf);
  return ret_val;
}

int
cci_glo_compress_data (int con_h_id, char *oid_str, T_CCI_ERROR * err_buf)
{
  char glo_cmd = GLO_CMD_COMPRESS_DATA;
  int ret_val;
  T_NET_BUF net_buf;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("cci_glo_compress_data %d", con_h_id));
#endif

  if ((ret_val = glo_cmd_init (&net_buf, glo_cmd, oid_str, err_buf)) < 0)
    return ret_val;

  ret_val = glo_cmd_common (con_h_id, &net_buf, NULL, NULL, err_buf);
  net_buf_clear (&net_buf);
  return ret_val;
}

int
cci_glo_destroy_data (int con_h_id, char *oid_str, T_CCI_ERROR * err_buf)
{
  char glo_cmd = GLO_CMD_DESTROY_DATA;
  int ret_val;
  T_NET_BUF net_buf;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("cci_glo_destroy_data %d", con_h_id));
#endif

  if ((ret_val = glo_cmd_init (&net_buf, glo_cmd, oid_str, err_buf)) < 0)
    return ret_val;

  ret_val = glo_cmd_common (con_h_id, &net_buf, NULL, NULL, err_buf);
  net_buf_clear (&net_buf);
  return ret_val;
}

int
cci_glo_like_search (int con_h_id, char *oid_str, int start_pos,
		     char *search_str, int *offset, int *cur_pos,
		     T_CCI_ERROR * err_buf)
{
  char glo_cmd = GLO_CMD_LIKE_SEARCH;
  int err_code;
  T_NET_BUF net_buf;
  char *result_msg = NULL;
  int result_msg_size = 0;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("cci_glo_like_search %d", con_h_id));
#endif

  if ((err_code = glo_cmd_init (&net_buf, glo_cmd, oid_str, err_buf)) < 0)
    return err_code;

  ADD_ARG_INT (&net_buf, start_pos);
  ADD_ARG_BYTES (&net_buf, search_str, strlen (search_str) + 1);

  err_code =
    glo_cmd_common (con_h_id, &net_buf, &result_msg, &result_msg_size,
		    err_buf);
  net_buf_clear (&net_buf);
  if (err_code < 0)
    return err_code;

  if (result_msg_size - 4 < 8)
    {
      FREE_MEM (result_msg);
      return CCI_ER_COMMUNICATION;
    }

  NET_STR_TO_INT (*offset, result_msg + 4);
  NET_STR_TO_INT (*cur_pos, result_msg + 8);
  FREE_MEM (result_msg);
  return 0;
}

int
cci_glo_reg_search (int con_h_id, char *oid_str, int start_pos,
		    char *search_str, int *offset, int *cur_pos,
		    T_CCI_ERROR * err_buf)
{
  char glo_cmd = GLO_CMD_REG_SEARCH;
  int err_code;
  T_NET_BUF net_buf;
  char *result_msg = NULL;
  int result_msg_size = 0;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("cci_glo_reg_search %d", con_h_id));
#endif

  if ((err_code = glo_cmd_init (&net_buf, glo_cmd, oid_str, err_buf)) < 0)
    return err_code;

  ADD_ARG_INT (&net_buf, start_pos);
  ADD_ARG_BYTES (&net_buf, search_str, strlen (search_str) + 1);

  err_code =
    glo_cmd_common (con_h_id, &net_buf, &result_msg, &result_msg_size,
		    err_buf);
  net_buf_clear (&net_buf);
  if (err_code < 0)
    return err_code;

  if (result_msg_size - 4 < 8)
    {
      FREE_MEM (result_msg);
      return CCI_ER_COMMUNICATION;
    }

  NET_STR_TO_INT (*offset, result_msg + 4);
  NET_STR_TO_INT (*cur_pos, result_msg + 8);
  FREE_MEM (result_msg);
  return 0;
}

int
cci_glo_binary_search (int con_h_id, char *oid_str, int start_pos, int length,
		       char *search_array, int *offset, int *cur_pos,
		       T_CCI_ERROR * err_buf)
{
  char glo_cmd = GLO_CMD_BINARY_SEARCH;
  int err_code;
  T_NET_BUF net_buf;
  char *result_msg = NULL;
  int result_msg_size = 0;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("cci_glo_binary_search %d", con_h_id));
#endif

  if ((err_code = glo_cmd_init (&net_buf, glo_cmd, oid_str, err_buf)) < 0)
    return err_code;

  ADD_ARG_INT (&net_buf, start_pos);
  ADD_ARG_BYTES (&net_buf, search_array, length);

  err_code =
    glo_cmd_common (con_h_id, &net_buf, &result_msg, &result_msg_size,
		    err_buf);
  net_buf_clear (&net_buf);
  if (err_code < 0)
    return err_code;

  if (result_msg_size - 4 < 8)
    {
      FREE_MEM (result_msg);
      return CCI_ER_COMMUNICATION;
    }

  NET_STR_TO_INT (*offset, result_msg + 4);
  NET_STR_TO_INT (*cur_pos, result_msg + 8);
  FREE_MEM (result_msg);
  return 0;
}

#ifdef CCI_XA
int
cci_xa_prepare (int con_id, XID * xid, T_CCI_ERROR * err_buf)
{
  int err_code = 0;
  T_CON_HANDLE *con_handle;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("cci_xa_prepare %d", con_id));
#endif

  err_buf_reset (err_buf);

  while (1)
    {
      MUTEX_LOCK (con_handle_table_mutex);

      con_handle = hm_find_con_handle (con_id);
      if (con_handle == NULL)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  return CCI_ER_CON_HANDLE;
	}

      if (con_handle->ref_count > 0)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  SLEEP_MILISEC (0, 100);
	}
      else
	{
	  con_handle->ref_count = 1;
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  break;
	}
    }

  if (IS_OUT_TRAN_STATUS (con_handle))
    {
      err_code = cas_connect (con_handle, err_buf);
    }

  if (err_code >= 0)
    err_code = qe_xa_prepare (con_handle, xid, err_buf);

  con_handle->ref_count = 0;
  con_handle->con_status = CCI_CON_STATUS_IN_TRAN;

  return err_code;
}

int
cci_xa_recover (int con_id, XID * xid, int num_xid, T_CCI_ERROR * err_buf)
{
  int err_code = 0;
  T_CON_HANDLE *con_handle;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("cci_xa_recover %d", con_id));
#endif

  err_buf_reset (err_buf);

  while (1)
    {
      MUTEX_LOCK (con_handle_table_mutex);

      con_handle = hm_find_con_handle (con_id);
      if (con_handle == NULL)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  return CCI_ER_CON_HANDLE;
	}

      if (con_handle->ref_count > 0)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  SLEEP_MILISEC (0, 100);
	}
      else
	{
	  con_handle->ref_count = 1;
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  break;
	}
    }

  if (IS_OUT_TRAN_STATUS (con_handle))
    {
      err_code = cas_connect (con_handle, err_buf);
    }

  if (err_code >= 0)
    {
      err_code = qe_xa_recover (con_handle, xid, num_xid, err_buf);
    }

  con_handle->ref_count = 0;
  con_handle->con_status = CCI_CON_STATUS_IN_TRAN;

  return err_code;
}

int
cci_xa_end_tran (int con_id, XID * xid, char type, T_CCI_ERROR * err_buf)
{
  int err_code = 0;
  T_CON_HANDLE *con_handle;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("cci_xa_end_tran %d", con_id));
#endif

  err_buf_reset (err_buf);

  while (1)
    {
      MUTEX_LOCK (con_handle_table_mutex);

      con_handle = hm_find_con_handle (con_id);
      if (con_handle == NULL)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  return CCI_ER_CON_HANDLE;
	}

      if (con_handle->ref_count > 0)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  SLEEP_MILISEC (0, 100);
	}
      else
	{
	  con_handle->ref_count = 1;
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  break;
	}
    }

  if (IS_OUT_TRAN_STATUS (con_handle))
    {
      err_code = cas_connect (con_handle, err_buf);
    }

  if (err_code >= 0)
    err_code = qe_xa_end_tran (con_handle, xid, type, err_buf);

  con_handle->ref_count = 0;
  con_handle->con_status = CCI_CON_STATUS_IN_TRAN;

  return err_code;
}
#endif


int
cci_get_dbms_type (int con_h_id)
{
  T_CON_HANDLE *con_handle;
  int dbms_type;

  MUTEX_LOCK (con_handle_table_mutex);

  con_handle = hm_find_con_handle (con_h_id);
  if (con_handle == NULL)
    {
      dbms_type = CCI_ER_CON_HANDLE;
    }
  else
    {
      dbms_type = BROKER_INFO_DBMS_TYPE (con_handle->broker_info);
    }

  MUTEX_UNLOCK (con_handle_table_mutex);

  return dbms_type;
}


static const char *
cci_get_err_msg_low (int err_code)
{
  switch (err_code)
    {
    case CCI_ER_DBMS:
    case CAS_ER_DBMS:
      return "CUBRID DBMS Error";

    case CCI_ER_CON_HANDLE:
      return "Invalid connection handle";

    case CCI_ER_NO_MORE_MEMORY:
      return "Memory allocation error";

    case CCI_ER_COMMUNICATION:
      return "Cannot communicate with server";

    case CCI_ER_NO_MORE_DATA:
      return "Invalid cursor position";

    case CCI_ER_TRAN_TYPE:
      return "Unknown transaction type";

    case CCI_ER_STRING_PARAM:
      return "Invalid string argument";

    case CCI_ER_TYPE_CONVERSION:
      return "Type conversion error";

    case CCI_ER_BIND_INDEX:
      return "Parameter index is out of range";

    case CCI_ER_ATYPE:
      return "Invalid T_CCI_A_TYPE value";

    case CCI_ER_NOT_BIND:
      return "Not used";

    case CCI_ER_PARAM_NAME:
      return "Invalid T_CCI_DB_PARAM value";

    case CCI_ER_COLUMN_INDEX:
      return "Column index is out of range";

    case CCI_ER_SCHEMA_TYPE:
      return "Not used";

    case CCI_ER_FILE:
      return "Cannot open file";

    case CCI_ER_CONNECT:
      return "Cannot connect to CUBRID CAS";

    case CCI_ER_ALLOC_CON_HANDLE:
      return "Cannot allocate connection handle";

    case CCI_ER_REQ_HANDLE:
      return "Cannot allocate request handle";

    case CCI_ER_INVALID_CURSOR_POS:
      return "Invalid cursor position";

    case CCI_ER_OBJECT:
      return "Invalid oid string";

    case CCI_ER_CAS:
      return "Not used";

    case CCI_ER_HOSTNAME:
      return "Unknown host name";

    case CCI_ER_OID_CMD:
      return "Invalid T_CCI_OID_CMD value";

    case CCI_ER_BIND_ARRAY_SIZE:
      return "Array binding size is not specified";

    case CCI_ER_ISOLATION_LEVEL:
      return "Unknown transaction isolation level";

    case CCI_ER_SET_INDEX:
      return "Invalid set index";

    case CCI_ER_DELETED_TUPLE:
      return "Current row was deleted";

    case CCI_ER_SAVEPOINT_CMD:
      return "Invalid T_CCI_SAVEPOINT_CMD value";

    case CCI_ER_THREAD_RUNNING:
      return "Thread is running ";

    case CAS_ER_INTERNAL:
      return "Not used";

    case CAS_ER_NO_MORE_MEMORY:
      return "Memory allocation error";

    case CAS_ER_COMMUNICATION:
      return "Cannot receive data from client";

    case CAS_ER_ARGS:
      return "Invalid argument";

    case CAS_ER_TRAN_TYPE:
      return "Invalid transaction type argument";

    case CAS_ER_SRV_HANDLE:
      return "Server handle not found";

    case CAS_ER_NUM_BIND:
      return "Invalid parameter binding value argument";

    case CAS_ER_UNKNOWN_U_TYPE:
      return "Invalid T_CCI_U_TYPE value";

    case CAS_ER_DB_VALUE:
      return "Cannot make DB_VALUE";

    case CAS_ER_TYPE_CONVERSION:
      return "Type conversion error";

    case CAS_ER_PARAM_NAME:
      return "Invalid T_CCI_DB_PARAM value";

    case CAS_ER_NO_MORE_DATA:
      return "Invalid cursor position";

    case CAS_ER_OBJECT:
      return "Invalid oid";

    case CAS_ER_OPEN_FILE:
      return "Cannot open file";

    case CAS_ER_SCHEMA_TYPE:
      return "Invalid T_CCI_SCH_TYPE value";

    case CAS_ER_VERSION:
      return "Version mismatch";

    case CAS_ER_FREE_SERVER:
      return "Cannot process the request.  Try again later";

    case CAS_ER_NOT_AUTHORIZED_CLIENT:
      return "Authorization error";

    case CAS_ER_QUERY_CANCEL:
      return "Cannot cancel the query";

    case CAS_ER_NOT_COLLECTION:
      return "The attribute domain must be the set type";

    case CAS_ER_COLLECTION_DOMAIN:
      return "Heterogeneous set is not supported";

    case CAS_ER_NO_MORE_RESULT_SET:
      return "No More Result";

    case CAS_ER_INVALID_CALL_STMT:
      return "Illegal CALL statement";

    case CAS_ER_STMT_POOLING:
      return "Invalid plan";

    case CAS_ER_DBSERVER_DISCONNECTED:
      return "Cannot communicate with DB Server";

    case CAS_ER_IS:
      return "Not used";

    default:
      return NULL;
    }
}

/*
  Called by PRINT_CCI_ERROR()
*/
int
cci_get_error_msg (int err_code, T_CCI_ERROR * error, char *buf, int bufsize)
{
  const char *err_msg;


  if ((buf == NULL) || (bufsize <= 0))
    {
      return -1;
    }

  err_msg = cci_get_err_msg_low (err_code);
  if (err_msg == NULL)
    {
      return -1;
    }
  else
    {
      if ((err_code < CCI_ER_DBMS) && (err_code > CAS_ER_DBMS))
	{
	  snprintf (buf, bufsize, "Error : %s", err_msg);
	}
      else if ((err_code < CAS_ER_DBMS) && (err_code >= CAS_ER_IS))
	{
	  snprintf (buf, bufsize, "CUBRID CAS Error : %s", err_msg);
	}
      if ((err_code == CCI_ER_DBMS) || (err_code == CAS_ER_DBMS))
	{
	  if (error == NULL)
	    {
	      snprintf (buf, bufsize, "%s ", err_msg);
	    }
	  else
	    {
	      snprintf (buf, bufsize, "%s : (%d) %s", err_msg,
			error->err_code, error->err_msg);
	    }
	}
    }
  return 0;
}

/*
   Called by applications.
   They don't need prefix such as "ERROR :" or "CUBRID CAS ERROR".
 */
int
cci_get_err_msg (int err_code, char *buf, int bufsize)
{
  const char *err_msg;

  if ((buf == NULL) || (bufsize <= 0))
    {
      return -1;
    }

  err_msg = cci_get_err_msg_low (err_code);
  if (err_msg == NULL)
    {
      return -1;
    }
  else
    {
      snprintf (buf, bufsize, "%s", err_msg);
    }

  return 0;
}

/************************************************************************
 * IMPLEMENTATION OF PUBLIC FUNCTIONS	 				*
 ************************************************************************/

/************************************************************************
 * IMPLEMENTATION OF PRIVATE FUNCTIONS	 				*
 ************************************************************************/

static void
err_buf_reset (T_CCI_ERROR * err_buf)
{
  err_buf->err_code = 0;
  err_buf->err_msg[0] = '\0';
}

static int
col_set_add_drop (int con_h_id, char col_cmd, char *oid_str, char *col_attr,
		  char *value, T_CCI_ERROR * err_buf)
{
  int err_code = 0;
  T_CON_HANDLE *con_handle = NULL;

  err_buf_reset (err_buf);

  while (1)
    {
      MUTEX_LOCK (con_handle_table_mutex);

      con_handle = hm_find_con_handle (con_h_id);
      if (con_handle == NULL)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  return CCI_ER_CON_HANDLE;
	}

      if (con_handle->ref_count > 0)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  SLEEP_MILISEC (0, 100);
	}
      else
	{
	  con_handle->ref_count = 1;
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  break;
	}
    }

  if (IS_OUT_TRAN_STATUS (con_handle))
    {
      err_code = cas_connect (con_handle, err_buf);
    }

  if (err_code >= 0)
    {
      err_code =
	qe_col_set_add_drop (con_handle->sock_fd, col_cmd, oid_str, col_attr,
			     value, err_buf);
    }

  con_handle->ref_count = 0;
  con_handle->con_status = CCI_CON_STATUS_IN_TRAN;

  return err_code;
}

static int
col_seq_op (int con_h_id, char col_cmd, char *oid_str, char *col_attr,
	    int index, const char *value, T_CCI_ERROR * err_buf)
{
  int err_code = 0;
  T_CON_HANDLE *con_handle = NULL;

  err_buf_reset (err_buf);

  while (1)
    {
      MUTEX_LOCK (con_handle_table_mutex);

      con_handle = hm_find_con_handle (con_h_id);
      if (con_handle == NULL)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  return CCI_ER_CON_HANDLE;
	}

      if (con_handle->ref_count > 0)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  SLEEP_MILISEC (0, 100);
	}
      else
	{
	  con_handle->ref_count = 1;
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  break;
	}
    }

  if (IS_OUT_TRAN_STATUS (con_handle))
    {
      err_code = cas_connect (con_handle, err_buf);
    }

  if (err_code >= 0)
    {
      err_code =
	qe_col_seq_op (con_handle->sock_fd, col_cmd, oid_str, col_attr, index,
		       value, err_buf);
    }

  con_handle->ref_count = 0;
  con_handle->con_status = CCI_CON_STATUS_IN_TRAN;

  return err_code;
}

static int
fetch_cmd (int req_h_id, char flag, T_CCI_ERROR * err_buf)
{
  T_REQ_HANDLE *req_handle;
  T_CON_HANDLE *con_handle;
  int err_code = 0;
  int result_set_index = 0;

  err_buf_reset (err_buf);

  while (1)
    {
      MUTEX_LOCK (con_handle_table_mutex);

      req_handle = hm_find_req_handle (req_h_id, &con_handle);
      if (req_handle == NULL)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  return CCI_ER_REQ_HANDLE;
	}

      if (con_handle->ref_count > 0)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  SLEEP_MILISEC (0, 100);
	}
      else
	{
	  con_handle->ref_count = 1;
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  break;
	}
    }

  err_code =
    qe_fetch (req_handle, con_handle->sock_fd, flag, result_set_index,
	      err_buf);

  con_handle->ref_count = 0;

  return err_code;
}

static int
cas_connect (T_CON_HANDLE * con_handle, T_CCI_ERROR * err_buf)
{
  int connect;
  return cas_connect_with_ret (con_handle, err_buf, &connect);
}

static int
cas_connect_with_ret (T_CON_HANDLE * con_handle, T_CCI_ERROR * err_buf,
		      int *connect)
{
  int err_code;
  err_code = cas_connect_low (con_handle, err_buf, connect);

  /* req_handle_table should be managed by list too. */
  if ((*connect) &&
      (BROKER_INFO_STATEMENT_POOLING (con_handle->broker_info) ==
       CAS_STATEMENT_POOLING_ON))
    {

      hm_invalidate_all_req_handle (con_handle);
    }

  return err_code;
}


static int
cas_connect_low (T_CON_HANDLE * con_handle, T_CCI_ERROR * err_buf,
		 int *connect)
{
  SOCKET sock_fd;
  int error;

  *connect = 0;

  if (net_check_cas_request (con_handle->sock_fd) < 0)
    {
      if (!IS_INVALID_SOCKET (con_handle->sock_fd))
	{
	  CLOSE_SOCKET (con_handle->sock_fd);
	  con_handle->sock_fd = INVALID_SOCKET;
	}
    }

  if (!IS_INVALID_SOCKET (con_handle->sock_fd))
    {
      return CCI_ER_NO_ERROR;
    }


  error = net_connect_srv (con_handle->ip_addr,
			   con_handle->port,
			   con_handle->db_name,
			   con_handle->db_user,
			   con_handle->db_passwd,
			   con_handle->is_first,
			   err_buf, con_handle->broker_info,
			   &(con_handle->cas_pid), &sock_fd);


  if (error == CCI_ER_NO_ERROR && !IS_INVALID_SOCKET (sock_fd))
    {
      con_handle->is_first = 0;
      con_handle->sock_fd = sock_fd;
      con_handle->con_status = CCI_CON_STATUS_IN_TRAN;

      if (con_handle->default_isolation_level > 0)
	{
	  qe_set_db_parameter (con_handle,
			       CCI_PARAM_ISOLATION_LEVEL,
			       &(con_handle->default_isolation_level),
			       err_buf);
	}
    }

  *connect = 1;
  return error;
}

static int
get_query_info (int req_h_id, char log_type, char **out_buf)
{
  T_REQ_HANDLE *req_handle;
  T_CON_HANDLE *con_handle;
  int err_code = 0;

  while (1)
    {
      MUTEX_LOCK (con_handle_table_mutex);

      req_handle = hm_find_req_handle (req_h_id, &con_handle);
      if (req_handle == NULL)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  return CCI_ER_REQ_HANDLE;
	}

      if (con_handle->ref_count > 0)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  SLEEP_MILISEC (0, 100);
	}
      else
	{
	  con_handle->ref_count = 1;
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  break;
	}
    }

  err_code =
    qe_get_query_info (req_handle, con_handle->sock_fd, log_type, out_buf);

  con_handle->ref_count = 0;

  return err_code;
}

static int
next_result_cmd (int req_h_id, char flag, T_CCI_ERROR * err_buf)
{
  T_REQ_HANDLE *req_handle;
  T_CON_HANDLE *con_handle;
  int err_code = 0;

  err_buf_reset (err_buf);

  while (1)
    {
      MUTEX_LOCK (con_handle_table_mutex);

      req_handle = hm_find_req_handle (req_h_id, &con_handle);
      if (req_handle == NULL)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  return CCI_ER_REQ_HANDLE;
	}

      if (con_handle->ref_count > 0)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  SLEEP_MILISEC (0, 100);
	}
      else
	{
	  con_handle->ref_count = 1;
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  break;
	}
    }

  err_code = qe_next_result (req_handle, flag, con_handle->sock_fd, err_buf);

  con_handle->ref_count = 0;

  return err_code;
}

static int
glo_cmd_init (T_NET_BUF * net_buf, char glo_cmd, char *oid_str,
	      T_CCI_ERROR * err_buf)
{
  T_OBJECT oid;
  char func_code = CAS_FC_GLO_CMD;

  err_buf_reset (err_buf);

  if (ut_str_to_oid (oid_str, &oid) < 0)
    return CCI_ER_OBJECT;

  net_buf_init (net_buf);

  net_buf_cp_str (net_buf, &func_code, 1);

  ADD_ARG_BYTES (net_buf, &glo_cmd, 1);
  ADD_ARG_OBJECT (net_buf, &oid);

  return 0;
}

static int
glo_cmd_common (int con_h_id, T_NET_BUF * net_buf, char **result_msg,
		int *result_msg_size, T_CCI_ERROR * err_buf)
{
  T_CON_HANDLE *con_handle;
  int err_code = 0;

  while (1)
    {
      MUTEX_LOCK (con_handle_table_mutex);

      con_handle = hm_find_con_handle (con_h_id);
      if (con_handle == NULL)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  return CCI_ER_CON_HANDLE;
	}

      if (con_handle->ref_count > 0)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  SLEEP_MILISEC (0, 100);
	}
      else
	{
	  con_handle->ref_count = 1;
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  break;
	}
    }

  if (IS_OUT_TRAN_STATUS (con_handle))
    {
      err_code = cas_connect (con_handle, err_buf);
    }

  if (err_code >= 0)
    {
      err_code =
	glo_cmd_ex (con_handle->sock_fd, net_buf, result_msg, result_msg_size,
		    err_buf);
    }

  con_handle->ref_count = 0;
  con_handle->con_status = CCI_CON_STATUS_IN_TRAN;

  return err_code;
}

static int
glo_cmd_ex (SOCKET sock_fd, T_NET_BUF * net_buf, char **result_msg,
	    int *result_msg_size, T_CCI_ERROR * err_buf)
{
  int err_code;

  if (net_buf->err_code < 0)
    {
      return net_buf->err_code;
    }

  if ((err_code =
       net_send_msg (sock_fd, net_buf->data, net_buf->data_size)) < 0)
    return err_code;

  err_code = net_recv_msg (sock_fd, result_msg, result_msg_size, err_buf);
  return err_code;
}


#ifdef CCI_DEBUG
static void
print_debug_msg (const char *format, ...)
{
  va_list args;
  FILE *fp;
  char format_buf[128];
  static char *debug_file = "cci.log";

  sprintf (format_buf, "%s\n", format);

  va_start (args, format);

  fp = fopen (debug_file, "a");
  if (fp != NULL)
    {
      vfprintf (fp, format_buf, args);
      fclose (fp);
    }
  va_end (args);
}

static char *
dbg_tran_type_str (char type)
{
  switch (type)
    {
    case CCI_TRAN_COMMIT:
      return "COMMIT";
    case CCI_TRAN_ROLLBACK:
      return "ROLLBACK";
    default:
      return "***";
    }
}

static char *
dbg_a_type_str (T_CCI_A_TYPE atype)
{
  switch (atype)
    {
    case CCI_A_TYPE_STR:
      return "CCI_A_TYPE_STR";
    case CCI_A_TYPE_INT:
      return "CCI_A_TYPE_INT";
    case CCI_A_TYPE_BIGINT:
      return "CCI_A_TYPE_BIGINT";
    case CCI_A_TYPE_FLOAT:
      return "CCI_A_TYPE_FLOAT";
    case CCI_A_TYPE_DOUBLE:
      return "CCI_A_TYPE_DOUBLE";
    case CCI_A_TYPE_BIT:
      return "CCI_A_TYPE_BIT";
    case CCI_A_TYPE_DATE:
      return "CCI_A_TYPE_DATE";
    case CCI_A_TYPE_SET:
      return "CCI_A_TYPE_SET";
    default:
      return "***";
    }
}

static char *
dbg_u_type_str (T_CCI_U_TYPE utype)
{
  switch (utype)
    {
    case CCI_U_TYPE_NULL:
      return "CCI_U_TYPE_NULL";
    case CCI_U_TYPE_CHAR:
      return "CCI_U_TYPE_CHAR";
    case CCI_U_TYPE_STRING:
      return "CCI_U_TYPE_STRING";
    case CCI_U_TYPE_NCHAR:
      return "CCI_U_TYPE_NCHAR";
    case CCI_U_TYPE_VARNCHAR:
      return "CCI_U_TYPE_VARNCHAR";
    case CCI_U_TYPE_BIT:
      return "CCI_U_TYPE_BIT";
    case CCI_U_TYPE_VARBIT:
      return "CCI_U_TYPE_VARBIT";
    case CCI_U_TYPE_NUMERIC:
      return "CCI_U_TYPE_NUMERIC";
    case CCI_U_TYPE_BIGINT:
      return "CCI_U_TYPE_BIGINT";
    case CCI_U_TYPE_INT:
      return "CCI_U_TYPE_INT";
    case CCI_U_TYPE_SHORT:
      return "CCI_U_TYPE_SHORT";
    case CCI_U_TYPE_MONETARY:
      return "CCI_U_TYPE_MONETARY";
    case CCI_U_TYPE_FLOAT:
      return "CCI_U_TYPE_FLOAT";
    case CCI_U_TYPE_DOUBLE:
      return "CCI_U_TYPE_DOUBLE";
    case CCI_U_TYPE_DATE:
      return "CCI_U_TYPE_DATE";
    case CCI_U_TYPE_TIME:
      return "CCI_U_TYPE_TIME";
    case CCI_U_TYPE_TIMESTAMP:
      return "CCI_U_TYPE_TIMESTAMP";
    case CCI_U_TYPE_DATETIME:
      return "CCI_U_TYPE_DATETIME";
    case CCI_U_TYPE_SET:
      return "CCI_U_TYPE_SET";
    case CCI_U_TYPE_MULTISET:
      return "CCI_U_TYPE_MULTISET";
    case CCI_U_TYPE_SEQUENCE:
      return "CCI_U_TYPE_SEQUENCE";
    case CCI_U_TYPE_OBJECT:
      return "CCI_U_TYPE_OBJECT";
    default:
      return "***";
    }
}

static char *
dbg_db_param_str (T_CCI_DB_PARAM db_param)
{
  switch (db_param)
    {
    case CCI_PARAM_ISOLATION_LEVEL:
      return "CCI_PARAM_ISOLATION_LEVEL";
    case CCI_PARAM_LOCK_TIMEOUT:
      return "CCI_PARAM_LOCK_TIMEOUT";
    case CCI_PARAM_MAX_STRING_LENGTH:
      return "CCI_PARAM_MAX_STRING_LENGTH";
    default:
      return "***";
    }
}

static char *
dbg_cursor_pos_str (T_CCI_CURSOR_POS cursor_pos)
{
  switch (cursor_pos)
    {
    case CCI_CURSOR_FIRST:
      return "CCI_CURSOR_FIRST";
    case CCI_CURSOR_CURRENT:
      return "CCI_CURSOR_CURRENT";
    case CCI_CURSOR_LAST:
      return "CCI_CURSOR_LAST";
    default:
      return "***";
    }
}

static char *
dbg_sch_type_str (T_CCI_SCH_TYPE sch_type)
{
  switch (sch_type)
    {
    case CCI_SCH_CLASS:
      return "CCI_SCH_CLASS";
    case CCI_SCH_VCLASS:
      return "CCI_SCH_VCLASS";
    case CCI_SCH_QUERY_SPEC:
      return "CCI_SCH_QUERY_SPEC";
    case CCI_SCH_ATTRIBUTE:
      return "CCI_SCH_ATTRIBUTE";
    case CCI_SCH_CLASS_ATTRIBUTE:
      return "CCI_SCH_CLASS_ATTRIBUTE";
    case CCI_SCH_METHOD:
      return "CCI_SCH_METHOD";
    case CCI_SCH_CLASS_METHOD:
      return "CCI_SCH_CLASS_METHOD";
    case CCI_SCH_METHOD_FILE:
      return "CCI_SCH_METHOD_FILE";
    case CCI_SCH_SUPERCLASS:
      return "CCI_SCH_SUPERCLASS";
    case CCI_SCH_SUBCLASS:
      return "CCI_SCH_SUBCLASS";
    case CCI_SCH_CONSTRAINT:
      return "CCI_SCH_CONSTRAINT";
    case CCI_SCH_TRIGGER:
      return "CCI_SCH_TRIGGER";
    case CCI_SCH_CLASS_PRIVILEGE:
      return "CCI_SCH_CLASS_PRIVILEGE";
    case CCI_SCH_ATTR_PRIVILEGE:
      return "CCI_SCH_ATTR_PRIVILEGE";
    default:
      return "***";
    }
}

static char *
dbg_oid_cmd_str (T_CCI_OID_CMD oid_cmd)
{
  switch (oid_cmd)
    {
    case CCI_OID_DROP:
      return "CCI_OID_DROP";
    case CCI_OID_IS_INSTANCE:
      return "CCI_OID_IS_INSTANCE";
    case CCI_OID_LOCK_READ:
      return "CCI_OID_LOCK_READ";
    case CCI_OID_LOCK_WRITE:
      return "CCI_OID_LOCK_WRITE";
    case CCI_OID_CLASS_NAME:
      return "CCI_OID_CLASS_NAME";
    default:
      return "***";
    }
}

static char *
dbg_isolation_str (T_CCI_TRAN_ISOLATION isol_level)
{
  switch (isol_level)
    {
    case TRAN_COMMIT_CLASS_UNCOMMIT_INSTANCE:
      return "TRAN_COMMIT_CLASS_UNCOMMIT_INSTANCE";
    case TRAN_COMMIT_CLASS_COMMIT_INSTANCE:
      return "TRAN_COMMIT_CLASS_COMMIT_INSTANCE";
    case TRAN_REP_CLASS_UNCOMMIT_INSTANCE:
      return "TRAN_REP_CLASS_UNCOMMIT_INSTANCE";
    case TRAN_REP_CLASS_COMMIT_INSTANCE:
      return "TRAN_REP_CLASS_COMMIT_INSTANCE";
    case TRAN_REP_CLASS_REP_INSTANCE:
      return "TRAN_REP_CLASS_REP_INSTANCE";
    case TRAN_SERIALIZABLE:
      return "TRAN_SERIALIZABLE";
    default:
      return "***";
    }
}
#endif

static THREAD_FUNC
execute_thread (void *arg)
{
  T_EXEC_THR_ARG *exec_arg = (T_EXEC_THR_ARG *) arg;
  T_CON_HANDLE *curr_con_handle = (T_CON_HANDLE *) (exec_arg->con_handle);

  exec_arg->ret_code =
    qe_execute (exec_arg->req_handle, exec_arg->sock_fd, exec_arg->flag,
		exec_arg->max_col_size, &(exec_arg->err_buf));
  if (exec_arg->ret_code < 0
      && curr_con_handle->tran_status == CCI_TRAN_STATUS_START)
    {
      int con_err_code = 0;
      int connect_done;

      con_err_code =
	cas_connect_with_ret (curr_con_handle, &(exec_arg->err_buf),
			      &connect_done);
      if (con_err_code < 0)
	{
	  exec_arg->ret_code = con_err_code;
	  goto execute_end;
	}

      if (connect_done)
	{
	  req_handle_content_free (exec_arg->req_handle, 1);
	  exec_arg->ret_code = qe_prepare (exec_arg->req_handle,
					   curr_con_handle->sock_fd,
					   (exec_arg->req_handle)->sql_text,
					   (exec_arg->req_handle)->
					   prepare_flag, &(exec_arg->err_buf),
					   1);
	  if (exec_arg->ret_code < 0)
	    {
	      goto execute_end;
	    }
	  exec_arg->ret_code = qe_execute (exec_arg->req_handle,
					   exec_arg->sock_fd,
					   exec_arg->flag,
					   exec_arg->max_col_size,
					   &(exec_arg->err_buf));
	}
    }
  if (BROKER_INFO_STATEMENT_POOLING (curr_con_handle->broker_info) ==
      CAS_STATEMENT_POOLING_ON)
    {
      while (exec_arg->ret_code == CAS_ER_STMT_POOLING)
	{
	  req_handle_content_free (exec_arg->req_handle, 1);
	  exec_arg->ret_code =
	    qe_prepare (exec_arg->req_handle,
			curr_con_handle->sock_fd,
			exec_arg->req_handle->sql_text,
			exec_arg->req_handle->prepare_flag,
			&(exec_arg->err_buf), 1);
	  if (exec_arg->ret_code < 0)
	    {
	      goto execute_end;
	    }
	  exec_arg->ret_code = qe_execute (exec_arg->req_handle,
					   exec_arg->sock_fd,
					   exec_arg->flag,
					   exec_arg->max_col_size,
					   &(exec_arg->err_buf));
	}
    }

  if (curr_con_handle->tran_status == CCI_TRAN_STATUS_START)
    {
      curr_con_handle->tran_status = CCI_TRAN_STATUS_RUNNING;
    }

  curr_con_handle->con_status = CCI_CON_STATUS_IN_TRAN;

execute_end:
  if (exec_arg->ref_count_ptr)
    {
      *(exec_arg->ref_count_ptr) = 0;
    }

  THREAD_RETURN (NULL);
}

static int
get_thread_result (T_CON_HANDLE * con_handle, T_CCI_ERROR * err_buf)
{
  if (con_handle->ref_count > 0)
    return CCI_ER_THREAD_RUNNING;

  *err_buf = con_handle->thr_arg.err_buf;
  return con_handle->thr_arg.ret_code;
}

static int
connect_prepare_again (T_CON_HANDLE * con_handle, T_REQ_HANDLE * req_handle,
		       T_CCI_ERROR * err_buf)
{
  int err_code = 0;

  if (NEED_TO_CONNECT (con_handle))
    {
      err_code = cas_connect (con_handle, err_buf);
      if (err_code < 0)
	{
	  return err_code;
	}
    }

  if (!req_handle->valid)
    {
      req_handle_content_free (req_handle, 1);
      err_code =
	qe_prepare (req_handle, con_handle->sock_fd, req_handle->sql_text,
		    req_handle->prepare_flag, err_buf, 1);

      if (err_code < 0 && con_handle->tran_status == CCI_TRAN_STATUS_START)
	{
	  int connect_done;

	  err_code =
	    cas_connect_with_ret (con_handle, err_buf, &connect_done);
	  if (err_code < 0)
	    {
	      return err_code;
	    }
	  if (connect_done)
	    {
	      err_code = qe_prepare (req_handle,
				     con_handle->sock_fd,
				     req_handle->sql_text,
				     req_handle->prepare_flag, err_buf, 1);
	    }
	}
    }
  return err_code;
}
