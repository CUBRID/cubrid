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
   * minimum_log_lsa_monitor - definition
   *********************************************************************/

  minimum_log_lsa_monitor::minimum_log_lsa_monitor ()
  {
    /* MAX_LSA is used as an internal sentinel value; this is considered safe as long as,
     * if the engine actually gets to this value, things are already haywire elsewhere
     */
    std::lock_guard<std::mutex> lockg { m_values_mtx };
    m_values[PRODUCE_IDX] = MAX_LSA;
    m_values[CONSUME_IDX] = MAX_LSA;
    m_values[IN_PROGRESS_IDX] = MAX_LSA;
    m_values[OUTER_IDX] = MAX_LSA;
  }

  void
  minimum_log_lsa_monitor::set_for_produce (const log_lsa &a_lsa)
  {
    do_set_at (PRODUCE_IDX, a_lsa);
  }

  void
  minimum_log_lsa_monitor::set_for_produce_and_consume (
	  const log_lsa &a_produce_lsa, const log_lsa &a_consume_lsa)
  {
    assert (!a_produce_lsa.is_null ());
    assert (!a_consume_lsa.is_null ());
    {
      std::lock_guard<std::mutex> lockg { m_values_mtx };
      m_values[PRODUCE_IDX] = a_produce_lsa;
      m_values[CONSUME_IDX] = a_consume_lsa;
    }
    m_wait_for_target_value_cv.notify_all ();
  }

  void
  minimum_log_lsa_monitor::set_for_consume_and_in_progress (
	  const log_lsa &a_consume_lsa, const log_lsa &a_in_progress_lsa)
  {
    assert (!a_consume_lsa.is_null ());
    assert (!a_in_progress_lsa.is_null ());
    {
      std::lock_guard<std::mutex> lockg { m_values_mtx };
      m_values[CONSUME_IDX] = a_consume_lsa;
      m_values[IN_PROGRESS_IDX] = a_in_progress_lsa;
    }
    m_wait_for_target_value_cv.notify_all ();
  }

  void
  minimum_log_lsa_monitor::set_for_in_progress (const log_lsa &a_lsa)
  {
    do_set_at (IN_PROGRESS_IDX, a_lsa);
  }

  void minimum_log_lsa_monitor::set_for_outer (const log_lsa &a_lsa)
  {
    do_set_at (OUTER_IDX, a_lsa);
  }

  template <typename T_LOCKER>
  log_lsa minimum_log_lsa_monitor::do_locked_get (const T_LOCKER &) const
  {
    const log_lsa new_minimum_log_lsa = std::min (
    {
      m_values[PRODUCE_IDX],
      m_values[CONSUME_IDX],
      m_values[IN_PROGRESS_IDX],
      m_values[OUTER_IDX],
    });
    return new_minimum_log_lsa;
  }

  log_lsa
  minimum_log_lsa_monitor::get () const
  {
    std::lock_guard<std::mutex> lockg { m_values_mtx };
    return do_locked_get (lockg);
  }

  void
  minimum_log_lsa_monitor::do_set_at (ARRAY_INDEX a_idx, const log_lsa &a_new_lsa)
  {
    assert (!a_new_lsa.is_null ());

    bool do_notify_change = false; // avoid gratuitous wake-ups
    {
      std::lock_guard<std::mutex> lockg { m_values_mtx };
      do_notify_change = m_values[a_idx] != a_new_lsa;

      m_values[a_idx] = a_new_lsa;
    }

    if (do_notify_change)
      {
	m_wait_for_target_value_cv.notify_all ();
      }
  }

  log_lsa
  minimum_log_lsa_monitor::wait_past_target_lsa (const log_lsa &a_target_lsa)
  {
    assert (!a_target_lsa.is_null ());

    std::unique_lock<std::mutex> ulock { m_values_mtx };
    log_lsa outer_res;
    m_wait_for_target_value_cv.wait (ulock, [this, &ulock, &a_target_lsa, &outer_res] ()
    {
      const log_lsa current_minimum_lsa = do_locked_get (ulock);
      if (current_minimum_lsa == MAX_LSA)
	{
	  // TODO: corner case that can appear if no entries have ever been processed
	  // the value is actually invalid but the condition would pass 'stricto sensu'
	  // what to do in this case?
	  // assert (false);
	}
      if (current_minimum_lsa > a_target_lsa)
	{
	  outer_res = current_minimum_lsa;
	  return true;
	}
      return false;
    });
    return outer_res;
  }

  /*********************************************************************
   * redo_parallel::redo_job_queue - definition
   *********************************************************************/

  redo_parallel::redo_job_queue::redo_job_queue (minimum_log_lsa_monitor *a_minimum_log_lsa)
    : m_produce_queue (new redo_job_deque_t ())
    , m_consume_queue (new redo_job_deque_t ())
    , m_queues_empty (true)
    , m_adding_finished { false }
    , m_minimum_log_lsa { a_minimum_log_lsa }
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
  redo_parallel::redo_job_queue::push_job (redo_parallel::redo_job_base *a_job)
  {
    std::lock_guard<std::mutex> lockg (m_produce_queue_mutex);
    if (m_minimum_log_lsa != nullptr && m_produce_queue->empty ())
      {
	m_minimum_log_lsa->set_for_produce (a_job->get_log_lsa ());
      }
    m_produce_queue->push_back (a_job);
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

  redo_parallel::redo_job_base *
  redo_parallel::redo_job_queue::pop_job (bool &out_adding_finished)
  {
    std::lock_guard<std::mutex> consume_lockg (m_consume_queue_mutex);

    out_adding_finished = false;

    do_swap_queues_if_needed (consume_lockg);

    if (m_consume_queue->size () > 0)
      {
	// IDEA: instead of every task sifting through entries locked in execution by other tasks,
	// promote those entries as they are found to separate queues on a per-VPID basis; thus,
	// when that specific task enters the critical section, it will first investigate these
	// promoted queues to see if there's smth to consume from there; this has the added benefit that
	// it will also, possibly, bundle together consecutive entries that pertain to that task, thus,
	// further, possibly reducing contention on the happy path (ie: many consecutive same-VPID entries)
	// and also cache localization/affinity given that the same task (presumabily scheduled on the same
	// core) might end up consuming consecutive same-VPID entries which are applied to same location in
	// memory

	// IDEA: if this is a  non-synched op, search all other entries from the current queue
	// pertaining to the same vpid and consolidate them all in a single 'execution'; stop
	// when all entries in the consume queue have been added or when a synched (to-be-waited-for)
	// entry is found

	std::lock_guard<std::mutex> in_progress_lockg (m_in_progress_mutex);
	redo_parallel::redo_job_base *job_to_consume =
		do_locked_find_job_to_consume_and_mark_in_progress (consume_lockg, in_progress_lockg);

	// specifically leave the 'out_adding_finished' on false as set at the beginning:
	//  - even if adding has been finished, the produce queue might still have jobs to be consumed
	//  - if so, setting the out param to true here, will cause consuming tasks to finish
	//  - thus allowing for a corner case where the jobs are not fully processed and all
	//    tasks have finished executing

	// job is null at this point, task will have to spin-wait and come again
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
  redo_parallel::redo_job_queue::do_swap_queues_if_needed (const std::lock_guard<std::mutex> &a_consume_lockg)
  {
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
	  std::lock_guard<std::mutex> produce_lockg (m_produce_queue_mutex);
	  std::swap (m_produce_queue, m_consume_queue);
	  // if both queues empty, notify the, possibly waiting, producing thread
	  m_queues_empty = m_produce_queue->empty () && m_consume_queue->empty ();
	  notify_queues_empty = m_queues_empty;

	  if (m_minimum_log_lsa != nullptr)
	    {
	      // lsa's are ever incresing
	      const log_lsa produce_minimum_log_lsa =
		      m_produce_queue->empty () ? MAX_LSA : (*m_produce_queue->begin ())->get_log_lsa ();
	      const log_lsa consume_minimum_log_lsa =
		      m_consume_queue->empty () ? MAX_LSA : (* m_consume_queue->begin ())->get_log_lsa ();
	      m_minimum_log_lsa->set_for_produce_and_consume (produce_minimum_log_lsa, consume_minimum_log_lsa);
	    }
	}
	if (notify_queues_empty)
	  {
	    m_queues_empty_cv.notify_one ();
	  }
      }
  }

  redo_parallel::redo_job_base *
  redo_parallel::redo_job_queue::do_locked_find_job_to_consume_and_mark_in_progress (
	  const std::lock_guard<std::mutex> &a_consume_lockg, const std::lock_guard<std::mutex> &a_in_progress_lockg)
  {
    redo_job_deque_t::iterator consume_queue_it = m_consume_queue->begin ();
    for (; consume_queue_it != m_consume_queue->end (); ++consume_queue_it)
      {
	const vpid it_vpid = (*consume_queue_it)->get_vpid ();
	if (m_in_progress_vpids.find ((it_vpid)) == m_in_progress_vpids.cend ())
	  {
	    redo_parallel::redo_job_base *const job = *consume_queue_it;
	    m_consume_queue->erase (consume_queue_it);

	    do_locked_mark_job_in_progress (a_in_progress_lockg, job);

	    if (m_minimum_log_lsa != nullptr)
	      {
		const log_lsa consume_minimum_log_lsa =
			m_consume_queue->empty () ? MAX_LSA : (* m_consume_queue->begin ())->get_log_lsa ();
		// mark transition in one go for consistency
		// if:
		//  - first the consume is being changed (or even cleared)
		//  - then, separately, the in-progress is updated
		// the following might happen:
		//  - suppose that there is only one job left in the consume queue, everything else is empty
		//  - the minimum value for the consume queue would be cleared
		//  - at this point, the minimum log lsa will actually be MAX_LSA
		//  - while there is actually one more job (that has just been taken out of the consume queue
		//    and is to be transferred to the in progress set)
		m_minimum_log_lsa->set_for_consume_and_in_progress (
			consume_minimum_log_lsa, *m_in_progress_lsas.cbegin ());
	      }

	    return job;
	  }
      }

    // consumer task will have to spin-wait
    return nullptr;
  }

  void
  redo_parallel::redo_job_queue::do_locked_mark_job_in_progress (
	  const std::lock_guard<std::mutex> &a_in_progress_lockg,
	  const redo_parallel::redo_job_base *a_job)
  {
    assert (m_in_progress_vpids.size () == m_in_progress_lsas.size ());

    const vpid &job_vpid = a_job->get_vpid ();
    assert (m_in_progress_vpids.find (job_vpid) == m_in_progress_vpids.cend ());
    m_in_progress_vpids.insert (job_vpid);

    const log_lsa &job_log_lsa = a_job->get_log_lsa ();
    assert (m_in_progress_lsas.find (job_log_lsa) == m_in_progress_lsas.cend ());
    m_in_progress_lsas.insert (job_log_lsa);
  }

  void
  redo_parallel::redo_job_queue::notify_job_finished (const redo_job_base *a_job)
  {
    bool set_empty = false;
    {
      std::lock_guard<std::mutex> in_progress_lockg (m_in_progress_mutex);

      const auto &job_vpid = a_job->get_vpid ();
      const auto vpid_it = m_in_progress_vpids.find (job_vpid);
      assert (vpid_it != m_in_progress_vpids.cend ());
      m_in_progress_vpids.erase (vpid_it);
      set_empty = m_in_progress_vpids.empty ();

      const log_lsa &job_log_lsa = a_job->get_log_lsa ();
      const auto log_lsa_it = m_in_progress_lsas.find (job_log_lsa);
      assert (log_lsa_it != m_in_progress_lsas.cend ());
      m_in_progress_lsas.erase (log_lsa_it);

      if (m_minimum_log_lsa != nullptr)
	{
	  const log_lsa in_progress_minimum_log_lsa = m_in_progress_lsas.empty ()
	      ? MAX_LSA
	      : *m_in_progress_lsas.cbegin ();
	  m_minimum_log_lsa->set_for_in_progress (in_progress_minimum_log_lsa);
	}

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
      // only locking the produce mutex because the value is only 'produced' under that lock
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

      log_reader m_log_pgptr_reader { LOG_CS_SAFE_READER };
      LOG_ZIP m_undo_unzip_support;
      LOG_ZIP m_redo_unzip_support;
  };

  /*********************************************************************
   * redo_parallel::redo_task - definition
   *********************************************************************/

  constexpr unsigned short redo_parallel::redo_task::WAIT_AND_CHECK_MILLIS;

  redo_parallel::redo_task::redo_task (redo_parallel::task_active_state_bookkeeping &task_state_bookkeeping
				       , redo_parallel::redo_job_queue &a_queue)
    : m_task_state_bookkeeping (task_state_bookkeeping)
    , m_queue (a_queue)
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
	redo_parallel::redo_job_base *const job = m_queue.pop_job (adding_finished);

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

		job->retire ();
	      }
	  }
      }

    m_task_state_bookkeeping.set_inactive ();
  }

  /*********************************************************************
   * redo_parallel - definition
   *********************************************************************/

  redo_parallel::redo_parallel (unsigned a_worker_count, minimum_log_lsa_monitor *a_minimum_log_lsa)
    : m_task_count { a_worker_count }
    , m_worker_pool (nullptr)
    , m_job_queue { a_minimum_log_lsa }
    , m_waited_for_termination (false)
  {
    assert (a_worker_count > 0);

    const thread_type tt = TT_RECOVERY;
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
  redo_parallel::add (redo_job_base *a_job)
  {
    assert (false == m_waited_for_termination);
    assert (false == m_job_queue.get_adding_finished ());

    m_job_queue.push_job (a_job);
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
   * redo_job_impl - definition
   *********************************************************************/

  redo_job_impl::redo_job_impl (const log_lsa *a_end_redo_lsa, bool force_each_page_fetch,
				reusable_jobs_stack *a_reusable_job_stack)
    : redo_parallel::redo_job_base (VPID_INITIALIZER, NULL_LSA)
    , m_end_redo_lsa { a_end_redo_lsa }
    , m_log_reader_page_fetch_mode (force_each_page_fetch
				    ? log_reader::fetch_mode::FORCE
				    : log_reader::fetch_mode::NORMAL)
    , m_reusable_job_stack { a_reusable_job_stack }
    , m_log_rtype { LOG_SMALLER_LOGREC_TYPE }
  {
    assert (a_reusable_job_stack != nullptr);
  }

  void redo_job_impl::reinitialize (VPID a_vpid, const log_lsa &a_rcv_lsa, LOG_RECTYPE a_log_rtype)
  {
    this->redo_job_base::reinitialize (a_vpid, a_rcv_lsa);
    assert (a_log_rtype != LOG_SMALLER_LOGREC_TYPE && a_log_rtype != LOG_LARGER_LOGREC_TYPE);
    m_log_rtype = a_log_rtype;
  }

  int redo_job_impl::execute (THREAD_ENTRY *thread_p, log_reader &log_pgptr_reader,
			      LOG_ZIP &undo_unzip_support, LOG_ZIP &redo_unzip_support)
  {
    /* perf data for processing log redo asynchronously, enabled:
     *  - during log crash recovery
     *  - on the page server, when replication is executing in the asynchronous mode
     * in both cases, it does include the part that effectively calls the redo function, so, for accurate
     * evaluation the part that effectively executes the redo function must be accounted for
     */
    perfmon_counter_timer_raii_tracker perfmon { PSTAT_LOG_REDO_ASYNC };

    const auto &rcv_lsa = get_log_lsa ();
    const int err_fetch = log_pgptr_reader.set_lsa_and_fetch_page (rcv_lsa, m_log_reader_page_fetch_mode);
    if (err_fetch != NO_ERROR)
      {
	return err_fetch;
      }
    log_pgptr_reader.add_align (sizeof (LOG_RECORD_HEADER));
    const auto &rcv_vpid = get_vpid ();
    switch (m_log_rtype)
      {
      case LOG_REDO_DATA:
	read_record_and_redo<log_rec_redo> (thread_p, log_pgptr_reader, rcv_vpid, rcv_lsa,
					    undo_unzip_support, redo_unzip_support);
	break;
      case LOG_MVCC_REDO_DATA:
	read_record_and_redo<log_rec_mvcc_redo> (thread_p, log_pgptr_reader, rcv_vpid, rcv_lsa,
	    undo_unzip_support, redo_unzip_support);
	break;
      case LOG_UNDOREDO_DATA:
      case LOG_DIFF_UNDOREDO_DATA:
	read_record_and_redo<log_rec_undoredo> (thread_p, log_pgptr_reader, rcv_vpid, rcv_lsa,
						undo_unzip_support, redo_unzip_support);
	break;
      case LOG_MVCC_UNDOREDO_DATA:
      case LOG_MVCC_DIFF_UNDOREDO_DATA:
	read_record_and_redo<log_rec_mvcc_undoredo> (thread_p, log_pgptr_reader, rcv_vpid, rcv_lsa,
	    undo_unzip_support, redo_unzip_support);
	break;
      case LOG_RUN_POSTPONE:
	read_record_and_redo<log_rec_run_postpone> (thread_p, log_pgptr_reader, rcv_vpid, rcv_lsa,
	    undo_unzip_support, redo_unzip_support);
	break;
      case LOG_COMPENSATE:
	read_record_and_redo<log_rec_compensate> (thread_p, log_pgptr_reader, rcv_vpid, rcv_lsa,
	    undo_unzip_support, redo_unzip_support);
	break;
      default:
	assert (false);
	break;
      }

    return NO_ERROR;
  }

  void redo_job_impl::retire ()
  {
    // return the job back to the pool of available reusable jobs
    m_reusable_job_stack->push (this);
  }

  template <typename T>
  inline void
  redo_job_impl::read_record_and_redo (THREAD_ENTRY *thread_p, log_reader &log_pgptr_reader, const VPID &rcv_vpid,
				       const log_lsa &rcv_lsa, LOG_ZIP &undo_unzip_support, LOG_ZIP &redo_unzip_support)
  {
    log_pgptr_reader.advance_when_does_not_fit (sizeof (T));
    const T log_rec = log_pgptr_reader.reinterpret_copy_and_add_align<T> ();
    log_rv_redo_record_sync<T> (thread_p, log_pgptr_reader, log_rec, rcv_vpid, rcv_lsa,
				m_end_redo_lsa, m_log_rtype,
				undo_unzip_support, redo_unzip_support);
  }

  /*********************************************************************
   * reusable_jobs_stack - definition
   *********************************************************************/

  reusable_jobs_stack::reusable_jobs_stack ()
    : m_stack_size { 0 }
  {
  }

  void reusable_jobs_stack::initialize (std::size_t a_stack_size, const log_lsa *a_end_redo_lsa,
					bool force_each_page_fetch)
  {
    assert (m_pop_stack.empty ());
    assert (m_push_stack.empty ());

    m_stack_size = a_stack_size;
    for (std::size_t idx = 0; idx < m_stack_size; ++idx)
      {
	redo_job_impl *job = new redo_job_impl (a_end_redo_lsa, force_each_page_fetch, this);
	m_pop_stack.push (job);
      }
  }

  reusable_jobs_stack::~reusable_jobs_stack ()
  {
    assert ((m_pop_stack.size () + m_push_stack.size ()) == m_stack_size);

    while (!m_push_stack.empty ())
      {
	redo_job_impl *const push_job = m_push_stack.top ();
	m_push_stack.pop ();
	delete push_job;
      }
    while (!m_pop_stack.empty ())
      {
	redo_job_impl *const pop_job = m_pop_stack.top ();
	m_pop_stack.pop ();
	delete pop_job;
      }
  }

  redo_job_impl *reusable_jobs_stack::blocking_pop ()
  {
    if (!m_pop_stack.empty ())
      {
	redo_job_impl *const pop_job = m_pop_stack.top ();
	m_pop_stack.pop ();
	return pop_job;
      }
    else
      {
	std::unique_lock<std::mutex> ulock { m_mutex };
	m_jobs_available_on_push_stack_cv.wait (ulock, [this] ()
	{
	  return !m_push_stack.empty ();
	});

	m_pop_stack.swap (m_push_stack);

	redo_job_impl *const pop_job = m_pop_stack.top ();
	m_pop_stack.pop ();
	return pop_job;
      }

    assert ("unreachable state reached" == nullptr);
    return nullptr;
  }

  void reusable_jobs_stack::push (redo_job_impl *a_job)
  {
    {
      std::lock_guard<std::mutex> stack_lockg { m_mutex };
      m_push_stack.push (a_job);
    }
    m_jobs_available_on_push_stack_cv.notify_one ();
  }
}
