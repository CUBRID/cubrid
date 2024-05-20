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

#ifndef _LOG_RECOVERY_REDO_PARALLEL_HPP_
#define _LOG_RECOVERY_REDO_PARALLEL_HPP_

#if defined(SERVER_MODE)
#include "log_recovery_redo.hpp"

#include "log_compress.h"
#include "thread_manager.hpp"
#include "thread_worker_pool.hpp"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <set>
#endif /* SERVER_MODE */

namespace cublog
{
#if defined(SERVER_MODE)
  /* a class to handle infrastructure for parallel log recovery RAII-style;
   * usage:
   *  - instantiate an object of this class with the desired number of background workers
   *  - create and add jobs - see 'redo_job_impl'
   *  - after all jobs have been added, explicitly call 'set_adding_finished'
   *  - and, as a final step, call 'wait_for_termination_and_stop_execution'; implementation
   *    explicitly needs these last two steps to be executed in this order to be able to clean-up
   */
  class redo_parallel final
  {
    public:
      /* forward declarations for implementation details */
      class redo_job_base;
      class redo_task;

    public:
      redo_parallel (unsigned a_worker_count);

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
      void do_init_worker_pool ();
      void do_init_tasks ();

    private:
      /* rynchronizes prod/cons of log entries in n-prod - m-cons fashion
       * internal implementation detail; not to be used externally
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

	  void notify_in_progress_vpid_finished (VPID a_vpid);

	  /* wait until all data has been consumed internally; blocking call
	   */
	  void wait_for_idle ();

	private:
	  /* swap internal queues and notify if both are empty
	   * assumes the consume queue is locked
	   */
	  void do_swap_queues_if_needed ();

	  /* find first job that can be consumed (ie: is not already marked
	   * in the 'in progress vpids' set)
	   * assumes the the in progress vpids set is locked
	   */
	  ux_redo_job_base do_find_job_to_consume ();

	private:
	  /* two queues are internally managed
	   */
	  ux_redo_job_deque *m_produce_queue;
	  std::mutex m_produce_queue_mutex;
	  ux_redo_job_deque *m_consume_queue;
	  std::mutex m_consume_queue_mutex;

	  bool m_queues_empty;
	  std::condition_variable m_queues_empty_cv;

	  std::atomic_bool m_adding_finished;

	  /* bookkeeping for log entries currently in process of being applied, this
	   * mechanism guarantees ordering among entries with the same VPID;
	   */
	  vpid_set m_in_progress_vpids;
	  std::mutex m_in_progress_vpids_mutex;
	  std::condition_variable m_in_progress_vpids_empty_cv;
      };

      /* maintain a bookkeeping of tasks that are still performing work;
       * internal implementation detail; not to be used externally
       */
      class task_active_state_bookkeeping final
      {
	public:
	  task_active_state_bookkeeping () = default;

	  task_active_state_bookkeeping (const task_active_state_bookkeeping &) = delete;
	  task_active_state_bookkeeping (task_active_state_bookkeeping &&) = delete;

	  task_active_state_bookkeeping &operator = (const task_active_state_bookkeeping &) = delete;
	  task_active_state_bookkeeping &operator = (task_active_state_bookkeeping &&) = delete;

	  /* increment the internal number of active tasks
	   */
	  inline void set_active ();

	  /* decrement the internal number of active tasks
	   */
	  inline void set_inactive ();

	  /* blocking call until all active tasks terminate
	   */
	  inline void wait_for_termination ();

	private:
	  int m_active_count { 0 };
	  std::mutex m_active_count_mtx;
	  std::condition_variable m_active_count_cv;
      };

    private:
      unsigned m_task_count;

      /* the workpool already has and internal bookkeeping and can also wait for the tasks to terminate;
       * however, it also has a hardcoded maximum wait time (60 seconds) after which it will assert;
       * adding this internal additional bookeeping - which does not assume a maximum wait time - allows
       * to circumvent that hardcoded parameter
       */
      task_active_state_bookkeeping m_task_state_bookkeeping;

      cubthread::entry_workpool *m_worker_pool;

      redo_job_queue m_job_queue;

      bool m_waited_for_termination;
  };


  /* contains a single vpid-backed log record entry to be applied on a page;
  * this is a base class designed to allow handling in a container;
  * internal implementation detail; not to be used externally
  *
  * NOTE: an optimization would be to allow this to be a container of more than
  *  one same-vpid log record entry to be applied in one go by the same thread
  */
  class redo_parallel::redo_job_base
  {
    public:
      redo_job_base (VPID a_vpid)
	: m_vpid (a_vpid)
      {
	// the logic implemented below, in the queue and task, does allow for null-vpid type of
	// entries to be executed - they are called "to be waited for" or "synched" operations;
	// but, for now, do not allow them to be dispatched to be executed asynchronously
	assert (!VPID_ISNULL (&m_vpid));
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
	return m_vpid;
      }

      virtual int execute (THREAD_ENTRY *thread_p, log_reader &log_pgptr_reader,
			   LOG_ZIP &undo_unzip_support, LOG_ZIP &redo_unzip_support) = 0;

    private:
      const VPID m_vpid;
  };


  /* actual job implementation that performs log recovery redo
   */
  template <typename TYPE_LOG_REC>
  class redo_job_impl final : public redo_parallel::redo_job_base
  {
      using log_rec_t = TYPE_LOG_REC;

    public:
      redo_job_impl (VPID a_vpid, const log_lsa &a_rcv_lsa, const LOG_LSA *a_end_redo_lsa, LOG_RECTYPE a_log_rtype);

      redo_job_impl (redo_job_impl const &) = delete;
      redo_job_impl (redo_job_impl &&) = delete;

      ~redo_job_impl () override = default;

      redo_job_impl &operator = (redo_job_impl const &) = delete;
      redo_job_impl &operator = (redo_job_impl &&) = delete;

      int execute (THREAD_ENTRY *thread_p, log_reader &log_pgptr_reader,
		   LOG_ZIP &undo_unzip_support, LOG_ZIP &redo_unzip_support) override;

    private:
      const log_lsa m_rcv_lsa;
      const LOG_LSA *m_end_redo_lsa;  // by design pointer is guaranteed to outlive this instance
      const LOG_RECTYPE m_log_rtype;
  };

  /*********************************************************************
   * template/inline implementations
   *********************************************************************/

  template <typename TYPE_LOG_REC>
  redo_job_impl<TYPE_LOG_REC>::redo_job_impl (VPID a_vpid, const log_lsa &a_rcv_lsa, const LOG_LSA *a_end_redo_lsa,
      LOG_RECTYPE a_log_rtype)
    : redo_parallel::redo_job_base (a_vpid)
    , m_rcv_lsa (a_rcv_lsa)
    , m_end_redo_lsa (a_end_redo_lsa)
    , m_log_rtype (a_log_rtype)
  {
  }

  template <typename TYPE_LOG_REC>
  int  redo_job_impl<TYPE_LOG_REC>::execute (THREAD_ENTRY *thread_p, log_reader &log_pgptr_reader,
      LOG_ZIP &undo_unzip_support, LOG_ZIP &redo_unzip_support)
  {
    const int err_set_lsa_and_fetch_page = log_pgptr_reader.set_lsa_and_fetch_page (m_rcv_lsa);
    if (err_set_lsa_and_fetch_page != NO_ERROR)
      {
	return err_set_lsa_and_fetch_page;
      }
    log_pgptr_reader.add_align (sizeof (LOG_RECORD_HEADER));
    log_pgptr_reader.advance_when_does_not_fit (sizeof (log_rec_t));
    const log_rec_t log_rec
      = log_pgptr_reader.reinterpret_copy_and_add_align<log_rec_t> ();

    const auto &rcv_vpid = get_vpid ();
    log_rv_redo_record_sync<log_rec_t> (thread_p, log_pgptr_reader, log_rec, rcv_vpid, m_rcv_lsa, m_end_redo_lsa,
					m_log_rtype, undo_unzip_support, redo_unzip_support);
    return NO_ERROR;
  }

#else /* SERVER_MODE */
  /* dummy implementation for SA mode
   */
  class redo_parallel final
  {
  };
#endif /* SERVER_MODE */
}

#endif // _LOG_RECOVERY_REDO_PARALLEL_HPP_
