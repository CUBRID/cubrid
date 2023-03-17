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
 * master_heartbeat.h - heartbeat module in cub_master
 */

#ifndef _MASTER_HEARTBEAT_H_
#define _MASTER_HEARTBEAT_H_

#ident "$Id$"

#include "heartbeat.h"
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

/* ping result */
enum HB_PING_RESULT
{
  HB_PING_UNKNOWN = -1,
  HB_PING_SUCCESS = 0,
  HB_PING_USELESS_HOST = 1,
  HB_PING_SYS_ERR = 2,
  HB_PING_FAILURE = 3
};

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
#define HB_PSTATE_UNKNOWN_STR                   "unknown"
#define HB_PSTATE_DEAD_STR                      "dead"
#define HB_PSTATE_DEREGISTERED_STR              "deregistered"
#define HB_PSTATE_STARTED_STR                   "started"
#define HB_PSTATE_NOT_REGISTERED_STR            "not_registered"
#define HB_PSTATE_REGISTERED_STR                "registered"
#define HB_PSTATE_REGISTERED_AND_STANDBY_STR		"registered_and_standby"
#define HB_PSTATE_REGISTERED_AND_TO_BE_STANDBY_STR	"registered_and_to_be_standby"
#define HB_PSTATE_REGISTERED_AND_ACTIVE_STR			"registered_and_active"
#define HB_PSTATE_REGISTERED_AND_TO_BE_ACTIVE_STR	"registered_and_to_be_active"
#define HB_PSTATE_STR_SZ                        (32)

#define HB_REPLICA_PRIORITY                     0x7FFF

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
#define HB_MAX_CHANGEMODE_DIFF_TO_TERM		(12)
#define HB_MAX_CHANGEMODE_DIFF_TO_KILL		(24)

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

/* heartbeat validation result */
enum HB_VALID_RESULT
{
  HB_VALID_NO_ERROR = 0,
  HB_VALID_UNIDENTIFIED_NODE = 1,
  HB_VALID_GROUP_NAME_MISMATCH = 2,
  HB_VALID_IP_ADDR_MISMATCH = 3,
  HB_VALID_CANNOT_RESOLVE_HOST = 4
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

#define HB_IS_INITIALIZED_TIME(arg_time) \
            ((arg_time.tv_sec == 0 && arg_time.tv_usec == 0) ? 1 : 0)

#define HB_PROC_RECOVERY_DELAY_TIME		(30* 1000)	/* milli-second */

#define HB_UI_NODE_CLEANUP_TIME_IN_MSECS	(3600 * 1000)
#define HB_UI_NODE_CACHE_TIME_IN_MSECS		(60 * 1000)
#define HB_IPV4_STR_LEN				(16)

/* heartbeat list */
typedef struct hb_list HB_LIST;
struct hb_list
{
  HB_LIST *next;
  HB_LIST **prev;
};


/* heartbeat node entries */
typedef struct hb_node_entry HB_NODE_ENTRY;
struct hb_node_entry
{
  HB_NODE_ENTRY *next;
  HB_NODE_ENTRY **prev;

  char host_name[CUB_MAXHOSTNAMELEN];
  unsigned short priority;
  HB_NODE_STATE_TYPE state;
  short score;
  short heartbeat_gap;

  struct timeval last_recv_hbtime;	/* last received heartbeat time */
};

/* heartbeat ping host entries */
typedef struct hb_ping_host_entry HB_PING_HOST_ENTRY;
struct hb_ping_host_entry
{
  HB_PING_HOST_ENTRY *next;
  HB_PING_HOST_ENTRY **prev;

  char host_name[CUB_MAXHOSTNAMELEN];
  int port;
  int ping_result;
};

/* heartbeat unidentifed host entries */
typedef struct hb_ui_node_entry HB_UI_NODE_ENTRY;
struct hb_ui_node_entry
{
  HB_UI_NODE_ENTRY *next;
  HB_UI_NODE_ENTRY **prev;

  char host_name[CUB_MAXHOSTNAMELEN];
  char group_id[HB_MAX_GROUP_ID_LEN];
  struct sockaddr_in saddr;
  struct timeval last_recv_time;
  int v_result;
};

/* herartbeat cluster */
typedef struct hb_cluster HB_CLUSTER;
struct hb_cluster
{
  pthread_mutex_t lock;

  SOCKET sfd;

  HB_NODE_STATE_TYPE state;
  char group_id[HB_MAX_GROUP_ID_LEN];
  char host_name[CUB_MAXHOSTNAMELEN];

  int num_nodes;
  HB_NODE_ENTRY *nodes;

  HB_NODE_ENTRY *myself;
  HB_NODE_ENTRY *master;

  bool shutdown;
  bool hide_to_demote;
  bool is_isolated;
  bool is_ping_check_enabled;

  HB_PING_HOST_ENTRY *ping_hosts;
  int num_ping_hosts;
  int ping_timeout;

  HB_UI_NODE_ENTRY *ui_nodes;
  int num_ui_nodes;
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

  struct timeval frtime;	/* first registered time */
  struct timeval rtime;		/* registerd time */
  struct timeval dtime;		/* deregistered time */
  struct timeval ktime;		/* shutdown time */
  struct timeval stime;		/* start time */

  unsigned short changemode_rid;
  unsigned short changemode_gap;

  LOG_LSA prev_eof;
  LOG_LSA curr_eof;
  bool is_curr_eof_received;

  CSS_CONN_ENTRY *conn;

  bool being_shutdown;		/* whether the proc is being shut down */
  bool server_hang;
};

/* heartbeat resources */
typedef struct hb_resource HB_RESOURCE;
struct hb_resource
{
  pthread_mutex_t lock;

  HB_NODE_STATE_TYPE state;	/* mode/state */

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

extern HB_CLUSTER *hb_Cluster;
extern HB_RESOURCE *hb_Resource;
extern HB_JOB *cluster_Jobs;
extern HB_JOB *resource_Jobs;

extern bool hb_Deactivate_immediately;

extern int hb_master_init (void);
extern void hb_resource_shutdown_and_cleanup (void);
extern void hb_cluster_shutdown_and_cleanup (void);

extern void hb_cleanup_conn_and_start_process (CSS_CONN_ENTRY * conn, SOCKET sfd);

extern void hb_get_node_info_string (char **str, bool verbose_yn);
extern void hb_get_process_info_string (char **str, bool verbose_yn);
extern void hb_get_ping_host_info_string (char **str);
extern void hb_get_tcp_ping_host_info_string (char **str);
extern void hb_get_admin_info_string (char **str);
extern void hb_kill_all_heartbeat_process (char **str);

extern void hb_deregister_by_pid (pid_t pid);
extern void hb_deregister_by_args (char *args);

extern void hb_reconfig_heartbeat (char **str);
extern int hb_prepare_deactivate_heartbeat (void);
extern int hb_deactivate_heartbeat (void);
extern int hb_activate_heartbeat (void);

extern bool hb_is_registered_process (CSS_CONN_ENTRY * conn, char *args);
extern void hb_register_new_process (CSS_CONN_ENTRY * conn);
extern void hb_resource_receive_changemode (CSS_CONN_ENTRY * conn);
extern void hb_resource_receive_get_eof (CSS_CONN_ENTRY * conn);

extern int hb_check_request_eligibility (SOCKET sd);
extern void hb_start_deactivate_server_info (void);
extern int hb_get_deactivating_server_count (void);
extern bool hb_is_deactivation_started (void);
extern bool hb_is_deactivation_ready (void);
extern void hb_finish_deactivate_server_info (void);

extern int hb_start_util_process (char *args);

extern void hb_enable_er_log (void);
extern void hb_disable_er_log (int reason, const char *msg_fmt, ...);

extern int hb_return_proc_state_by_fd (int sfd);
extern bool hb_is_hang_process (int sfd);

#endif /* _MASTER_HEARTBEAT_H_ */
