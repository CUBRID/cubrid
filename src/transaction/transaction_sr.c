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
 * transaction_sr.c -
 */

#ident "$Id$"

#include "config.h"

#include <assert.h>

#include "db.h"
#include "transaction_sr.h"
#include "locator_sr.h"
#include "log_manager.h"
#include "log_impl.h"
#include "wait_for_graph.h"
#include "thread.h"
#if defined(SERVER_MODE)
#include "server_support.h"
#endif
#include "xserver_interface.h"
#if defined(ENABLE_SYSTEMTAP)
#include "probes.h"
#endif /* ENABLE_SYSTEMTAP */

/*
 * xtran_server_commit - Commit the current transaction
 *
 * return: state of operation
 *
 *   thrd(in): this thread handle
 *   retain_lock(in): false = release locks (default)
 *                      true  = retain locks
 *
 * NOTE: Commit the current transaction. All transient class name
 *              entries are removed, and the commit is forwarded to the log
 *              and recovery manager. The log manager declares all changes of
 *              the transaction as permanent and releases all locks acquired
 *              by the transaction. The return value may indicate that the
 *              transaction has not been committed completely when there are
 *              some loose_end postpone actions to be done in the client
 *              machine. In this case the client transaction manager must
 *              obtain and execute these actions.
 *
 *       This function should be called after all objects that have
 *              been updated by the transaction are flushed from the workspace
 *              (client) to the page buffer pool (server).
 */
TRAN_STATE
xtran_server_commit (THREAD_ENTRY * thread_p, bool retain_lock)
{
  TRAN_STATE state;
  int tran_index;

  /* 
   * Execute some few remaining actions before the log manager is notified of
   * the commit
   */

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);

  state = log_commit (thread_p, tran_index, retain_lock);

#if defined(ENABLE_SYSTEMTAP)
  if (state == TRAN_UNACTIVE_COMMITTED || state == TRAN_UNACTIVE_COMMITTED_INFORMING_PARTICIPANTS)
    {
      CUBRID_TRAN_COMMIT (tran_index);
    }
#endif /* ENABLE_SYSTEMTAP */

  return state;
}

/*
 * xtran_server_abort - Abort the current transaction
 *
 * return: state of operation
 *
 *   thrd(in): this thread handle
 *
 * NOTE: Abort the current transaction. All transient class name
 *              entries are removed, and the abort operation is forwarded to
 *              to the log/recovery manager. The log manager undoes any
 *              changes made by the transaction and releases all lock acquired
 *              by the transaction. The return value may indicate that the
 *              transaction has not been aborted completely when there are
 *              some loose_end undo actions  to be executed in the client
 *              machine. In this case the client transaction manager must
 *              obtain and execute these actions.
 *       This function should be called after all updated objects in
 *              the workspace are removed.
 */
TRAN_STATE
xtran_server_abort (THREAD_ENTRY * thread_p)
{
  CSS_CONN_ENTRY *conn = NULL;
  TRAN_STATE state;
  int tran_index;
#if defined(SERVER_MODE)
  bool continue_check;
#endif

  /* 
   * Execute some few remaining actions before the log manager is notified of
   * the commit
   */

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);

#if defined(SERVER_MODE)
  conn = thread_p->conn_entry;
  assert (conn);

  if (conn->client_type == BOOT_CLIENT_LOG_PREFETCHER)
    {
      while (conn->prefetcher_thread_count > 0)
	{
	  if (logtb_is_interrupted (thread_p, true, &continue_check) == true)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INTERRUPTED, 0);
	      return ER_INTERRUPTED;
	    }

	  thread_sleep (10);	/* 10 msec */
	}
    }
#endif

  state = log_abort (thread_p, tran_index);

#if defined(ENABLE_SYSTEMTAP)
  CUBRID_TRAN_ABORT (tran_index, state);
#endif /* ENABLE_SYSTEMTAP */

  return state;
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * tran_server_unilaterally_abort - Unilaterally abort the current transaction
 *
 * return: states of transactions
 *
 *   tran_index(in): Transaction index
 *
 * NOTE: The given transaction is unilaterally aborted by the server
 *              Execute tran_server_abort & set an error message
 */
TRAN_STATE
tran_server_unilaterally_abort (THREAD_ENTRY * thread_p, int tran_index)
{
  TRAN_STATE state;
  int save_tran_index;
  char *client_prog_name;	/* Client user name for transaction */
  char *client_user_name;	/* Client user name for transaction */
  char *client_host_name;	/* Client host for transaction */
  int client_pid;		/* Client process identifier for transaction */

  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  save_tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  LOG_SET_CURRENT_TRAN_INDEX (thread_p, tran_index);

  (void) logtb_find_client_name_host_pid (tran_index, &client_prog_name, &client_user_name, &client_host_name,
					  &client_pid);
  state = xtran_server_abort (thread_p);

  /* free any drivers used by this transaction */
  LOG_SET_CURRENT_TRAN_INDEX (thread_p, save_tran_index);

  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LK_UNILATERALLY_ABORTED, 4, tran_index, client_user_name,
	  client_host_name, client_pid);

  return state;
}
#endif

#if defined(SERVER_MODE)
/*
 * tran_server_unilaterally_abort_tran -
 *
 * return:
 *
 * NOTE:this function is used when pgbuf_fix() results in deadlock.
 * It is used by request handler functions to rollback gracefully,
 */
void
tran_server_unilaterally_abort_tran (THREAD_ENTRY * thread_p)
{
  TRAN_STATE state;
  int tran_index;

  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  state = xtran_server_abort (thread_p);
}
#endif /* SERVER_MODE */

/*
 * xtran_server_start_topop - Start a server macro nested top operation
 *
 * return: NO_ERROR if all OK, ER_ status otherwise
 *
 *   topop_lsa(in/out): Address of top operation for rollback purposes
 *
 */
int
xtran_server_start_topop (THREAD_ENTRY * thread_p, LOG_LSA * topop_lsa)
{
  int error_code = NO_ERROR;

  /* 
   * Execute some few remaining actions before the start top nested action is
   * started by the log manager.
   */

  log_sysop_start (thread_p);
  if (log_get_parent_lsa_system_op (thread_p, topop_lsa) == NULL)
    {
      assert_release (false);
      return ER_FAILED;
    }
  error_code = locator_savepoint_transient_class_name_entries (thread_p, topop_lsa);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      LSA_SET_NULL (topop_lsa);
      return error_code;
    }
  return NO_ERROR;
}

/*
 * xtran_server_end_topop - End a macro nested top operation
 *
 * return: states of transactions
 *
 *   result(in): Result of the nested top action
 *   topop_lsa(in): Address where the top operation for rollback purposes
 *                  started.
 *
 * NOTE:Finish the latest nested top macro operation by either
 *              aborting or attaching to outer parent.
 *
 *      Note that a top operation is not associated with the current
 *              transaction, thus, it can be aborted
 *              independently of the transaction.
 */
TRAN_STATE
xtran_server_end_topop (THREAD_ENTRY * thread_p, LOG_RESULT_TOPOP result, LOG_LSA * topop_lsa)
{
  TRAN_STATE state;
  bool drop_transient_class = false;
  LOG_TDES *tdes = LOG_FIND_CURRENT_TDES (thread_p);

  assert (result == LOG_RESULT_TOPOP_ABORT || result == LOG_RESULT_TOPOP_ATTACH_TO_OUTER);

  /* 
   * Execute some few remaining actions before the start top nested action is
   * started by the log manager.
   */

  if (tdes == NULL)
    {
      int tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_UNKNOWN_TRANINDEX, 1, tran_index);
      return TRAN_UNACTIVE_UNKNOWN;
    }

  switch (result)
    {
    case LOG_RESULT_TOPOP_COMMIT:
    case LOG_RESULT_TOPOP_ABORT:
      if (log_get_parent_lsa_system_op (thread_p, topop_lsa) == topop_lsa)
	{
	  drop_transient_class = true;
	}
      if (result == LOG_RESULT_TOPOP_COMMIT)
	{
	  log_sysop_commit (thread_p);
	  state = TRAN_UNACTIVE_COMMITTED;
	}
      else
	{
	  log_sysop_abort (thread_p);
	  state = TRAN_UNACTIVE_ABORTED;
	}
      if (drop_transient_class)
	{
	  (void) locator_drop_transient_class_name_entries (thread_p, topop_lsa);
	}
      if (result == LOG_RESULT_TOPOP_ABORT)
	{
	  log_clear_lob_locator_list (thread_p, tdes, false, topop_lsa);
	}
      break;

    case LOG_RESULT_TOPOP_ATTACH_TO_OUTER:
    default:
      log_sysop_attach_to_outer (thread_p);
      state = tdes->state;
      break;
    }
  return state;
}

/*
 * xtran_server_savepoint - Declare a user savepoint
 *
 * return: NO_ERROR if all OK, ER_ status otherwise
 *
 *   savept_name(in): Name of the savepoint
 *   savept_lsa(in): Address of save point operation.
 *
 * NOTE: A savepoint is established for the current transaction so
 *              that future transaction actions can be rolled back to this
 *              established savepoint. We call this operation a partial abort
 *              (rollback). That is, all database actions affected by the
 *              transaction after the savepoint are "undone", and all effects
 *              of the transaction preceding the savepoint remain. The
 *              transaction can then continue executing other database
 *              statements. It is permissible to abort to the same savepoint
 *              repeatedly within the same transaction.
 *              If the same savepoint name is used in multiple savepoint
 *              declarations within the same transaction, then only the latest
 *              savepoint with that name is available for aborts and the
 *              others are forgotten.
 *              There are no limits on the number of savepoints that a
 *              transaction can have.
 */
int
xtran_server_savepoint (THREAD_ENTRY * thread_p, const char *savept_name, LOG_LSA * savept_lsa)
{
  LOG_LSA *lsa;
  int error_code = NO_ERROR;

  /* 
   * Execute some few remaining actions before the start top nested action is
   * started by the log manager.
   */

  lsa = log_append_savepoint (thread_p, savept_name);
  if (lsa == NULL)
    {
      LSA_SET_NULL (savept_lsa);
      error_code = ER_FAILED;
    }
  else
    {
      LSA_COPY (savept_lsa, lsa);
      error_code = locator_savepoint_transient_class_name_entries (thread_p, lsa);
      if (error_code != NO_ERROR)
	{
	  LSA_SET_NULL (savept_lsa);
	}
    }

  return error_code;
}

/*
 * xtran_server_partial_abort -Abort operations of a transaction up to a savepoint
 *
 * return: state of partial aborted operation (i.e., notify if
 *              there are client actions that need to be undone).
 *
 *   savept_name(in): Name of the savepoint
 *   savept_lsa(in/out): Address of save point operation.
 *
 * Note: All the effects of the current transaction after the
 *              given savepoint are undone and all the effects of the transaction
 *              preceding the given savepoint remain. After the partial abort
 *              the transaction can continue its normal execution as if
 *              the statements that were undone, were never executed.
 *              The return value may indicate that there are some client
 *              loose_end undo actions to be performed at the client machine.
 *              In this case the transaction manager must obtain and execute
 *              these actions at the client.
 */
TRAN_STATE
xtran_server_partial_abort (THREAD_ENTRY * thread_p, const char *savept_name, LOG_LSA * savept_lsa)
{
  TRAN_STATE state;

  state = log_abort_partial (thread_p, savept_name, savept_lsa);

  return state;
}

/*
 * xtran_server_set_global_tran_info - Set global transaction information
 *
 * return: NO_ERROR if all OK, ER_ status otherwise
 *
 *   gtrid(in): global transaction identifier
 *   info(in): pointer to the user information to be set
 *   size(in): size of the user information to be set
 *
 * NOTE: Set the user information related with the global transaction.
 *              The global transaction identified by the 'gtrid' should exist
 *              and should be the value returned by 'db_2pc_start_transaction'
 *              You can use this function to set the longer format of global
 *              transaction identifier such as XID of XA interface.
 */
int
xtran_server_set_global_tran_info (THREAD_ENTRY * thread_p, int gtrid, void *info, int size)
{
  return log_set_global_tran_info (thread_p, gtrid, info, size);
}

/*
 * xtran_server_get_global_tran_info - Get global transaction information
 *
 * return: NO_ERROR if all OK, ER_ status otherwise
 *
 *   gtrid(in): global transaction identifier
 *   buffer(in):pointer to the buffer into which the user information is stored
 *   size(in): size of the buffer
 *
 * NOTE: Get the user information of the global transaction identified
 *              by the 'gtrid'.
 *              You can use this function to get the longer format of global
 *              transaction identifier such as XID of XA interface. This
 *              function is designed to use if you want to get XID after
 *              calling 'db_2pc_prepared_transactions' to support xa_recover()
 */
int
xtran_server_get_global_tran_info (THREAD_ENTRY * thread_p, int gtrid, void *buffer, int size)
{
  return log_get_global_tran_info (thread_p, gtrid, buffer, size);
}

/*
 * xtran_server_2pc_start - Start transaction as a part of global transaction
 *
 * return: return global transaction identifier
 *
 * Note: Make current transaction as a part of a global transaction by
 *              assigning a global transaction identifier(gtrid).
 *              It is recommended to call this function just after the end of
 *              a transaction(commit or abort) before executing other works.
 *              This function is one way of getting gtrid of the transaction.
 *              The other way is to use 'db_2pc_prepare_to_commit_transaction'
 *              The function 'db_2pc_prepare_transaction' should be used if
 *              this function is called.
 */
int
xtran_server_2pc_start (THREAD_ENTRY * thread_p)
{
  return log_2pc_start (thread_p);
}

/*
 * xtran_server_2pc_prepare - Prepare transaction to commit
 *
 * return: TRAN_STATE
 *
 * Note: Prepare the current transaction for commitment in 2PC. The
 *              transaction should be made as a part of a global transaction
 *              before by 'db_2pc_start_transaction', a pair one of this
 *              function.
 *              The system promises not to unilaterally abort the transaction.
 *              After this function call, the only API functions that should
 *              be executed are 'db_commit_transaction' &
 *              'db_abort_transaction'.
 */
TRAN_STATE
xtran_server_2pc_prepare (THREAD_ENTRY * thread_p)
{
  /* Make the transient classname entries permanent; If transaction aborted after this point, these operations will
   * also be undone, just like previous operations of the transaction.  */
  (void) locator_drop_transient_class_name_entries (thread_p, NULL);

  return log_2pc_prepare (thread_p);
}

/*
 * xtran_server_2pc_recovery_prepared - Obtain list of prepared transactions
 *
 * return: the number of ids copied into 'gtrids[]'
 *
 *   gtrids(in): array into which global transaction identifiers are copied
 *   size(in): size of 'gtrids[]' array
 *
 * Note: For restart recovery of global transactions, this function
 *              returns gtrids of transactions in prepared state, which was
 *              a part of a global transaction.
 *              If the return value is less than the 'size', there's no more
 *              transactions to recover.
 */
int
xtran_server_2pc_recovery_prepared (THREAD_ENTRY * thread_p, int gtrids[], int size)
{
  return log_2pc_recovery_prepared (thread_p, gtrids, size);
}

/*
 * xtran_server_2pc_attach_global_tran - Attach to a loose end 2pc transaction
 *
 * return: New transaction index
 *
 *   gtrid(in): Global transaction identifier
 *
 * Note:The current client index is attached to the given 2PC loose
 *              end (i.e., transaction wiating for decision) transaction with
 *              the given global transaction identifier.
 *              The old client transaction is aborted before the attachement,
 *              the old client transaction must not be in the middle of a 2PC.
 *              It is recommended to attach a client to a 2PC loose end
 *              transaction just after the client restart or after a commit
 *              or abort.
 */
int
xtran_server_2pc_attach_global_tran (THREAD_ENTRY * thread_p, int gtrid)
{
  return log_2pc_attach_global_tran (thread_p, gtrid);
}

/*
 * xtran_server_2pc_prepare_global_tran - Prepare the transaction to commit
 *
 * return: TRAN_STATE
 *
 *   global_tranid(in): Identifier of the global transaction
 *
 * Note:This function prepares the transaction identified by "gtrid"
 *              for commitment. Any objects and data that the transaction held
 *              or modified are placed in a state that can be guarantee the
 *              the commintment of the transaction by coordinator request
 *              regardless of failures. The shared type locks (IS, S) acquired
 *              by the transaction are released (SIX is demoted to IX lock)
 *              and the exclusive type locks (IX, X) acquired by the
 *              transaction are saved in the log as part of the prepare to
 *              commit log record. This is needed since, we must guarantee the
 *              consistency of the updated data until the transaction is
 *              either committed or aborted by the coordinator regardless of
 *              failures. If the transaction cannot be committed, it was
 *              previously aborted, and the coordinator is notified of such
 *              state.
 */
TRAN_STATE
xtran_server_2pc_prepare_global_tran (THREAD_ENTRY * thread_p, int global_tranid)
{
  /* Make the transient classname entries permanent; If transaction aborted after this point, these operations will
   * also be undone, just like previous operations of the transaction. */
  (void) locator_drop_transient_class_name_entries (thread_p, NULL);

  return log_2pc_prepare_global_tran (thread_p, global_tranid);
}

/*
 * xtran_is_blocked - Is transaction suspended ?
 *
 * return:
 *
 *   tran_index(in): Transaction index
 *
 * NOTE: Find if current transaction is suspended.
 */
bool
xtran_is_blocked (THREAD_ENTRY * thread_p, int tran_index)
{
  return lock_is_waiting_transaction (tran_index);
}

/*
 * xtran_server_has_updated -  Has transaction updated the database ?
 *
 * return:
 *
 * NOTE: Find if the transaction has dirtied the database. We say that
 *              a transaction has updated the database, if it has log
 *              something and it has a write lock on an object, or if there
 *              has been an update to a remote database.
 */
bool
xtran_server_has_updated (THREAD_ENTRY * thread_p)
{
  return ((logtb_has_updated (thread_p) && lock_has_xlock (thread_p)));
}

/*
 * xtran_wait_server_active_trans -
 *
 * return:
 *
 * NOTE: wait for server threads with current tran index to finish
 */
int
xtran_wait_server_active_trans (THREAD_ENTRY * thread_p)
{
#if defined(SERVER_MODE)
  int prev_thrd_cnt, thrd_cnt;
  CSS_CONN_ENTRY *p;
  int tran_index, client_id;
  bool continue_check;

  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  p = thread_p->conn_entry;
  if (p == NULL)
    {
      return 0;
    }

  tran_index = thread_p->tran_index;
  client_id = p->client_id;

loop:
  prev_thrd_cnt = thread_has_threads (thread_p, tran_index, client_id);
  if (prev_thrd_cnt > 0)
    {
      if (!logtb_is_interrupted_tran (thread_p, false, &continue_check, tran_index))
	{
	  logtb_set_tran_index_interrupt (thread_p, tran_index, true);
	}
    }

  while ((thrd_cnt = thread_has_threads (thread_p, tran_index, client_id)) >= prev_thrd_cnt && thrd_cnt > 0)
    {
      /* Some threads may wait for data from the m-driver. It's possible from the fact that css_server_thread() is
       * responsible for receiving every data from which is sent by a client and all m-drivers. We must have chance to
       * receive data from them. */
      thread_sleep (10);	/* 10 msec */
    }

  if (thrd_cnt > 0)
    {
      goto loop;
    }

  logtb_set_tran_index_interrupt (thread_p, tran_index, false);

#endif /* SERVER_MODE */
  return 0;
}

/*
 * xtran_server_is_active_and_has_updated - Find if transaction is active and
 *					    has updated the database ?
 * return:
 *
 * NOTE: Find if the transaction is active and has dirtied the
 *              database. We say that a transaction has updated the database,
 *              if it has log something and it has a write lock on an object.
 */
int
xtran_server_is_active_and_has_updated (THREAD_ENTRY * thread_p)
{
  return (logtb_is_current_active (thread_p) && xtran_server_has_updated (thread_p));
}

/*
 * xtran_get_local_transaction_id -
 *
 * return: NO_ERROR if all OK, ER_ status otherwise
 *
 *   trid(in):
 *
 * NOTE:
 */
int
xtran_get_local_transaction_id (THREAD_ENTRY * thread_p, DB_VALUE * trid)
{
  int error_code = NO_ERROR;

  assert (trid != (DB_VALUE *) NULL);

  error_code = db_value_domain_init (trid, DB_TYPE_INTEGER, 0, 0);
  if (error_code == NO_ERROR)
    {
      DB_MAKE_INTEGER (trid, logtb_find_current_tranid (thread_p));
    }

  return error_code;
}

/*
 * xtran_lock_rep_read - lock RR transaction with specified lock
 *
 * return: NO_ERROR if all OK, ER_ status otherwise
 *
 *   thread_p(in):
 *   lock_rr_tran(in):
 *
 * NOTE:
 */
int
xtran_lock_rep_read (THREAD_ENTRY * thread_p, LOCK lock_rr_tran)
{
  return lock_rep_read_tran (thread_p, lock_rr_tran, LK_UNCOND_LOCK);
}
