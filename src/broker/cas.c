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
#include "cas.h"
#include "cas_network.h"
#include "cas_function.h"
#include "cas_net_buf.h"
#include "cas_log.h"
#include "cas_util.h"
#include "broker_filename.h"
#include "cas_execute.h"

#if defined(CAS_FOR_ORACLE)
#include "cas_oracle.h"
#include "cas_error_log.h"
#elif defined(CAS_FOR_MYSQL)
#include "cas_mysql.h"
#include "cas_error_log.h"
#endif /* CAS_FOR_MYSQL */

#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
#include "connection_support.h"
#include "perf_monitor.h"
#endif /* !CAS_FOR_ORACLE && !CAS_FOR_MYSQL */

#if !defined(WINDOWS)
#include "broker_recv_fd.h"
#endif /* !WINDOWS */

#include "broker_shm.h"
#include "broker_util.h"
#include "broker_env_def.h"
#include "broker_process_size.h"
#include "cas_sql_log2.h"
#include "broker_acl.h"
#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
#include "environment_variable.h"
#endif /* !CAS_FOR_ORACLE && !CAS_FOR_MYSQL */

static const int DEFAULT_CHECK_INTERVAL = 1;

#define FUNC_NEEDS_RESTORING_CON_STATUS(func_code) \
  (((func_code) == CAS_FC_GET_DB_PARAMETER) \
   ||((func_code) == CAS_FC_SET_DB_PARAMETER) \
   ||((func_code) == CAS_FC_CLOSE_REQ_HANDLE) \
   ||((func_code) == CAS_FC_GET_DB_VERSION) \
   ||((func_code) == CAS_FC_GET_ATTR_TYPE_STR) \
   ||((func_code) == CAS_FC_CURSOR_CLOSE) \
   ||((func_code) == CAS_FC_END_SESSION)  \
   ||((func_code) == CAS_FC_CAS_CHANGE_MODE))

static FN_RETURN process_request (SOCKET sock_fd, T_NET_BUF * net_buf,
				  T_REQ_INFO * req_info);

#if defined(WINDOWS)
LONG WINAPI CreateMiniDump (struct _EXCEPTION_POINTERS *pException);
#endif /* WINDOWS */

#ifndef LIBCAS_FOR_JSP
static int cas_main (void);
static int shard_cas_main (void);
static void cas_sig_handler (int signo);
static int cas_init (void);
static void cas_final (void);
static void cas_free (bool free_srv_handle);
static void query_cancel (int signo);

static int cas_init_shm (void);
static int cas_register_to_proxy (SOCKET proxy_sock_fd);
static int net_read_process (SOCKET proxy_sock_fd,
			     MSG_HEADER * client_msg_header,
			     T_REQ_INFO * req_info);
static int get_graceful_down_timeout (void);
#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
static void set_db_parameter (void);
#endif /* !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL) */

static int net_read_int_keep_con_auto (SOCKET clt_sock_fd,
				       MSG_HEADER * client_msg_header,
				       T_REQ_INFO * req_info);
static int net_read_header_keep_con_on (SOCKET clt_sock_fd,
					MSG_HEADER * client_msg_header);
static void set_db_connection_info (void);
static void clear_db_connection_info (void);
static bool need_database_reconnect (void);

#else /* !LIBCAS_FOR_JSP */
extern int libcas_main (SOCKET jsp_sock_fd);
extern void *libcas_get_db_result_set (int h_id);
extern void libcas_srv_handle_free (int h_id);
#endif /* !LIBCAS_FOR_JSP */

static void set_cas_info_size (void);

static char cas_db_name[MAX_HA_DBINFO_LENGTH];
static char cas_db_user[SRV_CON_DBUSER_SIZE];
static char cas_db_passwd[SRV_CON_DBPASSWD_SIZE];
static int query_sequence_num = 0;

int cas_shard_flag = OFF;
int shm_shard_id = SHARD_ID_UNSUPPORTED;

#ifndef LIBCAS_FOR_JSP
const char *program_name;
char broker_name[BROKER_NAME_LEN];
int psize_at_start;

int shm_as_index;
T_SHM_APPL_SERVER *shm_appl;
T_APPL_SERVER_INFO *as_info;
int shm_proxy_id = -1;
int shm_shard_cas_id = -1;

struct timeval tran_start_time;
struct timeval query_start_time;
int tran_timeout = 0;
int query_timeout = 0;
INT64 query_cancel_time;
char query_cancel_flag;

bool autocommit_deferred = false;
#endif /* !LIBCAS_FOR_JSP */

int errors_in_transaction = 0;
char stripped_column_name;
char cas_client_type;

#ifndef LIBCAS_FOR_JSP
int con_status_before_check_cas;
bool is_first_request;
SOCKET new_req_sock_fd = INVALID_SOCKET;
#endif /* !LIBCAS_FOR_JSP */
int cas_default_isolation_level = 0;
int cas_default_lock_timeout = -1;
bool cas_default_ansi_quotes = true;
#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
bool cas_default_no_backslash_escapes = true;
#endif /* !CAS_FOR_ORACLE && !CAS_FOR_MYSQL */
int cas_send_result_flag = TRUE;
int cas_info_size = CAS_INFO_SIZE;
char prev_cas_info[CAS_INFO_SIZE];

T_ERROR_INFO err_info;

#if defined(CAS_FOR_ORACLE) || defined(CAS_FOR_MYSQL)
static T_SERVER_FUNC server_fn_table[] = {
  fn_end_tran,			/* CAS_FC_END_TRAN */
  fn_prepare,			/* CAS_FC_PREPARE */
  fn_execute,			/* CAS_FC_EXECUTE */
  fn_not_supported,		/* CAS_FC_GET_DB_PARAMETER */
  fn_not_supported,		/* CAS_FC_SET_DB_PARAMETER */
  fn_close_req_handle,		/* CAS_FC_CLOSE_REQ_HANDLE */
  fn_not_supported,		/* CAS_FC_CURSOR */
  fn_fetch,			/* CAS_FC_FETCH */
  fn_not_supported,		/* CAS_FC_SCHEMA_INFO */
  fn_not_supported,		/* CAS_FC_OID_GET */
  fn_not_supported,		/* CAS_FC_OID_SET */
  fn_not_supported,		/* CAS_FC_DEPRECATED1 */
  fn_not_supported,		/* CAS_FC_DEPRECATED2 */
  fn_not_supported,		/* CAS_FC_DEPRECATED3 */
  fn_get_db_version,		/* CAS_FC_GET_DB_VERSION */
  fn_not_supported,		/* CAS_FC_GET_CLASS_NUM_OBJS */
  fn_not_supported,		/* CAS_FC_OID_CMD */
  fn_not_supported,		/* CAS_FC_COLLECTION */
  fn_not_supported,		/* CAS_FC_NEXT_RESULT */
  fn_not_supported,		/* CAS_FC_EXECUTE_BATCH */
  fn_execute_array,		/* CAS_FC_EXECUTE_ARRAY */
  fn_not_supported,		/* CAS_FC_CURSOR_UPDATE */
  fn_not_supported,		/* CAS_FC_GET_ATTR_TYPE_STR */
  fn_not_supported,		/* CAS_FC_GET_QUERY_INFO */
  fn_not_supported,		/* CAS_FC_DEPRECATED4 */
  fn_not_supported,		/* CAS_FC_SAVEPOINT */
  fn_not_supported,		/* CAS_FC_PARAMETER_INFO */
  fn_not_supported,		/* CAS_FC_XA_PREPARE */
  fn_not_supported,		/* CAS_FC_XA_RECOVER */
  fn_not_supported,		/* CAS_FC_XA_END_TRAN */
  fn_con_close,			/* CAS_FC_CON_CLOSE */
  fn_check_cas,			/* CAS_FC_CHECK_CAS */
  fn_make_out_rs,		/* CAS_FC_MAKE_OUT_RS */
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
#else /* CAS_FOR_ORACLE || CAS_FOR_MYSQL */
static T_SERVER_FUNC server_fn_table[] = {
  fn_end_tran,			/* CAS_FC_END_TRAN */
  fn_prepare,			/* CAS_FC_PREPARE */
  fn_execute,			/* CAS_FC_EXECUTE */
  fn_get_db_parameter,		/* CAS_FC_GET_DB_PARAMETER */
  fn_set_db_parameter,		/* CAS_FC_SET_DB_PARAMETER */
  fn_close_req_handle,		/* CAS_FC_CLOSE_REQ_HANDLE */
  fn_cursor,			/* CAS_FC_CURSOR */
  fn_fetch,			/* CAS_FC_FETCH */
  fn_schema_info,		/* CAS_FC_SCHEMA_INFO */
  fn_oid_get,			/* CAS_FC_OID_GET */
  fn_oid_put,			/* CAS_FC_OID_SET */
  fn_deprecated,		/* CAS_FC_DEPRECATED1 *//* fn_glo_new */
  fn_deprecated,		/* CAS_FC_DEPRECATED2 *//* fn_glo_save */
  fn_deprecated,		/* CAS_FC_DEPRECATED3 *//* fn_glo_load */
  fn_get_db_version,		/* CAS_FC_GET_DB_VERSION */
  fn_get_class_num_objs,	/* CAS_FC_GET_CLASS_NUM_OBJS */
  fn_oid,			/* CAS_FC_OID_CMD */
  fn_collection,		/* CAS_FC_COLLECTION */
  fn_next_result,		/* CAS_FC_NEXT_RESULT */
  fn_execute_batch,		/* CAS_FC_EXECUTE_BATCH */
  fn_execute_array,		/* CAS_FC_EXECUTE_ARRAY */
  fn_cursor_update,		/* CAS_FC_CURSOR_UPDATE */
  fn_get_attr_type_str,		/* CAS_FC_GET_ATTR_TYPE_STR */
  fn_get_query_info,		/* CAS_FC_GET_QUERY_INFO */
  fn_deprecated,		/* CAS_FC_DEPRECATED4 *//* fn_glo_cmd */
  fn_savepoint,			/* CAS_FC_SAVEPOINT */
  fn_parameter_info,		/* CAS_FC_PARAMETER_INFO */
  fn_xa_prepare,		/* CAS_FC_XA_PREPARE */
  fn_xa_recover,		/* CAS_FC_XA_RECOVER */
  fn_xa_end_tran,		/* CAS_FC_XA_END_TRAN */
  fn_con_close,			/* CAS_FC_CON_CLOSE */
  fn_check_cas,			/* CAS_FC_CHECK_CAS */
  fn_make_out_rs,		/* CAS_FC_MAKE_OUT_RS */
  fn_get_generated_keys,	/* CAS_FC_GET_GENERATED_KEYS */
  fn_lob_new,			/* CAS_FC_LOB_NEW */
  fn_lob_write,			/* CAS_FC_LOB_WRITE */
  fn_lob_read,			/* CAS_FC_LOB_READ */
  fn_end_session,		/* CAS_FC_END_SESSION */
  fn_get_row_count,		/* CAS_FC_GET_ROW_COUNT */
  fn_get_last_insert_id,	/* CAS_FC_GET_LAST_INSERT_ID */
  fn_prepare_and_execute,	/* CAS_FC_PREPARE_AND_EXECUTE */
  fn_cursor_close,		/* CAS_FC_CURSOR_CLOSE */
  fn_not_supported,		/* CAS_FC_GET_SHARD_INFO */
  fn_set_cas_change_mode	/* CAS_FC_SET_CAS_CHANGE_MODE */
};
#endif /* CAS_FOR_ORACLE || CAS_FOR_MYSQL */

#ifndef LIBCAS_FOR_JSP
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
  "glo_new(deprecated)",
  "glo_save(deprecated)",
  "glo_load(deprecated)",
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
  "glo_cmd(deprecated)",
  "savepoint",
  "parameter_info",
  "xa_prepare",
  "xa_recover",
  "xa_end_tran",
  "con_close",
  "check_cas",
  "fn_make_out_rs",
  "fn_get_generated_keys",
  "fn_lob_new",
  "fn_lob_write",
  "fn_lob_read",
  "fn_end_session",
  "fn_get_row_count",
  "fn_get_last_insert_id",
  "fn_prepare_and_execute",
  "fn_cursor_close",
  "fn_get_shard_info",
  "fn_set_cas_change_mode"
};
#endif /* !LIBCAS_FOR_JSP */

static T_REQ_INFO req_info;
#ifndef LIBCAS_FOR_JSP
static SOCKET srv_sock_fd;
static int cas_req_count = 1;
#endif /* !LIBCAS_FOR_JSP */

#ifndef LIBCAS_FOR_JSP
#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
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
      char key[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

      cas_log_write_and_end (0, false,
			     "session id (old protocol) for connection 0");
      db_set_server_session_key (key);
      db_set_session_id (DB_EMPTY_SESSION);
    }
}
#endif

static void
cas_send_connect_reply_to_driver (T_CAS_PROTOCOL protocol,
				  SOCKET client_sock_fd, char *cas_info)
{
  char msgbuf[CAS_CONNECTION_REPLY_SIZE + 8];
  char *p = msgbuf;
  char sessid[DRIVER_SESSION_SIZE];
  int v;

#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
  cas_make_session_for_driver (sessid);
#else
  memset (sessid, 0, DRIVER_SESSION_SIZE);
#endif /* !CAS_FOR_ORACLE && !CAS_FOR_MYSQL */

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
#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
      v = htonl (db_get_session_id ());
#else
      v = 0;
#endif /* !CAS_FOR_ORACLE && !CAS_FOR_MYSQL */
      memcpy (p, &v, SESSION_ID_SIZE);
      p += SESSION_ID_SIZE;
    }
  net_write_stream (client_sock_fd, msgbuf, p - msgbuf);
}

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
  signal (SIGUSR1, SIG_IGN);
  signal (SIGPIPE, SIG_IGN);
  signal (SIGXFSZ, SIG_IGN);
#endif /* WINDOWS */

  if (cas_init () < 0)
    return -1;

#if !defined(WINDOWS)
  program_name = argv[0];
  if (argc == 2 && strcmp (argv[1], "--version") == 0)
    {
      printf ("%s\n", makestring (BUILD_NUMBER));
      return 0;
    }
#else /* !WINDOWS */
#if defined(CAS_FOR_ORACLE)
  program_name = APPL_SERVER_CAS_ORACLE_NAME;
#elif defined(CAS_FOR_MYSQL)
  program_name = APPL_SERVER_CAS_MYSQL_NAME;
#else /* CAS_FOR_MYSQL */
  program_name = APPL_SERVER_CAS_NAME;
#endif /* CAS_FOR_MYSQL */
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

static int
shard_cas_main (void)
{
  T_NET_BUF net_buf;
  SOCKET proxy_sock_fd = INVALID_SOCKET;
  int err_code;
#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
  SESSION_ID session_id = DB_EMPTY_SESSION;
#endif /* !CAS_FOR_ORACLE && !CAS_FOR_MYSQL */
  int one = 1;
  char cas_info[CAS_INFO_SIZE] = { CAS_INFO_STATUS_INACTIVE,
    CAS_INFO_RESERVED_DEFAULT,
    CAS_INFO_RESERVED_DEFAULT,
    CAS_INFO_RESERVED_DEFAULT
  };
  FN_RETURN fn_ret = FN_KEEP_CONN;

  struct timeval cas_start_time;

  char func_code = 0x01;
  int error;

  bool is_first = true;

  prev_cas_info[CAS_INFO_STATUS] = CAS_INFO_RESERVED_DEFAULT;

  net_buf_init (&net_buf);
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
      while (as_info->uts_status == UTS_STATUS_RESTART
	     || as_info->uts_status == UTS_STATUS_STOP);
    }
  is_first = false;

  net_timeout_set (-1);

  cas_log_open (broker_name);
  cas_slow_log_open (broker_name);
  cas_log_write_and_end (0, true, "CAS STARTED pid %d", getpid ());
#if defined(CAS_FOR_ORACLE) || defined(CAS_FOR_MYSQL)
  cas_error_log_open (broker_name);
#endif

  /* This is a only use in proxy-cas internal message */
  req_info.client_version = CAS_PROTO_CURRENT_VER;

  set_cas_info_size ();

  gettimeofday (&cas_start_time, NULL);

#if defined(CAS_FOR_ORACLE) || defined(CAS_FOR_MYSQL)
  snprintf (cas_db_name, MAX_HA_DBINFO_LENGTH, "%s",
	    shm_appl->shard_conn_info[shm_shard_id].db_name);
#else
  snprintf (cas_db_name, MAX_HA_DBINFO_LENGTH, "%s@%s",
	    shm_appl->shard_conn_info[shm_shard_id].db_name,
	    shm_appl->shard_conn_info[shm_shard_id].db_host);
#endif /* CAS_FOR_ORACLE || CAS_FOR_MYSQL */

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
	err_code =
	  ux_database_connect (cas_db_name, cas_db_user, cas_db_passwd, NULL);
	if (err_code < 0)
	  {
	    clear_db_connection_info ();
	    SLEEP_SEC (1);
	    goto finish_cas;
	  }

#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
	ux_set_default_setting ();
#endif /* !CAS_FOR_ORACLE && !CAS_FOR_MYSQL */

	cas_log_write_and_end (0, false, "connect db %s user %s", cas_db_name,
			       cas_db_user);
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

    setsockopt (proxy_sock_fd, IPPROTO_TCP, TCP_NODELAY, (char *) &one,
		sizeof (one));

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
    setsockopt (proxy_sock_fd, IPPROTO_TCP, TCP_NODELAY, (char *) &one,
		sizeof (one));

    if (IS_INVALID_SOCKET (proxy_sock_fd))
      {
	goto conn_proxy_retry;
      }

    as_info->auto_commit_mode = FALSE;
    cas_log_write_and_end (0, false, "DEFAULT isolation_level %d, "
			   "lock_timeout %d",
			   cas_default_isolation_level,
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

    for (;;)
      {
#if !defined(WINDOWS)
      retry:
#endif
#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
	cas_log_error_handler_begin ();
#endif /* !CAS_FOR_ORACLE && !CAS_FOR_MYSQL */
	fn_ret = FN_KEEP_CONN;
	as_info->con_status = CON_STATUS_OUT_TRAN;

	while (fn_ret == FN_KEEP_CONN)
	  {
#if !defined(WINDOWS)
	    signal (SIGUSR1, query_cancel);
#endif /* !WINDOWS */

	    fn_ret = process_request (proxy_sock_fd, &net_buf, &req_info);
#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
	    cas_log_error_handler_clear ();
#endif /* !CAS_FOR_ORACLE && !CAS_FOR_MYSQL */
#if !defined(WINDOWS)
	    signal (SIGUSR1, SIG_IGN);
#endif /* !WINDOWS */
	    as_info->last_access_time = time (NULL);

	    if (as_info->con_status == CON_STATUS_OUT_TRAN
		&& hm_srv_handle_get_current_count () >=
		shm_appl->max_prepared_stmt_count)
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
	    ux_end_tran (CCI_TRAN_ROLLBACK, false);
	  }

#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
	if (fn_ret != FN_KEEP_SESS)
	  {
	    ux_end_session ();
	  }
#endif

	if (as_info->reset_flag == TRUE || is_xa_prepared ())
	  {
	    ux_database_shutdown ();
	    as_info->reset_flag = FALSE;
	    cas_set_db_connect_status (-1);	/* DB_CONNECTION_STATUS_RESET */
	  }

#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
	cas_log_error_handler_end ();
#endif /* !CAS_FOR_ORACLE && !CAS_FOR_MYSQL */
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
#if defined(CAS_FOR_ORACLE) || defined(CAS_FOR_MYSQL)
	cas_error_log_close (true);
#endif

	cas_req_count++;
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
cas_main (void)
{
  T_NET_BUF net_buf;
  SOCKET br_sock_fd, client_sock_fd;
  char read_buf[1024];
  int err_code;
  char *db_name, *db_user, *db_passwd, *db_sessionid;
#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
  SESSION_ID session_id = DB_EMPTY_SESSION;
#endif /* !CAS_FOR_ORACLE && !CAS_FOR_MYSQL */
  int one = 1, db_info_size;
#if defined(WINDOWS)
  int new_port;
#else
  int con_status;
  char port_name[BROKER_PATH_MAX];
  char do_not_use_driver_info[SRV_CON_CLIENT_INFO_SIZE];
#endif /* WINDOWS */
  int client_ip_addr;
  char cas_info[CAS_INFO_SIZE] = { CAS_INFO_STATUS_INACTIVE,
    CAS_INFO_RESERVED_DEFAULT,
    CAS_INFO_RESERVED_DEFAULT,
    CAS_INFO_RESERVED_DEFAULT
  };
  FN_RETURN fn_ret = FN_KEEP_CONN;
  char client_ip_str[16];
  bool is_new_connection;

  prev_cas_info[CAS_INFO_STATUS] = CAS_INFO_RESERVED_DEFAULT;

#if defined(CAS_FOR_ORACLE)
  cas_bi_set_dbms_type (CAS_DBMS_ORACLE);
#elif defined(CAS_FOR_MYSQL)
  cas_bi_set_dbms_type (CAS_DBMS_MYSQL);
#endif /* CAS_FOR_MYSQL */

#if defined(WINDOWS)
  if (shm_appl->as_port > 0)
    {
      new_port = shm_appl->as_port + shm_as_index;
    }
  else
    {
      new_port = 0;
    }
  srv_sock_fd = net_init_env (&new_port);
#else /* WINDOWS */
  ut_get_as_port_name (port_name, broker_name, shm_as_index, BROKER_PATH_MAX);

  srv_sock_fd = net_init_env (port_name);
#endif /* WINDOWS */
  if (IS_INVALID_SOCKET (srv_sock_fd))
    {
      return -1;
    }

  net_buf_init (&net_buf);
  net_buf.data = (char *) MALLOC (NET_BUF_ALLOC_SIZE);
  if (net_buf.data == NULL)
    {
      return -1;
    }
  net_buf.alloc_size = NET_BUF_ALLOC_SIZE;

  cas_log_open (broker_name);
  cas_slow_log_open (broker_name);
  cas_log_write_and_end (0, true, "CAS STARTED pid %d", getpid ());

#if defined(WINDOWS)
  as_info->as_port = new_port;
#endif /* WINDOWS */

  unset_hang_check_time ();

  as_info->service_ready_flag = TRUE;
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
			     shm_appl->appl_server_max_size / ONE_K,
			     shm_appl->appl_server_hard_limit / ONE_K);
    }

  stripped_column_name = shm_appl->stripped_column_name;

#if defined(WINDOWS)
  __try
  {
#endif /* WINDOWS */
    for (;;)
      {
#if !defined(WINDOWS)
      retry:
#endif
	error_info_clear ();
	cas_info[CAS_INFO_STATUS] = CAS_INFO_STATUS_INACTIVE;

	unset_hang_check_time ();
	br_sock_fd = net_connect_client (srv_sock_fd);

	if (IS_INVALID_SOCKET (br_sock_fd))
	  {
	    goto finish_cas;
	  }

	req_info.client_version = as_info->clt_version;
	memcpy (req_info.driver_info, as_info->driver_info,
		SRV_CON_CLIENT_INFO_SIZE);

	set_cas_info_size ();

#if defined(WINDOWS)
	as_info->uts_status = UTS_STATUS_BUSY;
#endif /* WINDOWS */
	as_info->con_status = CON_STATUS_IN_TRAN;
	as_info->transaction_start_time = time (0);
	errors_in_transaction = 0;

	client_ip_addr = 0;

#if defined(WINDOWS)
	client_sock_fd = br_sock_fd;
	if (ioctlsocket (client_sock_fd, FIONBIO, (u_long *) & one) < 0)
	  {
	    goto finish_cas;
	  }
	memcpy (&client_ip_addr, as_info->cas_clt_ip, 4);
#else /* WINDOWS */
	net_timeout_set (NET_MIN_TIMEOUT);

	if (net_read_int (br_sock_fd, &con_status) < 0)
	  {
	    cas_log_write_and_end (0, false,
				   "HANDSHAKE ERROR net_read_int(con_status)");
	    CLOSE_SOCKET (br_sock_fd);
	    goto finish_cas;
	  }
	if (net_write_int (br_sock_fd, as_info->con_status) < 0)
	  {
	    cas_log_write_and_end (0, false,
				   "HANDSHAKE ERROR net_write_int(con_status)");
	    CLOSE_SOCKET (br_sock_fd);
	    goto finish_cas;
	  }

	client_sock_fd =
	  recv_fd (br_sock_fd, &client_ip_addr, do_not_use_driver_info);
	if (client_sock_fd == -1)
	  {
	    cas_log_write_and_end (0, false, "HANDSHAKE ERROR recv_fd %d",
				   client_sock_fd);
	    CLOSE_SOCKET (br_sock_fd);
	    goto finish_cas;
	  }
	if (net_write_int (br_sock_fd, as_info->uts_status) < 0)
	  {
	    cas_log_write_and_end (0, false,
				   "HANDSHAKE ERROR net_write_int(uts_status)");
	    CLOSE_SOCKET (br_sock_fd);
	    CLOSE_SOCKET (client_sock_fd);
	    goto finish_cas;
	  }

	CLOSE_SOCKET (br_sock_fd);
#endif /* WINDOWS */
	set_hang_check_time ();

	net_timeout_set (NET_DEFAULT_TIMEOUT);

	cas_log_open (broker_name);
	cas_slow_log_open (broker_name);
	as_info->cur_sql_log2 = shm_appl->sql_log2;
	sql_log2_init (broker_name, shm_as_index, as_info->cur_sql_log2,
		       false);
#if defined(CAS_FOR_ORACLE) || defined(CAS_FOR_MYSQL)
	cas_error_log_open (broker_name);
#else
	if (as_info->cas_err_log_reset == CAS_LOG_RESET_REOPEN)
	  {
	    set_cubrid_file (FID_CUBRID_ERR_DIR, shm_appl->err_log_dir);

	    as_db_err_log_set (broker_name, shm_proxy_id, shm_shard_id,
			       shm_shard_cas_id, shm_as_index,
			       cas_shard_flag);
	    er_final ();
	    as_info->cas_err_log_reset = 0;
	  }
#endif

	ut_get_ipv4_string (client_ip_str, sizeof (client_ip_str),
			    (unsigned char *) (&client_ip_addr));
	cas_log_write_and_end (0, false, "CLIENT IP %s", client_ip_str);
	setsockopt (client_sock_fd, IPPROTO_TCP, TCP_NODELAY, (char *) &one,
		    sizeof (one));
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
	memcpy (req_info.driver_info, as_info->driver_info,
		SRV_CON_CLIENT_INFO_SIZE);
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

	if (net_read_stream (client_sock_fd, read_buf, db_info_size) < 0)
	  {
	    cas_info[CAS_INFO_STATUS] = CAS_INFO_STATUS_INACTIVE;
	    net_write_error (client_sock_fd, req_info.client_version,
			     req_info.driver_info,
			     cas_info, cas_info_size, CAS_ERROR_INDICATOR,
			     CAS_ER_COMMUNICATION, NULL);
	  }
	else
	  {
	    int len;
	    unsigned char *ip_addr;
	    char *db_err_msg = NULL, *url = NULL;
	    struct timeval cas_start_time;

	    gettimeofday (&cas_start_time, NULL);

	    db_name = read_buf;
	    db_name[SRV_CON_DBNAME_SIZE - 1] = '\0';

	    /* Send response to broker health checker */
	    if (strcmp (db_name, HEALTH_CHECK_DUMMY_DB) == 0)
	      {
		cas_log_write_and_end (0, false,
				       "Incoming health check request from client.");

		net_write_int (client_sock_fd, 0);
		cas_info[CAS_INFO_STATUS] = CAS_INFO_STATUS_ACTIVE;
		net_write_stream (client_sock_fd, cas_info, cas_info_size);
		CLOSE_SOCKET (client_sock_fd);

		goto finish_cas;
	      }

	    db_user = db_name + SRV_CON_DBNAME_SIZE;
	    db_user[SRV_CON_DBUSER_SIZE - 1] = '\0';
	    if (db_user[0] == '\0')
	      {
		strcpy (db_user, "PUBLIC");
	      }

	    db_passwd = db_user + SRV_CON_DBUSER_SIZE;
	    db_passwd[SRV_CON_DBPASSWD_SIZE - 1] = '\0';

	    if (req_info.client_version >= CAS_MAKE_VER (8, 2, 0))
	      {
		url = db_passwd + SRV_CON_DBPASSWD_SIZE;
		url[SRV_CON_URL_SIZE - 1] = '\0';
	      }

	    if (req_info.client_version >= CAS_MAKE_VER (8, 4, 0))
	      {
		assert (url != NULL);

		db_sessionid = url + SRV_CON_URL_SIZE;
		db_sessionid[SRV_CON_DBSESS_ID_SIZE - 1] = '\0';
	      }
	    else
	      {
		/* even drivers do not send session id (under RB-8.4.0)
		 * the cas_set_session_id() should be called
		 */
		db_sessionid = NULL;
	      }
	    as_info->driver_version[0] = '\0';
	    if (DOES_CLIENT_UNDERSTAND_THE_PROTOCOL (req_info.client_version,
						     PROTOCOL_V5))
	      {
		assert (url != NULL);

		len = *(url + strlen (url) + 1);
		if (len > 0 && len < SRV_CON_VER_STR_MAX_SIZE)
		  {
		    memcpy (as_info->driver_version, url + strlen (url) + 2,
			    (int) len);
		    as_info->driver_version[len] = '\0';
		  }
		else
		  {
		    snprintf (as_info->driver_version,
			      SRV_CON_VER_STR_MAX_SIZE, "PROTOCOL V%d",
			      (int) (CAS_PROTO_VER_MASK & req_info.
				     client_version));
		  }
	      }
	    else
	      if (DOES_CLIENT_UNDERSTAND_THE_PROTOCOL
		  (req_info.client_version, PROTOCOL_V1))
	      {
		char *ver;

		CAS_PROTO_TO_VER_STR (&ver,
				      (int) (CAS_PROTO_VER_MASK & req_info.
					     client_version));

		strncpy (as_info->driver_version, ver,
			 SRV_CON_VER_STR_MAX_SIZE);
	      }
	    else
	      {
		snprintf (as_info->driver_version,
			  SRV_CON_VER_STR_MAX_SIZE, "%d.%d.%d",
			  CAS_VER_TO_MAJOR (req_info.client_version),
			  CAS_VER_TO_MINOR (req_info.client_version),
			  CAS_VER_TO_PATCH (req_info.client_version));
	      }
	    cas_log_write_and_end (0, false, "CLIENT VERSION %s",
				   as_info->driver_version);
#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
	    cas_set_session_id (req_info.client_version, db_sessionid);
	    if (db_get_session_id () != DB_EMPTY_SESSION)
	      {
		is_new_connection = false;
	      }
	    else
	      {
		is_new_connection = true;
	      }
#endif /* !CAS_FOR_ORACLE && !CAS_FOR_MYSQL */

	    set_hang_check_time ();

	    cas_log_debug (ARG_FILE_LINE,
			   "db_name %s db_user %s url %s "
			   "session id %s", db_name, db_user, url,
			   db_sessionid);
	    if (as_info->reset_flag == TRUE)
	      {
		cas_log_debug (ARG_FILE_LINE, "main: set reset_flag");
		cas_set_db_connect_status (-1);	/* DB_CONNECTION_STATUS_RESET */
		as_info->reset_flag = FALSE;
	      }

	    unset_hang_check_time ();

	    ip_addr = (unsigned char *) (&client_ip_addr);

	    if (shm_appl->access_control)
	      {
		if (access_control_check_right
		    (shm_appl, db_name, db_user, ip_addr) < 0)
		  {
		    char err_msg[1024];

		    as_info->num_connect_rejected++;

		    sprintf (err_msg,
			     "Authorization error.(Address is rejected)");

		    cas_info[CAS_INFO_STATUS] = CAS_INFO_STATUS_INACTIVE;
		    net_write_error (client_sock_fd, req_info.client_version,
				     req_info.driver_info,
				     cas_info, cas_info_size,
				     DBMS_ERROR_INDICATOR,
				     CAS_ER_NOT_AUTHORIZED_CLIENT, err_msg);

		    set_hang_check_time ();

		    cas_log_write_and_end (0, false,
					   "connect db %s user %s url %s - rejected",
					   db_name, db_user, url);

		    if (shm_appl->access_log == ON)
		      {
			cas_access_log (&cas_start_time, shm_as_index,
					client_ip_addr, db_name, db_user,
					ACL_REJECTED);
		      }

		    unset_hang_check_time ();

		    CLOSE_SOCKET (client_sock_fd);

		    goto finish_cas;
		  }
	      }

	    err_code =
	      ux_database_connect (db_name, db_user, db_passwd, &db_err_msg);

	    if (err_code < 0)
	      {
		char msg_buf[LINE_MAX];

		cas_info[CAS_INFO_STATUS] = CAS_INFO_STATUS_INACTIVE;
		net_write_error (client_sock_fd, req_info.client_version,
				 req_info.driver_info,
				 cas_info, cas_info_size,
				 err_info.err_indicator,
				 err_info.err_number, db_err_msg);

		if (db_err_msg == NULL)
		  {
		    snprintf (msg_buf, LINE_MAX,
			      "connect db %s user %s url %s, error:%d.",
			      db_name, db_user, url, err_info.err_number);
		  }
		else
		  {
		    snprintf (msg_buf, LINE_MAX,
			      "connect db %s user %s url %s, error:%d, %s",
			      db_name, db_user, url, err_info.err_number,
			      db_err_msg);
		  }

		cas_log_write_and_end (0, false, msg_buf);
		cas_slow_log_write_and_end (NULL, 0, msg_buf);

		CLOSE_SOCKET (client_sock_fd);
		FREE_MEM (db_err_msg);

		goto finish_cas;
	      }

	    FREE_MEM (db_err_msg);

	    set_hang_check_time ();

#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
	    session_id = db_get_session_id ();

	    if (shm_appl->access_log == ON)
	      {
		ACCESS_LOG_TYPE type =
		  (is_new_connection) ? NEW_CONNECTION : CLIENT_CHANGED;

		cas_access_log (&cas_start_time, shm_as_index,
				client_ip_addr, db_name, db_user, type);
	      }

	    cas_log_write_and_end (0, false, "connect db %s@%s user %s url %s"
				   " session id %u", as_info->database_name,
				   as_info->database_host, db_user, url,
				   session_id);
#else
	    cas_log_write_and_end (0, false, "connect db %s user %s url %s",
				   db_name, db_user, url);
#endif /* !CAS_FOR_ORACLE && !CAS_FOR_MYSQL */

#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
	    ux_set_default_setting ();
#endif /* !CAS_FOR_ORACLE && !CAS_FOR_MYSQL */

	    as_info->auto_commit_mode = FALSE;
	    cas_log_write_and_end (0, false, "DEFAULT isolation_level %d, "
				   "lock_timeout %d",
				   cas_default_isolation_level,
				   cas_default_lock_timeout);

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

	    cas_info[CAS_INFO_STATUS] = CAS_INFO_STATUS_ACTIVE;
	    cas_send_connect_reply_to_driver (req_info.client_version,
					      client_sock_fd, cas_info);

	    as_info->cci_default_autocommit =
	      shm_appl->cci_default_autocommit;
	    req_info.need_rollback = TRUE;

	    gettimeofday (&tran_start_time, NULL);
	    gettimeofday (&query_start_time, NULL);
	    tran_timeout = 0;
	    query_timeout = 0;

#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
	    cas_log_error_handler_begin ();
#endif /* !CAS_FOR_ORACLE && !CAS_FOR_MYSQL */
#ifndef LIBCAS_FOR_JSP
	    con_status_before_check_cas = -1;
	    is_first_request = true;
#endif /* !LIBCAS_FOR_JSP */
	    fn_ret = FN_KEEP_CONN;
	    while (fn_ret == FN_KEEP_CONN)
	      {
#if !defined(WINDOWS)
		signal (SIGUSR1, query_cancel);
#endif /* !WINDOWS */
		fn_ret = process_request (client_sock_fd, &net_buf,
					  &req_info);
#ifndef LIBCAS_FOR_JSP
		is_first_request = false;
#endif /* !LIBCAS_FOR_JSP */
#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
		cas_log_error_handler_clear ();
#endif /* !CAS_FOR_ORACLE && !CAS_FOR_MYSQL */
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

	    if (!is_xa_prepared ())
	      {
		if (ux_end_tran (CCI_TRAN_ROLLBACK, false) < 0)
		  {
		    as_info->reset_flag = TRUE;
		  }
	      }

#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
	    if (fn_ret != FN_KEEP_SESS)
	      {
		ux_end_session ();
	      }
#endif

	    if (is_xa_prepared ())
	      {
		ux_database_shutdown ();
		ux_database_connect (db_name, db_user, db_passwd, NULL);
	      }

	    if (as_info->reset_flag == TRUE)
	      {
		ux_database_shutdown ();
		as_info->reset_flag = FALSE;
		cas_set_db_connect_status (-1);	/* DB_CONNECTION_STATUS_RESET */
	      }

#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
	    cas_log_error_handler_end ();
#endif /* !CAS_FOR_ORACLE && !CAS_FOR_MYSQL */
	  }

	CLOSE_SOCKET (client_sock_fd);

      finish_cas:
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
#if defined(CAS_FOR_ORACLE) || defined(CAS_FOR_MYSQL)
	cas_error_log_close (true);
#endif
	cas_req_count++;

	unset_hang_check_time ();

	if (is_server_aborted ())
	  {
	    cas_final ();
	    return 0;
	  }
	else
	  if (!
	      (as_info->cur_keep_con == KEEP_CON_AUTO
	       && as_info->con_status == CON_STATUS_CLOSE_AND_CONNECT))
	  {
	    if (restart_is_needed ())
	      {
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

#else /* LIBCAS_FOR_JSP */
int
libcas_main (SOCKET jsp_sock_fd)
{
  T_NET_BUF net_buf;
  SOCKET client_sock_fd;

  memset (&req_info, 0, sizeof (req_info));

  req_info.client_version = CAS_PROTO_CURRENT_VER;
  req_info.driver_info[DRIVER_INFO_FUNCTION_FLAG] =
    BROKER_RENEWED_ERROR_CODE | BROKER_SUPPORT_HOLDABLE_RESULT;
  client_sock_fd = jsp_sock_fd;

  net_buf_init (&net_buf);
  net_buf.data = (char *) MALLOC (NET_BUF_ALLOC_SIZE);
  if (net_buf.data == NULL)
    {
      return 0;
    }
  net_buf.alloc_size = NET_BUF_ALLOC_SIZE;

  while (1)
    {
      if (process_request (client_sock_fd, &net_buf,
			   &req_info) != FN_KEEP_CONN)
	{
	  break;
	}
    }

  net_buf_clear (&net_buf);
  net_buf_destroy (&net_buf);

  return 0;
}

void *
libcas_get_db_result_set (int h_id)
{
  T_SRV_HANDLE *srv_handle;

  srv_handle = hm_find_srv_handle (h_id);
  if (srv_handle == NULL || srv_handle->cur_result == NULL)
    {
      return NULL;
    }

  return srv_handle;
}

void
libcas_srv_handle_free (int h_id)
{
  hm_srv_handle_free (h_id);
}
#endif /* !LIBCAS_FOR_JSP */

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
#if !defined(LIBCAS_FOR_JSP)
  if (cas_shard_flag == OFF
      && as_info != NULL && shm_appl != NULL && shm_appl->monitor_hang_flag)
    {
      as_info->claimed_alive_time = time (NULL);
    }
#endif /* !LIBCAS_FOR_JSP */
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
#if !defined(LIBCAS_FOR_JSP)
  if (cas_shard_flag == OFF
      && as_info != NULL && shm_appl != NULL && shm_appl->monitor_hang_flag)
    {
      as_info->claimed_alive_time = (time_t) 0;
    }
#endif /* !LIBCAS_FOR_JSP */
  return;
}

#ifndef LIBCAS_FOR_JSP
static void
cas_sig_handler (int signo)
{
  signal (signo, SIG_IGN);
  cas_free (false);
  exit (0);
}

static void
cas_final (void)
{
  cas_free (true);
  as_info->pid = 0;
  as_info->uts_status = UTS_STATUS_RESTART;
  exit (0);
}

static void
cas_free (bool free_srv_handle)
{
#ifdef MEM_DEBUG
  int fd;
#endif
  int max_process_size;

  ux_database_shutdown ();

  if (as_info->cur_statement_pooling && free_srv_handle == true)
    {
      hm_srv_handle_free_all (true);
    }
#if defined(AIX)
  /* In linux, getsize() returns VSM(55M). but in AIX, getsize() returns
   * vritual meory size for data(900K). so, the size of cub_cas process exceeds
   * 'psize_at_start * 2' very easily. the linux's rule to restart cub_cas
   * is not suit for AIX. In AIX, we use 20M as max_process_size. */
  max_process_size = (shm_appl->appl_server_max_size > 0) ?
    shm_appl->appl_server_max_size : 20 * ONE_K;
#else
  max_process_size = (shm_appl->appl_server_max_size > 0) ?
    shm_appl->appl_server_max_size : (psize_at_start * 2);
#endif
  if (as_info->psize > max_process_size)
    {
      cas_log_write_and_end (0, true,
			     "CAS MEMORY USAGE (%dM) HAS EXCEEDED MAX SIZE (%dM)",
			     as_info->psize / ONE_K,
			     max_process_size / ONE_K);
    }

  if (as_info->psize > shm_appl->appl_server_hard_limit)
    {
      cas_log_write_and_end (0, true,
			     "CAS MEMORY USAGE (%dM) HAS EXCEEDED HARD LIMIT (%dM)",
			     as_info->psize / ONE_K,
			     shm_appl->appl_server_hard_limit / ONE_K);
    }

  cas_log_write_and_end (0, true, "CAS TERMINATED pid %d", getpid ());
  cas_log_close (true);
  cas_slow_log_close ();
#if defined(CAS_FOR_ORACLE) || defined(CAS_FOR_MYSQL)
  cas_error_log_close (true);
#endif

#ifdef MEM_DEBUG
  fd = open ("mem_debug.log", O_CREAT | O_TRUNC | O_WRONLY, 0666);
  if (fd > 0)
    {
      malloc_dump (fd);
      close (fd);
    }
#endif
}

static void
query_cancel (int signo)
{
#if !defined(WINDOWS)
  struct timespec ts;
#if defined(CAS_FOR_ORACLE)
  signal (signo, SIG_IGN);
  cas_oracle_query_cancel ();
#elif defined(CAS_FOR_MYSQL)
#else /* CAS_FOR_CUBRID */
  signal (signo, SIG_IGN);
  db_set_interrupt (1);
#endif /* CAS_FOR_ORACLE */
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
#endif /* !LIBCAS_FOR_JSP */

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
#ifndef LIBCAS_FOR_JSP
  int con_status_to_restore, old_con_status;
#endif
  T_SERVER_FUNC server_fn;
  FN_RETURN fn_ret = FN_KEEP_CONN;

  error_info_clear ();
  init_msg_header (&client_msg_header);
  init_msg_header (&cas_msg_header);

#ifndef LIBCAS_FOR_JSP
  old_con_status = as_info->con_status;
#endif

#ifndef LIBCAS_FOR_JSP
  if (cas_shard_flag == ON)
    {
      /* set req_info->client_version in net_read_process */
      err_code = net_read_process (sock_fd, &client_msg_header, req_info);
      if (err_code < 0)
	{
	  const char *cas_log_msg = NULL;
	  net_write_error (sock_fd, req_info->client_version,
			   req_info->driver_info,
			   cas_msg_header.info_ptr, cas_info_size,
			   CAS_ERROR_INDICATOR, CAS_ER_COMMUNICATION, NULL);
	  fn_ret = FN_CLOSE_CONN;

	  if (is_net_timed_out ())
	    {
	      if (as_info->reset_flag == TRUE)
		{
		  cas_log_msg = "CONNECTION RESET";
		}
	      else if (get_graceful_down_timeout () > 0)
		{
		  cas_log_msg = "SESSION TIMEOUT AND EXPIRE IDLE TIMEOUT";
		  fn_ret = FN_GRACEFUL_DOWN;
		}
	      else
		{
		  if (as_info->con_status == CON_STATUS_IN_TRAN)
		    {
		      cas_log_msg = "SESSION TIMEOUT";
		    }
		  else
		    {
		      cas_log_msg = "CONNECTION WAIT TIMEOUT";
		    }
		}
	    }
	  else
	    {
	      cas_log_msg = "COMMUNICATION ERROR net_read_header()";
	    }
	  cas_log_write_and_end (0, true, cas_log_msg);
	  return fn_ret;
	}
      else
	{
	  as_info->uts_status = UTS_STATUS_BUSY;

	  if (need_database_reconnect ())
	    {
	      assert (as_info->fixed_shard_user == false);

	      set_db_connection_info ();

	      err_code = ux_database_connect (cas_db_name, cas_db_user,
					      cas_db_passwd, NULL);
	      if (err_code < 0)
		{
		  clear_db_connection_info ();
		  net_write_error (sock_fd, req_info->client_version,
				   req_info->driver_info,
				   cas_msg_header.info_ptr, cas_info_size,
				   err_info.err_indicator,
				   err_info.err_number, err_info.err_string);
		  return FN_CLOSE_CONN;
		}

#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
	      ux_set_default_setting ();
#endif /* !CAS_FOR_ORACLE && !CAS_FOR_MYSQL */

	      cas_log_write_and_end (0, false, "connect db %s user %s",
				     cas_db_name, cas_db_user);
	    }
	}
    }
  else
#endif /* !LIBCAS_FOR_JSP */
    {
#ifndef LIBCAS_FOR_JSP
      unset_hang_check_time ();
      if (as_info->cur_keep_con == KEEP_CON_AUTO)
	{
	  err_code = net_read_int_keep_con_auto (sock_fd,
						 &client_msg_header,
						 req_info);
	}
      else
	{
	  err_code =
	    net_read_header_keep_con_on (sock_fd, &client_msg_header);

	  if (as_info->cur_keep_con == KEEP_CON_ON
	      && as_info->con_status == CON_STATUS_OUT_TRAN)
	    {
	      as_info->con_status = CON_STATUS_IN_TRAN;
	      as_info->transaction_start_time = time (0);
	      errors_in_transaction = 0;
	    }
	}

#else /* !LIBCAS_FOR_JSP */
      net_timeout_set (60);
      err_code = net_read_header (sock_fd, &client_msg_header);
#endif /* !LIBCAS_FOR_JSP */

      if (err_code < 0)
	{
	  const char *cas_log_msg = NULL;

	  fn_ret = FN_CLOSE_CONN;

#ifndef LIBCAS_FOR_JSP
	  if (as_info->reset_flag)
	    {
	      cas_log_msg = "RESET";
	      cas_log_write_and_end (0, true, cas_log_msg);
	      fn_ret = FN_KEEP_SESS;
	    }
	  if (as_info->con_status == CON_STATUS_CLOSE_AND_CONNECT)
	    {
	      cas_log_msg = "CHANGE CLIENT";
	      fn_ret = FN_KEEP_SESS;
	    }
#endif /* !LIBCAS_FOR_JSP */
	  if (cas_log_msg == NULL)
	    {
	      if (is_net_timed_out ())
		{
#ifndef LIBCAS_FOR_JSP
		  if (as_info->reset_flag == TRUE)
		    {
		      cas_log_msg = "CONNECTION RESET";
		    }
		  else
		    {
		      cas_log_msg = "SESSION TIMEOUT";
		    }
#else
		  cas_log_msg = "SESSION TIMEOUT";
#endif /* !LIBCAS_FOR_JSP */
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

#ifndef LIBCAS_FOR_JSP
#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
#if !defined(WINDOWS)
  /* Before start to execute a new request,
   * try to reset a previous interrupt request we might have.
   * The interrupt request arrived too late to interrupt the previous request
   * and still remains.
   */
  db_set_interrupt (0);
#endif /* !WINDOWS */
#endif /* !CAS_FOR_ORACLE && !CAS_FOR_MYSQL) */

#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
  if (cas_shard_flag == ON)
    {
      set_db_parameter ();
    }
#endif /* !CAS_FOR_ORACLE && !CAS_FOR_MYSQL) */

  if (shm_appl->session_timeout < 0)
    net_timeout_set (NET_DEFAULT_TIMEOUT);
  else
    net_timeout_set (MIN (shm_appl->session_timeout, NET_DEFAULT_TIMEOUT));
#else /* !LIBCAS_FOR_JSP */
  net_timeout_set (NET_DEFAULT_TIMEOUT);
#endif /* LIBCAS_FOR_JSP */

  if (cas_shard_flag == ON && req_info->client_version == 0)
    {
      assert (0);
      req_info->client_version = CAS_PROTO_CURRENT_VER;
    }

  read_msg = (char *) MALLOC (*(client_msg_header.msg_body_size_ptr));
  if (read_msg == NULL)
    {
      net_write_error (sock_fd, req_info->client_version,
		       req_info->driver_info,
		       cas_msg_header.info_ptr, cas_info_size,
		       CAS_ERROR_INDICATOR, CAS_ER_NO_MORE_MEMORY, NULL);
      return FN_CLOSE_CONN;
    }
  if (net_read_stream (sock_fd, read_msg,
		       *(client_msg_header.msg_body_size_ptr)) < 0)
    {
      FREE_MEM (read_msg);
      net_write_error (sock_fd, req_info->client_version,
		       req_info->driver_info,
		       cas_msg_header.info_ptr, cas_info_size,
		       CAS_ERROR_INDICATOR, CAS_ER_COMMUNICATION, NULL);
      cas_log_write_and_end (0, true,
			     "COMMUNICATION ERROR net_read_stream()");
      return FN_CLOSE_CONN;
    }

  argc =
    net_decode_str (read_msg,
		    *(client_msg_header.msg_body_size_ptr),
		    &func_code, &argv);
  if (argc < 0)
    {
      FREE_MEM (read_msg);
      net_write_error (sock_fd, req_info->client_version,
		       req_info->driver_info,
		       cas_msg_header.info_ptr, cas_info_size,
		       CAS_ERROR_INDICATOR, CAS_ER_COMMUNICATION, NULL);
      return FN_CLOSE_CONN;
    }

  if (func_code <= 0 || func_code >= CAS_FC_MAX)
    {
      FREE_MEM (argv);
      FREE_MEM (read_msg);
      net_write_error (sock_fd, req_info->client_version,
		       req_info->driver_info,
		       cas_msg_header.info_ptr, cas_info_size,
		       CAS_ERROR_INDICATOR, CAS_ER_COMMUNICATION, NULL);
      return FN_CLOSE_CONN;
    }

#ifndef LIBCAS_FOR_JSP
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
	  /* If this request is the first request after connection established,
	   * con_status should be CON_STATUS_OUT_TRAN.  
	   */
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
#endif /* !LIBCAS_FOR_JSP */

  server_fn = server_fn_table[func_code - 1];

#ifndef LIBCAS_FOR_JSP
  if (prev_cas_info[CAS_INFO_STATUS] != CAS_INFO_RESERVED_DEFAULT)
    {
      assert (prev_cas_info[CAS_INFO_STATUS] ==
	      client_msg_header.info_ptr[CAS_INFO_STATUS]);
#if defined (PROTOCOL_EXTENDS_DEBUG)	/* for debug cas <-> JDBC info */
      if (prev_cas_info[CAS_INFO_STATUS] !=
	  client_msg_header.info_ptr[CAS_INFO_STATUS])
	{
	  cas_log_debug (ARG_FILE_LINE,
			 "[%d][PREV : %d, RECV : %d], "
			 "[preffunc : %d, recvfunc : %d], [REQ: %d, REQ: %d], "
			 "[JID : %d] \n", func_code - 1,
			 prev_cas_info[CAS_INFO_STATUS],
			 client_msg_header.
			 info_ptr[CAS_INFO_STATUS],
			 prev_cas_info[CAS_INFO_RESERVED_1],
			 client_msg_header.
			 info_ptr[CAS_INFO_RESERVED_1],
			 prev_cas_info[CAS_INFO_RESERVED_2],
			 client_msg_header.
			 info_ptr[CAS_INFO_RESERVED_2],
			 client_msg_header.info_ptr[CAS_INFO_RESERVED_3]);
	}
#endif /* end for debug */
    }
#endif /* !LIBCAS_FOR_JSP */

#ifndef LIBCAS_FOR_JSP
  req_info->need_auto_commit = TRAN_NOT_AUTOCOMMIT;
#endif /* !LIBCAS_FOR_JSP */
  cas_send_result_flag = TRUE;

#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
  /* for 9.0 driver */
  if (DOES_CLIENT_MATCH_THE_PROTOCOL (req_info->client_version, PROTOCOL_V2))
    {
      ux_set_utype_for_enum (CCI_U_TYPE_STRING);
    }
#endif /* !CAS_FOR_ORACLE && !CAS_FOR_MYSQL */

  set_hang_check_time ();
  fn_ret = (*server_fn) (sock_fd, argc, argv, net_buf, req_info);
  set_hang_check_time ();

#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
  /* set back original utype for enum */
  if (DOES_CLIENT_MATCH_THE_PROTOCOL (req_info->client_version, PROTOCOL_V2))
    {
      ux_set_utype_for_enum (CCI_U_TYPE_ENUM);
    }
#endif /* !CAS_FOR_ORACLE && !CAS_FOR_MYSQL */

#ifndef LIBCAS_FOR_JSP
  cas_log_debug (ARG_FILE_LINE, "process_request: %s() err_code %d",
		 server_func_name[func_code - 1], err_info.err_number);

  if (con_status_to_restore != -1)
    {
      CON_STATUS_LOCK (as_info, CON_STATUS_LOCK_CAS);
      as_info->con_status = con_status_to_restore;
      CON_STATUS_UNLOCK (as_info, CON_STATUS_LOCK_CAS);
    }
#endif /* !LIBCAS_FOR_JSP */

  if (cas_shard_flag == ON
      && (func_code == CAS_FC_PREPARE || func_code == CAS_FC_CHECK_CAS)
      && (client_msg_header.info_ptr[CAS_INFO_ADDITIONAL_FLAG]
	  & CAS_INFO_FLAG_MASK_FORCE_OUT_TRAN))
    {
      /* for shard dummy prepare */
      /* for connection check */
      req_info->need_auto_commit = TRAN_AUTOROLLBACK;
    }

#ifndef LIBCAS_FOR_JSP
  if (fn_ret == FN_KEEP_CONN && net_buf->err_code == 0
      && as_info->con_status == CON_STATUS_IN_TRAN
      && req_info->need_auto_commit != TRAN_NOT_AUTOCOMMIT
      && err_info.err_number != CAS_ER_STMT_POOLING)
    {
      /* no communication error and auto commit is needed */
      err_code = ux_auto_commit (net_buf, req_info);
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
	  if (!ux_is_database_connected ())
	    {
	      fn_ret = FN_CLOSE_CONN;
	    }
	  else if (restart_is_needed ())
	    {
	      fn_ret = FN_KEEP_SESS;
	    }
	  if (shm_appl->sql_log2 != as_info->cur_sql_log2)
	    {
	      sql_log2_end (false);
	      as_info->cur_sql_log2 = shm_appl->sql_log2;
	      sql_log2_init (broker_name, shm_as_index,
			     as_info->cur_sql_log2, true);
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
#endif /* !LIBCAS_FOR_JSP */

  if (net_buf->err_code)
    {
      net_write_error (sock_fd, req_info->client_version,
		       req_info->driver_info,
		       cas_msg_header.info_ptr, cas_info_size,
		       CAS_ERROR_INDICATOR, net_buf->err_code, NULL);
      fn_ret = FN_CLOSE_CONN;
      goto exit_on_end;
    }

  if (cas_send_result_flag && net_buf->data != NULL)
    {
#ifndef LIBCAS_FOR_JSP
      cas_msg_header.info_ptr[CAS_INFO_ADDITIONAL_FLAG] &=
	~CAS_INFO_FLAG_MASK_AUTOCOMMIT;
      cas_msg_header.info_ptr[CAS_INFO_ADDITIONAL_FLAG] |=
	(as_info->cci_default_autocommit & CAS_INFO_FLAG_MASK_AUTOCOMMIT);

      if (cas_shard_flag == ON)
	{
	  cas_msg_header.info_ptr[CAS_INFO_ADDITIONAL_FLAG] &=
	    ~CAS_INFO_FLAG_MASK_FORCE_OUT_TRAN;
	}
#if defined (PROTOCOL_EXTENDS_DEBUG)	/* for debug cas<->jdbc info */
      cas_msg_header.info_ptr[CAS_INFO_RESERVED_1] = func_code - 1;
      cas_msg_header.info_ptr[CAS_INFO_RESERVED_2] =
	as_info->num_requests_received % 128;
      prev_cas_info[CAS_INFO_STATUS] =
	cas_msg_header.info_ptr[CAS_INFO_STATUS];
      prev_cas_info[CAS_INFO_RESERVED_1] =
	cas_msg_header.info_ptr[CAS_INFO_RESERVED_1];
      prev_cas_info[CAS_INFO_RESERVED_2] =
	cas_msg_header.info_ptr[CAS_INFO_RESERVED_2];
#endif /* end for debug */

#endif /* !LIBCAS_FOR_JSP */

      *(cas_msg_header.msg_body_size_ptr) = htonl (net_buf->data_size);
      memcpy (net_buf->data, cas_msg_header.msg_body_size_ptr,
	      NET_BUF_HEADER_MSG_SIZE);

      if (cas_info_size > 0)
	{
	  memcpy (net_buf->data + NET_BUF_HEADER_MSG_SIZE,
		  cas_msg_header.info_ptr, cas_info_size);
	}

      assert (NET_BUF_CURR_SIZE (net_buf) <= net_buf->alloc_size);
      if (net_write_stream (sock_fd, net_buf->data,
			    NET_BUF_CURR_SIZE (net_buf)) < 0)
	{
	  cas_log_write_and_end (0, true,
				 "COMMUNICATION ERROR net_write_stream()");
	}
    }

  if (cas_shard_flag == OFF
      && cas_send_result_flag && net_buf->post_send_file != NULL)
    {
      err_code = net_write_from_file (sock_fd,
				      net_buf->post_file_size,
				      net_buf->post_send_file);
      unlink (net_buf->post_send_file);
      if (err_code < 0)
	{
	  fn_ret = FN_CLOSE_CONN;
	  goto exit_on_end;
	}
    }

#ifndef LIBCAS_FOR_JSP
  if (as_info->reset_flag
      && ((as_info->con_status != CON_STATUS_IN_TRAN
	   && as_info->num_holdable_results < 1
	   && as_info->cas_change_mode == CAS_CHANGE_MODE_AUTO)
	  || (cas_get_db_connect_status () == -1)))
    {
      cas_log_debug (ARG_FILE_LINE,
		     "process_request: reset_flag && !CON_STATUS_IN_TRAN");
      fn_ret = FN_KEEP_SESS;
      goto exit_on_end;
    }
#endif /* !LIBCAS_FOR_JSP */

exit_on_end:
#ifndef LIBCAS_FOR_JSP
  if (cas_shard_flag == ON
      && as_info->con_status != CON_STATUS_IN_TRAN
      && as_info->uts_status == UTS_STATUS_BUSY)
    {
      as_info->uts_status = UTS_STATUS_IDLE;
    }
#endif /* !LIBCAS_FOR_JSP */

  net_buf_clear (net_buf);

  FREE_MEM (read_msg);
  FREE_MEM (argv);

  return fn_ret;
}

#ifndef LIBCAS_FOR_JSP
static int
cas_init ()
{
  if (cas_init_shm () < 0)
    {
      return -1;
    }

  strncpy (broker_name, shm_appl->broker_name, BROKER_NAME_LEN);

  set_cubrid_file (FID_SQL_LOG_DIR, shm_appl->log_dir);
  set_cubrid_file (FID_SLOW_LOG_DIR, shm_appl->slow_log_dir);
  set_cubrid_file (FID_CUBRID_ERR_DIR, shm_appl->err_log_dir);

  as_pid_file_create (broker_name, as_info->as_id);
  as_db_err_log_set (broker_name, shm_proxy_id, shm_shard_id,
		     shm_shard_cas_id, shm_as_index, cas_shard_flag);

#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
  if (cas_shard_flag == OFF)
    {
      css_register_server_timeout_fn (set_hang_check_time);
    }
#endif /* !CAS_FOR_ORACLE && !CAS_FOR_MYSQL */
  return 0;
}

static int
net_read_process (SOCKET proxy_sock_fd,
		  MSG_HEADER * client_msg_header, T_REQ_INFO * req_info)
{
  int ret_value = 0;
  int elapsed_sec = 0, elapsed_msec = 0;
  int timeout = 0, remained_timeout = 0;
  bool is_proxy_conn_wait_timeout = false;

  if (as_info->con_status == CON_STATUS_IN_TRAN)
    {
      net_timeout_set (shm_appl->session_timeout);
    }
  else
    {
      net_timeout_set (DEFAULT_CHECK_INTERVAL);

      timeout = get_graceful_down_timeout ();
      if (timeout < 0 && as_info->database_user[0] != '\0')
	{
	  timeout = as_info->proxy_conn_wait_timeout;
	  is_proxy_conn_wait_timeout = true;
	}
#if defined(CAS_FOR_MYSQL)
      if (timeout > 0)
	{
	  timeout = MIN (timeout, cas_mysql_get_mysql_wait_timeout ());
	}
      else
	{
	  timeout = cas_mysql_get_mysql_wait_timeout ();
	}
#endif

      remained_timeout = timeout;
    }

  do
    {
      if (as_info->cas_log_reset)
	{
	  cas_log_reset (broker_name);
	}

      if (as_info->con_status == CON_STATUS_CLOSE)
	{
	  break;
	}
      else if (as_info->con_status == CON_STATUS_OUT_TRAN)
	{
	  remained_timeout -= DEFAULT_CHECK_INTERVAL;
	}

      /*
         net_read_header error case.
         case 1 : disconnect with proxy_sock_fd
         case 2 : CON_STATUS_IN_TRAN && session_timeout
         case 3 : reset_flag is TRUE
       */
      if (net_read_header (proxy_sock_fd, client_msg_header) < 0)
	{
	  /* if in-transaction state, return network error */
	  if (as_info->con_status == CON_STATUS_IN_TRAN
	      || !is_net_timed_out ())
	    {
	      ret_value = -1;
	      break;
	    }
	  /* if out-of-transaction state, check whether restart is needed */
	  if (as_info->con_status == CON_STATUS_OUT_TRAN
	      && is_net_timed_out ())
	    {
	      if (as_info->reset_flag == TRUE)
		{
		  ret_value = -1;
		  break;
		}

	      if (restart_is_needed ())
		{
		  cas_log_debug (ARG_FILE_LINE, "net_read_process: "
				 "restart_is_needed()");
		  ret_value = -1;
		  break;
		}

	      /* this is not real timeout. try again. */
	      if (timeout < 0 || remained_timeout > 0)
		{
		  continue;
		}

#if defined (CAS_FOR_MYSQL)
	      /* execute dummy query to reset wait_timeout of MySQL */
	      if (cas_mysql_execute_dummy () >= 0)
		{
		  remained_timeout = timeout;
		  continue;
		}
#endif /* CAS_FOR_MYSQL */

	      if (is_proxy_conn_wait_timeout)
		{
		  as_info->database_user[0] = '\0';
		  as_info->database_passwd[0] = '\0';
		}

	      /* MYSQL_CONNECT_TIMEOUT case */
	      /* SHARD_CAS expire idle time and restart case */
	      ret_value = -1;
	      break;
	    }
	}
      else
	{
	  break;
	}
    }
  while (1);

  CON_STATUS_LOCK (as_info, CON_STATUS_LOCK_CAS);

  if (as_info->con_status == CON_STATUS_OUT_TRAN)
    {
      as_info->num_request++;
      gettimeofday (&tran_start_time, NULL);
    }

  if (as_info->con_status == CON_STATUS_CLOSE)
    {
      ret_value = -1;
    }
  else
    {
      if (as_info->con_status != CON_STATUS_IN_TRAN)
	{
	  if (ret_value >= 0)
	    {
	      as_info->con_status = CON_STATUS_IN_TRAN;
	      errors_in_transaction = 0;
	    }

	  cas_log_write_client_ip (as_info->cas_clt_ip);

	  /* This is a real client protocol version */
	  req_info->client_version = as_info->clt_version;
	  memcpy (req_info->driver_info, as_info->driver_info,
		  SRV_CON_CLIENT_INFO_SIZE);
	  cas_log_write_and_end (0, false, "CLIENT VERSION %s",
				 as_info->driver_version);
	}
    }

  CON_STATUS_UNLOCK (as_info, CON_STATUS_LOCK_CAS);

  return ret_value;
}

static int
net_read_int_keep_con_auto (SOCKET clt_sock_fd,
			    MSG_HEADER * client_msg_header,
			    T_REQ_INFO * req_info)
{
  int ret_value = 0;
  int elapsed_sec = 0, elapsed_msec = 0;
  int timeout = 0, remained_timeout = 0;

  if (as_info->con_status == CON_STATUS_IN_TRAN)
    {
      /* holdable results have the same lifespan of a normal session */
      net_timeout_set (shm_appl->session_timeout);
    }
  else
    {
      net_timeout_set (DEFAULT_CHECK_INTERVAL);

#if defined(CAS_FOR_MYSQL)
      timeout = cas_mysql_get_mysql_wait_timeout ();
      remained_timeout = timeout;
#endif /* CAS_FOR_MYSQL */

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

      if (as_info->con_status != CON_STATUS_IN_TRAN
	  && as_info->reset_flag == TRUE)
	{
	  return -1;
	}

      if (as_info->con_status == CON_STATUS_CLOSE
	  || as_info->con_status == CON_STATUS_CLOSE_AND_CONNECT)
	{
	  break;
	}
#if defined (CAS_FOR_MYSQL)
      else if (as_info->con_status == CON_STATUS_OUT_TRAN)
	{
	  remained_timeout -= DEFAULT_CHECK_INTERVAL;
	}
#endif /* CAS_FOR_MYSQL */

      if (net_read_header (clt_sock_fd, client_msg_header) < 0)
	{
	  /* if in-transaction state, return network error */
	  if (as_info->con_status == CON_STATUS_IN_TRAN
	      || !is_net_timed_out ())
	    {
	      ret_value = -1;
	      break;
	    }
	  /* if out-of-transaction state, check whether restart is needed */
	  if (as_info->con_status == CON_STATUS_OUT_TRAN
	      && is_net_timed_out ())
	    {
	      if (restart_is_needed ())
		{
		  cas_log_debug (ARG_FILE_LINE,
				 "net_read_int_keep_con_auto: "
				 "restart_is_needed()");
		  ret_value = -1;
		  break;
		}

	      if (as_info->reset_flag == TRUE)
		{
		  ret_value = -1;
		  break;
		}

#if defined (CAS_FOR_MYSQL)
	      /* this is not real timeout. try again. */
	      if (remained_timeout > 0)
		{
		  continue;
		}

	      /* execute dummy query to reset wait_timeout of MySQL */
	      if (cas_mysql_execute_dummy () >= 0)
		{
		  remained_timeout = timeout;
		  continue;
		}
#endif /* CAS_FOR_MYSQL */
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

  if (as_info->con_status == CON_STATUS_CLOSE
      || as_info->con_status == CON_STATUS_CLOSE_AND_CONNECT)
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

static int
net_read_header_keep_con_on (SOCKET clt_sock_fd,
			     MSG_HEADER * client_msg_header)
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
	  if (as_info->con_status == CON_STATUS_IN_TRAN
	      || !is_net_timed_out ())
	    {
	      ret_value = -1;
	      break;
	    }
	  /* if out-of-transaction state, check whether restart is needed */
	  if (as_info->con_status == CON_STATUS_OUT_TRAN
	      && is_net_timed_out ())
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

static void
set_db_connection_info (void)
{
  if (as_info->fixed_shard_user)
    {
      strncpy (as_info->database_user,
	       shm_appl->shard_conn_info[shm_shard_id].db_user,
	       SRV_CON_DBUSER_SIZE - 1);
      as_info->database_user[SRV_CON_DBUSER_SIZE - 1] = '\0';

      strncpy (as_info->database_passwd,
	       shm_appl->shard_conn_info[shm_shard_id].db_password,
	       SRV_CON_DBPASSWD_SIZE - 1);
      as_info->database_passwd[SRV_CON_DBUSER_SIZE - 1] = '\0';
    }

  strncpy (cas_db_user, as_info->database_user, SRV_CON_DBUSER_SIZE - 1);
  cas_db_user[SRV_CON_DBUSER_SIZE - 1] = '\0';

  strncpy (cas_db_passwd, as_info->database_passwd,
	   SRV_CON_DBPASSWD_SIZE - 1);
  cas_db_passwd[SRV_CON_DBPASSWD_SIZE - 1] = '\0';

  cas_log_debug (ARG_FILE_LINE, "db_name %s db_user %s", cas_db_name,
		 cas_db_user);
}

static void
clear_db_connection_info (void)
{
  if (as_info->fixed_shard_user)
    {
      return;
    }

  cas_db_user[0] = '\0';
  cas_db_passwd[0] = '\0';
  as_info->database_user[0] = '\0';
  as_info->database_passwd[0] = '\0';
}

static bool
need_database_reconnect (void)
{
  if (as_info->force_reconnect)
    {
      return true;
    }
#if defined(CAS_FOR_MYSQL)
  if (strcmp (cas_db_user, as_info->database_user))
#else /* CAS_FOR_MYSQL */
  if (strcasecmp (cas_db_user, as_info->database_user))
#endif /* CAS_FOR_MYSQL */
    {
      return true;
    }

  if (strcmp (cas_db_passwd, as_info->database_passwd))
    {
      return true;
    }

  return false;
}
#endif /* !LIBCAS_FOR_JSP */

static void
set_cas_info_size (void)
{
#if !defined(LIBCAS_FOR_JSP)
  if (cas_shard_flag == OFF && as_info->clt_version <= CAS_MAKE_VER (8, 1, 5))
    {
      cas_info_size = 0;
    }
  else
#endif /* !LIBCAS_FOR_JSP */
    {
      cas_info_size = CAS_INFO_SIZE;
    }
}

#ifndef LIBCAS_FOR_JSP
int
restart_is_needed (void)
{
  if (as_info->num_holdable_results > 0
      || as_info->cas_change_mode == CAS_CHANGE_MODE_KEEP)
    {
      /* we do not want to restart the CAS when there are open
         holdable results or cas_change_mode is CAS_CHANGE_MODE_KEEP */
      return 0;
    }
#if defined(WINDOWS)
  if (shm_appl->use_pdh_flag == TRUE)
    {
      if ((as_info->pid ==
	   as_info->pdh_pid)
	  && (as_info->pdh_workset > shm_appl->appl_server_max_size))
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
      if (cas_req_count % 500 == 0)
	return 1;
      else
	return 0;
    }
#else /* WINDOWS */
  int max_process_size;

#if defined(AIX)
  /* In linux, getsize() returns VSM(55M). but in AIX, getsize() returns
   * vritual meory size for data(900K). so, the size of cub_cas process exceeds
   * 'psize_at_start * 2' very easily. the linux's rule to restart cub_cas
   * is not suit for AIX. In AIX, we use 20M as max_process_size. */
  max_process_size = (shm_appl->appl_server_max_size > 0) ?
    shm_appl->appl_server_max_size : 20 * ONE_K;
#else
  max_process_size = (shm_appl->appl_server_max_size > 0) ?
    shm_appl->appl_server_max_size : (psize_at_start * 2);
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

#if defined(WINDOWS)

LONG WINAPI
CreateMiniDump (struct _EXCEPTION_POINTERS * pException)
{
  TCHAR DumpFile[MAX_PATH] = { 0, };
  TCHAR DumpPath[MAX_PATH] = { 0, };
  SYSTEMTIME SystemTime;
  HANDLE FileHandle;

  GetLocalTime (&SystemTime);

  sprintf (DumpFile, "%d-%d-%d %d_%d_%d.dmp",
	   SystemTime.wYear,
	   SystemTime.wMonth,
	   SystemTime.wDay,
	   SystemTime.wHour, SystemTime.wMinute, SystemTime.wSecond);
  envvar_bindir_file (DumpPath, MAX_PATH, DumpFile);

  FileHandle = CreateFile (DumpPath,
			   GENERIC_WRITE,
			   FILE_SHARE_WRITE,
			   NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

  if (FileHandle != INVALID_HANDLE_VALUE)
    {
      MINIDUMP_EXCEPTION_INFORMATION MiniDumpExceptionInfo;
      BOOL Success;

      MiniDumpExceptionInfo.ThreadId = GetCurrentThreadId ();
      MiniDumpExceptionInfo.ExceptionPointers = pException;
      MiniDumpExceptionInfo.ClientPointers = FALSE;

      Success = MiniDumpWriteDump (GetCurrentProcess (),
				   GetCurrentProcessId (),
				   FileHandle,
				   MiniDumpNormal,
				   (pException) ?
				   &MiniDumpExceptionInfo : NULL, NULL, NULL);
    }

  CloseHandle (FileHandle);

  ux_database_shutdown ();

  return EXCEPTION_EXECUTE_HANDLER;
}
#endif /* WINDOWS */

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
  SHARD_ERR ("<CAS> APPL_SERVER_SHM_KEY_STR:[%d:%x]\n", as_shm_key,
	     as_shm_key);
  shm_appl =
    (T_SHM_APPL_SERVER *) uw_shm_open (as_shm_key, SHM_APPL_SERVER,
				       SHM_MODE_ADMIN);

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
get_graceful_down_timeout (void)
{
  if (as_info->advance_activate_flag)
    {
      return -1;
    }

  return 1 * 60;		/* 1 min */
}

#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
static void
set_db_parameter (void)
{
  int cur_isolation_level;
  int cur_lock_timeout;
  int isolation_level = as_info->isolation_level;
  int lock_timeout = as_info->lock_timeout;

  if (isolation_level == CAS_USE_DEFAULT_DB_PARAM)
    {
      isolation_level = cas_default_isolation_level;
    }

  if (lock_timeout == CAS_USE_DEFAULT_DB_PARAM)
    {
      lock_timeout = cas_default_lock_timeout;
    }

  ux_get_tran_setting (&cur_lock_timeout, &cur_isolation_level);
  if (cur_lock_timeout != lock_timeout)
    {
      ux_set_lock_timeout (lock_timeout);

      cas_log_write_and_end (0, false,
			     "set_db_parameter lock_timeout %d",
			     lock_timeout);
    }

  if (cur_isolation_level != isolation_level)
    {
      ux_set_isolation_level (isolation_level, NULL);

      cas_log_write_and_end (0, false,
			     "set_db_parameter isolation_level %d",
			     isolation_level);
    }
}
#endif /* !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL) */
#endif /* !LIBCAS_FOR_JSP */

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
