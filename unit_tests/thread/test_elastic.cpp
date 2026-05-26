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
 * test_elastic.cpp - implementation of elastic worker pool tests
 */

#include "test_elastic.hpp"

// testing server mode
#define SERVER_MODE
#include "concurrency_slot.hpp"
#include "thread_entry.hpp"
#include "thread_entry_task.hpp"
#include "thread_manager.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

namespace test_thread
{
  namespace
  {
    constexpr std::size_t ELASTIC_CORE_COUNT = 1;
    constexpr std::size_t ELASTIC_DEFAULT_POOL_ENTRIES = 4;
    constexpr std::size_t ELASTIC_ONE_SLOT = 1;
    constexpr std::size_t ELASTIC_TWO_SLOTS = 2;
    constexpr std::size_t ELASTIC_ONE_WORKER = 1;
    constexpr std::size_t ELASTIC_TWO_WORKERS = 2;
    constexpr std::size_t ELASTIC_THREE_WORKERS = 3;

    REGISTER_WORKERPOOL (elastic, []()
    {
      return static_cast<int> (ELASTIC_DEFAULT_POOL_ENTRIES);
    });

    using elastic_pool_type = worker_pool_type<cubthread::stats_t::on, cubthread::pool_t::elastic>;

    // Each worker stat is a COUNTER_AND_TIMER, so each stat::id occupies two consecutive values
    // (count at id*2, time at id*2+1). The runtime layout check below catches regressions if a
    // stat is reordered, added, or switched to a different type.
    template <elastic_pool_type::stats::id StatId>
    constexpr std::size_t worker_stat_count_index ()
    {
      return static_cast<std::size_t> (StatId) * 2;
    }

    bool
    worker_stats_layout_is_counter_and_timer ()
    {
      return elastic_pool_type::stats::get_count ()
	     == static_cast<std::size_t> (elastic_pool_type::stats::id::type_count) * 2;
    }

    struct runtime_stats
    {
      UINT64 total_slots = 0;
      UINT64 target_slots = 0;
      UINT64 busy_slots = 0;
      UINT64 total_workers = 0;
      UINT64 target_workers = 0;
      UINT64 busy_workers = 0;
    };

    class gate
    {
      public:
	void signal ()
	{
	  std::lock_guard<std::mutex> lock (m_mutex);
	  m_open = true;
	  m_condvar.notify_all ();
	}

	template <typename Rep, typename Period>
	bool wait_for (const std::chrono::duration<Rep, Period> &timeout)
	{
	  std::unique_lock<std::mutex> lock (m_mutex);

	  return m_condvar.wait_for (lock, timeout, [this]
	  {
	    return m_open;
	  });
	}

      private:
	std::mutex m_mutex;
	std::condition_variable m_condvar;
	bool m_open = false;
    };

    struct elastic_test_state
    {
      // Gates keep otherwise short-lived worker-pool states observable from the main test thread.
      gate started;
      gate release;

      // These flags are written by worker threads and asserted by the main test thread.
      std::atomic<cubthread::entry *> entry { nullptr };
      std::atomic_bool had_slot { false };
      std::atomic_bool executed { false };
      std::atomic_bool done { false };
      std::atomic_bool retired { false };
      std::atomic_int order { 0 };
    };

    struct load_test_state
    {
      // Load-test counters are asserted by the main test thread after all producers and workers drain.
      std::atomic<std::size_t> submitted { 0 };
      std::atomic<std::size_t> started { 0 };
      std::atomic<std::size_t> completed { 0 };
      std::atomic<std::size_t> retired { 0 };
      std::atomic<std::size_t> running { 0 };

      // These fields capture transient peaks that would be too short for statdump-based verification.
      std::atomic<std::size_t> max_running { 0 };
      std::atomic<UINT64> max_busy_slots_seen { 0 };
      std::atomic<UINT64> max_workers_seen { 0 };
      std::atomic<std::size_t> max_pending { 0 };
      std::atomic<std::size_t> runtime_samples { 0 };
      std::atomic<std::size_t> adjust_count { 0 };

      // Every task must start with an owned slot. A nonzero value means the pool violated the slot contract.
      std::atomic<std::size_t> missing_initial_slot { 0 };
    };

    struct runtime_adjustment_step
    {
      std::size_t max_concurrency;
      std::size_t max_worker;
    };

    void
    fail_test (const char *message)
    {
      std::cerr << "elastic worker pool test failed: " << message << std::endl;
      std::abort ();
    }

    void
    require_test (bool condition, const char *message)
    {
      if (!condition)
	{
	  fail_test (message);
	}
    }

    void
    print_pass (const char *message)
    {
      std::cout << "    OK - " << message << std::endl;
    }

    template <typename T>
    void
    update_maximum (std::atomic<T> &maximum, T value)
    {
      T current = maximum.load ();

      while (current < value && !maximum.compare_exchange_weak (current, value))
	{
	}
    }

    class daemon_guard
    {
      public:
	daemon_guard () = default;

	explicit daemon_guard (bool start_now)
	{
	  if (start_now)
	    {
	      start ();
	    }
	}

	~daemon_guard ()
	{
	  stop ();
	}

	daemon_guard (const daemon_guard &) = delete;
	daemon_guard &operator= (const daemon_guard &) = delete;

	void start ()
	{
	  if (!m_started)
	    {
	      cubthread::concurrency_slot_daemon::initialize ();
	      m_started = true;
	    }
	}

	void stop ()
	{
	  if (m_started)
	    {
	      cubthread::concurrency_slot_daemon::finalize ();
	      m_started = false;
	    }
	}

      private:
	bool m_started = false;
    };

    template <typename Predicate>
    bool
    wait_until (Predicate &&predicate, std::chrono::milliseconds timeout)
    {
      const auto deadline = std::chrono::steady_clock::now () + timeout;

      // Elastic behavior depends on daemon and worker scheduling. Poll the observable state instead of sleeping for a
      // fixed interval and hoping to catch a transient state.
      while (std::chrono::steady_clock::now () < deadline)
	{
	  if (predicate ())
	    {
	      return true;
	    }
	  std::this_thread::sleep_for (std::chrono::milliseconds (10));
	}

      return predicate ();
    }

    timespec
    make_abs_timespec (std::chrono::milliseconds delta)
    {
      timespec result;

      clock_gettime (CLOCK_REALTIME, &result);
      result.tv_sec += static_cast<time_t> (delta.count () / 1000);
      result.tv_nsec += static_cast<long> ((delta.count () % 1000) * 1000000);
      if (result.tv_nsec >= 1000000000)
	{
	  result.tv_sec++;
	  result.tv_nsec -= 1000000000;
	}

      return result;
    }

    runtime_stats
    read_runtime_stats (elastic_pool_type *pool)
    {
      runtime_stats stats;

      pool->get_runtime_stats (stats.total_slots, stats.target_slots, stats.busy_slots,
			       stats.total_workers, stats.target_workers, stats.busy_workers);

      return stats;
    }

    elastic_pool_type *
    create_pool (std::size_t pool_entries, std::size_t max_concurrency, std::size_t max_worker,
		 bool pool_threads = false,
		 cubthread::wait_seconds idle_timeout = cubthread::wait_seconds (std::chrono::seconds (1)))
    {
      return thread_create_worker_pool<cubthread::stats_t::on, cubthread::pool_t::elastic> (
		     pool_entries,
		     ELASTIC_CORE_COUNT,
		     max_concurrency,
		     max_worker,
		     "elastic-test",
		     thread_get_entry_manager (),
		     pool_threads,
		     idle_timeout
	     );
    }

    elastic_pool_type *
    create_pool_with_cores (std::size_t pool_entries, std::size_t core_count, std::size_t max_concurrency,
			    std::size_t max_worker)
    {
      return thread_create_worker_pool<cubthread::stats_t::on, cubthread::pool_t::elastic> (
		     pool_entries,
		     core_count,
		     max_concurrency,
		     max_worker,
		     "elastic-test",
		     thread_get_entry_manager (),
		     false,
		     cubthread::wait_seconds (std::chrono::seconds (1))
	     );
    }

    void
    destroy_pool (elastic_pool_type *&pool)
    {
      thread_get_manager ()->destroy_worker_pool (pool);
    }

    bool
    entry_is_waiting (cubthread::entry *entry_p, thread_resume_suspend_status status)
    {
      entry_p->lock ();
      const bool result = entry_p->m_status == cubthread::entry::status::TS_WAIT
			  && entry_p->resume_status == status;
      entry_p->unlock ();

      return result;
    }

    bool
    entry_is_lock_waiting (cubthread::entry *entry_p)
    {
      return entry_is_waiting (entry_p, THREAD_LOCK_SUSPENDED);
    }

    bool
    entry_is_slot_waiting (cubthread::entry *entry_p)
    {
      return entry_is_waiting (entry_p, THREAD_CONCURRENCY_SLOT_SUSPENDED);
    }

    bool
    entry_has_slot (cubthread::entry *entry_p)
    {
      entry_p->lock ();
      const bool result = entry_p->m_slot != nullptr;
      entry_p->unlock ();

      return result;
    }

    class hold_slot_task : public cubthread::entry_task
    {
      public:
	explicit hold_slot_task (elastic_test_state &state, bool wait_for_release = true,
				 std::atomic_int *order_counter = nullptr)
	  : m_state (state)
	  , m_wait_for_release (wait_for_release)
	  , m_order_counter (order_counter)
	{
	}

	void execute (cubthread::entry &context) override
	{
	  m_state.executed.store (true);
	  m_state.had_slot.store (context.m_slot != nullptr);
	  m_state.entry.store (&context);
	  if (m_order_counter != nullptr)
	    {
	      m_state.order.store (m_order_counter->fetch_add (1) + 1);
	    }
	  m_state.started.signal ();

	  if (m_wait_for_release)
	    {
	      // Keep the slot busy long enough for the main test thread to observe the worker-pool state.
	      (void) m_state.release.wait_for (std::chrono::seconds (5));
	    }

	  m_state.done.store (true);
	}

	void retire () override
	{
	  m_state.retired.store (true);
	  cubthread::entry_task::retire ();
	}

      private:
	elastic_test_state &m_state;
	bool m_wait_for_release;
	std::atomic_int *m_order_counter;
    };

    class suspend_task : public cubthread::entry_task
    {
      public:
	suspend_task (elastic_test_state &state, thread_resume_suspend_status suspend_status)
	  : m_state (state)
	  , m_suspend_status (suspend_status)
	{
	}

	void execute (cubthread::entry &context) override
	{
	  // Elastic worker-pool tasks must run with a slot. Eligible suspend states later allow the daemon to steal it.
	  m_state.executed.store (true);
	  m_state.had_slot.store (context.m_slot != nullptr);
	  m_state.entry.store (&context);
	  m_state.started.signal ();

	  // This is intentionally not a plain condition_variable wait. The CUBRID suspend path marks the entry status
	  // and, for eligible reasons, starts the elastic-slot wait timer.
	  context.lock ();
	  thread_suspend_wakeup_and_unlock_entry (&context, m_suspend_status);

	  // If the slot was stolen, this line is reached only after the entry reacquires a concurrency slot.
	  m_state.done.store (true);
	}

	void retire () override
	{
	  m_state.retired.store (true);
	  cubthread::entry_task::retire ();
	}

      private:
	elastic_test_state &m_state;
	thread_resume_suspend_status m_suspend_status;
    };

    class timed_suspend_task : public cubthread::entry_task
    {
      public:
	timed_suspend_task (elastic_test_state &state, std::chrono::milliseconds timeout)
	  : m_state (state)
	  , m_timeout (timeout)
	{
	}

	void execute (cubthread::entry &context) override
	{
	  m_state.executed.store (true);
	  m_state.had_slot.store (context.m_slot != nullptr);
	  m_state.entry.store (&context);
	  m_state.started.signal ();

	  timespec timeout = make_abs_timespec (m_timeout);

	  context.lock ();
	  (void) thread_suspend_timeout_wakeup_and_unlock_entry (&context, &timeout, THREAD_LOCK_SUSPENDED);

	  m_state.done.store (true);
	}

	void retire () override
	{
	  m_state.retired.store (true);
	  cubthread::entry_task::retire ();
	}

      private:
	elastic_test_state &m_state;
	std::chrono::milliseconds m_timeout;
    };

    class coordinated_timed_suspend_task : public cubthread::entry_task
    {
      public:
	coordinated_timed_suspend_task (elastic_test_state &state, gate &release_suspend,
					std::chrono::milliseconds timeout)
	  : m_state (state)
	  , m_release_suspend (release_suspend)
	  , m_timeout (timeout)
	{
	}

	void execute (cubthread::entry &context) override
	{
	  m_state.executed.store (true);
	  m_state.had_slot.store (context.m_slot != nullptr);
	  m_state.entry.store (&context);
	  m_state.started.signal ();

	  (void) m_release_suspend.wait_for (std::chrono::seconds (5));

	  timespec timeout = make_abs_timespec (m_timeout);

	  context.lock ();
	  (void) thread_suspend_timeout_wakeup_and_unlock_entry (&context, &timeout, THREAD_LOCK_SUSPENDED);

	  m_state.done.store (true);
	}

	void retire () override
	{
	  m_state.retired.store (true);
	  cubthread::entry_task::retire ();
	}

      private:
	elastic_test_state &m_state;
	gate &m_release_suspend;
	std::chrono::milliseconds m_timeout;
    };

    class mixed_load_task : public cubthread::entry_task
    {
      public:
	mixed_load_task (load_test_state &state, std::size_t task_id)
	  : m_state (state)
	  , m_task_id (task_id)
	{
	}

	void execute (cubthread::entry &context) override
	{
	  if (context.m_slot == nullptr)
	    {
	      m_state.missing_initial_slot.fetch_add (1);
	    }

	  const std::size_t running_now = m_state.running.fetch_add (1) + 1;

	  update_maximum (m_state.max_running, running_now);
	  m_state.started.fetch_add (1);

	  // The fixed pattern mixes long eligible waits, short waits, and quick work. This keeps the test deterministic
	  // while still forcing the daemon, worker creation, slot stealing, slot return, and normal queueing paths to
	  // interleave under sustained producer pressure.
	  switch (m_task_id % 6)
	    {
	    case 0:
	      timed_suspend (context, THREAD_LOCK_SUSPENDED, std::chrono::milliseconds (120));
	      break;
	    case 1:
	      timed_suspend (context, THREAD_CSS_QUEUE_SUSPENDED, std::chrono::milliseconds (100));
	      break;
	    case 2:
	      std::this_thread::sleep_for (std::chrono::milliseconds (8));
	      break;
	    case 3:
	      timed_suspend (context, THREAD_LOCK_SUSPENDED, std::chrono::milliseconds (25));
	      break;
	    case 4:
	      std::this_thread::sleep_for (std::chrono::milliseconds (2));
	      break;
	    default:
	      std::this_thread::yield ();
	      break;
	    }

	  m_state.running.fetch_sub (1);
	  m_state.completed.fetch_add (1);
	}

	void retire () override
	{
	  m_state.retired.fetch_add (1);
	  cubthread::entry_task::retire ();
	}

      private:
	void timed_suspend (cubthread::entry &context, thread_resume_suspend_status status,
			    std::chrono::milliseconds timeout)
	{
	  timespec timeout_spec = make_abs_timespec (timeout);

	  context.lock ();
	  (void) thread_suspend_timeout_wakeup_and_unlock_entry (&context, &timeout_spec, status);
	}

	load_test_state &m_state;
	std::size_t m_task_id;
    };

    void
    require_task_started (elastic_test_state &state, const char *message)
    {
      require_test (state.started.wait_for (std::chrono::seconds (5)), message);
      require_test (state.executed.load (), "task gate opened without execute");
    }

    void
    require_task_not_started (elastic_test_state &state, std::chrono::milliseconds timeout, const char *message)
    {
      require_test (!state.started.wait_for (timeout), message);
      require_test (!state.executed.load (), "task executed although start gate did not open");
    }

    void
    wake_task (elastic_test_state &state, thread_resume_suspend_status resume_status)
    {
      cubthread::entry *entry_p = state.entry.load ();

      require_test (entry_p != nullptr, "task did not publish its entry before wakeup");
      thread_wakeup (entry_p, resume_status);
    }

    void
    test_initial_runtime_stats ()
    {
      elastic_pool_type *pool = create_pool (ELASTIC_DEFAULT_POOL_ENTRIES, ELASTIC_TWO_SLOTS, ELASTIC_THREE_WORKERS);

      require_test (pool != nullptr, "failed to create elastic worker pool");
      require_test (pool->get_max_concurrency () == ELASTIC_TWO_SLOTS, "wrong max concurrency");
      require_test (pool->get_max_worker () == ELASTIC_THREE_WORKERS, "wrong max worker");

      const runtime_stats stats = read_runtime_stats (pool);

      require_test (stats.total_slots == ELASTIC_TWO_SLOTS, "wrong initial slot count");
      require_test (stats.target_slots == ELASTIC_TWO_SLOTS, "wrong initial target slot count");
      require_test (stats.busy_slots == 0, "initial slots are busy");
      require_test (stats.total_workers == ELASTIC_TWO_SLOTS, "wrong initial worker count");
      require_test (stats.target_workers == ELASTIC_THREE_WORKERS, "wrong initial target worker count");

      destroy_pool (pool);
      print_pass ("initial runtime stats are consistent");
    }

    void
    test_basic_slot_lifecycle ()
    {
      elastic_test_state task;
      elastic_pool_type *pool = create_pool (ELASTIC_DEFAULT_POOL_ENTRIES, ELASTIC_ONE_SLOT, ELASTIC_ONE_WORKER);

      thread_get_manager ()->push_task (pool, new hold_slot_task (task));
      require_task_started (task, "basic task did not start");
      require_test (task.had_slot.load (), "basic task did not receive a slot");

      require_test (wait_until ([pool]
      {
	return read_runtime_stats (pool).busy_slots == ELASTIC_ONE_SLOT;
      }, std::chrono::seconds (1)), "busy slot was not reported while task was running");

      task.release.signal ();

      require_test (wait_until ([&task]
      {
	return task.done.load () && task.retired.load ();
      }, std::chrono::seconds (5)), "basic task did not finish");
      require_test (read_runtime_stats (pool).busy_slots == 0, "slot remained busy after basic task");

      destroy_pool (pool);
      print_pass ("basic task uses and returns a slot");
    }

    void
    test_queued_task_waits_for_slot_return ()
    {
      elastic_test_state holder;
      elastic_test_state queued;
      elastic_pool_type *pool = create_pool (ELASTIC_DEFAULT_POOL_ENTRIES, ELASTIC_ONE_SLOT, ELASTIC_ONE_WORKER);

      thread_get_manager ()->push_task (pool, new hold_slot_task (holder));
      require_task_started (holder, "holder task did not start");

      thread_get_manager ()->push_task (pool, new hold_slot_task (queued));
      require_task_not_started (queued, std::chrono::milliseconds (200),
				"queued task started while the only slot was busy");
      require_test (read_runtime_stats (pool).busy_slots == ELASTIC_ONE_SLOT, "wrong busy slot count");

      holder.release.signal ();
      require_task_started (queued, "queued task did not start after slot was returned");
      require_test (queued.had_slot.load (), "queued task did not receive the returned slot");

      queued.release.signal ();
      require_test (wait_until ([&holder, &queued]
      {
	return holder.done.load () && queued.done.load ();
      }, std::chrono::seconds (5)), "queued slot-return scenario did not finish");

      destroy_pool (pool);
      print_pass ("queued task waits until a slot is returned");
    }

    void
    test_short_wait_is_not_stolen ()
    {
      daemon_guard daemon (true);
      elastic_test_state waiter;
      gate release_suspend;
      elastic_pool_type *pool =
	      create_pool (ELASTIC_DEFAULT_POOL_ENTRIES, ELASTIC_ONE_SLOT, ELASTIC_TWO_WORKERS);

      // The daemon's steal threshold is 50ms (concurrency_slot.cpp). This timed wait stays under
      // the threshold. The task first waits on a test gate so the main thread can be ready before
      // the short CUBRID suspend window starts.
      thread_get_manager ()->push_task (pool,
					new coordinated_timed_suspend_task (waiter, release_suspend,
					    std::chrono::milliseconds (30)));
      require_task_started (waiter, "short waiter did not start");

      cubthread::entry *waiter_entry = waiter.entry.load ();
      require_test (waiter_entry != nullptr, "short waiter did not publish its entry");
      release_suspend.signal ();
      require_test (wait_until ([waiter_entry]
      {
	return entry_is_waiting (waiter_entry, THREAD_LOCK_SUSPENDED);
      }, std::chrono::milliseconds (100)), "short waiter did not enter wait state");

      // Atomically check, under the entry lock, that while still in the short wait the slot
      // remains held. Reading both fields in one critical section avoids a race with the
      // resumption path that clears m_slot just after m_status changes.
      const auto deadline = std::chrono::steady_clock::now () + std::chrono::seconds (1);
      while (std::chrono::steady_clock::now () < deadline)
	{
	  bool still_waiting;
	  bool has_slot;

	  waiter_entry->lock ();
	  still_waiting = waiter_entry->m_status == cubthread::entry::status::TS_WAIT
			  && waiter_entry->resume_status == THREAD_LOCK_SUSPENDED;
	  has_slot = waiter_entry->m_slot != nullptr;
	  waiter_entry->unlock ();

	  if (!still_waiting)
	    {
	      break;
	    }
	  require_test (has_slot, "slot was stolen during a sub-threshold wait");
	  std::this_thread::sleep_for (std::chrono::microseconds (200));
	}
      require_test (waiter.done.load (), "short waiter did not finish before slot polling timeout");

      require_test (wait_until ([&waiter]
      {
	return waiter.done.load ();
      }, std::chrono::seconds (1)), "short waiter did not resume");
      require_test (read_runtime_stats (pool).busy_slots == 0, "short wait leaked a busy slot");

      destroy_pool (pool);
      print_pass ("short eligible wait keeps its slot and exits without leak");
    }

    void
    test_non_eligible_wait_is_not_stolen ()
    {
      daemon_guard daemon (true);
      elastic_test_state waiter;
      elastic_test_state queued;
      elastic_pool_type *pool =
	      create_pool (ELASTIC_DEFAULT_POOL_ENTRIES, ELASTIC_ONE_SLOT, ELASTIC_TWO_WORKERS);

      thread_get_manager ()->push_task (pool, new suspend_task (waiter, THREAD_PGBUF_SUSPENDED));
      require_task_started (waiter, "non-eligible waiter did not start");
      require_test (wait_until ([&waiter]
      {
	return entry_is_waiting (waiter.entry.load (), THREAD_PGBUF_SUSPENDED);
      }, std::chrono::seconds (1)), "non-eligible waiter did not enter wait state");

      thread_get_manager ()->push_task (pool, new hold_slot_task (queued));
      require_task_not_started (queued, std::chrono::milliseconds (300),
				"task started while non-eligible waiter held the only slot");
      require_test (entry_has_slot (waiter.entry.load ()), "non-eligible waiter's slot was stolen");

      wake_task (waiter, THREAD_PGBUF_RESUMED);
      require_task_started (queued, "queued task did not start after non-eligible waiter resumed");
      queued.release.signal ();

      require_test (wait_until ([&waiter, &queued]
      {
	return waiter.done.load () && queued.done.load ();
      }, std::chrono::seconds (5)), "non-eligible wait scenario did not finish");

      destroy_pool (pool);
      print_pass ("non-eligible wait state is not stolen");
    }

    void
    test_css_wait_is_stolen ()
    {
      daemon_guard daemon (true);
      elastic_test_state waiter;
      elastic_test_state holder;
      elastic_pool_type *pool =
	      create_pool (ELASTIC_DEFAULT_POOL_ENTRIES, ELASTIC_ONE_SLOT, ELASTIC_TWO_WORKERS);

      thread_get_manager ()->push_task (pool, new suspend_task (waiter, THREAD_CSS_QUEUE_SUSPENDED));
      require_task_started (waiter, "CSS waiter did not start");
      require_test (wait_until ([&waiter]
      {
	return entry_is_waiting (waiter.entry.load (), THREAD_CSS_QUEUE_SUSPENDED);
      }, std::chrono::seconds (1)), "CSS waiter did not enter wait state");

      thread_get_manager ()->push_task (pool, new hold_slot_task (holder));
      require_task_started (holder, "holder did not start after CSS slot stealing");
      require_test (holder.had_slot.load (), "holder did not receive stolen CSS slot");
      require_test (!entry_has_slot (waiter.entry.load ()), "CSS waiter's slot was not stolen");

      wake_task (waiter, THREAD_CSS_QUEUE_RESUMED);
      require_test (wait_until ([&waiter]
      {
	return entry_is_slot_waiting (waiter.entry.load ());
      }, std::chrono::seconds (1)), "CSS waiter did not wait to reacquire a slot");

      holder.release.signal ();
      require_test (wait_until ([&waiter, &holder]
      {
	return waiter.done.load () && holder.done.load ();
      }, std::chrono::seconds (5)), "CSS steal scenario did not finish");

      destroy_pool (pool);
      print_pass ("CSS queue wait is stealable");
    }

    void
    test_lock_wait_slot_steal_and_worker_shrink ()
    {
      daemon_guard daemon (true);
      elastic_test_state waiter;
      elastic_test_state holder;

      // The daemon is responsible for stealing slots from entries that stay in an eligible wait state long enough.
      // One slot and up to two workers are enough to force the interesting transition:
      // task A owns the only slot, task A waits, the daemon steals the slot, task B starts on a new worker.
      elastic_pool_type *pool =
	      create_pool (ELASTIC_DEFAULT_POOL_ENTRIES, ELASTIC_ONE_SLOT, ELASTIC_TWO_WORKERS);

      require_test (pool != nullptr, "failed to create elastic worker pool");
      print_pass ("created elastic worker pool");

      // Phase 1: run task A and hold it in an elastic-slot-stealable lock wait.
      thread_get_manager ()->push_task (pool, new suspend_task (waiter, THREAD_LOCK_SUSPENDED));

      require_task_started (waiter, "waiter task did not start");
      require_test (waiter.had_slot.load (), "waiter task did not receive a concurrency slot");

      cubthread::entry *waiter_entry = waiter.entry.load ();
      require_test (waiter_entry != nullptr, "waiter task did not publish its entry");
      require_test (wait_until ([waiter_entry]
      {
	return entry_is_lock_waiting (waiter_entry);
      }, std::chrono::seconds (1)), "waiter task did not enter lock wait");
      print_pass ("waiter entered lock wait with a concurrency slot");

      // Phase 2: push task B. With max_concurrency == 1, B can run only after the daemon steals A's slot and the pool
      // creates the second worker, up to max_worker.
      thread_get_manager ()->push_task (pool, new hold_slot_task (holder));

      require_task_started (holder, "holder task did not start after slot stealing");
      require_test (holder.had_slot.load (), "holder task did not receive the stolen slot");
      require_test (!entry_has_slot (waiter_entry), "waiter entry still has a slot after holder started");

      require_test (wait_until ([pool]
      {
	const runtime_stats stats = read_runtime_stats (pool);

	return stats.total_slots == ELASTIC_ONE_SLOT
	&& stats.target_slots == ELASTIC_ONE_SLOT
	&& stats.busy_slots == ELASTIC_ONE_SLOT
	&& stats.total_workers == ELASTIC_TWO_WORKERS
	&& stats.target_workers == ELASTIC_TWO_WORKERS;
      }, std::chrono::seconds (1)), "runtime stats did not show stolen busy slot and overcommitted worker");
      print_pass ("slot was stolen and an extra worker was started");

      // Phase 3: wake A. B still owns the only slot, so A must block in the nested concurrency-slot wait instead of
      // completing immediately.
      thread_wakeup (waiter_entry, THREAD_LOCK_RESUMED);

      require_test (wait_until ([waiter_entry]
      {
	return entry_is_slot_waiting (waiter_entry);
      }, std::chrono::seconds (1)), "waiter task did not wait to reacquire a concurrency slot");
      require_test (!waiter.done.load (), "waiter task finished while holder still owned the only slot");
      print_pass ("resumed waiter waited to reacquire the slot");

      // Phase 4: release B. The stolen slot returns to the pool, A reacquires it, and both tasks can complete.
      holder.release.signal ();

      require_test (wait_until ([&waiter, &holder]
      {
	return holder.done.load () && waiter.done.load ();
      }, std::chrono::seconds (5)), "tasks did not finish after holder released the slot");

      require_test (wait_until ([pool]
      {
	return read_runtime_stats (pool).busy_slots == 0;
      }, std::chrono::seconds (1)), "slot remained busy after all tasks finished");
      print_pass ("tasks finished and slot became idle");

      // Phase 5: the overcommitted worker is not needed anymore. After the idle timeout, worker count should shrink
      // back to the target concurrency.
      require_test (wait_until ([pool]
      {
	return read_runtime_stats (pool).total_workers == ELASTIC_ONE_WORKER;
      }, std::chrono::seconds (5)), "overcommitted worker did not retire after idle timeout");
      print_pass ("overcommitted worker retired");

      destroy_pool (pool);
    }

    void
    test_max_worker_cap ()
    {
      daemon_guard daemon (true);
      elastic_test_state first_waiter;
      elastic_test_state second_waiter;
      elastic_test_state capped_task;
      elastic_pool_type *pool =
	      create_pool (ELASTIC_DEFAULT_POOL_ENTRIES, ELASTIC_ONE_SLOT, ELASTIC_TWO_WORKERS);

      thread_get_manager ()->push_task (pool, new suspend_task (first_waiter, THREAD_LOCK_SUSPENDED));
      require_task_started (first_waiter, "first waiter did not start");
      require_test (wait_until ([&first_waiter]
      {
	return entry_is_lock_waiting (first_waiter.entry.load ());
      }, std::chrono::seconds (1)), "first waiter did not enter lock wait");

      thread_get_manager ()->push_task (pool, new suspend_task (second_waiter, THREAD_LOCK_SUSPENDED));
      require_task_started (second_waiter, "second waiter did not start through overcommit");
      require_test (wait_until ([&second_waiter]
      {
	return entry_is_lock_waiting (second_waiter.entry.load ());
      }, std::chrono::seconds (1)), "second waiter did not enter lock wait");

      require_test (wait_until ([&second_waiter]
      {
	return !entry_has_slot (second_waiter.entry.load ());
      }, std::chrono::seconds (5)), "second waiter's slot was not stolen");

      thread_get_manager ()->push_task (pool, new hold_slot_task (capped_task));
      require_task_not_started (capped_task, std::chrono::milliseconds (300),
				"task started after max_worker was already reached");
      require_test (read_runtime_stats (pool).total_workers == ELASTIC_TWO_WORKERS, "worker cap was exceeded");

      wake_task (second_waiter, THREAD_LOCK_RESUMED);
      require_task_started (capped_task, "capped task did not start after a worker became reusable");
      capped_task.release.signal ();
      wake_task (first_waiter, THREAD_LOCK_RESUMED);

      require_test (wait_until ([&first_waiter, &second_waiter, &capped_task]
      {
	return first_waiter.done.load () && second_waiter.done.load () && capped_task.done.load ();
      }, std::chrono::seconds (5)), "max worker cap scenario did not finish");

      destroy_pool (pool);
      print_pass ("worker count is capped by max_worker");
    }

    void
    test_runtime_parameter_increase ()
    {
      elastic_test_state first;
      elastic_test_state second;
      elastic_pool_type *pool =
	      create_pool (ELASTIC_DEFAULT_POOL_ENTRIES, ELASTIC_ONE_SLOT, ELASTIC_ONE_WORKER);

      pool->adjust_runtime_parameter (ELASTIC_TWO_SLOTS, ELASTIC_TWO_WORKERS);
      require_test (pool->get_max_concurrency () == ELASTIC_TWO_SLOTS, "increased concurrency was not stored");
      require_test (pool->get_max_worker () == ELASTIC_TWO_WORKERS, "increased max worker was not stored");

      require_test (wait_until ([pool]
      {
	const runtime_stats stats = read_runtime_stats (pool);

	return stats.target_slots == ELASTIC_TWO_SLOTS && stats.target_workers == ELASTIC_TWO_WORKERS;
      }, std::chrono::seconds (1)), "increased runtime parameters were not propagated to stats");

      thread_get_manager ()->push_task (pool, new hold_slot_task (first));
      thread_get_manager ()->push_task (pool, new hold_slot_task (second));

      require_task_started (first, "first task did not start after parameter increase");
      require_task_started (second, "second task did not start after parameter increase");
      require_test (first.had_slot.load () && second.had_slot.load (), "increased slots were not usable");

      first.release.signal ();
      second.release.signal ();
      require_test (wait_until ([&first, &second]
      {
	return first.done.load () && second.done.load ();
      }, std::chrono::seconds (5)), "runtime increase scenario did not finish");

      destroy_pool (pool);
      print_pass ("runtime parameter increase is applied");
    }

    void
    test_runtime_parameter_decrease ()
    {
      elastic_test_state first;
      elastic_test_state second;
      elastic_pool_type *pool =
	      create_pool (ELASTIC_DEFAULT_POOL_ENTRIES, ELASTIC_TWO_SLOTS, ELASTIC_TWO_WORKERS);

      thread_get_manager ()->push_task (pool, new hold_slot_task (first));
      thread_get_manager ()->push_task (pool, new hold_slot_task (second));

      require_task_started (first, "first task did not start before parameter decrease");
      require_task_started (second, "second task did not start before parameter decrease");

      pool->adjust_runtime_parameter (ELASTIC_ONE_SLOT, ELASTIC_ONE_WORKER);
      require_test (pool->get_max_concurrency () == ELASTIC_ONE_SLOT, "decreased concurrency was not stored");
      require_test (pool->get_max_worker () == ELASTIC_ONE_WORKER, "decreased max worker was not stored");

      require_test (read_runtime_stats (pool).target_slots == ELASTIC_ONE_SLOT, "decreased slot target not visible");
      require_test (read_runtime_stats (pool).target_workers == ELASTIC_ONE_WORKER,
		    "decreased worker target not visible");

      first.release.signal ();
      second.release.signal ();

      require_test (wait_until ([pool]
      {
	const runtime_stats stats = read_runtime_stats (pool);

	return stats.busy_slots == 0 && stats.total_slots == ELASTIC_ONE_SLOT;
      }, std::chrono::seconds (5)), "slots did not converge to decreased target");

      require_test (wait_until ([pool]
      {
	return read_runtime_stats (pool).total_workers == ELASTIC_ONE_WORKER;
      }, std::chrono::seconds (5)), "workers did not converge to decreased target");

      destroy_pool (pool);
      print_pass ("runtime parameter decrease is applied after busy work drains");
    }

    void
    test_fifo_order ()
    {
      elastic_test_state first;
      elastic_test_state second;
      elastic_test_state third;
      std::atomic_int order_counter { 0 };
      elastic_pool_type *pool =
	      create_pool (ELASTIC_DEFAULT_POOL_ENTRIES, ELASTIC_ONE_SLOT, ELASTIC_ONE_WORKER);

      thread_get_manager ()->push_task (pool, new hold_slot_task (first, true, &order_counter));
      require_task_started (first, "first FIFO task did not start");

      thread_get_manager ()->push_task (pool, new hold_slot_task (second, true, &order_counter));
      thread_get_manager ()->push_task (pool, new hold_slot_task (third, false, &order_counter));
      require_task_not_started (second, std::chrono::milliseconds (200), "second FIFO task started too early");
      require_task_not_started (third, std::chrono::milliseconds (200), "third FIFO task started too early");

      first.release.signal ();
      require_task_started (second, "second FIFO task did not start after first released the slot");
      require_task_not_started (third, std::chrono::milliseconds (200), "third FIFO task bypassed second task");

      second.release.signal ();
      require_task_started (third, "third FIFO task did not start after second released the slot");

      require_test (wait_until ([&first, &second, &third]
      {
	return first.done.load () && second.done.load () && third.done.load ();
      }, std::chrono::seconds (5)), "FIFO scenario did not finish");
      require_test (first.order.load () == 1 && second.order.load () == 2 && third.order.load () == 3,
		    "task execution order was not FIFO");

      destroy_pool (pool);
      print_pass ("queued tasks keep FIFO order");
    }

    void
    test_stop_retires_queued_and_rejects_new_tasks ()
    {
      elastic_test_state running;
      elastic_test_state queued;
      elastic_test_state rejected;
      elastic_pool_type *pool =
	      create_pool (ELASTIC_DEFAULT_POOL_ENTRIES, ELASTIC_ONE_SLOT, ELASTIC_ONE_WORKER);

      thread_get_manager ()->push_task (pool, new hold_slot_task (running));
      require_task_started (running, "running task did not start before stop");

      thread_get_manager ()->push_task (pool, new hold_slot_task (queued));
      require_task_not_started (queued, std::chrono::milliseconds (200), "queued task started before stop");

      std::thread releaser ([&running] ()
      {
	std::this_thread::sleep_for (std::chrono::milliseconds (100));
	running.release.signal ();
      });

      pool->stop_execution ();
      releaser.join ();

      require_test (running.done.load (), "running task did not finish during stop");
      require_test (!queued.executed.load () && queued.retired.load (), "queued task was not retired during stop");

      thread_get_manager ()->push_task (pool, new hold_slot_task (rejected, false));
      require_test (!rejected.executed.load () && rejected.retired.load (), "new task was not rejected after stop");

      destroy_pool (pool);
      print_pass ("stop retires queued tasks and rejects new tasks");
    }

    void
    test_timeout_returns_slot ()
    {
      elastic_test_state waiter;
      elastic_test_state next;
      elastic_pool_type *pool =
	      create_pool (ELASTIC_DEFAULT_POOL_ENTRIES, ELASTIC_ONE_SLOT, ELASTIC_ONE_WORKER);

      thread_get_manager ()->push_task (pool, new timed_suspend_task (waiter, std::chrono::milliseconds (20)));
      require_task_started (waiter, "timed waiter did not start");

      require_test (wait_until ([&waiter]
      {
	return waiter.done.load ();
      }, std::chrono::seconds (1)), "timed waiter did not finish");
      require_test (waiter.had_slot.load (), "timed waiter did not receive a slot");
      require_test (read_runtime_stats (pool).busy_slots == 0, "timed-out waiter leaked a busy slot");

      thread_get_manager ()->push_task (pool, new hold_slot_task (next));
      require_task_started (next, "next task did not receive slot after timeout");
      next.release.signal ();

      require_test (wait_until ([&next]
      {
	return next.done.load ();
      }, std::chrono::seconds (5)), "timeout follow-up task did not finish");

      destroy_pool (pool);
      print_pass ("timeout returns the held slot");
    }

    void
    test_interrupt_releases_held_slot ()
    {
      elastic_test_state waiter;
      elastic_test_state next;
      elastic_pool_type *pool =
	      create_pool (ELASTIC_DEFAULT_POOL_ENTRIES, ELASTIC_ONE_SLOT, ELASTIC_ONE_WORKER);

      thread_get_manager ()->push_task (pool, new suspend_task (waiter, THREAD_LOCK_SUSPENDED));
      require_task_started (waiter, "interrupt waiter did not start");
      require_test (wait_until ([&waiter]
      {
	return entry_is_lock_waiting (waiter.entry.load ());
      }, std::chrono::seconds (1)), "interrupt waiter did not enter lock wait");

      wake_task (waiter, THREAD_RESUME_DUE_TO_INTERRUPT);

      require_test (wait_until ([&waiter]
      {
	return waiter.done.load ();
      }, std::chrono::seconds (1)), "interrupted waiter did not finish");
      require_test (read_runtime_stats (pool).busy_slots == 0, "interrupted waiter leaked a held slot");

      thread_get_manager ()->push_task (pool, new hold_slot_task (next));
      require_task_started (next, "next task did not start after interrupt released the slot");
      next.release.signal ();

      require_test (wait_until ([&next]
      {
	return next.done.load ();
      }, std::chrono::seconds (5)), "interrupt follow-up task did not finish");

      destroy_pool (pool);
      print_pass ("interrupt releases a held slot");
    }

    void
    test_interrupt_while_waiting_to_reacquire_slot ()
    {
      daemon_guard daemon (true);
      elastic_test_state waiter;
      elastic_test_state holder;
      elastic_pool_type *pool =
	      create_pool (ELASTIC_DEFAULT_POOL_ENTRIES, ELASTIC_ONE_SLOT, ELASTIC_TWO_WORKERS);

      thread_get_manager ()->push_task (pool, new suspend_task (waiter, THREAD_LOCK_SUSPENDED));
      require_task_started (waiter, "reacquire-interrupt waiter did not start");
      require_test (wait_until ([&waiter]
      {
	return entry_is_lock_waiting (waiter.entry.load ());
      }, std::chrono::seconds (1)), "reacquire-interrupt waiter did not enter lock wait");

      thread_get_manager ()->push_task (pool, new hold_slot_task (holder));
      require_task_started (holder, "holder did not start before reacquire interrupt");
      require_test (!entry_has_slot (waiter.entry.load ()), "waiter slot was not stolen before reacquire interrupt");

      wake_task (waiter, THREAD_LOCK_RESUMED);
      require_test (wait_until ([&waiter]
      {
	return entry_is_slot_waiting (waiter.entry.load ());
      }, std::chrono::seconds (1)), "waiter did not enter slot reacquire wait");

      wake_task (waiter, THREAD_RESUME_DUE_TO_INTERRUPT);
      require_test (wait_until ([&waiter]
      {
	return waiter.done.load ();
      }, std::chrono::seconds (1)), "waiter did not finish after reacquire interrupt");

      holder.release.signal ();
      require_test (wait_until ([&holder]
      {
	return holder.done.load ();
      }, std::chrono::seconds (5)), "holder did not finish after reacquire interrupt");
      require_test (wait_until ([pool]
      {
	return read_runtime_stats (pool).busy_slots == 0;
      }, std::chrono::seconds (1)), "reacquire interrupt leaked a slot");

      destroy_pool (pool);
      print_pass ("interrupt while reacquiring a slot does not leak slots");
    }

    void
    test_daemon_is_required_for_slot_steal ()
    {
      daemon_guard daemon;
      elastic_test_state waiter;
      elastic_test_state holder;
      elastic_pool_type *pool =
	      create_pool (ELASTIC_DEFAULT_POOL_ENTRIES, ELASTIC_ONE_SLOT, ELASTIC_TWO_WORKERS);

      thread_get_manager ()->push_task (pool, new suspend_task (waiter, THREAD_LOCK_SUSPENDED));
      require_task_started (waiter, "daemon-required waiter did not start");
      require_test (wait_until ([&waiter]
      {
	return entry_is_lock_waiting (waiter.entry.load ());
      }, std::chrono::seconds (1)), "daemon-required waiter did not enter lock wait");

      thread_get_manager ()->push_task (pool, new hold_slot_task (holder));
      require_task_not_started (holder, std::chrono::milliseconds (300),
				"slot was stolen even though the daemon was not running");
      require_test (entry_has_slot (waiter.entry.load ()), "waiter's slot changed before daemon start");

      daemon.start ();
      require_task_started (holder, "holder did not start after daemon was started");
      require_test (!entry_has_slot (waiter.entry.load ()), "daemon did not steal the waiter's slot");

      wake_task (waiter, THREAD_LOCK_RESUMED);
      holder.release.signal ();

      require_test (wait_until ([&waiter, &holder]
      {
	return waiter.done.load () && holder.done.load ();
      }, std::chrono::seconds (5)), "daemon-required scenario did not finish");

      destroy_pool (pool);
      print_pass ("daemon is required for stealing slots");
    }

    void
    test_pool_threads_keep_target_workers_alive ()
    {
      // Case 1: pool_threads=true with infinite idle timeout keeps every target worker alive.
      {
	elastic_pool_type *pool =
		create_pool (ELASTIC_DEFAULT_POOL_ENTRIES, ELASTIC_TWO_SLOTS, ELASTIC_TWO_WORKERS, true,
			     cubthread::wait_seconds ());

	require_test (read_runtime_stats (pool).total_workers == ELASTIC_TWO_WORKERS,
		      "pool_threads did not create target workers");
	std::this_thread::sleep_for (std::chrono::milliseconds (500));
	require_test (read_runtime_stats (pool).total_workers == ELASTIC_TWO_WORKERS,
		      "pool_threads workers retired while idle with infinite timeout");

	destroy_pool (pool);
      }

      // Case 2: pool_threads=true with a finite idle timeout. The non-persistent worker thread
      // times out and retires, shrinking total_workers. This complement confirms that case 1
      // actually exercised live threads (otherwise no retire could occur regardless of timeout).
      {
	elastic_pool_type *pool =
		create_pool (ELASTIC_DEFAULT_POOL_ENTRIES, ELASTIC_TWO_SLOTS, ELASTIC_TWO_WORKERS, true,
			     cubthread::wait_seconds (std::chrono::seconds (1)));

	require_test (read_runtime_stats (pool).total_workers == ELASTIC_TWO_WORKERS,
		      "pool_threads did not create target workers");
	require_test (wait_until ([pool]
	{
	  return read_runtime_stats (pool).total_workers == ELASTIC_ONE_WORKER;
	}, std::chrono::seconds (5)), "non-persistent pool thread did not retire after idle timeout");

	destroy_pool (pool);
      }

      print_pass ("pool_threads keeps target workers alive only with infinite idle timeout");
    }

    void
    test_multi_core_surplus_slot_rebalance ()
    {
      daemon_guard daemon (true);
      elastic_test_state first;
      elastic_test_state second;
      elastic_test_state third;
      constexpr std::size_t core_count = 2;
      constexpr std::size_t max_concurrency = 4;
      constexpr std::size_t max_worker = 6;
      elastic_pool_type *pool = create_pool_with_cores (ELASTIC_DEFAULT_POOL_ENTRIES * 2, core_count,
				max_concurrency, max_worker);

      // Core 0 consumes both of its local slots. Core 1 starts with two idle slots, which become surplus after the
      // daemon's surplus threshold and can be moved to core 0.
      thread_get_manager ()->push_task_on_core (pool, new hold_slot_task (first), 0);
      thread_get_manager ()->push_task_on_core (pool, new hold_slot_task (second), 0);
      require_task_started (first, "first multi-core task did not start");
      require_task_started (second, "second multi-core task did not start");

      thread_get_manager ()->push_task_on_core (pool, new hold_slot_task (third), 0);
      require_task_not_started (third, std::chrono::milliseconds (500),
				"third multi-core task started before surplus rebalance");

      require_task_started (third, "third multi-core task did not start after surplus rebalance");

      first.release.signal ();
      second.release.signal ();
      third.release.signal ();

      require_test (wait_until ([&first, &second, &third]
      {
	return first.done.load () && second.done.load () && third.done.load ();
      }, std::chrono::seconds (5)), "multi-core surplus rebalance scenario did not finish");

      destroy_pool (pool);
      print_pass ("multi-core surplus slot rebalance wakes queued work");
    }

    void
    test_complex_concurrent_reacquire_and_runtime_adjust ()
    {
      daemon_guard daemon (true);
      elastic_test_state waiter_core0;
      elastic_test_state waiter_core1;
      elastic_test_state holder_core0;
      elastic_test_state holder_core1;
      elastic_test_state surge_core0;
      elastic_test_state surge_core1;
      constexpr std::size_t core_count = 2;
      constexpr std::size_t initial_concurrency = 2;
      constexpr std::size_t initial_workers = 4;
      constexpr std::size_t increased_concurrency = 4;
      constexpr std::size_t increased_workers = 6;
      elastic_pool_type *pool = create_pool_with_cores (ELASTIC_DEFAULT_POOL_ENTRIES * 2, core_count,
				initial_concurrency, initial_workers);

      // One waiter per core consumes all initial slots. The daemon must steal both slots before the holder tasks can
      // start, and each core may overcommit one extra worker up to the initial max_worker.
      thread_get_manager ()->push_task_on_core (pool, new suspend_task (waiter_core0, THREAD_LOCK_SUSPENDED), 0);
      thread_get_manager ()->push_task_on_core (pool, new suspend_task (waiter_core1, THREAD_LOCK_SUSPENDED), 1);
      require_task_started (waiter_core0, "core 0 waiter did not start");
      require_task_started (waiter_core1, "core 1 waiter did not start");
      require_test (wait_until ([&waiter_core0, &waiter_core1]
      {
	return entry_is_lock_waiting (waiter_core0.entry.load ())
	&& entry_is_lock_waiting (waiter_core1.entry.load ());
      }, std::chrono::seconds (1)), "complex waiters did not enter lock wait");

      require_test (wait_until ([&waiter_core0, &waiter_core1]
      {
	return !entry_has_slot (waiter_core0.entry.load ()) && !entry_has_slot (waiter_core1.entry.load ());
      }, std::chrono::seconds (5)), "complex waiters did not have their slots stolen");

      thread_get_manager ()->push_task_on_core (pool, new hold_slot_task (holder_core0), 0);
      thread_get_manager ()->push_task_on_core (pool, new hold_slot_task (holder_core1), 1);
      require_task_started (holder_core0, "core 0 holder did not start after slot stealing");
      require_task_started (holder_core1, "core 1 holder did not start after slot stealing");
      require_test (!entry_has_slot (waiter_core0.entry.load ()), "core 0 waiter still has a stolen slot");
      require_test (!entry_has_slot (waiter_core1.entry.load ()), "core 1 waiter still has a stolen slot");

      require_test (wait_until ([pool]
      {
	const runtime_stats stats = read_runtime_stats (pool);

	return stats.total_slots == initial_concurrency
	&& stats.busy_slots == initial_concurrency
	&& stats.total_workers == initial_workers;
      }, std::chrono::seconds (1)), "complex scenario did not reach initial overcommit state");

      // Resume both waiters while the holders still own all slots. Both waiters must block on slot reacquisition.
      wake_task (waiter_core0, THREAD_LOCK_RESUMED);
      wake_task (waiter_core1, THREAD_LOCK_RESUMED);
      require_test (wait_until ([&waiter_core0, &waiter_core1]
      {
	return entry_is_slot_waiting (waiter_core0.entry.load ())
	&& entry_is_slot_waiting (waiter_core1.entry.load ());
      }, std::chrono::seconds (1)), "complex waiters did not wait to reacquire slots");
      require_test (!waiter_core0.done.load () && !waiter_core1.done.load (),
		    "complex waiters finished before more slots were added");

      // Increase concurrency while waiters are blocked in the nested slot wait. The newly added slots should wake both
      // waiters without releasing the holder tasks.
      pool->adjust_runtime_parameter (increased_concurrency, increased_workers);
      require_test (wait_until ([&waiter_core0, &waiter_core1]
      {
	return waiter_core0.done.load () && waiter_core1.done.load ();
      }, std::chrono::seconds (5)), "complex waiters did not finish after runtime concurrency increase");
      require_test (!holder_core0.done.load () && !holder_core1.done.load (),
		    "complex holders finished before their gates were released");

      require_test (wait_until ([pool]
      {
	const runtime_stats stats = read_runtime_stats (pool);

	return stats.target_slots == increased_concurrency
	&& stats.target_workers == increased_workers
	&& stats.busy_slots == initial_concurrency;
      }, std::chrono::seconds (1)), "complex runtime increase was not reflected in stats");

      // Use the extra capacity for a second wave of work while the original holders are still running.
      thread_get_manager ()->push_task_on_core (pool, new hold_slot_task (surge_core0), 0);
      thread_get_manager ()->push_task_on_core (pool, new hold_slot_task (surge_core1), 1);
      require_task_started (surge_core0, "core 0 surge task did not start with increased concurrency");
      require_task_started (surge_core1, "core 1 surge task did not start with increased concurrency");
      require_test (wait_until ([pool]
      {
	return read_runtime_stats (pool).busy_slots == increased_concurrency;
      }, std::chrono::seconds (1)), "complex surge did not occupy all increased slots");

      // Decrease the targets while all increased slots are busy. The targets should change immediately, but busy slots
      // should drain naturally as tasks finish instead of being forcibly revoked.
      pool->adjust_runtime_parameter (initial_concurrency, initial_workers);
      require_test (wait_until ([pool]
      {
	const runtime_stats stats = read_runtime_stats (pool);

	return stats.target_slots == initial_concurrency
	&& stats.target_workers == initial_workers
	&& stats.busy_slots == increased_concurrency;
      }, std::chrono::seconds (1)), "complex runtime decrease did not preserve busy slots until drain");

      surge_core0.release.signal ();
      surge_core1.release.signal ();
      holder_core0.release.signal ();
      holder_core1.release.signal ();

      require_test (wait_until ([&holder_core0, &holder_core1, &surge_core0, &surge_core1]
      {
	return holder_core0.done.load () && holder_core1.done.load ()
	&& surge_core0.done.load () && surge_core1.done.load ();
      }, std::chrono::seconds (5)), "complex concurrent workload did not drain");

      require_test (wait_until ([pool]
      {
	const runtime_stats stats = read_runtime_stats (pool);

	return stats.busy_slots == 0 && stats.total_slots == initial_concurrency;
      }, std::chrono::seconds (5)), "complex slots did not converge after drain");

      require_test (wait_until ([pool]
      {
	return read_runtime_stats (pool).total_workers == initial_concurrency;
      }, std::chrono::seconds (5)), "complex overcommitted workers did not retire after drain");

      destroy_pool (pool);
      print_pass ("complex concurrent steal, reacquire, resize, drain and shrink scenario passed");
    }

    void
    test_long_running_mixed_load ()
    {
      daemon_guard daemon (true);
      load_test_state state;
      constexpr std::size_t core_count = 2;
      constexpr std::size_t load_concurrency = 4;
      constexpr std::size_t load_max_workers = 8;
      constexpr std::size_t producer_count = 4;
      constexpr std::size_t min_submitted_tasks = producer_count * 100;
      constexpr auto load_duration = std::chrono::seconds (5);
      constexpr auto producer_interval = std::chrono::milliseconds (30);
      const auto load_end = std::chrono::steady_clock::now () + load_duration;
      elastic_pool_type *pool =
	      create_pool_with_cores (load_max_workers, core_count, load_concurrency, load_max_workers);
      std::vector<std::thread> producers;

      for (std::size_t producer = 0; producer < producer_count; producer++)
	{
	  producers.emplace_back ([pool, &state, load_end, producer_interval]
	  {
	    while (std::chrono::steady_clock::now () < load_end)
	      {
		const std::size_t task_id = state.submitted.fetch_add (1);

		thread_get_manager ()->push_task_on_core (pool, new mixed_load_task (state, task_id),
							  task_id % core_count);
		std::this_thread::sleep_for (producer_interval);
	      }
	  });
	}

      auto sample_runtime_stats = [pool, &state]
      {
	const runtime_stats stats = read_runtime_stats (pool);

	update_maximum (state.max_busy_slots_seen, stats.busy_slots);
	update_maximum (state.max_workers_seen, stats.total_workers);

	require_test (stats.target_slots == load_concurrency, "load test changed slot target unexpectedly");
	require_test (stats.target_workers == load_max_workers, "load test changed worker target unexpectedly");
	require_test (stats.busy_slots <= stats.total_slots, "load test observed more busy slots than total slots");
	require_test (stats.busy_workers <= stats.total_workers,
		      "load test observed more busy workers than total workers");
	require_test (stats.total_workers <= load_max_workers, "load test exceeded max worker cap");
      };

      while (std::chrono::steady_clock::now () < load_end)
	{
	  sample_runtime_stats ();
	  std::this_thread::sleep_for (std::chrono::milliseconds (10));
	}

      for (std::thread &producer : producers)
	{
	  producer.join ();
	}

      const std::size_t total_tasks = state.submitted.load ();
      const auto drain_deadline = std::chrono::steady_clock::now () + std::chrono::seconds (30);

      while (state.completed.load () < total_tasks && std::chrono::steady_clock::now () < drain_deadline)
	{
	  sample_runtime_stats ();
	  std::this_thread::sleep_for (std::chrono::milliseconds (10));
	}

      require_test (total_tasks >= min_submitted_tasks, "load test did not keep producers active long enough");
      require_test (state.started.load () == total_tasks, "load test did not start all tasks");
      require_test (state.completed.load () == total_tasks, "load test did not complete all tasks before deadline");
      require_test (wait_until ([&state, total_tasks]
      {
	return state.retired.load () == total_tasks;
      }, std::chrono::seconds (5)), "load test did not retire all tasks");
      require_test (state.missing_initial_slot.load () == 0, "load test task started without a slot");

      require_test (state.max_busy_slots_seen.load () >= load_concurrency,
		    "load test never saturated all concurrency slots");
      require_test (state.max_running.load () > load_concurrency, "load test did not overcommit running workers");
      require_test (state.max_workers_seen.load () > load_concurrency, "load test did not create elastic workers");
      require_test (state.max_workers_seen.load () <= load_max_workers, "load test recorded too many workers");

      require_test (wait_until ([pool]
      {
	const runtime_stats stats = read_runtime_stats (pool);

	return stats.busy_slots == 0 && stats.total_slots == load_concurrency;
      }, std::chrono::seconds (5)), "load test slots did not drain cleanly");

      require_test (wait_until ([pool]
      {
	return read_runtime_stats (pool).total_workers == load_concurrency;
      }, std::chrono::seconds (10)), "load test elastic workers did not shrink after idle timeout");

      destroy_pool (pool);
      std::cout << "    OK - long-running mixed producer load passed (" << total_tasks
		<< " tasks over " << load_duration.count () << "s load)" << std::endl;
    }

    void
    test_large_scale_adjusting_producer_load ()
    {
      daemon_guard daemon (true);
      load_test_state state;
      constexpr std::size_t core_count = 36;
      constexpr std::size_t producer_count = 100;
      constexpr std::size_t base_concurrency = 36;
      constexpr std::size_t peak_concurrency = 90;
      constexpr std::size_t max_worker_cap = 108;
      constexpr std::size_t min_submitted_tasks = 3000;
      constexpr auto load_duration = std::chrono::seconds (3);
      constexpr auto producer_interval = std::chrono::milliseconds (50);
      constexpr auto adjust_interval = std::chrono::milliseconds (80);
      const std::vector<runtime_adjustment_step> adjust_steps =
      {
	{ 36, 72 },
	{ 72, 108 },
	{ 54, 90 },
	{ 90, 108 },
	{ 45, 72 },
	{ 63, 108 }
      };
      const auto load_start = std::chrono::steady_clock::now ();
      const auto load_end = load_start + load_duration;
      elastic_pool_type *pool = create_pool_with_cores (max_worker_cap, core_count, base_concurrency, max_worker_cap);
      std::vector<std::thread> producers;

      auto sample_runtime_stats = [pool, &state]
      {
	const runtime_stats stats = read_runtime_stats (pool);
	const std::size_t submitted = state.submitted.load ();
	const std::size_t completed = state.completed.load ();

	update_maximum (state.max_busy_slots_seen, stats.busy_slots);
	update_maximum (state.max_workers_seen, stats.total_workers);
	if (submitted >= completed)
	  {
	    update_maximum (state.max_pending, submitted - completed);
	  }
	state.runtime_samples.fetch_add (1);

	require_test (stats.target_slots >= base_concurrency, "large load target slots dropped below core count");
	require_test (stats.target_slots <= peak_concurrency, "large load target slots exceeded peak concurrency");
	require_test (stats.target_workers >= base_concurrency, "large load target workers dropped below core count");
	require_test (stats.target_workers <= max_worker_cap, "large load target workers exceeded cap");
	require_test (stats.total_slots <= peak_concurrency, "large load total slots exceeded peak concurrency");
	require_test (stats.busy_slots <= stats.total_slots, "large load observed more busy slots than total slots");
	require_test (stats.busy_workers <= stats.total_workers,
		      "large load observed more busy workers than total workers");
	require_test (stats.total_workers <= max_worker_cap, "large load exceeded max worker cap");
      };

      for (std::size_t producer = 0; producer < producer_count; producer++)
	{
	  producers.emplace_back ([pool, &state, load_end, producer_interval, producer]
	  {
	    while (std::chrono::steady_clock::now () < load_end)
	      {
		const std::size_t task_id = state.submitted.fetch_add (1);
		const std::size_t core_hash = (task_id + producer) % core_count;

		thread_get_manager ()->push_task_on_core (pool, new mixed_load_task (state, task_id), core_hash);
		std::this_thread::sleep_for (producer_interval);
	      }
	  });
	}

      std::thread adjuster ([pool, &state, &adjust_steps, load_end, adjust_interval]
      {
	std::size_t step_index = 0;

	while (std::chrono::steady_clock::now () < load_end)
	  {
	    const runtime_adjustment_step &step = adjust_steps[step_index++ % adjust_steps.size ()];

	    pool->adjust_runtime_parameter (step.max_concurrency, step.max_worker);
	    state.adjust_count.fetch_add (1);
	    std::this_thread::sleep_for (adjust_interval);
	  }
      });

      while (std::chrono::steady_clock::now () < load_end)
	{
	  sample_runtime_stats ();
	  std::this_thread::sleep_for (std::chrono::milliseconds (10));
	}

      for (std::thread &producer : producers)
	{
	  producer.join ();
	}
      adjuster.join ();

      pool->adjust_runtime_parameter (peak_concurrency, max_worker_cap);

      const std::size_t total_tasks = state.submitted.load ();
      const auto drain_start = std::chrono::steady_clock::now ();
      const auto drain_deadline = drain_start + std::chrono::seconds (30);

      while (state.completed.load () < total_tasks && std::chrono::steady_clock::now () < drain_deadline)
	{
	  sample_runtime_stats ();
	  std::this_thread::sleep_for (std::chrono::milliseconds (10));
	}

      const auto drain_end = std::chrono::steady_clock::now ();
      const auto drain_millis = std::chrono::duration_cast<std::chrono::milliseconds> (drain_end - drain_start);
      const auto total_millis = std::chrono::duration_cast<std::chrono::milliseconds> (drain_end - load_start);

      require_test (total_tasks >= min_submitted_tasks, "large load did not submit enough tasks");
      require_test (state.adjust_count.load () >= 10, "large load did not adjust runtime parameters enough");
      require_test (state.runtime_samples.load () >= 100, "large load did not sample runtime stats enough");
      require_test (state.started.load () == total_tasks, "large load did not start all tasks");
      require_test (state.completed.load () == total_tasks, "large load did not complete all tasks before deadline");
      require_test (wait_until ([&state, total_tasks]
      {
	return state.retired.load () == total_tasks;
      }, std::chrono::seconds (5)), "large load did not retire all tasks");
      require_test (state.missing_initial_slot.load () == 0, "large load task started without a slot");
      require_test (state.max_busy_slots_seen.load () >= base_concurrency,
		    "large load never saturated base concurrency slots");
      require_test (state.max_workers_seen.load () > base_concurrency, "large load did not create elastic workers");
      require_test (state.max_workers_seen.load () <= max_worker_cap, "large load recorded too many workers");
      require_test (drain_millis <= std::chrono::seconds (25), "large load drain took too long");

      pool->adjust_runtime_parameter (base_concurrency, base_concurrency);

      require_test (wait_until ([pool]
      {
	const runtime_stats stats = read_runtime_stats (pool);

	return stats.busy_slots == 0 && stats.target_slots == base_concurrency;
      }, std::chrono::seconds (10)), "large load slots did not drain with base target concurrency");

      require_test (wait_until ([pool]
      {
	const runtime_stats stats = read_runtime_stats (pool);

	return stats.busy_workers == 0 && stats.target_workers == base_concurrency;
      }, std::chrono::seconds (15)), "large load workers did not become idle with base worker target");

      const runtime_stats final_stats = read_runtime_stats (pool);

      require_test (worker_stats_layout_is_counter_and_timer (),
		    "worker stats layout changed: update worker_stat_count_index assumptions");

      std::vector<cubperf::stat_value> worker_stats (elastic_pool_type::stats::get_count (), 0);

      pool->get_stats (worker_stats.data ());
      require_test (worker_stats[worker_stat_count_index<elastic_pool_type::stats::id::start_thread> ()]
		    >= base_concurrency, "large load worker stats did not record initial worker starts");
      require_test (worker_stats[worker_stat_count_index<elastic_pool_type::stats::id::execute_task> ()]
		    == total_tasks, "large load worker stats execute count mismatch");
      require_test (worker_stats[worker_stat_count_index<elastic_pool_type::stats::id::retire_task> ()]
		    == total_tasks, "large load worker stats retire count mismatch");
      require_test (worker_stats[worker_stat_count_index<elastic_pool_type::stats::id::found_in_queue> ()]
		    > 0, "large load worker stats did not record queued task pickup");
      require_test (worker_stats[worker_stat_count_index<elastic_pool_type::stats::id::wakeup_with_task> ()]
		    > 0, "large load worker stats did not record worker wakeups");

      destroy_pool (pool);
      std::cout << "    OK - large-scale adjusting producer load passed (" << total_tasks
		<< " tasks, " << producer_count << " producers, " << core_count
		<< " cores, adjusts " << state.adjust_count.load ()
		<< ", max workers " << state.max_workers_seen.load ()
		<< ", final workers " << final_stats.total_workers
		<< ", final busy workers " << final_stats.busy_workers
		<< ", max pending " << state.max_pending.load ()
		<< ", drain " << drain_millis.count () << "ms"
		<< ", total " << total_millis.count () << "ms)" << std::endl;
    }

    std::vector<cubperf::stat_value>
    read_worker_stats (elastic_pool_type *pool)
    {
      std::vector<cubperf::stat_value> values (elastic_pool_type::stats::get_count (), 0);

      pool->get_stats (values.data ());

      return values;
    }

    void
    test_retired_worker_stats_are_preserved ()
    {
      daemon_guard daemon (true);
      elastic_test_state waiter;
      elastic_test_state holder;
      elastic_pool_type *pool =
	      create_pool (ELASTIC_DEFAULT_POOL_ENTRIES, ELASTIC_ONE_SLOT, ELASTIC_TWO_WORKERS);

      thread_get_manager ()->push_task (pool, new suspend_task (waiter, THREAD_LOCK_SUSPENDED));
      require_task_started (waiter, "stats waiter did not start");

      thread_get_manager ()->push_task (pool, new hold_slot_task (holder));
      require_task_started (holder, "stats holder did not start");

      wake_task (waiter, THREAD_LOCK_RESUMED);
      holder.release.signal ();

      require_test (wait_until ([&waiter, &holder]
      {
	return waiter.done.load () && holder.done.load ();
      }, std::chrono::seconds (5)), "stats preservation tasks did not finish");

      const std::vector<cubperf::stat_value> before_retire = read_worker_stats (pool);

      require_test (wait_until ([pool]
      {
	return read_runtime_stats (pool).total_workers == ELASTIC_ONE_WORKER;
      }, std::chrono::seconds (5)), "stats worker did not retire");

      const std::vector<cubperf::stat_value> after_retire = read_worker_stats (pool);

      require_test (before_retire.size () == after_retire.size (), "stats size changed after worker retire");
      for (std::size_t i = 0; i < before_retire.size (); i++)
	{
	  require_test (after_retire[i] >= before_retire[i], "worker stats decreased after worker retire");
	}

      destroy_pool (pool);
      print_pass ("retired worker stats are preserved");
    }

  } // namespace

  int
  test_elastic_worker_pool (void)
  {
    test_initial_runtime_stats ();
    test_basic_slot_lifecycle ();
    test_queued_task_waits_for_slot_return ();
    test_short_wait_is_not_stolen ();
    test_non_eligible_wait_is_not_stolen ();
    test_css_wait_is_stolen ();
    test_lock_wait_slot_steal_and_worker_shrink ();
    test_max_worker_cap ();
    test_runtime_parameter_increase ();
    test_runtime_parameter_decrease ();
    test_fifo_order ();
    test_stop_retires_queued_and_rejects_new_tasks ();
    test_timeout_returns_slot ();
    test_interrupt_releases_held_slot ();
    test_interrupt_while_waiting_to_reacquire_slot ();
    test_daemon_is_required_for_slot_steal ();
    test_pool_threads_keep_target_workers_alive ();
    test_multi_core_surplus_slot_rebalance ();
    test_complex_concurrent_reacquire_and_runtime_adjust ();
    test_long_running_mixed_load ();
    test_large_scale_adjusting_producer_load ();
    test_retired_worker_stats_are_preserved ();

    std::cout << "  test_elastic_worker_pool OK" << std::endl;

    return 0;
  }

} // namespace test_thread
