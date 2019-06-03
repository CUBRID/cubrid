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
 * master_heartbeat.hpp - heartbeat module in cub_master
 */

#ifndef _MASTER_HEARTBEAT_HPP_
#define _MASTER_HEARTBEAT_HPP_

#include "heartbeat.h"
#include "heartbeat_cluster.hpp"
#include "log_lsa.hpp"
#include "master_util.h"
#include "porting.h"
#include "system_parameter.h"

#if defined (LINUX)
#include <netinet/in.h>
#elif defined (WINDOWS)
#include <winsock2.h>
#endif

#if defined(WINDOWS)
typedef int pid_t;
#endif

#define HB_PING_UNKNOWN_STR          "UNKNOWN"
#define HB_PING_SUCCESS_STR          "SUCCESS"
#define HB_PING_USELESS_HOST_STR     "SKIPPED"
#define HB_PING_SYS_ERR_STR          "ERROR"
#define HB_PING_FAILURE_STR          "FAILURE"
#define HB_PING_STR_SIZE             (7)

/* heartbeat cluster jobs */
enum HB_CLUSTER_JOB
{
  HB_CJOB_INIT = 0,
  HB_CJOB_HEARTBEAT = 1,
  HB_CJOB_CALC_SCORE = 2,
  HB_CJOB_CHECK_PING = 3,
  HB_CJOB_FAILOVER = 4,
  HB_CJOB_FAILBACK = 5,
  HB_CJOB_CHECK_VALID_PING_SERVER = 6,
  HB_CJOB_DEMOTE = 7,
  HB_CJOB_MAX
};

/* heartbeat resource jobs */
enum HB_RESOURCE_JOB
{
  HB_RJOB_PROC_START = 0,
  HB_RJOB_PROC_DEREG = 1,
  HB_RJOB_CONFIRM_START = 2,
  HB_RJOB_CONFIRM_DEREG = 3,
  HB_RJOB_CHANGE_MODE = 4,
  HB_RJOB_DEMOTE_START_SHUTDOWN = 5,
  HB_RJOB_DEMOTE_CONFIRM_SHUTDOWN = 6,
  HB_RJOB_CLEANUP_ALL = 7,
  HB_RJOB_CONFIRM_CLEANUP_ALL = 8,
  HB_RJOB_SEND_MASTER_HOSTNAME = 9,
  HB_RJOB_MAX
};

/*  heartbet resource process state
 *  When change this, must be change the SERVER_STATE.
 *  broker.c : enum SERVER_STATE */
enum HB_PROC_STATE
{
  HB_PSTATE_UNKNOWN = 0,
  HB_PSTATE_DEAD = 1,
  HB_PSTATE_DEREGISTERED = 2,
  HB_PSTATE_STARTED = 3,
  HB_PSTATE_NOT_REGISTERED = 4,
  HB_PSTATE_REGISTERED = 5,
  HB_PSTATE_REGISTERED_AND_STANDBY = HB_PSTATE_REGISTERED,
  HB_PSTATE_REGISTERED_AND_TO_BE_STANDBY = 6,
  HB_PSTATE_REGISTERED_AND_ACTIVE = 7,
  HB_PSTATE_REGISTERED_AND_TO_BE_ACTIVE = 8,
  HB_PSTATE_MAX
};

#define HB_NSTATE_UNKNOWN_STR       "unknown"
#define HB_NSTATE_SLAVE_STR         "slave"
#define HB_NSTATE_TO_BE_MASTER_STR  "to-be-master"
#define HB_NSTATE_TO_BE_SLAVE_STR   "to-be-slave"
#define HB_NSTATE_MASTER_STR        "master"
#define HB_NSTATE_REPLICA_STR       "replica"
#define HB_NSTATE_STR_SZ            (32)

#define HB_PSTATE_UNKNOWN_STR                       "unknown"
#define HB_PSTATE_DEAD_STR                          "dead"
#define HB_PSTATE_DEREGISTERED_STR                  "deregistered"
#define HB_PSTATE_STARTED_STR                       "started"
#define HB_PSTATE_NOT_REGISTERED_STR                "not_registered"
#define HB_PSTATE_REGISTERED_STR                    "registered"
#define HB_PSTATE_REGISTERED_AND_STANDBY_STR        "registered_and_standby"
#define HB_PSTATE_REGISTERED_AND_TO_BE_STANDBY_STR  "registered_and_to_be_standby"
#define HB_PSTATE_REGISTERED_AND_ACTIVE_STR         "registered_and_active"
#define HB_PSTATE_REGISTERED_AND_TO_BE_ACTIVE_STR   "registered_and_to_be_active"
#define HB_PSTATE_STR_SZ                             (32)

/* heartbeat node score bitmask */
#define HB_NODE_SCORE_MASTER                    0x8000
#define HB_NODE_SCORE_TO_BE_MASTER              0xF000
#define HB_NODE_SCORE_SLAVE                     0x0000
#define HB_NODE_SCORE_UNKNOWN                   0x7FFF

#define HB_BUFFER_SZ                            (4096)
#define HB_MAX_NUM_NODES                        (8)
#define HB_MAX_NUM_RESOURCE_PROC                (16)
#define HB_MAX_PING_CHECK                       (3)
#define HB_MAX_WAIT_FOR_NEW_MASTER              (60)
#define HB_MAX_CHANGEMODE_DIFF_TO_TERM          (12)
#define HB_MAX_CHANGEMODE_DIFF_TO_KILL          (24)

/* various strings for er_set */
#define HB_RESULT_SUCCESS_STR                   "Success"
#define HB_RESULT_FAILURE_STR                   "Failure"

#define HB_CMD_ACTIVATE_STR                     "activate"
#define HB_CMD_DEACTIVATE_STR                   "deactivate"
#define HB_CMD_DEREGISTER_STR                   "deregister"
#define HB_CMD_RELOAD_STR                       "reload"
#define HB_CMD_UTIL_START_STR                   "util process start"

enum HB_HOST_CHECK_RESULT
{
  HB_HC_ELIGIBLE_LOCAL,
  HB_HC_ELIGIBLE_REMOTE,
  HB_HC_UNAUTHORIZED,
  HB_HC_FAILED
};

enum HB_NOLOG_REASON
{
  HB_NOLOG_DEMOTE_ON_DISK_FAIL,
  HB_NOLOG_REMOTE_STOP,
  HB_NOLOG_MAX = HB_NOLOG_REMOTE_STOP
};

#define HB_VALID_NO_ERROR_STR			"no_error"
#define HB_VALID_UNIDENTIFIED_NODE_STR		"unidentified_node"
#define HB_VALID_GROUP_NAME_MISMATCH_STR	"group_name_mismatch"
#define HB_VALID_IP_ADDR_MISMATCH_STR		"ip_addr_mismatch"
#define HB_VALID_CANNOT_RESOLVE_HOST_STR	"cannot_resolve_host_name"

/* time related macro */
#define HB_GET_ELAPSED_TIME(end_time, start_time) \
            ((double)(end_time.tv_sec - start_time.tv_sec) * 1000 + \
             (end_time.tv_usec - start_time.tv_usec)/1000.0)

#define HB_PROC_RECOVERY_DELAY_TIME		(30* 1000)	/* milli-second */

#define HB_IPV4_STR_LEN				(16)

/* heartbeat list */
typedef struct hb_list HB_LIST;
struct hb_list
{
  HB_LIST *next;
  HB_LIST **prev;
};

/* heartbeat processs entries */
struct HB_PROC_ENTRY
{
  HB_PROC_ENTRY *next;
  HB_PROC_ENTRY **prev;

  unsigned char state;		/* process state */
  unsigned char type;		/* single/master-slave */

  int sfd;

  int pid;
  char exec_path[HB_MAX_SZ_PROC_EXEC_PATH];
  char args[HB_MAX_SZ_PROC_ARGS];
  char argv[HB_MAX_NUM_PROC_ARGV][HB_MAX_SZ_PROC_ARGV];

  struct timeval frtime;	/* first registered time */
  struct timeval rtime;		/* registerd time */
  struct timeval dtime;		/* deregistered time */
  struct timeval ktime;		/* shutdown time */
  struct timeval stime;		/* start time */

  unsigned short changemode_rid;
  unsigned short changemode_gap;

  LOG_LSA prev_eof;
  LOG_LSA curr_eof;

  CSS_CONN_ENTRY *conn;

  bool being_shutdown;		/* whether the proc is being shut down */
  bool server_hang;
  bool knows_master_hostname;
};

/* heartbeat resources */
typedef struct hb_resource HB_RESOURCE;
struct hb_resource
{
  pthread_mutex_t lock;

  cubhb::node_state state;	/* mode/state */

  int num_procs;
  HB_PROC_ENTRY *procs;

  bool shutdown;
};

/* heartbeat cluster job argument */
typedef struct hb_cluster_job_arg HB_CLUSTER_JOB_ARG;
struct hb_cluster_job_arg
{
  unsigned int ping_check_count;
  unsigned int retries;		/* job retries */
};

/* heartbeat resource job argument */
typedef struct hb_resource_job_arg HB_RESOURCE_JOB_ARG;
struct hb_resource_job_arg
{
  int pid;			/* process id */
  int sfd;			/* socket fd */

  char args[HB_MAX_SZ_PROC_ARGS];	/* args */

  unsigned int retries;		/* job retries */
  unsigned int max_retries;	/* job max retries */

  struct timeval ftime;		/* first job execution time */
  struct timeval ltime;		/* last job execution time */
};

/* heartbeat job argument */
typedef union hb_job_arg HB_JOB_ARG;
union hb_job_arg
{
  HB_CLUSTER_JOB_ARG cluster_job_arg;
  HB_RESOURCE_JOB_ARG resource_job_arg;
};

typedef void (*HB_JOB_FUNC) (HB_JOB_ARG *);

/* timer job queue entries */
typedef struct hb_job_entry HB_JOB_ENTRY;
struct hb_job_entry
{
  HB_JOB_ENTRY *next;
  HB_JOB_ENTRY **prev;

  unsigned int type;

  struct timeval expire;

  HB_JOB_FUNC func;
  HB_JOB_ARG *arg;
};

/* timer job queue */
typedef struct hb_job HB_JOB;
struct hb_job
{
  pthread_mutex_t lock;

  unsigned short num_jobs;
  HB_JOB_ENTRY *jobs;

  HB_JOB_FUNC *job_funcs;

  bool shutdown;
};

extern cubhb::cluster *hb_Cluster;
extern HB_RESOURCE *hb_Resource;
extern HB_JOB *cluster_Jobs;
extern HB_JOB *resource_Jobs;

extern bool hb_Deactivate_immediately;

int hb_master_init (void);
void hb_resource_shutdown_and_cleanup (void);
void hb_cluster_shutdown_and_cleanup (void);

void hb_cleanup_conn_and_start_process (CSS_CONN_ENTRY *conn, SOCKET sfd);

void hb_get_node_info_string (char **str, bool verbose_yn);
void hb_get_process_info_string (char **str, bool verbose_yn);
void hb_get_ping_host_info_string (char **str);
void hb_get_admin_info_string (char **str);
#if defined (ENABLE_OLD_REPLICATION)
void hb_kill_all_heartbeat_process (char **str);
#endif

void hb_deregister_by_pid (pid_t pid);
void hb_deregister_by_args (char *args);

void hb_reconfig_heartbeat (char **str);
int hb_prepare_deactivate_heartbeat (void);
int hb_deactivate_heartbeat (void);
int hb_activate_heartbeat (void);

bool hb_is_registered_process (CSS_CONN_ENTRY *conn, char *args);
void hb_register_new_process (CSS_CONN_ENTRY *conn);
void hb_resource_receive_changemode (CSS_CONN_ENTRY *conn);
void hb_resource_receive_get_eof (CSS_CONN_ENTRY *conn);

int hb_check_request_eligibility (SOCKET sd);
void hb_start_deactivate_server_info (void);
int hb_get_deactivating_server_count (void);
bool hb_is_deactivation_started (void);
bool hb_is_deactivation_ready (void);
void hb_finish_deactivate_server_info (void);

int hb_start_util_process (char *args);

void hb_enable_er_log (void);
void hb_disable_er_log (int reason, const char *msg_fmt, ...);

int hb_return_proc_state_by_fd (int sfd);
bool hb_is_hang_process (int sfd);
const cubbase::hostname_type& hb_find_host_name_of_master_server ();

cubhb::ping_host::ping_result hb_check_ping (const char *host);
const char *hb_valid_result_string (cubhb::ui_node_result v_result);

/* cluster jobs queue */
int hb_cluster_job_set_expire_and_reorder (unsigned int job_type, unsigned int msec);
#endif /* _MASTER_HEARTBEAT_HPP_ */
