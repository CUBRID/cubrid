/*
 *
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

#include <cstring>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace cubthread
{
  //////////////////////////////////////////////////////////////////////////
  // worker_pool implementation
  //////////////////////////////////////////////////////////////////////////

  worker_pool::worker_pool (std::size_t pool_size, std::size_t core_count, const char *name, entry_manager &entry_mgr,
			    bool pool_threads, wait_seconds wait_for_task_time)
    : m_name (name == NULL ? "" : name)
    , m_entry_manager (entry_mgr)
    , m_max_workers (pool_size)
    , m_round_robin_counter (0)
    , m_stopped (false)
    , m_pool_threads (pool_threads)
    , m_wait_for_task_time (wait_for_task_time)
  {
    assert (core_count > 0 && core_count <= pool_size);
  }

  worker_pool::~worker_pool ()
  {
    // not safe to destroy running pools
    assert (m_stopped);
  }

  const std::string &
  worker_pool::get_name (void) const
  {
    return m_name;
  }

  void
  worker_pool::initialize (std::size_t worker_count, std::size_t core_count)
  {
    allocate_cores (core_count);
    assign_workers_to_cores (worker_count);

    // [optional] this option must be useful using perf
    if (wp_is_thread_always_alive_forced ())
      {
	// override pooling/wait time options to keep threads always alive
	m_pool_threads = true;
	m_wait_for_task_time.set_infinite_wait ();
      }
  }

  void
  worker_pool::execute (task_type *work_arg)
  {
    execute_on_core (work_arg, get_next_core ());
  }

  void
  worker_pool::execute_on_core (task_type *work_arg, std::size_t core_hash, bool is_temp)
  {
    std::size_t core_index;

    core_index = core_hash % m_cores.size ();
    m_cores[core_index]->execute_task (work_arg, is_temp);
  }

  bool
  worker_pool::is_running (void) const
  {
    return !m_stopped;
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
	for (std::size_t it = 0; it < m_cores.size (); it++)
	  {
	    // notify all workers to stop. if any worker is still running, is_not_stopped = true is output
	    m_cores[it]->notify_stop (is_not_stopped);
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
    for (std::size_t it = 0; it < m_cores.size (); it++)
      {
	m_cores[it]->retire_queued_tasks ();
      }
  }

  std::size_t
  worker_pool::get_worker_count (void) const
  {
    return m_max_workers;
  }

  std::size_t
  worker_pool::get_core_count (void) const
  {
    return m_cores.size ();
  }

  std::unique_ptr<worker_pool::core>
  worker_pool::allocate_core (void)
  {
    return std::unique_ptr<core> (new core ());
  }

  void
  worker_pool::allocate_cores (std::size_t core_count)
  {
    std::size_t it;

    m_cores.reserve (core_count);
    for (it = 0; it < core_count; it++)
      {
	m_cores.push_back (allocate_core ());
      }
  }

  void
  worker_pool::assign_workers_to_cores (std::size_t worker_count)
  {
    std::size_t quotient, remainder;
    std::size_t it;

    quotient = worker_count / m_cores.size ();
    remainder = worker_count % m_cores.size ();

    for (it = 0; it < remainder; it++)
      {
	m_cores[it]->set_worker_pool (*this);
	// worker pool is referenced in worker to get m_pool_threads when initializing core
	m_cores[it]->initialize (quotient + 1);
      }
    for (; it < m_cores.size (); it++)
      {
	m_cores[it]->set_worker_pool (*this);
	// worker pool is referenced in worker to get m_pool_threads when initializing core
	m_cores[it]->initialize (quotient);
      }
  }

  std::size_t
  worker_pool::get_next_core (void)
  {
    return get_round_robin_core_hash ();
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
  {
  }

  worker_pool::core::~core ()
  {
  }

  void
  worker_pool::core::initialize (std::size_t worker_count)
  {
    assert (worker_count > 0);

    // resources reserve
    m_available_workers.reserve (worker_count);

    // workers
    allocate_workers (worker_count);
    initialize_workers ();
  }

  void
  worker_pool::core::set_worker_pool (worker_pool &parent)
  {
    m_parent_pool = &parent;
  }

  void
  worker_pool::core::execute_task (task_type *task_p, bool is_temp)
  {
    // find an available worker
    // 1. one already active is preferable
    // 2. inactive will do too
    // 3. if no workers, enqueue the task

    assert (task_p != NULL);

    worker *refp = NULL;

    if (m_parent_pool->m_stopped)
      {
	// reject task
	task_p->retire ();
	return;
      }

    std::unique_lock<std::mutex> ulock (m_workers_mutex);

    if (!m_available_workers.empty ())
      {
	refp = m_available_workers.back ();
	m_available_workers.pop_back ();
	ulock.unlock ();

	assert (refp != NULL);
	refp->assign_task (task_p);
      }
    else
      {
	if (is_temp)
	  {
	    // no need to hold the mutex (prevent deadlock)
	    ulock.unlock ();

	    execute_task_as_temp (task_p);
	  }
	else
	  {
	    // save to queue
	    m_task_queue.push (task_p);
	  }
      }
  }

  void
  worker_pool::core::notify_stop (bool &is_not_stopped)
  {
    // stop all temp workers first
    std::unique_lock<std::mutex> temp_guard (m_temp_workers_mutex);

    for (auto it = m_temp_workers.begin (); it != m_temp_workers.end (); it++)
      {
	(*it)->stop_execution (is_not_stopped);
      }
    temp_guard.unlock ();

    // tell all workers to stop
    std::unique_lock<std::mutex> worker_guard (m_workers_mutex);

    for (auto it = m_workers.begin (); it != m_workers.end (); it++)
      {
	(*it)->stop_execution (is_not_stopped);
      }
    worker_guard.unlock ();
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

    m_available_workers.push_back (&worker_arg);
    assert (m_available_workers.size () <= m_workers.size ());

    return NULL;
  }

  void
  worker_pool::core::become_available (worker &worker_arg)
  {
    std::unique_lock<std::mutex> ulock (m_workers_mutex);

    m_available_workers.push_back (&worker_arg);
    assert (m_available_workers.size () <= m_workers.size ());
  }

  void
  worker_pool::core::check_worker_not_available (const worker &worker_arg)
  {
#if !defined (NDEBUG)
    std::unique_lock<std::mutex> ulock (m_workers_mutex);

    for (auto it = m_available_workers.begin (); it != m_available_workers.end (); it++)
      {
	assert (*it != &worker_arg);
      }
#endif // DEBUG
  }

  entry_manager &
  worker_pool::core::get_entry_manager (void)
  {
    return m_parent_pool->m_entry_manager;
  }

  std::size_t
  worker_pool::core::get_worker_count (void) const
  {
    return m_workers.size ();
  }

  void
  worker_pool::core::register_free_temp_list (worker *w)
  {
    std::unique_lock<std::mutex> ulock (m_temp_workers_mutex);

    auto it = std::find_if (m_temp_workers.begin (), m_temp_workers.end (), [w] (const std::unique_ptr<worker> &p)
    {
      return p.get () == w;
    });

    assert (it != m_temp_workers.end ());

    m_free_temp_workers.push_back (std::move (*it));
    m_temp_workers.erase (it);
  }

  void
  worker_pool::core::free_all_temp_list ()
  {
    std::unique_lock<std::mutex> ulock (m_temp_workers_mutex);

    m_free_temp_workers.clear ();
  }

  std::unique_ptr<worker_pool::core::worker>
  worker_pool::core::allocate_worker (bool is_temp)
  {
    return std::unique_ptr<worker> (new worker (is_temp));
  }

  void
  worker_pool::core::allocate_workers (std::size_t worker_count)
  {
    std::size_t it;

    m_workers.reserve (worker_count);
    for (it = 0; it < worker_count; it++)
      {
	m_workers.push_back (allocate_worker ());
      }
  }

  void
  worker_pool::core::initialize_workers ()
  {
    for (auto it = m_workers.begin (); it != m_workers.end (); it++)
      {
	(*it)->set_core (*this);

	if (m_parent_pool->get_pool_threads ())
	  {
	    // assign task / start thread
	    // it will add itself to available workers
	    (*it)->assign_task (NULL);
	  }
	else
	  {
	    // add to available workers
	    m_available_workers.push_back (it->get ());
	  }
      }
  }

  void
  worker_pool::core::execute_task_as_temp (task_type *task_p)
  {
    auto w = allocate_worker (true);
    w->set_core (*this);

    std::lock_guard<std::mutex> ulock (m_temp_workers_mutex);

    m_temp_workers.push_back (std::move (w));
    m_temp_workers.back ()->assign_task (task_p);
  }

  //////////////////////////////////////////////////////////////////////////
  // worker_pool<Context>::core::worker
  //////////////////////////////////////////////////////////////////////////

  worker_pool::core::worker::worker (bool is_temp)
    : m_parent_core (NULL)
    , m_context_p (NULL)
    , m_task_p (NULL)
    , m_stop (false)
    , m_has_thread (false)
    , m_is_temp (is_temp)
  {
  }

  worker_pool::core::worker::~worker (void)
  {
  }

  void
  worker_pool::core::worker::set_core (core &parent)
  {
    m_parent_core = &parent;
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
  worker_pool::core::worker::assign_task (task_type *work_p)
  {
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
  worker_pool::core::worker::push_task_on_running_thread (task_type *work_p)
  {
    // must lock task mutex
    std::unique_lock<std::mutex> ulock (m_task_mutex);

    assert (work_p != NULL);
    // make sure worker is in a valid state
    assert (m_task_p == NULL);
    assert (m_context_p != NULL);

    // set task
    m_task_p = work_p;

    // mutex is not needed for notify
    ulock.unlock ();
    // notify waiting thread
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
	// this thread is still running
	is_not_stopped = true;
      }

    // stop worker
    m_stop = true;
    // mutex is not needed for notify
    ulock.unlock ();

    if (m_is_temp)
      {
	// not to notify one if it is for temp
	return;
      }
    m_task_cv.notify_one ();
  }

  void
  worker_pool::core::worker::run (void)
  {
    // clear the affinity at start
    os::resources::cpu::clearaffinity ();
    pthread_setname_np (pthread_self (), m_parent_core->get_parent_pool ()->get_name ().c_str ());

    // do stuff at the beginning like creating context
    init_run ();

    // do task and terminate if this is temp worker
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
  worker_pool::core::worker::init_run (void)
  {
    // safe-guard - we have a thread
    assert (m_has_thread);

#if !defined (NDEBUG)
    // safe-guard - threads should [no longer] be available
    if (!m_is_temp)
      {
	m_parent_core->check_worker_not_available (*this);
      }
#endif

    // a context is required
    m_context_p = &m_parent_core->get_entry_manager ().create_context ();
  }

  void
  worker_pool::core::worker::finish_run (void)
  {
    assert (m_task_p == NULL);
    assert (m_context_p != NULL);

    // retire context
    m_parent_core->get_entry_manager ().retire_context (*m_context_p);
    m_context_p = NULL;

    if (m_is_temp)
      {
	m_parent_core->register_free_temp_list (this);
      }
  }

  void
  worker_pool::core::worker::execute_current_task (void)
  {
    assert (m_task_p != NULL);

    // execute task
    m_task_p->execute (*m_context_p);

    // and retire task
    retire_current_task ();

    // and recycle context before getting another task
    m_parent_core->get_entry_manager ().recycle_context (*m_context_p);

    // notify core one task was finished
    if (m_is_temp == false)
      {
	m_parent_core->free_all_temp_list ();
      }
  }

  void
  worker_pool::core::worker::retire_current_task (void)
  {
    assert (m_task_p != NULL);

    // retire task
    m_task_p->retire ();
    m_task_p = NULL;
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
	return true;
      }
  }

  //////////////////////////////////////////////////////////////////////////
  // [optional] useful when using perf
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

} // namespace cubthread
