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
 * cas.c -
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

#include "cas_common.h"
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

#include "cas.h"
#include "cas_dispatch.h"
#include "cas_network.h"
#include "cas_function.h"
#include "cas_net_buf.h"
#include "query_replace.h"
#include "cas_execute.h"
#include "connection_support.hpp"
#include "broker_process_size.h"
#include "cas_ssl.h"


/* ========================================================================
 * Function Tables
 * ======================================================================== */


#if defined(WINDOWS)
LONG WINAPI CreateMiniDump (struct _EXCEPTION_POINTERS *pException);
#endif /* WINDOWS */

/* Main functions */
static int cas_main (void);
static int shard_cas_main (void);
static int cas_init (void);
static int cas_init_shm (void);
static int cas_register_to_proxy (SOCKET proxy_sock_fd);

/* Callback functions for cas_main_loop() */
static int cas_init_specific (void);
static int cas_db_connect (SOCKET client_sock_fd, const char *db_name, const char *db_user, const char *db_passwd,
			   const char *url, T_REQ_INFO * req_info, char *cas_info);
static void cas_post_db_connect (void *context, struct timeval *cas_start_time, int shm_as_index, int client_ip_addr,
				 char *db_name, char *db_user, const char *url, bool is_new_connection);
static void cas_cleanup_session (void);

/* Protocol functions */
static void cas_send_connect_reply_to_driver (T_CAS_PROTOCOL protocol, SOCKET client_sock_fd, char *cas_info);
static void cas_make_session_for_driver (char *out);
static void cas_set_session_id (T_CAS_PROTOCOL protocol, char *session);

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

  if (cas_init () < 0)
    {
      fprintf (stderr, "CAS initialization failed. Exiting.\n");
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
  program_name = APPL_SERVER_CAS_NAME;
#endif /* !WINDOWS */

  memset (&req_info, 0, sizeof (req_info));

  set_cubrid_home ();

  if (cas_shard_flag == ON)
    {
      res = shard_cas_main ();
    }
  else
    {
      res = cas_main ();
    }

  return res;
}

/* once-per-process initialization for the non-shard CAS (cub_cas).
 * invoked by cas_main_loop() before the request loop. */
static int
cas_init_specific (void)
{
  /* attach to the query replace rule segment built by the broker.  a shard or CGW broker
   * never publishes one (broker_shm.c leaves query_replace_shm_key at 0), so this is a
   * no-op there.  the CAS_FOR_CGW guard is defensive: cas.c is built only into cub_cas. */
#if !defined(CAS_FOR_CGW)
  qr_init (shm_appl);
#endif
  return 0;
}

static int
cas_main (void)
{
  CAS_MAIN_OPS ops = {
    .init_specific = cas_init_specific,	/* attach query replace rule segment */
    .pre_db_connect = NULL,	/* No pre-connect processing for cas.c */
    .db_connect = cas_db_connect,
    .post_db_connect = cas_post_db_connect,
    .cleanup_session = cas_cleanup_session,
    .process_request = cas_process_request,
    .set_session_id = cas_set_session_id,
    .send_connect_reply = cas_send_connect_reply_to_driver,
    .context = NULL
  };

  return cas_main_loop (&ops);
}

static void
cas_make_session_for_driver (char *out)
{
  size_t size = 0;
  SESSION_ID session;


  memcpy (out + size, db_get_server_session_key (), SERVER_SESSION_KEY_SIZE);
  size += SERVER_SESSION_KEY_SIZE;
  session = db_get_session_id ();
  session = htonl (session);
  memcpy (out + size, &session, sizeof (SESSION_ID));
  size += sizeof (SESSION_ID);
  memset (out + size, 0, DRIVER_SESSION_SIZE - size);
}

static void
cas_set_session_id (T_CAS_PROTOCOL protocol, char *session)
{
  SESSION_ID id = DB_EMPTY_SESSION;

  if (DOES_CLIENT_UNDERSTAND_THE_PROTOCOL (protocol, PROTOCOL_V3))
    {
      id = *(SESSION_ID *) (session + 8);
      id = ntohl (id);
      db_set_server_session_key (session);
      db_set_session_id (id);
      cas_log_write_and_end (0, false, "session id for connection %u", id);
    }
  else
    {
      /* always create new session for old drivers */
      char key[] =
	{ (char) 0xFF, (char) 0xFF, (char) 0xFF, (char) 0xFF, (char) 0xFF, (char) 0xFF, (char) 0xFF, (char) 0xFF };

      cas_log_write_and_end (0, false, "session id (old protocol) for connection 0");
      db_set_server_session_key (key);
      db_set_session_id (DB_EMPTY_SESSION);
    }
}

static void
cas_send_connect_reply_to_driver (T_CAS_PROTOCOL protocol, SOCKET client_sock_fd, char *cas_info)
{
  char msgbuf[CAS_CONNECTION_REPLY_SIZE + 8];
  char *p = msgbuf;
  char sessid[DRIVER_SESSION_SIZE];
  int v;

  cas_make_session_for_driver (sessid);

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
      v = htonl (db_get_session_id ());
      memcpy (p, &v, SESSION_ID_SIZE);
      p += SESSION_ID_SIZE;
    }
  net_write_stream (client_sock_fd, msgbuf, p - msgbuf);
}

static int
cas_db_connect (SOCKET client_sock_fd, const char *db_name, const char *db_user, const char *db_passwd, const char *url,
		T_REQ_INFO * req_info, char *cas_info)
{
  int err_code;
  char *db_err_msg = NULL;
  err_code = ux_database_connect ((char *) db_name, (char *) db_user, (char *) db_passwd, &db_err_msg);
  if (err_code < 0)
    {
      char msg_buf[LINE_MAX];
      cas_info[CAS_INFO_STATUS] = CAS_INFO_STATUS_INACTIVE;

      net_write_error (client_sock_fd, req_info->client_version, req_info->driver_info, cas_info, cas_info_size,
		       err_info.err_indicator, err_info.err_number, db_err_msg);
      if (db_err_msg == NULL)
	{
	  snprintf (msg_buf, LINE_MAX, "connect db %s user %s url %s, error:%d.", db_name, db_user, url,
		    err_info.err_number);
	}
      else
	{
	  snprintf (msg_buf, LINE_MAX, "connect db %s user %s url %s, error:%d, %s", db_name, db_user, url,
		    err_info.err_number, db_err_msg);
	}

      cas_log_write_and_end (0, false, "%s", msg_buf);
      cas_slow_log_write_and_end (NULL, 0, "%s", msg_buf);
      cas_finish_session (client_sock_fd, ssl_client);
      FREE_MEM (db_err_msg);
      return -1;
    }

  qr_load_dbuser_has_rules (as_info->database_name, db_user);

  return err_code;
}

static void
cas_post_db_connect (void *context, struct timeval *cas_start_time, int shm_as_index, int client_ip_addr, char *db_name,
		     char *db_user, const char *url, bool is_new_connection)
{
  SESSION_ID session_id;

  session_id = db_get_session_id ();
  as_info->session_id = session_id;

  if (shm_appl->access_log == ON)
    {
      ACCESS_LOG_TYPE type = (is_new_connection) ? NEW_CONNECTION : CLIENT_CHANGED;

      cas_access_log (cas_start_time, shm_as_index, client_ip_addr, db_name, db_user, type);
    }

  cas_log_write_and_end (0, false, "connect db %s@%s user %s url %s" " session id %u", as_info->database_name,
			 as_info->database_host, db_user, url, session_id);

  ux_set_default_setting ();
}

typedef struct
{
  FN_RETURN fn_ret;
} CAS_CLEANUP_CONTEXT;

/* Get fn_ret from cas_main_loop() */
extern FN_RETURN cas_main_fn_ret;

static void
cas_cleanup_session (void)
{
  if (!is_xa_prepared ())
    {
      if (ux_end_tran (CCI_TRAN_ROLLBACK, false, true) < 0)
	{
	  as_info->reset_flag = TRUE;
	}
    }

  if (cas_main_fn_ret != FN_KEEP_SESS)
    {
      ux_end_session ();
    }

  if (is_xa_prepared ())
    {
      ux_database_shutdown (true);
      /* Note: db_name, db_user, db_passwd should be available from context */
    }

  if (as_info->reset_flag == TRUE)
    {
      ux_database_shutdown (true);
      as_info->reset_flag = FALSE;
      cas_set_db_connect_status (-1);	/* DB_CONNECTION_STATUS_RESET */
    }
}

static int
shard_cas_main (void)
{
  T_NET_BUF net_buf;
  SOCKET proxy_sock_fd = INVALID_SOCKET;
  int err_code;
  int one = 1;
  FN_RETURN fn_ret = FN_KEEP_CONN;

  struct timeval cas_start_time;

  int error;

  bool is_first = true;

  prev_cas_info[CAS_INFO_STATUS] = CAS_INFO_RESERVED_DEFAULT;

  net_buf_init (&net_buf, cas_get_client_version ());
  net_buf.data = (char *) MALLOC (SHARD_NET_BUF_ALLOC_SIZE);
  if (net_buf.data == NULL)
    {
      return -1;
    }
  net_buf.alloc_size = SHARD_NET_BUF_ALLOC_SIZE;

  as_info->service_ready_flag = TRUE;
  as_info->con_status = CON_STATUS_IN_TRAN;
  as_info->cur_keep_con = KEEP_CON_DEFAULT;
  errors_in_transaction = 0;
#if !defined(WINDOWS)
  psize_at_start = as_info->psize = getsize (getpid ());
#endif /* !WINDOWS */

  stripped_column_name = shm_appl->stripped_column_name;

conn_retry:
  if (is_first == false)
    {
      do
	{
	  SLEEP_SEC (1);
	}
      while (as_info->uts_status == UTS_STATUS_RESTART || as_info->uts_status == UTS_STATUS_STOP);
    }
  is_first = false;

  net_timeout_set (-1);

  cas_log_open (broker_name);
  cas_slow_log_open (broker_name);
  cas_log_write_and_end (0, true, "CAS STARTED pid %d", getpid ());

  /* This is a only use in proxy-cas internal message */
  req_info.client_version = CAS_PROTO_CURRENT_VER;

  set_cas_info_size ();

  gettimeofday (&cas_start_time, NULL);

  int ret;
  ret = snprintf (cas_db_name, MAX_HA_DBINFO_LENGTH - 1, "%s@%s", shm_appl->shard_conn_info[shm_shard_id].db_name,
		  shm_appl->shard_conn_info[shm_shard_id].db_host);

  if (ret < 0)
    {
      assert (false);
      FREE (net_buf.data);
      return -1;
    }

  set_db_connection_info ();

  if (as_info->reset_flag == TRUE)
    {
      cas_log_debug (ARG_FILE_LINE, "main: set reset_flag");
      cas_set_db_connect_status (-1);	/* DB_CONNECTION_STATUS_RESET */
      as_info->reset_flag = FALSE;
    }

#if defined(WINDOWS)
  __try
  {
#endif /* WINDOWS */

    if (cas_db_user[0] != '\0')
      {
	err_code = ux_database_connect (cas_db_name, cas_db_user, cas_db_passwd, NULL);
	if (err_code < 0)
	  {
	    clear_db_connection_info ();
	    SLEEP_SEC (1);
	    goto finish_cas;
	  }

	ux_set_default_setting ();

	cas_log_write_and_end (0, false, "connect db %s user %s", cas_db_name, cas_db_user);
      }

    as_info->uts_status = UTS_STATUS_IDLE;

  conn_proxy_retry:
    net_timeout_set (NET_DEFAULT_TIMEOUT);

#if defined(WINDOWS)
    proxy_sock_fd = net_connect_proxy (shm_proxy_id);
#else /* WINDOWS */
    proxy_sock_fd = net_connect_proxy ();
#endif /* !WINDOWS */

    if (IS_INVALID_SOCKET (proxy_sock_fd))
      {
	SLEEP_SEC (1);
	goto conn_proxy_retry;
      }

    net_timeout_set (-1);

    setsockopt (proxy_sock_fd, IPPROTO_TCP, TCP_NODELAY, (char *) &one, sizeof (one));

    error = cas_register_to_proxy (proxy_sock_fd);
    if (error)
      {
	CLOSE_SOCKET (proxy_sock_fd);
	SLEEP_SEC (1);
	goto conn_proxy_retry;
      }

#if defined(WINDOWS)
    as_info->uts_status = UTS_STATUS_BUSY;
#endif /* WINDOWS */
    errors_in_transaction = 0;

    net_timeout_set (NET_DEFAULT_TIMEOUT);

    as_info->cur_sql_log2 = shm_appl->sql_log2;
    sql_log2_init (broker_name, shm_as_index, as_info->cur_sql_log2, false);
    setsockopt (proxy_sock_fd, IPPROTO_TCP, TCP_NODELAY, (char *) &one, sizeof (one));

    if (IS_INVALID_SOCKET (proxy_sock_fd))
      {
	goto conn_proxy_retry;
      }

    as_info->auto_commit_mode = FALSE;
    cas_log_write_and_end (0, false, "DEFAULT isolation_level %d, " "lock_timeout %d", cas_default_isolation_level,
			   cas_default_lock_timeout);

    if (shm_appl->statement_pooling)
      {
	as_info->cur_statement_pooling = ON;
      }
    else
      {
	as_info->cur_statement_pooling = OFF;
      }
/* TODO : SHARD, assume KEEP_CON_ON*/
    as_info->cur_keep_con = KEEP_CON_ON;

    as_info->cci_default_autocommit = shm_appl->cci_default_autocommit;
    req_info.need_rollback = TRUE;

    gettimeofday (&tran_start_time, NULL);
    gettimeofday (&query_start_time, NULL);
    tran_timeout = 0;
    query_timeout = 0;

    er_init (NULL, ER_NEVER_EXIT);

    for (;;)
      {
	cas_log_error_handler_begin ();
	fn_ret = FN_KEEP_CONN;
	as_info->con_status = CON_STATUS_OUT_TRAN;

	while (fn_ret == FN_KEEP_CONN)
	  {
#if !defined(WINDOWS)
	    signal (SIGUSR1, query_cancel);
#endif /* !WINDOWS */

	    fn_ret = cas_process_request (proxy_sock_fd, &net_buf, &req_info, INVALID_SOCKET);
	    cas_log_error_handler_clear ();
#if !defined(WINDOWS)
	    signal (SIGUSR1, SIG_IGN);
#endif /* !WINDOWS */
	    as_info->last_access_time = time (NULL);

	    if (as_info->con_status == CON_STATUS_OUT_TRAN
		&& hm_srv_handle_get_current_count () >= shm_appl->max_prepared_stmt_count)
	      {
		fn_ret = FN_CLOSE_CONN;
	      }
	  }
	/* This is a only use in proxy-cas internal message */
	req_info.client_version = CAS_PROTO_CURRENT_VER;

	prev_cas_info[CAS_INFO_STATUS] = CAS_INFO_RESERVED_DEFAULT;

	if (as_info->cur_statement_pooling)
	  {
	    hm_srv_handle_free_all (true);
	  }

	if (!is_xa_prepared ())
	  {
	    ux_end_tran (CCI_TRAN_ROLLBACK, false, true);
	  }

	if (fn_ret != FN_KEEP_SESS)
	  {
	    ux_end_session ();
	  }

	if (as_info->reset_flag == TRUE || is_xa_prepared ())
	  {
	    ux_database_shutdown (true);
	    as_info->reset_flag = FALSE;
	    cas_set_db_connect_status (-1);	/* DB_CONNECTION_STATUS_RESET */
	  }

	cas_log_error_handler_end ();
      finish_cas:
#if defined(WINDOWS)
	as_info->close_flag = 1;
#endif /* WINDOWS */

	cas_log_write_and_end (0, true, "disconnect");
	cas_log_write2 (sql_log2_get_filename ());
	cas_log_write_and_end (0, false, "STATE idle");
	cas_log_close (true);
	cas_slow_log_close ();
	sql_log2_end (true);

#if defined(WINDOWS)
	cas_req_count++;
#endif /* WINDOWS */
	CLOSE_SOCKET (proxy_sock_fd);

	if (restart_is_needed ())
	  {
	    cas_final ();
	    return 0;
	  }
	else if (fn_ret == FN_GRACEFUL_DOWN)
	  {
	    as_info->uts_status = UTS_STATUS_STOP;
	  }
	else
	  {
	    as_info->uts_status = UTS_STATUS_CON_WAIT;
	  }

	goto conn_retry;
      }
#if defined(WINDOWS)
  }
  __except (CreateMiniDump (GetExceptionInformation ()))
  {
  }
#endif /* WINDOWS */

  return 0;
}

static int
cas_init ()
{
  if (cas_init_shm () < 0)
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

  /* Set database shutdown callback for cas.c specific implementation */
  cas_set_database_shutdown_callback (ux_database_shutdown);

  if (cas_shard_flag == OFF)
    {
      css_register_check_server_alive_fn (check_server_alive);
      css_register_server_timeout_fn (set_hang_check_time);
    }
  return 0;
}

static int
cas_init_shm (void)
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

  pxy_id = as_info->proxy_id;
  SHARD_ERR ("<CAS> PROXY_ID:[%d]\n", pxy_id);
  shm_proxy_id = pxy_id;

  shd_id = as_info->shard_id;
  SHARD_ERR ("<CAS> SHARD_ID:[%d]\n", shd_id);
  shm_shard_id = shd_id;

  shard_cas_id = as_info->shard_cas_id;
  SHARD_ERR ("<CAS> SHARD_CAS_ID:[%d]\n", shard_cas_id);
  shm_shard_cas_id = shard_cas_id;

  return 0;

#if 1
  /* SHARD TODO : tuning cur_keep_con parameter */
  as_info->cur_keep_con = 1;
#endif

  return 0;
return_error:

  if (shm_appl)
    {
      uw_shm_detach (shm_appl);
      shm_appl = NULL;
    }

  return -1;
}


static int
cas_register_to_proxy (SOCKET proxy_sock_fd)
{
  MSG_HEADER proxy_msg_header;
  char func_code = 0x01;

  /* proxy/cas connection handshake */
  init_msg_header (&proxy_msg_header);

  *(proxy_msg_header.msg_body_size_ptr) = sizeof (char) /* func_code */  +
    sizeof (int) /* shard_id */  +
    sizeof (int) /* cas_id */ ;

  if (net_write_header (proxy_sock_fd, &proxy_msg_header))
    {
      cas_log_write_and_end (0, false, "HANDSHAKE ERROR send msg_header");
      return -1;
    }

  if (net_write_stream (proxy_sock_fd, &func_code, 1) < 0)
    {
      cas_log_write_and_end (0, false, "HANDSHAKE ERROR send func_code");
      return -1;
    }

  if (net_write_int (proxy_sock_fd, shm_shard_id) < 0)
    {
      cas_log_write_and_end (0, false, "HANDSHAKE ERROR send shard_id");
      return -1;
    }

  if (net_write_int (proxy_sock_fd, shm_shard_cas_id) < 0)
    {
      cas_log_write_and_end (0, false, "HANDSHAKE ERROR send cas_id");
      return -1;
    }

  return 0;
}
