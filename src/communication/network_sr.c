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
 * network_sr.c - server side support functions.
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <string.h>

#include "server_interface.h"
#include "memory_alloc.h"
#include "system_parameter.h"
#include "network.h"
#include "boot_sr.h"
#include "network_interface_sr.h"
#include "query_list.h"
#include "thread_impl.h"
#include "critical_section.h"
#include "release_string.h"
#include "server_support.h"
#include "connection_sr.h"
#include "job_queue.h"
#include "connection_error.h"
#include "message_catalog.h"

#if defined(WINDOWS)
#include "wintcp.h"
#endif /* WINDOWS */

#if defined(DIAG_DEVEL)
#include "perf_monitor.h"
#endif /* DIAG_DEVEL */

#if defined(CUBRID_DEBUG)
#include "environment_variable.h"

#define LAST_ENTRY              SERVER_SHUTDOWN
#define UNKNOWN_REQUEST_STRING  "unknown request"

typedef struct net_request_buffer NET_REQ_BUF;
struct net_request_buffer
{
  const char *name;
  int request_count;
  int total_size_sent;
  int total_size_received;
  int elapsed_time;
};

static int net_Histo_call_count;
static NET_REQ_BUF net_Req_buffer[LAST_ENTRY + 1];
#endif /* CUBRID_DEBUG */

static int net_server_request (THREAD_ENTRY * thrd, unsigned int rid,
			       int request, int size, char *buffer);
static int net_server_conn_down (THREAD_ENTRY * thrd, CSS_THREAD_ARG arg);

#if defined(CUBRID_DEBUG)
static void net_trace_client_request (int rid, int request, int size,
				      char *buffer);
#endif /* CUBRID_DEBUG */

#if defined(CUBRID_DEBUG)
/*
 * net_trace_client_request () -
 *   return:
 *   rid(in):
 *   request(in):
 *   size(in):
 *   buffer(in):
 */
static void
net_trace_client_request (int rid, int request, int size, char *buffer)
{
  char *str;
  int i;
  static int enable = -1;
  static FILE *fp = NULL;
  CSS_CONN_ENTRY *conn;

  if (enable == -1)
    {
      str = envvar_get ("REQUEST_TRACE_FILE");
      if (str)
	{
	  enable = 1;
	  if (!strcmp (str, "stdout"))
	    {
	      fp = stdout;
	    }
	  else if (!strcmp (str, "stderr"))
	    {
	      fp = stderr;
	    }
	  else
	    {
	      fp = fopen (str, "w+");
	      if (!fp)
		{
		  fprintf (stderr, "Error: REQUEST_TRACE_FILE '%s'\n", str);
		  enable = 0;
		}
	    }
	}
      else
	{
	  enable = 0;
	}
    }

  if (!enable || !fp)
    {
      return;
    }

  switch (request)
    {
    case SERVER_PING_WITH_HANDSHAKE:
      str = "SERVER_PING_WITH_HANDSHAKE";
      break;
    case SERVER_BO_INIT_SERVER:
      str = "SERVER_BO_INIT_SERVER";
      break;
    case SERVER_BO_REGISTER_CLIENT:
      str = "SERVER_BO_REGISTER_CLIENT";
      break;
    case SERVER_BO_UNREGISTER_CLIENT:
      str = "SERVER_BO_UNREGISTER_CLIENT";
      break;
    case SERVER_BO_SIMULATE_SERVER_CRASH:
      str = "SERVER_BO_SIMULATE_SERVER_CRASH";
      break;
    case SERVER_BO_SIMULATE_SERVER:
      str = "SERVER_BO_SIMULATE_SERVER";
      break;
    case SERVER_BO_KILL_SERVER:
      str = "SERVER_BO_KILL_SERVER";
      break;
    case SERVER_BO_BACKUP:
      str = "SERVER_BO_BACKUP";
      break;
    case SERVER_BO_ADD_VOLEXT:
      str = "SERVER_BO_ADD_VOLEXT";
      break;
    case SERVER_BO_CHECK_DBCONSISTENCY:
      str = "SERVER_BO_CHECK_DBCONSISTENCY";
      break;
    case SERVER_BO_FIND_NPERM_VOLS:
      str = "SERVER_BO_FIND_NPERM_VOLS";
      break;
    case SERVER_BO_FIND_NTEMP_VOLS:
      str = "SERVER_BO_FIND_NTEMP_VOLS";
      break;
    case SERVER_BO_FIND_LAST_TEMP:
      str = "SERVER_BO_FIND_LAST_TEMP";
      break;
    case SERVER_TM_SERVER_COMMIT:
      str = "SERVER_TM_SERVER_COMMIT";
      break;
    case SERVER_TM_SERVER_ABORT:
      str = "SERVER_TM_SERVER_ABORT";
      break;
    case SERVER_TM_SERVER_START_TOPOP:
      str = "SERVER_TM_SERVER_START_TOPOP";
      break;
    case SERVER_TM_SERVER_END_TOPOP:
      str = "SERVER_TM_SERVER_END_TOPOP";
      break;
    case SERVER_TM_SERVER_SAVEPOINT:
      str = "SERVER_TM_SERVER_SAVEPOINT";
      break;
    case SERVER_TM_SERVER_PARTIAL_ABORT:
      str = "SERVER_TM_SERVER_PARTIAL_ABORT";
      break;
    case SERVER_TM_SERVER_HAS_UPDATED:
      str = "SERVER_TM_SERVER_HAS_UPDATED";
      break;
    case SERVER_TM_SERVER_ISACTIVE_AND_HAS_UPDATED:
      str = "SERVER_TM_SERVER_ISACTIVE_AND_HAS_UPDATED";
      break;
    case SERVER_TM_ISBLOCKED:
      str = "SERVER_TM_ISBLOCKED";
      break;
    case SERVER_TM_SERVER_GET_GTRINFO:
      str = "SERVER_TM_SERVER_GET_GTRINFO";
      break;
    case SERVER_TM_SERVER_SET_GTRINFO:
      str = "SERVER_TM_SERVER_SET_GTRINFO";
      break;
    case SERVER_TM_SERVER_2PC_START:
      str = "SERVER_TM_SERVER_2PC_START";
      break;
    case SERVER_TM_SERVER_2PC_PREPARE:
      str = "SERVER_TM_SERVER_2PC_PREPARE";
      break;
    case SERVER_TM_SERVER_2PC_RECOVERY_PREPARED:
      str = "SERVER_TM_SERVER_2PC_RECOVERY_PREPARED";
      break;
    case SERVER_TM_SERVER_2PC_ATTACH_GT:
      str = "SERVER_TM_SERVER_2PC_ATTACH_GT";
      break;
    case SERVER_TM_SERVER_2PC_PREPARE_GT:
      str = "SERVER_TM_SERVER_2PC_PREPARE_GT";
      break;
    case SERVER_LC_FETCH:
      str = "SERVER_LC_FETCH";
      break;
    case SERVER_LC_FETCHALL:
      str = "SERVER_LC_FETCHALL";
      break;
    case SERVER_LC_FETCH_LOCKSET:
      str = "SERVER_LC_FETCH_LOCKSET";
      break;
    case SERVER_LC_FETCH_ALLREFS_LOCKSET:
      str = "SERVER_LC_FETCH_ALLREFS_LOCKSET";
      break;
    case SERVER_LC_GET_CLASS:
      str = "SERVER_LC_GET_CLASS";
      break;
    case SERVER_LC_FIND_CLASSOID:
      str = "SERVER_LC_FIND_CLASSOID";
      break;
    case SERVER_LC_DOESEXIST:
      str = "SERVER_LC_DOESEXIST";
      break;
    case SERVER_LC_FORCE:
      str = "SERVER_LC_FORCE";
      break;
    case SERVER_LC_RESERVE_CLASSNAME:
      str = "SERVER_LC_RESERVE_CLASSNAME";
      break;
    case SERVER_LC_DELETE_CLASSNAME:
      str = "SERVER_LC_DELETE_CLASSNAME";
      break;
    case SERVER_LC_RENAME_CLASSNAME:
      str = "SERVER_LC_RENAME_CLASSNAME";
      break;
    case SERVER_LC_ASSIGN_OID:
      str = "SERVER_LC_ASSIGN_OID";
      break;
    case SERVER_LC_ASSIGN_OID_BATCH:
      str = "SERVER_LC_ASSIGN_OID_BATCH";
      break;
    case SERVER_LC_NOTIFY_ISOLATION_INCONS:
      str = "SERVER_LC_NOTIFY_ISOLATION_INCONS";
      break;
    case SERVER_LC_FIND_LOCKHINT_CLASSOIDS:
      str = "SERVER_LC_FIND_LOCKHINT_CLASSOIDS";
      break;
    case SERVER_LC_FETCH_LOCKHINT_CLASSES:
      str = "_LOCKHINT_CLASSES";
      break;
    case SERVER_LC_REM_CLASS_FROM_INDEX:
      str = "SERVER_LC_REM_CLASS_FROM_INDEX";
      break;
    case SERVER_HEAP_CREATE:
      str = "SERVER_HEAP_CREATE";
      break;
    case SERVER_HEAP_DESTROY:
      str = "SERVER_HEAP_DESTROY";
      break;
    case SERVER_HEAP_DESTROY_WHEN_NEW:
      str = "SERVER_HEAP_DESTROY_WHEN_NEW";
      break;
    case SERVER_LARGEOBJMGR_CREATE:
      str = "SERVER_LARGEOBJMGR_CREATE";
      break;
    case SERVER_LARGEOBJMGR_READ:
      str = "SERVER_LARGEOBJMGR_READ";
      break;
    case SERVER_LARGEOBJMGR_WRITE:
      str = "SERVER_LARGEOBJMGR_WRITE";
      break;
    case SERVER_LARGEOBJMGR_INSERT:
      str = "SERVER_LARGEOBJMGR_INSERT";
      break;
    case SERVER_LARGEOBJMGR_DESTROY:
      str = "SERVER_LARGEOBJMGR_DESTROY";
      break;
    case SERVER_LARGEOBJMGR_DELETE:
      str = "SERVER_LARGEOBJMGR_DELETE";
      break;
    case SERVER_LARGEOBJMGR_APPEND:
      str = "SERVER_LARGEOBJMGR_APPEND";
      break;
    case SERVER_LARGEOBJMGR_TRUNCATE:
      str = "SERVER_LARGEOBJMGR_TRUNCATE";
      break;
    case SERVER_LARGEOBJMGR_COMPRESS:
      str = "SERVER_LARGEOBJMGR_COMPRESS";
      break;
    case SERVER_LARGEOBJMGR_LENGTH:
      str = "SERVER_LARGEOBJMGR_LENGTH:";
      break;
    case SERVER_LOG_RESET_WAITSECS:
      str = "SERVER_LOG_RESET_WAITSECS";
      break;
    case SERVER_LOG_RESET_ISOLATION:
      str = "SERVER_LOG_RESET_ISOLATION";
      break;
    case SERVER_LOG_SET_INTERRUPT:
      str = "SERVER_LOG_SET_INTERRUPT";
      break;
    case SERVER_LOG_CLIENT_UNDO:
      str = "SERVER_LOG_CLIENT_UNDO";
      break;
    case SERVER_LOG_CLIENT_POSTPONE:
      str = "SERVER_LOG_CLIENT_POSTPONE";
      break;
    case SERVER_LOG_HAS_FINISHED_CLIENT_POSTPONE:
      str = "SERVER_LOG_HAS_FINISHED_CLIENT_POSTPONE";
      break;
    case SERVER_LOG_HAS_FINISHED_CLIENT_UNDO:
      str = "SERVER_LOG_HAS_FINISHED_CLIENT_UNDO";
      break;
    case SERVER_LOG_CLIENT_GET_FIRST_POSTPONE:
      str = "SERVER_LOG_CLIENT_GET_FIRST_POSTPONE";
      break;
    case SERVER_LOG_CLIENT_GET_FIRST_UNDO:
      str = "SERVER_LOG_CLIENT_GET_FIRST_UNDO";
      break;
    case SERVER_LOG_CLIENT_GET_NEXT_POSTPONE:
      str = "SERVER_LOG_CLIENT_GET_NEXT_POSTPONE";
      break;
    case SERVER_LOG_CLIENT_GET_NEXT_UNDO:
      str = "SERVER_LOG_CLIENT_GET_NEXT_UNDO";
      break;
    case SERVER_LOG_CLIENT_UNKNOWN_STATE_ABORT_GET_FIRST_UNDO:
      str = "SERVER_LOG_CLIENT_UNKNOWN_STATE_ABORT_GET_FIRST_UNDO";
      break;
    case SERVER_LOG_GETPACK_TRANTB:
      str = "SERVER_LOG_GETPACK_TRANTB";
      break;
    case SERVER_LK_DUMP:
      str = "SERVER_LK_DUMP";
      break;
    case SERVER_BTREE_ADDINDEX:
      str = "SERVER_BTREE_ADDINDEX";
      break;
    case SERVER_BTREE_DELINDEX:
      str = "SERVER_BTREE_DELINDEX";
      break;
    case SERVER_BTREE_LOADINDEX:
      str = "SERVER_BTREE_LOADINDEX";
      break;
    case SERVER_BTREE_FIND_UNIQUE:
      str = "SERVER_BTREE_FIND_UNIQUE";
      break;
    case SERVER_BTREE_CLASS_UNIQUE_TEST:
      str = "SERVER_BTREE_CLASS_UNIQUE_TEST";
      break;
    case SERVER_DISK_TOTALPGS:
      str = "SERVER_DISK_TOTALPGS";
      break;
    case SERVER_DISK_FREEPGS:
      str = "SERVER_DISK_FREEPGS";
      break;
    case SERVER_DISK_REMARKS:
      str = "SERVER_DISK_REMARKS";
      break;
    case SERVER_DISK_PURPOSE:
      str = "SERVER_DISK_PURPOSE";
      break;
    case SERVER_DISK_PURPOSE_TOTALPGS_AND_FREEPGS:
      str = "SERVER_DISK_PURPOSE_TOTALPGS_AND_FREEPGS";
      break;
    case SERVER_DISK_VLABEL:
      str = "SERVER_DISK_VLABEL";
      break;
    case SERVER_QST_SERVER_GET_STATISTICS:
      str = "SERVER_QST_SERVER_GET_STATISTICS";
      break;
    case SERVER_QST_UPDATE_CLASS_STATISTICS:
      str = "SERVER_QST_UPDATE_CLASS_STATISTICS";
      break;
    case SERVER_QST_UPDATE_STATISTICS:
      str = "SERVER_QST_UPDATE_STATISTICS";
      break;
    case SERVER_QM_QUERY_PREPARE:
      str = "SERVER_QM_QUERY_PREPARE";
      break;
    case SERVER_QM_QUERY_EXECUTE:
      str = "SERVER_QM_QUERY_EXECUTE";
      break;
    case SERVER_QM_QUERY_PREPARE_AND_EXECUTE:
      str = "SERVER_QM_QUERY_PREPARE_AND_EXECUTE";
      break;
    case SERVER_QM_QUERY_EXECUTE_ASYNC:
      str = "SERVER_QM_QUERY_EXECUTE_ASYNC";
      break;
    case SERVER_QM_QUERY_PREPARE_AND_EXECUTE_ASYNC:
      str = "SERVER_QM_QUERY_PREPARE_AND_EXECUTE_ASYNC";
      break;
    case SERVER_QM_QUERY_SYNC:
      str = "SERVER_QM_QUERY_SYNC";
      break;
    case SERVER_QM_GET_QUERY_INFO:
      str = "SERVER_QM_GET_QUERY_INFO";
      break;
    case SERVER_QM_QUERY_END:
      str = "SERVER_QM_QUERY_END";
      break;
    case SERVER_QM_QUERY_DROP_PLAN:
      str = "SERVER_QM_QUERY_DROP_PLAN";
      break;
    case SERVER_QM_QUERY_DROP_ALL_PLANS:
      str = "SERVER_QM_QUERY_DROP_ALL_PLANS";
      break;
    case SERVER_QM_QUERY_DUMP_PLANS:
      str = "SERVER_QM_QUERY_DUMP_PLANS";
      break;
    case SERVER_QM_QUERY_DUMP_CACHE:
      str = "SERVER_QM_QUERY_DUMP_CACHE";
      break;
    case SERVER_LS_GET_LIST_FILE_PAGE:
      str = "SERVER_LS_GET_LIST_FILE_PAGE";
      break;
    case SERVER_MNT_SERVER_START_STATS:
      str = "SERVER_MNT_SERVER_START_STATS";
      break;
    case SERVER_MNT_SERVER_STOP_STATS:
      str = "SERVER_MNT_SERVER_STOP_STATS";
      break;
    case SERVER_MNT_SERVER_RESET_STATS:
      str = "SERVER_MNT_SERVER_RESET_STATS";
      break;
    case SERVER_MNT_SERVER_COPY_STATS:
      str = "SERVER_MNT_SERVER_COPY_STATS";
      break;
    case SERVER_CT_CAN_ACCEPT_NEW_REPR:
      str = "SERVER_CT_CAN_ACCEPT_NEW_REPR";
      break;
    case SERVER_TEST_PERFORMANCE:
      str = "SERVER_TEST_PERFORMANCE";
      break;
    case SERVER_SET_CLIENT_TIMEOUT:
      str = "SERVER_SET_CLIENT_TIMEOUT";
      break;
    case SERVER_RESTART_EVENT_HANDLER:
      str = "SERVER_RESTART_EVENT_HANDLER";
      break;
    case SERVER_CSS_KILL_TRANSACTION:
      str = "SERVER_CSS_KILL_TRANSACTION";
      break;
    case SERVER_SHUTDOWN:
      str = "SERVER_SHUTDOWN";
      break;
    case SERVER_QPROC_GET_SYS_TIMESTAMP:
      str = "SERVER_QP_GET_SYS_TIMESTAMP";
      break;
    case SERVER_QPROC_GET_CURRENT_VALUE:
      str = "SERVER_QP_GET_CURRENT_VALUE";
      break;
    case SERVER_QPROC_GET_NEXT_VALUE:
      str = "SERVER_QP_GET_NEXT_VALUE";
      break;
    case SERVER_HEAP_GET_CLASS_NOBJS_AND_NPAGES:
      str = "SERVER_HEAP_GET_CLASS_NOBJS_AND_NPAGES";
      break;
    case SERVER_HEAP_HAS_INSTANCE:
      str = "SERVER_HEAP_HAS_INSTANCE";
      break;
    case SERVER_BTREE_GET_STATISTICS:
      str = "SERVER_BTREE_GET_STATISTICS";
      break;
    case SERVER_QPROC_GET_SERVER_INFO:
      str = "SERVER_QP_GET_SEVER_INFO";
      break;
    case SERVER_PRM_SET_PARAMETERS:
      str = "SERVER_SET_PARAMETERS";
      break;
    case SERVER_PRM_GET_PARAMETERS:
      str = "SERVER_GET_PARAMETERS";
      break;
    case SERVER_TM_LOCAL_TRANSACTION_ID:
      str = "SERVER_TM_LOCAL_TRANSACTION_ID";
      break;
    case SERVER_JSP_GET_SERVER_PORT:
      str = "SERVER_JSP_GET_SERVER_PORT";
      break;
    case SERVER_REPL_INFO:
      str = "SERVER_REPL_INFO";
      break;
    case SERVER_REPL_LOG_GET_APPEND_LSA:
      str = "SERVER_REPL_LOG_GET_APPEND_LSA";
      break;
    default:
      str = "Unacceptable request";
      break;
    }

  fprintf (fp,
	   "(pid %d) ClientRequest: rid %d, request %s(%d), size %d, buffer",
	   getpid (), rid, str, request, size);

  for (i = 0; i < ((size > 128) ? 128 : size); i++)
    {
      fprintf (fp, " %02x", (unsigned char) buffer[i]);
    }

  if (size > 128)
    {
      fprintf (fp, "......(");
    }
  else
    {
      fprintf (fp, "(");
    }

  for (i = 0; i < ((size > 128) ? 128 : size); i++)
    {
      fprintf (fp, "%c", (isprint (buffer[i]) ? buffer[i] : '_'));
    }

  if (size > 128)
    {
      fprintf (fp, "......) ");
    }
  else
    {
      fprintf (fp, ") ");
    }

  conn = thread_get_current_conn_entry ();

  fprintf (fp,
	   "(pid %d) ConnectionInfo: rid %d, status %d, tid %d, cid %d, error %d\n",
	   getpid (), conn->request_id, conn->status, conn->transaction_id,
	   conn->client_id, conn->db_error);
  fflush (fp);
}

/*
 * histo_setup_names () -
 *   return:
 */
void
histo_setup_names (void)
{
  int i;

  net_Histo_call_count = 0;

  for (i = 0; i < DIM (net_Req_buffer); i++)
    {
      net_Req_buffer[i].name = UNKNOWN_REQUEST_STRING;
      net_Req_buffer[i].request_count = 0;
      net_Req_buffer[i].total_size_sent = 0;
      net_Req_buffer[i].total_size_received = 0;
      net_Req_buffer[i].elapsed_time = 0;
    }

  net_Req_buffer[SERVER_PING_WITH_HANDSHAKE].name = "PING_WITH_HANDSHAKE";

  net_Req_buffer[SERVER_BO_INIT_SERVER].name = "BO_INIT_SERVER";
  net_Req_buffer[SERVER_BO_RESTART_SERVER].name = "BO_RESTART_SERVER";
  net_Req_buffer[SERVER_BO_REGISTER_CLIENT].name = "BO_REGISTER_CLIENT";
  net_Req_buffer[SERVER_BO_UNREGISTER_CLIENT].name = "BO_UNREGISTER_CLIENT";
  net_Req_buffer[SERVER_BO_SIMULATE_SERVER_CRASH].name =
    "BO_SIMULATE_SERVER_CRASH";
  net_Req_buffer[SERVER_BO_BACKUP].name = "BO_BACKUP";
  net_Req_buffer[SERVER_BO_ADD_VOLEXT].name = "BO_ADD_VOLEXT";
  net_Req_buffer[SERVER_BO_CHECK_DBCONSISTENCY].name =
    "BO_CHECK_DBCONSISTENCY";
  net_Req_buffer[SERVER_BO_FIND_NPERM_VOLS].name = "BO_FIND_NPERM_VOLS";

  net_Req_buffer[SERVER_TM_SERVER_COMMIT].name = "TM_SERVER_COMMIT";
  net_Req_buffer[SERVER_TM_SERVER_ABORT].name = "TM_SERVER_ABORT";
  net_Req_buffer[SERVER_TM_SERVER_START_TOPOP].name = "TM_SERVER_START_TOPOP";
  net_Req_buffer[SERVER_TM_SERVER_END_TOPOP].name = "TM_SERVER_END_TOPOP";
  net_Req_buffer[SERVER_TM_SERVER_SAVEPOINT].name = "TM_SERVER_SAVEPOINT";
  net_Req_buffer[SERVER_TM_SERVER_PARTIAL_ABORT].name =
    "TM_SERVER_PARTIAL_ABORT";
  net_Req_buffer[SERVER_TM_SERVER_HAS_UPDATED].name = "TM_SERVER_HAS_UPDATED";
  net_Req_buffer[SERVER_TM_SERVER_ISACTIVE_AND_HAS_UPDATED].name =
    "TM_SERVER_ISACTIVE_AND_HAS_UPDATED";
  net_Req_buffer[SERVER_TM_ISBLOCKED].name = "TM_ISBLOCKED";
  net_Req_buffer[SERVER_TM_SERVER_2PC_ATTACH_GT].name =
    "TM_SERVER_2PC_ATTACH_GT";
  net_Req_buffer[SERVER_TM_SERVER_2PC_PREPARE_GT].name =
    "TM_SERVER_2PC_PREPARE_GT";

  net_Req_buffer[SERVER_LC_FETCH].name = "LC_FETCH";
  net_Req_buffer[SERVER_LC_FETCHALL].name = "LC_FETCHALL";
  net_Req_buffer[SERVER_LC_FETCH_LOCKSET].name = "LC_FETCH_LOCKSET";
  net_Req_buffer[SERVER_LC_FETCH_ALLREFS_LOCKSET].name =
    "LC_FETCH_ALLREFS_LOCKSET";
  net_Req_buffer[SERVER_LC_GET_CLASS].name = "LC_GET_CLASS";
  net_Req_buffer[SERVER_LC_FIND_CLASSOID].name = "LC_FIND_CLASSOID";
  net_Req_buffer[SERVER_LC_DOESEXIST].name = "LC_DOESEXIST";
  net_Req_buffer[SERVER_LC_FORCE].name = "LC_FORCE";
  net_Req_buffer[SERVER_LC_RESERVE_CLASSNAME].name = "LC_RESERVE_CLASSNAME";
  net_Req_buffer[SERVER_LC_DELETE_CLASSNAME].name = "LC_DELETE_CLASSNAME";
  net_Req_buffer[SERVER_LC_RENAME_CLASSNAME].name = "LC_RENAME_CLASSNAME";
  net_Req_buffer[SERVER_LC_ASSIGN_OID].name = "LC_ASSIGN_OID";
  net_Req_buffer[SERVER_LC_NOTIFY_ISOLATION_INCONS].name =
    "LC_NOTIFY_ISOLATION_INCONS";
  net_Req_buffer[SERVER_LC_FIND_LOCKHINT_CLASSOIDS].name =
    "LC_FIND_LOCKHINT_CLASSOIDS";
  net_Req_buffer[SERVER_LC_FETCH_LOCKHINT_CLASSES].name =
    "LC_FETCH_LOCKHINT_CLASSES";

  net_Req_buffer[SERVER_HEAP_CREATE].name = "HEAP_CREATE";
  net_Req_buffer[SERVER_HEAP_DESTROY].name = "HEAP_DESTROY";
  net_Req_buffer[SERVER_HEAP_DESTROY_WHEN_NEW].name = "HEAP_DESTROY_WHEN_NEW";

  net_Req_buffer[SERVER_LARGEOBJMGR_CREATE].name = "LARGEOBJMGR_CREATE";
  net_Req_buffer[SERVER_LARGEOBJMGR_READ].name = "LARGEOBJMGR_READ";
  net_Req_buffer[SERVER_LARGEOBJMGR_WRITE].name = "LARGEOBJMGR_WRITE";
  net_Req_buffer[SERVER_LARGEOBJMGR_INSERT].name = "LARGEOBJMGR_INSERT";
  net_Req_buffer[SERVER_LARGEOBJMGR_DESTROY].name = "LARGEOBJMGR_DESTROY";
  net_Req_buffer[SERVER_LARGEOBJMGR_DELETE].name = "LARGEOBJMGR_DELETE";
  net_Req_buffer[SERVER_LARGEOBJMGR_APPEND].name = "LARGEOBJMGR_APPEND";
  net_Req_buffer[SERVER_LARGEOBJMGR_TRUNCATE].name = "LARGEOBJMGR_TRUNCATE";
  net_Req_buffer[SERVER_LARGEOBJMGR_COMPRESS].name = "LARGEOBJMGR_COMPRESS";
  net_Req_buffer[SERVER_LARGEOBJMGR_LENGTH].name = "LARGEOBJMGR_LENGTH";

  net_Req_buffer[SERVER_LOG_RESET_WAITSECS].name = "LOG_RESET_WAITSECS";
  net_Req_buffer[SERVER_LOG_RESET_ISOLATION].name = "LOG_RESET_ISOLATION";
  net_Req_buffer[SERVER_LOG_SET_INTERRUPT].name = "LOG_SET_INTERRUPT";
  net_Req_buffer[SERVER_LOG_CLIENT_UNDO].name = "LOG_CLIENT_UNDO";
  net_Req_buffer[SERVER_LOG_CLIENT_POSTPONE].name = "LOG_CLIENT_POSTPONE";
  net_Req_buffer[SERVER_LOG_HAS_FINISHED_CLIENT_POSTPONE].name =
    "LOG_HAS_FINISHED_CLIENT_POSTPONE";
  net_Req_buffer[SERVER_LOG_HAS_FINISHED_CLIENT_UNDO].name =
    "LOG_HAS_FINISHED_CLIENT_UNDO";
  net_Req_buffer[SERVER_LOG_CLIENT_GET_FIRST_POSTPONE].name =
    "LOG_CLIENT_GET_FIRST_POSTPONE";
  net_Req_buffer[SERVER_LOG_CLIENT_GET_FIRST_UNDO].name =
    "LOG_CLIENT_GET_FIRST_UNDO";
  net_Req_buffer[SERVER_LOG_CLIENT_GET_NEXT_POSTPONE].name =
    "LOG_CLIENT_GET_NEXT_POSTPONE";
  net_Req_buffer[SERVER_LOG_CLIENT_GET_NEXT_UNDO].name =
    "LOG_CLIENT_GET_NEXT_UNDO";
  net_Req_buffer[SERVER_LOG_CLIENT_UNKNOWN_STATE_ABORT_GET_FIRST_UNDO].name =
    "LOG_CLIENT_UNKNOWN_STATE_ABORT_GET_FIRST_UNDO";

  net_Req_buffer[SERVER_LK_DUMP].name = "LK_DUMP";

  net_Req_buffer[SERVER_BTREE_ADDINDEX].name = "BTREE_ADDINDEX";
  net_Req_buffer[SERVER_BTREE_DELINDEX].name = "BTREE_DELINDEX";
  net_Req_buffer[SERVER_BTREE_LOADINDEX].name = "BTREE_LOADINDEX";

  net_Req_buffer[SERVER_BTREE_FIND_UNIQUE].name = "BTREE_FIND_FIND_UNIQUE";
  net_Req_buffer[SERVER_BTREE_CLASS_UNIQUE_TEST].name =
    "BTREE_CLASS_UNIQUE_TEST";

  net_Req_buffer[SERVER_DISK_TOTALPGS].name = "DISK_TOTALPGS";
  net_Req_buffer[SERVER_DISK_FREEPGS].name = "DISK_FREEPGS";
  net_Req_buffer[SERVER_DISK_REMARKS].name = "DISK_REMARKS";
  net_Req_buffer[SERVER_DISK_PURPOSE].name = "DISK_PURPOSE";
  net_Req_buffer[SERVER_DISK_PURPOSE_TOTALPGS_AND_FREEPGS].name =
    "DISK_PURPOSE_TOTALPGS_AND_FREEPGS";
  net_Req_buffer[SERVER_DISK_VLABEL].name = "DISK_VLABEL";

  net_Req_buffer[SERVER_QST_SERVER_GET_STATISTICS].name =
    "QST_SERVER_GET_STATISTICS";
  net_Req_buffer[SERVER_QST_UPDATE_CLASS_STATISTICS].name =
    "QST_UPDATE_CLASS_STATISTICS";
  net_Req_buffer[SERVER_QST_UPDATE_STATISTICS].name = "QST_UPDATE_STATISTICS";

  net_Req_buffer[SERVER_QM_QUERY_PREPARE].name = "QM_QUERY_PREPARE";
  net_Req_buffer[SERVER_QM_QUERY_EXECUTE].name = "QM_QUERY_EXECUTE";
  net_Req_buffer[SERVER_QM_QUERY_PREPARE_AND_EXECUTE].name =
    "QM_QUERY_PREPARE_AND_EXECUTE";
  net_Req_buffer[SERVER_QM_QUERY_END].name = "QM_QUERY_END";
  net_Req_buffer[SERVER_QM_QUERY_DROP_PLAN].name = "QM_QUERY_DROP_PLAN";
  net_Req_buffer[SERVER_QM_QUERY_DROP_ALL_PLANS].name =
    "QM_QUERY_DROP_ALL_PLANS";

  net_Req_buffer[SERVER_LS_GET_LIST_FILE_PAGE].name = "LS_GET_LIST_FILE_PAGE";

  net_Req_buffer[SERVER_MNT_SERVER_START_STATS].name =
    "MNT_SERVER_START_STATS";
  net_Req_buffer[SERVER_MNT_SERVER_STOP_STATS].name = "MNT_SERVER_STOP_STATS";
  net_Req_buffer[SERVER_MNT_SERVER_RESET_STATS].name =
    "MNT_SERVER_RESET_STATS";
  net_Req_buffer[SERVER_MNT_SERVER_COPY_STATS].name = "MNT_SERVER_COPY_STATS";

  net_Req_buffer[SERVER_CT_CAN_ACCEPT_NEW_REPR].name =
    "CT_CAN_ACCEPT_NEW_REPR";

  net_Req_buffer[SERVER_BO_KILL_SERVER].name = "BO_KILL_SERVER";
  net_Req_buffer[SERVER_BO_SIMULATE_SERVER].name = "BO_SIMULATE_SERVER";
  net_Req_buffer[SERVER_TEST_PERFORMANCE].name = "TEST_PERFORMANCE";

  net_Req_buffer[SERVER_SET_CLIENT_TIMEOUT].name = "SET_CLIENT_TIMEOUT";
  net_Req_buffer[SERVER_RESTART_EVENT_HANDLER].name =
    "ER_RESTART_EVENT_HANDLER";
  net_Req_buffer[SERVER_CSS_KILL_TRANSACTION].name = "CSS_KILL_TRANSACTION";
  net_Req_buffer[SERVER_LOG_GETPACK_TRANTB].name = "LOG_GETPACK_TRANTB";

  net_Req_buffer[SERVER_LC_ASSIGN_OID_BATCH].name = "LC_ASSIGN_OID_BATCH";

  net_Req_buffer[SERVER_BO_FIND_NTEMP_VOLS].name = "BO_FIND_NTEMP_VOLS";
  net_Req_buffer[SERVER_BO_FIND_LAST_TEMP].name = "BO_FIND_LAST_TEMP";

  net_Req_buffer[SERVER_LC_REM_CLASS_FROM_INDEX].name =
    "LC_REM_CLASS_FROM_INDEX";

  net_Req_buffer[SERVER_QM_QUERY_SYNC].name = "QM_QUERY_SYNC";
  net_Req_buffer[SERVER_QM_GET_QUERY_INFO].name = "QM_GET_QUERY_INFO";
  net_Req_buffer[SERVER_QM_QUERY_EXECUTE_ASYNC].name =
    "QM_QUERY_EXECUTE_ASYNC";
  net_Req_buffer[SERVER_QM_QUERY_PREPARE_AND_EXECUTE_ASYNC].name =
    "QM_QUERY_PREPARE_AND_EXECUTE_ASYNC";

  net_Req_buffer[SERVER_QPROC_GET_SYS_TIMESTAMP].name =
    "QP_GET_SYS_TIMESTAMP";
  net_Req_buffer[SERVER_QPROC_GET_CURRENT_VALUE].name =
    "QP_GET_CURRENT_VALUE";
  net_Req_buffer[SERVER_QPROC_GET_NEXT_VALUE].name = "QP_GET_NEXT_VALUE";

  net_Req_buffer[SERVER_HEAP_GET_CLASS_NOBJS_AND_NPAGES].name =
    "HEAP_GET_CLASS_NOBJS_AND_NPAGES";
  net_Req_buffer[SERVER_BTREE_GET_STATISTICS].name = "BF_GET_STATISTICS";

  net_Req_buffer[SERVER_PING_WITH_HANDSHAKE].name = "PING_WITH_HANDSHAKE";

  net_Req_buffer[SERVER_SHUTDOWN].name = "SHUTDOWN";
}

/*
 * histo_print () -
 *   return:
 */
void
histo_print (void)
{
  int i, found = 0, total_requests = 0, total_size_sent = 0;
  int total_size_received = 0;
  float server_time, total_server_time = 0;
  float avg_response_time, avg_client_time;

  fprintf (stdout, "\nHistogram of client requests:\n");
  fprintf (stdout, "%-31s %6s  %10s %10s , %10s \n",
	   "Name", "Rcount", "Sent size", "Recv size", "Server time");

  for (i = 0; i < DIM (net_Req_buffer); i++)
    {
      if (net_Req_buffer[i].request_count)
	{
	  found = 1;
	  server_time = ((float) net_Req_buffer[i].elapsed_time / 1000000 /
			 (float) (net_Req_buffer[i].request_count));
	  fprintf (stdout, "%-29s %6d X %10d+%10d b, %10.6f s\n",
		   net_Req_buffer[i].name, net_Req_buffer[i].request_count,
		   net_Req_buffer[i].total_size_sent,
		   net_Req_buffer[i].total_size_received, server_time);
	  total_requests += net_Req_buffer[i].request_count;
	  total_size_sent += net_Req_buffer[i].total_size_sent;
	  total_size_received += net_Req_buffer[i].total_size_received;
	  total_server_time +=
	    (server_time * net_Req_buffer[i].request_count);
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
}

/*
 * histo_add_entry () -
 *   return:
 *   request(in):
 *   data_sent(in):
 */
void
histo_add_entry (int request, int data_sent)
{
  if (request > LAST_ENTRY)
    {
      return;
    }

  net_Req_buffer[request].request_count++;
  net_Req_buffer[request].total_size_sent += data_sent;

  net_Histo_call_count++;
}
#endif /* CUBRID_DEBUG */

/*
 * net_server_request () - The main server request dispatch handler
 *   return: error status
 *   thrd(in): this thread handle
 *   rid(in): CSS request id
 *   request(in): request constant
 *   size(in): size of argument buffer
 *   buffer(in): argument buffer
 */
static int
net_server_request (THREAD_ENTRY * thrd, unsigned int rid, int request,
		    int size, char *buffer)
{
#if defined(DIAG_DEVEL)
  struct timeval diag_start_time, diag_end_time;
#endif /* DIAG_DEVEL */
  int status = CSS_NO_ERRORS;

  if (buffer == NULL && size > 0)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NET_CANT_ALLOC_BUFFER, 0);
      return_error_to_client (rid);
      status = CSS_UNPLANNED_SHUTDOWN;
    }
  else
    {
#if defined(CUBRID_DEBUG)
      net_trace_client_request (rid, request, size, buffer);
      histo_add_entry (request, size);
#endif /* CUBRID_DEBUG */

      switch (request)
	{
	case SERVER_PING_WITH_HANDSHAKE:
	  status = server_ping_with_handshake (rid, buffer, size);
	  break;
	case SERVER_BO_INIT_SERVER:
	  sboot_initialize_server (thrd, rid, buffer, size);
	  break;
	case SERVER_BO_REGISTER_CLIENT:
	  sboot_register_client (thrd, rid, buffer, size);
	  break;
	case SERVER_BO_UNREGISTER_CLIENT:
	  sboot_notify_unregister_client (thrd, rid, buffer, size);
	  status = CSS_PLANNED_SHUTDOWN;
	  break;
	case SERVER_BO_BACKUP:
	  sboot_backup (thrd, rid, buffer, size);
	  break;
	case SERVER_BO_ADD_VOLEXT:
	  sboot_add_volume_extension (thrd, rid, buffer, size);
	  break;
	case SERVER_BO_CHECK_DBCONSISTENCY:
	  sboot_check_db_consistency (thrd, rid, buffer, size);
	  break;
	case SERVER_BO_FIND_NPERM_VOLS:
	  sboot_find_number_permanent_volumes (thrd, rid, buffer, size);
	  break;
	case SERVER_BO_FIND_NTEMP_VOLS:
	  sboot_find_number_temp_volumes (thrd, rid, buffer, size);
	  break;
	case SERVER_BO_FIND_LAST_TEMP:
	  sboot_find_last_temp (thrd, rid, buffer, size);
	  break;

	case SERVER_TM_SERVER_COMMIT:
#if defined(DIAG_DEVEL)
	  SET_DIAG_VALUE (diag_executediag, DIAG_OBJ_TYPE_CONN_CLI_REQUEST, 1,
			  DIAG_VAL_SETTYPE_INC, NULL);
#endif /* DIAG_DEVEL */
	  stran_server_commit (thrd, rid, buffer, size);
	  break;
	case SERVER_TM_SERVER_ABORT:
	  stran_server_abort (thrd, rid, buffer, size);
#if defined(DIAG_DEVEL)
	  SET_DIAG_VALUE (diag_executediag, DIAG_OBJ_TYPE_CONN_CLI_REQUEST, 1,
			  DIAG_VAL_SETTYPE_INC, NULL);
#endif /* DIAG_DEVEL */
	  break;
	case SERVER_TM_SERVER_START_TOPOP:
	  stran_server_start_topop (thrd, rid, buffer, size);
	  break;
	case SERVER_TM_SERVER_END_TOPOP:
	  stran_server_end_topop (thrd, rid, buffer, size);
	  break;
	case SERVER_TM_SERVER_SAVEPOINT:
	  stran_server_savepoint (thrd, rid, buffer, size);
	  break;
	case SERVER_TM_SERVER_PARTIAL_ABORT:
	  stran_server_partial_abort (thrd, rid, buffer, size);
	  break;

	case SERVER_TM_SERVER_HAS_UPDATED:
	  stran_server_has_updated (thrd, rid, buffer, size);
	  break;
	case SERVER_TM_SERVER_ISACTIVE_AND_HAS_UPDATED:
	  stran_server_is_active_and_has_updated (thrd, rid, buffer, size);
	  break;
	case SERVER_TM_ISBLOCKED:
	  stran_is_blocked (thrd, rid, buffer, size);
	  break;
	case SERVER_TM_WAIT_SERVER_ACTIVE_TRANS:
	  stran_wait_server_active_trans (thrd, rid, buffer, size);
	  break;
	case SERVER_TM_SERVER_GET_GTRINFO:
	  stran_server_get_global_tran_info (thrd, rid, buffer, size);
	  break;
	case SERVER_TM_SERVER_SET_GTRINFO:
	  stran_server_set_global_tran_info (thrd, rid, buffer, size);
	  break;
	case SERVER_TM_SERVER_2PC_START:
	  stran_server_2pc_start (thrd, rid, buffer, size);
	  break;
	case SERVER_TM_SERVER_2PC_PREPARE:
	  stran_server_2pc_prepare (thrd, rid, buffer, size);
	  break;
	case SERVER_TM_SERVER_2PC_RECOVERY_PREPARED:
	  stran_server_2pc_recovery_prepared (thrd, rid, buffer, size);
	  break;
	case SERVER_TM_SERVER_2PC_ATTACH_GT:
	  stran_server_2pc_attach_global_tran (thrd, rid, buffer, size);
	  break;
	case SERVER_TM_SERVER_2PC_PREPARE_GT:
	  stran_server_2pc_prepare_global_tran (thrd, rid, buffer, size);
	  break;

	case SERVER_LC_FETCH:
	  slocator_fetch (thrd, rid, buffer, size);
	  break;
	case SERVER_LC_FETCHALL:
	  slocator_fetch_all (thrd, rid, buffer, size);
	  break;
	case SERVER_LC_FETCH_LOCKSET:
	  slocator_fetch_lockset (thrd, rid, buffer, size);
	  break;
	case SERVER_LC_FETCH_ALLREFS_LOCKSET:
	  slocator_fetch_all_reference_lockset (thrd, rid, buffer, size);
	  break;
	case SERVER_LC_GET_CLASS:
	  slocator_get_class (thrd, rid, buffer, size);
	  break;
	case SERVER_LC_FIND_CLASSOID:
	  slocator_find_class_oid (thrd, rid, buffer, size);
	  break;
	case SERVER_LC_DOESEXIST:
	  slocator_does_exist (thrd, rid, buffer, size);
	  break;
	case SERVER_LC_FORCE:
#if defined(DIAG_DEVEL)
	  SET_DIAG_VALUE (diag_executediag, DIAG_OBJ_TYPE_CONN_CLI_REQUEST, 1,
			  DIAG_VAL_SETTYPE_INC, NULL);
#endif /* DIAG_DEVEL */
	  slocator_force (thrd, rid, buffer, size);
	  break;
	case SERVER_LC_RESERVE_CLASSNAME:
	  slocator_reserve_classname (thrd, rid, buffer, size);
	  break;
	case SERVER_LC_DELETE_CLASSNAME:
	  slocator_delete_class_name (thrd, rid, buffer, size);
	  break;
	case SERVER_LC_RENAME_CLASSNAME:
	  slocator_rename_class_name (thrd, rid, buffer, size);
	  break;
	case SERVER_LC_ASSIGN_OID:
	  slocator_assign_oid (thrd, rid, buffer, size);
	  break;
	case SERVER_LC_ASSIGN_OID_BATCH:
	  slocator_assign_oid_batch (thrd, rid, buffer, size);
	  break;
	case SERVER_LC_NOTIFY_ISOLATION_INCONS:
	  slocator_notify_isolation_incons (thrd, rid, buffer, size);
	  break;
	case SERVER_LC_FIND_LOCKHINT_CLASSOIDS:
	  slocator_find_lockhint_class_oids (thrd, rid, buffer, size);
	  break;
	case SERVER_LC_FETCH_LOCKHINT_CLASSES:
	  slocator_fetch_lockhint_classes (thrd, rid, buffer, size);
	  break;
	case SERVER_LC_REM_CLASS_FROM_INDEX:
	  slocator_remove_class_from_index (thrd, rid, buffer, size);
	  break;

	case SERVER_HEAP_CREATE:
	  shf_create (thrd, rid, buffer, size);
	  break;
	case SERVER_HEAP_DESTROY:
	  shf_destroy (thrd, rid, buffer, size);
	  break;
	case SERVER_HEAP_DESTROY_WHEN_NEW:
	  shf_destroy_when_new (thrd, rid, buffer, size);
	  break;

	case SERVER_LARGEOBJMGR_CREATE:
	  slargeobjmgr_create (thrd, rid, buffer, size);
	  break;
	case SERVER_LARGEOBJMGR_READ:
	  slargeobjmgr_read (thrd, rid, buffer, size);
	  break;
	case SERVER_LARGEOBJMGR_WRITE:
	  slargeobjmgr_write (thrd, rid, buffer, size);
	  break;
	case SERVER_LARGEOBJMGR_INSERT:
	  slargeobjmgr_insert (thrd, rid, buffer, size);
	  break;
	case SERVER_LARGEOBJMGR_DESTROY:
	  slargeobjmgr_destroy (thrd, rid, buffer, size);
	  break;
	case SERVER_LARGEOBJMGR_DELETE:
	  slargeobjmgr_delete (thrd, rid, buffer, size);
	  break;
	case SERVER_LARGEOBJMGR_APPEND:
	  slargeobjmgr_append (thrd, rid, buffer, size);
	  break;
	case SERVER_LARGEOBJMGR_TRUNCATE:
	  slargeobjmgr_truncate (thrd, rid, buffer, size);
	  break;
	case SERVER_LARGEOBJMGR_COMPRESS:
	  slargeobjmgr_compress (thrd, rid, buffer, size);
	  break;
	case SERVER_LARGEOBJMGR_LENGTH:
	  slargeobjmgr_length (thrd, rid, buffer, size);
	  break;

	case SERVER_LOG_RESET_WAITSECS:
	  slogtb_reset_wait_secs (thrd, rid, buffer, size);
	  break;
	case SERVER_LOG_RESET_ISOLATION:
	  slogtb_reset_isolation (thrd, rid, buffer, size);
	  break;
	case SERVER_LOG_SET_INTERRUPT:
	  slogtb_set_interrupt (thrd, rid, buffer, size);
	  break;
	case SERVER_LOG_CLIENT_UNDO:
	  slog_append_client_undo (thrd, rid, buffer, size);
	  break;
	case SERVER_LOG_CLIENT_POSTPONE:
	  slog_append_client_postpone (thrd, rid, buffer, size);
	  break;
	case SERVER_LOG_HAS_FINISHED_CLIENT_POSTPONE:
	  slog_client_complete_postpone (thrd, rid, buffer, size);
	  break;
	case SERVER_LOG_HAS_FINISHED_CLIENT_UNDO:
	  slog_client_complete_undo (thrd, rid, buffer, size);
	  break;
	case SERVER_LOG_CLIENT_GET_FIRST_POSTPONE:
	  slog_client_get_first_postpone (thrd, rid, buffer, size);
	  break;
	case SERVER_LOG_CLIENT_GET_FIRST_UNDO:
	  slog_client_get_first_undo (thrd, rid, buffer, size);
	  break;
	case SERVER_LOG_CLIENT_GET_NEXT_POSTPONE:
	  slog_client_get_next_postpone (thrd, rid, buffer, size);
	  break;
	case SERVER_LOG_CLIENT_GET_NEXT_UNDO:
	  slog_client_get_next_undo (thrd, rid, buffer, size);
	  break;
	case SERVER_LOG_CLIENT_UNKNOWN_STATE_ABORT_GET_FIRST_UNDO:
	  slog_client_unknown_state_abort_get_first_undo (thrd, rid, buffer,
							  size);
	  break;
	case SERVER_LOG_GETPACK_TRANTB:
	  slogtb_get_pack_tran_table (thrd, rid, buffer, size);
	  break;

	case SERVER_LK_DUMP:
	  slock_dump (thrd, rid, buffer, size);
	  break;

	case SERVER_BTREE_ADDINDEX:
	  sbtree_add_index (thrd, rid, buffer, size);
	  break;
	case SERVER_BTREE_DELINDEX:
	  sbtree_delete_index (thrd, rid, buffer, size);
	  break;
	case SERVER_BTREE_LOADINDEX:
	  sbtree_load_index (thrd, rid, buffer, size);
	  break;
	case SERVER_BTREE_FIND_UNIQUE:
	  sbtree_find_unique (thrd, rid, buffer, size);
	  break;
	case SERVER_BTREE_CLASS_UNIQUE_TEST:
	  sbtree_class_test_unique (thrd, rid, buffer, size);
	  break;

	case SERVER_DISK_TOTALPGS:
	  sdk_totalpgs (thrd, rid, buffer, size);
	  break;
	case SERVER_DISK_FREEPGS:
	  sdk_freepgs (thrd, rid, buffer, size);
	  break;
	case SERVER_DISK_REMARKS:
	  sdk_remarks (thrd, rid, buffer, size);
	  break;
	case SERVER_DISK_PURPOSE:
	  sdk_purpose (thrd, rid, buffer, size);
	  break;
	case SERVER_DISK_PURPOSE_TOTALPGS_AND_FREEPGS:
	  sdk_purpose_totalpgs_and_freepgs (thrd, rid, buffer, size);
	  break;
	case SERVER_DISK_VLABEL:
	  sdk_vlabel (thrd, rid, buffer, size);
	  break;

	case SERVER_QST_SERVER_GET_STATISTICS:
	  sqst_server_get_statistics (thrd, rid, buffer, size);
	  break;
	case SERVER_QST_UPDATE_CLASS_STATISTICS:
	  sqst_update_class_statistics (thrd, rid, buffer, size);
	  break;
	case SERVER_QST_UPDATE_STATISTICS:
	  sqst_update_statistics (thrd, rid, buffer, size);
	  break;

	case SERVER_QM_QUERY_PREPARE:
	  sqmgr_prepare_query (thrd, rid, buffer, size);
	  break;
	case SERVER_QM_QUERY_EXECUTE:
#if defined(DIAG_DEVEL)
	  SET_DIAG_VALUE (diag_executediag, DIAG_OBJ_TYPE_CONN_CLI_REQUEST, 1,
			  DIAG_VAL_SETTYPE_INC, NULL);
	  DIAG_GET_TIME (diag_executediag, diag_start_time);
#endif /* DIAG_DEVEL */
	  sqmgr_execute_query (thrd, rid, buffer, size);
#if defined(DIAG_DEVEL)
	  DIAG_GET_TIME (diag_executediag, diag_end_time);
	  SET_DIAG_VALUE_SLOW_QUERY (diag_executediag, diag_start_time,
				     diag_end_time, 1, DIAG_VAL_SETTYPE_INC,
				     NULL);
#endif /* DIAG_DEVEL */
	  break;
	case SERVER_QM_QUERY_PREPARE_AND_EXECUTE:
#if defined(DIAG_DEVEL)
	  SET_DIAG_VALUE (diag_executediag, DIAG_OBJ_TYPE_CONN_CLI_REQUEST, 1,
			  DIAG_VAL_SETTYPE_INC, NULL);
	  DIAG_GET_TIME (diag_executediag, diag_start_time);
#endif /* DIAG_DEVEL */
	  sqmgr_prepare_and_execute_query (thrd, rid, buffer, size);
#if defined(DIAG_DEVEL)
	  DIAG_GET_TIME (diag_executediag, diag_end_time);
	  SET_DIAG_VALUE_SLOW_QUERY (diag_executediag, diag_start_time,
				     diag_end_time, 1, DIAG_VAL_SETTYPE_INC,
				     NULL);
#endif /* DIAG_DEVEL */
	  break;
	case SERVER_QM_QUERY_EXECUTE_ASYNC:
#if defined(DIAG_DEVEL)
	  SET_DIAG_VALUE (diag_executediag, DIAG_OBJ_TYPE_CONN_CLI_REQUEST, 1,
			  DIAG_VAL_SETTYPE_INC, NULL);
	  DIAG_GET_TIME (diag_executediag, diag_start_time);
#endif /* DIAG_DEVEL */
	  sqmgr_execute_query (thrd, rid, buffer, size);
#if defined(DIAG_DEVEL)
	  DIAG_GET_TIME (diag_executediag, diag_end_time);
	  SET_DIAG_VALUE_SLOW_QUERY (diag_executediag, diag_start_time,
				     diag_end_time, 1, DIAG_VAL_SETTYPE_INC,
				     NULL);
#endif /* DIAG_DEVEL */
	  break;
	case SERVER_QM_QUERY_PREPARE_AND_EXECUTE_ASYNC:
#if defined(DIAG_DEVEL)
	  SET_DIAG_VALUE (diag_executediag, DIAG_OBJ_TYPE_CONN_CLI_REQUEST, 1,
			  DIAG_VAL_SETTYPE_INC, NULL);
	  DIAG_GET_TIME (diag_executediag, diag_start_time);
#endif /* DIAG_DEVEL */
	  sqmgr_prepare_and_execute_query (thrd, rid, buffer, size);
#if defined(DIAG_DEVEL)
	  DIAG_GET_TIME (diag_executediag, diag_end_time);
	  SET_DIAG_VALUE_SLOW_QUERY (diag_executediag, diag_start_time,
				     diag_end_time, 1, DIAG_VAL_SETTYPE_INC,
				     NULL);
#endif /* DIAG_DEVEL */
	  break;
	case SERVER_QM_QUERY_SYNC:
	  sqmgr_sync_query (thrd, rid, buffer, size);
	  break;
	case SERVER_QM_GET_QUERY_INFO:
	  sqmgr_get_query_info (thrd, rid, buffer, size);
	  break;
	case SERVER_QM_QUERY_END:
	  sqmgr_end_query (thrd, rid, buffer, size);
	  break;
	case SERVER_QM_QUERY_DROP_PLAN:
	  sqmgr_drop_query_plan (thrd, rid, buffer, size);
	  break;
	case SERVER_QM_QUERY_DROP_ALL_PLANS:
	  sqmgr_drop_all_query_plans (thrd, rid, buffer, size);
	  break;
	case SERVER_QM_QUERY_DUMP_PLANS:
	  sqmgr_dump_query_plans (thrd, rid, buffer, size);
	  break;
	case SERVER_QM_QUERY_DUMP_CACHE:
	  sqmgr_dump_query_cache (thrd, rid, buffer, size);
	  break;
	  /* AsyncCommit */
	case SERVER_LOG_DUMP_STAT:
	  slogpb_dump_stat (rid, buffer, size);
	  break;

	case SERVER_LS_GET_LIST_FILE_PAGE:
	  sqfile_get_list_file_page (thrd, rid, buffer, size);
	  break;

	case SERVER_MNT_SERVER_START_STATS:
	  smnt_server_start_stats (thrd, rid, buffer, size);
	  break;
	case SERVER_MNT_SERVER_STOP_STATS:
	  smnt_server_stop_stats (thrd, rid, buffer, size);
	  break;
	case SERVER_MNT_SERVER_RESET_STATS:
	  smnt_server_reset_stats (thrd, rid, buffer, size);
	  break;
	case SERVER_MNT_SERVER_COPY_STATS:
	  smnt_server_copy_stats (thrd, rid, buffer, size);
	  break;

	case SERVER_CT_CAN_ACCEPT_NEW_REPR:
	  sct_can_accept_new_repr (thrd, rid, buffer, size);
	  break;

	case SERVER_TEST_PERFORMANCE:
	  stest_performance (rid, buffer, size);
	  break;

	case SERVER_RESTART_EVENT_HANDLER:
	  tm_restart_event_handler (rid, buffer, size);
	  break;

	case SERVER_CSS_KILL_TRANSACTION:
	  sthread_kill_tran_index (thrd, rid, buffer, size);
	  break;

	case SERVER_SHUTDOWN:
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_NET_SERVER_SHUTDOWN,
		  0);
	  /* When this actually does a shutdown, change to CSS_PLANNED_SHUTDOWN */
	  status = CSS_UNPLANNED_SHUTDOWN;
	  break;

	case SERVER_QPROC_GET_SYS_TIMESTAMP:
	  sqp_get_sys_timestamp (rid);
	  break;

	case SERVER_QPROC_GET_CURRENT_VALUE:
	  sqp_get_current_value (thrd, rid, buffer, size);
	  break;

	case SERVER_QPROC_GET_NEXT_VALUE:
	  sqp_get_next_value (thrd, rid, buffer, size);
	  break;

	case SERVER_HEAP_GET_CLASS_NOBJS_AND_NPAGES:
	  shf_get_class_num_objs_and_pages (thrd, rid, buffer, size);
	  break;

	case SERVER_BTREE_GET_STATISTICS:
	  sbtree_get_statistics (thrd, rid, buffer, size);
	  break;

	case SERVER_QPROC_GET_SERVER_INFO:
	  sqp_get_server_info (thrd, rid, buffer, size);
	  break;

	case SERVER_PRM_SET_PARAMETERS:
	  sprm_server_change_parameters (thrd, rid, buffer, size);
	  break;

	case SERVER_PRM_GET_PARAMETERS:
	  sprm_server_obtain_parameters (thrd, rid, buffer, size);
	  break;

	case SERVER_HEAP_HAS_INSTANCE:
	  shf_has_instance (thrd, rid, buffer, size);
	  break;

	case SERVER_TM_LOCAL_TRANSACTION_ID:
	  stran_get_local_transaction_id (thrd, rid, buffer, size);
	  break;

	case SERVER_JSP_GET_SERVER_PORT:
	  sjsp_get_server_port (rid);
	  break;

	case SERVER_REPL_INFO:
	  srepl_set_info (thrd, rid, buffer, size);
	  break;

	case SERVER_REPL_LOG_GET_APPEND_LSA:
	  srepl_log_get_append_lsa (rid, buffer, size);
	  break;

	case SERVER_LC_BUILD_FK_OBJECT_CACHE:
	  slocator_build_fk_object_cache (thrd, rid, buffer, size);
	  break;

	default:
	  break;
	}

      if (size)
	{
	  free_and_init (buffer);
	}

      /* clear memory to be used at request handling */
      db_clear_private_heap (thrd, 0);
    }

  return (status);
}

/*
 * net_server_conn_down () - CSS callback function used when a connection to a
 *                       particular client went down
 *   return: 0
 *   arg(in): transaction id
 */
static int
net_server_conn_down (THREAD_ENTRY * thread_p, CSS_THREAD_ARG arg)
{
  int tran_index;
  CSS_CONN_ENTRY *conn_p;
  int prev_thrd_cnt, thrd_cnt;
  bool continue_check;
  int client_id;
  int local_tran_index;

  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  local_tran_index = thread_p->tran_index;

  conn_p = (CSS_CONN_ENTRY *) arg;
  tran_index = conn_p->transaction_id;
  client_id = conn_p->client_id;

  THREAD_SET_INFO (thread_p, client_id, 0, tran_index);
  MUTEX_UNLOCK (thread_p->tran_index_lock);

  css_end_server_request (conn_p);

  /* avoid infinite waiting with xtran_wait_server_active_trans() */
  thread_p->status = TS_CHECK;

loop:
  prev_thrd_cnt = thread_has_threads (tran_index, client_id);
  if (prev_thrd_cnt > 0)
    {
      if (!logtb_is_interrupt_tran
	  (thread_p, false, &continue_check, tran_index))
	{
	  logtb_set_tran_index_interrupt (thread_p, tran_index, true);
	}
    }

  while ((thrd_cnt = thread_has_threads (tran_index, client_id))
	 >= prev_thrd_cnt && thrd_cnt > 0)
    {
      /* Some threads may wait for data from the m-driver.
       * It's possible from the fact that css_server_thread() is responsible
       * for receiving every data from which is sent by a client and all
       * m-drivers. We must have chance to receive data from them.
       */
      thread_sleep (0, 500);
    }

  if (thrd_cnt > 0)
    {
      goto loop;
    }

  logtb_set_tran_index_interrupt (thread_p, tran_index, false);

  if (tran_index != NULL_TRAN_INDEX)
    {
      (void) xboot_unregister_client (thread_p, tran_index);
    }
  css_free_conn (conn_p);

  THREAD_SET_INFO (thread_p, -1, 0, local_tran_index);
  thread_p->status = TS_RUN;

  return 0;
}

/*
 * net_server_start () - Starts the operation of a CUBRID server
 *   return: error status
 *   server_name(in): name of server
 */
int
net_server_start (const char *server_name)
{
  int error = NO_ERROR;
  int name_length;
  char *packed_name;
  int r, status = 0;

#if defined(WINDOWS)
  if (css_windows_startup () < 0)
    {
      printf ("Winsock startup error\n");
      return -1;
    }
#endif /* WINDOWS */

  /* open the system message catalog, before prm_ ?  */
  if (msgcat_init () != NO_ERROR)
    return -1;
  /* initialize system parameters */
  if (sysprm_load_and_init (NULL, NULL) != NO_ERROR)
    return -1;
  thread_initialize_manager (PRM_MAX_THREADS);
  csect_initialize ();
  if (er_init (NULL, 0) != NO_ERROR)
    {
      printf ("Failed to initialize error manager\n");
      return -1;
    }

  css_initialize_server_interfaces (net_server_request, net_server_conn_down);

#if defined(CUBRID_DEBUG)
  histo_setup_names ();
#endif /* CUBRID_DEBUG */

  if (boot_restart_server (NULL, true, server_name, false, NULL) != NO_ERROR)
    {
      error = er_errid ();
    }
  else
    {
      packed_name = css_pack_server_name (server_name, &name_length);
      css_init_job_queue ();

#if defined(DIAG_DEVEL)
      init_diag_mgr (server_name, MAX_NTHRDS, NULL);
#endif /* DIAG_DEVEL */

      r = css_init (packed_name, name_length, PRM_TCP_PORT_ID);

      free_and_init (packed_name);

#if defined(DIAG_DEVEL)
      close_diag_mgr ();
#endif /* DIAG_DEVEL */

      if (r < 0)
	{
	  error = er_errid ();

	  if (error == NO_ERROR)
	    {
	      error = ER_NET_NO_MASTER;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	    }

	  xboot_shutdown_server (NULL, false);
	}
      else
	{
	  (void) xboot_shutdown_server (NULL, true);
	}

#if defined(CUBRID_DEBUG)
      histo_print ();
#endif /* CUBRID_DEBUG */

      thread_kill_all_workers ();
      css_final_job_queue ();
      css_final_conn_list ();
    }

  csect_finalize ();

  if (error != NO_ERROR)
    {
      fprintf (stderr, "%s\n", er_msg ());
      fflush (stderr);
      status = 2;
    }

  thread_final_manager ();

#if defined(WINDOWS)
  css_windows_shutdown ();
#endif /* WINDOWS */

  return status;
}

/*
 * net_cleanup_server_queues () -
 *   return:
 *   rid(in):
 */
void
net_cleanup_server_queues (unsigned int rid)
{
  css_cleanup_server_queues (rid);
}
