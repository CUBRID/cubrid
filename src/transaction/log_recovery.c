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
 * log_recovery.c -
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <assert.h>

#include "porting.h"
#include "log_manager.h"
#include "log_impl.h"
#include "log_comm.h"
#include "recovery.h"
#include "boot_sr.h"
#include "locator_sr.h"
#include "disk_manager.h"
#include "page_buffer.h"
#include "file_io.h"
#include "storage_common.h"
#include "error_manager.h"
#include "memory_alloc.h"
#include "system_parameter.h"
#include "message_catalog.h"
#if defined(SERVER_MODE)
#include "connection_error.h"
#include "thread.h"
#endif /* SERVER_MODE */
#include "log_compress.h"
#include "vacuum.h"

static void log_rv_undo_record (THREAD_ENTRY * thread_p, LOG_LSA * log_lsa, LOG_PAGE * log_page_p,
				LOG_RCVINDEX rcvindex, const VPID * rcv_vpid, LOG_RCV * rcv,
				const LOG_LSA * rcv_lsa_ptr, LOG_TDES * tdes, LOG_ZIP * undo_unzip_ptr);
static void log_rv_redo_record (THREAD_ENTRY * thread_p, LOG_LSA * log_lsa, LOG_PAGE * log_page_p,
				int (*redofun) (THREAD_ENTRY * thread_p, LOG_RCV *), LOG_RCV * rcv,
				LOG_LSA * rcv_lsa_ptr, int undo_length, char *undo_data, LOG_ZIP * redo_unzip_ptr);
static bool log_rv_find_checkpoint (THREAD_ENTRY * thread_p, VOLID volid, LOG_LSA * rcv_lsa);
static bool log_rv_get_unzip_log_data (THREAD_ENTRY * thread_p, int length, LOG_LSA * log_lsa, LOG_PAGE * log_page_p,
				       LOG_ZIP * undo_unzip_ptr);
static int log_rv_analysis_undo_redo (THREAD_ENTRY * thread_p, int tran_id, LOG_LSA * log_lsa);
static int log_rv_analysis_dummy_head_postpone (THREAD_ENTRY * thread_p, int tran_id, LOG_LSA * log_lsa);
static int log_rv_analysis_postpone (THREAD_ENTRY * thread_p, int tran_id, LOG_LSA * log_lsa);
static int log_rv_analysis_run_postpone (THREAD_ENTRY * thread_p, int tran_id, LOG_LSA * log_lsa, LOG_PAGE * log_page_p,
					 LOG_LSA * check_point);
static int log_rv_analysis_compensate (THREAD_ENTRY * thread_p, int tran_id, LOG_LSA * log_lsa, LOG_PAGE * log_page_p);
static int log_rv_analysis_will_commit (THREAD_ENTRY * thread_p, int tran_id, LOG_LSA * log_lsa);
static int log_rv_analysis_commit_with_postpone (THREAD_ENTRY * thread_p, int tran_id, LOG_LSA * log_lsa,
						 LOG_PAGE * log_page_p);
static int log_rv_analysis_sysop_start_postpone (THREAD_ENTRY * thread_p, int tran_id, LOG_LSA * log_lsa,
						 LOG_PAGE * log_page_p);
static int log_rv_analysis_complete (THREAD_ENTRY * thread_p, int tran_id, LOG_LSA * log_lsa, LOG_PAGE * log_page_p,
				     LOG_LSA * prev_lsa, bool is_media_crash, time_t * stop_at,
				     bool * did_incom_recovery);
static int log_rv_analysis_sysop_end (THREAD_ENTRY * thread_p, int tran_id, LOG_LSA * log_lsa, LOG_PAGE * log_page_p);
static int log_rv_analysis_start_checkpoint (LOG_LSA * log_lsa, LOG_LSA * start_lsa, bool * may_use_checkpoint);
static int log_rv_analysis_end_checkpoint (THREAD_ENTRY * thread_p, LOG_LSA * log_lsa, LOG_PAGE * log_page_p,
					   LOG_LSA * check_point, LOG_LSA * start_redo_lsa, bool * may_use_checkpoint,
					   bool * may_need_synch_checkpoint_2pc);
static int log_rv_analysis_save_point (THREAD_ENTRY * thread_p, int tran_id, LOG_LSA * log_lsa);
static int log_rv_analysis_2pc_prepare (THREAD_ENTRY * thread_p, int tran_id, LOG_LSA * log_lsa);
static int log_rv_analysis_2pc_start (THREAD_ENTRY * thread_p, int tran_id, LOG_LSA * log_lsa);
static int log_rv_analysis_2pc_commit_decision (THREAD_ENTRY * thread_p, int tran_id, LOG_LSA * log_lsa);
static int log_rv_analysis_2pc_abort_decision (THREAD_ENTRY * thread_p, int tran_id, LOG_LSA * log_lsa);
static int log_rv_analysis_2pc_commit_inform_particps (THREAD_ENTRY * thread_p, int tran_id, LOG_LSA * log_lsa);
static int log_rv_analysis_2pc_abort_inform_particps (THREAD_ENTRY * thread_p, int tran_id, LOG_LSA * log_lsa);
static int log_rv_analysis_2pc_recv_ack (THREAD_ENTRY * thread_p, int tran_id, LOG_LSA * log_lsa);
static int log_rv_analysis_log_end (int tran_id, LOG_LSA * log_lsa);
static void log_rv_analysis_record (THREAD_ENTRY * thread_p, LOG_RECTYPE log_type, int tran_id, LOG_LSA * log_lsa,
				    LOG_PAGE * log_page_p, LOG_LSA * check_point, LOG_LSA * prev_lsa,
				    LOG_LSA * start_lsa, LOG_LSA * start_redo_lsa, bool is_media_crash,
				    time_t * stop_at, bool * did_incom_recovery, bool * may_use_checkpoint,
				    bool * may_need_synch_checkpoint_2pc);
static void log_recovery_analysis (THREAD_ENTRY * thread_p, LOG_LSA * start_lsa, LOG_LSA * start_redolsa,
				   LOG_LSA * end_redo_lsa, int ismedia_crash, time_t * stopat,
				   bool * did_incom_recovery, INT64 * num_redo_log_records);
static void log_recovery_redo (THREAD_ENTRY * thread_p, const LOG_LSA * start_redolsa, const LOG_LSA * end_redo_lsa,
			       time_t * stopat);
static void log_recovery_finish_all_postpone (THREAD_ENTRY * thread_p);
static void log_recovery_undo (THREAD_ENTRY * thread_p);
static void log_recovery_notpartof_archives (THREAD_ENTRY * thread_p, int start_arv_num, const char *info_reason);
static bool log_unformat_ahead_volumes (THREAD_ENTRY * thread_p, VOLID volid, VOLID * start_volid);
static void log_recovery_notpartof_volumes (THREAD_ENTRY * thread_p);
static void log_recovery_resetlog (THREAD_ENTRY * thread_p, LOG_LSA * new_append_lsa, bool is_new_append_page,
				   LOG_LSA * last_lsa);
static int log_recovery_find_first_postpone (THREAD_ENTRY * thread_p, LOG_LSA * ret_lsa, LOG_LSA * start_postpone_lsa,
					     LOG_TDES * tdes);

static int log_rv_record_modify_internal (THREAD_ENTRY * thread_p, LOG_RCV * rcv, bool is_undo);
static int log_rv_undoredo_partial_changes_recursive (THREAD_ENTRY * thread_p, OR_BUF * rcv_buf, RECDES * record,
						      bool is_undo);

STATIC_INLINE PAGE_PTR log_rv_redo_fix_page (THREAD_ENTRY * thread_p, const VPID * vpid_rcv, LOG_RCVINDEX rcvindex)
  __attribute__ ((ALWAYS_INLINE));

/*
 * CRASH RECOVERY PROCESS
 */

/*
 * log_rv_undo_record - EXECUTE AN UNDO RECORD
 *
 * return: nothing
 *
 *   log_lsa(in/out): Log address identifier containing the log record
 *   log_page_p(in/out): Pointer to page where data starts (Set as a side
 *              effect to the page where data ends)
 *   rcvindex(in): Index to recovery functions
 *   rcv_vpid(in): Address of page to recover
 *   rcv(in/out): Recovery structure for recovery function
 *   rcv_undo_lsa(in): Address of the undo record
 *   tdes(in/out): State structure of transaction undoing data
 *   undo_unzip_ptr(in):
 *
 * NOTE:Execute an undo log record during restart recovery undo phase.
 *              A compensating log record for operation page level logging is
 *              written by the current function. For logical level logging,
 *              the undo function is responsible to log a redo record, which
 *              is converted into a compensating record by the log manager.
 *
 *              This function is very similar than log_rollback_rec, however,
 *              page locking is not done.. and the transaction index that is
 *              running is set to the one in the tdes. Probably, this
 *              function should have not been duplicated for the above few
 *              things.
 */
static void
log_rv_undo_record (THREAD_ENTRY * thread_p, LOG_LSA * log_lsa, LOG_PAGE * log_page_p, LOG_RCVINDEX rcvindex,
		    const VPID * rcv_vpid, LOG_RCV * rcv, const LOG_LSA * rcv_undo_lsa, LOG_TDES * tdes,
		    LOG_ZIP * undo_unzip_ptr)
{
  char *area = NULL;
  TRAN_STATE save_state;	/* The current state of the transaction. Must be returned to this state */
  bool is_zip = false;
  int error_code = NO_ERROR;
  int save_thread_type = 0;

  if (thread_p == NULL)
    {
      /* Thread entry info is required. */
      thread_p = thread_get_thread_entry_info ();
    }

  if (LOG_IS_VACUUM_THREAD_TRANID (tdes->trid))
    {
      /* Convert thread to a vacuum worker. */
      VACUUM_CONVERT_THREAD_TO_VACUUM (thread_p, vacuum_rv_get_worker_by_trid (thread_p, tdes->trid), save_thread_type);

      vacuum_er_log (VACUUM_ER_LOG_RECOVERY | VACUUM_ER_LOG_TOPOPS,
		     "VACUUM: Log undo (%lld, %d), rcvindex=%d for tdes: tdes->trid=%d.",
		     (long long int) rcv_undo_lsa->pageid, (int) rcv_undo_lsa->offset, rcvindex, tdes->trid);
    }
  else
    {
      /* Convert thread to active transaction worker. */
      LOG_SET_CURRENT_TRAN_INDEX (thread_p, tdes->tran_index);
    }

  if (MVCCID_IS_VALID (rcv->mvcc_id))
    {
      /* Assign the MVCCID to transaction. */

      assert (LOG_IS_MVCC_OPERATION (rcvindex));

      logtb_rv_assign_mvccid_for_undo_recovery (thread_p, rcv->mvcc_id);
    }

  /* 
   * Fetch the page for physical log records. The page is not locked since the
   * recovery process is the only one running. If the page does not exist
   * anymore or there are problems fetching the page, continue anyhow, so that
   * compensating records are logged.
   */

  if (RCV_IS_LOGICAL_LOG (rcv_vpid, rcvindex))
    {
      rcv->pgptr = NULL;
    }
  else
    {
      rcv->pgptr = pgbuf_fix (thread_p, rcv_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
      if (rcv->pgptr == NULL)
	{
	  assert (false);
	}
    }

  /* GET BEFORE DATA */

  /* 
   * If data is contained in only one buffer, pass pointer directly.
   * Otherwise, allocate a contiguous area, copy the data and pass this area.
   * At the end deallocate the area.
   */

  if (ZIP_CHECK (rcv->length))
    {				/* check compress data */
      rcv->length = (int) GET_ZIP_LEN (rcv->length);	/* convert compress length */
      is_zip = true;
    }

  if (log_lsa->offset + rcv->length < (int) LOGAREA_SIZE)
    {
      rcv->data = (char *) log_page_p->area + log_lsa->offset;
      log_lsa->offset += rcv->length;
    }
  else
    {
      /* Need to copy the data into a contiguous area */
      area = (char *) malloc (rcv->length);
      if (area == NULL)
	{
	  logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_rv_undo_record");
	  goto end;
	}
      /* Copy the data */
      logpb_copy_from_log (thread_p, area, rcv->length, log_lsa, log_page_p);
      rcv->data = area;
    }

  if (is_zip)
    {
      if (log_unzip (undo_unzip_ptr, rcv->length, (char *) rcv->data))
	{
	  rcv->length = (int) undo_unzip_ptr->data_length;
	  rcv->data = (char *) undo_unzip_ptr->log_data;
	}
      else
	{
	  logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_rv_undo_record");
	  goto end;
	}
    }

  /* Now call the UNDO recovery function */
  if (rcv->pgptr != NULL || RCV_IS_LOGICAL_LOG (rcv_vpid, rcvindex))
    {
      /* 
       * Write a compensating log record for operation page level logging.
       * For logical level logging, the recovery undo function must log an
       * redo/CLR log to describe the undo.
       */

      if (rcvindex == RVBT_MVCC_INCREMENTS_UPD)
	{
	  /* nothing to do during recovery */
	}
      else if (rcvindex == RVBT_MVCC_NOTIFY_VACUUM || rcvindex == RVES_NOTIFY_VACUUM)
	{
	  /* nothing to do */
	}
      else if (rcvindex == RVBT_LOG_GLOBAL_UNIQUE_STATS_COMMIT)
	{
	  /* this only modifies in memory data that is only flushed to disk on checkpoints. we need to execute undo
	   * every time recovery is run, and we cannot compensate it. */
	  error_code = (*RV_fun[rcvindex].undofun) (thread_p, rcv);
	  assert (error_code == NO_ERROR);
	}
      else if (RCV_IS_LOGICAL_COMPENSATE_MANUAL (rcvindex))
	{
	  /* B-tree logical logs will add a regular compensate in the modified pages. They do not require a logical
	   * compensation since the "undone" page can be accessed and logged. Only no-page logical operations require
	   * logical compensation. */
	  /* Invoke Undo recovery function */
	  LSA_COPY (&rcv->reference_lsa, &tdes->undo_nxlsa);
	  error_code = (*RV_fun[rcvindex].undofun) (thread_p, rcv);
	  if (error_code != NO_ERROR)
	    {
	      logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
				 "log_rv_undo_record: Error applying compensation at log_lsa=(%lld, %d), "
				 "rcv = {mvccid=%llu, offset = %d, data_length = %d}",
				 (long long int) rcv_undo_lsa->pageid, (int) rcv_undo_lsa->offset,
				 (unsigned long long int) rcv->mvcc_id, (int) rcv->offset, (int) rcv->length);
	    }
	  else if (RCV_IS_BTREE_LOGICAL_LOG (rcvindex) && prm_get_bool_value (PRM_ID_LOG_BTREE_OPS))
	    {
	      _er_log_debug (ARG_FILE_LINE,
			     "BTREE_UNDO: Successfully executed undo/compensate for log entry at "
			     "lsa=%lld|%d, before lsa=%lld|%d, undo_nxlsa=%lld|%d. "
			     "Transaction=%d, rcvindex=%d.\n", (long long int) rcv_undo_lsa->pageid,
			     (int) rcv_undo_lsa->offset, (long long int) log_lsa->pageid, (int) log_lsa->offset,
			     (long long int) tdes->undo_nxlsa.pageid, (int) tdes->undo_nxlsa.offset, tdes->tran_index,
			     rcvindex);
	    }
	}
      else if (!RCV_IS_LOGICAL_LOG (rcv_vpid, rcvindex))
	{
	  log_append_compensate (thread_p, rcvindex, rcv_vpid, rcv->offset, rcv->pgptr, rcv->length, rcv->data, tdes);

	  error_code = (*RV_fun[rcvindex].undofun) (thread_p, rcv);

	  if (error_code != NO_ERROR)
	    {
	      VPID vpid;

	      if (rcv->pgptr != NULL)
		{
		  pgbuf_get_vpid (rcv->pgptr, &vpid);
		}
	      else
		{
		  VPID_SET_NULL (&vpid);
		}

	      logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
				 "log_rv_undo_record: Error applying compensation at log_lsa=(%lld, %d), "
				 "rcv = {mvccid=%llu, vpid=(%d, %d), offset = %d, data_length = %d}",
				 (long long int) rcv_undo_lsa->pageid, (int) rcv_undo_lsa->offset,
				 (unsigned long long int) rcv->mvcc_id, (int) vpid.pageid, (int) vpid.volid,
				 (int) rcv->offset, (int) rcv->length);
	      goto end;
	    }
	}
      else
	{
	  /* Logical logging? This is a logical undo. For now, we also use a logical compensation, meaning that we
	   * open a system operation that is committed & compensate at the same time.
	   * However, there might be cases when compensation is not necessarily logical. If the compensation can be
	   * made in a single log record and can be attached to a page, the system operation becomes useless. Take the
	   * example of some b-tree cases for compensations. There might be other cases too.
	   */
	  save_state = tdes->state;

	  LSA_COPY (&rcv->reference_lsa, &tdes->undo_nxlsa);
	  log_sysop_start (thread_p);

#if defined(CUBRID_DEBUG)
	  {
	    /* TODO: What is this? We might remove the block. */
	    LOG_LSA check_tail_lsa;

	    LSA_COPY (&check_tail_lsa, &tdes->tail_lsa);
	    (void) (*RV_fun[rcvindex].undofun) (rcv);

	    /* 
	     * Make sure that a CLR was logged.
	     *
	     * If we do not log anything and the logical undo_nxlsa is not the
	     * tail, give a warning.. unless it is a temporary file deletion.
	     *
	     * WARNING: the if condition is different from the one of normal
	     *          rollback.
	     */

	    if (LSA_EQ (&check_tail_lsa, &tdes->tail_lsa) && !LSA_EQ (rcv_undo_lsa, &tdes->tail_lsa))
	      {
		er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_MISSING_COMPENSATING_RECORD, 1,
			rv_rcvindex_string (rcvindex));
	      }
	  }
#else /* CUBRID_DEBUG */
	  (void) (*RV_fun[rcvindex].undofun) (thread_p, rcv);
#endif /* CUBRID_DEBUG */

	  log_sysop_end_logical_compensate (thread_p, &rcv->reference_lsa);
	  tdes->state = save_state;
	}
    }
  else
    {
      log_append_compensate (thread_p, rcvindex, rcv_vpid, rcv->offset, NULL, rcv->length, rcv->data, tdes);
      /* 
       * Unable to fetch page of volume... May need media recovery on such
       * page
       */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_MAYNEED_MEDIA_RECOVERY, 1,
	      fileio_get_volume_label (rcv_vpid->volid, PEEK));
    }

end:
  if (area != NULL)
    {
      free_and_init (area);
    }

  if (rcv->pgptr != NULL)
    {
      pgbuf_unfix (thread_p, rcv->pgptr);
    }

  /* Convert thread back to system transaction. */
  if (LOG_IS_VACUUM_THREAD_TRANID (tdes->trid))
    {
      VACUUM_RESTORE_THREAD (thread_p, save_thread_type);
    }
  else
    {
      LOG_SET_CURRENT_TRAN_INDEX (thread_p, LOG_SYSTEM_TRAN_INDEX);
    }
}

/*
 * log_rv_redo_record - EXECUTE A REDO RECORD
 *
 * return: nothing
 *
 *   log_lsa(in/out): Log address identifier containing the log record
 *   log_page_p(in/out): Pointer to page where data starts (Set as a side
 *               effect to the page where data ends)
 *   redofun(in): Function to invoke to redo the data
 *   rcv(in/out): Recovery structure for recovery function(Set as a side
 *               effect)
 *   rcv_lsa_ptr(in): Reset data page (rcv->pgptr) to this LSA
 *   ignore_redofunc(in):
 *   undo_length(in):
 *   undo_data(in):
 *   redo_unzip_ptr(in):
 *
 * NOTE: Execute a redo log record.
 */
static void
log_rv_redo_record (THREAD_ENTRY * thread_p, LOG_LSA * log_lsa, LOG_PAGE * log_page_p,
		    int (*redofun) (THREAD_ENTRY * thread_p, LOG_RCV *), LOG_RCV * rcv, LOG_LSA * rcv_lsa_ptr,
		    int undo_length, char *undo_data, LOG_ZIP * redo_unzip_ptr)
{
  char *area = NULL;
  bool is_zip = false;
  int error_code;

  /* Note the the data page rcv->pgptr has been fetched by the caller */

  /* 
   * If data is contained in only one buffer, pass pointer directly.
   * Otherwise, allocate a contiguous area, copy the data and pass this area.
   * At the end deallocate the area.
   */

  if (ZIP_CHECK (rcv->length))
    {
      rcv->length = (int) GET_ZIP_LEN (rcv->length);
      is_zip = true;
    }

  if (log_lsa->offset + rcv->length < (int) LOGAREA_SIZE)
    {
      rcv->data = (char *) log_page_p->area + log_lsa->offset;
      log_lsa->offset += rcv->length;
    }
  else
    {
      area = (char *) malloc (rcv->length);
      if (area == NULL)
	{
	  logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_rvredo_rec");
	  return;
	}
      /* Copy the data */
      logpb_copy_from_log (thread_p, area, rcv->length, log_lsa, log_page_p);
      rcv->data = area;
    }

  if (is_zip)
    {
      if (log_unzip (redo_unzip_ptr, rcv->length, (char *) rcv->data))
	{
	  if ((undo_length > 0) && (undo_data != NULL))
	    {
	      (void) log_diff (undo_length, undo_data, redo_unzip_ptr->data_length, redo_unzip_ptr->log_data);
	      rcv->length = (int) redo_unzip_ptr->data_length;
	      rcv->data = (char *) redo_unzip_ptr->log_data;
	    }
	  else
	    {
	      rcv->length = (int) redo_unzip_ptr->data_length;
	      rcv->data = (char *) redo_unzip_ptr->log_data;
	    }
	}
      else
	{
	  logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_rvredo_rec");
	}
    }

  if (redofun != NULL)
    {
      error_code = (*redofun) (thread_p, rcv);
      if (error_code != NO_ERROR)
	{
	  VPID vpid;
	  if (rcv->pgptr != NULL)
	    {
	      pgbuf_get_vpid (rcv->pgptr, &vpid);
	    }
	  else
	    {
	      VPID_SET_NULL (&vpid);
	    }

	  logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
			     "log_rvredo_rec: Error applying redo record at log_lsa=(%lld, %d), "
			     "rcv = {mvccid=%llu, vpid=(%d, %d), offset = %d, data_length = %d}",
			     (long long int) rcv_lsa_ptr->pageid, (int) rcv_lsa_ptr->offset,
			     (long long int) rcv->mvcc_id, (int) vpid.pageid, (int) vpid.volid, (int) rcv->offset,
			     (int) rcv->length);
	}
    }
  else
    {
      er_log_debug (ARG_FILE_LINE,
		    "log_rvredo_rec: WARNING.. There is not a"
		    " REDO function to execute. May produce recovery problems.");
    }

  if (rcv->pgptr != NULL)
    {
      (void) pgbuf_set_lsa (thread_p, rcv->pgptr, rcv_lsa_ptr);
    }

  if (area != NULL)
    {
      free_and_init (area);
    }
}

/*
 * log_rv_find_checkpoint - FIND RECOVERY CHECKPOINT
 *
 * return: true
 *
 *   volid(in): Volume identifier
 *   rcv_lsa(in/out): Recovery log sequence address
 *
 * NOTE: Find the recovery checkpoint address of the given volume. If
 *              it is smaller than rcv_lsa, rcv_lsa is reset to such value.
 */
static bool
log_rv_find_checkpoint (THREAD_ENTRY * thread_p, VOLID volid, LOG_LSA * rcv_lsa)
{
  LOG_LSA chkpt_lsa;		/* Checkpoint LSA of volume */
  int ret = NO_ERROR;

  ret = disk_get_checkpoint (thread_p, volid, &chkpt_lsa);
  if (LSA_ISNULL (rcv_lsa) || LSA_LT (&chkpt_lsa, rcv_lsa))
    {
      LSA_COPY (rcv_lsa, &chkpt_lsa);
    }

  return true;
}

/*
 * get_log_data - GET UNZIP LOG DATA FROM LOG
 *
 * return:
 *
 *   length(in): log data size
 *   log_lsa(in/out): Log address identifier containing the log record
 *   log_page_p(in): Log page pointer where LSA is located
 *   undo_unzip_ptr(in):
 *
 * NOTE:if log_data is unzip data return LOG_ZIP data
 *               else log_data is zip data return unzip log data
 */
static bool
log_rv_get_unzip_log_data (THREAD_ENTRY * thread_p, int length, LOG_LSA * log_lsa, LOG_PAGE * log_page_p,
			   LOG_ZIP * undo_unzip_ptr)
{
  char *ptr;			/* Pointer to data to be printed */
  char *area = NULL;
  bool is_zip = false;

  /* 
   * If data is contained in only one buffer, pass pointer directly.
   * Otherwise, allocate a contiguous area,
   * copy the data and pass this area. At the end deallocate the area
   */

  if (ZIP_CHECK (length))
    {
      length = (int) GET_ZIP_LEN (length);
      is_zip = true;
    }

  if (log_lsa->offset + length < (int) LOGAREA_SIZE)
    {
      /* Data is contained in one buffer */
      ptr = (char *) log_page_p->area + log_lsa->offset;
      log_lsa->offset += length;
    }
  else
    {
      /* Need to copy the data into a contiguous area */
      area = (char *) malloc (length);
      if (area == NULL)
	{
	  return false;
	}
      /* Copy the data */
      logpb_copy_from_log (thread_p, area, length, log_lsa, log_page_p);
      ptr = area;
    }

  if (is_zip)
    {
      if (!log_unzip (undo_unzip_ptr, length, ptr))
	{
	  if (area != NULL)
	    {
	      free_and_init (area);
	    }
	  return false;
	}
    }
  else
    {
      undo_unzip_ptr->data_length = length;
      memcpy (undo_unzip_ptr->log_data, ptr, length);
    }
  LOG_READ_ALIGN (thread_p, log_lsa, log_page_p);

  if (area != NULL)
    {
      free_and_init (area);
    }

  return true;
}

/*
 * log_recovery - Recover information
 *
 * return: nothing
 *
 *   ismedia_crash(in):Are we recovering from a media crash ?
 *   stopat(in):
 *
 */
void
log_recovery (THREAD_ENTRY * thread_p, int ismedia_crash, time_t * stopat)
{
  LOG_TDES *rcv_tdes;		/* Tran. descriptor for the recovery phase */
  int rcv_tran_index;		/* Saved transaction index */
  LOG_RECORD_HEADER *eof;	/* End of the log record */
  LOG_LSA rcv_lsa;		/* Where to start the recovery */
  LOG_LSA start_redolsa;	/* Where to start redo phase */
  LOG_LSA end_redo_lsa;		/* Where to stop the redo phase */
  bool did_incom_recovery;
  int tran_index;
  INT64 num_redo_log_records;
  int error_code = NO_ERROR;

  assert (LOG_CS_OWN_WRITE_MODE (thread_p));

  /* Save the transaction index and find the transaction descriptor */

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  rcv_tdes = LOG_FIND_TDES (tran_index);
  if (rcv_tdes == NULL)
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_UNKNOWN_TRANINDEX, 1, tran_index);
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_recovery:LOG_FIND_TDES");
      return;
    }

  rcv_tran_index = tran_index;
  rcv_tdes->state = TRAN_RECOVERY;

  if (LOG_HAS_LOGGING_BEEN_IGNORED ())
    {
      /* 
       * Your database is corrupted since it crashed when logging was ignored
       */
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_CORRUPTED_DB_DUE_CRASH_NOLOGGING, 0);
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_recovery:LOG_HAS_LOGGING_BEEN_IGNORED");
      log_Gl.hdr.has_logging_been_skipped = false;
    }

  /* Find the starting LSA for the analysis phase */

  LSA_COPY (&rcv_lsa, &log_Gl.hdr.chkpt_lsa);
  if (ismedia_crash != false)
    {
      /* 
       * Media crash, we may have to start from an older checkpoint...
       * check disk headers
       */
      (void) fileio_map_mounted (thread_p, (bool (*)(THREAD_ENTRY *, VOLID, void *)) log_rv_find_checkpoint, &rcv_lsa);
    }
  else
    {
      /* 
       * We do incomplete recovery only when we are comming from a media crash.
       * That is, we are restarting from a backup
       */
      if (stopat != NULL)
	{
	  *stopat = -1;
	}
    }

  /* Notify vacuum it may need to recover the lost block data.
   * There are two possible cases here:
   * 1. recovery finds MVCC op log records after last checkpoint, so vacuum can start its recovery from last MVCC op
   *    log record.
   * 2. no MVCC op log record is found, so vacuum has to start recovery from checkpoint LSA. It will go
   *    backwards record by record until it either finds a MVCC op log record or until it reaches last block in
   *    vacuum data.
   */
  vacuum_notify_server_crashed (&rcv_lsa);

  /* 
   * First,  ANALYSIS the log to find the state of the transactions
   * Second, REDO going forward
   * Last,   UNDO going backwards
   */

  log_Gl.rcv_phase = LOG_RECOVERY_ANALYSIS_PHASE;
  log_recovery_analysis (thread_p, &rcv_lsa, &start_redolsa, &end_redo_lsa, ismedia_crash, stopat, &did_incom_recovery,
			 &num_redo_log_records);

  er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_LOG_RECOVERY_STARTED, 3, num_redo_log_records,
	  start_redolsa.pageid, end_redo_lsa.pageid);

  LSA_COPY (&log_Gl.chkpt_redo_lsa, &start_redolsa);

  LOG_SET_CURRENT_TRAN_INDEX (thread_p, rcv_tran_index);
  if (logpb_fetch_start_append_page (thread_p) != NO_ERROR)
    {
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_recovery:logpb_fetch_start_append_page");
      er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_LOG_RECOVERY_FINISHED, 0);
      return;
    }

  if (did_incom_recovery == false)
    {
      /* Read the End of file record to find out the previous address */
      eof = (LOG_RECORD_HEADER *) LOG_APPEND_PTR ();
      LOG_RESET_PREV_LSA (&eof->back_lsa);
    }

#if !defined(SERVER_MODE)
  LSA_COPY (&log_Gl.final_restored_lsa, &log_Gl.hdr.append_lsa);
#endif /* SERVER_MODE */

  log_append_empty_record (thread_p, LOG_DUMMY_CRASH_RECOVERY, NULL);

  /* 
   * Save the crash point lsa for use during the remaining recovery
   * phases.
   */
  LSA_COPY (&log_Gl.rcv_phase_lsa, &rcv_tdes->tail_lsa);

  /* Redo phase */
  log_Gl.rcv_phase = LOG_RECOVERY_REDO_PHASE;

  LOG_SET_CURRENT_TRAN_INDEX (thread_p, rcv_tran_index);

  log_recovery_redo (thread_p, &start_redolsa, &end_redo_lsa, stopat);
  boot_reset_db_parm (thread_p);

  /* Undo phase */
  log_Gl.rcv_phase = LOG_RECOVERY_UNDO_PHASE;

  LOG_SET_CURRENT_TRAN_INDEX (thread_p, rcv_tran_index);

  log_recovery_undo (thread_p);
  boot_reset_db_parm (thread_p);

  if (did_incom_recovery == true)
    {
      log_recovery_notpartof_volumes (thread_p);
    }

  /* Client loose ends */
  rcv_tdes->state = TRAN_ACTIVE;

  LOG_SET_CURRENT_TRAN_INDEX (thread_p, rcv_tran_index);

  (void) logtb_set_num_loose_end_trans (thread_p);

  /* Try to finish any 2PC blocked transactions */
  if (log_Gl.trantable.num_coord_loose_end_indices > 0 || log_Gl.trantable.num_prepared_loose_end_indices > 0)
    {

      log_Gl.rcv_phase = LOG_RECOVERY_FINISH_2PC_PHASE;

      LOG_SET_CURRENT_TRAN_INDEX (thread_p, rcv_tran_index);

      log_2pc_recovery (thread_p);

      /* Check number of loose end transactions again.. */
      rcv_tdes->state = TRAN_ACTIVE;

      LOG_SET_CURRENT_TRAN_INDEX (thread_p, rcv_tran_index);

      (void) logtb_set_num_loose_end_trans (thread_p);
    }

  /* Dismount any archive and checkpoint the database */
  logpb_decache_archive_info (thread_p);

  LOG_CS_EXIT (thread_p);
  (void) logpb_checkpoint (thread_p);
  LOG_CS_ENTER (thread_p);

  /* Flush all dirty pages */
  logpb_flush_pages_direct (thread_p);
  (void) pgbuf_flush_all (thread_p, NULL_VOLID);
  (void) fileio_synchronize_all (thread_p, false);

  logpb_flush_header (thread_p);

  /* re-cache Tracker */
  error_code = locator_initialize (thread_p);
  if (error_code != NO_ERROR)
    {
      assert (false);
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_recovery:locator_initialize");
      er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_LOG_RECOVERY_FINISHED, 0);
      return;
    }

  er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_LOG_RECOVERY_FINISHED, 0);
}

/*
 * log_rv_analysis_undo_redo -
 *
 * return: error code
 *
 *   tran_id(in):
 *   lsa(in/out):
 * Note:
 */
static int
log_rv_analysis_undo_redo (THREAD_ENTRY * thread_p, int tran_id, LOG_LSA * log_lsa)
{
  LOG_TDES *tdes;

  /* 
   * If this is the first time, the transaction is seen. Assign a new
   * index to describe it and assume that the transaction was active
   * at the time of the crash, and thus it will be unilateraly
   * aborted. The truth of this statement will be find reading the
   * rest of the log
   */
  tdes = logtb_rv_find_allocate_tran_index (thread_p, tran_id, log_lsa);
  if (tdes == NULL)
    {
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_rv_analysis_undo_redo");
      return ER_FAILED;
    }

  if (LOG_IS_VACUUM_THREAD_TRANID (tdes->trid))
    {
      vacuum_er_log (VACUUM_ER_LOG_RECOVERY | VACUUM_ER_LOG_TOPOPS, "VACUUM: Found undo_redo record. tdes->trid=%d.",
		     tdes->trid);
    }

  /* New tail and next to undo */
  LSA_COPY (&tdes->tail_lsa, log_lsa);
  LSA_COPY (&tdes->undo_nxlsa, &tdes->tail_lsa);

  return NO_ERROR;
}

/*
 * log_rv_analysis_dummy_head_postpone -
 *
 * return: error code
 *
 *   tran_id(in):
 *   lsa(in/out):
 * Note:
 */
static int
log_rv_analysis_dummy_head_postpone (THREAD_ENTRY * thread_p, int tran_id, LOG_LSA * log_lsa)
{
  LOG_TDES *tdes;

  /* 
   * If this is the first time, the transaction is seen. Assign a new
   * index to describe it and assume that the transaction was active
   * at the time of the crash, and thus it will be unilateraly
   * aborted. The truth of this statement will be find reading the
   * rest of the log
   */
  tdes = logtb_rv_find_allocate_tran_index (thread_p, tran_id, log_lsa);
  if (tdes == NULL)
    {
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_rv_analysis_dummy_head_postpone");
      return ER_FAILED;
    }

  /* New tail and next to undo */
  LSA_COPY (&tdes->tail_lsa, log_lsa);
  LSA_COPY (&tdes->undo_nxlsa, &tdes->tail_lsa);

  /* if first postpone, then set address late */
  if (LSA_ISNULL (&tdes->posp_nxlsa))
    {
      LSA_COPY (&tdes->posp_nxlsa, &tdes->tail_lsa);
    }

  return NO_ERROR;
}

/*
 * log_rv_analysis_postpone -
 *
 * return: error code
 *
 *   tran_id(in):
 *   lsa(in/out):
 *
 * Note:
 */
static int
log_rv_analysis_postpone (THREAD_ENTRY * thread_p, int tran_id, LOG_LSA * log_lsa)
{
  LOG_TDES *tdes;

  /* 
   * If this is the first time, the transaction is seen. Assign a new
   * index to describe it and assume that the transaction was active
   * at the time of the crash, and thus it will be unilateraly
   * aborted. The truth of this statement will be find reading the
   * rest of the log
   */
  tdes = logtb_rv_find_allocate_tran_index (thread_p, tran_id, log_lsa);
  if (tdes == NULL)
    {
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_rv_analysis_postpone");
      return ER_FAILED;
    }

  if (LOG_IS_VACUUM_THREAD_TRANID (tdes->trid))
    {
      vacuum_er_log (VACUUM_ER_LOG_RECOVERY | VACUUM_ER_LOG_TOPOPS, "VACUUM: Found postpone record. tdes->trid=%d.",
		     tdes->trid);
    }

  /* if first postpone, then set address early */
  if (LSA_ISNULL (&tdes->posp_nxlsa))
    {
      LSA_COPY (&tdes->posp_nxlsa, &tdes->tail_lsa);
    }

  /* New tail and next to undo */
  LSA_COPY (&tdes->tail_lsa, log_lsa);
  LSA_COPY (&tdes->undo_nxlsa, &tdes->tail_lsa);

  return NO_ERROR;
}

/*
 * log_rv_analysis_run_postpone -
 *
 * return: error code
 *
 *   tran_id(in):
 *   lsa(in/out):
 *   log_page_p(in/out):
 *   check_point(in/out):
 *
 * Note:
 */
static int
log_rv_analysis_run_postpone (THREAD_ENTRY * thread_p, int tran_id, LOG_LSA * log_lsa, LOG_PAGE * log_page_p,
			      LOG_LSA * check_point)
{
  LOG_TDES *tdes;
  LOG_REC_RUN_POSTPONE *run_posp;

  tdes = logtb_rv_find_allocate_tran_index (thread_p, tran_id, log_lsa);
  if (tdes == NULL)
    {
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_rv_analysis_run_postpone");
      return ER_FAILED;
    }

  if (tdes->state != TRAN_UNACTIVE_WILL_COMMIT && tdes->state != TRAN_UNACTIVE_COMMITTED_WITH_POSTPONE
      && tdes->state != TRAN_UNACTIVE_TOPOPE_COMMITTED_WITH_POSTPONE)
    {
      /* 
       * If we are comming from a checkpoint this is the result of a
       * system error since the transaction must have been already in
       * one of these states.
       * If we are not comming from a checkpoint, it is likely that
       * we are in a commit point of either a transaction or a top
       * operation.
       */
      if (!LSA_ISNULL (check_point))
	{
	  er_log_debug (ARG_FILE_LINE,
			"log_recovery_analysis: SYSTEM ERROR\n Incorrect state = %s\n at log_rec at %lld|%d\n"
			" for transaction = %d (index %d).\n State should have been either of\n %s\n %s\n %s\n",
			log_state_string (tdes->state), (long long int) log_lsa->pageid, (int) log_lsa->offset,
			tdes->trid, tdes->tran_index, log_state_string (TRAN_UNACTIVE_WILL_COMMIT),
			log_state_string (TRAN_UNACTIVE_COMMITTED_WITH_POSTPONE),
			log_state_string (TRAN_UNACTIVE_TOPOPE_COMMITTED_WITH_POSTPONE));
	}
      /* 
       * Continue the execution by guessing that the transaction has
       * been committed
       */
      if (tdes->topops.last == -1)
	{
	  tdes->state = TRAN_UNACTIVE_COMMITTED_WITH_POSTPONE;
	}
      else
	{
	  tdes->state = TRAN_UNACTIVE_TOPOPE_COMMITTED_WITH_POSTPONE;
	}
    }

  if (tdes->state == TRAN_UNACTIVE_COMMITTED_WITH_POSTPONE)
    {
      /* Nothing to undo */
      LSA_SET_NULL (&tdes->undo_nxlsa);
    }

  LSA_COPY (&tdes->tail_lsa, log_lsa);

  /* 
   * Need to read the log_run_postpone record to reset the posp_nxlsa
   * of transaction or top action to the value of log_ref
   */

  /* Read the DATA HEADER */
  LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_RECORD_HEADER), log_lsa, log_page_p);
  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (LOG_REC_RUN_POSTPONE), log_lsa, log_page_p);

  run_posp = (LOG_REC_RUN_POSTPONE *) ((char *) log_page_p->area + log_lsa->offset);

  if (tdes->state == TRAN_UNACTIVE_TOPOPE_COMMITTED_WITH_POSTPONE)
    {
      /* Reset start of postpone transaction for the top action */
      LSA_COPY (&tdes->topops.stack[tdes->topops.last].posp_lsa, &run_posp->ref_lsa);
    }
  else
    {
      assert (tdes->state == TRAN_UNACTIVE_WILL_COMMIT || tdes->state == TRAN_UNACTIVE_COMMITTED_WITH_POSTPONE);

      /* Reset start of postpone transaction */
      LSA_COPY (&tdes->posp_nxlsa, &run_posp->ref_lsa);
    }

  if (LOG_IS_VACUUM_THREAD_TRANID (tdes->trid))
    {
      vacuum_er_log (VACUUM_ER_LOG_RECOVERY | VACUUM_ER_LOG_TOPOPS,
		     "VACUUM: Found postpone record. tdes->trid=%d, tdes->state=%d, ref_lsa=(%lld, %d).", tdes->trid,
		     tdes->state, (long long int) run_posp->ref_lsa.pageid, (int) run_posp->ref_lsa.offset);
    }

  return NO_ERROR;
}

/*
 * log_rv_analysis_compensate -
 *
 * return: error code
 *
 *   tran_id(in):
 *   lsa(in/out):
 *   log_page_p(in/out):
 *
 * Note:
 */
static int
log_rv_analysis_compensate (THREAD_ENTRY * thread_p, int tran_id, LOG_LSA * log_lsa, LOG_PAGE * log_page_p)
{
  LOG_TDES *tdes;
  LOG_REC_COMPENSATE *compensate;

  /* 
   * If this is the first time, the transaction is seen. Assign a new
   * index to describe it and assume that the transaction was active
   * at the time of the crash, and thus it will be unilateraly
   * aborted. The truth of this statement will be find reading the
   * rest of the log
   */
  tdes = logtb_rv_find_allocate_tran_index (thread_p, tran_id, log_lsa);
  if (tdes == NULL)
    {
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_rv_analysis_compensate");
      return ER_FAILED;
    }

  /* 
   * Need to read the compensating record to set the next undo address
   */

  /* Read the DATA HEADER */
  LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_RECORD_HEADER), log_lsa, log_page_p);
  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (LOG_REC_COMPENSATE), log_lsa, log_page_p);

  compensate = (LOG_REC_COMPENSATE *) ((char *) log_page_p->area + log_lsa->offset);
  LSA_COPY (&tdes->undo_nxlsa, &compensate->undo_nxlsa);

  return NO_ERROR;
}

/*
 * log_rv_analysis_will_commit -
 *
 * return: error code
 *
 *   tran_id(in):
 *   lsa(in/out):
 *
 * Note:
 */
static int
log_rv_analysis_will_commit (THREAD_ENTRY * thread_p, int tran_id, LOG_LSA * log_lsa)
{
  LOG_TDES *tdes;

  /* 
   * If this is the first time, the transaction is seen. Assign a new
   * index to describe it. The transaction was in the process of
   * getting committed at this point.
   */
  tdes = logtb_rv_find_allocate_tran_index (thread_p, tran_id, log_lsa);
  if (tdes == NULL)
    {
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_rv_analysis_will_commit");
      return ER_FAILED;
    }

  tdes->state = TRAN_UNACTIVE_WILL_COMMIT;

  /* Nothing to undo */
  LSA_SET_NULL (&tdes->undo_nxlsa);
  LSA_COPY (&tdes->tail_lsa, log_lsa);

  return NO_ERROR;
}

/*
 * log_rv_analysis_commit_with_postpone -
 *
 * return: error code
 *
 *   tran_id(in):
 *   lsa(in/out):
 *   log_page_p(in/out):
 *
 * Note:
 */
static int
log_rv_analysis_commit_with_postpone (THREAD_ENTRY * thread_p, int tran_id, LOG_LSA * log_lsa, LOG_PAGE * log_page_p)
{
  LOG_TDES *tdes;
  LOG_REC_START_POSTPONE *start_posp;

  /* 
   * If this is the first time, the transaction is seen. Assign a new
   * index to describe it. The transaction was in the process of
   * getting committed at this point.
   */
  tdes = logtb_rv_find_allocate_tran_index (thread_p, tran_id, log_lsa);
  if (tdes == NULL)
    {
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_rv_analysis_commit_with_postpone");
      return ER_FAILED;
    }

  tdes->state = TRAN_UNACTIVE_COMMITTED_WITH_POSTPONE;

  /* Nothing to undo */
  LSA_SET_NULL (&tdes->undo_nxlsa);
  LSA_COPY (&tdes->tail_lsa, log_lsa);
  tdes->rcv.tran_start_postpone_lsa = tdes->tail_lsa;

  /* 
   * Need to read the start postpone record to set the postpone address
   * of the transaction
   */
  LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_RECORD_HEADER), log_lsa, log_page_p);
  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (LOG_REC_START_POSTPONE), log_lsa, log_page_p);

  start_posp = (LOG_REC_START_POSTPONE *) ((char *) log_page_p->area + log_lsa->offset);
  LSA_COPY (&tdes->posp_nxlsa, &start_posp->posp_lsa);

  return NO_ERROR;
}

/*
 * log_rv_analysis_sysop_start_postpone - start system op postpone.
 *
 * return: error code
 *
 *   tran_id(in):
 *   lsa(in/out):
 *   log_page_p(in/out):
 *
 * Note:
 */
static int
log_rv_analysis_sysop_start_postpone (THREAD_ENTRY * thread_p, int tran_id, LOG_LSA * log_lsa, LOG_PAGE * log_page_p)
{
  LOG_TDES *tdes;
  LOG_REC_SYSOP_START_POSTPONE *sysop_start_posp;

  /* 
   * If this is the first time, the transaction is seen. Assign a new
   * index to describe it. A top system operation was in the process
   * of getting committed at this point.
   */
  tdes = logtb_rv_find_allocate_tran_index (thread_p, tran_id, log_lsa);
  if (tdes == NULL)
    {
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_rv_analysis_sysop_start_postpone");
      return ER_FAILED;
    }

  if (LOG_IS_VACUUM_THREAD_TRANID (tdes->trid))
    {
      vacuum_er_log (VACUUM_ER_LOG_RECOVERY | VACUUM_ER_LOG_TOPOPS,
		     "VACUUM: Found commit_topope_with_postpone. tdes->trid=%d. ", tdes->trid);
    }

  LSA_COPY (&tdes->tail_lsa, log_lsa);
  LSA_COPY (&tdes->undo_nxlsa, &tdes->tail_lsa);

  tdes->rcv.sysop_start_postpone_lsa = tdes->tail_lsa;

  /* 
   * Need to read the start postpone record to set the start address
   * of top system operation
   */
  LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_RECORD_HEADER), log_lsa, log_page_p);
  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (LOG_REC_SYSOP_START_POSTPONE), log_lsa, log_page_p);
  sysop_start_posp = ((LOG_REC_SYSOP_START_POSTPONE *) ((char *) log_page_p->area + log_lsa->offset));

  if (tdes->state == TRAN_UNACTIVE_TOPOPE_COMMITTED_WITH_POSTPONE)
    {
      /* this is not a valid situation */
      assert_release (false);
    }
  else if (tdes->state == TRAN_UNACTIVE_COMMITTED_WITH_POSTPONE)
    {
      /* only transaction run postpones are acceptable */
      assert (sysop_start_posp->sysop_end.type == LOG_SYSOP_END_LOGICAL_RUN_POSTPONE);
      assert (sysop_start_posp->sysop_end.run_postpone.is_sysop_postpone == false);

      /* no undo */
      LSA_SET_NULL (&tdes->undo_nxlsa);
    }
  else if (sysop_start_posp->sysop_end.type == LOG_SYSOP_END_LOGICAL_RUN_POSTPONE)
    {
      if (sysop_start_posp->sysop_end.run_postpone.is_sysop_postpone)
	{
	  /* system op postpone inside system op postpone. not a valid situation */
	  assert (false);
	}
      else
	{
	  /* no undo. it is possible that the transaction state is TRAN_UNACTIVE_UNILATERALLY_ABORTED because this might
	   * be the first log record discovered for current transaction. it will be set correctly when the system op end
	   * is found or executed.
	   */
	  LSA_SET_NULL (&tdes->undo_nxlsa);
	}
    }
  else
    {
      assert (sysop_start_posp->sysop_end.type != LOG_SYSOP_END_ABORT);
    }

  /* update state */
  tdes->state = TRAN_UNACTIVE_TOPOPE_COMMITTED_WITH_POSTPONE;

  if (tdes->topops.max == 0 || (tdes->topops.last + 1) >= tdes->topops.max)
    {
      if (logtb_realloc_topops_stack (tdes, 1) == NULL)
	{
	  /* Out of memory */
	  logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_recovery_analysis");
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}
    }

  /* 
   * NOTE if tdes->topops.last >= 0, there is an already
   * defined top system operation. However, I do not think so
   * do to the nested fashion of top system operations. Outer
   * top nested system operations will come later in the log.
   */

  if (tdes->topops.last == -1)
    {
      tdes->topops.last++;
    }
  else
    {
      /* not expected */
      assert (false);
    }

  LSA_COPY (&tdes->topops.stack[tdes->topops.last].lastparent_lsa, &sysop_start_posp->sysop_end.lastparent_lsa);
  LSA_COPY (&tdes->topops.stack[tdes->topops.last].posp_lsa, &sysop_start_posp->posp_lsa);

  return NO_ERROR;
}

/*
 * log_rv_analysis_complete -
 *
 * return: error code
 *
 *   tran_id(in):
 *   log_lsa(in/out):
 *   log_page_p(in/out):
 *   lsa(in/out):
 *   is_media_crash(in):
 *   stop_at(in):
 *   did_incom_recovery(in/out):
 *
 * Note:
 */
static int
log_rv_analysis_complete (THREAD_ENTRY * thread_p, int tran_id, LOG_LSA * log_lsa, LOG_PAGE * log_page_p,
			  LOG_LSA * prev_lsa, bool is_media_crash, time_t * stop_at, bool * did_incom_recovery)
{
  LOG_REC_DONETIME *donetime;
  int tran_index;
  time_t last_at_time;
  char time_val[CTIME_MAX];
  LOG_LSA record_header_lsa;

  /* 
   * The transaction has been fully completed. therefore, it was not
   * active at the time of the crash
   */
  tran_index = logtb_find_tran_index (thread_p, tran_id);

  if (is_media_crash != true)
    {
      goto end;
    }

  LSA_COPY (&record_header_lsa, log_lsa);

  /* 
   * Need to read the donetime record to find out if we need to stop
   * the recovery at this point.
   */
  LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_RECORD_HEADER), log_lsa, log_page_p);
  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (LOG_REC_DONETIME), log_lsa, log_page_p);

  donetime = (LOG_REC_DONETIME *) ((char *) log_page_p->area + log_lsa->offset);
  last_at_time = (time_t) donetime->at_time;
  if (stop_at != NULL && *stop_at != (time_t) (-1) && difftime (*stop_at, last_at_time) < 0)
    {
#if !defined(NDEBUG)
      if (prm_get_bool_value (PRM_ID_LOG_TRACE_DEBUG))
	{
	  fprintf (stdout, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_LOG, MSGCAT_LOG_STARTS));
	  (void) ctime_r (&last_at_time, time_val);
	  fprintf (stdout, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_LOG, MSGCAT_LOG_INCOMPLTE_MEDIA_RECOVERY),
		   record_header_lsa.pageid, record_header_lsa.offset, time_val);
	  fprintf (stdout, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_LOG, MSGCAT_LOG_STARTS));
	  fflush (stdout);
	}
#endif /* !NDEBUG */
      /* 
       * Reset the log active and stop the recovery process at this
       * point. Before reseting the log, make sure that we are not
       * holding a page.
       */
      log_lsa->pageid = NULL_PAGEID;
      log_recovery_resetlog (thread_p, &record_header_lsa, false, prev_lsa);
      *did_incom_recovery = true;

      return NO_ERROR;
    }

end:

  /* 
   * The transaction has been fully completed. Therefore, it was not
   * active at the time of the crash
   */
  if (tran_index != NULL_TRAN_INDEX)
    {
      logtb_free_tran_index (thread_p, tran_index);
    }

  return NO_ERROR;
}

/* TODO: We need to understand how recovery of system operations really works. We need to find its limitations.
 *	 For now, I did find out this:
 *	 1. if tdes->topops.last is 0 (cannot be bigger during recovery), it means the transaction should be in
 *	    state TRAN_UNACTIVE_TOPOPE_COMMITTED_WITH_POSTPONE. tdes->topops.last may be incremented by
 *	    log_rv_analysis_sysop_start_postpone or may be already 0 from checkpoint collected topops.
 *	 2. In all other cases it must be -1.
 *	 3. We used to consider that first sys op commit (or abort) that follows is the one that should end the postpone
 *	    phase.
 *
 *	 Now, for TRAN_UNACTIVE_TOPOPE_COMMITTED_WITH_POSTPONE state we will expect last to be always 0.
 *	 We will handle sys op ends like:
 *	 - commit, logical undo, logical compensate: we consider this ends the
 *         TRAN_UNACTIVE_TOPOPE_COMMITTED_WITH_POSTPONE phase.
 *	 - logical run postpone: it depends on is_sysop_postpone. if true, then
 *         TRAN_UNACTIVE_TOPOPE_COMMITTED_WITH_POSTPONE continues. If it is false, then it commits the system op and
 *         transitions to TRAN_UNACTIVE_COMMITTED_WITH_POSTPONE.
 *	 - abort: we assume this was a nested run postpone that got aborted
 *
 *	 In the future, I hope we can come up with a more clear and less restrictive system to handle system operations
 *	 during recovery.
 */

/*
 * log_rv_analysis_sysop_end () - Analyze system operation commit log record.
 *
 * return	   : Error code.
 * thread_p (in)   : Thread entry.
 * tran_id (in)	   : Transaction ID.
 * log_lsa (in)	   : LSA of log record.
 * log_page_p (in) : Page of log record.
 */
static int
log_rv_analysis_sysop_end (THREAD_ENTRY * thread_p, int tran_id, LOG_LSA * log_lsa, LOG_PAGE * log_page_p)
{
  LOG_TDES *tdes;
  LOG_REC_SYSOP_END *sysop_end;
  bool commit_start_postpone = false;

  /* The top system action is declared as finished. Pop it from the stack of finished actions */
  tdes = logtb_rv_find_allocate_tran_index (thread_p, tran_id, log_lsa);
  if (tdes == NULL)
    {
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_rv_analysis_complete_topope");
      return ER_FAILED;
    }

  if (LOG_IS_VACUUM_THREAD_TRANID (tdes->trid))
    {
      vacuum_er_log (VACUUM_ER_LOG_RECOVERY | VACUUM_ER_LOG_TOPOPS, "VACUUM: Found commit sysop. tdes->trid=%d.",
		     tdes->trid);
    }

  LSA_COPY (&tdes->tail_lsa, log_lsa);
  LSA_COPY (&tdes->undo_nxlsa, &tdes->tail_lsa);
  LSA_COPY (&tdes->tail_topresult_lsa, &tdes->tail_lsa);

  LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_RECORD_HEADER), log_lsa, log_page_p);
  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (*sysop_end), log_lsa, log_page_p);
  sysop_end = (LOG_REC_SYSOP_END *) (log_page_p->area + log_lsa->offset);

  LOG_SYSOP_END_TYPE_CHECK (sysop_end->type);

  switch (sysop_end->type)
    {
    case LOG_SYSOP_END_ABORT:
      /* abort does not change state and does not finish TRAN_UNACTIVE_TOPOPE_COMMITTED_WITH_POSTPONE */
      if (tdes->state == TRAN_UNACTIVE_COMMITTED_WITH_POSTPONE)
	{
	  /* no undo */
	  LSA_SET_NULL (&tdes->undo_nxlsa);
	}
      break;

    case LOG_SYSOP_END_LOGICAL_UNDO:
    case LOG_SYSOP_END_LOGICAL_MVCC_UNDO:
      /* can be used in all states, but cannot have postpones. so it doesn't commit_start_postpone.
       * note: if we add postpones to logical undo, it will have to save previous state... maybe it is a good idea to
       *       do this for all types of end system op. */
      break;

    case LOG_SYSOP_END_COMMIT:
      assert (tdes->state != TRAN_UNACTIVE_COMMITTED_WITH_POSTPONE);
      commit_start_postpone = true;
      break;

    case LOG_SYSOP_END_LOGICAL_COMPENSATE:
      /* compensate undo */
      assert (tdes->state != TRAN_UNACTIVE_COMMITTED_WITH_POSTPONE);
      tdes->undo_nxlsa = sysop_end->compensate_lsa;
      commit_start_postpone = true;
      break;

    case LOG_SYSOP_END_LOGICAL_RUN_POSTPONE:
      /* we have a complicated story here, because logical run postpone can be run in two situations: transaction
       * postpone or system op postpone.
       * logical run postpone in transaction postpone can have other postpones inside it (e.g. file destroy).
       *
       * as a consequence, if state is TRAN_UNACTIVE_TOPOPE_COMMITTED_WITH_POSTPONE, we have an ambiguous case:
       * 1. it can be from a system op postpone and the logical run postpone is run for a postpone log record inside the
       *    system op.
       * 2. it can be from a transaction postpone and this is the postpone phase of logical run postpone system op.
       * the ambiguity is solved with run_postpone.is_sysop_postpone field, which is true if run postpone belongs to
       * system op postpone state and false if it belongs to transaction postpone phase.
       */

      if (sysop_end->run_postpone.is_sysop_postpone)
	{
	  /* run postpone for log record inside a system op */
	  if (tdes->topops.last < 0 || tdes->state != TRAN_UNACTIVE_TOPOPE_COMMITTED_WITH_POSTPONE)
	    {
	      tdes->topops.last = 0;
	      tdes->state = TRAN_UNACTIVE_TOPOPE_COMMITTED_WITH_POSTPONE;
	    }
	  tdes->topops.stack[tdes->topops.last].posp_lsa = sysop_end->run_postpone.postpone_lsa;
	}
      else
	{
	  /* run postpone for log record inside transaction */
	  tdes->posp_nxlsa = sysop_end->run_postpone.postpone_lsa;
	  if (tdes->topops.last != -1)
	    {
	      assert (tdes->topops.last == 0);
	      assert (tdes->state == TRAN_UNACTIVE_TOPOPE_COMMITTED_WITH_POSTPONE);
	    }
	  else
	    {
	      /* state must be TRAN_UNACTIVE_COMMITTED_WITH_POSTPONE. it may be TRAN_UNACTIVE_UNILATERALLY_ABORTED */
	      tdes->state = TRAN_UNACTIVE_COMMITTED_WITH_POSTPONE;
	    }

	  /* no undo */
	  LSA_SET_NULL (&tdes->undo_nxlsa);

	  /* TRAN_UNACTIVE_TOPOPE_COMMITTED_WITH_POSTPONE transition to TRAN_UNACTIVE_COMMITTED_WITH_POSTPONE */
	  commit_start_postpone = true;
	}
      break;
    }

  if (tdes->state == TRAN_UNACTIVE_TOPOPE_COMMITTED_WITH_POSTPONE)
    {
      assert (tdes->topops.last == 0);
      if (commit_start_postpone)
	{
	  /* change state to previous state */
	  if (sysop_end->type == LOG_SYSOP_END_LOGICAL_RUN_POSTPONE)
	    {
	      tdes->state = TRAN_UNACTIVE_COMMITTED_WITH_POSTPONE;
	    }
	  else
	    {
	      /* default state in recovery is TRAN_UNACTIVE_UNILATERALLY_ABORTED */
	      tdes->state = TRAN_UNACTIVE_UNILATERALLY_ABORTED;
	    }
	  tdes->topops.last = -1;
	}
      else
	{
	  tdes->topops.last = 0;
	}
    }
  else
    {
      assert (tdes->topops.last == -1);
      tdes->topops.last = -1;
    }

  return NO_ERROR;
}

/*
 * log_rv_analysis_start_checkpoint -
 *
 * return: error code
 *
 *   lsa(in/out):
 *   start_lsa(in/out):
 *   may_use_checkpoint(in/out):
 *
 * Note:
 */
static int
log_rv_analysis_start_checkpoint (LOG_LSA * log_lsa, LOG_LSA * start_lsa, bool * may_use_checkpoint)
{
  /* 
   * Use the checkpoint record only if it is the first record in the
   * analysis. If it is not, it is likely that we are restarting from
   * crashes when the multimedia crashes were off. We skip the
   * checkpoint since it can contain stuff which does not exist any
   * longer.
   */

  if (LSA_EQ (log_lsa, start_lsa))
    {
      *may_use_checkpoint = true;
    }

  return NO_ERROR;
}

/*
 * log_rv_analysis_end_checkpoint -
 *
 * return: error code
 *
 *   lsa(in/out):
 *   log_page_p(in/out):
 *   check_point(in/out):
 *   start_redo_lsa(in/out):
 *   may_use_checkpoint(in/out):
 *   may_need_synch_checkpoint_2pc(in/out):
 *
 * Note:
 */
static int
log_rv_analysis_end_checkpoint (THREAD_ENTRY * thread_p, LOG_LSA * log_lsa, LOG_PAGE * log_page_p,
				LOG_LSA * check_point, LOG_LSA * start_redo_lsa, bool * may_use_checkpoint,
				bool * may_need_synch_checkpoint_2pc)
{
  LOG_TDES *tdes;
  LOG_REC_CHKPT *tmp_chkpt;
  LOG_REC_CHKPT chkpt;
  LOG_INFO_CHKPT_TRANS *chkpt_trans;
  LOG_INFO_CHKPT_TRANS *chkpt_one;
  LOG_INFO_CHKPT_SYSOP_START_POSTPONE *chkpt_topops;
  LOG_INFO_CHKPT_SYSOP_START_POSTPONE *chkpt_topone;
  int size;
  void *area;
  int i;

  LOG_PAGE *log_page_local = NULL;
  char log_page_buffer[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT];
  LOG_LSA log_lsa_local;
  LOG_REC_SYSOP_START_POSTPONE sysop_start_postpone;

  int error_code = NO_ERROR;

  /* 
   * Use the checkpoint record only if it is the first record in the
   * analysis. If it is not, it is likely that we are restarting from
   * crashes when the multimedia crashes were off. We skip the
   * checkpoint since it can contain stuff which does not exist any
   * longer.
   */

  if (*may_use_checkpoint == false)
    {
      return NO_ERROR;
    }
  *may_use_checkpoint = false;

  /* 
   * Read the checkpoint record information to find out the
   * start_redolsa and the active transactions
   */

  LSA_COPY (check_point, log_lsa);

  /* Read the DATA HEADER */
  LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_RECORD_HEADER), log_lsa, log_page_p);
  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (LOG_REC_CHKPT), log_lsa, log_page_p);
  tmp_chkpt = (LOG_REC_CHKPT *) ((char *) log_page_p->area + log_lsa->offset);
  chkpt = *tmp_chkpt;

  /* GET THE CHECKPOINT TRANSACTION INFORMATION */
  LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_REC_CHKPT), log_lsa, log_page_p);

  /* Now get the data of active transactions */

  area = NULL;
  size = sizeof (LOG_INFO_CHKPT_TRANS) * chkpt.ntrans;
  if (log_lsa->offset + size < (int) LOGAREA_SIZE)
    {
      chkpt_trans = (LOG_INFO_CHKPT_TRANS *) ((char *) log_page_p->area + log_lsa->offset);
      log_lsa->offset += size;
    }
  else
    {
      /* Need to copy the data into a contiguous area */
      area = malloc (size);
      if (area == NULL)
	{
	  logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_recovery_analysis");
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}
      /* Copy the data */
      logpb_copy_from_log (thread_p, (char *) area, size, log_lsa, log_page_p);
      chkpt_trans = (LOG_INFO_CHKPT_TRANS *) area;
    }

  /* Add the transactions to the transaction table */
  for (i = 0; i < chkpt.ntrans; i++)
    {
      /* 
       * If this is the first time, the transaction is seen. Assign a
       * new index to describe it and assume that the transaction was
       * active at the time of the crash, and thus it will be
       * unilaterally aborted. The truth of this statement will be find
       * reading the rest of the log
       */
      tdes = logtb_rv_find_allocate_tran_index (thread_p, chkpt_trans[i].trid, log_lsa);
      if (tdes == NULL)
	{
	  if (area != NULL)
	    {
	      free_and_init (area);
	    }

	  logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_recovery_analysis");
	  return ER_FAILED;
	}
      chkpt_one = &chkpt_trans[i];

      /* 
       * Clear the transaction since it may have old stuff in it.
       * Use the one that is find in the checkpoint record
       */
      logtb_clear_tdes (thread_p, tdes);

      tdes->isloose_end = chkpt_one->isloose_end;
      if (chkpt_one->state == TRAN_ACTIVE || chkpt_one->state == TRAN_UNACTIVE_ABORTED)
	{
	  tdes->state = TRAN_UNACTIVE_UNILATERALLY_ABORTED;
	}
      else
	{
	  tdes->state = chkpt_one->state;
	}
      LSA_COPY (&tdes->head_lsa, &chkpt_one->head_lsa);
      LSA_COPY (&tdes->tail_lsa, &chkpt_one->tail_lsa);
      LSA_COPY (&tdes->undo_nxlsa, &chkpt_one->undo_nxlsa);
      LSA_COPY (&tdes->posp_nxlsa, &chkpt_one->posp_nxlsa);
      LSA_COPY (&tdes->savept_lsa, &chkpt_one->savept_lsa);
      LSA_COPY (&tdes->tail_topresult_lsa, &chkpt_one->tail_topresult_lsa);
      LSA_COPY (&tdes->rcv.tran_start_postpone_lsa, &chkpt_one->start_postpone_lsa);
      logtb_set_client_ids_all (&tdes->client, 0, NULL, chkpt_one->user_name, NULL, NULL, NULL, -1);
      if (LOG_ISTRAN_2PC (tdes))
	{
	  *may_need_synch_checkpoint_2pc = true;
	}
    }

  if (area != NULL)
    {
      free_and_init (area);
    }

  /* 
   * Now add top system operations that were in the process of
   * commit to this transactions
   */

  log_page_local = (LOG_PAGE *) PTR_ALIGN (log_page_buffer, MAX_ALIGNMENT);
  log_page_local->hdr.logical_pageid = NULL_PAGEID;
  log_page_local->hdr.offset = NULL_OFFSET;

  if (chkpt.ntops > 0)
    {
      size = sizeof (LOG_INFO_CHKPT_SYSOP_START_POSTPONE) * chkpt.ntops;
      if (log_lsa->offset + size < (int) LOGAREA_SIZE)
	{
	  chkpt_topops = ((LOG_INFO_CHKPT_SYSOP_START_POSTPONE *) ((char *) log_page_p->area + log_lsa->offset));
	  log_lsa->offset += size;
	}
      else
	{
	  /* Need to copy the data into a contiguous area */
	  area = malloc (size);
	  if (area == NULL)
	    {
	      logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_recovery_analysis");
	      return ER_OUT_OF_VIRTUAL_MEMORY;
	    }
	  /* Copy the data */
	  logpb_copy_from_log (thread_p, (char *) area, size, log_lsa, log_page_p);
	  chkpt_topops = (LOG_INFO_CHKPT_SYSOP_START_POSTPONE *) area;
	}

      /* Add the top system operations to the transactions */

      for (i = 0; i < chkpt.ntops; i++)
	{
	  chkpt_topone = &chkpt_topops[i];
	  tdes = logtb_rv_find_allocate_tran_index (thread_p, chkpt_topone->trid, log_lsa);
	  if (tdes == NULL)
	    {
	      if (area != NULL)
		{
		  free_and_init (area);
		}

	      logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_recovery_analysis");
	      return ER_FAILED;
	    }

	  if (tdes->topops.max == 0 || (tdes->topops.last + 1) >= tdes->topops.max)
	    {
	      if (logtb_realloc_topops_stack (tdes, chkpt.ntops) == NULL)
		{
		  if (area != NULL)
		    {
		      free_and_init (area);
		    }

		  logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_recovery_analysis");
		  return ER_OUT_OF_VIRTUAL_MEMORY;
		}
	    }

	  tdes->topops.last++;
	  tdes->rcv.sysop_start_postpone_lsa = chkpt_topone->sysop_start_postpone_lsa;
	  log_lsa_local = chkpt_topone->sysop_start_postpone_lsa;
	  error_code =
	    log_read_sysop_start_postpone (thread_p, &log_lsa_local, log_page_local, false, &sysop_start_postpone,
					   NULL, NULL, NULL, NULL);
	  if (error_code != NO_ERROR)
	    {
	      assert (false);
	      return error_code;
	    }
	  tdes->topops.stack[tdes->topops.last].lastparent_lsa = sysop_start_postpone.sysop_end.lastparent_lsa;
	  tdes->topops.stack[tdes->topops.last].posp_lsa = sysop_start_postpone.posp_lsa;
	}
    }

  if (LSA_LT (&chkpt.redo_lsa, start_redo_lsa))
    {
      LSA_COPY (start_redo_lsa, &chkpt.redo_lsa);
    }

  if (area != NULL)
    {
      free_and_init (area);
    }

  return NO_ERROR;
}

/*
 * log_rv_analysis_save_point -
 *
 * return: error code
 *
 *   tran_id(in):
 *   lsa(in/out):
 *
 * Note:
 */
static int
log_rv_analysis_save_point (THREAD_ENTRY * thread_p, int tran_id, LOG_LSA * log_lsa)
{
  LOG_TDES *tdes;

  /* 
   * If this is the first time, the transaction is seen. Assign a new
   * index to describe it and assume that the transaction was active
   * at the time of the crash, and thus it will be unilateraly
   * aborted. The truth of this statement will be find reading the
   * rest of the log
   */
  tdes = logtb_rv_find_allocate_tran_index (thread_p, tran_id, log_lsa);
  if (tdes == NULL)
    {
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_rv_analysis_save_point");
      return ER_FAILED;
    }

  /* New tail, next to undo and savepoint */
  LSA_COPY (&tdes->tail_lsa, log_lsa);
  LSA_COPY (&tdes->undo_nxlsa, &tdes->tail_lsa);
  LSA_COPY (&tdes->savept_lsa, &tdes->tail_lsa);

  return NO_ERROR;
}

/*
 * log_rv_analysis_2pc_prepare -
 *
 * return: error code
 *
 *   tran_id(in):
 *   lsa(in/out):
 *
 * Note:
 */
static int
log_rv_analysis_2pc_prepare (THREAD_ENTRY * thread_p, int tran_id, LOG_LSA * log_lsa)
{
  LOG_TDES *tdes;

  /* 
   * If this is the first time, the transaction is seen. Assign a new
   * index to describe it. The transaction has agreed not to
   * unilaterally abort the transaction.
   * This is a participant of the transaction
   */

  tdes = logtb_rv_find_allocate_tran_index (thread_p, tran_id, log_lsa);
  if (tdes == NULL)
    {
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_rv_analysis_2pc_prepare");
      return ER_FAILED;
    }

  tdes->state = TRAN_UNACTIVE_2PC_PREPARE;
  LSA_COPY (&tdes->tail_lsa, log_lsa);

  /* Put a note that prepare_to_commit log record needs to be read during either redo phase, or during
   * finish_commit_protocol phase */
  tdes->gtrid = LOG_2PC_NULL_GTRID;

  return NO_ERROR;
}

/*
 * log_rv_analysis_2pc_start -
 *
 * return: error code
 *
 *   tran_id(in):
 *   lsa(in/out):
 *
 * Note:
 */
static int
log_rv_analysis_2pc_start (THREAD_ENTRY * thread_p, int tran_id, LOG_LSA * log_lsa)
{
  LOG_TDES *tdes;

  /* 
   * If this is the first time, the transaction is seen. Assign a new
   * index to describe it. The transaction was part of the two phase
   * commit process. This is a coordinator site.
   */
  tdes = logtb_rv_find_allocate_tran_index (thread_p, tran_id, log_lsa);
  if (tdes == NULL)
    {
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_rv_analysis_2pc_start");
      return ER_FAILED;
    }

  tdes->state = TRAN_UNACTIVE_2PC_COLLECTING_PARTICIPANT_VOTES;
  LSA_COPY (&tdes->tail_lsa, log_lsa);

  /* Put a note that prepare_to_commit log record needs to be read during either redo phase, or during
   * finish_commit_protocol phase */
  tdes->gtrid = LOG_2PC_NULL_GTRID;

  return NO_ERROR;
}

/*
 * log_rv_analysis_2pc_commit_decision -
 *
 * return: error code
 *
 *   tran_id(in):
 *   lsa(in/out):
 *
 * Note:
 */
static int
log_rv_analysis_2pc_commit_decision (THREAD_ENTRY * thread_p, int tran_id, LOG_LSA * log_lsa)
{
  LOG_TDES *tdes;

  /* 
   * If this is the first time, the transaction is seen. Assign a new
   * index to describe it. The transaction was part of the two phase
   * commit process. A commit decsion has been agreed.
   * This is a coordinator site.
   */
  tdes = logtb_rv_find_allocate_tran_index (thread_p, tran_id, log_lsa);
  if (tdes == NULL)
    {
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_rv_analysis_2pc_commit_decision");
      return ER_FAILED;
    }

  tdes->state = TRAN_UNACTIVE_2PC_COMMIT_DECISION;
  LSA_COPY (&tdes->tail_lsa, log_lsa);

  return NO_ERROR;
}

/*
 * log_rv_analysis_2pc_abort_decision -
 *
 * return: error code
 *
 *   tran_id(in):
 *   lsa(in/out):
 *
 * Note:
 */
static int
log_rv_analysis_2pc_abort_decision (THREAD_ENTRY * thread_p, int tran_id, LOG_LSA * log_lsa)
{
  LOG_TDES *tdes;

  /* 
   * If this is the first time, the transaction is seen. Assign a new
   * index to describe it. The transaction was part of the two phase
   * commit process. An abort decsion has been decided.
   * This is a coordinator site.
   */
  tdes = logtb_rv_find_allocate_tran_index (thread_p, tran_id, log_lsa);
  if (tdes == NULL)
    {
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_rv_analysis_2pc_abort_decision");
      return ER_FAILED;
    }

  tdes->state = TRAN_UNACTIVE_2PC_ABORT_DECISION;
  LSA_COPY (&tdes->tail_lsa, log_lsa);

  return NO_ERROR;
}

/*
 * log_rv_analysis_2pc_commit_inform_particps -
 *
 * return: error code
 *
 *   tran_id(in):
 *   lsa(in/out):
 *
 * Note:
 */
static int
log_rv_analysis_2pc_commit_inform_particps (THREAD_ENTRY * thread_p, int tran_id, LOG_LSA * log_lsa)
{
  LOG_TDES *tdes;

  /* 
   * If this is the first time, the transaction is seen. Assign a new
   * index to describe it. The transaction was part of the two phase
   * commit process. A commit decsion has been agreed and the
   * transaction was waiting on acknowledgment from participants
   * This is a coordinator site.
   */
  tdes = logtb_rv_find_allocate_tran_index (thread_p, tran_id, log_lsa);
  if (tdes == NULL)
    {
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_rv_analysis_2pc_commit_inform_particps");
      return ER_FAILED;
    }

  tdes->state = TRAN_UNACTIVE_COMMITTED_INFORMING_PARTICIPANTS;
  LSA_COPY (&tdes->tail_lsa, log_lsa);

  return NO_ERROR;
}

/*
 * log_rv_analysis_2pc_abort_inform_particps -
 *
 * return: error code
 *
 *   tran_id(in):
 *   lsa(in/out):
 *
 * Note:
 */
static int
log_rv_analysis_2pc_abort_inform_particps (THREAD_ENTRY * thread_p, int tran_id, LOG_LSA * log_lsa)
{
  LOG_TDES *tdes;

  /* 
   * If this is the first time, the transaction is seen. Assign a new
   * index to describe it. The transaction was part of the two phase
   * commit process. An abort decsion has been decided and the
   * transaction was waiting on acknowledgment from participants
   * This is a coordinator site.
   */
  tdes = logtb_rv_find_allocate_tran_index (thread_p, tran_id, log_lsa);
  if (tdes == NULL)
    {
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_recovery_analysis");
      return ER_FAILED;
    }

  tdes->state = TRAN_UNACTIVE_ABORTED_INFORMING_PARTICIPANTS;
  LSA_COPY (&tdes->tail_lsa, log_lsa);

  return NO_ERROR;
}

/*
 * log_rv_analysis_2pc_recv_ack -
 *
 * return: error code
 *
 *   tran_id(in):
 *   lsa(in/out):
 *
 * Note:
 */
static int
log_rv_analysis_2pc_recv_ack (THREAD_ENTRY * thread_p, int tran_id, LOG_LSA * log_lsa)
{
  LOG_TDES *tdes;

  tdes = logtb_rv_find_allocate_tran_index (thread_p, tran_id, log_lsa);
  if (tdes == NULL)
    {
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_recovery_analysis");
      return ER_FAILED;
    }

  LSA_COPY (&tdes->tail_lsa, log_lsa);

  return NO_ERROR;
}

/*
 * log_rv_analysis_log_end -
 *
 * return: error code
 *
 *   tran_id(in):
 *   lsa(in/out):
 *
 * Note:
 */
static int
log_rv_analysis_log_end (int tran_id, LOG_LSA * log_lsa)
{
  if (!logpb_is_page_in_archive (log_lsa->pageid))
    {
      /* 
       * Reset the log header for the recovery undo operation
       */
      LOG_RESET_APPEND_LSA (log_lsa);
      log_Gl.hdr.next_trid = tran_id;
    }

  return NO_ERROR;
}

/*
 * log_rv_analysis_record -
 *
 * return: error code
 *
 *
 * Note:
 */
static void
log_rv_analysis_record (THREAD_ENTRY * thread_p, LOG_RECTYPE log_type, int tran_id, LOG_LSA * log_lsa,
			LOG_PAGE * log_page_p, LOG_LSA * checkpoint_lsa, LOG_LSA * prev_lsa, LOG_LSA * start_lsa,
			LOG_LSA * start_redo_lsa, bool is_media_crash, time_t * stop_at, bool * did_incom_recovery,
			bool * may_use_checkpoint, bool * may_need_synch_checkpoint_2pc)
{
  switch (log_type)
    {
    case LOG_UNDOREDO_DATA:
    case LOG_DIFF_UNDOREDO_DATA:
    case LOG_UNDO_DATA:
    case LOG_REDO_DATA:
    case LOG_MVCC_UNDOREDO_DATA:
    case LOG_MVCC_DIFF_UNDOREDO_DATA:
    case LOG_MVCC_UNDO_DATA:
    case LOG_MVCC_REDO_DATA:
    case LOG_DBEXTERN_REDO_DATA:
      (void) log_rv_analysis_undo_redo (thread_p, tran_id, log_lsa);
      break;

    case LOG_DUMMY_HEAD_POSTPONE:
      (void) log_rv_analysis_dummy_head_postpone (thread_p, tran_id, log_lsa);
      break;

    case LOG_POSTPONE:
      (void) log_rv_analysis_postpone (thread_p, tran_id, log_lsa);
      break;

    case LOG_RUN_POSTPONE:
      (void) log_rv_analysis_run_postpone (thread_p, tran_id, log_lsa, log_page_p, checkpoint_lsa);
      break;

    case LOG_COMPENSATE:
      (void) log_rv_analysis_compensate (thread_p, tran_id, log_lsa, log_page_p);
      break;

    case LOG_WILL_COMMIT:
      (void) log_rv_analysis_will_commit (thread_p, tran_id, log_lsa);
      break;

    case LOG_COMMIT_WITH_POSTPONE:
      (void) log_rv_analysis_commit_with_postpone (thread_p, tran_id, log_lsa, log_page_p);
      break;

    case LOG_SYSOP_START_POSTPONE:
      (void) log_rv_analysis_sysop_start_postpone (thread_p, tran_id, log_lsa, log_page_p);
      break;

    case LOG_COMMIT:
    case LOG_ABORT:
      (void) log_rv_analysis_complete (thread_p, tran_id, log_lsa, log_page_p, prev_lsa, is_media_crash, stop_at,
				       did_incom_recovery);
      break;

    case LOG_SYSOP_END:
      log_rv_analysis_sysop_end (thread_p, tran_id, log_lsa, log_page_p);
      break;

    case LOG_START_CHKPT:
      log_rv_analysis_start_checkpoint (log_lsa, start_lsa, may_use_checkpoint);
      break;

    case LOG_END_CHKPT:
      log_rv_analysis_end_checkpoint (thread_p, log_lsa, log_page_p, checkpoint_lsa, start_redo_lsa, may_use_checkpoint,
				      may_need_synch_checkpoint_2pc);
      break;

    case LOG_SAVEPOINT:
      (void) log_rv_analysis_save_point (thread_p, tran_id, log_lsa);
      break;

    case LOG_2PC_PREPARE:
      (void) log_rv_analysis_2pc_prepare (thread_p, tran_id, log_lsa);
      break;

    case LOG_2PC_START:
      (void) log_rv_analysis_2pc_start (thread_p, tran_id, log_lsa);
      break;

    case LOG_2PC_COMMIT_DECISION:
      (void) log_rv_analysis_2pc_commit_decision (thread_p, tran_id, log_lsa);
      break;

    case LOG_2PC_ABORT_DECISION:
      (void) log_rv_analysis_2pc_abort_decision (thread_p, tran_id, log_lsa);
      break;

    case LOG_2PC_COMMIT_INFORM_PARTICPS:
      (void) log_rv_analysis_2pc_commit_inform_particps (thread_p, tran_id, log_lsa);
      break;

    case LOG_2PC_ABORT_INFORM_PARTICPS:
      (void) log_rv_analysis_2pc_abort_inform_particps (thread_p, tran_id, log_lsa);
      break;

    case LOG_2PC_RECV_ACK:
      (void) log_rv_analysis_2pc_recv_ack (thread_p, tran_id, log_lsa);
      break;

    case LOG_END_OF_LOG:
      (void) log_rv_analysis_log_end (tran_id, log_lsa);
      break;

    case LOG_DUMMY_CRASH_RECOVERY:
    case LOG_REPLICATION_DATA:
    case LOG_REPLICATION_STATEMENT:
    case LOG_DUMMY_HA_SERVER_STATE:
    case LOG_DUMMY_OVF_RECORD:
    case LOG_DUMMY_GENERIC:
      break;

    case LOG_SMALLER_LOGREC_TYPE:
    case LOG_LARGER_LOGREC_TYPE:
    default:
#if defined(CUBRID_DEBUG)
      er_log_debug (ARG_FILE_LINE,
		    "log_recovery_analysis: Unknown record type = %d (%s) ... May be a system error\n", log_rtype,
		    log_to_string (log_rtype));
#endif /* CUBRID_DEBUG */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_PAGE_CORRUPTED, 1, log_lsa->pageid);
      break;
    }
}

/*
 * log_recovery_analysis - FIND STATE OF TRANSACTIONS AT SYSTEM CRASH
 *
 * return: nothing
 *
 *   start_lsa(in): Starting address for the analysis phase
 *   start_redolsa(in/out): Starting address for redo phase
 *   end_redo_lsa(in):
 *   ismedia_crash(in): Are we recovering from a media crash ?
 *   stopat(in/out): Where to stop the recovery process.
 *                   (It may be set as a side effectto the location of last
 *                    recovery transaction).
 *   did_incom_recovery(in):
 *
 * NOTE: The recovery analysis phase scans the log forward since the
 *              last checkpoint record reflected in the log and the data
 *              volumes. The transaction table and the starting address for
 *              redo phase is created. When this phase is finished, we know
 *              the transactions that need to be unilateraly aborted (active)
 *              and the transactions that have to be completed due to postpone
 *              actions and client loose ends.
 */

static void
log_recovery_analysis (THREAD_ENTRY * thread_p, LOG_LSA * start_lsa, LOG_LSA * start_redo_lsa, LOG_LSA * end_redo_lsa,
		       int is_media_crash, time_t * stop_at, bool * did_incom_recovery, INT64 * num_redo_log_records)
{
  LOG_LSA checkpoint_lsa = { -1, -1 };
  LOG_LSA lsa;			/* LSA of log record to analyse */
  char log_pgbuf[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT], *aligned_log_pgbuf;
  LOG_PAGE *log_page_p = NULL;	/* Log page pointer where LSA is located */
  LOG_LSA log_lsa, prev_lsa;
  LOG_RECTYPE log_rtype;	/* Log record type */
  LOG_RECORD_HEADER *log_rec = NULL;	/* Pointer to log record */
  LOG_REC_CHKPT *tmp_chkpt;	/* Temp Checkpoint log record */
  LOG_REC_CHKPT chkpt;		/* Checkpoint log record */
  LOG_INFO_CHKPT_TRANS *chkpt_trans;
  time_t last_at_time = -1;
  char time_val[CTIME_MAX];
  bool may_need_synch_checkpoint_2pc = false;
  bool may_use_checkpoint = false;
  int tran_index;
  TRANID tran_id;
  LOG_TDES *tdes;		/* Transaction descriptor */
  void *area = NULL;
  int size;
  int i;

  aligned_log_pgbuf = PTR_ALIGN (log_pgbuf, MAX_ALIGNMENT);

  if (num_redo_log_records != NULL)
    {
      *num_redo_log_records = 0;
    }

  /* 
   * Find the committed, aborted, and unilaterrally aborted (active)
   * transactions at system crash
   */

  LSA_COPY (&lsa, start_lsa);

  LSA_COPY (start_redo_lsa, &lsa);
  LSA_COPY (end_redo_lsa, &lsa);
  LSA_COPY (&prev_lsa, &lsa);
  *did_incom_recovery = false;

  log_page_p = (LOG_PAGE *) aligned_log_pgbuf;

  while (!LSA_ISNULL (&lsa))
    {
      /* Fetch the page where the LSA record to undo is located */
      LSA_COPY (&log_lsa, &lsa);
      if (logpb_fetch_page (thread_p, &log_lsa, LOG_CS_FORCE_USE, log_page_p) != NO_ERROR)
	{
	  if (is_media_crash == true)
	    {
	      if (stop_at != NULL)
		{
		  *stop_at = last_at_time;
		}

#if !defined(NDEBUG)
	      if (prm_get_bool_value (PRM_ID_LOG_TRACE_DEBUG))
		{
		  fprintf (stdout, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_LOG, MSGCAT_LOG_STARTS));
		  (void) ctime_r (&last_at_time, time_val);
		  fprintf (stdout,
			   msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_LOG, MSGCAT_LOG_INCOMPLTE_MEDIA_RECOVERY),
			   end_redo_lsa->pageid, end_redo_lsa->offset, ((last_at_time == -1) ? "???...\n" : time_val));
		  fprintf (stdout, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_LOG, MSGCAT_LOG_STARTS));
		  fflush (stdout);
		}
#endif /* !NDEBUG */
	      /* if previous log record exists, reset tdes->tail_lsa/undo_nxlsa as previous of end_redo_lsa */
	      if (log_rec != NULL)
		{
		  LOG_TDES *last_log_tdes = LOG_FIND_TDES (logtb_find_tran_index (thread_p, log_rec->trid));
		  if (last_log_tdes != NULL)
		    {
		      LSA_COPY (&last_log_tdes->tail_lsa, &log_rec->prev_tranlsa);
		      LSA_COPY (&last_log_tdes->undo_nxlsa, &log_rec->prev_tranlsa);
		    }
		}
	      log_recovery_resetlog (thread_p, &lsa, true, end_redo_lsa);
	      *did_incom_recovery = true;

	      logtb_reset_bit_area_start_mvccid ();
	      return;
	    }
	  else
	    {
	      logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_recovery_analysis");
	      return;
	    }
	}

      /* Check all log records in this phase */
      while (!LSA_ISNULL (&lsa) && lsa.pageid == log_lsa.pageid)
	{
	  /* 
	   * If an offset is missing, it is because an incomplete log record was
	   * archived. This log_record was completed later. Thus, we have to
	   * find the offset by searching for the next log_record in the page
	   */
	  if (lsa.offset == NULL_OFFSET)
	    {
	      lsa.offset = log_page_p->hdr.offset;
	      if (lsa.offset == NULL_OFFSET)
		{
		  /* Continue with next pageid */
		  if (logpb_is_page_in_archive (log_lsa.pageid))
		    {
		      lsa.pageid = log_lsa.pageid + 1;
		    }
		  else
		    {
		      lsa.pageid = NULL_PAGEID;
		    }
		  continue;
		}
	    }

	  /* Find the log record */
	  log_lsa.offset = lsa.offset;
	  log_rec = LOG_GET_LOG_RECORD_HEADER (log_page_p, &log_lsa);

	  tran_id = log_rec->trid;
	  log_rtype = log_rec->type;

	  /* 
	   * Save the address of last redo log record.
	   * Get the address of next log record to scan
	   */

	  LSA_COPY (end_redo_lsa, &lsa);
	  LSA_COPY (&lsa, &log_rec->forw_lsa);

	  /* 
	   * If the next page is NULL_PAGEID and the current page is an archive
	   * page, this is not the end of the log. This situation happens when an
	   * incomplete log record is archived. Thus, its forward address is NULL.
	   * Note that we have to set lsa.pageid here since the log_lsa.pageid value
	   * can be changed (e.g., the log record is stored in two pages: an
	   * archive page, and an active page. Later, we try to modify it whenever
	   * is possible.
	   */

	  if (LSA_ISNULL (&lsa) && logpb_is_page_in_archive (log_lsa.pageid))
	    {
	      lsa.pageid = log_lsa.pageid + 1;
	    }

	  if (!LSA_ISNULL (&lsa) && log_lsa.pageid != NULL_PAGEID
	      && (lsa.pageid < log_lsa.pageid || (lsa.pageid == log_lsa.pageid && lsa.offset <= log_lsa.offset)))
	    {
	      /* It seems to be a system error. Maybe a loop in the log */
	      er_log_debug (ARG_FILE_LINE,
			    "log_recovery_analysis: ** System error: It seems to be a loop in the log\n."
			    " Current log_rec at %lld|%d. Next log_rec at %lld|%d\n", (long long int) log_lsa.pageid,
			    log_lsa.offset, (long long int) lsa.pageid, lsa.offset);
	      logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_recovery_analysis");
	      LSA_SET_NULL (&lsa);
	      break;
	    }

	  if (LSA_ISNULL (&lsa) && log_rtype != LOG_END_OF_LOG && *did_incom_recovery == false)
	    {
	      LOG_RESET_APPEND_LSA (end_redo_lsa);
	      if (log_startof_nxrec (thread_p, &log_Gl.hdr.append_lsa, true) == NULL)
		{
		  /* We may destroy a record */
		  LOG_RESET_APPEND_LSA (end_redo_lsa);
		}
	      else
		{
		  LOG_RESET_APPEND_LSA (&log_Gl.hdr.append_lsa);

		  /* 
		   * Reset the forward address of current record to next record,
		   * and then flush the page.
		   */
		  LSA_COPY (&log_rec->forw_lsa, &log_Gl.hdr.append_lsa);

		  assert (log_lsa.pageid == log_page_p->hdr.logical_pageid);
		  logpb_write_page_to_disk (thread_p, log_page_p, log_lsa.pageid);
		}
	      er_log_debug (ARG_FILE_LINE,
			    "log_recovery_analysis: ** WARNING: An end of the log record was not found."
			    " Will Assume = %lld|%d and Next Trid = %d\n", (long long int) log_Gl.hdr.append_lsa.pageid,
			    log_Gl.hdr.append_lsa.offset, tran_id);
	      log_Gl.hdr.next_trid = tran_id;
	    }

	  if (num_redo_log_records)
	    {
	      switch (log_rtype)
		{
		  /* count redo log */
		case LOG_REDO_DATA:
		case LOG_UNDOREDO_DATA:
		case LOG_DIFF_UNDOREDO_DATA:
		case LOG_DBEXTERN_REDO_DATA:
		case LOG_MVCC_REDO_DATA:
		case LOG_MVCC_UNDOREDO_DATA:
		case LOG_MVCC_DIFF_UNDOREDO_DATA:
		case LOG_RUN_POSTPONE:
		case LOG_COMPENSATE:
		case LOG_2PC_PREPARE:
		case LOG_2PC_START:
		case LOG_2PC_RECV_ACK:
		  (*num_redo_log_records)++;
		  break;
		default:
		  break;
		}
	    }

	  log_rv_analysis_record (thread_p, log_rtype, tran_id, &log_lsa, log_page_p, &checkpoint_lsa, &prev_lsa,
				  start_lsa, start_redo_lsa, is_media_crash, stop_at, did_incom_recovery,
				  &may_use_checkpoint, &may_need_synch_checkpoint_2pc);
	  if (*did_incom_recovery == true)
	    {
	      LSA_SET_NULL (&lsa);
	      break;
	    }
	  if (LSA_EQ (end_redo_lsa, &lsa))
	    {
	      assert_release (!LSA_EQ (end_redo_lsa, &lsa));
	      LSA_SET_NULL (&lsa);
	      break;
	    }

	  LSA_COPY (&prev_lsa, end_redo_lsa);

	  /* 
	   * We can fix the lsa.pageid in the case of log_records without forward
	   * address at this moment.
	   */
	  if (lsa.offset == NULL_OFFSET && lsa.pageid != NULL_PAGEID && lsa.pageid < log_lsa.pageid)
	    {
	      lsa.pageid = log_lsa.pageid;
	    }
	}
    }

  if (may_need_synch_checkpoint_2pc == true)
    {
      /* 
       * We may need to obtain 2PC information of distributed transactions that
       * were in the 2PC at the time of the checkpoint and they were still 2PC
       * at the time of the crash.
       * GET  the checkpoint log record information one more time..
       */
      log_lsa.pageid = checkpoint_lsa.pageid;
      log_lsa.offset = checkpoint_lsa.offset;

      log_page_p = (LOG_PAGE *) aligned_log_pgbuf;

      if (logpb_fetch_page (thread_p, &log_lsa, LOG_CS_FORCE_USE, log_page_p) != NO_ERROR)
	{
	  /* 
	   * There is a problem. We have just read this page a little while ago
	   */
	  logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_recovery_analysis");
	  return;
	}

      log_rec = LOG_GET_LOG_RECORD_HEADER (log_page_p, &log_lsa);

      /* Read the DATA HEADER */
      LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_RECORD_HEADER), &log_lsa, log_page_p);
      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (LOG_REC_CHKPT), &log_lsa, log_page_p);
      tmp_chkpt = (LOG_REC_CHKPT *) ((char *) log_page_p->area + log_lsa.offset);
      chkpt = *tmp_chkpt;

      /* GET THE CHECKPOINT TRANSACTION INFORMATION */
      LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_REC_CHKPT), &log_lsa, log_page_p);

      /* Now get the data of active transactions */
      area = NULL;
      size = sizeof (LOG_INFO_CHKPT_TRANS) * chkpt.ntrans;
      if (log_lsa.offset + size < (int) LOGAREA_SIZE)
	{
	  chkpt_trans = (LOG_INFO_CHKPT_TRANS *) ((char *) log_page_p->area + log_lsa.offset);
	}
      else
	{
	  /* Need to copy the data into a contiguous area */
	  area = malloc (size);
	  if (area == NULL)
	    {
	      logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_recovery_analysis");
	      return;
	    }
	  /* Copy the data */
	  logpb_copy_from_log (thread_p, (char *) area, size, &log_lsa, log_page_p);
	  chkpt_trans = (LOG_INFO_CHKPT_TRANS *) area;
	}

      /* Add the transactions to the transaction table */
      for (i = 0; i < chkpt.ntrans; i++)
	{
	  tran_index = logtb_find_tran_index (thread_p, chkpt_trans[i].trid);
	  if (tran_index != NULL_TRAN_INDEX)
	    {
	      tdes = LOG_FIND_TDES (tran_index);
	      if (tdes != NULL && LOG_ISTRAN_2PC (tdes))
		{
		  log_2pc_recovery_analysis_info (thread_p, tdes, &chkpt_trans[i].tail_lsa);
		}
	    }
	}
      if (area != NULL)
	{
	  free_and_init (area);
	}
    }

  logtb_reset_bit_area_start_mvccid ();

  return;
}

/*
 * log_recovery_redo - SCAN FORWARD REDOING DATA
 *
 * return: nothing
 *
 *   start_redolsa(in): Starting address for recovery redo phase
 *   end_redo_lsa(in):
 *   stopat(in):
 *
 * NOTE:In the redo phase, updates that are not reflected in the
 *              database are repeated for not only the committed transaction
 *              but also for all aborted transactions and the transactions
 *              that were in progress at the time of the failure. This phase
 *              reestablishes the state of the database as of the time of the
 *              failure. The redo phase starts by scanning the log records
 *              from the redo LSA address determined in the analysis phase.
 *              When a redoable record is found, a check is done to find out
 *              if the redo action is already reflected in the page. If it is
 *              not, then the redo actions are executed. A redo action can be
 *              skipped if the LSA of the affected page is greater or equal
 *              than that of the LSA of the log record. Any postpone actions
 *              (after commit actions) of committed transactions that have not
 *              been executed are done. Loose_ends of client actions that have
 *              not been done are postponed until the client is restarted.
 *              At the end of the recovery phase, all data dirty pages are
 *              flushed.
 *              The redo of aborted transactions are undone executing its
 *              respective compensating log records.
 */
static void
log_recovery_redo (THREAD_ENTRY * thread_p, const LOG_LSA * start_redolsa, const LOG_LSA * end_redo_lsa,
		   time_t * stopat)
{
  LOG_LSA lsa;			/* LSA of log record to redo */
  char log_pgbuf[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT], *aligned_log_pgbuf;
  LOG_PAGE *log_pgptr = NULL;	/* Log page pointer where LSA is located */
  LOG_LSA log_lsa;
  LOG_RECORD_HEADER *log_rec = NULL;	/* Pointer to log record */
  LOG_REC_UNDOREDO *undoredo = NULL;	/* Undo_redo log record */
  LOG_REC_MVCC_UNDOREDO *mvcc_undoredo = NULL;	/* MVCC op undo/redo log record */
  LOG_REC_REDO *redo = NULL;	/* Redo log record */
  LOG_REC_MVCC_REDO *mvcc_redo = NULL;	/* MVCC op redo log record */
  LOG_REC_MVCC_UNDO *mvcc_undo = NULL;	/* MVCC op undo log record */
  LOG_REC_DBOUT_REDO *dbout_redo = NULL;	/* A external redo log record */
  LOG_REC_COMPENSATE *compensate = NULL;	/* Compensating log record */
  LOG_REC_RUN_POSTPONE *run_posp = NULL;	/* A run postpone action */
  LOG_REC_2PC_START *start_2pc = NULL;	/* Start 2PC commit log record */
  LOG_REC_2PC_PARTICP_ACK *received_ack = NULL;	/* A 2PC participant ack */
  LOG_REC_DONETIME *donetime = NULL;
  LOG_RCV rcv;			/* Recovery structure */
  VPID rcv_vpid;		/* VPID of data to recover */
  LOG_RCVINDEX rcvindex;	/* Recovery index function */
  LOG_LSA rcv_lsa;		/* Address of redo log record */
  LOG_LSA *rcv_page_lsaptr;	/* LSA of data page for log record to redo */
  LOG_TDES *tdes;		/* Transaction descriptor */
  int num_particps;		/* Number of participating sites */
  int particp_id_length;	/* Length of particp_ids block */
  void *block_particps_ids;	/* A block of participant ids */
  int temp_length;
  int tran_index;
  volatile TRANID tran_id;
  volatile LOG_RECTYPE log_rtype;
  int i;
  int data_header_size = 0;
  MVCCID mvccid = MVCCID_NULL;
  LOG_ZIP *undo_unzip_ptr = NULL;
  LOG_ZIP *redo_unzip_ptr = NULL;
  bool is_diff_rec;
  bool is_mvcc_op = false;

  aligned_log_pgbuf = PTR_ALIGN (log_pgbuf, MAX_ALIGNMENT);

  /* 
   * GO FORWARD, redoing records of all transactions including aborted ones.
   *
   * Compensating records undo the redo of already executed undo records.
   * Transactions that were active at the time of the crash are aborted
   * during the log_recovery_undo phase
   */

  LSA_COPY (&lsa, start_redolsa);

  /* Defense for illegal start_redolsa */
  if ((lsa.offset + (int) sizeof (LOG_RECORD_HEADER)) >= LOGAREA_SIZE)
    {
      assert (false);
      /* move first record of next page */
      lsa.pageid++;
      lsa.offset = NULL_OFFSET;
    }

  log_pgptr = (LOG_PAGE *) aligned_log_pgbuf;

  undo_unzip_ptr = log_zip_alloc (LOGAREA_SIZE, false);
  redo_unzip_ptr = log_zip_alloc (LOGAREA_SIZE, false);

  if (undo_unzip_ptr == NULL || redo_unzip_ptr == NULL)
    {
      if (undo_unzip_ptr)
	{
	  log_zip_free (undo_unzip_ptr);
	}
      if (redo_unzip_ptr)
	{
	  log_zip_free (redo_unzip_ptr);
	}
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_recovery_redo");
      return;
    }

  while (!LSA_ISNULL (&lsa))
    {
      /* Fetch the page where the LSA record to undo is located */
      LSA_COPY (&log_lsa, &lsa);
      if (logpb_fetch_page (thread_p, &log_lsa, LOG_CS_FORCE_USE, log_pgptr) != NO_ERROR)
	{
	  if (end_redo_lsa != NULL && (LSA_ISNULL (end_redo_lsa) || LSA_GT (&lsa, end_redo_lsa)))
	    {
	      goto exit;
	    }
	  else
	    {
	      LSA_SET_NULL (&log_Gl.unique_stats_table.curr_rcv_rec_lsa);
	      logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_recovery_redo");
	      return;
	    }
	}

      /* Check all log records in this phase */
      while (lsa.pageid == log_lsa.pageid)
	{
	  tdes = NULL;
	  /* 
	   * Do we want to stop the recovery redo process at this time ?
	   */
	  if (end_redo_lsa != NULL && !LSA_ISNULL (end_redo_lsa) && LSA_GT (&lsa, end_redo_lsa))
	    {
	      LSA_SET_NULL (&lsa);
	      break;
	    }

	  /* 
	   * If an offset is missing, it is because we archive an incomplete
	   * log record. This log_record was completed later. Thus, we have to
	   * find the offset by searching for the next log_record in the page
	   */
	  if (lsa.offset == NULL_OFFSET && (lsa.offset = log_pgptr->hdr.offset) == NULL_OFFSET)
	    {
	      /* Continue with next pageid */
	      if (logpb_is_page_in_archive (log_lsa.pageid))
		{
		  lsa.pageid = log_lsa.pageid + 1;
		}
	      else
		{
		  lsa.pageid = NULL_PAGEID;
		}
	      continue;
	    }

	  LSA_COPY (&log_Gl.unique_stats_table.curr_rcv_rec_lsa, &lsa);

	  /* Find the log record */
	  log_lsa.offset = lsa.offset;
	  log_rec = LOG_GET_LOG_RECORD_HEADER (log_pgptr, &log_lsa);

	  tran_id = log_rec->trid;
	  log_rtype = log_rec->type;

	  /* Get the address of next log record to scan */
	  LSA_COPY (&lsa, &log_rec->forw_lsa);

	  /* 
	   * If the next page is NULL_PAGEID and the current page is an archive
	   * page, this is not the end, this situation happens when an incomplete
	   * log record is archived. Thus, its forward address is NULL.
	   * Note that we have to set lsa.pageid here since the log_lsa.pageid value
	   * can be changed (e.g., the log record is stored in an archive page and
	   * in an active page. Later, we try to modify it whenever is possible.
	   */

	  if (LSA_ISNULL (&lsa) && logpb_is_page_in_archive (log_lsa.pageid))
	    {
	      lsa.pageid = log_lsa.pageid + 1;
	    }

	  if (!LSA_ISNULL (&lsa) && log_lsa.pageid != NULL_PAGEID
	      && (lsa.pageid < log_lsa.pageid || (lsa.pageid == log_lsa.pageid && lsa.offset <= log_lsa.offset)))
	    {
	      /* It seems to be a system error. Maybe a loop in the log */

	      LSA_SET_NULL (&log_Gl.unique_stats_table.curr_rcv_rec_lsa);

	      er_log_debug (ARG_FILE_LINE,
			    "log_recovery_redo: ** System error: It seems to be a loop in the log\n."
			    " Current log_rec at %lld|%d. Next log_rec at %lld|%d\n", (long long int) log_lsa.pageid,
			    log_lsa.offset, (long long int) lsa.pageid, lsa.offset);
	      logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_recovery_redo");
	      LSA_SET_NULL (&lsa);
	      break;
	    }

	  switch (log_rtype)
	    {
	    case LOG_MVCC_UNDOREDO_DATA:
	    case LOG_MVCC_DIFF_UNDOREDO_DATA:
	    case LOG_UNDOREDO_DATA:
	    case LOG_DIFF_UNDOREDO_DATA:
	      /* Is diff record type? */
	      if (log_rtype == LOG_DIFF_UNDOREDO_DATA || log_rtype == LOG_MVCC_DIFF_UNDOREDO_DATA)
		{
		  is_diff_rec = true;
		}
	      else
		{
		  is_diff_rec = false;
		}

	      /* Does record belong to a MVCC op */
	      if (log_rtype == LOG_MVCC_UNDOREDO_DATA || log_rtype == LOG_MVCC_DIFF_UNDOREDO_DATA)
		{
		  is_mvcc_op = true;
		}
	      else
		{
		  is_mvcc_op = false;
		}

	      /* REDO the record if needed */
	      LSA_COPY (&rcv_lsa, &log_lsa);

	      /* Get the DATA HEADER */
	      LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_RECORD_HEADER), &log_lsa, log_pgptr);

	      if (is_mvcc_op)
		{
		  /* Data header is a MVCC undoredo */
		  data_header_size = sizeof (LOG_REC_MVCC_UNDOREDO);
		  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, data_header_size, &log_lsa, log_pgptr);
		  mvcc_undoredo = (LOG_REC_MVCC_UNDOREDO *) ((char *) log_pgptr->area + log_lsa.offset);

		  /* Get undoredo structure */
		  undoredo = &mvcc_undoredo->undoredo;

		  /* Get transaction MVCCID */
		  mvccid = mvcc_undoredo->mvccid;

		  /* Check if MVCC next ID must be updated */
		  if (!MVCC_ID_PRECEDES (mvccid, log_Gl.hdr.mvcc_next_id))
		    {
		      log_Gl.hdr.mvcc_next_id = mvccid;
		      MVCCID_FORWARD (log_Gl.hdr.mvcc_next_id);
		    }
		}
	      else
		{
		  /* Data header is a regular undoredo */
		  data_header_size = sizeof (LOG_REC_UNDOREDO);
		  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, data_header_size, &log_lsa, log_pgptr);
		  undoredo = (LOG_REC_UNDOREDO *) ((char *) log_pgptr->area + log_lsa.offset);

		  mvccid = MVCCID_NULL;
		}

	      if (is_mvcc_op)
		{
		  /* Save last MVCC operation LOG_LSA. */
		  LSA_COPY (&log_Gl.hdr.mvcc_op_log_lsa, &rcv_lsa);
		}

	      /* Do we need to redo anything ? */

	      /* 
	       * Fetch the page for physical log records and check if redo
	       * is needed by comparing the log sequence numbers
	       */

	      rcv_vpid.volid = undoredo->data.volid;
	      rcv_vpid.pageid = undoredo->data.pageid;

	      rcv.pgptr = NULL;
	      rcvindex = undoredo->data.rcvindex;
	      /* If the page does not exit, there is nothing to redo */
	      if (rcv_vpid.pageid != NULL_PAGEID && rcv_vpid.volid != NULL_VOLID)
		{
		  rcv.pgptr = log_rv_redo_fix_page (thread_p, &rcv_vpid, rcvindex);
		  if (rcv.pgptr == NULL)
		    {
		      /* deallocated */
		      break;
		    }
		}

	      if (rcv.pgptr != NULL)
		{
		  rcv_page_lsaptr = pgbuf_get_lsa (rcv.pgptr);
		  /* 
		   * Do we need to execute the redo operation ?
		   * If page_lsa >= lsa... already updated. In this case make sure
		   * that the redo is not far from the end_redo_lsa
		   */
		  assert (end_redo_lsa == NULL || LSA_ISNULL (end_redo_lsa) || LSA_LE (rcv_page_lsaptr, end_redo_lsa));
		  if (LSA_LE (&rcv_lsa, rcv_page_lsaptr))
		    {
		      /* It is already done */
		      pgbuf_unfix (thread_p, rcv.pgptr);
		      break;
		    }
		}
	      else
		{
		  rcv_page_lsaptr = NULL;
		}

	      temp_length = undoredo->ulength;

	      rcv.length = undoredo->rlength;
	      rcv.offset = undoredo->data.offset;
	      rcv.mvcc_id = mvccid;

	      LOG_READ_ADD_ALIGN (thread_p, data_header_size, &log_lsa, log_pgptr);

	      if (is_diff_rec)
		{
		  /* Get Undo data */
		  if (!log_rv_get_unzip_log_data (thread_p, temp_length, &log_lsa, log_pgptr, undo_unzip_ptr))
		    {
		      LSA_SET_NULL (&log_Gl.unique_stats_table.curr_rcv_rec_lsa);
		      logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_recovery_redo");
		    }
		}
	      else
		{
		  temp_length = (int) GET_ZIP_LEN (temp_length);
		  /* Undo Data Pass */
		  if (log_lsa.offset + temp_length < (int) LOGAREA_SIZE)
		    {
		      log_lsa.offset += temp_length;
		    }
		  else
		    {
		      while (temp_length > 0)
			{
			  if (temp_length + log_lsa.offset >= (int) LOGAREA_SIZE)
			    {
			      LOG_LSA fetch_lsa;

			      temp_length -= LOGAREA_SIZE - (int) (log_lsa.offset);
			      assert (log_pgptr != NULL);

			      fetch_lsa.pageid = ++log_lsa.pageid;
			      fetch_lsa.offset = LOG_PAGESIZE;

			      if ((logpb_fetch_page (thread_p, &fetch_lsa, LOG_CS_FORCE_USE, log_pgptr)) != NO_ERROR)
				{
				  LSA_SET_NULL (&log_Gl.unique_stats_table.curr_rcv_rec_lsa);
				  logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_recovery_redo");
				  return;
				}
			      log_lsa.offset = 0;
			      LOG_READ_ALIGN (thread_p, &log_lsa, log_pgptr);
			    }
			  else
			    {
			      log_lsa.offset += temp_length;
			      temp_length = 0;
			    }
			}
		    }
		}

	      /* GET AFTER DATA */
	      LOG_READ_ALIGN (thread_p, &log_lsa, log_pgptr);
#if !defined(NDEBUG)
	      if (prm_get_bool_value (PRM_ID_LOG_TRACE_DEBUG))
		{
		  fprintf (stdout,
			   "TRACE REDOING[1]: LSA = %lld|%d, Rv_index = %s,\n"
			   "      volid = %d, pageid = %d, offset = %d,\n", (long long int) rcv_lsa.pageid,
			   (int) rcv_lsa.offset, rv_rcvindex_string (rcvindex), rcv_vpid.volid, rcv_vpid.pageid,
			   rcv.offset);
		  if (rcv_page_lsaptr != NULL)
		    {
		      fprintf (stdout, "      page_lsa = %lld|%d\n", (long long int) rcv_page_lsaptr->pageid,
			       rcv_page_lsaptr->offset);
		    }
		  else
		    {
		      fprintf (stdout, "      page_lsa = %d|%d\n", -1, -1);
		    }
		  fflush (stdout);
		}
#endif /* !NDEBUG */

	      if (is_diff_rec)
		{
		  /* XOR Process */
		  log_rv_redo_record (thread_p, &log_lsa, log_pgptr, RV_fun[rcvindex].redofun, &rcv, &rcv_lsa,
				      (int) undo_unzip_ptr->data_length, (char *) undo_unzip_ptr->log_data,
				      redo_unzip_ptr);
		}
	      else
		{
		  log_rv_redo_record (thread_p, &log_lsa, log_pgptr, RV_fun[rcvindex].redofun, &rcv, &rcv_lsa, 0, NULL,
				      redo_unzip_ptr);
		}
	      if (rcv.pgptr != NULL)
		{
		  pgbuf_unfix (thread_p, rcv.pgptr);
		}
	      break;

	    case LOG_MVCC_REDO_DATA:
	    case LOG_REDO_DATA:
	      /* Does log record belong to a MVCC op? */
	      is_mvcc_op = (log_rtype == LOG_MVCC_REDO_DATA);

	      LSA_COPY (&rcv_lsa, &log_lsa);

	      /* Get the DATA HEADER */
	      LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_RECORD_HEADER), &log_lsa, log_pgptr);

	      if (is_mvcc_op)
		{
		  /* Data header is MVCC redo */
		  data_header_size = sizeof (LOG_REC_MVCC_REDO);
		  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, data_header_size, &log_lsa, log_pgptr);
		  mvcc_redo = (LOG_REC_MVCC_REDO *) ((char *) log_pgptr->area + log_lsa.offset);
		  /* Get redo info */
		  redo = &mvcc_redo->redo;

		  /* Get transaction MVCCID */
		  mvccid = mvcc_redo->mvccid;

		  /* Check if MVCC next ID must be updated */
		  if (!MVCC_ID_PRECEDES (mvccid, log_Gl.hdr.mvcc_next_id))
		    {
		      log_Gl.hdr.mvcc_next_id = mvccid;
		      MVCCID_FORWARD (log_Gl.hdr.mvcc_next_id);
		    }
		}
	      else
		{
		  /* Data header is regular redo */
		  data_header_size = sizeof (LOG_REC_REDO);
		  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, data_header_size, &log_lsa, log_pgptr);

		  redo = (LOG_REC_REDO *) ((char *) log_pgptr->area + log_lsa.offset);

		  mvccid = MVCCID_NULL;
		}

	      /* Do we need to redo anything ? */

	      if (redo->data.rcvindex == RVVAC_COMPLETE)
		{
		  LOG_LSA null_lsa = LSA_INITIALIZER;

		  /* Reset log header MVCC info */
		  LSA_SET_NULL (&log_Gl.hdr.mvcc_op_log_lsa);
		  log_Gl.hdr.last_block_oldest_mvccid = MVCCID_NULL;
		  log_Gl.hdr.last_block_newest_mvccid = MVCCID_NULL;

		  /* Reset vacuum recover LSA */
		  vacuum_notify_server_crashed (&null_lsa);
		}

	      /* 
	       * Fetch the page for physical log records and check if redo
	       * is needed by comparing the log sequence numbers
	       */

	      rcv_vpid.volid = redo->data.volid;
	      rcv_vpid.pageid = redo->data.pageid;

	      rcv.pgptr = NULL;
	      rcvindex = redo->data.rcvindex;
	      /* If the page does not exit, there is nothing to redo */
	      if (rcv_vpid.pageid != NULL_PAGEID && rcv_vpid.volid != NULL_VOLID)
		{
		  rcv.pgptr = log_rv_redo_fix_page (thread_p, &rcv_vpid, rcvindex);
		  if (rcv.pgptr == NULL)
		    {
		      /* deallocated */
		      break;
		    }
		}

	      if (rcv.pgptr != NULL)
		{
		  rcv_page_lsaptr = pgbuf_get_lsa (rcv.pgptr);
		  /* 
		   * Do we need to execute the redo operation ?
		   * If page_lsa >= rcv_lsa... already updated
		   */
		  assert (end_redo_lsa == NULL || LSA_ISNULL (end_redo_lsa) || LSA_LE (rcv_page_lsaptr, end_redo_lsa));
		  if (LSA_LE (&rcv_lsa, rcv_page_lsaptr))
		    {
		      /* It is already done */
		      pgbuf_unfix (thread_p, rcv.pgptr);
		      break;
		    }
		}
	      else
		{
		  rcv.pgptr = NULL;
		  rcv_page_lsaptr = NULL;
		}

	      rcv.length = redo->length;
	      rcv.offset = redo->data.offset;
	      rcv.mvcc_id = mvccid;

	      LOG_READ_ADD_ALIGN (thread_p, data_header_size, &log_lsa, log_pgptr);

#if !defined(NDEBUG)
	      if (prm_get_bool_value (PRM_ID_LOG_TRACE_DEBUG))
		{
		  fprintf (stdout,
			   "TRACE REDOING[2]: LSA = %lld|%d, Rv_index = %s,\n"
			   "      volid = %d, pageid = %d, offset = %d,\n", (long long int) rcv_lsa.pageid,
			   (int) rcv_lsa.offset, rv_rcvindex_string (rcvindex), rcv_vpid.volid, rcv_vpid.pageid,
			   rcv.offset);
		  if (rcv_page_lsaptr != NULL)
		    {
		      fprintf (stdout, "      page_lsa = %lld|%d\n", (long long int) rcv_page_lsaptr->pageid,
			       rcv_page_lsaptr->offset);
		    }
		  else
		    {
		      fprintf (stdout, "      page_lsa = %lld|%d\n", -1LL, -1);
		    }
		  fflush (stdout);
		}
#endif /* !NDEBUG */

	      log_rv_redo_record (thread_p, &log_lsa, log_pgptr, RV_fun[rcvindex].redofun, &rcv, &rcv_lsa, 0, NULL,
				  redo_unzip_ptr);

	      if (rcv.pgptr != NULL)
		{
		  pgbuf_unfix (thread_p, rcv.pgptr);
		}
	      break;

	    case LOG_DBEXTERN_REDO_DATA:
	      LSA_COPY (&rcv_lsa, &log_lsa);

	      /* Get the DATA HEADER */
	      LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_RECORD_HEADER), &log_lsa, log_pgptr);
	      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (LOG_REC_DBOUT_REDO), &log_lsa, log_pgptr);

	      dbout_redo = ((LOG_REC_DBOUT_REDO *) ((char *) log_pgptr->area + log_lsa.offset));

	      VPID_SET_NULL (&rcv_vpid);
	      rcv.offset = -1;
	      rcv.pgptr = NULL;

	      rcvindex = dbout_redo->rcvindex;
	      rcv.length = dbout_redo->length;

	      /* GET AFTER DATA */
	      LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_REC_DBOUT_REDO), &log_lsa, log_pgptr);

#if !defined(NDEBUG)
	      if (prm_get_bool_value (PRM_ID_LOG_TRACE_DEBUG))
		{
		  fprintf (stdout, "TRACE EXT REDOING[3]: LSA = %lld|%d, Rv_index = %s\n",
			   (long long int) rcv_lsa.pageid, rcv_lsa.offset, rv_rcvindex_string (rcvindex));
		  fflush (stdout);
		}
#endif /* !NDEBUG */

	      log_rv_redo_record (thread_p, &log_lsa, log_pgptr, RV_fun[rcvindex].redofun, &rcv, &rcv_lsa, 0, NULL,
				  NULL);
	      break;

	    case LOG_RUN_POSTPONE:
	      LSA_COPY (&rcv_lsa, &log_lsa);

	      /* Get the DATA HEADER */
	      LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_RECORD_HEADER), &log_lsa, log_pgptr);
	      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (LOG_REC_RUN_POSTPONE), &log_lsa, log_pgptr);
	      run_posp = (LOG_REC_RUN_POSTPONE *) ((char *) log_pgptr->area + log_lsa.offset);

	      /* Do we need to redo anything ? */

	      /* 
	       * Fetch the page for physical log records and check if redo
	       * is needed by comparing the log sequence numbers
	       */

	      rcv_vpid.volid = run_posp->data.volid;
	      rcv_vpid.pageid = run_posp->data.pageid;

	      rcv.pgptr = NULL;
	      rcvindex = run_posp->data.rcvindex;
	      /* If the page does not exit, there is nothing to redo */
	      if (rcv_vpid.pageid != NULL_PAGEID && rcv_vpid.volid != NULL_VOLID)
		{
		  rcv.pgptr = log_rv_redo_fix_page (thread_p, &rcv_vpid, rcvindex);
		  if (rcv.pgptr == NULL)
		    {
		      /* deallocated */
		      break;
		    }
		}

	      if (rcv.pgptr != NULL)
		{
		  rcv_page_lsaptr = pgbuf_get_lsa (rcv.pgptr);
		  /* 
		   * Do we need to execute the redo operation ?
		   * If page_lsa >= rcv_lsa... already updated
		   */
		  assert (end_redo_lsa == NULL || LSA_ISNULL (end_redo_lsa) || LSA_LE (rcv_page_lsaptr, end_redo_lsa));
		  if (LSA_LE (&rcv_lsa, rcv_page_lsaptr))
		    {
		      /* It is already done */
		      pgbuf_unfix (thread_p, rcv.pgptr);
		      break;
		    }
		}
	      else
		{
		  rcv.pgptr = NULL;
		  rcv_page_lsaptr = NULL;
		}

	      rcv.length = run_posp->length;
	      rcv.offset = run_posp->data.offset;

	      LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_REC_RUN_POSTPONE), &log_lsa, log_pgptr);
	      /* GET AFTER DATA */
	      LOG_READ_ALIGN (thread_p, &log_lsa, log_pgptr);

#if !defined(NDEBUG)
	      if (prm_get_bool_value (PRM_ID_LOG_TRACE_DEBUG))
		{
		  fprintf (stdout,
			   "TRACE REDOING[4]: LSA = %lld|%d, Rv_index = %s,\n"
			   "      volid = %d, pageid = %d, offset = %d,\n", (long long int) rcv_lsa.pageid,
			   (int) rcv_lsa.offset, rv_rcvindex_string (rcvindex), rcv_vpid.volid, rcv_vpid.pageid,
			   rcv.offset);
		  if (rcv_page_lsaptr != NULL)
		    {
		      fprintf (stdout, "      page_lsa = %lld|%d\n", (long long int) rcv_page_lsaptr->pageid,
			       rcv_page_lsaptr->offset);
		    }
		  else
		    {
		      fprintf (stdout, "      page_lsa = %lld|%d\n", -1LL, -1);
		    }
		  fflush (stdout);
		}
#endif /* !NDEBUG */

	      log_rv_redo_record (thread_p, &log_lsa, log_pgptr, RV_fun[rcvindex].redofun, &rcv, &rcv_lsa, 0, NULL,
				  NULL);

	      if (rcv.pgptr != NULL)
		{
		  pgbuf_unfix (thread_p, rcv.pgptr);
		}
	      break;

	    case LOG_COMPENSATE:
	      LSA_COPY (&rcv_lsa, &log_lsa);

	      /* Get the DATA HEADER */
	      LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_RECORD_HEADER), &log_lsa, log_pgptr);
	      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (LOG_REC_COMPENSATE), &log_lsa, log_pgptr);
	      compensate = (LOG_REC_COMPENSATE *) ((char *) log_pgptr->area + log_lsa.offset);

	      /* Do we need to redo anything ? */

	      /* 
	       * Fetch the page for physical log records and check if redo
	       * is needed by comparing the log sequence numbers
	       */

	      rcv_vpid.volid = compensate->data.volid;
	      rcv_vpid.pageid = compensate->data.pageid;

	      rcv.pgptr = NULL;
	      rcvindex = compensate->data.rcvindex;
	      /* If the page does not exit, there is nothing to redo */
	      if (rcv_vpid.pageid != NULL_PAGEID && rcv_vpid.volid != NULL_VOLID)
		{
		  rcv.pgptr = log_rv_redo_fix_page (thread_p, &rcv_vpid, rcvindex);
		  if (rcv.pgptr == NULL)
		    {
		      /* deallocated */
		      break;
		    }
		}

	      if (rcv.pgptr != NULL)
		{
		  rcv_page_lsaptr = pgbuf_get_lsa (rcv.pgptr);
		  /* 
		   * Do we need to execute the redo operation ?
		   * If page_lsa >= rcv_lsa... already updated
		   */
		  assert (end_redo_lsa == NULL || LSA_ISNULL (end_redo_lsa) || LSA_LE (rcv_page_lsaptr, end_redo_lsa));
		  if (LSA_LE (&rcv_lsa, rcv_page_lsaptr))
		    {
		      /* It is already done */
		      pgbuf_unfix (thread_p, rcv.pgptr);
		      break;
		    }
		}
	      else
		{
		  rcv.pgptr = NULL;
		  rcv_page_lsaptr = NULL;
		}

	      rcv.length = compensate->length;
	      rcv.offset = compensate->data.offset;

	      LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_REC_COMPENSATE), &log_lsa, log_pgptr);
	      /* GET COMPENSATING DATA */
	      LOG_READ_ALIGN (thread_p, &log_lsa, log_pgptr);
#if !defined(NDEBUG)
	      if (prm_get_bool_value (PRM_ID_LOG_TRACE_DEBUG))
		{
		  fprintf (stdout,
			   "TRACE REDOING[5]: LSA = %lld|%d, Rv_index = %s,\n"
			   "      volid = %d, pageid = %d, offset = %d,\n", (long long int) rcv_lsa.pageid,
			   (int) rcv_lsa.offset, rv_rcvindex_string (rcvindex), rcv_vpid.volid, rcv_vpid.pageid,
			   rcv.offset);
		  if (rcv_page_lsaptr != NULL)
		    {
		      fprintf (stdout, "      page_lsa = %lld|%d\n", (long long int) rcv_page_lsaptr->pageid,
			       rcv_page_lsaptr->offset);
		    }
		  else
		    {
		      fprintf (stdout, "      page_lsa = %lld|%d\n", -1LL, -1);
		    }
		  fflush (stdout);
		}
#endif /* !NDEBUG */

	      log_rv_redo_record (thread_p, &log_lsa, log_pgptr, RV_fun[rcvindex].undofun, &rcv, &rcv_lsa, 0, NULL,
				  NULL);
	      if (rcv.pgptr != NULL)
		{
		  pgbuf_unfix (thread_p, rcv.pgptr);
		}
	      if (prm_get_bool_value (PRM_ID_LOG_BTREE_OPS) && rcvindex == RVBT_RECORD_MODIFY_COMPENSATE)
		{
		  _er_log_debug (ARG_FILE_LINE,
				 "BTREE_REDO: Successfully applied compensate lsa=%lld|%d, undo_nxlsa=%lld|%d.\n",
				 (long long int) rcv_lsa.pageid, (int) rcv_lsa.offset,
				 (long long int) compensate->undo_nxlsa.pageid, (int) compensate->undo_nxlsa.offset);
		}
	      break;

	    case LOG_2PC_PREPARE:
	      tran_index = logtb_find_tran_index (thread_p, tran_id);
	      if (tran_index == NULL_TRAN_INDEX)
		{
		  break;
		}
	      tdes = LOG_FIND_TDES (tran_index);
	      if (tdes == NULL)
		{
		  break;
		}
	      /* 
	       * The transaction was still alive at the time of crash, therefore,
	       * the decision of the 2PC is not known. Reacquire the locks
	       * acquired at the time of the crash
	       */

	      /* Get the DATA HEADER */
	      LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_RECORD_HEADER), &log_lsa, log_pgptr);

	      if (tdes->state == TRAN_UNACTIVE_2PC_PREPARE)
		{
		  /* The transaction was still prepared_to_commit state at the time of crash. So, read the global
		   * transaction identifier and list of locks from the log record, and acquire all of the locks. */

		  log_2pc_read_prepare (thread_p, LOG_2PC_OBTAIN_LOCKS, tdes, &log_lsa, log_pgptr);
		}
	      else
		{
		  /* The transaction was not in prepared_to_commit state anymore at the time of crash. So, there is no
		   * need to read the list of locks from the log record. Read only the global transaction from the log
		   * record. */
		  log_2pc_read_prepare (thread_p, LOG_2PC_DONT_OBTAIN_LOCKS, tdes, &log_lsa, log_pgptr);
		}
	      break;

	    case LOG_2PC_START:
	      tran_index = logtb_find_tran_index (thread_p, tran_id);
	      if (tran_index != NULL_TRAN_INDEX)
		{
		  /* The transaction was still alive at the time of crash. */
		  tdes = LOG_FIND_TDES (tran_index);
		  if (tdes != NULL && LOG_ISTRAN_2PC (tdes))
		    {
		      /* The transaction was still alive at the time of crash. So, copy the coordinator information
		       * from the log record to the transaction descriptor. */

		      /* Get the DATA HEADER */
		      LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_RECORD_HEADER), &log_lsa, log_pgptr);

		      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (LOG_REC_2PC_START), &log_lsa, log_pgptr);
		      start_2pc = ((LOG_REC_2PC_START *) ((char *) log_pgptr->area + log_lsa.offset));

		      /* 
		       * Obtain the participant information
		       */
		      logtb_set_client_ids_all (&tdes->client, 0, NULL, start_2pc->user_name, NULL, NULL, NULL, -1);
		      tdes->gtrid = start_2pc->gtrid;

		      num_particps = start_2pc->num_particps;
		      particp_id_length = start_2pc->particp_id_length;
		      block_particps_ids = malloc (particp_id_length * num_particps);
		      if (block_particps_ids == NULL)
			{
			  /* Out of memory */
			  LSA_SET_NULL (&log_Gl.unique_stats_table.curr_rcv_rec_lsa);
			  logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_recovery_redo");
			  break;
			}

		      LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_REC_2PC_START), &log_lsa, log_pgptr);
		      LOG_READ_ALIGN (thread_p, &log_lsa, log_pgptr);

		      /* Read in the participants info. block from the log */
		      logpb_copy_from_log (thread_p, (char *) block_particps_ids, particp_id_length * num_particps,
					   &log_lsa, log_pgptr);

		      /* Initilize the coordinator information */
		      if (log_2pc_alloc_coord_info (tdes, num_particps, particp_id_length, block_particps_ids) == NULL)
			{
			  LSA_SET_NULL (&log_Gl.unique_stats_table.curr_rcv_rec_lsa);
			  logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_recovery_redo");
			  break;
			}

		      /* Initilize the Acknowledgement vector to 0 since we do not know what acknowledgments have
		       * already been received. we need to continue reading the log */

		      i = sizeof (int) * tdes->coord->num_particps;
		      if ((tdes->coord->ack_received = (int *) malloc (i)) == NULL)
			{
			  /* Out of memory */
			  LSA_SET_NULL (&log_Gl.unique_stats_table.curr_rcv_rec_lsa);
			  logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_recovery_redo");
			  break;
			}
		      for (i = 0; i < tdes->coord->num_particps; i++)
			{
			  tdes->coord->ack_received[i] = false;
			}
		    }
		}
	      break;

	    case LOG_2PC_RECV_ACK:
	      tran_index = logtb_find_tran_index (thread_p, tran_id);
	      if (tran_index != NULL_TRAN_INDEX)
		{
		  /* The transaction was still alive at the time of crash. */

		  tdes = LOG_FIND_TDES (tran_index);
		  if (tdes != NULL && LOG_ISTRAN_2PC (tdes))
		    {
		      /* 
		       * The 2PC_START record should have already been read by this
		       * time, otherwise, there is an error in the recovery analysis
		       * phase.
		       */
#if defined(CUBRID_DEBUG)
		      if (tdes->coord == NULL || tdes->coord->ack_received == NULL)
			{
			  er_log_debug (ARG_FILE_LINE,
					"log_recovery_redo: SYSTEM ERROR There is likely an error in the recovery"
					" analysis phase since coordinator information"
					" has not been allocated for transaction = %d with state = %s", tdes->trid,
					log_state_string (tdes->state));
			  break;
			}
#endif /* CUBRID_DEBUG */

		      /* The transaction was still alive at the time of crash. So, read the participant index from the
		       * log record and set the acknowledgement flag of that participant. */

		      /* Get the DATA HEADER */
		      LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_RECORD_HEADER), &log_lsa, log_pgptr);

		      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (LOG_REC_2PC_PARTICP_ACK), &log_lsa,
							log_pgptr);
		      received_ack = ((LOG_REC_2PC_PARTICP_ACK *) ((char *) log_pgptr->area + log_lsa.offset));
		      tdes->coord->ack_received[received_ack->particp_index] = true;
		    }
		}
	      break;

	    case LOG_COMMIT:
	    case LOG_ABORT:
	      {
		bool free_tran = false;

		tran_index = logtb_find_tran_index (thread_p, tran_id);
		if (tran_index != NULL_TRAN_INDEX && tran_index != LOG_SYSTEM_TRAN_INDEX)
		  {
		    tdes = LOG_FIND_TDES (tran_index);
		    assert (tdes && tdes->state != TRAN_ACTIVE);
		    free_tran = true;
		  }

		if (stopat != NULL && *stopat != -1)
		  {
		    /* 
		     * Need to read the donetime record to find out if we need to stop
		     * the recovery at this point.
		     */
		    LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_RECORD_HEADER), &log_lsa, log_pgptr);
		    LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (LOG_REC_DONETIME), &log_lsa, log_pgptr);
		    donetime = (LOG_REC_DONETIME *) ((char *) log_pgptr->area + log_lsa.offset);

		    if (difftime (*stopat, (time_t) donetime->at_time) < 0)
		      {
			/* 
			 * Stop the recovery process at this point
			 */
			LSA_SET_NULL (&lsa);

			/* Commit/abort record was recorded after the stopat recovery time. The transaction needs to
			 * undo all its changes (log_recovery_undo), so transaction descriptor needs to be kept,
			 * and transaction state should be changed to aborted. The undo process starts from this
			 * record's LSA and undoes all previous changes of the transaction
			 * (See log_find_unilaterally_largest_undo_lsa usage from log_recovery_undo) */
			if (tdes != NULL)
			  {
			    tdes->state = TRAN_UNACTIVE_UNILATERALLY_ABORTED;
			  }
			free_tran = false;
		      }
		  }
		if (free_tran == true)
		  {
		    logtb_free_tran_index (thread_p, tran_index);
		  }
	      }

	      break;

	    case LOG_MVCC_UNDO_DATA:
	      /* Must detect MVCC operations and recover vacuum data buffer. The found operation is not actually
	       * redone/undone, but it has information that can be used for vacuum. */

	      LSA_COPY (&rcv_lsa, &log_lsa);
	      LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_RECORD_HEADER), &log_lsa, log_pgptr);
	      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (LOG_REC_MVCC_UNDO), &log_lsa, log_pgptr);
	      mvcc_undo = (LOG_REC_MVCC_UNDO *) ((char *) log_pgptr->area + log_lsa.offset);
	      mvccid = mvcc_undo->mvccid;

	      /* Check if MVCC next ID must be updated */
	      if (!MVCC_ID_PRECEDES (mvccid, log_Gl.hdr.mvcc_next_id))
		{
		  log_Gl.hdr.mvcc_next_id = mvccid;
		  MVCCID_FORWARD (log_Gl.hdr.mvcc_next_id);
		}

	      if (is_mvcc_op)
		{
		  /* Save last MVCC operation LOG_LSA. */
		  LSA_COPY (&log_Gl.hdr.mvcc_op_log_lsa, &rcv_lsa);
		}
	      break;

	    case LOG_UNDO_DATA:
	    case LOG_DUMMY_HEAD_POSTPONE:
	    case LOG_POSTPONE:
	    case LOG_WILL_COMMIT:
	    case LOG_COMMIT_WITH_POSTPONE:
	    case LOG_SYSOP_START_POSTPONE:
	    case LOG_SYSOP_END:
	    case LOG_START_CHKPT:
	    case LOG_END_CHKPT:
	    case LOG_SAVEPOINT:
	    case LOG_2PC_COMMIT_DECISION:
	    case LOG_2PC_ABORT_DECISION:
	    case LOG_2PC_COMMIT_INFORM_PARTICPS:
	    case LOG_2PC_ABORT_INFORM_PARTICPS:
	    case LOG_DUMMY_CRASH_RECOVERY:
	    case LOG_REPLICATION_DATA:
	    case LOG_REPLICATION_STATEMENT:
	    case LOG_DUMMY_HA_SERVER_STATE:
	    case LOG_DUMMY_OVF_RECORD:
	    case LOG_DUMMY_GENERIC:
	    case LOG_END_OF_LOG:
	      break;

	    case LOG_SMALLER_LOGREC_TYPE:
	    case LOG_LARGER_LOGREC_TYPE:
	    default:
#if defined(CUBRID_DEBUG)
	      er_log_debug (ARG_FILE_LINE,
			    "log_recovery_redo: Unknown record type = %d (%s)... May be a system error", log_rtype,
			    log_to_string (log_rtype));
#endif /* CUBRID_DEBUG */
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_PAGE_CORRUPTED, 1, log_lsa.pageid);
	      if (LSA_EQ (&lsa, &log_lsa))
		{
		  LSA_SET_NULL (&lsa);
		}
	      break;
	    }

	  /* 
	   * We can fix the lsa.pageid in the case of log_records without forward
	   * address at this moment.
	   */
	  if (lsa.offset == NULL_OFFSET && lsa.pageid != NULL_PAGEID && lsa.pageid < log_lsa.pageid)
	    {
	      lsa.pageid = log_lsa.pageid;
	    }
	}
    }

  log_zip_free (undo_unzip_ptr);
  log_zip_free (redo_unzip_ptr);

  logtb_reset_bit_area_start_mvccid ();

  /* Now finish all postpone operations */
  log_recovery_finish_all_postpone (thread_p);

  /* Flush all dirty pages */
  logpb_flush_pages_direct (thread_p);

  logpb_flush_header (thread_p);
  (void) pgbuf_flush_all (thread_p, NULL_VOLID);

exit:
  LSA_SET_NULL (&log_Gl.unique_stats_table.curr_rcv_rec_lsa);

  return;
}

/*
 * log_recovery_finish_postpone () - Finish postpone during recovery for one
 *				     transaction descriptor.
 *
 * return	 : Void.
 * thread_p (in) : Thread entry.
 * tdes (in)	 : Transaction descriptor.
 */
static void
log_recovery_finish_postpone (THREAD_ENTRY * thread_p, LOG_TDES * tdes)
{
  LOG_LSA first_postpone_to_apply;

  if (tdes == NULL || tdes->trid == NULL_TRANID)
    {
      /* Nothing to do */
      return;
    }

  if (tdes->state == TRAN_UNACTIVE_TOPOPE_COMMITTED_WITH_POSTPONE)
    {
      /* We need to read the log record for system op start postpone */
      LOG_LSA sysop_start_postpone_lsa = tdes->rcv.sysop_start_postpone_lsa;
      char log_page_buffer[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT];
      LOG_PAGE *log_page = (LOG_PAGE *) PTR_ALIGN (log_page_buffer, MAX_ALIGNMENT);
      LOG_REC_SYSOP_START_POSTPONE sysop_start_postpone;
      int undo_buffer_size = 0, undo_data_size = 0;
      char *undo_buffer = NULL, *undo_data = NULL;

      assert (tdes->topops.last == 0);
      assert (!LSA_ISNULL (&sysop_start_postpone_lsa));
      LSA_SET_NULL (&first_postpone_to_apply);

      /* first verify it didn't crash in the middle of a run postpone system op */
      if (!LSA_ISNULL (&tdes->undo_nxlsa) && LSA_LT (&sysop_start_postpone_lsa, &tdes->undo_nxlsa))
	{
	  /* rollback. simulate a new system op */

	  log_sysop_start (thread_p);

	  /* now we don't really know where to stop the rollback, however we can estimate. the postpone phase should be
	   * only populated with run postpone log records and logical run postpone system operations.
	   * if last log record before this system op is a logical run postpone, its LSA is stored in
	   * tdes->tail_topresult_lsa. if it is a run postpone, we won't know, but rollback skips them. so we can set
	   * this system op parent to either tail_topresult_lsa or to sysop_start_postpone_lsa, whichever comes last.
	   */
	  if (!LSA_ISNULL (&tdes->tail_topresult_lsa) && LSA_GT (&tdes->tail_topresult_lsa, &sysop_start_postpone_lsa))
	    {
	      tdes->topops.stack[tdes->topops.last].lastparent_lsa = tdes->tail_topresult_lsa;
	    }
	  else
	    {
	      tdes->topops.stack[tdes->topops.last].lastparent_lsa = sysop_start_postpone_lsa;
	    }
	  /* rollback */
	  log_sysop_abort (thread_p);
	  assert (tdes->topops.last == 0);
	}

      /* find first postpone */
      log_recovery_find_first_postpone (thread_p, &first_postpone_to_apply,
					&tdes->topops.stack[tdes->topops.last].posp_lsa, tdes);

      if (!LSA_ISNULL (&first_postpone_to_apply))
	{
	  log_do_postpone (thread_p, tdes, &first_postpone_to_apply);
	}
      LSA_SET_NULL (&tdes->topops.stack[tdes->topops.last].posp_lsa);

      /* simulate system op commit */
      /* we need the data from system op start postpone */
      log_page->hdr.logical_pageid = NULL_PAGEID;
      if (log_read_sysop_start_postpone (thread_p, &sysop_start_postpone_lsa, log_page, true, &sysop_start_postpone,
					 &undo_buffer_size, &undo_buffer, &undo_data_size, &undo_data) != NO_ERROR)
	{
	  /* give up */
	  assert_release (false);
	  return;
	}
      /* check this is not a system op postpone during system op postpone */
      assert (sysop_start_postpone.sysop_end.type == LOG_SYSOP_END_LOGICAL_RUN_POSTPONE
	      || !sysop_start_postpone.sysop_end.run_postpone.is_sysop_postpone);

      log_sysop_end_recovery_postpone (thread_p, &sysop_start_postpone.sysop_end, undo_data_size, undo_data);
      if (undo_buffer != NULL)
	{
	  /* no longer needed */
	  db_private_free (thread_p, undo_buffer);
	}

      if (sysop_start_postpone.sysop_end.type == LOG_SYSOP_END_LOGICAL_RUN_POSTPONE)
	{
	  if (sysop_start_postpone.sysop_end.run_postpone.is_sysop_postpone)
	    {
	      /* this is a system op postpone during system op postpone? should not happen! */
	      assert (false);
	      tdes->state = TRAN_UNACTIVE_UNILATERALLY_ABORTED;
	      tdes->undo_nxlsa = tdes->tail_lsa;
	    }
	  else
	    {
	      /* logical run postpone during transaction postpone. */
	      tdes->state = TRAN_UNACTIVE_COMMITTED_WITH_POSTPONE;
	      LSA_SET_NULL (&tdes->undo_nxlsa);

	      /* we just finished the run postpone. update tdes->posp_nxlsa */
	      tdes->posp_nxlsa = sysop_start_postpone.sysop_end.run_postpone.postpone_lsa;
	    }
	}
      else
	{
	  assert (sysop_start_postpone.sysop_end.type != LOG_SYSOP_END_ABORT);
	  tdes->state = TRAN_UNACTIVE_UNILATERALLY_ABORTED;
	  tdes->undo_nxlsa = tdes->tail_lsa;
	}

      if (tdes->topops.last >= 0)
	{
	  assert_release (false);
	  tdes->topops.last = -1;
	}
    }
  assert (tdes->state != TRAN_UNACTIVE_TOPOPE_COMMITTED_WITH_POSTPONE);

  LSA_SET_NULL (&first_postpone_to_apply);

  if (tdes->state == TRAN_UNACTIVE_WILL_COMMIT || tdes->state == TRAN_UNACTIVE_COMMITTED_WITH_POSTPONE)
    {
      LSA_SET_NULL (&first_postpone_to_apply);

      assert (!VACUUM_IS_THREAD_VACUUM (thread_p));
      assert (!LSA_ISNULL (&tdes->rcv.tran_start_postpone_lsa));

      /* 
       * The transaction was the one that was committing
       */
      if (tdes->state == TRAN_UNACTIVE_COMMITTED_WITH_POSTPONE && !LSA_ISNULL (&tdes->undo_nxlsa)
	  && LSA_GT (&tdes->undo_nxlsa, &tdes->rcv.tran_start_postpone_lsa))
	{
	  /* Transaction stopped in the middle of a logical postpone. We must rollback it. */
	  assert (tdes->topops.last == -1);
	  log_sysop_start (thread_p);

	  /* same as with the system op case, we need to set last parent to tail_topresult_lsa or
	   * tran_start_postpone_lsa, whichever is last. */
	  if (!LSA_ISNULL (&tdes->tail_topresult_lsa)
	      && LSA_GT (&tdes->tail_topresult_lsa, &tdes->rcv.tran_start_postpone_lsa))
	    {
	      tdes->topops.stack[tdes->topops.last].lastparent_lsa = tdes->tail_topresult_lsa;
	    }
	  else
	    {
	      tdes->topops.stack[tdes->topops.last].lastparent_lsa = tdes->rcv.tran_start_postpone_lsa;
	    }
	  log_sysop_abort (thread_p);
	  /* no more undo */
	  LSA_SET_NULL (&tdes->undo_nxlsa);
	}

      log_recovery_find_first_postpone (thread_p, &first_postpone_to_apply, &tdes->posp_nxlsa, tdes);
      if (!LSA_ISNULL (&first_postpone_to_apply))
	{
	  log_do_postpone (thread_p, tdes, &first_postpone_to_apply);
	}

      if (tdes->coord == NULL)
	{			/* If this is a local transaction */
	  (void) log_complete (thread_p, tdes, LOG_COMMIT, LOG_DONT_NEED_NEWTRID, LOG_NEED_TO_WRITE_EOT_LOG);
	  logtb_free_tran_index (thread_p, tdes->tran_index);
	}
    }
  else if (tdes->state == TRAN_UNACTIVE_COMMITTED)
    {
      if (tdes->coord == NULL)
	{
	  (void) log_complete (thread_p, tdes, LOG_COMMIT, LOG_DONT_NEED_NEWTRID, LOG_NEED_TO_WRITE_EOT_LOG);
	  logtb_free_tran_index (thread_p, tdes->tran_index);
	}
    }
}

/*
 * log_recovery_finish_all_postpone - FINISH COMMITTING TRANSACTIONS WITH
 *                                   UNFINISH POSTPONE ACTIONS
 *
 * return: nothing
 *
 * NOTE:Finish the committing of transactions which have been declared
 *              as committed, but not all their postpone actions are done.
 *              This happens when there is a crash in the middle of a
 *              log_commit_with_postpone and log_commit.
 *              This function should be called after the log_recovery_redo
 *              function.
 */
static void
log_recovery_finish_all_postpone (THREAD_ENTRY * thread_p)
{
  int i;
  int save_tran_index;
  int save_thread_type = 0;
  TRANID trid;
  LOG_TDES *tdes = NULL;	/* Transaction descriptor */
  VACUUM_WORKER *worker = NULL;

  /* Finish committing transactions with unfinished postpone actions */
  thread_p = thread_p != NULL ? thread_p : thread_get_thread_entry_info ();

  save_tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);

  for (i = 0; i < log_Gl.trantable.num_total_indices; i++)
    {
      tdes = LOG_FIND_TDES (i);
      if (tdes == NULL || tdes->trid == NULL_TRANID)
	{
	  continue;
	}
      LOG_SET_CURRENT_TRAN_INDEX (thread_p, i);
      log_recovery_finish_postpone (thread_p, tdes);
      LOG_SET_CURRENT_TRAN_INDEX (thread_p, save_tran_index);
    }
  for (trid = LOG_FIRST_VACUUM_WORKER_TRANID; trid <= LOG_LAST_VACUUM_WORKER_TRANID; trid++)
    {
      /* Convert thread to vacuum worker */
      worker = vacuum_rv_get_worker_by_trid (thread_p, trid);
      if (worker->state != VACUUM_WORKER_STATE_RECOVERY)
	{
	  /* Nothing to do */
	  continue;
	}
      VACUUM_CONVERT_THREAD_TO_VACUUM (thread_p, worker, save_thread_type);
      tdes = worker->tdes;

      vacuum_er_log (VACUUM_ER_LOG_RECOVERY | VACUUM_ER_LOG_TOPOPS,
		     "VACUUM: Finish postpone for tdes: tdes->trid=%d, tdes->state=%d, tdes->topops.last=%d.",
		     tdes->trid, tdes->state, tdes->topops.last);

      log_recovery_finish_postpone (thread_p, tdes);

      /* Restore thread */
      VACUUM_RESTORE_THREAD (thread_p, save_thread_type);
    }
}

/*
 * log_recovery_undo - SCAN BACKWARDS UNDOING DATA
 *
 * return: nothing
 *
 */
static void
log_recovery_undo (THREAD_ENTRY * thread_p)
{
  LOG_LSA *lsa_ptr;		/* LSA of log record to undo */
  char log_pgbuf[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT], *aligned_log_pgbuf;
  LOG_PAGE *log_pgptr = NULL;	/* Log page pointer where LSA is located */
  LOG_LSA log_lsa;
  LOG_RECORD_HEADER *log_rec = NULL;	/* Pointer to log record */
  LOG_REC_UNDOREDO *undoredo = NULL;	/* Undo_redo log record */
  LOG_REC_UNDO *undo = NULL;	/* Undo log record */
  LOG_REC_MVCC_UNDOREDO *mvcc_undoredo = NULL;	/* MVCC op Undo_redo log record */
  LOG_REC_MVCC_UNDO *mvcc_undo = NULL;	/* MVCC op Undo log record */
  LOG_REC_COMPENSATE *compensate;	/* Compensating log record */
  LOG_REC_SYSOP_END *sysop_end;	/* Result of top system op */
  LOG_RCVINDEX rcvindex;	/* Recovery index function */
  LOG_RCV rcv;			/* Recovery structure */
  VPID rcv_vpid;		/* VPID of data to recover */
  LOG_LSA rcv_lsa;		/* Address of redo log record */
  LOG_LSA prev_tranlsa;		/* prev LSA of transaction */
  LOG_TDES *tdes;		/* Transaction descriptor */
  int tran_index;
  int data_header_size = 0;
  LOG_ZIP *undo_unzip_ptr = NULL;
  bool is_mvcc_op;
  VACUUM_WORKER *worker = NULL;
  volatile TRANID tran_id;
  volatile LOG_RECTYPE log_rtype;

  aligned_log_pgbuf = PTR_ALIGN (log_pgbuf, MAX_ALIGNMENT);

  /* 
   * Remove from the list of transaction to abort, those that have finished
   * when the crash happens, so it does not remain dangling in the transaction
   * table.
   */

  for (tran_index = 0; tran_index < log_Gl.trantable.num_total_indices; tran_index++)
    {
      if (tran_index != LOG_SYSTEM_TRAN_INDEX)
	{
	  if ((tdes = LOG_FIND_TDES (tran_index)) != NULL && tdes->trid != NULL_TRANID
	      && (tdes->state == TRAN_UNACTIVE_UNILATERALLY_ABORTED || tdes->state == TRAN_UNACTIVE_ABORTED)
	      && LSA_ISNULL (&tdes->undo_nxlsa))
	    {
	      (void) log_complete (thread_p, tdes, LOG_ABORT, LOG_DONT_NEED_NEWTRID, LOG_NEED_TO_WRITE_EOT_LOG);
	      logtb_free_tran_index (thread_p, tran_index);
	    }
	}
    }
  for (tran_id = LOG_FIRST_VACUUM_WORKER_TRANID; tran_id <= LOG_LAST_VACUUM_WORKER_TRANID; tran_id++)
    {
      worker = vacuum_rv_get_worker_by_trid (thread_p, tran_id);
      if (worker->state == VACUUM_WORKER_STATE_RECOVERY && LSA_ISNULL (&worker->tdes->undo_nxlsa))
	{
	  /* Nothing to recover for this worker */
	  vacuum_rv_finish_worker_recovery (thread_p, tran_id);
	}
    }

  /* 
   * GO BACKWARDS, undoing records
   */

  /* Find the largest LSA to undo */
  lsa_ptr = log_find_unilaterally_largest_undo_lsa (thread_p);

  log_pgptr = (LOG_PAGE *) aligned_log_pgbuf;

  undo_unzip_ptr = log_zip_alloc (LOGAREA_SIZE, false);
  if (undo_unzip_ptr == NULL)
    {
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_recovery_undo");
      return;
    }

  while (lsa_ptr != NULL && !LSA_ISNULL (lsa_ptr))
    {
      /* Fetch the page where the LSA record to undo is located */
      LSA_COPY (&log_lsa, lsa_ptr);
      if (logpb_fetch_page (thread_p, &log_lsa, LOG_CS_FORCE_USE, log_pgptr) != NO_ERROR)
	{
	  log_zip_free (undo_unzip_ptr);

	  logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_recovery_undo");
	  return;
	}

      /* Check all log records in this phase */
      while (lsa_ptr != NULL && lsa_ptr->pageid == log_lsa.pageid)
	{
	  /* Find the log record */
	  log_lsa.offset = lsa_ptr->offset;
	  log_rec = LOG_GET_LOG_RECORD_HEADER (log_pgptr, &log_lsa);

	  tran_id = log_rec->trid;
	  log_rtype = log_rec->type;

	  LSA_COPY (&prev_tranlsa, &log_rec->prev_tranlsa);

	  if (LOG_IS_VACUUM_THREAD_TRANID (tran_id))
	    {
	      worker = vacuum_rv_get_worker_by_trid (thread_p, tran_id);
	      if (worker->state == VACUUM_WORKER_STATE_RECOVERY)
		{
		  tdes = worker->tdes;
		}
	      else
		{
		  /* State is wrong */
		  assert (false);
		  tdes = NULL;
		}
	    }
	  else
	    {
	      /* Active worker transaction */
	      tran_index = logtb_find_tran_index (thread_p, tran_id);
	      if (tran_index == NULL_TRAN_INDEX)
		{
#if defined(CUBRID_DEBUG)
		  er_log_debug (ARG_FILE_LINE, "log_recovery_undo: SYSTEM ERROR for log located at %lld|%d\n",
				(long long int) log_lsa.pageid, log_lsa.offset);
#endif /* CUBRID_DEBUG */
		  logtb_free_tran_index_with_undo_lsa (thread_p, lsa_ptr);
		}
	      else
		{
		  tdes = LOG_FIND_TDES (tran_index);
		  if (tdes == NULL)
		    {
		      /* This looks like a system error in the analysis phase */
#if defined(CUBRID_DEBUG)
		      er_log_debug (ARG_FILE_LINE, "log_recovery_undo: SYSTEM ERROR for log located at %lld|%d\n",
				    (long long int) log_lsa.pageid, log_lsa.offset);
#endif /* CUBRID_DEBUG */
		      logtb_free_tran_index_with_undo_lsa (thread_p, lsa_ptr);
		    }
		}
	    }

	  if (tran_index != NULL_TRAN_INDEX && tdes != NULL)
	    {
	      LSA_COPY (&tdes->undo_nxlsa, &prev_tranlsa);

	      switch (log_rtype)
		{
		case LOG_MVCC_UNDOREDO_DATA:
		case LOG_MVCC_DIFF_UNDOREDO_DATA:
		case LOG_UNDOREDO_DATA:
		case LOG_DIFF_UNDOREDO_DATA:
		  LSA_COPY (&rcv_lsa, &log_lsa);
		  /* 
		   * The transaction was active at the time of the crash. The
		   * transaction is unilaterally aborted by the system
		   */

		  if (log_rtype == LOG_MVCC_UNDOREDO_DATA || log_rtype == LOG_MVCC_DIFF_UNDOREDO_DATA)
		    {
		      is_mvcc_op = true;
		    }
		  else
		    {
		      is_mvcc_op = false;
		    }

		  /* Get the DATA HEADER */
		  LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_RECORD_HEADER), &log_lsa, log_pgptr);

		  if (is_mvcc_op)
		    {
		      data_header_size = sizeof (LOG_REC_MVCC_UNDOREDO);
		      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, data_header_size, &log_lsa, log_pgptr);
		      mvcc_undoredo = (LOG_REC_MVCC_UNDOREDO *) ((char *) log_pgptr->area + log_lsa.offset);

		      /* Get undoredo info */
		      undoredo = &mvcc_undoredo->undoredo;

		      /* Save transaction MVCCID to recovery */
		      rcv.mvcc_id = mvcc_undoredo->mvccid;
		    }
		  else
		    {
		      data_header_size = sizeof (LOG_REC_UNDOREDO);
		      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, data_header_size, &log_lsa, log_pgptr);
		      undoredo = (LOG_REC_UNDOREDO *) ((char *) log_pgptr->area + log_lsa.offset);

		      rcv.mvcc_id = MVCCID_NULL;
		    }

		  rcvindex = undoredo->data.rcvindex;
		  rcv.length = undoredo->ulength;
		  rcv.offset = undoredo->data.offset;
		  rcv_vpid.volid = undoredo->data.volid;
		  rcv_vpid.pageid = undoredo->data.pageid;

		  LOG_READ_ADD_ALIGN (thread_p, data_header_size, &log_lsa, log_pgptr);

#if !defined(NDEBUG)
		  if (prm_get_bool_value (PRM_ID_LOG_TRACE_DEBUG))
		    {
		      fprintf (stdout,
			       "TRACE UNDOING[1]: LSA = %lld|%d, Rv_index = %s,\n"
			       "      volid = %d, pageid = %d, offset = %d,\n", (long long int) rcv_lsa.pageid,
			       (int) rcv_lsa.offset, rv_rcvindex_string (rcvindex), rcv_vpid.volid, rcv_vpid.pageid,
			       rcv.offset);
		      fflush (stdout);
		    }
#endif /* !NDEBUG */

		  log_rv_undo_record (thread_p, &log_lsa, log_pgptr, rcvindex, &rcv_vpid, &rcv, &rcv_lsa, tdes,
				      undo_unzip_ptr);
		  break;

		case LOG_MVCC_UNDO_DATA:
		case LOG_UNDO_DATA:
		  /* Does the record belong to a MVCC op? */
		  is_mvcc_op = log_rtype == LOG_MVCC_UNDO_DATA;

		  LSA_COPY (&rcv_lsa, &log_lsa);
		  /* 
		   * The transaction was active at the time of the crash. The
		   * transaction is unilaterally aborted by the system
		   */

		  /* Get the DATA HEADER */
		  LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_RECORD_HEADER), &log_lsa, log_pgptr);

		  if (is_mvcc_op)
		    {
		      data_header_size = sizeof (LOG_REC_MVCC_UNDO);
		      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, data_header_size, &log_lsa, log_pgptr);
		      mvcc_undo = (LOG_REC_MVCC_UNDO *) ((char *) log_pgptr->area + log_lsa.offset);

		      /* Get undo info */
		      undo = &mvcc_undo->undo;

		      /* Save transaction MVCCID to recovery */
		      rcv.mvcc_id = mvcc_undo->mvccid;
		    }
		  else
		    {
		      data_header_size = sizeof (LOG_REC_UNDO);
		      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, data_header_size, &log_lsa, log_pgptr);
		      undo = (LOG_REC_UNDO *) ((char *) log_pgptr->area + log_lsa.offset);

		      rcv.mvcc_id = MVCCID_NULL;
		    }

		  rcvindex = undo->data.rcvindex;
		  rcv.length = undo->length;
		  rcv.offset = undo->data.offset;
		  rcv_vpid.volid = undo->data.volid;
		  rcv_vpid.pageid = undo->data.pageid;

		  LOG_READ_ADD_ALIGN (thread_p, data_header_size, &log_lsa, log_pgptr);

#if !defined(NDEBUG)
		  if (prm_get_bool_value (PRM_ID_LOG_TRACE_DEBUG))
		    {
		      fprintf (stdout,
			       "TRACE UNDOING[2]: LSA = %lld|%d, Rv_index = %s,\n"
			       "      volid = %d, pageid = %d, offset = %d,\n", (long long int) rcv_lsa.pageid,
			       rcv_lsa.offset, rv_rcvindex_string (rcvindex), rcv_vpid.volid, rcv_vpid.pageid,
			       rcv.offset);
		      fflush (stdout);
		    }
#endif /* !NDEBUG */
		  log_rv_undo_record (thread_p, &log_lsa, log_pgptr, rcvindex, &rcv_vpid, &rcv, &rcv_lsa, tdes,
				      undo_unzip_ptr);
		  break;

		case LOG_REDO_DATA:
		case LOG_MVCC_REDO_DATA:
		case LOG_DBEXTERN_REDO_DATA:
		case LOG_DUMMY_HEAD_POSTPONE:
		case LOG_POSTPONE:
		case LOG_SAVEPOINT:
		case LOG_REPLICATION_DATA:
		case LOG_REPLICATION_STATEMENT:
		case LOG_DUMMY_HA_SERVER_STATE:
		case LOG_DUMMY_OVF_RECORD:
		case LOG_DUMMY_GENERIC:
		  /* Not for UNDO ... */
		  /* Break switch to go to previous record */
		  break;

		case LOG_COMPENSATE:
		  /* Only for REDO .. Go to next undo record Need to read the compensating record to set the next undo
		   * address. */

		  /* Get the DATA HEADER */
		  LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_RECORD_HEADER), &log_lsa, log_pgptr);
		  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (LOG_REC_COMPENSATE), &log_lsa, log_pgptr);
		  compensate = (LOG_REC_COMPENSATE *) ((char *) log_pgptr->area + log_lsa.offset);
		  LSA_COPY (&prev_tranlsa, &compensate->undo_nxlsa);
		  break;

		case LOG_SYSOP_END:
		  /* 
		   * We found a system top operation that should be skipped from
		   * rollback
		   */

		  /* Read the DATA HEADER */
		  LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_RECORD_HEADER), &log_lsa, log_pgptr);
		  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (LOG_REC_SYSOP_END), &log_lsa, log_pgptr);
		  sysop_end = ((LOG_REC_SYSOP_END *) ((char *) log_pgptr->area + log_lsa.offset));

		  if (sysop_end->type == LOG_SYSOP_END_LOGICAL_UNDO)
		    {
		      /* execute undo */
		      rcvindex = sysop_end->undo.data.rcvindex;
		      rcv.length = sysop_end->undo.length;
		      rcv.offset = sysop_end->undo.data.offset;
		      rcv_vpid.volid = sysop_end->undo.data.volid;
		      rcv_vpid.pageid = sysop_end->undo.data.pageid;
		      rcv.mvcc_id = MVCCID_NULL;

		      /* will jump to parent LSA. save it now before advancing to undo data */
		      LSA_COPY (&prev_tranlsa, &sysop_end->lastparent_lsa);
		      LSA_COPY (&tdes->undo_nxlsa, &sysop_end->lastparent_lsa);

		      LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_REC_SYSOP_END), &log_lsa, log_pgptr);
		      log_rv_undo_record (thread_p, &log_lsa, log_pgptr, rcvindex, &rcv_vpid, &rcv, &rcv_lsa, tdes,
					  undo_unzip_ptr);
		    }
		  else if (sysop_end->type == LOG_SYSOP_END_LOGICAL_MVCC_UNDO)
		    {
		      /* execute undo */
		      rcvindex = sysop_end->mvcc_undo.undo.data.rcvindex;
		      rcv.length = sysop_end->mvcc_undo.undo.length;
		      rcv.offset = sysop_end->mvcc_undo.undo.data.offset;
		      rcv_vpid.volid = sysop_end->mvcc_undo.undo.data.volid;
		      rcv_vpid.pageid = sysop_end->mvcc_undo.undo.data.pageid;
		      rcv.mvcc_id = sysop_end->mvcc_undo.mvccid;

		      /* will jump to parent LSA. save it now before advancing to undo data */
		      LSA_COPY (&prev_tranlsa, &sysop_end->lastparent_lsa);
		      LSA_COPY (&tdes->undo_nxlsa, &sysop_end->lastparent_lsa);
		      LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_REC_SYSOP_END), &log_lsa, log_pgptr);
		      log_rv_undo_record (thread_p, &log_lsa, log_pgptr, rcvindex, &rcv_vpid, &rcv, &rcv_lsa, tdes,
					  undo_unzip_ptr);
		    }
		  else if (sysop_end->type == LOG_SYSOP_END_LOGICAL_COMPENSATE)
		    {
		      /* compensate */
		      LSA_COPY (&prev_tranlsa, &sysop_end->compensate_lsa);
		    }
		  else
		    {
		      /* should not find run postpones on undo recovery */
		      assert (sysop_end->type != LOG_SYSOP_END_LOGICAL_RUN_POSTPONE);

		      /* jump to parent LSA */
		      LSA_COPY (&prev_tranlsa, &sysop_end->lastparent_lsa);
		    }
		  break;

		case LOG_RUN_POSTPONE:
		case LOG_WILL_COMMIT:
		case LOG_COMMIT_WITH_POSTPONE:
		case LOG_COMMIT:
		case LOG_SYSOP_START_POSTPONE:
		case LOG_ABORT:
		case LOG_START_CHKPT:
		case LOG_END_CHKPT:
		case LOG_2PC_PREPARE:
		case LOG_2PC_START:
		case LOG_2PC_ABORT_DECISION:
		case LOG_2PC_COMMIT_DECISION:
		case LOG_2PC_ABORT_INFORM_PARTICPS:
		case LOG_2PC_COMMIT_INFORM_PARTICPS:
		case LOG_2PC_RECV_ACK:
		case LOG_DUMMY_CRASH_RECOVERY:
		case LOG_END_OF_LOG:
		  /* This looks like a system error in the analysis phase */
#if defined(CUBRID_DEBUG)
		  er_log_debug (ARG_FILE_LINE,
				"log_recovery_undo: SYSTEM ERROR for log located at %lld|%d,"
				" Bad log_rectype = %d\n (%s).\n", (long long int) log_lsa.pageid, log_lsa.offset,
				log_rtype, log_to_string (log_rtype));
#endif /* CUBRID_DEBUG */
		  /* Remove the transaction from the recovery process */
		  assert (false);

		  /* Clear MVCCID */
		  tdes->mvccinfo.id = MVCCID_NULL;

		  if (LOG_IS_VACUUM_THREAD_TRANID (tran_id))
		    {
		      vacuum_rv_finish_worker_recovery (thread_p, tran_id);
		    }
		  else
		    {
		      (void) log_complete (thread_p, tdes, LOG_ABORT, LOG_DONT_NEED_NEWTRID, LOG_NEED_TO_WRITE_EOT_LOG);
		      logtb_free_tran_index (thread_p, tran_index);
		    }
		  tdes = NULL;
		  break;

		case LOG_SMALLER_LOGREC_TYPE:
		case LOG_LARGER_LOGREC_TYPE:
		default:
#if defined(CUBRID_DEBUG)
		  er_log_debug (ARG_FILE_LINE,
				"log_recovery_undo: Unknown record type = %d (%s)\n ... May be a system error",
				log_rtype, log_to_string (log_rtype));
#endif /* CUBRID_DEBUG */
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_PAGE_CORRUPTED, 1, log_lsa.pageid);
		  assert (false);

		  /* 
		   * Remove the transaction from the recovery process
		   */

		  /* Clear MVCCID */
		  tdes->mvccinfo.id = MVCCID_NULL;

		  if (LOG_IS_VACUUM_THREAD_TRANID (tran_id))
		    {
		      vacuum_rv_finish_worker_recovery (thread_p, tran_id);
		    }
		  else
		    {
		      (void) log_complete (thread_p, tdes, LOG_ABORT, LOG_DONT_NEED_NEWTRID, LOG_NEED_TO_WRITE_EOT_LOG);
		      logtb_free_tran_index (thread_p, tran_index);
		    }
		  tdes = NULL;
		  break;
		}

	      /* Just in case, it was changed */
	      if (tdes != NULL)
		{
		  /* Is this the end of transaction? */
		  if (LSA_ISNULL (&prev_tranlsa))
		    {
		      /* Clear MVCCID */
		      tdes->mvccinfo.id = MVCCID_NULL;

		      if (LOG_IS_VACUUM_THREAD_TRANID (tran_id))
			{
			  vacuum_rv_finish_worker_recovery (thread_p, tran_id);
			}
		      else
			{
			  (void) log_complete (thread_p, tdes, LOG_ABORT, LOG_DONT_NEED_NEWTRID,
					       LOG_NEED_TO_WRITE_EOT_LOG);
			  logtb_free_tran_index (thread_p, tran_index);
			  tdes = NULL;
			}
		    }
		  else
		    {
		      /* Update transaction next undo LSA */
		      LSA_COPY (&tdes->undo_nxlsa, &prev_tranlsa);

		      if (LOG_IS_VACUUM_THREAD_TRANID (tdes->trid))
			{
			  vacuum_er_log (VACUUM_ER_LOG_RECOVERY | VACUUM_ER_LOG_TOPOPS,
					 "VACUUM: Update undo_nxlsa=(%lld, %d) for tdes->trid=%d.",
					 (long long int) tdes->undo_nxlsa.pageid, (int) tdes->undo_nxlsa.offset,
					 tdes->trid);
			}
		    }
		}
	    }

	  /* Find the next log record to undo */
	  lsa_ptr = log_find_unilaterally_largest_undo_lsa (thread_p);
	}
    }

  log_zip_free (undo_unzip_ptr);

  /* Flush all dirty pages */

  logpb_flush_pages_direct (thread_p);

  logpb_flush_header (thread_p);
  (void) pgbuf_flush_all (thread_p, NULL_VOLID);

  return;
}

/*
 * log_recovery_notpartof_archives - REMOVE ARCHIVES THAT ARE NOT PART OF
 *                                 DATABASE (USED for PARTIAL RECOVERY)
 *
 * return: nothing..
 *
 *   start_arv_num(in): Start removing archives at this point.
 *   info_reason(in): Reason for removal
 *
 * NOTE: Remove archives that are not part of database any longer.
 *              This happen when we do partial recovery.
 */
static void
log_recovery_notpartof_archives (THREAD_ENTRY * thread_p, int start_arv_num, const char *info_reason)
{
  char logarv_name[PATH_MAX];	/* Archive name */
  char logarv_name_first[PATH_MAX];	/* Archive name */
  int i;
  const char *catmsg;
  int error_code;

  if (log_Gl.append.vdes != NULL_VOLDES)
    {
      /* 
       * Trust the current log header to remove any archives that are not
       * needed due to partial recovery
       */
      for (i = start_arv_num; i <= log_Gl.hdr.nxarv_num - 1; i++)
	{
	  fileio_make_log_archive_name (logarv_name, log_Archive_path, log_Prefix, i);
	  fileio_unformat (thread_p, logarv_name);
	}
    }
  else
    {
      /* 
       * We don't know where to stop. Stop when an archive is not in the OS
       * This may not be good enough.
       */
      for (i = start_arv_num; i <= INT_MAX; i++)
	{
	  fileio_make_log_archive_name (logarv_name, log_Archive_path, log_Prefix, i);
	  if (fileio_is_volume_exist (logarv_name) == false)
	    {
	      if (i > start_arv_num)
		{
		  fileio_make_log_archive_name (logarv_name, log_Archive_path, log_Prefix, i - 1);
		}
	      break;
	    }
	  fileio_unformat (thread_p, logarv_name);
	}
    }

  if (info_reason != NULL)
    /* 
     * Note if start_arv_num == i, we break from the loop and did not remove
     * anything
     */
    if (start_arv_num != i)
      {
	if (start_arv_num == i - 1)
	  {
	    catmsg = msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_LOG, MSGCAT_LOG_LOGINFO_REMOVE_REASON);
	    if (catmsg == NULL)
	      {
		catmsg = "REMOVE: %d %s to \n%d %s.\nREASON: %s\n";
	      }
	    error_code =
	      log_dump_log_info (log_Name_info, true, catmsg, start_arv_num, logarv_name, start_arv_num, logarv_name,
				 info_reason);
	    if (error_code != NO_ERROR && error_code != ER_LOG_MOUNT_FAIL)
	      {
		return;
	      }
	  }
	else
	  {
	    fileio_make_log_archive_name (logarv_name_first, log_Archive_path, log_Prefix, start_arv_num);

	    catmsg = msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_LOG, MSGCAT_LOG_LOGINFO_REMOVE_REASON);
	    if (catmsg == NULL)
	      {
		catmsg = "REMOVE: %d %s to \n%d %s.\nREASON: %s\n";
	      }
	    error_code =
	      log_dump_log_info (log_Name_info, true, catmsg, start_arv_num, logarv_name_first, i - 1, logarv_name,
				 info_reason);
	    if (error_code != NO_ERROR && error_code != ER_LOG_MOUNT_FAIL)
	      {
		return;
	      }
	  }
      }

  log_Gl.hdr.last_deleted_arv_num = (start_arv_num == i) ? i : i - 1;


  if (log_Gl.append.vdes != NULL_VOLDES)
    {
      logpb_flush_header (thread_p);	/* to get rid off archives */
    }

}

/*
 * log_unformat_ahead_volumes -
 *
 * return:
 *
 *   volid(in):
 *   start_volid(in):
 *
 * NOTE:
 */
static bool
log_unformat_ahead_volumes (THREAD_ENTRY * thread_p, VOLID volid, VOLID * start_volid)
{
  bool result = true;

  if (volid != NULL_VOLID && volid >= *start_volid)
    {
      /* This volume is not part of the database any longer */
      if (pgbuf_invalidate_all (thread_p, volid) != NO_ERROR)
	{
	  result = false;
	}
      else
	{
	  char *vlabel = fileio_get_volume_label (volid, ALLOC_COPY);
	  fileio_unformat (thread_p, vlabel);
	  if (vlabel)
	    {
	      free (vlabel);
	    }
	}
    }
  return result;
}

/*
 * log_recovery_notpartof_volumes -
 *
 * return:
 *
 * NOTE:
 */
static void
log_recovery_notpartof_volumes (THREAD_ENTRY * thread_p)
{
  const char *ext_path;
  const char *ext_name;
  int vdes;
  VOLID start_volid;
  VOLID volid;
  char vol_fullname[PATH_MAX];
  INT64 vol_dbcreation;		/* Database creation time in volume */
  char *alloc_extpath = NULL;
  int ret = NO_ERROR;

  start_volid = boot_find_next_permanent_volid (thread_p);

  /* 
   * FIRST: ASSUME VOLUME INFORMATION WAS AHEAD OF US.
   * Start removing mounted volumes that are not part of the database any
   * longer due to partial recovery point. Note that these volumes were
   * mounted before the recovery started.
   */

  (void) fileio_map_mounted (thread_p, (bool (*)(THREAD_ENTRY *, VOLID, void *)) log_unformat_ahead_volumes,
			     &start_volid);

  /* 
   * SECOND: ASSUME RIGHT VOLUME INFORMATION.
   * Remove any volumes that are laying around on disk
   */

  /* 
   * Get the name of the extension: ext_path|dbname|"ext"|volid
   */

  /* Use the directory where the primary volume is located */
  alloc_extpath = (char *) malloc (PATH_MAX);
  if (alloc_extpath != NULL)
    {
      ext_path = fileio_get_directory_path (alloc_extpath, log_Db_fullname);
      if (ext_path == NULL)
	{
	  alloc_extpath[0] = '\0';
	  ext_path = alloc_extpath;
	}
    }
  else
    {
      ext_path = "";		/* Pointer to a null terminated string */
    }

  ext_name = fileio_get_base_file_name (log_Db_fullname);
  /* 
   * We don't know where to stop. Stop when an archive is not in the OS
   */

  for (volid = start_volid; volid < LOG_MAX_DBVOLID; volid++)
    {
      fileio_make_volume_ext_name (vol_fullname, ext_path, ext_name, volid);
      if (fileio_is_volume_exist (vol_fullname) == false)
	{
	  break;
	}

      vdes = fileio_mount (thread_p, log_Db_fullname, vol_fullname, volid, false, false);
      if (vdes != NULL_VOLDES)
	{
	  ret = disk_get_creation_time (thread_p, volid, &vol_dbcreation);
	  fileio_dismount (thread_p, vdes);
	  if (difftime ((time_t) vol_dbcreation, (time_t) log_Gl.hdr.db_creation) != 0)
	    {
	      /* This volume does not belong to given database */
	      ;			/* NO-OP */
	    }
	  else
	    {
	      fileio_unformat (thread_p, vol_fullname);
	    }
	}
    }

  if (alloc_extpath)
    {
      free_and_init (alloc_extpath);
    }

  (void) logpb_recreate_volume_info (thread_p);

}

/*
 * log_recovery_resetlog -
 *
 * return:
 *
 *   new_appendlsa(in):
 *
 * NOTE:
 */
static void
log_recovery_resetlog (THREAD_ENTRY * thread_p, LOG_LSA * new_append_lsa, bool is_new_append_page, LOG_LSA * last_lsa)
{
  char newappend_pgbuf[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT];
  char *aligned_newappend_pgbuf;
  LOG_PAGE *newappend_pgptr = NULL;
  int arv_num;
  const char *catmsg;
  char *catmsg_dup;
  int ret = NO_ERROR;

  assert (LOG_CS_OWN_WRITE_MODE (thread_p));
  assert (last_lsa != NULL);

  aligned_newappend_pgbuf = PTR_ALIGN (newappend_pgbuf, MAX_ALIGNMENT);

  if (log_Gl.append.vdes != NULL_VOLDES && log_Gl.append.log_pgptr != NULL)
    {
      logpb_flush_pages_direct (thread_p);
      logpb_invalid_all_append_pages (thread_p);
    }

  if (LSA_ISNULL (new_append_lsa))
    {
      log_Gl.hdr.append_lsa.pageid = 0;
      log_Gl.hdr.append_lsa.offset = 0;
      LOG_RESET_APPEND_LSA (&log_Gl.hdr.append_lsa);
    }
  else
    {
      if (log_Gl.append.vdes == NULL_VOLDES
	  || (log_Gl.hdr.fpageid > new_append_lsa->pageid && new_append_lsa->offset > 0))
	{
	  /* 
	   * We are going to rest the header of the active log to the past.
	   * Save the content of the new append page since it has to be
	   * transfered to the new location. This is needed since we may not
	   * start at location zero.
	   *
	   * We need to destroy any log archive createded after this point
	   */

	  newappend_pgptr = (LOG_PAGE *) aligned_newappend_pgbuf;

	  if ((logpb_fetch_page (thread_p, new_append_lsa, LOG_CS_FORCE_USE, newappend_pgptr)) != NO_ERROR)
	    {
	      newappend_pgptr = NULL;
	    }
	}
      LOG_RESET_APPEND_LSA (new_append_lsa);
    }

  LSA_COPY (&log_Gl.hdr.chkpt_lsa, &log_Gl.hdr.append_lsa);
  log_Gl.hdr.is_shutdown = false;

  logpb_invalidate_pool (thread_p);

  if (log_Gl.append.vdes == NULL_VOLDES || log_Gl.hdr.fpageid > log_Gl.hdr.append_lsa.pageid)
    {
      LOG_PAGE *loghdr_pgptr = NULL;
      LOG_PAGE *append_pgptr = NULL;

      /* 
       * Don't have the log active, or we are going to the past
       */
      arv_num = logpb_get_archive_number (thread_p, log_Gl.hdr.append_lsa.pageid - 1);
      if (arv_num == -1)
	{
	  logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_recovery_resetlog");
	  return;
	}
      arv_num = arv_num + 1;

      catmsg = msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_LOG, MSGCAT_LOG_RESETLOG_DUE_INCOMPLTE_MEDIA_RECOVERY);
      catmsg_dup = strdup (catmsg);
      if (catmsg_dup != NULL)
	{
	  log_recovery_notpartof_archives (thread_p, arv_num, catmsg_dup);
	  free_and_init (catmsg_dup);
	}
      else
	{
	  /* NOTE: catmsg..may get corrupted if the function calls the catalog */
	  log_recovery_notpartof_archives (thread_p, arv_num, catmsg);
	}

      log_Gl.hdr.fpageid = log_Gl.hdr.append_lsa.pageid;
      log_Gl.hdr.nxarv_pageid = log_Gl.hdr.append_lsa.pageid;
      log_Gl.hdr.nxarv_phy_pageid = logpb_to_physical_pageid (log_Gl.hdr.nxarv_pageid);
      log_Gl.hdr.nxarv_num = arv_num;
      log_Gl.hdr.last_arv_num_for_syscrashes = -1;
      log_Gl.hdr.last_deleted_arv_num = -1;

      if (log_Gl.append.vdes == NULL_VOLDES)
	{
	  /* Create the log active since we do not have one */
	  ret = disk_get_creation_time (thread_p, LOG_DBFIRST_VOLID, &log_Gl.hdr.db_creation);
	  if (ret != NO_ERROR)
	    {
	      logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_recovery_resetlog");
	      return;
	    }

	  log_Gl.append.vdes =
	    fileio_format (thread_p, log_Db_fullname, log_Name_active, LOG_DBLOG_ACTIVE_VOLID,
			   log_get_num_pages_for_creation (-1), false, true, false, LOG_PAGESIZE, 0, false);

	  if (log_Gl.append.vdes != NULL_VOLDES)
	    {
	      loghdr_pgptr = logpb_create_header_page (thread_p);
	    }

	  if (log_Gl.append.vdes == NULL_VOLDES || loghdr_pgptr == NULL)
	    {
	      logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_recovery_resetlog");
	      return;
	    }

	  /* 
	   * Flush the header page and first append page so that we can record
	   * the header page on it
	   */
	  if (logpb_flush_page (thread_p, loghdr_pgptr) != NO_ERROR)
	    {
	      logpb_fatal_error (thread_p, false, ARG_FILE_LINE, "log_recovery_resetlog");
	    }
	}

      append_pgptr = logpb_create_page (thread_p, log_Gl.hdr.fpageid);
      if (append_pgptr == NULL)
	{
	  logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_recovery_resetlog");
	  return;
	}
      if (logpb_flush_page (thread_p, append_pgptr) != NO_ERROR)
	{
	  logpb_fatal_error (thread_p, false, ARG_FILE_LINE, "log_recovery_resetlog");
	  return;
	}
    }
  else
    {
      /* 
       * There is already a log active and the new append location is in the
       * current range. Leave the log as it is, just reset the append location.
       */
      if (log_Gl.hdr.nxarv_pageid >= log_Gl.hdr.append_lsa.pageid)
	{
	  log_Gl.hdr.nxarv_pageid = log_Gl.hdr.append_lsa.pageid;
	  log_Gl.hdr.nxarv_phy_pageid = logpb_to_physical_pageid (log_Gl.hdr.nxarv_pageid);
	}
    }

  /* 
   * Fetch the append page and write it with and end of log mark.
   * Then, free the page, same for the header page.
   */

  if (is_new_append_page == true)
    {
      if (logpb_fetch_start_append_page_new (thread_p) == NULL)
	{
	  logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_recovery_resetlog");
	  return;
	}
    }
  else
    {
      if (logpb_fetch_start_append_page (thread_p) == NO_ERROR)
	{
	  if (newappend_pgptr != NULL && log_Gl.append.log_pgptr != NULL)
	    {
	      memcpy ((char *) log_Gl.append.log_pgptr, (char *) newappend_pgptr, LOG_PAGESIZE);
	      logpb_set_dirty (thread_p, log_Gl.append.log_pgptr);
	    }
	  logpb_flush_pages_direct (thread_p);
	}
    }

  LOG_RESET_PREV_LSA (last_lsa);

  logpb_flush_header (thread_p);
  logpb_decache_archive_info (thread_p);

  return;
}

/*
 * log_startof_nxrec - FIND START OF NEXT RECORD (USED FOR PARTIAL RECOVERY)
 *
 * return: lsa or NULL in case of error
 *
 *   lsa(in):  Starting address. Set as a side effect to next address
 *   canuse_forwaddr(in): Use forward address if available
 *
 * NOTE:Find start address of next record either by looking to forward
 *              address or by scanning the current record.
 */
LOG_LSA *
log_startof_nxrec (THREAD_ENTRY * thread_p, LOG_LSA * lsa, bool canuse_forwaddr)
{
  char log_pgbuf[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT], *aligned_log_pgbuf;
  LOG_PAGE *log_pgptr = NULL;	/* Log page pointer where LSA is located */
  LOG_LSA log_lsa;
  LOG_RECTYPE type;		/* Log record type */
  LOG_RECORD_HEADER *log_rec;	/* Pointer to log record */
  LOG_REC_UNDOREDO *undoredo;	/* Undo_redo log record */
  LOG_REC_UNDO *undo;		/* Undo log record */
  LOG_REC_REDO *redo;		/* Redo log record */
  LOG_REC_MVCC_UNDOREDO *mvcc_undoredo;	/* MVCC op undo_redo log record */
  LOG_REC_MVCC_UNDO *mvcc_undo;	/* MVCC op undo log record */
  LOG_REC_MVCC_REDO *mvcc_redo;	/* MVCC op redo log record */
  LOG_REC_DBOUT_REDO *dbout_redo;	/* A external redo log record */
  LOG_REC_SAVEPT *savept;	/* A savepoint log record */
  LOG_REC_COMPENSATE *compensate;	/* Compensating log record */
  LOG_REC_RUN_POSTPONE *run_posp;	/* A run postpone action */
  LOG_REC_CHKPT *chkpt;		/* Checkpoint log record */
  LOG_REC_2PC_START *start_2pc;	/* A 2PC start log record */
  LOG_REC_2PC_PREPCOMMIT *prepared;	/* A 2PC prepare to commit */
  LOG_REC_REPLICATION *repl_log;

  int undo_length;		/* Undo length */
  int redo_length;		/* Redo length */
  unsigned int nobj_locks;
  int repl_log_length;
  size_t size;

  aligned_log_pgbuf = PTR_ALIGN (log_pgbuf, MAX_ALIGNMENT);

  if (LSA_ISNULL (lsa))
    {
      return NULL;
    }

  log_pgptr = (LOG_PAGE *) aligned_log_pgbuf;

  if (logpb_fetch_page (thread_p, lsa, LOG_CS_FORCE_USE, log_pgptr) != NO_ERROR)
    {
      fprintf (stdout, " Error reading page %lld... Quit\n", (long long int) lsa->pageid);
      goto error;
    }

  /* 
   * If offset is missing, it is because we archive an incomplete
   * log record or we start dumping the log not from its first page. We
   * have to find the offset by searching for the next log_record in the page
   */
  if (lsa->offset == NULL_OFFSET)
    {
      lsa->offset = log_pgptr->hdr.offset;
      if (lsa->offset == NULL_OFFSET)
	{
	  goto error;
	}
    }

  LSA_COPY (&log_lsa, lsa);
  log_rec = LOG_GET_LOG_RECORD_HEADER (log_pgptr, &log_lsa);
  type = log_rec->type;

  if (canuse_forwaddr == true)
    {
      /* 
       * Use forward address of current log record
       */
      LSA_COPY (lsa, &log_rec->forw_lsa);
      if (LSA_ISNULL (lsa) && logpb_is_page_in_archive (log_lsa.pageid))
	{
	  lsa->pageid = log_lsa.pageid + 1;
	}

      if (!LSA_ISNULL (lsa))
	{
	  return lsa;
	}
    }

  /* Advance the pointer to log_rec data */

  LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_RECORD_HEADER), &log_lsa, log_pgptr);
  switch (type)
    {
    case LOG_UNDOREDO_DATA:
    case LOG_DIFF_UNDOREDO_DATA:
      /* Read the DATA HEADER */
      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (LOG_REC_UNDOREDO), &log_lsa, log_pgptr);
      undoredo = (LOG_REC_UNDOREDO *) ((char *) log_pgptr->area + log_lsa.offset);

      undo_length = (int) GET_ZIP_LEN (undoredo->ulength);
      redo_length = (int) GET_ZIP_LEN (undoredo->rlength);

      LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_REC_UNDOREDO), &log_lsa, log_pgptr);
      LOG_READ_ADD_ALIGN (thread_p, undo_length, &log_lsa, log_pgptr);
      LOG_READ_ADD_ALIGN (thread_p, redo_length, &log_lsa, log_pgptr);
      break;

    case LOG_MVCC_UNDOREDO_DATA:
    case LOG_MVCC_DIFF_UNDOREDO_DATA:
      /* Read the DATA HEADER */
      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (LOG_REC_MVCC_UNDOREDO), &log_lsa, log_pgptr);
      mvcc_undoredo = (LOG_REC_MVCC_UNDOREDO *) ((char *) log_pgptr->area + log_lsa.offset);

      undo_length = (int) GET_ZIP_LEN (mvcc_undoredo->undoredo.ulength);
      redo_length = (int) GET_ZIP_LEN (mvcc_undoredo->undoredo.rlength);

      LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_REC_MVCC_UNDOREDO), &log_lsa, log_pgptr);
      LOG_READ_ADD_ALIGN (thread_p, undo_length, &log_lsa, log_pgptr);
      LOG_READ_ADD_ALIGN (thread_p, redo_length, &log_lsa, log_pgptr);
      break;

    case LOG_UNDO_DATA:
      /* Read the DATA HEADER */
      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (LOG_REC_UNDO), &log_lsa, log_pgptr);
      undo = (LOG_REC_UNDO *) ((char *) log_pgptr->area + log_lsa.offset);

      undo_length = (int) GET_ZIP_LEN (undo->length);

      LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_REC_UNDO), &log_lsa, log_pgptr);
      LOG_READ_ADD_ALIGN (thread_p, undo_length, &log_lsa, log_pgptr);
      break;

    case LOG_MVCC_UNDO_DATA:
      /* Read the DATA HEADER */
      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (LOG_REC_MVCC_UNDO), &log_lsa, log_pgptr);
      mvcc_undo = (LOG_REC_MVCC_UNDO *) ((char *) log_pgptr->area + log_lsa.offset);

      undo_length = (int) GET_ZIP_LEN (mvcc_undo->undo.length);

      LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_REC_MVCC_UNDO), &log_lsa, log_pgptr);
      LOG_READ_ADD_ALIGN (thread_p, undo_length, &log_lsa, log_pgptr);
      break;

    case LOG_MVCC_REDO_DATA:
      /* Read the DATA HEADER */
      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (LOG_REC_MVCC_REDO), &log_lsa, log_pgptr);
      mvcc_redo = (LOG_REC_MVCC_REDO *) ((char *) log_pgptr->area + log_lsa.offset);
      redo_length = (int) GET_ZIP_LEN (mvcc_redo->redo.length);

      LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_REC_MVCC_REDO), &log_lsa, log_pgptr);
      LOG_READ_ADD_ALIGN (thread_p, redo_length, &log_lsa, log_pgptr);
      break;

    case LOG_REDO_DATA:
    case LOG_POSTPONE:
      /* Read the DATA HEADER */
      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (LOG_REC_REDO), &log_lsa, log_pgptr);
      redo = (LOG_REC_REDO *) ((char *) log_pgptr->area + log_lsa.offset);
      redo_length = (int) GET_ZIP_LEN (redo->length);

      LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_REC_REDO), &log_lsa, log_pgptr);
      LOG_READ_ADD_ALIGN (thread_p, redo_length, &log_lsa, log_pgptr);
      break;

    case LOG_RUN_POSTPONE:
      /* Read the DATA HEADER */
      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (LOG_REC_RUN_POSTPONE), &log_lsa, log_pgptr);
      run_posp = (LOG_REC_RUN_POSTPONE *) ((char *) log_pgptr->area + log_lsa.offset);
      redo_length = run_posp->length;

      LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_REC_RUN_POSTPONE), &log_lsa, log_pgptr);
      LOG_READ_ADD_ALIGN (thread_p, redo_length, &log_lsa, log_pgptr);
      break;

    case LOG_DBEXTERN_REDO_DATA:
      /* Read the data header */
      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (LOG_REC_DBOUT_REDO), &log_lsa, log_pgptr);
      dbout_redo = ((LOG_REC_DBOUT_REDO *) ((char *) log_pgptr->area + log_lsa.offset));
      redo_length = dbout_redo->length;

      LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_REC_DBOUT_REDO), &log_lsa, log_pgptr);
      LOG_READ_ADD_ALIGN (thread_p, redo_length, &log_lsa, log_pgptr);
      break;

    case LOG_COMPENSATE:
      /* Read the DATA HEADER */
      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (LOG_REC_COMPENSATE), &log_lsa, log_pgptr);
      compensate = (LOG_REC_COMPENSATE *) ((char *) log_pgptr->area + log_lsa.offset);
      redo_length = compensate->length;

      LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_REC_COMPENSATE), &log_lsa, log_pgptr);
      LOG_READ_ADD_ALIGN (thread_p, redo_length, &log_lsa, log_pgptr);
      break;

    case LOG_COMMIT_WITH_POSTPONE:
      /* Read the DATA HEADER */
      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (LOG_REC_START_POSTPONE), &log_lsa, log_pgptr);

      LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_REC_START_POSTPONE), &log_lsa, log_pgptr);
      break;

    case LOG_COMMIT:
    case LOG_ABORT:
      /* Read the DATA HEADER */
      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (LOG_REC_DONETIME), &log_lsa, log_pgptr);

      LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_REC_DONETIME), &log_lsa, log_pgptr);
      break;

    case LOG_SYSOP_START_POSTPONE:
      {
	LOG_REC_SYSOP_START_POSTPONE *sysop_start_postpone;
	int undo_size = 0;

	/* Read the DATA HEADER */
	LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (LOG_REC_SYSOP_START_POSTPONE), &log_lsa, log_pgptr);
	sysop_start_postpone = (LOG_REC_SYSOP_START_POSTPONE *) (log_pgptr->area + log_lsa.offset);
	if (sysop_start_postpone->sysop_end.type == LOG_SYSOP_END_LOGICAL_UNDO)
	  {
	    undo_size = sysop_start_postpone->sysop_end.undo.length;
	  }
	else if (sysop_start_postpone->sysop_end.type == LOG_SYSOP_END_LOGICAL_MVCC_UNDO)
	  {
	    undo_size = sysop_start_postpone->sysop_end.mvcc_undo.undo.length;
	  }
	LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_REC_SYSOP_START_POSTPONE), &log_lsa, log_pgptr);
	LOG_READ_ADD_ALIGN (thread_p, undo_size, &log_lsa, log_pgptr);
      }
      break;

    case LOG_SYSOP_END:
      {
	LOG_REC_SYSOP_END *sysop_end;
	int undo_size = 0;

	/* Read the DATA HEADER */
	LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (LOG_REC_SYSOP_END), &log_lsa, log_pgptr);

	sysop_end = (LOG_REC_SYSOP_END *) (log_pgptr->area + log_lsa.offset);

	if (sysop_end->type == LOG_SYSOP_END_LOGICAL_UNDO)
	  {
	    undo_size = sysop_end->undo.length;
	  }
	else if (sysop_end->type == LOG_SYSOP_END_LOGICAL_MVCC_UNDO)
	  {
	    undo_size = sysop_end->mvcc_undo.undo.length;
	  }
	LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_REC_SYSOP_END), &log_lsa, log_pgptr);
      }
      break;

    case LOG_END_CHKPT:
      /* Read the DATA HEADER */
      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (LOG_REC_CHKPT), &log_lsa, log_pgptr);
      chkpt = (LOG_REC_CHKPT *) ((char *) log_pgptr->area + log_lsa.offset);
      undo_length = sizeof (LOG_INFO_CHKPT_TRANS) * chkpt->ntrans;
      redo_length = sizeof (LOG_INFO_CHKPT_SYSOP_START_POSTPONE) * chkpt->ntops;

      LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_REC_CHKPT), &log_lsa, log_pgptr);
      LOG_READ_ADD_ALIGN (thread_p, undo_length, &log_lsa, log_pgptr);
      if (redo_length > 0)
	{
	  LOG_READ_ADD_ALIGN (thread_p, redo_length, &log_lsa, log_pgptr);
	}
      break;

    case LOG_SAVEPOINT:
      /* Read the DATA HEADER */
      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (LOG_REC_SAVEPT), &log_lsa, log_pgptr);
      savept = (LOG_REC_SAVEPT *) ((char *) log_pgptr->area + log_lsa.offset);
      undo_length = savept->length;

      LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_REC_SAVEPT), &log_lsa, log_pgptr);
      LOG_READ_ADD_ALIGN (thread_p, undo_length, &log_lsa, log_pgptr);
      break;

    case LOG_2PC_PREPARE:
      /* Get the DATA HEADER */
      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (LOG_REC_2PC_PREPCOMMIT), &log_lsa, log_pgptr);
      prepared = (LOG_REC_2PC_PREPCOMMIT *) ((char *) log_pgptr->area + log_lsa.offset);
      nobj_locks = prepared->num_object_locks;
      /* ignore npage_locks */

      LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_REC_2PC_PREPCOMMIT), &log_lsa, log_pgptr);

      if (prepared->gtrinfo_length > 0)
	{
	  LOG_READ_ADD_ALIGN (thread_p, prepared->gtrinfo_length, &log_lsa, log_pgptr);
	}

      if (nobj_locks > 0)
	{
	  size = nobj_locks * sizeof (LK_ACQOBJ_LOCK);
	  LOG_READ_ADD_ALIGN (thread_p, (INT16) size, &log_lsa, log_pgptr);
	}
      break;

    case LOG_2PC_START:
      /* Get the DATA HEADER */
      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (LOG_REC_2PC_START), &log_lsa, log_pgptr);
      start_2pc = (LOG_REC_2PC_START *) ((char *) log_pgptr->area + log_lsa.offset);

      LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_REC_2PC_START), &log_lsa, log_pgptr);
      LOG_READ_ADD_ALIGN (thread_p, (start_2pc->particp_id_length * start_2pc->num_particps), &log_lsa, log_pgptr);
      break;

    case LOG_2PC_RECV_ACK:
      /* Get the DATA HEADER */
      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (LOG_REC_2PC_PARTICP_ACK), &log_lsa, log_pgptr);

      LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_REC_2PC_PARTICP_ACK), &log_lsa, log_pgptr);
      break;

    case LOG_WILL_COMMIT:
    case LOG_START_CHKPT:
    case LOG_2PC_COMMIT_DECISION:
    case LOG_2PC_ABORT_DECISION:
    case LOG_2PC_COMMIT_INFORM_PARTICPS:
    case LOG_2PC_ABORT_INFORM_PARTICPS:
    case LOG_DUMMY_HEAD_POSTPONE:
    case LOG_DUMMY_CRASH_RECOVERY:
    case LOG_DUMMY_OVF_RECORD:
    case LOG_DUMMY_GENERIC:
    case LOG_END_OF_LOG:
      break;

    case LOG_REPLICATION_DATA:
    case LOG_REPLICATION_STATEMENT:
      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (LOG_REC_REPLICATION), &log_lsa, log_pgptr);

      repl_log = (LOG_REC_REPLICATION *) ((char *) log_pgptr->area + log_lsa.offset);
      repl_log_length = (int) GET_ZIP_LEN (repl_log->length);

      LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_REC_REPLICATION), &log_lsa, log_pgptr);
      LOG_READ_ADD_ALIGN (thread_p, repl_log_length, &log_lsa, log_pgptr);
      break;

    case LOG_DUMMY_HA_SERVER_STATE:
      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (LOG_REC_HA_SERVER_STATE), &log_lsa, log_pgptr);
      LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_REC_HA_SERVER_STATE), &log_lsa, log_pgptr);
      break;

    case LOG_SMALLER_LOGREC_TYPE:
    case LOG_LARGER_LOGREC_TYPE:
    default:
      break;
    }

  /* Make sure you point to beginning of a next record */
  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (LOG_RECORD_HEADER), &log_lsa, log_pgptr);

  LSA_COPY (lsa, &log_lsa);

  return lsa;

error:

  return NULL;
}

/*
 * log_recovery_find_first_postpone -
 *      Find the first postpone log lsa to be applied.
 *
 * return: error code
 *
 *   ret_lsa(out):
 *   start_postpone_lsa(in):
 *   tdes(in):
 *
 */
static int
log_recovery_find_first_postpone (THREAD_ENTRY * thread_p, LOG_LSA * ret_lsa, LOG_LSA * start_postpone_lsa,
				  LOG_TDES * tdes)
{
  LOG_LSA end_postpone_lsa;
  LOG_LSA start_seek_lsa;
  LOG_LSA *end_seek_lsa;
  LOG_LSA next_start_seek_lsa;
  LOG_LSA log_lsa;
  LOG_LSA forward_lsa;
  LOG_LSA next_postpone_lsa;
  LOG_LSA local_start_postpone_run_lsa;
  LOG_REC_RUN_POSTPONE *run_posp;

  char log_pgbuf[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT];
  char *aligned_log_pgbuf;
  LOG_PAGE *log_pgptr = NULL;
  LOG_RECORD_HEADER *log_rec;
  bool isdone;

  LOG_TOPOP_RANGE nxtop_array[LOG_TOPOP_STACK_INIT_SIZE];
  LOG_TOPOP_RANGE *nxtop_stack = NULL;
  LOG_TOPOP_RANGE *nxtop_range = NULL;
  int nxtop_count = 0;
  bool start_postpone_lsa_wasapplied = false;

  LOG_REC_SYSOP_END *topop_result = NULL;
  bool found_commit_with_postpone = false;

  assert (ret_lsa && start_postpone_lsa && tdes);

  LSA_SET_NULL (ret_lsa);

  if (log_is_in_crash_recovery () == false
      || (tdes->state != TRAN_UNACTIVE_WILL_COMMIT && tdes->state != TRAN_UNACTIVE_COMMITTED_WITH_POSTPONE
	  && tdes->state != TRAN_UNACTIVE_TOPOPE_COMMITTED_WITH_POSTPONE))
    {
      assert (0);
      return ER_FAILED;
    }

  if (LSA_ISNULL (start_postpone_lsa))
    {
      return NO_ERROR;
    }

  LSA_SET_NULL (&next_postpone_lsa);

  aligned_log_pgbuf = PTR_ALIGN (log_pgbuf, MAX_ALIGNMENT);
  log_pgptr = (LOG_PAGE *) aligned_log_pgbuf;

  LSA_COPY (&end_postpone_lsa, &tdes->tail_lsa);
  LSA_COPY (&next_start_seek_lsa, start_postpone_lsa);

  nxtop_stack = nxtop_array;
  nxtop_count = log_get_next_nested_top (thread_p, tdes, start_postpone_lsa, &nxtop_stack);

  while (!LSA_ISNULL (&next_start_seek_lsa))
    {
      LSA_COPY (&start_seek_lsa, &next_start_seek_lsa);

      if (nxtop_count > 0)
	{
	  nxtop_count--;
	  nxtop_range = &(nxtop_stack[nxtop_count]);

	  if (LSA_LT (&start_seek_lsa, &(nxtop_range->start_lsa)))
	    {
	      end_seek_lsa = &(nxtop_range->start_lsa);
	      LSA_COPY (&next_start_seek_lsa, &(nxtop_range->end_lsa));
	    }
	  else if (LSA_EQ (&start_seek_lsa, &(nxtop_range->end_lsa)))
	    {
	      end_seek_lsa = &end_postpone_lsa;
	      LSA_SET_NULL (&next_start_seek_lsa);
	    }
	  else
	    {
	      LSA_COPY (&next_start_seek_lsa, &(nxtop_range->end_lsa));
	      continue;
	    }
	}
      else
	{
	  end_seek_lsa = &end_postpone_lsa;
	  LSA_SET_NULL (&next_start_seek_lsa);
	}

      /* 
       * Start doing postpone operation for this range
       */

      LSA_COPY (&forward_lsa, &start_seek_lsa);

      isdone = false;
      while (!LSA_ISNULL (&forward_lsa) && !isdone)
	{
	  /* Fetch the page where the postpone LSA record is located */
	  LSA_COPY (&log_lsa, &forward_lsa);
	  if (logpb_fetch_page (thread_p, &log_lsa, LOG_CS_FORCE_USE, log_pgptr) != NO_ERROR)
	    {
	      logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_recovery_find_first_postpone");
	      goto end;
	    }

	  while (forward_lsa.pageid == log_lsa.pageid && !isdone)
	    {
	      if (LSA_GT (&forward_lsa, end_seek_lsa))
		{
		  /* Finish at this point */
		  isdone = true;
		  break;
		}
	      /* 
	       * If an offset is missing, it is because we archive an incomplete
	       * log record. This log_record was completed later.
	       * Thus, we have to find the offset by searching
	       * for the next log_record in the page.
	       */
	      if (forward_lsa.offset == NULL_OFFSET)
		{
		  forward_lsa.offset = log_pgptr->hdr.offset;
		  if (forward_lsa.offset == NULL_OFFSET)
		    {
		      /* Continue at next pageid */
		      if (logpb_is_page_in_archive (log_lsa.pageid))
			{
			  forward_lsa.pageid = log_lsa.pageid + 1;
			}
		      else
			{
			  forward_lsa.pageid = NULL_PAGEID;
			}
		      continue;
		    }
		}

	      log_lsa.offset = forward_lsa.offset;
	      log_rec = LOG_GET_LOG_RECORD_HEADER (log_pgptr, &log_lsa);

	      /* Find the next log record in the log */
	      LSA_COPY (&forward_lsa, &log_rec->forw_lsa);

	      if (forward_lsa.pageid == NULL_PAGEID && logpb_is_page_in_archive (log_lsa.pageid))
		{
		  forward_lsa.pageid = log_lsa.pageid + 1;
		}

	      if (log_rec->trid == tdes->trid)
		{
		  switch (log_rec->type)
		    {
		    case LOG_RUN_POSTPONE:
		      LSA_COPY (&local_start_postpone_run_lsa, &log_lsa);
		      LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_RECORD_HEADER), &log_lsa, log_pgptr);

		      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (LOG_REC_RUN_POSTPONE), &log_lsa, log_pgptr);

		      run_posp = (LOG_REC_RUN_POSTPONE *) ((char *) log_pgptr->area + log_lsa.offset);

		      if (LSA_EQ (start_postpone_lsa, &run_posp->ref_lsa))
			{
			  /* run_postpone_log of start_postpone is found, next_postpone_lsa is the first postpone to be 
			   * applied. */

			  start_postpone_lsa_wasapplied = true;
			  isdone = true;
			}
		      break;

		    case LOG_SYSOP_END:
		      {
			LOG_REC_SYSOP_END *sysop_end = NULL;

			LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_RECORD_HEADER), &log_lsa, log_pgptr);
			LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (LOG_REC_SYSOP_END), &log_lsa, log_pgptr);

			sysop_end = (LOG_REC_SYSOP_END *) (log_pgptr->area + log_lsa.offset);
			if (sysop_end->type == LOG_SYSOP_END_LOGICAL_RUN_POSTPONE)
			  {
			    LSA_COPY (&local_start_postpone_run_lsa, &log_lsa);

			    if (LSA_EQ (start_postpone_lsa, &sysop_end->run_postpone.postpone_lsa))
			      {
				start_postpone_lsa_wasapplied = true;
				isdone = true;
			      }
			  }
		      }
		      break;

		    case LOG_POSTPONE:
		      if (LSA_ISNULL (&next_postpone_lsa) && !LSA_EQ (start_postpone_lsa, &log_lsa))
			{
			  /* remember next postpone_lsa */
			  LSA_COPY (&next_postpone_lsa, &log_lsa);
			}
		      break;

		    case LOG_END_OF_LOG:
		      if (forward_lsa.pageid == NULL_PAGEID && logpb_is_page_in_archive (log_lsa.pageid))
			{
			  forward_lsa.pageid = log_lsa.pageid + 1;
			}
		      break;

		    default:
		      break;
		    }
		}

	      /* 
	       * We can fix the lsa.pageid in the case of log_records without
	       * forward address at this moment.
	       */

	      if (forward_lsa.offset == NULL_OFFSET && forward_lsa.pageid != NULL_PAGEID
		  && forward_lsa.pageid < log_lsa.pageid)
		{
		  forward_lsa.pageid = log_lsa.pageid;
		}
	    }
	}
    }

end:
  if (nxtop_stack != nxtop_array && nxtop_stack != NULL)
    {
      free_and_init (nxtop_stack);
    }

  if (start_postpone_lsa_wasapplied == false)
    {
      LSA_COPY (ret_lsa, start_postpone_lsa);
    }
  else
    {
      LSA_COPY (ret_lsa, &next_postpone_lsa);
    }

  return NO_ERROR;
}

/*
 * log_rv_undoredo_partial_changes_recursive () - Parse log data recursively
 *						  and apply changes.
 *
 * return	 : Error code.
 * thread_p (in) : Thread entry.
 * rcv_buf (in)  : Buffer to process log data.
 * record (in)   : Record being modified.
 * is_undo (in)  : True for undo, false for redo.
 *
 * NOTE: The recursive function is applied for both undo and redo data.
 *	 Changes are logged in the order they are made during runtime.
 *	 Redo will apply all changes in the same order, but undo will have
 *	 to apply them in reversed order.
 */
static int
log_rv_undoredo_partial_changes_recursive (THREAD_ENTRY * thread_p, OR_BUF * rcv_buf, RECDES * record, bool is_undo)
{
  int error_code = NO_ERROR;	/* Error code. */
  int offset_to_data;		/* Offset to data being modified. */
  int old_data_size;		/* Size of old data. */
  int new_data_size;		/* Size of new data. */
  char *new_data = NULL;	/* New data. */

  if (rcv_buf->ptr == rcv_buf->endptr)
    {
      /* Finished. */
      return NO_ERROR;
    }

  /* At least offset_to_data, old_data_size and new_data_size should be stored. */
  if (rcv_buf->ptr + OR_SHORT_SIZE + 2 * OR_BYTE_SIZE > rcv_buf->endptr)
    {
      assert_release (false);
      return or_overflow (rcv_buf);
    }

  /* Get offset_to_data. */
  offset_to_data = (int) or_get_short (rcv_buf, &error_code);
  if (error_code != NO_ERROR)
    {
      assert_release (false);
      return error_code;
    }

  /* Get old_data_size */
  old_data_size = (int) or_get_byte (rcv_buf, &error_code);
  if (error_code != NO_ERROR)
    {
      assert_release (false);
      return error_code;
    }

  /* Get new_data_size */
  new_data_size = (int) or_get_byte (rcv_buf, &error_code);
  if (error_code != NO_ERROR)
    {
      assert_release (false);
      return error_code;
    }

  if (new_data_size > 0)
    {
      /* Get new data. */
      new_data = rcv_buf->ptr;
      error_code = or_advance (rcv_buf, new_data_size);
      if (error_code != NO_ERROR)
	{
	  assert_release (false);
	  return error_code;
	}
    }
  else
    {
      /* No new data. */
      new_data = NULL;
    }

  /* Align buffer to expected alignment. */
  or_align (rcv_buf, INT_ALIGNMENT);

  if (!is_undo)
    {
      /* Changes must be applied in the same order they are logged. Change record and then advance to next changes. */
      RECORD_REPLACE_DATA (record, offset_to_data, old_data_size, new_data_size, new_data);
    }
  error_code = log_rv_undoredo_partial_changes_recursive (thread_p, rcv_buf, record, is_undo);
  if (error_code != NO_ERROR)
    {
      assert_release (false);
      return error_code;
    }
  if (is_undo)
    {
      /* Changes must be made in reversed order. Change record after advancing to next changes. */
      RECORD_REPLACE_DATA (record, offset_to_data, old_data_size, new_data_size, new_data);
    }
  return NO_ERROR;
}

/*
 * log_rv_undoredo_record_partial_changes () - Undoredo record data changes.
 *
 * return		: Error code.
 * thread_p (in)	: Thread entry.
 * rcv_data (in)	: Recovery data pointer.
 * rcv_data_length (in) : Recovery data length.
 * record (in)		: Record being modified.
 *
 * TODO: Extend this to undo and undoredo.
 */
int
log_rv_undoredo_record_partial_changes (THREAD_ENTRY * thread_p, char *rcv_data, int rcv_data_length, RECDES * record,
					bool is_undo)
{
  OR_BUF rcv_buf;		/* Buffer used to process recovery data. */

  /* Assert expected arguments. */
  assert (rcv_data != NULL);
  assert (rcv_data_length > 0);
  assert (record != NULL);

  /* Prepare buffer. */
  OR_BUF_INIT (rcv_buf, rcv_data, rcv_data_length);

  return log_rv_undoredo_partial_changes_recursive (thread_p, &rcv_buf, record, is_undo);
}

/*
 * log_rv_redo_record_modify () - Modify one record of database slotted page.
 *				  The change can be one of:
 *				  1. New record is inserted.
 *				  2. Existing record is removed.
 *				  3. Existing record is entirely updated.
 *				  4. Existing record is partially updated.
 *
 * return	 : Error code.
 * thread_p (in) : Thread entry.
 * rcv (in)	 : Recovery data.
 */
int
log_rv_redo_record_modify (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  return log_rv_record_modify_internal (thread_p, rcv, false);
}

/*
 * log_rv_undo_record_modify () - Modify one record of database slotted page.
 *				  The change can be one of:
 *				  1. New record is inserted.
 *				  2. Existing record is removed.
 *				  3. Existing record is entirely updated.
 *				  4. Existing record is partially updated.
 *
 * return	 : Error code.
 * thread_p (in) : Thread entry.
 * rcv (in)	 : Recovery data.
 */
int
log_rv_undo_record_modify (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  return log_rv_record_modify_internal (thread_p, rcv, true);
}

/*
 * log_rv_redo_record_modify () - Modify one record of database slotted page.
 *				  The change can be one of:
 *				  1. New record is inserted.
 *				  2. Existing record is removed.
 *				  3. Existing record is entirely updated.
 *				  4. Existing record is partially updated.
 *
 * return	 : Error code.
 * thread_p (in) : Thread entry.
 * rcv (in)	 : Recovery data.
 * is_undo (in)  : True if undo recovery, false if redo recovery.
 */
static int
log_rv_record_modify_internal (THREAD_ENTRY * thread_p, LOG_RCV * rcv, bool is_undo)
{
  INT16 flags = rcv->offset & LOG_RV_RECORD_MODIFY_MASK;
  PGSLOTID slotid = rcv->offset & (~LOG_RV_RECORD_MODIFY_MASK);
  RECDES record;
  char data_buffer[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT];
  INT16 rec_type = REC_UNKNOWN;
  char *ptr = NULL;
  int error_code = NO_ERROR;

  if ((!is_undo && LOG_RV_RECORD_IS_INSERT (flags)) || (is_undo && LOG_RV_RECORD_IS_DELETE (flags)))
    {
      /* Insert new record. */
      ptr = (char *) rcv->data;
      /* Get record type. */
      record.type = OR_GET_BYTE (ptr);
      ptr += OR_BYTE_SIZE;
      /* Get record data. */
      record.data = ptr;
      record.length = rcv->length - CAST_BUFLEN (ptr - rcv->data);
      if (spage_insert_at (thread_p, rcv->pgptr, slotid, &record) != SP_SUCCESS)
	{
	  /* Unexpected. */
	  assert_release (false);
	  return ER_FAILED;
	}
      /* Success. */
    }
  else if ((!is_undo && LOG_RV_RECORD_IS_DELETE (flags)) || (is_undo && LOG_RV_RECORD_IS_INSERT (flags)))
    {
      if (spage_delete (thread_p, rcv->pgptr, slotid) != slotid)
	{
	  assert_release (false);
	  return ER_FAILED;
	}
      /* Success. */
    }
  else if (LOG_RV_RECORD_IS_UPDATE_ALL (flags))
    {
      ptr = (char *) rcv->data;
      /* Get record type. */
      record.type = OR_GET_BYTE (ptr);
      ptr += OR_BYTE_SIZE;
      /* Get record data. */
      record.data = ptr;
      record.length = rcv->length - CAST_BUFLEN (ptr - rcv->data);
      if (spage_update (thread_p, rcv->pgptr, slotid, &record) != SP_SUCCESS)
	{
	  assert_release (false);
	  return ER_FAILED;
	}
      /* Success */
    }
  else
    {
      assert (LOG_RV_RECORD_IS_UPDATE_PARTIAL (flags));
      /* Limited changes are done to record and updating it entirely is not the most efficient way of using log-space.
       * Only the change is logged (change location, old data size, new data size, new data). */
      /* Copy existing record. */
      record.data = PTR_ALIGN (data_buffer, MAX_ALIGNMENT);
      record.area_size = DB_PAGESIZE;
      if (spage_get_record (rcv->pgptr, slotid, &record, COPY) != S_SUCCESS)
	{
	  /* Unexpected failure. */
	  assert_release (false);
	  return ER_FAILED;
	}
      /* Make recorded changes. */
      error_code = log_rv_undoredo_record_partial_changes (thread_p, (char *) rcv->data, rcv->length, &record, is_undo);
      if (error_code != NO_ERROR)
	{
	  assert_release (false);
	  return error_code;
	}

      /* Update in page. */
      if (spage_update (thread_p, rcv->pgptr, slotid, &record) != SP_SUCCESS)
	{
	  /* Unexpected. */
	  assert_release (false);
	  return error_code;
	}
      /* Success. */
    }
  /* Page was successfully modified. */
  pgbuf_set_dirty (thread_p, rcv->pgptr, DONT_FREE);
  return NO_ERROR;
}

/*
 * log_rv_pack_redo_record_changes () - Pack recovery data for redo record
 *					change.
 *
 * return	       : Error code.
 * ptr (in)	       : Where recovery data is packed.
 * offset_to_data (in) : Offset to data being modified.
 * old_data_size (in)  : Old data size.
 * new_data_size (in)  : New data size.
 * new_data (in)       : New data.
 */
char *
log_rv_pack_redo_record_changes (char *ptr, int offset_to_data, int old_data_size, int new_data_size, char *new_data)
{
  /* Assert expected arguments. */
  assert (ptr != NULL);
  assert (offset_to_data >= 0 && offset_to_data <= 0x8FFF);
  assert (old_data_size >= 0 && new_data_size >= 0);
  assert (old_data_size <= 255 && new_data_size <= 255);
  assert (new_data_size == 0 || new_data != NULL);

  ptr = PTR_ALIGN (ptr, INT_ALIGNMENT);

  OR_PUT_SHORT (ptr, (short) offset_to_data);
  ptr += OR_SHORT_SIZE;

  OR_PUT_BYTE (ptr, (INT16) old_data_size);
  ptr += OR_BYTE_SIZE;

  OR_PUT_BYTE (ptr, (INT16) new_data_size);
  ptr += OR_BYTE_SIZE;

  if (new_data_size > 0)
    {
      memcpy (ptr, new_data, new_data_size);
      ptr += new_data_size;
    }
  ptr = PTR_ALIGN (ptr, INT_ALIGNMENT);

  return ptr;
}

/*
 * log_rv_pack_redo_record_changes () - Pack recovery data for undo record
 *					change.
 *
 * return	       : Error code.
 * ptr (in)	       : Where recovery data is packed.
 * offset_to_data (in) : Offset to data being modified.
 * old_data_size (in)  : Old data size.
 * new_data_size (in)  : New data size.
 * old_data (in)       : Old data.
 */
char *
log_rv_pack_undo_record_changes (char *ptr, int offset_to_data, int old_data_size, int new_data_size, char *old_data)
{
  /* Assert expected arguments. */
  assert (ptr != NULL);
  assert (offset_to_data >= 0 && offset_to_data <= 0x8FFF);
  assert (old_data_size >= 0 && new_data_size >= 0);
  assert (old_data_size <= 255 && new_data_size <= 255);
  assert (old_data_size == 0 || old_data != NULL);

  ptr = PTR_ALIGN (ptr, INT_ALIGNMENT);

  OR_PUT_SHORT (ptr, (short) offset_to_data);
  ptr += OR_SHORT_SIZE;

  OR_PUT_BYTE (ptr, (INT16) new_data_size);
  ptr += OR_BYTE_SIZE;

  OR_PUT_BYTE (ptr, (INT16) old_data_size);
  ptr += OR_BYTE_SIZE;

  if (old_data_size > 0)
    {
      memcpy (ptr, old_data, old_data_size);
      ptr += old_data_size;
    }
  ptr = PTR_ALIGN (ptr, INT_ALIGNMENT);

  return ptr;
}

/*
 * log_rv_redo_fix_page () - fix page for recovery
 *
 * return        : fixed page or NULL
 * thread_p (in) : thread entry
 * vpid_rcv (in) : page identifier
 * rcvindex (in) : recovery index of log record to redo
 */
STATIC_INLINE PAGE_PTR
log_rv_redo_fix_page (THREAD_ENTRY * thread_p, const VPID * vpid_rcv, LOG_RCVINDEX rcvindex)
{
  PAGE_PTR page = NULL;

  assert (vpid_rcv != NULL && !VPID_ISNULL (vpid_rcv));

  /* how it works:
   * since we are during recovery, we don't know the current state of page. it may be unreserved (its file is destroyed)
   * or it may not be allocated. these are expected cases and we don't want to raise errors if it happens.
   * some redo records are used to initialize a page for the first time (also setting its page type which is necessary
   * to consider a page allocated). even first attempt to fix page fails, but the page's sector is reserved, we will
   * fix the page as NEW_PAGE and apply its initialization redo log record.
   */

  /* let's first try to fix page if it is not deallocated. */
  if (pgbuf_fix_if_not_deallocated (thread_p, vpid_rcv, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH, &page)
      != NO_ERROR)
    {
      ASSERT_ERROR ();
      return NULL;
    }
  if (page == NULL && RCV_IS_NEW_PAGE_INIT (rcvindex))
    {
      /* page is deallocated. however, this is redo of a new page initialization, we still have to apply it.
       * page must still be reserved, otherwise it means its file was completely destroyed. */
      DISK_ISVALID isvalid;
      isvalid = disk_is_page_sector_reserved (thread_p, vpid_rcv->volid, vpid_rcv->pageid);
      if (isvalid == DISK_ERROR)
	{
	  ASSERT_ERROR ();
	  return NULL;
	}
      else if (isvalid == DISK_INVALID)
	{
	  /* not reserved */
	  return NULL;
	}
      assert (isvalid == DISK_VALID);
      page = pgbuf_fix (thread_p, vpid_rcv, NEW_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
      if (page == NULL)
	{
	  ASSERT_ERROR ();
	  return NULL;
	}
    }
  return page;
}
