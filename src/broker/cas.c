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

#if defined(CUBRID_SHARD)
#include "shard_metadata.h"
#include "shard_shm.h"
#endif /* CUBRID_SHARD */

#define FUNC_NEEDS_RESTORING_CON_STATUS(func_code) \
  (((func_code) == CAS_FC_GET_DB_PARAMETER) \
   ||((func_code) == CAS_FC_SET_DB_PARAMETER) \
   ||((func_code) == CAS_FC_CLOSE_REQ_HANDLE) \
   ||((func_code) == CAS_FC_GET_DB_VERSION) \
   ||((func_code) == CAS_FC_GET_ATTR_TYPE_STR) \
   ||((func_code) == CAS_FC_CURSOR_CLOSE) \
   ||((func_code) == CAS_FC_END_SESSION))

static FN_RETURN process_request (SOCKET sock_fd, T_NET_BUF * net_buf,
				  T_REQ_INFO * req_info);

#if defined(WINDOWS)
LONG WINAPI CreateMiniDump (struct _EXCEPTION_POINTERS *pException);
#endif /* WINDOWS */

#ifndef LIBCAS_FOR_JSP
static void cas_sig_handler (int signo);
static int cas_init (void);
static void cas_final (void);
static void cas_free (bool free_srv_handle);
static void query_cancel (int signo);

#if defined(CUBRID_SHARD)
static int cas_init_shm (void);
static int cas_register_to_proxy (SOCKET proxy_sock_fd);
static int net_read_process (SOCKET proxy_sock_fd,
			     MSG_HEADER * client_msg_header,
			     T_REQ_INFO * req_info);
#else
static int net_read_int_keep_con_auto (SOCKET clt_sock_fd,
				       MSG_HEADER * client_msg_header,
				       T_REQ_INFO * req_info);
#endif /* CUBRID_SHARD */

#else /* !LIBCAS_FOR_JSP */
extern int libcas_main (SOCKET jsp_sock_fd);
extern void *libcas_get_db_result_set (int h_id);
extern void libcas_srv_handle_free (int h_id);
#endif /* !LIBCAS_FOR_JSP */

void set_cas_info_size (void);

#ifndef LIBCAS_FOR_JSP
const char *program_name;
char broker_name[BROKER_NAME_LEN];
int psize_at_start;

int shm_as_index;
T_SHM_APPL_SERVER *shm_appl;
T_APPL_SERVER_INFO *as_info;
#if defined(CUBRID_SHARD)
int shm_proxy_id = -1;
int shm_shard_id = -1;
char *shm_as_cp = NULL;
T_SHARD_INFO *shard_info_p = NULL;
#endif /* CUBRID_SHARD */

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
  fn_not_supported,		/* CAS_FC_MAKE_OUT_RS */
  fn_not_supported,		/* CAS_FC_GET_GENERATED_KEYS */
  fn_not_supported,		/* CAS_FC_LOB_NEW */
  fn_not_supported,		/* CAS_FC_LOB_WRITE */
  fn_not_supported,		/* CAS_FC_LOB_READ */
  fn_not_supported,		/* CAS_FC_END_SESSION */
  fn_not_supported,		/* CAS_FC_GET_ROW_COUNT */
  fn_not_supported,		/* CAS_FC_GET_LAST_INSERT_ID */
  fn_not_supported,		/* CAS_FC_CURSOR_CLOSE */
  fn_not_supported		/* CAS_FC_PREPARE_AND_EXECUTE */
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
  fn_cursor_close,		/* CAS_FC_CURSOR_CLOSE */
  fn_prepare_and_execute	/* CAS_FC_PREPARE_AND_EXECUTE */
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
  "fn_cursor_close",
  "fn_prepare_and_execute"
};
#endif /* !LIBCAS_FOR_JSP */

static T_REQ_INFO req_info;
#ifndef LIBCAS_FOR_JSP
static SOCKET srv_sock_fd;
static int cas_req_count = 1;
#endif /* !LIBCAS_FOR_JSP */

#ifndef LIBCAS_FOR_JSP
#if defined(CUBRID_SHARD)
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
  T_NET_BUF net_buf;
  SOCKET proxy_sock_fd = INVALID_SOCKET;
  char read_buf[1024];
  int err_code;
  char *t, *db_sessionid;
  char db_name[MAX_HA_DBNAME_LENGTH];
  char db_user[SRV_CON_DBUSER_SIZE];
  char db_passwd[SRV_CON_DBPASSWD_SIZE];
#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
  SESSION_ID session_id = DB_EMPTY_SESSION;
#endif /* !CAS_FOR_ORACLE && !CAS_FOR_MYSQL */
  int one = 1, db_info_size;
  int con_status;
#if defined(WINDOWS)
  int new_port;
#endif /* WINDOWS */
  char cas_info[CAS_INFO_SIZE] = { CAS_INFO_STATUS_INACTIVE,
    CAS_INFO_RESERVED_DEFAULT,
    CAS_INFO_RESERVED_DEFAULT,
    CAS_INFO_RESERVED_DEFAULT
  };
  FN_RETURN fn_ret = FN_KEEP_CONN;

  MSG_HEADER proxy_msg_header;
  MSG_HEADER cas_msg_header;
  int cas_id;
  short proxy_ack_shard_id;
  short proxy_ack_cas_id;

  char *url;
  struct timeval cas_start_time;

  char func_code = 0x01;
  int error;

  prev_cas_info[CAS_INFO_STATUS] = CAS_INFO_RESERVED_DEFAULT;

#if !defined(WINDOWS)
  signal (SIGTERM, cas_sig_handler);
  signal (SIGINT, cas_sig_handler);
  signal (SIGUSR1, SIG_IGN);
  signal (SIGPIPE, SIG_IGN);
  signal (SIGXFSZ, SIG_IGN);
#endif /* WINDOWS */

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

  if (cas_init () < 0)
    return -1;

  net_buf_init (&net_buf);
  net_buf.data = (char *) MALLOC (NET_BUF_ALLOC_SIZE);
  if (net_buf.data == NULL)
    {
      return -1;
    }
  net_buf.alloc_size = NET_BUF_ALLOC_SIZE;

  as_info->service_ready_flag = TRUE;
  as_info->con_status = CON_STATUS_IN_TRAN;
  as_info->cur_keep_con = KEEP_CON_OFF;
  errors_in_transaction = 0;
#if !defined(WINDOWS)
  psize_at_start = as_info->psize = getsize (getpid ());
#endif /* !WINDOWS */

  stripped_column_name = shm_appl->stripped_column_name;

conn_retry:
  do
    {
      SLEEP_SEC (1);
    }
  while (as_info->uts_status == UTS_STATUS_RESTART);

  net_timeout_set (-1);

  cas_log_open (broker_name, shm_as_index);
  cas_slow_log_open (broker_name, shm_as_index);
  cas_log_write_and_end (0, true, "CAS STARTED pid %d", getpid ());
#if defined(CAS_FOR_ORACLE) || defined(CAS_FOR_MYSQL)
  cas_error_log_open (broker_name, shm_as_index);
#endif

  /* TODO: SHARD support only 8.4.0 above */
  req_info.client_version = CAS_PROTO_CURRENT_VER;

  set_cas_info_size ();

  gettimeofday (&cas_start_time, NULL);

#if defined(CAS_FOR_ORACLE) || defined(CAS_FOR_MYSQL)
  snprintf (db_name, MAX_HA_DBNAME_LENGTH, "%s", shard_info_p->db_name);
#else
  snprintf (db_name, MAX_HA_DBNAME_LENGTH, "%s@%s",
	    shard_info_p->db_name, shard_info_p->db_conn_info);
#endif /* CAS_FOR_ORACLE || CAS_FOR_MYSQL */
  strncpy (db_user, shard_info_p->db_user, SRV_CON_DBUSER_SIZE - 1);
  db_user[SRV_CON_DBUSER_SIZE - 1] = '\0';

  strncpy (db_passwd, shard_info_p->db_password, SRV_CON_DBPASSWD_SIZE - 1);
  db_passwd[SRV_CON_DBPASSWD_SIZE - 1] = '\0';

  if (req_info.client_version >= CAS_MAKE_VER (8, 2, 0))
    {
      url = db_passwd + SRV_CON_DBPASSWD_SIZE;
      url[SRV_CON_URL_SIZE - 1] = '\0';
    }

  if (req_info.client_version >= CAS_MAKE_VER (8, 4, 0))
    {
      db_sessionid = url + SRV_CON_URL_SIZE;
      db_sessionid[SRV_CON_DBSESS_ID_SIZE - 1] = '\0';
#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
      db_set_session_id (atol (db_sessionid));
#endif /* !CAS_FOR_ORACLE && !CAS_FOR_MYSQL */
    }

  cas_log_debug (ARG_FILE_LINE,
		 "db_name %s db_user %s db_passwd %s url %s "
		 "session id %s", db_name, db_user, db_passwd, url,
		 db_sessionid);
  if (as_info->reset_flag == TRUE)
    {
      cas_log_debug (ARG_FILE_LINE, "main: set reset_flag");
      set_db_connect_status (-1);	/* DB_CONNECTION_STATUS_RESET */
      as_info->reset_flag = FALSE;
    }

  err_code = ux_database_connect (db_name, db_user, db_passwd, NULL);

  if (err_code < 0)
    {
      SLEEP_SEC (1);
      goto error1;
    }

  as_info->uts_status = UTS_STATUS_IDLE;

conn_proxy_retry:
  net_timeout_set (NET_DEFAULT_TIMEOUT);

  proxy_sock_fd = net_connect_proxy ();

  if (proxy_sock_fd == INVALID_SOCKET)
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

  shard_info_p->service_ready_flag = true;

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

#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
  session_id = db_get_session_id ();
  cas_log_write_and_end (0, false, "connect db %s user %s url %s"
			 " session id %u", db_name, db_user, url, session_id);
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
#if defined(WINDOWS)
  __try
  {
#endif /* WINDOWS */
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

#if defined(CUBRID_SHARD)
	    if (as_info->con_status == CON_STATUS_OUT_TRAN
		&& hm_srv_handle_get_current_count () >=
		shm_appl->max_prepared_stmt_count)
	      {
		fn_ret = FN_CLOSE_CONN;
	      }
#endif
	  }

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
	  }

#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
	cas_log_error_handler_end ();
#endif /* !CAS_FOR_ORACLE && !CAS_FOR_MYSQL */
      error1:
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
#else /* CUBRID_SHARD */
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
#endif				/* !WINDOWS */
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
#endif /* WINDOWS */
  char broker_info[BROKER_INFO_SIZE];
  int client_ip_addr;
  char cas_info[CAS_INFO_SIZE] = { CAS_INFO_STATUS_INACTIVE,
    CAS_INFO_RESERVED_DEFAULT,
    CAS_INFO_RESERVED_DEFAULT,
    CAS_INFO_RESERVED_DEFAULT
  };
  FN_RETURN fn_ret = FN_KEEP_CONN;
  char client_ip_str[16];

  prev_cas_info[CAS_INFO_STATUS] = CAS_INFO_RESERVED_DEFAULT;

#if !defined(WINDOWS)
  signal (SIGTERM, cas_sig_handler);
  signal (SIGINT, cas_sig_handler);
  signal (SIGUSR1, SIG_IGN);
  signal (SIGPIPE, SIG_IGN);
  signal (SIGXFSZ, SIG_IGN);
#endif /* WINDOWS */

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

  memset (broker_info, 0, sizeof (broker_info));
#if defined(CAS_FOR_ORACLE)
  broker_info[BROKER_INFO_DBMS_TYPE] = CCI_DBMS_ORACLE;
#elif defined(CAS_FOR_MYSQL)
  broker_info[BROKER_INFO_DBMS_TYPE] = CCI_DBMS_MYSQL;
#else /* CAS_FOR_MYSQL */
  broker_info[BROKER_INFO_DBMS_TYPE] = CCI_DBMS_CUBRID;
#endif /* CAS_FOR_MYSQL */

  broker_info[BROKER_INFO_PROTO_VERSION] = CAS_PROTO_PACK_CURRENT_NET_VER;
  broker_info[BROKER_INFO_RESERVED1] = 0;
  broker_info[BROKER_INFO_RESERVED2] = 0;
  broker_info[BROKER_INFO_RESERVED3] = 0;

  set_cubrid_home ();

  if (cas_init () < 0)
    {
      return -1;
    }

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
  srv_sock_fd = net_init_env ();
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

  cas_log_open (broker_name, shm_as_index);
  cas_slow_log_open (broker_name, shm_as_index);
  cas_log_write_and_end (0, true, "CAS STARTED pid %d", getpid ());

#if defined(WINDOWS)
  as_info->as_port = new_port;
#endif /* WINDOWS */

  as_info->service_ready_flag = TRUE;
  as_info->con_status = CON_STATUS_IN_TRAN;
  cas_info[CAS_INFO_STATUS] = CAS_INFO_STATUS_ACTIVE;
  as_info->transaction_start_time = time (0);
  as_info->cur_keep_con = KEEP_CON_OFF;
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
	br_sock_fd = net_connect_client (srv_sock_fd);

	if (IS_INVALID_SOCKET (br_sock_fd))
	  {
	    goto error1;
	  }

	req_info.client_version = as_info->clt_version;

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
	    goto error1;
	  }
	memcpy (&client_ip_addr, as_info->cas_clt_ip, 4);
#else /* WINDOWS */
	net_timeout_set (NET_MIN_TIMEOUT);

	if (net_read_int (br_sock_fd, &con_status) < 0)
	  {
	    cas_log_write_and_end (0, false,
				   "HANDSHAKE ERROR net_read_int(con_status)");
	    CLOSE_SOCKET (br_sock_fd);
	    goto error1;
	  }
	if (net_write_int (br_sock_fd, as_info->con_status) < 0)
	  {
	    cas_log_write_and_end (0, false,
				   "HANDSHAKE ERROR net_write_int(con_status)");
	    CLOSE_SOCKET (br_sock_fd);
	    goto error1;
	  }

	client_sock_fd = recv_fd (br_sock_fd, &client_ip_addr);
	if (client_sock_fd == -1)
	  {
	    cas_log_write_and_end (0, false, "HANDSHAKE ERROR recv_fd %d",
				   client_sock_fd);
	    CLOSE_SOCKET (br_sock_fd);
	    goto error1;
	  }
	if (net_write_int (br_sock_fd, as_info->uts_status) < 0)
	  {
	    cas_log_write_and_end (0, false,
				   "HANDSHAKE ERROR net_write_int(uts_status)");
	    CLOSE_SOCKET (br_sock_fd);
	    CLOSE_SOCKET (client_sock_fd);
	    goto error1;
	  }

	CLOSE_SOCKET (br_sock_fd);
#endif /* WINDOWS */

	net_timeout_set (NET_DEFAULT_TIMEOUT);

	cas_log_open (broker_name, shm_as_index);
	cas_slow_log_open (broker_name, shm_as_index);
	as_info->cur_sql_log2 = shm_appl->sql_log2;
	sql_log2_init (broker_name, shm_as_index, as_info->cur_sql_log2,
		       false);
#if defined(CAS_FOR_ORACLE) || defined(CAS_FOR_MYSQL)
	cas_error_log_open (broker_name, shm_as_index);
#endif
	ut_get_ipv4_string (client_ip_str, sizeof (client_ip_str),
			    (unsigned char *) (&client_ip_addr));
	cas_log_write_and_end (0, false, "CLIENT IP %s", client_ip_str);
	setsockopt (client_sock_fd, IPPROTO_TCP, TCP_NODELAY, (char *) &one,
		    sizeof (one));
	ut_set_keepalive (client_sock_fd, 1800);

	if (IS_INVALID_SOCKET (client_sock_fd))
	  {
	    goto error1;
	  }
#if !defined(WINDOWS)
	else
	  {
	    /* send NO_ERROR to client */
	    if (net_write_int (client_sock_fd, 0) < 0)
	      {
		CLOSE_SOCKET (client_sock_fd);
		goto error1;
	      }
	  }
#endif
	req_info.client_version = as_info->clt_version;
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
	    NET_WRITE_ERROR_CODE (client_sock_fd, req_info.client_version,
				  cas_info, CAS_ERROR_INDICATOR,
				  CAS_ER_COMMUNICATION);
	  }
	else
	  {
	    unsigned char *ip_addr;
	    char *db_err_msg = NULL, *url;
	    struct timeval cas_start_time;

	    gettimeofday (&cas_start_time, NULL);

	    db_name = read_buf;
	    db_name[SRV_CON_DBNAME_SIZE - 1] = '\0';

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
		db_sessionid = url + SRV_CON_URL_SIZE;
		db_sessionid[SRV_CON_DBSESS_ID_SIZE - 1] = '\0';
#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
		db_set_session_id (atol (db_sessionid));
#endif /* !CAS_FOR_ORACLE && !CAS_FOR_MYSQL */
	      }

	    cas_log_debug (ARG_FILE_LINE,
			   "db_name %s db_user %s db_passwd %s url %s "
			   "session id %s", db_name, db_user, db_passwd, url,
			   db_sessionid);
	    if (as_info->reset_flag == TRUE)
	      {
		cas_log_debug (ARG_FILE_LINE, "main: set reset_flag");
		set_db_connect_status (-1);	/* DB_CONNECTION_STATUS_RESET */
		as_info->reset_flag = FALSE;
	      }

	    ip_addr = (unsigned char *) (&client_ip_addr);

	    if (shm_appl->access_control)
	      {
		if (access_control_check_right
		    (shm_appl, db_name, db_user, ip_addr) < 0)
		  {
		    char err_msg[1024];

		    sprintf (err_msg,
			     "Authorization error.(Address %s is rejected)",
			     ut_get_ipv4_string (client_ip_str,
						 sizeof (client_ip_str),
						 as_info->cas_clt_ip));

		    NET_WRITE_ERROR_CODE_WITH_MSG (client_sock_fd,
						   req_info.client_version,
						   cas_info,
						   DBMS_ERROR_INDICATOR,
						   CAS_ER_NOT_AUTHORIZED_CLIENT,
						   err_msg);
		    cas_log_write_and_end (0, false,
					   "connect db %s user %s url %s - rejected",
					   db_name, db_user, url);

		    if (shm_appl->access_log == ON)
		      {
			cas_access_log (&cas_start_time, shm_as_index,
					client_ip_addr, db_name, db_user,
					false);
		      }

		    CLOSE_SOCKET (client_sock_fd);

		    goto error1;
		  }
	      }

	    err_code =
	      ux_database_connect (db_name, db_user, db_passwd, &db_err_msg);

	    if (err_code < 0)
	      {
		if (db_err_msg == NULL)
		  {
		    NET_WRITE_ERROR_CODE (client_sock_fd,
					  req_info.client_version, cas_info,
					  err_info.err_indicator,
					  err_info.err_number);
		  }
		else
		  {
		    NET_WRITE_ERROR_CODE_WITH_MSG (client_sock_fd,
						   req_info.client_version,
						   cas_info,
						   err_info.err_indicator,
						   err_info.err_number,
						   db_err_msg);
		  }
		CLOSE_SOCKET (client_sock_fd);
		FREE_MEM (db_err_msg);

		goto error1;
	      }
#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
	    session_id = db_get_session_id ();
	    cas_log_write_and_end (0, false, "connect db %s user %s url %s"
				   " session id %u", db_name, db_user, url,
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

	    as_info->cur_keep_con = KEEP_CON_OFF;
	    broker_info[BROKER_INFO_KEEP_CONNECTION] =
	      CAS_KEEP_CONNECTION_OFF;

	    if (shm_appl->statement_pooling)
	      {
		broker_info[BROKER_INFO_STATEMENT_POOLING] =
		  CAS_STATEMENT_POOLING_ON;
		as_info->cur_statement_pooling = ON;
	      }
	    else
	      {
		broker_info[BROKER_INFO_STATEMENT_POOLING] =
		  CAS_STATEMENT_POOLING_OFF;
		as_info->cur_statement_pooling = OFF;
	      }
	    broker_info[BROKER_INFO_CCI_PCONNECT] =
	      (shm_appl->cci_pconnect ? CCI_PCONNECT_ON : CCI_PCONNECT_OFF);

	    do
	      {
		/* PID + BROKER_INFO + SESSION_ID */
		char msgbuf[CAS_CONNECTION_REPLY_SIZE];
		int caspid = htonl (getpid ());
		unsigned int sessid = 0;

#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
		sessid = htonl (session_id);
#endif /* !CAS_FOR_ORACLE && !CAS_FOR_MYSQL */

		as_info->cur_keep_con = shm_appl->keep_connection;
		if (as_info->cur_keep_con != KEEP_CON_OFF)
		  {
		    broker_info[BROKER_INFO_KEEP_CONNECTION] =
		      CAS_KEEP_CONNECTION_ON;
		  }

		net_write_int (client_sock_fd, CAS_CONNECTION_REPLY_SIZE);

		if (cas_info_size > 0)
		  {
		    net_write_stream (client_sock_fd, cas_info,
				      cas_info_size);
		  }

		memcpy (msgbuf, &caspid, CAS_PID_SIZE);
		memcpy (msgbuf + CAS_PID_SIZE, broker_info, BROKER_INFO_SIZE);
		memcpy (msgbuf + CAS_PID_SIZE + BROKER_INFO_SIZE, &sessid,
			SESSION_ID_SIZE);
		net_write_stream (client_sock_fd, msgbuf,
				  CAS_CONNECTION_REPLY_SIZE);
	      }
	    while (0);

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
#endif
	    fn_ret = FN_KEEP_CONN;
	    while (fn_ret == FN_KEEP_CONN)
	      {
#if !defined(WINDOWS)
		signal (SIGUSR1, query_cancel);
#endif /* !WINDOWS */
		fn_ret = process_request (client_sock_fd, &net_buf,
					  &req_info);
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
	      }

#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
	    cas_log_error_handler_end ();
#endif /* !CAS_FOR_ORACLE && !CAS_FOR_MYSQL */

	    if (shm_appl->access_log == ON)
	      {
		cas_access_log (&cas_start_time, shm_as_index,
				client_ip_addr, db_name, db_user, true);
	      }

	  }

	CLOSE_SOCKET (client_sock_fd);

      error1:
#if defined(WINDOWS)
	as_info->close_flag = 1;
#endif /* WINDOWS */
	if (as_info->con_status != CON_STATUS_CLOSE_AND_CONNECT)
	  {
	    memset (as_info->cas_clt_ip, 0x0, sizeof (as_info->cas_clt_ip));
	    as_info->cas_clt_port = 0;
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
#endif /* CUBRID_SHARD */
#else /* !LIBCAS_FOR_JSP */
int
libcas_main (SOCKET jsp_sock_fd)
{
  T_NET_BUF net_buf;
  SOCKET client_sock_fd;

#if defined(CUBRID_SHARD)
  /* not support LIBCAS_FOR_JSP in CUBRID_SHARD */
  return 0;
#endif

  memset (&req_info, 0, sizeof (req_info));

  req_info.client_version = CAS_PROTO_CURRENT_VER;
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

#if defined(CUBRID_SHARD)
  /* not support LIBCAS_FOR_JSP in CUBRID_SHARD */
  return 0;
#endif

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
#if defined(CUBRID_SHARD)
  /* not support LIBCAS_FOR_JSP in CUBRID_SHARD */
  return 0;
#endif

  hm_srv_handle_free (h_id);
}
#endif /* !LIBCAS_FOR_JSP */

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

  max_process_size = (shm_appl->appl_server_max_size > 0) ?
    shm_appl->appl_server_max_size : (psize_at_start * 2);
  if (as_info->psize > max_process_size)
    {
      cas_log_write_and_end (0, true,
			     "CAS MEMORY USAGE HAS EXCEEDED MAX SIZE (%dM)",
			     max_process_size / ONE_K);
    }

  if (as_info->psize > shm_appl->appl_server_hard_limit)
    {
      cas_log_write_and_end (0, true,
			     "CAS MEMORY USAGE HAS EXCEEDED HARD LIMIT (%dM)",
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

  /* At here, can not free alloced net_buf->data;
   * simply exit
   */
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

#if defined(CUBRID_SHARD)
  err_code = net_read_process (sock_fd, &client_msg_header, req_info);
  if (err_code < 0)
    {
      const char *cas_log_msg = NULL;
      NET_WRITE_ERROR_CODE (sock_fd, req_info->client_version,
			    cas_msg_header.info_ptr, CAS_ERROR_INDICATOR,
			    CAS_ER_COMMUNICATION);
      fn_ret = FN_CLOSE_CONN;

      if (is_net_timed_out ())
	{
#if defined(CAS_FOR_MYSQL)
	  cas_log_msg = "SESSION TIMEOUT OR MYSQL CONNECT TIMEOUT";
#else
	  cas_log_msg = "SESSION TIMEOUT";
#endif
	}
      else
	{
	  cas_log_msg = "COMMUNICATION ERROR net_read_header()";
	}
      cas_log_write_and_end (0, true, cas_log_msg);
      return fn_ret;
    }
#else /* CUBRID_SHARD */
#ifndef LIBCAS_FOR_JSP
  if (as_info->cur_keep_con == KEEP_CON_AUTO)
    {
      err_code = net_read_int_keep_con_auto (sock_fd,
					     &client_msg_header, req_info);
    }
  else
    {
      net_timeout_set (shm_appl->session_timeout);
      err_code = net_read_header (sock_fd, &client_msg_header);

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
      NET_WRITE_ERROR_CODE (sock_fd, req_info->client_version,
			    cas_msg_header.info_ptr, CAS_ERROR_INDICATOR,
			    CAS_ER_COMMUNICATION);
      fn_ret = FN_CLOSE_CONN;

#ifndef LIBCAS_FOR_JSP
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
	      cas_log_msg = "SESSION TIMEOUT";
	    }
	  else
	    {
	      cas_log_msg = "COMMUNICATION ERROR net_read_header()";
	    }
	}
      cas_log_write_and_end (0, true, cas_log_msg);
      return fn_ret;
    }
#endif /* CUBRID_SHARD */

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

  if (shm_appl->session_timeout < 0)
    net_timeout_set (NET_DEFAULT_TIMEOUT);
  else
    net_timeout_set (MIN (shm_appl->session_timeout, NET_DEFAULT_TIMEOUT));
#else /* !LIBCAS_FOR_JSP */
  net_timeout_set (NET_DEFAULT_TIMEOUT);
#endif /* LIBCAS_FOR_JSP */

  read_msg = (char *) MALLOC (*(client_msg_header.msg_body_size_ptr));

  if (read_msg == NULL)
    {
      NET_WRITE_ERROR_CODE (sock_fd, req_info->client_version,
			    cas_msg_header.info_ptr, CAS_ERROR_INDICATOR,
			    CAS_ER_NO_MORE_MEMORY);
      return FN_CLOSE_CONN;
    }
  if (net_read_stream (sock_fd, read_msg,
		       *(client_msg_header.msg_body_size_ptr)) < 0)
    {
      FREE_MEM (read_msg);
      NET_WRITE_ERROR_CODE (sock_fd, req_info->client_version,
			    cas_msg_header.info_ptr, CAS_ERROR_INDICATOR,
			    CAS_ER_COMMUNICATION);
      cas_log_write_and_end (0, true,
			     "COMMUNICATION ERROR net_read_stream()");
      return FN_CLOSE_CONN;
    }

  argc = net_decode_str (read_msg, *(client_msg_header.msg_body_size_ptr),
			 &func_code, &argv);
  if (argc < 0)
    {
      FREE_MEM (read_msg);
      NET_WRITE_ERROR_CODE (sock_fd, req_info->client_version,
			    cas_msg_header.info_ptr, CAS_ERROR_INDICATOR,
			    CAS_ER_COMMUNICATION);
      return FN_CLOSE_CONN;
    }

  if (func_code <= 0 || func_code >= CAS_FC_MAX)
    {
      FREE_MEM (argv);
      FREE_MEM (read_msg);
      NET_WRITE_ERROR_CODE (sock_fd, req_info->client_version,
			    cas_msg_header.info_ptr, CAS_ERROR_INDICATOR,
			    CAS_ER_COMMUNICATION);
      return FN_CLOSE_CONN;
    }

#ifndef LIBCAS_FOR_JSP
  con_status_to_restore = -1;

  if (FUNC_NEEDS_RESTORING_CON_STATUS (func_code)
      && (as_info->cur_keep_con == KEEP_CON_AUTO
	  || as_info->cur_keep_con == KEEP_CON_ON))
    {
      con_status_to_restore = (con_status_before_check_cas != -1) ?
	con_status_before_check_cas : old_con_status;

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
	  cas_log_debug (ARG_FILE_LINE, "[%d][PREV : %d, RECV : %d], "
			 "[preffunc : %d, recvfunc : %d], [REQ: %d, REQ: %d], "
			 "[JID : %d] \n",
			 func_code - 1, prev_cas_info[CAS_INFO_STATUS],
			 client_msg_header.info_ptr[CAS_INFO_STATUS],
			 prev_cas_info[CAS_INFO_RESERVED_1],
			 client_msg_header.info_ptr[CAS_INFO_RESERVED_1],
			 prev_cas_info[CAS_INFO_RESERVED_2],
			 client_msg_header.info_ptr[CAS_INFO_RESERVED_2],
			 client_msg_header.info_ptr[CAS_INFO_RESERVED_3]);
	}
#endif /* end for debug */
    }
#endif /* !LIBCAS_FOR_JSP */

#ifndef LIBCAS_FOR_JSP
  req_info->need_auto_commit = TRAN_NOT_AUTOCOMMIT;
#endif /* !LIBCAS_FOR_JSP */
  cas_send_result_flag = TRUE;

  fn_ret = (*server_fn) (sock_fd, argc, argv, net_buf, req_info);
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

#if defined(CUBRID_SHARD)
  if (func_code == CAS_FC_PREPARE &&
      (client_msg_header.info_ptr[CAS_INFO_ADDITIONAL_FLAG] &
       CAS_INFO_FLAG_MASK_FORCE_OUT_TRAN))
    {
      /* for shard dummy prepare */
      req_info->need_auto_commit = TRAN_AUTOROLLBACK;
    }
#endif /* CUBRID_SHARD */

#ifndef LIBCAS_FOR_JSP
  if (fn_ret == FN_KEEP_CONN && net_buf->err_code == 0
      && as_info->con_status == CON_STATUS_IN_TRAN
      && req_info->need_auto_commit != TRAN_NOT_AUTOCOMMIT)
    {
      /* no error and auto commit is needed */
      err_code = ux_auto_commit (net_buf, req_info);
      if (err_code < 0)
	{
	  fn_ret = FN_CLOSE_CONN;
	}
      else if (as_info->cur_keep_con != KEEP_CON_OFF)
	{
	  if (as_info->cas_log_reset)
	    {
	      cas_log_reset (broker_name, shm_as_index);
	    }
	  if (as_info->cas_slow_log_reset)
	    {
	      cas_slow_log_reset (broker_name, shm_as_index);
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
	      sql_log2_init (broker_name, shm_as_index, as_info->cur_sql_log2,
			     true);
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
#endif /* !LIBCAS_FOR_JSP */

#ifndef LIBCAS_FOR_JSP
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
#endif /* !LIBCAS_FOR_JSP */

#ifndef LIBCAS_FOR_JSP
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
      NET_WRITE_ERROR_CODE (sock_fd, req_info->client_version,
			    cas_msg_header.info_ptr, CAS_ERROR_INDICATOR,
			    net_buf->err_code);
      fn_ret = FN_CLOSE_CONN;
      goto exit_on_end;
    }

  if (cas_send_result_flag && net_buf->data != NULL)
    {
      *(cas_msg_header.msg_body_size_ptr) = htonl (net_buf->data_size);

      memcpy (net_buf->data, cas_msg_header.msg_body_size_ptr,
	      NET_BUF_HEADER_MSG_SIZE);
#ifndef LIBCAS_FOR_JSP
      cas_msg_header.info_ptr[CAS_INFO_ADDITIONAL_FLAG] &=
	~CAS_INFO_FLAG_MASK_AUTOCOMMIT;
      cas_msg_header.info_ptr[CAS_INFO_ADDITIONAL_FLAG] |=
	(as_info->cci_default_autocommit & CAS_INFO_FLAG_MASK_AUTOCOMMIT);
#if defined(CUBRID_SHARD)
      cas_msg_header.info_ptr[CAS_INFO_ADDITIONAL_FLAG] &=
	~CAS_INFO_FLAG_MASK_FORCE_OUT_TRAN;
#endif

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
      if (cas_info_size > 0)
	{
	  memcpy (net_buf->data + NET_BUF_HEADER_MSG_SIZE,
		  cas_msg_header.info_ptr, cas_info_size);
	}
      if (net_write_stream (sock_fd, net_buf->data,
			    NET_BUF_CURR_SIZE (net_buf)) < 0)
	{
	  cas_log_write_and_end (0, true,
				 "COMMUNICATION ERROR net_write_stream()");
	}
    }

#if !defined(CUBRID_SHARD)
  if (cas_send_result_flag && net_buf->post_send_file != NULL)
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
#endif /* CUBRID_SHARD */

#ifndef LIBCAS_FOR_JSP
  if (as_info->reset_flag
      && ((as_info->con_status != CON_STATUS_IN_TRAN
	   && as_info->num_holdable_results < 1)
	  || (get_db_connect_status () == -1)))
    {
      cas_log_debug (ARG_FILE_LINE,
		     "process_request: reset_flag && !CON_STATUS_IN_TRAN");
      fn_ret = FN_CLOSE_CONN;
      goto exit_on_end;
    }
#endif /* !LIBCAS_FOR_JSP */

exit_on_end:
  net_buf_clear (net_buf);

  FREE_MEM (read_msg);
  FREE_MEM (argv);

  return fn_ret;
}

#ifndef LIBCAS_FOR_JSP
static int
cas_init ()
{
  char *tmp_p;

#if defined(CUBRID_SHARD)
  if (cas_init_shm () < 0)
    {
      return -1;
    }

  strcpy (broker_name, shm_appl->broker_name);
#else
  if (as_get_my_as_info (broker_name, &shm_as_index) < 0)
    {
      return -1;
    }

  if (shm_as_index < 0)
    {
      return -1;
    }

  tmp_p = getenv (APPL_SERVER_SHM_KEY_STR);
  if (tmp_p == NULL)
    {
      return -1;
    }

  shm_appl =
    (T_SHM_APPL_SERVER *) uw_shm_open (atoi (tmp_p), SHM_APPL_SERVER,
				       SHM_MODE_ADMIN);
  if (shm_appl == NULL)
    {
      return -1;
    }
  as_info = &(shm_appl->as_info[shm_as_index]);
#endif /* CUBRID_SHARD */

  set_cubrid_file (FID_SQL_LOG_DIR, shm_appl->log_dir);
  set_cubrid_file (FID_SLOW_LOG_DIR, shm_appl->slow_log_dir);
  set_cubrid_file (FID_CUBRID_ERR_DIR, shm_appl->err_log_dir);

#if defined(CUBRID_SHARD)
  as_pid_file_create (broker_name, shm_proxy_id, shm_shard_id, shm_as_index);
  as_db_err_log_set (broker_name, shm_proxy_id, shm_shard_id, shm_as_index);
#else
  as_pid_file_create (broker_name, PROXY_INVALID_ID, SHARD_INVALID_ID,
		      shm_as_index);
  as_db_err_log_set (broker_name, PROXY_INVALID_ID, SHARD_INVALID_ID,
		     shm_as_index);
#endif /* CUBRID_SHARD */

  return 0;
}
#endif /* !LIBCAS_FOR_JSP */


#ifndef LIBCAS_FOR_JSP
#if defined(CUBRID_SHARD)
static int
net_read_process (SOCKET proxy_sock_fd,
		  MSG_HEADER * client_msg_header, T_REQ_INFO * req_info)
{
  int ret_value = 0;
  int elapsed_sec = 0, elapsed_msec = 0;

  if (as_info->con_status == CON_STATUS_IN_TRAN)
    {
      net_timeout_set (shm_appl->session_timeout);
    }
  else
    {
#if defined(CAS_FOR_MYSQL)
      net_timeout_set (MYSQL_CONNECT_TIMEOUT);
#else
      net_timeout_set (-1);
#endif
    }

  do
    {
      if (as_info->cas_log_reset)
	{
	  cas_log_reset (broker_name, shm_as_index);
	}

      if (as_info->con_status == CON_STATUS_CLOSE)
	{
	  break;
	}

      /*
         net_read_header error case.
         case 1 : disconnect with proxy_sock_fd
         case 2 : CON_STATUS_IN_TRAN && session_timeout
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
	      if (restart_is_needed ())
		{
		  cas_log_debug (ARG_FILE_LINE, "net_read_process: "
				 "restart_is_needed()");
		  ret_value = -1;
		  break;
		}
#if defined(CAS_FOR_MYSQL)
	      /* MYSQL_CONNECT_TIMEOUT case */
	      ret_value = -1;
	      break;
#endif
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
	  cas_log_write_client_ip (as_info->cas_clt_ip);
	  as_info->con_status = CON_STATUS_IN_TRAN;
	  errors_in_transaction = 0;
	}
    }

  CON_STATUS_UNLOCK (as_info, CON_STATUS_LOCK_CAS);

  return ret_value;
}
#else
static int
net_read_int_keep_con_auto (SOCKET clt_sock_fd,
			    MSG_HEADER * client_msg_header,
			    T_REQ_INFO * req_info)
{
  int ret_value = 0;
  int elapsed_sec = 0, elapsed_msec = 0;

  if (as_info->con_status == CON_STATUS_IN_TRAN)
    {
      /* holdable results have the same lifespan of a normal session */
      net_timeout_set (shm_appl->session_timeout);
    }
  else
    {
      net_timeout_set (1);
      new_req_sock_fd = srv_sock_fd;
    }

  do
    {
      if (as_info->cas_log_reset)
	{
	  cas_log_reset (broker_name, shm_as_index);
	}
      if (as_info->cas_slow_log_reset)
	{
	  cas_slow_log_reset (broker_name, shm_as_index);
	}

      if (as_info->con_status == CON_STATUS_CLOSE
	  || as_info->con_status == CON_STATUS_CLOSE_AND_CONNECT)
	{
	  break;
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
	      if (restart_is_needed ())
		{
		  cas_log_debug (ARG_FILE_LINE, "net_read_int_keep_con_auto: "
				 "restart_is_needed()");
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
#endif
#endif /* !LIBCAS_FOR_JSP */

void
set_cas_info_size (void)
{
#if !defined(LIBCAS_FOR_JSP) && !defined(CUBRID_SHARD)
  if (as_info->clt_version <= CAS_MAKE_VER (8, 1, 5))
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
  if (as_info->num_holdable_results > 0)
    {
      /* we do not want to restart the CAS when there are open
         holdable results */
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

  max_process_size = (shm_appl->appl_server_max_size > 0) ?
    shm_appl->appl_server_max_size : (psize_at_start * 2);

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
				   (pException) ? &MiniDumpExceptionInfo :
				   NULL, NULL, NULL);
    }

  CloseHandle (FileHandle);

  ux_database_shutdown ();

  return EXCEPTION_EXECUTE_HANDLER;
}
#endif /* WINDOWS */
#endif /* !LIBCAS_FOR_JSP */

#if defined(CUBRID_SHARD)
int
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

  if (net_write_int (proxy_sock_fd, shm_as_index) < 0)
    {
      cas_log_write_and_end (0, false, "HANDSHAKE ERROR send cas_id");
      return -1;
    }

  return 0;
}

int
cas_init_shm (void)
{
  char *p;
  int as_shm_key;
  int pxy_id, shd_id, as_id;
  T_PROXY_INFO *proxy_info_p;

  p = getenv (APPL_SERVER_SHM_KEY_STR);
  if (p == NULL)
    {
      goto return_error;
    }
  as_shm_key = strtoul (p, NULL, 10);
  SHARD_ERR ("<CAS> APPL_SERVER_SHM_KEY_STR:[%d:%x]\n", as_shm_key,
	     as_shm_key);

  p = getenv (PROXY_ID_ENV_STR);
  if (p == NULL)
    {
      goto return_error;
    }
  pxy_id = strtoul (p, NULL, 10);
  SHARD_ERR ("<CAS> PROXY_ID_ENV_STR:[%d]\n", pxy_id);
  shm_proxy_id = pxy_id;

  p = getenv (SHARD_ID_ENV_STR);
  if (p == NULL)
    {
      goto return_error;
    }
  shd_id = strtoul (p, NULL, 10);
  SHARD_ERR ("<CAS> SHARD_ID_ENV_STR:[%d]\n", shd_id);
  shm_shard_id = shd_id;

  p = getenv (AS_ID_ENV_STR);
  if (p == NULL)
    {
      goto return_error;
    }
  as_id = strtoul (p, NULL, 10);
  SHARD_ERR ("<CAS> AS_ID_ENV_STR:[%d]\n", as_id);
  shm_as_index = as_id;

  shm_as_cp =
    (char *) uw_shm_open (as_shm_key, SHM_APPL_SERVER, SHM_MODE_ADMIN);
  if (shm_as_cp == NULL)
    {
      goto return_error;
    }

  shm_appl = shard_shm_get_appl_server (shm_as_cp);
  if (shm_appl == NULL)
    {
      goto return_error;
    }

  proxy_info_p = shard_shm_get_proxy_info (shm_as_cp, shm_proxy_id);
  if (proxy_info_p == NULL)
    {
      goto return_error;
    }

  shard_info_p = shard_shm_find_shard_info (proxy_info_p, shm_shard_id);
  if (shard_info_p == NULL)
    {
      goto return_error;
    }

  as_info = shard_shm_get_as_info (proxy_info_p, shm_shard_id, as_id);
  if (as_info == NULL)
    {
      goto return_error;
    }

#if 1
  /* SHARD TODO : tuning cur_keep_con parameter */
  as_info->cur_keep_con = 1;
#endif

  return 0;
return_error:

  if (shm_as_cp)
    {
      uw_shm_detach (shm_as_cp);
      shm_as_cp = NULL;
    }

  return -1;
}
#endif /* CUBRID_SHARD */
