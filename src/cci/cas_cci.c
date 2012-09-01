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
#include "error_code.h"

#if defined(WINDOWS)
int wsa_initialize ();
#define getpid _getpid
#endif

#ifdef CCI_XA
#include "cci_xa.h"
#endif

/************************************************************************
 * PRIVATE DEFINITIONS							*
 ************************************************************************/
#define MAKE_STRI(x) #x
#define MAKE_STR(x) MAKE_STRI(x)

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

#define IS_STMT_POOL(c) \
  ((c)->datasource && (c)->datasource->pool_prepared_statement)
#define IS_BROKER_STMT_POOL(c) \
  ((c)->broker_info[BROKER_INFO_STATEMENT_POOLING] == CAS_STATEMENT_POOLING_ON)
#define IS_OUT_TRAN(c) ((c)->con_status == CCI_CON_STATUS_OUT_TRAN)
#define IS_ER_COMMUNICATION(e) \
  ((e) == CCI_ER_COMMUNICATION || (e) == CAS_ER_COMMUNICATION)
#define IS_SERVER_DOWN(e) \
  (((e) == ER_TM_SERVER_DOWN_UNILATERALLY_ABORTED) \
   || ((e) == ER_OBJ_NO_CONNECT) || ((e) == ER_NET_SERVER_CRASHED) \
   || ((e) == ER_BO_CONNECT_FAILED))
#define CCI_SET_ERROR_BUFFER(e, err_buf) \
  do { \
    if (((e) != CCI_ER_NO_ERROR) && ((err_buf) != NULL) \
       && ((err_buf)->err_code == CCI_ER_NO_ERROR)) { \
      (err_buf)->err_code = (e); \
      cci_get_err_msg ((e), (err_buf)->err_msg, sizeof ((err_buf)->err_msg)); \
    } \
  } while (0)

/* default value of each datesource property */
#define CCI_DS_POOL_SIZE_DEFAULT 			10
#define CCI_DS_MAX_WAIT_DEFAULT 			1000
#define CCI_DS_POOL_PREPARED_STATEMENT_DEFAULT 		false
#define CCI_DS_DISCONNECT_ON_QUERY_TIMEOUT_DEFAULT	false
#define CCI_DS_DEFAULT_AUTOCOMMIT_DEFAULT 		-1
#define CCI_DS_DEFAULT_ISOLATION_DEFAULT 		TRAN_UNKNOWN_ISOLATION
#define CCI_DS_DEFAULT_LOCK_TIMEOUT_DEFAULT 		CCI_LOCK_TIMEOUT_DEFAULT

#define CON_HANDLE_ID_FACTOR            1000000
#define CON_ID(a) ((a) / CON_HANDLE_ID_FACTOR)
#define REQ_ID(a) ((a) % CON_HANDLE_ID_FACTOR)
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
static int cas_connect_internal (T_CON_HANDLE * con_handle,
				 T_CCI_ERROR * err_buf, int *connect);
static int get_query_info (int req_h_id, char log_type, char **out_buf);
static int next_result_cmd (int req_h_id, char flag, T_CCI_ERROR * err_buf);

static int cas_end_session (T_CON_HANDLE * con_handle, T_CCI_ERROR * err_buf);

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

static THREAD_FUNC execute_thread (void *arg);
static int get_thread_result (T_CON_HANDLE * con_handle,
			      T_CCI_ERROR * err_buf);
static int connect_prepare_again (T_CON_HANDLE * con_handle,
				  T_REQ_HANDLE * req_handle,
				  T_CCI_ERROR * err_buf);
static const char *cci_get_err_msg_internal (int err_code);

static int cci_get_new_handle_id (char *ip, int port, char *db_name,
				  char *db_user, char *dbpasswd);
static bool cci_datasource_make_url (T_CCI_PROPERTIES * prop, char *new_url,
				     char *url, T_CCI_ERROR * err_buf);

static int cci_time_string (char *buf, struct timeval *time_val);
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
extern HANDLE ha_status_mutex;
#else
static T_MUTEX con_handle_table_mutex = PTHREAD_MUTEX_INITIALIZER;
extern T_MUTEX ha_status_mutex;
#endif

static char init_flag = 0;
static char cci_init_flag = 1;
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
  CCI_DS_PROPERTY_LOGIN_TIMEOUT,
  CCI_DS_PROPERTY_QUERY_TIMEOUT,
  CCI_DS_PROPERTY_DISCONNECT_ON_QUERY_TIMEOUT,
  CCI_DS_PROPERTY_DEFAULT_AUTOCOMMIT,
  CCI_DS_PROPERTY_DEFAULT_ISOLATION,
  CCI_DS_PROPERTY_DEFAULT_LOCK_TIMEOUT
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
  long start_time_milli, end_time_milli;
  struct timeval end_time;

  assert (start_time);

  if (start_time->tv_sec == 0 && start_time->tv_usec == 0)
    {
      return 0;
    }

  gettimeofday (&end_time, NULL);

  start_time_milli = start_time->tv_sec * 1000 + start_time->tv_usec / 1000;
  end_time_milli = end_time.tv_sec * 1000 + end_time.tv_usec / 1000;

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
      MUTEX_INIT (ha_status_mutex);
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
  snprintf (str, len, "%s", build_number);
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
  int conn_id;
  T_CON_HANDLE *conn;
  int err_code = CCI_ER_NO_ERROR;

  if (ip == NULL || port < 0 || db == NULL || user == NULL || pass == NULL)
    {
      err_code = CCI_ER_CONNECT;
      goto error;
    }

#if defined(WINDOWS)
  if (wsa_initialize () < 0)
    {
      err_code = CCI_ER_CONNECT;
      goto error;
    }
#endif

  conn_id = cci_get_new_handle_id (ip, port, db, user, pass);
  if (conn_id < 0)
    {
      err_code = conn_id;
      goto error;
    }

  conn = hm_find_con_handle (conn_id);
  if (conn == NULL)
    {
      err_code = CCI_ER_CON_HANDLE;
      goto error;
    }

  SET_START_TIME_FOR_LOGIN (conn);
  err_code = cas_connect (conn, err_buf);
  if (err_code < 0)
    {
      hm_con_handle_free (conn_id);
      goto error;
    }
  err_code = qe_end_tran (conn, CCI_TRAN_COMMIT, err_buf);
  if (err_code < 0)
    {
      hm_con_handle_free (conn_id);
      goto error;
    }
  SET_AUTOCOMMIT_FROM_CASINFO (conn);
  RESET_START_TIME (conn);

  return conn_id;

error:
  CCI_SET_ERROR_BUFFER (err_code, err_buf);

  return err_code;
}

int
cci_connect_ex (char *ip, int port, char *db, char *user, char *pass,
		T_CCI_ERROR * err_buf)
{
  return cci_connect_internal (ip, port, db, user, pass, err_buf);
}

int
CCI_CONNECT_INTERNAL_FUNC_NAME (char *ip, int port, char *db_name,
				char *db_user, char *dbpasswd)
{
  int id;
#ifdef CCI_DEBUG
  int pid = getpid ();
#endif

  id = cci_connect_internal (ip, port, db_name, db_user, dbpasswd, NULL);
#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("cci_connect (pid %d, h:%d): %s %d %s %s %s", pid,
		    id, ip, port, DEBUG_STR (db_name),
		    DEBUG_STR (db_user), DEBUG_STR (dbpasswd)));
#endif
  return id;
}

static int
cci_connect_with_url_internal (char *url, char *user, char *pass,
			       T_CCI_ERROR * err_buf)
{
  char *token[MAX_URL_MATCH_COUNT];
  int err_code = CCI_ER_NO_ERROR;
  unsigned i;

  char *property = NULL;
  char *end = NULL;
  char *host, *dbname;
  int port;
  int conn_id;
  bool use_url = false;
  T_CON_HANDLE *conn = NULL;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("cci_connect_with_url %s %s %s",
		    DEBUG_STR (url), DEBUG_STR (user), DEBUG_STR (pass)));
#endif

  if (url == NULL)
    {
      err_code = CCI_ER_CONNECT;
      goto ret;
    }

#if defined(WINDOWS)
  if (wsa_initialize () < 0)
    {
      err_code = CCI_ER_CONNECT;
      goto ret;
    }
#endif

  /* The NULL is same as "". */
  if (user == NULL)
    {
      user = (char *) "";
    }
  if (pass == NULL)
    {
      pass = (char *) "";
    }

  if (user[0] == '\0')
    {
      if (pass[0] != '\0')
	{
	  /* error - cci_connect_with_url (url, "", "pass") */
	  err_code = CCI_ER_CONNECT;
	  goto ret;
	}
      use_url = true;
    }

  err_code = cci_url_match (url, token);
  if (err_code != CCI_ER_NO_ERROR)
    {
      goto ret;
    }

  host = token[0];
  port = (int) strtol (token[1], &end, 10);
  dbname = token[2];
  if (use_url)
    {
      user = token[3];
      pass = token[4];
    }
  property = token[5];
  if (property == NULL)
    {
      property = (char *) "";
    }

  if (user[0] == '\0')
    {
      /* A user don't exist in the parameter and url */
      user = (char *) "PUBLIC";
    }

  conn_id = cci_get_new_handle_id (host, port, dbname, user, pass);
  if (conn_id < 0)
    {
      err_code = conn_id;
      goto ret;
    }

  conn = hm_find_con_handle (conn_id);
  if (conn == NULL)
    {
      err_code = CCI_ER_CON_HANDLE;
      goto ret;
    }

  snprintf (conn->url, SRV_CON_URL_SIZE,
	    "cci:cubrid:%s:%d:%s:%s:********:%s",
	    host, port, dbname, user, property);
  if (property != NULL)
    {
      err_code = cci_conn_set_properties (conn, property);
      API_SLOG (conn);
      if (conn->log_trace_api)
	{
	  CCI_LOGF_DEBUG (conn->logger, "URL[%s]", url);
	}
      if (err_code < 0)
	{
	  hm_con_handle_free (conn_id);
	  goto ret;
	}

      if (conn->alter_host_count > 0)
	{
	  conn->alter_host_id = 0;
	}
    }

  SET_START_TIME_FOR_LOGIN (conn);
  err_code = cas_connect (conn, err_buf);
  if (err_code < 0)
    {
      hm_con_handle_free (conn_id);
      goto ret;
    }
  err_code = qe_end_tran (conn, CCI_TRAN_COMMIT, err_buf);
  if (err_code < 0)
    {
      hm_con_handle_free (conn_id);
      goto ret;
    }
  SET_AUTOCOMMIT_FROM_CASINFO (conn);
  RESET_START_TIME (conn);

ret:
  for (i = 0; i < MAX_URL_MATCH_COUNT; i++)
    {
      FREE_MEM (token[i]);
    }

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("cci_connect_with_url return %d", conn_id));
#endif
  if (conn_id >= 0)
    {
      API_ELOG (conn, conn_id);
    }

  if (err_code == CCI_ER_NO_ERROR)
    {
      return conn_id;
    }

  CCI_SET_ERROR_BUFFER (err_code, err_buf);

  return err_code;
}

int
cci_connect_with_url (char *url, char *user, char *password)
{
  return cci_connect_with_url_internal (url, user, password, NULL);
}

int
cci_connect_with_url_ex (char *url, char *user, char *pass,
			 T_CCI_ERROR * err_buf)
{
  return cci_connect_with_url_internal (url, user, pass, err_buf);
}

static int
cas_end_session (T_CON_HANDLE * con_handle, T_CCI_ERROR * err_buf)
{
  int err = CCI_ER_NO_ERROR;

  API_SLOG (con_handle);

  err = qe_end_session (con_handle, err_buf);
  if (IS_ER_COMMUNICATION (err) &&
      con_handle->con_status == CCI_CON_STATUS_OUT_TRAN)
    {
      int connect_done;

      err = cas_connect_with_ret (con_handle, err_buf, &connect_done);
      if (err < 0)
	{
	  return err;
	}
      if (connect_done)
	{
	  err = qe_end_session (con_handle, err_buf);
	}
    }

  API_ELOG (con_handle, err);
  return err;
}

int
cci_disconnect (int con_h_id, T_CCI_ERROR * err_buf)
{
  int err_code = CCI_ER_NO_ERROR;
  T_CON_HANDLE *con_handle;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("(%d)cci_disconnect", con_h_id));
#endif

  err_buf_reset (err_buf);

  while (1)
    {
      MUTEX_LOCK (con_handle_table_mutex);

      con_handle = hm_find_con_handle (con_h_id);
      if (con_handle == NULL)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  err_code = CCI_ER_CON_HANDLE;
	  goto ret;
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

  API_SLOG (con_handle);

  if (con_handle->datasource)
    {
      con_handle->ref_count = 0;
      cas_end_session (con_handle, err_buf);
      cci_datasource_release (con_handle->datasource, con_h_id, err_buf);
      if (con_handle->log_trace_api)
	{
	  CCI_LOGF_DEBUG (con_handle->logger,
			  "[%04d][API][E][cci_datasource_release]", con_h_id);
	}
    }
  else if (con_handle->broker_info[BROKER_INFO_CCI_PCONNECT]
	   && hm_put_con_to_pool (con_h_id) >= 0)
    {
      con_handle->ref_count = 0;
      cci_end_tran (con_h_id, CCI_TRAN_ROLLBACK, err_buf);

      /* We have to call end session for connection pool also.
       * cas_end_session might return an error but there's nothing we can
       * do about it at this point
       */
      cas_end_session (con_handle, err_buf);
      API_ELOG (con_handle, 0);
    }
  else
    {
      /* We have to call end session for connection pool also.
       * cas_end_session might return an error but there's nothing we can
       * do about it at this point.
       */
      err_code = qe_con_close (con_handle);
      API_ELOG (con_handle, err_code);

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
    }

ret:
  CCI_SET_ERROR_BUFFER (err_code, err_buf);

  return err_code;
}

int
cci_cancel (int con_h_id)
{
  T_CON_HANDLE *con_handle;
  int ref_count;
  int err;

  MUTEX_LOCK (con_handle_table_mutex);

  con_handle = hm_find_con_handle (con_h_id);
  if (con_handle == NULL)
    {
      MUTEX_UNLOCK (con_handle_table_mutex);
      return CCI_ER_CON_HANDLE;
    }

  ref_count = con_handle->ref_count;

  MUTEX_UNLOCK (con_handle_table_mutex);

  if (ref_count <= 0)
    {
      return 0;
    }

  API_SLOG (con_handle);
  err = net_cancel_request (con_handle);
  API_ELOG (con_handle, err);

  return err;
}

int
cci_end_tran (int con_h_id, char type, T_CCI_ERROR * err_buf)
{
  int err_code = CCI_ER_NO_ERROR;
  T_CON_HANDLE *con_handle;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("(%d)tran: %s", con_h_id, dbg_tran_type_str (type)));
#endif

  err_buf_reset (err_buf);

  while (1)
    {
      MUTEX_LOCK (con_handle_table_mutex);

      con_handle = hm_find_con_handle (con_h_id);
      if (con_handle == NULL)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  err_code = CCI_ER_CON_HANDLE;
	  goto ret;
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

  API_SLOG (con_handle);
  if (con_handle->con_status != CCI_CON_STATUS_OUT_TRAN)
    {
      err_code = qe_end_tran (con_handle, type, err_buf);
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
  API_ELOG (con_handle, err_code);

ret:
  if (con_handle != NULL)
    {
      con_handle->ref_count = 0;
    }

  CCI_SET_ERROR_BUFFER (err_code, err_buf);

  return err_code;
}

int
cci_prepare (int con_id, char *sql_stmt, char flag, T_CCI_ERROR * err_buf)
{
  int req_handle_id = -1;
  int err_code = CCI_ER_NO_ERROR;
  int con_err_code = 0;
  int connect_done;
  T_CON_HANDLE *con_handle = NULL;
  T_REQ_HANDLE *req_handle = NULL;

  err_buf_reset (err_buf);

  if (sql_stmt == NULL)
    {
      err_code = CCI_ER_STRING_PARAM;
      goto error;
    }

  while (1)
    {
      MUTEX_LOCK (con_handle_table_mutex);

      con_handle = hm_find_con_handle (con_id);
      if (con_handle == NULL)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  err_code = CCI_ER_CON_HANDLE;
	  goto error;
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

  API_SLOG (con_handle);
  if (con_handle->log_trace_api)
    {
      CCI_LOGF_DEBUG (con_handle->logger, "FLAG[%d],SQL[%s]", flag, sql_stmt);
    }

  if (IS_STMT_POOL (con_handle))
    {
      req_handle_id = hm_req_get_from_pool (con_handle, sql_stmt);
      if (req_handle_id != CCI_ER_REQ_HANDLE)
	{
	  cci_set_query_timeout (req_handle_id, con_handle->query_timeout);
	  goto prepare_end;
	}
    }

  req_handle_id = hm_req_handle_alloc (con_id, &req_handle);
  if (req_handle_id < 0)
    {
      err_code = req_handle_id;
      goto prepare_error;
    }

  SET_START_TIME_FOR_QUERY (con_handle, req_handle);

  err_code = qe_prepare (req_handle, con_handle, sql_stmt, flag, err_buf, 0);

  if ((IS_OUT_TRAN (con_handle) && IS_ER_COMMUNICATION (err_code))
      || IS_SERVER_DOWN (err_buf->err_code))
    {
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
      err_code = qe_prepare (req_handle, con_handle, sql_stmt,
			     flag, err_buf, 0);
    }

  if (err_code < 0)
    {
      goto prepare_error;
    }

  if (IS_STMT_POOL (con_handle))
    {
      err_code = hm_req_add_to_pool (con_handle, sql_stmt, req_handle_id);
      if (err_code != CCI_ER_NO_ERROR)
	{
	  goto prepare_error;
	}
    }

prepare_end:
  RESET_START_TIME (con_handle);
  con_handle->ref_count = 0;

  API_ELOG (con_handle, req_handle_id);
#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("(%d:%d)pre: %d %s", con_id,
		    REQ_ID (req_handle_id), flag, DEBUG_STR (sql_stmt)));
#endif
  return req_handle_id;

prepare_error:
  RESET_START_TIME (con_handle);

  if (req_handle)
    {
      hm_req_handle_free (con_handle, req_handle_id, req_handle);
    }
  con_handle->ref_count = 0;

  if (err_code == CCI_ER_QUERY_TIMEOUT &&
      con_handle->disconnect_on_query_timeout)
    {
      CLOSE_SOCKET (con_handle->sock_fd);
      con_handle->sock_fd = INVALID_SOCKET;
      con_handle->con_status = CCI_CON_STATUS_OUT_TRAN;
    }

  API_ELOG (con_handle, err_code);
#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("(%d:%d)cci_prepare error: code(%d), flag(%d) %s", con_id,
		    REQ_ID (req_handle_id), err_code, flag,
		    DEBUG_STR (sql_stmt)));
#endif

error:
  CCI_SET_ERROR_BUFFER (err_code, err_buf);

  return (err_code);
}

int
cci_get_bind_num (int req_h_id)
{
  T_REQ_HANDLE *req_handle;
  int num_bind;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("(%d:%d)cci_get_bind_num", CON_ID (req_h_id),
		    REQ_ID (req_h_id)));
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
  CCI_DEBUG_PRINT (print_debug_msg
		   ("(%d:%d)cci_is_updatable", CON_ID (req_h_id),
		    REQ_ID (req_h_id)));
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

#ifdef CCI_FULL_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("(%d:%d)cci_get_result_info", CON_ID (req_h_id),
		    REQ_ID (req_h_id)));
#endif

  if (cmd_type)
    {
      *cmd_type = (T_CCI_CUBRID_STMT) - 1;
    }

  if (num)
    {
      *num = 0;
    }

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

#ifdef CCI_FULL_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("(%d:%d)cci_bind_param: %d %s %p %s %d",
		    CON_ID (req_h_id), REQ_ID (req_h_id), index,
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

#ifdef CCI_FULL_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("(%d:%d)cci_bind_param_array_size: %d", CON_ID (req_h_id),
		    REQ_ID (req_h_id), array_size));
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

#ifdef CCI_FULL_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("(%d:%d)cci_bind_param_array: %d %s %p %p %s",
		    CON_ID (req_h_id), REQ_ID (req_h_id), index,
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
  int err_code = CCI_ER_NO_ERROR;
  struct timeval st, et;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("(%d:%d)exe: %d, %d",
		    CON_ID (req_h_id), REQ_ID (req_h_id), flag,
		    max_col_size));
#endif

  err_buf_reset (err_buf);

  while (1)
    {
      MUTEX_LOCK (con_handle_table_mutex);

      req_handle = hm_find_req_handle (req_h_id, &con_handle);
      if (req_handle == NULL)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  err_code = CCI_ER_REQ_HANDLE;
	  goto ret;
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

  SET_START_TIME_FOR_QUERY (con_handle, req_handle);

  if (IS_BROKER_STMT_POOL (con_handle))
    {
      err_code = connect_prepare_again (con_handle, req_handle, err_buf);
    }

  if (err_code >= 0)
    {
      if (flag & CCI_EXEC_THREAD)
	{
	  T_THREAD thrid;

	  err_buf_reset (&(con_handle->thr_arg.err_buf));
	  con_handle->thr_arg.req_handle = req_handle;
	  con_handle->thr_arg.flag = flag ^ CCI_EXEC_THREAD;
	  con_handle->thr_arg.max_col_size = max_col_size;
	  con_handle->thr_arg.ref_count_ptr = &(con_handle->ref_count);
	  con_handle->thr_arg.con_handle = con_handle;
	  THREAD_BEGIN (thrid, execute_thread, &(con_handle->thr_arg));
	  err_code = CCI_ER_THREAD_RUNNING;
	  goto thread_end;
	}

      err_code = qe_execute (req_handle, con_handle, flag, max_col_size,
			     err_buf);
    }
  if ((IS_OUT_TRAN (con_handle) && IS_ER_COMMUNICATION (err_code))
      || IS_SERVER_DOWN (err_buf->err_code))
    {
      int connect_done;

      err_code = cas_connect_with_ret (con_handle, err_buf, &connect_done);
      if (err_code < 0)
	{
	  goto execute_end;
	}

      if (connect_done)
	{
	  /* error is caused by connection fail */
	  req_handle_content_free (req_handle, 1);
	  err_code = qe_prepare (req_handle, con_handle, req_handle->sql_text,
				 req_handle->prepare_flag, err_buf, 1);
	  if (err_code < 0)
	    {
	      goto execute_end;
	    }
	  err_code = qe_execute (req_handle, con_handle, flag, max_col_size,
				 err_buf);
	}
    }

  /* If prepared plan is invalidated while using plan cache,
     the error, CAS_ER_STMT_POOLING, is returned.
     In this case, prepare and execute have to be executed again.
   */
  while (err_code == CAS_ER_STMT_POOLING && IS_BROKER_STMT_POOL (con_handle))
    {
      req_handle_content_free (req_handle, 1);
      err_code = qe_prepare (req_handle, con_handle, req_handle->sql_text,
			     req_handle->prepare_flag, err_buf, 1);
      if (err_code < 0)
	{
	  goto execute_end;
	}
      err_code = qe_execute (req_handle, con_handle, flag, max_col_size,
			     err_buf);
    }

execute_end:
  RESET_START_TIME (con_handle);

  if (err_code == CCI_ER_QUERY_TIMEOUT &&
      con_handle->disconnect_on_query_timeout)
    {
      CLOSE_SOCKET (con_handle->sock_fd);
      con_handle->sock_fd = INVALID_SOCKET;
      con_handle->con_status = CCI_CON_STATUS_OUT_TRAN;
    }

  API_ELOG (con_handle, err_code);
  if (con_handle->log_slow_queries)
    {
      long elapsed;

      gettimeofday (&et, NULL);
      elapsed = ELAPSED_MSECS (et, st);
      if (elapsed > con_handle->slow_query_threshold_millis)
	{
	  CCI_LOGF_DEBUG (con_handle->logger, "[%04d][SLOW][%d] SQL[%s]",
			  con_handle->id, elapsed, req_handle->sql_text);
	}
    }

ret:
  if (con_handle != NULL)
    {
      con_handle->ref_count = 0;
    }

thread_end:
  CCI_SET_ERROR_BUFFER (err_code, err_buf);

  return err_code;
}

int
cci_prepare_and_execute (int con_id, char *sql_stmt,
			 int max_col_size, int *exec_retval,
			 T_CCI_ERROR * err_buf)
{
  T_CON_HANDLE *con_handle = NULL;
  T_REQ_HANDLE *req_handle = NULL;
  int err_code = CCI_ER_NO_ERROR;
  int req_handle_id;
  struct timeval st, et;

  if (exec_retval != NULL)
    {
      *exec_retval = 0;
    }

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("cci_prepare_and_execute %d %d %d %d %s", con_handle,
		    max_col_size, DEBUG_STR (sql_stmt)));
#endif

  err_buf_reset (err_buf);

  if (sql_stmt == NULL)
    {
      err_code = CCI_ER_STRING_PARAM;
      goto error;
    }

  while (1)
    {
      MUTEX_LOCK (con_handle_table_mutex);

      con_handle = hm_find_con_handle (con_id);
      if (con_handle == NULL)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  err_code = CCI_ER_CON_HANDLE;
	  goto error;
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

  req_handle_id = hm_req_handle_alloc (con_id, &req_handle);
  if (req_handle_id < 0)
    {
      err_code = req_handle_id;
      goto prepare_execute_error;
    }

  SET_START_TIME_FOR_QUERY (con_handle, req_handle);

  err_code = qe_prepare_and_execute (req_handle, con_handle, sql_stmt,
				     max_col_size, err_buf);

  if (exec_retval != NULL)
    {
      *exec_retval = err_code;
    }

  API_ELOG (con_handle, err_code);
  if (err_code < 0)
    {
      goto prepare_execute_error;
    }

  if (con_handle->log_slow_queries)
    {
      long elapsed;

      gettimeofday (&et, NULL);
      elapsed = ELAPSED_MSECS (et, st);
      if (elapsed > con_handle->slow_query_threshold_millis)
	{
	  CCI_LOGF_DEBUG (con_handle->logger, "[%04d][SLOW][%d] SQL[%s]",
			  con_handle->id, elapsed, sql_stmt);
	}
    }

  RESET_START_TIME (con_handle);
  con_handle->ref_count = 0;
  return req_handle_id;

prepare_execute_error:
  RESET_START_TIME (con_handle);

  if (req_handle)
    {
      hm_req_handle_free (con_handle, req_handle_id, req_handle);
    }

  con_handle->ref_count = 0;

  if (err_code == CCI_ER_QUERY_TIMEOUT &&
      con_handle->disconnect_on_query_timeout)
    {
      CLOSE_SOCKET (con_handle->sock_fd);
      con_handle->sock_fd = INVALID_SOCKET;
      con_handle->con_status = CCI_CON_STATUS_OUT_TRAN;
    }

error:
  CCI_SET_ERROR_BUFFER (err_code, err_buf);

  return err_code;
}

int
cci_get_thread_result (int con_id, T_CCI_ERROR * err_buf)
{
  int err_code = CCI_ER_NO_ERROR;
  T_CON_HANDLE *con_handle;

  err_buf_reset (err_buf);

  MUTEX_LOCK (con_handle_table_mutex);

  con_handle = hm_find_con_handle (con_id);
  if (con_handle == NULL)
    {
      MUTEX_UNLOCK (con_handle_table_mutex);
      err_code = CCI_ER_CON_HANDLE;
      goto ret;
    }

  err_code = get_thread_result (con_handle, err_buf);

  MUTEX_UNLOCK (con_handle_table_mutex);

ret:
  CCI_SET_ERROR_BUFFER (err_code, err_buf);

  return err_code;
}

int
cci_next_result (int req_h_id, T_CCI_ERROR * err_buf)
{
#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("(%d:%d)cci_next_result", CON_ID (req_h_id),
		    REQ_ID (req_h_id)));
#endif

  return (next_result_cmd (req_h_id, CCI_CLOSE_CURRENT_RESULT, err_buf));
}

int
cci_execute_array (int req_h_id, T_CCI_QUERY_RESULT ** qr,
		   T_CCI_ERROR * err_buf)
{
  T_REQ_HANDLE *req_handle;
  T_CON_HANDLE *con_handle;
  int err_code = CCI_ER_NO_ERROR;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("(%d:%d)cci_execute_array", CON_ID (req_h_id),
		    REQ_ID (req_h_id)));
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
	  err_code = CCI_ER_REQ_HANDLE;
	  goto ret;
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

  SET_START_TIME_FOR_QUERY (con_handle, req_handle);

  if (IS_BROKER_STMT_POOL (con_handle))
    {
      err_code = connect_prepare_again (con_handle, req_handle, err_buf);
    }

  if (err_code >= 0)
    {
      err_code = qe_execute_array (req_handle, con_handle, qr, err_buf);
    }

  if ((IS_OUT_TRAN (con_handle) && IS_ER_COMMUNICATION (err_code))
      || IS_SERVER_DOWN (err_buf->err_code))
    {
      int connect_done;

      err_code = cas_connect_with_ret (con_handle, err_buf, &connect_done);
      if (err_code < 0)
	{
	  goto execute_end;
	}

      if (connect_done)
	{
	  req_handle_content_free (req_handle, 1);
	  err_code = qe_prepare (req_handle, con_handle, req_handle->sql_text,
				 req_handle->prepare_flag, err_buf, 1);
	  if (err_code < 0)
	    {
	      goto execute_end;
	    }
	  err_code = qe_execute_array (req_handle, con_handle, qr, err_buf);
	}
    }

  while (err_code == CAS_ER_STMT_POOLING && IS_BROKER_STMT_POOL (con_handle))
    {
      req_handle_content_free (req_handle, 1);
      err_code = qe_prepare (req_handle, con_handle, req_handle->sql_text,
			     req_handle->prepare_flag, err_buf, 1);
      if (err_code < 0)
	{
	  goto execute_end;
	}
      err_code = qe_execute_array (req_handle, con_handle, qr, err_buf);
    }

execute_end:
  RESET_START_TIME (con_handle);

  con_handle->ref_count = 0;

  if (err_code == CCI_ER_QUERY_TIMEOUT &&
      con_handle->disconnect_on_query_timeout)
    {
      CLOSE_SOCKET (con_handle->sock_fd);
      con_handle->sock_fd = INVALID_SOCKET;
      con_handle->con_status = CCI_CON_STATUS_OUT_TRAN;
    }

ret:
  CCI_SET_ERROR_BUFFER (err_code, err_buf);

  return err_code;
}

int
cci_get_db_parameter (int con_h_id, T_CCI_DB_PARAM param_name, void *value,
		      T_CCI_ERROR * err_buf)
{
  T_CON_HANDLE *con_handle;
  int err_code = CCI_ER_NO_ERROR;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("cci_get_db_parameter %d %s",
				    con_h_id, dbg_db_param_str (param_name)));
#endif

  err_buf_reset (err_buf);

  if (param_name < CCI_PARAM_FIRST || param_name > CCI_PARAM_LAST)
    {
      err_code = CCI_ER_PARAM_NAME;
      goto ret;
    }

  while (1)
    {
      MUTEX_LOCK (con_handle_table_mutex);

      con_handle = hm_find_con_handle (con_h_id);
      if (con_handle == NULL)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  err_code = CCI_ER_CON_HANDLE;
	  goto ret;
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
      err_code = qe_get_db_parameter (con_handle, param_name, value, err_buf);
    }

ret:
  if (con_handle != NULL)
    {
      con_handle->ref_count = 0;
    }

  CCI_SET_ERROR_BUFFER (err_code, err_buf);

  return err_code;
}

long
cci_escape_string (int con_h_id, char *to, const char *from,
		   unsigned long length, T_CCI_ERROR * err_buf)
{
  T_CON_HANDLE *con_handle;
  int err_code = CCI_ER_NO_ERROR;
  unsigned long i;
  char *target_ptr = to;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("cci_escape_string %d", con_h_id));
#endif

  err_buf_reset (err_buf);

  while (1)
    {
      MUTEX_LOCK (con_handle_table_mutex);

      con_handle = hm_find_con_handle (con_h_id);
      if (con_handle == NULL)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  err_code = CCI_ER_CON_HANDLE;
	  goto error;
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

  if (err_code < 0)
    {
      goto error;
    }

  if (con_handle->no_backslash_escapes == CCI_NO_BACKSLASH_ESCAPES_NOT_SET)
    {
      err_code = qe_get_db_parameter (con_handle,
				      CCI_PARAM_NO_BACKSLASH_ESCAPES,
				      &con_handle->no_backslash_escapes,
				      err_buf);

      if (err_code < 0)
	{
	  goto error;
	}
    }

  for (i = 0; i < length; i++)
    {
      if (from[i] == '\'')
	{
	  /* single-quote is converted to two-single-quote */
	  *(target_ptr) = '\'';
	  *(target_ptr + 1) = '\'';
	  target_ptr += 2;
	}
      else if (con_handle->no_backslash_escapes ==
	       CCI_NO_BACKSLASH_ESCAPES_FALSE)
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

  con_handle->ref_count = 0;
  return ((long) (target_ptr - to));

error:
  if (con_handle != NULL)
    {
      con_handle->ref_count = 0;
    }

  CCI_SET_ERROR_BUFFER (err_code, err_buf);

  return ((long) err_code);
}

int
cci_set_db_parameter (int con_h_id, T_CCI_DB_PARAM param_name, void *value,
		      T_CCI_ERROR * err_buf)
{
  T_CON_HANDLE *con_handle;
  int err_code = CCI_ER_NO_ERROR;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("cci_set_db_parameter %d %s", con_h_id,
		    dbg_db_param_str (param_name)));
#endif

  err_buf_reset (err_buf);

  if (param_name < CCI_PARAM_FIRST || param_name > CCI_PARAM_LAST)
    {
      err_code = CCI_ER_PARAM_NAME;
      goto ret;
    }

  while (1)
    {
      MUTEX_LOCK (con_handle_table_mutex);

      con_handle = hm_find_con_handle (con_h_id);
      if (con_handle == NULL)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  err_code = CCI_ER_CON_HANDLE;
	  goto ret;
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
      err_code = qe_set_db_parameter (con_handle, param_name, value, err_buf);
    }

ret:
  if (con_handle != NULL)
    {
      con_handle->ref_count = 0;
    }

  CCI_SET_ERROR_BUFFER (err_code, err_buf);

  return err_code;
}

int
cci_close_req_handle (int req_h_id)
{
  T_REQ_HANDLE *req_handle;
  T_CON_HANDLE *con_handle;
  int err_code = 0;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("(%d:%d)cci_close_req_handle", CON_ID (req_h_id),
		    REQ_ID (req_h_id)));
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

  API_SLOG (con_handle);

  if (IS_STMT_POOL (con_handle))
    {
      if (con_handle->autocommit_mode == CCI_AUTOCOMMIT_TRUE &&
	  con_handle->con_status != CCI_CON_STATUS_OUT_TRAN)
	{
	  T_CCI_ERROR err_buf;
	  qe_end_tran (con_handle, CCI_TRAN_ROLLBACK, &err_buf);
	}

      con_handle->ref_count = 0;
      return err_code;
    }

  if (req_handle->handle_type == HANDLE_PREPARE
      || req_handle->handle_type == HANDLE_SCHEMA_INFO)
    {
      err_code = qe_close_req_handle (req_handle, con_handle);
    }
  hm_req_handle_free (con_handle, req_h_id, req_handle);

  con_handle->ref_count = 0;

  API_ELOG (con_handle, err_code);
  return err_code;
}

int
cci_cursor (int req_h_id, int offset, T_CCI_CURSOR_POS origin,
	    T_CCI_ERROR * err_buf)
{
  T_REQ_HANDLE *req_handle;
  T_CON_HANDLE *con_handle;
  int err_code = CCI_ER_NO_ERROR;

#ifdef CCI_FULL_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("(%d:%d)cci_cursor: %d %s",
				    CON_ID (req_h_id), REQ_ID (req_h_id),
				    offset, dbg_cursor_pos_str (origin)));
#endif

  err_buf_reset (err_buf);

  while (1)
    {
      MUTEX_LOCK (con_handle_table_mutex);

      req_handle = hm_find_req_handle (req_h_id, &con_handle);
      if (req_handle == NULL)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  err_code = CCI_ER_REQ_HANDLE;
	  goto ret;
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

  err_code = qe_cursor (req_handle, con_handle, offset, (char) origin,
			err_buf);

ret:
  if (con_handle != NULL)
    {
      con_handle->ref_count = 0;
    }

  CCI_SET_ERROR_BUFFER (err_code, err_buf);

  return err_code;
}

int
cci_fetch_size (int req_h_id, int fetch_size)
{
  T_REQ_HANDLE *req_handle;
  int err_code = 0;

#ifdef CCI_FULL_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("(%d:%d)cci_fetch_size: %d",
				    CON_ID (req_h_id), REQ_ID (req_h_id),
				    fetch_size));
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
#ifdef CCI_FULL_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("(%d:%d)cci_fetch", CON_ID (req_h_id),
		    REQ_ID (req_h_id)));
#endif

  return (fetch_cmd (req_h_id, 0, err_buf));
}

int
cci_fetch_sensitive (int req_h_id, T_CCI_ERROR * err_buf)
{
#ifdef CCI_FULL_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("(%d:%d)cci_fetch_sensitive", CON_ID (req_h_id),
		    REQ_ID (req_h_id)));
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

#ifdef CCI_FULL_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("(%d:%d)cci_get_data: %d %s",
				    CON_ID (req_h_id), REQ_ID (req_h_id),
				    col_no, dbg_a_type_str (a_type)));
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
cci_schema_info (int con_h_id, T_CCI_SCH_TYPE type, char *arg1,
		 char *arg2, char flag, T_CCI_ERROR * err_buf)
{
  T_CON_HANDLE *con_handle;
  T_REQ_HANDLE *req_handle = NULL;
  int req_handle_id = -1;
  int err_code = CCI_ER_NO_ERROR;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("cci_schema_info %d %s %s %s %d",
				    con_h_id, dbg_sch_type_str (type),
				    DEBUG_STR (arg1), DEBUG_STR (arg2),
				    flag));
#endif

  err_buf_reset (err_buf);

  while (1)
    {
      MUTEX_LOCK (con_handle_table_mutex);

      con_handle = hm_find_con_handle (con_h_id);
      if (con_handle == NULL)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  err_code = CCI_ER_CON_HANDLE;
	  goto ret;
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

  if (err_code < 0)
    {
      goto ret;
    }
  else
    {
      req_handle_id = hm_req_handle_alloc (con_h_id, &req_handle);
      if (req_handle_id >= 0)
	{
	  err_code = qe_schema_info (req_handle, con_handle,
				     (int) type, arg1, arg2, flag, err_buf);
	  if (err_code < 0)
	    {
	      hm_req_handle_free (con_handle, req_handle_id, req_handle);
	    }
	}
    }

ret:
  if (con_handle != NULL)
    {
      con_handle->ref_count = 0;
    }

  if (err_code == CCI_ER_NO_ERROR)
    {
      return req_handle_id;
    }

  CCI_SET_ERROR_BUFFER (err_code, err_buf);

  return err_code;
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
  int err_code = CCI_ER_NO_ERROR;

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
	  err_code = CCI_ER_CON_HANDLE;
	  goto ret;
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
      goto ret;
    }
  else
    {
      req_handle_id = hm_req_handle_alloc (con_h_id, &req_handle);
      if (req_handle_id >= 0)
	{
	  err_code = qe_oid_get (req_handle, con_handle,
				 oid_str, attr_name, err_buf);
	  if (err_code < 0)
	    {
	      hm_req_handle_free (con_handle, req_handle_id, req_handle);
	    }
	}
    }

ret:
  if (con_handle != NULL)
    {
      con_handle->ref_count = 0;
    }

  if (err_code == CCI_ER_NO_ERROR)
    {
      return req_handle_id;
    }

  CCI_SET_ERROR_BUFFER (err_code, err_buf);

  return err_code;
}

int
cci_oid_put (int con_h_id, char *oid_str, char **attr_name, char **new_val,
	     T_CCI_ERROR * err_buf)
{
  T_CON_HANDLE *con_handle;
  int err_code = CCI_ER_NO_ERROR;

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
	  err_code = CCI_ER_CON_HANDLE;
	  goto ret;
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
      err_code = qe_oid_put (con_handle, oid_str, attr_name, new_val,
			     err_buf);
    }

ret:
  if (con_handle != NULL)
    {
      con_handle->ref_count = 0;
    }

  CCI_SET_ERROR_BUFFER (err_code, err_buf);

  return err_code;
}

int
cci_oid_put2 (int con_h_id, char *oid_str, char **attr_name, void **new_val,
	      int *a_type, T_CCI_ERROR * err_buf)
{
  T_CON_HANDLE *con_handle;
  int err_code = CCI_ER_NO_ERROR;

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
	  err_code = CCI_ER_CON_HANDLE;
	  goto ret;
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
      err_code = qe_oid_put2 (con_handle, oid_str, attr_name, new_val, a_type,
			      err_buf);
    }

ret:
  if (con_handle != NULL)
    {
      con_handle->ref_count = 0;
    }

  CCI_SET_ERROR_BUFFER (err_code, err_buf);

  return err_code;
}

int
cci_set_autocommit (int con_h_id, CCI_AUTOCOMMIT_MODE autocommit_mode)
{
  T_CON_HANDLE *con_handle;
  int err_code = 0;
  T_CCI_ERROR tmp_error;

#ifdef CCI_FULL_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("(%d)cci_set_autocommit: %d", con_h_id, autocommit_mode));
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

  if (autocommit_mode != con_handle->autocommit_mode
      && con_handle->con_status != CCI_CON_STATUS_OUT_TRAN)
    {
      err_code = qe_end_tran (con_handle, CCI_TRAN_COMMIT, &tmp_error);
    }

  if (err_code == 0)
    {
      con_handle->autocommit_mode = autocommit_mode;
    }

  con_handle->ref_count = 0;
  return err_code;
}

CCI_AUTOCOMMIT_MODE
cci_get_autocommit (int con_h_id)
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

#ifdef CCI_FULL_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("(%d)cci_get_autocommit: current mode %d", con_h_id,
		    con_handle->autocommit_mode));
#endif

  con_handle->ref_count = 0;

  return con_handle->autocommit_mode;
}

int
cci_set_holdability (int con_h_id, int holdable)
{
  T_CON_HANDLE *con_handle;
  int err_code = 0;

  if (holdable < 0 || holdable > 1)
    {
      return CCI_ER_INVALID_HOLDABILITY;
    }

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("cci_set_con_handle_holdable %d", holdable));
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

  if (err_code == 0)
    {
      hm_set_con_handle_holdable (con_handle, holdable);
    }

  con_handle->ref_count = 0;

  return err_code;
}

int
cci_get_holdability (int con_h_id)
{
  T_CON_HANDLE *con_handle;
  int err_code = 0;
  int holdable = 0;

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

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("cci_get_con_handle_holdable %d",
		    con_handle->is_holdable));
#endif
  holdable = hm_get_con_handle_holdable (con_handle);

  con_handle->ref_count = 0;

  return holdable;
}

int
cci_get_db_version (int con_h_id, char *out_buf, int buf_size)
{
  T_CON_HANDLE *con_handle;
  int err_code = 0;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("(%d)cci_get_db_version", con_h_id));
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

  API_SLOG (con_handle);
  if (IS_OUT_TRAN_STATUS (con_handle))
    {
      err_code = cas_connect (con_handle, NULL);
    }

  if (err_code >= 0)
    {
      err_code = qe_get_db_version (con_handle, out_buf, buf_size);
    }

  con_handle->ref_count = 0;

  API_ELOG (con_handle, err_code);
  return err_code;
}

int
cci_get_class_num_objs (int con_h_id, char *class_name, int flag,
			int *num_objs, int *num_pages, T_CCI_ERROR * err_buf)
{
  T_CON_HANDLE *con_handle;
  int err_code = CCI_ER_NO_ERROR;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("(%d)cci_get_class_num_objs", con_h_id));
#endif

  while (1)
    {
      MUTEX_LOCK (con_handle_table_mutex);

      con_handle = hm_find_con_handle (con_h_id);
      if (con_handle == NULL)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  err_code = CCI_ER_CON_HANDLE;
	  goto ret;
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

ret:
  if (con_handle != NULL)
    {
      con_handle->ref_count = 0;
    }

  CCI_SET_ERROR_BUFFER (err_code, err_buf);

  return err_code;
}

int
cci_oid (int con_h_id, T_CCI_OID_CMD cmd, char *oid_str,
	 T_CCI_ERROR * err_buf)
{
  T_CON_HANDLE *con_handle;
  int err_code = CCI_ER_NO_ERROR;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("(%d)cci_oid: %s", con_h_id, dbg_oid_cmd_str (cmd)));
#endif

  err_buf_reset (err_buf);

  while (1)
    {
      MUTEX_LOCK (con_handle_table_mutex);

      con_handle = hm_find_con_handle (con_h_id);
      if (con_handle == NULL)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  err_code = CCI_ER_CON_HANDLE;
	  goto ret;
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
      err_code = qe_oid_cmd (con_handle, (char) cmd, oid_str, NULL, 0,
			     err_buf);
    }

ret:
  if (con_handle != NULL)
    {
      con_handle->ref_count = 0;
    }

  CCI_SET_ERROR_BUFFER (err_code, err_buf);

  return err_code;
}

int
cci_oid_get_class_name (int con_h_id, char *oid_str, char *out_buf,
			int out_buf_size, T_CCI_ERROR * err_buf)
{
  T_CON_HANDLE *con_handle;
  int err_code = CCI_ER_NO_ERROR;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("(%d)cci_oid_get_class_name", con_h_id));
#endif

  err_buf_reset (err_buf);

  while (1)
    {
      MUTEX_LOCK (con_handle_table_mutex);

      con_handle = hm_find_con_handle (con_h_id);
      if (con_handle == NULL)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  err_code = CCI_ER_CON_HANDLE;
	  goto ret;
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
      err_code = qe_oid_cmd (con_handle, (char) CCI_OID_CLASS_NAME, oid_str,
			     out_buf, out_buf_size, err_buf);
    }

ret:
  if (con_handle != NULL)
    {
      con_handle->ref_count = 0;
    }

  CCI_SET_ERROR_BUFFER (err_code, err_buf);

  return err_code;
}

int
cci_col_get (int con_h_id, char *oid_str, char *col_attr, int *col_size,
	     int *col_type, T_CCI_ERROR * err_buf)
{
  int err_code = CCI_ER_NO_ERROR;
  int req_handle_id = -1;
  T_CON_HANDLE *con_handle = NULL;
  T_REQ_HANDLE *req_handle = NULL;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("(%d)cci_col_get", con_h_id));
#endif

  err_buf_reset (err_buf);

  while (1)
    {
      MUTEX_LOCK (con_handle_table_mutex);

      con_handle = hm_find_con_handle (con_h_id);
      if (con_handle == NULL)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  err_code = CCI_ER_CON_HANDLE;
	  goto error;
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

  if (err_code != CCI_ER_NO_ERROR)
    {
      goto error;
    }

  req_handle_id = hm_req_handle_alloc (con_h_id, &req_handle);
  if (req_handle_id < 0)
    {
      err_code = req_handle_id;
      goto error;
    }
  else
    {
      err_code = qe_col_get (req_handle, con_handle, oid_str,
			     col_attr, col_size, col_type, err_buf);
      if (err_code < 0)
	{
	  hm_req_handle_free (con_handle, req_handle_id, req_handle);
	  goto error;
	}
    }

  con_handle->ref_count = 0;
  return req_handle_id;

error:
  if (con_handle != NULL)
    {
      con_handle->ref_count = 0;
    }

  CCI_SET_ERROR_BUFFER (err_code, err_buf);

  return err_code;
}

int
cci_col_size (int con_h_id, char *oid_str, char *col_attr, int *col_size,
	      T_CCI_ERROR * err_buf)
{
  int err_code = CCI_ER_NO_ERROR;
  T_CON_HANDLE *con_handle = NULL;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("(%d)cci_col_size", con_h_id));
#endif

  err_buf_reset (err_buf);

  while (1)
    {
      MUTEX_LOCK (con_handle_table_mutex);

      con_handle = hm_find_con_handle (con_h_id);
      if (con_handle == NULL)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  err_code = CCI_ER_CON_HANDLE;
	  goto ret;
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
      err_code = qe_col_size (con_handle, oid_str, col_attr, col_size,
			      err_buf);
    }

ret:
  if (con_handle != NULL)
    {
      con_handle->ref_count = 0;
    }

  CCI_SET_ERROR_BUFFER (err_code, err_buf);

  return err_code;
}

int
cci_col_set_drop (int con_h_id, char *oid_str, char *col_attr, char *value,
		  T_CCI_ERROR * err_buf)
{
#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("(%d)cci_col_set_drop", con_h_id));
#endif

  return (col_set_add_drop (con_h_id, CCI_COL_SET_DROP, oid_str, col_attr,
			    value, err_buf));
}

int
cci_col_set_add (int con_h_id, char *oid_str, char *col_attr, char *value,
		 T_CCI_ERROR * err_buf)
{
#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("(%d)cci_col_set_add", con_h_id));
#endif

  return (col_set_add_drop (con_h_id, CCI_COL_SET_ADD, oid_str, col_attr,
			    value, err_buf));
}

int
cci_col_seq_drop (int con_h_id, char *oid_str, char *col_attr, int index,
		  T_CCI_ERROR * err_buf)
{
#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("(%d)cci_coil_seq_drop", con_h_id));
#endif

  return (col_seq_op (con_h_id, CCI_COL_SEQ_DROP, oid_str, col_attr, index,
		      "", err_buf));
}

int
cci_col_seq_insert (int con_h_id, char *oid_str, char *col_attr, int index,
		    char *value, T_CCI_ERROR * err_buf)
{
#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("(%d)cci_col_seq_insert", con_h_id));
#endif

  return (col_seq_op (con_h_id, CCI_COL_SEQ_INSERT, oid_str, col_attr, index,
		      value, err_buf));
}

int
cci_col_seq_put (int con_h_id, char *oid_str, char *col_attr, int index,
		 char *value, T_CCI_ERROR * err_buf)
{
#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("(%d)cci_col_seq_put", con_h_id));
#endif

  return (col_seq_op (con_h_id, CCI_COL_SEQ_PUT, oid_str, col_attr, index,
		      value, err_buf));
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
  int err_code = CCI_ER_NO_ERROR;
  T_REQ_HANDLE *req_handle;
  T_CON_HANDLE *con_handle;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("(%d:%d)cci_cursor_update: %d %d %s %p",
		    CON_ID (req_h_id), REQ_ID (req_h_id), cursor_pos, index,
		    dbg_a_type_str (a_type), value));
#endif

  err_buf_reset (err_buf);

  while (1)
    {
      MUTEX_LOCK (con_handle_table_mutex);

      req_handle = hm_find_req_handle (req_h_id, &con_handle);
      if (req_handle == NULL)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  err_code = CCI_ER_REQ_HANDLE;
	  goto ret;
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

  if ((req_handle->prepare_flag & CCI_PREPARE_UPDATABLE) == 0)
    {
      err_code = CCI_ER_NOT_UPDATABLE;
    }
  else
    {
      err_code = qe_cursor_update (req_handle, con_handle, cursor_pos, index,
				   a_type, value, err_buf);
    }

ret:
  if (con_handle != NULL)
    {
      con_handle->ref_count = 0;
    }

  CCI_SET_ERROR_BUFFER (err_code, err_buf);

  return err_code;
}

int
cci_execute_batch (int con_h_id, int num_query, char **sql_stmt,
		   T_CCI_QUERY_RESULT ** qr, T_CCI_ERROR * err_buf)
{
  T_CON_HANDLE *con_handle = NULL;
  int err_code = CCI_ER_NO_ERROR;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("(%d)cci_execute_batch: %d", con_h_id, num_query));
#endif

  err_buf_reset (err_buf);
  *qr = NULL;

  if (num_query <= 0)
    {
      return CCI_ER_NO_ERROR;
    }

  while (1)
    {
      MUTEX_LOCK (con_handle_table_mutex);

      con_handle = hm_find_con_handle (con_h_id);
      if (con_handle == NULL)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  err_code = CCI_ER_CON_HANDLE;
	  goto ret;
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

  SET_START_TIME_FOR_QUERY (con_handle, NULL);

  if (IS_OUT_TRAN_STATUS (con_handle))
    {
      err_code = cas_connect (con_handle, err_buf);
    }

  if (err_code >= 0)
    {
      err_code = qe_execute_batch (con_handle, num_query, sql_stmt, qr,
				   err_buf);
    }

  if (err_code == CCI_ER_QUERY_TIMEOUT &&
      con_handle->disconnect_on_query_timeout)
    {
      CLOSE_SOCKET (con_handle->sock_fd);
      con_handle->sock_fd = INVALID_SOCKET;
      con_handle->con_status = CCI_CON_STATUS_OUT_TRAN;
    }

  RESET_START_TIME (con_handle);

ret:
  if (con_handle != NULL)
    {
      con_handle->ref_count = 0;
    }

  CCI_SET_ERROR_BUFFER (err_code, err_buf);

  return err_code;
}

int
cci_fetch_buffer_clear (int req_h_id)
{
  T_REQ_HANDLE *req_handle;
  T_CON_HANDLE *con_handle;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("(%d:%d)cci_fetch_buffer_clear", CON_ID (req_h_id),
		    REQ_ID (req_h_id)));
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
  int err_code = CCI_ER_NO_ERROR;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("(%d:%d)cci_execute_result", CON_ID (req_h_id),
		    REQ_ID (req_h_id)));
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
	  err_code = CCI_ER_REQ_HANDLE;
	  goto ret;
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

ret:
  if (con_handle != NULL)
    {
      con_handle->ref_count = 0;
    }

  CCI_SET_ERROR_BUFFER (err_code, err_buf);

  return err_code;
}

int
cci_set_isolation_level (int con_id, T_CCI_TRAN_ISOLATION val,
			 T_CCI_ERROR * err_buf)
{
  return cci_set_db_parameter (con_id, CCI_PARAM_ISOLATION_LEVEL, &val,
			       err_buf);
}

int
cci_set_lock_timeout (int con_id, int val, T_CCI_ERROR * err_buf)
{
  return cci_set_db_parameter (con_id, CCI_PARAM_LOCK_TIMEOUT, &val, err_buf);
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

  if (set != NULL)
    {
      *set = tmp_set;
    }
  return 0;
}

int
cci_get_attr_type_str (int con_h_id, char *class_name, char *attr_name,
		       char *buf, int buf_size, T_CCI_ERROR * err_buf)
{
  T_CON_HANDLE *con_handle;
  int err_code = CCI_ER_NO_ERROR;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("(%d)cci_get_attr_type_str", con_h_id));
#endif

  err_buf_reset (err_buf);

  while (1)
    {
      MUTEX_LOCK (con_handle_table_mutex);

      con_handle = hm_find_con_handle (con_h_id);
      if (con_handle == NULL)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  err_code = CCI_ER_CON_HANDLE;
	  goto ret;
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
      err_code = qe_get_attr_type_str (con_handle, class_name, attr_name, buf,
				       buf_size, err_buf);
    }

ret:
  if (con_handle != NULL)
    {
      con_handle->ref_count = 0;
    }

  CCI_SET_ERROR_BUFFER (err_code, err_buf);

  return err_code;
}

int
cci_get_query_plan (int req_h_id, char **out_buf)
{
#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("(%d:%d)cci_get_query_plan", CON_ID (req_h_id),
		    REQ_ID (req_h_id)));
#endif

  return (get_query_info (req_h_id, CAS_GET_QUERY_INFO_PLAN, out_buf));
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
		   ("(%d:%d)cci_set_max_row: %d", CON_ID (req_h_id),
		    REQ_ID (req_h_id), max_row));
#endif

  MUTEX_LOCK (con_handle_table_mutex);

  req_handle = hm_find_req_handle (req_h_id, NULL);
  if (req_handle == NULL)
    {
      err_code = CCI_ER_REQ_HANDLE;
    }
  else
    {
      req_handle->max_row = max_row;
    }

  MUTEX_UNLOCK (con_handle_table_mutex);

  return err_code;
}

int
cci_savepoint (int con_h_id, T_CCI_SAVEPOINT_CMD cmd, char *savepoint_name,
	       T_CCI_ERROR * err_buf)
{
  T_CON_HANDLE *con_handle;
  int err_code = CCI_ER_NO_ERROR;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("(%d)cci_savepoint", con_h_id));
#endif

  err_buf_reset (err_buf);

  while (1)
    {
      MUTEX_LOCK (con_handle_table_mutex);

      con_handle = hm_find_con_handle (con_h_id);
      if (con_handle == NULL)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  err_code = CCI_ER_CON_HANDLE;
	  goto ret;
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
      err_code = qe_savepoint_cmd (con_handle, (char) cmd, savepoint_name,
				   err_buf);
    }

ret:
  if (con_handle != NULL)
    {
      con_handle->ref_count = 0;
    }

  CCI_SET_ERROR_BUFFER (err_code, err_buf);

  return err_code;
}

int
cci_get_param_info (int req_h_id, T_CCI_PARAM_INFO ** param,
		    T_CCI_ERROR * err_buf)
{
  T_REQ_HANDLE *req_handle;
  T_CON_HANDLE *con_handle;
  int err_code = CCI_ER_NO_ERROR;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("(%d:%d)cci_get_param_infod", CON_ID (req_h_id),
		    REQ_ID (req_h_id)));
#endif

  err_buf_reset (err_buf);

  while (1)
    {
      MUTEX_LOCK (con_handle_table_mutex);

      req_handle = hm_find_req_handle (req_h_id, &con_handle);
      if (req_handle == NULL)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  err_code = CCI_ER_REQ_HANDLE;
	  goto ret;
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
    {
      *param = NULL;
    }

  err_code = qe_get_param_info (req_handle, con_handle, param, err_buf);

ret:
  if (con_handle != NULL)
    {
      con_handle->ref_count = 0;
    }

  CCI_SET_ERROR_BUFFER (err_code, err_buf);

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
cci_set_query_timeout (int req_h_id, int timeout)
{
  T_REQ_HANDLE *req_handle;
  int old_value;

  if (timeout < 0)
    {
      timeout = 0;
    }
#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("(%d:%d)cci_set_query_timeout: %d", CON_ID (req_h_id),
		    REQ_ID (req_h_id), timeout));
#endif

  MUTEX_LOCK (con_handle_table_mutex);
  req_handle = hm_find_req_handle (req_h_id, NULL);
  if (req_handle == NULL)
    {
      MUTEX_UNLOCK (con_handle_table_mutex);
      return CCI_ER_REQ_HANDLE;
    }
  MUTEX_UNLOCK (con_handle_table_mutex);

  old_value = req_handle->query_timeout;
  req_handle->query_timeout = timeout;

  return old_value;
}

int
cci_get_query_timeout (int req_h_id)
{
  T_REQ_HANDLE *req_handle;
#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("(%d:%d)cci_get_query_timeout", CON_ID (req_h_id),
		    REQ_ID (req_h_id)));
#endif
  MUTEX_LOCK (con_handle_table_mutex);
  req_handle = hm_find_req_handle (req_h_id, NULL);
  if (req_handle == NULL)
    {
      MUTEX_UNLOCK (con_handle_table_mutex);
      return CCI_ER_REQ_HANDLE;
    }
  MUTEX_UNLOCK (con_handle_table_mutex);

  return req_handle->query_timeout;
}

static int
cci_lob_new (int con_h_id, void *lob, T_CCI_U_TYPE type,
	     T_CCI_ERROR * err_buf)
{
  T_CON_HANDLE *con_handle;
  int err_code = CCI_ER_NO_ERROR;
  T_LOB *lob_handle = NULL;

  if (lob == NULL)
    {
      err_code = CCI_ER_INVALID_LOB_HANDLE;
      goto ret;
    }

  err_buf_reset (err_buf);

  while (1)
    {
      MUTEX_LOCK (con_handle_table_mutex);

      con_handle = hm_find_con_handle (con_h_id);
      if (con_handle == NULL)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  err_code = CCI_ER_CON_HANDLE;
	  goto ret;
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
      err_code = qe_lob_new (con_handle, &lob_handle, type, err_buf);
      if (err_code >= 0)
	{
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
	}
    }

ret:
  if (con_handle != NULL)
    {
      con_handle->ref_count = 0;
    }

  CCI_SET_ERROR_BUFFER (err_code, err_buf);

  return err_code;
}

int
cci_blob_new (int con_h_id, T_CCI_BLOB * blob, T_CCI_ERROR * err_buf)
{
#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("(%d)cci_blob_new", con_h_id));
#endif
  return cci_lob_new (con_h_id, (void *) blob, CCI_U_TYPE_BLOB, err_buf);
}

int
cci_clob_new (int con_h_id, T_CCI_CLOB * clob, T_CCI_ERROR * err_buf)
{
#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("(%d)cci_clob_new", con_h_id));
#endif
  return cci_lob_new (con_h_id, (void *) clob, CCI_U_TYPE_CLOB, err_buf);
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
cci_lob_write (int con_h_id, void *lob, long long start_pos,
	       int length, const char *buf, T_CCI_ERROR * err_buf)
{
  T_CON_HANDLE *con_handle;
  int err_code = CCI_ER_NO_ERROR;
  T_LOB *lob_handle = (T_LOB *) lob;
  int nwritten = 0;
  int current_write_len;

  if (lob == NULL)
    {
      err_code = CCI_ER_INVALID_LOB_HANDLE;
      goto ret;
    }

  err_buf_reset (err_buf);

  while (1)
    {
      MUTEX_LOCK (con_handle_table_mutex);

      con_handle = hm_find_con_handle (con_h_id);
      if (con_handle == NULL)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  err_code = CCI_ER_CON_HANDLE;
	  goto ret;
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
      while (nwritten < length)
	{
	  current_write_len = ((LOB_IO_LENGTH > length - nwritten)
			       ? (length - nwritten) : LOB_IO_LENGTH);
	  err_code = qe_lob_write (con_handle, lob_handle,
				   start_pos + nwritten, current_write_len,
				   buf + nwritten, err_buf);
	  if (err_code >= 0)
	    {
	      nwritten += err_code;
	    }
	  else
	    {
	      break;
	    }
	}
    }

ret:
  if (con_handle != NULL)
    {
      con_handle->ref_count = 0;
    }

  if (err_code >= 0)
    {
      return nwritten;
    }

  CCI_SET_ERROR_BUFFER (err_code, err_buf);

  return err_code;
}

int
cci_blob_write (int con_h_id, T_CCI_BLOB blob, long long start_pos,
		int length, const char *buf, T_CCI_ERROR * err_buf)
{
#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("(%d)cci_blob_write: lob %p pos %d len %d", con_h_id,
		    blob, start_pos, length));
#endif
  return cci_lob_write (con_h_id, blob, start_pos, length, buf, err_buf);
}

int
cci_clob_write (int con_h_id, T_CCI_CLOB clob, long long start_pos,
		int length, const char *buf, T_CCI_ERROR * err_buf)
{
#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("(%d)cci_clob_write: lob %p pos %d len %d", con_h_id,
		    clob, start_pos, length));
#endif
  return cci_lob_write (con_h_id, clob, start_pos, length, buf, err_buf);
}

static int
cci_lob_read (int con_h_id, void *lob, long long start_pos,
	      int length, char *buf, T_CCI_ERROR * err_buf)
{
  T_CON_HANDLE *con_handle;
  int err_code = CCI_ER_NO_ERROR;
  T_LOB *lob_handle = (T_LOB *) lob;
  int nread = 0;
  int current_read_len;
  INT64 lob_size;

  if (lob == NULL)
    {
      err_code = CCI_ER_INVALID_LOB_HANDLE;
      goto ret;
    }

  err_buf_reset (err_buf);

  while (1)
    {
      MUTEX_LOCK (con_handle_table_mutex);

      con_handle = hm_find_con_handle (con_h_id);
      if (con_handle == NULL)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  err_code = CCI_ER_CON_HANDLE;
	  goto ret;
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

  lob_size = t_lob_get_size (lob_handle->handle);

  if (start_pos >= lob_size)
    {
      err_code = CCI_ER_INVALID_LOB_READ_POS;
    }

  if (err_code >= 0)
    {
      while (nread < length && start_pos + nread < lob_size)
	{
	  current_read_len = ((LOB_IO_LENGTH > length - nread)
			      ? (length - nread) : LOB_IO_LENGTH);
	  err_code = qe_lob_read (con_handle, lob_handle, start_pos + nread,
				  current_read_len, buf + nread, err_buf);
	  if (err_code >= 0)
	    {
	      nread += err_code;
	    }
	  else
	    {
	      break;
	    }
	}
    }

ret:
  if (con_handle != NULL)
    {
      con_handle->ref_count = 0;
    }

  if (err_code >= 0)
    {
      return nread;
    }

  CCI_SET_ERROR_BUFFER (err_code, err_buf);

  return err_code;
}


int
cci_blob_read (int con_h_id, T_CCI_BLOB blob, long long start_pos,
	       int length, char *buf, T_CCI_ERROR * err_buf)
{
#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("(%d)cci_blob_read: lob %p pos %d len %d",
				    con_h_id, blob, start_pos, length));
#endif
  return cci_lob_read (con_h_id, blob, start_pos, length, buf, err_buf);
}


int
cci_clob_read (int con_h_id, T_CCI_CLOB clob, long long start_pos,
	       int length, char *buf, T_CCI_ERROR * err_buf)
{
#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("(%d)cci_clob_read: lob %p pos %d len %d",
				    con_h_id, clob, start_pos, length));
#endif
  return cci_lob_read (con_h_id, clob, start_pos, length, buf, err_buf);
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
cci_xa_prepare (int con_id, XID * xid, T_CCI_ERROR * err_buf)
{
  int err_code = 0;
  T_CON_HANDLE *con_handle;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("(%d)cci_xa_prepare", con_id));
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
      err_code = qe_xa_prepare (con_handle, xid, err_buf);
    }

  con_handle->ref_count = 0;

  return err_code;
}

int
cci_xa_recover (int con_id, XID * xid, int num_xid, T_CCI_ERROR * err_buf)
{
  int err_code = 0;
  T_CON_HANDLE *con_handle;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("(%d)cci_xa_recover", con_id));
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
  return err_code;
}

int
cci_xa_end_tran (int con_id, XID * xid, char type, T_CCI_ERROR * err_buf)
{
  int err_code = 0;
  T_CON_HANDLE *con_handle;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("(%d)cci_xa_end_tran", con_id));
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
      err_code = qe_xa_end_tran (con_handle, xid, type, err_buf);
    }

  con_handle->ref_count = 0;
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
      dbms_type = con_handle->broker_info[BROKER_INFO_DBMS_TYPE];
    }

  MUTEX_UNLOCK (con_handle_table_mutex);

  return dbms_type;
}

int
cci_set_charset (int con_h_id, char *charset)
{
  T_CON_HANDLE *con_handle;
  int err_code;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg
		   ("(%d)cci_set_charset: %s", con_h_id, charset));
#endif

#if defined(WINDOWS)
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

  err_code = qe_set_charset (con_handle, charset);
  con_handle->ref_count = 0;

  return err_code;
#else
  return 0;
#endif
}

int
cci_row_count (int con_h_id, int *row_count, T_CCI_ERROR * err_buf)
{
  int err_code = CCI_ER_NO_ERROR;
  int req_handle_id = -1;
  T_CON_HANDLE *con_handle = NULL;
  T_REQ_HANDLE *req_handle = NULL;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("(%d)cci_row_count", con_h_id));
#endif

  err_buf_reset (err_buf);
  while (1)
    {
      MUTEX_LOCK (con_handle_table_mutex);

      con_handle = hm_find_con_handle (con_h_id);
      if (con_handle == NULL)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  err_code = CCI_ER_CON_HANDLE;
	  goto ret;
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
	{
	  err_code = req_handle_id;
	}
      else
	{
	  err_code = qe_get_row_count (req_handle, con_handle,
				       row_count, err_buf);
	  hm_req_handle_free (con_handle, req_handle_id, req_handle);
	}
    }

ret:
  if (con_handle != NULL)
    {
      con_handle->ref_count = 0;
    }

  CCI_SET_ERROR_BUFFER (err_code, err_buf);

  return err_code;
}

int
cci_last_insert_id (int con_h_id, void *value, T_CCI_ERROR * err_buf)
{
  int err_code = CCI_ER_NO_ERROR;
  int req_handle_id = -1;
  T_CON_HANDLE *con_handle = NULL;
  T_REQ_HANDLE *req_handle = NULL;

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("(%d)cci_last_insert_id", con_h_id));
#endif

  err_buf_reset (err_buf);
  while (1)
    {
      MUTEX_LOCK (con_handle_table_mutex);

      con_handle = hm_find_con_handle (con_h_id);
      if (con_handle == NULL)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  err_code = CCI_ER_CON_HANDLE;
	  goto ret;
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
	{
	  err_code = req_handle_id;
	}
      else
	{
	  err_code = qe_get_last_insert_id (req_handle, con_handle, value,
					    err_buf);
	  hm_req_handle_free (con_handle, req_handle_id, req_handle);
	}
    }

ret:
  if (con_handle != NULL)
    {
      con_handle->ref_count = 0;
    }

  CCI_SET_ERROR_BUFFER (err_code, err_buf);

  return err_code;
}

static const char *
cci_get_err_msg_internal (int err_code)
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

    case CCI_ER_RESULT_SET_CLOSED:
      return "Result set is closed";

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

    case CCI_ER_INVALID_LOB_HANDLE:
      return "Invalid lob handle";

    case CCI_ER_INVALID_LOB_READ_POS:
      return "Invalid lob read position";

    case CCI_ER_INVALID_HOLDABILITY:
      return "Invalid holdability mode. The only accepted values are 0 or 1";

    case CCI_ER_NOT_UPDATABLE:
      return "Request handle is not updatable";

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

    case CAS_ER_HOLDABLE_NOT_ALLOWED_KEEP_CON_OFF:
      return "Holdable results are not allowed while KEEP_CONNECTION is off";

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

  err_msg = cci_get_err_msg_internal (err_code);
  if (err_msg == NULL)
    {
      return -1;
    }
  else
    {
      if ((err_code < CCI_ER_DBMS) && (err_code > CCI_ER_END))
	{
	  snprintf (buf, bufsize, "CCI Error : %s", err_msg);
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

  err_msg = cci_get_err_msg_internal (err_code);
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
  if (err_buf)
    {
      err_buf->err_code = 0;
      err_buf->err_msg[0] = '\0';
    }
}

static int
col_set_add_drop (int con_h_id, char col_cmd, char *oid_str, char *col_attr,
		  char *value, T_CCI_ERROR * err_buf)
{
  int err_code = CCI_ER_NO_ERROR;
  T_CON_HANDLE *con_handle = NULL;

  err_buf_reset (err_buf);

  while (1)
    {
      MUTEX_LOCK (con_handle_table_mutex);

      con_handle = hm_find_con_handle (con_h_id);
      if (con_handle == NULL)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  err_code = CCI_ER_CON_HANDLE;
	  goto ret;
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
      err_code = qe_col_set_add_drop (con_handle, col_cmd, oid_str, col_attr,
				      value, err_buf);
    }

ret:
  if (con_handle != NULL)
    {
      con_handle->ref_count = 0;
    }

  CCI_SET_ERROR_BUFFER (err_code, err_buf);

  return err_code;
}

static int
col_seq_op (int con_h_id, char col_cmd, char *oid_str, char *col_attr,
	    int index, const char *value, T_CCI_ERROR * err_buf)
{
  int err_code = CCI_ER_NO_ERROR;
  T_CON_HANDLE *con_handle = NULL;

  err_buf_reset (err_buf);

  while (1)
    {
      MUTEX_LOCK (con_handle_table_mutex);

      con_handle = hm_find_con_handle (con_h_id);
      if (con_handle == NULL)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  err_code = CCI_ER_CON_HANDLE;
	  goto ret;
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
      err_code = qe_col_seq_op (con_handle, col_cmd, oid_str, col_attr, index,
				value, err_buf);
    }

ret:
  if (con_handle != NULL)
    {
      con_handle->ref_count = 0;
    }

  CCI_SET_ERROR_BUFFER (err_code, err_buf);

  return err_code;
}

static int
fetch_cmd (int req_h_id, char flag, T_CCI_ERROR * err_buf)
{
  T_REQ_HANDLE *req_handle;
  T_CON_HANDLE *con_handle;
  int err_code = CCI_ER_NO_ERROR;
  int result_set_index = 0;

  err_buf_reset (err_buf);

  while (1)
    {
      MUTEX_LOCK (con_handle_table_mutex);

      req_handle = hm_find_req_handle (req_h_id, &con_handle);
      if (req_handle == NULL)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  err_code = CCI_ER_REQ_HANDLE;
	  goto ret;
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

  if ((req_handle->prepare_flag & CCI_PREPARE_HOLDABLE) != 0
      && (flag & CCI_FETCH_SENSITIVE) != 0)
    {
      err_code = CAS_ER_HOLDABLE_NOT_ALLOWED;
    }

  if (err_code >= 0)
    {
      err_code = qe_fetch (req_handle, con_handle, flag, result_set_index,
			   err_buf);
    }

ret:
  if (con_handle != NULL)
    {
      con_handle->ref_count = 0;
    }

  CCI_SET_ERROR_BUFFER (err_code, err_buf);

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

#ifdef CCI_DEBUG
  CCI_DEBUG_PRINT (print_debug_msg ("(%d)cas_connect_with_ret",
				    con_handle->id));
#endif
  err_code = cas_connect_internal (con_handle, err_buf, connect);

  /* req_handle_table should be managed by list too. */
  if (((*connect) != 0) && IS_BROKER_STMT_POOL (con_handle))
    {
      hm_invalidate_all_req_handle (con_handle);
    }
  if ((*connect) != 0)
    {
      con_handle->no_backslash_escapes = CCI_NO_BACKSLASH_ESCAPES_NOT_SET;
    }

  return err_code;
}

static int
cas_connect_internal (T_CON_HANDLE * con_handle, T_CCI_ERROR * err_buf,
		      int *connect)
{
  int error;
  int i;
  int remained_time = 0;

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

  /* first, try to connect to a last connected host */
  error = net_connect_srv (con_handle, con_handle->alter_host_id, err_buf,
			   remained_time);
  if (error == CCI_ER_NO_ERROR)
    {
      *connect = 1;
      return CCI_ER_NO_ERROR;
    }

  /* second, try to connect all hosts */
  for (i = 0; i < con_handle->alter_host_count; i++)
    {
      error = net_connect_srv (con_handle, i, err_buf, remained_time);
      if (error == CCI_ER_NO_ERROR)
	{
	  *connect = 1;
	  return CCI_ER_NO_ERROR;
	}
    }

  if (error == CCI_ER_QUERY_TIMEOUT)
    {
      error = CCI_ER_LOGIN_TIMEOUT;
    }

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

  err_code = qe_get_query_info (req_handle, con_handle, log_type, out_buf);

  con_handle->ref_count = 0;

  return err_code;
}

static int
next_result_cmd (int req_h_id, char flag, T_CCI_ERROR * err_buf)
{
  T_REQ_HANDLE *req_handle;
  T_CON_HANDLE *con_handle;
  int err_code = CCI_ER_NO_ERROR;

  err_buf_reset (err_buf);

  while (1)
    {
      MUTEX_LOCK (con_handle_table_mutex);

      req_handle = hm_find_req_handle (req_h_id, &con_handle);
      if (req_handle == NULL)
	{
	  MUTEX_UNLOCK (con_handle_table_mutex);
	  err_code = CCI_ER_REQ_HANDLE;
	  goto ret;
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

  err_code = qe_next_result (req_handle, flag, con_handle, err_buf);

ret:
  if (con_handle != NULL)
    {
      con_handle->ref_count = 0;
    }

  CCI_SET_ERROR_BUFFER (err_code, err_buf);

  return err_code;
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

  /* Do not support timeout feature in thread execute. */
  RESET_START_TIME (curr_con_handle);

  exec_arg->ret_code = qe_execute (exec_arg->req_handle, curr_con_handle,
				   exec_arg->flag,
				   exec_arg->max_col_size,
				   &(exec_arg->err_buf));
  if (exec_arg->ret_code < 0)
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
					   curr_con_handle,
					   (exec_arg->req_handle)->sql_text,
					   (exec_arg->req_handle)->
					   prepare_flag, &(exec_arg->err_buf),
					   1);
	  if (exec_arg->ret_code < 0)
	    {
	      goto execute_end;
	    }
	  exec_arg->ret_code = qe_execute (exec_arg->req_handle,
					   curr_con_handle,
					   exec_arg->flag,
					   exec_arg->max_col_size,
					   &(exec_arg->err_buf));
	}
    }
  if (IS_BROKER_STMT_POOL (curr_con_handle))
    {
      while (exec_arg->ret_code == CAS_ER_STMT_POOLING)
	{
	  req_handle_content_free (exec_arg->req_handle, 1);
	  exec_arg->ret_code = qe_prepare (exec_arg->req_handle,
					   curr_con_handle,
					   exec_arg->req_handle->sql_text,
					   exec_arg->req_handle->prepare_flag,
					   &(exec_arg->err_buf), 1);
	  if (exec_arg->ret_code < 0)
	    {
	      goto execute_end;
	    }
	  exec_arg->ret_code = qe_execute (exec_arg->req_handle,
					   curr_con_handle,
					   exec_arg->flag,
					   exec_arg->max_col_size,
					   &(exec_arg->err_buf));
	}
    }

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
  assert (err_buf != NULL);

  if (con_handle->ref_count > 0)
    {
      return CCI_ER_THREAD_RUNNING;
    }

  *err_buf = con_handle->thr_arg.err_buf;

  return con_handle->thr_arg.ret_code;
}

static int
connect_prepare_again (T_CON_HANDLE * con_handle, T_REQ_HANDLE * req_handle,
		       T_CCI_ERROR * err_buf)
{
  int err_code = 0;

  if (req_handle->valid)
    {
      return err_code;
    }

  req_handle_content_free (req_handle, 1);
  err_code = qe_prepare (req_handle, con_handle, req_handle->sql_text,
			 req_handle->prepare_flag, err_buf, 1);

  if ((IS_OUT_TRAN (con_handle) && IS_ER_COMMUNICATION (err_code))
      || IS_SERVER_DOWN (err_buf->err_code))
    {
      int connect_done;

      err_code = cas_connect_with_ret (con_handle, err_buf, &connect_done);
      if (err_code < 0)
	{
	  return err_code;
	}
      if (connect_done)
	{
	  err_code = qe_prepare (req_handle, con_handle, req_handle->sql_text,
				 req_handle->prepare_flag, err_buf, 1);
	}
    }
  return err_code;
}

static int
cci_get_new_handle_id (char *ip, int port, char *db_name,
		       char *db_user, char *dbpasswd)
{
  int con_handle_id;
  unsigned char ip_addr[4];

  (void) hm_ip_str_to_addr (ip, ip_addr);

  MUTEX_LOCK (con_handle_table_mutex);

  if (init_flag == 0)
    {
      hm_con_handle_table_init ();
      init_flag = 1;
    }

  con_handle_id = hm_get_con_from_pool (ip_addr, port, db_name, db_user,
					dbpasswd);
  if (con_handle_id < 0)
    {
      con_handle_id = hm_con_handle_alloc (ip, port, db_name, db_user,
					   dbpasswd);
    }

  MUTEX_UNLOCK (con_handle_table_mutex);

#if !defined(WINDOWS)
  if (!cci_SIGPIPE_ignore)
    {
      signal (SIGPIPE, SIG_IGN);
      cci_SIGPIPE_ignore = 1;
    }
#endif

  return con_handle_id;
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
		      int *out_value, int default_value,
		      T_CCI_ERROR * err_buf)
{
  char *tmp;
  long val;

  assert (out_value != NULL);

  tmp = cci_property_get (prop, datasource_key[key]);
  if (tmp == NULL)
    {
      err_buf->err_code = CCI_ER_NO_PROPERTY;
      *out_value = default_value;
    }
  else
    {
      if (cci_strtol (&val, tmp, 10))
	{
	  err_buf->err_code = CCI_ER_NO_ERROR;
	  *out_value = (int) val;
	}
      else
	{
	  err_buf->err_code = CCI_ER_PROPERTY_TYPE;
	  snprintf (err_buf->err_msg, 1023, "strtol: %s", strerror (errno));
	  return false;
	}
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

  tmp = cci_property_get (prop, datasource_key[key]);
  if (tmp == NULL)
    {
      err_buf->err_code = CCI_ER_NO_PROPERTY;
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
	  err_buf->err_code = CCI_ER_PROPERTY_TYPE;
	  snprintf (err_buf->err_msg, 1023, "boolean parsing : %s", tmp);
	  return false;
	}
      err_buf->err_code = CCI_ER_NO_ERROR;
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
  if (strcasecmp (isolation, "TRAN_COMMIT_CLASS_UNCOMMIT_INSTANCE") == 0)
    {
      return TRAN_COMMIT_CLASS_UNCOMMIT_INSTANCE;
    }
  else if (strcasecmp (isolation, "TRAN_COMMIT_CLASS_COMMIT_INSTANCE") == 0)
    {
      return TRAN_COMMIT_CLASS_COMMIT_INSTANCE;
    }
  else if (strcasecmp (isolation, "TRAN_REP_CLASS_UNCOMMIT_INSTANCE") == 0)
    {
      return TRAN_REP_CLASS_UNCOMMIT_INSTANCE;
    }
  else if (strcasecmp (isolation, "TRAN_REP_CLASS_COMMIT_INSTANCE") == 0)
    {
      return TRAN_REP_CLASS_COMMIT_INSTANCE;
    }
  else if (strcasecmp (isolation, "TRAN_REP_CLASS_REP_INSTANCE") == 0)
    {
      return TRAN_REP_CLASS_REP_INSTANCE;
    }
  else if (strcasecmp (isolation, "TRAN_SERIALIZABLE") == 0)
    {
      return TRAN_SERIALIZABLE;
    }
  return TRAN_ISOLATION_MAX + 1;
}

static bool
cci_property_get_isolation (T_CCI_PROPERTIES * prop, T_CCI_DATASOURCE_KEY key,
			    T_CCI_TRAN_ISOLATION * out_value,
			    int default_value, T_CCI_ERROR * err_buf)
{
  char *tmp;

  assert (out_value != NULL);

  tmp = cci_property_get (prop, datasource_key[key]);
  if (tmp == NULL)
    {
      err_buf->err_code = CCI_ER_NO_PROPERTY;
      *out_value = default_value;
    }
  else
    {
      T_CCI_TRAN_ISOLATION i = cci_property_conv_isolation (tmp);
      if (i > TRAN_ISOLATION_MAX)
	{
	  err_buf->err_code = CCI_ER_PROPERTY_TYPE;
	  snprintf (err_buf->err_msg, 1023, "isolation parsing : %s", tmp);
	  return false;
	}
      else
	{
	  *out_value = (T_CCI_TRAN_ISOLATION) i;
	}
      err_buf->err_code = CCI_ER_NO_ERROR;
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
	  err_buf->err_code = CCI_ER_NO_MORE_MEMORY;
	  if (err_buf->err_msg)
	    {
	      snprintf (err_buf->err_msg, 1023, "strdup: %s",
			strerror (errno));
	    }
	  return false;
	}
    }
  else
    {
      err_buf->err_code = CCI_ER_NO_PROPERTY;
      if (err_buf->err_msg)
	{
	  snprintf (err_buf->err_msg, 1023,
		    "Could not found user property for connection");
	}
      return false;
    }

  return true;
}

static void
cci_disconnect_force (int con_h_id, bool try_close)
{
  T_CON_HANDLE *con_handle;

  if (con_h_id <= 0)
    {
      return;
    }

  con_handle = hm_find_con_handle (con_h_id);
  if (con_handle == NULL)
    {
      return;
    }

  API_SLOG (con_handle);
  if (try_close)
    {
      qe_con_close (con_handle);
    }

  API_ELOG (con_handle, 0);
  hm_req_handle_free_all (con_handle);
  if (!IS_INVALID_SOCKET (con_handle->sock_fd))
    {
      CLOSE_SOCKET (con_handle->sock_fd);
      con_handle->sock_fd = INVALID_SOCKET;
    }
  hm_con_handle_free (con_h_id);
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

  if (!new_url || !url)
    {
      return false;
    }

  strncpy (new_url, url, LINE_MAX);
  if (strlen (url) >= LINE_MAX)
    {
      new_url[LINE_MAX] = '\0';
      err_buf->err_code = CCI_ER_NO_MORE_MEMORY;
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
			     login_timeout, err_buf))
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
	  err_buf->err_code = CCI_ER_NO_MORE_MEMORY;
	  return false;
	}
      strncat (new_url, append_str, rlen);
      delim = '&';
    }

  if (!cci_property_get_int (prop, CCI_DS_KEY_QUERY_TIMEOUT, &query_timeout,
			     query_timeout, err_buf))
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
	  err_buf->err_code = CCI_ER_NO_MORE_MEMORY;
	  return false;
	}
      strncat (new_url, append_str, rlen);
      delim = '&';
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
	  err_buf->err_code = CCI_ER_NO_MORE_MEMORY;
	  return false;
	}
      strncat (new_url, append_str, rlen);
      delim = '&';
    }

  err_buf->err_code = CCI_ER_NO_ERROR;
  return true;
}

T_CCI_DATASOURCE *
cci_datasource_create (T_CCI_PROPERTIES * prop, T_CCI_ERROR * err_buf)
{
  T_CCI_DATASOURCE *ds = NULL;
  int i;

  ds = MALLOC (sizeof (T_CCI_DATASOURCE));
  if (ds == NULL)
    {
      err_buf->err_code = CCI_ER_NO_MORE_MEMORY;
      if (err_buf->err_msg)
	{
	  snprintf (err_buf->err_msg, 1023, "memory allocation error: %s",
		    strerror (errno));
	}
      return NULL;
    }
  memset (ds, 0x0, sizeof (T_CCI_DATASOURCE));

  ds->user = cci_property_get (prop, datasource_key[CCI_DS_KEY_USER]);
  if (!cci_check_property (&ds->user, err_buf))
    {
      goto create_datasource_error;
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
      if (!cci_check_property (&ds->pass, err_buf))
	{
	  goto create_datasource_error;
	}
    }

  ds->url = cci_property_get (prop, datasource_key[CCI_DS_KEY_URL]);
  if (!cci_check_property (&ds->url, err_buf))
    {
      goto create_datasource_error;
    }

  if (!cci_property_get_int (prop, CCI_DS_KEY_POOL_SIZE, &ds->pool_size,
			     CCI_DS_POOL_SIZE_DEFAULT, err_buf))
    {
      goto create_datasource_error;
    }

  if (!cci_property_get_int (prop, CCI_DS_KEY_MAX_WAIT, &ds->max_wait,
			     CCI_DS_MAX_WAIT_DEFAULT, err_buf))
    {
      goto create_datasource_error;
    }

  if (!cci_property_get_bool (prop, CCI_DS_KEY_POOL_PREPARED_STATEMENT,
			      &ds->pool_prepared_statement,
			      CCI_DS_POOL_PREPARED_STATEMENT_DEFAULT,
			      err_buf))
    {
      goto create_datasource_error;
    }

  if (!cci_property_get_bool_internal (prop, CCI_DS_KEY_DEFAULT_AUTOCOMMIT,
				       &ds->default_autocommit,
				       CCI_DS_DEFAULT_AUTOCOMMIT_DEFAULT,
				       err_buf))
    {
      goto create_datasource_error;
    }

  if (!cci_property_get_isolation (prop, CCI_DS_KEY_DEFAULT_ISOLATION,
				   &ds->default_isolation,
				   CCI_DS_DEFAULT_ISOLATION_DEFAULT, err_buf))
    {
      goto create_datasource_error;
    }

  if (!cci_property_get_int (prop, CCI_DS_KEY_DEFAULT_LOCK_TIMEOUT,
			     &ds->default_lock_timeout,
			     CCI_DS_DEFAULT_LOCK_TIMEOUT_DEFAULT, err_buf))
    {
      goto create_datasource_error;
    }

  ds->con_handles = CALLOC (ds->pool_size, sizeof (T_CCI_CONN));
  if (ds->con_handles == NULL)
    {
      err_buf->err_code = CCI_ER_NO_MORE_MEMORY;
      if (err_buf->err_msg)
	{
	  snprintf (err_buf->err_msg, 1023, "memory allocation error: %s",
		    strerror (errno));
	}
      goto create_datasource_error;
    }

  ds->mutex = MALLOC (sizeof (pthread_mutex_t));
  if (ds->mutex == NULL)
    {
      err_buf->err_code = CCI_ER_NO_MORE_MEMORY;
      if (err_buf->err_msg)
	{
	  snprintf (err_buf->err_msg, 1023, "memory allocation error: %s",
		    strerror (errno));
	}
      goto create_datasource_error;
    }
  pthread_mutex_init ((pthread_mutex_t *) ds->mutex, NULL);

  ds->cond = MALLOC (sizeof (pthread_cond_t));
  if (ds->cond == NULL)
    {
      err_buf->err_code = CCI_ER_NO_MORE_MEMORY;
      if (err_buf->err_msg)
	{
	  snprintf (err_buf->err_msg, 1023, "memory allocation error: %s",
		    strerror (errno));
	}
      goto create_datasource_error;
    }
  pthread_cond_init ((pthread_cond_t *) ds->cond, NULL);

  ds->num_idle = ds->pool_size;
  for (i = 0; i < ds->pool_size; i++)
    {
      T_CON_HANDLE *handle;
      T_CCI_CONN id;
      char new_url[LINE_MAX];

      if (!cci_datasource_make_url (prop, new_url, ds->url, err_buf))
	{
	  goto create_datasource_error;
	}
      id = cci_connect_with_url (new_url, ds->user, ds->pass);

      if (id < 0)
	{
	  err_buf->err_code = CCI_ER_CONNECT;
	  if (err_buf->err_msg)
	    {
	      snprintf (err_buf->err_msg, 1023,
			"Could not connect to database");
	    }
	  goto create_datasource_error;
	}

      handle = hm_find_con_handle (id);
      handle->datasource = ds;
      ds->con_handles[i] = id;
    }

  ds->is_init = 1;
  return ds;

create_datasource_error:
  if (ds->con_handles != NULL)
    {
      for (i = 0; i < ds->pool_size; i++)
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

  FREE_MEM (ds->user);
  FREE_MEM (ds->pass);
  FREE_MEM (ds->url);

  pthread_mutex_destroy ((pthread_mutex_t *) ds->mutex);
  FREE_MEM (ds->mutex);
  pthread_cond_destroy ((pthread_cond_t *) ds->cond);
  FREE_MEM (ds->cond);

  if (ds->con_handles)
    {
      for (i = 0; i < ds->pool_size; i++)
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

  FREE_MEM (ds);
}

T_CCI_CONN
cci_datasource_borrow (T_CCI_DATASOURCE * ds, T_CCI_ERROR * err_buf)
{
  T_CCI_CONN id = -1;
  int i;

  if (ds == NULL || !ds->is_init)
    {
      err_buf->err_code = CCI_ER_INVALID_DATASOURCE;
      snprintf (err_buf->err_msg, 1023, "CCI data source is invalid");
      return CCI_ER_INVALID_DATASOURCE;
    }

  /* critical section begin */
  pthread_mutex_lock ((pthread_mutex_t *) ds->mutex);
  if (ds->num_idle == 0)
    {
      /* wait max_wait msecs */
      struct timespec ts;
      struct timeval tv;
      int r;

      gettimeofday (&tv, NULL);
      ts.tv_sec = tv.tv_sec + (ds->max_wait / 1000);
      ts.tv_nsec = (tv.tv_usec + (ds->max_wait % 1000) * 1000) * 1000;
      if (ts.tv_nsec > 1000000000)
	{
	  ts.tv_sec += 1;
	  ts.tv_nsec -= 1000000000;
	}

      while (ds->num_idle == 0)
	{
	  r = pthread_cond_timedwait ((pthread_cond_t *) ds->cond,
				      (pthread_mutex_t *) ds->mutex, &ts);
	  if (r == ETIMEDOUT)
	    {
	      err_buf->err_code = CCI_ER_DATASOURCE_TIMEOUT;
	      snprintf (err_buf->err_msg, 1023, "All connections are used");
	      pthread_mutex_unlock ((pthread_mutex_t *) ds->mutex);
	      return CCI_ER_DATASOURCE_TIMEOUT;
	    }
	  else if (r != 0)
	    {
	      err_buf->err_code = CCI_ER_DATASOURCE_TIMEDWAIT;
	      snprintf (err_buf->err_msg, 1023, "pthread_cond_timedwait : %d",
			r);
	      pthread_mutex_unlock ((pthread_mutex_t *) ds->mutex);
	      return CCI_ER_DATASOURCE_TIMEDWAIT;
	    }
	}
    }

  assert (ds->num_idle > 0);

  for (i = 0; i < ds->pool_size; i++)
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
      id = CCI_ER_DATASOURCE_TIMEOUT;
      err_buf->err_code = CCI_ER_DATASOURCE_TIMEOUT;
      snprintf (err_buf->err_msg, 1023, "All connections are used");
    }
  else
    {
      /* reset to default value when default_xxx property is set by user */
      if (ds->default_autocommit != CCI_DS_DEFAULT_AUTOCOMMIT_DEFAULT)
	{
	  cci_set_autocommit (id, ds->default_autocommit);
	}
      if (ds->default_lock_timeout != CCI_DS_DEFAULT_LOCK_TIMEOUT_DEFAULT)
	{
	  cci_set_lock_timeout (id, ds->default_lock_timeout, err_buf);
	}
      if (ds->default_isolation != CCI_DS_DEFAULT_ISOLATION_DEFAULT)
	{
	  cci_set_isolation_level (id, ds->default_isolation, err_buf);
	}
    }

  return id;
}

int
cci_datasource_release (T_CCI_DATASOURCE * ds, T_CCI_CONN conn,
			T_CCI_ERROR * err_buf)
{
  int i;

  if (ds == NULL || !ds->is_init)
    {
      err_buf->err_code = CCI_ER_INVALID_DATASOURCE;
      snprintf (err_buf->err_msg, 1023, "CCI data source is invalid");
      return CCI_ER_INVALID_DATASOURCE;
    }

  /* critical section begin */
  pthread_mutex_lock ((pthread_mutex_t *) ds->mutex);
  for (i = 0; i < ds->pool_size; i++)
    {
      if (ds->con_handles[i] == -conn)
	{
	  T_CCI_ERROR err_buf;
	  cci_end_tran (conn, CCI_TRAN_ROLLBACK, &err_buf);
	  ds->con_handles[i] = conn;
	  break;
	}
    }
  if (i == ds->pool_size)
    {
      /* could not found con_handles */
      pthread_mutex_unlock ((pthread_mutex_t *) ds->mutex);
      return 0;
    }

  ds->num_idle++;
  pthread_cond_signal ((pthread_cond_t *) ds->cond);
  pthread_mutex_unlock ((pthread_mutex_t *) ds->mutex);
  /* critical section end */

  return 1;
}

int
cci_set_allocators (CCI_MALLOC_FUNCTION malloc_func,
		    CCI_FREE_FUNCTION free_func,
		    CCI_REALLOC_FUNCTION realloc_func,
		    CCI_CALLOC_FUNCTION calloc_func)
{
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

#if defined(WINDOWS)
  tm_p = localtime (&sec);
  if (tm_p)
    {
      tm = *tm_p;
    }
#else
  tm_p = localtime_r (&sec, &tm);
#endif
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
