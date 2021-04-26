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
      log_Gl.prior_info.prior_lsa_mutex.unlock ();
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
    int error_code;
    LOG_TDES *tdes = nullptr;
    LOG_PAGE *log_page_local = nullptr;
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
	tdes->client.set_system_internal_with_user (chkpt.user_name);
	if (LOG_ISTRAN_2PC (tdes))
	  {
	    m_has_2pc = true;
	  }
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
	tdes = logtb_rv_find_allocate_tran_index (thread_p, sysop.trid, &NULL_LSA);
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

	if (tdes->topops.last == -1)
	  {
	    tdes->topops.last++;
	  }
	else
	  {
	    assert (tdes->topops.last == 0);
	  }
	tdes->rcv.sysop_start_postpone_lsa = sysop.sysop_start_postpone_lsa;
	tdes->rcv.atomic_sysop_start_lsa = sysop.atomic_sysop_start_lsa;
	log_lsa_local = sysop.sysop_start_postpone_lsa;
	error_code = log_read_sysop_start_postpone (thread_p, &log_lsa_local, log_page_local, false, &sysop_start_postpone,
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
  checkpoint_info::recovery_2pc_analysis (THREAD_ENTRY *thread_p) const
  {
    if (!m_has_2pc)
      {
	return;
      }

    for (auto chkpt : m_trans)
      {
	int tran_index;
	LOG_TDES *tdes;		/* Transaction descriptor */

	tran_index = logtb_find_tran_index (thread_p, chkpt.trid);
	if (tran_index != NULL_TRAN_INDEX)
	  {
	    tdes = LOG_FIND_TDES (tran_index);
	    if (tdes != NULL && LOG_ISTRAN_2PC (tdes))
	      {
		log_2pc_recovery_analysis_info (thread_p, tdes, &chkpt.tail_lsa);
	      }
	  }
      }
  }

}
