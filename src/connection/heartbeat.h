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
 * heartbeat.h -
 */

#ifndef _HEARTBEAT_H_
#define _HEARTBEAT_H_

#ident "$Id$"

#include "connection_defs.h"

/* heartbeat */
#define HB_DEFAULT_HA_PORT_ID                           (59901)
#define HB_DEFAULT_APPLY_MAX_MEM_SIZE                   (500)

#define HB_DEFAULT_INIT_TIMER_IN_MSECS                  (10*1000)
#define HB_DEFAULT_HEARTBEAT_INTERVAL_IN_MSECS          (500)
#define HB_DEFAULT_CALC_SCORE_INTERVAL_IN_MSECS         (3*1000)
#define HB_DEFAULT_CHECK_VALID_PING_SERVER_INTERVAL_IN_MSECS  (1*60*60*1000)
#define HB_TEMP_CHECK_VALID_PING_SERVER_INTERVAL_IN_MSECS     (5*60*1000)
#define HB_DEFAULT_FAILOVER_WAIT_TIME_IN_MSECS          (3*1000)
#define HB_DEFAULT_START_CONFIRM_INTERVAL_IN_MSECS      (3*1000)
#define HB_DEFAULT_DEREG_CONFIRM_INTERVAL_IN_MSECS      (500)
#define HB_DEFAULT_MAX_PROCESS_START_CONFIRM            (20)
#define HB_DEFAULT_MAX_PROCESS_DEREG_CONFIRM            (120)
#define HB_DEFAULT_UNACCEPTABLE_PROC_RESTART_TIMEDIFF_IN_MSECS   (2*60*1000)
#define HB_DEFAULT_CHANGEMODE_INTERVAL_IN_MSECS		(5*1000)
#define HB_DEFAULT_MAX_HEARTBEAT_GAP                    (5)
#define HB_MIN_DIFF_CHECK_DISK_FAILURE_INTERVAL_IN_SECS (10)

#define HB_JOB_TIMER_IMMEDIATELY                        (0)
#define HB_JOB_TIMER_WAIT_A_SECOND                      (1*1000)
#define HB_JOB_TIMER_WAIT_500_MILLISECOND               (5*100)
#define HB_JOB_TIMER_WAIT_100_MILLISECOND               (1*100)
#define HB_DISK_FAILURE_CHECK_TIMER_IN_MSECS            (1*100)

#define HB_START_WAITING_TIME_IN_SECS			(10)
#define HB_STOP_WAITING_TIME_IN_SECS			(1)

/* heartbeat resource process type */
enum hb_proc_type
{
  HB_PTYPE_PAGE_SERVER = 0,
  HB_PTYPE_TRAN_SERVER = 1,
  // TODO: Remove HB_PTYPE_SERVER/COPYLOGDB/APPLYLOGDB
  HB_PTYPE_SERVER = 2,
  HB_PTYPE_COPYLOGDB = 3,
  HB_PTYPE_APPLYLOGDB = 4,
  HB_PTYPE_MAX
};
typedef enum hb_proc_type HB_PROC_TYPE;
#define HB_PTYPE_TRAN_SERVER_STR        "HA-tran-server"
#define HB_PTYPE_PAGE_SERVER_STR        "HA-page-server"
// TODO: Remove HB_PTYPE_SERVER/COPYLOGDB/APPLYLOGDB_STR
#define HB_PTYPE_SERVER_STR             "HA-server"
#define HB_PTYPE_COPYLOGDB_STR          "HA-copylogdb"
#define HB_PTYPE_APPLYLOGDB_STR         "HA-applylogdb"
#define HB_PTYPE_STR_SZ                 (16)

enum HBP_CLUSTER_MESSAGE
{
  HBP_CLUSTER_HEARTBEAT = 0,
  HBP_CLUSTER_MSG_MAX
};

#define HB_MAX_GROUP_ID_LEN		(64)
#define HB_MAX_SZ_PROC_EXEC_PATH        (128)
#define HB_MAX_NUM_PROC_ARGV            (16)
#define HB_MAX_SZ_PROC_ARGV             (64)
#define HB_MAX_SZ_PROC_ARGS             (HB_MAX_NUM_PROC_ARGV*HB_MAX_SZ_PROC_ARGV)

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

#define HB_NSTATE_STR_SZ        (32)

typedef enum HB_NODE_STATE HB_NODE_STATE_TYPE;

/*
 * heartbeat cluster message header and body
 */

/* heartbeat net header */
typedef struct hbp_header HBP_HEADER;
struct hbp_header
{
  unsigned char type;
#if defined(HPUX) || defined(_AIX) || defined(sparc)
  char r:1;			/* is request? */
  char reserved:7;
#else
  char reserved:7;
  char r:1;			/* is request? */
#endif
  unsigned short len;
  unsigned int seq;
  char group_id[HB_MAX_GROUP_ID_LEN];
  char orig_host_name[CUB_MAXHOSTNAMELEN];
  char dest_host_name[CUB_MAXHOSTNAMELEN];
};


/*
 * heartbeat resource message body
 */

/* process register */
typedef struct hbp_proc_register HBP_PROC_REGISTER;
struct hbp_proc_register
{
  int pid;
  int type;
  char exec_path[HB_MAX_SZ_PROC_EXEC_PATH];
  char args[HB_MAX_SZ_PROC_ARGS];
};


/*
 * externs
 */
extern const char *hb_process_type_string (int ptype);
extern void hb_set_exec_path (char *exec_path);
extern void hb_set_argv (char **argv);
extern int css_send_heartbeat_request (CSS_CONN_ENTRY * conn, int command);
extern int css_send_heartbeat_data (CSS_CONN_ENTRY * conn, const char *data, int size);
extern int css_receive_heartbeat_request (CSS_CONN_ENTRY * conn, int *command);
extern int css_receive_heartbeat_data (CSS_CONN_ENTRY * conn, char *data, int size);
extern int hb_process_master_request (void);
extern int hb_register_to_master (CSS_CONN_ENTRY * conn, int type);
extern int hb_deregister_from_master (void);
extern int hb_process_init (const char *server_name, const char *log_path, HB_PROC_TYPE type);
extern void hb_process_term (void);
extern const char *hb_node_state_string (HB_NODE_STATE_TYPE nstate);

extern bool hb_Proc_shutdown;

#endif /* _HEARTBEAT_H_ */
