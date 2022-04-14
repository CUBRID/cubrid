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

#ifndef _ATOMIC_REPLICATION_HELPER_HPP_
#define _ATOMIC_REPLICATION_HELPER_HPP_

#include <map>
#include <vector>

#include "log_lsa.hpp"
#include "log_record.hpp"
#include "log_recovery_redo.hpp"
#include "page_buffer.h"
#include "thread_entry.hpp"

namespace cublog
{
  /*
   * Atomic replication helper class that holds all atomic logs mapped by the transaction id
   */
  class atomic_replication_helper
  {
      using atomic_replication_sequence_type = std::vector<atomic_replication_unit>;
    public:
      atomic_replication_helper () = default;

      atomic_replication_helper (const atomic_replication_helper &) = delete;
      atomic_replication_helper (atomic_replication_helper &&) = delete;

      ~atomic_replication_helper () = default;

      atomic_replication_helper &operator= (const atomic_replication_helper &) = delete;
      atomic_replication_helper &operator= (atomic_replication_helper &&) = delete;

      template <typename T>
      void add_atomic_replication_unit (THREAD_ENTRY *thread_p, TRANID tranid, log_lsa record_lsa, LOG_RCVINDEX rcvindex,
					VPID vpid, log_rv_redo_context &redo_context, const log_rv_redo_rec_info<T> &record_info);
      void unfix_atomic_replication_sequence (THREAD_ENTRY *thread_p, TRANID tranid);
      bool is_part_of_atomic_replication (TRANID tranid) const;
#if !defined (NDEBUG)
      bool check_for_page_validity (VPID vpid, TRANID tranid) const;
#endif

    private:
      /*
       * Atomic replication unit holds the log record information necessary for recovery redo
       */
      class atomic_replication_unit
      {
	public:
	  atomic_replication_unit () = delete;
	  atomic_replication_unit (log_lsa lsa, VPID vpid, LOG_RCVINDEX rcvindex);

	  atomic_replication_unit (const atomic_replication_unit &) = delete;
	  atomic_replication_unit (atomic_replication_unit &&) = delete;

	  ~atomic_replication_unit ();

	  atomic_replication_unit &operator= (const atomic_replication_unit &) = delete;
	  atomic_replication_unit &operator= (atomic_replication_unit &&) = delete;

	  template <typename T>
	  void apply_log_redo (THREAD_ENTRY *thread_p, log_rv_redo_context &redo_context,
			       const log_rv_redo_rec_info<T> &record_info);
	  void fix_page (THREAD_ENTRY *thread_p);
	  void unfix_page (THREAD_ENTRY *thread_p);

	  VPID m_vpid;
	private:
	  log_lsa m_record_lsa;
	  PAGE_PTR m_page_ptr;
	  PGBUF_WATCHER m_watcher;
	  LOG_RCVINDEX m_record_index;
      };

      std::map<TRANID, atomic_replication_sequence_type> m_atomic_sequences_map;
  };
}

#endif // _ATOMIC_REPLICATION_HELPER_HPP_
