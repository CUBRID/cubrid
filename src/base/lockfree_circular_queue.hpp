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
 * lockfree_circular_queue.hpp : Lock-free structures interface.
 */

#ifndef _LOCKFREE_CIRCULAR_QUEUE_HPP_
#define _LOCKFREE_CIRCULAR_QUEUE_HPP_

#include "base_flag.hpp"

#include <atomic>
#include <cassert>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <type_traits>

// activate preprocessor if you need to debug low-level execution of lockfree circular queue
// note that if you wanna track the execution history of low-level operation of a certain queue, you also need to
// replace produce/consume functions with produce_debug/consume_debug
//
// #define DEBUG_LFCQ

namespace lockfree
{

  template <class T>
  class circular_queue
  {
    public:
      using cursor_type = std::uint64_t;
      using atomic_cursor_type = std::atomic<cursor_type>;
      using data_type = T;
      using atomic_flag_type = atomic_cursor_type;

      circular_queue (std::size_t size);
      ~circular_queue ();

      inline bool is_empty () const;              // is query empty? use it for early outs but don't rely on the answer
      inline bool is_full () const;               // is query full? use it for early outs but don't rely on the answer
      inline bool is_half_full ();
      inline std::size_t size ();

      inline bool consume (T &element);               // consume one element from queue; returns false on fail
      // IMPORTANT!
      //   Element argument may change even if consume fails.
      //   Using its value after failed consumption is not safe.
      inline bool produce (const T &element);         // produce an element to queue; returns false on fail
      inline void force_produce (const T &element);   // force produce (loop until successful)

      inline std::uint64_t get_consumer_cursor ();            // get consume cursor

      // note:
      //
      //    above functions are cloned by debug counterparts which track a history of executed low-level operations.
      //    they were not cloned initially, but the code became unreadable with all the #ifdef DEBUG_LFCQ/#endif
      //    preprocessor directives.
      //    therefore, if you update the code for any of these functions, please make sure the debug counterparts are
      //    updated too.

    private:
      static const cursor_type BLOCK_FLAG;

      // block default and copy constructors
      circular_queue ();
      circular_queue (const circular_queue &);

      inline std::size_t next_pow2 (std::size_t size) const;

      inline bool test_empty_cursors (cursor_type produce_cursor, cursor_type consume_cursor) const;
      inline bool test_full_cursors (cursor_type produce_cursor, cursor_type consume_cursor) const;

      inline cursor_type load_cursor (atomic_cursor_type &cursor);
      inline bool test_and_increment_cursor (atomic_cursor_type &cursor, cursor_type crt_value);

      inline T load_data (cursor_type consume_cursor) const;
      inline void store_data (cursor_type index, const T &data);
      inline std::size_t get_cursor_index (cursor_type cursor) const;

      inline bool is_blocked (cursor_type cursor) const;
      inline bool block (cursor_type cursor);
      inline void unblock (cursor_type cursor);
      inline void init_blocked_cursors (void);

      data_type *m_data;   // data storage. access is atomic
      atomic_flag_type *m_blocked_cursors;  // is_blocked flag; when producing new data, there is a time window between
      // cursor increment and until data is copied. block flag will tell when
      // produce is completed.
      atomic_cursor_type m_produce_cursor;  // cursor for produce position
      atomic_cursor_type m_consume_cursor;  // cursor for consume position
      std::size_t m_capacity;                    // queue capacity
      std::size_t m_index_mask;                  // mask used to compute a cursor's index in queue

#if defined (DEBUG_LFCQ)
    private:
      enum class local_action;
      struct local_event;

    public:
      class local_history
      {
	public:
	  local_history ()
	    : m_cursor (0)
	  {
	  }

	  inline void register_event (local_action action)
	  {
	    m_cursor = (m_cursor + 1) % LOCAL_HISTORY_SIZE;
	    m_events[m_cursor].action = action;
	  }

	  inline void register_event (local_action action, cursor_type cursor)
	  {
	    register_event (action);
	    m_events[m_cursor].m_consequence.cursor_value = cursor;
	  }

	  inline void register_event (local_action action, T data)
	  {
	    register_event (action);
	    m_events[m_cursor].m_consequence.data_value = data;
	  }
	private:
	  static const std::size_t LOCAL_HISTORY_SIZE = 64;
	  local_event m_events[LOCAL_HISTORY_SIZE];
	  std::size_t m_cursor;
      };

      // clones of produce/consume with additional debug code. caller should keep its history in thread context to be
      // inspected on demand
      inline bool consume_debug (T &element, local_history &my_history);
      inline bool produce_debug (const T &element, local_history &my_history);
      inline bool force_produce_debug (const T &element, local_history &my_history);

    private:

      enum class local_action
      {
	NO_ACTION,
	LOOP_PRODUCE,
	LOOP_CONSUME,
	LOAD_PRODUCE_CURSOR,
	LOAD_CONSUME_CURSOR,
	INCREMENT_PRODUCE_CURSOR,
	INCREMENT_CONSUME_CURSOR,
	NOT_INCREMENT_PRODUCE_CURSOR,
	NOT_INCREMENT_CONSUME_CURSOR,
	BLOCKED_CURSOR,
	NOT_BLOCKED_CURSOR,
	UNBLOCKED_CURSOR,
	LOADED_DATA,
	STORED_DATA,
	QUEUE_FULL,
	QUEUE_EMPTY
      };
      struct local_event
      {
	local_action action;
	union consequence
	{
	  consequence () : cursor_value (0) {}

	  cursor_type cursor_value;
	  T data_value;
	} m_consequence;

	local_event ()
	  : action (local_action::NO_ACTION)
	  , m_consequence ()
	{
	}
      };
      static local_history m_shared_dummy_history;
#endif // DEBUG_LFCQ
  };

} // namespace lockfree

namespace lockfree
{

  template<class T>
  typename circular_queue<T>::cursor_type const circular_queue<T>::BLOCK_FLAG =
	  ((cursor_type) 1) << ((sizeof (cursor_type) * CHAR_BIT) - 1);         // 0x8000...

  template<class T>
  inline
  circular_queue<T>::circular_queue (std::size_t size) :
    m_produce_cursor (0),
    m_consume_cursor (0)
  {
    m_capacity = next_pow2 (size);
    m_index_mask = m_capacity - 1;
    assert ((m_capacity & m_index_mask) == 0);

    m_data = new data_type[m_capacity];
    init_blocked_cursors ();
  }

  template<class T>
  inline
  circular_queue<T>::~circular_queue ()
  {
    delete [] m_data;
    delete [] m_blocked_cursors;
  }

  template<class T>
  inline bool
  circular_queue<T>::produce (const T &element)
  {
    cursor_type cc;
    cursor_type pc;
    bool did_block;

    // how this works:
    //
    // in systems where concurrent producing is possible, we need to avoid synchronize them. in this lock-free circular
    // queue, that translates to each thread saving its data in a data array unique slot.
    //
    //  current way of doing the synchronization is having a block flag for each slot in the array. when a produced wants
    //  to push its data, it must first successfully block the slot to avoid racing others to write in the slot.
    //
    //  the produce algorithm is a loop where:
    //  1. early out if queue is full.
    //  2. get current produce cursor, then try block its slot, then try to increment cursor (compare & swap);
    //     incrementing is executed regardless of blocking success. that's because if block fails, then someone else
    //     blocked this slot. everyone who failed should advance to next, and incrementing cursor as fast as possible
    //     helps.
    //  3. if block was successful, then store data to the blocked slot and unlock for next generation.
    //
    //  the loop can be broken in two ways:
    //  1. the queue is full and push fails
    //  2. the producer successfully blocks a cursor
    //
    // NOTE: I have an implementation issue I don't know how to fix without a significant overhead. When the queue is
    //       very stressed (many threads produce/consume very often), the system may preempt a thread while it has a slot
    //       blocked and may keep it preempted for a very long time (long enough to produce and consume an entire
    //       generation). I'd like to any blocking of any kind.
    //       One possible direction is to separate data storage (and slot index) from cursor. When one wants to produce
    //       an element, it first reserves a slot in storage, adds his data, and then saves slot index/cursor in what is
    //       now m_blocked_cursors using CAS operation. if this succeeds, produced data is immediately available for
    //       consumption. any preemption would not block the queue (just delay when the produce is happening).
    //
    //       however, finding a way to dispatch slots to producers safely and without a sensible overhead is not quite
    //       straightforward.
    //

    while (true)
      {
	pc = load_cursor (m_produce_cursor);
	cc = load_cursor (m_consume_cursor);

	if (test_full_cursors (pc, cc))
	  {
	    /* cannot produce */
	    return false;
	  }

	// first block position
	did_block = block (pc);

	// make sure cursor is incremented whether I blocked it or not
	if (test_and_increment_cursor (m_produce_cursor, pc))
	  {
	    // do nothing
	  }
	if (did_block)
	  {
	    /* I blocked it, it is mine. I can write my data. */
	    store_data (pc, element);

	    unblock (pc);
	    return true;
	  }
      }
  }

  template<class T>
  inline void
  circular_queue<T>::force_produce (const T &element)
  {
    while (!produce (element))
      {
	std::this_thread::yield ();
      }
  }

  template<class T>
  inline std::uint64_t
  circular_queue<T>::get_consumer_cursor ()
  {
    return m_consume_cursor;
  }

  template<class T>
  inline bool
  circular_queue<T>::consume (T &element)
  {
    cursor_type cc;
    cursor_type pc;

    // how consume works:
    //
    // condition: every produced entry must be consumed once and only once.
    //
    // to make sure this condition is met and consume is possible concurrently and without locks, a consume cursor is
    // used. the entry at a certain cursor is consumed only by the thread who successfully increments the cursor by one
    // (using compare and swap, not atomic increment). the "consumed" entry is read before the cursor update, therefore
    // the slot is freed for further produce operations immediately after the update
    //
    // unlike producers, a consumer cannot block the queue if it is preempted during execution
    //

    while (true)
      {
	cc = load_cursor (m_consume_cursor);
	pc = load_cursor (m_produce_cursor);

	if (test_empty_cursors (pc, cc))
	  {
	    /* empty */
	    return false;
	  }

	if (is_blocked (cc))
	  {
	    // first element is not yet produced. this means we can consider the queue still empty
	    return false;
	  }

	// copy element first. however, the consume is not actually happening until cursor is successfully incremented.
	element = load_data (cc);

	if (test_and_increment_cursor (m_consume_cursor, cc))
	  {
	    // consume is complete

	    /* break loop */
	    break;
	  }
	else
	  {
	    // consume unsuccessful
	  }
      }

    // consume successful
    return true;
  }

  template<class T>
  inline bool
  circular_queue<T>::is_empty () const
  {
    return test_empty_cursors (m_produce_cursor, m_consume_cursor);
  }

  template<class T>
  inline bool
  circular_queue<T>::is_full () const
  {
    return test_full_cursors (m_produce_cursor, m_consume_cursor);
  }

  template<class T>
  inline bool
  circular_queue<T>::is_half_full ()
  {
    return size () >= (m_capacity / 2);
  }

  template<class T>
  inline std::size_t
  circular_queue<T>::size ()
  {
    cursor_type cc = load_cursor (m_consume_cursor);
    cursor_type pc = load_cursor (m_produce_cursor);

    if (pc <= cc)
      {
	return 0;
      }

    return pc - cc;
  }

  template<class T>
  inline std::size_t circular_queue<T>::next_pow2 (std::size_t size) const
  {
    std::size_t next_pow = 1;
    for (--size; size != 0; size /= 2)
      {
	next_pow *= 2;
      }
    return next_pow;
  }

  template<class T>
  inline bool circular_queue<T>::test_empty_cursors (cursor_type produce_cursor, cursor_type consume_cursor) const
  {
    return produce_cursor <= consume_cursor;
  }

  template<class T>
  inline bool circular_queue<T>::test_full_cursors (cursor_type produce_cursor, cursor_type consume_cursor) const
  {
    return consume_cursor + m_capacity <= produce_cursor;
  }

  template<class T>
  inline typename circular_queue<T>::cursor_type
  circular_queue<T>::load_cursor (atomic_cursor_type &cursor)
  {
    return cursor.load ();
  }

  template<class T>
  inline bool
  circular_queue<T>::test_and_increment_cursor (atomic_cursor_type &cursor, cursor_type crt_value)
  {
    // can weak be used here? I tested, no performance difference from using one or the other
    return cursor.compare_exchange_strong (crt_value, crt_value + 1);
  }

  template<class T>
  inline std::size_t
  circular_queue<T>::get_cursor_index (cursor_type cursor) const
  {
    return cursor & m_index_mask;
  }

  template<class T>
  inline void
  circular_queue<T>::store_data (cursor_type cursor, const T &data)
  {
    m_data[get_cursor_index (cursor)] = data;
  }

  template<class T>
  inline T
  circular_queue<T>::load_data (cursor_type consume_cursor) const
  {
    return m_data[get_cursor_index (consume_cursor)];
  }

  template<class T>
  inline bool
  circular_queue<T>::is_blocked (cursor_type cursor) const
  {
    cursor_type block_val = m_blocked_cursors[get_cursor_index (cursor)].load ();
    return flag<cursor_type>::is_flag_set (block_val, BLOCK_FLAG);
  }

  template<class T>
  inline bool
  circular_queue<T>::block (cursor_type cursor)
  {
    cursor_type block_val = flag<cursor_type> (cursor).set (BLOCK_FLAG).get_flags ();
    // can weak be used here?
    return m_blocked_cursors[get_cursor_index (cursor)].compare_exchange_strong (cursor, block_val);
  }

  template<class T>
  inline void
  circular_queue<T>::unblock (cursor_type cursor)
  {
    atomic_flag_type &ref_blocked_cursor = m_blocked_cursors[get_cursor_index (cursor)];
    flag<cursor_type> blocked_cursor_value = ref_blocked_cursor.load ();

    assert (blocked_cursor_value.is_set (BLOCK_FLAG));
    cursor_type nextgen_cursor = blocked_cursor_value.clear (BLOCK_FLAG).get_flags () + m_capacity;

    ref_blocked_cursor.store (nextgen_cursor);
  }

  template<class T>
  inline void
  circular_queue<T>::init_blocked_cursors (void)
  {
    m_blocked_cursors = new atomic_flag_type [m_capacity];
    for (cursor_type cursor = 0; cursor < m_capacity; cursor++)
      {
	// set expected cursor values in first generation (matches index)
	m_blocked_cursors[cursor] = cursor;
      }
  }

#if defined (DEBUG_LFCQ)
  template<class T>
  inline bool
  circular_queue<T>::produce_debug (const T &element, local_history &my_history)
  {
    cursor_type cc;
    cursor_type pc;
    bool did_block;

    while (true)
      {
	my_history.register_event (local_action::LOOP_PRODUCE);

	pc = load_cursor (m_produce_cursor);
	my_history.register_event (local_action::LOAD_PRODUCE_CURSOR, pc);

	cc = load_cursor (m_consume_cursor);
	my_history.register_event (local_action::LOAD_CONSUME_CURSOR, cc);

	if (test_full_cursors (pc, cc))
	  {
	    /* cannot produce */
	    my_history.register_event (local_action::QUEUE_FULL);
	    return false;
	  }

	// first block position
	did_block = block (pc);
	my_history.register_event (did_block ? local_action::BLOCKED_CURSOR : local_action::NOT_BLOCKED_CURSOR, pc);

	// make sure cursor is incremented whether I blocked it or not
	if (test_and_increment_cursor (m_produce_cursor, pc))
	  {
	    // do nothing
	    my_history.register_event (local_action::INCREMENT_PRODUCE_CURSOR, pc);
	  }
	else
	  {
	    my_history.register_event (local_action::NOT_INCREMENT_PRODUCE_CURSOR, pc);
	  }
	if (did_block)
	  {
	    /* I blocked it, it is mine. I can write my data. */
	    store_data (pc, element);
	    my_history.register_event (local_action::STORED_DATA, element);

	    unblock (pc);
	    my_history.register_event (local_action::UNBLOCKED_CURSOR, pc);
	    return true;
	  }
      }
  }

  template<class T>
  inline bool circular_queue<T>::force_produce_debug (const T &element, local_history &my_history)
  {
    while (!produce_debug (element, my_history))
      {
	std::this_thread::yield ();
      }
  }

  template<class T>
  inline bool circular_queue<T>::consume_debug (T &element, local_history &my_history)
  {
    cursor_type cc;
    cursor_type pc;

    while (true)
      {
	my_history.register_event (local_action::LOOP_CONSUME);

	cc = load_cursor (m_consume_cursor);
	my_history.register_event (local_action::LOAD_CONSUME_CURSOR, cc);

	pc = load_cursor (m_produce_cursor);
	my_history.register_event (local_action::LOAD_PRODUCE_CURSOR, pc);

	if (pc <= cc)
	  {
	    /* empty */
	    my_history.register_event (local_action::QUEUE_EMPTY);
	    return false;
	  }

	if (is_blocked (cc))
	  {
	    // first element is not yet produced. this means we can consider the queue still empty
	    my_history.register_event (local_action::QUEUE_EMPTY);
	    return false;
	  }

	// copy element first. however, the consume is not actually happening until cursor is successfully incremented.
	element = load_data (cc);
	my_history.register_event (local_action::LOADED_DATA, element);

	if (test_and_increment_cursor (m_consume_cursor, cc))
	  {
	    // consume is complete
	    my_history.register_event (local_action::INCREMENT_CONSUME_CURSOR, cc);

	    /* break loop */
	    break;
	  }
	else
	  {
	    // consume unsuccessful
	    my_history.register_event (local_action::NOT_INCREMENT_CONSUME_CURSOR, cc);
	  }
      }

    // consume successful
    return true;
  }
#endif // DEBUG_LFCQ

}  // namespace lockfree

#endif // _LOCKFREE_CIRCULAR_QUEUE_HPP_
