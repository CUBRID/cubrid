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


static void
log_rv_undo_record (THREAD_ENTRY * thread_p, LOG_LSA * log_lsa,
		    LOG_PAGE * log_page_p, LOG_RCVINDEX rcvindex,
		    const VPID * rcv_vpid, LOG_RCV * rcv,
		    const LOG_LSA * rcv_lsa_ptr, LOG_TDES * tdes,
		    LOG_ZIP * undo_unzip_ptr);
static void
log_rv_redo_record (THREAD_ENTRY * thread_p, LOG_LSA * log_lsa,
		    LOG_PAGE * log_page_p,
		    int (*redofun) (THREAD_ENTRY * thread_p, LOG_RCV *),
		    LOG_RCV * rcv, LOG_LSA * rcv_lsa_ptr,
		    bool ignore_redofunc, int undo_length, char *undo_data,
		    LOG_ZIP * redo_unzip_ptr);
static bool log_rv_find_checkpoint (THREAD_ENTRY * thread_p, VOLID volid,
				    LOG_LSA * rcv_lsa);
static bool log_rv_get_unzip_log_data (THREAD_ENTRY * thread_p, int length,
				       LOG_LSA * log_lsa,
				       LOG_PAGE * log_page_p,
				       LOG_ZIP * undo_unzip_ptr);
static int log_rv_analysis_client_name (THREAD_ENTRY * thread_p, int tran_id,
					LOG_LSA * log_lsa,
					LOG_PAGE * log_page_p);
static int log_rv_analysis_undo_redo (THREAD_ENTRY * thread_p, int tran_id,
				      LOG_LSA * log_lsa);
static int log_rv_analysis_dummy_head_postpone (THREAD_ENTRY * thread_p,
						int tran_id,
						LOG_LSA * log_lsa);
static int log_rv_analysis_postpone (THREAD_ENTRY * thread_p, int tran_id,
				     LOG_LSA * log_lsa);
static int log_rv_analysis_run_postpone (THREAD_ENTRY * thread_p, int tran_id,
					 LOG_LSA * log_lsa,
					 LOG_PAGE * log_page_p,
					 LOG_LSA * check_point);
static int log_rv_analysis_compensate (THREAD_ENTRY * thread_p, int tran_id,
				       LOG_LSA * log_lsa,
				       LOG_PAGE * log_page_p);
static int log_rv_analysis_lcompensate (THREAD_ENTRY * thread_p, int tran_id,
					LOG_LSA * log_lsa,
					LOG_PAGE * log_page_p);
static int log_rv_analysis_client_user_undo_data (THREAD_ENTRY * thread_p,
						  int tran_id,
						  LOG_LSA * log_lsa);
static int log_rv_analysis_client_user_postpone_data (THREAD_ENTRY * thread_p,
						      int tran_id,
						      LOG_LSA * log_lsa);
static int log_rv_analysis_will_commit (THREAD_ENTRY * thread_p, int tran_id,
					LOG_LSA * log_lsa);
static int log_rv_analysis_commit_with_postpone (THREAD_ENTRY * thread_p,
						 int tran_id,
						 LOG_LSA * log_lsa,
						 LOG_PAGE * log_page_p);
static int log_rv_analysis_commit_with_loose_ends (THREAD_ENTRY * thread_p,
						   int tran_id,
						   LOG_LSA * log_lsa,
						   LOG_PAGE * log_page_p);
static int log_rv_analysis_abort_with_loose_ends (THREAD_ENTRY * thread_p,
						  int tran_id,
						  LOG_LSA * log_lsa,
						  LOG_PAGE * log_page_p);
static int log_rv_analysis_commit_topope_with_postpone (THREAD_ENTRY *
							thread_p, int tran_id,
							LOG_LSA * log_lsa,
							LOG_PAGE *
							log_page_p);
static int log_rv_analysis_commit_topope_with_loose_ends (THREAD_ENTRY *
							  thread_p,
							  int tran_id,
							  LOG_LSA * log_lsa,
							  LOG_PAGE *
							  log_page_p);
static int log_rv_analysis_abort_topope_with_loose_ends (THREAD_ENTRY *
							 thread_p,
							 int tran_id,
							 LOG_LSA * log_lsa,
							 LOG_PAGE *
							 log_page_p);
static int log_rv_analysis_run_next_client_undo (THREAD_ENTRY * thread_p,
						 int tran_id,
						 LOG_LSA * log_lsa,
						 LOG_PAGE * log_page_p,
						 LOG_LSA * check_point);
static int log_rv_analysis_run_next_client_postpone (THREAD_ENTRY * thread_p,
						     int tran_id,
						     LOG_LSA * log_lsa,
						     LOG_PAGE * log_page_p,
						     LOG_LSA * check_point);
static int log_rv_analysis_complete (THREAD_ENTRY * thread_p, int tran_id,
				     LOG_LSA * log_lsa, LOG_PAGE * log_page_p,
				     LOG_LSA * end_redo_lsa, LOG_LSA * lsa,
				     bool is_media_crash, time_t * stop_at,
				     bool * did_incom_recovery);
static int log_rv_analysis_complte_topope (THREAD_ENTRY * thread_p,
					   int tran_id, LOG_LSA * log_lsa);
static int log_rv_analysis_start_checkpoint (LOG_LSA * log_lsa,
					     LOG_LSA * start_lsa,
					     bool * may_use_checkpoint);
static int log_rv_analysis_end_checkpoint (THREAD_ENTRY * thread_p,
					   LOG_LSA * log_lsa,
					   LOG_PAGE * log_page_p,
					   LOG_LSA * check_point,
					   LOG_LSA * start_redo_lsa,
					   bool * may_use_checkpoint,
					   bool *
					   may_need_synch_checkpoint_2pc);
static int log_rv_analysis_save_point (THREAD_ENTRY * thread_p, int tran_id,
				       LOG_LSA * log_lsa);
static int log_rv_analysis_2pc_prepare (THREAD_ENTRY * thread_p, int tran_id,
					LOG_LSA * log_lsa);
static int log_rv_analysis_2pc_start (THREAD_ENTRY * thread_p, int tran_id,
				      LOG_LSA * log_lsa);
static int log_rv_analysis_2pc_commit_decision (THREAD_ENTRY * thread_p,
						int tran_id,
						LOG_LSA * log_lsa);
static int log_rv_analysis_2pc_abort_decision (THREAD_ENTRY * thread_p,
					       int tran_id,
					       LOG_LSA * log_lsa);
static int log_rv_analysis_2pc_commit_inform_particps (THREAD_ENTRY *
						       thread_p, int tran_id,
						       LOG_LSA * log_lsa);
static int log_rv_analysis_2pc_abort_inform_particps (THREAD_ENTRY * thread_p,
						      int tran_id,
						      LOG_LSA * log_lsa);
static int log_rv_analysis_2pc_recv_ack (THREAD_ENTRY * thread_p, int tran_id,
					 LOG_LSA * log_lsa);
static int log_rv_analysis_log_end (int tran_id, LOG_LSA * log_lsa);
static void
log_rv_analysis_record (THREAD_ENTRY * thread_p, LOG_RECTYPE log_type,
			int tran_id, LOG_LSA * log_lsa, LOG_PAGE * log_page_p,
			LOG_LSA * check_point, LOG_LSA * lsa,
			LOG_LSA * start_lsa, LOG_LSA * start_redo_lsa,
			LOG_LSA * end_redo_lsa, bool is_media_crash,
			time_t * stop_at, bool * did_incom_recovery,
			bool * may_use_checkpoint,
			bool * may_need_synch_checkpoint_2pc);
static void log_recovery_analysis (THREAD_ENTRY * thread_p,
				   LOG_LSA * start_lsa,
				   LOG_LSA * start_redolsa,
				   LOG_LSA * end_redo_lsa, int ismedia_crash,
				   time_t * stopat,
				   bool * did_incom_recovery,
				   INT64 * num_redo_log_records);
static void log_recovery_redo (THREAD_ENTRY * thread_p,
			       const LOG_LSA * start_redolsa,
			       const LOG_LSA * end_redo_lsa, time_t * stopat);
static void log_recovery_finish_all_postpone (THREAD_ENTRY * thread_p);
static void log_recovery_undo (THREAD_ENTRY * thread_p);
static void
log_recovery_notpartof_archives (THREAD_ENTRY * thread_p, int start_arv_num,
				 const char *info_reason);
static bool log_unformat_ahead_volumes (THREAD_ENTRY * thread_p, VOLID volid,
					VOLID * start_volid);
static void log_recovery_notpartof_volumes (THREAD_ENTRY * thread_p);
static void log_recovery_resetlog (THREAD_ENTRY * thread_p,
				   LOG_LSA * new_append_lsa,
				   bool is_new_append_page,
				   LOG_LSA * last_lsa);
static int log_recovery_find_first_postpone (THREAD_ENTRY * thread_p,
					     LOG_LSA * ret_lsa,
					     LOG_LSA *
					     start_postpone_lsa,
					     LOG_TDES * tdes);

static void log_recovery_vacuum_data_buffer (THREAD_ENTRY * thread_p,
					     LOG_LSA * mvcc_op_lsa,
					     MVCCID mvccid);
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
log_rv_undo_record (THREAD_ENTRY * thread_p, LOG_LSA * log_lsa,
		    LOG_PAGE * log_page_p, LOG_RCVINDEX rcvindex,
		    const VPID * rcv_vpid, LOG_RCV * rcv,
		    const LOG_LSA * rcv_undo_lsa, LOG_TDES * tdes,
		    LOG_ZIP * undo_unzip_ptr)
{
  char *area = NULL;
  LOG_LSA logical_undo_nxlsa;
  TRAN_STATE save_state;	/* The current state of the transaction. Must be
				 * returned to this state
				 */
  bool is_zip = false;
  int error_code = NO_ERROR;
  LOG_SET_CURRENT_TRAN_INDEX (thread_p, tdes->tran_index);

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

  if (RCV_IS_LOGICAL_LOG (rcv_vpid, rcvindex)
      || (disk_isvalid_page (thread_p, rcv_vpid->volid,
			     rcv_vpid->pageid) != DISK_VALID))
    {
      rcv->pgptr = NULL;
    }
  else
    {
      rcv->pgptr = pgbuf_fix (thread_p, rcv_vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
			      PGBUF_UNCONDITIONAL_LATCH);
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
	  logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
			     "log_rv_undo_record");
	  if (rcv->pgptr != NULL)
	    {
	      pgbuf_unfix (thread_p, rcv->pgptr);
	    }
	  return;
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
	  logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
			     "log_rv_undo_record");
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

      if (rcvindex == RVVAC_DROPPED_FILE_ADD)
	{
	  error_code = vacuum_notify_dropped_file (thread_p, rcv, NULL);
	}
      else if (!RCV_IS_LOGICAL_LOG (rcv_vpid, rcvindex))
	{
	  log_append_compensate (thread_p, rcvindex, rcv_vpid,
				 rcv->offset, rcv->pgptr, rcv->length,
				 rcv->data, tdes);

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
				 "log_rvredo_rec: Error applying redo record "
				 "at log_lsa=(%lld, %d), "
				 "rcv = {mvccid=%lld, vpid=(%d, %d), "
				 "offset = %d, data_length = %d}",
				 (long long int) rcv_undo_lsa->pageid,
				 (int) rcv_undo_lsa->offset,
				 (long long int) rcv->mvcc_id,
				 (int) vpid.pageid, (int) vpid.volid,
				 (int) rcv->offset, (int) rcv->length);
	    }
	}
      else
	{
	  /*
	   * Logical logging. The undo function is responsible for logging the
	   * needed undo and redo records to make the logical undo operation
	   * atomic.
	   * The recovery manager sets a dummy compensating record, to fix the
	   * undo_nx_lsa record at crash recovery time.
	   */
	  LSA_COPY (&logical_undo_nxlsa, &tdes->undo_nxlsa);
	  save_state = tdes->state;

	  /*
	   * A system operation is needed since the postpone operations of an
	   * undo log must be done at the end of the logical undo. Without this
	   * if there is a crash, we will be in trouble since we will not be able
	   * to undo a postpone operation.
	   */
	  if (log_start_system_op (thread_p) == NULL)
	    {
	      logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
				 "log_rv_undo_record");
	      if (area != NULL)
		{
		  free_and_init (area);
		}
	      if (rcv->pgptr != NULL)
		{
		  pgbuf_unfix (thread_p, rcv->pgptr);
		}
	      return;
	    }

#if defined(CUBRID_DEBUG)
	  {
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

	    if (LSA_EQ (&check_tail_lsa, &tdes->tail_lsa)
		&& !LSA_EQ (rcv_undo_lsa, &tdes->tail_lsa)
		&& rcvindex != RVFL_CREATE_TMPFILE)
	      {
		er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			ER_LOG_MISSING_COMPENSATING_RECORD, 1,
			rv_rcvindex_string (rcvindex));
	      }
	  }
#else /* CUBRID_DEBUG */
	  (void) (*RV_fun[rcvindex].undofun) (thread_p, rcv);
#endif /* CUBRID_DEBUG */
	  log_end_system_op (thread_p, LOG_RESULT_TOPOP_COMMIT);
	  tdes->state = save_state;
	  /*
	   * Now add the dummy logical compensating record. This mark the end of
	   * the logical operation.
	   */
	  log_append_logical_compensate (thread_p, rcvindex, tdes,
					 &logical_undo_nxlsa);
	}
    }
  else
    {
      log_append_compensate (thread_p, rcvindex, rcv_vpid,
			     rcv->offset, NULL, rcv->length, rcv->data, tdes);
      /*
       * Unable to fetch page of volume... May need media recovery on such
       * page
       */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_MAYNEED_MEDIA_RECOVERY,
	      1, fileio_get_volume_label (rcv_vpid->volid, PEEK));
    }

  if (area != NULL)
    {
      free_and_init (area);
    }

  if (rcv->pgptr != NULL)
    {
      pgbuf_unfix (thread_p, rcv->pgptr);
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
log_rv_redo_record (THREAD_ENTRY * thread_p, LOG_LSA * log_lsa,
		    LOG_PAGE * log_page_p,
		    int (*redofun) (THREAD_ENTRY * thread_p, LOG_RCV *),
		    LOG_RCV * rcv, LOG_LSA * rcv_lsa_ptr,
		    bool ignore_redofunc, int undo_length, char *undo_data,
		    LOG_ZIP * redo_unzip_ptr)
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
	      (void) log_diff (undo_length, undo_data,
			       redo_unzip_ptr->data_length,
			       redo_unzip_ptr->log_data);
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
      if (!ignore_redofunc)
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
				 "log_rvredo_rec: Error applying redo record "
				 "at log_lsa=(%lld, %d), "
				 "rcv = {mvccid=%lld, vpid=(%d, %d), "
				 "offset = %d, data_length = %d}",
				 (long long int) rcv_lsa_ptr->pageid,
				 (int) rcv_lsa_ptr->offset,
				 (long long int) rcv->mvcc_id,
				 (int) vpid.pageid, (int) vpid.volid,
				 (int) rcv->offset, (int) rcv->length);
	    }
	}
    }
  else
    {
      er_log_debug (ARG_FILE_LINE, "log_rvredo_rec: WARNING.. There is not a"
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
log_rv_find_checkpoint (THREAD_ENTRY * thread_p, VOLID volid,
			LOG_LSA * rcv_lsa)
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
log_rv_get_unzip_log_data (THREAD_ENTRY * thread_p, int length,
			   LOG_LSA * log_lsa, LOG_PAGE * log_page_p,
			   LOG_ZIP * undo_unzip_ptr)
{
  char *ptr;			/* Pointer to data to be printed            */
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
  int rcv_tran_index;		/* Saved transaction index                 */
  LOG_RECORD_HEADER *eof;	/* End of the log record                   */
  LOG_LSA rcv_lsa;		/* Where to start the recovery             */
  LOG_LSA start_redolsa;	/* Where to start redo phase               */
  LOG_LSA end_redo_lsa;		/* Where to stop the redo phase            */
  bool did_incom_recovery;
  int tran_index;
  INT64 num_redo_log_records;

  assert (LOG_CS_OWN_WRITE_MODE (thread_p));
  mvcc_Enabled = prm_get_bool_value (PRM_ID_MVCC_ENABLED);

  /* Save the transaction index and find the transaction descriptor */

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  rcv_tdes = LOG_FIND_TDES (tran_index);
  if (rcv_tdes == NULL)
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_LOG_UNKNOWN_TRANINDEX, 1, tran_index);
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_recovery");
      return;
    }

  rcv_tran_index = tran_index;
  rcv_tdes->state = TRAN_RECOVERY;

  if (LOG_HAS_LOGGING_BEEN_IGNORED ())
    {
      /*
       * Your database is corrupted since it crashed when logging was ignored
       */
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_LOG_CORRUPTED_DB_DUE_CRASH_NOLOGGING, 0);
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_recovery");
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
      (void) fileio_map_mounted (thread_p,
				 (bool (*)(THREAD_ENTRY *, VOLID, void *))
				 log_rv_find_checkpoint, &rcv_lsa);
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

  /*
   * First,  ANALYSIS the log to find the state of the transactions
   * Second, REDO going forward
   * Last,   UNDO going backwards
   */

  log_Gl.rcv_phase = LOG_RECOVERY_ANALYSIS_PHASE;
  log_recovery_analysis (thread_p, &rcv_lsa, &start_redolsa, &end_redo_lsa,
			 ismedia_crash, stopat, &did_incom_recovery,
			 &num_redo_log_records);

  er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE,
	  ER_LOG_RECOVERY_STARTED, 3, num_redo_log_records,
	  start_redolsa.pageid, end_redo_lsa.pageid);

  LSA_COPY (&log_Gl.chkpt_redo_lsa, &start_redolsa);

  LOG_SET_CURRENT_TRAN_INDEX (thread_p, rcv_tran_index);
  if (logpb_fetch_start_append_page (thread_p) == NULL)
    {
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_recovery");
      er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE,
	      ER_LOG_RECOVERY_FINISHED, 0);
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
  if (log_Gl.trantable.num_coord_loose_end_indices > 0
      || log_Gl.trantable.num_prepared_loose_end_indices > 0)
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

  er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_LOG_RECOVERY_FINISHED,
	  0);
}

/*
 * log_rv_analysis_client_name -
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
log_rv_analysis_client_name (THREAD_ENTRY * thread_p, int tran_id,
			     LOG_LSA * log_lsa, LOG_PAGE * log_page_p)
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
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
			 "log_rv_analysis_client_name");
      return ER_FAILED;
    }

  /* New tail and next to undo */
  LSA_COPY (&tdes->tail_lsa, log_lsa);
  LSA_COPY (&tdes->undo_nxlsa, &tdes->tail_lsa);

  /*
   * Need to assign the name of the client associated with the
   * transaction
   */
  LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_RECORD_HEADER), log_lsa,
		      log_page_p);
  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, LOG_USERNAME_MAX, log_lsa,
				    log_page_p);
  logtb_set_client_ids_all (&tdes->client, 0, NULL,
			    (char *) log_page_p->area + log_lsa->offset, NULL,
			    NULL, NULL, -1);

  return NO_ERROR;
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
log_rv_analysis_undo_redo (THREAD_ENTRY * thread_p, int tran_id,
			   LOG_LSA * log_lsa)
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
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
			 "log_rv_analysis_undo_redo");
      return ER_FAILED;
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
log_rv_analysis_dummy_head_postpone (THREAD_ENTRY * thread_p, int tran_id,
				     LOG_LSA * log_lsa)
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
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
			 "log_rv_analysis_dummy_head_postpone");
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
log_rv_analysis_postpone (THREAD_ENTRY * thread_p, int tran_id,
			  LOG_LSA * log_lsa)
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
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
			 "log_rv_analysis_postpone");
      return ER_FAILED;
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
log_rv_analysis_run_postpone (THREAD_ENTRY * thread_p, int tran_id,
			      LOG_LSA * log_lsa, LOG_PAGE * log_page_p,
			      LOG_LSA * check_point)
{
  LOG_TDES *tdes;
  struct log_run_postpone *run_posp;

  tdes = logtb_rv_find_allocate_tran_index (thread_p, tran_id, log_lsa);
  if (tdes == NULL)
    {
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
			 "log_rv_analysis_run_postpone");
      return ER_FAILED;
    }

  if (tdes->state != TRAN_UNACTIVE_WILL_COMMIT
      && tdes->state != TRAN_UNACTIVE_COMMITTED_WITH_POSTPONE
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
			"log_recovery_analysis: SYSTEM ERROR\n"
			" Incorrect state = %s\n at log_rec at %lld|%d\n"
			" for transaction = %d (index %d).\n"
			" State should have been either of\n"
			" %s\n %s\n %s\n", log_state_string (tdes->state),
			(long long int) log_lsa->pageid, log_lsa->offset,
			tdes->trid, tdes->tran_index,
			log_state_string (TRAN_UNACTIVE_WILL_COMMIT),
			log_state_string
			(TRAN_UNACTIVE_COMMITTED_WITH_POSTPONE),
			log_state_string
			(TRAN_UNACTIVE_TOPOPE_COMMITTED_WITH_POSTPONE));
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
  LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_RECORD_HEADER), log_lsa,
		      log_page_p);
  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p,
				    sizeof (struct log_run_postpone), log_lsa,
				    log_page_p);

  run_posp = (struct log_run_postpone *) ((char *) log_page_p->area
					  + log_lsa->offset);

  if (tdes->state == TRAN_UNACTIVE_TOPOPE_COMMITTED_WITH_POSTPONE)
    {
      /* Reset start of postpone transaction for the top action */
      LSA_COPY (&tdes->topops.stack[tdes->topops.last].posp_lsa,
		&run_posp->ref_lsa);
    }
  else
    {
      assert (tdes->state == TRAN_UNACTIVE_WILL_COMMIT
	      || tdes->state == TRAN_UNACTIVE_COMMITTED_WITH_POSTPONE);

      /* Reset start of postpone transaction */
      LSA_COPY (&tdes->posp_nxlsa, &run_posp->ref_lsa);
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
log_rv_analysis_compensate (THREAD_ENTRY * thread_p, int tran_id,
			    LOG_LSA * log_lsa, LOG_PAGE * log_page_p)
{
  LOG_TDES *tdes;
  struct log_compensate *compensate;

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
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
			 "log_rv_analysis_compensate");
      return ER_FAILED;
    }

  /*
   * Need to read the compensating record to set the next undo address
   */

  /* Read the DATA HEADER */
  LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_RECORD_HEADER), log_lsa,
		      log_page_p);
  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (struct log_compensate),
				    log_lsa, log_page_p);

  compensate = (struct log_compensate *) ((char *) log_page_p->area
					  + log_lsa->offset);
  LSA_COPY (&tdes->undo_nxlsa, &compensate->undo_nxlsa);

  return NO_ERROR;
}

/*
 * log_rv_analysis_lcompensate -
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
log_rv_analysis_lcompensate (THREAD_ENTRY * thread_p, int tran_id,
			     LOG_LSA * log_lsa, LOG_PAGE * log_page_p)
{
  LOG_TDES *tdes;
  struct log_logical_compensate *logical_comp;

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
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
			 "log_rv_analysis_lcompensate");
      return ER_FAILED;
    }

  /*
   * Need to read the compensating record to set the next undo address
   */

  /* Read the DATA HEADER */
  LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_RECORD_HEADER), log_lsa,
		      log_page_p);
  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p,
				    sizeof (struct log_logical_compensate),
				    log_lsa, log_page_p);

  logical_comp = ((struct log_logical_compensate *)
		  ((char *) log_page_p->area + log_lsa->offset));
  LSA_COPY (&tdes->undo_nxlsa, &logical_comp->undo_nxlsa);

  return NO_ERROR;
}

/*
 * log_rv_analysis_client_user_undo_data -
 *
 * return: error code
 *
 *   tran_id(in):
 *   lsa(in/out):
 *
 * Note:
 */
static int
log_rv_analysis_client_user_undo_data (THREAD_ENTRY * thread_p, int tran_id,
				       LOG_LSA * log_lsa)
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
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
			 "log_rv_analysis_client_user_undo_data");
      return ER_FAILED;
    }

  /* New tail and next to undo */
  LSA_COPY (&tdes->tail_lsa, log_lsa);
  LSA_COPY (&tdes->undo_nxlsa, &tdes->tail_lsa);

  /* First client undo ? */
  if (LSA_ISNULL (&tdes->client_undo_lsa))
    {
      LSA_COPY (&tdes->client_undo_lsa, &tdes->tail_lsa);
    }

  return NO_ERROR;
}

/*
 * log_rv_analysis_client_user_postpone_data -
 *
 * return: error code
 *
 *   tran_id(in):
 *   lsa(in/out):
 *
 * Note:
 */
static int
log_rv_analysis_client_user_postpone_data (THREAD_ENTRY * thread_p,
					   int tran_id, LOG_LSA * log_lsa)
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
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
			 "log_rv_analysis_client_user_postpone_data");
      return ER_FAILED;
    }

  /* New tail and next to undo */
  LSA_COPY (&tdes->tail_lsa, log_lsa);
  LSA_COPY (&tdes->undo_nxlsa, &tdes->tail_lsa);

  /* First client postpone ? */
  if (LSA_ISNULL (&tdes->client_posp_lsa))
    {
      LSA_COPY (&tdes->client_posp_lsa, &tdes->tail_lsa);
    }

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
log_rv_analysis_will_commit (THREAD_ENTRY * thread_p, int tran_id,
			     LOG_LSA * log_lsa)
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
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
			 "log_rv_analysis_will_commit");
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
log_rv_analysis_commit_with_postpone (THREAD_ENTRY * thread_p, int tran_id,
				      LOG_LSA * log_lsa,
				      LOG_PAGE * log_page_p)
{
  LOG_TDES *tdes;
  struct log_start_postpone *start_posp;

  /*
   * If this is the first time, the transaction is seen. Assign a new
   * index to describe it. The transaction was in the process of
   * getting committed at this point.
   */
  tdes = logtb_rv_find_allocate_tran_index (thread_p, tran_id, log_lsa);
  if (tdes == NULL)
    {
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
			 "log_rv_analysis_commit_with_postpone");
      return ER_FAILED;
    }

  tdes->state = TRAN_UNACTIVE_COMMITTED_WITH_POSTPONE;

  /* Nothing to undo */
  LSA_SET_NULL (&tdes->undo_nxlsa);
  LSA_COPY (&tdes->tail_lsa, log_lsa);

  /*
   * Need to read the start postpone record to set the postpone address
   * of the transaction
   */
  LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_RECORD_HEADER), log_lsa,
		      log_page_p);
  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p,
				    sizeof (struct log_start_postpone),
				    log_lsa, log_page_p);

  start_posp =
    (struct log_start_postpone *) ((char *) log_page_p->area +
				   log_lsa->offset);
  LSA_COPY (&tdes->posp_nxlsa, &start_posp->posp_lsa);

  return NO_ERROR;
}

/*
 * log_rv_analysis_commit_with_loose_ends -
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
log_rv_analysis_commit_with_loose_ends (THREAD_ENTRY * thread_p, int tran_id,
					LOG_LSA * log_lsa,
					LOG_PAGE * log_page_p)
{
  LOG_TDES *tdes;
  struct log_start_client *start_client;

  /*
   * If this is the first time, the transaction is seen. Assign a new
   * index to describe it. The transaction was in the process of
   * getting committed at this point.
   */
  tdes = logtb_rv_find_allocate_tran_index (thread_p, tran_id, log_lsa);
  if (tdes == NULL)
    {
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
			 "log_rv_analysis_commit_with_loose_ends");
      return ER_FAILED;
    }

  tdes->state = TRAN_UNACTIVE_COMMITTED_WITH_CLIENT_USER_LOOSE_ENDS;

  /* Nothing to undo at server and client */
  LSA_SET_NULL (&tdes->undo_nxlsa);
  LSA_SET_NULL (&tdes->client_undo_lsa);

  LSA_COPY (&tdes->tail_lsa, log_lsa);

  /*
   * Need to read the start postpone record to set the postpone address
   * of the transaction
   */
  LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_RECORD_HEADER), log_lsa,
		      log_page_p);
  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p,
				    sizeof (struct log_start_client), log_lsa,
				    log_page_p);
  start_client =
    (struct log_start_client *) ((char *) log_page_p->area + log_lsa->offset);
  LSA_COPY (&tdes->client_posp_lsa, &start_client->lsa);

  return NO_ERROR;
}

/*
 * log_rv_analysis_abort_with_loose_ends -
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
log_rv_analysis_abort_with_loose_ends (THREAD_ENTRY * thread_p, int tran_id,
				       LOG_LSA * log_lsa,
				       LOG_PAGE * log_page_p)
{
  LOG_TDES *tdes;
  struct log_start_client *start_client;

  /*
   * If this is the first time, the transaction is seen. Assign a new
   * index to describe it. The transaction was in the process of
   * abort at this time.. some client loose ends has not been aborted
   */
  tdes = logtb_rv_find_allocate_tran_index (thread_p, tran_id, log_lsa);
  if (tdes == NULL)
    {
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
			 "log_rv_analysis_abort_with_loose_ends");
      return ER_FAILED;
    }

  tdes->state = TRAN_UNACTIVE_ABORTED_WITH_CLIENT_USER_LOOSE_ENDS;

  /* Nothing to undo at server */
  LSA_SET_NULL (&tdes->undo_nxlsa);

  /* Nothing to redo related to pospone actions at client and server */
  LSA_SET_NULL (&tdes->client_posp_lsa);

  LSA_COPY (&tdes->tail_lsa, log_lsa);

  /*
   * Need to read the start postpone record to set the postpone address
   * of the transaction
   */
  LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_RECORD_HEADER), log_lsa,
		      log_page_p);
  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p,
				    sizeof (struct log_start_client), log_lsa,
				    log_page_p);
  start_client =
    (struct log_start_client *) ((char *) log_page_p->area + log_lsa->offset);
  LSA_COPY (&tdes->client_undo_lsa, &start_client->lsa);

  return NO_ERROR;
}

/*
 * log_rv_analysis_commit_topope_with_postpone -
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
log_rv_analysis_commit_topope_with_postpone (THREAD_ENTRY * thread_p,
					     int tran_id, LOG_LSA * log_lsa,
					     LOG_PAGE * log_page_p)
{
  LOG_TDES *tdes;
  struct log_topope_start_postpone *top_start_posp;

  /*
   * If this is the first time, the transaction is seen. Assign a new
   * index to describe it. A top system operation was in the process
   * of getting committed at this point.
   */
  tdes = logtb_rv_find_allocate_tran_index (thread_p, tran_id, log_lsa);
  if (tdes == NULL)
    {
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
			 "log_rv_analysis_commit_topope_with_postpone");
      return ER_FAILED;
    }

  tdes->state = TRAN_UNACTIVE_TOPOPE_COMMITTED_WITH_POSTPONE;

  LSA_COPY (&tdes->tail_lsa, log_lsa);
  LSA_COPY (&tdes->undo_nxlsa, &tdes->tail_lsa);

  /*
   * Need to read the start postpone record to set the start address
   * of top system operation
   */
  LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_RECORD_HEADER), log_lsa,
		      log_page_p);
  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p,
				    sizeof (struct log_topope_start_postpone),
				    log_lsa, log_page_p);
  top_start_posp =
    ((struct log_topope_start_postpone *) ((char *) log_page_p->area +
					   log_lsa->offset));

  if (tdes->topops.max == 0 || (tdes->topops.last + 1) >= tdes->topops.max)
    {
      if (logtb_realloc_topops_stack (tdes, 1) == NULL)
	{
	  /* Out of memory */
	  logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
			     "log_recovery_analysis");
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

  LSA_COPY (&tdes->topops.stack[tdes->topops.last].lastparent_lsa,
	    &top_start_posp->lastparent_lsa);
  LSA_COPY (&tdes->topops.stack[tdes->topops.last].posp_lsa,
	    &top_start_posp->posp_lsa);
  LSA_SET_NULL (&tdes->topops.stack[tdes->topops.last].client_undo_lsa);
  LSA_SET_NULL (&tdes->topops.stack[tdes->topops.last].client_posp_lsa);

  return NO_ERROR;
}

/*
 * log_rv_analysis_commit_topope_with_loose_ends -
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
log_rv_analysis_commit_topope_with_loose_ends (THREAD_ENTRY * thread_p,
					       int tran_id, LOG_LSA * log_lsa,
					       LOG_PAGE * log_page_p)
{
  LOG_TDES *tdes;
  struct log_topope_start_client *top_start_client;

  /*
   * If this is the first time, the transaction is seen. Assign a new
   * index to describe it. A top system operation was in the process
   * of getting committed at this point.
   */
  tdes = logtb_rv_find_allocate_tran_index (thread_p, tran_id, log_lsa);
  if (tdes == NULL)
    {
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
			 "log_rv_analysis_commit_topope_with_loose_ends");
      return ER_FAILED;
    }

  tdes->state = TRAN_UNACTIVE_XTOPOPE_COMMITTED_WITH_CLIENT_USER_LOOSE_ENDS;

  LSA_COPY (&tdes->tail_lsa, log_lsa);
  LSA_COPY (&tdes->undo_nxlsa, &tdes->tail_lsa);

  /*
   * Need to read the start client record to set the start address
   * of top system operation
   */
  LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_RECORD_HEADER), log_lsa,
		      log_page_p);
  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p,
				    sizeof (struct log_topope_start_client),
				    log_lsa, log_page_p);
  top_start_client =
    ((struct log_topope_start_client *) ((char *) log_page_p->area +
					 log_lsa->offset));

  if (tdes->topops.max == 0 || (tdes->topops.last + 1) >= tdes->topops.max)
    {
      if (logtb_realloc_topops_stack (tdes, 1) == NULL)
	{
	  /* Out of memory */
	  logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
			     "log_rv_analysis_commit_topope_with_loose_ends");
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

  LSA_COPY (&tdes->topops.stack[tdes->topops.last].lastparent_lsa,
	    &top_start_client->lastparent_lsa);
  LSA_COPY (&tdes->topops.stack[tdes->topops.last].client_posp_lsa,
	    &top_start_client->lsa);

  return NO_ERROR;
}

/*
 * log_rv_analysis_abort_topope_with_loose_ends -
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
log_rv_analysis_abort_topope_with_loose_ends (THREAD_ENTRY * thread_p,
					      int tran_id, LOG_LSA * log_lsa,
					      LOG_PAGE * log_page_p)
{
  LOG_TDES *tdes;
  struct log_topope_start_client *top_start_client;

  /*
   * If this is the first time, the transaction is seen. Assign a new
   * index to describe it. A top system operation was in the process
   * of getting committed at this point.
   */
  tdes = logtb_rv_find_allocate_tran_index (thread_p, tran_id, log_lsa);
  if (tdes == NULL)
    {
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
			 "log_rv_analysis_abort_topope_with_loose_ends");
      return ER_FAILED;
    }

  tdes->state = TRAN_UNACTIVE_TOPOPE_ABORTED_WITH_CLIENT_USER_LOOSE_ENDS;

  LSA_COPY (&tdes->tail_lsa, log_lsa);
  LSA_COPY (&tdes->undo_nxlsa, &tdes->tail_lsa);

  /*
   * Need to read the start client record to set the start address
   * of top system operation
   */
  LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_RECORD_HEADER), log_lsa,
		      log_page_p);
  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p,
				    sizeof (struct log_topope_start_client),
				    log_lsa, log_page_p);
  top_start_client =
    ((struct log_topope_start_client *) ((char *) log_page_p->area +
					 log_lsa->offset));

  if (tdes->topops.max == 0 || (tdes->topops.last + 1) >= tdes->topops.max)
    {
      if (logtb_realloc_topops_stack (tdes, 1) == NULL)
	{
	  /* Out of memory */
	  logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
			     "log_rv_analysis_abort_topope_with_loose_ends");
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

  LSA_COPY (&tdes->topops.stack[tdes->topops.last].lastparent_lsa,
	    &top_start_client->lastparent_lsa);
  LSA_COPY (&tdes->topops.stack[tdes->topops.last].client_undo_lsa,
	    &top_start_client->lsa);

  return NO_ERROR;
}

/*
 * log_rv_analysis_run_next_client_undo -
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
log_rv_analysis_run_next_client_undo (THREAD_ENTRY * thread_p, int tran_id,
				      LOG_LSA * log_lsa,
				      LOG_PAGE * log_page_p,
				      LOG_LSA * check_point)
{
  LOG_TDES *tdes;
  struct log_run_client *run_client;


  tdes = logtb_rv_find_allocate_tran_index (thread_p, tran_id, log_lsa);
  if (tdes == NULL)
    {
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
			 "log_rv_analysis_run_next_client_undo");
      return ER_FAILED;
    }

  if (tdes->state != TRAN_UNACTIVE_ABORTED_WITH_CLIENT_USER_LOOSE_ENDS
      && (tdes->state !=
	  TRAN_UNACTIVE_TOPOPE_ABORTED_WITH_CLIENT_USER_LOOSE_ENDS))
    {
      /*
       * If we are comming from a checkpoint this is the result of a
       * system error since the transaction must have been already in
       * one of this states.
       * If we are not comming from a checkpoint, it is likely that
       * we are in a commit point of either a transactionor a top
       * operation.
       */
      if (!LSA_ISNULL (check_point))
	{
	  er_log_debug
	    (ARG_FILE_LINE,
	     "log_recovery_analysis: SYSTEM ERROR\n"
	     " Incorrect state = %s\n at log_rec at %lld|%d\n"
	     " for transaction = %d (index %d).\n"
	     " State should have been either of\n" " %s\n %s\n",
	     log_state_string (tdes->state), (long long int) log_lsa->pageid,
	     log_lsa->offset, tdes->trid, tdes->tran_index,
	     log_state_string
	     (TRAN_UNACTIVE_ABORTED_WITH_CLIENT_USER_LOOSE_ENDS),
	     log_state_string
	     (TRAN_UNACTIVE_TOPOPE_ABORTED_WITH_CLIENT_USER_LOOSE_ENDS));
	}
      /*
       * Continue the execution by guessing that the transaction has
       * been committed
       */
      if (tdes->topops.last == -1)
	{
	  tdes->state = TRAN_UNACTIVE_ABORTED_WITH_CLIENT_USER_LOOSE_ENDS;
	}
      else
	{
	  tdes->state =
	    TRAN_UNACTIVE_TOPOPE_ABORTED_WITH_CLIENT_USER_LOOSE_ENDS;
	}
    }

  /* Nothing to undo at server */
  LSA_SET_NULL (&tdes->undo_nxlsa);

  LSA_COPY (&tdes->tail_lsa, log_lsa);

  /*
   * Need to read the run client record to set the next client undo
   * address of the transaction
   */
  LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_RECORD_HEADER), log_lsa,
		      log_page_p);
  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (struct log_run_client),
				    log_lsa, log_page_p);
  run_client =
    (struct log_run_client *) ((char *) log_page_p->area + log_lsa->offset);

  if (tdes->topops.last == -1)
    {
      LSA_COPY (&tdes->client_undo_lsa, &run_client->nxlsa);
    }
  else
    {
      LSA_COPY (&tdes->topops.stack[tdes->topops.last].client_undo_lsa,
		&run_client->nxlsa);
    }

  return NO_ERROR;
}

/*
 * log_rv_analysis_run_next_client_postpone -
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
log_rv_analysis_run_next_client_postpone (THREAD_ENTRY * thread_p,
					  int tran_id, LOG_LSA * log_lsa,
					  LOG_PAGE * log_page_p,
					  LOG_LSA * check_point)
{
  LOG_TDES *tdes;
  struct log_run_client *run_client;


  tdes = logtb_rv_find_allocate_tran_index (thread_p, tran_id, log_lsa);
  if (tdes == NULL)
    {
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
			 "log_rv_analysis_run_next_client_postpone");
      return ER_FAILED;
    }

  if ((tdes->state !=
       TRAN_UNACTIVE_COMMITTED_WITH_CLIENT_USER_LOOSE_ENDS)
      && (tdes->state !=
	  TRAN_UNACTIVE_XTOPOPE_COMMITTED_WITH_CLIENT_USER_LOOSE_ENDS))
    {
      /*
       * If we are comming from a checkpoint this is the result of a
       * system error since the transaction must have been already in
       * one of this states.
       * If we are not comming from a checkpoint, it is likely that
       * we are in a commit point of either a transactionor a top
       * operation.
       */
      if (!LSA_ISNULL (check_point))
	{
	  er_log_debug (ARG_FILE_LINE,
			"log_recovery_analysis: SYSTEM ERROR\n"
			" Incorrect state = %s\n at log_rec at %lld|%d\n"
			" for transaction = %d (index %d).\n"
			" State should have been either of\n" " %s\n %s\n",
			log_state_string (tdes->state),
			(long long int) log_lsa->pageid,
			log_lsa->offset, tdes->trid, tdes->tran_index,
			log_state_string
			(TRAN_UNACTIVE_COMMITTED_WITH_CLIENT_USER_LOOSE_ENDS),
			log_state_string
			(TRAN_UNACTIVE_XTOPOPE_COMMITTED_WITH_CLIENT_USER_LOOSE_ENDS));
	}
      /*
       * Continue the execution by guessing that the transaction has
       * been committed
       */

      if (tdes->topops.last == -1)
	{
	  tdes->state = TRAN_UNACTIVE_COMMITTED_WITH_CLIENT_USER_LOOSE_ENDS;
	}
      else
	{
	  tdes->state =
	    TRAN_UNACTIVE_XTOPOPE_COMMITTED_WITH_CLIENT_USER_LOOSE_ENDS;
	}
    }

  /* Nothing to undo */
  LSA_SET_NULL (&tdes->undo_nxlsa);

  LSA_COPY (&tdes->tail_lsa, log_lsa);

  /*
   * Need to read the run client record to set the next client undo
   * address of the transaction
   */
  LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_RECORD_HEADER), log_lsa,
		      log_page_p);
  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (struct log_run_client),
				    log_lsa, log_page_p);
  run_client =
    (struct log_run_client *) ((char *) log_page_p->area + log_lsa->offset);
  if (tdes->topops.last == -1)
    {
      LSA_COPY (&tdes->client_posp_lsa, &run_client->nxlsa);
    }
  else
    {
      LSA_COPY (&tdes->topops.stack[tdes->topops.last].client_posp_lsa,
		&run_client->nxlsa);
    }

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
log_rv_analysis_complete (THREAD_ENTRY * thread_p, int tran_id,
			  LOG_LSA * log_lsa, LOG_PAGE * log_page_p,
			  LOG_LSA * end_redo_lsa, LOG_LSA * lsa,
			  bool is_media_crash, time_t * stop_at,
			  bool * did_incom_recovery)
{
  struct log_donetime *donetime;
  int tran_index;
  time_t last_at_time;
  char time_val[CTIME_MAX];

  /*
   * The transaction has been fully completed. therefore, it was not
   * active at the time of the crash
   */
  tran_index = logtb_find_tran_index (thread_p, tran_id);

  if (is_media_crash != true)
    {
      goto end;
    }


  /*
   * Need to read the donetime record to find out if we need to stop
   * the recovery at this point.
   */
  LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_RECORD_HEADER), log_lsa,
		      log_page_p);
  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (struct log_donetime),
				    log_lsa, log_page_p);

  donetime = (struct log_donetime *) ((char *) log_page_p->area
				      + log_lsa->offset);
  last_at_time = (time_t) donetime->at_time;
  if (stop_at != NULL && *stop_at != (time_t) - 1
      && difftime (*stop_at, last_at_time) < 0)
    {
#if !defined(NDEBUG)
      if (prm_get_bool_value (PRM_ID_LOG_TRACE_DEBUG))
	{
	  fprintf (stdout,
		   msgcat_message (MSGCAT_CATALOG_CUBRID,
				   MSGCAT_SET_LOG, MSGCAT_LOG_STARTS));
	  (void) ctime_r (&last_at_time, time_val);
	  fprintf (stdout,
		   msgcat_message (MSGCAT_CATALOG_CUBRID,
				   MSGCAT_SET_LOG,
				   MSGCAT_LOG_INCOMPLTE_MEDIA_RECOVERY),
		   end_redo_lsa->pageid, end_redo_lsa->offset, time_val);
	  fprintf (stdout,
		   msgcat_message (MSGCAT_CATALOG_CUBRID,
				   MSGCAT_SET_LOG, MSGCAT_LOG_STARTS));
	  fflush (stdout);
	}
#endif /* !NDEBUG */
      /*
       * Reset the log active and stop the recovery process at this
       * point. Before reseting the log, make sure that we are not
       * holding a page.
       */
      log_lsa->pageid = NULL_PAGEID;
      log_recovery_resetlog (thread_p, lsa, false, end_redo_lsa);
      *did_incom_recovery = true;
      LSA_SET_NULL (lsa);

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

/*
 * log_rv_analysis_complte_topope -
 *
 * return: error code
 *
 *   tran_id(in):
 *   lsa(in/out):
 *
 * Note:
 */
static int
log_rv_analysis_complte_topope (THREAD_ENTRY * thread_p, int tran_id,
				LOG_LSA * log_lsa)
{
  LOG_TDES *tdes;

  /*
   * The top system action is declared as finished. Pop it from the
   * stack of finished actions
   */
  tdes = logtb_rv_find_allocate_tran_index (thread_p, tran_id, log_lsa);
  if (tdes == NULL)
    {
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
			 "log_rv_analysis_complte_topope");
      return ER_FAILED;
    }

  LSA_COPY (&tdes->tail_lsa, log_lsa);
  LSA_COPY (&tdes->undo_nxlsa, &tdes->tail_lsa);

  if (tdes->topops.last >= 0)
    {
      tdes->topops.last--;
    }

  LSA_COPY (&tdes->tail_topresult_lsa, log_lsa);
  tdes->state = TRAN_UNACTIVE_UNILATERALLY_ABORTED;

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
log_rv_analysis_start_checkpoint (LOG_LSA * log_lsa, LOG_LSA * start_lsa,
				  bool * may_use_checkpoint)
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
log_rv_analysis_end_checkpoint (THREAD_ENTRY * thread_p, LOG_LSA * log_lsa,
				LOG_PAGE * log_page_p, LOG_LSA * check_point,
				LOG_LSA * start_redo_lsa,
				bool * may_use_checkpoint,
				bool * may_need_synch_checkpoint_2pc)
{
  LOG_TDES *tdes;
  struct log_chkpt *tmp_chkpt;
  struct log_chkpt chkpt;
  struct log_chkpt_trans *chkpt_trans;
  struct log_chkpt_trans *chkpt_one;
  struct log_chkpt_topops_commit_posp *chkpt_topops;
  struct log_chkpt_topops_commit_posp *chkpt_topone;
  int size;
  void *area;
  int i;

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
  LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_RECORD_HEADER), log_lsa,
		      log_page_p);
  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (struct log_chkpt),
				    log_lsa, log_page_p);
  tmp_chkpt =
    (struct log_chkpt *) ((char *) log_page_p->area + log_lsa->offset);
  chkpt = *tmp_chkpt;

  /* GET THE CHECKPOINT TRANSACTION INFORMATION */
  LOG_READ_ADD_ALIGN (thread_p, sizeof (struct log_chkpt), log_lsa,
		      log_page_p);

  /* Now get the data of active transactions */

  area = NULL;
  size = sizeof (struct log_chkpt_trans) * chkpt.ntrans;
  if (log_lsa->offset + size < (int) LOGAREA_SIZE)
    {
      chkpt_trans =
	(struct log_chkpt_trans *) ((char *) log_page_p->area +
				    log_lsa->offset);
      log_lsa->offset += size;
    }
  else
    {
      /* Need to copy the data into a contiguous area */
      area = malloc (size);
      if (area == NULL)
	{
	  logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
			     "log_recovery_analysis");
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}
      /* Copy the data */
      logpb_copy_from_log (thread_p, (char *) area, size, log_lsa,
			   log_page_p);
      chkpt_trans = (struct log_chkpt_trans *) area;
    }

  /* Add the transactions to the transaction table */
  for (i = 0; i < chkpt.ntrans; i++)
    {
      /*
       * If this is the first time, the transaction is seen. Assign a
       * new index to describe it and assume that the transaction was
       * active at the time of the crash, and thus it will be
       * unilateraly aborted. The truth of this statement will be find
       * reading the rest of the log
       */
      tdes =
	logtb_rv_find_allocate_tran_index (thread_p, chkpt_trans[i].trid,
					   log_lsa);
      if (tdes == NULL)
	{
	  if (area != NULL)
	    {
	      free_and_init (area);
	    }

	  logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
			     "log_recovery_analysis");
	  return ER_FAILED;
	}
      chkpt_one = &chkpt_trans[i];

      /*
       * Clear the transaction since it may have old stuff in it.
       * Use the one that is find in the checkpoint record
       */
      logtb_clear_tdes (thread_p, tdes);

      tdes->isloose_end = chkpt_one->isloose_end;
      if (chkpt_one->state != TRAN_ACTIVE
	  && chkpt_one->state != TRAN_UNACTIVE_ABORTED
	  && chkpt_one->state != TRAN_UNACTIVE_COMMITTED)
	{
	  tdes->state = chkpt_one->state;
	}
      else
	{
	  tdes->state = TRAN_UNACTIVE_UNILATERALLY_ABORTED;
	}
      LSA_COPY (&tdes->head_lsa, &chkpt_one->head_lsa);
      LSA_COPY (&tdes->tail_lsa, &chkpt_one->tail_lsa);
      LSA_COPY (&tdes->undo_nxlsa, &chkpt_one->undo_nxlsa);
      LSA_COPY (&tdes->posp_nxlsa, &chkpt_one->posp_nxlsa);
      LSA_COPY (&tdes->savept_lsa, &chkpt_one->savept_lsa);
      LSA_COPY (&tdes->tail_topresult_lsa, &chkpt_one->tail_topresult_lsa);
      LSA_COPY (&tdes->client_undo_lsa, &chkpt_one->client_undo_lsa);
      LSA_COPY (&tdes->client_posp_lsa, &chkpt_one->client_posp_lsa);
      logtb_set_client_ids_all (&tdes->client, 0, NULL, chkpt_one->user_name,
				NULL, NULL, NULL, -1);
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

  if (chkpt.ntops > 0)
    {
      size = sizeof (struct log_chkpt_topops_commit_posp) * chkpt.ntops;
      if (log_lsa->offset + size < (int) LOGAREA_SIZE)
	{
	  chkpt_topops = ((struct log_chkpt_topops_commit_posp *)
			  ((char *) log_page_p->area + log_lsa->offset));
	  log_lsa->offset += size;
	}
      else
	{
	  /* Need to copy the data into a contiguous area */
	  area = malloc (size);
	  if (area == NULL)
	    {
	      logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
				 "log_recovery_analysis");
	      return ER_OUT_OF_VIRTUAL_MEMORY;
	    }
	  /* Copy the data */
	  logpb_copy_from_log (thread_p, (char *) area, size, log_lsa,
			       log_page_p);
	  chkpt_topops = (struct log_chkpt_topops_commit_posp *) area;
	}

      /* Add the top system operations to the transactions */

      for (i = 0; i < chkpt.ntops; i++)
	{
	  chkpt_topone = &chkpt_topops[i];
	  tdes =
	    logtb_rv_find_allocate_tran_index (thread_p, chkpt_topone->trid,
					       log_lsa);
	  if (tdes == NULL)
	    {
	      if (area != NULL)
		{
		  free_and_init (area);
		}

	      logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
				 "log_recovery_analysis");
	      return ER_FAILED;
	    }

	  if (tdes->topops.max == 0 ||
	      (tdes->topops.last + 1) >= tdes->topops.max)
	    {
	      if (logtb_realloc_topops_stack (tdes, chkpt.ntops) == NULL)
		{
		  if (area != NULL)
		    {
		      free_and_init (area);
		    }

		  logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
				     "log_recovery_analysis");
		  return ER_OUT_OF_VIRTUAL_MEMORY;
		}
	    }

	  tdes->topops.last++;
	  LSA_COPY (&tdes->topops.stack[tdes->topops.last].lastparent_lsa,
		    &chkpt_topone->lastparent_lsa);
	  LSA_COPY (&tdes->topops.stack[tdes->topops.last].posp_lsa,
		    &chkpt_topone->posp_lsa);
	  LSA_COPY (&tdes->topops.stack[tdes->topops.last].client_posp_lsa,
		    &chkpt_topone->client_posp_lsa);
	  LSA_COPY (&tdes->topops.stack[tdes->topops.last].client_undo_lsa,
		    &chkpt_topone->client_undo_lsa);
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
log_rv_analysis_save_point (THREAD_ENTRY * thread_p, int tran_id,
			    LOG_LSA * log_lsa)
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
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
			 "log_rv_analysis_save_point");
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
log_rv_analysis_2pc_prepare (THREAD_ENTRY * thread_p, int tran_id,
			     LOG_LSA * log_lsa)
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
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
			 "log_rv_analysis_2pc_prepare");
      return ER_FAILED;
    }

  tdes->state = TRAN_UNACTIVE_2PC_PREPARE;
  LSA_COPY (&tdes->tail_lsa, log_lsa);

  /* Put a note that prepare_to_commit log record needs to be read
   * during either redo phase, or during finish_commit_protocol phase
   */
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
log_rv_analysis_2pc_start (THREAD_ENTRY * thread_p, int tran_id,
			   LOG_LSA * log_lsa)
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
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
			 "log_rv_analysis_2pc_start");
      return ER_FAILED;
    }

  tdes->state = TRAN_UNACTIVE_2PC_COLLECTING_PARTICIPANT_VOTES;
  LSA_COPY (&tdes->tail_lsa, log_lsa);

  /* Put a note that prepare_to_commit log record needs to be read
   * during either redo phase, or during finish_commit_protocol phase
   */
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
log_rv_analysis_2pc_commit_decision (THREAD_ENTRY * thread_p, int tran_id,
				     LOG_LSA * log_lsa)
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
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
			 "log_rv_analysis_2pc_commit_decision");
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
log_rv_analysis_2pc_abort_decision (THREAD_ENTRY * thread_p, int tran_id,
				    LOG_LSA * log_lsa)
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
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
			 "log_rv_analysis_2pc_abort_decision");
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
log_rv_analysis_2pc_commit_inform_particps (THREAD_ENTRY * thread_p,
					    int tran_id, LOG_LSA * log_lsa)
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
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
			 "log_rv_analysis_2pc_commit_inform_particps");
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
log_rv_analysis_2pc_abort_inform_particps (THREAD_ENTRY * thread_p,
					   int tran_id, LOG_LSA * log_lsa)
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
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
			 "log_recovery_analysis");
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
log_rv_analysis_2pc_recv_ack (THREAD_ENTRY * thread_p, int tran_id,
			      LOG_LSA * log_lsa)
{
  LOG_TDES *tdes;

  tdes = logtb_rv_find_allocate_tran_index (thread_p, tran_id, log_lsa);
  if (tdes == NULL)
    {
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
			 "log_recovery_analysis");
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
log_rv_analysis_record (THREAD_ENTRY * thread_p, LOG_RECTYPE log_type,
			int tran_id, LOG_LSA * log_lsa, LOG_PAGE * log_page_p,
			LOG_LSA * checkpoint_lsa, LOG_LSA * lsa,
			LOG_LSA * start_lsa, LOG_LSA * start_redo_lsa,
			LOG_LSA * end_redo_lsa, bool is_media_crash,
			time_t * stop_at, bool * did_incom_recovery,
			bool * may_use_checkpoint,
			bool * may_need_synch_checkpoint_2pc)
{
  switch (log_type)
    {
    case LOG_CLIENT_NAME:
      (void) log_rv_analysis_client_name (thread_p, tran_id, log_lsa,
					  log_page_p);
      break;

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
      (void) log_rv_analysis_run_postpone (thread_p, tran_id, log_lsa,
					   log_page_p, checkpoint_lsa);
      break;

    case LOG_COMPENSATE:
      (void) log_rv_analysis_compensate (thread_p, tran_id, log_lsa,
					 log_page_p);
      break;

    case LOG_LCOMPENSATE:
      (void) log_rv_analysis_lcompensate (thread_p, tran_id, log_lsa,
					  log_page_p);
      break;

    case LOG_CLIENT_USER_UNDO_DATA:
      (void) log_rv_analysis_client_user_undo_data (thread_p, tran_id,
						    log_lsa);
      break;

    case LOG_CLIENT_USER_POSTPONE_DATA:
      (void) log_rv_analysis_client_user_postpone_data (thread_p, tran_id,
							log_lsa);
      break;

    case LOG_WILL_COMMIT:
      (void) log_rv_analysis_will_commit (thread_p, tran_id, log_lsa);
      break;

    case LOG_COMMIT_WITH_POSTPONE:
      (void) log_rv_analysis_commit_with_postpone (thread_p, tran_id, log_lsa,
						   log_page_p);
      break;

    case LOG_COMMIT_WITH_CLIENT_USER_LOOSE_ENDS:
      (void) log_rv_analysis_commit_with_loose_ends (thread_p, tran_id,
						     log_lsa, log_page_p);
      break;

    case LOG_ABORT_WITH_CLIENT_USER_LOOSE_ENDS:
      (void) log_rv_analysis_abort_with_loose_ends (thread_p, tran_id,
						    log_lsa, log_page_p);
      break;

    case LOG_COMMIT_TOPOPE_WITH_POSTPONE:
      (void) log_rv_analysis_commit_topope_with_postpone (thread_p, tran_id,
							  log_lsa,
							  log_page_p);
      break;

    case LOG_COMMIT_TOPOPE_WITH_CLIENT_USER_LOOSE_ENDS:
      (void)
	log_rv_analysis_commit_topope_with_loose_ends (thread_p, tran_id,
						       log_lsa, log_page_p);
      break;

    case LOG_ABORT_TOPOPE_WITH_CLIENT_USER_LOOSE_ENDS:
      (void)
	log_rv_analysis_abort_topope_with_loose_ends (thread_p, tran_id,
						      log_lsa, log_page_p);
      break;

    case LOG_RUN_NEXT_CLIENT_UNDO:
      (void) log_rv_analysis_run_next_client_undo (thread_p, tran_id, log_lsa,
						   log_page_p,
						   checkpoint_lsa);
      break;

    case LOG_RUN_NEXT_CLIENT_POSTPONE:
      (void) log_rv_analysis_run_next_client_postpone (thread_p, tran_id,
						       log_lsa, log_page_p,
						       checkpoint_lsa);
      break;

    case LOG_COMMIT:
    case LOG_ABORT:
      (void) log_rv_analysis_complete (thread_p, tran_id, log_lsa, log_page_p,
				       end_redo_lsa, lsa, is_media_crash,
				       stop_at, did_incom_recovery);
      break;

    case LOG_COMMIT_TOPOPE:
    case LOG_ABORT_TOPOPE:
      log_rv_analysis_complte_topope (thread_p, tran_id, log_lsa);
      break;

    case LOG_START_CHKPT:
      log_rv_analysis_start_checkpoint (log_lsa, start_lsa,
					may_use_checkpoint);
      break;

    case LOG_END_CHKPT:
      log_rv_analysis_end_checkpoint (thread_p, log_lsa, log_page_p,
				      checkpoint_lsa, start_redo_lsa,
				      may_use_checkpoint,
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
      (void) log_rv_analysis_2pc_commit_inform_particps (thread_p, tran_id,
							 log_lsa);
      break;

    case LOG_2PC_ABORT_INFORM_PARTICPS:
      (void) log_rv_analysis_2pc_abort_inform_particps (thread_p, tran_id,
							log_lsa);
      break;

    case LOG_2PC_RECV_ACK:
      (void) log_rv_analysis_2pc_recv_ack (thread_p, tran_id, log_lsa);
      break;

    case LOG_END_OF_LOG:
      (void) log_rv_analysis_log_end (tran_id, log_lsa);
      break;

    case LOG_DUMMY_CRASH_RECOVERY:
    case LOG_DUMMY_FILLPAGE_FORARCHIVE:	/* for backward compatibility */
    case LOG_REPLICATION_DATA:
    case LOG_REPLICATION_SCHEMA:
    case LOG_UNLOCK_COMMIT:
    case LOG_UNLOCK_ABORT:
    case LOG_DUMMY_HA_SERVER_STATE:
    case LOG_DUMMY_OVF_RECORD:
      break;

    case LOG_SMALLER_LOGREC_TYPE:
    case LOG_LARGER_LOGREC_TYPE:
    default:
#if defined(CUBRID_DEBUG)
      er_log_debug (ARG_FILE_LINE,
		    "log_recovery_analysis: Unknown record"
		    " type = %d (%s) ... May be a system error\n",
		    log_rtype, log_to_string (log_rtype));
#endif /* CUBRID_DEBUG */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_PAGE_CORRUPTED,
	      1, log_lsa->pageid);

      if (LSA_EQ (lsa, log_lsa))
	{
	  lsa->pageid = NULL_PAGEID;
	}
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
log_recovery_analysis (THREAD_ENTRY * thread_p, LOG_LSA * start_lsa,
		       LOG_LSA * start_redo_lsa, LOG_LSA * end_redo_lsa,
		       int is_media_crash, time_t * stop_at,
		       bool * did_incom_recovery,
		       INT64 * num_redo_log_records)
{
  LOG_LSA checkpoint_lsa = { -1, -1 };
  LOG_LSA lsa;			/* LSA of log record to analyse */
  char log_pgbuf[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT], *aligned_log_pgbuf;
  LOG_PAGE *log_page_p = NULL;	/* Log page pointer where LSA
				 * is located
				 */
  LOG_LSA log_lsa;
  LOG_RECTYPE log_rtype;	/* Log record type            */
  LOG_RECORD_HEADER *log_rec = NULL;	/* Pointer to log record      */
  struct log_chkpt *tmp_chkpt;	/* Temp Checkpoint log record */
  struct log_chkpt chkpt;	/* Checkpoint log record     */
  struct log_chkpt_trans *chkpt_trans;
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
  *did_incom_recovery = false;

  log_page_p = (LOG_PAGE *) aligned_log_pgbuf;

  while (!LSA_ISNULL (&lsa))
    {
      /* Fetch the page where the LSA record to undo is located */
      log_lsa.pageid = lsa.pageid;
      if (logpb_fetch_page (thread_p, log_lsa.pageid, log_page_p) == NULL)
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
		  fprintf (stdout,
			   msgcat_message (MSGCAT_CATALOG_CUBRID,
					   MSGCAT_SET_LOG,
					   MSGCAT_LOG_STARTS));
		  (void) ctime_r (&last_at_time, time_val);
		  fprintf (stdout,
			   msgcat_message (MSGCAT_CATALOG_CUBRID,
					   MSGCAT_SET_LOG,
					   MSGCAT_LOG_INCOMPLTE_MEDIA_RECOVERY),
			   end_redo_lsa->pageid, end_redo_lsa->offset,
			   ((last_at_time == -1) ? "???...\n" : time_val));
		  fprintf (stdout,
			   msgcat_message (MSGCAT_CATALOG_CUBRID,
					   MSGCAT_SET_LOG,
					   MSGCAT_LOG_STARTS));
		  fflush (stdout);
		}
#endif /* !NDEBUG */
	      /* if previous log record exists,
	       * reset tdes->tail_lsa/undo_nxlsa as previous of end_redo_lsa */
	      if (log_rec != NULL)
		{
		  LOG_TDES *last_log_tdes =
		    LOG_FIND_TDES (logtb_find_tran_index
				   (thread_p, log_rec->trid));
		  if (last_log_tdes != NULL)
		    {
		      LSA_COPY (&last_log_tdes->tail_lsa,
				&log_rec->prev_tranlsa);
		      LSA_COPY (&last_log_tdes->undo_nxlsa,
				&log_rec->prev_tranlsa);
		    }
		}
	      log_recovery_resetlog (thread_p, &lsa, true, end_redo_lsa);
	      *did_incom_recovery = true;
	      log_Gl.mvcc_table.highest_completed_mvccid =
		log_Gl.hdr.mvcc_next_id;
	      MVCCID_BACKWARD (log_Gl.mvcc_table.highest_completed_mvccid);
	      return;
	    }
	  else
	    {
	      logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
				 "log_recovery_analysis");
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
	      && (lsa.pageid < log_lsa.pageid
		  || (lsa.pageid == log_lsa.pageid
		      && lsa.offset <= log_lsa.offset)))
	    {
	      /* It seems to be a system error. Maybe a loop in the log */
	      er_log_debug (ARG_FILE_LINE,
			    "log_recovery_analysis: ** System error:"
			    " It seems to be a loop in the log\n."
			    " Current log_rec at %lld|%d. Next log_rec at %lld|%d\n",
			    (long long int) log_lsa.pageid, log_lsa.offset,
			    (long long int) lsa.pageid, lsa.offset);
	      logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
				 "log_recovery_analysis");
	      LSA_SET_NULL (&lsa);
	      break;
	    }

	  if (LSA_ISNULL (&lsa) && log_rtype != LOG_END_OF_LOG
	      && *did_incom_recovery == false)
	    {
	      LOG_RESET_APPEND_LSA (end_redo_lsa);
	      if (log_startof_nxrec (thread_p, &log_Gl.hdr.append_lsa, true)
		  == NULL)
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
		  logpb_write_page_to_disk (thread_p, log_page_p,
					    log_lsa.pageid);
		}
	      er_log_debug (ARG_FILE_LINE,
			    "log_recovery_analysis: ** WARNING:"
			    " An end of the log record was not found."
			    " Will Assume = %lld|%d and Next Trid = %d\n",
			    (long long int) log_Gl.hdr.append_lsa.pageid,
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

	  log_rv_analysis_record (thread_p, log_rtype, tran_id, &log_lsa,
				  log_page_p, &checkpoint_lsa, &lsa,
				  start_lsa, start_redo_lsa, end_redo_lsa,
				  is_media_crash, stop_at, did_incom_recovery,
				  &may_use_checkpoint,
				  &may_need_synch_checkpoint_2pc);

	  /*
	   * We can fix the lsa.pageid in the case of log_records without forward
	   * address at this moment.
	   */
	  if (lsa.offset == NULL_OFFSET && lsa.pageid != NULL_PAGEID
	      && lsa.pageid < log_lsa.pageid)
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

      if (logpb_fetch_page (thread_p, log_lsa.pageid, log_page_p) == NULL)
	{
	  /*
	   * There is a problem. We have just read this page a little while ago
	   */
	  logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
			     "log_recovery_analysis");
	  return;
	}

      log_rec = LOG_GET_LOG_RECORD_HEADER (log_page_p, &log_lsa);

      /* Read the DATA HEADER */
      LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_RECORD_HEADER), &log_lsa,
			  log_page_p);
      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (struct log_chkpt),
					&log_lsa, log_page_p);
      tmp_chkpt =
	(struct log_chkpt *) ((char *) log_page_p->area + log_lsa.offset);
      chkpt = *tmp_chkpt;

      /* GET THE CHECKPOINT TRANSACTION INFORMATION */
      LOG_READ_ADD_ALIGN (thread_p, sizeof (struct log_chkpt), &log_lsa,
			  log_page_p);

      /* Now get the data of active transactions */
      area = NULL;
      size = sizeof (struct log_chkpt_trans) * chkpt.ntrans;
      if (log_lsa.offset + size < (int) LOGAREA_SIZE)
	{
	  chkpt_trans =
	    (struct log_chkpt_trans *) ((char *) log_page_p->area +
					log_lsa.offset);
	}
      else
	{
	  /* Need to copy the data into a contiguous area */
	  area = malloc (size);
	  if (area == NULL)
	    {
	      logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
				 "log_recovery_analysis");
	      return;
	    }
	  /* Copy the data */
	  logpb_copy_from_log (thread_p, (char *) area, size, &log_lsa,
			       log_page_p);
	  chkpt_trans = (struct log_chkpt_trans *) area;
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
		  log_2pc_recovery_analysis_info (thread_p, tdes,
						  &chkpt_trans[i].tail_lsa);
		}
	    }
	}
      if (area != NULL)
	{
	  free_and_init (area);
	}
    }

  log_Gl.mvcc_table.highest_completed_mvccid = log_Gl.hdr.mvcc_next_id;
  MVCCID_BACKWARD (log_Gl.mvcc_table.highest_completed_mvccid);

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
log_recovery_redo (THREAD_ENTRY * thread_p, const LOG_LSA * start_redolsa,
		   const LOG_LSA * end_redo_lsa, time_t * stopat)
{
  LOG_LSA lsa;			/* LSA of log record to redo  */
  char log_pgbuf[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT], *aligned_log_pgbuf;
  LOG_PAGE *log_pgptr = NULL;	/* Log page pointer where LSA
				 * is located
				 */
  LOG_LSA log_lsa;
  LOG_RECORD_HEADER *log_rec = NULL;	/* Pointer to log record */
  struct log_undoredo *undoredo = NULL;	/* Undo_redo log record */
  struct log_mvcc_undoredo *mvcc_undoredo = NULL;	/* MVCC op undo/redo log record */
  struct log_redo *redo = NULL;	/* Redo log record */
  struct log_mvcc_redo *mvcc_redo = NULL;	/* MVCC op redo log record */
  struct log_mvcc_undo *mvcc_undo = NULL;	/* MVCC op undo log record */
  struct log_dbout_redo *dbout_redo = NULL;	/* A external redo log record */
  struct log_compensate *compensate = NULL;	/* Compensating log record */
  struct log_run_postpone *run_posp = NULL;	/* A run postpone action */
  struct log_2pc_start *start_2pc = NULL;	/* Start 2PC commit log record */
  struct log_2pc_particp_ack *received_ack = NULL;	/* A 2PC participant ack */
  struct log_donetime *donetime = NULL;
  LOG_RCV rcv;			/* Recovery structure */
  VPID rcv_vpid;		/* VPID of data to recover */
  LOG_RCVINDEX rcvindex;	/* Recovery index function */
  LOG_LSA rcv_lsa;		/* Address of redo log record */
  LOG_LSA *rcv_page_lsaptr;	/* LSA of data page for log
				 * record to redo
				 */
  LOG_LSA rcv_vacuum_data_lsa;
  LOG_TDES *tdes;		/* Transaction descriptor */
  int num_particps;		/* Number of participating sites */
  int particp_id_length;	/* Length of particp_ids block */
  void *block_particps_ids;	/* A block of participant ids */
  int temp_length;
  int tran_index;
  int i;
  int data_header_size = 0;
  MVCCID mvccid = MVCCID_NULL;
  bool ignore_redofunc;
  LOG_ZIP *undo_unzip_ptr = NULL;
  LOG_ZIP *redo_unzip_ptr = NULL;
  bool is_diff_rec;
  LOG_PAGEID last_vacuum_data_pageid = NULL_PAGEID;
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

  if (mvcc_Enabled)
    {
      /* Get the identifier for the last saved block in vacuum data. Data about
       * blocks after this must be recovered.
       */
      last_vacuum_data_pageid = vacuum_data_get_last_log_pageid (thread_p);
    }

  /* Reset mvcc_op_log_lsa as it may not point to the last mvcc operation.
   * It will be recovered.
   * NOTE: We will lose one link from the chain of MVCC operations, but that
   * is not relevant. The lost link is between the last entry in a block and
   * the first entry of a following block. The lost link will not affect the
   * vacuum process.
   */
  LSA_SET_NULL (&log_Gl.hdr.mvcc_op_log_lsa);

  while (!LSA_ISNULL (&lsa))
    {
      /* Fetch the page where the LSA record to undo is located */
      log_lsa.pageid = lsa.pageid;
      if (logpb_fetch_page (thread_p, log_lsa.pageid, log_pgptr) == NULL)
	{
	  if (end_redo_lsa != NULL
	      && (LSA_ISNULL (end_redo_lsa) || LSA_GT (&lsa, end_redo_lsa)))
	    {
	      return;
	    }
	  else
	    {
	      logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
				 "log_recovery_redo");
	      return;
	    }
	}

      /* Check all log records in this phase */
      while (lsa.pageid == log_lsa.pageid)
	{
	  /*
	   * Do we want to stop the recovery redo process at this time ?
	   */
	  if (end_redo_lsa != NULL && !LSA_ISNULL (end_redo_lsa)
	      && LSA_GT (&lsa, end_redo_lsa))
	    {
	      LSA_SET_NULL (&lsa);
	      break;
	    }

	  /*
	   * If an offset is missing, it is because we archive an incomplete
	   * log record. This log_record was completed later. Thus, we have to
	   * find the offset by searching for the next log_record in the page
	   */
	  if (lsa.offset == NULL_OFFSET
	      && (lsa.offset = log_pgptr->hdr.offset) == NULL_OFFSET)
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

	  /* Find the log record */
	  log_lsa.offset = lsa.offset;
	  log_rec = LOG_GET_LOG_RECORD_HEADER (log_pgptr, &log_lsa);

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
	      && (lsa.pageid < log_lsa.pageid
		  || (lsa.pageid == log_lsa.pageid
		      && lsa.offset <= log_lsa.offset)))
	    {
	      /* It seems to be a system error. Maybe a loop in the log */
	      er_log_debug (ARG_FILE_LINE,
			    "log_recovery_redo: ** System error:"
			    " It seems to be a loop in the log\n."
			    " Current log_rec at %lld|%d. Next log_rec at %lld|%d\n",
			    (long long int) log_lsa.pageid, log_lsa.offset,
			    (long long int) lsa.pageid, lsa.offset);
	      logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
				 "log_recovery_redo");
	      LSA_SET_NULL (&lsa);
	      break;
	    }

	  switch (log_rec->type)
	    {
	    case LOG_MVCC_UNDOREDO_DATA:
	    case LOG_MVCC_DIFF_UNDOREDO_DATA:
	    case LOG_UNDOREDO_DATA:
	    case LOG_DIFF_UNDOREDO_DATA:
	      /* Is diff record type? */
	      if (log_rec->type == LOG_DIFF_UNDOREDO_DATA
		  || log_rec->type == LOG_MVCC_DIFF_UNDOREDO_DATA)
		{
		  is_diff_rec = true;
		}
	      else
		{
		  is_diff_rec = false;
		}

	      /* Does record belong to a MVCC op */
	      if (log_rec->type == LOG_MVCC_UNDOREDO_DATA
		  || log_rec->type == LOG_MVCC_DIFF_UNDOREDO_DATA)
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
	      LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_RECORD_HEADER),
				  &log_lsa, log_pgptr);

	      if (is_mvcc_op)
		{
		  /* Data header is a MVCC undoredo */
		  data_header_size = sizeof (struct log_mvcc_undoredo);
		  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p,
						    data_header_size,
						    &log_lsa, log_pgptr);
		  mvcc_undoredo =
		    (struct log_mvcc_undoredo *) ((char *) log_pgptr->area +
						  log_lsa.offset);

		  /* Get undoredo structure */
		  undoredo = &mvcc_undoredo->undoredo;

		  /* Get transaction MVCCID */
		  mvccid = mvcc_undoredo->mvccid;

		  /* Check if MVCC next ID must be updated */
		  if (!mvcc_id_precedes (mvccid, log_Gl.hdr.mvcc_next_id))
		    {
		      log_Gl.hdr.mvcc_next_id = mvccid;
		      MVCCID_FORWARD (log_Gl.hdr.mvcc_next_id);
		    }
		}
	      else
		{
		  /* Data header is a regular undoredo */
		  data_header_size = sizeof (struct log_undoredo);
		  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p,
						    data_header_size,
						    &log_lsa, log_pgptr);
		  undoredo =
		    (struct log_undoredo *) ((char *) log_pgptr->area +
					     log_lsa.offset);

		  mvccid = MVCCID_NULL;
		}

	      assert (!LOG_IS_VACUUM_DATA_RECOVERY (undoredo->data.rcvindex));

	      /* Do we need to redo anything ? */

	      /*
	       * Fetch the page for physical log records and check if redo
	       * is needed by comparing the log sequence numbers
	       */

	      rcv_vpid.volid = undoredo->data.volid;
	      rcv_vpid.pageid = undoredo->data.pageid;

	      rcv.pgptr = NULL;
	      /* If the page does not exit, there is nothing to redo */
	      if (rcv_vpid.pageid != NULL_PAGEID
		  && rcv_vpid.volid != NULL_VOLID)
		{
		  if (disk_isvalid_page (thread_p, rcv_vpid.volid,
					 rcv_vpid.pageid) != DISK_VALID)
		    {
		      break;
		    }
		  rcv.pgptr = pgbuf_fix (thread_p, &rcv_vpid, OLD_PAGE,
					 PGBUF_LATCH_WRITE,
					 PGBUF_UNCONDITIONAL_LATCH);
		  if (rcv.pgptr == NULL)
		    {
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
		  if (LSA_LE (&rcv_lsa, rcv_page_lsaptr)
		      && (end_redo_lsa == NULL || LSA_ISNULL (end_redo_lsa)
			  || LSA_LE (rcv_page_lsaptr, end_redo_lsa)))
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

	      rcvindex = undoredo->data.rcvindex;
	      rcv.length = undoredo->rlength;
	      rcv.offset = undoredo->data.offset;
	      rcv.mvcc_id = mvccid;

	      LOG_READ_ADD_ALIGN (thread_p, data_header_size, &log_lsa,
				  log_pgptr);

	      if (is_diff_rec)
		{
		  /* Get Undo data */
		  if (!log_rv_get_unzip_log_data
		      (thread_p, temp_length, &log_lsa, log_pgptr,
		       undo_unzip_ptr))
		    {
		      logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
					 "log_recovery_redo");
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
			  if (temp_length + log_lsa.offset >=
			      (int) LOGAREA_SIZE)
			    {
			      temp_length -=
				LOGAREA_SIZE - (int) (log_lsa.offset);
			      assert (log_pgptr != NULL);
			      if ((logpb_fetch_page
				   (thread_p, ++log_lsa.pageid,
				    log_pgptr)) == NULL)
				{
				  logpb_fatal_error (thread_p, true,
						     ARG_FILE_LINE,
						     "log_recovery_redo");
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
			   "      volid = %d, pageid = %d, offset = %d,\n",
			   (long long int) rcv_lsa.pageid,
			   (int) rcv_lsa.offset,
			   rv_rcvindex_string (rcvindex), rcv_vpid.volid,
			   rcv_vpid.pageid, rcv.offset);
		  if (rcv_page_lsaptr != NULL)
		    {
		      fprintf (stdout, "      page_lsa = %lld|%d\n",
			       (long long int) rcv_page_lsaptr->pageid,
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
		  log_rv_redo_record (thread_p, &log_lsa,
				      log_pgptr, RV_fun[rcvindex].redofun,
				      &rcv, &rcv_lsa, false,
				      (int) undo_unzip_ptr->data_length,
				      (char *) undo_unzip_ptr->log_data,
				      redo_unzip_ptr);
		}
	      else
		{
		  log_rv_redo_record (thread_p, &log_lsa,
				      log_pgptr, RV_fun[rcvindex].redofun,
				      &rcv, &rcv_lsa, false, 0, NULL,
				      redo_unzip_ptr);
		}
	      if (rcv.pgptr != NULL)
		{
		  pgbuf_unfix (thread_p, rcv.pgptr);
		}

	      if (is_mvcc_op && (rcv_lsa.pageid > last_vacuum_data_pageid))
		{
		  /* Recover vacuum data */
		  log_recovery_vacuum_data_buffer (thread_p, &rcv_lsa,
						   mvccid);
		}
	      break;

	    case LOG_MVCC_REDO_DATA:
	    case LOG_REDO_DATA:
	      /* Does log record belong to a MVCC op? */
	      is_mvcc_op = (log_rec->type == LOG_MVCC_REDO_DATA);

	      LSA_COPY (&rcv_lsa, &log_lsa);

	      /* Get the DATA HEADER */
	      LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_RECORD_HEADER),
				  &log_lsa, log_pgptr);

	      if (is_mvcc_op)
		{
		  /* Data header is MVCC redo */
		  data_header_size = sizeof (struct log_mvcc_redo);
		  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p,
						    data_header_size,
						    &log_lsa, log_pgptr);
		  mvcc_redo =
		    (struct log_mvcc_redo *) ((char *) log_pgptr->area +
					      log_lsa.offset);
		  /* Get redo info */
		  redo = &mvcc_redo->redo;

		  /* Get transaction MVCCID */
		  mvccid = mvcc_redo->mvccid;

		  /* Check if MVCC next ID must be updated */
		  if (!mvcc_id_precedes (mvccid, log_Gl.hdr.mvcc_next_id))
		    {
		      log_Gl.hdr.mvcc_next_id = mvccid;
		      MVCCID_FORWARD (log_Gl.hdr.mvcc_next_id);
		    }
		}
	      else
		{
		  /* Data header is regular redo */
		  data_header_size = sizeof (struct log_redo);
		  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p,
						    data_header_size,
						    &log_lsa, log_pgptr);

		  redo =
		    (struct log_redo *) ((char *) log_pgptr->area +
					 log_lsa.offset);

		  mvccid = MVCCID_NULL;
		}

	      /* Do we need to redo anything ? */

	      /* For vacuum data records check vacuum data lsa */
	      if (LOG_IS_VACUUM_DATA_RECOVERY (redo->data.rcvindex))
		{
		  vacuum_get_vacuum_data_lsa (thread_p, &rcv_vacuum_data_lsa);
		  if (LSA_LE (&rcv_lsa, &rcv_vacuum_data_lsa)
		      && (end_redo_lsa == NULL || LSA_ISNULL (end_redo_lsa)
			  || LSA_LE (&rcv_vacuum_data_lsa, end_redo_lsa)))
		    {
		      break;
		    }
		}

	      /*
	       * Fetch the page for physical log records and check if redo
	       * is needed by comparing the log sequence numbers
	       */

	      rcv_vpid.volid = redo->data.volid;
	      rcv_vpid.pageid = redo->data.pageid;

	      rcv.pgptr = NULL;
	      /* If the page does not exit, there is nothing to redo */
	      if (rcv_vpid.pageid != NULL_PAGEID
		  && rcv_vpid.volid != NULL_VOLID)
		{
		  if (disk_isvalid_page (thread_p, rcv_vpid.volid,
					 rcv_vpid.pageid) != DISK_VALID)
		    {
		      break;
		    }
		  rcv.pgptr = pgbuf_fix (thread_p, &rcv_vpid, OLD_PAGE,
					 PGBUF_LATCH_WRITE,
					 PGBUF_UNCONDITIONAL_LATCH);
		  if (rcv.pgptr == NULL)
		    {
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
		  if (LSA_LE (&rcv_lsa, rcv_page_lsaptr)
		      && (end_redo_lsa == NULL || LSA_ISNULL (end_redo_lsa)
			  || LSA_LE (rcv_page_lsaptr, end_redo_lsa)))
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

	      rcvindex = redo->data.rcvindex;
	      rcv.length = redo->length;
	      rcv.offset = redo->data.offset;
	      rcv.mvcc_id = mvccid;

	      LOG_READ_ADD_ALIGN (thread_p, data_header_size, &log_lsa,
				  log_pgptr);

#if !defined(NDEBUG)
	      if (prm_get_bool_value (PRM_ID_LOG_TRACE_DEBUG))
		{
		  fprintf (stdout,
			   "TRACE REDOING[2]: LSA = %lld|%d, Rv_index = %s,\n"
			   "      volid = %d, pageid = %d, offset = %d,\n",
			   (long long int) rcv_lsa.pageid,
			   (int) rcv_lsa.offset,
			   rv_rcvindex_string (rcvindex), rcv_vpid.volid,
			   rcv_vpid.pageid, rcv.offset);
		  if (rcv_page_lsaptr != NULL)
		    {
		      fprintf (stdout, "      page_lsa = %lld|%d\n",
			       (long long int) rcv_page_lsaptr->pageid,
			       rcv_page_lsaptr->offset);
		    }
		  else
		    {
		      fprintf (stdout, "      page_lsa = %lld|%d\n", -1LL,
			       -1);
		    }
		  fflush (stdout);
		}
#endif /* !NDEBUG */

	      ignore_redofunc = false;
	      if (rcvindex == RVBT_OID_TRUNCATE
		  || rcvindex == RVBT_KEYVAL_DEL_OID_TRUNCATE)
		{
		  tdes = LOG_FIND_TDES (logtb_find_tran_index (thread_p,
							       log_rec->
							       trid));
		  /* if RVBT_OID_TRUNCATE or RVBT_KEYVAL_DEL_OID_TRUNCATE
		   * redo log is the last log of the tranx,
		   * then NEVER redo it.
		   */
		  if (tdes != NULL && LSA_EQ (&rcv_lsa, &tdes->tail_lsa))
		    {
		      ignore_redofunc = true;
		    }
		}
	      log_rv_redo_record (thread_p, &log_lsa,
				  log_pgptr, RV_fun[rcvindex].redofun, &rcv,
				  &rcv_lsa, ignore_redofunc, 0, NULL,
				  redo_unzip_ptr);

	      if (rcv.pgptr != NULL)
		{
		  pgbuf_unfix (thread_p, rcv.pgptr);
		}

	      if (LOG_IS_VACUUM_DATA_RECOVERY (rcvindex))
		{
		  vacuum_set_vacuum_data_lsa (thread_p, &rcv_lsa);
		}
	      break;

	    case LOG_DBEXTERN_REDO_DATA:
	      LSA_COPY (&rcv_lsa, &log_lsa);

	      /* Get the DATA HEADER */
	      LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_RECORD_HEADER),
				  &log_lsa, log_pgptr);
	      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p,
						sizeof (struct
							log_dbout_redo),
						&log_lsa, log_pgptr);

	      dbout_redo = ((struct log_dbout_redo *)
			    ((char *) log_pgptr->area + log_lsa.offset));

	      VPID_SET_NULL (&rcv_vpid);
	      rcv.offset = -1;
	      rcv.pgptr = NULL;

	      rcvindex = dbout_redo->rcvindex;
	      rcv.length = dbout_redo->length;

	      assert (!LOG_IS_VACUUM_DATA_RECOVERY (rcvindex));

	      /* GET AFTER DATA */
	      LOG_READ_ADD_ALIGN (thread_p, sizeof (struct log_dbout_redo),
				  &log_lsa, log_pgptr);

#if !defined(NDEBUG)
	      if (prm_get_bool_value (PRM_ID_LOG_TRACE_DEBUG))
		{
		  fprintf (stdout,
			   "TRACE EXT REDOING[3]: LSA = %lld|%d, Rv_index = %s\n",
			   (long long int) rcv_lsa.pageid, rcv_lsa.offset,
			   rv_rcvindex_string (rcvindex));
		  fflush (stdout);
		}
#endif /* !NDEBUG */

	      log_rv_redo_record (thread_p, &log_lsa,
				  log_pgptr, RV_fun[rcvindex].redofun, &rcv,
				  &rcv_lsa, false, 0, NULL, NULL);
	      break;

	    case LOG_RUN_POSTPONE:
	      LSA_COPY (&rcv_lsa, &log_lsa);

	      /* Get the DATA HEADER */
	      LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_RECORD_HEADER),
				  &log_lsa, log_pgptr);
	      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p,
						sizeof (struct
							log_run_postpone),
						&log_lsa, log_pgptr);
	      run_posp =
		(struct log_run_postpone *) ((char *) log_pgptr->area +
					     log_lsa.offset);

	      assert (!LOG_IS_VACUUM_DATA_RECOVERY (run_posp->data.rcvindex));

	      /* Do we need to redo anything ? */

	      /*
	       * Fetch the page for physical log records and check if redo
	       * is needed by comparing the log sequence numbers
	       */

	      rcv_vpid.volid = run_posp->data.volid;
	      rcv_vpid.pageid = run_posp->data.pageid;

	      rcv.pgptr = NULL;
	      /* If the page does not exit, there is nothing to redo */
	      if (rcv_vpid.pageid != NULL_PAGEID
		  && rcv_vpid.volid != NULL_VOLID)
		{
		  if (disk_isvalid_page (thread_p, rcv_vpid.volid,
					 rcv_vpid.pageid) != DISK_VALID)
		    {
		      break;
		    }
		  rcv.pgptr = pgbuf_fix (thread_p, &rcv_vpid, OLD_PAGE,
					 PGBUF_LATCH_WRITE,
					 PGBUF_UNCONDITIONAL_LATCH);
		  if (rcv.pgptr == NULL)
		    {
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
		  if (LSA_LE (&rcv_lsa, rcv_page_lsaptr)
		      && (end_redo_lsa == NULL || LSA_ISNULL (end_redo_lsa)
			  || LSA_LE (rcv_page_lsaptr, end_redo_lsa)))
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

	      rcvindex = run_posp->data.rcvindex;
	      rcv.length = run_posp->length;
	      rcv.offset = run_posp->data.offset;

	      LOG_READ_ADD_ALIGN (thread_p, sizeof (struct log_run_postpone),
				  &log_lsa, log_pgptr);
	      /* GET AFTER DATA */
	      LOG_READ_ALIGN (thread_p, &log_lsa, log_pgptr);

#if !defined(NDEBUG)
	      if (prm_get_bool_value (PRM_ID_LOG_TRACE_DEBUG))
		{
		  fprintf (stdout,
			   "TRACE REDOING[4]: LSA = %lld|%d, Rv_index = %s,\n"
			   "      volid = %d, pageid = %d, offset = %d,\n",
			   (long long int) rcv_lsa.pageid,
			   (int) rcv_lsa.offset,
			   rv_rcvindex_string (rcvindex), rcv_vpid.volid,
			   rcv_vpid.pageid, rcv.offset);
		  if (rcv_page_lsaptr != NULL)
		    {
		      fprintf (stdout, "      page_lsa = %lld|%d\n",
			       (long long int) rcv_page_lsaptr->pageid,
			       rcv_page_lsaptr->offset);
		    }
		  else
		    {
		      fprintf (stdout, "      page_lsa = %lld|%d\n", -1LL,
			       -1);
		    }
		  fflush (stdout);
		}
#endif /* !NDEBUG */

	      log_rv_redo_record (thread_p, &log_lsa,
				  log_pgptr, RV_fun[rcvindex].redofun, &rcv,
				  &rcv_lsa, false, 0, NULL, NULL);

	      if (rcv.pgptr != NULL)
		{
		  pgbuf_unfix (thread_p, rcv.pgptr);
		}
	      break;

	    case LOG_COMPENSATE:
	      LSA_COPY (&rcv_lsa, &log_lsa);

	      /* Get the DATA HEADER */
	      LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_RECORD_HEADER),
				  &log_lsa, log_pgptr);
	      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p,
						sizeof (struct
							log_compensate),
						&log_lsa, log_pgptr);
	      compensate =
		(struct log_compensate *) ((char *) log_pgptr->area +
					   log_lsa.offset);

	      assert (!LOG_IS_VACUUM_DATA_RECOVERY (compensate->data.
						    rcvindex));

	      /* Do we need to redo anything ? */

	      /*
	       * Fetch the page for physical log records and check if redo
	       * is needed by comparing the log sequence numbers
	       */

	      rcv_vpid.volid = compensate->data.volid;
	      rcv_vpid.pageid = compensate->data.pageid;

	      rcv.pgptr = NULL;
	      /* If the page does not exit, there is nothing to redo */
	      if (rcv_vpid.pageid != NULL_PAGEID
		  && rcv_vpid.volid != NULL_VOLID)
		{
		  if (disk_isvalid_page
		      (thread_p, rcv_vpid.volid,
		       rcv_vpid.pageid) != DISK_VALID)
		    {
		      break;
		    }
		  rcv.pgptr = pgbuf_fix (thread_p, &rcv_vpid, OLD_PAGE,
					 PGBUF_LATCH_WRITE,
					 PGBUF_UNCONDITIONAL_LATCH);
		  if (rcv.pgptr == NULL)
		    {
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
		  if (LSA_LE (&rcv_lsa, rcv_page_lsaptr)
		      && (end_redo_lsa == NULL || LSA_ISNULL (end_redo_lsa)
			  || LSA_LE (rcv_page_lsaptr, end_redo_lsa)))
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

	      rcvindex = compensate->data.rcvindex;
	      rcv.length = compensate->length;
	      rcv.offset = compensate->data.offset;

	      LOG_READ_ADD_ALIGN (thread_p, sizeof (struct log_compensate),
				  &log_lsa, log_pgptr);
	      /* GET COMPENSATING DATA */
	      LOG_READ_ALIGN (thread_p, &log_lsa, log_pgptr);
#if !defined(NDEBUG)
	      if (prm_get_bool_value (PRM_ID_LOG_TRACE_DEBUG))
		{
		  fprintf (stdout,
			   "TRACE REDOING[5]: LSA = %lld|%d, Rv_index = %s,\n"
			   "      volid = %d, pageid = %d, offset = %d,\n",
			   (long long int) rcv_lsa.pageid,
			   (int) rcv_lsa.offset,
			   rv_rcvindex_string (rcvindex), rcv_vpid.volid,
			   rcv_vpid.pageid, rcv.offset);
		  if (rcv_page_lsaptr != NULL)
		    {
		      fprintf (stdout, "      page_lsa = %lld|%d\n",
			       (long long int) rcv_page_lsaptr->pageid,
			       rcv_page_lsaptr->offset);
		    }
		  else
		    {
		      fprintf (stdout, "      page_lsa = %lld|%d\n", -1LL,
			       -1);
		    }
		  fflush (stdout);
		}
#endif /* !NDEBUG */

	      log_rv_redo_record (thread_p, &log_lsa,
				  log_pgptr, RV_fun[rcvindex].undofun, &rcv,
				  &rcv_lsa, false, 0, NULL, NULL);
	      if (rcv.pgptr != NULL)
		{
		  pgbuf_unfix (thread_p, rcv.pgptr);
		}
	      break;

	    case LOG_2PC_PREPARE:
	      tran_index = logtb_find_tran_index (thread_p, log_rec->trid);
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
	      LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_RECORD_HEADER),
				  &log_lsa, log_pgptr);

	      if (tdes->state == TRAN_UNACTIVE_2PC_PREPARE)
		{
		  /* The transaction was still prepared_to_commit state at the
		   * time of crash. So, read the global transaction identifier
		   * and list of locks from the log record, and acquire all of
		   * the locks.
		   */

		  log_2pc_read_prepare (thread_p, LOG_2PC_OBTAIN_LOCKS, tdes,
					&log_lsa, log_pgptr);
		}
	      else
		{
		  /* The transaction was not in prepared_to_commit state anymore
		   * at the time of crash. So, there is no need to read the list of
		   * locks from the log record. Read only the global transaction
		   * from the log record.
		   *
		   */
		  log_2pc_read_prepare (thread_p, LOG_2PC_DONT_OBTAIN_LOCKS,
					tdes, &log_lsa, log_pgptr);
		}
	      break;

	    case LOG_2PC_START:
	      tran_index = logtb_find_tran_index (thread_p, log_rec->trid);
	      if (tran_index != NULL_TRAN_INDEX)
		{
		  /* The transaction was still alive at the time of crash. */
		  tdes = LOG_FIND_TDES (tran_index);
		  if (tdes != NULL && LOG_ISTRAN_2PC (tdes))
		    {
		      /* The transaction was still alive at the time of crash. So,
		       * copy the coordinator information from the log record to
		       * the transaction descriptor.
		       */

		      /* Get the DATA HEADER */
		      LOG_READ_ADD_ALIGN (thread_p,
					  sizeof (LOG_RECORD_HEADER),
					  &log_lsa, log_pgptr);

		      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p,
							sizeof (struct
								log_2pc_start),
							&log_lsa, log_pgptr);
		      start_2pc =
			((struct log_2pc_start *) ((char *) log_pgptr->area +
						   log_lsa.offset));

		      /*
		       * Obtain the participant information
		       */
		      logtb_set_client_ids_all (&tdes->client, 0, NULL,
						start_2pc->user_name, NULL,
						NULL, NULL, -1);
		      tdes->gtrid = start_2pc->gtrid;

		      num_particps = start_2pc->num_particps;
		      particp_id_length = start_2pc->particp_id_length;
		      block_particps_ids = malloc (particp_id_length
						   * num_particps);
		      if (block_particps_ids == NULL)
			{
			  /* Out of memory */
			  logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
					     "log_recovery_redo");
			  break;
			}

		      LOG_READ_ADD_ALIGN (thread_p,
					  sizeof (struct log_2pc_start),
					  &log_lsa, log_pgptr);
		      LOG_READ_ALIGN (thread_p, &log_lsa, log_pgptr);

		      /* Read in the participants info. block from the log */
		      logpb_copy_from_log (thread_p,
					   (char *) block_particps_ids,
					   particp_id_length * num_particps,
					   &log_lsa, log_pgptr);

		      /* Initilize the coordinator information */
		      if (log_2pc_alloc_coord_info (tdes, num_particps,
						    particp_id_length,
						    block_particps_ids)
			  == NULL)
			{
			  logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
					     "log_recovery_redo");
			  break;
			}

		      /* Initilize the Acknowledgement vector to 0 since we do not
		       * know what acknowledgments have already been received.
		       * we need to continue reading the log
		       */

		      i = sizeof (int) * tdes->coord->num_particps;
		      if ((tdes->coord->ack_received =
			   (int *) malloc (i)) == NULL)
			{
			  /* Out of memory */
			  logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
					     "log_recovery_redo");
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
	      tran_index = logtb_find_tran_index (thread_p, log_rec->trid);
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
		      if (tdes->coord == NULL
			  || tdes->coord->ack_received == NULL)
			{
			  er_log_debug (ARG_FILE_LINE,
					"log_recovery_redo: SYSTEM ERROR"
					" There is likely an error in the recovery"
					" analysis phase since coordinator information"
					" has not been allocated for transaction = %d"
					" with state = %s", tdes->trid,
					log_state_string (tdes->state));
			  break;
			}
#endif /* CUBRID_DEBUG */

		      /* The transaction was still alive at the time of crash. So,
		       * read the participant index from the log record and set the
		       * acknowledgement flag of that participant.
		       */

		      /* Get the DATA HEADER */
		      LOG_READ_ADD_ALIGN (thread_p,
					  sizeof (LOG_RECORD_HEADER),
					  &log_lsa, log_pgptr);

		      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p,
							sizeof (struct
								log_2pc_particp_ack),
							&log_lsa, log_pgptr);
		      received_ack =
			((struct log_2pc_particp_ack *) ((char *) log_pgptr->
							 area +
							 log_lsa.offset));
		      tdes->coord->ack_received[received_ack->particp_index] =
			true;
		    }
		}
	      break;

	    case LOG_RUN_NEXT_CLIENT_UNDO:
	    case LOG_RUN_NEXT_CLIENT_POSTPONE:
	      /* Client actions are not redone */
	      break;

	    case LOG_COMMIT:
	    case LOG_ABORT:
	      if (stopat != NULL && *stopat != -1)
		{

		  /*
		   * Need to read the donetime record to find out if we need to stop
		   * the recovery at this point.
		   */
		  LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_RECORD_HEADER),
				      &log_lsa, log_pgptr);
		  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p,
						    sizeof (struct
							    log_donetime),
						    &log_lsa, log_pgptr);
		  donetime =
		    (struct log_donetime *) ((char *) log_pgptr->area +
					     log_lsa.offset);
		  if (difftime (*stopat, (time_t) donetime->at_time) < 0)
		    {
		      /*
		       * Stop the recovery process at this point
		       */
		      LSA_SET_NULL (&lsa);
		    }
		}
	      break;

	    case LOG_MVCC_UNDO_DATA:
	      /* Must detect MVCC operations and recover vacuum data buffer.
	       * The found operation is not actually redone/undone, but it
	       * has information that can be used for vacuum.
	       */
	      if (!mvcc_Enabled || log_lsa.pageid <= last_vacuum_data_pageid)
		{
		  /* No need to recover vacuum information from this page */
		  break;
		}

	      LSA_COPY (&rcv_lsa, &log_lsa);
	      LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_RECORD_HEADER),
				  &log_lsa, log_pgptr);
	      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p,
						sizeof (struct log_mvcc_undo),
						&log_lsa, log_pgptr);
	      mvcc_undo =
		(struct log_mvcc_undo *) ((char *) log_pgptr->area +
					  log_lsa.offset);
	      mvccid = mvcc_undo->mvccid;

	      /* Check if MVCC next ID must be updated */
	      if (!mvcc_id_precedes (mvccid, log_Gl.hdr.mvcc_next_id))
		{
		  log_Gl.hdr.mvcc_next_id = mvccid;
		  MVCCID_FORWARD (log_Gl.hdr.mvcc_next_id);
		}

	      /* Recover block data buffer for vacuum */
	      log_recovery_vacuum_data_buffer (thread_p, &rcv_lsa, mvccid);
	      break;

	    case LOG_UNDO_DATA:
	    case LOG_CLIENT_NAME:
	    case LOG_LCOMPENSATE:
	    case LOG_DUMMY_HEAD_POSTPONE:
	    case LOG_POSTPONE:
	    case LOG_CLIENT_USER_UNDO_DATA:
	    case LOG_CLIENT_USER_POSTPONE_DATA:
	    case LOG_WILL_COMMIT:
	    case LOG_COMMIT_WITH_POSTPONE:
	    case LOG_COMMIT_WITH_CLIENT_USER_LOOSE_ENDS:
	    case LOG_ABORT_WITH_CLIENT_USER_LOOSE_ENDS:
	    case LOG_COMMIT_TOPOPE_WITH_POSTPONE:
	    case LOG_COMMIT_TOPOPE_WITH_CLIENT_USER_LOOSE_ENDS:
	    case LOG_ABORT_TOPOPE_WITH_CLIENT_USER_LOOSE_ENDS:
	    case LOG_COMMIT_TOPOPE:
	    case LOG_ABORT_TOPOPE:
	    case LOG_START_CHKPT:
	    case LOG_END_CHKPT:
	    case LOG_SAVEPOINT:
	    case LOG_2PC_COMMIT_DECISION:
	    case LOG_2PC_ABORT_DECISION:
	    case LOG_2PC_COMMIT_INFORM_PARTICPS:
	    case LOG_2PC_ABORT_INFORM_PARTICPS:
	    case LOG_DUMMY_CRASH_RECOVERY:
	    case LOG_DUMMY_FILLPAGE_FORARCHIVE:	/* for backward compatibility */
	    case LOG_REPLICATION_DATA:
	    case LOG_REPLICATION_SCHEMA:
	    case LOG_UNLOCK_COMMIT:
	    case LOG_UNLOCK_ABORT:
	    case LOG_DUMMY_HA_SERVER_STATE:
	    case LOG_DUMMY_OVF_RECORD:
	    case LOG_END_OF_LOG:
	      break;

	    case LOG_SMALLER_LOGREC_TYPE:
	    case LOG_LARGER_LOGREC_TYPE:
	    default:
#if defined(CUBRID_DEBUG)
	      er_log_debug (ARG_FILE_LINE, "log_recovery_redo: Unknown record"
			    " type = %d (%s)... May be a system error",
			    log_rec->type, log_to_string (log_rec->type));
#endif /* CUBRID_DEBUG */
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_PAGE_CORRUPTED,
		      1, log_lsa.pageid);
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
	  if (lsa.offset == NULL_OFFSET && lsa.pageid != NULL_PAGEID
	      && lsa.pageid < log_lsa.pageid)
	    {
	      lsa.pageid = log_lsa.pageid;
	    }
	}
    }

  log_zip_free (undo_unzip_ptr);
  log_zip_free (redo_unzip_ptr);

  if (mvcc_Enabled)
    {
      log_Gl.mvcc_table.highest_completed_mvccid = log_Gl.hdr.mvcc_next_id;
      MVCCID_BACKWARD (log_Gl.mvcc_table.highest_completed_mvccid);

      /* Add any non-duplicate block data to vacuum */
      vacuum_consume_buffer_log_blocks (thread_p, true);
    }

  /* Now finish all postpone operations */
  log_recovery_finish_all_postpone (thread_p);

  /* Flush all dirty pages */
  logpb_flush_pages_direct (thread_p);

  logpb_flush_header (thread_p);
  (void) pgbuf_flush_all (thread_p, NULL_VOLID);

  return;
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
  LOG_TDES *tdes;		/* Transaction descriptor */
  LOG_LSA first_postpone_to_apply;

  /* Finish committing transactions with unfinished postpone actions */

  save_tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);

  for (i = 0; i < log_Gl.trantable.num_total_indices; i++)
    {
      tdes = LOG_FIND_TDES (i);
      if (tdes != NULL && tdes->trid != NULL_TRANID
	  && (tdes->state == TRAN_UNACTIVE_WILL_COMMIT
	      || tdes->state == TRAN_UNACTIVE_COMMITTED_WITH_POSTPONE
	      || tdes->state == TRAN_UNACTIVE_TOPOPE_COMMITTED_WITH_POSTPONE))
	{
	  LSA_SET_NULL (&first_postpone_to_apply);

	  LOG_SET_CURRENT_TRAN_INDEX (thread_p, i);

	  if (tdes->state == TRAN_UNACTIVE_WILL_COMMIT
	      || tdes->state == TRAN_UNACTIVE_COMMITTED_WITH_POSTPONE)
	    {
	      /*
	       * The transaction was the one that was committing
	       */
	      log_recovery_find_first_postpone (thread_p,
						&first_postpone_to_apply,
						&tdes->posp_nxlsa, tdes);

	      log_do_postpone (thread_p, tdes, &first_postpone_to_apply,
			       LOG_COMMIT_WITH_POSTPONE);
	      /*
	       * Are there any postpone client actions that need to be done at the
	       * client machine ?
	       */
	      if (!LSA_ISNULL (&tdes->client_posp_lsa))
		{
		  log_append_commit_client_loose_ends (thread_p, tdes);
		  /*
		   * Now the client transaction manager should request the postpone
		   * actions until all of them become exhausted.
		   */
		}
	      else if (tdes->coord == NULL)
		{		/* If this is a local transaction */
		  (void) log_complete (thread_p, tdes, LOG_COMMIT,
				       LOG_DONT_NEED_NEWTRID);
		  logtb_free_tran_index (thread_p, tdes->tran_index);
		}
	    }
	  else
	    {
	      /*
	       * A top system operation of the transaction was the one that was
	       * committing
	       */
	      log_recovery_find_first_postpone (thread_p,
						&first_postpone_to_apply,
						&tdes->topops.
						stack[tdes->topops.
						      last].posp_lsa, tdes);

	      log_do_postpone (thread_p, tdes,
			       &first_postpone_to_apply,
			       LOG_COMMIT_TOPOPE_WITH_POSTPONE);
	      LSA_SET_NULL (&tdes->topops.stack[tdes->topops.last].posp_lsa);
	      (void) log_end_system_op (thread_p, LOG_RESULT_TOPOP_COMMIT);
	      LSA_COPY (&tdes->undo_nxlsa, &tdes->tail_lsa);
	      if (tdes->topops.last < 0)
		{
		  tdes->state = TRAN_UNACTIVE_UNILATERALLY_ABORTED;
		}
	      /*
	       * The rest of the transaction will be aborted using the recovery undo
	       * process
	       */
	    }
	}
    }

  LOG_SET_CURRENT_TRAN_INDEX (thread_p, save_tran_index);
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
  struct log_undoredo *undoredo = NULL;	/* Undo_redo log record */
  struct log_undo *undo = NULL;	/* Undo log record */
  struct log_mvcc_undoredo *mvcc_undoredo = NULL;	/* MVCC op Undo_redo log record */
  struct log_mvcc_undo *mvcc_undo = NULL;	/* MVCC op Undo log record */
  struct log_compensate *compensate;	/* Compensating log record */
  struct log_logical_compensate *logical_comp;	/* end of a logical undo */
  struct log_topop_result *top_result;	/* Result of top system op */
  LOG_RCVINDEX rcvindex;	/* Recovery index function */
  LOG_RCV rcv;			/* Recovery structure */
  VPID rcv_vpid;		/* VPID of data to recover */
  LOG_LSA rcv_lsa;		/* Address of redo log record */
  LOG_LSA prev_tranlsa;		/* prev LSA of transaction */
  LOG_TDES *tdes;		/* Transaction descriptor */
  int last_tranlogrec;		/* Is this last log record ? */
  int tran_index;
  int data_header_size = 0;
  LOG_ZIP *undo_unzip_ptr = NULL;
  bool is_mvcc_op;

  aligned_log_pgbuf = PTR_ALIGN (log_pgbuf, MAX_ALIGNMENT);

  /*
   * Remove from the list of transaction to abort, those that have finished
   * when the crash happens, so it does not remain dangling in the transaction
   * table.
   */

  for (tran_index = 0;
       tran_index < log_Gl.trantable.num_total_indices; tran_index++)
    {
      if (tran_index != LOG_SYSTEM_TRAN_INDEX)
	{
	  if ((tdes = LOG_FIND_TDES (tran_index)) != NULL
	      && tdes->trid != NULL_TRANID
	      && (tdes->state == TRAN_UNACTIVE_UNILATERALLY_ABORTED
		  || tdes->state == TRAN_UNACTIVE_ABORTED)
	      && LSA_ISNULL (&tdes->undo_nxlsa))
	    {
	      /*
	       * May be nothing to do with this transaction
	       *
	       * Are there any loose ends to be done in the client ?
	       */
	      if (!LSA_ISNULL (&tdes->client_undo_lsa))
		{
		  log_append_abort_client_loose_ends (thread_p, tdes);
		  /*
		   * Now the client transaction manager should request all
		   * client undo actions on the client machine
		   */
		}
	      else
		{
		  (void) log_complete (thread_p, tdes, LOG_ABORT,
				       LOG_DONT_NEED_NEWTRID);
		  logtb_free_tran_index (thread_p, tran_index);
		}
	    }
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
      log_lsa.pageid = lsa_ptr->pageid;
      if (logpb_fetch_page (thread_p, log_lsa.pageid, log_pgptr) == NULL)
	{
	  log_zip_free (undo_unzip_ptr);

	  logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
			     "log_recovery_undo");
	  return;
	}

      /* Check all log records in this phase */
      while (lsa_ptr != NULL && lsa_ptr->pageid == log_lsa.pageid)
	{
	  /* Find the log record */
	  log_lsa.offset = lsa_ptr->offset;
	  log_rec = LOG_GET_LOG_RECORD_HEADER (log_pgptr, &log_lsa);

	  LSA_COPY (&prev_tranlsa, &log_rec->prev_tranlsa);

	  tran_index = logtb_find_tran_index (thread_p, log_rec->trid);
	  if (tran_index == NULL_TRAN_INDEX)
	    {
#if defined(CUBRID_DEBUG)
	      er_log_debug (ARG_FILE_LINE,
			    "log_recovery_undo: SYSTEM ERROR for"
			    " log located at %lld|%d\n",
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
		  er_log_debug (ARG_FILE_LINE,
				"log_recovery_undo: SYSTEM ERROR for"
				" log located at %lld|%d\n",
				(long long int) log_lsa.pageid,
				log_lsa.offset);
#endif /* CUBRID_DEBUG */
		  logtb_free_tran_index_with_undo_lsa (thread_p, lsa_ptr);
		}
	    }

	  if (tran_index != NULL_TRAN_INDEX && tdes != NULL)
	    {
	      LSA_COPY (&tdes->undo_nxlsa, &prev_tranlsa);

	      switch (log_rec->type)
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
		  if (prev_tranlsa.pageid == NULL_PAGEID)
		    {
		      last_tranlogrec = true;
		    }
		  else
		    {
		      last_tranlogrec = false;
		    }

		  if (log_rec->type == LOG_MVCC_UNDOREDO_DATA
		      || log_rec->type == LOG_MVCC_DIFF_UNDOREDO_DATA)
		    {
		      is_mvcc_op = true;
		    }
		  else
		    {
		      is_mvcc_op = false;
		    }

		  /* Get the DATA HEADER */
		  LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_RECORD_HEADER),
				      &log_lsa, log_pgptr);

		  if (is_mvcc_op)
		    {
		      data_header_size = sizeof (struct log_mvcc_undoredo);
		      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p,
							data_header_size,
							&log_lsa, log_pgptr);
		      mvcc_undoredo =
			(struct log_mvcc_undoredo *)
			((char *) log_pgptr->area + log_lsa.offset);

		      /* Get undoredo info */
		      undoredo = &mvcc_undoredo->undoredo;

		      /* Save transaction MVCCID to recovery */
		      rcv.mvcc_id = mvcc_undoredo->mvccid;
		    }
		  else
		    {
		      data_header_size = sizeof (struct log_undoredo);
		      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p,
							data_header_size,
							&log_lsa, log_pgptr);
		      undoredo =
			(struct log_undoredo *) ((char *) log_pgptr->area +
						 log_lsa.offset);

		      rcv.mvcc_id = MVCCID_NULL;
		    }

		  rcvindex = undoredo->data.rcvindex;
		  rcv.length = undoredo->ulength;
		  rcv.offset = undoredo->data.offset;
		  rcv_vpid.volid = undoredo->data.volid;
		  rcv_vpid.pageid = undoredo->data.pageid;

		  LOG_READ_ADD_ALIGN (thread_p, data_header_size, &log_lsa,
				      log_pgptr);

#if !defined(NDEBUG)
		  if (prm_get_bool_value (PRM_ID_LOG_TRACE_DEBUG))
		    {
		      fprintf (stdout,
			       "TRACE UNDOING[1]: LSA = %lld|%d, Rv_index = %s,\n"
			       "      volid = %d, pageid = %d, offset = %d,\n",
			       (long long int) rcv_lsa.pageid,
			       (int) rcv_lsa.offset,
			       rv_rcvindex_string (rcvindex), rcv_vpid.volid,
			       rcv_vpid.pageid, rcv.offset);
		      fflush (stdout);
		    }
#endif /* !NDEBUG */

		  log_rv_undo_record (thread_p, &log_lsa, log_pgptr,
				      rcvindex, &rcv_vpid, &rcv, &rcv_lsa,
				      tdes, undo_unzip_ptr);

		  /* Is this the end of the transaction ? */
		  if (last_tranlogrec == true)
		    {
		      /* Are they any loose ends to be done in the client ? */
		      if (!LSA_ISNULL (&tdes->client_undo_lsa))
			{
			  log_append_abort_client_loose_ends (thread_p, tdes);
			  /*
			   * Now the client transaction manager should request all
			   * client undo actions on the client machine
			   */
			}
		      else
			{
			  if (tdes->mvcc_info != NULL)
			    {
			      /* Clear MVCCID */
			      tdes->mvcc_info->mvcc_id = MVCCID_NULL;
			    }

			  (void) log_complete (thread_p, tdes, LOG_ABORT,
					       LOG_DONT_NEED_NEWTRID);
			  logtb_free_tran_index (thread_p, tran_index);
			  tdes = NULL;
			}
		    }
		  break;

		case LOG_MVCC_UNDO_DATA:
		case LOG_UNDO_DATA:
		  /* Does the record belong to a MVCC op? */
		  is_mvcc_op = log_rec->type == LOG_MVCC_UNDO_DATA;

		  LSA_COPY (&rcv_lsa, &log_lsa);
		  /*
		   * The transaction was active at the time of the crash. The
		   * transaction is unilaterally aborted by the system
		   */
		  if (prev_tranlsa.pageid == NULL_PAGEID)
		    {
		      last_tranlogrec = true;
		    }
		  else
		    {
		      last_tranlogrec = false;
		    }

		  /* Get the DATA HEADER */
		  LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_RECORD_HEADER),
				      &log_lsa, log_pgptr);

		  if (is_mvcc_op)
		    {
		      data_header_size = sizeof (struct log_mvcc_undo);
		      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p,
							data_header_size,
							&log_lsa, log_pgptr);
		      mvcc_undo =
			(struct log_mvcc_undo *) ((char *) log_pgptr->area +
						  log_lsa.offset);

		      /* Get undo info */
		      undo = &mvcc_undo->undo;

		      /* Save transaction MVCCID to recovery */
		      rcv.mvcc_id = mvcc_undo->mvccid;
		    }
		  else
		    {
		      data_header_size = sizeof (struct log_undo);
		      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p,
							data_header_size,
							&log_lsa, log_pgptr);
		      undo =
			(struct log_undo *) ((char *) log_pgptr->area +
					     log_lsa.offset);

		      rcv.mvcc_id = MVCCID_NULL;
		    }

		  rcvindex = undo->data.rcvindex;
		  rcv.length = undo->length;
		  rcv.offset = undo->data.offset;
		  rcv_vpid.volid = undo->data.volid;
		  rcv_vpid.pageid = undo->data.pageid;

		  LOG_READ_ADD_ALIGN (thread_p, data_header_size, &log_lsa,
				      log_pgptr);

#if !defined(NDEBUG)
		  if (prm_get_bool_value (PRM_ID_LOG_TRACE_DEBUG))
		    {
		      fprintf (stdout,
			       "TRACE UNDOING[2]: LSA = %lld|%d, Rv_index = %s,\n"
			       "      volid = %d, pageid = %d, offset = %d,\n",
			       (long long int) rcv_lsa.pageid, rcv_lsa.offset,
			       rv_rcvindex_string (rcvindex), rcv_vpid.volid,
			       rcv_vpid.pageid, rcv.offset);
		      fflush (stdout);
		    }
#endif /* !NDEBUG */
		  log_rv_undo_record (thread_p, &log_lsa, log_pgptr,
				      rcvindex, &rcv_vpid, &rcv, &rcv_lsa,
				      tdes, undo_unzip_ptr);

		  /* Is this the end of the transaction ? */
		  if (last_tranlogrec == true)
		    {
		      /* Are they any loose ends to be done in the client ? */
		      if (!LSA_ISNULL (&tdes->client_undo_lsa))
			{
			  log_append_abort_client_loose_ends (thread_p, tdes);
			  /*
			   * Now the client transaction manager should request all
			   * client undo actions on the client machine
			   */
			}
		      else
			{
			  if (tdes->mvcc_info != NULL)
			    {
			      /* Clear MVCCID */
			      tdes->mvcc_info->mvcc_id = MVCCID_NULL;
			    }

			  (void) log_complete (thread_p, tdes, LOG_ABORT,
					       LOG_DONT_NEED_NEWTRID);
			  logtb_free_tran_index (thread_p, tran_index);
			  tdes = NULL;
			}
		    }
		  break;

		case LOG_CLIENT_NAME:
		case LOG_REDO_DATA:
		case LOG_MVCC_REDO_DATA:
		case LOG_DBEXTERN_REDO_DATA:
		case LOG_DUMMY_HEAD_POSTPONE:
		case LOG_POSTPONE:
		case LOG_CLIENT_USER_UNDO_DATA:
		case LOG_CLIENT_USER_POSTPONE_DATA:
		case LOG_SAVEPOINT:
		case LOG_REPLICATION_DATA:
		case LOG_REPLICATION_SCHEMA:
		case LOG_UNLOCK_COMMIT:
		case LOG_UNLOCK_ABORT:
		case LOG_DUMMY_HA_SERVER_STATE:
		case LOG_DUMMY_OVF_RECORD:
		  /* Not for UNDO .. Go to previous record */

		  /* Is this the end of the transaction ? */
		  if (LSA_ISNULL (&prev_tranlsa))
		    {
		      /* Are there any loose ends to be done in the client ? */
		      if (!LSA_ISNULL (&tdes->client_undo_lsa))
			{
			  log_append_abort_client_loose_ends (thread_p, tdes);
			  /*
			   * Now the client transaction manager should request all
			   * client undo actions on the client machine
			   */
			}
		      else
			{
			  if (tdes->mvcc_info != NULL)
			    {
			      /* Clear MVCCID */
			      tdes->mvcc_info->mvcc_id = MVCCID_NULL;
			    }

			  (void) log_complete (thread_p, tdes, LOG_ABORT,
					       LOG_DONT_NEED_NEWTRID);
			  logtb_free_tran_index (thread_p, tran_index);
			  tdes = NULL;
			}
		    }
		  break;

		case LOG_COMPENSATE:
		  /* Only for REDO .. Go to next undo record
		   * Need to read the compensating record to set the next
		   * undo address
		   */

		  /* Get the DATA HEADER */
		  LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_RECORD_HEADER),
				      &log_lsa, log_pgptr);
		  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p,
						    sizeof (struct
							    log_compensate),
						    &log_lsa, log_pgptr);
		  compensate =
		    (struct log_compensate *) ((char *) log_pgptr->area +
					       log_lsa.offset);
		  /* Is this the end of the transaction ? */
		  if (LSA_ISNULL (&compensate->undo_nxlsa))
		    {
		      /* Are there any loose ends to be done in the client ? */
		      if (!LSA_ISNULL (&tdes->client_undo_lsa))
			{
			  log_append_abort_client_loose_ends (thread_p, tdes);
			  /*
			   * Now the client transaction manager should request all
			   * client undo actions on the client machine
			   */
			}
		      else
			{
			  if (tdes->mvcc_info != NULL)
			    {
			      /* Clear MVCCID */
			      tdes->mvcc_info->mvcc_id = MVCCID_NULL;
			    }

			  (void) log_complete (thread_p, tdes, LOG_ABORT,
					       LOG_DONT_NEED_NEWTRID);
			  logtb_free_tran_index (thread_p, tran_index);
			  tdes = NULL;
			}
		    }
		  else
		    {
		      LSA_COPY (&prev_tranlsa, &compensate->undo_nxlsa);
		    }
		  break;

		case LOG_LCOMPENSATE:
		  /* Only for REDO .. Go to next undo record
		   * Need to read the compensating record to set the next
		   * undo address
		   */

		  /* Get the DATA HEADER */
		  LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_RECORD_HEADER),
				      &log_lsa, log_pgptr);
		  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p,
						    sizeof (struct
							    log_logical_compensate),
						    &log_lsa, log_pgptr);
		  logical_comp =
		    ((struct log_logical_compensate *) ((char *) log_pgptr->
							area +
							log_lsa.offset));

		  /* Is this the end of the transaction ? */
		  if (LSA_ISNULL (&logical_comp->undo_nxlsa))
		    {
		      /* Are there any loose ends to be done in the client ? */
		      if (!LSA_ISNULL (&tdes->client_undo_lsa))
			{
			  log_append_abort_client_loose_ends (thread_p, tdes);
			  /*
			   * Now the client transaction manager should request all
			   * client undo actions on the client machine
			   */
			}
		      else
			{
			  if (tdes->mvcc_info != NULL)
			    {
			      /* Clear MVCCID */
			      tdes->mvcc_info->mvcc_id = MVCCID_NULL;
			    }

			  (void) log_complete (thread_p, tdes, LOG_ABORT,
					       LOG_DONT_NEED_NEWTRID);
			  logtb_free_tran_index (thread_p, tran_index);
			  tdes = NULL;
			}
		    }
		  else
		    {
		      LSA_COPY (&prev_tranlsa, &logical_comp->undo_nxlsa);
		    }
		  break;

		case LOG_COMMIT_TOPOPE:
		case LOG_ABORT_TOPOPE:
		  /*
		   * We found a system top operation that should be skipped from
		   * rollback
		   */

		  /* Read the DATA HEADER */
		  LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_RECORD_HEADER),
				      &log_lsa, log_pgptr);
		  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p,
						    sizeof (struct
							    log_topop_result),
						    &log_lsa, log_pgptr);
		  top_result =
		    ((struct log_topop_result *) ((char *) log_pgptr->area +
						  log_lsa.offset));
		  /* Is this the end of the transaction ? */
		  if (LSA_ISNULL (&top_result->lastparent_lsa))
		    {
		      /* Are there any loose ends to be done in the client ? */
		      if (!LSA_ISNULL (&tdes->client_undo_lsa))
			{
			  log_append_abort_client_loose_ends (thread_p, tdes);
			  /*
			   * Now the client transaction manager should request all
			   * client undo actions on the client machine
			   */
			}
		      else
			{
			  if (tdes->mvcc_info != NULL)
			    {
			      /* Clear MVCCID */
			      tdes->mvcc_info->mvcc_id = MVCCID_NULL;
			    }

			  (void) log_complete (thread_p, tdes, LOG_ABORT,
					       LOG_DONT_NEED_NEWTRID);
			  logtb_free_tran_index (thread_p, tran_index);
			  tdes = NULL;
			}
		    }
		  else
		    {
		      LSA_COPY (&prev_tranlsa, &top_result->lastparent_lsa);
		    }
		  break;

		case LOG_RUN_POSTPONE:
		case LOG_RUN_NEXT_CLIENT_POSTPONE:
		case LOG_RUN_NEXT_CLIENT_UNDO:
		case LOG_WILL_COMMIT:
		case LOG_COMMIT_WITH_POSTPONE:
		case LOG_COMMIT_WITH_CLIENT_USER_LOOSE_ENDS:
		case LOG_COMMIT:
		case LOG_COMMIT_TOPOPE_WITH_POSTPONE:
		case LOG_COMMIT_TOPOPE_WITH_CLIENT_USER_LOOSE_ENDS:
		case LOG_ABORT_TOPOPE_WITH_CLIENT_USER_LOOSE_ENDS:
		case LOG_ABORT_WITH_CLIENT_USER_LOOSE_ENDS:
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
		case LOG_DUMMY_FILLPAGE_FORARCHIVE:	/* for backward compatibility */
		case LOG_END_OF_LOG:
		  /* This looks like a system error in the analysis phase */
#if defined(CUBRID_DEBUG)
		  er_log_debug (ARG_FILE_LINE,
				"log_recovery_undo: SYSTEM ERROR for"
				" log located at %lld|%d,"
				" Bad log_rectype = %d\n (%s).\n",
				(long long int) log_lsa.pageid,
				log_lsa.offset, log_rec->type,
				log_to_string (log_rec->type));
#endif /* CUBRID_DEBUG */
		  /* Remove the transaction from the recovery process */
		  if (tdes->mvcc_info != NULL)
		    {
		      /* Clear MVCCID */
		      tdes->mvcc_info->mvcc_id = MVCCID_NULL;
		    }

		  (void) log_complete (thread_p, tdes, LOG_ABORT,
				       LOG_DONT_NEED_NEWTRID);
		  logtb_free_tran_index (thread_p, tran_index);
		  tdes = NULL;
		  break;

		case LOG_SMALLER_LOGREC_TYPE:
		case LOG_LARGER_LOGREC_TYPE:
		default:
#if defined(CUBRID_DEBUG)
		  er_log_debug (ARG_FILE_LINE,
				"log_recovery_undo: Unknown record"
				" type = %d (%s)\n ... May be a system error",
				log_rec->type, log_to_string (log_rec->type));
#endif /* CUBRID_DEBUG */
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			  ER_LOG_PAGE_CORRUPTED, 1, log_lsa.pageid);

		  /*
		   * Remove the transaction from the recovery process
		   */
		  if (tdes->mvcc_info != NULL)
		    {
		      /* Clear MVCCID */
		      tdes->mvcc_info->mvcc_id = MVCCID_NULL;
		    }

		  (void) log_complete (thread_p, tdes, LOG_ABORT,
				       LOG_DONT_NEED_NEWTRID);
		  logtb_free_tran_index (thread_p, tran_index);
		  tdes = NULL;
		  break;
		}

	      /* Just in case, it was changed */
	      if (tdes != NULL)
		{
		  LSA_COPY (&tdes->undo_nxlsa, &prev_tranlsa);
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
log_recovery_notpartof_archives (THREAD_ENTRY * thread_p, int start_arv_num,
				 const char *info_reason)
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
	  fileio_make_log_archive_name (logarv_name, log_Archive_path,
					log_Prefix, i);
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
	  fileio_make_log_archive_name (logarv_name, log_Archive_path,
					log_Prefix, i);
	  if (fileio_is_volume_exist (logarv_name) == false)
	    {
	      if (i > start_arv_num)
		{
		  fileio_make_log_archive_name (logarv_name, log_Archive_path,
						log_Prefix, i - 1);
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
	    catmsg = msgcat_message (MSGCAT_CATALOG_CUBRID,
				     MSGCAT_SET_LOG,
				     MSGCAT_LOG_LOGINFO_REMOVE_REASON);
	    if (catmsg == NULL)
	      {
		catmsg = "REMOVE: %d %s to \n%d %s.\nREASON: %s\n";
	      }
	    error_code = log_dump_log_info (log_Name_info, true, catmsg,
					    start_arv_num, logarv_name,
					    start_arv_num, logarv_name,
					    info_reason);
	    if (error_code != NO_ERROR && error_code != ER_LOG_MOUNT_FAIL)
	      {
		return;
	      }
	  }
	else
	  {
	    fileio_make_log_archive_name (logarv_name_first, log_Archive_path,
					  log_Prefix, start_arv_num);

	    catmsg = msgcat_message (MSGCAT_CATALOG_CUBRID,
				     MSGCAT_SET_LOG,
				     MSGCAT_LOG_LOGINFO_REMOVE_REASON);
	    if (catmsg == NULL)
	      {
		catmsg = "REMOVE: %d %s to \n%d %s.\nREASON: %s\n";
	      }
	    error_code = log_dump_log_info (log_Name_info, true, catmsg,
					    start_arv_num,
					    logarv_name_first, i - 1,
					    logarv_name, info_reason);
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
log_unformat_ahead_volumes (THREAD_ENTRY * thread_p, VOLID volid,
			    VOLID * start_volid)
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

  (void) fileio_map_mounted (thread_p,
			     (bool (*)(THREAD_ENTRY *, VOLID, void *))
			     log_unformat_ahead_volumes, &start_volid);

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

      vdes =
	fileio_mount (thread_p, log_Db_fullname, vol_fullname, volid, false,
		      false);
      if (vdes != NULL_VOLDES)
	{
	  ret = disk_get_creation_time (thread_p, volid, &vol_dbcreation);
	  fileio_dismount (thread_p, vdes);
	  if (difftime ((time_t) vol_dbcreation,
			(time_t) log_Gl.hdr.db_creation) != 0)
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
log_recovery_resetlog (THREAD_ENTRY * thread_p, LOG_LSA * new_append_lsa,
		       bool is_new_append_page, LOG_LSA * last_lsa)
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
	  || (log_Gl.hdr.fpageid > new_append_lsa->pageid
	      && new_append_lsa->offset > 0))
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

	  if ((logpb_fetch_page (thread_p, new_append_lsa->pageid,
				 newappend_pgptr)) == NULL)
	    {
	      newappend_pgptr = NULL;
	    }
	}
      LOG_RESET_APPEND_LSA (new_append_lsa);
    }

  LSA_COPY (&log_Gl.hdr.chkpt_lsa, &log_Gl.hdr.append_lsa);
  log_Gl.hdr.is_shutdown = false;

  logpb_invalidate_pool (thread_p);

  if (log_Gl.append.vdes == NULL_VOLDES
      || log_Gl.hdr.fpageid > log_Gl.hdr.append_lsa.pageid)
    {
      LOG_PAGE *loghdr_pgptr = NULL;
      LOG_PAGE *append_pgptr = NULL;

      /*
       * Don't have the log active, or we are going to the past
       */
      arv_num = logpb_get_archive_number (thread_p,
					  log_Gl.hdr.append_lsa.pageid - 1);
      if (arv_num == -1)
	{
	  logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
			     "log_recovery_resetlog");
	  return;
	}
      arv_num = arv_num + 1;

      catmsg = msgcat_message (MSGCAT_CATALOG_CUBRID,
			       MSGCAT_SET_LOG,
			       MSGCAT_LOG_RESETLOG_DUE_INCOMPLTE_MEDIA_RECOVERY);
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
      log_Gl.hdr.nxarv_phy_pageid =
	logpb_to_physical_pageid (log_Gl.hdr.nxarv_pageid);
      log_Gl.hdr.nxarv_num = arv_num;
      log_Gl.hdr.last_arv_num_for_syscrashes = -1;
      log_Gl.hdr.last_deleted_arv_num = -1;

      if (log_Gl.append.vdes == NULL_VOLDES)
	{
	  /* Create the log active since we do not have one */
	  ret = disk_get_creation_time (thread_p, LOG_DBFIRST_VOLID,
					&log_Gl.hdr.db_creation);
	  if (ret != NO_ERROR)
	    {
	      logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
				 "log_recovery_resetlog");
	      return;
	    }

	  log_Gl.append.vdes = fileio_format (thread_p, log_Db_fullname,
					      log_Name_active,
					      LOG_DBLOG_ACTIVE_VOLID,
					      log_get_num_pages_for_creation
					      (-1), false, true, false,
					      LOG_PAGESIZE, 0, false);

	  if (log_Gl.append.vdes != NULL_VOLDES)
	    {
	      loghdr_pgptr = logpb_create_header_page (thread_p);
	    }

	  if (log_Gl.append.vdes == NULL_VOLDES || loghdr_pgptr == NULL)
	    {
	      if (loghdr_pgptr != NULL)
		{
		  logpb_free_page (thread_p, loghdr_pgptr);
		}
	      logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
				 "log_recovery_resetlog");
	      return;
	    }

	  /*
	   * Flush the header page and first append page so that we can record
	   * the header page on it
	   */
	  if (logpb_flush_page (thread_p, loghdr_pgptr, FREE) != NO_ERROR)
	    {
	      logpb_fatal_error (thread_p, false, ARG_FILE_LINE,
				 "log_recovery_resetlog");
	    }
	}

      append_pgptr = logpb_create (thread_p, log_Gl.hdr.fpageid);
      if (append_pgptr == NULL)
	{
	  logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
			     "log_recovery_resetlog");
	  return;
	}
      if (logpb_flush_page (thread_p, append_pgptr, FREE) != NO_ERROR)
	{
	  logpb_fatal_error (thread_p, false, ARG_FILE_LINE,
			     "log_recovery_resetlog");
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
	  log_Gl.hdr.nxarv_phy_pageid =
	    logpb_to_physical_pageid (log_Gl.hdr.nxarv_pageid);
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
	  logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
			     "log_recovery_resetlog");
	  return;
	}
    }
  else
    {
      if (logpb_fetch_start_append_page (thread_p) != NULL)
	{
	  if (newappend_pgptr != NULL && log_Gl.append.log_pgptr != NULL)
	    {
	      memcpy ((char *) log_Gl.append.log_pgptr,
		      (char *) newappend_pgptr, LOG_PAGESIZE);
	      logpb_set_dirty (thread_p, log_Gl.append.log_pgptr, DONT_FREE);
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
log_startof_nxrec (THREAD_ENTRY * thread_p, LOG_LSA * lsa,
		   bool canuse_forwaddr)
{
  char log_pgbuf[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT], *aligned_log_pgbuf;
  LOG_PAGE *log_pgptr = NULL;	/* Log page pointer where
				 * LSA is located
				 */
  LOG_LSA log_lsa;
  LOG_RECTYPE type;		/* Log record type */
  LOG_RECORD_HEADER *log_rec;	/* Pointer to log record */
  struct log_undoredo *undoredo;	/* Undo_redo log record */
  struct log_undo *undo;	/* Undo log record */
  struct log_redo *redo;	/* Redo log record */
  struct log_mvcc_undoredo *mvcc_undoredo;	/* MVCC op undo_redo log record */
  struct log_mvcc_undo *mvcc_undo;	/* MVCC op undo log record */
  struct log_mvcc_redo *mvcc_redo;	/* MVCC op redo log record */
  struct log_dbout_redo *dbout_redo;	/* A external redo log record */
  struct log_savept *savept;	/* A savepoint log record */
  struct log_compensate *compensate;	/* Compensating log record */
  struct log_client *client_postpone;	/* Client postpone log record */
  struct log_client *client_undo;	/* Client undo log record */
  struct log_run_postpone *run_posp;	/* A run postpone action */
  struct log_chkpt *chkpt;	/* Checkpoint log record */
  struct log_2pc_start *start_2pc;	/* A 2PC start log record */
  struct log_2pc_prepcommit *prepared;	/* A 2PC prepare to commit */
  struct log_replication *repl_log;

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

  if (logpb_fetch_page (thread_p, lsa->pageid, log_pgptr) == NULL)
    {
      fprintf (stdout, " Error reading page %lld... Quit\n",
	       (long long int) lsa->pageid);
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

  LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_RECORD_HEADER), &log_lsa,
		      log_pgptr);
  switch (type)
    {
    case LOG_CLIENT_NAME:
      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, LOG_USERNAME_MAX,
					&log_lsa, log_pgptr);

      LOG_READ_ADD_ALIGN (thread_p, LOG_USERNAME_MAX, &log_lsa, log_pgptr);
      break;

    case LOG_UNDOREDO_DATA:
    case LOG_DIFF_UNDOREDO_DATA:
      /* Read the DATA HEADER */
      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p,
					sizeof (struct log_undoredo),
					&log_lsa, log_pgptr);
      undoredo =
	(struct log_undoredo *) ((char *) log_pgptr->area + log_lsa.offset);

      undo_length = (int) GET_ZIP_LEN (undoredo->ulength);
      redo_length = (int) GET_ZIP_LEN (undoredo->rlength);

      LOG_READ_ADD_ALIGN (thread_p, sizeof (struct log_undoredo), &log_lsa,
			  log_pgptr);
      LOG_READ_ADD_ALIGN (thread_p, undo_length, &log_lsa, log_pgptr);
      LOG_READ_ADD_ALIGN (thread_p, redo_length, &log_lsa, log_pgptr);
      break;

    case LOG_MVCC_UNDOREDO_DATA:
    case LOG_MVCC_DIFF_UNDOREDO_DATA:
      /* Read the DATA HEADER */
      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p,
					sizeof (struct log_mvcc_undoredo),
					&log_lsa, log_pgptr);
      mvcc_undoredo =
	(struct log_mvcc_undoredo *) ((char *) log_pgptr->area +
				      log_lsa.offset);

      undo_length = (int) GET_ZIP_LEN (mvcc_undoredo->undoredo.ulength);
      redo_length = (int) GET_ZIP_LEN (mvcc_undoredo->undoredo.rlength);

      LOG_READ_ADD_ALIGN (thread_p, sizeof (struct log_mvcc_undoredo),
			  &log_lsa, log_pgptr);
      LOG_READ_ADD_ALIGN (thread_p, undo_length, &log_lsa, log_pgptr);
      LOG_READ_ADD_ALIGN (thread_p, redo_length, &log_lsa, log_pgptr);
      break;

    case LOG_UNDO_DATA:
      /* Read the DATA HEADER */
      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (struct log_undo),
					&log_lsa, log_pgptr);
      undo = (struct log_undo *) ((char *) log_pgptr->area + log_lsa.offset);

      undo_length = (int) GET_ZIP_LEN (undo->length);

      LOG_READ_ADD_ALIGN (thread_p, sizeof (struct log_undo), &log_lsa,
			  log_pgptr);
      LOG_READ_ADD_ALIGN (thread_p, undo_length, &log_lsa, log_pgptr);
      break;

    case LOG_MVCC_UNDO_DATA:
      /* Read the DATA HEADER */
      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p,
					sizeof (struct log_mvcc_undo),
					&log_lsa, log_pgptr);
      mvcc_undo =
	(struct log_mvcc_undo *) ((char *) log_pgptr->area + log_lsa.offset);

      undo_length = (int) GET_ZIP_LEN (mvcc_undo->undo.length);

      LOG_READ_ADD_ALIGN (thread_p, sizeof (struct log_mvcc_undo), &log_lsa,
			  log_pgptr);
      LOG_READ_ADD_ALIGN (thread_p, undo_length, &log_lsa, log_pgptr);
      break;

    case LOG_MVCC_REDO_DATA:
      /* Read the DATA HEADER */
      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p,
					sizeof (struct log_mvcc_redo),
					&log_lsa, log_pgptr);
      mvcc_redo =
	(struct log_mvcc_redo *) ((char *) log_pgptr->area + log_lsa.offset);
      redo_length = (int) GET_ZIP_LEN (mvcc_redo->redo.length);

      LOG_READ_ADD_ALIGN (thread_p, sizeof (struct log_mvcc_redo), &log_lsa,
			  log_pgptr);
      LOG_READ_ADD_ALIGN (thread_p, redo_length, &log_lsa, log_pgptr);
      break;

    case LOG_REDO_DATA:
    case LOG_POSTPONE:
      /* Read the DATA HEADER */
      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (struct log_redo),
					&log_lsa, log_pgptr);
      redo = (struct log_redo *) ((char *) log_pgptr->area + log_lsa.offset);
      redo_length = (int) GET_ZIP_LEN (redo->length);

      LOG_READ_ADD_ALIGN (thread_p, sizeof (struct log_redo), &log_lsa,
			  log_pgptr);
      LOG_READ_ADD_ALIGN (thread_p, redo_length, &log_lsa, log_pgptr);
      break;

    case LOG_RUN_POSTPONE:
      /* Read the DATA HEADER */
      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p,
					sizeof (struct log_run_postpone),
					&log_lsa, log_pgptr);
      run_posp =
	(struct log_run_postpone *) ((char *) log_pgptr->area +
				     log_lsa.offset);
      redo_length = run_posp->length;

      LOG_READ_ADD_ALIGN (thread_p, sizeof (struct log_run_postpone),
			  &log_lsa, log_pgptr);
      LOG_READ_ADD_ALIGN (thread_p, redo_length, &log_lsa, log_pgptr);
      break;

    case LOG_DBEXTERN_REDO_DATA:
      /* Read the data header */
      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p,
					sizeof (struct log_dbout_redo),
					&log_lsa, log_pgptr);
      dbout_redo =
	((struct log_dbout_redo *) ((char *) log_pgptr->area +
				    log_lsa.offset));
      redo_length = dbout_redo->length;

      LOG_READ_ADD_ALIGN (thread_p, sizeof (struct log_dbout_redo),
			  &log_lsa, log_pgptr);
      LOG_READ_ADD_ALIGN (thread_p, redo_length, &log_lsa, log_pgptr);
      break;

    case LOG_COMPENSATE:
      /* Read the DATA HEADER */
      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p,
					sizeof (struct log_compensate),
					&log_lsa, log_pgptr);
      compensate =
	(struct log_compensate *) ((char *) log_pgptr->area + log_lsa.offset);
      redo_length = compensate->length;

      LOG_READ_ADD_ALIGN (thread_p, sizeof (struct log_compensate),
			  &log_lsa, log_pgptr);
      LOG_READ_ADD_ALIGN (thread_p, redo_length, &log_lsa, log_pgptr);
      break;

    case LOG_LCOMPENSATE:
      /* Read the DATA HEADER */
      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p,
					sizeof (struct
						log_logical_compensate),
					&log_lsa, log_pgptr);

      LOG_READ_ADD_ALIGN (thread_p, sizeof (struct log_logical_compensate),
			  &log_lsa, log_pgptr);
      break;

    case LOG_CLIENT_USER_UNDO_DATA:
      /* Read the DATA HEADER */
      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p,
					sizeof (struct log_client),
					&log_lsa, log_pgptr);
      client_undo =
	(struct log_client *) ((char *) log_pgptr->area + log_lsa.offset);
      undo_length = client_undo->length;

      LOG_READ_ADD_ALIGN (thread_p, sizeof (struct log_client), &log_lsa,
			  log_pgptr);
      LOG_READ_ADD_ALIGN (thread_p, undo_length, &log_lsa, log_pgptr);
      break;

    case LOG_CLIENT_USER_POSTPONE_DATA:
      /* Read the DATA HEADER */
      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p,
					sizeof (struct log_client),
					&log_lsa, log_pgptr);
      client_postpone =
	(struct log_client *) ((char *) log_pgptr->area + log_lsa.offset);
      redo_length = client_postpone->length;

      LOG_READ_ADD_ALIGN (thread_p, sizeof (struct log_client), &log_lsa,
			  log_pgptr);
      LOG_READ_ADD_ALIGN (thread_p, redo_length, &log_lsa, log_pgptr);
      break;

    case LOG_RUN_NEXT_CLIENT_UNDO:
    case LOG_RUN_NEXT_CLIENT_POSTPONE:
      /* Read the DATA HEADER */
      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p,
					sizeof (struct log_run_client),
					&log_lsa, log_pgptr);

      LOG_READ_ADD_ALIGN (thread_p, sizeof (struct log_run_client),
			  &log_lsa, log_pgptr);
      break;

    case LOG_COMMIT_WITH_POSTPONE:
      /* Read the DATA HEADER */
      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p,
					sizeof (struct log_start_postpone),
					&log_lsa, log_pgptr);

      LOG_READ_ADD_ALIGN (thread_p, sizeof (struct log_start_postpone),
			  &log_lsa, log_pgptr);
      break;

    case LOG_COMMIT_WITH_CLIENT_USER_LOOSE_ENDS:
    case LOG_ABORT_WITH_CLIENT_USER_LOOSE_ENDS:
      /* Read the DATA HEADER */
      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p,
					sizeof (struct log_start_client),
					&log_lsa, log_pgptr);

      LOG_READ_ADD_ALIGN (thread_p, sizeof (struct log_start_client),
			  &log_lsa, log_pgptr);
      break;

    case LOG_COMMIT:
    case LOG_ABORT:
      /* Read the DATA HEADER */
      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p,
					sizeof (struct log_donetime),
					&log_lsa, log_pgptr);

      LOG_READ_ADD_ALIGN (thread_p, sizeof (struct log_donetime), &log_lsa,
			  log_pgptr);
      break;

    case LOG_COMMIT_TOPOPE_WITH_POSTPONE:
      /* Read the DATA HEADER */
      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p,
					sizeof (struct
						log_topope_start_postpone),
					&log_lsa, log_pgptr);

      LOG_READ_ADD_ALIGN (thread_p,
			  sizeof (struct log_topope_start_postpone),
			  &log_lsa, log_pgptr);
      break;

    case LOG_COMMIT_TOPOPE_WITH_CLIENT_USER_LOOSE_ENDS:
    case LOG_ABORT_TOPOPE_WITH_CLIENT_USER_LOOSE_ENDS:
      /* Read the DATA HEADER */
      LOG_READ_ADVANCE_WHEN_DOESNT_FIT
	(thread_p, sizeof (struct log_topope_start_client), &log_lsa,
	 log_pgptr);

      LOG_READ_ADD_ALIGN (thread_p, sizeof (struct log_topope_start_client),
			  &log_lsa, log_pgptr);
      break;

    case LOG_COMMIT_TOPOPE:
    case LOG_ABORT_TOPOPE:
      /* Read the DATA HEADER */
      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p,
					sizeof (struct log_topop_result),
					&log_lsa, log_pgptr);

      LOG_READ_ADD_ALIGN (thread_p, sizeof (struct log_topop_result),
			  &log_lsa, log_pgptr);
      break;

    case LOG_END_CHKPT:
      /* Read the DATA HEADER */
      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (struct log_chkpt),
					&log_lsa, log_pgptr);
      chkpt =
	(struct log_chkpt *) ((char *) log_pgptr->area + log_lsa.offset);
      undo_length = sizeof (struct log_chkpt_trans) * chkpt->ntrans;
      redo_length = (sizeof (struct log_chkpt_topops_commit_posp) *
		     chkpt->ntops);

      LOG_READ_ADD_ALIGN (thread_p, sizeof (struct log_chkpt), &log_lsa,
			  log_pgptr);
      LOG_READ_ADD_ALIGN (thread_p, undo_length, &log_lsa, log_pgptr);
      if (redo_length > 0)
	LOG_READ_ADD_ALIGN (thread_p, redo_length, &log_lsa, log_pgptr);
      break;

    case LOG_SAVEPOINT:
      /* Read the DATA HEADER */
      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p,
					sizeof (struct log_savept),
					&log_lsa, log_pgptr);
      savept =
	(struct log_savept *) ((char *) log_pgptr->area + log_lsa.offset);
      undo_length = savept->length;

      LOG_READ_ADD_ALIGN (thread_p, sizeof (struct log_savept), &log_lsa,
			  log_pgptr);
      LOG_READ_ADD_ALIGN (thread_p, undo_length, &log_lsa, log_pgptr);
      break;

    case LOG_2PC_PREPARE:
      /* Get the DATA HEADER */
      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p,
					sizeof (struct log_2pc_prepcommit),
					&log_lsa, log_pgptr);
      prepared =
	(struct log_2pc_prepcommit *) ((char *) log_pgptr->area +
				       log_lsa.offset);
      nobj_locks = prepared->num_object_locks;
      /* ignore npage_locks */

      LOG_READ_ADD_ALIGN (thread_p, sizeof (struct log_2pc_prepcommit),
			  &log_lsa, log_pgptr);

      if (prepared->gtrinfo_length > 0)
	{
	  LOG_READ_ADD_ALIGN (thread_p, prepared->gtrinfo_length, &log_lsa,
			      log_pgptr);
	}

      if (nobj_locks > 0)
	{
	  size = nobj_locks * sizeof (LK_ACQOBJ_LOCK);
	  LOG_READ_ADD_ALIGN (thread_p, (INT16) size, &log_lsa, log_pgptr);
	}
      break;

    case LOG_2PC_START:
      /* Get the DATA HEADER */
      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p,
					sizeof (struct log_2pc_start),
					&log_lsa, log_pgptr);
      start_2pc =
	(struct log_2pc_start *) ((char *) log_pgptr->area + log_lsa.offset);

      LOG_READ_ADD_ALIGN (thread_p, sizeof (struct log_2pc_start), &log_lsa,
			  log_pgptr);
      LOG_READ_ADD_ALIGN (thread_p,
			  (start_2pc->particp_id_length *
			   start_2pc->num_particps), &log_lsa, log_pgptr);
      break;

    case LOG_2PC_RECV_ACK:
      /* Get the DATA HEADER */
      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p,
					sizeof (struct log_2pc_particp_ack),
					&log_lsa, log_pgptr);

      LOG_READ_ADD_ALIGN (thread_p, sizeof (struct log_2pc_particp_ack),
			  &log_lsa, log_pgptr);
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
    case LOG_UNLOCK_COMMIT:
    case LOG_UNLOCK_ABORT:
    case LOG_END_OF_LOG:
      break;

    case LOG_REPLICATION_DATA:
    case LOG_REPLICATION_SCHEMA:
      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p,
					sizeof (struct log_replication),
					&log_lsa, log_pgptr);

      repl_log = (struct log_replication *) ((char *) log_pgptr->area
					     + log_lsa.offset);
      repl_log_length = (int) GET_ZIP_LEN (repl_log->length);

      LOG_READ_ADD_ALIGN (thread_p, sizeof (struct log_replication),
			  &log_lsa, log_pgptr);
      LOG_READ_ADD_ALIGN (thread_p, repl_log_length, &log_lsa, log_pgptr);
      break;

    case LOG_DUMMY_HA_SERVER_STATE:
      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p,
					sizeof (struct log_ha_server_state),
					&log_lsa, log_pgptr);
      LOG_READ_ADD_ALIGN (thread_p, sizeof (struct log_ha_server_state),
			  &log_lsa, log_pgptr);
      break;

    case LOG_DUMMY_FILLPAGE_FORARCHIVE:	/* for backward compatibility */
      /* Get to start of next page */
      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, LOG_PAGESIZE, &log_lsa,
					log_pgptr);
      break;

    case LOG_SMALLER_LOGREC_TYPE:
    case LOG_LARGER_LOGREC_TYPE:
    default:
      break;
    }

  /* Make sure you point to beginning of a next record */
  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (LOG_RECORD_HEADER),
				    &log_lsa, log_pgptr);

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
 *   pospone_type(in):
 *
 */
static int
log_recovery_find_first_postpone (THREAD_ENTRY * thread_p,
				  LOG_LSA * ret_lsa,
				  LOG_LSA * start_postpone_lsa,
				  LOG_TDES * tdes)
{
  LOG_LSA end_postpone_lsa;
  LOG_LSA start_seek_lsa;
  LOG_LSA *end_seek_lsa;
  LOG_LSA next_start_seek_lsa;
  LOG_LSA log_lsa;
  LOG_LSA forward_lsa;
  LOG_LSA next_postpone_lsa;
  struct log_run_postpone *run_posp;

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

  assert (ret_lsa && start_postpone_lsa && tdes);

  LSA_SET_NULL (ret_lsa);

  if (log_is_in_crash_recovery () == false
      || (tdes->state != TRAN_UNACTIVE_WILL_COMMIT
	  && tdes->state != TRAN_UNACTIVE_COMMITTED_WITH_POSTPONE
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
  nxtop_count = log_get_next_nested_top (thread_p, tdes, start_postpone_lsa,
					 &nxtop_stack);

  while (!LSA_ISNULL (&next_start_seek_lsa))
    {
      LSA_COPY (&start_seek_lsa, &next_start_seek_lsa);

      if (nxtop_count > 0)
	{
	  /* This is not possible (top op cannot be nested),
	   * because log_is_in_crash_recovery.
	   */
	  assert (0);

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
	  log_lsa.pageid = forward_lsa.pageid;
	  if (logpb_fetch_page (thread_p, log_lsa.pageid, log_pgptr) == NULL)
	    {
	      logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
				 "log_recovery_find_first_postpone");
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

	      if (forward_lsa.pageid == NULL_PAGEID
		  && logpb_is_page_in_archive (log_lsa.pageid))
		{
		  forward_lsa.pageid = log_lsa.pageid + 1;
		}

	      if (log_rec->trid == tdes->trid)
		{
		  switch (log_rec->type)
		    {
		    case LOG_RUN_POSTPONE:
		      LOG_READ_ADD_ALIGN (thread_p,
					  sizeof (LOG_RECORD_HEADER),
					  &log_lsa, log_pgptr);

		      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p,
							sizeof (struct
								log_run_postpone),
							&log_lsa, log_pgptr);

		      run_posp =
			(struct log_run_postpone *) ((char *) log_pgptr->
						     area + log_lsa.offset);

		      if (LSA_EQ (start_postpone_lsa, &run_posp->ref_lsa))
			{
			  /* run_postpone_log of start_postpone is found,
			   * next_postpone_lsa is the first postpone
			   * to be applied.
			   */
			  start_postpone_lsa_wasapplied = true;
			  isdone = true;
			}
		      break;

		    case LOG_POSTPONE:
		      if (LSA_ISNULL (&next_postpone_lsa)
			  && !LSA_EQ (start_postpone_lsa, &log_lsa))
			{
			  /* remember next postpone_lsa */
			  LSA_COPY (&next_postpone_lsa, &log_lsa);
			}
		      break;

		    case LOG_END_OF_LOG:
		      if (forward_lsa.pageid == NULL_PAGEID
			  && logpb_is_page_in_archive (log_lsa.pageid))
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

	      if (forward_lsa.offset == NULL_OFFSET
		  && forward_lsa.pageid != NULL_PAGEID
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
 * log_recovery_vacuum_data_buffer () - Recover vacuum data buffer. If the
 *					server crashed, some block data saved
 *					temporarily in a buffer may be lost.
 *
 * return			: Void.
 * thread_p (in)		: Thread entry.
 * mvcc_op_lsa (in)		: LSA of current MVCC operation.
 * mvccid (in)			: MVCCID of current MVCC operation.
 */
static void
log_recovery_vacuum_data_buffer (THREAD_ENTRY * thread_p,
				 LOG_LSA * mvcc_op_lsa, MVCCID mvccid)
{
  assert (mvcc_Enabled);

  assert (mvccid != MVCCID_NULL);
  assert (mvcc_id_precedes (mvccid, log_Gl.hdr.mvcc_next_id));

  /* Recover vacuum data information */
  if (LSA_ISNULL (&log_Gl.hdr.mvcc_op_log_lsa)
      || (VACUUM_GET_LOG_BLOCKID (log_Gl.hdr.mvcc_op_log_lsa.pageid)
	  != VACUUM_GET_LOG_BLOCKID (mvcc_op_lsa->pageid)))
    {
      /* A new block is started */
      if (!LSA_ISNULL (&log_Gl.hdr.mvcc_op_log_lsa))
	{
	  /* Save previous block data */
	  vacuum_produce_log_block_data (thread_p,
					 &log_Gl.hdr.mvcc_op_log_lsa,
					 log_Gl.hdr.last_block_oldest_mvccid,
					 log_Gl.hdr.last_block_newest_mvccid);
	}

      LSA_COPY (&log_Gl.hdr.mvcc_op_log_lsa, mvcc_op_lsa);
      log_Gl.hdr.last_block_newest_mvccid = mvccid;
      log_Gl.hdr.last_block_oldest_mvccid = mvccid;
    }
  else
    {
      /* New MVCC log record belongs to the same block as
       * previous MVCC log record.
       */
      LSA_COPY (&log_Gl.hdr.mvcc_op_log_lsa, mvcc_op_lsa);

      if (log_Gl.hdr.last_block_newest_mvccid == MVCCID_NULL
	  || mvcc_id_precedes (log_Gl.hdr.last_block_newest_mvccid, mvccid))
	{
	  log_Gl.hdr.last_block_newest_mvccid = mvccid;
	}

      if (log_Gl.hdr.last_block_oldest_mvccid == MVCCID_NULL
	  || mvcc_id_precedes (mvccid, log_Gl.hdr.last_block_oldest_mvccid))
	{
	  log_Gl.hdr.last_block_oldest_mvccid = mvccid;
	}
    }
}
