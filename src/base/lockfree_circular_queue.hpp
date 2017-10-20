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
#if USE_STD_ATOMIC
#include <atomic>
/* TODO: use std::atomic. However, we must give up on systems with gcc 4.5 or older and VS 2010 */
#endif // USE_STD_ATOMIC

namespace lockfree {

template <class T>
class circular_queue
{
public:

  circular_queue (size_t size);
  ~circular_queue ();

  // produce data. if successful, returns true, if full, returns false
  inline bool produce (const T& element);
  // consume data
  inline bool consume (T& element);
  inline bool is_empty () const;
  inline bool is_full () const;

private:
#if USE_STD_ATOMIC
  // std::atomic
  typedef unsigned long long cursor_type;
  typedef std::atomic<cursor_type> atomic_cursor_type;
  typedef std::atomic<T> atomic_data_type;
#else // not USE_STD_ATOMIC
  typedef unsigned long long cursor_type;
  typedef volatile cursor_type atomic_cursor_type;
  typedef volatile T atomic_data_type;
#endif // not USE_STD_ATOMIC
  typedef atomic_cursor_type atomic_flag_type;

  static const cursor_type BLOCK_FLAG;

  // block default and copy constructors
  circular_queue ();
  circular_queue (const circular_queue&);

  inline size_t next_pow2 (size_t size);

  inline bool test_empty_cursors (cursor_type produce_cursor, cursor_type consume_cursor);
  inline bool test_full_cursors (cursor_type produce_cursor, cursor_type consume_cursor);

  inline cursor_type load_cursor (atomic_cursor_type & cursor);
  inline bool test_and_increment_cursor (atomic_cursor_type& cursor, cursor_type& crt_value);

  inline T load_data (size_t index);
  inline void store_data (size_t index, const T& data);
  inline size_t get_cursor_index (cursor_type cursor);

  inline bool is_blocked (cursor_type cursor);
  inline bool block (cursor_type cursor);
  inline void unblock (cursor_type cursor);

  atomic_data_type *m_data;   // data storage. access is atomic
  atomic_flag_type *m_blocked_cursors;  // is_blocked flag; when producing new data, there is a time window between
                                        // cursor increment and until data is copied. block flag will tell when
                                        // produce is completed.
  atomic_cursor_type m_produce_cursor;  // cursor for produce position
  atomic_cursor_type m_consume_cursor;  // cursor for consume position
  size_t m_capacity;                    // queue capacity
  size_t m_index_mask;                  // mask used to compute a cursor's index in queue
};

} // namespace lockfree

#endif // !_LOCKFREE_CIRCULAR_QUEUE_HPP_

#include "base_flag.hpp"
#ifndef USE_STD_ATOMIC
#include "porting.h"
#endif /* not USE_STD_ATOMIC */

namespace lockfree {

circular_queue::BLOCK_FLAG = (1 << (sizeof (BLOCK_FLAG) - 1)); // most significant bit

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
  m_blocked_cursors = new atomic_flag_type[m_capacity];
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
circular_queue<T>::produce (const T & element)
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
      pc = m_produce_cursor.load ();
      cc = m_consume_cursor.load ();

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
          store_data (idx, element);
          unblock (pc);
          return true;
        }
    }
}

template<class T>
inline bool circular_queue<T>::consume (T & element)
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

  do
    {
      cc = load_cursor (m_consume_cursor);
      pc = load_cursor (m_produce_cursor);

      if (pc <= cc)
        {
          /* empty */
          return false;
        }

      if (is_blocked (cc))
        {
          // first element is not yet produced. this means we can consider the queue still empty
          // todo: count these cases
          return false;
        }

      // copy element first. however, the consume is not actually happening until cursor is successfully incremented.
      element = load_data (idx);
    }
  while (!test_and_increment_cursor (m_consume_cursor, cc));

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
  for (; size != 0; size /= 2)
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
inline cursor_type circular_queue<T>::load_cursor (atomic_cursor_type & cursor)
{
#ifdef USE_STD_ATOMIC
  return cursor.load ();
#else
  ATOMIC_LOAD (&cursor);
#endif /* */
}

template<class T>
inline bool circular_queue<T>::test_and_increment_cursor (atomic_cursor_type & cursor, cursor_type & crt_value)
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
inline T circular_queue<T>::load_data (int index)
{
#ifdef USE_STD_ATOMIC
  return m_data[index].load ();
#else /* not USE_STD_ATOMIC */
  return ATOMIC_LOAD (&m_data[index]);
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
  cursor_type block_val = m_blocked_cursors[get_cursor_index (cursor)]->load ();
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
inline void circular_queue<T>::unblock (cursor_type cursor)
{
  atomic_flag_type& ref_blocked_cursor = m_blocked_cursors[get_cursor_index (cursor)];
#ifdef USE_STD_ATOMIC
  flag<cursor_type> blocked_cursor_value = ref_blocked_cursor.load ();
#else /* not USE_STD_ATOMIC */
  flag<cursor_type> blocked_cursor_value = ATOMIC_LOAD (&ref_blocked_cursor);
#endif /* not USE_STD_ATOMIC */

  assert (blocked_cursor_value.is_set (BLOCK_FLAG));
  cursor_type nextgen_cursor = blocked_cursor_value.clear (BLOCK_FLAG) + m_capacity;

#ifdef USE_STD_ATOMIC
  ref_blocked_cursor.store (nextgen_cursor);
#else /* not USE_STD_ATOMIC */
  ATOMIC_STORE (&ref_blocked_cursor, nextgen_cursor);
#endif /* not USE_STD_ATOMIC */
}

}  // namespace lockfree
