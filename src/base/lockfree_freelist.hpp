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

#include "lockfree_transaction_def.hpp"

#include <atomic>
#include <cstddef>

namespace lockfree
{
  template <class T>
  class freelist
  {
    public:
      class hazard_node;

      freelist () = delete;
      freelist (size_t block_size, size_t initial_block_count = 1);
      ~freelist ();

      hazard_node *claim (tran::index tran_index);
      void retire (tran::index tran_index, hazard_node &hzn);

      size_t get_alloc_count () const;
      size_t get_available_count () const;
      size_t get_backbuffer_count () const;
      size_t get_forced_allocation_count () const;

    private:
      size_t m_block_size;

      std::atomic<hazard_node *> m_available_list;      // list of available entries

      // backbuffer head & tail; when available list is consumed, it is quickly replaced with back-buffer; without
      // backbuffer, multiple threads can race to allocate multiple blocks at once
      std::atomic<hazard_node *> m_backbuffer_head;
      std::atomic<hazard_node *> m_backbuffer_tail;

      // statistics:
      std::atomic<size_t> m_available_count;
      std::atomic<size_t> m_alloc_count;
      std::atomic<size_t> m_bb_count;
      std::atomic<size_t> m_forced_alloc_count;

      void swap_backbuffer ();
      void alloc_backbuffer ();
      void force_alloc_block ();

      void alloc_list (hazard_node *&head, hazard_node *&tail);
      void dealloc_list (hazard_node *head);

      hazard_node *pop_from_available ();
      void push_to_list (hazard_node &head, hazard_node &tail, std::atomic<hazard_node *> dest);

      void clear ();                    // not thread safe!
      void final_sanity_checks () const;
  };

  template <class T>
  class freelist<T>::hazard_node : public hazard_pointer
  {
    public:
      hazard_node ();
      ~hazard_node () = default;

      virtual void on_reclaim ();

    private:

      void set_owner (freelist &m_freelist);

      void set_freelist_next (hazard_node *next);
      void reset_freelist_next (void);
      hazard_node *get_freelist_next ();

      freelist *m_owner;
      T m_t;
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
    hazard_node *bb_head = m_backbuffer_head;
    if (bb_head == NULL)
      {
	// somebody already allocated block
	return;
      }
    hazard_node *bb_head_copy = bb_head; // make sure a copy is passed to compare exchange
    if (!m_backbuffer_head.compare_exchange_strong (bb_head_copy, NULL))
      {
	// somebody already changing it
	return;
      }

    hazard_node *bb_tail = m_backbuffer_tail.exchange (NULL);
    assert (bb_tail != NULL);

    push_to_list (*bb_head, *bb_tail, m_available_list);
    m_available_count += m_bb_count.exchange (0);

    alloc_backbuffer ();
  }

  template <class T>
  void
  freelist<T>::alloc_backbuffer ()
  {
    hazard_node *new_bb_head = NULL;
    hazard_node *new_bb_tail = NULL;

    alloc_list (new_bb_head, new_bb_tail);

    // update backbuffer tail
    hazard_node *dummy_null = NULL;
    m_backbuffer_tail.compare_exchange_strong (dummy_null, new_bb_tail);

    // update backbuffer head
    push_to_list (*new_bb_head, *new_bb_tail, m_backbuffer_head);
    m_bb_count += m_block_size;
  }

  template <class T>
  void
  freelist<T>::force_alloc_block ()
  {
    hazard_node *new_head = NULL;
    hazard_node *new_tail = NULL;
    alloc_list (new_head, new_tail);

    // push directly to available
    push_to_list (*new_head, *new_tail, m_available_list);
    m_available_count += m_block_size;
    ++m_forced_alloc_count;
  }

  template <class T>
  void
  freelist<T>::alloc_list (hazard_node *&head, hazard_node *&tail)
  {
    head = tail = NULL;
    hazard_node *hzn;
    for (size_t i = 0; i < m_block_size; i++)
      {
	hzn = new hazard_node ();
	hzn->set_owner (*this);
	if (tail == NULL)
	  {
	    tail = hzn;
	  }
	hzn->set_freelist_next (head);
	head = hzn;
      }
    m_alloc_count += m_block_size;
  }

  template <class T>
  void
  freelist<T>::dealloc_list (hazard_node *head)
  {
    // free all
    hazard_node *save_next = NULL;
    for (hazard_node *hzn = head; hzn != NULL; hzn = save_next)
      {
	save_next = hzn->get_freelist_next ();
	delete hzn;
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

  template<class T>
  typename freelist<T>::hazard_node *
  freelist<T>::claim (tran::index tran_index)
  {
    // todo: make sure transaction is open here
    hazard_node *hazard_node;
    size_t count = 0;
    for (hazard_node = pop_from_available (); hazard_node == NULL && count < 100;
	 hazard_node = pop_from_available (), ++count)
      {
	// if it loops many times, it is probably because the back-buffer allocator was preempted for a very long time.
	// force allocations
	swap_backbuffer ();
      }
    // if swapping backbuffer didn't work (probably back-buffer allocator was preempted for a long time), force
    // allocating directly into available list
    while (hazard_node == NULL)
      {
	force_alloc_block ();
	hazard_node = pop_from_available ();
      }
    assert (hazard_node != NULL);
    m_available_count--;
    return hazard_node;
  }

  template<class T>
  typename freelist<T>::hazard_node *
  freelist<T>::pop_from_available ()
  {
    hazard_node *rhead = NULL;
    hazard_node *rhead_copy = NULL;
    hazard_node *next;
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

  template<class T>
  void
  freelist<T>::retire (tran::index tran_index, hazard_node &t)
  {
    // make sure transaction is open here and transaction ID was incremented
    push_to_list (&t, &t, m_available_list);
    m_available_count++;
  }

  template<class T>
  void
  freelist<T>::push_to_list (hazard_node &head, hazard_node &tail, std::atomic<hazard_node *> dest)
  {
    hazard_node *rhead;
    assert (tail.get_freelist_next () == NULL);

    do
      {
	rhead = dest;
	tail.set_freelist_next (rhead);
      }
    while (!dest.compare_exchange_strong (rhead, head));
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

  template<class T>
  size_t
  freelist<T>::get_backbuffer_count () const
  {
    return m_bb_count;
  }

  template<class T>
  size_t
  freelist<T>::get_forced_allocation_count () const
  {
    return m_forced_alloc_count;
  }

  template<class T>
  void
  freelist<T>::final_sanity_checks () const
  {
#if !defined (NDEBUG)
    assert (m_available_count + m_bb_count == m_alloc_count);

    // check back-buffer
    size_t list_count = 0;
    hazard_node *save_last = NULL;
    for (hazard_node *iter = m_backbuffer_head; iter != NULL; iter = iter->get_freelist_next ())
      {
	++list_count;
	save_last = iter;
      }
    assert (list_count == m_bb_count);
    assert (list_count == m_block_size);
    assert (save_last == m_backbuffer_tail);

    // check available
    list_count = 0;
    for (hazard_node *iter = m_available_list; iter != NULL; iter = iter->get_freelist_next ())
      {
	++list_count;
      }
    assert (list_count == m_available_count);
#endif // DEBUG
  }

  //
  // freelist::handle
  //
  template<class T>
  freelist<T>::hazard_node::hazard_node ()
    : hazard_pointer ()
    , m_owner (NULL)
    , m_t {}
  {
  }

  template<class T>
  void
  freelist<T>::hazard_node::on_reclaim ()
  {
    // do nothing by default
  }

  template<class T>
  void
  freelist<T>::hazard_node::set_owner (freelist &fl)
  {
    m_owner = &fl;
  }

  template<class T>
  void
  freelist<T>::hazard_node::set_freelist_next (hazard_node *next)
  {
    m_hazard_next = next;
  }

  template<class T>
  void
  freelist<T>::hazard_node::reset_freelist_next ()
  {
    m_hazard_next = NULL;
  }

  template<class T>
  typename freelist<T>::hazard_node *
  freelist<T>::hazard_node::get_freelist_next ()
  {
    assert (dynamic_cast<hazard_node *> (m_hazard_next) != NULL);
    return static_cast<hazard_node *> (m_hazard_next);
  }
} // namespace lockfree

#endif // !_LOCKFREE_FREELIST_HPP_
