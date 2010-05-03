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
 * master_heartbeat.c - heartbeat module in cub_master
 */

#ident "$Id$"


#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <errno.h>
#include <sys/wait.h>
#include <assert.h>

#if !defined(WINDOWS)
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#endif

#include "porting.h"
#include "tcp.h"
#include "object_representation.h"
#include "connection_cl.h"
#include "master_util.h"
#include "master_heartbeat.h"
#include "heartbeat.h"


/* externs */
extern void css_remove_entry_by_conn (CSS_CONN_ENTRY * conn_p,
				      SOCKET_QUEUE_ENTRY ** anchor_p);
extern void css_master_cleanup (int sig);
extern int css_Master_timeout_value_in_seconds;
extern int css_Master_timeout_value_in_microseconds;

extern SOCKET_QUEUE_ENTRY *css_add_request_to_socket_queue (CSS_CONN_ENTRY *
							    conn_p,
							    int info_p,
							    char *name_p,
							    SOCKET fd,
							    int fd_type,
							    int pid,
							    SOCKET_QUEUE_ENTRY
							    ** anchor_p);

extern SOCKET_QUEUE_ENTRY *css_return_entry_by_conn (CSS_CONN_ENTRY * conn_p,
						     SOCKET_QUEUE_ENTRY **
						     anchor_p);


extern SOCKET_QUEUE_ENTRY *css_Master_socket_anchor;
#if !defined(WINDOWS)
extern MUTEX_T css_Master_socket_anchor_lock;
#endif

/* list */
static void hb_list_add (HB_LIST ** p, HB_LIST * n);
static void hb_list_remove (HB_LIST * n);
static void hb_list_move (HB_LIST ** dest_pp, HB_LIST ** source_pp);

/* jobs */
static void hb_add_timeval (struct timeval *tv_p, unsigned int msec);
static int hb_compare_timeval (struct timeval *arg1, struct timeval *arg2);
const char *hb_strtime (char *s, unsigned int max, struct timeval *tv_p);

static int hb_job_queue (HB_JOB * jobs, unsigned int job_type,
			 HB_JOB_ARG * arg, unsigned int msec);
static HB_JOB_ENTRY *hb_job_dequeue (HB_JOB * jobs);
static void hb_job_shutdown (HB_JOB * jobs);


/* cluster jobs */
static void hb_cluster_job_init (HB_JOB_ARG * arg);
static void hb_cluster_job_heartbeat (HB_JOB_ARG * arg);
static void hb_cluster_job_calc_score (HB_JOB_ARG * arg);
static void hb_cluster_job_failover (HB_JOB_ARG * arg);

static void hb_cluster_request_heartbeat_to_all (void);
static void hb_cluster_send_heartbeat (bool is_req, char *host_name);
static void hb_cluster_receive_heartbeat (char *buffer, int len,
					  struct sockaddr_in *from,
					  socklen_t from_len);

static int hb_cluster_calc_score (void);

static void hb_set_net_header (HBP_HEADER * header, unsigned char type,
			       bool is_req, unsigned short len,
			       unsigned int seq, char *dest_host_name);
static int hb_sockaddr (const char *host, int port, struct sockaddr *saddr,
			socklen_t * slen);

/* cluster jobs queue */
static HB_JOB_ENTRY *hb_cluster_job_dequeue (void);
static int hb_cluster_job_queue (unsigned int job_type, HB_JOB_ARG * arg,
				 unsigned int msec);
static void hb_cluster_job_shutdown (void);

/* cluster node */
static HB_NODE_ENTRY *hb_add_node_to_cluster (char *host_name,
					      unsigned short priority);
static void hb_remove_node (HB_NODE_ENTRY * entry_p);
static void hb_cluster_remove_all_nodes (HB_NODE_ENTRY * first);
static HB_NODE_ENTRY *hb_return_node_by_name (char *name);
static HB_NODE_ENTRY *hb_return_node_by_name_except_me (char *name);

static int hb_cluster_load_group_and_node_list (char *ha_node_list);


/* resource jobs */
static void hb_resource_job_proc_start (HB_JOB_ARG * arg);
static void hb_resource_job_proc_dereg (HB_JOB_ARG * arg);
static void hb_resource_job_confirm_start (HB_JOB_ARG * arg);
static void hb_resource_job_confirm_dereg (HB_JOB_ARG * arg);
static void hb_resource_job_change_mode (HB_JOB_ARG * arg);


/* resource job queue */
static HB_JOB_ENTRY *hb_resource_job_dequeue (void);
static int hb_resource_job_queue (unsigned int job_type, HB_JOB_ARG * arg,
				  unsigned int msec);
static void hb_resource_job_shutdown (void);

/* resource process */
static HB_PROC_ENTRY *hb_alloc_new_proc (void);
static void hb_remove_proc (HB_PROC_ENTRY * entry_p);
static void hb_remove_all_procs (HB_PROC_ENTRY * first);

static HB_PROC_ENTRY *hb_return_proc_by_args (char *args);
static HB_PROC_ENTRY *hb_return_proc_by_pid (int pid);
static HB_PROC_ENTRY *hb_return_proc_by_fd (int sfd);
static void hb_proc_make_arg (char **arg, char *argv);

/* resource process connection */
void hb_cleanup_conn_and_start_process (CSS_CONN_ENTRY * conn, SOCKET sfd);
static int hb_resource_send_changemode (HB_PROC_ENTRY * proc);
extern void hb_register_new_process (CSS_CONN_ENTRY * conn);
extern void hb_resource_receive_changemode (CSS_CONN_ENTRY * conn);

/* cluster/resource threads */
#if defined(WINDOW)
static unsigned __stdcall hb_thread_cluster_worker (void *arg);
static unsigned __stdcall hb_thread_cluster_reader (void *arg);
static unsigned __stdcall hb_thread_resource_worker (void *arg);
#else
static void *hb_thread_cluster_worker (void *arg);
static void *hb_thread_cluster_reader (void *arg);
static void *hb_thread_resource_worker (void *arg);
#endif


/* initializer */
static int hb_cluster_initialize (const char *nodes);
static int hb_cluster_job_initialize (void);
static int hb_resource_initialize (void);
static int hb_resource_job_initialize (void);
extern int hb_thread_initialize (void);

/* terminator */
static void hb_resource_cleanup (void);
static void hb_cluster_cleanup (void);

/* process command */
static const char *hb_node_state_string (int nstate);
static const char *hb_process_state_string (int pstate);
static int hb_reload_config (void);
void hb_get_node_info_string (char **str, bool verbose_yn);
void hb_get_process_info_string (char **str, bool verbose_yn);
void hb_dereg_process (pid_t pid, char **str);
void hb_reconfig_heartbeat (char **str);
void hb_deactivate_heartbeat (char **str);
void hb_activate_heartbeat (char **str);


/* debug and test */
static void hb_print_nodes (void);
static void hb_print_jobs (HB_JOB * jobs);
static void hb_print_procs (void);


HB_CLUSTER *hb_Cluster = NULL;
HB_RESOURCE *hb_Resource = NULL;
HB_JOB *cluster_Jobs = NULL;
HB_JOB *resource_Jobs = NULL;

static bool hb_Is_activated = true;

/* cluster jobs */
static HB_JOB_FUNC hb_cluster_jobs[] = {
  hb_cluster_job_init,
  hb_cluster_job_heartbeat,
  hb_cluster_job_calc_score,
  hb_cluster_job_failover,
  NULL
};

/* resource jobs */
static HB_JOB_FUNC hb_resource_jobs[] = {
  hb_resource_job_proc_start,
  hb_resource_job_proc_dereg,
  hb_resource_job_confirm_start,
  hb_resource_job_confirm_dereg,
  hb_resource_job_change_mode,
  NULL
};

#define HA_NODE_INFO_FORMAT_STRING       \
	" HA-Node Info (current %s, state %s)\n"
#define HA_NODE_FORMAT_STRING            \
	"   Node %s (priority %d, state %s)\n"
#define HA_NODE_SCORE_FORMAT_STRING      \
        "    - score %d\n"
#define HA_NODE_HEARTBEAT_GAP_FORMAT_STRING      \
        "    - heartbeat-gap %d\n"

#define HA_PROCESS_INFO_FORMAT_STRING    \
	" HA-Process Info (master %d, state %s)\n"
#define HA_SERVER_PROCESS_FORMAT_STRING  \
	"   Server %s (pid %d, state %s)\n"
#define HA_COPYLOG_PROCESS_FORMAT_STRING \
	"   Copylogdb %s (pid %d, state %s)\n"
#define HA_APPLYLOG_PROCESS_FORMAT_STRING        \
	"   Applylogdb %s (pid %d, state %s)\n"
#define HA_PROCESS_EXEC_PATH_FORMAT_STRING       \
        "    - exec-path [%s] \n"
#define HA_PROCESS_ARGV_FORMAT_STRING            \
        "    - argv      [%s] \n"
#define HA_PROCESS_REGISTER_TIME_FORMAT_STRING     \
        "    - registered-time   %s\n"
#define HA_PROCESS_DEREGISTER_TIME_FORMAT_STRING   \
        "    - deregistered-time %s\n"
#define HA_PROCESS_SHUTDOWN_TIME_FORMAT_STRING     \
        "    - shutdown-time     %s\n"
#define HA_PROCESS_START_TIME_FORMAT_STRING        \
        "    - start-time        %s\n"

/*
 * linked list
 */
/*
 * hb_list_add() -
 *   return: none
 *
 *   prev(in):
 *   entry(in/out):
 */
static void
hb_list_add (HB_LIST ** p, HB_LIST * n)
{
  n->next = *(p);
  if (n->next)
    {
      n->next->prev = &(n->next);
    }
  n->prev = p;
  *(p) = n;
}

/*
 * hb_list_remove() -
 *   return: none
 *   entry(in):
 */
static void
hb_list_remove (HB_LIST * n)
{
  if (n->prev)
    {
      *(n->prev) = n->next;
      if (*(n->prev))
	{
	  n->next->prev = n->prev;
	}
    }
  n->next = NULL;
  n->prev = NULL;
}

/*
 * hb_list_move() -
 *   return: none 
 *   dest_pp(in):
 *   source_pp(in):
 */
static void
hb_list_move (HB_LIST ** dest_pp, HB_LIST ** source_pp)
{
  *dest_pp = *source_pp;
  if (*dest_pp)
    {
      (*dest_pp)->prev = dest_pp;
    }

  *source_pp = NULL;
}

/*
 * job common
 */

/*
 * hb_add_timeval() -
 *
 *   return: none
 *   tv_p(in/out):
 *   msec(in):
 */
static void
hb_add_timeval (struct timeval *tv_p, unsigned int msec)
{
  if (NULL == tv_p)
    {
      return;
    }

  tv_p->tv_sec += (msec / 1000);
  tv_p->tv_usec += ((msec % 1000) * 1000);
}

/*
 * hb_compare_timeval() -
 *   return: (1)  if arg1 > arg2
 *           (0)  if arg1 = arg2
 *           (-1) if arg1 < arg2
 *
 *   arg1(in):
 *   arg2(in):
 */
static int
hb_compare_timeval (struct timeval *arg1, struct timeval *arg2)
{
  if (NULL == arg1 && NULL == arg2)
    {
      return 0;
    }
  if (NULL == arg1)
    {
      return -1;
    }
  if (NULL == arg2)
    {
      return 1;
    }

  if (arg1->tv_sec > arg2->tv_sec)
    {
      return 1;
    }
  else if (arg1->tv_sec == arg2->tv_sec)
    {
      if (arg1->tv_usec > arg2->tv_usec)
	{
	  return 1;
	}
      else if (arg1->tv_usec == arg2->tv_usec)
	{
	  return 0;
	}
      else
	{
	  return -1;
	}
    }
  else
    {
      return -1;
    }
}

/*
 * hb_strtime() -
 *
 *   return: time string
 *   s(in):
 *   max(in):
 *   tv_p(in):
 */
const char *
hb_strtime (char *s, unsigned int max, struct timeval *tv_p)
{
  struct tm hb_tm, *hb_tm_p = &hb_tm;

  if (NULL == s || 24 > max || NULL == tv_p || 0 == tv_p->tv_sec)
    {
      goto error_return;
    }
  *s = '\0';

#if !defined(WINDOWS)
  hb_tm_p = localtime_r (&tv_p->tv_sec, &hb_tm);
#else
  hb_tm_p = localtime (&tv_p->tv_sec);
#endif

  if (NULL == hb_tm_p)
    {
      goto error_return;
    }

  snprintf (s + strftime (s, (max - 5), "%m/%d/%y %H:%M:%S", hb_tm_p),
	    (max - 1), ".%03ld", tv_p->tv_usec / 1000);
  s[max - 1] = '\0';
  return (const char *) s;

error_return:
  return (const char *) "00/00/00 00:00:00.000";
}

/*
 * hb_job_queue() - enqueue a job to the queue sorted by expire time
 *   return: NO_ERROR or ER_FAILED
 *
 *   jobs(in):
 *   job_type(in):
 *   arg(in):
 *   msec(in):
 */
static int
hb_job_queue (HB_JOB * jobs, unsigned int job_type, HB_JOB_ARG * arg,
	      unsigned int msec)
{
  HB_JOB_ENTRY **job;
  HB_JOB_ENTRY *new_job;
  struct timeval now;
  int rv;

  new_job = (HB_JOB_ENTRY *) malloc (sizeof (HB_JOB_ENTRY));
  if (NULL == new_job)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      sizeof (HB_JOB_ENTRY));
      return (ER_OUT_OF_VIRTUAL_MEMORY);
    }

  gettimeofday (&now, NULL);
  hb_add_timeval (&now, msec);

  new_job->prev = NULL;
  new_job->next = NULL;
  new_job->type = job_type;
  new_job->func = jobs->job_funcs[job_type];
  new_job->arg = arg;
  memcpy ((void *) &(new_job->expire), (void *) &now,
	  sizeof (struct timeval));

  MUTEX_LOCK (rv, jobs->lock);
  for (job = &(jobs->jobs); *job; job = &((*job)->next))
    {
      /*
       * compare expire time of new job and current job
       * until new job's expire is larger than current's
       */
      if (hb_compare_timeval (&((*job)->expire), &now) <= 0)
	{
	  continue;
	}
      break;
    }
  hb_list_add ((HB_LIST **) job, (HB_LIST *) new_job);

  MUTEX_UNLOCK (jobs->lock);
  return (NO_ERROR);
}

/*
 * hb_job_dequeue() - dequeue a job from queue expiration time
 *                    is smaller than current time
 *   return: pointer to heartbeat job entry
 *
 *   jobs(in):
 */
static HB_JOB_ENTRY *
hb_job_dequeue (HB_JOB * jobs)
{
  struct timeval now;
  HB_JOB_ENTRY *job;
  int rv;

  gettimeofday (&now, NULL);

  MUTEX_LOCK (rv, jobs->lock);
  if (true == jobs->shutdown)
    {
      MUTEX_UNLOCK (jobs->lock);
      return NULL;
    }

  job = jobs->jobs;
  if (NULL == job)
    {
      MUTEX_UNLOCK (jobs->lock);
      return NULL;
    }

  if (hb_compare_timeval (&now, &job->expire) >= 0)
    {
      hb_list_remove ((HB_LIST *) job);
    }
  else
    {
      MUTEX_UNLOCK (jobs->lock);
      return NULL;
    }
  MUTEX_UNLOCK (jobs->lock);

  return job;
}

/*
 * hb_job_shutdown() - clear job queue and stop job worker thread
 *   return: none
 *
 *   jobs(in):
 */
static void
hb_job_shutdown (HB_JOB * jobs)
{
  int rv;
  HB_JOB_ENTRY *job, *job_next;

  MUTEX_LOCK (rv, jobs->lock);
  for (job = jobs->jobs; job; job = job_next)
    {
      job_next = job->next;

      hb_list_remove ((HB_LIST *) job);
      free_and_init (job);
    }
  jobs->shutdown = true;
  MUTEX_UNLOCK (jobs->lock);
}


/*
 *cluster node job actions
 */

/*
 * hb_cluster_job_init() -
 *   return: none
 *
 *   arg(in):
 */
static void
hb_cluster_job_init (HB_JOB_ARG * arg)
{
  int error;

  error = hb_cluster_job_queue (HB_CJOB_HEARTBEAT, NULL,
				HB_JOB_TIMER_IMMEDIATELY);
  assert (error == NO_ERROR);

  error = hb_cluster_job_queue (HB_CJOB_CALC_SCORE, NULL,
				PRM_HA_INIT_TIMER_IN_MSECS);
  assert (error == NO_ERROR);

  free_and_init (arg);
}

/*
 * hb_cluster_job_heartbeat() - send heartbeat request to other nodes
 *   return: none
 *
 *   jobs(in):
 */
static void
hb_cluster_job_heartbeat (HB_JOB_ARG * arg)
{
  int error, rv;

  MUTEX_LOCK (rv, hb_Cluster->lock);
  hb_cluster_request_heartbeat_to_all ();
  MUTEX_UNLOCK (hb_Cluster->lock);
  error = hb_cluster_job_queue (HB_CJOB_HEARTBEAT, NULL,
				PRM_HA_HEARTBEAT_INTERVAL_IN_MSECS);
  assert (error == NO_ERROR);

  free_and_init (arg);
  return;
}

/*
 * hb_cluster_job_calc_score() -
 *   return: none
 *
 *   jobs(in):
 */
static void
hb_cluster_job_calc_score (HB_JOB_ARG * arg)
{
  int error, rv;
  int num_master;

  MUTEX_LOCK (rv, hb_Cluster->lock);

  num_master = hb_cluster_calc_score ();

  /* case : split-brain */
  if ((num_master > 1) &&
      (hb_Cluster->master && hb_Cluster->myself &&
       hb_Cluster->master->priority != hb_Cluster->myself->priority))
    {
      hb_Cluster->shutdown = true;
      hb_Cluster->state = HB_NSTATE_UNKNOWN;
      hb_cluster_request_heartbeat_to_all ();

      hb_print_nodes ();

      MUTEX_UNLOCK (hb_Cluster->lock);

      free_and_init (arg);

      er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_HB_NODE_EVENT, 1,
	      "More than one master detected and local processes and cub_master will be terminated");

      /* TODO : hb_terminate() */
      css_master_cleanup (SIGTERM);
      return;
    }

  /* case : failover */
  if ((hb_Cluster->state == HB_NSTATE_SLAVE) &&
      (hb_Cluster->master && hb_Cluster->myself &&
       hb_Cluster->master->priority == hb_Cluster->myself->priority))
    {
      hb_Cluster->state = HB_NSTATE_MASTER;
      hb_cluster_request_heartbeat_to_all ();

      hb_print_nodes ();

      MUTEX_UNLOCK (hb_Cluster->lock);

      error = hb_cluster_job_queue (HB_CJOB_FAILOVER, NULL,
				    PRM_HA_FAILOVER_WAIT_TIME_IN_MSECS);

      assert (error == NO_ERROR);

      free_and_init (arg);

      er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_HB_NODE_EVENT, 1,
	      "A failover attempted to make the current node a master");
      return;
    }

  hb_print_nodes ();

  MUTEX_UNLOCK (hb_Cluster->lock);

  error = hb_cluster_job_queue (HB_CJOB_CALC_SCORE, NULL,
				PRM_HA_CALC_SCORE_INTERVAL_IN_MSECS);
  assert (error == NO_ERROR);

  free_and_init (arg);
  return;
}

/*
 * hb_cluster_job_failover() -
 *   return: none
 *
 *   jobs(in):
 */
static void
hb_cluster_job_failover (HB_JOB_ARG * arg)
{
  int error, rv;
  int num_master;

  MUTEX_LOCK (rv, hb_Cluster->lock);

  num_master = hb_cluster_calc_score ();

  if (hb_Cluster->master && hb_Cluster->myself
      && hb_Cluster->master->priority == hb_Cluster->myself->priority)
    {
      er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_HB_NODE_EVENT, 1,
	      "Failover completed");
      hb_Resource->state = HB_NSTATE_MASTER;
    }
  else
    {
      er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_HB_NODE_EVENT, 1,
	      "Failover cancelled");
      hb_Cluster->state = HB_NSTATE_SLAVE;
    }

  hb_cluster_request_heartbeat_to_all ();
  MUTEX_UNLOCK (hb_Cluster->lock);

  error = hb_cluster_job_queue (HB_CJOB_CALC_SCORE, NULL,
				PRM_HA_CALC_SCORE_INTERVAL_IN_MSECS);
  assert (error == NO_ERROR);

  free_and_init (arg);
  return;
}



/*
 * cluster common
 */

/*
 * hb_cluster_calc_score() -
 *   return: number of master nodes in heartbeat cluster
 */
static int
hb_cluster_calc_score (void)
{
  int num_master = 0;
  short min_index = -1;
  short min_score = HB_NODE_SCORE_UNKNOWN;
  HB_NODE_ENTRY *node;

  if (NULL == hb_Cluster)
    {
      er_log_debug (ARG_FILE_LINE, "hb_Cluster is null. \n");
      return (ER_FAILED);
    }

  hb_Cluster->myself->state = hb_Cluster->state;

  for (node = hb_Cluster->nodes; node; node = node->next)
    {
      if (node->heartbeat_gap > PRM_HA_MAX_HEARTBEAT_GAP)
	{
	  node->heartbeat_gap = 0;
	  node->state = HB_NSTATE_UNKNOWN;
	}

      switch (node->state)
	{
	case HB_NSTATE_MASTER:
	  {
	    node->score = node->priority | HB_NODE_SCORE_MASTER;
	  }
	  break;
	case HB_NSTATE_SLAVE:
	  {
	    node->score = node->priority | HB_NODE_SCORE_SLAVE;
	  }
	  break;
	case HB_NSTATE_UNKNOWN:
	default:
	  {
	    node->score = node->priority | HB_NODE_SCORE_UNKNOWN;
	  }
	  break;
	}

      if (min_score > node->score)
	{
	  hb_Cluster->master = node;
	  min_score = node->score;
	}

      if (node->score < 0)
	{
	  num_master++;
	}
    }

  return num_master;
}

/*
 * hb_cluster_request_heartbeat_to_all() -
 *   return: none
 *
 */
static void
hb_cluster_request_heartbeat_to_all (void)
{
  HB_NODE_ENTRY *node;
  int i;

  if (NULL == hb_Cluster)
    {
      er_log_debug (ARG_FILE_LINE, "hb_Cluster is null. \n");
      return;
    }

  for (node = hb_Cluster->nodes; node; node = node->next)
    {
      if (strcmp (hb_Cluster->host_name, node->host_name) == 0)
	continue;

      hb_cluster_send_heartbeat (true, node->host_name);
      node->heartbeat_gap++;
    }

  return;
}

/*
 * hb_cluster_send_heartbeat() -
 *   return: none
 *
 *   is_req(in):
 *   host_name(in):
 */
static void
hb_cluster_send_heartbeat (bool is_req, char *host_name)
{
  int error;

  HBP_HEADER *hbp_header;
  char buffer[HB_BUFFER_SZ], *p;
  size_t msg_len;
  int send_len;

  struct sockaddr_in saddr;
  socklen_t saddr_len;

  /* construct destination address */
  memset ((void *) &saddr, 0, sizeof (saddr));
  if (hb_sockaddr
      (host_name, PRM_HA_PORT_ID, (struct sockaddr *) &saddr,
       &saddr_len) != NO_ERROR)
    {
      er_log_debug (ARG_FILE_LINE, "hb_sockaddr failed. \n");
      return;
    }


  memset ((void *) buffer, 0, sizeof (buffer));
  hbp_header = (HBP_HEADER *) (&buffer[0]);

  hb_set_net_header (hbp_header, HBP_CLUSTER_HEARTBEAT, is_req,
		     OR_INT_SIZE, 0, host_name);

  p = (char *) (hbp_header + 1);
  p = or_pack_int (p, hb_Cluster->state);

  msg_len = sizeof (HBP_HEADER) + OR_INT_SIZE;

  send_len = sendto (hb_Cluster->sfd, (void *) &buffer[0], msg_len, 0,
		     (struct sockaddr *) &saddr, saddr_len);
  if (send_len <= 0)
    {
      er_log_debug (ARG_FILE_LINE, "sendto failed. \n");
      /* TODO : if error */
    }

  return;
}


/*
 * hb_cluster_receive_heartbeat() -
 *   return: none
 *
 *   buffer(in):
 *   len(in):
 *   from(in):
 *   from_len(in):
 */
static void
hb_cluster_receive_heartbeat (char *buffer, int len, struct sockaddr_in *from,
			      socklen_t from_len)
{
  int rv;
  HBP_HEADER *hbp_header;
  HB_NODE_ENTRY *node;
  char *p;

  int state;

  hbp_header = (HBP_HEADER *) (buffer);

  MUTEX_LOCK (rv, hb_Cluster->lock);
  if (hb_Cluster->shutdown)
    {
      MUTEX_UNLOCK (hb_Cluster->lock);
      return;
    }

  /* validate receive message */
  if (strcmp (hb_Cluster->host_name, hbp_header->dest_host_name))
    {
      er_log_debug (ARG_FILE_LINE, "hostname mismatch. "
		    "(host_name:{%s}, dest_host_name:{%s}).\n",
		    hb_Cluster->host_name, hbp_header->dest_host_name);
      MUTEX_UNLOCK (hb_Cluster->lock);
      return;
    }

  if (len != (int) (sizeof (*hbp_header) + htons (hbp_header->len)))
    {
      er_log_debug (ARG_FILE_LINE, "size mismatch. "
		    "(len:%d, msg_size:%d).\n", len,
		    (sizeof (*hbp_header) + htons (hbp_header->len)));
      MUTEX_UNLOCK (hb_Cluster->lock);
      return;
    }

#if 0
  er_log_debug (ARG_FILE_LINE, "hbp_header. (type:%d, r:%d, len:%d, seq:%d, "
		"orig_host_name:{%s}, dest_host_name:{%s}). \n",
		hbp_header->type, (hbp_header->r) ? 1 : 0,
		ntohs (hbp_header->len), ntohl (hbp_header->seq),
		hbp_header->orig_host_name, hbp_header->dest_host_name);
#endif

  switch (hbp_header->type)
    {
    case HBP_CLUSTER_HEARTBEAT:
      {
	p = (char *) (hbp_header + 1);
	or_unpack_int (p, &state);

	if (state < 0 || state >= HB_NSTATE_MAX)
	  {
	    er_log_debug (ARG_FILE_LINE,
			  "receive heartbeat have unknown state. "
			  "(state:%u).\n", state);
	    MUTEX_UNLOCK (hb_Cluster->lock);
	    return;
	  }
#if 0
	er_log_debug (ARG_FILE_LINE, "hbp_heartbeat. (state:%d). \n", state);
#endif


	/* 
	 * if heartbeat group id is mismatch, ignore heartbeat 
	 */
	if (strcmp (hbp_header->group_id, hb_Cluster->group_id))
	  {
	    MUTEX_UNLOCK (hb_Cluster->lock);
	    return;
	  }

	/* 
	 * must send heartbeat response in order to avoid split-brain 
	 * when heartbeat configuration changed 
	 */
	if (hbp_header->r)
	  {
	    hb_cluster_send_heartbeat (false, hbp_header->orig_host_name);
	  }

	node = hb_return_node_by_name_except_me (hbp_header->orig_host_name);
	if (node)
	  {
	    node->state = (unsigned short) state;
	    node->heartbeat_gap = MAX (0, (node->heartbeat_gap - 1));

	  }
	else
	  {
	    er_log_debug (ARG_FILE_LINE,
			  "receive heartbeat have unknown host_name. "
			  "(host_name:{%s}).\n", hbp_header->orig_host_name);
	  }
      }
      break;
    default:
      {
	er_log_debug (ARG_FILE_LINE, "unknown heartbeat message. "
		      "(type:%d). \n", hbp_header->type);
      }
      break;

    }

  MUTEX_UNLOCK (hb_Cluster->lock);

  return;
}

/*
 * hb_set_net_header() -
 *   return: none
 *
 *   header(out):
 *   type(in):
 *   is_req(in):
 *   len(in):
 *   seq(in):
 *   dest_host_name(in):
 */
static void
hb_set_net_header (HBP_HEADER * header, unsigned char type, bool is_req,
		   unsigned short len, unsigned int seq, char *dest_host_name)
{
  header->type = type;
  header->r = (is_req) ? 1 : 0;
  header->len = htons (len);
  header->seq = htonl (seq);
  strncpy (header->group_id, hb_Cluster->group_id,
	   sizeof (header->group_id) - 1);
  header->group_id[sizeof (header->group_id) - 1] = '\0';
  strncpy (header->dest_host_name, dest_host_name,
	   sizeof (header->dest_host_name) - 1);
  header->dest_host_name[sizeof (header->dest_host_name) - 1] = '\0';
  strncpy (header->orig_host_name, hb_Cluster->host_name,
	   sizeof (header->orig_host_name) - 1);
  header->orig_host_name[sizeof (header->orig_host_name) - 1] = '\0';
}

/*
 * hb_sockaddr() -
 *   return: NO_ERROR
 *
 *   host(in):
 *   port(in):
 *   saddr(out):
 *   slen(out):
 */
static int
hb_sockaddr (const char *host, int port, struct sockaddr *saddr,
	     socklen_t * slen)
{
  struct sockaddr_in udp_saddr;
  in_addr_t in_addr;

  /*
   * Construct address for UDP socket
   */
  memset ((void *) &udp_saddr, 0, sizeof (udp_saddr));
  udp_saddr.sin_family = AF_INET;
  udp_saddr.sin_port = htons (port);

  /*
   * First try to convert to the host name as a dotten-decimal number.
   * Only if that fails do we call gethostbyname.
   */
  in_addr = inet_addr (host);
  if (in_addr != INADDR_NONE)
    {
      memcpy ((void *) &udp_saddr.sin_addr, (void *) &in_addr,
	      sizeof (in_addr));
    }
  else
    {
#ifdef HAVE_GETHOSTBYNAME_R
# if defined (HAVE_GETHOSTBYNAME_R_GLIBC)
      struct hostent *hp, hent;
      int herr;
      char buf[1024];

      if (gethostbyname_r (host, &hent, buf, sizeof (buf), &hp, &herr) != 0 ||
	  hp == NULL)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			       ERR_CSS_TCP_HOST_NAME_ERROR, 1, host);
	  return INVALID_SOCKET;
	}
      memcpy ((void *) &udp_saddr.sin_addr, (void *) hent.h_addr,
	      hent.h_length);
# elif defined (HAVE_GETHOSTBYNAME_R_SOLARIS)
      struct hostent hent;
      int herr;
      char buf[1024];

      if (gethostbyname_r (host, &hent, buf, sizeof (buf), &herr) == NULL)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			       ERR_CSS_TCP_HOST_NAME_ERROR, 1, host);
	  return INVALID_SOCKET;
	}
      memcpy ((void *) &udp_saddr.sin_addr, (void *) hent.h_addr,
	      hent.h_length);
# elif defined (HAVE_GETHOSTBYNAME_R_HPUX)
      struct hostent hent;
      char buf[1024];

      if (gethostbyname_r (host, &hent, buf) == -1)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			       ERR_CSS_TCP_HOST_NAME_ERROR, 1, host);
	  return INVALID_SOCKET;
	}
      memcpy ((void *) &udp_saddr.sin_addr, (void *) hent.h_addr,
	      hent.h_length);
# else
#   error "HAVE_GETHOSTBYNAME_R"
# endif
#else /* HAVE_GETHOSTBYNAME_R */
      struct hostent *hp;
      int r;

      MUTEX_LOCK (r, gethostbyname_lock);
      hp = gethostbyname (host);
      if (hp == NULL)
	{
	  MUTEX_UNLOCK (gethostbyname_lock);
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			       ERR_CSS_TCP_HOST_NAME_ERROR, 1, host);
	  return INVALID_SOCKET;
	}
      memcpy ((void *) &udp_saddr.sin_addr, (void *) hp->h_addr,
	      hp->h_length);
      MUTEX_UNLOCK (gethostbyname_lock);
#endif /* !HAVE_GETHOSTBYNAME_R */
    }

  *slen = sizeof (udp_saddr);
  memcpy ((void *) saddr, (void *) &udp_saddr, *slen);

  return NO_ERROR;
}



/*
 * cluster job queue
 */

/*
 * hb_cluster_job_dequeue() -
 *   return: pointer to cluster job entry
 */
static HB_JOB_ENTRY *
hb_cluster_job_dequeue (void)
{
  return hb_job_dequeue (cluster_Jobs);
}

/*
 * hb_cluster_job_queue() -
 *   return: NO_ERROR or ER_FAILED
 *
 *   job_type(in):
 *   arg(in):
 *   msec(in):
 */
static int
hb_cluster_job_queue (unsigned int job_type, HB_JOB_ARG * arg,
		      unsigned int msec)
{
  if (job_type >= HB_CJOB_MAX)
    {
      er_log_debug (ARG_FILE_LINE, "unknown job type. (job_type:%d).\n",
		    job_type);
      return (ER_FAILED);
    }

  return hb_job_queue (cluster_Jobs, job_type, arg, msec);
}

/*
 * hb_cluster_job_shutdown() -
 *   return: pointer to cluster job entry
 */
static void
hb_cluster_job_shutdown (void)
{
  return hb_job_shutdown (cluster_Jobs);
}


/*
 * cluster node
 */

/*
 * hb_add_node_to_cluster() -
 *   return: pointer to heartbeat node entry
 *
 *   host_name(in):
 *   priority(in):
 */
static HB_NODE_ENTRY *
hb_add_node_to_cluster (char *host_name, unsigned short priority)
{
  HB_NODE_ENTRY *p;
  HB_NODE_ENTRY **first_pp;

  if (NULL == host_name)
    {
      return NULL;
    }

  p = (HB_NODE_ENTRY *) malloc (sizeof (HB_NODE_ENTRY));
  if (p)
    {
      if (strcmp (host_name, "localhost") == 0)
	{
	  strncpy (p->host_name, hb_Cluster->host_name,
		   sizeof (p->host_name) - 1);
	}
      else
	{
	  strncpy (p->host_name, host_name, sizeof (p->host_name) - 1);
	}
      p->host_name[sizeof (p->host_name) - 1] = '\0';
      p->priority = priority;
      p->state = HB_NSTATE_UNKNOWN;
      p->score = 0;
      p->heartbeat_gap = 0;

      p->next = NULL;
      p->prev = NULL;
      first_pp = &hb_Cluster->nodes;
      hb_list_add ((HB_LIST **) first_pp, (HB_LIST *) p);
    }

  return (p);
}

/*
 * hb_remove_node() -
 *   return: none
 *
 *   entry_p(in):
 */
static void
hb_remove_node (HB_NODE_ENTRY * entry_p)
{
  if (entry_p)
    {
      hb_list_remove ((HB_LIST *) entry_p);
      free_and_init (entry_p);
    }
  return;
}

/*
 * hb_cluster_remove_all_nodes() -
 *   return: none
 *
 *   first(in):
 */
static void
hb_cluster_remove_all_nodes (HB_NODE_ENTRY * first)
{
  HB_NODE_ENTRY *node, *next_node;

  for (node = first; node; node = next_node)
    {
      next_node = node->next;
      hb_remove_node (node);
    }
}

/*
 * hb_return_node_by_name() -
 *   return: pointer to heartbeat node entry
 *
 *   name(in):
 */
static HB_NODE_ENTRY *
hb_return_node_by_name (char *name)
{
  int i;
  HB_NODE_ENTRY *node;

  for (node = hb_Cluster->nodes; node; node = node->next)
    {
      if (strcmp (name, node->host_name))
	{
	  continue;
	}

      return (node);
    }

  return NULL;
}

/*
 * hb_return_node_by_name_except_me() -
 *   return: pointer to heartbeat node entry
 *
 *   name(in):
 */
static HB_NODE_ENTRY *
hb_return_node_by_name_except_me (char *name)
{
  int i;
  HB_NODE_ENTRY *node;

  for (node = hb_Cluster->nodes; node; node = node->next)
    {
      if (strcmp (name, node->host_name) ||
	  (strcmp (name, hb_Cluster->host_name) == 0))
	{
	  continue;
	}

      return (node);
    }

  return NULL;
}

/*
 * hb_cluster_load_group_and_node_list() -
 *   return: number of cluster nodes
 *
 *   host_list(in):
 */
static int
hb_cluster_load_group_and_node_list (char *ha_node_list)
{
  int priority;
  char *tmp_ha_node_list = NULL;
  char *p, *savep;
  HB_NODE_ENTRY *node;

  if (NULL == ha_node_list)
    {
      er_log_debug (ARG_FILE_LINE,
		    "invalid ha_node_list. (ha_node_list:NULL).\n");
      return (ER_FAILED);
    }

  tmp_ha_node_list = strdup (ha_node_list);
  if (NULL == tmp_ha_node_list)
    {
      return (ER_FAILED);
    }

  hb_Cluster->myself = NULL;
  for (priority = 0, p = strtok_r (tmp_ha_node_list, "@\r\n", &savep);
       p; priority++, p = strtok_r (NULL, ",:\r\n", &savep))
    {

      if (priority == 0)
	{
	  /* TODO : trim group id */
	  /* set heartbeat group id */
	  strncpy (hb_Cluster->group_id, p,
		   sizeof (hb_Cluster->group_id) - 1);
	  hb_Cluster->group_id[sizeof (hb_Cluster->group_id) - 1] = '\0';

	}
      else
	{
	  /* TODO : trim node name */
	  node = hb_add_node_to_cluster (p, (priority));
	  if (node)
	    {
	      if (strcmp (node->host_name, hb_Cluster->host_name) == 0)
		{
		  hb_Cluster->myself = node;
#if defined (HB_VERBOSE_DEBUG)
		  er_log_debug (ARG_FILE_LINE,
				"find myself node. (myself:%p, priority:%d). \n",
				hb_Cluster->myself,
				hb_Cluster->myself->priority);
#endif
		}
	    }
	}
    }

  free_and_init (tmp_ha_node_list);

  if (NULL == hb_Cluster->myself)
    {
      er_log_debug (ARG_FILE_LINE, "cannot find myself. \n");
      return (ER_FAILED);
    }

  return (priority);
}



/*
 * resource process job actions
 */

/*
 * hb_resource_job_proc_start () -
 *   return: none
 *
 *   arg(in):
 */
static void
hb_resource_job_proc_start (HB_JOB_ARG * arg)
{
  int error, rv;
  char error_string[LINE_MAX] = "";
  pid_t pid;
  HB_PROC_ENTRY *proc;
  HB_RESOURCE_JOB_ARG *proc_arg = (arg) ? &(arg->resource_job_arg) : NULL;
  char *argv[HB_MAX_NUM_PROC_ARGV] = { NULL, };

  if (arg == NULL || proc_arg == NULL)
    {
      er_log_debug (ARG_FILE_LINE,
		    "invalid arg or proc_arg. (arg:%p, proc_arg:%p). \n",
		    arg, proc_arg);
      free_and_init (arg);
      return;
    }

  MUTEX_LOCK (rv, hb_Resource->lock);
  proc = hb_return_proc_by_args (proc_arg->args);
  if (proc == NULL || proc->state == HB_PSTATE_DEREGISTERED)
    {
      MUTEX_UNLOCK (hb_Resource->lock);
      free_and_init (arg);
      return;
    }

  snprintf (error_string, LINE_MAX, "(args:%s)", proc->args);
  er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_HB_PROCESS_EVENT, 2,
	  "Restart the process", error_string);

  hb_proc_make_arg (argv, (char *) proc->argv);

  pid = fork ();
  if (pid < 0)
    {
      MUTEX_UNLOCK (hb_Resource->lock);

      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ERR_CSS_CANNOT_FORK, 0);

      error = hb_resource_job_queue (HB_RJOB_PROC_START, arg,
				     HB_JOB_TIMER_WAIT_A_SECOND);
      if (error != NO_ERROR)
	{
	  assert (false);
	  free_and_init (arg);
	}
      return;
    }
  else if (pid == 0)
    {
#if defined (HB_VERBOSE_DEBUG)
      er_log_debug (ARG_FILE_LINE,
		    "execute:{%s} arg[0]:{%s} arg[1]:{%s} arg[2]:{%s} "
		    "arg[3]:{%s} arg{4}:{%s} arg[5]:{%s} arg[6]:{%s} "
		    "arg[7]:{%s} arg[8]:{%s} arg[9]:{%s}.\n",
		    proc->exec_path, (argv[0]) ? argv[0] : "",
		    (argv[1]) ? argv[1] : "", (argv[2]) ? argv[2] : "",
		    (argv[3]) ? argv[3] : "", (argv[4]) ? argv[4] : "",
		    (argv[5]) ? argv[5] : "", (argv[6]) ? argv[6] : "",
		    (argv[7]) ? argv[7] : "", (argv[8]) ? argv[8] : "",
		    (argv[9]) ? argv[9] : "");
#endif
      error = execv (proc->exec_path, argv);
      MUTEX_UNLOCK (hb_Resource->lock);

      free_and_init (arg);
      css_master_cleanup (SIGTERM);
      return;
    }
  else
    {
      proc->pid = pid;
      proc->state = HB_PSTATE_STARTED;
      gettimeofday (&proc->stime, NULL);
    }

  MUTEX_UNLOCK (hb_Resource->lock);

  error = hb_resource_job_queue (HB_RJOB_CONFIRM_START, arg,
				 PRM_HA_PROCESS_START_CONFIRM_INTERVAL_IN_MSECS);
  if (error != NO_ERROR)
    {
      assert (false);
      free_and_init (arg);
    }

  return;
}

/*
 * hb_resource_job_proc_dereg() -
 *   return: none
 *
 *   arg(in):
 */
static void
hb_resource_job_proc_dereg (HB_JOB_ARG * arg)
{
  int error, rv;
  HB_PROC_ENTRY *proc;
  HB_RESOURCE_JOB_ARG *proc_arg = (arg) ? &(arg->resource_job_arg) : NULL;

  if (arg == NULL || proc_arg == NULL)
    {
      er_log_debug (ARG_FILE_LINE,
		    "invalid arg or proc_arg. (arg:%p, proc_arg:%p). \n",
		    arg, proc_arg);
      free_and_init (arg);
      return;
    }
#if defined (HB_VERBOSE_DEBUG)
  er_log_debug (ARG_FILE_LINE, "deregister process. (pid:%d). \n",
		proc_arg->pid);
#endif

  MUTEX_LOCK (rv, hb_Resource->lock);
  proc = hb_return_proc_by_pid (proc_arg->pid);
  if (proc == NULL)
    {
      er_log_debug (ARG_FILE_LINE,
		    "cannot find process entry. (unknown pid, pid:%d). \n",
		    proc_arg->pid);
      MUTEX_UNLOCK (hb_Resource->lock);

      free_and_init (arg);
      return;
    }

  if (proc->state != HB_PSTATE_DEREGISTERED)
    {
      er_log_debug (ARG_FILE_LINE,
		    "invalid process state. (pid:%d, state:%d). \n",
		    proc_arg->pid, proc->state);
      MUTEX_UNLOCK (hb_Resource->lock);

      free_and_init (arg);
      return;
    }

  error = kill (proc->pid, SIGTERM);
  if (error && errno == ESRCH)
    {
      hb_Resource->num_procs--;
      hb_remove_proc (proc);
      proc = NULL;

      MUTEX_UNLOCK (hb_Resource->lock);

      free_and_init (arg);
      return;
    }

  MUTEX_UNLOCK (hb_Resource->lock);

  error = hb_resource_job_queue (HB_RJOB_CONFIRM_DEREG, arg,
				 PRM_HA_PROCESS_DEREG_CONFIRM_INTERVAL_IN_MSECS);
  if (error != NO_ERROR)
    {
      assert (false);
      free_and_init (arg);
    }

  return;
}

/*
 * hb_resource_job_confirm_start() -
 *   return: none
 *
 *   arg(in):
 */
static void
hb_resource_job_confirm_start (HB_JOB_ARG * arg)
{
  int error, rv;
  char error_string[LINE_MAX] = "";
  bool retry = true;
  HB_PROC_ENTRY *proc;
  HB_RESOURCE_JOB_ARG *proc_arg = (arg) ? &(arg->resource_job_arg) : NULL;

  if (arg == NULL || proc_arg == NULL)
    {
      er_log_debug (ARG_FILE_LINE,
		    "invalid arg or proc_arg. (arg:%p, proc_arg:%p). \n",
		    arg, proc_arg);
      free_and_init (arg);
      return;
    }

  MUTEX_LOCK (rv, hb_Resource->lock);
  proc = hb_return_proc_by_args (proc_arg->args);
  if (proc == NULL || proc->state == HB_PSTATE_DEREGISTERED)
    {
      MUTEX_UNLOCK (hb_Resource->lock);
      free_and_init (arg);
      return;
    }

  if (++(proc_arg->retries) > proc_arg->max_retries)
    {
      MUTEX_UNLOCK (hb_Resource->lock);

      snprintf (error_string, LINE_MAX, "(exceed max retry count, args:%s)",
		proc->args);
      er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_HB_PROCESS_EVENT, 2,
	      "Failed to restart the process", error_string);

      if (hb_Resource->state == HB_NSTATE_MASTER
	  && proc->type == HB_PTYPE_SERVER)
	{
	  free_and_init (arg);
	  css_master_cleanup (SIGTERM);
	  return;
	}
      else
	{
	  proc_arg->retries = 0;
	  error = hb_resource_job_queue (HB_RJOB_PROC_START, arg,
					 HB_JOB_TIMER_WAIT_A_SECOND);
	  if (error != NO_ERROR)
	    {
	      assert (false);
	      free_and_init (arg);
	    }
	  return;
	}
    }

  error = kill (proc->pid, 0);
  if (error)
    {
      MUTEX_UNLOCK (hb_Resource->lock);
      if (errno == ESRCH)
	{
	  snprintf (error_string, LINE_MAX, "(process not found, args:%s)",
		    proc->args);
	  er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE,
		  ER_HB_PROCESS_EVENT, 2, "Failed to restart process",
		  error_string);

	  error = hb_resource_job_queue (HB_RJOB_PROC_START, arg,
					 HB_JOB_TIMER_WAIT_A_SECOND);
	  if (error != NO_ERROR)
	    {
	      assert (false);
	      free_and_init (arg);
	    }
	}
      else
	{
	  error = hb_resource_job_queue (HB_RJOB_CONFIRM_START, arg,
					 PRM_HA_PROCESS_START_CONFIRM_INTERVAL_IN_MSECS);
	  if (error != NO_ERROR)
	    {
	      assert (false);
	      free_and_init (arg);
	    }
	}
      return;
    }

  if (proc->state == HB_PSTATE_NOT_REGISTERED)
    {
      proc->state = HB_PSTATE_REGISTERED;
      retry = false;
    }

  MUTEX_UNLOCK (hb_Resource->lock);

  hb_print_procs ();

  if (retry)
    {
      error = hb_resource_job_queue (HB_RJOB_CONFIRM_START, arg,
				     PRM_HA_PROCESS_START_CONFIRM_INTERVAL_IN_MSECS);
      if (error != NO_ERROR)
	{
	  assert (false);
	  free_and_init (arg);
	}
      return;
    }

  free_and_init (arg);

  return;
}

/*
 * hb_resource_job_confirm_dereg() -
 *   return: none
 *
 *   arg(in):
 */
static void
hb_resource_job_confirm_dereg (HB_JOB_ARG * arg)
{
  int error, rv;
  bool retry = true;
  HB_PROC_ENTRY *proc;
  HB_RESOURCE_JOB_ARG *proc_arg = (arg) ? &(arg->resource_job_arg) : NULL;

  if (arg == NULL || proc_arg == NULL)
    {
      er_log_debug (ARG_FILE_LINE,
		    "invalid arg or proc_arg. (arg:%p, proc_arg:%p). \n",
		    arg, proc_arg);
      free_and_init (arg);
      return;
    }

#if defined (HB_VERBOSE_DEBUG)
  er_log_debug (ARG_FILE_LINE,
		"deregister confirm process. (pid:%d, args:{%s}). \n",
		proc_arg->pid, proc_arg->args);
#endif

  MUTEX_LOCK (rv, hb_Resource->lock);
  proc = hb_return_proc_by_pid (proc_arg->pid);
  if (proc == NULL)
    {
      MUTEX_UNLOCK (hb_Resource->lock);
      free_and_init (arg);
      return;
    }

  if (proc->state != HB_PSTATE_DEREGISTERED)
    {
      er_log_debug (ARG_FILE_LINE,
		    "invalid process state. (pid:%d, state:%d). \n",
		    proc_arg->pid, proc->state);
      MUTEX_UNLOCK (hb_Resource->lock);

      free_and_init (arg);
      return;
    }

  error = kill (proc->pid, 0);
  if (error)
    {
      if (errno == ESRCH)
	{
	  retry = false;
	}
    }
  else
    {
      if (++(proc_arg->retries) > proc_arg->max_retries)
	{
	  kill (proc->pid, SIGKILL);
	  retry = false;
	}
    }

  if (retry)
    {
      MUTEX_UNLOCK (hb_Resource->lock);
      error = hb_resource_job_queue (HB_RJOB_CONFIRM_DEREG, arg,
				     PRM_HA_PROCESS_DEREG_CONFIRM_INTERVAL_IN_MSECS);
      if (error != NO_ERROR)
	{
	  assert (false);
	  free_and_init (arg);
	}
      return;
    }

  hb_Resource->num_procs--;
  hb_remove_proc (proc);
  proc = NULL;

  MUTEX_UNLOCK (hb_Resource->lock);

  free_and_init (arg);

  return;
}

/*
 * hb_resource_job_change_mode() -
 *   return: none
 *
 *   arg(in):
 */
static void
hb_resource_job_change_mode (HB_JOB_ARG * arg)
{
  int i, error, rv;
  HB_PROC_ENTRY *proc;

  if (hb_Resource->state == HB_NSTATE_MASTER)
    {
#if !defined(WINDOWS)
      MUTEX_LOCK (rv, css_Master_socket_anchor_lock);
#endif
      MUTEX_LOCK (rv, hb_Resource->lock);
      for (proc = hb_Resource->procs; proc; proc = proc->next)
	{
	  if (proc->type != HB_PTYPE_SERVER
	      || proc->state != HB_PSTATE_REGISTERED)
	    {
	      continue;
	    }

	  /* TODO : send heartbeat changemode request */
	  er_log_debug (ARG_FILE_LINE,
			"send change-mode request. "
			"(node_state:%d, pid:%d, proc_state:%d). \n",
			hb_Resource->state, proc->pid, proc->state);

	  error = hb_resource_send_changemode (proc);
	  if (NO_ERROR != error)
	    {
	      /* TODO : if error */
	    }
	}

      if (hb_Resource->procs)
	{
	  hb_print_procs ();
	}

      MUTEX_UNLOCK (hb_Resource->lock);
#if !defined(WINDOWS)
      MUTEX_UNLOCK (css_Master_socket_anchor_lock);
#endif
    }

  error = hb_resource_job_queue (HB_RJOB_CHANGE_MODE, NULL,
				 PRM_HA_CHANGEMODE_INTERVAL_IN_MSECS);
  assert (error == NO_ERROR);

  free_and_init (arg);
  return;
}



/*
 * resource job queue
 */

/*
 * hb_resource_job_dequeue() -
 *   return: pointer to resource job entry
 *
 */
static HB_JOB_ENTRY *
hb_resource_job_dequeue (void)
{
  return hb_job_dequeue (resource_Jobs);
}

/*
 * hb_resource_job_queue() -
 *   return: NO_ERROR or ER_FAILED
 *
 *   job_type(in):
 *   arg(in):
 *   msec(in):
 */
static int
hb_resource_job_queue (unsigned int job_type, HB_JOB_ARG * arg,
		       unsigned int msec)
{
  if (job_type >= HB_RJOB_MAX)
    {
      er_log_debug (ARG_FILE_LINE, "unknown job type. (job_type:%d).\n",
		    job_type);
      return (ER_FAILED);
    }

  return hb_job_queue (resource_Jobs, job_type, arg, msec);
}

/*
 * hb_resource_job_shutdown() -
 *   return: none
 *
 */
static void
hb_resource_job_shutdown (void)
{
  return hb_job_shutdown (resource_Jobs);
}

/*
 * resource process
 */

/*
 * hb_alloc_new_proc() -
 *   return: pointer to resource process entry
 *
 */
static HB_PROC_ENTRY *
hb_alloc_new_proc (void)
{
  HB_PROC_ENTRY *p;
  HB_PROC_ENTRY **first_pp;

  p = (HB_PROC_ENTRY *) malloc (sizeof (HB_PROC_ENTRY));
  if (p)
    {
      memset ((void *) p, 0, sizeof (HB_PROC_ENTRY));
      p->state = HB_PSTATE_UNKNOWN;
      p->next = NULL;
      p->prev = NULL;
      first_pp = &hb_Resource->procs;
      hb_list_add ((HB_LIST **) first_pp, (HB_LIST *) p);
    }

  return (p);
}

/*
 * hb_remove_proc() -
 *   return: none
 *
 *   entry_p(in):
 */
static void
hb_remove_proc (HB_PROC_ENTRY * entry_p)
{
  if (entry_p)
    {
      hb_list_remove ((HB_LIST *) entry_p);
      free_and_init (entry_p);
    }
  return;
}

/*
 * hb_remove_all_procs() -
 *   return: none
 *
 *   first(in):
 */
static void
hb_remove_all_procs (HB_PROC_ENTRY * first)
{
  HB_PROC_ENTRY *proc, *next_proc;

  for (proc = first; proc; proc = next_proc)
    {
      next_proc = proc->next;
      hb_remove_proc (proc);
    }
}

/*
 * hb_return_proc_by_args() -
 *   return: pointer to resource process entry
 *
 *   args(in):
 */
static HB_PROC_ENTRY *
hb_return_proc_by_args (char *args)
{
  int i;
  HB_PROC_ENTRY *proc;

  for (proc = hb_Resource->procs; proc; proc = proc->next)
    {
      if (strcmp (proc->args, args))
	{
	  continue;
	}
      return (proc);
    }
  return NULL;
}

/*
 * hb_return_proc_by_pid() -
 *   return: pointer to resource process entry
 *
 *   sfd(in):
 */
static HB_PROC_ENTRY *
hb_return_proc_by_pid (int pid)
{
  int i;
  HB_PROC_ENTRY *proc;

  for (proc = hb_Resource->procs; proc; proc = proc->next)
    {
      if (proc->pid != pid)
	{
	  continue;
	}
      return (proc);

    }
  return NULL;
}

/*
 * hb_return_proc_by_fd() -
 *   return: pointer to resource process entry
 *
 *   sfd(in):
 */
static HB_PROC_ENTRY *
hb_return_proc_by_fd (int sfd)
{
  int i;
  HB_PROC_ENTRY *proc;

  for (proc = hb_Resource->procs; proc; proc = proc->next)
    {
      if (proc->sfd != sfd)
	{
	  continue;
	}
      return (proc);

    }
  return NULL;
}

/*
 * hb_proc_make_arg() -
 *   return: none
 *
 *   arg(out):
 *   argv(in):
 */
static void
hb_proc_make_arg (char **arg, char *argv)
{
  int i;

  for (i = 0; i < HB_MAX_NUM_PROC_ARGV;
       i++, arg++, argv += HB_MAX_SZ_PROC_ARGV)
    {
      if ((*argv == 0))
	{
	  break;
	}

      (*arg) = argv;
    }
  return;
}


/*
 * resource process connection
 */

/*
 * hb_cleanup_conn_and_start_process() -
 *   return: none
 *
 *   sfd(in):
 *   conn(in):
 */
void
hb_cleanup_conn_and_start_process (CSS_CONN_ENTRY * conn, SOCKET sfd)
{
  int error, rv;
  char error_string[LINE_MAX] = "";
  HB_PROC_ENTRY *proc;
  HB_JOB_ARG *job_arg;
  HB_RESOURCE_JOB_ARG *proc_arg;

  css_remove_entry_by_conn (conn, &css_Master_socket_anchor);

  MUTEX_LOCK (rv, hb_Resource->lock);
  proc = hb_return_proc_by_fd (sfd);
  if (NULL == proc)
    {
      er_log_debug (ARG_FILE_LINE, "cannot find process. (fd:%d). \n", sfd);
      MUTEX_UNLOCK (hb_Resource->lock);
      return;
    }

  if (HB_PSTATE_REGISTERED != proc->state
      && HB_PSTATE_REGISTERED_AND_ACTIVE != proc->state)
    {
      er_log_debug (ARG_FILE_LINE, "unexpected process's state. "
		    "(fd:%d, pid:%d, state:%d, args:{%s}). \n", sfd,
		    proc->pid, proc->state, proc->args);
      MUTEX_UNLOCK (hb_Resource->lock);
      return;
    }

  gettimeofday (&proc->ktime, NULL);
#if defined (HB_VERBOSE_DEBUG)
  er_log_debug (ARG_FILE_LINE,
		"process terminated. (args:{%s}, pid:%d, state:%d). \n",
		proc->args, proc->pid, proc->state);
#endif

  snprintf (error_string, LINE_MAX, "(pid:%d, args:%s)", proc->pid,
	    proc->args);
  er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_HB_PROCESS_EVENT, 2,
	  "Process failure detected", error_string);

  job_arg = (HB_JOB_ARG *) malloc (sizeof (HB_JOB_ARG));
  if (NULL == job_arg)
    {
      MUTEX_UNLOCK (hb_Resource->lock);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      sizeof (HB_JOB_ARG));
      return;
    }

  proc_arg = &(job_arg->resource_job_arg);
  proc_arg->pid = proc->pid;
  memcpy ((void *) &proc_arg->args[0], proc->args, sizeof (proc_arg->args));
  proc_arg->retries = 0;
  proc_arg->max_retries = PRM_HA_MAX_PROCESS_START_CONFIRM;
  gettimeofday (&proc_arg->ftime, NULL);

  proc->state = HB_PSTATE_DEAD;
  proc->pid = 0;
  proc->sfd = INVALID_SOCKET;
  proc->conn = NULL;

  MUTEX_UNLOCK (hb_Resource->lock);

  error = hb_resource_job_queue (HB_RJOB_PROC_START, job_arg,
				 HB_JOB_TIMER_WAIT_A_SECOND);
  assert (error == NO_ERROR);

  return;
}

/*
 * hb_register_new_process() -
 *   return: none
 *
 *   conn(in):
 *   rid(in):
 */
void
hb_register_new_process (CSS_CONN_ENTRY * conn)
{
  int rv, buffer_size;
  HBP_PROC_REGISTER *hbp_proc_register = NULL;
  SOCKET_QUEUE_ENTRY *temp;
  HB_PROC_ENTRY *proc;
  unsigned char proc_state = HB_PSTATE_UNKNOWN;
  char buffer[HB_BUFFER_SZ];
  char error_string[LINE_MAX] = "";

  if (NULL == hb_Resource)
    {
      return;
    }

  buffer_size = sizeof (HBP_PROC_REGISTER);

  rv = css_receive_heartbeat_data (conn, buffer, buffer_size);
  if (rv != NO_ERRORS)
    {
      return;
    }

  hbp_proc_register = (HBP_PROC_REGISTER *) buffer;

  MUTEX_LOCK (rv, hb_Resource->lock);
  if (hb_Resource->shutdown)
    {
      MUTEX_UNLOCK (hb_Resource->lock);
      css_remove_entry_by_conn (conn, &css_Master_socket_anchor);
      return;
    }

  proc = hb_return_proc_by_args (hbp_proc_register->args);
  if (NULL == proc)
    {
      proc = hb_alloc_new_proc ();
      if (proc == NULL)
	{
	  MUTEX_UNLOCK (hb_Resource->lock);
	  css_remove_entry_by_conn (conn, &css_Master_socket_anchor);
	  return;
	}
      else
	{
	  proc_state = HB_PSTATE_REGISTERED;
	}
    }
  else
    {
      proc_state =
	(proc->state ==
	 HB_PSTATE_STARTED) ? HB_PSTATE_NOT_REGISTERED : HB_PSTATE_UNKNOWN;
    }

  if ((proc_state == HB_PSTATE_REGISTERED) ||
      (proc_state == HB_PSTATE_NOT_REGISTERED
       && proc->pid == (int) ntohl (hbp_proc_register->pid)
       && !(kill (proc->pid, 0) && errno == ESRCH)))

    {
      proc->state = proc_state;
      proc->sfd = conn->fd;
      proc->conn = conn;
      gettimeofday (&proc->rtime, NULL);

      if (proc->state == HB_PSTATE_REGISTERED)
	{
	  proc->pid = ntohl (hbp_proc_register->pid);
	  proc->type = ntohl (hbp_proc_register->type);
	  memcpy ((void *) &proc->exec_path[0],
		  (void *) &hbp_proc_register->exec_path[0],
		  sizeof (proc->exec_path));
	  memcpy ((void *) &proc->args[0],
		  (void *) &hbp_proc_register->args[0], sizeof (proc->args));
	  memcpy ((void *) &proc->argv[0],
		  (void *) &hbp_proc_register->argv[0], sizeof (proc->argv));
	  hb_Resource->num_procs++;
	}

#if defined (HB_VERBOSE_DEBUG)
      er_log_debug (ARG_FILE_LINE,
		    "hbp_proc_register. (sizeof(hbp_proc_register):%d, \n"
		    "type:%d, state:%d, pid:%d, exec_path:{%s}, "
		    "args:{%s}). \n",
		    sizeof (HBP_PROC_REGISTER), proc->type, proc->state,
		    proc->pid, proc->exec_path, proc->args);
      hb_print_procs ();
#endif

      snprintf (error_string, LINE_MAX, "%s (pid:%d, state:%d, args:%s)",
		HB_RESULT_SUCCESS_STR, ntohl (hbp_proc_register->pid),
		proc->state, hbp_proc_register->args);
      er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_HB_PROCESS_EVENT, 2,
	      "Registered as local process entries", error_string);

      MUTEX_UNLOCK (hb_Resource->lock);
      return;
    }

  MUTEX_UNLOCK (hb_Resource->lock);

  snprintf (error_string, LINE_MAX, "%s (pid:%d, state:%d, args:%s)",
	    HB_RESULT_FAILURE_STR, ntohl (hbp_proc_register->pid),
	    proc->state, hbp_proc_register->args);
  er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_HB_PROCESS_EVENT, 2,
	  "Registered as local process entries", error_string);

  css_remove_entry_by_conn (conn, &css_Master_socket_anchor);
  return;
}

/*
 * hb_resource_send_changemode -
 *   return: none
 *
 *   proc(in):
 */
static int
hb_resource_send_changemode (HB_PROC_ENTRY * proc)
{
  int error = NO_ERROR;
  int state;

  if (NULL == proc->conn)
    {
      return (ER_FAILED);
    }

  switch (hb_Resource->state)
    {
    case HB_NSTATE_MASTER:
      {
	state = HA_SERVER_STATE_ACTIVE;
      }
      break;
    case HB_NSTATE_SLAVE:
    default:
      {
	return (ER_FAILED);
      }
      break;
    }

  error = css_send_heartbeat_request (proc->conn, SERVER_CHANGE_HA_MODE);
  if (NO_ERRORS != error)
    {
      return (ER_FAILED);
    }

  state = htonl (state);
  error = css_send_heartbeat_data (proc->conn, (char *) &state,
				   sizeof (state));
  if (NO_ERRORS != error)
    {
      return (ER_FAILED);
    }

  return (NO_ERROR);
}

/*
 * hb_resource_receive_changemode -
 *   return: none
 *
 *   conn(in):
 */
void
hb_resource_receive_changemode (CSS_CONN_ENTRY * conn)
{
  int sfd, error, rv;
  HB_PROC_ENTRY *proc;
  int state;
  char *ptr;

  if (NULL == hb_Resource)
    {
      return;
    }

  rv = css_receive_heartbeat_data (conn, (char *) &state, sizeof (state));
  if (rv != NO_ERRORS)
    {
      return;
    }
  state = ntohl (state);

  sfd = conn->fd;
  MUTEX_LOCK (rv, hb_Resource->lock);
  proc = hb_return_proc_by_fd (sfd);
  if (NULL == proc)
    {
      er_log_debug (ARG_FILE_LINE, "cannot find process. (fd:%d). \n", sfd);
      MUTEX_UNLOCK (hb_Resource->lock);
      return;
    }

#if defined (HB_VERBOSE_DEBUG)
  er_log_debug (ARG_FILE_LINE,
		"receive changemode response. (fd:%d, state:%d). \n", sfd,
		state);
#endif

  switch (state)
    {
    case HA_SERVER_STATE_ACTIVE:
    case HA_SERVER_STATE_TO_BE_ACTIVE:
      proc->state = HB_PSTATE_REGISTERED_AND_ACTIVE;
      break;
    default:
      break;
    }

  MUTEX_UNLOCK (hb_Resource->lock);

  return;
}

/*
 * heartbeat worker threads
 */

/*
 * hb_thread_cluster_worker -
 *   return: none
 *
 *   arg(in):
 */
#if defined(WINDOWS)
static unsigned __stdcall
hb_thread_cluster_worker (void *arg)
#else
static void *
hb_thread_cluster_worker (void *arg)
#endif
{
  HB_JOB_ENTRY *job;

#if defined (HB_VERBOSE_DEBUG)
  er_log_debug (ARG_FILE_LINE, "thread started. (thread:{%s}, tid:%d).\n",
		__func__, THREAD_ID ());
#endif

  while (false == cluster_Jobs->shutdown)
    {
      while ((job = hb_cluster_job_dequeue ()) != NULL)
	{
	  job->func (job->arg);
	  free_and_init (job);
	}

      SLEEP_MILISEC (0, 10);
    }

#if defined (HB_VERBOSE_DEBUG)
  er_log_debug (ARG_FILE_LINE, "thread exit.\n");
#endif

#if defined(WINDOWS)
  return 0;
#else /* WINDOWS */
  return NULL;
#endif /* WINDOWS */
}

/*
 * hb_thread_cluster_reader -
 *   return: none
 *
 *   arg(in):
 */
#if defined(WINDOWS)
static unsigned __stdcall
hb_thread_cluster_reader (void *arg)
#else
static void *
hb_thread_cluster_reader (void *arg)
#endif
{
  int error, rv;
  SOCKET sfd;
  fd_set rfds;
  struct timeval tv;
  char buffer[HB_BUFFER_SZ];
  int len;

  struct sockaddr_in from;
  socklen_t from_len;

#if defined (HB_VERBOSE_DEBUG)
  er_log_debug (ARG_FILE_LINE, "thread started. (thread:{%s}, tid:%d).\n",
		__func__, THREAD_ID ());
#endif

  sfd = hb_Cluster->sfd;
  while (false == hb_Cluster->shutdown)
    {
      FD_ZERO (&rfds);
      FD_SET (sfd, &rfds);

      tv.tv_sec = 0;
      tv.tv_usec = 1000;
      error = select (FD_SETSIZE, &rfds, NULL, NULL, &tv);
      if (error <= 0)
	{
	  continue;
	}

      if (FD_ISSET (sfd, &rfds) && sfd == hb_Cluster->sfd)
	{
	  from_len = sizeof (from);
	  len =
	    recvfrom (sfd, (void *) &buffer[0], sizeof (buffer),
		      0, (struct sockaddr *) &from, &from_len);
	  if (len > 0)
	    {
	      hb_cluster_receive_heartbeat (buffer, len, &from, from_len);
	    }
	}
    }

#if defined (HB_VERBOSE_DEBUG)
  er_log_debug (ARG_FILE_LINE, "thread exit.\n");
#endif

#if defined(WINDOWS)
  return 0;
#else /* WINDOWS */
  return NULL;
#endif /* WINDOWS */
}

/*
 * hb_thread_resource_worker -
 *   return: none
 *
 *   arg(in):
 */
#if defined(WINDOWS)
static unsigned __stdcall
hb_thread_resource_worker (void *arg)
#else
static void *
hb_thread_resource_worker (void *arg)
#endif
{
  HB_JOB_ENTRY *job;

#if defined (HB_VERBOSE_DEBUG)
  er_log_debug (ARG_FILE_LINE, "thread started. (thread:{%s}, tid:%d).\n",
		__func__, THREAD_ID ());
#endif

  while (false == resource_Jobs->shutdown)
    {
      while ((job = hb_resource_job_dequeue ()) != NULL)
	{
	  job->func (job->arg);
	  free_and_init (job);
	}

      SLEEP_MILISEC (0, 10);
    }

#if defined (HB_VERBOSE_DEBUG)
  er_log_debug (ARG_FILE_LINE, "thread exit.\n");
#endif

#if defined(WINDOWS)
  return 0;
#else /* WINDOWS */
  return NULL;
#endif /* WINDOWS */
}


/*
 * master heartbeat initializer
 */

/*
 * hb_cluster_job_initialize -
 *   return: NO_ERROR or ER_FAILED
 *
 */
static int
hb_cluster_job_initialize (void)
{
  int rv, error;

  if (NULL == cluster_Jobs)
    {
      cluster_Jobs = (HB_JOB *) malloc (sizeof (HB_JOB));
      if (NULL == cluster_Jobs)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
		  1, sizeof (HB_JOB));
	  return (ER_OUT_OF_VIRTUAL_MEMORY);
	}

      MUTEX_INIT (cluster_Jobs->lock);
    }

  MUTEX_LOCK (rv, cluster_Jobs->lock);
  cluster_Jobs->shutdown = false;
  cluster_Jobs->num_jobs = 0;
  cluster_Jobs->jobs = NULL;
  cluster_Jobs->job_funcs = &hb_cluster_jobs[0];
  MUTEX_UNLOCK (cluster_Jobs->lock);

  error = hb_cluster_job_queue (HB_CJOB_INIT, NULL, HB_JOB_TIMER_IMMEDIATELY);
  if (error != NO_ERROR)
    {
      assert (false);
      return (ER_FAILED);
    }

  return (NO_ERROR);
}


/*
 * hb_cluster_initialize -
 *   return: NO_ERROR or ER_FAILED
 *
 */
static int
hb_cluster_initialize (const char *nodes)
{
  int rv, error;
  HB_NODE_ENTRY *node;
  struct sockaddr_in udp_saddr;
  char host_name[MAXHOSTNAMELEN];
  char host_list[(MAXHOSTNAMELEN + 1) * HB_MAX_NUM_NODES];

  if (NULL == nodes)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_PRM_BAD_VALUE, 1,
	      PRM_NAME_HA_NODE_LIST);

      return (ER_PRM_BAD_VALUE);
    }

  if (NULL == hb_Cluster)
    {
      hb_Cluster = (HB_CLUSTER *) malloc (sizeof (HB_CLUSTER));
      if (NULL == hb_Cluster)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
		  1, sizeof (HB_CLUSTER));
	  return (ER_OUT_OF_VIRTUAL_MEMORY);
	}

      MUTEX_INIT (hb_Cluster->lock);
    }

  if (GETHOSTNAME (host_name, sizeof (host_name)))
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_BO_UNABLE_TO_FIND_HOSTNAME, 0);
      return ER_BO_UNABLE_TO_FIND_HOSTNAME;
    }

  MUTEX_LOCK (rv, hb_Cluster->lock);
  hb_Cluster->shutdown = false;
  hb_Cluster->sfd = INVALID_SOCKET;
  strncpy (hb_Cluster->host_name, host_name,
	   sizeof (hb_Cluster->host_name) - 1);
  hb_Cluster->host_name[sizeof (hb_Cluster->host_name) - 1] = '\0';
  hb_Cluster->state = HB_NSTATE_SLAVE;
  hb_Cluster->master = NULL;
  hb_Cluster->myself = NULL;
  hb_Cluster->nodes = NULL;

  strncpy (host_list, nodes, sizeof (host_list) - 1);
  host_list[sizeof (host_list) - 1] = '\0';

  hb_Cluster->num_nodes = hb_cluster_load_group_and_node_list (host_list);
  if (hb_Cluster->num_nodes < 1)
    {
      er_log_debug (ARG_FILE_LINE,
		    "hb_Cluster->num_nodes is smaller than '1'. (num_nodes=%d). \n",
		    hb_Cluster->num_nodes);
      MUTEX_UNLOCK (hb_Cluster->lock);

      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_PRM_BAD_VALUE, 1,
	      PRM_NAME_HA_NODE_LIST);
      return (ER_PRM_BAD_VALUE);
    }

#if defined (HB_VERBOSE_DEBUG)
  hb_print_nodes ();
#endif

  /* initialize udp socket */
  hb_Cluster->sfd = socket (AF_INET, SOCK_DGRAM, 0);
  if (hb_Cluster->sfd < 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ERR_CSS_TCP_DATAGRAM_SOCKET, 0);
      MUTEX_UNLOCK (hb_Cluster->lock);
      return (ERR_CSS_TCP_DATAGRAM_SOCKET);
    }

  memset ((void *) &udp_saddr, 0, sizeof (udp_saddr));
  udp_saddr.sin_family = AF_INET;
  udp_saddr.sin_addr.s_addr = htonl (INADDR_ANY);
  udp_saddr.sin_port = htons (PRM_HA_PORT_ID);

  if (bind
      (hb_Cluster->sfd, (struct sockaddr *) &udp_saddr,
       sizeof (udp_saddr)) < 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ERR_CSS_TCP_DATAGRAM_BIND, 0);
      MUTEX_UNLOCK (hb_Cluster->lock);
      return (ERR_CSS_TCP_DATAGRAM_BIND);
    }
  MUTEX_UNLOCK (hb_Cluster->lock);

  return (NO_ERROR);
}

/*
 * hb_resource_initialize -
 *   return: NO_ERROR or ER_FAILED
 *
 */
static int
hb_resource_initialize (void)
{
  int rv, error;
  CSS_CONN_ENTRY *conn;
  HB_PROC_ENTRY *proc;

  if (NULL == hb_Resource)
    {
      hb_Resource = (HB_RESOURCE *) malloc (sizeof (HB_RESOURCE));
      if (NULL == hb_Resource)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
		  1, sizeof (HB_RESOURCE));
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}

      MUTEX_INIT (hb_Resource->lock);
    }

  MUTEX_LOCK (rv, hb_Resource->lock);
  hb_Resource->shutdown = false;
  hb_Resource->state = HB_NSTATE_SLAVE;
  hb_Resource->num_procs = 0;
  hb_Resource->procs = NULL;
  MUTEX_UNLOCK (hb_Resource->lock);

  return (NO_ERROR);
}

/*
 * hb_resource_job_initialize -
 *   return: NO_ERROR or ER_FAILED
 *
 */
static int
hb_resource_job_initialize ()
{
  int rv, error;

  if (NULL == resource_Jobs)
    {
      resource_Jobs = (HB_JOB *) malloc (sizeof (HB_JOB));
      if (NULL == resource_Jobs)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
		  1, sizeof (HB_JOB));
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}

      MUTEX_INIT (resource_Jobs->lock);
    }

  MUTEX_LOCK (rv, resource_Jobs->lock);
  resource_Jobs->shutdown = false;
  resource_Jobs->num_jobs = 0;
  resource_Jobs->jobs = NULL;
  resource_Jobs->job_funcs = &hb_resource_jobs[0];
  MUTEX_UNLOCK (resource_Jobs->lock);

  error = hb_resource_job_queue (HB_RJOB_CHANGE_MODE, NULL,
				 PRM_HA_INIT_TIMER_IN_MSECS +
				 PRM_HA_FAILOVER_WAIT_TIME_IN_MSECS);
  if (error != NO_ERROR)
    {
      assert (false);
      return (ER_FAILED);
    }

  return (NO_ERROR);
}

/*
 * hb_thread_initialize -
 *   return: NO_ERROR or ER_FAILED
 *
 */
int
hb_thread_initialize (void)
{
  int rv;

#if !defined(WINDOWS)
  THREAD_ATTR_T thread_attr;
  size_t ts_size;
#endif /* not WINDOWS */
  THREAD_T cluster_worker_th;
  THREAD_T cluster_reader_th;
  THREAD_T resource_worker_th;

#if defined(WINDOWS)
  UINTPTR thread_handle;
#endif /* WINDOWS */

#if !defined(WINDOWS)
  rv = THREAD_ATTR_INIT (thread_attr);
  if (rv != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_CSS_PTHREAD_ATTR_INIT, 0);
      return ER_CSS_PTHREAD_ATTR_INIT;
    }

  rv = THREAD_ATTR_SETDETACHSTATE (thread_attr, PTHREAD_CREATE_DETACHED);
  if (rv != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_CSS_PTHREAD_ATTR_SETDETACHSTATE, 0);
      return ER_CSS_PTHREAD_ATTR_SETDETACHSTATE;
    }

#if defined(AIX)
  /* AIX's pthread is slightly different from other systems.
     Its performance highly depends on the pthread's scope and it's related
     kernel parameters. */
  rv = THREAD_ATTR_SETSCOPE (thread_attr,
			     PRM_PTHREAD_SCOPE_PROCESS ?
			     PTHREAD_SCOPE_PROCESS : PTHREAD_SCOPE_SYSTEM);
#else /* AIX */
  rv = THREAD_ATTR_SETSCOPE (thread_attr, PTHREAD_SCOPE_SYSTEM);
#endif /* AIX */
  if (rv != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_CSS_PTHREAD_ATTR_SETSCOPE, 0);
      return ER_CSS_PTHREAD_ATTR_SETSCOPE;
    }

  /* Sun Solaris allocates 1M for a thread stack, and it is quite enough */
#if !defined(sun) && !defined(SOLARIS)
#if defined(_POSIX_THREAD_ATTR_STACKSIZE)
  rv = THREAD_ATTR_GETSTACKSIZE (thread_attr, ts_size);
  if (ts_size < (size_t) PRM_THREAD_STACKSIZE)
    {
      rv = THREAD_ATTR_SETSTACKSIZE (thread_attr, PRM_THREAD_STACKSIZE);
      if (rv != 0)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			       ER_CSS_PTHREAD_ATTR_SETSTACKSIZE, 0);
	  return ER_CSS_PTHREAD_ATTR_SETSTACKSIZE;
	}

      THREAD_ATTR_GETSTACKSIZE (thread_attr, ts_size);
    }
#endif /* _POSIX_THREAD_ATTR_STACKSIZE */
#endif /* not sun && not SOLARIS */
#endif /* not WINDOWS */


  rv = THREAD_CREATE (thread_handle, &thread_attr,
		      hb_thread_cluster_reader, NULL, &cluster_worker_th);
  if (rv != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_CSS_PTHREAD_CREATE, 0);
      return ER_CSS_PTHREAD_CREATE;
    }

  rv = THREAD_CREATE (thread_handle, &thread_attr,
		      hb_thread_cluster_worker, NULL, &cluster_worker_th);
  if (rv != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_CSS_PTHREAD_CREATE, 0);
      return ER_CSS_PTHREAD_CREATE;
    }

  rv = THREAD_CREATE (thread_handle, &thread_attr,
		      hb_thread_resource_worker, NULL, &resource_worker_th);
  if (rv != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_CSS_PTHREAD_CREATE, 0);
      return ER_CSS_PTHREAD_CREATE;
    }

#if !defined(WINDOWS)
  /* destroy thread_attribute */
  rv = THREAD_ATTR_DESTROY (thread_attr);
  if (rv != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_CSS_PTHREAD_ATTR_DESTROY, 0);
      return ER_CSS_PTHREAD_ATTR_DESTROY;
    }
#endif /* not WINDOWS */

  return (NO_ERROR);
}

/*
 * hb_master_init -
 *   return: NO_ERROR or ER_FAILED,...
 *
 */
int
hb_master_init (void)
{
  int error;

  er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_HB_STARTED, 0);
#if defined (HB_VERBOSE_DEBUG)
  er_log_debug (ARG_FILE_LINE,
		"heartbeat params. (ha_mode:%s, heartbeat_nodes:{%s}"
		", ha_port_id:%d). \n",
		(PRM_HA_MODE) ? "yes" : "no", PRM_HA_NODE_LIST,
		PRM_HA_PORT_ID);
#endif

  sysprm_reload_and_init (NULL, NULL);
  error = hb_cluster_initialize (PRM_HA_NODE_LIST);
  if (NO_ERROR != error)
    {
      er_log_debug (ARG_FILE_LINE, "hb_cluster_initialize failed. "
		    "(error=%d). \n", error);
      goto error_return;
    }

  error = hb_cluster_job_initialize ();
  if (NO_ERROR != error)
    {
      er_log_debug (ARG_FILE_LINE, "hb_cluster_job_initialize failed. "
		    "(error=%d). \n", error);
      goto error_return;
    }

  error = hb_resource_initialize ();
  if (NO_ERROR != error)
    {
      er_log_debug (ARG_FILE_LINE, "hb_resource_initialize failed. "
		    "(error=%d). \n", error);
      goto error_return;
    }

  error = hb_resource_job_initialize ();
  if (NO_ERROR != error)
    {
      er_log_debug (ARG_FILE_LINE, "hb_resource_job_initialize failed. "
		    "(error=%d). \n", error);
      goto error_return;
    }

  error = hb_thread_initialize ();
  if (NO_ERROR != error)
    {
      er_log_debug (ARG_FILE_LINE, "hb_thread_initialize failed. "
		    "(error=%d). \n", error);
      goto error_return;
    }

  return (NO_ERROR);

error_return:
  if (hb_Cluster && hb_Cluster->shutdown == false)
    {
      hb_cluster_cleanup ();
    }

  if (cluster_Jobs && cluster_Jobs->shutdown == false)
    {
      hb_cluster_job_shutdown ();
    }

  if (hb_Resource && hb_Resource->shutdown == false)
    {
      hb_resource_cleanup ();
    }

  if (resource_Jobs && resource_Jobs->shutdown == false)
    {
      hb_resource_job_shutdown ();
    }

  return error;
}


/*
 * terminator
 */

/*
 * hb_resource_cleanup() -
 *   return: 
 *
 */
static void
hb_resource_cleanup (void)
{
  int rv, i, num_active_process;
  HB_PROC_ENTRY *proc;

  MUTEX_LOCK (rv, hb_Resource->lock);

  /* set process state to deregister and close connection  */
  for (proc = hb_Resource->procs; proc; proc = proc->next)
    {
      if (proc->conn)
	{
#if defined (HB_VERBOSE_DEBUG)
	  er_log_debug (ARG_FILE_LINE,
			"remove socket-queue entry. (pid:%d). \n", proc->pid);
#endif
	  css_remove_entry_by_conn (proc->conn, &css_Master_socket_anchor);
	  proc->conn = NULL;
	}
      else
	{
	  er_log_debug (ARG_FILE_LINE,
			"invalid socket-queue entry. (pid:%d).\n", proc->pid);
	}

      proc->state = HB_PSTATE_DEREGISTERED;
    }

#if defined (HB_VERBOSE_DEBUG)
  er_log_debug (ARG_FILE_LINE,
		"close all local heartbeat connection. (timer:%d*%d).\n",
		PRM_HA_MAX_PROCESS_DEREG_CONFIRM,
		PRM_HA_PROCESS_DEREG_CONFIRM_INTERVAL_IN_MSECS);
#endif

  /* wait until all process shutdown */
  for (i = 0; i < PRM_HA_MAX_PROCESS_DEREG_CONFIRM; i++)
    {
      for (num_active_process = 0, proc = hb_Resource->procs; proc;
	   proc = proc->next)
	{
	  if (proc->pid && proc->state != HB_PSTATE_DEAD)
	    {
	      if (kill (proc->pid, 0) && errno == ESRCH)
		{
		  proc->state = HB_PSTATE_DEAD;
		  continue;
		}
#if defined (HB_VERBOSE_DEBUG)
	      er_log_debug (ARG_FILE_LINE,
			    "process did not terminated. (pid:%d). \n",
			    proc->pid);
#endif
	      num_active_process++;
	    }
	}
      if (num_active_process == 0)
	{
#if defined (HB_VERBOSE_DEBUG)
	  er_log_debug (ARG_FILE_LINE, "all ha processes are terminated. \n");
#endif
	  break;
	}

      SLEEP_MILISEC (0, PRM_HA_PROCESS_DEREG_CONFIRM_INTERVAL_IN_MSECS);
    }

  /* send SIGKILL to all active process */
  for (proc = hb_Resource->procs; proc; proc = proc->next)
    {
      if (proc->pid && proc->state != HB_PSTATE_DEAD)
	{
#if defined (HB_VERBOSE_DEBUG)
	  er_log_debug (ARG_FILE_LINE,
			"force kill local process. (pid:%d).\n", proc->pid);
#endif
	  kill (proc->pid, SIGKILL);
	}
    }
  hb_remove_all_procs (hb_Resource->procs);
  hb_Resource->procs = NULL;
  hb_Resource->num_procs = 0;
  hb_Resource->state = HB_NSTATE_UNKNOWN;
  hb_Resource->shutdown = true;
  MUTEX_UNLOCK (hb_Resource->lock);

  return;
}

/*
 * hb_resource_shutdown_and_cleanup() -
 *   return: 
 *
 */
void
hb_resource_shutdown_and_cleanup (void)
{
  hb_resource_job_shutdown ();
  hb_resource_cleanup ();
  return;
}

/*
 * hb_cluster_cleanup() -
 *   return: 
 *
 */
static void
hb_cluster_cleanup (void)
{
  int rv;
  HB_NODE_ENTRY *node;

  MUTEX_LOCK (rv, hb_Cluster->lock);
  hb_Cluster->state = HB_NSTATE_UNKNOWN;

  for (node = hb_Cluster->nodes; node; node = node->next)
    {
      if (strcmp (hb_Cluster->host_name, node->host_name) == 0)
	continue;

      hb_cluster_send_heartbeat (true, node->host_name);
      node->heartbeat_gap++;
    }

  hb_cluster_remove_all_nodes (hb_Cluster->nodes);
  hb_Cluster->nodes = NULL;
  hb_Cluster->master = NULL;
  hb_Cluster->myself = NULL;
  hb_Cluster->shutdown = true;
  if (hb_Cluster->sfd != INVALID_SOCKET)
    {
      close (hb_Cluster->sfd);
      hb_Cluster->sfd = INVALID_SOCKET;
    }

  MUTEX_UNLOCK (hb_Cluster->lock);
}

/*
 * hb_cluster_cleanup() -
 *   return: 
 *
 */
void
hb_cluster_shutdown_and_cleanup (void)
{
  hb_cluster_job_shutdown ();
  hb_cluster_cleanup ();
}


/*
 * hb_node_state_string -
 *   return: node state sring
 *
 *   nstate(in):
 */
const char *
hb_node_state_string (int nstate)
{
  switch (nstate)
    {
    case HB_NSTATE_UNKNOWN:
      return HB_NSTATE_UNKNOWN_STR;
    case HB_NSTATE_SLAVE:
      return HB_NSTATE_SLAVE_STR;
    case HB_NSTATE_MASTER:
      return HB_NSTATE_MASTER_STR;
    }

  return "invalid";
}

/*
 * hb_process_state_string -
 *   return: process state sring
 *
 *   pstate(in):
 */
const char *
hb_process_state_string (int pstate)
{
  switch (pstate)
    {
    case HB_PSTATE_UNKNOWN:
      return HB_PSTATE_UNKNOWN_STR;
    case HB_PSTATE_DEAD:
      return HB_PSTATE_DEAD_STR;
    case HB_PSTATE_DEREGISTERED:
      return HB_PSTATE_DEREGISTERED_STR;
    case HB_PSTATE_STARTED:
      return HB_PSTATE_STARTED_STR;
    case HB_PSTATE_NOT_REGISTERED:
      return HB_PSTATE_NOT_REGISTERED_STR;
    case HB_PSTATE_REGISTERED:
      return HB_PSTATE_REGISTERED_STR;
    case HB_PSTATE_REGISTERED_AND_ACTIVE:
      return HB_PSTATE_REGISTERED_AND_ACTIVE_STR;
    }

  return "invalid";
}

/*
 * hb_reload_config -
 *   return: NO_ERROR or ER_FAILED
 *
 */
static int
hb_reload_config (void)
{
  int rv, old_num_nodes;
  HB_NODE_ENTRY *old_nodes;
  HB_NODE_ENTRY *old_node, *old_myself, *old_master, *new_node;
  HB_NODE_ENTRY **old_node_pp, **first_node_pp;

  sysprm_reload_and_init (NULL, NULL);

#if defined (HB_VERBOSE_DEBUG)
  er_log_debug (ARG_FILE_LINE, "reload configuration. (nodes:{%s}).\n",
		PRM_HA_NODE_LIST);
#endif

  if (NULL == PRM_HA_NODE_LIST)
    {
      return (ER_FAILED);
    }

  MUTEX_LOCK (rv, hb_Cluster->lock);

  old_node_pp = &old_nodes;
  first_node_pp = &hb_Cluster->nodes;
  hb_list_move ((HB_LIST **) old_node_pp, (HB_LIST **) first_node_pp);
  old_myself = hb_Cluster->myself;
  old_master = hb_Cluster->master;
  old_num_nodes = hb_Cluster->num_nodes;

  hb_Cluster->nodes = NULL;
  hb_Cluster->num_nodes =
    hb_cluster_load_group_and_node_list ((char *) PRM_HA_NODE_LIST);

  if (hb_Cluster->num_nodes < 1 ||
      (hb_Cluster->master
       && hb_return_node_by_name (hb_Cluster->master->host_name) == NULL))
    {
      er_log_debug (ARG_FILE_LINE, "reconfigure heartebat failed. "
		    "(num_nodes:%d, master:{%s}).\n",
		    hb_Cluster->num_nodes,
		    (hb_Cluster->master) ? hb_Cluster->master->
		    host_name : "-");

      hb_cluster_remove_all_nodes (hb_Cluster->nodes);
      hb_Cluster->myself = old_myself;
      hb_Cluster->master = old_master;
      hb_Cluster->num_nodes = old_num_nodes;

      old_node_pp = &old_nodes;
      first_node_pp = &hb_Cluster->nodes;
      hb_list_move ((HB_LIST **) first_node_pp, (HB_LIST **) old_node_pp);

      MUTEX_UNLOCK (hb_Cluster->lock);

      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_PRM_BAD_VALUE, 1,
	      PRM_NAME_HA_NODE_LIST);
      return (ER_PRM_BAD_VALUE);
    }

  for (new_node = hb_Cluster->nodes; new_node; new_node = new_node->next)
    {
#if defined (HB_VERBOSE_DEBUG)
      er_log_debug (ARG_FILE_LINE, "reloaded nodes list. (nodes:{%s}).\n",
		    new_node->host_name);
#endif
      for (old_node = old_nodes; old_node; old_node = old_node->next)
	{
	  if (strcmp (new_node->host_name, old_node->host_name))
	    {
	      continue;
	    }
	  if (old_master
	      && strcmp (new_node->host_name, old_master->host_name) == 0)
	    {
	      hb_Cluster->master = new_node;
	    }
	  new_node->state = old_node->state;
	  new_node->score = old_node->score;
	  new_node->heartbeat_gap = old_node->heartbeat_gap;
	}
    }
  if (old_nodes)
    {
      hb_cluster_remove_all_nodes (old_nodes);
    }

  MUTEX_UNLOCK (hb_Cluster->lock);
  return (NO_ERROR);
}

/*
 * hb_get_node_info_string -
 *   return: none
 *
 *   str(out):
 *   verbose_yn(in):
 */
void
hb_get_node_info_string (char **str, bool verbose_yn)
{
  HB_NODE_ENTRY *node;
  int rv, buf_size = 0, required_size = 0;
  char *p, *last;

  if (*str)
    {
      **str = 0;
      return;
    }

  required_size = strlen (HA_NODE_INFO_FORMAT_STRING);
  required_size += MAXHOSTNAMELEN;	/* length of node name */
  required_size += HB_NSTATE_STR_SZ;	/* length of node state */
  buf_size += required_size;

  required_size = strlen (HA_NODE_FORMAT_STRING);
  required_size += MAXHOSTNAMELEN;	/* length of node name */
  required_size += 5;		/* length of priority */
  required_size += HB_NSTATE_STR_SZ;	/* length of node state */
  if (verbose_yn)
    {
      required_size += strlen (HA_NODE_SCORE_FORMAT_STRING);
      required_size += 6;	/* length of score      */
      required_size += strlen (HA_NODE_HEARTBEAT_GAP_FORMAT_STRING);
      required_size += 6;	/* length of heartbeat-gap */
    }

  MUTEX_LOCK (rv, hb_Cluster->lock);

  required_size *= hb_Cluster->num_nodes;
  buf_size += required_size;

  *str = (char *) malloc (sizeof (char) * buf_size);
  if (*str == NULL)
    {
      MUTEX_UNLOCK (hb_Cluster->lock);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      sizeof (char) * buf_size);
      return;
    }
  **str = '\0';

  p = (char *) (*str);
  last = p + buf_size;

  p +=
    snprintf (p, MAX ((last - p), 0), HA_NODE_INFO_FORMAT_STRING,
	      hb_Cluster->host_name,
	      hb_node_state_string (hb_Cluster->state));

  for (node = hb_Cluster->nodes; node; node = node->next)
    {
      p += snprintf (p, MAX ((last - p), 0), HA_NODE_FORMAT_STRING,
		     node->host_name, node->priority,
		     hb_node_state_string (node->state));
      if (verbose_yn)
	{
	  p +=
	    snprintf (p, MAX ((last - p), 0), HA_NODE_SCORE_FORMAT_STRING,
		      node->score);
	  p +=
	    snprintf (p, MAX ((last - p), 0),
		      HA_NODE_HEARTBEAT_GAP_FORMAT_STRING,
		      node->heartbeat_gap);
	}
    }
  MUTEX_UNLOCK (hb_Cluster->lock);

  return;
}

/*
 * hb_get_process_info_string -
 *   return: none
 *
 *   str(out):
 *   verbose_yn(in):
 */
void
hb_get_process_info_string (char **str, bool verbose_yn)
{
  HB_PROC_ENTRY *proc;
  SOCKET_QUEUE_ENTRY *sock_entq;
  int rv, buf_size = 0, required_size = 0;
  char *p, *last;
  char time_str[64];

  if (*str)
    {
      **str = 0;
      return;
    }

  required_size = strlen (HA_PROCESS_INFO_FORMAT_STRING);
  required_size += 10;		/* length of pid */
  required_size += HB_NSTATE_STR_SZ;	/* length of node state */
  buf_size += required_size;

  required_size = strlen (HA_APPLYLOG_PROCESS_FORMAT_STRING);
  required_size += 256;		/* length of connection name */
  required_size += 10;		/* length of pid */
  required_size += HB_PSTATE_STR_SZ;	/* length of process state */

  if (verbose_yn)
    {
      required_size += strlen (HA_PROCESS_EXEC_PATH_FORMAT_STRING);
      required_size += HB_MAX_SZ_PROC_EXEC_PATH;
      required_size += strlen (HA_PROCESS_ARGV_FORMAT_STRING);
      required_size += HB_MAX_SZ_PROC_ARGS;
      required_size += strlen (HA_PROCESS_REGISTER_TIME_FORMAT_STRING);
      required_size += 64;	/* length of registered time */
      required_size += strlen (HA_PROCESS_DEREGISTER_TIME_FORMAT_STRING);
      required_size += 64;	/* length of deregistered time */
      required_size += strlen (HA_PROCESS_SHUTDOWN_TIME_FORMAT_STRING);
      required_size += 64;	/* length of shutdown time */
      required_size += strlen (HA_PROCESS_START_TIME_FORMAT_STRING);
      required_size += 64;	/* length of start time */
    }

  MUTEX_LOCK (rv, hb_Resource->lock);

  required_size *= hb_Resource->num_procs;
  buf_size += required_size;

  *str = (char *) malloc (sizeof (char) * buf_size);
  if (*str == NULL)
    {
      MUTEX_UNLOCK (hb_Resource->lock);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      sizeof (char) * buf_size);
      return;
    }
  **str = '\0';

  p = (char *) (*str);
  last = p + buf_size;

  p +=
    snprintf (p, MAX ((last - p), 0), HA_PROCESS_INFO_FORMAT_STRING,
	      getpid (), hb_node_state_string (hb_Resource->state));

  for (proc = hb_Resource->procs; proc; proc = proc->next)
    {
      sock_entq =
	css_return_entry_by_conn (proc->conn, &css_Master_socket_anchor);
      if (sock_entq == NULL || sock_entq->name == NULL)
	{
	  continue;
	}

      switch (proc->type)
	{
	case HB_PTYPE_SERVER:
	  p += snprintf (p, MAX ((last - p), 0),
			 HA_SERVER_PROCESS_FORMAT_STRING,
			 sock_entq->name + 1, proc->pid,
			 hb_process_state_string (proc->state));
	  break;
	case HB_PTYPE_COPYLOGDB:
	  p += snprintf (p, MAX ((last - p), 0),
			 HA_COPYLOG_PROCESS_FORMAT_STRING,
			 sock_entq->name + 1, proc->pid,
			 hb_process_state_string (proc->state));
	  break;
	case HB_PTYPE_APPLYLOGDB:
	  p += snprintf (p, MAX ((last - p), 0),
			 HA_APPLYLOG_PROCESS_FORMAT_STRING,
			 sock_entq->name + 1, proc->pid,
			 hb_process_state_string (proc->state));
	  break;
	default:
	  break;
	}

      if (verbose_yn)
	{
	  p += snprintf (p, MAX ((last - p), 0),
			 HA_PROCESS_EXEC_PATH_FORMAT_STRING, proc->exec_path);
	  p += snprintf (p, MAX ((last - p), 0),
			 HA_PROCESS_ARGV_FORMAT_STRING, proc->args);
	  p += snprintf (p, MAX ((last - p), 0),
			 HA_PROCESS_REGISTER_TIME_FORMAT_STRING,
			 hb_strtime (time_str, sizeof (time_str),
				     &proc->rtime));
	  p += snprintf (p, MAX ((last - p), 0),
			 HA_PROCESS_DEREGISTER_TIME_FORMAT_STRING,
			 hb_strtime (time_str, sizeof (time_str),
				     &proc->dtime));
	  p +=
	    snprintf (p, MAX ((last - p), 0),
		      HA_PROCESS_SHUTDOWN_TIME_FORMAT_STRING,
		      hb_strtime (time_str, sizeof (time_str), &proc->ktime));
	  p +=
	    snprintf (p, MAX ((last - p), 0),
		      HA_PROCESS_START_TIME_FORMAT_STRING,
		      hb_strtime (time_str, sizeof (time_str), &proc->stime));
	}
    }

  MUTEX_UNLOCK (hb_Resource->lock);

  return;
}


/*
 * hb_dereg_process -
 *   return: none
 *
 *   pid(in):
 *   str(out):
 */
void
hb_dereg_process (pid_t pid, char **str)
{
  HB_PROC_ENTRY *proc;
  HB_JOB_ARG *job_arg;
  HB_RESOURCE_JOB_ARG *proc_arg;
  int rv, error;
  char error_string[LINE_MAX] = "";
  char *p, *last;

  MUTEX_LOCK (rv, hb_Resource->lock);
  proc = hb_return_proc_by_pid (pid);
  if (proc == NULL)
    {
      MUTEX_UNLOCK (hb_Resource->lock);

      snprintf (error_string, LINE_MAX,
		"%s. (cannot find process to deregister, pid:%d)",
		HB_RESULT_FAILURE_STR, pid);
      er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE,
	      ER_HB_COMMAND_EXECUTION, 2, HB_CMD_DEREGISTER_STR,
	      error_string);

      return;
    }

  if ((proc->state < HB_PSTATE_NOT_REGISTERED)
      || (proc->state >= HB_PSTATE_MAX) || (proc->pid < 0))
    {
      MUTEX_UNLOCK (hb_Resource->lock);

      snprintf (error_string, LINE_MAX,
		"%s. (unexpected process status or invalid pid, status:%d, pid:%d)",
		HB_RESULT_FAILURE_STR, proc->state, pid);
      er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE,
	      ER_HB_COMMAND_EXECUTION, 2, HB_CMD_DEREGISTER_STR,
	      error_string);

      return;
    }

  gettimeofday (&proc->dtime, NULL);

  job_arg = (HB_JOB_ARG *) malloc (sizeof (HB_JOB_ARG));
  if (job_arg == NULL)
    {
      MUTEX_UNLOCK (hb_Resource->lock);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      sizeof (HB_JOB_ARG));
      return;
    }

  proc_arg = &(job_arg->resource_job_arg);
  proc_arg->pid = proc->pid;
  memcpy ((void *) &proc_arg->args[0], proc->args, sizeof (proc_arg->args));
  proc_arg->retries = 0;
  proc_arg->max_retries = PRM_HA_MAX_PROCESS_DEREG_CONFIRM;
  gettimeofday (&proc_arg->ftime, NULL);

  proc->state = HB_PSTATE_DEREGISTERED;

  MUTEX_UNLOCK (hb_Resource->lock);

  error = hb_resource_job_queue (HB_RJOB_PROC_DEREG, job_arg,
				 HB_JOB_TIMER_IMMEDIATELY);
  assert (error == NO_ERROR);

  hb_get_process_info_string (str, false);

  snprintf (error_string, LINE_MAX, "%s. (pid:%d)", HB_RESULT_SUCCESS_STR,
	    pid);
  er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_HB_COMMAND_EXECUTION, 2,
	  HB_CMD_DEREGISTER_STR, error_string);

  return;
}

/*
 * hb_reconfig_heartbeat -
 *   return: none
 *
 *   str(out):
 */
void
hb_reconfig_heartbeat (char **str)
{
  int rv, error;
  char error_string[LINE_MAX] = "";
  char *p, *last;
  HB_NODE_ENTRY *node;
  char node_entries[HB_MAX_NUM_NODES * (MAXHOSTNAMELEN + 2)];

  error = hb_reload_config ();
  if (error)
    {
      snprintf (error_string, LINE_MAX,
		"%s. (failed to reload CUBRID heartbeat configuration)",
		HB_RESULT_FAILURE_STR);
      er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE,
	      ER_HB_COMMAND_EXECUTION, 2, HB_CMD_RELOAD_STR, error_string);
    }
  else
    {
      snprintf (error_string, LINE_MAX, "%s.", HB_RESULT_SUCCESS_STR);
      er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE,
	      ER_HB_COMMAND_EXECUTION, 2, HB_CMD_RELOAD_STR, error_string);
    }

  hb_get_node_info_string (str, false);

  snprintf (error_string, LINE_MAX, "\n%s", (str && *str) ? *str : "");
  er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_HB_COMMAND_EXECUTION, 2,
	  HB_CMD_RELOAD_STR, error_string);

  return;
}

/*
 * hb_deactivate_heartbeat -
 *   return: none
 *
 *   str(out):
 */
void
hb_deactivate_heartbeat (char **str)
{
  int rv, error;
  char error_string[LINE_MAX] = "";
  int required_size = 0;
  char *p, *last;
  HB_NODE_ENTRY *node;

  if (*str)
    {
      **str = 0;
      return;
    }
  required_size = 2;
  *str = (char *) malloc (required_size * sizeof (char));
  if (NULL == *str)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      required_size * sizeof (char));
      return;
    }
  **str = '\0';

  if (hb_Is_activated == false)
    {
      snprintf (error_string, LINE_MAX,
		"%s. (CUBRID heartbeat feature already deactivated)",
		HB_RESULT_FAILURE_STR);
      er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE,
	      ER_HB_COMMAND_EXECUTION, 2, HB_CMD_DEACTIVATE_STR,
	      error_string);

      return;
    }

  if (hb_Resource && resource_Jobs)
    {
      hb_resource_shutdown_and_cleanup ();
    }

  if (hb_Cluster && cluster_Jobs)
    {
      hb_cluster_shutdown_and_cleanup ();
    }

  hb_Is_activated = false;

  snprintf (error_string, LINE_MAX, "%s.", HB_RESULT_SUCCESS_STR);
  er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_HB_COMMAND_EXECUTION, 2,
	  HB_CMD_DEACTIVATE_STR, error_string);

  return;
}

/*
 * hb_activate_heartbeat -
 *   return: none
 *
 *   str(out):
 */
void
hb_activate_heartbeat (char **str)
{
  int rv, error;
  char error_string[LINE_MAX] = "";
  int required_size = 0;
  char *p, *last;
  HB_NODE_ENTRY *node;

  if (*str)
    {
      **str = 0;
      return;
    }
  required_size = 2;
  *str = (char *) malloc (required_size * sizeof (char));
  if (NULL == *str)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      required_size * sizeof (char));
      return;
    }
  **str = '\0';

  if (hb_Is_activated == true)
    {
      snprintf (error_string, LINE_MAX,
		"%s. (CUBRID heartbeat feature already activated)",
		HB_RESULT_FAILURE_STR);
      er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE,
	      ER_HB_COMMAND_EXECUTION, 2, HB_CMD_ACTIVATE_STR, error_string);
      return;
    }

  error = hb_master_init ();
  if (error != NO_ERROR)
    {
      snprintf (error_string, LINE_MAX,
		"%s. (failed to initialize CUBRID heartbeat feature)",
		HB_RESULT_FAILURE_STR);
      er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE,
	      ER_HB_COMMAND_EXECUTION, 2, HB_CMD_ACTIVATE_STR, error_string);
      return;
    }

  hb_Is_activated = true;

  snprintf (error_string, LINE_MAX, "%s.", HB_RESULT_SUCCESS_STR);
  er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_HB_COMMAND_EXECUTION, 2,
	  HB_CMD_ACTIVATE_STR, error_string);

  return;
}




/* 
 * debug and test
 */

/*
 * hb_print_nodes -
 *   return: none
 *
 */
static void
hb_print_nodes (void)
{
  int i;
  HB_NODE_ENTRY *node;
  char buffer[8192] = { 0, }, *p, *last;

  if (NULL == hb_Cluster)
    {
      er_log_debug (ARG_FILE_LINE, "hb_Cluster is null. \n");
      return;
    }

  p = (char *) &buffer[0];
  last = p + sizeof (buffer);

  p += snprintf (p, MAX ((last - p), 0), "\n * print cluster * \n");
  p += snprintf (p, MAX ((last - p), 0), "=============================="
		 "==================================================\n");
  p +=
    snprintf (p, MAX ((last - p), 0),
	      " * group_id : %s   host_name : %s   state : %u \n",
	      hb_Cluster->group_id, hb_Cluster->host_name, hb_Cluster->state);
  p +=
    snprintf (p, MAX ((last - p), 0),
	      "------------------------------"
	      "--------------------------------------------------\n");
  p +=
    snprintf (p, MAX ((last - p), 0), "%-20s %-10s %-10s %-10s %-10s\n",
	      "name", "priority", "state", "score", "hb_gap");
  p +=
    snprintf (p, MAX ((last - p), 0),
	      "------------------------------"
	      "--------------------------------------------------\n");

  for (node = hb_Cluster->nodes; node; node = node->next)
    {
      p +=
	snprintf (p, MAX ((last - p), 0), "%-20s %-10u %-10u %-10d %-10d\n",
		  node->host_name, node->priority, node->state, node->score,
		  node->heartbeat_gap);
    }

  p += snprintf (p, MAX ((last - p), 0), "=============================="
		 "==================================================\n");
  p += snprintf (p, MAX ((last - p), 0), "\n");

  er_log_debug (ARG_FILE_LINE, "%s", buffer);
}

/*
 * hb_print_jobs -
 *   return: none
 *
 */
static void
hb_print_jobs (HB_JOB * jobs)
{
  int rv;
  HB_JOB_ENTRY *job;
  char buffer[8192] = { 0, }, *p, *last;

  p = (char *) &buffer[0];
  last = p + sizeof (buffer);

  p += snprintf (p, MAX ((last - p), 0), "\n * print jobs * \n");
  p += snprintf (p, MAX ((last - p), 0), "=============================="
		 "==================================================\n");
  p += snprintf (p, MAX ((last - p), 0), "%-10s %-20s %-20s %-20s\n", "type",
		 "func", "arg", "expire");
  p += snprintf (p, MAX ((last - p), 0), "------------------------------"
		 "--------------------------------------------------\n");

  MUTEX_LOCK (rv, jobs->lock);
  for (job = jobs->jobs; job; job = job->next)
    {

      p += snprintf (p, MAX ((last - p), 0),
		     "%-10d %-20p %-20p %-10d.%-10d\n", job->type,
		     (void *) job->func, (void *) job->arg,
		     (unsigned int) job->expire.tv_sec,
		     (unsigned int) job->expire.tv_usec);
    }

  MUTEX_UNLOCK (jobs->lock);

  p += snprintf (p, MAX ((last - p), 0), "=============================="
		 "==================================================\n");
  p += snprintf (p, MAX ((last - p), 0), "\n");

  er_log_debug (ARG_FILE_LINE, "%s", buffer);
}

/*
 * hb_print_procs -
 *   return: none
 *
 */
void
hb_print_procs (void)
{
  int i, rv;
  HB_PROC_ENTRY *proc;
  char buffer[8192] = { 0, }, *p, *last;

  if (NULL == hb_Resource)
    {
      er_log_debug (ARG_FILE_LINE, "hb_Resource is null. \n");
      return;
    }

  p = (char *) &buffer[0];
  last = p + sizeof (buffer);

  p += snprintf (p, MAX ((last - p), 0), "\n * print process * \n");

  p += snprintf (p, MAX ((last - p), 0), "=============================="
		 "==================================================\n");
  p +=
    snprintf (p, MAX ((last - p), 0), " * state : %u \n", hb_Resource->state);

  p += snprintf (p, MAX ((last - p), 0), "------------------------------"
		 "--------------------------------------------------\n");
  p +=
    snprintf (p, MAX ((last - p), 0), "%-10s %-5s %-5s %-10s\n", "pid",
	      "state", "type", "sfd");
  p +=
    snprintf (p, MAX ((last - p), 0), "     %-30s %-35s\n", "exec-path",
	      "args");
  p +=
    snprintf (p, MAX ((last - p), 0),
	      "------------------------------"
	      "--------------------------------------------------\n");

  for (proc = hb_Resource->procs; proc; proc = proc->next)
    {
      if (proc->state == HB_PSTATE_UNKNOWN)
	continue;

      p +=
	snprintf (p, MAX ((last - p), 0), "%-10d %-5u %-5u %-10d\n",
		  proc->pid, proc->state, proc->type, proc->sfd);
      p +=
	snprintf (p, MAX ((last - p), 0), "      %-30s %-35s\n",
		  proc->exec_path, proc->args);
    }

  p += snprintf (p, MAX ((last - p), 0), "=============================="
		 "==================================================\n");
  p += snprintf (p, MAX ((last - p), 0), "\n");

  er_log_debug (ARG_FILE_LINE, "%s", buffer);
}
