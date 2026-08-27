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

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <limits>
#include <new>
#include <type_traits>

namespace lockfree
{
  namespace tran
  {
    class system;
  }
}

namespace lockfree
{
  // Does T offer its own on_reclaim ()? Standalone users of freelist supply one; the hashmap does not - it
  // reclaims through the entry descriptor's f_uninit, which only the hashmap knows about, so it overrides
  // on_node_reclaim () instead.
  template <class U, class = void>
  struct has_on_reclaim : std::false_type {};
  template <class U>
  struct has_on_reclaim<U, std::void_t<decltype (std::declval<U &> ().on_reclaim ())>> : std::true_type {};

  template <class T>
  class freelist : public tran::reclaimable_owner
  {
    public:
      class free_node;

      // tran::reclaimable_owner - the single vtable behind node reclamation. Every node retired into this
      // freelist's tran::table comes back here, so the nodes need carry no dispatch of their own.
      void reclaim_run (tran::reclaimable_node *head, tran::reclaimable_node *tail, size_t count) final override;

    protected:
      // Deleting the transaction table reclaims whatever its descriptors still hold, and that reclamation
      // goes through on_node_reclaim (). A derived override of it is gone by the time ~freelist () runs -
      // during base destruction the dynamic type is freelist<T> - so the most-derived class must drain here,
      // from its own destructor, while its override still answers. ~freelist () repeats the call for the
      // classes that add nothing; it is idempotent.
      void drain_transaction_table ()
      {
	delete m_trantable;
	m_trantable = NULL;
      }

      // called once per node being reclaimed, before the node rejoins the available list
      virtual void on_node_reclaim (T &t)
      {
	if constexpr (has_on_reclaim<T>::value)
	  {
	    t.on_reclaim ();
	  }
	else
	  {
	    (void) t;
	  }
      }

    public:

      freelist () = delete;
      freelist (tran::system &transys, size_t block_size, size_t initial_block_count = 1);
      ~freelist ();

      free_node *claim (tran::descriptor &tdes);
      free_node *claim (tran::index tran_index);                // claim a free node
      // note: on success the transaction will remain started! on failure it is ended again if this call is what
      //       started it, as lf_freelist_claim () does.

      void retire (tran::descriptor &tdes, free_node &node);
      void retire (tran::index tran_index, free_node &node);

      void set_max_alloc_count (size_t max_alloc_count);

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
      // above this, reclaim frees instead of recycling - edesc->max_alloc_cnt (CBRD-24474). uncapped by default.
      std::atomic<size_t> m_max_alloc_count;
      std::atomic<size_t> m_bb_count;
      std::atomic<size_t> m_forced_alloc_count;
      std::atomic<size_t> m_retired_count;

      void swap_backbuffer ();
      void alloc_backbuffer ();
      bool force_alloc_block ();

      size_t alloc_list (free_node *&head, free_node *&tail);
      size_t dealloc_list (free_node *head);

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

    private:
      friend freelist;

      void set_freelist_next (free_node *next);
      void reset_freelist_next (void);
      free_node *get_freelist_next ();

      // no owner pointer: the freelist that reclaims this node is reached from the descriptor's table,
      // once per run rather than once per node.
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
    , m_max_alloc_count { std::numeric_limits<size_t>::max () }
    , m_bb_count { 0 }
    , m_forced_alloc_count { 0 }
    , m_retired_count { 0 }
  {
    // minimum two blocks
    if (initial_block_count <= 1)
      {
	// halve, but never below one: at zero alloc_backbuffer () publishes an empty block and push_to_list ()
	// dereferences NULL. lf_freelist_init () accepts a block of one - xcache_initialize () passes one for
	// max_plan_cache_entries <= 3 - so this constructor must not be stricter.
	m_block_size = std::max<size_t> (m_block_size / 2, 1);
	initial_block_count = 2;
      }
    assert (m_block_size > 0);

    // the descriptors of this table reclaim through us; nothing else retires into them
    m_trantable->set_reclaimable_owner (*this);

    // initial_block_count blocks in total, the back-buffer's included. lf_freelist_init () allocates exactly
    // that many, and lock_dump_resource () prints the count, so one block more would change cubrid lockdb.
    alloc_backbuffer ();
    for (size_t i = 1; i < initial_block_count; i++)
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

    // credit what is moved, not m_bb_count: a swap racing alloc_backbuffer () used to read 0 here and publish
    // a whole block with no count behind it, so claim () popped a node with m_available_count already at 0 and
    // wrapped it. the back-buffer holds exactly one block, as final_sanity_checks () also relies on.
    m_bb_count -= m_block_size;
    m_available_count += m_block_size;
    push_to_list (*bb_head, *bb_tail, m_available_list);

    alloc_backbuffer ();
  }

  template <class T>
  void
  freelist<T>::alloc_backbuffer ()
  {
    free_node *new_bb_head = NULL;
    free_node *new_bb_tail = NULL;

    if (alloc_list (new_bb_head, new_bb_tail) < m_block_size)
      {
	// the back-buffer must hold a whole block or nothing: swap_backbuffer () credits m_block_size for it, and
	// final_sanity_checks () asserts the same. give a short block back and leave the buffer empty - claim ()
	// falls through to force_alloc_block () and reports the failure from there.
	m_alloc_count -= dealloc_list (new_bb_head);
	return;
      }

    // update backbuffer tail
    free_node *dummy_null = NULL;
    m_backbuffer_tail.compare_exchange_strong (dummy_null, new_bb_tail);

    // count the block before publishing it, so a swap that takes the list never finds it uncounted
    m_bb_count += m_block_size;
    push_to_list (*new_bb_head, *new_bb_tail, m_backbuffer_head);
  }

  template <class T>
  bool
  freelist<T>::force_alloc_block ()
  {
    free_node *new_head = NULL;
    free_node *new_tail = NULL;
    size_t allocated = alloc_list (new_head, new_tail);
    if (allocated == 0)
      {
	return false;
      }

    // push directly to available; a short block is still usable here
    m_available_count += allocated;
    ++m_forced_alloc_count;
    push_to_list (*new_head, *new_tail, m_available_list);
    return true;
  }

  template <class T>
  size_t
  freelist<T>::alloc_list (free_node *&head, free_node *&tail)
  {
    head = tail = NULL;
    free_node *node;
    size_t allocated = 0;
    for (size_t i = 0; i < m_block_size; i++)
      {
	// nothrow, and checked: lf_freelist_alloc_block () reported ER_OUT_OF_VIRTUAL_MEMORY and let the caller
	// fail the statement. throwing from here would unwind through a started lock-free transaction that
	// nothing ends, pinning the minimum active id and stopping reclamation for this table for good.
	node = new (std::nothrow) free_node ();
	if (node == NULL)
	  {
	    break;
	  }
	if (tail == NULL)
	  {
	    tail = node;
	  }
	node->set_freelist_next (head);
	head = node;
	++allocated;
      }
    m_alloc_count += allocated;
    return allocated;
  }

  template <class T>
  size_t
  freelist<T>::dealloc_list (free_node *head)
  {
    // free all; returns how many, so a caller undoing a partial allocation can correct m_alloc_count
    size_t count = 0;
    free_node *save_next = NULL;
    for (free_node *node = head; node != NULL; node = save_next)
      {
	save_next = node->get_freelist_next ();
	delete node;
	++count;
      }
    return count;
  }

  template <class T>
  freelist<T>::~freelist ()
  {
    // a derived class that overrides on_node_reclaim () has already done this from its own destructor
    drain_transaction_table ();
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
    const bool is_local_tran = !tdes.is_tran_started ();
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
	if (!force_alloc_block ())
	  {
	    // out of memory. legacy lf_freelist_claim () answered NULL here and its callers - xcache_new_entry ()
	    // among them - already expect that. It also ended the transaction it had opened
	    // (lock_free.c:850-857): a descriptor left holding a published id pins the table's minimum active id
	    // and stops reclamation for this table for the life of the process.
	    if (is_local_tran)
	      {
		tdes.end_tran ();
	      }
	    return NULL;
	  }
	node = pop_from_available ();
      }

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
  void
  freelist<T>::set_max_alloc_count (size_t max_alloc_count)
  {
    m_max_alloc_count = max_alloc_count;
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
    // a whole block, or nothing: alloc_backbuffer () gives a short block back and leaves the buffer empty when
    // it cannot allocate, and claim () falls through to force_alloc_block () from there.
    assert (list_count == m_block_size || list_count == 0);
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
    // the node no longer carries its owner; ownership is a property of the tran::table it retires into
    assert (node != NULL);
    (void) node;
  }

  //
  // freelist::handle
  //
  template<class T>
  freelist<T>::free_node::free_node ()
    : tran::reclaimable_node ()
    , m_t {}
  {
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
  freelist<T>::reclaim_run (tran::reclaimable_node *head, tran::reclaimable_node *tail, size_t count)
  {
    // splicing is sound because every node in a descriptor's retired list belongs to this freelist: each
    // freelist builds its own tran::table, so nothing else can retire into its descriptors.
    freelist *owner = this;
    free_node *run_head = NULL;
    free_node *run_tail = NULL;
    size_t reusable = 0;
    size_t freed = 0;
    size_t alloc_now = owner->m_alloc_count.load ();
    free_node *save_next = NULL;

    for (free_node *node = static_cast<free_node *> (head); node != NULL; node = save_next)
      {
	save_next = (node == static_cast<free_node *> (tail)) ? NULL : node->get_freelist_next ();
	on_node_reclaim (node->m_t);
	node->reset_freelist_next ();

	if (alloc_now > owner->m_max_alloc_count)
	  {
	    // over the cap: free rather than recycle, as lf_freelist_transport () does. only nodes older than
	    // the minimum active transaction reach here, so the node is already unreachable.
	    delete node;
	    --alloc_now;
	    ++freed;
	    continue;
	  }

	// prepend: the first node kept becomes the tail, already NULL-linked
	if (run_tail == NULL)
	  {
	    run_tail = node;
	  }
	node->set_freelist_next (run_head);
	run_head = node;
	++reusable;
      }

    owner->m_retired_count -= count;
    if (freed != 0)
      {
	owner->m_alloc_count -= freed;
      }
    if (run_head != NULL)
      {
	// the whole run joins with one CAS and one counter update
	owner->m_available_count += reusable;
	owner->push_to_list (*run_head, *run_tail, owner->m_available_list);
      }
  }

  template<class T>
  T &
  freelist<T>::free_node::get_data ()
  {
    return m_t;
  }
} // namespace lockfree

#endif // !_LOCKFREE_FREELIST_HPP_
