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
   * Helper class that provides methods to allow handling of all atomic replication sequences
   * generated on the active transaction server.
   *
   * All examples given below are treated as a single atomic sequence.
   * The following cases are covered by the implementation:
   *
   * 1.
   *    LOG_START_ATOMIC_REPL
   *        (undo|redo|undoredo|..)+
   *    LOG_END_ATOMIC_REPL
   *
   * 2. standalone sysop atomic sequences whose ending condition is based on the sysop end's
   *    last parent LSA
   *
   *    LOG_SYSOP_ATOMIC_START
   *        (undo|redo|undoredo|..)+
   *    LOG_SYSOP_END
   *
   * 3. vacuum generated atomic sysops with nested postpone logical operations which, themselves,
   *    can contain atomic operations; these have the following layout:
   *
   *    LOG_SYSOP_ATOMIC_START
   *        (undo|redo|undoredo|..)+
   *        ..
   *        LOG_POSTPONE 1 (eg: with RVFL_DEALLOC - logical or physical change)
   *        (undo|redo|undoredo|..)*
   *        ..
   *        LOG_POSTPONE 2 (eg: other index)
   *        (undo|redo|undoredo|..)*
   *        ..
   *    LOG_SYSOP_START_POSTPONE
   *        log records for LOG_POSTPONE 1 (RVFL_DEALLOC) with postpone_lsa pointing to the
   *          LSA of LOG_POSTPONE 1
   *        LOG_SYSOP_END with LOG_SYSOP_END_LOGICAL_RUN_POSTPONE
   *        log records for LOG_POSTPONE 2 (other index) with postpone_lsa pointing *still* to the
   *          LSA of LOG_POSTPONE 1 (yes, it points to the start of all postpones, to allow recovery
   *          which, potentially needs to iterate and execute all postpones if not already executed)
   *        LOG_SYSOP_END with LOG_SYSOP_END_LOGICAL_RUN_POSTPONE
   *    LOG_SYSOP_END with LOG_SYSOP_END_COMMIT
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

      // start a new non-sysop atomic replication sequence for a transaction;
      // the transaction must not already have an atomic replication sequence started
      void add_atomic_replication_sequence (TRANID trid, LOG_LSA start_lsa, const log_rv_redo_context &redo_context);
      // add a new log record as part of an already existing atomic replication
      // sequence (be it sysop or non-sysop)
      int add_atomic_replication_log (THREAD_ENTRY *thread_p, TRANID tranid, log_lsa record_lsa, LOG_RCVINDEX rcvindex,
				      VPID vpid);

      // start a new sysop atomic replication sequence for a transaction
      // the transaction must not already have an atomic replication sequence started
      void start_sysop_sequence (TRANID trid, LOG_LSA start_lsa,
				 const log_rv_redo_context &redo_context);
      // can a sysop-type atomic sequence be ended under the transaction
      bool can_end_sysop_sequence (TRANID trid, LOG_LSA sysop_parent_lsa) const;
      bool can_end_sysop_sequence (TRANID trid) const;

      // mark the start of postpone sequence for a transaction; transaction must have
      // already started an atomic sequence; the postpone sequence can contain nested atomic
      // replication sequences which will be treated unioned with the main, already started, one
      void start_postpone_sequence (TRANID trid);
      bool is_postpone_sequence_started (TRANID trid) const;
      void complete_one_postpone_sequence (TRANID trid);
      // there is no easy way of knowing how many postpone sequences are in the transaction
      // but there should be at least one
      bool is_at_least_one_postpone_sequence_completed (TRANID trid) const;

      void apply_and_unfix_atomic_replication_sequence (THREAD_ENTRY *thread_p, TRANID tranid);

      bool is_part_of_atomic_replication (TRANID tranid) const;

      log_lsa get_the_lowest_start_lsa () const;

    private: // methods
      void start_sequence_internal (TRANID trid, LOG_LSA start_lsa,
				    const log_rv_redo_context &redo_context, bool is_sysop);
#if !defined (NDEBUG)
      bool check_for_page_validity (VPID vpid, TRANID tranid) const;
#endif

    private: // types
      class atomic_log_sequence
      {
	public:
	  atomic_log_sequence () = delete;
	  explicit atomic_log_sequence (const log_rv_redo_context &redo_context);

	  atomic_log_sequence (const atomic_log_sequence &) = delete;
	  atomic_log_sequence (atomic_log_sequence &&) = delete;

	  ~atomic_log_sequence () = default;

	  atomic_log_sequence &operator= (const atomic_log_sequence &) = delete;
	  atomic_log_sequence &operator= (atomic_log_sequence &&) = delete;

	  // technical: function is needed to avoid double constructing a redo_context - which is expensive -
	  // upon constructing a sequence
	  void initialize (LOG_LSA start_lsa, bool is_sysop);

	  int add_atomic_replication_log (THREAD_ENTRY *thread_p, log_lsa record_lsa, LOG_RCVINDEX rcvindex, VPID vpid);

	  bool can_end_sysop_sequence (const LOG_LSA &sysop_parent_lsa) const;
	  bool can_end_sysop_sequence () const;

	  void start_postpone_sequence ();
	  bool is_postpone_sequence_started () const;
	  void complete_one_postpone_sequence ();
	  bool is_at_least_one_postpone_sequence_completed () const;

	  void apply_and_unfix_sequence (THREAD_ENTRY *thread_p);

	  log_lsa get_start_lsa () const;

	private:
	  void dump ();

	private: // types
	  /*
	   * Holds the log record information necessary for recovery redo
	   */
	  struct atomic_log_entry
	  {
	    atomic_log_entry () = delete;
	    atomic_log_entry (log_lsa lsa, VPID vpid, LOG_RCVINDEX rcvindex, PAGE_PTR page_ptr);

	    atomic_log_entry (const atomic_log_entry &) = delete;
	    atomic_log_entry (atomic_log_entry &&that);

	    atomic_log_entry &operator= (const atomic_log_entry &) = delete;
	    atomic_log_entry &operator= (atomic_log_entry &&) = delete;

	    void apply_log_redo (THREAD_ENTRY *thread_p, log_rv_redo_context &redo_context) const;
	    template <typename T>
	    void apply_log_by_type (THREAD_ENTRY *thread_p, log_rv_redo_context &redo_context,
				    LOG_RECTYPE rectype) const;

	    const VPID m_vpid;
	    const log_lsa m_record_lsa;
	    const LOG_RCVINDEX m_record_index;
	    // ownership of page pointer is with the bookkeeper in the owning class; this is just a
	    // reference to allow applying the redo function when needed
	    PAGE_PTR const m_page_ptr;
	  };

	  using page_ptr_watcher_uptr_type = std::unique_ptr<PGBUF_WATCHER>;

	  struct page_ptr_info
	  {
	    page_ptr_info () = default;

	    page_ptr_info (const page_ptr_info &) = delete;
	    page_ptr_info (page_ptr_info &&) = default;

	    page_ptr_info &operator= (const page_ptr_info &) = delete;
	    page_ptr_info &operator= (page_ptr_info &&) = delete;

	    ~page_ptr_info ();

	    VPID m_vpid = VPID_INITIALIZER;
	    LOG_RCVINDEX m_rcv_index = RV_NOT_DEFINED;
	    PAGE_PTR m_page_p = nullptr;
	    page_ptr_watcher_uptr_type m_watcher_p;
	    int m_ref_count = -1;
	  };

	  /*
	   * Implements a RAII-like reference counted functionality to bokkeep page pointers for
	   * a sequence of [possibly] nested atomic replication sub-sequences.
	   * A page can be needed by multiple levels of a nested atomic replication sequence which
	   * perfom changes on the page. Once the page is unfixed in a sequence at a certain
	   * level, it can be:
	   *  - either still kept fixed if a parent [sub]sequence did the fixing and still
	   *    needs the page
	   *  - or unfixed if there is no parent [sub]sequence which needs the page anymore (aka:
	   *    the [sub]sequence which just requested the fix is the outer-most one that needed
	   *    the page in the current overall sequence of possibly nested [sub]sequences
	   */
	  struct page_ptr_bookkeeping
	  {
	    page_ptr_bookkeeping () = default;
	    ~page_ptr_bookkeeping ();

	    page_ptr_bookkeeping (const page_ptr_bookkeeping &) = delete;
	    page_ptr_bookkeeping (page_ptr_bookkeeping &&) = delete;

	    page_ptr_bookkeeping &operator= (const page_ptr_bookkeeping &) = delete;
	    page_ptr_bookkeeping &operator= (page_ptr_bookkeeping &&) = delete;

	    int fix_page (THREAD_ENTRY *thread_p, VPID vpid, LOG_RCVINDEX rcv_index, PAGE_PTR &page_ptr_out);
	    int unfix_page (THREAD_ENTRY *thread_p, VPID vpid);

	    using page_ptr_info_map_type = std::map<VPID, page_ptr_info>;

	    page_ptr_info_map_type m_page_ptr_info_map;
	  };

	  using atomic_log_entry_vector_type = std::vector<atomic_log_entry>;

	private: // variables
	  /* The LSA of the log record which started this atomic sequence.
	   * It is used for comparison to see whether a sysop end operation can close an
	   * atomic replication sequence. */
	  LOG_LSA m_start_lsa;
	  /* Separates the two types of atomic sequences:
	   *  - sysop
	   *  - non-sysop
	   */
	  bool m_is_sysop = false;
	  bool m_postpone_started = false;
	  int m_end_pospone_count = 0;

	  log_rv_redo_context m_redo_context;
	  atomic_log_entry_vector_type m_log_vec;
	  page_ptr_bookkeeping m_page_ptr_bookkeeping;
      };

      using sequence_map_type = std::map<TRANID, atomic_log_sequence>;

#if !defined (NDEBUG)
      // check validity of atomic sequences
      // one page can only be accessed by one atomic sequence within one transaction
      // this check makes sense because, on active transaction server, there is no
      // notion of an "atomic" sequence and, hence, it is totally possible that
      // another transaction might access the same page
      using vpid_set_type = std::set<VPID>;
#endif

    private: // variables
      sequence_map_type m_sequences_map;

#if !defined (NDEBUG)
      std::map<TRANID, vpid_set_type> m_vpid_sets_map;
#endif
  };

  int pgbuf_fix_or_ordered_fix (THREAD_ENTRY *thread_p, VPID vpid, LOG_RCVINDEX rcv_index,
				std::unique_ptr<PGBUF_WATCHER> &watcher_uptr, PAGE_PTR &page_ptr);
  void pgbuf_unfix_or_ordered_unfix (THREAD_ENTRY *thread_p, LOG_RCVINDEX rcv_index,
				     std::unique_ptr<PGBUF_WATCHER> &watcher_uptr, PAGE_PTR &page_ptr);

  /*********************************************************************************************************
   * template functions implementations
   *********************************************************************************************************/

  template <typename T>
  void
  atomic_replication_helper::atomic_log_sequence::atomic_log_entry::apply_log_by_type (
	  THREAD_ENTRY *thread_p, log_rv_redo_context &redo_context, LOG_RECTYPE rectype) const
  {
    LOG_RCV rcv;
    assert (m_page_ptr != nullptr);
    rcv.pgptr = m_page_ptr;

    redo_context.m_reader.advance_when_does_not_fit (sizeof (T));
    const log_rv_redo_rec_info<T> record_info (m_record_lsa, rectype,
	redo_context.m_reader.reinterpret_copy_and_add_align<T> ());
    if (log_rv_check_redo_is_needed (rcv.pgptr, record_info.m_start_lsa, redo_context.m_end_redo_lsa))
      {
	rcv.reference_lsa = m_record_lsa;
	log_rv_redo_record_sync_apply (thread_p, redo_context, record_info, m_vpid, rcv);
      }
  }
}

#endif // _ATOMIC_REPLICATION_HELPER_HPP_
