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


namespace mem
{
  /*
   * Implementation of a enhanced BI-Partite circular buffer : returns a contiguous memory for append
   * and allows reads in past appended (and committed) data.
   *
   * The buffer has an active region (m_ptr_start_a -> m_ptr_end_a) which servers the appends
   * The append pointer (m_ptr_append) is located in this range (m_ptr_start_a <= m_ptr_append <= m_ptr_end_a)
   * The m_ptr_end_a servers as a "reserve margin" limit : the range (append -> end_a) 
   * is the immediate "future" data (to be reserved). When the reserve margin cannot be guaranteed till end of buffer,
   * the B region is activated (normally is inactive, and when active it always resides the the beginning of the buffer).
   * 
   * The append consists for two steps:
   *  1. reserve (amount) : increments the append pointer with the desired amount
   *  2. commit (ptr) : increases either the lower limit of A region, or lower limit of previous "era" of region A
   *     (which is the range : m_ptr_prev_gen_committed -> m_ptr_prev_gen_last_reserved), depending on the value of ptr
   *
   *  Interlaced commit/reserve calls may be performed (concurrent calls are handled at upper layer).
   *  For proper functionallity of the buffer, it is assumed that commits are always performed in the same order
   *  as original reserve :  R1, R2, C1, R3, C2, C3;  (an invalid order would be: R1, R2, C2, C1). This should be
   *  handled by upper layer.
   *
   * The reads are performed in three steps:
   * 1. get_read_ranges : returns valid read ranges
   *  The reads cannot enter either of region A or region B (start_a -> end_a; buffer_start -> end_b) to prevent
   *  interfering with appenders. Also, it may read only data which was actually previously written 
   *  (cannot read past 'm_ptr_prev_gen_committed'). The call returns two ranges:
   *     i) trail_b + amount_b (the range after region B), in case region B is de-activated, this range extends from start
   *        buffer to start of region A
   *    ii) trail_a + amount_a (range after region A), this is the oldest data in the buffer and extends from end of A
   *        till m_ptr_prev_gen_committed.
   * 2. start_read (ptr, amount) : the buffer is split into equal sized pages (template parameter); 
   *    each page has a fixed count (m_read_fcnt) and a bit flag in m_read_flags.
   *    the start_read, increments the fix count of each affected page, and sets the correspoding bit
   * 3. end_read (ptr, amount) : the reserve of start_read (decrements the fix count,
   *    and clears the bit if count reaches zero)
   *
   * The reserve may fail for several reasons:
   *  a) amount is too big - static condition (amount > threshold)
   *     (this is subject to change, we should probably set by configuration/ratio of capacity)
   *  b) amount is too big - dynamic condition : the active region (difference between reserved and commited)
   *     is too large, and no contiguous range may be served
   *  c) the append pointer cannot be extended because readers are pending
   *  in all cases, NULL is returned; it is the responsibility of upper layer to handle this cases
   *  (preferably to prevent them)
   */

  /* TODO: template parameter is required by bitset; a solution would be to define a biset with a maximum size
   * and let the code use only as much as it needs */
  template <unsigned int P>
  class bip_buffer
  {
    public:
      bip_buffer ()
        {
          m_buffer = NULL;
          m_cycles = 0;
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

          /* TODO : adaptive reserve margin */
          /* TODO : a possible optimiztion would be to increase m_ptr_end_a with page increments,
           * instead of keeping it consistently ahead of append with a fixed amount */
          if (amount > m_reserve_margin)
            {
              assert (amount < m_capacity / 10);
              if (amount < m_capacity / 10)
                {
                  m_reserve_margin = amount;
                }
              else
                {
                  return NULL;
                }
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
          assert (ptr > m_buffer);

          if (ptr >= m_ptr_start_a && ptr < m_ptr_end_a)
            {
              assert (m_ptr_prev_gen_committed == m_ptr_prev_gen_last_reserved);
              m_ptr_start_a = ptr;
            }
          else
            {
              assert (m_ptr_prev_gen_committed != NULL);
              assert (m_ptr_prev_gen_last_reserved != NULL);

              assert (ptr >= m_ptr_prev_gen_committed && ptr < m_ptr_prev_gen_last_reserved);

              m_ptr_prev_gen_committed = ptr;
            }

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
        };
        
      const size_t get_page_size (void)
        {
          return m_read_page_size;
        };

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

          return (a2 > b1) && (b2 > a1);
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
          m_cycles++;
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

      std::uint64_t m_cycles;
  };

} /* namespace mem */

#endif /* _BIP_BUFFER_HPP_ */
