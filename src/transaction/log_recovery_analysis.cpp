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

#include "log_recovery_analysis.hpp"

#include "log_append.hpp"
#include "log_impl.h"
#include "log_manager.h"
#include "log_reader.hpp"
#include "log_record.hpp"
#include "log_recovery.h"
#include "log_recovery_context.hpp"
#include "log_storage.hpp"
#include "log_system_tran.hpp"
#include "log_volids.hpp"
#include "message_catalog.h"
#include "msgcat_set_log.hpp"
#include "server_type.hpp"
#include "system_parameter.h"
#include "util_func.h"

#include <set>

static void log_rv_analysis_handle_fetch_page_fail (THREAD_ENTRY *thread_p, log_recovery_context &context,
    LOG_PAGE *log_page_p, const LOG_RECORD_HEADER *log_rec,
    const log_lsa &prev_lsa, const log_lsa &prev_prev_lsa);
static int log_rv_analysis_undo_redo_internal (THREAD_ENTRY *thread_p, int tran_id, const LOG_LSA *log_lsa, LOG_TDES *&tdes);
static int log_rv_analysis_undo_redo (THREAD_ENTRY *thread_p, int tran_id, const LOG_LSA *log_lsa);
static int log_rv_analysis_mvcc_undo_redo (THREAD_ENTRY *thread_p, int tran_id, LOG_LSA *log_lsa, LOG_PAGE *log_page_p,
    LOG_RECTYPE log_type);
static int log_rv_analysis_dummy_head_postpone (THREAD_ENTRY *thread_p, int tran_id, LOG_LSA *log_lsa);
static int log_rv_analysis_postpone (THREAD_ENTRY *thread_p, int tran_id, LOG_LSA *log_lsa);
static int log_rv_analysis_run_postpone (THREAD_ENTRY *thread_p, int tran_id, LOG_LSA *log_lsa,
    LOG_PAGE *log_page_p);
static int log_rv_analysis_compensate (THREAD_ENTRY *thread_p, int tran_id, LOG_LSA *log_lsa, LOG_PAGE *log_page_p);
static int log_rv_analysis_commit_with_postpone (THREAD_ENTRY *thread_p, int tran_id, LOG_LSA *log_lsa,
    LOG_PAGE *log_page_p);
static int log_rv_analysis_sysop_start_postpone (THREAD_ENTRY *thread_p, int tran_id, LOG_LSA *log_lsa,
    LOG_PAGE *log_page_p);
static int log_rv_analysis_atomic_sysop_start (THREAD_ENTRY *thread_p, int tran_id, LOG_LSA *log_lsa);
static int log_rv_analysis_complete (THREAD_ENTRY *thread_p, int tran_id, LOG_LSA *log_lsa, LOG_PAGE *log_page_p,
				     LOG_LSA *prev_lsa, log_recovery_context &context);
static int log_rv_analysis_sysop_end (THREAD_ENTRY *thread_p, int tran_id, LOG_LSA *log_lsa, LOG_PAGE *log_page_p);
static int log_rv_analysis_save_point (THREAD_ENTRY *thread_p, int tran_id, LOG_LSA *log_lsa);
static int log_rv_analysis_2pc_prepare (THREAD_ENTRY *thread_p, int tran_id, LOG_LSA *log_lsa);
static int log_rv_analysis_2pc_start (THREAD_ENTRY *thread_p, int tran_id, LOG_LSA *log_lsa);
static int log_rv_analysis_2pc_commit_decision (THREAD_ENTRY *thread_p, int tran_id, LOG_LSA *log_lsa);
static int log_rv_analysis_2pc_abort_decision (THREAD_ENTRY *thread_p, int tran_id, LOG_LSA *log_lsa);
static int log_rv_analysis_2pc_commit_inform_particps (THREAD_ENTRY *thread_p, int tran_id, LOG_LSA *log_lsa);
static int log_rv_analysis_2pc_abort_inform_particps (THREAD_ENTRY *thread_p, int tran_id, LOG_LSA *log_lsa);
static int log_rv_analysis_2pc_recv_ack (THREAD_ENTRY *thread_p, int tran_id, LOG_LSA *log_lsa);
static int log_rv_analysis_log_end (int tran_id, const LOG_LSA *log_lsa, const LOG_PAGE *log_page_p);
static void log_rv_analysis_record (THREAD_ENTRY *thread_p, LOG_RECTYPE log_type, int tran_id, LOG_LSA *log_lsa,
				    LOG_PAGE *log_page_p, LOG_LSA *prev_lsa, log_recovery_context &context);
static void log_rv_analysis_record_on_page_server (THREAD_ENTRY *thread_p, LOG_RECTYPE log_type, int tran_id,
    const LOG_LSA *log_lsa, const LOG_PAGE *log_page_p);
static void log_rv_analysis_record_on_tran_server (THREAD_ENTRY *thread_p, LOG_RECTYPE log_type, int tran_id,
    LOG_LSA *log_lsa, LOG_PAGE *log_page_p, LOG_LSA *prev_lsa,
    log_recovery_context &context);
static bool log_is_page_of_record_broken (THREAD_ENTRY *thread_p, const LOG_LSA *log_lsa,
    const LOG_RECORD_HEADER *log_rec_header);
static void log_recovery_resetlog (THREAD_ENTRY *thread_p, const LOG_LSA *new_append_lsa,
				   const LOG_LSA *new_prev_lsa);
static void log_recovery_notpartof_archives (THREAD_ENTRY *thread_p, int start_arv_num, const char *info_reason);
static int log_recovery_analysis_load_trantable_snapshot (THREAD_ENTRY *thread_p,
    const log_lsa &most_recent_trantable_snapshot_lsa, cublog::checkpoint_info &chkpt_info, log_lsa &snapshot_lsa);
static void log_recovery_build_mvcc_table_from_trantable (THREAD_ENTRY *thread_p);

class corruption_checker
{
    //////////////////////////////////////////////////////////////////////////
    // Does log data sanity checks and saves the results.
    //////////////////////////////////////////////////////////////////////////

  public:
    static constexpr size_t IO_BLOCK_SIZE = 4 * ONE_K;   // 4k

    corruption_checker ();

    // Check if given page checksum is correct. Otherwise page is corrupted
    void check_page_checksum (const LOG_PAGE *log_pgptr);
    // Additional checks on log record:
    void check_log_record (const log_lsa &record_lsa, const log_rec_header &record_header, const LOG_PAGE *log_page_p);
    // Detect the address of first block having only 0xFF bytes
    void find_first_corrupted_block (const LOG_PAGE *log_pgptr);

    bool is_page_corrupted () const;		      // is last checked page corrupted?
    const log_lsa &get_first_corrupted_lsa () const;  // get the address of first block full of 0xFF

  private:

    const char *get_block_ptr (const LOG_PAGE *page, size_t block_index) const;	  // the starting pointer of block

    bool m_is_page_corrupted = false;		      // true if last checked page is corrupted, false otherwise
    LOG_LSA m_first_corrupted_rec_lsa = NULL_LSA;     // set by last find_first_corrupted_block call
    std::unique_ptr<char[]> m_null_block;	      // 4k of 0xFF for null block check
    size_t m_blocks_in_page_count = 0;		      // number of blocks in a log page
};

int
log_rv_analysis_check_page_corruption (THREAD_ENTRY *thread_p, LOG_PAGEID pageid, const LOG_PAGE *log_page_p,
				       corruption_checker &checker)
{
#if !defined(NDEBUG)
  if (prm_get_bool_value (PRM_ID_LOGPB_LOGGING_DEBUG))
    {
      _er_log_debug (ARG_FILE_LINE, "logpb_recovery_analysis: log page %lld, checksum %d\n",
		     log_page_p->hdr.logical_pageid, log_page_p->hdr.checksum);
      fileio_page_hexa_dump ((const char *) log_page_p, LOG_PAGESIZE);
    }
#endif /* !NDEBUG */

  checker.check_page_checksum (log_page_p);
  if (checker.is_page_corrupted ())
    {
      if (logpb_is_page_in_archive (pageid))
	{
	  /* Should not happen. */
	  logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_rv_analysis_check_page_corruption");
	  return ER_FAILED;
	}

      checker.find_first_corrupted_block (log_page_p);

      /* Found corrupted log page. */
      if (prm_get_bool_value (PRM_ID_LOGPB_LOGGING_DEBUG))
	{
	  _er_log_debug (ARG_FILE_LINE, "logpb_recovery_analysis: log page %lld is corrupted due to partial flush"
			 " (first_corrupted_rec_lsa = %lld|%d)\n",
			 (long long int) pageid, LSA_AS_ARGS (&checker.get_first_corrupted_lsa ()));
	}
    }
  return NO_ERROR;
}

void
log_recovery_analysis (THREAD_ENTRY *thread_p, INT64 *num_redo_log_records, log_recovery_context &context)
{
  // Navigation LSA's
  LOG_LSA record_nav_lsa = NULL_LSA;		/* LSA used to navigate from one record to the next */
  LOG_LSA log_nav_lsa = NULL_LSA;		/* LSA used to navigate through log pages and the data inside the pages.*/
  LOG_LSA prev_lsa = NULL_LSA;			/* LSA of previous log record */
  LOG_LSA prev_prev_lsa = NULL_LSA;		/* LSA of record previous to previous log record */

  // Two additional log_lsa will be used in the while loop:
  //
  //	- A constant crt_record_lsa, that is used everywhere the LSA of current record can be used. No other variables,
  //	even if they have the same values, should be used as an immutable reference to the current record LSA.
  //	- A next_record_lsa, that is copied from log_rec->forw_lsa and is adjusted if its pageid/offset values are
  //	null.

  // Log page
  char log_pgbuf[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT];
  LOG_PAGE *log_page_p = (LOG_PAGE *) PTR_ALIGN (log_pgbuf, MAX_ALIGNMENT);

  // Current log record
  LOG_RECTYPE log_rtype = LOG_LARGER_LOGREC_TYPE;	/* Log record type */
  LOG_RECORD_HEADER *log_rec = NULL;

  // Stuff used for corruption checks
  corruption_checker checker;

  if (num_redo_log_records != NULL)
    {
      *num_redo_log_records = 0;
    }

  /*
   * Find the committed, aborted, and unilaterally aborted (active) transactions at system crash
   */

  // Start with the record at checkpoint LSA.
  record_nav_lsa = context.get_checkpoint_lsa ();

  // If the recovery start matches a checkpoint, use the checkpoint information.
  const cublog::checkpoint_info *chkpt_infop = log_Gl.m_metainfo.get_checkpoint_info (record_nav_lsa);

  /* Check all log records in this phase */
  while (!LSA_ISNULL (&record_nav_lsa))
    {
      if (record_nav_lsa.pageid != log_nav_lsa.pageid)
	{
	  /* Fetch the page of record */
	  /* We may fetch only if log page not already broken, but is better in this way. */
	  if (logpb_fetch_page (thread_p, &record_nav_lsa, LOG_CS_FORCE_USE, log_page_p) != NO_ERROR)
	    {
	      // unable to fetch the current log page.
	      log_rv_analysis_handle_fetch_page_fail (thread_p, context, log_page_p, log_rec, prev_lsa, prev_prev_lsa);
	      return;
	    }

	  /* If the page changed, check whether is corrupted. */
	  if (log_rv_analysis_check_page_corruption (thread_p, record_nav_lsa.pageid, log_page_p, checker) != NO_ERROR)
	    {
	      return;
	    }
	}
      log_nav_lsa = record_nav_lsa;

      /* If an offset is missing, it is because an incomplete log record was archived. This log_record was completed
       * later. Thus, we have to find the offset by searching for the next log_record in the page
       */
      if (record_nav_lsa.offset == NULL_OFFSET)
	{
	  record_nav_lsa.offset = log_page_p->hdr.offset;
	  if (record_nav_lsa.offset == NULL_OFFSET)
	    {
	      /* Continue with next pageid */
	      if (logpb_is_page_in_archive (record_nav_lsa.pageid))
		{
		  record_nav_lsa.pageid = record_nav_lsa.pageid + 1;
		}
	      else
		{
		  record_nav_lsa.pageid = NULL_PAGEID;
		}
	      continue;
	    }
	}

      // Set-up a constant value for current record LSA to be used onward.
      const LOG_LSA crt_record_lsa = record_nav_lsa;
      assert (crt_record_lsa.pageid != NULL_PAGEID && crt_record_lsa.offset != NULL_OFFSET);

      /* Get the record header */
      log_rec = LOG_GET_LOG_RECORD_HEADER (log_page_p, &crt_record_lsa);

      if (context.is_restore_from_backup ())
	{
	  /* Check also the last log page of current record. We need to check after obtaining log_rec. */
	  if (log_is_page_of_record_broken (thread_p, &crt_record_lsa, log_rec))
	    {
	      /* Needs to reset the log. It is done in the outer loop. Set end_redo and prev used at reset. */
	      prev_prev_lsa = prev_lsa;
	      prev_lsa = crt_record_lsa;
	      context.set_end_redo_lsa (crt_record_lsa);
	      er_log_debug (ARG_FILE_LINE, "logpb_recovery_analysis: broken record at LSA=%lld|%d ",
			    LSA_AS_ARGS (&crt_record_lsa));
	      log_rv_analysis_handle_fetch_page_fail (thread_p, context, log_page_p, log_rec, prev_lsa,
						      prev_prev_lsa);
	      return;
	    }
	}

      /* Check whether null LSA is reached. */
      checker.check_log_record (crt_record_lsa, *log_rec, log_page_p);

      if (checker.is_page_corrupted () && !checker.get_first_corrupted_lsa ().is_null ())
	{
	  /* If the record is corrupted - it resides in a corrupted block, then
	   * resets append lsa to last valid address and stop.
	   */
	  if (crt_record_lsa > checker.get_first_corrupted_lsa ())
	    {
	      LOG_RESET_APPEND_LSA (&context.get_end_redo_lsa ());
	      break;
	    }
	  else
	    {
	      bool is_log_lsa_corrupted = false;

	      if (crt_record_lsa == checker.get_first_corrupted_lsa ()
		  || log_rec->forw_lsa > checker.get_first_corrupted_lsa ())
		{
		  /* When log_lsa = first_corrupted_rec_lsa, forw_lsa may be NULL. */
		  is_log_lsa_corrupted = true;
		}
	      else
		{
		  /* Check correctness of information from log header. */
		  LOG_LSA end_of_header_lsa = crt_record_lsa;
		  end_of_header_lsa.offset += sizeof (LOG_RECORD_HEADER);
		  end_of_header_lsa.offset = DB_ALIGN (end_of_header_lsa.offset, MAX_ALIGNMENT);
		  if (end_of_header_lsa.offset > LOGAREA_SIZE || end_of_header_lsa > checker.get_first_corrupted_lsa ())
		    {
		      is_log_lsa_corrupted = true;
		    }
		}

	      if (is_log_lsa_corrupted)
		{
		  if (prm_get_bool_value (PRM_ID_LOGPB_LOGGING_DEBUG))
		    {
		      _er_log_debug (ARG_FILE_LINE, "logpb_recovery_analysis: Partial page flush - "
				     "first corrupted log record LSA = (%lld, %d)\n",
				     (long long int) crt_record_lsa.pageid, crt_record_lsa.offset);
		    }
		  LOG_RESET_APPEND_LSA (&crt_record_lsa);
		  break;
		}
	    }
	}

      const TRANID tran_id = log_rec->trid;
      log_rtype = log_rec->type;

      /* Save the address of last redo log record. Get the address of next log record to scan */
      context.set_end_redo_lsa (crt_record_lsa);
      LOG_LSA next_record_lsa = log_rec->forw_lsa;

      if (checker.is_page_corrupted () && (log_rtype != LOG_END_OF_LOG)
	  && (next_record_lsa.pageid != crt_record_lsa.pageid))
	{
	  /* The page is corrupted, do not allow to advance to the next page. */
	  LSA_SET_NULL (&next_record_lsa);
	}

      /*
       * If the next page is NULL_PAGEID and the current page is an archive page, this is not the end of the log.
       * This situation happens when an incomplete log record is archived. Thus, its forward address is NULL.
       * Note that we have to set next_record_lsa.pageid here since the crt_record_lsa.pageid value can be changed
       * (e.g., the log record is stored in two pages: an archive page, and an active page). Later, we try to modify
       * it whenever is possible.
       */

      if (LSA_ISNULL (&next_record_lsa) && (log_rtype != LOG_END_OF_LOG) && logpb_is_page_in_archive (crt_record_lsa.pageid))
	{
	  next_record_lsa.pageid = crt_record_lsa.pageid + 1;
	}

      if (!LSA_ISNULL (&next_record_lsa) && crt_record_lsa.pageid != NULL_PAGEID && next_record_lsa <= crt_record_lsa)
	{
	  /* It seems to be a system error. Maybe a loop in the log */
	  er_log_debug (ARG_FILE_LINE,
			"log_recovery_analysis: ** System error: It seems to be a loop in the log\n."
			" Current log_rec at %lld|%d. Next log_rec at %lld|%d\n", (long long int) crt_record_lsa.pageid,
			crt_record_lsa.offset, (long long int) next_record_lsa.pageid, next_record_lsa.offset);
	  logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_recovery_analysis");
	  return;
	}

      if (LSA_ISNULL (&next_record_lsa) && log_rtype != LOG_END_OF_LOG && context.is_restore_incomplete () == false)
	{
	  LOG_RESET_APPEND_LSA (&context.get_end_redo_lsa ());
	  if (log_startof_nxrec (thread_p, &log_Gl.hdr.append_lsa, true) == NULL)
	    {
	      /* We may destroy a record */
	      LOG_RESET_APPEND_LSA (&context.get_end_redo_lsa ());
	    }
	  else
	    {
	      LOG_RESET_APPEND_LSA (&log_Gl.hdr.append_lsa);

	      /*
	       * Reset the forward address of current record to next record,
	       * and then flush the page.
	       */
	      LSA_COPY (&log_rec->forw_lsa, &log_Gl.hdr.append_lsa);

	      assert (crt_record_lsa.pageid == log_page_p->hdr.logical_pageid);
	      logpb_write_page_to_disk (thread_p, log_page_p, crt_record_lsa.pageid);
	    }
	  er_log_debug (ARG_FILE_LINE,
			"log_recovery_analysis: ** WARNING: An end of the log record was not found."
			" Will Assume = %lld|%d and Next Trid = %d\n",
			(long long int) log_Gl.hdr.append_lsa.pageid, log_Gl.hdr.append_lsa.offset, tran_id);
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

      if (chkpt_infop != nullptr && chkpt_infop->get_snapshot_lsa () == crt_record_lsa)
	{
	  // The transaction table snapshot was taken before the next log record was logged.
	  // Rebuild the transaction table image based on checkpoint information
	  LOG_LSA start_redo_lsa = NULL_LSA;
	  chkpt_infop->recovery_analysis (thread_p, start_redo_lsa);
	  assert (is_tran_server_with_remote_storage () || !start_redo_lsa.is_null ());
	  context.set_start_redo_lsa (start_redo_lsa);
	}

      log_rv_analysis_record (thread_p, log_rtype, tran_id, &log_nav_lsa, log_page_p, &prev_lsa, context);
      if (context.is_restore_incomplete ())
	{
	  break;
	}
      if (context.get_end_redo_lsa () == next_record_lsa)
	{
	  assert_release (context.get_end_redo_lsa () != next_record_lsa);
	  break;
	}
      if (checker.is_page_corrupted () && (log_rtype == LOG_END_OF_LOG))
	{
	  /* The page is corrupted. Stop if end of log was found in page. In this case,
	   * the remaining data in log page is corrupted. If end of log is not found,
	   * then we will advance up to NULL LSA. It is important to initialize page with -1.
	   * Another option may be to store the previous LSA in header page.
	   * Or, to use checksum on log records, but this may slow down the system.
	   */
	  break;
	}

      prev_prev_lsa = prev_lsa;
      prev_lsa = context.get_end_redo_lsa ();

      /*
       * We can fix the lsa.pageid in the case of log_records without forward
       * address at this moment.
       */
      if (next_record_lsa.offset == NULL_OFFSET && next_record_lsa.pageid != NULL_PAGEID
	  && next_record_lsa.pageid < crt_record_lsa.pageid)
	{
	  next_record_lsa.pageid = crt_record_lsa.pageid;
	}

      record_nav_lsa = next_record_lsa;
    }

  if (chkpt_infop != nullptr)
    {
      chkpt_infop->recovery_2pc_analysis (thread_p);
    }

  log_Gl.mvcc_table.reset_start_mvccid ();

  if (prm_get_bool_value (PRM_ID_LOGPB_LOGGING_DEBUG))
    {
      _er_log_debug (ARG_FILE_LINE, "log_recovery_analysis: end of analysis phase, append_lsa = (%lld|%d) \n",
		     (long long int) log_Gl.hdr.append_lsa.pageid, log_Gl.hdr.append_lsa.offset);
    }

  return;
}

static void
log_rv_analysis_handle_fetch_page_fail (THREAD_ENTRY *thread_p, log_recovery_context &context, LOG_PAGE *log_page_p,
					const LOG_RECORD_HEADER *log_rec, const log_lsa &prev_lsa,
					const log_lsa &prev_prev_lsa)
{
  if (context.is_restore_from_backup ())
    {
      context.set_forced_restore_stop ();

#if !defined(NDEBUG)
      if (prm_get_bool_value (PRM_ID_LOG_TRACE_DEBUG))
	{
	  fprintf (stdout, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_LOG, MSGCAT_LOG_STARTS));
	  fprintf (stdout,
		   msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_LOG,
				   MSGCAT_LOG_INCOMPLTE_MEDIA_RECOVERY), context.get_end_redo_lsa ().pageid,
		   context.get_end_redo_lsa ().offset, "???...\n");
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
	      if (prm_get_bool_value (PRM_ID_LOGPB_LOGGING_DEBUG))
		{
		  _er_log_debug (ARG_FILE_LINE, "logpb_recovery_analysis: trid = %d, tail_lsa=%lld|%d\n",
				 log_rec->trid, last_log_tdes->tail_lsa.pageid, last_log_tdes->tail_lsa.offset);
		}
	    }
	}

      assert (!prev_lsa.is_null ());
      if (logpb_fetch_page (thread_p, &prev_lsa, LOG_CS_FORCE_USE, log_page_p) != NO_ERROR)
	{
	  logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "reset log is impossible");
	  return;
	}
      log_recovery_resetlog (thread_p, &prev_lsa, &prev_prev_lsa);

      log_Gl.mvcc_table.reset_start_mvccid ();
    }
  else
    {
      if (er_errid () == ER_TDE_CIPHER_IS_NOT_LOADED)
	{
	  /* TDE Module has to be loaded because there are some TDE-encrypted log pages */
	  logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
			     "log_recovery_analysis: log page %lld has been encrypted (TDE) and cannot be decrypted",
			     log_page_p->hdr.logical_pageid);
	}
      else
	{
	  logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_recovery_analysis");
	}
    }
}

static int
log_rv_analysis_undo_redo_internal (THREAD_ENTRY *thread_p, int tran_id, const LOG_LSA *log_lsa, LOG_TDES *&tdes)
{
  assert (tdes == nullptr);

  /*
   * If this is the first time, the transaction is seen. Assign a new
   * index to describe it and assume that the transaction was active
   * at the time of the crash, and thus it will be unilaterally
   * aborted. The truth of this statement will be find reading the
   * rest of the log
   */
  tdes = logtb_rv_find_allocate_tran_index (thread_p, tran_id, log_lsa);
  if (tdes == NULL)
    {
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_rv_analysis_undo_redo");
      return ER_FAILED;
    }

  /* New tail and next to undo */
  LSA_COPY (&tdes->tail_lsa, log_lsa);
  LSA_COPY (&tdes->undo_nxlsa, &tdes->tail_lsa);

  return NO_ERROR;
}

/*
 * log_rv_analysis_undo_redo - recovery analysis for [diff] undo and/or redo log records
 *
 * return: error code
 *
 *   tran_id(in):
 *   lsa(in/out):
 * Note:
 */
static int
log_rv_analysis_undo_redo (THREAD_ENTRY *thread_p, int tran_id, const LOG_LSA *log_lsa)
{
  LOG_TDES *tdes = nullptr;
  return log_rv_analysis_undo_redo_internal (thread_p, tran_id, log_lsa, tdes);
}

/*
 * log_rv_analysis_mvcc_undo_redo - recovery analysis for mvcc [diff] undo and/or redo log records
 *
 * return: error code
 *
 *   tran_id(in):
 *   lsa(in/out):
 * Note:
 */
static int
log_rv_analysis_mvcc_undo_redo (THREAD_ENTRY *thread_p, int tran_id, LOG_LSA *log_lsa, LOG_PAGE *log_page_p,
				LOG_RECTYPE log_type)
{
  LOG_TDES *tdes = nullptr;
  int error_code = log_rv_analysis_undo_redo_internal (thread_p, tran_id, log_lsa, tdes);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }
  assert (tdes != nullptr);

  // MVCC handling
  tdes->last_mvcc_lsa = *log_lsa;

  // assign transaction mvccid from log record to transaction descriptor
  assert (log_page_p != nullptr);

  // move read pointer past the log header which is actually read upper in the stack
  LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_RECORD_HEADER), log_lsa, log_page_p);
  switch (log_type)
    {
    case LOG_MVCC_UNDOREDO_DATA:
    case LOG_MVCC_DIFF_UNDOREDO_DATA:
    {
      // align to read the specific record info
      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (LOG_REC_MVCC_UNDOREDO), log_lsa, log_page_p);
      const LOG_REC_MVCC_UNDOREDO *const log_rec
	= (const LOG_REC_MVCC_UNDOREDO *) ((char *)log_page_p->area + log_lsa->offset);
      tdes->mvccinfo.id = log_rec->mvccid;
      break;
    }
    case LOG_MVCC_UNDO_DATA:
    {
      // align to read the specific record info
      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (LOG_REC_MVCC_UNDO), log_lsa, log_page_p);
      const LOG_REC_MVCC_UNDO *const log_rec
	= (const LOG_REC_MVCC_UNDO *) ((char *)log_page_p->area + log_lsa->offset);
      tdes->mvccinfo.id = log_rec->mvccid;
      break;
    }
    case LOG_MVCC_REDO_DATA:
    {
      // align to read the specific record info
      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (LOG_REC_MVCC_REDO), log_lsa, log_page_p);
      const LOG_REC_MVCC_REDO *const log_rec
	= (const LOG_REC_MVCC_REDO *) ((char *)log_page_p->area + log_lsa->offset);
      tdes->mvccinfo.id = log_rec->mvccid;
      break;
    }
    default:
      assert ("other log record not expected to be handled here" == nullptr);
      error_code = ER_FAILED;
    }

  return error_code;
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
log_rv_analysis_dummy_head_postpone (THREAD_ENTRY *thread_p, int tran_id, LOG_LSA *log_lsa)
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
log_rv_analysis_postpone (THREAD_ENTRY *thread_p, int tran_id, LOG_LSA *log_lsa)
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
log_rv_analysis_run_postpone (THREAD_ENTRY *thread_p, int tran_id, LOG_LSA *log_lsa, LOG_PAGE *log_page_p)
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
log_rv_analysis_compensate (THREAD_ENTRY *thread_p, int tran_id, LOG_LSA *log_lsa, LOG_PAGE *log_page_p)
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
log_rv_analysis_commit_with_postpone (THREAD_ENTRY *thread_p, int tran_id, LOG_LSA *log_lsa, LOG_PAGE *log_page_p)
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
log_rv_analysis_sysop_start_postpone (THREAD_ENTRY *thread_p, int tran_id, LOG_LSA *log_lsa, LOG_PAGE *log_page_p)
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
	  logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_rv_analysis_atomic_sysop_start");
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

  if (LSA_LT (&sysop_start_posp->sysop_end.lastparent_lsa, &tdes->rcv.atomic_sysop_start_lsa))
    {
      /* reset tdes->rcv.atomic_sysop_start_lsa */
      LSA_SET_NULL (&tdes->rcv.atomic_sysop_start_lsa);
    }

  return NO_ERROR;
}

/*
 * log_rv_analysis_atomic_sysop_start () - analyze start atomic system operation
 *
 * return        : error code
 * thread_p (in) : thread entry
 * tran_id (in)  : transaction ID
 * log_lsa (in)  : log record LSA. will be used as marker for start system operation.
 */
static int
log_rv_analysis_atomic_sysop_start (THREAD_ENTRY *thread_p, int tran_id, LOG_LSA *log_lsa)
{
  LOG_TDES *tdes;

  tdes = logtb_rv_find_allocate_tran_index (thread_p, tran_id, log_lsa);
  if (tdes == NULL)
    {
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_rv_analysis_sysop_start_postpone");
      return ER_FAILED;
    }

  tdes->tail_lsa = *log_lsa;
  tdes->undo_nxlsa = tdes->tail_lsa;

  /* this is a marker for system operations that need to be atomic. they will be rollbacked before postpone is finished.
   */
  tdes->rcv.atomic_sysop_start_lsa = *log_lsa;

  return NO_ERROR;
}

static int
log_rv_analysis_assigned_mvccid (THREAD_ENTRY *thread_p, int tran_id, LOG_LSA *log_lsa, LOG_PAGE *log_page_p)
{
  LOG_TDES *tdes = logtb_rv_find_allocate_tran_index (thread_p, tran_id, log_lsa);
  if (tdes == nullptr)
    {
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_rv_analysis_sysop_start_postpone");
      return ER_FAILED;
    }

  tdes->last_mvcc_lsa = *log_lsa;

  // move read pointer past the log header which is actually read upper in the stack
  LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_RECORD_HEADER), log_lsa, log_page_p);
  // align to read the specific record info
  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (LOG_REC_ASSIGNED_MVCCID), log_lsa, log_page_p);
  auto rec = (const LOG_REC_ASSIGNED_MVCCID *) (log_page_p->area + log_lsa->offset);
  tdes->mvccinfo.id = rec->mvccid;

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
log_rv_analysis_complete (THREAD_ENTRY *thread_p, int tran_id, LOG_LSA *log_lsa, LOG_PAGE *log_page_p,
			  LOG_LSA *prev_lsa, log_recovery_context &context)
{
  const int tran_index = logtb_find_tran_index (thread_p, tran_id);
  if (!context.is_restore_from_backup ())
    {
      // Recovery after crash
      // The transaction has been fully completed. Therefore, it was not active at the time of the crash.
      if (tran_index != NULL_TRAN_INDEX)
	{
	  // newer quick fix on top of older quick fix: mark the mvccid as completed
	  LOG_TDES *const tdes = LOG_FIND_TDES (tran_index);
	  if (is_passive_transaction_server ())
	    {
	      if (MVCCID_IS_VALID (tdes->mvccinfo.id))
		{
		  log_Gl.mvcc_table.complete_mvcc (tran_index, tdes->mvccinfo.id, true);
		}
	      else
		{
		  assert (LSA_ISNULL (&tdes->last_mvcc_lsa));
		}
	    }
	  // quick fix: reset mvccid.
	  tdes->mvccinfo.id = MVCCID_NULL;
	  logtb_free_tran_index (thread_p, tran_index);
	}
      return NO_ERROR;
    }

  // Restore from backup
  const LOG_LSA record_header_lsa = *log_lsa;

  /* Need to read the donetime record to find out if we need to stop the recovery at this point. */
  LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_RECORD_HEADER), log_lsa, log_page_p);
  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (LOG_REC_DONETIME), log_lsa, log_page_p);

  const LOG_REC_DONETIME *const donetime = (LOG_REC_DONETIME *) ((char *) log_page_p->area + log_lsa->offset);
  const time_t last_at_time = util_msec_to_sec (donetime->at_time);
  if (context.does_restore_stop_before_time (last_at_time))
    {
#if !defined(NDEBUG)
      char time_val[CTIME_MAX];
      if (prm_get_bool_value (PRM_ID_LOG_TRACE_DEBUG))
	{
	  fprintf (stdout, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_LOG, MSGCAT_LOG_STARTS));
	  (void) ctime_r (&last_at_time, time_val);
	  fprintf (stdout,
		   msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_LOG, MSGCAT_LOG_INCOMPLTE_MEDIA_RECOVERY),
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
      log_recovery_resetlog (thread_p, &record_header_lsa, prev_lsa);
      context.set_incomplete_restore ();

      return NO_ERROR;
    }
  else
    {
      // Transaction is completed.
      if (tran_index != NULL_TRAN_INDEX)
	{
	  // newer quick fix on top of older quick fix: mark the mvccid as completed
	  LOG_TDES *const tdes = LOG_FIND_TDES (tran_index);
	  if (is_passive_transaction_server ())
	    {
	      if (MVCCID_IS_VALID (tdes->mvccinfo.id))
		{
		  log_Gl.mvcc_table.complete_mvcc (tran_index, tdes->mvccinfo.id, true);
		}
	      else
		{
		  assert (LSA_ISNULL (&tdes->last_mvcc_lsa));
		}
	    }
	  // quick fix: reset mvccid.
	  tdes->mvccinfo.id = MVCCID_NULL;
	  logtb_free_tran_index (thread_p, tran_index);
	}
      return NO_ERROR;
    }
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
log_rv_analysis_sysop_end (THREAD_ENTRY *thread_p, int tran_id, LOG_LSA *log_lsa, LOG_PAGE *log_page_p)
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
      tdes->rcv.analysis_last_aborted_sysop_lsa = *log_lsa;
      tdes->rcv.analysis_last_aborted_sysop_start_lsa = sysop_end->lastparent_lsa;
      break;

    case LOG_SYSOP_END_COMMIT:
      assert (tdes->state != TRAN_UNACTIVE_COMMITTED_WITH_POSTPONE);
      commit_start_postpone = true;
      break;

    case LOG_SYSOP_END_LOGICAL_MVCC_UNDO:
      tdes->last_mvcc_lsa = tdes->tail_lsa;
    // fall through
    case LOG_SYSOP_END_LOGICAL_UNDO:
      /* todo: I think it will be safer to save previous states in nested system operations, rather than rely on context
       *       to guess it. we should consider that for cherry. */
      commit_start_postpone = true;
      break;

    case LOG_SYSOP_END_LOGICAL_COMPENSATE:
      /* compensate undo */
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
	      if (tdes->topops.max == 0 && logtb_realloc_topops_stack (tdes, 1) == NULL)
		{
		  /* Out of memory */
		  logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_recovery_analysis");
		  return ER_OUT_OF_VIRTUAL_MEMORY;
		}
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
      // topops stack is bumped when system operation start postpone is found.
      assert (tdes->topops.last == 0);
      if (commit_start_postpone)
	{
	  /* change state to previous state, which is either TRAN_UNACTIVE_COMMITTED_WITH_POSTPONE or
	   * TRAN_UNACTIVE_UNILATERALLY_ABORTED. Use tdes->rcv.tran_start_postpone_lsa to determine which case it is. */
	  if (!LSA_ISNULL (&tdes->rcv.tran_start_postpone_lsa))
	    {
	      /* this must be after start postpone */
	      assert (LSA_LE (&tdes->rcv.tran_start_postpone_lsa, &sysop_end->lastparent_lsa));
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

  // if this is the end of atomic system operation or system operation postpone phase, now it is time to reset it
  //
  // NOTE - we might actually be in both a system operation postpone phase and an atomic system operation, one nested
  //        in the other. we need to check which is last and end sysop should belong to that.
  //
  // NOTE - I really hate this guessing state system and we really, really should consider a more deterministic way.
  //        Logging ALL started system operations and replicating the system operation stack precisely would really
  //        help us avoiding all these ambiguities.
  //

  // do we reset atomic sysop? next conditions must be met:
  // 1. is there atomic system operation started?
  // 2. is atomic system operation more recent than start postpone?
  // 3. is atomic system operation equal or more recent to system operation last parent?
  if (!LSA_ISNULL (&tdes->rcv.atomic_sysop_start_lsa)	/* 1 */
      && LSA_GT (&tdes->rcv.atomic_sysop_start_lsa, &tdes->rcv.sysop_start_postpone_lsa)	/* 2 */
      && LSA_GT (&tdes->rcv.atomic_sysop_start_lsa, &sysop_end->lastparent_lsa) /* 3 */ )
    {
      /* reset tdes->rcv.atomic_sysop_start_lsa */
      LSA_SET_NULL (&tdes->rcv.atomic_sysop_start_lsa);
    }
  // do we reset sysop start postpone? next conditions must be met:
  // 1. is there system operation start postpone in progress?
  // 2. is system operation start postpone more recent than atomic system operation?
  // 3. is system operation start postpone more recent than system operation last parent?
  if (!LSA_ISNULL (&tdes->rcv.sysop_start_postpone_lsa)
      && LSA_GT (&tdes->rcv.sysop_start_postpone_lsa, &tdes->rcv.atomic_sysop_start_lsa)
      && LSA_GT (&tdes->rcv.sysop_start_postpone_lsa, &sysop_end->lastparent_lsa))
    {
      /* reset tdes->rcv.sysop_start_postpone_lsa */
      LSA_SET_NULL (&tdes->rcv.sysop_start_postpone_lsa);
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
log_rv_analysis_save_point (THREAD_ENTRY *thread_p, int tran_id, LOG_LSA *log_lsa)
{
  LOG_TDES *tdes;

  /*
   * If this is the first time, the transaction is seen. Assign a new
   * index to describe it and assume that the transaction was active
   * at the time of the crash, and thus it will be unilaterally
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
log_rv_analysis_2pc_prepare (THREAD_ENTRY *thread_p, int tran_id, LOG_LSA *log_lsa)
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
log_rv_analysis_2pc_start (THREAD_ENTRY *thread_p, int tran_id, LOG_LSA *log_lsa)
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
log_rv_analysis_2pc_commit_decision (THREAD_ENTRY *thread_p, int tran_id, LOG_LSA *log_lsa)
{
  LOG_TDES *tdes;

  /*
   * If this is the first time, the transaction is seen. Assign a new
   * index to describe it. The transaction was part of the two phase
   * commit process. A commit decision has been agreed.
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
log_rv_analysis_2pc_abort_decision (THREAD_ENTRY *thread_p, int tran_id, LOG_LSA *log_lsa)
{
  LOG_TDES *tdes;

  /*
   * If this is the first time, the transaction is seen. Assign a new
   * index to describe it. The transaction was part of the two phase
   * commit process. An abort decision has been decided.
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
log_rv_analysis_2pc_commit_inform_particps (THREAD_ENTRY *thread_p, int tran_id, LOG_LSA *log_lsa)
{
  LOG_TDES *tdes;

  /*
   * If this is the first time, the transaction is seen. Assign a new
   * index to describe it. The transaction was part of the two phase
   * commit process. A commit decision has been agreed and the
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
log_rv_analysis_2pc_abort_inform_particps (THREAD_ENTRY *thread_p, int tran_id, LOG_LSA *log_lsa)
{
  LOG_TDES *tdes;

  /*
   * If this is the first time, the transaction is seen. Assign a new
   * index to describe it. The transaction was part of the two phase
   * commit process. An abort decision has been decided and the
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
log_rv_analysis_2pc_recv_ack (THREAD_ENTRY *thread_p, int tran_id, LOG_LSA *log_lsa)
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
log_rv_analysis_log_end (int tran_id, const LOG_LSA *log_lsa, const LOG_PAGE *log_page_p)
{
  assert (log_page_p != nullptr);
  if ((is_tran_server_with_remote_storage () && is_active_transaction_server ())
      || (!is_tran_server_with_remote_storage () && !logpb_is_page_in_archive (log_lsa->pageid)))
    {
      /* Reset the log header for the recovery undo operation.
       * Do not execute routine on the passive transaction server because LSA's are initialized
       * by copying a consistent snapshot of them from the page server. */

      LOG_RESET_APPEND_LSA (log_lsa);

      // log page pointer passed here is that of the last log page - the append page
      assert (!log_Gl.hdr.append_lsa.is_null ());
      const LOG_RECORD_HEADER *const log_append_log_header =
	      (const LOG_RECORD_HEADER *) (log_page_p->area + log_Gl.hdr.append_lsa.offset);
      LOG_RESET_PREV_LSA (&log_append_log_header->back_lsa);

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
log_rv_analysis_record (THREAD_ENTRY *thread_p, LOG_RECTYPE log_type, int tran_id, LOG_LSA *log_lsa,
			LOG_PAGE *log_page_p, LOG_LSA *prev_lsa, log_recovery_context &context)
{
  if (context.is_page_server ())
    {
      log_rv_analysis_record_on_page_server (thread_p, log_type, tran_id, log_lsa, log_page_p);
    }
  else
    {
      log_rv_analysis_record_on_tran_server (thread_p, log_type, tran_id, log_lsa, log_page_p, prev_lsa, context);
    }
}

//
// log_rv_analysis_record_on_page_server - find the end of log
//
static void
log_rv_analysis_record_on_page_server (THREAD_ENTRY *thread_p, LOG_RECTYPE log_type, int tran_id,
				       const LOG_LSA *log_lsa, const LOG_PAGE *log_page_p)
{
  if (log_type == LOG_END_OF_LOG)
    {
      log_rv_analysis_log_end (tran_id, log_lsa, log_page_p);
    }
}

//
// log_rv_analysis_record_on_tran_server - rebuild the transaction table when the server stopped unexpectedly
//
static void
log_rv_analysis_record_on_tran_server (THREAD_ENTRY *thread_p, LOG_RECTYPE log_type, int tran_id, LOG_LSA *log_lsa,
				       LOG_PAGE *log_page_p, LOG_LSA *prev_lsa, log_recovery_context &context)
{
  switch (log_type)
    {
    case LOG_MVCC_UNDOREDO_DATA:
    case LOG_MVCC_DIFF_UNDOREDO_DATA:
    case LOG_MVCC_UNDO_DATA:
    case LOG_MVCC_REDO_DATA:
      (void) log_rv_analysis_mvcc_undo_redo (thread_p, tran_id, log_lsa, log_page_p, log_type);
      break;
    case LOG_UNDOREDO_DATA:
    case LOG_DIFF_UNDOREDO_DATA:
    case LOG_UNDO_DATA:
    case LOG_REDO_DATA:
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
      (void) log_rv_analysis_run_postpone (thread_p, tran_id, log_lsa, log_page_p);
      break;

    case LOG_COMPENSATE:
      (void) log_rv_analysis_compensate (thread_p, tran_id, log_lsa, log_page_p);
      break;

    case LOG_COMMIT_WITH_POSTPONE:
      (void) log_rv_analysis_commit_with_postpone (thread_p, tran_id, log_lsa, log_page_p);
      break;

    case LOG_SYSOP_START_POSTPONE:
      (void) log_rv_analysis_sysop_start_postpone (thread_p, tran_id, log_lsa, log_page_p);
      break;

    case LOG_COMMIT:
    case LOG_ABORT:
      (void) log_rv_analysis_complete (thread_p, tran_id, log_lsa, log_page_p, prev_lsa, context);
      break;

    case LOG_SYSOP_END:
      log_rv_analysis_sysop_end (thread_p, tran_id, log_lsa, log_page_p);
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
      (void) log_rv_analysis_log_end (tran_id, log_lsa, log_page_p);
      break;

    case LOG_SYSOP_ATOMIC_START:
      (void) log_rv_analysis_atomic_sysop_start (thread_p, tran_id, log_lsa);
      break;

    case LOG_ASSIGNED_MVCCID:
      (void) log_rv_analysis_assigned_mvccid (thread_p, tran_id, log_lsa, log_page_p);
      break;

    case LOG_DUMMY_CRASH_RECOVERY:
    case LOG_REPLICATION_DATA:
    case LOG_REPLICATION_STATEMENT:
    case LOG_DUMMY_HA_SERVER_STATE:
    case LOG_DUMMY_OVF_RECORD:
    case LOG_DUMMY_GENERIC:
    case LOG_SUPPLEMENTAL_INFO:
    case LOG_START_ATOMIC_REPL:
    case LOG_END_ATOMIC_REPL:
    case LOG_TRANTABLE_SNAPSHOT:
      break;

    case LOG_SMALLER_LOGREC_TYPE:
    case LOG_LARGER_LOGREC_TYPE:
    default:
#if defined(CUBRID_DEBUG)
      er_log_debug (ARG_FILE_LINE,
		    "log_recovery_analysis: Unknown record type = %d (%s) ... May be a system error\n", log_rtype,
		    log_to_string (log_rtype));
#endif /* CUBRID_DEBUG */
      /* If we are here, probably the log is corrupted.  */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_PAGE_CORRUPTED, 1, log_lsa->pageid);
      assert (false);
      break;
    }
}

/*
 * log_is_page_of_record_broken - check last page of the record
 *
 * return: true, if last page of the record is broken. false, if it is sane
 *
 *   log_lsa(in): Log record address
 *   log_rec_header(in): Log record header
 */
static bool
log_is_page_of_record_broken (THREAD_ENTRY *thread_p, const LOG_LSA *log_lsa,
			      const LOG_RECORD_HEADER *log_rec_header)
{
  char fwd_log_pgbuf[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT];
  char *fwd_aligned_log_pgbuf;
  LOG_PAGE *log_fwd_page_p;
  LOG_LSA fwd_log_lsa;
  bool is_log_page_broken = false;

  assert (log_lsa != NULL && log_rec_header != NULL);

  fwd_aligned_log_pgbuf = PTR_ALIGN (fwd_log_pgbuf, MAX_ALIGNMENT);
  log_fwd_page_p = (LOG_PAGE *) fwd_aligned_log_pgbuf;

  LSA_COPY (&fwd_log_lsa, &log_rec_header->forw_lsa);

  /* TODO - Do we need to handle NULL fwd_log_lsa? */
  if (!LSA_ISNULL (&fwd_log_lsa))
    {
      assert (fwd_log_lsa.pageid >= log_lsa->pageid);

      if (fwd_log_lsa.pageid != log_lsa->pageid
	  && (fwd_log_lsa.offset != 0 || fwd_log_lsa.pageid > log_lsa->pageid + 1))
	{
	  // The current log record spreads into several log pages.
	  // Check whether the last page of the record exists.
	  if (logpb_fetch_page (thread_p, &fwd_log_lsa, LOG_CS_FORCE_USE, log_fwd_page_p) != NO_ERROR)
	    {
	      /* The forward log page does not exists. */
	      is_log_page_broken = true;
	    }
	}
    }

  return is_log_page_broken;
}

static void
log_recovery_resetlog (THREAD_ENTRY *thread_p, const LOG_LSA *new_append_lsa, const LOG_LSA *new_prev_lsa)
{
  char newappend_pgbuf[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT];
  char *aligned_newappend_pgbuf;
  LOG_PAGE *newappend_pgptr = NULL;
  int arv_num;
  const char *catmsg;
  char *catmsg_dup;
  int ret = NO_ERROR;

  assert (LOG_CS_OWN_WRITE_MODE (thread_p));
  assert (new_prev_lsa != NULL);

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
	   * We need to destroy any log archive created after this point
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

  if (logpb_fetch_start_append_page (thread_p) == NO_ERROR)
    {
      if (newappend_pgptr != NULL && log_Gl.append.log_pgptr != NULL)
	{
	  memcpy ((char *) log_Gl.append.log_pgptr, (char *) newappend_pgptr, LOG_PAGESIZE);
	  logpb_set_dirty (thread_p, log_Gl.append.log_pgptr);
	}
      logpb_flush_pages_direct (thread_p);
    }

  LOG_RESET_PREV_LSA (new_prev_lsa);
  log_Gl.prior_info.check_lsa_consistency ();

  log_Gl.hdr.mvcc_op_log_lsa.set_null ();

  // set a flag that active log was reset; some operations may be affected
  log_Gl.hdr.was_active_log_reset = true;

  logpb_flush_header (thread_p);
  logpb_decache_archive_info (thread_p);

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
log_recovery_notpartof_archives (THREAD_ENTRY *thread_p, int start_arv_num, const char *info_reason)
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

corruption_checker::corruption_checker ()
{
  m_blocks_in_page_count = LOG_PAGESIZE / IO_BLOCK_SIZE;
  m_null_block.reset (new char[IO_BLOCK_SIZE]);
  std::memset (m_null_block.get (), LOG_PAGE_INIT_VALUE, IO_BLOCK_SIZE);
}

void
corruption_checker::check_page_checksum (const LOG_PAGE *log_pgptr)
{
  m_is_page_corrupted = !logpb_page_has_valid_checksum (log_pgptr);
}

void corruption_checker::find_first_corrupted_block (const LOG_PAGE *log_pgptr)
{
  m_first_corrupted_rec_lsa = NULL_LSA;
  for (size_t block_index = 0; block_index < m_blocks_in_page_count; ++block_index)
    {
      if (std::memcmp (get_block_ptr (log_pgptr, block_index), m_null_block.get (), IO_MAX_PAGE_SIZE) == 0)
	{
	  // Found a block full of 0xFF
	  m_first_corrupted_rec_lsa.pageid = log_pgptr->hdr.logical_pageid;
	  m_first_corrupted_rec_lsa.offset =
		  block_index == 0 ? 0 : block_index * IO_MAX_PAGE_SIZE - sizeof (LOG_HDRPAGE);
	}
    }
}

bool
corruption_checker::is_page_corrupted () const
{
  return m_is_page_corrupted;
}

const
log_lsa &corruption_checker::get_first_corrupted_lsa () const
{
  return m_first_corrupted_rec_lsa;
}

const char *
corruption_checker::get_block_ptr (const LOG_PAGE *page, size_t block_index) const
{
  return (reinterpret_cast<const char *> (page)) + block_index * IO_BLOCK_SIZE;
}

void
corruption_checker::check_log_record (const log_lsa &record_lsa, const log_rec_header &record_header,
				      const LOG_PAGE *log_page_p)
{
  if (m_is_page_corrupted)
    {
      // Already found corruption
      return;
    }

  /* For safety reason. Normally, checksum must detect corrupted pages. */
  if (LSA_ISNULL (&record_header.forw_lsa) && record_header.type != LOG_END_OF_LOG
      && !logpb_is_page_in_archive (record_lsa.pageid))
    {
      /* Can't find the end of log. The next log is null. Consider the page corrupted. */
      m_is_page_corrupted = true;
      find_first_corrupted_block (log_page_p);

      er_log_debug (ARG_FILE_LINE,
		    "log_recovery_analysis: ** WARNING: An end of the log record was not found."
		    "Latest log record at lsa = %lld|%d, first_corrupted_lsa = %lld|%d\n",
		    LSA_AS_ARGS (&record_lsa), LSA_AS_ARGS (&m_first_corrupted_rec_lsa));
    }
  else if (record_header.forw_lsa.pageid == record_lsa.pageid)
    {
      /* Quick fix. Sometimes page corruption is not detected. Check whether the current log record
       * is in corrupted block. If true, consider the page corrupted.
       */
      size_t start_block_index = (record_lsa.offset + sizeof (LOG_HDRPAGE) - 1) / IO_BLOCK_SIZE;
      assert (start_block_index <= m_blocks_in_page_count);
      size_t end_block_index = (record_header.forw_lsa.offset + sizeof (LOG_HDRPAGE) - 1) / IO_BLOCK_SIZE;
      assert (end_block_index <= m_blocks_in_page_count);

      if (start_block_index != end_block_index)
	{
	  assert (start_block_index < end_block_index);
	  if (std::memcmp (get_block_ptr (log_page_p, end_block_index), m_null_block.get (), IO_BLOCK_SIZE) == 0)
	    {
	      /* The current record is corrupted - ends into a corrupted block. */
	      m_first_corrupted_rec_lsa = record_lsa;
	      m_is_page_corrupted = true;

	      er_log_debug (ARG_FILE_LINE,
			    "log_recovery_analysis: ** WARNING: An end of the log record was not found."
			    "Latest log record at lsa = %lld|%d, first_corrupted_lsa = %lld|%d\n",
			    LSA_AS_ARGS (&record_lsa), LSA_AS_ARGS (&m_first_corrupted_rec_lsa));
	    }
	}
    }
}

/* log_recovery_analysis_load_trantable_snapshot - starting from a [most recent] trantable snapshot LSA, read
 *                the log record and deserialize its contents as a checkpoint info
 */
static int
log_recovery_analysis_load_trantable_snapshot (THREAD_ENTRY *thread_p,
    const log_lsa &most_recent_trantable_snapshot_lsa, cublog::checkpoint_info &chkpt_info, log_lsa &snapshot_lsa)
{
  assert (!most_recent_trantable_snapshot_lsa.is_null ());

  log_reader lr (LOG_CS_SAFE_READER);
  int log_page_read_err = lr.set_lsa_and_fetch_page (most_recent_trantable_snapshot_lsa);
  if (log_page_read_err != NO_ERROR)
    {
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
			 "log_recovery_analysis_load_trantable_snapshot: error reading trantable snapshot log page");
      return log_page_read_err;
    }

  // always copy because the next add might advance to the next log page
  const log_rec_header log_rec_hdr = lr.reinterpret_copy_and_add_align<log_rec_header> ();
  assert (log_rec_hdr.type == LOG_TRANTABLE_SNAPSHOT);

  lr.advance_when_does_not_fit (sizeof (log_rec_trantable_snapshot));
  const log_rec_trantable_snapshot log_rec = lr.reinterpret_copy_and_add_align<log_rec_trantable_snapshot> ();
  std::unique_ptr<char []> snapshot_data_buf = std::make_unique<char []> (static_cast<size_t> (log_rec.length));
  lr.copy_from_log (snapshot_data_buf.get (), log_rec.length);

  snapshot_lsa = log_rec.snapshot_lsa;

  cubpacking::unpacker unpacker;
  unpacker.set_buffer (snapshot_data_buf.get (), log_rec.length);

  chkpt_info.unpack (unpacker);

  return NO_ERROR;
}

/* log_recovery_build_mvcc_table_from_trantable - build mvcc table using transaction table
 */
static void
log_recovery_build_mvcc_table_from_trantable (THREAD_ENTRY *thread_p)
{
  assert (is_passive_transaction_server ());
  assert (LOG_CS_OWN_WRITE_MODE (thread_p));

  MVCCID smallest_mvccid = std::numeric_limits<MVCCID>::max ();
  MVCCID largest_mvccid = std::numeric_limits<MVCCID>::min ();
  std::set<MVCCID> present_mvccids;
  for (int i = 0; i < log_Gl.trantable.num_total_indices; ++i)
    {
      if (i != LOG_SYSTEM_TRAN_INDEX)
	{
	  const log_tdes *const tdes = log_Gl.trantable.all_tdes[i];
	  if (tdes != nullptr && tdes->trid != NULL_TRANID)
	    {
	      if (tdes->mvccinfo.id < smallest_mvccid)
		{
		  smallest_mvccid = tdes->mvccinfo.id;
		}
	      if (tdes->mvccinfo.id > largest_mvccid)
		{
		  largest_mvccid = tdes->mvccinfo.id;
		}
	      present_mvccids.insert (tdes->mvccinfo.id);
	    }
	}
    }
  log_Gl.hdr.mvcc_next_id = smallest_mvccid;
  log_Gl.mvcc_table.reset_start_mvccid ();

  if (!present_mvccids.empty ())
    {
      // complete each mvccid between the smallest and the highest, that is missing from the table
      std::set<MVCCID>::const_iterator present_mvccids_it = present_mvccids.cbegin ();
      MVCCID prev_mvccid = *present_mvccids_it;
      ++present_mvccids_it;
      for (; present_mvccids_it != present_mvccids.cend (); ++present_mvccids_it)
	{
	  const MVCCID curr_mvccid = *present_mvccids_it;
	  for (MVCCID missing_mvccid = prev_mvccid + 1; missing_mvccid < curr_mvccid; ++missing_mvccid)
	    {
	      log_Gl.mvcc_table.complete_mvcc (LOG_SYSTEM_TRAN_INDEX, missing_mvccid, true);
	    }
	  prev_mvccid = curr_mvccid;
	}
    }

  log_Gl.hdr.mvcc_next_id = largest_mvccid + 1;
}

/* log_recovery_analysis_from_trantable_snapshot - perform recovery for a passive transaction server
 *                  starting from a [recent] transaction table snapshot relayed via log and page server from
 *                  the active transaction server
 *
 * most_recent_trantable_snapshot_lsa (in): the lsa where a record containing, as payload, the packed contents
 *                                of a recent transaction table snapshot; starting from that snapshot, analyze the log
 *                                to construct an actual starting transaction table
 */
void
log_recovery_analysis_from_trantable_snapshot (THREAD_ENTRY *thread_p,
    log_lsa most_recent_trantable_snapshot_lsa)
{
  assert (is_passive_transaction_server ());
  assert (LOG_CS_OWN_WRITE_MODE (thread_p));

  // analysis changes the transaction index and leaves it in an indefinite state
  // therefore reset to system transaction index afterwards;
  // first make sure we're executing on the system thread
  const int sys_tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  assert (sys_tran_index == LOG_SYSTEM_TRAN_INDEX);

  cublog::checkpoint_info chkpt_info;
  log_lsa snapshot_lsa;
  int error = log_recovery_analysis_load_trantable_snapshot (thread_p, most_recent_trantable_snapshot_lsa,
	      chkpt_info, snapshot_lsa);
  if (error != NO_ERROR)
    {
      return;
    }

  log_lsa start_redo_lsa;
  chkpt_info.recovery_analysis (thread_p, start_redo_lsa);

  log_recovery_context log_rcv_context;
  log_rcv_context.init_for_recovery (snapshot_lsa);
  // no recovery is done, start redo lsa may remain invalid
  log_rcv_context.set_start_redo_lsa (NULL_LSA);
  log_rcv_context.set_end_redo_lsa (NULL_LSA);

  // passive transaction server needs to analyze up to the point its replication will pick-up things;
  // that means until the append_lsa; incidentally, end of log record should be found at the current append_lsa, so
  // analysis should stop there

  INT64 dummy_redo_log_record_count = 0LL;
  log_recovery_analysis (thread_p, &dummy_redo_log_record_count, log_rcv_context);
  assert (!log_rcv_context.is_restore_incomplete ());

  // on passive transaction server, the recovery analysis has only the role of bringing the
  // transaction table up to date because it is relevant in read-only results
  log_system_tdes::rv_delete_all_tdes_if ([] (const log_tdes &)
  {
    return true;
  });

  LOG_SET_CURRENT_TRAN_INDEX (thread_p, sys_tran_index);

  log_recovery_build_mvcc_table_from_trantable (thread_p);
}
