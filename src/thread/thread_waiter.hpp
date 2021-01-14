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
 * thread_waiter - interface of suspending/waking threads
 */

#ifndef _THREAD_WAITER_HPP_
#define _THREAD_WAITER_HPP_

#include "perf_def.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

#include <cassert>
#include <cinttypes>

namespace cubthread
{
  // cubthread::waiter
  //
  //  description:
  //    usable to suspend thread and wait for a time or for wakeup request
  //
  //  how to use:
  //
  //    // Thread 1
  //    // this thread wants to wait indefinitely
  //    waiter_shared_variable->wait_inf ();
  //
  //    // Thread 2
  //    // wake Thread 1
  //    waiter_shared_variable->wakeup ();
  //
  //    // similarly, first thread can wait with timeout using wait_for or wait_until functions
  //
  class waiter
  {
    public:
      waiter ();
      ~waiter();

      void wakeup (void);                                             // wakeup waiter thread

      void wait_inf (void);                                           // wait until wakeup
      bool wait_for (const std::chrono::system_clock::duration &delta);   // wait for period of time or until wakeup
      // returns true if woke up before timeout
      bool wait_until (const std::chrono::system_clock::time_point &timeout_time);  // wait until time or until wakeup
      // returns true if woke up before timeout

      // statistics
      static std::size_t get_stats_value_count (void);
      static const char *get_stat_name (std::size_t stat_index);
      void get_stats (cubperf::stat_value *stats_out);

      bool is_running ();                                             // true, if running

    private:

      enum status       // waiter status
      {
	RUNNING,
	SLEEPING,
	AWAKENING,
      };

      inline bool check_wake (void);      // check wake condition; used to avoid spurious wakeups
      void goto_sleep (void);
      inline void awake (void);
      void run (void);

      std::mutex m_mutex;                 // mutex used to synchronize waiter states
      std::condition_variable m_condvar;  // condition variable used to wait/wakeup
      status m_status;                    // current status

      // stats
      cubperf::statset &m_stats;
      cubperf::atomic_stat_counter m_wakeup_calls;
      bool m_was_awaken;                  // used for statistics
  };

  // cubthread::wait_duration
  //
  //  description:
  template <class D>
  struct wait_duration
  {
    D m_duration;
    bool m_infinite;

    wait_duration ()
      : m_duration (0)
      , m_infinite (true)
    {
      //
    }

    wait_duration (const D &duration)
      : m_duration (duration)
      , m_infinite (false)
    {
      //
    }

    const wait_duration &operator= (const D &duration)
    {
      m_duration = duration;
      m_infinite = false;
    }

    void set_infinite_wait ();
    void set_duration (const D &duration);
  };
  using wait_seconds = wait_duration<std::chrono::seconds>;

  template <typename D>
  void condvar_wait (std::condition_variable &condvar, std::unique_lock<std::mutex> &lock,
		     const wait_duration<D> &duration);
  template <typename D, typename P>
  bool condvar_wait (std::condition_variable &condvar, std::unique_lock<std::mutex> &lock,
		     const wait_duration<D> &duration, P pred);

  //////////////////////////////////////////////////////////////////////////
  // template/inline implementation
  //////////////////////////////////////////////////////////////////////////

  template <typename D>
  void
  condvar_wait (std::condition_variable &condvar, std::unique_lock<std::mutex> &lock,
		const wait_duration<D> &duration)
  {
    if (duration.m_infinite)
      {
	condvar.wait (lock);
      }
    else
      {
	(void) condvar.wait_for (lock, duration.m_duration);
      }
  }

  template <typename D, typename P>
  bool
  condvar_wait (std::condition_variable &condvar, std::unique_lock<std::mutex> &lock,
		const wait_duration<D> &duration, P pred)
  {
    if (duration.m_infinite)
      {
	condvar.wait (lock, pred);
	return true;
      }
    else
      {
	return condvar.wait_for (lock, duration.m_duration, pred);
      }
  }

  template <class D>
  void
  wait_duration<D>::set_infinite_wait ()
  {
    m_infinite = true;
  }

  template <class D>
  void
  wait_duration<D>::set_duration (const D &duration)
  {
    m_duration = duration;
    m_infinite = false;
  }

} // namespace cubthread

#endif // _THREAD_WAITER_HPP_
