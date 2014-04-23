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
#define HB_DEFAULT_DEREG_CONFIRM_INTERVAL_IN_MSECS      (1*1000)
#define HB_DEFAULT_MAX_PROCESS_START_CONFIRM            (20)
#define HB_DEFAULT_MAX_PROCESS_DEREG_CONFIRM            (60)
#define HB_DEFAULT_MAX_PROCESS_SHUTDOWN_CONFIRM_EXCEPT_SERVER (5)
#define HB_DEFAULT_UNACCEPTABLE_PROC_RESTART_TIMEDIFF_IN_MSECS   (2*60*1000)
#define HB_DEFAULT_CHANGEMODE_INTERVAL_IN_MSECS		(5*1000)
#define HB_DEFAULT_MAX_HEARTBEAT_GAP                    (5)

#define HB_JOB_TIMER_IMMEDIATELY                        (0)
#define HB_JOB_TIMER_WAIT_A_SECOND                      (1*1000)

#define HB_START_WAITING_TIME_IN_SECS			(10)

enum HA_READ_MODE
{
  HA_IGNORE = 0,
  HA_READ = 1
};

/* heartbeat resource process type */
typedef enum hb_proc_type HB_PROC_TYPE;
enum hb_proc_type
{
  HB_PTYPE_SERVER = 0,
  HB_PTYPE_COPYLOGDB = 1,
  HB_PTYPE_APPLYLOGDB = 2,
  HB_PTYPE_PREFETCHLOGDB = 3,
  HB_PTYPE_MAX
};
#define HB_PTYPE_SERVER_STR             "HA-server"
#define HB_PTYPE_COPYLOGDB_STR          "HA-copylogdb"
#define HB_PTYPE_APPLYLOGDB_STR         "HA-applylogdb"
#define HB_PTYPE_PREFETCHLOGDB_STR      "HA-prefetchlogdb"
#define HB_PTYPE_STR_SZ                 (16)

#if !defined(WINDOWS)
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
  char orig_host_name[MAXHOSTNAMELEN];
  char dest_host_name[MAXHOSTNAMELEN];
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
  char argv[HB_MAX_NUM_PROC_ARGV][HB_MAX_SZ_PROC_ARGV];
};


/* 
 * externs 
 */
extern const char *hb_process_type_string (int ptype);
extern void hb_set_exec_path (char *exec_path);
extern void hb_set_argv (char **argv);
extern int css_send_heartbeat_request (CSS_CONN_ENTRY * conn, int command);
extern int css_send_heartbeat_data (CSS_CONN_ENTRY * conn, const char *data,
				    int size);
extern int css_receive_heartbeat_request (CSS_CONN_ENTRY * conn,
					  int *command);
extern int css_receive_heartbeat_data (CSS_CONN_ENTRY * conn, char *data,
				       int size);
extern int hb_process_master_request (void);
extern int hb_register_to_master (CSS_CONN_ENTRY * conn, int type);
extern int hb_deregister_from_master (void);
extern int hb_process_init (const char *server_name, const char *log_path,
			    HB_PROC_TYPE type);
extern void hb_process_term (void);

extern bool hb_Proc_shutdown;

#endif /* !WINDOWS */

#endif /* _HEARTBEAT_H_ */
