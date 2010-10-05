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
#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
#include "environment_variable.h"
#endif /* !CAS_FOR_ORACLE && !CAS_FOR_MYSQL */

static int process_request (SOCKET sock_fd, T_NET_BUF * net_buf,
			    T_REQ_INFO * req_info);

#if defined(WINDOWS)
LONG WINAPI CreateMiniDump (struct _EXCEPTION_POINTERS *pException);
#endif /* WINDOWS */

#ifndef LIBCAS_FOR_JSP
static void cleanup (int signo);
static int cas_init (void);
static void query_cancel (int signo);
static int net_read_int_keep_con_auto (SOCKET clt_sock_fd,
				       MSG_HEADER * client_msg_header,
				       T_REQ_INFO * req_info);
#else /* !LIBCAS_FOR_JSP */
extern int libcas_main (SOCKET jsp_sock_fd);
extern void *libcas_get_db_result_set (int h_id);
extern void libcas_srv_handle_free (int h_id);
#endif /* !LIBCAS_FOR_JSP */

void set_cas_info_size (void);

#ifndef LIBCAS_FOR_JSP
const char *program_name;
char broker_name[32];
int psize_at_start;

int shm_as_index;
T_SHM_APPL_SERVER *shm_appl;
T_APPL_SERVER_INFO *as_info;

struct timeval tran_start_time;
struct timeval query_start_time;
int tran_timeout = 0;
int query_timeout = 0;

bool autocommit_deferred = false;
#endif /* !LIBCAS_FOR_JSP */

int errors_in_transaction = 0;
char stripped_column_name;
char cas_client_type;

#ifndef LIBCAS_FOR_JSP
SOCKET new_req_sock_fd = INVALID_SOCKET;
#endif /* !LIBCAS_FOR_JSP */
int cas_default_isolation_level = 0;
int cas_default_lock_timeout = -1;
int cas_send_result_flag = TRUE;
int cas_info_size = CAS_INFO_SIZE;
char prev_cas_info[CAS_INFO_SIZE];

T_ERROR_INFO err_info;

#if defined(CAS_FOR_ORACLE) || defined(CAS_FOR_MYSQL)
static T_SERVER_FUNC server_fn_table[] = { fn_end_tran,
  fn_prepare,
  fn_execute,
  fn_not_supported,
  fn_not_supported,
  fn_close_req_handle,
  fn_not_supported,
  fn_fetch,
  fn_not_supported,
  fn_not_supported,
  fn_not_supported,
  fn_not_supported,
  fn_not_supported,
  fn_not_supported,
  fn_get_db_version,
  fn_not_supported,
  fn_not_supported,
  fn_not_supported,
  fn_not_supported,
  fn_not_supported,
  fn_not_supported,
  fn_not_supported,
  fn_not_supported,
  fn_not_supported,
  fn_not_supported,
  fn_not_supported,
  fn_not_supported,
  fn_not_supported,
  fn_not_supported,
  fn_not_supported,
  fn_con_close,
  fn_check_cas,
  fn_not_supported,
  fn_not_supported,
};
#else /* CAS_FOR_ORACLE || CAS_FOR_MYSQL */
static T_SERVER_FUNC server_fn_table[] = { fn_end_tran,
  fn_prepare,
  fn_execute,
  fn_get_db_parameter,
  fn_set_db_parameter,
  fn_close_req_handle,
  fn_cursor,
  fn_fetch,
  fn_schema_info,
  fn_oid_get,
  fn_oid_put,
  fn_glo_new,
  fn_glo_save,
  fn_glo_load,
  fn_get_db_version,
  fn_get_class_num_objs,
  fn_oid,
  fn_collection,
  fn_next_result,
  fn_execute_batch,
  fn_execute_array,
  fn_cursor_update,
  fn_get_attr_type_str,
  fn_get_query_info,
  fn_glo_cmd,
  fn_savepoint,
  fn_parameter_info,
  fn_xa_prepare,
  fn_xa_recover,
  fn_xa_end_tran,
  fn_con_close,
  fn_check_cas,
  fn_make_out_rs,
  fn_get_generated_keys,
};
#endif /* CAS_FOR_ORACLE || CAS_FOR_MYSQL */

#ifndef LIBCAS_FOR_JSP
static const char *server_func_name[] = { "end_tran",
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
  "glo_new",
  "glo_save",
  "glo_load",
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
  "glo_cmd",
  "savepoint",
  "parameter_info",
  "xa_prepare",
  "xa_recover",
  "xa_end_tran",
  "con_close",
  "check_cas",
  "fn_make_out_rs",
  "fn_get_generated_keys",
};
#endif /* !LIBCAS_FOR_JSP */

static T_REQ_INFO req_info;
#ifndef LIBCAS_FOR_JSP
static SOCKET srv_sock_fd;
static int cas_req_count = 1;
#endif /* !LIBCAS_FOR_JSP */

#ifndef LIBCAS_FOR_JSP
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
  char *db_name, *db_user, *db_passwd, *t;
  int one = 1, db_info_size;
#if defined(WINDOWS)
  int new_port;
#endif /* WINDOWS */
  char broker_info[BROKER_INFO_SIZE];
  int client_ip_addr;
  char dummy_info[CAS_INFO_SIZE] = { CAS_INFO_STATUS_INACTIVE,
    CAS_INFO_RESERVED_DEFAULT,
    CAS_INFO_RESERVED_DEFAULT,
    CAS_INFO_RESERVED_DEFAULT
  };

  prev_cas_info[CAS_INFO_STATUS] = CAS_INFO_RESERVED_DEFAULT;

#if !defined(WINDOWS)
  signal (SIGTERM, cleanup);
  signal (SIGINT, cleanup);
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
  broker_info[BROKER_INFO_DBMS_TYPE] = DBMS_ORACLE;
#elif defined(CAS_FOR_MYSQL)
  broker_info[BROKER_INFO_DBMS_TYPE] = DBMS_MYSQL;
#else /* CAS_FOR_MYSQL */
  broker_info[BROKER_INFO_DBMS_TYPE] = CCI_DBMS_CUBRID;
#endif /* CAS_FOR_MYSQL */

  set_cubrid_home ();

  if (cas_init () < 0)
    return -1;

#if defined(WINDOWS)
  if (shm_appl->as_port > 0)
    new_port = shm_appl->as_port + shm_as_index;
  else
    new_port = 0;
  srv_sock_fd = net_init_env (&new_port);
#else /* WINDOWS */
  srv_sock_fd = net_init_env ();
#endif /* WINDOWS */
  if (IS_INVALID_SOCKET (srv_sock_fd))
    {
      return -1;
    }

  net_buf_init (&net_buf);
  if ((net_buf.data = (char *) MALLOC (NET_BUF_ALLOC_SIZE)) == NULL)
    {
      return -1;
    }
  net_buf.alloc_size = NET_BUF_ALLOC_SIZE;

  cas_log_open (broker_name, shm_as_index);
  cas_log_write_and_end (0, true, "CAS STARTED pid %d", getpid ());

#if defined(WINDOWS)
  as_info->as_port = new_port;
  as_info->glo_read_size = 0;
  as_info->glo_write_size = 0;
  as_info->glo_flag = 0;
#endif /* WINDOWS */

  as_info->service_ready_flag = TRUE;
  as_info->con_status = CON_STATUS_IN_TRAN;
  as_info->cur_keep_con = KEEP_CON_OFF;
  errors_in_transaction = 0;
#if !defined(WINDOWS)
  psize_at_start = as_info->psize = getsize (getpid ());
#endif /* !WINDOWS */

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
	br_sock_fd = net_connect_client (srv_sock_fd);

	if (IS_INVALID_SOCKET (br_sock_fd))
	  {
	    goto error1;
	  }

	req_info.client_version =
	  CAS_MAKE_VER (as_info->clt_major_version,
			as_info->clt_minor_version,
			as_info->clt_patch_version);

	set_cas_info_size ();

#if defined(WINDOWS)
	as_info->uts_status = UTS_STATUS_BUSY;
#endif /* WINDOWS */
	as_info->con_status = CON_STATUS_IN_TRAN;
	errors_in_transaction = 0;

	net_timeout_set (NET_DEFAULT_TIMEOUT);
	client_ip_addr = 0;

#if defined(WINDOWS)
	client_sock_fd = br_sock_fd;
	if (ioctlsocket (client_sock_fd, FIONBIO, (u_long *) & one) < 0)
	  goto error1;
	memcpy (&client_ip_addr, as_info->cas_clt_ip, 4);
#else /* WINDOWS */
	client_sock_fd = recv_fd (br_sock_fd, &client_ip_addr);

	if (client_sock_fd == -1)
	  {
	    CLOSE_SOCKET (br_sock_fd);
	    goto retry;
	  }

	net_write_stream (br_sock_fd, "OK", 2);
	CLOSE_SOCKET (br_sock_fd);
#endif /* WINDOWS */

	cas_log_open (broker_name, shm_as_index);
	as_info->cur_sql_log2 = shm_appl->sql_log2;
	sql_log2_init (broker_name, shm_as_index, as_info->cur_sql_log2,
		       false);
#if defined(CAS_FOR_ORACLE) || defined(CAS_FOR_MYSQL)
	cas_error_log_open (broker_name, shm_as_index);
#endif


	t = ut_uchar2ipstr ((unsigned char *) (&client_ip_addr));
	cas_log_write_and_end (0, false, "CLIENT IP %s", t);
	strncpy (as_info->clt_ip_addr, t, 19);
	setsockopt (client_sock_fd, IPPROTO_TCP, TCP_NODELAY, (char *) &one,
		    sizeof (one));

	if (IS_INVALID_SOCKET (client_sock_fd))
	  {
	    goto error1;
	  }


	req_info.client_version =
	  CAS_MAKE_VER (as_info->clt_major_version,
			as_info->clt_minor_version,
			as_info->clt_patch_version);
	cas_client_type = as_info->cas_client_type;

	if (req_info.client_version < CAS_MAKE_VER (8, 2, 0))
	  {
	    db_info_size = SRV_CON_DB_INFO_SIZE_PRIOR_8_2_0;
	  }
	else
	  {
	    db_info_size = SRV_CON_DB_INFO_SIZE;
	  }
	if (net_read_stream (client_sock_fd, read_buf, db_info_size) < 0)
	  {
	    NET_WRITE_ERROR_CODE (client_sock_fd, req_info.client_version,
				  dummy_info, CAS_ERROR_INDICATOR,
				  CAS_ER_COMMUNICATION);
	  }
	else
	  {
	    char *db_err_msg = NULL, *url;
	    struct timeval cas_start_time;

	    gettimeofday (&cas_start_time, NULL);
	    db_name = read_buf;

	    db_user = db_name + SRV_CON_DBNAME_SIZE;
	    db_passwd = db_user + SRV_CON_DBUSER_SIZE;
	    url = db_passwd + SRV_CON_DBPASSWD_SIZE;
	    db_name[SRV_CON_DBNAME_SIZE - 1] =
	      db_user[SRV_CON_DBUSER_SIZE - 1] =
	      db_passwd[SRV_CON_DBPASSWD_SIZE - 1] = '\0';
	    if (db_user[0] == '\0')
	      strcpy (db_user, "public");

	    cas_log_debug (ARG_FILE_LINE,
			   "db_name %s db_user %s db_passwd %s url %s",
			   db_name, db_user, db_passwd, url);
	    if (as_info->reset_flag == TRUE)
	      {
		cas_log_debug (ARG_FILE_LINE, "main: set reset_flag");
		set_db_connect_status (-1);	/* DB_CONNECTION_STATUS_RESET */
		as_info->reset_flag = FALSE;
	      }
	    err_code =
	      ux_database_connect (db_name, db_user, db_passwd, &db_err_msg);
	    if (err_code < 0)
	      {
		if (db_err_msg == NULL)
		  {
		    NET_WRITE_ERROR_CODE (client_sock_fd,
					  req_info.client_version, dummy_info,
					  err_info.err_indicator,
					  err_info.err_number);
		  }
		else
		  {
		    NET_WRITE_ERROR_CODE_WITH_MSG (client_sock_fd,
						   req_info.client_version,
						   dummy_info,
						   err_info.err_indicator,
						   err_info.err_number,
						   db_err_msg);
		  }
		CLOSE_SOCKET (client_sock_fd);
		FREE_MEM (db_err_msg);

		goto error1;
	      }
	    cas_log_write_and_end (0, false, "connect db %s user %s url %s",
				   db_name, db_user, url);

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
		/* PID + BROKER_INFO */
		char msgbuf[4 + BROKER_INFO_SIZE];
		int tmpint = htonl (getpid ());

		as_info->cur_keep_con = shm_appl->keep_connection;
		if (as_info->cur_keep_con != KEEP_CON_OFF)
		  {
		    broker_info[BROKER_INFO_KEEP_CONNECTION] =
		      CAS_KEEP_CONNECTION_ON;
		  }
		net_write_int (client_sock_fd, 4 + BROKER_INFO_SIZE);
		if (cas_info_size > 0)
		  {
		    net_write_stream (client_sock_fd, dummy_info,
				      cas_info_size);
		  }

		memcpy (msgbuf, &tmpint, 4);
		memcpy (msgbuf + 4, broker_info, BROKER_INFO_SIZE);
		net_write_stream (client_sock_fd, msgbuf,
				  4 + BROKER_INFO_SIZE);
	      }
	    while (0);

	    req_info.need_rollback = TRUE;

	    gettimeofday (&tran_start_time, NULL);
	    gettimeofday (&query_start_time, NULL);
	    tran_timeout = 0;
	    query_timeout = 0;

#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
	    cas_log_error_handler_begin ();
#endif /* !CAS_FOR_ORACLE && !CAS_FOR_MYSQL */
	    err_code = 0;
	    while (err_code >= 0)
	      {
#if !defined(WINDOWS)
		signal (SIGUSR1, query_cancel);
#endif /* !WINDOWS */
		err_code =
		  process_request (client_sock_fd, &net_buf, &req_info);
#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
		cas_log_error_handler_clear ();
#endif /* !CAS_FOR_ORACLE && !CAS_FOR_MYSQL */
#if !defined(WINDOWS)
		signal (SIGUSR1, SIG_IGN);
#endif /* !WINDOWS */
#if defined(WINDOWS)
		if (as_info->glo_read_size < 0)
		  {
		    as_info->glo_read_size = 0;
		  }
		if (as_info->glo_write_size < 0)
		  {
		    as_info->glo_write_size = 0;
		  }
		as_info->glo_flag = 0;
#endif /* WINDOWS */
		as_info->last_access_time = time (NULL);
	      }

	    prev_cas_info[CAS_INFO_STATUS] = CAS_INFO_RESERVED_DEFAULT;

	    if (as_info->cur_statement_pooling)
	      {
		hm_srv_handle_free_all ();
	      }

	    if (is_xa_prepared ())
	      {
		ux_database_shutdown ();
		ux_database_connect (db_name, db_user, db_passwd, NULL);
	      }
	    else
	      {
		ux_end_tran (CCI_TRAN_ROLLBACK, false);
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
				client_ip_addr);
	      }

	  }

	CLOSE_SOCKET (client_sock_fd);

      error1:
#if defined(WINDOWS)
	as_info->close_flag = 1;
#endif /* WINDOWS */

	as_info->clt_ip_addr[0] = '\0';
	cas_log_write_and_end (0, true, "disconnect");
	cas_log_write2 (sql_log2_get_filename ());
	cas_log_write_and_end (0, false, "STATE idle");
	cas_log_close (true);
	sql_log2_end (true);
#if defined(CAS_FOR_ORACLE) || defined(CAS_FOR_MYSQL)
	cas_error_log_close (true);
#endif

	if (is_server_aborted ())
	  {
	    as_info->uts_status = UTS_STATUS_RESTART;
	  }
	else
	  if (!
	      (as_info->cur_keep_con == KEEP_CON_AUTO
	       && as_info->con_status == CON_STATUS_CLOSE_AND_CONNECT))
	  {
	    if (restart_is_needed ())
	      as_info->uts_status = UTS_STATUS_RESTART;
	    else
	      as_info->uts_status = UTS_STATUS_IDLE;
	  }
	cas_req_count++;
      }
#if defined(WINDOWS)
  }
  __except (CreateMiniDump (GetExceptionInformation ()))
  {
  }
#endif /* WINDOWS */

  return 0;
}
#else /* !LIBCAS_FOR_JSP */
int
libcas_main (SOCKET jsp_sock_fd)
{
  T_NET_BUF net_buf;
  SOCKET client_sock_fd;
  char broker_info[BROKER_INFO_SIZE];
  int err_code;

  memset (&req_info, 0, sizeof (req_info));
  memset (broker_info, 0, sizeof (broker_info));

  req_info.client_version = CAS_CUR_VERSION;
  broker_info[BROKER_INFO_DBMS_TYPE] = CCI_DBMS_CUBRID;
  client_sock_fd = jsp_sock_fd;

  net_buf_init (&net_buf);
  if ((net_buf.data = (char *) MALLOC (NET_BUF_ALLOC_SIZE)) == NULL)
    {
      return 0;
    }
  net_buf.alloc_size = NET_BUF_ALLOC_SIZE;

  while (1)
    {
      err_code = process_request (client_sock_fd, &net_buf, &req_info);
      if (err_code < 0)
	break;
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

#ifndef LIBCAS_FOR_JSP
static void
cleanup (int signo)
{
#ifdef MEM_DEBUG
  int fd;
#endif

  signal (signo, SIG_IGN);

  ux_database_shutdown ();

  cas_log_write_and_end (0, true, "CAS TERMINATED pid %d", getpid ());
  cas_log_close (true);
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

  exit (0);
}

static void
query_cancel (int signo)
{
#if defined(CAS_FOR_ORACLE)
  signal (signo, SIG_IGN);
  cas_oracle_query_cancel ();
#elif defined(CAS_FOR_MYSQL)
#else /* CAS_FOR_CUBRID */
  signal (signo, SIG_IGN);
  db_set_interrupt (1);
#endif /* CAS_FOR_ORACLE */
  cas_log_write (0, false, "query_cancel");
}
#endif /* !LIBCAS_FOR_JSP */

static int
process_request (SOCKET clt_sock_fd, T_NET_BUF * net_buf,
		 T_REQ_INFO * req_info)
{
  MSG_HEADER client_msg_header;
  MSG_HEADER cas_msg_header;
  char *read_msg;
  char func_code;
  int argc;
  void **argv = NULL;
  int err_code;
  T_SERVER_FUNC server_fn;

  error_info_clear ();
  init_msg_header (&client_msg_header);
  init_msg_header (&cas_msg_header);

#ifndef LIBCAS_FOR_JSP
  if (as_info->cur_keep_con == KEEP_CON_AUTO)
    {
      err_code = net_read_int_keep_con_auto (clt_sock_fd,
					     &client_msg_header, req_info);
    }
  else
    {
      net_timeout_set (shm_appl->session_timeout);
      err_code = net_read_header (clt_sock_fd, &client_msg_header);

      if ((as_info->cur_keep_con == KEEP_CON_ON)
	  && (as_info->con_status == CON_STATUS_OUT_TRAN))
	{
	  as_info->con_status = CON_STATUS_IN_TRAN;
	  errors_in_transaction = 0;
	}
    }
#else /* !LIBCAS_FOR_JSP */
  net_timeout_set (60);
  err_code = net_read_header (clt_sock_fd, &client_msg_header);
#endif /* !LIBCAS_FOR_JSP */

  if (err_code < 0)
    {
      const char *cas_log_msg = NULL;
      NET_WRITE_ERROR_CODE (clt_sock_fd, req_info->client_version,
			    cas_msg_header.info_ptr, CAS_ERROR_INDICATOR,
			    CAS_ER_COMMUNICATION);
#ifndef LIBCAS_FOR_JSP
      if (as_info->con_status == CON_STATUS_CLOSE_AND_CONNECT)
	{
	  cas_log_msg = "CHANGE CLIENT";
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
      return -1;
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
      NET_WRITE_ERROR_CODE (clt_sock_fd, req_info->client_version,
			    cas_msg_header.info_ptr, CAS_ERROR_INDICATOR,
			    CAS_ER_NO_MORE_MEMORY);
      return -1;
    }
  if (net_read_stream (clt_sock_fd, read_msg,
		       *(client_msg_header.msg_body_size_ptr)) < 0)
    {
      FREE_MEM (read_msg);
      NET_WRITE_ERROR_CODE (clt_sock_fd, req_info->client_version,
			    cas_msg_header.info_ptr, CAS_ERROR_INDICATOR,
			    CAS_ER_COMMUNICATION);
      cas_log_write_and_end (0, true,
			     "COMMUNICATION ERROR net_read_stream()");
      return -1;
    }

  argc = net_decode_str (read_msg, *(client_msg_header.msg_body_size_ptr),
			 &func_code, &argv);
  if (argc < 0)
    {
      FREE_MEM (read_msg);
      NET_WRITE_ERROR_CODE (clt_sock_fd, req_info->client_version,
			    cas_msg_header.info_ptr, CAS_ERROR_INDICATOR,
			    CAS_ER_COMMUNICATION);
      return argc;
    }

  if (func_code <= 0 || func_code >= CAS_FC_MAX)
    {
      FREE_MEM (argv);
      FREE_MEM (read_msg);
      NET_WRITE_ERROR_CODE (clt_sock_fd, req_info->client_version,
			    cas_msg_header.info_ptr, CAS_ERROR_INDICATOR,
			    CAS_ER_COMMUNICATION);
      return CAS_ER_COMMUNICATION;
    }

#ifndef LIBCAS_FOR_JSP
  strcpy (as_info->log_msg, server_func_name[func_code - 1]);

#if defined(WINDOWS)
  if (!(func_code == CAS_FC_GLO_NEW || func_code == CAS_FC_GLO_SAVE))
    {
      as_info->glo_read_size = 0;
    }
#endif /* WINDOWS */
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

  err_code = (*server_fn) (clt_sock_fd, argc, argv, net_buf, req_info);
#ifndef LIBCAS_FOR_JSP
  cas_log_debug (ARG_FILE_LINE, "process_request: %s() err_code %d",
		 server_func_name[func_code - 1], err_info.err_number);
#endif /* !LIBCAS_FOR_JSP */

#ifndef LIBCAS_FOR_JSP
  if (err_code == 0 && net_buf->err_code == 0
      && req_info->need_auto_commit != TRAN_NOT_AUTOCOMMIT)
    {
      /* no error and auto commit is needed */
      err_code = ux_auto_commit (net_buf, req_info);

      as_info->num_transactions_processed %= MAX_DIAG_DATA_VALUE;
      as_info->num_transactions_processed++;
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
#if defined(WINDOWS)
  as_info->glo_read_size = 0;
#endif /* WINDOWS */

  as_info->log_msg[0] = '\0';
#endif /* !LIBCAS_FOR_JSP */

  if (net_buf->err_code)
    {
      NET_WRITE_ERROR_CODE (clt_sock_fd, req_info->client_version,
			    cas_msg_header.info_ptr, CAS_ERROR_INDICATOR,
			    net_buf->err_code);
      err_code = CAS_ERROR_INDICATOR;
      goto exit_on_end;
    }

#ifndef LIBCAS_FOR_JSP
#if defined(WINDOWS)
  if (net_buf->post_send_file == NULL)
    as_info->glo_write_size = 0;
  else
    as_info->glo_write_size = net_buf->post_file_size;
#endif /* WINDOWS */
#endif /* !LIBCAS_FOR_JSP */
  if (cas_send_result_flag && net_buf->data != NULL)
    {
      *(cas_msg_header.msg_body_size_ptr) = htonl (net_buf->data_size);

      memcpy (net_buf->data, cas_msg_header.msg_body_size_ptr,
	      NET_BUF_HEADER_MSG_SIZE);
#ifndef LIBCAS_FOR_JSP
      if (as_info->con_status == CON_STATUS_IN_TRAN)
	{
	  cas_msg_header.info_ptr[CAS_INFO_STATUS] = CAS_INFO_STATUS_ACTIVE;
	}
      else
	{
	  cas_msg_header.info_ptr[CAS_INFO_STATUS] = CAS_INFO_STATUS_INACTIVE;
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
      if (cas_info_size > 0)
	{
	  memcpy (net_buf->data + NET_BUF_HEADER_MSG_SIZE,
		  cas_msg_header.info_ptr, cas_info_size);
	}
      if (net_write_stream (clt_sock_fd, net_buf->data,
			    NET_BUF_CURR_SIZE (net_buf)) < 0)
	{
	  cas_log_write_and_end (0, true,
				 "COMMUNICATION ERROR net_write_stream()");
	}
    }
  if (cas_send_result_flag && net_buf->post_send_file != NULL)
    {
      err_code = net_write_from_file (clt_sock_fd,
				      net_buf->post_file_size,
				      net_buf->post_send_file);
      unlink (net_buf->post_send_file);
      if (err_code < 0)
	{
	  goto exit_on_end;
	}
    }

#ifndef LIBCAS_FOR_JSP
  if (as_info->reset_flag && as_info->con_status != CON_STATUS_IN_TRAN)
    {
      cas_log_debug (ARG_FILE_LINE,
		     "process_request: reset_flag && !CON_STATUS_IN_TRAN");
      err_code = -1;
      goto exit_on_end;
    }
#endif /* !LIBCAS_FOR_JSP */

exit_on_end:
  net_buf_clear (net_buf);

  FREE_MEM (read_msg);
  FREE_MEM (argv);

  return err_code;
}

#ifndef LIBCAS_FOR_JSP
static int
cas_init ()
{
  char *tmp_p;

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

  set_cubrid_file (FID_SQL_LOG_DIR, shm_appl->log_dir);
  set_cubrid_file (FID_CUBRID_ERR_DIR, shm_appl->err_log_dir);

  as_pid_file_create (broker_name, shm_as_index);
  as_db_err_log_set (broker_name, shm_as_index);

  return 0;
}
#endif /* !LIBCAS_FOR_JSP */


#ifndef LIBCAS_FOR_JSP
static int
net_read_int_keep_con_auto (SOCKET clt_sock_fd,
			    MSG_HEADER * client_msg_header,
			    T_REQ_INFO * req_info)
{
  int ret_value = 0;
  int elapsed_sec = 0, elapsed_msec = 0;

  if (as_info->con_status == CON_STATUS_IN_TRAN)
    {
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
	  errors_in_transaction = 0;
	}
    }

  CON_STATUS_UNLOCK (&(shm_appl->as_info[shm_as_index]), CON_STATUS_LOCK_CAS);

  return ret_value;
}
#endif /* !LIBCAS_FOR_JSP */

void
set_cas_info_size (void)
{
#ifndef LIBCAS_FOR_JSP
  if (CAS_MAKE_VER (as_info->clt_major_version,
		    as_info->clt_minor_version,
		    as_info->clt_patch_version) <= CAS_MAKE_VER (8, 1, 5))
#else /* !LIBCAS_FOR_JSP */
  if (CAS_CUR_VERSION <= CAS_MAKE_VER (8, 1, 5))
#endif /* !LIBCAS_FOR_JSP */
    {
      cas_info_size = 0;
    }
  else
    {
      cas_info_size = CAS_INFO_SIZE;
    }
}

#ifndef LIBCAS_FOR_JSP
int
restart_is_needed (void)
{
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
