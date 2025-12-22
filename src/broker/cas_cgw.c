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
 * cas_cgw.c
 *
 */

#ident "$Id$"

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <assert.h>

#if defined(WINDOWS)
#include <winsock2.h>
#include <windows.h>
#include <process.h>
#include <sys/timeb.h>
#include <dbgHelp.h>
#else /* WINDOWS */
#include <unistd.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/time.h>
#endif /* WINDOWS */

#include "cas_common_main.h"
#include "cas_common_vars.h"
#include "broker_shm.h"
#include "broker_util.h"
#include "broker_env_def.h"
#include "broker_filename.h"
#include "cas_log.h"
#include "cas_common_execute.h"
#include "perf_monitor.h"
#include "cas_sql_log2.h"
#include "error_manager.h"
#include "ddl_log.h"
#include "broker_cas_cci.h"

#include "cas_cgw_function.h"
#include "cas_cgw_odbc.h"
#include "cas_cgw_execute.h"

#include "cas_ssl.h"

/* ========================================================================
 * Type Definitions
 * ======================================================================== */
/* CGW context structure */
typedef struct
{
  char odbc_resolved_url[CGW_LINK_URL_MAX_LEN];
  char odbc_connect_url[CGW_LINK_URL_MAX_LEN];
  char tmp_name[SRV_CON_DBNAME_SIZE];
  char tmp_user[SRV_CON_DBUSER_SIZE];
  char tmp_passwd[SRV_CON_DBPASSWD_SIZE];
  T_DBMS_TYPE dbms_type;
} CGW_CONTEXT;

/* ========================================================================
 * Global Variable Definitions
 * ======================================================================== */
/* Database connection info */
static char cas_db_name[MAX_HA_DBINFO_LENGTH];
static char cas_db_user[SRV_CON_DBUSER_SIZE];
static char cas_db_passwd[SRV_CON_DBPASSWD_SIZE];

/* ========================================================================
 * Static Variable Definitions
 * ======================================================================== */
static SOCKET srv_sock_fd;
static CGW_CONTEXT *cgw_current_ctx = NULL;	/* Global CGW context for db_connect callback */

/* ========================================================================
 * Function Tables
 * ======================================================================== */
static T_SERVER_FUNC server_fn_table[] = {
  fn_cgw_end_tran,		/* CAS_FC_END_TRAN */
  fn_cgw_prepare,		/* CAS_FC_PREPARE */
  fn_cgw_execute,		/* CAS_FC_EXECUTE */
  fn_not_supported,		/* CAS_FC_GET_DB_PARAMETER */
  fn_not_supported,		/* CAS_FC_SET_DB_PARAMETER */
  fn_cgw_close_req_handle,	/* CAS_FC_CLOSE_REQ_HANDLE */
  fn_cgw_cursor,		/* CAS_FC_CURSOR */
  fn_cgw_get_fetch,		/* CAS_FC_FETCH */
  fn_not_supported,		/* CAS_FC_SCHEMA_INFO */
  fn_not_supported,		/* CAS_FC_OID_GET */
  fn_not_supported,		/* CAS_FC_OID_SET */
  fn_not_supported,		/* CAS_FC_DEPRECATED1 */
  fn_not_supported,		/* CAS_FC_DEPRECATED2 */
  fn_not_supported,		/* CAS_FC_DEPRECATED3 */
  fn_cgw_get_db_version,	/* CAS_FC_GET_DB_VERSION */
  fn_not_supported,		/* CAS_FC_GET_CLASS_NUM_OBJS */
  fn_not_supported,		/* CAS_FC_OID_CMD */
  fn_not_supported,		/* CAS_FC_COLLECTION */
  fn_not_supported,		/* CAS_FC_NEXT_RESULT */
  fn_not_supported,		/* CAS_FC_EXECUTE_BATCH */
  fn_not_supported,		/* CAS_FC_EXECUTE_ARRAY */
  fn_not_supported,		/* CAS_FC_CURSOR_UPDATE */
  fn_not_supported,		/* CAS_FC_GET_ATTR_TYPE_STR */
  fn_not_supported,		/* CAS_FC_GET_QUERY_INFO */
  fn_not_supported,		/* CAS_FC_DEPRECATED4 */
  fn_not_supported,		/* CAS_FC_SAVEPOINT */
  fn_not_supported,		/* CAS_FC_PARAMETER_INFO */
  fn_not_supported,		/* CAS_FC_XA_PREPARE */
  fn_not_supported,		/* CAS_FC_XA_RECOVER */
  fn_not_supported,		/* CAS_FC_XA_END_TRAN */
  fn_cgw_con_close,		/* CAS_FC_CON_CLOSE */
  fn_cgw_check_cas,		/* CAS_FC_CHECK_CAS */
  fn_not_supported,		/* CAS_FC_MAKE_OUT_RS */
  fn_not_supported,		/* CAS_FC_GET_GENERATED_KEYS */
  fn_not_supported,		/* CAS_FC_LOB_NEW */
  fn_not_supported,		/* CAS_FC_LOB_WRITE */
  fn_not_supported,		/* CAS_FC_LOB_READ */
  fn_not_supported,		/* CAS_FC_END_SESSION */
  fn_not_supported,		/* CAS_FC_GET_ROW_COUNT */
  fn_not_supported,		/* CAS_FC_GET_LAST_INSERT_ID */
  fn_not_supported,		/* CAS_FC_PREPARE_AND_EXECUTE */
  fn_not_supported,		/* CAS_FC_CURSOR_CLOSE */
  fn_not_supported,		/* CAS_FC_GET_SHARD_INFO */
  fn_not_supported		/* CAS_FC_SET_CAS_CHANGE_MODE */
};

static const char *server_func_name[] = {
  "end_tran",
  "prepare",
  "execute",
  "get_db_parameter",
  "set_db_parameter",
  "close_req_handle",
  "cursor",
  "fetch",
  "schema_info",
  "oid_get",
  "oid_put",
  "deprecated1",
  "deprecated2",
  "deprecated3",
  "get_db_version",
  "get_class_num_objs",
  "oid",
  "collection",
  "next_result",
  "execute_batch",
  "execute_array",
  "cursor_update",
  "get_attr_type_str",
  "get_query_info",
  "deprecated4",
  "savepoint",
  "parameter_info",
  "xa_prepare",
  "xa_recover",
  "xa_end_tran",
  "con_close",
  "check_cas",
  "make_out_rs",
  "get_generated_keys",
  "lob_new",
  "lob_write",
  "lob_read",
  "end_session",
  "get_row_count",
  "get_last_insert_id",
  "prepare_and_execute",
  "cursor_close",
  "get_shard_info",
  "set_cas_change_mode"
};

static int cgw_cas_main (void);
static int cgw_cas_init (void);
static int cgw_init_shm (void);

/* Callback functions for cas_main_loop() */
static int cgw_pre_db_connect (const char *db_name, const char *db_user, const char *db_passwd, const char *url,
			       void *context);
static int cgw_db_connect (SOCKET client_sock_fd, const char *db_name, const char *db_user, const char *db_passwd,
			   const char *url, T_REQ_INFO * req_info, char *cas_info);
static void cgw_post_db_connect (void *context, struct timeval *cas_start_time, int shm_as_index, int client_ip_addr,
				 char *db_name, char *db_user, const char *url, bool is_new_connection);
static void cgw_cleanup_session (void);

static void cas_send_connect_reply_to_driver (T_CAS_PROTOCOL protocol, SOCKET client_sock_fd, char *cas_info);
static FN_RETURN process_request (SOCKET sock_fd, T_NET_BUF * net_buf, T_REQ_INFO * req_info);


#if defined(WINDOWS)
int WINAPI
WinMain (HINSTANCE hInstance,	// handle to current instance
	 HINSTANCE hPrevInstance,	// handle to previous instance
	 LPSTR lpCmdLine,	// pointer to command line
	 int nShowCmd		// show state of window
  )
#else /* WINDOWS */
int
main (int argc, char *argv[])
#endif
{
  int res = 0;

#if !defined(WINDOWS)
  signal (SIGTERM, cas_sig_handler);
  signal (SIGINT, cas_sig_handler);
  signal (SIGSEGV, cas_sig_handler);
  signal (SIGABRT, cas_sig_handler);
  signal (SIGFPE, cas_sig_handler);
  signal (SIGILL, cas_sig_handler);
  signal (SIGBUS, cas_sig_handler);
  signal (SIGSYS, cas_sig_handler);
  signal (SIGUSR1, SIG_IGN);
  signal (SIGPIPE, SIG_IGN);
  signal (SIGXFSZ, SIG_IGN);
#endif /* WINDOWS */

  if (cgw_cas_init () < 0)
    {
      fprintf (stderr, "CGW initialization failed. Exiting.\n");
      return -1;
    }

#if !defined(WINDOWS)
  program_name = argv[0];
  if (argc == 2 && strcmp (argv[1], "--version") == 0)
    {
      printf ("%s\n", makestring (BUILD_NUMBER));
      return 0;
    }
#else /* !WINDOWS */
  program_name = APPL_SERVER_CAS_CGW_NAME;
#endif /* !WINDOWS */

  memset (&req_info, 0, sizeof (req_info));

  set_cubrid_home ();

  res = cgw_cas_main ();
  return res;
}

static int
cgw_cas_main (void)
{
  CGW_CONTEXT cgw_ctx = { 0 };
  CAS_MAIN_OPS ops = {
    .init_specific = cgw_init,	/* CGW specific initialization */
    .pre_db_connect = cgw_pre_db_connect,
    .db_connect = cgw_db_connect,
    .post_db_connect = cgw_post_db_connect,
    .cleanup_session = cgw_cleanup_session,
    .process_request = process_request,
    .set_session_id = NULL,	/* CGW doesn't use session ID */
    .send_connect_reply = cas_send_connect_reply_to_driver,
    .context = &cgw_ctx
  };

  /* Initialize error log file path for CGW */
  char errplog_path[BROKER_PATH_MAX] = { 0, };
  char errlog_file[BROKER_PATH_MAX] = { 0, };

  sprintf (errlog_file, "%s%s_%d.err",
	   get_cubrid_file (FID_CUBRID_ERR_DIR, errplog_path, BROKER_PATH_MAX), shm_appl->broker_name,
	   shm_as_index + 1);

  er_init (errlog_file, ER_NEVER_EXIT);

  /* Set global context for db_connect callback */
  cgw_current_ctx = &cgw_ctx;

  return cas_main_loop (&ops);
}

static void
cas_send_connect_reply_to_driver (T_CAS_PROTOCOL protocol, SOCKET client_sock_fd, char *cas_info)
{
  char msgbuf[CAS_CONNECTION_REPLY_SIZE + 8];
  char *p = msgbuf;
  char sessid[DRIVER_SESSION_SIZE];
  int v;

  memset (sessid, 0, DRIVER_SESSION_SIZE);

  if (DOES_CLIENT_UNDERSTAND_THE_PROTOCOL (protocol, PROTOCOL_V4))
    {
      v = htonl (CAS_CONNECTION_REPLY_SIZE);
    }
  else if (DOES_CLIENT_UNDERSTAND_THE_PROTOCOL (protocol, PROTOCOL_V3))
    {
      v = htonl (CAS_CONNECTION_REPLY_SIZE_V3);
    }
  else
    {
      v = htonl (CAS_CONNECTION_REPLY_SIZE_PRIOR_PROTOCOL_V3);
    }
  memcpy (p, &v, sizeof (int));
  p += sizeof (int);
  if (cas_info_size > 0)
    {
      memcpy (p, cas_info, cas_info_size);
      p += cas_info_size;
    }
  v = htonl (getpid ());
  memcpy (p, &v, CAS_PID_SIZE);
  p += CAS_PID_SIZE;
  memcpy (p, cas_bi_get_broker_info (), BROKER_INFO_SIZE);
  p += BROKER_INFO_SIZE;
  if (DOES_CLIENT_UNDERSTAND_THE_PROTOCOL (protocol, PROTOCOL_V4))
    {
      v = htonl (shm_as_index + 1);
      memcpy (p, &v, CAS_PID_SIZE);
      p += CAS_PID_SIZE;
    }
  if (DOES_CLIENT_UNDERSTAND_THE_PROTOCOL (protocol, PROTOCOL_V3))
    {
      memcpy (p, sessid, DRIVER_SESSION_SIZE);
      p += DRIVER_SESSION_SIZE;
    }
  else
    {
      v = 0;
      memcpy (p, &v, SESSION_ID_SIZE);
      p += SESSION_ID_SIZE;
    }
  net_write_stream (client_sock_fd, msgbuf, p - msgbuf);
}

static int
cgw_pre_db_connect (const char *db_name, const char *db_user, const char *db_passwd, const char *url, void *context)
{
  CGW_CONTEXT *cgw_ctx = (CGW_CONTEXT *) context;
  const char *find_gateway;

  if (url == NULL)
    {
      return -1;
    }

  find_gateway = strstr (url, "__gateway=true");
  if (find_gateway == NULL)
    {
      return -1;		/* Will be handled as authorization error */
    }

  cgw_ctx->dbms_type = cgw_is_supported_dbms (shm_appl->cgw_link_server);
  cgw_set_dbms_type (cgw_ctx->dbms_type);

  strncpy (cgw_ctx->tmp_name, db_name, SRV_CON_DBNAME_SIZE - 1);
  cgw_ctx->tmp_name[SRV_CON_DBNAME_SIZE - 1] = '\0';
  strncpy (cgw_ctx->tmp_user, db_user, SRV_CON_DBUSER_SIZE - 1);
  cgw_ctx->tmp_user[SRV_CON_DBUSER_SIZE - 1] = '\0';
  strncpy (cgw_ctx->tmp_passwd, db_passwd, SRV_CON_DBPASSWD_SIZE - 1);
  cgw_ctx->tmp_passwd[SRV_CON_DBPASSWD_SIZE - 1] = '\0';

  if (cgw_ctx->dbms_type == CAS_CGW_DBMS_ORACLE)
    {
      snprintf (cgw_ctx->odbc_connect_url, CGW_LINK_URL_MAX_LEN, ORACLE_CONNECT_URL_FORMAT,
		shm_appl->cgw_link_odbc_driver_name,
		cgw_ctx->tmp_name,
		shm_appl->cgw_link_server_port,
		cgw_ctx->tmp_name, cgw_ctx->tmp_user, cgw_ctx->tmp_passwd, shm_appl->cgw_link_connect_url_property);

      snprintf (cgw_ctx->odbc_resolved_url, CGW_LINK_URL_MAX_LEN, ORACLE_CONNECT_URL_FORMAT,
		shm_appl->cgw_link_odbc_driver_name,
		cgw_ctx->tmp_name,
		shm_appl->cgw_link_server_port,
		cgw_ctx->tmp_name, cgw_ctx->tmp_user, "********", shm_appl->cgw_link_connect_url_property);
    }
  else if (cgw_ctx->dbms_type == CAS_CGW_DBMS_MYSQL || cgw_ctx->dbms_type == CAS_CGW_DBMS_MARIADB)
    {
      snprintf (cgw_ctx->odbc_connect_url, CGW_LINK_URL_MAX_LEN, MYSQL_CONNECT_URL_FORMAT,
		shm_appl->cgw_link_odbc_driver_name,
		shm_appl->cgw_link_server_ip,
		shm_appl->cgw_link_server_port,
		cgw_ctx->tmp_name, cgw_ctx->tmp_user, cgw_ctx->tmp_passwd, shm_appl->cgw_link_connect_url_property);

      snprintf (cgw_ctx->odbc_resolved_url, CGW_LINK_URL_MAX_LEN, MYSQL_CONNECT_URL_FORMAT,
		shm_appl->cgw_link_odbc_driver_name,
		shm_appl->cgw_link_server_ip,
		shm_appl->cgw_link_server_port,
		cgw_ctx->tmp_name, cgw_ctx->tmp_user, "********", shm_appl->cgw_link_connect_url_property);
    }
  else
    {
      return -1;		/* Unsupported DBMS */
    }

  return 0;
}

static int
cgw_db_connect (SOCKET client_sock_fd, const char *db_name, const char *db_user, const char *db_passwd, const char *url,
		T_REQ_INFO * req_info, char *cas_info)
{
  int err_code;
  char *db_err_msg = NULL;

  if (cgw_current_ctx == NULL)
    {
      return -1;
    }

  err_code = cgw_database_connect (cgw_current_ctx->dbms_type, cgw_current_ctx->odbc_connect_url, (char *) db_name,
				   (char *) db_user, (char *) db_passwd);

  if (err_code < 0)
    {
      char msg_buf[LINE_MAX];
      cas_info[CAS_INFO_STATUS] = CAS_INFO_STATUS_INACTIVE;

      err_code = ERROR_INFO_SET (db_error_code (), DBMS_ERROR_INDICATOR);
      db_err_msg = (char *) db_error_string (1);
      net_write_error (client_sock_fd, req_info->client_version, req_info->driver_info, cas_info, cas_info_size,
		       err_info.err_indicator, err_info.err_number, db_err_msg);

      if (db_err_msg == NULL)
	{
	  snprintf (msg_buf, LINE_MAX, "connect url %s, error:%d", cgw_current_ctx->odbc_connect_url,
		    err_info.err_number);
	}
      else
	{
	  snprintf (msg_buf, LINE_MAX, "connect url %s, error:%d, %s", cgw_current_ctx->odbc_connect_url,
		    err_info.err_number, db_err_msg);
	}

      cas_log_write_and_end (0, false, msg_buf);
      cas_slow_log_write_and_end (NULL, 0, msg_buf);
      cas_finish_session (client_sock_fd, ssl_client);
      return err_code;
    }

  return err_code;
}

static void
cgw_post_db_connect (void *context, struct timeval *cas_start_time, int shm_as_index, int client_ip_addr, char *db_name,
		     char *db_user, const char *url, bool is_new_connection)
{
  CGW_CONTEXT *cgw_ctx = (CGW_CONTEXT *) context;

  cas_bi_set_dbms_type (cgw_ctx->dbms_type);
  cas_log_write_and_end (0, false, "connect db %s@%s user %s url %s", cgw_ctx->tmp_name,
			 shm_appl->cgw_link_server_ip, cgw_ctx->tmp_user, cgw_ctx->odbc_resolved_url);
}

static void
cgw_cleanup_session (void)
{
  extern FN_RETURN cas_main_fn_ret;

  if (ux_cgw_end_tran (CCI_TRAN_ROLLBACK, false, true) < 0)
    {
      as_info->reset_flag = TRUE;
    }
}

static FN_RETURN
process_request (SOCKET sock_fd, T_NET_BUF * net_buf, T_REQ_INFO * req_info)
{
  MSG_HEADER client_msg_header;
  MSG_HEADER cas_msg_header;
  char *read_msg;
  char func_code;
  int argc;
  void **argv = NULL;
  int err_code;

  int con_status_to_restore, old_con_status;
  T_SERVER_FUNC server_fn;
  FN_RETURN fn_ret = FN_KEEP_CONN;

  error_info_clear ();
  init_msg_header (&client_msg_header);
  init_msg_header (&cas_msg_header);

  old_con_status = as_info->con_status;

  if (cas_shard_flag == ON)
    {
      assert (0);
      return FN_CLOSE_CONN;
    }
  else
    {
      unset_hang_check_time ();
      if (as_info->cur_keep_con == KEEP_CON_AUTO)
	{
	  err_code = net_read_int_keep_con_auto (sock_fd, &client_msg_header, req_info, srv_sock_fd);
	}
      else
	{
	  err_code = net_read_header_keep_con_on (sock_fd, &client_msg_header);

	  if (as_info->cur_keep_con == KEEP_CON_ON && as_info->con_status == CON_STATUS_OUT_TRAN)
	    {
	      as_info->con_status = CON_STATUS_IN_TRAN;
	      as_info->transaction_start_time = time (0);
	      errors_in_transaction = 0;
	    }
	}
      if (err_code < 0)
	{
	  const char *cas_log_msg = NULL;

	  fn_ret = FN_CLOSE_CONN;

	  if (as_info->reset_flag)
	    {
	      cas_log_msg = "RESET";
	      cas_log_write_and_end (0, true, cas_log_msg);
	      fn_ret = FN_KEEP_SESS;
	      db_set_keep_session (true);
	    }
	  if (as_info->con_status == CON_STATUS_CLOSE_AND_CONNECT)
	    {
	      cas_log_msg = "CHANGE CLIENT";
	      fn_ret = FN_KEEP_SESS;
	      db_set_keep_session (true);
	    }

	  if (cas_log_msg == NULL)
	    {
	      if (is_net_timed_out ())
		{
		  if (as_info->reset_flag == TRUE)
		    {
		      cas_log_msg = "CONNECTION RESET";
		    }
		  else
		    {
		      cas_log_msg = "SESSION TIMEOUT";
		    }
		}
	      else
		{
		  cas_log_msg = "COMMUNICATION ERROR net_read_header()";
		}
	    }
	  cas_log_write_and_end (0, true, cas_log_msg);
	  return fn_ret;
	}
    }

  if (shm_appl->session_timeout < 0)
    net_timeout_set (NET_DEFAULT_TIMEOUT);
  else
    net_timeout_set (MIN (shm_appl->session_timeout, NET_DEFAULT_TIMEOUT));

  if (cas_shard_flag == ON && req_info->client_version == 0)
    {
      assert (0);
      req_info->client_version = CAS_PROTO_CURRENT_VER;
    }

  read_msg = (char *) MALLOC (*(client_msg_header.msg_body_size_ptr));
  if (read_msg == NULL)
    {
      net_write_error (sock_fd, req_info->client_version, req_info->driver_info, cas_msg_header.info_ptr, cas_info_size,
		       CAS_ERROR_INDICATOR, CAS_ER_NO_MORE_MEMORY, NULL);
      return FN_CLOSE_CONN;
    }
  if (net_read_stream (sock_fd, read_msg, *(client_msg_header.msg_body_size_ptr)) < 0)
    {
      FREE_MEM (read_msg);
      net_write_error (sock_fd, req_info->client_version, req_info->driver_info, cas_msg_header.info_ptr, cas_info_size,
		       CAS_ERROR_INDICATOR, CAS_ER_COMMUNICATION, NULL);
      cas_log_write_and_end (0, true, "COMMUNICATION ERROR net_read_stream()");
      return FN_CLOSE_CONN;
    }

  argc = net_decode_str (read_msg, *(client_msg_header.msg_body_size_ptr), &func_code, &argv);
  if (argc < 0)
    {
      FREE_MEM (read_msg);
      net_write_error (sock_fd, req_info->client_version, req_info->driver_info, cas_msg_header.info_ptr, cas_info_size,
		       CAS_ERROR_INDICATOR, CAS_ER_COMMUNICATION, NULL);
      return FN_CLOSE_CONN;
    }

  if (func_code <= 0 || func_code >= CAS_FC_MAX)
    {
      FREE_MEM (argv);
      FREE_MEM (read_msg);
      net_write_error (sock_fd, req_info->client_version, req_info->driver_info, cas_msg_header.info_ptr, cas_info_size,
		       CAS_ERROR_INDICATOR, CAS_ER_COMMUNICATION, NULL);
      return FN_CLOSE_CONN;
    }

  /* PROTOCOL_V2 is used only 9.0.0 */
  if (DOES_CLIENT_MATCH_THE_PROTOCOL (req_info->client_version, PROTOCOL_V2))
    {
      switch (func_code)
	{
	case CAS_FC_PREPARE_AND_EXECUTE:
	  func_code = CAS_FC_PREPARE_AND_EXECUTE_FOR_PROTO_V2;
	  break;
	case CAS_FC_CURSOR_CLOSE:
	  func_code = CAS_FC_CURSOR_CLOSE_FOR_PROTO_V2;
	  break;
	default:
	  break;
	}
    }

  con_status_to_restore = -1;

  if (FUNC_NEEDS_RESTORING_CON_STATUS (func_code))
    {
      if (is_first_request == true)
	{
	  /* If this request is the first request after connection established, con_status should be
	   * CON_STATUS_OUT_TRAN. */
	  con_status_to_restore = CON_STATUS_OUT_TRAN;
	}
      else if (con_status_before_check_cas != -1)
	{
	  con_status_to_restore = con_status_before_check_cas;
	}
      else
	{
	  con_status_to_restore = old_con_status;
	}

      con_status_before_check_cas = -1;
    }
  else if (func_code == CAS_FC_CHECK_CAS)
    {
      con_status_before_check_cas = old_con_status;
    }
  else
    {
      con_status_before_check_cas = -1;
    }

  strcpy (as_info->log_msg, server_func_name[func_code - 1]);

  server_fn = server_fn_table[func_code - 1];

  if (prev_cas_info[CAS_INFO_STATUS] != CAS_INFO_RESERVED_DEFAULT)
    {
      assert (prev_cas_info[CAS_INFO_STATUS] == client_msg_header.info_ptr[CAS_INFO_STATUS]);
#if defined (PROTOCOL_EXTENDS_DEBUG)	/* for debug cas <-> JDBC info */
      if (prev_cas_info[CAS_INFO_STATUS] != client_msg_header.info_ptr[CAS_INFO_STATUS])
	{
	  cas_log_debug (ARG_FILE_LINE,
			 "[%d][PREV : %d, RECV : %d], " "[preffunc : %d, recvfunc : %d], [REQ: %d, REQ: %d], "
			 "[JID : %d] \n", func_code - 1, prev_cas_info[CAS_INFO_STATUS],
			 client_msg_header.info_ptr[CAS_INFO_STATUS], prev_cas_info[CAS_INFO_RESERVED_1],
			 client_msg_header.info_ptr[CAS_INFO_RESERVED_1], prev_cas_info[CAS_INFO_RESERVED_2],
			 client_msg_header.info_ptr[CAS_INFO_RESERVED_2],
			 client_msg_header.info_ptr[CAS_INFO_RESERVED_3]);
	}
#endif /* end for debug */
    }

  req_info->need_auto_commit = TRAN_NOT_AUTOCOMMIT;

  cas_send_result_flag = TRUE;

  /* for 9.0 driver */
  if (DOES_CLIENT_MATCH_THE_PROTOCOL (req_info->client_version, PROTOCOL_V2))
    {
      ux_set_utype_for_enum (CCI_U_TYPE_STRING);
    }

  /* for driver less than 10.0 */
  if (!DOES_CLIENT_UNDERSTAND_THE_PROTOCOL (req_info->client_version, PROTOCOL_V7))
    {
      ux_set_utype_for_datetimetz (CCI_U_TYPE_DATETIME);
      ux_set_utype_for_timestamptz (CCI_U_TYPE_TIMESTAMP);
      ux_set_utype_for_datetimeltz (CCI_U_TYPE_DATETIME);
      ux_set_utype_for_timestampltz (CCI_U_TYPE_TIMESTAMP);
    }

  /* driver version < 10.2 */
  if (!DOES_CLIENT_UNDERSTAND_THE_PROTOCOL (req_info->client_version, PROTOCOL_V8))
    {
      ux_set_utype_for_json (CCI_U_TYPE_STRING);
    }

  as_info->fn_status = FN_STATUS_BUSY;

  net_buf->client_version = req_info->client_version;
  set_hang_check_time ();
  fn_ret = (*server_fn) (sock_fd, argc, argv, net_buf, req_info);
  set_hang_check_time ();

  /* set back original utype for enum, date-time, JSON */
  if (DOES_CLIENT_MATCH_THE_PROTOCOL (req_info->client_version, PROTOCOL_V2))
    {
      ux_set_utype_for_enum (CCI_U_TYPE_ENUM);
    }

  /* for driver less than 10.0 */
  if (!DOES_CLIENT_UNDERSTAND_THE_PROTOCOL (req_info->client_version, PROTOCOL_V7))
    {
      ux_set_utype_for_datetimetz (CCI_U_TYPE_DATETIMETZ);
      ux_set_utype_for_timestamptz (CCI_U_TYPE_TIMESTAMPTZ);
      ux_set_utype_for_datetimeltz (CCI_U_TYPE_DATETIMETZ);
      ux_set_utype_for_timestampltz (CCI_U_TYPE_TIMESTAMPTZ);
    }

  /* driver version < 10.2 */
  if (!DOES_CLIENT_UNDERSTAND_THE_PROTOCOL (req_info->client_version, PROTOCOL_V8))
    {
      ux_set_utype_for_json (CCI_U_TYPE_JSON);
    }

  cas_log_debug (ARG_FILE_LINE, "process_request: %s() err_code %d", server_func_name[func_code - 1],
		 err_info.err_number);

  if (con_status_to_restore != -1)
    {
      CON_STATUS_LOCK (as_info, CON_STATUS_LOCK_CAS);
      as_info->con_status = con_status_to_restore;
      CON_STATUS_UNLOCK (as_info, CON_STATUS_LOCK_CAS);
    }


  if (cas_shard_flag == ON && (func_code == CAS_FC_PREPARE || func_code == CAS_FC_CHECK_CAS)
      && (client_msg_header.info_ptr[CAS_INFO_ADDITIONAL_FLAG] & CAS_INFO_FLAG_MASK_FORCE_OUT_TRAN))
    {
      /* for shard dummy prepare */
      /* for connection check */
      req_info->need_auto_commit = TRAN_AUTOROLLBACK;
    }


  if (fn_ret == FN_KEEP_CONN && net_buf->err_code == 0 && as_info->con_status == CON_STATUS_IN_TRAN
      && req_info->need_auto_commit != TRAN_NOT_AUTOCOMMIT && err_info.err_number != CAS_ER_STMT_POOLING)
    {
      /* no communication error and auto commit is needed */
      err_code = ux_cgw_auto_commit (net_buf, req_info);
      if (err_code < 0)
	{
	  fn_ret = FN_CLOSE_CONN;
	}
      else
	{
	  if (as_info->cas_log_reset)
	    {
	      cas_log_reset (broker_name);
	    }
	  if (as_info->cas_slow_log_reset)
	    {
	      cas_slow_log_reset (broker_name);
	    }
	  if (!ux_cgw_is_database_connected ())
	    {
	      fn_ret = FN_CLOSE_CONN;
	    }
	  else if (restart_is_needed ())
	    {
	      fn_ret = FN_KEEP_SESS;
	      db_set_keep_session (true);
	    }
	  if (shm_appl->sql_log2 != as_info->cur_sql_log2)
	    {
	      sql_log2_end (false);
	      as_info->cur_sql_log2 = shm_appl->sql_log2;
	      sql_log2_init (broker_name, shm_as_index, as_info->cur_sql_log2, true);
	    }
	}
      as_info->num_transactions_processed %= MAX_DIAG_DATA_VALUE;
      as_info->num_transactions_processed++;

      /* should be OUT_TRAN in auto commit */
      CON_STATUS_LOCK (as_info, CON_STATUS_LOCK_CAS);
      if (as_info->con_status == CON_STATUS_IN_TRAN)
	{
	  as_info->con_status = CON_STATUS_OUT_TRAN;
	}
      CON_STATUS_UNLOCK (as_info, CON_STATUS_LOCK_CAS);
    }

  if ((func_code == CAS_FC_EXECUTE) || (func_code == CAS_FC_SCHEMA_INFO))
    {
      as_info->num_requests_received %= MAX_DIAG_DATA_VALUE;
      as_info->num_requests_received++;
    }
  else if (func_code == CAS_FC_END_TRAN)
    {
      as_info->num_transactions_processed %= MAX_DIAG_DATA_VALUE;
      as_info->num_transactions_processed++;
    }

  as_info->log_msg[0] = '\0';
  if (as_info->con_status == CON_STATUS_IN_TRAN)
    {
      cas_msg_header.info_ptr[CAS_INFO_STATUS] = CAS_INFO_STATUS_ACTIVE;
    }
  else
    {
      cas_msg_header.info_ptr[CAS_INFO_STATUS] = CAS_INFO_STATUS_INACTIVE;
    }

  if (func_code == CAS_FC_EXECUTE || func_code == CAS_FC_EXECUTE_ARRAY || func_code == CAS_FC_EXECUTE_BATCH
      || err_info.err_number < 0)
    {
      logddl_write_end ();
    }

  if (net_buf->err_code)
    {
      net_write_error (sock_fd, req_info->client_version, req_info->driver_info, cas_msg_header.info_ptr, cas_info_size,
		       CAS_ERROR_INDICATOR, net_buf->err_code, NULL);
      fn_ret = FN_CLOSE_CONN;
      goto exit_on_end;
    }

  if (cas_send_result_flag && net_buf->data != NULL)
    {

      cas_msg_header.info_ptr[CAS_INFO_ADDITIONAL_FLAG] &= ~CAS_INFO_FLAG_MASK_AUTOCOMMIT;
      cas_msg_header.info_ptr[CAS_INFO_ADDITIONAL_FLAG] |=
	(as_info->cci_default_autocommit & CAS_INFO_FLAG_MASK_AUTOCOMMIT);

      if (cas_shard_flag == ON)
	{
	  cas_msg_header.info_ptr[CAS_INFO_ADDITIONAL_FLAG] &= ~CAS_INFO_FLAG_MASK_FORCE_OUT_TRAN;
	}
#if defined (PROTOCOL_EXTENDS_DEBUG)	/* for debug cas<->jdbc info */
      cas_msg_header.info_ptr[CAS_INFO_RESERVED_1] = func_code - 1;
      cas_msg_header.info_ptr[CAS_INFO_RESERVED_2] = as_info->num_requests_received % 128;
      prev_cas_info[CAS_INFO_STATUS] = cas_msg_header.info_ptr[CAS_INFO_STATUS];
      prev_cas_info[CAS_INFO_RESERVED_1] = cas_msg_header.info_ptr[CAS_INFO_RESERVED_1];
      prev_cas_info[CAS_INFO_RESERVED_2] = cas_msg_header.info_ptr[CAS_INFO_RESERVED_2];
#endif /* end for debug */



      *(cas_msg_header.msg_body_size_ptr) = htonl (net_buf->data_size);
      memcpy (net_buf->data, cas_msg_header.msg_body_size_ptr, NET_BUF_HEADER_MSG_SIZE);

      if (cas_info_size > 0)
	{
	  memcpy (net_buf->data + NET_BUF_HEADER_MSG_SIZE, cas_msg_header.info_ptr, cas_info_size);
	}

      assert (NET_BUF_CURR_SIZE (net_buf) <= net_buf->alloc_size);
      if (net_write_stream (sock_fd, net_buf->data, NET_BUF_CURR_SIZE (net_buf)) < 0)
	{
	  cas_log_write_and_end (0, true, "COMMUNICATION ERROR net_write_stream()");
	}
    }

  if (cas_shard_flag == OFF && cas_send_result_flag && net_buf->post_send_file != NULL)
    {
      err_code = net_write_from_file (sock_fd, net_buf->post_file_size, net_buf->post_send_file);
      unlink (net_buf->post_send_file);
      if (err_code < 0)
	{
	  fn_ret = FN_CLOSE_CONN;
	  goto exit_on_end;
	}
    }

exit_on_end:

  if (cas_shard_flag == ON && as_info->con_status != CON_STATUS_IN_TRAN && as_info->uts_status == UTS_STATUS_BUSY)
    {
      as_info->uts_status = UTS_STATUS_IDLE;
    }


  net_buf_clear (net_buf);

  FREE_MEM (read_msg);
  FREE_MEM (argv);

  return fn_ret;
}

static int
cgw_cas_init ()
{
  if (cgw_init_shm () < 0)
    {
      return -1;
    }

  assert (sizeof (broker_name) == sizeof (shm_appl->broker_name));
  strcpy (broker_name, shm_appl->broker_name);

  set_cubrid_file (FID_SQL_LOG_DIR, shm_appl->log_dir);
  set_cubrid_file (FID_SLOW_LOG_DIR, shm_appl->slow_log_dir);
  set_cubrid_file (FID_CUBRID_ERR_DIR, shm_appl->err_log_dir);

  as_pid_file_create (broker_name, as_info->as_id);
  as_db_err_log_set (broker_name, shm_proxy_id, shm_shard_id, shm_shard_cas_id, shm_as_index, cas_shard_flag);

  /* Set cleanup callback for CGW specific cleanup */
  cas_set_cleanup_callback (cgw_cleanup);

  return 0;
}

static int
cgw_init_shm (void)
{
  char *p;
  int as_shm_key;
  int pxy_id, shd_id, shard_cas_id, as_id;

  p = getenv (APPL_SERVER_SHM_KEY_STR);
  if (p == NULL)
    {
      goto return_error;
    }

  parse_int (&as_shm_key, p, 10);
  SHARD_ERR ("<CAS> APPL_SERVER_SHM_KEY_STR:[%d:%x]\n", as_shm_key, as_shm_key);
  shm_appl = (T_SHM_APPL_SERVER *) uw_shm_open (as_shm_key, SHM_APPL_SERVER, SHM_MODE_ADMIN);

  if (shm_appl == NULL)
    {
      goto return_error;
    }

  p = getenv (AS_ID_ENV_STR);
  if (p == NULL)
    {
      goto return_error;
    }

  parse_int (&as_id, p, 10);
  SHARD_ERR ("<CAS> AS_ID_ENV_STR:[%d]\n", as_id);
  as_info = &shm_appl->as_info[as_id];

  shm_as_index = as_id;

  cas_shard_flag = shm_appl->shard_flag;

  if (cas_shard_flag == OFF)
    {
      return 0;
    }

return_error:

  if (shm_appl)
    {
      uw_shm_detach (shm_appl);
      shm_appl = NULL;
    }

  return -1;
}
