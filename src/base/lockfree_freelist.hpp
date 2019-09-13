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

#ifndef _LOCKFREE_FREELIST_HPP_
#define _LOCKFREE_FREELIST_HPP_

#include <atomic>
#include <cstddef>

namespace lockfree
{
  // template T:
  //  methods:
  //    freelist::atomic_link_type &get_freelist_link ();
  template <class T>
  class freelist
  {
    public:
      using atomic_link_type = std::atomic<T *>;

      class factory
      {
	public:
	  factory () = default;
	  virtual ~factory () = default;

	  virtual T *alloc ();
	  virtual void init (T &t);
	  virtual void uninit (T &t);
      };

      freelist () = delete;
      freelist (factory<T> &freelist_factory, size_t block_size, size_t initial_block_count = 1);
      ~freelist ();

      T *claim ();

      // make sure what you retire is no longer accessible
      void retire (T &t);
      void retire_list (T *head);

    private:
      factory<T> &m_factory;

      size_t m_block_size;

      atomic_link_type m_available_list;

      // statistics:
      std::atomic<size_t> m_available_count;
      std::atomic<size_t> m_alloc_count;

      T *alloc_block ();
      T *pop ();
      void push (T *head, T *tail);
  };
} // namespace lockfree

namespace lockfree
{
  //
  // freelist
  //
  template <class T>
  freelist<T>::freelist (factory<T> &freelist_factory, size_t block_size, size_t initial_block_count)
    : m_factory (freelist_factory)
    , m_block_size (block_size)
    , m_available_count (block_size * initial_block_count)
    , m_available_list { NULL }
  {
    for (size_t i = 0; i < initial_block_count; i++)
      {
	alloc_block ();
      }
  }

  template <class T>
  T *
  freelist<T>::alloc_block ()
  {
    T *block_head = NULL;
    T *block_tail = NULL;
    T *t;
    for (size_t i = 0; i < m_block_size; i++)
      {
	t = m_factory.alloc ();
	if (block_tail == NULL)
	  {
	    block_tail = t;
	  }
	t->get_freelist_link ().store (block_head);
	block_head = t;
      }
    push (block_head, block_tail);

    m_available_count += m_block_size;
    m_alloc_count += m_block_size;
  }

  template <class T>
  freelist<T>::~freelist ()
  {
    T *save_next = NULL;
    for (T *t = m_available_list; t != NULL; t = save_next)
      {
	save_next = t->get_freelist_link ().load ();
	delete t;
      }
  }

  template <class T>
  T *
  freelist<T>::claim ()
  {
    T *t;
    for (t = pop (); t == NULL; t = pop ())
      {
	alloc_block ();
      }
    assert (t != NULL);
    m_factory->init (*t);
    m_available_count--;
    return t;
  }

  template <class T>
  T *
  freelist<T>::pop ()
  {
    T *rhead = NULL;
    T *next;
    do
      {
	rhead = m_available_list;
	if (rhead == NULL)
	  {
	    return NULL;
	  }
	next = rhead->get_freelist_link ().load ();
      }
    while (!m_available_list.compare_exchange_strong (rhead, next));

    rhead->get_freelist_link ().store (NULL);
    return rhead;
  }

  template <class T>
  void
  freelist<T>::retire (T &t)
  {
    m_factory.uninit (t);
    push (&t, &t);
    m_available_count++;
  }

  template <class T>
  void
  freelist<T>::retire_list (T *head)
  {
    if (head == NULL)
      {
	return;
      }

    T *tail;
    size_t list_size = 1;
    for (tail = head; tail->get_freelist_link () != NULL; tail = tail->get_freelist_link ())
      {
	m_factory->uninit (*tail);
	++list_size;
      }
    assert (tail != NULL);
    m_factory->uninit (*tail);
    push (head, tail);
    m_available_count += list_size;
  }

  template <class T>
  void
  freelist<T>::push (T *head, T *tail)
  {
    T *avail_head;
    assert (head != NULL);
    assert (tail != NULL);
    assert (tail->get_freelist_link () == NULL);

    do
      {
	avail_head = m_available_list;
	tail->get_freelist_link () = avail_head;
      }
    while (m_available_list.compare_exchange_strong (avail_head, head));
  }

  //
  // freelist::factory
  //
  template<class T>
  T *
  freelist<T>::factory::alloc ()
  {
    return new T ();
  }

  template<class T>
  void
  freelist<T>::factory::init (T &t)
  {
  }

  template<class T>
  void
  freelist<T>::factory::uninit (T &t)
  {
  }
} // namespace lockfree

#endif // !_LOCKFREE_FREELIST_HPP_
