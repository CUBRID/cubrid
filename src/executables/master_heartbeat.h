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
 * master_heartbeat.h - heartbeat module in cub_master
 */

#ifndef _MASTER_HEARTBEAT_H_
#define _MASTER_HEARTBEAT_H_

#ident "$Id$"


#include "system_parameter.h"
#include "porting.h"
#include "master_util.h"
#include "heartbeat.h"

/* ping result */
enum HB_PING_RESULT
{
  HB_PING_SUCCESS = 0,
  HB_PING_USELESS_HOST = 1,
  HB_PING_SYS_ERR = 2,
  HB_PING_FAILURE = 3
};

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
  HB_CJOB_MAX
};

/* heartbeat node state */
enum HB_NODE_STATE
{
  HB_NSTATE_UNKNOWN = 0,
  HB_NSTATE_SLAVE = 1,
  HB_NSTATE_TO_BE_MASTER = 2,
  HB_NSTATE_TO_BE_SLAVE = 3,
  HB_NSTATE_MASTER = 4,
  HB_NSTATE_REPLICA = 5,
  HB_NSTATE_MAX
};
#define HB_NSTATE_UNKNOWN_STR   "unknown"
#define HB_NSTATE_SLAVE_STR     "slave"
#define HB_NSTATE_TO_BE_MASTER_STR    "to-be-master"
#define HB_NSTATE_TO_BE_SLAVE_STR "to-be-slave"
#define HB_NSTATE_MASTER_STR    "master"
#define HB_NSTATE_REPLICA_STR   "replica"
#define HB_NSTATE_STR_SZ        (8)

/* heartbeat resource jobs */
enum HB_RESOURCE_JOB
{
  HB_RJOB_PROC_START = 0,
  HB_RJOB_PROC_DEREG = 1,
  HB_RJOB_CONFIRM_START = 2,
  HB_RJOB_CONFIRM_DEREG = 3,
  HB_RJOB_CHANGE_MODE = 4,
  HB_RJOB_MAX
};

/* heartbet resource process state */
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
#define HB_MAX_CHANGEMODE_DIFF_TO_TERM		(12)
#define HB_MAX_CHANGEMODE_DIFF_TO_KILL		(24)

/* various strings for er_set */
#define HB_RESULT_SUCCESS_STR                   "Success"
#define HB_RESULT_FAILURE_STR                   "Failure"

#define HB_CMD_ACTIVATE_STR                     "activate"
#define HB_CMD_DEACTIVATE_STR                   "deactivate"
#define HB_CMD_DEREGISTER_STR                   "deregister"
#define HB_CMD_RELOAD_STR                       "reload"

/* time related macro */
#define HB_GET_ELAPSED_TIME(end_time, start_time) \
            ((double)(end_time.tv_sec - start_time.tv_sec) * 1000 + \
             (end_time.tv_usec - start_time.tv_usec)/1000.0)

#define HB_IS_INITIALIZED_TIME(arg_time) \
            ((arg_time.tv_sec == 0 && arg_time.tv_usec == 0) ? 1 : 0)

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

  char host_name[MAXHOSTNAMELEN];
  unsigned short priority;
  unsigned short state;
  short score;
  short heartbeat_gap;

  struct timeval last_recv_hbtime;	/* last received heartbeat time */
};

/* herartbeat cluster */
typedef struct hb_cluster HB_CLUSTER;
struct hb_cluster
{
  pthread_mutex_t lock;

  SOCKET sfd;

  unsigned int state;
  char group_id[HB_MAX_GROUP_ID_LEN];
  char host_name[MAXHOSTNAMELEN];

  int num_nodes;
  HB_NODE_ENTRY *nodes;

  HB_NODE_ENTRY *myself;
  HB_NODE_ENTRY *master;

  bool shutdown;
};

/* heartbeat processs entries */
typedef struct hb_proc_entry HB_PROC_ENTRY;
struct hb_proc_entry
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

  struct timeval rtime;		/* registerd time */
  struct timeval dtime;		/* deregistered time */
  struct timeval ktime;		/* shutdown time */
  struct timeval stime;		/* start time */

  unsigned short changemode_rid;
  unsigned short changemode_gap;

  CSS_CONN_ENTRY *conn;
};

/* heartbeat resources */
typedef struct hb_resource HB_RESOURCE;
struct hb_resource
{
  pthread_mutex_t lock;

  unsigned int state;		/* mode/state */

  int num_procs;
  HB_PROC_ENTRY *procs;

  bool shutdown;
};


/* heartbeat cluster job argument */
typedef struct hb_cluster_job_arg HB_CLUSTER_JOB_ARG;
struct hb_cluster_job_arg
{
  unsigned int ping_check_count;
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

extern int hb_master_init (void);
extern void hb_resource_shutdown_and_cleanup (void);
extern void hb_cluster_shutdown_and_cleanup (void);

extern void hb_cleanup_conn_and_start_process (CSS_CONN_ENTRY * conn,
					       SOCKET sfd);

extern void hb_get_node_info_string (char **str, bool verbose_yn);
extern void hb_get_process_info_string (char **str, bool verbose_yn);
extern void hb_kill_all_heartbeat_process (char **str);
extern void hb_dereg_process (pid_t pid, char **str);
extern void hb_reconfig_heartbeat (char **str);
extern void hb_deactivate_heartbeat (char **str);
extern void hb_activate_heartbeat (char **str);

extern int hb_is_registered_process (CSS_CONN_ENTRY * conn, void *buffer);
extern void hb_register_new_process (CSS_CONN_ENTRY * conn);
extern void hb_resource_receive_changemode (CSS_CONN_ENTRY * conn);

#endif /* _MASTER_HEARTBEAT_H_ */
