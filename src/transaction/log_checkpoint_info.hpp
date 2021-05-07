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

#include "log_lsa.hpp"
#include "log_record.hpp"
#include "log_system_tran.hpp"
#include "packable_object.hpp"

#include <vector>

//
// log_checkpoint_info.hpp - the information saved during log checkpoint and used for recovery
//
// Replaces LOG_REC_CHKPT
namespace cublog
{
  using checkpoint_tran_info = log_info_chkpt_trans;	// todo: replace log_info_chkpt_trans
  using checkpoint_sysop_info = log_info_chkpt_sysop;	// todo: replace log_info_chkpt_sysop

  class checkpoint_info : public cubpacking::packable_object
  {
    public:
      checkpoint_info () = default;
      checkpoint_info (checkpoint_info &&) = default;
      checkpoint_info (const checkpoint_info &) = default;
      ~checkpoint_info () override = default;

      void pack (cubpacking::packer &serializator) const override;
      void unpack (cubpacking::unpacker &deserializator) override;
      size_t get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const override;

      // with tran table and prior lock, save snapshot LSA and get trans/sysops info from transaction table
      void load_trantable_snapshot (THREAD_ENTRY *thread_p, LOG_LSA &smallest_lsa);

      // restore transaction table based on checkpoint info
      void recovery_analysis (THREAD_ENTRY *thread_p, log_lsa &start_redo_lsa) const;
      // if m_has_2pc, also do 2pc analysis
      void recovery_2pc_analysis (THREAD_ENTRY *thread_p) const;

      log_lsa get_snapshot_lsa () const;	      // the LSA of loaded snapshot
      log_lsa get_start_redo_lsa () const;     // the LSA of starting redo (min LSA of checkpoint and oldest unflushed)
      void set_start_redo_lsa (const log_lsa &start_redo_lsa);

      size_t get_transaction_count () const;
      size_t get_sysop_count () const;

    private:
      void load_checkpoint_trans (log_tdes &tdes, LOG_LSA &smallest_lsa);
      void load_checkpoint_topop (log_tdes &tdes);

      log_lsa m_start_redo_lsa;
      log_lsa m_snapshot_lsa;
      std::vector<checkpoint_tran_info> m_trans;
      std::vector<checkpoint_sysop_info> m_sysops;
      bool m_has_2pc;				      // true if any LOG_ISTRAN_2PC (tdes) is true
  };
}
#endif
