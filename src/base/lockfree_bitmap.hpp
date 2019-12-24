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
 * lock-free bitmap
 */

#ifndef _LOCKFREE_BITMAP_HPP_
#define _LOCKFREE_BITMAP_HPP_

#include <atomic>

namespace lockfree
{
  // todo - refactoring
  class bitmap
  {
    public:
      static const float FULL_USAGE_RATIO;
      static const float NINTETYFIVE_PERCENTILE_USAGE_RATIO;

      enum chunking_style
      {
	ONE_CHUNK = 0,
	LIST_OF_CHUNKS
      };

      bitmap ();
      ~bitmap ();

      void init (chunking_style style, int entries_count, float usage_ratio);
      void destroy ();

      int get_entry ();
      void free_entry (int entry_idx);

      bool is_full () const;

      // todo: make private fields
      /* bitfield for entries array */
      std::atomic<unsigned int> *bitfield;

      /* capacity count */
      int entry_count;

      /* current used count */
      std::atomic<int> entry_count_in_use;

      /* style */
      chunking_style style;

      /* threshold for usage */
      float usage_threshold;

      /* the start chunk index for round-robin */
      std::atomic<unsigned int> start_idx;
  };
} // namespace lockfree

using LF_BITMAP = lockfree::bitmap;

using LF_BITMAP_STYLE = lockfree::bitmap::chunking_style;
static const LF_BITMAP_STYLE LF_BITMAP_ONE_CHUNK = LF_BITMAP_STYLE::ONE_CHUNK;
static const LF_BITMAP_STYLE LF_BITMAP_LIST_OF_CHUNKS = LF_BITMAP_STYLE::LIST_OF_CHUNKS;

// todo - replace macros
#define LF_BITMAP_FULL_USAGE_RATIO lockfree::bitmap::FULL_USAGE_RATIO
#define LF_BITMAP_95PERCENTILE_USAGE_RATIO lockfree::bitmap::NINTETYFIVE_PERCENTILE_USAGE_RATIO

#define LF_BITFIELD_WORD_SIZE    (int) (sizeof (unsigned int) * 8)

#define LF_BITMAP_IS_FULL(bitmap) (bitmap)->is_full ()

#define LF_BITMAP_COUNT_ALIGN(count) \
    (((count) + (LF_BITFIELD_WORD_SIZE) - 1) & ~((LF_BITFIELD_WORD_SIZE) - 1))

#endif // !_LOCKFREE_BITMAP_HPP_
