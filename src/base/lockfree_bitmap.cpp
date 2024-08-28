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

#include "lockfree_bitmap.hpp"

#include "memory_alloc.h"

#include <cassert>
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace lockfree
{
  const float bitmap::FULL_USAGE_RATIO = 1.0f;
  const float bitmap::NINTETYFIVE_PERCENTILE_USAGE_RATIO = 0.95f;

  static void lf_bitmap_init (LF_BITMAP *bitmap, LF_BITMAP_STYLE style, int entries_cnt, float usage_threshold);
  static void lf_bitmap_destroy (LF_BITMAP *bitmap);
  static int lf_bitmap_get_entry (LF_BITMAP *bitmap);
  static void lf_bitmap_free_entry (LF_BITMAP *bitmap, int entry_idx);

  bitmap::bitmap ()
    : bitfield (NULL)
    , entry_count (0)
    , entry_count_in_use { 0 }
    , style (chunking_style::ONE_CHUNK)
    , usage_threshold (FULL_USAGE_RATIO)
    , start_idx { 0 }
  {
  }

  bitmap::~bitmap ()
  {
    destroy ();
  }

  void
  bitmap::init (chunking_style style_arg, int entries_count_arg, float usage_ratio_arg)
  {
    lf_bitmap_init (this, style_arg, entries_count_arg, usage_ratio_arg);
  }

  void
  bitmap::destroy ()
  {
    lf_bitmap_destroy (this);
  }

  int
  bitmap::get_entry ()
  {
    return lf_bitmap_get_entry (this);
  }

  void
  bitmap::free_entry (int entry_idx)
  {
    lf_bitmap_free_entry (this, entry_idx);
  }

  bool
  bitmap::is_full () const
  {
    return ((float) entry_count_in_use.load ()) >= usage_threshold * entry_count;
  }

  static void
  lf_bitmap_init (LF_BITMAP *bitmap, LF_BITMAP_STYLE style, int entries_cnt, float usage_threshold)
  {
    size_t chunk_count;
    unsigned int mask, chunk;
    int i;

    assert (bitmap != NULL);
    /* We only allow full usage for LF_BITMAP_ONE_CHUNK. */
    assert (style == LF_BITMAP_LIST_OF_CHUNKS || usage_threshold == 1.0f);

    bitmap->style = style;
    bitmap->entry_count = entries_cnt;
    bitmap->entry_count_in_use = 0;
    bitmap->usage_threshold = usage_threshold;
    if (usage_threshold < 0.0f || usage_threshold > 1.0f)
      {
	bitmap->usage_threshold = 1.0f;
      }
    bitmap->start_idx = 0;

    /* initialize bitfield */
    chunk_count = (size_t) CEIL_PTVDIV (entries_cnt, LF_BITFIELD_WORD_SIZE);
    bitmap->bitfield = new std::atomic<unsigned int>[chunk_count] ();
    for (size_t it = 0; it < chunk_count; it++)
      {
	bitmap->bitfield[it] = 0;
      }

    /* pad out the rest bits with 1, It will simplify the code in lf_bitmap_get_entry() */
    if (entries_cnt % LF_BITFIELD_WORD_SIZE != 0)
      {
	chunk = 0;
	mask = 1;
	for (i = entries_cnt % LF_BITFIELD_WORD_SIZE, mask <<= i; i < LF_BITFIELD_WORD_SIZE; i++, mask <<= 1)
	  {
	    chunk |= mask;
	  }
	bitmap->bitfield[chunk_count - 1] = chunk;
      }
  }

  static void
  lf_bitmap_destroy (LF_BITMAP *bitmap)
  {
    assert (bitmap != NULL);
    delete [] bitmap->bitfield;
    bitmap->bitfield = NULL;
    bitmap->entry_count = 0;
    bitmap->entry_count_in_use = 0;
    bitmap->style = LF_BITMAP_ONE_CHUNK;
    bitmap->usage_threshold = 1.0f;
    bitmap->start_idx = 0;
  }

  static int
  lf_bitmap_get_entry (LF_BITMAP *bitmap)
  {
    int chunk_count;
    unsigned int mask, chunk, start_idx;
    int i, chunk_idx, slot_idx;

    assert (bitmap != NULL);
    assert (bitmap->entry_count > 0);
    assert (bitmap->bitfield != NULL);

    chunk_count = CEIL_PTVDIV (bitmap->entry_count, LF_BITFIELD_WORD_SIZE);

restart:			/* wait-free process */
    chunk_idx = -1;
    slot_idx = -1;

    /* when reaches the predefined threshold */
    if (LF_BITMAP_IS_FULL (bitmap))
      {
	return -1;
      }

#if defined (SERVER_MODE)
    /* round-robin to get start chunk index */
    start_idx = bitmap->start_idx++;
    start_idx = start_idx % ((unsigned int) chunk_count);
#else
    /* iterate from the last allocated chunk */
    start_idx = bitmap->start_idx;
#endif

    /* find a chunk with an empty slot */
    i = start_idx;
    do
      {
	chunk = bitmap->bitfield[i].load ();
	if (~chunk)
	  {
	    chunk_idx = i;
	    break;
	  }

	i++;
	if (i >= chunk_count)
	  {
	    i = 0;
	  }
      }
    while (i != (int) start_idx);

    if (chunk_idx == -1)
      {
	/* full? */
	if (bitmap->style == LF_BITMAP_ONE_CHUNK)
	  {
	    assert (false);
	  }
	return -1;
      }

    /* find first empty slot in chunk */
    for (i = 0, mask = 1; i < LF_BITFIELD_WORD_SIZE; i++, mask <<= 1)
      {
	if ((~chunk) & mask)
	  {
	    slot_idx = i;
	    break;
	  }
      }

    if (slot_idx == -1)
      {
	/* chunk was filled in the meantime */
	goto restart;
      }

    assert ((chunk_idx * LF_BITFIELD_WORD_SIZE + slot_idx) < bitmap->entry_count);
    do
      {
	chunk = bitmap->bitfield[chunk_idx].load ();
	if (chunk & mask)
	  {
	    /* slot was marked by someone else */
	    goto restart;
	  }
      }
    while (!bitmap->bitfield[chunk_idx].compare_exchange_weak (chunk, chunk | mask));

    if (bitmap->style == LF_BITMAP_LIST_OF_CHUNKS)
      {
	bitmap->entry_count_in_use++;
      }

#if !defined (SERVER_MODE)
    bitmap->start_idx = chunk_idx;
#endif

    return chunk_idx * LF_BITFIELD_WORD_SIZE + slot_idx;
  }

  static void
  lf_bitmap_free_entry (LF_BITMAP *bitmap, int entry_idx)
  {
    unsigned int mask, inverse_mask, curr;
    int pos, bit;

    assert (bitmap != NULL);
    assert (entry_idx >= 0);
    assert (entry_idx < bitmap->entry_count);

    /* clear bitfield so slot may be reused */
    pos = entry_idx / LF_BITFIELD_WORD_SIZE;
    bit = entry_idx % LF_BITFIELD_WORD_SIZE;
    inverse_mask = (unsigned int) (1 << bit);
    mask = ~inverse_mask;

    do
      {
	/* clear slot */
	curr = bitmap->bitfield[pos].load ();

	if (! (curr & inverse_mask))
	  {
	    assert (false);
	    return;
	  }
      }
    while (!bitmap->bitfield[pos].compare_exchange_weak (curr, curr & mask));

    if (bitmap->style == LF_BITMAP_LIST_OF_CHUNKS)
      {
	bitmap->entry_count_in_use--;
      }

#if !defined (SERVER_MODE)
    bitmap->start_idx = pos;	/* hint for a free slot */
#endif
  }
} // namespace lockfree
