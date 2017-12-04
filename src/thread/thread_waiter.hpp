/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */

/*
 * thread_waiter - interface of suspending/waking threads
 */

#ifndef _THREAD_WAITER_HPP_
#define _THREAD_WAITER_HPP_

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

#include <cassert>

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

namespace cubthread
{

  class waiter
  {
    public:
      waiter ();
      ~waiter();

      void wakeup (void);                                             // wakeup waiter thread

      void wait_inf (void);                                           // wait until wakeup
      template< class Rep, class Period >
      bool wait_for (std::chrono::duration<Rep, Period> &delta);      // wait for period of time or until wakeup
      // returns true if woke up before timeout
      template< class Clock, class Duration >
      bool wait_until (std::chrono::time_point<Clock, Duration> &timeout_time); // wait until time or until wakeup
      // returns true if woke up before
      // timeout

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
  };

} // namespace cubthread

#endif // _THREAD_WAITER_HPP_

/************************************************************************/
/* Template implementation                                              */
/************************************************************************/

namespace cubthread
{

  template< class Rep, class Period >
  bool
  waiter::wait_for (std::chrono::duration<Rep, Period> &delta)
  {
    if (delta == std::chrono::duration<Rep, Period> (0))
      {
#if defined (NO_GCC_44)
	// no wait, just yield
	std::this_thread::yield ();
#endif // not GCC 4.4
	return true;
      }

    std::unique_lock<std::mutex> lock (m_mutex);    // mutex is also locked
    goto_sleep ();

#if defined (NO_GCC_44)
    bool ret = m_condvar.wait_for (lock, delta, [this] { return m_status == AWAKENING; });
#else // NO_GCC_44
    bool ret = (m_condvar.wait_for (lock, delta) != std::cv_status::timeout);
#endif // MP_GCC_44

    run ();

    // mutex is automatically unlocked
    return ret;
  }

  template<class Clock, class Duration>
  bool
  waiter::wait_until (std::chrono::time_point<Clock, Duration> &timeout_time)
  {
    std::unique_lock<std::mutex> lock (m_mutex);    // mutex is also locked
    goto_sleep ();

#if defined (NO_GCC_44)
    bool ret = m_condvar.wait_until (lock, timeout_time, [this] { return m_status == AWAKENING; });
#else // GCC 4.4
    bool ret = (m_condvar.wait_until (lock, timeout_time) != std::cv_status::timeout);
#endif // GCC 4.4

    run ();

    // mutex is automatically unlocked
    return ret;
  }

} // namespace cubthread
