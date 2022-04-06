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
 * connection_defs.h - all the #define, the structure defs and the typedefs
 *          for the client/server implementation
 */

#ifndef _CONNECTION_DEFS_H_
#define _CONNECTION_DEFS_H_

#ident "$Id$"

#include "boot.h"
#if defined(SERVER_MODE)
#include "connection_list_sr.h"
#include "critical_section.h"
#endif
#include "error_manager.h"
#include "memory_alloc.h"
#include "porting.h"
#include "thread_compat.hpp"

#if defined(WINDOWS)
#include <dos.h>
#endif // WINDOWS
#include <stdio.h>
#if defined(WINDOWS)
#include <process.h>
#else
#include <poll.h>
#endif /* WINDOWS */
#if !defined(WINDOWS) && defined(SERVER_MODE)
#include <pthread.h>
#endif /* !WINDOWS && SERVER_MODE */

#if defined (__cplusplus)
#include <atomic>
#endif // C++

#define NUM_MASTER_CHANNEL 1

/*
 * These are the types of top-level commands sent to the master server
 * from the client when initiating a connection. They distinguish the
 * difference between an information connection and a user connection.
 */
enum css_command_type
{
  NULL_REQUEST = 0,
  INFO_REQUEST = 1,		/* get runtime info from the master server */
  DATA_REQUEST = 2,		/* get data from the database server */
  SERVER_REQUEST = 3,		/* let new server attach */
  UNUSED_REQUEST = 4,		/* unused request - leave it for compatibility */
  SERVER_REQUEST_NEW = 5,	/* new-style server request */
  CMD_SERVER_SERVER_CONNECT = 6,
  MAX_REQUEST
};

/*
 * These are the responses from the master to a server
 * when it is trying to connect and register itself.
 */
enum css_master_response
{
  SERVER_ALREADY_EXISTS = 0,
  SERVER_REQUEST_ACCEPTED = 1,
  DRIVER_NOT_FOUND = 2,
  SERVER_REQUEST_ACCEPTED_NEW = 3
};

/*
 * These are the types of requests sent by the information client to
 * the master.
 */
enum css_client_request
{
  GET_START_TIME = 1,
  GET_SERVER_COUNT = 2,
  GET_REQUEST_COUNT = 3,
  START_MASTER_TRACING = 4,
  STOP_MASTER_TRACING = 5,
  START_SLAVE_TRACING = 6,
  STOP_SLAVE_TRACING = 7,
  SET_SERVER_LIMIT = 8,
  STOP_SERVER = 9,
  START_SERVER = 10,
  GET_SERVER_LIST = 11,
  KILL_MASTER_SERVER = 12,
  KILL_SLAVE_SERVER = 13,
  START_SHUTDOWN = 14,
  CANCEL_SHUTDOWN = 15,
  GET_SHUTDOWN_TIME = 16,
  KILL_SERVER_IMMEDIATE = 17,
  GET_REPL_LIST = 20,		/* REPL: get the info. for a process */
  GET_ALL_LIST = 21,		/* REPL: get the info. for all processes */
  GET_REPL_COUNT = 22,		/* REPL: get the # of repl processes */
  GET_ALL_COUNT = 23,		/* REPL: get the # of all processes */
  KILL_REPL_SERVER = 24,	/* REPL: kill the repl process */
  GET_SERVER_HA_MODE = 25,	/* HA: get server ha mode */
  GET_HA_NODE_LIST = 26,	/* HA: get ha node list */
  GET_HA_NODE_LIST_VERBOSE = 27,	/* HA: get ha node list verbose */
  GET_HA_PROCESS_LIST = 28,	/* HA: get ha process list */
  GET_HA_PROCESS_LIST_VERBOSE = 29,	/* HA: get ha process list verbose */
  DEREGISTER_HA_PROCESS_BY_PID = 30,	/* HA: deregister ha process by pid */
  RECONFIG_HEARTBEAT = 31,	/* HA: reconfigure ha node */
  DEACTIVATE_HEARTBEAT = 32,	/* HA: deactivate */
  ACTIVATE_HEARTBEAT = 33,	/* HA: activate */
  KILL_ALL_HA_PROCESS = 34,	/* HA: kill all ha processes */
  IS_REGISTERED_HA_PROC = 35,	/* HA: check registered ha process */
  DEREGISTER_HA_PROCESS_BY_ARGS = 36,	/* HA: deregister ha process by args */
  GET_HA_PING_HOST_INFO = 37,	/* HA: get ping hosts info */
  DEACT_STOP_ALL = 38,		/* HA: prepare for deactivation */
  DEACT_CONFIRM_STOP_ALL = 39,	/* HA: confirm preparation for deactiavtion */
  DEACT_CONFIRM_NO_SERVER = 40,	/* HA: confirm the completion of deactivation */
  GET_HA_ADMIN_INFO = 41,	/* HA: get administrative info */
  GET_SERVER_STATE = 42,	/* broker: get the server state */
  START_HA_UTIL_PROCESS = 43	/* HA: start ha utility process */
};

/*
 * These are the types of requests sent between the master and the servers.
 */
enum css_server_request
{
  SERVER_START_TRACING = 1,
  SERVER_STOP_TRACING = 2,
  SERVER_HALT_EXECUTION = 3,
  SERVER_RESUME_EXECUTION = 4,
  SERVER_START_NEW_CLIENT = 5,
  SERVER_START_SHUTDOWN = 6,
  SERVER_STOP_SHUTDOWN = 7,
  SERVER_SHUTDOWN_IMMEDIATE = 8,
  SERVER_GET_HA_MODE = 9,
  SERVER_REGISTER_HA_PROCESS = 10,
  SERVER_CHANGE_HA_MODE = 11,
  SERVER_DEREGISTER_HA_PROCESS = 12,
  SERVER_GET_EOF = 13,
  SERVER_SERVER_CONNECT = 14,
};
typedef enum css_server_request CSS_SERVER_REQUEST;

/*
 * These are the status codes for the connection structure which represent
 * the state of the connection.
 */
enum css_conn_status
{
  CONN_OPEN = 1,
  CONN_CLOSED = 2,
  CONN_CLOSING = 3
};

/*
 * These are the types of fds in the socket queue.
 */
enum
{
  READ_WRITE = 0,
  READ_ONLY = 1,
  WRITE_ONLY = 2
};

/*
 * These are the types of "packets" that can be sent over the comm interface.
 */
enum css_packet_type
{
  COMMAND_TYPE = 1,
  DATA_TYPE = 2,
  ABORT_TYPE = 3,
  CLOSE_TYPE = 4,
  ERROR_TYPE = 5
};

/*
 * These are the status conditions that can be returned when a client
 * is trying to get a connection.
 */
enum css_status
{
  SERVER_CONNECTED = 0,
  SERVER_NOT_FOUND = 1,
  SERVER_STARTED = 2,
  SERVER_IS_RECOVERING = 3,	/* not used */
  SERVER_HAS_SHUT_DOWN = 4,	/* not used */
  ERROR_MESSAGE_FROM_MASTER = 5,	/* an error message is returned */
  SERVER_CONNECTED_NEW = 6,
  SERVER_CLIENTS_EXCEEDED = 7,
  SERVER_INACCESSIBLE_IP = 8,
  SERVER_HANG = 9
};

/*
 * These are the error values returned by the client and server interfaces
 */
enum css_error_code
{
  NO_ERRORS = 1,
  CONNECTION_CLOSED = 2,
  REQUEST_REFUSED = 3,
  ERROR_ON_READ = 4,
  ERROR_ON_WRITE = 5,
  RECORD_TRUNCATED = 6,
  ERROR_WHEN_READING_SIZE = 7,
  READ_LENGTH_MISMATCH = 8,
  ERROR_ON_COMMAND_READ = 9,
  NO_DATA_AVAILABLE = 10,
  WRONG_PACKET_TYPE = 11,
  SERVER_WAS_NOT_FOUND = 12,
  SERVER_ABORTED = 13,
  INTERRUPTED_READ = 14,
  CANT_ALLOC_BUFFER = 15,
  OS_ERROR = 16,
  TIMEDOUT_ON_QUEUE = 17,
  INTERNAL_CSS_ERROR = 18
};

/*
 * Server's request_handler status codes.
 * Assigned to error_p in current socket queue entry.
 */
enum css_status_code
{
  CSS_NO_ERRORS = 0,
  CSS_UNPLANNED_SHUTDOWN = 1,
  CSS_PLANNED_SHUTDOWN = 2
};

/*
 * There are the modes to check peer-alive.
 */
enum css_check_peer_alive
{
  CSS_CHECK_PEER_ALIVE_NONE,
  CSS_CHECK_PEER_ALIVE_SERVER_ONLY,
  CSS_CHECK_PEER_ALIVE_CLIENT_ONLY,
  CSS_CHECK_PEER_ALIVE_BOTH
};
#define CHECK_CLIENT_IS_ALIVE() \
  (prm_get_integer_value (PRM_ID_CHECK_PEER_ALIVE) == CSS_CHECK_PEER_ALIVE_BOTH \
  || prm_get_integer_value (PRM_ID_CHECK_PEER_ALIVE) == CSS_CHECK_PEER_ALIVE_SERVER_ONLY)
#define CHECK_SERVER_IS_ALIVE() \
  (prm_get_integer_value (PRM_ID_CHECK_PEER_ALIVE) == CSS_CHECK_PEER_ALIVE_BOTH \
  || prm_get_integer_value (PRM_ID_CHECK_PEER_ALIVE) == CSS_CHECK_PEER_ALIVE_CLIENT_ONLY)

/*
 * HA mode
 */
enum ha_mode
{
  HA_MODE_OFF = 0,
  HA_MODE_FAIL_OVER = 1,	/* unused */
  HA_MODE_FAIL_BACK = 2,
  HA_MODE_LAZY_BACK = 3,	/* not implemented yet */
  HA_MODE_ROLE_CHANGE = 4,
  HA_MODE_REPLICA = 5
};
typedef enum ha_mode HA_MODE;
#define HA_MODE_OFF_STR		"off"
#define HA_MODE_FAIL_OVER_STR	"fail-over"
#define HA_MODE_FAIL_BACK_STR	"fail-back"
#define HA_MODE_LAZY_BACK_STR	"lazy-back"
#define HA_MODE_ROLE_CHANGE_STR	"role-change"
#define HA_MODE_REPLICA_STR     "replica"
#define HA_MODE_ON_STR          "on"

#define HA_GET_MODE() ((HA_MODE) prm_get_integer_value (PRM_ID_HA_MODE))
#define HA_DISABLED() (HA_GET_MODE () == HA_MODE_OFF)

/*
 * HA server mode
 */
enum ha_server_mode
{
  HA_SERVER_MODE_ACTIVE = 0,
  HA_SERVER_MODE_STANDBY = 1,
  HA_SERVER_MODE_BACKUP = 2,
  HA_SERVER_MODE_PRIMARY = 0,	/* alias of active */
  HA_SERVER_MODE_SECONDARY = 1,	/* alias of standby */
  HA_SERVER_MODE_TERNARY = 2	/* alias of backup */
};
typedef enum ha_server_mode HA_SERVER_MODE;
#define HA_SERVER_MODE_ACTIVE_STR      "active"
#define HA_SERVER_MODE_STANDBY_STR     "standby"
#define HA_SERVER_MODE_BACKUP_STR      "backup"
#define HA_SERVER_MODE_PRIMARY_STR      "primary"
#define HA_SERVER_MODE_SECONDARY_STR    "secondary"
#define HA_SERVER_MODE_TERNARY_STR      "ternary"

/*
 * HA log applier state
 */
enum ha_log_applier_state
{
  HA_LOG_APPLIER_STATE_NA = -1,
  HA_LOG_APPLIER_STATE_UNREGISTERED = 0,
  HA_LOG_APPLIER_STATE_RECOVERING = 1,
  HA_LOG_APPLIER_STATE_WORKING = 2,
  HA_LOG_APPLIER_STATE_DONE = 3,
  HA_LOG_APPLIER_STATE_ERROR = 4
};
typedef enum ha_log_applier_state HA_LOG_APPLIER_STATE;
#define HA_LOG_APPLIER_STATE_UNREGISTERED_STR   "unregistered"
#define HA_LOG_APPLIER_STATE_RECOVERING_STR     "recovering"
#define HA_LOG_APPLIER_STATE_WORKING_STR        "working"
#define HA_LOG_APPLIER_STATE_DONE_STR           "done"
#define HA_LOG_APPLIER_STATE_ERROR_STR          "error"

#define HA_CHANGE_MODE_DEFAULT_TIMEOUT_IN_SECS	5
#define HA_CHANGE_MODE_IMMEDIATELY		0

#define HA_DELAY_ERR_CORRECTION             1

#define HA_REQUEST_SUCCESS      "1\0"
#define HA_REQUEST_FAILURE      "0\0"
#define HA_REQUEST_RESULT_SIZE  2

/*
 * This constant defines the maximum size of a msg from the master to the
 * server.  Every msg between the master and the server will transmit this
 * many bytes.  A constant msg size is necessary since the protocol does
 * not pre-send the msg length to the server before sending the actual msg.
 */
#define MASTER_TO_SRV_MSG_SIZE 1024

#ifdef PRINTING
#define TPRINTF(error_string, arg) \
  do \
    { \
      fprintf (stderr, error_string, (arg)); \
      fflush (stderr); \
    } \
  while (0)

#define TPRINTF2(error_string, arg1, arg2) \
  do \
    { \
      fprintf (stderr, error_string, (arg1), (arg2)); \
      fflush (stderr); \
    } \
  while (0)
#else /* PRINTING */
#define TPRINTF(error_string, arg)
#define TPRINTF2(error_string, arg1, arg2)
#endif /* PRINTING */

/* TODO: 64Bit porting */
#define HIGH16BITS(X) (((X) >> 16) & 0xffffL)
#define LOW16BITS(X)  ((X) & 0xffffL)
#define DEFAULT_HEADER_DATA {0,0,0,NULL_TRAN_INDEX,0,0,0,0,0}

#define CSS_RID_FROM_EID(eid)           ((unsigned short) LOW16BITS(eid))
#define CSS_ENTRYID_FROM_EID(eid)       ((unsigned short) HIGH16BITS(eid))

#define NET_HEADER_FLAG_METHOD_MODE         0x4000
#define NET_HEADER_FLAG_INVALIDATE_SNAPSHOT 0x8000

/*
 * This is the format of the header for each command packet that is sent
 * across the network.
 */
typedef struct packet_header NET_HEADER;
struct packet_header
{
  int type;
  int version;
  int host_id;
  int transaction_id;
  int request_id;
  int db_error;
  short function_code;
  unsigned short flags;
  int buffer_size;
};

/*
 * These are the data definitions for the queuing routines.
 */
typedef struct css_queue_entry CSS_QUEUE_ENTRY;
struct css_queue_entry
{
  CSS_QUEUE_ENTRY *next;
  char *buffer;

#if !defined(SERVER_MODE)
  unsigned int key;
#else
  int key;
#endif

  int size;
  int rc;
  int transaction_id;
  int invalidate_snapshot;
  int db_error;
  bool in_method;

#if !defined(SERVER_MODE)
  char lock;
#endif
};
#if defined(SERVER_MODE)
struct session_state;
#endif
/*
 * This data structure is the interface between the client and the
 * communication software to identify the data connection.
 */
typedef struct css_conn_entry CSS_CONN_ENTRY;
struct css_conn_entry
{
  SOCKET fd;
  unsigned short request_id;
  int status;			/* CONN_OPEN, CONN_CLOSED, CONN_CLOSING = 3 */
  int invalidate_snapshot;
  int client_id;
  int db_error;
  bool in_transaction;		/* this client is in-transaction or out-of- */
  bool reset_on_commit;		/* set reset_on_commit when commit/abort */
  bool in_method;		/* this connection is for method callback */

  bool in_flashback;		/* this client is in progress of flashback */
#if defined(SERVER_MODE)
  int idx;			/* connection index */
  BOOT_CLIENT_TYPE client_type;
  SYNC_RMUTEX rmutex;		/* connection mutex */

  bool stop_talk;		/* block and stop this connection */
  bool ignore_repl_delay;	/* don't do reset_on_commit by the delay of replication */
  unsigned short stop_phase;

  char *version_string;		/* client version string */

  CSS_QUEUE_ENTRY *free_queue_list;
  struct css_wait_queue_entry *free_wait_queue_list;
  char *free_net_header_list;
  int free_queue_count;
  int free_wait_queue_count;
  int free_net_header_count;

  CSS_LIST request_queue;	/* list of requests */
  CSS_LIST data_queue;		/* list of data packets */
  CSS_LIST data_wait_queue;	/* list of waiters */
  CSS_LIST abort_queue;		/* list of aborted requests */
  CSS_LIST buffer_queue;	/* list of buffers queued for data */
  CSS_LIST error_queue;		/* list of (server) error messages */
  struct session_state *session_p;	/* session object for current request */
#else
  FILE *file;
  CSS_QUEUE_ENTRY *request_queue;	/* the header for unseen requests */
  CSS_QUEUE_ENTRY *data_queue;	/* header for unseen data packets */
  CSS_QUEUE_ENTRY *abort_queue;	/* queue of aborted requests */
  CSS_QUEUE_ENTRY *buffer_queue;	/* header of buffers queued for data */
  CSS_QUEUE_ENTRY *error_queue;	/* queue of (server) error messages */
  void *cnxn;
#endif
  SESSION_ID session_id;
  CSS_CONN_ENTRY *next;

#if defined __cplusplus
  // transaction ID manipulation
  void set_tran_index (int tran_index);
  int get_tran_index (void);

  // request count manipulation
  void add_pending_request ();
  void start_request ();
  bool has_pending_request () const;
  void init_pending_request ();

private:
  // note - I want to protect this.
  int transaction_id;
  // *INDENT-OFF*
  std::atomic<size_t> pending_request_count;
  // *INDENT-ON*
#else				// not c++ = c
  int transaction_id;
#endif				// not c++ = c
};

/*
 * This is the mapping entry from a host/key to/from the entry id.
 */
typedef struct css_mapping_entry CSS_MAP_ENTRY;
struct css_mapping_entry
{
  char *key;			/* host name (or some such) */
  CSS_CONN_ENTRY *conn;		/* the connection */
#if !defined(SERVER_MODE)
  CSS_MAP_ENTRY *next;
#endif
  unsigned short id;		/* host id to help identify the connection */
};

/*
 * This data structure is the information of user access status written
 * when client login server.
 */
typedef struct last_access_status LAST_ACCESS_STATUS;
struct last_access_status
{
  char db_user[DB_MAX_USER_LENGTH];
  time_t time;
  char host[CUB_MAXHOSTNAMELEN];
  char program_name[32];
  LAST_ACCESS_STATUS *next;
};

#endif /* _CONNECTION_DEFS_H_ */
