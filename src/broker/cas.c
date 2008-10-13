/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * cas.c -
 */

#ident "$Id$"

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>

#ifdef WIN32
#include <winsock2.h>
#include <windows.h>
#include <process.h>
#include <sys/timeb.h>
#include <dbgHelp.h>
#else
#include <unistd.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#endif

#include "cas_common.h"
#include "cas.h"
#include "net_cas.h"
#include "func.h"
#include "net_buf.h"
#include "cas_log.h"
#include "cas_util.h"
#include "file_name.h"
#include "exec_db.h"
#ifndef WIN32
#include "recv_fd.h"
#endif
#ifdef DIAG_DEVEL
#include "perf_monitor.h"
#endif

#include "shm.h"
#include "env_str_def.h"
#include "getsize.h"
#include "as_util.h"
#include "sql_log2.h"



static int process_request (int sock_fd, T_NET_BUF * net_buf,
			    T_REQ_INFO * req_info);


#if defined(WINDOWS)
#define EXECUTABLE_BIN_DIR "bin"
LONG WINAPI CreateMiniDump(struct _EXCEPTION_POINTERS *pException);
#endif

#ifndef LIBCAS_FOR_JSP
static void cleanup (int signo);
static int cas_init (void);
static void query_cancel (int signo);
static int net_read_int_keep_con_auto (int clt_sock_fd, int *msg_size);
#endif


#ifndef LIBCAS_FOR_JSP
char broker_name[32];
int shm_as_index;
T_SHM_APPL_SERVER *shm_appl;
char sql_log_mode;
T_TIMEVAL tran_start_time;
#endif

char stripped_column_name;
char cas_client_type;

#ifndef LIBCAS_FOR_JSP
int need_auto_commit = FALSE;
int new_req_sock_fd = 0;
#endif
int cas_default_isolation_level = 0;
int cas_default_lock_timeout = -1;
int cas_send_result_flag = TRUE;

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

#ifndef LIBCAS_FOR_JSP
static char *server_func_name[] = { "end_tran",
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
#endif

static T_REQ_INFO req_info;
#ifndef LIBCAS_FOR_JSP
static int srv_sock_fd;
static int cas_req_count = 1;
#endif

#ifndef LIBCAS_FOR_JSP
#ifdef WIN32
int WINAPI
WinMain (HINSTANCE hInstance,	// handle to current instance
	 HINSTANCE hPrevInstance,	// handle to previous instance
	 LPSTR lpCmdLine,	// pointer to command line
	 int nShowCmd		// show state of window
  )
#else
int
main (int argc, char *argv[])
#endif
{
  T_NET_BUF net_buf;
  int br_sock_fd, client_sock_fd;
  char read_buf[1024];
  int err_code;
  char *db_name;
  int one = 1;
#ifdef WIN32
  int new_port;
#endif
  char *db_user, *db_passwd;
  char *tmp_db_user, *tmp_db_passwd;
  char broker_info[BROKER_INFO_SIZE];
  int client_ip_addr;
  char print_cas_pid = TRUE;

#ifndef WIN32
  signal (SIGTERM, cleanup);
  signal (SIGINT, cleanup);
  signal (SIGUSR1, SIG_IGN);
  signal (SIGPIPE, SIG_IGN);
  signal (SIGXFSZ, SIG_IGN);
#endif

#ifndef WIN32
  if (argc == 2 && strcmp (argv[1], "--version") == 0)
    {
      printf ("%s\n", makestring (BUILD_NUMBER));
      return 0;
    }
#endif

  memset (&req_info, 0, sizeof (req_info));

  memset (broker_info, 0, sizeof (broker_info));
  BROKER_INFO_DBMS_TYPE (broker_info) = CCI_DBMS_CUBRID;

  set_cubrid_home ();

  if (cas_init () < 0)
    return -1;

#ifdef WIN32
  if (shm_appl->as_port > 0)
    new_port = shm_appl->as_port + shm_as_index;
  else
    new_port = 0;
  srv_sock_fd = net_init_env (&new_port);
#else
  srv_sock_fd = net_init_env ();
#endif
  if (srv_sock_fd < 0)
    {
      return -1;
    }

  net_buf_init (&net_buf);
  if ((net_buf.data = (char *) MALLOC (NET_BUF_ALLOC_SIZE)) == NULL)
    {
      return -1;
    }
  net_buf.alloc_size = NET_BUF_ALLOC_SIZE;

  db_name = getenv ("UNICAS_DATABASE");
  if (db_name)
    {
      tmp_db_user = getenv ("UNICAS_DB_USER");
      tmp_db_passwd = getenv ("UNICAS_DB_PASSWD");
      if (tmp_db_user == NULL || tmp_db_user[0] == '\0')
	tmp_db_user = "public";
      if (tmp_db_passwd == NULL)
	tmp_db_passwd = "";
      UX_DATABASE_CONNECT (db_name, tmp_db_user, tmp_db_passwd, NULL,
			   req_info.isql);
    }

  if (shm_appl->sql_log_mode & SQL_LOG_MODE_ON)
    {
      sql_log_mode = SQL_LOG_MODE_ON | SQL_LOG_MODE_APPEND;
      cas_log_open (broker_name, shm_as_index);
      cas_log_end (NULL, broker_name, shm_as_index, 0, NULL, 0, TRUE);
    }

#ifdef WIN32
  shm_appl->as_info[shm_as_index].as_port = new_port;
  shm_appl->as_info[shm_as_index].glo_read_size = 0;
  shm_appl->as_info[shm_as_index].glo_write_size = 0;
  shm_appl->as_info[shm_as_index].glo_flag = 0;
#endif

  shm_appl->as_info[shm_as_index].service_ready_flag = TRUE;
  shm_appl->as_info[shm_as_index].con_status = CON_STATUS_IN_TRAN;
  shm_appl->as_info[shm_as_index].cur_keep_con = KEEP_CON_OFF;
#ifndef WIN32
  shm_appl->as_info[shm_as_index].psize = getsize (getpid ());
#endif

  stripped_column_name = shm_appl->stripped_column_name;

#if defined(WINDOWS)
  __try{
#endif

  for (;;)
    {
      br_sock_fd = net_connect_client (srv_sock_fd);
      if (br_sock_fd < 0)
	{
	  goto error1;
	}

#ifdef WIN32
      shm_appl->as_info[shm_as_index].uts_status = UTS_STATUS_BUSY;
#endif
      shm_appl->as_info[shm_as_index].con_status = CON_STATUS_IN_TRAN;

      net_timeout_set (NET_DEFAULT_TIMEOUT);
      TIMEVAL_MAKE (&tran_start_time);
      client_ip_addr = 0;

#ifdef WIN32
      client_sock_fd = br_sock_fd;
      if (ioctlsocket (client_sock_fd, FIONBIO, (u_long *) & one) < 0)
	goto error1;
      memcpy (&client_ip_addr, shm_appl->as_info[shm_as_index].cas_clt_ip, 4);
#else
      client_sock_fd = recv_fd (br_sock_fd, &client_ip_addr);
      net_write_stream (br_sock_fd, "OK", 2);
      CLOSE_SOCKET (br_sock_fd);
#endif

      sql_log_mode = shm_appl->sql_log_mode;
      if (!(sql_log_mode & SQL_LOG_MODE_ON))
	{
	  sql_log_mode = SQL_LOG_MODE_OFF;
	}

      cas_log_open (broker_name, shm_as_index);
      shm_appl->as_info[shm_as_index].cur_sql_log2 = shm_appl->sql_log2;
      sql_log2_init (broker_name, shm_as_index,
		     shm_appl->as_info[shm_as_index].cur_sql_log2, FALSE);

      if (print_cas_pid)
	{
	  cas_log_write (0, TRUE, "CAS_STARTED pid:%d", getpid ());
	  print_cas_pid = FALSE;
	}
      cas_log_write (0, TRUE,
		     "connect %s",
		     ut_uchar2ipstr ((unsigned char *) (&client_ip_addr)));
      setsockopt (client_sock_fd, IPPROTO_TCP, TCP_NODELAY, (char *) &one,
		  sizeof (one));

      if (client_sock_fd < 0)
	{
	  goto error1;
	}


      req_info.client_version =
	CAS_MAKE_VER (shm_appl->as_info[shm_as_index].clt_major_version,
		      shm_appl->as_info[shm_as_index].clt_minor_version,
		      shm_appl->as_info[shm_as_index].clt_patch_version);
      cas_client_type = shm_appl->as_info[shm_as_index].cas_client_type;

      if (net_read_stream (client_sock_fd, read_buf, SRV_CON_DB_INFO_SIZE) <
	  0)
	{
	  NET_WRITE_ERROR_CODE (client_sock_fd, CAS_ER_COMMUNICATION);
	}
      else
	{
	  char *db_err_msg = NULL;


	  db_name = read_buf;

	  db_user = db_name + SRV_CON_DBNAME_SIZE;
	  db_passwd = db_user + SRV_CON_DBUSER_SIZE;
	  db_name[SRV_CON_DBNAME_SIZE - 1] =
	    db_user[SRV_CON_DBUSER_SIZE - 1] =
	    db_passwd[SRV_CON_DBPASSWD_SIZE - 1] = '\0';
	  if (db_user[0] == '\0')
	    strcpy (db_user, "public");

	  cas_log_write (0, TRUE, "connect %s %s", db_name, db_user);

	  err_code =
	    UX_DATABASE_CONNECT (db_name, db_user, db_passwd, &db_err_msg,
				 req_info.isql);
	  if (err_code < 0)
	    {
	      if (db_err_msg == NULL)
		{
		  NET_WRITE_ERROR_CODE (client_sock_fd, err_code);
		}
	      else
		{
		  net_write_int (client_sock_fd, strlen (db_err_msg) + 5);
		  net_write_int (client_sock_fd, err_code);
		  net_write_stream (client_sock_fd, db_err_msg,
				    strlen (db_err_msg) + 1);
		}
	      CLOSE_SOCKET (client_sock_fd);
	      FREE_MEM (db_err_msg);

	      goto error1;
	    }

	  UX_SET_DEFAULT_SETTING ();

	  shm_appl->as_info[shm_as_index].auto_commit_mode = FALSE;
	  cas_log_write (0, TRUE,
			 "isolation level : %d, lock timeout : %d, auto_commit : %s",
			 cas_default_isolation_level,
			 cas_default_lock_timeout,
			 shm_appl->as_info[shm_as_index].
			 auto_commit_mode ? "true" : "false");

	  shm_appl->as_info[shm_as_index].cur_keep_con = KEEP_CON_OFF;
	  BROKER_INFO_KEEP_CONNECTION (broker_info) = CAS_KEEP_CONNECTION_OFF;

	  if (shm_appl->statement_pooling)
	    {
	      BROKER_INFO_STATEMENT_POOLING (broker_info) =
		CAS_STATEMENT_POOLING_ON;
	      shm_appl->as_info[shm_as_index].cur_statement_pooling = ON;
	    }
	  else
	    {
	      BROKER_INFO_STATEMENT_POOLING (broker_info) =
		CAS_STATEMENT_POOLING_OFF;
	      shm_appl->as_info[shm_as_index].cur_statement_pooling = OFF;
	    }

	  do
	    {
	      char msgbuf[4 + BROKER_INFO_SIZE];
	      int tmpint = htonl (getpid ());

	      shm_appl->as_info[shm_as_index].cur_keep_con =
		shm_appl->keep_connection;
	      if (shm_appl->as_info[shm_as_index].cur_keep_con !=
		  KEEP_CON_OFF)
		{
		  BROKER_INFO_KEEP_CONNECTION (broker_info) =
		    CAS_KEEP_CONNECTION_ON;
		}
	      net_write_int (client_sock_fd, 4 + BROKER_INFO_SIZE);
	      memcpy (msgbuf, &tmpint, 4);
	      memcpy (msgbuf + 4, broker_info, BROKER_INFO_SIZE);
	      net_write_stream (client_sock_fd, msgbuf, 4 + BROKER_INFO_SIZE);
	    }
	  while (0);

	  req_info.need_rollback = TRUE;

	  while (1)
	    {
#ifndef WIN32
	      signal (SIGUSR1, query_cancel);
#endif
	      err_code =
		process_request (client_sock_fd, &net_buf, &req_info);
#ifndef WIN32
	      signal (SIGUSR1, SIG_IGN);
#endif
#ifdef WIN32
	      if (shm_appl->as_info[shm_as_index].glo_read_size < 0)
		shm_appl->as_info[shm_as_index].glo_read_size = 0;
	      if (shm_appl->as_info[shm_as_index].glo_write_size < 0)
		shm_appl->as_info[shm_as_index].glo_write_size = 0;
	      shm_appl->as_info[shm_as_index].glo_flag = 0;
#endif
	      shm_appl->as_info[shm_as_index].last_access_time = time (NULL);
	      if (err_code < 0)
		break;
	    }

	  if (shm_appl->as_info[shm_as_index].cur_statement_pooling)
	    hm_srv_handle_free_all ();

	  if (xa_prepare_flag)
	    {
	      UX_DATABASE_SHUTDOWN (req_info.isql);
	      UX_DATABASE_CONNECT (db_name, db_user, db_passwd, NULL,
				   req_info.isql);
	    }
	  else
	    {
	      ux_end_tran (CCI_TRAN_ROLLBACK, FALSE);
	    }

	  if (shm_appl->access_log == ON)
	    cas_access_log (&tran_start_time, shm_as_index, client_ip_addr);

	}

      CLOSE_SOCKET (client_sock_fd);

    error1:
#ifdef WIN32
      shm_appl->as_info[shm_as_index].close_flag = 1;
#endif

      cas_log_write (0, TRUE, "disconnect");

      cas_log_end (&tran_start_time, broker_name, shm_as_index,
		   shm_appl->sql_log_time, sql_log2_get_filename (),
		   shm_appl->sql_log_max_size, TRUE);
      sql_log2_end (TRUE);

      if (!(shm_appl->as_info[shm_as_index].cur_keep_con == KEEP_CON_AUTO &&
	    shm_appl->as_info[shm_as_index].con_status ==
	    CON_STATUS_CLOSE_AND_CONNECT))
	{
	  if (restart_is_needed ())
	    shm_appl->as_info[shm_as_index].uts_status = UTS_STATUS_RESTART;
	  else
	    shm_appl->as_info[shm_as_index].uts_status = UTS_STATUS_IDLE;
	}
      cas_req_count++;
    }
#if defined(WINDOWS)
  }__except(CreateMiniDump(GetExceptionInformation())){}
#endif	

  return 0;
}
#else
int
libcas_main (int jsp_sock_fd)
{
  T_NET_BUF net_buf;
  int client_sock_fd;
  char broker_info[BROKER_INFO_SIZE];
  int err_code;

  memset (&req_info, 0, sizeof (req_info));
  memset (broker_info, 0, sizeof (broker_info));

  req_info.client_version = CAS_CUR_VERSION;
  BROKER_INFO_DBMS_TYPE (broker_info) = CCI_DBMS_CUBRID;
  client_sock_fd = jsp_sock_fd;
/*  net_write_int(client_sock_fd, 1);

  req_info.client_version = CAS_CUR_VERSION;
  if (net_read_stream(client_sock_fd, read_buf, SRV_CON_DB_INFO_SIZE) < 0) {
    NET_WRITE_ERROR_CODE(client_sock_fd, CAS_ER_COMMUNICATION);
  }
  else {
    BROKER_INFO_KEEP_CONNECTION(broker_info) = CAS_KEEP_CONNECTION_OFF;
    {
      char msgbuf[4 + BROKER_INFO_SIZE];
      int tmpint = htonl(getpid());

      BROKER_INFO_KEEP_CONNECTION(broker_info) = CAS_KEEP_CONNECTION_ON;
      net_write_int(client_sock_fd, 4 + BROKER_INFO_SIZE);
      memcpy(msgbuf, &tmpint, 4);
      memcpy(msgbuf+4, broker_info, BROKER_INFO_SIZE);
      net_write_stream(client_sock_fd, msgbuf, 4 + BROKER_INFO_SIZE);
    }
*/
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
/*  }*/

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
#endif

#ifndef LIBCAS_FOR_JSP
static void
cleanup (int signo)
{
#ifdef MEM_DEBUG
  int fd;
#endif

  signal (signo, SIG_IGN);

  UX_DATABASE_SHUTDOWN (req_info.isql);

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
  signal (signo, SIG_IGN);
  db_set_interrupt (1);
  cas_log_write (0, TRUE, "query_cancel");
}
#endif /* end of ifndef LIBCAS_FOR_JSP */

static int
process_request (int clt_sock_fd, T_NET_BUF * net_buf, T_REQ_INFO * req_info)
{
  int msg_size;
  char *read_msg;
  char func_code;
  int argc;
  void **argv = NULL;
  int err_code;
  T_SERVER_FUNC server_fn;

#ifndef LIBCAS_FOR_JSP
  if (shm_appl->as_info[shm_as_index].cur_keep_con == KEEP_CON_AUTO)
    {
      err_code = net_read_int_keep_con_auto (clt_sock_fd, &msg_size);
    }
  else
    {
      net_timeout_set (shm_appl->session_timeout);
      err_code = net_read_int (clt_sock_fd, &msg_size);
    }
#else
  net_timeout_set (60);
  err_code = net_read_int (clt_sock_fd, &msg_size);
#endif

  if (err_code < 0)
    {
      char *cas_log_msg = NULL;
      NET_WRITE_ERROR_CODE (clt_sock_fd, CAS_ER_COMMUNICATION);
#ifndef LIBCAS_FOR_JSP
      if (shm_appl->as_info[shm_as_index].con_status ==
	  CON_STATUS_CLOSE_AND_CONNECT)
	{
	  cas_log_msg = "CHANGE_CLIENT";
	}
#endif
      if (cas_log_msg == NULL)
	{
	  if (net_timeout_flag)
	    {
	      cas_log_msg = "SESSION_TIMEOUT";
	    }
	  else
	    {
	      cas_log_msg = "CONN_ERR net_read_int()";
	    }
	}
      cas_log_write (0, TRUE, cas_log_msg);
      return -1;
    }

#ifndef LIBCAS_FOR_JSP
  if (shm_appl->session_timeout < 0)
    net_timeout_set (NET_DEFAULT_TIMEOUT);
  else
    net_timeout_set (MIN (shm_appl->session_timeout, NET_DEFAULT_TIMEOUT));
#else
  net_timeout_set (NET_DEFAULT_TIMEOUT);
#endif

  read_msg = (char *) MALLOC (msg_size);
  if (read_msg == NULL)
    {
      NET_WRITE_ERROR_CODE (clt_sock_fd, CAS_ER_NO_MORE_MEMORY);
      return -1;
    }
  if (net_read_stream (clt_sock_fd, read_msg, msg_size) < 0)
    {
      NET_WRITE_ERROR_CODE (clt_sock_fd, CAS_ER_COMMUNICATION);
      cas_log_write (0, TRUE, "CONN_ERR net_read_stream()");
      return -1;
    }

  argc = net_decode_str (read_msg, msg_size, &func_code, &argv);
  if (argc < 0)
    {
      FREE_MEM (read_msg);
      NET_WRITE_ERROR_CODE (clt_sock_fd, CAS_ER_COMMUNICATION);
      return argc;
    }

  if (func_code <= 0 || func_code >= CAS_FC_MAX)
    {
      NET_WRITE_ERROR_CODE (clt_sock_fd, CAS_ER_COMMUNICATION);
      return CAS_ER_COMMUNICATION;
    }

#ifndef LIBCAS_FOR_JSP
  strcpy (shm_appl->as_info[shm_as_index].log_msg,
	  server_func_name[func_code - 1]);

#ifdef WIN32
  if (!(func_code == CAS_FC_GLO_NEW || func_code == CAS_FC_GLO_SAVE))
    {
      shm_appl->as_info[shm_as_index].glo_read_size = 0;
    }
#endif
#endif

  server_fn = server_fn_table[func_code - 1];

#ifndef LIBCAS_FOR_JSP
  need_auto_commit = FALSE;
#endif
  cas_send_result_flag = TRUE;

  err_code = (*server_fn) (clt_sock_fd, argc, argv, net_buf, req_info);
#ifndef LIBCAS_FOR_JSP
  if (err_code == 0 && net_buf->err_code == 0 && need_auto_commit == TRUE)
    {
      /* no error and auto commit is needed */

      err_code = fn_auto_commit (clt_sock_fd, 0, NULL, net_buf, req_info);

#ifdef DIAG_DEVEL
      shm_appl->as_info[shm_as_index].num_transactions_processed %=
	MAX_DIAG_DATA_VALUE;
      shm_appl->as_info[shm_as_index].num_transactions_processed++;
#endif
    }
#endif

#ifndef LIBCAS_FOR_JSP
#ifdef DIAG_DEVEL
  if ((func_code == CAS_FC_EXECUTE) || (func_code == CAS_FC_SCHEMA_INFO))
    {
      shm_appl->as_info[shm_as_index].num_requests_received %=
	MAX_DIAG_DATA_VALUE;
      shm_appl->as_info[shm_as_index].num_requests_received++;
    }
  else if (func_code == CAS_FC_END_TRAN)
    {
      shm_appl->as_info[shm_as_index].num_transactions_processed %=
	MAX_DIAG_DATA_VALUE;
      shm_appl->as_info[shm_as_index].num_transactions_processed++;
    }
#endif
#endif

#ifndef LIBCAS_FOR_JSP
#ifdef WIN32
  shm_appl->as_info[shm_as_index].glo_read_size = 0;
#endif

  shm_appl->as_info[shm_as_index].log_msg[0] = '\0';
#endif

  if (net_buf->err_code)
    {
      int err_code;
      NET_WRITE_ERROR_CODE (clt_sock_fd, net_buf->err_code);
      err_code = net_buf->err_code;
      net_buf_clear (net_buf);
      return err_code;
    }

#ifndef LIBCAS_FOR_JSP
#ifdef WIN32
  if (net_buf->post_send_file == NULL)
    shm_appl->as_info[shm_as_index].glo_write_size = 0;
  else
    shm_appl->as_info[shm_as_index].glo_write_size = net_buf->post_file_size;
#endif
#endif
  if (cas_send_result_flag && net_buf->data != NULL)
    {
      int data_size;
      data_size = htonl (net_buf->data_size);
      memcpy (net_buf->data, &data_size, NET_BUF_HEADER_SIZE);
      if (net_write_stream
	  (clt_sock_fd, net_buf->data, NET_BUF_CURR_SIZE (net_buf)) < 0)
	{
	  cas_log_write (0, TRUE, "CONN_ERR net_write_stream()");
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
	  net_buf_clear (net_buf);
	  return err_code;
	}
    }
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
  char buf[PATH_MAX];

  if (as_get_my_as_info (broker_name, &shm_as_index) < 0)
    return -1;

  if ((tmp_p = getenv (APPL_SERVER_SHM_KEY_STR)) == NULL)
    return -1;

  shm_appl =
    (T_SHM_APPL_SERVER *) uw_shm_open (atoi (tmp_p), SHM_APPL_SERVER,
				       SHM_MODE_ADMIN);
  if (shm_appl == NULL)
    return -1;

  sprintf (buf, "%s/%s", shm_appl->log_dir, CUBRID_SQL_LOG_DIR);
  set_cubrid_file (FID_SQL_LOG_DIR, buf);
  set_cubrid_file (FID_CUBRID_ERR_DIR, shm_appl->log_dir);

  as_pid_file_create (broker_name, shm_as_index);
  as_db_err_log_set (broker_name, shm_as_index);

  return 0;
}
#endif


#ifndef LIBCAS_FOR_JSP
static int
net_read_int_keep_con_auto (int clt_sock_fd, int *msg_size)
{
  int ret_value = 0;

  if (shm_appl->as_info[shm_as_index].con_status == CON_STATUS_IN_TRAN)
    net_timeout_set (shm_appl->session_timeout);
  else
    {
      net_timeout_set (1);
      new_req_sock_fd = srv_sock_fd;
    }

  do
    {
      if (shm_appl->as_info[shm_as_index].cas_log_reset)
	{
	  cas_log_reset (broker_name, shm_as_index);
	}

      if (shm_appl->as_info[shm_as_index].con_status == CON_STATUS_CLOSE ||
	  shm_appl->as_info[shm_as_index].con_status ==
	  CON_STATUS_CLOSE_AND_CONNECT)
	{
	  break;
	}

      if (net_read_int (clt_sock_fd, msg_size) < 0)
	{
	  if (shm_appl->as_info[shm_as_index].con_status == CON_STATUS_IN_TRAN
	      || !net_timeout_flag)
	    {
	      ret_value = -1;
	      break;
	    }
	  if (shm_appl->as_info[shm_as_index].con_status ==
	      CON_STATUS_OUT_TRAN && net_timeout_flag && restart_is_needed ())
	    {
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

  new_req_sock_fd = -1;

  CON_STATUS_LOCK (&(shm_appl->as_info[shm_as_index]), CON_STATUS_LOCK_CAS);

  if (shm_appl->as_info[shm_as_index].con_status == CON_STATUS_OUT_TRAN)
    {
      shm_appl->as_info[shm_as_index].num_request++;
      TIMEVAL_MAKE (&tran_start_time);
    }

  if (shm_appl->as_info[shm_as_index].con_status == CON_STATUS_CLOSE ||
      shm_appl->as_info[shm_as_index].con_status ==
      CON_STATUS_CLOSE_AND_CONNECT)
    {
      ret_value = -1;
    }
  else
    shm_appl->as_info[shm_as_index].con_status = CON_STATUS_IN_TRAN;

  CON_STATUS_UNLOCK (&(shm_appl->as_info[shm_as_index]), CON_STATUS_LOCK_CAS);

  return ret_value;
}
#endif

#ifndef LIBCAS_FOR_JSP
int
restart_is_needed (void)
{
#ifdef WIN32
  if (shm_appl->use_pdh_flag == TRUE)
    {
      if ((shm_appl->as_info[shm_as_index].pid ==
	   shm_appl->as_info[shm_as_index].pdh_pid)
	  && (shm_appl->as_info[shm_as_index].pdh_workset >
	      shm_appl->appl_server_max_size))
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
      if (cas_req_count % 100 == 0)
	return 1;
      else
	return 0;
    }
#else
  if (shm_appl->as_info[shm_as_index].psize > shm_appl->appl_server_max_size)
    return 1;
  else
    return 0;
#endif
}

#if defined(WINDOWS)

LONG WINAPI CreateMiniDump(struct _EXCEPTION_POINTERS *pException)
{
	STARTUPINFO si;
	PROCESS_INFORMATION pi;
	BOOL fSuccess;
	char cmd_line[PATH_MAX];
    TCHAR        DumpPath[MAX_PATH] = {0,};
    SYSTEMTIME    SystemTime;
	HANDLE FileHandle ;
	char * cubid_env;

    GetLocalTime(&SystemTime);

    sprintf(DumpPath, "%s\\%s\\%d-%d-%d %d_%d_%d.dmp", 
		get_cubrid_home(),
		EXECUTABLE_BIN_DIR,
        SystemTime.wYear,
        SystemTime.wMonth,
        SystemTime.wDay,
        SystemTime.wHour,
        SystemTime.wMinute,
        SystemTime.wSecond);
    
    FileHandle = CreateFile(
        DumpPath, 
        GENERIC_WRITE, 
        FILE_SHARE_WRITE, 
        NULL, CREATE_ALWAYS, 
        FILE_ATTRIBUTE_NORMAL, 
        NULL);

    if (FileHandle != INVALID_HANDLE_VALUE)
    {
        MINIDUMP_EXCEPTION_INFORMATION MiniDumpExceptionInfo;
		BOOL Success ;
        
        MiniDumpExceptionInfo.ThreadId            = GetCurrentThreadId();
        MiniDumpExceptionInfo.ExceptionPointers    = pException;
        MiniDumpExceptionInfo.ClientPointers    = FALSE;

        Success = MiniDumpWriteDump(
            GetCurrentProcess(), 
            GetCurrentProcessId(), 
            FileHandle, 
            MiniDumpNormal, 
			(pException) ? &MiniDumpExceptionInfo : NULL, 
            NULL, 
            NULL);
    }

    CloseHandle(FileHandle);

	UX_DATABASE_SHUTDOWN (req_info.isql);

	return EXCEPTION_EXECUTE_HANDLER;
}
#endif
#endif
