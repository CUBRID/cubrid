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

/*
 * thread_worker_pool.cpp
 */

#include "thread_worker_pool.hpp"

#include "resources.hpp"
#include "error_manager.h"
#include "perf.hpp"

#include <sstream>
#include <cstring>
#include <cmath>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace cubthread
{
  /************************************************************************/
  /* inline implementation						  */
  /************************************************************************/

  //////////////////////////////////////////////////////////////////////////
  // worker_pool implementation
  //////////////////////////////////////////////////////////////////////////

  worker_pool::worker_pool (std::size_t pool_size, std::size_t task_max_count,
			    entry_manager &entry_mgr, const char *name, std::size_t core_count,
			    bool debug_log, bool pool_threads, wait_seconds wait_for_task_time)
    : m_max_workers (pool_size)
    , m_task_max_count (task_max_count)
    , m_task_count (0)
    , m_entry_manager (entry_mgr)
    , m_core_array (NULL)
    , m_core_count (core_count)
    , m_round_robin_counter (0)
    , m_stopped (false)
    , m_log (debug_log)
    , m_pool_threads (pool_threads)
    , m_wait_for_task_time (wait_for_task_time)
    , m_name (name == NULL ? "" : name)
  {
    // initialize cores; we'll try to distribute pool evenly to all cores. if core count is not fully contained in
    // pool size, some cores will have one additional worker

    if (m_core_count == 0)
      {
	assert (false);
	m_core_count = 1;
      }

    if (m_core_count > pool_size)
      {
	m_core_count = pool_size;
      }

    m_core_array = new core[m_core_count];

    std::size_t quotient = m_max_workers / m_core_count;
    std::size_t remainder = m_max_workers % m_core_count;
    std::size_t it = 0;

    for (; it < remainder; it++)
      {
	m_core_array[it].init_pool_and_workers (*this, quotient + 1);
      }
    for (; it < m_core_count; it++)
      {
	m_core_array[it].init_pool_and_workers (*this, quotient);
      }

    if (wp_is_thread_always_alive_forced ())
      {
	// override pooling/wait time options to keep threads always alive
	m_pool_threads = true;
	m_wait_for_task_time.set_infinite_wait ();
      }
  }

  worker_pool::~worker_pool ()
  {
    // not safe to destroy running pools
    assert (m_stopped);

    delete [] m_core_array;
    m_core_array = NULL;
  }

  bool
  worker_pool::try_execute (task_type *work_arg)
  {
    if (is_full ())
      {
	return false;
      }

    execute (work_arg);
    return true;
  }

  void
  worker_pool::execute (task_type *work_arg)
  {
    execute_on_core (work_arg, get_round_robin_core_hash ());
  }

  void
  worker_pool::execute_on_core (task_type *work_arg, std::size_t core_hash, bool for_method)
  {
    // increment task count
    ++m_task_count;

    std::size_t core_index = core_hash % m_core_count;
    m_core_array[core_index].execute_task (work_arg, for_method);
  }

  void
  worker_pool::stop_execution (void)
  {
    if (m_stopped.exchange (true))
      {
	// already stopped
	return;
      }
    else
      {
	// I am responsible with stopping threads
      }

#if defined (NDEBUG)
    const std::chrono::seconds time_wait_to_thread_stop (30);   // timeout duration = 30 secs on release mode
    const std::chrono::milliseconds time_spin_sleep (10);       // sleep between spins for 10 milliseconds
#else // DEBUG
    const std::chrono::seconds time_wait_to_thread_stop (60);   // timeout duration = 60 secs on debug mode
    const std::chrono::milliseconds time_spin_sleep (10);       // sleep between spins for 10 milliseconds
#endif

    auto timeout = std::chrono::system_clock::now () + time_wait_to_thread_stop;

    bool is_not_stopped;
    while (true)
      {
	// notify all cores to stop
	is_not_stopped = false;     // assume all are stopped
	for (std::size_t it = 0; it < m_core_count; it++)
	  {
	    // notify all workers to stop. if any worker is still running, is_not_stopped = true is output
	    m_core_array[it].notify_stop (is_not_stopped);
	  }

	if (!is_not_stopped)
	  {
	    // all stopped
	    break;
	  }

	if (std::chrono::system_clock::now () > timeout)
	  {
	    // timed out
	    assert (false);
	    break;
	  }

	// sleep for a while to give running threads a chance to finish
	std::this_thread::sleep_for (time_spin_sleep);
      }

    // retire all tasks that have not been executed; at this point, no new tasks are produced
    for (std::size_t it = 0; it < m_core_count; it++)
      {
	m_core_array[it].retire_queued_tasks ();
      }
  }

  void
  worker_pool::start_all_workers (void)
  {
    for (std::size_t it = 0; it < m_core_count; it++)
      {
	m_core_array[it].start_all_workers ();
      }
  }

  bool
  worker_pool::is_running (void) const
  {
    return !m_stopped;
  }

  const std::string &
  worker_pool::get_name (void) const
  {
    return m_name;
  }

  bool
  worker_pool::is_full (void) const
  {
    return m_task_count >= m_task_max_count;
  }

  std::size_t
  worker_pool::get_max_count (void) const
  {
    return m_max_workers;
  }

  std::size_t
  worker_pool::get_core_count (void) const
  {
    return m_core_count;
  }

  void
  worker_pool::get_stats (cubperf::stat_value *stats_out) const
  {
    for (std::size_t it = 0; it < m_core_count; it++)
      {
	m_core_array[it].get_stats (stats_out);
      }
  }

  void worker_pool::get_task_stats (uint64_t *stats_out) noexcept
  {
    for (std::size_t it = 0; it < m_core_count; it++)
      {
	auto [requested, started, completed] = m_core_array[it].get_task_stats ();
	stats_out[0] += requested;
	stats_out[1] += started;
	stats_out[2] += completed;
      }
  }

  void
  worker_pool::er_log_stats (void) const
  {
    if (!m_log)
      {
	return;
      }

    const std::size_t MAX_SIZE = 32;
    cubperf::stat_value stats[MAX_SIZE];
    std::memset (stats, 0, sizeof (stats));
    get_stats (stats);
    wp_er_log_stats (m_name.c_str (), stats);
  }

  std::size_t
  worker_pool::get_round_robin_core_hash (void)
  {
    // cores are not necessarily equal, so we try to preserve the assignments proportional to their size.
    // if the worker pool size is 15 and there are four cores, three of them will have four workers and one only three.
    // task are dispatched in this order:
    //
    // core 1  |  core 2  |  core 3  |  core 4
    //      1  |       2  |       3  |       4
    //      5  |       6  |       7  |       8
    //      9  |      10  |      11  |      12
    //     13  |      14  |      15                   // last one is skipped this round to keep proportions
    //     16  |      17  |      18  |      19
    //  ...
    //

    // get a core index atomically
    std::size_t index;
    std::size_t next_index;

    while (true)
      {
	index = m_round_robin_counter;

	next_index = index + 1;
	if (next_index == m_max_workers)
	  {
	    next_index = 0;
	  }

	if (m_round_robin_counter.compare_exchange_strong (index, next_index))
	  {
	    // my index is found
	    break;
	  }
      }

    return index;
  }

  //////////////////////////////////////////////////////////////////////////
  // worker_pool::core
  //////////////////////////////////////////////////////////////////////////

  worker_pool::core::core ()
    : m_parent_pool (NULL)
    , m_max_workers (0)
    , m_worker_array (NULL)
    , m_available_workers (NULL)
    , m_available_count (0)
    , m_task_queue ()
    , m_workers_mutex ()
    , m_temp_workers ()
    , m_temp_free_workers ()
    , m_temp_workers_mutex ()
  {
    m_task_metrics.requested.store (0, std::memory_order_relaxed);
    m_task_metrics.started.store (0, std::memory_order_relaxed);
    m_task_metrics.completed.store (0, std::memory_order_relaxed);
  }

  worker_pool::core::~core ()
  {
    delete [] m_worker_array;
    m_worker_array = NULL;

    delete [] m_available_workers;
    m_available_workers = NULL;

    std::lock_guard<std::mutex> ulock (m_temp_workers_mutex);

    for (worker *w : m_temp_workers)
      {
	if (w)
	  {
	    delete w;
	  }
      }
    m_temp_workers.clear ();

    for (worker *w : m_temp_free_workers)
      {
	delete w;
      }
    m_temp_free_workers.clear ();
  }

  void
  worker_pool::core::init_pool_and_workers (worker_pool &parent, std::size_t worker_count)
  {
    assert (worker_count > 0);

    m_parent_pool = &parent;
    m_max_workers = worker_count;

    // allocate workers array
    m_worker_array = new worker[m_max_workers];
    m_available_workers = new worker*[m_max_workers];

    for (std::size_t it = 0; it < m_max_workers; it++)
      {
	m_worker_array[it].init_core (*this);
	if (m_parent_pool->m_pool_threads)
	  {
	    // assign task / start thread
	    // it will add itself to available workers
	    m_worker_array[it].assign_task (NULL, cubperf::clock::now ());
	  }
	else
	  {
	    // add to available workers
	    m_available_workers[m_available_count++] = &m_worker_array[it];
	  }
      }
  }

  void
  worker_pool::core::finished_task_notification (void)
  {
    // decrement task count
    -- (m_parent_pool->m_task_count);
  }

  void
  worker_pool::core::execute_task (task_type *task_p, bool for_method)
  {
    assert (task_p != NULL);

    // find an available worker
    // 1. one already active is preferable
    // 2. inactive will do too
    // 3. if no workers, reject task (returns false)

    cubperf::time_point push_time = cubperf::clock::now ();
    worker *refp = NULL;

    if (m_parent_pool->m_stopped)
      {
	// reject task
	task_p->retire ();
	return;
      }

    on_task_requested ();

    std::unique_lock<std::mutex> ulock (m_workers_mutex);

    if (m_available_count > 0)
      {
	refp = m_available_workers[--m_available_count];
	ulock.unlock ();

	assert (refp != NULL);
	refp->assign_task (task_p, push_time);
      }
    else
      {
	if (for_method)
	  {
	    execute_temp_task (task_p, push_time);
	  }
	else
	  {
	    // save to queue
	    m_task_queue.push (task_p);
	  }
      }
  }

  void
  worker_pool::core::execute_temp_task (task_type *task_p, const cubperf::time_point &push_time)
  {
    worker *w = new worker (true);
    w->init_core (*this);

    std::lock_guard<std::mutex> ulock (m_temp_workers_mutex);
    m_temp_workers.insert (w);
    w->assign_task (task_p, push_time);
  }

  worker_pool::task_type *
  worker_pool::core::get_task_or_become_available (worker &worker_arg)
  {
    std::unique_lock<std::mutex> ulock (m_workers_mutex);

    if (!m_task_queue.empty ())
      {
	task_type *task_p = m_task_queue.front ();
	assert (task_p != NULL);
	m_task_queue.pop ();
	return task_p;
      }

    m_available_workers[m_available_count++] = &worker_arg;
    assert (m_available_count <= m_max_workers);
    return NULL;
  }

  void
  worker_pool::core::become_available (worker &worker_arg)
  {
    std::unique_lock<std::mutex> ulock (m_workers_mutex);
    m_available_workers[m_available_count++] = &worker_arg;
    assert (m_available_count <= m_max_workers);
  }

  void
  worker_pool::core::check_worker_not_available (const worker &worker_arg)
  {
#if !defined (NDEBUG)
    std::unique_lock<std::mutex> ulock (m_workers_mutex);

    for (std::size_t idx = 0; idx < m_available_count; idx++)
      {
	assert (m_available_workers[idx] != &worker_arg);
      }
#endif // DEBUG
  }

  entry_manager &
  worker_pool::core::get_entry_manager (void)
  {
    return m_parent_pool->m_entry_manager;
  }

  std::size_t
  worker_pool::core::get_max_worker_count (void) const
  {
    return m_max_workers;
  }

  void
  worker_pool::core::notify_stop (bool &is_not_stopped)
  {
    // stop all temp workers first
    std::unique_lock<std::mutex> ulock (m_temp_workers_mutex);
    for (worker *w : m_temp_workers)
      {
	w->stop_execution (is_not_stopped);
      }
    ulock.unlock ();

    // tell all workers to stop
    for (std::size_t it = 0; it < m_max_workers; it++)
      {
	m_worker_array[it].stop_execution (is_not_stopped);
      }
  }

  void
  worker_pool::core::start_all_workers (void)
  {
    worker *refp = NULL;
    const std::size_t AVAILABLE_STACK_DEFAULT_SIZE = 1024;
    cubmem::appendable_array<worker *, AVAILABLE_STACK_DEFAULT_SIZE> available_stack;

    // how this works:
    //
    // we need to start all workers, but we need to consider the fact that some may be already running. what we need
    // to do is process the available workers and start threads for all those that don't have a thread started.
    //
    // workers that already have threads are saved and added back after processing all available workers.
    //
    // NOTE: this function does not guarantee that at the end all workers have threads. workers that stop their threads
    //       during processing available workers are not restarted. however, we end up starting all or almost all
    //       threads, which is good enough.
    //

    while (true)
      {
	// processing is done in two steps:
	//
	//    1. retrieve worker from available workers holding workers mutex
	//    2. verify if worker has a thread started
	//        2.1. if it doesn't have a thread, start one
	//        2.2. if it does have thread, save it to available_stack.
	//

	std::unique_lock<std::mutex> core_lock (m_workers_mutex);
	if (m_available_count == 0)
	  {
	    break;
	  }
	refp = m_available_workers[--m_available_count];
	core_lock.unlock ();

	if (refp->has_thread ())
	  {
	    // stack to make available at the end
	    available_stack.append (&refp, 1);

	    // note: this worker's thread may stop soon or may have stopped already. this case is accepted.
	  }
	else
	  {
	    // this thread is already stopped and we can start its thread
	    refp->set_push_time_now ();
	    refp->set_has_thread ();
	    refp->start_thread ();
	  }
      }

    // copy all workers having threads back to available array.
    if (available_stack.get_size () > 0)
      {
	std::unique_lock<std::mutex> core_lock (m_workers_mutex);
	if (m_available_count > 0)
	  {
	    // move current available to make room for older ones
	    std::memmove (m_available_workers + available_stack.get_size (), m_available_workers,
			  m_available_count * sizeof (worker *));
	  }

	// copy from stack at the beginning of m_available_workers
	std::memcpy (m_available_workers, available_stack.get_array (), available_stack.get_memsize ());

	// update available count
	m_available_count += available_stack.get_size ();
      }
  }

  void
  worker_pool::core::retire_queued_tasks (void)
  {
    std::unique_lock<std::mutex> ulock (m_workers_mutex);

    while (!m_task_queue.empty ())
      {
	m_task_queue.front ()->retire ();
	m_task_queue.pop ();
      }
  }

  void
  worker_pool::core::get_stats (cubperf::stat_value *stats_out) const
  {
    for (std::size_t it = 0; it < m_max_workers; it++)
      {
	m_worker_array[it].get_stats (stats_out);
      }

    std::unique_lock<std::mutex> ulock (m_temp_workers_mutex);
    for (worker *w: m_temp_workers)
      {
	w->get_stats (stats_out);
      }
  }

  void
  worker_pool::core::register_free_temp_list (worker *w)
  {
    std::unique_lock<std::mutex> ulock (m_temp_workers_mutex);

    m_temp_workers.erase (w);
    m_temp_free_workers.push_back (w);
  }

  void
  worker_pool::core::free_all_temp_list ()
  {
    std::unique_lock<std::mutex> ulock (m_temp_workers_mutex);

    for (worker *w: m_temp_free_workers)
      {
	delete w;
      }
    m_temp_free_workers.clear ();
  }

  inline uint64_t worker_pool::core::on_task_requested () noexcept
  {
    return m_task_metrics.requested.fetch_add (1, std::memory_order_relaxed);
  }

  inline uint64_t worker_pool::core::on_task_started () noexcept
  {
    return m_task_metrics.started.fetch_add (1, std::memory_order_relaxed);
  }

  inline uint64_t worker_pool::core::on_task_completed () noexcept
  {
    return m_task_metrics.completed.fetch_add (1, std::memory_order_relaxed);
  }

  std::tuple<uint64_t, uint64_t, uint64_t> worker_pool::core::get_task_stats () noexcept
  {
    uint64_t completed = m_task_metrics.completed.load (std::memory_order_relaxed);
    uint64_t started = m_task_metrics.started.load (std::memory_order_relaxed);
    uint64_t requested = m_task_metrics.requested.load (std::memory_order_relaxed);
    return { requested, started, completed };
  }

  //////////////////////////////////////////////////////////////////////////
  // worker_pool<Context>::core::worker
  //////////////////////////////////////////////////////////////////////////

  worker_pool::core::worker::worker (bool is_temp)
    : m_parent_core (NULL)
    , m_context_p (NULL)
    , m_task_p (NULL)
    , m_task_cv ()
    , m_task_mutex ()
    , m_stop (false)
    , m_has_thread (false)
    , m_is_temp (is_temp)
    , m_statistics (wp_worker_statset_create ())
    , m_push_time ()
  {
  }

  worker_pool::core::worker::~worker (void)
  {
    wp_worker_statset_destroy (m_statistics);
  }

  void
  worker_pool::core::worker::init_core (core &parent)
  {
    m_parent_core = &parent;
  }

  void
  worker_pool::core::worker::assign_task (task_type *work_p, cubperf::time_point push_time)
  {
    // save push time
    m_push_time = push_time;

    std::unique_lock<std::mutex> ulock (m_task_mutex);

    // save task
    m_task_p = work_p;

    if (m_is_temp)
      {
	m_has_thread = true;
	assert (m_context_p == NULL);
	start_thread ();
      }

    if (m_has_thread)
      {
	// notify waiting thread
	ulock.unlock (); // mutex is not needed for notify
	m_task_cv.notify_one ();
      }
    else
      {
	m_has_thread = true;
	ulock.unlock ();

	assert (m_context_p == NULL);

	start_thread ();
      }
  }

  void
  worker_pool::core::worker::start_thread (void)
  {
    assert (m_has_thread);

    //
    // the next code tries to help visualizing any system errors that can occur during create or detach in debug
    // mode
    //
    // release will basically be reduced to:
    // std::thread (&worker::run, this).detach ();
    //

    std::thread t;

    auto lambda_create = [&] (void) -> void { t = std::thread (&worker::run, this); };
    auto lambda_detach = [&] (void) -> void { t.detach (); };

    wp_call_func_throwing_system_error ("starting thread", lambda_create);
    wp_call_func_throwing_system_error ("detaching thread", lambda_detach);
  }

  void
  worker_pool::core::worker::push_task_on_running_thread (task_type *work_p,
      cubperf::time_point push_time)
  {
    // run on current thread
    assert (work_p != NULL);

    m_push_time = push_time;

    // must lock task mutex
    std::unique_lock<std::mutex> ulock (m_task_mutex);

    // make sure worker is in a valid state
    assert (m_task_p == NULL);
    assert (m_context_p != NULL);

    // set task
    m_task_p = work_p;

    // notify waiting thread
    ulock.unlock (); // mutex is not needed for notify
    m_task_cv.notify_one ();
  }

  void
  worker_pool::core::worker::stop_execution (bool &is_not_stopped)
  {
    context_type *context_p = m_context_p;

    if (context_p != NULL)
      {
	// notify context to stop
	m_parent_core->get_entry_manager ().stop_execution (*context_p);
      }

    // make sure thread is not waiting for tasks
    std::unique_lock<std::mutex> ulock (m_task_mutex);

    if (m_has_thread)
      {
	/// this thread is still running
	is_not_stopped = true;
      }

    m_stop = true;    // stop worker
    ulock.unlock ();    // mutex is not needed for notify

    if (m_is_temp)
      {
	// not to notify one if it is for temp
	return;
      }
    m_task_cv.notify_one ();
  }

  void
  worker_pool::core::worker::init_run (void)
  {
    // safe-guard - we have a thread
    assert (m_has_thread);

    // safe-guard - threads should [no longer] be available
    if (m_is_temp == false)
      {
	m_parent_core->check_worker_not_available (*this);
      }

    // thread was started
    m_statistics.m_timept = m_push_time;
    wp_worker_statset_time_and_increment (m_statistics, Wpstat_start_thread);

    // a context is required
    m_context_p = &m_parent_core->get_entry_manager ().create_context ();
    wp_worker_statset_time_and_increment (m_statistics, Wpstat_create_context);
  }

  void
  worker_pool::core::worker::finish_run (void)
  {
    assert (m_task_p == NULL);
    assert (m_context_p != NULL);

    // retire context
    m_parent_core->get_entry_manager ().retire_context (*m_context_p);
    m_context_p = NULL;
    wp_worker_statset_time_and_increment (m_statistics, Wpstat_retire_context);

    if (m_is_temp)
      {
	m_parent_core->register_free_temp_list (this);
      }
  }

  void
  worker_pool::core::worker::retire_current_task (void)
  {
    assert (m_task_p != NULL);

    // retire task
    m_task_p->retire ();
    m_task_p = NULL;
    wp_worker_statset_time_and_increment (m_statistics, Wpstat_retire_task);
  }

  void
  worker_pool::core::worker::execute_current_task (void)
  {
    assert (m_task_p != NULL);

    m_parent_core->on_task_started ();

    // execute task
    m_task_p->execute (*m_context_p);

    m_parent_core->on_task_completed ();
    wp_worker_statset_time_and_increment (m_statistics, Wpstat_execute_task);

    // and retire task
    retire_current_task ();

    // and recycle context before getting another task
    m_parent_core->get_entry_manager ().recycle_context (*m_context_p);
    wp_worker_statset_time_and_increment (m_statistics, Wpstat_recycle_context);

    // notify core one task was finished
    if (m_is_temp == false)
      {
	m_parent_core->finished_task_notification ();
	m_parent_core->free_all_temp_list ();
      }
  }

  bool
  worker_pool::core::worker::get_new_task (void)
  {
    assert (m_task_p == NULL);

    std::unique_lock<std::mutex> ulock (m_task_mutex, std::defer_lock);

    // check stop condition
    if (!m_stop)
      {
	// get a queued task or wait for one to come

	// either get a queued task or add to free active list
	// note: returned task cannot be saved directly to m_task_p. if worker is added to wait queue and NULL is returned,
	//       current thread may be preempted. worker is then claimed from free active list and worker is assigned
	//       a task. this changes expected behavior and can have unwanted consequences.
	task_type *task_p = m_parent_core->get_task_or_become_available (*this);
	if (task_p != NULL)
	  {
	    wp_worker_statset_time_and_increment (m_statistics, Wpstat_found_in_queue);

	    // it is safe to set here
	    m_task_p = task_p;
	    return true;
	  }

	// wait for task
	ulock.lock ();
	if (m_task_p == NULL && !m_stop)
	  {
	    // wait until a task is received or stopped ...
	    // ... or time out
	    condvar_wait (m_task_cv, ulock, m_parent_core->get_parent_pool ()->get_wait_for_task_time (),
			  [this] () -> bool { return m_task_p != NULL || m_stop; });
	  }
	else
	  {
	    // no need to wait
	  }
      }
    else
      {
	// we need to add to available list
	m_parent_core->become_available (*this);

	ulock.lock ();
      }

    // did I get a task?
    if (m_task_p == NULL)
      {
	// no; this thread will stop. from this point forward, if a new task is assigned, a new thread must be spawned
	m_has_thread = false;

	// finish_run; we neet to retire context before another thread uses this worker
	m_statistics.m_timept = cubperf::clock::now ();
	finish_run ();

	return false;
      }
    else
      {
	// unlock mutex
	ulock.unlock ();

	// safe-guard - threads should no longer be available
	m_parent_core->check_worker_not_available (*this);

	// found task
	m_statistics.m_timept = m_push_time;
	wp_worker_statset_time_and_increment (m_statistics, Wpstat_wakeup_with_task);
	return true;
      }
  }

  void
  worker_pool::core::worker::run (void)
  {
    os::resources::cpu::clearaffinity ();   // clear the affinity at start
    pthread_setname_np (pthread_self (), m_parent_core->get_parent_pool ()->get_name ().c_str ());

    init_run ();    // do stuff at the beginning like creating context

    if (m_is_temp)
      {
	execute_current_task ();
	finish_run ();
	return;
      }

    if (m_task_p == NULL)
      {
	// started without task; get one
	if (get_new_task ())
	  {
	    assert (m_task_p != NULL);
	  }
      }

    if (m_task_p != NULL)
      {
	// loop and execute as many tasks as possible
	do
	  {
	    execute_current_task ();
	  }
	while (get_new_task ());
      }
    else
      {
	// never got a task
      }

    // finish_run ();    // do stuff on end like retiring context
  }

  void
  worker_pool::core::worker::get_stats (cubperf::stat_value *sum_inout) const
  {
    wp_worker_statset_accumulate (m_statistics, sum_inout);
  }

  //////////////////////////////////////////////////////////////////////////
  // checker
  //////////////////////////////////////////////////////////////////////////

  static bool FORCE_THREAD_ALWAYS_ALIVE = false;

  bool
  wp_is_thread_always_alive_forced ()
  {
    return FORCE_THREAD_ALWAYS_ALIVE;
  }

  void
  wp_set_force_thread_always_alive ()
  {
    FORCE_THREAD_ALWAYS_ALIVE = true;
  }

  //////////////////////////////////////////////////////////////////////////
  // statistics
  //////////////////////////////////////////////////////////////////////////

  static const cubperf::statset_definition Worker_pool_statdef =
  {
    cubperf::stat_definition (Wpstat_start_thread, cubperf::stat_definition::COUNTER_AND_TIMER,
			      "Counter_start_thread", "Timer_start_thread"),
    cubperf::stat_definition (Wpstat_create_context, cubperf::stat_definition::COUNTER_AND_TIMER,
			      "Counter_create_context", "Timer_create_context"),
    cubperf::stat_definition (Wpstat_execute_task, cubperf::stat_definition::COUNTER_AND_TIMER,
			      "Counter_execute_task", "Timer_execute_task"),
    cubperf::stat_definition (Wpstat_retire_task, cubperf::stat_definition::COUNTER_AND_TIMER,
			      "Counter_retire_task", "Timer_retire_task"),
    cubperf::stat_definition (Wpstat_found_in_queue, cubperf::stat_definition::COUNTER_AND_TIMER,
			      "Counter_found_task_in_queue", "Timer_found_task_in_queue"),
    cubperf::stat_definition (Wpstat_wakeup_with_task, cubperf::stat_definition::COUNTER_AND_TIMER,
			      "Counter_wakeup_with_task", "Timer_wakeup_with_task"),
    cubperf::stat_definition (Wpstat_recycle_context, cubperf::stat_definition::COUNTER_AND_TIMER,
			      "Counter_recycle_context", "Timer_recycle_context"),
    cubperf::stat_definition (Wpstat_retire_context, cubperf::stat_definition::COUNTER_AND_TIMER,
			      "Counter_retire_context", "Timer_retire_context")
  };

  cubperf::statset &
  wp_worker_statset_create (void)
  {
    return *Worker_pool_statdef.create_statset ();
  }

  void
  wp_worker_statset_destroy (cubperf::statset &stats)
  {
    delete &stats;
  }

  void
  wp_worker_statset_time_and_increment (cubperf::statset &stats, cubperf::stat_id id)
  {
    Worker_pool_statdef.time_and_increment (stats, id);
  }

  void
  wp_worker_statset_accumulate (const cubperf::statset &what, cubperf::stat_value *where)
  {
    Worker_pool_statdef.add_stat_values_with_converted_timers<std::chrono::microseconds> (what, where);
  }

  std::size_t
  wp_worker_statset_get_count (void)
  {
    return Worker_pool_statdef.get_value_count ();
  }

  const char *
  wp_worker_statset_get_name (std::size_t stat_index)
  {
    return Worker_pool_statdef.get_value_name (stat_index);
  }

  //////////////////////////////////////////////////////////////////////////
  // functions
  //////////////////////////////////////////////////////////////////////////

  std::size_t
  system_core_count (void)
  {
    return os::resources::cpu::effective ().adjusted_max;
  }

  void
  wp_handle_system_error (const char *message, const std::system_error &e)
  {
    er_print_callstack (ARG_FILE_LINE, "%s - throws err = %d: %s\n", message, e.code().value(), e.what ());
    assert (false);
    throw e;
  }

  void
  wp_er_log_stats (const char *header, cubperf::stat_value *statsp)
  {
    std::stringstream ss;

    ss << "Worker pool statistics: " << header << std::endl;

    for (std::size_t index = 0; index < Worker_pool_statdef.get_value_count (); index++)
      {
	ss << "\t" << Worker_pool_statdef.get_value_name (index) << ": ";
	ss << statsp[index] << std::endl;
      }

    std::string str = ss.str ();
    _er_log_debug (ARG_FILE_LINE, str.c_str ());
  }

} // namespace cubthread
