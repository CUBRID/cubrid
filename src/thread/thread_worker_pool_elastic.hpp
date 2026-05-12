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
      using stats_base = typename worker_pool_impl<Stats>::stats_base;
      using stats = typename worker_pool_impl<Stats>::stats;

      // forward declaration
      class core_elastic;

      ~worker_pool_elastic ();

      // init
      void initialize (std::size_t pool_size, std::size_t core_count) override;

      // runtime variable parameter
      void adjust_runtime_parameter (std::size_t max_concurrency, std::size_t max_worker);

      // get the number of threads that can run concurrently in this worker pool
      std::size_t get_max_concurrency (void) const;
      std::size_t get_max_worker (void) const;

    private:
      worker_pool_elastic (std::size_t pool_size, std::size_t core_count, std::size_t max_concurrency, std::size_t max_worker,
			   const char *name, entry_manager &entry_mgr, bool pool_threads = false,
			   wait_seconds idle_timeout = std::chrono::seconds (5));

      std::unique_ptr<worker_pool::core> allocate_core (bool pool_threads) override;

      // variant worker pool (m_max_concurrency <= the number of workers <= m_max_worker)
      std::size_t m_max_concurrency;
      std::size_t m_max_worker;

      mutable std::mutex m_variant_mutex;
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
      // forward declaration
      class worker_elastic;

      ~core_elastic ();

      // init
      void initialize (std::size_t concurrency) override;

      // runtime variable parameter
      void adjust_runtime_parameter (std::size_t max_concurrency, std::size_t max_worker);

      // execute task
      void execute_task (task_type *task_p) override;

      // concurrency slot management
      unique_slot try_acquire_slot (bool has_mutex = true);
      unique_slot acquire_slot (bool has_mutex = true);

      void release_slot (unique_slot slot, bool has_mutex = true);

      std::optional<std::pair<wrapped_task, unique_slot>> get_task_and_slot_or_become_available (worker &worker_arg);

      void get_retire_if_excess (worker_elastic *w);

      // stats
      void get_stats (cubperf::stat_value *stats_out) const override;

    private:
      core_elastic (bool pool_threads);

      std::unique_ptr<worker> allocate_worker () override;

      worker *get_or_make_available_worker ();

      void try_execute_task_with_slot (worker_elastic *worker_p, wrapped_task &&task_ref, unique_slot slot);

      concurrency_slot_pool m_slots;

      // m_max_concurrency <= the number of workers <= m_max_workers
      std::size_t m_max_concurrency;
      std::size_t m_max_worker;

      std::size_t m_retire_threshold;

      stats_base m_retired_stats;
  };

  // worker_pool_elastic<Stats>::core_elastic::worker_elastic
  //
  // description
  //    worker implementation that carries a task together with a concurrency slot.
  //
  template <stats_t Stats>
  class worker_pool_elastic<Stats>::core_elastic::worker_elastic final
    : public worker_pool_impl<Stats>::core_impl::worker_impl
  {
      friend class core_elastic;

    public:
      ~worker_elastic ();

      // assign a task to the worker; wake a running thread or start a new one
      std::optional<std::pair<wrapped_task, unique_slot>> assign_task (wrapped_task &&task_ref, unique_slot slot);

    private:
      worker_elastic ();

      // run function invoked by spawned thread
      void run (void) override;

      // execute m_wrapped_task
      void execute_current_task (void) override;

      // get a new task and slot
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
  worker_pool_elastic<Stats>::worker_pool_elastic (std::size_t pool_size, std::size_t core_count,
      std::size_t max_concurrency, std::size_t max_worker, const char *name, entry_manager &entry_mgr, bool pool_threads,
      wait_seconds idle_timeout)
    : worker_pool_impl<Stats> (pool_size, core_count, name, entry_mgr, pool_threads, idle_timeout)
    , m_max_concurrency (max_concurrency)
    , m_max_worker (max_worker)
  {
  }

  template <stats_t Stats>
  worker_pool_elastic<Stats>::~worker_pool_elastic ()
  {
  }

  template <stats_t Stats>
  void
  worker_pool_elastic<Stats>::initialize (std::size_t pool_size, std::size_t core_count)
  {
    // pool_size is the entry reservation count; initial worker count comes from m_max_concurrency

    // initialize the base worker pool as worker is max_concurrency and core is core_count
    worker_pool_impl<Stats>::initialize (m_max_concurrency, core_count);

    // set the overcommit parameters in each core
    adjust_runtime_parameter (m_max_concurrency, m_max_worker);
  }

  template <stats_t Stats>
  void
  worker_pool_elastic<Stats>::adjust_runtime_parameter (std::size_t max_concurrency, std::size_t max_worker)
  {
    assert (max_concurrency > 0);
    assert (max_worker >= max_concurrency);

    std::size_t concurrency_quotient, concurrency_remainder;
    std::size_t worker_quotient, worker_remainder;
    std::size_t c, w;
    std::size_t it;

    concurrency_quotient = max_concurrency / this->m_cores.size ();
    concurrency_remainder = max_concurrency % this->m_cores.size ();
    worker_quotient = max_worker / this->m_cores.size ();
    worker_remainder = max_worker % this->m_cores.size ();

    for (it = 0; it < this->m_cores.size (); it++)
      {
	assert (dynamic_cast<core_elastic *> (this->m_cores[it].get ()));

	c = it < concurrency_remainder ? concurrency_quotient + 1 : concurrency_quotient;
	w = it < worker_remainder ? worker_quotient + 1 : worker_quotient;

	static_cast<core_elastic *> (this->m_cores[it].get ())->adjust_runtime_parameter (c, w);
      }

    std::lock_guard<std::mutex> lock (m_variant_mutex);

    m_max_concurrency = max_concurrency;
    m_max_worker = max_worker;
  }

  template <stats_t Stats>
  std::size_t
  worker_pool_elastic<Stats>::get_max_concurrency (void) const
  {
    std::lock_guard<std::mutex> lock (m_variant_mutex);

    return m_max_concurrency;
  }

  template <stats_t Stats>
  std::size_t
  worker_pool_elastic<Stats>::get_max_worker (void) const
  {
    std::lock_guard<std::mutex> lock (m_variant_mutex);

    return m_max_worker;
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
    , m_slots (this->m_core_mutex)
    , m_max_concurrency (0)
    , m_max_worker (0)
    , m_retire_threshold (0)
    , m_retired_stats (stats::create ())
  {
  }

  template <stats_t Stats>
  worker_pool_elastic<Stats>::core_elastic::~core_elastic ()
  {
    stats::destroy (m_retired_stats);
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
  worker_pool_elastic<Stats>::core_elastic::adjust_runtime_parameter (std::size_t max_concurrency,
      std::size_t max_worker)
  {
    assert (max_concurrency > 0);
    assert (max_worker >= max_concurrency);

    std::lock_guard<std::mutex> lock (this->m_core_mutex);

    m_max_concurrency = max_concurrency;
    m_max_worker = max_worker;
    m_retire_threshold = std::max ((max_concurrency + max_worker) / 2, max_concurrency);
  }

  template <stats_t Stats>
  void
  worker_pool_elastic<Stats>::core_elastic::execute_task (task_type *task_p)
  {
    assert (task_p != nullptr);

    worker_elastic *worker_p = nullptr;

    if (!this->m_parent_pool->is_running ())
      {
	// reject task
	task_p->retire ();
	return;
      }

    wrapped_task task_ref (task_p);
    std::unique_lock<std::mutex> ulock (this->m_core_mutex);

    unique_slot slot = try_acquire_slot ();
    if (slot)
      {
	worker_p = static_cast<worker_elastic *> (get_or_make_available_worker ());

	if (worker_p)
	  {
	    // preserve FIFO order when queued tasks already exist
	    if (!this->m_task_queue.empty ())
	      {
		// enqueue the new task behind existing work
		this->m_task_queue.push_back (std::move (task_ref));

		// dispatch the oldest queued task first
		wrapped_task queued_task = std::move (this->m_task_queue.front ());
		this->m_task_queue.pop_front ();

		ulock.unlock ();

		try_execute_task_with_slot (worker_p, std::move (queued_task), std::move (slot));
	      }
	    else
	      {
		ulock.unlock ();

		try_execute_task_with_slot (worker_p, std::move (task_ref), std::move (slot));
	      }

	    // successfully execute the task
	    return;
	  }

	// release the slot
	release_slot (std::move (slot));
      }

    // enqueue the task until a slot is available
    this->m_task_queue.push_back (std::move (task_ref));
  }

  template <stats_t Stats>
  typename worker_pool_elastic<Stats>::unique_slot
  worker_pool_elastic<Stats>::core_elastic::try_acquire_slot (bool has_mutex)
  {
    auto slot = m_slots.try_acquire_slot (has_mutex);
    assert (!slot || (slot->get_owner_pool () && slot->get_holder_pool ()));

    return slot;
  }

  template <stats_t Stats>
  typename worker_pool_elastic<Stats>::unique_slot
  worker_pool_elastic<Stats>::core_elastic::acquire_slot (bool has_mutex)
  {
    auto slot = m_slots.acquire_slot (has_mutex);
    assert (slot->get_owner_pool () && slot->get_holder_pool ());

    return slot;
  }

  template <stats_t Stats>
  void
  worker_pool_elastic<Stats>::core_elastic::release_slot (unique_slot slot, bool has_mutex)
  {
    m_slots.release_slot (std::move (slot), has_mutex);
  }

  template <stats_t Stats>
  std::optional<std::pair<typename worker_pool_elastic<Stats>::wrapped_task, typename worker_pool_elastic<Stats>::unique_slot>>
      worker_pool_elastic<Stats>::core_elastic::get_task_and_slot_or_become_available (worker &worker_arg)
  {
    std::lock_guard<std::mutex> lock (this->m_core_mutex);

    if (!this->m_task_queue.empty ())
      {
	unique_slot slot = try_acquire_slot ();
	if (slot)
	  {
	    wrapped_task queued_task = std::move (this->m_task_queue.front ());
	    this->m_task_queue.pop_front ();

	    return std::optional<std::pair<wrapped_task, unique_slot>> (std::in_place, std::move (queued_task), std::move (slot));
	  }
      }

    // add this worker to the available list
    this->m_available_workers.push_back (&worker_arg);
    assert (this->m_available_workers.size () <= this->m_workers.size ());

    return std::nullopt;
  }

  template <stats_t Stats>
  void
  worker_pool_elastic<Stats>::core_elastic::get_retire_if_excess (worker_elastic *w)
  {
    std::lock_guard<std::mutex> lock (this->m_core_mutex);

    if (this->m_workers.size () >= m_retire_threshold)
      {
	auto available_it = std::find (this->m_available_workers.begin (), this->m_available_workers.end (), w);
	if (available_it != this->m_available_workers.end ())
	  {
	    // not selected yet
	    this->m_available_workers.erase (available_it);

	    auto worker_it = std::find_if (this->m_workers.begin (), this->m_workers.end (),
					   [w] (const std::unique_ptr<worker> &ptr)
	    {
	      return ptr.get () == w;
	    });
	    assert (worker_it != this->m_workers.end ());

	    // collect the stats from the worker to be removed
	    stats::accumulate (w->m_stats, m_retired_stats);

	    // remove
	    this->m_workers.erase (worker_it);
	  }
      }
  }

  template <stats_t Stats>
  void
  worker_pool_elastic<Stats>::core_elastic::get_stats (cubperf::stat_value *stats_out) const
  {
    std::lock_guard<std::mutex> lock (this->m_core_mutex);

    for (const auto &it : this->m_workers)
      {
	it->get_stats (stats_out);
      }

    stats::accumulate (m_retired_stats, stats_out);
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
    if (!worker_p && this->m_workers.size () < m_max_worker)
      {
	// create a worker when none is available
	std::unique_ptr<worker> w = this->allocate_worker ();
	w->set_parent_core (*this);
	worker_p = w.get ();
	this->m_workers.push_back (std::move (w));
      }
    return worker_p;
  }

  template <stats_t Stats>
  void
  worker_pool_elastic<Stats>::core_elastic::try_execute_task_with_slot (worker_elastic *worker_p, wrapped_task &&task_ref,
      unique_slot slot)
  {
    auto unexecuted = worker_p->assign_task (std::move (task_ref), std::move (slot));
    if (unexecuted.has_value ())
      {
	// could not start a new thread; put the task and slot back
	std::lock_guard<std::mutex> lock (this->m_core_mutex);
	// requeue the task at the front to preserve FIFO order
	this->m_task_queue.push_front (std::move (unexecuted->first));
	// release the slot
	release_slot (std::move (unexecuted->second));
	// return the worker to the available list
	this->m_available_workers.push_back (worker_p);
      }
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

    // assign the task
    this->m_wrapped_task.emplace (std::move (task_ref));
    // assign the slot
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

	// wait for a task
	ulock.lock ();

	assert ((!this->m_wrapped_task.has_value () && !m_slot) || (this->m_wrapped_task.has_value () && m_slot));

	if ((!this->m_wrapped_task.has_value () && !m_slot) && !this->m_stop)
	  {
	    // wait for a task, a stop request, or idle timeout
	    if (this->m_persistent)
	      {
		condvar_wait (this->m_task_cv, ulock, cubthread::wait_seconds (),
			      [this] () -> bool { return (this->m_wrapped_task.has_value () && m_slot) || this->m_stop; });
	      }
	    else
	      {
		condvar_wait (this->m_task_cv, ulock, this->m_parent_core->get_parent_pool ()->get_idle_timeout (),
			      [this] () -> bool { return (this->m_wrapped_task.has_value () && m_slot) || this->m_stop; });
	      }
	  }
	else
	  {
	    // no need to wait
	  }
      }
    else
      {
	// keep this worker visible to the core while it is stopping
	static_cast<core_elastic *> (this->m_parent_core)->become_available (*this);

	ulock.lock ();
      }

    assert ((!this->m_wrapped_task.has_value () && !m_slot) || (this->m_wrapped_task.has_value () && m_slot));

    // does this worker have both a task and a slot ?
    if (!this->m_wrapped_task.has_value () && !m_slot)
      {
	// no task is available; future assignments must start a new thread
	this->m_has_thread = false;

	// retire the context before another thread reuses this worker
	this->finish_run ();

	return false;
      }
    else
      {
	// unlock the worker mutex
	ulock.unlock ();

	// sanity check: this worker should no longer be available
	static_cast<core_elastic *> (this->m_parent_core)->check_worker_not_available (*this);

	// stats: wake up with task
	stats::time_and_increment (this->m_stats, stats::id::wakeup_with_task);

	// found a task
	return true;
      }
  }

  template <stats_t Stats>
  void
  worker_pool_elastic<Stats>::core_elastic::worker_elastic::run (void)
  {
    worker_pool_impl<Stats>::core_impl::worker_impl::run ();

    // removed or not
    static_cast<core_elastic *> (this->m_parent_core)->get_retire_if_excess (this);
  }

  template <stats_t Stats>
  void
  worker_pool_elastic<Stats>::core_elastic::worker_elastic::execute_current_task (void)
  {
    assert (dynamic_cast<core_elastic *> (this->m_parent_core));
    assert (this->m_wrapped_task.has_value () && m_slot);
    assert (!this->m_context_p->m_slot);

    // move the slot to the thread entry for task execution
    this->m_context_p->m_slot = std::move (m_slot);
    // execute the task
    this->m_wrapped_task->execute (*this->m_context_p);

    // return the slot to the pool
    static_cast<core_elastic *> (this->m_parent_core)->release_slot (std::move (this->m_context_p->m_slot), false);
    this->m_context_p->m_slot = nullptr;

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
