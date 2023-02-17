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

#if defined (SERVER_MODE)
#include "thread_manager.hpp"
#include "thread_worker_pool.hpp"
#include "vpid_utilities.hpp"

#include <atomic>
#include <bitset>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <set>
#endif /* SERVER_MODE */

namespace cublog
{
#if defined (SERVER_MODE)
  // TODO: these constants could be dynamic based on expected load of recovery/replication
  static constexpr std::size_t PARALLEL_REDO_REUSABLE_JOBS_COUNT = ONE_M;
  static constexpr std::size_t PARALLEL_REDO_JOB_VECTOR_RESERVE_SIZE = ONE_M;
  static constexpr std::size_t PARALLEL_REDO_REUSABLE_JOBS_FLUSH_BACK_COUNT = ONE_K;

  // forward declaration
  class reusable_jobs_stack;

  /* handle infrastructure for parallel log recovery/replication RAII-style;
   * usage:
   *  - instantiate an object of this class with the desired number of background workers
   *  - create and add jobs - see 'redo_job_impl'
   *  - after all jobs have been added, explicitly call 'wait_for_termination_and_stop_execution'
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
      redo_parallel (unsigned a_worker_count, bool a_do_monitor_min_unapplied_log_lsa,
		     const log_lsa &a_start_main_thread_log_lsa, const log_rv_redo_context &copy_context,
		     thread_type replication_thread_type);

      redo_parallel (const redo_parallel &) = delete;
      redo_parallel (redo_parallel &&) = delete;

      ~redo_parallel ();

      redo_parallel &operator = (const redo_parallel &) = delete;
      redo_parallel &operator = (redo_parallel &&) = delete;

      /* add new work job
       */
      void add (redo_job_base *job);

      /* mandatory to explicitly call this before dtor
       * blocking call
       */
      void wait_for_termination_and_stop_execution ();

      void log_perf_stats () const;

      void set_main_thread_unapplied_log_lsa (const log_lsa &a_log_lsa);

      void wait_past_target_lsa (const log_lsa &a_target_lsa);

    private:
      void do_init_worker_pool (std::size_t a_task_count);
      void do_init_tasks (std::size_t a_task_count, bool a_do_monitor_unapplied_log_lsa,
			  const log_rv_redo_context &copy_context);

    private:
      /* maintain a bookkeeping of tasks that are still performing work;
       * internal implementation detail; not to be used externally
       */
      class task_active_state_bookkeeping final
      {
	  static constexpr std::size_t BITSET_MAX_SIZE = 256;

	public:
	  task_active_state_bookkeeping (std::size_t a_size);

	  task_active_state_bookkeeping (const task_active_state_bookkeeping &) = delete;
	  task_active_state_bookkeeping (task_active_state_bookkeeping &&) = delete;

	  task_active_state_bookkeeping &operator = (const task_active_state_bookkeeping &) = delete;
	  task_active_state_bookkeeping &operator = (task_active_state_bookkeeping &&) = delete;

	  inline void set_active (std::size_t a_index);
	  inline bool is_active (std::size_t a_index) const;
	  inline void set_inactive (std::size_t a_index);
	  bool is_any_active () const;

	  /* blocking call until all active tasks terminate
	   */
	  inline void wait_for_termination ();

	private:
	  const std::size_t m_size;
	  std::bitset<BITSET_MAX_SIZE> m_values;
	  mutable std::mutex m_values_mtx;
	  std::condition_variable m_values_cv;
      };

      /* handles monitoring of minimum unapplied log_lsa
       */
      class min_unapplied_log_lsa_monitoring final
      {
	public:
	  min_unapplied_log_lsa_monitoring (bool a_do_monitor, const log_lsa &a_start_main_thread_log_lsa,
					    const std::vector<std::unique_ptr<redo_task>> &a_redo_task);

	  min_unapplied_log_lsa_monitoring (const min_unapplied_log_lsa_monitoring &) = delete;
	  min_unapplied_log_lsa_monitoring (min_unapplied_log_lsa_monitoring &&) = delete;

	  ~min_unapplied_log_lsa_monitoring ();

	  min_unapplied_log_lsa_monitoring &operator= (const min_unapplied_log_lsa_monitoring &) = delete;
	  min_unapplied_log_lsa_monitoring &operator= (min_unapplied_log_lsa_monitoring &&) = delete;

	  /* only start calculation once the tasks have been created; avoid race
	   * conditions of the internal calculation and actual creation of the tasks
	   */
	  void start ();

	  void set_main_thread_unapplied_log_lsa (const log_lsa &a_log_lsa);

	  /* blocking call
	   */
	  void wait_past_target_log_lsa (const log_lsa &a_target_lsa);

	private:
	  log_lsa calculate ();
	  void calculate_loop ();

	private:
	  const bool m_do_monitor;

	  /* the not-applied set from the outside by the main thread (for recovery, the main thread
	   * is the thread running the analysis redo and undo; for replication, the main thread is the
	   * recovery applying thread);
	   * the reason the main thread log_lsa is needed is because some of the log entries are being applied
	   * synchronously by the main thread and the calculation of the minimum not-applied log_lsa needs
	   * to be accurate
	   */
	  std::atomic<log_lsa> m_main_thread_unapplied_log_lsa;

	  const std::vector<std::unique_ptr<redo_task>> &m_redo_tasks;

	  /* following members control the pro-active calculation of the minimum not-applied log_lsa as well
	   * as means to actually trigger and wait for the calculation asynchronously
	   */
	  log_lsa m_calculated_log_lsa;
	  std::mutex m_calculate_mtx;
	  volatile bool m_terminate_calculation;
	  std::thread m_calculate_thread;
	  std::condition_variable m_calculate_cv;
      };

    private:
      const std::size_t m_task_count;

      std::unique_ptr<cubthread::entry_manager> m_pool_context_manager;

      /* the workpool already has and internal bookkeeping and can also wait for the tasks to terminate;
       * however, it also has a hardcoded maximum wait time (60 seconds) after which it will assert;
       * adding this internal additional bookeeping - which does not assume a maximum wait time - allows
       * to circumvent that hardcoded parameter
       */
      task_active_state_bookkeeping m_task_state_bookkeeping;

      cubthread::entry_workpool *m_worker_pool;
      /* tasks have owner-controlled life-time in order to be able to
       * collect post-execution perf stats from them
       */
      std::vector<std::unique_ptr<redo_task>> m_redo_tasks;

      std::hash<VPID> m_vpid_hash;

      min_unapplied_log_lsa_monitoring m_min_unapplied_log_lsa_calculation;
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
      redo_job_base () = default;

      redo_job_base (VPID a_vpid, const log_lsa &a_log_lsa)
	: m_vpid (a_vpid), m_log_lsa (a_log_lsa)
      {
      }
      redo_job_base (redo_job_base const &) = default;
      redo_job_base (redo_job_base &&) = default;

      virtual ~redo_job_base () = default;

      redo_job_base &operator = (redo_job_base const &) = default;
      redo_job_base &operator = (redo_job_base &&) = default;

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

      inline void set_record_info (VPID a_vpid, const log_lsa &a_log_lsa)
      {
	assert (!VPID_ISNULL (&a_vpid));
	m_vpid = a_vpid;

	assert (!LSA_ISNULL (&a_log_lsa));
	m_log_lsa = a_log_lsa;
      }

      virtual int execute (THREAD_ENTRY *thread_p, log_rv_redo_context &redo_context) = 0;
      virtual void retire (std::size_t a_task_idx) = 0;

    private:
      VPID m_vpid = { NULL_PAGEID, NULL_VOLID };
      log_lsa m_log_lsa = NULL_LSA;
  };


  /* actual job implementation that performs log recovery/replication redo,
   * also used for log replication
   */
  class redo_job_impl final : public redo_parallel::redo_job_base
  {
    public:
      /*
       *  force_each_page_fetch: force fetch log pages each time regardless of other internal
       *                        conditions; needed to be enabled when job is dispatched in
       *                        page server recovery context
       */
      redo_job_impl (reusable_jobs_stack *a_reusable_job_stack);

      redo_job_impl (redo_job_impl const &) = default;
      redo_job_impl (redo_job_impl &&) = default;

      ~redo_job_impl () override = default;

      redo_job_impl &operator = (redo_job_impl const &) = default;
      redo_job_impl &operator = (redo_job_impl &&) = default;

      void set_record_info (VPID a_vpid, const log_lsa &a_rcv_lsa, LOG_RECTYPE a_log_rtype);

      int execute (THREAD_ENTRY *thread_p, log_rv_redo_context &redo_context) override;
      void retire (std::size_t a_task_idx) override;

    private:
      template <typename T>
      inline void read_record_and_redo (THREAD_ENTRY *thread_p, log_rv_redo_context &redo_context);

    private:
      // by design pointer is guaranteed to outlive this instance
      reusable_jobs_stack *m_reusable_job_stack = nullptr;

      LOG_RECTYPE m_log_rtype = LOG_SMALLER_LOGREC_TYPE;
  };


  /* a preallocated pool of jobs
   */
  class reusable_jobs_stack final
  {
      using job_container_t = std::vector<redo_job_impl *>;

    public:
      reusable_jobs_stack ();
      reusable_jobs_stack (const reusable_jobs_stack &) = delete;
      reusable_jobs_stack (reusable_jobs_stack &&) = delete;

      ~reusable_jobs_stack ();

      reusable_jobs_stack &operator = (const reusable_jobs_stack &) = delete;
      reusable_jobs_stack &operator = (reusable_jobs_stack &&) = delete;

      void initialize (std::size_t a_push_task_count);

      redo_job_impl *blocking_pop (perf_stats &a_rcv_redo_perf_stat);
      void push (std::size_t a_task_idx, redo_job_impl *a_job);

    private:
      /* configuration, constants after initialization
       */
      const std::size_t m_flush_push_at_count;

      /* support array for initializing jobs in-place
       */
      std::vector<redo_job_impl> m_job_pool;

      /* pop is done, unsynchronized, from this container
       * if empty, it is re-filled, synchronized, from the push container
       */
      job_container_t m_pop_jobs;

      /* from time to time, the worker tasks (threads) also push data into this
       * container using the synchronization primitives below
       */
      job_container_t m_push_jobs;
      std::mutex m_push_mtx;
      std::condition_variable m_push_jobs_available_cv;

      /* each worker task (thread) pushes, unsynched on its own container from this vector
       * at configurable intervals, objects are further moved, synchronized, to the push container
       */
      std::vector<job_container_t> m_per_task_push_jobs_vec;
  };
#else /* SERVER_MODE */
  /* dummy implementations for SA mode
   */
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
	THREAD_ENTRY *thread_p, log_rv_redo_context &redo_context, const log_rv_redo_rec_info<T> &record_info,
	std::unique_ptr<cublog::redo_parallel> &parallel_recovery_redo,
	cublog::reusable_jobs_stack &a_reusable_jobs, cublog::perf_stats &a_rcv_redo_perf_stat)
{
  const VPID rcv_vpid = log_rv_get_log_rec_vpid<T> (record_info.m_logrec);
  // at this point, vpid can either be valid or not

#if defined (SERVER_MODE)
  const LOG_DATA &log_data = log_rv_get_log_rec_data<T> (record_info.m_logrec);
  const bool need_sync_redo = log_rv_need_sync_redo (rcv_vpid, log_data.rcvindex);
  a_rcv_redo_perf_stat.time_and_increment (cublog::PERF_STAT_ID_REDO_OR_PUSH_PREP);

  // once vpid is extracted (or not), and depending on parameters, either dispatch the applying of
  // log redo asynchronously, or invoke synchronously
  if (parallel_recovery_redo == nullptr || need_sync_redo)
    {
      // invoke sync
      log_rv_redo_record_sync<T> (thread_p, redo_context, record_info, rcv_vpid);
      a_rcv_redo_perf_stat.time_and_increment (cublog::PERF_STAT_ID_REDO_OR_PUSH_DO_SYNC);
    }
  else
    {
      // dispatch async
      cublog::redo_job_impl *const job = a_reusable_jobs.blocking_pop (a_rcv_redo_perf_stat);
      assert (job != nullptr);

      job->set_record_info (rcv_vpid, record_info.m_start_lsa, record_info.m_type);
      // it is the callee's responsibility to return the pointer back to the reusable
      // job stack after having processed it
      parallel_recovery_redo->add (job);
      a_rcv_redo_perf_stat.time_and_increment (cublog::PERF_STAT_ID_REDO_OR_PUSH_DO_ASYNC);
    }
#else // !SERVER_MODE = SA_MODE
  log_rv_redo_record_sync<T> (thread_p, redo_context, record_info, rcv_vpid);
#endif // !SERVER_MODE = SA_MODE
}

#endif // _LOG_RECOVERY_REDO_PARALLEL_HPP_
