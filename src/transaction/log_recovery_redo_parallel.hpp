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

#ifndef _LOG_RECOVERY_REDO_HPP
#define LOG_RECOVERY_REDO_HPP

#include "log_compress.h"
#include "log_recovery_redo.hpp"
#include "thread_manager.hpp"
#include "thread_worker_pool.hpp"

#include <bitset>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <set>

namespace cublog
{
  /* contains a single vpid-backed log record entry to be applied on a page;
   * this is a base class designed to allow handling in a container;
   * internal implementation detail
   *
   * NOTE: an optimization would be to allow this to be a container of more than
   *  one same-vpid log record entry to be applied in one go by the same thread
   */
  class redo_job_base
  {
    public:
      redo_job_base (VPID a_vpid)
	: vpid (a_vpid)
      {
	// the logic implemented below, in the queue and task, does allow for null-vpid type of
	// entries to be executed - they are called "to be waited for" or "synched" operations;
	// but, for now, do not allow them to be dispatched to be executed asynchronously
	assert (!VPID_ISNULL (&vpid));
      }
      redo_job_base () = delete;
      redo_job_base (redo_job_base const &) = delete;
      redo_job_base (redo_job_base &&) = delete;

      virtual ~redo_job_base () = default;

      redo_job_base &operator = (redo_job_base const &) = delete;
      redo_job_base &operator = (redo_job_base &&) = delete;

      /* log entries come in more than one flavor:
       *  - pertain to a certain vpid - aka: page update
       *  - pertain to a certain VOLID - aka: volume extend, new page
       *  - pertain to no VOLID - aka: database extend, new volume
       */
      const VPID &get_vpid () const
      {
	return vpid;
      }

      virtual int execute (THREAD_ENTRY *thread_p, log_reader &log_pgptr_reader,
			   LOG_ZIP &undo_unzip_support, LOG_ZIP &redo_unzip_support) = 0;

      bool get_is_to_be_waited_for () const
      {
	return vpid.volid == NULL_VOLID || vpid.pageid == NULL_PAGEID;
      }

    private:
      const VPID vpid;
  };


  /* actual job implementation that performs log redo
   */
  template <typename TYPE_LOG_REC>
  class redo_job_impl final : public redo_job_base
  {
      using log_rec_t = TYPE_LOG_REC;

    public:
      redo_job_impl () = delete;
      redo_job_impl (VPID a_vpid, const log_lsa &a_rcv_lsa, const LOG_LSA *a_end_redo_lsa, LOG_RECTYPE a_log_rtype)
	: redo_job_base (a_vpid)
	, rcv_lsa (a_rcv_lsa)
	, end_redo_lsa (a_end_redo_lsa)
	, log_rtype (a_log_rtype)
      {
      }

      redo_job_impl (redo_job_impl const &) = delete;
      redo_job_impl (redo_job_impl &&) = delete;

      ~redo_job_impl () override = default;

      redo_job_impl &operator = (redo_job_impl const &) = delete;
      redo_job_impl &operator = (redo_job_impl &&) = delete;

      int execute (THREAD_ENTRY *thread_p, log_reader &log_pgptr_reader,
		   LOG_ZIP &undo_unzip_support, LOG_ZIP &redo_unzip_support) override
      {
	const int err_set_lsa_and_fetch_page =  log_pgptr_reader.set_lsa_and_fetch_page (rcv_lsa);
	if (err_set_lsa_and_fetch_page != NO_ERROR)
	  {
	    return err_set_lsa_and_fetch_page;
	  }
	log_pgptr_reader.add_align (sizeof (LOG_RECORD_HEADER));
	log_pgptr_reader.advance_when_does_not_fit (sizeof (log_rec_t));
	const log_rec_t log_rec
	  = log_pgptr_reader.reinterpret_copy_and_add_align<log_rec_t> ();

	const auto &rcv_vpid = get_vpid ();
	log_rv_redo_record_sync<log_rec_t> (thread_p, log_pgptr_reader, log_rec, rcv_vpid, rcv_lsa, end_redo_lsa,
					    log_rtype, undo_unzip_support, redo_unzip_support);
	return NO_ERROR;
      }

    private:
      const log_lsa rcv_lsa;
      const LOG_LSA *end_redo_lsa;  // by design pointer is guaranteed to outlive this instance
      const LOG_RECTYPE log_rtype;
  };

  /* a class to handle infrastructure for parallel log recovery RAII-style
   */
  class redo_parallel final
  {
    private:
      /* rynchronizes prod/cons of log entries in n-prod - m-cons fashion
       * internal implementation detail
       */
      class redo_job_queue final
      {
	  using ux_redo_job_base = std::unique_ptr<redo_job_base>;
	  using ux_redo_job_deque = std::deque<ux_redo_job_base>;
	  using vpid_set = std::set<VPID>;

	public:
	  redo_job_queue ();
	  ~redo_job_queue ();

	  redo_job_queue (redo_job_queue const &) = delete;
	  redo_job_queue (redo_job_queue &&) = delete;

	  redo_job_queue &operator= (redo_job_queue const &) = delete;
	  redo_job_queue &operator= (redo_job_queue &&) = delete;

	  void locked_push (ux_redo_job_base &&job);

	  /* to be called after all known entries have been added
	   * part of a mechanism to signal to the consumers, together with
	   * whether there is still data to process, that they can bail out
	   */
	  void set_adding_finished ();

	  bool get_adding_finished () const;

	  /* the combination of a null return value with a finished
	   * flag set to true signals to the callers that no more data is expected
	   * and, therefore, they can also terminate
	   */
	  ux_redo_job_base locked_pop (bool &adding_finished);

	  void notify_to_be_waited_for_op_finished ();
	  void notify_in_progress_vpid_finished (VPID a_vpid);

	  /* wait until all data has been consumed internally; blocking call
	   */
	  void wait_for_idle ();

	private:
	  /* two queues are internally managed
	   */
	  ux_redo_job_deque *produce_queue;
	  std::mutex produce_queue_mutex;
	  ux_redo_job_deque *consume_queue;
	  std::mutex consume_queue_mutex;

	  bool queues_empty;
	  std::condition_variable queues_empty_cv;

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
	  std::condition_variable in_progress_vpids_empty_cv;
      };

      /* maintain a bookkeeping of tasks that are still performing work;
       * internal implementation detail
       *
       * TODO: this class is supposed to be polled to see whether any tasks are still active; it might
       * be better to implement this via either a callback or a condition variable
       */
      class redo_task_active_state_bookkeeping final
      {
	public:
	  static constexpr std::size_t BOOKKEEPING_MAX_COUNT = 1024;

	public:
	  redo_task_active_state_bookkeeping ()
	  {
	    active_set.reset ();
	  }

	  redo_task_active_state_bookkeeping (const redo_task_active_state_bookkeeping &) = delete;
	  redo_task_active_state_bookkeeping (redo_task_active_state_bookkeeping &&) = delete;

	  redo_task_active_state_bookkeeping &operator = (const redo_task_active_state_bookkeeping &) = delete;
	  redo_task_active_state_bookkeeping &operator = (redo_task_active_state_bookkeeping &&) = delete;

	  void set_active (std::size_t a_id)
	  {
	    std::lock_guard<std::mutex> lck (active_set_mutex);

	    assert (a_id < BOOKKEEPING_MAX_COUNT);
	    assert (false == active_set.test (a_id)); // supplied id must be unique among all used ids
	    active_set.set (a_id);
	  }

	  void set_inactive (std::size_t a_id)
	  {
	    std::lock_guard<std::mutex> lck (active_set_mutex);

	    assert (a_id < BOOKKEEPING_MAX_COUNT);
	    assert (true == active_set.test (a_id)); // supplied id must be unique among all used ids
	    active_set.reset (a_id);
	  }

	  bool any_active () const
	  {
	    std::lock_guard<std::mutex> lck (active_set_mutex);

	    return active_set.any ();
	  }

	private:
	  std::bitset<BOOKKEEPING_MAX_COUNT> active_set;
	  mutable std::mutex active_set_mutex;
      };


      /* a long running task looping and processing redo log jobs;
       * offers some 'support' instances to the passing guest log jobs: a log reader,
       * unzip support memory for undo data, unzip support memory for redo data;
       * internal implementation detail
       */
      class redo_task final : public cubthread::task<cubthread::entry>
      {
	public:
	  static constexpr unsigned short WAIT_AND_CHECK_MILLIS = 5;

	public:
	  redo_task (std::size_t a_task_id, redo_task_active_state_bookkeeping &a_task_active_state_bookkeeping,
		     redo_job_queue &a_queue);
	  redo_task (const redo_task &) = delete;
	  redo_task (redo_task &&) = delete;

	  redo_task &operator = (const redo_task &) = delete;
	  redo_task &operator = (redo_task &&) = delete;

	  ~redo_task () override;

	  void execute (context_type &context);

	private:
	  // internal bookkeeping variable, must be unique among all task id's within the same pool
	  std::size_t task_id;

	  redo_task_active_state_bookkeeping &task_active_state_bookkeeping;
	  redo_job_queue &queue;

	  log_reader log_pgptr_reader;
	  LOG_ZIP undo_unzip_support;
	  LOG_ZIP redo_unzip_support;
      };

    public:
      redo_parallel (int a_worker_count);

      redo_parallel (const redo_parallel &) = delete;
      redo_parallel (redo_parallel &&) = delete;

      ~redo_parallel ();

      redo_parallel &operator = (const redo_parallel &) = delete;
      redo_parallel &operator = (redo_parallel &&) = delete;

      /* add new work job
       */
      void add (std::unique_ptr<redo_job_base> &&job);

      /* mandatory to explicitly call this after all data have been added
       */
      void set_adding_finished ();

      /* wait until all data has been consumed internally; blocking call
       */
      void wait_for_idle ();

      /* mandatory to explicitly call this before dtor
       */
      void wait_for_termination_and_stop_execution ();

    private:
      void do_init_thread_manager ();
      void do_init_worker_pool ();
      void do_init_tasks ();

    private:
      unsigned task_count;

      cubthread::manager *thread_manager;
      cubthread::entry_manager worker_pool_context_manager;
      cubthread::entry_workpool *worker_pool;

      redo_job_queue queue;
      redo_task_active_state_bookkeeping task_active_state_bookkeeping;

      bool waited_for_termination;
  };
}

#endif // LOG_RECOVERY_REDO_HPP
