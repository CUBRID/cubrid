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

#include "log_recovery_redo_parallel.hpp"

#include "log_manager.h"
#include "vpid.hpp"

namespace cublog
{
  /*********************************************************************
   * redo_parallel::task_active_state_bookkeeping - definition
   *********************************************************************/

  void
  redo_parallel::task_active_state_bookkeeping::set_active ()
  {
    std::lock_guard<std::mutex> lck { m_active_count_mtx };
    ++m_active_count;
    assert (m_active_count > 0);
  }

  void
  redo_parallel::task_active_state_bookkeeping::set_inactive ()
  {
    bool do_notify { false };
    {
      std::lock_guard<std::mutex> lck { m_active_count_mtx };
      assert (m_active_count > 0);
      --m_active_count;
      do_notify = m_active_count == 0;
    }

    if (do_notify)
      {
	m_active_count_cv.notify_one ();
      }
  }

  void
  redo_parallel::task_active_state_bookkeeping::wait_for_termination ()
  {
    std::unique_lock<std::mutex> lck (m_active_count_mtx);
    m_active_count_cv.wait (lck, [this] ()
    {
      const auto res = m_active_count == 0;
      return res;
    });
  }

  /*********************************************************************
   * redo_parallel::redo_task - declaration
   *********************************************************************/

  /* a long running task looping and processing redo log jobs;
   * offers some 'support' instances to the passing guest log jobs: a log reader,
   * unzip support memory for undo data, unzip support memory for redo data;
   * internal implementation detail; not to be used externally
   */
  class redo_parallel::redo_task final : public cubthread::task<cubthread::entry>
  {
    public:
      static constexpr unsigned short WAIT_AND_CHECK_MILLIS = 5;

    private:
      using redo_job_vector_t = std::vector<redo_parallel::redo_job_base *>;

    public:
      redo_task () = delete;
      redo_task (std::size_t a_task_idx, bool a_do_monitor_not_applied_log_lsa,
		 redo_parallel::task_active_state_bookkeeping &task_state_bookkeeping,
		 const log_rv_redo_context &copy_context, std::atomic_bool &a_adding_finished);
      redo_task (const redo_task &) = delete;
      redo_task (redo_task &&) = delete;

      ~redo_task () override;

      redo_task &operator = (const redo_task &) = delete;
      redo_task &operator = (redo_task &&) = delete;

      void execute (context_type &context) override;
      void retire () override;

      void log_perf_stats () const;
      void accumulate_perf_stats (cubperf::stat_value *a_output_stats, std::size_t a_output_stats_size) const;

      bool is_idle () const;

      inline void push_job (redo_parallel::redo_job_base *a_job);

      inline log_lsa get_not_applied_log_lsa ();

    private:
      inline void pop_jobs (redo_job_vector_t *&a_out_job_vec);

      inline void set_not_applied_log_lsa_from_push_side (const log_lsa &a_log_lsa);
      inline void set_not_applied_log_lsa_from_pop_side (const log_lsa &a_log_lsa);

    private:
      const std::size_t m_task_idx;
      const bool m_do_monitor_not_applied_log_lsa;

      redo_parallel::task_active_state_bookkeeping &m_task_state_bookkeeping;

      cubperf::statset_definition m_perf_stats_definition;
      perf_stats m_perf_stats;

      log_rv_redo_context m_redo_context;

      redo_job_vector_t *m_produce_vec;
      mutable std::mutex m_produce_vec_mtx;

      std::atomic_bool m_in_execution;
      std::atomic_bool &m_adding_finished;

      /* minimum still-to-be-applied (not-applied) log_lsa for a single log applying task
       * scenarios:
       * - job is pushed for processing to the internal waiting queue:
       *    - internal waiting queue and processing queue are still empty:
       *      - old: MAX_LSA
       *      - new: pushed job's log_lsa
       *    - either internal waiting queue, or processing queue, or both are non-empty:
       *      - old: minimum log_lsa is smaller than that of the currently pushed job since
       *        jobs log_lsa's are in ever increasing order
       * - task picks-up existing jobs in the internal waiting queue
       *    - if previosly processing queue was non empty:
       *      - minimum log_lsa from the previous update from the processing queue
       *    - if previous processing queue was empty
       *      - if waiting queue is empty
       *        - MAX_LSA
       *      - if waiting queue is non-empty
       *        - minimum log_lsa from waiting queue
       * - task has just finished processing a job:
       *    - there are still jobs to process in the processing queue:
       *      - minimum log_lsa from the processing queue (ie: the log_lsa of the next job since
       *        log_lsa are inserted in ever increasing order
       *    - processing queue is empty
       *      - no change,the minimum log_lsa will be advanced upon subsequent request to pop
       *        jobs from the internal waiting queue (see corresponding step)
       *
       * - a job is pushed for processing to the internal waiting queue:
       *    - if current minimum log_lsa is MAX_LSA, the newly added job's log_lsa
       *        becomes the new minimum log_lsa
       *    - if current minimum log_lsa is not MAX_LSA, assert that the newly added
       *        job has a log_lsa greater than the existing minimum log_lsa
       */
      std::atomic<log_lsa> m_not_applied_log_lsa;
  };

  /*********************************************************************
   * redo_parallel::redo_task - definition
   *********************************************************************/

  constexpr unsigned short redo_parallel::redo_task::WAIT_AND_CHECK_MILLIS;

  redo_parallel::redo_task::redo_task (std::size_t a_task_idx, bool a_do_monitor_not_applied_log_lsa,
				       redo_parallel::task_active_state_bookkeeping &task_state_bookkeeping,
				       const log_rv_redo_context &copy_context, std::atomic_bool &a_adding_finished)
    : m_task_idx { a_task_idx }
    , m_do_monitor_not_applied_log_lsa { a_do_monitor_not_applied_log_lsa }
    , m_task_state_bookkeeping (task_state_bookkeeping)
    , m_perf_stats_definition { perf_stats_async_definition_init_list }
    , m_perf_stats { perf_stats_is_active_for_async (), m_perf_stats_definition }
    , m_redo_context { copy_context }
    , m_produce_vec { new redo_job_vector_t () }
  , m_adding_finished { a_adding_finished }
  , m_not_applied_log_lsa { MAX_LSA }
  {
    // important to set this at this moment and not when execution begins
    // to circumvent race conditions where all tasks haven't yet started work
    // while already bookkeeping is being checked
    m_task_state_bookkeeping.set_active ();
    m_in_execution.store (true);

    m_produce_vec->reserve (PARALLEL_REDO_JOB_VECTOR_RESERVE_SIZE);
  }

  redo_parallel::redo_task::~redo_task ()
  {
    assert (is_idle ());
    assert (m_produce_vec != nullptr);
    delete m_produce_vec;
    m_produce_vec = nullptr;
  }

  void
  redo_parallel::redo_task::execute (context_type &context)
  {
    redo_job_vector_t *jobs_vec = new redo_job_vector_t ();
    // according to spec, reserved size survives clearing of the vector
    // which should help to only allocate/reserve once
    jobs_vec->reserve (PARALLEL_REDO_JOB_VECTOR_RESERVE_SIZE);

    for (bool finished = false; !finished ;)
      {
	pop_jobs (jobs_vec);
	const bool jobs_popped { !jobs_vec->empty ()};
	m_perf_stats.time_and_increment (cublog::PERF_STAT_ID_PARALLEL_POP);
	if (!jobs_popped && m_adding_finished.load ())
	  {
	    finished = true;
	  }
	else
	  {
	    if (!jobs_popped)
	      {
		// TODO: if needed, check if requested to finish ourselves

		// no notification towards the parent for empty here, because at the next attempt
		// we might get more work

		// expecting more data, sleep and check again
		std::this_thread::sleep_for (std::chrono::milliseconds (WAIT_AND_CHECK_MILLIS));
		m_perf_stats.time_and_increment (cublog::PERF_STAT_ID_PARALLEL_SLEEP);
	      }
	    else
	      {
		assert (!jobs_vec->empty ());
		THREAD_ENTRY *const thread_entry = &context;
		for (auto &job : *jobs_vec)
		  {
		    if (m_do_monitor_not_applied_log_lsa)
		      {
			set_not_applied_log_lsa_from_pop_side (job->get_log_lsa ());
		      }
		    job->execute (thread_entry, m_redo_context);
		    job->retire (m_task_idx);
		  }

		// pointers still present in the vector are either:
		//  - already passed on to the reusable job container
		//  - dangling, as they have deleted themselves
		jobs_vec->clear ();
		m_perf_stats.time_and_increment (cublog::PERF_STAT_ID_PARALLEL_EXECUTE_AND_RETIRE);
	      }
	  }
      }

    assert (jobs_vec != nullptr);
    assert (jobs_vec->empty ());
    delete jobs_vec;

    m_task_state_bookkeeping.set_inactive ();
    m_in_execution.store (false);
  }

  void
  redo_parallel::redo_task::retire ()
  {
    // avoid self destruct, will be deleted by owning class
  }

  void
  redo_parallel::redo_task::log_perf_stats () const
  {
    std::stringstream ss;
    ss << "Log recovery redo worker thread " << m_task_idx << " perf stats";
    m_perf_stats.log (ss.str ().c_str ());
  }

  void
  redo_parallel::redo_task::accumulate_perf_stats (
	  cubperf::stat_value *a_output_stats, std::size_t a_output_stats_size) const
  {
    m_perf_stats.accumulate (a_output_stats, a_output_stats_size);
  }

  bool
  redo_parallel::redo_task::is_idle () const
  {
    bool is_vector_empty = false;
    {
      std::scoped_lock<std::mutex> slock { m_produce_vec_mtx };
      is_vector_empty = m_produce_vec->empty ();
    }
    return is_vector_empty && !m_in_execution.load ();
  }

  inline void
  redo_parallel::redo_task::push_job (redo_parallel::redo_job_base *a_job)
  {
    std::scoped_lock<std::mutex> slock { m_produce_vec_mtx };

    if (m_do_monitor_not_applied_log_lsa && m_produce_vec->empty ())
      {
	const log_lsa &job_log_lsa = a_job->get_log_lsa ();
	set_not_applied_log_lsa_from_push_side (job_log_lsa);
      }

    m_produce_vec->push_back (a_job);
  }

  inline void
  redo_parallel::redo_task::pop_jobs (redo_parallel::redo_task::redo_job_vector_t *&a_out_job_vec)
  {
    assert (a_out_job_vec->empty ());

    std::scoped_lock<std::mutex> slock { m_produce_vec_mtx };
    std::swap (m_produce_vec, a_out_job_vec);

    if (m_do_monitor_not_applied_log_lsa)
      {
	const log_lsa new_log_lsa =  a_out_job_vec->empty ()
				     ? MAX_LSA
				     : (*a_out_job_vec->begin ())->get_log_lsa ();
	set_not_applied_log_lsa_from_pop_side (new_log_lsa);
      }
  }

  inline void
  redo_parallel::redo_task::set_not_applied_log_lsa_from_push_side (const log_lsa &a_log_lsa)
  {
    assert (m_do_monitor_not_applied_log_lsa);

    {
      const log_lsa snapshot_not_applied_log_lsa { m_not_applied_log_lsa.load () };
      // strict comparison because jobs have ever-increasing log_lsa's
      assert (snapshot_not_applied_log_lsa == MAX_LSA || snapshot_not_applied_log_lsa < a_log_lsa);
    }

    log_lsa expected_not_applied_log_lsa { MAX_LSA };
    m_not_applied_log_lsa.compare_exchange_strong (expected_not_applied_log_lsa, a_log_lsa);
    // TODO: any check for the return value?
  }

  inline void
  redo_parallel::redo_task::set_not_applied_log_lsa_from_pop_side (const log_lsa &a_log_lsa)
  {
    assert (m_do_monitor_not_applied_log_lsa);

    log_lsa expected_log_lsa;
    do
      {
	expected_log_lsa = m_not_applied_log_lsa.load ();
	// -or-equal because push side can fill in a minimum log_lsa, while the task (ie: pop side) is
	// still empty, which is then found and tested-only by the pop side
	assert (expected_log_lsa <= a_log_lsa);
      }
    while (!m_not_applied_log_lsa.compare_exchange_strong (expected_log_lsa, a_log_lsa));
  }

  inline log_lsa
  redo_parallel::redo_task::get_not_applied_log_lsa ()
  {
    assert (m_do_monitor_not_applied_log_lsa);

    return m_not_applied_log_lsa.load ();
  }

  /*********************************************************************
   * redo_parallel - definition
   *********************************************************************/

  redo_parallel::redo_parallel (unsigned a_task_count, bool a_do_monitor_minimum_not_applied_log_lsa,
				const log_rv_redo_context &copy_context)
    : m_task_count { a_task_count }
    , m_do_monitor_minimum_log_lsa { a_do_monitor_minimum_not_applied_log_lsa }
    , m_worker_pool (nullptr)
    , m_waited_for_termination (false)
    , m_adding_finished { false }
    , m_outer_not_applied_log_lsa { MAX_LSA }
    , m_calculated_minimum_not_applied_log_lsa { MAX_LSA }
    , m_terminate_minimum_not_applied_log_lsa_calculation { false }
  {
    assert (a_task_count > 0);

    const thread_type tt = log_is_in_crash_recovery () ? TT_RECOVERY : TT_REPLICATION;
    m_pool_context_manager = std::make_unique<cubthread::system_worker_entry_manager> (tt);

    do_init_worker_pool (a_task_count);
    do_init_tasks (a_task_count, a_do_monitor_minimum_not_applied_log_lsa, copy_context);

    if (m_do_monitor_minimum_log_lsa)
      {
	m_calculate_minimum_not_applied_log_lsa_thread = std::thread
	{
	  std::bind (&redo_parallel::redo_parallel::calculate_and_save_minimum_not_applied_log_lsa, std::ref (*this))
	};
      }
  }

  redo_parallel::~redo_parallel ()
  {
    assert (m_adding_finished.load ());
    assert (m_worker_pool == nullptr);
    assert (m_waited_for_termination);
    for (auto &redo_task: m_redo_tasks)
      {
	assert (redo_task->is_idle ());
      }

    if (m_do_monitor_minimum_log_lsa)
      {
	assert (m_calculate_minimum_not_applied_log_lsa_thread.joinable ());
	m_terminate_minimum_not_applied_log_lsa_calculation = true;
	m_calculate_minimum_not_applied_log_lsa_cv.notify_one ();
	m_calculate_minimum_not_applied_log_lsa_thread.join ();
      }
    else
      {
	assert (!m_calculate_minimum_not_applied_log_lsa_thread.joinable ());
	assert (m_calculated_minimum_not_applied_log_lsa == MAX_LSA);
      }
  }

  void
  redo_parallel::add (redo_job_base *a_job)
  {
    assert (false == m_waited_for_termination);
    assert (false == m_adding_finished.load ());

    const vpid &job_vpid = a_job->get_vpid ();
    const std::size_t vpid_hash = m_vpid_hash (job_vpid);
    const std::size_t task_index = vpid_hash % m_task_count;

    redo_task *const task = m_redo_tasks[task_index].get ();
    task->push_job (a_job);
  }

  void
  redo_parallel::set_adding_finished ()
  {
    assert (false == m_waited_for_termination);
    assert (false == m_adding_finished.load ());

    m_adding_finished.store (true);
  }

  void
  redo_parallel::wait_for_termination_and_stop_execution ()
  {
    assert (false == m_waited_for_termination);
    assert (m_adding_finished.load ());

    // blocking call
    m_task_state_bookkeeping.wait_for_termination ();

    for (auto &redo_task: m_redo_tasks)
      {
	assert (redo_task->is_idle ());
      }

    m_worker_pool->stop_execution ();
    cubthread::manager *thread_manager = cubthread::get_manager ();
    thread_manager->destroy_worker_pool (m_worker_pool);
    assert (m_worker_pool == nullptr);

    m_waited_for_termination = true;
  }

  void
  redo_parallel::do_init_worker_pool (std::size_t a_task_count)
  {
    assert (a_task_count > 0);
    assert (m_worker_pool == nullptr);

    // NOTE: already initialized globally (probably during boot)
    cubthread::manager *thread_manager = cubthread::get_manager ();

    m_worker_pool = thread_manager->create_worker_pool (a_task_count, a_task_count, "log_recovery_redo_thread_pool",
		    m_pool_context_manager.get (),
		    a_task_count, false /*debug_logging*/);
  }

  void
  redo_parallel::do_init_tasks (std::size_t a_task_count, bool a_do_monitor_not_applied_log_lsa,
				const log_rv_redo_context &copy_context)
  {
    assert (a_task_count > 0);
    assert (m_worker_pool != nullptr);

    for (unsigned task_idx = 0; task_idx < a_task_count; ++task_idx)
      {
	auto task = std::make_unique<redo_parallel::redo_task> (task_idx, a_do_monitor_not_applied_log_lsa,
		    m_task_state_bookkeeping, copy_context, m_adding_finished);
	m_worker_pool->execute (task.get ());
	m_redo_tasks.push_back (std::move (task));
      }
  }

  void
  redo_parallel::log_perf_stats () const
  {
    if ( !perf_stats_is_active_for_async () )
      {
	return;
      }

    const cubperf::statset_definition definition
    {
      perf_stats_async_definition_init_list
    };

    std::vector<cubperf::stat_value> accum_perf_stat_results;
    accum_perf_stat_results.resize (definition.get_value_count ());

    for (auto &redo_task: m_redo_tasks)
      {
	redo_task->accumulate_perf_stats (accum_perf_stat_results.data (), accum_perf_stat_results.size ());
	redo_task->log_perf_stats ();
      }

    // average
    const std::size_t task_count = m_redo_tasks.size ();
    const std::size_t value_count = accum_perf_stat_results.size ();
    std::vector<cubperf::stat_value> avg_perf_stat_results;
    avg_perf_stat_results.resize (value_count, 0);
    for (std::size_t idx = 0; idx < value_count; ++idx)
      {
	avg_perf_stat_results[idx] = accum_perf_stat_results[idx] / task_count;
      }

    log_perf_stats_values_with_definition ("Log recovery redo worker threads averaged perf stats",
					   definition, avg_perf_stat_results.data (), value_count);
  }

  void
  redo_parallel::set_outer_not_applied_log_lsa (const log_lsa &a_log_lsa)
  {
    assert (m_do_monitor_minimum_log_lsa);

    m_outer_not_applied_log_lsa.store (a_log_lsa);
  }

  log_lsa
  redo_parallel::get_calculated_minimum_not_applied_log_lsa ()
  {
    assert (m_do_monitor_minimum_log_lsa);

    std::scoped_lock<std::mutex> slock { m_calculate_minimum_not_applied_log_lsa_mtx };
    return m_calculated_minimum_not_applied_log_lsa;
  }

  log_lsa
  redo_parallel::wait_past_target_lsa (const log_lsa &a_target_lsa)
  {
    assert (m_do_monitor_minimum_log_lsa);
    assert (a_target_lsa != MAX_LSA);
    assert (a_target_lsa != NULL_LSA);

    log_lsa calculated_minimum_not_applied_log_lsa { MAX_LSA };

    {
      // avoid gratuitously notifying the calculating internal thread if the condition is already satisfied
      std::scoped_lock<std::mutex> slock { m_calculate_minimum_not_applied_log_lsa_mtx };
      calculated_minimum_not_applied_log_lsa = m_calculated_minimum_not_applied_log_lsa;
    }

    if (calculated_minimum_not_applied_log_lsa <= a_target_lsa)
      {
	// there is only one thread doing this calculation
	m_calculate_minimum_not_applied_log_lsa_cv.notify_one ();

	{
	  std::unique_lock<std::mutex> ulock { m_calculate_minimum_not_applied_log_lsa_mtx };
	  m_calculate_minimum_not_applied_log_lsa_cv.wait (ulock,
	      [this, &calculated_minimum_not_applied_log_lsa, &a_target_lsa] ()
	  {
	    calculated_minimum_not_applied_log_lsa = m_calculated_minimum_not_applied_log_lsa;
	    assert (!m_adding_finished.load ());
	    return (calculated_minimum_not_applied_log_lsa == MAX_LSA)
		   || (calculated_minimum_not_applied_log_lsa > a_target_lsa);
	  });
	}
      }

    return calculated_minimum_not_applied_log_lsa;
  }

  log_lsa
  redo_parallel::calculate_minimum_log_lsa ()
  {
    assert (m_do_monitor_minimum_log_lsa);

    log_lsa minimum_not_applied_log_lsa { m_outer_not_applied_log_lsa.load () };
    for (auto &redo_task: m_redo_tasks)
      {
	const log_lsa minimum_task_log_lsa { redo_task->get_not_applied_log_lsa () };
	if (minimum_task_log_lsa != MAX_LSA && minimum_task_log_lsa < minimum_not_applied_log_lsa)
	  {
	    minimum_not_applied_log_lsa = minimum_task_log_lsa;
	  }
      }

    // - assert might be invalid due to the calculating thread which might kick pro-active
    //    calculation before any client requests it
    // - also, a MAX_LSA result might mean that all threads have finished processing jobs
    //assert (minimum_not_applied_log_lsa != MAX_LSA);

    return minimum_not_applied_log_lsa;
  }

  void
  redo_parallel::calculate_and_save_minimum_not_applied_log_lsa ()
  {
    assert (m_do_monitor_minimum_log_lsa);

    while (!m_terminate_minimum_not_applied_log_lsa_calculation)
      {
	// calculation happens outside lock to not hold waiting threads
	const log_lsa calculated_minimum_not_applied_log_lsa = calculate_minimum_log_lsa ();
	{
	  std::scoped_lock<std::mutex> slock { m_calculate_minimum_not_applied_log_lsa_mtx };
	  m_calculated_minimum_not_applied_log_lsa = calculated_minimum_not_applied_log_lsa;
	}

	// there might be more than one waiting thread
	m_calculate_minimum_not_applied_log_lsa_cv.notify_all ();

	{
	  std::unique_lock<std::mutex> ulock { m_calculate_minimum_not_applied_log_lsa_mtx };
	  // wait might be interrupted by an outside notify and this is expected
	  m_calculate_minimum_not_applied_log_lsa_cv.wait_for (ulock, std::chrono::milliseconds (1000));
	}
      }
  }

  /*********************************************************************
   * redo_job_impl - definition
   *********************************************************************/

  redo_job_impl::redo_job_impl (reusable_jobs_stack *a_reusable_job_stack)
    : redo_parallel::redo_job_base (VPID_INITIALIZER, NULL_LSA)
    , m_reusable_job_stack { a_reusable_job_stack }
    , m_log_rtype { LOG_SMALLER_LOGREC_TYPE }
  {
    assert (a_reusable_job_stack != nullptr);
  }

  void redo_job_impl::set_record_info (VPID a_vpid, const log_lsa &a_rcv_lsa, LOG_RECTYPE a_log_rtype)
  {
    this->redo_job_base::set_record_info (a_vpid, a_rcv_lsa);
    assert (a_log_rtype > LOG_SMALLER_LOGREC_TYPE && a_log_rtype < LOG_LARGER_LOGREC_TYPE);
    m_log_rtype = a_log_rtype;
  }

  int redo_job_impl::execute (THREAD_ENTRY *thread_p, log_rv_redo_context &redo_context)
  {
    /* perf data for processing log redo asynchronously, enabled:
     *  - during log crash recovery
     *  - on the page server, when replication is executing in the asynchronous mode
     * in both cases, it does include the part that effectively calls the redo function, so, for accurate
     * evaluation the part that effectively executes the redo function must be accounted for
     */

    const int err_fetch =
	    redo_context.m_reader.set_lsa_and_fetch_page (get_log_lsa (), redo_context.m_reader_fetch_page_mode);
    if (err_fetch != NO_ERROR)
      {
	return err_fetch;
      }
    redo_context.m_reader.add_align (sizeof (LOG_RECORD_HEADER));
    switch (m_log_rtype)
      {
      case LOG_REDO_DATA:
	read_record_and_redo<log_rec_redo> (thread_p, redo_context);
	break;
      case LOG_MVCC_REDO_DATA:
	read_record_and_redo<log_rec_mvcc_redo> (thread_p, redo_context);
	break;
      case LOG_UNDOREDO_DATA:
      case LOG_DIFF_UNDOREDO_DATA:
	read_record_and_redo<log_rec_undoredo> (thread_p, redo_context);
	break;
      case LOG_MVCC_UNDOREDO_DATA:
      case LOG_MVCC_DIFF_UNDOREDO_DATA:
	read_record_and_redo<log_rec_mvcc_undoredo> (thread_p, redo_context);
	break;
      case LOG_RUN_POSTPONE:
	read_record_and_redo<log_rec_run_postpone> (thread_p, redo_context);
	break;
      case LOG_COMPENSATE:
	read_record_and_redo<log_rec_compensate> (thread_p, redo_context);
	break;
      default:
	assert (false);
	break;
      }

    return NO_ERROR;
  }

  void redo_job_impl::retire (std::size_t a_task_idx)
  {
    // return the job back to the pool of available reusable jobs
    m_reusable_job_stack->push (a_task_idx, this);
  }

  template <typename T>
  inline void
  redo_job_impl::read_record_and_redo (THREAD_ENTRY *thread_p, log_rv_redo_context &redo_context)
  {
    redo_context.m_reader.advance_when_does_not_fit (sizeof (T));
    log_rv_redo_rec_info<T> record_info (get_log_lsa (), m_log_rtype,
					 redo_context.m_reader.reinterpret_copy_and_add_align<T> ());

    log_rv_redo_record_sync<T> (thread_p, redo_context, record_info, get_vpid ());
  }

  /*********************************************************************
   * reusable_jobs_stack - definition
   *********************************************************************/

  reusable_jobs_stack::reusable_jobs_stack ()
    : m_flush_push_at_count { cublog::PARALLEL_REDO_REUSABLE_JOBS_FLUSH_BACK_COUNT }
  {
  }

  void reusable_jobs_stack::initialize (std::size_t a_push_task_count)
  {
    assert (m_pop_jobs.empty ());
    assert (m_push_jobs.empty ());

    assert (m_per_task_push_jobs_vec.empty ());

    assert (a_push_task_count > 0);

    m_job_pool.resize (PARALLEL_REDO_REUSABLE_JOBS_COUNT, redo_job_impl (this));

    m_pop_jobs.reserve (m_job_pool.size ());
    for (std::size_t idx = 0; idx < m_job_pool.size (); ++idx)
      {
	m_pop_jobs.push_back (&m_job_pool[idx]);
      }

    m_push_jobs.reserve (m_job_pool.size ());

    m_per_task_push_jobs_vec.resize (a_push_task_count);
    for (job_container_t &jobs: m_per_task_push_jobs_vec)
      {
	jobs.reserve (m_flush_push_at_count);
      }
  }

  reusable_jobs_stack::~reusable_jobs_stack ()
  {
    // consistency check that all job instances have been 'returned to the source'
    assert ([this] ()
    {
      const std::size_t pop_size = m_pop_jobs.size ();
      const std::size_t push_size = m_push_jobs.size ();
      std::size_t per_task_push_size = 0;
      for (auto &push_container: m_per_task_push_jobs_vec)
	{
	  per_task_push_size += push_container.size ();
	}
      assert ((pop_size + push_size + per_task_push_size) == m_job_pool.size ());
      return true;
    }
    ());
  }

  redo_job_impl *reusable_jobs_stack::blocking_pop (perf_stats &a_rcv_redo_perf_stat)
  {
    if (!m_pop_jobs.empty ())
      {
	redo_job_impl *const pop_job = m_pop_jobs.back ();
	m_pop_jobs.pop_back ();
	a_rcv_redo_perf_stat.time_and_increment (PERF_STAT_ID_REDO_OR_PUSH_POP_REUSABLE_DIRECT);
	return pop_job;
      }
    else
      {
	{
	  std::unique_lock<std::mutex> locku { m_push_mtx };
	  m_push_jobs_available_cv.wait (locku, [this] ()
	  {
	    return !m_push_jobs.empty ();
	  });

	  m_pop_jobs.swap (m_push_jobs);
	}

	redo_job_impl *const pop_job = m_pop_jobs.back ();
	m_pop_jobs.pop_back ();
	a_rcv_redo_perf_stat.time_and_increment (PERF_STAT_ID_REDO_OR_PUSH_POP_REUSABLE_WAIT);
	return pop_job;
      }

    assert ("unreachable state reached" == nullptr);
    return nullptr;
  }

  void reusable_jobs_stack::push (std::size_t a_task_idx, redo_job_impl *a_job)
  {
    job_container_t &push_jobs = m_per_task_push_jobs_vec[a_task_idx];
    push_jobs.push_back (a_job);

    if (push_jobs.size () > m_flush_push_at_count)
      {
	{
	  std::lock_guard<std::mutex> locku { m_push_mtx };
	  m_push_jobs.insert (m_push_jobs.end (), push_jobs.cbegin (), push_jobs.cend ());
	}
	push_jobs.clear ();
	m_push_jobs_available_cv.notify_one ();
      }
  }
}
