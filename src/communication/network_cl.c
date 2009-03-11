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
 * network_cl.c - client side support functions.
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <string.h>

/* for performance metering */
#if !defined(WINDOWS)
#include <sys/time.h>
#include <sys/resource.h>
#endif /* !WINDDOWS */

#include "network.h"
#include "network_interface_cl.h"
#include "chartype.h"
#include "connection_cl.h"
#include "server_interface.h"
#include "memory_alloc.h"
#include "databases_file.h"
#include "error_manager.h"
#include "system_parameter.h"
#include "boot_cl.h"
#include "xasl_support.h"
#include "query_method.h"
#include "release_string.h"
#include "log_comm.h"
#include "file_io.h"
#include "locator.h"
#include "db.h"
#include "client_support.h"
#include "perf_monitor.h"

#if defined(CS_MODE)
extern unsigned short css_return_rid_from_eid (unsigned int eid);
#endif /* CS_MODE */

/*
 * To check for errors from the comm system. Note that if we get any error
 * other than RECORD_TRUNCATED or CANT_ALLOC_BUFFER, we will call it a
 * SERVER_CRASHED error.  Also note that CANT_ALLOC_BUFFER allows the
 * calling function to continue whereas other errors disconnect and escape
 * the function.
 */

#define SET_ALLOC_ERR_AND_READ_EXPECTED_PACKETS(err, rc, num_packets) \
  set_alloc_err_and_read_expected_packets((err), (rc), (num_packets), \
					  __FILE__, __LINE__)

#define COMPARE_SIZE_AND_BUFFER(replysize, size, replybuf, buf)       \
  compare_size_and_buffer((replysize), (size), (replybuf), (buf),     \
			  __FILE__, __LINE__)

#define UNKNOWN_REQUEST_STRING "unknown request"

#define LAST_ENTRY SERVER_SHUTDOWN

/*
 * Add instrumentation to the client side to get histogram of network
 * requests
 */

typedef struct request_buffer REQUEST_BUFFER;
struct request_buffer
{
  const char *name;
  int request_count;
  int total_size_sent;
  int total_size_received;
  int elapsed_time;
};

typedef struct request_buffer REQ_BUF;
typedef REQ_BUF *REQ_BUF_PTR;

static REQ_BUF req_buffer[LAST_ENTRY + 1];

static int Setup = 0;
static int Setup_mnt = 0;

static int Histo_call_count = 0;
static int Histo_last_call_time = 0;
static int Histo_total_server_time = 0;

#if defined(CS_MODE)
unsigned short method_request_id;
#endif /* CS_MODE */

/* Contains the name of the current sever host machine.  */
static char net_Server_host[MAX_SERVER_HOST_NAME] = { 0 };

/* Contains the name of the current server name. */
static char net_Server_name[MAX_SERVER_NAME] = { 0 };

#if defined(CS_MODE)
static int save_query_id = -1;
#endif /* CS_MODE */


static void return_error_to_server (char *host, unsigned int eid);
static void record_server_capabilities (int server_capabilities);
static void
set_alloc_err_and_read_expected_packets (int *err, int rc, int num_packets,
					 const char *file, const int line);
static int
compare_size_and_buffer (int *replysize, int size, char **replybuf, char *buf,
			 const char *file, const int line);
static bool
net_check_client_server_compatibility (const char *server_release,
				       const char *client_release);
static int
net_client_request_internal (bool send_by_oob, int request, char *argbuf,
			     int argsize, char *replybuf, int replysize,
			     char *databuf, int datasize, char *replydata,
			     int replydatasize);
static int
net_client_request_buffer (unsigned int rc, char **buf_ptr,
			   int expected_size);

static int set_server_error (int error);

#if defined(CUBRID_DEBUG)
static void
add_client_name_to_request (char *request, unsigned int *request_size);
#endif /* CUBRID_DEBUG */

/*
 * Shouldn't know about db_Connect_status at this level, must set this
 * to disable all db_ functions
 */

/*
 * set_server_error -
 *
 * return:
 *   error(in):
 *
 * Note:
 */

static int
set_server_error (int error)
{
  int server_error;
  switch (error)
    {
    case CANT_ALLOC_BUFFER:
      server_error = ER_NET_CANT_ALLOC_BUFFER;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, server_error, 0);
      break;
    case RECORD_TRUNCATED:
      server_error = ER_NET_DATA_TRUNCATED;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, server_error, 0);
      break;
    default:
      server_error = ER_NET_SERVER_CRASHED;
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, server_error, 0);
      break;
    }

  db_Connect_status = 0;

  if (net_Server_name[0] != '\0')
    {
      net_Server_name[0] = '\0';
      net_Server_host[0] = '\0';
      boot_server_die ();
    }

  return (server_error);
}

/*
 * return_error_to_server -
 *
 * return:
 *
 *   host(in):
 *   eid(in):
 *
 * Note:
 */
static void
return_error_to_server (char *host, unsigned int eid)
{
  void *error_string;
  int length;

  error_string = er_get_area_error (&length);
  if (error_string != NULL)
    {
      css_send_error_to_server (host, eid, (char *) error_string, length);
      free_and_init (error_string);
    }
}

/*
 * record_server_capabilities -
 *
 * return:
 *
 *   server_capabilities(in):
 *
 * Note:
 */
static void
record_server_capabilities (int server_capabilities)
{
  if (server_capabilities & NET_INTERRUPT_ENABLED_CAP)
    {
      net_Interrupt_enabled = 1;
    }
  else
    {
      net_Interrupt_enabled = 0;
    }
}

/*
 * set_alloc_err_and_read_expected_packets -
 *
 * return:
 *
 *   err(in):
 *   rc(in):
 *   num_packets(in):
 *   file(in):
 *   line(in):
 *
 * Note:
 *    Allocation failures are recorded and any outstanding data packets
 *    will try to be read.  Called by macro of the same name.
 */
static void
set_alloc_err_and_read_expected_packets (int *err, int rc, int num_packets,
					 const char *file, const int line)
{
  char *reply = NULL;
  int i, size = 0;

  /* don't set error if there already is one */
  if (!(*err))
    {
      *err = ER_NET_CANT_ALLOC_BUFFER;
      er_set (ER_ERROR_SEVERITY, file, line, *err, 0);
    }
  for (i = 0; i < (num_packets); i++)
    {
      css_receive_data_from_server ((rc), &reply, &size);
      if (reply != NULL)
	{
	  free_and_init (reply);
	}
    }
}

/*
 * compare_size_and_buffer -
 *
 * return:
 *
 *   replysize(in):
 *   size(in):
 *   replybuf(in):
 *   buf(in):
 *   file(in):
 *   line(in):
 *
 * Note:
 *    Compares sizes and buffers that have been queued with the actual
 *    received values after a data read.  Called by macro of the same name.
 */
static int
compare_size_and_buffer (int *replysize, int size, char **replybuf, char *buf,
			 const char *file, const int line)
{
  int err = NO_ERROR;

  if (size != *replysize)
    {
      err = ER_NET_DATASIZE_MISMATCH;
      er_set (ER_ERROR_SEVERITY, file, line, err, 2, replysize, size);
      *replysize = size;
    }
  if (buf != *replybuf)
    {
      err = ER_NET_UNUSED_BUFFER;
      er_set (ER_ERROR_SEVERITY, file, line, err, 0);
      /* free it ? */
      *replybuf = buf;
    }
  return err;
}

/*
 * net_check_client_server_compatibility -
 *
 * return: true if compatible
 *
 *   server_release(in):
 *   client_release(in):
 *
 * Note:
 *    Compare the release strings from the server and client to
 *    determine compatibility.
 *
 *    We considered whether or not to create an additional
 *    "protocol" version string that must be incremented every time
 *    someone makes a change that would affect the communications protocol.
 *
 *    We decided that the developer who realizes that their change would
 *    have an impact is the developer who will make sure their change is
 *    backwardly compatible.  An un-incremented protocol string does no
 *    good to detect unwitting protocol changes.
 *
 */
static bool
net_check_client_server_compatibility (const char *server_release,
				       const char *client_release)
{
  bool compatible = false;

  /* Release strings should be in the form: "<major>.<minor>[.<bugfix>]".
   * The server and client must have the same release.
   */
  if (server_release && client_release &&
      rel_compare ((char *) server_release, (char *) client_release) == 0)
    {
      compatible = true;
    }
  return compatible;
}

/*
 * histo_setup_names -
 *
 * return:
 *
 * Note:
 */
void
histo_setup_names (void)
{
  unsigned int i;

  for (i = 0; i < DIM (req_buffer); i++)
    {
      req_buffer[i].name = UNKNOWN_REQUEST_STRING;
    }

  req_buffer[SERVER_PING_WITH_HANDSHAKE].name = "PING_WITH_HANDSHAKE";

  req_buffer[SERVER_BO_INIT_SERVER].name = "BO_INIT_SERVER";
  req_buffer[SERVER_BO_RESTART_SERVER].name = "BO_RESTART_SERVER";
  req_buffer[SERVER_BO_REGISTER_CLIENT].name = "BO_REGISTER_CLIENT";
  req_buffer[SERVER_BO_UNREGISTER_CLIENT].name = "BO_UNREGISTER_CLIENT";
  req_buffer[SERVER_BO_SIMULATE_SERVER_CRASH].name =
    "BO_SIMULATE_SERVER_CRASH";
  req_buffer[SERVER_BO_BACKUP].name = "BO_BACKUP";
  req_buffer[SERVER_BO_ADD_VOLEXT].name = "BO_ADD_VOLEXT";
  req_buffer[SERVER_BO_CHECK_DBCONSISTENCY].name = "BO_CHECK_DBCONSISTENCY";
  req_buffer[SERVER_BO_FIND_NPERM_VOLS].name = "BO_FIND_NPERM_VOLS";

  req_buffer[SERVER_TM_SERVER_COMMIT].name = "TM_SERVER_COMMIT";
  req_buffer[SERVER_TM_SERVER_ABORT].name = "TM_SERVER_ABORT";
  req_buffer[SERVER_TM_SERVER_START_TOPOP].name = "TM_SERVER_START_TOPOP";
  req_buffer[SERVER_TM_SERVER_END_TOPOP].name = "TM_SERVER_END_TOPOP";
  req_buffer[SERVER_TM_SERVER_SAVEPOINT].name = "TM_SERVER_SAVEPOINT";
  req_buffer[SERVER_TM_SERVER_PARTIAL_ABORT].name = "TM_SERVER_PARTIAL_ABORT";
  req_buffer[SERVER_TM_SERVER_HAS_UPDATED].name = "TM_SERVER_HAS_UPDATED";
  req_buffer[SERVER_TM_SERVER_ISACTIVE_AND_HAS_UPDATED].name =
    "TM_SERVER_ISACTIVE_AND_HAS_UPDATED";
  req_buffer[SERVER_TM_ISBLOCKED].name = "TM_ISBLOCKED";
  req_buffer[SERVER_TM_SERVER_2PC_ATTACH_GT].name = "TM_SERVER_2PC_ATTACH_GT";
  req_buffer[SERVER_TM_SERVER_2PC_PREPARE_GT].name =
    "TM_SERVER_2PC_PREPARE_GT";

  req_buffer[SERVER_LC_FETCH].name = "LC_FETCH";
  req_buffer[SERVER_LC_FETCHALL].name = "LC_FETCHALL";
  req_buffer[SERVER_LC_FETCH_LOCKSET].name = "LC_FETCH_LOCKSET";
  req_buffer[SERVER_LC_FETCH_ALLREFS_LOCKSET].name =
    "LC_FETCH_ALLREFS_LOCKSET";
  req_buffer[SERVER_LC_GET_CLASS].name = "LC_GET_CLASS";
  req_buffer[SERVER_LC_FIND_CLASSOID].name = "LC_FIND_CLASSOID";
  req_buffer[SERVER_LC_DOESEXIST].name = "LC_DOESEXIST";
  req_buffer[SERVER_LC_FORCE].name = "LC_FORCE";
  req_buffer[SERVER_LC_RESERVE_CLASSNAME].name = "LC_RESERVE_CLASSNAME";
  req_buffer[SERVER_LC_DELETE_CLASSNAME].name = "LC_DELETE_CLASSNAME";
  req_buffer[SERVER_LC_RENAME_CLASSNAME].name = "LC_RENAME_CLASSNAME";
  req_buffer[SERVER_LC_ASSIGN_OID].name = "LC_ASSIGN_OID";
  req_buffer[SERVER_LC_NOTIFY_ISOLATION_INCONS].name =
    "LC_NOTIFY_ISOLATION_INCONS";
  req_buffer[SERVER_LC_FIND_LOCKHINT_CLASSOIDS].name =
    "LC_FIND_LOCKHINT_CLASSOIDS";
  req_buffer[SERVER_LC_FETCH_LOCKHINT_CLASSES].name =
    "LC_FETCH_LOCKHINT_CLASSES";

  req_buffer[SERVER_HEAP_CREATE].name = "HEAP_CREATE";
  req_buffer[SERVER_HEAP_DESTROY].name = "HEAP_DESTROY";
  req_buffer[SERVER_HEAP_DESTROY_WHEN_NEW].name = "HEAP_DESTROY_WHEN_NEW";

  req_buffer[SERVER_LARGEOBJMGR_CREATE].name = "LARGEOBJMGR_CREATE";
  req_buffer[SERVER_LARGEOBJMGR_READ].name = "LARGEOBJMGR_READ";
  req_buffer[SERVER_LARGEOBJMGR_WRITE].name = "LARGEOBJMGR_WRITE";
  req_buffer[SERVER_LARGEOBJMGR_INSERT].name = "LARGEOBJMGR_INSERT";
  req_buffer[SERVER_LARGEOBJMGR_DESTROY].name = "LARGEOBJMGR_DESTROY";
  req_buffer[SERVER_LARGEOBJMGR_DELETE].name = "LARGEOBJMGR_DELETE";
  req_buffer[SERVER_LARGEOBJMGR_APPEND].name = "LARGEOBJMGR_APPEND";
  req_buffer[SERVER_LARGEOBJMGR_TRUNCATE].name = "LARGEOBJMGR_TRUNCATE";
  req_buffer[SERVER_LARGEOBJMGR_COMPRESS].name = "LARGEOBJMGR_COMPRESS";
  req_buffer[SERVER_LARGEOBJMGR_LENGTH].name = "LARGEOBJMGR_LENGTH";

  req_buffer[SERVER_LOG_RESET_WAITSECS].name = "LOG_RESET_WAITSECS";
  req_buffer[SERVER_LOG_RESET_ISOLATION].name = "LOG_RESET_ISOLATION";
  req_buffer[SERVER_LOG_SET_INTERRUPT].name = "LOG_SET_INTERRUPT";
  req_buffer[SERVER_LOG_CLIENT_UNDO].name = "LOG_CLIENT_UNDO";
  req_buffer[SERVER_LOG_CLIENT_POSTPONE].name = "LOG_CLIENT_POSTPONE";
  req_buffer[SERVER_LOG_HAS_FINISHED_CLIENT_POSTPONE].name =
    "LOG_HAS_FINISHED_CLIENT_POSTPONE";
  req_buffer[SERVER_LOG_HAS_FINISHED_CLIENT_UNDO].name =
    "LOG_HAS_FINISHED_CLIENT_UNDO";
  req_buffer[SERVER_LOG_CLIENT_GET_FIRST_POSTPONE].name =
    "LOG_CLIENT_GET_FIRST_POSTPONE";
  req_buffer[SERVER_LOG_CLIENT_GET_FIRST_UNDO].name =
    "LOG_CLIENT_GET_FIRST_UNDO";
  req_buffer[SERVER_LOG_CLIENT_GET_NEXT_POSTPONE].name =
    "LOG_CLIENT_GET_NEXT_POSTPONE";
  req_buffer[SERVER_LOG_CLIENT_GET_NEXT_UNDO].name =
    "LOG_CLIENT_GET_NEXT_UNDO";
  req_buffer[SERVER_LOG_CLIENT_UNKNOWN_STATE_ABORT_GET_FIRST_UNDO].name =
    "LOG_CLIENT_UNKNOWN_STATE_ABORT_GET_FIRST_UNDO";

  req_buffer[SERVER_LK_DUMP].name = "LK_DUMP";

  req_buffer[SERVER_BTREE_ADDINDEX].name = "BTREE_ADDINDEX";
  req_buffer[SERVER_BTREE_DELINDEX].name = "BTREE_DELINDEX";
  req_buffer[SERVER_BTREE_LOADINDEX].name = "BTREE_LOADINDEX";

  req_buffer[SERVER_BTREE_FIND_UNIQUE].name = "BTREE_FIND_FIND_UNIQUE";
  req_buffer[SERVER_BTREE_CLASS_UNIQUE_TEST].name = "BTREE_CLASS_UNIQUE_TEST";

  req_buffer[SERVER_DISK_TOTALPGS].name = "DISK_TOTALPGS";
  req_buffer[SERVER_DISK_FREEPGS].name = "DISK_FREEPGS";
  req_buffer[SERVER_DISK_REMARKS].name = "DISK_REMARKS";
  req_buffer[SERVER_DISK_PURPOSE].name = "DISK_PURPOSE";
  req_buffer[SERVER_DISK_PURPOSE_TOTALPGS_AND_FREEPGS].name =
    "DISK_PURPOSE_TOTALPGS_AND_FREEPGS";
  req_buffer[SERVER_DISK_VLABEL].name = "DISK_VLABEL";

  req_buffer[SERVER_QST_SERVER_GET_STATISTICS].name =
    "QST_SERVER_GET_STATISTICS";
  req_buffer[SERVER_QST_UPDATE_CLASS_STATISTICS].name =
    "QST_UPDATE_CLASS_STATISTICS";
  req_buffer[SERVER_QST_UPDATE_STATISTICS].name = "QST_UPDATE_STATISTICS";

  req_buffer[SERVER_QM_QUERY_PREPARE].name = "QM_QUERY_PREPARE";
  req_buffer[SERVER_QM_QUERY_EXECUTE].name = "QM_QUERY_EXECUTE";
  req_buffer[SERVER_QM_QUERY_PREPARE_AND_EXECUTE].name =
    "QM_QUERY_PREPARE_AND_EXECUTE";
  req_buffer[SERVER_QM_QUERY_END].name = "QM_QUERY_END";
  req_buffer[SERVER_QM_QUERY_DROP_PLAN].name = "QM_QUERY_DROP_PLAN";
  req_buffer[SERVER_QM_QUERY_DROP_ALL_PLANS].name = "QM_QUERY_DROP_ALL_PLANS";

  req_buffer[SERVER_LS_GET_LIST_FILE_PAGE].name = "LS_GET_LIST_FILE_PAGE";

  req_buffer[SERVER_MNT_SERVER_START_STATS].name = "MNT_SERVER_START_STATS";
  req_buffer[SERVER_MNT_SERVER_STOP_STATS].name = "MNT_SERVER_STOP_STATS";
  req_buffer[SERVER_MNT_SERVER_RESET_STATS].name = "MNT_SERVER_RESET_STATS";
  req_buffer[SERVER_MNT_SERVER_COPY_STATS].name = "MNT_SERVER_COPY_STATS";

  req_buffer[SERVER_CT_CAN_ACCEPT_NEW_REPR].name = "CT_CAN_ACCEPT_NEW_REPR";

  req_buffer[SERVER_BO_KILL_SERVER].name = "BO_KILL_SERVER";
  req_buffer[SERVER_BO_SIMULATE_SERVER].name = "BO_SIMULATE_SERVER";
  req_buffer[SERVER_TEST_PERFORMANCE].name = "TEST_PERFORMANCE";

  req_buffer[SERVER_SET_CLIENT_TIMEOUT].name = "SET_CLIENT_TIMEOUT";
  req_buffer[SERVER_RESTART_EVENT_HANDLER].name = "ER_RESTART_EVENT_HANDLER";
  req_buffer[SERVER_CSS_KILL_TRANSACTION].name = "CSS_KILL_TRANSACTION";
  req_buffer[SERVER_LOG_GETPACK_TRANTB].name = "LOG_GETPACK_TRANTB";

  req_buffer[SERVER_LC_ASSIGN_OID_BATCH].name = "LC_ASSIGN_OID_BATCH";

  req_buffer[SERVER_BO_FIND_NTEMP_VOLS].name = "BO_FIND_NTEMP_VOLS";
  req_buffer[SERVER_BO_FIND_LAST_TEMP].name = "BO_FIND_LAST_TEMP";

  req_buffer[SERVER_LC_REM_CLASS_FROM_INDEX].name = "LC_REM_CLASS_FROM_INDEX";

  req_buffer[SERVER_QM_QUERY_SYNC].name = "QM_QUERY_SYNC";
  req_buffer[SERVER_QM_GET_QUERY_INFO].name = "QM_GET_QUERY_INFO";
  req_buffer[SERVER_QM_QUERY_EXECUTE_ASYNC].name = "QM_QUERY_EXECUTE_ASYNC";
  req_buffer[SERVER_QM_QUERY_PREPARE_AND_EXECUTE_ASYNC].name =
    "QM_QUERY_PREPARE_AND_EXECUTE_ASYNC";

  req_buffer[SERVER_QPROC_GET_SYS_TIMESTAMP].name = "QP_GET_SYS_TIMESTAMP";
  req_buffer[SERVER_QPROC_GET_CURRENT_VALUE].name = "QP_GET_CURRENT_VALUE";
  req_buffer[SERVER_QPROC_GET_NEXT_VALUE].name = "QP_GET_NEXT_VALUE";

  req_buffer[SERVER_HEAP_GET_CLASS_NOBJS_AND_NPAGES].name =
    "HEAP_GET_CLASS_NOBJS_AND_NPAGES";
  req_buffer[SERVER_BTREE_GET_STATISTICS].name = "BF_GET_STATISTICS";

  req_buffer[SERVER_PING_WITH_HANDSHAKE].name = "PING_WITH_HANDSHAKE";

  req_buffer[SERVER_SHUTDOWN].name = "SHUTDOWN";
}

/*
 * histo_is_supported -
 *
 * return:
 *
 * Note:
 */
bool
histo_is_supported (void)
{
  /* introduce PRM_... */
  return PRM_ENABLE_HISTO;
}

/*
 * histo_clear -
 *
 * return:
 *
 * NOTE:
 */
void
histo_clear (void)
{
  unsigned int i;

  if (Setup_mnt)
    {
      mnt_reset_stats ();
    }

  Histo_call_count = 0;
  Histo_last_call_time = 0;
  Histo_total_server_time = 0;
  for (i = 0; i < DIM (req_buffer); i++)
    {
      req_buffer[i].request_count = 0;
      req_buffer[i].total_size_sent = 0;
      req_buffer[i].total_size_received = 0;
      req_buffer[i].elapsed_time = 0;
    }
}

/*
 * histo_print -
 *
 * return:
 *
 * Note:
 */
void
histo_print (void)
{
  unsigned int i;
  int found = 0, total_requests = 0, total_size_sent = 0;
  int total_size_received = 0;
  float server_time, total_server_time = 0;
  float avg_response_time, avg_client_time;

  fprintf (stdout, "\nHistogram of client requests:\n");
  fprintf (stdout, "%-31s %6s  %10s %10s , %10s \n",
	   "Name", "Rcount", "Sent size", "Recv size", "Server time");
  for (i = 0; i < DIM (req_buffer); i++)
    {
      if (req_buffer[i].request_count)
	{
	  found = 1;
	  server_time = ((float) req_buffer[i].elapsed_time / 1000000 /
			 (float) (req_buffer[i].request_count));
	  fprintf (stdout, "%-29s %6d X %10d+%10d b, %10.6f s\n",
		   req_buffer[i].name, req_buffer[i].request_count,
		   req_buffer[i].total_size_sent,
		   req_buffer[i].total_size_received, server_time);
	  total_requests += req_buffer[i].request_count;
	  total_size_sent += req_buffer[i].total_size_sent;
	  total_size_received += req_buffer[i].total_size_received;
	  total_server_time += (server_time * req_buffer[i].request_count);
	}
    }
  if (!found)
    {
      fprintf (stdout, " No server requests made\n");
    }
  else
    {
      fprintf (stdout,
	       "-------------------------------------------------------------"
	       "--------------\n");
      fprintf (stdout,
	       "Totals:                       %6d X %10d+%10d b  "
	       "%10.6f s\n", total_requests, total_size_sent,
	       total_size_received, total_server_time);
      avg_response_time = total_server_time / total_requests;
      avg_client_time = 0.0;
      fprintf (stdout, "\n Average server response time = %6.6f secs \n"
	       " Average time between client requests = %6.6f secs \n",
	       avg_response_time, avg_client_time);
    }
  if (Setup_mnt)
    {
      mnt_print_stats (NULL);
    }
}

/*
 * histo_start -
 *
 * return:
 *
 * Note:
 */
void
histo_start (void)
{
  if (Setup == 0)
    {
      histo_clear ();
      histo_setup_names ();
      Setup = 1;
    }
  if (Setup_mnt == 0)
    {
      mnt_start_stats ();
      Setup_mnt = 1;
    }
}

/*
 * histo_stop -
 *
 * return:
 *
 * Note:
 */
void
histo_stop (void)
{
  if (Setup_mnt == 1)
    {
      mnt_stop_stats ();
      Setup_mnt = 0;
    }
  if (Setup == 1)
    {
      Setup = 0;
    }
}

/*
 * histo_add_entry -
 *
 * return:
 *
 *   request(in):
 *   data_sent(in):
 *
 * Note:
 */
void
histo_add_entry (int request, int data_sent)
{
#if !defined(WINDOWS)
  struct timeval tp;
#endif /* WINDOWS */

  if (request > LAST_ENTRY)
    {
      return;
    }

  req_buffer[request].request_count++;
  req_buffer[request].total_size_sent += data_sent;
#if !defined(WINDOWS)
  if (gettimeofday (&tp, NULL) == 0)
    {
      Histo_last_call_time = tp.tv_sec * 1000000 + tp.tv_usec;
    }
#endif /* !WINDOWS */
  Histo_call_count++;
}

/*
 * histo_request_finished -
 *
 * return:
 *
 *   request(in):
 *   data_received(in):
 *
 * Note:
 */
void
histo_request_finished (int request, int data_received)
{
#if !defined(WINDOWS)
  struct timeval tp;
  int current_time;
#endif /* !WINDOWS */

  req_buffer[request].total_size_received += data_received;

#if !defined(WINDOWS)
  if (gettimeofday (&tp, NULL) == 0)
    {
      current_time = tp.tv_sec * 1000000 + tp.tv_usec;
      req_buffer[request].elapsed_time +=
	(current_time - Histo_last_call_time);
      Histo_total_server_time = (current_time - Histo_last_call_time);
    }
#endif /* !WINDOWS */
}

/*
 * histo_total_interfaces -
 *
 * return:
 *
 * Note:
 */
int
histo_total_interfaces (void)
{
  return (DIM (req_buffer));
}

/*
 * histo_hit -
 *
 * return:
 *
 *   index(in):
 *
 * Note:
 */
int
histo_hit (int index)
{
  return (req_buffer[index].request_count);
}

/*
 * histo_get_name -
 *
 * return:
 *
 *   index(in):
 *
 * Note:
 */
const char *
histo_get_name (int index)
{
  if (strcmp (req_buffer[index].name, UNKNOWN_REQUEST_STRING) == 0)
    {
      return NULL;
    }
  else
    {
      return (req_buffer[index].name);
    }
}


/*
 * net_client_req_no_reply_via_oob -
 *
 * return:
 *
 *   request(in): server request id
 *   argbuf(in): argument buffer (small)
 *   argsize(in): byte size of argbuf
 *
 * Note: same as net_client_request_no_reply, but sends the message via oob
 *       channel
 */
int
net_client_req_no_reply_via_oob (int request, char *argbuf, int argsize)
{
  unsigned int rc;
  int error;

  error = NO_ERROR;

  if (net_Server_name[0] == '\0')
    {
      /* need to have a more appropriate "unexpected disconnect" message */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NET_SERVER_CRASHED, 0);
      error = -1;
      return error;
    }

  if (Setup)
    {
      histo_add_entry (request, argsize);
    }

  rc =
    css_send_oob_to_server_with_buffer (net_Server_host, request, argbuf,
					argsize);
  if (rc == 0)
    {
      error = css_Errno;
      return set_server_error (error);
    }

  return error;
}

/*
 * net_client_get_server_host () - the name of the current sever host machine
 *
 * return: string
 */
char *
net_client_get_server_host (void)
{
  return net_Server_host;
}

/*
 * net_client_request_internal -
 *
 * return: error status
 *
 *   send_by_oob(in): flag, if true, to send request via oob mesasge.
 *   request(in): server request id
 *   argbuf(in): argument buffer (small)
 *   argsize(in): byte size of argbuf
 *   replybuf(in): reply argument buffer (small)
 *   replysize(in): size of reply argument buffer
 *   databuf(in): data buffer to send (large)
 *   datasize(in): size of data buffer
 *   replydata(in): receive data buffer (large)
 *   replydatasize(in): size of expected reply data
 *
 * Note: This is one of two functions that is called to perform a server
 *       request.  All network interface routines will call either this
 *       functino or net_client_request2.
 */
static int
net_client_request_internal (bool send_by_oob, int request, char *argbuf,
			     int argsize, char *replybuf, int replysize,
			     char *databuf, int datasize, char *replydata,
			     int replydatasize)
{
  unsigned int rc;
  int size;
  int error;
  char *reply;

  error = 0;

  if (net_Server_name[0] == '\0')
    {
      /* need to have a more appropriate "unexpected disconnect" message */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NET_SERVER_CRASHED, 0);
      error = -1;
      return error;
    }

#if defined(HISTO)
  if (Setup)
    {
      histo_add_entry (request, argsize + datasize);
    }
#endif /* HISTO */

  if (send_by_oob)
    {
      rc = css_send_oob_to_server_with_buffer (net_Server_host,
					       request, argbuf, argsize);
      if ((rc != 0) && (databuf != NULL))
	{
	  rc = css_send_data_to_server (net_Server_host,
					rc, databuf, datasize);
	}
    }
  else
    {
      rc = css_send_req_to_server (net_Server_host,
				   request, argbuf, argsize,
				   databuf, datasize, replybuf, replysize);
    }

  if (rc == 0)
    {
      error = css_Errno;
      return set_server_error (error);
    }

  if (rc)
    {
      if (replydata != NULL)
	{
	  css_queue_receive_data_buffer (rc, replydata, replydatasize);
	}
      error = css_receive_data_from_server (rc, &reply, &size);
      if (error != NO_ERROR)
	{
	  return set_server_error (error);
	}
      else
	{
	  error =
	    COMPARE_SIZE_AND_BUFFER (&replysize, size, &replybuf, reply);
	}

      if (replydata != NULL)
	{
	  error = css_receive_data_from_server (rc, &reply, &size);
	  if (error != NO_ERROR)
	    {
	      return set_server_error (error);
	    }
	  else
	    {
	      error = COMPARE_SIZE_AND_BUFFER (&replydatasize, size,
					       &replydata, reply);
	    }
	}
    }
#if defined(HISTO)
  if (Setup)
    {
      histo_request_finished (request, replysize + replydatasize);
    }
#endif /* HISTO */
  return (error);
}

/*
 * net_client_request -
 *
 * return: error status
 *
 *   request(in): server request id
 *   argbuf(in): argument buffer (small)
 *   argsize(in): byte size of argbuf
 *   replybuf(in): reply argument buffer (small)
 *   replysize(in): size of reply argument buffer
 *   databuf(in): data buffer to send (large)
 *   datasize(in): size of data buffer
 *   replydata(in): receive data buffer (large)
 *   replydatasize(in): size of expected reply data
 *
 * Note: This is one of two functions that is called to perform a server
 *    request.  All network interface routines will call either this
 *    function or net_client_request2.
 */
int
net_client_request (int request, char *argbuf, int argsize, char *replybuf,
		    int replysize, char *databuf, int datasize,
		    char *replydata, int replydatasize)
{
  /*
   * if request is SERVER_QM_QUERY_END, delay to send the request until next
   * request.
   * if next request is SERVER_TM_SERVER_COMMIT or SERVER_TM_SERVER_ABORT,
   * do not send SERVER_QM_QUERY_END request to be kept, else send it before
   * sending next request.
   */
  if (request == SERVER_QM_QUERY_END && save_query_id == -1)
    {
      /* unpack query id and save it, and return success */
      (void) or_unpack_int (argbuf, &save_query_id);
      (void) or_pack_int (replybuf, (int) NO_ERROR);
      replydatasize = sizeof (int);
      return 0;
    }

  if (request == SERVER_TM_SERVER_COMMIT || request == SERVER_TM_SERVER_ABORT)
    {
      /* skip to send SERVER_QM_QUERY_END request */
      save_query_id = -1;
    }
  else if (save_query_id != -1)
    {
      int status = ER_FAILED;
      int req_error;
      OR_ALIGNED_BUF (OR_INT_SIZE) a_request;
      char *requestbuf;
      OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
      char *reply;

      requestbuf = OR_ALIGNED_BUF_START (a_request);
      reply = OR_ALIGNED_BUF_START (a_reply);

      (void) or_pack_int (requestbuf, save_query_id);

      /* send SERVER_QM_QUERY_END request */
      req_error = net_client_request_internal (false, SERVER_QM_QUERY_END,
					       requestbuf,
					       OR_ALIGNED_BUF_SIZE
					       (a_request), reply,
					       OR_ALIGNED_BUF_SIZE (a_reply),
					       NULL, 0, NULL, 0);
      if (!req_error)
	{
	  (void) or_unpack_int (reply, &status);
	}

      save_query_id = -1;
#if 0				/* ignore result */
      return ((int) status);
#endif
    }
  return (net_client_request_internal (false, request,
				       argbuf, argsize,
				       replybuf, replysize,
				       databuf, datasize,
				       replydata, replydatasize));
}

/*
 * net_client_request_via_oob -
 *
 * return: error status
 *
 *   request(in): server request id
 *   argbuf(in): argument buffer (small)
 *   argsize(in): byte size of argbuf
 *   replybuf(in): reply argument buffer (small)
 *   replysize(in): size of reply argument buffer
 *   databuf(in): data buffer to send (large)
 *   datasize(in): size of data buffer
 *   replydata(in): receive data buffer (large)
 *   replydatasize(in): size of expected reply data
 *
 * Note: This is one of the functions called to send a request to the server.
 *    This request is sent via the out-of-band message interface which will
 *    cause an immediate interrupt on the server.
 */
int
net_client_request_via_oob (int request, char *argbuf, int argsize,
			    char *replybuf, int replysize, char *databuf,
			    int datasize, char *replydata, int replydatasize)
{
  return (net_client_request_internal (true, request,
				       argbuf, argsize,
				       replybuf, replysize,
				       databuf, datasize,
				       replydata, replydatasize));
}

/*
 * net_client_request2 -
 *
 * return: error status
 *
 *   request(in): server request id
 *   argbuf(in): argument buffer (small)
 *   argsize(in): byte size of argbuf
 *   replybuf(in): reply argument buffer (small)
 *   replysize(in): size of reply argument buffer
 *   databuf(in): data buffer to send (large)
 *   datasize(in): size of data buffer
 *   replydata_ptr(in): receive data buffer (large)
 *   replydatasize_ptr(in):  size of expected reply data
 *
 * Note: This is one of two functions that is called to perform a server
 *    request.  All network interface routines will call either this
 *    functino or net_client_request.
 *    This is similar to net_client_request but the size of the reply
 *    data buffer is not known and must be determined from the first
 *    field in the reply argument buffer.
 */
int
net_client_request2 (int request, char *argbuf, int argsize, char *replybuf,
		     int replysize, char *databuf, int datasize,
		     char **replydata_ptr, int *replydatasize_ptr)
{
  unsigned int rc;
  int size;
  int reply_datasize, error;
  char *reply, *replydata;

  error = 0;
  *replydata_ptr = NULL;
  *replydatasize_ptr = 0;

  if (net_Server_name[0] == '\0')
    {
      /* need to have a more appropriate "unexpected disconnect" message */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NET_SERVER_CRASHED, 0);
      error = -1;
      return error;
    }
#if defined(HISTO)
  if (Setup)
    {
      histo_add_entry (request, argsize + datasize);
    }
#endif /* HISTO */
  rc = css_send_req_to_server (net_Server_host,
			       request, argbuf, argsize,
			       databuf, datasize, replybuf, replysize);
  if (rc == 0)
    {
      error = css_Errno;
      return set_server_error (error);
    }

  error = css_receive_data_from_server (rc, &reply, &size);

  if (error != NO_ERROR)
    {
      return set_server_error (error);
    }
  else
    {
      error = COMPARE_SIZE_AND_BUFFER (&replysize, size, &replybuf, reply);
    }

  /* here we assume that the first integer in the reply is the length
     of the following data block */
  or_unpack_int (reply, &reply_datasize);

  if (reply_datasize)
    {
      if ((error == NO_ERROR)
	  && (replydata = (char *) malloc (reply_datasize)) != NULL)
	{
	  css_queue_receive_data_buffer (rc, replydata, reply_datasize);
	  error = css_receive_data_from_server (rc, &reply, &size);

	  if (error != NO_ERROR)
	    {
	      free_and_init (replydata);
	      return set_server_error (error);
	    }
	  else
	    {
	      error = COMPARE_SIZE_AND_BUFFER (&reply_datasize, size,
					       &replydata, reply);
	    }
	  *replydata_ptr = reply;
	  *replydatasize_ptr = size;
	}
      else
	{
	  SET_ALLOC_ERR_AND_READ_EXPECTED_PACKETS (&error, rc, 1);
	}
    }

#if defined(HISTO)
  if (Setup)
    {
      histo_request_finished (request, replysize + *replydatasize_ptr);
    }
#endif /* HISTO */
  return (error);
}

/*
 * net_client_request2_no_malloc -
 *
 * return: error status
 *
 *   request(in): server request id
 *   argbuf(in): argument buffer (small)
 *   argsize(in):  byte size of argbuf
 *   replybuf(in): reply argument buffer (small)
 *   replysize(in): size of reply argument buffer
 *   databuf(in): data buffer to send (large)
 *   datasize(in): size of data buffer
 *   replydata(in): receive data buffer (large)
 *   replydatasize_ptr(in): size of expected reply data
 *
 * Note: This is one of two functions that is called to perform a server
 *    request.  All network interface routines will call either this
 *    functino or net_client_request.
 *    This is similar to net_client_request but the size of the reply
 *    data buffer is not known and must be determined from the first
 *    field in the reply argument buffer.
 */
int
net_client_request2_no_malloc (int request, char *argbuf, int argsize,
			       char *replybuf, int replysize,
			       char *databuf, int datasize,
			       char *replydata, int *replydatasize_ptr)
{
  unsigned int rc;
  int size;
  int reply_datasize, error;
  char *reply;

  error = 0;
  *replydatasize_ptr = 0;

  if (net_Server_name[0] == '\0')
    {
      /* need to have a more appropriate "unexpected disconnect" message */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NET_SERVER_CRASHED, 0);
      error = -1;
    }
  else
    {
#if defined(HISTO)
      if (Setup)
	{
	  histo_add_entry (request, argsize + datasize);
	}
#endif /* HISTO */
      rc =
	css_send_req_to_server (net_Server_host, request, argbuf, argsize,
				databuf, datasize, replybuf, replysize);
      if (rc == 0)
	{
	  return set_server_error (css_Errno);
	}

      error = css_receive_data_from_server (rc, &reply, &size);

      if (error != NO_ERROR)
	{
	  return set_server_error (error);
	}
      else
	{
	  error =
	    COMPARE_SIZE_AND_BUFFER (&replysize, size, &replybuf, reply);
	}

      /* here we assume that the first integer in the reply is the length
         of the following data block */
      or_unpack_int (reply, &reply_datasize);

      if (reply_datasize)
	{
	  css_queue_receive_data_buffer (rc, replydata, reply_datasize);
	  error = css_receive_data_from_server (rc, &reply, &size);
	  if (error != NO_ERROR)
	    {
	      return set_server_error (error);
	    }
	  else
	    {
	      error = COMPARE_SIZE_AND_BUFFER (&reply_datasize, size,
					       &replydata, reply);
	    }
	  *replydatasize_ptr = size;
	}
#if defined(HISTO)
      if (Setup)
	{
	  histo_request_finished (request, replysize + *replydatasize_ptr);
	}
#endif /* HISTO */
    }
  return (error);
}

/*
 * net_client_request_3_data -
 *
 * return: error status (0 = success, non-zero = error)
 *
 *   request(in): server request id
 *   argbuf(in): argument buffer (small)
 *   argsize(in): byte size of argbuf
 *   databuf1(in): first data buffer to send
 *   datasize1(in): size of first data buffer
 *   databuf2(in): second data buffer to send
 *   datasize2(in): size of second data buffer
 *   reply0(in): first reply argument buffer (small)
 *   replysize0(in): size of first reply argument buffer
 *   reply1(in): second reply argument buffer
 *   replysize1(in): size of second reply argument buffer
 *   reply2(in): third reply argument buffer
 *   replysize2(in): size of third reply argument buffer
 *
 * Note: This is one of two functions that is called to perform a server
 *    request.  All network interface routines will call either this
 *    functino or net_client_request2.
 */
int
net_client_request_3_data (int request, char *argbuf, int argsize,
			   char *databuf1, int datasize1, char *databuf2,
			   int datasize2, char *reply0, int replysize0,
			   char *reply1, int replysize1, char *reply2,
			   int replysize2)
{
  unsigned int rid;
  int rc;
  int size;
  int p1_size, p2_size, error;
  char *ptr;

  error = rc = 0;

  if (net_Server_name[0] == '\0')
    {
      /* need to have a more appropriate "unexpected disconnect" message */
      rc = ER_NET_SERVER_CRASHED;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, rc, 0);
    }
  else
    {
#if defined(HISTO)
      if (Setup)
	{
	  histo_add_entry (request, argsize + datasize1 + datasize2);
	}
#endif /* HISTO */
      rid = css_send_req_to_server_2_data (net_Server_host,
					   request, argbuf, argsize,
					   databuf1, datasize1,
					   databuf2, datasize2, NULL, 0);
      if (rid == 0)
	{
	  return set_server_error (css_Errno);
	}

      css_queue_receive_data_buffer (rid, reply0, replysize0);
      error = css_receive_data_from_server (rid, &ptr, &size);
      if (error != NO_ERROR)
	{
	  return set_server_error (error);
	}
      else
	{
	  /* Ignore this error status here, since the caller must check it */
	  ptr = or_unpack_int (reply0, &error);
	  ptr = or_unpack_int (ptr, &p1_size);
	  (void) or_unpack_int (ptr, &p2_size);

	  if (p1_size == 0)
	    {
	      return (rc);
	    }

	  css_queue_receive_data_buffer (rid, reply1, p1_size);
	  if (p2_size > 0)
	    {
	      css_queue_receive_data_buffer (rid, reply2, p2_size);
	    }
	  error = css_receive_data_from_server (rid, &ptr, &size);
	  if (error != NO_ERROR)
	    {
	      return set_server_error (error);
	    }
	  else
	    {
	      error =
		COMPARE_SIZE_AND_BUFFER (&replysize1, size, &reply1, ptr);
	    }

	  if (p2_size > 0)
	    {
	      error = css_receive_data_from_server (rid, &ptr, &size);
	      if (error != NO_ERROR)
		{
		  return set_server_error (error);
		}
	      else
		{
		  error =
		    COMPARE_SIZE_AND_BUFFER (&replysize2, size, &reply2, ptr);
		}
	    }
	}
#if defined(HISTO)
      if (Setup)
	{
	  histo_request_finished (request, replysize1 + replysize2);
	}
#endif /* HISTO */
    }
  return (rc);
}

/*
 * net_client_request_with_callback -
 *
 * return: error status
 *
 *   request(in): server request id
 *   argbuf(in): argument buffer (small)
 *   argsize(in): byte size of argbuf
 *   replybuf(in): reply argument buffer (small)
 *   replysize(in): size of reply argument buffer
 *   databuf1(in): first data buffer to send (large)
 *   datasize1(in): size of first data buffer
 *   databuf2(in): second data buffer to send (large)
 *   datasize2(in): size of second data buffer
 *   replydata_ptr1(in): first receive data buffer (large)
 *   replydatasize_ptr1(in): size of first expected reply data
 *   replydata_ptr2(in): second receive data buffer (large)
 *   replydatasize_ptr2(in): size of second expected reply data
 *
 * Note: This is one of the functions that is called to perform a server
 *    request.
 *    This is similar to net_client_request2, but the first
 *    field in the reply argument buffer is a request code which can
 *    cause the client to perform actions such as call methods.  When
 *    the actions are completed, a reply is sent to the server.  Eventually
 *    the server responds to the original request with a request code
 *    that indicates that the request is complete and this routine
 *    returns.
 */
int
net_client_request_with_callback (int request, char *argbuf, int argsize,
				  char *replybuf, int replysize,
				  char *databuf1, int datasize1,
				  char *databuf2, int datasize2,
				  char **replydata_ptr1,
				  int *replydatasize_ptr1,
				  char **replydata_ptr2,
				  int *replydatasize_ptr2)
{
  unsigned int rc;
  int size;
  int reply_datasize1, reply_datasize2, error;
  char *reply, *replydata, *ptr;
  QUERY_SERVER_REQUEST server_request;
  int server_request_num;

  error = 0;
  *replydata_ptr1 = NULL;
  *replydata_ptr2 = NULL;
  *replydatasize_ptr1 = 0;
  *replydatasize_ptr2 = 0;

  if (net_Server_name[0] == '\0')
    {
      /* need to have a more appropriate "unexpected disconnect" message */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NET_SERVER_CRASHED, 0);
      error = -1;
    }
  else
    {
#ifdef HISTO
      if (Setup)
	{
	  histo_add_entry (request, argsize + datasize1 + datasize2);
	}
#endif
      rc = css_send_req_to_server_2_data (net_Server_host, request, argbuf,
					  argsize, databuf1, datasize1,
					  databuf2, datasize2, replybuf,
					  replysize);

      if (rc == 0)
	{
	  return set_server_error (css_Errno);
	}

      do
	{
	  error = css_receive_data_from_server (rc, &reply, &size);
	  if (error != NO_ERROR)
	    {
	      return set_server_error (error);
	    }
	  else
	    {
	      error =
		COMPARE_SIZE_AND_BUFFER (&replysize, size, &replybuf, reply);
	    }

	  ptr = or_unpack_int (reply, &server_request_num);
	  server_request = (QUERY_SERVER_REQUEST) server_request_num;

	  switch (server_request)
	    {
	    case QUERY_END:
	      /* here we assume that the first integer in the reply is the length
	         of the following data block */
	      ptr = or_unpack_int (ptr, &reply_datasize1);
	      ptr = or_unpack_int (ptr, &reply_datasize2);

	      if (reply_datasize1)
		{
		  if ((error == NO_ERROR)
		      && (replydata =
			  (char *) malloc (reply_datasize1)) != NULL)
		    {
		      css_queue_receive_data_buffer (rc, replydata,
						     reply_datasize1);
		      error =
			css_receive_data_from_server (rc, &reply, &size);
		      if (error != NO_ERROR)
			{
			  free_and_init (replydata);
			  return set_server_error (error);
			}
		      else
			{
			  error =
			    COMPARE_SIZE_AND_BUFFER (&reply_datasize1,
						     size, &replydata, reply);
			}
		      *replydata_ptr1 = reply;
		      *replydatasize_ptr1 = size;

		    }
		  else
		    {
		      SET_ALLOC_ERR_AND_READ_EXPECTED_PACKETS (&error, rc, 1);
		    }
		}

	      if (reply_datasize2)
		{
		  if ((error == NO_ERROR)
		      && (replydata = (char *) malloc (DB_PAGESIZE)) != NULL)
		    {
		      css_queue_receive_data_buffer (rc, replydata,
						     reply_datasize2);
		      error =
			css_receive_data_from_server (rc, &reply, &size);
		      if (error != NO_ERROR)
			{
			  free_and_init (replydata);
			  return set_server_error (error);
			}
		      else
			{
			  error =
			    COMPARE_SIZE_AND_BUFFER (&reply_datasize2,
						     size, &replydata, reply);
			}
		      *replydata_ptr2 = reply;
		      *replydatasize_ptr2 = size;

		    }
		  else
		    {
		      SET_ALLOC_ERR_AND_READ_EXPECTED_PACKETS (&error, rc, 1);
		    }
		}
	      break;

	    case METHOD_CALL:
	      {
		char *methoddata;
		int methoddata_size;
		QFILE_LIST_ID *method_call_list_id = (QFILE_LIST_ID *) 0;
		METHOD_SIG_LIST *method_call_sig_list = (METHOD_SIG_LIST *) 0;

		er_clear ();
		error = NO_ERROR;
		/* here we assume that the first integer in the reply is the length
		   of the following data block */
		or_unpack_int (ptr, &methoddata_size);
		methoddata = (char *) malloc (methoddata_size);
		if (methoddata != NULL)
		  {
		    css_queue_receive_data_buffer (rc, methoddata,
						   methoddata_size);
		    error = css_receive_data_from_server (rc, &reply, &size);
		    if (error != NO_ERROR)
		      {
			free_and_init (methoddata);
			return set_server_error (error);
		      }
		    else
		      {
#if defined(CS_MODE)
			bool need_to_reset = false;
			if (method_request_id == 0)
			  {
			    method_request_id = css_return_rid_from_eid (rc);
			    need_to_reset = true;
			  }
#endif /* CS_MODE */
			error = COMPARE_SIZE_AND_BUFFER (&methoddata_size,
							 size, &methoddata,
							 reply);
			ptr = or_unpack_unbound_listid (methoddata,
							(void **)
							&method_call_list_id);
			method_call_list_id->last_pgptr = NULL;
			ptr = or_unpack_method_sig_list (ptr,
							 (void **)
							 &method_call_sig_list);
			free_and_init (methoddata);
			error = method_invoke_for_server (rc,
							  net_Server_host,
							  net_Server_name,
							  method_call_list_id,
							  method_call_sig_list);
			regu_free_listid (method_call_list_id);
			regu_free_method_sig_list (method_call_sig_list);
			if (error != NO_ERROR)
			  {
			    error = er_errid ();
			    if (error == NO_ERROR)
			      {
				error = ER_NET_SERVER_DATA_RECEIVE;
				er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
					error, 0);
			      }
			  }
			else
			  {
			    error = NO_ERROR;
			  }
#if defined(CS_MODE)
			if (need_to_reset == true)
			  {
			    method_request_id = 0;
			    need_to_reset = false;
			  }
#endif /* CS_MODE */
		      }
		  }
		else
		  {
		    SET_ALLOC_ERR_AND_READ_EXPECTED_PACKETS (&error, rc, 1);
		  }

		if (error != NO_ERROR)
		  {
		    return_error_to_server (net_Server_host, rc);
		    method_send_error_to_server (rc, net_Server_host,
						 net_Server_name);
		  }
		css_queue_receive_data_buffer (rc, replybuf, replysize);
	      }
	      break;

	      /*
	       * A code of END_CALLBACK is followed immediately by an
	       * integer returning status from the remote call.  The second
	       * integer represents the return value and must be returned
	       * to the calling function.
	       */
	    case END_CALLBACK:
	      /* The calling function will have to ignore this value in
	       * the reply buffer.
	       */
	      error = NO_ERROR;
	      break;

	    case ASYNC_OBTAIN_USER_INPUT:
	      {
		FILEIO_REMOTE_PROMPT_TYPE prompt_id;
		int length;
		char *promptdata = NULL;
		char user_response_buffer[FILEIO_MAX_USER_RESPONSE_SIZE + 1];
		char *user_response_ptr = user_response_buffer;
		int pr_status = ER_FAILED;
		int pr_len = 0;
		bool response_needed = false;
		bool retry_in = true;
		int x;
		/* The following variables are need to decode the data packet */
		char *display_string;
		char *prompt = NULL;
		char *failure_prompt = NULL;
		char *secondary_prompt = NULL;
		int range_lower, range_higher;
		int reprompt_value;
		int error2;
		char *a_ptr;

		ptr = or_unpack_int (ptr, (int *) &prompt_id);
		ptr = or_unpack_int (ptr, &length);
		promptdata = (char *) malloc (MAX (length,
						   FILEIO_MAX_USER_RESPONSE_SIZE
						   + OR_INT_SIZE));
		if (promptdata != NULL)
		  {
		    css_queue_receive_data_buffer (rc, promptdata, length);
		    error =
		      css_receive_data_from_server (rc, &reply, &length);
		    if (error != NO_ERROR)
		      {
			server_request = END_CALLBACK;
			free_and_init (promptdata);
			return set_server_error (error);
		      }
		    else
		      {
			ptr = or_unpack_string_nocopy (reply, &prompt);
			/*
			 * the following data are used depending on prompt type
			 * but will always be in the input stream
			 */
			ptr = or_unpack_string_nocopy (ptr, &failure_prompt);
			ptr = or_unpack_int (ptr, &range_lower);
			ptr = or_unpack_int (ptr, &range_higher);
			ptr =
			  or_unpack_string_nocopy (ptr, &secondary_prompt);
			ptr = or_unpack_int (ptr, &reprompt_value);
		      }

		    display_string = prompt;

		    memset (user_response_buffer, 0,
			    sizeof (user_response_buffer));

		    while (error == NO_ERROR && retry_in)
		      {
			/* Display prompt, then get user's input. */
			fprintf (stdout, display_string);
			pr_status = ER_FAILED;
			pr_len = 0;
			retry_in = false;

			if (prompt_id != FILEIO_PROMPT_DISPLAY_ONLY)
			  {
			    error2 = scanf ("%s", user_response_ptr);
			    if (error2 > 0)
			      {
				/* basic input int validation before we send it back */
				switch (prompt_id)
				  {
				  case FILEIO_PROMPT_RANGE_TYPE:
				    /* Numeric range checking */
				    x = strtol (user_response_ptr, &a_ptr,
						10);
				    if (a_ptr == user_response_ptr
					|| x < range_lower
					|| x > range_higher)
				      {
					fprintf (stdout, failure_prompt);
					retry_in = true;
				      }
				    else
				      {
					response_needed = true;
					pr_status = NO_ERROR;
				      }
				    break;

				    /*
				     * simply boolean (y, yes, 1, n, no, 0)
				     * validation
				     */
				  case FILEIO_PROMPT_BOOLEAN_TYPE:
				    if ((char_tolower (*user_response_ptr) ==
					 'y')
					|| (*user_response_ptr == '1')
					||
					(intl_mbs_casecmp
					 (user_response_ptr, "yes") == 0))
				      {
					response_needed = true;
					pr_status = NO_ERROR;
					/* convert all affirmate answers into '1' */
					strcpy (user_response_ptr, "1");
				      }
				    else
				      {
					/* assume negative */
					response_needed = true;
					pr_status = NO_ERROR;
					/* convert all negative answers into '0' */
					strcpy (user_response_ptr, "0");
				      }
				    break;

				    /* no validation to do */
				  case FILEIO_PROMPT_STRING_TYPE:
				    response_needed = true;
				    pr_status = NO_ERROR;
				    break;

				    /* Validate initial prompt, then post secondary
				     * prompt
				     */
				  case FILEIO_PROMPT_RANGE_WITH_SECONDARY_STRING_TYPE:
				    /* Numeric range checking on the first promp,
				     * but user's answer we really want is the second
				     * prompt
				     */
				    x = strtol (user_response_ptr, &a_ptr,
						10);
				    if (a_ptr == user_response_ptr
					|| x < range_lower
					|| x > range_higher)
				      {
					fprintf (stdout, failure_prompt);
					retry_in = true;
				      }
				    else if (x == reprompt_value)
				      {
					/* The first answer requires another prompt */
					display_string = secondary_prompt;
					retry_in = true;
					prompt_id = FILEIO_PROMPT_STRING_TYPE;
					/* moving the response buffer ptr forward insures
					 * that both the first response and the second
					 * are included in the buffer. (no delimiter or null
					 * bytes allowed)
					 */
					user_response_ptr +=
					  strlen (user_response_ptr);
				      }
				    else
				      {
					/* This answer was sufficient */
					response_needed = true;
					pr_status = NO_ERROR;
				      }
				    break;

				  default:
				    /* should we treat this as an error? */
				    response_needed = true;
				    pr_status = NO_ERROR;
				  }
			      }
			    else if (error2 == 0)
			      {
				retry_in = true;
			      }
			    else
			      {
				pr_status = ER_FAILED;
			      }
			  }
			else
			  {
			    response_needed = true;
			    pr_status = NO_ERROR;
			  }
		      }		/* while */

		    /* Return the user's answer to the server.
		     * All of the cases above should get to here after looping
		     * or whatever is necessary and provide
		     * indication of local errors (pr_status), as well as provide
		     * a string in user_response.  We send back to the
		     * server an int (status) followed by a string.
		     */
		    /* check for overflow, could be dangerous */
		    pr_len = strlen (user_response_buffer);
		    if (pr_len > FILEIO_MAX_USER_RESPONSE_SIZE)
		      {
			error = ER_NET_DATA_TRUNCATED;
			er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, error,
				0);
			pr_status = ER_FAILED;
		      }

		    if (error)
		      {
			pr_status = ER_FAILED;
		      }

		    /* we already malloced large enough buffer, reuse promptdata */
		    ptr = or_pack_int (promptdata, pr_status);
		    if (response_needed)
		      {
			ptr = or_pack_string_with_length (ptr,
							  user_response_buffer,
							  pr_len);
		      }
		    error2 = net_client_send_data (net_Server_host, rc,
						   promptdata,
						   ptr - promptdata);
		    if (error2 != NO_ERROR)
		      {
			/* the error should have already been generated */
			server_request = END_CALLBACK;
		      }
		    if (error == NO_ERROR && error2 != NO_ERROR)
		      {
			error = error2;
		      }

		    if (error != NO_ERROR)
		      {
			server_request = END_CALLBACK;
			/* Do we need to tell the server about it? */
			return_error_to_server (net_Server_host, rc);
		      }
		  }
		else
		  {
		    /* send back some kind of error to server */
		    SET_ALLOC_ERR_AND_READ_EXPECTED_PACKETS (&error, rc, 1);

		    /* Do we need to tell the server? */
		    server_request = END_CALLBACK;	/* force a stop */
		    return_error_to_server (net_Server_host, rc);
		  }

		/* Clean up and avoid leaks */
		if (promptdata)
		  {
		    free_and_init (promptdata);
		  }
	      }
	      /* expecting another reply */
	      css_queue_receive_data_buffer (rc, replybuf, replysize);

	      break;

	    default:
	      error = ER_NET_SERVER_DATA_RECEIVE;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	      server_request = QUERY_END;
	      break;
	    }
	}
      while (server_request != END_CALLBACK && server_request != QUERY_END);

#if defined(HISTO)
      if (Setup)
	{
	  histo_request_finished (request, replysize + *replydatasize_ptr1 +
				  *replydatasize_ptr2);
	}
#endif /* HISTO */
    }
  return (error);
}

/*
 * net_client_request_buffer -
 *
 * return: error status
 *
 *   rc(in): pre-allocated data buffer
 *   buf_ptr(in): pre-allocated data buffer
 *   expected_size(in): size of data buffer
 *
 * Note: This is used to read an expected network buffer.
 *    Returns non-zero if an error condition was detected.
 *
 *    the other two client request functions should use this, re-write after
 *    1.1 release
 */
static int
net_client_request_buffer (unsigned int rc, char **buf_ptr, int expected_size)
{
  int error;
  int reply_size;
  char *buffer, *reply;

  error = 0;
  *buf_ptr = NULL;

  buffer = (char *) malloc (expected_size);
  if (buffer != NULL)
    {
      css_queue_receive_data_buffer (rc, buffer, expected_size);
      error = css_receive_data_from_server (rc, &reply, &reply_size);
      if (error != NO_ERROR)
	{
	  free_and_init (buffer);
	  return set_server_error (error);
	}
      else
	{
	  error = COMPARE_SIZE_AND_BUFFER (&expected_size, reply_size,
					   &buffer, reply);
	}

      if (error)
	{
	  free_and_init (buffer);
	}
      else
	{
	  *buf_ptr = buffer;
	}
    }
  else
    {
      SET_ALLOC_ERR_AND_READ_EXPECTED_PACKETS (&error, rc, 1);
    }

  return (error);
}

/*
 * net_client_request3 -
 *
 * return: error status
 *
 *   request(in): request id
 *   argbuf(in): request argument buffer
 *   argsize(in): request argument buffer size
 *   replybuf(in): reply argument buffer
 *   replysize(in): reply argument buffer size
 *   databuf(in): send data buffer
 *   datasize(in): send data buffer size
 *   replydata_ptr(in): returned data buffer pointer
 *   replydatasize_ptr(in): returned data buffer size
 *   replydata_ptr2(in): second reply data buffer pointer
 *   replydatasize_ptr2(in): second reply buffer size
 *
 * Note: Like net_client_request2 but expectes two reply data buffers.
 *    Need to generalize this.
 */
int
net_client_request3 (int request, char *argbuf, int argsize,
		     char *replybuf, int replysize, char *databuf,
		     int datasize, char **replydata_ptr,
		     int *replydatasize_ptr, char **replydata_ptr2,
		     int *replydatasize_ptr2)
{
  unsigned int rc;
  int size;
  int reply_datasize, reply_datasize2, error;
  char *reply, *replydata, *replydata2;
  char *ptr;

  error = 0;
  *replydata_ptr = NULL;
  *replydata_ptr2 = NULL;
  *replydatasize_ptr = 0;
  *replydatasize_ptr2 = 0;

  if (net_Server_name[0] == '\0')
    {
      /* need to have a more appropriate "unexpected disconnect" message */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NET_SERVER_CRASHED, 0);
      error = -1;
    }
  else
    {
#if defined(HISTO)
      if (Setup)
	{
	  histo_add_entry (request, argsize + datasize);
	}
#endif /* HISTO */
      rc =
	css_send_req_to_server (net_Server_host, request, argbuf, argsize,
				databuf, datasize, replybuf, replysize);
      if (rc == 0)
	{
	  return set_server_error (css_Errno);
	}

      error = css_receive_data_from_server (rc, &reply, &size);
      if (error != NO_ERROR)
	{
	  return set_server_error (error);
	}
      else
	{
	  error =
	    COMPARE_SIZE_AND_BUFFER (&replysize, size, &replybuf, reply);
	}

      /* here we assume that the first two integers in the reply are the
         lengths of the following data blocks */
      ptr = or_unpack_int (reply, &reply_datasize);
      (void) or_unpack_int (ptr, &reply_datasize2);

      replydata = NULL;
      replydata2 = NULL;

      if (reply_datasize)
	{
	  error = net_client_request_buffer (rc, &replydata, reply_datasize);
	}

      if ((error == NO_ERROR) && reply_datasize2)
	{
	  error =
	    net_client_request_buffer (rc, &replydata2, reply_datasize2);
	}

      if (error)
	{
	  if (replydata != NULL)
	    {
	      free_and_init (replydata);
	      replydata = NULL;
	    }
	  if (replydata2)
	    {
	      free_and_init (replydata2);
	      replydata2 = NULL;
	    }
	}

      *replydata_ptr = replydata;
      *replydatasize_ptr = reply_datasize;
      *replydata_ptr2 = replydata2;
      *replydatasize_ptr2 = reply_datasize2;
#if defined(HISTO)
      if (Setup)
	{
	  histo_request_finished (request, replysize + *replydatasize_ptr +
				  *replydatasize_ptr2);
	}
#endif /* HISTO */
    }
  return (error);
}

/*
 * net_client_request_recv_copyarea -
 *
 * return:
 *
 *   request(in):
 *   argbuf(in):
 *   argsize(in):
 *   replybuf(in):
 *   replysize(in):
 *   reply_copy_area(in):
 *
 * Note:
 */
int
net_client_request_recv_copyarea (int request,
				  char *argbuf, int argsize,
				  char *replybuf,
				  int replysize,
				  LC_COPYAREA ** reply_copy_area)
{
  unsigned int rc;
  int size;
  int error;
  char *reply;
  int content_size;
  char *content_ptr;
  int num_objs;
  char *packed_desc;
  int packed_desc_size;

  error = 0;
  if (net_Server_name[0] == '\0')
    {
      /* need to have a more appropriate "unexpected disconnect" message */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NET_SERVER_CRASHED, 0);
      error = -1;
    }
  else
    {
#if defined(HISTO)
      if (Setup)
	{
	  histo_add_entry (request, argsize);
	}
#endif /* HISTO */

      rc =
	css_send_req_to_server (net_Server_host, request, argbuf, argsize,
				NULL, 0, replybuf, replysize);
      if (rc == 0)
	{
	  return set_server_error (css_Errno);
	}

      /*
       * Receive replybuf
       */

      error = css_receive_data_from_server (rc, &reply, &size);
      if (error != NO_ERROR)
	{
	  return set_server_error (error);
	}
      else
	{
	  error =
	    COMPARE_SIZE_AND_BUFFER (&replysize, size, &replybuf, reply);
	}

      /*
       * Receive copyarea
       * Here assume that the next two integers in the reply are the lengths of
       * the copy descriptor and content descriptor
       */

      reply = or_unpack_int (reply, &num_objs);
      reply = or_unpack_int (reply, &packed_desc_size);
      reply = or_unpack_int (reply, &content_size);

      if (packed_desc_size != 0 || content_size != 0)
	{
	  if (error == NO_ERROR)
	    {
	      *reply_copy_area = locator_recv_allocate_copyarea (num_objs,
						&packed_desc,
						packed_desc_size,
								 &content_ptr,
								 content_size);
	      if (*reply_copy_area != NULL)
		{
		  if (packed_desc_size > 0)
		    {
		      css_queue_receive_data_buffer (rc, packed_desc,
						     packed_desc_size);
		      error = css_receive_data_from_server (rc, &packed_desc,
							    &size);
		      if (error != NO_ERROR)
			{
			  free_and_init (packed_desc);
			  return set_server_error (error);
			}
		      else
			{
			  locator_unpack_copy_area_descriptor (num_objs,
							       *reply_copy_area,
							       packed_desc);
			  free_and_init (packed_desc);
			}
		    }

		  if (content_size > 0)
		    {
		      error = css_queue_receive_data_buffer (rc, content_ptr,
							     content_size);
		      if (error != NO_ERROR)
			{
			  SET_ALLOC_ERR_AND_READ_EXPECTED_PACKETS (&error, rc,
								   1);
			}
		      else
			{
			  error =
			    css_receive_data_from_server (rc, &content_ptr,
							  &size);
			}
		      if (error != NO_ERROR)
			{
			  return set_server_error (error);
			}
		    }

		}
	      else
		{
		  int num_packets = 0;

		  if (packed_desc_size > 0)
		    {
		      num_packets++;
		    }
		  if (content_size > 0)
		    {
		      num_packets++;
		    }
		  SET_ALLOC_ERR_AND_READ_EXPECTED_PACKETS (&error, rc,
							   num_packets);
		}
	    }
	  else
	    {
	      int num_packets = 0;

	      if (packed_desc_size > 0)
		{
		  num_packets++;
		}
	      if (content_size > 0)
		{
		  num_packets++;
		}
	      SET_ALLOC_ERR_AND_READ_EXPECTED_PACKETS (&error, rc,
						       num_packets);

	    }
	}
#if defined(HISTO)
      if (Setup)
	{
	  histo_request_finished (request, replysize + content_size +
				  packed_desc_size);
	}
#endif /* HISTO */
    }
  return (error);
}

/*
 * net_client_request_recv_logarea -
 *
 * return:
 *
 *   request(in):
 *   argbuf(in):
 *   argsize(in):
 *   replybuf(in):
 *   replysize(in):
 *   reply_log_area(in):
 *
 * Note:
 */
int
net_client_request_recv_logarea (int request, char *argbuf, int argsize,
				 char *replybuf, int replysize,
				 LOG_COPY ** reply_log_area)
{
  unsigned int rc;
  int size;
  int error;
  char *reply;
  int content_size;
  char *content_ptr;
  int num_records;
  char *packed_desc;
  int packed_desc_size;

  error = 0;
  if (net_Server_name[0] == '\0')
    {
      /* need to have a more appropriate "unexpected disconnect" message */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NET_SERVER_CRASHED, 0);
      error = -1;
      return error;
    }

#if defined(HISTO)
  if (Setup)
    {
      histo_add_entry (request, argsize);
    }
#endif /* HISTO */

  rc =
    css_send_req_to_server (net_Server_host, request, argbuf, argsize,
			    NULL, 0, replybuf, replysize);
  if (rc == 0)
    {
      return set_server_error (css_Errno);
    }

  /*
   * Receive replybuf
   */
  error = css_receive_data_from_server (rc, &reply, &size);
  if (error != NO_ERROR)
    {
      return set_server_error (error);
    }
  else
    {
      error = COMPARE_SIZE_AND_BUFFER (&replysize, size, &replybuf, reply);
    }

  /*
   * Receive logarea
   * Here assume that the next two integers in the reply are the lengths of
   * the copy descriptor and content descriptor
   */

  reply = or_unpack_int (reply, &num_records);
  reply = or_unpack_int (reply, &packed_desc_size);
  reply = or_unpack_int (reply, &content_size);

  /* allocate the logarea */
  *reply_log_area = NULL;
  if (packed_desc_size != 0 || content_size != 0)
    {
      if (error == NO_ERROR)
	{
	  *reply_log_area = log_copy_area_malloc_recv (num_records,
						       &packed_desc,
						       packed_desc_size,
						       &content_ptr,
						       content_size);
	  if (*reply_log_area != NULL)
	    {
	      if (packed_desc_size > 0)
		{
		  css_queue_receive_data_buffer (rc, packed_desc,
						 packed_desc_size);
		  error = css_receive_data_from_server (rc, &packed_desc,
							&size);
		  if (error != NO_ERROR)
		    {
		      free_and_init (packed_desc);
		      return set_server_error (error);
		    }
		  log_unpack_descriptors (num_records, *reply_log_area,
					  packed_desc);
		  free_and_init (packed_desc);
		}

	      if (content_size > 0)
		{
		  css_queue_receive_data_buffer (rc, content_ptr,
						 content_size);
		  error = css_receive_data_from_server (rc, &content_ptr,
							&size);
		  if (error != NO_ERROR)
		    {
		      return set_server_error (error);
		    }
		}
	    }
	  else
	    {
	      int num_packets = 0;

	      if (packed_desc_size > 0)
		{
		  num_packets++;
		}
	      if (content_size > 0)
		{
		  num_packets++;
		}
	      SET_ALLOC_ERR_AND_READ_EXPECTED_PACKETS (&error, rc,
						       num_packets);
	    }
	}
      else
	{
	  int num_packets = 0;

	  if (packed_desc_size > 0)
	    {
	      num_packets++;
	    }
	  if (content_size > 0)
	    {
	      num_packets++;
	    }
	  SET_ALLOC_ERR_AND_READ_EXPECTED_PACKETS (&error, rc, num_packets);

	}
    }

#if defined(HISTO)
  if (Setup)
    {
      histo_request_finished (request, replysize + content_size +
			      packed_desc_size);
    }
#endif /* HISTO */

  return (error);
}

/*
 * net_client_request_2recv_nomalloc_buffer_and_malloc_copyarea -
 *
 * return:
 *
 *   request(in):
 *   argbuf(in):
 *   argsize(in):
 *   replybuf(in):
 *   replysize(in):
 *   databuf(in):
 *   datasize(in):
 *   recvbuffer(in):
 *   recvbuffer_size(in):
 *   reply_copy_area(in):
 *   eid(in):
 *
 * Note:
 */
int
net_client_request_2recv_nomalloc_buffer_and_malloc_copyarea (int request,
							      char
							      *argbuf,
							      int argsize,
							      char
							      *replybuf,
							      int
							      replysize,
							      char
							      *databuf,
							      int
							      datasize,
							      char
							      *recvbuffer,
							      int
							      recvbuffer_size,
							      LC_COPYAREA
							      **
							      reply_copy_area,
							      int *eid)
{
  unsigned int rc;
  int size;
  int p_size, error;
  char *reply;
  int content_size;
  char *content_ptr;
  int num_objs;
  char *packed_desc;
  int packed_desc_size;

  error = 0;
  if (net_Server_name[0] == '\0')
    {
      /* need to have a more appropriate "unexpected disconnect" message */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NET_SERVER_CRASHED, 0);
      error = -1;
      return error;
    }
#if defined(HISTO)
  if (Setup)
    {
      histo_add_entry (request, argsize + datasize);
    }
#endif /* HISTO */

  rc =
    css_send_req_to_server (net_Server_host, request, argbuf, argsize,
			    databuf, datasize, replybuf, replysize);
  if (rc == 0)
    {
      return set_server_error (css_Errno);
    }

  *eid = rc;

  /*
   * Receive replybuf
   */

  error = css_receive_data_from_server (rc, &reply, &size);
  if (error != NO_ERROR)
    {
      return set_server_error (error);
    }
  else
    {
      error = COMPARE_SIZE_AND_BUFFER (&replysize, size, &replybuf, reply);
    }

  /*
   * Receive recvbuffer
   * Here we assume that the first integer in the reply is the length
   * of the following data block
   */

  replybuf = or_unpack_int (replybuf, &p_size);

  if (recvbuffer_size < p_size)
    {
      /* too big for what we allocated */
      error = set_server_error (CANT_ALLOC_BUFFER);
    }

  if (p_size > 0)
    {
      if (error)
	{
	  /* maintain error status.  If we continued without checking
	   * this, error could become NO_ERROR and caller would never
	   * know.
	   */
	  css_receive_data_from_server (rc, &reply, &size);
	}
      else
	{
	  css_queue_receive_data_buffer (rc, recvbuffer, p_size);
	  error = css_receive_data_from_server (rc, &reply, &size);
	  if (error != NO_ERROR)
	    {
	      return set_server_error (error);
	    }
	  else
	    {
	      /* we expect that the sizes won't match, but we must be
	       * sure that the we can accomodate the data in our buffer.
	       * So, don't use COMPARE_SIZE_AND_BUFFER() here.
	       */
	      if (recvbuffer_size < size)
		{
		  error = ER_NET_DATASIZE_MISMATCH;
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 2,
			  recvbuffer_size, size);
		}
	      else
		{
		  recvbuffer_size = size;
		}

	      if (reply != recvbuffer)
		{
		  error = ER_NET_UNUSED_BUFFER;
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
		}
	    }
	}
    }

  /*
   * Receive copyarea
   * Here assume that the next two integers in the reply are the lengths of
   * the copy descriptor and content descriptor
   */

  replybuf = or_unpack_int (replybuf, &num_objs);
  replybuf = or_unpack_int (replybuf, &packed_desc_size);
  replybuf = or_unpack_int (replybuf, &content_size);

  /* allocate the copyarea */
  *reply_copy_area = NULL;
  if (packed_desc_size != 0 || content_size != 0)
    {
      if (error == NO_ERROR)
	{
	  *reply_copy_area = locator_recv_allocate_copyarea (num_objs,
							     &packed_desc,
					    packed_desc_size,
							     &content_ptr,
							     content_size);
	  if (*reply_copy_area != NULL)
	    {
	      if (packed_desc_size > 0)
		{
		  css_queue_receive_data_buffer (rc, packed_desc,
						 packed_desc_size);
		  error = css_receive_data_from_server (rc, &packed_desc,
							&size);
		  if (error != NO_ERROR)
		    {
		      free_and_init (packed_desc);
		      return set_server_error (error);
		    }
		  else
		    {
		      locator_unpack_copy_area_descriptor (num_objs,
							   *reply_copy_area,
							   packed_desc);
		      free_and_init (packed_desc);
		    }
		}

	      if (content_size > 0)
		{
		  css_queue_receive_data_buffer (rc, content_ptr,
						 content_size);
		  error = css_receive_data_from_server (rc, &content_ptr,
							&size);
		  if (error != NO_ERROR)
		    {
		      return set_server_error (error);
		    }
		}
	    }
	  else
	    {
	      int num_packets = 0;

	      if (packed_desc_size > 0)
		{
		  num_packets++;
		}
	      if (content_size > 0)
		{
		  num_packets++;
		}
	      SET_ALLOC_ERR_AND_READ_EXPECTED_PACKETS (&error, rc,
						       num_packets);
	    }
	}
      else
	{
	  int num_packets = 0;

	  if (packed_desc_size > 0)
	    {
	      num_packets++;
	    }
	  if (content_size > 0)
	    {
	      num_packets++;
	    }
	  SET_ALLOC_ERR_AND_READ_EXPECTED_PACKETS (&error, rc, num_packets);

	}
    }

#if defined(HISTO)
  if (Setup)
    {
      histo_request_finished (request, replysize + recvbuffer_size +
			      content_size + packed_desc_size);
    }
#endif /* HISTO */
  return (error);
}

/*
 * net_client_recv_nomalloc_buffer_and_malloc_copyarea -
 *
 * return:
 *
 *   request(in):
 *   replybuf(in):
 *   replysize(in):
 *   recvbuffer(in):
 *   recvbuffer_size(in):
 *   reply_copy_area(in):
 *   rc(in):
 *
 * Note:
 */
int
net_client_recv_nomalloc_buffer_and_malloc_copyarea (int request,
						     char *replybuf,
						     int replysize,
						     char *recvbuffer,
						     int recvbuffer_size,
						     LC_COPYAREA **
						     reply_copy_area, int rc)
{
  int size;
  int error, p_size;
  char *reply;
  int content_size;
  char *content_ptr;
  int num_objs;
  char *packed_desc;
  int packed_desc_size;

  error = 0;
  if (net_Server_name[0] == '\0')
    {
      /* need to have a more appropriate "unexpected disconnect" message */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NET_SERVER_CRASHED, 0);
      error = -1;
      return error;
    }
#if defined(HISTO)
  if (Setup)
    {
      histo_add_entry (request, 0);
    }
#endif /* HISTO */

  /*
   * Receive replybuf
   */

  css_queue_receive_data_buffer (rc, replybuf, replysize);
  error = css_receive_data_from_server (rc, &reply, &size);
  if (error != NO_ERROR)
    {
      return set_server_error (error);
    }
  else
    {
      error = COMPARE_SIZE_AND_BUFFER (&replysize, size, &replybuf, reply);
    }

  /*
   * Receive recvbuffer
   * Here we assume that the first integer in the reply is the length
   * of the following data block
   */

  replybuf = or_unpack_int (replybuf, &p_size);

  if (recvbuffer_size < p_size)
    {
      error = set_server_error (CANT_ALLOC_BUFFER);
    }

  if (p_size > 0)
    {
      if (error)
	{
	  /* maintain error status.  If we continued without checking
	   * this, error could become NO_ERROR and caller would never
	   * know.
	   */
	  css_receive_data_from_server (rc, &reply, &size);
	}
      else
	{
	  css_queue_receive_data_buffer (rc, recvbuffer, p_size);
	  error = css_receive_data_from_server (rc, &reply, &size);
	  if (error != NO_ERROR)
	    {
	      return set_server_error (error);
	    }
	  else
	    {
	      if (recvbuffer_size < size)
		{
		  /* we expect that the sizes won't match, but we must be
		   * sure that the we can accomodate the data in our buffer.
		   * So, don't use COMPARE_SIZE_AND_BUFFER() here.
		   */
		  error = ER_NET_DATASIZE_MISMATCH;
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 2,
			  recvbuffer_size, size);
		}
	      else
		{
		  recvbuffer_size = size;
		}

	      if (reply != recvbuffer)
		{
		  error = ER_NET_UNUSED_BUFFER;
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
		}
	    }
	}
    }

  /*
   * Receive copyarea
   * Here assume that the next two integers in the reply are the lengths of
   * the copy descriptor and content descriptor
   */

  replybuf = or_unpack_int (replybuf, &num_objs);
  replybuf = or_unpack_int (replybuf, &packed_desc_size);
  replybuf = or_unpack_int (replybuf, &content_size);

  /* allocate the copyarea */
  *reply_copy_area = NULL;
  if (packed_desc_size != 0 || content_size != 0)
    {
      if (error == NO_ERROR)
	{
	  *reply_copy_area = locator_recv_allocate_copyarea (num_objs,
							     &packed_desc,
					    packed_desc_size,
							     &content_ptr,
							     content_size);
	  if (*reply_copy_area != NULL)
	    {
	      if (packed_desc_size > 0)
		{
		  css_queue_receive_data_buffer (rc, packed_desc,
						 packed_desc_size);
		  error = css_receive_data_from_server (rc, &packed_desc,
							&size);
		  if (error != NO_ERROR)
		    {
		      free_and_init (packed_desc);
		      return set_server_error (error);
		    }
		  else
		    {
		      locator_unpack_copy_area_descriptor (num_objs,
							   *reply_copy_area,
							   packed_desc);
		      free_and_init (packed_desc);
		    }
		}

	      if (content_size > 0)
		{
		  css_queue_receive_data_buffer (rc, content_ptr,
						 content_size);
		  error = css_receive_data_from_server (rc, &content_ptr,
							&size);
		  if (error != NO_ERROR)
		    {
		      return set_server_error (error);
		    }
		}

	    }
	  else
	    {
	      int num_packets = 0;

	      if (packed_desc_size > 0)
		{
		  num_packets++;
		}
	      if (content_size > 0)
		{
		  num_packets++;
		}
	      SET_ALLOC_ERR_AND_READ_EXPECTED_PACKETS (&error, rc,
						       num_packets);
	    }
	}
      else
	{
	  int num_packets = 0;

	  if (packed_desc_size > 0)
	    {
	      num_packets++;
	    }
	  if (content_size > 0)
	    {
	      num_packets++;
	    }
	  SET_ALLOC_ERR_AND_READ_EXPECTED_PACKETS (&error, rc, num_packets);
	}
    }

#if defined(HISTO)
  if (Setup)
    {
      histo_request_finished (request, replysize + recvbuffer_size +
			      content_size + packed_desc_size);
    }
#endif /* HISTO */

  return (error);
}

/*
 * net_client_request_3recv_malloc_buffer_and_malloc_copyarea -
 *
 * return:
 *
 *   request(in):
 *   argbuf(in):
 *   argsize(in):
 *   replybuf(in):
 *   replysize(in):
 *   databuf(in):
 *   datasize(in):
 *   recvbuffer(in):
 *   recvbuffer_size(in):
 *   reply_copy_area(in):
 *
 * Note:
 */
int
net_client_request_3recv_malloc_buffer_and_malloc_copyarea (int request,
							    char *argbuf,
							    int argsize,
							    char
							    *replybuf,
							    int replysize,
							    char *databuf,
							    int datasize,
							    char
							    **recvbuffer,
							    int
							    *recvbuffer_size,
							    LC_COPYAREA **
							    reply_copy_area)
{
  unsigned int rc;
  int size;
  int p_size, error;
  char *reply;
  int content_size;
  char *content_ptr;
  int num_objs;
  char *packed_desc;
  int packed_desc_size;

  error = 0;
  *recvbuffer = NULL;
  *recvbuffer_size = 0;

  if (net_Server_name[0] == '\0')
    {
      /* need to have a more appropriate "unexpected disconnect" message */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NET_SERVER_CRASHED, 0);
      error = -1;
      return error;
    }
#if defined(HISTO)
  if (Setup)
    {
      histo_add_entry (request, argsize + datasize);
    }
#endif /* HISTO */

  rc = css_send_req_to_server (net_Server_host, request, argbuf, argsize,
			    databuf, datasize, replybuf, replysize);
  if (rc == 0)
    {
      return set_server_error (css_Errno);
    }

  /*
   * Receive replybuf
   */
  error = css_receive_data_from_server (rc, &reply, &size);
  if (error != NO_ERROR)
    {
      return set_server_error (error);
    }
  else
    {
      error = COMPARE_SIZE_AND_BUFFER (&replysize, size, &replybuf, reply);
    }

  /*
   * Receive recvbuffer
   * Here we assume that the first integer in the reply is the length
   * of the following data block
   */

  replybuf = or_unpack_int (replybuf, &p_size);

  if (p_size > 0)
    {
      *recvbuffer_size = p_size;

      if ((error == NO_ERROR)
	  && (*recvbuffer = (char *) malloc (p_size)) != NULL)
	{
	  css_queue_receive_data_buffer (rc, *recvbuffer, p_size);
	  error = css_receive_data_from_server (rc, &reply, &size);
	  if (error != NO_ERROR)
	    {
	      free_and_init (*recvbuffer);
	      return set_server_error (error);
	    }
	  else
	    {
	      error = COMPARE_SIZE_AND_BUFFER (recvbuffer_size, size,
					       recvbuffer, reply);
	    }
	}
      else
	{
	  *recvbuffer_size = 0;
	  SET_ALLOC_ERR_AND_READ_EXPECTED_PACKETS (&error, rc, 1);
	}
    }

  /*
   * Receive copyarea
   * Here assume that the next two integers in the reply are the lengths of
   * the copy descriptor and content descriptor
   */

  replybuf = or_unpack_int (replybuf, &num_objs);
  replybuf = or_unpack_int (replybuf, &packed_desc_size);
  replybuf = or_unpack_int (replybuf, &content_size);

  /* allocate the copyarea */
  *reply_copy_area = NULL;
  if (packed_desc_size != 0 || content_size != 0)
    {
      if ((error == NO_ERROR)
	  && ((*reply_copy_area = locator_recv_allocate_copyarea (num_objs,
								  &packed_desc,
								  packed_desc_size,
								  &content_ptr,
								  content_size))
	      != NULL))
	{
	  if (packed_desc_size > 0)
	    {
	      css_queue_receive_data_buffer (rc, packed_desc,
					     packed_desc_size);
	      error = css_receive_data_from_server (rc, &packed_desc, &size);
	      if (error != NO_ERROR)
		{
		  free_and_init (packed_desc);
		  return set_server_error (error);
		}
	      else
		{
		  locator_unpack_copy_area_descriptor (num_objs,
						       *reply_copy_area,
						       packed_desc);
		  free_and_init (packed_desc);
		}
	    }

	  if (content_size > 0)
	    {
	      css_queue_receive_data_buffer (rc, content_ptr, content_size);
	      error = css_receive_data_from_server (rc, &content_ptr, &size);
	      if (error != NO_ERROR)
		{
		  return set_server_error (error);
		}
	    }
	}
      else
	{
	  int num_packets = 0;

	  if (packed_desc_size > 0)
	    {
	      num_packets++;
	    }
	  if (content_size > 0)
	    {
	      num_packets++;
	    }
	  SET_ALLOC_ERR_AND_READ_EXPECTED_PACKETS (&error, rc, num_packets);

	}
    }


#if defined(HISTO)
  if (Setup)
    {
      histo_request_finished (request, replysize + *recvbuffer_size +
			      content_size + packed_desc_size);
    }
#endif /* HISTO */

  return error;
}

/*
 * net_client_request_recv_stream -
 *
 * return:
 *
 *   request(in):
 *   argbuf(in):
 *   argsize(in):
 *   replybuf(in):
 *   replybuf_size(in):
 *   databuf(in):
 *   datasize(in):
 *   outfp(in):
 *
 * Note:
 */
int
net_client_request_recv_stream (int request, char *argbuf, int argsize,
				char *replybuf, int replybuf_size,
				char *databuf, int datasize, FILE * outfp)
{
  unsigned int rc;
  int size;
  int error;
  char *reply;
  char *send_argbuffer;
  int send_argsize;
  char *recv_replybuf;
  int recv_replybuf_size;
  char reply_streamdata[100];
  int reply_streamdata_size = 100;
  int file_size;

  error = NO_ERROR;

  send_argsize = argsize + OR_INT_SIZE;
  recv_replybuf_size = replybuf_size + OR_INT_SIZE;

  send_argbuffer = (char *) malloc (send_argsize);
  if (send_argbuffer == NULL)
    {
      error = ER_NET_CANT_ALLOC_BUFFER;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
      return error;
    }

  or_pack_int (send_argbuffer, reply_streamdata_size);

  if (argsize > 0)
    {
      memcpy (send_argbuffer + OR_INT_SIZE, argbuf, argsize);
    }

  recv_replybuf = (char *) malloc (recv_replybuf_size);
  if (recv_replybuf == NULL)
    {
      free_and_init (send_argbuffer);
      error = ER_NET_CANT_ALLOC_BUFFER;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
      return error;
    }

  if (net_Server_name[0] == '\0')
    {
      /* need to have a more appropriate "unexpected disconnect" message */
      free_and_init (send_argbuffer);
      free_and_init (recv_replybuf);
      error = ER_NET_SERVER_CRASHED;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
      return error;
    }

#if defined(HISTO)
  if (Setup)
    {
      histo_add_entry (request, send_argsize + datasize);
    }
#endif /* HISTO */

  rc = css_send_req_to_server (net_Server_host, request, send_argbuffer,
			       send_argsize, databuf, datasize,
			       recv_replybuf, recv_replybuf_size);
  if (rc == 0)
    {
      error = set_server_error (css_Errno);
      goto end;
    }
  else
    {
      error = css_receive_data_from_server (rc, &reply, &size);
      if (error != NO_ERROR)
	{
	  error = set_server_error (error);
	  goto end;
	}
      else
	{
	  error = COMPARE_SIZE_AND_BUFFER (&recv_replybuf_size, size,
					   &recv_replybuf, reply);
	}

      /* Get total size of file to transfered */
      or_unpack_int (recv_replybuf, &file_size);

      if (replybuf)
	{
	  memcpy (replybuf, recv_replybuf + OR_INT_SIZE,
		  recv_replybuf_size - OR_INT_SIZE);
	}

#if defined(HISTO)
      if (Setup)
	{
	  histo_request_finished (request, recv_replybuf_size + file_size);
	}
#endif /* HISTO */

      while (file_size > 0)
	{
	  css_queue_receive_data_buffer (rc, reply_streamdata,
					 reply_streamdata_size);
	  error = css_receive_data_from_server (rc, &reply, &size);
	  if (error != NO_ERROR)
	    {
	      error = set_server_error (error);
	      goto end;
	    }
	  else
	    {
	      if (reply != reply_streamdata)
		{
		  error = ER_NET_UNUSED_BUFFER;
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
		  break;
		}
	      if (size > reply_streamdata_size)
		{
		  error = ER_NET_DATASIZE_MISMATCH;
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			  error, 2, reply_streamdata_size, size);
		  break;
		}
	      file_size -= size;
	      fwrite (reply_streamdata, 1, size, outfp);
	    }
	}
    }

end:

  free_and_init (send_argbuffer);
  free_and_init (recv_replybuf);

  return (error);
}

#if defined(CUBRID_DEBUG)
/*
 * add_client_name_to_request -
 *
 * return: error code
 *
 *   request(in):
 *   request_size(in):
 *
 * Note: Attempts to make contact with the server over the network.
 *
 */
static void
add_client_name_to_request (char *request, unsigned int *request_size)
{
  /* Setup for sending client name with ping message since tracing is
     enabled */
  char clientName[SIM_MAX_CLIENT_NAME];
  int i, clientName_size;

  memset (clientName, 0, SIM_MAX_CLIENT_NAME);
  SimCfgGetClientName (clientName);

  clientName_size = or_packed_string_length (clientName);
  if (clientName_size + *request_size < NET_PINGBUF_SIZE)
    {
      /* move the original request over to accommodate the client name */
      for (i = (*request_size - 1); i >= 0; i--)
	{
	  request[i + clientName_size] = request[i];
	}
      *request_size = clientName_size + *request_size;
      or_pack_string (request, clientName);
    }
}
#endif /* CUBRID_DEBUG */

/*
 * net_client_ping_server -
 *
 * return:
 *
 * Note:
 */
int
net_client_ping_server (void)
{
  char *sr_rel_str = NULL, *sr_hand_shake = NULL, *ptr;
  int error = NO_ERROR;
  unsigned int rc, reply_size, request_size;
  OR_ALIGNED_BUF (NET_PINGBUF_SIZE) a_request;
  char *request = OR_ALIGNED_BUF_START (a_request);
  char reply[NET_PINGBUF_SIZE], *reply_ptr, *client_version;
  int server_capabilities;
  int size;

  client_version = (char *) rel_release_string ();
  or_pack_string (request, client_version);
  request_size = or_packed_string_length (client_version);

  reply_size = (or_packed_string_length (client_version) +
		or_packed_string_length (NET_CLIENT_SERVER_HAND_SHAKE) +
		OR_INT_SIZE);

  if (reply_size > NET_PINGBUF_SIZE)
    {
      error = ER_NET_CANT_ALLOC_BUFFER;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
      return error;
    }

  reply_ptr = reply;

  if (net_Server_host[0] == '\0' || net_Server_name[0] == '\0')
    {
      error = ER_NET_NO_SERVER_HOST;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
      return error;
    }

#if defined(CUBRID_DEBUG)
  if (SIM_CLIENT_ON)
    {
      add_client_name_to_request (request, &request_size);
    }
#endif /* CUBRID_DEBUG */

  rc = css_send_request_to_server_with_buffer (net_Server_host,
					       SERVER_PING_WITH_HANDSHAKE,
					       request, request_size, reply,
					       NET_PINGBUF_SIZE);
  if (!rc)
    {
      error = ER_NET_CANT_CONNECT_SERVER;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 2, net_Server_name,
	      net_Server_host);
      return error;
    }

  error = css_receive_data_from_server (rc, &reply_ptr, &size);
  if (error)
    {
      error = ER_NET_SERVER_DATA_RECEIVE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
      return error;
    }

  if (reply != reply_ptr)
    {
      error = ER_NET_UNUSED_BUFFER;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
      return error;
    }

  ptr = or_unpack_string_nocopy (reply, &sr_rel_str);
  ptr = or_unpack_string_nocopy (ptr, &sr_hand_shake);
  ptr = or_unpack_int (ptr, &server_capabilities);

  if (!net_check_client_server_compatibility (sr_rel_str, client_version))
    {
      error = ER_NET_DIFFERENT_RELEASE;
      goto cleanup;
    }

  record_server_capabilities (server_capabilities);

cleanup:

  return error;
}

/*
 * net_client_shutdown_server -
 *
 * return:
 *
 * Note: Sends the server shutdown request to the server.
 *    This is not used and I'm not sure if it even works.
 *    Need to be careful that we don't expect a reply here.
 */
void
net_client_shutdown_server (void)
{
  css_send_request_to_server (net_Server_host, SERVER_SHUTDOWN, NULL, 0);
}

/*
 * net_client_init -
 *
 * return: error code
 *
 *   dbname(in): server name
 *   hostname(in): server host name
 *
 * Note: This is called during startup to initialize the client side
 *    communications. It sets up CSS and verifies connection with the server.
 */
int
net_client_init (const char *dbname, const char *hostname)
{
  int error = NO_ERROR;

  /* don't really need to do this every time but bruce says its ok -
     we probably need to guarentee that a css_terminate is always
     called before this */
  error = css_client_init (PRM_TCP_PORT_ID, NULL, dbname, hostname);
  if (error != NO_ERROR)
    {
      goto end;
    }

  /* since urgent_message_handler() doesn't do anything yet, just
     use the default handler provided by css which writes things
     to the system console */

  /* set our host/server names for further css communication */
  if ((hostname != NULL) && ((strlen (hostname) + 1) < MAX_SERVER_HOST_NAME))
    {
      strcpy (net_Server_host, hostname);
      if ((dbname != NULL) && ((strlen (dbname) + 1) < MAX_SERVER_NAME))
	{
	  strcpy (net_Server_name, dbname);
	}
      else
	{
	  error = ER_NET_INVALID_SERVER_NAME;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, dbname);
	}
    }
  else
    {
      error = ER_NET_INVALID_HOST_NAME;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, hostname);
    }

  if (error == NO_ERROR)
    {
      /* ping to validate availability */
      error = net_client_ping_server ();
    }

  /* On error, flush any state that may have been initialized by css.
   * This is important for the PC's since we must shutdown Winsock
   * after it has been opened by css_client_init.
   */
end:
  if (error)
    {
      css_terminate ();
    }

  return (error);
}

/*
 * net_cleanup_client_queues -
 *
 * return:
 *
 * Note:
 */
void
net_cleanup_client_queues (void)
{
  if (net_Server_host[0] != '\0' && net_Server_name[0] != '\0')
    {
      css_cleanup_client_queues (net_Server_host);
    }
}

/*
 * net_client_final -
 *
 * return: error cod
 *
 * Note: This is called during shutdown to close the communication interface.
 */
int
net_client_final (void)
{
  css_terminate ();
  return (NO_ERROR);
}

/*
 * net_client_send_data -
 *
 * return:
 *
 *   host(in):
 *   rc(in):
 *   databuf(in):
 *   datasize(in):
 *
 * Note: Send a data buffer to the server.
 */
int
net_client_send_data (char *host, unsigned int rc,
		      char *databuf, int datasize)
{
  int error;

  if (databuf != NULL)
    {
      error = css_send_data_to_server (host, rc, databuf, datasize);
      if (error != NO_ERROR)
	{
	  return set_server_error (error);
	}
    }

  return NO_ERROR;
}

/*
 * net_client_receive_action -
 *
 * return:
 *
 *   rc(in):
 *   action(in):
 *
 * Note:
 */
int
net_client_receive_action (int rc, int *action)
{
  int size;
  int error;
  char *reply;
  int replysize = OR_INT_SIZE;

  error = NO_ERROR;
  if (net_Server_name[0] == '\0')
    {
      /* need to have a more appropriate "unexpected disconnect" message */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NET_SERVER_CRASHED, 0);
      error = ER_NET_SERVER_CRASHED;
    }
  else
    {
      error = css_receive_data_from_server (rc, &reply, &size);
      if (error != NO_ERROR)
	{
	  return set_server_error (error);
	}

      if (size != replysize)
	{
	  error = ER_NET_DATASIZE_MISMATCH;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 2, replysize,
		  size);
	  replysize = size;
	  return set_server_error (error);
	}
      or_unpack_int (reply, action);
      free_and_init (reply);
    }

  return (error);
}
