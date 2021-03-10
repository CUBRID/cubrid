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

#ifndef LOG_RECOVERY_REDO_HPP
#define LOG_RECOVERY_REDO_HPP

#include "log_compress.h"
#include "log_reader.hpp"
#include "log_record.hpp"
#include "log_recovery_redo_sequential.hpp"
#include "storage_common.h"
#include "thread_entry.hpp"
#include "thread_task.hpp"

#include <atomic>
#include <bitset>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <set>
#include <vector>

namespace log_recovery_ns
{
  /* contains a single vpid-backed log record entry to be applied on a page
   *
   * NOTE: an optimization would be to allow this to be a container of more than
   *  one same-vpid log record entry to be applied in one go by the same thread
   */
  class redo_log_rec_entry
  {
    public:
      redo_log_rec_entry () = delete;
      redo_log_rec_entry ( redo_log_rec_entry const &) = delete;
      redo_log_rec_entry ( redo_log_rec_entry &&) = delete;

      redo_log_rec_entry &operator = ( redo_log_rec_entry const &) = delete;
      redo_log_rec_entry &operator = ( redo_log_rec_entry &&) = delete;

      redo_log_rec_entry (VPID a_vpid)
	: vpid (a_vpid)
      {
	// the logic implemented below, in the queue an task, does allow for null-vpid type of
	// entries to be executed - they are called "to be waited for" or "synched" operations;
	// but, for now, do not allow them to be dispatched to be executed asynchronously
	assert (!VPID_ISNULL (&vpid));
      }

      virtual ~redo_log_rec_entry() = default;

      /* log entries come in more than one flavor:
       *  - pertain to a certain vpid - aka: page update
       *  - pertain to a certain VOLID - aka: volume extend, new page
       *  - pertain to no VOLID - aka: database extend, new volume
       */
      const VPID &get_vpid() const
      {
	return vpid;
      }

      inline VOLID get_vol_id() const
      {
	return vpid.volid;
      }

      inline PAGEID get_page_id() const
      {
	return vpid.pageid;
      }

      virtual int do_work (THREAD_ENTRY *thread_p, log_reader &log_pgptr_reader,
			   LOG_ZIP &undo_unzip_support, LOG_ZIP &redo_unzip_support) = 0;

      /* TODO: functions are needed only for testing purposes and might not have
       * any usefulness/meaning upon integration in production code
       */
      bool get_is_to_be_waited_for_op() const
      {
	return vpid.volid == NULL_VOLID || vpid.pageid == NULL_PAGEID;
      }

      bool is_volume_creation() const
      {
	return vpid.volid == NULL_VOLID && vpid.pageid == NULL_PAGEID;
      }
      bool is_volume_extension() const
      {
	return vpid.volid != NULL_VOLID && vpid.pageid == NULL_PAGEID;
      }
      bool is_page_modification() const
      {
	return vpid.volid != NULL_VOLID && vpid.pageid != NULL_PAGEID;
      }

      bool operator == (const redo_log_rec_entry &that) const
      {
	const bool res = get_vol_id() == that.get_vol_id()
			 && get_page_id() == that.get_page_id();
	if (!res)
	  {
	    const auto dummy = res;
	  }
	return res;
      }

      /* NOTE: actual payload goes below
       */

    private:
      VPID vpid;

      /* NOTE: actual payload goes below
       */
  };
  using ux_redo_lsa_log_entry = std::unique_ptr<redo_log_rec_entry>;


  /*
   */
  template <typename TYPE_LOG_REC>
  class redo_log_rec_entry_templ : public redo_log_rec_entry
  {
      using log_rec_t = TYPE_LOG_REC;

    public:
      redo_log_rec_entry_templ () = delete;
      redo_log_rec_entry_templ (VPID a_vpid, const log_rec_t &a_log_rec, const log_lsa &a_rcv_lsa,
				const LOG_LSA *a_end_redo_lsa,
				LOG_RECTYPE a_log_rtype)
	: redo_log_rec_entry (a_vpid)
	, log_rec (a_log_rec)
	, rcv_lsa (a_rcv_lsa)
	, end_redo_lsa (a_end_redo_lsa)
	, log_rtype (a_log_rtype)
      {
      }

      redo_log_rec_entry_templ ( redo_log_rec_entry_templ const &) = delete;
      redo_log_rec_entry_templ ( redo_log_rec_entry_templ &&) = delete;

      ~redo_log_rec_entry_templ() override = default;

      redo_log_rec_entry_templ &operator = ( redo_log_rec_entry_templ const &) = delete;
      redo_log_rec_entry_templ &operator = ( redo_log_rec_entry_templ &&) = delete;

      int do_work (THREAD_ENTRY *thread_p, log_reader &log_pgptr_reader,
		   LOG_ZIP &undo_unzip_support, LOG_ZIP &redo_unzip_support) override
      {
	const auto &rcv_vpid = get_vpid ();
	log_rv_redo_record_sync<log_rec_t> (thread_p, log_pgptr_reader, log_rec, rcv_vpid, rcv_lsa, end_redo_lsa,
					    log_rtype, undo_unzip_support, redo_unzip_support);
	return NO_ERROR;
      }

    private:
      const log_rec_t log_rec;
      const log_lsa rcv_lsa;
      const LOG_LSA *end_redo_lsa;  // by design pointer is guaranteed to outlive this instance
      const LOG_RECTYPE log_rtype;
  };


  /* rynchronizes prod/cons of log entries in n-prod - m-cons fashion
   */
  class redo_log_rec_entry_queue
  {
      using ux_entry_deque = std::deque<ux_redo_lsa_log_entry>;
      using vpid_set = std::set<VPID>;

    public:
      redo_log_rec_entry_queue();
      ~redo_log_rec_entry_queue();

      redo_log_rec_entry_queue ( redo_log_rec_entry_queue const & ) = delete;
      redo_log_rec_entry_queue ( redo_log_rec_entry_queue && ) = delete;
      redo_log_rec_entry_queue &operator= ( redo_log_rec_entry_queue const & ) = delete;
      redo_log_rec_entry_queue &operator= ( redo_log_rec_entry_queue && ) = delete;

      void locked_push (ux_redo_lsa_log_entry &&entry);

      /* to be called after all known entries have been added
       * part of a mechanism to signal to the consumers, together with
       * whether there is still data to process, that they can bail out
       */
      void set_adding_finished();

      /* the combination of a null return value with a finished
       * flag set to true signals to the callers that no more data is expected
       * and, therefore, they can also terminate
       */
      ux_redo_lsa_log_entry locked_pop (bool &adding_finished);

      void notify_to_be_waited_for_op_finished();
      void notify_in_progress_vpid_finished (VPID a_vpid);

      size_t get_dbg_stats_cons_queue_skip_count() const
      {
	return dbg_stats_cons_queue_skip_count;
      }

      size_t get_stats_spin_wait_count() const
      {
	return sbg_stats_spin_wait_count;
      }

    private:
      /* two queues are internally managed
       */
      ux_entry_deque *produce_queue;
      std::mutex produce_queue_mutex;
      ux_entry_deque *consume_queue;
      std::mutex consume_queue_mutex;

      std::atomic_bool adding_finished;

      /* barrier mechanism for log entries which are to be executed before all
       * log entries that come after them in the log
       *
       * TODO: given that such 'to_be_waited_for' log entries come in a certain order which
       * results in ever increasing VPID's, a better mechanism can be put in place where instead of
       * a single barrier for all VPID's, one barrier for each combinations of max(VOLID), max(PAGEID)
       * on a per volume basis can be used
       */
      bool to_be_waited_for_op_in_progress;
      std::condition_variable to_be_waited_for_op_in_progress_cv;

      /* bookkeeping for log entries currently in process of being applied, this
       * mechanism guarantees ordering among entries with the same VPID;
       */
      vpid_set in_progress_vpids;
      std::mutex in_progress_vpids_mutex;

      size_t dbg_stats_cons_queue_skip_count;
      size_t sbg_stats_spin_wait_count;
  };


  /*
   */
  class redo_task_active_state_bookkeeping
  {
    public:
      static constexpr std::size_t BOOKKEEPING_MAX_COUNT = 1024;

    public:
      redo_task_active_state_bookkeeping()
      {
	active_set.reset();
      }

      void set_active (std::size_t _id)
      {
	std::lock_guard<std::mutex> lck (active_set_mutex);

	assert (_id < BOOKKEEPING_MAX_COUNT);
	active_set.set (_id);
      }

      void set_inactive (std::size_t _id)
      {
	std::lock_guard<std::mutex> lck (active_set_mutex);

	assert (_id < BOOKKEEPING_MAX_COUNT);
	active_set.reset (_id);
      }

      bool any_active() const
      {
	std::lock_guard<std::mutex> lck (active_set_mutex);

	return active_set.any();
      }

    private:
      std::bitset<BOOKKEEPING_MAX_COUNT> active_set;
      mutable std::mutex active_set_mutex;
  };


  /*
   */
  class redo_task : public cubthread::task<cubthread::entry>
  {
    public:
      static constexpr unsigned short WAIT_AND_CHECK_MILLIS = 5;

    public:
      redo_task (std::size_t _task_id, redo_task_active_state_bookkeeping &_task_active_state_bookkeeping,
		 redo_log_rec_entry_queue &_bucket_queue);
      ~redo_task() override = default;

      void execute (context_type &context);

    private:
      static void dummy_busy_wait (size_t _millis);

    private:
      // debug variable
      std::size_t task_id;

      redo_task_active_state_bookkeeping &task_active_state_bookkeeping;

      redo_log_rec_entry_queue &bucket_queue;

      log_reader log_pgptr_reader;
      LOG_ZIP undo_unzip_support;
      LOG_ZIP redo_unzip_support;
  };

}

#endif // LOG_RECOVERY_REDO_HPP
