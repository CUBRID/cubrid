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
 * semaphore.hpp - interface of file & line location
 */

#ifndef _SEMAPHORE_HPP_
#define _SEMAPHORE_HPP_

#include <condition_variable>
#include <mutex>

namespace cubsync
{
  /*
   *
   * Usage :
   *    semaphore<boolean> s (false);
   *    s.signal (true);
   *    s.wait (true);
   *
   *    semaphore<int> s (0);
   *    s.signal (2);
   *    s.wait (2);
   *
   */
  template <typename T>
  class semaphore
  {
    private:
      T value;

      std::mutex m_mutex;
      std::condition_variable m_cond_var;

    public:
      semaphore (const T &init_value)
      {
	value = init_value;
      }

      void signal (const T &new_value)
      {
	std::unique_lock<std::mutex> ulock (m_mutex);
	value = new_value;
	ulock.unlock ();
	m_cond_var.notify_all ();
      }

      void wait (const T &required_value)
      {
	if (value == required_value)
	  {
	    return;
	  }

	std::unique_lock<std::mutex> ulock (m_mutex);
	m_cond_var.wait (ulock, [this, required_value] { return value == required_value;});
      }
  };

  class event_semaphore
  {
    private:
      semaphore<bool> m_semaphore;

    public:

      event_semaphore ():
	m_semaphore (false)
      {
      }

      void set ()
      {
	m_semaphore.signal (true);
      }

      void clear ()
      {
	m_semaphore.signal (false);
      }

      void wait ()
      {
	m_semaphore.wait (true);
      }
  };

} // namespace cubsync

#endif // _SEMAPHORE_HPP_
