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
#include <assert.h>
#include <sys/timeb.h>
#include <stdarg.h>

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
#include <sys/socket.h>
#endif


/************************************************************************
 * OTHER IMPORTED HEADER FILES						*
 ************************************************************************/

#if defined(WINDOWS)
#include "version.h"
#endif
#include "porting.h"
#include "cci_common.h"
#include "cas_cci.h"
#include "cci_handle_mng.h"
#include "cci_network.h"
#include "cci_query_execute.h"
#include "cci_t_set.h"
#include "cas_protocol.h"
#include "cci_net_buf.h"
#include "cci_util.h"
#include "cci_log.h"
#include "cci_map.h"
#include "error_code.h"

#if defined(WINDOWS)
int wsa_initialize ();
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

#define ELAPSED_MSECS(e, s) \
    ((e).tv_sec - (s).tv_sec) * 1000 + ((e).tv_usec - (s).tv_usec) / 1000

#define IS_OUT_TRAN_STATUS(CON_HANDLE) \
        (IS_INVALID_SOCKET((CON_HANDLE)->sock_fd) || \
         ((CON_HANDLE)->con_status == CCI_CON_STATUS_OUT_TRAN))

#define IS_BROKER_STMT_POOL(c) \
  ((c)->broker_info[BROKER_INFO_STATEMENT_POOLING] == CAS_STATEMENT_POOLING_ON)
#define IS_OUT_TRAN(c) ((c)->con_status == CCI_CON_STATUS_OUT_TRAN)
#define IS_IN_TRAN(c) ((c)->con_status == CCI_CON_STATUS_IN_TRAN)
#define IS_FORCE_FAILBACK(c) ((c)->force_failback == 1)
#define IS_ER_COMMUNICATION(e) \
  ((e) == CCI_ER_COMMUNICATION || (e) == CAS_ER_COMMUNICATION)
#define IS_SERVER_DOWN(e) \
  (((e) == ER_TM_SERVER_DOWN_UNILATERALLY_ABORTED) \
   || ((e) == ER_OBJ_NO_CONNECT) || ((e) == ER_NET_SERVER_CRASHED) \
   || ((e) == ER_BO_CONNECT_FAILED))
#define IS_ER_TO_RECONNECT(e1, e2) \
  (((e1) == CCI_ER_DBMS && IS_SERVER_DOWN (e2)) ? true : IS_ER_COMMUNICATION (e1))

#define NEED_TO_RECONNECT(CON,ERR) \
  (IS_ER_COMMUNICATION(ERR) || !hm_broker_reconnect_when_server_down(CON))

/* default value of each datesource property */
#define CCI_DS_POOL_SIZE_DEFAULT 			10
#define CCI_DS_MAX_WAIT_DEFAULT 			1000
#define CCI_DS_POOL_PREPARED_STATEMENT_DEFAULT 		false
#define CCI_DS_MAX_OPEN_PREPARED_STATEMENT_DEFAULT	1000
#define CCI_DS_DISCONNECT_ON_QUERY_TIMEOUT_DEFAULT	false
#define CCI_DS_DEFAULT_AUTOCOMMIT_DEFAULT 		(CCI_AUTOCOMMIT_TRUE)
#define CCI_DS_DEFAULT_ISOLATION_DEFAULT 		TRAN_UNKNOWN_ISOLATION
#define CCI_DS_DEFAULT_LOCK_TIMEOUT_DEFAULT 		CCI_LOCK_TIMEOUT_DEFAULT
#define CCI_DS_LOGIN_TIMEOUT_DEFAULT			(CCI_LOGIN_TIMEOUT_DEFAULT)

#define CON_HANDLE_ID_FACTOR            1000000
#define CON_ID(a) ((a) / CON_HANDLE_ID_FACTOR)
#define REQ_ID(a) ((a) % CON_HANDLE_ID_FACTOR)
/************************************************************************
 * PRIVATE FUNCTION PROTOTYPES						*
 ************************************************************************/

static void reset_error_buffer (T_CCI_ERROR * err_buf);
static int col_set_add_drop (int resolved_id, char col_cmd, char *oid_str,
			     char *col_attr, char *value,
			     T_CCI_ERROR * err_buf);
static int col_seq_op (int resolved_id, char col_cmd, char *oid_str,
		       char *col_attr, int index, const char *value,
		       T_CCI_ERROR * err_buf);
static int fetch_cmd (int reg_h_id, char flag, T_CCI_ERROR * err_buf);
static int cas_connect (T_CON_HANDLE * con_handle, T_CCI_ERROR * err_buf);
static int cas_connect_with_ret (T_CON_HANDLE * con_handle,
				 T_CCI_ERROR * err_buf, int *connect);
static int cas_connect_internal (T_CON_HANDLE * con_handle,
				 T_CCI_ERROR * err_buf, int *connect);
static int next_result_cmd (T_REQ_HANDLE * req_handle,
			    T_CON_HANDLE * con_handle, char flag,
			    T_CCI_ERROR * err_buf);
#if defined UNUSED_FUNCTION
static int cas_end_session (T_CON_HANDLE * con_handle, T_CCI_ERROR * err_buf);
#endif

#ifdef CCI_DEBUG
static void print_debug_msg (const char *format, ...);
static const char *dbg_tran_type_str (char type);
static const char *dbg_a_type_str (T_CCI_A_TYPE);
static const char *dbg_u_type_str (T_CCI_U_TYPE);
static const char *dbg_db_param_str (T_CCI_DB_PARAM db_param);
static const char *dbg_cursor_pos_str (T_CCI_CURSOR_POS cursor_pos);
static const char *dbg_sch_type_str (T_CCI_SCH_TYPE sch_type);
static const char *dbg_oid_cmd_str (T_CCI_OID_CMD oid_cmd);
static const char *dbg_isolation_str (T_CCI_TRAN_ISOLATION isol_level);
#endif

static const char *cci_get_err_msg_internal (int error);

static T_CON_HANDLE *get_new_connection (char *ip, int port, char *db_name,
					 char *db_user, char *dbpasswd);
static bool cci_datasource_make_url (T_CCI_PROPERTIES * prop, char *new_url,
				     char *url, T_CCI_ERROR * err_buf);

static int cci_time_string (char *buf, struct timeval *time_val);
static void set_error_buffer (T_CCI_ERROR * err_buf_p,
			      int error, const char *message, ...);
static void copy_error_buffer (T_CCI_ERROR * dest_err_buf_p,
			       T_CCI_ERROR * src_err_buf_p);
static int cci_datasource_release_internal (T_CCI_DATASOURCE * ds,
					    T_CON_HANDLE * con_handle);
static int cci_end_tran_internal (T_CON_HANDLE * con_handle, char type);
static void get_last_error (T_CON_HANDLE * con_handle,
			    T_CCI_ERROR * dest_err_buf);

static int convert_cas_mode_to_driver_mode (int cas_mode);
static int convert_driver_mode_to_cas_mode (int driver_mode);

/************************************************************************
 * INTERFACE VARIABLES							*
 ************************************************************************/

/************************************************************************
 * PUBLIC VARIABLES							*
 ************************************************************************/

/************************************************************************
 * PRIVATE VARIABLES							*
 ************************************************************************/
static const char *build_number = "VERSION=" MAKE_STR (BUILD_NUMBER);

#if defined(WINDOWS)
static HANDLE con_handle_table_mutex;
static HANDLE health_check_th_mutex;
#else
static T_MUTEX con_handle_table_mutex = PTHREAD_MUTEX_INITIALIZER;
static T_MUTEX health_check_th_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

static char init_flag = 0;
static char cci_init_flag = 1;
static char is_health_check_th_started = 0;
#if !defined(WINDOWS)
static int cci_SIGPIPE_ignore = 0;
#endif

static const char *datasource_key[] = {
  CCI_DS_PROPERTY_USER,
  CCI_DS_PROPERTY_PASSWORD,
  CCI_DS_PROPERTY_URL,
  CCI_DS_PROPERTY_POOL_SIZE,
  CCI_DS_PROPERTY_MAX_WAIT,
  CCI_DS_PROPERTY_POOL_PREPARED_STATEMENT,
  CCI_DS_PROPERTY_MAX_OPEN_PREPARED_STATEMENT,
  CCI_DS_PROPERTY_LOGIN_TIMEOUT,
  CCI_DS_PROPERTY_QUERY_TIMEOUT,
  CCI_DS_PROPERTY_DISCONNECT_ON_QUERY_TIMEOUT,
  CCI_DS_PROPERTY_DEFAULT_AUTOCOMMIT,
  CCI_DS_PROPERTY_DEFAULT_ISOLATION,
  CCI_DS_PROPERTY_DEFAULT_LOCK_TIMEOUT,
  CCI_DS_PROPERTY_MAX_POOL_SIZE
};

CCI_MALLOC_FUNCTION cci_malloc = malloc;
CCI_CALLOC_FUNCTION cci_calloc = calloc;
CCI_REALLOC_FUNCTION cci_realloc = realloc;
CCI_FREE_FUNCTION cci_free = free;


#ifdef CCI_DEBUG
/*
 * cci debug message file.
 * CAUTION: this file size is not limited
 */
static FILE *cci_debug_fp = NULL;
static const char *cci_debug_filename = "cci.log";
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

int
get_elapsed_time (struct timeval *start_time)
{
  INT64 start_time_milli, end_time_milli;
  struct timeval end_time;

  assert (start_time);

  if (start_time->tv_sec == 0 && start_time->tv_usec == 0)
    {
      return 0;
    }

  gettimeofday (&end_time, NULL);

  start_time_milli =
    start_time->tv_sec * 1000LL + start_time->tv_usec / 1000LL;
  end_time_milli = end_time.tv_sec * 1000LL + end_time.tv_usec / 1000LL;

  return (int) (end_time_milli - start_time_milli);
}

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
cci_end (void)
{
  return;
}

int
cci_get_version_string (char *str, size_t len)
{
  if (str)
    {
      snprintf (str, len, "%s", build_number);
    }
  return 0;
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

static int
cci_connect_internal (char *ip, int port, char *db, char *user, char *pass,
		      T_CCI_ERROR * err_buf)
{
  T_CON_HANDLE *con_handle;
  int error = CCI_ER_NO_ERROR;

  reset_error_buffer (err_buf);

  if (ip == NULL || port < 0 || db == NULL || user == NULL || pass == NULL)
    {
      set_error_buffer (err_buf, CCI_ER_CONNECT, NULL);
      return CCI_ER_CONNECT;
    }

#if defined(WINDOWS)
  if (wsa_initialize () < 0)
    {
      set_error_buffer (err_buf, CCI_ER_CONNECT, NULL);
      return CCI_ER_CONNECT;
    }
#endif

  con_handle = get_new_connection (ip, port, db, user, pass);
  if (con_handle == NULL)
    {
      set_error_buffer (err_buf, CCI_ER_CONNECT, NULL);

      return CCI_ER_CONNECT;
    }

  reset_error_buffer (&(con_handle->err_buf));

  SET_START_TIME_FOR_LOGIN (con_handle);
  error = cas_connect (con_handle, &(con_handle->err_buf));
  if (error < 0)
    {
      get_last_error (con_handle, err_buf);
      hm_con_handle_free (con_handle);
      goto error;
    }

  error = qe_end_tran (con_handle, CCI_TRAN_COMMIT, &con_handle->err_buf);
  if (error < 0)
    {
      get_last_error (con_handle, err_buf);
      hm_con_handle_free (con_handle);
      goto error;
    }

  SET_AUTOCOMMIT_FROM_CASINFO (con_handle);
  RESET_START_TIME (con_handle);

  set_error_buffer (&(con_handle->err_buf), error, NULL);
  get_last_error (con_handle, err_buf);

  return con_handle->id;

error:

  set_error_buffer (err_buf, error, NULL);

  return error;
}

int
cci_connect_ex (char *ip, int port, char *db, char *user, char *pass,
		T_CCI_ERROR * err_buf)
{
  T_CCI_CONN connection_id, mapped_conn_id;

  connection_id = cci_connect_internal (ip, port, db, user, pass, err_buf);
  if (connection_id < 0)
    {
      return connection_id;
    }

  map_open_otc (connection_id, &mapped_conn_id);
  return mapped_conn_id;
}

int
CCI_CONNECT_INTERNAL_FUNC_NAME (char *ip, int port, char *db_name,
				char *db_user, char *dbpasswd)
{
  T_CCI_CONN connection_id, mapped_conn_id;

  connection_id = cci_connect_internal (ip, port, db_name, db_user, dbpasswd,
					NULL);
#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("cci_connect (pid %d, h:%d): %s %d %s %s %s", getpid (),
		    connection_id, ip, port, DEBUG_STR (db_name),
		    DEBUG_STR (db_user), DEBUG_STR (dbpasswd)));
#endif
  if (connection_id < 0)
    {
      return connection_id;
    }

  map_open_otc (connection_id, &mapped_conn_id);
  return mapped_conn_id;
}

static int
cci_connect_with_url_internal (char *url, char *user, char *pass,
			       T_CCI_ERROR * err_buf)
{
  char *token[MAX_URL_MATCH_COUNT] = { NULL };
  int error = CCI_ER_NO_ERROR;
  unsigned i;

  char *property = NULL;
  char *end = NULL;
  char *host, *dbname;
  int port;
  T_CON_HANDLE *con_handle = NULL;

  reset_error_buffer (err_buf);

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("cci_connect_with_url %s %s %s",
		    DEBUG_STR (url), DEBUG_STR (user), DEBUG_STR (pass)));
#endif

  if (url == NULL)
    {
      set_error_buffer (err_buf, CCI_ER_CONNECT, NULL);
      return CCI_ER_CONNECT;
    }

#if defined(WINDOWS)
  if (wsa_initialize () < 0)
    {
      set_error_buffer (err_buf, CCI_ER_CONNECT, NULL);
      return CCI_ER_CONNECT;
    }
#endif

  error = cci_url_match (url, token);
  if (error != CCI_ER_NO_ERROR)
    {
      set_error_buffer (err_buf, error, NULL);
      return error;
    }

  host = token[0];
  port = (int) strtol (token[1], &end, 10);
  dbname = token[2];

  if (user == NULL)
    {
      user = token[3];
    }
  if (pass == NULL)
    {
      pass = token[4];
    }

  property = token[5];
  if (property == NULL)
    {
      property = (char *) "";
    }

  /* start health check thread */
  MUTEX_LOCK (health_check_th_mutex);
  if (!is_health_check_th_started)
    {
      hm_create_health_check_th ();
      is_health_check_th_started = 1;
    }
  MUTEX_UNLOCK (health_check_th_mutex);

  con_handle = get_new_connection (host, port, dbname, user, pass);
  if (con_handle == NULL)
    {
      for (i = 0; i < MAX_URL_MATCH_COUNT; i++)
	{
	  FREE_MEM (token[i]);
	}

      set_error_buffer (err_buf, CCI_ER_CONNECT, NULL);
      return CCI_ER_CONNECT;
    }

  reset_error_buffer (&(con_handle->err_buf));

  snprintf (con_handle->url, SRV_CON_URL_SIZE,
	    "cci:cubrid:%s:%d:%s:%s:********:%s",
	    host, port, dbname, user, property);

  if (property != NULL)
    {
      error = cci_conn_set_properties (con_handle, property);
      API_SLOG (con_handle);

      if (con_handle->log_trace_api)
	{
	  CCI_LOGF_DEBUG (con_handle->logger, "URL[%s]", url);
	}

      if (error < 0)
	{
	  hm_con_handle_free (con_handle);
	  goto ret;
	}

      if (con_handle->alter_host_count > 0)
	{
	  con_handle->alter_host_id = 0;
	}
    }

  SET_START_TIME_FOR_LOGIN (con_handle);
  error = cas_connect (con_handle, &(con_handle->err_buf));
  if (error < 0)
    {
      get_last_error (con_handle, err_buf);
      hm_con_handle_free (con_handle);
      goto ret;
    }

  error = qe_end_tran (con_handle, CCI_TRAN_COMMIT, &con_handle->err_buf);
  if (error < 0)
    {
      get_last_error (con_handle, err_buf);
      hm_con_handle_free (con_handle);
      goto ret;
    }

  SET_AUTOCOMMIT_FROM_CASINFO (con_handle);
  RESET_START_TIME (con_handle);

ret:
  for (i = 0; i < MAX_URL_MATCH_COUNT; i++)
    {
      FREE_MEM (token[i]);
    }

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("cci_connect_with_url return %d", conn_id));
#endif

  if (error == CCI_ER_NO_ERROR)
    {
      API_ELOG (con_handle, error);
      set_error_buffer (&(con_handle->err_buf), error, NULL);
      get_last_error (con_handle, err_buf);

      return con_handle->id;
    }

  set_error_buffer (err_buf, error, NULL);

  return error;
}

int
cci_connect_with_url (char *url, char *user, char *password)
{
  T_CCI_CONN connection_id, mapped_conn_id;

  connection_id = cci_connect_with_url_internal (url, user, password, NULL);
  if (connection_id < 0)
    {
      return connection_id;
    }

  map_open_otc (connection_id, &mapped_conn_id);
  return mapped_conn_id;
}

int
cci_connect_with_url_ex (char *url, char *user, char *pass,
			 T_CCI_ERROR * err_buf)
{
  T_CCI_CONN connection_id, mapped_conn_id;

  connection_id = cci_connect_with_url_internal (url, user, pass, err_buf);
  if (connection_id < 0)
    {
      return connection_id;
    }

  map_open_otc (connection_id, &mapped_conn_id);
  return mapped_conn_id;
}

#if defined UNUSED_FUNCTION
static int
cas_end_session (T_CON_HANDLE * con_handle, T_CCI_ERROR * err_buf)
{
  int error = CCI_ER_NO_ERROR;

  API_SLOG (con_handle);

  error = qe_end_session (con_handle, err_buf);
  while (IS_OUT_TRAN (con_handle)
	 && IS_ER_TO_RECONNECT (error, con_handle->err_buf.err_code))
    {
      if (NEED_TO_RECONNECT (con_handle, error))
	{
	  /* Finally, reset_connect will return ER_TIMEOUT */
	  error = reset_connect (con_handle, NULL, &(con_handle->err_buf));
	  if (error != CCI_ER_NO_ERROR)
	    {
	      break;
	    }
	}


      error = qe_end_session (con_handle, err_buf);
    }

  API_ELOG (con_handle, error);
  return error;
}
#endif

int
cci_disconnect (int mapped_conn_id, T_CCI_ERROR * err_buf)
{
  int error = CCI_ER_NO_ERROR;
  T_CON_HANDLE *con_handle = NULL;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("(%d)cci_disconnect", mapped_conn_id));
#endif

  reset_error_buffer (err_buf);
  error = hm_get_connection (mapped_conn_id, &con_handle);
  if (error != CCI_ER_NO_ERROR)
    {
      set_error_buffer (err_buf, error, NULL);
      return error;
    }
  reset_error_buffer (&(con_handle->err_buf));
  con_handle->shard_id = CCI_SHARD_ID_INVALID;

  API_SLOG (con_handle);

  if (con_handle->datasource)
    {
      con_handle->used = false;
      hm_release_connection (mapped_conn_id, &con_handle);

      if (cci_end_tran_internal (con_handle, CCI_TRAN_ROLLBACK) != NO_ERROR)
	{
	  qe_con_close (con_handle);
	  con_handle->con_status = CCI_CON_STATUS_OUT_TRAN;
	}

      cci_datasource_release_internal (con_handle->datasource, con_handle);
      if (con_handle->log_trace_api)
	{
	  CCI_LOGF_DEBUG (con_handle->logger,
			  "[%04d][API][E][cci_datasource_release]",
			  con_handle->id);
	}

      get_last_error (con_handle, err_buf);
    }
  else if (con_handle->broker_info[BROKER_INFO_CCI_PCONNECT]
	   && hm_put_con_to_pool (con_handle->id) >= 0)
    {
      cci_end_tran_internal (con_handle, CCI_TRAN_ROLLBACK);
      API_ELOG (con_handle, 0);

      get_last_error (con_handle, err_buf);
      con_handle->used = false;
      hm_release_connection (mapped_conn_id, &con_handle);
    }
  else
    {
      error = qe_con_close (con_handle);
      API_ELOG (con_handle, error);

      set_error_buffer (&(con_handle->err_buf), error, NULL);
      get_last_error (con_handle, err_buf);
      con_handle->used = false;

      MUTEX_LOCK (con_handle_table_mutex);
      hm_delete_connection (mapped_conn_id, &con_handle);
      MUTEX_UNLOCK (con_handle_table_mutex);
    }

  return error;
}

int
cci_cancel (int mapped_conn_id)
{
  T_CON_HANDLE *con_handle = NULL;
  int error;

  error = hm_get_connection_force (mapped_conn_id, &con_handle);
  if (error != CCI_ER_NO_ERROR)
    {
      return error;
    }

  API_SLOG (con_handle);
  error = net_cancel_request (con_handle);
  API_ELOG (con_handle, error);

  return error;
}

static int
cci_end_tran_internal (T_CON_HANDLE * con_handle, char type)
{
  int error = CCI_ER_NO_ERROR;

  if (IS_IN_TRAN (con_handle))
    {
      error = qe_end_tran (con_handle, type, &(con_handle->err_buf));
    }
  else if (type == CCI_TRAN_ROLLBACK)
    {
      /* even if con status is CCI_CON_STATUS_OUT_TRAN, there may be holdable
       * req_handles that remained open after commit
       * if a rollback is done after commit, these req_handles should be
       * closed or freed
       */
      if (con_handle->broker_info[BROKER_INFO_STATEMENT_POOLING] !=
	  CAS_STATEMENT_POOLING_ON)
	{
	  hm_req_handle_free_all (con_handle);
	}
      else
	{
	  hm_req_handle_close_all_resultsets (con_handle);
	}
    }
  API_ELOG (con_handle, error);

  if (IS_OUT_TRAN (con_handle))
    {
      hm_check_rc_time (con_handle);
    }

  return error;
}

int
cci_end_tran (int mapped_conn_id, char type, T_CCI_ERROR * err_buf)
{
  int error = CCI_ER_NO_ERROR;
  T_CON_HANDLE *con_handle = NULL;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("(%d)tran: %s", mapped_conn_id,
		    dbg_tran_type_str (type)));
#endif

  reset_error_buffer (err_buf);
  error = hm_get_connection (mapped_conn_id, &con_handle);
  if (error != CCI_ER_NO_ERROR)
    {
      set_error_buffer (err_buf, error, NULL);
      return error;
    }

  reset_error_buffer (&(con_handle->err_buf));

  API_SLOG (con_handle);

  error = cci_end_tran_internal (con_handle, type);

  set_error_buffer (&(con_handle->err_buf), error, NULL);
  get_last_error (con_handle, err_buf);
  con_handle->used = false;

  return error;
}

static int
reset_connect (T_CON_HANDLE * con_handle, T_REQ_HANDLE * req_handle,
	       T_CCI_ERROR * err_buf)
{
  int connect_done;
  int error;
  int old_timeout;

  reset_error_buffer (err_buf);
  if (req_handle != NULL)
    {
      req_handle_content_free (req_handle, 1);
    }

  /* save query timeout */
  old_timeout = con_handle->current_timeout;
  if (con_handle->current_timeout <= 0)
    {
      /* if (query_timeout <= 0) */
      con_handle->current_timeout = con_handle->login_timeout;
    }
  error = cas_connect_with_ret (con_handle, err_buf, &connect_done);

  /* restore query timeout */
  con_handle->current_timeout = old_timeout;
  if (error < 0 || !connect_done)
    {
      return error;
    }

  return CCI_ER_NO_ERROR;
}

/*
 * For the purpose of re-balancing existing connections, cci_prepare,
 * cci_execute, cci_execute_array, cci_prepare_and_execute,
 * cci_execute_batch require to forcefully disconnect the current
 * con_handle when it is in the OUT_TRAN state and the time elapsed
 * after the last failure of a host is over rc_time.
 */
int
cci_prepare (int mapped_conn_id, char *sql_stmt, char flag,
	     T_CCI_ERROR * err_buf)
{
  int statement_id = -1;
  int error = CCI_ER_NO_ERROR;
  int con_err_code = 0;
  T_CON_HANDLE *con_handle = NULL;
  T_REQ_HANDLE *req_handle = NULL;
  int is_first_prepare_in_tran;

  reset_error_buffer (err_buf);
  error = hm_get_connection (mapped_conn_id, &con_handle);
  if (error != CCI_ER_NO_ERROR)
    {
      set_error_buffer (err_buf, error, NULL);
      return error;
    }
  reset_error_buffer (&(con_handle->err_buf));

  if (sql_stmt == NULL)
    {
      error = CCI_ER_STRING_PARAM;
      goto error;
    }

  API_SLOG (con_handle);
  if (con_handle->log_trace_api)
    {
      CCI_LOGF_DEBUG (con_handle->logger, "FLAG[%d],SQL[%s]", flag, sql_stmt);
    }

  if (DOES_CONNECTION_HAVE_STMT_POOL (con_handle))
    {
      statement_id = hm_req_get_from_pool (con_handle, &req_handle, sql_stmt);
      if (statement_id != CCI_ER_REQ_HANDLE)
	{
	  req_handle->query_timeout = con_handle->query_timeout;
	  goto prepare_end;
	}
    }

  statement_id = hm_req_handle_alloc (con_handle, &req_handle);
  if (statement_id < 0)
    {
      error = statement_id;
      goto prepare_error;
    }

  if (IS_OUT_TRAN (con_handle) && IS_FORCE_FAILBACK (con_handle)
      && !IS_INVALID_SOCKET (con_handle->sock_fd))
    {
      hm_force_close_connection (con_handle);
    }
  SET_START_TIME_FOR_QUERY (con_handle, req_handle);

  is_first_prepare_in_tran = IS_OUT_TRAN (con_handle);

  error = qe_prepare (req_handle, con_handle, sql_stmt, flag,
		      &(con_handle->err_buf), 0);

  while ((IS_OUT_TRAN (con_handle) || is_first_prepare_in_tran)
	 && IS_ER_TO_RECONNECT (error, con_handle->err_buf.err_code))
    {
      if (NEED_TO_RECONNECT (con_handle, error))
	{
	  /* Finally, reset_connect will return ER_TIMEOUT */
	  error =
	    reset_connect (con_handle, req_handle, &(con_handle->err_buf));
	  if (error != CCI_ER_NO_ERROR)
	    {
	      break;
	    }
	}

      error = qe_prepare (req_handle, con_handle, sql_stmt, flag,
			  &(con_handle->err_buf), 0);
    }

  if (error < 0)
    {
      goto prepare_error;
    }

  if (DOES_CONNECTION_HAVE_STMT_POOL (con_handle))
    {
      /* add new allocated req_handle to use list */
      hm_pool_add_statement_to_use (con_handle, statement_id);
    }

prepare_end:
  RESET_START_TIME (con_handle);

  API_ELOG (con_handle, statement_id);
#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("(%d:%d)pre: %d %s", mapped_conn_id,
		    REQ_ID (statement_id), flag, DEBUG_STR (sql_stmt)));
#endif

  map_open_ots (statement_id, &req_handle->mapped_stmt_id);
  con_handle->used = false;

  return req_handle->mapped_stmt_id;

prepare_error:
  RESET_START_TIME (con_handle);

  if (req_handle)
    {
      hm_req_handle_free (con_handle, req_handle);
      req_handle = NULL;
    }

  if (error == CCI_ER_QUERY_TIMEOUT &&
      con_handle->disconnect_on_query_timeout)
    {
      CLOSE_SOCKET (con_handle->sock_fd);
      con_handle->sock_fd = INVALID_SOCKET;
      con_handle->con_status = CCI_CON_STATUS_OUT_TRAN;
    }

  if (IS_OUT_TRAN (con_handle))
    {
      hm_check_rc_time (con_handle);
    }

error:
  API_ELOG (con_handle, error);
#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("(%d:%d)cci_prepare error: code(%d), flag(%d) %s",
		    mapped_conn_id, REQ_ID (statement_id), error, flag,
		    DEBUG_STR (sql_stmt)));
#endif

  set_error_buffer (&(con_handle->err_buf), error, NULL);
  get_last_error (con_handle, err_buf);
  con_handle->used = false;

  return error;
}

int
cci_get_bind_num (int mapped_stmt_id)
{
  T_CON_HANDLE *con_handle = NULL;
  T_REQ_HANDLE *req_handle = NULL;
  int error;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("(%d:%d)cci_get_bind_num", CON_ID (mapped_stmt_id),
		    REQ_ID (mapped_stmt_id)));
#endif

  error = hm_get_statement (mapped_stmt_id, &con_handle, &req_handle);
  if (error != CCI_ER_NO_ERROR)
    {
      return error;
    }

  con_handle->used = false;

  return req_handle->num_bind;
}

int
cci_is_updatable (int mapped_stmt_id)
{
  T_CON_HANDLE *con_handle = NULL;
  T_REQ_HANDLE *req_handle = NULL;
  int error;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("(%d:%d)cci_is_updatable", CON_ID (mapped_stmt_id),
		    REQ_ID (mapped_stmt_id)));
#endif

  error = hm_get_statement (mapped_stmt_id, &con_handle, &req_handle);
  if (error != CCI_ER_NO_ERROR)
    {
      return error;
    }

  con_handle->used = false;

  return (int) req_handle->updatable_flag;
}

int
cci_is_holdable (int mapped_stmt_id)
{
  T_CON_HANDLE *con_handle = NULL;
  T_REQ_HANDLE *req_handle = NULL;
  int error;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("(%d:%d)cci_is_holdable", CON_ID (mapped_stmt_id),
		    REQ_ID (mapped_stmt_id)));
#endif

  error = hm_get_statement (mapped_stmt_id, &con_handle, &req_handle);
  if (error != CCI_ER_NO_ERROR)
    {
      return error;
    }

  con_handle->used = false;

  return hm_get_req_handle_holdable (con_handle, req_handle);
}

T_CCI_COL_INFO *
cci_get_result_info (int mapped_stmt_id, T_CCI_CUBRID_STMT * cmd_type,
		     int *num)
{
  T_CON_HANDLE *con_handle = NULL;
  T_REQ_HANDLE *req_handle = NULL;
  T_CCI_COL_INFO *col_info = NULL;
  int error;

#ifdef CCI_FULL_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("(%d:%d)cci_get_result_info", CON_ID (mapped_stmt_id),
		    REQ_ID (mapped_stmt_id)));
#endif

  if (cmd_type)
    {
      *cmd_type = (T_CCI_CUBRID_STMT) (-1);
    }

  if (num)
    {
      *num = 0;
    }

  error = hm_get_statement (mapped_stmt_id, &con_handle, &req_handle);
  if (error != CCI_ER_NO_ERROR)
    {
      return NULL;
    }

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

  con_handle->used = false;

  return col_info;
}

int
cci_bind_param (int mapped_stmt_id, int index, T_CCI_A_TYPE a_type,
		void *value, T_CCI_U_TYPE u_type, char flag)
{
  int error;
  T_CON_HANDLE *con_handle = NULL;
  T_REQ_HANDLE *req_handle = NULL;

#ifdef CCI_FULL_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("(%d:%d)cci_bind_param: %d %s %p %s %d",
		    CON_ID (mapped_stmt_id), REQ_ID (mapped_stmt_id), index,
		    dbg_a_type_str (a_type), value, dbg_u_type_str (u_type),
		    flag));
#endif

  error = hm_get_statement (mapped_stmt_id, &con_handle, &req_handle);
  if (error != CCI_ER_NO_ERROR)
    {
      return error;
    }

  error = qe_bind_param (req_handle, index, a_type, value,
			 UNMEASURED_LENGTH, u_type, flag);

  con_handle->used = false;

  return error;
}

int
cci_bind_param_ex (int mapped_stmt_id, int index, T_CCI_A_TYPE a_type,
		   void *value, int length, T_CCI_U_TYPE u_type, char flag)
{
  int error;
  T_CON_HANDLE *con_handle = NULL;
  T_REQ_HANDLE *req_handle = NULL;

  error = hm_get_statement (mapped_stmt_id, &con_handle, &req_handle);
  if (error != CCI_ER_NO_ERROR)
    {
      return error;
    }

  error =
    qe_bind_param (req_handle, index, a_type, value, length, u_type, flag);

  con_handle->used = false;

  return error;
}

int
cci_register_out_param (int mapped_stmt_id, int index)
{
  return cci_register_out_param_ex (mapped_stmt_id, index, CCI_U_TYPE_NULL);
}

int
cci_register_out_param_ex (int mapped_stmt_id, int index, T_CCI_U_TYPE u_type)
{
  T_CON_HANDLE *con_handle = NULL;
  T_REQ_HANDLE *req_handle = NULL;
  int error;

  assert (u_type >= CCI_U_TYPE_FIRST && u_type <= CCI_U_TYPE_LAST);

  error = hm_get_statement (mapped_stmt_id, &con_handle, &req_handle);
  if (error != CCI_ER_NO_ERROR)
    {
      return error;
    }

  if (index <= 0 || index > req_handle->num_bind)
    {
      error = CCI_ER_BIND_INDEX;
    }
  else
    {
      if (is_connected_to_oracle (con_handle))
	{
	  req_handle->bind_value[index - 1].u_type = u_type;
	}

      req_handle->bind_mode[index - 1] |= CCI_PARAM_MODE_OUT;
    }

  con_handle->used = false;

  return error;
}

int
cci_bind_param_array_size (int mapped_stmt_id, int array_size)
{
  T_CON_HANDLE *con_handle = NULL;
  T_REQ_HANDLE *req_handle = NULL;
  int error = 0;

#ifdef CCI_FULL_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("(%d:%d)cci_bind_param_array_size: %d",
		    CON_ID (mapped_stmt_id), REQ_ID (mapped_stmt_id),
		    array_size));
#endif

  error = hm_get_statement (mapped_stmt_id, &con_handle, &req_handle);
  if (error != CCI_ER_NO_ERROR)
    {
      return error;
    }

  req_handle->bind_array_size = array_size;
  con_handle->used = false;

  return CCI_ER_NO_ERROR;
}

int
cci_bind_param_array (int mapped_stmt_id, int index, T_CCI_A_TYPE a_type,
		      void *value, int *null_ind, T_CCI_U_TYPE u_type)
{
  T_CON_HANDLE *con_handle = NULL;
  T_REQ_HANDLE *req_handle = NULL;
  int error = 0;

#ifdef CCI_FULL_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("(%d:%d)cci_bind_param_array: %d %s %p %p %s",
		    CON_ID (mapped_stmt_id), REQ_ID (mapped_stmt_id), index,
		    dbg_a_type_str (a_type), value, null_ind,
		    dbg_u_type_str (u_type)));
#endif

  assert (u_type >= CCI_U_TYPE_FIRST && u_type <= CCI_U_TYPE_LAST);

  error = hm_get_statement (mapped_stmt_id, &con_handle, &req_handle);
  if (error != CCI_ER_NO_ERROR)
    {
      return error;
    }

  if (req_handle->bind_array_size <= 0)
    {
      error = CCI_ER_BIND_ARRAY_SIZE;
    }
  else if (index <= 0 || index > req_handle->num_bind)
    {
      error = CCI_ER_BIND_INDEX;
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

  con_handle->used = false;

  return error;
}

/*
 * For the purpose of re-balancing existing connections, cci_prepare,
 * cci_execute, cci_execute_array, cci_prepare_and_execute,
 * cci_execute_batch require to forcefully disconnect the current
 * con_handle when it is in the OUT_TRAN state and the time elapsed
 * after the last failure of a host is over rc_time.
 */
int
cci_execute (int mapped_stmt_id, char flag, int max_col_size,
	     T_CCI_ERROR * err_buf)
{
  T_REQ_HANDLE *req_handle = NULL;
  T_CON_HANDLE *con_handle = NULL;
  int error = CCI_ER_NO_ERROR;
  int con_err_code = 0;
  struct timeval st, et;
  bool is_first_exec_in_tran = false;
  T_BROKER_VERSION broker_ver;
  int loop;
  char prepare_flag;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("(%d:%d)exe: %d, %d",
		    CON_ID (mapped_stmt_id), REQ_ID (mapped_stmt_id), flag,
		    max_col_size));
#endif

  reset_error_buffer (err_buf);
  error = hm_get_statement (mapped_stmt_id, &con_handle, &req_handle);
  if (error != CCI_ER_NO_ERROR)
    {
      set_error_buffer (err_buf, error, NULL);
      return error;
    }
  reset_error_buffer (&(con_handle->err_buf));
  con_handle->shard_id = CCI_SHARD_ID_INVALID;
  req_handle->shard_id = CCI_SHARD_ID_INVALID;

  if (con_handle->log_slow_queries)
    {
      gettimeofday (&st, NULL);
    }

  API_SLOG (con_handle);
  if (con_handle->log_trace_api)
    {
      CCI_LOGF_DEBUG (con_handle->logger, "FLAG[%d], MAX_COL_SIZE[%d]",
		      flag, max_col_size);
    }

  if (flag & CCI_EXEC_ONLY_QUERY_PLAN)
    {
      flag |= CCI_EXEC_QUERY_INFO;
    }

  /* Asynchronous mode is unsupported. */
  if (flag & CCI_EXEC_ASYNC)
    {
      flag &= ~CCI_EXEC_ASYNC;
    }

  if (IS_OUT_TRAN (con_handle) && IS_FORCE_FAILBACK (con_handle)
      && !IS_INVALID_SOCKET (con_handle->sock_fd))
    {
      hm_force_close_connection (con_handle);
    }
  SET_START_TIME_FOR_QUERY (con_handle, req_handle);

  if (IS_BROKER_STMT_POOL (con_handle) && req_handle->valid == false)
    {
      error = qe_prepare (req_handle, con_handle, req_handle->sql_text,
			  req_handle->prepare_flag, &(con_handle->err_buf),
			  1);
    }

  is_first_exec_in_tran = IS_OUT_TRAN (con_handle);

  if (error >= 0)
    {
      error = qe_execute (req_handle, con_handle, flag, max_col_size,
			  &(con_handle->err_buf));
    }
  while ((IS_OUT_TRAN (con_handle) || is_first_exec_in_tran)
	 && IS_ER_TO_RECONNECT (error, con_handle->err_buf.err_code))
    {
      if (NEED_TO_RECONNECT (con_handle, error))
	{
	  /* Finally, reset_connect will return ER_TIMEOUT */
	  error =
	    reset_connect (con_handle, req_handle, &(con_handle->err_buf));
	  if (error != CCI_ER_NO_ERROR)
	    {
	      break;
	    }
	}

      error = qe_prepare (req_handle, con_handle, req_handle->sql_text,
			  req_handle->prepare_flag, &(con_handle->err_buf),
			  1);
      if (error < 0)
	{
	  continue;
	}

      error = qe_execute (req_handle, con_handle, flag, max_col_size,
			  &(con_handle->err_buf));
    }

  broker_ver = hm_get_broker_version (con_handle);
  if (hm_broker_understand_the_protocol (broker_ver, PROTOCOL_V7))
    {
      loop = 0;			/* retry once more */
      prepare_flag = (req_handle->prepare_flag
		      | CCI_PREPARE_XASL_CACHE_PINNED);
    }
  else
    {
      /* legacy broker */

      loop = 1;			/* infinitely */
      prepare_flag = req_handle->prepare_flag;
    }

  do
    {
      /* If prepared plan is invalidated while using plan cache,
       * the error, CAS_ER_STMT_POOLING, is returned.
       * In this case, prepare and execute have to be executed again.
       */
      if (error == CAS_ER_STMT_POOLING && IS_BROKER_STMT_POOL (con_handle))
	{
	  req_handle_content_free (req_handle, 1);
	  error = qe_prepare (req_handle, con_handle, req_handle->sql_text,
			      prepare_flag, &(con_handle->err_buf), 1);
	  if (error < 0)
	    {
	      goto execute_end;
	    }
	  error = qe_execute (req_handle, con_handle, flag, max_col_size,
			      &(con_handle->err_buf));
	}
    }
  while (loop && error == CAS_ER_STMT_POOLING
	 && IS_BROKER_STMT_POOL (con_handle));

execute_end:
  RESET_START_TIME (con_handle);

  if (error == CCI_ER_QUERY_TIMEOUT &&
      con_handle->disconnect_on_query_timeout)
    {
      hm_force_close_connection (con_handle);
    }

  if (IS_OUT_TRAN (con_handle))
    {
      hm_check_rc_time (con_handle);
    }

  API_ELOG (con_handle, error);
  if (con_handle->log_slow_queries)
    {
      long elapsed;

      gettimeofday (&et, NULL);
      elapsed = ELAPSED_MSECS (et, st);
      if (elapsed > con_handle->slow_query_threshold_millis)
	{
	  CCI_LOGF_DEBUG (con_handle->logger, "[CONHANDLE - %04d] "
			  "[CAS INFO - %d.%d.%d.%d:%d, %d, %d] "
			  "[SLOW QUERY - ELAPSED : %d] [SQL - %s]",
			  con_handle->id,
			  con_handle->ip_addr[0],
			  con_handle->ip_addr[1],
			  con_handle->ip_addr[2],
			  con_handle->ip_addr[3],
			  con_handle->port,
			  con_handle->cas_id, con_handle->cas_pid,
			  elapsed, req_handle->sql_text);
	}
    }

  set_error_buffer (&(con_handle->err_buf), error, NULL);
  get_last_error (con_handle, err_buf);
  con_handle->used = false;

  return error;
}

/*
 * For the purpose of re-balancing existing connections, cci_prepare,
 * cci_execute, cci_execute_array, cci_prepare_and_execute,
 * cci_execute_batch require to forcefully disconnect the current
 * con_handle when it is in the OUT_TRAN state and the time elapsed
 * after the last failure of a host is over rc_time.
 */
int
cci_prepare_and_execute (int mapped_conn_id, char *sql_stmt,
			 int max_col_size, int *exec_retval,
			 T_CCI_ERROR * err_buf)
{
  T_CON_HANDLE *con_handle = NULL;
  T_REQ_HANDLE *req_handle = NULL;
  int error = CCI_ER_NO_ERROR;
  int statement_id;
  struct timeval st, et;
  int is_first_prepare_in_tran;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("cci_prepare_and_execute %d %d %d %d %s", con_handle,
		    max_col_size, DEBUG_STR (sql_stmt)));
#endif

  reset_error_buffer (err_buf);
  error = hm_get_connection (mapped_conn_id, &con_handle);
  if (error != CCI_ER_NO_ERROR)
    {
      set_error_buffer (err_buf, error, NULL);
      if (exec_retval != NULL)
	{
	  *exec_retval = error;
	}
      return error;
    }
  reset_error_buffer (&(con_handle->err_buf));
  con_handle->shard_id = CCI_SHARD_ID_INVALID;

  if (sql_stmt == NULL)
    {
      error = CCI_ER_STRING_PARAM;
      goto error;
    }

  if (con_handle->log_slow_queries)
    {
      gettimeofday (&st, NULL);
    }

  API_SLOG (con_handle);
  if (con_handle->log_trace_api)
    {
      CCI_LOGF_DEBUG (con_handle->logger, "MAX_COL_SIZE[%d], SQL[%s]",
		      max_col_size, sql_stmt);
    }

  statement_id = hm_req_handle_alloc (con_handle, &req_handle);
  if (statement_id < 0)
    {
      error = statement_id;
      goto prepare_execute_error;
    }
  req_handle->shard_id = CCI_SHARD_ID_INVALID;

  if (IS_OUT_TRAN (con_handle) && IS_FORCE_FAILBACK (con_handle)
      && !IS_INVALID_SOCKET (con_handle->sock_fd))
    {
      hm_force_close_connection (con_handle);
    }

  SET_START_TIME_FOR_QUERY (con_handle, req_handle);
  is_first_prepare_in_tran = IS_OUT_TRAN (con_handle);

  error = qe_prepare_and_execute (req_handle, con_handle, sql_stmt,
				  max_col_size, &(con_handle->err_buf));
  while ((IS_OUT_TRAN (con_handle) || is_first_prepare_in_tran)
	 && IS_ER_TO_RECONNECT (error, con_handle->err_buf.err_code))
    {
      if (NEED_TO_RECONNECT (con_handle, error))
	{
	  /* Finally, reset_connect will return ER_TIMEOUT */
	  error =
	    reset_connect (con_handle, req_handle, &(con_handle->err_buf));
	  if (error != CCI_ER_NO_ERROR)
	    {
	      break;
	    }
	}

      error = qe_prepare_and_execute (req_handle, con_handle,
				      sql_stmt, max_col_size,
				      &(con_handle->err_buf));
    }

  API_ELOG (con_handle, error);
  if (error < 0)
    {
      goto prepare_execute_error;
    }

  if (exec_retval != NULL)
    {
      *exec_retval = error;
    }

  if (con_handle->log_slow_queries)
    {
      long elapsed;

      gettimeofday (&et, NULL);
      elapsed = ELAPSED_MSECS (et, st);
      if (elapsed > con_handle->slow_query_threshold_millis)
	{
	  CCI_LOGF_DEBUG (con_handle->logger, "[CONHANDLE - %04d] "
			  "[CAS INFO - %d.%d.%d.%d:%d, %d, %d] "
			  "[SLOW QUERY - ELAPSED : %d] [SQL - %s]",
			  con_handle->id,
			  con_handle->ip_addr[0],
			  con_handle->ip_addr[1],
			  con_handle->ip_addr[2],
			  con_handle->ip_addr[3],
			  con_handle->port,
			  con_handle->cas_id, con_handle->cas_pid,
			  elapsed, req_handle->sql_text);
	}
    }

  RESET_START_TIME (con_handle);

  if (IS_OUT_TRAN (con_handle))
    {
      hm_check_rc_time (con_handle);
    }

  map_open_ots (statement_id, &req_handle->mapped_stmt_id);
  con_handle->used = false;

  return req_handle->mapped_stmt_id;

prepare_execute_error:
  RESET_START_TIME (con_handle);

  if (req_handle)
    {
      hm_req_handle_free (con_handle, req_handle);
      req_handle = NULL;
    }

  if (error == CCI_ER_QUERY_TIMEOUT &&
      con_handle->disconnect_on_query_timeout)
    {
      CLOSE_SOCKET (con_handle->sock_fd);
      con_handle->sock_fd = INVALID_SOCKET;
      con_handle->con_status = CCI_CON_STATUS_OUT_TRAN;
    }

  if (IS_OUT_TRAN (con_handle))
    {
      hm_check_rc_time (con_handle);
    }

error:
  set_error_buffer (&(con_handle->err_buf), error, NULL);
  get_last_error (con_handle, err_buf);
  con_handle->used = false;
  if (exec_retval != NULL)
    {
      *exec_retval = error;
    }

  return error;
}

int
cci_next_result (int mapped_stmt_id, T_CCI_ERROR * err_buf)
{
  T_REQ_HANDLE *req_handle = NULL;
  T_CON_HANDLE *con_handle = NULL;
  int error = CCI_ER_NO_ERROR;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("(%d:%d)cci_next_result", CON_ID (mapped_stmt_id),
		    REQ_ID (mapped_stmt_id)));
#endif

  reset_error_buffer (err_buf);
  error = hm_get_statement (mapped_stmt_id, &con_handle, &req_handle);
  if (error != CCI_ER_NO_ERROR)
    {
      set_error_buffer (err_buf, error, NULL);
      return error;
    }
  reset_error_buffer (&(con_handle->err_buf));

  if (req_handle->current_query_res + 1 < req_handle->num_query_res)
    {
      error =
	next_result_cmd (req_handle, con_handle, CCI_CLOSE_CURRENT_RESULT,
			 &(con_handle->err_buf));
      if (error >= 0)
	{
	  req_handle->current_query_res++;
	}
    }
  else
    {
      error = CAS_ER_NO_MORE_RESULT_SET;
    }

  set_error_buffer (&(con_handle->err_buf), error, NULL);
  get_last_error (con_handle, err_buf);
  con_handle->used = false;

  return error;
}

/*
 * For the purpose of re-balancing existing connections, cci_prepare,
 * cci_execute, cci_execute_array, cci_prepare_and_execute,
 * cci_execute_batch require to forcefully disconnect the current
 * con_handle when it is in the OUT_TRAN state and the time elapsed
 * after the last failure of a host is over rc_time.
 */
int
cci_execute_array (int mapped_stmt_id, T_CCI_QUERY_RESULT ** qr,
		   T_CCI_ERROR * err_buf)
{
  T_REQ_HANDLE *req_handle = NULL;
  T_CON_HANDLE *con_handle = NULL;
  int error = CCI_ER_NO_ERROR;
  int con_err_code = CCI_ER_NO_ERROR;
  bool is_first_exec_in_tran = false;
  T_BROKER_VERSION broker_ver;
  int loop;
  char prepare_flag;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("(%d:%d)cci_execute_array", CON_ID (mapped_stmt_id),
		    REQ_ID (mapped_stmt_id)));
#endif

  if (qr == NULL)
    {
      return CCI_ER_INVALID_ARGS;
    }

  *qr = NULL;

  reset_error_buffer (err_buf);
  error = hm_get_statement (mapped_stmt_id, &con_handle, &req_handle);
  if (error != CCI_ER_NO_ERROR)
    {
      set_error_buffer (err_buf, error, NULL);
      return error;
    }
  reset_error_buffer (&(con_handle->err_buf));
  con_handle->shard_id = CCI_SHARD_ID_INVALID;
  req_handle->shard_id = CCI_SHARD_ID_INVALID;

  if (IS_OUT_TRAN (con_handle) && IS_FORCE_FAILBACK (con_handle)
      && !IS_INVALID_SOCKET (con_handle->sock_fd))
    {
      hm_force_close_connection (con_handle);
    }
  SET_START_TIME_FOR_QUERY (con_handle, req_handle);

  if (IS_BROKER_STMT_POOL (con_handle) && req_handle->valid == false)
    {
      error = qe_prepare (req_handle, con_handle, req_handle->sql_text,
			  req_handle->prepare_flag, &(con_handle->err_buf),
			  1);
    }

  is_first_exec_in_tran = IS_OUT_TRAN (con_handle);

  if (error >= 0)
    {
      error = qe_execute_array (req_handle, con_handle, qr,
				&(con_handle->err_buf));
    }
  while ((IS_OUT_TRAN (con_handle) || is_first_exec_in_tran)
	 && IS_ER_TO_RECONNECT (error, con_handle->err_buf.err_code))
    {
      if (NEED_TO_RECONNECT (con_handle, error))
	{
	  /* Finally, reset_connect will return ER_TIMEOUT */
	  error = reset_connect (con_handle, req_handle,
				 &(con_handle->err_buf));
	  if (error != CCI_ER_NO_ERROR)
	    {
	      break;
	    }
	}

      error = qe_prepare (req_handle, con_handle, req_handle->sql_text,
			  req_handle->prepare_flag, &(con_handle->err_buf),
			  1);
      if (error < 0)
	{
	  continue;
	}

      error = qe_execute_array (req_handle, con_handle, qr,
				&(con_handle->err_buf));
    }

  broker_ver = hm_get_broker_version (con_handle);
  if (hm_broker_understand_the_protocol (broker_ver, PROTOCOL_V7))
    {
      loop = 0;			/* retry once more */
      prepare_flag = (req_handle->prepare_flag
		      | CCI_PREPARE_XASL_CACHE_PINNED);
    }
  else
    {
      /* legacy broker */

      loop = 1;			/* infinitely */
      prepare_flag = req_handle->prepare_flag;
    }

  do
    {
      if (error == CAS_ER_STMT_POOLING && IS_BROKER_STMT_POOL (con_handle))
	{
	  req_handle_content_free (req_handle, 1);
	  error = qe_prepare (req_handle, con_handle, req_handle->sql_text,
			      prepare_flag, &(con_handle->err_buf), 1);
	  if (error < 0)
	    {
	      goto execute_end;
	    }
	  error = qe_execute_array (req_handle, con_handle, qr,
				    &(con_handle->err_buf));
	}
    }
  while (loop && error == CAS_ER_STMT_POOLING
	 && IS_BROKER_STMT_POOL (con_handle));

execute_end:
  RESET_START_TIME (con_handle);

  if (error == CCI_ER_QUERY_TIMEOUT &&
      con_handle->disconnect_on_query_timeout)
    {
      hm_force_close_connection (con_handle);
    }

  if (IS_OUT_TRAN (con_handle))
    {
      hm_check_rc_time (con_handle);
    }

  set_error_buffer (&(con_handle->err_buf), error, NULL);
  get_last_error (con_handle, err_buf);
  con_handle->used = false;

  return error;
}

int
cci_get_db_parameter (int mapped_conn_id, T_CCI_DB_PARAM param_name,
		      void *value, T_CCI_ERROR * err_buf)
{
  T_CON_HANDLE *con_handle = NULL;
  int error = CCI_ER_NO_ERROR;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("cci_get_db_parameter %d %s",
				    mapped_conn_id,
				    dbg_db_param_str (param_name)));
#endif

  reset_error_buffer (err_buf);
  error = hm_get_connection (mapped_conn_id, &con_handle);
  if (error != CCI_ER_NO_ERROR)
    {
      set_error_buffer (err_buf, error, NULL);
      return error;
    }
  reset_error_buffer (&(con_handle->err_buf));

  if (param_name < CCI_PARAM_FIRST || param_name > CCI_PARAM_LAST)
    {
      error = CCI_ER_PARAM_NAME;
      goto ret;
    }

  if (param_name == CCI_PARAM_ISOLATION_LEVEL &&
      con_handle->isolation_level != TRAN_UNKNOWN_ISOLATION)
    {
      memcpy (value, &con_handle->isolation_level, sizeof (int));
      goto ret;
    }

  if (param_name == CCI_PARAM_LOCK_TIMEOUT &&
      con_handle->lock_timeout != CCI_LOCK_TIMEOUT_DEFAULT)
    {
      memcpy (value, &con_handle->lock_timeout, sizeof (int));
      goto ret;
    }

  if (param_name == CCI_PARAM_AUTO_COMMIT)
    {
      memcpy (value, &con_handle->autocommit_mode, sizeof (int));
      goto ret;
    }

  error = qe_get_db_parameter (con_handle, param_name, value,
			       &(con_handle->err_buf));
  while (IS_OUT_TRAN (con_handle)
	 && IS_ER_TO_RECONNECT (error, con_handle->err_buf.err_code))
    {
      if (NEED_TO_RECONNECT (con_handle, error))
	{
	  /* Finally, reset_connect will return ER_TIMEOUT */
	  error = reset_connect (con_handle, NULL, &(con_handle->err_buf));
	  if (error != CCI_ER_NO_ERROR)
	    {
	      break;
	    }
	}

      error = qe_get_db_parameter (con_handle, param_name, value,
				   &(con_handle->err_buf));
    }

ret:
  set_error_buffer (&(con_handle->err_buf), error, NULL);
  get_last_error (con_handle, err_buf);
  con_handle->used = false;

  return error;
}

long
cci_escape_string (int mapped_conn_id, char *to, const char *from,
		   unsigned long length, T_CCI_ERROR * err_buf)
{
  T_CON_HANDLE *con_handle = NULL;
  int error = CCI_ER_NO_ERROR;
  unsigned long i;
  char *target_ptr = to;
  int no_backslash_escapes;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("cci_escape_string %d", mapped_conn_id));
#endif
  reset_error_buffer (err_buf);
  if (to == NULL || from == NULL)
    {
      set_error_buffer (err_buf, CCI_ER_INVALID_ARGS, NULL);
      return CCI_ER_INVALID_ARGS;
    }

  if (mapped_conn_id == CCI_NO_BACKSLASH_ESCAPES_FALSE
      || mapped_conn_id == CCI_NO_BACKSLASH_ESCAPES_TRUE)
    {
      no_backslash_escapes = mapped_conn_id;
      goto convert;
    }

  error = hm_get_connection (mapped_conn_id, &con_handle);
  if (error != CCI_ER_NO_ERROR)
    {
      set_error_buffer (err_buf, error, NULL);
      return error;
    }
  reset_error_buffer (&(con_handle->err_buf));

  if (con_handle->no_backslash_escapes == CCI_NO_BACKSLASH_ESCAPES_NOT_SET)
    {
      error = qe_get_db_parameter (con_handle, CCI_PARAM_NO_BACKSLASH_ESCAPES,
				   &con_handle->no_backslash_escapes,
				   &(con_handle->err_buf));
      while (IS_OUT_TRAN (con_handle)
	     && IS_ER_TO_RECONNECT (error, con_handle->err_buf.err_code))
	{
	  if (NEED_TO_RECONNECT (con_handle, error))
	    {
	      /* Finally, reset_connect will return ER_TIMEOUT */
	      error = reset_connect (con_handle, NULL,
				     &(con_handle->err_buf));
	      if (error != CCI_ER_NO_ERROR)
		{
		  break;
		}
	    }

	  error = qe_get_db_parameter (con_handle,
				       CCI_PARAM_NO_BACKSLASH_ESCAPES,
				       &con_handle->no_backslash_escapes,
				       &(con_handle->err_buf));
	}

      if (error != CCI_ER_NO_ERROR)
	{
	  goto error;
	}
    }

  no_backslash_escapes = con_handle->no_backslash_escapes;

convert:
  for (i = 0; i < length; i++)
    {
      if (from[i] == '\'')
	{
	  /* single-quote is converted to two-single-quote */
	  *(target_ptr) = '\'';
	  *(target_ptr + 1) = '\'';
	  target_ptr += 2;
	}
      else if (no_backslash_escapes == CCI_NO_BACKSLASH_ESCAPES_FALSE)
	{
	  if (from[i] == '\0')
	    {
	      /* ASCII 0 is converted to "\" + "0" */
	      *(target_ptr) = '\\';
	      *(target_ptr + 1) = '0';
	      target_ptr += 2;
	    }
	  else if (from[i] == '\r')
	    {
	      /* carrage return is converted to "\" + "r" */
	      *(target_ptr) = '\\';
	      *(target_ptr + 1) = 'r';
	      target_ptr += 2;
	    }
	  else if (from[i] == '\n')
	    {
	      /* new line is converted to "\" + "n" */
	      *(target_ptr) = '\\';
	      *(target_ptr + 1) = 'n';
	      target_ptr += 2;
	    }
	  else if (from[i] == '\\')
	    {
	      /* \ is converted to \\ */
	      *(target_ptr) = '\\';
	      *(target_ptr + 1) = '\\';
	      target_ptr += 2;
	    }
	  else
	    {
	      *(target_ptr) = from[i];
	      target_ptr++;
	    }
	}
      else
	{
	  *(target_ptr) = from[i];
	  target_ptr++;
	}
    }

  /* terminating NULL char */
  *target_ptr = '\0';

  if (con_handle != NULL)
    {
      assert (mapped_conn_id != CCI_NO_BACKSLASH_ESCAPES_FALSE
	      && mapped_conn_id != CCI_NO_BACKSLASH_ESCAPES_TRUE);
      con_handle->used = false;
    }

  return ((long) (target_ptr - to));

error:
  assert (con_handle != NULL);
  assert (mapped_conn_id != CCI_NO_BACKSLASH_ESCAPES_FALSE
	  && mapped_conn_id != CCI_NO_BACKSLASH_ESCAPES_TRUE);

  set_error_buffer (&(con_handle->err_buf), error, NULL);
  get_last_error (con_handle, err_buf);
  con_handle->used = false;

  return ((long) error);
}

int
cci_set_db_parameter (int mapped_conn_id, T_CCI_DB_PARAM param_name,
		      void *value, T_CCI_ERROR * err_buf)
{
  T_CON_HANDLE *con_handle = NULL;
  int error = CCI_ER_NO_ERROR;
  int i_val;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("cci_set_db_parameter %d %s", mapped_conn_id,
		    dbg_db_param_str (param_name)));
#endif

  reset_error_buffer (err_buf);
  error = hm_get_connection (mapped_conn_id, &con_handle);
  if (error != CCI_ER_NO_ERROR)
    {
      set_error_buffer (err_buf, error, NULL);
      return error;
    }
  reset_error_buffer (&(con_handle->err_buf));

  if (param_name < CCI_PARAM_FIRST || param_name > CCI_PARAM_LAST)
    {
      error = CCI_ER_PARAM_NAME;
      goto ret;
    }

  if (param_name == CCI_PARAM_ISOLATION_LEVEL &&
      con_handle->isolation_level != TRAN_UNKNOWN_ISOLATION &&
      con_handle->isolation_level == *((int *) value))
    {
      goto ret;
    }

  if (param_name == CCI_PARAM_LOCK_TIMEOUT &&
      con_handle->lock_timeout != CCI_LOCK_TIMEOUT_DEFAULT &&
      con_handle->lock_timeout == *((int *) value))
    {
      goto ret;
    }

  error = qe_set_db_parameter (con_handle, param_name, value,
			       &(con_handle->err_buf));
  while (IS_OUT_TRAN (con_handle)
	 && IS_ER_TO_RECONNECT (error, con_handle->err_buf.err_code))
    {
      if (NEED_TO_RECONNECT (con_handle, error))
	{
	  /* Finally, reset_connect will return ER_TIMEOUT */
	  error = reset_connect (con_handle, NULL, &(con_handle->err_buf));
	  if (error != CCI_ER_NO_ERROR)
	    {
	      break;
	    }
	}

      error = qe_set_db_parameter (con_handle, param_name, value,
				   &(con_handle->err_buf));
    }

  if (error >= 0)
    {
      i_val = *((int *) value);

      if (param_name == CCI_PARAM_LOCK_TIMEOUT)
	{
	  con_handle->lock_timeout = i_val;
	}
      else if (param_name == CCI_PARAM_ISOLATION_LEVEL)
	{
	  con_handle->isolation_level = i_val;
	}
    }

ret:
  set_error_buffer (&(con_handle->err_buf), error, NULL);
  get_last_error (con_handle, err_buf);
  con_handle->used = false;

  return error;
}

static int
convert_cas_mode_to_driver_mode (int cas_mode)
{
  int driver_mode = 0;

  switch (cas_mode)
    {
    case CAS_CHANGE_MODE_AUTO:
      driver_mode = CCI_CAS_CHANGE_MODE_AUTO;
      break;
    case CAS_CHANGE_MODE_KEEP:
      driver_mode = CCI_CAS_CHANGE_MODE_KEEP;
      break;
    default:
      driver_mode = CCI_CAS_CHANGE_MODE_UNKNOWN;
      break;
    }

  return driver_mode;
}

static int
convert_driver_mode_to_cas_mode (int driver_mode)
{
  int cas_mode = 0;

  switch (driver_mode)
    {
    case CCI_CAS_CHANGE_MODE_AUTO:
      cas_mode = CAS_CHANGE_MODE_AUTO;
      break;
    case CCI_CAS_CHANGE_MODE_KEEP:
      cas_mode = CAS_CHANGE_MODE_KEEP;
      break;
    default:
      cas_mode = CAS_CHANGE_MODE_UNKNOWN;
      break;
    }

  return cas_mode;
}

int
cci_set_cas_change_mode (int mapped_conn_id, int driver_mode,
			 T_CCI_ERROR * err_buf)
{
  T_CON_HANDLE *con_handle = NULL;
  int error = CCI_ER_NO_ERROR;
  int cas_mode;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("cci_set_cas_change_mode %d %d", mapped_conn_id,
		    driver_mode));
#endif

  reset_error_buffer (err_buf);
  error = hm_get_connection (mapped_conn_id, &con_handle);
  if (error != CCI_ER_NO_ERROR)
    {
      set_error_buffer (err_buf, error, NULL);
      return error;
    }
  reset_error_buffer (&(con_handle->err_buf));

  cas_mode = convert_driver_mode_to_cas_mode (driver_mode);
  if (cas_mode == CAS_CHANGE_MODE_UNKNOWN)
    {
      error = CCI_ER_INVALID_ARGS;
      goto ret;
    }

  cas_mode = qe_set_cas_change_mode (con_handle, cas_mode,
				     &(con_handle->err_buf));
  while (IS_OUT_TRAN (con_handle)
	 && IS_ER_TO_RECONNECT (cas_mode, con_handle->err_buf.err_code))
    {
      if (NEED_TO_RECONNECT (con_handle, cas_mode))
	{
	  /* Finally, reset_connect will return ER_TIMEOUT */
	  cas_mode = reset_connect (con_handle, NULL, &(con_handle->err_buf));
	  if (cas_mode != CCI_ER_NO_ERROR)
	    {
	      break;
	    }
	}

      cas_mode = qe_set_cas_change_mode (con_handle, cas_mode,
					 &(con_handle->err_buf));
    }

  if (cas_mode < 0)
    {
      error = cas_mode;
    }
  else
    {
      driver_mode = convert_cas_mode_to_driver_mode (cas_mode);
      if (driver_mode == CCI_CAS_CHANGE_MODE_UNKNOWN)
	{
	  error = CCI_ER_COMMUNICATION;
	}
    }

ret:
  set_error_buffer (&(con_handle->err_buf), error, NULL);
  get_last_error (con_handle, err_buf);
  con_handle->used = false;

  if (error < 0)
    {
      return error;
    }
  else
    {
      return driver_mode;
    }
}

int
cci_close_query_result (int mapped_stmt_id, T_CCI_ERROR * err_buf)
{
  T_REQ_HANDLE *req_handle = NULL;
  T_CON_HANDLE *con_handle = NULL;
  int error = 0;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("(%d:%d)cci_close_query_result", CON_ID (mapped_stmt_id),
		    REQ_ID (mapped_stmt_id)));
#endif

  reset_error_buffer (err_buf);
  error = hm_get_statement (mapped_stmt_id, &con_handle, &req_handle);
  if (error != CCI_ER_NO_ERROR)
    {
      set_error_buffer (err_buf, error, NULL);
      return error;
    }
  reset_error_buffer (&(con_handle->err_buf));

  error = qe_close_query_result (req_handle, con_handle);
  if (IS_ER_TO_RECONNECT (error, con_handle->err_buf.err_code))
    {
      if (IS_OUT_TRAN (con_handle))
	{
	  error = CCI_ER_NO_ERROR;
	}
    }

  if (error == CCI_ER_NO_ERROR)
    {
      error = req_close_query_result (req_handle);
    }

  set_error_buffer (&(con_handle->err_buf), error, NULL);
  get_last_error (con_handle, err_buf);
  con_handle->used = false;

  return error;
}

int
cci_close_req_handle (int mapped_stmt_id)
{
  T_REQ_HANDLE *req_handle = NULL;
  T_CON_HANDLE *con_handle = NULL;
  int error = 0;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("(%d:%d)cci_close_req_handle", CON_ID (mapped_stmt_id),
		    REQ_ID (mapped_stmt_id)));
#endif

  error = hm_get_statement (mapped_stmt_id, &con_handle, &req_handle);
  if (error != CCI_ER_NO_ERROR)
    {
      return error;
    }
  reset_error_buffer (&(con_handle->err_buf));

  API_SLOG (con_handle);

  if (DOES_CONNECTION_HAVE_STMT_POOL (con_handle)
      && req_handle->sql_text != NULL)
    {
      qe_close_query_result (req_handle, con_handle);

      /* free req_handle resources */
      req_handle_content_free_for_pool (req_handle);

      if (con_handle->autocommit_mode == CCI_AUTOCOMMIT_TRUE
	  && IS_IN_TRAN (con_handle))
	{
	  T_CCI_ERROR err_buf;
	  qe_end_tran (con_handle, CCI_TRAN_ROLLBACK, &err_buf);
	}

      error = hm_req_add_to_pool (con_handle, req_handle->sql_text,
				  mapped_stmt_id, req_handle);
      if (error == CCI_ER_NO_ERROR)
	{
	  goto cci_close_req_handle_end;
	}
    }

  if (req_handle->handle_type == HANDLE_PREPARE
      || req_handle->handle_type == HANDLE_SCHEMA_INFO)
    {
      error = qe_close_req_handle (req_handle, con_handle);
      if (IS_ER_TO_RECONNECT (error, con_handle->err_buf.err_code))
	{
	  if (IS_OUT_TRAN (con_handle))
	    {
	      error = CCI_ER_NO_ERROR;
	    }
	}
    }
  else
    {
      error = CCI_ER_NO_ERROR;
    }
  hm_req_handle_free (con_handle, req_handle);
  req_handle = NULL;

  if (IS_OUT_TRAN (con_handle))
    {
      hm_check_rc_time (con_handle);
    }

cci_close_req_handle_end:
  API_ELOG (con_handle, error);
  con_handle->used = false;
  hm_release_statement (mapped_stmt_id, &con_handle, &req_handle);

  return error;
}

int
cci_cursor (int mapped_stmt_id, int offset, T_CCI_CURSOR_POS origin,
	    T_CCI_ERROR * err_buf)
{
  T_REQ_HANDLE *req_handle = NULL;
  T_CON_HANDLE *con_handle = NULL;
  int error = CCI_ER_NO_ERROR;

#ifdef CCI_FULL_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("(%d:%d)cci_cursor: %d %s",
				    CON_ID (mapped_stmt_id),
				    REQ_ID (mapped_stmt_id), offset,
				    dbg_cursor_pos_str (origin)));
#endif

  reset_error_buffer (err_buf);
  error = hm_get_statement (mapped_stmt_id, &con_handle, &req_handle);
  if (error != CCI_ER_NO_ERROR)
    {
      set_error_buffer (err_buf, error, NULL);
      return error;
    }
  reset_error_buffer (&(con_handle->err_buf));

  error = qe_cursor (req_handle, con_handle, offset, (char) origin,
		     &(con_handle->err_buf));

  set_error_buffer (&(con_handle->err_buf), error, NULL);
  get_last_error (con_handle, err_buf);
  con_handle->used = false;

  return error;
}

int
cci_fetch_size (int mapped_stmt_id, int fetch_size)
{
  T_CON_HANDLE *con_handle = NULL;
  T_REQ_HANDLE *req_handle = NULL;
  int error;

#ifdef CCI_FULL_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("(%d:%d)cci_fetch_size: %d",
				    CON_ID (mapped_stmt_id),
				    REQ_ID (mapped_stmt_id), fetch_size));
#endif

  error = hm_get_statement (mapped_stmt_id, &con_handle, &req_handle);
  if (error != CCI_ER_NO_ERROR)
    {
      return error;
    }

  req_handle->fetch_size = fetch_size;
  con_handle->used = false;

  return error;
}

int
cci_fetch (int mapped_stmt_id, T_CCI_ERROR * err_buf)
{
#ifdef CCI_FULL_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("(%d:%d)cci_fetch", CON_ID (mapped_stmt_id),
		    REQ_ID (mapped_stmt_id)));
#endif

  return fetch_cmd (mapped_stmt_id, 0, err_buf);
}

int
cci_fetch_sensitive (int mapped_stmt_id, T_CCI_ERROR * err_buf)
{
#ifdef CCI_FULL_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("(%d:%d)cci_fetch_sensitive", CON_ID (mapped_stmt_id),
		    REQ_ID (mapped_stmt_id)));
#endif

  return fetch_cmd (mapped_stmt_id, CCI_FETCH_SENSITIVE, err_buf);
}

int
cci_get_data (int mapped_stmt_id, int col_no, int a_type, void *value,
	      int *indicator)
{
  T_REQ_HANDLE *req_handle = NULL;
  T_CON_HANDLE *con_handle = NULL;
  int error = 0;

#ifdef CCI_FULL_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("(%d:%d)cci_get_data: %d %s",
				    CON_ID (mapped_stmt_id),
				    REQ_ID (mapped_stmt_id), col_no,
				    dbg_a_type_str (a_type)));
#endif

  if (indicator == NULL || value == NULL)
    {
      return CCI_ER_INVALID_ARGS;
    }

  *indicator = -1;

  error = hm_get_statement (mapped_stmt_id, &con_handle, &req_handle);
  if (error != CCI_ER_NO_ERROR)
    {
      return error;
    }
  reset_error_buffer (&(con_handle->err_buf));

  error =
    qe_get_data (con_handle, req_handle, col_no, a_type, value, indicator);

  con_handle->used = false;

  return error;
}

static int
cci_schema_info_internal (int mapped_conn_id, T_CCI_SCH_TYPE type, char *arg1,
			  char *arg2, char flag, int shard_id,
			  T_CCI_ERROR * err_buf)
{
  T_CON_HANDLE *con_handle = NULL;
  T_REQ_HANDLE *req_handle = NULL;
  int statement_id = -1;
  int error = CCI_ER_NO_ERROR;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("cci_schema_info %d %s %s %s %d",
				    mapped_conn_id, dbg_sch_type_str (type),
				    DEBUG_STR (arg1), DEBUG_STR (arg2),
				    flag));
#endif

  reset_error_buffer (err_buf);
  error = hm_get_connection (mapped_conn_id, &con_handle);
  if (error != CCI_ER_NO_ERROR)
    {
      set_error_buffer (err_buf, error, NULL);
      return error;
    }
  reset_error_buffer (&(con_handle->err_buf));

  statement_id = hm_req_handle_alloc (con_handle, &req_handle);
  if (statement_id < 0)
    {
      error = statement_id;
      goto ret;
    }

  error = qe_schema_info (req_handle, con_handle, (int) type, arg1, arg2,
			  flag, shard_id, &(con_handle->err_buf));
  while (IS_OUT_TRAN (con_handle)
	 && IS_ER_TO_RECONNECT (error, con_handle->err_buf.err_code))
    {
      if (NEED_TO_RECONNECT (con_handle, error))
	{
	  /* Finally, reset_connect will return ER_TIMEOUT */
	  error = reset_connect (con_handle, NULL, &(con_handle->err_buf));
	  if (error != CCI_ER_NO_ERROR)
	    {
	      break;
	    }
	}

      error = qe_schema_info (req_handle, con_handle, (int) type, arg1, arg2,
			      flag, shard_id, &(con_handle->err_buf));
    }

  if (error < 0)
    {
      hm_req_handle_free (con_handle, req_handle);
      req_handle = NULL;
    }

ret:
  con_handle->used = false;

  if (error == CCI_ER_NO_ERROR)
    {
      map_open_ots (statement_id, &req_handle->mapped_stmt_id);
      return req_handle->mapped_stmt_id;
    }

  set_error_buffer (&(con_handle->err_buf), error, NULL);
  get_last_error (con_handle, err_buf);

  return error;
}

int
cci_schema_info (int mapped_conn_id, T_CCI_SCH_TYPE type, char *arg1,
		 char *arg2, char flag, T_CCI_ERROR * err_buf)
{
  int error;

  error = cci_schema_info_internal (mapped_conn_id, type, arg1, arg2,
				    flag, 0 /* SHARD #0 */ , err_buf);
  return error;
}

int
cci_shard_schema_info (int mapped_conn_id, int shard_id, T_CCI_SCH_TYPE type,
		       char *class_name, char *attr_name, char flag,
		       T_CCI_ERROR * err_buf)
{
  int error;
  int is_shard;

  is_shard = cci_is_shard (mapped_conn_id, err_buf);
  if (is_shard == 0)
    {
      error = CCI_ER_NO_SHARD_AVAILABLE;
      return error;
    }

  error = cci_schema_info_internal (mapped_conn_id, type, class_name,
				    attr_name, flag, shard_id, err_buf);

  return error;
}

int
cci_is_shard (int mapped_conn_id, T_CCI_ERROR * err_buf)
{
  T_CON_HANDLE *con_handle = NULL;
  int is_shard = 0;
  int error = CCI_ER_NO_ERROR;

  reset_error_buffer (err_buf);
  error = hm_get_connection (mapped_conn_id, &con_handle);
  if (error != CCI_ER_NO_ERROR)
    {
      return error;
    }
  is_shard = qe_is_shard (con_handle);
  con_handle->used = false;

  return is_shard;
}

int
cci_get_cur_oid (int mapped_stmt_id, char *oid_str_buf)
{
  T_CON_HANDLE *con_handle = NULL;
  T_REQ_HANDLE *req_handle = NULL;
  int error = 0;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("cci_get_cur_oid %d", mapped_stmt_id));
#endif
  if (oid_str_buf == NULL)
    {
      return CCI_ER_INVALID_ARGS;
    }

  error = hm_get_statement (mapped_stmt_id, &con_handle, &req_handle);
  if (error != CCI_ER_NO_ERROR)
    {
      return error;
    }

  error = qe_get_cur_oid (req_handle, oid_str_buf);
  con_handle->used = false;

  return error;
}

int
cci_oid_get (int mapped_conn_id, char *oid_str, char **attr_name,
	     T_CCI_ERROR * err_buf)
{
  T_CON_HANDLE *con_handle = NULL;
  T_REQ_HANDLE *req_handle = NULL;
  int statement_id = -1;
  int error = CCI_ER_NO_ERROR;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("cci_oid_get %d", mapped_conn_id));
#endif

  reset_error_buffer (err_buf);
  error = hm_get_connection (mapped_conn_id, &con_handle);
  if (error != CCI_ER_NO_ERROR)
    {
      set_error_buffer (err_buf, error, NULL);
      return error;
    }
  reset_error_buffer (&(con_handle->err_buf));

  error = 0;

  statement_id = hm_req_handle_alloc (con_handle, &req_handle);
  if (statement_id < 0)
    {
      error = statement_id;
      goto ret;
    }

  error = qe_oid_get (req_handle, con_handle, oid_str, attr_name,
		      &(con_handle->err_buf));
  while (IS_OUT_TRAN (con_handle)
	 && IS_ER_TO_RECONNECT (error, con_handle->err_buf.err_code))
    {
      if (NEED_TO_RECONNECT (con_handle, error))
	{
	  /* Finally, reset_connect will return ER_TIMEOUT */
	  error = reset_connect (con_handle, NULL, &(con_handle->err_buf));
	  if (error != CCI_ER_NO_ERROR)
	    {
	      break;
	    }
	}

      error = qe_oid_get (req_handle, con_handle, oid_str, attr_name,
			  &(con_handle->err_buf));
    }

  if (error < 0)
    {
      hm_req_handle_free (con_handle, req_handle);
      req_handle = NULL;
    }

ret:
  con_handle->used = false;

  if (error == CCI_ER_NO_ERROR)
    {
      map_open_ots (statement_id, &req_handle->mapped_stmt_id);
      return req_handle->mapped_stmt_id;
    }

  set_error_buffer (&(con_handle->err_buf), error, NULL);
  get_last_error (con_handle, err_buf);

  return error;
}

int
cci_oid_put (int mapped_conn_id, char *oid_str, char **attr_name,
	     char **new_val, T_CCI_ERROR * err_buf)
{
  T_CON_HANDLE *con_handle = NULL;
  int error = CCI_ER_NO_ERROR;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("cci_oid_put %d", mapped_conn_id));
#endif

  reset_error_buffer (err_buf);
  if (attr_name == NULL || new_val == NULL)
    {
      /*  oid_str would be checked in qe_oid_put */
      set_error_buffer (err_buf, CCI_ER_INVALID_ARGS, NULL);
      return CCI_ER_INVALID_ARGS;
    }

  error = hm_get_connection (mapped_conn_id, &con_handle);
  if (error != CCI_ER_NO_ERROR)
    {
      set_error_buffer (err_buf, error, NULL);
      return error;
    }
  reset_error_buffer (&(con_handle->err_buf));

  error = qe_oid_put (con_handle, oid_str, attr_name, new_val,
		      &(con_handle->err_buf));
  while (IS_OUT_TRAN (con_handle)
	 && IS_ER_TO_RECONNECT (error, con_handle->err_buf.err_code))
    {
      if (NEED_TO_RECONNECT (con_handle, error))
	{
	  /* Finally, reset_connect will return ER_TIMEOUT */
	  error = reset_connect (con_handle, NULL, &(con_handle->err_buf));
	  if (error != CCI_ER_NO_ERROR)
	    {
	      break;
	    }
	}

      error = qe_oid_put (con_handle, oid_str, attr_name, new_val,
			  &(con_handle->err_buf));
    }

  set_error_buffer (&(con_handle->err_buf), error, NULL);
  get_last_error (con_handle, err_buf);
  con_handle->used = false;

  return error;
}

int
cci_oid_put2 (int mapped_conn_id, char *oid_str, char **attr_name,
	      void **new_val, int *a_type, T_CCI_ERROR * err_buf)
{
  T_CON_HANDLE *con_handle = NULL;
  int error = CCI_ER_NO_ERROR;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("cci_oid_put2 %d", mapped_conn_id));
#endif

  reset_error_buffer (err_buf);
  if (attr_name == NULL || new_val == NULL || a_type == NULL)
    {
      /* oid_str would be checked in qe_oid_put2 */
      set_error_buffer (err_buf, CCI_ER_INVALID_ARGS, NULL);
      return CCI_ER_INVALID_ARGS;
    }

  error = hm_get_connection (mapped_conn_id, &con_handle);
  if (error != CCI_ER_NO_ERROR)
    {
      set_error_buffer (err_buf, error, NULL);
      return error;
    }
  reset_error_buffer (&(con_handle->err_buf));

  error = qe_oid_put2 (con_handle, oid_str, attr_name, new_val, a_type,
		       &(con_handle->err_buf));
  while (IS_OUT_TRAN (con_handle)
	 && IS_ER_TO_RECONNECT (error, con_handle->err_buf.err_code))
    {
      if (NEED_TO_RECONNECT (con_handle, error))
	{
	  /* Finally, reset_connect will return ER_TIMEOUT */
	  error = reset_connect (con_handle, NULL, &(con_handle->err_buf));
	  if (error != CCI_ER_NO_ERROR)
	    {
	      break;
	    }
	}

      error = qe_oid_put2 (con_handle, oid_str, attr_name, new_val, a_type,
			   &(con_handle->err_buf));
    }

  set_error_buffer (&(con_handle->err_buf), error, NULL);
  get_last_error (con_handle, err_buf);
  con_handle->used = false;

  return error;
}

int
cci_set_autocommit (int mapped_conn_id, CCI_AUTOCOMMIT_MODE autocommit_mode)
{
  T_CON_HANDLE *con_handle = NULL;
  int error = 0;

#ifdef CCI_FULL_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("(%d)cci_set_autocommit: %d", mapped_conn_id,
		    autocommit_mode));
#endif

  error = hm_get_connection (mapped_conn_id, &con_handle);
  if (error != CCI_ER_NO_ERROR)
    {
      return error;
    }
  reset_error_buffer (&(con_handle->err_buf));

  if (autocommit_mode != con_handle->autocommit_mode
      && IS_IN_TRAN (con_handle))
    {
      error = qe_end_tran (con_handle, CCI_TRAN_COMMIT, &con_handle->err_buf);
    }

  if (error == 0)
    {
      con_handle->autocommit_mode = autocommit_mode;
    }
  con_handle->used = false;

  return error;
}

CCI_AUTOCOMMIT_MODE
cci_get_autocommit (int mapped_conn_id)
{
  T_CON_HANDLE *con_handle = NULL;
  int error = 0;

#ifdef CCI_FULL_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("(%d)cci_get_autocommit: current mode %d", mapped_conn_id,
		    con_handle->autocommit_mode));
#endif

  error = hm_get_connection (mapped_conn_id, &con_handle);
  if (error != CCI_ER_NO_ERROR)
    {
      return error;
    }
  reset_error_buffer (&(con_handle->err_buf));
  con_handle->used = false;

  return con_handle->autocommit_mode;
}

int
cci_set_holdability (int mapped_conn_id, int holdable)
{
  T_CON_HANDLE *con_handle = NULL;
  int error = 0;

  if (holdable < 0 || holdable > 1)
    {
      return CCI_ER_INVALID_HOLDABILITY;
    }

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("cci_set_con_handle_holdable %d", holdable));
#endif

  error = hm_get_connection (mapped_conn_id, &con_handle);
  if (error != CCI_ER_NO_ERROR)
    {
      return error;
    }
  reset_error_buffer (&(con_handle->err_buf));

  hm_set_con_handle_holdable (con_handle, holdable);
  con_handle->used = false;

  return error;
}

int
cci_get_holdability (int mapped_conn_id)
{
  T_CON_HANDLE *con_handle = NULL;
  int error = 0;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("cci_get_con_handle_holdable %d",
		    con_handle->is_holdable));
#endif

  error = hm_get_connection (mapped_conn_id, &con_handle);
  if (error != CCI_ER_NO_ERROR)
    {
      return error;
    }
  reset_error_buffer (&(con_handle->err_buf));
  con_handle->used = false;

  return hm_get_con_handle_holdable (con_handle);
}

int
cci_get_db_version (int mapped_conn_id, char *out_buf, int buf_size)
{
  T_CON_HANDLE *con_handle = NULL;
  int error = 0;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("(%d)cci_get_db_version", mapped_conn_id));
#endif

  if (out_buf && buf_size >= 1)
    {
      out_buf[0] = '\0';
    }

  error = hm_get_connection (mapped_conn_id, &con_handle);
  if (error != CCI_ER_NO_ERROR)
    {
      return error;
    }
  reset_error_buffer (&(con_handle->err_buf));

  API_SLOG (con_handle);
  SET_START_TIME_FOR_QUERY (con_handle, NULL);

  error = qe_get_db_version (con_handle, out_buf, buf_size);
  while (IS_OUT_TRAN (con_handle)
	 && IS_ER_TO_RECONNECT (error, con_handle->err_buf.err_code))
    {
      if (NEED_TO_RECONNECT (con_handle, error))
	{
	  /* Finally, reset_connect will return ER_TIMEOUT */
	  error = reset_connect (con_handle, NULL, &(con_handle->err_buf));
	  if (error != CCI_ER_NO_ERROR)
	    {
	      break;
	    }
	}

      error = qe_get_db_version (con_handle, out_buf, buf_size);
    }

  API_ELOG (con_handle, error);
  con_handle->used = false;

  RESET_START_TIME (con_handle);
  return error;
}

int
cci_get_class_num_objs (int mapped_conn_id, char *class_name, int flag,
			int *num_objs, int *num_pages, T_CCI_ERROR * err_buf)
{
  T_CON_HANDLE *con_handle = NULL;
  int error = CCI_ER_NO_ERROR;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("(%d)cci_get_class_num_objs", mapped_conn_id));
#endif

  reset_error_buffer (err_buf);
  if (class_name == NULL)
    {
      set_error_buffer (err_buf, CCI_ER_INVALID_ARGS, NULL);
      return CCI_ER_INVALID_ARGS;
    }

  error = hm_get_connection (mapped_conn_id, &con_handle);
  if (error != CCI_ER_NO_ERROR)
    {
      set_error_buffer (err_buf, error, NULL);
      return error;
    }
  reset_error_buffer (&(con_handle->err_buf));

  error = qe_get_class_num_objs (con_handle, class_name, (char) flag,
				 num_objs, num_pages, &(con_handle->err_buf));
  while (IS_OUT_TRAN (con_handle)
	 && IS_ER_TO_RECONNECT (error, con_handle->err_buf.err_code))
    {
      if (NEED_TO_RECONNECT (con_handle, error))
	{
	  /* Finally, reset_connect will return ER_TIMEOUT */
	  error = reset_connect (con_handle, NULL, &(con_handle->err_buf));
	  if (error != CCI_ER_NO_ERROR)
	    {
	      break;
	    }
	}

      error = qe_get_class_num_objs (con_handle, class_name, (char) flag,
				     num_objs, num_pages,
				     &(con_handle->err_buf));
    }

  set_error_buffer (&(con_handle->err_buf), error, NULL);
  get_last_error (con_handle, err_buf);
  con_handle->used = false;

  return error;
}

int
cci_oid (int mapped_conn_id, T_CCI_OID_CMD cmd, char *oid_str,
	 T_CCI_ERROR * err_buf)
{
  T_CON_HANDLE *con_handle = NULL;
  int error = CCI_ER_NO_ERROR;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("(%d)cci_oid: %s", mapped_conn_id,
		    dbg_oid_cmd_str (cmd)));
#endif

  reset_error_buffer (err_buf);
  error = hm_get_connection (mapped_conn_id, &con_handle);
  if (error != CCI_ER_NO_ERROR)
    {
      set_error_buffer (err_buf, error, NULL);
      return error;
    }
  reset_error_buffer (&(con_handle->err_buf));

  error = qe_oid_cmd (con_handle, (char) cmd, oid_str, NULL, 0,
		      &(con_handle->err_buf));
  while (IS_OUT_TRAN (con_handle)
	 && IS_ER_TO_RECONNECT (error, con_handle->err_buf.err_code))
    {
      if (NEED_TO_RECONNECT (con_handle, error))
	{
	  /* Finally, reset_connect will return ER_TIMEOUT */
	  error = reset_connect (con_handle, NULL, &(con_handle->err_buf));
	  if (error != CCI_ER_NO_ERROR)
	    {
	      break;
	    }
	}

      error = qe_oid_cmd (con_handle, (char) cmd, oid_str, NULL, 0,
			  &(con_handle->err_buf));
    }

  set_error_buffer (&(con_handle->err_buf), error, NULL);
  get_last_error (con_handle, err_buf);
  con_handle->used = false;

  return error;
}

int
cci_oid_get_class_name (int mapped_conn_id, char *oid_str, char *out_buf,
			int out_buf_size, T_CCI_ERROR * err_buf)
{
  T_CON_HANDLE *con_handle = NULL;
  int error = CCI_ER_NO_ERROR;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("(%d)cci_oid_get_class_name", mapped_conn_id));
#endif

  reset_error_buffer (err_buf);
  error = hm_get_connection (mapped_conn_id, &con_handle);
  if (error != CCI_ER_NO_ERROR)
    {
      set_error_buffer (err_buf, error, NULL);
      return error;
    }
  reset_error_buffer (&(con_handle->err_buf));

  error = qe_oid_cmd (con_handle, (char) CCI_OID_CLASS_NAME, oid_str,
		      out_buf, out_buf_size, &(con_handle->err_buf));
  while (IS_OUT_TRAN (con_handle)
	 && IS_ER_TO_RECONNECT (error, con_handle->err_buf.err_code))
    {
      if (NEED_TO_RECONNECT (con_handle, error))
	{
	  /* Finally, reset_connect will return ER_TIMEOUT */
	  error = reset_connect (con_handle, NULL, &(con_handle->err_buf));
	  if (error != CCI_ER_NO_ERROR)
	    {
	      break;
	    }
	}

      error = qe_oid_cmd (con_handle, (char) CCI_OID_CLASS_NAME, oid_str,
			  out_buf, out_buf_size, &(con_handle->err_buf));
    }

  set_error_buffer (&(con_handle->err_buf), error, NULL);
  get_last_error (con_handle, err_buf);
  con_handle->used = false;

  return error;
}

int
cci_col_get (int mapped_conn_id, char *oid_str, char *col_attr, int *col_size,
	     int *col_type, T_CCI_ERROR * err_buf)
{
  int error = CCI_ER_NO_ERROR;
  int statement_id = -1;
  T_CON_HANDLE *con_handle = NULL;
  T_REQ_HANDLE *req_handle = NULL;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("(%d)cci_col_get", mapped_conn_id));
#endif

  reset_error_buffer (err_buf);
  error = hm_get_connection (mapped_conn_id, &con_handle);
  if (error != CCI_ER_NO_ERROR)
    {
      set_error_buffer (err_buf, error, NULL);
      return error;
    }
  reset_error_buffer (&(con_handle->err_buf));

  statement_id = hm_req_handle_alloc (con_handle, &req_handle);
  if (statement_id < 0)
    {
      error = statement_id;
      goto error;
    }

  error = qe_col_get (req_handle, con_handle, oid_str, col_attr, col_size,
		      col_type, &(con_handle->err_buf));
  while (IS_OUT_TRAN (con_handle)
	 && IS_ER_TO_RECONNECT (error, con_handle->err_buf.err_code))
    {
      if (NEED_TO_RECONNECT (con_handle, error))
	{
	  /* Finally, reset_connect will return ER_TIMEOUT */
	  error = reset_connect (con_handle, NULL, &(con_handle->err_buf));
	  if (error != CCI_ER_NO_ERROR)
	    {
	      break;
	    }
	}

      error = qe_col_get (req_handle, con_handle, oid_str, col_attr, col_size,
			  col_type, &(con_handle->err_buf));
    }

  if (error < 0)
    {
      hm_req_handle_free (con_handle, req_handle);
      req_handle = NULL;
      goto error;
    }

  map_open_ots (statement_id, &req_handle->mapped_stmt_id);
  con_handle->used = false;

  return req_handle->mapped_stmt_id;

error:
  set_error_buffer (&(con_handle->err_buf), error, NULL);
  get_last_error (con_handle, err_buf);
  con_handle->used = false;

  return error;
}

int
cci_col_size (int mapped_conn_id, char *oid_str, char *col_attr,
	      int *col_size, T_CCI_ERROR * err_buf)
{
  int error = CCI_ER_NO_ERROR;
  T_CON_HANDLE *con_handle = NULL;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("(%d)cci_col_size", mapped_conn_id));
#endif

  reset_error_buffer (err_buf);
  error = hm_get_connection (mapped_conn_id, &con_handle);
  if (error != CCI_ER_NO_ERROR)
    {
      set_error_buffer (err_buf, error, NULL);
      return error;
    }
  reset_error_buffer (&(con_handle->err_buf));

  error = qe_col_size (con_handle, oid_str, col_attr, col_size,
		       &(con_handle->err_buf));
  while (IS_OUT_TRAN (con_handle)
	 && IS_ER_TO_RECONNECT (error, con_handle->err_buf.err_code))
    {
      if (NEED_TO_RECONNECT (con_handle, error))
	{
	  /* Finally, reset_connect will return ER_TIMEOUT */
	  error = reset_connect (con_handle, NULL, &(con_handle->err_buf));
	  if (error != CCI_ER_NO_ERROR)
	    {
	      break;
	    }
	}

      error = qe_col_size (con_handle, oid_str, col_attr, col_size,
			   &(con_handle->err_buf));
    }

  set_error_buffer (&(con_handle->err_buf), error, NULL);
  get_last_error (con_handle, err_buf);
  con_handle->used = false;

  return error;
}

int
cci_col_set_drop (int mapped_conn_id, char *oid_str, char *col_attr,
		  char *value, T_CCI_ERROR * err_buf)
{
#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("(%d)cci_col_set_drop", mapped_conn_id));
#endif

  return col_set_add_drop (mapped_conn_id, CCI_COL_SET_DROP, oid_str,
			   col_attr, value, err_buf);
}

int
cci_col_set_add (int mapped_conn_id, char *oid_str, char *col_attr,
		 char *value, T_CCI_ERROR * err_buf)
{
#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("(%d)cci_col_set_add", mapped_conn_id));
#endif

  return col_set_add_drop (mapped_conn_id, CCI_COL_SET_ADD, oid_str, col_attr,
			   value, err_buf);
}

int
cci_col_seq_drop (int mapped_conn_id, char *oid_str, char *col_attr,
		  int index, T_CCI_ERROR * err_buf)
{
#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("(%d)cci_coil_seq_drop", mapped_conn_id));
#endif

  return col_seq_op (mapped_conn_id, CCI_COL_SEQ_DROP, oid_str, col_attr,
		     index, "", err_buf);
}

int
cci_col_seq_insert (int mapped_conn_id, char *oid_str, char *col_attr,
		    int index, char *value, T_CCI_ERROR * err_buf)
{
#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("(%d)cci_col_seq_insert", mapped_conn_id));
#endif

  return col_seq_op (mapped_conn_id, CCI_COL_SEQ_INSERT, oid_str, col_attr,
		     index, value, err_buf);
}

int
cci_col_seq_put (int mapped_conn_id, char *oid_str, char *col_attr, int index,
		 char *value, T_CCI_ERROR * err_buf)
{
#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("(%d)cci_col_seq_put", mapped_conn_id));
#endif

  return col_seq_op (mapped_conn_id, CCI_COL_SEQ_PUT, oid_str, col_attr,
		     index, value, err_buf);
}

int
cci_query_result_free (T_CCI_QUERY_RESULT * qr, int num_q)
{
#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("cci_query_result_free"));
#endif

  qe_query_result_free (num_q, qr);

  return CCI_ER_NO_ERROR;
}

int
cci_cursor_update (int mapped_stmt_id, int cursor_pos, int index,
		   T_CCI_A_TYPE a_type, void *value, T_CCI_ERROR * err_buf)
{
  int error = CCI_ER_NO_ERROR;
  T_REQ_HANDLE *req_handle = NULL;
  T_CON_HANDLE *con_handle = NULL;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("(%d:%d)cci_cursor_update: %d %d %s %p",
		    CON_ID (mapped_stmt_id), REQ_ID (mapped_stmt_id),
		    cursor_pos, index, dbg_a_type_str (a_type), value));
#endif

  reset_error_buffer (err_buf);
  error = hm_get_statement (mapped_stmt_id, &con_handle, &req_handle);
  if (error != CCI_ER_NO_ERROR)
    {
      set_error_buffer (err_buf, error, NULL);
      return error;
    }
  reset_error_buffer (&(con_handle->err_buf));

  if ((req_handle->prepare_flag & CCI_PREPARE_UPDATABLE) == 0)
    {
      error = CCI_ER_NOT_UPDATABLE;
    }
  else
    {
      error = qe_cursor_update (req_handle, con_handle, cursor_pos, index,
				a_type, value, &(con_handle->err_buf));
    }

  set_error_buffer (&(con_handle->err_buf), error, NULL);
  get_last_error (con_handle, err_buf);
  con_handle->used = false;

  return error;
}

/*
 * For the purpose of re-balancing existing connections, cci_prepare,
 * cci_execute, cci_execute_array, cci_prepare_and_execute,
 * cci_execute_batch require to forcefully disconnect the current
 * con_handle when it is in the OUT_TRAN state and the time elapsed
 * after the last failure of a host is over rc_time.
 */
int
cci_execute_batch (int mapped_conn_id, int num_query, char **sql_stmt,
		   T_CCI_QUERY_RESULT ** qr, T_CCI_ERROR * err_buf)
{
  T_CON_HANDLE *con_handle = NULL;
  int error = CCI_ER_NO_ERROR;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("(%d)cci_execute_batch: %d", mapped_conn_id, num_query));
#endif

  reset_error_buffer (err_buf);
  if (qr == NULL || sql_stmt == NULL)
    {
      set_error_buffer (err_buf, CCI_ER_INVALID_ARGS, NULL);
      return CCI_ER_INVALID_ARGS;
    }

  *qr = NULL;
  error = hm_get_connection (mapped_conn_id, &con_handle);
  if (error != CCI_ER_NO_ERROR)
    {
      set_error_buffer (err_buf, error, NULL);
      return error;
    }
  reset_error_buffer (&(con_handle->err_buf));
  con_handle->shard_id = CCI_SHARD_ID_INVALID;

  if (num_query <= 0)
    {
      goto ret;
    }

  if (IS_OUT_TRAN (con_handle) && IS_FORCE_FAILBACK (con_handle)
      && !IS_INVALID_SOCKET (con_handle->sock_fd))
    {
      hm_force_close_connection (con_handle);
    }
  SET_START_TIME_FOR_QUERY (con_handle, NULL);

  error = qe_execute_batch (con_handle, num_query, sql_stmt, qr,
			    &(con_handle->err_buf));
  while (IS_OUT_TRAN (con_handle)
	 && IS_ER_TO_RECONNECT (error, con_handle->err_buf.err_code))
    {
      if (NEED_TO_RECONNECT (con_handle, error))
	{
	  /* Finally, reset_connect will return ER_TIMEOUT */
	  error = reset_connect (con_handle, NULL, &(con_handle->err_buf));
	  if (error != CCI_ER_NO_ERROR)
	    {
	      break;
	    }
	}

      error = qe_execute_batch (con_handle, num_query, sql_stmt, qr,
				&(con_handle->err_buf));
    }

  if (error == CCI_ER_QUERY_TIMEOUT &&
      con_handle->disconnect_on_query_timeout)
    {
      CLOSE_SOCKET (con_handle->sock_fd);
      con_handle->sock_fd = INVALID_SOCKET;
      con_handle->con_status = CCI_CON_STATUS_OUT_TRAN;
    }

  RESET_START_TIME (con_handle);

ret:
  set_error_buffer (&(con_handle->err_buf), error, NULL);
  get_last_error (con_handle, err_buf);
  con_handle->used = false;

  return error;
}

int
cci_fetch_buffer_clear (int mapped_stmt_id)
{
  T_CON_HANDLE *con_handle = NULL;
  T_REQ_HANDLE *req_handle = NULL;
  int error;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("(%d:%d)cci_fetch_buffer_clear", CON_ID (mapped_stmt_id),
		    REQ_ID (mapped_stmt_id)));
#endif

  error = hm_get_statement (mapped_stmt_id, &con_handle, &req_handle);
  if (error != CCI_ER_NO_ERROR)
    {
      return error;
    }

  hm_req_handle_fetch_buf_free (req_handle);
  con_handle->used = false;

  return CCI_ER_NO_ERROR;
}

int
cci_execute_result (int mapped_stmt_id, T_CCI_QUERY_RESULT ** qr,
		    T_CCI_ERROR * err_buf)
{
  T_REQ_HANDLE *req_handle = NULL;
  T_CON_HANDLE *con_handle = NULL;
  int error = CCI_ER_NO_ERROR;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("(%d:%d)cci_execute_result", CON_ID (mapped_stmt_id),
		    REQ_ID (mapped_stmt_id)));
#endif

  if (qr == NULL)
    {
      return CCI_ER_INVALID_ARGS;
    }

  *qr = NULL;

  reset_error_buffer (err_buf);
  error = hm_get_statement (mapped_stmt_id, &con_handle, &req_handle);
  if (error != CCI_ER_NO_ERROR)
    {
      set_error_buffer (err_buf, error, NULL);
      return error;
    }
  reset_error_buffer (&(con_handle->err_buf));

  if (qr != NULL)
    {
      *qr = NULL;
    }

  error = qe_query_result_copy (req_handle, qr);

  set_error_buffer (&(con_handle->err_buf), error, NULL);
  get_last_error (con_handle, err_buf);
  con_handle->used = false;

  return error;
}

int
cci_set_login_timeout (int mapped_conn_id, int timeout, T_CCI_ERROR * err_buf)
{
  T_CON_HANDLE *con_handle = NULL;
  int error = CCI_ER_NO_ERROR;

  reset_error_buffer (err_buf);
  if (timeout < 0)
    {
      set_error_buffer (err_buf, CCI_ER_INVALID_ARGS, NULL);
      return CCI_ER_INVALID_ARGS;
    }

  error = hm_get_connection (mapped_conn_id, &con_handle);
  if (error != CCI_ER_NO_ERROR)
    {
      set_error_buffer (err_buf, error, NULL);
      return error;
    }
  reset_error_buffer (&(con_handle->err_buf));

  con_handle->login_timeout = timeout;
  con_handle->used = false;

  return error;
}

int
cci_get_login_timeout (int mapped_conn_id, int *val, T_CCI_ERROR * err_buf)
{
  T_CON_HANDLE *con_handle = NULL;
  int error = CCI_ER_NO_ERROR;

  reset_error_buffer (err_buf);

  if (val == NULL)
    {
      set_error_buffer (err_buf, CCI_ER_INVALID_ARGS, NULL);
      return CCI_ER_INVALID_ARGS;
    }

  error = hm_get_connection (mapped_conn_id, &con_handle);
  if (error != CCI_ER_NO_ERROR)
    {
      set_error_buffer (err_buf, error, NULL);
      return error;
    }
  reset_error_buffer (&(con_handle->err_buf));

  *val = con_handle->login_timeout;
  con_handle->used = false;

  return error;
}

int
cci_set_isolation_level (int mapped_conn_id, T_CCI_TRAN_ISOLATION val,
			 T_CCI_ERROR * err_buf)
{
  return cci_set_db_parameter (mapped_conn_id, CCI_PARAM_ISOLATION_LEVEL,
			       &val, err_buf);
}

int
cci_set_lock_timeout (int mapped_conn_id, int val, T_CCI_ERROR * err_buf)
{
  return cci_set_db_parameter (mapped_conn_id, CCI_PARAM_LOCK_TIMEOUT, &val,
			       err_buf);
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
  if (value == NULL || ind == NULL)
    {
      return CCI_ER_INVALID_ARGS;
    }

  return (t_set_get ((T_SET *) set, index, a_type, value, ind));
}

int
cci_set_make (T_CCI_SET * set, T_CCI_U_TYPE u_type, int size, void *value,
	      int *ind)
{
  T_SET *tmp_set;
  int error;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("cci_set_make"));
#endif
  if (value == NULL || ind == NULL)
    {
      return CCI_ER_INVALID_ARGS;
    }

  if ((error = t_set_alloc (&tmp_set)) < 0)
    {
      return error;
    }

  if ((error = t_set_make (tmp_set, (char) u_type, size, value, ind)) < 0)
    {
      return error;
    }

  if (set != NULL)
    {
      *set = tmp_set;
    }
  return 0;
}

int
cci_get_attr_type_str (int mapped_conn_id, char *class_name, char *attr_name,
		       char *buf, int buf_size, T_CCI_ERROR * err_buf)
{
  T_CON_HANDLE *con_handle = NULL;
  int error = CCI_ER_NO_ERROR;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("(%d)cci_get_attr_type_str", mapped_conn_id));
#endif

  reset_error_buffer (err_buf);
  if (class_name == NULL || attr_name == NULL)
    {
      set_error_buffer (err_buf, CCI_ER_INVALID_ARGS, NULL);
      return CCI_ER_INVALID_ARGS;
    }

  error = hm_get_connection (mapped_conn_id, &con_handle);
  if (error != CCI_ER_NO_ERROR)
    {
      set_error_buffer (err_buf, error, NULL);
      return error;
    }
  reset_error_buffer (&(con_handle->err_buf));

  error = qe_get_attr_type_str (con_handle, class_name, attr_name, buf,
				buf_size, &(con_handle->err_buf));
  while (IS_OUT_TRAN (con_handle)
	 && IS_ER_TO_RECONNECT (error, con_handle->err_buf.err_code))
    {
      if (NEED_TO_RECONNECT (con_handle, error))
	{
	  /* Finally, reset_connect will return ER_TIMEOUT */
	  error = reset_connect (con_handle, NULL, &(con_handle->err_buf));
	  if (error != CCI_ER_NO_ERROR)
	    {
	      break;
	    }
	}

      error = qe_get_attr_type_str (con_handle, class_name, attr_name, buf,
				    buf_size, &(con_handle->err_buf));
    }

  set_error_buffer (&(con_handle->err_buf), error, NULL);
  get_last_error (con_handle, err_buf);
  con_handle->used = false;

  return error;
}

int
cci_get_query_plan (int mapped_stmt_id, char **out_buf)
{
  T_REQ_HANDLE *req_handle = NULL;
  T_CON_HANDLE *con_handle = NULL;
  int error = 0;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("(%d:%d)cci_get_query_plan", CON_ID (mapped_stmt_id),
		    REQ_ID (mapped_stmt_id)));
#endif

  error = hm_get_statement (mapped_stmt_id, &con_handle, &req_handle);
  if (error != CCI_ER_NO_ERROR)
    {
      return error;
    }
  reset_error_buffer (&(con_handle->err_buf));

  error = qe_get_query_info (req_handle, con_handle, CAS_GET_QUERY_INFO_PLAN,
			     out_buf);
  con_handle->used = false;

  return error;
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
cci_set_max_row (int mapped_stmt_id, int max_row)
{
  T_CON_HANDLE *con_handle = NULL;
  T_REQ_HANDLE *req_handle = NULL;
  int error = CCI_ER_NO_ERROR;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("(%d:%d)cci_set_max_row: %d", CON_ID (mapped_stmt_id),
		    REQ_ID (mapped_stmt_id), max_row));
#endif
  if (max_row < 0)
    {
      return CCI_ER_INVALID_ARGS;
    }
  error = hm_get_statement (mapped_stmt_id, &con_handle, &req_handle);
  if (error != CCI_ER_NO_ERROR)
    {
      return error;
    }

  req_handle->max_row = max_row;
  con_handle->used = false;

  return error;
}

int
cci_savepoint (int mapped_conn_id, T_CCI_SAVEPOINT_CMD cmd,
	       char *savepoint_name, T_CCI_ERROR * err_buf)
{
  T_CON_HANDLE *con_handle = NULL;
  int error = CCI_ER_NO_ERROR;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("(%d)cci_savepoint", mapped_conn_id));
#endif

  reset_error_buffer (err_buf);
  error = hm_get_connection (mapped_conn_id, &con_handle);
  if (error != CCI_ER_NO_ERROR)
    {
      set_error_buffer (err_buf, error, NULL);
      return error;
    }
  reset_error_buffer (&(con_handle->err_buf));

  error = qe_savepoint_cmd (con_handle, (char) cmd, savepoint_name,
			    &(con_handle->err_buf));
  while (IS_OUT_TRAN (con_handle)
	 && IS_ER_TO_RECONNECT (error, con_handle->err_buf.err_code))
    {
      if (NEED_TO_RECONNECT (con_handle, error))
	{
	  /* Finally, reset_connect will return ER_TIMEOUT */
	  error = reset_connect (con_handle, NULL, &(con_handle->err_buf));
	  if (error != CCI_ER_NO_ERROR)
	    {
	      break;
	    }
	}

      error = qe_savepoint_cmd (con_handle, (char) cmd, savepoint_name,
				&(con_handle->err_buf));
    }

  set_error_buffer (&(con_handle->err_buf), error, NULL);
  get_last_error (con_handle, err_buf);
  con_handle->used = false;

  return error;
}

int
cci_get_param_info (int mapped_stmt_id, T_CCI_PARAM_INFO ** param,
		    T_CCI_ERROR * err_buf)
{
  T_REQ_HANDLE *req_handle = NULL;
  T_CON_HANDLE *con_handle = NULL;
  int error = CCI_ER_NO_ERROR;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("(%d:%d)cci_get_param_infod", CON_ID (mapped_stmt_id),
		    REQ_ID (mapped_stmt_id)));
#endif

  reset_error_buffer (err_buf);
  error = hm_get_statement (mapped_stmt_id, &con_handle, &req_handle);
  if (error != CCI_ER_NO_ERROR)
    {
      set_error_buffer (err_buf, error, NULL);
      return error;
    }
  reset_error_buffer (&(con_handle->err_buf));

  if (param)
    {
      *param = NULL;
    }

  error = qe_get_param_info (req_handle, con_handle, param,
			     &(con_handle->err_buf));

  set_error_buffer (&(con_handle->err_buf), error, NULL);
  get_last_error (con_handle, err_buf);
  con_handle->used = false;

  return error;
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
cci_set_query_timeout (int mapped_stmt_id, int timeout)
{
  T_CON_HANDLE *con_handle = NULL;
  T_REQ_HANDLE *req_handle = NULL;
  int old_value;
  int error;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("(%d:%d)cci_set_query_timeout: %d",
		    CON_ID (mapped_stmt_id), REQ_ID (mapped_stmt_id),
		    timeout));
#endif

  if (timeout < 0)
    {
      return CCI_ER_INVALID_ARGS;
    }

  error = hm_get_statement (mapped_stmt_id, &con_handle, &req_handle);
  if (error != CCI_ER_NO_ERROR)
    {
      return error;
    }

  old_value = req_handle->query_timeout;
  req_handle->query_timeout = timeout;
  con_handle->used = false;

  return old_value;
}

int
cci_get_query_timeout (int mapped_stmt_id)
{
  T_CON_HANDLE *con_handle = NULL;
  T_REQ_HANDLE *req_handle = NULL;
  int error;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("(%d:%d)cci_get_query_timeout", CON_ID (mapped_stmt_id),
		    REQ_ID (mapped_stmt_id)));
#endif

  error = hm_get_statement (mapped_stmt_id, &con_handle, &req_handle);
  if (error != CCI_ER_NO_ERROR)
    {
      return error;
    }
  con_handle->used = false;

  return req_handle->query_timeout;
}

static int
cci_lob_new (int mapped_conn_id, void *lob, T_CCI_U_TYPE type,
	     T_CCI_ERROR * err_buf)
{
  T_CON_HANDLE *con_handle = NULL;
  int error = CCI_ER_NO_ERROR;
  T_LOB *lob_handle = NULL;

  reset_error_buffer (err_buf);
  error = hm_get_connection (mapped_conn_id, &con_handle);
  if (error != CCI_ER_NO_ERROR)
    {
      set_error_buffer (err_buf, error, NULL);
      return error;
    }
  reset_error_buffer (&(con_handle->err_buf));

  if (lob == NULL)
    {
      error = CCI_ER_INVALID_LOB_HANDLE;
      goto ret;
    }

  error = qe_lob_new (con_handle, &lob_handle, type, &(con_handle->err_buf));
  while (IS_OUT_TRAN (con_handle)
	 && IS_ER_TO_RECONNECT (error, con_handle->err_buf.err_code))
    {
      if (NEED_TO_RECONNECT (con_handle, error))
	{
	  /* Finally, reset_connect will return ER_TIMEOUT */
	  error = reset_connect (con_handle, NULL, &(con_handle->err_buf));
	  if (error != CCI_ER_NO_ERROR)
	    {
	      break;
	    }
	}

      error = qe_lob_new (con_handle, &lob_handle, type,
			  &(con_handle->err_buf));
    }

  if (error < 0)
    {
      goto ret;
    }

  if (type == CCI_U_TYPE_BLOB)
    {
      *(T_CCI_BLOB *) lob = (T_CCI_BLOB) lob_handle;
    }
  else if (type == CCI_U_TYPE_CLOB)
    {
      *(T_CCI_CLOB *) lob = (T_CCI_CLOB) lob_handle;
    }
  else
    {
      *(T_CCI_CLOB *) lob = NULL;
    }

ret:
  set_error_buffer (&(con_handle->err_buf), error, NULL);
  get_last_error (con_handle, err_buf);
  con_handle->used = false;

  return error;
}

int
cci_blob_new (int mapped_conn_id, T_CCI_BLOB * blob, T_CCI_ERROR * err_buf)
{
#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("(%d)cci_blob_new", mapped_conn_id));
#endif

  return cci_lob_new (mapped_conn_id, (void *) blob, CCI_U_TYPE_BLOB,
		      err_buf);
}

int
cci_clob_new (int mapped_conn_id, T_CCI_CLOB * clob, T_CCI_ERROR * err_buf)
{
#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("(%d)cci_clob_new", mapped_conn_id));
#endif

  return cci_lob_new (mapped_conn_id, (void *) clob, CCI_U_TYPE_CLOB,
		      err_buf);
}

static long long
cci_lob_size (void *lob)
{
  T_LOB *lob_handle = (T_LOB *) lob;

  if (lob == NULL)
    {
      return CCI_ER_INVALID_LOB_HANDLE;
    }

  return (long long) t_lob_get_size (lob_handle->handle);
}

long long
cci_blob_size (T_CCI_BLOB blob)
{
#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("cci_blob_size"));
#endif
  return cci_lob_size (blob);
}

long long
cci_clob_size (T_CCI_CLOB clob)
{
#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("cci_clob_size"));
#endif
  return cci_lob_size (clob);
}

static int
cci_lob_write (int mapped_conn_id, void *lob, long long start_pos,
	       int length, const char *buf, T_CCI_ERROR * err_buf)
{
  T_CON_HANDLE *con_handle = NULL;
  int error = CCI_ER_NO_ERROR;
  T_LOB *lob_handle = (T_LOB *) lob;
  int nwritten = 0;
  int current_write_len;

  reset_error_buffer (err_buf);
  if (buf == NULL)
    {
      set_error_buffer (err_buf, CCI_ER_INVALID_ARGS, NULL);
      return CCI_ER_INVALID_ARGS;
    }

  error = hm_get_connection (mapped_conn_id, &con_handle);
  if (error != CCI_ER_NO_ERROR)
    {
      set_error_buffer (err_buf, error, NULL);
      return error;
    }
  reset_error_buffer (&(con_handle->err_buf));

  if (lob == NULL)
    {
      error = CCI_ER_INVALID_LOB_HANDLE;
      goto ret;
    }

  if (IS_OUT_TRAN_STATUS (con_handle))
    {
      error = cas_connect (con_handle, &(con_handle->err_buf));
    }

  if (error >= 0)
    {
      while (nwritten < length)
	{
	  current_write_len = ((LOB_IO_LENGTH > length - nwritten)
			       ? (length - nwritten) : LOB_IO_LENGTH);
	  error = qe_lob_write (con_handle, lob_handle, start_pos + nwritten,
				current_write_len, buf + nwritten,
				&(con_handle->err_buf));
	  if (error >= 0)
	    {
	      nwritten += error;
	    }
	  else
	    {
	      break;
	    }
	}
    }

ret:
  con_handle->used = false;

  if (error >= 0)
    {
      return nwritten;
    }

  set_error_buffer (&(con_handle->err_buf), error, NULL);
  get_last_error (con_handle, err_buf);

  return error;
}

int
cci_blob_write (int mapped_conn_id, T_CCI_BLOB blob, long long start_pos,
		int length, const char *buf, T_CCI_ERROR * err_buf)
{
#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("(%d)cci_blob_write: lob %p pos %d len %d",
		    mapped_conn_id, blob, start_pos, length));
#endif

  return cci_lob_write (mapped_conn_id, blob, start_pos, length, buf,
			err_buf);
}

int
cci_clob_write (int mapped_conn_id, T_CCI_CLOB clob, long long start_pos,
		int length, const char *buf, T_CCI_ERROR * err_buf)
{
#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("(%d)cci_clob_write: lob %p pos %d len %d",
		    mapped_conn_id, clob, start_pos, length));
#endif

  return cci_lob_write (mapped_conn_id, clob, start_pos, length, buf,
			err_buf);
}

static int
cci_lob_read (int mapped_conn_id, void *lob, long long start_pos,
	      int length, char *buf, T_CCI_ERROR * err_buf)
{
  T_CON_HANDLE *con_handle = NULL;
  int error = CCI_ER_NO_ERROR;
  T_LOB *lob_handle = (T_LOB *) lob;
  int nread = 0;
  int current_read_len;
  INT64 lob_size;

  reset_error_buffer (err_buf);
  if (buf == NULL)
    {
      set_error_buffer (err_buf, CCI_ER_INVALID_ARGS, NULL);
      return CCI_ER_INVALID_ARGS;
    }

  error = hm_get_connection (mapped_conn_id, &con_handle);
  if (error != CCI_ER_NO_ERROR)
    {
      set_error_buffer (err_buf, error, NULL);
      return error;
    }
  reset_error_buffer (&(con_handle->err_buf));

  if (lob == NULL)
    {
      error = CCI_ER_INVALID_LOB_HANDLE;
      goto ret;
    }

  if (IS_OUT_TRAN_STATUS (con_handle))
    {
      error = cas_connect (con_handle, &(con_handle->err_buf));
    }

  lob_size = t_lob_get_size (lob_handle->handle);

  if (start_pos >= lob_size)
    {
      error = CCI_ER_INVALID_LOB_READ_POS;
    }

  if (error >= 0)
    {
      while (nread < length && start_pos + nread < lob_size)
	{
	  current_read_len = ((LOB_IO_LENGTH > length - nread)
			      ? (length - nread) : LOB_IO_LENGTH);
	  error = qe_lob_read (con_handle, lob_handle, start_pos + nread,
			       current_read_len, buf + nread,
			       &(con_handle->err_buf));
	  if (error >= 0)
	    {
	      nread += error;
	    }
	  else
	    {
	      break;
	    }
	}
    }

ret:
  con_handle->used = false;

  if (error >= 0)
    {
      return nread;
    }

  set_error_buffer (&(con_handle->err_buf), error, NULL);
  get_last_error (con_handle, err_buf);

  return error;
}


int
cci_blob_read (int mapped_conn_id, T_CCI_BLOB blob, long long start_pos,
	       int length, char *buf, T_CCI_ERROR * err_buf)
{
#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("(%d)cci_blob_read: lob %p pos %d len %d",
				    mapped_conn_id, blob, start_pos, length));
#endif

  return cci_lob_read (mapped_conn_id, blob, start_pos, length, buf, err_buf);
}


int
cci_clob_read (int mapped_conn_id, T_CCI_CLOB clob, long long start_pos,
	       int length, char *buf, T_CCI_ERROR * err_buf)
{
#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("(%d)cci_clob_read: lob %p pos %d len %d",
				    mapped_conn_id, clob, start_pos, length));
#endif

  return cci_lob_read (mapped_conn_id, clob, start_pos, length, buf, err_buf);
}


static int
cci_lob_free (void *lob)
{
  T_LOB *lob_handle = (T_LOB *) lob;

  if (lob == NULL)
    {
      return CCI_ER_INVALID_LOB_HANDLE;
    }

  FREE_MEM (lob_handle->handle);
  FREE_MEM (lob_handle);

  return 0;
}


int
cci_blob_free (T_CCI_BLOB blob)
{
  return cci_lob_free (blob);
}


int
cci_clob_free (T_CCI_CLOB clob)
{
  return cci_lob_free (clob);
}


#ifdef CCI_XA
int
cci_xa_prepare (int mapped_conn_id, XID * xid, T_CCI_ERROR * err_buf)
{
  int error = 0;
  T_CON_HANDLE *con_handle = NULL;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("(%d)cci_xa_prepare", mapped_conn_id));
#endif

  reset_error_buffer (err_buf);
  error = hm_get_connection (mapped_conn_id, &con_handle);
  if (error != CCI_ER_NO_ERROR)
    {
      set_error_buffer (err_buf, error, NULL);
      return error;
    }
  reset_error_buffer (&(con_handle->err_buf));

  error = qe_xa_prepare (con_handle, xid, &(con_handle->err_buf));
  while (IS_OUT_TRAN (con_handle)
	 && IS_ER_TO_RECONNECT (error, con_handle->err_buf.err_code))
    {
      if (NEED_TO_RECONNECT (con_handle, error))
	{
	  /* Finally, reset_connect will return ER_TIMEOUT */
	  error = reset_connect (con_handle, NULL, &(con_handle->err_buf));
	  if (error != CCI_ER_NO_ERROR)
	    {
	      break;
	    }
	}

      error = qe_xa_prepare (con_handle, xid, &(con_handle->err_buf));
    }

  set_error_buffer (&(con_handle->err_buf), error, NULL);
  get_last_error (con_handle, err_buf);

  return error;
}

int
cci_xa_recover (int mapped_conn_id, XID * xid, int num_xid,
		T_CCI_ERROR * err_buf)
{
  int error = 0;
  T_CON_HANDLE *con_handle = NULL;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("(%d)cci_xa_recover", mapped_conn_id));
#endif

  reset_error_buffer (err_buf);
  error = hm_get_connection (mapped_conn_id, &con_handle);
  if (error != CCI_ER_NO_ERROR)
    {
      set_error_buffer (err_buf, error, NULL);
      return error;
    }
  reset_error_buffer (&(con_handle->err_buf));

  error = qe_xa_recover (con_handle, xid, num_xid, &(con_handle->err_buf));
  while (IS_OUT_TRAN (con_handle)
	 && IS_ER_TO_RECONNECT (error, con_handle->err_buf.err_code))
    {
      if (NEED_TO_RECONNECT (con_handle, error))
	{
	  /* Finally, reset_connect will return ER_TIMEOUT */
	  error = reset_connect (con_handle, NULL, &(con_handle->err_buf));
	  if (error != CCI_ER_NO_ERROR)
	    {
	      break;
	    }
	}

      error = qe_xa_recover (con_handle, xid, num_xid,
			     &(con_handle->err_buf));
    }

  set_error_buffer (&(con_handle->err_buf), error, NULL);
  get_last_error (con_handle, err_buf);

  return error;
}

int
cci_xa_end_tran (int mapped_conn_id, XID * xid, char type,
		 T_CCI_ERROR * err_buf)
{
  int error = 0;
  T_CON_HANDLE *con_handle = NULL;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("(%d)cci_xa_end_tran", mapped_conn_id));
#endif

  reset_error_buffer (err_buf);
  error = hm_get_connection (mapped_conn_id, &con_handle);
  if (error != CCI_ER_NO_ERROR)
    {
      set_error_buffer (err_buf, error, NULL);
      return error;
    }
  reset_error_buffer (&(con_handle->err_buf));

  error = qe_xa_end_tran (con_handle, xid, type, &(con_handle->err_buf));
  while (IS_OUT_TRAN (con_handle)
	 && IS_ER_TO_RECONNECT (error, con_handle->err_buf.err_code))
    {
      if (NEED_TO_RECONNECT (con_handle, error))
	{
	  /* Finally, reset_connect will return ER_TIMEOUT */
	  error = reset_connect (con_handle, NULL, &(con_handle->err_buf));
	  if (error != CCI_ER_NO_ERROR)
	    {
	      break;
	    }
	}

      error = qe_xa_end_tran (con_handle, xid, type, &(con_handle->err_buf));
    }

  set_error_buffer (&(con_handle->err_buf), error, NULL);
  get_last_error (con_handle, err_buf);

  return error;
}
#endif


int
cci_get_dbms_type (int mapped_conn_id)
{
  T_CON_HANDLE *con_handle = NULL;
  int error;

  error = hm_get_connection (mapped_conn_id, &con_handle);
  if (error != CCI_ER_NO_ERROR)
    {
      return error;
    }
  reset_error_buffer (&(con_handle->err_buf));
  con_handle->used = false;

  return con_handle->broker_info[BROKER_INFO_DBMS_TYPE];
}

int
cci_set_charset (int mapped_conn_id, char *charset)
{
#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("(%d)cci_set_charset: %s", mapped_conn_id, charset));
#endif

#if defined(WINDOWS)
  T_CON_HANDLE *con_handle = NULL;
  int error;

  if (charset == NULL)
    {
      return CCI_ER_INVALID_ARGS;
    }

  error = hm_get_connection (mapped_conn_id, &con_handle);
  if (error != CCI_ER_NO_ERROR)
    {
      return error;
    }
  reset_error_buffer (&(con_handle->err_buf));

  error = qe_set_charset (con_handle, charset);
  con_handle->used = false;

  return error;
#else
  return CCI_ER_NO_ERROR;
#endif
}

int
cci_row_count (int mapped_conn_id, int *row_count, T_CCI_ERROR * err_buf)
{
  int error = CCI_ER_NO_ERROR;
  int req_handle_id = -1;
  T_CON_HANDLE *con_handle = NULL;
  T_REQ_HANDLE *req_handle = NULL;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("(%d)cci_row_count", mapped_conn_id));
#endif

  reset_error_buffer (err_buf);
  error = hm_get_connection (mapped_conn_id, &con_handle);
  if (error != CCI_ER_NO_ERROR)
    {
      set_error_buffer (err_buf, error, NULL);
      return error;
    }
  reset_error_buffer (&(con_handle->err_buf));

  req_handle_id = hm_req_handle_alloc (con_handle, &req_handle);
  if (req_handle_id < 0)
    {
      error = req_handle_id;
      goto ret;
    }

  error = qe_get_row_count (req_handle, con_handle, row_count,
			    &(con_handle->err_buf));
  while (IS_OUT_TRAN (con_handle)
	 && IS_ER_TO_RECONNECT (error, con_handle->err_buf.err_code))
    {
      if (NEED_TO_RECONNECT (con_handle, error))
	{
	  /* Finally, reset_connect will return ER_TIMEOUT */
	  error = reset_connect (con_handle, NULL, &(con_handle->err_buf));
	  if (error != CCI_ER_NO_ERROR)
	    {
	      break;
	    }
	}

      error = qe_get_row_count (req_handle, con_handle, row_count,
				&(con_handle->err_buf));
    }
  hm_req_handle_free (con_handle, req_handle);
  req_handle = NULL;

ret:
  set_error_buffer (&(con_handle->err_buf), error, NULL);
  get_last_error (con_handle, err_buf);
  con_handle->used = false;

  return error;
}

int
cci_get_shard_id_with_con_handle (int mapped_conn_id, int *shard_id,
				  T_CCI_ERROR * err_buf)
{
  T_CON_HANDLE *con_handle = NULL;
  int error = CCI_ER_NO_ERROR;

  reset_error_buffer (err_buf);
  error = hm_get_connection (mapped_conn_id, &con_handle);
  if (error != CCI_ER_NO_ERROR)
    {
      return error;
    }

  if (shard_id)
    {
      *shard_id = con_handle->shard_id;
    }
  con_handle->used = false;

  return CCI_ER_NO_ERROR;
}

int
cci_get_shard_id_with_req_handle (int mapped_stmt_id, int *shard_id,
				  T_CCI_ERROR * err_buf)
{
  T_CON_HANDLE *con_handle = NULL;
  T_REQ_HANDLE *req_handle = NULL;
  int error = CCI_ER_NO_ERROR;

  reset_error_buffer (err_buf);
  error = hm_get_statement (mapped_stmt_id, &con_handle, &req_handle);
  if (error != CCI_ER_NO_ERROR)
    {
      return error;
    }

  if (shard_id)
    {
      *shard_id = req_handle->shard_id;
    }
  con_handle->used = false;

  return CCI_ER_NO_ERROR;
}

/*
 * IMPORTANT: cci_last_insert_id and cci_get_last_insert_id
 *
 *   cci_get_last_insert_id set value as last insert id in con_handle
 *   so it could be changed when new insertion is executed.
 *
 *   cci_last_insert_id set value as allocated last insert id
 *   so it won't be changed but user should free it manually.
 *
 *   But, It's possible to make some problem when it working with
 *   user own memory allocators or Windows shared library memory space.
 *
 *   So we deprecate cci_last_insert_id and strongly recommend to use
 *   cci_get_last_insert_id.
 */
int
cci_last_insert_id (int mapped_conn_id, void *value, T_CCI_ERROR * err_buf)
{
  int error = CCI_ER_NO_ERROR;
  char *ptr, *val;

  /* value init & null check are in below function */
  error = cci_get_last_insert_id (mapped_conn_id, &ptr, err_buf);

  if (error == CCI_ER_NO_ERROR && ptr != NULL && value != NULL)
    {
      /* 2 for sign & null termination */
      int value_len = strnlen (ptr, MAX_NUMERIC_PRECISION + 2);
      assert (value_len < MAX_NUMERIC_PRECISION + 2);

      val = MALLOC (value_len + 1);
      if (val == NULL)
	{
	  error = CCI_ER_NO_MORE_MEMORY;
	  set_error_buffer (err_buf, error, NULL);
	  return error;
	}

      strncpy (val, ptr, value_len);
      val[value_len] = '\0';

      *(char **) value = val;
    }

  return error;
}

int
cci_get_last_insert_id (int mapped_conn_id, void *value,
			T_CCI_ERROR * err_buf)
{
  int error = CCI_ER_NO_ERROR;
  int req_handle_id = -1;
  T_CON_HANDLE *con_handle = NULL;
  T_REQ_HANDLE *req_handle = NULL;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("(%d)cci_get_last_insert_id", mapped_conn_id));
#endif

  reset_error_buffer (err_buf);

  if (value == NULL)
    {
      error = CCI_ER_INVALID_ARGS;
      set_error_buffer (err_buf, error, NULL);
      return error;
    }

  *(char **) value = NULL;

  error = hm_get_connection (mapped_conn_id, &con_handle);
  if (error != CCI_ER_NO_ERROR)
    {
      set_error_buffer (err_buf, error, NULL);
      return error;
    }

  reset_error_buffer (&(con_handle->err_buf));

  req_handle_id = hm_req_handle_alloc (con_handle, &req_handle);
  if (req_handle_id < 0)
    {
      error = req_handle_id;
      goto ret;
    }
  error = qe_get_last_insert_id (req_handle, con_handle, value,
				 &(con_handle->err_buf));
  while (IS_OUT_TRAN (con_handle)
	 && IS_ER_TO_RECONNECT (error, con_handle->err_buf.err_code))
    {
      if (NEED_TO_RECONNECT (con_handle, error))
	{
	  /* Finally, reset_connect will return ER_TIMEOUT */
	  error = reset_connect (con_handle, NULL, &(con_handle->err_buf));
	  if (error != CCI_ER_NO_ERROR)
	    {
	      break;
	    }
	}

      error = qe_get_last_insert_id (req_handle, con_handle, value,
				     &(con_handle->err_buf));
    }
  hm_req_handle_free (con_handle, req_handle);
  req_handle = NULL;

ret:
  set_error_buffer (&(con_handle->err_buf), error, NULL);
  get_last_error (con_handle, err_buf);
  con_handle->used = false;

  return error;
}

static const char *
cci_get_err_msg_internal (int error)
{
  switch (error)
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

    case CCI_ER_INVALID_URL:
      return "Invalid url string";

    case CCI_ER_INVALID_LOB_READ_POS:
      return "Invalid lob read position";

    case CCI_ER_INVALID_LOB_HANDLE:
      return "Invalid lob handle";

    case CCI_ER_NO_PROPERTY:
      return "Cannot find a property";

      /* CCI_ER_INVALID_PROPERTY_VALUE equals to CCI_ER_PROPERTY_TYPE */
    case CCI_ER_INVALID_PROPERTY_VALUE:
      return "Invalid property value";

    case CCI_ER_INVALID_DATASOURCE:
      return "Invalid CCI datasource";

    case CCI_ER_DATASOURCE_TIMEOUT:
      return "All connections are used";

    case CCI_ER_DATASOURCE_TIMEDWAIT:
      return "pthread_cond_timedwait error";

    case CCI_ER_LOGIN_TIMEOUT:
      return "Connection timed out";

    case CCI_ER_QUERY_TIMEOUT:
      return "Request timed out";

    case CCI_ER_RESULT_SET_CLOSED:
      return "Result set is closed";

    case CCI_ER_INVALID_HOLDABILITY:
      return "Invalid holdability mode. The only accepted values are 0 or 1";

    case CCI_ER_NOT_UPDATABLE:
      return "Request handle is not updatable";

    case CCI_ER_INVALID_ARGS:
      return "Invalid argument";

    case CCI_ER_USED_CONNECTION:
      return "This connection is used already.";

    case CCI_ER_NO_SHARD_AVAILABLE:
      return "No shard available";

    case CCI_ER_INVALID_SHARD:
      return "Invalid shard";

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

    case CAS_ER_MAX_PREPARED_STMT_COUNT_EXCEEDED:
      return "Cannot prepare more than MAX_PREPARED_STMT_COUNT statements";

    case CAS_ER_HOLDABLE_NOT_ALLOWED:
      return "Holdable results may not be updatable or sensitive";

    case CAS_ER_MAX_CLIENT_EXCEEDED:
      return "Proxy refused client connection. max clients exceeded";

    case CAS_ER_INVALID_CURSOR_POS:
      return "Invalid cursor position";

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
cci_get_error_msg (int error, T_CCI_ERROR * cci_err, char *buf, int bufsize)
{
  const char *err_msg;

  if ((buf == NULL) || (bufsize <= 0))
    {
      return -1;
    }

  err_msg = cci_get_err_msg_internal (error);
  if (err_msg == NULL)
    {
      return -1;
    }
  else
    {
      if ((error < CCI_ER_DBMS) && (error > CCI_ER_END))
	{
	  snprintf (buf, bufsize, "CCI Error : %s", err_msg);
	}
      else if ((error < CAS_ER_DBMS) && (error >= CAS_ER_IS))
	{
	  snprintf (buf, bufsize, "CUBRID CAS Error : %s", err_msg);
	}
      if ((error == CCI_ER_DBMS) || (error == CAS_ER_DBMS))
	{
	  if (cci_err == NULL)
	    {
	      snprintf (buf, bufsize, "%s ", err_msg);
	    }
	  else
	    {
	      snprintf (buf, bufsize, "%s : (%d) %s", err_msg,
			cci_err->err_code, cci_err->err_msg);
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
cci_get_err_msg (int error, char *buf, int bufsize)
{
  const char *err_msg;

  if ((buf == NULL) || (bufsize <= 0))
    {
      return -1;
    }

  err_msg = cci_get_err_msg_internal (error);
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
reset_error_buffer (T_CCI_ERROR * err_buf)
{
  if (err_buf != NULL)
    {
      err_buf->err_code = CCI_ER_NO_ERROR;
      err_buf->err_msg[0] = '\0';
    }
}

static int
col_set_add_drop (int resolved_id, char col_cmd, char *oid_str,
		  char *col_attr, char *value, T_CCI_ERROR * err_buf)
{
  int error = CCI_ER_NO_ERROR;
  T_CON_HANDLE *con_handle = NULL;

  reset_error_buffer (err_buf);
  error = hm_get_connection (resolved_id, &con_handle);
  if (error != CCI_ER_NO_ERROR)
    {
      set_error_buffer (err_buf, error, NULL);
      return error;
    }
  reset_error_buffer (&(con_handle->err_buf));

  error = qe_col_set_add_drop (con_handle, col_cmd, oid_str, col_attr,
			       value, &(con_handle->err_buf));
  while (IS_OUT_TRAN (con_handle)
	 && IS_ER_TO_RECONNECT (error, con_handle->err_buf.err_code))
    {
      if (NEED_TO_RECONNECT (con_handle, error))
	{
	  /* Finally, reset_connect will return ER_TIMEOUT */
	  error = reset_connect (con_handle, NULL, &(con_handle->err_buf));
	  if (error != CCI_ER_NO_ERROR)
	    {
	      break;
	    }
	}

      error = qe_col_set_add_drop (con_handle, col_cmd, oid_str, col_attr,
				   value, &(con_handle->err_buf));
    }

  set_error_buffer (&(con_handle->err_buf), error, NULL);
  get_last_error (con_handle, err_buf);
  con_handle->used = false;

  return error;
}

static int
col_seq_op (int resolved_id, char col_cmd, char *oid_str, char *col_attr,
	    int index, const char *value, T_CCI_ERROR * err_buf)
{
  int error = CCI_ER_NO_ERROR;
  T_CON_HANDLE *con_handle = NULL;

  reset_error_buffer (err_buf);
  error = hm_get_connection (resolved_id, &con_handle);
  if (error != CCI_ER_NO_ERROR)
    {
      set_error_buffer (err_buf, error, NULL);
      return error;
    }
  reset_error_buffer (&(con_handle->err_buf));

  error = qe_col_seq_op (con_handle, col_cmd, oid_str, col_attr, index,
			 value, &(con_handle->err_buf));
  while (IS_OUT_TRAN (con_handle)
	 && IS_ER_TO_RECONNECT (error, con_handle->err_buf.err_code))
    {
      if (NEED_TO_RECONNECT (con_handle, error))
	{
	  /* Finally, reset_connect will return ER_TIMEOUT */
	  error = reset_connect (con_handle, NULL, &(con_handle->err_buf));
	  if (error != CCI_ER_NO_ERROR)
	    {
	      break;
	    }
	}

      error = qe_col_seq_op (con_handle, col_cmd, oid_str, col_attr, index,
			     value, &(con_handle->err_buf));
    }

  set_error_buffer (&(con_handle->err_buf), error, NULL);
  get_last_error (con_handle, err_buf);
  con_handle->used = false;

  return error;
}

static int
fetch_cmd (int mapped_stmt_id, char flag, T_CCI_ERROR * err_buf)
{
  T_REQ_HANDLE *req_handle = NULL;
  T_CON_HANDLE *con_handle = NULL;
  int error = CCI_ER_NO_ERROR;
  int result_set_index = 0;

  reset_error_buffer (err_buf);
  error = hm_get_statement (mapped_stmt_id, &con_handle, &req_handle);
  if (error != CCI_ER_NO_ERROR)
    {
      set_error_buffer (err_buf, error, NULL);
      return error;
    }
  reset_error_buffer (&(con_handle->err_buf));

  if ((req_handle->prepare_flag & CCI_PREPARE_HOLDABLE) != 0
      && (flag & CCI_FETCH_SENSITIVE) != 0)
    {
      error = CAS_ER_HOLDABLE_NOT_ALLOWED;
    }

  if (error >= 0)
    {
      error = qe_fetch (req_handle, con_handle, flag, result_set_index,
			&(con_handle->err_buf));
    }

  if (IS_OUT_TRAN (con_handle))
    {
      hm_check_rc_time (con_handle);
    }

  set_error_buffer (&(con_handle->err_buf), error, NULL);
  get_last_error (con_handle, err_buf);
  con_handle->used = false;

  return error;
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
  int error;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("(%d)cas_connect_with_ret",
				    con_handle->id));
#endif
  error = cas_connect_internal (con_handle, err_buf, connect);

  /* req_handle_table should be managed by list too. */
  if (((*connect) != 0) && IS_BROKER_STMT_POOL (con_handle))
    {
      hm_invalidate_all_req_handle (con_handle);
    }
  if ((*connect) != 0)
    {
      con_handle->no_backslash_escapes = CCI_NO_BACKSLASH_ESCAPES_NOT_SET;
    }

  return error;
}

static int
cas_connect_internal (T_CON_HANDLE * con_handle, T_CCI_ERROR * err_buf,
		      int *connect)
{
  int error = CCI_ER_NO_ERROR;
  int i;
  int remained_time = 0;
  int retry = 0;

  assert (connect != NULL);

  *connect = 0;

  if (TIMEOUT_IS_SET (con_handle))
    {
      remained_time = (con_handle->current_timeout
		       - get_elapsed_time (&con_handle->start_time));
      if (remained_time <= 0)
	{
	  return CCI_ER_LOGIN_TIMEOUT;
	}
    }

  if (net_check_cas_request (con_handle) != 0)
    {
      if (!IS_INVALID_SOCKET (con_handle->sock_fd))
	{
	  CLOSE_SOCKET (con_handle->sock_fd);
	  con_handle->sock_fd = INVALID_SOCKET;
	  con_handle->con_status = CCI_CON_STATUS_OUT_TRAN;
	}
    }

  if (!IS_INVALID_SOCKET (con_handle->sock_fd))
    {
      return CCI_ER_NO_ERROR;
    }
  CLOSE_SOCKET (con_handle->sock_fd);
  con_handle->sock_fd = INVALID_SOCKET;
  con_handle->con_status = CCI_CON_STATUS_OUT_TRAN;

  if (con_handle->alter_host_count == 0)
    {
      error = net_connect_srv (con_handle, con_handle->alter_host_id, err_buf,
			       remained_time);
      if (error == CCI_ER_NO_ERROR)
	{
	  *connect = 1;
	  return CCI_ER_NO_ERROR;
	}
    }

  do
    {
      for (i = 0; i < con_handle->alter_host_count; i++)
	{
	  /* if all hosts turn out to be unreachable,
	   *  ignore host reachability and try one more time
	   */
	  if (hm_is_host_reachable (con_handle, i) || retry)
	    {
	      error = net_connect_srv (con_handle, i, err_buf, remained_time);
	      if (error == CCI_ER_NO_ERROR)
		{
		  hm_set_host_status (con_handle, i, REACHABLE);

		  *connect = 1;
		  return CCI_ER_NO_ERROR;
		}

	      if (error == CCI_ER_COMMUNICATION
		  || error == CCI_ER_CONNECT
		  || error == CCI_ER_LOGIN_TIMEOUT
		  || error == CAS_ER_FREE_SERVER)
		{
		  hm_set_host_status (con_handle, i, UNREACHABLE);
		}
	      else
		{
		  break;
		}
	    }
	  con_handle->last_failure_time = time (NULL);
	}
      retry++;
    }
  while (retry < 2);

  if (error == CCI_ER_QUERY_TIMEOUT)
    {
      error = CCI_ER_LOGIN_TIMEOUT;
    }

  return error;
}

static int
next_result_cmd (T_REQ_HANDLE * req_handle, T_CON_HANDLE * con_handle,
		 char flag, T_CCI_ERROR * err_buf)
{
  return qe_next_result (req_handle, flag, con_handle, err_buf);
}

#ifdef CCI_DEBUG
static void
print_debug_msg (const char *format, ...)
{
  va_list args;

  char format_buf[128];
  char time_buf[128];

  cci_time_string (time_buf, NULL);

  snprintf (sizeof (format_buf), format_buf, "%s %s\n", time_buf, format);

  va_start (args, format);

  if (cci_debug_fp == NULL)
    {
      cci_debug_fp = fopen (cci_debug_filename, "a");
    }

  if (cci_debug_fp != NULL)
    {
      vfprintf (cci_debug_fp, format_buf, args);
      fflush (cci_debug_fp);
    }
  va_end (args);
}

static const char *
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

static const char *
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
    case CCI_A_TYPE_BLOB:
      return "CCI_A_TYPE_BLOB";
    case CCI_A_TYPE_CLOB:
      return "CCI_A_TYPE_CLOB";
    case CCI_A_TYPE_REQ_HANDLE:
      return "CCI_A_TYPE_REQ_HANDLE";
    case CCI_A_TYPE_UINT:
      return "CCI_A_TYPE_UINT";
    case CCI_A_TYPE_UBIGINT:
      return "CCI_A_TYPE_UBIGINT";
    default:
      return "***";
    }
}

static const char *
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
    case CCI_U_TYPE_ENUM:
      return "CCI_U_TYPE_ENUM";
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
    case CCI_U_TYPE_TIMETZ:
      return "CCI_U_TYPE_TIMETZ";
    case CCI_U_TYPE_TIMESTAMP:
      return "CCI_U_TYPE_TIMESTAMP";
    case CCI_U_TYPE_TIMESTAMPTZ:
      return "CCI_U_TYPE_TIMESTAMPTZ";
    case CCI_U_TYPE_DATETIME:
      return "CCI_U_TYPE_DATETIME";
    case CCI_U_TYPE_DATETIMETZ:
      return "CCI_U_TYPE_DATETIMETZ";
    case CCI_U_TYPE_SET:
      return "CCI_U_TYPE_SET";
    case CCI_U_TYPE_MULTISET:
      return "CCI_U_TYPE_MULTISET";
    case CCI_U_TYPE_SEQUENCE:
      return "CCI_U_TYPE_SEQUENCE";
    case CCI_U_TYPE_OBJECT:
      return "CCI_U_TYPE_OBJECT";
    case CCI_U_TYPE_RESULTSET:
      return "CCI_U_TYPE_RESULTSET";
    case CCI_U_TYPE_BLOB:
      return "CCI_U_TYPE_BLOB";
    case CCI_U_TYPE_CLOB:
      return "CCI_U_TYPE_CLOB";
    case CCI_U_TYPE_USHORT:
      return "CCI_U_TYPE_USHORT";
    case CCI_U_TYPE_UINT:
      return "CCI_U_TYPE_UINT";
    case CCI_U_TYPE_UBIGINT:
      return "CCI_U_TYPE_UBIGINT";
    default:
      return "***";
    }
}

static const char *
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

static const char *
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

static const char *
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
    case CCI_SCH_IMPORTED_KEYS:
      return "CCI_SCH_IMPORTED_KEYS";
    case CCI_SCH_EXPORTED_KEYS:
      return "CCI_SCH_EXPORTED_KEYS";
    case CCI_SCH_CROSS_REFERENCE:
      return "CCI_SCH_CROSS_REFERENCE";
    default:
      return "***";
    }
}

static const char *
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

static const char *
dbg_isolation_str (T_CCI_TRAN_ISOLATION isol_level)
{
  switch (isol_level)
    {
    case TRAN_READ_COMMITTED:
      return "TRAN_READ_COMMITTED";
    case TRAN_REPEATABLE_READ:
      return "TRAN_REPEATABLE_READ";
    case TRAN_SERIALIZABLE:
      return "TRAN_SERIALIZABLE";
    default:
      return "***";
    }
}
#endif

static T_CON_HANDLE *
get_new_connection (char *ip, int port, char *db_name,
		    char *db_user, char *dbpasswd)
{
  T_CON_HANDLE *con_handle;
  unsigned char ip_addr[4];

  if (hm_ip_str_to_addr (ip, ip_addr) < 0)
    {
      return NULL;
    }

  MUTEX_LOCK (con_handle_table_mutex);

  if (init_flag == 0)
    {
      hm_con_handle_table_init ();
      init_flag = 1;
    }

  con_handle = hm_get_con_from_pool (ip_addr, port, db_name, db_user,
				     dbpasswd);
  if (con_handle == NULL)
    {
      con_handle = hm_con_handle_alloc (ip, port, db_name, db_user, dbpasswd);
    }

  MUTEX_UNLOCK (con_handle_table_mutex);

#if !defined(WINDOWS)
  if (!cci_SIGPIPE_ignore)
    {
      signal (SIGPIPE, SIG_IGN);
      cci_SIGPIPE_ignore = 1;
    }
#endif

  return con_handle;
}

T_CCI_PROPERTIES *
cci_property_create ()
{
  T_CCI_PROPERTIES *prop;

  prop = MALLOC (sizeof (T_CCI_PROPERTIES));
  if (prop == NULL)
    {
      return NULL;
    }

  prop->capacity = 10;
  prop->size = 0;
  prop->pair = MALLOC (prop->capacity * sizeof (T_CCI_PROPERTIES_PAIR));
  if (prop->pair == NULL)
    {
      FREE_MEM (prop);
      return NULL;
    }

  return prop;
}

void
cci_property_destroy (T_CCI_PROPERTIES * properties)
{
  int i;

  if (properties == NULL)
    {
      return;
    }

  for (i = 0; i < properties->size; i++)
    {
      FREE_MEM (properties->pair[i].key);
      FREE_MEM (properties->pair[i].value);
    }

  if (properties->pair)
    {
      FREE_MEM (properties->pair);
    }
  FREE_MEM (properties);
}

int
cci_property_set (T_CCI_PROPERTIES * properties, char *key, char *value)
{
  char *pkey, *pvalue;

  if (properties == NULL || key == NULL || value == NULL)
    {
      return 0;
    }

  if (strlen (key) >= LINE_MAX || strlen (value) >= LINE_MAX)
    {
      /* max size of key and value buffer is LINE_MAX */
      return 0;
    }

  if (properties->capacity == properties->size)
    {
      T_CCI_PROPERTIES_PAIR *tmp;
      int new_capa = properties->capacity + 10;
      tmp = REALLOC (properties->pair,
		     sizeof (T_CCI_PROPERTIES_PAIR) * new_capa);
      if (tmp == NULL)
	{
	  return 0;
	}
      properties->capacity = new_capa;
      properties->pair = tmp;
    }

  pkey = strdup (key);
  if (pkey == NULL)
    {
      return 0;
    }
  pvalue = strdup (value);
  if (pvalue == NULL)
    {
      FREE_MEM (pkey);
      return 0;
    }

  properties->pair[properties->size].key = pkey;
  properties->pair[properties->size].value = pvalue;
  properties->size++;

  return 1;
}

char *
cci_property_get (T_CCI_PROPERTIES * properties, const char *key)
{
  int i;

  if (properties == NULL || key == NULL)
    {
      return NULL;
    }

  for (i = 0; i < properties->size; i++)
    {
      if (strcasecmp (properties->pair[i].key, key) == 0)
	{
	  return properties->pair[i].value;
	}
    }

  return NULL;
}

static int
cci_strtol (long *val, char *str, int base)
{
  long v;
  char *end;

  assert (val != NULL);

  errno = 0;			/* To distinguish success/failure after call */
  v = strtol (str, &end, base);
  if ((errno == ERANGE && (v == LONG_MAX || v == LONG_MIN))
      || (errno != 0 && v == 0))
    {
      /* perror ("strtol"); */
      return false;
    }

  if (end == str)
    {
      /* No digits were found. */
      return false;
    }

  *val = v;
  return true;
}

static bool
cci_property_get_int (T_CCI_PROPERTIES * prop, T_CCI_DATASOURCE_KEY key,
		      int *out_value, int default_value, int min, int max,
		      T_CCI_ERROR * err_buf)
{
  char *tmp;
  long val;

  assert (out_value != NULL);
  assert (err_buf != NULL);

  tmp = cci_property_get (prop, datasource_key[key]);
  if (tmp == NULL)
    {
      set_error_buffer (err_buf, CCI_ER_NO_PROPERTY,
			"Could not found integer property");
      *out_value = default_value;
    }
  else
    {
      if (cci_strtol (&val, tmp, 10))
	{
	  *out_value = (int) val;
	  reset_error_buffer (err_buf);
	}
      else
	{
	  set_error_buffer (err_buf, CCI_ER_INVALID_PROPERTY_VALUE,
			    "strtol: %s", strerror (errno));
	  return false;
	}
    }

  if (*out_value < min || *out_value > max)
    {
      reset_error_buffer (err_buf);
      set_error_buffer (err_buf, CCI_ER_INVALID_PROPERTY_VALUE,
			"The %d is out of range (%s, %d to %d).",
			*out_value, datasource_key[key], min, max);
      return false;
    }

  return true;
}

static bool
cci_property_get_bool_internal (T_CCI_PROPERTIES * prop,
				T_CCI_DATASOURCE_KEY key, int *out_value,
				int default_value, T_CCI_ERROR * err_buf)
{
  char *tmp;


  assert (out_value != NULL);
  assert (err_buf != NULL);

  tmp = cci_property_get (prop, datasource_key[key]);
  if (tmp == NULL)
    {
      set_error_buffer (err_buf, CCI_ER_NO_PROPERTY,
			"Could not found boolean property");
      *out_value = default_value;
    }
  else
    {
      if (strcasecmp (tmp, "true") == 0)
	{
	  *out_value = (bool) true;
	}
      else if (strcasecmp (tmp, "yes") == 0)
	{
	  *out_value = (bool) true;
	}
      else if (strcasecmp (tmp, "false") == 0)
	{
	  *out_value = (bool) false;
	}
      else if (strcasecmp (tmp, "no") == 0)
	{
	  *out_value = (bool) false;
	}
      else
	{
	  set_error_buffer (err_buf, CCI_ER_INVALID_PROPERTY_VALUE,
			    "boolean parsing : %s", tmp);
	  return false;
	}
      reset_error_buffer (err_buf);
    }

  return true;
}

static bool
cci_property_get_bool (T_CCI_PROPERTIES * prop, T_CCI_DATASOURCE_KEY key,
		       bool * out_value, bool default_value,
		       T_CCI_ERROR * err_buf)
{
  int retval, tmp_out;

  assert (out_value != NULL);

  retval = cci_property_get_bool_internal (prop, key, &tmp_out, default_value,
					   err_buf);
  if (!retval)
    {
      return false;
    }

  *out_value = (tmp_out ? true : false);
  return true;
}

static T_CCI_TRAN_ISOLATION
cci_property_conv_isolation (const char *isolation)
{
  if (strcasecmp (isolation, "TRAN_READ_COMMITTED") == 0
      || strcasecmp (isolation, "TRAN_REP_CLASS_COMMIT_INSTANCE") == 0)
    {
      return TRAN_READ_COMMITTED;
    }
  else if (strcasecmp (isolation, "TRAN_REPEATABLE_READ") == 0
	   || strcasecmp (isolation, "TRAN_REP_CLASS_REP_INSTANCE") == 0)
    {
      return TRAN_REPEATABLE_READ;
    }
  else if (strcasecmp (isolation, "TRAN_SERIALIZABLE") == 0)
    {
      return TRAN_SERIALIZABLE;
    }
  else
    {
      return TRAN_UNKNOWN_ISOLATION;
    }
}

static bool
cci_property_get_isolation (T_CCI_PROPERTIES * prop,
			    T_CCI_DATASOURCE_KEY key,
			    T_CCI_TRAN_ISOLATION * out_value,
			    int default_value, T_CCI_ERROR * err_buf)
{
  char *tmp;

  assert (out_value != NULL);
  assert (err_buf != NULL);

  tmp = cci_property_get (prop, datasource_key[key]);
  if (tmp == NULL)
    {
      set_error_buffer (err_buf, CCI_ER_NO_PROPERTY,
			"Could not found isolation property");
      *out_value = default_value;
    }
  else
    {
      T_CCI_TRAN_ISOLATION i = cci_property_conv_isolation (tmp);
      if (i == TRAN_UNKNOWN_ISOLATION)
	{
	  set_error_buffer (err_buf, CCI_ER_INVALID_PROPERTY_VALUE,
			    "isolation parsing : %s", tmp);
	  return false;
	}
      else
	{
	  *out_value = (T_CCI_TRAN_ISOLATION) i;
	}
      reset_error_buffer (err_buf);
    }

  return true;
}

static bool
cci_check_property (char **property, T_CCI_ERROR * err_buf)
{
  if (*property)
    {
      *property = strdup (*property);
      if (*property == NULL)
	{
	  set_error_buffer (err_buf, CCI_ER_NO_MORE_MEMORY,
			    "strdup: %s", strerror (errno));
	  return false;
	}
    }
  else
    {
      set_error_buffer (err_buf, CCI_ER_NO_PROPERTY,
			"Could not found user property for connection");
      return false;
    }

  return true;
}

static void
cci_disconnect_force (int resolved_id, bool try_close)
{
  T_CON_HANDLE *con_handle = NULL;
  int error;

  assert (resolved_id > 0);

  error = hm_get_connection_by_resolved_id (resolved_id, &con_handle);
  if (error != CCI_ER_NO_ERROR)
    {
      return;
    }
  reset_error_buffer (&(con_handle->err_buf));
  con_handle->shard_id = CCI_SHARD_ID_INVALID;

  API_SLOG (con_handle);
  if (try_close)
    {
      qe_con_close (con_handle);
    }

  API_ELOG (con_handle, 0);
  hm_con_handle_free (con_handle);
}

static bool
cci_datasource_make_url (T_CCI_PROPERTIES * prop, char *new_url, char *url,
			 T_CCI_ERROR * err_buf)
{
  char delim;
  const char *str;
  char append_str[LINE_MAX];
  int login_timeout = -1, query_timeout = -1;
  bool disconnect_on_query_timeout;
  int rlen, n;

  assert (new_url && url);
  assert (err_buf != NULL);

  if (!new_url || !url)
    {
      return false;
    }

  strncpy (new_url, url, LINE_MAX);
  if (strlen (url) >= LINE_MAX)
    {
      new_url[LINE_MAX] = '\0';

      set_error_buffer (err_buf, CCI_ER_NO_MORE_MEMORY, NULL);

      return false;
    }
  rlen = LINE_MAX - strlen (new_url);

  if (strchr (new_url, '?'))
    {
      delim = '&';
    }
  else
    {
      delim = '?';
    }

  if (!cci_property_get_int (prop, CCI_DS_KEY_LOGIN_TIMEOUT, &login_timeout,
			     login_timeout, -1, INT_MAX, err_buf))
    {
      return false;
    }
  else if (err_buf->err_code != CCI_ER_NO_PROPERTY)
    {
      str = datasource_key[CCI_DS_KEY_LOGIN_TIMEOUT];

      n = snprintf (append_str, rlen, "%c%s=%d", delim, str, login_timeout);
      rlen -= n;
      if (rlen <= 0 || n < 0)
	{
	  set_error_buffer (err_buf, CCI_ER_NO_MORE_MEMORY, NULL);
	  return false;
	}
      strncat (new_url, append_str, rlen);
      delim = '&';

      reset_error_buffer (err_buf);
    }

  if (!cci_property_get_int (prop, CCI_DS_KEY_QUERY_TIMEOUT, &query_timeout,
			     query_timeout, -1, INT_MAX, err_buf))
    {
      return false;
    }
  else if (err_buf->err_code != CCI_ER_NO_PROPERTY)
    {
      str = datasource_key[CCI_DS_KEY_QUERY_TIMEOUT];

      n = snprintf (append_str, rlen, "%c%s=%d", delim, str, query_timeout);
      rlen -= n;
      if (rlen <= 0 || n < 0)
	{
	  set_error_buffer (err_buf, CCI_ER_NO_MORE_MEMORY, NULL);
	  return false;
	}
      strncat (new_url, append_str, rlen);
      delim = '&';

      reset_error_buffer (err_buf);
    }

  if (!cci_property_get_bool (prop, CCI_DS_KEY_DISCONNECT_ON_QUERY_TIMEOUT,
			      &disconnect_on_query_timeout,
			      CCI_DS_DISCONNECT_ON_QUERY_TIMEOUT_DEFAULT,
			      err_buf))
    {
      return false;
    }
  else if (err_buf->err_code != CCI_ER_NO_PROPERTY)
    {
      str = datasource_key[CCI_DS_KEY_DISCONNECT_ON_QUERY_TIMEOUT];

      n = snprintf (append_str, rlen, "%c%s=%s", delim,
		    str, disconnect_on_query_timeout ? "true" : "false");
      rlen -= n;
      if (rlen <= 0 || n < 0)
	{
	  set_error_buffer (err_buf, CCI_ER_NO_MORE_MEMORY, NULL);
	  return false;
	}
      strncat (new_url, append_str, rlen);
      delim = '&';

      reset_error_buffer (err_buf);
    }

  reset_error_buffer (err_buf);

  return true;
}

T_CCI_DATASOURCE *
cci_datasource_create (T_CCI_PROPERTIES * prop, T_CCI_ERROR * err_buf)
{
  T_CCI_DATASOURCE *ds = NULL;
  int i;

  T_CCI_ERROR latest_err_buf;

  reset_error_buffer (err_buf);

  ds = MALLOC (sizeof (T_CCI_DATASOURCE));
  if (ds == NULL)
    {
      set_error_buffer (err_buf, CCI_ER_NO_MORE_MEMORY,
			"memory allocation error: %s", strerror (errno));
      return NULL;
    }
  memset (ds, 0x0, sizeof (T_CCI_DATASOURCE));

  reset_error_buffer (&latest_err_buf);

  ds->user = cci_property_get (prop, datasource_key[CCI_DS_KEY_USER]);
  if (ds->user == NULL)
    {
      /* a user may b e null */
      ds->user = strdup ("");
      if (ds->user == NULL)
	{
	  goto create_datasource_error;
	}
    }
  else
    {
      if (!cci_check_property (&ds->user, &latest_err_buf))
	{
	  goto create_datasource_error;
	}
    }

  ds->pass = cci_property_get (prop, datasource_key[CCI_DS_KEY_PASSWORD]);
  if (ds->pass == NULL)
    {
      /* a pass may be null */
      ds->pass = strdup ("");
      if (ds->pass == NULL)
	{
	  goto create_datasource_error;
	}
    }
  else
    {
      if (!cci_check_property (&ds->pass, &latest_err_buf))
	{
	  goto create_datasource_error;
	}
    }

  ds->url = cci_property_get (prop, datasource_key[CCI_DS_KEY_URL]);
  if (!cci_check_property (&ds->url, &latest_err_buf))
    {
      goto create_datasource_error;
    }

  if (!cci_property_get_int (prop, CCI_DS_KEY_POOL_SIZE, &ds->pool_size,
			     CCI_DS_POOL_SIZE_DEFAULT, 1, INT_MAX,
			     &latest_err_buf))
    {
      goto create_datasource_error;
    }

  if (!cci_property_get_int (prop, CCI_DS_KEY_MAX_POOL_SIZE,
			     &ds->max_pool_size, ds->pool_size,
			     1, INT_MAX, &latest_err_buf))
    {
      goto create_datasource_error;
    }
  if (ds->max_pool_size < ds->pool_size)
    {
      latest_err_buf.err_code = CCI_ER_INVALID_PROPERTY_VALUE;
      if (latest_err_buf.err_msg)
	{
	  snprintf (latest_err_buf.err_msg, 1023,
		    "'max_pool_size' should be greater than 'pool_size'");
	}
      goto create_datasource_error;
    }

  if (!cci_property_get_int (prop, CCI_DS_KEY_MAX_WAIT, &ds->max_wait,
			     CCI_DS_MAX_WAIT_DEFAULT, 1, INT_MAX,
			     &latest_err_buf))
    {
      goto create_datasource_error;
    }

  if (!cci_property_get_bool (prop, CCI_DS_KEY_POOL_PREPARED_STATEMENT,
			      &ds->pool_prepared_statement,
			      CCI_DS_POOL_PREPARED_STATEMENT_DEFAULT,
			      &latest_err_buf))
    {
      goto create_datasource_error;
    }

  if (!cci_property_get_int (prop, CCI_DS_KEY_MAX_OPEN_PREPARED_STATEMENT,
			     &ds->max_open_prepared_statement,
			     CCI_DS_MAX_OPEN_PREPARED_STATEMENT_DEFAULT,
			     1, INT_MAX, &latest_err_buf))
    {
      goto create_datasource_error;
    }

  if (!cci_property_get_bool_internal (prop, CCI_DS_KEY_DEFAULT_AUTOCOMMIT,
				       &ds->default_autocommit,
				       CCI_DS_DEFAULT_AUTOCOMMIT_DEFAULT,
				       &latest_err_buf))
    {
      goto create_datasource_error;
    }

  if (!cci_property_get_isolation (prop, CCI_DS_KEY_DEFAULT_ISOLATION,
				   &ds->default_isolation,
				   CCI_DS_DEFAULT_ISOLATION_DEFAULT,
				   &latest_err_buf))
    {
      goto create_datasource_error;
    }

  if (!cci_property_get_int (prop, CCI_DS_KEY_DEFAULT_LOCK_TIMEOUT,
			     &ds->default_lock_timeout,
			     CCI_DS_DEFAULT_LOCK_TIMEOUT_DEFAULT,
			     CCI_DS_DEFAULT_LOCK_TIMEOUT_DEFAULT, INT_MAX,
			     &latest_err_buf))
    {
      goto create_datasource_error;
    }

  if (!cci_property_get_int (prop, CCI_DS_KEY_LOGIN_TIMEOUT,
			     &ds->login_timeout,
			     CCI_DS_LOGIN_TIMEOUT_DEFAULT,
			     CCI_LOGIN_TIMEOUT_INFINITE, INT_MAX,
			     &latest_err_buf))
    {
      goto create_datasource_error;
    }

  ds->con_handles = CALLOC (ds->max_pool_size, sizeof (T_CCI_CONN));
  if (ds->con_handles == NULL)
    {
      set_error_buffer (&latest_err_buf, CCI_ER_NO_MORE_MEMORY,
			"memory allocation error: %s", strerror (errno));
      goto create_datasource_error;
    }

  ds->mutex = MALLOC (sizeof (pthread_mutex_t));
  if (ds->mutex == NULL)
    {
      set_error_buffer (&latest_err_buf, CCI_ER_NO_MORE_MEMORY,
			"memory allocation error: %s", strerror (errno));
      goto create_datasource_error;
    }
  pthread_mutex_init ((pthread_mutex_t *) ds->mutex, NULL);

  ds->cond = MALLOC (sizeof (pthread_cond_t));
  if (ds->cond == NULL)
    {
      set_error_buffer (&latest_err_buf, CCI_ER_NO_MORE_MEMORY,
			"memory allocation error: %s", strerror (errno));
      goto create_datasource_error;
    }
  pthread_cond_init ((pthread_cond_t *) ds->cond, NULL);

  ds->num_idle = ds->pool_size;
  for (i = 0; i < ds->max_pool_size; i++)
    {
      T_CON_HANDLE *handle;
      int id;
      char new_url[LINE_MAX + 1];	/* reserve buffer for '\0' */

      if (!cci_datasource_make_url (prop, new_url, ds->url, &latest_err_buf))
	{
	  goto create_datasource_error;
	}

      id = cci_connect_with_url (new_url, ds->user, ds->pass);
      if (id < 0)
	{
	  set_error_buffer (&latest_err_buf, CCI_ER_CONNECT,
			    "Could not connect to database");
	  goto create_datasource_error;
	}

      if (hm_get_connection (id, &handle) != CCI_ER_NO_ERROR)
	{
	  set_error_buffer (&latest_err_buf, CCI_ER_CON_HANDLE, NULL);
	  goto create_datasource_error;
	}
      handle->datasource = ds;
      ds->con_handles[i] = handle->id;
      handle->used = false;
      hm_release_connection (id, &handle);
    }

  ds->is_init = 1;
  ds->num_waiter = 0;

  return ds;

create_datasource_error:

  if (ds->con_handles != NULL)
    {
      for (i = 0; i < ds->max_pool_size; i++)
	{
	  if (ds->con_handles[i] > 0)
	    {
	      cci_disconnect_force (ds->con_handles[i], true);
	    }
	}
    }
  FREE_MEM (ds->user);
  FREE_MEM (ds->pass);
  FREE_MEM (ds->url);
  if (ds->mutex != NULL)
    {
      pthread_mutex_destroy ((pthread_mutex_t *) ds->mutex);
      FREE (ds->mutex);
    }
  if (ds->cond != NULL)
    {
      pthread_cond_destroy ((pthread_cond_t *) ds->cond);
      FREE (ds->cond);
    }
  FREE_MEM (ds->con_handles);
  FREE_MEM (ds);

  copy_error_buffer (err_buf, &latest_err_buf);

  return NULL;
}

void
cci_datasource_destroy (T_CCI_DATASOURCE * ds)
{
  int i;

  if (ds == NULL)
    {
      return;
    }

  /* critical section begin */
  pthread_mutex_lock ((pthread_mutex_t *) ds->mutex);

  if (ds->con_handles)
    {
      for (i = 0; i < ds->max_pool_size; i++)
	{
	  T_CCI_CONN id = ds->con_handles[i];
	  if (id == 0)
	    {
	      /* uninitialized */
	      continue;
	    }
	  if (id < 0)
	    {
	      /* The id has been using */
	      id = -id;
	      cci_disconnect_force (id, false);
	    }
	  else
	    {
	      cci_disconnect_force (id, true);
	    }
	}
      FREE_MEM (ds->con_handles);
    }

  /* critical section end */
  pthread_mutex_unlock ((pthread_mutex_t *) ds->mutex);

  FREE_MEM (ds->user);
  FREE_MEM (ds->pass);
  FREE_MEM (ds->url);

  pthread_mutex_destroy ((pthread_mutex_t *) ds->mutex);
  FREE_MEM (ds->mutex);
  pthread_cond_destroy ((pthread_cond_t *) ds->cond);
  FREE_MEM (ds->cond);

  FREE_MEM (ds);
}

int
cci_datasource_change_property (T_CCI_DATASOURCE * ds, const char *key,
				const char *val)
{
  T_CCI_ERROR err_buf;
  T_CCI_PROPERTIES *properties;
  int error = NO_ERROR;

  if (ds == NULL || !ds->is_init)
    {
      return CCI_ER_INVALID_DATASOURCE;
    }

  properties = cci_property_create ();
  if (properties == NULL)
    {
      return CCI_ER_NO_MORE_MEMORY;
    }

  /* critical section begin */
  pthread_mutex_lock ((pthread_mutex_t *) ds->mutex);

  if (!cci_property_set (properties, (char *) key, (char *) val))
    {
      error = CCI_ER_NO_PROPERTY;
      goto change_property_end;
    }

  if (strcasecmp (key, CCI_DS_PROPERTY_DEFAULT_AUTOCOMMIT) == 0)
    {
      int v;

      if (!cci_property_get_bool_internal (properties,
					   CCI_DS_KEY_DEFAULT_AUTOCOMMIT,
					   &v,
					   CCI_DS_DEFAULT_AUTOCOMMIT_DEFAULT,
					   &err_buf))
	{
	  error = err_buf.err_code;
	  goto change_property_end;
	}

      ds->default_autocommit = v;
    }
  else if (strcasecmp (key, CCI_DS_PROPERTY_DEFAULT_ISOLATION) == 0)
    {
      T_CCI_TRAN_ISOLATION v;

      if (!cci_property_get_isolation (properties,
				       CCI_DS_KEY_DEFAULT_ISOLATION,
				       &v,
				       CCI_DS_DEFAULT_ISOLATION_DEFAULT,
				       &err_buf))
	{
	  error = err_buf.err_code;
	  goto change_property_end;
	}

      ds->default_isolation = v;
    }
  else if (strcasecmp (key, CCI_DS_PROPERTY_DEFAULT_LOCK_TIMEOUT) == 0)
    {
      int v;

      if (!cci_property_get_int (properties, CCI_DS_KEY_DEFAULT_LOCK_TIMEOUT,
				 &v, CCI_DS_DEFAULT_LOCK_TIMEOUT_DEFAULT,
				 CCI_DS_DEFAULT_LOCK_TIMEOUT_DEFAULT,
				 INT_MAX, &err_buf))
	{
	  error = err_buf.err_code;
	  goto change_property_end;
	}

      ds->default_lock_timeout = v;
    }
  else if (strcasecmp (key, CCI_DS_PROPERTY_LOGIN_TIMEOUT) == 0)
    {
      int v;

      if (!cci_property_get_int (properties, CCI_DS_KEY_LOGIN_TIMEOUT, &v,
				 CCI_DS_LOGIN_TIMEOUT_DEFAULT,
				 CCI_LOGIN_TIMEOUT_INFINITE, INT_MAX,
				 &err_buf))
	{
	  error = err_buf.err_code;
	  goto change_property_end;
	}

      ds->login_timeout = v;
    }
  else if (strcasecmp (key, CCI_DS_PROPERTY_POOL_SIZE) == 0)
    {
      int v;

      if (!cci_property_get_int (properties, CCI_DS_KEY_POOL_SIZE, &v,
				 ds->max_pool_size, 1, INT_MAX, &err_buf))
	{
	  error = err_buf.err_code;
	  goto change_property_end;
	}


      if (v > ds->max_pool_size)
	{
	  error = CCI_ER_INVALID_PROPERTY_VALUE;
	  goto change_property_end;
	}

      ds->num_idle += (v - ds->pool_size);
      ds->pool_size = v;
    }
  else
    {
      error = CCI_ER_NO_PROPERTY;
    }

change_property_end:
  pthread_mutex_unlock ((pthread_mutex_t *) ds->mutex);
  /* critical section end */

  if (properties)
    {
      cci_property_destroy (properties);
    }

  return error;
}

T_CCI_CONN
cci_datasource_borrow (T_CCI_DATASOURCE * ds, T_CCI_ERROR * err_buf)
{
  T_CCI_CONN id = -1, mapped_id;
  int i;

  reset_error_buffer (err_buf);

  if (ds == NULL || !ds->is_init)
    {
      set_error_buffer (err_buf, CCI_ER_INVALID_DATASOURCE,
			"CCI data source is invalid");
      return CCI_ER_INVALID_DATASOURCE;
    }

  /* critical section begin */
  pthread_mutex_lock ((pthread_mutex_t *) ds->mutex);
  if (ds->num_idle <= 0 || ds->num_waiter > 0)
    {
      /* wait max_wait msecs */
      struct timespec ts;
      struct timeval tv;
      int r;

      gettimeofday (&tv, NULL);
      ts.tv_sec = tv.tv_sec + (ds->max_wait / 1000);
      ts.tv_nsec = (tv.tv_usec + (ds->max_wait % 1000) * 1000) * 1000;
      if (ts.tv_nsec >= 1000000000)
	{
	  ts.tv_sec += 1;
	  ts.tv_nsec -= 1000000000;
	}

      do
	{
	  ds->num_waiter++;
	  r = pthread_cond_timedwait ((pthread_cond_t *) ds->cond,
				      (pthread_mutex_t *) ds->mutex, &ts);
	  ds->num_waiter--;
	  if (r == ETIMEDOUT)
	    {
	      set_error_buffer (err_buf, CCI_ER_DATASOURCE_TIMEOUT, NULL);
	      pthread_mutex_unlock ((pthread_mutex_t *) ds->mutex);
	      return CCI_ER_DATASOURCE_TIMEOUT;
	    }
	  else if (r != 0)
	    {
	      set_error_buffer (err_buf, CCI_ER_DATASOURCE_TIMEDWAIT,
				"pthread_cond_timedwait : %d", r);
	      pthread_mutex_unlock ((pthread_mutex_t *) ds->mutex);
	      return CCI_ER_DATASOURCE_TIMEDWAIT;
	    }
	}
      while (ds->num_idle <= 0);
    }

  assert (ds->num_idle > 0);

  for (i = 0; i < ds->max_pool_size; i++)
    {
      if (ds->con_handles[i] > 0)
	{
	  id = ds->con_handles[i];
	  ds->con_handles[i] = -id;
	  ds->num_idle--;
	  break;
	}
    }
  pthread_mutex_unlock ((pthread_mutex_t *) ds->mutex);
  /* critical section end */

  if (id < 0)
    {
      set_error_buffer (err_buf, CCI_ER_DATASOURCE_TIMEOUT, NULL);
      return CCI_ER_DATASOURCE_TIMEOUT;
    }
  else
    {
      map_open_otc (id, &mapped_id);

      /* reset to default value */

      cci_set_autocommit (mapped_id, ds->default_autocommit);

      if (ds->default_lock_timeout != CCI_DS_DEFAULT_LOCK_TIMEOUT_DEFAULT)
	{
	  cci_set_lock_timeout (mapped_id, ds->default_lock_timeout, err_buf);
	}

      if (ds->default_isolation != TRAN_UNKNOWN_ISOLATION)
	{
	  cci_set_isolation_level (mapped_id, ds->default_isolation, err_buf);
	}

      cci_set_login_timeout (mapped_id, ds->login_timeout, err_buf);
    }

  return mapped_id;
}

static int
cci_datasource_release_internal (T_CCI_DATASOURCE * ds,
				 T_CON_HANDLE * con_handle)
{
  int i;

  if (con_handle->datasource != ds)
    {
      set_error_buffer (&(con_handle->err_buf), CCI_ER_INVALID_DATASOURCE,
			"The connection does not belong to this data source.");
      return CCI_ER_INVALID_DATASOURCE;
    }

  if (ds->pool_prepared_statement == true)
    {
      hm_pool_restore_used_statements (con_handle);
    }
  else
    {
      qe_close_req_handle_all (con_handle);
    }

  /* critical section begin */
  pthread_mutex_lock ((pthread_mutex_t *) ds->mutex);
  for (i = 0; i < ds->max_pool_size; i++)
    {
      if (ds->con_handles[i] == -(con_handle->id))
	{
	  ds->con_handles[i] = con_handle->id;
	  break;
	}
    }

  if (i == ds->max_pool_size)
    {
      /* could not found con_handles */
      pthread_mutex_unlock ((pthread_mutex_t *) ds->mutex);

      set_error_buffer (&(con_handle->err_buf), CCI_ER_CON_HANDLE, NULL);
      return CCI_ER_CON_HANDLE;
    }

  ds->num_idle++;
  pthread_cond_signal ((pthread_cond_t *) ds->cond);
  pthread_mutex_unlock ((pthread_mutex_t *) ds->mutex);
  /* critical section end */

  return CCI_ER_NO_ERROR;
}

int
cci_datasource_release (T_CCI_DATASOURCE * ds, T_CCI_CONN mapped_conn_id,
			T_CCI_ERROR * err_buf)
{
  int ret = 1;
  T_CON_HANDLE *con_handle;

  reset_error_buffer (err_buf);
  if (ds == NULL || !ds->is_init)
    {
      set_error_buffer (err_buf, CCI_ER_INVALID_DATASOURCE,
			"CCI data source is invalid");
      return 0;
    }

  ret = hm_get_connection (mapped_conn_id, &con_handle);
  if (ret != CCI_ER_NO_ERROR)
    {
      set_error_buffer (err_buf, ret, NULL);
      return 0;
    }
  reset_error_buffer (&(con_handle->err_buf));

  hm_release_connection (mapped_conn_id, &con_handle);
  con_handle->used = false;

  if (cci_end_tran_internal (con_handle, CCI_TRAN_ROLLBACK) != NO_ERROR)
    {
      qe_con_close (con_handle);
      con_handle->con_status = CCI_CON_STATUS_OUT_TRAN;
    }
  ret = cci_datasource_release_internal (ds, con_handle);

  get_last_error (con_handle, err_buf);

  if (ret != CCI_ER_NO_ERROR)
    {
      return 0;
    }
  else
    {
      return 1;
    }
}

int
cci_set_allocators (CCI_MALLOC_FUNCTION malloc_func,
		    CCI_FREE_FUNCTION free_func,
		    CCI_REALLOC_FUNCTION realloc_func,
		    CCI_CALLOC_FUNCTION calloc_func)
{
#if !defined(WINDOWS)
  /* none or all should be set */
  if (malloc_func == NULL && free_func == NULL
      && realloc_func == NULL && calloc_func == NULL)
    {
      /* use default allocators */
      cci_malloc = malloc;
      cci_free = free;
      cci_realloc = realloc;
      cci_calloc = calloc;
    }
  else if (malloc_func == NULL || free_func == NULL
	   || realloc_func == NULL || calloc_func == NULL)
    {
      return CCI_ER_NOT_IMPLEMENTED;
    }
  else
    {
      /* use user defined allocators */
      cci_malloc = malloc_func;
      cci_free = free_func;
      cci_realloc = realloc_func;
      cci_calloc = calloc_func;
    }
#endif

  return CCI_ER_NO_ERROR;
}

int
cci_get_shard_info (int mapped_conn_id, T_CCI_SHARD_INFO ** shard_info,
		    T_CCI_ERROR * err_buf)
{
  T_CON_HANDLE *con_handle = NULL;
  int error = CCI_ER_NO_ERROR;

  reset_error_buffer (err_buf);
  error = hm_get_connection (mapped_conn_id, &con_handle);
  if (error != CCI_ER_NO_ERROR)
    {
      set_error_buffer (err_buf, error, NULL);
      return error;
    }
  reset_error_buffer (&(con_handle->err_buf));

  if (shard_info)
    {
      *shard_info = NULL;
    }

  error = qe_get_shard_info (con_handle, shard_info, &(con_handle->err_buf));
  while (IS_OUT_TRAN (con_handle)
	 && IS_ER_TO_RECONNECT (error, con_handle->err_buf.err_code))
    {
      if (NEED_TO_RECONNECT (con_handle, error))
	{
	  /* Finally, reset_connect will return ER_TIMEOUT */
	  error = reset_connect (con_handle, NULL, &(con_handle->err_buf));
	  if (error != CCI_ER_NO_ERROR)
	    {
	      break;
	    }
	}

      error = qe_get_shard_info (con_handle, shard_info,
				 &(con_handle->err_buf));
    }

  set_error_buffer (&(con_handle->err_buf), error, NULL);
  get_last_error (con_handle, err_buf);
  con_handle->used = false;

  return error;
}

int
cci_shard_info_free (T_CCI_SHARD_INFO * shard_info)
{
  (void) qe_shard_info_free (shard_info);
  return CCI_ER_NO_ERROR;
}

#ifdef CCI_DEBUG
int
cci_time_string (char *buf, struct timeval *time_val)
{
  struct tm tm, *tm_p;
  time_t sec;
  int millisec;

  if (buf == NULL)
    {
      return 0;
    }

  if (time_val == NULL)
    {
      struct timeb tb;

      /* current time */
      (void) ftime (&tb);
      sec = tb.time;
      millisec = tb.millitm;
    }
  else
    {
      sec = time_val->tv_sec;
      millisec = time_val->tv_usec / 1000;
    }

  tm_p = localtime_r (&sec, &tm);
  tm.tm_mon++;

  buf[0] = (tm.tm_mon / 10) + '0';
  buf[1] = (tm.tm_mon % 10) + '0';
  buf[2] = (tm.tm_mday / 10) + '0';
  buf[3] = (tm.tm_mday % 10) + '0';
  buf[4] = ' ';
  buf[5] = (tm.tm_hour / 10) + '0';
  buf[6] = (tm.tm_hour % 10) + '0';
  buf[7] = ':';
  buf[8] = (tm.tm_min / 10) + '0';
  buf[9] = (tm.tm_min % 10) + '0';
  buf[10] = ':';
  buf[11] = (tm.tm_sec / 10) + '0';
  buf[12] = (tm.tm_sec % 10) + '0';
  buf[13] = '.';
  buf[14] = (millisec % 10) + '0';
  millisec /= 10;
  buf[15] = (millisec % 10) + '0';
  millisec /= 10;
  buf[16] = (millisec % 10) + '0';
  buf[17] = '\0';

  return 18;
}
#endif

static void
set_error_buffer (T_CCI_ERROR * err_buf_p,
		  int error, const char *message, ...)
{
  /* don't overwrite when err_buf->error is not equal CCI_ER_NO_ERROR */
  if (error != CCI_ER_NO_ERROR && err_buf_p != NULL
      && err_buf_p->err_code == CCI_ER_NO_ERROR)
    {
      err_buf_p->err_code = error;

      /* Find error message from catalog when you don't give specific message */
      if (message == NULL)
	{
	  cci_get_err_msg (error, err_buf_p->err_msg,
			   sizeof (err_buf_p->err_msg));
	}
      else
	{
	  va_list args;

	  va_start (args, message);

	  if (err_buf_p->err_msg != NULL)
	    {
	      vsnprintf (err_buf_p->err_msg,
			 sizeof (err_buf_p->err_msg), message, args);
	    }

	  va_end (args);
	}
    }
}

/*
 * get_last_error ()
 *   con_handle (in):
 *   dest_err_buf (out):
 */
static void
get_last_error (T_CON_HANDLE * con_handle, T_CCI_ERROR * dest_err_buf)
{
  const char *info_type = NULL;

  if (qe_is_shard (con_handle))
    {
      info_type = "PROXY INFO";
    }
  else
    {
      info_type = "CAS INFO";
    }

  if (con_handle == NULL || dest_err_buf == NULL)
    {
      return;
    }

  if (con_handle->err_buf.err_code != CCI_ER_NO_ERROR
      && con_handle->err_buf.err_msg[0] != '\0')
    {
      dest_err_buf->err_code = con_handle->err_buf.err_code;
      snprintf (dest_err_buf->err_msg, sizeof (dest_err_buf->err_msg),
		"%s[%s-%d.%d.%d.%d:%d,%d,%d].",
		con_handle->err_buf.err_msg, info_type,
		con_handle->ip_addr[0], con_handle->ip_addr[1],
		con_handle->ip_addr[2], con_handle->ip_addr[3],
		con_handle->port, con_handle->cas_id, con_handle->cas_pid);
    }
  else
    {
      copy_error_buffer (dest_err_buf, &(con_handle->err_buf));
    }

  return;
}

static void
copy_error_buffer (T_CCI_ERROR * dest_err_buf_p, T_CCI_ERROR * src_err_buf_p)
{
  if (dest_err_buf_p != NULL && src_err_buf_p != NULL)
    {
      *dest_err_buf_p = *src_err_buf_p;
    }
}

/*
 * cci_get_cas_info()
 *   info_buf (out):
 *   buf_length (in):
 *   err_buf (in):
 */
int
cci_get_cas_info (int mapped_conn_id, char *info_buf, int buf_length,
		  T_CCI_ERROR * err_buf)
{
  int error = CCI_ER_NO_ERROR;
  T_CON_HANDLE *con_handle = NULL;

  reset_error_buffer (err_buf);

  if (info_buf == NULL || buf_length <= 0)
    {
      set_error_buffer (err_buf, CCI_ER_INVALID_ARGS, NULL);
      return CCI_ER_INVALID_ARGS;
    }

  error = hm_get_connection (mapped_conn_id, &con_handle);
  if (error != CCI_ER_NO_ERROR)
    {
      set_error_buffer (err_buf, error, NULL);
      return error;
    }

  reset_error_buffer (&(con_handle->err_buf));

  API_SLOG (con_handle);

  snprintf (info_buf, buf_length - 1, "%d.%d.%d.%d:%d,%d,%d",
	    con_handle->ip_addr[0], con_handle->ip_addr[1],
	    con_handle->ip_addr[2], con_handle->ip_addr[3],
	    con_handle->port, con_handle->cas_id, con_handle->cas_pid);

  info_buf[buf_length - 1] = '\0';

  if (con_handle->log_trace_api)
    {
      CCI_LOGF_DEBUG (con_handle->logger, "[%s]", info_buf);
    }

  API_ELOG (con_handle, error);

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("(%d)get_cas_info:%s",
				    mapped_conn_id, info_buf));
#endif

  get_last_error (con_handle, err_buf);
  con_handle->used = false;

  return error;
}
