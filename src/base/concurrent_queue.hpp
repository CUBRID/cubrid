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
 * concurrent_queue.hpp - concurrent queue
 */

#ifndef _CONCURRENT_QUEUE_HPP_
#define _CONCURRENT_QUEUE_HPP_

#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>

namespace cubsync
{
  template<typename T>
  class concurrent_queue
  {
    private:
      std::queue<T> m_q;
      std::mutex m_qmtx;
      std::condition_variable m_cond_var;

      bool m_release_waiters;

    public:
      concurrent_queue ();
      bool empty () const;

      T try_pop_front ();
      T wait_for_one ();

      void push_one (T &&element);
      void push_one (const T &element);

      void accept_waiters ();
      void release_waiters ();
      bool notifications_enabled () const;
  };
}

namespace cubsync
{
  template<typename T>
  concurrent_queue<T>::concurrent_queue ()
    : m_release_waiters (false)
  {
  }

  template<typename T>
  bool concurrent_queue<T>::empty () const
  {
    return m_q.empty ();
  }

  template<typename T>
  T concurrent_queue<T>::try_pop_front ()
  {
    std::lock_guard<std::mutex> lg (m_qmtx);
    if (m_q.empty ())
      {
	// For ptr T default constructor makes a NULL ptr
	return T();
      }

    T front = m_q.front ();
    m_q.pop ();

    return front;
  }

  template<typename T>
  T concurrent_queue<T>::wait_for_one ()
  {
    std::unique_lock<std::mutex> ul (m_qmtx);

    m_cond_var.wait (ul, [this] ()
    {
      return m_q.empty () || m_release_waiters;
    });
    if (m_release_waiters)
      {
	ul.unlock ();
	return try_pop_front ();
      }

    T front = m_q.front ();
    m_q.pop ();

    return front;
  }

  template<typename T>
  void concurrent_queue<T>::push_one (T &&element)
  {
    std::lock_guard<std::mutex> lg (m_qmtx);
    m_q.push (std::move (element));

    if (!m_release_waiters)
      {
	m_cond_var.notify_all ();
      }
  }

  template<typename T>
  void concurrent_queue<T>::push_one (const T &element)
  {
    std::lock_guard<std::mutex> lg (m_qmtx);
    m_q.push (element);
    m_cond_var.notify_all ();
  }

  template<typename T>
  void concurrent_queue<T>::accept_waiters ()
  {
    std::unique_lock<std::mutex> ul (m_qmtx);
    m_release_waiters = false;
  }

  template<typename T>
  void concurrent_queue<T>::release_waiters ()
  {
    std::unique_lock<std::mutex> ul (m_qmtx);
    m_release_waiters = true;
    ul.unlock ();

    m_cond_var.notify_all ();
  }

  template<typename T>
  bool concurrent_queue<T>::notifications_enabled () const
  {
    return !m_release_waiters;
  }
}

#endif // _CONCURRENT_QUEUE_HPP_
