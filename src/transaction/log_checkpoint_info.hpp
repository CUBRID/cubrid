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

#ifndef _LOG_CHECKPOINT_INFO_HPP_
#define _LOG_CHECKPOINT_INFO_HPP_

#include "client_credentials.hpp"
#include "log_comm.h"
#include "log_lsa.hpp"
#include "packable_object.hpp"
#include "storage_common.h"
#include "thread_compat.hpp"

#include <vector>

// forward declaration
struct log_tdes;

//
// log_checkpoint_info.hpp - the information saved during log checkpoint and used for recovery
//
// Replaces LOG_REC_CHKPT
namespace cublog
{
  /* used in two contexts:
   *  - regular checkpoint on the page server used for page server recovery
   *  - transaction table checkpoint on the transaction server used for transaction server recovery
   */
  class checkpoint_info : public cubpacking::packable_object
  {
    public:
      checkpoint_info () = default;
      checkpoint_info (checkpoint_info &&) = default;
      checkpoint_info (const checkpoint_info &) = delete;

      ~checkpoint_info () override = default;

      checkpoint_info &operator = (checkpoint_info &&) = delete;
      checkpoint_info &operator = (const checkpoint_info &) = delete;

      void pack (cubpacking::packer &serializator) const override;
      void unpack (cubpacking::unpacker &deserializator) override;
      size_t get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const override;

      // with tran table and prior lock, save snapshot LSA and get trans/sysops info from transaction table
      void load_trantable_snapshot (THREAD_ENTRY *thread_p, LOG_LSA &smallest_lsa);

      // restore transaction table based on checkpoint info
      void recovery_analysis (THREAD_ENTRY *thread_p, log_lsa &start_redo_lsa,
			      bool skip_empty_transactions) const;
      // if m_has_2pc, also do 2pc analysis
      void recovery_2pc_analysis (THREAD_ENTRY *thread_p) const;

      log_lsa get_snapshot_lsa () const;	      // the LSA of loaded snapshot
      log_lsa get_start_redo_lsa () const;     // the LSA of starting redo (min LSA of checkpoint and oldest unflushed)
      void set_start_redo_lsa (const log_lsa &start_redo_lsa);

      size_t get_transaction_count () const;
      size_t get_sysop_count () const;

      MVCCID get_mvcc_next_id () const;

      void dump (FILE *out_fp);

    private:
      void load_checkpoint_trans (log_tdes &tdes, LOG_LSA &smallest_lsa,
				  bool &at_least_one_active_transaction_has_valid_mvccid);
      void load_checkpoint_topop (log_tdes &tdes);

      struct tran_info;
      struct sysop_info;

      log_lsa m_start_redo_lsa = NULL_LSA;
      log_lsa m_snapshot_lsa = NULL_LSA;
      std::vector<tran_info> m_trans;
      std::vector<sysop_info> m_sysops;
      bool m_has_2pc = false;				      // true if any LOG_ISTRAN_2PC (tdes) is true
      MVCCID m_mvcc_next_id = MVCCID_NULL; // only filled if no transaction with valid mvccid info is present
  };

  struct checkpoint_info::tran_info
  {
    int isloose_end;
    TRANID trid;			/* Transaction identifier */
    TRAN_STATE state;		/* Transaction state (e.g., Active, aborted) */
    LOG_LSA head_lsa;		/* First log address of transaction */
    LOG_LSA tail_lsa;		/* Last log record address of transaction */
    LOG_LSA undo_nxlsa;		/* Next log record address of transaction for UNDO purposes. Needed since compensating
                                   * log records are logged during UNDO */
    LOG_LSA posp_nxlsa;		/* First address of a postpone record */
    LOG_LSA savept_lsa;		/* Address of last savepoint */
    LOG_LSA tail_topresult_lsa;	/* Address of last partial abort/commit */
    LOG_LSA start_postpone_lsa;	/* Address of start postpone (if transaction was doing postpone during checkpoint) */
    LOG_LSA last_mvcc_lsa;

    MVCCID mvcc_id;
    MVCCID mvcc_sub_id;
    char user_name[LOG_USERNAME_MAX];	/* Name of the client */

    inline bool operator== (const tran_info &other) const;
  };

  struct checkpoint_info::sysop_info
  {
    TRANID trid;			/* Transaction identifier */
    LOG_LSA sysop_start_postpone_lsa;	/* saved lsa of system op start postpone log record */
    LOG_LSA atomic_sysop_start_lsa;	/* saved lsa of atomic system op start */

    inline bool operator== (const sysop_info &other) const;
  };
}
#endif
