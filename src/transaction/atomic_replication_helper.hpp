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

      void add_atomic_replication_sequence (TRANID trid, LOG_LSA start_lsa, const log_rv_redo_context &redo_context);
      int add_atomic_replication_unit (THREAD_ENTRY *thread_p, TRANID tranid, log_lsa record_lsa, LOG_RCVINDEX rcvindex,
				       VPID vpid);
      void unfix_atomic_replication_sequence (THREAD_ENTRY *thread_p, TRANID tranid);
      bool is_part_of_atomic_replication (TRANID tranid) const;
#if !defined (NDEBUG)
      bool check_for_page_validity (VPID vpid, TRANID tranid) const;
#endif
      bool can_end_atomic_sequence (TRANID tranid, LOG_LSA sysop_parent_lsa) const;
      log_lsa get_the_lowest_start_lsa ();

    private:

      class atomic_replication_sequence
      {
	public:
	  atomic_replication_sequence () = delete;
	  explicit atomic_replication_sequence (const log_rv_redo_context &redo_context);

	  atomic_replication_sequence (const atomic_replication_sequence &) = delete;
	  atomic_replication_sequence (atomic_replication_sequence &&) = delete;

	  ~atomic_replication_sequence () = default;

	  atomic_replication_sequence &operator= (const atomic_replication_sequence &) = delete;
	  atomic_replication_sequence &operator= (atomic_replication_sequence &&) = delete;

	  // technical: function is needed to avoid double constructing a redo_context - which is expensive -
	  // upon constructing a sequence
	  void set_start_lsa (LOG_LSA start_lsa);

	  void apply_and_unfix_sequence (THREAD_ENTRY *thread_p);
	  int add_atomic_replication_unit (THREAD_ENTRY *thread_p, log_lsa record_lsa, LOG_RCVINDEX rcvindex, VPID vpid);
	  bool can_end_atomic_sequence (LOG_LSA sysop_parent_lsa) const;
	  log_lsa get_start_lsa () const;
	private:
	  void apply_all_log_redos (THREAD_ENTRY *thread_p);

	  /*
	   * Atomic replication unit holds the log record information necessary for recovery redo
	   */
	  class atomic_replication_unit
	  {
	    public:
	      atomic_replication_unit () = delete;
	      atomic_replication_unit (log_lsa lsa, VPID vpid, LOG_RCVINDEX rcvindex);

	      atomic_replication_unit (const atomic_replication_unit &) = default;
	      atomic_replication_unit (atomic_replication_unit &&) = default;

	      ~atomic_replication_unit ();

	      atomic_replication_unit &operator= (const atomic_replication_unit &) = delete;
	      atomic_replication_unit &operator= (atomic_replication_unit &&) = delete;

	      int apply_log_redo (THREAD_ENTRY *thread_p, log_rv_redo_context &redo_context);
	      template <typename T>
	      void apply_log_by_type (THREAD_ENTRY *thread_p, log_rv_redo_context &redo_context, LOG_RECTYPE rectype);
	      int fix_page (THREAD_ENTRY *thread_p);
	      void unfix_page (THREAD_ENTRY *thread_p);
	      PAGE_PTR get_page_ptr ();
	      void set_page_ptr (const PAGE_PTR &ptr);
	      LOG_LSA get_lsa () const;

	      VPID m_vpid;
	    private:
	      log_lsa m_record_lsa;
	      PAGE_PTR m_page_ptr;
	      PGBUF_WATCHER m_watcher;
	      LOG_RCVINDEX m_record_index;
	  };

	  /* The LSA of the log record which started this atomic sequence.
	   * It is used for comparison to see whether a sysop end operation can close an
	   * atomic replication sequence. */
	  LOG_LSA m_start_lsa;

	  using atomic_unit_vector_type = std::vector<atomic_replication_unit>;
	  using vpid_to_page_ptr_map_type = std::map<VPID, PAGE_PTR>;

	  log_rv_redo_context m_redo_context;
	  atomic_unit_vector_type m_units;
	  vpid_to_page_ptr_map_type m_page_map;
      };

    private:
      using sequence_map_type = std::map<TRANID, atomic_replication_sequence>;

      sequence_map_type m_sequences_map;
#if !defined (NDEBUG)
      using vpid_set_type = std::set<VPID>;

      std::map<TRANID, vpid_set_type> m_vpid_sets_map;
#endif
  };

  template <typename T>
  void atomic_replication_helper::atomic_replication_sequence::atomic_replication_unit::apply_log_by_type (
	  THREAD_ENTRY *thread_p, log_rv_redo_context &redo_context, LOG_RECTYPE rectype)
  {
    LOG_RCV rcv;
    if (m_page_ptr != nullptr)
      {
	assert (m_watcher.pgptr == nullptr);
	rcv.pgptr = m_page_ptr;
      }
    else if (m_watcher.pgptr != nullptr)
      {
	rcv.pgptr = m_watcher.pgptr;
      }
    else
      {
	assert_release (false);
      }

    redo_context.m_reader.advance_when_does_not_fit (sizeof (T));
    const log_rv_redo_rec_info<T> record_info (m_record_lsa, rectype,
	redo_context.m_reader.reinterpret_copy_and_add_align<T> ());
    if (log_rv_check_redo_is_needed (rcv.pgptr, record_info.m_start_lsa, redo_context.m_end_redo_lsa))
      {
	rcv.reference_lsa = m_record_lsa;
	log_rv_redo_record_sync_apply (thread_p, redo_context, record_info, m_vpid, rcv);
      }
  }

  inline LOG_LSA atomic_replication_helper::atomic_replication_sequence::atomic_replication_unit::get_lsa () const
  {
    return m_record_lsa;
  }
}

#endif // _ATOMIC_REPLICATION_HELPER_HPP_
