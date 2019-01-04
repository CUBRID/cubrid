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
 * collapsable_circular_queue.hpp
 */

#ifndef _COLLAPSABLE_CIRCULAR_QUEUE_HPP_
#define _COLLAPSABLE_CIRCULAR_QUEUE_HPP_

#ident "$Id$"

#include <assert.h>
#include <cstring>

/*
 * Collapsable circular queue : this is similar to circular queue, with some particularities:
 * - head value points to the first used element, tail points to the insert pointer
 * - queue is empty when tail == head
 * - elements of the queue may be flagged as free (consumed) from the middle of queue
 * - when consuming or freeing the head (consume pointer), will collapse all free elements
 * - consuming from middle or tail does not move tail/head pointers
 * - the capacity of queue is fixed
 * - the actual maximum size is (capacity - 1)
 */
namespace cubmem
{
  template <typename T>
  class collapsable_circular_queue
  {
    private:
      struct CCQ_SLOT
      {
	T value;
	char flags;
      };

      enum CCQ_SLOT_FLAG
      {
	CCQ_FREE = 0,
	CCQ_USED
      };

    public:
      collapsable_circular_queue (const size_t capacity)
      {
	init (capacity);
      };

      ~collapsable_circular_queue ()
      {
	clear ();
      };

      int size (void)
      {
	return (m_tail >= m_head) ? (m_tail - m_head) : (m_capacity + m_tail - m_head);
      };

      /* returns position in queue reserved */
      T *produce (T &elem)
      {
	int pos;

	if (size () >= m_capacity - 1)
	  {
	    /* is full */
	    return NULL;
	  }
	pos = m_tail;

	assert (m_buffer[pos].flags == CCQ_FREE);
	m_buffer[pos].flags = CCQ_USED;

	m_tail = (m_tail + 1) % m_capacity;

	assert (m_head != m_tail);

	m_buffer[pos].value = elem;

	return & (m_buffer[pos].value);
      };

      T *produce (void)
      {
	int pos;

	if (size () >= m_capacity - 1)
	  {
	    /* is full */
	    return NULL;
	  }
	pos = m_tail;

	assert (m_buffer[pos].flags == CCQ_FREE);
	m_buffer[pos].flags = CCQ_USED;

	m_tail = (m_tail + 1) % m_capacity;

	assert (m_head != m_tail);

	return & (m_buffer[pos].value);
      };

      T *peek_head (void)
      {
	if (m_head != m_tail)
	  {
	    return & (m_buffer[m_head].value);
	  }
	return NULL;
      };

      bool consume (T *elem, T *&last_used_elem)
      {
	CCQ_SLOT *slot = reinterpret_cast <CCQ_SLOT *> (elem);
	int pos = (int) (slot - m_buffer);

	return consume (pos, last_used_elem);
      };

      /* deletes the last element of queue (this needs to be produced-undo without release the mutex) */
      void undo_produce (T *elem)
      {
	CCQ_SLOT *slot = reinterpret_cast <CCQ_SLOT *> (elem);
	int pos = (int) (slot - m_buffer);

	int last_element = (m_tail == 0) ? (m_capacity - 1) : (m_tail - 1);

	assert (pos = last_element);

	assert (m_buffer[pos].flags == CCQ_USED);

	m_buffer[pos].flags = CCQ_FREE;
	m_tail = last_element;
      };

    protected:
      void init (const size_t capacity)
      {
	m_capacity = MAX (2, (int) capacity);
	m_buffer = new CCQ_SLOT[m_capacity];

	std::memset (m_buffer, 0, m_capacity * sizeof (CCQ_SLOT));

	m_head = 0;
	m_tail = 0;
      };

      void clear ()
      {
	delete [] m_buffer;
	m_capacity = 0;
      };

      /* returns count of collapsed positions and if last used element (or last before tail) */
      bool consume (const int pos, T *&last_used_elem)
      {
	if (m_buffer[pos].flags != CCQ_USED)
	  {
	    assert (false);
	    return false;
	  }

	m_buffer[pos].flags = CCQ_FREE;

	if (pos == m_head)
	  {
	    int collapsed_count = 0;
	    int prev_used_pos;

	    while (m_buffer[m_head].flags == CCQ_FREE)
	      {
		prev_used_pos = m_head;
		m_head = (m_head + 1) % m_capacity;
		collapsed_count++;

		if (m_head == m_tail)
		  {
		    /* queue becomes free */
		    break;
		  }
	      }

	    assert (m_head == m_tail || m_buffer[m_head].flags == CCQ_USED);

	    last_used_elem = & (m_buffer[prev_used_pos].value);

	    return true;
	  }

	/* do not collapse tail : is allowed only to advance */

	return false;
      };

    private:
      int m_head;
      int m_tail;

      int m_capacity;

      CCQ_SLOT *m_buffer;
  };

} /* namespace cubmem */

#endif /* _COLLAPSABLE_CIRCULAR_QUEUE_HPP_ */
