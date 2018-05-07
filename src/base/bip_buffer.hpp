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
 * bip_buffer.hpp
 */

#ifndef _BIP_BUFFER_HPP_
#define _BIP_BUFFER_HPP_

#ident "$Id$"

#include "collapsable_circular_queue.hpp"
#include "memory_alloc.h"
#include <algorithm>
#include <bitset>
#include <assert.h>


/*
 * Implementation BI-Partite circular buffer : returns a contiguous memory
 */
namespace mem
{
  template <unsigned int P>
  class bip_buffer
  {
    public:
      bip_buffer ()
        {
          m_buffer = NULL;
        };

      ~bip_buffer ()
        {
          delete [] m_buffer;
        };

      void init (const size_t capacity)
        {
          m_capacity = DB_ALIGN (capacity, 4 * 1024);
          m_capacity = MIN (m_capacity, 100 * 1024 * 1024);
          m_read_page_size = m_capacity / P;
          m_capacity = DB_ALIGN (m_capacity, m_read_page_size);

          m_read_page_size = m_capacity / P;

          m_reserve_margin = m_capacity / 10;
          m_reserve_margin = DB_ALIGN (m_reserve_margin, MAX_ALIGNMENT);
          m_reserve_margin = MAX (m_reserve_margin, 10 * 1024);
          m_reserve_margin = MIN (m_reserve_margin, 10 * 1024 * 1024);

          assert (m_reserve_margin < m_capacity);

          m_buffer = new char[m_capacity];
          m_buffer_end = m_buffer + m_capacity;

          m_ptr_start_a = m_buffer;
          m_ptr_append = m_ptr_start_a;
          m_ptr_end_a = m_ptr_append + m_reserve_margin;

          m_ptr_end_b = NULL;

          m_ptr_prev_gen_committed = NULL;
          m_ptr_prev_gen_last_reserved = NULL;

          memset (m_read_fcnt, 0, P * sizeof (m_read_fcnt[0]));
          m_read_flags.reset ();
        };

      size_t get_capacity ()
        {
          return m_capacity;
        };

      void set_reserve_margin (const size_t margin)
        {
          m_reserve_margin = margin;
        };
 
      const char * reserve (const size_t amount)
        {
          const char *reserved_ptr = NULL;
          bool need_take_margin = true;

          if (amount > m_reserve_margin)
            {
              assert (false);
              return NULL;
            }

          while (1)
            {
              if (m_ptr_append + amount > m_buffer_end)
                {
                  /* not enough till end of whole buffer; try switch to region B */
                  if (is_region_b_used () == false)
                    {
                      assert (false);

                      activate_region_b ();
                    }
                  else
                    {
                      switch_to_region_b ();
                    }
                  /* next loop */
               }
              else if (m_ptr_append + amount >= m_ptr_end_a)
                {
                  /* enough till end of buffer, but need to extend region A */
                  take_margin_in_region_a (amount);
                  need_take_margin = false;
                  /* next loop */
                }
              else  /* enough in region A */
                {
                  assert (m_ptr_append + amount < m_ptr_end_a);

                  /* check that no readers are pending inside new region A */
                  if (check_readers (m_ptr_append, m_ptr_append + amount) == false)
                    {
                      /* would block */
                      return NULL;
                    }
                  reserved_ptr = m_ptr_append;
                  m_ptr_append += amount;

                  if (need_take_margin)
                    {
                      take_margin_in_region_a (0);
                    }

                  return reserved_ptr;
                }
            }

          return reserved_ptr;
        };

      int commit (const char *ptr)
        {
          assert (ptr <= m_ptr_append);
          assert (ptr > m_buffer);

          m_ptr_start_a = ptr;

          return NO_ERROR;
        };

      int start_read (const char *ptr, const size_t amount)
        {
          int start_page_idx, end_page_idx;
          int idx;

          if (is_region_b_used ())
            {
              if (is_range_overlap (ptr, amount, m_buffer, m_ptr_end_b - m_buffer))
                {
                  return ER_FAILED;
                }
            }

          if (is_range_overlap (ptr, amount, m_ptr_start_a, m_ptr_end_a - m_ptr_start_a))
            {
              return ER_FAILED;
            }

          start_page_idx = get_page_from_ptr (ptr);
          end_page_idx =  get_page_from_ptr (ptr + amount);

          for (idx = start_page_idx; idx <= end_page_idx; idx++)
            {
              m_read_fcnt[idx]++;
              m_read_flags.set (idx);
            }
          return NO_ERROR;
        };

      int end_read (const char *ptr, const size_t amount)
        {
          int start_page_idx, end_page_idx;
          int idx;

          start_page_idx = get_page_from_ptr (ptr);
          end_page_idx =  get_page_from_ptr (ptr + amount);

          for (idx = start_page_idx; idx <= end_page_idx; idx++)
            {
              assert (m_read_fcnt[idx] > 0);
              m_read_fcnt[idx]--;
              if (m_read_fcnt[idx] == 0)
                {
                  m_read_flags.reset (idx);
                }
            }

          return NO_ERROR;
        };

      void get_read_ranges (const char* &trail_b, size_t &amount_trail_b, const char* &trail_a, size_t &amount_trail_a)
        {
          if (is_region_b_used ())
            {
              trail_b = m_ptr_end_b;
              amount_trail_b = m_ptr_start_a - m_ptr_end_b;
            }
          else
            {
              amount_trail_b = m_ptr_start_a - m_buffer;
              trail_b = m_buffer;
            }

          if (m_ptr_prev_gen_committed != NULL)
            {
              trail_a = m_ptr_end_a;
              amount_trail_a = m_ptr_prev_gen_committed - m_ptr_end_a;
            }
          else
            {
              amount_trail_a = 0;
            }
        }
        
     protected:
      int get_page_from_ptr (const char *ptr)
        {
          if (ptr >= m_buffer_end)
            {
              return -1;
            }

          return (ptr - m_buffer) / m_read_page_size;
        }

      bool is_range_overlap (const char *ptr1, const size_t size1, const char *ptr2, const size_t size2)
        {
          const char *a1 = ptr1;
          const char *a2 = ptr1 + size1;
          const char *b1 = ptr2;
          const char *b2 = ptr2 + size2;

          return (a2 >= b1) && (a1 >= b2);
        };

      bool check_readers (const char *start_ptr, const char *end_ptr)
        {
          int start_page_idx = get_page_from_ptr (start_ptr);
          int end_page_idx = get_page_from_ptr (end_ptr - 1);
          int idx;

          assert (start_ptr < end_ptr -1);
          assert (start_ptr >= m_buffer);
          assert (end_ptr < m_buffer_end);

          if (m_read_flags.any ())
            {
              for (idx = start_page_idx; idx <= end_page_idx; idx++)
                {
                  if (m_read_flags.test (idx))
                    {
                      return false;
                    }
                }
            }

          return true;
        };

      bool is_region_b_used (void)
        {
          return m_ptr_end_b != NULL;
        };

      void activate_region_b ()
        {
          m_ptr_end_b = m_buffer + m_reserve_margin;
        };

      void deactivate_region_b ()
        {
          m_ptr_end_b = NULL;
        };

      void switch_to_region_b ()
        {
          m_ptr_prev_gen_committed = m_ptr_start_a;
          m_ptr_prev_gen_last_reserved = m_ptr_append;

          m_ptr_start_a = m_buffer;
          m_ptr_end_a = m_ptr_end_b;

          m_ptr_append = m_ptr_start_a;

          deactivate_region_b ();
        };

      void take_margin_in_region_a (const size_t amount)
        {
          if (m_ptr_append + amount + m_reserve_margin < m_buffer_end)
            {
              m_ptr_end_a = m_ptr_append + amount + m_reserve_margin;
             }
          else
            {
              m_ptr_end_a = m_buffer_end;
              if (is_region_b_used () == false)
                {
                  activate_region_b ();
                }
            }
        };
    private:
      size_t m_capacity;

      const char *m_buffer;
      const char *m_buffer_end;

      /* limits of region A : end_a points to first byte after the last one of region */
      const char *m_ptr_start_a;
      const char *m_ptr_end_a;
      const char *m_ptr_append;

      /* pointer to last committed data in previous generation of Region A 
       * should be updated only by external user
       * should finally catchup with m_ptr_prev_gen_last_reserved
       * it is used by readers to check the upper bound of available data
       */
      const char *m_ptr_prev_gen_committed;
      /* pointer to last reserved position in previous generation of Region A
       * is updated when current region A switches to backup region B
       */
      const char *m_ptr_prev_gen_last_reserved;

      /* region B is activated as stand-by when end of region A is close to end of buffer
       * region B always starts from offset zero of buffer
       */
      const char *m_ptr_end_b;


      /* for reading, we split the entire buffer in equal fixed sized pages:
       * - each page has read fix count (the counter is incremented each time a read starts,
       *   is decremented when a read ends)
       * - a page read cannot be validated if at has at least one part of the page covered by any of 
       *   the "write" regions (A or B)
       * - the append pointer of A region cannot advance into a page having non-zero fix count (write is blocked)
       * - the "spare" interval (an amount of space after the current append pointer) can be advanced 
       *   into a "read" page
       */
      /* read fix count for each page */
      int m_read_fcnt[P];
      std::bitset<P> m_read_flags;

      size_t m_read_page_size;

      size_t m_reserve_margin;

      int m_read_pages;
  };

} /* namespace mem */

#endif /* _BIP_BUFFER_HPP_ */
