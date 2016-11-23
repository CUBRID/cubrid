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
#include "environment_variable.h"
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
#include "log_writer.h"

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

#define COMPARE_AND_FREE_BUFFER(queued, reply) \
  do { \
    if (((reply) != NULL) && ((reply) != (queued))) { \
      free_and_init ((reply)); \
    } \
    (reply) = NULL; \
  } while (0)

/*
 * Add instrumentation to the client side to get histogram of network
 * requests
 */

struct net_request_buffer
{
  const char *name;
  int request_count;
  int total_size_sent;
  int total_size_received;
  int elapsed_time;
};
static struct net_request_buffer net_Req_buffer[NET_SERVER_REQUEST_END];

static int net_Histo_setup = 0;
static int net_Histo_setup_mnt = 0;
static int net_Histo_call_count = 0;
static INT64 net_Histo_last_call_time = 0;
static INT64 net_Histo_total_server_time = 0;

#if defined(CS_MODE)
unsigned short method_request_id;
#endif /* CS_MODE */

/* Contains the name of the current sever host machine.  */
static char net_Server_host[MAXHOSTNAMELEN + 1] = "";

/* Contains the name of the current server name. */
static char net_Server_name[DB_MAX_IDENTIFIER_LENGTH + 1] = "";

static void return_error_to_server (char *host, unsigned int eid);
static int client_capabilities (void);
static int check_server_capabilities (int server_cap, int client_type, int rel_compare,
				      REL_COMPATIBILITY * compatibility, const char *server_host, int opt_cap);
static void set_alloc_err_and_read_expected_packets (int *err, int rc, int num_packets, const char *file,
						     const int line);
static int compare_size_and_buffer (int *replysize, int size, char **replybuf, char *buf, const char *file,
				    const int line);
static int net_client_request_internal (int request, char *argbuf, int argsize, char *replybuf, int replysize,
					char *databuf, int datasize, char *replydata, int replydatasize);
#if defined(ENABLE_UNUSED_FUNCTION)
static int net_client_request_buffer (unsigned int rc, char **buf_ptr, int expected_size);
#endif
static int set_server_error (int error);

static void net_histo_setup_names (void);
static void net_histo_add_entry (int request, int data_sent);
static void net_histo_request_finished (int request, int data_received);

static const char *get_capability_string (int cap, int cap_type);

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
    case REQUEST_REFUSED:
      assert (er_errid () != NO_ERROR);
      server_error = er_errid ();
      break;
    case SERVER_ABORTED:
      /* server error may not be set when SERVER_ABORTED is given. server may send ABORT_TYPE for some cases and the
       * server error will not be delivered for the case. */
      server_error = er_errid ();
      /* those errors are generated by the net_server_request() so that do not fall to server crash handling */
      switch (server_error)
	{
	case ER_DB_NO_MODIFICATIONS:
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, server_error, 0);
	  return server_error;
	case ER_AU_DBA_ONLY:
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, server_error, 1, "");
	  return server_error;
	}
      /* no break; fall through */
    default:
      server_error = ER_NET_SERVER_CRASHED;
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, server_error, 0);
      break;
    }

  er_log_debug (ARG_FILE_LINE, "set_server_error(%d) server_error %d\n", error, server_error);

  db_Connect_status = DB_CONNECTION_STATUS_NOT_CONNECTED;

  if (net_Server_name[0] != '\0')
    {
      net_Server_name[0] = '\0';
      net_Server_host[0] = '\0';
      boot_server_die_or_changed ();
    }

  return server_error;
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
  void *area;
  char buffer[1024];
  int length = 1024;

  area = er_get_area_error (buffer, &length);
  if (area != NULL)
    {
      css_send_error_to_server (host, eid, (char *) area, length);
    }
}

/*
 * client_capabilities -
 *
 * return:
 */
static int
client_capabilities (void)
{
  int capabilities = 0;

  capabilities |= NET_CAP_INTERRUPT_ENABLED;
  if (db_Disable_modifications > 0)
    {
      capabilities |= NET_CAP_UPDATE_DISABLED;
    }

  if (db_need_ignore_repl_delay ())
    {
      capabilities |= NET_CAP_HA_IGNORE_REPL_DELAY;
    }

  return capabilities;
}

/*
 * get_capability_string - for the purpose of error logging,
 *                         it translate cap into a word
 *
 * return:
 */
static const char *
get_capability_string (int cap, int cap_type)
{
  switch (cap_type)
    {
    case NET_CAP_INTERRUPT_ENABLED:
      if (cap & NET_CAP_INTERRUPT_ENABLED)
	{
	  return "enabled";
	}
      return "disabled";
    case NET_CAP_UPDATE_DISABLED:
      if (cap & NET_CAP_UPDATE_DISABLED)
	{
	  return "read only";
	}
      return "read/write";
    default:
      return "-";
    }
}

/*
 * check_server_capabilities -
 *
 * return:
 */
static int
check_server_capabilities (int server_cap, int client_type, int rel_compare, REL_COMPATIBILITY * compatibility,
			   const char *server_host, int opt_cap)
{
  int client_cap;

  assert (compatibility != NULL);

  client_cap = client_capabilities ();
  client_cap |= opt_cap;

  /* interrupt-ability should be same */
  if ((client_cap ^ server_cap) & NET_CAP_INTERRUPT_ENABLED)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NET_HS_INCOMPAT_INTERRUPTIBILITY, 3, net_Server_host,
	      get_capability_string (client_cap, NET_CAP_INTERRUPT_ENABLED),
	      get_capability_string (server_cap, NET_CAP_INTERRUPT_ENABLED));
      server_cap ^= NET_CAP_INTERRUPT_ENABLED;
    }

  /* replica only client should check whether the server is replica */
  if (BOOT_REPLICA_ONLY_BROKER_CLIENT_TYPE (client_type))
    {
      if (~server_cap & NET_CAP_HA_REPLICA)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NET_HS_HA_REPLICA_ONLY, 1, net_Server_host);
	  server_cap ^= NET_CAP_HA_REPLICA;
	}
    }
  else
    {
      /* update-ability should be same */
      if ((client_cap ^ server_cap) & NET_CAP_UPDATE_DISABLED)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NET_HS_INCOMPAT_RW_MODE, 3, net_Server_host,
		  get_capability_string (client_cap, NET_CAP_UPDATE_DISABLED),
		  get_capability_string (server_cap, NET_CAP_UPDATE_DISABLED));
	  server_cap ^= NET_CAP_UPDATE_DISABLED;

	  db_set_host_status (net_Server_host, DB_HS_MISMATCHED_RW_MODE);
	}
    }

  /* 
   * check HA replication delay
   * if client_cap is on, it checks the server delay status
   * else, it ignores the delay status.
   */
  if (client_cap & NET_CAP_HA_REPL_DELAY & server_cap)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NET_HS_HA_REPL_DELAY, 1, net_Server_host);
      server_cap ^= NET_CAP_HA_REPL_DELAY;

      db_set_host_status (net_Server_host, DB_HS_HA_DELAYED);
    }

  /* network protocol compatibility */
  if (*compatibility == REL_NOT_COMPATIBLE)
    {
      if (rel_compare < 0 && ((server_cap & NET_CAP_BACKWARD_COMPATIBLE) || (client_cap & NET_CAP_FORWARD_COMPATIBLE)))
	{
	  /* 
	   * The client is older than the server but the server has a backward
	   * compatible capability or the client has a forward compatible
	   * capability.
	   */
	  *compatibility = REL_FORWARD_COMPATIBLE;
	}
      if (rel_compare > 0 && ((server_cap & NET_CAP_FORWARD_COMPATIBLE) || (client_cap & NET_CAP_BACKWARD_COMPATIBLE)))
	{
	  /* 
	   * The client is newer than the server but the server has a forward
	   * compatible capability or the client has a backward compatible
	   * capability.
	   */
	  *compatibility = REL_BACKWARD_COMPATIBLE;
	}
    }

  /* remote connection capability */
  if ((server_cap & NET_CAP_REMOTE_DISABLED)
      && !BOOT_IS_ALLOWED_CLIENT_TYPE_IN_MT_MODE (server_host, boot_Host_name, client_type))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NET_HS_REMOTE_DISABLED, 1, net_Server_host);
      server_cap ^= NET_CAP_REMOTE_DISABLED;
    }

  return server_cap;
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
set_alloc_err_and_read_expected_packets (int *err, int rc, int num_packets, const char *file, const int line)
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
compare_size_and_buffer (int *replysize, int size, char **replybuf, char *buf, const char *file, const int line)
{
  int err = NO_ERROR;

  if (size <= 0)
    {
      return NO_ERROR;
    }

  if (size != *replysize)
    {
      err = ER_NET_DATASIZE_MISMATCH;
      er_set (ER_ERROR_SEVERITY, file, line, err, 2, *replysize, size);
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
 * net_histo_setup_names -
 *
 * return:
 *
 * Note:
 */
static void
net_histo_setup_names (void)
{
  unsigned int i;

  for (i = 0; i < DIM (net_Req_buffer); i++)
    {
      net_Req_buffer[i].name = "";
      net_Req_buffer[i].request_count = 0;
      net_Req_buffer[i].total_size_sent = 0;
      net_Req_buffer[i].total_size_received = 0;
      net_Req_buffer[i].elapsed_time = 0;
    }

  net_Req_buffer[NET_SERVER_BO_INIT_SERVER].name = "NET_SERVER_BO_INIT_SERVER";
  net_Req_buffer[NET_SERVER_BO_REGISTER_CLIENT].name = "NET_SERVER_BO_REGISTER_CLIENT";
  net_Req_buffer[NET_SERVER_BO_UNREGISTER_CLIENT].name = "NET_SERVER_BO_UNREGISTER_CLIENT";
  net_Req_buffer[NET_SERVER_BO_BACKUP].name = "NET_SERVER_BO_BACKUP";
  net_Req_buffer[NET_SERVER_BO_ADD_VOLEXT].name = "NET_SERVER_BO_ADD_VOLEXT";
  net_Req_buffer[NET_SERVER_BO_CHECK_DBCONSISTENCY].name = "NET_SERVER_BO_CHECK_DBCONSISTENCY";
  net_Req_buffer[NET_SERVER_BO_FIND_NPERM_VOLS].name = "NET_SERVER_BO_FIND_NPERM_VOLS";
  net_Req_buffer[NET_SERVER_BO_FIND_NTEMP_VOLS].name = "NET_SERVER_BO_FIND_NTEMP_VOLS";
  net_Req_buffer[NET_SERVER_BO_FIND_LAST_PERM].name = "NET_SERVER_BO_FIND_LAST_PERM";
  net_Req_buffer[NET_SERVER_BO_FIND_LAST_TEMP].name = "NET_SERVER_BO_FIND_LAST_TEMP";
  net_Req_buffer[NET_SERVER_BO_CHANGE_HA_MODE].name = "NET_SERVER_BO_CHANGE_HA_MODE";
  net_Req_buffer[NET_SERVER_BO_NOTIFY_HA_LOG_APPLIER_STATE].name = "NET_SERVER_BO_NOTIFY_HA_LOG_APPLIER_STATE";
  net_Req_buffer[NET_SERVER_BO_COMPACT_DB].name = "NET_SERVER_BO_COMPACT_DB";
  net_Req_buffer[NET_SERVER_BO_HEAP_COMPACT].name = "NET_SERVER_BO_HEAP_COMPACT";
  net_Req_buffer[NET_SERVER_BO_COMPACT_DB_START].name = "NET_SERVER_BO_COMPACT_DB_START";
  net_Req_buffer[NET_SERVER_BO_COMPACT_DB_STOP].name = "NET_SERVER_BO_COMPACT_DB_STOP";
  net_Req_buffer[NET_SERVER_BO_GET_LOCALES_INFO].name = "NET_SERVER_BO_GET_LOCALES_INFO";

  net_Req_buffer[NET_SERVER_TM_SERVER_COMMIT].name = "NET_SERVER_TM_SERVER_COMMIT";
  net_Req_buffer[NET_SERVER_TM_SERVER_ABORT].name = "NET_SERVER_TM_SERVER_ABORT";
  net_Req_buffer[NET_SERVER_TM_SERVER_START_TOPOP].name = "NET_SERVER_TM_SERVER_START_TOPOP";
  net_Req_buffer[NET_SERVER_TM_SERVER_END_TOPOP].name = "NET_SERVER_TM_SERVER_END_TOPOP";
  net_Req_buffer[NET_SERVER_TM_SERVER_SAVEPOINT].name = "NET_SERVER_TM_SERVER_SAVEPOINT";
  net_Req_buffer[NET_SERVER_TM_SERVER_PARTIAL_ABORT].name = "NET_SERVER_TM_SERVER_PARTIAL_ABORT";
  net_Req_buffer[NET_SERVER_TM_SERVER_HAS_UPDATED].name = "NET_SERVER_TM_SERVER_HAS_UPDATED";
  net_Req_buffer[NET_SERVER_TM_SERVER_ISACTIVE_AND_HAS_UPDATED].name = "NET_SERVER_TM_SERVER_ISACTIVE_AND_HAS_UPDATED";
  net_Req_buffer[NET_SERVER_TM_ISBLOCKED].name = "NET_SERVER_TM_ISBLOCKED";
  net_Req_buffer[NET_SERVER_TM_WAIT_SERVER_ACTIVE_TRANS].name = "NET_SERVER_TM_WAIT_SERVER_ACTIVE_TRANS";
  net_Req_buffer[NET_SERVER_TM_SERVER_GET_GTRINFO].name = "NET_SERVER_TM_SERVER_GET_GTRINFO";
  net_Req_buffer[NET_SERVER_TM_SERVER_SET_GTRINFO].name = "NET_SERVER_TM_SERVER_SET_GTRINFO";
  net_Req_buffer[NET_SERVER_TM_SERVER_2PC_START].name = "NET_SERVER_TM_SERVER_2PC_START";
  net_Req_buffer[NET_SERVER_TM_SERVER_2PC_PREPARE].name = "NET_SERVER_TM_SERVER_2PC_PREPARE";
  net_Req_buffer[NET_SERVER_TM_SERVER_2PC_RECOVERY_PREPARED].name = "NET_SERVER_TM_SERVER_2PC_RECOVERY_PREPARED";
  net_Req_buffer[NET_SERVER_TM_SERVER_2PC_ATTACH_GT].name = "NET_SERVER_TM_SERVER_2PC_ATTACH_GT";
  net_Req_buffer[NET_SERVER_TM_SERVER_2PC_PREPARE_GT].name = "NET_SERVER_TM_SERVER_2PC_PREPARE_GT";
  net_Req_buffer[NET_SERVER_TM_LOCAL_TRANSACTION_ID].name = "NET_SERVER_TM_LOCAL_TRANSACTION_ID";
  net_Req_buffer[NET_SERVER_LOG_CHECKPOINT].name = "NET_SERVER_LOG_CHECKPOINT";

  net_Req_buffer[NET_SERVER_LC_FETCH].name = "NET_SERVER_LC_FETCH";
  net_Req_buffer[NET_SERVER_LC_FETCHALL].name = "NET_SERVER_LC_FETCHALL";
  net_Req_buffer[NET_SERVER_LC_FETCH_LOCKSET].name = "NET_SERVER_LC_FETCH_LOCKSET";
  net_Req_buffer[NET_SERVER_LC_FETCH_ALLREFS_LOCKSET].name = "NET_SERVER_LC_FETCH_ALLREFS_LOCKSET";
  net_Req_buffer[NET_SERVER_LC_GET_CLASS].name = "NET_SERVER_LC_GET_CLASS";
  net_Req_buffer[NET_SERVER_LC_FIND_CLASSOID].name = "NET_SERVER_LC_FIND_CLASSOID";
  net_Req_buffer[NET_SERVER_LC_DOESEXIST].name = "NET_SERVER_LC_DOESEXIST";
  net_Req_buffer[NET_SERVER_LC_FORCE].name = "NET_SERVER_LC_FORCE";
  net_Req_buffer[NET_SERVER_LC_RESERVE_CLASSNAME].name = "NET_SERVER_LC_RESERVE_CLASSNAME";
  net_Req_buffer[NET_SERVER_LC_RESERVE_CLASSNAME_GET_OID].name = "NET_SERVER_LC_RESERVE_CLASSNAME_GET_OID";
  net_Req_buffer[NET_SERVER_LC_DELETE_CLASSNAME].name = "NET_SERVER_LC_DELETE_CLASSNAME";
  net_Req_buffer[NET_SERVER_LC_RENAME_CLASSNAME].name = "NET_SERVER_LC_RENAME_CLASSNAME";
  net_Req_buffer[NET_SERVER_LC_ASSIGN_OID].name = "NET_SERVER_LC_ASSIGN_OID";
  net_Req_buffer[NET_SERVER_LC_NOTIFY_ISOLATION_INCONS].name = "NET_SERVER_LC_NOTIFY_ISOLATION_INCONS";
  net_Req_buffer[NET_SERVER_LC_FIND_LOCKHINT_CLASSOIDS].name = "NET_SERVER_LC_FIND_LOCKHINT_CLASSOIDS";
  net_Req_buffer[NET_SERVER_LC_FETCH_LOCKHINT_CLASSES].name = "NET_SERVER_LC_FETCH_LOCKHINT_CLASSES";
  net_Req_buffer[NET_SERVER_LC_ASSIGN_OID_BATCH].name = "NET_SERVER_LC_ASSIGN_OID_BATCH";
  net_Req_buffer[NET_SERVER_LC_CHECK_FK_VALIDITY].name = "NET_SERVER_LC_CHECK_FK_VALIDITY";
  net_Req_buffer[NET_SERVER_LC_REM_CLASS_FROM_INDEX].name = "NET_SERVER_LC_REM_CLASS_FROM_INDEX";

  net_Req_buffer[NET_SERVER_HEAP_CREATE].name = "NET_SERVER_HEAP_CREATE";
  net_Req_buffer[NET_SERVER_HEAP_DESTROY].name = "NET_SERVER_HEAP_DESTROY";
  net_Req_buffer[NET_SERVER_HEAP_DESTROY_WHEN_NEW].name = "NET_SERVER_HEAP_DESTROY_WHEN_NEW";
  net_Req_buffer[NET_SERVER_HEAP_GET_CLASS_NOBJS_AND_NPAGES].name = "NET_SERVER_HEAP_GET_CLASS_NOBJS_AND_NPAGES";
  net_Req_buffer[NET_SERVER_HEAP_HAS_INSTANCE].name = "NET_SERVER_HEAP_HAS_INSTANCE";
  net_Req_buffer[NET_SERVER_HEAP_RECLAIM_ADDRESSES].name = "NET_SERVER_HEAP_RECLAIM_ADDRESSES";

  net_Req_buffer[NET_SERVER_LOG_RESET_WAIT_MSECS].name = "NET_SERVER_LOG_RESET_WAIT_MSECS";
  net_Req_buffer[NET_SERVER_LOG_RESET_ISOLATION].name = "NET_SERVER_LOG_RESET_ISOLATION";
  net_Req_buffer[NET_SERVER_LOG_SET_INTERRUPT].name = "NET_SERVER_LOG_SET_INTERRUPT";
  net_Req_buffer[NET_SERVER_LOG_DUMP_STAT].name = "NET_SERVER_LOG_DUMP_STAT";
  net_Req_buffer[NET_SERVER_LOG_GETPACK_TRANTB].name = "NET_SERVER_LOG_GETPACK_TRANTB";
  net_Req_buffer[NET_SERVER_LOG_DUMP_TRANTB].name = "NET_SERVER_LOG_DUMP_TRANTB";
  net_Req_buffer[NET_SERVER_LOG_SET_SUPPRESS_REPL_ON_TRANSACTION].name =
    "NET_SERVER_LOG_SET_SUPPRESS_REPL_ON_TRANSACTION";

  net_Req_buffer[NET_SERVER_LOG_FIND_LOB_LOCATOR].name = "NET_SERVER_LOG_FIND_LOB_LOCATOR";
  net_Req_buffer[NET_SERVER_LOG_ADD_LOB_LOCATOR].name = "NET_SERVER_LOG_ADD_LOB_LOCATOR";
  net_Req_buffer[NET_SERVER_LOG_CHANGE_STATE_OF_LOCATOR].name = "NET_SERVER_LOG_CHANGE_STATE_OF_LOCATOR";
  net_Req_buffer[NET_SERVER_LOG_DROP_LOB_LOCATOR].name = "NET_SERVER_LOG_DROP_LOB_LOCATOR";

  net_Req_buffer[NET_SERVER_LK_DUMP].name = "NET_SERVER_LK_DUMP";

  net_Req_buffer[NET_SERVER_BTREE_ADDINDEX].name = "NET_SERVER_BTREE_ADDINDEX";
  net_Req_buffer[NET_SERVER_BTREE_DELINDEX].name = "NET_SERVER_BTREE_DELINDEX";
  net_Req_buffer[NET_SERVER_BTREE_LOADINDEX].name = "NET_SERVER_BTREE_LOADINDEX";
  net_Req_buffer[NET_SERVER_BTREE_FIND_UNIQUE].name = "NET_SERVER_BTREE_FIND_UNIQUE";
  net_Req_buffer[NET_SERVER_BTREE_CLASS_UNIQUE_TEST].name = "NET_SERVER_BTREE_CLASS_UNIQUE_TEST";
  net_Req_buffer[NET_SERVER_BTREE_GET_STATISTICS].name = "NET_SERVER_BTREE_GET_STATISTICS";
  net_Req_buffer[NET_SERVER_BTREE_GET_KEY_TYPE].name = "NET_SERVER_BTREE_GET_KEY_TYPE";

  net_Req_buffer[NET_SERVER_DISK_TOTALPGS].name = "NET_SERVER_DISK_TOTALPGS";
  net_Req_buffer[NET_SERVER_DISK_FREEPGS].name = "NET_SERVER_DISK_FREEPGS";
  net_Req_buffer[NET_SERVER_DISK_REMARKS].name = "NET_SERVER_DISK_REMARKS";
  net_Req_buffer[NET_SERVER_DISK_GET_PURPOSE_AND_SPACE_INFO].name = "NET_SERVER_DISK_GET_PURPOSE_AND_SPACE_INFO";
  net_Req_buffer[NET_SERVER_DISK_VLABEL].name = "NET_SERVER_DISK_VLABEL";
  net_Req_buffer[NET_SERVER_DISK_IS_EXIST].name = "NET_SERVER_DISK_IS_EXIST";

  net_Req_buffer[NET_SERVER_QST_GET_STATISTICS].name = "NET_SERVER_QST_GET_STATISTICS";
  net_Req_buffer[NET_SERVER_QST_UPDATE_STATISTICS].name = "NET_SERVER_QST_UPDATE_STATISTICS";
  net_Req_buffer[NET_SERVER_QST_UPDATE_ALL_STATISTICS].name = "NET_SERVER_QST_UPDATE_ALL_STATISTICS";

  net_Req_buffer[NET_SERVER_QM_QUERY_PREPARE].name = "NET_SERVER_QM_QUERY_PREPARE";
  net_Req_buffer[NET_SERVER_QM_QUERY_EXECUTE].name = "NET_SERVER_QM_QUERY_EXECUTE";
  net_Req_buffer[NET_SERVER_QM_QUERY_PREPARE_AND_EXECUTE].name = "NET_SERVER_QM_QUERY_PREPARE_AND_EXECUTE";
  net_Req_buffer[NET_SERVER_QM_QUERY_END].name = "NET_SERVER_QM_QUERY_END";
  net_Req_buffer[NET_SERVER_QM_QUERY_DROP_ALL_PLANS].name = "NET_SERVER_QM_QUERY_DROP_ALL_PLANS";
  net_Req_buffer[NET_SERVER_QM_QUERY_DUMP_PLANS].name = "NET_SERVER_QM_QUERY_DUMP_PLANS";
  net_Req_buffer[NET_SERVER_QM_QUERY_DUMP_CACHE].name = "NET_SERVER_QM_QUERY_DUMP_CACHE";

  net_Req_buffer[NET_SERVER_LS_GET_LIST_FILE_PAGE].name = "NET_SERVER_LS_GET_LIST_FILE_PAGE";

  net_Req_buffer[NET_SERVER_MNT_SERVER_START_STATS].name = "NET_SERVER_MNT_SERVER_START_STATS";
  net_Req_buffer[NET_SERVER_MNT_SERVER_STOP_STATS].name = "NET_SERVER_MNT_SERVER_STOP_STATS";
  net_Req_buffer[NET_SERVER_MNT_SERVER_COPY_STATS].name = "NET_SERVER_MNT_SERVER_COPY_STATS";

  net_Req_buffer[NET_SERVER_CT_CHECK_REP_DIR].name = "NET_SERVER_CT_CHECK_REP_DIR";

  net_Req_buffer[NET_SERVER_CSS_KILL_TRANSACTION].name = "NET_SERVER_CSS_KILL_TRANSACTION";
  net_Req_buffer[NET_SERVER_CSS_DUMP_CS_STAT].name = "NET_SERVER_CSS_DUMP_CS_STAT";

  net_Req_buffer[NET_SERVER_QPROC_GET_SYS_TIMESTAMP].name = "NET_SERVER_QPROC_GET_SYS_TIMESTAMP";
  net_Req_buffer[NET_SERVER_QPROC_GET_CURRENT_VALUE].name = "NET_SERVER_QPROC_GET_CURRENT_VALUE";
  net_Req_buffer[NET_SERVER_QPROC_GET_NEXT_VALUE].name = "NET_SERVER_QPROC_GET_NEXT_VALUE";
  net_Req_buffer[NET_SERVER_QPROC_GET_SERVER_INFO].name = "NET_SERVER_QPROC_GET_SERVER_INFO";
  net_Req_buffer[NET_SERVER_SERIAL_DECACHE].name = "NET_SERVER_SERIAL_DECACHE";

  net_Req_buffer[NET_SERVER_PRM_SET_PARAMETERS].name = "NET_SERVER_PRM_SET_PARAMETERS";
  net_Req_buffer[NET_SERVER_PRM_GET_PARAMETERS].name = "NET_SERVER_PRM_GET_PARAMETERS";
  net_Req_buffer[NET_SERVER_PRM_GET_PARAMETERS].name = "NET_SERVER_PRM_GET_FORCE_PARAMETERS";
  net_Req_buffer[NET_SERVER_PRM_DUMP_PARAMETERS].name = "NET_SERVER_PRM_DUMP_PARAMETERS";

  net_Req_buffer[NET_SERVER_JSP_GET_SERVER_PORT].name = "NET_SERVER_JSP_GET_SERVER_PORT";

  net_Req_buffer[NET_SERVER_REPL_INFO].name = "NET_SERVER_REPL_INFO";
  net_Req_buffer[NET_SERVER_REPL_LOG_GET_APPEND_LSA].name = "NET_SERVER_REPL_LOG_GET_APPEND_LSA";
  net_Req_buffer[NET_SERVER_REPL_BTREE_FIND_UNIQUE].name = "NET_SERVER_REPL_BTREE_FIND_UNIQUE";

  net_Req_buffer[NET_SERVER_LOGWR_GET_LOG_PAGES].name = "NET_SERVER_LOGWR_GET_LOG_PAGES";

  net_Req_buffer[NET_SERVER_ES_CREATE_FILE].name = "NET_SERVER_ES_CREATE_FILE";
  net_Req_buffer[NET_SERVER_ES_WRITE_FILE].name = "NET_SERVER_ES_WRITE_FILE";
  net_Req_buffer[NET_SERVER_ES_READ_FILE].name = "NET_SERVER_ES_READ_FILE";
  net_Req_buffer[NET_SERVER_ES_DELETE_FILE].name = "NET_SERVER_ES_DELETE_FILE";
  net_Req_buffer[NET_SERVER_ES_COPY_FILE].name = "NET_SERVER_ES_COPY_FILE";
  net_Req_buffer[NET_SERVER_ES_RENAME_FILE].name = "NET_SERVER_ES_RENAME_FILE";
  net_Req_buffer[NET_SERVER_ES_GET_FILE_SIZE].name = "NET_SERVER_ES_GET_FILE_SIZE";

  net_Req_buffer[NET_SERVER_TEST_PERFORMANCE].name = "NET_SERVER_TEST_PERFORMANCE";

  net_Req_buffer[NET_SERVER_SHUTDOWN].name = "NET_SERVER_SHUTDOWN";

  net_Req_buffer[NET_SERVER_LC_UPGRADE_INSTANCES_DOMAIN].name = "NET_SERVER_LC_UPGRADE_INSTANCES_DOMAIN";

  net_Req_buffer[NET_SERVER_SES_CHECK_SESSION].name = "NET_SERVER_SES_CHECK_SESSION";
  net_Req_buffer[NET_SERVER_SES_END_SESSION].name = "NET_SERVER_END_SESSION";
  net_Req_buffer[NET_SERVER_SES_SET_ROW_COUNT].name = "NET_SERVER_SES_SET_ROW_COUNT";
  net_Req_buffer[NET_SERVER_SES_GET_ROW_COUNT].name = "NET_SERVER_GET_ROW_COUNT";
  net_Req_buffer[NET_SERVER_SES_GET_LAST_INSERT_ID].name = "NET_SERVER_SES_GET_LAST_INSERT_ID";
  net_Req_buffer[NET_SERVER_SES_RESET_CUR_INSERT_ID].name = "NET_SERVER_SES_RESET_CUR_INSERT_ID";
  net_Req_buffer[NET_SERVER_SES_CREATE_PREPARED_STATEMENT].name = "NET_SERVER_SES_CREATE_PREPARED_STATEMENT";
  net_Req_buffer[NET_SERVER_SES_GET_PREPARED_STATEMENT].name = "NET_SERVER_SES_GET_PREPARED_STATEMENT";
  net_Req_buffer[NET_SERVER_SES_DELETE_PREPARED_STATEMENT].name = "NET_SERVER_SES_DELETE_PREPARED_STATEMENT";
  net_Req_buffer[NET_SERVER_SES_SET_SESSION_VARIABLES].name = "NET_SERVER_SES_SET_SESSION_VARIABLES";
  net_Req_buffer[NET_SERVER_SES_GET_SESSION_VARIABLE].name = "NET_SERVER_SES_GET_SESSION_VARIABLE";
  net_Req_buffer[NET_SERVER_SES_DROP_SESSION_VARIABLES].name = "NET_SERVER_SES_DROP_SESSION_VARIABLES";
  net_Req_buffer[NET_SERVER_BTREE_FIND_MULTI_UNIQUES].name = "NET_SERVER_FIND_MULTI_UNIQUES";
  net_Req_buffer[NET_SERVER_VACUUM].name = "NET_SERVER_VACUUM";
  net_Req_buffer[NET_SERVER_GET_MVCC_SNAPSHOT].name = "NET_SERVER_GET_MVCC_SNAPSHOT";
  net_Req_buffer[NET_SERVER_LOCK_RR].name = "NET_SERVER_LOCK_RR";
}

/*
 * net_histo_clear -
 *
 * return:
 *
 * NOTE:
 */
void
net_histo_clear (void)
{
  unsigned int i;

  if (net_Histo_setup_mnt)
    {
      perfmon_reset_stats ();
    }

  net_Histo_call_count = 0;
  net_Histo_last_call_time = 0;
  net_Histo_total_server_time = 0;
  for (i = 0; i < DIM (net_Req_buffer); i++)
    {
      net_Req_buffer[i].request_count = 0;
      net_Req_buffer[i].total_size_sent = 0;
      net_Req_buffer[i].total_size_received = 0;
      net_Req_buffer[i].elapsed_time = 0;
    }
}

/*
 * net_histo_print -
 *
 * return:
 *
 * Note:
 */
int
net_histo_print (FILE * stream)
{
  unsigned int i;
  int found = 0, total_requests = 0, total_size_sent = 0;
  int total_size_received = 0;
  float server_time, total_server_time = 0;
  float avg_response_time, avg_client_time;
  int err = NO_ERROR;

  if (stream == NULL)
    {
      stream = stdout;
    }

  fprintf (stream, "\nHistogram of client requests:\n");
  fprintf (stream, "%-31s %6s  %10s %10s , %10s \n", "Name", "Rcount", "Sent size", "Recv size", "Server time");
  for (i = 0; i < DIM (net_Req_buffer); i++)
    {
      if (net_Req_buffer[i].request_count)
	{
	  found = 1;
	  server_time = ((float) net_Req_buffer[i].elapsed_time / 1000000 / (float) (net_Req_buffer[i].request_count));
	  fprintf (stream, "%-29s %6d X %10d+%10d b, %10.6f s\n", net_Req_buffer[i].name,
		   net_Req_buffer[i].request_count, net_Req_buffer[i].total_size_sent,
		   net_Req_buffer[i].total_size_received, server_time);
	  total_requests += net_Req_buffer[i].request_count;
	  total_size_sent += net_Req_buffer[i].total_size_sent;
	  total_size_received += net_Req_buffer[i].total_size_received;
	  total_server_time += (server_time * net_Req_buffer[i].request_count);
	}
    }
  if (!found)
    {
      fprintf (stream, " No server requests made\n");
    }
  else
    {
      fprintf (stream, "-------------------------------------------------------------" "--------------\n");
      fprintf (stream, "Totals:                       %6d X %10d+%10d b  " "%10.6f s\n", total_requests,
	       total_size_sent, total_size_received, total_server_time);
      avg_response_time = total_server_time / total_requests;
      avg_client_time = 0.0;
      fprintf (stream,
	       "\n Average server response time = %6.6f secs \n"
	       " Average time between client requests = %6.6f secs \n", avg_response_time, avg_client_time);
    }
  if (net_Histo_setup_mnt)
    {
      err = perfmon_print_stats (stream);
    }
  return err;
}

/*
 * net_histo_print_global_stats -
 *
 * return:
 *
 * Note:
 */
int
net_histo_print_global_stats (FILE * stream, bool cumulative, const char *substr)
{
  int err = NO_ERROR;

  if (net_Histo_setup_mnt)
    {
      err = perfmon_print_global_stats (stream, cumulative, substr);
    }
  return err;
}

/*
 * net_histo_start -
 *
 * return: NO_ERROR or ER_FAILED
 *
 * Note:
 */
int
net_histo_start (bool for_all_trans)
{
  if (net_Histo_setup == 0)
    {
      net_histo_clear ();
      net_histo_setup_names ();
      net_Histo_setup = 1;
    }

  if (net_Histo_setup_mnt == 0)
    {
      if (perfmon_start_stats (for_all_trans) != NO_ERROR)
	{
	  return ER_FAILED;
	}
      net_Histo_setup_mnt = 1;
    }

  return NO_ERROR;
}

/*
 * net_histo_stop -
 *
 * return: NO_ERROR or ER_FAILED
 *
 * Note:
 */
int
net_histo_stop (void)
{
  int err = NO_ERROR;

  if (net_Histo_setup_mnt == 1)
    {
      err = perfmon_stop_stats ();
      net_Histo_setup_mnt = 0;
    }

  if (net_Histo_setup == 1)
    {
      net_Histo_setup = 0;
    }

  return err;
}

/*
 * net_histo_add_entry -
 *
 * return:
 *
 *   request(in):
 *   data_sent(in):
 *
 * Note:
 */
static void
net_histo_add_entry (int request, int data_sent)
{
#if !defined(WINDOWS)
  struct timeval tp;
#endif /* WINDOWS */

  if (request <= NET_SERVER_REQUEST_START || request >= NET_SERVER_REQUEST_END)
    {
      return;
    }

  net_Req_buffer[request].request_count++;
  net_Req_buffer[request].total_size_sent += data_sent;
#if !defined(WINDOWS)
  if (gettimeofday (&tp, NULL) == 0)
    {
      net_Histo_last_call_time = tp.tv_sec * 1000000LL + tp.tv_usec;
    }
#endif /* !WINDOWS */
  net_Histo_call_count++;
}

/*
 * net_histo_request_finished -
 *
 * return:
 *
 *   request(in):
 *   data_received(in):
 *
 * Note:
 */
static void
net_histo_request_finished (int request, int data_received)
{
#if !defined(WINDOWS)
  struct timeval tp;
  INT64 current_time;
#endif /* !WINDOWS */

  net_Req_buffer[request].total_size_received += data_received;

#if !defined(WINDOWS)
  if (gettimeofday (&tp, NULL) == 0)
    {
      current_time = tp.tv_sec * 1000000LL + tp.tv_usec;
      net_Histo_total_server_time = current_time - net_Histo_last_call_time;
      net_Req_buffer[request].elapsed_time += net_Histo_total_server_time;
    }
#endif /* !WINDOWS */
}

/*
 * net_client_request_no_reply -
 *
 * return:
 *
 *   request(in): server request id
 *   argbuf(in): argument buffer (small)
 *   argsize(in): byte size of argbuf
 *
 */
int
net_client_request_no_reply (int request, char *argbuf, int argsize)
{
  unsigned int rc;
  int error;

  error = NO_ERROR;

  assert (request == NET_SERVER_LOG_SET_INTERRUPT);

  if (net_Server_name[0] == '\0')
    {
      /* need to have a more appropriate "unexpected disconnect" message */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NET_SERVER_CRASHED, 0);
      error = -1;
      return error;
    }

#if defined(HISTO)
  if (net_Histo_setup)
    {
      net_histo_add_entry (request, argsize);
    }
#endif /* HISTO */

  rc = css_send_req_to_server_no_reply (net_Server_host, request, argbuf, argsize);
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
 *       function or net_client_request2.
 */
static int
net_client_request_internal (int request, char *argbuf, int argsize, char *replybuf, int replysize, char *databuf,
			     int datasize, char *replydata, int replydatasize)
{
  unsigned int rc;
  int size;
  int error;
  char *reply = NULL;

  error = 0;

  if (net_Server_name[0] == '\0')
    {
      /* need to have a more appropriate "unexpected disconnect" message */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NET_SERVER_CRASHED, 0);
      error = -1;
      return error;
    }

#if defined(HISTO)
  if (net_Histo_setup)
    {
      net_histo_add_entry (request, argsize + datasize);
    }
#endif /* HISTO */

  rc = css_send_req_to_server (net_Server_host, request, argbuf, argsize, databuf, datasize, replybuf, replysize);
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
	  COMPARE_AND_FREE_BUFFER (replybuf, reply);
	  return set_server_error (error);
	}
      else
	{
	  error = COMPARE_SIZE_AND_BUFFER (&replysize, size, &replybuf, reply);
	}

      if (replydata != NULL)
	{
	  error = css_receive_data_from_server (rc, &reply, &size);
	  if (error != NO_ERROR)
	    {
	      COMPARE_AND_FREE_BUFFER (replydata, reply);
	      return set_server_error (error);
	    }
	  else
	    {
	      error = COMPARE_SIZE_AND_BUFFER (&replydatasize, size, &replydata, reply);
	    }
	}
    }
#if defined(HISTO)
  if (net_Histo_setup)
    {
      net_histo_request_finished (request, replysize + replydatasize);
    }
#endif /* HISTO */
  return error;
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
net_client_request (int request, char *argbuf, int argsize, char *replybuf, int replysize, char *databuf, int datasize,
		    char *replydata, int replydatasize)
{
  return (net_client_request_internal (request, argbuf, argsize, replybuf, replysize, databuf, datasize, replydata,
				       replydatasize));
}

#if defined(ENABLE_UNUSED_FUNCTION)
/*
 * net_client_request_send_large_data -
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
net_client_request_send_large_data (int request, char *argbuf, int argsize, char *replybuf, int replysize,
				    char *databuf, INT64 datasize, char *replydata, int replydatasize)
{
  unsigned int rc;
  int size;
  int error;
  char *reply = NULL;

  error = 0;

  if (net_Server_name[0] == '\0')
    {
      /* need to have a more appropriate "unexpected disconnect" message */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NET_SERVER_CRASHED, 0);
      error = -1;
      return error;
    }

#if defined(HISTO)
  if (net_Histo_setup)
    {
      net_histo_add_entry (request, argsize + datasize);
    }
#endif /* HISTO */

  rc = css_send_req_to_server_with_large_data (net_Server_host, request, argbuf, argsize, databuf, datasize, replybuf,
					       replysize);

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
	  COMPARE_AND_FREE_BUFFER (replybuf, reply);
	  return set_server_error (error);
	}
      else
	{
	  error = COMPARE_SIZE_AND_BUFFER (&replysize, size, &replybuf, reply);
	}

      if (replydata != NULL)
	{
	  error = css_receive_data_from_server (rc, &reply, &size);
	  if (error != NO_ERROR)
	    {
	      COMPARE_AND_FREE_BUFFER (replydata, reply);
	      return set_server_error (error);
	    }
	  else
	    {
	      error = COMPARE_SIZE_AND_BUFFER (&replydatasize, size, &replydata, reply);
	    }
	}
    }
#if defined(HISTO)
  if (net_Histo_setup)
    {
      net_histo_request_finished (request, replysize + replydatasize);
    }
#endif /* HISTO */
  return error;
}

/*
 * net_client_request_recv_large_data -
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
 * Note:
 */
int
net_client_request_recv_large_data (int request, char *argbuf, int argsize, char *replybuf, int replysize,
				    char *databuf, int datasize, char *replydata, INT64 * replydatasize_ptr)
{
  unsigned int rc;
  int size;
  int error;
  INT64 reply_datasize;
  int num_data;
  char *reply = NULL, *ptr, *packed_desc;
  int i, packed_desc_size;

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
      if (net_Histo_setup)
	{
	  net_histo_add_entry (request, argsize + datasize);
	}
#endif /* HISTO */
      rc = css_send_req_to_server (net_Server_host, request, argbuf, argsize, databuf, datasize, replybuf, replysize);
      if (rc == 0)
	{
	  return set_server_error (css_Errno);
	}

      error = css_receive_data_from_server (rc, &reply, &size);

      if (error != NO_ERROR || reply == NULL)
	{
	  COMPARE_AND_FREE_BUFFER (replybuf, reply);
	  return set_server_error (error);
	}
      else
	{
	  error = COMPARE_SIZE_AND_BUFFER (&replysize, size, &replybuf, reply);
	}

      /* here we assume that the first integer in the reply is the length of the following data block */
      ptr = or_unpack_int64 (reply, &reply_datasize);
      num_data = (int) (reply_datasize / INT_MAX + 1);

      if (reply_datasize)
	{
	  for (i = 0; i < num_data; i++)
	    {
	      packed_desc_size = MIN ((int) reply_datasize, INT_MAX);

	      packed_desc = (char *) malloc (packed_desc_size);
	      if (packed_desc == NULL)
		{
		  return set_server_error (CANT_ALLOC_BUFFER);
		}
	      css_queue_receive_data_buffer (rc, packed_desc, packed_desc_size);
	      error = css_receive_data_from_server (rc, &reply, &size);
	      if (error != NO_ERROR || reply == NULL)
		{
		  COMPARE_AND_FREE_BUFFER (packed_desc, reply);
		  free_and_init (packed_desc);
		  return set_server_error (error);
		}
	      else
		{
		  memcpy (replydata, reply, size);
		  COMPARE_AND_FREE_BUFFER (packed_desc, reply);
		  free_and_init (packed_desc);
		}
	      *replydatasize_ptr += size;
	      reply_datasize -= size;
	      replydata += size;
	    }
	}

#if defined(HISTO)
      if (net_Histo_setup)
	{
	  net_histo_request_finished (request, replysize + *replydatasize_ptr);
	}
#endif /* HISTO */
    }
  return error;
}
#endif /* ENABLE_UNUSED_FUNCTION */

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
net_client_request2 (int request, char *argbuf, int argsize, char *replybuf, int replysize, char *databuf, int datasize,
		     char **replydata_ptr, int *replydatasize_ptr)
{
  unsigned int rc;
  int size;
  int reply_datasize, error;
  char *reply = NULL, *replydata;

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
  if (net_Histo_setup)
    {
      net_histo_add_entry (request, argsize + datasize);
    }
#endif /* HISTO */
  rc = css_send_req_to_server (net_Server_host, request, argbuf, argsize, databuf, datasize, replybuf, replysize);
  if (rc == 0)
    {
      error = css_Errno;
      return set_server_error (error);
    }

  error = css_receive_data_from_server (rc, &reply, &size);

  if (error != NO_ERROR || reply == NULL)
    {
      COMPARE_AND_FREE_BUFFER (replybuf, reply);
      return set_server_error (error);
    }
  else
    {
      error = COMPARE_SIZE_AND_BUFFER (&replysize, size, &replybuf, reply);
    }

  /* here we assume that the first integer in the reply is the length of the following data block */
  or_unpack_int (reply, &reply_datasize);

  if (reply_datasize)
    {
      if ((error == NO_ERROR) && (replydata = (char *) malloc (reply_datasize)) != NULL)
	{
	  css_queue_receive_data_buffer (rc, replydata, reply_datasize);
	  error = css_receive_data_from_server (rc, &reply, &size);

	  if (error != NO_ERROR)
	    {
	      COMPARE_AND_FREE_BUFFER (replydata, reply);
	      free_and_init (replydata);
	      return set_server_error (error);
	    }
	  else
	    {
	      error = COMPARE_SIZE_AND_BUFFER (&reply_datasize, size, &replydata, reply);
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
  if (net_Histo_setup)
    {
      net_histo_request_finished (request, replysize + *replydatasize_ptr);
    }
#endif /* HISTO */
  return error;
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
net_client_request2_no_malloc (int request, char *argbuf, int argsize, char *replybuf, int replysize, char *databuf,
			       int datasize, char *replydata, int *replydatasize_ptr)
{
  unsigned int rc;
  int size;
  int reply_datasize, error;
  char *reply = NULL;

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
      if (net_Histo_setup)
	{
	  net_histo_add_entry (request, argsize + datasize);
	}
#endif /* HISTO */
      rc = css_send_req_to_server (net_Server_host, request, argbuf, argsize, databuf, datasize, replybuf, replysize);
      if (rc == 0)
	{
	  return set_server_error (css_Errno);
	}

      error = css_receive_data_from_server (rc, &reply, &size);

      if (error != NO_ERROR || reply == NULL)
	{
	  COMPARE_AND_FREE_BUFFER (replybuf, reply);
	  return set_server_error (error);
	}
      else
	{
	  error = COMPARE_SIZE_AND_BUFFER (&replysize, size, &replybuf, reply);
	}

      /* here we assume that the first integer in the reply is the length of the following data block */
      or_unpack_int (reply, &reply_datasize);

      if (reply_datasize > 0)
	{
	  css_queue_receive_data_buffer (rc, replydata, reply_datasize);
	  error = css_receive_data_from_server (rc, &reply, &size);
	  if (error != NO_ERROR)
	    {
	      COMPARE_AND_FREE_BUFFER (replydata, reply);
	      return set_server_error (error);
	    }
	  else
	    {
	      error = COMPARE_SIZE_AND_BUFFER (&reply_datasize, size, &replydata, reply);
	    }
	  *replydatasize_ptr = size;
	}
#if defined(HISTO)
      if (net_Histo_setup)
	{
	  net_histo_request_finished (request, replysize + *replydatasize_ptr);
	}
#endif /* HISTO */
    }
  return error;
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
net_client_request_3_data (int request, char *argbuf, int argsize, char *databuf1, int datasize1, char *databuf2,
			   int datasize2, char *reply0, int replysize0, char *reply1, int replysize1, char *reply2,
			   int replysize2)
{
  unsigned int rid;
  int rc;
  int size;
  int p1_size, p2_size, error;
  char *reply = NULL, *ptr = NULL;

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
      if (net_Histo_setup)
	{
	  net_histo_add_entry (request, argsize + datasize1 + datasize2);
	}
#endif /* HISTO */
      rid = css_send_req_to_server_2_data (net_Server_host, request, argbuf, argsize, databuf1, datasize1, databuf2,
					   datasize2, NULL, 0);
      if (rid == 0)
	{
	  return set_server_error (css_Errno);
	}

      css_queue_receive_data_buffer (rid, reply0, replysize0);
      error = css_receive_data_from_server (rid, &reply, &size);
      if (error != NO_ERROR || reply == NULL)
	{
	  COMPARE_AND_FREE_BUFFER (reply0, reply);
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
	      COMPARE_AND_FREE_BUFFER (reply0, reply);
	      return rc;
	    }

	  css_queue_receive_data_buffer (rid, reply1, p1_size);
	  if (p2_size > 0)
	    {
	      css_queue_receive_data_buffer (rid, reply2, p2_size);
	    }
	  error = css_receive_data_from_server (rid, &reply, &size);
	  if (error != NO_ERROR)
	    {
	      COMPARE_AND_FREE_BUFFER (reply1, reply);
	      return set_server_error (error);
	    }
	  else
	    {
	      error = COMPARE_SIZE_AND_BUFFER (&replysize1, size, &reply1, reply);
	    }

	  if (p2_size > 0)
	    {
	      error = css_receive_data_from_server (rid, &reply, &size);
	      if (error != NO_ERROR)
		{
		  COMPARE_AND_FREE_BUFFER (reply2, reply);
		  return set_server_error (error);
		}
	      else
		{
		  error = COMPARE_SIZE_AND_BUFFER (&replysize2, size, &reply2, reply);
		}
	    }
	}
#if defined(HISTO)
      if (net_Histo_setup)
	{
	  net_histo_request_finished (request, replysize1 + replysize2);
	}
#endif /* HISTO */
    }
  return rc;
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
net_client_request_with_callback (int request, char *argbuf, int argsize, char *replybuf, int replysize, char *databuf1,
				  int datasize1, char *databuf2, int datasize2, char **replydata_listid,
				  int *replydatasize_listid, char **replydata_page, int *replydatasize_page,
				  char **replydata_plan, int *replydatasize_plan)
{
  unsigned int rc;
  int size;
  int reply_datasize_listid, reply_datasize_page, reply_datasize_plan, error;
  char *reply = NULL, *replydata, *ptr;
  QUERY_SERVER_REQUEST server_request;
  int server_request_num;

  error = 0;
  *replydata_listid = NULL;
  *replydata_page = NULL;
  if (replydata_plan != NULL)
    {
      *replydata_plan = NULL;
    }

  *replydatasize_listid = 0;
  *replydatasize_page = 0;

  if (replydatasize_plan != NULL)
    {
      *replydatasize_plan = 0;
    }

  if (net_Server_name[0] == '\0')
    {
      /* need to have a more appropriate "unexpected disconnect" message */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NET_SERVER_CRASHED, 0);
      error = -1;
    }
  else
    {
#if defined(HISTO)
      if (net_Histo_setup)
	{
	  net_histo_add_entry (request, argsize + datasize1 + datasize2);
	}
#endif /* HISTO */
      rc = css_send_req_to_server_2_data (net_Server_host, request, argbuf, argsize, databuf1, datasize1, databuf2,
					  datasize2, replybuf, replysize);
      if (rc == 0)
	{
	  return set_server_error (css_Errno);
	}

      do
	{
	  error = css_receive_data_from_server (rc, &reply, &size);
	  if (error != NO_ERROR || reply == NULL)
	    {
	      COMPARE_AND_FREE_BUFFER (replybuf, reply);
	      return set_server_error (error);
	    }
#if 0
	  else
	    {
	      error = COMPARE_SIZE_AND_BUFFER (&replysize, size, &replybuf, reply);
	    }
#endif

	  ptr = or_unpack_int (reply, &server_request_num);
	  server_request = (QUERY_SERVER_REQUEST) server_request_num;

	  switch (server_request)
	    {
	    case QUERY_END:
	      /* here we assume that the first integer in the reply is the length of the following data block */
	      ptr = or_unpack_int (ptr, &reply_datasize_listid);
	      ptr = or_unpack_int (ptr, &reply_datasize_page);
	      ptr = or_unpack_int (ptr, &reply_datasize_plan);
	      COMPARE_AND_FREE_BUFFER (replybuf, reply);

	      if (reply_datasize_listid + reply_datasize_page + reply_datasize_plan)
		{
		  if ((error == NO_ERROR) && (replydata = (char *) malloc (reply_datasize_listid)) != NULL)
		    {
		      css_queue_receive_data_buffer (rc, replydata, reply_datasize_listid);
		      error = css_receive_data_from_server (rc, &reply, &size);
		      if (error != NO_ERROR)
			{
			  COMPARE_AND_FREE_BUFFER (replydata, reply);
			  free_and_init (replydata);
			  return set_server_error (error);
			}
		      else
			{
			  error = COMPARE_SIZE_AND_BUFFER (&reply_datasize_listid, size, &replydata, reply);
			}

		      *replydata_listid = reply;
		      *replydatasize_listid = size;
		      reply = NULL;
		    }
		  else
		    {
		      SET_ALLOC_ERR_AND_READ_EXPECTED_PACKETS (&error, rc, 1);
		    }
		}

	      if (reply_datasize_page + reply_datasize_plan)
		{
		  if ((error == NO_ERROR) && (replydata = (char *) malloc (DB_PAGESIZE)) != NULL)
		    {
		      css_queue_receive_data_buffer (rc, replydata, reply_datasize_page);
		      error = css_receive_data_from_server (rc, &reply, &size);
		      if (error != NO_ERROR)
			{
			  COMPARE_AND_FREE_BUFFER (replydata, reply);
			  free_and_init (replydata);
			  return set_server_error (error);
			}
		      else
			{
			  error = COMPARE_SIZE_AND_BUFFER (&reply_datasize_page, size, &replydata, reply);
			}
		      *replydata_page = reply;
		      *replydatasize_page = size;
		      reply = NULL;
		    }
		  else
		    {
		      SET_ALLOC_ERR_AND_READ_EXPECTED_PACKETS (&error, rc, 1);
		    }
		}

	      if (reply_datasize_plan)
		{
		  if ((error == NO_ERROR) && (replydata = (char *) malloc (reply_datasize_plan + 1)) != NULL)
		    {
		      css_queue_receive_data_buffer (rc, replydata, reply_datasize_plan);
		      error = css_receive_data_from_server (rc, &reply, &size);
		      if (error != NO_ERROR)
			{
			  COMPARE_AND_FREE_BUFFER (replydata, reply);
			  free_and_init (replydata);
			  return set_server_error (error);
			}
		      else
			{
			  error = COMPARE_SIZE_AND_BUFFER (&reply_datasize_plan, size, &replydata, reply);
			}

		      if (replydata_plan != NULL)
			{
			  *replydata_plan = reply;
			}

		      if (replydatasize_plan != NULL)
			{
			  *replydatasize_plan = size;
			}

		      reply = NULL;
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
		/* here we assume that the first integer in the reply is the length of the following data block */
		or_unpack_int (ptr, &methoddata_size);
		COMPARE_AND_FREE_BUFFER (replybuf, reply);

		methoddata = (char *) malloc (methoddata_size);
		if (methoddata != NULL)
		  {
		    css_queue_receive_data_buffer (rc, methoddata, methoddata_size);
		    error = css_receive_data_from_server (rc, &reply, &size);
		    if (error != NO_ERROR)
		      {
			COMPARE_AND_FREE_BUFFER (methoddata, reply);
			free_and_init (methoddata);
			return set_server_error (error);
		      }
		    else
		      {
#if defined(CS_MODE)
			bool need_to_reset = false;
			if (method_request_id == 0)
			  {
			    method_request_id = CSS_RID_FROM_EID (rc);
			    need_to_reset = true;
			  }
#endif /* CS_MODE */
			error = COMPARE_SIZE_AND_BUFFER (&methoddata_size, size, &methoddata, reply);
			ptr = or_unpack_unbound_listid (methoddata, (void **) &method_call_list_id);
			method_call_list_id->last_pgptr = NULL;
			ptr = or_unpack_method_sig_list (ptr, (void **) &method_call_sig_list);

			COMPARE_AND_FREE_BUFFER (methoddata, reply);
			free_and_init (methoddata);

			error =
			  method_invoke_for_server (rc, net_Server_host, net_Server_name, method_call_list_id,
						    method_call_sig_list);
			regu_free_listid (method_call_list_id);
			regu_free_method_sig_list (method_call_sig_list);
			if (error != NO_ERROR)
			  {
			    assert (er_errid () != NO_ERROR);
			    error = er_errid ();
			    if (error == NO_ERROR)
			      {
				error = ER_NET_SERVER_DATA_RECEIVE;
				er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
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
		    method_send_error_to_server (rc, net_Server_host, net_Server_name);
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
	      /* The calling function will have to ignore this value in the reply buffer. */
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
		int result = 0;
		char *a_ptr;

		ptr = or_unpack_int (ptr, &x);
		prompt_id = (FILEIO_REMOTE_PROMPT_TYPE) x;

		ptr = or_unpack_int (ptr, &length);
		COMPARE_AND_FREE_BUFFER (replybuf, reply);

		promptdata = (char *) malloc (MAX (length, FILEIO_MAX_USER_RESPONSE_SIZE + OR_INT_SIZE));
		if (promptdata != NULL)
		  {
		    css_queue_receive_data_buffer (rc, promptdata, length);
		    error = css_receive_data_from_server (rc, &reply, &length);
		    if (error != NO_ERROR || reply == NULL)
		      {
			server_request = END_CALLBACK;
			COMPARE_AND_FREE_BUFFER (promptdata, reply);
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
			ptr = or_unpack_string_nocopy (ptr, &secondary_prompt);
			ptr = or_unpack_int (ptr, &reprompt_value);
		      }

		    display_string = prompt;

		    memset (user_response_buffer, 0, sizeof (user_response_buffer));

		    while (error == NO_ERROR && retry_in)
		      {
			/* Display prompt, then get user's input. */
			fprintf (stdout, display_string);
			pr_status = ER_FAILED;
			pr_len = 0;
			retry_in = false;

			if (prompt_id != FILEIO_PROMPT_DISPLAY_ONLY)
			  {
			    error2 = scanf ("%2000s", user_response_ptr);
			    if (error2 > 0)
			      {
				/* basic input int validation before we send it back */
				switch (prompt_id)
				  {
				  case FILEIO_PROMPT_RANGE_TYPE:
				    /* Numeric range checking */
				    result = str_to_int32 (&x, &a_ptr, user_response_ptr, 10);
				    if (result != 0 || x < range_lower || x > range_higher)
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
				    if ((char_tolower (*user_response_ptr) == 'y') || (*user_response_ptr == '1')
					|| (intl_mbs_casecmp (user_response_ptr, "yes") == 0))
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

				    /* Validate initial prompt, then post secondary prompt */
				  case FILEIO_PROMPT_RANGE_WITH_SECONDARY_STRING_TYPE:
				    /* Numeric range checking on the first promp, but user's answer we really want is
				     * the second prompt */
				    result = str_to_int32 (&x, &a_ptr, user_response_ptr, 10);
				    if (result != 0 || x < range_lower || x > range_higher)
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
					/* moving the response buffer ptr forward insures that both the first response
					 * and the second are included in the buffer. (no delimiter or null bytes
					 * allowed) */
					user_response_ptr += strlen (user_response_ptr);
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

		    /* Return the user's answer to the server. All of the cases above should get to here after looping
		     * or whatever is necessary and provide indication of local errors (pr_status), as well as provide
		     * a string in user_response.  We send back to the server an int (status) followed by a string. */
		    /* check for overflow, could be dangerous */
		    pr_len = strlen (user_response_buffer);
		    if (pr_len > FILEIO_MAX_USER_RESPONSE_SIZE)
		      {
			error = ER_NET_DATA_TRUNCATED;
			er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
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
			ptr = or_pack_string_with_length (ptr, user_response_buffer, pr_len);
		      }
		    error2 = net_client_send_data (net_Server_host, rc, promptdata, CAST_STRLEN (ptr - promptdata));
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

		    COMPARE_AND_FREE_BUFFER (promptdata, reply);
		    free_and_init (promptdata);
		  }
		else
		  {
		    /* send back some kind of error to server */
		    SET_ALLOC_ERR_AND_READ_EXPECTED_PACKETS (&error, rc, 1);

		    /* Do we need to tell the server? */
		    server_request = END_CALLBACK;	/* force a stop */
		    return_error_to_server (net_Server_host, rc);
		  }
	      }
	      /* expecting another reply */
	      css_queue_receive_data_buffer (rc, replybuf, replysize);

	      break;

	    case CONSOLE_OUTPUT:
	      {
		int length;
		char *print_data, *print_str;

		ptr = or_unpack_int (ptr, &length);
		ptr = or_unpack_int (ptr, &length);
		COMPARE_AND_FREE_BUFFER (replybuf, reply);

		print_data = (char *) malloc (length);
		if (print_data != NULL)
		  {
		    css_queue_receive_data_buffer (rc, print_data, length);
		    error = css_receive_data_from_server (rc, &reply, &length);
		    if (error != NO_ERROR || reply == NULL)
		      {
			server_request = END_CALLBACK;
			COMPARE_AND_FREE_BUFFER (print_data, reply);
			free_and_init (print_data);
			return set_server_error (error);
		      }
		    else
		      {
			ptr = or_unpack_string_nocopy (reply, &print_str);
			fprintf (stdout, print_str);
			fflush (stdout);
		      }
		    free_and_init (print_data);
		  }
	      }

	      /* expecting another reply */
	      css_queue_receive_data_buffer (rc, replybuf, replysize);

	      error = NO_ERROR;
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
      if (net_Histo_setup)
	{
	  net_histo_request_finished (request,
				      replysize + *replydatasize_listid + *replydatasize_page + *replaydatasize_plan);
	}
#endif /* HISTO */
    }
  return error;
}

/*
 * net_client_check_log_header -
 *
 * return:
 * Note:
 */
int
net_client_check_log_header (LOGWR_CONTEXT * ctx_ptr, char *argbuf, int argsize, char *replybuf, int replysize,
			     char **logpg_area_buf, bool verbose)
{
  unsigned int rc;
  char *reply = NULL;
  char *ptr;
  int error = NO_ERROR;
  int size;
  int fillsize;
  int request = NET_SERVER_LOGWR_GET_LOG_PAGES;
  QUERY_SERVER_REQUEST server_request;
  int server_request_num;

  if (net_Server_name[0] == '\0')
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NET_SERVER_CRASHED, 0);
      error = ER_NET_SERVER_CRASHED;
    }
  else
    {
      if (ctx_ptr->rc == -1)
	{
	  /* HEADER PAGE REQUEST */
	  rc = css_send_req_to_server_2_data (net_Server_host, request, argbuf, argsize, NULL, 0, NULL, 0, replybuf,
					      replysize);
	  if (rc == 0)
	    {
	      return set_server_error (css_Errno);
	    }
	  ctx_ptr->rc = rc;
	}
      else
	{
	  /* END PROTOCOL */
	  rc = ctx_ptr->rc;
	  error = net_client_send_data (net_Server_host, rc, argbuf, argsize);
	  if (error != NO_ERROR)
	    {
	      return error;
	    }
	  (void) css_queue_receive_data_buffer (rc, replybuf, replysize);
	}

      error = css_receive_data_from_server (rc, &reply, &size);
      if (error != NO_ERROR || reply == NULL)
	{
	  COMPARE_AND_FREE_BUFFER (replybuf, reply);
	  return set_server_error (error);
	}
      else
	{
	  error = COMPARE_SIZE_AND_BUFFER (&replysize, size, &replybuf, reply);
	  if (error != NO_ERROR)
	    {
	      return error;
	    }
	}

      ptr = or_unpack_int (reply, &server_request_num);
      server_request = (QUERY_SERVER_REQUEST) server_request_num;

      switch (server_request)
	{
	case GET_NEXT_LOG_PAGES:
	  {
	    int length;
	    char *logpg_area;
	    char *reply_logpg = NULL;
	    ptr = or_unpack_int (ptr, (int *) (&length));
	    if (length <= 0)
	      {
		er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NET_SERVER_CRASHED, 0);
		error = ER_NET_SERVER_CRASHED;
	      }

	    logpg_area = (char *) malloc (length);
	    if (logpg_area == NULL)
	      {
		er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) length);
		error = ER_OUT_OF_VIRTUAL_MEMORY;
	      }
	    css_queue_receive_data_buffer (rc, logpg_area, length);
	    error = css_receive_data_from_server (rc, &reply_logpg, &fillsize);
	    if (error != NO_ERROR)
	      {
		COMPARE_AND_FREE_BUFFER (logpg_area, reply_logpg);
		return set_server_error (error);
	      }
	    else
	      {
		*logpg_area_buf = logpg_area;
	      }
	  }
	  break;
	case END_CALLBACK:
	  error = NO_ERROR;
	  break;
	default:
	  error = ER_NET_SERVER_DATA_RECEIVE;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	  break;
	}
    }
  return error;
}

/*
 * net_client_request_with_logwr_context -
 *
 * return:
 * Note:
 */
int
net_client_request_with_logwr_context (LOGWR_CONTEXT * ctx_ptr, int request, char *argbuf, int argsize, char *replybuf,
				       int replysize, char *databuf1, int datasize1, char *databuf2, int datasize2,
				       char **replydata_ptr1, int *replydatasize_ptr1, char **replydata_ptr2,
				       int *replydatasize_ptr2)
{
  unsigned int rc;
  int size;
  int error;
  int request_error;
  char *reply = NULL, *ptr;
  QUERY_SERVER_REQUEST server_request;
  int server_request_num;
  bool do_read;

  error = 0;
  *replydata_ptr1 = NULL;
  *replydata_ptr2 = NULL;
  *replydatasize_ptr1 = 0;
  *replydatasize_ptr2 = 0;

  if (net_Server_name[0] == '\0')
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NET_SERVER_CRASHED, 0);
      error = ER_NET_SERVER_CRASHED;
    }
  else
    {
#if defined (HISTO)
      if (net_Histo_setup)
	{
	  net_histo_add_entry (request, argsize + datasize1 + datasize2);
	}
#endif /* HISTO */
      if (ctx_ptr->rc == -1)
	{
	  /* It sends a new request */
	  rc =
	    css_send_req_to_server_2_data (net_Server_host, request, argbuf, argsize, databuf1, datasize1, databuf2,
					   datasize2, replybuf, replysize);
	  if (rc == 0)
	    {
	      return set_server_error (css_Errno);
	    }
	  ctx_ptr->rc = rc;
	}
      else
	{
	  /* It sends the same request with new arguments */
	  rc = ctx_ptr->rc;
	  error = net_client_send_data (net_Server_host, rc, argbuf, argsize);
	  if (error != NO_ERROR)
	    {
	      return error;
	    }
	  (void) css_queue_receive_data_buffer (rc, replybuf, replysize);
	}

      do
	{
	  do_read = false;
#ifndef WINDOWS
	  if (logwr_Gl.mode == LOGWR_MODE_SEMISYNC)
	    {
	      error = css_receive_data_from_server_with_timeout (rc, &reply, &size, 1000);
	    }
	  else
#endif
	    error = css_receive_data_from_server (rc, &reply, &size);
	  if (error != NO_ERROR || reply == NULL)
	    {
	      COMPARE_AND_FREE_BUFFER (replybuf, reply);
	      return set_server_error (error);
	    }
	  else
	    {
	      error = COMPARE_SIZE_AND_BUFFER (&replysize, size, &replybuf, reply);
	      if (error != NO_ERROR)
		{
		  return error;
		}
	    }

	  ptr = or_unpack_int (reply, &server_request_num);
	  server_request = (QUERY_SERVER_REQUEST) server_request_num;

	  switch (server_request)
	    {
	    case GET_NEXT_LOG_PAGES:
	      {
		int length;
		ptr = or_unpack_int (ptr, (int *) (&length));
		error = net_client_get_next_log_pages (rc, replybuf, replysize, length);
	      }
	      break;
	    case END_CALLBACK:
	      if (logwr_Gl.mode == LOGWR_MODE_SEMISYNC)
		{
		  logwr_Gl.force_flush = true;
		  error = logwr_set_hdr_and_flush_info ();
		  if (error == NO_ERROR)
		    {
		      error = logwr_write_log_pages ();
		    }
		  logwr_Gl.action &= LOGWR_ACTION_DELAYED_WRITE;
		}

	      ptr = or_unpack_int (ptr, &request_error);
	      if (request_error != ctx_ptr->last_error)
		{
		  /* By server error or shutdown */
		  error = request_error;
		  if (error != ER_HA_LW_FAILED_GET_LOG_PAGE)
		    {
		      error = ER_NET_SERVER_CRASHED;
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
		    }
		}

	      ctx_ptr->shutdown = true;
	      break;
	    default:
	      /* TODO: handle the unknown request as an error */
	      if (logwr_Gl.mode == LOGWR_MODE_SEMISYNC)
		{
		  logwr_Gl.force_flush = true;
		  error = logwr_set_hdr_and_flush_info ();
		  if (error == NO_ERROR)
		    {
		      error = logwr_write_log_pages ();
		    }
		  logwr_Gl.action &= LOGWR_ACTION_DELAYED_WRITE;
		}

	      error = ER_NET_SERVER_DATA_RECEIVE;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);

	      ctx_ptr->shutdown = true;
	      break;
	    }
	}
      while (do_read /* server_request != END_CALLBACK */ );
#if defined(HISTO)
      if (net_Histo_setup)
	{
	  net_histo_request_finished (request, replysize + *replydatasize_ptr1 + *replydatasize_ptr2);
	}
#endif /* HISTO */
    }
  return error;
}

/*
 * net_client_logwr_send_end_msg
 *
 * return:
 * note:
 */
void
net_client_logwr_send_end_msg (int rc, int error)
{
  OR_ALIGNED_BUF (OR_INT_SIZE * 2 + OR_INT64_SIZE) a_request;
  char *request;
  char *ptr;

  request = OR_ALIGNED_BUF_START (a_request);

  /* END REQUEST */
  ptr = or_pack_int64 (request, LOGPB_HEADER_PAGE_ID);
  ptr = or_pack_int (ptr, LOGWR_MODE_ASYNC);
  ptr = or_pack_int (ptr, error);

  net_client_send_data (net_Server_host, rc, request, OR_ALIGNED_BUF_SIZE (a_request));

  return;
}

/*
 * net_client_get_next_log_pages -
 *
 * return:
 *
 *   rc(in): pre-allocated data buffer
 *   replybuf(in): reply argument buffer
 *   replysize(in): reply argument buffer size
 *   ptr(in): pre-allocated data buffer
 *
 * Note:
 */
int
net_client_get_next_log_pages (int rc, char *replybuf, int replysize, int length)
{
  char *reply = NULL;
  int error;

  if (logwr_Gl.logpg_area_size < length)
    {
      /* 
       * It means log_buffer_size/log_page_size are different between master
       * and slave.
       * In this case, we have to disconnect from server and try to reconnect.
       */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NET_SERVER_CRASHED, 0);
      return ER_NET_SERVER_CRASHED;
    }

  (void) css_queue_receive_data_buffer (rc, logwr_Gl.logpg_area, logwr_Gl.logpg_area_size);
  error = css_receive_data_from_server (rc, &reply, &logwr_Gl.logpg_fill_size);
  if (error != NO_ERROR)
    {
      COMPARE_AND_FREE_BUFFER (logwr_Gl.logpg_area, reply);
      return set_server_error (error);
    }
  else
    {
      error = logwr_set_hdr_and_flush_info ();
      if (error != NO_ERROR)
	{
	  COMPARE_AND_FREE_BUFFER (logwr_Gl.logpg_area, reply);
	  return error;
	}

      switch (logwr_Gl.mode)
	{
	case LOGWR_MODE_SYNC:
	case LOGWR_MODE_SEMISYNC:
	  error = logwr_write_log_pages ();
	  break;
	case LOGWR_MODE_ASYNC:
	  logwr_Gl.action |= LOGWR_ACTION_ASYNC_WRITE;
	  break;
	default:
	  break;
	}
    }

  COMPARE_AND_FREE_BUFFER (logwr_Gl.logpg_area, reply);
  return error;
}

#if defined(ENABLE_UNUSED_FUNCTION)
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
  char *buffer, *reply = NULL;

  error = 0;
  *buf_ptr = NULL;

  buffer = (char *) malloc (expected_size);
  if (buffer != NULL)
    {
      css_queue_receive_data_buffer (rc, buffer, expected_size);
      error = css_receive_data_from_server (rc, &reply, &reply_size);
      if (error != NO_ERROR)
	{
	  COMPARE_AND_FREE_BUFFER (buffer, reply);
	  free_and_init (buffer);
	  return set_server_error (error);
	}
      else
	{
	  error = COMPARE_SIZE_AND_BUFFER (&expected_size, reply_size, &buffer, reply);
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

  return error;
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
net_client_request3 (int request, char *argbuf, int argsize, char *replybuf, int replysize, char *databuf, int datasize,
		     char **replydata_ptr, int *replydatasize_ptr, char **replydata_ptr2, int *replydatasize_ptr2)
{
  unsigned int rc;
  int size;
  int reply_datasize, reply_datasize2, error;
  char *reply = NULL, *replydata, *replydata2;
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
      if (net_Histo_setup)
	{
	  net_histo_add_entry (request, argsize + datasize);
	}
#endif /* HISTO */
      rc = css_send_req_to_server (net_Server_host, request, argbuf, argsize, databuf, datasize, replybuf, replysize);
      if (rc == 0)
	{
	  return set_server_error (css_Errno);
	}

      error = css_receive_data_from_server (rc, &reply, &size);
      if (error != NO_ERROR || reply == NULL)
	{
	  COMPARE_AND_FREE_BUFFER (replybuf, reply);
	  return set_server_error (error);
	}
      else
	{
	  error = COMPARE_SIZE_AND_BUFFER (&replysize, size, &replybuf, reply);
	}

      /* here we assume that the first two integers in the reply are the lengths of the following data blocks */
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
	  error = net_client_request_buffer (rc, &replydata2, reply_datasize2);
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
      if (net_Histo_setup)
	{
	  net_histo_request_finished (request, replysize + *replydatasize_ptr + *replydatasize_ptr2);
	}
#endif /* HISTO */
    }
  return error;
}
#endif /* ENABLE_UNUSED_FUNCTION */

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
net_client_request_recv_copyarea (int request, char *argbuf, int argsize, char *replybuf, int replysize,
				  LC_COPYAREA ** reply_copy_area)
{
  unsigned int rc;
  int size;
  int error;
  char *reply = NULL;
  int content_size;
  char *content_ptr = NULL;
  int num_objs;
  char *packed_desc = NULL;
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
      if (net_Histo_setup)
	{
	  net_histo_add_entry (request, argsize);
	}
#endif /* HISTO */

      rc = css_send_req_to_server (net_Server_host, request, argbuf, argsize, NULL, 0, replybuf, replysize);
      if (rc == 0)
	{
	  return set_server_error (css_Errno);
	}

      /* 
       * Receive replybuf
       */

      error = css_receive_data_from_server (rc, &reply, &size);
      if (error != NO_ERROR || reply == NULL)
	{
	  COMPARE_AND_FREE_BUFFER (replybuf, reply);
	  return set_server_error (error);
	}
      else
	{
	  error = COMPARE_SIZE_AND_BUFFER (&replysize, size, &replybuf, reply);
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
	  if (error == NO_ERROR && reply_copy_area != NULL)
	    {
	      *reply_copy_area = locator_recv_allocate_copyarea (num_objs, &packed_desc, packed_desc_size, &content_ptr,
								 content_size);
	      if (*reply_copy_area != NULL)
		{
		  if (packed_desc != NULL && packed_desc_size > 0)
		    {
		      css_queue_receive_data_buffer (rc, packed_desc, packed_desc_size);
		      error = css_receive_data_from_server (rc, &reply, &size);
		      if (error != NO_ERROR)
			{
			  COMPARE_AND_FREE_BUFFER (packed_desc, reply);
			  free_and_init (packed_desc);
			  locator_free_copy_area (*reply_copy_area);
			  *reply_copy_area = NULL;
			  return set_server_error (error);
			}
		      else
			{
			  locator_unpack_copy_area_descriptor (num_objs, *reply_copy_area, packed_desc);
			  COMPARE_AND_FREE_BUFFER (packed_desc, reply);
			  free_and_init (packed_desc);
			}
		    }

		  if (content_size > 0)
		    {
		      error = css_queue_receive_data_buffer (rc, content_ptr, content_size);
		      if (error != NO_ERROR)
			{
			  SET_ALLOC_ERR_AND_READ_EXPECTED_PACKETS (&error, rc, 1);
			}
		      else
			{
			  error = css_receive_data_from_server (rc, &reply, &size);
			}

		      COMPARE_AND_FREE_BUFFER (content_ptr, reply);

		      if (error != NO_ERROR)
			{
			  if (packed_desc != NULL)
			    {
			      free_and_init (packed_desc);
			    }
			  locator_free_copy_area (*reply_copy_area);
			  *reply_copy_area = NULL;
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

	      if (packed_desc != NULL)
		{
		  free_and_init (packed_desc);
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
      if (net_Histo_setup)
	{
	  net_histo_request_finished (request, replysize + content_size + packed_desc_size);
	}
#endif /* HISTO */
    }
  return error;
}

/*
 * net_client_request_2recv_copyarea -
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
net_client_request_2recv_copyarea (int request, char *argbuf, int argsize, char *replybuf, int replysize, char *databuf,
				   int datasize, char *recvbuffer, int recvbuffer_size, LC_COPYAREA ** reply_copy_area,
				   int *eid)
{
  unsigned int rc;
  int size;
  int p_size, error;
  char *reply = NULL;
  int content_size;
  char *content_ptr = NULL;
  int num_objs;
  char *packed_desc = NULL;
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
  if (net_Histo_setup)
    {
      net_histo_add_entry (request, argsize + datasize);
    }
#endif /* HISTO */

  rc = css_send_req_to_server (net_Server_host, request, argbuf, argsize, databuf, datasize, replybuf, replysize);
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
      COMPARE_AND_FREE_BUFFER (replybuf, reply);
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
	  /* maintain error status.  If we continued without checking this, error could become NO_ERROR and caller
	   * would never know. */
	  css_receive_data_from_server (rc, &reply, &size);
	  if (reply != NULL)
	    {
	      free_and_init (reply);
	    }
	}
      else
	{
	  css_queue_receive_data_buffer (rc, recvbuffer, p_size);
	  error = css_receive_data_from_server (rc, &reply, &size);
	  if (error != NO_ERROR)
	    {
	      COMPARE_AND_FREE_BUFFER (recvbuffer, reply);
	      return set_server_error (error);
	    }
	  else
	    {
	      /* we expect that the sizes won't match, but we must be sure that the we can accomodate the data in our
	       * buffer. So, don't use COMPARE_SIZE_AND_BUFFER() here. */
	      if (recvbuffer_size < size)
		{
		  error = ER_NET_DATASIZE_MISMATCH;
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 2, recvbuffer_size, size);
		}
	      else
		{
		  recvbuffer_size = size;
		}

	      if (reply != recvbuffer)
		{
		  error = ER_NET_UNUSED_BUFFER;
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
		  free_and_init (reply);
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
	  *reply_copy_area = locator_recv_allocate_copyarea (num_objs, &packed_desc, packed_desc_size, &content_ptr,
							     content_size);
	  if (*reply_copy_area != NULL)
	    {
	      if (packed_desc != NULL && packed_desc_size > 0)
		{
		  css_queue_receive_data_buffer (rc, packed_desc, packed_desc_size);
		  error = css_receive_data_from_server (rc, &reply, &size);
		  if (error != NO_ERROR)
		    {
		      COMPARE_AND_FREE_BUFFER (packed_desc, reply);
		      free_and_init (packed_desc);
		      return set_server_error (error);
		    }
		  else
		    {
		      locator_unpack_copy_area_descriptor (num_objs, *reply_copy_area, packed_desc);
		      COMPARE_AND_FREE_BUFFER (packed_desc, reply);
		      free_and_init (packed_desc);
		    }
		}

	      if (content_size > 0)
		{
		  css_queue_receive_data_buffer (rc, content_ptr, content_size);
		  error = css_receive_data_from_server (rc, &reply, &size);
		  COMPARE_AND_FREE_BUFFER (content_ptr, reply);
		  if (error != NO_ERROR)
		    {
		      if (packed_desc != NULL)
			{
			  free_and_init (packed_desc);
			}
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

	  if (packed_desc != NULL)
	    {
	      free_and_init (packed_desc);
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
  if (net_Histo_setup)
    {
      net_histo_request_finished (request, replysize + recvbuffer_size + content_size + packed_desc_size);
    }
#endif /* HISTO */
  return error;
}

/*
 * net_client_request_3_data_recv_copyarea -
 *
 * return:
 *
 *   request(in):
 *   argbuf(in):
 *   argsize(in):
 *   databuf1(in):
 *   datasize1(in):
 *   databuf2(in):
 *   datasize2(in):
 *   replybuf(in):
 *   replysize(in):
 *   reply_copy_area(out): copy area sent by server
 *
 * Note:
 */
int
net_client_request_3_data_recv_copyarea (int request, char *argbuf, int argsize, char *databuf1, int datasize1,
					 char *databuf2, int datasize2, char *replybuf, int replysize,
					 LC_COPYAREA ** reply_copy_area)
{
  unsigned int rid;
  int size;
  int error;
  char *reply = NULL;
  int content_size;
  char *content_ptr = NULL;
  int num_objs;
  char *packed_desc = NULL;
  int packed_desc_size;
  // test code
  int success;

  error = 0;
  if (net_Server_name[0] == '\0')
    {
      /* need to have a more appropriate "unexpected disconnect" message */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NET_SERVER_CRASHED, 0);
      error = -1;
      return error;
    }
#if defined(HISTO)
  if (net_Histo_setup)
    {
      net_histo_add_entry (request, argsize + datasize);
    }
#endif /* HISTO */

  rid =
    css_send_req_to_server_2_data (net_Server_host, request, argbuf, argsize, databuf1, datasize1, databuf2, datasize2,
				   replybuf, replysize);
  if (rid == 0)
    {
      return set_server_error (css_Errno);
    }

  error = css_receive_data_from_server (rid, &reply, &size);
  if (error != NO_ERROR || reply == NULL)
    {
      COMPARE_AND_FREE_BUFFER (replybuf, reply);
      return set_server_error (error);
    }
  else
    {
      error = COMPARE_SIZE_AND_BUFFER (&replysize, size, &replybuf, reply);
    }

  replybuf = or_unpack_int (replybuf, &num_objs);
  replybuf = or_unpack_int (replybuf, &packed_desc_size);
  replybuf = or_unpack_int (replybuf, &content_size);
  replybuf = or_unpack_int (replybuf, &success);

  *reply_copy_area = NULL;
  if (packed_desc_size != 0 || content_size != 0)
    {
      if (error == NO_ERROR)
	{
	  *reply_copy_area = locator_recv_allocate_copyarea (num_objs, &packed_desc, packed_desc_size, &content_ptr,
							     content_size);
	  if (*reply_copy_area != NULL)
	    {
	      if (packed_desc != NULL && packed_desc_size > 0)
		{
		  css_queue_receive_data_buffer (rid, packed_desc, packed_desc_size);
		  error = css_receive_data_from_server (rid, &reply, &size);
		  if (error != NO_ERROR)
		    {
		      COMPARE_AND_FREE_BUFFER (packed_desc, reply);
		      free_and_init (packed_desc);
		      return set_server_error (error);
		    }
		  else
		    {
		      locator_unpack_copy_area_descriptor (num_objs, *reply_copy_area, packed_desc);
		      COMPARE_AND_FREE_BUFFER (packed_desc, reply);
		      free_and_init (packed_desc);
		    }
		}

	      if (content_size > 0)
		{
		  css_queue_receive_data_buffer (rid, content_ptr, content_size);
		  error = css_receive_data_from_server (rid, &reply, &size);
		  COMPARE_AND_FREE_BUFFER (content_ptr, reply);
		  if (error != NO_ERROR)
		    {
		      if (packed_desc != NULL)
			{
			  free_and_init (packed_desc);
			}
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
	      SET_ALLOC_ERR_AND_READ_EXPECTED_PACKETS (&error, rid, num_packets);
	    }

	  if (packed_desc != NULL)
	    {
	      free_and_init (packed_desc);
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
	  SET_ALLOC_ERR_AND_READ_EXPECTED_PACKETS (&error, rid, num_packets);

	}
    }

#if defined(HISTO)
  if (net_Histo_setup)
    {
      net_histo_request_finished (request, replysize + recvbuffer_size + content_size + packed_desc_size);
    }
#endif /* HISTO */
  return error;
}

/*
 * net_client_recv_copyarea -
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
net_client_recv_copyarea (int request, char *replybuf, int replysize, char *recvbuffer, int recvbuffer_size,
			  LC_COPYAREA ** reply_copy_area, int rc)
{
  int size;
  int error, p_size;
  char *reply = NULL;
  int content_size;
  char *content_ptr = NULL;
  int num_objs;
  char *packed_desc = NULL;
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
  if (net_Histo_setup)
    {
      net_histo_add_entry (request, 0);
    }
#endif /* HISTO */

  /* 
   * Receive replybuf
   */

  css_queue_receive_data_buffer (rc, replybuf, replysize);
  error = css_receive_data_from_server (rc, &reply, &size);
  if (error != NO_ERROR)
    {
      COMPARE_AND_FREE_BUFFER (replybuf, reply);
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
	  /* maintain error status.  If we continued without checking this, error could become NO_ERROR and caller
	   * would never know. */
	  css_receive_data_from_server (rc, &reply, &size);
	  if (reply != NULL)
	    {
	      free_and_init (reply);
	    }
	}
      else
	{
	  css_queue_receive_data_buffer (rc, recvbuffer, p_size);
	  error = css_receive_data_from_server (rc, &reply, &size);
	  if (error != NO_ERROR)
	    {
	      COMPARE_AND_FREE_BUFFER (recvbuffer, reply);
	      return set_server_error (error);
	    }
	  else
	    {
	      if (recvbuffer_size < size)
		{
		  /* we expect that the sizes won't match, but we must be sure that the we can accomodate the data in
		   * our buffer. So, don't use COMPARE_SIZE_AND_BUFFER() here. */
		  error = ER_NET_DATASIZE_MISMATCH;
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 2, recvbuffer_size, size);
		}
	      else
		{
		  recvbuffer_size = size;
		}

	      if (reply != recvbuffer)
		{
		  error = ER_NET_UNUSED_BUFFER;
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
		  free_and_init (reply);
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
	  *reply_copy_area =
	    locator_recv_allocate_copyarea (num_objs, &packed_desc, packed_desc_size, &content_ptr, content_size);
	  if (*reply_copy_area != NULL)
	    {
	      if (packed_desc != NULL && packed_desc_size > 0)
		{
		  css_queue_receive_data_buffer (rc, packed_desc, packed_desc_size);
		  error = css_receive_data_from_server (rc, &reply, &size);
		  if (error != NO_ERROR)
		    {
		      COMPARE_AND_FREE_BUFFER (packed_desc, reply);
		      free_and_init (packed_desc);
		      return set_server_error (error);
		    }
		  else
		    {
		      locator_unpack_copy_area_descriptor (num_objs, *reply_copy_area, packed_desc);
		      COMPARE_AND_FREE_BUFFER (packed_desc, reply);
		      free_and_init (packed_desc);
		    }
		}

	      if (content_size > 0)
		{
		  css_queue_receive_data_buffer (rc, content_ptr, content_size);
		  error = css_receive_data_from_server (rc, &reply, &size);
		  COMPARE_AND_FREE_BUFFER (content_ptr, reply);
		  if (error != NO_ERROR)
		    {
		      if (packed_desc != NULL)
			{
			  free_and_init (packed_desc);
			}
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

	  if (packed_desc != NULL)
	    {
	      free_and_init (packed_desc);
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
  if (net_Histo_setup)
    {
      net_histo_request_finished (request, replysize + recvbuffer_size + content_size + packed_desc_size);
    }
#endif /* HISTO */

  return error;
}

/*
 * net_client_request_3recv_copyarea -
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
net_client_request_3recv_copyarea (int request, char *argbuf, int argsize, char *replybuf, int replysize, char *databuf,
				   int datasize, char **recvbuffer, int *recvbuffer_size,
				   LC_COPYAREA ** reply_copy_area)
{
  unsigned int rc;
  int size;
  int p_size, error;
  char *reply = NULL;
  int content_size;
  char *content_ptr = NULL;
  int num_objs;
  char *packed_desc = NULL;
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
  if (net_Histo_setup)
    {
      net_histo_add_entry (request, argsize + datasize);
    }
#endif /* HISTO */

  rc = css_send_req_to_server (net_Server_host, request, argbuf, argsize, databuf, datasize, replybuf, replysize);
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
      COMPARE_AND_FREE_BUFFER (replybuf, reply);
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

      if ((error == NO_ERROR) && (*recvbuffer = (char *) malloc (p_size)) != NULL)
	{
	  css_queue_receive_data_buffer (rc, *recvbuffer, p_size);
	  error = css_receive_data_from_server (rc, &reply, &size);
	  if (error != NO_ERROR)
	    {
	      COMPARE_AND_FREE_BUFFER (*recvbuffer, reply);
	      free_and_init (*recvbuffer);
	      return set_server_error (error);
	    }
	  else
	    {
	      error = COMPARE_SIZE_AND_BUFFER (recvbuffer_size, size, recvbuffer, reply);
	    }

	  COMPARE_AND_FREE_BUFFER (*recvbuffer, reply);
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
	  &&
	  ((*reply_copy_area =
	    locator_recv_allocate_copyarea (num_objs, &packed_desc, packed_desc_size, &content_ptr,
					    content_size)) != NULL))
	{
	  if (packed_desc != NULL && packed_desc_size > 0)
	    {
	      css_queue_receive_data_buffer (rc, packed_desc, packed_desc_size);
	      error = css_receive_data_from_server (rc, &reply, &size);
	      if (error != NO_ERROR)
		{
		  COMPARE_AND_FREE_BUFFER (packed_desc, reply);
		  free_and_init (packed_desc);
		  return set_server_error (error);
		}
	      else
		{
		  locator_unpack_copy_area_descriptor (num_objs, *reply_copy_area, packed_desc);
		  COMPARE_AND_FREE_BUFFER (packed_desc, reply);
		  free_and_init (packed_desc);
		}
	    }

	  if (content_size > 0)
	    {
	      css_queue_receive_data_buffer (rc, content_ptr, content_size);
	      error = css_receive_data_from_server (rc, &reply, &size);
	      COMPARE_AND_FREE_BUFFER (content_ptr, reply);
	      if (error != NO_ERROR)
		{
		  if (packed_desc != NULL)
		    {
		      free_and_init (packed_desc);
		    }
		  return set_server_error (error);
		}
	    }

	  if (packed_desc != NULL)
	    {
	      free_and_init (packed_desc);
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
  if (net_Histo_setup)
    {
      net_histo_request_finished (request, replysize + *recvbuffer_size + content_size + packed_desc_size);
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
net_client_request_recv_stream (int request, char *argbuf, int argsize, char *replybuf, int replybuf_size,
				char *databuf, int datasize, FILE * outfp)
{
  unsigned int rc;
  int size;
  int error;
  char *reply = NULL;
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
  if (net_Histo_setup)
    {
      net_histo_add_entry (request, send_argsize + datasize);
    }
#endif /* HISTO */

  rc = css_send_req_to_server (net_Server_host, request, send_argbuffer, send_argsize, databuf, datasize, recv_replybuf,
			       recv_replybuf_size);
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
	  COMPARE_AND_FREE_BUFFER (recv_replybuf, reply);
	  error = set_server_error (error);
	  goto end;
	}
      else
	{
	  error = COMPARE_SIZE_AND_BUFFER (&recv_replybuf_size, size, &recv_replybuf, reply);
	}

      /* Get total size of file to transfered */
      or_unpack_int (recv_replybuf, &file_size);

      if (replybuf)
	{
	  memcpy (replybuf, recv_replybuf + OR_INT_SIZE, recv_replybuf_size - OR_INT_SIZE);
	}

#if defined(HISTO)
      if (net_Histo_setup)
	{
	  net_histo_request_finished (request, recv_replybuf_size + file_size);
	}
#endif /* HISTO */

      while (file_size > 0)
	{
	  css_queue_receive_data_buffer (rc, reply_streamdata, reply_streamdata_size);
	  error = css_receive_data_from_server (rc, &reply, &size);
	  if (error != NO_ERROR)
	    {
	      COMPARE_AND_FREE_BUFFER (reply_streamdata, reply);
	      error = set_server_error (error);
	      goto end;
	    }
	  else
	    {
	      if (reply != reply_streamdata)
		{
		  error = ER_NET_UNUSED_BUFFER;
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
		  COMPARE_AND_FREE_BUFFER (reply_streamdata, reply);
		  break;
		}
	      if (size > reply_streamdata_size)
		{
		  error = ER_NET_DATASIZE_MISMATCH;
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 2, reply_streamdata_size, size);
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

  return error;
}

/*
 * net_client_ping_server -ping the server
 *
 * return:
 */

int
net_client_ping_server (int client_val, int *server_val, int timeout)
{
  OR_ALIGNED_BUF (OR_INT_SIZE) a_request;
  char *request = OR_ALIGNED_BUF_START (a_request);
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply_buf = OR_ALIGNED_BUF_START (a_reply);
  char *reply = NULL;
  int eid, error, reply_size;

  er_log_debug (ARG_FILE_LINE, "The net_client_ping_server() is calling.");

  error = NO_ERROR;
  if (net_Server_host[0] == '\0' || net_Server_name[0] == '\0')
    {
      error = ER_NET_NO_SERVER_HOST;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
      return error;
    }

  /* you can envelope something useful into the request */
  or_pack_int (request, client_val);
  eid = css_send_request_to_server_with_buffer (net_Server_host, NET_SERVER_PING, request, OR_INT_SIZE, reply_buf,
						OR_INT_SIZE);
  if (eid == 0)
    {
      error = ER_NET_CANT_CONNECT_SERVER;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 2, net_Server_name, net_Server_host);
      return error;
    }

  error = css_receive_data_from_server_with_timeout (eid, &reply, &reply_size, timeout);
  if (error || reply == NULL)
    {
      COMPARE_AND_FREE_BUFFER (reply_buf, reply);
      error = ER_NET_SERVER_DATA_RECEIVE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
      return error;
    }

  /* you can get something useful from the server */
  if (server_val)
    {
      or_unpack_int (reply, server_val);
    }

  COMPARE_AND_FREE_BUFFER (reply_buf, reply);
  return error;
}

/*
 * net_client_ping_server_with_handshake -
 *
 * return:
 */
int
net_client_ping_server_with_handshake (int client_type, bool check_capabilities, int opt_cap)
{
  const char *client_release;
  char *server_release, *server_host, *server_handshake, *ptr;
  int error = NO_ERROR;
  OR_ALIGNED_BUF (REL_MAX_RELEASE_LENGTH + (OR_INT_SIZE * 2) + MAXHOSTNAMELEN) a_request;
  char *request = OR_ALIGNED_BUF_START (a_request);
  OR_ALIGNED_BUF (REL_MAX_RELEASE_LENGTH + (OR_INT_SIZE * 3) + MAXHOSTNAMELEN) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply), *reply_ptr;
  int reply_size = OR_ALIGNED_BUF_SIZE (a_reply);
  int eid, request_size, server_capabilities, server_bit_platform;
  int strlen1, strlen2;
  REL_COMPATIBILITY compat;

  if (net_Server_host[0] == '\0' || net_Server_name[0] == '\0')
    {
      error = ER_NET_NO_SERVER_HOST;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
      return error;
    }

  client_release = rel_release_string ();

  request_size = (or_packed_string_length (client_release, &strlen1) + (OR_INT_SIZE * 2)
		  + or_packed_string_length (boot_Host_name, &strlen2));
  ptr = or_pack_string_with_length (request, client_release, strlen1);
  ptr = or_pack_int (ptr, client_capabilities ());
  ptr = or_pack_int (ptr, rel_bit_platform ());
  ptr = or_pack_int (ptr, client_type);
  ptr = or_pack_string_with_length (ptr, boot_Host_name, strlen2);

  eid = css_send_request_to_server_with_buffer (net_Server_host, NET_SERVER_PING_WITH_HANDSHAKE, request, request_size,
						reply, reply_size);
  if (eid == 0)
    {
      error = ER_NET_CANT_CONNECT_SERVER;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 2, net_Server_name, net_Server_host);
      return error;
    }

  reply_ptr = reply;
  error = css_receive_data_from_server (eid, &reply_ptr, &reply_size);
  if (error)
    {
      COMPARE_AND_FREE_BUFFER (reply, reply_ptr);
      error = ER_NET_SERVER_DATA_RECEIVE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
      return error;
    }
  if (reply != reply_ptr)
    {
      error = ER_NET_UNUSED_BUFFER;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
      free_and_init (reply_ptr);
      return error;
    }

  ptr = or_unpack_string_nocopy (reply, &server_release);
  ptr = or_unpack_string_nocopy (ptr, &server_handshake);	/* for backward compatibility */
  ptr = or_unpack_int (ptr, &server_capabilities);
  ptr = or_unpack_int (ptr, &server_bit_platform);
  ptr = or_unpack_string_nocopy (ptr, &server_host);

  /* get the error code which was from the server if it exists */
  error = er_errid ();
  if (error != NO_ERROR)
    {
      return error;
    }

  /* check bits model */
  if (server_bit_platform != rel_bit_platform ())
    {
      error = ER_NET_DIFFERENT_BIT_PLATFORM;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 2, server_bit_platform, rel_bit_platform ());
      return error;
    }

  /* If we can't get the server version, we have to disconnect it. */
  if (server_release == NULL)
    {
      error = ER_NET_HS_UNKNOWN_SERVER_REL;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
      return error;
    }

  /* 
   * 1. get the result of compatibility check.
   * 2. check if the both capabilities of client and server are compatible.
   * 3. check if the server has a capability to make it compatible.
   */
  compat = rel_get_net_compatible (client_release, server_release);
  if ((check_capabilities == true || server_capabilities & NET_CAP_REMOTE_DISABLED)
      && check_server_capabilities (server_capabilities, client_type, rel_compare (client_release, server_release),
				    &compat, server_host, opt_cap) != server_capabilities)
    {
      error = ER_NET_SERVER_HAND_SHAKE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, net_Server_host);
      return error;
    }
  if (compat == REL_NOT_COMPATIBLE)
    {
      error = ER_NET_DIFFERENT_RELEASE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 2, server_release, client_release);
      return error;
    }

  return error;
}

#if defined(ENABLE_UNUSED_FUNCTION)
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
  css_send_request_to_server (net_Server_host, NET_SERVER_SHUTDOWN, NULL, 0);
}
#endif /* ENABLE_UNUSED_FUNCTION */

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

  /* don't really need to do this every time but bruce says its ok - we probably need to guarentee that a css_terminate 
   * is always called before this */
  error = css_client_init (prm_get_integer_value (PRM_ID_TCP_PORT_ID), dbname, hostname);
  if (error != NO_ERROR)
    {
      goto end;
    }

  /* since urgent_message_handler() doesn't do anything yet, just use the default handler provided by css which writes
   * things to the system console */

  /* set our host/server names for further css communication */
  if (hostname != NULL && strlen (hostname) <= MAXHOSTNAMELEN)
    {
      strcpy (net_Server_host, hostname);
      if (dbname != NULL && strlen (dbname) <= DB_MAX_IDENTIFIER_LENGTH)
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

  /* On error, flush any state that may have been initialized by css. This is important for the PC's since we must
   * shutdown Winsock after it has been opened by css_client_init. */
end:
  if (error)
    {
      css_terminate (false);
    }

  return error;
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
  css_terminate (false);
  return NO_ERROR;
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
net_client_send_data (char *host, unsigned int rc, char *databuf, int datasize)
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
  char *reply = NULL;
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
      if (error != NO_ERROR || reply == NULL)
	{
	  if (reply != NULL)
	    {
	      free_and_init (reply);
	    }
	  return set_server_error (error);
	}

      if (size != replysize)
	{
	  error = ER_NET_DATASIZE_MISMATCH;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 2, replysize, size);
	  replysize = size;
	  if (reply != NULL)
	    {
	      free_and_init (reply);
	    }
	  return set_server_error (error);
	}
      or_unpack_int (reply, action);
      free_and_init (reply);
    }

  return error;
}
