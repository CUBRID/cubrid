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
