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

#ifndef _LOCKFREE_FREELIST_HPP_
#define _LOCKFREE_FREELIST_HPP_

#include "lockfree_transaction_def.hpp"
#include "lockfree_transaction_descriptor.hpp"
#include "lockfree_transaction_reclaimable.hpp"
#include "lockfree_transaction_table.hpp"

#include <atomic>
#include <cstddef>

namespace lockfree
{
  namespace tran
  {
    class system;
  }
}

namespace lockfree
{
  // T must have on_reclaim function
  template <class T>
  class freelist
  {
    public:
      class free_node;

      freelist () = delete;
      freelist (tran::system &transys, size_t block_size, size_t initial_block_count = 1);
      ~freelist ();

      free_node *claim (tran::descriptor &tdes);
      free_node *claim (tran::index tran_index);                // claim a free node
      // note: transaction will remain started!

      void retire (tran::descriptor &tdes, free_node &node);
      void retire (tran::index tran_index, free_node &node);

      size_t get_alloc_count () const;
      size_t get_available_count () const;
      size_t get_backbuffer_count () const;
      size_t get_forced_allocation_count () const;
      size_t get_retired_count () const;
      size_t get_claimed_count () const;

      tran::system &get_transaction_system ();
      tran::table &get_transaction_table ();

    private:
      tran::system &m_transys;
      tran::table *m_trantable;

      size_t m_block_size;

      std::atomic<free_node *> m_available_list;      // list of available entries

      // backbuffer head & tail; when available list is consumed, it is quickly replaced with back-buffer; without
      // backbuffer, multiple threads can race to allocate multiple blocks at once
      std::atomic<free_node *> m_backbuffer_head;
      std::atomic<free_node *> m_backbuffer_tail;

      // statistics:
      std::atomic<size_t> m_available_count;
      std::atomic<size_t> m_alloc_count;
      std::atomic<size_t> m_bb_count;
      std::atomic<size_t> m_forced_alloc_count;
      std::atomic<size_t> m_retired_count;

      void swap_backbuffer ();
      void alloc_backbuffer ();
      void force_alloc_block ();

      void alloc_list (free_node *&head, free_node *&tail);
      void dealloc_list (free_node *head);

      free_node *pop_from_available ();
      void push_to_list (free_node &head, free_node &tail, std::atomic<free_node *> &dest);

      void clear_free_nodes ();                    // not thread safe!
      void final_sanity_checks () const;
      void check_my_pointer (free_node *node);
  };

  template <class T>
  class freelist<T>::free_node : public tran::reclaimable_node
  {
    public:
      free_node ();
      ~free_node () = default;

      T &get_data ();

      void reclaim () final override;

    private:
      friend freelist;

      void set_owner (freelist &m_freelist);

      void set_freelist_next (free_node *next);
      void reset_freelist_next (void);
      free_node *get_freelist_next ();

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
  freelist<T>::freelist (tran::system &transys, size_t block_size, size_t initial_block_count)
    : m_transys (transys)
    , m_trantable (new tran::table (transys))
    , m_block_size (block_size)
    , m_available_list { NULL }
    , m_backbuffer_head { NULL }
    , m_backbuffer_tail { NULL }
    , m_available_count { 0 }
    , m_alloc_count { 0 }
    , m_bb_count { 0 }
    , m_forced_alloc_count { 0 }
    , m_retired_count { 0 }
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
    free_node *bb_head = m_backbuffer_head;
    if (bb_head == NULL)
      {
	// somebody already allocated block
	return;
      }
    free_node *bb_head_copy = bb_head; // make sure a copy is passed to compare exchange
    if (!m_backbuffer_head.compare_exchange_strong (bb_head_copy, NULL))
      {
	// somebody already changing it
	return;
      }

    free_node *bb_tail = m_backbuffer_tail.exchange (NULL);
    assert (bb_tail != NULL);

    m_available_count += m_bb_count.exchange (0);
    push_to_list (*bb_head, *bb_tail, m_available_list);

    alloc_backbuffer ();
  }

  template <class T>
  void
  freelist<T>::alloc_backbuffer ()
  {
    free_node *new_bb_head = NULL;
    free_node *new_bb_tail = NULL;

    alloc_list (new_bb_head, new_bb_tail);

    // update backbuffer tail
    free_node *dummy_null = NULL;
    m_backbuffer_tail.compare_exchange_strong (dummy_null, new_bb_tail);

    // update backbuffer head
    push_to_list (*new_bb_head, *new_bb_tail, m_backbuffer_head);
    m_bb_count += m_block_size;
  }

  template <class T>
  void
  freelist<T>::force_alloc_block ()
  {
    free_node *new_head = NULL;
    free_node *new_tail = NULL;
    alloc_list (new_head, new_tail);

    // push directly to available
    m_available_count += m_block_size;
    ++m_forced_alloc_count;
    push_to_list (*new_head, *new_tail, m_available_list);
  }

  template <class T>
  void
  freelist<T>::alloc_list (free_node *&head, free_node *&tail)
  {
    head = tail = NULL;
    free_node *node;
    for (size_t i = 0; i < m_block_size; i++)
      {
	node = new free_node ();
	node->set_owner (*this);
	if (tail == NULL)
	  {
	    tail = node;
	  }
	node->set_freelist_next (head);
	head = node;
      }
    m_alloc_count += m_block_size;
  }

  template <class T>
  void
  freelist<T>::dealloc_list (free_node *head)
  {
    // free all
    free_node *save_next = NULL;
    for (free_node *node = head; node != NULL; node = save_next)
      {
	save_next = node->get_freelist_next ();
	delete node;
      }
  }

  template <class T>
  freelist<T>::~freelist ()
  {
    delete m_trantable;
    clear_free_nodes ();
  }

  template <class T>
  void
  freelist<T>::clear_free_nodes ()
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
  typename freelist<T>::free_node *
  freelist<T>::claim (tran::index tran_index)
  {
    return claim (m_trantable->get_descriptor (tran_index));
  }

  template<class T>
  typename freelist<T>::free_node *
  freelist<T>::claim (tran::descriptor &tdes)
  {
    tdes.start_tran ();
    tdes.reclaim_retired_list ();

    free_node *node;
    size_t count = 0;
    for (node = pop_from_available (); node == NULL && count < 100; node = pop_from_available (), ++count)
      {
	// if it loops many times, it is probably because the back-buffer allocator was preempted for a very long time.
	// force allocations
	swap_backbuffer ();
      }
    // if swapping backbuffer didn't work (probably back-buffer allocator was preempted for a long time), force
    // allocating directly into available list
    while (node == NULL)
      {
	force_alloc_block ();
	node = pop_from_available ();
      }

    assert (node != NULL);
    assert (m_available_count > 0);
    m_available_count--;
    check_my_pointer (node);

    return node;
  }

  template<class T>
  typename freelist<T>::free_node *
  freelist<T>::pop_from_available ()
  {
    free_node *rhead = NULL;
    free_node *rhead_copy = NULL;
    free_node *next;
    do
      {
	rhead = m_available_list;
	if (rhead == NULL)
	  {
	    return NULL;
	  }
	next = rhead->get_freelist_next ();
	rhead_copy = rhead;
	// todo: this is a dangerous preemption point; if I am preempted here, and thread 2 comes and does:
	//   - second thread gets same rhead and successfully moves m_available_list to next
	//   - third thread gets next and successfully moves m_available_list to next->next
	//   - second thread retires rhead. m_available_list becomes rhead and its next becomes next->next
	//   - I wake up, compare exchange m_available_list successfully because it is rhead again, but next will
	//     become the item third thread already claimed.
      }
    while (!m_available_list.compare_exchange_weak (rhead_copy, next));

    rhead->reset_freelist_next ();
    return rhead;
  }

  template<class T>
  void
  freelist<T>::retire (tran::index tran_index, free_node &node)
  {
    retire (m_trantable->get_descriptor (tran_index), node);
  }

  template<class T>
  void
  freelist<T>::retire (tran::descriptor &tdes, free_node &node)
  {
    assert (node.get_freelist_next () == NULL);
    ++m_retired_count;
    check_my_pointer (&node);
    tdes.retire_node (node);
  }

  template<class T>
  void
  freelist<T>::push_to_list (free_node &head, free_node &tail, std::atomic<free_node *> &dest)
  {
    free_node *rhead;
    assert (tail.get_freelist_next () == NULL);

    do
      {
	rhead = dest;
	tail.set_freelist_next (rhead);
      }
    while (!dest.compare_exchange_weak (rhead, &head));
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
  size_t
  freelist<T>::get_retired_count () const
  {
    return m_retired_count;
  }

  template<class T>
  size_t
  freelist<T>::get_claimed_count () const
  {
    size_t alloc_count = m_alloc_count;
    size_t unused_count = m_available_count + m_bb_count + m_retired_count;
    if (alloc_count > unused_count)
      {
	return alloc_count - unused_count;
      }
    else
      {
	return 0;
      }
  }

  template<class T>
  tran::system &
  freelist<T>::get_transaction_system ()
  {
    return m_transys;
  }

  template<class T>
  tran::table &
  freelist<T>::get_transaction_table ()
  {
    return *m_trantable;
  }

  template<class T>
  void
  freelist<T>::final_sanity_checks () const
  {
#if !defined (NDEBUG)
    assert (m_available_count + m_bb_count == m_alloc_count);

    // check back-buffer
    size_t list_count = 0;
    free_node *save_last = NULL;
    for (free_node *iter = m_backbuffer_head; iter != NULL; iter = iter->get_freelist_next ())
      {
	++list_count;
	save_last = iter;
      }
    assert (list_count == m_bb_count);
    assert (list_count == m_block_size);
    assert (save_last == m_backbuffer_tail);

    // check available
    list_count = 0;
    for (free_node *iter = m_available_list; iter != NULL; iter = iter->get_freelist_next ())
      {
	++list_count;
      }
    assert (list_count == m_available_count);
#endif // DEBUG
  }

  template<class T>
  void
  freelist<T>::check_my_pointer (free_node *node)
  {
    assert (this == node->m_owner);
  }

  //
  // freelist::handle
  //
  template<class T>
  freelist<T>::free_node::free_node ()
    : tran::reclaimable_node ()
    , m_owner (NULL)
    , m_t {}
  {
  }

  template<class T>
  void
  freelist<T>::free_node::set_owner (freelist &fl)
  {
    m_owner = &fl;
  }

  template<class T>
  void
  freelist<T>::free_node::set_freelist_next (free_node *next)
  {
    m_retired_next = next;
  }

  template<class T>
  void
  freelist<T>::free_node::reset_freelist_next ()
  {
    m_retired_next = NULL;
  }

  template<class T>
  typename freelist<T>::free_node *
  freelist<T>::free_node::get_freelist_next ()
  {
    return static_cast<free_node *> (m_retired_next);
  }

  template<class T>
  void
  freelist<T>::free_node::reclaim ()
  {
    m_t.on_reclaim ();

    m_retired_next = NULL;
    --m_owner->m_retired_count;
    ++m_owner->m_available_count;
    m_owner->push_to_list (*this, *this, m_owner->m_available_list);
  }

  template<class T>
  T &
  freelist<T>::free_node::get_data ()
  {
    return m_t;
  }
} // namespace lockfree

#endif // !_LOCKFREE_FREELIST_HPP_
