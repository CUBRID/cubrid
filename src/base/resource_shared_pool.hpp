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
 * resource_shared_pool - template implementation for preallocated shared resources pool & dispatch
 */

#ifndef _RESOURCE_SHARED_POOL_HPP_
#define _RESOURCE_SHARED_POOL_HPP_

#include <mutex>

template <class T>
class resource_shared_pool
{
  public:
    resource_shared_pool (size_t size, bool allow_claimed_on_destruction = false)
      : m_size (size)
      , m_free_stack_size (size)
      , m_mutex ()
      , m_allow_claimed_on_destruction (allow_claimed_on_destruction)
    {
      m_resources = new T [size];
      m_own_resources = m_resources;
      populate_free_stack ();
    }

    resource_shared_pool (T *resources, size_t size, bool allow_claimed_on_destruction = false)
      : m_size (size)
      , m_free_stack_size (size)
      , m_mutex ()
      , m_resources (resources)
      , m_own_resources (NULL)
      , m_free_stack (NULL)
      , m_allow_claimed_on_destruction (allow_claimed_on_destruction)
    {
      populate_free_stack ();
    }

    ~resource_shared_pool ()
    {
      assert (m_allow_claimed_on_destruction || m_free_stack_size == m_size);
      delete [] m_free_stack;
      delete [] m_own_resources;
    }

    inline T *claim (void)
    {
      assert (m_free_stack_size <= m_size);

      std::unique_lock<std::mutex> ulock (m_mutex);
      if (m_free_stack_size == 0)
	{
	  return NULL;
	}
      return m_free_stack[--m_free_stack_size];
    }

    inline void retire (T &claimed)
    {
      assert (&claimed >= m_resources && &claimed < m_resources + m_size);
      assert (m_free_stack_size < m_size);

      std::unique_lock<std::mutex> ulock (m_mutex);

      m_free_stack[m_free_stack_size++] = &claimed;
    }

  private:
    resource_shared_pool ();    // no implicit constructor
    resource_shared_pool (const resource_shared_pool &); // no copy constructor

    void populate_free_stack ()
    {
      m_free_stack = new T* [m_size];
      for (size_t i = 0; i < m_free_stack_size; i++)
	{
	  m_free_stack[i] = &m_resources[i];
	}
    }

    size_t m_size;
    size_t m_free_stack_size;
    std::mutex m_mutex;

    T *m_resources;
    T *m_own_resources;
    T **m_free_stack;

    bool m_allow_claimed_on_destruction;
};

#endif // _RESOURCE_SHARED_POOL_HPP_
