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
 * master_heartbeat.cpp - heartbeat module in cub_master
 */

#include "master_heartbeat.hpp"

#include "config.h"
#include "connection_cl.h"
#include "dbi.h"
#include "environment_variable.h"
#include "error_context.hpp"
#include "heartbeat.h"
#include "master_request.h"
#include "master_util.h"
#include "message_catalog.h"
#include "object_representation.h"
#include "porting.h"
#include "tcp.h"
#include "utility.h"

#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define HB_INFO_STR_MAX         8192

#define ENTER_FUNC() 	\
do {			\
  MASTER_ER_LOG_DEBUG (ARG_FILE_LINE, "%s : enter", __func__);		\
} while(0);

#define EXIT_FUNC() 	\
do {			\
  MASTER_ER_LOG_DEBUG (ARG_FILE_LINE, "%s : exit", __func__);		\
} while(0);

typedef struct hb_deactivate_info HB_DEACTIVATE_INFO;
struct hb_deactivate_info
{
  int *server_pid_list;
  int server_count;
  bool info_started;
};

/* list */
static void hb_list_add (HB_LIST **p, HB_LIST *n);
static void hb_list_remove (HB_LIST *n);

/* jobs */
static void hb_add_timeval (struct timeval *tv_p, unsigned int msec);
static int hb_compare_timeval (struct timeval *arg1, struct timeval *arg2);
static const char *hb_strtime (char *s, unsigned int max, struct timeval *tv_p);

static int hb_job_queue (HB_JOB *jobs, unsigned int job_type, HB_JOB_ARG *arg, unsigned int msec);
static HB_JOB_ENTRY *hb_job_dequeue (HB_JOB *jobs);
static void hb_job_set_expire_and_reorder (HB_JOB *jobs, unsigned int job_type, unsigned int msec);
static void hb_job_shutdown (HB_JOB *jobs);

/* cluster jobs */
static void hb_cluster_job_init (HB_JOB_ARG *arg);
static void hb_cluster_job_heartbeat (HB_JOB_ARG *arg);
static void hb_cluster_job_calc_score (HB_JOB_ARG *arg);
static void hb_cluster_job_failover (HB_JOB_ARG *arg);
static void hb_cluster_job_failback (HB_JOB_ARG *arg);
static void hb_cluster_job_check_ping (HB_JOB_ARG *arg);
static void hb_cluster_job_check_valid_ping_server (HB_JOB_ARG *arg);
static void hb_cluster_job_demote (HB_JOB_ARG *arg);

static void hb_cluster_request_heartbeat_to_all (void);
static void hb_cluster_send_heartbeat_req (const char *dest_host_name);
static void hb_cluster_send_heartbeat_resp (struct sockaddr_in *saddr, socklen_t saddr_len, char *dest_host_name);
static void hb_cluster_send_heartbeat_internal (struct sockaddr_in *saddr, socklen_t saddr_len,
    const char *dest_host_name, bool is_req);

static void hb_cluster_receive_heartbeat (char *buffer, int len, struct sockaddr_in *from, socklen_t from_len);
static bool hb_cluster_is_isolated (void);
static bool hb_cluster_is_received_heartbeat_from_all (void);

static int hb_cluster_calc_score (void);

static void hb_set_net_header (HBP_HEADER *header, unsigned char type, bool is_req, unsigned short len,
			       unsigned int seq, const char *dest_host_name);
static int hb_hostname_to_sin_addr (const char *host, struct in_addr *addr);
static int hb_hostname_n_port_to_sockaddr (const char *host, int port, struct sockaddr *saddr, socklen_t *slen);

/* cluster jobs queue */
static HB_JOB_ENTRY *hb_cluster_job_dequeue (void);
static int hb_cluster_job_queue (unsigned int job_type, HB_JOB_ARG *arg, unsigned int msec);
static int hb_cluster_job_set_expire_and_reorder (unsigned int job_type, unsigned int msec);
static void hb_cluster_job_shutdown (void);

/* cluster node */
static cubhb::node_entry *hb_return_node_by_name_except_me (char *name);

static int hb_is_heartbeat_valid (char *host_name, char *group_id, struct sockaddr_in *from);
static const char *hb_valid_result_string (int v_result);

/* resource jobs */
static void hb_resource_job_proc_start (HB_JOB_ARG *arg);
static void hb_resource_job_proc_dereg (HB_JOB_ARG *arg);
static void hb_resource_job_confirm_start (HB_JOB_ARG *arg);
static void hb_resource_job_confirm_dereg (HB_JOB_ARG *arg);
static void hb_resource_job_change_mode (HB_JOB_ARG *arg);
static void hb_resource_job_demote_start_shutdown (HB_JOB_ARG *arg);
static void hb_resource_job_demote_confirm_shutdown (HB_JOB_ARG *arg);
static void hb_resource_job_cleanup_all (HB_JOB_ARG *arg);
static void hb_resource_job_confirm_cleanup_all (HB_JOB_ARG *arg);
static void hb_resource_job_send_master_hostname (HB_JOB_ARG *arg);

static void hb_resource_demote_start_shutdown_server_proc (void);
static bool hb_resource_demote_confirm_shutdown_server_proc (void);
static void hb_resource_demote_kill_server_proc (void);

/* resource job queue */
static HB_JOB_ENTRY *hb_resource_job_dequeue (void);
static int hb_resource_job_queue (unsigned int job_type, HB_JOB_ARG *arg, unsigned int msec);
static int hb_resource_job_set_expire_and_reorder (unsigned int job_type, unsigned int msec);

static void hb_resource_job_shutdown (void);

/* resource process */
static HB_PROC_ENTRY *hb_alloc_new_proc (void);
static void hb_remove_proc (HB_PROC_ENTRY *entry_p);
static void hb_remove_all_procs (HB_PROC_ENTRY *first);

static HB_PROC_ENTRY *hb_return_proc_by_args (char *args);
static HB_PROC_ENTRY *hb_return_proc_by_pid (int pid);
static HB_PROC_ENTRY *hb_return_proc_by_fd (int sfd);
static void hb_proc_make_arg (char **arg, char *argv);
static HB_JOB_ARG *hb_deregister_process (HB_PROC_ENTRY *proc);
#if defined (ENABLE_UNUSED_FUNCTION)
static void hb_deregister_nodes (char *node_to_dereg);
#endif /* ENABLE_UNUSED_FUNCTION */

/* resource process connection */
static int hb_resource_send_changemode (HB_PROC_ENTRY *proc);
static void hb_resource_send_get_eof (void);
static bool hb_resource_check_server_log_grow (void);

/* cluster/resource threads */
static void *hb_thread_cluster_worker (void *arg);
static void *hb_thread_cluster_reader (void *arg);
static void *hb_thread_resource_worker (void *arg);
static void *hb_thread_check_disk_failure (void *arg);

/* initializer */
static int hb_cluster_initialize ();
static int hb_cluster_job_initialize (void);
static int hb_resource_initialize (void);
static int hb_resource_job_initialize (void);
static int hb_thread_initialize (void);

/* terminator */
static void hb_resource_cleanup (void);
static void hb_resource_shutdown_all_ha_procs (void);
static void hb_cluster_cleanup (void);
static void hb_kill_process (pid_t *pids, int count);

/* process command */
static const char *hb_node_state_string (cubhb::node_entry::node_state nstate);
static const char *hb_process_state_string (unsigned char ptype, int pstate);
static const char *hb_ping_result_string (int ping_result);

static int hb_help_sprint_processes_info (char *buffer, int max_length);
static int hb_help_sprint_nodes_info (char *buffer, int max_length);
static int hb_help_sprint_jobs_info (HB_JOB *jobs, char *buffer, int max_length);
static int hb_help_sprint_ping_host_info (char *buffer, int max_length);

cubhb::cluster *hb_Cluster = NULL;
HB_RESOURCE *hb_Resource = NULL;
HB_JOB *cluster_Jobs = NULL;
HB_JOB *resource_Jobs = NULL;
bool hb_Deactivate_immediately = false;

static char hb_Nolog_event_msg[LINE_MAX] = "";

static HB_DEACTIVATE_INFO hb_Deactivate_info = { NULL, 0, false };

static bool hb_Is_activated = true;
static char *current_master_hostname = NULL;

/* cluster jobs */
static HB_JOB_FUNC hb_cluster_jobs[] =
{
  hb_cluster_job_init,
  hb_cluster_job_heartbeat,
  hb_cluster_job_calc_score,
  hb_cluster_job_check_ping,
  hb_cluster_job_failover,
  hb_cluster_job_failback,
  hb_cluster_job_check_valid_ping_server,
  hb_cluster_job_demote,
  NULL
};

/* resource jobs */
static HB_JOB_FUNC hb_resource_jobs[] =
{
  hb_resource_job_proc_start,
  hb_resource_job_proc_dereg,
  hb_resource_job_confirm_start,
  hb_resource_job_confirm_dereg,
  hb_resource_job_change_mode,
  hb_resource_job_demote_start_shutdown,
  hb_resource_job_demote_confirm_shutdown,
  hb_resource_job_cleanup_all,
  hb_resource_job_confirm_cleanup_all,
  hb_resource_job_send_master_hostname,
  NULL
};

#define HA_NODE_INFO_FORMAT_STRING                " HA-Node Info (current %s, state %s)\n"
#define HA_NODE_FORMAT_STRING                     "   Node %s (priority %d, state %s)\n"
#define HA_UI_NODE_FORMAT_STRING                  "   * Node %s (ip %s, group %s, state %s)\n"
#define HA_NODE_SCORE_FORMAT_STRING               "    - score %d\n"
#define HA_NODE_HEARTBEAT_GAP_FORMAT_STRING       "    - missed heartbeat %d\n"

#define HA_PROCESS_INFO_FORMAT_STRING             " HA-Process Info (master %d, state %s)\n"
#define HA_SERVER_PROCESS_FORMAT_STRING           "   Server %s (pid %d, state %s)\n"
#define HA_COPYLOG_PROCESS_FORMAT_STRING          "   Copylogdb %s (pid %d, state %s)\n"
#define HA_APPLYLOG_PROCESS_FORMAT_STRING         "   Applylogdb %s (pid %d, state %s)\n"
#define HA_PROCESS_EXEC_PATH_FORMAT_STRING        "    - exec-path [%s] \n"
#define HA_PROCESS_ARGV_FORMAT_STRING             "    - argv      [%s] \n"
#define HA_PROCESS_REGISTER_TIME_FORMAT_STRING    "    - registered-time   %s\n"
#define HA_PROCESS_DEREGISTER_TIME_FORMAT_STRING  "    - deregistered-time %s\n"
#define HA_PROCESS_SHUTDOWN_TIME_FORMAT_STRING    "    - shutdown-time     %s\n"
#define HA_PROCESS_START_TIME_FORMAT_STRING       "    - start-time        %s\n"

#define HA_PING_HOSTS_INFO_FORMAT_STRING          " HA-Ping Host Info (PING check %s)\n"
#define HA_PING_HOSTS_FORMAT_STRING               "   %-20s %s\n"

#define HA_ADMIN_INFO_FORMAT_STRING               " HA-Admin Info\n"
#define HA_ADMIN_INFO_NOLOG_FORMAT_STRING         "  Error Logging: disabled\n"
#define HA_ADMIN_INFO_NOLOG_EVENT_FORMAT_STRING   "    %s\n"

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
hb_list_add (HB_LIST **p, HB_LIST *n)
{
  n->next = * (p);
  if (n->next)
    {
      n->next->prev = & (n->next);
    }
  n->prev = p;
  * (p) = n;
}

/*
 * hb_list_remove() -
 *   return: none
 *   entry(in):
 */
static void
hb_list_remove (HB_LIST *n)
{
  if (n->prev)
    {
      * (n->prev) = n->next;
      if (* (n->prev))
	{
	  n->next->prev = n->prev;
	}
    }
  n->next = NULL;
  n->prev = NULL;
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
  if (tv_p == NULL)
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
  if (arg1 == NULL && arg2 == NULL)
    {
      return 0;
    }
  if (arg1 == NULL)
    {
      return -1;
    }
  if (arg2 == NULL)
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
static const char *
hb_strtime (char *s, unsigned int max, struct timeval *tv_p)
{
  struct tm hb_tm, *hb_tm_p = &hb_tm;

  if (s == NULL || max < 24 || tv_p == NULL || tv_p->tv_sec == 0)
    {
      goto error_return;
    }
  *s = '\0';

  hb_tm_p = localtime_r (&tv_p->tv_sec, &hb_tm);

  if (hb_tm_p == NULL)
    {
      goto error_return;
    }

  snprintf (s + strftime (s, (max - 5), "%m/%d/%y %H:%M:%S", hb_tm_p), (max - 1), ".%03ld", tv_p->tv_usec / 1000);
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
hb_job_queue (HB_JOB *jobs, unsigned int job_type, HB_JOB_ARG *arg, unsigned int msec)
{
  HB_JOB_ENTRY **job;
  HB_JOB_ENTRY *new_job;
  struct timeval now;
  int rv;

  new_job = (HB_JOB_ENTRY *) malloc (sizeof (HB_JOB_ENTRY));
  if (new_job == NULL)
    {
      MASTER_ER_SET (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (HB_JOB_ENTRY));
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  gettimeofday (&now, NULL);
  hb_add_timeval (&now, msec);

  new_job->prev = NULL;
  new_job->next = NULL;
  new_job->type = job_type;
  new_job->func = jobs->job_funcs[job_type];
  new_job->arg = arg;
  memcpy ((void *) & (new_job->expire), (void *) &now, sizeof (struct timeval));

  rv = pthread_mutex_lock (&jobs->lock);
  for (job = & (jobs->jobs); *job; job = & ((*job)->next))
    {
      /*
       * compare expire time of new job and current job
       * until new job's expire is larger than current's
       */
      if (hb_compare_timeval (& ((*job)->expire), &now) <= 0)
	{
	  continue;
	}
      break;
    }
  hb_list_add ((HB_LIST **) job, (HB_LIST *) new_job);

  pthread_mutex_unlock (&jobs->lock);
  return NO_ERROR;
}

/*
 * hb_job_dequeue() - dequeue a job from queue expiration time
 *                    is smaller than current time
 *   return: pointer to heartbeat job entry
 *
 *   jobs(in):
 */
static HB_JOB_ENTRY *
hb_job_dequeue (HB_JOB *jobs)
{
  struct timeval now;
  HB_JOB_ENTRY *job;
  int rv;

  gettimeofday (&now, NULL);

  rv = pthread_mutex_lock (&jobs->lock);
  if (jobs->shutdown)
    {
      pthread_mutex_unlock (&jobs->lock);
      return NULL;
    }

  job = jobs->jobs;
  if (job == NULL)
    {
      pthread_mutex_unlock (&jobs->lock);
      return NULL;
    }

  if (hb_compare_timeval (&now, &job->expire) >= 0)
    {
      hb_list_remove ((HB_LIST *) job);
    }
  else
    {
      pthread_mutex_unlock (&jobs->lock);
      return NULL;
    }
  pthread_mutex_unlock (&jobs->lock);

  return job;
}

/*
 * hb_job_set_expire_and_reorder - set expiration time of the first job which match job_type
 *                                 reorder job with expiration time changed
 *   return: none
 *
 *   jobs(in):
 *   job_type(in):
 *   msec(in):
 */
static void
hb_job_set_expire_and_reorder (HB_JOB *jobs, unsigned int job_type, unsigned int msec)
{
  HB_JOB_ENTRY **job = NULL;
  HB_JOB_ENTRY *target_job = NULL;
  struct timeval now;

  gettimeofday (&now, NULL);
  hb_add_timeval (&now, msec);

  pthread_mutex_lock (&jobs->lock);

  if (jobs->shutdown)
    {
      pthread_mutex_unlock (&jobs->lock);
      return;
    }

  for (job = & (jobs->jobs); *job; job = & ((*job)->next))
    {
      if ((*job)->type == job_type)
	{
	  target_job = *job;
	  break;
	}
    }

  if (target_job == NULL)
    {
      pthread_mutex_unlock (&jobs->lock);
      return;
    }

  memcpy ((void *) & (target_job->expire), (void *) &now, sizeof (struct timeval));

  /*
   * so now we change target job's turn to adjust sorted queue
   */
  hb_list_remove ((HB_LIST *) target_job);

  for (job = & (jobs->jobs); *job; job = & ((*job)->next))
    {
      /*
       * compare expiration time of target job and current job
       * until target job's expire is larger than current's
       */
      if (hb_compare_timeval (& ((*job)->expire), & (target_job->expire)) > 0)
	{
	  break;
	}
    }

  hb_list_add ((HB_LIST **) job, (HB_LIST *) target_job);

  pthread_mutex_unlock (&jobs->lock);
}

/*
 * hb_job_shutdown() - clear job queue and stop job worker thread
 *   return: none
 *
 *   jobs(in):
 */
static void
hb_job_shutdown (HB_JOB *jobs)
{
  int rv;
  HB_JOB_ENTRY *job, *job_next;

  rv = pthread_mutex_lock (&jobs->lock);
  for (job = jobs->jobs; job; job = job_next)
    {
      job_next = job->next;

      hb_list_remove ((HB_LIST *) job);
      free_and_init (job);
    }
  jobs->shutdown = true;
  pthread_mutex_unlock (&jobs->lock);
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
hb_cluster_job_init (HB_JOB_ARG *arg)
{
  int error;

  error = hb_cluster_job_queue (HB_CJOB_HEARTBEAT, NULL, HB_JOB_TIMER_IMMEDIATELY);
  assert (error == NO_ERROR);

  error = hb_cluster_job_queue (HB_CJOB_CHECK_VALID_PING_SERVER, NULL, HB_JOB_TIMER_IMMEDIATELY);
  assert (error == NO_ERROR);

  error = hb_cluster_job_queue (HB_CJOB_CALC_SCORE, NULL, prm_get_integer_value (PRM_ID_HA_INIT_TIMER_IN_MSECS));
  assert (error == NO_ERROR);

  if (arg)
    {
      free_and_init (arg);
    }
}

/*
 * hb_cluster_job_heartbeat() - send heartbeat request to other nodes
 *   return: none
 *
 *   jobs(in):
 */
static void
hb_cluster_job_heartbeat (HB_JOB_ARG *arg)
{
  int error, rv;

  rv = pthread_mutex_lock (&hb_Cluster->lock);

  if (!hb_Cluster->hide_to_demote)
    {
      hb_cluster_request_heartbeat_to_all ();
    }

  pthread_mutex_unlock (&hb_Cluster->lock);
  error = hb_cluster_job_queue (HB_CJOB_HEARTBEAT, NULL, prm_get_integer_value (PRM_ID_HA_HEARTBEAT_INTERVAL_IN_MSECS));
  assert (error == NO_ERROR);

  if (arg)
    {
      free_and_init (arg);
    }
}

/*
 * hb_cluster_is_isolated() -
 *   return: whether current node is isolated or not
 *
 */
static bool
hb_cluster_is_isolated (void)
{
  for (cubhb::node_entry *node : hb_Cluster->nodes)
    {
      if (node->state == cubhb::node_entry::REPLICA)
	{
	  continue;
	}

      if (hb_Cluster->myself != node && node->state != cubhb::node_entry::UNKNOWN)
	{
	  return false;
	}
    }

  return true;
}

/*
 * hb_cluster_is_received_heartbeat_from_all() -
 *   return: whether current node received heartbeat from all node
 */
static bool
hb_cluster_is_received_heartbeat_from_all (void)
{
  struct timeval now;
  unsigned int heartbeat_confirm_time;

  heartbeat_confirm_time = prm_get_integer_value (PRM_ID_HA_HEARTBEAT_INTERVAL_IN_MSECS);

  gettimeofday (&now, NULL);

  for (cubhb::node_entry *node : hb_Cluster->nodes)
    {
      if (hb_Cluster->myself != node && HB_GET_ELAPSED_TIME (now, node->last_recv_hbtime) > heartbeat_confirm_time)
	{
	  return false;
	}
    }
  return true;
}

/*
 * hb_cluster_job_calc_score() -
 *   return: none
 *
 *   jobs(in):
 */
static void
hb_cluster_job_calc_score (HB_JOB_ARG *arg)
{
  int error, rv;
  int num_master;
  unsigned int failover_wait_time;
  HB_JOB_ARG *job_arg;
  HB_CLUSTER_JOB_ARG *clst_arg;
  char hb_info_str[HB_INFO_STR_MAX];

  ENTER_FUNC ();

  rv = pthread_mutex_lock (&hb_Cluster->lock);

  num_master = hb_cluster_calc_score ();
  hb_Cluster->is_isolated = hb_cluster_is_isolated ();

  if (hb_Cluster->state == cubhb::node_entry::REPLICA || hb_Cluster->hide_to_demote)
    {
      goto calc_end;
    }

  /* case : check whether master has been isolated */
  if (hb_Cluster->state == cubhb::node_entry::MASTER)
    {
      if (hb_Cluster->is_isolated)
	{
	  /* check ping if Ping host exist */
	  pthread_mutex_unlock (&hb_Cluster->lock);

	  job_arg = (HB_JOB_ARG *) malloc (sizeof (HB_JOB_ARG));
	  if (job_arg)
	    {
	      clst_arg = & (job_arg->cluster_job_arg);
	      clst_arg->ping_check_count = 0;
	      clst_arg->retries = 0;

	      error = hb_cluster_job_queue (HB_CJOB_CHECK_PING, job_arg, HB_JOB_TIMER_IMMEDIATELY);
	      assert (error == NO_ERROR);
	    }

	  if (arg)
	    {
	      free_and_init (arg);
	    }

	  return;
	}
    }

  /* case : split-brain */
  if ((num_master > 1)
      && (hb_Cluster->master && hb_Cluster->myself && hb_Cluster->myself->state == cubhb::node_entry::MASTER
	  && hb_Cluster->master->priority != hb_Cluster->myself->priority))
    {
      MASTER_ER_SET (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HB_NODE_EVENT, 1,
		     "More than one master detected and failback will be initiated");

      hb_help_sprint_nodes_info (hb_info_str, HB_INFO_STR_MAX);
      MASTER_ER_SET (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HB_NODE_EVENT, 1, hb_info_str);

      if (!hb_Cluster->ping_hosts.empty ())
	{
	  hb_help_sprint_ping_host_info (hb_info_str, HB_INFO_STR_MAX);
	  MASTER_ER_SET (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HB_NODE_EVENT, 1, hb_info_str);
	}

      pthread_mutex_unlock (&hb_Cluster->lock);

      error = hb_cluster_job_queue (HB_CJOB_FAILBACK, NULL, HB_JOB_TIMER_IMMEDIATELY);
      assert (error == NO_ERROR);

      if (arg)
	{
	  free_and_init (arg);
	}

      return;
    }

  /* case : failover */
  if ((hb_Cluster->state == cubhb::node_entry::SLAVE)
      && (hb_Cluster->master && hb_Cluster->myself && hb_Cluster->master->priority == hb_Cluster->myself->priority))
    {
      hb_Cluster->state = cubhb::node_entry::TO_BE_MASTER;
      hb_cluster_request_heartbeat_to_all ();

      pthread_mutex_unlock (&hb_Cluster->lock);

      job_arg = (HB_JOB_ARG *) malloc (sizeof (HB_JOB_ARG));
      if (job_arg)
	{
	  clst_arg = & (job_arg->cluster_job_arg);
	  clst_arg->ping_check_count = 0;

	  error = hb_cluster_job_queue (HB_CJOB_CHECK_PING, job_arg, HB_JOB_TIMER_WAIT_100_MILLISECOND);
	  assert (error == NO_ERROR);
	}
      else
	{
	  SLEEP_MILISEC (0, HB_JOB_TIMER_WAIT_100_MILLISECOND);

	  if (hb_cluster_is_received_heartbeat_from_all ())
	    {
	      failover_wait_time = HB_JOB_TIMER_WAIT_500_MILLISECOND;
	    }
	  else
	    {
	      /* If current node didn't receive heartbeat from some node, wait for some time */
	      failover_wait_time = prm_get_integer_value (PRM_ID_HA_FAILOVER_WAIT_TIME_IN_MSECS);
	    }

	  error = hb_cluster_job_queue (HB_CJOB_FAILOVER, NULL, failover_wait_time);
	  assert (error == NO_ERROR);

	  MASTER_ER_SET (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HB_NODE_EVENT, 1,
			 "A failover attempted to make the current node a master");
	}

      if (arg)
	{
	  free_and_init (arg);
	}

      return;
    }

  hb_help_sprint_nodes_info (hb_info_str, HB_INFO_STR_MAX);
  MASTER_ER_LOG_DEBUG (ARG_FILE_LINE, "%s", hb_info_str);

calc_end:
  pthread_mutex_unlock (&hb_Cluster->lock);

  error =
	  hb_cluster_job_queue (HB_CJOB_CALC_SCORE, NULL, prm_get_integer_value (PRM_ID_HA_CALC_SCORE_INTERVAL_IN_MSECS));
  assert (error == NO_ERROR);

  if (arg)
    {
      free_and_init (arg);
    }
}

/*
 * hb_cluster_job_check_ping() -
 *   return: none
 *
 *   jobs(in):
 */
static void
hb_cluster_job_check_ping (HB_JOB_ARG *arg)
{
  int error, rv;
  int ping_try_count = 0;
  bool ping_success = false;
  int ping_result;
  unsigned int failover_wait_time;
  HB_CLUSTER_JOB_ARG *clst_arg = (arg) ? & (arg->cluster_job_arg) : NULL;

  ENTER_FUNC ();

  rv = pthread_mutex_lock (&hb_Cluster->lock);

  if (clst_arg == NULL || hb_Cluster->ping_hosts.empty () || !hb_Cluster->is_ping_check_enabled)
    {
      /* If Ping Host is either empty or marked invalid, MASTER->MASTER, SLAVE->MASTER. It may cause split-brain
       * problem. */
      if (hb_Cluster->state == cubhb::node_entry::MASTER)
	{
	  goto ping_check_cancel;
	}
    }
  else
    {
      for (cubhb::ping_host *host : hb_Cluster->ping_hosts)
	{
	  ping_result = hb_check_ping (host->get_hostname_cstr ());

	  host->ping_result = ping_result;
	  if (ping_result == cubhb::ping_host::SUCCESS)
	    {
	      ping_try_count++;
	      ping_success = true;
	      break;
	    }
	  else if (ping_result == cubhb::ping_host::FAILURE)
	    {
	      ping_try_count++;
	    }
	}

      if (hb_Cluster->state == cubhb::node_entry::MASTER)
	{
	  if (ping_try_count == 0 || ping_success)
	    {
	      goto ping_check_cancel;
	    }
	}
      else
	{
	  if (ping_try_count > 0 && !ping_success)
	    {
	      goto ping_check_cancel;
	    }
	}

      if ((++clst_arg->ping_check_count) < HB_MAX_PING_CHECK)
	{
	  /* Try ping test again */
	  pthread_mutex_unlock (&hb_Cluster->lock);

	  error = hb_cluster_job_queue (HB_CJOB_CHECK_PING, arg, HB_JOB_TIMER_IMMEDIATELY);
	  assert (error == NO_ERROR);

	  return;
	}
    }

  /* Now, we have tried ping test over HB_MAX_PING_CHECK times. (or Slave's ping host is either empty or invalid.) So,
   * we can determine this node's next job (failover or failback). */

  hb_cluster_request_heartbeat_to_all ();

  pthread_mutex_unlock (&hb_Cluster->lock);

  if (hb_Cluster->state == cubhb::node_entry::MASTER)
    {
      /* If this node is Master, do failback */
      error = hb_cluster_job_queue (HB_CJOB_FAILBACK, NULL, HB_JOB_TIMER_IMMEDIATELY);
      assert (error == NO_ERROR);
    }
  else
    {
      /* If this node is Slave, do failover */
      if (hb_cluster_is_received_heartbeat_from_all ())
	{
	  failover_wait_time = HB_JOB_TIMER_WAIT_500_MILLISECOND;
	}
      else
	{
	  /* If current node didn't receive heartbeat from some node, wait for some time */
	  failover_wait_time = prm_get_integer_value (PRM_ID_HA_FAILOVER_WAIT_TIME_IN_MSECS);
	}
      error = hb_cluster_job_queue (HB_CJOB_FAILOVER, NULL, failover_wait_time);
      assert (error == NO_ERROR);
    }

  if (arg)
    {
      free_and_init (arg);
    }

  EXIT_FUNC ();

  return;

ping_check_cancel:
  /* if this node is a master, then failback is cancelled */

  if (hb_Cluster->state != cubhb::node_entry::MASTER)
    {
      MASTER_ER_SET (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_HB_NODE_EVENT, 1, "Failover cancelled by ping check");
      hb_Cluster->state = cubhb::node_entry::SLAVE;
    }
  hb_cluster_request_heartbeat_to_all ();

  pthread_mutex_unlock (&hb_Cluster->lock);

  /* do calc_score job again */
  error =
	  hb_cluster_job_queue (HB_CJOB_CALC_SCORE, NULL, prm_get_integer_value (PRM_ID_HA_CALC_SCORE_INTERVAL_IN_MSECS));
  assert (error == NO_ERROR);

  if (arg)
    {
      free_and_init (arg);
    }

  EXIT_FUNC ();
}

/*
 * hb_cluster_job_failover() -
 *   return: none
 *
 *   jobs(in):
 */
static void
hb_cluster_job_failover (HB_JOB_ARG *arg)
{
  int error;
  char hb_info_str[HB_INFO_STR_MAX];

  ENTER_FUNC ();

  pthread_mutex_lock (&hb_Cluster->lock);

  hb_cluster_calc_score ();

  if (hb_Cluster->master && hb_Cluster->myself && hb_Cluster->master->priority == hb_Cluster->myself->priority)
    {
      MASTER_ER_SET (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HB_NODE_EVENT, 1, "Failover completed");
      hb_Cluster->state = cubhb::node_entry::MASTER;
      hb_Resource->state = cubhb::node_entry::MASTER;

      error = hb_resource_job_set_expire_and_reorder (HB_RJOB_CHANGE_MODE, HB_JOB_TIMER_IMMEDIATELY);
      assert (error == NO_ERROR);
    }
  else
    {
      MASTER_ER_SET (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HB_NODE_EVENT, 1, "Failover cancelled");
      hb_Cluster->state = cubhb::node_entry::SLAVE;
    }

  hb_help_sprint_nodes_info (hb_info_str, HB_INFO_STR_MAX);
  MASTER_ER_SET (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HB_NODE_EVENT, 1, hb_info_str);

  if (!hb_Cluster->ping_hosts.empty ())
    {
      hb_help_sprint_ping_host_info (hb_info_str, HB_INFO_STR_MAX);
      MASTER_ER_SET (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HB_NODE_EVENT, 1, hb_info_str);
    }

  hb_cluster_request_heartbeat_to_all ();
  pthread_mutex_unlock (&hb_Cluster->lock);

  error =
	  hb_cluster_job_queue (HB_CJOB_CALC_SCORE, NULL, prm_get_integer_value (PRM_ID_HA_CALC_SCORE_INTERVAL_IN_MSECS));
  assert (error == NO_ERROR);

  if (arg)
    {
      free_and_init (arg);
    }
}

/*
 * hb_cluster_job_demote() -
 *      it waits for new master to be elected.
 *      hb_resource_job_demote_start_shutdown must be proceeded
 *      before this job.
 *   return: none
 *
 *   arg(in):
 */
static void
hb_cluster_job_demote (HB_JOB_ARG *arg)
{
  int rv, error;
  HB_CLUSTER_JOB_ARG *clst_arg = (arg) ? & (arg->cluster_job_arg) : NULL;
  char hb_info_str[HB_INFO_STR_MAX];

  ENTER_FUNC ();

  if (arg == NULL || clst_arg == NULL)
    {
      MASTER_ER_LOG_DEBUG (ARG_FILE_LINE, "invalid arg or proc_arg. " "(arg:%p, proc_arg:%p). \n", arg, clst_arg);
      return;
    }

  rv = pthread_mutex_lock (&hb_Cluster->lock);

  if (clst_arg->retries == 0)
    {
      assert (hb_Cluster->state == cubhb::node_entry::MASTER);
      assert (hb_Resource->state == cubhb::node_entry::SLAVE);

      /* send state (cubhb::node_entry::UNKNOWN) to other nodes for making other node be master */
      hb_Cluster->state = cubhb::node_entry::UNKNOWN;
      hb_cluster_request_heartbeat_to_all ();

      MASTER_ER_SET (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HB_NODE_EVENT, 1,
		     "Waiting for a new node to be elected as master");
    }

  hb_Cluster->hide_to_demote = true;
  hb_Cluster->state = cubhb::node_entry::SLAVE;
  hb_Cluster->myself->state = hb_Cluster->state;

  if (hb_Cluster->is_isolated || ++ (clst_arg->retries) > HB_MAX_WAIT_FOR_NEW_MASTER)
    {
      MASTER_ER_SET (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HB_NODE_EVENT, 1,
		     "Failed to find a new master node and it changes " "its role back to master again");
      hb_Cluster->hide_to_demote = false;

      pthread_mutex_unlock (&hb_Cluster->lock);

      if (arg)
	{
	  free_and_init (arg);
	}
      return;
    }

  for (cubhb::node_entry *node : hb_Cluster->nodes)
    {
      if (node->state == cubhb::node_entry::MASTER)
	{
	  assert (node != hb_Cluster->myself);

	  MASTER_ER_SET (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HB_NODE_EVENT, 1, "Found a new master node");

	  hb_help_sprint_nodes_info (hb_info_str, HB_INFO_STR_MAX);
	  MASTER_ER_SET (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HB_NODE_EVENT, 1, hb_info_str);

	  if (!hb_Cluster->ping_hosts.empty ())
	    {
	      hb_help_sprint_ping_host_info (hb_info_str, HB_INFO_STR_MAX);
	      MASTER_ER_SET (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HB_NODE_EVENT, 1, hb_info_str);
	    }

	  hb_Cluster->hide_to_demote = false;

	  pthread_mutex_unlock (&hb_Cluster->lock);

	  if (arg)
	    {
	      free_and_init (arg);
	    }
	  return;
	}
    }

  pthread_mutex_unlock (&hb_Cluster->lock);

  error = hb_cluster_job_queue (HB_CJOB_DEMOTE, arg, HB_JOB_TIMER_WAIT_A_SECOND);

  if (error != NO_ERROR)
    {
      assert (false);
      free_and_init (arg);
    }
}

/*
 * hb_cluster_job_failback () -
 *   return: none
 *
 *   jobs(in):
 *
 *   NOTE: this job waits for servers to be killed.
 *   Therefore, be aware that adding this job to queue might
 *   temporarily prevent cluster_job_calc or any other cluster
 *   jobs following this one from executing at regular intervals
 *   as intended.
 */
static void
hb_cluster_job_failback (HB_JOB_ARG *arg)
{
  int error, count = 0;
  char hb_info_str[HB_INFO_STR_MAX];
  HB_PROC_ENTRY *proc;
  pid_t *pids = NULL;
  size_t size;
  bool emergency_kill_enabled = false;

  ENTER_FUNC ();

  pthread_mutex_lock (&hb_Cluster->lock);

  hb_Cluster->state = cubhb::node_entry::SLAVE;
  hb_Cluster->myself->state = hb_Cluster->state;

  hb_cluster_request_heartbeat_to_all ();

  MASTER_ER_SET (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HB_NODE_EVENT, 1,
		 "This master will become a slave and cub_server will be restarted");

  hb_help_sprint_nodes_info (hb_info_str, HB_INFO_STR_MAX);
  MASTER_ER_SET (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HB_NODE_EVENT, 1, hb_info_str);

  if (!hb_Cluster->ping_hosts.empty ())
    {
      hb_help_sprint_ping_host_info (hb_info_str, HB_INFO_STR_MAX);
      MASTER_ER_SET (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HB_NODE_EVENT, 1, hb_info_str);
    }

  pthread_mutex_unlock (&hb_Cluster->lock);

  pthread_mutex_lock (&hb_Resource->lock);
  hb_Resource->state = cubhb::node_entry::SLAVE;

  proc = hb_Resource->procs;
  while (proc)
    {
      if (proc->type != HB_PTYPE_SERVER)
	{
	  proc = proc->next;
	  continue;
	}

      if (!emergency_kill_enabled)
	{
	  size = sizeof (pid_t) * (count + 1);
	  pids = (pid_t *) realloc (pids, size);
	  if (pids == NULL)
	    {
	      MASTER_ER_SET (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, size);

	      /*
	       * in case that memory allocation fails,
	       * kill all cub_server processes with SIGKILL
	       */
	      emergency_kill_enabled = true;
	      proc = hb_Resource->procs;
	      continue;
	    }
	  pids[count++] = proc->pid;
	}
      else
	{
	  assert (proc->pid > 0);
	  if (proc->pid > 0)
	    {
	      kill (proc->pid, SIGKILL);
	    }
	}
      proc = proc->next;
    }

  pthread_mutex_unlock (&hb_Resource->lock);

  if (!emergency_kill_enabled)
    {
      hb_kill_process (pids, count);
    }

  if (pids)
    {
      free_and_init (pids);
    }

  error =
	  hb_cluster_job_queue (HB_CJOB_CALC_SCORE, NULL, prm_get_integer_value (PRM_ID_HA_CALC_SCORE_INTERVAL_IN_MSECS));
  assert (error == NO_ERROR);

  if (arg)
    {
      free_and_init (arg);
    }
}

/*
 * hb_cluster_job_check_valid_ping_server() -
 *   return: none
 *
 *   jobs(in):
 */
static void
hb_cluster_job_check_valid_ping_server (HB_JOB_ARG *arg)
{
  int error, rv;
  bool valid_ping_host_exists;
  char buf[LINE_MAX];
  int check_interval = HB_DEFAULT_CHECK_VALID_PING_SERVER_INTERVAL_IN_MSECS;

  rv = pthread_mutex_lock (&hb_Cluster->lock);

  if (hb_Cluster->ping_hosts.empty ())
    {
      goto check_end;
    }

  valid_ping_host_exists = hb_Cluster->check_valid_ping_host ();
  if (!valid_ping_host_exists && !hb_cluster_is_isolated ())
    {
      check_interval = HB_TEMP_CHECK_VALID_PING_SERVER_INTERVAL_IN_MSECS;

      if (hb_Cluster->is_ping_check_enabled)
	{
	  hb_Cluster->is_ping_check_enabled = false;
	  snprintf (buf, LINE_MAX,
		    "Validity check for PING failed on all hosts " "and PING check is now temporarily disabled.");
	  MASTER_ER_SET (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HB_NODE_EVENT, 1, buf);
	}
    }
  else if (valid_ping_host_exists)
    {
      if (!hb_Cluster->is_ping_check_enabled)
	{
	  hb_Cluster->is_ping_check_enabled = true;
	  snprintf (buf, LINE_MAX, "Validity check for PING succeeded " "and PING check is now enabled.");
	  MASTER_ER_SET (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HB_NODE_EVENT, 1, buf);
	}
    }

check_end:
  pthread_mutex_unlock (&hb_Cluster->lock);

  error = hb_cluster_job_queue (HB_CJOB_CHECK_VALID_PING_SERVER, NULL, check_interval);

  assert (error == NO_ERROR);
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
  short min_score = HB_NODE_SCORE_UNKNOWN;
  struct timeval now;

  if (hb_Cluster == NULL)
    {
      MASTER_ER_LOG_DEBUG (ARG_FILE_LINE, "hb_Cluster is null. \n");
      return ER_FAILED;
    }

  hb_Cluster->myself->state = hb_Cluster->state;
  gettimeofday (&now, NULL);

  for (cubhb::node_entry *node : hb_Cluster->nodes)
    {
      /* If this node does not receive heartbeat message over than prm_get_integer_value (PRM_ID_HA_MAX_HEARTBEAT_GAP)
       * times, (or sufficient time has been elapsed from the last received heartbeat message time), this node does not
       * know what other node state is. */
      if (node->heartbeat_gap > prm_get_integer_value (PRM_ID_HA_MAX_HEARTBEAT_GAP)
	  || (!HB_IS_INITIALIZED_TIME (node->last_recv_hbtime)
	      && HB_GET_ELAPSED_TIME (now, node->last_recv_hbtime) >
	      prm_get_integer_value (PRM_ID_HA_CALC_SCORE_INTERVAL_IN_MSECS)))
	{
	  node->heartbeat_gap = 0;
	  node->last_recv_hbtime.tv_sec = 0;
	  node->last_recv_hbtime.tv_usec = 0;
	  node->state = cubhb::node_entry::UNKNOWN;
	}

      switch (node->state)
	{
	case cubhb::node_entry::MASTER:
	case cubhb::node_entry::TO_BE_SLAVE:
	{
	  node->score = node->priority | HB_NODE_SCORE_MASTER;
	}
	break;
	case cubhb::node_entry::TO_BE_MASTER:
	{
	  node->score = node->priority | HB_NODE_SCORE_TO_BE_MASTER;
	}
	break;
	case cubhb::node_entry::SLAVE:
	{
	  node->score = node->priority | HB_NODE_SCORE_SLAVE;
	}
	break;
	case cubhb::node_entry::REPLICA:
	case cubhb::node_entry::UNKNOWN:
	default:
	{
	  node->score = node->priority | HB_NODE_SCORE_UNKNOWN;
	}
	break;
	}

      if (node->score < min_score)
	{
	  hb_Cluster->master = node;
	  min_score = node->score;
	}

      if (node->score < (short) HB_NODE_SCORE_TO_BE_MASTER)
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
  if (hb_Cluster == NULL)
    {
      MASTER_ER_LOG_DEBUG (ARG_FILE_LINE, "hb_Cluster is null. \n");
      return;
    }

  for (cubhb::node_entry *node : hb_Cluster->nodes)
    {
      if (hb_Cluster->hostname == node->get_hostname ())
	{
	  continue;
	}

      hb_cluster_send_heartbeat_req (node->get_hostname_cstr ());
      node->heartbeat_gap++;
    }
}

/*
 * hb_cluster_send_heartbeat_req() -
 *   return: none
 *
 *   host_name(in):
 */
static void
hb_cluster_send_heartbeat_req (const char *dest_host_name)
{
  struct sockaddr_in saddr;
  socklen_t saddr_len;

  /* construct destination address */
  memset ((void *) &saddr, 0, sizeof (saddr));
  if (hb_hostname_n_port_to_sockaddr (dest_host_name, prm_get_integer_value (PRM_ID_HA_PORT_ID),
				      (struct sockaddr *) &saddr, &saddr_len) != NO_ERROR)
    {
      MASTER_ER_LOG_DEBUG (ARG_FILE_LINE, "hb_hostname_n_port_to_sockaddr failed. \n");
      return;
    }

  hb_cluster_send_heartbeat_internal (&saddr, saddr_len, dest_host_name, true);
}

static void
hb_cluster_send_heartbeat_resp (struct sockaddr_in *saddr, socklen_t saddr_len, char *dest_host_name)
{
  hb_cluster_send_heartbeat_internal (saddr, saddr_len, dest_host_name, false);
}

static void
hb_cluster_send_heartbeat_internal (struct sockaddr_in *saddr, socklen_t saddr_len, const char *dest_host_name,
				    bool is_req)
{
  HBP_HEADER *hbp_header;
  char buffer[HB_BUFFER_SZ], *p;
  size_t hb_len;
  int send_len;

  memset ((void *) buffer, 0, sizeof (buffer));
  hbp_header = (HBP_HEADER *) (&buffer[0]);

  hb_set_net_header (hbp_header, HBP_CLUSTER_HEARTBEAT, is_req, OR_INT_SIZE, 0, dest_host_name);

  p = (char *) (hbp_header + 1);
  p = or_pack_int (p, hb_Cluster->state);

  hb_len = sizeof (HBP_HEADER) + OR_INT_SIZE;

  send_len = sendto (hb_Cluster->sfd, (void *) &buffer[0], hb_len, 0, (struct sockaddr *) saddr, saddr_len);
  if (send_len <= 0)
    {
      MASTER_ER_LOG_DEBUG (ARG_FILE_LINE, "sendto failed. \n");
      /* TODO : error */
    }
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
hb_cluster_receive_heartbeat (char *buffer, int len, struct sockaddr_in *from, socklen_t from_len)
{
  int rv;
  HBP_HEADER *hbp_header;
  cubhb::node_entry *node;
  cubhb::ui_node *ui_node;
  char error_string[LINE_MAX] = "";
  char *p;

  int state = 0;		/* cubhb::node_entry::node_state */
  bool is_state_changed = false;

  hbp_header = (HBP_HEADER *) (buffer);

  rv = pthread_mutex_lock (&hb_Cluster->lock);
  if (hb_Cluster->shutdown)
    {
      pthread_mutex_unlock (&hb_Cluster->lock);
      return;
    }

  /* validate receive message */
  if (strcmp (hb_Cluster->hostname.c_str (), hbp_header->dest_host_name) != 0)
    {
      MASTER_ER_LOG_DEBUG (ARG_FILE_LINE, "hostname mismatch. " "(host_name:{%s}, dest_host_name:{%s}).\n",
			   hb_Cluster->hostname.c_str (), hbp_header->dest_host_name);
      pthread_mutex_unlock (&hb_Cluster->lock);
      return;
    }

  if (len != (int) (sizeof (*hbp_header) + htons (hbp_header->len)))
    {
      MASTER_ER_LOG_DEBUG (ARG_FILE_LINE, "size mismatch. " "(len:%d, msg_size:%d).\n", len,
			   (sizeof (*hbp_header) + htons (hbp_header->len)));
      pthread_mutex_unlock (&hb_Cluster->lock);
      return;
    }

#if 0
  MASTER_ER_LOG_DEBUG (ARG_FILE_LINE,
		       "hbp_header. (type:%d, r:%d, len:%d, seq:%d, " "orig_host_name:{%s}, dest_host_name:{%s}). \n",
		       hbp_header->type, (hbp_header->r) ? 1 : 0, ntohs (hbp_header->len), ntohl (hbp_header->seq),
		       hbp_header->orig_host_name, hbp_header->dest_host_name);
#endif

  switch (hbp_header->type)
    {
    case HBP_CLUSTER_HEARTBEAT:
    {
      cubhb::node_entry::node_state hb_state;

      p = (char *) (hbp_header + 1);
      or_unpack_int (p, &state);

      hb_state = (cubhb::node_entry::node_state) state;

      if (hb_state < cubhb::node_entry::UNKNOWN || hb_state >= cubhb::node_entry::MAX)
	{
	  MASTER_ER_LOG_DEBUG (ARG_FILE_LINE, "receive heartbeat have unknown state. " "(state:%u).\n", state);
	  pthread_mutex_unlock (&hb_Cluster->lock);
	  return;
	}

      rv = hb_is_heartbeat_valid (hbp_header->orig_host_name, hbp_header->group_id, from);
      if (rv != HB_VALID_NO_ERROR)
	{
	  ui_node = hb_Cluster->find_ui_node (hbp_header->orig_host_name, hbp_header->group_id, *from);

	  if (ui_node && ui_node->v_result != rv)
	    {
	      hb_Cluster->remove_ui_node (ui_node);
	    }

	  if (ui_node == NULL)
	    {
	      char *ipv4_p;

	      ipv4_p = (char *) &from->sin_addr.s_addr;
	      snprintf (error_string, sizeof (error_string),
			"Receive heartbeat from unidentified host. " "(host_name:'%s', group:'%s', "
			"ip_addr:'%u.%u.%u.%u', state:'%s')", hbp_header->orig_host_name, hbp_header->group_id,
			(unsigned char) (ipv4_p[0]), (unsigned char) (ipv4_p[1]), (unsigned char) (ipv4_p[2]),
			(unsigned char) (ipv4_p[3]), hb_valid_result_string (rv));
	      MASTER_ER_SET (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HB_NODE_EVENT, 1, error_string);

	      hb_Cluster->insert_ui_node (hbp_header->orig_host_name, hbp_header->group_id, *from, rv);
	    }
	  else
	    {
	      ui_node->set_last_recv_time_to_now ();
	    }
	}

      /*
       * if heartbeat group id is mismatch, ignore heartbeat
       */
      if (strcmp (hbp_header->group_id, hb_Cluster->group_id.c_str ()) != 0)
	{
	  pthread_mutex_unlock (&hb_Cluster->lock);
	  return;
	}

      /*
       * must send heartbeat response in order to avoid split-brain
       * when heartbeat configuration changed
       */
      if (hbp_header->r && !hb_Cluster->hide_to_demote)
	{
	  hb_cluster_send_heartbeat_resp (from, from_len, hbp_header->orig_host_name);
	}

      node = hb_return_node_by_name_except_me (hbp_header->orig_host_name);
      if (node)
	{
	  if (node->state == cubhb::node_entry::MASTER && node->state != hb_state)
	    {
	      is_state_changed = true;
	    }

	  node->state = hb_state;
	  node->heartbeat_gap = MAX (0, (node->heartbeat_gap - 1));
	  gettimeofday (&node->last_recv_hbtime, NULL);
	}
      else
	{
	  MASTER_ER_LOG_DEBUG (ARG_FILE_LINE, "receive heartbeat have unknown host_name. " "(host_name:{%s}).\n",
			       hbp_header->orig_host_name);
	}
    }
    break;
    default:
    {
      MASTER_ER_LOG_DEBUG (ARG_FILE_LINE, "unknown heartbeat message. " "(type:%d). \n", hbp_header->type);
    }
    break;

    }

  pthread_mutex_unlock (&hb_Cluster->lock);

  if (is_state_changed)
    {
      MASTER_ER_LOG_DEBUG (ARG_FILE_LINE, "peer node state has been changed.");
      hb_cluster_job_set_expire_and_reorder (HB_CJOB_CALC_SCORE, HB_JOB_TIMER_IMMEDIATELY);
    }
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
hb_set_net_header (HBP_HEADER *header, unsigned char type, bool is_req, unsigned short len, unsigned int seq,
		   const char *dest_host_name)
{
  header->type = type;
  header->r = (is_req) ? 1 : 0;
  header->len = htons (len);
  header->seq = htonl (seq);
  strncpy (header->group_id, hb_Cluster->group_id.c_str (), sizeof (header->group_id) - 1);
  header->group_id[sizeof (header->group_id) - 1] = '\0';
  strncpy (header->dest_host_name, dest_host_name, sizeof (header->dest_host_name) - 1);
  header->dest_host_name[sizeof (header->dest_host_name) - 1] = '\0';
  strncpy (header->orig_host_name, hb_Cluster->hostname.c_str (), sizeof (header->orig_host_name) - 1);
  header->orig_host_name[sizeof (header->orig_host_name) - 1] = '\0';
}

/*
 * hb_hostname_to_sin_addr() -
 *   return:
 *
 *   host(in):
 *   addr(out):
 */
static int
hb_hostname_to_sin_addr (const char *host, struct in_addr *addr)
{
  in_addr_t in_addr;

  /*
   * First try to convert to the host name as a dotten-decimal number.
   * Only if that fails do we call gethostbyname.
   */
  in_addr = inet_addr (host);
  if (in_addr != INADDR_NONE)
    {
      memcpy ((void *) addr, (void *) &in_addr, sizeof (in_addr));
    }
  else
    {
#ifdef HAVE_GETHOSTBYNAME_R
#if defined (HAVE_GETHOSTBYNAME_R_GLIBC)
      struct hostent *hp, hent;
      int herr;
      char buf[1024];

      if (gethostbyname_r (host, &hent, buf, sizeof (buf), &hp, &herr) != 0 || hp == NULL)
	{
	  MASTER_ER_SET_WITH_OSERROR (ER_ERROR_SEVERITY, ARG_FILE_LINE, ERR_CSS_TCP_HOST_NAME_ERROR, 1, host);
	  return ERR_CSS_TCP_HOST_NAME_ERROR;
	}
      memcpy ((void *) addr, (void *) hent.h_addr, hent.h_length);
#elif defined (HAVE_GETHOSTBYNAME_R_SOLARIS)
      struct hostent hent;
      int herr;
      char buf[1024];

      if (gethostbyname_r (host, &hent, buf, sizeof (buf), &herr) == NULL)
	{
	  MASTER_ER_SET_WITH_OSERROR (ER_ERROR_SEVERITY, ARG_FILE_LINE, ERR_CSS_TCP_HOST_NAME_ERROR, 1, host);
	  return ERR_CSS_TCP_HOST_NAME_ERROR;
	}
      memcpy ((void *) addr, (void *) hent.h_addr, hent.h_length);
#elif defined (HAVE_GETHOSTBYNAME_R_HOSTENT_DATA)
      struct hostent hent;
      struct hostent_data ht_data;

      if (gethostbyname_r (host, &hent, &ht_data) == -1)
	{
	  MASTER_ER_SET_WITH_OSERROR (ER_ERROR_SEVERITY, ARG_FILE_LINE, ERR_CSS_TCP_HOST_NAME_ERROR, 1, host);
	  return ERR_CSS_TCP_HOST_NAME_ERROR;
	}
      memcpy ((void *) addr, (void *) hent.h_addr, hent.h_length);
#else
#error "HAVE_GETHOSTBYNAME_R"
#endif
#else /* HAVE_GETHOSTBYNAME_R */
      struct hostent *hp;
      int r;

      r = pthread_mutex_lock (&gethostbyname_lock);
      hp = gethostbyname (host);
      if (hp == NULL)
	{
	  pthread_mutex_unlock (&gethostbyname_lock);
	  MASTER_ER_SET_WITH_OSERROR (ER_ERROR_SEVERITY, ARG_FILE_LINE, ERR_CSS_TCP_HOST_NAME_ERROR, 1, host);
	  return ERR_CSS_TCP_HOST_NAME_ERROR;
	}
      memcpy ((void *) addr, (void *) hp->h_addr, hp->h_length);
      pthread_mutex_unlock (&gethostbyname_lock);
#endif /* !HAVE_GETHOSTBYNAME_R */
    }

  return NO_ERROR;
}

/*
 * hb_hostname_n_port_to_sockaddr() -
 *   return: NO_ERROR
 *
 *   host(in):
 *   port(in):
 *   saddr(out):
 *   slen(out):
 */
static int
hb_hostname_n_port_to_sockaddr (const char *host, int port, struct sockaddr *saddr, socklen_t *slen)
{
  int error = NO_ERROR;
  struct sockaddr_in udp_saddr;

  /*
   * Construct address for UDP socket
   */
  memset ((void *) &udp_saddr, 0, sizeof (udp_saddr));
  udp_saddr.sin_family = AF_INET;
  udp_saddr.sin_port = htons (port);

  error = hb_hostname_to_sin_addr (host, &udp_saddr.sin_addr);
  if (error != NO_ERROR)
    {
      return INVALID_SOCKET;
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
hb_cluster_job_queue (unsigned int job_type, HB_JOB_ARG *arg, unsigned int msec)
{
  if (job_type >= HB_CJOB_MAX)
    {
      MASTER_ER_LOG_DEBUG (ARG_FILE_LINE, "unknown job type. (job_type:%d).\n", job_type);
      return ER_FAILED;
    }

  return hb_job_queue (cluster_Jobs, job_type, arg, msec);
}

/*
 * hb_cluster_job_set_expire_and_reorder() -
 *   return: NO_ERROR or ER_FAILED
 *
 *   job_type(in):
 *   msec(in):
 */
static int
hb_cluster_job_set_expire_and_reorder (unsigned int job_type, unsigned int msec)
{
  if (job_type >= HB_CJOB_MAX)
    {
      MASTER_ER_LOG_DEBUG (ARG_FILE_LINE, "unknown job type. (job_type:%d).\n", job_type);
      return ER_FAILED;
    }

  hb_job_set_expire_and_reorder (cluster_Jobs, job_type, msec);

  return NO_ERROR;
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
 * hb_return_node_by_name_except_me() -
 *   return: pointer to heartbeat node entry
 *
 *   name(in):
 */
static cubhb::node_entry *
hb_return_node_by_name_except_me (char *name)
{
  for (cubhb::node_entry *node : hb_Cluster->nodes)
    {
      if (strcmp (name, node->get_hostname_cstr ()) != 0 || strcmp (name, hb_Cluster->hostname.c_str ()) == 0)
	{
	  continue;
	}

      return node;
    }

  return NULL;
}

static int
hb_is_heartbeat_valid (char *host_name, char *group_id, struct sockaddr_in *from)
{
  int error;
  struct in_addr sin_addr;

  cubhb::node_entry *node = hb_return_node_by_name_except_me (host_name);
  if (node == NULL)
    {
      return HB_VALID_UNIDENTIFIED_NODE;
    }

  if (strcmp (group_id, hb_Cluster->group_id.c_str ()) != 0)
    {
      return HB_VALID_GROUP_NAME_MISMATCH;
    }

  error = hb_hostname_to_sin_addr (host_name, &sin_addr);
  if (error == NO_ERROR)
    {
      if (memcmp ((void *) &sin_addr, (void *) &from->sin_addr, sizeof (struct in_addr)) != 0)
	{
	  return HB_VALID_IP_ADDR_MISMATCH;
	}
    }
  else
    {
      return HB_VALID_CANNOT_RESOLVE_HOST;
    }

  return HB_VALID_NO_ERROR;
}

/*
 * hb_valid_result_string() -
 */
static const char *
hb_valid_result_string (int v_result)
{
  switch (v_result)
    {
    case HB_VALID_NO_ERROR:
      return HB_VALID_NO_ERROR_STR;
    case HB_VALID_UNIDENTIFIED_NODE:
      return HB_VALID_UNIDENTIFIED_NODE_STR;
    case HB_VALID_GROUP_NAME_MISMATCH:
      return HB_VALID_GROUP_NAME_MISMATCH_STR;
    case HB_VALID_IP_ADDR_MISMATCH:
      return HB_VALID_IP_ADDR_MISMATCH_STR;
    case HB_VALID_CANNOT_RESOLVE_HOST:
      return HB_VALID_CANNOT_RESOLVE_HOST_STR;
    }

  assert (false);
  return "invalid";
}

/*
 * resource process job actions
 */

/*
 * hb_resource_job_confirm_cleanup_all () - confirm that all HA processes are shutdown
 *   for deactivating heartbeat
 *   return: none
 *
 *   arg(in):
 */
static void
hb_resource_job_confirm_cleanup_all (HB_JOB_ARG *arg)
{
  int rv, error;
  HB_RESOURCE_JOB_ARG *resource_job_arg;
  HB_PROC_ENTRY *proc, *proc_next;
  char error_string[LINE_MAX] = "";
  int num_connected_rsc = 0;

  resource_job_arg = (arg) ? & (arg->resource_job_arg) : NULL;

  if (arg == NULL || resource_job_arg == NULL)
    {
      MASTER_ER_LOG_DEBUG (ARG_FILE_LINE, "invalid arg or resource_job_arg. (arg:%p, resource_job_arg:%p). \n", arg,
			   resource_job_arg);
      return;
    }

  rv = pthread_mutex_lock (&hb_Resource->lock);

  if (++ (resource_job_arg->retries) > resource_job_arg->max_retries || hb_Deactivate_immediately)
    {
      for (proc = hb_Resource->procs; proc; proc = proc_next)
	{
	  assert (proc->state == HB_PSTATE_DEREGISTERED);
	  assert (proc->pid > 0);

	  proc_next = proc->next;

	  if (proc->pid > 0 && (kill (proc->pid, 0) == 0 || errno != ESRCH))
	    {
	      snprintf (error_string, LINE_MAX, "(pid: %d, args:%s)", proc->pid, proc->args);
	      if (hb_Deactivate_immediately)
		{
		  MASTER_ER_SET (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HB_PROCESS_EVENT, 2,
				 "Immediate shutdown requested. Process killed", error_string);
		}
	      else
		{
		  MASTER_ER_SET (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HB_PROCESS_EVENT, 2,
				 "No response to shutdown request. Process killed", error_string);
		}

	      kill (proc->pid, SIGKILL);
	    }

	  hb_Resource->num_procs--;
	  hb_remove_proc (proc);
	  proc = NULL;
	}

      assert (hb_Resource->num_procs == 0);
      goto end_confirm_cleanup;
    }

  for (proc = hb_Resource->procs; proc; proc = proc_next)
    {
      assert (proc->state == HB_PSTATE_DEREGISTERED);
      assert (proc->pid > 0);

      proc_next = proc->next;

      if (proc->type != HB_PTYPE_SERVER)
	{
	  if (proc->pid > 0 && (kill (proc->pid, 0) == 0 || errno != ESRCH))
	    {
	      kill (proc->pid, SIGKILL);

	      snprintf (error_string, LINE_MAX, "(pid: %d, args:%s)", proc->pid, proc->args);
	      MASTER_ER_SET (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HB_PROCESS_EVENT, 2,
			     "No response to shutdown request. Process killed", error_string);
	    }
	  hb_Resource->num_procs--;
	  hb_remove_proc (proc);
	  proc = NULL;
	}
      else
	{
	  if (proc->pid <= 0 || (kill (proc->pid, 0) && errno == ESRCH))
	    {
	      hb_Resource->num_procs--;
	      hb_remove_proc (proc);
	      proc = NULL;
	      continue;
	    }
	}

      if (proc && proc->conn != NULL)
	{
	  num_connected_rsc++;
	}

      assert (hb_Resource->num_procs >= 0);
    }

  if (hb_Resource->num_procs == 0 || num_connected_rsc == 0)
    {
      goto end_confirm_cleanup;
    }

  pthread_mutex_unlock (&hb_Resource->lock);

  error = hb_resource_job_queue (HB_RJOB_CONFIRM_CLEANUP_ALL, arg,
				 prm_get_integer_value (PRM_ID_HA_PROCESS_DEREG_CONFIRM_INTERVAL_IN_MSECS));

  if (error != NO_ERROR)
    {
      assert (false);
      free_and_init (arg);
    }

  return;

end_confirm_cleanup:
  pthread_mutex_unlock (&hb_Resource->lock);

  if (arg != NULL)
    {
      free_and_init (arg);
    }

  MASTER_ER_SET (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_HB_PROCESS_EVENT, 2, "ready to deactivate heartbeat", "");
}

/*
 * hb_resource_job_cleanup_all () - shutdown all HA processes including cub_server
 *   for deactivating heartbeat
 *   return: none
 *
 *   arg(in):
 */
static void
hb_resource_job_cleanup_all (HB_JOB_ARG *arg)
{
  int rv, i, error;
  HB_PROC_ENTRY *proc;
  HB_JOB_ARG *job_arg;
  HB_RESOURCE_JOB_ARG *resource_job_arg;

  rv = pthread_mutex_lock (&css_Master_socket_anchor_lock);
  rv = pthread_mutex_lock (&hb_Resource->lock);

  if (!hb_Deactivate_immediately)
    {
      /* register CUBRID server pid */
      hb_Deactivate_info.server_pid_list = (int *) calloc (hb_Resource->num_procs, sizeof (int));

      for (i = 0, proc = hb_Resource->procs; proc; proc = proc->next)
	{
	  if (proc->conn && proc->type == HB_PTYPE_SERVER)
	    {
	      hb_Deactivate_info.server_pid_list[i] = proc->pid;
	      i++;
	    }
	}

      hb_Deactivate_info.server_count = i;

      assert (hb_Resource->num_procs >= i);
    }

  hb_resource_shutdown_all_ha_procs ();

  job_arg = (HB_JOB_ARG *) malloc (sizeof (HB_JOB_ARG));
  if (job_arg == NULL)
    {
      pthread_mutex_unlock (&hb_Resource->lock);
      pthread_mutex_unlock (&css_Master_socket_anchor_lock);

      MASTER_ER_SET (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (HB_JOB_ARG));

      return;
    }

  resource_job_arg = & (job_arg->resource_job_arg);
  resource_job_arg->retries = 0;
  resource_job_arg->max_retries = prm_get_integer_value (PRM_ID_HA_MAX_PROCESS_DEREG_CONFIRM);
  gettimeofday (&resource_job_arg->ftime, NULL);

  pthread_mutex_unlock (&hb_Resource->lock);
  pthread_mutex_unlock (&css_Master_socket_anchor_lock);

  error = hb_resource_job_queue (HB_RJOB_CONFIRM_CLEANUP_ALL, job_arg, HB_JOB_TIMER_WAIT_500_MILLISECOND);

  if (error != NO_ERROR)
    {
      assert (false);
      free_and_init (job_arg);
    }
}

/*
 * hb_resource_job_proc_start () -
 *   return: none
 *
 *   arg(in):
 */
static void
hb_resource_job_proc_start (HB_JOB_ARG *arg)
{
  int error, rv;
  char error_string[LINE_MAX] = "";
  pid_t pid;
  struct timeval now;
  HB_PROC_ENTRY *proc;
  HB_RESOURCE_JOB_ARG *proc_arg = (arg) ? & (arg->resource_job_arg) : NULL;
  char *argv[HB_MAX_NUM_PROC_ARGV] = { NULL, };

  if (arg == NULL || proc_arg == NULL)
    {
      MASTER_ER_LOG_DEBUG (ARG_FILE_LINE, "invalid arg or proc_arg. (arg:%p, proc_arg:%p). \n", arg, proc_arg);
      return;
    }

  rv = pthread_mutex_lock (&hb_Resource->lock);
  proc = hb_return_proc_by_args (proc_arg->args);
  if (proc == NULL || proc->state == HB_PSTATE_DEREGISTERED)
    {
      pthread_mutex_unlock (&hb_Resource->lock);
      free_and_init (arg);
      return;
    }

  if (proc->being_shutdown)
    {
      assert (proc_arg->pid > 0);
      if (proc_arg->pid <= 0 || (kill (proc_arg->pid, 0) && errno == ESRCH))
	{
	  proc->being_shutdown = false;
	}
      else
	{
	  pthread_mutex_unlock (&hb_Resource->lock);
	  error = hb_resource_job_queue (HB_RJOB_PROC_START, arg, HB_JOB_TIMER_WAIT_A_SECOND);
	  if (error != NO_ERROR)
	    {
	      assert (false);
	      free_and_init (arg);
	    }
	  return;
	}
    }

  gettimeofday (&now, NULL);
  if (HB_GET_ELAPSED_TIME (now, proc->frtime) < HB_PROC_RECOVERY_DELAY_TIME)
    {
      MASTER_ER_LOG_DEBUG (ARG_FILE_LINE, "delay the restart of the process. (arg:%p, proc_arg:%p). \n", arg, proc_arg);

      pthread_mutex_unlock (&hb_Resource->lock);
      error = hb_resource_job_queue (HB_RJOB_PROC_START, arg, HB_JOB_TIMER_WAIT_A_SECOND);
      if (error != NO_ERROR)
	{
	  assert (false);
	  free_and_init (arg);
	}
      return;
    }

  snprintf (error_string, LINE_MAX, "(args:%s)", proc->args);
  MASTER_ER_SET (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HB_PROCESS_EVENT, 2, "Restart the process", error_string);

  hb_proc_make_arg (argv, (char *) proc->argv);

  pid = fork ();
  if (pid < 0)
    {
      pthread_mutex_unlock (&hb_Resource->lock);

      MASTER_ER_SET (ER_WARNING_SEVERITY, ARG_FILE_LINE, ERR_CSS_CANNOT_FORK, 0);

      error = hb_resource_job_queue (HB_RJOB_PROC_START, arg, HB_JOB_TIMER_WAIT_A_SECOND);
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
      MASTER_ER_LOG_DEBUG (ARG_FILE_LINE,
			   "execute:{%s} arg[0]:{%s} arg[1]:{%s} arg[2]:{%s} "
			   "arg[3]:{%s} arg{4}:{%s} arg[5]:{%s} arg[6]:{%s} " "arg[7]:{%s} arg[8]:{%s} arg[9]:{%s}.\n",
			   proc->exec_path, (argv[0]) ? argv[0] : "", (argv[1]) ? argv[1] : "",
			   (argv[2]) ? argv[2] : "", (argv[3]) ? argv[3] : "", (argv[4]) ? argv[4] : "",
			   (argv[5]) ? argv[5] : "", (argv[6]) ? argv[6] : "", (argv[7]) ? argv[7] : "",
			   (argv[8]) ? argv[8] : "", (argv[9]) ? argv[9] : "");
#endif
      error = execv (proc->exec_path, argv);
      pthread_mutex_unlock (&hb_Resource->lock);

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

  pthread_mutex_unlock (&hb_Resource->lock);

  error =
	  hb_resource_job_queue (HB_RJOB_CONFIRM_START, arg,
				 prm_get_integer_value (PRM_ID_HA_PROCESS_START_CONFIRM_INTERVAL_IN_MSECS));
  if (error != NO_ERROR)
    {
      assert (false);
      free_and_init (arg);
    }
}

/*
 * hb_resource_job_proc_dereg() -
 *   return: none
 *
 *   arg(in):
 */
static void
hb_resource_job_proc_dereg (HB_JOB_ARG *arg)
{
  int error, rv;
  HB_PROC_ENTRY *proc;
  HB_RESOURCE_JOB_ARG *proc_arg = (arg) ? & (arg->resource_job_arg) : NULL;
  SOCKET_QUEUE_ENTRY *sock_entq;
  char buffer[MASTER_TO_SRV_MSG_SIZE];

  if (arg == NULL || proc_arg == NULL)
    {
      MASTER_ER_LOG_DEBUG (ARG_FILE_LINE, "invalid arg or proc_arg. (arg:%p, proc_arg:%p). \n", arg, proc_arg);
      return;
    }
#if defined (HB_VERBOSE_DEBUG)
  MASTER_ER_LOG_DEBUG (ARG_FILE_LINE, "deregister process. (pid:%d). \n", proc_arg->pid);
#endif
  rv = pthread_mutex_lock (&css_Master_socket_anchor_lock);
  rv = pthread_mutex_lock (&hb_Resource->lock);
  proc = hb_return_proc_by_pid (proc_arg->pid);
  if (proc == NULL)
    {
      MASTER_ER_LOG_DEBUG (ARG_FILE_LINE, "cannot find process entry. (unknown pid, pid:%d). \n", proc_arg->pid);
      pthread_mutex_unlock (&hb_Resource->lock);
      pthread_mutex_unlock (&css_Master_socket_anchor_lock);

      free_and_init (arg);
      return;
    }

  if (proc->state != HB_PSTATE_DEREGISTERED)
    {
      MASTER_ER_LOG_DEBUG (ARG_FILE_LINE, "invalid process state. (pid:%d, state:%d). \n", proc_arg->pid, proc->state);
      pthread_mutex_unlock (&hb_Resource->lock);
      pthread_mutex_unlock (&css_Master_socket_anchor_lock);

      free_and_init (arg);
      return;
    }

  if (proc->type == HB_PTYPE_SERVER)
    {
      sock_entq = css_return_entry_by_conn (proc->conn, &css_Master_socket_anchor);
      assert_release (sock_entq == NULL || sock_entq->name != NULL);
      if (sock_entq == NULL || sock_entq->name == NULL)
	{
	  MASTER_ER_LOG_DEBUG (ARG_FILE_LINE, "invalid process conn entry. (pid:%d). \n", proc_arg->pid);
	  goto hb_resource_job_proc_dereg_end;

	}
      memset (buffer, 0, sizeof (buffer));
      snprintf (buffer, sizeof (buffer) - 1,
		msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_MASTER, MASTER_MSG_SERVER_STATUS),
		sock_entq->name + 1, 0);
      css_process_start_shutdown (sock_entq, 0, buffer);
    }
  else
    {
      assert (proc->pid > 0);
      if (proc->pid <= 0 || (kill (proc->pid, SIGTERM) && errno == ESRCH))
	{
	  hb_Resource->num_procs--;
	  hb_remove_proc (proc);
	  proc = NULL;

	  pthread_mutex_unlock (&hb_Resource->lock);
	  pthread_mutex_unlock (&css_Master_socket_anchor_lock);

	  free_and_init (arg);
	  return;
	}
    }

hb_resource_job_proc_dereg_end:
  pthread_mutex_unlock (&hb_Resource->lock);
  pthread_mutex_unlock (&css_Master_socket_anchor_lock);

  error =
	  hb_resource_job_queue (HB_RJOB_CONFIRM_DEREG, arg,
				 prm_get_integer_value (PRM_ID_HA_PROCESS_DEREG_CONFIRM_INTERVAL_IN_MSECS));
  if (error != NO_ERROR)
    {
      assert (false);
      free_and_init (arg);
    }
}

/*
 * hb_resource_demote_start_shutdown_server_proc() -
 *      send shutdown request to server
 *   return: none
 *
 */
static void
hb_resource_demote_start_shutdown_server_proc (void)
{
  HB_PROC_ENTRY *proc;
  SOCKET_QUEUE_ENTRY *sock_entq;
  char buffer[MASTER_TO_SRV_MSG_SIZE];

  for (proc = hb_Resource->procs; proc; proc = proc->next)
    {
      /* leave processes other than cub_server */
      if (proc->state != HB_PSTATE_REGISTERED_AND_TO_BE_ACTIVE && proc->state != HB_PSTATE_REGISTERED_AND_ACTIVE)
	{
	  continue;
	}
      assert (proc->type == HB_PTYPE_SERVER);

      if (proc->server_hang)
	{
	  /* terminate a hang server process immediately */
	  assert (proc->pid > 0);
	  if (proc->pid > 0 && (kill (proc->pid, 0) == 0 || errno != ESRCH))
	    {
	      kill (proc->pid, SIGKILL);
	    }
	  continue;
	}

      sock_entq = css_return_entry_by_conn (proc->conn, &css_Master_socket_anchor);
      assert_release (sock_entq == NULL || sock_entq->name != NULL);
      if (sock_entq != NULL && sock_entq->name != NULL)
	{
	  memset (buffer, 0, sizeof (buffer));
	  snprintf (buffer, sizeof (buffer) - 1,
		    msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_MASTER, MASTER_MSG_SERVER_STATUS),
		    sock_entq->name + 1, 0);

	  css_process_start_shutdown (sock_entq, 0, buffer);
	  proc->being_shutdown = true;
	}
    }
}

/*
 * hb_resource_demote_confirm_shutdown_server_proc() -
 *      confirm that server process is shutdown
 *   return: whether all active, to-be-active server proc's are shutdown
 *
 */
static bool
hb_resource_demote_confirm_shutdown_server_proc (void)
{
  HB_PROC_ENTRY *proc;

  for (proc = hb_Resource->procs; proc; proc = proc->next)
    {
      if (proc->server_hang)
	{
	  /* don't wait for a hang server process that has already been terminated */
	  continue;
	}

      if (proc->state == HB_PSTATE_REGISTERED_AND_TO_BE_ACTIVE || proc->state == HB_PSTATE_REGISTERED_AND_ACTIVE)
	{
	  assert (proc->type == HB_PTYPE_SERVER);
	  return false;
	}
    }
  return true;
}

/*
 * hb_resource_demote_kill_server_proc() -
 *      kill server process in an active or to-be-active state
 *   return: none
 *
 */
static void
hb_resource_demote_kill_server_proc (void)
{
  HB_PROC_ENTRY *proc;
  char error_string[LINE_MAX] = "";

  for (proc = hb_Resource->procs; proc; proc = proc->next)
    {
      if (proc->state == HB_PSTATE_REGISTERED_AND_TO_BE_ACTIVE || proc->state == HB_PSTATE_REGISTERED_AND_ACTIVE)
	{
	  assert (proc->type == HB_PTYPE_SERVER);
	  assert (proc->pid > 0);
	  if (proc->pid > 0 && (kill (proc->pid, 0) == 0 || errno != ESRCH))
	    {
	      snprintf (error_string, LINE_MAX, "(pid: %d, args:%s)", proc->pid, proc->args);
	      MASTER_ER_SET (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HB_PROCESS_EVENT, 2,
			     "No response to shutdown request. Process killed", error_string);
	      kill (proc->pid, SIGKILL);
	    }
	}
    }
}

/*
 * hb_resource_job_demote_confirm_shutdown() -
 *      prepare for demoting master
 *      it checks if every active server process is shutdown
 *      if so, it assigns demote cluster job
 *   return: none
 *
 *   arg(in):
 */
static void
hb_resource_job_demote_confirm_shutdown (HB_JOB_ARG *arg)
{
  int error, rv;
  HB_JOB_ARG *job_arg;
  HB_RESOURCE_JOB_ARG *proc_arg = (arg) ? & (arg->resource_job_arg) : NULL;
  HB_CLUSTER_JOB_ARG *clst_arg;

  if (arg == NULL || proc_arg == NULL)
    {
      MASTER_ER_LOG_DEBUG (ARG_FILE_LINE, "invalid arg or proc_arg. (arg:%p, proc_arg:%p). \n", arg, proc_arg);
      return;
    }

  rv = pthread_mutex_lock (&hb_Resource->lock);

  if (++ (proc_arg->retries) > proc_arg->max_retries)
    {
      hb_resource_demote_kill_server_proc ();
      goto demote_confirm_shutdown_end;
    }

  if (!hb_resource_demote_confirm_shutdown_server_proc ())
    {
      pthread_mutex_unlock (&hb_Resource->lock);

      error =
	      hb_resource_job_queue (HB_RJOB_DEMOTE_CONFIRM_SHUTDOWN, arg,
				     prm_get_integer_value (PRM_ID_HA_PROCESS_DEREG_CONFIRM_INTERVAL_IN_MSECS));

      assert (error == NO_ERROR);

      return;
    }

demote_confirm_shutdown_end:
  pthread_mutex_unlock (&hb_Resource->lock);

  job_arg = (HB_JOB_ARG *) malloc (sizeof (HB_JOB_ARG));
  if (job_arg == NULL)
    {
      MASTER_ER_SET (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (HB_JOB_ARG));
      if (arg)
	{
	  free_and_init (arg);
	}
      css_master_cleanup (SIGTERM);
      return;
    }

  clst_arg = & (job_arg->cluster_job_arg);
  clst_arg->ping_check_count = 0;
  clst_arg->retries = 0;

  error = hb_cluster_job_queue (HB_CJOB_DEMOTE, job_arg, HB_JOB_TIMER_IMMEDIATELY);

  if (error != NO_ERROR)
    {
      assert (false);
      free_and_init (job_arg);
    }

  if (arg)
    {
      free_and_init (arg);
    }
}

/*
 * hb_resource_job_demote_start_shutdown() -
 *      prepare for demoting master
 *      it shuts down working active server processes
 *   return: none
 *
 *   arg(in):
 */
static void
hb_resource_job_demote_start_shutdown (HB_JOB_ARG *arg)
{
  int error, rv;
  HB_JOB_ARG *job_arg;
  HB_RESOURCE_JOB_ARG *proc_arg;

  rv = pthread_mutex_lock (&css_Master_socket_anchor_lock);
  rv = pthread_mutex_lock (&hb_Resource->lock);

  hb_resource_demote_start_shutdown_server_proc ();

  rv = pthread_mutex_unlock (&hb_Resource->lock);
  rv = pthread_mutex_unlock (&css_Master_socket_anchor_lock);

  job_arg = (HB_JOB_ARG *) malloc (sizeof (HB_JOB_ARG));
  if (job_arg == NULL)
    {
      MASTER_ER_SET (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (HB_JOB_ARG));
      if (arg)
	{
	  free_and_init (arg);
	}
      css_master_cleanup (SIGTERM);
      return;
    }

  proc_arg = & (job_arg->resource_job_arg);
  proc_arg->retries = 0;
  proc_arg->max_retries = prm_get_integer_value (PRM_ID_HA_MAX_PROCESS_DEREG_CONFIRM);
  gettimeofday (&proc_arg->ftime, NULL);

  error =
	  hb_resource_job_queue (HB_RJOB_DEMOTE_CONFIRM_SHUTDOWN, job_arg,
				 prm_get_integer_value (PRM_ID_HA_PROCESS_DEREG_CONFIRM_INTERVAL_IN_MSECS));
  if (error != NO_ERROR)
    {
      assert (false);
      free_and_init (job_arg);
    }

  if (arg)
    {
      free_and_init (arg);
    }
}

/*
 * hb_resource_job_confirm_start() -
 *   return: none
 *
 *   arg(in):
 */
static void
hb_resource_job_confirm_start (HB_JOB_ARG *arg)
{
  int error, rv;
  char error_string[LINE_MAX] = "";
  bool retry = true;
  HB_PROC_ENTRY *proc;
  HB_RESOURCE_JOB_ARG *proc_arg = (arg) ? & (arg->resource_job_arg) : NULL;
  char hb_info_str[HB_INFO_STR_MAX];

  if (arg == NULL || proc_arg == NULL)
    {
      MASTER_ER_LOG_DEBUG (ARG_FILE_LINE, "invalid arg or proc_arg. (arg:%p, proc_arg:%p). \n", arg, proc_arg);
      return;
    }

  rv = pthread_mutex_lock (&hb_Resource->lock);
  proc = hb_return_proc_by_args (proc_arg->args);
  if (proc == NULL || proc->state == HB_PSTATE_DEREGISTERED)
    {
      pthread_mutex_unlock (&hb_Resource->lock);
      free_and_init (arg);
      return;
    }

  if (++ (proc_arg->retries) > proc_arg->max_retries)
    {
      snprintf (error_string, LINE_MAX, "(exceed max retry count for pid: %d, args:%s)", proc->pid, proc->args);

      if (hb_Resource->state == cubhb::node_entry::MASTER && proc->type == HB_PTYPE_SERVER && !hb_Cluster->is_isolated)
	{
	  hb_Resource->state = cubhb::node_entry::SLAVE;
	  pthread_mutex_unlock (&hb_Resource->lock);

	  MASTER_ER_SET (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HB_PROCESS_EVENT, 2,
			 "Failed to restart the process " "and the current node will be demoted", error_string);

	  /* keep checking problematic process */
	  proc_arg->retries = 0;
	  error =
		  hb_resource_job_queue (HB_RJOB_CONFIRM_START, arg,
					 prm_get_integer_value (PRM_ID_HA_PROCESS_START_CONFIRM_INTERVAL_IN_MSECS));
	  if (error != NO_ERROR)
	    {
	      free_and_init (arg);
	      assert (false);
	    }

	  /* shutdown working server processes to change its role to slave */
	  error = hb_resource_job_queue (HB_RJOB_DEMOTE_START_SHUTDOWN, NULL, HB_JOB_TIMER_IMMEDIATELY);
	  assert (error == NO_ERROR);

	  return;
	}
      else
	{
	  pthread_mutex_unlock (&hb_Resource->lock);
	  MASTER_ER_SET (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HB_PROCESS_EVENT, 2,
			 "Keep checking to confirm the completion of the process startup", error_string);
	  proc_arg->retries = 0;
	  error =
		  hb_resource_job_queue (HB_RJOB_CONFIRM_START, arg,
					 prm_get_integer_value (PRM_ID_HA_PROCESS_START_CONFIRM_INTERVAL_IN_MSECS));
	  if (error != NO_ERROR)
	    {
	      assert (false);
	      free_and_init (arg);
	    }
	  return;
	}
    }

  assert (proc->pid > 0);
  error = kill (proc->pid, 0);
  if (error)
    {
      pthread_mutex_unlock (&hb_Resource->lock);
      if (errno == ESRCH)
	{
	  snprintf (error_string, LINE_MAX, "(process not found, expected pid: %d, args:%s)", proc->pid, proc->args);
	  MASTER_ER_SET (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HB_PROCESS_EVENT, 2, "Failed to restart process",
			 error_string);

	  error = hb_resource_job_queue (HB_RJOB_PROC_START, arg, HB_JOB_TIMER_WAIT_A_SECOND);
	  if (error != NO_ERROR)
	    {
	      assert (false);
	      free_and_init (arg);
	    }
	}
      else
	{
	  error =
		  hb_resource_job_queue (HB_RJOB_CONFIRM_START, arg,
					 prm_get_integer_value (PRM_ID_HA_PROCESS_START_CONFIRM_INTERVAL_IN_MSECS));
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
      if (proc->type == HB_PTYPE_SERVER)
	{
	  proc->state = HB_PSTATE_REGISTERED_AND_STANDBY;
	}
      else
	{
	  proc->state = HB_PSTATE_REGISTERED;
	}

      retry = false;
    }

  pthread_mutex_unlock (&hb_Resource->lock);

  hb_help_sprint_processes_info (hb_info_str, HB_INFO_STR_MAX);
  MASTER_ER_LOG_DEBUG (ARG_FILE_LINE, "%s", hb_info_str);

  if (retry)
    {
      error =
	      hb_resource_job_queue (HB_RJOB_CONFIRM_START, arg,
				     prm_get_integer_value (PRM_ID_HA_PROCESS_START_CONFIRM_INTERVAL_IN_MSECS));
      if (error != NO_ERROR)
	{
	  assert (false);
	  free_and_init (arg);
	}
      return;
    }

  free_and_init (arg);
}

/*
 * hb_resource_job_confirm_dereg() -
 *   return: none
 *
 *   arg(in):
 */
static void
hb_resource_job_confirm_dereg (HB_JOB_ARG *arg)
{
  int error, rv;
  bool retry = true;
  HB_PROC_ENTRY *proc;
  HB_RESOURCE_JOB_ARG *proc_arg = (arg) ? & (arg->resource_job_arg) : NULL;

  if (arg == NULL || proc_arg == NULL)
    {
      MASTER_ER_LOG_DEBUG (ARG_FILE_LINE, "invalid arg or proc_arg. (arg:%p, proc_arg:%p). \n", arg, proc_arg);
      return;
    }

#if defined (HB_VERBOSE_DEBUG)
  MASTER_ER_LOG_DEBUG (ARG_FILE_LINE, "deregister confirm process. (pid:%d, args:{%s}). \n", proc_arg->pid,
		       proc_arg->args);
#endif

  rv = pthread_mutex_lock (&hb_Resource->lock);
  proc = hb_return_proc_by_pid (proc_arg->pid);
  if (proc == NULL)
    {
      pthread_mutex_unlock (&hb_Resource->lock);
      free_and_init (arg);
      return;
    }

  if (proc->state != HB_PSTATE_DEREGISTERED)
    {
      MASTER_ER_LOG_DEBUG (ARG_FILE_LINE, "invalid process state. (pid:%d, state:%d). \n", proc_arg->pid, proc->state);
      pthread_mutex_unlock (&hb_Resource->lock);

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
      if (++ (proc_arg->retries) > proc_arg->max_retries)
	{
	  assert (proc->pid > 0);
	  if (proc->pid > 0)
	    {
	      kill (proc->pid, SIGKILL);
	    }
	  retry = false;
	}
    }

  if (retry)
    {
      pthread_mutex_unlock (&hb_Resource->lock);
      error =
	      hb_resource_job_queue (HB_RJOB_CONFIRM_DEREG, arg,
				     prm_get_integer_value (PRM_ID_HA_PROCESS_DEREG_CONFIRM_INTERVAL_IN_MSECS));
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

  pthread_mutex_unlock (&hb_Resource->lock);

  free_and_init (arg);
}

/*
 * hb_resource_job_change_mode() -
 *   return: none
 *
 *   arg(in):
 */
static void
hb_resource_job_change_mode (HB_JOB_ARG *arg)
{
  int error, rv;
  HB_PROC_ENTRY *proc;
  char hb_info_str[HB_INFO_STR_MAX];

  rv = pthread_mutex_lock (&css_Master_socket_anchor_lock);
  rv = pthread_mutex_lock (&hb_Resource->lock);
  for (proc = hb_Resource->procs; proc; proc = proc->next)
    {
      if (proc->type != HB_PTYPE_SERVER)
	{
	  continue;
	}

      if ((hb_Resource->state == cubhb::node_entry::MASTER
	   && (proc->state == HB_PSTATE_REGISTERED_AND_STANDBY || proc->state == HB_PSTATE_REGISTERED_AND_TO_BE_ACTIVE))
	  || (hb_Resource->state == cubhb::node_entry::TO_BE_SLAVE
	      && (proc->state == HB_PSTATE_REGISTERED_AND_ACTIVE
		  || proc->state == HB_PSTATE_REGISTERED_AND_TO_BE_STANDBY)))
	{
	  /* TODO : send heartbeat changemode request */
	  MASTER_ER_LOG_DEBUG (ARG_FILE_LINE, "send change-mode request. " "(node_state:%d, pid:%d, proc_state:%d). \n",
			       hb_Resource->state, proc->pid, proc->state);

	  error = hb_resource_send_changemode (proc);
	  if (NO_ERROR != error)
	    {
	      /* TODO : if error */
	    }
	}
    }

  if (hb_Resource->procs)
    {
      hb_help_sprint_processes_info (hb_info_str, HB_INFO_STR_MAX);
      MASTER_ER_LOG_DEBUG (ARG_FILE_LINE, "%s", hb_info_str);
    }

  pthread_mutex_unlock (&hb_Resource->lock);
  pthread_mutex_unlock (&css_Master_socket_anchor_lock);

  error =
	  hb_resource_job_queue (HB_RJOB_CHANGE_MODE, NULL, prm_get_integer_value (PRM_ID_HA_CHANGEMODE_INTERVAL_IN_MSECS));
  assert (error == NO_ERROR);

  if (arg)
    {
      free_and_init (arg);
    }
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
hb_resource_job_queue (unsigned int job_type, HB_JOB_ARG *arg, unsigned int msec)
{
  if (job_type >= HB_RJOB_MAX)
    {
      MASTER_ER_LOG_DEBUG (ARG_FILE_LINE, "unknown job type. (job_type:%d).\n", job_type);
      return ER_FAILED;
    }

  return hb_job_queue (resource_Jobs, job_type, arg, msec);
}

/*
 * hb_resource_job_set_expire_and_reorder() -
 *   return: NO_ERROR or ER_FAILED
 *
 *   job_type(in):
 *   msec(in):
 */
static int
hb_resource_job_set_expire_and_reorder (unsigned int job_type, unsigned int msec)
{
  if (job_type >= HB_RJOB_MAX)
    {
      MASTER_ER_LOG_DEBUG (ARG_FILE_LINE, "unknown job type. (job_type:%d).\n", job_type);
      return ER_FAILED;
    }

  hb_job_set_expire_and_reorder (resource_Jobs, job_type, msec);

  return NO_ERROR;
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

static void
hb_resource_job_send_master_hostname (HB_JOB_ARG *arg)
{
  const char *hostname = hb_find_host_name_of_master_server ();
  int error, rv;
  HB_PROC_ENTRY *proc = NULL;
  CSS_CONN_ENTRY *conn = NULL;

  rv = pthread_mutex_lock (&hb_Resource->lock);
  proc = hb_Resource->procs;
  while (proc)
    {
      MASTER_ER_LOG_DEBUG (ARG_FILE_LINE, "send_master_hostname type:%d, state:%d, knows_master_hostname:%d\n",
			   proc->type, proc->state, proc->knows_master_hostname);
      if (proc->type == HB_PTYPE_SERVER && proc->state >= HB_PSTATE_REGISTERED)
	{
	  if (proc->knows_master_hostname)
	    {
	      pthread_mutex_unlock (&hb_Resource->lock);
	      return;
	    }

	  conn = proc->conn;
	  break;
	}
      proc = proc->next;
    }
  pthread_mutex_unlock (&hb_Resource->lock);

  if (proc != NULL)
    {
      if (hostname == NULL)
	{
	  proc->knows_master_hostname = false;
	  current_master_hostname = NULL;
	  return;
	}

      if (current_master_hostname == NULL)
	{
	  current_master_hostname = const_cast<char *> (hostname);
	  proc->knows_master_hostname = false;
	}
      else if (current_master_hostname == hostname && proc->knows_master_hostname)
	{
	  return;
	}
      else if (current_master_hostname != hostname)
	{
	  proc->knows_master_hostname = false;
	}

      error = css_send_to_my_server_the_master_hostname (hostname, proc, conn);
      assert (error == NO_ERROR);
    }

  error =
	  hb_resource_job_queue (HB_RJOB_SEND_MASTER_HOSTNAME, NULL,
				 prm_get_integer_value (PRM_ID_HA_UPDATE_HOSTNAME_INTERVAL_IN_MSECS));
  assert (error == NO_ERROR);

  if (arg)
    {
      free_and_init (arg);
    }
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
      p->being_shutdown = false;
      p->server_hang = false;
      p->knows_master_hostname = false;
      LSA_SET_NULL (&p->prev_eof);
      LSA_SET_NULL (&p->curr_eof);

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
hb_remove_proc (HB_PROC_ENTRY *entry_p)
{
  if (entry_p)
    {
      hb_list_remove ((HB_LIST *) entry_p);
      free_and_init (entry_p);
    }
}

/*
 * hb_remove_all_procs() -
 *   return: none
 *
 *   first(in):
 */
static void
hb_remove_all_procs (HB_PROC_ENTRY *first)
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
  HB_PROC_ENTRY *proc;

  for (proc = hb_Resource->procs; proc; proc = proc->next)
    {
      if (strcmp (proc->args, args))
	{
	  continue;
	}
      return proc;
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
  HB_PROC_ENTRY *proc;

  for (proc = hb_Resource->procs; proc; proc = proc->next)
    {
      if (proc->pid != pid)
	{
	  continue;
	}
      return proc;

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
  HB_PROC_ENTRY *proc;

  for (proc = hb_Resource->procs; proc; proc = proc->next)
    {
      if (proc->sfd != sfd)
	{
	  continue;
	}
      return proc;

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

  for (i = 0; i < HB_MAX_NUM_PROC_ARGV; i++, arg++, argv += HB_MAX_SZ_PROC_ARGV)
    {
      if ((*argv == 0))
	{
	  break;
	}

      (*arg) = argv;
    }
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
hb_cleanup_conn_and_start_process (CSS_CONN_ENTRY *conn, SOCKET sfd)
{
  int error, rv;
  char error_string[LINE_MAX] = "";
  HB_PROC_ENTRY *proc;
  HB_JOB_ARG *job_arg;
  HB_RESOURCE_JOB_ARG *proc_arg;

  css_remove_entry_by_conn (conn, &css_Master_socket_anchor);

  if (hb_Resource == NULL)
    {
      return;
    }

  rv = pthread_mutex_lock (&hb_Resource->lock);
  proc = hb_return_proc_by_fd (sfd);
  if (proc == NULL)
    {
      MASTER_ER_LOG_DEBUG (ARG_FILE_LINE, "cannot find process. (fd:%d). \n", sfd);
      pthread_mutex_unlock (&hb_Resource->lock);
      return;
    }

  proc->conn = NULL;
  proc->sfd = INVALID_SOCKET;

  if (proc->state < HB_PSTATE_REGISTERED)
    {
      MASTER_ER_LOG_DEBUG (ARG_FILE_LINE, "unexpected process's state. " "(fd:%d, pid:%d, state:%d, args:{%s}). \n",
			   sfd, proc->pid, proc->state, proc->args);
      /*
       * Do not delete process entry.
       * process entry will be removed by resource job.
       */

      pthread_mutex_unlock (&hb_Resource->lock);
      return;
    }

  gettimeofday (&proc->ktime, NULL);
#if defined (HB_VERBOSE_DEBUG)
  MASTER_ER_LOG_DEBUG (ARG_FILE_LINE, "process terminated. (args:{%s}, pid:%d, state:%d). \n", proc->args, proc->pid,
		       proc->state);
#endif

  snprintf (error_string, LINE_MAX, "(pid:%d, args:%s)", proc->pid, proc->args);

  if (proc->being_shutdown)
    {
      MASTER_ER_SET (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HB_PROCESS_EVENT, 2, "Process shutdown detected",
		     error_string);
    }
  else
    {
      MASTER_ER_SET (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HB_PROCESS_EVENT, 2, "Process failure detected",
		     error_string);
    }

  if (hb_Resource->state == cubhb::node_entry::MASTER && proc->type == HB_PTYPE_SERVER && !hb_Cluster->is_isolated)
    {
      if (HB_GET_ELAPSED_TIME (proc->ktime, proc->rtime) <
	  prm_get_integer_value (PRM_ID_HA_UNACCEPTABLE_PROC_RESTART_TIMEDIFF_IN_MSECS))
	{
	  /* demote the current node */
	  hb_Resource->state = cubhb::node_entry::SLAVE;

	  snprintf (error_string, LINE_MAX, "(args:%s)", proc->args);
	  MASTER_ER_SET (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HB_PROCESS_EVENT, 2,
			 "Process failure repeated within a short period of time. " "The current node will be demoted",
			 error_string);

	  /* shutdown working server processes to change its role to slave */
	  error = hb_resource_job_queue (HB_RJOB_DEMOTE_START_SHUTDOWN, NULL, HB_JOB_TIMER_IMMEDIATELY);
	  assert (error == NO_ERROR);
	}
    }

  job_arg = (HB_JOB_ARG *) malloc (sizeof (HB_JOB_ARG));
  if (job_arg == NULL)
    {
      pthread_mutex_unlock (&hb_Resource->lock);
      MASTER_ER_SET (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (HB_JOB_ARG));
      return;
    }

  proc_arg = & (job_arg->resource_job_arg);
  proc_arg->pid = proc->pid;
  memcpy ((void *) &proc_arg->args[0], proc->args, sizeof (proc_arg->args));
  proc_arg->retries = 0;
  proc_arg->max_retries = prm_get_integer_value (PRM_ID_HA_MAX_PROCESS_START_CONFIRM);
  gettimeofday (&proc_arg->ftime, NULL);

  proc->state = HB_PSTATE_DEAD;
  proc->server_hang = false;
  LSA_SET_NULL (&proc->prev_eof);
  LSA_SET_NULL (&proc->curr_eof);

  pthread_mutex_unlock (&hb_Resource->lock);

  error = hb_resource_job_queue (HB_RJOB_PROC_START, job_arg, HB_JOB_TIMER_WAIT_A_SECOND);
  assert (error == NO_ERROR);
}

/*
 * hb_is_regiestered_process() -
 *   return: none
 *
 *   conn(in):
 */
bool
hb_is_registered_process (CSS_CONN_ENTRY *conn, char *args)
{
  HB_PROC_ENTRY *proc;

  if (hb_Resource == NULL)
    {
      return false;
    }

  (void) pthread_mutex_lock (&hb_Resource->lock);
  if (hb_Resource->shutdown)
    {
      pthread_mutex_unlock (&hb_Resource->lock);
      return false;
    }

  proc = hb_return_proc_by_args (args);
  (void) pthread_mutex_unlock (&hb_Resource->lock);

  return proc != NULL;
}

/*
 * hb_register_new_process() -
 *   return: none
 *
 *   conn(in):
 *   rid(in):
 */
void
hb_register_new_process (CSS_CONN_ENTRY *conn)
{
  int rv, buffer_size;
  HBP_PROC_REGISTER *hbp_proc_register = NULL;
  HB_PROC_ENTRY *proc;
  unsigned char proc_state = HB_PSTATE_UNKNOWN;
  char buffer[HB_BUFFER_SZ];
  char error_string[LINE_MAX] = "";

  if (hb_Resource == NULL)
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

  rv = pthread_mutex_lock (&hb_Resource->lock);
  if (hb_Resource->shutdown)
    {
      pthread_mutex_unlock (&hb_Resource->lock);
      css_remove_entry_by_conn (conn, &css_Master_socket_anchor);
      return;
    }

  proc = hb_return_proc_by_args (hbp_proc_register->args);
  if (proc == NULL)
    {
      proc = hb_alloc_new_proc ();
      if (proc == NULL)
	{
	  pthread_mutex_unlock (&hb_Resource->lock);
	  css_remove_entry_by_conn (conn, &css_Master_socket_anchor);
	  return;
	}
      else
	{
	  proc_state = HB_PSTATE_REGISTERED;	/* first register */
	  gettimeofday (&proc->frtime, NULL);
	}
    }
  else
    {
      proc_state = (proc->state == HB_PSTATE_STARTED) ? HB_PSTATE_NOT_REGISTERED
		   /* restarted by heartbeat */ :
		   HB_PSTATE_UNKNOWN /* already registered */ ;
    }

  if ((proc_state == HB_PSTATE_REGISTERED)
      || (proc_state == HB_PSTATE_NOT_REGISTERED && proc->pid == (int) ntohl (hbp_proc_register->pid)
	  && ! (kill (proc->pid, 0) && errno == ESRCH)))
    {
      proc->state = proc_state;
      proc->sfd = conn->fd;
      proc->conn = conn;
      gettimeofday (&proc->rtime, NULL);
      proc->changemode_gap = 0;
      proc->server_hang = false;

      if (proc->state == HB_PSTATE_REGISTERED)
	{
	  proc->pid = ntohl (hbp_proc_register->pid);
	  proc->type = ntohl (hbp_proc_register->type);
	  if (proc->type == HB_PTYPE_SERVER)
	    {
	      proc->state = HB_PSTATE_REGISTERED_AND_STANDBY;
	    }
	  memcpy ((void *) &proc->exec_path[0], (void *) &hbp_proc_register->exec_path[0], sizeof (proc->exec_path));
	  memcpy ((void *) &proc->args[0], (void *) &hbp_proc_register->args[0], sizeof (proc->args));
	  memcpy ((void *) &proc->argv[0], (void *) &hbp_proc_register->argv[0], sizeof (proc->argv));
	  hb_Resource->num_procs++;
	}

      assert (proc->pid > 0);

#if defined (HB_VERBOSE_DEBUG)
      MASTER_ER_LOG_DEBUG (ARG_FILE_LINE,
			   "hbp_proc_register. (sizeof(hbp_proc_register):%d, \n"
			   "type:%d, state:%d, pid:%d, exec_path:{%s}, " "args:{%s}). \n", sizeof (HBP_PROC_REGISTER),
			   proc->type, proc->state, proc->pid, proc->exec_path, proc->args);
      hb_print_procs ();
#endif

      snprintf (error_string, LINE_MAX, "%s (pid:%d, state:%s, args:%s)", HB_RESULT_SUCCESS_STR,
		ntohl (hbp_proc_register->pid), hb_process_state_string (proc->type, proc->state),
		hbp_proc_register->args);
      MASTER_ER_SET (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HB_PROCESS_EVENT, 2, "Registered as local process entries",
		     error_string);

      pthread_mutex_unlock (&hb_Resource->lock);
      return;
    }

  pthread_mutex_unlock (&hb_Resource->lock);

  snprintf (error_string, LINE_MAX, "%s (expected pid: %d, pid:%d, state:%s, args:%s)", HB_RESULT_FAILURE_STR,
	    proc->pid, ntohl (hbp_proc_register->pid), hb_process_state_string (proc->type, proc->state),
	    hbp_proc_register->args);
  MASTER_ER_SET (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HB_PROCESS_EVENT, 2, "Registered as local process entries",
		 error_string);

  css_remove_entry_by_conn (conn, &css_Master_socket_anchor);
}

/*
 * hb_resource_send_changemode -
 *   return: none
 *
 *   proc(in):
 */
static int
hb_resource_send_changemode (HB_PROC_ENTRY *proc)
{
  int error = NO_ERROR;
  HA_SERVER_STATE state;
  int nstate;
  int sig = 0;
  char error_string[LINE_MAX] = "";

  if (proc->conn == NULL)
    {
      return ER_FAILED;
    }

  if (proc->changemode_gap == HB_MAX_CHANGEMODE_DIFF_TO_TERM)
    {
      sig = SIGTERM;
    }
  else if (proc->changemode_gap >= HB_MAX_CHANGEMODE_DIFF_TO_KILL)
    {
      sig = SIGKILL;
    }

  if (sig)
    {
      assert (proc->pid > 0);
      if (proc->pid > 0 && (kill (proc->pid, 0) == 0 || errno != ESRCH))
	{
	  snprintf (error_string, sizeof (error_string),
		    "process does not respond for a long time. kill pid %d signal %d.", proc->pid, sig);
	  MASTER_ER_SET (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HB_PROCESS_EVENT, 2, "Process failure detected",
			 error_string);
	  kill (proc->pid, sig);
	}
      return ER_FAILED;
    }

  switch (hb_Resource->state)
    {
    case cubhb::node_entry::MASTER:
    {
      state = HA_SERVER_STATE_ACTIVE;
      proc->knows_master_hostname = true;
    }
    break;
    case cubhb::node_entry::TO_BE_SLAVE:
    {
      state = HA_SERVER_STATE_STANDBY;
      proc->knows_master_hostname = false;
    }
    break;
    case cubhb::node_entry::SLAVE:
    default:
    {
      proc->knows_master_hostname = false;
      return ER_FAILED;
    }
    break;
    }

  error = css_send_heartbeat_request (proc->conn, SERVER_CHANGE_HA_MODE);
  if (NO_ERRORS != error)
    {
      return ER_FAILED;
    }

  nstate = htonl ((int) state);
  error = css_send_heartbeat_data (proc->conn, (char *) &nstate, sizeof (nstate));
  if (NO_ERRORS != error)
    {
      snprintf (error_string, LINE_MAX,
		"Failed to send changemode request to the server. " "(state:%d[%s], args:[%s], pid:%d)", state,
		css_ha_server_state_string (state), proc->args, proc->pid);
      MASTER_ER_SET (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HA_GENERIC_ERROR, 1, error_string);

      return ER_FAILED;
    }

  snprintf (error_string, LINE_MAX, "Send changemode request to the server. " "(state:%d[%s], args:[%s], pid:%d)",
	    state, css_ha_server_state_string (state), proc->args, proc->pid);
  MASTER_ER_SET (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HA_GENERIC_ERROR, 1, error_string);

  return NO_ERROR;
}

/*
 * hb_resource_receive_changemode -
 *   return: none
 *
 *   conn(in):
 */
void
hb_resource_receive_changemode (CSS_CONN_ENTRY *conn)
{
  int sfd, rv;
  HB_PROC_ENTRY *proc;
  HA_SERVER_STATE state;
  int nstate;
  char error_string[LINE_MAX] = "";

  if (hb_Resource == NULL)
    {
      return;
    }

  rv = css_receive_heartbeat_data (conn, (char *) &nstate, sizeof (nstate));
  if (rv != NO_ERRORS)
    {
      return;
    }
  state = (HA_SERVER_STATE) ntohl (nstate);

  sfd = conn->fd;
  rv = pthread_mutex_lock (&hb_Cluster->lock);
  rv = pthread_mutex_lock (&hb_Resource->lock);
  proc = hb_return_proc_by_fd (sfd);
  if (proc == NULL || proc->state == HB_PSTATE_DEREGISTERED)
    {
      pthread_mutex_unlock (&hb_Resource->lock);
      pthread_mutex_unlock (&hb_Cluster->lock);
      return;
    }

  snprintf (error_string, LINE_MAX, "Receive changemode response from the server. " "(state:%d[%s], args:[%s], pid:%d)",
	    state, css_ha_server_state_string (state), proc->args, proc->pid);
  MASTER_ER_SET (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HA_GENERIC_ERROR, 1, error_string);

  switch (state)
    {
    case HA_SERVER_STATE_ACTIVE:
      proc->state = HB_PSTATE_REGISTERED_AND_ACTIVE;
      proc->knows_master_hostname = true;
      break;

    case HA_SERVER_STATE_TO_BE_ACTIVE:
      proc->state = HB_PSTATE_REGISTERED_AND_TO_BE_ACTIVE;
      proc->knows_master_hostname = true;
      break;

    case HA_SERVER_STATE_STANDBY:
      proc->state = HB_PSTATE_REGISTERED_AND_STANDBY;
      hb_Cluster->state = cubhb::node_entry::SLAVE;
      hb_Resource->state = cubhb::node_entry::SLAVE;
      proc->knows_master_hostname = false;
      break;

    case HA_SERVER_STATE_TO_BE_STANDBY:
      proc->state = HB_PSTATE_REGISTERED_AND_TO_BE_STANDBY;
      proc->knows_master_hostname = false;
      break;

    default:
      break;
    }

  proc->changemode_gap = 0;

  pthread_mutex_unlock (&hb_Resource->lock);
  pthread_mutex_unlock (&hb_Cluster->lock);
}

/*
 * hb_resource_check_server_log_grow() -
 *      check if active server is alive
 *   return: none
 *
 */
static bool
hb_resource_check_server_log_grow (void)
{
  int dead_cnt = 0;
  HB_PROC_ENTRY *proc;

  for (proc = hb_Resource->procs; proc; proc = proc->next)
    {
      if (proc->type != HB_PTYPE_SERVER || proc->state != HB_PSTATE_REGISTERED_AND_ACTIVE || proc->server_hang)
	{
	  continue;
	}

      if (LSA_ISNULL (&proc->curr_eof))
	{
	  continue;
	}

      if (LSA_GT (&proc->curr_eof, &proc->prev_eof))
	{
	  LSA_COPY (&proc->prev_eof, &proc->curr_eof);
	}
      else
	{
	  proc->server_hang = true;
	  dead_cnt++;
	}
    }

  return dead_cnt <= 0;
}

/*
 * hb_resource_send_get_eof -
 *   return: none
 *
 *   proc(in):
 */
static void
hb_resource_send_get_eof (void)
{
  HB_PROC_ENTRY *proc;

  if (hb_Resource->state != cubhb::node_entry::MASTER)
    {
      return;
    }

  for (proc = hb_Resource->procs; proc; proc = proc->next)
    {
      if (proc->state == HB_PSTATE_REGISTERED_AND_ACTIVE)
	{
	  css_send_heartbeat_request (proc->conn, SERVER_GET_EOF);
	}
    }
}

/*
 * hb_resource_receive_get_eof -
 *   return: none
 *
 *   conn(in):
 */
void
hb_resource_receive_get_eof (CSS_CONN_ENTRY *conn)
{
  int rv;
  HB_PROC_ENTRY *proc;
  OR_ALIGNED_BUF (OR_LOG_LSA_ALIGNED_SIZE) a_reply;
  char *reply;

  reply = OR_ALIGNED_BUF_START (a_reply);

  rv = css_receive_heartbeat_data (conn, reply, OR_ALIGNED_BUF_SIZE (a_reply));
  if (rv != NO_ERRORS)
    {
      return;
    }

  rv = pthread_mutex_lock (&hb_Resource->lock);

  proc = hb_return_proc_by_fd (conn->fd);
  if (proc == NULL)
    {
      MASTER_ER_LOG_DEBUG (ARG_FILE_LINE, "cannot find process. (fd:%d). \n", conn->fd);
      pthread_mutex_unlock (&hb_Resource->lock);
      return;
    }

  if (proc->state == HB_PSTATE_REGISTERED_AND_ACTIVE)
    {
      or_unpack_log_lsa (reply, &proc->curr_eof);
    }

  MASTER_ER_LOG_DEBUG (ARG_FILE_LINE, "received eof [%lld|%lld]\n", proc->curr_eof.pageid, proc->curr_eof.offset);

  pthread_mutex_unlock (&hb_Resource->lock);
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
static void *
hb_thread_cluster_worker (void *arg)
{
  HB_JOB_ENTRY *job;
  /* *INDENT-OFF* */
  cuberr::context er_context (true);
  /* *INDENT-ON* */

#if defined (HB_VERBOSE_DEBUG)
  MASTER_ER_LOG_DEBUG (ARG_FILE_LINE, "thread started. (thread:{%s}, tid:%d).\n", __func__, THREAD_ID ());
#endif

  while (!cluster_Jobs->shutdown)
    {
      while ((job = hb_cluster_job_dequeue ()) != NULL)
	{
	  job->func (job->arg);
	  free_and_init (job);
	}

      SLEEP_MILISEC (0, 10);
    }

#if defined (HB_VERBOSE_DEBUG)
  MASTER_ER_LOG_DEBUG (ARG_FILE_LINE, "thread exit.\n");
#endif

  return NULL;
}

/*
 * hb_thread_cluster_reader -
 *   return: none
 *
 *   arg(in):
 */
static void *
hb_thread_cluster_reader (void *arg)
{
  int error;
  SOCKET sfd;
  char buffer[HB_BUFFER_SZ + MAX_ALIGNMENT], *aligned_buffer;
  int len;
  struct pollfd po[1] = { {0, 0, 0} };

  struct sockaddr_in from;
  socklen_t from_len;

  /* *INDENT-OFF* */
  cuberr::context er_context (true);
  /* *INDENT-ON* */

#if defined (HB_VERBOSE_DEBUG)
  MASTER_ER_LOG_DEBUG (ARG_FILE_LINE, "thread started. (thread:{%s}, tid:%d).\n", __func__, THREAD_ID ());
#endif

  aligned_buffer = PTR_ALIGN (buffer, MAX_ALIGNMENT);
  sfd = hb_Cluster->sfd;
  while (!hb_Cluster->shutdown)
    {
      po[0].fd = sfd;
      po[0].events = POLLIN;
      error = poll (po, 1, 1);
      if (error <= 0)
	{
	  continue;
	}

      if ((po[0].revents & POLLIN) && sfd == hb_Cluster->sfd)
	{
	  from_len = sizeof (from);
	  len = recvfrom (sfd, (void *) aligned_buffer, HB_BUFFER_SZ, 0, (struct sockaddr *) &from, &from_len);
	  if (len > 0)
	    {
	      hb_cluster_receive_heartbeat (aligned_buffer, len, &from, from_len);
	    }
	}
    }

#if defined (HB_VERBOSE_DEBUG)
  MASTER_ER_LOG_DEBUG (ARG_FILE_LINE, "thread exit.\n");
#endif

  return NULL;
}

/*
 * hb_thread_resource_worker -
 *   return: none
 *
 *   arg(in):
 */
static void *
hb_thread_resource_worker (void *arg)
{
  HB_JOB_ENTRY *job;
  /* *INDENT-OFF* */
  cuberr::context er_context (true);
  /* *INDENT-ON* */

#if defined (HB_VERBOSE_DEBUG)
  MASTER_ER_LOG_DEBUG (ARG_FILE_LINE, "thread started. (thread:{%s}, tid:%d).\n", __func__, THREAD_ID ());
#endif

  while (!resource_Jobs->shutdown)
    {
      while ((job = hb_resource_job_dequeue ()) != NULL)
	{
	  job->func (job->arg);
	  free_and_init (job);
	}

      SLEEP_MILISEC (0, 10);
    }

#if defined (HB_VERBOSE_DEBUG)
  MASTER_ER_LOG_DEBUG (ARG_FILE_LINE, "thread exit.\n");
#endif

  return NULL;
}

/*
 * hb_thread_resource_worker -
 *   return: none
 *
 *   arg(in):
 */
static void *
hb_thread_check_disk_failure (void *arg)
{
  int rv, error;
  int interval;
  INT64 remaining_time_msecs = 0;
  /* *INDENT-OFF* */
  cuberr::context er_context (true);
  /* *INDENT-ON* */

#if defined (HB_VERBOSE_DEBUG)
  MASTER_ER_LOG_DEBUG (ARG_FILE_LINE, "thread started. (thread:{%s}, tid:%d).\n", __func__, THREAD_ID ());
#endif

  while (!hb_Resource->shutdown)
    {
      interval = prm_get_integer_value (PRM_ID_HA_CHECK_DISK_FAILURE_INTERVAL_IN_SECS);
      if (interval > 0 && remaining_time_msecs <= 0)
	{
	  rv = pthread_mutex_lock (&css_Master_socket_anchor_lock);
	  rv = pthread_mutex_lock (&hb_Cluster->lock);
	  rv = pthread_mutex_lock (&hb_Resource->lock);

	  if (!hb_Cluster->is_isolated && hb_Resource->state == cubhb::node_entry::MASTER)
	    {
	      if (!hb_resource_check_server_log_grow ())
		{
		  /* be silent to avoid blocking write operation on disk */
		  hb_disable_er_log (HB_NOLOG_DEMOTE_ON_DISK_FAIL, NULL);
		  hb_Resource->state = cubhb::node_entry::SLAVE;

		  pthread_mutex_unlock (&hb_Resource->lock);
		  pthread_mutex_unlock (&hb_Cluster->lock);
		  pthread_mutex_unlock (&css_Master_socket_anchor_lock);

		  error = hb_resource_job_queue (HB_RJOB_DEMOTE_START_SHUTDOWN, NULL, HB_JOB_TIMER_IMMEDIATELY);
		  assert (error == NO_ERROR);

		  continue;
		}
	    }

	  if (hb_Resource->state == cubhb::node_entry::MASTER)
	    {
	      hb_resource_send_get_eof ();
	    }
	  pthread_mutex_unlock (&hb_Resource->lock);
	  pthread_mutex_unlock (&hb_Cluster->lock);
	  pthread_mutex_unlock (&css_Master_socket_anchor_lock);

	  remaining_time_msecs = interval * 1000;
	}

      SLEEP_MILISEC (0, HB_DISK_FAILURE_CHECK_TIMER_IN_MSECS);
      if (interval > 0)
	{
	  remaining_time_msecs -= HB_DISK_FAILURE_CHECK_TIMER_IN_MSECS;
	}
    }

#if defined (HB_VERBOSE_DEBUG)
  MASTER_ER_LOG_DEBUG (ARG_FILE_LINE, "thread exit.\n");
#endif

  return NULL;
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

  if (cluster_Jobs == NULL)
    {
      cluster_Jobs = (HB_JOB *) malloc (sizeof (HB_JOB));
      if (cluster_Jobs == NULL)
	{
	  MASTER_ER_SET (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (HB_JOB));
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}

      pthread_mutex_init (&cluster_Jobs->lock, NULL);
    }

  rv = pthread_mutex_lock (&cluster_Jobs->lock);
  cluster_Jobs->shutdown = false;
  cluster_Jobs->num_jobs = 0;
  cluster_Jobs->jobs = NULL;
  cluster_Jobs->job_funcs = &hb_cluster_jobs[0];
  pthread_mutex_unlock (&cluster_Jobs->lock);

  error = hb_cluster_job_queue (HB_CJOB_INIT, NULL, HB_JOB_TIMER_IMMEDIATELY);
  if (error != NO_ERROR)
    {
      assert (false);
      return ER_FAILED;
    }

  return NO_ERROR;
}

/*
 * hb_cluster_initialize -
 *   return: NO_ERROR or ER_FAILED
 *
 */
static int
hb_cluster_initialize ()
{
  if (hb_Cluster == NULL)
    {
      hb_Cluster = new cubhb::cluster ();
      pthread_mutex_init (&hb_Cluster->lock, NULL);
    }

  pthread_mutex_lock (&hb_Cluster->lock);
  int error_code = hb_Cluster->init ();
  if (error_code != NO_ERROR)
    {
      pthread_mutex_unlock (&hb_Cluster->lock);
      hb_Cluster->stop ();
      delete hb_Cluster;
      hb_Cluster = NULL;
      return error_code;
    }

  error_code = hb_Cluster->listen ();
  pthread_mutex_unlock (&hb_Cluster->lock);

  if (error_code != NO_ERROR)
    {
      hb_Cluster->stop ();
      delete hb_Cluster;
      hb_Cluster = NULL;
    }

  return error_code;
}

/*
 * hb_resource_initialize -
 *   return: NO_ERROR or ER_FAILED
 *
 */
static int
hb_resource_initialize (void)
{
  int rv;

  if (hb_Resource == NULL)
    {
      hb_Resource = (HB_RESOURCE *) malloc (sizeof (HB_RESOURCE));
      if (hb_Resource == NULL)
	{
	  MASTER_ER_SET (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (HB_RESOURCE));
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}

      pthread_mutex_init (&hb_Resource->lock, NULL);
    }

  rv = pthread_mutex_lock (&hb_Resource->lock);
  hb_Resource->shutdown = false;
  hb_Resource->state = cubhb::node_entry::SLAVE;
  hb_Resource->num_procs = 0;
  hb_Resource->procs = NULL;
  pthread_mutex_unlock (&hb_Resource->lock);

  return NO_ERROR;
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

  if (resource_Jobs == NULL)
    {
      resource_Jobs = (HB_JOB *) malloc (sizeof (HB_JOB));
      if (resource_Jobs == NULL)
	{
	  MASTER_ER_SET (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (HB_JOB));
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}

      pthread_mutex_init (&resource_Jobs->lock, NULL);
    }

  rv = pthread_mutex_lock (&resource_Jobs->lock);
  resource_Jobs->shutdown = false;
  resource_Jobs->num_jobs = 0;
  resource_Jobs->jobs = NULL;
  resource_Jobs->job_funcs = &hb_resource_jobs[0];
  pthread_mutex_unlock (&resource_Jobs->lock);

  error = hb_resource_job_queue (HB_RJOB_CHANGE_MODE, NULL,
				 prm_get_integer_value (PRM_ID_HA_INIT_TIMER_IN_MSECS) +
				 prm_get_integer_value (PRM_ID_HA_FAILOVER_WAIT_TIME_IN_MSECS));
  if (error != NO_ERROR)
    {
      assert (false);
      return ER_FAILED;
    }

  /* TODO add other timers */
  error = hb_resource_job_queue (HB_RJOB_SEND_MASTER_HOSTNAME, NULL,
				 prm_get_integer_value (PRM_ID_HA_INIT_TIMER_IN_MSECS) +
				 prm_get_integer_value (PRM_ID_HA_FAILOVER_WAIT_TIME_IN_MSECS));
  if (error != NO_ERROR)
    {
      assert (false);
      return ER_FAILED;
    }

  return NO_ERROR;
}

/*
 * hb_thread_initialize -
 *   return: NO_ERROR or ER_FAILED
 *
 */
static int
hb_thread_initialize (void)
{
  int rv;

  pthread_attr_t thread_attr;
  size_t ts_size;
  pthread_t cluster_worker_th;
  pthread_t resource_worker_th;
  pthread_t check_disk_failure_th;

  rv = pthread_attr_init (&thread_attr);
  if (rv != 0)
    {
      MASTER_ER_SET_WITH_OSERROR (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_ATTR_INIT, 0);
      return ER_CSS_PTHREAD_ATTR_INIT;
    }

  rv = pthread_attr_setdetachstate (&thread_attr, PTHREAD_CREATE_DETACHED);
  if (rv != 0)
    {
      MASTER_ER_SET_WITH_OSERROR (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_ATTR_SETDETACHSTATE, 0);
      return ER_CSS_PTHREAD_ATTR_SETDETACHSTATE;
    }

#if defined(AIX)
  /* AIX's pthread is slightly different from other systems. Its performance highly depends on the pthread's scope and
   * it's related kernel parameters. */
  rv =
	  pthread_attr_setscope (&thread_attr,
				 prm_get_bool_value (PRM_ID_PTHREAD_SCOPE_PROCESS) ? PTHREAD_SCOPE_PROCESS :
				 PTHREAD_SCOPE_SYSTEM);
#else /* AIX */
  rv = pthread_attr_setscope (&thread_attr, PTHREAD_SCOPE_SYSTEM);
#endif /* AIX */
  if (rv != 0)
    {
      MASTER_ER_SET_WITH_OSERROR (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_ATTR_SETSCOPE, 0);
      return ER_CSS_PTHREAD_ATTR_SETSCOPE;
    }

  /* Sun Solaris allocates 1M for a thread stack, and it is quite enough */
#if !defined(sun) && !defined(SOLARIS)
#if defined(_POSIX_THREAD_ATTR_STACKSIZE)
  rv = pthread_attr_getstacksize (&thread_attr, &ts_size);
  if (ts_size < (size_t) prm_get_bigint_value (PRM_ID_THREAD_STACKSIZE))
    {
      rv = pthread_attr_setstacksize (&thread_attr, prm_get_bigint_value (PRM_ID_THREAD_STACKSIZE));
      if (rv != 0)
	{
	  MASTER_ER_SET_WITH_OSERROR (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_ATTR_SETSTACKSIZE, 0);
	  return ER_CSS_PTHREAD_ATTR_SETSTACKSIZE;
	}

      pthread_attr_getstacksize (&thread_attr, &ts_size);
    }
#endif /* _POSIX_THREAD_ATTR_STACKSIZE */
#endif /* not sun && not SOLARIS */

  rv = pthread_create (&cluster_worker_th, &thread_attr, hb_thread_cluster_reader, NULL);
  if (rv != 0)
    {
      MASTER_ER_SET_WITH_OSERROR (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_CREATE, 0);
      return ER_CSS_PTHREAD_CREATE;
    }

  rv = pthread_create (&cluster_worker_th, &thread_attr, hb_thread_cluster_worker, NULL);
  if (rv != 0)
    {
      MASTER_ER_SET_WITH_OSERROR (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_CREATE, 0);
      return ER_CSS_PTHREAD_CREATE;
    }

  rv = pthread_create (&resource_worker_th, &thread_attr, hb_thread_resource_worker, NULL);
  if (rv != 0)
    {
      MASTER_ER_SET_WITH_OSERROR (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_CREATE, 0);
      return ER_CSS_PTHREAD_CREATE;
    }

  rv = pthread_create (&check_disk_failure_th, &thread_attr, hb_thread_check_disk_failure, NULL);
  if (rv != 0)
    {
      MASTER_ER_SET_WITH_OSERROR (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_CREATE, 0);
      return ER_CSS_PTHREAD_CREATE;
    }

  /* destroy thread_attribute */
  rv = pthread_attr_destroy (&thread_attr);
  if (rv != 0)
    {
      MASTER_ER_SET_WITH_OSERROR (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_ATTR_DESTROY, 0);
      return ER_CSS_PTHREAD_ATTR_DESTROY;
    }

  return NO_ERROR;
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

  hb_enable_er_log ();

  MASTER_ER_SET (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_HB_STARTED, 0);
#if defined (HB_VERBOSE_DEBUG)
  MASTER_ER_LOG_DEBUG (ARG_FILE_LINE, "heartbeat params. (ha_mode:%s, heartbeat_nodes:{%s}" ", ha_port_id:%d). \n",
		       (!HA_DISABLED ())? "yes" : "no",
		       prm_get_string_value (PRM_ID_HA_NODE_LIST), prm_get_integer_value (PRM_ID_HA_PORT_ID));
#endif

  sysprm_reload_and_init (NULL, NULL);
  error = hb_cluster_initialize ();
  if (error != NO_ERROR)
    {
      MASTER_ER_LOG_DEBUG (ARG_FILE_LINE, "hb_cluster_initialize failed. " "(error=%d). \n", error);
      util_log_write_errstr ("%s\n", db_error_string (3));
      goto error_return;
    }

#if defined (HB_VERBOSE_DEBUG)
  hb_print_nodes ();
#endif

  error = hb_cluster_job_initialize ();
  if (error != NO_ERROR)
    {
      MASTER_ER_LOG_DEBUG (ARG_FILE_LINE, "hb_cluster_job_initialize failed. " "(error=%d). \n", error);
      util_log_write_errstr ("%s\n", db_error_string (3));
      goto error_return;
    }

  error = hb_resource_initialize ();
  if (error != NO_ERROR)
    {
      MASTER_ER_LOG_DEBUG (ARG_FILE_LINE, "hb_resource_initialize failed. " "(error=%d). \n", error);
      util_log_write_errstr ("%s\n", db_error_string (3));
      goto error_return;
    }

  error = hb_resource_job_initialize ();
  if (error != NO_ERROR)
    {
      MASTER_ER_LOG_DEBUG (ARG_FILE_LINE, "hb_resource_job_initialize failed. " "(error=%d). \n", error);
      util_log_write_errstr ("%s\n", db_error_string (3));
      goto error_return;
    }

  error = hb_thread_initialize ();
  if (error != NO_ERROR)
    {
      MASTER_ER_LOG_DEBUG (ARG_FILE_LINE, "hb_thread_initialize failed. " "(error=%d). \n", error);
      util_log_write_errstr ("%s\n", db_error_string (3));
      goto error_return;
    }

  hb_Deactivate_immediately = false;

  return NO_ERROR;

error_return:
  if (hb_Cluster && !hb_Cluster->shutdown)
    {
      hb_cluster_cleanup ();
    }

  if (cluster_Jobs && !cluster_Jobs->shutdown)
    {
      hb_cluster_job_shutdown ();
    }

  if (hb_Resource && !hb_Resource->shutdown)
    {
      hb_resource_cleanup ();
    }

  if (resource_Jobs && !resource_Jobs->shutdown)
    {
      hb_resource_job_shutdown ();
    }

  return error;
}

/*
 * terminator
 */

/*
 * hb_resource_shutdown_all_ha_procs() -
 *   return:
 *
 */
static void
hb_resource_shutdown_all_ha_procs (void)
{
  HB_PROC_ENTRY *proc;
  SOCKET_QUEUE_ENTRY *sock_ent;
  char buffer[MASTER_TO_SRV_MSG_SIZE];

  /* set process state to deregister and close connection */
  for (proc = hb_Resource->procs; proc; proc = proc->next)
    {
      if (proc->conn)
	{
	  if (proc->type != HB_PTYPE_SERVER)
	    {
#if defined (HB_VERBOSE_DEBUG)
	      MASTER_ER_LOG_DEBUG (ARG_FILE_LINE, "remove socket-queue entry. (pid:%d). \n", proc->pid);
#endif
	      css_remove_entry_by_conn (proc->conn, &css_Master_socket_anchor);
	      proc->conn = NULL;
	      proc->sfd = INVALID_SOCKET;
	    }
	  else
	    {
	      /* In case of HA server, just send shutdown request */
	      sock_ent = css_return_entry_by_conn (proc->conn, &css_Master_socket_anchor);
	      assert_release (sock_ent == NULL || sock_ent->name != NULL);
	      if (sock_ent != NULL && sock_ent->name != NULL)
		{
		  memset (buffer, 0, sizeof (buffer));
		  snprintf (buffer, sizeof (buffer) - 1,
			    msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_MASTER, MASTER_MSG_SERVER_STATUS),
			    sock_ent->name + 1, 0);

		  css_process_start_shutdown (sock_ent, 0, buffer);
		}
	      else
		{
		  proc->conn = NULL;
		  proc->sfd = INVALID_SOCKET;
		}
	    }
	}
      else
	{
	  MASTER_ER_LOG_DEBUG (ARG_FILE_LINE, "invalid socket-queue entry. (pid:%d).\n", proc->pid);
	}

      proc->state = HB_PSTATE_DEREGISTERED;
    }
}

/*
 * hb_resource_cleanup() -
 *   return:
 *
 */
static void
hb_resource_cleanup (void)
{
  HB_PROC_ENTRY *proc;

  pthread_mutex_lock (&hb_Resource->lock);

  hb_resource_shutdown_all_ha_procs ();

  for (proc = hb_Resource->procs; proc; proc = proc->next)
    {
      if (proc->conn && proc->pid > 0)
	{
	  kill (proc->pid, SIGKILL);
	}
    }

  hb_remove_all_procs (hb_Resource->procs);
  hb_Resource->procs = NULL;
  hb_Resource->num_procs = 0;
  hb_Resource->state = cubhb::node_entry::UNKNOWN;
  hb_Resource->shutdown = true;
  pthread_mutex_unlock (&hb_Resource->lock);
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
}

/*
 * hb_cluster_cleanup() -
 *   return:
 *
 */
static void
hb_cluster_cleanup (void)
{
  pthread_mutex_lock (&hb_Cluster->lock);

  for (cubhb::node_entry *node : hb_Cluster->nodes)
    {
      if (hb_Cluster->hostname == node->get_hostname ())
	{
	  continue;
	}

      hb_cluster_send_heartbeat_req (node->get_hostname_cstr ());
      node->heartbeat_gap++;
    }

  hb_Cluster->stop ();

  pthread_mutex_unlock (&hb_Cluster->lock);
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
hb_node_state_string (cubhb::node_entry::node_state nstate)
{
  switch (nstate)
    {
    case cubhb::node_entry::UNKNOWN:
      return HB_NSTATE_UNKNOWN_STR;
    case cubhb::node_entry::SLAVE:
      return HB_NSTATE_SLAVE_STR;
    case cubhb::node_entry::TO_BE_MASTER:
      return HB_NSTATE_TO_BE_MASTER_STR;
    case cubhb::node_entry::TO_BE_SLAVE:
      return HB_NSTATE_TO_BE_SLAVE_STR;
    case cubhb::node_entry::MASTER:
      return HB_NSTATE_MASTER_STR;
    case cubhb::node_entry::REPLICA:
      return HB_NSTATE_REPLICA_STR;
    default:
      return "invalid";
    }
}

/*
 * hb_process_state_string -
 *   return: process state sring
 *
 *   ptype(in):
 *   pstate(in):
 */
const char *
hb_process_state_string (unsigned char ptype, int pstate)
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
      if (ptype == HB_PTYPE_SERVER)
	{
	  return HB_PSTATE_REGISTERED_AND_STANDBY_STR;
	}
      else
	{
	  return HB_PSTATE_REGISTERED_STR;
	}
    case HB_PSTATE_REGISTERED_AND_TO_BE_STANDBY:
      return HB_PSTATE_REGISTERED_AND_TO_BE_STANDBY_STR;
    case HB_PSTATE_REGISTERED_AND_ACTIVE:
      return HB_PSTATE_REGISTERED_AND_ACTIVE_STR;
    case HB_PSTATE_REGISTERED_AND_TO_BE_ACTIVE:
      return HB_PSTATE_REGISTERED_AND_TO_BE_ACTIVE_STR;
    default:
      return "invalid";
    }
}

/*
 * hb_ping_result_string -
 *   return: ping result string
 *
 *   ping_result(in):
 */
const char *
hb_ping_result_string (int ping_result)
{
  switch (ping_result)
    {
    case cubhb::ping_host::UNKNOWN:
      return HB_PING_UNKNOWN_STR;
    case cubhb::ping_host::SUCCESS:
      return HB_PING_SUCCESS_STR;
    case cubhb::ping_host::USELESS_HOST:
      return HB_PING_USELESS_HOST_STR;
    case cubhb::ping_host::SYS_ERR:
      return HB_PING_SYS_ERR_STR;
    case cubhb::ping_host::FAILURE:
      return HB_PING_FAILURE_STR;
    default:
      return "invalid";
    }
}

#if defined (ENABLE_UNUSED_FUNCTION)
static void
hb_deregister_nodes (char *node_to_dereg)
{
  const char *delim = ":";
  int error;
  HB_PROC_ENTRY *proc;
  HB_JOB_ARG *job_arg;
  char *p, *savep;
  char *node_name;
  char *log_path;

  for (p = strtok_r (node_to_dereg, delim, &savep); p; p = strtok_r (NULL, delim, &savep))
    {

      (void) pthread_mutex_lock (&hb_Resource->lock);
      for (proc = hb_Resource->procs; proc; proc = proc->next)
	{
	  if (proc->type == HB_PTYPE_SERVER)
	    {
	      continue;
	    }

	  job_arg = NULL;

	  log_path = proc->argv[3];
	  node_name = strrchr (log_path, '_');
	  if (node_name)
	    {
	      node_name++;
	      if (strncmp (node_name, p, strlen (p)) == 0)
		{
		  job_arg = hb_deregister_process (proc);
		}
	    }
	  if (job_arg)
	    {
	      error = hb_resource_job_queue (HB_RJOB_PROC_DEREG, job_arg, HB_JOB_TIMER_IMMEDIATELY);
	      if (error != NO_ERROR)
		{
		  assert (false);
		  free_and_init (job_arg);
		}
	    }
	}
      (void) pthread_mutex_unlock (&hb_Resource->lock);
    }
}
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * hb_get_admin_info_string -
 *   return: none
 *
 *   str(out):
 */
void
hb_get_admin_info_string (char **str)
{
  int rv, buf_size = 0;
  char *p, *last;

  if (*str)
    {
      **str = 0;
      return;
    }

  rv = pthread_mutex_lock (&css_Master_er_log_enable_lock);

  if (css_Master_er_log_enabled || hb_Nolog_event_msg[0] == '\0')
    {
      pthread_mutex_unlock (&css_Master_er_log_enable_lock);
      return;
    }

  buf_size = strlen (HA_ADMIN_INFO_FORMAT_STRING);
  buf_size += strlen (HA_ADMIN_INFO_NOLOG_FORMAT_STRING);
  buf_size += strlen (HA_ADMIN_INFO_NOLOG_EVENT_FORMAT_STRING);
  buf_size += strlen (hb_Nolog_event_msg);

  *str = (char *) malloc (sizeof (char) * buf_size);
  if (*str == NULL)
    {
      pthread_mutex_unlock (&css_Master_er_log_enable_lock);
      MASTER_ER_SET (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (char) * buf_size);
      return;
    }
  **str = '\0';

  p = (char *) (*str);
  last = p + buf_size;

  p += snprintf (p, MAX ((last - p), 0), HA_ADMIN_INFO_FORMAT_STRING);
  p += snprintf (p, MAX ((last - p), 0), HA_ADMIN_INFO_NOLOG_FORMAT_STRING);
  p += snprintf (p, MAX ((last - p), 0), HA_ADMIN_INFO_NOLOG_EVENT_FORMAT_STRING, hb_Nolog_event_msg);

  pthread_mutex_unlock (&css_Master_er_log_enable_lock);
}

/*
 * hb_get_ping_host_info_string -
 *   return: none
 *
 *   str(out):
 */
void
hb_get_ping_host_info_string (char **str)
{
  int rv, buf_size = 0, required_size = 0;
  char *p, *last;
  bool valid_ping_host_exists;
  bool is_ping_check_enabled = true;

  if (hb_Cluster == NULL)
    {
      return;
    }

  if (*str)
    {
      **str = 0;
      return;
    }

  rv = pthread_mutex_lock (&hb_Cluster->lock);

  if (hb_Cluster->ping_hosts.empty ())
    {
      pthread_mutex_unlock (&hb_Cluster->lock);
      return;
    }

  /* refresh ping host info */
  valid_ping_host_exists = hb_Cluster->check_valid_ping_host ();

  if (!valid_ping_host_exists && !hb_cluster_is_isolated ())
    {
      is_ping_check_enabled = false;
    }

  if (is_ping_check_enabled != hb_Cluster->is_ping_check_enabled)
    {
      hb_cluster_job_set_expire_and_reorder (HB_CJOB_CHECK_VALID_PING_SERVER, HB_JOB_TIMER_IMMEDIATELY);
    }

  required_size = strlen (HA_PING_HOSTS_INFO_FORMAT_STRING);
  required_size += 7;		/* length of ping check status */

  buf_size += required_size;

  required_size = strlen (HA_PING_HOSTS_FORMAT_STRING);
  required_size += MAXHOSTNAMELEN;
  required_size += HB_PING_STR_SIZE;	/* length of ping test result */
  required_size *= hb_Cluster->ping_hosts.size ();

  buf_size += required_size;

  *str = (char *) malloc (sizeof (char) * buf_size);
  if (*str == NULL)
    {
      pthread_mutex_unlock (&hb_Cluster->lock);
      MASTER_ER_SET (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (char) * buf_size);
      return;
    }
  **str = '\0';

  p = (char *) (*str);
  last = p + buf_size;

  p += snprintf (p, MAX ((last - p), 0), HA_PING_HOSTS_INFO_FORMAT_STRING,
		 is_ping_check_enabled ? "enabled" : "disabled");

  for (cubhb::ping_host *host : hb_Cluster->ping_hosts)
    {
      p += snprintf (p, MAX ((last - p), 0), HA_PING_HOSTS_FORMAT_STRING, host->get_hostname_cstr (),
		     hb_ping_result_string (host->ping_result));
    }

  pthread_mutex_unlock (&hb_Cluster->lock);
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
  int rv, buf_size = 0, required_size = 0;
  char *p, *last;
  char *ipv4_p;
  char ipv4_str[HB_IPV4_STR_LEN];

  if (hb_Cluster == NULL)
    {
      return;
    }

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
      required_size += 6;	/* length of score */
      required_size += strlen (HA_NODE_HEARTBEAT_GAP_FORMAT_STRING);
      required_size += 6;	/* length of missed heartbeat */
    }

  rv = pthread_mutex_lock (&hb_Cluster->lock);

  required_size *= hb_Cluster->nodes.size ();
  buf_size += required_size;

  /* unidentifed node info */
  required_size = strlen (HA_UI_NODE_FORMAT_STRING);
  required_size += HB_IPV4_STR_LEN;
  required_size += HB_MAX_GROUP_ID_LEN;
  required_size += HB_NSTATE_STR_SZ;

  required_size *= hb_Cluster->ui_nodes.size ();

  buf_size += required_size;

  *str = (char *) malloc (sizeof (char) * buf_size);
  if (*str == NULL)
    {
      pthread_mutex_unlock (&hb_Cluster->lock);
      MASTER_ER_SET (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (char) * buf_size);
      return;
    }
  **str = '\0';

  p = (char *) (*str);
  last = p + buf_size;

  p += snprintf (p, MAX ((last - p), 0), HA_NODE_INFO_FORMAT_STRING, hb_Cluster->hostname.c_str (),
		 hb_node_state_string (hb_Cluster->state));

  for (cubhb::node_entry *node : hb_Cluster->nodes)
    {
      p += snprintf (p, MAX ((last - p), 0), HA_NODE_FORMAT_STRING, node->get_hostname_cstr (), node->priority,
		     hb_node_state_string (node->state));
      if (verbose_yn)
	{
	  p += snprintf (p, MAX ((last - p), 0), HA_NODE_SCORE_FORMAT_STRING, node->score);
	  p += snprintf (p, MAX ((last - p), 0), HA_NODE_HEARTBEAT_GAP_FORMAT_STRING, node->heartbeat_gap);
	}
    }

  hb_Cluster->cleanup_ui_nodes ();

  std::chrono::system_clock::time_point now = std::chrono::system_clock::now ();
  for (cubhb::ui_node *ui_node : hb_Cluster->ui_nodes)
    {
      if ((now - ui_node->last_recv_time) > cubhb::HB_UI_NODE_CACHE_TIME_IN_MSECS)
	{
	  continue;
	}

      ipv4_p = (char *) &ui_node->saddr.sin_addr.s_addr;
      snprintf (ipv4_str, sizeof (ipv4_str), "%u.%u.%u.%u", (unsigned char) ipv4_p[0], (unsigned char) ipv4_p[1],
		(unsigned char) ipv4_p[2], (unsigned char) ipv4_p[3]);
      p += snprintf (p, MAX ((last - p), 0), HA_UI_NODE_FORMAT_STRING, ui_node->get_hostname_cstr (), ipv4_str,
		     ui_node->group_id.c_str (), hb_valid_result_string (ui_node->v_result));
    }

  pthread_mutex_unlock (&hb_Cluster->lock);
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

  if (hb_Resource == NULL)
    {
      return;
    }

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

  rv = pthread_mutex_lock (&hb_Resource->lock);

  required_size *= hb_Resource->num_procs;
  buf_size += required_size;

  *str = (char *) malloc (sizeof (char) * buf_size);
  if (*str == NULL)
    {
      pthread_mutex_unlock (&hb_Resource->lock);
      MASTER_ER_SET (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (char) * buf_size);
      return;
    }
  **str = '\0';

  p = (char *) (*str);
  last = p + buf_size;

  p += snprintf (p, MAX ((last - p), 0), HA_PROCESS_INFO_FORMAT_STRING, getpid (),
		 hb_node_state_string (hb_Resource->state));

  for (proc = hb_Resource->procs; proc; proc = proc->next)
    {
      sock_entq = css_return_entry_by_conn (proc->conn, &css_Master_socket_anchor);
      assert_release (sock_entq == NULL || sock_entq->name != NULL);
      if (sock_entq == NULL || sock_entq->name == NULL)
	{
	  continue;
	}

      switch (proc->type)
	{
	case HB_PTYPE_SERVER:
	  p += snprintf (p, MAX ((last - p), 0), HA_SERVER_PROCESS_FORMAT_STRING, sock_entq->name + 1, proc->pid,
			 hb_process_state_string (proc->type, proc->state));
	  break;
	case HB_PTYPE_COPYLOGDB:
	  p += snprintf (p, MAX ((last - p), 0), HA_COPYLOG_PROCESS_FORMAT_STRING, sock_entq->name + 1, proc->pid,
			 hb_process_state_string (proc->type, proc->state));
	  break;
	case HB_PTYPE_APPLYLOGDB:
	  p += snprintf (p, MAX ((last - p), 0), HA_APPLYLOG_PROCESS_FORMAT_STRING, sock_entq->name + 1, proc->pid,
			 hb_process_state_string (proc->type, proc->state));
	  break;
	default:
	  break;
	}

      if (verbose_yn)
	{
	  p += snprintf (p, MAX ((last - p), 0), HA_PROCESS_EXEC_PATH_FORMAT_STRING, proc->exec_path);
	  p += snprintf (p, MAX ((last - p), 0), HA_PROCESS_ARGV_FORMAT_STRING, proc->args);
	  p += snprintf (p, MAX ((last - p), 0), HA_PROCESS_REGISTER_TIME_FORMAT_STRING,
			 hb_strtime (time_str, sizeof (time_str), &proc->rtime));
	  p += snprintf (p, MAX ((last - p), 0), HA_PROCESS_DEREGISTER_TIME_FORMAT_STRING,
			 hb_strtime (time_str, sizeof (time_str), &proc->dtime));
	  p += snprintf (p, MAX ((last - p), 0), HA_PROCESS_SHUTDOWN_TIME_FORMAT_STRING,
			 hb_strtime (time_str, sizeof (time_str), &proc->ktime));
	  p += snprintf (p, MAX ((last - p), 0), HA_PROCESS_START_TIME_FORMAT_STRING,
			 hb_strtime (time_str, sizeof (time_str), &proc->stime));
	}
    }

  pthread_mutex_unlock (&hb_Resource->lock);
}

/*
 * hb_kill_process - kill a list of processes
 *   return: none
 *
 */
static void
hb_kill_process (pid_t *pids, int count)
{
  int error;
  int i = 0, j = 0;
  int max_retries, wait_time_in_secs;
  int signum = SIGTERM;
  bool finished;

  max_retries = 20;
  wait_time_in_secs = 3;
  for (i = 0; i < max_retries; i++)
    {
      finished = true;
      for (j = 0; j < count; j++)
	{
	  if (pids[j] > 0)
	    {
	      error = kill (pids[j], signum);
	      if (error && errno == ESRCH)
		{
		  pids[j] = 0;
		}
	      else
		{
		  finished = false;
		}
	    }
	}
      if (finished)
	{
	  return;
	}
      signum = 0;
      SLEEP_MILISEC (wait_time_in_secs, 0);
    }

  for (j = 0; j < count; j++)
    {
      if (pids[j] > 0)
	{
	  kill (pids[j], SIGKILL);
	}
    }
}

#if defined (ENABLE_OLD_REPLICATION)
/*
 * hb_kill_all_heartbeat_process -
 *   return: none
 *
 *   str(out):
 */
void
hb_kill_all_heartbeat_process (char **str)
{
  int rv, count, i;
  pid_t *pids;
  HB_PROC_ENTRY *proc;
  size_t size;

  if (hb_Resource == NULL)
    {
      return;
    }

  count = 0;
  pids = NULL;

  rv = pthread_mutex_lock (&hb_Resource->lock);
  for (proc = hb_Resource->procs; proc; proc = proc->next)
    {
      if (proc->type == HB_PTYPE_APPLYLOGDB || proc->type == HB_PTYPE_COPYLOGDB)
	{
	  size = sizeof (pid_t) * (count + 1);
	  pids = (pid_t *) realloc (pids, size);
	  if (pids == NULL)
	    {
	      pthread_mutex_unlock (&hb_Resource->lock);
	      MASTER_ER_SET (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, size);
	      return;
	    }
	  pids[count] = proc->pid;
	  count++;
	}
    }
  pthread_mutex_unlock (&hb_Resource->lock);

  for (i = 0; i < count; i++)
    {
      hb_deregister_by_pid (pids[i]);
    }

  free (pids);
}
#endif /* ENABLE_OLD_REPLICATION */

/*
 * hb_deregister_by_pid -
 *   return: none
 *
 *   pid(in):
 *   str(out):
 */
void
hb_deregister_by_pid (pid_t pid)
{
  int error = NO_ERROR;
  HB_PROC_ENTRY *proc;
  HB_JOB_ARG *job_arg;
  char error_string[LINE_MAX] = "";

  if (hb_Resource == NULL)
    {
      return;
    }

  (void) pthread_mutex_lock (&hb_Resource->lock);
  proc = hb_return_proc_by_pid (pid);
  if (proc == NULL)
    {
      (void) pthread_mutex_unlock (&hb_Resource->lock);
      snprintf (error_string, LINE_MAX, "%s. (cannot find process to deregister, pid:%d)", HB_RESULT_FAILURE_STR, pid);
      MASTER_ER_SET (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HB_COMMAND_EXECUTION, 2, HB_CMD_DEREGISTER_STR, error_string);
      return;
    }

  job_arg = hb_deregister_process (proc);
  (void) pthread_mutex_unlock (&hb_Resource->lock);

  if (job_arg)
    {
      error = hb_resource_job_queue (HB_RJOB_PROC_DEREG, job_arg, HB_JOB_TIMER_IMMEDIATELY);
      if (error != NO_ERROR)
	{
	  assert (false);
	  free_and_init (job_arg);
	}
    }

  snprintf (error_string, LINE_MAX, "%s. (pid:%d)", HB_RESULT_SUCCESS_STR, pid);
  MASTER_ER_SET (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_HB_COMMAND_EXECUTION, 2, HB_CMD_DEREGISTER_STR,
		 error_string);
}

/*
 * hb_deregister_by_args -
 *   return: none
 *
 *   args(in):
 *   str(out):
 */
void
hb_deregister_by_args (char *args)
{
  int error = NO_ERROR;
  HB_PROC_ENTRY *proc;
  HB_JOB_ARG *job_arg;
  char error_string[LINE_MAX] = "";

  if (hb_Resource == NULL)
    {
      return;
    }

  (void) pthread_mutex_lock (&hb_Resource->lock);
  proc = hb_return_proc_by_args (args);
  if (proc == NULL)
    {
      (void) pthread_mutex_unlock (&hb_Resource->lock);
      snprintf (error_string, LINE_MAX, "%s. (cannot find process to deregister, args:%s)", HB_RESULT_FAILURE_STR,
		args);
      MASTER_ER_SET (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_HB_COMMAND_EXECUTION, 2, HB_CMD_DEREGISTER_STR,
		     error_string);
      return;
    }

  job_arg = hb_deregister_process (proc);
  (void) pthread_mutex_unlock (&hb_Resource->lock);

  if (job_arg)
    {
      error = hb_resource_job_queue (HB_RJOB_PROC_DEREG, job_arg, HB_JOB_TIMER_IMMEDIATELY);
      if (error != NO_ERROR)
	{
	  assert (false);
	  free_and_init (job_arg);
	}
    }

  snprintf (error_string, LINE_MAX, "%s. (args:%s)", HB_RESULT_SUCCESS_STR, args);
  MASTER_ER_SET (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_HB_COMMAND_EXECUTION, 2, HB_CMD_DEREGISTER_STR,
		 error_string);
}

static HB_JOB_ARG *
hb_deregister_process (HB_PROC_ENTRY *proc)
{
  HB_JOB_ARG *job_arg;
  HB_RESOURCE_JOB_ARG *proc_arg;
  char error_string[LINE_MAX] = "";

  if ((proc->state < HB_PSTATE_DEAD) || (proc->state >= HB_PSTATE_MAX) || (proc->pid < 0))
    {
      snprintf (error_string, LINE_MAX, "%s. (unexpected process status or invalid pid, status:%d, pid:%d)",
		HB_RESULT_FAILURE_STR, proc->state, proc->pid);
      MASTER_ER_SET (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HB_COMMAND_EXECUTION, 2, HB_CMD_DEREGISTER_STR, error_string);
      return NULL;
    }

  gettimeofday (&proc->dtime, NULL);

  job_arg = (HB_JOB_ARG *) malloc (sizeof (HB_JOB_ARG));
  if (job_arg == NULL)
    {
      MASTER_ER_SET (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (HB_JOB_ARG));
      return NULL;
    }

  proc_arg = & (job_arg->resource_job_arg);
  proc_arg->pid = proc->pid;
  memcpy ((void *) &proc_arg->args[0], proc->args, sizeof (proc_arg->args));
  proc_arg->retries = 0;
  proc_arg->max_retries = prm_get_integer_value (PRM_ID_HA_MAX_PROCESS_DEREG_CONFIRM);
  gettimeofday (&proc_arg->ftime, NULL);

  proc->state = HB_PSTATE_DEREGISTERED;

  return job_arg;
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
  int error;
  char error_string[LINE_MAX] = "";

  error = hb_Cluster->reload ();
  if (error)
    {
      snprintf (error_string, LINE_MAX, "%s. (failed to reload CUBRID heartbeat configuration)", HB_RESULT_FAILURE_STR);
      MASTER_ER_SET (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HB_COMMAND_EXECUTION, 2, HB_CMD_RELOAD_STR, error_string);
      *str = NULL;
    }
  else
    {
      snprintf (error_string, LINE_MAX, "%s.", HB_RESULT_SUCCESS_STR);
      MASTER_ER_SET (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HB_COMMAND_EXECUTION, 2, HB_CMD_RELOAD_STR, error_string);
      hb_get_node_info_string (str, false);

      snprintf (error_string, LINE_MAX, "\n%s", (str && *str) ? *str : "");
      MASTER_ER_SET (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HB_COMMAND_EXECUTION, 2, HB_CMD_RELOAD_STR, error_string);
    }
}

/*
 * hb_prepare_deactivate_heartbeat - shutdown all HA processes
 *      to deactivate heartbeat
 *   return:
 *
 */
int
hb_prepare_deactivate_heartbeat (void)
{
  int rv, error = NO_ERROR;
  char error_string[LINE_MAX] = "";

  if (hb_Cluster == NULL || hb_Resource == NULL)
    {
      return ER_FAILED;
    }

  rv = pthread_mutex_lock (&hb_Resource->lock);
  if (hb_Resource->shutdown)
    {
      /* resources have already been cleaned up */
      pthread_mutex_unlock (&hb_Resource->lock);

      return NO_ERROR;
    }
  hb_Resource->shutdown = true;
  pthread_mutex_unlock (&hb_Resource->lock);

  error = hb_resource_job_queue (HB_RJOB_CLEANUP_ALL, NULL, HB_JOB_TIMER_IMMEDIATELY);
  if (error != NO_ERROR)
    {
      assert (false);
    }
  else
    {
      snprintf (error_string, LINE_MAX, "CUBRID heartbeat starts to shutdown all HA processes.");
      MASTER_ER_SET (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_HB_COMMAND_EXECUTION, 2, HB_CMD_DEACTIVATE_STR,
		     error_string);
    }

  return error;
}

/*
 * hb_deactivate_heartbeat -
 *   return:
 *
 */
int
hb_deactivate_heartbeat (void)
{
  char error_string[LINE_MAX] = "";

  if (hb_Cluster == NULL)
    {
      return ER_FAILED;
    }

  if (!hb_Is_activated)
    {
      snprintf (error_string, LINE_MAX, "%s. (CUBRID heartbeat feature already deactivated)", HB_RESULT_FAILURE_STR);
      MASTER_ER_SET (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HB_COMMAND_EXECUTION, 2, HB_CMD_DEACTIVATE_STR, error_string);

      return NO_ERROR;
    }

  if (hb_Resource != NULL && resource_Jobs != NULL)
    {
      hb_resource_shutdown_and_cleanup ();
    }

  if (hb_Cluster != NULL && cluster_Jobs != NULL)
    {
      hb_cluster_shutdown_and_cleanup ();
    }

  hb_Is_activated = false;

  snprintf (error_string, LINE_MAX, "%s.", HB_RESULT_SUCCESS_STR);
  MASTER_ER_SET (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HB_COMMAND_EXECUTION, 2, HB_CMD_DEACTIVATE_STR, error_string);

  return NO_ERROR;
}

/*
 * hb_activate_heartbeat -
 *   return: none
 *
 *   str(out):
 */
int
hb_activate_heartbeat (void)
{
  int error;
  char error_string[LINE_MAX] = "";

  if (hb_Cluster == NULL)
    {
      return ER_FAILED;
    }

  /* unfinished job of deactivation exists */
  if (hb_Deactivate_info.info_started)
    {
      snprintf (error_string, LINE_MAX, "%s. (CUBRID heartbeat feature is being deactivated)", HB_RESULT_FAILURE_STR);
      MASTER_ER_SET (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HB_COMMAND_EXECUTION, 2, HB_CMD_ACTIVATE_STR, error_string);
      return ER_FAILED;
    }

  if (hb_Is_activated)
    {
      snprintf (error_string, LINE_MAX, "%s. (CUBRID heartbeat feature already activated)", HB_RESULT_FAILURE_STR);
      MASTER_ER_SET (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HB_COMMAND_EXECUTION, 2, HB_CMD_ACTIVATE_STR, error_string);
      return NO_ERROR;
    }

  error = hb_master_init ();
  if (error != NO_ERROR)
    {
      snprintf (error_string, LINE_MAX, "%s. (failed to initialize CUBRID heartbeat feature)", HB_RESULT_FAILURE_STR);
      MASTER_ER_SET (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HB_COMMAND_EXECUTION, 2, HB_CMD_ACTIVATE_STR, error_string);

      return ER_FAILED;
    }

  hb_Is_activated = true;

  snprintf (error_string, LINE_MAX, "%s.", HB_RESULT_SUCCESS_STR);
  MASTER_ER_SET (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HB_COMMAND_EXECUTION, 2, HB_CMD_ACTIVATE_STR, error_string);

  return NO_ERROR;
}

/*
 * hb_start_util_process -
 *   return: none
 *
 *   args(in):
 *   str(out):
 */
int
hb_start_util_process (char *args)
{
  char error_string[LINE_MAX] = "";
  HB_PROC_ENTRY *proc;
  int pid;

  char executable_path[PATH_MAX];
  int i, num_args = 0;
  char *s, *save;
  char argvs[HB_MAX_NUM_PROC_ARGV][HB_MAX_SZ_PROC_ARGV];
  char *argvp[HB_MAX_NUM_PROC_ARGV];

  if (hb_Resource == NULL)
    {
      return ER_FAILED;
    }

  (void) pthread_mutex_lock (&hb_Resource->lock);
  proc = hb_return_proc_by_args (args);
  if (proc != NULL)
    {
      (void) pthread_mutex_unlock (&hb_Resource->lock);

      snprintf (error_string, LINE_MAX, "%s. (process already running, args:%s)", HB_RESULT_FAILURE_STR, args);
      MASTER_ER_SET (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_HB_COMMAND_EXECUTION, 2, HB_CMD_UTIL_START_STR,
		     error_string);
      return NO_ERROR;
    }

  pid = fork ();
  if (pid < 0)
    {
      (void) pthread_mutex_unlock (&hb_Resource->lock);

      MASTER_ER_SET (ER_ERROR_SEVERITY, ARG_FILE_LINE, ERR_CSS_CANNOT_FORK, 0);
      return ER_FAILED;
    }
  else if (pid == 0)
    {
      memset (argvp, 0, sizeof (argvp));
      memset (argvs, 0, sizeof (argvs));
      s = strtok_r (args, " \t\n", &save);
      while (s)
	{
	  strncpy (argvs[num_args++], s, HB_MAX_SZ_PROC_ARGV - 1);
	  s = strtok_r (NULL, " \t\n", &save);
	}

      for (i = 0; i < num_args; i++)
	{
	  argvp[i] = argvs[i];
	}

      envvar_bindir_file (executable_path, PATH_MAX, argvp[0]);
      (void) execv (executable_path, argvp);

      (void) pthread_mutex_unlock (&hb_Resource->lock);
      css_master_cleanup (SIGTERM);
      return NO_ERROR;
    }
  else
    {
      (void) pthread_mutex_unlock (&hb_Resource->lock);
    }

  return NO_ERROR;
}

/*
 * common
 */

void
hb_enable_er_log (void)
{
  int rv;

  rv = pthread_mutex_lock (&css_Master_er_log_enable_lock);

  css_Master_er_log_enabled = true;
  hb_Nolog_event_msg[0] = '\0';

  pthread_mutex_unlock (&css_Master_er_log_enable_lock);
}

void
hb_disable_er_log (int reason, const char *msg_fmt, ...)
{
  va_list args;
  char *p, *last;
  const char *event_name;
  char time_str[64];
  struct timeval curr_time;
  int rv;

  rv = pthread_mutex_lock (&css_Master_er_log_enable_lock);

  if (!css_Master_er_log_enabled)
    {
      pthread_mutex_unlock (&css_Master_er_log_enable_lock);
      return;
    }

  if (reason == HB_NOLOG_DEMOTE_ON_DISK_FAIL)
    {
      event_name = "DEMOTE ON DISK FAILURE";
    }
  else if (reason == HB_NOLOG_REMOTE_STOP)
    {
      event_name = "REMOTE STOP";
    }
  else
    {
      pthread_mutex_unlock (&css_Master_er_log_enable_lock);
      return;
    }

  css_Master_er_log_enabled = false;

  p = hb_Nolog_event_msg;
  last = hb_Nolog_event_msg + sizeof (hb_Nolog_event_msg);

  gettimeofday (&curr_time, NULL);

  p += snprintf (p, MAX ((last - p), 0), "[%s][%s]", hb_strtime (time_str, sizeof (time_str), &curr_time), event_name);

  if (msg_fmt != NULL)
    {
      va_start (args, msg_fmt);
      vsnprintf (p, MAX ((last - p), 0), msg_fmt, args);
      va_end (args);
    }

  pthread_mutex_unlock (&css_Master_er_log_enable_lock);
}

/*
 * hb_check_ping -
 *   return : int
 *
 */
int
hb_check_ping (const char *host)
{
#define PING_COMMAND_FORMAT \
"ping -w 1 -c 1 %s >/dev/null 2>&1; " \
"echo $?"

  char ping_command[256], result_str[16];
  char buf[128];
  char *end_p;
  int result = 0;
  int ping_result;
  FILE *fp;

  /* If host_p is in the cluster node, then skip to check */
  for (cubhb::node_entry *node : hb_Cluster->nodes)
    {
      if (strcmp (host, node->get_hostname_cstr ()) == 0)
	{
	  /* PING Host is same as cluster's host name */
	  snprintf (buf, sizeof (buf), "Useless PING host name %s", host);
	  MASTER_ER_SET (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HB_NODE_EVENT, 1, buf);
	  return cubhb::ping_host::USELESS_HOST;
	}
    }

  snprintf (ping_command, sizeof (ping_command), PING_COMMAND_FORMAT, host);
  fp = popen (ping_command, "r");
  if (fp == NULL)
    {
      /* ping open fail */
      MASTER_ER_SET (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HB_NODE_EVENT, 1, "PING command fork failed");
      return cubhb::ping_host::SYS_ERR;
    }

  if (fgets (result_str, sizeof (result_str), fp) == NULL)
    {
      pclose (fp);
      MASTER_ER_SET (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HB_NODE_EVENT, 1, "Can't get PING result");
      return cubhb::ping_host::SYS_ERR;
    }

  result_str[sizeof (result_str) - 1] = 0;

  pclose (fp);

  result = str_to_int32 (&ping_result, &end_p, result_str, 10);
  if (result != 0 || ping_result != NO_ERROR)
    {
      /* ping failed */
      snprintf (buf, sizeof (buf), "PING failed for host %s", host);
      MASTER_ER_SET (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HB_NODE_EVENT, 1, buf);

      return cubhb::ping_host::FAILURE;
    }

  return cubhb::ping_host::SUCCESS;
}

static int
hb_help_sprint_ping_host_info (char *buffer, int max_length)
{
  char *p, *last;

  if (*buffer != '\0')
    {
      memset (buffer, 0, max_length);
    }

  p = buffer;
  last = buffer + max_length;

  p += snprintf (p, MAX ((last - p), 0), "HA Ping Host Info\n");
  p += snprintf (p, MAX ((last - p), 0),
		 "==============================" "==================================================\n");

  p += snprintf (p, MAX ((last - p), 0), " * PING check is %s\n",
		 hb_Cluster->is_ping_check_enabled ? "enabled" : "disabled");
  p += snprintf (p, MAX ((last - p), 0),
		 "------------------------------" "--------------------------------------------------\n");
  p += snprintf (p, MAX ((last - p), 0), "%-20s %-20s\n", "hostname", "PING check result");
  p += snprintf (p, MAX ((last - p), 0),
		 "------------------------------" "--------------------------------------------------\n");
  for (cubhb::ping_host *host : hb_Cluster->ping_hosts)
    {
      p += snprintf (p, MAX ((last - p), 0), "%-20s %-20s\n", host->get_hostname_cstr (),
		     hb_ping_result_string (host->ping_result));
    }
  p += snprintf (p, MAX ((last - p), 0),
		 "==============================" "==================================================\n");

  return p - buffer;
}

static int
hb_help_sprint_nodes_info (char *buffer, int max_length)
{
  char *p, *last;

  if (*buffer != '\0')
    {
      memset (buffer, 0, max_length);
    }

  p = buffer;
  last = buffer + max_length;

  p += snprintf (p, MAX ((last - p), 0), "HA Node Info\n");
  p += snprintf (p, MAX ((last - p), 0),
		 "==============================" "==================================================\n");
  p += snprintf (p, MAX ((last - p), 0), " * group_id : %s   host_name : %s   state : %s \n",
		 hb_Cluster->group_id.c_str (), hb_Cluster->hostname.c_str (),
		 hb_node_state_string (hb_Cluster->state));
  p += snprintf (p, MAX ((last - p), 0),
		 "------------------------------" "--------------------------------------------------\n");
  p += snprintf (p, MAX ((last - p), 0), "%-20s %-10s %-15s %-10s %-20s\n", "name", "priority", "state", "score",
		 "missed heartbeat");
  p += snprintf (p, MAX ((last - p), 0),
		 "------------------------------" "--------------------------------------------------\n");

  for (cubhb::node_entry *node : hb_Cluster->nodes)
    {
      p += snprintf (p, MAX ((last - p), 0), "%-20s %-10u %-15s %-10d %-20d\n", node->get_hostname_cstr (),
		     node->priority, hb_node_state_string (node->state), node->score, node->heartbeat_gap);
    }

  p += snprintf (p, MAX ((last - p), 0),
		 "==============================" "==================================================\n");
  p += snprintf (p, MAX ((last - p), 0), "\n");

  return p - buffer;
}

static int
hb_help_sprint_processes_info (char *buffer, int max_length)
{
  HB_PROC_ENTRY *proc;
  char *p, *last;

  if (*buffer != '\0')
    {
      memset (buffer, 0, max_length);
    }

  p = buffer;
  last = p + max_length;

  p += snprintf (p, MAX ((last - p), 0), "HA Process Info\n");

  p += snprintf (p, MAX ((last - p), 0),
		 "==============================" "==================================================\n");
  p += snprintf (p, MAX ((last - p), 0), " * state : %s \n", hb_node_state_string (hb_Cluster->state));
  p += snprintf (p, MAX ((last - p), 0),
		 "------------------------------" "--------------------------------------------------\n");
  p += snprintf (p, MAX ((last - p), 0), "%-10s %-22s %-15s %-10s\n", "pid", "state", "type", "socket fd");
  p += snprintf (p, MAX ((last - p), 0), "     %-30s %-35s\n", "exec-path", "args");
  p += snprintf (p, MAX ((last - p), 0),
		 "------------------------------" "--------------------------------------------------\n");

  for (proc = hb_Resource->procs; proc; proc = proc->next)
    {
      if (proc->state == HB_PSTATE_UNKNOWN)
	{
	  continue;
	}

      p += snprintf (p, MAX ((last - p), 0), "%-10d %-22s %-15s %-10d\n", proc->pid,
		     hb_process_state_string (proc->type, proc->state), hb_process_type_string (proc->type), proc->sfd);
      p += snprintf (p, MAX ((last - p), 0), "      %-30s %-35s\n", proc->exec_path, proc->args);
    }

  p += snprintf (p, MAX ((last - p), 0),
		 "==============================" "==================================================\n");
  p += snprintf (p, MAX ((last - p), 0), "\n");

  return p - buffer;
}

static int
hb_help_sprint_jobs_info (HB_JOB *jobs, char *buffer, int max_length)
{
  int rv;
  HB_JOB_ENTRY *job;
  char *p, *last;

  p = (char *) &buffer[0];
  last = p + sizeof (buffer);

  p += snprintf (p, MAX ((last - p), 0), "HA Job Info\n");
  p += snprintf (p, MAX ((last - p), 0),
		 "==============================" "==================================================\n");
  p += snprintf (p, MAX ((last - p), 0), "%-10s %-20s %-20s %-20s\n", "type", "func", "arg", "expire");
  p += snprintf (p, MAX ((last - p), 0),
		 "------------------------------" "--------------------------------------------------\n");

  rv = pthread_mutex_lock (&jobs->lock);
  for (job = jobs->jobs; job; job = job->next)
    {
      p += snprintf (p, MAX ((last - p), 0), "%-10d %-20p %-20p %-10d.%-10d\n", job->type, (void *) job->func,
		     (void *) job->arg, (unsigned int) job->expire.tv_sec, (unsigned int) job->expire.tv_usec);
    }

  pthread_mutex_unlock (&jobs->lock);

  p += snprintf (p, MAX ((last - p), 0),
		 "==============================" "==================================================\n");
  p += snprintf (p, MAX ((last - p), 0), "\n");

  return p - buffer;
}

int
hb_check_request_eligibility (SOCKET sd)
{
  int rv, error, result;
  struct sockaddr_in req_addr;
  struct in_addr node_addr;
  socklen_t req_addr_len;

  req_addr_len = sizeof (req_addr);

  if (getpeername (sd, (struct sockaddr *) &req_addr, &req_addr_len) < 0)
    {
      return HB_HC_FAILED;
    }

  /* from localhost */
  if (req_addr.sin_family == AF_UNIX)
    {
      return HB_HC_ELIGIBLE_LOCAL;
    }

  rv = pthread_mutex_lock (&hb_Cluster->lock);

  result = HB_HC_UNAUTHORIZED;
  for (cubhb::node_entry *node : hb_Cluster->nodes)
    {
      error = hb_hostname_to_sin_addr (node->get_hostname_cstr (), &node_addr);
      if (error != NO_ERROR)
	{
	  MASTER_ER_LOG_DEBUG (ARG_FILE_LINE, "Failed to resolve IP address of %s", node->get_hostname_cstr ());
	  result = HB_HC_FAILED;
	  continue;
	}

      if (memcmp (&req_addr.sin_addr, &node_addr, sizeof (struct in_addr)) == 0)
	{
	  pthread_mutex_unlock (&hb_Cluster->lock);
	  return HB_HC_ELIGIBLE_REMOTE;
	}
    }
  pthread_mutex_unlock (&hb_Cluster->lock);

  return result;
}

/*
 * hb_start_deactivate_server_info -
 *     Initialize hb_Server_deactivate_info,
 *     and set info_started flag to true.
 *
 *   return: none
 */
void
hb_start_deactivate_server_info (void)
{
  assert (!hb_Deactivate_info.info_started);

  if (hb_Deactivate_info.server_pid_list != NULL)
    {
      free_and_init (hb_Deactivate_info.server_pid_list);
    }

  hb_Deactivate_info.server_count = 0;
  hb_Deactivate_info.info_started = true;
}

bool
hb_is_deactivation_started (void)
{
  return hb_Deactivate_info.info_started;
}

bool
hb_is_deactivation_ready (void)
{
  HB_PROC_ENTRY *proc;

  pthread_mutex_lock (&hb_Resource->lock);
  for (proc = hb_Resource->procs; proc; proc = proc->next)
    {
      if (proc->conn != NULL)
	{
	  pthread_mutex_unlock (&hb_Resource->lock);
	  return false;
	}
      assert (proc->sfd == INVALID_SOCKET);
    }
  pthread_mutex_unlock (&hb_Resource->lock);

  return true;
}

/*
 * hb_get_deactivating_server_count -
 *
 *   return: none
 */
int
hb_get_deactivating_server_count (void)
{
  int i, num_active_server = 0;

  if (hb_Deactivate_info.info_started)
    {
      for (i = 0; i < hb_Deactivate_info.server_count; i++)
	{
	  if (hb_Deactivate_info.server_pid_list[i] > 0)
	    {
	      if (kill (hb_Deactivate_info.server_pid_list[i], 0) && errno == ESRCH)
		{
		  /* server was terminated */
		  hb_Deactivate_info.server_pid_list[i] = 0;
		}
	      else
		{
		  num_active_server++;
		}
	    }
	}

      return num_active_server;
    }

  return 0;
}

/*
 * hb_finish_deactivate_server_info -
 *     clear hb_Server_deactivate_info.
 *     and set info_started flag to false.
 *
 *   return: none
 */
void
hb_finish_deactivate_server_info (void)
{
  if (hb_Deactivate_info.server_pid_list != NULL)
    {
      free_and_init (hb_Deactivate_info.server_pid_list);
    }

  hb_Deactivate_info.server_count = 0;
  hb_Deactivate_info.info_started = false;
}

/*
 * hb_return_proc_state_by_fd() -
 *   return: process state
 *
 *   sfd(in):
 */
int
hb_return_proc_state_by_fd (int sfd)
{
  int rv;
  int state = 0;
  HB_PROC_ENTRY *proc;

  if (hb_Resource == NULL)
    {
      return HB_PSTATE_UNKNOWN;
    }

  rv = pthread_mutex_lock (&hb_Resource->lock);
  proc = hb_return_proc_by_fd (sfd);
  if (proc == NULL)
    {
      pthread_mutex_unlock (&hb_Resource->lock);
      return HB_PSTATE_DEAD;
    }

  state = (int) proc->state;

  if (proc->server_hang)
    {
      state = HB_PSTATE_DEAD;
    }
  pthread_mutex_unlock (&hb_Resource->lock);

  return state;
}

/*
 * hb_is_hang_process() -
 *   return:
 *
 *   sfd(in):
 */
bool
hb_is_hang_process (int sfd)
{
  int rv;
  HB_PROC_ENTRY *proc;

  if (hb_Resource == NULL)
    {
      return false;
    }

  rv = pthread_mutex_lock (&hb_Resource->lock);
  proc = hb_return_proc_by_fd (sfd);
  if (proc == NULL)
    {
      pthread_mutex_unlock (&hb_Resource->lock);
      return false;
    }

  if (proc->server_hang)
    {
      pthread_mutex_unlock (&hb_Resource->lock);
      return true;
    }
  pthread_mutex_unlock (&hb_Resource->lock);

  return false;
}

const char *
hb_find_host_name_of_master_server ()
{
  int rv = pthread_mutex_lock (&hb_Cluster->lock);
  for (cubhb::node_entry *node : hb_Cluster->nodes)
    {
      if (node->state == cubhb::node_entry::MASTER && hb_Cluster->master == node)
	{
	  assert (node->get_hostname () == hb_Cluster->master->get_hostname ());
	  pthread_mutex_unlock (&hb_Cluster->lock);
	  return node->get_hostname_cstr ();
	}
    }
  pthread_mutex_unlock (&hb_Cluster->lock);

  return NULL;
}
