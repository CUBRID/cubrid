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
#include <set>

#include "log_lsa.hpp"
#include "log_record.hpp"
#include "log_recovery_redo.hpp"
#include "page_buffer.h"
#include "thread_entry.hpp"
#include "vpid_utilities.hpp"

namespace cublog
{
  /*
   * Atomic replication helper class that holds all atomic logs mapped by the transaction id
   */
  class atomic_replication_helper
  {
    public:
      atomic_replication_helper () = default;

      atomic_replication_helper (const atomic_replication_helper &) = delete;
      atomic_replication_helper (atomic_replication_helper &&) = delete;

      ~atomic_replication_helper () = default;

      atomic_replication_helper &operator= (const atomic_replication_helper &) = delete;
      atomic_replication_helper &operator= (atomic_replication_helper &&) = delete;

      void start_new_atomic_replication_sequence (TRANID tranid, LOG_LSA lsa);
      template <typename T>
      int add_atomic_replication_unit (THREAD_ENTRY *thread_p, TRANID tranid, log_lsa record_lsa, LOG_RCVINDEX rcvindex,
				       VPID vpid, log_rv_redo_context &redo_context, const log_rv_redo_rec_info<T> &record_info);
      void unfix_atomic_replication_sequence (THREAD_ENTRY *thread_p, TRANID tranid);
      bool is_part_of_atomic_replication (TRANID tranid) const;
      bool check_for_sysop_end (TRANID tranid, LOG_LSA lsa) const;
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
	  atomic_replication_unit (atomic_replication_unit &&that);

	  ~atomic_replication_unit ();

	  atomic_replication_unit &operator= (const atomic_replication_unit &) = delete;
	  atomic_replication_unit &operator= (atomic_replication_unit &&) = delete;

	  template <typename T>
	  void apply_log_redo (THREAD_ENTRY *thread_p, log_rv_redo_context &redo_context,
			       const log_rv_redo_rec_info<T> &record_info);
	  int fix_page (THREAD_ENTRY *thread_p);
	  void unfix_page (THREAD_ENTRY *thread_p);
	  LOG_LSA get_lsa () const;

	private:
	  const VPID m_vpid;
	  const log_lsa m_record_lsa;
	  const LOG_RCVINDEX m_record_index;

	  PAGE_PTR m_page_ptr;
	  PGBUF_WATCHER m_watcher;
      };

      using atomic_replication_sequence_type = std::vector<atomic_replication_unit>;
      std::map<TRANID, atomic_replication_sequence_type> m_atomic_sequences_map;

#if !defined (NDEBUG)
      using vpid_set_type = std::set<VPID>;
      std::map<TRANID, vpid_set_type> m_atomic_sequences_vpids_map;
#endif
  };

  template <typename T>
  int atomic_replication_helper::add_atomic_replication_unit (THREAD_ENTRY *thread_p, TRANID tranid, log_lsa record_lsa,
      LOG_RCVINDEX rcvindex, VPID vpid, log_rv_redo_context &redo_context, const log_rv_redo_rec_info<T> &record_info)
  {
#if !defined (NDEBUG)
    if (!VPID_ISNULL (&vpid) && !check_for_page_validity (vpid, tranid))
      {
	// the page is no longer relevant
	return ER_FAILED;
      }
    vpid_set_type &vpids = m_atomic_sequences_vpids_map[tranid];
    vpids.insert (vpid);
#endif

    m_atomic_sequences_map[tranid].emplace_back (atomic_replication_unit (record_lsa, vpid, rcvindex));
    int error_code = m_atomic_sequences_map[tranid].back ().fix_page (thread_p);
    if (error_code != NO_ERROR)
      {
	return error_code;
      }

    m_atomic_sequences_map[tranid].back ().apply_log_redo (thread_p, redo_context, record_info);
    return NO_ERROR;
  }

  template <typename T>
  void atomic_replication_helper::atomic_replication_unit::apply_log_redo (THREAD_ENTRY *thread_p,
      log_rv_redo_context &redo_context, const log_rv_redo_rec_info<T> &record_info)
  {
    LOG_RCV rcv;
    if (m_page_ptr != nullptr)
      {
	rcv.pgptr = m_page_ptr;
      }
    else
      {
	assert (m_watcher.page_was_unfixed == 0);
	rcv.pgptr = m_watcher.pgptr;
      }
    rcv.reference_lsa = m_record_lsa;
    log_rv_redo_record_sync_apply (thread_p,redo_context, record_info, m_vpid, rcv);
  }

}

#endif // _ATOMIC_REPLICATION_HELPER_HPP_
