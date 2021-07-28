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
  // TODO: reserve size configurable based on expected load of recovery
  static constexpr size_t QUEUE_INTERNAL_JOB_VECTOR_RESERVE_SIZE = 1000000;

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

  redo_parallel::redo_job_queue::redo_job_queue (const unsigned a_task_count, minimum_log_lsa_monitor *a_minimum_log_lsa)
    : m_task_count { a_task_count }
#if defined (USE_LOCKFREE_QUEUE)
    , m_pre_produce_circ_queue { 50 * ONE_M }
    , m_pre_produce_finished { false }
#endif
    , m_produce_vec { a_task_count }
    , m_produce_mutex_vec { a_task_count }
//    : m_produce (new vpid_ux_redo_job_deque_map_t ())
//    , m_consume (new vpid_ux_redo_job_deque_map_t ())
//    , m_queues_empty (true)
    , m_adding_finished { false }
    , m_monitor_minimum_log_lsa { a_minimum_log_lsa != nullptr }
    , m_minimum_log_lsa { a_minimum_log_lsa }
  {
    for (auto &jobs_vec : m_produce_vec)
      {
	jobs_vec = new redo_job_vector_t ();
	jobs_vec->reserve (QUEUE_INTERNAL_JOB_VECTOR_RESERVE_SIZE);
	assert (jobs_vec->capacity () == QUEUE_INTERNAL_JOB_VECTOR_RESERVE_SIZE);
      }

    if (m_monitor_minimum_log_lsa)
      {
#if defined (USE_LOCKFREE_QUEUE)
	// the extra one (last) is for the pre-produce queue
	m_empty_vec.resize (a_task_count + 1, true);
#else
	m_empty_vec.resize (a_task_count, true);
#endif
      }

#if defined (USE_LOCKFREE_QUEUE)
    auto job_push_func =
	    std::bind (&redo_parallel::redo_job_queue::do_push_pre_produce, std::ref (*this));
    m_pre_produce_thr = std::thread (job_push_func);
#endif
  }

  redo_parallel::redo_job_queue::~redo_job_queue ()
  {
    assert_idle ();

    for (auto &jobs : m_produce_vec)
      {
	assert (jobs != nullptr);
	assert (jobs->empty ());
	delete jobs;
      }
//    delete m_produce;
//    m_produce = nullptr;

//    delete m_consume;
//    m_consume = nullptr;

#if defined (USE_LOCKFREE_QUEUE)
    m_pre_produce_thr.join ();
#endif
  }

#if defined (USE_LOCKFREE_QUEUE)
  void
  redo_parallel::redo_job_queue::do_push_pre_produce ()
  {
    redo_job_base *job { nullptr };
    while (true)
      {
	if (m_pre_produce_circ_queue.consume (job))
	  {
	    const vpid &job_vpid = job->get_vpid ();

	    const int32_t vpid_hash = m_vpid_hash (job_vpid);
	    const std::size_t vec_idx = vpid_hash % m_task_count;

	    std::mutex &mtx = m_produce_mutex_vec[vec_idx];
	    std::lock_guard<std::mutex> lockg (mtx);

	    redo_job_vector_t *jobs = m_produce_vec[vec_idx];
	    if (jobs->empty ())
	      {
		// will be empty no more
		set_non_empty_at (vec_idx);
	      }

	    jobs->push_back (job);
	  }
	else
	  {
	    if (m_pre_produce_finished.load ())
	      {
		assert (m_pre_produce_circ_queue.is_empty ());
		m_adding_finished.store (true);
		set_empty_at (m_empty_vec.size () - 1);
		break;
	      }
	    else
	      {
		std::this_thread::yield ();
	      }
	  }
      }
  }
#endif

  void
  redo_parallel::redo_job_queue::push_job (redo_job_base *a_job)
  {
#if defined (USE_LOCKFREE_QUEUE)
    if (m_pre_produce_circ_queue.is_empty ())
      {
	set_non_empty_at (m_empty_vec.size () - 1);
      }
    const bool produced = m_pre_produce_circ_queue.produce (a_job);
    assert (produced);
#else
    const vpid &job_vpid = a_job->get_vpid ();
    const std::size_t vpid_hash = m_vpid_hash (job_vpid);
    const std::size_t vec_idx = vpid_hash % m_task_count;

    std::mutex &mtx = m_produce_mutex_vec[vec_idx];
    std::lock_guard<std::mutex> lockg (mtx);

    redo_job_vector_t *jobs = m_produce_vec[vec_idx];
    if (jobs->empty ())
      {
	// will be empty no more
	set_non_empty_at (vec_idx);
      }

    jobs->push_back (a_job);
#endif
  }

  void
  redo_parallel::redo_job_queue::set_adding_finished ()
  {
#if defined (USE_LOCKFREE_QUEUE)
    m_pre_produce_finished.store (true);
#else
    m_adding_finished.store (true);
#endif
  }

  bool
  redo_parallel::redo_job_queue::get_adding_finished () const
  {
#if defined (USE_LOCKFREE_QUEUE)
    return m_pre_produce_finished.load ();
#else
    return m_adding_finished.load ();
#endif
  }

  bool
  redo_parallel::redo_job_queue::pop_jobs (unsigned a_task_idx,
      redo_parallel::redo_job_queue::redo_job_vector_t *&in_out_jobs,
      bool &out_adding_finished)
  {
    out_adding_finished = false;

    std::mutex &mtx = m_produce_mutex_vec[a_task_idx];
    std::lock_guard<std::mutex> lockg (mtx);
    redo_job_vector_t *jobs_to_consume = m_produce_vec[a_task_idx];
    if (jobs_to_consume->empty ())
      {
	out_adding_finished = m_adding_finished.load ();
	// avoid replacing empty container with new empty container
	return false;
      }
    else
      {
	m_produce_vec[a_task_idx] = in_out_jobs;
	in_out_jobs = jobs_to_consume;
	return true;
      }

//    std::lock_guard<std::mutex> consume_lockg (m_consume_mutex);

//    out_adding_finished = false;

//    do_swap_queues_if_needed (consume_lockg);

//    if (m_consume->size () > 0)
//      {
//	// IDEA: instead of every task sifting through entries locked in execution by other tasks,
//	// promote those entries as they are found to separate queues on a per-VPID basis; thus,
//	// when that specific task enters the critical section, it will first investigate these
//	// promoted queues to see if there's smth to consume from there; this has the added benefit that
//	// it will also, possibly, bundle together consecutive entries that pertain to that task, thus,
//	// further, possibly reducing contention on the happy path (ie: many consecutive same-VPID entries)
//	// and also cache localization/affinity given that the same task (presumabily scheduled on the same
//	// core) might end up consuming consecutive same-VPID entries which are applied to same location in
//	// memory

//	// IDEA: if this is a  non-synched op, search all other entries from the current queue
//	// pertaining to the same vpid and consolidate them all in a single 'execution'; stop
//	// when all entries in the consume queue have been added or when a synched (to-be-waited-for)
//	// entry is found

//	std::lock_guard<std::mutex> in_progress_lockg (m_in_progress_mutex);
//	ux_redo_job_deque jobs_to_consume =
//		do_locked_find_job_to_consume_and_mark_in_progress (consume_lockg, in_progress_lockg);

//	// specifically leave the 'out_adding_finished' on false as set at the beginning:
//	//  - even if adding has been finished, the produce queue might still have jobs to be consumed
//	//  - if so, setting the out param to true here, will cause consuming tasks to finish
//	//  - thus allowing for a corner case where the jobs are not fully processed and all
//	//    tasks have finished executing

//	// job is null at this point, task will have to spin-wait and come again
//	return jobs_to_consume;
//      }
//    else
//      {
//	// because two alternating queues are used internally, and because, when the consumption queue
//	// is being almost exhausted (i.e.: there are still entries to be consumed but all of them are locked
//	// by other tasks - via the m_in_progress_vpids), there are a few times when false negatives are returned
//	// to the consumption tasks (see the 'return nullptr' in the 'then' branch); but, if control reaches here
//	// it is ensured that indeed no more data exists and that no more data will be produced
//	out_adding_finished = m_adding_finished.load ();

//	// if no more data will be produced (signalled by the flag), the
//	// consumer will just need to terminate; otherwise, consumer is expected to
//	// spin-wait and try again
//	return ux_redo_job_deque ();
//      }
  }

//  void
//  redo_parallel::redo_job_queue::do_swap_queues_if_needed (const std::lock_guard<std::mutex> &a_consume_lockg)
//  {
//    // if consumption of everything in consume queue finished; see whether there's some more in the other one
//    if (m_consume->size () == 0)
//      {
//	// TODO: a second barrier such that, consumption threads do not 'busy wait' by
//	// constantly swapping two empty containers; works in combination with a notification
//	// dispatched after new work items are added to the produce queue; this, in effect means that
//	// this function will semantically become 'blocking_pop'

//	// consumer queue is locked anyway, interested in not locking the producer queue
//	bool notify_queues_empty = false;
//	{
//	  // effectively, an 'atomic' swap because both queues are locked
//	  std::lock_guard<std::mutex> produce_lockg (m_produce_mutex);
//	  std::swap (m_produce, m_consume);
//	  m_produce_min_lsa_map.swap (m_consume_min_lsa_map);

//	  assert (m_produce->size () == m_produce_min_lsa_map.size () && m_produce->empty ());
//	  assert (m_consume->size () == m_consume_min_lsa_map.size ());

//	  if (m_monitor_minimum_log_lsa)
//	    {
//	      const log_lsa consume_minimum_log_lsa =
//		      m_consume->empty () ? MAX_LSA : m_consume_min_lsa_map.cbegin ()->first;
//	      // consume side, being empty, update with guard value
//	      m_minimum_log_lsa->set_for_produce_and_consume (
//		      MAX_LSA, consume_minimum_log_lsa);
//	    }

//	  // if both queues empty, notify the, possibly waiting, main thread
//	  m_queues_empty = m_produce->empty () && m_consume->empty ();
//	  notify_queues_empty = m_queues_empty;
//	}
//	if (notify_queues_empty)
//	  {
//	    m_queues_empty_cv.notify_one ();
//	  }
//      }
//  }

//  redo_parallel::redo_job_queue::ux_redo_job_deque
//  redo_parallel::redo_job_queue::do_locked_find_job_to_consume_and_mark_in_progress (
//	  const std::lock_guard<std::mutex> &a_consume_lockg,
//	  const std::lock_guard<std::mutex> &a_in_progress_lockg)
//  {
//    // choose the vpid with the minimum lsa, to avoid going back in time with the bookkeeping
//    // if jobs for that vpid happen to be in progress by another task, only way is to bail out, effectively
//    // spin-waiting until that other task would have finished processing entries for the vpid currently
//    // with lowest log_lsa;
//    // this might happen in situations where there is very high contention on one single vpid
//    const log_lsa_vpid_map_t::iterator consume_min_log_lsa_it = m_consume_min_lsa_map.begin ();
//    const vpid &consume_min_log_lsa_vpid = consume_min_log_lsa_it->second;
//    if (m_in_progress_vpids.find (consume_min_log_lsa_vpid) == m_in_progress_vpids.cend ())
//      {
//	vpid_ux_redo_job_deque_map_t::iterator consume_it = m_consume->find (consume_min_log_lsa_vpid);
//	assert (consume_it != m_consume->end ());

//	ux_redo_job_deque ret_job_deq {std::move (consume_it->second)};
//	m_consume->erase (consume_it);

//	assert (consume_min_log_lsa_it->first == (*ret_job_deq.cbegin ())->get_log_lsa ());
//	m_consume_min_lsa_map.erase (consume_min_log_lsa_it);

//	assert (m_consume->size () == m_consume_min_lsa_map.size ());

//	do_locked_mark_job_deque_in_progress (a_in_progress_lockg, ret_job_deq);

//	if (m_monitor_minimum_log_lsa)
//	  {
//	    // mark transition in one go for consistency
//	    // if:
//	    //  - first the consume is being changed (or even cleared)
//	    //  - then, separately, the in-progress is updated
//	    // the following might happen:
//	    //  - suppose that there is only one job left in the consume queue, everything else is empty
//	    //  - the minimum value for the consume queue would be cleared
//	    //  - at this point, the minimum log lsa will actually be MAX_LSA
//	    //  - while there is actually one more job (that has just been taken out of the consume queue
//	    //    and is to be transferred to the in progress set)
//	    const log_lsa &consume_minimum_log_lsa =
//		    m_consume->empty () ? MAX_LSA : m_consume_min_lsa_map.cbegin ()->first;
//	    const log_lsa in_progress_minimum_log_lsa = *m_in_progress_lsas.cbegin ();
//	    m_minimum_log_lsa->set_for_consume_and_in_progress (
//		    consume_minimum_log_lsa, in_progress_minimum_log_lsa);
//	  }

//	return ret_job_deq;
//      }

//    // consumer task will have to spin-wait
//    return ux_redo_job_deque ();
//  }

//  void
//  redo_parallel::redo_job_queue::do_locked_mark_job_deque_in_progress (
//	  const std::lock_guard<std::mutex> &a_in_progress_lockg,
//	  const ux_redo_job_deque &a_job_deque)
//  {
//    // all jobs have the same vpid
//    const ux_redo_job_base &first_job = *a_job_deque.cbegin ();
//    const vpid &first_job_vpid = first_job->get_vpid ();
//    assert (m_in_progress_vpids.find (first_job_vpid) == m_in_progress_vpids.cend ());
//    m_in_progress_vpids.insert (first_job_vpid);

//    if (m_monitor_minimum_log_lsa)
//      {
//	// each job has a different log_lsa but, within a queue, jobs have ever increasing log_lsa's
//	// so, suffices to only add the first one
//	const log_lsa &first_job_log_lsa = first_job->get_log_lsa ();
//	assert (m_in_progress_lsas.find (first_job_log_lsa) == m_in_progress_lsas.cend ());
//	m_in_progress_lsas.insert (first_job_log_lsa);
//      }
//  }

  void
  redo_parallel::redo_job_queue::notify_job_deque_finished (const ux_redo_job_deque &a_job_deque)
  {
//    bool vpid_set_empty = false;
//    bool lsa_set_empty = false;
//    {
//      std::lock_guard<std::mutex> in_progress_lockg (m_in_progress_mutex);

//      const ux_redo_job_base &first_job = *a_job_deque.cbegin ();
//      // all jobs have the same vpid
//      {
//	const auto &first_job_vpid = first_job->get_vpid ();
//	const auto vpid_it = m_in_progress_vpids.find (first_job_vpid);
//	assert (vpid_it != m_in_progress_vpids.cend ());
//	m_in_progress_vpids.erase (vpid_it);
//      }
//      vpid_set_empty = m_in_progress_vpids.empty ();

//      if (m_monitor_minimum_log_lsa)
//	{
//	  // each job has a different log_lsa but, within a queue, jobs have ever increasing log_lsa's
//	  // so, suffices to only add the first one
//	  const log_lsa &first_job_log_lsa = first_job->get_log_lsa ();
//	  const auto log_lsa_it = m_in_progress_lsas.find (first_job_log_lsa);
//	  assert (log_lsa_it != m_in_progress_lsas.cend ());
//	  m_in_progress_lsas.erase (log_lsa_it);

//	  const log_lsa &in_progress_minimum_log_lsa = m_in_progress_lsas.empty ()
//	      ? MAX_LSA : *m_in_progress_lsas.cbegin ();
//	  m_minimum_log_lsa->set_for_in_progress (in_progress_minimum_log_lsa);

//	  assert (m_in_progress_vpids.size () == m_in_progress_lsas.size ());
//	  assert ((vpid_set_empty && lsa_set_empty) || (!vpid_set_empty && !lsa_set_empty));
//	}
//      lsa_set_empty = m_in_progress_lsas.empty ();

//    }

//    if (vpid_set_empty && lsa_set_empty)
//      {
//	m_in_progress_vpids_empty_cv.notify_one ();
//      }
  }

  void
  redo_parallel::redo_job_queue::wait_for_idle () const
  {
    assert (m_monitor_minimum_log_lsa);

    {
      std::unique_lock<std::mutex> empty_ulock (m_empty_mutex);
      m_empty_cv.wait (empty_ulock, [this] ()
      {
	for (const bool &curr_empty : m_empty_vec)
	  {
	    if (!curr_empty)
	      {
		return false;
	      }
	  }
	return true;
      });
    }

//    {
//      std::unique_lock<std::mutex> empty_in_progress_lock (m_in_progress_mutex);
//      m_in_progress_vpids_empty_cv.wait (empty_in_progress_lock, [this] ()
//      {
//	return m_in_progress_vpids.empty ();
//      });
//      assert (m_in_progress_lsas.empty ());
//    }

//    assert_idle ();
  }

  bool
  redo_parallel::redo_job_queue::is_idle () const
  {
    assert (m_monitor_minimum_log_lsa);

    std::lock_guard<std::mutex> empty_lockg (m_empty_mutex);
    for (const bool &curr_empty : m_empty_vec)
      {
	if (!curr_empty)
	  {
	    return false;
	  }
      }
    return true;
  }

  void
  redo_parallel::redo_job_queue::assert_idle () const
  {
    assert (m_task_count == m_produce_vec.size ());
#if defined (USE_LOCKFREE_QUEUE)
    assert (m_pre_produce_circ_queue.is_empty ());
#endif
    for (const auto &jobs : m_produce_vec)
      {
	assert (jobs != nullptr);
	assert (jobs->empty ());
      }
//    assert (m_produce->empty ());
//    assert (m_produce_min_lsa_map.empty ());

//    assert (m_consume->empty ());
//    assert (m_consume_min_lsa_map.empty ());

//    assert (m_in_progress_vpids.empty ());
//    assert (m_in_progress_lsas.empty ());
  }

  void
  redo_parallel::redo_job_queue::set_empty_at (unsigned a_index)
  {
    if (m_monitor_minimum_log_lsa)
      {
	std::lock_guard<std::mutex> empty_lockg (m_empty_mutex);
	m_empty_vec[a_index] = true;
	m_empty_cv.notify_all ();
      }
  }

  void
  redo_parallel::redo_job_queue::set_non_empty_at (unsigned a_index)
  {
    if (m_monitor_minimum_log_lsa)
      {
	std::lock_guard<std::mutex> empty_lockg (m_empty_mutex);
	m_empty_vec[a_index] = false;
      }
  }

//  void
//  redo_parallel::redo_job_queue::do_check_empty_and_notify(std::lock_guard<std::mutex> &a_empty_lockg)
//  {
//    bool all_empty = true;
//    for (const bool &curr_empty : m_empty_vec)
//      {
//        if (!curr_empty)
//          {
//            all_empty = false;
//            break;
//          }
//      }
//    if (all_empty)
//      {
//        m_empty_cv.notify_all();
//      }
//  }

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
      redo_task (unsigned a_task_idx, redo_parallel::task_active_state_bookkeeping &task_state_bookkeeping
		 , redo_parallel::redo_job_queue &a_queue, reusable_jobs_stack *a_reusable_jobs);
      redo_task (const redo_task &) = delete;
      redo_task (redo_task &&) = delete;

      redo_task &operator = (const redo_task &) = delete;
      redo_task &operator = (redo_task &&) = delete;

      ~redo_task () override;

      void execute (context_type &context);

    private:
      const unsigned m_task_idx;
      redo_parallel::task_active_state_bookkeeping &m_task_state_bookkeeping;
      redo_parallel::redo_job_queue &m_queue;
      reusable_jobs_stack *m_reusable_jobs;

      log_reader m_log_pgptr_reader;
      LOG_ZIP m_undo_unzip_support;
      LOG_ZIP m_redo_unzip_support;
  };

  /*********************************************************************
   * redo_parallel::redo_task - definition
   *********************************************************************/

  constexpr unsigned short redo_parallel::redo_task::WAIT_AND_CHECK_MILLIS;

  redo_parallel::redo_task::redo_task (unsigned a_task_idx
				       , redo_parallel::task_active_state_bookkeeping &task_state_bookkeeping
				       , redo_parallel::redo_job_queue &a_queue
				       , reusable_jobs_stack *a_reusable_jobs)
    : m_task_idx { a_task_idx }
    , m_task_state_bookkeeping (task_state_bookkeeping)
    , m_queue (a_queue)
    , m_reusable_jobs { a_reusable_jobs }
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

    redo_job_queue::redo_job_vector_t *jobs_vec =
	    new redo_parallel::redo_job_queue::redo_job_vector_t ();
    // according to spec, reserved size survives clearing of the vector
    // which should help to only allocate/reserve once
    jobs_vec->reserve (QUEUE_INTERNAL_JOB_VECTOR_RESERVE_SIZE);
    assert (jobs_vec->capacity () == QUEUE_INTERNAL_JOB_VECTOR_RESERVE_SIZE);

    for (; !finished ;)
      {
	bool adding_finished = false;
	const bool jobs_popped = m_queue.pop_jobs (m_task_idx, jobs_vec, adding_finished);
	if (!jobs_popped && adding_finished)
	  {
	    m_queue.set_empty_at (m_task_idx);
	    finished = true;
	  }
	else
	  {
	    if (!jobs_popped)
	      {
		// TODO: if needed, check if requested to finish ourselves

		// no notification towards the queue for empty here, because at the next attempt
		// we might get more work

		// expecting more data, sleep and check again
		std::this_thread::sleep_for (std::chrono::milliseconds (WAIT_AND_CHECK_MILLIS));
	      }
	    else
	      {
		assert (!jobs_vec->empty ());
		THREAD_ENTRY *const thread_entry = &context;
		for (auto &job : *jobs_vec)
		  {
		    job->execute (thread_entry, m_log_pgptr_reader, m_undo_unzip_support, m_redo_unzip_support);
		  }

		//m_queue.notify_job_deque_finished (jobs);

		m_reusable_jobs->push (*jobs_vec);
		assert (jobs_vec->empty ());
		assert (jobs_vec->capacity () == QUEUE_INTERNAL_JOB_VECTOR_RESERVE_SIZE);
	      }
	  }
      }

    assert (jobs_vec != nullptr);
    assert (jobs_vec->empty ());
    delete jobs_vec;

    m_task_state_bookkeeping.set_inactive ();
  }

  /*********************************************************************
   * redo_parallel - definition
   *********************************************************************/

  redo_parallel::redo_parallel (unsigned a_worker_count, reusable_jobs_stack *a_reusable_jobs,
				minimum_log_lsa_monitor *a_minimum_log_lsa)
    : m_task_count { a_worker_count }
    , m_worker_pool (nullptr)
    , m_job_queue { a_worker_count, a_minimum_log_lsa }
    , m_waited_for_termination (false)
  {
    assert (a_worker_count > 0);

    const thread_type tt = log_is_in_crash_recovery () ? TT_RECOVERY : TT_REPLICATION;
    m_pool_context_manager = std::make_unique<cubthread::system_worker_entry_manager> (tt);

    do_init_worker_pool ();
    do_init_tasks (a_reusable_jobs);
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

  bool
  redo_parallel::is_idle () const
  {
    assert (false == m_waited_for_termination);
    assert (false == m_job_queue.get_adding_finished ());

    return m_job_queue.is_idle ();
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
  redo_parallel::do_init_tasks (reusable_jobs_stack *a_reusable_jobs)
  {
    assert (m_task_count > 0);
    assert (m_worker_pool != nullptr);

    for (unsigned task_idx = 0; task_idx < m_task_count; ++task_idx)
      {
	// NOTE: task ownership goes to the worker pool
	auto task = new redo_task (task_idx, m_task_state_bookkeeping, m_job_queue, a_reusable_jobs);
	m_worker_pool->execute (task);
      }
  }

  /*********************************************************************
   * reusable_redo_job_impl - definition
   *********************************************************************/

  reusable_redo_job_impl::reusable_redo_job_impl (const log_lsa *a_end_redo_lsa, bool force_each_page_fetch)
    : redo_parallel::redo_job_base (VPID_INITIALIZER, NULL_LSA)
    , m_end_redo_lsa { a_end_redo_lsa }
    , m_log_reader_page_fetch_mode (force_each_page_fetch
				    ? log_reader::fetch_mode::FORCE
				    : log_reader::fetch_mode::NORMAL)
    , m_log_rtype { LOG_SMALLER_LOGREC_TYPE }
  {
  }

  void reusable_redo_job_impl::reset (VPID a_vpid, const log_lsa &a_rcv_lsa, LOG_RECTYPE a_log_rtype)
  {
    m_vpid = a_vpid;
    m_log_lsa = a_rcv_lsa;
    m_log_rtype = a_log_rtype;
  }

  int reusable_redo_job_impl::execute (THREAD_ENTRY *thread_p, log_reader &log_pgptr_reader,
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
    const int err_set_lsa_and_fetch_page
      = log_pgptr_reader.set_lsa_and_fetch_page (rcv_lsa, m_log_reader_page_fetch_mode);
    if (err_set_lsa_and_fetch_page != NO_ERROR)
      {
	return err_set_lsa_and_fetch_page;
      }
    log_pgptr_reader.add_align (sizeof (LOG_RECORD_HEADER));
    const auto &rcv_vpid = get_vpid ();
    switch (m_log_rtype)
      {
      case LOG_REDO_DATA:
      {
	log_pgptr_reader.advance_when_does_not_fit (sizeof (log_rec_redo));
	const log_rec_redo log_rec = log_pgptr_reader.reinterpret_copy_and_add_align<log_rec_redo> ();
	log_rv_redo_record_sync<log_rec_redo> (thread_p, log_pgptr_reader, log_rec, rcv_vpid, rcv_lsa, m_end_redo_lsa,
					       m_log_rtype, undo_unzip_support, redo_unzip_support);
	break;
      }
      case LOG_MVCC_REDO_DATA:
      {
	log_pgptr_reader.advance_when_does_not_fit (sizeof (log_rec_mvcc_redo));
	const log_rec_mvcc_redo log_rec = log_pgptr_reader.reinterpret_copy_and_add_align<log_rec_mvcc_redo> ();
	log_rv_redo_record_sync<log_rec_mvcc_redo> (thread_p, log_pgptr_reader, log_rec, rcv_vpid, rcv_lsa, m_end_redo_lsa,
	    m_log_rtype, undo_unzip_support, redo_unzip_support);
	break;
      }
      case LOG_UNDOREDO_DATA:
      case LOG_DIFF_UNDOREDO_DATA:
      {
	log_pgptr_reader.advance_when_does_not_fit (sizeof (log_rec_undoredo));
	const log_rec_undoredo log_rec = log_pgptr_reader.reinterpret_copy_and_add_align<log_rec_undoredo> ();
	log_rv_redo_record_sync<log_rec_undoredo> (thread_p, log_pgptr_reader, log_rec, rcv_vpid, rcv_lsa, m_end_redo_lsa,
	    m_log_rtype, undo_unzip_support, redo_unzip_support);
	break;
      }
      case LOG_MVCC_UNDOREDO_DATA:
      case LOG_MVCC_DIFF_UNDOREDO_DATA:
      {
	log_pgptr_reader.advance_when_does_not_fit (sizeof (log_rec_mvcc_undoredo));
	const log_rec_mvcc_undoredo log_rec = log_pgptr_reader.reinterpret_copy_and_add_align<log_rec_mvcc_undoredo> ();
	log_rv_redo_record_sync<log_rec_mvcc_undoredo> (thread_p, log_pgptr_reader, log_rec, rcv_vpid, rcv_lsa, m_end_redo_lsa,
	    m_log_rtype, undo_unzip_support, redo_unzip_support);
	break;
      }
      case LOG_RUN_POSTPONE:
      {
	log_pgptr_reader.advance_when_does_not_fit (sizeof (log_rec_run_postpone));
	const log_rec_run_postpone log_rec = log_pgptr_reader.reinterpret_copy_and_add_align<log_rec_run_postpone> ();
	log_rv_redo_record_sync<log_rec_run_postpone> (thread_p, log_pgptr_reader, log_rec, rcv_vpid, rcv_lsa, m_end_redo_lsa,
	    m_log_rtype, undo_unzip_support, redo_unzip_support);
	break;
      }
      case LOG_COMPENSATE:
      {
	log_pgptr_reader.advance_when_does_not_fit (sizeof (log_rec_compensate));
	const log_rec_compensate log_rec = log_pgptr_reader.reinterpret_copy_and_add_align<log_rec_compensate> ();
	log_rv_redo_record_sync<log_rec_compensate> (thread_p, log_pgptr_reader, log_rec, rcv_vpid, rcv_lsa, m_end_redo_lsa,
	    m_log_rtype, undo_unzip_support, redo_unzip_support);
	break;
      }
      default:
	assert (false);
	break;
      }

    return NO_ERROR;
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
    m_stack_size = a_stack_size;
    for (std::size_t idx = 0; idx < m_stack_size; ++idx)
      {
	redo_parallel::redo_job_base *job = new reusable_redo_job_impl (a_end_redo_lsa, force_each_page_fetch);
	m_stack.push (job);
      }
  }

  reusable_jobs_stack::~reusable_jobs_stack ()
  {
    assert (m_stack.size () == m_stack_size);
    for (std::size_t idx = 0; idx < m_stack_size; ++idx)
      {
	redo_parallel::redo_job_base *const job = m_stack.top ();
	m_stack.pop ();
	delete job;
      }
  }

  redo_parallel::redo_job_base *reusable_jobs_stack::pop ()
  {
    std::lock_guard<std::mutex> stack_lockg { m_stack_mutex };
    if (m_stack.empty ())
      {
	return nullptr;
      }
    redo_parallel::redo_job_base *const job = m_stack.top ();
    m_stack.pop ();
    return job;
  }

  void reusable_jobs_stack::push (std::vector<redo_parallel::redo_job_base *> &a_jobs)
  {
    if (!a_jobs.empty ())
      {
	std::lock_guard<std::mutex> stack_lockg { m_stack_mutex };
	for (auto &job: a_jobs)
	  {
	    m_stack.push (job);
	  }
	// clear leaves the capacity unchanged
	a_jobs.clear ();
      }
  }
}
