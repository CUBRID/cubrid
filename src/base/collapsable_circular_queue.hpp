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
#include <mutex>

/*
 * Collapsable circular queue : this is similar to circular queue, with some particularities:
 * - elements of the queue may be flagged as free (consumed) from the middle of queue
 * - when consuming or freeing the head (consume pointer), will collpase all free elements
 * - the capacity of queue is fixed
 * - the actual maximum size is (capacity - 1)
 */
namespace mem
{
  template <typename T>
  class collapsable_circular_queue
  {
    public:
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

      collapsable_circular_queue ()
        {
          m_capacity = 0;
        };

      ~collapsable_circular_queue ()
        {
          clear ();
        };

      void init (const size_t capacity)
        {
          m_capacity = capacity;
          m_buffer = new CCQ_SLOT[capacity];

          std::memset (m_buffer, 0, capacity * sizeof (CCQ_SLOT));

          m_head = 0;
          m_tail = 0;
        };

      void clear ()
        {
          delete[] m_buffer;
          m_capacity = 0;
        };
        
      int size (void)
        {
          return (m_tail >= m_head) ? (m_tail - m_head) : (m_capacity + m_tail - m_head);
        };

      /* returns position in queue reserved */
      T* produce (T &elem)
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
  
          m_buffer[pos].value = elem;

          return &(m_buffer[pos].value);
        };

      T* peek_head (void)
        {
          if (m_head != m_tail)
            {
              return &(m_buffer[m_head].value);
            }
          return NULL; 
        };

      int consume (T *elem, T* &new_head)
        {
          CCQ_SLOT *slot = reinterpret_cast <CCQ_SLOT *> (elem);
          int pos = slot - m_buffer;

          return consume (pos, new_head);
        };

    protected:
      /* returns count of collapsed positions */
      int consume (const int pos, T* &new_head)
      {
        if (m_buffer[pos].flags != CCQ_USED)
          {
            assert (false);
            return -1;
          }

        m_buffer[pos].flags = CCQ_FREE;

        if (pos == m_head)
          {
            while (m_buffer[m_head].flags == CCQ_FREE)
              {
                m_head = (m_head + 1) % m_capacity;

                if (m_head == m_tail)
                  {
                    /* queue becomes free */
                    break;
                  }
              }
            if (m_head != m_tail)
              {
                new_head = &(m_buffer[m_head].value);
              }

            return (m_head >= pos) ? (m_head - pos) : (m_capacity + m_head - pos);
          }

        if (pos == m_tail)
          {
            while (m_buffer[m_tail].flags == CCQ_FREE)
              {
                m_tail = (m_tail - 1) % m_capacity;

                if (m_head == m_tail)
                  {
                    /* queue becomes free */
                    break;
                  }
              }

            /* do not return colapsed count here */
          }

        return 0;
      };
    private:
      int m_head;
      int m_tail;

      int m_capacity;

      CCQ_SLOT *m_buffer;
    };

} /* namespace mem */

#endif /* _COLLAPSABLE_CIRCULAR_QUEUE_HPP_ */
