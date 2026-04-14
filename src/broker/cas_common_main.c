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
 * cas_common_main.c 
 */

#ident "$Id$"

#include <string.h>
#include <assert.h>
#include <strings.h>
#include <sys/time.h>
#include <time.h>

#if defined(WINDOWS)
#include <windows.h>
#include <dbgHelp.h>
#endif

#if !defined(WINDOWS)
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <time.h>
#else
#include <signal.h>
#include <time.h>
#endif

#include "cas_common_main.h"
#include "broker_shm.h"
#include "cas_log.h"
#include "cas_handle.h"
#include "error_manager.h"
#include "ddl_log.h"
#include "broker_process_size.h"
#include "cas_common_execute.h"
#include "cas_protocol.h"
#include "cas_common_vars.h"
#include "dbi.h"
#include "perf_monitor.h"
#include "cas_network.h"
#include "broker_env_def.h"
#include "broker_filename.h"
#include "cas_sql_log2.h"
#include "broker_acl.h"
#include "cas_ssl.h"
#include "broker_util.h"

#if !defined(WINDOWS)
#include "broker_recv_fd.h"
#include <netinet/tcp.h>
#endif

static int query_sequence_num;

FN_RETURN cas_main_fn_ret = FN_KEEP_CONN;

static cas_cleanup_callback_t cleanup_callback = NULL;
static cas_database_shutdown_callback_t database_shutdown_callback = NULL;

int
cas_main_loop (CAS_MAIN_OPS * ops)
{
  T_NET_BUF net_buf;
  SOCKET srv_sock_fd, br_sock_fd, client_sock_fd;
  char read_buf[1024];
  int err_code;
  int one = 1, db_info_size;
  int client_ip_addr;
  char cas_info[CAS_INFO_SIZE] = { CAS_INFO_STATUS_INACTIVE,
    CAS_INFO_RESERVED_DEFAULT,
    CAS_INFO_RESERVED_DEFAULT,
    CAS_INFO_RESERVED_DEFAULT
  };
  FN_RETURN fn_ret = FN_KEEP_CONN;
  char client_ip_str[16];
  bool is_new_connection = true;
  DB_CONN_INFO conn_info;
  prev_cas_info[CAS_INFO_STATUS] = CAS_INFO_RESERVED_DEFAULT;

  /* Initialize */
  if (cas_main_init (&net_buf, &srv_sock_fd) < 0)
    {
      return -1;
    }

  /* Mode-specific initialization */
  if (ops->init_specific && ops->init_specific () < 0)
    {
      return -1;
    }

#if defined(WINDOWS)
  __try
  {
#endif /* WINDOWS */
    for (;;)
      {
	ssl_client = false;
	error_info_clear ();
	cas_info[CAS_INFO_STATUS] = CAS_INFO_STATUS_INACTIVE;

	unset_hang_check_time ();
	br_sock_fd = net_connect_client (srv_sock_fd);

	if (IS_INVALID_SOCKET (br_sock_fd))
	  {
	    goto finish_cas;
	  }

	req_info.client_version = as_info->clt_version;
	memcpy (req_info.driver_info, as_info->driver_info, SRV_CON_CLIENT_INFO_SIZE);

	set_cas_info_size ();

#if defined(WINDOWS)
	as_info->uts_status = UTS_STATUS_BUSY;
#endif /* WINDOWS */
	as_info->fn_status = FN_STATUS_BUSY;
	as_info->con_status = CON_STATUS_IN_TRAN;
	as_info->transaction_start_time = time (0);
	errors_in_transaction = 0;

	client_ip_addr = 0;

	/* Accept client connection */
	if (cas_accept_client (br_sock_fd, &client_sock_fd, &client_ip_addr) < 0)
	  {
	    goto finish_cas;
	  }
	set_hang_check_time ();

	net_timeout_set (NET_DEFAULT_TIMEOUT);

	cas_log_open (broker_name);
	cas_slow_log_open (broker_name);
	as_info->cur_sql_log2 = shm_appl->sql_log2;
	sql_log2_init (broker_name, shm_as_index, as_info->cur_sql_log2, false);
	if (as_info->cas_err_log_reset == CAS_LOG_RESET_REOPEN)
	  {
	    set_cubrid_file (FID_CUBRID_ERR_DIR, shm_appl->err_log_dir);
	    as_db_err_log_set (broker_name, shm_proxy_id, shm_shard_id, shm_shard_cas_id, shm_as_index, cas_shard_flag);
	    er_final (ER_ALL_FINAL);
	    er_init (NULL, ER_NEVER_EXIT);
	    as_info->cas_err_log_reset = 0;
	  }

	ut_get_ipv4_string (client_ip_str, sizeof (client_ip_str), (unsigned char *) (&client_ip_addr));
	cas_log_write_and_end (0, false, "CLIENT IP %s", client_ip_str);
	setsockopt (client_sock_fd, IPPROTO_TCP, TCP_NODELAY, (char *) &one, sizeof (one));
	ut_set_keepalive (client_sock_fd);

	unset_hang_check_time ();

	if (IS_INVALID_SOCKET (client_sock_fd))
	  {
	    goto finish_cas;
	  }
#if !defined(WINDOWS)
	else
	  {
	    /* send NO_ERROR to client */
	    if (net_write_int (client_sock_fd, 0) < 0)
	      {
		CLOSE_SOCKET (client_sock_fd);
		goto finish_cas;
	      }
	  }
#endif
	req_info.client_version = as_info->clt_version;
	memcpy (req_info.driver_info, as_info->driver_info, SRV_CON_CLIENT_INFO_SIZE);
	cas_client_type = as_info->cas_client_type;

	if (req_info.client_version < CAS_MAKE_VER (8, 2, 0))
	  {
	    db_info_size = SRV_CON_DB_INFO_SIZE_PRIOR_8_2_0;
	  }
	else if (req_info.client_version < CAS_MAKE_VER (8, 4, 0))
	  {
	    db_info_size = SRV_CON_DB_INFO_SIZE_PRIOR_8_4_0;
	  }
	else
	  {
	    db_info_size = SRV_CON_DB_INFO_SIZE;
	  }

	if (IS_SSL_CLIENT (req_info.driver_info))
	  {
	    err_code = cas_init_ssl (client_sock_fd);
	    if (err_code < 0)
	      {
		net_write_error (client_sock_fd, req_info.client_version, req_info.driver_info, cas_info, cas_info_size,
				 CAS_ERROR_INDICATOR, CAS_ER_COMMUNICATION, NULL);
		goto finish_cas;
	      }
	  }

	if (net_read_stream (client_sock_fd, read_buf, db_info_size) < 0)
	  {
	    cas_info[CAS_INFO_STATUS] = CAS_INFO_STATUS_INACTIVE;
	    net_write_error (client_sock_fd, req_info.client_version, req_info.driver_info, cas_info, cas_info_size,
			     CAS_ERROR_INDICATOR, CAS_ER_COMMUNICATION, NULL);
	  }
	else
	  {
	    /* Parse DB connection information */
	    err_code = cas_parse_db_info (read_buf, db_info_size, &req_info, &conn_info);
	    if (err_code == -2)
	      {
		/* Health check request */
		cas_log_write_and_end (0, false, "Incoming health check request from client.");
		net_write_int (client_sock_fd, 0);
		cas_info[CAS_INFO_STATUS] = CAS_INFO_STATUS_ACTIVE;
		net_write_stream (client_sock_fd, cas_info, cas_info_size);
		cas_finish_session (client_sock_fd, ssl_client);
		goto finish_cas;
	      }
	    else if (err_code < 0)
	      {
		goto finish_cas;
	      }

	    if (ops->set_session_id)
	      {
		ops->set_session_id ((T_CAS_PROTOCOL) req_info.client_version, conn_info.db_sessionid);

		if (db_get_session_id () != DB_EMPTY_SESSION)
		  {
		    is_new_connection = false;
		  }
		else
		  {
		    is_new_connection = true;
		  }
	      }

	    set_hang_check_time ();

	    cas_log_debug (ARG_FILE_LINE, "db_name %s db_user %s url %s " "session id %s", conn_info.db_name,
			   conn_info.db_user, conn_info.url, conn_info.db_sessionid);
	    if (as_info->reset_flag == TRUE)
	      {
		cas_log_debug (ARG_FILE_LINE, "main: set reset_flag");
		if (ops->set_session_id)
		  {
		    cas_set_db_connect_status (-1);	/* DB_CONNECTION_STATUS_RESET */
		  }
		as_info->reset_flag = FALSE;
	      }

	    unset_hang_check_time ();

	    err_code =
	      cas_handle_db_connection (client_sock_fd, &req_info, &conn_info, cas_info, client_ip_addr, ops,
					is_new_connection);
	    if (err_code < 0)
	      {
		cas_finish_session (client_sock_fd, ssl_client);
		goto finish_cas;
	      }

	    set_hang_check_time ();

	    /* Common post-connection setup */
	    as_info->auto_commit_mode = FALSE;
	    cas_log_write_and_end (0, false, "DEFAULT isolation_level %d, " "lock_timeout %d",
				   cas_default_isolation_level, cas_default_lock_timeout);

	    as_info->cur_keep_con = shm_appl->keep_connection;
	    cas_bi_set_statement_pooling (shm_appl->statement_pooling);
	    if (shm_appl->statement_pooling)
	      {
		as_info->cur_statement_pooling = ON;
	      }
	    else
	      {
		as_info->cur_statement_pooling = OFF;
	      }
	    cas_bi_set_cci_pconnect (shm_appl->cci_pconnect);

	    if (DOES_CLIENT_UNDERSTAND_THE_PROTOCOL (req_info.client_version, PROTOCOL_V12))
	      {
		cas_bi_set_oracle_compat_number_behavior (prm_get_bool_value (PRM_ID_ORACLE_COMPAT_NUMBER_BEHAVIOR));
	      }

	    cas_info[CAS_INFO_STATUS] = CAS_INFO_STATUS_ACTIVE;
	    if (ops->send_connect_reply)
	      {
		ops->send_connect_reply ((T_CAS_PROTOCOL) req_info.client_version, client_sock_fd, cas_info);
	      }

	    as_info->cci_default_autocommit = shm_appl->cci_default_autocommit;
	    req_info.need_rollback = TRUE;

	    gettimeofday (&tran_start_time, NULL);
	    logddl_set_start_time (&tran_start_time);
	    gettimeofday (&query_start_time, NULL);
	    tran_timeout = 0;
	    query_timeout = 0;

	    cas_log_error_handler_begin ();

	    con_status_before_check_cas = -1;
	    is_first_request = true;

	    fn_ret = FN_KEEP_CONN;
	    cas_main_fn_ret = fn_ret;
	    while (fn_ret == FN_KEEP_CONN)
	      {
#if !defined(WINDOWS)
		signal (SIGUSR1, query_cancel);
#endif /* !WINDOWS */

		fn_ret = ops->process_request (client_sock_fd, &net_buf, &req_info, srv_sock_fd);
		cas_main_fn_ret = fn_ret;
		as_info->fn_status = FN_STATUS_DONE;

		is_first_request = false;

		cas_log_error_handler_clear ();
#if !defined(WINDOWS)
		signal (SIGUSR1, SIG_IGN);
#endif /* !WINDOWS */
		as_info->last_access_time = time (NULL);
	      }

	    prev_cas_info[CAS_INFO_STATUS] = CAS_INFO_RESERVED_DEFAULT;

	    if (as_info->cur_statement_pooling)
	      {
		hm_srv_handle_free_all (true);
	      }

	    /* Cleanup session */
	    if (ops->cleanup_session)
	      {
		ops->cleanup_session ();
	      }
	    cas_log_error_handler_end ();
	  }

	cas_finish_session (client_sock_fd, ssl_client);

      finish_cas:
	as_info->fn_status = FN_STATUS_IDLE;
	set_hang_check_time ();
#if defined(WINDOWS)
	as_info->close_flag = 1;
#endif /* WINDOWS */
	if (as_info->con_status != CON_STATUS_CLOSE_AND_CONNECT)
	  {
	    memset (as_info->cas_clt_ip, 0x0, sizeof (as_info->cas_clt_ip));
	    as_info->cas_clt_port = 0;
	    as_info->driver_version[0] = '\0';
	  }

	as_info->transaction_start_time = (time_t) 0;
	cas_log_write_and_end (0, true, "disconnect");
	cas_log_write2 (sql_log2_get_filename ());
	cas_log_write_and_end (0, false, "STATE idle");
	cas_log_close (true);
	cas_slow_log_close ();
	sql_log2_end (true);
#if defined(WINDOWS)
	cas_req_count++;
#endif /* WINDOWS */

	unset_hang_check_time ();

	if (is_server_aborted ())
	  {
#if defined(WINDOWS)
	    CLOSE_SOCKET (srv_sock_fd);
	    WSACleanup ();
#endif
	    cas_final ();
	    return 0;
	  }
	else if (!(as_info->cur_keep_con == KEEP_CON_AUTO && as_info->con_status == CON_STATUS_CLOSE_AND_CONNECT))
	  {
	    if (restart_is_needed ())
	      {
#if defined(WINDOWS)
		CLOSE_SOCKET (srv_sock_fd);
		WSACleanup ();
#endif
		cas_final ();
		return 0;
	      }
	    else
	      {
		as_info->uts_status = UTS_STATUS_IDLE;
	      }
	  }
      }
#if defined(WINDOWS)
  }
  __except (CreateMiniDump (GetExceptionInformation ()))
  {
  }
#endif /* WINDOWS */

  return 0;
}

/* cas_main() common initialization */
int
cas_main_init (T_NET_BUF * net_buf, SOCKET * srv_sock_fd)
{
#if defined(WINDOWS)
  int new_port;
#else
  char port_name[BROKER_PATH_MAX];
#endif /* WINDOWS */

#if defined(WINDOWS)
  if (shm_appl->as_port > 0)
    {
      new_port = shm_appl->as_port + shm_as_index;
    }
  else
    {
      new_port = 0;
    }
  *srv_sock_fd = net_init_env (&new_port);
#else /* WINDOWS */
  ut_get_as_port_name (port_name, broker_name, shm_as_index, BROKER_PATH_MAX);
  *srv_sock_fd = net_init_env (port_name);
#endif /* WINDOWS */
  if (IS_INVALID_SOCKET (*srv_sock_fd))
    {
      return -1;
    }

  net_buf_init (net_buf, cas_get_client_version ());
  net_buf->data = (char *) MALLOC (NET_BUF_ALLOC_SIZE);
  if (net_buf->data == NULL)
    {
      return -1;
    }
  net_buf->alloc_size = NET_BUF_ALLOC_SIZE;

  cas_log_open (broker_name);
  cas_slow_log_open (broker_name);
  cas_log_write_and_end (0, true, "CAS STARTED pid %d", getpid ());

#if defined(WINDOWS)
  as_info->as_port = new_port;
#endif /* WINDOWS */

  unset_hang_check_time ();

  as_info->service_ready_flag = TRUE;
  as_info->fn_status = FN_STATUS_CONN;
  as_info->con_status = CON_STATUS_IN_TRAN;
  as_info->transaction_start_time = time (0);
  as_info->cur_keep_con = KEEP_CON_DEFAULT;
  query_cancel_flag = 0;
  errors_in_transaction = 0;
#if !defined(WINDOWS)
  psize_at_start = as_info->psize = getsize (getpid ());
#endif /* !WINDOWS */
  if (shm_appl->appl_server_max_size > shm_appl->appl_server_hard_limit)
    {
      cas_log_write_and_end (0, true,
			     "CONFIGURATION WARNING - the APPL_SERVER_MAX_SIZE(%dM) is greater than the APPL_SERVER_MAX_SIZE_HARD_LIMIT(%dM)",
			     shm_appl->appl_server_max_size / ONE_K, shm_appl->appl_server_hard_limit / ONE_K);
    }

  stripped_column_name = shm_appl->stripped_column_name;

  // init error manager with default arguments; should be reinitialized later
  er_init (NULL, ER_NEVER_EXIT);

  logddl_init (APP_NAME_CAS);

  return 0;
}

/* Set cleanup callback (for CGW specific cleanup) */
void
cas_set_cleanup_callback (cas_cleanup_callback_t callback)
{
  cleanup_callback = callback;
}

void
cas_set_database_shutdown_callback (cas_database_shutdown_callback_t callback)
{
  database_shutdown_callback = callback;
}

int
cas_get_graceful_down_timeout (void)
{
  if (as_info->advance_activate_flag)
    {
      return -1;
    }

  return 1 * 60;		/* 1 min */
}

void
cas_sig_handler (int signo)
{
  static int is_doing_signal_handler = 0;

  if (is_doing_signal_handler)
    {
      return;
    }
  is_doing_signal_handler = 1;

  signal (signo, SIG_IGN);

  er_print_crash_callstack (signo);

  if (signo == SIGTERM || signo == SIGABRT || signo == SIGINT)
    {
      cas_free (true);
    }
  as_info->pid = 0;
  as_info->uts_status = UTS_STATUS_RESTART;

#ifdef _GCOV
  exit (0);
#else
  _exit (0);
#endif
}

void
cas_final (void)
{
  signal (SIGTERM, SIG_IGN);
  signal (SIGINT, SIG_IGN);
  cas_free (false);
  as_info->pid = 0;
  as_info->uts_status = UTS_STATUS_RESTART;
  er_final (ER_ALL_FINAL);
  exit (0);
}

void
cas_free (bool from_sighandler)
{
#ifdef MEM_DEBUG
  int fd;
#endif
  int max_process_size;

  if (from_sighandler)
    {
      cas_log_debug (ARG_FILE_LINE, "request cas_free() from the signal handler");
    }
  else
    {
      cas_log_debug (ARG_FILE_LINE, "request cas_free() from the cas_final()");
    }

  if (as_info->cur_statement_pooling && !from_sighandler)
    {
      hm_srv_handle_free_all (true);
    }
#if defined(WINDOWS)
  if (shm_appl->use_pdh_flag)
    {
      if ((as_info->pid == as_info->pdh_pid) && (as_info->pdh_workset > shm_appl->appl_server_max_size))
	{
	  if (cas_log_get_fd_status () == CAS_LOG_FD_OPENED)
	    {
	      cas_log_write_and_end (0, true, "CAS MEMORY USAGE (%dM) HAS EXCEEDED MAX SIZE (%dM)",
				     as_info->pdh_workset / ONE_K, shm_appl->appl_server_max_size / ONE_K);
	    }
	  else
	    {
	      cas_log_open_and_write (broker_name, 0, true,
				      "CAS MEMORY USAGE (%dM) HAS EXCEEDED MAX SIZE (%dM)",
				      as_info->pdh_workset / ONE_K, shm_appl->appl_server_max_size / ONE_K);
	    }
	}

      if ((as_info->pid == as_info->pdh_pid) && (as_info->pdh_workset > shm_appl->appl_server_hard_limit))
	{
	  if (cas_log_get_fd_status () == CAS_LOG_FD_OPENED)
	    {
	      cas_log_write_and_end (0, true, "CAS MEMORY USAGE (%dM) HAS EXCEEDED HARD LIMIT (%dM)",
				     as_info->pdh_workset / ONE_K, shm_appl->appl_server_hard_limit / ONE_K);
	    }
	  else
	    {
	      cas_log_open_and_write (broker_name, 0, true, "CAS MEMORY USAGE (%dM) HAS EXCEEDED HARD LIMIT (%dM)",
				      as_info->pdh_workset / ONE_K, shm_appl->appl_server_hard_limit / ONE_K);
	    }
	}
    }
  else
    {
      if (cas_req_count > 500)
	{
	  if (cas_log_get_fd_status () == CAS_LOG_FD_OPENED)
	    {
	      cas_log_write_and_end (0, true, "CAS REQUEST COUNT (%d) HAS EXCEEDED MAX LIMIT (%d)", cas_req_count, 500);
	    }
	  else
	    {
	      cas_log_open_and_write (broker_name, 0, true, "CAS REQUEST COUNT (%d) HAS EXCEEDED MAX LIMIT (%d)",
				      cas_req_count, 500);
	    }
	}
    }
#else /* WINDOWS */
#if defined(AIX)
  /* In linux, getsize() returns VSM(55M). but in AIX, getsize() returns vritual meory size for data(900K). so, the
   * size of cub_cas process exceeds 'psize_at_start * 2' very easily. the linux's rule to restart cub_cas is not suit
   * for AIX. In AIX, we use 20M as max_process_size. */
  max_process_size = (shm_appl->appl_server_max_size > 0) ? shm_appl->appl_server_max_size : 20 * ONE_K;
#else
  max_process_size = (shm_appl->appl_server_max_size > 0) ? shm_appl->appl_server_max_size : (psize_at_start * 10);
#endif
  if (as_info->psize > max_process_size)
    {

      if (cas_log_get_fd_status () == CAS_LOG_FD_OPENED)
	{
	  cas_log_write_and_end (0, true, "CAS MEMORY USAGE (%dM) HAS EXCEEDED MAX SIZE (%dM)", as_info->psize / ONE_K,
				 max_process_size / ONE_K);
	}
      else
	{
	  cas_log_open_and_write (broker_name, 0, true, "CAS MEMORY USAGE (%dM) HAS EXCEEDED MAX SIZE (%dM)",
				  as_info->psize / ONE_K, max_process_size / ONE_K);
	}
    }

  if (as_info->psize > shm_appl->appl_server_hard_limit)
    {
      if (cas_log_get_fd_status () == CAS_LOG_FD_OPENED)
	{
	  cas_log_write_and_end (0, true, "CAS MEMORY USAGE (%dM) HAS EXCEEDED HARD LIMIT (%dM)",
				 as_info->psize / ONE_K, shm_appl->appl_server_hard_limit / ONE_K);
	}
      else
	{
	  cas_log_open_and_write (broker_name, 0, true, "CAS MEMORY USAGE (%dM) HAS EXCEEDED HARD LIMIT (%dM)",
				  as_info->psize / ONE_K, shm_appl->appl_server_hard_limit / ONE_K);
	}
    }
#endif /* !WINDOWS */
  if (cas_log_get_fd_status () == CAS_LOG_FD_OPENED)
    {
      cas_log_write_and_end (0, true, "CAS TERMINATED pid %d", getpid ());
    }
  else
    {
      cas_log_open_and_write (broker_name, 0, true, "CAS TERMINATED pid %d", getpid ());
    }

  cas_log_close (true);
  cas_slow_log_close ();
  logddl_destroy ();

#ifdef MEM_DEBUG
  fd = open ("mem_debug.log", O_CREAT | O_TRUNC | O_WRONLY, 0666);
  if (fd > 0)
    {
      malloc_dump (fd);
      close (fd);
    }
#endif

  if (cleanup_callback != NULL)
    {
      cleanup_callback ();
    }

  if (database_shutdown_callback != NULL)
    {
      if (from_sighandler)
	{
	  database_shutdown_callback (false);
	}
      else
	{
	  database_shutdown_callback (true);
	}
    }
}

/*
 * set_hang_check_time() -
 *   Mark the current time so that cas hang checker thread
 *   in broker can monitor the status of the cas.
 *   If the time is set, ALWAYS unset it
 *   before meeting indefinite blocking operation.
 */
void
set_hang_check_time (void)
{
  if (cas_shard_flag == OFF && as_info != NULL && shm_appl != NULL && shm_appl->monitor_hang_flag)
    {
      as_info->claimed_alive_time = time (NULL);
    }
  return;
}

/*
 * unset_hang_check_time -
 *   Clear the time and the cas is free from being monitored
 *   by hang checker in broker.
 */
void
unset_hang_check_time (void)
{
  if (cas_shard_flag == OFF && as_info != NULL && shm_appl != NULL && shm_appl->monitor_hang_flag)
    {
      as_info->claimed_alive_time = (time_t) 0;
    }
  return;
}

bool
check_server_alive (const char *db_name, const char *db_host)
{
  int i, u_index;
  char *unusable_db_name;
  char *unusable_db_host;
  const char *check_db_host = db_host;
  const char *check_db_name = db_name;

  if (cas_shard_flag == OFF && as_info != NULL && shm_appl != NULL && shm_appl->monitor_server_flag)
    {
      /* if db_name is NULL, use the CAS shared memory */
      if (db_name == NULL)
	{
	  check_db_name = as_info->database_name;
	}

      /* if db_host is NULL, use the CAS shared memory */
      if (db_host == NULL)
	{
	  check_db_host = as_info->database_host;
	}

      u_index = shm_appl->unusable_databases_seq % 2;

      for (i = 0; i < shm_appl->unusable_databases_cnt[u_index]; i++)
	{
	  unusable_db_name = shm_appl->unusable_databases[u_index][i].database_name;
	  unusable_db_host = shm_appl->unusable_databases[u_index][i].database_host;

	  if (strcmp (unusable_db_name, check_db_name) == 0 && strcmp (unusable_db_host, check_db_host) == 0)
	    {
	      return false;
	    }
	}
    }

  return true;
}

void
query_cancel (int signo)
{
#if !defined(WINDOWS)
  struct timespec ts;
  signal (signo, SIG_IGN);
  db_set_interrupt (1);
  as_info->num_interrupts %= MAX_DIAG_DATA_VALUE;
  as_info->num_interrupts++;

  clock_gettime (CLOCK_REALTIME, &ts);
  query_cancel_time = ts.tv_sec * 1000LL;
  query_cancel_time += (ts.tv_nsec / 1000000LL);
  query_cancel_flag = 1;
#else
  assert (0);
#endif /* !WINDOWS */
}

void
set_cas_info_size (void)
{
  if (cas_shard_flag == OFF && as_info->clt_version <= CAS_MAKE_VER (8, 1, 5))
    {
      cas_info_size = 0;
    }
  else
    {
      cas_info_size = CAS_INFO_SIZE;
    }
}

int
restart_is_needed (void)
{
  if (as_info->num_holdable_results > 0 || as_info->cas_change_mode == CAS_CHANGE_MODE_KEEP)
    {
      /* we do not want to restart the CAS when there are open holdable results or cas_change_mode is
       * CAS_CHANGE_MODE_KEEP */
      return 0;
    }
#if defined(WINDOWS)
  if (shm_appl->use_pdh_flag == TRUE)
    {
      if ((as_info->pid == as_info->pdh_pid) && (as_info->pdh_workset > shm_appl->appl_server_max_size))
	{
	  return 1;
	}
      else
	{
	  return 0;
	}
    }
  else
    {
      if (cas_req_count > 500)
	return 1;
      else
	return 0;
    }
#else /* WINDOWS */
  int max_process_size;

#if defined(AIX)
  /* In linux, getsize() returns VSM(55M). but in AIX, getsize() returns vritual meory size for data(900K). so, the
   * size of cub_cas process exceeds 'psize_at_start * 2' very easily. the linux's rule to restart cub_cas is not suit
   * for AIX. In AIX, we use 20M as max_process_size. */
  max_process_size = (shm_appl->appl_server_max_size > 0) ? shm_appl->appl_server_max_size : 20 * ONE_K;
#else
  max_process_size = (shm_appl->appl_server_max_size > 0) ? shm_appl->appl_server_max_size : (psize_at_start * 10);
#endif

  if (as_info->psize > max_process_size)
    {
      return 1;
    }
  else
    {
      return 0;
    }
#endif /* !WINDOWS */
}

int
query_seq_num_next_value (void)
{
  return ++query_sequence_num;
}

int
query_seq_num_current_value (void)
{
  return query_sequence_num;
}

T_BROKER_VERSION
cas_get_client_version (void)
{
  return req_info.client_version;
}

int
net_read_header_keep_con_on (SOCKET clt_sock_fd, MSG_HEADER * client_msg_header)
{
  int ret_value = 0;
  int timeout = 0, remained_timeout = 0;

  if (as_info->con_status == CON_STATUS_IN_TRAN)
    {
      net_timeout_set (shm_appl->session_timeout);
    }
  else
    {
      net_timeout_set (DEFAULT_CHECK_INTERVAL);
      timeout = shm_appl->session_timeout;
      remained_timeout = timeout;
    }

  do
    {
      if (as_info->con_status == CON_STATUS_OUT_TRAN)
	{
	  remained_timeout -= DEFAULT_CHECK_INTERVAL;
	}

      if (net_read_header (clt_sock_fd, client_msg_header) < 0)
	{
	  /* if in-transaction state, return network error */
	  if (as_info->con_status == CON_STATUS_IN_TRAN || !is_net_timed_out ())
	    {
	      ret_value = -1;
	      break;
	    }
	  /* if out-of-transaction state, check whether restart is needed */
	  if (as_info->con_status == CON_STATUS_OUT_TRAN && is_net_timed_out ())
	    {
	      if (as_info->reset_flag == TRUE)
		{
		  ret_value = -1;
		  break;
		}

	      if (timeout > 0 && remained_timeout <= 0)
		{
		  ret_value = -1;
		  break;
		}
	    }
	}
      else
	{
	  break;
	}
    }
  while (1);

  return ret_value;
}

#if defined(WINDOWS)

LONG WINAPI
CreateMiniDump (struct _EXCEPTION_POINTERS * pException)
{
  TCHAR DumpFile[MAX_PATH] = { 0, };
  TCHAR DumpPath[MAX_PATH] = { 0, };
  SYSTEMTIME SystemTime;
  HANDLE FileHandle;

  GetLocalTime (&SystemTime);

  sprintf (DumpFile, "%d-%d-%d %d_%d_%d.dmp", SystemTime.wYear, SystemTime.wMonth, SystemTime.wDay, SystemTime.wHour,
	   SystemTime.wMinute, SystemTime.wSecond);
  envvar_bindir_file (DumpPath, MAX_PATH, DumpFile);

  FileHandle = CreateFile (DumpPath, GENERIC_WRITE, FILE_SHARE_WRITE, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

  if (FileHandle != INVALID_HANDLE_VALUE)
    {
      MINIDUMP_EXCEPTION_INFORMATION MiniDumpExceptionInfo;
      BOOL Success;

      MiniDumpExceptionInfo.ThreadId = GetCurrentThreadId ();
      MiniDumpExceptionInfo.ExceptionPointers = pException;
      MiniDumpExceptionInfo.ClientPointers = FALSE;

      Success =
	MiniDumpWriteDump (GetCurrentProcess (), GetCurrentProcessId (), FileHandle, MiniDumpNormal,
			   (pException) ? &MiniDumpExceptionInfo : NULL, NULL, NULL);
    }

  CloseHandle (FileHandle);

  ux_database_shutdown (true);

  return EXCEPTION_EXECUTE_HANDLER;
}
#endif /* WINDOWS */

/* Accept client connection and perform handshake */
int
cas_accept_client (SOCKET br_sock_fd, SOCKET * client_sock_fd, int *client_ip_addr)
{
#if defined(WINDOWS)
  static int one = 1;
  *client_sock_fd = br_sock_fd;
  if (ioctlsocket (*client_sock_fd, FIONBIO, (u_long *) (&one)) < 0)
    {
      return -1;
    }
  memcpy (client_ip_addr, as_info->cas_clt_ip, 4);
#else /* WINDOWS */
  int con_status;
  char do_not_use_driver_info[SRV_CON_CLIENT_INFO_SIZE];

  net_timeout_set (NET_MIN_TIMEOUT);

  if (net_read_int (br_sock_fd, &con_status) < 0)
    {
      cas_log_write_and_end (0, false, "HANDSHAKE ERROR net_read_int(con_status)");
      CLOSE_SOCKET (br_sock_fd);
      return -1;
    }
  if (net_write_int (br_sock_fd, as_info->con_status) < 0)
    {
      cas_log_write_and_end (0, false, "HANDSHAKE ERROR net_write_int(con_status)");
      CLOSE_SOCKET (br_sock_fd);
      return -1;
    }

  *client_sock_fd = recv_fd (br_sock_fd, client_ip_addr, do_not_use_driver_info);
  if (*client_sock_fd == -1)
    {
      cas_log_write_and_end (0, false, "HANDSHAKE ERROR recv_fd %d", *client_sock_fd);
      CLOSE_SOCKET (br_sock_fd);
      return -1;
    }
  if (net_write_int (br_sock_fd, as_info->uts_status) < 0)
    {
      cas_log_write_and_end (0, false, "HANDSHAKE ERROR net_write_int(uts_status)");
      CLOSE_SOCKET (br_sock_fd);
      CLOSE_SOCKET (*client_sock_fd);
      return -1;
    }

  CLOSE_SOCKET (br_sock_fd);
#endif /* WINDOWS */

  return 0;
}

/* Parse DB connection information from read buffer */
int
cas_parse_db_info (char *read_buf, int db_info_size, T_REQ_INFO * req_info, DB_CONN_INFO * conn_info)
{
  int len;
  char *db_name, *db_user, *db_passwd, *url, *db_sessionid;

  db_name = read_buf;
  db_name[SRV_CON_DBNAME_SIZE - 1] = '\0';

  /* Send response to broker health checker */
  if (strcmp (db_name, HEALTH_CHECK_DUMMY_DB) == 0)
    {
      return -2;		/* Special return value for health check */
    }

  db_user = db_name + SRV_CON_DBNAME_SIZE;
  db_user[SRV_CON_DBUSER_SIZE - 1] = '\0';
  if (db_user[0] == '\0')
    {
      strcpy (db_user, "PUBLIC");
    }

  db_passwd = db_user + SRV_CON_DBUSER_SIZE;
  db_passwd[SRV_CON_DBPASSWD_SIZE - 1] = '\0';

  if (req_info->client_version >= CAS_MAKE_VER (8, 2, 0))
    {
      url = db_passwd + SRV_CON_DBPASSWD_SIZE;
      url[SRV_CON_URL_SIZE - 1] = '\0';
    }
  else
    {
      url = NULL;
    }

  if (req_info->client_version >= CAS_MAKE_VER (8, 4, 0))
    {
      assert (url != NULL);
      db_sessionid = url + SRV_CON_URL_SIZE;
      db_sessionid[SRV_CON_DBSESS_ID_SIZE - 1] = '\0';
    }
  else
    {
      /* even drivers do not send session id (under RB-8.4.0) the cas_set_session_id() should be called */
      db_sessionid = NULL;
    }

  as_info->driver_version[0] = '\0';
  if (DOES_CLIENT_UNDERSTAND_THE_PROTOCOL (req_info->client_version, PROTOCOL_V5))
    {
      assert (url != NULL);
      len = *(url + strlen (url) + 1);
      if (len > 0 && len < SRV_CON_VER_STR_MAX_SIZE)
	{
	  memcpy (as_info->driver_version, url + strlen (url) + 2, (int) len);
	  as_info->driver_version[len] = '\0';
	}
      else
	{
	  snprintf (as_info->driver_version, SRV_CON_VER_STR_MAX_SIZE, "PROTOCOL V%d",
		    (int) (CAS_PROTO_VER_MASK & req_info->client_version));
	}
    }
  else if (DOES_CLIENT_UNDERSTAND_THE_PROTOCOL (req_info->client_version, PROTOCOL_V1))
    {
      char *ver;
      CAS_PROTO_TO_VER_STR (&ver, (int) (CAS_PROTO_VER_MASK & req_info->client_version));
      strncpy_bufsize (as_info->driver_version, ver);
    }
  else
    {
      snprintf (as_info->driver_version, SRV_CON_VER_STR_MAX_SIZE, "%d.%d.%d",
		CAS_VER_TO_MAJOR (req_info->client_version), CAS_VER_TO_MINOR (req_info->client_version),
		CAS_VER_TO_PATCH (req_info->client_version));
    }
  cas_log_write_and_end (0, false, "CLIENT VERSION %s", as_info->driver_version);

  conn_info->db_name = db_name;
  conn_info->db_user = db_user;
  conn_info->db_passwd = db_passwd;
  conn_info->url = url;
  conn_info->db_sessionid = db_sessionid;

  return 0;
}

int
cas_handle_db_connection (SOCKET client_sock_fd, T_REQ_INFO * req_info,
			  DB_CONN_INFO * conn_info, char *cas_info, int client_ip_addr, CAS_MAIN_OPS * ops,
			  bool is_new_connection)
{
  int err_code;
  unsigned char *ip_addr;
  char client_ip_str[16];
  struct timeval cas_start_time;

  gettimeofday (&cas_start_time, NULL);

  /* Pre-connect processing */
  if (ops->pre_db_connect)
    {
      err_code = ops->pre_db_connect (conn_info->db_name, conn_info->db_user,
				      conn_info->db_passwd, conn_info->url, ops->context);
      if (err_code < 0)
	{
	  char err_msg[1024];
	  if (conn_info->url && strstr (conn_info->url, "__gateway=true") == NULL)
	    {
	      sprintf (err_msg, "Authorization error");
	    }
	  else
	    {
	      sprintf (err_msg, "%s is not supported DBMS.", shm_appl->cgw_link_server);
	    }
	  cas_info[CAS_INFO_STATUS] = CAS_INFO_STATUS_INACTIVE;
	  net_write_error (client_sock_fd, req_info->client_version, req_info->driver_info, cas_info, cas_info_size,
			   DBMS_ERROR_INDICATOR, CAS_ER_NOT_AUTHORIZED_CLIENT, err_msg);
	  return err_code;
	}
    }

  /* Access control check */
  ip_addr = (unsigned char *) (&client_ip_addr);
  if (shm_appl->access_control)
    {
      if (access_control_check_right (shm_appl, conn_info->db_name, conn_info->db_user, ip_addr) < 0)
	{
	  char err_msg[1024];
	  as_info->num_connect_rejected++;
	  sprintf (err_msg, "Authorization error.(Address is rejected)");
	  cas_info[CAS_INFO_STATUS] = CAS_INFO_STATUS_INACTIVE;
	  net_write_error (client_sock_fd, req_info->client_version, req_info->driver_info, cas_info, cas_info_size,
			   DBMS_ERROR_INDICATOR, CAS_ER_NOT_AUTHORIZED_CLIENT, err_msg);
	  set_hang_check_time ();
	  cas_log_write_and_end (0, false, "connect db %s user %s url %s - rejected", conn_info->db_name,
				 conn_info->db_user, conn_info->url);
	  if (shm_appl->access_log == ON)
	    {
	      cas_access_log (&cas_start_time, shm_as_index, client_ip_addr, conn_info->db_name, conn_info->db_user,
			      ACL_REJECTED);
	    }
	  unset_hang_check_time ();
	  return -1;
	}
    }

  /* DB connection */
  err_code =
    ops->db_connect (client_sock_fd, conn_info->db_name, conn_info->db_user, conn_info->db_passwd, conn_info->url,
		     req_info, cas_info);
  if (err_code < 0)
    {
      return -1;
    }

  /* Post-connect processing */
  if (ops->post_db_connect)
    {
      ops->post_db_connect (ops->context, &cas_start_time, shm_as_index, client_ip_addr, conn_info->db_name,
			    conn_info->db_user, conn_info->url, is_new_connection);
    }

  ut_get_ipv4_string (client_ip_str, sizeof (client_ip_str), (unsigned char *) (&client_ip_addr));
  logddl_check_ddl_audit_param ();
  logddl_set_broker_info (shm_as_index, shm_appl->broker_name);
  logddl_set_ip (client_ip_str);
  db_set_client_ip_addr (client_ip_str);

  return 0;
}

void
cas_finish_session (SOCKET client_sock_fd, bool ssl_client)
{
  CLOSE_SOCKET (client_sock_fd);
  if (ssl_client)
    {
      cas_ssl_close (client_sock_fd);
    }
}

void
cas_set_db_connect_status (int status)
{
  db_set_connect_status (status);
}

int
cas_get_db_connect_status (void)
{
  return db_get_connect_status ();
}

int
net_read_int_keep_con_auto (SOCKET clt_sock_fd, MSG_HEADER * client_msg_header, T_REQ_INFO * req_info,
			    SOCKET srv_sock_fd)
{
  int ret_value = 0;

  if (as_info->con_status == CON_STATUS_IN_TRAN)
    {
      /* holdable results have the same lifespan of a normal session */
      net_timeout_set (shm_appl->session_timeout);
    }
  else
    {
      net_timeout_set (DEFAULT_CHECK_INTERVAL);

      new_req_sock_fd = srv_sock_fd;
    }

  do
    {
      if (as_info->cas_log_reset)
	{
	  cas_log_reset (broker_name);
	}
      if (as_info->cas_slow_log_reset)
	{
	  cas_slow_log_reset (broker_name);
	}

      if (as_info->con_status != CON_STATUS_IN_TRAN && as_info->reset_flag == TRUE)
	{
	  return -1;
	}

      if (as_info->con_status == CON_STATUS_CLOSE || as_info->con_status == CON_STATUS_CLOSE_AND_CONNECT)
	{
	  break;
	}

      if (net_read_header (clt_sock_fd, client_msg_header) < 0)
	{
	  /* if in-transaction state, return network error */
	  if (as_info->con_status == CON_STATUS_IN_TRAN || !is_net_timed_out ())
	    {
	      ret_value = -1;
	      break;
	    }
	  /* if out-of-transaction state, check whether restart is needed */
	  if (as_info->con_status == CON_STATUS_OUT_TRAN && is_net_timed_out ())
	    {
	      if (restart_is_needed ())
		{
		  cas_log_debug (ARG_FILE_LINE, "net_read_int_keep_con_auto: " "restart_is_needed()");
		  ret_value = -1;
		  break;
		}

	      if (as_info->reset_flag == TRUE)
		{
		  ret_value = -1;
		  break;
		}
	    }
	}
      else
	{
	  break;
	}
    }
  while (1);

  new_req_sock_fd = INVALID_SOCKET;

  CON_STATUS_LOCK (&(shm_appl->as_info[shm_as_index]), CON_STATUS_LOCK_CAS);

  if (as_info->con_status == CON_STATUS_OUT_TRAN)
    {
      as_info->num_request++;
      gettimeofday (&tran_start_time, NULL);
    }
  logddl_set_start_time (&tran_start_time);

  if (as_info->con_status == CON_STATUS_CLOSE || as_info->con_status == CON_STATUS_CLOSE_AND_CONNECT)
    {
      ret_value = -1;
    }
  else
    {
      if (as_info->con_status != CON_STATUS_IN_TRAN)
	{
	  as_info->con_status = CON_STATUS_IN_TRAN;
	  as_info->transaction_start_time = time (0);
	  errors_in_transaction = 0;
	}
    }

  CON_STATUS_UNLOCK (&(shm_appl->as_info[shm_as_index]), CON_STATUS_LOCK_CAS);

  return ret_value;
}
