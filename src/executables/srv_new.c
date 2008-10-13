/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * server.c - server interface
 *
 * Note:
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#if !defined(WINDOWS)
#include <signal.h>
#include <unistd.h>
#if defined(SOLARIS)
#include <sys/filio.h>
#endif /* SOLARIS */
#include <sys/socket.h>
#include <fcntl.h>
#include <netinet/in.h>
#endif /* !WINDOWS */

#include "thread.h"
#include "memory_manager_2.h"
#include "boot_sr.h"
#include "defs.h"
#include "globals.h"
#include "oob.h"
#include "release_string.h"
#include "system_parameter.h"
#include "environment_variable.h"
#include "porting.h"
#include "error_manager.h"
#include "jobqueue.h"
#include "thread_impl.h"
#include "csserror.h"
#include "message_catalog.h"
#include "critical_section.h"
#include "lock.h"
#include "log.h"
#include "network.h"
#include "jsp_earth.h"
#if defined(WINDOWS)
#include "wintcp.h"
#else /* WINDOWS */
#include "tcp.h"
#endif /* WINDOWS */
#include "conn.h"
#include "srv_new.h"

#define CSS_WAIT_COUNT 5	/* # of retry to connect to master */
#define CSS_NUM_JOB_QUEUE 10	/* # of job queues */
#define CSS_GOING_DOWN_IMMEDIATELY "Server going down immediately"

#if defined(WINDOWS)
#define SockError    SOCKET_ERROR
#else /* WINDOWS */
#define SockError    -1
#endif /* WINDOWS */

static struct timeval *css_Timeout = NULL;
static char *css_Master_server_name = NULL;	/* database identifier */
static int css_Master_port_id;
static int css_Server_timeout_value_in_seconds = 3;
static int css_Server_timeout_value_in_microseconds = 0;
static CSS_CONN_ENTRY *css_Master_conn;
#if defined(WINDOWS)
static int css_Win_kill_signaled = 0;
#endif /* WINDOWS */
/* internal request hander function */
static int (*css_Server_request_handler) (THREAD_ENTRY *, unsigned int,
					  int, int, char *);

typedef struct job_queue JOB_QUEUE;
struct job_queue
{
  MUTEX_T job_lock;
  CSS_LIST job_list;
  THREAD_ENTRY *worker_thrd_list;
  MUTEX_T free_lock;
  CSS_JOB_ENTRY *free_list;
};

static JOB_QUEUE css_Job_queue[CSS_NUM_JOB_QUEUE] = {
  {MUTEX_INITIALIZER, {NULL, NULL, 0, NULL, 0},
   NULL, MUTEX_INITIALIZER, NULL},
  {MUTEX_INITIALIZER, {NULL, NULL, 0, NULL, 0},
   NULL, MUTEX_INITIALIZER, NULL},
  {MUTEX_INITIALIZER, {NULL, NULL, 0, NULL, 0},
   NULL, MUTEX_INITIALIZER, NULL},
  {MUTEX_INITIALIZER, {NULL, NULL, 0, NULL, 0},
   NULL, MUTEX_INITIALIZER, NULL},
  {MUTEX_INITIALIZER, {NULL, NULL, 0, NULL, 0},
   NULL, MUTEX_INITIALIZER, NULL},
  {MUTEX_INITIALIZER, {NULL, NULL, 0, NULL, 0},
   NULL, MUTEX_INITIALIZER, NULL},
  {MUTEX_INITIALIZER, {NULL, NULL, 0, NULL, 0},
   NULL, MUTEX_INITIALIZER, NULL},
  {MUTEX_INITIALIZER, {NULL, NULL, 0, NULL, 0},
   NULL, MUTEX_INITIALIZER, NULL},
  {MUTEX_INITIALIZER, {NULL, NULL, 0, NULL, 0},
   NULL, MUTEX_INITIALIZER, NULL},
  {MUTEX_INITIALIZER, {NULL, NULL, 0, NULL, 0},
   NULL, MUTEX_INITIALIZER, NULL}
};

static int css_free_job_entry_func (void *data, void *dummy);
static void css_empty_job_queue (void);
static int css_setup_server_loop (void);
static int css_check_conn (CSS_CONN_ENTRY * p);
static int css_get_master_request (int master_fd);
static int css_process_master_request (int master_fd,
				       fd_set * read_fd_var,
				       fd_set * exception_fd_var);
static int css_process_shutdown_immediate (void);
static void css_process_stop_shutdown (void);
static void css_process_shutdown_request (int master_fd);
static int css_send_oob_msg_to_client (CSS_CONN_ENTRY * conn,
				       char data, char *buffer, int len);
static int css_broadcast_oob_msg (char data, char *buffer, int len);
static void css_process_new_client (int master_fd,
				    fd_set * read_fd_var,
				    fd_set * exception_fd_var);
static void css_close_connection_to_master (void);
static int css_process_timeout (void);
static int css_reestablish_connection_to_master (void);
static void dummy_sigurg_handler (int sig);
static int css_server_thread (THREAD_ENTRY * thrd, CSS_THREAD_ARG arg);
static int css_internal_connection_handler (CSS_CONN_ENTRY * conn);
static int css_internal_request_handler (THREAD_ENTRY * thrd,
					 CSS_THREAD_ARG arg);
static void css_test_for_client_errors (CSS_CONN_ENTRY * conn,
					unsigned int eid);
static int css_wait_worker_thread_on_jobq (THREAD_ENTRY * thrd,
					   int jobq_index);
static int css_wakeup_worker_thread_on_jobq (int jobq_index);

#if defined(WINDOWS)
static int css_process_new_connection_request (void);
static BOOL WINAPI ctrl_sig_handler (DWORD ctrl_event);
#endif /* WINDOWS */

/*
 * css_make_job_entry () -
 *   return:
 *   conn(in):
 *   func(in):
 *   arg(in):
 *   index(in):
 */
CSS_JOB_ENTRY *
css_make_job_entry (CSS_CONN_ENTRY * conn, CSS_THREAD_FN func,
		    CSS_THREAD_ARG arg, int index)
{
  CSS_JOB_ENTRY *job_entry_p;
  int jobq_index;
  int rv;

  if (index >= 0)
    {
      /* explicit index */
      jobq_index = index % CSS_NUM_JOB_QUEUE;
    }
  else
    {
      if (conn == NULL)
	{
	  THREAD_ENTRY *thrd = thread_get_thread_entry_info ();
	  jobq_index = thrd->index % CSS_NUM_JOB_QUEUE;
	}
      else
	{
	  jobq_index = conn->idx % CSS_NUM_JOB_QUEUE;
	}
    }

  if (css_Job_queue[jobq_index].free_list == NULL)
    {
      job_entry_p = (CSS_JOB_ENTRY *) malloc (sizeof (CSS_JOB_ENTRY));
      job_entry_p->jobq_index = jobq_index;
    }
  else
    {
      MUTEX_LOCK (rv, css_Job_queue[jobq_index].free_lock);
      if (css_Job_queue[jobq_index].free_list == NULL)
	{
	  MUTEX_UNLOCK (css_Job_queue[jobq_index].free_lock);
	  job_entry_p = (CSS_JOB_ENTRY *) malloc (sizeof (CSS_JOB_ENTRY));
	  job_entry_p->jobq_index = jobq_index;
	}
      else
	{
	  job_entry_p = css_Job_queue[jobq_index].free_list;
	  css_Job_queue[jobq_index].free_list = job_entry_p->next;
	  MUTEX_UNLOCK (css_Job_queue[jobq_index].free_lock);
	}
    }

  if (job_entry_p != NULL)
    {
      job_entry_p->conn_entry = conn;
      job_entry_p->func = func;
      job_entry_p->arg = arg;
    }

  return job_entry_p;
}

/*
 * css_free_job_entry () -
 *   return:
 *   p(in):
 */
void
css_free_job_entry (CSS_JOB_ENTRY * job_entry_p)
{
  int rv;

  if (job_entry_p != NULL)
    {
      MUTEX_LOCK (rv, css_Job_queue[job_entry_p->jobq_index].free_lock);

      job_entry_p->next = css_Job_queue[job_entry_p->jobq_index].free_list;
      css_Job_queue[job_entry_p->jobq_index].free_list = job_entry_p;

      MUTEX_UNLOCK (css_Job_queue[job_entry_p->jobq_index].free_lock);
    }
}

/*
 * css_init_job_queue () -
 *   return:
 */
void
css_init_job_queue (void)
{
  int i, j;
#if defined(WINDOWS)
  int r;
#endif /* WINDOWS */
  CSS_JOB_ENTRY *job_entry_p;

  for (i = 0; i < CSS_NUM_JOB_QUEUE; i++)
    {
#if defined(WINDOWS)
      r = MUTEX_INIT (css_Job_queue[i].job_lock);
      CSS_CHECK_RETURN (r, ER_CSS_PTHREAD_MUTEX_INIT);
      r = MUTEX_INIT (css_Job_queue[i].free_lock);
      CSS_CHECK_RETURN (r, ER_CSS_PTHREAD_MUTEX_INIT);
#endif /* WINDOWS */

      css_initialize_list (&css_Job_queue[i].job_list, PRM_CSS_MAX_CLIENTS);
      css_Job_queue[i].free_list = NULL;
      for (j = 0; j < PRM_CSS_MAX_CLIENTS; j++)
	{
	  job_entry_p = (CSS_JOB_ENTRY *) malloc (sizeof (CSS_JOB_ENTRY));
	  if (job_entry_p == NULL)
	    break;
	  job_entry_p->jobq_index = i;
	  job_entry_p->next = css_Job_queue[i].free_list;
	  css_Job_queue[i].free_list = job_entry_p;
	}
    }
}

/*
 * css_broadcast_shutdown_thread () -
 *   return:
 */
void
css_broadcast_shutdown_thread (void)
{
  int rv;
  int i;
  THREAD_ENTRY *thrd = NULL;

  /* Every idle worker thread are blocked on the job queue. */
  for (i = 0; i < CSS_NUM_JOB_QUEUE; i++)
    {
      MUTEX_LOCK (rv, css_Job_queue[i].job_lock);

      thrd = css_Job_queue[i].worker_thrd_list;
      while (thrd)
	{
	  thread_wakeup (thrd);
	  thrd = thrd->worker_thrd_list;
	}

      MUTEX_UNLOCK (css_Job_queue[i].job_lock);
    }
}

/*
 * css_add_to_job_queue () -
 *   return:
 *   job_entry_p(in):
 */
void
css_add_to_job_queue (CSS_JOB_ENTRY * job_entry_p)
{
  int rv;

  MUTEX_LOCK (rv, css_Job_queue[job_entry_p->jobq_index].job_lock);

  css_add_list (&css_Job_queue[job_entry_p->jobq_index].job_list,
		job_entry_p);
  css_wakeup_worker_thread_on_jobq (job_entry_p->jobq_index);

  MUTEX_UNLOCK (css_Job_queue[job_entry_p->jobq_index].job_lock);
}

/*
 * css_get_new_job() - fetch a job from the queue
 *   return:
 */
CSS_JOB_ENTRY *
css_get_new_job (void)
{
  CSS_JOB_ENTRY *job_entry_p;
  int r;
  THREAD_ENTRY *thrd = thread_get_thread_entry_info ();
  int jobq_index = thrd->index % CSS_NUM_JOB_QUEUE;

  MUTEX_LOCK (r, css_Job_queue[jobq_index].job_lock);

  if (css_Job_queue[jobq_index].job_list.count == 0)
    {
      css_wait_worker_thread_on_jobq (thrd, jobq_index);
    }
  job_entry_p = (CSS_JOB_ENTRY *)
    css_remove_list_from_head (&css_Job_queue[jobq_index].job_list);

  MUTEX_UNLOCK (css_Job_queue[jobq_index].job_lock);

  MUTEX_LOCK (r, thrd->tran_index_lock);

  /* if job_entry_p == NULL, system will be shutdown soon. */
  return job_entry_p;
}

/*
 * css_free_job_entry_func () -
 *   return:
 *   data(in):
 *   user(in):
 */
static int
css_free_job_entry_func (void *data, void *dummy)
{
  CSS_JOB_ENTRY *job_entry_p = (CSS_JOB_ENTRY *) data;
  int rv;

  MUTEX_LOCK (rv, css_Job_queue[job_entry_p->jobq_index].free_lock);

  job_entry_p->next = css_Job_queue[job_entry_p->jobq_index].free_list;
  css_Job_queue[job_entry_p->jobq_index].free_list = job_entry_p;

  MUTEX_UNLOCK (css_Job_queue[job_entry_p->jobq_index].free_lock);

  return TRAV_CONT_DELETE;
}

/*
 * css_empty_job_queue() - delete all job from the job queue
 *   return:
 */
static void
css_empty_job_queue (void)
{
  int rv;
  int i;

  for (i = 0; i < CSS_NUM_JOB_QUEUE; i++)
    {
      MUTEX_LOCK (rv, css_Job_queue[i].job_lock);

      css_traverse_list (&css_Job_queue[i].job_list, css_free_job_entry_func,
			 NULL);

      MUTEX_UNLOCK (css_Job_queue[i].job_lock);
    }
}

/*
 * css_final_job_queue() -
 *   return:
 */
void
css_final_job_queue (void)
{
  int rv;
  CSS_JOB_ENTRY *p;
  int i;

  for (i = 0; i < CSS_NUM_JOB_QUEUE; i++)
    {
      MUTEX_LOCK (rv, css_Job_queue[i].job_lock);

      css_traverse_list (&css_Job_queue[i].job_list, css_free_job_entry_func,
			 NULL);
      css_finalize_list (&css_Job_queue[i].job_list);
      while (css_Job_queue[i].free_list != NULL)
	{
	  p = css_Job_queue[i].free_list;
	  css_Job_queue[i].free_list = p->next;
	  free_and_init (p);
	}

      MUTEX_UNLOCK (css_Job_queue[i].job_lock);
    }
}

/*
 * css_setup_server_loop() -
 *   return:
 */
static int
css_setup_server_loop (void)
{
#if !defined(WINDOWS)
  (void) os_set_signal_handler (SIGPIPE, SIG_IGN);
#endif /* not WINDOWS */

#if defined(LINUX) || defined(x86_SOLARIS) || defined(HPUX)
  if (!jsp_jvm_is_loaded ())
    {
      (void) os_set_signal_handler (SIGFPE, SIG_IGN);
    }
#else /* LINUX || x86_SOLARIS || HPUX */
  (void) os_set_signal_handler (SIGFPE, SIG_IGN);
#endif /* LINUX || x86_SOLARIS || HPUX */

  if (css_Pipe_to_master >= 0)
    {
      /* startup worker/daemon threads */
      if (thread_start_workers () != NO_ERROR)
	{
	  return 0;		/* thread creation error */
	}

      /* execute master thread. */
      css_master_thread ();

      /* stop threads */
      thread_stop_active_workers ();
      thread_stop_active_daemons ();

      css_close_server_connection_socket ();

#if defined(WINDOWS)
      /* since this will exit, we have to make sure and shut down Winsock */
      css_windows_shutdown ();
#endif /* WINDOWS */
    }
  else
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ERR_CSS_MASTER_PIPE_ERROR, 0);
    }

  return 1;
}

/*
 * css_check_conn() -
 *   return:
 *   p(in):
 */
static int
css_check_conn (CSS_CONN_ENTRY * p)
{
#if defined(WINDOWS)
  u_long status = 0;
#else
  int status = 0;
#endif

#if defined(WINDOWS)
  if (ioctlsocket (p->fd, FIONREAD, &status) == SockError
      || p->error_p || p->status != CONN_OPEN)
    {
      return -1;
    }
#else /* WINDOWS */
  if (fcntl (p->fd, F_GETFL, status) < 0
      || p->error_p || p->status != CONN_OPEN)
    {
      return -1;
    }
#endif /* WINDOWS */

  return 0;
}

/*
 * css_master_thread() - Master thread, accept/process master process's request
 *   return:
 *   arg(in):
 */
#if defined(WINDOWS)
unsigned __stdcall
css_master_thread (void)
#else /* WINDOWS */
void *
css_master_thread (void)
#endif				/* WINDOWS */
{
  fd_set read_fdset, exception_fdset;
  struct timeval timeout;
  int r, run_code = 1, status = 0;

  timeout.tv_sec = css_Server_timeout_value_in_seconds;
  timeout.tv_usec = css_Server_timeout_value_in_microseconds;

  while (run_code)
    {
      /* check if socket has error or client is down */
      if (css_Pipe_to_master >= 0 && css_check_conn (css_Master_conn) < 0)
	{
	  css_shutdown_conn (css_Master_conn);
	  css_Pipe_to_master = -1;
	}

      FD_ZERO (&read_fdset);
      FD_ZERO (&exception_fdset);
      if (css_Pipe_to_master >= 0)
	{
	  FD_SET (css_Pipe_to_master, &read_fdset);
	  FD_SET (css_Pipe_to_master, &exception_fdset);
	}
#if defined(WINDOWS)
      if (css_Server_connection_socket >= 0)
	{
	  FD_SET (css_Server_connection_socket, &read_fdset);
	  FD_SET (css_Server_connection_socket, &exception_fdset);
	}
#endif /* WINDOWS */

      /* select() sets timeout value to 0 or waited time */
      timeout.tv_sec = css_Server_timeout_value_in_seconds;
      timeout.tv_usec = css_Server_timeout_value_in_microseconds;

      r = select (FD_SETSIZE, &read_fdset, NULL, &exception_fdset, &timeout);
      if (r > 0
	  && (css_Pipe_to_master < 0
	      || !FD_ISSET (css_Pipe_to_master, &read_fdset))
#if defined(WINDOWS)
	  && (css_Server_connection_socket < 0
	      || !FD_ISSET (css_Server_connection_socket, &read_fdset))
#endif /* WINDOWS */
	)
	{
	  continue;
	}

      if (r < 0)
	{
	  if (css_Pipe_to_master >= 0 &&
#if defined(WINDOWS)
	      ioctlsocket (css_Pipe_to_master, FIONREAD,
			   (u_long *) & status) == SockError
#else /* WINDOWS */
	      fcntl (css_Pipe_to_master, F_GETFL, status) == SockError
#endif /* WINDOWS */
	    )
	    {
	      css_close_connection_to_master ();
	      break;
	    }
	}
      else if (r > 0)
	{
	  /*
	   * process master request;
	   * type and handler of a request from the master
	   *  SERVER_START_NEW_CLIENT   : css_process_new_client()
	   *  SERVER_START_SHUTDOWN     : css_process_shutdown_request()
	   *  SERVER_STOP_SHUTDOWN      : css_process_stop_shutdown()
	   *  SERVER_SHUTDOWN_IMMEDIATE : css_process_shutdown_immediate()
	   *
	   *  SERVER_START_TRACING, SERVER_STOP_TRACING, SERVER_HALT_EXECUTION,
	   *  SERVER_RESUME_EXECUTION:
	   */
	  if (css_Pipe_to_master >= 0
	      && FD_ISSET (css_Pipe_to_master, &read_fdset))
	    {
	      run_code = css_process_master_request (css_Pipe_to_master,
						     &read_fdset,
						     &exception_fdset);
	      if (run_code == -1)
		{
		  css_close_connection_to_master ();
		  run_code = 1;	/* shutdown message received */
		}
	    }
#if !defined(WINDOWS)
	  else
	    {
	      break;
	    }

#else /* !WINDOWS */
	  if (css_Server_connection_socket >= 0
	      && FD_ISSET (css_Server_connection_socket, &read_fdset))
	    {
	      css_process_new_connection_request ();
	    }
#endif /* !WINDOWS */
	}

      if (run_code)
	{
	  run_code = css_process_timeout ();
	}
      else
	{
	  break;
	}
    }

  /* going down, so stop dispatching request */
  css_empty_job_queue ();

#if defined(WINDOWS)
  return 0;
#else /* WINDOWS */
  return NULL;
#endif /* WINDOWS */
}

/*
 * css_get_master_request () -
 *   return:
 *   master_fd(in):
 */
static int
css_get_master_request (int master_fd)
{
  int request, r;

  r = css_readn (master_fd, (char *) &request, sizeof (int));
  if (r == sizeof (int))
    {
      return ((int) ntohl (request));
    }
  else
    {
      return (-1);
    }
}

/*
 * css_process_master_request () -
 *   return:
 *   master_fd(in):
 *   read_fd_var(in):
 *   exception_fd_var(in):
 */
static int
css_process_master_request (int master_fd, fd_set * read_fd_var,
			    fd_set * exception_fd_var)
{
  int request, r;

  r = 1;
  request = (int) css_get_master_request (master_fd);

  switch (request)
    {
    case SERVER_START_NEW_CLIENT:
      css_process_new_client (master_fd, read_fd_var, exception_fd_var);
      break;

    case SERVER_START_SHUTDOWN:
      css_process_shutdown_request (master_fd);
      break;

    case SERVER_STOP_SHUTDOWN:
      css_process_stop_shutdown ();
      break;

    case SERVER_SHUTDOWN_IMMEDIATE:
      r = css_process_shutdown_immediate ();
      break;

    case SERVER_START_TRACING:
    case SERVER_STOP_TRACING:
    case SERVER_HALT_EXECUTION:
    case SERVER_RESUME_EXECUTION:
      break;
    default:
      /* master do not respond */
      r = -1;
      break;
    }

  return r;
}

/*
 * css_process_shutdown_immediate() -
 *   return:
 */
static int
css_process_shutdown_immediate (void)
{
  int r = 0;

  r = css_broadcast_oob_msg ((char) OOB_SERVER_DOWN,
			     (char *) CSS_GOING_DOWN_IMMEDIATELY,
			     strlen (CSS_GOING_DOWN_IMMEDIATELY) + 1);
  return r;
}

/*
 * css_process_stop_shutdown() -
 *   return:
 */
static void
css_process_stop_shutdown (void)
{
  char msg[] = "Shutdown cancelled\n";

  if (css_Timeout)
    {
      free_and_init (css_Timeout);
      css_Timeout = NULL;
    }

  css_broadcast_oob_msg (0, msg, strlen (msg));
}

/*
 * css_process_shutdown_request () -
 *   return:
 *   master_fd(in):
 */
static void
css_process_shutdown_request (int master_fd)
{
  char buffer[MASTER_TO_SRV_MSG_SIZE];
  int r, timeout;

  timeout = (int) css_get_master_request (master_fd);

  if (css_Timeout == NULL)
    {
      css_Timeout = (struct timeval *) malloc (sizeof (struct timeval));
    }

  if (css_Timeout == NULL)
    {
      return;
    }

  if (gettimeofday (css_Timeout, NULL) != 0)
    {
      free_and_init (css_Timeout);
      css_Timeout = NULL;
      return;
    }

  css_Timeout->tv_sec += timeout;

  r = css_readn (master_fd, buffer, MASTER_TO_SRV_MSG_SIZE);
  if (r < 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ERR_CSS_SHUTDOWN_ERROR, 0);
      free_and_init (css_Timeout);
      css_Timeout = NULL;
      return;
    }

  css_broadcast_oob_msg (OOB_SERVER_SHUTDOWN, buffer, r);
}

/*
 * css_send_oob_msg_to_client () -
 *   return:
 *   conn(in):
 *   data(in):
 *   buffer(in):
 *   len(in):
 */
static int
css_send_oob_msg_to_client (CSS_CONN_ENTRY * conn, char data, char *buffer,
			    int len)
{
  if (conn->fd > 0 && conn->client_timeout == NULL
      && conn->fd != css_Pipe_to_master)
    {
      if (css_send_oob (conn, data, buffer, len) != NO_ERRORS)
	return 1;
    }

  return 0;
}

/*
 * css_broadcast_oob_msg () -
 *   return:
 *   data(in):
 *   buffer(in):
 *   len(in):
 */
static int
css_broadcast_oob_msg (char data, char *buffer, int len)
{
  CSS_CONN_ENTRY *conn;
  int r = 0;

  if (css_Active_conn_anchor == NULL)
    return 0;

  csect_enter_critical_section (NULL, &css_Active_conn_csect, INF_WAIT);

  for (conn = css_Active_conn_anchor; conn != NULL; conn = conn->next)
    {
      r += css_send_oob_msg_to_client (conn, data, buffer, len);
    }

  csect_exit_critical_section (&css_Active_conn_csect);

  return (r > 0) ? 1 : 0;
}

/*
 * css_process_new_client () -
 *   return:
 *   master_fd(in):
 *   read_fd_var(in/out):
 *   exception_fd_var(in/out):
 */
static void
css_process_new_client (int master_fd, fd_set * read_fd_var,
			fd_set * exception_fd_var)
{
  int new_fd;
  int reason;
  CSS_CONN_ENTRY *conn;
  unsigned short rid;

  /* receive new socket descriptor from the master */
  new_fd = css_open_new_socket_from_master (master_fd, &rid);
  if (new_fd < 0)
    {
      return;
    }

  if (read_fd_var != NULL)
    FD_CLR (new_fd, read_fd_var);
  if (exception_fd_var != NULL)
    FD_CLR (new_fd, exception_fd_var);

  conn = css_make_conn (new_fd);
  if (conn == NULL)
    {
      CSS_CONN_ENTRY new_conn;
      void *error_string;
      int length;

      css_initialize_conn (&new_conn, new_fd);

      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_CSS_CLIENTS_EXCEEDED, 1, PRM_CSS_MAX_CLIENTS);
      reason = htonl (SERVER_CLIENTS_EXCEEDED);
      css_send_data (&new_conn, rid, (char *) &reason, (int) sizeof (int));

      error_string = er_get_area_error (&length);
      new_conn.db_error = ER_CSS_CLIENTS_EXCEEDED;
      css_send_error (&new_conn, rid, (const char *) error_string, length);
      css_shutdown_conn (&new_conn);
      css_dealloc_conn_csect (&new_conn);
      er_clear ();
      free (error_string);
      return;
    }

  reason = htonl (SERVER_CONNECTED);
  css_send_data (conn, rid, (char *) &reason, sizeof (int));

  if (css_Connect_handler)
    {
      (*css_Connect_handler) (conn);
    }
}

/*
 * css_close_connection_to_master() -
 *   return:
 */
static void
css_close_connection_to_master (void)
{
  if (css_Pipe_to_master >= 0)
    {
      css_shutdown_conn (css_Master_conn);
    }
  css_Pipe_to_master = -1;
  css_Master_conn = NULL;
}

/*
 * css_process_timeout() -
 *   return:
 */
static int
css_process_timeout (void)
{
  struct timeval timeout;

  if (css_Timeout)
    {
      /* css_Timeout is set by shutdown request */
      if (gettimeofday (&timeout, NULL) == 0)
	{
	  if (css_Timeout->tv_sec <= timeout.tv_sec)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_CSS_TIMEOUT_DUE_SHUTDOWN, 0);
	      css_process_shutdown_immediate ();
	      free_and_init (css_Timeout);
	      css_Timeout = NULL;
	      return 0;
	    }
	}
    }

  if (css_Pipe_to_master < 0)
    css_reestablish_connection_to_master ();

  return 1;
}

#if defined(WINDOWS)
/*
 * css_process_new_connection_request () -
 *   return:
 *
 * Note: Called when a connect() is detected on the
 *       css_Server_connection_socket indicating the presence of a new client
 *       attempting to connect. Accept the connection and establish a new FD
 *       for this client. Send him back a little blip so he knows things are
 *       ok.
 */
static int
css_process_new_connection_request (void)
{
  int new_fd;
  int reason, buffer_size, rc;
  CSS_CONN_ENTRY *conn;
  unsigned short rid;

  NET_HEADER header = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };

  new_fd = css_server_accept (css_Server_connection_socket);

  if (new_fd >= 0)
    {
      conn = css_make_conn (new_fd);
      if (conn == NULL)
	{
	  /*
	   * all pre-allocated connection entries are being used now.
	   * create a new entry and send error message throuth it.
	   */
	  CSS_CONN_ENTRY new_conn;
	  void *error_string;
	  int length;

	  css_initialize_conn (&new_conn, new_fd);

	  rc = css_read_header (&new_conn, &header);
	  buffer_size = rid = 0;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_CSS_CLIENTS_EXCEEDED, 1, PRM_CSS_MAX_CLIENTS);
	  reason = htonl (SERVER_CLIENTS_EXCEEDED);
	  css_send_data (&new_conn, rid, (char *) &reason,
			 (int) sizeof (int));

	  error_string = er_get_area_error (&length);
	  new_conn.db_error = ER_CSS_CLIENTS_EXCEEDED;
	  css_send_error (&new_conn, rid, (const char *) error_string,
			  length);
	  css_shutdown_conn (&new_conn);
	  css_dealloc_conn_csect (&new_conn);

	  er_clear ();
	  return -1;
	}

      buffer_size = sizeof (NET_HEADER);
      do
	{
	  /* css_receive_request */
	  if (!conn || conn->status != CONN_OPEN)
	    {
	      rc = CONNECTION_CLOSED;
	      break;
	    }

	  rc = css_read_header (conn, &header);
	  if (rc == NO_ERRORS)
	    {
	      rid = ntohl (header.request_id);

	      if (ntohl (header.type) != COMMAND_TYPE)
		{
		  buffer_size = reason = rid = 0;
		  rc = WRONG_PACKET_TYPE;
		}
	      else
		{
		  reason =
		    (int) (unsigned short) ntohs (header.function_code);
		  buffer_size = (int) ntohl (header.buffer_size);
		}
	    }
	}
      while (rc == WRONG_PACKET_TYPE);

      if (rc == NO_ERRORS)
	{
	  if (reason == DATA_REQUEST)
	    {
	      reason = htonl (SERVER_CONNECTED);
	      (void) css_send_data (conn, rid, (char *) &reason,
				    sizeof (int));

	      if (css_Connect_handler)
		{
		  (*css_Connect_handler) (conn);
		}
	    }
	  else
	    {
	      reason = htonl (SERVER_NOT_FOUND);
	      (void) css_send_data (conn, rid, (char *) &reason,
				    sizeof (int));
	      css_free_conn (conn);
	    }
	}
    }

  /* can't let problems accepting client requests terminate the loop */
  return 1;
}
#endif /* WINDOWS */

/*
 * css_reestablish_connection_to_master() -
 *   return:
 */
static int
css_reestablish_connection_to_master (void)
{
  CSS_CONN_ENTRY *conn;
  static int i = CSS_WAIT_COUNT;
  char *packed_server_name;
  int name_length;

  if (i-- > 0)
    return 0;
  i = CSS_WAIT_COUNT;

  packed_server_name = css_pack_server_name (css_Master_server_name,
					     &name_length);
  if (packed_server_name != NULL)
    {
      conn = css_connect_to_master_server (css_Master_port_id,
					   packed_server_name, name_length);
      if (conn != NULL)
	{
	  css_Pipe_to_master = conn->fd;
	  if (css_Master_conn)
	    css_free_conn (css_Master_conn);
	  css_Master_conn = conn;
	  free_and_init (packed_server_name);
	  return 1;
	}
      else
	{
	  free_and_init (packed_server_name);
	}
    }

  css_Pipe_to_master = -1;
  return 0;
}

/*
 * dummy_sigurg_handler () - SIGURG signal handling thread
 *   return:
 *   sig(in):
 */
static void
dummy_sigurg_handler (int sig)
{
}

/*
 * css_server_thread() - Accept/process request from one client
 *   return:
 *   arg(in):
 *
 * Note: One server thread per one client
 */
static int
css_server_thread (THREAD_ENTRY * thread_p, CSS_THREAD_ARG arg)
{
  fd_set read_fdset;
  struct timeval timeout;
  CSS_CONN_ENTRY *conn;
  CSS_JOB_ENTRY *job;
  int r, type, rv;

  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  conn = (CSS_CONN_ENTRY *) arg;

  MUTEX_UNLOCK (thread_p->tran_index_lock);

  thread_p->type = TT_SERVER;	/* server thread */

  /* check if socket has error or client is down */
  while (thread_p->shutdown == false && conn->stop_talk == false)
    {
      if (css_check_conn (conn) < 0)
	{
	  MUTEX_LOCK (rv, thread_p->tran_index_lock);
	  (*css_Connection_error_handler) (thread_p, conn);
	  break;
	}

      FD_ZERO (&read_fdset);
      FD_SET (conn->fd, &read_fdset);

      timeout.tv_sec = css_Server_timeout_value_in_seconds;
      timeout.tv_usec = css_Server_timeout_value_in_microseconds;

      r = select (FD_SETSIZE, &read_fdset, NULL, NULL, &timeout);
      if (r > 0 && !FD_ISSET (conn->fd, &read_fdset))
	{
	  continue;
	}

      if (r < 0)
	{
	  if (css_check_conn (conn) < 0)
	    {
	      MUTEX_LOCK (rv, thread_p->tran_index_lock);
	      (*css_Connection_error_handler) (thread_p, conn);
	      break;
	    }
	}
      else if (r > 0)
	{
	  /* read command/data/etc request from socket,
	     and enqueue it to appr. queue */
	  if (css_read_and_queue (conn, &type) != NO_ERRORS)
	    {
	      MUTEX_LOCK (rv, thread_p->tran_index_lock);
	      (*css_Connection_error_handler) (thread_p, conn);
	      break;
	    }
	  else
	    {
	      /* if new command request has arrived,
	         make new job and add it to job queue */
	      if (type == COMMAND_TYPE)
		{
		  job = css_make_job_entry (conn, css_Request_handler,
					    conn, -1);
		  if (job)
		    {
		      css_add_to_job_queue (job);
		    }
		}
	    }
	}
    }

  thread_p->type = TT_WORKER;
  return 0;
}

#if defined(WINDOWS)
/*
 * ctrl_sig_handler () -
 *   return:
 *   ctrl_event(in):
 */
static BOOL WINAPI
ctrl_sig_handler (DWORD ctrl_event)
{
  if (ctrl_event == CTRL_BREAK_EVENT)
    {
      ;
    }
  else
    {
      css_Win_kill_signaled = 1;
    }

  return TRUE;			/* Continue */
}
#endif /* WINDOWS */

/*
 * css_oob_handler_thread() -
 *   return:
 *   arg(in):
 */
#if defined(WINDOWS)
unsigned __stdcall
css_oob_handler_thread (void *arg)
#else /* WINDOWS */
void *
css_oob_handler_thread (void *arg)
#endif				/* WINDOWS */
{
  THREAD_ENTRY *thrd_entry;
  int sig, r;
#if !defined(WINDOWS)
  sigset_t sigurg_mask;
  struct sigaction act;
#endif /* !WINDOWS */

  thrd_entry = (THREAD_ENTRY *) arg;

  /* wait until THREAD_CREATE finish */
  MUTEX_LOCK (r, thrd_entry->th_entry_lock);
  MUTEX_UNLOCK (thrd_entry->th_entry_lock);

  thread_set_thread_entry_info (thrd_entry);
  thrd_entry->status = TS_RUN;

#if !defined(WINDOWS)
  sigemptyset (&sigurg_mask);
  sigaddset (&sigurg_mask, SIGURG);

  memset (&act, 0, sizeof (act));
  act.sa_handler = dummy_sigurg_handler;
  sigaction (SIGURG, &act, NULL);

  pthread_sigmask (SIG_UNBLOCK, &sigurg_mask, NULL);
#else /* !WINDOWS */
  SetConsoleCtrlHandler (ctrl_sig_handler, TRUE);
#endif /* !WINDOWS */

  while (!thrd_entry->shutdown)
    {
#if !defined(WINDOWS)
      r = sigwait (&sigurg_mask, &sig);
#else /* WINDOWS */
      Sleep (1000);
#endif /* WINDOWS */
    }
  thrd_entry->status = TS_DEAD;

#if defined(WINDOWS)
  return 0;
#else /* WINDOWS */
  return NULL;
#endif /* WINDOWS */
}

/*
 * css_block_all_active_conn() - Before shutdown, stop all server thread
 *   return:
 *
 * Note:  All communication will be stopped
 */
void
css_block_all_active_conn (void)
{
  CSS_CONN_ENTRY *conn;

  csect_enter_critical_section (NULL, &css_Active_conn_csect, INF_WAIT);

  for (conn = css_Active_conn_anchor; conn != NULL; conn = conn->next)
    {
      csect_enter_critical_section (NULL, &conn->csect, INF_WAIT);

      css_end_server_request (conn);
      if (conn->fd > 0 && conn->fd != css_Pipe_to_master)
	{
	  conn->stop_talk = true;
	  logtb_set_tran_index_interrupt (NULL, conn->transaction_id, 1);
	  thread_sleep (0, 100);
	}

      csect_exit_critical_section (&conn->csect);
    }

  csect_exit_critical_section (&css_Active_conn_csect);
}

/*
 * css_internal_connection_handler() -
 *   return:
 *   conn(in):
 *
 * Note: This routine is "registered" to be called when a new connection is
 *       requested by the client
 */
static int
css_internal_connection_handler (CSS_CONN_ENTRY * conn)
{
  CSS_JOB_ENTRY *job;

  css_insert_into_active_conn_list (conn);

  job = css_make_job_entry (conn, css_server_thread, conn,
			    -1 /* implicit: DEFAULT */ );
  if (job != NULL)
    {
      css_add_to_job_queue (job);
    }

  return (1);
}

/*
 * css_internal_request_handler() -
 *   return:
 *   arg(in):
 *
 * Note: This routine is "registered" to be called when a new request is
 *       initiated by the client. This is used in drivers/main.c.
 *
 *       To now support multiple concurrent requests from the same client,
 *       check if a request is actually sent on the socket. If data was sent
 *       (not a request), then just return and the scheduler will wake up the
 *       thread that is blocking for data.
 */
static int
css_internal_request_handler (THREAD_ENTRY * thread_p, CSS_THREAD_ARG arg)
{
  CSS_CONN_ENTRY *conn;
  unsigned short rid;
  unsigned int eid;
  int request, rc, size = 0;
  char *buffer = NULL;
  int local_tran_index;
  int status = CSS_UNPLANNED_SHUTDOWN;

  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  local_tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);

  conn = (CSS_CONN_ENTRY *) arg;
  CSS_ASSERT (conn != NULL);

  rc = css_receive_request (conn, &rid, &request, &size);
  if (rc == NO_ERRORS)
    {
      /* 1. change thread's transaction id to this connection's */
      THREAD_SET_TRAN_INDEX (thread_p, conn->transaction_id);

      MUTEX_UNLOCK (thread_p->tran_index_lock);

      if (size)
	{
	  rc = css_receive_data (conn, rid, &buffer, &size);
	  if (rc != NO_ERRORS)
	    {
	      return status;
	    }
	}

      conn->db_error = 0;	/* This will reset the error indicator */

      eid = css_return_eid_from_conn (conn, rid);
      /* 2. change thread's client, rid, tran_index for this request */
      THREAD_SET_INFO (thread_p, conn->client_id, eid, conn->transaction_id);

      /* 3. Call server_request() function */
      status =
	(*css_Server_request_handler) (thread_p, eid, request, size, buffer);

      /* 4. reset thread transaction id(may be NULL_TRAN_INDEX) */
      THREAD_SET_INFO (thread_p, -1, 0, local_tran_index);
    }
  else
    {
      MUTEX_UNLOCK (thread_p->tran_index_lock);

      if (rc == ERROR_WHEN_READING_SIZE || rc == NO_DATA_AVAILABLE)
	{
	  status = CSS_NO_ERRORS;
	}
    }

  return status;
}

/*
 * css_initialize_server_interfaces() - initialize the server interfaces
 *   return:
 *   request_handler(in):
 *   thrd(in):
 *   eid(in):
 *   request(in):
 *   size(in):
 *   buffer(in):
 */
void
css_initialize_server_interfaces (int (*request_handler)
				  (THREAD_ENTRY * thrd, unsigned int eid,
				   int request, int size, char *buffer),
				  CSS_THREAD_FN connection_error_function)
{
  css_Server_request_handler = request_handler;
  css_register_handler_routines (css_internal_connection_handler,
				 css_internal_request_handler,
				 connection_error_function);
}

/*
 * css_init() -
 *   return:
 *   server_name(in):
 *   name_length(in):
 *   port_id(in):
 *
 * Note: This routine is the entry point for the server interface. Once this
 *       routine is called, control will not return to the caller until the
 *       server/scheduler is stopped. Please call
 *       css_initialize_server_interfaces before calling this function.
 */
int
css_init (char *server_name, int name_length, int port_id)
{
  CSS_CONN_ENTRY *conn;
  int status = -1;

  if (port_id <= 0)
    return -1;

#if defined(WINDOWS)
  if (css_windows_startup () < 0)
    {
      printf ("Winsock startup error\n");
      return -1;
    }
#endif /* WINDOWS */

  css_Server_connection_socket = -1;

  conn = css_connect_to_master_server (port_id, server_name, name_length);
  if (conn != NULL)
    {
      /* insert conn into active conn list */
      css_insert_into_active_conn_list (conn);

      css_Master_server_name = strdup (server_name);
      css_Master_port_id = port_id;
      css_Pipe_to_master = conn->fd;
      css_Master_conn = conn;

      if (!css_setup_server_loop ())
	{
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_THREAD_STACK,
		  1, PRM_MAX_THREADS);
	  er_print ();
	}

      status = 0;
    }

  if (css_Master_server_name)
    free_and_init (css_Master_server_name);

  /* If this was opened for the new style connection protocol, make sure
   * it gets closed.
   */
  css_close_server_connection_socket ();
#if defined(WINDOWS)
  css_windows_shutdown ();
#endif /* WINDOWS */

  return status;
}

/*
 * css_shutdown() - Shuts down the communication interface
 *   return:
 *   exit_reason(in):
 *
 * Note: This is the routine to call when the server is going down
 */
void
css_shutdown (int exit_reason)
{
  thread_exit (exit_reason);
}

/*
 * css_set_error_code() - set the error value to return to the client
 *   return:
 *   eid(in): enquiry id                                                    *
 *   error_code(in): error code to return                *
 */
void
css_set_error_code (unsigned int eid, int error_code)
{
  int idx = css_return_entry_id_from_eid (eid);

  css_Conn_array[idx].db_error = error_code;
}

/*
 * css_send_data_to_client() - send a data buffer to the server
 *   return:
 *   eid(in): enquiry id
 *   buffer(in): data buffer to queue for expected data.
 *   buffer_size(in): size of data buffer
 *
 * Note: This is to be used ONLY by the server to return data to the client
 */
unsigned int
css_send_data_to_client (unsigned int eid, char *buffer, int buffer_size)
{
  CSS_CONN_ENTRY *conn;
  int rc = 0;
  int idx = css_return_entry_id_from_eid (eid);

  conn = &css_Conn_array[idx];
  rc = css_send_data (conn, css_return_rid_from_eid (eid),
		      buffer, buffer_size);
  return (rc == NO_ERRORS) ? 0 : rc;
}

/*
 * css_send_reply_and_data_to_client() - send a reply to the server,
 *                                       and optionaly, an additional data
 *  buffer
 *   return:
 *   eid(in): enquiry id
 *   reply(in): the reply data (error or no error)
 *   reply_size(in): the size of the reply data.
 *   buffer(in): data buffer to queue for expected data.
 *   buffer_size(in): size of data buffer
 *
 * Note: This is to be used only by the server
 */
unsigned int
css_send_reply_and_data_to_client (unsigned int eid, char *reply,
				   int reply_size, char *buffer,
				   int buffer_size)
{
  CSS_CONN_ENTRY *conn;
  int rc = 0;
  int idx = css_return_entry_id_from_eid (eid);

  conn = &css_Conn_array[idx];
  if (buffer_size > 0 && buffer != NULL)
    {
      rc = css_send_two_data (conn, css_return_rid_from_eid (eid),
			      reply, reply_size, buffer, buffer_size);
    }
  else
    {
      rc = css_send_data (conn,
			  css_return_rid_from_eid (eid), reply, reply_size);
    }

  return (rc == NO_ERRORS) ? NO_ERROR : rc;
}

/*
 * css_send_reply_and_2_data_to_client() - send a reply to the server,
 *                                         and optionaly, an additional data
 *  buffer
 *   return:
 *   eid(in): enquiry id
 *   reply(in): the reply data (error or no error)
 *   reply_size(in): the size of the reply data.
 *   buffer1(in): data buffer to queue for expected data.
 *   buffer1_size(in): size of data buffer
 *   buffer2(in): data buffer to queue for expected data.
 *   buffer2_size(in): size of data buffer
 *
 * Note: This is to be used only by the server
 */
unsigned int
css_send_reply_and_2_data_to_client (unsigned int eid,
				     char *reply, int reply_size,
				     char *buffer1, int buffer1_size,
				     char *buffer2, int buffer2_size)
{
  CSS_CONN_ENTRY *conn;
  int rc = 0;
  int idx = css_return_entry_id_from_eid (eid);

  conn = &css_Conn_array[idx];

  if (buffer2 == NULL || buffer2_size <= 0)
    {
      return (css_send_reply_and_data_to_client (eid, reply, reply_size,
						 buffer1, buffer1_size));
    }
  rc = css_send_three_data (conn, css_return_rid_from_eid (eid),
			    reply, reply_size, buffer1, buffer1_size,
			    buffer2, buffer2_size);

  return (rc == NO_ERRORS) ? 0 : rc;
}

/*
 * css_send_reply_and_3_data_to_client() - send a reply to the server,
 *                                         and optionaly, an additional data
 *  buffer
 *   return:
 *   eid(in): enquiry id
 *   reply(in): the reply data (error or no error)
 *   reply_size(in): the size of the reply data.
 *   buffer1(in): data buffer to queue for expected data.
 *   buffer1_size(in): size of data buffer
 *   buffer2(in): data buffer to queue for expected data.
 *   buffer2_size(in): size of data buffer
 *   buffer3(in): data buffer to queue for expected data.
 *   buffer3_size(in): size of data buffer
 *
 * Note: This is to be used only by the server
 */
unsigned int
css_send_reply_and_3_data_to_client (unsigned int eid,
				     char *reply, int reply_size,
				     char *buffer1, int buffer1_size,
				     char *buffer2, int buffer2_size,
				     char *buffer3, int buffer3_size)
{
  CSS_CONN_ENTRY *conn;
  int rc = 0;
  int idx = css_return_entry_id_from_eid (eid);

  conn = &css_Conn_array[idx];

  if (buffer3 == NULL || buffer3_size <= 0)
    {
      return (css_send_reply_and_2_data_to_client (eid, reply, reply_size,
						   buffer1, buffer1_size,
						   buffer2, buffer2_size));
    }
  rc = css_send_four_data (conn, css_return_rid_from_eid (eid),
			   reply, reply_size, buffer1, buffer1_size,
			   buffer2, buffer2_size, buffer3, buffer3_size);

  return (rc == NO_ERRORS) ? 0 : rc;
}

/*
 * css_send_error_to_client() - send an error buffer to the server
 *   return:
 *   eid(in): enquiry id
 *   buffer(in): data buffer to queue for expected data.
 *   buffer_size(in): size of data buffer
 *
 * Note: This is to be used ONLY by the server to return error data to the
 *       client.
 */
unsigned int
css_send_error_to_client (unsigned int eid, char *buffer, int buffer_size)
{
  CSS_CONN_ENTRY *conn;
  int rc = 0;
  int idx = css_return_entry_id_from_eid (eid);

  conn = &css_Conn_array[idx];
  rc = css_send_error (conn, css_return_rid_from_eid (eid),
		       buffer, buffer_size);

  return (rc == NO_ERRORS) ? 0 : rc;
}

/*
 * css_send_abort_to_client() - send an abort message to the client
 *   return:
 *   eid(in): enquiry id
 */
unsigned int
css_send_abort_to_client (unsigned int eid)
{
  CSS_CONN_ENTRY *conn;
  int rc = 0;
  int idx = css_return_entry_id_from_eid (eid);

  conn = &css_Conn_array[idx];
  rc = css_send_abort_request (conn, css_return_rid_from_eid (eid));

  return (rc == NO_ERRORS) ? 0 : rc;
}

/*
 * css_test_for_client_errors () -
 *   return:
 *   conn(in):
 *   eid(in):
 */
static void
css_test_for_client_errors (CSS_CONN_ENTRY * conn, unsigned int eid)
{
  char *error_buffer;
  int error_size;
  int rc;

  if (css_return_queued_error (conn, css_return_rid_from_eid (eid),
			       &error_buffer, &error_size, &rc))
    {
      er_set_area_error ((void *) error_buffer);
      free_and_init (error_buffer);
    }
}

/*
 * css_receive_data_from_client() - return data that was sent by the server
 *   return:
 *   eid(in): enquiry id
 *   buffer(out): data buffer to send to client.
 *   buffer_size(out): size of data buffer
 */
unsigned int
css_receive_data_from_client (unsigned int eid, char **buffer, int *size)
{
  int rc = 0;
  int idx = css_return_entry_id_from_eid (eid);

  *size = 0;

  rc = css_receive_data (&css_Conn_array[idx],
			 css_return_rid_from_eid (eid), buffer, size);

  if (rc == NO_ERRORS || rc == RECORD_TRUNCATED)
    {
      css_test_for_client_errors (&css_Conn_array[idx], eid);
      return 0;
    }

  return rc;
}

/*
 * css_end_server_request() - terminates the request from the client
 *   return:
 *   conn(in/out):
 */
void
css_end_server_request (CSS_CONN_ENTRY * conn)
{
  csect_enter_critical_section (NULL, &conn->csect, INF_WAIT);

  css_remove_all_unexpected_packets (conn);
  conn->status = CONN_CLOSING;

  csect_exit_critical_section (&conn->csect);
}

/*
 * css_pack_server_name() -
 *   return: a new string containing the server name and the database version
 *           string
 *   server_name(in): the name of the database volume
 *   name_length(out): returned size of the server_name
 *
 * Note: Builds a character buffer with three embedded strings: the database
 *       volume name, a string containing the release identifier, and the
 *       CUBRID environment variable (if exists)
 */
char *
css_pack_server_name (const char *server_name, int *name_length)
{
  char *packed_name = NULL;
  const char *env_name = NULL;
  char pid_string[10], *s;
  const char *t;

  if (server_name != NULL)
    {
      env_name = envvar_root ();

      /*
       * here we changed the 2nd string in packed_name from
       * rel_release_string() to rel_major_release_string()
       * solely for the purpose of matching the name of the cubrid driver.
       * That is, the name of the cubrid driver has been changed to use
       * MAJOR_RELEASE_STRING (see drivers/Makefile).  So, here we must also
       * use rel_major_release_string(), so master can successfully find and
       * fork cubrid drivers.
       */

      sprintf (pid_string, "%d", getpid ());
      *name_length = strlen (server_name) + 1
	+ strlen (rel_major_release_string ()) + 1
	+ strlen (env_name) + 1 + strlen (pid_string) + 1;
      s = packed_name = (char *) malloc (*name_length);

      t = server_name;
      while (*t)
	*s++ = *t++;
      *s++ = '\0';

      t = rel_major_release_string ();
      while (*t)
	*s++ = *t++;
      *s++ = '\0';

      t = env_name;
      while (*t)
	*s++ = *t++;
      *s++ = '\0';

      t = pid_string;
      while (*t)
	*s++ = *t++;
      *s++ = '\0';
    }
  return packed_name;
}

#if 0
/*
 * css_add_client_timeout_from_eid
 *
 */
void
css_add_client_timeout_from_eid (unsigned int eid, int timeout_duration)
{
  CSS_CONN_ENTRY *entry;

  entry = thread_get_current_conn_entry ();
  if (entry != NULL)
    {
      css_add_client_timeout (entry, timeout_duration);
    }
}
#endif

/*
 * css_add_client_version_string() - add the version_string to socket queue
 *                                   entry structure
 *   return: pointer to version_string in the socket queue entry structure
 *   version_string(in):
 */
char *
css_add_client_version_string (const char *version_string)
{
  char *ver_str = NULL;
  CSS_CONN_ENTRY *entry;

  entry = thread_get_current_conn_entry ();
  if (entry != NULL)
    {
      if (entry->version_string == NULL)
	{
	  ver_str = (char *) malloc (strlen (version_string) + 1);
	  if (ver_str != NULL)
	    {
	      strcpy (ver_str, version_string);
	      entry->version_string = ver_str;
	    }
	}
      else
	{
	  /* already registered */
	  ver_str = entry->version_string;
	}
    }

  return ver_str;
}

/*
 * css_get_client_version_string() - retrieve the version_string to socket
 *                                   queue entry structure
 *   return:
 */
char *
css_get_client_version_string (void)
{
  CSS_CONN_ENTRY *entry;

  entry = thread_get_current_conn_entry ();
  if (entry != NULL)
    {
      return entry->version_string;
    }
  else
    {
      return NULL;
    }
}

/*
 * css_cleanup_server_queues () -
 *   return:
 *   eid(in):
 */
void
css_cleanup_server_queues (unsigned int eid)
{
  int idx = css_return_entry_id_from_eid (eid);

  css_remove_all_unexpected_packets (&css_Conn_array[idx]);
}

/*
 * css_number_of_clients() - Returns the number of clients connected to
 *                           the server
 *   return:
 */
int
css_number_of_clients (void)
{
  int n = 0;
  CSS_CONN_ENTRY *conn;

  csect_enter_critical_section_as_reader (NULL, &css_Active_conn_csect,
					  INF_WAIT);

  for (conn = css_Active_conn_anchor; conn != NULL; conn = conn->next)
    {
      if (conn != css_Master_conn)
	{
	  n++;
	}
    }

  csect_exit_critical_section (&css_Active_conn_csect);

  return n;
}

/*
 * css_wait_worker_thread_on_jobq () -
 *   return:
 *   thrd(in):
 *   jobq_index(in):
 */
static int
css_wait_worker_thread_on_jobq (THREAD_ENTRY * thrd, int jobq_index)
{
#if defined(WINDOWS)
  int r;
#endif /* WINDOWS */

  /* add thrd in the front of job queue */
  thrd->worker_thrd_list = css_Job_queue[jobq_index].worker_thrd_list;
  css_Job_queue[jobq_index].worker_thrd_list = thrd;

  /* sleep on the thrd's condition variable with the mutex of the job queue */
  COND_WAIT (thrd->wakeup_cond, css_Job_queue[jobq_index].job_lock);
#if defined(WINDOWS)
  MUTEX_LOCK (r, css_Job_queue[jobq_index].job_lock);
  CSS_CHECK_RETURN_ERROR (r, ER_CSS_PTHREAD_MUTEX_UNLOCK);
#endif /* WINDOWS */

  return NO_ERROR;
}

/*
 * css_wakeup_worker_thread_on_jobq () -
 *   return:
 *   jobq_index(in):
 */
static int
css_wakeup_worker_thread_on_jobq (int jobq_index)
{
  THREAD_ENTRY *wait_thrd = NULL, *prev_thrd = NULL;
  int r = NO_ERROR;

  for (wait_thrd = css_Job_queue[jobq_index].worker_thrd_list;
       wait_thrd; wait_thrd = wait_thrd->worker_thrd_list)
    {
      /* wakeup a free worker thread */
      if (wait_thrd->status == TS_FREE
	  && (r = thread_wakeup (wait_thrd)) == NO_ERROR)
	{
	  if (prev_thrd == NULL)
	    {
	      css_Job_queue[jobq_index].worker_thrd_list =
		wait_thrd->worker_thrd_list;
	    }
	  else
	    {
	      prev_thrd->worker_thrd_list = wait_thrd->worker_thrd_list;
	    }
	  wait_thrd->worker_thrd_list = NULL;
	  break;
	}
      prev_thrd = wait_thrd;
    }

  return r;
}
