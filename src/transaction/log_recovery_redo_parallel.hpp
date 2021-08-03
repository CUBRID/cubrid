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
#include "log_replication.hpp"
#include "log_recovery_redo_perf.hpp"

//#define USE_LOCKFREE_QUEUE

#if defined (USE_LOCKFREE_QUEUE)
#include "lockfree_circular_queue.hpp"
#endif

#if defined (SERVER_MODE)
#include "thread_manager.hpp"
#include "thread_worker_pool.hpp"
#include "vpid_utilities.hpp"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <set>
#endif /* SERVER_MODE */

namespace cublog
{
#if defined (SERVER_MODE)
  // TODO: make reserve size configurable based on expected load of recovery
  static constexpr size_t PARALLEL_RECOVERY_REDO_TUNING_JOB_VECTOR_RESERVE_SIZE = ONE_M;
  static constexpr size_t PARALLEL_RECOVERY_REDO_TUNING_REUSABLE_JOBS_STACK_SIZE = 100 * ONE_K;

  // forward
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
      redo_parallel (unsigned a_worker_count, reusable_jobs_stack *a_reusable_jobs,
		     minimum_log_lsa_monitor *a_minimum_log_lsa);

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
      // TODO: rename to: wait_for_termination because that's the only context in which it is used

      /* check if all fed data has ben consumed internally; non-blocking call
       * NOTE: the nature of this function is 'volatile' - ie: what might be
       * true at the moment the function is called is not necessarily true a moment
       * later; it can be useful only if the caller is aware of the execution context
       */
      bool is_idle () const;

      /* mandatory to explicitly call this before dtor
       */
      void wait_for_termination_and_stop_execution ();

    private:
      void do_init_worker_pool ();
      void do_init_tasks (reusable_jobs_stack *a_reusable_jobs);

    private:
      /* rynchronizes prod/cons of log entries in n-prod - m-cons fashion
       * internal implementation detail; not to be used externally
       */
      class redo_job_queue final
      {
	  using ux_redo_job_base = std::unique_ptr<redo_job_base>;

	public:
	  using ux_redo_job_deque = std::deque<ux_redo_job_base>;
	  using redo_job_vector_t = std::vector<redo_job_base *>;

	private:
	  //using vpid_ux_redo_job_deque_map_t = std::unordered_map<vpid, ux_redo_job_deque, std::hash<VPID>>;
	  using vpid_set = std::set<VPID>;
	  using log_lsa_set = std::set<log_lsa>;
	  using log_lsa_vpid_map_t = std::map<log_lsa, vpid>;
#if defined (USE_LOCKFREE_QUEUE)
	  using job_circ_queue_t = lockfree::circular_queue<redo_job_base *>;
#endif

	public:
	  redo_job_queue () = delete;
	  redo_job_queue (const unsigned m_task_count, minimum_log_lsa_monitor *a_minimum_log_lsa);
	  ~redo_job_queue ();

	  redo_job_queue (redo_job_queue const &) = delete;
	  redo_job_queue (redo_job_queue &&) = delete;

	  redo_job_queue &operator= (redo_job_queue const &) = delete;
	  redo_job_queue &operator= (redo_job_queue &&) = delete;

	  void push_job (redo_job_base *job);

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
	  bool pop_jobs (unsigned a_task_idx, redo_job_vector_t *&in_out_jobs, bool &out_adding_finished);

	  void notify_job_deque_finished (const ux_redo_job_deque &a_job_deque);

	  /* wait until all data has been consumed internally; blocking call
	   */
	  void wait_for_idle () const;

	  /* check if all fed data has ben consumed internally; non-blocking call
	   * NOTE: the nature of this function is 'volatile' - ie: what might be
	   * true at the moment the function is called is not necessarily true a moment
	   * later; it can be useful only if the caller is aware of the execution context
	   */
	  // TODO: remove function, only used in unit tests
	  bool is_idle () const;

	  void set_empty_at (unsigned a_index);

	private:
#if defined (USE_LOCKFREE_QUEUE)
	  void do_push_pre_produce ();
#endif

	  void assert_idle () const;

	  /* swap internal queues and notify if both are empty
	   * assumes the consume queue is locked
	   */
//	  void do_swap_queues_if_needed (const std::lock_guard<std::mutex> &a_consume_lockg);

	  /* find first job that can be consumed (ie: is not already marked
	   * in the 'in progress vpids' set)
	   *
	   * NOTE: '*_locked_*' functions are supposed to be called from within locked
	   * areas with respect to the resources they make use of
	   */
//	  ux_redo_job_deque do_locked_find_job_to_consume_and_mark_in_progress (
//		  const std::lock_guard<std::mutex> &a_consume_lockg,
//		  const std::lock_guard<std::mutex> &a_in_progress_lockg);

	  /* NOTE: '*_locked_*' functions are supposed to be called from within locked
	   * areas with respect to the resources they make use of
	   */
//	  void do_locked_mark_job_deque_in_progress (
//		  const std::lock_guard<std::mutex> &a_in_progress_lockg,
//		  const ux_redo_job_deque &a_job_deque);

	  void set_non_empty_at (unsigned a_index);
//          void do_check_empty_and_notify(std::lock_guard<std::mutex> &a_empty_lockg);

	private:
	  const unsigned m_task_count;

#if defined (USE_LOCKFREE_QUEUE)
	  /* jobs are first added to a pre-produce queue
	   * an internal thread then distributes them to the
	   * produce vector from where they are picked up by their respective threads
	   */
	  job_circ_queue_t m_pre_produce_circ_queue;
	  std::thread m_pre_produce_thr;
	  std::atomic_bool m_pre_produce_finished;
#endif

	  std::vector<redo_job_vector_t *> m_produce_vec;
	  std::hash<VPID> m_vpid_hash;
	  std::vector<std::mutex> m_produce_mutex_vec;

//	  std::map<unsigned, log_lsa> m_produce_index_to_min_lsa_map;
//	  log_lsa_set m_produce_min_lsa_set;

	  /* NOTE: in the next variables, empty actually means 'finished'
	   */
	  /* for each execution task (aka: thread, there is a flag to indicate whether
	   * that thread is empty (aka: has nothing to process at the moment); there is
	   * also one extra position at the end of the vector for the corresponding state
	   * of the pre-produce queue
	   */
	  std::vector<bool> m_empty_vec;
	  mutable std::mutex m_empty_mutex;
	  mutable std::condition_variable m_empty_cv;

	  std::atomic_bool m_adding_finished;

//	  /* bookkeeping for log entries currently in process of being applied, this
//	   * mechanism guarantees ordering among entries with the same VPID;
//	   */
//	  vpid_set m_in_progress_vpids;
//	  log_lsa_set m_in_progress_lsas;
//	  mutable std::mutex m_in_progress_mutex;
//	  /* signalled when the 'in progress' containers are empty
//	   */
//	  mutable std::condition_variable m_in_progress_vpids_empty_cv;

//	  /* utility class to maintain a minimum log_lsa that is still
//	   * to be processed (consumed); non-owning pointer, can be null
//	   */
	  const bool m_monitor_minimum_log_lsa;
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
      inline const VPID &get_vpid () const
      {
	assert (!VPID_ISNULL (&m_vpid));
	return m_vpid;
      }

      inline void set_vpid (const VPID &a_vpid)
      {
	assert (!VPID_ISNULL (&a_vpid));
	m_vpid = a_vpid;
      }

      inline const log_lsa &get_log_lsa () const
      {
	assert (!LSA_ISNULL (&m_log_lsa));
	return m_log_lsa;
      }

      inline void set_log_lsa (const log_lsa &a_log_lsa)
      {
	assert (!LSA_ISNULL (&a_log_lsa));
	m_log_lsa = a_log_lsa;
      }

      virtual int execute (THREAD_ENTRY *thread_p, log_reader &log_pgptr_reader,
			   LOG_ZIP &undo_unzip_support, LOG_ZIP &redo_unzip_support) = 0;

    private:
      VPID m_vpid;
      log_lsa m_log_lsa;
  };


  /* actual job implementation that performs log recovery/replication redo,
   * also used for log replication
   *
   * NOTE: implementation to be used with reusable_jobs_stack below;
   * provides method to be re-initialized for multiple uses
   */
  class redo_job_impl final : public redo_parallel::redo_job_base
  {
    public:
      /*
       *  force_each_page_fetch: force fetch log pages each time regardless of other internal
       *                        conditions; needed to be enabled when job is dispatched in
       *                        page server recovery context
       */
      redo_job_impl (const log_lsa *a_end_redo_lsa, bool force_each_page_fetch);

      redo_job_impl (redo_job_impl const &) = delete;
      redo_job_impl (redo_job_impl &&) = delete;

      ~redo_job_impl () override = default;

      redo_job_impl &operator = (redo_job_impl const &) = delete;
      redo_job_impl &operator = (redo_job_impl &&) = delete;

      void reset (VPID a_vpid, const log_lsa &a_rcv_lsa, LOG_RECTYPE a_log_rtype);

      int execute (THREAD_ENTRY *thread_p, log_reader &log_pgptr_reader,
		   LOG_ZIP &undo_unzip_support, LOG_ZIP &redo_unzip_support) override;

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

      redo_parallel::redo_job_base *pop ();
      void push (std::vector<redo_parallel::redo_job_base *> &a_jobs);

    private:
      std::size_t m_stack_size;
      std::stack<redo_parallel::redo_job_base *> m_stack;
      std::mutex m_stack_mutex;
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
	bool force_each_log_page_fetch,
	log_recovery_redo_perf_stat &a_rcv_redo_perf_stat)
{
  const VPID rcv_vpid = log_rv_get_log_rec_vpid<T> (log_rec);
  // at this point, vpid can either be valid or not

#if defined (SERVER_MODE)
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
      // ownership of raw pointer goes to the callee
      while (true)
	{
	  cublog::redo_parallel::redo_job_base *const job_base = a_reusable_jobs.pop ();
	  if (job_base != nullptr)
	    {
	      cublog::redo_job_impl *const job = dynamic_cast<cublog::redo_job_impl *> (job_base);
	      job->reset (rcv_vpid, rcv_lsa, log_rtype);
	      parallel_recovery_redo->add (job);
	      a_rcv_redo_perf_stat.time_and_increment (PERF_STAT_ID_REDO_OR_PUSH_DO_ASYNC);
	      break;
	    }
	  else
	    {
	      std::this_thread::sleep_for (std::chrono::microseconds (1));
	      a_rcv_redo_perf_stat.time_and_increment (PERF_STAT_ID_REDO_OR_PUSH_POP_BUSY_WAIT);
	    }
	}
    }
#else // !SERVER_MODE = SA_MODE
  log_rv_redo_record_sync<T> (thread_p, log_pgptr_reader, log_rec, rcv_vpid, rcv_lsa, end_redo_lsa, log_rtype,
			      undo_unzip_support, redo_unzip_support);
#endif // !SERVER_MODE = SA_MODE
}

#endif // _LOG_RECOVERY_REDO_PARALLEL_HPP_
