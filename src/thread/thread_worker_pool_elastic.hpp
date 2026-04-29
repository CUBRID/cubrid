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
 * thread_worker_pool_elastic.hpp
 */

#ifndef _THREAD_WORKER_POOL_ELASTIC_HPP_
#define _THREAD_WORKER_POOL_ELASTIC_HPP_

#if !defined (SERVER_MODE)
#error Wrong module
#endif // not SERVER_MODE

// same module include
#include "concurrency_slot.hpp"
#include "thread_daemon.hpp"
#include "thread_worker_pool_impl.hpp"

// cubrid includes
#include "error_manager.h"

// system includes
#include <chrono>
#include <memory>
#include <algorithm>

namespace cubthread
{
  // worker_pool_elastic<Stats>
  //
  // description
  //    worker pool that maintains target concurrency by spawning
  //    additional workers when existing workers enter a known wait
  //    (e.g., blocked on a transaction lock).
  //
  template <stats_t Stats>
  class worker_pool_elastic final : public worker_pool_impl<Stats>
  {
      // forward definition for nested core class
      friend class manager;

    public:
      using worker = worker_pool::core::worker;

      using task_type = worker_pool::task_type;
      using wrapped_task = typename worker_pool_impl<Stats>::wrapped_task;

      using unique_slot = std::unique_ptr<concurrency_slot>;
      using stats = typename worker_pool_impl<Stats>::stats;

      // forward definition
      class core_elastic;

      ~worker_pool_elastic ();

    private:
      worker_pool_elastic (std::size_t pool_size, std::size_t core_count, const char *name, entry_manager &entry_mgr,
			   bool pool_threads = false, wait_seconds idle_timeout = std::chrono::seconds (5));

      std::unique_ptr<worker_pool::core> allocate_core (bool pool_threads) override;
  };

  // worker_pool_elastic<Stats>::core_elastic
  //
  // description
  //    elastic worker-pool core with a per-core slot pool that limits runnable concurrency.
  //
  template <stats_t Stats>
  class worker_pool_elastic<Stats>::core_elastic final : public worker_pool_impl<Stats>::core_impl
  {
      friend class worker_pool_elastic;

    public:
      // forward definition
      class worker_elastic;

      ~core_elastic ();

      void initialize (std::size_t concurrency) override;

      // execute task
      void execute_task (task_type *task_p) override;

      // concurrency slot management
      unique_slot try_acquire_slot ();
      unique_slot acquire_slot ();

      void release_slot (unique_slot slot);

      std::optional<std::pair<wrapped_task, unique_slot>> get_task_and_slot_or_become_available (worker &worker_arg);

    private:
      core_elastic (bool pool_threads);

      std::unique_ptr<worker> allocate_worker () override;

      worker *get_or_make_available_worker ();

      concurrency_slot_pool m_slots;
  };

  // worker_pool_elastic<Stats>::core_elastic::worker_elastic
  //
  // description
  //    elastic worker-pool core with a per-core slot pool that limits runnable concurrency.
  //
  template <stats_t Stats>
  class worker_pool_elastic<Stats>::core_elastic::worker_elastic final
    : public worker_pool_impl<Stats>::core_impl::worker_impl
  {
      friend class core_elastic;

    public:
      ~worker_elastic ();

      // assign task to worker; wake a running thread or start a new one.
      std::optional<std::pair<wrapped_task, unique_slot>> assign_task (wrapped_task &&task_ref, unique_slot slot);

    private:
      worker_elastic ();

      // execute m_wrapped_task
      void execute_current_task (void) override;

      // get new task with slot
      bool get_new_task (void) override;

      // guarded by m_task_mutex
      unique_slot m_slot;
  };

} // namespace cubthread

namespace cubthread
{
  //////////////////////////////////////////////////////////////////////////
  // worker_pool_elastic<Stats>
  //////////////////////////////////////////////////////////////////////////

  template <stats_t Stats>
  worker_pool_elastic<Stats>::worker_pool_elastic (std::size_t pool_size, std::size_t core_count, const char *name,
      entry_manager &entry_mgr, bool pool_threads, wait_seconds idle_timeout)
    : worker_pool_impl<Stats> (pool_size, core_count, name, entry_mgr, pool_threads, idle_timeout)
  {
  }

  template <stats_t Stats>
  worker_pool_elastic<Stats>::~worker_pool_elastic ()
  {
  }

  template <stats_t Stats>
  std::unique_ptr<typename worker_pool::core>
  worker_pool_elastic<Stats>::allocate_core (bool pool_threads)
  {
    return std::unique_ptr<worker_pool::core> (new core_elastic (pool_threads));
  }

  //////////////////////////////////////////////////////////////////////////
  // worker_pool_elastic<Stats>::core_elastic
  //////////////////////////////////////////////////////////////////////////

  template <stats_t Stats>
  worker_pool_elastic<Stats>::core_elastic::core_elastic (bool pool_threads)
    : worker_pool_impl<Stats>::core_impl (pool_threads)
  {
  }

  template <stats_t Stats>
  worker_pool_elastic<Stats>::core_elastic::~core_elastic ()
  {
  }

  template <stats_t Stats>
  void
  worker_pool_elastic<Stats>::core_elastic::initialize (std::size_t concurrency)
  {
    worker_pool_impl<Stats>::core_impl::initialize (concurrency);

    m_slots.initialize (static_cast<void *> (this->m_parent_pool), concurrency);
  }

  template <stats_t Stats>
  void
  worker_pool_elastic<Stats>::core_elastic::execute_task (task_type *task_p)
  {
    // find an available worker
    // 1. one already active is preferable
    // 2. inactive will do too
    // 3. if no workers, enqueue the task

    assert (task_p != nullptr);

    std::unique_lock<std::mutex> ulock (this->m_workers_mutex, std::defer_lock);
    worker_elastic *worker_p = nullptr;

    if (!this->m_parent_pool->is_running ())
      {
	// reject task
	task_p->retire ();
	return;
      }

    wrapped_task task_ref (task_p);

    unique_slot slot = try_acquire_slot ();
    // hold the mutex here
    ulock.lock ();
    if (slot)
      {
	worker_p = static_cast<worker_elastic *> (get_or_make_available_worker ());
	assert (worker_p);

	ulock.unlock ();

	std::optional<std::pair<wrapped_task, unique_slot>> unexecuted =
		    worker_p->assign_task (std::move (task_ref), std::move (slot));
	if (!unexecuted.has_value ())
	  {
	    /* successfully assign the task */
	    return;
	  }

	// failed to start new thread
	// return the slot to the pool
	m_slots.release_slot (std::move (unexecuted->second));

	ulock.lock ();

	// save to queue
	this->m_task_queue.push (std::move (unexecuted->first));
	// return the worker
	this->m_available_workers.push_back (worker_p);
      }
    else
      {
	// save to queue
	this->m_task_queue.push (std::move (task_ref));
      }
  }

  template <stats_t Stats>
  typename worker_pool_elastic<Stats>::unique_slot
  worker_pool_elastic<Stats>::core_elastic::try_acquire_slot ()
  {
    return m_slots.try_acquire_slot ();
  }

  template <stats_t Stats>
  typename worker_pool_elastic<Stats>::unique_slot
  worker_pool_elastic<Stats>::core_elastic::acquire_slot ()
  {
    return m_slots.acquire_slot ();
  }

  template <stats_t Stats>
  void
  worker_pool_elastic<Stats>::core_elastic::release_slot (unique_slot slot)
  {
    m_slots.release_slot (std::move (slot));
  }

  template <stats_t Stats>
  std::optional<std::pair<typename worker_pool_elastic<Stats>::wrapped_task, typename worker_pool_elastic<Stats>::unique_slot>>
      worker_pool_elastic<Stats>::core_elastic::get_task_and_slot_or_become_available (worker &worker_arg)
  {
    std::unique_lock<std::mutex> ulock (this->m_workers_mutex, std::defer_lock);

    unique_slot slot = try_acquire_slot ();
    ulock.lock ();
    if (slot)
      {
	if (!this->m_task_queue.empty ())
	  {
	    wrapped_task queued_task = std::move (this->m_task_queue.front ());
	    this->m_task_queue.pop ();

	    return std::optional<std::pair<wrapped_task, unique_slot>> (std::in_place, std::move (queued_task), std::move (slot));
	  }

	// insert this worker into available list
	this->m_available_workers.push_back (&worker_arg);
	assert (this->m_available_workers.size () <= this->m_workers.size ());

	ulock.unlock ();

	// return the slot
	release_slot (std::move (slot));

	return std::nullopt;
      }

    // insert this worker into available list
    this->m_available_workers.push_back (&worker_arg);
    assert (this->m_available_workers.size () <= this->m_workers.size ());

    return std::nullopt;
  }

  template <stats_t Stats>
  std::unique_ptr<typename worker_pool::core::worker>
  worker_pool_elastic<Stats>::core_elastic::allocate_worker ()
  {
    return std::unique_ptr<worker> (new worker_elastic ());
  }

  template <stats_t Stats>
  typename worker_pool_elastic<Stats>::worker *
  worker_pool_elastic<Stats>::core_elastic::get_or_make_available_worker ()
  {
    worker *worker_p;

    worker_p = this->get_available_worker ();
    if (!worker_p)
      {
	// make a new worker
	std::unique_ptr<worker> w = this->allocate_worker ();
	w->set_parent_core (*this);
	worker_p = w.get ();
	this->m_workers.push_back (std::move (w));
      }
    return worker_p;
  }

  template <stats_t Stats>
  worker_pool_elastic<Stats>::core_elastic::worker_elastic::worker_elastic ()
    : m_slot (nullptr)
  {
  }

  template <stats_t Stats>
  worker_pool_elastic<Stats>::core_elastic::worker_elastic::~worker_elastic ()
  {
  }

  template <stats_t Stats>
  std::optional<std::pair<typename worker_pool_elastic<Stats>::wrapped_task, typename worker_pool_elastic<Stats>::unique_slot>>
      worker_pool_elastic<Stats>::core_elastic::worker_elastic::assign_task (wrapped_task &&task_ref, unique_slot slot)
  {
    std::unique_lock<std::mutex> ulock (this->m_task_mutex);

    assert (!this->m_wrapped_task.has_value ());

    // give the task
    this->m_wrapped_task.emplace (std::move (task_ref));
    // give the slot
    m_slot = std::move (slot);

    if (this->m_has_thread)
      {
	ulock.unlock ();

	// notify waiting thread
	this->m_task_cv.notify_one ();

	return std::nullopt;
      }

    assert (!this->m_has_thread);

    this->m_has_thread = true;

    ulock.unlock ();

    assert (this->m_context_p == nullptr);

    if (!this->start_thread ())
      {
	ulock.lock ();

	std::pair<wrapped_task, unique_slot> unexecuted (std::move (*this->m_wrapped_task), std::move (m_slot));
	this->m_wrapped_task = std::nullopt;
	m_slot = nullptr;

	this->m_has_thread = false;

	return unexecuted;
      }
    return std::nullopt;
  }

  template <stats_t Stats>
  bool
  worker_pool_elastic<Stats>::core_elastic::worker_elastic::get_new_task (void)
  {
    assert (!this->m_wrapped_task.has_value ());
    assert (dynamic_cast<core_elastic *> (this->m_parent_core));

    std::unique_lock<std::mutex> ulock (this->m_task_mutex, std::defer_lock);

    if (!this->m_stop)
      {
	std::optional<std::pair<wrapped_task, unique_slot>> queued =
		    static_cast<core_elastic *> (this->m_parent_core)->get_task_and_slot_or_become_available (*this);
	if (queued.has_value ())
	  {
	    // stats: found in queue
	    stats::time_and_increment (this->m_stats, stats::id::found_in_queue);

	    // it is safe to set here
	    this->m_wrapped_task.emplace (std::move (queued->first));
	    m_slot = std::move (queued->second);

	    return true;
	  }

	// wait for task
	ulock.lock ();

	assert ((!this->m_wrapped_task.has_value () && !m_slot) || (this->m_wrapped_task.has_value () && m_slot));

	if ((!this->m_wrapped_task.has_value () && !m_slot) && !this->m_stop)
	  {
	    // wait until a task is received or stopped ...
	    // ... or time out
	    condvar_wait (this->m_task_cv, ulock, this->m_parent_core->get_parent_pool ()->get_idle_timeout (),
			  [this] () -> bool { return (this->m_wrapped_task.has_value () && m_slot) || this->m_stop; });
	  }
	else
	  {
	    // no need to wait
	  }
      }
    else
      {
	// we need to add to available list
	static_cast<core_elastic *> (this->m_parent_core)->become_available (*this);

	ulock.lock ();
      }

    assert ((!this->m_wrapped_task.has_value () && !m_slot) || (this->m_wrapped_task.has_value () && m_slot));

    // does this worker have a task with slot ?
    if (!this->m_wrapped_task.has_value () && !m_slot)
      {
	// no; this thread will stop. from this point forward, if a new task is assigned, a new thread must be spawned
	this->m_has_thread = false;

	// we need to retire context before another thread uses this worker
	this->finish_run ();

	return false;
      }
    else
      {
	// unlock mutex
	ulock.unlock ();

	// safe-guard - threads should no longer be available
	static_cast<core_elastic *> (this->m_parent_core)->check_worker_not_available (*this);

	// stats: wake up with task
	stats::time_and_increment (this->m_stats, stats::id::wakeup_with_task);

	// found task
	return true;
      }
  }

  template <stats_t Stats>
  void
  worker_pool_elastic<Stats>::core_elastic::worker_elastic::execute_current_task (void)
  {
    assert (dynamic_cast<core_elastic *> (this->m_parent_core));
    assert (this->m_wrapped_task.has_value () && m_slot);
    assert (!this->m_context_p->slot);

    // give the slot to the entry
    this->m_context_p->slot = std::move (m_slot);
    // execute task
    this->m_wrapped_task->execute (*this->m_context_p);

    // return the slot to the pool
    static_cast<core_elastic *> (this->m_parent_core)->release_slot (std::move (this->m_context_p->slot));
    this->m_context_p->slot = nullptr;

    // stats: execute task
    stats::time_and_increment (this->m_stats, stats::id::execute_task);

    // and retire task
    this->retire_current_task ();

    // and recycle context before getting another task
    this->m_parent_core->get_entry_manager ().recycle_context (*this->m_context_p);
    // stats: context recycle
    stats::time_and_increment (this->m_stats, stats::id::recycle_context);
  }

} // namespace cubthread

#endif // _THREAD_WORKER_POOL_ELASTIC_HPP_
