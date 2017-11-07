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


#include <cassert>

namespace thread
{

class waiter
{
public:
  waiter ();
  ~waiter();

  void wakeup (void);

  void wait_inf (void);
  template< class Rep, class Period >
  bool wait_for (std::chrono::duration<Rep, Period>& delta);
  template< class Clock, class Duration >
  bool wait_until (std::chrono::time_point<Clock, Duration>& timeout_time);

private:

  enum status
  {
    RUNNING,
    SLEEPING,
    AWAKENING,
  };

  inline bool check_wake (void);
  inline void sleep (void);
  inline void awake (void);
  inline void run (void);

  std::mutex m_mutex;
  std::condition_variable m_condvar;
  status m_status;
};

} // namespace thread

#endif // _THREAD_WAITER_HPP_

/************************************************************************/
/* Template implementation                                              */
/************************************************************************/

namespace thread
{

template< class Rep, class Period >
bool
waiter::wait_for (std::chrono::duration<Rep, Period>& delta)
{
  if (delta == std::chrono::duration<Rep, Period> (0))
    {
      // no wait, just yield
      std::this_thread::yield ();
      return true;
    }

  std::unique_lock<std::mutex> lock (m_mutex);    // mutex is also locked
  sleep ();

  bool ret = m_condvar.wait_for (lock, delta, [this] { return m_status == AWAKENING; });

  run ();

  // mutex is automatically unlocked
  return ret;
}

template<class Clock, class Duration>
bool
waiter::wait_until (std::chrono::time_point<Clock, Duration>& timeout_time)
{
  std::unique_lock<std::mutex> lock (m_mutex);    // mutex is also locked
  sleep ();

  bool ret = m_condvar.wait_until (lock, timeout_time, check_wake);

  run ();

  // mutex is automatically unlocked
  return ret;
}

} // namespace thread
