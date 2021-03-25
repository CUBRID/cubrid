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
#include "log_manager.h"
#include "memory_alloc.h"
#include "page_buffer.h"
#include "transaction_global.hpp"
#include "thread_entry.hpp"
#include "scope_exit.hpp"
#include "system_parameter.h"

#include <cstring>

namespace cublog
{

  int
  log_lsa_size (cubpacking::packer &serializator, std::size_t start_offset, std::size_t size_arg)
  {
    size_t size = size_arg;
    size += serializator.get_packed_bigint_size (start_offset + size);
    size += serializator.get_packed_short_size (start_offset + size);

    return size;
  }

  void
  log_lsa_pack (LOG_LSA log, cubpacking::packer &serializator)
  {
    serializator.pack_bigint (log.pageid);
    serializator.pack_short (log.offset);
  }

  LOG_LSA
  log_lsa_unpack (cubpacking::unpacker &deserializator)
  {
    int64_t pageid;
    short offset;
    deserializator.unpack_bigint (pageid);
    deserializator.unpack_short  (offset);

    return log_lsa (pageid, offset);
  }

  void
  checkpoint_info::pack (cubpacking::packer &serializator) const
  {

    log_lsa_pack (m_start_redo_lsa, serializator);
    log_lsa_pack (m_snapshot_lsa, serializator);

    serializator.pack_bigint (m_trans.size ());
    for (const checkpoint_tran_info tran_info : m_trans)
      {
	serializator.pack_int (tran_info.isloose_end);
	serializator.pack_int (tran_info.trid);
	serializator.pack_int (tran_info.state);
	log_lsa_pack (tran_info.head_lsa, serializator);
	log_lsa_pack (tran_info.tail_lsa, serializator);
	log_lsa_pack (tran_info.undo_nxlsa, serializator);

	log_lsa_pack (tran_info.posp_nxlsa, serializator);
	log_lsa_pack (tran_info.savept_lsa, serializator);
	log_lsa_pack (tran_info.tail_topresult_lsa, serializator);
	log_lsa_pack (tran_info.start_postpone_lsa, serializator);
	serializator.pack_c_string (tran_info.user_name, strlen (tran_info.user_name));
      }

    serializator.pack_bigint (m_sysops.size ());
    for (const checkpoint_sysop_info sysop_info : m_sysops)
      {
	serializator.pack_int (sysop_info.trid);
	log_lsa_pack (sysop_info.sysop_start_postpone_lsa, serializator);
	log_lsa_pack (sysop_info.atomic_sysop_start_lsa, serializator);
      }

    serializator.pack_bool (m_has_2pc);
  }

  void
  checkpoint_info::unpack (cubpacking::unpacker &deserializator)
  {
    m_start_redo_lsa = log_lsa_unpack (deserializator);
    m_snapshot_lsa = log_lsa_unpack (deserializator);

    std::uint64_t trans_size = 0;
    deserializator.unpack_bigint (trans_size);
    for (uint i = 0; i < trans_size; i++)
      {
	LOG_INFO_CHKPT_TRANS chkpt_trans;

	deserializator.unpack_int (chkpt_trans.isloose_end);
	deserializator.unpack_int (chkpt_trans.trid);

	deserializator.unpack_from_int (chkpt_trans.state);
	chkpt_trans.head_lsa = log_lsa_unpack (deserializator);
	chkpt_trans.tail_lsa = log_lsa_unpack (deserializator);
	chkpt_trans.undo_nxlsa = log_lsa_unpack (deserializator);

	chkpt_trans.posp_nxlsa = log_lsa_unpack (deserializator);
	chkpt_trans.savept_lsa = log_lsa_unpack (deserializator);
	chkpt_trans.tail_topresult_lsa = log_lsa_unpack (deserializator);
	chkpt_trans.start_postpone_lsa = log_lsa_unpack (deserializator);
	deserializator.unpack_c_string (chkpt_trans.user_name, LOG_USERNAME_MAX);

	m_trans.push_back (chkpt_trans);

      }

    std::uint64_t sysop_size = 0;
    deserializator.unpack_bigint (sysop_size);
    for (uint i = 0; i < sysop_size; i++)
      {
	LOG_INFO_CHKPT_SYSOP chkpt_sysop;
	deserializator.unpack_int (chkpt_sysop.trid);
	chkpt_sysop.sysop_start_postpone_lsa = log_lsa_unpack (deserializator);
	chkpt_sysop.atomic_sysop_start_lsa = log_lsa_unpack (deserializator);

	m_sysops.push_back (chkpt_sysop);
      }

    deserializator.unpack_bool (m_has_2pc);
  }

  size_t
  checkpoint_info::get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const
  {
    size_t size =  0;
    size = log_lsa_size (serializator, start_offset, size);
    size = log_lsa_size (serializator, start_offset, size);

    size += serializator.get_packed_bigint_size (start_offset + size);
    for (const checkpoint_tran_info tran_info : m_trans)
      {
	size += serializator.get_packed_int_size (start_offset + size);
	size += serializator.get_packed_int_size (start_offset + size);
	size += serializator.get_packed_int_size (start_offset + size);

	size = log_lsa_size (serializator, start_offset, size);
	size = log_lsa_size (serializator, start_offset, size);
	size = log_lsa_size (serializator, start_offset, size);

	size = log_lsa_size (serializator, start_offset, size);
	size = log_lsa_size (serializator, start_offset, size);
	size = log_lsa_size (serializator, start_offset, size);
	size = log_lsa_size (serializator, start_offset, size);
	size += serializator.get_packed_c_string_size (tran_info.user_name, strlen (tran_info.user_name), start_offset + size);
      }

    size += serializator.get_packed_bigint_size (start_offset + size);
    for (const checkpoint_sysop_info sysop_info : m_sysops)
      {
	size += serializator.get_packed_int_size (start_offset + size);
	size = log_lsa_size (serializator, start_offset, size);
	size = log_lsa_size (serializator, start_offset, size);
      }

    size += serializator.get_packed_bool_size (start_offset + size);

    return size;
  }

  void
  checkpoint_info::load_checkpoint_trans (log_tdes &tdes, LOG_LSA &smallest_lsa)
  {
    if (tdes.trid != NULL_TRANID && !LSA_ISNULL (&tdes.tail_lsa))
      {
	m_trans.emplace_back ();
	LOG_INFO_CHKPT_TRANS &chkpt_tran = m_trans.back ();

	chkpt_tran.isloose_end = tdes.isloose_end;
	chkpt_tran.trid = tdes.trid;
	chkpt_tran.state = tdes.state;

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
    if (tdes.trid != NULL_TRANID && (!LSA_ISNULL (&tdes.rcv.sysop_start_postpone_lsa)
				     || !LSA_ISNULL (&tdes.rcv.atomic_sysop_start_lsa)))
      {
	/* this transaction is running system operation postpone or an atomic system operation
	 * note: we cannot compare tdes->state with TRAN_UNACTIVE_TOPOPE_COMMITTED_WITH_POSTPONE. we are
	 *       not synchronizing setting transaction state.
	 *       however, setting tdes->rcv.sysop_start_postpone_lsa is protected by
	 *       log_Gl.prior_info.prior_lsa_mutex. so we check this instead of state.
	 */
	m_sysops.emplace_back();
	LOG_INFO_CHKPT_SYSOP &chkpt_topop = m_sysops.back();
	chkpt_topop.trid = tdes.trid;
	chkpt_topop.sysop_start_postpone_lsa = tdes.rcv.sysop_start_postpone_lsa;
	chkpt_topop.atomic_sysop_start_lsa = tdes.rcv.atomic_sysop_start_lsa;
      }
  }

  void
  checkpoint_info::load_trantable_snapshot (THREAD_ENTRY *thread_p, LOG_LSA &smallest_lsa)
  {

    LOG_TDES *act_tdes;		/* Transaction descriptor of an active transaction */
    int i;
    log_system_tdes::map_func mapper;

    //ENTER the critical section
    TR_TABLE_CS_ENTER (thread_p);
    log_Gl.prior_info.prior_lsa_mutex.lock ();

    scope_exit <std::function<void (void)>> unlock_on_exit ([thread_p] ()
    {
      log_Gl.prior_info.prior_lsa_mutex.lock ();
      TR_TABLE_CS_EXIT (thread_p);
    });

    /* CHECKPOINT THE TRANSACTION TABLE */
    LSA_SET_NULL (&smallest_lsa);
    for (i = 0; i < log_Gl.trantable.num_total_indices; i++)
      {
	/*
	 * Don't checkpoint current system transaction. That is, the one of
	 * checkpoint process
	 */
	if (i == LOG_SYSTEM_TRAN_INDEX)
	  {
	    continue;
	  }
	act_tdes = LOG_FIND_TDES (i);
	assert (act_tdes != nullptr);
	load_checkpoint_trans (*act_tdes, smallest_lsa);
	load_checkpoint_topop (*act_tdes);
      }

    // Checkpoint system transactions' topops
    mapper = [this] (log_tdes &tdes)
    {
      load_checkpoint_topop (tdes);
    };
    log_system_tdes::map_all_tdes (mapper);
  }

  void
  checkpoint_info::recovery_analysis (THREAD_ENTRY *thread_p, log_lsa &start_redo_lsa)
  {
    int i, size, error_code;
    void *area;
    LOG_TDES *tdes;
    LOG_INFO_CHKPT_TRANS *chkpt_trans;
    LOG_INFO_CHKPT_TRANS *chkpt_one;
    LOG_INFO_CHKPT_SYSOP *chkpt_topops;
    LOG_INFO_CHKPT_SYSOP *chkpt_topone;
    LOG_PAGE *log_page_local = NULL;
    LOG_LSA log_lsa_local;
    char log_page_buffer[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT];
    LOG_REC_SYSOP_START_POSTPONE sysop_start_postpone;

    /* Add the transactions to the transaction table */
    for (auto chkpt : m_trans)
      {
	/*
	 * If this is the first time, the transaction is seen. Assign a
	 * new index to describe it and assume that the transaction was
	 * active at the time of the crash, and thus it will be
	 * unilaterally aborted. The truth of this statement will be find
	 * reading the rest of the log
	 */
	tdes = logtb_rv_find_allocate_tran_index (thread_p, chkpt.trid, &NULL_LSA);
	if (tdes == NULL)
	  {
	    if (area != NULL)
	      {
		free_and_init (area);
	      }

	    logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_recovery_analysis");
	    return;
	  }
	chkpt_one = &chkpt;

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
	tdes->client.set_system_internal_with_user (chkpt_one->user_name);
	if (LOG_ISTRAN_2PC (tdes))
	  {
	    m_has_2pc = true;
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

    for (auto sysop : m_sysops)
      {
	chkpt_topone = &sysop;
	tdes = logtb_rv_find_allocate_tran_index (thread_p, chkpt_topone->trid, &NULL_LSA);
	if (tdes == NULL)
	  {
	    if (area != NULL)
	      {
		free_and_init (area);
	      }

	    logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_recovery_analysis");
	    return;
	  }

	if (tdes->topops.max == 0 || (tdes->topops.last + 1) >= tdes->topops.max)
	  {
	    if (logtb_realloc_topops_stack (tdes, m_sysops.size()) == NULL)
	      {
		if (area != NULL)
		  {
		    free_and_init (area);
		  }

		logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_recovery_analysis");
		return;
	      }
	  }

	if (tdes->topops.last == -1)
	  {
	    tdes->topops.last++;
	  }
	else
	  {
	    assert (tdes->topops.last == 0);
	  }
	tdes->rcv.sysop_start_postpone_lsa = chkpt_topone->sysop_start_postpone_lsa;
	tdes->rcv.atomic_sysop_start_lsa = chkpt_topone->atomic_sysop_start_lsa;
	log_lsa_local = chkpt_topone->sysop_start_postpone_lsa;
	error_code =
		log_read_sysop_start_postpone (thread_p, &log_lsa_local, log_page_local, false, &sysop_start_postpone,
					       NULL, NULL, NULL, NULL);
	if (error_code != NO_ERROR)
	  {
	    assert (false);
	    return;
	  }
	tdes->topops.stack[tdes->topops.last].lastparent_lsa = sysop_start_postpone.sysop_end.lastparent_lsa;
	tdes->topops.stack[tdes->topops.last].posp_lsa = sysop_start_postpone.posp_lsa;
      }
  }
  void
  checkpoint_info::recovery_2pc_analysis() const
  {

  }

//  void
//  log_2pc_recovery_analysis_info (THREAD_ENTRY * thread_p, log_tdes * tdes, LOG_LSA * upto_chain_lsa)
//  {
//    LOG_RECORD_HEADER *log_rec;	/* Pointer to log record */
//    char log_pgbuf[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT], *aligned_log_pgbuf;
//    LOG_PAGE *log_page_p = NULL;	/* Log page pointer where LSA is located */
//    LOG_LSA lsa;
//    LOG_LSA prev_tranlsa;		/* prev LSA of transaction */
//    bool search_2pc_prepare = false;
//    bool search_2pc_start = false;
//    int ack_count = 0;
//    int *ack_list = NULL;
//    int size_ack_list = 0;

//    aligned_log_pgbuf = PTR_ALIGN (log_pgbuf, MAX_ALIGNMENT);

//    if (!LOG_ISTRAN_2PC (tdes))
//      {
//        return;
//      }

//    /* For a transaction that was prepared to commit at the time of the crash, make sure that its global transaction
//     * identifier is obtained from the log and that the update_type locks that were acquired before the time of the crash
//     * are reacquired. */

//    if (tdes->gtrid == LOG_2PC_NULL_GTRID)
//      {
//        search_2pc_prepare = true;
//      }

//    /* If this is a coordinator transaction performing 2PC and voting record has not been read from the log in the
//     * recovery redo phase, read the voting record and any acknowledgement records logged for this transaction */

//    if (tdes->coord == NULL)
//      {
//        search_2pc_start = true;
//      }

//    /*
//     * Follow the undo tail chain starting at upto_chain_tail finding all
//     * 2PC related information
//     */
//    log_page_p = (LOG_PAGE *) aligned_log_pgbuf;

//    LSA_COPY (&prev_tranlsa, upto_chain_lsa);
//    while (!LSA_ISNULL (&prev_tranlsa) && (search_2pc_prepare || search_2pc_start))
//      {
//        LSA_COPY (&lsa, &prev_tranlsa);
//        if ((logpb_fetch_page (thread_p, &lsa, LOG_CS_FORCE_USE, log_page_p)) != NO_ERROR)
//          {
//            logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_2pc_recovery_analysis_info");
//            break;
//          }

//        while (prev_tranlsa.pageid == lsa.pageid && (search_2pc_prepare || search_2pc_start))
//          {
//            lsa.offset = prev_tranlsa.offset;

//            log_rec = LOG_GET_LOG_RECORD_HEADER (log_page_p, &lsa);
//            LSA_COPY (&prev_tranlsa, &log_rec->prev_tranlsa);

//            if (log_2pc_recovery_analysis_record
//                (thread_p, log_rec->type, tdes, &lsa, log_page_p, &ack_list, &ack_count, &size_ack_list,
//                 &search_2pc_prepare, &search_2pc_start) != NO_ERROR)
//              {
//                LSA_SET_NULL (&prev_tranlsa);
//              }
//            free_and_init (ack_list);
//          }			/* while */
//      }				/* while */

//    /* Check for error conditions */
//    if (tdes->state == TRAN_UNACTIVE_2PC_PREPARE && tdes->gtrid == LOG_2PC_NULL_GTRID)
//      {
//  #if defined(CUBRID_DEBUG)
//        er_log_debug (ARG_FILE_LINE,
//                      "log_2pc_recovery_analysis_info:" " SYSTEM ERROR... Either the LOG_2PC_PREPARE/LOG_2PC_START\n"
//                      " log record was not found for participant of distributed" " trid = %d with state = %s", tdes->trid,
//                      log_state_string (tdes->state));
//  #endif /* CUBRID_DEBUG */
//      }

//    /*
//     * Now the client should attach to this prepared transaction and
//     * provide the decision (commit/abort). Until then this thread
//     * is suspended.
//     */

//    if (search_2pc_start)
//      {
//        /*
//         * A 2PC start log record was not found for the coordinator
//         */
//        if (tdes->state != TRAN_UNACTIVE_2PC_PREPARE)
//          {
//  #if defined(CUBRID_DEBUG)
//            er_log_debug (ARG_FILE_LINE,
//                          "log_2pc_recovery_analysis_info:" " SYSTEM ERROR... The LOG_2PC_START log record was"
//                          " not found for coordinator of distributed trid = %d" " with state = %s", tdes->trid,
//                          log_state_string (tdes->state));
//  #endif /* CUBRID_DEBUG */
//          }
//      }
//  }
}

