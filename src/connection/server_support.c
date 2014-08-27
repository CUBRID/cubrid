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
 * server_support.c - server interface
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
#include <assert.h>

#include "porting.h"
#include "thread.h"
#include "memory_alloc.h"
#include "boot_sr.h"
#include "connection_defs.h"
#include "connection_globals.h"
#include "release_string.h"
#include "system_parameter.h"
#include "environment_variable.h"
#include "error_manager.h"
#include "job_queue.h"
#include "thread.h"
#include "connection_error.h"
#include "message_catalog.h"
#include "critical_section.h"
#include "lock_manager.h"
#include "log_manager.h"
#include "network.h"
#include "jsp_sr.h"
#if defined(WINDOWS)
#include "wintcp.h"
#else /* WINDOWS */
#include "tcp.h"
#endif /* WINDOWS */
#include "connection_sr.h"
#include "xserver_interface.h"
#include "server_support.h"
#include "utility.h"
#if !defined(WINDOWS)
#include "heartbeat.h"
#endif

#define CSS_WAIT_COUNT 5	/* # of retry to connect to master */
#define CSS_NUM_JOB_QUEUE 10	/* # of job queues */
#define CSS_GOING_DOWN_IMMEDIATELY "Server going down immediately"

#if defined(WINDOWS)
#define SockError    SOCKET_ERROR
#else /* WINDOWS */
#define SockError    -1
#endif /* WINDOWS */

static struct timeval css_Shutdown_timeout = { 0, 0 };
static char *css_Master_server_name = NULL;	/* database identifier */
static int css_Master_port_id;
static CSS_CONN_ENTRY *css_Master_conn;
#if defined(WINDOWS)
static int css_Win_kill_signaled = 0;
#endif /* WINDOWS */
static IP_INFO *css_Server_accessible_ip_info;
static char *ip_list_file_name = NULL;
static char ip_file_real_path[PATH_MAX];

/* internal request hander function */
static int (*css_Server_request_handler) (THREAD_ENTRY *, unsigned int,
					  int, int, char *);

/* server's state for HA feature */
static HA_SERVER_STATE ha_Server_state = HA_SERVER_STATE_IDLE;
static bool ha_Repl_delay_detected = false;

static int ha_Server_num_of_hosts = 0;

typedef struct job_queue JOB_QUEUE;
struct job_queue
{
  pthread_mutex_t job_lock;
  CSS_LIST job_list;
  THREAD_ENTRY *worker_thrd_list;
  pthread_mutex_t free_lock;
  CSS_JOB_ENTRY *free_list;
};

static JOB_QUEUE css_Job_queue[CSS_NUM_JOB_QUEUE] = {
  {PTHREAD_MUTEX_INITIALIZER, {NULL, NULL, NULL, 0, 0},
   NULL, PTHREAD_MUTEX_INITIALIZER, NULL},
  {PTHREAD_MUTEX_INITIALIZER, {NULL, NULL, NULL, 0, 0},
   NULL, PTHREAD_MUTEX_INITIALIZER, NULL},
  {PTHREAD_MUTEX_INITIALIZER, {NULL, NULL, NULL, 0, 0},
   NULL, PTHREAD_MUTEX_INITIALIZER, NULL},
  {PTHREAD_MUTEX_INITIALIZER, {NULL, NULL, NULL, 0, 0},
   NULL, PTHREAD_MUTEX_INITIALIZER, NULL},
  {PTHREAD_MUTEX_INITIALIZER, {NULL, NULL, NULL, 0, 0},
   NULL, PTHREAD_MUTEX_INITIALIZER, NULL},
  {PTHREAD_MUTEX_INITIALIZER, {NULL, NULL, NULL, 0, 0},
   NULL, PTHREAD_MUTEX_INITIALIZER, NULL},
  {PTHREAD_MUTEX_INITIALIZER, {NULL, NULL, NULL, 0, 0},
   NULL, PTHREAD_MUTEX_INITIALIZER, NULL},
  {PTHREAD_MUTEX_INITIALIZER, {NULL, NULL, NULL, 0, 0},
   NULL, PTHREAD_MUTEX_INITIALIZER, NULL},
  {PTHREAD_MUTEX_INITIALIZER, {NULL, NULL, NULL, 0, 0},
   NULL, PTHREAD_MUTEX_INITIALIZER, NULL},
  {PTHREAD_MUTEX_INITIALIZER, {NULL, NULL, NULL, 0, 0},
   NULL, PTHREAD_MUTEX_INITIALIZER, NULL}
};

#define HA_LOG_APPLIER_STATE_TABLE_MAX  5
typedef struct ha_log_applier_state_table HA_LOG_APPLIER_STATE_TABLE;
struct ha_log_applier_state_table
{
  int client_id;
  HA_LOG_APPLIER_STATE state;
};

static HA_LOG_APPLIER_STATE_TABLE
  ha_Log_applier_state[HA_LOG_APPLIER_STATE_TABLE_MAX] = {
  {-1, HA_LOG_APPLIER_STATE_NA},
  {-1, HA_LOG_APPLIER_STATE_NA},
  {-1, HA_LOG_APPLIER_STATE_NA},
  {-1, HA_LOG_APPLIER_STATE_NA},
  {-1, HA_LOG_APPLIER_STATE_NA}
};
static int ha_Log_applier_state_num = 0;

static int css_free_job_entry_func (void *data, void *dummy);
static void css_empty_job_queue (void);
static void css_setup_server_loop (void);
static int css_check_conn (CSS_CONN_ENTRY * p);
static void css_set_shutdown_timeout (int timeout);
static int css_get_master_request (SOCKET master_fd);
static int css_process_master_request (SOCKET master_fd);
static void css_process_shutdown_request (SOCKET master_fd);
static void css_process_new_client (SOCKET master_fd);
static void css_process_get_server_ha_mode_request (SOCKET master_fd);
static void css_process_change_server_ha_mode_request (SOCKET master_fd);
static void css_process_get_eof_request (SOCKET master_fd);

static void css_close_connection_to_master (void);
static int css_reestablish_connection_to_master (void);
static void dummy_sigurg_handler (int sig);
static int css_connection_handler_thread (THREAD_ENTRY * thrd,
					  CSS_CONN_ENTRY * conn);
static int css_internal_connection_handler (CSS_CONN_ENTRY * conn);
static int css_internal_request_handler (THREAD_ENTRY * thrd,
					 CSS_THREAD_ARG arg);
static int css_test_for_client_errors (CSS_CONN_ENTRY * conn,
				       unsigned int eid);
static int css_wait_worker_thread_on_jobq (THREAD_ENTRY * thrd,
					   int jobq_index);
static bool css_can_occupy_worker_thread_on_jobq (int jobq_index);
static int css_wakeup_worker_thread_on_jobq (int jobq_index);
static int css_check_accessibility (SOCKET new_fd);

#if defined(WINDOWS)
static int css_process_new_connection_request (void);
static BOOL WINAPI ctrl_sig_handler (DWORD ctrl_event);
#endif /* WINDOWS */

static bool css_check_ha_log_applier_done (void);
static bool css_check_ha_log_applier_working (void);

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
      if (job_entry_p == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
		  1, sizeof (CSS_JOB_ENTRY));
	  return NULL;
	}

      job_entry_p->jobq_index = jobq_index;
    }
  else
    {
      rv = pthread_mutex_lock (&css_Job_queue[jobq_index].free_lock);
      if (css_Job_queue[jobq_index].free_list == NULL)
	{
	  pthread_mutex_unlock (&css_Job_queue[jobq_index].free_lock);
	  job_entry_p = (CSS_JOB_ENTRY *) malloc (sizeof (CSS_JOB_ENTRY));
	  if (job_entry_p == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (CSS_JOB_ENTRY));
	      return NULL;
	    }

	  job_entry_p->jobq_index = jobq_index;
	}
      else
	{
	  job_entry_p = css_Job_queue[jobq_index].free_list;
	  css_Job_queue[jobq_index].free_list = job_entry_p->next;
	  pthread_mutex_unlock (&css_Job_queue[jobq_index].free_lock);
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
  int rv, jobq_index;

  if (job_entry_p != NULL)
    {
      jobq_index = job_entry_p->jobq_index;

      rv = pthread_mutex_lock (&css_Job_queue[jobq_index].free_lock);

      job_entry_p->next = css_Job_queue[jobq_index].free_list;
      css_Job_queue[jobq_index].free_list = job_entry_p;

      pthread_mutex_unlock (&css_Job_queue[jobq_index].free_lock);
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
  int num_job_list;

  num_job_list = NUM_NON_SYSTEM_TRANS;
  for (i = 0; i < CSS_NUM_JOB_QUEUE; i++)
    {
#if defined(WINDOWS)
      r = pthread_mutex_init (&css_Job_queue[i].job_lock, NULL);
      CSS_CHECK_RETURN (r, ER_CSS_PTHREAD_MUTEX_INIT);
      r = pthread_mutex_init (&css_Job_queue[i].free_lock, NULL);
      CSS_CHECK_RETURN (r, ER_CSS_PTHREAD_MUTEX_INIT);
#endif /* WINDOWS */

      css_initialize_list (&css_Job_queue[i].job_list, num_job_list);
      css_Job_queue[i].free_list = NULL;

      for (j = 0; j < num_job_list; j++)
	{
	  job_entry_p = (CSS_JOB_ENTRY *) malloc (sizeof (CSS_JOB_ENTRY));
	  if (job_entry_p == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (CSS_JOB_ENTRY));
	      break;
	    }

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
  THREAD_ENTRY *thrd = NULL;
  int rv;
  int i, thread_count;

  /* idle worker threads are blocked on the job queue. */
  for (i = 0; i < CSS_NUM_JOB_QUEUE; i++)
    {
      rv = pthread_mutex_lock (&css_Job_queue[i].job_lock);

      thrd = css_Job_queue[i].worker_thrd_list;
      thread_count = 0;

      while (thrd)
	{
	  thread_wakeup (thrd, THREAD_RESUME_DUE_TO_SHUTDOWN);
	  thrd = thrd->worker_thrd_list;
	  thread_count++;

	  if (thread_count > thread_num_worker_threads ())
	    {
	      /* prevent infinite loop */
	      assert (0);
	      break;
	    }
	}

      pthread_mutex_unlock (&css_Job_queue[i].job_lock);
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
  int i, j, jobq_index;
  const int MAX_NTRIES = 100;

  jobq_index = job_entry_p->jobq_index;

  for (i = 0; i <= MAX_NTRIES; i++)
    {
      for (j = 0; j < CSS_NUM_JOB_QUEUE; j++)
	{
	  rv = pthread_mutex_lock (&css_Job_queue[jobq_index].job_lock);

	  if (css_can_occupy_worker_thread_on_jobq (jobq_index)
	      || i == MAX_NTRIES)
	    {
	      /* If there's no available thread even though it has probed
	       * the entire job queues, just add the job to the original job queue.
	       */
	      css_add_list (&css_Job_queue[jobq_index].job_list, job_entry_p);
	      css_wakeup_worker_thread_on_jobq (jobq_index);

	      pthread_mutex_unlock (&css_Job_queue[jobq_index].job_lock);
	      return;
	    }

	  pthread_mutex_unlock (&css_Job_queue[jobq_index].job_lock);

	  /* linear probing */
	  jobq_index++;
	  jobq_index %= CSS_NUM_JOB_QUEUE;
	}

      thread_sleep (10 * ((i / 10) + 1));	/* sleep 10ms upto 100ms */
    }
}

/*
 * css_get_new_job() - fetch a job from the queue
 *   return:
 */
CSS_JOB_ENTRY *
css_get_new_job (void)
{
  CSS_JOB_ENTRY *job_entry_p;
  THREAD_ENTRY *thrd = thread_get_thread_entry_info ();
  int jobq_index = thrd->index % CSS_NUM_JOB_QUEUE;
  int r;

  r = pthread_mutex_lock (&css_Job_queue[jobq_index].job_lock);

  if (css_Job_queue[jobq_index].job_list.count == 0)
    {
      r = css_wait_worker_thread_on_jobq (thrd, jobq_index);
      if (r != NO_ERROR)
	{
	  pthread_mutex_unlock (&css_Job_queue[jobq_index].job_lock);
	  r = pthread_mutex_lock (&thrd->tran_index_lock);

	  return NULL;
	}
    }

  job_entry_p = (CSS_JOB_ENTRY *)
    css_remove_list_from_head (&css_Job_queue[jobq_index].job_list);

  pthread_mutex_unlock (&css_Job_queue[jobq_index].job_lock);

  r = pthread_mutex_lock (&thrd->tran_index_lock);

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

  rv = pthread_mutex_lock (&css_Job_queue[job_entry_p->jobq_index].free_lock);

  job_entry_p->next = css_Job_queue[job_entry_p->jobq_index].free_list;
  css_Job_queue[job_entry_p->jobq_index].free_list = job_entry_p;

  pthread_mutex_unlock (&css_Job_queue[job_entry_p->jobq_index].free_lock);

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
      rv = pthread_mutex_lock (&css_Job_queue[i].job_lock);

      css_traverse_list (&css_Job_queue[i].job_list, css_free_job_entry_func,
			 NULL);

      pthread_mutex_unlock (&css_Job_queue[i].job_lock);
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
      rv = pthread_mutex_lock (&css_Job_queue[i].job_lock);

      css_traverse_list (&css_Job_queue[i].job_list, css_free_job_entry_func,
			 NULL);
      css_finalize_list (&css_Job_queue[i].job_list);
      while (css_Job_queue[i].free_list != NULL)
	{
	  p = css_Job_queue[i].free_list;
	  css_Job_queue[i].free_list = p->next;
	  free_and_init (p);
	}

      pthread_mutex_unlock (&css_Job_queue[i].job_lock);
    }
}

/*
 * css_setup_server_loop() -
 *   return:
 */
static void
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

  if (!IS_INVALID_SOCKET (css_Pipe_to_master))
    {
      /* execute master thread. */
      css_master_thread ();

    }
  else
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ERR_CSS_MASTER_PIPE_ERROR, 0);
    }
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
      || p->status != CONN_OPEN)
    {
      return ER_FAILED;
    }
#else /* WINDOWS */
  if (fcntl (p->fd, F_GETFL, status) < 0 || p->status != CONN_OPEN)
    {
      return ER_FAILED;
    }
#endif /* WINDOWS */

  return NO_ERROR;
}

/*
 * css_set_shutdown_timeout() -
 *   return:
 *   timeout(in):
 */
static void
css_set_shutdown_timeout (int timeout)
{
  if (gettimeofday (&css_Shutdown_timeout, NULL) == 0)
    {
      css_Shutdown_timeout.tv_sec += timeout;
    }
  return;
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
  int r, run_code = 1, status = 0, nfds;
  struct pollfd po[] = { {0, 0, 0}, {0, 0, 0} };

  while (run_code)
    {
      /* check if socket has error or client is down */
      if (!IS_INVALID_SOCKET (css_Pipe_to_master)
	  && css_check_conn (css_Master_conn) < 0)
	{
	  css_shutdown_conn (css_Master_conn);
	  css_Pipe_to_master = INVALID_SOCKET;
	}

      if (!IS_INVALID_SOCKET (css_Pipe_to_master))
	{
	  po[0].fd = css_Pipe_to_master;
	  po[0].events = POLLIN;
	  nfds = 1;
	}
#if defined(WINDOWS)
      if (!IS_INVALID_SOCKET (css_Server_connection_socket))
	{
	  po[1].fd = css_Server_connection_socket;
	  po[1].events = POLLIN;
	  nfds = 2;
	}
#endif /* WINDOWS */

      /* select() sets timeout value to 0 or waited time */
      r = poll (po, nfds,
		(prm_get_integer_value (PRM_ID_TCP_CONNECTION_TIMEOUT) *
		 1000));
      if (r > 0
	  && (IS_INVALID_SOCKET (css_Pipe_to_master)
	      || !(po[0].revents & POLLIN))
#if defined(WINDOWS)
	  && (IS_INVALID_SOCKET (css_Server_connection_socket)
	      || !(po[1].revents & POLLIN))
#endif /* WINDOWS */
	)
	{
	  continue;
	}

      if (r < 0)
	{
	  if (!IS_INVALID_SOCKET (css_Pipe_to_master)
#if defined(WINDOWS)
	      && ioctlsocket (css_Pipe_to_master, FIONREAD,
			      (u_long *) & status) == SockError
#else /* WINDOWS */
	      && fcntl (css_Pipe_to_master, F_GETFL, status) == SockError
#endif /* WINDOWS */
	    )
	    {
	      css_close_connection_to_master ();
	      break;
	    }
	}
      else if (r > 0)
	{
	  if (!IS_INVALID_SOCKET (css_Pipe_to_master)
	      && (po[0].revents & POLLIN))
	    {
	      run_code = css_process_master_request (css_Pipe_to_master);
	      if (run_code == -1)
		{
		  css_close_connection_to_master ();
		  /* shutdown message received */
		  run_code =
		    (prm_get_integer_value (PRM_ID_HA_MODE) !=
		     HA_MODE_OFF) ? 0 : 1;
		}

	      if (run_code == 0
		  && prm_get_integer_value (PRM_ID_HA_MODE) != HA_MODE_OFF)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			  ER_HB_PROCESS_EVENT, 2,
			  "Disconnected with the cub_master and will shut itself down",
			  "");
		}
	    }
#if !defined(WINDOWS)
	  else
	    {
	      break;
	    }

#else /* !WINDOWS */
	  if (!IS_INVALID_SOCKET (css_Server_connection_socket)
	      && (po[1].revents & POLLIN))
	    {
	      css_process_new_connection_request ();
	    }
#endif /* !WINDOWS */
	}

      if (run_code)
	{
	  if (IS_INVALID_SOCKET (css_Pipe_to_master))
	    {
	      css_reestablish_connection_to_master ();
	    }
	}
      else
	{
	  break;
	}
    }

  css_set_shutdown_timeout (prm_get_integer_value
			    (PRM_ID_SHUTDOWN_WAIT_TIME_IN_SECS));

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
css_get_master_request (SOCKET master_fd)
{
  int request, r;

  r = css_readn (master_fd, (char *) &request, sizeof (int), -1);
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
css_process_master_request (SOCKET master_fd)
{
  int request, r;

  r = 1;
  request = (int) css_get_master_request (master_fd);

  switch (request)
    {
    case SERVER_START_NEW_CLIENT:
      css_process_new_client (master_fd);
      break;

    case SERVER_START_SHUTDOWN:
      css_process_shutdown_request (master_fd);
      r = 0;
      break;

    case SERVER_STOP_SHUTDOWN:
    case SERVER_SHUTDOWN_IMMEDIATE:
    case SERVER_START_TRACING:
    case SERVER_STOP_TRACING:
    case SERVER_HALT_EXECUTION:
    case SERVER_RESUME_EXECUTION:
    case SERVER_REGISTER_HA_PROCESS:
      break;
    case SERVER_GET_HA_MODE:
      css_process_get_server_ha_mode_request (master_fd);
      break;
#if !defined(WINDOWS)
    case SERVER_CHANGE_HA_MODE:
      css_process_change_server_ha_mode_request (master_fd);
      break;
    case SERVER_GET_EOF:
      css_process_get_eof_request (master_fd);
      break;
#endif
    default:
      /* master do not respond */
      r = -1;
      break;
    }

  return r;
}

/*
 * css_process_shutdown_request () -
 *   return:
 *   master_fd(in):
 */
static void
css_process_shutdown_request (SOCKET master_fd)
{
  char buffer[MASTER_TO_SRV_MSG_SIZE];
  int r, timeout;

  timeout = (int) css_get_master_request (master_fd);

  r = css_readn (master_fd, buffer, MASTER_TO_SRV_MSG_SIZE, -1);
  if (r < 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ERR_CSS_SHUTDOWN_ERROR, 0);
      return;
    }
}

/*
 * css_process_new_client () -
 *   return:
 *   master_fd(in):
 *   read_fd_var(in/out):
 *   exception_fd_var(in/out):
 */
static void
css_process_new_client (SOCKET master_fd)
{
  SOCKET new_fd;
  int reason;
  CSS_CONN_ENTRY *conn;
  unsigned short rid;
  CSS_CONN_ENTRY temp_conn;
  void *area;
  char buffer[1024];
  int length = 1024;

  /* receive new socket descriptor from the master */
  new_fd = css_open_new_socket_from_master (master_fd, &rid);
  if (IS_INVALID_SOCKET (new_fd))
    {
      return;
    }

  if (prm_get_bool_value (PRM_ID_ACCESS_IP_CONTROL) == true &&
      css_check_accessibility (new_fd) != NO_ERROR)
    {
      css_initialize_conn (&temp_conn, new_fd);
      csect_initialize_critical_section (&temp_conn.csect);

      reason = htonl (SERVER_INACCESSIBLE_IP);
      css_send_data (&temp_conn, rid, (char *) &reason, (int) sizeof (int));

      area = er_get_area_error (buffer, &length);

      temp_conn.db_error = ER_INACCESSIBLE_IP;
      css_send_error (&temp_conn, rid, (const char *) area, length);
      css_shutdown_conn (&temp_conn);
      css_dealloc_conn_csect (&temp_conn);
      er_clear ();
      return;
    }

  conn = css_make_conn (new_fd);
  if (conn == NULL)
    {
      css_initialize_conn (&temp_conn, new_fd);
      csect_initialize_critical_section (&temp_conn.csect);

      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_CLIENTS_EXCEEDED,
	      1, NUM_NORMAL_TRANS);
      reason = htonl (SERVER_CLIENTS_EXCEEDED);
      css_send_data (&temp_conn, rid, (char *) &reason, (int) sizeof (int));

      area = er_get_area_error (buffer, &length);

      temp_conn.db_error = ER_CSS_CLIENTS_EXCEEDED;
      css_send_error (&temp_conn, rid, (const char *) area, length);
      css_shutdown_conn (&temp_conn);
      css_dealloc_conn_csect (&temp_conn);
      er_clear ();
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
 * css_process_get_server_ha_mode_request() -
 *   return:
 */
static void
css_process_get_server_ha_mode_request (SOCKET master_fd)
{
  int r;
  int response;

  if (prm_get_integer_value (PRM_ID_HA_MODE) == HA_MODE_OFF)
    {
      response = htonl (HA_SERVER_STATE_NA);
    }
  else
    {
      response = htonl (ha_Server_state);
    }

  r = send (master_fd, (char *) &response, sizeof (int), 0);
  if (r < 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_WRITE, 0);
      return;
    }

}

/*
 * css_process_get_server_ha_mode_request() -
 *   return:
 */
static void
css_process_change_server_ha_mode_request (SOCKET master_fd)
{
#if !defined(WINDOWS)
  int rv;
  int state;
  int response;
  THREAD_ENTRY *thread_p;

  state = (int) css_get_master_request (master_fd);

  thread_p = thread_get_thread_entry_info ();
  assert (thread_p != NULL);

  if (state == HA_SERVER_STATE_ACTIVE || state == HA_SERVER_STATE_STANDBY)
    {
      if (css_change_ha_server_state
	  (thread_p, state, false, HA_CHANGE_MODE_IMMEDIATELY,
	   true) != NO_ERROR)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ERR_CSS_ERROR_FROM_SERVER,
		  1, "Cannot change server HA mode");
	}
    }
  else
    {
      er_log_debug (ARG_FILE_LINE,
		    "ERROR : unexpected state. (state :%d). \n", state);
    }

  state = htonl ((int) css_ha_server_state ());

  css_send_heartbeat_request (css_Master_conn, SERVER_CHANGE_HA_MODE);
  css_send_heartbeat_data (css_Master_conn, (char *) &state, sizeof (state));
#endif
}

/*
 * css_process_get_eof_request() -
 *   return:
 */
static void
css_process_get_eof_request (SOCKET master_fd)
{
#if !defined(WINDOWS)
  LOG_LSA *eof_lsa;
  OR_ALIGNED_BUF (OR_LOG_LSA_ALIGNED_SIZE) a_reply;
  char *reply;
  THREAD_ENTRY *thread_p;

  reply = OR_ALIGNED_BUF_START (a_reply);

  thread_p = thread_get_thread_entry_info ();
  assert (thread_p != NULL);

  LOG_CS_ENTER_READ_MODE (thread_p);

  eof_lsa = log_get_eof_lsa ();
  (void) or_pack_log_lsa (reply, eof_lsa);

  LOG_CS_EXIT (thread_p);

  css_send_heartbeat_request (css_Master_conn, SERVER_GET_EOF);
  css_send_heartbeat_data (css_Master_conn, reply,
			   OR_ALIGNED_BUF_SIZE (a_reply));
#endif
}

/*
 * css_close_connection_to_master() -
 *   return:
 */
static void
css_close_connection_to_master (void)
{
  if (!IS_INVALID_SOCKET (css_Pipe_to_master))
    {
      css_shutdown_conn (css_Master_conn);
    }
  css_Pipe_to_master = INVALID_SOCKET;
  css_Master_conn = NULL;
}

/*
 * css_shutdown_timeout() -
 *   return:
 */
bool
css_is_shutdown_timeout_expired (void)
{
  struct timeval timeout;

  /* css_Shutdown_timeout is set by shutdown request */
  if (css_Shutdown_timeout.tv_sec != 0 && gettimeofday (&timeout, NULL) == 0)
    {
      if (css_Shutdown_timeout.tv_sec <= timeout.tv_sec)
	{
	  return true;
	}
    }

  return false;
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
  SOCKET new_fd;
  int reason, buffer_size, rc;
  CSS_CONN_ENTRY *conn;
  unsigned short rid;
  char *buffer[1024];
  int length = 1024;

  NET_HEADER header = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };

  new_fd = css_server_accept (css_Server_connection_socket);

  if (!IS_INVALID_SOCKET (new_fd))
    {
      if (prm_get_bool_value (PRM_ID_ACCESS_IP_CONTROL) == true &&
	  css_check_accessibility (new_fd) != NO_ERROR)
	{
	  CSS_CONN_ENTRY new_conn;
	  void *error_string;

	  css_initialize_conn (&new_conn, new_fd);
	  csect_initialize_critical_section (&new_conn.csect);

	  rc = css_read_header (&new_conn, &header);
	  buffer_size = rid = 0;

	  reason = htonl (SERVER_INACCESSIBLE_IP);
	  css_send_data (&new_conn, rid, (char *) &reason,
			 (int) sizeof (int));

	  error_string = er_get_area_error (buffer, &length);
	  new_conn.db_error = ER_INACCESSIBLE_IP;
	  css_send_error (&new_conn, rid, (const char *) error_string,
			  length);
	  css_shutdown_conn (&new_conn);
	  css_dealloc_conn_csect (&new_conn);

	  er_clear ();
	  return -1;
	}

      conn = css_make_conn (new_fd);
      if (conn == NULL)
	{
	  /*
	   * all pre-allocated connection entries are being used now.
	   * create a new entry and send error message throuth it.
	   */
	  CSS_CONN_ENTRY new_conn;
	  void *error_string;

	  css_initialize_conn (&new_conn, new_fd);
	  csect_initialize_critical_section (&new_conn.csect);

	  rc = css_read_header (&new_conn, &header);
	  buffer_size = rid = 0;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_CLIENTS_EXCEEDED,
		  1, NUM_NORMAL_TRANS);
	  reason = htonl (SERVER_CLIENTS_EXCEEDED);
	  css_send_data (&new_conn, rid, (char *) &reason,
			 (int) sizeof (int));

	  error_string = er_get_area_error (buffer, &length);
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
	      rid = (unsigned short) ntohl (header.request_id);

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
    {
      return 0;
    }
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
	    {
	      css_free_conn (css_Master_conn);
	    }
	  css_Master_conn = conn;
	  free_and_init (packed_server_name);
	  return 1;
	}
      else
	{
	  free_and_init (packed_server_name);
	}
    }

  css_Pipe_to_master = INVALID_SOCKET;
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
 * css_connection_handler_thread () - Accept/process request from
 *                                    one client
 *   return:
 *   arg(in):
 *
 * Note: One server thread per one client
 */
static int
css_connection_handler_thread (THREAD_ENTRY * thread_p, CSS_CONN_ENTRY * conn)
{
  CSS_JOB_ENTRY *job;
  int n, type, rv, status, timeout;
  int prefetchlogdb_max_thread_count = 0;
  SOCKET fd;
  struct pollfd po[1] = { {0, 0, 0} };

  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  fd = conn->fd;

  pthread_mutex_unlock (&thread_p->tran_index_lock);

  thread_p->type = TT_SERVER;	/* server thread */

  prefetchlogdb_max_thread_count =
    prm_get_integer_value (PRM_ID_HA_PREFETCHLOGDB_MAX_THREAD_COUNT);

  status = NO_ERRORS;
  /* check if socket has error or client is down */
  while (thread_p->shutdown == false && conn->stop_talk == false)
    {
      /* check the connection */
      if (css_check_conn (conn) != NO_ERROR)
	{
	  status = CONNECTION_CLOSED;
	  break;
	}

      po[0].fd = fd;
      po[0].events = POLLIN;
      po[0].revents = 0;
      timeout = 5000;
      n = poll (po, 1, timeout);
      if (n == 0)
	{
#if !defined (WINDOWS)
	  /* 0 means it timed out and no fd is changed. */
	  if (CHECK_CLIENT_IS_ALIVE ())
	    {
	      if (css_peer_alive (fd, timeout) == false)
		{
		  er_log_debug (ARG_FILE_LINE,
				"css_connection_handler_thread: "
				"css_peer_alive() error\n");
		  status = CONNECTION_CLOSED;
		  break;
		}
	    }

	  /* check server's HA state */
	  if (ha_Server_state == HA_SERVER_STATE_TO_BE_STANDBY
	      && conn->in_transaction == false
	      && thread_has_threads (thread_p, conn->transaction_id,
				     conn->client_id) == 0)
	    {
	      status = REQUEST_REFUSED;
	      break;
	    }
#endif /* !WINDOWS */
	  continue;
	}
      else if (n < 0)
	{
	  if (errno == EINTR)
	    {
	      continue;
	    }
	  else
	    {
	      er_log_debug (ARG_FILE_LINE,
			    "css_connection_handler_thread: "
			    "select() error\n");
	      status = ERROR_ON_READ;
	      break;
	    }
	}
      else
	{
	  if (po[0].revents & POLLERR || po[0].revents & POLLHUP)
	    {
	      status = ERROR_ON_READ;
	      break;
	    }

	  /* read command/data/etc request from socket,
	     and enqueue it to appr. queue */
	  status = css_read_and_queue (conn, &type);
	  if (status != NO_ERRORS)
	    {
	      er_log_debug (ARG_FILE_LINE,
			    "css_connection_handler_thread: "
			    "css_read_and_queue() error\n");
	      break;
	    }
	  else
	    {
	      /* if new command request has arrived,
	         make new job and add it to job queue */
	      if (type == COMMAND_TYPE)
		{
		  do
		    {
		      if (conn->client_type == BOOT_CLIENT_LOG_PREFETCHER
			  && (prefetchlogdb_max_thread_count
			      > 0)
			  && (conn->prefetcher_thread_count >=
			      prefetchlogdb_max_thread_count))
			{
			  thread_sleep (10);	/* 10 msec */
			  continue;
			}

		      break;
		    }
		  while (thread_p->shutdown == false
			 && conn->stop_talk == false);

		  job = css_make_job_entry (conn, css_Request_handler,
					    (CSS_THREAD_ARG) conn, -1);
		  if (job)
		    {
		      css_add_to_job_queue (job);
		    }
		}
	    }
	}
    }

  /* check the connection and call connection error handler */
  if (status != NO_ERRORS || css_check_conn (conn) != NO_ERROR)
    {
      er_log_debug (ARG_FILE_LINE, "css_connection_handler_thread: "
		    "status %d conn { status %d transaction_id %d "
		    "db_error %d stop_talk %d stop_phase %d }\n",
		    status, conn->status, conn->transaction_id,
		    conn->db_error, conn->stop_talk, conn->stop_phase);
      rv = pthread_mutex_lock (&thread_p->tran_index_lock);
      (*css_Connection_error_handler) (thread_p, conn);
    }
  else
    {
      assert (thread_p->shutdown == true || conn->stop_talk == true);
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
  int r;
#if !defined(WINDOWS)
  int sig;
  sigset_t sigurg_mask;
  struct sigaction act;
#endif /* !WINDOWS */

  thrd_entry = (THREAD_ENTRY *) arg;

  /* wait until THREAD_CREATE finish */
  r = pthread_mutex_lock (&thrd_entry->th_entry_lock);
  pthread_mutex_unlock (&thrd_entry->th_entry_lock);

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
css_block_all_active_conn (unsigned short stop_phase)
{
  CSS_CONN_ENTRY *conn;

  csect_enter (NULL, CSECT_CONN_ACTIVE, INF_WAIT);

  for (conn = css_Active_conn_anchor; conn != NULL; conn = conn->next)
    {
#if defined(SERVER_MODE)
      assert (conn->csect.cs_index == CRITICAL_SECTION_COUNT + conn->idx);
      assert (conn->csect.name == css_Csect_name_conn);
#endif

      csect_enter_critical_section (NULL, &conn->csect, INF_WAIT);
      if (conn->stop_phase != stop_phase)
	{
#if defined(SERVER_MODE)
	  assert (conn->csect.cs_index == CRITICAL_SECTION_COUNT + conn->idx);
	  assert (conn->csect.name == css_Csect_name_conn);
#endif

	  csect_exit_critical_section (NULL, &conn->csect);
	  continue;
	}
      css_end_server_request (conn);
      if (!IS_INVALID_SOCKET (conn->fd) && conn->fd != css_Pipe_to_master)
	{
	  conn->stop_talk = true;
	  logtb_set_tran_index_interrupt (NULL, conn->transaction_id, 1);
	  thread_sleep (10);	/* 10 msec */
	}

#if defined(SERVER_MODE)
      assert (conn->csect.cs_index == CRITICAL_SECTION_COUNT + conn->idx);
      assert (conn->csect.name == css_Csect_name_conn);
#endif

      csect_exit_critical_section (NULL, &conn->csect);
    }

  csect_exit (NULL, CSECT_CONN_ACTIVE);
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

  job = css_make_job_entry (conn,
			    (CSS_THREAD_FN) css_connection_handler_thread,
			    (CSS_THREAD_ARG) conn,
			    -1 /* implicit: DEFAULT */ );
  assert (job != NULL);

  if (job != NULL)
    {
      css_add_to_job_queue (job);
    }

  return 1;
}

/*
 * css_internal_request_handler() -
 *   return:
 *   arg(in):
 *
 * Note: This routine is "registered" to be called when a new request is
 *       initiated by the client.
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
  assert (conn != NULL);

  rc = css_receive_request (conn, &rid, &request, &size);
  if (rc == NO_ERRORS)
    {
      /* 1. change thread's transaction id to this connection's */
      thread_p->tran_index = conn->transaction_id;

      pthread_mutex_unlock (&thread_p->tran_index_lock);

      if (size)
	{
	  rc = css_receive_data (conn, rid, &buffer, &size, -1);
	  if (rc != NO_ERRORS)
	    {
	      return status;
	    }
	}

      conn->db_error = 0;	/* This will reset the error indicator */

      eid = css_return_eid_from_conn (conn, rid);
      /* 2. change thread's client, rid, tran_index for this request */
      thread_set_info (thread_p, conn->client_id, eid, conn->transaction_id);

      /* 3. Call server_request() function */
      status = (*css_Server_request_handler) (thread_p, eid, request,
					      size, buffer);

      /* 4. reset thread transaction id(may be NULL_TRAN_INDEX) */
      thread_set_info (thread_p, -1, 0, local_tran_index);
    }
  else
    {
      pthread_mutex_unlock (&thread_p->tran_index_lock);

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
  THREAD_ENTRY *thread_p;
  CSS_CONN_ENTRY *conn;
  int status = ER_FAILED;

  if (server_name == NULL || port_id <= 0)
    {
      return ER_FAILED;
    }

#if defined(WINDOWS)
  if (css_windows_startup () < 0)
    {
      fprintf (stderr, "Winsock startup error\n");
      return ER_FAILED;
    }
#endif /* WINDOWS */

  /* startup worker/daemon threads */
  status = thread_start_workers ();
  if (status != NO_ERROR)
    {
      if (status == ER_CSS_PTHREAD_CREATE)
	{
	  /* thread creation error */
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_THREAD_STACK,
		  1, NUM_NORMAL_TRANS);
	}
      return ER_FAILED;
    }

  css_Server_connection_socket = INVALID_SOCKET;

  conn = css_connect_to_master_server (port_id, server_name, name_length);
  if (conn != NULL)
    {
      /* insert conn into active conn list */
      css_insert_into_active_conn_list (conn);

      css_Master_server_name = strdup (server_name);
      css_Master_port_id = port_id;
      css_Pipe_to_master = conn->fd;
      css_Master_conn = conn;

#if !defined(WINDOWS)
      if (prm_get_integer_value (PRM_ID_HA_MODE) != HA_MODE_OFF)
	{
	  status = hb_register_to_master (css_Master_conn, HB_PTYPE_SERVER);
	  if (status != NO_ERROR)
	    {
	      fprintf (stderr, "failed to heartbeat register.\n");
	      goto shutdown;
	    }
	}
#endif

      css_setup_server_loop ();

      status = NO_ERROR;
    }

  /*
   * start to shutdown server
   */
#if !defined(WINDOWS)
shutdown:
#endif

  /* stop threads */
  thread_stop_active_workers (THREAD_STOP_WORKERS_EXCEPT_LOGWR);

  /* we should flush all append pages before stop log writer */
  thread_p = thread_get_thread_entry_info ();
  assert_release (thread_p != NULL);

  LOG_CS_ENTER (thread_p);
  logpb_flush_pages_direct (thread_p);

#if !defined(NDEBUG)
  pthread_mutex_lock (&log_Gl.prior_info.prior_lsa_mutex);
  if (!LSA_EQ (&log_Gl.append.nxio_lsa, &log_Gl.prior_info.prior_lsa))
    {
      LOG_PRIOR_NODE *node;

      assert (LSA_LT (&log_Gl.append.nxio_lsa, &log_Gl.prior_info.prior_lsa));
      node = log_Gl.prior_info.prior_list_header;
      while (node != NULL)
	{
	  /* TODO: This is temporary disabled, because vacuum workers are
	   *	   still active and they are currently considered active
	   *	   transaction.
	   *	   Add assert back after changing vacuum workers to system
	   *	   transactions.
	   */
#if 0
	  assert (node->log_header.trid == LOG_SYSTEM_TRANID);
#endif
	  node = node->next;
	}
    }
  pthread_mutex_unlock (&log_Gl.prior_info.prior_lsa_mutex);
#endif

  LOG_CS_EXIT (thread_p);

  thread_stop_active_workers (THREAD_STOP_LOGWR);

  if (prm_get_integer_value (PRM_ID_HA_MODE) != HA_MODE_OFF)
    {
      css_close_connection_to_master ();
    }

  css_close_server_connection_socket ();

  if (css_Master_server_name)
    {
      free_and_init (css_Master_server_name);
    }

  /* If this was opened for the new style connection protocol, make sure
   * it gets closed.
   */
  css_close_server_connection_socket ();
#if defined(WINDOWS)
  css_windows_shutdown ();
#endif /* WINDOWS */

  return status;
}

#if defined (ENABLE_UNUSED_FUNCTION)
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
#endif /* ENABLE_UNUSED_FUNCTION */

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
css_send_data_to_client (CSS_CONN_ENTRY * conn, unsigned int eid,
			 char *buffer, int buffer_size)
{
  int rc = 0;

  assert (conn != NULL);

  rc = css_send_data (conn, CSS_RID_FROM_EID (eid), buffer, buffer_size);
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
css_send_reply_and_data_to_client (CSS_CONN_ENTRY * conn, unsigned int eid,
				   char *reply, int reply_size, char *buffer,
				   int buffer_size)
{
  int rc = 0;

  assert (conn != NULL);

  if (buffer_size > 0 && buffer != NULL)
    {
      rc = css_send_two_data (conn, CSS_RID_FROM_EID (eid),
			      reply, reply_size, buffer, buffer_size);
    }
  else
    {
      rc = css_send_data (conn, CSS_RID_FROM_EID (eid), reply, reply_size);
    }

  return (rc == NO_ERRORS) ? NO_ERROR : rc;
}

#if 0
/*
 * css_send_reply_and_large_data_to_client() - send a reply to the server,
 *                                       and optionaly, an additional l
 *                                       large data
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
css_send_reply_and_large_data_to_client (unsigned int eid, char *reply,
					 int reply_size, char *buffer,
					 INT64 buffer_size)
{
  CSS_CONN_ENTRY *conn;
  int rc = 0;
  int idx = CSS_ENTRYID_FROM_EID (eid);
  int num_buffers;
  char **buffers;
  int *buffers_size, i;
  INT64 pos = 0;

  conn = &css_Conn_array[idx];
  if (buffer_size > 0 && buffer != NULL)
    {
      num_buffers = (int) (buffer_size / INT_MAX) + 2;

      buffers = (char **) malloc (sizeof (char *) * num_buffers);
      if (buffers == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
		  1, sizeof (char *) * num_buffers);
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}

      buffers_size = (int *) malloc (sizeof (int) * num_buffers);
      if (buffers_size == NULL)
	{
	  free (buffers);
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
		  1, sizeof (int) * num_buffers);
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}

      buffers[0] = reply;
      buffers_size[0] = reply_size;

      for (i = 1; i < num_buffers; i++)
	{
	  buffers[i] = &buffer[pos];
	  if (buffer_size > INT_MAX)
	    {
	      buffers_size[i] = INT_MAX;
	    }
	  else
	    {
	      buffers_size[i] = buffer_size;
	    }
	  pos += buffers_size[i];
	}

      rc = css_send_large_data (conn, CSS_RID_FROM_EID (eid),
				(const char **) buffers, buffers_size,
				num_buffers);

      free (buffers);
      free (buffers_size);
    }
  else
    {
      rc = css_send_data (conn, CSS_RID_FROM_EID (eid), reply, reply_size);
    }

  return (rc == NO_ERRORS) ? NO_ERROR : rc;
}
#endif

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
css_send_reply_and_2_data_to_client (CSS_CONN_ENTRY * conn, unsigned int eid,
				     char *reply, int reply_size,
				     char *buffer1, int buffer1_size,
				     char *buffer2, int buffer2_size)
{
  int rc = 0;

  assert (conn != NULL);

  if (buffer2 == NULL || buffer2_size <= 0)
    {
      return (css_send_reply_and_data_to_client (conn,
						 eid, reply, reply_size,
						 buffer1, buffer1_size));
    }
  rc = css_send_three_data (conn, CSS_RID_FROM_EID (eid),
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
css_send_reply_and_3_data_to_client (CSS_CONN_ENTRY * conn, unsigned int eid,
				     char *reply, int reply_size,
				     char *buffer1, int buffer1_size,
				     char *buffer2, int buffer2_size,
				     char *buffer3, int buffer3_size)
{
  int rc = 0;

  assert (conn != NULL);

  if (buffer3 == NULL || buffer3_size <= 0)
    {
      return (css_send_reply_and_2_data_to_client
	      (conn, eid, reply, reply_size, buffer1, buffer1_size, buffer2,
	       buffer2_size));
    }
  rc = css_send_four_data (conn, CSS_RID_FROM_EID (eid),
			   reply, reply_size, buffer1, buffer1_size,
			   buffer2, buffer2_size, buffer3, buffer3_size);

  return (rc == NO_ERRORS) ? 0 : rc;
}

/*
 * css_send_error_to_client() - send an error buffer to the server
 *   return:
 *   conn(in): connection entry
 *   eid(in): enquiry id
 *   buffer(in): data buffer to queue for expected data.
 *   buffer_size(in): size of data buffer
 *
 * Note: This is to be used ONLY by the server to return error data to the
 *       client.
 */
unsigned int
css_send_error_to_client (CSS_CONN_ENTRY * conn, unsigned int eid,
			  char *buffer, int buffer_size)
{
  int rc;

  assert (conn != NULL);

  rc = css_send_error (conn, CSS_RID_FROM_EID (eid), buffer, buffer_size);

  return (rc == NO_ERRORS) ? 0 : rc;
}

/*
 * css_send_abort_to_client() - send an abort message to the client
 *   return:
 *   eid(in): enquiry id
 */
unsigned int
css_send_abort_to_client (CSS_CONN_ENTRY * conn, unsigned int eid)
{
  int rc = 0;

  assert (conn != NULL);

  rc = css_send_abort_request (conn, CSS_RID_FROM_EID (eid));

  return (rc == NO_ERRORS) ? 0 : rc;
}

/*
 * css_test_for_client_errors () -
 *   return: error id from the client
 *   conn(in):
 *   eid(in):
 */
static int
css_test_for_client_errors (CSS_CONN_ENTRY * conn, unsigned int eid)
{
  char *error_buffer;
  int error_size, rc, errid = NO_ERROR;

  assert (conn != NULL);

  if (css_return_queued_error (conn, CSS_RID_FROM_EID (eid),
			       &error_buffer, &error_size, &rc))
    {
      errid = er_set_area_error ((void *) error_buffer);
      free_and_init (error_buffer);
    }
  return errid;
}

/*
 * css_receive_data_from_client() - return data that was sent by the server
 *   return:
 *   eid(in): enquiry id
 *   buffer(out): data buffer to send to client.
 *   buffer_size(out): size of data buffer
 *
 *   note: caller should know that it returns zero on success and
 *   returns css error code on failure
 */
unsigned int
css_receive_data_from_client (CSS_CONN_ENTRY * conn, unsigned int eid,
			      char **buffer, int *size)
{
  return css_receive_data_from_client_with_timeout (conn, eid, buffer, size,
						    -1);
}

/*
 * css_receive_data_from_client_with_timeout() - return data that was sent by the server
 *   return:
 *   eid(in): enquiry id
 *   buffer(out): data buffer to send to client.
 *   buffer_size(out): size of data buffer
 *   timeout(in): timeout in seconds
 *
 *   note: caller should know that it returns zero on success and
 *   returns css error code on failure
 */
unsigned int
css_receive_data_from_client_with_timeout (CSS_CONN_ENTRY * conn,
					   unsigned int eid, char **buffer,
					   int *size, int timeout)
{
  int rc = 0;

  assert (conn != NULL);

  *size = 0;

  rc = css_receive_data (conn, CSS_RID_FROM_EID (eid), buffer, size, timeout);

  if (rc == NO_ERRORS || rc == RECORD_TRUNCATED)
    {
      css_test_for_client_errors (conn, eid);
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
#if defined(SERVER_MODE)
  assert (conn->csect.cs_index == CRITICAL_SECTION_COUNT + conn->idx);
  assert (conn->csect.name == css_Csect_name_conn);
#endif

  csect_enter_critical_section (NULL, &conn->csect, INF_WAIT);

  css_remove_all_unexpected_packets (conn);
  conn->status = CONN_CLOSING;

#if defined(SERVER_MODE)
  assert (conn->csect.cs_index == CRITICAL_SECTION_COUNT + conn->idx);
  assert (conn->csect.name == css_Csect_name_conn);
#endif

  csect_exit_critical_section (NULL, &conn->csect);
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
  char pid_string[16], *s;
  const char *t;

  if (server_name != NULL)
    {
      env_name = envvar_root ();
      if (env_name == NULL)
	{
	  return NULL;
	}

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

      /* in order to prepend '#' */
      if (prm_get_integer_value (PRM_ID_HA_MODE) != HA_MODE_OFF)
	{
	  (*name_length)++;
	}

      packed_name = (char *) malloc (*name_length);
      if (packed_name == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
		  1, *name_length);
	  return NULL;
	}

      s = packed_name;
      t = server_name;

      if (prm_get_integer_value (PRM_ID_HA_MODE) != HA_MODE_OFF)
	{
	  *s++ = '#';
	}

      while (*t)
	{
	  *s++ = *t++;
	}
      *s++ = '\0';

      t = rel_major_release_string ();
      while (*t)
	{
	  *s++ = *t++;
	}
      *s++ = '\0';

      t = env_name;
      while (*t)
	{
	  *s++ = *t++;
	}
      *s++ = '\0';

      t = pid_string;
      while (*t)
	{
	  *s++ = *t++;
	}
      *s++ = '\0';
    }
  return packed_name;
}

/*
 * css_add_client_version_string() - add the version_string to socket queue
 *                                   entry structure
 *   return: pointer to version_string in the socket queue entry structure
 *   version_string(in):
 */
char *
css_add_client_version_string (THREAD_ENTRY * thread_p,
			       const char *version_string)
{
  char *ver_str = NULL;
  CSS_CONN_ENTRY *conn;

  assert (thread_p != NULL);

  conn = thread_p->conn_entry;
  if (conn != NULL)
    {
      if (conn->version_string == NULL)
	{
	  ver_str = (char *) malloc (strlen (version_string) + 1);
	  if (ver_str != NULL)
	    {
	      strcpy (ver_str, version_string);
	      conn->version_string = ver_str;
	    }
	  else
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_OUT_OF_VIRTUAL_MEMORY, 1,
		      strlen (version_string) + 1);
	    }
	}
      else
	{
	  /* already registered */
	  ver_str = conn->version_string;
	}
    }

  return ver_str;
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * css_get_client_version_string() - retrieve the version_string from socket
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
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * css_cleanup_server_queues () -
 *   return:
 *   eid(in):
 */
void
css_cleanup_server_queues (unsigned int eid)
{
  int idx = CSS_ENTRYID_FROM_EID (eid);

  css_remove_all_unexpected_packets (&css_Conn_array[idx]);
}

#if defined (ENABLE_UNUSED_FUNCTION)
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

  csect_enter_as_reader (NULL, CSECT_CONN_ACTIVE, INF_WAIT);

  for (conn = css_Active_conn_anchor; conn != NULL; conn = conn->next)
    {
      if (conn != css_Master_conn)
	{
	  n++;
	}
    }

  csect_exit (NULL, CSECT_CONN_ACTIVE);

  return n;
}
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * css_wait_worker_thread_on_jobq () -
 *   return:
 *   thrd(in):
 *   jobq_index(in):
 */
static int
css_wait_worker_thread_on_jobq (THREAD_ENTRY * thrd, int jobq_index)
{
#if defined(DEBUG)
  THREAD_ENTRY *t;
#endif /* DEBUG */

#if defined(DEBUG)
  /* to detect whether the job queue has a cycle or not */
  t = css_Job_queue[jobq_index].worker_thrd_list;
  while (t)
    {
      assert (t != thrd);
      t = t->worker_thrd_list;
    }
#endif /* DEBUG */

  /* add thrd at the front of job queue */
  thrd->worker_thrd_list = css_Job_queue[jobq_index].worker_thrd_list;
  css_Job_queue[jobq_index].worker_thrd_list = thrd;

  thrd->resume_status = THREAD_JOB_QUEUE_SUSPENDED;

  /* sleep on the thrd's condition variable with the mutex of the job queue */
  pthread_cond_wait (&thrd->wakeup_cond, &css_Job_queue[jobq_index].job_lock);

  if (thrd->shutdown == false
      && thrd->resume_status != THREAD_JOB_QUEUE_RESUMED)
    {
      char notimsg[LINE_MAX];
      THREAD_ENTRY *current, *prev = NULL;

      /* something is wrong!!! My entry in the job queue must be removed. */
      assert (thrd->resume_status == THREAD_JOB_QUEUE_RESUMED);

      snprintf (notimsg, LINE_MAX, "Thread %p's resume status is"
		" not JOB QUEUE RESUMED but %d (job Q index : %d)",
		thrd, thrd->resume_status, jobq_index);
      er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE,
	      ER_EMERGENCY_ERROR, 1, notimsg);

      current = css_Job_queue[jobq_index].worker_thrd_list;
      while (current)
	{
	  if (current == thrd)
	    {
	      if (prev == NULL)
		{
		  css_Job_queue[jobq_index].worker_thrd_list =
		    thrd->worker_thrd_list;
		}
	      else
		{
		  prev->worker_thrd_list = thrd->worker_thrd_list;
		}
	      thrd->worker_thrd_list = NULL;
	      break;
	    }
	  else
	    {
	      prev = current;
	    }

	  current = current->worker_thrd_list;
	}
    }

  if (thrd->resume_status == THREAD_RESUME_DUE_TO_SHUTDOWN
      || thrd->resume_status == THREAD_RESUME_DUE_TO_INTERRUPT)
    {
      assert_release (thrd->shutdown == true);
      return ER_FAILED;
    }

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
  THREAD_ENTRY *worker = NULL;

  for (wait_thrd = css_Job_queue[jobq_index].worker_thrd_list;
       wait_thrd; wait_thrd = wait_thrd->worker_thrd_list)
    {
      /* wakeup a free worker thread */
      if (wait_thrd->status == TS_FREE)
	{
	  r = thread_wakeup (wait_thrd, THREAD_JOB_QUEUE_RESUMED);
	  if (r == NO_ERROR)
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
	      worker = wait_thrd;

	      break;
	    }
	}

      prev_thrd = wait_thrd;
    }

  if (worker == NULL)
    {
      int thread_index, num_workers;

      /* Defence code of a bug.
       * Find an available worker thread
       * that is not in the list of job queue,
       * but is waiting for a job.
       */
      thread_index = jobq_index;
      if (thread_index == 0)
	{
	  thread_index += CSS_NUM_JOB_QUEUE;
	}

      num_workers = thread_num_worker_threads ();

      for (; thread_index <= num_workers; thread_index += CSS_NUM_JOB_QUEUE)
	{
	  THREAD_ENTRY *thread_p;

	  thread_p = thread_find_entry_by_index (thread_index);
	  assert_release (thread_p->index % CSS_NUM_JOB_QUEUE == jobq_index);

	  if (thread_p->resume_status == THREAD_JOB_QUEUE_SUSPENDED
	      && thread_p->status == TS_FREE)
	    {
	      r = thread_wakeup (thread_p, THREAD_JOB_QUEUE_RESUMED);
	      if (r == NO_ERROR)
		{
		  char notimsg[LINE_MAX];

		  snprintf (notimsg, LINE_MAX, "Wakes up thread %p. "
			    "It was suspended in the job queue (index : %d), "
			    "but was not in the joq Q's worker thread list",
			    thread_p, jobq_index);

		  er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE,
			  ER_EMERGENCY_ERROR, 1, notimsg);
		  thread_p->worker_thrd_list = NULL;
		  break;
		}
	    }
	}
    }

  return r;
}

/*
 * css_can_occupy_worker_thread_on_jobq () -
 *   return:
 *   jobq_index(in):
 *
 * NOTE: The caller should hold mutex on css_Job_queue[jobq_index]
 *       to call this function.
 */
static bool
css_can_occupy_worker_thread_on_jobq (int jobq_index)
{
  THREAD_ENTRY *wait_thrd;

  for (wait_thrd = css_Job_queue[jobq_index].worker_thrd_list;
       wait_thrd; wait_thrd = wait_thrd->worker_thrd_list)
    {
      if (wait_thrd->status == TS_FREE)
	{
	  return true;
	}
    }

  return false;
}

/*
 * css_set_ha_num_of_hosts -
 *   return: none
 *
 * Note: be careful to use
 */
void
css_set_ha_num_of_hosts (int num)
{
  if (num < 1)
    {
      num = 1;
    }
  if (num > HA_LOG_APPLIER_STATE_TABLE_MAX)
    {
      num = HA_LOG_APPLIER_STATE_TABLE_MAX;
    }
  ha_Server_num_of_hosts = num - 1;
}

/*
 * css_get_ha_num_of_hosts -
 *   return: return the number of hosts
 *
 * Note:
 */
int
css_get_ha_num_of_hosts (void)
{
  return ha_Server_num_of_hosts;
}

/*
 * css_ha_server_state - return the current HA server state
 *   return: one of HA_SERVER_STATE
 */
HA_SERVER_STATE
css_ha_server_state (void)
{
  return ha_Server_state;
}

bool
css_is_ha_repl_delayed (void)
{
  return ha_Repl_delay_detected;
}

void
css_set_ha_repl_delayed (void)
{
  ha_Repl_delay_detected = true;
}

void
css_unset_ha_repl_delayed (void)
{
  ha_Repl_delay_detected = false;
}

/*
 * css_transit_ha_server_state - request to transit the current HA server
 *                               state to the required state
 *   return: new state changed if successful or HA_SERVER_STATE_NA
 *   req_state(in): the state for the server to transit
 *
 */
static HA_SERVER_STATE
css_transit_ha_server_state (THREAD_ENTRY * thread_p,
			     HA_SERVER_STATE req_state)
{
  struct ha_server_state_transition_table
  {
    HA_SERVER_STATE cur_state;
    HA_SERVER_STATE req_state;
    HA_SERVER_STATE next_state;
  };
  static struct ha_server_state_transition_table
    ha_Server_state_transition[] = {
    /* idle -> active */
    {HA_SERVER_STATE_IDLE, HA_SERVER_STATE_ACTIVE, HA_SERVER_STATE_ACTIVE},
#if 0
    /* idle -> to-be-standby */
    {HA_SERVER_STATE_IDLE, HA_SERVER_STATE_STANDBY,
     HA_SERVER_STATE_TO_BE_STANDBY},
#else
    /* idle -> standby */
    {HA_SERVER_STATE_IDLE, HA_SERVER_STATE_STANDBY,
     HA_SERVER_STATE_STANDBY},
#endif
    /* idle -> maintenance */
    {HA_SERVER_STATE_IDLE, HA_SERVER_STATE_MAINTENANCE,
     HA_SERVER_STATE_MAINTENANCE},
    /* active -> active */
    {HA_SERVER_STATE_ACTIVE, HA_SERVER_STATE_ACTIVE, HA_SERVER_STATE_ACTIVE},
    /* active -> to-be-standby */
    {HA_SERVER_STATE_ACTIVE, HA_SERVER_STATE_STANDBY,
     HA_SERVER_STATE_TO_BE_STANDBY},
    /* to-be-active -> active */
    {HA_SERVER_STATE_TO_BE_ACTIVE, HA_SERVER_STATE_ACTIVE,
     HA_SERVER_STATE_ACTIVE},
    /* standby -> standby */
    {HA_SERVER_STATE_STANDBY, HA_SERVER_STATE_STANDBY,
     HA_SERVER_STATE_STANDBY},
    /* standby -> to-be-active */
    {HA_SERVER_STATE_STANDBY, HA_SERVER_STATE_ACTIVE,
     HA_SERVER_STATE_TO_BE_ACTIVE},
    /* statndby -> maintenance */
    {HA_SERVER_STATE_STANDBY, HA_SERVER_STATE_MAINTENANCE,
     HA_SERVER_STATE_MAINTENANCE},
    /* to-be-standby -> standby */
    {HA_SERVER_STATE_TO_BE_STANDBY, HA_SERVER_STATE_STANDBY,
     HA_SERVER_STATE_STANDBY},
    /* maintenance -> standby */
    {HA_SERVER_STATE_MAINTENANCE, HA_SERVER_STATE_STANDBY,
     HA_SERVER_STATE_TO_BE_STANDBY},
    /* end of table */
    {HA_SERVER_STATE_NA, HA_SERVER_STATE_NA, HA_SERVER_STATE_NA}
  };
  struct ha_server_state_transition_table *table;
  HA_SERVER_STATE new_state = HA_SERVER_STATE_NA;

  if (ha_Server_state == req_state)
    {
      return req_state;
    }

  csect_enter (thread_p, CSECT_HA_SERVER_STATE, INF_WAIT);

  for (table = ha_Server_state_transition;
       table->cur_state != HA_SERVER_STATE_NA; table++)
    {
      if (table->cur_state == ha_Server_state
	  && table->req_state == req_state)
	{
	  er_log_debug (ARG_FILE_LINE, "css_transit_ha_server_state: "
			"ha_Server_state (%s) -> (%s)\n",
			css_ha_server_state_string (ha_Server_state),
			css_ha_server_state_string (table->next_state));
	  new_state = table->next_state;
	  /* append a dummy log record for LFT to wake LWTs up */
	  log_append_ha_server_state (thread_p, new_state);
	  if (prm_get_integer_value (PRM_ID_HA_MODE) != HA_MODE_OFF)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_CSS_SERVER_HA_MODE_CHANGE, 2,
		      css_ha_server_state_string (ha_Server_state),
		      css_ha_server_state_string (new_state));
	    }
	  ha_Server_state = new_state;
	  /* sync up the current HA state with the system parameter */
	  prm_set_integer_value (PRM_ID_HA_SERVER_STATE, ha_Server_state);
	  break;
	}
    }

  csect_exit (thread_p, CSECT_HA_SERVER_STATE);
  return new_state;
}

/*
 * css_check_ha_server_state_for_client
 *   return: NO_ERROR or errno
 *   whence(in): 0: others, 1: register_client, 2: unregister_client
 */
int
css_check_ha_server_state_for_client (THREAD_ENTRY * thread_p, int whence)
{
#define FROM_OTHERS             0
#define FROM_REGISTER_CLIENT    1
#define FROM_UNREGISTER_CLIENT  2
  int err = NO_ERROR;
  HA_SERVER_STATE state;

  /* csect_enter (thread_p, CSECT_HA_SERVER_STATE, INF_WAIT); */

  switch (ha_Server_state)
    {
    case HA_SERVER_STATE_TO_BE_ACTIVE:
      /* Server accepts clients even though it is in a to-be-active state */
      break;

    case HA_SERVER_STATE_TO_BE_STANDBY:
      /*
       * If the server's state is 'to-be-standby',
       * new connection request will be rejected for HA fail-back action.
       */
      if (whence == FROM_REGISTER_CLIENT)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ERR_CSS_ERROR_FROM_SERVER,
		  1, "Connection rejected. "
		  "The server is changing to standby mode.");
	  err = ERR_CSS_ERROR_FROM_SERVER;
	}
      /*
       * If all connected clients are released (by reset-on-commit),
       * change the state to 'standby' as a completion of HA fail-back action.
       */
      else if (whence == FROM_UNREGISTER_CLIENT)
	{
	  if (logtb_count_clients (thread_p) == 1)
	    {
	      er_log_debug (ARG_FILE_LINE,
			    "logtb_count_clients () = 1 including me "
			    "transit state from 'to-be-standby' to 'standby'\n");
	      state = css_transit_ha_server_state (thread_p,
						   HA_SERVER_STATE_STANDBY);
	      assert (state == HA_SERVER_STATE_STANDBY);
	      if (state == HA_SERVER_STATE_STANDBY)
		{
		  er_log_debug (ARG_FILE_LINE,
				"css_check_ha_server_state_for_client: "
				"logtb_disable_update() \n");
		  logtb_disable_update (thread_p);
		}
	    }
	}
      break;

    default:
      break;
    }

  /* csect_exit (CSECT_HA_SERVER_STATE); */
  return err;
}

/*
 * css_check_ha_log_applier_done - check all log appliers have done
 *   return: true or false
 */
static bool
css_check_ha_log_applier_done (void)
{
  int i;

  for (i = 0; i < ha_Server_num_of_hosts; i++)
    {
      if (ha_Log_applier_state[i].state != HA_LOG_APPLIER_STATE_DONE)
	{
	  break;
	}
    }
  if (i == ha_Server_num_of_hosts
      && (ha_Server_state == HA_SERVER_STATE_TO_BE_ACTIVE
	  || ha_Server_state == HA_SERVER_STATE_ACTIVE))
    {
      return true;
    }
  return false;
}

/*
 * css_check_ha_log_applier_working - check all log appliers are working
 *   return: true or false
 */
static bool
css_check_ha_log_applier_working (void)
{
  int i;

  for (i = 0; i < ha_Server_num_of_hosts; i++)
    {
      if (ha_Log_applier_state[i].state != HA_LOG_APPLIER_STATE_WORKING
	  || ha_Log_applier_state[i].state != HA_LOG_APPLIER_STATE_DONE)
	{
	  break;
	}
    }
  if (i == ha_Server_num_of_hosts
      && (ha_Server_state == HA_SERVER_STATE_TO_BE_STANDBY
	  || ha_Server_state == HA_SERVER_STATE_STANDBY))
    {
      return true;
    }
  return false;
}

/*
 * css_change_ha_server_state - change the server's HA state
 *   return: NO_ERROR or ER_FAILED
 *   state(in): new state for server to be
 *   force(in): force to change
 *   timeout(in): timeout (standby to maintenance)
 *   heartbeat(in): from heartbeat master
 */
int
css_change_ha_server_state (THREAD_ENTRY * thread_p, HA_SERVER_STATE state,
			    bool force, int timeout, bool heartbeat)
{
  HA_SERVER_STATE orig_state;
  int i;

  er_log_debug (ARG_FILE_LINE,
		"css_change_ha_server_state: ha_Server_state %s "
		"state %s force %c heartbeat %c\n",
		css_ha_server_state_string (ha_Server_state),
		css_ha_server_state_string (state), (force ? 't' : 'f'),
		(heartbeat ? 't' : 'f'));

  assert (state >= HA_SERVER_STATE_IDLE && state <= HA_SERVER_STATE_DEAD);

  if (state == ha_Server_state
      || (!force && ha_Server_state == HA_SERVER_STATE_TO_BE_ACTIVE
	  && state == HA_SERVER_STATE_ACTIVE)
      || (!force && ha_Server_state == HA_SERVER_STATE_TO_BE_STANDBY
	  && state == HA_SERVER_STATE_STANDBY))
    {
      return NO_ERROR;
    }

  if (heartbeat == false
      && !(ha_Server_state == HA_SERVER_STATE_STANDBY
	   && state == HA_SERVER_STATE_MAINTENANCE)
      && !(ha_Server_state == HA_SERVER_STATE_MAINTENANCE
	   && state == HA_SERVER_STATE_STANDBY)
      && !(force && ha_Server_state == HA_SERVER_STATE_TO_BE_ACTIVE
	   && state == HA_SERVER_STATE_ACTIVE))
    {
      return NO_ERROR;
    }

  csect_enter (thread_p, CSECT_HA_SERVER_STATE, INF_WAIT);

  orig_state = ha_Server_state;

  if (force)
    {
      if (ha_Server_state != state)
	{
	  er_log_debug (ARG_FILE_LINE, "css_change_ha_server_state:"
			" set force from %s to state %s\n",
			css_ha_server_state_string (ha_Server_state),
			css_ha_server_state_string (state));
	  ha_Server_state = state;
	  /* append a dummy log record for LFT to wake LWTs up */
	  log_append_ha_server_state (thread_p, state);
	  if (prm_get_integer_value (PRM_ID_HA_MODE) != HA_MODE_OFF)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_CSS_SERVER_HA_MODE_CHANGE, 2,
		      css_ha_server_state_string (ha_Server_state),
		      css_ha_server_state_string (state));
	    }
	}
    }

  switch (state)
    {
    case HA_SERVER_STATE_ACTIVE:
      state = css_transit_ha_server_state (thread_p, HA_SERVER_STATE_ACTIVE);
      if (state == HA_SERVER_STATE_NA)
	{
	  break;
	}
      /* If log appliers have changed their state to done,
       * go directly to active mode */
      if (css_check_ha_log_applier_done ())
	{
	  er_log_debug (ARG_FILE_LINE, "css_change_ha_server_state: "
			"css_check_ha_log_applier_done ()\n");
	  state =
	    css_transit_ha_server_state (thread_p, HA_SERVER_STATE_ACTIVE);
	  assert (state == HA_SERVER_STATE_ACTIVE);
	}
      if (state == HA_SERVER_STATE_ACTIVE)
	{
	  er_log_debug (ARG_FILE_LINE, "css_change_ha_server_state: "
			"logtb_enable_update() \n");
	  logtb_enable_update (thread_p);
	}
      break;

    case HA_SERVER_STATE_STANDBY:
      state = css_transit_ha_server_state (thread_p, HA_SERVER_STATE_STANDBY);
      if (state == HA_SERVER_STATE_NA)
	{
	  break;
	}
      if (orig_state == HA_SERVER_STATE_IDLE)
	{
	  /* If all log appliers have done their recovering actions,
	   * go directly to standby mode */
	  if (css_check_ha_log_applier_working ())
	    {
	      er_log_debug (ARG_FILE_LINE, "css_change_ha_server_state: "
			    "css_check_ha_log_applier_working ()\n");
	      state =
		css_transit_ha_server_state (thread_p,
					     HA_SERVER_STATE_STANDBY);
	      assert (state == HA_SERVER_STATE_STANDBY);
	    }
	}
      else
	{
	  /* If there's no active clients (except me),
	   * go directly to standby mode */
	  if (logtb_count_clients (thread_p) == 0)
	    {
	      er_log_debug (ARG_FILE_LINE, "css_change_ha_server_state: "
			    "logtb_count_clients () = 0\n");
	      state =
		css_transit_ha_server_state (thread_p,
					     HA_SERVER_STATE_STANDBY);
	      assert (state == HA_SERVER_STATE_STANDBY);
	    }
	}
      if (orig_state == HA_SERVER_STATE_MAINTENANCE)
	{
	  boot_server_status (BOOT_SERVER_UP);
	}
      if (state == HA_SERVER_STATE_STANDBY)
	{
	  er_log_debug (ARG_FILE_LINE, "css_change_ha_server_state: "
			"logtb_disable_update() \n");
	  logtb_disable_update (thread_p);
	}
      break;

    case HA_SERVER_STATE_MAINTENANCE:
      state =
	css_transit_ha_server_state (thread_p, HA_SERVER_STATE_MAINTENANCE);
      if (state == HA_SERVER_STATE_NA)
	{
	  break;
	}

      if (state == HA_SERVER_STATE_MAINTENANCE)
	{
	  er_log_debug (ARG_FILE_LINE, "css_change_ha_server_state: "
			"logtb_enable_update() \n");
	  logtb_enable_update (thread_p);

	  boot_server_status (BOOT_SERVER_MAINTENANCE);
	}

      for (i = 0; i < timeout; i++)
	{
	  /* waiting timeout second while transaction terminated normally. */
	  if (logtb_count_not_allowed_clients_in_maintenance_mode (thread_p)
	      == 0)
	    {
	      break;
	    }
	  thread_sleep (1000);	/* 1000 msec */
	}

      if (logtb_count_not_allowed_clients_in_maintenance_mode (thread_p) != 0)
	{
	  LOG_TDES *tdes;

	  /* try to kill transaction. */
	  TR_TABLE_CS_ENTER (thread_p);
	  for (i = 0; i < log_Gl.trantable.num_total_indices; i++)
	    {
	      tdes = log_Gl.trantable.all_tdes[i];
	      if (tdes != NULL && tdes->trid != NULL_TRANID)
		{
		  if (!BOOT_IS_ALLOWED_CLIENT_TYPE_IN_MT_MODE
		      (tdes->client.host_name, boot_Host_name,
		       tdes->client.client_type))
		    {
		      thread_slam_tran_index (thread_p, tdes->tran_index);
		    }
		}
	    }
	  TR_TABLE_CS_EXIT (thread_p);

	  thread_sleep (2000);	/* 2000 msec */
	}
      break;

    default:
      state = HA_SERVER_STATE_NA;
      break;
    }

  csect_exit (thread_p, CSECT_HA_SERVER_STATE);

  return (state != HA_SERVER_STATE_NA) ? NO_ERROR : ER_FAILED;
}

/*
 * css_notify_ha_server_mode - notify the log applier's HA state
 *   return: NO_ERROR or ER_FAILED
 *   state(in): new state to be recorded
 */
int
css_notify_ha_log_applier_state (THREAD_ENTRY * thread_p,
				 HA_LOG_APPLIER_STATE state)
{
  HA_LOG_APPLIER_STATE_TABLE *table;
  HA_SERVER_STATE server_state;
  int i, client_id;

  assert (state >= HA_LOG_APPLIER_STATE_UNREGISTERED
	  && state <= HA_LOG_APPLIER_STATE_ERROR);

  csect_enter (thread_p, CSECT_HA_SERVER_STATE, INF_WAIT);

  client_id = thread_get_client_id (thread_p);
  er_log_debug (ARG_FILE_LINE,
		"css_notify_ha_log_applier_state: client %d state %s\n",
		client_id, css_ha_server_state_string (state));
  for (i = 0, table = ha_Log_applier_state;
       i < ha_Log_applier_state_num; i++, table++)
    {
      if (table->client_id == client_id)
	{
	  if (table->state == state)
	    {
	      csect_exit (thread_p, CSECT_HA_SERVER_STATE);
	      return NO_ERROR;
	    }
	  table->state = state;
	  break;
	}
      if (table->state == HA_LOG_APPLIER_STATE_UNREGISTERED)
	{
	  table->client_id = client_id;
	  table->state = state;
	  break;
	}
    }
  if (i == ha_Log_applier_state_num
      && ha_Log_applier_state_num < ha_Server_num_of_hosts)
    {
      table = &ha_Log_applier_state[ha_Log_applier_state_num++];
      table->client_id = client_id;
      table->state = state;
    }

  if (css_check_ha_log_applier_done ())
    {
      er_log_debug (ARG_FILE_LINE, "css_notify_ha_log_applier_state: "
		    "css_check_ha_log_applier_done()\n");
      server_state =
	css_transit_ha_server_state (thread_p, HA_SERVER_STATE_ACTIVE);
      assert (server_state == HA_SERVER_STATE_ACTIVE);
      if (server_state == HA_SERVER_STATE_ACTIVE)
	{
	  er_log_debug (ARG_FILE_LINE, "css_notify_ha_log_applier_state: "
			"logtb_enable_update() \n");
	  logtb_enable_update (thread_p);
	}
    }

  if (css_check_ha_log_applier_working ())
    {
      er_log_debug (ARG_FILE_LINE, "css_notify_ha_log_applier_state: "
		    "css_check_ha_log_applier_working()\n");
      server_state =
	css_transit_ha_server_state (thread_p, HA_SERVER_STATE_STANDBY);
      assert (server_state == HA_SERVER_STATE_STANDBY);
      if (server_state == HA_SERVER_STATE_STANDBY)
	{
	  er_log_debug (ARG_FILE_LINE, "css_notify_ha_log_applier_state: "
			"logtb_disable_update() \n");
	  logtb_disable_update (thread_p);
	}
    }

  csect_exit (thread_p, CSECT_HA_SERVER_STATE);
  return NO_ERROR;
}

#if defined(SERVER_MODE)
static int
css_check_accessibility (SOCKET new_fd)
{
#if defined(WINDOWS) || defined(SOLARIS)
  int saddr_len;
#elif defined(UNIXWARE7)
  size_t saddr_len;
#else
  socklen_t saddr_len;
#endif
  struct sockaddr_in clt_sock_addr;
  unsigned char *ip_addr;
  int err_code;

  saddr_len = sizeof (clt_sock_addr);

  if (getpeername (new_fd,
		   (struct sockaddr *) &clt_sock_addr, &saddr_len) != 0)
    {
      return ER_FAILED;
    }

  ip_addr = (unsigned char *) &(clt_sock_addr.sin_addr);

  if (clt_sock_addr.sin_family == AF_UNIX ||
      (ip_addr[0] == 127 && ip_addr[1] == 0 &&
       ip_addr[2] == 0 && ip_addr[3] == 1))
    {
      return NO_ERROR;
    }

  if (css_Server_accessible_ip_info == NULL)
    {
      char ip_str[32];

      sprintf (ip_str, "%d.%d.%d.%d", (unsigned char) ip_addr[0],
	       ip_addr[1], ip_addr[2], ip_addr[3]);

      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_INACCESSIBLE_IP, 1, ip_str);

      return ER_INACCESSIBLE_IP;
    }

  csect_enter_as_reader (NULL, CSECT_ACL, INF_WAIT);
  err_code = css_check_ip (css_Server_accessible_ip_info, ip_addr);
  csect_exit (NULL, CSECT_ACL);

  if (err_code != NO_ERROR)
    {
      char ip_str[32];

      sprintf (ip_str, "%d.%d.%d.%d", (unsigned char) ip_addr[0],
	       ip_addr[1], ip_addr[2], ip_addr[3]);

      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_INACCESSIBLE_IP, 1, ip_str);
    }

  return err_code;
}

int
css_set_accessible_ip_info ()
{
  int ret_val;
  IP_INFO *tmp_accessible_ip_info;

  if (prm_get_string_value (PRM_ID_ACCESS_IP_CONTROL_FILE) == NULL)
    {
      css_Server_accessible_ip_info = NULL;
      return NO_ERROR;
    }

#if defined (WINDOWS)
  if (strlen (prm_get_string_value (PRM_ID_ACCESS_IP_CONTROL_FILE)) > 2
      && isalpha (prm_get_string_value (PRM_ID_ACCESS_IP_CONTROL_FILE)[0])
      && prm_get_string_value (PRM_ID_ACCESS_IP_CONTROL_FILE)[1] == ':')
#else
  if (prm_get_string_value (PRM_ID_ACCESS_IP_CONTROL_FILE)[0] ==
      PATH_SEPARATOR)
#endif
    {
      ip_list_file_name =
	(char *) prm_get_string_value (PRM_ID_ACCESS_IP_CONTROL_FILE);
    }
  else
    {
      ip_list_file_name =
	envvar_confdir_file (ip_file_real_path, PATH_MAX,
			     prm_get_string_value
			     (PRM_ID_ACCESS_IP_CONTROL_FILE));
    }

  ret_val = css_read_ip_info (&tmp_accessible_ip_info, ip_list_file_name);
  if (ret_val == NO_ERROR)
    {
      csect_enter (NULL, CSECT_ACL, INF_WAIT);

      if (css_Server_accessible_ip_info != NULL)
	{
	  css_free_accessible_ip_info ();
	}
      css_Server_accessible_ip_info = tmp_accessible_ip_info;

      csect_exit (NULL, CSECT_ACL);
    }

  return ret_val;
}

int
css_free_accessible_ip_info ()
{
  int ret_val;

  ret_val = css_free_ip_info (css_Server_accessible_ip_info);
  css_Server_accessible_ip_info = NULL;

  return ret_val;
}

void
xacl_dump (THREAD_ENTRY * thread_p, FILE * outfp)
{
  int i, j;

  if (outfp == NULL)
    {
      outfp = stdout;
    }

  fprintf (outfp, "access_ip_control=%s\n",
	   (prm_get_bool_value (PRM_ID_ACCESS_IP_CONTROL) ? "yes" : "no"));
  fprintf (outfp, "access_ip_control_file=%s\n",
	   (ip_list_file_name != NULL) ? ip_list_file_name : "NULL");

  if (prm_get_bool_value (PRM_ID_ACCESS_IP_CONTROL) == false
      || css_Server_accessible_ip_info == NULL)
    {
      return;
    }

  csect_enter_as_reader (thread_p, CSECT_ACL, INF_WAIT);

  for (i = 0; i < css_Server_accessible_ip_info->num_list; i++)
    {
      int address_index = i * IP_BYTE_COUNT;

      for (j = 0;
	   j < css_Server_accessible_ip_info->
	   address_list[address_index]; j++)
	{
	  fprintf (outfp, "%d%s",
		   css_Server_accessible_ip_info->
		   address_list[address_index + j + 1],
		   ((j != 3) ? "." : ""));
	}
      if (j != 4)
	{
	  fprintf (outfp, "*");
	}
      fprintf (outfp, "\n");
    }

  fprintf (outfp, "\n");
  csect_exit (thread_p, CSECT_ACL);

  return;
}

int
xacl_reload (THREAD_ENTRY * thread_p)
{
  return css_set_accessible_ip_info ();
}
#endif
