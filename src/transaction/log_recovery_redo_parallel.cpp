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
   * redo_parallel::redo_job_queue::minimum_log_lsa - utility to
   *       support calculation of a minimum log_lsa while taking care to
   *       ignore those that are null
   *********************************************************************/

  minimum_log_lsa_monitor::minimum_log_lsa_monitor ()
    : m_min_calculated_lsa { MAX_LSA }
  {
    /* MAX_LSA is used as an internal sentinel value; this is considered safe as long as,
     * if the engine actually gets to use this value, things are already haywire elsewhere
     */
    std::lock_guard<std::mutex> lck { m_values_mtx };
    m_values[PRODUCE_IDX] = MAX_LSA;
    m_values[CONSUME_IDX] = MAX_LSA;
    m_values[IN_PROGRESS_IDX] = MAX_LSA;
  }

  void
  minimum_log_lsa_monitor::set_for_produce (const log_lsa &a_lsa)
  {
    assert (NULL_LSA != a_lsa);
    {
      std::lock_guard<std::mutex> lck { m_values_mtx };
      m_values[PRODUCE_IDX] = a_lsa;

      do_locked_calculate_and_set_minimum (lck);
    }
    m_wait_for_target_value_cv.notify_one ();
  }

  void
  minimum_log_lsa_monitor::set_for_consume (const log_lsa &a_lsa)
  {
    assert (NULL_LSA != a_lsa);
    {
      std::lock_guard<std::mutex> lck { m_values_mtx };
      m_values[CONSUME_IDX] = a_lsa;

      do_locked_calculate_and_set_minimum (lck);
    }
    m_wait_for_target_value_cv.notify_one ();
  }

  void
  minimum_log_lsa_monitor::set_for_produce_and_consume (
	  const log_lsa &a_produce_lsa, const log_lsa &a_consume_lsa)
  {
    assert (NULL_LSA != a_produce_lsa);
    assert (NULL_LSA != a_consume_lsa);
    {
      std::lock_guard<std::mutex> lck { m_values_mtx };
      m_values[PRODUCE_IDX] = a_produce_lsa;
      m_values[CONSUME_IDX] = a_consume_lsa;

      do_locked_calculate_and_set_minimum (lck);
    }
    m_wait_for_target_value_cv.notify_one ();
  }

  void
  minimum_log_lsa_monitor::set_for_in_progress (const log_lsa &a_lsa)
  {
    assert (NULL_LSA != a_lsa);
    {
      std::lock_guard<std::mutex> lck { m_values_mtx };
      m_values[IN_PROGRESS_IDX] = a_lsa;

      do_locked_calculate_and_set_minimum (lck);
    }
    m_wait_for_target_value_cv.notify_one ();
  }

  log_lsa
  minimum_log_lsa_monitor::get () const
  {
    std::lock_guard<std::mutex> lck { m_values_mtx };
    return m_min_calculated_lsa;
  }

  void
  minimum_log_lsa_monitor::do_locked_calculate_and_set_minimum (const std::lock_guard<std::mutex> &)
  {
    const log_lsa new_minimum_log_lsa = std::min (
    {
      m_values[PRODUCE_IDX],
      m_values[CONSUME_IDX],
      m_values[IN_PROGRESS_IDX]
    });
    m_min_calculated_lsa = new_minimum_log_lsa;
  }

  log_lsa
  minimum_log_lsa_monitor::wait_for_target_lsa (const log_lsa &a_target_lsa)
  {
    assert (!a_target_lsa.is_null ());

    // check upfront to avoid gratuitously waiting:
    // - the condition is met already
    // - no minimum lsa can actually be calculated, return immediately with the value
    //   and client should be able to decide what to do
    {
      const log_lsa minimum_lsa = get ();
      if (/*minimum_lsa.is_null () ||*/ minimum_lsa == MAX_LSA || minimum_lsa > a_target_lsa)
	{
	  return minimum_lsa;
	}
    }

    // otherwise, wait
    std::unique_lock<std::mutex> lck { m_values_mtx };
    log_lsa outer_res;
    m_wait_for_target_value_cv.wait (lck, [this, &a_target_lsa, &outer_res] ()
    {
      assert (m_min_calculated_lsa != MAX_LSA);
      if (m_min_calculated_lsa > a_target_lsa)
	{
	  outer_res = m_min_calculated_lsa;
	  return true;
	}
      return false;
    });
    return outer_res;
  }

  /*********************************************************************
   * redo_parallel::redo_job_queue - definition
   *********************************************************************/

  redo_parallel::redo_job_queue::redo_job_queue ()
    : m_produce_queue (new ux_redo_job_deque ())
    , m_consume_queue (new ux_redo_job_deque ())
    , m_queues_empty (true)
    , m_adding_finished { false }
  {
  }

  redo_parallel::redo_job_queue::~redo_job_queue ()
  {
    assert_idle ();

    delete m_produce_queue;
    m_produce_queue = nullptr;

    delete m_consume_queue;
    m_consume_queue = nullptr;
  }

  void
  redo_parallel::redo_job_queue::push_job (ux_redo_job_base &&job)
  {
    std::lock_guard<std::mutex> lck (m_produce_queue_mutex);
    if (m_produce_queue->empty ())
      {
	m_minimum_log_lsa_to_process.set_for_produce (job->get_log_lsa ());
      }
    m_produce_queue->push_back (std::move (job));
    m_queues_empty = false;
  }

  void
  redo_parallel::redo_job_queue::set_adding_finished ()
  {
    m_adding_finished.store (true);
  }

  bool
  redo_parallel::redo_job_queue::get_adding_finished () const
  {
    return m_adding_finished.load ();
  }

  redo_parallel::redo_job_queue::ux_redo_job_base
  redo_parallel::redo_job_queue::pop_job (bool &out_adding_finished)
  {
    std::unique_lock<std::mutex> consume_queue_lock (m_consume_queue_mutex);

    out_adding_finished = false;

    do_swap_queues_if_needed (consume_queue_lock);

    if (m_consume_queue->size () > 0)
      {
	ux_redo_job_base job_to_consume;
	{
	  // TODO: instead of every task sifting through entries locked in execution by other tasks,
	  // promote those entries as they are found to separate queues on a per-VPID basis; thus,
	  // when that specific task enters the critical section, it will first investigate these
	  // promoted queues to see if there's smth to consume from there; this has the added benefit that
	  // it will also, possibly, bundle together consecutive entries that pertain to that task, thus,
	  // further, possibly reducing contention on the happy path (ie: many consecutive same-VPID entries)
	  // and also cache localization/affinity given that the same task (presumabily scheduled on the same
	  // core) might end up consuming consecutive same-VPID entries which are applied to same location in
	  // memory

	  // access the m_in_progress_vpids guarded; another option to reduce contention would
	  // be to copy the contents (while guarded) and then check against the contents outside of lock
	  std::lock_guard<std::mutex> in_progress_lock (m_in_progress_mutex);
	  job_to_consume = do_locked_find_job_to_consume ();
	  if (job_to_consume == nullptr)
	    {
	      // specifically leave the 'out_adding_finished' on false:
	      //  - even if adding has been finished, the produce queue might still have jobs to be consumed
	      //  - if so, setting the out param to true here, will cause consuming tasks to finish
	      //  - thus allowing for a corner case where the jobs are not fully processed and all
	      //    tasks have finished executing

	      // consumer will have to spin-wait
	      return nullptr;
	    }

	  // IDEA: if this is a  non-synched op, search all other entries from the current queue
	  // pertaining to the same vpid and consolidate them all in a single 'execution'; stop
	  // when all entries in the consume queue have been added or when a synched (to-be-waited-for)
	  // entry is found

	  do_locked_mark_job_started (job_to_consume);
	} // unlock m_in_progress_vpids

	// unlocking consume queue before this will not guarantee total ordering amongst entries' execution

	return job_to_consume;
      }
    else
      {
	// because two alternating queues are used internally, and because, when the consumption queue
	// is being almost exhausted (i.e.: there are still entries to be consumed but all of them are locked
	// by other tasks - via the m_in_progress_vpids), there are a few times when false negatives are returned
	// to the consumption tasks (see the 'return nullptr' in the 'then' branch); but, if control reaches here
	// it is ensured that indeed no more data exists and that no more data will be produced
	out_adding_finished = m_adding_finished.load ();

	// if no more data will be produced (signalled by the flag), the
	// consumer will just need to terminate; otherwise, consumer is expected to
	// spin-wait and try again
	return nullptr;
      }
  }

  void
  redo_parallel::redo_job_queue::do_swap_queues_if_needed (const std::unique_lock<std::mutex> &)
  {
    // PRECOND: consume queue must be locked at this point
    // if consumption of everything in consume queue finished; see whether there's some more in the other one
    if (m_consume_queue->size () == 0)
      {
	// TODO: a second barrier such that, consumption threads do not 'busy wait' by
	// constantly swapping two empty containers; works in combination with a notification
	// dispatched after new work items are added to the produce queue; this, in effect means that
	// this function will semantically become 'blocking_pop'

	// consumer queue is locked anyway, interested in not locking the producer queue
	bool notify_queues_empty = false;
	{
	  // effectively, an 'atomic' swap because both queues are locked
	  std::lock_guard<std::mutex> lck (m_produce_queue_mutex);
	  std::swap (m_produce_queue, m_consume_queue);
	  // if both queues empty, notify the, possibly waiting, producing thread
	  m_queues_empty = m_produce_queue->empty () && m_consume_queue->empty ();
	  notify_queues_empty = m_queues_empty;

	  const log_lsa produce_minimum_log_lsa = m_produce_queue->empty ()
						  ? MAX_LSA
						  : (*m_produce_queue->begin ())->get_log_lsa ();
	  const log_lsa consume_minimum_log_lsa = m_consume_queue->empty ()
						  ? MAX_LSA
						  : (* m_consume_queue->begin ())->get_log_lsa ();
	  m_minimum_log_lsa_to_process.set_for_produce_and_consume (
		  produce_minimum_log_lsa, consume_minimum_log_lsa);
	}
	if (notify_queues_empty)
	  {
	    m_queues_empty_cv.notify_one ();
	  }
      }
  }

  redo_parallel::redo_job_queue::ux_redo_job_base
  redo_parallel::redo_job_queue::do_locked_find_job_to_consume ()
  {
    // PRECOND: both queues must be locked at this point
    ux_redo_job_base job;
    ux_redo_job_deque::iterator consume_queue_it = m_consume_queue->begin ();
    for (; consume_queue_it != m_consume_queue->end (); ++consume_queue_it)
      {
	const vpid it_vpid = (*consume_queue_it)->get_vpid ();
	if (m_in_progress_vpids.find ((it_vpid)) == m_in_progress_vpids.cend ())
	  {
	    const bool replace_produce_lsa { consume_queue_it == m_consume_queue->begin () };
	    job = std::move (*consume_queue_it);
	    m_consume_queue->erase (consume_queue_it);

	    if (replace_produce_lsa)
	      {
		const log_lsa consume_minimum_log_lsa = m_consume_queue->empty ()
							? MAX_LSA
							: (* m_consume_queue->begin ())->get_log_lsa ();
		m_minimum_log_lsa_to_process.set_for_consume (consume_minimum_log_lsa);
	      }

	    break;
	  }
      }

    // if null here, consumer task will have to spin-wait
    return job;
  }

  void
  redo_parallel::redo_job_queue::do_locked_mark_job_started (const ux_redo_job_base &a_job)
  {
    assert (m_in_progress_vpids.size () == m_in_progress_lsas.size ());

    const vpid &job_vpid = a_job->get_vpid ();
    assert (m_in_progress_vpids.find (job_vpid) == m_in_progress_vpids.cend ());
    m_in_progress_vpids.insert (job_vpid);

    const log_lsa &job_log_lsa = a_job->get_log_lsa ();
    assert (m_in_progress_lsas.find (job_log_lsa) == m_in_progress_lsas.cend ());
    m_in_progress_lsas.insert (job_log_lsa);

    m_minimum_log_lsa_to_process.set_for_in_progress (*m_in_progress_lsas.cbegin ());
  }

  void
  redo_parallel::redo_job_queue::notify_job_finished (const ux_redo_job_base &a_job)
  {
    bool set_empty = false;
    {
      std::lock_guard<std::mutex> in_progress_lock (m_in_progress_mutex);

      const auto &job_vpid = a_job->get_vpid ();
      const auto vpid_it = m_in_progress_vpids.find (job_vpid);
      assert (vpid_it != m_in_progress_vpids.cend ());
      m_in_progress_vpids.erase (vpid_it);
      set_empty = m_in_progress_vpids.empty ();

      const log_lsa &job_log_lsa = a_job->get_log_lsa ();
      const auto log_lsa_it = m_in_progress_lsas.find (job_log_lsa);
      assert (log_lsa_it != m_in_progress_lsas.cend ());
      m_in_progress_lsas.erase (log_lsa_it);

      const log_lsa in_progress_minimum_log_lsa = m_in_progress_lsas.empty ()
	  ? MAX_LSA
	  : *m_in_progress_lsas.cbegin ();
      m_minimum_log_lsa_to_process.set_for_in_progress (in_progress_minimum_log_lsa);

      assert (m_in_progress_vpids.size () == m_in_progress_lsas.size ());
    }
    if (set_empty)
      {
	m_in_progress_vpids_empty_cv.notify_one ();
      }
  }

  void
  redo_parallel::redo_job_queue::wait_for_idle () const
  {
    {
      std::unique_lock<std::mutex> empty_queues_lock (m_produce_queue_mutex);
      m_queues_empty_cv.wait (empty_queues_lock, [this] ()
      {
	return m_queues_empty;
      });
    }

    {
      std::unique_lock<std::mutex> empty_in_progress_lock (m_in_progress_mutex);
      m_in_progress_vpids_empty_cv.wait (empty_in_progress_lock, [this] ()
      {
	return m_in_progress_vpids.empty ();
      });
      assert (m_in_progress_lsas.empty ());
    }

    assert_idle ();
  }

  bool
  redo_parallel::redo_job_queue::is_idle () const
  {
    bool queues_empty = false;
    bool in_progress_vpids_empty = false;
    {
      std::lock_guard<std::mutex> empty_queues_lock (m_produce_queue_mutex);
      queues_empty = m_queues_empty;
    }
    {
      std::lock_guard<std::mutex> empty_in_progress_lock (m_in_progress_mutex);
      in_progress_vpids_empty = m_in_progress_vpids.empty ();
    }
    return queues_empty && in_progress_vpids_empty;
  }

  log_lsa
  redo_parallel::redo_job_queue::get_minimum_unprocessed_lsa () const
  {
    return m_minimum_log_lsa_to_process.get ();
  }

  void
  redo_parallel::redo_job_queue::assert_idle () const
  {
    assert (m_produce_queue->size () == 0);
    assert (m_consume_queue->size () == 0);
    assert (m_in_progress_vpids.size () == 0);
  }

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

    public:
      redo_task (redo_parallel::task_active_state_bookkeeping &task_state_bookkeeping
		 , redo_parallel::redo_job_queue &a_queue);
      redo_task (const redo_task &) = delete;
      redo_task (redo_task &&) = delete;

      redo_task &operator = (const redo_task &) = delete;
      redo_task &operator = (redo_task &&) = delete;

      ~redo_task () override;

      void execute (context_type &context);

    private:
      redo_parallel::task_active_state_bookkeeping &m_task_state_bookkeeping;
      redo_parallel::redo_job_queue &m_queue;

      log_reader m_log_pgptr_reader;
      LOG_ZIP m_undo_unzip_support;
      LOG_ZIP m_redo_unzip_support;
  };

  /*********************************************************************
   * redo_parallel::redo_task - definition
   *********************************************************************/

  constexpr unsigned short redo_parallel::redo_task::WAIT_AND_CHECK_MILLIS;

  redo_parallel::redo_task::redo_task (redo_parallel::task_active_state_bookkeeping &task_state_bookkeeping
				       , redo_parallel::redo_job_queue &a_queue)
    : m_task_state_bookkeeping (task_state_bookkeeping), m_queue (a_queue)
  {
    // important to set this at this moment and not when execution begins
    // to circumvent race conditions where all tasks haven't yet started work
    // while already bookkeeping is being checked
    m_task_state_bookkeeping.set_active ();

    log_zip_realloc_if_needed (m_undo_unzip_support, LOGAREA_SIZE);
    log_zip_realloc_if_needed (m_redo_unzip_support, LOGAREA_SIZE);
  }

  redo_parallel::redo_task::~redo_task ()
  {
    log_zip_free_data (m_undo_unzip_support);
    log_zip_free_data (m_redo_unzip_support);
  }

  void
  redo_parallel::redo_task::execute (context_type &context)
  {
    bool finished = false;

    for (; !finished ;)
      {
	bool adding_finished = false;
	std::unique_ptr<redo_job_base> job = m_queue.pop_job (adding_finished);

	if (job == nullptr && adding_finished)
	  {
	    finished = true;
	  }
	else
	  {
	    if (job == nullptr)
	      {
		// TODO: if needed, check if requested to finish ourselves

		// expecting more data, sleep and check again
		std::this_thread::sleep_for (std::chrono::milliseconds (WAIT_AND_CHECK_MILLIS));
	      }
	    else
	      {
		THREAD_ENTRY *const thread_entry = &context;
		job->execute (thread_entry, m_log_pgptr_reader, m_undo_unzip_support, m_redo_unzip_support);

		m_queue.notify_job_finished (job);
	      }
	  }
      }

    m_task_state_bookkeeping.set_inactive ();
  }

  /*********************************************************************
   * redo_parallel - definition
   *********************************************************************/

  redo_parallel::redo_parallel (unsigned a_worker_count)
    : m_task_count { a_worker_count }
    , m_worker_pool (nullptr)
    , m_waited_for_termination (false)
  {
    assert (a_worker_count > 0);

    const thread_type tt = log_is_in_crash_recovery () ? TT_RECOVERY : TT_REPLICATION;
    m_pool_context_manager = std::make_unique<cubthread::system_worker_entry_manager> (tt);

    do_init_worker_pool ();
    do_init_tasks ();
  }

  redo_parallel::~redo_parallel ()
  {
    assert (m_job_queue.get_adding_finished ());
    assert (m_worker_pool == nullptr);
    assert (m_waited_for_termination);
  }

  void
  redo_parallel::add (std::unique_ptr<redo_job_base> &&job)
  {
    assert (false == m_waited_for_termination);
    assert (false == m_job_queue.get_adding_finished ());

    m_job_queue.push_job (std::move (job));
  }

  void
  redo_parallel::set_adding_finished ()
  {
    assert (false == m_waited_for_termination);
    assert (false == m_job_queue.get_adding_finished ());

    m_job_queue.set_adding_finished ();
  }

  void
  redo_parallel::wait_for_termination_and_stop_execution ()
  {
    assert (false == m_waited_for_termination);

    // blocking call
    m_task_state_bookkeeping.wait_for_termination ();

    m_worker_pool->stop_execution ();
    cubthread::manager *thread_manager = cubthread::get_manager ();
    thread_manager->destroy_worker_pool (m_worker_pool);
    assert (m_worker_pool == nullptr);

    m_waited_for_termination = true;
  }

  void
  redo_parallel::wait_for_idle ()
  {
    assert (false == m_waited_for_termination);
    assert (false == m_job_queue.get_adding_finished ());

    m_job_queue.wait_for_idle ();
  }

  bool
  redo_parallel::is_idle () const
  {
    assert (false == m_waited_for_termination);
    assert (false == m_job_queue.get_adding_finished ());

    return m_job_queue.is_idle ();
  }

  log_lsa
  redo_parallel::get_minimum_unprocessed_lsa () const
  {
    return m_job_queue.get_minimum_unprocessed_lsa ();
  }

  void
  redo_parallel::do_init_worker_pool ()
  {
    assert (m_task_count > 0);
    assert (m_worker_pool == nullptr);

    // NOTE: already initialized globally (probably during boot)
    cubthread::manager *thread_manager = cubthread::get_manager ();

    m_worker_pool = thread_manager->create_worker_pool (m_task_count, m_task_count, "log_recovery_redo_thread_pool",
		    m_pool_context_manager.get (),
		    m_task_count, false /*debug_logging*/);
  }

  void
  redo_parallel::do_init_tasks ()
  {
    assert (m_task_count > 0);
    assert (m_worker_pool != nullptr);

    for (unsigned task_idx = 0; task_idx < m_task_count; ++task_idx)
      {
	// NOTE: task ownership goes to the worker pool
	auto task = new redo_task (m_task_state_bookkeeping, m_job_queue);
	m_worker_pool->execute (task);
      }
  }

  /*********************************************************************
   * redo_job_replication_delay_impl - definition
   *********************************************************************/

  redo_job_replication_delay_impl::redo_job_replication_delay_impl (
	  const log_lsa &a_rcv_lsa, time_msec_t a_start_time_msec)
    : redo_parallel::redo_job_base (SENTINEL_VPID, a_rcv_lsa)
    , m_start_time_msec (a_start_time_msec)
  {
  }

  int  redo_job_replication_delay_impl::execute (THREAD_ENTRY *thread_p, log_reader &,
      LOG_ZIP &, LOG_ZIP &)
  {
    const int res = log_rpl_calculate_replication_delay (thread_p, m_start_time_msec);
    return res;
  }
}
