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

#include "log_checkpoint_info.hpp"

#include "client_credentials.hpp"
#include "critical_section.h"
#include "log_impl.h"
#include "log_lsa_utils.hpp"
#include "log_manager.h"
#include "log_system_tran.hpp"
#include "memory_alloc.h"
#include "page_buffer.h"
#include "scope_exit.hpp"
#include "system_parameter.h"
#include "thread_entry.hpp"
#include "transaction_global.hpp"

#include <cstring>

namespace cublog
{
  void
  checkpoint_info::pack (cubpacking::packer &serializator) const
  {
    lsa_utils::pack (serializator, m_start_redo_lsa);
    lsa_utils::pack (serializator, m_snapshot_lsa);

    serializator.pack_bigint (m_trans.size ());
    for (const auto &tran_info : m_trans)
      {
	serializator.pack_int (tran_info.isloose_end);
	serializator.pack_int (tran_info.trid);
	serializator.pack_int (tran_info.state);
	lsa_utils::pack (serializator, tran_info.head_lsa);
	lsa_utils::pack (serializator, tran_info.tail_lsa);
	lsa_utils::pack (serializator, tran_info.undo_nxlsa);

	lsa_utils::pack (serializator, tran_info.posp_nxlsa);
	lsa_utils::pack (serializator, tran_info.savept_lsa);
	lsa_utils::pack (serializator, tran_info.tail_topresult_lsa);
	lsa_utils::pack (serializator, tran_info.start_postpone_lsa);
	lsa_utils::pack (serializator, tran_info.last_mvcc_lsa);

	serializator.pack_bigint (tran_info.mvcc_id);
	serializator.pack_bigint (tran_info.mvcc_sub_id);

	serializator.pack_c_string (tran_info.user_name, strlen (tran_info.user_name));
      }

    serializator.pack_bigint (m_sysops.size ());
    for (const auto &sysop_info : m_sysops)
      {
	serializator.pack_int (sysop_info.trid);
	lsa_utils::pack (serializator, sysop_info.sysop_start_postpone_lsa);
	lsa_utils::pack (serializator, sysop_info.atomic_sysop_start_lsa);
      }

    serializator.pack_bool (m_has_2pc);
    serializator.pack_bigint (m_mvcc_next_id);
  }

  void
  checkpoint_info::unpack (cubpacking::unpacker &deserializator)
  {
    lsa_utils::unpack (deserializator, m_start_redo_lsa);
    lsa_utils::unpack (deserializator, m_snapshot_lsa);

    std::uint64_t trans_size = 0;
    deserializator.unpack_bigint (trans_size);
    for (unsigned i = 0; i < trans_size; i++)
      {
	tran_info chkpt_trans;

	deserializator.unpack_int (chkpt_trans.isloose_end);
	deserializator.unpack_int (chkpt_trans.trid);

	deserializator.unpack_from_int (chkpt_trans.state);
	lsa_utils::unpack (deserializator, chkpt_trans.head_lsa);
	lsa_utils::unpack (deserializator, chkpt_trans.tail_lsa);
	lsa_utils::unpack (deserializator, chkpt_trans.undo_nxlsa);

	lsa_utils::unpack (deserializator, chkpt_trans.posp_nxlsa);
	lsa_utils::unpack (deserializator, chkpt_trans.savept_lsa);
	lsa_utils::unpack (deserializator, chkpt_trans.tail_topresult_lsa);
	lsa_utils::unpack (deserializator, chkpt_trans.start_postpone_lsa);
	lsa_utils::unpack (deserializator, chkpt_trans.last_mvcc_lsa);

	deserializator.unpack_bigint (chkpt_trans.mvcc_id);
	deserializator.unpack_bigint (chkpt_trans.mvcc_sub_id);

	deserializator.unpack_c_string (chkpt_trans.user_name, LOG_USERNAME_MAX);

	m_trans.push_back (chkpt_trans);

      }

    std::uint64_t sysop_size = 0;
    deserializator.unpack_bigint (sysop_size);
    for (unsigned i = 0; i < sysop_size; i++)
      {
	sysop_info chkpt_sysop;
	deserializator.unpack_int (chkpt_sysop.trid);
	lsa_utils::unpack (deserializator, chkpt_sysop.sysop_start_postpone_lsa);
	lsa_utils::unpack (deserializator, chkpt_sysop.atomic_sysop_start_lsa);

	m_sysops.push_back (chkpt_sysop);
      }

    deserializator.unpack_bool (m_has_2pc);
    deserializator.unpack_bigint (m_mvcc_next_id);
  }

  size_t
  checkpoint_info::get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const
  {
    size_t size =  0;
    size += lsa_utils::get_packed_size (serializator, start_offset + size);
    size += lsa_utils::get_packed_size (serializator, start_offset + size);

    size += serializator.get_packed_bigint_size (start_offset + size);
    for (const auto &tran_info : m_trans)
      {
	size += serializator.get_packed_int_size (start_offset + size);
	size += serializator.get_packed_int_size (start_offset + size);
	size += serializator.get_packed_int_size (start_offset + size);

	size += lsa_utils::get_packed_size (serializator, start_offset + size);
	size += lsa_utils::get_packed_size (serializator, start_offset + size);
	size += lsa_utils::get_packed_size (serializator, start_offset + size);

	size += lsa_utils::get_packed_size (serializator, start_offset + size);
	size += lsa_utils::get_packed_size (serializator, start_offset + size);
	size += lsa_utils::get_packed_size (serializator, start_offset + size);
	size += lsa_utils::get_packed_size (serializator, start_offset + size);
	size += lsa_utils::get_packed_size (serializator, start_offset + size);

	size += serializator.get_packed_bigint_size (start_offset + size);
	size += serializator.get_packed_bigint_size (start_offset + size);

	size += serializator.get_packed_c_string_size (tran_info.user_name, strlen (tran_info.user_name), start_offset + size);
      }

    size += serializator.get_packed_bigint_size (start_offset + size);
    for (const auto &sysop_info : m_sysops)
      {
	size += serializator.get_packed_int_size (start_offset + size);
	size += lsa_utils::get_packed_size (serializator, start_offset + size);
	size += lsa_utils::get_packed_size (serializator, start_offset + size);
      }

    size += serializator.get_packed_bool_size (start_offset + size);
    size += serializator.get_packed_bigint_size (start_offset + size);

    return size;
  }

  void
  checkpoint_info::load_checkpoint_trans (log_tdes &tdes, LOG_LSA &smallest_lsa,
					  bool &at_least_one_active_transaction_has_valid_mvccid)
  {
    // - snapshot even transactions that do not have a valid tail LSA (ie: transactions that
    //    did not add a log record yet);
    // - this helps cover a scenario for the initialization of a passive transaction server:
    //    - a passive transaction server needs a consistent MVCC table when it starts
    //    - an active transaction might request an MVCCID but might not have yet added a log record
    //      registering that MVCCID (but that MVCCID is in the transaction descriptor already)
    //    - thus, when initializing a passive transaction server that - still "ghost" - MVCCID (but
    //      which will appear later in the log records registered by the transaction) will be
    //      taken into calculation of the MVCC table (see explanation in function
    //      log_recovery_analysis_from_trantable_snapshot)
    // - however, this should also be taken into account when using the transaction table snapshot to
    //    initialize a crashed active transaction server (see recovery_analysis function below)
    //
    if (tdes.trid != NULL_TRANID && tdes.commit_abort_lsa.is_null ())
      {
	m_trans.emplace_back ();
	tran_info &chkpt_tran = m_trans.back ();

	chkpt_tran.isloose_end = tdes.isloose_end;
	chkpt_tran.trid = tdes.trid;
	chkpt_tran.state = tdes.state;
	chkpt_tran.mvcc_id = tdes.mvccinfo.id;

	at_least_one_active_transaction_has_valid_mvccid =
		at_least_one_active_transaction_has_valid_mvccid || MVCCID_IS_NORMAL (tdes.mvccinfo.id);
	if (tdes.mvccinfo.sub_ids.size () == 0)
	  {
	    chkpt_tran.mvcc_sub_id = MVCCID_NULL;
	  }
	else
	  {
	    assert (tdes.mvccinfo.sub_ids.size () == 1);
	    chkpt_tran.mvcc_sub_id = tdes.mvccinfo.sub_ids[0];
	    at_least_one_active_transaction_has_valid_mvccid =
		    at_least_one_active_transaction_has_valid_mvccid || MVCCID_IS_NORMAL (tdes.mvccinfo.sub_ids[0]);
	  }

	LSA_COPY (&chkpt_tran.head_lsa, &tdes.head_lsa);
	LSA_COPY (&chkpt_tran.tail_lsa, &tdes.tail_lsa);
	if (chkpt_tran.state == TRAN_UNACTIVE_ABORTED)
	  {
	    /*
	     * Transaction is in the middle of an abort, since rollback does
	     * is not run in a critical section. Set the undo point to be the
	     * same as its tail. The recovery process will read the last
	     * record which is likely a compensating one, and find where to
	     * continue a rollback operation.
	     */
	    LSA_COPY (&chkpt_tran.undo_nxlsa, &tdes.tail_lsa);
	  }
	else
	  {
	    LSA_COPY (&chkpt_tran.undo_nxlsa, &tdes.undo_nxlsa);
	  }

	LSA_COPY (&chkpt_tran.posp_nxlsa, &tdes.posp_nxlsa);
	LSA_COPY (&chkpt_tran.savept_lsa, &tdes.savept_lsa);
	LSA_COPY (&chkpt_tran.tail_topresult_lsa, &tdes.tail_topresult_lsa);
	LSA_COPY (&chkpt_tran.start_postpone_lsa, &tdes.rcv.tran_start_postpone_lsa);
	chkpt_tran.last_mvcc_lsa = tdes.last_mvcc_lsa;
	std::strncpy (chkpt_tran.user_name, tdes.client.get_db_user (), LOG_USERNAME_MAX);

	if (LSA_ISNULL (&smallest_lsa) || LSA_GT (&smallest_lsa, &tdes.head_lsa))
	  {
	    LSA_COPY (&smallest_lsa, &tdes.head_lsa);
	  }
      }
  }

  void
  checkpoint_info::load_checkpoint_topop (log_tdes &tdes)
  {
    // TODO:
    //  - do the sysops/topops occur only in the context of an active transactions?
    //  - if yes, then does the condition here implies the complete condition in load_checkpoint_trans?
    if (tdes.trid != NULL_TRANID && (!LSA_ISNULL (&tdes.rcv.sysop_start_postpone_lsa)
				     || !LSA_ISNULL (&tdes.rcv.atomic_sysop_start_lsa)))
      {
	/* this transaction is running system operation postpone or an atomic system operation
	 * note: we cannot compare tdes->state with TRAN_UNACTIVE_TOPOPE_COMMITTED_WITH_POSTPONE. we are
	 *       not synchronizing setting transaction state.
	 *       however, setting tdes->rcv.sysop_start_postpone_lsa is protected by
	 *       log_Gl.prior_info.prior_lsa_mutex. so we check this instead of state.
	 */
	m_sysops.emplace_back ();
	sysop_info &chkpt_topop = m_sysops.back ();
	chkpt_topop.trid = tdes.trid;
	chkpt_topop.sysop_start_postpone_lsa = tdes.rcv.sysop_start_postpone_lsa;
	chkpt_topop.atomic_sysop_start_lsa = tdes.rcv.atomic_sysop_start_lsa;
      }
  }

  void
  checkpoint_info::load_trantable_snapshot (THREAD_ENTRY *thread_p, LOG_LSA &smallest_lsa)
  {
    // ENTER the critical section
    TR_TABLE_CS_ENTER (thread_p);
    log_Gl.prior_info.prior_lsa_mutex.lock ();

    scope_exit <std::function<void (void)>> unlock_on_exit ([thread_p] ()
    {
      log_Gl.prior_info.prior_lsa_mutex.unlock ();
      TR_TABLE_CS_EXIT (thread_p);
    });

    // CHECKPOINT THE TRANSACTION TABLE
    bool at_least_one_active_transaction_has_valid_mvccid = false;
    LSA_SET_NULL (&smallest_lsa);
    for (int i = 0; i < log_Gl.trantable.num_total_indices; i++)
      {
	/*
	 * Don't checkpoint current system transaction. That is, the one of
	 * checkpoint process
	 */
	if (i == LOG_SYSTEM_TRAN_INDEX)
	  {
	    continue;
	  }
	LOG_TDES *act_tdes = LOG_FIND_TDES (i);
	assert (act_tdes != nullptr);
	load_checkpoint_trans (*act_tdes, smallest_lsa, at_least_one_active_transaction_has_valid_mvccid);
	load_checkpoint_topop (*act_tdes);
	if (LOG_ISTRAN_2PC (act_tdes))
	  {
	    m_has_2pc = true;
	  }
      }
    assert ((m_trans.size () > 0 && m_sysops.size () <= m_trans.size ())
	    || (m_trans.empty () && m_sysops.empty ()));

    // Checkpoint system transactions' topops
    log_system_tdes::map_func mapper = [this] (log_tdes &tdes)
    {
      load_checkpoint_topop (tdes);
    };
    log_system_tdes::map_all_tdes (mapper);

    m_snapshot_lsa = log_Gl.prior_info.prev_lsa;

    // either no active transaction present or none of the active transactions has an active mvccid;
    // it is needed to relay most recent mvccid as part of the
    if (!at_least_one_active_transaction_has_valid_mvccid)
      {
	// TODO: is any lock needed to ensure consistency of reading the mvccid
	// a client transaction can acquire a new mvccid while not being blocked by the prior lock currently held
	m_mvcc_next_id = log_Gl.hdr.mvcc_next_id;
	assert (MVCCID_IS_VALID (m_mvcc_next_id));
      }
  }

  void
  checkpoint_info::recovery_analysis (THREAD_ENTRY *thread_p, log_lsa &start_redo_lsa,
				      bool recover_empty_transactions) const
  {
    start_redo_lsa = m_start_redo_lsa;

    /* Add the transactions to the transaction table */
    for (const auto &chkpt : m_trans)
      {
	// if tail LSA is missing, the transaction is present in the transaction table
	// but did not yet got to adding a log record; we either need to recover these
	// transaction or not:
	//  - on a passive transaction server, we need these transactions because they might contain
	//    valid MVCCID's already registered with them which are used in constructing a consistent
	//    MVCC table
	//  - on an active transaction server, upon recovery, these transactions offer no useful
	//    information, and can thus be sckipped
	if (chkpt.tail_lsa.is_null ())
	  {
	    if (recover_empty_transactions)
	      {
		// fall through to recover/register empty transactions
	      }
	    else
	      {
		// do not recover/register empty transaction
		continue;
	      }
	  }

	/*
	 * If this is the first time, the transaction is seen. Assign a
	 * new index to describe it and assume that the transaction was
	 * active at the time of the crash, and thus it will be
	 * unilaterally aborted. The truth of this statement will be find
	 * reading the rest of the log
	 */
	// TODO: tail_lsa & commit_abort_lsa condition; symmetric with load_checkpoint_trans
	LOG_TDES *const tdes = logtb_rv_find_allocate_tran_index (thread_p, chkpt.trid, &NULL_LSA);
	if (tdes == NULL)
	  {
	    logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_recovery_analysis");
	    return;
	  }
	/*
	 * Clear the transaction since it may have old stuff in it.
	 * Use the one that is find in the checkpoint record
	 */
	logtb_clear_tdes (thread_p, tdes);

	tdes->isloose_end = chkpt.isloose_end;
	if (chkpt.state == TRAN_ACTIVE || chkpt.state == TRAN_UNACTIVE_ABORTED)
	  {
	    tdes->state = TRAN_UNACTIVE_UNILATERALLY_ABORTED;
	  }
	else
	  {
	    tdes->state = chkpt.state;
	  }
	LSA_COPY (&tdes->head_lsa, &chkpt.head_lsa);
	LSA_COPY (&tdes->tail_lsa, &chkpt.tail_lsa);
	LSA_COPY (&tdes->undo_nxlsa, &chkpt.undo_nxlsa);
	LSA_COPY (&tdes->posp_nxlsa, &chkpt.posp_nxlsa);
	LSA_COPY (&tdes->savept_lsa, &chkpt.savept_lsa);
	LSA_COPY (&tdes->tail_topresult_lsa, &chkpt.tail_topresult_lsa);
	LSA_COPY (&tdes->rcv.tran_start_postpone_lsa, &chkpt.start_postpone_lsa);
	tdes->last_mvcc_lsa = chkpt.last_mvcc_lsa;
	tdes->mvccinfo.id = chkpt.mvcc_id;
	if (chkpt.mvcc_sub_id != MVCCID_NULL)
	  {
	    assert (tdes->mvccinfo.sub_ids.size () == 0);
	    tdes->mvccinfo.sub_ids.emplace_back (chkpt.mvcc_sub_id);
	  }
	tdes->client.set_system_internal_with_user (chkpt.user_name);
      }

    /*
     * Now add top system operations that were in the process of
     * commit to this transactions
     */
    char log_page_buffer[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT];
    LOG_PAGE *log_page_local = (LOG_PAGE *) PTR_ALIGN (log_page_buffer, MAX_ALIGNMENT);
    log_page_local->hdr.logical_pageid = NULL_PAGEID;
    log_page_local->hdr.offset = NULL_OFFSET;

    for (const auto &sysop : m_sysops)
      {
	LOG_TDES *const tdes = logtb_rv_find_allocate_tran_index (thread_p, sysop.trid, &NULL_LSA);
	if (tdes == NULL)
	  {
	    logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_recovery_analysis");
	    return;
	  }

	if (tdes->topops.max == 0 || (tdes->topops.last + 1) >= tdes->topops.max)
	  {
	    if (logtb_realloc_topops_stack (tdes, tdes->topops.last + 1) == NULL)
	      {
		logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_recovery_analysis");
		return;
	      }
	  }

	tdes->rcv.sysop_start_postpone_lsa = sysop.sysop_start_postpone_lsa;
	tdes->rcv.atomic_sysop_start_lsa = sysop.atomic_sysop_start_lsa;
	if (!sysop.sysop_start_postpone_lsa.is_null ())
	  {
	    // Bump the sysop level to save lastparent_lsa and posp_lsa.
	    if (tdes->topops.last == -1)
	      {
		tdes->topops.last++;
	      }
	    else
	      {
		assert (tdes->topops.last == 0);
	      }

	    LOG_LSA log_lsa_local = sysop.sysop_start_postpone_lsa;
	    LOG_REC_SYSOP_START_POSTPONE sysop_start_postpone;
	    const int error_code = log_read_sysop_start_postpone (thread_p, &log_lsa_local, log_page_local, false,
				   &sysop_start_postpone, NULL, NULL, NULL, NULL);
	    if (error_code != NO_ERROR)
	      {
		assert (false);
		return;
	      }
	    tdes->topops.stack[tdes->topops.last].lastparent_lsa = sysop_start_postpone.sysop_end.lastparent_lsa;
	    tdes->topops.stack[tdes->topops.last].posp_lsa = sysop_start_postpone.posp_lsa;
	  }
      }
  }

  void
  checkpoint_info::recovery_2pc_analysis (THREAD_ENTRY *thread_p) const
  {
    if (!m_has_2pc)
      {
	return;
      }

    for (const auto &chkpt : m_trans)
      {
	int tran_index = logtb_find_tran_index (thread_p, chkpt.trid);
	if (tran_index != NULL_TRAN_INDEX)
	  {
	    LOG_TDES *tdes = LOG_FIND_TDES (tran_index);
	    if (tdes != NULL && LOG_ISTRAN_2PC (tdes))
	      {
		log_2pc_recovery_analysis_info (thread_p, tdes, &chkpt.tail_lsa);
	      }
	  }
      }
  }

  size_t
  checkpoint_info::get_transaction_count () const
  {
    return m_trans.size ();
  }

  size_t
  checkpoint_info::get_sysop_count () const
  {
    return m_sysops.size ();
  }

  log_lsa
  checkpoint_info::get_snapshot_lsa () const
  {
    assert (!m_snapshot_lsa.is_null ());

    return m_snapshot_lsa;
  }

  log_lsa
  checkpoint_info::get_start_redo_lsa () const
  {
    return m_start_redo_lsa;
  }

  void
  checkpoint_info::set_start_redo_lsa (const log_lsa &start_redo_lsa)
  {
    m_start_redo_lsa = start_redo_lsa;
  }


  MVCCID
  checkpoint_info::get_mvcc_next_id () const
  {
    return m_mvcc_next_id;
  }

  void
  checkpoint_info::dump (FILE *out_fp)
  {
    assert (out_fp != nullptr);

    fprintf (out_fp, "-->> Data:\n");
    fprintf (out_fp, "  start_redo_lsa = %lld|%d  snapshot_lsa = %lld|%d  has_2pc = %d\n"
	     "  mvcc_next_id = %llu\n",
	     LSA_AS_ARGS (&m_start_redo_lsa), LSA_AS_ARGS (&m_snapshot_lsa), (int)m_has_2pc,
	     (unsigned long long)m_mvcc_next_id);

    fprintf (out_fp, "  transaction_count = %u :\n", (unsigned) m_trans.size ());
    int index = 0;
    for (const auto &ti : m_trans)
      {
	fprintf (out_fp,
		 "%3d isloose_end = %d  trid = %d  state = %s\n"
		 "    head_lsa = %lld|%d  tail_lsa = %lld|%d\n"
		 "    undo_nxlsa = %lld|%d  posp_nxlsa = %lld|%d\n"
		 "    savept_lsa = %lld|%d\n"
		 "    tail_topresult_lsa = %lld|%d  start_postpone_lsa = %lld|%d\n"
		 "    last_mvcc_lsa = %lld|%d  mvcc_id = %llu  mvcc_sub_id = %llu\n"
		 "    user_name = %s\n",
		 index, (int)ti.isloose_end, ti.trid, log_state_string (ti.state),
		 LSA_AS_ARGS (&ti.head_lsa), LSA_AS_ARGS (&ti.tail_lsa),
		 LSA_AS_ARGS (&ti.undo_nxlsa), LSA_AS_ARGS (&ti.posp_nxlsa),
		 LSA_AS_ARGS (&ti.savept_lsa),
		 LSA_AS_ARGS (&ti.tail_topresult_lsa), LSA_AS_ARGS (&ti.start_postpone_lsa),
		 LSA_AS_ARGS (&ti.last_mvcc_lsa), (unsigned long long)ti.mvcc_id, (unsigned long long)ti.mvcc_sub_id,
		 (ti.user_name != nullptr ? ti.user_name : "<NULL>"));
	++index;
      }

    fprintf (out_fp, "  sysop_count = %u :\n", (unsigned) m_sysops.size ());
    index = 0;
    for (const auto &si : m_sysops)
      {
	fprintf (out_fp,
		 "%3d trid = %d  sysop_start_postpone_lsa = %lld|%d\n"
		 "    atomic_sysop_start_lsa = %lld|%d\n",
		 index, si.trid, LSA_AS_ARGS (&si.sysop_start_postpone_lsa),
		 LSA_AS_ARGS (&si.atomic_sysop_start_lsa));
	++index;
      }
  }

  bool
  checkpoint_info::tran_info::operator== (const tran_info &other) const
  {
    if (isloose_end != other.isloose_end)
      {
	return false;
      }

    if (trid != other.trid)
      {
	return false;
      }

    if (state != other.state)
      {
	return false;
      }

    if (head_lsa != other.head_lsa)
      {
	return false;
      }

    if (tail_lsa != other.tail_lsa)
      {
	return false;
      }

    if (undo_nxlsa != other.undo_nxlsa)
      {
	return false;
      }

    if (posp_nxlsa != other.posp_nxlsa)
      {
	return false;
      }

    if (savept_lsa != other.savept_lsa)
      {
	return false;
      }

    if (tail_topresult_lsa != other.tail_topresult_lsa)
      {
	return false;
      }

    if (start_postpone_lsa != other.start_postpone_lsa)
      {
	return false;
      }

    if (mvcc_id != other.mvcc_id)
      {
	return false;
      }

    if (mvcc_sub_id != other.mvcc_sub_id)
      {
	return false;
      }

    if (std::strcmp (user_name, other.user_name) != 0)
      {
	return false;
      }

    return true;
  }

  bool
  checkpoint_info::sysop_info::operator== (const sysop_info &other) const
  {
    if (trid != other.trid)
      {
	return false;
      }

    if (sysop_start_postpone_lsa != other.sysop_start_postpone_lsa)
      {
	return false;
      }

    if (atomic_sysop_start_lsa != other.atomic_sysop_start_lsa)
      {
	return false;
      }
    return true;
  }
}
