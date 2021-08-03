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

#include "log_reader.hpp"
#include "log_recovery_redo.hpp"
#include "log_recovery_redo_perf.hpp"

#if defined(SERVER_MODE)
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
  static constexpr size_t PARALLEL_REDO_REUSABLE_JOBS_STACK_SIZE = 100 * ONE_K;

  // forward declaration
  class reusable_jobs_stack;

  /* minimum_log_lsa_monitor - utility to support calculation of a minimum
   *       lsa out of a set of, concurrently (or not), advacing ones; not
   *       all lsa's participate in the game in every scenarios; those that
   *       do not partake are gracefully ignored by being kept to a maximum
   *       sentinel value
   */
  class minimum_log_lsa_monitor final
  {
    private:
      static constexpr int ARRAY_LENGTH = 4;

      using log_lsa_array_t = std::array<log_lsa, ARRAY_LENGTH>;

      enum ARRAY_INDEX
      {
	PRODUCE_IDX = 0,
	CONSUME_IDX = 1,
	IN_PROGRESS_IDX = 2,
	OUTER_IDX = 3,
      };

    public:
      minimum_log_lsa_monitor ();
      minimum_log_lsa_monitor (const minimum_log_lsa_monitor &) = delete;
      minimum_log_lsa_monitor (minimum_log_lsa_monitor &&) = delete;

      minimum_log_lsa_monitor &operator = (const minimum_log_lsa_monitor &) = delete;
      minimum_log_lsa_monitor &operator = (minimum_log_lsa_monitor &&) = delete;

      void set_for_produce (const log_lsa &a_lsa);
      void set_for_produce_and_consume (const log_lsa &a_produce_lsa, const log_lsa &a_consume_lsa);
      void set_for_consume_and_in_progress (const log_lsa &a_consume_lsa, const log_lsa &a_in_progress_lsa);
      void set_for_in_progress (const log_lsa &a_lsa);
      void set_for_outer (const log_lsa &a_lsa);

      /* obtain the current calculated minimum value
       */
      log_lsa get () const;

      /* wait till the recorder minimum lsa has passed the target lsa
       * blocking call
       *
       * return: minimum log_lsa value calculated internally (mainly for testing purpose)
       */
      log_lsa wait_past_target_lsa (const log_lsa &a_target_lsa);

    private:
      void do_set_at (ARRAY_INDEX a_idx, const log_lsa &a_new_lsa);
      template <typename T_LOCKER>
      log_lsa do_locked_get (const T_LOCKER &) const;

    private:
      mutable std::mutex m_values_mtx;
      log_lsa_array_t m_values;

      std::condition_variable m_wait_for_target_value_cv;
  };

  /* a class to handle infrastructure for parallel log recovery/replication RAII-style;
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
      /* - worker_count: the number of parallel tasks to spin that consume jobs
       */
      redo_parallel (unsigned a_worker_count, minimum_log_lsa_monitor *a_minimum_log_lsa);

      redo_parallel (const redo_parallel &) = delete;
      redo_parallel (redo_parallel &&) = delete;

      ~redo_parallel ();

      redo_parallel &operator = (const redo_parallel &) = delete;
      redo_parallel &operator = (redo_parallel &&) = delete;

      /* add new work job
       */
      void add (redo_job_base *job);

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
	  using redo_job_deque_t = std::deque<redo_parallel::redo_job_base *>;
	  using vpid_set = std::set<VPID>;
	  using log_lsa_set = std::set<log_lsa>;

	public:
	  redo_job_queue (minimum_log_lsa_monitor *a_minimum_log_lsa);
	  ~redo_job_queue ();

	  redo_job_queue (redo_job_queue const &) = delete;
	  redo_job_queue (redo_job_queue &&) = delete;

	  redo_job_queue &operator= (redo_job_queue const &) = delete;
	  redo_job_queue &operator= (redo_job_queue &&) = delete;

	  void push_job (redo_parallel::redo_job_base *a_job);

	  /* to be called after all known entries have been added
	   * part of a mechanism to signal to the consumers, together with
	   * whether there is still data to process, that they can bail out
	   */
	  void set_adding_finished ();

	  /* mostly for debugging
	   */
	  bool get_adding_finished () const;

	  /* the combination of a null return value with a finished
	   * flag set to true signals to the callers that no more data is expected
	   * and, therefore, they can also terminate
	   */
	  redo_parallel::redo_job_base *pop_job (bool &out_adding_finished);

	  void notify_job_finished (const redo_job_base *a_job);

	  /* wait until all data has been consumed internally; blocking call
	   */
	  void wait_for_idle () const;

	private:
	  void assert_idle () const;

	  /* swap internal queues and notify if both are empty
	   * assumes the consume queue is locked
	   */
	  void do_swap_queues_if_needed (const std::lock_guard<std::mutex> &a_consume_lockg);

	  /* find first job that can be consumed (ie: is not already marked
	   * in the 'in progress vpids' set)
	   *
	   * NOTE: '*_locked_*' functions are supposed to be called from within locked
	   * areas with respect to the resources they make use of
	   */
	  redo_parallel::redo_job_base *do_locked_find_job_to_consume_and_mark_in_progress (
		  const std::lock_guard<std::mutex> &a_consume_lockg,
		  const std::lock_guard<std::mutex> &a_in_progress_lockg);

	  /* NOTE: '*_locked_*' functions are supposed to be called from within locked
	   * areas with respect to the resources they make use of
	   */
	  void do_locked_mark_job_in_progress (
		  const std::lock_guard<std::mutex> &a_in_progress_lockg,
		  const redo_parallel::redo_job_base *a_job);

	private:
	  /* two queues are internally managed and take turns at being either
	   * on the producing or on the consumption side
	   */
	  redo_job_deque_t *m_produce_queue;
	  mutable std::mutex m_produce_queue_mutex;
	  redo_job_deque_t *m_consume_queue;
	  std::mutex m_consume_queue_mutex;

	  bool m_queues_empty;
	  mutable std::condition_variable m_queues_empty_cv;

	  std::atomic_bool m_adding_finished;

	  /* bookkeeping for log entries currently in process of being applied, this
	   * mechanism guarantees ordering among entries with the same VPID;
	   */
	  vpid_set m_in_progress_vpids;
	  log_lsa_set m_in_progress_lsas;
	  mutable std::mutex m_in_progress_mutex;
	  /* signalled when the 'in progress' containers are empty
	   */
	  mutable std::condition_variable m_in_progress_vpids_empty_cv;

	  /* utility class to maintain a minimum log_lsa that is still
	   * to be processed (consumed); non-owning pointer, can be null
	   */
	  minimum_log_lsa_monitor *m_minimum_log_lsa;
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
      const unsigned m_task_count;
      std::unique_ptr<cubthread::entry_manager> m_pool_context_manager;

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
      redo_job_base (VPID a_vpid, const log_lsa &a_log_lsa)
	: m_vpid (a_vpid), m_log_lsa (a_log_lsa)
      {
      }
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
      inline const VPID &get_vpid () const
      {
	assert (!VPID_ISNULL (&m_vpid));
	return m_vpid;
      }

      inline const log_lsa &get_log_lsa () const
      {
	assert (!LSA_ISNULL (&m_log_lsa));
	return m_log_lsa;
      }

      inline void reinitialize (VPID a_vpid, const log_lsa &a_log_lsa)
      {
	assert (!VPID_ISNULL (&a_vpid));
	m_vpid = a_vpid;

	assert (!LSA_ISNULL (&a_log_lsa));
	m_log_lsa = a_log_lsa;
      }

      virtual int execute (THREAD_ENTRY *thread_p, log_reader &log_pgptr_reader,
			   LOG_ZIP &undo_unzip_support, LOG_ZIP &redo_unzip_support) = 0;
      virtual void retire () = 0;

    private:
      VPID m_vpid;
      log_lsa m_log_lsa;
  };


  /* actual job implementation that performs log recovery redo
   */
  class redo_job_impl final : public redo_parallel::redo_job_base
  {
    public:
      /*
       *  force_each_page_fetch: force fetch log pages each time regardless of other internal
       *                        conditions; needed to be enabled when job is dispatched in
       *                        page server recovery context
       */
      redo_job_impl (const log_lsa *a_end_redo_lsa, bool force_each_page_fetch,
		     reusable_jobs_stack *a_reusable_job_stack);

      redo_job_impl (redo_job_impl const &) = delete;
      redo_job_impl (redo_job_impl &&) = delete;

      ~redo_job_impl () override = default;

      redo_job_impl &operator = (redo_job_impl const &) = delete;
      redo_job_impl &operator = (redo_job_impl &&) = delete;

      void reinitialize (VPID a_vpid, const log_lsa &a_rcv_lsa, LOG_RECTYPE a_log_rtype);

      int execute (THREAD_ENTRY *thread_p, log_reader &log_pgptr_reader,
		   LOG_ZIP &undo_unzip_support, LOG_ZIP &redo_unzip_support) override;
      void retire () override;

    private:
      template <typename T>
      inline void
      read_record_and_redo (THREAD_ENTRY *thread_p, log_reader &log_pgptr_reader,
			    const VPID &rcv_vpid, const log_lsa &rcv_lsa,
			    LOG_ZIP &undo_unzip_support, LOG_ZIP &redo_unzip_support);

    private:
      // by design pointer is guaranteed to outlive this instance
      const log_lsa *const m_end_redo_lsa;
      const log_reader::fetch_mode m_log_reader_page_fetch_mode;
      reusable_jobs_stack *const m_reusable_job_stack;

      LOG_RECTYPE m_log_rtype;
  };


  /* a preallocated pool of jobs
   */
  class reusable_jobs_stack final
  {
    public:
      reusable_jobs_stack ();
      reusable_jobs_stack (const reusable_jobs_stack &) = delete;
      reusable_jobs_stack (reusable_jobs_stack &&) = delete;

      ~reusable_jobs_stack ();

      reusable_jobs_stack &operator = (const reusable_jobs_stack &) = delete;
      reusable_jobs_stack &operator = (reusable_jobs_stack &&) = delete;

      void initialize (std::size_t a_stack_size, const log_lsa *a_end_redo_lsa,
		       bool force_each_page_fetch);

      std::size_t size () const
      {
	return m_stack_size;
      }

      redo_job_impl *blocking_pop ();
      void push (redo_job_impl *a_job);

    private:
      std::size_t m_stack_size;

      /* pop is done, unsynchronized, from this stack
       * if empty, it is re-filled, synchronized, from the push stack
       */
      std::stack<redo_job_impl *> m_pop_stack;
      /* push happens, synchronized, on this stack
       */
      std::stack<redo_job_impl *> m_push_stack;
      std::mutex m_mutex;
      /* used to wait for jobs to be available on the push stack
       * in case no jobs are found on the pop stack
       */
      std::condition_variable m_jobs_available_on_push_stack_cv;
  };

#else /* SERVER_MODE */
  /* dummy implementations for SA mode
   */
  class minimum_log_lsa_monitor final { };
  class redo_parallel final { };
  class reusable_jobs_stack final { };
#endif /* SERVER_MODE */
}

/*
 * log_rv_redo_record_sync_or_dispatch_async - execute a redo record synchronously or
 *                    (in future) asynchronously
 *
 * return: nothing
 *
 *   thread_p(in):
 *   log_pgptr_reader(in/out): log reader
 *   log_rec(in): log record structure with info about the location and size of the data in the log page
 *   rcv_lsa(in): Reset data page (rcv->pgptr) to this LSA
 *   log_rtype(in): log record type needed to check if diff information is needed or should be skipped
 *   undo_unzip_support(out): extracted undo data support structure (set as a side effect)
 *   redo_unzip_support(out): extracted redo data support structure (set as a side effect); required to
 *                    be passed by address because it also functions as an internal working buffer;
 *                    functions as an outside managed (ie: longer lived) buffer space for the underlying
 *                    logic to perform its work; the pointer to the buffer is then passed - non-owningly
 *                    - to the rcv structure which, in turn, is passed to the actual apply function
 *   force_each_log_page_fetch(in): the log page will be fetched anew each time a fetch is requested
 *                    regardless of other considerations
 *
 */
template <typename T>
void
log_rv_redo_record_sync_or_dispatch_async (
	THREAD_ENTRY *thread_p, log_reader &log_pgptr_reader,
	const T &log_rec, const log_lsa &rcv_lsa,
	const LOG_LSA *end_redo_lsa, LOG_RECTYPE log_rtype,
	LOG_ZIP &undo_unzip_support, LOG_ZIP &redo_unzip_support,
	std::unique_ptr<cublog::redo_parallel> &parallel_recovery_redo,
	cublog::reusable_jobs_stack &a_reusable_jobs,
	bool force_each_log_page_fetch, log_recovery_redo_perf_stat &a_rcv_redo_perf_stat)
{
  const VPID rcv_vpid = log_rv_get_log_rec_vpid<T> (log_rec);
  // at this point, vpid can either be valid or not

#if defined(SERVER_MODE)
  const LOG_DATA &log_data = log_rv_get_log_rec_data<T> (log_rec);
  const bool need_sync_redo = log_rv_need_sync_redo (rcv_vpid, log_data.rcvindex);
  a_rcv_redo_perf_stat.time_and_increment (PERF_STAT_ID_REDO_OR_PUSH_PREP);

  // once vpid is extracted (or not), and depending on parameters, either dispatch the applying of
  // log redo asynchronously, or invoke synchronously
  if (parallel_recovery_redo == nullptr || need_sync_redo)
    {
      // invoke sync
      log_rv_redo_record_sync<T> (thread_p, log_pgptr_reader, log_rec, rcv_vpid, rcv_lsa, end_redo_lsa, log_rtype,
				  undo_unzip_support, redo_unzip_support);
      a_rcv_redo_perf_stat.time_and_increment (PERF_STAT_ID_REDO_OR_PUSH_DO_SYNC);
    }
  else
    {
      // dispatch async
      cublog::redo_job_impl *const job = a_reusable_jobs.blocking_pop ();
      assert (job != nullptr);
      a_rcv_redo_perf_stat.time_and_increment (PERF_STAT_ID_REDO_OR_PUSH_POP_REUSABLE);

      job->reinitialize (rcv_vpid, rcv_lsa, log_rtype);
      // it is the callee's responsibility to return the pointer back to the reusable
      // job stack after having processed it
      parallel_recovery_redo->add (job);
      a_rcv_redo_perf_stat.time_and_increment (PERF_STAT_ID_REDO_OR_PUSH_DO_ASYNC);
    }
#else // !SERVER_MODE = SA_MODE
  log_rv_redo_record_sync<T> (thread_p, log_pgptr_reader, log_rec, rcv_vpid, rcv_lsa, end_redo_lsa, log_rtype,
			      undo_unzip_support, redo_unzip_support);
#endif // !SERVER_MODE = SA_MODE
}

#endif // _LOG_RECOVERY_REDO_PARALLEL_HPP_
