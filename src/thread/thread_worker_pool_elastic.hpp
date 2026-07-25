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
#include <cstdint>
#include <memory>
#include <atomic>
#include <algorithm>

namespace cubthread
{
  // worker_pool_elastic<Stats>
  //
  // description
  //    worker pool that maintains a normal concurrency target and adds temporary slots and workers only when queued
  //    work remains but no task completes.
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

      // forward declarations
      class progress_tracker;
      class core_elastic;

      // pure policy helpers kept public for deterministic boundary tests
      static std::size_t calculate_regular_worker_limit (std::size_t max_concurrency, std::size_t max_worker);
      static std::size_t calculate_next_expansion_count (std::size_t current_expansion_count);
      static std::size_t calculate_active_regular_worker_count (std::size_t reserved_worker_count,
	  std::size_t retiring_worker_count, std::size_t blocking_continuation_worker_count,
	  std::size_t retiring_blocking_continuation_worker_count);

      ~worker_pool_elastic ();

      // init
      void initialize (std::size_t pool_size, std::size_t core_count) override;

      // runtime variable parameter
      void adjust_runtime_parameter (std::size_t max_concurrency, std::size_t max_worker);

      // get the number of threads that can run concurrently in this worker pool
      std::size_t get_max_concurrency (void) const;
      std::size_t get_max_worker (void) const;

      // true if any core still has work that could not be dispatched
      bool has_queued_tasks (void) const;

      void check_progress (void);

      void get_runtime_stats (UINT64 &total_slots, UINT64 &target_slots, UINT64 &busy_slots,
			      UINT64 &total_workers, UINT64 &target_workers, UINT64 &busy_workers) const;

    private:
      worker_pool_elastic (std::size_t pool_size, std::size_t core_count, std::size_t max_concurrency, std::size_t max_worker,
			   const char *name, entry_manager &entry_mgr, bool pool_threads = false,
			   wait_seconds idle_timeout = std::chrono::seconds (5));

      static constexpr std::size_t MAX_BLOCKING_CONTINUATION_WORKER_RESERVE = 32;
      // the slot daemon normally wakes every 50 ms. poll at the same cadence, but gate actual pool-wide capacity
      // changes to 500 ms so daemon wakeup jitter cannot multiply the growth rate.
      static constexpr std::chrono::milliseconds PROGRESS_CHECK_INTERVAL { 50 };
      static constexpr std::chrono::milliseconds CAPACITY_ADJUSTMENT_INTERVAL { 500 };
      static constexpr std::size_t MAX_CAPACITY_ADJUSTMENT_COUNT = 32;

      std::unique_ptr<worker_pool::core> allocate_core (bool pool_threads) override;

      // variant worker pool (placed in same cache line)
      std::atomic<std::size_t> m_current_worker;
      std::atomic<std::size_t> m_current_regular_worker;

      std::atomic<std::size_t> m_max_concurrency;
      // absolute hard cap and regular ceiling that preserves the blocking-continuation reserve
      std::atomic<std::size_t> m_max_worker;
      std::atomic<std::size_t> m_max_regular_worker;

      std::chrono::steady_clock::time_point m_next_progress_check;
      std::vector<progress_tracker> m_progress_trackers;
      std::vector<std::size_t> m_expansion_requests;
      std::vector<std::size_t> m_shrink_requests;
      std::vector<std::uint64_t> m_observed_completed_task_counts;
      std::size_t m_next_progress_core;
      std::size_t m_next_shrink_core;
      std::size_t m_next_expansion_count;
      std::chrono::steady_clock::time_point m_next_expansion_time;
      std::chrono::steady_clock::time_point m_next_shrink_time;
      std::atomic<bool> m_initialized;
  };

  // worker_pool_elastic<Stats>::progress_tracker
  //
  // description
  //    tracks stable stall and idle periods. The pool applies the global exponential ramp to requested adjustments.
  //
  template <stats_t Stats>
  class worker_pool_elastic<Stats>::progress_tracker
  {
    public:
      using clock = std::chrono::steady_clock;

      struct decision
      {
	bool request_expansion = false;
	bool request_shrink = false;
	bool stalled = false;
	bool idle = false;
      };

      explicit progress_tracker (clock::duration stall_timeout = CAPACITY_ADJUSTMENT_INTERVAL)
	: m_stall_timeout (stall_timeout)
	, m_no_progress_since ()
	, m_completed_task_count (0)
	, m_had_queued_task (false)
	, m_stalled (false)
	, m_idle (false)
	, m_initialized (false)
      {
      }

      decision observe (clock::time_point now, bool has_queued_task, std::uint64_t completed_task_count)
      {
	decision result;

	if (!m_initialized)
	  {
	    m_no_progress_since = now;
	    m_completed_task_count = completed_task_count;
	    m_had_queued_task = has_queued_task;
	    m_initialized = true;
	    return result;
	  }

	bool made_progress = completed_task_count != m_completed_task_count;
	bool queue_started = has_queued_task && !m_had_queued_task;
	bool queue_stopped = !has_queued_task && m_had_queued_task;
	m_completed_task_count = completed_task_count;
	m_had_queued_task = has_queued_task;

	if (!has_queued_task)
	  {
	    m_stalled = false;
	    if (queue_stopped || made_progress)
	      {
		m_no_progress_since = now;
		m_idle = false;
	      }
	    else if (now - m_no_progress_since >= m_stall_timeout)
	      {
		m_no_progress_since = now;
		m_idle = true;
		result.request_shrink = true;
	      }

	    result.idle = m_idle;
	    return result;
	  }

	m_idle = false;
	if (queue_started || made_progress)
	  {
	    m_no_progress_since = now;
	    m_stalled = false;
	    return result;
	  }

	if (now - m_no_progress_since >= m_stall_timeout)
	  {
	    m_no_progress_since = now;
	    m_stalled = true;
	    result.request_expansion = true;
	  }

	result.stalled = m_stalled;
	return result;
      }

      void reset (clock::time_point now, std::uint64_t completed_task_count)
      {
	m_no_progress_since = now;
	m_completed_task_count = completed_task_count;
	m_had_queued_task = false;
	m_stalled = false;
	m_idle = false;
	m_initialized = true;
      }

    private:
      clock::duration m_stall_timeout;
      clock::time_point m_no_progress_since;
      std::uint64_t m_completed_task_count;
      bool m_had_queued_task;
      bool m_stalled;
      bool m_idle;
      bool m_initialized;
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
      void adjust_runtime_parameter (std::size_t max_concurrency);
      void adjust_workers ();
      void adjust_workers (std::unique_lock<std::mutex> &ulock);
      typename progress_tracker::decision observe_progress (std::chrono::steady_clock::time_point now,
	  progress_tracker &tracker, std::uint64_t &completed_task_count);
      bool expand_for_stall (std::uint64_t completed_task_count);
      bool shrink_idle_capacity (void);
      std::uint64_t get_completed_task_count () const;

      // execute task
      void execute_task (task_type *task_p, task_submission_options options) override;
      bool has_queued_task (void) const;
      bool has_queued_task (std::unique_lock<std::mutex> &ulock);

      bool stop_execution (void) override;
      std::deque<wrapped_task> take_queued_tasks (void) override;

      // concurrency slot interface
      void release_slot (unique_slot slot);
      void release_slot (unique_slot slot, std::unique_lock<std::mutex> &ulock);

      std::optional<std::pair<wrapped_task, unique_slot>> get_task_and_slot_or_become_available (worker &worker_arg,
	  bool completed_task);

      void worker_thread_exited (worker_elastic *w);

      // stats
      void get_stats (cubperf::stat_value *stats_out) const override;
      void get_runtime_stats (UINT64 &total_slots, UINT64 &target_slots, INT64 &busy_slots,
			      UINT64 &total_workers, UINT64 &target_workers, UINT64 &busy_workers) const;

    private:
      using snapshot_guard = typename worker_pool_impl<Stats>::core_impl::snapshot_guard;

      enum class worker_reservation_mode
      {
	normal,
	stall_recovery
      };

      core_elastic (bool pool_threads, std::atomic<std::size_t> &current_worker,
		    std::atomic<std::size_t> &current_regular_worker, std::atomic<std::size_t> &max_worker,
		    std::atomic<std::size_t> &max_regular_worker);

      std::unique_ptr<worker> allocate_worker () override;

      unique_slot acquire_slot (task_admission admission, std::unique_lock<std::mutex> &ulock);
      bool reserve_available_worker (task_admission admission, worker_reservation_mode reservation_mode);
      worker_elastic *get_available_worker (task_admission admission);
      worker_elastic *get_or_make_available_worker (task_admission admission,
	  worker_reservation_mode reservation_mode);
      bool has_available_worker (task_admission admission) const;
      bool has_queued_task_unlocked (void) const;
      void enqueue_task (wrapped_task &&task_ref, bool front = false);
      wrapped_task dequeue_task (void);
      bool try_dispatch_queued_task (std::unique_lock<std::mutex> &ulock,
				     worker_reservation_mode reservation_mode);
      bool try_expand_for_queued_task (std::unique_lock<std::mutex> &ulock);
      bool request_worker_retirement (worker_elastic *w);
      void retire_available_excess_workers (std::unique_lock<std::mutex> &ulock);
      void wait_for_worker_retirements (std::unique_lock<std::mutex> &ulock);
      void erase_worker (worker_elastic *w, std::unique_lock<std::mutex> &ulock);

      std::size_t get_active_regular_worker_count (void) const;

      bool try_execute_task_with_slot (worker_elastic *worker_p, wrapped_task &&task_ref, unique_slot slot,
				       std::unique_lock<std::mutex> &ulock);

      concurrency_slot_pool m_slots;
      // FIFO is preserved within each class; progress-critical continuations are always selected before regular work.
      std::deque<wrapped_task> m_blocking_continuation_queue;

      // per core
      std::size_t m_max_concurrency;

      // global worker count, absolute hard cap, and regular-work cap
      std::atomic<std::size_t> &m_current_worker;
      std::atomic<std::size_t> &m_current_regular_worker;
      std::atomic<std::size_t> &m_max_worker;
      std::atomic<std::size_t> &m_max_regular_worker;

      // guarded by the core mutex
      std::size_t m_reserved_worker_count;
      std::size_t m_retiring_worker_count;
      std::size_t m_blocking_continuation_worker_count;
      std::size_t m_retiring_blocking_continuation_worker_count;
      std::condition_variable m_worker_retire_cv;
      std::size_t m_progress_worker_count;
      std::uint64_t m_completed_task_count;

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

      void on_thread_exit (void) override;
      std::size_t finish_active_thread (void);

      void before_task_execution (void) override;
      void after_task_execution (void) override;

      // get a new task and slot
      bool get_new_task (void) override;

      bool request_retire (void);
      bool can_retire (void) const;

      // guarded by m_task_mutex
      unique_slot m_slot;

      // accessed only by the worker thread
      bool m_completed_task;

      // set before the worker's first assignment and never changed. it must retire instead of consuming regular work
      // once the blocking-continuation queue is empty, preserving the pool's reserved emergency headroom.
      bool m_blocking_continuation_only;
      // request one immediate regular replacement after this continuation-only worker retires
      bool m_replace_with_regular_on_retire;

      std::atomic<bool> m_retire_requested;
  };

} // namespace cubthread

namespace cubthread
{
  //////////////////////////////////////////////////////////////////////////
  // worker_pool_elastic<Stats>
  //////////////////////////////////////////////////////////////////////////

  template <stats_t Stats>
  std::size_t
  worker_pool_elastic<Stats>::calculate_regular_worker_limit (std::size_t max_concurrency, std::size_t max_worker)
  {
    assert (max_worker >= max_concurrency);

    if (max_worker <= max_concurrency)
      {
	return max_worker;
      }

    std::size_t elastic_headroom = max_worker - max_concurrency;
    // split a small elastic range between both recovery paths and give the odd worker to the continuation reserve.
    std::size_t continuation_reserve = std::min (MAX_BLOCKING_CONTINUATION_WORKER_RESERVE,
				       elastic_headroom / 2 + elastic_headroom % 2);

    // Regular work may grow lazily up to the hard cap, except for the headroom reserved for progress-critical
    // continuations. Growth is governed separately by a pool-wide bounded ramp, so this limit is only a ceiling.
    return max_worker - continuation_reserve;
  }

  template <stats_t Stats>
  std::size_t
  worker_pool_elastic<Stats>::calculate_next_expansion_count (std::size_t current_expansion_count)
  {
    if (current_expansion_count == 0)
      {
	return 1;
      }
    if (current_expansion_count >= MAX_CAPACITY_ADJUSTMENT_COUNT)
      {
	return MAX_CAPACITY_ADJUSTMENT_COUNT;
      }
    return std::min (current_expansion_count * 2, MAX_CAPACITY_ADJUSTMENT_COUNT);
  }

  template <stats_t Stats>
  std::size_t
  worker_pool_elastic<Stats>::calculate_active_regular_worker_count (std::size_t reserved_worker_count,
      std::size_t retiring_worker_count, std::size_t blocking_continuation_worker_count,
      std::size_t retiring_blocking_continuation_worker_count)
  {
    assert (reserved_worker_count >= retiring_worker_count);
    assert (reserved_worker_count >= blocking_continuation_worker_count);
    assert (retiring_worker_count >= retiring_blocking_continuation_worker_count);
    assert (blocking_continuation_worker_count >= retiring_blocking_continuation_worker_count);

    std::size_t reserved_regular_worker_count = reserved_worker_count - blocking_continuation_worker_count;
    std::size_t retiring_regular_worker_count = retiring_worker_count - retiring_blocking_continuation_worker_count;
    assert (reserved_regular_worker_count >= retiring_regular_worker_count);
    return reserved_regular_worker_count - retiring_regular_worker_count;
  }

  template <stats_t Stats>
  worker_pool_elastic<Stats>::worker_pool_elastic (std::size_t pool_size, std::size_t core_count,
      std::size_t max_concurrency, std::size_t max_worker, const char *name, entry_manager &entry_mgr, bool pool_threads,
      wait_seconds idle_timeout)
    : worker_pool_impl<Stats> (pool_size, core_count, name, entry_mgr, pool_threads, idle_timeout)
    , m_current_worker (max_concurrency)
    , m_current_regular_worker (max_concurrency)
    , m_max_concurrency (max_concurrency)
    , m_max_worker (max_worker)
    , m_max_regular_worker (calculate_regular_worker_limit (max_concurrency, max_worker))
    , m_next_progress_check (progress_tracker::clock::now () + PROGRESS_CHECK_INTERVAL)
    , m_progress_trackers (core_count)
    , m_expansion_requests (core_count, 0)
    , m_shrink_requests (core_count, 0)
    , m_observed_completed_task_counts (core_count, 0)
    , m_next_progress_core (0)
    , m_next_shrink_core (0)
    , m_next_expansion_count (1)
    , m_next_expansion_time (progress_tracker::clock::now ())
    , m_next_shrink_time (progress_tracker::clock::now ())
    , m_initialized (false)
  {
  }

  template <stats_t Stats>
  worker_pool_elastic<Stats>::~worker_pool_elastic ()
  {
    m_initialized.store (false, std::memory_order_release);

    // a daemon traversal holds the publisher lock while using subscriber and pool pointers. unsubscribe every core
    // before the base destructor starts releasing core storage; deactivate () waits for an in-flight traversal.
    for (const auto &core : this->m_cores)
      {
	static_cast<core_elastic *> (core.get ())->m_slots.deactivate ();
      }
  }

  template <stats_t Stats>
  void
  worker_pool_elastic<Stats>::initialize (std::size_t pool_size, std::size_t core_count)
  {
    // pool_size is the entry reservation count; initial worker count comes from m_max_concurrency

    // initialize the base worker pool as worker is max_concurrency and core is core_count
    worker_pool_impl<Stats>::initialize (m_max_concurrency.load (), core_count);

    // set the overcommit parameters in each core
    adjust_runtime_parameter (m_max_concurrency.load (), m_max_worker.load ());

    // Cores subscribe while they initialize. Do not let the daemon inspect the pool as a whole until every core and
    // progress tracker has reached its initial state.
    m_initialized.store (true, std::memory_order_release);
  }

  template <stats_t Stats>
  void
  worker_pool_elastic<Stats>::adjust_runtime_parameter (std::size_t max_concurrency, std::size_t max_worker)
  {
    assert (max_concurrency > 0);
    assert (max_worker >= max_concurrency);

    std::size_t concurrency_quotient, concurrency_remainder;
    std::size_t concurrency;
    std::size_t it;

    m_max_concurrency.store (max_concurrency);
    m_max_worker.store (max_worker);
    m_max_regular_worker.store (calculate_regular_worker_limit (max_concurrency, max_worker));

    concurrency_quotient = max_concurrency / this->m_cores.size ();
    concurrency_remainder = max_concurrency % this->m_cores.size ();

    for (it = 0; it < this->m_cores.size (); it++)
      {
	assert (dynamic_cast<core_elastic *> (this->m_cores[it].get ()));

	concurrency = it < concurrency_remainder ? concurrency_quotient + 1 : concurrency_quotient;
	core_elastic *core = static_cast<core_elastic *> (this->m_cores[it].get ());
	core->adjust_runtime_parameter (concurrency);
	m_progress_trackers[it].reset (progress_tracker::clock::now (), core->get_completed_task_count ());
      }

    auto now = progress_tracker::clock::now ();
    std::fill (m_expansion_requests.begin (), m_expansion_requests.end (), 0);
    std::fill (m_shrink_requests.begin (), m_shrink_requests.end (), 0);
    m_next_expansion_count = 1;
    m_next_expansion_time = now;
    m_next_shrink_time = now;

    // Shrinking cores above released their idle worker reservations. Dispatch only after every core has applied its
    // new target, so a core that grows earlier in index order cannot be starved by a core that shrinks later.
    for (it = 0; it < this->m_cores.size (); ++it)
      {
	static_cast<core_elastic *> (this->m_cores[it].get ())->adjust_workers ();
      }
  }

  template <stats_t Stats>
  std::size_t
  worker_pool_elastic<Stats>::get_max_concurrency (void) const
  {
    return m_max_concurrency.load ();
  }

  template <stats_t Stats>
  std::size_t
  worker_pool_elastic<Stats>::get_max_worker (void) const
  {
    return m_max_worker.load ();
  }

  template <stats_t Stats>
  bool
  worker_pool_elastic<Stats>::has_queued_tasks (void) const
  {
    if (!this->is_running ())
      {
	return false;
      }

    for (const auto &core : this->m_cores)
      {
	if (static_cast<const core_elastic *> (core.get ())->has_queued_task ())
	  {
	    return true;
	  }
      }
    return false;
  }

  template <stats_t Stats>
  void
  worker_pool_elastic<Stats>::check_progress (void)
  {
    if (!m_initialized.load (std::memory_order_acquire))
      {
	return;
      }

    auto now = progress_tracker::clock::now ();

    if (now < m_next_progress_check)
      {
	return;
      }
    m_next_progress_check = now + PROGRESS_CHECK_INTERVAL;

    bool has_stalled_core = false;
    for (std::size_t index = 0; index < this->m_cores.size (); ++index)
      {
	auto decision = static_cast<core_elastic *> (this->m_cores[index].get ())->observe_progress (
				now, m_progress_trackers[index], m_observed_completed_task_counts[index]);

	if (decision.request_expansion)
	  {
	    m_expansion_requests[index] = 1;
	  }
	else if (!decision.stalled)
	  {
	    m_expansion_requests[index] = 0;
	  }

	if (decision.request_shrink)
	  {
	    m_shrink_requests[index] = 1;
	  }
	else if (!decision.idle)
	  {
	    m_shrink_requests[index] = 0;
	  }

	has_stalled_core = has_stalled_core || decision.stalled;
      }

    std::size_t core_count = this->m_cores.size ();
    if (core_count == 0)
      {
	return;
      }

    // Reclaim only capacity that is observably idle. Each successful step removes one unused target slot and asks an
    // excess regular worker to retire asynchronously; running expanded work is therefore never cut back underneath it.
    bool has_shrink_request = std::any_of (m_shrink_requests.begin (), m_shrink_requests.end (),
					   [] (std::size_t request)
    {
      return request != 0;
    });
    if (has_shrink_request && now >= m_next_shrink_time)
      {
	std::size_t shrink_count = 0;
	bool made_adjustment = true;
	while (shrink_count < MAX_CAPACITY_ADJUSTMENT_COUNT && made_adjustment)
	  {
	    made_adjustment = false;
	    for (std::size_t offset = 0; offset < core_count && shrink_count < MAX_CAPACITY_ADJUSTMENT_COUNT; ++offset)
	      {
		std::size_t index = (m_next_shrink_core + offset) % core_count;
		if (m_shrink_requests[index] == 0)
		  {
		    continue;
		  }

		if (static_cast<core_elastic *> (this->m_cores[index].get ())->shrink_idle_capacity ())
		  {
		    ++shrink_count;
		    made_adjustment = true;
		  }
		else
		  {
		    m_shrink_requests[index] = 0;
		  }
	      }
	  }

	std::fill (m_shrink_requests.begin (), m_shrink_requests.end (), 0);
	if (shrink_count > 0)
	  {
	    m_next_shrink_time = now + CAPACITY_ADJUSTMENT_INTERVAL;
	    m_next_shrink_core = (m_next_shrink_core + 1) % core_count;
	  }
      }

    if (!has_stalled_core)
      {
	m_next_expansion_count = 1;
	m_next_expansion_time = now;
      }

    bool has_expansion_request = std::any_of (m_expansion_requests.begin (), m_expansion_requests.end (),
				 [] (std::size_t request)
    {
      return request != 0;
    });
    if (has_expansion_request && now >= m_next_expansion_time)
      {
	std::size_t expansion_count = 0;
	bool made_adjustment = true;
	while (expansion_count < m_next_expansion_count && made_adjustment)
	  {
	    made_adjustment = false;
	    for (std::size_t offset = 0; offset < core_count && expansion_count < m_next_expansion_count; ++offset)
	      {
		std::size_t index = (m_next_progress_core + offset) % core_count;
		if (m_expansion_requests[index] == 0)
		  {
		    continue;
		  }

		if (static_cast<core_elastic *> (this->m_cores[index].get ())->expand_for_stall (
			    m_observed_completed_task_counts[index]))
		  {
		    ++expansion_count;
		    made_adjustment = true;
		  }
		else
		  {
		    // Do not retry a core in this epoch after its queue, progress snapshot, or capacity changed.
		    m_expansion_requests[index] = 0;
		  }
	      }
	  }

	std::fill (m_expansion_requests.begin (), m_expansion_requests.end (), 0);
	if (expansion_count > 0)
	  {
	    m_next_expansion_count = calculate_next_expansion_count (m_next_expansion_count);
	    m_next_expansion_time = now + CAPACITY_ADJUSTMENT_INTERVAL;
	    m_next_progress_core = (m_next_progress_core + 1) % core_count;
	  }
      }
  }

  template <stats_t Stats>
  void
  worker_pool_elastic<Stats>::get_runtime_stats (UINT64 &total_slots, UINT64 &target_slots, UINT64 &busy_slots,
      UINT64 &total_workers, UINT64 &target_workers, UINT64 &busy_workers) const
  {
    INT64 signed_busy_slots = 0;

    total_slots = 0;
    target_slots = 0;
    total_workers = 0;
    target_workers = 0;
    busy_workers = 0;

    for (auto &it : this->m_cores)
      {
	assert (dynamic_cast<core_elastic *> (it.get ()));

	static_cast<core_elastic *> (it.get ())->get_runtime_stats (
		total_slots,
		target_slots,
		signed_busy_slots,
		total_workers,
		target_workers,
		busy_workers
	);
      }

    busy_slots = signed_busy_slots >= 0 ? signed_busy_slots : 0;
  }

  template <stats_t Stats>
  std::unique_ptr<typename worker_pool::core>
  worker_pool_elastic<Stats>::allocate_core (bool pool_threads)
  {
    return std::unique_ptr<worker_pool::core> (
		   new core_elastic (pool_threads, m_current_worker, m_current_regular_worker, m_max_worker,
				     m_max_regular_worker));
  }

  //////////////////////////////////////////////////////////////////////////
  // worker_pool_elastic<Stats>::core_elastic
  //////////////////////////////////////////////////////////////////////////

  template <stats_t Stats>
  worker_pool_elastic<Stats>::core_elastic::core_elastic (bool pool_threads, std::atomic<std::size_t> &current_worker,
      std::atomic<std::size_t> &current_regular_worker, std::atomic<std::size_t> &max_worker,
      std::atomic<std::size_t> &max_regular_worker)
    : worker_pool_impl<Stats>::core_impl (pool_threads)
    , m_slots (this, this->m_core_mutex)
    , m_max_concurrency (0)
    , m_current_worker (current_worker)
    , m_current_regular_worker (current_regular_worker)
    , m_max_worker (max_worker)
    , m_max_regular_worker (max_regular_worker)
    , m_reserved_worker_count (0)
    , m_retiring_worker_count (0)
    , m_blocking_continuation_worker_count (0)
    , m_retiring_blocking_continuation_worker_count (0)
    , m_progress_worker_count (0)
    , m_completed_task_count (0)
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
    // pooled threads may run as soon as the base initializer starts them. publish their real retirement target first.
    // otherwise they observe the constructor's zero target and immediately retire themselves.
    m_max_concurrency = concurrency;
    m_reserved_worker_count = concurrency;
    worker_pool_impl<Stats>::core_impl::initialize (concurrency);

    m_slots.initialize (static_cast<void *> (this->m_parent_pool), concurrency);
  }

  template <stats_t Stats>
  void
  worker_pool_elastic<Stats>::core_elastic::adjust_runtime_parameter (std::size_t max_concurrency)
  {
    assert (max_concurrency > 0);

    std::unique_lock<std::mutex> ulock (this->m_core_mutex);

    m_max_concurrency = max_concurrency;
    m_progress_worker_count = 0;

    m_slots.adjust_concurrency (m_max_concurrency, ulock);
    retire_available_excess_workers (ulock);
    wait_for_worker_retirements (ulock);
  }


  template <stats_t Stats>
  void
  worker_pool_elastic<Stats>::core_elastic::adjust_workers ()
  {
    std::unique_lock<std::mutex> ulock (this->m_core_mutex);

    adjust_workers (ulock);
  }

  template <stats_t Stats>
  void
  worker_pool_elastic<Stats>::core_elastic::adjust_workers (std::unique_lock<std::mutex> &ulock)
  {
    assert (ulock.owns_lock ());

    if (!this->m_parent_pool->is_running ())
      {
	return;
      }

    while (has_queued_task_unlocked () && // the tasks exist in queue
	   (m_slots.available_slots () > 0 || !m_blocking_continuation_queue.empty ()))
      {
	if (!try_dispatch_queued_task (ulock, worker_reservation_mode::normal))
	  {
	    break;
	  }
      }
  }

  template <stats_t Stats>
  typename worker_pool_elastic<Stats>::progress_tracker::decision
  worker_pool_elastic<Stats>::core_elastic::observe_progress (std::chrono::steady_clock::time_point now,
      progress_tracker &tracker, std::uint64_t &completed_task_count)
  {
    std::unique_lock<std::mutex> ulock (this->m_core_mutex);

    if (!this->m_parent_pool->is_running ())
      {
	return typename progress_tracker::decision {};
      }

    completed_task_count = m_completed_task_count;
    return tracker.observe (now, has_queued_task_unlocked (), m_completed_task_count);
  }

  template <stats_t Stats>
  bool
  worker_pool_elastic<Stats>::core_elastic::expand_for_stall (std::uint64_t completed_task_count)
  {
    std::unique_lock<std::mutex> ulock (this->m_core_mutex);

    if (!this->m_parent_pool->is_running () || !has_queued_task_unlocked ()
	|| m_completed_task_count != completed_task_count)
      {
	return false;
      }

    return try_expand_for_queued_task (ulock);
  }

  template <stats_t Stats>
  bool
  worker_pool_elastic<Stats>::core_elastic::shrink_idle_capacity (void)
  {
    std::unique_lock<std::mutex> ulock (this->m_core_mutex);

    if (!this->m_parent_pool->is_running () || has_queued_task_unlocked () || m_progress_worker_count == 0
	|| m_slots.available_slots () == 0)
      {
	return false;
      }

    --m_progress_worker_count;
    m_slots.adjust_concurrency (m_max_concurrency + m_progress_worker_count, ulock);
    retire_available_excess_workers (ulock);
    return true;
  }

  template <stats_t Stats>
  std::uint64_t
  worker_pool_elastic<Stats>::core_elastic::get_completed_task_count () const
  {
    std::unique_lock<std::mutex> ulock (this->m_core_mutex);

    return m_completed_task_count;
  }

  template <stats_t Stats>
  void
  worker_pool_elastic<Stats>::core_elastic::execute_task (task_type *task_p, task_submission_options options)
  {
    assert (task_p != nullptr);

    wrapped_task task_ref (task_p, options);
    std::unique_lock<std::mutex> ulock (this->m_core_mutex);

    if (!this->m_parent_pool->is_running ())
      {
	// this task belongs to a submission admitted before shutdown closed the gate. leave retirement to the shutdown
	// queue pass, after the submitter has released its admission count.
	enqueue_task (std::move (task_ref));
	return;
      }

    // blocking continuations are kept in their own FIFO. the capacity granted by their admission is therefore
    // consumed by a continuation itself, rather than by unrelated regular work that may block its caller.
    enqueue_task (std::move (task_ref));
    (void) try_dispatch_queued_task (ulock, worker_reservation_mode::normal);
  }

  template <stats_t Stats>
  bool
  worker_pool_elastic<Stats>::core_elastic::stop_execution (void)
  {
    bool has_running_workers = worker_pool_impl<Stats>::core_impl::stop_execution ();

    // synchronize with the final worker exit hook. retiring worker decrements its active-thread count before it can
    // wait for snapshot readers and erase itself, so the retirement count must also participate in shutdown.
    std::lock_guard<std::mutex> guard (this->m_core_mutex);
    return has_running_workers || m_retiring_worker_count > 0;
  }

  template <stats_t Stats>
  bool
  worker_pool_elastic<Stats>::core_elastic::has_queued_task (void) const
  {
    std::lock_guard<std::mutex> guard (this->m_core_mutex);
    return has_queued_task_unlocked ();
  }

  template <stats_t Stats>
  bool
  worker_pool_elastic<Stats>::core_elastic::has_queued_task (std::unique_lock<std::mutex> &ulock)
  {
    assert (ulock.owns_lock ());

    return has_queued_task_unlocked ();
  }

  template <stats_t Stats>
  std::deque<typename worker_pool_elastic<Stats>::wrapped_task>
  worker_pool_elastic<Stats>::core_elastic::take_queued_tasks (void)
  {
    std::deque<wrapped_task> queued_tasks;

    std::lock_guard<std::mutex> lock (this->m_core_mutex);

    queued_tasks.swap (m_blocking_continuation_queue);
    while (!this->m_task_queue.empty ())
      {
	queued_tasks.push_back (std::move (this->m_task_queue.front ()));
	this->m_task_queue.pop_front ();
      }
    return queued_tasks;
  }

  template <stats_t Stats>
  void
  worker_pool_elastic<Stats>::core_elastic::release_slot (unique_slot slot)
  {
    m_slots.release_slot (std::move (slot));
  }

  template <stats_t Stats>
  void
  worker_pool_elastic<Stats>::core_elastic::release_slot (unique_slot slot, std::unique_lock<std::mutex> &ulock)
  {
    m_slots.release_slot (std::move (slot), ulock);
  }

  template <stats_t Stats>
  std::optional<std::pair<typename worker_pool_elastic<Stats>::wrapped_task, typename worker_pool_elastic<Stats>::unique_slot>>
      worker_pool_elastic<Stats>::core_elastic::get_task_and_slot_or_become_available (worker &worker_arg,
	  bool completed_task)
  {
    std::unique_lock<std::mutex> ulock (this->m_core_mutex);

    if (completed_task)
      {
	++m_completed_task_count;
      }

    worker_elastic *worker_p = static_cast<worker_elastic *> (&worker_arg);

    if (!this->m_parent_pool->is_running ())
      {
	this->m_available_workers.push_back (&worker_arg);
	assert (this->m_available_workers.size () <= this->m_workers.size ());
	return std::nullopt;
      }

    bool has_blocking_continuation = !m_blocking_continuation_queue.empty ();
    std::size_t target_worker_count = m_max_concurrency + m_progress_worker_count;
    bool is_excess_worker = get_active_regular_worker_count () > target_worker_count;
    if (!has_blocking_continuation && (worker_p->m_blocking_continuation_only || is_excess_worker)
	&& worker_p->can_retire ())
      {
	worker_p->m_replace_with_regular_on_retire = worker_p->m_blocking_continuation_only
	    && !this->m_task_queue.empty ();
	(void) request_worker_retirement (worker_p);
	return std::nullopt;
      }

    if (has_queued_task_unlocked ())
      {
	wrapped_task queued_task = dequeue_task ();
	task_admission admission = queued_task.get_admission ();
	unique_slot slot = acquire_slot (admission, ulock);
	if (slot)
	  {
	    return std::optional<std::pair<wrapped_task, unique_slot>> (std::in_place, std::move (queued_task), std::move (slot));
	  }

	enqueue_task (std::move (queued_task), true);
      }

    // add this worker to the available list
    this->m_available_workers.push_back (&worker_arg);
    assert (this->m_available_workers.size () <= this->m_workers.size ());

    return std::nullopt;
  }

  template <stats_t Stats>
  void
  worker_pool_elastic<Stats>::core_elastic::worker_thread_exited (worker_elastic *w)
  {
    std::unique_lock<std::mutex> ulock (this->m_core_mutex);

    std::size_t active_thread_count = w->finish_active_thread ();
    if (active_thread_count > 0)
      {
	// replacement thread was started while this thread was retiring its context.
	return;
      }

    bool retire_requested = w->m_retire_requested.load (std::memory_order_acquire);
    auto available_it = std::find (this->m_available_workers.begin (), this->m_available_workers.end (), w);

    if (!retire_requested)
      {
	// if the worker was claimed while the old detached thread was returning, its replacement owns the object now.
	std::size_t target_worker_count = m_max_concurrency + m_progress_worker_count;
	bool is_excess_worker = get_active_regular_worker_count () > target_worker_count;
	if (available_it == this->m_available_workers.end () || !w->can_retire ()
	    || (!w->m_blocking_continuation_only && !is_excess_worker))
	  {
	    return;
	  }

	w->m_replace_with_regular_on_retire = w->m_blocking_continuation_only
					      && m_blocking_continuation_queue.empty () && !this->m_task_queue.empty ();
	(void) request_worker_retirement (w);
	retire_requested = true;
      }

    if (available_it != this->m_available_workers.end ())
      {
	this->m_available_workers.erase (available_it);
      }

    assert (retire_requested);
    erase_worker (w, ulock);
  }

  template <stats_t Stats>
  void
  worker_pool_elastic<Stats>::core_elastic::get_stats (cubperf::stat_value *stats_out) const
  {
    std::unique_lock<std::mutex> ulock (this->m_core_mutex);

    stats::accumulate (m_retired_stats, stats_out);

    snapshot_guard snapshot (this, ulock);
    ulock.unlock ();

    for (const auto &it : snapshot.get_snapshot ())
      {
	it->get_stats (stats_out);
      }
  }

  template <stats_t Stats>
  void
  worker_pool_elastic<Stats>::core_elastic::get_runtime_stats (UINT64 &total_slots, UINT64 &target_slots,
      INT64 &busy_slots, UINT64 &total_workers, UINT64 &target_workers, UINT64 &busy_workers) const
  {
    std::unique_lock<std::mutex> ulock (this->m_core_mutex);

    m_slots.get_runtime_stats (total_slots, target_slots, busy_slots);
    assert (this->m_workers.size () >= this->m_available_workers.size ());
    total_workers += this->m_workers.size ();
    target_workers += m_max_concurrency + m_progress_worker_count;
    busy_workers += this->m_workers.size () - this->m_available_workers.size ();
  }

  template <stats_t Stats>
  std::unique_ptr<typename worker_pool::core::worker>
  worker_pool_elastic<Stats>::core_elastic::allocate_worker ()
  {
    return std::unique_ptr<worker> (new worker_elastic ());
  }

  template <stats_t Stats>
  typename worker_pool_elastic<Stats>::unique_slot
  worker_pool_elastic<Stats>::core_elastic::acquire_slot (task_admission admission,
      std::unique_lock<std::mutex> &ulock)
  {
    // both acquisition paths must keep ulock continuously held. dispatch removes a task from its queue before
    // calling this helper and relies on this contract to restore the task safely if no slot is available.
    unique_slot slot = m_slots.try_acquire_slot (ulock);
    if (!slot && admission == task_admission::blocking_continuation)
      {
	slot = m_slots.acquire_temporary_slot (ulock);
      }
    return slot;
  }

  template <stats_t Stats>
  bool
  worker_pool_elastic<Stats>::core_elastic::reserve_available_worker (task_admission admission,
      worker_reservation_mode reservation_mode)
  {
    // the regular target is a steady-state limit and may temporarily trail active workers after asynchronous shrink.
    // only a controller-issued stall recovery dispatch may bypass it; the pool-wide ceilings below always apply.
    if (admission == task_admission::regular
	&& reservation_mode == worker_reservation_mode::normal
	&& get_active_regular_worker_count () >= m_max_concurrency + m_progress_worker_count)
      {
	return false;
      }

    std::size_t expected = m_current_worker.load ();
    do
      {
	if (expected >= m_max_worker.load ())
	  {
	    return false;
	  }
      }
    while (!m_current_worker.compare_exchange_strong (expected, expected + 1));

    if (admission == task_admission::regular)
      {
	expected = m_current_regular_worker.load ();
	do
	  {
	    std::size_t regular_worker_limit = std::min (m_max_regular_worker.load (), m_max_worker.load ());
	    if (expected >= regular_worker_limit)
	      {
		m_current_worker.fetch_sub (1);
		return false;
	      }
	  }
	while (!m_current_regular_worker.compare_exchange_strong (expected, expected + 1));
      }

    ++m_reserved_worker_count;
    if (admission == task_admission::blocking_continuation)
      {
	++m_blocking_continuation_worker_count;
      }
    return true;
  }

  template <stats_t Stats>
  bool
  worker_pool_elastic<Stats>::core_elastic::request_worker_retirement (worker_elastic *w)
  {
    assert (!w->m_retire_requested.load (std::memory_order_acquire));
    ++m_retiring_worker_count;
    if (w->m_blocking_continuation_only)
      {
	++m_retiring_blocking_continuation_worker_count;
      }
    return w->request_retire ();
  }

  template <stats_t Stats>
  void
  worker_pool_elastic<Stats>::core_elastic::retire_available_excess_workers (std::unique_lock<std::mutex> &ulock)
  {
    assert (ulock.owns_lock ());

    std::size_t target_worker_count = m_max_concurrency + m_progress_worker_count;
    while (true)
      {
	bool has_excess_regular_worker = get_active_regular_worker_count () > target_worker_count;
	auto available_it = std::find_if (this->m_available_workers.rbegin (), this->m_available_workers.rend (),
					  [has_excess_regular_worker] (worker *worker_p)
	{
	  worker_elastic *worker = static_cast<worker_elastic *> (worker_p);
	  return worker->can_retire () && (worker->m_blocking_continuation_only || has_excess_regular_worker);
	});
	if (available_it == this->m_available_workers.rend ())
	  {
	    // busy surplus and continuation-only workers retire before trying to dequeue another task.
	    break;
	  }

	worker_elastic *worker_p = static_cast<worker_elastic *> (*available_it);
	this->m_available_workers.erase (std::next (available_it).base ());

	bool has_thread = request_worker_retirement (worker_p);
	if (!has_thread)
	  {
	    erase_worker (worker_p, ulock);
	  }
      }
  }

  template <stats_t Stats>
  void
  worker_pool_elastic<Stats>::core_elastic::wait_for_worker_retirements (std::unique_lock<std::mutex> &ulock)
  {
    assert (ulock.owns_lock ());

    if (m_retiring_worker_count > 0)
      {
	// the wait releases m_core_mutex, so retiring workers can enter worker_thread_exited () and erase themselves.
	m_worker_retire_cv.wait (ulock, [this] ()
	{
	  return m_retiring_worker_count == 0;
	});
      }
  }

  template <stats_t Stats>
  std::size_t
  worker_pool_elastic<Stats>::core_elastic::get_active_regular_worker_count (void) const
  {
    return worker_pool_elastic::calculate_active_regular_worker_count (m_reserved_worker_count,
	   m_retiring_worker_count, m_blocking_continuation_worker_count,
	   m_retiring_blocking_continuation_worker_count);
  }

  template <stats_t Stats>
  void
  worker_pool_elastic<Stats>::core_elastic::erase_worker (worker_elastic *w,
      std::unique_lock<std::mutex> &ulock)
  {
    assert (ulock.owns_lock ());

    this->m_workers_readers_cv.wait (ulock, [this] ()
    {
      return !this->has_workers_snapshot_readers ();
    });

    auto worker_it = std::find_if (this->m_workers.begin (), this->m_workers.end (),
				   [w] (const std::unique_ptr<worker> &ptr)
    {
      return ptr.get () == w;
    });
    assert (worker_it != this->m_workers.end ());

    bool retire_requested = w->m_retire_requested.load (std::memory_order_acquire);
    assert (retire_requested);
    bool is_blocking_continuation_worker = w->m_blocking_continuation_only;
    bool replace_with_regular = w->m_replace_with_regular_on_retire;

    stats::accumulate (w->m_stats, m_retired_stats);
    this->m_workers.erase (worker_it);

    assert (m_reserved_worker_count > 0);
    assert (m_current_worker.load () > 0);
    --m_reserved_worker_count;
    if (is_blocking_continuation_worker)
      {
	assert (m_blocking_continuation_worker_count > 0);
	--m_blocking_continuation_worker_count;
      }
    else
      {
	assert (m_current_regular_worker.load () > 0);
	m_current_regular_worker.fetch_sub (1);
      }
    m_current_worker.fetch_sub (1);
    if (retire_requested)
      {
	assert (m_retiring_worker_count > 0);
	--m_retiring_worker_count;
	if (is_blocking_continuation_worker)
	  {
	    assert (m_retiring_blocking_continuation_worker_count > 0);
	    --m_retiring_blocking_continuation_worker_count;
	  }
      }
    m_worker_retire_cv.notify_all ();

    if (replace_with_regular && this->m_parent_pool->is_running () && m_blocking_continuation_queue.empty ()
	&& !this->m_task_queue.empty ())
      {
	// Reuse already admitted regular capacity immediately. If none exists, leave target growth to the pool-wide
	// progress controller so simultaneous continuation retirements cannot bypass its bounded ramp.
	adjust_workers (ulock);
      }
  }

  template <stats_t Stats>
  typename worker_pool_elastic<Stats>::core_elastic::worker_elastic *
  worker_pool_elastic<Stats>::core_elastic::get_available_worker (task_admission admission)
  {
    auto is_eligible = [admission] (worker *worker_p)
    {
      return admission == task_admission::blocking_continuation
	     || !static_cast<worker_elastic *> (worker_p)->m_blocking_continuation_only;
    };

    auto selected_it = this->m_available_workers.end ();
    for (auto it = this->m_available_workers.begin (); it != this->m_available_workers.end (); ++it)
      {
	if (is_eligible (*it) && static_cast<worker_elastic *> (*it)->has_thread ())
	  {
	    selected_it = it;
	    break;
	  }
      }

    if (selected_it == this->m_available_workers.end ())
      {
	for (auto it = this->m_available_workers.rbegin (); it != this->m_available_workers.rend (); ++it)
	  {
	    if (is_eligible (*it))
	      {
		selected_it = std::prev (it.base ());
		break;
	      }
	  }
      }

    if (selected_it == this->m_available_workers.end ())
      {
	return nullptr;
      }

    worker_elastic *worker_p = static_cast<worker_elastic *> (*selected_it);
    this->m_available_workers.erase (selected_it);
    return worker_p;
  }

  template <stats_t Stats>
  bool
  worker_pool_elastic<Stats>::core_elastic::has_available_worker (task_admission admission) const
  {
    return std::any_of (this->m_available_workers.begin (), this->m_available_workers.end (),
			[admission] (worker *worker_p)
    {
      return admission == task_admission::blocking_continuation
	     || !static_cast<worker_elastic *> (worker_p)->m_blocking_continuation_only;
    });
  }

  template <stats_t Stats>
  typename worker_pool_elastic<Stats>::core_elastic::worker_elastic *
  worker_pool_elastic<Stats>::core_elastic::get_or_make_available_worker (task_admission admission,
      worker_reservation_mode reservation_mode)
  {
    worker_elastic *worker_p;

    worker_p = get_available_worker (admission);
    if (!worker_p && reserve_available_worker (admission, reservation_mode))
      {
	// create a worker when none is available
	this->m_workers.push_back (this->allocate_worker ());
	worker_p = static_cast<worker_elastic *> (this->m_workers.back ().get ());
	worker_p->set_parent_core (*this);
	worker_p->m_blocking_continuation_only = admission == task_admission::blocking_continuation;
      }
    return worker_p;
  }

  template <stats_t Stats>
  bool
  worker_pool_elastic<Stats>::core_elastic::has_queued_task_unlocked (void) const
  {
    return !m_blocking_continuation_queue.empty () || !this->m_task_queue.empty ();
  }

  template <stats_t Stats>
  void
  worker_pool_elastic<Stats>::core_elastic::enqueue_task (wrapped_task &&task_ref, bool front)
  {
    std::deque<wrapped_task> &queue = task_ref.get_admission () == task_admission::blocking_continuation
				      ? m_blocking_continuation_queue : this->m_task_queue;

    if (front)
      {
	queue.push_front (std::move (task_ref));
      }
    else
      {
	queue.push_back (std::move (task_ref));
      }
  }

  template <stats_t Stats>
  typename worker_pool_elastic<Stats>::wrapped_task
  worker_pool_elastic<Stats>::core_elastic::dequeue_task (void)
  {
    assert (has_queued_task_unlocked ());

    std::deque<wrapped_task> &queue = m_blocking_continuation_queue.empty () ? this->m_task_queue
				      : m_blocking_continuation_queue;
    wrapped_task task_ref = std::move (queue.front ());
    queue.pop_front ();
    return task_ref;
  }

  template <stats_t Stats>
  bool
  worker_pool_elastic<Stats>::core_elastic::try_expand_for_queued_task (std::unique_lock<std::mutex> &ulock)
  {
    assert (ulock.owns_lock ());

    if (!this->m_parent_pool->is_running () || !has_queued_task_unlocked ())
      {
	return false;
      }

    task_admission admission = m_blocking_continuation_queue.empty () ? task_admission::regular
			       : task_admission::blocking_continuation;
    bool has_reservation_capacity = m_current_worker.load (std::memory_order_relaxed)
				    < m_max_worker.load (std::memory_order_relaxed);
    if (has_reservation_capacity && admission == task_admission::regular)
      {
	std::size_t regular_worker_limit = std::min (m_max_regular_worker.load (std::memory_order_relaxed),
					   m_max_worker.load (std::memory_order_relaxed));
	has_reservation_capacity = m_current_regular_worker.load (std::memory_order_relaxed) < regular_worker_limit;
      }
    if (!has_available_worker (admission) && !has_reservation_capacity)
      {
	return false;
      }

    if (admission == task_admission::blocking_continuation)
      {
	// continuations use a temporary slot and their reserved hard-cap headroom. they must not permanently raise the
	// regular target, which would later allow unrelated requests to consume that emergency capacity
	return try_dispatch_queued_task (ulock, worker_reservation_mode::normal);
      }

    std::size_t previous_progress_worker_count = m_progress_worker_count;
    ++m_progress_worker_count;
    std::size_t expanded_target_worker_count = m_max_concurrency + m_progress_worker_count;
    worker_reservation_mode reservation_mode =
	    get_active_regular_worker_count () >= expanded_target_worker_count
	    ? worker_reservation_mode::stall_recovery : worker_reservation_mode::normal;

    // decide before adjust_concurrency (), which may release the core mutex. normal mode prevents another dispatch
    // from consuming the new target and this controller dispatch from also reserving an extra worker
    m_slots.adjust_concurrency (expanded_target_worker_count, ulock);

    // adjust_concurrency () may have allowed a continuation to enter the queue while the core mutex was released
    // do not let that continuation commit the raised regular target
    if (m_blocking_continuation_queue.empty () && try_dispatch_queued_task (ulock, reservation_mode))
      {
	return true;
      }

    // DO NOT accumulate slots while thread creation repeatedly fails or another core consumes the last reservation
    m_progress_worker_count = previous_progress_worker_count;
    m_slots.adjust_concurrency (m_max_concurrency + m_progress_worker_count, ulock);
    retire_available_excess_workers (ulock);
    return false;
  }

  template <stats_t Stats>
  bool
  worker_pool_elastic<Stats>::core_elastic::try_dispatch_queued_task (std::unique_lock<std::mutex> &ulock,
      worker_reservation_mode reservation_mode)
  {
    assert (ulock.owns_lock ());

    if (!has_queued_task_unlocked ())
      {
	return false;
      }

    wrapped_task queued_task = dequeue_task ();
    task_admission admission = queued_task.get_admission ();
    auto slot = acquire_slot (admission, ulock);
    if (!slot)
      {
	enqueue_task (std::move (queued_task), true);
	return false;
      }

    worker_elastic *worker_p = get_or_make_available_worker (admission, reservation_mode);
    if (worker_p == nullptr)
      {
	// release_slot may temporarily drop the core lock while waking a waiter. restore queue visibility first so a
	// concurrent one-pass shutdown drain cannot miss this task.
	enqueue_task (std::move (queued_task), true);
	release_slot (std::move (slot), ulock);
	return false;
      }

    return try_execute_task_with_slot (worker_p, std::move (queued_task), std::move (slot), ulock);
  }

  template <stats_t Stats>
  bool
  worker_pool_elastic<Stats>::core_elastic::try_execute_task_with_slot (worker_elastic *worker_p, wrapped_task &&task_ref,
      unique_slot slot, std::unique_lock<std::mutex> &ulock)
  {
    assert (ulock.owns_lock ());

    auto unexecuted = worker_p->assign_task (std::move (task_ref), std::move (slot));
    if (unexecuted.has_value ())
      {
	// could not start a new thread; put the task and slot back
	// requeue in front of the same priority class to preserve its FIFO order
	enqueue_task (std::move (unexecuted->first), true);
	// release the slot
	release_slot (std::move (unexecuted->second), ulock);
	if (worker_p->m_blocking_continuation_only)
	  {
	    // A continuation-only worker without a thread cannot remain visible as regular capacity. Retire the failed
	    // reservation; the queued continuation will retry on the next dispatch trigger.
	    bool has_thread = request_worker_retirement (worker_p);
	    assert (!has_thread);
	    erase_worker (worker_p, ulock);
	  }
	else
	  {
	    // return the regular worker to the available list so a transient thread-creation failure can be retried
	    this->m_available_workers.push_back (worker_p);
	  }

	return false;
      }
    return true;
  }

  template <stats_t Stats>
  worker_pool_elastic<Stats>::core_elastic::worker_elastic::worker_elastic ()
    : m_slot (nullptr)
    , m_completed_task (false)
    , m_blocking_continuation_only (false)
    , m_replace_with_regular_on_retire (false)
    , m_retire_requested (false)
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
    assert (!m_retire_requested.load (std::memory_order_acquire));

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
    bool completed_task = m_completed_task;
    m_completed_task = false;

    if (!this->m_stop && !m_retire_requested.load (std::memory_order_acquire))
      {
	std::optional<std::pair<wrapped_task, unique_slot>> queued =
		    static_cast<core_elastic *> (this->m_parent_core)->get_task_and_slot_or_become_available (*this,
			completed_task);
	if (queued.has_value ())
	  {
	    // stats: found in queue
	    stats::time_and_increment (this->m_stats, stats::id::found_in_queue);

	    this->m_wrapped_task.emplace (std::move (queued->first));
	    m_slot = std::move (queued->second);

	    return true;
	  }

	// wait for a task
	ulock.lock ();

	assert ((!this->m_wrapped_task.has_value () && !m_slot) || (this->m_wrapped_task.has_value () && m_slot));

	if ((!this->m_wrapped_task.has_value () && !m_slot) && !this->m_stop
	    && !m_retire_requested.load (std::memory_order_acquire))
	  {
	    // wait for a task, a stop request, or idle timeout
	    if (this->m_persistent)
	      {
		condvar_wait (this->m_task_cv, ulock, cubthread::wait_seconds (), [this] () -> bool
		{
		  return (this->m_wrapped_task.has_value () && m_slot) || this->m_stop
		  || m_retire_requested.load (std::memory_order_acquire);
		});
	      }
	    else
	      {
		condvar_wait (this->m_task_cv, ulock, this->m_parent_core->get_parent_pool ()->get_idle_timeout (),
			      [this] () -> bool
		{
		  return (this->m_wrapped_task.has_value () && m_slot) || this->m_stop
		  || m_retire_requested.load (std::memory_order_acquire);
		});
	      }
	  }
	else
	  {
	    // no need to wait
	  }
      }
    else if (this->m_stop)
      {
	// keep this worker visible to the core while it is stopping
	static_cast<core_elastic *> (this->m_parent_core)->become_available (*this);

	ulock.lock ();
      }
    else
      {
	// retiring worker was already removed from the available list by the core.
	assert (m_retire_requested.load (std::memory_order_acquire));
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
  bool
  worker_pool_elastic<Stats>::core_elastic::worker_elastic::request_retire (void)
  {
    assert (can_retire ());

    std::unique_lock<std::mutex> ulock (this->m_task_mutex);
    assert (!this->m_wrapped_task.has_value () && !m_slot);

    m_retire_requested.store (true, std::memory_order_release);
    bool has_thread = this->has_active_threads ();
    ulock.unlock ();

    this->m_task_cv.notify_one ();
    return has_thread;
  }

  template <stats_t Stats>
  bool
  worker_pool_elastic<Stats>::core_elastic::worker_elastic::can_retire (void) const
  {
    return !this->m_persistent;
  }

  template <stats_t Stats>
  void
  worker_pool_elastic<Stats>::core_elastic::worker_elastic::on_thread_exit (void)
  {
    // the core serializes the final active-thread decrement with worker selection and forced retirement. it may delete
    // this worker when the decrement reaches zero; do not access any member after this call.
    static_cast<core_elastic *> (this->m_parent_core)->worker_thread_exited (this);
  }

  template <stats_t Stats>
  std::size_t
  worker_pool_elastic<Stats>::core_elastic::worker_elastic::finish_active_thread (void)
  {
    return this->decrement_active_thread_count ();
  }

  template <stats_t Stats>
  void
  worker_pool_elastic<Stats>::core_elastic::worker_elastic::before_task_execution (void)
  {
    assert (dynamic_cast<core_elastic *> (this->m_parent_core));
    assert (this->m_wrapped_task.has_value () && m_slot);
    assert (!this->m_context_p->m_slot);

    // move the slot to the thread entry for task execution
    this->m_context_p->m_slot = std::move (m_slot);
  }

  template <stats_t Stats>
  void
  worker_pool_elastic<Stats>::core_elastic::worker_elastic::after_task_execution (void)
  {
    m_completed_task = true;

    // return the slot to the pool
    if (this->m_context_p->m_slot)
      {
	this->m_context_p->m_slot->return_to_pool (std::move (this->m_context_p->m_slot));
	this->m_context_p->m_slot = nullptr;
      }
  }

} // namespace cubthread

#endif // _THREAD_WORKER_POOL_ELASTIC_HPP_
