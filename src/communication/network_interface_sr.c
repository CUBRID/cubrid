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
 * network_interface_sr.c - Server side network interface functions
 *                          for client requests.
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "porting.h"
#include "porting_inline.hpp"
#include "perf_monitor.h"
#include "memory_alloc.h"
#include "storage_common.h"
#include "xserver_interface.h"
#include "statistics_sr.h"
#include "btree_load.h"
#include "perf_monitor.h"
#include "log_impl.h"
#include "log_lsa.hpp"
#include "boot_sr.h"
#include "locator_sr.h"
#include "server_interface.h"
#include "oid.h"
#include "error_manager.h"
#include "object_representation.h"
#include "network.h"
#include "log_comm.h"
#include "network_interface_sr.h"
#include "page_buffer.h"
#include "file_manager.h"
#include "boot_sr.h"
#include "arithmetic.h"
#include "serial.h"
#include "query_manager.h"
#include "transaction_sr.h"
#include "release_string.h"
#include "critical_section.h"
#include "statistics.h"
#include "chartype.h"
#include "heap_file.h"
#include "jsp_sr.h"
#include "replication.h"
#include "server_support.h"
#include "connection_sr.h"
#include "log_writer.h"
#include "databases_file.h"
#include "es.h"
#include "es_posix.h"
#include "event_log.h"
#include "tsc_timer.h"
#include "vacuum.h"
#include "object_primitive.h"
#include "tz_support.h"
#include "dbtype.h"
#include "thread_manager.hpp"	// for thread_get_thread_entry_info
#include "compile_context.h"
#include "load_session.hpp"
#include "session.h"
#include "xasl.h"
#include "xasl_cache.h"
#include "elo.h"
#include "transaction_transient.hpp"
#include "method_invoke_group.hpp"
#include "method_runtime_context.hpp"
#include "log_manager.h"
#include "crypt_opfunc.h"
#include "flashback.h"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"
#if defined (SUPPRESS_STRLEN_WARNING)
#define strlen(s1)  ((int) strlen(s1))
#endif /* defined (SUPPRESS_STRLEN_WARNING) */

#define NET_COPY_AREA_SENDRECV_SIZE (OR_INT_SIZE * 3)
#define NET_SENDRECV_BUFFSIZE (OR_INT_SIZE)

#define STATDUMP_BUF_SIZE (2 * 16 * 1024)
#define QUERY_INFO_BUF_SIZE (2048 + STATDUMP_BUF_SIZE)

#define NET_DEFER_END_QUERIES_MAX 10

/* Query execution with commit. */
#define QEWC_SAFE_GUARD_SIZE 1024
// To have the safe area is just a safe guard to avoid potential issues of bad size calculation.
#define QEWC_MAX_DATA_SIZE  (DB_PAGESIZE - QEWC_SAFE_GUARD_SIZE)

/* This file is only included in the server.  So set the on_server flag on */
unsigned int db_on_server = 1;

STATIC_INLINE TRAN_STATE stran_server_commit_internal (THREAD_ENTRY * thread_p, unsigned int rid, bool retain_lock,
						       bool * should_conn_reset) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE TRAN_STATE stran_server_abort_internal (THREAD_ENTRY * thread_p, unsigned int rid, bool retain_lock,
						      bool * should_conn_reset) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void stran_server_auto_commit_or_abort (THREAD_ENTRY * thread_p, unsigned int rid,
						      QUERY_ID * p_end_queries, int n_query_ids, bool need_abort,
						      bool has_updated, bool * end_query_allowed,
						      TRAN_STATE * tran_state, bool * should_conn_reset)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int stran_can_end_after_query_execution (THREAD_ENTRY * thread_p, int query_flag, QFILE_LIST_ID * list_id,
						       bool * can_end_transaction) __attribute__ ((ALWAYS_INLINE));

static bool need_to_abort_tran (THREAD_ENTRY * thread_p, int *errid);
static int server_capabilities (void);
static int check_client_capabilities (THREAD_ENTRY * thread_p, int client_cap, int rel_compare,
				      REL_COMPATIBILITY * compatibility, const char *client_host);
static void sbtree_find_unique_internal (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen);
static int er_log_slow_query (THREAD_ENTRY * thread_p, EXECUTION_INFO * info, int time,
			      UINT64 * diff_stats, char *queryinfo_string);
static void event_log_slow_query (THREAD_ENTRY * thread_p, EXECUTION_INFO * info, int time, UINT64 * diff_stats);
static void event_log_many_ioreads (THREAD_ENTRY * thread_p, EXECUTION_INFO * info, int time, UINT64 * diff_stats);
static void event_log_temp_expand_pages (THREAD_ENTRY * thread_p, EXECUTION_INFO * info);

/*
 * stran_server_commit_internal - commit transaction on server.
 *
 * return:
 *
 *   thread_p(in): thred entry.
 *   rid(in): request id.
 *   retain_lock(in): true, if retains lock.
 *   should_conn_reset(out): reset on commit.
 *
 * NOTE: This function must be called at transaction commit.
 */
STATIC_INLINE TRAN_STATE
stran_server_commit_internal (THREAD_ENTRY * thread_p, unsigned int rid, bool retain_lock, bool * should_conn_reset)
{
  bool has_updated;
  TRAN_STATE state;

  assert (should_conn_reset != NULL);
  has_updated = logtb_has_updated (thread_p);

  state = xtran_server_commit (thread_p, retain_lock);

  net_cleanup_server_queues (rid);

  if (state != TRAN_UNACTIVE_COMMITTED && state != TRAN_UNACTIVE_COMMITTED_INFORMING_PARTICIPANTS)
    {
      /* Likely the commit failed.. somehow */
      (void) return_error_to_client (thread_p, rid);
    }

  *should_conn_reset = xtran_should_connection_reset (thread_p, has_updated);

  return state;
}

/*
 * stran_server_abort_internal - abort transaction on server.
 *
 * return:
 *
 *   thread_p(in): thred entry.
 *   rid(in): request id.
 *   should_conn_reset(out): reset on commit.
 *
 * NOTE: This function must be called at transaction abort.
 */
STATIC_INLINE TRAN_STATE
stran_server_abort_internal (THREAD_ENTRY * thread_p, unsigned int rid, bool * should_conn_reset)
{
  TRAN_STATE state;
  bool has_updated;

  has_updated = logtb_has_updated (thread_p);

  state = xtran_server_abort (thread_p);

  net_cleanup_server_queues (rid);

  if (state != TRAN_UNACTIVE_ABORTED && state != TRAN_UNACTIVE_ABORTED_INFORMING_PARTICIPANTS)
    {
      /* Likely the abort failed.. somehow */
      (void) return_error_to_client (thread_p, rid);
    }

  *should_conn_reset = xtran_should_connection_reset (thread_p, has_updated);

  return state;
}

/*
 * stran_server_auto_commit_or_abort - do server-side auto-commit or abort
 *
 * return: nothing
 *
 *   thread_p(in): thread entry
 *   rid(in): request id
 *   p_end_queries(in): queries to end
 *   n_query_ids(in): the number of queries to end
 *   need_abort(in): true, if need to abort
 *   has_updated(in):true, if has updated before abort
 *   end_query_allowed(in/out): true, if end query is allowed
 *   tran_state(in/out): transaction state
 *   should_conn_reset(in/out): reset on commit
 *
 * Note: This function must be called only when the query is executed with commit, soon after query execution.
 *       When we call this function, it is possible that transaction was aborted.
 */
STATIC_INLINE void
stran_server_auto_commit_or_abort (THREAD_ENTRY * thread_p, unsigned int rid, QUERY_ID * p_end_queries,
				   int n_query_ids, bool need_abort, bool has_updated, bool * end_query_allowed,
				   TRAN_STATE * tran_state, bool * should_conn_reset)
{
  int error_code, all_error_code, i;

  assert (tran_state != NULL && should_conn_reset != NULL && end_query_allowed != NULL);

  *should_conn_reset = false;

  if (*end_query_allowed == false)
    {
      if (prm_get_bool_value (PRM_ID_DEBUG_AUTOCOMMIT))
	{
	  _er_log_debug (ARG_FILE_LINE, "stran_server_auto_commit_or_abort: active transaction.\n");
	}
      return;
    }

  /* We commit/abort transaction, after ending queries. */
  all_error_code = NO_ERROR;
  if ((*tran_state != TRAN_UNACTIVE_ABORTED) && (*tran_state != TRAN_UNACTIVE_ABORTED_INFORMING_PARTICIPANTS))
    {
      /* If not already aborted, ends the queries. */
      for (i = 0; i < n_query_ids; i++)
	{
	  if (p_end_queries[i] > 0)
	    {
	      error_code = xqmgr_end_query (thread_p, p_end_queries[i]);
	      if (error_code != NO_ERROR)
		{
		  all_error_code = error_code;
		  /* Continue to try to close as many queries as possible. */
		}
	    }
	}
    }

  if (all_error_code != NO_ERROR)
    {
      (void) return_error_to_client (thread_p, rid);
      *end_query_allowed = false;
    }
  else if (need_abort == false)
    {
      /* Needs commit. */
      *tran_state = stran_server_commit_internal (thread_p, rid, false, should_conn_reset);
      if (prm_get_bool_value (PRM_ID_DEBUG_AUTOCOMMIT))
	{
	  _er_log_debug (ARG_FILE_LINE, "stran_server_auto_commit_or_abort: transaction committed. \n");
	}
    }
  else
    {
      /* Needs abort. */
      if ((*tran_state != TRAN_UNACTIVE_ABORTED) && (*tran_state != TRAN_UNACTIVE_ABORTED_INFORMING_PARTICIPANTS))
	{
	  /* We have an error and the transaction was not aborted. Since is auto commit transaction, we can abort it.
	   * In this way, we can avoid abort request.
	   */
	  *tran_state = stran_server_abort_internal (thread_p, rid, should_conn_reset);
	  if (prm_get_bool_value (PRM_ID_DEBUG_AUTOCOMMIT))
	    {
	      _er_log_debug (ARG_FILE_LINE, "stran_server_auto_commit_or_abort: transaction aborted. \n");
	    }
	}
      else
	{
	  /* Transaction was already aborted. */
	  *should_conn_reset = xtran_should_connection_reset (thread_p, has_updated);
	}
    }
}

/*
 * need_to_abort_tran - check whether the transaction should be aborted
 *
 * return: true/false
 *
 *   thread_p(in): thread entry
 *   errid(out): the latest error code
 */
static bool
need_to_abort_tran (THREAD_ENTRY * thread_p, int *errid)
{
  LOG_TDES *tdes;
  bool flag_abort = false;

  assert (thread_p != NULL);

#if 0				/* TODO */
  assert (er_errid () != NO_ERROR);
#endif

  *errid = er_errid ();
  if (*errid == ER_LK_UNILATERALLY_ABORTED || *errid == ER_DB_NO_MODIFICATIONS)
    {
      flag_abort = true;
    }

  /*
   * DEFENCE CODE:
   *  below block means ER_LK_UNILATERALLY_ABORTED occurs but another error
   *  set after that.
   *  So, re-set that error to rollback in client side.
   */
  tdes = LOG_FIND_CURRENT_TDES (thread_p);
  if (tdes != NULL && tdes->tran_abort_reason != TRAN_NORMAL && flag_abort == false)
    {
      flag_abort = true;

      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LK_UNILATERALLY_ABORTED, 4, thread_p->tran_index,
	      tdes->client.get_db_user (), tdes->client.get_host_name (), tdes->client.process_id);
    }

  return flag_abort;
}

/*
 * return_error_to_client -
 *
 * return: state of operation
 *
 *   rid(in):
 *
 * NOTE:
 */
TRAN_STATE
return_error_to_client (THREAD_ENTRY * thread_p, unsigned int rid)
{
  LOG_TDES *tdes;
  int errid;
  bool flag_abort = false;
  char *area;
  OR_ALIGNED_BUF (1024) a_buffer;
  char *buffer;
  int length = 1024;
  TRAN_STATE tran_state = TRAN_UNACTIVE_UNKNOWN;

  CSS_CONN_ENTRY *conn;

  assert (thread_p != NULL);

  conn = thread_p->conn_entry;
  assert (conn != NULL);

  tdes = LOG_FIND_CURRENT_TDES (thread_p);
  if (tdes != NULL)
    {
      tran_state = tdes->state;
    }
  flag_abort = need_to_abort_tran (thread_p, &errid);

  /* check some errors which require special actions */
  /*
   * ER_LK_UNILATERALLY_ABORTED may have occurred due to deadlock.
   * If it happened, do server-side rollback of the transaction.
   * If ER_DB_NO_MODIFICATIONS error is occurred in server-side,
   * it means that the user tried to update the database
   * when the server was disabled to modify. (aka standby mode)
   */
  if (flag_abort == true)
    {
      /* need to hide the previous error, ER_LK_UNILATERALLY_ABORTED to rollback the current transaction. */
      er_stack_push ();
      tran_state = tran_server_unilaterally_abort_tran (thread_p);
      er_stack_pop ();
    }

  if (errid == ER_DB_NO_MODIFICATIONS)
    {
      conn->reset_on_commit = true;
    }

  buffer = OR_ALIGNED_BUF_START (a_buffer);
  area = er_get_area_error (buffer, &length);
  if (area != NULL)
    {
      conn->db_error = errid;
      css_send_error_to_client (conn, rid, area, length);
      conn->db_error = 0;
    }

  if (tdes != NULL)
    {
      tdes->tran_abort_reason = TRAN_NORMAL;
    }

  return tran_state;
}

/*
 * server_capabilities -
 *
 * return:
 */
static int
server_capabilities (void)
{
  int capabilities = 0;

  capabilities |= NET_CAP_INTERRUPT_ENABLED;
  if (db_Disable_modifications > 0)
    {
      capabilities |= NET_CAP_UPDATE_DISABLED;
    }
  if (boot_Server_status == BOOT_SERVER_MAINTENANCE)
    {
      capabilities |= NET_CAP_REMOTE_DISABLED;
    }
  if (css_is_ha_repl_delayed () == true)
    {
      capabilities |= NET_CAP_HA_REPL_DELAY;
    }
  if (HA_GET_MODE () == HA_MODE_REPLICA)
    {
      assert_release (css_ha_server_state () == HA_SERVER_STATE_STANDBY);
      capabilities |= NET_CAP_HA_REPLICA;
    }

  return capabilities;
}

/*
 * check_client_capabilities -
 *
 * return:
 *   client_cap(in): client capability
 *
 */
static int
check_client_capabilities (THREAD_ENTRY * thread_p, int client_cap, int rel_compare, REL_COMPATIBILITY * compatibility,
			   const char *client_host)
{
  int server_cap;

  assert (compatibility != NULL);

  server_cap = server_capabilities ();
  /* interrupt-ability should be same */
  if ((server_cap ^ client_cap) & NET_CAP_INTERRUPT_ENABLED)
    {
      client_cap ^= NET_CAP_INTERRUPT_ENABLED;
    }
  /* network protocol compatibility */
  if (*compatibility == REL_NOT_COMPATIBLE)
    {
      if (rel_compare < 0 && (client_cap & NET_CAP_FORWARD_COMPATIBLE))
	{
	  /*
	   * The client is older than the server but the client has a forward
	   * compatible capability.
	   */
	  *compatibility = REL_FORWARD_COMPATIBLE;
	}
      if (rel_compare > 0 && (client_cap & NET_CAP_BACKWARD_COMPATIBLE))
	{
	  /*
	   * The client is newer than the server but the client has a backward
	   * compatible capability.
	   */
	  *compatibility = REL_BACKWARD_COMPATIBLE;
	}
    }
  /* remote connection capability */
  if (server_cap & NET_CAP_REMOTE_DISABLED)
    {
      /* do capability check on client side */
      er_log_debug (ARG_FILE_LINE, "NET_CAP_REMOTE_DISABLED server %s %d client %s %d\n", boot_Host_name,
		    server_cap & NET_CAP_REMOTE_DISABLED, client_host, client_cap & NET_CAP_REMOTE_DISABLED);
    }

  if (client_cap & NET_CAP_HA_IGNORE_REPL_DELAY)
    {
      thread_p->conn_entry->ignore_repl_delay = true;

      er_log_debug (ARG_FILE_LINE, "NET_CAP_HA_IGNORE_REPL_DELAY client %s %d\n", client_host,
		    client_cap & NET_CAP_HA_IGNORE_REPL_DELAY);
    }

  return client_cap;
}

/*
 * server_ping - return that the server is alive
 *   return:
 *   rid(in): request id
 */
void
server_ping (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  int client_val, server_val;

  er_log_debug (ARG_FILE_LINE, "The server_ping() is called.");

  /* you can get something useful from the request */
  or_unpack_int (request, &client_val);

  /* you can envelope something useful into the reply */
  server_val = 0;
  or_pack_int (reply, server_val);
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_INT_SIZE);
}

/*
 * server_ping_with_handshake -
 *
 * return:
 *
 *   rid(in): request id
 *   request(in):
 *   reqlen(in):
 *
 * NOTE: Handler for the SERVER_PING_WITH_HANDSHAKE request.
 *    We record the client's version string here and send back our own
 *    version string so the client can determine compatibility.
 */
int
server_ping_with_handshake (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  OR_ALIGNED_BUF (REL_MAX_RELEASE_LENGTH + (OR_INT_SIZE * 3) + CUB_MAXHOSTNAMELEN) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  int reply_size = OR_ALIGNED_BUF_SIZE (a_reply);
  char *ptr, *client_release, *client_host;
  const char *server_release;
  int client_capabilities, client_bit_platform, status = CSS_NO_ERRORS;
  int client_type;
  int strlen1, strlen2;
  REL_COMPATIBILITY compat;

  server_release = rel_release_string ();

  if (reqlen > 0)
    {
      ptr = or_unpack_string_nocopy (request, &client_release);
      ptr = or_unpack_int (ptr, &client_capabilities);
      ptr = or_unpack_int (ptr, &client_bit_platform);
      ptr = or_unpack_int (ptr, &client_type);
      ptr = or_unpack_string_nocopy (ptr, &client_host);
      if (client_release != NULL)
	{
	  client_release = css_add_client_version_string (thread_p, client_release);
	}
    }
  else
    {
      client_release = NULL;
      client_bit_platform = 0;
      client_capabilities = 0;
      client_host = NULL;
    }

  /* check bits model */
  if (client_bit_platform != rel_bit_platform ())
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NET_DIFFERENT_BIT_PLATFORM, 2, rel_bit_platform (),
	      client_bit_platform);
      (void) return_error_to_client (thread_p, rid);
      status = CSS_UNPLANNED_SHUTDOWN;
    }

  /* If we can't get the client version, we have to disconnect it. */
  if (client_release == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NET_SERVER_HAND_SHAKE, 1, client_host);
      (void) return_error_to_client (thread_p, rid);
      status = CSS_UNPLANNED_SHUTDOWN;
    }

  /*
   * 1. get the result of compatibility check.
   * 2. check if the both capabilities of client and server are compatible.
   * 3. check if the client has a capability to make it compatible.
   */
  compat = rel_get_net_compatible (client_release, server_release);
  if (check_client_capabilities (thread_p, client_capabilities, rel_compare (client_release, server_release),
				 &compat, client_host) != client_capabilities)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NET_SERVER_HAND_SHAKE, 1, client_host);
      (void) return_error_to_client (thread_p, rid);
    }
  if (compat == REL_NOT_COMPATIBLE)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NET_DIFFERENT_RELEASE, 2, server_release, client_release);
      (void) return_error_to_client (thread_p, rid);
      status = CSS_UNPLANNED_SHUTDOWN;
    }

  /* update connection counters for reserved connections */
  if (css_increment_num_conn ((BOOT_CLIENT_TYPE) client_type) != NO_ERROR)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_CLIENTS_EXCEEDED, 1, NUM_NORMAL_TRANS);
      (void) return_error_to_client (thread_p, rid);
      status = CSS_UNPLANNED_SHUTDOWN;
    }
  else
    {
      thread_p->conn_entry->client_type = (BOOT_CLIENT_TYPE) client_type;
    }

  reply_size = (or_packed_string_length (server_release, &strlen1) + (OR_INT_SIZE * 3)
		+ or_packed_string_length (boot_Host_name, &strlen2));
  ptr = or_pack_string_with_length (reply, (char *) server_release, strlen1);
  ptr = or_pack_string (ptr, NULL);	/* for backward compatibility */
  ptr = or_pack_int (ptr, server_capabilities ());
  ptr = or_pack_int (ptr, rel_bit_platform ());
  ptr = or_pack_string_with_length (ptr, boot_Host_name, strlen2);
  css_send_data_to_client (thread_p->conn_entry, rid, reply, reply_size);

  return status;
}

/*
 * slocator_fetch -
 *
 * return:
 *
 *   thrd(in):
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
slocator_fetch (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  OID oid;
  int chn;
  LOCK lock;
  OID class_oid;
  int class_chn;
  int prefetch;
  LC_COPYAREA *copy_area;
  char *ptr;
  int success;
  OR_ALIGNED_BUF (NET_COPY_AREA_SENDRECV_SIZE + OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *desc_ptr = NULL;
  int desc_size;
  char *content_ptr;
  int content_size;
  int num_objs = 0;
  int fetch_version_type;

  ptr = or_unpack_oid (request, &oid);
  ptr = or_unpack_int (ptr, &chn);
  ptr = or_unpack_lock (ptr, &lock);
  ptr = or_unpack_int (ptr, &fetch_version_type);
  ptr = or_unpack_oid (ptr, &class_oid);
  ptr = or_unpack_int (ptr, &class_chn);
  ptr = or_unpack_int (ptr, &prefetch);

  copy_area = NULL;
  success =
    xlocator_fetch (thread_p, &oid, chn, lock, (LC_FETCH_VERSION_TYPE) fetch_version_type,
		    (LC_FETCH_VERSION_TYPE) fetch_version_type, &class_oid, class_chn, prefetch, &copy_area);

  if (success != NO_ERROR)
    {
      (void) return_error_to_client (thread_p, rid);
    }

  if (copy_area != NULL)
    {
      num_objs = locator_send_copy_area (copy_area, &content_ptr, &content_size, &desc_ptr, &desc_size);
    }
  else
    {
      desc_ptr = NULL;
      desc_size = 0;
      content_ptr = NULL;
      content_size = 0;
    }

  /* Send sizes of databuffer and copy area (descriptor + content) */

  ptr = or_pack_int (reply, num_objs);
  ptr = or_pack_int (ptr, desc_size);
  ptr = or_pack_int (ptr, content_size);
  ptr = or_pack_int (ptr, success);

  if (copy_area == NULL)
    {
      css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
    }
  else
    {
      css_send_reply_and_2_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply), desc_ptr,
					   desc_size, content_ptr, content_size);
      locator_free_copy_area (copy_area);
    }
  if (desc_ptr)
    {
      free_and_init (desc_ptr);
    }
}

/*
 * slocator_get_class -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
slocator_get_class (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  OID class_oid, oid;
  int class_chn;
  LOCK lock;
  int prefetching;
  LC_COPYAREA *copy_area;
  int success;
  char *ptr;
  OR_ALIGNED_BUF (NET_COPY_AREA_SENDRECV_SIZE + OR_OID_SIZE + OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *desc_ptr = NULL;
  int desc_size;
  char *content_ptr;
  int content_size;
  int num_objs = 0;

  ptr = or_unpack_oid (request, &class_oid);
  ptr = or_unpack_int (ptr, &class_chn);
  ptr = or_unpack_oid (ptr, &oid);
  ptr = or_unpack_lock (ptr, &lock);
  ptr = or_unpack_int (ptr, &prefetching);

  copy_area = NULL;
  success = xlocator_get_class (thread_p, &class_oid, class_chn, &oid, lock, prefetching, &copy_area);
  if (success != NO_ERROR)
    {
      (void) return_error_to_client (thread_p, rid);
    }

  if (copy_area != NULL)
    {
      num_objs = locator_send_copy_area (copy_area, &content_ptr, &content_size, &desc_ptr, &desc_size);
    }
  else
    {
      desc_ptr = NULL;
      desc_size = 0;
      content_ptr = NULL;
      content_size = 0;
    }

  /* Send sizes of databuffer and copy area (descriptor + content) */

  ptr = or_pack_int (reply, num_objs);
  ptr = or_pack_int (ptr, desc_size);
  ptr = or_pack_int (ptr, content_size);
  ptr = or_pack_oid (ptr, &class_oid);
  ptr = or_pack_int (ptr, success);

  if (copy_area == NULL)
    {
      css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
    }
  else
    {
      css_send_reply_and_2_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply), desc_ptr,
					   desc_size, content_ptr, content_size);
      locator_free_copy_area (copy_area);
    }

  if (desc_ptr)
    {
      free_and_init (desc_ptr);
    }
}

/*
 * slocator_fetch_all -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
slocator_fetch_all (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  HFID hfid;
  LOCK lock;
  OID class_oid, last_oid;
  int nobjects, nfetched;
  LC_COPYAREA *copy_area;
  int success;
  char *ptr;
  int fetch_version_type;
  OR_ALIGNED_BUF (NET_COPY_AREA_SENDRECV_SIZE + (OR_INT_SIZE * 4) + OR_OID_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *desc_ptr = NULL;
  int desc_size;
  char *content_ptr;
  int content_size;
  int num_objs = 0;

  ptr = or_unpack_hfid (request, &hfid);
  ptr = or_unpack_lock (ptr, &lock);
  ptr = or_unpack_int (ptr, &fetch_version_type);
  ptr = or_unpack_oid (ptr, &class_oid);
  ptr = or_unpack_int (ptr, &nobjects);
  ptr = or_unpack_int (ptr, &nfetched);
  ptr = or_unpack_oid (ptr, &last_oid);

  copy_area = NULL;
  success =
    xlocator_fetch_all (thread_p, &hfid, &lock, (LC_FETCH_VERSION_TYPE) fetch_version_type, &class_oid, &nobjects,
			&nfetched, &last_oid, &copy_area);

  if (success != NO_ERROR)
    {
      (void) return_error_to_client (thread_p, rid);
    }

  if (copy_area != NULL)
    {
      num_objs = locator_send_copy_area (copy_area, &content_ptr, &content_size, &desc_ptr, &desc_size);
    }
  else
    {
      desc_ptr = NULL;
      desc_size = 0;
      content_ptr = NULL;
      content_size = 0;
    }

  /* Send sizes of databuffer and copy area (descriptor + content) */

  ptr = or_pack_int (reply, num_objs);
  ptr = or_pack_int (ptr, desc_size);
  ptr = or_pack_int (ptr, content_size);
  ptr = or_pack_lock (ptr, lock);
  ptr = or_pack_int (ptr, nobjects);
  ptr = or_pack_int (ptr, nfetched);
  ptr = or_pack_oid (ptr, &last_oid);
  ptr = or_pack_int (ptr, success);

  if (copy_area == NULL)
    {
      css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
    }
  else
    {
      css_send_reply_and_2_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply), desc_ptr,
					   desc_size, content_ptr, content_size);
      locator_free_copy_area (copy_area);
    }

  if (desc_ptr)
    {
      free_and_init (desc_ptr);
    }
}

/*
 * slocator_does_exist -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
slocator_does_exist (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  OID oid, class_oid;
  int chn, class_chn, prefetch, doesexist;
  int need_fetching;
  LOCK lock;
  int fetch_version_type;
  LC_COPYAREA *copy_area;
  char *ptr;
  OR_ALIGNED_BUF (NET_COPY_AREA_SENDRECV_SIZE + OR_INT_SIZE + OR_OID_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *desc_ptr = NULL;
  int desc_size;
  char *content_ptr;
  int content_size;
  int num_objs = 0;

  ptr = or_unpack_oid (request, &oid);
  ptr = or_unpack_int (ptr, &chn);
  ptr = or_unpack_lock (ptr, &lock);
  ptr = or_unpack_int (ptr, &fetch_version_type);
  ptr = or_unpack_oid (ptr, &class_oid);
  ptr = or_unpack_int (ptr, &class_chn);
  ptr = or_unpack_int (ptr, &need_fetching);
  ptr = or_unpack_int (ptr, &prefetch);

  copy_area = NULL;
  doesexist =
    xlocator_does_exist (thread_p, &oid, chn, lock, (LC_FETCH_VERSION_TYPE) fetch_version_type, &class_oid, class_chn,
			 need_fetching, prefetch, &copy_area);

  if (doesexist == LC_ERROR)
    {
      (void) return_error_to_client (thread_p, rid);
    }

  if (copy_area != NULL)
    {
      num_objs = locator_send_copy_area (copy_area, &content_ptr, &content_size, &desc_ptr, &desc_size);
    }
  else
    {
      desc_ptr = NULL;
      desc_size = 0;
      content_ptr = NULL;
      content_size = 0;
    }

  /* Send sizes of databuffer and copy area (descriptor + content) */

  ptr = or_pack_int (reply, num_objs);
  ptr = or_pack_int (ptr, desc_size);
  ptr = or_pack_int (ptr, content_size);
  ptr = or_pack_int (ptr, doesexist);
  ptr = or_pack_oid (ptr, &class_oid);

  if (copy_area == NULL)
    {
      css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
    }
  else
    {
      css_send_reply_and_2_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply), desc_ptr,
					   desc_size, content_ptr, content_size);
      locator_free_copy_area (copy_area);
    }
  if (desc_ptr)
    {
      free_and_init (desc_ptr);
    }
}

/*
 * slocator_notify_isolation_incons -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
slocator_notify_isolation_incons (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  LC_COPYAREA *copy_area;
  char *ptr;
  int success;
  OR_ALIGNED_BUF (NET_COPY_AREA_SENDRECV_SIZE + OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *desc_ptr = NULL;
  int desc_size;
  char *content_ptr;
  int content_size;
  int num_objs = 0;

  copy_area = NULL;
  success = xlocator_notify_isolation_incons (thread_p, &copy_area);
  if (success != NO_ERROR)
    {
      (void) return_error_to_client (thread_p, rid);
    }

  if (copy_area != NULL)
    {
      num_objs = locator_send_copy_area (copy_area, &content_ptr, &content_size, &desc_ptr, &desc_size);
    }
  else
    {
      desc_ptr = NULL;
      desc_size = 0;
      content_ptr = NULL;
      content_size = 0;
    }

  /* Send sizes of databuffer and copy area (descriptor + content) */

  ptr = or_pack_int (reply, num_objs);
  ptr = or_pack_int (ptr, desc_size);
  ptr = or_pack_int (ptr, content_size);

  ptr = or_pack_int (ptr, success);

  if (copy_area == NULL)
    {
      css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
    }
  else
    {
      css_send_reply_and_2_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply), desc_ptr,
					   desc_size, content_ptr, content_size);
      locator_free_copy_area (copy_area);
    }
  if (desc_ptr)
    {
      free_and_init (desc_ptr);
    }
}

/*
 * slocator_repl_force - process log applier's request to replicate data
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
slocator_repl_force (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  int size;
  int success;
  LC_COPYAREA *copy_area = NULL, *reply_copy_area = NULL;
  char *ptr;
  int csserror;
  OR_ALIGNED_BUF (NET_COPY_AREA_SENDRECV_SIZE + OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  int content_size;
  char *content_ptr = NULL, *new_content_ptr = NULL;
  char *reply_content_ptr = NULL;
  int num_objs;
  char *packed_desc = NULL;
  int packed_desc_size;
  LC_COPYAREA_MANYOBJS *mobjs, *reply_mobjs;
  char *desc_ptr = NULL;
  int desc_size;

  ptr = or_unpack_int (request, &num_objs);
  ptr = or_unpack_int (ptr, &packed_desc_size);
  ptr = or_unpack_int (ptr, &content_size);

  csserror = 0;

  copy_area = locator_recv_allocate_copyarea (num_objs, &content_ptr, content_size);
  if (copy_area)
    {
      if (num_objs > 0)
	{
	  csserror = css_receive_data_from_client (thread_p->conn_entry, rid, &packed_desc, &size);
	}

      if (csserror)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NET_SERVER_DATA_RECEIVE, 0);
	  css_send_abort_to_client (thread_p->conn_entry, rid);
	  goto end;
	}
      else
	{
	  locator_unpack_copy_area_descriptor (num_objs, copy_area, packed_desc);
	  mobjs = LC_MANYOBJS_PTR_IN_COPYAREA (copy_area);

	  if (content_size > 0)
	    {
	      csserror = css_receive_data_from_client (thread_p->conn_entry, rid, &new_content_ptr, &size);

	      if (new_content_ptr != NULL)
		{
		  memcpy (content_ptr, new_content_ptr, size);
		  free_and_init (new_content_ptr);
		}

	      if (csserror)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NET_SERVER_DATA_RECEIVE, 0);
		  css_send_abort_to_client (thread_p->conn_entry, rid);
		  goto end;
		}

	      /* make copy_area (estimated size) to report errors */
	      reply_copy_area =
		locator_recv_allocate_copyarea (num_objs, &reply_content_ptr, content_size + OR_INT_SIZE * num_objs);
	      reply_mobjs = LC_MANYOBJS_PTR_IN_COPYAREA (reply_copy_area);
	      reply_mobjs->num_objs = 0;
	    }

	  success = xlocator_repl_force (thread_p, copy_area, &reply_copy_area);

	  /*
	   * Send the descriptor and content to handle errors
	   */

	  num_objs = locator_send_copy_area (reply_copy_area, &reply_content_ptr, &content_size, &desc_ptr, &desc_size);

	  ptr = or_pack_int (reply, num_objs);
	  ptr = or_pack_int (ptr, desc_size);
	  ptr = or_pack_int (ptr, content_size);
	  ptr = or_pack_int (ptr, success);

	  if (success != NO_ERROR && success != ER_LC_PARTIALLY_FAILED_TO_FLUSH)
	    {
	      (void) return_error_to_client (thread_p, rid);
	    }

	  css_send_reply_and_2_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply),
					       desc_ptr, desc_size, reply_content_ptr, content_size);
	}
    }

end:
  if (packed_desc)
    {
      free_and_init (packed_desc);
    }
  if (copy_area != NULL)
    {
      locator_free_copy_area (copy_area);
    }
  if (reply_copy_area != NULL)
    {
      locator_free_copy_area (reply_copy_area);
    }
  if (desc_ptr)
    {
      free_and_init (desc_ptr);
    }

  return;
}

/*
 * slocator_force -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
slocator_force (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  int received_size;
  int success;
  LC_COPYAREA *copy_area = NULL;
  char *ptr;
  int csserror;
  OR_ALIGNED_BUF (OR_INT_SIZE * 3) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  int content_size;
  char *content_ptr = NULL, *new_content_ptr = NULL;
  int num_objs;
  char *packed_desc = NULL;
  int packed_desc_size;
  int multi_update_flags;
  LC_COPYAREA_MANYOBJS *mobjs;
  int i, num_ignore_error_list;
  int ignore_error_list[-ER_LAST_ERROR];

  ptr = or_unpack_int (request, &num_objs);
  ptr = or_unpack_int (ptr, &multi_update_flags);
  ptr = or_unpack_int (ptr, &packed_desc_size);
  ptr = or_unpack_int (ptr, &content_size);

  ptr = or_unpack_int (ptr, &num_ignore_error_list);
  for (i = 0; i < num_ignore_error_list; i++)
    {
      ptr = or_unpack_int (ptr, &ignore_error_list[i]);
    }

  csserror = 0;

  copy_area = locator_recv_allocate_copyarea (num_objs, &content_ptr, content_size);
  if (copy_area)
    {
      if (num_objs > 0)
	{
	  csserror = css_receive_data_from_client (thread_p->conn_entry, rid, &packed_desc, &received_size);
	  assert (csserror || packed_desc_size == received_size);
	}

      if (csserror)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NET_SERVER_DATA_RECEIVE, 0);
	  css_send_abort_to_client (thread_p->conn_entry, rid);
	  goto end;
	}
      else
	{
	  locator_unpack_copy_area_descriptor (num_objs, copy_area, packed_desc);
	  mobjs = LC_MANYOBJS_PTR_IN_COPYAREA (copy_area);
	  mobjs->multi_update_flags = multi_update_flags;

	  if (content_size > 0)
	    {
	      csserror = css_receive_data_from_client (thread_p->conn_entry, rid, &new_content_ptr, &received_size);

	      if (new_content_ptr != NULL)
		{
		  memcpy (content_ptr, new_content_ptr, received_size);
		  free_and_init (new_content_ptr);
		}

	      if (csserror)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NET_SERVER_DATA_RECEIVE, 0);
		  css_send_abort_to_client (thread_p->conn_entry, rid);
		  goto end;
		}
	    }

	  success = xlocator_force (thread_p, copy_area, num_ignore_error_list, ignore_error_list);

	  /*
	   * Send the descriptor part since some information about the objects
	   * (e.g., OIDs) may be send back to client.
	   * Don't need to send the content since it is not updated.
	   */

	  locator_pack_copy_area_descriptor (num_objs, copy_area, packed_desc, packed_desc_size);
	  ptr = or_pack_int (reply, success);
	  ptr = or_pack_int (ptr, packed_desc_size);
	  ptr = or_pack_int (ptr, 0);

	  if (success != NO_ERROR && success != ER_LC_PARTIALLY_FAILED_TO_FLUSH)
	    {
	      (void) return_error_to_client (thread_p, rid);
	    }

	  css_send_reply_and_2_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply),
					       packed_desc, packed_desc_size, NULL, 0);
	  if (packed_desc)
	    {
	      free_and_init (packed_desc);
	    }
	}
    }

end:
  if (packed_desc)
    {
      free_and_init (packed_desc);
    }
  if (copy_area != NULL)
    {
      locator_free_copy_area (copy_area);
    }
}

/*
 * slocator_fetch_lockset -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
slocator_fetch_lockset (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  int success;
  LC_COPYAREA *copy_area;
  LC_LOCKSET *lockset;
  OR_ALIGNED_BUF (NET_SENDRECV_BUFFSIZE + NET_COPY_AREA_SENDRECV_SIZE + OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *desc_ptr;
  int desc_size;
  char *content_ptr;
  int content_size;
  char *ptr;
  bool first_call;
  int num_objs;
  char *packed = NULL;
  int packed_size;
  int send_size;

  ptr = or_unpack_int (request, &packed_size);

  if (packed_size == 0 || css_receive_data_from_client (thread_p->conn_entry, rid, &packed, (int *) &packed_size))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NET_SERVER_DATA_RECEIVE, 0);
      css_send_abort_to_client (thread_p->conn_entry, rid);
      if (packed)
	{
	  free_and_init (packed);
	}
      return;
    }

  lockset = locator_allocate_and_unpack_lockset (packed, packed_size, true, true, false);
  free_and_init (packed);

  if ((lockset == NULL) || (lockset->length <= 0))
    {
      (void) return_error_to_client (thread_p, rid);
      ptr = or_pack_int (reply, 0);
      ptr = or_pack_int (ptr, 0);
      ptr = or_pack_int (ptr, 0);
      ptr = or_pack_int (ptr, 0);
      ptr = or_pack_int (ptr, ER_FAILED);
      css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
      return;
    }

  first_call = true;
  do
    {
      desc_ptr = NULL;
      num_objs = 0;

      copy_area = NULL;
      success = xlocator_fetch_lockset (thread_p, lockset, &copy_area);

      if (success != NO_ERROR)
	{
	  (void) return_error_to_client (thread_p, rid);
	}

      if (copy_area != NULL)
	{
	  num_objs = locator_send_copy_area (copy_area, &content_ptr, &content_size, &desc_ptr, &desc_size);
	}
      else
	{
	  desc_ptr = NULL;
	  desc_size = 0;
	  content_ptr = NULL;
	  content_size = 0;
	}

      /* Send sizes of databuffer and copy area (descriptor + content) */

      send_size = locator_pack_lockset (lockset, first_call, false);

      packed = lockset->packed;
      packed_size = lockset->packed_size;

      ptr = or_pack_int (reply, send_size);
      ptr = or_pack_int (ptr, num_objs);
      ptr = or_pack_int (ptr, desc_size);
      ptr = or_pack_int (ptr, content_size);

      ptr = or_pack_int (ptr, success);

      if (copy_area == NULL && lockset == NULL)
	{
	  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
	}
      else
	{
	  css_send_reply_and_3_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply), packed,
					       send_size, desc_ptr, desc_size, content_ptr, content_size);
	}
      if (copy_area != NULL)
	{
	  locator_free_copy_area (copy_area);
	}
      if (desc_ptr)
	{
	  free_and_init (desc_ptr);
	}

      first_call = false;
    }
  while (copy_area && lockset
	 && ((lockset->num_classes_of_reqobjs > lockset->num_classes_of_reqobjs_processed)
	     || (lockset->num_reqobjs > lockset->num_reqobjs_processed)));

  if (lockset)
    {
      locator_free_lockset (lockset);
    }
}

/*
 * slocator_fetch_all_reference_lockset -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
slocator_fetch_all_reference_lockset (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  OID oid;
  int chn;
  LOCK lock;
  OID class_oid;
  int class_chn;
  int prune_level;
  int quit_on_errors;
  int success;
  LC_COPYAREA *copy_area;
  LC_LOCKSET *lockset;
  OR_ALIGNED_BUF (NET_SENDRECV_BUFFSIZE + NET_COPY_AREA_SENDRECV_SIZE + OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *desc_ptr = NULL;
  int desc_size;
  char *content_ptr;
  int content_size;
  char *ptr;
  int num_objs = 0;
  char *packed = NULL;
  int packed_size;
  int send_size = 0;

  ptr = or_unpack_oid (request, &oid);
  ptr = or_unpack_int (ptr, &chn);
  ptr = or_unpack_oid (ptr, &class_oid);
  ptr = or_unpack_int (ptr, &class_chn);
  ptr = or_unpack_lock (ptr, &lock);
  ptr = or_unpack_int (ptr, &quit_on_errors);
  ptr = or_unpack_int (ptr, &prune_level);

  lockset = NULL;
  copy_area = NULL;

  success =
    xlocator_fetch_all_reference_lockset (thread_p, &oid, chn, &class_oid, class_chn, lock, quit_on_errors, prune_level,
					  &lockset, &copy_area);

  if (success != NO_ERROR)
    {
      (void) return_error_to_client (thread_p, rid);
    }

  if (lockset != NULL && lockset->length > 0)
    {
      send_size = locator_pack_lockset (lockset, true, true);

      packed = lockset->packed;
      packed_size = lockset->packed_size;

      if (!packed)
	{
	  (void) return_error_to_client (thread_p, rid);
	  success = ER_FAILED;
	}
    }

  if (copy_area != NULL)
    {
      num_objs = locator_send_copy_area (copy_area, &content_ptr, &content_size, &desc_ptr, &desc_size);
    }
  else
    {
      desc_ptr = NULL;
      desc_size = 0;
      content_ptr = NULL;
      content_size = 0;
    }

  /* Send sizes of databuffer and copy area (descriptor + content) */

  ptr = or_pack_int (reply, send_size);
  ptr = or_pack_int (ptr, num_objs);
  ptr = or_pack_int (ptr, desc_size);
  ptr = or_pack_int (ptr, content_size);

  ptr = or_pack_int (ptr, success);

  if (copy_area == NULL && lockset == NULL)
    {
      css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
    }
  else
    {
      css_send_reply_and_3_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply), packed,
					   send_size, desc_ptr, desc_size, content_ptr, content_size);
      if (copy_area != NULL)
	{
	  locator_free_copy_area (copy_area);
	}

      if (lockset != NULL)
	{
	  locator_free_lockset (lockset);
	}

      if (desc_ptr)
	{
	  free_and_init (desc_ptr);
	}
    }
}

/*
 * slocator_find_class_oid -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
slocator_find_class_oid (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  LC_FIND_CLASSNAME found;
  char *classname;
  OID class_oid;
  LOCK lock;
  char *ptr;
  OR_ALIGNED_BUF (OR_INT_SIZE + OR_OID_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  ptr = or_unpack_string_nocopy (request, &classname);
  ptr = or_unpack_oid (ptr, &class_oid);
  ptr = or_unpack_lock (ptr, &lock);

  found = xlocator_find_class_oid (thread_p, classname, &class_oid, lock);

  if (found == LC_CLASSNAME_ERROR)
    {
      (void) return_error_to_client (thread_p, rid);
    }

  ptr = or_pack_int (reply, found);
  ptr = or_pack_oid (ptr, &class_oid);
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * slocator_reserve_classnames -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
slocator_reserve_classnames (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  LC_FIND_CLASSNAME reserved = LC_CLASSNAME_ERROR;
  int num_classes;
  char **classnames;
  OID *class_oids;
  char *ptr;
  int i;
  int malloc_size;
  char *malloc_area;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  ptr = or_unpack_int (request, &num_classes);

  malloc_size = ((sizeof (char *) + sizeof (OID)) * num_classes);
  malloc_area = (char *) db_private_alloc (thread_p, malloc_size);
  if (malloc_area != NULL)
    {
      classnames = (char **) malloc_area;
      class_oids = (OID *) ((char *) malloc_area + (sizeof (char *) * num_classes));
      for (i = 0; i < num_classes; i++)
	{
	  ptr = or_unpack_string_nocopy (ptr, &classnames[i]);
	  ptr = or_unpack_oid (ptr, &class_oids[i]);
	}
      reserved = xlocator_reserve_class_names (thread_p, num_classes, (const char **) classnames, class_oids);
    }

  if (reserved == LC_CLASSNAME_ERROR)
    {
      (void) return_error_to_client (thread_p, rid);
    }

  (void) or_pack_int (reply, reserved);

  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));

  if (malloc_area)
    {
      db_private_free_and_init (thread_p, malloc_area);
    }
}

/*
 * slocator_get_reserved_class_name_oid () - Send to client whether class name was
 *					reserved by it.
 *
 * return	 : Void.
 * thread_p (in) : Thread entry.
 * rid (in)      : ??
 * request (in)  : Request data.
 * reqlen (in)   : Request.
 */
void
slocator_get_reserved_class_name_oid (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  char *classname;
  OID class_oid = OID_INITIALIZER;
  OR_ALIGNED_BUF (OR_OID_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  int error = NO_ERROR;

  (void) or_unpack_string_nocopy (request, &classname);

  error = xlocator_get_reserved_class_name_oid (thread_p, classname, &class_oid);
  if (error != NO_ERROR)
    {
      ASSERT_ERROR ();
      (void) return_error_to_client (thread_p, rid);
    }
  else
    {
      assert (!OID_ISNULL (&class_oid));
    }
  (void) or_pack_oid (reply, &class_oid);
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * slocator_delete_class_name -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
slocator_delete_class_name (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  char *classname;
  LC_FIND_CLASSNAME deleted;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  (void) or_unpack_string_nocopy (request, &classname);

  deleted = xlocator_delete_class_name (thread_p, classname);
  if (deleted == LC_CLASSNAME_ERROR)
    {
      (void) return_error_to_client (thread_p, rid);
    }

  (void) or_pack_int (reply, deleted);
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * slocator_rename_class_name -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
slocator_rename_class_name (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  char *oldname, *newname;
  OID class_oid;
  LC_FIND_CLASSNAME renamed;
  char *ptr;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  ptr = or_unpack_string_nocopy (request, &oldname);
  ptr = or_unpack_string_nocopy (ptr, &newname);
  ptr = or_unpack_oid (ptr, &class_oid);

  renamed = xlocator_rename_class_name (thread_p, oldname, newname, &class_oid);
  if (renamed == LC_CLASSNAME_ERROR)
    {
      (void) return_error_to_client (thread_p, rid);
    }

  (void) or_pack_int (reply, renamed);
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * slocator_assign_oid -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
slocator_assign_oid (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  HFID hfid;
  int expected_length;
  OID class_oid, perm_oid;
  char *classname;
  int success;
  char *ptr;
  OR_ALIGNED_BUF (OR_INT_SIZE + OR_OID_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  ptr = or_unpack_hfid (request, &hfid);
  ptr = or_unpack_int (ptr, &expected_length);
  ptr = or_unpack_oid (ptr, &class_oid);
  ptr = or_unpack_string_nocopy (ptr, &classname);

  success = ((xlocator_assign_oid (thread_p, &hfid, &perm_oid, expected_length, &class_oid, classname) == NO_ERROR)
	     ? NO_ERROR : ER_FAILED);
  if (success != NO_ERROR)
    {
      (void) return_error_to_client (thread_p, rid);
    }

  ptr = or_pack_int (reply, success);
  ptr = or_pack_oid (ptr, &perm_oid);
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * sqst_server_get_statistics -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
sqst_server_get_statistics (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  OID classoid;
  unsigned int timestamp;
  char *ptr;
  char *buffer;
  int buffer_length;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  ptr = or_unpack_oid (request, &classoid);
  ptr = or_unpack_int (ptr, (int *) &timestamp);

  buffer = xstats_get_statistics_from_server (thread_p, &classoid, timestamp, &buffer_length);
  if (buffer == NULL && buffer_length < 0)
    {
      (void) return_error_to_client (thread_p, rid);
      buffer_length = 0;
    }

  (void) or_pack_int (reply, buffer_length);

  css_send_reply_and_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply), buffer,
				     buffer_length);
  if (buffer != NULL)
    {
      /* since this was copied to the client, we don't need it on the server */
      free_and_init (buffer);
    }
}

/*
 * slog_checkpoint -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
slog_checkpoint (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  int error = NO_ERROR;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  log_wakeup_checkpoint_daemon ();

  /* just send back a dummy message */
  (void) or_pack_errcode (reply, error);
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

#if defined(ENABLE_UNUSED_FUNCTION)
/*
 * slogtb_has_updated -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
slogtb_has_updated (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  int has_updated;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  has_updated = logtb_has_updated (thread_p);

  (void) or_pack_int (reply, has_updated);
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}
#endif /* ENABLE_UNUSED_FUNCTION */
/*
 * slogtb_set_interrupt -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
slogtb_set_interrupt (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  int set;

  (void) or_unpack_int (request, &set);
  xlogtb_set_interrupt (thread_p, set);

  /*
   *  No reply expected...
   */
}

/*
 * slogtb_set_suppress_repl_on_transaction -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
slogtb_set_suppress_repl_on_transaction (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  int set;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  (void) or_unpack_int (request, &set);
  xlogtb_set_suppress_repl_on_transaction (thread_p, set);

  /* always success */
  (void) or_pack_int (reply, NO_ERROR);
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * slogtb_reset_wait_msecs -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
slogtb_reset_wait_msecs (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  int wait_msecs;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  (void) or_unpack_int (request, &wait_msecs);

  wait_msecs = xlogtb_reset_wait_msecs (thread_p, wait_msecs);

  (void) or_pack_int (reply, wait_msecs);
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * slogtb_reset_isolation -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
slogtb_reset_isolation (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  int isolation, error_code;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *ptr;

  ptr = or_unpack_int (request, &isolation);

  error_code = (int) xlogtb_reset_isolation (thread_p, (TRAN_ISOLATION) isolation);

  if (error_code != NO_ERROR)
    {
      (void) return_error_to_client (thread_p, rid);
    }

  (void) or_pack_int (reply, error_code);
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * slogpb_dump_stat -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
slogpb_dump_stat (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  FILE *outfp;
  int file_size;
  char *buffer;
  int buffer_size;
  int send_size;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  (void) or_unpack_int (request, &buffer_size);

  buffer = (char *) db_private_alloc (NULL, buffer_size);
  if (buffer == NULL)
    {
      css_send_abort_to_client (thread_p->conn_entry, rid);
      return;
    }

  outfp = tmpfile ();
  if (outfp == NULL)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      css_send_abort_to_client (thread_p->conn_entry, rid);
      db_private_free_and_init (NULL, buffer);
      return;
    }

  xlogpb_dump_stat (outfp);
  file_size = ftell (outfp);

  /*
   * Send the file in pieces
   */
  rewind (outfp);

  (void) or_pack_int (reply, (int) file_size);
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));

  while (file_size > 0)
    {
      if (file_size > buffer_size)
	{
	  send_size = buffer_size;
	}
      else
	{
	  send_size = file_size;
	}

      file_size -= send_size;
      if (fread (buffer, 1, send_size, outfp) == 0)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  css_send_abort_to_client (thread_p->conn_entry, rid);
	  /*
	   * Continue sending the stuff that was prmoised to client. In this case
	   * junk (i.e., whatever it is in the buffers) is sent.
	   */
	}
      css_send_data_to_client (thread_p->conn_entry, rid, buffer, send_size);
    }
  fclose (outfp);
  db_private_free_and_init (NULL, buffer);
}

/*
 * slog_find_lob_locator -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
slog_find_lob_locator (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  char *locator;
  ES_URI real_locator;
  int real_loc_size;
  LOB_LOCATOR_STATE state;
  OR_ALIGNED_BUF (OR_INT_SIZE + OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *ptr;

  (void) or_unpack_string_nocopy (request, &locator);

  state = xtx_find_lob_locator (thread_p, locator, real_locator);
  real_loc_size = strlen (real_locator) + 1;

  ptr = or_pack_int (reply, real_loc_size);
  (void) or_pack_int (ptr, (int) state);
  css_send_reply_and_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply), real_locator,
				     real_loc_size);
}

/*
 * slog_add_lob_locator -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
slog_add_lob_locator (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  char *locator;
  LOB_LOCATOR_STATE state;
  int tmp_int, error;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *ptr;

  ptr = or_unpack_string_nocopy (request, &locator);
  ptr = or_unpack_int (ptr, &tmp_int);
  state = (LOB_LOCATOR_STATE) tmp_int;

  error = xtx_add_lob_locator (thread_p, locator, state);
  if (error != NO_ERROR)
    {
      (void) return_error_to_client (thread_p, rid);
    }

  or_pack_int (reply, error);
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * slog_change_state_of_locator -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
slog_change_state_of_locator (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  char *locator, *new_locator;
  LOB_LOCATOR_STATE state;
  int tmp_int, error;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *ptr;

  ptr = or_unpack_string_nocopy (request, &locator);
  ptr = or_unpack_string_nocopy (ptr, &new_locator);
  ptr = or_unpack_int (ptr, &tmp_int);
  state = (LOB_LOCATOR_STATE) tmp_int;

  error = xtx_change_state_of_locator (thread_p, locator, new_locator, state);
  if (error != NO_ERROR)
    {
      (void) return_error_to_client (thread_p, rid);
    }

  or_pack_int (reply, error);
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * slog_drop_lob_locator -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
slog_drop_lob_locator (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  char *locator;
  int error;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *ptr;

  ptr = or_unpack_string_nocopy (request, &locator);

  error = xtx_drop_lob_locator (thread_p, locator);
  if (error != NO_ERROR)
    {
      (void) return_error_to_client (thread_p, rid);
    }

  or_pack_int (reply, error);
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

void
slog_supplement_statement (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  int success = NO_ERROR;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *ptr, *start_ptr;

  int ddl_type;
  int obj_type;
  OID classoid;
  OID oid;
  char *stmt_text;

  char *supplemental_data;
  int data_len;
  LOG_TDES *tdes;

  /* CAS and Server are able to have different parameter value, when broker restarted with changed parameter value */
  if (prm_get_integer_value (PRM_ID_SUPPLEMENTAL_LOG) == 1)
    {
      ptr = or_unpack_int (request, &ddl_type);
      ptr = or_unpack_int (ptr, &obj_type);

      ptr = or_unpack_oid (ptr, &classoid);
      ptr = or_unpack_oid (ptr, &oid);

      or_unpack_string_nocopy (ptr, &stmt_text);

      /* ddl_type | obj_type | class OID | OID | stmt_text len | stmt_text */
      data_len =
	OR_INT_SIZE + OR_INT_SIZE + OR_OID_SIZE + OR_OID_SIZE + OR_INT_SIZE + or_packed_string_length (stmt_text, NULL);

      supplemental_data = (char *) malloc (data_len + MAX_ALIGNMENT);
      if (supplemental_data == NULL)
	{
	  success = ER_OUT_OF_VIRTUAL_MEMORY;
	  goto end;
	}

      ptr = start_ptr = supplemental_data;

      ptr = or_pack_int (ptr, ddl_type);
      ptr = or_pack_int (ptr, obj_type);
      ptr = or_pack_oid (ptr, &classoid);
      ptr = or_pack_oid (ptr, &oid);
      ptr = or_pack_int (ptr, strlen (stmt_text));
      ptr = or_pack_string (ptr, stmt_text);

      data_len = ptr - start_ptr;

      tdes = LOG_FIND_CURRENT_TDES (thread_p);

      if (!tdes->has_supplemental_log)
	{
	  log_append_supplemental_info (thread_p, LOG_SUPPLEMENT_TRAN_USER, strlen (tdes->client.get_db_user ()),
					tdes->client.get_db_user ());
	  tdes->has_supplemental_log = true;
	}

      log_append_supplemental_info (thread_p, LOG_SUPPLEMENT_DDL, data_len, (void *) supplemental_data);

      free_and_init (supplemental_data);
    }

end:
  (void) or_pack_int (reply, success);

  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * sacl_reload -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
sacl_reload (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  int error;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  error = xacl_reload (thread_p);
  if (error != NO_ERROR)
    {
      (void) return_error_to_client (thread_p, rid);
    }

  (void) or_pack_errcode (reply, error);
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * sacl_dump -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
sacl_dump (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  FILE *outfp;
  int file_size;
  char *buffer;
  int buffer_size;
  int send_size;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  (void) or_unpack_int (request, &buffer_size);

  buffer = (char *) db_private_alloc (thread_p, buffer_size);
  if (buffer == NULL)
    {
      css_send_abort_to_client (thread_p->conn_entry, rid);
      return;
    }

  outfp = tmpfile ();
  if (outfp == NULL)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      css_send_abort_to_client (thread_p->conn_entry, rid);
      db_private_free_and_init (thread_p, buffer);
      return;
    }

  xacl_dump (thread_p, outfp);
  file_size = ftell (outfp);

  /*
   * Send the file in pieces
   */
  rewind (outfp);

  (void) or_pack_int (reply, (int) file_size);
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));

  while (file_size > 0)
    {
      if (file_size > buffer_size)
	{
	  send_size = buffer_size;
	}
      else
	{
	  send_size = file_size;
	}

      file_size -= send_size;
      if (fread (buffer, 1, send_size, outfp) == 0)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  css_send_abort_to_client (thread_p->conn_entry, rid);
	  /*
	   * Continue sending the stuff that was prmoised to client. In this case
	   * junk (i.e., whatever it is in the buffers) is sent.
	   */
	}
      css_send_data_to_client (thread_p->conn_entry, rid, buffer, send_size);
    }
  fclose (outfp);
  db_private_free_and_init (thread_p, buffer);
}

/*
 * slock_dump -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
slock_dump (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  FILE *outfp;
  int file_size;
  char *buffer;
  int buffer_size;
  int is_contention;
  int send_size;
  char *ptr;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  ptr = or_unpack_int (request, &buffer_size);
  ptr = or_unpack_int (ptr, &is_contention);

  buffer = (char *) db_private_alloc (thread_p, buffer_size);
  if (buffer == NULL)
    {
      css_send_abort_to_client (thread_p->conn_entry, rid);
      return;
    }

  outfp = tmpfile ();
  if (outfp == NULL)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      css_send_abort_to_client (thread_p->conn_entry, rid);
      db_private_free_and_init (thread_p, buffer);
      return;
    }

  xlock_dump (thread_p, outfp, is_contention);
  file_size = ftell (outfp);

  /*
   * Send the file in pieces
   */
  rewind (outfp);

  (void) or_pack_int (reply, (int) file_size);
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));

  while (file_size > 0)
    {
      if (file_size > buffer_size)
	{
	  send_size = buffer_size;
	}
      else
	{
	  send_size = file_size;
	}

      file_size -= send_size;
      if (fread (buffer, 1, send_size, outfp) == 0)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  css_send_abort_to_client (thread_p->conn_entry, rid);
	  /*
	   * Continue sending the stuff that was prmoised to client. In this case
	   * junk (i.e., whatever it is in the buffers) is sent.
	   */
	}
      css_send_data_to_client (thread_p->conn_entry, rid, buffer, send_size);
    }
  fclose (outfp);
  db_private_free_and_init (thread_p, buffer);
}

/*
 * shf_create -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
shf_create (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  int error;
  HFID hfid;
  char *ptr;
  OID class_oid;
  OR_ALIGNED_BUF (OR_INT_SIZE + OR_HFID_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  int reuse_oid = 0;

  ptr = or_unpack_hfid (request, &hfid);
  ptr = or_unpack_oid (ptr, &class_oid);
  ptr = or_unpack_int (ptr, &reuse_oid);

  error = xheap_create (thread_p, &hfid, &class_oid, (bool) reuse_oid);
  if (error != NO_ERROR)
    {
      (void) return_error_to_client (thread_p, rid);
    }

  ptr = or_pack_errcode (reply, error);
  ptr = or_pack_hfid (ptr, &hfid);
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * shf_destroy -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
shf_destroy (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
#if defined(ENABLE_UNUSED_FUNCTION)
  int error;
  HFID hfid;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  (void) or_unpack_hfid (request, &hfid);

  error = xheap_destroy (thread_p, &hfid, NULL);
  if (error != NO_ERROR)
    {
      (void) return_error_to_client (thread_p, rid);
    }

  (void) or_pack_errcode (reply, error);
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
#endif /* ENABLE_UNUSED_FUNCTION */
}

/*
 * shf_destroy_when_new -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
shf_destroy_when_new (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  int error;
  HFID hfid;
  OID class_oid;
  int force;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *ptr;

  ptr = or_unpack_hfid (request, &hfid);
  ptr = or_unpack_oid (ptr, &class_oid);
  ptr = or_unpack_int (ptr, &force);

  error = xheap_destroy_newly_created (thread_p, &hfid, &class_oid, (bool) force);
  if (error != NO_ERROR)
    {
      (void) return_error_to_client (thread_p, rid);
    }

  (void) or_pack_errcode (reply, error);
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * shf_heap_reclaim_addresses -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
shf_heap_reclaim_addresses (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  int error;
  HFID hfid;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *ptr = NULL;

  if (boot_can_compact (thread_p) == false)
    {
      (void) or_pack_errcode (reply, ER_COMPACTDB_ALREADY_STARTED);

      css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));

      return;
    }

  ptr = or_unpack_hfid (request, &hfid);

  error = xheap_reclaim_addresses (thread_p, &hfid);
  if (error != NO_ERROR)
    {
      (void) return_error_to_client (thread_p, rid);
    }

  (void) or_pack_errcode (reply, error);
  if (css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply)) != NO_ERROR)
    {
      boot_compact_stop (thread_p);
    }

}

/*
 * shf_get_maxslotted_reclength -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
shf_get_maxslotted_reclength (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  int maxslotted_reclength;

  (void) xheap_get_maxslotted_reclength (maxslotted_reclength);

  reply = OR_ALIGNED_BUF_START (a_reply);
  (void) or_pack_int (reply, maxslotted_reclength);

  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * sfile_apply_tde_to_class_files -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
sfile_apply_tde_to_class_files (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  int error;
  char *ptr;
  OID class_oid;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  ptr = or_unpack_oid (request, &class_oid);

  error = xfile_apply_tde_to_class_files (thread_p, &class_oid);
  if (error != NO_ERROR)
    {
      (void) return_error_to_client (thread_p, rid);
    }

  ptr = or_pack_errcode (reply, error);
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

void
sdblink_get_crypt_keys (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  int area_size = -1;
  char *reply, *area, *ptr;
  OR_ALIGNED_BUF (OR_INT_SIZE + OR_INT_SIZE) a_reply;
  int err = NO_ERROR;
  unsigned char crypt_key[DBLINK_CRYPT_KEY_LENGTH];
  int length = dblink_get_encrypt_key (crypt_key, sizeof (crypt_key));

  reply = OR_ALIGNED_BUF_START (a_reply);

  if (length < 0)
    {
      (void) return_error_to_client (thread_p, rid);
      area = NULL;
      area_size = 0;
      err = length;
    }
  else
    {
      area_size = OR_INT_SIZE + or_packed_stream_length (length);
      area = (char *) db_private_alloc (thread_p, area_size);
      if (area == NULL)
	{
	  (void) return_error_to_client (thread_p, rid);
	  area_size = 0;
	}
      else
	{
	  ptr = or_pack_int (area, length);
	  ptr = or_pack_stream (ptr, (char *) crypt_key, length);
	}
    }

  ptr = or_pack_int (reply, area_size);
  ptr = or_pack_int (ptr, err);
  css_send_reply_and_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply), area, area_size);

  if (area != NULL)
    {
      db_private_free_and_init (thread_p, area);
    }
}

void
stde_get_data_keys (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  int area_size = -1;
  char *reply, *area, *ptr;
  OR_ALIGNED_BUF (OR_INT_SIZE + OR_INT_SIZE) a_reply;
  int err = NO_ERROR;

  reply = OR_ALIGNED_BUF_START (a_reply);

  if (!tde_is_loaded ())
    {
      (void) return_error_to_client (thread_p, rid);
      area = NULL;
      area_size = 0;
      err = ER_TDE_CIPHER_IS_NOT_LOADED;
    }
  else
    {
      area_size = 3 * or_packed_stream_length (TDE_DATA_KEY_LENGTH);

      area = (char *) db_private_alloc (thread_p, area_size);
      if (area == NULL)
	{
	  (void) return_error_to_client (thread_p, rid);
	  area_size = 0;
	}
      else
	{
	  ptr = or_pack_stream (area, (char *) tde_Cipher.data_keys.perm_key, TDE_DATA_KEY_LENGTH);
	  ptr = or_pack_stream (ptr, (char *) tde_Cipher.data_keys.temp_key, TDE_DATA_KEY_LENGTH);
	  ptr = or_pack_stream (ptr, (char *) tde_Cipher.data_keys.log_key, TDE_DATA_KEY_LENGTH);
	}
    }

  ptr = or_pack_int (reply, area_size);
  ptr = or_pack_int (ptr, err);
  css_send_reply_and_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply), area, area_size);

  if (area != NULL)
    {
      db_private_free_and_init (thread_p, area);
    }
}

/*
 * stde_is_loaded -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
stde_is_loaded (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  char *ptr;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  ptr = or_pack_int (reply, tde_is_loaded ());
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * stde_get_mk_file_path -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
stde_get_mk_file_path (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  char mk_path[PATH_MAX] = { 0, };
  int pathlen = 0;
  int area_size = -1;
  char *reply, *area, *ptr;
  OR_ALIGNED_BUF (OR_INT_SIZE + OR_INT_SIZE) a_reply;
  int err = NO_ERROR;

  reply = OR_ALIGNED_BUF_START (a_reply);

  tde_make_keys_file_fullname (mk_path, boot_db_full_name (), false);

  area_size = or_packed_string_length (mk_path, &pathlen);

  area = (char *) db_private_alloc (thread_p, area_size);
  if (area == NULL)
    {
      (void) return_error_to_client (thread_p, rid);
      area_size = 0;
    }
  else
    {
      ptr = or_pack_string_with_length (area, (char *) mk_path, pathlen);
    }

  ptr = or_pack_int (reply, area_size);
  ptr = or_pack_int (ptr, err);
  css_send_reply_and_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply), area, area_size);

  if (area != NULL)
    {
      db_private_free_and_init (thread_p, area);
    }
}

/*
 * stde_get_set_mk_info -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
stde_get_mk_info (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  int error;
  char *ptr;
  int mk_index;
  time_t created_time, set_time;
  OR_ALIGNED_BUF (OR_INT_SIZE + OR_INT_SIZE + OR_BIGINT_SIZE + OR_BIGINT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  error = xtde_get_mk_info (thread_p, &mk_index, &created_time, &set_time);
  if (error != NO_ERROR)
    {
      (void) return_error_to_client (thread_p, rid);
    }

  ptr = or_pack_errcode (reply, error);
  ptr = or_pack_int (ptr, mk_index);
  ptr = or_pack_int64 (ptr, created_time);
  ptr = or_pack_int64 (ptr, set_time);
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * stde_change_mk_on_server -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
stde_change_mk_on_server (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  int error;
  char *ptr;
  int mk_index;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  ptr = or_unpack_int (request, &mk_index);

  error = xtde_change_mk_without_flock (thread_p, mk_index);
  if (error != NO_ERROR)
    {
      (void) return_error_to_client (thread_p, rid);
    }

  ptr = or_pack_errcode (reply, error);
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * stran_server_commit -
 *
 * return:
 *
 *   thrd(in):
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
stran_server_commit (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  TRAN_STATE state;
  int xretain_lock;
  bool retain_lock, should_conn_reset = false;
  OR_ALIGNED_BUF (OR_INT_SIZE + OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *ptr;
  int row_count = DB_ROW_COUNT_NOT_SET;
  int n_query_ids = 0, i = 0;
  QUERY_ID query_id;

  ptr = or_unpack_int (request, &xretain_lock);
  ptr = or_unpack_int (ptr, &row_count);

  /* First end deferred queries */
  ptr = or_unpack_int (ptr, &n_query_ids);
  for (i = 0; i < n_query_ids; i++)
    {
      ptr = or_unpack_ptr (ptr, &query_id);
      if (query_id > 0)
	{
	  (void) xqmgr_end_query (thread_p, query_id);
	}
    }

  retain_lock = (bool) xretain_lock;

  /* set row count */
  xsession_set_row_count (thread_p, row_count);

  state = stran_server_commit_internal (thread_p, rid, retain_lock, &should_conn_reset);

  ptr = or_pack_int (reply, (int) state);
  ptr = or_pack_int (ptr, (int) should_conn_reset);
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * stran_server_abort -
 *
 * return:
 *
 *   thrd(in):
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
stran_server_abort (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  TRAN_STATE state;
  bool should_conn_reset = false, has_updated;
  OR_ALIGNED_BUF (OR_INT_SIZE + OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *ptr;

  has_updated = logtb_has_updated (thread_p);

  state = stran_server_abort_internal (thread_p, rid, &should_conn_reset);

  ptr = or_pack_int (reply, state);
  ptr = or_pack_int (ptr, (int) should_conn_reset);
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * stran_server_has_updated -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
stran_server_has_updated (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  int has_updated;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  has_updated = xtran_server_has_updated (thread_p);

  (void) or_pack_int (reply, has_updated);
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * stran_server_start_topop -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
stran_server_start_topop (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  int success;
  LOG_LSA topop_lsa;
  OR_ALIGNED_BUF (OR_INT_SIZE + OR_LOG_LSA_ALIGNED_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *ptr;

  success = (xtran_server_start_topop (thread_p, &topop_lsa) == NO_ERROR) ? NO_ERROR : ER_FAILED;
  if (success != NO_ERROR)
    {
      (void) return_error_to_client (thread_p, rid);
    }

  ptr = or_pack_int (reply, success);
  ptr = or_pack_log_lsa (ptr, &topop_lsa);
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * stran_server_end_topop -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
stran_server_end_topop (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  TRAN_STATE state;
  LOG_LSA topop_lsa;
  int xresult;
  LOG_RESULT_TOPOP result;
  OR_ALIGNED_BUF (OR_INT_SIZE + OR_LOG_LSA_ALIGNED_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *ptr;

  (void) or_unpack_int (request, &xresult);
  result = (LOG_RESULT_TOPOP) xresult;

  state = xtran_server_end_topop (thread_p, result, &topop_lsa);

  ptr = or_pack_int (reply, (int) state);
  ptr = or_pack_log_lsa (ptr, &topop_lsa);
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * stran_server_savepoint -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
stran_server_savepoint (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  int success;
  char *savept_name;
  LOG_LSA topop_lsa;
  OR_ALIGNED_BUF (OR_INT_SIZE + OR_LOG_LSA_ALIGNED_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *ptr;

  ptr = or_unpack_string_nocopy (request, &savept_name);

  success = (xtran_server_savepoint (thread_p, savept_name, &topop_lsa) == NO_ERROR) ? NO_ERROR : ER_FAILED;
  if (success != NO_ERROR)
    {
      (void) return_error_to_client (thread_p, rid);
    }

  ptr = or_pack_int (reply, success);
  ptr = or_pack_log_lsa (ptr, &topop_lsa);
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * stran_server_partial_abort -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
stran_server_partial_abort (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  TRAN_STATE state;
  char *savept_name;
  LOG_LSA savept_lsa;
  OR_ALIGNED_BUF (OR_INT_SIZE + OR_LOG_LSA_ALIGNED_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *ptr;

  ptr = or_unpack_string_nocopy (request, &savept_name);

  state = xtran_server_partial_abort (thread_p, savept_name, &savept_lsa);
  if (state != TRAN_UNACTIVE_ABORTED)
    {
      /* Likely the abort failed.. somehow */
      (void) return_error_to_client (thread_p, rid);
    }

  ptr = or_pack_int (reply, (int) state);
  ptr = or_pack_log_lsa (ptr, &savept_lsa);
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * stran_server_is_active_and_has_updated -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
stran_server_is_active_and_has_updated (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  int isactive_has_updated;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  isactive_has_updated = xtran_server_is_active_and_has_updated (thread_p);

  (void) or_pack_int (reply, isactive_has_updated);
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * stran_wait_server_active_trans -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
stran_wait_server_active_trans (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  int status;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  status = xtran_wait_server_active_trans (thread_p);

  (void) or_pack_int (reply, status);
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * stran_is_blocked -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
stran_is_blocked (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  int tran_index;
  bool blocked;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  (void) or_unpack_int (request, &tran_index);

  blocked = xtran_is_blocked (thread_p, tran_index);

  (void) or_pack_int (reply, blocked ? 1 : 0);
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * stran_server_set_global_tran_info -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
stran_server_set_global_tran_info (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  int success;
  int gtrid;
  void *info;
  int size;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  (void) or_unpack_int (request, &gtrid);

  if (css_receive_data_from_client (thread_p->conn_entry, rid, (char **) &info, (int *) &size))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NET_SERVER_DATA_RECEIVE, 0);
      css_send_abort_to_client (thread_p->conn_entry, rid);
      return;
    }

  success = (xtran_server_set_global_tran_info (thread_p, gtrid, info, size) == NO_ERROR) ? NO_ERROR : ER_FAILED;
  if (success != NO_ERROR)
    {
      (void) return_error_to_client (thread_p, rid);
    }

  (void) or_pack_int (reply, success);
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));

  if (info != NULL)
    {
      free_and_init (info);
    }
}

/*
 * stran_server_get_global_tran_info -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
stran_server_get_global_tran_info (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  int success;
  int gtrid;
  void *buffer;
  int size;
  OR_ALIGNED_BUF (OR_INT_SIZE * 2) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply), *ptr;

  ptr = or_unpack_int (request, &gtrid);
  ptr = or_unpack_int (ptr, &size);

  buffer = malloc (size);
  if (buffer == NULL)
    {
      css_send_abort_to_client (thread_p->conn_entry, rid);
      return;
    }

  success = (xtran_server_get_global_tran_info (thread_p, gtrid, buffer, size) == NO_ERROR) ? NO_ERROR : ER_FAILED;
  if (success != NO_ERROR)
    {
      (void) return_error_to_client (thread_p, rid);
      size = 0;
    }

  ptr = or_pack_int (reply, size);
  ptr = or_pack_int (ptr, success);
  css_send_reply_and_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply), (char *) buffer,
				     size);
  free_and_init (buffer);
}

/*
 * stran_server_2pc_start -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
stran_server_2pc_start (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  int gtrid = NULL_TRANID;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  gtrid = xtran_server_2pc_start (thread_p);
  if (gtrid < 0)
    {
      (void) return_error_to_client (thread_p, rid);
    }

  (void) or_pack_int (reply, gtrid);
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * stran_server_2pc_prepare -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
stran_server_2pc_prepare (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  TRAN_STATE state = TRAN_UNACTIVE_UNKNOWN;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  state = xtran_server_2pc_prepare (thread_p);
  if (state != TRAN_UNACTIVE_2PC_PREPARE && state != TRAN_UNACTIVE_COMMITTED)
    {
      /* the prepare failed. */
      (void) return_error_to_client (thread_p, rid);
    }

  (void) or_pack_int (reply, state);
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * stran_server_2pc_recovery_prepared -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
stran_server_2pc_recovery_prepared (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  int count, *gtrids, size, i;
  int reply_size;
  char *reply, *ptr;

  (void) or_unpack_int (request, &size);

  gtrids = (int *) malloc (sizeof (int) * size);
  if (gtrids == NULL)
    {
      css_send_abort_to_client (thread_p->conn_entry, rid);
      return;
    }

  count = xtran_server_2pc_recovery_prepared (thread_p, gtrids, size);
  if (count < 0 || count > size)
    {
      (void) return_error_to_client (thread_p, rid);
    }

  reply_size = OR_INT_SIZE + (OR_INT_SIZE * size);
  reply = (char *) malloc (reply_size);
  if (reply == NULL)
    {
      css_send_abort_to_client (thread_p->conn_entry, rid);
      free_and_init (gtrids);
      return;
    }
  (void) memset (reply, 0, reply_size);
  ptr = or_pack_int (reply, count);
  for (i = 0; i < count; i++)
    {
      ptr = or_pack_int (ptr, gtrids[i]);
    }
  css_send_data_to_client (thread_p->conn_entry, rid, reply, reply_size);

  free_and_init (gtrids);
  free_and_init (reply);
}

/*
 * stran_server_2pc_attach_global_tran -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
stran_server_2pc_attach_global_tran (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  int gtrid;
  int tran_index;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  (void) or_unpack_int (request, &gtrid);

  tran_index = xtran_server_2pc_attach_global_tran (thread_p, gtrid);
  if (tran_index == NULL_TRAN_INDEX)
    {
      (void) return_error_to_client (thread_p, rid);
    }

  (void) or_pack_int (reply, tran_index);
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * stran_server_2pc_prepare_global_tran -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
stran_server_2pc_prepare_global_tran (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  TRAN_STATE state;
  int gtrid;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  (void) or_unpack_int (request, &gtrid);

  state = xtran_server_2pc_prepare_global_tran (thread_p, gtrid);
  if (state != TRAN_UNACTIVE_2PC_PREPARE && state != TRAN_UNACTIVE_COMMITTED)
    {
      /* Likely the prepare failed.. somehow */
      (void) return_error_to_client (thread_p, rid);
    }

  (void) or_pack_int (reply, state);
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * stran_lock_rep_read -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
stran_lock_rep_read (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *ptr;
  int lock_rr_tran;
  int success;

  ptr = request;
  ptr = or_unpack_int (ptr, &lock_rr_tran);

  success = xtran_lock_rep_read (thread_p, (LOCK) lock_rr_tran);

  if (success != NO_ERROR)
    {
      (void) return_error_to_client (thread_p, rid);
    }

  ptr = or_pack_int (reply, success);
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * sboot_initialize_server -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
sboot_initialize_server (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  css_send_abort_to_client (thread_p->conn_entry, rid);
}

/*
 * sboot_register_client -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
sboot_register_client (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  int xint;
  BOOT_CLIENT_CREDENTIAL client_credential;
  BOOT_SERVER_CREDENTIAL server_credential;
  int tran_index, client_lock_wait;
  TRAN_ISOLATION client_isolation;
  TRAN_STATE tran_state;
  int area_size, strlen1, strlen2, strlen3, strlen4;
  char *reply, *area, *ptr;
  packing_unpacker unpacker;

  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;

  reply = OR_ALIGNED_BUF_START (a_reply);

  memset (&server_credential, 0, sizeof (server_credential));

  unpacker.set_buffer (request, (size_t) reqlen);
  unpacker.unpack_all (client_credential, client_lock_wait, xint);
  client_isolation = (TRAN_ISOLATION) xint;

  tran_index = xboot_register_client (thread_p, &client_credential, client_lock_wait, client_isolation, &tran_state,
				      &server_credential);
  if (tran_index == NULL_TRAN_INDEX)
    {
      (void) return_error_to_client (thread_p, rid);
      area = NULL;
      area_size = 0;
    }
  else
    {
      area_size = (OR_INT_SIZE	/* tran_index */
		   + OR_INT_SIZE	/* tran_state */
		   + or_packed_string_length (server_credential.db_full_name, &strlen1)	/* db_full_name */
		   + or_packed_string_length (server_credential.host_name, &strlen2)	/* host_name */
		   + or_packed_string_length (server_credential.lob_path, &strlen3)	/* lob_path */
		   + OR_INT_SIZE	/* process_id */
		   + OR_OID_SIZE	/* root_class_oid */
		   + OR_HFID_SIZE	/* root_class_hfid */
		   + OR_INT_SIZE	/* page_size */
		   + OR_INT_SIZE	/* log_page_size */
		   + OR_FLOAT_SIZE	/* disk_compatibility */
		   + OR_INT_SIZE	/* ha_server_state */
		   + OR_INT_SIZE	/* db_charset */
		   + or_packed_string_length (server_credential.db_lang, &strlen4) /* db_lang */ );

      area = (char *) db_private_alloc (thread_p, area_size);
      if (area == NULL)
	{
	  (void) return_error_to_client (thread_p, rid);
	  area_size = 0;
	}
      else
	{
	  ptr = or_pack_int (area, tran_index);
	  ptr = or_pack_int (ptr, (int) tran_state);
	  ptr = or_pack_string_with_length (ptr, server_credential.db_full_name, strlen1);
	  ptr = or_pack_string_with_length (ptr, server_credential.host_name, strlen2);
	  ptr = or_pack_string_with_length (ptr, server_credential.lob_path, strlen3);
	  ptr = or_pack_int (ptr, server_credential.process_id);
	  ptr = or_pack_oid (ptr, &server_credential.root_class_oid);
	  ptr = or_pack_hfid (ptr, &server_credential.root_class_hfid);
	  ptr = or_pack_int (ptr, (int) server_credential.page_size);
	  ptr = or_pack_int (ptr, (int) server_credential.log_page_size);
	  ptr = or_pack_float (ptr, server_credential.disk_compatibility);
	  ptr = or_pack_int (ptr, (int) server_credential.ha_server_state);
	  ptr = or_pack_int (ptr, server_credential.db_charset);
	  ptr = or_pack_string_with_length (ptr, server_credential.db_lang, strlen4);
	}
    }

  ptr = or_pack_int (reply, area_size);
  css_send_reply_and_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply), area, area_size);

  if (area != NULL)
    {
      db_private_free_and_init (thread_p, area);
    }
}

/*
 * sboot_notify_unregister_client -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
sboot_notify_unregister_client (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  CSS_CONN_ENTRY *conn;
  int tran_index;
  int success = NO_ERROR;
  int r;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  (void) or_unpack_int (request, &tran_index);

  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
      if (thread_p == NULL)
	{
	  return;
	}
    }
  conn = thread_p->conn_entry;
  assert (conn != NULL);

  /* There's an interesting race condition among client, worker thread and connection handler.
   * Please find CBRD-21375 for detail and also see css_connection_handler_thread.
   *
   * It is important to synchronize worker thread with connection handler to avoid the race condition.
   * To change conn->status and send reply to client should be atomic.
   * Otherwise, connection handler may disconnect the connection and it prevents client from receiving the reply.
   */
  r = rmutex_lock (thread_p, &conn->rmutex);
  assert (r == NO_ERROR);

  xboot_notify_unregister_client (thread_p, tran_index);

  (void) or_pack_int (reply, success);
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));

  r = rmutex_unlock (thread_p, &conn->rmutex);
  assert (r == NO_ERROR);
}

/*
 * sboot_backup -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
sboot_backup (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  int success;
  OR_ALIGNED_BUF (OR_INT_SIZE * 3) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *ptr;
  char *backup_path;
  FILEIO_BACKUP_LEVEL backup_level;
  int delete_unneeded_logarchives;
  char *backup_verbose_file;
  int num_threads;
  FILEIO_ZIP_METHOD zip_method;
  FILEIO_ZIP_LEVEL zip_level;
  int skip_activelog;
  int sleep_msecs;
  int separate_keys;

  ptr = or_unpack_string_nocopy (request, &backup_path);
  ptr = or_unpack_int (ptr, (int *) &backup_level);
  ptr = or_unpack_int (ptr, &delete_unneeded_logarchives);
  ptr = or_unpack_string_nocopy (ptr, &backup_verbose_file);
  ptr = or_unpack_int (ptr, &num_threads);
  ptr = or_unpack_int (ptr, (int *) &zip_method);
  ptr = or_unpack_int (ptr, (int *) &zip_level);
  ptr = or_unpack_int (ptr, (int *) &skip_activelog);
  ptr = or_unpack_int (ptr, (int *) &sleep_msecs);
  ptr = or_unpack_int (ptr, (int *) &separate_keys);

  success =
    xboot_backup (thread_p, backup_path, backup_level, delete_unneeded_logarchives, backup_verbose_file, num_threads,
		  zip_method, zip_level, skip_activelog, sleep_msecs, separate_keys);

  if (success != NO_ERROR)
    {
      (void) return_error_to_client (thread_p, rid);
    }

  /*
   * To indicate results we really only need 2 ints, but the remote
   * bo and callback routine was expecting to receive 3 ints.
   */
  ptr = or_pack_int (reply, (int) END_CALLBACK);
  ptr = or_pack_int (ptr, success);
  ptr = or_pack_int (ptr, 0xEEABCDFFL);	/* padding, not used */

  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * sboot_add_volume_extension -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
sboot_add_volume_extension (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *ptr;
  DBDEF_VOL_EXT_INFO ext_info;
  int tmp;
  VOLID volid;
  char *unpack_char;		/* to suppress warning */

  ptr = or_unpack_string_nocopy (request, &unpack_char);
  ext_info.path = unpack_char;
  ptr = or_unpack_string_nocopy (ptr, &unpack_char);
  ext_info.name = unpack_char;
  ptr = or_unpack_string_nocopy (ptr, &unpack_char);
  ext_info.comments = unpack_char;
  ptr = or_unpack_int (ptr, &ext_info.max_npages);
  ptr = or_unpack_int (ptr, &ext_info.max_writesize_in_sec);
  ptr = or_unpack_int (ptr, &tmp);
  ext_info.purpose = (DB_VOLPURPOSE) tmp;
  ptr = or_unpack_int (ptr, &tmp);
  ext_info.overwrite = (bool) tmp;

  volid = xboot_add_volume_extension (thread_p, &ext_info);

  if (volid == NULL_VOLID)
    {
      (void) return_error_to_client (thread_p, rid);
    }

  (void) or_pack_int (reply, (int) volid);
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * sboot_check_db_consistency -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
sboot_check_db_consistency (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  int success;
  OR_ALIGNED_BUF (OR_INT_SIZE * 3) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *ptr;
  int check_flag;
  int num = 0, i;
  OID *oids = NULL;
  BTID index_btid;

  if (request == NULL)
    {
      check_flag = CHECKDB_ALL_CHECK_EXCEPT_PREV_LINK;
    }
  else
    {
      ptr = or_unpack_int (request, &check_flag);
      ptr = or_unpack_int (ptr, &num);
      oids = (OID *) malloc (sizeof (OID) * num);
      if (oids == NULL)
	{
	  success = ER_FAILED;
	  goto function_exit;
	}
      for (i = 0; i < num; i++)
	{
	  ptr = or_unpack_oid (ptr, &oids[i]);
	}
      ptr = or_unpack_btid (ptr, &index_btid);
    }

  success = xboot_check_db_consistency (thread_p, check_flag, oids, num, &index_btid);
  success = success == NO_ERROR ? NO_ERROR : ER_FAILED;
  free_and_init (oids);

  if (success != NO_ERROR)
    {
      (void) return_error_to_client (thread_p, rid);
    }

function_exit:
  /*
   * To indicate results we really only need 2 ints, but the remote
   * bo and callback routine was expecting to receive 3 ints.
   */
  ptr = or_pack_int (reply, (int) END_CALLBACK);
  ptr = or_pack_int (ptr, success);
  ptr = or_pack_int (ptr, 0xEEABCDFFL);	/* padding, not used */
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * sboot_find_number_permanent_volumes -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
sboot_find_number_permanent_volumes (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  int nvols;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  nvols = xboot_find_number_permanent_volumes (thread_p);

  (void) or_pack_int (reply, nvols);
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * sboot_find_number_temp_volumes -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
sboot_find_number_temp_volumes (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  int nvols;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  nvols = xboot_find_number_temp_volumes (thread_p);

  (void) or_pack_int (reply, nvols);
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * sboot_find_last_permanent -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
sboot_find_last_permanent (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  VOLID volid;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  volid = xboot_find_last_permanent (thread_p);

  (void) or_pack_int (reply, (int) volid);
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * sboot_find_last_temp -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
sboot_find_last_temp (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  int nvols;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  nvols = xboot_find_last_temp (thread_p);

  (void) or_pack_int (reply, nvols);
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * sboot_change_ha_mode -
 */
void
sboot_change_ha_mode (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  int req_state, force, timeout;
  HA_SERVER_STATE state;
  DB_INFO *db;
  char *ptr;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  ptr = or_unpack_int (request, &req_state);
  state = (HA_SERVER_STATE) req_state;
  ptr = or_unpack_int (ptr, &force);
  ptr = or_unpack_int (ptr, &timeout);

  if (state > HA_SERVER_STATE_IDLE && state < HA_SERVER_STATE_DEAD)
    {
      if (css_change_ha_server_state (thread_p, state, force, timeout, false) != NO_ERROR)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ERR_CSS_ERROR_FROM_SERVER, 1, "Cannot change server HA mode");
	  (void) return_error_to_client (thread_p, rid);
	}
      else
	{
	  /* it is good chance to call 'css_set_ha_num_of_hosts' */
	  db = cfg_find_db (boot_db_name ());
	  if (db != NULL)
	    {
	      css_set_ha_num_of_hosts (db->num_hosts);
	      cfg_free_directory (db);
	    }
	}
    }

  state = css_ha_server_state ();
  or_pack_int (reply, (int) state);
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * sboot_notify_ha_log_applier_state -
 */
void
sboot_notify_ha_log_applier_state (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  int i, status;
  HA_LOG_APPLIER_STATE state;
  char *ptr;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  ptr = or_unpack_int (request, &i);
  state = (HA_LOG_APPLIER_STATE) i;

  if (state >= HA_LOG_APPLIER_STATE_UNREGISTERED && state <= HA_LOG_APPLIER_STATE_ERROR)
    {
      status = css_notify_ha_log_applier_state (thread_p, state);
      if (status != NO_ERROR)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ERR_CSS_ERROR_FROM_SERVER, 1, "Error in log applier state");
	  (void) return_error_to_client (thread_p, rid);
	}
    }
  else
    {
      status = ER_FAILED;
    }
  or_pack_int (reply, status);
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * sqst_update_statistics -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
sqst_update_statistics (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  int error, with_fullscan;
  OID classoid;
  char *ptr;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  CLASS_ATTR_NDV class_attr_ndv = CLASS_ATTR_NDV_INITIALIZER;

  ptr = or_unpack_int (request, &class_attr_ndv.attr_cnt);

  class_attr_ndv.attr_ndv = (ATTR_NDV *) malloc (sizeof (ATTR_NDV) * (class_attr_ndv.attr_cnt + 1));
  if (class_attr_ndv.attr_ndv == NULL)
    {
      (void) return_error_to_client (thread_p, rid);
    }

  for (int i = 0; i < class_attr_ndv.attr_cnt + 1; i++)
    {
      ptr = or_unpack_int (ptr, &class_attr_ndv.attr_ndv[i].id);
      ptr = or_unpack_int64 (ptr, &class_attr_ndv.attr_ndv[i].ndv);
    }
  ptr = or_unpack_oid (ptr, &classoid);
  ptr = or_unpack_int (ptr, &with_fullscan);

  error =
    xstats_update_statistics (thread_p, &classoid, (with_fullscan ? STATS_WITH_FULLSCAN : STATS_WITH_SAMPLING),
			      &class_attr_ndv);
  if (error != NO_ERROR)
    {
      (void) return_error_to_client (thread_p, rid);
    }

  (void) or_pack_errcode (reply, error);
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * sbtree_add_index -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
sbtree_add_index (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  BTID btid;
  BTID *return_btid = NULL;
  TP_DOMAIN *key_type;
  OID class_oid;
  int attr_id, unique_pk;
  char *ptr;
  int deduplicate_key_pos = -1;

  OR_ALIGNED_BUF (OR_INT_SIZE + OR_BTID_ALIGNED_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  ptr = or_unpack_btid (request, &btid);
  ptr = or_unpack_domain (ptr, &key_type, 0);
  ptr = or_unpack_oid (ptr, &class_oid);
  ptr = or_unpack_int (ptr, &attr_id);
  ptr = or_unpack_int (ptr, &unique_pk);
  ptr = or_unpack_int (ptr, &deduplicate_key_pos);	/* support for SUPPORT_DEDUPLICATE_KEY_MODE */

  return_btid =
    xbtree_add_index (thread_p, &btid, key_type, &class_oid, attr_id, unique_pk, 0, 0, 0, deduplicate_key_pos);
  if (return_btid == NULL)
    {
      (void) return_error_to_client (thread_p, rid);
      ptr = or_pack_int (reply, er_errid ());
    }
  else
    {
      ptr = or_pack_int (reply, NO_ERROR);
    }
  ptr = or_pack_btid (ptr, &btid);
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * sbtree_load_index -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
sbtree_load_index (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  BTID btid;
  BTID *return_btid = NULL;
  OID *class_oids = NULL;
  HFID *hfids = NULL;
  int unique_pk, not_null_flag;
  OID fk_refcls_oid;
  BTID fk_refcls_pk_btid;
  char *bt_name, *fk_name;
  int n_classes, n_attrs, *attr_ids = NULL;
  int *attr_prefix_lengths = NULL;
  TP_DOMAIN *key_type;
  char *ptr;
  OR_ALIGNED_BUF (OR_INT_SIZE * 2 + OR_BTID_ALIGNED_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *pred_stream = NULL;
  int pred_stream_size = 0, size = 0;
  int func_col_id = -1, expr_stream_size = 0, func_attr_index_start = -1;
  int index_info_type;
  char *expr_stream = NULL;
  int csserror;
  int index_status = 0;
  int ib_thread_count = 0;

  ptr = or_unpack_btid (request, &btid);
  ptr = or_unpack_string_nocopy (ptr, &bt_name);
  ptr = or_unpack_domain (ptr, &key_type, 0);

  ptr = or_unpack_int (ptr, &n_classes);
  ptr = or_unpack_int (ptr, &n_attrs);

  ptr = or_unpack_oid_array (ptr, n_classes, &class_oids);
  if (ptr == NULL)
    {
      (void) return_error_to_client (thread_p, rid);
      goto end;
    }

  ptr = or_unpack_int_array (ptr, (n_classes * n_attrs), &attr_ids);
  if (ptr == NULL)
    {
      (void) return_error_to_client (thread_p, rid);
      goto end;
    }

  if (n_classes == 1)
    {
      ptr = or_unpack_int_array (ptr, n_attrs, &attr_prefix_lengths);
      if (ptr == NULL)
	{
	  (void) return_error_to_client (thread_p, rid);
	  goto end;
	}
    }

  ptr = or_unpack_hfid_array (ptr, n_classes, &hfids);
  if (ptr == NULL)
    {
      (void) return_error_to_client (thread_p, rid);
      goto end;
    }

  ptr = or_unpack_int (ptr, &unique_pk);
  ptr = or_unpack_int (ptr, &not_null_flag);

  ptr = or_unpack_oid (ptr, &fk_refcls_oid);
  ptr = or_unpack_btid (ptr, &fk_refcls_pk_btid);
  ptr = or_unpack_string_nocopy (ptr, &fk_name);
  ptr = or_unpack_int (ptr, &index_info_type);
  switch (index_info_type)
    {
    case 0:
      ptr = or_unpack_int (ptr, &pred_stream_size);
      if (pred_stream_size > 0)
	{
	  csserror = css_receive_data_from_client (thread_p->conn_entry, rid, (char **) &pred_stream, &size);
	  if (csserror)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NET_SERVER_DATA_RECEIVE, 0);
	      css_send_abort_to_client (thread_p->conn_entry, rid);
	      goto end;
	    }
	}
      break;

    case 1:
      ptr = or_unpack_int (ptr, &expr_stream_size);
      ptr = or_unpack_int (ptr, &func_col_id);
      ptr = or_unpack_int (ptr, &func_attr_index_start);

      if (expr_stream_size > 0)
	{
	  csserror = css_receive_data_from_client (thread_p->conn_entry, rid, (char **) &expr_stream, &size);
	  if (csserror)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NET_SERVER_DATA_RECEIVE, 0);
	      css_send_abort_to_client (thread_p->conn_entry, rid);
	      goto end;
	    }
	}

      break;
    default:
      break;
    }

  ptr = or_unpack_int (ptr, &index_status);	/* Get index status. */
  ptr = or_unpack_int (ptr, &ib_thread_count);	/* Get thread count. */

  if (index_status == OR_ONLINE_INDEX_BUILDING_IN_PROGRESS)
    {
      return_btid =
	xbtree_load_online_index (thread_p, &btid, bt_name, key_type, class_oids, n_classes, n_attrs, attr_ids,
				  attr_prefix_lengths, hfids, unique_pk, not_null_flag, &fk_refcls_oid,
				  &fk_refcls_pk_btid, fk_name, pred_stream, pred_stream_size, expr_stream,
				  expr_stream_size, func_col_id, func_attr_index_start, ib_thread_count);
    }
  else
    {
      return_btid =
	xbtree_load_index (thread_p, &btid, bt_name, key_type, class_oids, n_classes, n_attrs, attr_ids,
			   attr_prefix_lengths, hfids, unique_pk, not_null_flag, &fk_refcls_oid, &fk_refcls_pk_btid,
			   fk_name, pred_stream, pred_stream_size, expr_stream, expr_stream_size, func_col_id,
			   func_attr_index_start);
    }

  if (return_btid == NULL)
    {
      (void) return_error_to_client (thread_p, rid);
    }

end:

  int err;

  if (return_btid == NULL)
    {
      ASSERT_ERROR_AND_SET (err);
      ptr = or_pack_int (reply, err);
    }
  else
    {
      err = NO_ERROR;
      ptr = or_pack_int (reply, err);
    }

  if (index_status == OR_ONLINE_INDEX_BUILDING_IN_PROGRESS)
    {
      // it may not be really necessary. it just help things don't go worse that client keep caching ex-lock.
      int tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
      LOCK cls_lock = lock_get_object_lock (&class_oids[0], oid_Root_class_oid);

      // in case of shutdown, index loader might be interrupted and got error
      // otherwise, it should restore SCH_M_LOCK
      assert ((err != NO_ERROR && css_is_shutdowning_server ()) || cls_lock == SCH_M_LOCK);
      ptr = or_pack_int (ptr, (int) cls_lock);
    }
  else
    {
      ptr = or_pack_int (ptr, SCH_M_LOCK);	// irrelevant
    }

  ptr = or_pack_btid (ptr, &btid);
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));

  if (class_oids != NULL)
    {
      db_private_free_and_init (thread_p, class_oids);
    }

  if (attr_ids != NULL)
    {
      db_private_free_and_init (thread_p, attr_ids);
    }

  if (attr_prefix_lengths != NULL)
    {
      db_private_free_and_init (thread_p, attr_prefix_lengths);
    }

  if (hfids != NULL)
    {
      db_private_free_and_init (thread_p, hfids);
    }

  if (expr_stream != NULL)
    {
      free_and_init (expr_stream);
    }

  if (pred_stream)
    {
      free_and_init (pred_stream);
    }
}

/*
 * sbtree_delete_index -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
sbtree_delete_index (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  BTID btid;
  int success;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  (void) or_unpack_btid (request, &btid);

  success = (xbtree_delete_index (thread_p, &btid) == NO_ERROR) ? NO_ERROR : ER_FAILED;
  if (success != NO_ERROR)
    {
      (void) return_error_to_client (thread_p, rid);
    }

  (void) or_pack_int (reply, (int) success);
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * slocator_remove_class_from_index -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
slocator_remove_class_from_index (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  OID oid;
  BTID btid;
  HFID hfid;
  int success;
  char *ptr;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  ptr = or_unpack_oid (request, &oid);
  ptr = or_unpack_btid (ptr, &btid);
  ptr = or_unpack_hfid (ptr, &hfid);

  success = (xlocator_remove_class_from_index (thread_p, &oid, &btid, &hfid) == NO_ERROR) ? NO_ERROR : ER_FAILED;
  if (success != NO_ERROR)
    {
      (void) return_error_to_client (thread_p, rid);
    }

  (void) or_pack_int (reply, (int) success);
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * sbtree_find_unique -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
sbtree_find_unique (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  sbtree_find_unique_internal (thread_p, rid, request, reqlen);
}

static void
sbtree_find_unique_internal (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  BTID btid;
  OID class_oid;
  OID oid;
  DB_VALUE key;
  char *ptr;
  int success;
  OR_ALIGNED_BUF (OR_OID_SIZE + OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  ptr = request;
  ptr = or_unpack_value (ptr, &key);
  ptr = or_unpack_oid (ptr, &class_oid);
  ptr = or_unpack_btid (ptr, &btid);

  OID_SET_NULL (&oid);
  success = xbtree_find_unique (thread_p, &btid, S_SELECT_WITH_LOCK, &key, &class_oid, &oid, false);
  if (success == BTREE_ERROR_OCCURRED)
    {
      (void) return_error_to_client (thread_p, rid);
    }

  /* free storage if the key was a string */
  pr_clear_value (&key);

  ptr = or_pack_int (reply, success);
  ptr = or_pack_oid (ptr, &oid);
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * sbtree_find_multi_uniques -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
sbtree_find_multi_uniques (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  OID class_oid;
  OR_ALIGNED_BUF (2 * OR_INT_SIZE) a_reply;
  char *ptr = NULL, *area = NULL;
  BTID *btids = NULL;
  DB_VALUE *keys = NULL;
  OID *oids = NULL;
  int count, needs_pruning, i, found = 0, area_size = 0;
  SCAN_OPERATION_TYPE op_type;
  int error = NO_ERROR;
  BTREE_SEARCH result = BTREE_KEY_FOUND;

  ptr = or_unpack_oid (request, &class_oid);
  ptr = or_unpack_int (ptr, &needs_pruning);
  ptr = or_unpack_int (ptr, &i);
  op_type = (SCAN_OPERATION_TYPE) i;
  ptr = or_unpack_int (ptr, &count);

  if (count <= 0)
    {
      assert_release (count > 0);
      error = ER_FAILED;
      goto cleanup;
    }

  btids = (BTID *) db_private_alloc (thread_p, count * sizeof (BTID));
  if (btids == NULL)
    {
      error = ER_FAILED;
      goto cleanup;
    }
  for (i = 0; i < count; i++)
    {
      ptr = or_unpack_btid (ptr, &btids[i]);
    }
  keys = (DB_VALUE *) db_private_alloc (thread_p, count * sizeof (DB_VALUE));
  if (keys == NULL)
    {
      error = ER_FAILED;
      goto cleanup;
    }
  for (i = 0; i < count; i++)
    {
      ptr = or_unpack_db_value (ptr, &keys[i]);
    }
  result = xbtree_find_multi_uniques (thread_p, &class_oid, needs_pruning, btids, keys, count, op_type, &oids, &found);
  if (result == BTREE_ERROR_OCCURRED)
    {
      error = ER_FAILED;
      goto cleanup;
    }

  /* start packing result */
  if (found > 0)
    {
      /* area size is (int:number of OIDs) + size of packed OIDs */
      area_size = OR_INT_SIZE + (found * sizeof (OID));
      area = (char *) db_private_alloc (thread_p, area_size);
      if (area == NULL)
	{
	  error = ER_FAILED;
	  goto cleanup;
	}
      ptr = or_pack_int (area, found);
      for (i = 0; i < found; i++)
	{
	  ptr = or_pack_oid (ptr, &oids[i]);
	}
    }
  else
    {
      area_size = 0;
      area = NULL;
    }

  /* pack area size */
  ptr = or_pack_int (OR_ALIGNED_BUF_START (a_reply), area_size);
  /* pack error (should be NO_ERROR here) */
  ptr = or_pack_int (ptr, error);

  css_send_reply_and_data_to_client (thread_p->conn_entry, rid, OR_ALIGNED_BUF_START (a_reply),
				     OR_ALIGNED_BUF_SIZE (a_reply), area, area_size);

cleanup:
  if (btids != NULL)
    {
      db_private_free (thread_p, btids);
    }
  if (keys != NULL)
    {
      for (i = 0; i < count; i++)
	{
	  pr_clear_value (&keys[i]);
	}
      db_private_free (thread_p, keys);
    }
  if (oids != NULL)
    {
      db_private_free (thread_p, oids);
    }
  if (area != NULL)
    {
      db_private_free (thread_p, area);
    }

  if (error != NO_ERROR)
    {
      (void) return_error_to_client (thread_p, rid);
      ptr = or_pack_int (OR_ALIGNED_BUF_START (a_reply), 0);
      ptr = or_pack_int (ptr, error);
      ptr = or_pack_int (ptr, 0);
      css_send_reply_and_data_to_client (thread_p->conn_entry, rid, OR_ALIGNED_BUF_START (a_reply),
					 OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0);
    }
}

/*
 * sbtree_class_test_unique -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
sbtree_class_test_unique (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  int success;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  success = xbtree_class_test_unique (thread_p, request, reqlen);

  if (success != NO_ERROR)
    {
      (void) return_error_to_client (thread_p, rid);
    }

  (void) or_pack_int (reply, (int) success);
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * sdk_totalpgs -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
sdk_totalpgs (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  DKNPAGES npages;
  VOLID volid;
  int int_volid;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  (void) or_unpack_int (request, &int_volid);
  volid = (VOLID) int_volid;

  npages = xdisk_get_total_numpages (thread_p, volid);
  if (npages < 0)
    {
      (void) return_error_to_client (thread_p, rid);
    }

  (void) or_pack_int (reply, npages);
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * sdk_freepgs -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
sdk_freepgs (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  DKNPAGES npages;
  VOLID volid;
  int int_volid;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  (void) or_unpack_int (request, &int_volid);
  volid = (VOLID) int_volid;

  npages = xdisk_get_free_numpages (thread_p, volid);
  if (npages < 0)
    {
      (void) return_error_to_client (thread_p, rid);
    }

  (void) or_pack_int (reply, npages);
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * sdk_remarks -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
sdk_remarks (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  int int_volid;
  char *remark;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  int area_length, strlen;
  char *area;

  (void) or_unpack_int (request, &int_volid);

  remark = xdisk_get_remarks (thread_p, (VOLID) int_volid);
  if (remark == NULL)
    {
      (void) return_error_to_client (thread_p, rid);
      area_length = 0;
      area = NULL;
    }
  else
    {
      area_length = or_packed_string_length (remark, &strlen);
      area = (char *) db_private_alloc (thread_p, area_length);
      if (area == NULL)
	{
	  (void) return_error_to_client (thread_p, rid);
	  area_length = 0;
	}
      else
	{
	  (void) or_pack_string_with_length (area, remark, strlen);
	}
    }

  (void) or_pack_int (reply, area_length);
  css_send_reply_and_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply), area,
				     area_length);
  if (remark != NULL)
    {

      /* since this was copied to the client, we don't need it on the server */
      free_and_init (remark);
    }
  if (area)
    {
      db_private_free_and_init (thread_p, area);
    }
}

/*
 * sdk_vlabel -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
sdk_vlabel (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  OR_ALIGNED_BUF (PATH_MAX) a_vol_fullname;
  char *vol_fullname = OR_ALIGNED_BUF_START (a_vol_fullname);
  int int_volid;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  int area_length, strlen;
  char *area;

  (void) or_unpack_int (request, &int_volid);

  if (xdisk_get_fullname (thread_p, (VOLID) int_volid, vol_fullname) == NULL)
    {
      (void) return_error_to_client (thread_p, rid);
      area_length = 0;
      area = NULL;
    }
  else
    {
      area_length = or_packed_string_length (vol_fullname, &strlen);
      area = (char *) db_private_alloc (thread_p, area_length);
      if (area == NULL)
	{
	  (void) return_error_to_client (thread_p, rid);
	  area_length = 0;
	}
      else
	{
	  (void) or_pack_string_with_length (area, vol_fullname, strlen);
	}
    }

  (void) or_pack_int (reply, area_length);
  css_send_reply_and_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply), area,
				     area_length);
  if (area)
    {
      db_private_free_and_init (thread_p, area);
    }
}

/*
 * sqfile_get_list_file_page -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
sqfile_get_list_file_page (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  QUERY_ID query_id;
  int volid, pageid;
  char *ptr;
  OR_ALIGNED_BUF (OR_INT_SIZE * 2) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char page_buf[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT], *aligned_page_buf;
  int page_size;
  int error = NO_ERROR;

  aligned_page_buf = PTR_ALIGN (page_buf, MAX_ALIGNMENT);

  ptr = or_unpack_ptr (request, &query_id);
  ptr = or_unpack_int (ptr, &volid);
  ptr = or_unpack_int (ptr, &pageid);

  if (volid == NULL_VOLID && pageid == NULL_PAGEID)
    {
      goto empty_page;
    }

  error = xqfile_get_list_file_page (thread_p, query_id, volid, pageid, aligned_page_buf, &page_size);
  if (error != NO_ERROR)
    {
      (void) return_error_to_client (thread_p, rid);
      goto empty_page;
    }

  if (page_size == 0)
    {
      goto empty_page;
    }

  ptr = or_pack_int (reply, page_size);
  ptr = or_pack_int (ptr, error);
  css_send_reply_and_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply), aligned_page_buf,
				     page_size);
  return;

empty_page:
  /* setup empty list file page and return it */
  qmgr_setup_empty_list_file (aligned_page_buf);
  page_size = QFILE_PAGE_HEADER_SIZE;
  ptr = or_pack_int (reply, page_size);
  ptr = or_pack_int (ptr, error);
  css_send_reply_and_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply), aligned_page_buf,
				     page_size);
}

/*
 * sqmgr_prepare_query - Process a SERVER_QM_PREPARE request
 *
 * return:
 *
 *   thrd(in):
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 * Receive XASL stream and return XASL file id (QFILE_LIST_ID) as a result.
 * If xasl_buffer == NULL, the server will look up the XASL cache and then
 * return the cached XASL file id if found, otherwise return NULL QFILE_LIST_ID.
 * This function is a counter part to qmgr_prepare_query().
 */
void
sqmgr_prepare_query (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  XASL_ID xasl_id;
  char *ptr = NULL;
  char *reply = NULL, *reply_buffer = NULL;
  int csserror, reply_buffer_size = 0, get_xasl_header = 0;
  int xasl_cache_pinned = 0, recompile_xasl_cache_pinned = 0;
  int recompile_xasl = 0;
  XASL_NODE_HEADER xasl_header;
  OR_ALIGNED_BUF (OR_INT_SIZE + OR_INT_SIZE + OR_XASL_ID_SIZE) a_reply;
  int error = NO_ERROR;
  COMPILE_CONTEXT context = { NULL, NULL, 0, NULL, NULL, 0, false, false, false, SHA1_HASH_INITIALIZER };
  XASL_STREAM stream = { NULL, NULL, NULL, 0 };
  bool was_recompile_xasl = false;
  bool force_recompile = false;

  reply = OR_ALIGNED_BUF_START (a_reply);

  /* unpack query alias string from the request data */
  ptr = or_unpack_string_nocopy (request, &context.sql_hash_text);

  /* unpack query plan from the request data */
  ptr = or_unpack_string_nocopy (ptr, &context.sql_plan_text);

  /* unpack query string from the request data */
  ptr = or_unpack_string_nocopy (ptr, &context.sql_user_text);

  /* unpack size of XASL stream */
  ptr = or_unpack_int (ptr, &stream.buffer_size);
  /* unpack get XASL node header boolean */
  ptr = or_unpack_int (ptr, &get_xasl_header);
  /* unpack pinned xasl cache flag boolean */
  ptr = or_unpack_int (ptr, &xasl_cache_pinned);
  context.is_xasl_pinned_reference = (bool) xasl_cache_pinned;
  /* unpack recompile flag boolean */
  ptr = or_unpack_int (ptr, &recompile_xasl_cache_pinned);
  context.recompile_xasl_pinned = (bool) recompile_xasl_cache_pinned;
  /* unpack recompile_xasl flag boolean */
  ptr = or_unpack_int (ptr, &recompile_xasl);
  context.recompile_xasl = (bool) recompile_xasl;
  /* unpack sha1 */
  ptr = or_unpack_sha1 (ptr, &context.sha1);

  if (get_xasl_header)
    {
      /* need to get XASL node header */
      stream.xasl_header = &xasl_header;
      INIT_XASL_NODE_HEADER (stream.xasl_header);
    }

  if (stream.buffer_size > 0)
    {
      /* receive XASL stream from the client */
      csserror = css_receive_data_from_client (thread_p->conn_entry, rid, &stream.buffer, &stream.buffer_size);
      if (csserror)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NET_SERVER_DATA_RECEIVE, 0);
	  css_send_abort_to_client (thread_p->conn_entry, rid);
	  if (stream.buffer)
	    free_and_init (stream.buffer);
	  return;
	}
    }

  /* call the server routine of query prepare */
  stream.xasl_id = &xasl_id;
  XASL_ID_SET_NULL (stream.xasl_id);

  /* Force recompile must be set to true if client did not intend to recompile, but must reconsider. This can happen
   * if recompile threshold is checked and if one of the related classes suffered significant changes to justify the
   * recompiling.
   * If client already means to recompile, force_recompile is not required.
   * xqmgr_prepare_query will change context.recompile_xasl if force recompile is required.
   */
  was_recompile_xasl = context.recompile_xasl;

  error = xqmgr_prepare_query (thread_p, &context, &stream);
  if (stream.buffer)
    {
      free_and_init (stream.buffer);
    }
  if (error != NO_ERROR)
    {
      ASSERT_ERROR ();
      (void) return_error_to_client (thread_p, rid);

      ptr = or_pack_int (reply, 0);
      ptr = or_pack_int (ptr, error);
    }
  else
    {
      /* Check if we need to force client to recompile. */
      force_recompile = !was_recompile_xasl && context.recompile_xasl;
      if (stream.xasl_id != NULL && !XASL_ID_IS_NULL (stream.xasl_id) && (get_xasl_header || force_recompile))
	{
	  /* pack XASL node header */
	  reply_buffer_size = get_xasl_header ? XASL_NODE_HEADER_SIZE : 0;
	  reply_buffer_size += force_recompile ? OR_INT_SIZE : 0;
	  assert (reply_buffer_size > 0);

	  reply_buffer = (char *) malloc (reply_buffer_size);
	  if (reply_buffer == NULL)
	    {
	      reply_buffer_size = 0;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) reply_buffer_size);
	      error = ER_OUT_OF_VIRTUAL_MEMORY;
	    }
	  else
	    {
	      ptr = reply_buffer;
	      if (get_xasl_header)
		{
		  OR_PACK_XASL_NODE_HEADER (ptr, stream.xasl_header);
		}
	      if (force_recompile)
		{
		  /* Doesn't really matter what we pack... */
		  ptr = or_pack_int (ptr, 1);
		}
	    }
	}

      ptr = or_pack_int (reply, reply_buffer_size);
      ptr = or_pack_int (ptr, NO_ERROR);
      /* pack XASL file id as a reply */
      OR_PACK_XASL_ID (ptr, stream.xasl_id);
    }

  /* send reply and data to the client */
  css_send_reply_and_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply), reply_buffer,
				     reply_buffer_size);

  if (reply_buffer != NULL)
    {
      free_and_init (reply_buffer);
    }
}

/*
 * stran_can_end_after_query_execution - Check whether can end transaction after query execution.
 *
 * return:error code
 *
 *   thread_p(in): thread entry
 *   query_flag(in): query flag
 *   list_id(in): list id
 *   can_end_transaction(out): true, if transaction can be safely ended
 *
 */
STATIC_INLINE int
stran_can_end_after_query_execution (THREAD_ENTRY * thread_p, int query_flag, QFILE_LIST_ID * list_id,
				     bool * can_end_transaction)
{
  QFILE_LIST_SCAN_ID scan_id;
  QFILE_TUPLE_RECORD tuple_record = { NULL, 0 };
  SCAN_CODE qp_scan;
  OR_BUF buf;
  TP_DOMAIN **domains;
  PR_TYPE *pr_type;
  int i, flag, compressed_size = 0, decompressed_size = 0, diff_size, val_length;
  char *tuple_p;
  bool found_compressible_string_domain, exceed_a_page;

  assert (list_id != NULL && list_id->type_list.domp != NULL && can_end_transaction != NULL);

  *can_end_transaction = false;

  if (list_id->page_cnt != 1)
    {
      /* Needs fetch request. Do not allow ending transaction. */
      return NO_ERROR;
    }

  if (list_id->last_offset >= QEWC_MAX_DATA_SIZE)
    {
      /* Needs fetch request. Do not allow ending transaction. */
      return NO_ERROR;
    }

  if (query_flag & RESULT_HOLDABLE)
    {
      /* Holdable result, do not check for compression. */
      *can_end_transaction = true;
      return NO_ERROR;
    }

  domains = list_id->type_list.domp;
  found_compressible_string_domain = false;
  for (i = 0; i < list_id->type_list.type_cnt; i++)
    {
      pr_type = domains[i]->type;
      assert (pr_type != NULL);

      if (pr_type->id == DB_TYPE_VARCHAR || pr_type->id == DB_TYPE_VARNCHAR)
	{
	  found_compressible_string_domain = true;
	  break;
	}
    }

  if (!found_compressible_string_domain)
    {
      /* Not compressible domains, do not check for compression. */
      *can_end_transaction = true;
      return NO_ERROR;
    }

  if (qfile_open_list_scan (list_id, &scan_id) != NO_ERROR)
    {
      return ER_FAILED;
    }

  /* Estimates the data and header information. */
  diff_size = 0;
  exceed_a_page = false;
  while (!exceed_a_page)
    {
      qp_scan = qfile_scan_list_next (thread_p, &scan_id, &tuple_record, PEEK);
      if (qp_scan != S_SUCCESS)
	{
	  break;
	}

      tuple_p = tuple_record.tpl;
      or_init (&buf, tuple_p, QFILE_GET_TUPLE_LENGTH (tuple_p));
      tuple_p += QFILE_TUPLE_LENGTH_SIZE;
      for (i = 0; i < list_id->type_list.type_cnt; i++)
	{
	  flag = QFILE_GET_TUPLE_VALUE_FLAG (tuple_p);
	  val_length = QFILE_GET_TUPLE_VALUE_LENGTH (tuple_p);
	  tuple_p += QFILE_TUPLE_VALUE_HEADER_SIZE;

	  pr_type = domains[i]->type;
	  if (flag != V_UNBOUND && (pr_type->id == DB_TYPE_VARCHAR || pr_type->id == DB_TYPE_VARNCHAR))
	    {
	      buf.ptr = tuple_p;
	      or_get_varchar_compression_lengths (&buf, &compressed_size, &decompressed_size);
	      if (compressed_size != 0)
		{
		  /* Compression used. */
		  diff_size += decompressed_size - compressed_size;
		  if (list_id->last_offset + diff_size >= QEWC_MAX_DATA_SIZE)
		    {
		      /* Needs fetch request. Do not allow ending transaction. */
		      exceed_a_page = true;
		      break;
		    }
		}
	    }

	  tuple_p += val_length;
	}
    }

  qfile_close_scan (thread_p, &scan_id);

  if (qp_scan == S_ERROR)
    {
      // might be interrupted
      return ER_FAILED;
    }

  *can_end_transaction = !exceed_a_page;

  return NO_ERROR;
}

/*
 * sqmgr_execute_query - Process a SERVER_QM_EXECUTE request
 *
 * return:error or no error
 *
 *   thrd(in):
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 * Receive XASL file id and parameter values if exist and return list file id
 * that contains query result. If an error occurs, return NULL QFILE_LIST_ID.
 * This function is a counter part to qmgr_execute_query().
 */
void
sqmgr_execute_query (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  XASL_ID xasl_id;
  QFILE_LIST_ID *list_id;
  int csserror, dbval_cnt, data_size, replydata_size, page_size;
  QUERY_ID query_id = NULL_QUERY_ID;
  char *ptr, *data = NULL, *reply, *replydata = NULL;
  PAGE_PTR page_ptr;
  char page_buf[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT], *aligned_page_buf;
  QUERY_FLAG query_flag;
  OR_ALIGNED_BUF (OR_INT_SIZE * 7 + OR_PTR_ALIGNED_SIZE + OR_CACHE_TIME_SIZE) a_reply;
  CACHE_TIME clt_cache_time;
  CACHE_TIME srv_cache_time;
  int query_timeout;
  XASL_CACHE_ENTRY *xasl_cache_entry_p = NULL;
  char data_buf[EXECUTE_QUERY_MAX_ARGUMENT_DATA_SIZE + MAX_ALIGNMENT], *aligned_data_buf = NULL;
  bool has_updated;

  int response_time = 0;

  TSC_TICKS start_tick, end_tick;
  TSCTIMEVAL tv_diff;

  int queryinfo_string_length = 0;
  char queryinfo_string[QUERY_INFO_BUF_SIZE];

  UINT64 *base_stats = NULL;
  UINT64 *current_stats = NULL;
  UINT64 *diff_stats = NULL;
  char *sql_id = NULL;
  int error_code = NO_ERROR, all_error_code = NO_ERROR;
  int trace_slow_msec, trace_ioreads;
  bool tran_abort = false, has_xasl_entry = false;

  EXECUTION_INFO info = { NULL, NULL, NULL };
  QUERY_ID net_Deferred_end_queries[NET_DEFER_END_QUERIES_MAX], *p_net_Deferred_end_queries = net_Deferred_end_queries;
  int n_query_ids = 0, i = 0;
  bool end_query_allowed, should_conn_reset;
  LOG_TDES *tdes;
  TRAN_STATE tran_state;
  bool is_tran_auto_commit;

  trace_slow_msec = prm_get_integer_value (PRM_ID_SQL_TRACE_SLOW_MSECS);
  trace_ioreads = prm_get_integer_value (PRM_ID_SQL_TRACE_IOREADS);

  if (trace_slow_msec >= 0 || trace_ioreads > 0)
    {
      perfmon_start_watch (thread_p);

      base_stats = perfmon_allocate_values ();
      if (base_stats == NULL)
	{
	  css_send_abort_to_client (thread_p->conn_entry, rid);
	  return;
	}
      if (prm_get_bool_value (PRM_ID_SQL_TRACE_EXECUTION_PLAN) == true)
	{
	  xperfmon_server_copy_stats (thread_p, base_stats);
	}
      else
	{
	  xperfmon_server_copy_stats_for_trace (thread_p, base_stats);
	}

      tsc_getticks (&start_tick);

      if (trace_slow_msec >= 0)
	{
	  thread_p->event_stats.trace_slow_query = true;
	}
    }

  aligned_page_buf = PTR_ALIGN (page_buf, MAX_ALIGNMENT);

  reply = OR_ALIGNED_BUF_START (a_reply);

  /* unpack XASL file id (XASL_ID), number of parameter values, size of the recieved data, and query execution mode
   * flag from the request data */
  ptr = request;
  OR_UNPACK_XASL_ID (ptr, &xasl_id);
  ptr = or_unpack_int (ptr, &dbval_cnt);
  ptr = or_unpack_int (ptr, &data_size);
  ptr = or_unpack_int (ptr, &query_flag);
  OR_UNPACK_CACHE_TIME (ptr, &clt_cache_time);
  ptr = or_unpack_int (ptr, &query_timeout);

  is_tran_auto_commit = IS_TRAN_AUTO_COMMIT (query_flag);
  xsession_set_tran_auto_commit (thread_p, is_tran_auto_commit);

  if (IS_QUERY_EXECUTE_WITH_COMMIT (query_flag))
    {
      ptr = or_unpack_int (ptr, &n_query_ids);
      if (n_query_ids + 1 > NET_DEFER_END_QUERIES_MAX)
	{
	  p_net_Deferred_end_queries = (QUERY_ID *) malloc ((n_query_ids + 1) * sizeof (QUERY_ID));
	  if (p_net_Deferred_end_queries == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
		      (size_t) (n_query_ids + 1) * sizeof (QUERY_ID));
	      css_send_abort_to_client (thread_p->conn_entry, rid);
	      return;
	    }
	}

      for (i = 0; i < n_query_ids; i++)
	{
	  ptr = or_unpack_ptr (ptr, p_net_Deferred_end_queries + i);
	}
    }

  if (IS_QUERY_EXECUTED_WITHOUT_DATA_BUFFERS (query_flag))
    {
      assert (data_size < EXECUTE_QUERY_MAX_ARGUMENT_DATA_SIZE);
      aligned_data_buf = PTR_ALIGN (data_buf, MAX_ALIGNMENT);
      data = aligned_data_buf;
      memcpy (data, ptr, data_size);
    }
  else if (0 < dbval_cnt)
    {
      /* receive parameter values (DB_VALUE) from the client */
      csserror = css_receive_data_from_client (thread_p->conn_entry, rid, &data, &data_size);
      if (csserror || data == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NET_SERVER_DATA_RECEIVE, 0);
	  css_send_abort_to_client (thread_p->conn_entry, rid);
	  if (data)
	    {
	      free_and_init (data);
	    }
	  return;		/* error */
	}
    }

  CACHE_TIME_RESET (&srv_cache_time);

  /* call the server routine of query execute */
  list_id = xqmgr_execute_query (thread_p, &xasl_id, &query_id, dbval_cnt, data, &query_flag, &clt_cache_time,
				 &srv_cache_time, query_timeout, &xasl_cache_entry_p);

  if (data != NULL && data != aligned_data_buf)
    {
      free_and_init (data);
    }

  if (xasl_cache_entry_p != NULL)
    {
      info = xasl_cache_entry_p->sql_info;
    }

  end_query_allowed = IS_QUERY_EXECUTE_WITH_COMMIT (query_flag);
  tdes = LOG_FIND_CURRENT_TDES (thread_p);
  tran_state = tdes->state;
  has_updated = false;

null_list:
  if (list_id == NULL && !CACHE_TIME_EQ (&clt_cache_time, &srv_cache_time))
    {
      ASSERT_ERROR_AND_SET (error_code);

      if (error_code != NO_ERROR)
	{
	  if (info.sql_hash_text != NULL)
	    {
	      if (qmgr_get_sql_id (thread_p, &sql_id, info.sql_hash_text, strlen (info.sql_hash_text)) != NO_ERROR)
		{
		  sql_id = NULL;
		}
	    }

	  if (error_code != ER_QPROC_XASLNODE_RECOMPILE_REQUESTED)
	    {
	      er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_QUERY_EXECUTION_ERROR, 3, error_code,
		      sql_id ? sql_id : "(UNKNOWN SQL_ID)",
		      info.sql_user_text ? info.sql_user_text : "(UNKNOWN USER_TEXT)");
	    }

	  if (sql_id != NULL)
	    {
	      free_and_init (sql_id);
	    }
	}

      tran_abort = need_to_abort_tran (thread_p, &error_code);
      if (tran_abort)
	{
	  if (xasl_cache_entry_p != NULL)
	    {
	      /* Remove transaction id from xasl cache entry before return_error_to_client, where current transaction
	       * may be aborted. Otherwise, another transaction may be resumed and xasl_cache_entry_p may be removed by
	       * that transaction, during class deletion. */
	      has_xasl_entry = true;
	      xcache_unfix (thread_p, xasl_cache_entry_p);
	      xasl_cache_entry_p = NULL;
	    }
	}

      if (IS_QUERY_EXECUTE_WITH_COMMIT (query_flag))
	{
	  /* Get has update before aborting transaction. */
	  has_updated = logtb_has_updated (thread_p);
	}

      tran_state = return_error_to_client (thread_p, rid);
    }

  page_size = 0;
  page_ptr = NULL;
  if (list_id != NULL)
    {
      /* get the first page of the list file */
      if (VPID_ISNULL (&(list_id->first_vpid)))
	{
	  // Note that not all list files have a page, for instance, insert.
	  page_ptr = NULL;
	}
      else
	{
	  page_ptr = qmgr_get_old_page (thread_p, &(list_id->first_vpid), list_id->tfile_vfid);

	  if (page_ptr != NULL)
	    {
	      /* calculate page size */
	      if (QFILE_GET_TUPLE_COUNT (page_ptr) == -2 || QFILE_GET_OVERFLOW_PAGE_ID (page_ptr) != NULL_PAGEID)
		{
		  page_size = DB_PAGESIZE;
		}
	      else
		{
		  int offset = QFILE_GET_LAST_TUPLE_OFFSET (page_ptr);

		  page_size = (offset + QFILE_GET_TUPLE_LENGTH (page_ptr + offset));
		}

	      memcpy (aligned_page_buf, page_ptr, page_size);
	      qmgr_free_old_page_and_init (thread_p, page_ptr, list_id->tfile_vfid);
	      page_ptr = aligned_page_buf;

	      /* for now, allow end query if there is only one page and more ... */
	      if (stran_can_end_after_query_execution (thread_p, query_flag, list_id, &end_query_allowed) != NO_ERROR)
		{
		  (void) return_error_to_client (thread_p, rid);
		}

	      // When !end_query_allowed, it means this execution request is followed by fetch request(s).
	    }
	  else
	    {
	      // might be interrupted to fetch query result
	      ASSERT_ERROR ();
	      QFILE_FREE_AND_INIT_LIST_ID (list_id);

	      goto null_list;
	    }
	}
    }

  replydata_size = list_id ? or_listid_length (list_id) : 0;
  if (0 < replydata_size)
    {
      /* pack list file id as a reply data */
      replydata = (char *) db_private_alloc (thread_p, replydata_size);
      if (replydata != NULL)
	{
	  (void) or_pack_listid (replydata, list_id);
	}
      else
	{
	  replydata_size = 0;
	  (void) return_error_to_client (thread_p, rid);
	}
    }

  /* We may release the xasl cache entry when the transaction aborted. To refer the contents of the freed entry for
   * the case will cause defects. */
  if (tran_abort == false)
    {
      if (trace_slow_msec >= 0 || trace_ioreads > 0)
	{
	  tsc_getticks (&end_tick);
	  tsc_elapsed_time_usec (&tv_diff, end_tick, start_tick);
	  response_time = (tv_diff.tv_sec * 1000) + (tv_diff.tv_usec / 1000);

	  if (base_stats == NULL)
	    {
	      base_stats = perfmon_allocate_values ();
	      if (base_stats == NULL)
		{
		  css_send_abort_to_client (thread_p->conn_entry, rid);
		  return;
		}
	    }

	  current_stats = perfmon_allocate_values ();
	  if (current_stats == NULL)
	    {
	      css_send_abort_to_client (thread_p->conn_entry, rid);
	      goto exit;
	    }
	  diff_stats = perfmon_allocate_values ();
	  if (diff_stats == NULL)
	    {
	      css_send_abort_to_client (thread_p->conn_entry, rid);
	      goto exit;
	    }

	  if (prm_get_bool_value (PRM_ID_SQL_TRACE_EXECUTION_PLAN) == true)
	    {
	      xperfmon_server_copy_stats (thread_p, current_stats);
	      perfmon_calc_diff_stats (diff_stats, current_stats, base_stats);
	    }
	  else
	    {
	      xperfmon_server_copy_stats_for_trace (thread_p, current_stats);
	      perfmon_calc_diff_stats_for_trace (diff_stats, current_stats, base_stats);
	    }

	  if (response_time >= trace_slow_msec)
	    {
	      queryinfo_string_length =
		er_log_slow_query (thread_p, &info, response_time, diff_stats, queryinfo_string);
	      event_log_slow_query (thread_p, &info, response_time, diff_stats);
	    }

	  if (trace_ioreads > 0
	      && diff_stats[pstat_Metadata[PSTAT_PB_NUM_IOREADS].start_offset] >= (UINT64) trace_ioreads)
	    {
	      event_log_many_ioreads (thread_p, &info, response_time, diff_stats);
	    }

	  perfmon_stop_watch (thread_p);
	}

      if (thread_p->event_stats.temp_expand_pages > 0)
	{
	  event_log_temp_expand_pages (thread_p, &info);
	}
    }

  if (xasl_cache_entry_p != NULL)
    {
      has_xasl_entry = true;
      xcache_unfix (thread_p, xasl_cache_entry_p);
      xasl_cache_entry_p = NULL;
    }

  /* pack 'QUERY_END' as a first argument of the reply */
  ptr = or_pack_int (reply, QUERY_END);
  /* pack size of list file id to return as a second argument of the reply */
  ptr = or_pack_int (ptr, replydata_size);
  /* pack size of a page to return as a third argumnet of the reply */
  ptr = or_pack_int (ptr, page_size);
  ptr = or_pack_int (ptr, queryinfo_string_length);

  /* query id to return as a fourth argument of the reply */
  ptr = or_pack_ptr (ptr, query_id);
  /* result cache created time */
  OR_PACK_CACHE_TIME (ptr, &srv_cache_time);

  if (IS_QUERY_EXECUTE_WITH_COMMIT (query_flag))
    {
      /* Try to end transaction and pack the result. */
      p_net_Deferred_end_queries[n_query_ids++] = query_id;
      if (error_code != NO_ERROR)
	{
	  if (error_code != ER_INTERRUPTED && has_xasl_entry)
	    {
	      tran_abort = true;
	      assert (end_query_allowed == true);
	    }
	  else
	    {
	      /* Do not abort the transaction, since XASL cache does not exists, so other fetch may be requested.
	       * Or, the execution was interrupted.
	       */
	      end_query_allowed = false;
	    }
	}

      stran_server_auto_commit_or_abort (thread_p, rid, p_net_Deferred_end_queries, n_query_ids,
					 tran_abort, has_updated, &end_query_allowed, &tran_state, &should_conn_reset);
      /* pack end query result */
      if (end_query_allowed == true)
	{
	  /* query ended */
	  ptr = or_pack_int (ptr, NO_ERROR);
	}
      else
	{
	  /* query not ended */
	  ptr = or_pack_int (ptr, !NO_ERROR);
	}

      /* pack commit/abart/active result */
      ptr = or_pack_int (ptr, (int) tran_state);
      ptr = or_pack_int (ptr, (int) should_conn_reset);
    }

#if !defined(NDEBUG)
  /* suppress valgrind UMW error */
  memset (ptr, 0, OR_ALIGNED_BUF_SIZE (a_reply) - (ptr - reply));
#endif

  css_send_reply_and_3_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply), replydata,
				       replydata_size, page_ptr, page_size, queryinfo_string, queryinfo_string_length);

  /* free QFILE_LIST_ID duplicated by xqmgr_execute_query() */
  if (replydata != NULL)
    {
      db_private_free_and_init (thread_p, replydata);
    }
  if (list_id != NULL)
    {
      QFILE_FREE_AND_INIT_LIST_ID (list_id);
    }

exit:
  if (p_net_Deferred_end_queries != net_Deferred_end_queries)
    {
      free_and_init (p_net_Deferred_end_queries);
    }
  if (base_stats != NULL)
    {
      free_and_init (base_stats);
    }
  if (current_stats != NULL)
    {
      free_and_init (current_stats);
    }
  if (diff_stats != NULL)
    {
      free_and_init (diff_stats);
    }
}

/*
 * er_log_slow_query - log slow query to error log file
 * return:
 *   thread_p(in):
 *   info(in):
 *   time(in):
 *   diff_stats(in):
 *   queryinfo_string(out):
 */
static int
er_log_slow_query (THREAD_ENTRY * thread_p, EXECUTION_INFO * info, int time, UINT64 * diff_stats,
		   char *queryinfo_string)
{
  char stat_buf[STATDUMP_BUF_SIZE];
  char *sql_id;
  int queryinfo_string_length;
  const char *line = "--------------------------------------------------------------------------------";
  const char *title = "Operation";

  if (prm_get_bool_value (PRM_ID_SQL_TRACE_EXECUTION_PLAN) == true)
    {
      perfmon_server_dump_stats_to_buffer (diff_stats, stat_buf, STATDUMP_BUF_SIZE, NULL);
    }
  else
    {
      info->sql_plan_text = NULL;
      stat_buf[0] = '\0';
    }

  if (info->sql_hash_text == NULL
      || qmgr_get_sql_id (thread_p, &sql_id, info->sql_hash_text, strlen (info->sql_hash_text)) != NO_ERROR)
    {
      sql_id = NULL;
    }

  queryinfo_string_length =
    snprintf (queryinfo_string, QUERY_INFO_BUF_SIZE, "%s\n%s\n%s\n %s\n\n /* SQL_ID: %s */ %s%s \n\n%s\n%s\n", line,
	      title, line, info->sql_user_text ? info->sql_user_text : "(UNKNOWN USER_TEXT)",
	      sql_id ? sql_id : "(UNKNOWN SQL_ID)", info->sql_hash_text ? info->sql_hash_text : "(UNKNOWN HASH_TEXT)",
	      info->sql_plan_text ? info->sql_plan_text : "", stat_buf, line);

  if (sql_id != NULL)
    {
      free (sql_id);
    }

  if (queryinfo_string_length >= QUERY_INFO_BUF_SIZE)
    {
      /* string is truncated */
      queryinfo_string_length = QUERY_INFO_BUF_SIZE - 1;
      queryinfo_string[queryinfo_string_length] = '\0';
    }

  er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_SLOW_QUERY, 2, time, queryinfo_string);

  return queryinfo_string_length;
}

/*
 * event_log_slow_query - log slow query to event log file
 * return:
 *   thread_p(in):
 *   info(in):
 *   time(in):
 *   diff_stats(in):
 *   num_bind_vals(in):
 *   bind_vals(in):
 */
static void
event_log_slow_query (THREAD_ENTRY * thread_p, EXECUTION_INFO * info, int time, UINT64 * diff_stats)
{
  FILE *log_fp;
  int indent = 2;
  LOG_TDES *tdes;
  int tran_index;

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tdes = LOG_FIND_TDES (tran_index);
  log_fp = event_log_start (thread_p, "SLOW_QUERY");

  if (tdes == NULL || log_fp == NULL)
    {
      return;
    }

  event_log_print_client_info (tran_index, indent);
  event_log_sql_without_user_oid (log_fp, "%*csql: %s\n", indent,
				  info->sql_hash_text ? info->sql_hash_text : "(UNKNOWN HASH_TEXT)");

  if (tdes->num_exec_queries <= MAX_NUM_EXEC_QUERY_HISTORY)
    {
      event_log_bind_values (thread_p, log_fp, tran_index, tdes->num_exec_queries - 1);
    }

  fprintf (log_fp, "%*ctime: %d\n", indent, ' ', time);
  fprintf (log_fp, "%*cbuffer: fetch=%lld, ioread=%lld, iowrite=%lld\n", indent, ' ',
	   (long long int) diff_stats[pstat_Metadata[PSTAT_PB_NUM_FETCHES].start_offset],
	   (long long int) diff_stats[pstat_Metadata[PSTAT_PB_NUM_IOREADS].start_offset],
	   (long long int) diff_stats[pstat_Metadata[PSTAT_PB_NUM_IOWRITES].start_offset]);
  fprintf (log_fp, "%*cwait: cs=%d, lock=%d, latch=%d\n\n", indent, ' ', TO_MSEC (thread_p->event_stats.cs_waits),
	   TO_MSEC (thread_p->event_stats.lock_waits), TO_MSEC (thread_p->event_stats.latch_waits));

  event_log_end (thread_p);
}

/*
 * event_log_many_ioreads - log many ioreads to event log file
 * return:
 *   thread_p(in):
 *   info(in):
 *   time(in):
 *   diff_stats(in):
 *   num_bind_vals(in):
 *   bind_vals(in):
 */
static void
event_log_many_ioreads (THREAD_ENTRY * thread_p, EXECUTION_INFO * info, int time, UINT64 * diff_stats)
{
  FILE *log_fp;
  int indent = 2;
  LOG_TDES *tdes;
  int tran_index;

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tdes = LOG_FIND_TDES (tran_index);
  log_fp = event_log_start (thread_p, "MANY_IOREADS");

  if (tdes == NULL || log_fp == NULL)
    {
      return;
    }

  event_log_print_client_info (tran_index, indent);
  event_log_sql_without_user_oid (log_fp, "%*csql: %s\n", indent,
				  info->sql_hash_text ? info->sql_hash_text : "(UNKNOWN HASH_TEXT)");

  if (tdes->num_exec_queries <= MAX_NUM_EXEC_QUERY_HISTORY)
    {
      event_log_bind_values (thread_p, log_fp, tran_index, tdes->num_exec_queries - 1);
    }

  fprintf (log_fp, "%*ctime: %d\n", indent, ' ', time);
  fprintf (log_fp, "%*cioreads: %lld\n\n", indent, ' ',
	   (long long int) diff_stats[pstat_Metadata[PSTAT_PB_NUM_IOREADS].start_offset]);

  event_log_end (thread_p);
}

/*
 * event_log_temp_expand_pages - log temp volume expand pages to event log file
 * return:
 *   thread_p(in):
 *   info(in):
 *   num_bind_vals(in):
 *   bind_vals(in):
 */
static void
event_log_temp_expand_pages (THREAD_ENTRY * thread_p, EXECUTION_INFO * info)
{
  FILE *log_fp;
  int indent = 2;
  LOG_TDES *tdes;
  int tran_index;

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tdes = LOG_FIND_TDES (tran_index);
  log_fp = event_log_start (thread_p, "TEMP_VOLUME_EXPAND");

  if (tdes == NULL || log_fp == NULL)
    {
      return;
    }

  event_log_print_client_info (tran_index, indent);
  event_log_sql_without_user_oid (log_fp, "%*csql: %s\n", indent,
				  info->sql_hash_text ? info->sql_hash_text : "(UNKNOWN HASH_TEXT)");

  if (tdes->num_exec_queries <= MAX_NUM_EXEC_QUERY_HISTORY)
    {
      event_log_bind_values (thread_p, log_fp, tran_index, tdes->num_exec_queries - 1);
    }

  fprintf (log_fp, "%*ctime: %d\n", indent, ' ', TO_MSEC (thread_p->event_stats.temp_expand_time));
  fprintf (log_fp, "%*cpages: %d\n\n", indent, ' ', thread_p->event_stats.temp_expand_pages);

  event_log_end (thread_p);
}

/*
 * sqmgr_prepare_and_execute_query -
 *
 * return:
 *
 *   thrd(in):
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
sqmgr_prepare_and_execute_query (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  int var_count, var_datasize, var_actual_datasize;
  QUERY_ID query_id;
  QFILE_LIST_ID *q_result;
  int csserror, listid_length;
  char *xasl_stream;
  int xasl_stream_size;
  char *ptr, *var_data, *list_data;
  OR_ALIGNED_BUF (OR_INT_SIZE * 4 + OR_PTR_ALIGNED_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  PAGE_PTR page_ptr;
  int page_size;
  int dummy_plan_size = 0;
  char page_buf[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT], *aligned_page_buf;
  QUERY_FLAG flag;
  int query_timeout;
  bool is_tran_auto_commit;

  aligned_page_buf = PTR_ALIGN (page_buf, MAX_ALIGNMENT);

  xasl_stream = NULL;
  xasl_stream_size = 0;

  var_data = NULL;
  var_datasize = 0;
  list_data = NULL;
  page_ptr = NULL;
  page_size = 0;
  q_result = NULL;

  csserror = css_receive_data_from_client (thread_p->conn_entry, rid, &xasl_stream, (int *) &xasl_stream_size);
  if (csserror)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NET_SERVER_DATA_RECEIVE, 0);
      css_send_abort_to_client (thread_p->conn_entry, rid);
      goto cleanup;
    }

  ptr = or_unpack_int (request, &var_count);
  ptr = or_unpack_int (ptr, &var_datasize);
  ptr = or_unpack_int (ptr, &flag);
  ptr = or_unpack_int (ptr, &query_timeout);

  if (var_count && var_datasize)
    {
      csserror = css_receive_data_from_client (thread_p->conn_entry, rid, &var_data, (int *) &var_actual_datasize);
      if (csserror)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NET_SERVER_DATA_RECEIVE, 0);
	  css_send_abort_to_client (thread_p->conn_entry, rid);
	  goto cleanup;
	}
    }

  is_tran_auto_commit = IS_TRAN_AUTO_COMMIT (flag);
  xsession_set_tran_auto_commit (thread_p, is_tran_auto_commit);

  /*
   * After this point, xqmgr_prepare_and_execute_query has assumed
   * responsibility for freeing xasl_stream...
   */
  q_result =
    xqmgr_prepare_and_execute_query (thread_p, xasl_stream, xasl_stream_size, &query_id, var_count, var_data, &flag,
				     query_timeout);
  if (var_data)
    {
      free_and_init (var_data);
    }

  if (xasl_stream)
    {
      free_and_init (xasl_stream);	/* allocated at css_receive_data_from_client() */
    }

  if (q_result == NULL)
    {
      (void) return_error_to_client (thread_p, rid);
      listid_length = 0;
    }
  else
    {
      listid_length = or_listid_length (q_result);
    }

  /* listid_length can be reset after pb_fetch() return move this after reset statement ptr = or_pack_int(ptr,
   * listid_length); */

  if (listid_length)
    {
      if (VPID_ISNULL (&q_result->first_vpid))
	{
	  page_ptr = NULL;
	}
      else
	{
	  page_ptr = qmgr_get_old_page (thread_p, &q_result->first_vpid, q_result->tfile_vfid);
	}

      if (page_ptr)
	{
	  if ((QFILE_GET_TUPLE_COUNT (page_ptr) == -2) || (QFILE_GET_OVERFLOW_PAGE_ID (page_ptr) != NULL_PAGEID))
	    {
	      page_size = DB_PAGESIZE;
	    }
	  else
	    {
	      int offset = QFILE_GET_LAST_TUPLE_OFFSET (page_ptr);

	      page_size = (offset + QFILE_GET_TUPLE_LENGTH (page_ptr + offset));
	    }

	  /* to free page_ptr early */
	  memcpy (aligned_page_buf, page_ptr, page_size);
	  qmgr_free_old_page_and_init (thread_p, page_ptr, q_result->tfile_vfid);
	}
      else
	{
	  /*
	   * During query execution, ER_LK_UNILATERALLY_ABORTED may have
	   * occurred.
	   * xqmgr_sync_query() had set this error
	   * so that the transaction will be rolled back.
	   */
	  if (er_errid () < 0)
	    {
	      (void) return_error_to_client (thread_p, rid);
	      listid_length = 0;
	    }
	  /* if query type is not select, page ptr can be null */
	}

      if ((page_size > DB_PAGESIZE) || (page_size < 0))
	{
	  page_size = 0;
	}

      if (listid_length > 0)
	{
	  list_data = (char *) db_private_alloc (thread_p, listid_length);
	  if (list_data == NULL)
	    {
	      listid_length = 0;
	    }
	}

      if (list_data)
	{
	  or_pack_listid (list_data, q_result);
	}
    }
  else
    {
      /* pack a couple of zeros for page_size and query_id since the client will unpack them. */
      listid_length = 0;
      page_size = 0;
      query_id = 0;
    }

  ptr = or_pack_int (reply, (int) QUERY_END);
  ptr = or_pack_int (ptr, listid_length);
  ptr = or_pack_int (ptr, page_size);
  ptr = or_pack_int (ptr, dummy_plan_size);
  ptr = or_pack_ptr (ptr, query_id);

#if !defined(NDEBUG)
  /* suppress valgrind UMW error */
  memset (ptr, 0, OR_ALIGNED_BUF_SIZE (a_reply) - (ptr - reply));
#endif

  css_send_reply_and_3_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply), list_data,
				       listid_length, aligned_page_buf, page_size, NULL, dummy_plan_size);

cleanup:
  if (xasl_stream)
    {
      free_and_init (xasl_stream);	/* allocated at css_receive_data_from_client() */
    }

  if (var_data)
    {
      free_and_init (var_data);
    }
  if (list_data)
    {
      db_private_free_and_init (thread_p, list_data);
    }

  /* since the listid was copied over to the client, we don't need this one on the server */
  if (q_result)
    {
      QFILE_FREE_AND_INIT_LIST_ID (q_result);
    }
}

/*
 * sqmgr_end_query -
 *
 * return:
 *
 *   thrd(in):
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
sqmgr_end_query (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  QUERY_ID query_id;
  int error_code = NO_ERROR;
  int all_error_code = NO_ERROR;
  int n_query_ids = 0, i = 0;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  request = or_unpack_int (request, &n_query_ids);
  for (i = 0; i < n_query_ids; i++)
    {
      request = or_unpack_ptr (request, &query_id);
      if (query_id > 0)
	{
	  error_code = xqmgr_end_query (thread_p, query_id);
	  if (error_code != NO_ERROR)
	    {
	      all_error_code = error_code;
	      /* Continue to try to close as many queries as possible. */
	    }
	}
    }
  if (all_error_code != NO_ERROR)
    {
      (void) return_error_to_client (thread_p, rid);
    }

  (void) or_pack_int (reply, all_error_code);
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * sqmgr_drop_all_query_plans - Process a SERVER_QM_DROP_ALL_PLANS request
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 * Clear all XASL cache entires out upon request of the client.
 * This function is a counter part to qmgr_drop_all_query_plans().
 */
void
sqmgr_drop_all_query_plans (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  int status;
  char *reply;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;

  reply = OR_ALIGNED_BUF_START (a_reply);

  /* call the server routine of query drop plan */
  status = xqmgr_drop_all_query_plans (thread_p);
  if (status != NO_ERROR)
    {
      (void) return_error_to_client (thread_p, rid);
    }

  /* pack status (DB_IN32) as a reply */
  (void) or_pack_int (reply, status);

  /* send reply and data to the client */
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * sqmgr_dump_query_plans -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
sqmgr_dump_query_plans (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  FILE *outfp;
  int file_size;
  char *buffer;
  int buffer_size;
  int send_size;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  (void) or_unpack_int (request, &buffer_size);

  buffer = (char *) db_private_alloc (thread_p, buffer_size);
  if (buffer == NULL)
    {
      css_send_abort_to_client (thread_p->conn_entry, rid);
      return;
    }

  outfp = tmpfile ();
  if (outfp == NULL)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      css_send_abort_to_client (thread_p->conn_entry, rid);
      db_private_free_and_init (thread_p, buffer);
      return;
    }

  xqmgr_dump_query_plans (thread_p, outfp);
  file_size = ftell (outfp);

  /*
   * Send the file in pieces
   */
  rewind (outfp);

  (void) or_pack_int (reply, (int) file_size);
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));

  while (file_size > 0)
    {
      if (file_size > buffer_size)
	{
	  send_size = buffer_size;
	}
      else
	{
	  send_size = file_size;
	}

      file_size -= send_size;
      if (fread (buffer, 1, send_size, outfp) == 0)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  css_send_abort_to_client (thread_p->conn_entry, rid);
	  /*
	   * Continue sending the stuff that was prmoised to client. In this case
	   * junk (i.e., whatever it is in the buffers) is sent.
	   */
	}
      css_send_data_to_client (thread_p->conn_entry, rid, buffer, send_size);
    }

  fclose (outfp);
  db_private_free_and_init (thread_p, buffer);
}

/*
 * sqmgr_dump_query_cache -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
sqmgr_dump_query_cache (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  FILE *outfp;
  int file_size;
  char *buffer;
  int buffer_size;
  int send_size;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  (void) or_unpack_int (request, &buffer_size);

  buffer = (char *) db_private_alloc (thread_p, buffer_size);
  if (buffer == NULL)
    {
      css_send_abort_to_client (thread_p->conn_entry, rid);
      return;
    }

  outfp = tmpfile ();
  if (outfp == NULL)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      css_send_abort_to_client (thread_p->conn_entry, rid);
      db_private_free_and_init (thread_p, buffer);
      return;
    }

  xqmgr_dump_query_cache (thread_p, outfp);
  file_size = ftell (outfp);

  /*
   * Send the file in pieces
   */
  rewind (outfp);

  (void) or_pack_int (reply, (int) file_size);
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));

  while (file_size > 0)
    {
      if (file_size > buffer_size)
	{
	  send_size = buffer_size;
	}
      else
	{
	  send_size = file_size;
	}

      file_size -= send_size;
      if (fread (buffer, 1, send_size, outfp) == 0)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  css_send_abort_to_client (thread_p->conn_entry, rid);
	  /*
	   * Continue sending the stuff that was prmoised to client. In this case
	   * junk (i.e., whatever it is in the buffers) is sent.
	   */
	}
      css_send_data_to_client (thread_p->conn_entry, rid, buffer, send_size);
    }
  fclose (outfp);
  db_private_free_and_init (thread_p, buffer);
}

/*
 * sqp_get_sys_timestamp -
 *
 * return:
 *
 *   rid(in):
 *
 * NOTE:
 */
void
sqp_get_sys_timestamp (THREAD_ENTRY * thread_p, unsigned int rid, char *request_ignore, int reqlen_ignore)
{
#if defined(ENABLE_UNUSED_FUNCTION)
  OR_ALIGNED_BUF (OR_UTIME_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  DB_VALUE sys_timestamp;

  db_sys_timestamp (&sys_timestamp);
  (void) or_pack_utime (reply, *(DB_TIMESTAMP *) db_get_timestamp (&sys_timestamp));
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
#endif /* ENABLE_UNUSED_FUNCTION */
}

/*
 * sserial_get_current_value -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
sserial_get_current_value (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  int error_status = NO_ERROR;
  DB_VALUE cur_val;
  OID oid;
  int cached_num;
  int buffer_length;
  char *buffer;
  OR_ALIGNED_BUF (OR_INT_SIZE + OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *p;

  p = or_unpack_oid (request, &oid);
  p = or_unpack_int (p, &cached_num);

  error_status = xserial_get_current_value (thread_p, &cur_val, &oid, cached_num);
  if (error_status != NO_ERROR)
    {
      assert (er_errid () != NO_ERROR);
      error_status = er_errid ();
      buffer_length = 0;
      buffer = NULL;
    }
  else
    {
      buffer_length = or_db_value_size (&cur_val);
      buffer = (char *) db_private_alloc (thread_p, buffer_length);
      if (buffer == NULL)
	{
	  error_status = ER_OUT_OF_VIRTUAL_MEMORY;
	  buffer_length = 0;
	}
    }
  p = or_pack_int (reply, buffer_length);
  p = or_pack_int (p, error_status);

  if (buffer == NULL)
    {
      (void) return_error_to_client (thread_p, rid);
      buffer_length = 0;
    }
  else
    {
      (void) or_pack_value (buffer, &cur_val);
      db_value_clear (&cur_val);
    }

  css_send_reply_and_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply), buffer,
				     buffer_length);
  if (buffer != NULL)
    {
      /* since this was copied to the client, we don't need it on the server */
      db_private_free_and_init (thread_p, buffer);
    }
}

/*
 * sserial_get_next_value -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
sserial_get_next_value (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  DB_VALUE next_val;
  OID oid;
  char *buffer;
  int cached_num, num_alloc, is_auto_increment;
  int buffer_length, errid;
  OR_ALIGNED_BUF (OR_INT_SIZE + OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *p;

  p = or_unpack_oid (request, &oid);
  p = or_unpack_int (p, &cached_num);
  p = or_unpack_int (p, &num_alloc);
  p = or_unpack_int (p, &is_auto_increment);

  /*
   * If a client wants to generate AUTO_INCREMENT value during client-side
   * insertion, a server should update LAST_INSERT_ID on a session.
   */
  errid = xserial_get_next_value (thread_p, &next_val, &oid, cached_num, num_alloc, is_auto_increment, true);

  if (errid != NO_ERROR)
    {
      buffer_length = 0;
      buffer = NULL;
    }
  else
    {
      buffer_length = or_db_value_size (&next_val);
      buffer = (char *) db_private_alloc (thread_p, buffer_length);
      if (buffer == NULL)
	{
	  buffer_length = 0;
	  errid = ER_OUT_OF_VIRTUAL_MEMORY;
	}
    }
  p = or_pack_int (reply, buffer_length);
  p = or_pack_int (p, errid);

  if (buffer == NULL)
    {
      (void) return_error_to_client (thread_p, rid);
    }
  else
    {
      (void) or_pack_value (buffer, &next_val);
      db_value_clear (&next_val);
    }

  css_send_reply_and_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply), buffer,
				     buffer_length);
  if (buffer != NULL)
    {
      /* since this was copied to the client, we don't need it on the server */
      db_private_free_and_init (thread_p, buffer);
    }
}

/*
 * sserial_decache -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
sserial_decache (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  OID oid;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  (void) or_unpack_oid (request, &oid);
  xserial_decache (thread_p, &oid);

  (void) or_pack_int (reply, NO_ERROR);
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * ssynonym_remove_xasl_by_oid -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
ssynonym_remove_xasl_by_oid (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  OID oid;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  (void) or_unpack_oid (request, &oid);
  xsynonym_remove_xasl_by_oid (thread_p, &oid);

  (void) or_pack_int (reply, NO_ERROR);
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * smnt_server_start_stats -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
smnt_server_start_stats (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  perfmon_start_watch (thread_p);

  (void) or_pack_int (reply, NO_ERROR);
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * smnt_server_stop_stats -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
smnt_server_stop_stats (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  perfmon_stop_watch (thread_p);
  /* dummy reply message */
  (void) or_pack_int (reply, 1);
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * smnt_server_copy_stats -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
smnt_server_copy_stats (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  char *reply = NULL;
  int nr_statistic_values;
  UINT64 *stats = NULL;

  nr_statistic_values = perfmon_get_number_of_statistic_values ();
  stats = perfmon_allocate_values ();

  if (stats == NULL)
    {
      ASSERT_ERROR ();
      css_send_abort_to_client (thread_p->conn_entry, rid);
      return;
    }

  reply = perfmon_allocate_packed_values_buffer ();
  if (reply == NULL)
    {
      ASSERT_ERROR ();
      free_and_init (stats);
      css_send_abort_to_client (thread_p->conn_entry, rid);
      return;
    }

  xperfmon_server_copy_stats (thread_p, stats);
  perfmon_pack_stats (reply, stats);
  css_send_data_to_client (thread_p->conn_entry, rid, reply, nr_statistic_values * sizeof (UINT64));
  free_and_init (stats);
  free_and_init (reply);
}

/*
 * smnt_server_copy_global_stats -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
smnt_server_copy_global_stats (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  char *reply = NULL;
  int nr_statistic_values;
  UINT64 *stats = NULL;

  nr_statistic_values = perfmon_get_number_of_statistic_values ();
  stats = perfmon_allocate_values ();
  if (stats == NULL)
    {
      ASSERT_ERROR ();
      css_send_abort_to_client (thread_p->conn_entry, rid);
      return;
    }

  reply = perfmon_allocate_packed_values_buffer ();
  if (reply == NULL)
    {
      ASSERT_ERROR ();
      free_and_init (stats);
      css_send_abort_to_client (thread_p->conn_entry, rid);
      return;
    }

  xperfmon_server_copy_global_stats (stats);
  perfmon_pack_stats (reply, stats);
  css_send_data_to_client (thread_p->conn_entry, rid, reply, nr_statistic_values * sizeof (UINT64));
  free_and_init (stats);
  free_and_init (reply);
}

/*
 * sct_check_rep_dir -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
sct_check_rep_dir (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  OID classoid;
  OID rep_dir;
  int success;
  OR_ALIGNED_BUF (OR_INT_SIZE + OR_OID_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *ptr;

  ptr = or_unpack_oid (request, &classoid);
  OID_SET_NULL (&rep_dir);	/* init */

  success = xcatalog_check_rep_dir (thread_p, &classoid, &rep_dir);
  if (success != NO_ERROR)
    {
      (void) return_error_to_client (thread_p, rid);
    }

  assert (success != NO_ERROR || !OID_ISNULL (&rep_dir));

  ptr = or_pack_int (reply, (int) success);
  ptr = or_pack_oid (ptr, &rep_dir);

  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * xs_send_method_call_info_to_client -
 *
 * return:
 *
 *   list_id(in):
 *   method_sig_list(in):
 *
 * NOTE:
 */
int
xs_send_method_call_info_to_client (THREAD_ENTRY * thread_p, qfile_list_id * list_id, method_sig_list * methsg_list)
{
  int length = 0;
  char *databuf;
  char *ptr;
  unsigned int rid;
  OR_ALIGNED_BUF (OR_INT_SIZE * 2) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  rid = css_get_comm_request_id (thread_p);
  length = or_listid_length ((void *) list_id);
  length += or_method_sig_list_length ((void *) methsg_list);
  ptr = or_pack_int (reply, (int) METHOD_CALL);
  ptr = or_pack_int (ptr, length);

#if !defined(NDEBUG)
  /* suppress valgrind UMW error */
  memset (ptr, 0, OR_ALIGNED_BUF_SIZE (a_reply) - (ptr - reply));
#endif

  databuf = (char *) db_private_alloc (thread_p, length);
  if (databuf == NULL)
    {
      return ER_FAILED;
    }

  ptr = or_pack_listid (databuf, (void *) list_id);
  ptr = or_pack_method_sig_list (ptr, (void *) methsg_list);
  css_send_reply_and_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply), databuf, length);
  db_private_free_and_init (thread_p, databuf);
  return NO_ERROR;
}

/*
 * xs_receive_data_from_client -
 *
 * return:
 *
 *   area(in):
 *   datasize(in):
 *
 * NOTE:
 */
int
xs_receive_data_from_client (THREAD_ENTRY * thread_p, char **area, int *datasize)
{
  return xs_receive_data_from_client_with_timeout (thread_p, area, datasize, -1);
}

/*
 * xs_receive_data_from_client_with_timeout -
 *
 * return:
 *
 *   area(in):
 *   datasize(in):
 *   timeout (in):
 *
 * NOTE:
 */
int
xs_receive_data_from_client_with_timeout (THREAD_ENTRY * thread_p, char **area, int *datasize, int timeout)
{
  unsigned int rid;
  int rc = 0;
  bool continue_checking = true;

  if (*area)
    {
      free_and_init (*area);
    }
  rid = css_get_comm_request_id (thread_p);

  rc = css_receive_data_from_client_with_timeout (thread_p->conn_entry, rid, area, (int *) datasize, timeout);

  if (rc == TIMEDOUT_ON_QUEUE)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NET_DATA_RECEIVE_TIMEDOUT, 0);
      return ER_NET_DATA_RECEIVE_TIMEDOUT;
    }
  else if (rc != 0)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NET_SERVER_DATA_RECEIVE, 0);
      return ER_FAILED;
    }

  if (logtb_is_interrupted (thread_p, false, &continue_checking))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INTERRUPTED, 0);
      return ER_FAILED;
    }

  return NO_ERROR;
}

/*
 * slocator_assign_oid_batch -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
slocator_assign_oid_batch (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  int success;
  LC_OIDSET *oidset = NULL;

  /* skip over the word at the front reserved for the return code */
  oidset = locator_unpack_oid_set_to_new (thread_p, request + OR_INT_SIZE);
  if (oidset == NULL)
    {
      (void) return_error_to_client (thread_p, rid);
      return;
    }

  success = xlocator_assign_oid_batch (thread_p, oidset);

  /* the buffer we send back is identical in size to the buffer that was received so we can reuse it. */

  /* first word is reserved for return code */
  or_pack_int (request, success);
  if (success == NO_ERROR)
    {
      if (locator_pack_oid_set (request + OR_INT_SIZE, oidset) == NULL)
	{
	  /* trouble packing oidset for the return trip, severe error */
	  success = ER_FAILED;
	  or_pack_int (request, success);
	}
    }

  if (success != NO_ERROR)
    {
      (void) return_error_to_client (thread_p, rid);
    }

  css_send_data_to_client (thread_p->conn_entry, rid, request, reqlen);

  locator_free_oid_set (thread_p, oidset);
}

/*
 * slocator_find_lockhint_class_oids -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
slocator_find_lockhint_class_oids (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  int num_classes;
  char **many_classnames;
  LOCK *many_locks = NULL;
  int *many_need_subclasses = NULL;
  OID *guessed_class_oids = NULL;
  int *guessed_class_chns = NULL;
  LC_PREFETCH_FLAGS *many_flags = NULL;
  int quit_on_errors, lock_rr_tran;
  LC_FIND_CLASSNAME allfind = LC_CLASSNAME_ERROR;
  LC_LOCKHINT *found_lockhint;
  LC_COPYAREA *copy_area;
  char *desc_ptr = NULL;
  int desc_size;
  char *content_ptr;
  int content_size;
  char *ptr;
  int num_objs = 0;
  char *packed = NULL;
  int packed_size;
  int send_size = 0;
  int i;
  int malloc_size;
  char *malloc_area;
  OR_ALIGNED_BUF (NET_SENDRECV_BUFFSIZE + NET_COPY_AREA_SENDRECV_SIZE + OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  found_lockhint = NULL;
  copy_area = NULL;

  ptr = or_unpack_int (request, &num_classes);
  ptr = or_unpack_int (ptr, &quit_on_errors);
  ptr = or_unpack_int (ptr, &lock_rr_tran);

  malloc_size = ((sizeof (char *) + sizeof (LOCK) + sizeof (int) + sizeof (int) + sizeof (OID) + sizeof (int))
		 * num_classes);

  malloc_area = (char *) db_private_alloc (thread_p, malloc_size);
  if (malloc_area != NULL)
    {
      many_classnames = (char **) malloc_area;
      many_locks = (LOCK *) ((char *) malloc_area + (sizeof (char *) * num_classes));
      many_need_subclasses = (int *) ((char *) many_locks + (sizeof (LOCK) * num_classes));
      many_flags = (LC_PREFETCH_FLAGS *) ((char *) many_need_subclasses + (sizeof (int) * num_classes));
      guessed_class_oids = (OID *) ((char *) many_flags + (sizeof (int) * num_classes));
      guessed_class_chns = (int *) ((char *) guessed_class_oids + (sizeof (OID) * num_classes));

      for (i = 0; i < num_classes; i++)
	{
	  ptr = or_unpack_string_nocopy (ptr, &many_classnames[i]);
	  ptr = or_unpack_lock (ptr, &many_locks[i]);
	  ptr = or_unpack_int (ptr, &many_need_subclasses[i]);
	  ptr = or_unpack_int (ptr, (int *) &many_flags[i]);
	  ptr = or_unpack_oid (ptr, &guessed_class_oids[i]);
	  ptr = or_unpack_int (ptr, &guessed_class_chns[i]);
	}

      allfind =
	xlocator_find_lockhint_class_oids (thread_p, num_classes, (const char **) many_classnames, many_locks,
					   many_need_subclasses, many_flags, guessed_class_oids, guessed_class_chns,
					   quit_on_errors, &found_lockhint, &copy_area);
    }
  if (allfind != LC_CLASSNAME_EXIST)
    {
      (void) return_error_to_client (thread_p, rid);
    }

  if ((LOCK) lock_rr_tran != NULL_LOCK)
    {
      /* lock the object common for RR transactions. This is used in ALTER TABLE ADD COLUMN NOT NULL scenarios */
      if (xtran_lock_rep_read (thread_p, (LOCK) lock_rr_tran) != NO_ERROR)
	{
	  allfind = LC_CLASSNAME_ERROR;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  (void) return_error_to_client (thread_p, rid);
	}
    }

  if (found_lockhint != NULL && found_lockhint->length > 0)
    {
      send_size = locator_pack_lockhint (found_lockhint, true);

      packed = found_lockhint->packed;
      packed_size = found_lockhint->packed_size;

      if (!packed)
	{
	  (void) return_error_to_client (thread_p, rid);
	  allfind = LC_CLASSNAME_ERROR;
	}
    }

  if (copy_area != NULL)
    {
      num_objs = locator_send_copy_area (copy_area, &content_ptr, &content_size, &desc_ptr, &desc_size);
    }
  else
    {
      desc_ptr = NULL;
      desc_size = 0;
      content_ptr = NULL;
      content_size = 0;
    }

  /* Send sizes of databuffer and copy area (descriptor + content) */

  ptr = or_pack_int (reply, send_size);
  ptr = or_pack_int (ptr, num_objs);
  ptr = or_pack_int (ptr, desc_size);
  ptr = or_pack_int (ptr, content_size);

  ptr = or_pack_int (ptr, allfind);

  if (copy_area == NULL && found_lockhint == NULL)
    {
      css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
    }
  else
    {
      css_send_reply_and_3_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply), packed,
					   send_size, desc_ptr, desc_size, content_ptr, content_size);
      if (copy_area != NULL)
	{
	  locator_free_copy_area (copy_area);
	}

      if (found_lockhint != NULL)
	{
	  locator_free_lockhint (found_lockhint);
	}

      if (desc_ptr)
	{
	  free_and_init (desc_ptr);
	}
    }

  if (malloc_area)
    {
      db_private_free_and_init (thread_p, malloc_area);
    }
}

/*
 * slocator_fetch_lockhint_classes -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
slocator_fetch_lockhint_classes (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  int success;
  LC_COPYAREA *copy_area;
  LC_LOCKHINT *lockhint;
  OR_ALIGNED_BUF (NET_SENDRECV_BUFFSIZE + NET_COPY_AREA_SENDRECV_SIZE + OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *desc_ptr;
  int desc_size;
  char *content_ptr;
  int content_size;
  char *ptr;
  bool first_call;
  int num_objs;
  char *packed = NULL;
  int packed_size;
  int send_size;

  ptr = or_unpack_int (request, &packed_size);

  if (packed_size == 0 || css_receive_data_from_client (thread_p->conn_entry, rid, &packed, (int *) &packed_size))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NET_SERVER_DATA_RECEIVE, 0);
      css_send_abort_to_client (thread_p->conn_entry, rid);
      if (packed)
	{
	  free_and_init (packed);
	}
      return;
    }

  lockhint = locator_allocate_and_unpack_lockhint (packed, packed_size, true, false);
  free_and_init (packed);

  if ((lockhint == NULL) || (lockhint->length <= 0))
    {
      (void) return_error_to_client (thread_p, rid);
      ptr = or_pack_int (reply, 0);
      ptr = or_pack_int (ptr, 0);
      ptr = or_pack_int (ptr, 0);
      ptr = or_pack_int (ptr, 0);
      ptr = or_pack_int (ptr, ER_FAILED);
      css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
      return;
    }

  first_call = true;
  do
    {
      desc_ptr = NULL;
      num_objs = 0;

      copy_area = NULL;
      success = xlocator_fetch_lockhint_classes (thread_p, lockhint, &copy_area);
      if (success != NO_ERROR)
	{
	  (void) return_error_to_client (thread_p, rid);
	}

      if (copy_area != NULL)
	{
	  num_objs = locator_send_copy_area (copy_area, &content_ptr, &content_size, &desc_ptr, &desc_size);
	}
      else
	{
	  desc_ptr = NULL;
	  desc_size = 0;
	  content_ptr = NULL;
	  content_size = 0;
	}

      /* Send sizes of databuffer and copy area (descriptor + content) */

      send_size = locator_pack_lockhint (lockhint, first_call);

      packed = lockhint->packed;
      packed_size = lockhint->packed_size;

      ptr = or_pack_int (reply, send_size);
      ptr = or_pack_int (ptr, num_objs);
      ptr = or_pack_int (ptr, desc_size);
      ptr = or_pack_int (ptr, content_size);

      ptr = or_pack_int (ptr, success);

      if (copy_area == NULL && lockhint == NULL)
	{
	  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
	}
      else
	{
	  css_send_reply_and_3_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply), packed,
					       send_size, desc_ptr, desc_size, content_ptr, content_size);
	}
      if (copy_area != NULL)
	{
	  locator_free_copy_area (copy_area);
	}
      if (desc_ptr)
	{
	  free_and_init (desc_ptr);
	}

      first_call = false;
    }
  while (copy_area && lockhint && ((lockhint->num_classes > lockhint->num_classes_processed)));

  if (lockhint)
    {
      locator_free_lockhint (lockhint);
    }
}

/*
 * sthread_kill_tran_index -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
sthread_kill_tran_index (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  int success;
  int kill_tran_index;
  int kill_pid;
  char *kill_user;
  char *kill_host;
  char *ptr;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  ptr = or_unpack_int (request, &kill_tran_index);
  ptr = or_unpack_string_nocopy (ptr, &kill_user);
  ptr = or_unpack_string_nocopy (ptr, &kill_host);
  ptr = or_unpack_int (ptr, &kill_pid);

  success = (xlogtb_kill_tran_index (thread_p, kill_tran_index, kill_user, kill_host, kill_pid)
	     == NO_ERROR) ? NO_ERROR : ER_FAILED;
  if (success != NO_ERROR)
    {
      (void) return_error_to_client (thread_p, rid);
    }

  ptr = or_pack_int (reply, success);
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * sthread_kill_or_interrupt_tran -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
sthread_kill_or_interrupt_tran (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  int success = NO_ERROR;
  char *ptr;
  OR_ALIGNED_BUF (OR_INT_SIZE + OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  int i = 0;
  int *tran_index_list;
  int num_tran_index, interrupt_only;
  int num_killed_tran = 0;
  int is_dba_group_member = 0;

  ptr = or_unpack_int (request, &is_dba_group_member);
  ptr = or_unpack_int (ptr, &num_tran_index);
  ptr = or_unpack_int_array (ptr, num_tran_index, &tran_index_list);
  ptr = or_unpack_int (ptr, &interrupt_only);

  for (i = 0; i < num_tran_index; i++)
    {
      success =
	xlogtb_kill_or_interrupt_tran (thread_p, tran_index_list[i], (bool) is_dba_group_member, (bool) interrupt_only);
      if (success == NO_ERROR)
	{
	  num_killed_tran++;
	}
      else if (success == ER_KILL_TR_NOT_ALLOWED)
	{
	  (void) return_error_to_client (thread_p, rid);
	  break;
	}
      else
	{
	  /* error not related with authorization is ignored and keep running */
	  success = NO_ERROR;
	}
    }

  ptr = or_pack_int (reply, success);
  ptr = or_pack_int (ptr, num_killed_tran);
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));

  if (tran_index_list)
    {
      db_private_free (NULL, tran_index_list);
    }
}

/*
 * sthread_dump_cs_stat -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
sthread_dump_cs_stat (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  FILE *outfp;
  int file_size;
  char *buffer;
  int buffer_size;
  int send_size;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  (void) or_unpack_int (request, &buffer_size);

  buffer = (char *) db_private_alloc (NULL, buffer_size);
  if (buffer == NULL)
    {
      css_send_abort_to_client (thread_p->conn_entry, rid);
      return;
    }

  outfp = tmpfile ();
  if (outfp == NULL)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      css_send_abort_to_client (thread_p->conn_entry, rid);
      db_private_free_and_init (NULL, buffer);
      return;
    }

  sync_dump_statistics (outfp, SYNC_TYPE_ALL);

  file_size = ftell (outfp);

  /*
   * Send the file in pieces
   */
  rewind (outfp);

  (void) or_pack_int (reply, (int) file_size);
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));

  while (file_size > 0)
    {
      if (file_size > buffer_size)
	{
	  send_size = buffer_size;
	}
      else
	{
	  send_size = file_size;
	}

      file_size -= send_size;
      if (fread (buffer, 1, send_size, outfp) == 0)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  css_send_abort_to_client (thread_p->conn_entry, rid);
	  /*
	   * Continue sending the stuff that was prmoised to client. In this case
	   * junk (i.e., whatever it is in the buffers) is sent.
	   */
	}
      css_send_data_to_client (thread_p->conn_entry, rid, buffer, send_size);
    }
  fclose (outfp);
  db_private_free_and_init (NULL, buffer);
}

/*
 * slogtb_get_pack_tran_table -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 */
void
slogtb_get_pack_tran_table (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  char *buffer, *ptr;
  int size;
  OR_ALIGNED_BUF (OR_INT_SIZE + OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  int error;
  int include_query_exec_info;

  (void) or_unpack_int (request, &include_query_exec_info);

  error = xlogtb_get_pack_tran_table (thread_p, &buffer, &size, include_query_exec_info);

  if (error != NO_ERROR)
    {
      ptr = or_pack_int (reply, 0);
      ptr = or_pack_int (ptr, error);
      css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
    }
  else
    {
      ptr = or_pack_int (reply, size);
      ptr = or_pack_int (ptr, error);
      css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
      css_send_data_to_client (thread_p->conn_entry, rid, buffer, size);
      free_and_init (buffer);
    }
}

/*
 * slogtb_dump_trantable -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 */
void
slogtb_dump_trantable (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  FILE *outfp;
  int file_size;
  char *buffer;
  int buffer_size;
  int send_size;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  (void) or_unpack_int (request, &buffer_size);

  buffer = (char *) db_private_alloc (thread_p, buffer_size);
  if (buffer == NULL)
    {
      css_send_abort_to_client (thread_p->conn_entry, rid);
      return;
    }

  outfp = tmpfile ();
  if (outfp == NULL)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      css_send_abort_to_client (thread_p->conn_entry, rid);
      db_private_free_and_init (thread_p, buffer);
      return;
    }

  xlogtb_dump_trantable (thread_p, outfp);
  file_size = ftell (outfp);

  /*
   * Send the file in pieces
   */
  rewind (outfp);

  (void) or_pack_int (reply, (int) file_size);
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));

  while (file_size > 0)
    {
      if (file_size > buffer_size)
	{
	  send_size = buffer_size;
	}
      else
	{
	  send_size = file_size;
	}

      file_size -= send_size;
      if (fread (buffer, 1, send_size, outfp) == 0)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  css_send_abort_to_client (thread_p->conn_entry, rid);
	  /*
	   * Continue sending the stuff that was prmoised to client. In this case
	   * junk (i.e., whatever it is in the buffers) is sent.
	   */
	}
      css_send_data_to_client (thread_p->conn_entry, rid, buffer, send_size);
    }
  fclose (outfp);
  db_private_free_and_init (thread_p, buffer);
}

/*
 * xcallback_console_print -
 *
 * return:
 *
 *   print_str(in):
 */
int
xcallback_console_print (THREAD_ENTRY * thread_p, char *print_str)
{
  OR_ALIGNED_BUF (OR_INT_SIZE * 3) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  unsigned int rid, rc;
  int data_len, print_len;
  char *ptr;
  char *databuf;

  rid = css_get_comm_request_id (thread_p);
  data_len = or_packed_string_length (print_str, &print_len);

  ptr = or_pack_int (reply, (int) CONSOLE_OUTPUT);
  ptr = or_pack_int (ptr, NO_ERROR);
  ptr = or_pack_int (ptr, data_len);

  databuf = (char *) db_private_alloc (thread_p, data_len);
  if (databuf == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) data_len);
      return ER_FAILED;
    }
  ptr = or_pack_string_with_length (databuf, print_str, print_len);
  rc =
    css_send_reply_and_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply), databuf,
				       data_len);
  db_private_free_and_init (thread_p, databuf);

  if (rc)
    {
      return ER_FAILED;
    }

  return NO_ERROR;
}

/*
 * xio_send_user_prompt_to_client -
 *
 * return:
 *
 *   prompt_id(in):
 *   prompt(in):
 *   failure_prompt(in):
 *   range_low(in):
 *   range_high(in):
 *   secondary_prompt(in):
 *   reprompt_value(in):
 *
 * NOTE:
 * can be called only in the context of a net_client_callback
 * that is waiting for the size of 3 integers to be returned.
 * presently, on the client side that is bo_backup.
 */
int
xio_send_user_prompt_to_client (THREAD_ENTRY * thread_p, FILEIO_REMOTE_PROMPT_TYPE prompt_id, const char *prompt,
				const char *failure_prompt, int range_low, int range_high, const char *secondary_prompt,
				int reprompt_value)
{
  OR_ALIGNED_BUF (OR_INT_SIZE * 3) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  int prompt_length, strlen1, strlen2, strlen3;
  unsigned int rid, rc;
  char *ptr;
  char *databuf;

  rid = css_get_comm_request_id (thread_p);
  /* need to know length of prompt string we are sending */
  prompt_length = (or_packed_string_length (prompt, &strlen1) + or_packed_string_length (failure_prompt, &strlen2)
		   + OR_INT_SIZE * 2 + or_packed_string_length (secondary_prompt, &strlen3) + OR_INT_SIZE);

  /*
   * Client side caller must be expecting a reply/callback followed
   * by 2 ints, otherwise client will abort due to protocol error
   * Prompt_length tells the receiver how big the followon message is.
   */
  ptr = or_pack_int (reply, (int) ASYNC_OBTAIN_USER_INPUT);
  ptr = or_pack_int (ptr, (int) prompt_id);
  ptr = or_pack_int (ptr, prompt_length);

  databuf = (char *) db_private_alloc (thread_p, prompt_length);
  if (databuf == NULL)
    {
      return ER_FAILED;
    }

  ptr = or_pack_string_with_length (databuf, prompt, strlen1);
  ptr = or_pack_string_with_length (ptr, failure_prompt, strlen2);
  ptr = or_pack_int (ptr, range_low);
  ptr = or_pack_int (ptr, range_high);
  ptr = or_pack_string_with_length (ptr, secondary_prompt, strlen3);
  ptr = or_pack_int (ptr, reprompt_value);

  rc =
    css_send_reply_and_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply), databuf,
				       prompt_length);
  db_private_free_and_init (thread_p, databuf);

  if (rc)
    {
      return ER_FAILED;
    }

  return NO_ERROR;
}

/*
 * xlog_send_log_pages_to_client -
 *
 * return:
 * NOTE:
 */
int
xlog_send_log_pages_to_client (THREAD_ENTRY * thread_p, char *logpg_area, int area_size, LOGWR_MODE mode)
{
  OR_ALIGNED_BUF (OR_INT_SIZE * 2) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  unsigned int rid, rc;
  char *ptr;

  rid = css_get_comm_request_id (thread_p);

  /*
   * Client side caller must be expecting a reply/callback followed
   * by 2 ints, otherwise client will abort due to protocol error
   * Prompt_length tells the receiver how big the followon message is.
   */
  ptr = or_pack_int (reply, (int) GET_NEXT_LOG_PAGES);
  ptr = or_pack_int (ptr, (int) area_size);

  rc =
    css_send_reply_and_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply), logpg_area,
				       area_size);
  if (rc)
    {
      return ER_FAILED;
    }

  er_log_debug (ARG_FILE_LINE, "xlog_send_log_pages_to_client\n");

  return NO_ERROR;
}

/*
 * xlog_get_page_request_with_reply
 *
 * return:
 * NOTE:
 */
int
xlog_get_page_request_with_reply (THREAD_ENTRY * thread_p, LOG_PAGEID * fpageid_ptr, LOGWR_MODE * mode_ptr, int timeout)
{
  char *reply = NULL;
  int reply_size;
  LOG_PAGEID first_pageid;
  int mode;
  char *ptr;
  int error;
  int remote_error;

  /* Obtain success message from the client, without blocking the server. */
  error = xs_receive_data_from_client_with_timeout (thread_p, &reply, &reply_size, timeout);
  if (error != NO_ERROR)
    {
      if (reply)
	{
	  free_and_init (reply);
	}

      return error;
    }

  assert (reply != NULL);
  ptr = or_unpack_int64 (reply, &first_pageid);
  ptr = or_unpack_int (ptr, &mode);
  ptr = or_unpack_int (ptr, &remote_error);
  free_and_init (reply);

  *fpageid_ptr = first_pageid;
  *mode_ptr = (LOGWR_MODE) mode;

  er_log_debug (ARG_FILE_LINE, "xlog_get_page_request_with_reply, " "fpageid(%lld), mode(%s)\n", first_pageid,
		mode == LOGWR_MODE_SYNC ? "sync" : (mode == LOGWR_MODE_ASYNC ? "async" : "semisync"));
  return (remote_error != NO_ERROR) ? remote_error : error;
}

/*
 * shf_get_class_num_objs_and_pages -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
shf_get_class_num_objs_and_pages (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  HFID hfid;
  int success, approximation, nobjs, npages;
  OR_ALIGNED_BUF (OR_INT_SIZE * 3) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *ptr;

  ptr = or_unpack_hfid (request, &hfid);
  ptr = or_unpack_int (ptr, &approximation);

  success = ((xheap_get_class_num_objects_pages (thread_p, &hfid, approximation, &nobjs, &npages) == NO_ERROR)
	     ? NO_ERROR : ER_FAILED);

  if (success != NO_ERROR)
    {
      (void) return_error_to_client (thread_p, rid);
    }

  ptr = or_pack_int (reply, (int) success);
  ptr = or_pack_int (ptr, (int) nobjs);
  ptr = or_pack_int (ptr, (int) npages);

  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * sbtree_get_statistics -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
sbtree_get_statistics (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  BTREE_STATS stat_info;
  int success;
  OR_ALIGNED_BUF (OR_INT_SIZE * 5) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *ptr;

  ptr = or_unpack_btid (request, &stat_info.btid);
  assert_release (!BTID_IS_NULL (&stat_info.btid));

  stat_info.keys = 0;
  stat_info.pkeys_size = 0;	/* do not request pkeys info */
  stat_info.pkeys = NULL;

  success = btree_get_stats (thread_p, &stat_info, STATS_WITH_SAMPLING);
  if (success != NO_ERROR)
    {
      (void) return_error_to_client (thread_p, rid);
    }

  ptr = or_pack_int (reply, success);
  ptr = or_pack_int (ptr, stat_info.leafs);
  ptr = or_pack_int (ptr, stat_info.pages);
  ptr = or_pack_int (ptr, stat_info.height);
  ptr = or_pack_int (ptr, stat_info.keys);

  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * sbtree_get_key_type () - Obtains key type from index b-tree.
 *
 * return :
 * thread_p (in) :
 * rid (in) :
 * request (in) :
 * int reqlen (in) :
 */
void
sbtree_get_key_type (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  BTID btid;
  int error;
  OR_ALIGNED_BUF (OR_INT_SIZE * 2) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *reply_data = NULL;
  char *ptr;
  int reply_data_size;
  TP_DOMAIN *key_type = NULL;

  /* Unpack BTID */
  ptr = or_unpack_btid (request, &btid);
  assert_release (!BTID_IS_NULL (&btid));

  /* Get key type */
  error = xbtree_get_key_type (thread_p, btid, &key_type);
  if (error != NO_ERROR && er_errid () != NO_ERROR)
    {
      (void) return_error_to_client (thread_p, rid);
    }

  if (key_type != NULL)
    {
      /* Send key type to client */
      reply_data_size = or_packed_domain_size (key_type, 0);
      reply_data = (char *) malloc (reply_data_size);
      if (reply_data == NULL)
	{
	  error = ER_OUT_OF_VIRTUAL_MEMORY;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, (size_t) reply_data_size);
	  reply_data_size = 0;
	}
      else
	{
	  (void) or_pack_domain (reply_data, key_type, 0, 0);
	}
    }
  else
    {
      reply_data_size = 0;
      reply_data = NULL;
      error = (error == NO_ERROR) ? ER_FAILED : error;
    }

  ptr = or_pack_int (reply, reply_data_size);
  ptr = or_pack_int (ptr, error);

  css_send_reply_and_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply), reply_data,
				     reply_data_size);

  if (reply_data != NULL)
    {
      free_and_init (reply_data);
    }
}

/*
 * sqp_get_server_info -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
sqp_get_server_info (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  int success = NO_ERROR;
  OR_ALIGNED_BUF (OR_INT_SIZE + OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *ptr, *buffer = NULL;
  int buffer_length;
  int server_info_bits;

  DB_VALUE dt_dbval, ts_dbval;	/* SI_SYS_DATETIME */
  DB_VALUE lt_dbval;		/* SI_LOCAL_TRANSACTION_ID */

  ptr = or_unpack_int (request, &server_info_bits);

  buffer_length = 0;

  if (server_info_bits & SI_SYS_DATETIME)
    {
      success = db_sys_date_and_epoch_time (&dt_dbval, &ts_dbval);
      if (success != NO_ERROR)
	{
	  goto error_exit;
	}
      buffer_length += OR_VALUE_ALIGNED_SIZE (&dt_dbval);
      buffer_length += OR_VALUE_ALIGNED_SIZE (&ts_dbval);
    }

  if (server_info_bits & SI_LOCAL_TRANSACTION_ID)
    {
      success = xtran_get_local_transaction_id (thread_p, &lt_dbval);
      if (success != NO_ERROR)
	{
	  goto error_exit;
	}
      buffer_length += OR_VALUE_ALIGNED_SIZE (&lt_dbval);
    }

  buffer = (char *) db_private_alloc (thread_p, buffer_length);
  if (buffer == NULL)
    {
      success = ER_OUT_OF_VIRTUAL_MEMORY;
      goto error_exit;
    }

  ptr = buffer;

  if (server_info_bits & SI_SYS_DATETIME)
    {
      ptr = or_pack_value (ptr, &dt_dbval);
      ptr = or_pack_value (ptr, &ts_dbval);
    }

  if (server_info_bits & SI_LOCAL_TRANSACTION_ID)
    {
      ptr = or_pack_value (ptr, &lt_dbval);
    }

#if !defined(NDEBUG)
  /* suppress valgrind UMW error */
  memset (ptr, 0, buffer_length - (ptr - buffer));
#endif

exit:
  ptr = or_pack_int (reply, buffer_length);
  ptr = or_pack_int (ptr, success);

  css_send_reply_and_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply), buffer,
				     buffer_length);
  if (buffer != NULL)
    {
      db_private_free_and_init (thread_p, buffer);
    }

  return;

error_exit:
  buffer_length = 0;
  (void) return_error_to_client (thread_p, rid);

  goto exit;
}

/*
 * sprm_server_change_parameters () - Changes server's system parameter
 *				      values.
 *
 * return	 :
 * thread_p (in) :
 * rid (in)      :
 * request (in)  :
 * reqlen (in)   :
 */
void
sprm_server_change_parameters (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  SYSPRM_ASSIGN_VALUE *assignments = NULL;

  (void) sysprm_unpack_assign_values (request, &assignments);

  xsysprm_change_server_parameters (assignments);

  (void) or_pack_int (reply, PRM_ERR_NO_ERROR);
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));

  sysprm_free_assign_values (&assignments);
}

/*
 * sprm_server_get_force_parameters () - Obtains values for server's system
 *					 parameters that are marked with
 *					 PRM_FORCE_SERVER flag.
 *
 * return	 :
 * thread_p (in) :
 * rid (in)	 :
 * request (in)	 :
 * reqlen (in)	 :
 */
void
sprm_server_get_force_parameters (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  SYSPRM_ASSIGN_VALUE *change_values;
  OR_ALIGNED_BUF (OR_INT_SIZE * 2) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  int area_size;
  char *area = NULL, *ptr = NULL;

  change_values = xsysprm_get_force_server_parameters ();
  if (change_values == NULL)
    {
      (void) return_error_to_client (thread_p, rid);
    }

  area_size = sysprm_packed_assign_values_length (change_values, 0);
  area = (char *) malloc (area_size);
  if (area == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) area_size);
      (void) return_error_to_client (thread_p, rid);
      area_size = 0;
    }
  ptr = or_pack_int (reply, area_size);
  ptr = or_pack_int (ptr, er_errid ());

  (void) sysprm_pack_assign_values (area, change_values);
  css_send_reply_and_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply), area, area_size);

  if (area != NULL)
    {
      free_and_init (area);
    }
  sysprm_free_assign_values (&change_values);
}

/*
 * sprm_server_obtain_parameters () - Obtains server's system parameter values
 *				      for the requested parameters.
 *
 * return	 :
 * thread_p (in) :
 * rid (in)	 :
 * request (in)	 :
 * reqlen (in)	 :
 */
void
sprm_server_obtain_parameters (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  SYSPRM_ERR rc = PRM_ERR_NO_ERROR;
  OR_ALIGNED_BUF (OR_INT_SIZE * 2) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *ptr = NULL, *reply_data = NULL;
  int reply_data_size;
  SYSPRM_ASSIGN_VALUE *prm_values = NULL;

  (void) sysprm_unpack_assign_values (request, &prm_values);
  xsysprm_obtain_server_parameters (prm_values);
  reply_data_size = sysprm_packed_assign_values_length (prm_values, 0);
  reply_data = (char *) malloc (reply_data_size);
  if (reply_data == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) reply_data_size);
      rc = PRM_ERR_NO_MEM_FOR_PRM;
      reply_data_size = 0;
    }
  else
    {
      (void) sysprm_pack_assign_values (reply_data, prm_values);
    }
  ptr = or_pack_int (reply, reply_data_size);
  ptr = or_pack_int (ptr, rc);
  css_send_reply_and_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply), reply_data,
				     reply_data_size);
  if (reply_data != NULL)
    {
      free_and_init (reply_data);
    }
  if (prm_values != NULL)
    {
      sysprm_free_assign_values (&prm_values);
    }
}

/*
 * sprm_server_dump_parameters -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 */
void
sprm_server_dump_parameters (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  FILE *outfp;
  int file_size;
  char *buffer;
  int buffer_size;
  int send_size;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  (void) or_unpack_int (request, &buffer_size);

  buffer = (char *) db_private_alloc (thread_p, buffer_size);
  if (buffer == NULL)
    {
      css_send_abort_to_client (thread_p->conn_entry, rid);
      return;
    }

  outfp = tmpfile ();
  if (outfp == NULL)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      css_send_abort_to_client (thread_p->conn_entry, rid);
      db_private_free_and_init (thread_p, buffer);
      return;
    }

  xsysprm_dump_server_parameters (outfp);
  file_size = ftell (outfp);

  /*
   * Send the file in pieces
   */
  rewind (outfp);

  (void) or_pack_int (reply, (int) file_size);
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));

  while (file_size > 0)
    {
      if (file_size > buffer_size)
	{
	  send_size = buffer_size;
	}
      else
	{
	  send_size = file_size;
	}

      file_size -= send_size;
      if (fread (buffer, 1, send_size, outfp) == 0)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  css_send_abort_to_client (thread_p->conn_entry, rid);
	  /*
	   * Continue sending the stuff that was prmoised to client. In this case
	   * junk (i.e., whatever it is in the buffers) is sent.
	   */
	}
      css_send_data_to_client (thread_p->conn_entry, rid, buffer, send_size);
    }

  fclose (outfp);
  db_private_free_and_init (thread_p, buffer);
}

/*
 * shf_has_instance -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
shf_has_instance (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  HFID hfid;
  OID class_oid;
  int r;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *ptr;
  int has_visible_instance;

  ptr = or_unpack_hfid (request, &hfid);
  ptr = or_unpack_oid (ptr, &class_oid);
  ptr = or_unpack_int (ptr, &has_visible_instance);

  r = xheap_has_instance (thread_p, &hfid, &class_oid, has_visible_instance);

  if (r == -1)
    {
      (void) return_error_to_client (thread_p, rid);
    }

  ptr = or_pack_int (reply, (int) r);

  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * stran_get_local_transaction_id -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
stran_get_local_transaction_id (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  OR_ALIGNED_BUF (OR_INT_SIZE + OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *ptr;
  DB_VALUE val;
  int success, trid;

  success = (xtran_get_local_transaction_id (thread_p, &val) == NO_ERROR) ? NO_ERROR : ER_FAILED;
  trid = db_get_int (&val);
  ptr = or_pack_int (reply, success);
  ptr = or_pack_int (ptr, trid);
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * sjsp_get_server_port -
 *
 * return:
 *
 *   rid(in):
 *
 * NOTE:
 */
void
sjsp_get_server_port (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  (void) or_pack_int (reply, jsp_server_port_from_info ());
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * srepl_set_info -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
srepl_set_info (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  int success = NO_ERROR;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *ptr;
  REPL_INFO repl_info = { NULL, 0, false };
  REPL_INFO_SBR repl_schema = { 0, NULL, NULL, NULL, NULL };

  if (!LOG_CHECK_LOG_APPLIER (thread_p) && log_does_allow_replication () == true)
    {
      ptr = or_unpack_int (request, &repl_info.repl_info_type);
      switch (repl_info.repl_info_type)
	{
	case REPL_INFO_TYPE_SBR:
	  {
	    ptr = or_unpack_int (ptr, &repl_schema.statement_type);
	    ptr = or_unpack_string_nocopy (ptr, &repl_schema.name);
	    ptr = or_unpack_string_nocopy (ptr, &repl_schema.stmt_text);
	    ptr = or_unpack_string_nocopy (ptr, &repl_schema.db_user);
	    ptr = or_unpack_string_nocopy (ptr, &repl_schema.sys_prm_context);

	    repl_info.info = (char *) &repl_schema;
	    break;
	  }
	default:
	  success = ER_FAILED;
	  break;
	}

      if (success == NO_ERROR)
	{
	  success = xrepl_set_info (thread_p, &repl_info);
	}
    }

  (void) or_pack_int (reply, success);
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * srepl_log_get_append_lsa -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
srepl_log_get_append_lsa (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  OR_ALIGNED_BUF (OR_LOG_LSA_ALIGNED_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  LOG_LSA *lsa;

  lsa = xrepl_log_get_append_lsa ();

  reply = OR_ALIGNED_BUF_START (a_reply);
  (void) or_pack_log_lsa (reply, lsa);

  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * slocator_check_fk_validity -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
slocator_check_fk_validity (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  OID class_oid;
  HFID hfid;
  OID pk_cls_oid;
  BTID pk_btid;
  int n_attrs, *attr_ids = NULL;
  TP_DOMAIN *key_type;
  char *fk_name = NULL;
  char *ptr;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  ptr = or_unpack_oid (request, &class_oid);
  ptr = or_unpack_hfid (ptr, &hfid);
  ptr = or_unpack_domain (ptr, &key_type, 0);
  ptr = or_unpack_int (ptr, &n_attrs);
  ptr = or_unpack_int_array (ptr, n_attrs, &attr_ids);
  if (ptr == NULL)
    {
      (void) return_error_to_client (thread_p, rid);
      goto end;
    }

  ptr = or_unpack_oid (ptr, &pk_cls_oid);
  ptr = or_unpack_btid (ptr, &pk_btid);
  ptr = or_unpack_string (ptr, &fk_name);

  if (xlocator_check_fk_validity (thread_p, &class_oid, &hfid, key_type, n_attrs, attr_ids, &pk_cls_oid, &pk_btid,
				  fk_name) != NO_ERROR)
    {
      (void) return_error_to_client (thread_p, rid);
    }

end:

  ptr = or_pack_int (reply, er_errid ());
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));

  if (attr_ids != NULL)
    {
      db_private_free_and_init (thread_p, attr_ids);
    }

  if (fk_name != NULL)
    {
      db_private_free_and_init (thread_p, fk_name);
    }
}

/*
 * slogwr_get_log_pages -
 *
 * return:
 *
 *   thread_p(in):
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * Note:
 */
void
slogwr_get_log_pages (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  OR_ALIGNED_BUF (OR_INT_SIZE * 2) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *ptr;
  LOG_PAGEID first_pageid;
  LOGWR_MODE mode;
  int m, error, remote_error;

  ptr = or_unpack_int64 (request, &first_pageid);
  ptr = or_unpack_int (ptr, &m);
  mode = (LOGWR_MODE) m;
  ptr = or_unpack_int (ptr, &remote_error);

  error = xlogwr_get_log_pages (thread_p, first_pageid, mode);
  if (error == ER_INTERRUPTED)
    {
      (void) return_error_to_client (thread_p, rid);
    }

  if (error == ER_NET_DATA_RECEIVE_TIMEDOUT)
    {
      css_end_server_request (thread_p->conn_entry);
    }
  else
    {
      ptr = or_pack_int (reply, (int) END_CALLBACK);
      ptr = or_pack_int (ptr, error);
      (void) css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
    }

  return;
}



/*
 * sboot_compact_db -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
sboot_compact_db (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  int success, n_classes, reply_size, i;
  char *reply = NULL;
  OID *class_oids = NULL;
  int *ids_repr = NULL;
  char *ptr = NULL;
  int space_to_process = 0, instance_lock_timeout = 0, delete_old_repr = 0;
  int class_lock_timeout = 0;
  OID last_processed_class_oid, last_processed_oid;
  int *total_objects = NULL, *failed_objects = NULL;
  int *modified_objects = NULL, *big_objects = NULL;

  ptr = or_unpack_int (request, &n_classes);
  if (ptr == NULL)
    {
      (void) return_error_to_client (thread_p, rid);
      return;
    }

  ptr = or_unpack_oid_array (ptr, n_classes, &class_oids);
  if (ptr == NULL)
    {
      (void) return_error_to_client (thread_p, rid);
      return;
    }

  ptr = or_unpack_int (ptr, &space_to_process);
  if (ptr == NULL)
    {
      (void) return_error_to_client (thread_p, rid);
      return;
    }

  ptr = or_unpack_int (ptr, &instance_lock_timeout);
  if (ptr == NULL)
    {
      (void) return_error_to_client (thread_p, rid);
      return;
    }

  ptr = or_unpack_int (ptr, &class_lock_timeout);
  if (ptr == NULL)
    {
      (void) return_error_to_client (thread_p, rid);
      return;
    }

  ptr = or_unpack_int (ptr, &delete_old_repr);
  if (ptr == NULL)
    {
      (void) return_error_to_client (thread_p, rid);
      return;
    }

  ptr = or_unpack_oid (ptr, &last_processed_class_oid);
  if (ptr == NULL)
    {
      (void) return_error_to_client (thread_p, rid);
      return;
    }

  ptr = or_unpack_oid (ptr, &last_processed_oid);
  if (ptr == NULL)
    {
      (void) return_error_to_client (thread_p, rid);
      return;
    }

  ptr = or_unpack_int_array (ptr, n_classes, &total_objects);
  if (ptr == NULL)
    {
      (void) return_error_to_client (thread_p, rid);
      return;
    }

  ptr = or_unpack_int_array (ptr, n_classes, &failed_objects);
  if (ptr == NULL)
    {
      (void) return_error_to_client (thread_p, rid);
      return;
    }

  ptr = or_unpack_int_array (ptr, n_classes, &modified_objects);
  if (ptr == NULL)
    {
      (void) return_error_to_client (thread_p, rid);
      return;
    }

  ptr = or_unpack_int_array (ptr, n_classes, &big_objects);
  if (ptr == NULL)
    {
      (void) return_error_to_client (thread_p, rid);
      return;
    }

  ptr = or_unpack_int_array (ptr, n_classes, &ids_repr);
  if (ptr == NULL)
    {
      (void) return_error_to_client (thread_p, rid);
      return;
    }

  success =
    xboot_compact_db (thread_p, class_oids, n_classes, space_to_process, instance_lock_timeout, class_lock_timeout,
		      (bool) delete_old_repr, &last_processed_class_oid, &last_processed_oid, total_objects,
		      failed_objects, modified_objects, big_objects, ids_repr);

  if (success != NO_ERROR)
    {
      (void) return_error_to_client (thread_p, rid);
    }

  reply_size = OR_OID_SIZE * 2 + OR_INT_SIZE * (5 * n_classes + 1);
  reply = (char *) db_private_alloc (thread_p, reply_size);
  if (reply == NULL)
    {
      css_send_abort_to_client (thread_p->conn_entry, rid);
      db_private_free_and_init (thread_p, class_oids);
      db_private_free_and_init (thread_p, ids_repr);
      db_private_free_and_init (thread_p, failed_objects);
      db_private_free_and_init (thread_p, modified_objects);
      db_private_free_and_init (thread_p, big_objects);
      db_private_free_and_init (thread_p, total_objects);
      return;
    }

  ptr = or_pack_int (reply, success);
  ptr = or_pack_oid (ptr, &last_processed_class_oid);
  ptr = or_pack_oid (ptr, &last_processed_oid);

  for (i = 0; i < n_classes; i++)
    {
      ptr = or_pack_int (ptr, total_objects[i]);
    }

  for (i = 0; i < n_classes; i++)
    {
      ptr = or_pack_int (ptr, failed_objects[i]);
    }

  for (i = 0; i < n_classes; i++)
    {
      ptr = or_pack_int (ptr, modified_objects[i]);
    }

  for (i = 0; i < n_classes; i++)
    {
      ptr = or_pack_int (ptr, big_objects[i]);
    }

  for (i = 0; i < n_classes; i++)
    {
      ptr = or_pack_int (ptr, ids_repr[i]);
    }

  if (css_send_data_to_client (thread_p->conn_entry, rid, reply, reply_size) != NO_ERROR)
    {
      boot_compact_stop (thread_p);
    }

  db_private_free_and_init (thread_p, class_oids);
  db_private_free_and_init (thread_p, ids_repr);
  db_private_free_and_init (thread_p, failed_objects);
  db_private_free_and_init (thread_p, modified_objects);
  db_private_free_and_init (thread_p, big_objects);
  db_private_free_and_init (thread_p, total_objects);

  db_private_free_and_init (thread_p, reply);
}

/*
 * sboot_heap_compact -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
sboot_heap_compact (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *ptr;
  int success;
  OID class_oid;

  ptr = or_unpack_oid (request, &class_oid);
  if (ptr == NULL)
    {
      (void) return_error_to_client (thread_p, rid);
      return;
    }

  success = xboot_heap_compact (thread_p, &class_oid);
  if (success != NO_ERROR)
    {
      (void) return_error_to_client (thread_p, rid);
    }

  or_pack_int (reply, success);

  if (css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply)) != NO_ERROR)
    {
      boot_compact_stop (thread_p);
    }
}

/*
 * sboot_compact_start -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
sboot_compact_start (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  int success;

  success = xboot_compact_start (thread_p);
  if (success != NO_ERROR)
    {
      (void) return_error_to_client (thread_p, rid);
    }

  or_pack_int (reply, success);

  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * sboot_compact_stop -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
sboot_compact_stop (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  int success;

  success = xboot_compact_stop (thread_p);
  if (success != NO_ERROR)
    {
      (void) return_error_to_client (thread_p, rid);
    }

  or_pack_int (reply, success);

  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * ses_posix_create_file -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
ses_posix_create_file (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  char new_path[PATH_MAX];
  int path_size = 0, ret;
  OR_ALIGNED_BUF (OR_INT_SIZE * 2) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *ptr;

  ret = xes_posix_create_file (new_path);
  if (ret != NO_ERROR)
    {
      (void) return_error_to_client (thread_p, rid);
    }
  else
    {
      path_size = strlen (new_path) + 1;
    }

  ptr = or_pack_int (reply, path_size);
  ptr = or_pack_int (ptr, ret);
  css_send_reply_and_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply), new_path,
				     path_size);
}

/*
 * ses_posix_write_file -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
ses_posix_write_file (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  char *path;
  void *buf = NULL;
  off_t offset;
  size_t count;
  INT64 ret, tmp_int64;
  int csserror, buf_size;
  OR_ALIGNED_BUF (OR_INT64_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *ptr;

  ptr = or_unpack_string_nocopy (request, &path);
  ptr = or_unpack_int64 (ptr, &tmp_int64);
  offset = (off_t) tmp_int64;
  ptr = or_unpack_int64 (ptr, &tmp_int64);
  count = (size_t) tmp_int64;

  csserror = css_receive_data_from_client (thread_p->conn_entry, rid, (char **) &buf, &buf_size);
  if (csserror)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NET_SERVER_DATA_RECEIVE, 0);
      css_send_abort_to_client (thread_p->conn_entry, rid);
    }
  else
    {
      ret = xes_posix_write_file (path, buf, count, offset);
      if (ret != NO_ERROR)
	{
	  (void) return_error_to_client (thread_p, rid);
	}

      ptr = or_pack_int64 (reply, (INT64) ret);
      css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
    }
  if (buf != NULL)
    {
      free_and_init (buf);
    }
}

/*
 * ses_posix_read_file -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
ses_posix_read_file (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  char *path;
  void *buf;
  off_t offset;
  size_t count;
  INT64 ret, tmp_int64;
  OR_ALIGNED_BUF (OR_INT64_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *ptr;

  ptr = or_unpack_string_nocopy (request, &path);
  ptr = or_unpack_int64 (ptr, &tmp_int64);
  offset = (off_t) tmp_int64;
  ptr = or_unpack_int64 (ptr, &tmp_int64);
  count = (size_t) tmp_int64;

  buf = db_private_alloc (thread_p, count);
  if (buf == NULL)
    {
      css_send_abort_to_client (thread_p->conn_entry, rid);
    }
  else
    {
      ret = xes_posix_read_file (path, buf, count, offset);
      if (ret != NO_ERROR)
	{
	  (void) return_error_to_client (thread_p, rid);
	}

      ptr = or_pack_int64 (reply, (INT64) ret);
      css_send_reply_and_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply), (char *) buf,
					 (int) count);
      db_private_free_and_init (thread_p, buf);
    }
}

/*
 * ses_posix_delete_file -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
ses_posix_delete_file (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  char *path;
  int ret;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *ptr;

  ptr = or_unpack_string_nocopy (request, &path);

  ret = xes_posix_delete_file (path);
  if (ret != NO_ERROR)
    {
      (void) return_error_to_client (thread_p, rid);
    }

  ptr = or_pack_int (reply, ret);
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * ses_posix_copy_file -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
ses_posix_copy_file (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  char *src_path, *metaname, new_path[PATH_MAX];
  int path_size = 0, ret;
  OR_ALIGNED_BUF (OR_INT_SIZE * 2) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *ptr;

  ptr = or_unpack_string_nocopy (request, &src_path);
  ptr = or_unpack_string_nocopy (ptr, &metaname);

  ret = xes_posix_copy_file (src_path, metaname, new_path);
  if (ret != NO_ERROR)
    {
      (void) return_error_to_client (thread_p, rid);
    }
  else
    {
      path_size = strlen (new_path) + 1;
    }

  ptr = or_pack_int (reply, path_size);
  ptr = or_pack_int (ptr, ret);
  css_send_reply_and_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply), new_path,
				     path_size);
}

/*
 * ses_posix_rename_file -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
ses_posix_rename_file (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  char *src_path, *metaname, new_path[PATH_MAX];
  int path_size = 0, ret;
  OR_ALIGNED_BUF (OR_INT_SIZE * 2) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *ptr;

  ptr = or_unpack_string_nocopy (request, &src_path);
  ptr = or_unpack_string_nocopy (ptr, &metaname);

  ret = xes_posix_rename_file (src_path, metaname, new_path);
  if (ret != NO_ERROR)
    {
      (void) return_error_to_client (thread_p, rid);
    }
  else
    {
      path_size = strlen (new_path) + 1;
    }

  ptr = or_pack_int (reply, path_size);
  ptr = or_pack_int (ptr, ret);
  css_send_reply_and_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply), new_path,
				     path_size);
}

/*
 * ses_posix_read_file -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
ses_posix_get_file_size (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  char *path;
  off_t file_size;
  OR_ALIGNED_BUF (OR_INT64_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *ptr;

  ptr = or_unpack_string_nocopy (request, &path);

  file_size = xes_posix_get_file_size (path);
  if (file_size < 0)
    {
      (void) return_error_to_client (thread_p, rid);
    }

  ptr = or_pack_int64 (reply, (INT64) file_size);
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * slocator_upgrade_instances_domain -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
slocator_upgrade_instances_domain (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *ptr;
  OID class_oid;
  int attr_id;
  int success;

  ptr = request;
  ptr = or_unpack_oid (ptr, &class_oid);
  ptr = or_unpack_int (ptr, &attr_id);

  success = xlocator_upgrade_instances_domain (thread_p, &class_oid, attr_id);

  if (success != NO_ERROR)
    {
      (void) return_error_to_client (thread_p, rid);
    }

  ptr = or_pack_int (reply, success);
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * ssession_find_or_create_session -
 *
 * return: void
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE: This function checks if a session is still active and creates a new
 * one if needed
 */
void
ssession_find_or_create_session (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  SESSION_ID id = DB_EMPTY_SESSION;
  int row_count = -1, area_size;
  OR_ALIGNED_BUF (OR_INT_SIZE + OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *ptr = NULL, *area = NULL;
  char *db_user = NULL, *host = NULL, *program_name = NULL;
  char db_user_upper[DB_MAX_USER_LENGTH] = { '\0' };
  char server_session_key[SERVER_SESSION_KEY_SIZE];
  SESSION_PARAM *session_params = NULL;
  int error = NO_ERROR, update_parameter_values = 0;

  ptr = or_unpack_int (request, (int *) &id);
  ptr = or_unpack_stream (ptr, server_session_key, SERVER_SESSION_KEY_SIZE);
  ptr = sysprm_unpack_session_parameters (ptr, &session_params);
  ptr = or_unpack_string_alloc (ptr, &db_user);
  ptr = or_unpack_string_alloc (ptr, &host);
  ptr = or_unpack_string_alloc (ptr, &program_name);

  if (id == DB_EMPTY_SESSION
      || memcmp (server_session_key, xboot_get_server_session_key (), SERVER_SESSION_KEY_SIZE) != 0
      || xsession_check_session (thread_p, id) != NO_ERROR)
    {
      /* not an error yet */
      er_clear ();
      /* create new session */
      error = xsession_create_new (thread_p, &id);
      if (error != NO_ERROR)
	{
	  (void) return_error_to_client (thread_p, rid);
	}
    }

  /* get row count */
  xsession_get_row_count (thread_p, &row_count);

  if (error == NO_ERROR)
    {
      error = sysprm_session_init_session_parameters (&session_params, &update_parameter_values);
      if (error != NO_ERROR)
	{
	  error = sysprm_set_error ((SYSPRM_ERR) error, NULL);
	  (void) return_error_to_client (thread_p, rid);
	}
    }

  area_size = 0;
  if (error == NO_ERROR)
    {
      /* key.id */
      area_size = OR_INT_SIZE;

      /* row_count */
      area_size += OR_INT_SIZE;

      /* server session key */
      area_size += or_packed_stream_length (SERVER_SESSION_KEY_SIZE);

      /* update_parameter_values */
      area_size += OR_INT_SIZE;

      if (update_parameter_values)
	{
	  /* session params */
	  area_size += sysprm_packed_session_parameters_length (session_params, area_size);
	}

      area = (char *) malloc (area_size);
      if (area != NULL)
	{
	  ptr = or_pack_int (area, id);
	  ptr = or_pack_int (ptr, row_count);
	  ptr = or_pack_stream (ptr, xboot_get_server_session_key (), SERVER_SESSION_KEY_SIZE);
	  ptr = or_pack_int (ptr, update_parameter_values);
	  if (update_parameter_values)
	    {
	      ptr = sysprm_pack_session_parameters (ptr, session_params);
	    }
	}
      else
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) area_size);
	  error = ER_OUT_OF_VIRTUAL_MEMORY;
	  area_size = 0;
	  (void) return_error_to_client (thread_p, rid);
	}
    }

  if (db_user != NULL)
    {
      assert (host != NULL);
      assert (program_name != NULL);

      intl_identifier_upper (db_user, db_user_upper);
      css_set_user_access_status (db_user_upper, host, program_name);

      logtb_set_current_user_name (thread_p, db_user_upper);
    }

  free_and_init (db_user);
  free_and_init (host);
  free_and_init (program_name);

  ptr = or_pack_int (reply, area_size);
  ptr = or_pack_int (ptr, error);
  css_send_reply_and_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply), area, area_size);
  if (area != NULL)
    {
      free_and_init (area);
    }
}

/*
 * ssession_end_session -
 *
 * return: void
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE: This function ends the session with the id contained in the request
 */
void
ssession_end_session (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  int err = NO_ERROR;
  SESSION_ID id;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *ptr = NULL;

  (void) or_unpack_int (request, (int *) &id);

  err = xsession_end_session (thread_p, id);

  ptr = or_pack_int (reply, err);
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * ssession_set_row_count - set the count of affected rows for a session
 *
 * return: void
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
ssession_set_row_count (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  int err = NO_ERROR;
  int row_count = 0;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *ptr = NULL;

  (void) or_unpack_int (request, &row_count);

  err = xsession_set_row_count (thread_p, row_count);
  if (err != NO_ERROR)
    {
      (void) return_error_to_client (thread_p, rid);
    }

  ptr = or_pack_int (reply, err);
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * ssession_get_row_count - get the count of affected rows for a session
 * return: void
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 * NOTE:
 */
void
ssession_get_row_count (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  int err = NO_ERROR;
  int row_count = 0;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *ptr = NULL;

  err = xsession_get_row_count (thread_p, &row_count);
  if (err != NO_ERROR)
    {
      (void) return_error_to_client (thread_p, rid);
    }

  ptr = or_pack_int (reply, row_count);
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * ssession_get_last_insert_id  - get the value of the last update serial
 * return: error code or NO_ERROR
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 * NOTE:
 */
void
ssession_get_last_insert_id (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  int err = NO_ERROR;
  DB_VALUE lid;
  OR_ALIGNED_BUF (OR_INT_SIZE + OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *data_reply = NULL;
  int data_size = 0;
  char *ptr = NULL;
  int update_last_insert_id;

  (void) or_unpack_int (request, &update_last_insert_id);

  err = xsession_get_last_insert_id (thread_p, &lid, (bool) update_last_insert_id);
  if (err != NO_ERROR)
    {
      (void) return_error_to_client (thread_p, rid);
      data_size = 0;
      goto end;
    }

  data_size = OR_VALUE_ALIGNED_SIZE (&lid);

  data_reply = (char *) db_private_alloc (thread_p, data_size);
  if (data_reply == NULL)
    {
      (void) return_error_to_client (thread_p, rid);
      err = ER_FAILED;
      data_size = 0;
      goto end;
    }
  or_pack_value (data_reply, &lid);

end:
  ptr = or_pack_int (reply, data_size);
  ptr = or_pack_int (ptr, err);

  css_send_reply_and_data_to_client (thread_p->conn_entry, rid, OR_ALIGNED_BUF_START (a_reply),
				     OR_ALIGNED_BUF_SIZE (a_reply), data_reply, data_size);

  if (data_reply != NULL)
    {
      db_private_free (thread_p, data_reply);
    }
}

/*
 * ssession_reset_cur_insert_id  - reset the current insert id as NULL
 * return: error code or NO_ERROR
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 * NOTE:
 */
void
ssession_reset_cur_insert_id (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  int err = NO_ERROR;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *ptr = NULL;

  err = xsession_reset_cur_insert_id (thread_p);
  if (err != NO_ERROR)
    {
      (void) return_error_to_client (thread_p, rid);
    }

  ptr = or_pack_int (reply, err);
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * ssession_create_prepared_statement - create a prepared statement
 * return: error code or NO_ERROR
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 * NOTE:
 */
void
ssession_create_prepared_statement (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  /* request data */
  char *name = NULL, *alias_print = NULL;
  char *reply = NULL, *ptr = NULL;
  char *data_request = NULL;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  int data_size = 0, err = 0;
  char *info = NULL;
  SHA1Hash alias_sha1 = SHA1_HASH_INITIALIZER;

  reply = OR_ALIGNED_BUF_START (a_reply);

  /* name */
  ptr = or_unpack_string_alloc (request, &name);
  /* alias_print */
  ptr = or_unpack_string_alloc (ptr, &alias_print);
  /* data_size */
  ptr = or_unpack_int (ptr, &data_size);
  if (data_size <= 0)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NET_SERVER_DATA_RECEIVE, 0);
      css_send_abort_to_client (thread_p->conn_entry, rid);
      goto error;
    }
  if (alias_print != NULL)
    {
      /* alias_sha1 */
      ptr = or_unpack_sha1 (ptr, &alias_sha1);
    }

  err = css_receive_data_from_client (thread_p->conn_entry, rid, &data_request, &data_size);
  if (err != NO_ERROR)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NET_SERVER_DATA_RECEIVE, 0);
      css_send_abort_to_client (thread_p->conn_entry, rid);
      goto error;
    }

  /* For prepared statements, on the server side, we only use the user OID, the statement name and the alias print.
   * User OID and alias_print are needed as XASL cache key and the statement name is the identifier for the statement.
   * The rest of the information will be kept unpacked and sent back to the client when requested */
  info = (char *) malloc (data_size);
  if (info == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) data_size);
      goto error;
    }
  memcpy (info, data_request, data_size);

  err = xsession_create_prepared_statement (thread_p, name, alias_print, &alias_sha1, info, data_size);

  if (err != NO_ERROR)
    {
      goto error;
    }

  or_pack_int (reply, err);
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));

  if (data_request != NULL)
    {
      free_and_init (data_request);
    }

  return;

error:
  (void) return_error_to_client (thread_p, rid);
  or_pack_int (reply, err);
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));

  /* free data */
  if (data_request != NULL)
    {
      free_and_init (data_request);
    }
  if (name != NULL)
    {
      free_and_init (name);
    }
  if (alias_print != NULL)
    {
      free_and_init (alias_print);
    }
  if (info != NULL)
    {
      free_and_init (info);
    }
}

/*
 * ssession_get_prepared_statement - create a prepared statement
 * return: error code or NO_ERROR
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 * NOTE:
 */
void
ssession_get_prepared_statement (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  char *name = NULL, *stmt_info = NULL;
  int info_len = 0;
  char *reply = NULL, *ptr = NULL, *data_reply = NULL;
  int err = NO_ERROR, reply_size = 0;
  XASL_ID xasl_id;
  /* return code + data length */
  OR_ALIGNED_BUF (OR_INT_SIZE * 2 + OR_XASL_ID_SIZE) a_reply;
  int get_xasl_header = 0;
  XASL_NODE_HEADER xasl_header, *xasl_header_p = NULL;

  /* unpack prepared statement name */
  ptr = or_unpack_string (request, &name);
  /* unpack get XASL node header boolean */
  ptr = or_unpack_int (ptr, &get_xasl_header);
  if (get_xasl_header)
    {
      /* need to get XASL node header too */
      xasl_header_p = &xasl_header;
      INIT_XASL_NODE_HEADER (xasl_header_p);
    }

  err = xsession_get_prepared_statement (thread_p, name, &stmt_info, &info_len, &xasl_id, xasl_header_p);
  if (err != NO_ERROR)
    {
      goto error;
    }

  /* pack reply buffer */
  reply_size = or_packed_stream_length (info_len);	/* smt_info */
  if (get_xasl_header)
    {
      reply_size += XASL_NODE_HEADER_SIZE;	/* xasl node header */
    }
  data_reply = (char *) malloc (reply_size);
  if (data_reply == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) reply_size);
      err = ER_FAILED;
      goto error;
    }

  ptr = or_pack_stream (data_reply, stmt_info, info_len);
  if (get_xasl_header)
    {
      /* pack XASL node header */
      OR_PACK_XASL_NODE_HEADER (ptr, xasl_header_p);
    }

  reply = OR_ALIGNED_BUF_START (a_reply);

  ptr = or_pack_int (reply, reply_size);
  ptr = or_pack_int (ptr, err);
  OR_PACK_XASL_ID (ptr, &xasl_id);

  err =
    css_send_reply_and_data_to_client (thread_p->conn_entry, rid, OR_ALIGNED_BUF_START (a_reply),
				       OR_ALIGNED_BUF_SIZE (a_reply), data_reply, reply_size);
  goto cleanup;

error:
  reply_size = 0;
  if (data_reply != NULL)
    {
      free_and_init (data_reply);
    }
  ptr = OR_ALIGNED_BUF_START (a_reply);
  ptr = or_pack_int (ptr, 0);
  or_pack_int (ptr, err);

  (void) return_error_to_client (thread_p, rid);

  err =
    css_send_reply_and_data_to_client (thread_p->conn_entry, rid, OR_ALIGNED_BUF_START (a_reply),
				       OR_ALIGNED_BUF_SIZE (a_reply), data_reply, reply_size);
  goto cleanup;

cleanup:
  if (data_reply != NULL)
    {
      free_and_init (data_reply);
    }
  if (stmt_info != NULL)
    {
      free_and_init (stmt_info);
    }
  if (name != NULL)
    {
      db_private_free_and_init (thread_p, name);
    }
}

/*
 * ssession_delete_prepared_statement - get prepared statement info
 * return: error code or NO_ERROR
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 * NOTE:
 */
void
ssession_delete_prepared_statement (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  int err = NO_ERROR;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *name = NULL;

  or_unpack_string_nocopy (request, &name);
  if (name == NULL)
    {
      (void) return_error_to_client (thread_p, rid);
      err = ER_FAILED;
    }
  else
    {
      err = xsession_delete_prepared_statement (thread_p, name);
      if (err != NO_ERROR)
	{
	  (void) return_error_to_client (thread_p, rid);
	}
    }

  or_pack_int (reply, err);

  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * slogin_user - login user
 * return: error code or NO_ERROR
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 */
void
slogin_user (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  int err = NO_ERROR;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *username = NULL;

  or_unpack_string_nocopy (request, &username);
  if (username == NULL)
    {
      (void) return_error_to_client (thread_p, rid);
      err = ER_FAILED;
    }
  else
    {
      err = xlogin_user (thread_p, username);
      if (err != NO_ERROR)
	{
	  (void) return_error_to_client (thread_p, rid);
	}
    }

  or_pack_int (reply, err);

  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * ssession_set_session_variables () - set session variables
 * return :void
 * thread_p (in) :
 * rid (in) :
 * request (in) :
 * reqlen (in) :
 */
void
ssession_set_session_variables (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  int count = 0, err = NO_ERROR, data_size = 0, i = 0;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = NULL, *ptr = NULL, *data_request = NULL;
  DB_VALUE *values = NULL;

  /* Unpack count of variables from request */
  ptr = or_unpack_int (request, &count);
  if (count <= 0)
    {
      goto cleanup;
    }

  /* fetch the values */
  err = css_receive_data_from_client (thread_p->conn_entry, rid, &data_request, &data_size);
  if (err != NO_ERROR)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NET_SERVER_DATA_RECEIVE, 0);
      css_send_abort_to_client (thread_p->conn_entry, rid);
      goto cleanup;
    }

  values = (DB_VALUE *) malloc (count * sizeof (DB_VALUE));
  if (values == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, count * sizeof (DB_VALUE));
      err = ER_FAILED;
      goto cleanup;
    }

  ptr = data_request;

  /* session variables are packed into an array containing DB_VALUE objects of the form name1, value1, name2, value2,
   * name3, value 3... */
  for (i = 0; i < count; i++)
    {
      ptr = or_unpack_db_value (ptr, &values[i]);
    }

  err = xsession_set_session_variables (thread_p, values, count);

cleanup:
  if (values != NULL)
    {
      for (i = 0; i < count; i++)
	{
	  pr_clear_value (&values[i]);
	}
      free_and_init (values);
    }

  if (data_request != NULL)
    {
      free_and_init (data_request);
    }

  reply = OR_ALIGNED_BUF_START (a_reply);

  or_pack_int (reply, err);

  if (err != NO_ERROR)
    {
      (void) return_error_to_client (thread_p, rid);
    }

  css_send_data_to_client (thread_p->conn_entry, rid, OR_ALIGNED_BUF_START (a_reply), OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * ssession_get_session_variable () - get the value of a session variable
 * return : void
 * thread_p (in) :
 * rid (in) :
 * request (in) :
 * reqlen (in) :
 */
void
ssession_get_session_variable (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  int err = NO_ERROR;
  OR_ALIGNED_BUF (OR_INT_SIZE * 2) a_reply;
  char *reply = NULL, *ptr = NULL, *data_reply = NULL;
  DB_VALUE result, name;
  int size = 0;

  db_make_null (&result);
  db_make_null (&name);

  reply = OR_ALIGNED_BUF_START (a_reply);

  or_unpack_db_value (request, &name);

  err = xsession_get_session_variable (thread_p, &name, &result);
  if (err != NO_ERROR)
    {
      (void) return_error_to_client (thread_p, rid);
    }

  size = or_db_value_size (&result);
  data_reply = (char *) malloc (size);
  if (data_reply != NULL)
    {
      or_pack_db_value (data_reply, &result);
    }
  else
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) size);
      (void) return_error_to_client (thread_p, rid);
      size = 0;
      err = ER_FAILED;
    }

  ptr = or_pack_int (reply, size);
  ptr = or_pack_int (ptr, err);

  css_send_reply_and_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply), data_reply, size);

  pr_clear_value (&result);
  pr_clear_value (&name);

  if (data_reply != NULL)
    {
      free_and_init (data_reply);
    }
}

/*
 * svacuum () - Calls vacuum function.
 *
 * return	 :
 * thread_p (in) :
 * rid (in)	 :
 * request (in)	 :
 * reqlen (in)	 :
 */
void
svacuum (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  int err = NO_ERROR;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = NULL;

  reply = OR_ALIGNED_BUF_START (a_reply);

  /* Call vacuum */
  err = xvacuum (thread_p);

  if (err != NO_ERROR)
    {
      (void) return_error_to_client (thread_p, rid);
    }

  /* Send error code as reply */
  (void) or_pack_int (reply, err);

  /* For now no results are required, just fail/success */
  css_send_data_to_client (thread_p->conn_entry, rid, OR_ALIGNED_BUF_START (a_reply), OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * svacuum_dump -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
svacuum_dump (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  FILE *outfp;
  int file_size;
  char *buffer;
  int buffer_size;
  int send_size;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  (void) or_unpack_int (request, &buffer_size);

  buffer = (char *) db_private_alloc (thread_p, buffer_size);
  if (buffer == NULL)
    {
      css_send_abort_to_client (thread_p->conn_entry, rid);
      return;
    }

  outfp = tmpfile ();
  if (outfp == NULL)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      css_send_abort_to_client (thread_p->conn_entry, rid);
      db_private_free_and_init (thread_p, buffer);
      return;
    }

  xvacuum_dump (thread_p, outfp);
  file_size = ftell (outfp);

  /*
   * Send the file in pieces
   */
  rewind (outfp);

  (void) or_pack_int (reply, (int) file_size);
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));

  while (file_size > 0)
    {
      if (file_size > buffer_size)
	{
	  send_size = buffer_size;
	}
      else
	{
	  send_size = file_size;
	}

      file_size -= send_size;
      if (fread (buffer, 1, send_size, outfp) == 0)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  css_send_abort_to_client (thread_p->conn_entry, rid);
	  /*
	   * Continue sending the stuff that was prmoised to client. In this case
	   * junk (i.e., whatever it is in the buffers) is sent.
	   */
	}
      css_send_data_to_client (thread_p->conn_entry, rid, buffer, send_size);
    }
  fclose (outfp);
  db_private_free_and_init (thread_p, buffer);
}

/*
 * slogtb_get_mvcc_snapshot () - Get MVCC Snapshot.
 *
 * return	 :
 * thread_p (in) :
 * rid (in)	 :
 * request (in)  :
 * reqlen (in)	 :
 */
void
slogtb_get_mvcc_snapshot (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = NULL;
  int err;

  err = xlogtb_get_mvcc_snapshot (thread_p);

  reply = OR_ALIGNED_BUF_START (a_reply);

  (void) or_pack_int (reply, err);

  if (err != NO_ERROR)
    {
      (void) return_error_to_client (thread_p, rid);
    }

  css_send_data_to_client (thread_p->conn_entry, rid, OR_ALIGNED_BUF_START (a_reply), OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * ssession_drop_session_variables () - drop session variables
 * return : void
 * thread_p (in) :
 * rid (in) :
 * request (in) :
 * reqlen (in) :
 */
void
ssession_drop_session_variables (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  int count = 0, err = NO_ERROR, data_size = 0, i = 0;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = NULL, *ptr = NULL, *data_request = NULL;
  DB_VALUE *values = NULL;

  /* Unpack count of variables from request */
  ptr = or_unpack_int (request, &count);
  if (count <= 0)
    {
      goto cleanup;
    }

  /* fetch the values */
  err = css_receive_data_from_client (thread_p->conn_entry, rid, &data_request, &data_size);
  if (err != NO_ERROR)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NET_SERVER_DATA_RECEIVE, 0);
      css_send_abort_to_client (thread_p->conn_entry, rid);
      goto cleanup;
    }

  values = (DB_VALUE *) malloc (count * sizeof (DB_VALUE));
  if (values == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, count * sizeof (DB_VALUE));
      err = ER_FAILED;
      goto cleanup;
    }

  ptr = data_request;
  for (i = 0; i < count; i++)
    {
      ptr = or_unpack_db_value (ptr, &values[i]);
    }

  err = xsession_drop_session_variables (thread_p, values, count);

cleanup:
  if (values != NULL)
    {
      for (i = 0; i < count; i++)
	{
	  pr_clear_value (&values[i]);
	}
      free_and_init (values);
    }

  if (data_request != NULL)
    {
      free_and_init (data_request);
    }

  reply = OR_ALIGNED_BUF_START (a_reply);

  or_pack_int (reply, err);

  if (err != NO_ERROR)
    {
      (void) return_error_to_client (thread_p, rid);
    }

  css_send_data_to_client (thread_p->conn_entry, rid, OR_ALIGNED_BUF_START (a_reply), OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * sboot_get_locales_info () - get info about locales
 * return : void
 * thread_p (in) :
 * rid (in) :
 * request (in) :
 * reqlen (in) :
 */
void
sboot_get_locales_info (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  int err = NO_ERROR;
  OR_ALIGNED_BUF (2 * OR_INT_SIZE) a_reply;
  char *reply = NULL, *ptr = NULL, *data_reply = NULL;
  int size = 0, i;
  int len_str;
  const int collation_cnt = lang_collation_count ();
  const int lang_cnt = lang_locales_count (false);
  const int locales_cnt = lang_locales_count (true);
  int found_coll = 0;

  reply = OR_ALIGNED_BUF_START (a_reply);

  /* compute size of packed information */
  for (i = 0; i < LANG_MAX_COLLATIONS; i++)
    {
      LANG_COLLATION *lc = lang_get_collation (i);

      assert (lc != NULL);
      if (i != 0 && lc->coll.coll_id == LANG_COLL_DEFAULT)
	{
	  /* iso88591 binary collation added only once */
	  continue;
	}
      found_coll++;

      size += 2 * OR_INT_SIZE;	/* collation id , codeset */
      size += or_packed_string_length (lc->coll.coll_name, &len_str);
      size += or_packed_string_length (lc->coll.checksum, &len_str);
    }

  assert (found_coll == collation_cnt);

  for (i = 0; i < lang_cnt; i++)
    {
      const LANG_LOCALE_DATA *lld = lang_get_first_locale_for_lang (i);

      assert (lld != NULL);

      do
	{
	  size += or_packed_string_length (lld->lang_name, &len_str);
	  size += OR_INT_SIZE;	/* codeset */
	  size += or_packed_string_length (lld->checksum, &len_str);

	  lld = lld->next_lld;
	}
      while (lld != NULL);
    }

  size += 2 * OR_INT_SIZE;	/* collation_cnt, locales_cnt */

  data_reply = (char *) malloc (size);
  if (data_reply != NULL)
    {
      ptr = or_pack_int (data_reply, collation_cnt);
      ptr = or_pack_int (ptr, locales_cnt);
      found_coll = 0;

      /* pack collation information : */
      for (i = 0; i < LANG_MAX_COLLATIONS; i++)
	{
	  LANG_COLLATION *lc = lang_get_collation (i);

	  assert (lc != NULL);

	  if (i != 0 && lc->coll.coll_id == LANG_COLL_DEFAULT)
	    {
	      continue;
	    }

	  found_coll++;

	  ptr = or_pack_int (ptr, lc->coll.coll_id);

	  len_str = strlen (lc->coll.coll_name);
	  ptr = or_pack_string_with_length (ptr, lc->coll.coll_name, len_str);

	  ptr = or_pack_int (ptr, (int) lc->codeset);

	  len_str = strlen (lc->coll.checksum);
	  ptr = or_pack_string_with_length (ptr, lc->coll.checksum, len_str);
	}
      assert (found_coll == collation_cnt);

      /* pack locale information : */
      for (i = 0; i < lang_cnt; i++)
	{
	  const LANG_LOCALE_DATA *lld = lang_get_first_locale_for_lang (i);

	  assert (lld != NULL);

	  do
	    {
	      len_str = strlen (lld->lang_name);
	      ptr = or_pack_string_with_length (ptr, lld->lang_name, len_str);

	      ptr = or_pack_int (ptr, lld->codeset);

	      len_str = strlen (lld->checksum);
	      ptr = or_pack_string_with_length (ptr, lld->checksum, len_str);

	      lld = lld->next_lld;
	    }
	  while (lld != NULL);
	}
    }
  else
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) size);
      (void) return_error_to_client (thread_p, rid);
      size = 0;
      err = ER_FAILED;
    }

  ptr = or_pack_int (reply, size);
  ptr = or_pack_int (ptr, err);

  css_send_reply_and_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply), data_reply, size);

  if (data_reply != NULL)
    {
      free_and_init (data_reply);
    }
}

/*
 * sboot_get_timezone_checksum () - get the timezone library checksum
 * return : void
 * thread_p (in) :
 * rid (in) :
 * request (in) :
 * reqlen (in) :
 */
void
sboot_get_timezone_checksum (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  int err = NO_ERROR;
  OR_ALIGNED_BUF (2 * OR_INT_SIZE) a_reply;
  char *reply = NULL, *ptr = NULL, *data_reply = NULL;
  int size = 0;
  int len_str;
  const TZ_DATA *tzd;

  tzd = tz_get_data ();

  assert (tzd != NULL);
  reply = OR_ALIGNED_BUF_START (a_reply);

  size += or_packed_string_length (tzd->checksum, &len_str);

  data_reply = (char *) malloc (size);

  if (data_reply != NULL)
    {
      len_str = strlen (tzd->checksum);
      ptr = or_pack_string_with_length (data_reply, tzd->checksum, len_str);
    }
  else
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, size);
      (void) return_error_to_client (thread_p, rid);
      size = 0;
      err = ER_FAILED;
    }

  ptr = or_pack_int (reply, size);
  ptr = or_pack_int (ptr, err);

  css_send_reply_and_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply), data_reply, size);

  if (data_reply != NULL)
    {
      free_and_init (data_reply);
    }
}

/*
 * schksum_insert_repl_log_and_demote_table_lock -
 *
 * return: error code
 *
 * NOTE: insert replication log and demote the read lock of the table
 */
void
schksum_insert_repl_log_and_demote_table_lock (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  int success = NO_ERROR;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *ptr;
  OID class_oid;
  REPL_INFO repl_info = { NULL, 0, false };
  REPL_INFO_SBR repl_stmt = { 0, NULL, NULL, NULL, NULL };

  ptr = or_unpack_oid (request, &class_oid);
  ptr = or_unpack_int (ptr, &repl_info.repl_info_type);
  switch (repl_info.repl_info_type)
    {
    case REPL_INFO_TYPE_SBR:
      {
	ptr = or_unpack_int (ptr, &repl_stmt.statement_type);
	ptr = or_unpack_string_nocopy (ptr, &repl_stmt.name);
	ptr = or_unpack_string_nocopy (ptr, &repl_stmt.stmt_text);
	ptr = or_unpack_string_nocopy (ptr, &repl_stmt.db_user);
	ptr = or_unpack_string_nocopy (ptr, &repl_stmt.sys_prm_context);

	repl_info.info = (char *) &repl_stmt;
	break;
      }
    default:
      success = ER_FAILED;
      break;
    }

  if (success == NO_ERROR)
    {
      success = xchksum_insert_repl_log_and_demote_table_lock (thread_p, &repl_info, &class_oid);
    }

  (void) or_pack_int (reply, success);
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * slogtb_does_active_user_exist -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
slogtb_does_active_user_exist (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  char *user_name;
  bool existed;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  (void) or_unpack_string_nocopy (request, &user_name);
  existed = xlogtb_does_active_user_exist (thread_p, user_name);

  (void) or_pack_int (reply, (int) existed);
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * slocator_redistribute_partition_data () -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
slocator_redistribute_partition_data (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *ptr;
  OID *oid_list;
  int nr_oids;
  int success = NO_ERROR;
  int i;
  OID class_oid;

  ptr = request;
  ptr = or_unpack_oid (ptr, &class_oid);
  ptr = or_unpack_int (ptr, &nr_oids);

  if (nr_oids < 1)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INVALID_PARTITION_REQUEST, 0);
      (void) return_error_to_client (thread_p, rid);
      success = ER_INVALID_PARTITION_REQUEST;
      goto end;
    }

  oid_list = (OID *) malloc (nr_oids * sizeof (OID));
  if (oid_list == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, nr_oids * sizeof (OID));
      (void) return_error_to_client (thread_p, rid);
      success = ER_OUT_OF_VIRTUAL_MEMORY;
      goto end;
    }

  for (i = 0; i < nr_oids; i++)
    {
      ptr = or_unpack_oid (ptr, &oid_list[i]);
    }

  success = xlocator_redistribute_partition_data (thread_p, &class_oid, nr_oids, oid_list);

  if (oid_list != NULL)
    {
      free_and_init (oid_list);
    }

  if (success != NO_ERROR)
    {
      (void) return_error_to_client (thread_p, rid);
    }

end:
  ptr = or_pack_int (reply, success);
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * netsr_spacedb () - server-side function to get database space info
 *
 * return        : void
 * thread_p (in) : thread entry
 * rid (in)      : request ID
 * request (in)  : request data
 * reqlen (in)   : request data length
 */
void
netsr_spacedb (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  SPACEDB_ALL all[SPACEDB_ALL_COUNT];
  SPACEDB_ONEVOL *vols = NULL;
  SPACEDB_FILES files[SPACEDB_FILE_COUNT];

  int get_vols = 0;
  int get_files = 0;
  SPACEDB_ONEVOL **volsp = NULL;
  SPACEDB_FILES *filesp = NULL;

  OR_ALIGNED_BUF (2 * OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *data_reply = NULL;
  int data_reply_length = 0;

  char *ptr;

  int error_code = NO_ERROR;

  /* do we need space information on all volumes? */
  ptr = or_unpack_int (request, &get_vols);
  if (get_vols)
    {
      volsp = &vols;
    }
  /* do we need detailed file information? */
  ptr = or_unpack_int (ptr, &get_files);
  if (get_files)
    {
      filesp = files;
    }

  /* get info from disk manager */
  error_code = disk_spacedb (thread_p, all, volsp);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
    }
  else if (get_files)
    {
      /* get info from file manager */
      error_code = file_spacedb (thread_p, filesp);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	}
    }

  if (error_code == NO_ERROR)
    {
      /* success. pack space info */
      data_reply_length = or_packed_spacedb_size (all, vols, filesp);
      data_reply = (char *) db_private_alloc (thread_p, data_reply_length);
      ptr = or_pack_spacedb (data_reply, all, vols, filesp);
      assert (ptr - data_reply == data_reply_length);
    }
  else
    {
      /* error */
      (void) return_error_to_client (thread_p, rid);
    }

  /* send result to client */
  ptr = or_pack_int (reply, data_reply_length);
  ptr = or_pack_int (ptr, error_code);

  css_send_reply_and_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply), data_reply,
				     data_reply_length);

  if (vols != NULL)
    {
      free_and_init (vols);
    }
  if (data_reply != NULL)
    {
      db_private_free_and_init (thread_p, data_reply);
    }
}

void
slocator_demote_class_lock (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  int error;
  OID class_oid;
  LOCK lock, ex_lock;
  char *ptr;
  OR_ALIGNED_BUF (OR_INT_SIZE + OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  ptr = or_unpack_oid (request, &class_oid);
  ptr = or_unpack_lock (ptr, &lock);

  error = xlocator_demote_class_lock (thread_p, &class_oid, lock, &ex_lock);

  if (error != NO_ERROR)
    {
      return_error_to_client (thread_p, rid);
    }

  ptr = or_pack_int (reply, error);
  ptr = or_pack_lock (ptr, ex_lock);
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

void
sloaddb_init (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  packing_unpacker unpacker (request, (size_t) reqlen);

  /* *INDENT-OFF* */
  cubload::load_args args;
  /* *INDENT-ON* */

  args.unpack (unpacker);

  load_session *session = new load_session (args);

  int error_code = session_set_load_session (thread_p, session);
  if (error_code != NO_ERROR)
    {
      delete session;
    }

  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  or_pack_int (reply, error_code);
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

void
sloaddb_install_class (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  packing_unpacker unpacker (request, (size_t) reqlen);
  bool is_ignored = false;

  /* *INDENT-OFF* */
  cubload::batch batch;
  /* *INDENT-ON* */

  batch.unpack (unpacker);

  load_session *session = NULL;
  int error_code = session_get_load_session (thread_p, session);
  std::string cls_name;
  if (error_code == NO_ERROR)
    {
      assert (session != NULL);
      error_code = session->install_class (*thread_p, batch, is_ignored, cls_name);
    }
  else
    {
      if (er_errid () == NO_ERROR || !er_has_error ())
	{
	  error_code = ER_LDR_INVALID_STATE;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_code, 0);
	}

      return_error_to_client (thread_p, rid);
    }

  // Error code and is_ignored.
  OR_ALIGNED_BUF (3 * OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *ptr;
  int buf_sz = (int) cls_name.length ();
  ptr = or_pack_int (reply, buf_sz);
  ptr = or_pack_int (ptr, error_code);
  ptr = or_pack_int (ptr, (is_ignored ? 1 : 0));

  css_send_reply_and_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply),
				     (char *) cls_name.c_str (), buf_sz);
}

void
sloaddb_load_batch (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  packing_unpacker unpacker (request, (size_t) reqlen);

  /* *INDENT-OFF* */
  cubload::batch *batch = NULL;
  load_status status;
  packing_packer packer;
  cubmem::extensible_block eb;
  /* *INDENT-ON* */

  char *reply_data = NULL;
  int reply_data_size = 0;

  bool use_temp_batch = false;
  unpacker.unpack_bool (use_temp_batch);
  if (!use_temp_batch)
    {
      batch = new cubload::batch ();
      batch->unpack (unpacker);
    }

  bool is_batch_accepted = false;
  load_session *session = NULL;

  session_get_load_session (thread_p, session);
  int error_code = session_get_load_session (thread_p, session);
  if (error_code == NO_ERROR)
    {
      assert (session != NULL);
      error_code = session->load_batch (*thread_p, batch, use_temp_batch, is_batch_accepted, status);

      packer.set_buffer_and_pack_all (eb, status);

      reply_data = eb.get_ptr ();
      reply_data_size = (int) packer.get_current_size ();
    }
  else
    {
      if (er_errid () == NO_ERROR || !er_has_error ())
	{
	  error_code = ER_LDR_INVALID_STATE;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_code, 0);
	}

      return_error_to_client (thread_p, rid);
    }

  OR_ALIGNED_BUF (OR_INT_SIZE * 3) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  char *ptr = or_pack_int (reply, reply_data_size);
  ptr = or_pack_int (ptr, error_code);
  or_pack_int (ptr, (is_batch_accepted ? 1 : 0));


  css_send_reply_and_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply), reply_data,
				     reply_data_size);
}

void
sloaddb_fetch_status (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  packing_packer packer;
  cubmem::extensible_block eb;

  char *buffer = NULL;
  int buffer_size = 0;

  load_session *session = NULL;
  int error_code = session_get_load_session (thread_p, session);
  if (error_code == NO_ERROR)
    {
      assert (session != NULL);
      load_status status;
      session->fetch_status (status, false);
      packer.set_buffer_and_pack_all (eb, status);

      buffer = eb.get_ptr ();
      buffer_size = (int) packer.get_current_size ();
    }
  else
    {
      if (er_errid () == NO_ERROR || !er_has_error ())
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LDR_INVALID_STATE, 0);
	}

      return_error_to_client (thread_p, rid);
    }

  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  or_pack_int (reply, buffer_size);
  css_send_reply_and_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply), buffer,
				     buffer_size);
}

void
sloaddb_destroy (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  load_session *session = NULL;
  int error_code = session_get_load_session (thread_p, session);
  if (error_code == NO_ERROR)
    {
      assert (session != NULL);

      session->wait_for_completion ();
      delete session;
      session_set_load_session (thread_p, NULL);
    }

  if (er_errid () != NO_ERROR || er_has_error ())
    {
      return_error_to_client (thread_p, rid);
    }

  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  or_pack_int (reply, error_code);
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

void
sloaddb_interrupt (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  load_session *session = NULL;
  int error_code = session_get_load_session (thread_p, session);
  if (error_code == NO_ERROR)
    {
      assert (session != NULL);

      session->interrupt ();
    }
  else
    {
      // what to do, what to do...
    }
}

void
sloaddb_update_stats (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  char *buffer = NULL, *ptr = NULL;
  int buffer_size = 0;
  int oid_cnt = 0;
  OR_ALIGNED_BUF (OR_INT_SIZE + OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  load_session *session = NULL;
  int error_code = session_get_load_session (thread_p, session);
  std::vector < const cubload::class_entry * >class_entries;

  if (error_code != NO_ERROR)
    {
      goto end;
    }
  assert (session != NULL);

  /* check disable_statistics */
  if (session->get_args ().disable_statistics || session->get_args ().syntax_check)
    {
      error_code = NO_ERROR;
      goto end;
    }

  session->get_class_registry ().get_all_class_entries (class_entries);

for (const cubload::class_entry * class_entry:class_entries)
    {
      if (!class_entry->is_ignored ())
	{
	  oid_cnt++;
	}
    }

  /* start packing result */
  /* buffer_size is (int:number of OIDs) + size of packed OIDs */
  buffer_size = OR_INT_SIZE + (oid_cnt * sizeof (OID));
  buffer = (char *) db_private_alloc (thread_p, buffer_size);
  if (buffer == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, buffer_size);
      error_code = ER_OUT_OF_VIRTUAL_MEMORY;
      goto end;
    }
  ptr = or_pack_int (buffer, oid_cnt);
for (const cubload::class_entry * class_entry:class_entries)
    {
      if (!class_entry->is_ignored ())
	{
	  OID *class_oid = const_cast < OID * >(&class_entry->get_class_oid ());
	  ptr = or_pack_oid (ptr, class_oid);
	}
    }

end:
  char *ptr2 = or_pack_int (reply, buffer_size);
  ptr2 = or_pack_int (ptr2, error_code);
  css_send_reply_and_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply), buffer,
				     buffer_size);

  if (buffer != NULL)
    {
      db_private_free_and_init (thread_p, buffer);
    }
}

void
ssession_stop_attached_threads (void *session)
{
  session_stop_attached_threads (session);
}

static bool
cdc_check_client_connection ()
{
  if (css_check_conn (&cdc_Gl.conn) == NO_ERROR)
    {
      /* existing connection is alive */
      return true;
    }
  else
    {
      return false;
    }
}

void
smethod_invoke_fold_constants (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  packing_unpacker unpacker (request, (size_t) reqlen);

  /* 1) unpack arguments */
  method_sig_list sig_list;
  std::vector < DB_VALUE > args;
  unpacker.unpack_all (sig_list, args);

  std::vector < std::reference_wrapper < DB_VALUE >> ref_args (args.begin (), args.end ());

  /* 2) invoke method */
  DB_VALUE ret_value;
  db_make_null (&ret_value);
  int error_code = xmethod_invoke_fold_constants (thread_p, sig_list, ref_args, ret_value);

  cubmethod::runtime_context * rctx = cubmethod::get_rctx (thread_p);
  cubmethod::method_invoke_group * top_on_stack = NULL;
  if (rctx)
    {
      top_on_stack = rctx->top_stack ();
    }

  if (rctx == NULL || top_on_stack == NULL)
    {
      /* might be interrupted and session is already freed */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INTERRUPTED, 0);
      error_code = ER_INTERRUPTED;
      assert (false);		// oops
    }

  packing_packer packer;
  cubmem::extensible_block eb;
  if (error_code == NO_ERROR)
    {
      /* 3) make out arguments */

      method_sig_node *sig = sig_list.method_sig;
      // *INDENT-OFF*
      DB_VALUE dummy_null;
      db_make_null (&dummy_null);
      std::vector<std::reference_wrapper<DB_VALUE>> out_args (sig->num_method_args, dummy_null);
      // *INDENT-ON*
      for (int i = 0; i < sig->num_method_args; i++)
	{
	  if (sig->arg_info.arg_mode[i] == METHOD_ARG_MODE_IN)
	    {
	      continue;
	    }

	  int pos = sig->method_arg_pos[i];
	  out_args[pos] = std::ref (args[pos]);
	}

      /* 4) pack */
      packer.set_buffer_and_pack_all (eb, ret_value, out_args);
    }
  else
    {
      if (rctx->is_interrupted ())
	{
	  rctx->set_local_error_for_interrupt ();
	}
      else if (error_code != ER_SM_INVALID_METHOD_ENV)	/* FIXME: error possibly occured in builtin method, It should be handled at CAS */
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_EXECUTE_ERROR, 1, top_on_stack->get_error_msg ().c_str ());
	}
      else
	{
	  // error is already set
	  assert (er_errid () != NO_ERROR);
	}

      std::string err_msg;

      if (er_errid () == ER_SP_EXECUTE_ERROR)
	{
	  err_msg.assign (top_on_stack->get_error_msg ());
	}
      else if (er_msg ())
	{
	  err_msg.assign (er_msg ());
	}

      if (er_has_error ())
	{
	  error_code = er_errid ();
	}

      packer.set_buffer_and_pack_all (eb, er_errid (), err_msg);
      (void) return_error_to_client (thread_p, rid);
    }

  char *reply_data = eb.get_ptr ();
  int reply_data_size = (int) packer.get_current_size ();

  OR_ALIGNED_BUF (OR_INT_SIZE * 3) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *ptr = or_pack_int (reply, (int) END_CALLBACK);
  ptr = or_pack_int (ptr, reply_data_size);
  ptr = or_pack_int (ptr, error_code);

  // clear
  if (top_on_stack)
    {
      top_on_stack->reset (true);
      top_on_stack->end ();
    }

  css_send_reply_and_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply), reply_data,
				     reply_data_size);

  if (top_on_stack)
    {
      rctx->pop_stack (thread_p, top_on_stack);
    }

  pr_clear_value_vector (args);
  db_value_clear (&ret_value);
  sig_list.freemem ();
}

void
scdc_start_session (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *ptr;
  int error_code;
  int max_log_item, extraction_timeout, all_in_cond, num_extraction_user, num_extraction_class;
  uint64_t *extraction_classoids = NULL;
  char **extraction_user = NULL;

  char *dummy_user = NULL;

  if (prm_get_integer_value (PRM_ID_SUPPLEMENTAL_LOG) == 0)
    {
      error_code = ER_CDC_NOT_AVAILABLE;
      er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_CDC_NOT_AVAILABLE, 0);
      goto error;
    }

  /* scdc_start_session more than once without scdc_end_session */
  if (cdc_Gl.conn.fd != -1)
    {
      if (thread_p->conn_entry->fd != cdc_Gl.conn.fd)
	{
	  /* check if existing connection is alive */
	  if (cdc_check_client_connection ())
	    {
	      cdc_log ("%s : More than two clients attempt to connect", __func__);

	      error_code = ER_CDC_NOT_AVAILABLE;
	      er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_CDC_NOT_AVAILABLE, 0);
	      goto error;
	    }
	}

      /* if existing session is dead, then pause loginfo producer thread (cdc). */
      if (cdc_Gl.producer.state != CDC_PRODUCER_STATE_WAIT)
	{
	  cdc_pause_producer ();
	}

      LSA_SET_NULL (&cdc_Gl.consumer.next_lsa);
    }

  cdc_Gl.conn.fd = thread_p->conn_entry->fd;
  cdc_Gl.conn.status = thread_p->conn_entry->status;

  ptr = or_unpack_int (request, &max_log_item);
  ptr = or_unpack_int (ptr, &extraction_timeout);
  ptr = or_unpack_int (ptr, &all_in_cond);
  ptr = or_unpack_int (ptr, &num_extraction_user);

  if (num_extraction_user > 0)
    {
      extraction_user = (char **) malloc (sizeof (char *) * num_extraction_user);
      if (extraction_user == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (char *) * num_extraction_user);
	  error_code = ER_OUT_OF_VIRTUAL_MEMORY;
	  goto error;
	}

      for (int i = 0; i < num_extraction_user; i++)
	{
	  ptr = or_unpack_string_nocopy (ptr, &dummy_user);

	  extraction_user[i] = strdup (dummy_user);
	  if (extraction_user[i] == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, strlen (dummy_user));
	      error_code = ER_OUT_OF_VIRTUAL_MEMORY;
	      goto error;
	    }
	}
    }

  ptr = or_unpack_int (ptr, &num_extraction_class);

  if (num_extraction_class > 0)
    {
      extraction_classoids = (UINT64 *) malloc (sizeof (UINT64) * num_extraction_class);
      if (extraction_classoids == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
		  sizeof (UINT64) * num_extraction_class);
	  error_code = ER_OUT_OF_VIRTUAL_MEMORY;
	  goto error;
	}

      for (int i = 0; i < num_extraction_class; i++)
	{
	  ptr = or_unpack_int64 (ptr, (INT64 *) & extraction_classoids[i]);
	}
    }

  cdc_log
    ("%s : max_log_item (%d), extraction_timeout (%d), all_in_cond (%d), num_extraction_user (%d), num_extraction_class (%d)",
     __func__, max_log_item, extraction_timeout, all_in_cond, num_extraction_user, num_extraction_class);

  error_code =
    cdc_set_configuration (max_log_item, extraction_timeout, all_in_cond, extraction_user, num_extraction_user,
			   extraction_classoids, num_extraction_class);
  if (error_code != NO_ERROR)
    {
      goto error;
    }

  or_pack_int (reply, error_code);

  (void) css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));

  return;

error:

  if (extraction_user != NULL)
    {
      for (int i = 0; i < num_extraction_user; i++)
	{
	  if (extraction_user[i] != NULL)
	    {
	      free_and_init (extraction_user[i]);
	    }
	}

      free_and_init (extraction_user);
    }

  if (extraction_classoids != NULL)
    {
      free_and_init (extraction_classoids);
    }

  or_pack_int (reply, error_code);

  (void) css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));

  return;
}

void
scdc_find_lsa (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  OR_ALIGNED_BUF (OR_INT_SIZE + OR_LOG_LSA_ALIGNED_SIZE + OR_INT64_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *ptr;
  LOG_LSA start_lsa;
  time_t input_time;
  int error_code;

  ptr = or_unpack_int64 (request, &input_time);
  //if scdc_find_lsa() is called more than once, it should pause running cdc_loginfo_producer_execute() thread 

  cdc_log ("%s : input time (%lld)", __func__, input_time);

  error_code = cdc_find_lsa (thread_p, &input_time, &start_lsa);
  if (error_code == NO_ERROR || error_code == ER_CDC_ADJUSTED_LSA)
    {
      // check producer is sleep, and if not 
      // make producer sleep, and producer request consumer to be sleep 
      // if request is set to consumer to be sleep, go into spinlock 
      // checks request is set to none, then if it is none, 
      if (cdc_Gl.producer.state != CDC_PRODUCER_STATE_WAIT)
	{
	  cdc_pause_producer ();
	}

      cdc_set_extraction_lsa (&start_lsa);

      cdc_reinitialize_queue (&start_lsa);

      cdc_wakeup_producer ();

      ptr = or_pack_int (reply, error_code);
      ptr = or_pack_log_lsa (ptr, &start_lsa);
      or_pack_int64 (ptr, input_time);

      cdc_log ("%s : reply contains start lsa (%lld|%d), output time (%lld)", __func__, LSA_AS_ARGS (&start_lsa),
	       input_time);

      (void) css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));

      return;
    }
  else
    {
      goto error;
    }

error:

  or_pack_int (reply, error_code);

  (void) css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

void
scdc_get_loginfo_metadata (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  OR_ALIGNED_BUF (OR_INT_SIZE * 3 + OR_LOG_LSA_ALIGNED_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *ptr;
  LOG_LSA start_lsa;
  LOG_LSA next_lsa;

  int total_length;
  char *log_info_list;
  int error_code = NO_ERROR;
  int num_log_info;

  int rc;

  or_unpack_log_lsa (request, &start_lsa);

  cdc_log ("%s : request from client contains start_lsa (%lld|%d)", __func__, LSA_AS_ARGS (&start_lsa));

  if (LSA_ISNULL (&cdc_Gl.consumer.next_lsa))
    {
      /* if server is restarted while cdc is running, and client immediately calls extraction without scdc_find_lsa() */

      error_code = cdc_validate_lsa (thread_p, &start_lsa);
      if (error_code != NO_ERROR)
	{
	  goto error;
	}

      cdc_set_extraction_lsa (&start_lsa);

      cdc_reinitialize_queue (&start_lsa);

      cdc_wakeup_producer ();
    }

  if (LSA_EQ (&cdc_Gl.consumer.next_lsa, &start_lsa))
    {
      error_code = cdc_make_loginfo (thread_p, &start_lsa);
      if (error_code != NO_ERROR)
	{
	  goto error;
	}

      error_code = cdc_get_loginfo_metadata (&next_lsa, &total_length, &num_log_info);
      if (error_code != NO_ERROR)
	{
	  goto error;
	}
    }
  else if (LSA_EQ (&cdc_Gl.consumer.start_lsa, &start_lsa))
    {
      /* Send again; only the case, where cdc client re-request loginfo metadata requested last time due to a problem like shutdown, will be dealt. */
      error_code = cdc_get_loginfo_metadata (&next_lsa, &total_length, &num_log_info);
      if (error_code != NO_ERROR)
	{
	  goto error;
	}
    }
  else
    {
      _er_log_debug (ARG_FILE_LINE,
		     "requested extract lsa is (%lld | %d) is not in a range of previously requested lsa (%lld | %d) and next lsa to extract (%lld | %d)",
		     LSA_AS_ARGS (&start_lsa), LSA_AS_ARGS (&cdc_Gl.consumer.start_lsa),
		     LSA_AS_ARGS (&cdc_Gl.consumer.next_lsa));

      er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_CDC_INVALID_LOG_LSA, 1, LSA_AS_ARGS (&start_lsa));
      error_code = ER_CDC_INVALID_LOG_LSA;
      goto error;
    }

  ptr = or_pack_int (reply, error_code);

  ptr = or_pack_log_lsa (ptr, &next_lsa);
  ptr = or_pack_int (ptr, num_log_info);
  ptr = or_pack_int (ptr, total_length);

  cdc_log ("%s : reply contains next lsa (%lld|%d), num log info (%d), total length (%d)", __func__,
	   LSA_AS_ARGS (&next_lsa), num_log_info, total_length);

  (void) css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
  return;

error:

  or_pack_int (reply, error_code);

  (void) css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

void
scdc_get_loginfo (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  cdc_log ("%s : size of log info is %d", __func__, cdc_Gl.consumer.log_info_size);

  (void) css_send_data_to_client (thread_p->conn_entry, rid, cdc_Gl.consumer.log_info, cdc_Gl.consumer.log_info_size);

  return;
}

void
scdc_end_session (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  int error_code;

  error_code = cdc_cleanup ();

  cdc_log ("%s : clean up for cdc thread has done.", __func__);

  cdc_Gl.conn.fd = -1;
  cdc_Gl.conn.status = CONN_CLOSED;

  or_pack_int (reply, error_code);
  (void) css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));

  return;
}

void
sflashback_get_summary (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  OR_ALIGNED_BUF (OR_INT_SIZE + OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *area = NULL;
  int area_size = 0;

  int error_code = NO_ERROR;
  char *ptr;
  char *start_ptr;

  char *num_ptr;		//pointer in which 'number of summary' is located
  int tmp_num = 0;

  LC_FIND_CLASSNAME status;

  FLASHBACK_SUMMARY_CONTEXT context = { LSA_INITIALIZER, LSA_INITIALIZER, NULL, 0, 0, };

  time_t start_time = 0;
  time_t end_time = 0;

  char *classname = NULL;

  error_code = flashback_initialize (thread_p);
  if (error_code != NO_ERROR)
    {
      goto error;
    }

  ptr = or_unpack_int (request, &context.num_class);

  for (int i = 0; i < context.num_class; i++)
    {
      OID classoid = OID_INITIALIZER;

      ptr = or_unpack_string_nocopy (ptr, &classname);

      status = xlocator_find_class_oid (thread_p, classname, &classoid, NULL_LOCK);
      if (status != LC_CLASSNAME_EXIST)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FLASHBACK_INVALID_CLASS, 1, classname);
	  error_code = ER_FLASHBACK_INVALID_CLASS;
	  goto error;
	}

      context.classoids.emplace_back (classoid);
    }

  ptr = or_unpack_string_nocopy (ptr, &context.user);
  ptr = or_unpack_int64 (ptr, &start_time);
  ptr = or_unpack_int64 (ptr, &end_time);

  error_code = flashback_verify_time (thread_p, &start_time, &end_time, &context.start_lsa, &context.end_lsa);
  if (error_code != NO_ERROR)
    {
      goto error;
    }

  assert (!LSA_ISNULL (&context.start_lsa));

  flashback_set_min_log_pageid_to_keep (&context.start_lsa);

  /* get summary list */
  error_code = flashback_make_summary_list (thread_p, &context);
  if (error_code != NO_ERROR)
    {
      goto error;
    }

  /* area : | class oid list | summary start time | summary end time | num summary | summary entry list |
   * summary entry : | trid | user | start/end time | num insert/update/delete | num class | class oid list |
   * OR_OID_SIZE * context.num_class means maximum class oid list size per summary entry */

  area_size = OR_OID_SIZE * context.num_class + OR_INT64_SIZE + OR_INT64_SIZE + OR_INT_SIZE
    + (OR_SUMMARY_ENTRY_SIZE_WITHOUT_CLASS + OR_OID_SIZE * context.num_class) * context.num_summary;

  area = (char *) db_private_alloc (thread_p, area_size);
  if (area == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, area_size);
      error_code = ER_OUT_OF_VIRTUAL_MEMORY;
      goto error;
    }

  /* reply packing : error_code | area size */
  ptr = or_pack_int (reply, area_size);
  ptr = or_pack_int (ptr, error_code);

  /* area packing : OID list | num summary | summary info list */
  ptr = area;

  // *INDENT-OFF*
  for (auto iter : context.classoids)
    {
      ptr = or_pack_oid (ptr, &iter);
    }
  // *INDENT-ON*

  ptr = or_pack_int64 (ptr, start_time);
  ptr = or_pack_int64 (ptr, end_time);

  num_ptr = ptr;

  ptr = or_pack_int (ptr, context.num_summary);
  ptr = flashback_pack_summary_entry (ptr, context, &tmp_num);

  if (context.num_summary != tmp_num)
    {
      or_pack_int (num_ptr, tmp_num);
    }

  error_code =
    css_send_reply_and_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply), area,
				       area_size);

  db_private_free_and_init (thread_p, area);

  if (error_code != NO_ERROR)
    {
      goto css_send_error;
    }

  return;
error:

  if (error_code == ER_FLASHBACK_INVALID_CLASS)
    {
      ptr = or_pack_int (reply, strlen (classname));
      or_pack_int (ptr, error_code);

      css_send_reply_and_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply), classname,
					 strlen (classname));
    }
  else if (error_code == ER_FLASHBACK_INVALID_TIME)
    {
      OR_ALIGNED_BUF (OR_INT64_SIZE) area_buf;
      area = OR_ALIGNED_BUF_START (area_buf);

      ptr = or_pack_int (reply, OR_ALIGNED_BUF_SIZE (area_buf));
      or_pack_int (ptr, error_code);

      or_pack_int64 (area, log_Gl.hdr.db_creation);
      css_send_reply_and_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply), area,
					 OR_ALIGNED_BUF_SIZE (area_buf));
    }
  else
    {
      ptr = or_pack_int (reply, 0);
      or_pack_int (ptr, error_code);

      (void) css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
    }

  if (error_code != ER_FLASHBACK_DUPLICATED_REQUEST)
    {
      /* if flashback variables are reset by duplicated request error,
       * variables for existing connection (valid connection) can be reset */
      flashback_reset ();
    }

  return;

css_send_error:
  flashback_reset ();

  return;
}

void
sflashback_get_loginfo (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  OR_ALIGNED_BUF (OR_INT_SIZE + OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *area = NULL;
  int area_size = 0;

  int error_code = NO_ERROR;
  char *ptr;
  char *start_ptr;

  int threshold_to_remove_archive = 0;

  FLASHBACK_LOGINFO_CONTEXT context = { -1, NULL, LSA_INITIALIZER, LSA_INITIALIZER, 0, 0, false, 0, OID_INITIALIZER, };

  /* request : trid | user | num_class | table oid list | start_lsa | end_lsa | num_item | forward/backward */

  ptr = or_unpack_int (request, &context.trid);
  ptr = or_unpack_string_nocopy (ptr, &context.user);
  ptr = or_unpack_int (ptr, &context.num_class);

  for (int i = 0; i < context.num_class; i++)
    {
      OID classoid;
      ptr = or_unpack_oid (ptr, &classoid);
      context.classoid_set.emplace (classoid);
    }

  ptr = or_unpack_log_lsa (ptr, &context.start_lsa);
  ptr = or_unpack_log_lsa (ptr, &context.end_lsa);
  ptr = or_unpack_int (ptr, &context.num_loginfo);
  ptr = or_unpack_int (ptr, &context.forward);

  error_code = flashback_make_loginfo (thread_p, &context);
  if (error_code != NO_ERROR)
    {
      goto error;
    }

  area_size = OR_LOG_LSA_ALIGNED_SIZE * 2 + OR_INT_SIZE;

  /* log info entries are chunks of memory that already packed together, and they need to be aligned  
   * | lsa | lsa | num item | align | log info 1 | align | log info 2 | align | log info 3 | ..
   * */

  area_size += context.queue_size + context.num_loginfo * MAX_ALIGNMENT;

  area = (char *) db_private_alloc (thread_p, area_size);
  if (area == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, area_size);
      error_code = ER_OUT_OF_VIRTUAL_MEMORY;
      goto error;
    }

  /* reply packing :  area size | error_code */
  ptr = or_pack_int (reply, area_size);
  or_pack_int (ptr, error_code);

  /* area packing : start lsa | end lsa | num item | item list */
  ptr = or_pack_log_lsa (area, &context.start_lsa);
  ptr = or_pack_log_lsa (ptr, &context.end_lsa);
  ptr = or_pack_int (ptr, context.num_loginfo);

  ptr = flashback_pack_loginfo (thread_p, ptr, context);

  error_code =
    css_send_reply_and_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply), area,
				       area_size);

  db_private_free_and_init (thread_p, area);

  if (error_code != NO_ERROR)
    {
      goto css_send_error;
    }

  if (flashback_is_loginfo_generation_finished (&context.start_lsa, &context.end_lsa))
    {
      flashback_reset ();
    }
  else
    {
      if (context.forward)
	{
	  /* start_lsa is increased only if direction is forward */
	  flashback_set_min_log_pageid_to_keep (&context.start_lsa);
	}
    }

  return;
error:

  if (error_code == ER_FLASHBACK_SCHEMA_CHANGED)
    {
      OR_ALIGNED_BUF (OR_OID_SIZE) area_buf;
      area = OR_ALIGNED_BUF_START (area_buf);

      ptr = or_pack_int (reply, OR_ALIGNED_BUF_SIZE (area_buf));
      or_pack_int (ptr, error_code);
      or_pack_oid (area, &context.invalid_class);
      css_send_reply_and_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply), area,
					 OR_ALIGNED_BUF_SIZE (area_buf));
    }
  else
    {
      ptr = or_pack_int (reply, 0);
      or_pack_int (ptr, error_code);
      (void) css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
    }

  flashback_reset ();

  return;
css_send_error:

  flashback_reset ();
  return;
}

/*
 * smmon_get_server_info - get memory usage info from memory monitor
 *
 * return:
 *
 *  rid(in):
 *  request(in):
 *  reqlen(in):
 */
void
smmon_get_server_info (THREAD_ENTRY * thread_p, unsigned int rid, char *request, int reqlen)
{
  char *buffer_a = NULL, *buffer, *ptr;
  int size = 0;
  OR_ALIGNED_BUF (OR_INT_SIZE + OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  int error = NO_ERROR;
#if !defined(WINDOWS)
  MMON_SERVER_INFO server_info;

  mmon_aggregate_server_info (server_info);

  // Size of server name
  size += or_packed_string_length (server_info.server_name, NULL);
  size = size % MAX_ALIGNMENT ? size + INT_ALIGNMENT : size;

  // Size of total_mem_usage
  size += OR_INT64_SIZE;

  // Size of total_metainfo_mem_usage
  size += OR_INT64_SIZE;

  // Size of num_stat
  size += OR_INT_SIZE;

  // Size of stat name and memory usage
  // *INDENT-OFF*
  for (const auto &s_info : server_info.stat_info)
    {
      // Size of filename
      size += or_packed_string_length (s_info.first.c_str (), NULL);
      size = size % MAX_ALIGNMENT ? size + INT_ALIGNMENT : size;

      // Size of memory usage
      size += OR_INT64_SIZE;
    }
  // *INDENT-ON*

  buffer_a = (char *) db_private_alloc (thread_p, size + MAX_ALIGNMENT);
  if (buffer_a == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, size);
      error = ER_OUT_OF_VIRTUAL_MEMORY;
    }
  else
    {
      buffer = PTR_ALIGN (buffer_a, MAX_ALIGNMENT);

      ptr = buffer;

      ptr = or_pack_string (ptr, server_info.server_name);
      ptr = or_pack_int64 (ptr, server_info.total_mem_usage);
      ptr = or_pack_int64 (ptr, server_info.total_metainfo_mem_usage);
      ptr = or_pack_int (ptr, server_info.num_stat);

      // *INDENT-OFF*
      for (const auto &s_info : server_info.stat_info)
        {
          ptr = or_pack_string (ptr, s_info.first.c_str ());
          ptr = or_pack_int64 (ptr, s_info.second);
        }
      // *INDENT-ON*
      assert (size == (int) (ptr - buffer));
    }

  if (error != NO_ERROR)
    {
      ptr = or_pack_int (reply, 0);
      ptr = or_pack_int (ptr, error);
      css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
    }
  else
    {
      ptr = or_pack_int (reply, size);
      ptr = or_pack_int (ptr, error);
      css_send_reply_and_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply), buffer, size);
    }
  db_private_free_and_init (thread_p, buffer_a);
#else // WINDOWS
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INTERFACE_NOT_SUPPORTED_OPERATION, 0);
  error = ER_INTERFACE_NOT_SUPPORTED_OPERATION;

  // send error
  ptr = or_pack_int (reply, 0);
  ptr = or_pack_int (ptr, error);
  css_send_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
#endif // !WINDOWS
}
