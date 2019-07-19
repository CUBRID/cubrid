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


  class event
  {
    private:
      semaphore<bool> m_semaphore;

    public:

      event ():
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
