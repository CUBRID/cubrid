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
 * log_recovery.c -
 */

#ident "$Id$"

#include "log_recovery.h"

#include "boot_sr.h"
#include "locator_sr.h"
#include "log_checkpoint_info.hpp"
#include "log_impl.h"
#include "log_lsa.hpp"
#include "log_manager.h"
#include "log_meta.hpp"
#include "log_reader.hpp"
#include "log_recovery_analysis.hpp"
#include "log_recovery_context.hpp"
#include "log_recovery_redo_parallel.hpp"
#include "log_system_tran.hpp"
#include "log_volids.hpp"
#include "message_catalog.h"
#include "msgcat_set_log.hpp"
#include "object_representation.h"
#include "page_buffer.h"
#include "server_type.hpp"
#include "slotted_page.h"
#include "system_parameter.h"
#include "thread_manager.hpp"
#include "util_func.h"

static void log_rv_undo_record (THREAD_ENTRY * thread_p, LOG_LSA * log_lsa, LOG_PAGE * log_page_p,
				LOG_RCVINDEX rcvindex, const VPID * rcv_vpid, LOG_RCV * rcv,
				const LOG_LSA * rcv_lsa_ptr, LOG_TDES * tdes, LOG_ZIP * undo_unzip_ptr);
static bool log_rv_find_checkpoint (THREAD_ENTRY * thread_p, VOLID volid, LOG_LSA * rcv_lsa);
static bool log_recovery_needs_skip_logical_redo (THREAD_ENTRY * thread_p, TRANID tran_id, LOG_RECTYPE log_rtype,
						  LOG_RCVINDEX rcv_index, const LOG_LSA * lsa);
static void log_recovery_redo (THREAD_ENTRY * thread_p, log_recovery_context & context);
static void log_recovery_abort_interrupted_sysop (THREAD_ENTRY * thread_p, LOG_TDES * tdes,
						  const LOG_LSA * postpone_start_lsa);
static void log_recovery_finish_sysop_postpone (THREAD_ENTRY * thread_p, LOG_TDES * tdes);
static void log_recovery_finish_postpone (THREAD_ENTRY * thread_p, LOG_TDES * tdes);
static void log_recovery_finish_all_postpone (THREAD_ENTRY * thread_p);
static void log_recovery_abort_atomic_sysop (THREAD_ENTRY * thread_p, LOG_TDES * tdes);
static void log_recovery_abort_all_atomic_sysops (THREAD_ENTRY * thread_p);
static void log_recovery_undo (THREAD_ENTRY * thread_p);
static bool log_unformat_ahead_volumes (THREAD_ENTRY * thread_p, VOLID volid, VOLID * start_volid);
static void log_recovery_notpartof_volumes (THREAD_ENTRY * thread_p);
static int log_recovery_find_first_postpone (THREAD_ENTRY * thread_p, LOG_LSA * ret_lsa, LOG_LSA * start_postpone_lsa,
					     LOG_TDES * tdes);

static int log_rv_record_modify_internal (THREAD_ENTRY * thread_p, const LOG_RCV * rcv, bool is_undo);
static int log_rv_undoredo_partial_changes_recursive (THREAD_ENTRY * thread_p, OR_BUF * rcv_buf, RECDES * record,
						      bool is_undo);

STATIC_INLINE PAGE_PTR log_rv_redo_fix_page (THREAD_ENTRY * thread_p, const VPID * vpid_rcv, LOG_RCVINDEX rcvindex)
  __attribute__ ((ALWAYS_INLINE));

static void log_rv_simulate_runtime_worker (THREAD_ENTRY * thread_p, LOG_TDES * tdes);
static void log_rv_end_simulation (THREAD_ENTRY * thread_p);
static void log_find_unilaterally_largest_undo_lsa (THREAD_ENTRY * thread_p, LOG_LSA & max_undo_lsa);

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
  thread_type save_thread_type = thread_type::TT_NONE;

  if (thread_p == NULL)
    {
      /* Thread entry info is required. */
      thread_p = thread_get_thread_entry_info ();
    }

  log_rv_simulate_runtime_worker (thread_p, tdes);

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
  log_rv_end_simulation (thread_p);
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
 *   redo_unzip(in): functions as an outside managed (longer lived) buffer space for the underlying
 *        to peform its work; the pointer to the buffer is then passed - non-owningly - to the rcv
 *        structure for further processing; it is therefore mandatory that the acontents of the
 *        buffer does not change until after the
 *
 * NOTE: Execute a redo log record.
 */
void
log_rv_redo_record (THREAD_ENTRY * thread_p, log_reader & log_pgptr_reader,
		    int (*redofun) (THREAD_ENTRY * thread_p, const LOG_RCV *), LOG_RCV * rcv,
		    const LOG_LSA * rcv_lsa_ptr, int undo_length, const char *undo_data, LOG_ZIP & redo_unzip)
{
  int error_code;

  /* Note the the data page rcv->pgptr has been fetched by the caller */

  if (log_rv_get_unzip_and_diff_redo_log_data
      (thread_p, log_pgptr_reader, rcv, undo_length, undo_data, redo_unzip) != NO_ERROR)
    {
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_rv_redo_record");
      return;
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
			     "log_rv_redo_record: Error applying redo record at log_lsa=(%lld, %d), "
			     "rcv = {mvccid=%llu, vpid=(%d, %d), offset = %d, data_length = %d}",
			     (long long int) rcv_lsa_ptr->pageid, (int) rcv_lsa_ptr->offset,
			     (long long int) rcv->mvcc_id, (int) vpid.pageid, (int) vpid.volid, (int) rcv->offset,
			     (int) rcv->length);
	}
    }
  else
    {
      er_log_debug (ARG_FILE_LINE,
		    "log_rv_redo_record: WARNING.. There is not a"
		    " REDO function to execute. May produce recovery problems.");
    }

  if (rcv->pgptr != NULL)
    {
      (void) pgbuf_set_lsa (thread_p, rcv->pgptr, rcv_lsa_ptr);
    }
}

/* log_rv_fix_page_and_check_redo_is_needed - check if page still exists and, if yes, whether the lsa has not
 *                    already been applied
 *
 * return: boolean
 *
 *  thread_p(in):
 *  page_vpid(in): page identifier
 *  rcv(in/out):
 *  rcvindex(in): recovery index of log record to redo
 *  rcv_lsa(in): Reset data page (rcv->pgptr) to this LSA
 *  end_redo_lsa(in):
 */
bool
log_rv_fix_page_and_check_redo_is_needed (THREAD_ENTRY * thread_p, const VPID & page_vpid, log_rcv & rcv,
					  LOG_RCVINDEX rcvindex, const log_lsa & rcv_lsa, const LOG_LSA * end_redo_lsa)
{
  assert (rcv.pgptr == nullptr);

  if (!VPID_ISNULL (&page_vpid))
    {
      rcv.pgptr = log_rv_redo_fix_page (thread_p, &page_vpid, rcvindex);
      if (rcv.pgptr == nullptr)
	{
	  /* the page was changed and also deallocated in the meantime, no need to apply redo */
	  // only acceptable during recovery, not acceptable for replication
	  assert (log_is_in_crash_recovery ());
	  return false;
	}
    }

  if (rcv.pgptr != nullptr)
    {
      /* LSA of data page for log record to redo */
      const log_lsa *const rcv_page_ptr = pgbuf_get_lsa (rcv.pgptr);
      /*
       * Do we need to execute the redo operation ?
       * If page_lsa >= lsa... already updated. In this case make sure
       * that the redo is not far from the end_redo_lsa (TODO: how to ensure this last bit?)
       */
      assert (rcv_page_ptr != nullptr);
      assert (end_redo_lsa == nullptr || LSA_ISNULL (end_redo_lsa) || *rcv_page_ptr <= *end_redo_lsa);
      if (rcv_lsa <= *rcv_page_ptr)
	{
	  /* already applied, make sure to unfix the page */
	  // only acceptable during recovery, not acceptable for replication
	  assert (log_is_in_crash_recovery ());
	  pgbuf_unfix_and_init (thread_p, rcv.pgptr);
	  return false;
	}
    }

  return true;
}

/* log_rv_need_sync_redo - some of the redo log records need to be applied synchronously:
 *
 *  a_rcv_vpid: log record vpid, can be a null vpid
 *  a_rcvindex: recovery index of log record to redo
 */
bool
log_rv_need_sync_redo (const vpid & a_rcv_vpid, LOG_RCVINDEX a_rcvindex)
{
  if (VPID_ISNULL (&a_rcv_vpid))
    {
      // if the log record does not modify a certain page (ie: the vpid is null)
      return true;
    }

  switch (a_rcvindex)
    {
    case RVDK_NEWVOL:
    case RVDK_FORMAT:
    case RVDK_INITMAP:
    case RVDK_EXPAND_VOLUME:
    case RVDK_VOLHEAD_EXPAND:
      // Creating/expanding a volume needs to be waited before applying other changes in new pages
      return true;
    case RVDK_RESERVE_SECTORS:
    case RVDK_UNRESERVE_SECTORS:
      // When sectors are unreserved, redo ops on these pages are not applied.
      // Sector reservation is handled synchronously for better control
      return true;
    default:
      return false;
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
 * log_rv_get_unzip_log_data - GET UNZIP (UNDO or REDO) LOG DATA FROM LOG
 *
 * return: error code
 *
 *   length(in): log data size
 *   log_reader(in/out): (output invariant) reader will be correctly aligned after reading needed data
 *   unzip_ptr(in/out): must be pre-allocated, where the data will be extracted, if the internal
 *                buffer is not enough, it will be re-alloc'ed internally
 *   is_zip(out): a helper flag which is only needed for the situation where
 *                this function is used for the extraction of the 'redo' log data
 *
 * NOTE:if log_data is unzip data return LOG_ZIP data
 *               else log_data is zip data return unzip log data
 */
int
log_rv_get_unzip_log_data (THREAD_ENTRY * thread_p, int length, log_reader & log_pgptr_reader,
			   LOG_ZIP * unzip_ptr, bool & is_zip)
{
  char *area_ptr = nullptr;	/* Temporary working pointer */
  // *INDENT-OFF*
  std::unique_ptr<char[]> area;
  // *INDENT-ON*

  /*
   * If data is contained in only one buffer, pass pointer directly.
   * Otherwise, allocate a contiguous area, copy the data and pass this area.
   * At the end the area will de-allocate itself so make sure that wherever
   * a pointer to this data ends, data is copied before the end of the scope.
   */
  is_zip = ZIP_CHECK (length);
  const int unzip_length = (is_zip) ? (int) GET_ZIP_LEN (length) : length;

  const bool fits_in_current_page = log_pgptr_reader.does_fit_in_current_page (unzip_length);
  if (fits_in_current_page)
    {
      /* Data is contained in one buffer */
      // *INDENT-OFF*
      area_ptr = const_cast<char*> (log_pgptr_reader.reinterpret_cptr<char> ());
      // *INDENT-ON*
    }
  else
    {
      /* Need to copy the data into a contiguous area */
      area.reset (new char[unzip_length]);
      area_ptr = area.get ();
      log_pgptr_reader.copy_from_log (area_ptr, unzip_length);
    }

  if (is_zip)
    {
      /* will re-alloc the buffer internally if current buffer is not enough */
      if (!log_unzip (unzip_ptr, unzip_length, area_ptr))
	{
	  logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_rv_get_unzip_log_data");
	  return ER_FAILED;
	}
    }
  else
    {
      /* explicitly re-alloc the buffer if needed */
      if (unzip_ptr->buf_size < unzip_length)
	{
	  if (!log_zip_realloc_if_needed (*unzip_ptr, unzip_length))
	    {
	      logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_rv_get_unzip_log_data");
	      return ER_FAILED;
	    }
	}
      assert (unzip_length <= unzip_ptr->buf_size);
      unzip_ptr->data_length = unzip_length;
      memcpy (unzip_ptr->log_data, area_ptr, unzip_length);
    }

  if (fits_in_current_page)
    {
      log_pgptr_reader.add_align (unzip_length);
    }
  else
    {
      /* only align; advance was peformed while copying from log into the supplied buffer */
      log_pgptr_reader.align ();
    }

  return NO_ERROR;
}

/*
 * log_rv_get_unzip_and_diff_redo_log_data - GET UNZIPPED and DIFFED REDO LOG DATA FROM LOG
 *
 * return: error code
 *
 *   length(in): log data size
 *   log_reader(in/out):
 *   rcv(in/out): Recovery structure for recovery function
 *   undo_length(in): undo data length
 *   undo_data(in): undo data
 *   redo_unzip(out): extracted redo data; required to be passed by address because
 *                    it also functions as an internal working buffer
 *
 * NOTE:if log_data is unzip data return LOG_ZIP data
 *               else log_data is zip data return unzip log data
 * TODO: after refactoring, it's easier to supply (or not) a pointer to the undo unzip
 * TODO: separate in 2 based on diffing or not and fork outside
 */
int
log_rv_get_unzip_and_diff_redo_log_data (THREAD_ENTRY * thread_p, log_reader & log_pgptr_reader,
					 LOG_RCV * rcv, int undo_length, const char *undo_data, LOG_ZIP & redo_unzip)
{
  bool is_zip = false;
  LOG_ZIP *local_unzip_ptr = nullptr;

  if (log_rv_get_unzip_log_data (thread_p, rcv->length, log_pgptr_reader, &redo_unzip, is_zip) != NO_ERROR)
    {
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_rv_get_unzip_and_diff_redo_log_data");
      return ER_FAILED;
    }

  if (is_zip)
    {
      if (undo_length > 0 && undo_data != nullptr)
	{
	  (void) log_diff (undo_length, undo_data, redo_unzip.data_length, redo_unzip.log_data);
	}
    }
  // *INDENT-OFF*
  rcv->length = static_cast<int> (redo_unzip.data_length);
  rcv->data = static_cast<char*> (redo_unzip.log_data);
  // *INDENT-ON*

  return NO_ERROR;
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
  int tran_index;
  INT64 num_redo_log_records;
  int error_code = NO_ERROR;

  log_recovery_context context;

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

  er_log_debug (ARG_FILE_LINE, "RECOVERY: start with %lld|%d and stop at %lld", LSA_AS_ARGS (&log_Gl.hdr.chkpt_lsa),
		stopat != NULL ? *stopat : -1);

  /* Find the starting LSA for the analysis phase */

  if (ismedia_crash != false)
    {
      /* Media crash means restore from backup. */
      LOG_LSA chkpt_lsa = log_Gl.hdr.chkpt_lsa;
      // Check data volumes, we may have to start from an older checkpoint
      (void) fileio_map_mounted (thread_p, (bool (*)(THREAD_ENTRY *, VOLID, void *)) log_rv_find_checkpoint,
				 &chkpt_lsa);
      context.init_for_restore (chkpt_lsa, stopat);
    }
  else
    {
      // Recovery after unexpected stop or crash
      context.init_for_recovery (log_Gl.hdr.chkpt_lsa);
    }

  /* Notify vacuum it may need to recover the lost block data.
   * There are two possible cases here:
   * 1. recovery finds MVCC op log records after last checkpoint, so vacuum can start its recovery from last MVCC op
   *    log record.
   * 2. no MVCC op log record is found, so vacuum has to start recovery from checkpoint LSA. It will go
   *    backwards record by record until it either finds a MVCC op log record or until it reaches last block in
   *    vacuum data.
   */
  vacuum_notify_server_crashed (&context.get_checkpoint_lsa ());

  /*
   * First,  ANALYSIS the log to find the state of the transactions
   * Second, REDO going forward
   * Last,   UNDO going backwards
   */

  log_Gl.rcv_phase = LOG_RECOVERY_ANALYSIS_PHASE;
  log_recovery_analysis (thread_p, &num_redo_log_records, context);

  er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_LOG_RECOVERY_STARTED, 3, num_redo_log_records,
	  context.get_start_redo_lsa ().pageid, context.get_end_redo_lsa ().pageid);

  log_Gl.chkpt_redo_lsa = context.get_start_redo_lsa ();

  LOG_SET_CURRENT_TRAN_INDEX (thread_p, rcv_tran_index);
  if (logpb_fetch_start_append_page (thread_p) != NO_ERROR)
    {
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_recovery:logpb_fetch_start_append_page");
      // dead-ended. not reach here
      return;
    }

  if (!context.is_restore_incomplete ())
    {
      /* Read the End of file record to find out the previous address */
      eof = (LOG_RECORD_HEADER *) LOG_APPEND_PTR ();
      LOG_RESET_PREV_LSA (&eof->back_lsa);
    }
  log_Gl.prior_info.check_lsa_consistency ();

#if !defined(SERVER_MODE)
  LSA_COPY (&log_Gl.final_restored_lsa, &log_Gl.hdr.append_lsa);
#endif /* SERVER_MODE */

  if (get_server_type () == SERVER_TYPE_TRANSACTION)
    {
      log_append_empty_record (thread_p, LOG_DUMMY_CRASH_RECOVERY, NULL);
    }

  /*
   * Save the crash point lsa for use during the remaining recovery
   * phases.
   */
  LSA_COPY (&log_Gl.rcv_phase_lsa, &rcv_tdes->tail_lsa);

  /* Redo phase */
  log_Gl.rcv_phase = LOG_RECOVERY_REDO_PHASE;

  LOG_SET_CURRENT_TRAN_INDEX (thread_p, rcv_tran_index);

  log_recovery_redo (thread_p, context);
  boot_reset_db_parm (thread_p);

  /* Undo phase */
  log_Gl.rcv_phase = LOG_RECOVERY_UNDO_PHASE;

  LOG_SET_CURRENT_TRAN_INDEX (thread_p, rcv_tran_index);

  if (!context.is_page_server ())
    {
      log_recovery_undo (thread_p);
      boot_reset_db_parm (thread_p);
    }

  // *INDENT-OFF*
  log_system_tdes::rv_final ();
  // *INDENT-ON*

  if (context.is_restore_incomplete ())
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
      // dead-ended. not reach here
      return;
    }

  /* Remove all class representations. */
  error_code = heap_classrepr_restart_cache ();
  if (error_code != NO_ERROR)
    {
      assert (false);
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_recovery:heap_classrepr_restart_cache");
      // dead-ended. not reach here
      return;
    }

  er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_LOG_RECOVERY_FINISHED, 0);
}

/*
 * log_recovery_needs_skip_logical_redo - Check whether we need to skip logical redo.
 *
 * return: true if skip logical redo, false otherwise
 *
 *   thread_p(in): Thread entry
 *   tran_id(in) : Transaction id.
 *   log_rtype(in): Log record type
 *   rcv_index(in): Recovery index
 *   lsa(in) : lsa to check
 *
 * NOTE: When logical redo logging is applied and the system crashes repeatedly, we need to
 *       skip redo logical record already applied. This function checks whether the logical redo must be skipped.
 */
static bool
log_recovery_needs_skip_logical_redo (THREAD_ENTRY * thread_p, TRANID tran_id, LOG_RECTYPE log_rtype,
				      LOG_RCVINDEX rcv_index, const LOG_LSA * lsa)
{
  int tran_index;
  LOG_TDES *tdes = NULL;	/* Transaction descriptor */

  assert (lsa != NULL);

  if (log_rtype != LOG_DBEXTERN_REDO_DATA)
    {
      return false;
    }

  tran_index = logtb_find_tran_index (thread_p, tran_id);
  if (tran_index == NULL_TRAN_INDEX)
    {
      return false;
    }

  tdes = LOG_FIND_TDES (tran_index);
  if (tdes == NULL)
    {
      return false;
    }

  /* logical redo logging */
  // analysis_last_aborted_sysop_start_lsa < lsa < analysis_last_aborted_sysop_lsa
  if (LSA_LT (&tdes->rcv.analysis_last_aborted_sysop_start_lsa, lsa)
      && LSA_LT (lsa, &tdes->rcv.analysis_last_aborted_sysop_lsa))
    {
      /* Logical redo already applied. */
      er_log_debug (ARG_FILE_LINE, "log_recovery_needs_skip_logical_redo: LSA = %lld|%d, Rv_index = %s, "
		    "analysis_last_aborted_sysop_lsa = %lld|%d, analysis_last_aborted_sysop_start_lsa = %lld|%d\n",
		    LSA_AS_ARGS (lsa), rv_rcvindex_string (rcv_index),
		    LSA_AS_ARGS (&tdes->rcv.analysis_last_aborted_sysop_lsa),
		    LSA_AS_ARGS (&tdes->rcv.analysis_last_aborted_sysop_start_lsa));
      return true;
    }

  return false;
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
log_recovery_redo (THREAD_ENTRY * thread_p, log_recovery_context & context)
{
  LOG_LSA lsa;			/* LSA of log record to redo */
  log_reader log_pgptr_reader;

  volatile TRANID tran_id;
  volatile LOG_RECTYPE log_rtype;
  LOG_ZIP *undo_unzip_ptr = NULL;
  LOG_ZIP *redo_unzip_ptr = NULL;
  bool is_mvcc_op = false;
  const bool force_each_log_page_fetch = false;

  /* depending on compilation mode and on a system parameter, initialize the
   * infrastructure for parallel log recovery;
   * if infrastructure is not initialized dependent code below works sequentially
   */
  LOG_CS_EXIT (thread_p);
  // *INDENT-OFF*
  std::unique_ptr<cublog::minimum_log_lsa_monitor> minimum_log_lsa;
  std::unique_ptr<cublog::redo_parallel> parallel_recovery_redo;
#if defined(SERVER_MODE)
  {
    const int log_recovery_redo_parallel_count = prm_get_integer_value (PRM_ID_RECOVERY_PARALLEL_COUNT);
    assert (log_recovery_redo_parallel_count >= 0);
    if (log_recovery_redo_parallel_count > 0)
      {
	minimum_log_lsa.reset (new cublog::minimum_log_lsa_monitor ());
	parallel_recovery_redo.reset (new cublog::redo_parallel (log_recovery_redo_parallel_count,
								 *minimum_log_lsa.get ()));
      }
  }
#endif
  // *INDENT-ON*

  /*
   * GO FORWARD, redoing records of all transactions including aborted ones.
   *
   * Compensating records undo the redo of already executed undo records.
   * Transactions that were active at the time of the crash are aborted
   * during the log_recovery_undo phase
   */
  lsa = context.get_start_redo_lsa ();

  /* Defense for illegal start_redolsa */
  if ((lsa.offset + (int) sizeof (LOG_RECORD_HEADER)) >= LOGAREA_SIZE)
    {
      assert (false);
      /* move first record of next page */
      lsa.pageid++;
      lsa.offset = NULL_OFFSET;
    }

  undo_unzip_ptr = log_zip_alloc (LOGAREA_SIZE);
  redo_unzip_ptr = log_zip_alloc (LOGAREA_SIZE);

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
      if (log_pgptr_reader.set_lsa_and_fetch_page (lsa) != NO_ERROR)
	{
	  if (lsa > context.get_end_redo_lsa ())
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
      while (lsa.pageid == log_pgptr_reader.get_pageid ())
	{
	  /*
	   * Do we want to stop the recovery redo process at this time ?
	   */
	  if (lsa > context.get_end_redo_lsa ())
	    {
	      LSA_SET_NULL (&lsa);
	      break;
	    }

	  /*
	   * If an offset is missing, it is because we archive an incomplete
	   * log record. This log_record was completed later. Thus, we have to
	   * find the offset by searching for the next log_record in the page
	   */
	  if (lsa.offset == NULL_OFFSET)
	    {
	      lsa.offset = log_pgptr_reader.get_page_header ().offset;
	      if (lsa.offset == NULL_OFFSET)
		{
		  /* Continue with next pageid */
		  const auto log_lsa_pageid = log_pgptr_reader.get_pageid ();
		  if (logpb_is_page_in_archive (log_lsa_pageid))
		    {
		      lsa.pageid = log_lsa_pageid + 1;
		    }
		  else
		    {
		      lsa.pageid = NULL_PAGEID;
		    }
		  continue;
		}
	    }

	  LSA_COPY (&log_Gl.unique_stats_table.curr_rcv_rec_lsa, &lsa);

	  /* both the page id and the offsset might have changed; page id is changed at the end of the loop */
	  log_pgptr_reader.set_lsa_and_fetch_page (lsa);

	  {
	    /* Pointer to log record */
	    // *INDENT-OFF*
	    const LOG_RECORD_HEADER *log_rec_header = log_pgptr_reader.reinterpret_cptr<LOG_RECORD_HEADER> ();
	    // *INDENT-ON*

	    tran_id = log_rec_header->trid;
	    log_rtype = log_rec_header->type;

	    /* Get the address of next log record to scan */
	    LSA_COPY (&lsa, &log_rec_header->forw_lsa);
	  }

	  /*
	   * If the next page is NULL_PAGEID and the current page is an archive
	   * page, this is not the end, this situation happens when an incomplete
	   * log record is archived. Thus, its forward address is NULL.
	   * Note that we have to set lsa.pageid here since the log_lsa.pageid value
	   * can be changed (e.g., the log record is stored in an archive page and
	   * in an active page. Later, we try to modify it whenever is possible.
	   */
	  {
	    const auto log_lsa_pageid = log_pgptr_reader.get_pageid ();
	    const auto log_lsa_offset = log_pgptr_reader.get_offset ();
	    if (LSA_ISNULL (&lsa) && logpb_is_page_in_archive (log_lsa_pageid))
	      {
		lsa.pageid = log_lsa_pageid + 1;
	      }

	    if (!LSA_ISNULL (&lsa) && log_lsa_pageid != NULL_PAGEID
		&& (lsa.pageid < log_lsa_pageid || (lsa.pageid == log_lsa_pageid && lsa.offset <= log_lsa_offset)))
	      {
		/* It seems to be a system error. Maybe a loop in the log */

		LSA_SET_NULL (&log_Gl.unique_stats_table.curr_rcv_rec_lsa);

		er_log_debug (ARG_FILE_LINE,
			      "log_recovery_redo: ** System error: It seems to be a loop in the log\n."
			      " Current log_rec at %lld|%d. Next log_rec at %lld|%d\n",
			      (long long int) log_lsa_pageid, log_lsa_offset, (long long int) lsa.pageid, lsa.offset);
		logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_recovery_redo");
		LSA_SET_NULL (&lsa);
		break;
	      }
	  }

	  switch (log_rtype)
	    {
	    case LOG_MVCC_UNDOREDO_DATA:
	    case LOG_MVCC_DIFF_UNDOREDO_DATA:
	      {
		const LOG_LSA rcv_lsa = log_pgptr_reader.get_lsa ();	/* Address of redo log record */

		/* skip log record header HEADER */
		log_pgptr_reader.add_align (sizeof (LOG_RECORD_HEADER));
		log_pgptr_reader.advance_when_does_not_fit (sizeof (LOG_REC_MVCC_UNDOREDO));

		/* MVCC op undo/redo log record */
		// *INDENT-OFF*
		const LOG_REC_MVCC_UNDOREDO log_rec_mvcc_undoredo
		    = log_pgptr_reader.reinterpret_copy_and_add_align<LOG_REC_MVCC_UNDOREDO> ();

		const MVCCID mvccid = log_rv_get_log_rec_mvccid<LOG_REC_MVCC_UNDOREDO> (log_rec_mvcc_undoredo);
		// *INDENT-ON*

		/* Check if MVCC next ID must be updated */
		if (!MVCC_ID_PRECEDES (mvccid, log_Gl.hdr.mvcc_next_id))
		  {
		    log_Gl.hdr.mvcc_next_id = mvccid;
		    MVCCID_FORWARD (log_Gl.hdr.mvcc_next_id);
		  }

		/* Save last MVCC operation LOG_LSA. */
		LSA_COPY (&log_Gl.hdr.mvcc_op_log_lsa, &rcv_lsa);

                // *INDENT-OFF*
                log_rv_redo_record_sync_or_dispatch_async<LOG_REC_MVCC_UNDOREDO>
		  (thread_p, log_pgptr_reader, log_rec_mvcc_undoredo, rcv_lsa, &context.get_end_redo_lsa (), log_rtype,
		   *undo_unzip_ptr, *redo_unzip_ptr, parallel_recovery_redo, force_each_log_page_fetch);
                // *INDENT-ON*
	      }
	      break;

	    case LOG_UNDOREDO_DATA:
	    case LOG_DIFF_UNDOREDO_DATA:
	      {
		const LOG_LSA rcv_lsa = log_pgptr_reader.get_lsa ();	/* Address of redo log record */

		/* skip log record header HEADER */
		log_pgptr_reader.add_align (sizeof (LOG_RECORD_HEADER));
		log_pgptr_reader.advance_when_does_not_fit (sizeof (LOG_REC_UNDOREDO));

		/* Get undoredo structure */
		// *INDENT-OFF*
		const LOG_REC_UNDOREDO log_rec_undoredo
		    = log_pgptr_reader.reinterpret_copy_and_add_align<LOG_REC_UNDOREDO> ();

                log_rv_redo_record_sync_or_dispatch_async<LOG_REC_UNDOREDO>
		  (thread_p, log_pgptr_reader, log_rec_undoredo, rcv_lsa, &context.get_end_redo_lsa (), log_rtype,
		   *undo_unzip_ptr, *redo_unzip_ptr, parallel_recovery_redo, force_each_log_page_fetch);
                // *INDENT-ON*
	      }
	      break;

	    case LOG_MVCC_REDO_DATA:
	      {
		const LOG_LSA rcv_lsa = log_pgptr_reader.get_lsa ();	/* Address of redo log record */

		/* Get the DATA HEADER */
		log_pgptr_reader.add_align (sizeof (LOG_RECORD_HEADER));
		log_pgptr_reader.advance_when_does_not_fit (sizeof (LOG_REC_MVCC_REDO));

		/* MVCC op redo log record */
		// *INDENT-OFF*
		const LOG_REC_MVCC_REDO log_rec_mvcc_redo
		    = log_pgptr_reader.reinterpret_copy_and_add_align<LOG_REC_MVCC_REDO> ();

		const MVCCID mvccid = log_rv_get_log_rec_mvccid<LOG_REC_MVCC_REDO> (log_rec_mvcc_redo);
		// *INDENT-ON*

		/* Check if MVCC next ID must be updated */
		if (!MVCC_ID_PRECEDES (mvccid, log_Gl.hdr.mvcc_next_id))
		  {
		    log_Gl.hdr.mvcc_next_id = mvccid;
		    MVCCID_FORWARD (log_Gl.hdr.mvcc_next_id);
		  }

		/* NOTE: do not update rcv_lsa on the global bookkeeping that is
		 * relevant for vacuum as vacuum only processes undo data */

                // *INDENT-OFF*
                log_rv_redo_record_sync_or_dispatch_async<LOG_REC_MVCC_REDO>
		  (thread_p, log_pgptr_reader, log_rec_mvcc_redo, rcv_lsa, &context.get_end_redo_lsa (), log_rtype,
		   *undo_unzip_ptr, *redo_unzip_ptr, parallel_recovery_redo, force_each_log_page_fetch);
                // *INDENT-ON*
	      }
	      break;

	    case LOG_REDO_DATA:
	      {
		const LOG_LSA rcv_lsa = log_pgptr_reader.get_lsa ();	/* Address of redo log record */

		/* Get the DATA HEADER */
		log_pgptr_reader.add_align (sizeof (LOG_RECORD_HEADER));
		log_pgptr_reader.advance_when_does_not_fit (sizeof (LOG_REC_REDO));

		// *INDENT-OFF*
		const LOG_REC_REDO log_rec_redo
		    = log_pgptr_reader.reinterpret_copy_and_add_align<LOG_REC_REDO> ();

		if (log_rec_redo.data.rcvindex == RVVAC_COMPLETE)
		  {
		    /* Reset log header MVCC info */
		    logpb_vacuum_reset_log_header_cache (thread_p, &log_Gl.hdr);
		  }

                log_rv_redo_record_sync_or_dispatch_async<LOG_REC_REDO>
		  (thread_p, log_pgptr_reader, log_rec_redo, rcv_lsa, &context.get_end_redo_lsa (), log_rtype,
		   *undo_unzip_ptr, *redo_unzip_ptr, parallel_recovery_redo, force_each_log_page_fetch);
                // *INDENT-ON*
	      }
	      break;

	    case LOG_DBEXTERN_REDO_DATA:
	      {
		LOG_RCV rcv;	// = LOG_RCV_INITIALIZER;  /* Recovery structure */
		const LOG_LSA rcv_lsa = log_pgptr_reader.get_lsa ();	/* Address of redo log record */

		/* Get the DATA HEADER */
		log_pgptr_reader.add_align (sizeof (LOG_RECORD_HEADER));
		log_pgptr_reader.advance_when_does_not_fit (sizeof (LOG_REC_DBOUT_REDO));
		/* A external redo log record */
		// *INDENT-OFF*
		const LOG_REC_DBOUT_REDO *dbout_redo = log_pgptr_reader.reinterpret_cptr<LOG_REC_DBOUT_REDO> ();
		// *INDENT-ON*

		const LOG_RCVINDEX rcvindex = dbout_redo->rcvindex;	/* Recovery index function */

		rcv.offset = -1;
		rcv.pgptr = NULL;
		rcv.length = dbout_redo->length;

		/* GET AFTER DATA */
		log_pgptr_reader.add_align (sizeof (LOG_REC_DBOUT_REDO));

#if !defined(NDEBUG)
		if (prm_get_bool_value (PRM_ID_LOG_TRACE_DEBUG))
		  {
		    fprintf (stdout, "TRACE EXT REDOING[3]: LSA = %lld|%d, Rv_index = %s\n", LSA_AS_ARGS (&rcv_lsa),
			     rv_rcvindex_string (rcvindex));
		    fflush (stdout);
		  }
#endif /* !NDEBUG */

		if (!log_recovery_needs_skip_logical_redo (thread_p, tran_id, log_rtype, rcvindex, &rcv_lsa))
		  {
		    log_rv_redo_record (thread_p, log_pgptr_reader, RV_fun[rcvindex].redofun, &rcv,
					&rcv_lsa, 0, nullptr, *redo_unzip_ptr);
		    /* unzip_ptr used here only as a buffer for the underlying logic, the structure's buffer
		     * will be reallocated downstream if needed */
		  }
	      }
	      break;

	    case LOG_RUN_POSTPONE:
	      {
		const LOG_LSA rcv_lsa = log_pgptr_reader.get_lsa ();	/* Address of redo log record */

		/* Get the DATA HEADER */
		log_pgptr_reader.add_align (sizeof (LOG_RECORD_HEADER));
		log_pgptr_reader.advance_when_does_not_fit (sizeof (LOG_REC_RUN_POSTPONE));
		/* A run postpone action */

		// *INDENT-OFF*
		const LOG_REC_RUN_POSTPONE log_rec_run_posp
		    = log_pgptr_reader.reinterpret_copy_and_add_align<LOG_REC_RUN_POSTPONE> ();

                log_rv_redo_record_sync_or_dispatch_async<LOG_REC_RUN_POSTPONE>
		  (thread_p, log_pgptr_reader, log_rec_run_posp, rcv_lsa, &context.get_end_redo_lsa (), log_rtype,
		   *undo_unzip_ptr, *redo_unzip_ptr, parallel_recovery_redo, force_each_log_page_fetch);
                // *INDENT-ON*
	      }
	      break;

	    case LOG_COMPENSATE:
	      {
		const LOG_LSA rcv_lsa = log_pgptr_reader.get_lsa ();	/* Address of redo log record */

		/* Get the DATA HEADER */
		log_pgptr_reader.add_align (sizeof (LOG_RECORD_HEADER));
		log_pgptr_reader.advance_when_does_not_fit (sizeof (LOG_REC_COMPENSATE));

		// *INDENT-OFF*
		const LOG_REC_COMPENSATE log_rec_compensate
		    = log_pgptr_reader.reinterpret_copy_and_add_align<LOG_REC_COMPENSATE> ();

                log_rv_redo_record_sync_or_dispatch_async<LOG_REC_COMPENSATE>
		  (thread_p, log_pgptr_reader, log_rec_compensate, rcv_lsa, &context.get_end_redo_lsa (), log_rtype,
		   *undo_unzip_ptr, *redo_unzip_ptr, parallel_recovery_redo, force_each_log_page_fetch);
                // *INDENT-ON*
	      }
	      break;

	    case LOG_2PC_PREPARE:
	      {
		const int tran_index = logtb_find_tran_index (thread_p, tran_id);
		if (tran_index == NULL_TRAN_INDEX)
		  {
		    break;
		  }
		LOG_TDES *const tdes = LOG_FIND_TDES (tran_index);
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
		log_pgptr_reader.add_align (sizeof (LOG_RECORD_HEADER));

		if (tdes->state == TRAN_UNACTIVE_2PC_PREPARE)
		  {
		    /* The transaction was still prepared_to_commit state at the time of crash. So, read the global
		     * transaction identifier and list of locks from the log record, and acquire all of the locks. */

		    log_2pc_read_prepare (thread_p, LOG_2PC_OBTAIN_LOCKS, tdes, log_pgptr_reader);
		  }
		else
		  {
		    /* The transaction was not in prepared_to_commit state anymore at the time of crash. So, there is no
		     * need to read the list of locks from the log record. Read only the global transaction from the log
		     * record. */
		    log_2pc_read_prepare (thread_p, LOG_2PC_DONT_OBTAIN_LOCKS, tdes, log_pgptr_reader);
		  }
	      }
	      break;

	    case LOG_2PC_START:
	      {
		const int tran_index = logtb_find_tran_index (thread_p, tran_id);
		if (tran_index != NULL_TRAN_INDEX)
		  {
		    /* The transaction was still alive at the time of crash. */
		    LOG_TDES *const tdes = LOG_FIND_TDES (tran_index);
		    if (tdes != NULL && LOG_ISTRAN_2PC (tdes))
		      {
			/* The transaction was still alive at the time of crash. So, copy the coordinator information
			 * from the log record to the transaction descriptor. */

			/* Get the DATA HEADER */
			log_pgptr_reader.add_align (sizeof (LOG_RECORD_HEADER));
			log_pgptr_reader.advance_when_does_not_fit (sizeof (LOG_REC_2PC_START));
			/* Start 2PC commit log record */
			// *INDENT-OFF*
			const LOG_REC_2PC_START *start_2pc = log_pgptr_reader.reinterpret_cptr<LOG_REC_2PC_START> ();
			// *INDENT-ON*

			/*
			 * Obtain the participant information
			 */
			tdes->client.set_system_internal_with_user (start_2pc->user_name);
			tdes->gtrid = start_2pc->gtrid;

			const int num_particps = start_2pc->num_particps;	/* Number of participating sites */
			const int particp_id_length = start_2pc->particp_id_length;	/* Length of particp_ids block */
			void *block_particps_ids = malloc (particp_id_length * num_particps);	/* A block of participant ids */
			if (block_particps_ids == NULL)
			  {
			    /* Out of memory */
			    LSA_SET_NULL (&log_Gl.unique_stats_table.curr_rcv_rec_lsa);
			    logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_recovery_redo");
			    break;
			  }

			log_pgptr_reader.add_align (sizeof (LOG_REC_2PC_START));
			log_pgptr_reader.align ();

			/* Read in the participants info. block from the log */
			log_pgptr_reader.copy_from_log ((char *) block_particps_ids, particp_id_length * num_particps);

			/* Initialize the coordinator information */
			if (log_2pc_alloc_coord_info (tdes, num_particps, particp_id_length, block_particps_ids) ==
			    NULL)
			  {
			    LSA_SET_NULL (&log_Gl.unique_stats_table.curr_rcv_rec_lsa);
			    logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_recovery_redo");
			    break;
			  }

			/* Initialize the acknowledgment vector to 0 since we do not know what acknowledgments have
			 * already been received. we need to continue reading the log */

			if ((tdes->coord->ack_received =
			     (int *) malloc (sizeof (int) * tdes->coord->num_particps)) == NULL)
			  {
			    /* Out of memory */
			    LSA_SET_NULL (&log_Gl.unique_stats_table.curr_rcv_rec_lsa);
			    logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_recovery_redo");
			    break;
			  }
			for (int i = 0; i < tdes->coord->num_particps; i++)
			  {
			    tdes->coord->ack_received[i] = false;
			  }
		      }
		  }
	      }
	      break;

	    case LOG_2PC_RECV_ACK:
	      {
		const int tran_index = logtb_find_tran_index (thread_p, tran_id);
		if (tran_index != NULL_TRAN_INDEX)
		  {
		    /* The transaction was still alive at the time of crash. */

		    LOG_TDES *const tdes = LOG_FIND_TDES (tran_index);
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
			log_pgptr_reader.add_align (sizeof (LOG_RECORD_HEADER));
			log_pgptr_reader.advance_when_does_not_fit (sizeof (LOG_REC_2PC_PARTICP_ACK));
			/* A 2PC participant ack */
			// *INDENT-OFF*
			const LOG_REC_2PC_PARTICP_ACK *received_ack = log_pgptr_reader.reinterpret_cptr<LOG_REC_2PC_PARTICP_ACK> ();
			// *INDENT-ON*

			tdes->coord->ack_received[received_ack->particp_index] = true;
		      }
		  }
	      }
	      break;

	    case LOG_COMMIT:
	    case LOG_ABORT:
	      {
		bool free_tran = false;
		const int tran_index = logtb_find_tran_index (thread_p, tran_id);
		LOG_TDES *tdes = nullptr;
		if (tran_index != NULL_TRAN_INDEX && tran_index != LOG_SYSTEM_TRAN_INDEX)
		  {
		    tdes = LOG_FIND_TDES (tran_index);
		    assert (tdes && tdes->state != TRAN_ACTIVE);
		    free_tran = true;
		  }

		if (context.is_restore_incomplete ())
		  {
		    /* Need to read the donetime record to find out if we need to stop the recovery at this point. */
		    log_pgptr_reader.add_align (sizeof (LOG_RECORD_HEADER));
		    log_pgptr_reader.advance_when_does_not_fit (sizeof (LOG_REC_DONETIME));
		    // *INDENT-OFF*
		    const LOG_REC_DONETIME *donetime = log_pgptr_reader.reinterpret_cptr<LOG_REC_DONETIME> ();
		    // *INDENT-ON*

		    // stopat is provided in seconds
		    const time_t log_at_time = util_msec_to_sec (donetime->at_time);
		    if (context.does_restore_stop_before_time (log_at_time))
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
	      {
		/* Must detect MVCC operations and recover vacuum data buffer. The found operation is not actually
		 * redone/undone, but it has information that can be used for vacuum. */

		const LOG_LSA rcv_lsa = log_pgptr_reader.get_lsa ();	/* Address of redo log record */

		log_pgptr_reader.add_align (sizeof (LOG_RECORD_HEADER));
		log_pgptr_reader.advance_when_does_not_fit (sizeof (LOG_REC_MVCC_UNDO));
		/* MVCC op undo log record */
		// *INDENT-OFF*
		const LOG_REC_MVCC_UNDO *mvcc_undo = log_pgptr_reader.reinterpret_cptr<LOG_REC_MVCC_UNDO> ();
		// *INDENT-ON*
		const MVCCID mvccid = mvcc_undo->mvccid;

		/* Check if MVCC next ID must be updated */
		if (!MVCC_ID_PRECEDES (mvccid, log_Gl.hdr.mvcc_next_id))
		  {
		    log_Gl.hdr.mvcc_next_id = mvccid;
		    MVCCID_FORWARD (log_Gl.hdr.mvcc_next_id);
		  }

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
	    case LOG_SYSOP_ATOMIC_START:
	      break;

	    case LOG_SYSOP_END:
	      {
		const LOG_LSA rcv_lsa = log_pgptr_reader.get_lsa ();	/* Address of redo log record */

		log_pgptr_reader.add_align (sizeof (LOG_RECORD_HEADER));
		log_pgptr_reader.advance_when_does_not_fit (sizeof (LOG_REC_SYSOP_END));
		/* Result of top system op */
		// *INDENT-OFF*
		const LOG_REC_SYSOP_END *sysop_end = log_pgptr_reader.reinterpret_cptr<LOG_REC_SYSOP_END> ();
		// *INDENT-ON*

		if (sysop_end->type == LOG_SYSOP_END_LOGICAL_MVCC_UNDO)
		  {
		    LSA_COPY (&log_Gl.hdr.mvcc_op_log_lsa, &rcv_lsa);
		  }
	      }
	      break;

	    case LOG_SMALLER_LOGREC_TYPE:
	    case LOG_LARGER_LOGREC_TYPE:
	    default:
#if defined(CUBRID_DEBUG)
	      er_log_debug (ARG_FILE_LINE,
			    "log_recovery_redo: Unknown record type = %d (%s)... May be a system error", log_rtype,
			    log_to_string (log_rtype));
#endif /* CUBRID_DEBUG */
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_PAGE_CORRUPTED, 1, log_pgptr_reader.get_pageid ());
	      if (lsa == log_pgptr_reader.get_lsa ())
		{
		  LSA_SET_NULL (&lsa);
		}
	      break;
	    }

	  /*
	   * We can fix the lsa.pageid in the case of log_records without forward
	   * address at this moment.
	   */
	  if (lsa.offset == NULL_OFFSET && lsa.pageid != NULL_PAGEID && lsa.pageid < log_pgptr_reader.get_pageid ())
	    {
	      lsa.pageid = log_pgptr_reader.get_pageid ();
	    }
	}
    }

  log_zip_free (undo_unzip_ptr);
  log_zip_free (redo_unzip_ptr);

#if defined(SERVER_MODE)
  if (parallel_recovery_redo != nullptr)
    {
      parallel_recovery_redo->set_adding_finished ();
      parallel_recovery_redo->wait_for_termination_and_stop_execution ();
    }
#endif
  LOG_CS_ENTER (thread_p);

  if (!context.is_page_server ())
    {
      log_Gl.mvcc_table.reset_start_mvccid ();

      /* Abort all atomic system operations that were open when server crashed */
      log_recovery_abort_all_atomic_sysops (thread_p);

      /* Now finish all postpone operations */
      log_recovery_finish_all_postpone (thread_p);
    }

  /* Flush all dirty pages */
  logpb_flush_pages_direct (thread_p);

  logpb_flush_header (thread_p);
  (void) pgbuf_flush_all (thread_p, NULL_VOLID);

exit:
  LSA_SET_NULL (&log_Gl.unique_stats_table.curr_rcv_rec_lsa);

#if !defined(NDEBUG)
  log_Gl_recovery_redo_consistency_check.cleanup ();
#endif

  return;
}

/*
 * log_recovery_abort_interrupted_sysop () - find and abort interruped system operation during postpones.
 *
 * return                  : void
 * thread_p (in)           : thread entry
 * tdes (in)               : transaction descriptor
 * postpone_start_lsa (in) : LSA of start postpone (system op or transaction)
 */
static void
log_recovery_abort_interrupted_sysop (THREAD_ENTRY * thread_p, LOG_TDES * tdes, const LOG_LSA * postpone_start_lsa)
{
  LOG_LSA iter_lsa, prev_lsa;
  LOG_RECORD_HEADER logrec_head;
  LOG_PAGE *log_page = NULL;
  char buffer_log_page[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT];
  LOG_LSA last_parent_lsa = LSA_INITIALIZER;

  assert (tdes->state == TRAN_UNACTIVE_TOPOPE_COMMITTED_WITH_POSTPONE
	  || tdes->state == TRAN_UNACTIVE_COMMITTED_WITH_POSTPONE);
  assert (!LSA_ISNULL (postpone_start_lsa));

  if (LSA_ISNULL (&tdes->undo_nxlsa) || LSA_LE (&tdes->undo_nxlsa, postpone_start_lsa))
    {
      /* nothing to abort */
      return;
    }

  log_page = (LOG_PAGE *) PTR_ALIGN (buffer_log_page, MAX_ALIGNMENT);

  /* how it works:
   * we can have so-called logical run postpone system operation for some complex cases (e.g. file destroy or
   * deallocate). if these operations are interrupted by crash, we must abort them first before finishing the postpone
   * phase of system operation or transaction.
   *
   * normally, all records during the postpone execution are run postpones - physical or logical system operations.
   * so to rollback a logical system op during postpone we have to stop at previous run postpone (or at the start of
   * postpone if this system op is first). we need to manually search it.
   */

  for (iter_lsa = tdes->undo_nxlsa; LSA_GT (&iter_lsa, postpone_start_lsa); iter_lsa = prev_lsa)
    {
      if (logpb_fetch_page (thread_p, &iter_lsa, LOG_CS_FORCE_USE, log_page) != NO_ERROR)
	{
	  logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_recovery_abort_interrupted_sysop");
	  return;
	}
      logrec_head = *(LOG_RECORD_HEADER *) (log_page->area + iter_lsa.offset);
      assert (logrec_head.trid == tdes->trid);

      if (logrec_head.type == LOG_RUN_POSTPONE)
	{
	  /* found run postpone, stop */
	  last_parent_lsa = iter_lsa;
	  break;
	}
      else if (logrec_head.type == LOG_SYSOP_END)
	{
	  /* we need to see why type of system op. */
	  LOG_LSA read_lsa = iter_lsa;
	  LOG_REC_SYSOP_END *sysop_end;

	  /* skip header */
	  LOG_READ_ADD_ALIGN (thread_p, sizeof (logrec_head), &read_lsa, log_page);
	  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (*sysop_end), &read_lsa, log_page);

	  sysop_end = (LOG_REC_SYSOP_END *) (log_page->area + read_lsa.offset);
	  if (sysop_end->type == LOG_SYSOP_END_LOGICAL_RUN_POSTPONE)
	    {
	      /* found run postpone, stop */
	      last_parent_lsa = iter_lsa;
	      break;
	    }
	  else
	    {
	      /* go to last parent */
	      prev_lsa = sysop_end->lastparent_lsa;
	    }
	}
      else
	{
	  /* safe-guard: we do not expect postpone starts */
	  assert (logrec_head.type != LOG_COMMIT_WITH_POSTPONE && logrec_head.type != LOG_SYSOP_START_POSTPONE);

	  /* move to previous */
	  prev_lsa = logrec_head.prev_tranlsa;
	}
      assert (!LSA_ISNULL (&prev_lsa) && !LSA_EQ (&prev_lsa, &iter_lsa));
    }

  if (LSA_ISNULL (&last_parent_lsa))
    {
      /* no run postpones before system op. stop at start postpone. */
      assert (LSA_EQ (&iter_lsa, postpone_start_lsa));
      last_parent_lsa = *postpone_start_lsa;
    }

  /* simulate system op */
  log_sysop_start (thread_p);
  /* hack last parent lsa */
  tdes->topops.stack[tdes->topops.last].lastparent_lsa = last_parent_lsa;
  /* rollback */
  log_sysop_abort (thread_p);
}

/*
 * log_recovery_finish_sysop_postpone () - Finish postpone during recovery for one system operation.
 *
 * return        : void
 * thread_p (in) : thread entry
 * tdes (in)     : transaction descriptor
 */
static void
log_recovery_finish_sysop_postpone (THREAD_ENTRY * thread_p, LOG_TDES * tdes)
{
  LOG_LSA first_postpone_to_apply;

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
      log_recovery_abort_interrupted_sysop (thread_p, tdes, &sysop_start_postpone_lsa);
      assert (tdes->topops.last == 0);

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
      assert (sysop_start_postpone.sysop_end.type != LOG_SYSOP_END_LOGICAL_RUN_POSTPONE
	      || !sysop_start_postpone.sysop_end.run_postpone.is_sysop_postpone);

      log_sysop_end_recovery_postpone (thread_p, &sysop_start_postpone.sysop_end, undo_data_size, undo_data);
      if (undo_buffer != NULL)
	{
	  /* no longer needed */
	  db_private_free (thread_p, undo_buffer);
	}

      assert (sysop_start_postpone.sysop_end.type != LOG_SYSOP_END_ABORT);
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
      else if (!LSA_ISNULL (&tdes->rcv.tran_start_postpone_lsa))
	{
	  /* this must be after start postpone */
	  assert (LSA_LE (&tdes->rcv.tran_start_postpone_lsa, &sysop_start_postpone.sysop_end.lastparent_lsa));
	  tdes->state = TRAN_UNACTIVE_COMMITTED_WITH_POSTPONE;

	  /* note: this is for nested system operations inside a transaction logical run postpone. this is not really,
	   * fully correct, covering all cases. however it should work for file_tracker_unregister case.
	   *
	   * however, to have a robust recovery in the future, no matter how complicated the nesting of operations,
	   * we should considering saving previous states.
	   */
	}
      else
	{
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

  /* first finish system op postpone (if the case). */
  log_recovery_finish_sysop_postpone (thread_p, tdes);

  if (tdes->state == TRAN_UNACTIVE_WILL_COMMIT || tdes->state == TRAN_UNACTIVE_COMMITTED_WITH_POSTPONE)
    {
      LSA_SET_NULL (&first_postpone_to_apply);

      assert (tdes->is_active_worker_transaction ());
      assert (!LSA_ISNULL (&tdes->rcv.tran_start_postpone_lsa));

      /*
       * The transaction was the one that was committing
       */
      if (tdes->state == TRAN_UNACTIVE_COMMITTED_WITH_POSTPONE)
	{
	  /* make sure to abort interrupted logical postpone. */
	  assert (tdes->topops.last == -1);
	  log_recovery_abort_interrupted_sysop (thread_p, tdes, &tdes->rcv.tran_start_postpone_lsa);
	  assert (tdes->topops.last == -1);
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
  // *INDENT-OFF*
  int i;
  LOG_TDES *tdes_it = NULL;	/* Transaction descriptor */

  /* Finish committing transactions with unfinished postpone actions */
  thread_p = thread_p != NULL ? thread_p : thread_get_thread_entry_info ();
  assert (thread_p->tran_index == LOG_SYSTEM_TRAN_INDEX);

  auto finish_sys_postpone = [&] (LOG_TDES & tdes)
    {
      log_rv_simulate_runtime_worker (thread_p, &tdes);
      log_recovery_finish_postpone (thread_p, &tdes);
      log_rv_end_simulation (thread_p);
    };

  for (i = 1; i < log_Gl.trantable.num_total_indices; i++)
    {
      tdes_it = LOG_FIND_TDES (i);
      if (tdes_it == NULL || tdes_it->trid == NULL_TRANID)
        {
          continue;
        }
      finish_sys_postpone (*tdes_it);
    }
  log_system_tdes::map_all_tdes (finish_sys_postpone);
  // *INDENT-ON*
}

/*
 * log_recovery_abort_all_atomic_sysops () - abort all atomic system operation opened at the moment of the crash
 *
 * return        : void
 * thread_p (in) : thread entry
 */
static void
log_recovery_abort_all_atomic_sysops (THREAD_ENTRY * thread_p)
{
  // *INDENT-OFF*
  int i;
  LOG_TDES *tdes_it = NULL;	/* Transaction descriptor */

  /* Finish committing transactions with unfinished postpone actions */
  thread_p = thread_p != NULL ? thread_p : thread_get_thread_entry_info ();
  assert (thread_p->tran_index == LOG_SYSTEM_TRAN_INDEX);
  auto abort_atomic_func = [&] (LOG_TDES & tdes)
    {
      log_rv_simulate_runtime_worker (thread_p, &tdes);
      log_recovery_abort_atomic_sysop (thread_p, &tdes);
      log_rv_end_simulation (thread_p);
    };
  for (i = 1; i < log_Gl.trantable.num_total_indices; i++)
    {
      tdes_it = LOG_FIND_TDES (i);
      if (tdes_it == NULL || tdes_it->trid == NULL_TRANID)
        {
          continue;
        }
      abort_atomic_func (*tdes_it);
    }
  log_system_tdes::map_all_tdes (abort_atomic_func);
  // *INDENT-ON*
}

/*
 * log_recovery_abort_atomic_sysop () - abort all changes down to tdes->rcv.atomic_sysop_start_lsa (where atomic system
 *                                      operation starts).
 *
 * return        : void
 * thread_p (in) : thread entry
 * tdes (in)     : transaction descriptor
 */
static void
log_recovery_abort_atomic_sysop (THREAD_ENTRY * thread_p, LOG_TDES * tdes)
{
  char log_pgbuf[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT], *aligned_log_pgbuf;
  LOG_PAGE *log_pgptr = NULL;
  LOG_RECORD_HEADER *log_rec;
  LOG_LSA prev_atomic_sysop_start_lsa;

  if (tdes == NULL || tdes->trid == NULL_TRANID)
    {
      /* Nothing to do */
      return;
    }

  if (LSA_ISNULL (&tdes->rcv.atomic_sysop_start_lsa))
    {
      /* no atomic system operation */
      return;
    }
  if (LSA_GE (&tdes->rcv.atomic_sysop_start_lsa, &tdes->undo_nxlsa))
    {
      /* nothing after tdes->rcv.atomic_sysop_start_lsa */
      assert (LSA_EQ (&tdes->rcv.atomic_sysop_start_lsa, &tdes->undo_nxlsa));
      LSA_SET_NULL (&tdes->rcv.atomic_sysop_start_lsa);
      er_log_debug (ARG_FILE_LINE, "(trid = %d) Nothing after atomic sysop (%lld|%d), nothing to rollback.\n",
		    tdes->trid, LSA_AS_ARGS (&tdes->rcv.atomic_sysop_start_lsa));
      return;
    }
  assert (tdes->topops.last <= 0);

  if (tdes->state == TRAN_UNACTIVE_TOPOPE_COMMITTED_WITH_POSTPONE
      && LSA_GT (&tdes->rcv.sysop_start_postpone_lsa, &tdes->rcv.atomic_sysop_start_lsa))
    {
      /* we have (maybe) the next case:
       *
       * 1. atomic operation was started.
       * 2. nested system operation is started.
       * 3. nested operation has postpones.
       * 4. nested operation is committed with postpone.
       * 5. crash
       *
       * I am not sure this really happens. It might, and it might be risky to allow it. However, I assume this nested
       * operation will not do other complicated operations to mess with other logical operations. I hope at least. */
      /* finish postpone of nested system op. */
      er_log_debug (ARG_FILE_LINE,
		    "(trid = %d) Nested sysop start pospone (%lld|%d) inside atomic sysop (%lld|%d). \n", tdes->trid,
		    LSA_AS_ARGS (&tdes->rcv.sysop_start_postpone_lsa), LSA_AS_ARGS (&tdes->rcv.atomic_sysop_start_lsa));
      log_recovery_finish_sysop_postpone (thread_p, tdes);
    }
  else if (tdes->state == TRAN_UNACTIVE_TOPOPE_COMMITTED_WITH_POSTPONE)
    {
      /* this would be also a nested case, but the other way around:
       *
       * 1. system op starts.
       * 2. system op end with postpone.
       * 3. nested atomic system op starts.
       * 4. crash.
       *
       * we first have to abort atomic system op. postpone will be finished later. */
      er_log_debug (ARG_FILE_LINE,
		    "(trid = %d) Nested atomic sysop  (%lld|%d) after sysop start postpone (%lld|%d). \n", tdes->trid,
		    LSA_AS_ARGS (&tdes->rcv.sysop_start_postpone_lsa), LSA_AS_ARGS (&tdes->rcv.atomic_sysop_start_lsa));
    }
  else
    {
      er_log_debug (ARG_FILE_LINE, "(trid = %d) Atomic sysop (%lld|%d). Rollback. \n", tdes->trid,
		    LSA_AS_ARGS (&tdes->rcv.atomic_sysop_start_lsa));
    }

  /* Get transaction lsa that precede atomic_sysop_start_lsa. */
  aligned_log_pgbuf = PTR_ALIGN (log_pgbuf, MAX_ALIGNMENT);
  log_pgptr = (LOG_PAGE *) aligned_log_pgbuf;
  if (logpb_fetch_page (thread_p, &tdes->rcv.atomic_sysop_start_lsa, LOG_CS_FORCE_USE, log_pgptr) != NO_ERROR)
    {
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_recovery_abort_atomic_sysop");
      return;
    }
  log_rec = LOG_GET_LOG_RECORD_HEADER (log_pgptr, &tdes->rcv.atomic_sysop_start_lsa);
  LSA_COPY (&prev_atomic_sysop_start_lsa, &log_rec->prev_tranlsa);

  /* rollback. simulate a new system op */
  log_sysop_start (thread_p);

  /* hack last parent to stop at transaction lsa that precede atomic_sysop_start_lsa. */
  LSA_COPY (&tdes->topops.stack[tdes->topops.last].lastparent_lsa, &prev_atomic_sysop_start_lsa);

  /* rollback */
  log_sysop_abort (thread_p);

  assert (tdes->topops.last <= 0);

  /* this is it. reset tdes->rcv.atomic_sysop_start_lsa and we're done. */
  LSA_SET_NULL (&tdes->rcv.atomic_sysop_start_lsa);
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
  LOG_LSA max_undo_lsa;		/* LSA of log record to undo */
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
  volatile TRANID tran_id;
  volatile LOG_RECTYPE log_rtype;

  aligned_log_pgbuf = PTR_ALIGN (log_pgbuf, MAX_ALIGNMENT);

  /*
   * Remove from the list of transaction to abort, those that have finished
   * when the crash happens, so it does not remain dangling in the transaction
   * table.
   */

  for (tran_index = 1; tran_index < log_Gl.trantable.num_total_indices; tran_index++)
    {
      if ((tdes = LOG_FIND_TDES (tran_index)) != NULL && tdes->trid != NULL_TRANID
	  && (tdes->state == TRAN_UNACTIVE_UNILATERALLY_ABORTED || tdes->state == TRAN_UNACTIVE_ABORTED)
	  && LSA_ISNULL (&tdes->undo_nxlsa))
	{
	  (void) log_complete (thread_p, tdes, LOG_ABORT, LOG_DONT_NEED_NEWTRID, LOG_NEED_TO_WRITE_EOT_LOG);
	  logtb_free_tran_index (thread_p, tran_index);
	}
    }
  // *INDENT-OFF*
  auto delete_func = [] (const LOG_TDES & tdes)
    {
      return LSA_ISNULL (&tdes.undo_nxlsa);
    };
  log_system_tdes::rv_delete_all_tdes_if (delete_func);
  // *INDENT-ON*

  /*
   * GO BACKWARDS, undoing records
   */

  /* Find the largest LSA to undo */
  log_find_unilaterally_largest_undo_lsa (thread_p, max_undo_lsa);

  log_pgptr = (LOG_PAGE *) aligned_log_pgbuf;

  undo_unzip_ptr = log_zip_alloc (LOGAREA_SIZE);
  if (undo_unzip_ptr == NULL)
    {
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_recovery_undo");
      return;
    }

  while (!LSA_ISNULL (&max_undo_lsa))
    {
      /* Fetch the page where the LSA record to undo is located */
      LSA_COPY (&log_lsa, &max_undo_lsa);
      if (logpb_fetch_page (thread_p, &log_lsa, LOG_CS_FORCE_USE, log_pgptr) != NO_ERROR)
	{
	  log_zip_free (undo_unzip_ptr);

	  logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_recovery_undo");
	  return;
	}

      /* Check all log records in this phase */
      while (max_undo_lsa.pageid == log_lsa.pageid)
	{
	  /* Find the log record */
	  log_lsa.offset = max_undo_lsa.offset;
	  log_rec = LOG_GET_LOG_RECORD_HEADER (log_pgptr, &log_lsa);

	  tran_id = log_rec->trid;
	  log_rtype = log_rec->type;

	  LSA_COPY (&prev_tranlsa, &log_rec->prev_tranlsa);

	  if (logtb_is_system_worker_tranid (tran_id))
	    {
	      // *INDENT-OFF*
	      tdes = log_system_tdes::rv_get_tdes (tran_id);
	      if (tdes == NULL)
		{
		  assert (false);
		}
	      // *INDENT-ON*
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
		  logtb_free_tran_index_with_undo_lsa (thread_p, &max_undo_lsa);
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
		      logtb_free_tran_index_with_undo_lsa (thread_p, &max_undo_lsa);
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
			       "      volid = %d, pageid = %d, offset = %hd,\n", LSA_AS_ARGS (&rcv_lsa),
			       rv_rcvindex_string (rcvindex), rcv_vpid.volid, rcv_vpid.pageid, rcv.offset);
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
		case LOG_SYSOP_ATOMIC_START:
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

		  if (logtb_is_system_worker_tranid (tran_id))
		    {
		      // *INDENT-OFF*
		      log_system_tdes::rv_delete_tdes (tran_id);
		      // *INDENT-ON*
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

		  if (logtb_is_system_worker_tranid (tran_id))
		    {
		      // *INDENT-OFF*
		      log_system_tdes::rv_delete_tdes (tran_id);
		      // *INDENT-ON*
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

		      if (logtb_is_system_worker_tranid (tran_id))
			{
			  // *INDENT-OFF*
			  log_system_tdes::rv_delete_tdes (tran_id);
			  // *INDENT-ON*
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
		    }
		}
	    }

	  /* Find the next log record to undo */
	  log_find_unilaterally_largest_undo_lsa (thread_p, max_undo_lsa);
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
    case LOG_2PC_COMMIT_DECISION:
    case LOG_2PC_ABORT_DECISION:
    case LOG_2PC_COMMIT_INFORM_PARTICPS:
    case LOG_2PC_ABORT_INFORM_PARTICPS:
    case LOG_DUMMY_HEAD_POSTPONE:
    case LOG_DUMMY_CRASH_RECOVERY:
    case LOG_DUMMY_OVF_RECORD:
    case LOG_DUMMY_GENERIC:
    case LOG_END_OF_LOG:
    case LOG_SYSOP_ATOMIC_START:
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
log_rv_undoredo_record_partial_changes (THREAD_ENTRY * thread_p, char *rcv_data, int rcv_data_length,
					RECDES * record, bool is_undo)
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
log_rv_redo_record_modify (THREAD_ENTRY * thread_p, const LOG_RCV * rcv)
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
log_rv_undo_record_modify (THREAD_ENTRY * thread_p, const LOG_RCV * rcv)
{
  return log_rv_record_modify_internal (thread_p, rcv, true);
}

/*
 * log_rv_record_modify_internal () - Modify one record of database slotted page.
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
log_rv_record_modify_internal (THREAD_ENTRY * thread_p, const LOG_RCV * rcv, bool is_undo)
{
  INT16 flags = rcv->offset & LOG_RV_RECORD_MODIFY_MASK;
  PGSLOTID slotid = rcv->offset & (~LOG_RV_RECORD_MODIFY_MASK);
  RECDES record;
  char data_buffer[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT];
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
      if (spage_get_record (thread_p, rcv->pgptr, slotid, &record, COPY) != S_SUCCESS)
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
 * log_rv_pack_undo_record_changes () - Pack recovery data for undo record
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

  //
  // during recovery, we don't care if a page is deallocated or not, apply the changes regardless. since changes to
  // sector reservation table are applied in parallel with the changes in pages, at times the page may appear to be
  // deallocated (part of an unreserved sector). but the changes were done while the sector was reserved and must be
  // re-applied to get a correct end result.
  // 
  // moreover, the sector reservation check is very expensive. running this check on every page fix costs much more
  // than any time gained by skipping redoing changes on deallocated pages.
  //
  // therefore, fix page using RECOVERY_PAGE mode. pgbuf_fix will know to accept even new or deallocated pages.
  //
  page = pgbuf_fix (thread_p, vpid_rcv, RECOVERY_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (page == NULL)
    {
      // this is terrible, because it makes recovery impossible
      assert_release (false);
      return NULL;
    }
  return page;
}

static void
log_rv_simulate_runtime_worker (THREAD_ENTRY * thread_p, LOG_TDES * tdes)
{
  assert (thread_p != NULL);
  if (tdes->is_active_worker_transaction ())
    {
      thread_p->tran_index = tdes->tran_index;
#if defined (SA_MODE)
      LOG_SET_CURRENT_TRAN_INDEX (thread_p, tdes->tran_index);
#endif // SA_MODE
    }
  else if (tdes->is_system_worker_transaction ())
    {
      log_system_tdes::rv_simulate_system_tdes (tdes->trid);
    }
  else
    {
      assert (false);
    }
}

static void
log_rv_end_simulation (THREAD_ENTRY * thread_p)
{
  assert (thread_p != NULL);
  thread_p->reset_system_tdes ();
  thread_p->tran_index = LOG_SYSTEM_TRAN_INDEX;
#if defined (SA_MODE)
  LOG_SET_CURRENT_TRAN_INDEX (thread_p, LOG_SYSTEM_TRAN_INDEX);
#endif // SA_MODE
}

/*
 * log_find_unilaterally_largest_undo_lsa - find maximum lsa address to undo
 *
 * return:
 *
 * Note: Find the maximum log sequence address to undo during the undo
 *              crash recovery phase.
 */
void
log_find_unilaterally_largest_undo_lsa (THREAD_ENTRY * thread_p, LOG_LSA & max_undo_lsa)
{
  // *INDENT-OFF*
  int i;
  LOG_TDES *tdes;		/* Transaction descriptor */

  TR_TABLE_CS_ENTER_READ_MODE (thread_p);

  LSA_SET_NULL (&max_undo_lsa);

  auto max_undo_lsa_func = [&] (log_tdes & tdes)
    {
      if (LSA_LT (&max_undo_lsa, &tdes.undo_nxlsa))
        {
          max_undo_lsa = tdes.undo_nxlsa;
        }
    };

  /* Check active transactions. */
  for (i = 0; i < log_Gl.trantable.num_total_indices; i++)
    {
      if (i != LOG_SYSTEM_TRAN_INDEX)
        {
          tdes = log_Gl.trantable.all_tdes[i];
          if (tdes != NULL && tdes->trid != NULL_TRANID
              && (tdes->state == TRAN_UNACTIVE_UNILATERALLY_ABORTED || tdes->state == TRAN_UNACTIVE_ABORTED))
            {
              max_undo_lsa_func (*tdes);
            }
        }
    }
  /* Check system worker transactions. */
  log_system_tdes::map_all_tdes (max_undo_lsa_func);

  TR_TABLE_CS_EXIT (thread_p);
  // *INDENT-ON*
}
