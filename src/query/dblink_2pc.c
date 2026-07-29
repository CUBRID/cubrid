/*
 *
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

#ident "$Id$"

// dblink connection handling for distributed transaction
#include "connection_defs.h"
#include "thread_manager.hpp"
#include "query_manager.h"
#include "dblink_scan.h"
#include "dblink_2pc.h"

#ifndef DBDEF_HEADER_
#define DBDEF_HEADER_
#endif

#include <cas_cci.h>
#include <cci_xa.h>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

int
dblink_2pc_get_participants (THREAD_ENTRY * thread_p, int *partid_len, void **block_particps_ids)
{
  int num_ids = 0, id_size = sizeof (DBLINK_CONN_INFO);
  char *ids;
  bool is_autocommit;

  DBLINK_CONN_ENTRY *dblink_conn = qmgr_dblink_get_conn_entry (thread_p, &is_autocommit);
  DBLINK_CONN_ENTRY *dblink = dblink_conn;

  if (is_autocommit)
    {
      return 0;
    }

  while (dblink)
    {
      if (dblink->is_2pc_participant)
	{
	  num_ids++;
	}

      dblink = dblink->next;
    }

  *block_particps_ids = NULL;

  if (num_ids > 0)
    {
      int nth = 0;

      ids = (char *) calloc (num_ids, id_size);
      if (ids == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, num_ids * id_size);
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}

      dblink = dblink_conn;
      while (dblink)
	{
	  if (dblink->is_2pc_participant)
	    {
	      memcpy (ids + (nth++) * id_size, &(dblink->conn_info), id_size);
	    }
	  dblink = dblink->next;
	}

      *block_particps_ids = (void *) ids;
    }

  *partid_len = id_size;

  return num_ids;
}

#ifdef CCI_XA
bool
dblink_2pc_send_prepare (THREAD_ENTRY * thread_p, int gtrid, int num_particps, void *block_particps_ids)
{
  int i;
  XID xid;
  T_CCI_ERROR err_buf;
  DBLINK_CONN_INFO *dblink;

  xid.formatID = MAJOR_VERSION * 100 + MINOR_VERSION;
  xid.gtrid_length = sizeof (int);
  xid.bqual_length = sizeof (int);

  dblink = (DBLINK_CONN_INFO *) block_particps_ids;
  for (i = 0; i < num_particps; i++)
    {
      memcpy (xid.data, &gtrid, xid.gtrid_length);
      memcpy (xid.data + xid.gtrid_length, &(dblink[i].conn_handle), xid.bqual_length);

      if (cci_xa_prepare (dblink[i].conn_handle, &xid, &err_buf) != NO_ERROR)
	{
	  /* XA prepare failed; abort and disconnect all remaining entries: this failed
	   * participant, any not-yet-attempted participants, and SELECT-only non-participants.
	   * The daemon delivers the abort decision to already-prepared participants via fresh
	   * connections using block_particps_ids, so ending existing handles here is safe. */
	  qmgr_dblink_clear_conn_entry (thread_p, false);
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_DBLINK_TRAN, 1, err_buf.err_msg);
	  return false;
	}

      /* The participant is now XA-prepared; its branch is persisted on the participant and survives a
       * disconnect. Ownership passes to the 2PC daemon (which reconnects via the gateway URL to send the
       * decision), so drop the entry from this transaction's dblink list and release the local socket.
       * The decision is driven by the block_particps_ids copy, not by this list. */
      (void) cci_disconnect (dblink[i].conn_handle, &err_buf);
      (void) qmgr_dblink_remove_conn_entry (thread_p, dblink[i].conn_handle);
    }

  /* All participants XA-prepared and removed from dblink_entry.  Commit and disconnect any
   * remaining non-participant (SELECT-only) entries and reset is_dblink_autocommit.
   * The CCI_XA FULL/PREPARE commit path skips phase 2, so this is the only cleanup point.
   * If SELECT-only remote commit fails, force abort: _db_global_tran has not yet been
   * updated and participants can still be rolled back. */
  if (qmgr_dblink_clear_conn_entry (thread_p, true) != NO_ERROR)
    {
      return false;
    }
  return true;
}

void
dblink_2pc_end_tran (THREAD_ENTRY * thread_p, int gtrid, int num_particps, bool is_commit, void *block_particps_ids)
{
  int i;
  DBLINK_CONN_INFO *dblink = (DBLINK_CONN_INFO *) block_particps_ids;

  for (i = 0; i < num_particps; i++)
    {
#if defined(SERVER_MODE)
      /* SERVER_MODE: dblink_2pc_daemon_init() runs before log_recovery() and
       * dblink_2pc_daemon_recovery_with_thread() scans _db_global_tran right after
       * log_2pc_recovery() completes, so any participant that fails here will be
       * picked up and retried by the daemon.  Blocking the recovery thread on a
       * single gateway failure is not acceptable. */
      (void) dblink_2pc_send_decision_one_participant (gtrid, &dblink[i], is_commit);
#else
      /* SA_MODE: no daemon, and dblink_2pc_daemon_recovery_with_thread() is
       * SERVER_MODE-only, so there is no automatic retry after this call.
       * TODO: add an SA_MODE _db_global_tran scan in log_recovery() (mirroring
       * dblink_2pc_daemon_recovery_with_thread) so this path can also be
       * fire-and-forget instead of blocking indefinitely on a downed participant. */
      while (dblink_2pc_send_decision_one_participant (gtrid, &dblink[i], is_commit) != NO_ERROR)
	{
	  thread_sleep (1000);	/* wait 1 second before retrying */
	}
#endif
    }

  /* Commit/abort and disconnect any remaining non-participant (SELECT-only) entries.
   * The decision has already been delivered to participants; cleanup errors are
   * logged inside dblink_end_tran but cannot change the outcome here. */
  (void) qmgr_dblink_clear_conn_entry (thread_p, is_commit);
}

void
dblink_2pc_dump_participants (FILE * fp, int block_length, void *block_particps_ids)
{
  int i, participant_num = block_length / sizeof (DBLINK_CONN_INFO);
  DBLINK_CONN_INFO *dblink = (DBLINK_CONN_INFO *) block_particps_ids;

  assert (participant_num > 0);

  for (i = 0; i < participant_num; i++)
    {
      fprintf (fp, "  CONN-HANDLE = %d, CONN-URL = %s, USER = %s\n", dblink[i].conn_handle, dblink[i].conn_url,
	       dblink[i].user_name);
    }
}

/*
 * dblink_2pc_send_decision_one_participant - For coordinator recovery: send commit/abort to one participant.
 *   Reconnects using conn_url, user_name, password and sends XA end_tran with (gtrid, bqual).
 *   Returns NO_ERROR on success, ER_* on failure.
 */
int
dblink_2pc_send_decision_one_participant (int gtrid, DBLINK_CONN_INFO * participant, bool is_commit)
{
  int err, bqual, conn_handle, len;
  XID xid;
  T_CCI_ERROR err_buf;
  char type = is_commit ? CCI_TRAN_COMMIT : CCI_TRAN_ROLLBACK;
  char conn_url_gateway[MAX_LEN_CONNECTION_URL + 16];

  char *conn_url = participant->conn_url;
  char *user_name = participant->user_name;
  char *password = participant->password;

  bqual = participant->conn_handle;

  if (conn_url == NULL || user_name == NULL || password == NULL)
    {
      return ER_FAILED;
    }

  /* The original conn_handle is always stale at this point: the normal path disconnects it right after
   * XA prepare (dblink_2pc_send_prepare), and the recovery path only has the bqual value read from the
   * catalog (the connection of the now-dead process no longer exists). So conn_handle is used only as
   * the bqual part of the xid, and the decision is always delivered over a fresh gateway connection.
   * This avoids a guaranteed-futile cci_xa_end_tran() on a closed handle and the risk of sending the
   * decision to a recycled handle that points at an unrelated connection. */
  xid.formatID = MAJOR_VERSION * 100 + MINOR_VERSION;
  xid.gtrid_length = sizeof (int);
  xid.bqual_length = sizeof (int);
  memcpy (xid.data, &gtrid, xid.gtrid_length);
  memcpy (xid.data + xid.gtrid_length, &bqual, xid.bqual_length);

  /* connect to the participant through the gateway to send the decision */
  if (strstr (conn_url, ":?"))
    {
      len = snprintf (conn_url_gateway, sizeof (conn_url_gateway), "%s%s", conn_url, "&__gateway=true");
    }
  else
    {
      len = snprintf (conn_url_gateway, sizeof (conn_url_gateway), "%s%s", conn_url, "?__gateway=true");
    }
  if (len < 0 || len >= (int) sizeof (conn_url_gateway))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_DBLINK, 1, "connection url too long");
      return ER_DBLINK;
    }

  conn_handle = cci_connect_with_url_ex (conn_url_gateway, user_name, password, &err_buf);
  if (conn_handle < 0)
    {
      return ER_DBLINK;
    }

  err = cci_xa_end_tran (conn_handle, &xid, type, &err_buf);
  (void) cci_disconnect (conn_handle, &err_buf);

  if (err == CCI_ER_NO_ERROR)
    {
      return NO_ERROR;
    }

  /* An unknown global transaction id means the participant no longer has this branch, so the decision
   * is already complete and re-sending it is idempotent (required for crash recovery, e.g. the P5 path
   * where a crash happens after the decision is sent but before the _db_global_tran row is deleted):
   *   - ABORT: the branch was never prepared or was already rolled back.
   *   - COMMIT: a row only reaches the 'C' state after every participant has successfully prepared, so
   *     a missing branch here means it was already committed (a prepared branch only disappears once it
   *     completes). There is no path that sends COMMIT to a never-prepared branch.
   * In both cases the participant is in (or past) the desired terminal state, so report success and let
   * the caller delete the row instead of retrying forever. */
  if (err_buf.err_code == ER_LOG_2PC_UNKNOWN_GTID)
    {
      return NO_ERROR;
    }

  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_DBLINK, 1, err_buf.err_msg);

  return ER_DBLINK;
}
#endif
