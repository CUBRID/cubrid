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
 * lock_free.h : Lock-free structures interface.
 */

#ifndef _LOCKFREE_CIRCULAR_QUEUE_HPP_
#define _LOCKFREE_CIRCULAR_QUEUE_HPP_

#include <type_traits>
#include <climits>
#include <cstdint>
#ifdef USE_STD_ATOMIC
#include <atomic>
/* TODO: use std::atomic. However, we must give up on systems with gcc 4.5 or older and VS 2010 */
#endif // USE_STD_ATOMIC
#include <cassert>

#if !defined (DEBUG_LGFCQ) && !defined (NDEBUG)
#define DEBUG_LFCQ
#endif // !defined (DEBUG_LGFCQ) && !defined (NDEBUG)

namespace lockfree {

template <class T>
class circular_queue
{
#if defined (DEBUG_LFCQ)
private:
  enum class local_action;
  struct local_event;
#endif // defined (DEBUG_LFCQ)

public:
#ifdef USE_STD_ATOMIC
  // std::atomic
  typedef std::uint64_t cursor_type;
  typedef std::atomic<cursor_type> atomic_cursor_type;
  typedef std::atomic<T> atomic_data_type;
#else // not USE_STD_ATOMIC
  typedef std::uint64_t cursor_type;
  typedef volatile cursor_type atomic_cursor_type;
  typedef volatile T atomic_data_type;
#endif // not USE_STD_ATOMIC
  typedef atomic_cursor_type atomic_flag_type;

  circular_queue (size_t size);
  ~circular_queue ();

#if defined (DEBUG_LFCQ)
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
    static const size_t LOCAL_HISTORY_SIZE = 64;
    local_event m_events[LOCAL_HISTORY_SIZE];
    size_t m_cursor;
  };
#endif // DEBUG_LFCQ

  // produce data. if successful, returns true, if full, returns false
  inline bool produce (const T& element
#if defined (DEBUG_LFCQ)
                       , local_history & my_history = m_shared_dummy_history
#endif // DEBUG_LFCQ
                       );
  // consume data
  inline bool consume (T& element
#if defined (DEBUG_LFCQ)
                       , local_history & my_history = m_shared_dummy_history
#endif // DEBUG_LFCQ
                       );
  inline bool is_empty () const;
  inline bool is_full () const;

private:
  static const cursor_type BLOCK_FLAG;

  // block default and copy constructors
  circular_queue ();
  circular_queue (const circular_queue&);

  inline size_t next_pow2 (size_t size);

  inline bool test_empty_cursors (cursor_type produce_cursor, cursor_type consume_cursor);
  inline bool test_full_cursors (cursor_type produce_cursor, cursor_type consume_cursor);

  inline cursor_type load_cursor (atomic_cursor_type & cursor);
  inline bool test_and_increment_cursor (atomic_cursor_type& cursor, cursor_type crt_value);

  inline T load_data (cursor_type consume_cursor);
  inline void store_data (size_t index, const T& data);
  inline size_t get_cursor_index (cursor_type cursor);

  inline bool is_blocked (cursor_type cursor);
  inline bool block (cursor_type cursor);
  inline void unblock (cursor_type cursor);
  inline void init_blocked_cursors (void);

  atomic_data_type *m_data;   // data storage. access is atomic
  atomic_flag_type *m_blocked_cursors;  // is_blocked flag; when producing new data, there is a time window between
                                        // cursor increment and until data is copied. block flag will tell when
                                        // produce is completed.
  atomic_cursor_type m_produce_cursor;  // cursor for produce position
  atomic_cursor_type m_consume_cursor;  // cursor for consume position
  size_t m_capacity;                    // queue capacity
  size_t m_index_mask;                  // mask used to compute a cursor's index in queue

#if defined (DEBUG_LFCQ)
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

#endif // !_LOCKFREE_CIRCULAR_QUEUE_HPP_

#include "base_flag.hpp"
#ifndef USE_STD_ATOMIC
#include "porting.h"
#endif /* not USE_STD_ATOMIC */

namespace lockfree {

template<class T>
typename circular_queue<T>::cursor_type const circular_queue<T>::BLOCK_FLAG =
  ((cursor_type) 1) << ((sizeof (cursor_type) * CHAR_BIT) - 1);         // 0x8000...
  

template<class T>
inline
circular_queue<T>::circular_queue (size_t size) :
  m_produce_cursor (0),
  m_consume_cursor (0)
{
  m_capacity = next_pow2 (size);
  m_index_mask = m_capacity - 1;
  assert ((m_capacity & m_index_mask) == 0);

  m_data = new atomic_data_type[m_capacity];
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
circular_queue<T>::produce (const T & element
#if defined (DEBUG_LFCQ)
                            , local_history & my_history
#endif // DEBUG_LFCQ
                            )
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

  while (true)
    {
#if defined (DEBUG_LFCQ)
      my_history.register_event (local_action::LOOP_PRODUCE);
#endif // DEBUG_LFCQ

      pc = load_cursor (m_produce_cursor);
#if defined (DEBUG_LFCQ)
      my_history.register_event (local_action::LOAD_PRODUCE_CURSOR, pc);
#endif // DEBUG_LFCQ

      cc = load_cursor (m_consume_cursor);
#if defined (DEBUG_LFCQ)
      my_history.register_event (local_action::LOAD_CONSUME_CURSOR, cc);
#endif // DEBUG_LFCQ

      if (test_full_cursors (pc, cc))
        {
          /* cannot produce */
#if defined (DEBUG_LFCQ)
          my_history.register_event (local_action::QUEUE_FULL);
#endif // DEBUG_LFCQ
          return false;
        }

      // first block position
      did_block = block (pc);
#if defined (DEBUG_LFCQ)
      my_history.register_event (did_block ? local_action::BLOCKED_CURSOR : local_action::NOT_BLOCKED_CURSOR, pc);
#endif // DEBUG_LFCQ

      // make sure cursor is incremented whether I blocked it or not
      if (test_and_increment_cursor (m_produce_cursor, pc))
        {
          // do nothing
#if defined (DEBUG_LFCQ)
          my_history.register_event (local_action::INCREMENT_PRODUCE_CURSOR, pc);
        }
      else
        {
          my_history.register_event (local_action::NOT_INCREMENT_PRODUCE_CURSOR, pc);
#endif // DEBUG_LFCQ
        }
      if (did_block)
        {
          /* I blocked it, it is mine. I can write my data. */
          store_data (pc, element);
#if defined (DEBUG_LFCQ)
          my_history.register_event (local_action::STORED_DATA, element);
#endif // DEBUG_LFCQ

          unblock (pc);
#if defined (DEBUG_LFCQ)
          my_history.register_event (local_action::UNBLOCKED_CURSOR, pc);
#endif // DEBUG_LFCQ
          return true;
        }
    }
}

template<class T>
inline bool circular_queue<T>::consume (T & element
#if defined (DEBUG_LFCQ)
                                        , local_history & my_history
#endif // DEBUG_LFCQ
                                        )
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
  // the slot is freed for further produce operations immediately after the update.
  //

  while (true)
    {
#if defined (DEBUG_LFCQ)
      my_history.register_event (local_action::LOOP_CONSUME);
#endif /* DEBUG_LFCQ */

      cc = load_cursor (m_consume_cursor);
#if defined (DEBUG_LFCQ)
      my_history.register_event (local_action::LOAD_CONSUME_CURSOR, cc);
#endif /* DEBUG_LFCQ */

      pc = load_cursor (m_produce_cursor);
#if defined (DEBUG_LFCQ)
      my_history.register_event (local_action::LOAD_PRODUCE_CURSOR, pc);
#endif /* DEBUG_LFCQ */

      if (pc <= cc)
        {
          /* empty */
#if defined (DEBUG_LFCQ)
          my_history.register_event (local_action::QUEUE_EMPTY);
#endif /* DEBUG_LFCQ */
          return false;
        }

      if (is_blocked (cc))
        {
          // first element is not yet produced. this means we can consider the queue still empty
#if defined (DEBUG_LFCQ)
          my_history.register_event (local_action::QUEUE_EMPTY);
#endif /* DEBUG_LFCQ */
          return false;
        }

      // copy element first. however, the consume is not actually happening until cursor is successfully incremented.
      element = load_data (cc);
#if defined (DEBUG_LFCQ)
      my_history.register_event (local_action::LOADED_DATA, element);
#endif /* DEBUG_LFCQ */

      if (test_and_increment_cursor (m_consume_cursor, cc))
        {
          // consume is complete
#if defined (DEBUG_LFCQ)
          my_history.register_event (local_action::INCREMENT_CONSUME_CURSOR, cc);
#endif /* DEBUG_LFCQ */

          /* break loop */
          break;
        }
      else
        {
          // consume unsuccessful
#if defined (DEBUG_LFCQ)
          my_history.register_event (local_action::NOT_INCREMENT_CONSUME_CURSOR, cc);
#endif /* DEBUG_LFCQ */
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
inline size_t circular_queue<T>::next_pow2 (size_t size)
{
  size_t next_pow = 1;
  for (--size; size != 0; size /= 2)
    {
      next_pow *= 2;
    }
  return next_pow;
}

template<class T>
inline bool circular_queue<T>::test_empty_cursors (cursor_type produce_cursor, cursor_type consume_cursor)
{
  return produce_cursor >= consume_cursor;
}

template<class T>
inline bool circular_queue<T>::test_full_cursors (cursor_type produce_cursor, cursor_type consume_cursor)
{
  return consume_cursor + m_capacity <= produce_cursor;
}

template<class T>
inline typename circular_queue<T>::cursor_type
circular_queue<T>::load_cursor (atomic_cursor_type & cursor)
{
#ifdef USE_STD_ATOMIC
  return cursor.load ();
#else
  return ATOMIC_LOAD (&cursor);
#endif /* */
}

template<class T>
inline bool circular_queue<T>::test_and_increment_cursor (atomic_cursor_type & cursor, cursor_type crt_value)
{
#ifdef USE_STD_ATOMIC
  // can weak be used here?
  return cursor.compare_exchange_strong (crt_value, crt_value + 1);
#else /* not USE_STD_ATOMIC */
  return ATOMIC_CAS (&cursor, crt_value, crt_value + 1);
#endif /* not USE_STD_ATOMIC */
}

template<class T>
inline void circular_queue<T>::store_data (cursor_type cursor, const T & data)
{
#ifdef USE_STD_ATOMIC
  m_data[get_cursor_index (cursor)].store (data);
#else /* not USE_STD_ATOMIC */
  ATOMIC_STORE (&m_data[get_cursor_index (cursor)], data);
#endif /* not USE_STD_ATOMIC */
}

template<class T>
inline T
circular_queue<T>::load_data (cursor_type consume_cursor)
{
#ifdef USE_STD_ATOMIC
  return m_data[get_cursor_index (consume_cursor)].load ();
#else /* not USE_STD_ATOMIC */
  return ATOMIC_LOAD (&m_data[get_cursor_index (consume_cursor)]);
#endif /* not USE_STD_ATOMIC */
}

template<class T>
inline size_t circular_queue<T>::get_cursor_index (cursor_type cursor)
{
  return cursor & m_index_mask;
}

template<class T>
inline bool
circular_queue<T>::is_blocked (cursor_type cursor)
{
#ifdef USE_STD_ATOMIC
  cursor_type block_val = m_blocked_cursors[get_cursor_index (cursor)].load ();
#else /* not USE_STD_ATOMIC */
  cursor_type block_val = ATOMIC_LOAD (&m_blocked_cursors[get_cursor_index (cursor)]);
#endif /* not USE_STD_ATOMIC */
  return flag<cursor_type>::is_flag_set (block_val, BLOCK_FLAG);
}

template<class T>
inline bool
circular_queue<T>::block (cursor_type cursor)
{
  cursor_type block_val = flag<cursor_type> (cursor).set (BLOCK_FLAG).get_flags ();
#ifdef USE_STD_ATOMIC
  // can weak be used here?
  return m_blocked_cursors[get_cursor_index (cursor)].compare_exchange_strong (cursor, block_val);
#else /* not USE_STD_ATOMIC */
  return ATOMIC_CAS (&m_blocked_cursors[get_cursor_index (cursor)], cursor, block_val);
#endif /* not USE_STD_ATOMIC */
}

template<class T>
inline void
circular_queue<T>::unblock (cursor_type cursor)
{
  atomic_flag_type& ref_blocked_cursor = m_blocked_cursors[get_cursor_index (cursor)];
#ifdef USE_STD_ATOMIC
  flag<cursor_type> blocked_cursor_value = ref_blocked_cursor.load ();
#else /* not USE_STD_ATOMIC */
  flag<cursor_type> blocked_cursor_value = ATOMIC_LOAD (&ref_blocked_cursor);
#endif /* not USE_STD_ATOMIC */

  assert (blocked_cursor_value.is_set (BLOCK_FLAG));
  cursor_type nextgen_cursor = blocked_cursor_value.clear (BLOCK_FLAG).get_flags () + m_capacity;

#ifdef USE_STD_ATOMIC
  ref_blocked_cursor.store (nextgen_cursor);
#else /* not USE_STD_ATOMIC */
  ATOMIC_STORE (&ref_blocked_cursor, nextgen_cursor);
#endif /* not USE_STD_ATOMIC */
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
template <class T>
typename circular_queue<T>::local_history circular_queue<T>::m_shared_dummy_history;
#endif // DEBUG_LFCQ

}  // namespace lockfree
