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

  redo_parallel::redo_job_queue::redo_job_queue (const std::size_t a_task_count,
      minimum_log_lsa_monitor *a_minimum_log_lsa)
    : m_task_count { a_task_count }
    , m_produce_vec { a_task_count }
    , m_produce_mutex_vec { a_task_count }
      /*
      : m_produce (new vpid_ux_redo_job_deque_map_t ())
      , m_consume (new vpid_ux_redo_job_deque_map_t ())
      , m_queues_empty (true)
      */
    , m_adding_finished { false }
    , m_monitor_minimum_log_lsa { a_minimum_log_lsa != nullptr }
    , m_minimum_log_lsa { a_minimum_log_lsa }
  {
    for (auto &jobs_vec : m_produce_vec)
      {
	jobs_vec = new redo_job_vector_t ();
	jobs_vec->reserve (PARALLEL_REDO_JOB_VECTOR_RESERVE_SIZE);
      }

    if (m_monitor_minimum_log_lsa)
      {
	m_empty_vec.resize (a_task_count, true);
      }
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
    /*
    delete m_produce;
    m_produce = nullptr;

    delete m_consume;
    m_consume = nullptr;
    */
  }

  void
  redo_parallel::redo_job_queue::push_job (redo_parallel::redo_job_base *a_job)
  {
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

  bool
  redo_parallel::redo_job_queue::pop_jobs (std::size_t a_task_idx,
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

    /*
    std::lock_guard<std::mutex> consume_lockg (m_consume_mutex);

    out_adding_finished = false;

    do_swap_queues_if_needed (consume_lockg);

    if (m_consume->size () > 0)
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
    ux_redo_job_deque jobs_to_consume =
    	do_locked_find_job_to_consume_and_mark_in_progress (consume_lockg, in_progress_lockg);

    // specifically leave the 'out_adding_finished' on false as set at the beginning:
    //  - even if adding has been finished, the produce queue might still have jobs to be consumed
    //  - if so, setting the out param to true here, will cause consuming tasks to finish
    //  - thus allowing for a corner case where the jobs are not fully processed and all
    //    tasks have finished executing

    // job is null at this point, task will have to spin-wait and come again
    return jobs_to_consume;
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
    return ux_redo_job_deque ();
      }
    */
  }

  /*
  void
  redo_parallel::redo_job_queue::do_swap_queues_if_needed (const std::lock_guard<std::mutex> &a_consume_lockg)
  {
    // if consumption of everything in consume queue finished; see whether there's some more in the other one
    if (m_consume->size () == 0)
      {
  // TODO: a second barrier such that, consumption threads do not 'busy wait' by
  // constantly swapping two empty containers; works in combination with a notification
  // dispatched after new work items are added to the produce queue; this, in effect means that
  // this function will semantically become 'blocking_pop'

  // consumer queue is locked anyway, interested in not locking the producer queue
  bool notify_queues_empty = false;
  {
    // effectively, an 'atomic' swap because both queues are locked
    std::lock_guard<std::mutex> produce_lockg (m_produce_mutex);
    std::swap (m_produce, m_consume);
    m_produce_min_lsa_map.swap (m_consume_min_lsa_map);

    assert (m_produce->size () == m_produce_min_lsa_map.size () && m_produce->empty ());
    assert (m_consume->size () == m_consume_min_lsa_map.size ());

    if (m_monitor_minimum_log_lsa)
      {
        const log_lsa consume_minimum_log_lsa =
  	      m_consume->empty () ? MAX_LSA : m_consume_min_lsa_map.cbegin ()->first;
        // consume side, being empty, update with guard value
        m_minimum_log_lsa->set_for_produce_and_consume (
  	      MAX_LSA, consume_minimum_log_lsa);
      }

    // if both queues empty, notify the, possibly waiting, main thread
    m_queues_empty = m_produce->empty () && m_consume->empty ();
    notify_queues_empty = m_queues_empty;
  }
  if (notify_queues_empty)
    {
      m_queues_empty_cv.notify_one ();
    }
      }
  }
  */

  /*
  redo_parallel::redo_job_queue::ux_redo_job_deque
  redo_parallel::redo_job_queue::do_locked_find_job_to_consume_and_mark_in_progress (
    const std::lock_guard<std::mutex> &a_consume_lockg,
    const std::lock_guard<std::mutex> &a_in_progress_lockg)
  {
    // choose the vpid with the minimum lsa, to avoid going back in time with the bookkeeping
    // if jobs for that vpid happen to be in progress by another task, only way is to bail out, effectively
    // spin-waiting until that other task would have finished processing entries for the vpid currently
    // with lowest log_lsa;
    // this might happen in situations where there is very high contention on one single vpid
    const log_lsa_vpid_map_t::iterator consume_min_log_lsa_it = m_consume_min_lsa_map.begin ();
    const vpid &consume_min_log_lsa_vpid = consume_min_log_lsa_it->second;
    if (m_in_progress_vpids.find (consume_min_log_lsa_vpid) == m_in_progress_vpids.cend ())
      {
  vpid_ux_redo_job_deque_map_t::iterator consume_it = m_consume->find (consume_min_log_lsa_vpid);
  assert (consume_it != m_consume->end ());

  ux_redo_job_deque ret_job_deq {std::move (consume_it->second)};
  m_consume->erase (consume_it);

  assert (consume_min_log_lsa_it->first == (*ret_job_deq.cbegin ())->get_log_lsa ());
  m_consume_min_lsa_map.erase (consume_min_log_lsa_it);

  assert (m_consume->size () == m_consume_min_lsa_map.size ());

  do_locked_mark_job_deque_in_progress (a_in_progress_lockg, ret_job_deq);

  if (m_monitor_minimum_log_lsa)
    {
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
      const log_lsa &consume_minimum_log_lsa =
  	    m_consume->empty () ? MAX_LSA : m_consume_min_lsa_map.cbegin ()->first;
      const log_lsa in_progress_minimum_log_lsa = *m_in_progress_lsas.cbegin ();
      m_minimum_log_lsa->set_for_consume_and_in_progress (
  	    consume_minimum_log_lsa, in_progress_minimum_log_lsa);
    }

  return ret_job_deq;
      }

    // consumer task will have to spin-wait
    return ux_redo_job_deque ();
  }
  */

  /*
  void
  redo_parallel::redo_job_queue::do_locked_mark_job_deque_in_progress (
    const std::lock_guard<std::mutex> &a_in_progress_lockg,
    const ux_redo_job_deque &a_job_deque)
  {
    // all jobs have the same vpid
    const ux_redo_job_base &first_job = *a_job_deque.cbegin ();
    const vpid &first_job_vpid = first_job->get_vpid ();
    assert (m_in_progress_vpids.find (first_job_vpid) == m_in_progress_vpids.cend ());
    m_in_progress_vpids.insert (first_job_vpid);

    if (m_monitor_minimum_log_lsa)
      {
  // each job has a different log_lsa but, within a queue, jobs have ever increasing log_lsa's
  // so, suffices to only add the first one
  const log_lsa &first_job_log_lsa = first_job->get_log_lsa ();
  assert (m_in_progress_lsas.find (first_job_log_lsa) == m_in_progress_lsas.cend ());
  m_in_progress_lsas.insert (first_job_log_lsa);
      }
  }
  */

  void
  redo_parallel::redo_job_queue::notify_job_deque_finished (const redo_job_vector_t &a_job_deque)
  {
    /*
    bool vpid_set_empty = false;
    bool lsa_set_empty = false;
    {
      std::lock_guard<std::mutex> in_progress_lockg (m_in_progress_mutex);

      const ux_redo_job_base &first_job = *a_job_deque.cbegin ();
      // all jobs have the same vpid
      {
    const auto &first_job_vpid = first_job->get_vpid ();
    const auto vpid_it = m_in_progress_vpids.find (first_job_vpid);
    assert (vpid_it != m_in_progress_vpids.cend ());
    m_in_progress_vpids.erase (vpid_it);
      }
      vpid_set_empty = m_in_progress_vpids.empty ();

      if (m_monitor_minimum_log_lsa)
    {
      // each job has a different log_lsa but, within a queue, jobs have ever increasing log_lsa's
      // so, suffices to only add the first one
      const log_lsa &first_job_log_lsa = first_job->get_log_lsa ();
      const auto log_lsa_it = m_in_progress_lsas.find (first_job_log_lsa);
      assert (log_lsa_it != m_in_progress_lsas.cend ());
      m_in_progress_lsas.erase (log_lsa_it);

      const log_lsa &in_progress_minimum_log_lsa = m_in_progress_lsas.empty ()
          ? MAX_LSA : *m_in_progress_lsas.cbegin ();
      m_minimum_log_lsa->set_for_in_progress (in_progress_minimum_log_lsa);

      assert (m_in_progress_vpids.size () == m_in_progress_lsas.size ());
      assert ((vpid_set_empty && lsa_set_empty) || (!vpid_set_empty && !lsa_set_empty));
    }
      lsa_set_empty = m_in_progress_lsas.empty ();

    }

    if (vpid_set_empty && lsa_set_empty)
      {
    m_in_progress_vpids_empty_cv.notify_one ();
      }
    */
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

    /*
    {
      std::unique_lock<std::mutex> empty_in_progress_lock (m_in_progress_mutex);
      m_in_progress_vpids_empty_cv.wait (empty_in_progress_lock, [this] ()
      {
    return m_in_progress_vpids.empty ();
      });
      assert (m_in_progress_lsas.empty ());
    }

    assert_idle ();
    */
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
    for (const auto &jobs : m_produce_vec)
      {
	assert (jobs != nullptr);
	assert (jobs->empty ());
      }
    /*
    assert (m_produce->empty ());
    assert (m_produce_min_lsa_map.empty ());

    assert (m_consume->empty ());
    assert (m_consume_min_lsa_map.empty ());

    assert (m_in_progress_vpids.empty ());
    assert (m_in_progress_lsas.empty ());
    */
  }

  void
  redo_parallel::redo_job_queue::set_empty_at (std::size_t a_index)
  {
    if (m_monitor_minimum_log_lsa)
      {
	std::lock_guard<std::mutex> empty_lockg (m_empty_mutex);
	m_empty_vec[a_index] = true;
	m_empty_cv.notify_all ();
      }
  }

  void
  redo_parallel::redo_job_queue::set_non_empty_at (std::size_t a_index)
  {
    if (m_monitor_minimum_log_lsa)
      {
	std::lock_guard<std::mutex> empty_lockg (m_empty_mutex);
	m_empty_vec[a_index] = false;
      }
  }

  /*
  void
  redo_parallel::redo_job_queue::do_check_empty_and_notify(std::lock_guard<std::mutex> &a_empty_lockg)
  {
    bool all_empty = true;
    for (const bool &curr_empty : m_empty_vec)
      {
        if (!curr_empty)
          {
            all_empty = false;
            break;
          }
      }
    if (all_empty)
      {
        m_empty_cv.notify_all();
      }
  }
  */

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
      redo_task (std::size_t a_task_idx, redo_parallel::task_active_state_bookkeeping &task_state_bookkeeping
		 , redo_parallel::redo_job_queue &a_queue);
      redo_task (const redo_task &) = delete;
      redo_task (redo_task &&) = delete;

      redo_task &operator = (const redo_task &) = delete;
      redo_task &operator = (redo_task &&) = delete;

      ~redo_task () override;

      void execute (context_type &context) override;
      void retire () override;

      void log_perf_stats () const;
      void accumulate_perf_stats (cubperf::stat_value *a_output_stats, std::size_t a_output_stats_size) const;

    private:
      const std::size_t m_task_idx;
      redo_parallel::task_active_state_bookkeeping &m_task_state_bookkeeping;
      redo_parallel::redo_job_queue &m_queue;

      cubperf::statset_definition m_perf_stats_definition;
      perf_stats m_perf_stats;

      log_reader m_log_pgptr_reader { LOG_CS_SAFE_READER };
      LOG_ZIP m_undo_unzip_support;
      LOG_ZIP m_redo_unzip_support;
  };

  /*********************************************************************
   * redo_parallel::redo_task - definition
   *********************************************************************/

  constexpr unsigned short redo_parallel::redo_task::WAIT_AND_CHECK_MILLIS;

  redo_parallel::redo_task::redo_task (std::size_t a_task_idx
				       , redo_parallel::task_active_state_bookkeeping &task_state_bookkeeping
				       , redo_parallel::redo_job_queue &a_queue)
    : m_task_idx { a_task_idx }
    , m_task_state_bookkeeping (task_state_bookkeeping)
    , m_queue (a_queue)
    , m_perf_stats_definition { perf_stats_async_definition_init_list }
    , m_perf_stats { perf_stats_is_active_for_async (), m_perf_stats_definition }
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
    jobs_vec->reserve (PARALLEL_REDO_JOB_VECTOR_RESERVE_SIZE);

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
		m_perf_stats.time_and_increment (cublog::PERF_STAT_ID_PARALLEL_SLEEP);
	      }
	    else
	      {
		assert (!jobs_vec->empty ());
		THREAD_ENTRY *const thread_entry = &context;
		for (auto &job : *jobs_vec)
		  {
		    job->execute (thread_entry, m_log_pgptr_reader, m_undo_unzip_support, m_redo_unzip_support);
		    job->retire (m_task_idx);
		  }

		m_queue.notify_job_deque_finished (*jobs_vec);

		// pointers still present in the vector are either:
		//  - already passed on to the reusable job container
		//  - dangling, as they have deleted themselves
		jobs_vec->clear ();
	      }
	  }
      }

    assert (jobs_vec != nullptr);
    assert (jobs_vec->empty ());
    delete jobs_vec;

    m_task_state_bookkeeping.set_inactive ();
  }

  void redo_parallel::redo_task::retire ()
  {
    // avoid self destruct, will be deleted by owning class
  }

  void redo_parallel::redo_task::log_perf_stats () const
  {
    std::stringstream ss;
    ss << "Log recovery redo worker thread " << m_task_idx << " perf stats";
    m_perf_stats.log (ss.str ().c_str ());
  }

  void redo_parallel::redo_task::accumulate_perf_stats (
	  cubperf::stat_value *a_output_stats, std::size_t a_output_stats_size) const
  {
    m_perf_stats.accumulate (a_output_stats, a_output_stats_size);
  }

  /*********************************************************************
   * redo_parallel - definition
   *********************************************************************/

  redo_parallel::redo_parallel (unsigned a_worker_count, minimum_log_lsa_monitor *a_minimum_log_lsa)
    : m_worker_pool (nullptr)
    , m_job_queue { a_worker_count, a_minimum_log_lsa }
    , m_waited_for_termination (false)
  {
    assert (a_worker_count > 0);

    const thread_type tt = log_is_in_crash_recovery () ? TT_RECOVERY : TT_REPLICATION;
    m_pool_context_manager = std::make_unique<cubthread::system_worker_entry_manager> (tt);

    do_init_worker_pool (a_worker_count);
    do_init_tasks (a_worker_count);
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
  redo_parallel::do_init_tasks (std::size_t a_task_count)
  {
    assert (a_task_count > 0);
    assert (m_worker_pool != nullptr);

    for (unsigned task_idx = 0; task_idx < a_task_count; ++task_idx)
      {
	auto task = std::make_unique<redo_parallel::redo_task> (task_idx, m_task_state_bookkeeping, m_job_queue);
	m_worker_pool->execute (task.get ());
	m_redo_tasks.push_back (std::move (task));
      }
  }

  void
  redo_parallel::log_perf_stats () const
  {
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
    avg_perf_stat_results.resize (value_count, 0.0);
    for (std::size_t idx = 0; idx < value_count; ++idx)
      {
	avg_perf_stat_results[idx] = accum_perf_stat_results[idx] / task_count;
      }

    log_perf_stats_values_with_definition ("Log recovery redo worker threads averaged perf stats",
					   definition, avg_perf_stat_results.data (), value_count);
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

  void redo_job_impl::retire (std::size_t a_task_idx)
  {
    // return the job back to the pool of available reusable jobs
    m_reusable_job_stack->push (a_task_idx, this);
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
    : m_job_count { 0 }
    , m_push_task_count { 0 }
    , m_flush_push_at_count { 0 }
    , m_jobs_arr { nullptr }
  {
  }

  void reusable_jobs_stack::initialize (std::size_t a_job_count, std::size_t a_push_task_count,
					std::size_t a_flush_push_at_count,
					const log_lsa *a_end_redo_lsa, bool force_each_page_fetch)
  {
    assert (m_job_count == 0);
    assert (m_push_task_count == 0);
    assert (m_flush_push_at_count == 0);

    assert (m_jobs_arr == nullptr);

    assert (m_pop_jobs.empty ());
    assert (m_push_jobs.empty ());

    assert (m_per_task_push_jobs_vec.empty ());

    assert (a_job_count > 0);
    assert (a_push_task_count > 0);
    assert (a_flush_push_at_count > 0);

    m_job_count = a_job_count;
    m_push_task_count = a_push_task_count;
    m_flush_push_at_count = a_flush_push_at_count;

    m_jobs_arr = static_cast<unsigned char *> (malloc (sizeof (redo_job_impl) * m_job_count));

    m_pop_jobs.reserve (m_job_count);
    for (std::size_t idx = 0; idx < m_job_count; ++idx)
      {
	redo_job_impl *const job = new (m_jobs_arr + sizeof (redo_job_impl) * idx)
	redo_job_impl (a_end_redo_lsa, force_each_page_fetch, this);
	m_pop_jobs.push_back (job);
      }

    m_push_jobs.reserve (m_job_count);

    m_per_task_push_jobs_vec.resize (m_push_task_count);
    const std::size_t per_task_reserve_size = m_job_count / m_push_task_count * 2;
    for (job_container_t &jobs: m_per_task_push_jobs_vec)
      {
	jobs.reserve (per_task_reserve_size);
      }
  }

  reusable_jobs_stack::~reusable_jobs_stack ()
  {
    // consistency check that all job instances have been 'retuned to the source'
    assert ([this] ()
    {
      const std::size_t pop_size = m_pop_jobs.size ();
      const std::size_t push_size = m_push_jobs.size ();
      std::size_t per_task_push_size = 0;
      for (auto &push_container: m_per_task_push_jobs_vec)
	{
	  per_task_push_size += push_container.size ();
	}
      assert ((pop_size + push_size + per_task_push_size) == m_job_count);
      return true;
    }
    ());

    // formally invoke dtor  on all in-place constructed objects
    for (auto &job : m_pop_jobs)
      {
	job->~redo_job_impl ();
      }
    for (auto &job : m_push_jobs)
      {
	job->~redo_job_impl ();
      }
    for (auto &push_job_container: m_per_task_push_jobs_vec)
      {
	for (auto &job : push_job_container)
	  {
	    job->~redo_job_impl ();
	  }
      }

    free (m_jobs_arr);
    m_jobs_arr = nullptr;
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
