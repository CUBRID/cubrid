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

      freelist () = delete;
      freelist (size_t block_size, size_t initial_block_count = 1);
      ~freelist ();

      T *claim ();

      // make sure what you retire is no longer accessible
      void retire (T &t);

      size_t get_alloc_count () const;
      size_t get_available_count () const;
      size_t get_backbuffer_count () const;
      size_t get_forced_allocation_count () const;

    private:

      size_t m_block_size;

      atomic_link_type m_available_list;      // list of available entries

      // backbuffer head & tail; when available list is consumed, it is quickly replaced with back-buffer; without
      // backbuffer, multiple threads can race to allocate multiple blocks at once
      atomic_link_type m_backbuffer_head;
      atomic_link_type m_backbuffer_tail;

      // statistics:
      std::atomic<size_t> m_available_count;
      std::atomic<size_t> m_alloc_count;
      std::atomic<size_t> m_bb_count;
      std::atomic<size_t> m_forced_alloc_count;

      void swap_backbuffer ();
      void alloc_backbuffer ();
      void force_alloc_block ();

      void alloc_list (T *&head, T *&tail);
      void dealloc_list (T *head);

      T *pop_from_available ();
      void push_to_list (T *head, T *tail, atomic_link_type &dest);

      void clear ();                    // not thread safe!
      void final_sanity_checks () const;
  };
} // namespace lockfree

//
// implementation
//
#include <cassert>

namespace lockfree
{
  //
  // freelist
  //
  template <class T>
  freelist<T>::freelist (size_t block_size, size_t initial_block_count)
    : m_block_size (block_size)
    , m_available_list { NULL }
    , m_backbuffer_head { NULL }
    , m_backbuffer_tail { NULL }
    , m_available_count { 0 }
    , m_alloc_count { 0 }
    , m_bb_count { 0 }
    , m_forced_alloc_count { 0 }
  {
    assert (block_size > 1);
    // minimum two blocks
    if (initial_block_count <= 1)
      {
	m_block_size /= 2;
	initial_block_count = 2;
      }

    alloc_backbuffer ();
    for (size_t i = 0; i < initial_block_count; i++)
      {
	swap_backbuffer ();
      }
  }

  template <class T>
  void
  freelist<T>::swap_backbuffer ()
  {
    T *bb_head = m_backbuffer_head;
    if (bb_head == NULL)
      {
	// somebody already allocated block
	return;
      }
    T *bb_head_copy = bb_head; // make sure a copy is passed to compare exchange
    if (!m_backbuffer_head.compare_exchange_strong (bb_head_copy, NULL))
      {
	// somebody already changing it
	return;
      }

    T *bb_tail = m_backbuffer_tail.exchange (NULL);
    assert (bb_tail != NULL);

    push_to_list (bb_head, bb_tail, m_available_list);
    m_available_count += m_bb_count.exchange (0);

    alloc_backbuffer ();
  }

  template <class T>
  void
  freelist<T>::alloc_backbuffer ()
  {
    T *new_bb_head = NULL;
    T *new_bb_tail = NULL;

    alloc_list (new_bb_head, new_bb_tail);

    // update backbuffer tail
    T *dummy_null = NULL;
    m_backbuffer_tail.compare_exchange_strong (dummy_null, new_bb_tail);

    // update backbuffer head
    push_to_list (new_bb_head, new_bb_tail, m_backbuffer_head);
    m_bb_count += m_block_size;
  }

  template <class T>
  void
  freelist<T>::force_alloc_block ()
  {
    T *new_head = NULL;
    T *new_tail = NULL;
    alloc_list (new_head, new_tail);

    // push directly to available
    push_to_list (new_head, new_tail, m_available_list);
    m_available_count += m_block_size;
    ++m_forced_alloc_count;
  }

  template <class T>
  void
  freelist<T>::alloc_list (T *&head, T *&tail)
  {
    head = tail = NULL;
    T *t;
    for (size_t i = 0; i < m_block_size; i++)
      {
	t = new T ();
	if (tail == NULL)
	  {
	    tail = t;
	  }
	t->get_freelist_link ().store (head);
	head = t;
      }
    m_alloc_count += m_block_size;
  }

  template <class T>
  void
  freelist<T>::dealloc_list (T *head)
  {
    // free all
    T *save_next = NULL;
    for (T *t = head; t != NULL; t = save_next)
      {
	save_next = t->get_freelist_link ().load ();
	delete t;
      }
  }

  template <class T>
  freelist<T>::~freelist ()
  {
    clear ();
  }

  template <class T>
  void
  freelist<T>::clear ()
  {
    final_sanity_checks ();

    // move back-buffer to available
    dealloc_list (m_backbuffer_head.load ());
    m_backbuffer_head = NULL;
    m_backbuffer_tail = NULL;

    dealloc_list (m_available_list.load ());
    m_available_list = NULL;

    m_available_count = m_bb_count = m_alloc_count = 0;
  }

  template <class T>
  T *
  freelist<T>::claim ()
  {
    // todo: make sure transaction is open here
    T *t;
    size_t count = 0;
    for (t = pop_from_available (); t == NULL && count < 100; t = pop_from_available (), ++count)
      {
	// if it loops many times, it is probably because the back-buffer allocator was preempted for a very long time.
	// force allocations
	swap_backbuffer ();
      }
    // if swapping backbuffer didn't work (probably back-buffer allocator was preempted for a long time), force
    // allocating directly into available list
    while (t == NULL)
      {
	force_alloc_block ();
	t = pop_from_available ();
      }
    assert (t != NULL);
    m_available_count--;
    return t;
  }

  template <class T>
  T *
  freelist<T>::pop_from_available ()
  {
    T *rhead = NULL;
    T *rhead_copy = NULL;
    T *next;
    do
      {
	rhead = m_available_list;
	if (rhead == NULL)
	  {
	    return NULL;
	  }
	next = rhead->get_freelist_link ().load ();
	rhead_copy = rhead;
	// todo: this is a dangerous preemption point; if I am preempted here, and thread 2 comes and does:
	//   - second thread gets same rhead and successfully moves m_available_list to next
	//   - third thread gets next and successfully moves m_available_list to next->next
	//   - second thread retires rhead. m_available_list becomes rhead and its next becomes next->next
	//   - I wake up, compare exchange m_available_list successfully because it is rhead again, but next will
	//     become the item third thread already claimed.
      }
    while (!m_available_list.compare_exchange_strong (rhead_copy, next));

    rhead->get_freelist_link ().store (NULL);
    return rhead;
  }

  template <class T>
  void
  freelist<T>::retire (T &t)
  {
    // make sure transaction is open here and transaction ID was incremented
    push_to_list (&t, &t, m_available_list);
    m_available_count++;
  }

  template<class T>
  void
  freelist<T>::push_to_list (T *head, T *tail, atomic_link_type &dest)
  {
    T *avail_head;
    assert (head != NULL);
    assert (tail != NULL);
    assert (tail->get_freelist_link () == NULL);

    do
      {
	avail_head = dest;
	tail->get_freelist_link () = avail_head;
      }
    while (!dest.compare_exchange_strong (avail_head, head));
  }

  template<class T>
  size_t
  freelist<T>::get_alloc_count () const
  {
    return m_alloc_count;
  }

  template<class T>
  size_t
  freelist<T>::get_available_count () const
  {
    return m_available_count;
  }

  template <class T>
  size_t
  freelist<T>::get_backbuffer_count () const
  {
    return m_bb_count;
  }

  template <class T>
  size_t
  freelist<T>::get_forced_allocation_count () const
  {
    return m_forced_alloc_count;
  }

  template <class T>
  void
  freelist<T>::final_sanity_checks () const
  {
#if !defined (NDEBUG)
    assert (m_available_count + m_bb_count == m_alloc_count);

    // check back-buffer
    size_t list_count = 0;
    T *save_last = NULL;
    for (T *iter = m_backbuffer_head; iter != NULL; iter = iter->get_freelist_link ())
      {
	++list_count;
	save_last = iter;
      }
    assert (list_count == m_bb_count);
    assert (list_count == m_block_size);
    assert (save_last == m_backbuffer_tail);

    // check available
    list_count = 0;
    for (T *iter = m_available_list; iter != NULL; iter = iter->get_freelist_link ())
      {
	++list_count;
      }
    assert (list_count == m_available_count);
#endif // DEBUG
  }
} // namespace lockfree

#endif // !_LOCKFREE_FREELIST_HPP_
