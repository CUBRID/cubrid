/*
 *
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

//
// bestspace_map.cpp - free space tier of every page of a heap file
//

#include "bestspace_map.hpp"

#include "bit.h"
#include "memory_alloc.h"

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <type_traits>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace cubstorage
{
  namespace
  {
    constexpr std::uint64_t ALL_NOT_DATA = 0xEEEEEEEEEEEEEEEEULL;
    constexpr std::uint64_t ALL_UNVERIFIED = 0xFFFFFFFFFFFFFFFFULL;

#if !defined (NDEBUG)
    std::atomic<int> bsmap_failing_allocations (0);
#endif

    // a value-initialized object, or nullptr when out of memory: the engine does not use exceptions, and in
    // SERVER_MODE memory_wrapper.hpp turns 'new' into a macro that does not take std::nothrow
    template <typename T>
    T *
    bsmap_alloc ()
    {
#if !defined (NDEBUG)
      if (bsmap_failing_allocations.load () > 0 && bsmap_failing_allocations.fetch_sub (1) > 0)
	{
	  return nullptr;
	}
#endif
      void *mem = malloc (sizeof (T));
      if (mem == nullptr)
	{
	  return nullptr;
	}
      return placement_new (static_cast<T *> (mem));
    }

    template <typename T>
    void
    bsmap_free (T *ptr)
    {
      if (ptr != nullptr)
	{
	  ptr->~T ();
	  free_and_init (ptr);
	}
    }

    inline std::size_t
    lowest_bit (std::uint64_t v)
    {
      return static_cast<std::size_t> (bit64_count_trailing_zeros (v));
    }

    // true if some 4-bit code of the word is CODE_UNVERIFIED (all four bits set)
    inline bool
    has_unverified_nibble (std::uint64_t w)
    {
      return (w & (w >> 1) & (w >> 2) & (w >> 3) & 0x1111111111111111ULL) != 0;
    }
  }

  //////////////////////////////////////////////////////////////////////////
  // bestspace_map
  //////////////////////////////////////////////////////////////////////////

  bestspace_map::bestspace_map ()
    : m_inline_persist_dirty (0)
    , m_root (nullptr)
    , m_radix (nullptr)
    , m_num_sectors (0)
    , m_num_unverified (0)
    , m_needs_retry (false)
    , m_grow_mutex ()
    , m_bytes (sizeof (bestspace_map))
    , m_reserved_for (std::numeric_limits<std::size_t>::max ())
  {
    std::size_t i;

    m_spare.mid = nullptr;
    m_spare.leaf = nullptr;
    m_spare.grp = nullptr;
    m_spare.num_rnodes = 0;
    for (i = 0; i < MAX_SPARE_RNODES; i++)
      {
	m_spare.rnodes[i] = nullptr;
      }

    for (i = 0; i < INLINE_SECTORS; i++)
      {
	init_record (m_inline[i], ALL_NOT_DATA);
	m_inline_keys[i] = 0;
      }
    for (i = 0; i < CNT_LAST; i++)
      {
	m_counters[i].store (0);
      }
  }

  bestspace_map::~bestspace_map ()
  {
    free_tree (m_root.load ());
    free_radix (m_radix.load (), 0);
    bsmap_free (m_spare.mid);
    bsmap_free (m_spare.leaf);
    bsmap_free (m_spare.grp);
    for (std::size_t i = 0; i < m_spare.num_rnodes; i++)
      {
	bsmap_free (m_spare.rnodes[i]);
      }
  }

  bestspace_map *
  bestspace_map::create ()
  {
    return bsmap_alloc<bestspace_map> ();
  }

  void
  bestspace_map::destroy (bestspace_map *&map)
  {
    bsmap_free (map);
    map = nullptr;
  }

#if !defined (NDEBUG)
  void
  bestspace_map::debug_fail_allocations (int count)
  {
    bsmap_failing_allocations.store (count);
  }
#endif

  // ------------------------------------------------------------------ keys and codes

  std::uint64_t
  bestspace_map::make_key (short volid, std::int32_t pageid)
  {
    std::uint64_t sectid = static_cast<std::uint64_t> (static_cast<std::uint32_t> (pageid)) / PAGES_PER_SECTOR;
    return ((static_cast<std::uint64_t> (static_cast<std::uint16_t> (volid)) + 1) << 32) | sectid;
  }

  short
  bestspace_map::key_volid (std::uint64_t key)
  {
    return static_cast<short> ((key >> 32) - 1);
  }

  std::int32_t
  bestspace_map::key_first_pageid (std::uint64_t key)
  {
    return static_cast<std::int32_t> ((key & 0xFFFFFFFFULL) * PAGES_PER_SECTOR);
  }

  std::uint8_t
  bestspace_map::entry_code (const bestspace_map_entry &entry, std::size_t offset)
  {
    return static_cast<std::uint8_t> ((entry.codes[offset / 16] >> ((offset % 16) * 4)) & 0xF);
  }

  void
  bestspace_map::entry_set_code (bestspace_map_entry &entry, std::size_t offset, std::uint8_t code)
  {
    std::size_t shift = (offset % 16) * 4;
    entry.codes[offset / 16] =
	    (entry.codes[offset / 16] & ~ (0xFULL << shift)) | (static_cast<std::uint64_t> (code) << shift);
  }

  // radix key: volid (15 bits) above sectid (25 bits) = 40 bits, consumed 6 bits per level from the top
  std::uint64_t
  bestspace_map::radix_key (std::uint64_t key)
  {
    std::uint64_t volid = (key >> 32) - 1;
    std::uint64_t sectid = key & 0x1FFFFFFULL;
    return (volid << 25) | sectid;
  }

  std::size_t
  bestspace_map::radix_slot (std::uint64_t rk, int level)
  {
    int shift = 6 * (RADIX_LEVELS - 1 - level);
    return static_cast<std::size_t> ((rk >> shift) & (FANOUT - 1));
  }

  std::uint8_t
  bestspace_map::get_code (record *rec, std::size_t off)
  {
    return static_cast<std::uint8_t> ((rec->codes[off / 16].load () >> ((off % 16) * 4)) & 0xF);
  }

  void
  bestspace_map::init_record (record &rec, std::uint64_t fill)
  {
    for (int i = 0; i < 4; i++)
      {
	rec.codes[i].store (fill, std::memory_order_relaxed);
      }
    rec.resident.store (0, std::memory_order_relaxed);
    rec.dirty.store (0, std::memory_order_relaxed);
    rec.version.store (0, std::memory_order_relaxed);
  }

  // ------------------------------------------------------------------ structure

  bestspace_map::group *
  bestspace_map::group_ptr (std::size_t g)
  {
    rootdir *root = m_root.load (std::memory_order_acquire);
    if (root == nullptr)
      {
	return nullptr;
      }
    middir *mid = root->children[g / (FANOUT * FANOUT)].load (std::memory_order_acquire);
    if (mid == nullptr)
      {
	return nullptr;
      }
    leafdir *leaf = mid->children[ (g / FANOUT) % FANOUT].load (std::memory_order_acquire);
    if (leaf == nullptr)
      {
	return nullptr;
      }
    return leaf->children[g % FANOUT].load (std::memory_order_acquire);
  }

  bestspace_map::group *
  bestspace_map::group_at (std::size_t g)
  {
    group *grp = group_ptr (g);
    assert (grp != nullptr);
    return grp;
  }

  bestspace_map::record *
  bestspace_map::sector_at (std::size_t n)
  {
    return n < INLINE_SECTORS ? &m_inline[n] : &group_at (n / FANOUT)->sectors[n % FANOUT];
  }

  std::uint64_t
  bestspace_map::key_at (std::size_t n)
  {
    return n < INLINE_SECTORS ? m_inline_keys[n] : group_at (n / FANOUT)->keys[n % FANOUT];
  }

  std::atomic<std::uint64_t> &
  bestspace_map::persist_word (std::size_t n)
  {
    return n < INLINE_SECTORS ? m_inline_persist_dirty : group_at (n / FANOUT)->persist_dirty;
  }

  bestspace_map::record *
  bestspace_map::find (short volid, std::int32_t pageid, std::size_t &n)
  {
    std::uint64_t key = make_key (volid, pageid);
    rnode *r = m_radix.load (std::memory_order_acquire);

    if (r == nullptr)
      {
	// inline: the keys are published before m_num_sectors
	std::size_t count = m_num_sectors.load (std::memory_order_acquire), s;
	for (s = 0; s < count && s < INLINE_SECTORS; s++)
	  {
	    if (m_inline_keys[s] == key)
	      {
		n = s;
		return &m_inline[s];
	      }
	  }
	return nullptr;
      }

    std::uint64_t rk = radix_key (key);
    for (int level = 0; r != nullptr && level < RADIX_LEVELS - 1; level++)
      {
	r = static_cast<rnode *> (r->slots[radix_slot (rk, level)].load (std::memory_order_acquire));
      }
    if (r == nullptr)
      {
	return nullptr;
      }
    std::uintptr_t v =
	    reinterpret_cast<std::uintptr_t> (r->slots[radix_slot (rk, RADIX_LEVELS - 1)].load (std::memory_order_acquire));
    if (v == 0)
      {
	return nullptr;
      }
    n = static_cast<std::size_t> (v - 1);
    return sector_at (n);
  }

  bestspace_map::record *
  bestspace_map::get_or_add (short volid, std::int32_t pageid, std::size_t &n, std::uint64_t fill, bool use_spares)
  {
    record *rec = find (volid, pageid, n);
    if (rec != nullptr)
      {
	return rec;
      }

    std::lock_guard<std::mutex> lock (m_grow_mutex);

    rec = find (volid, pageid, n);
    if (rec != nullptr)
      {
	return rec;
      }

    std::size_t s = m_num_sectors.load (std::memory_order_relaxed);
    std::uint64_t key = make_key (volid, pageid);
    if (use_spares)
      {
	// this add may use up the reserve even if it fails: the next reserve () checks it again
	m_reserved_for.store (std::numeric_limits<std::size_t>::max (), std::memory_order_relaxed);
      }
    if (s >= MAX_SECTORS)
      {
	m_needs_retry.store (true);
	return nullptr;
      }

    if (s < INLINE_SECTORS)
      {
	rec = &m_inline[s];
	init_record (*rec, fill);
	m_inline_keys[s] = key;
      }
    else
      {
	if (m_root.load (std::memory_order_relaxed) == nullptr && !promote (use_spares))
	  {
	    m_needs_retry.store (true);
	    return nullptr;
	  }
	std::size_t g = s / FANOUT;
	if (group_ptr (g) == nullptr && !add_group (m_root.load (std::memory_order_relaxed), g, use_spares))
	  {
	    m_needs_retry.store (true);
	    return nullptr;
	  }
	group *grp = group_at (g);
	rec = &grp->sectors[s % FANOUT];
	init_record (*rec, fill);
	grp->keys[s % FANOUT] = key;
	if (!radix_insert (m_radix, radix_key (key), s, use_spares))
	  {
	    m_needs_retry.store (true);
	    return nullptr;
	  }
      }

    if (fill == ALL_UNVERIFIED)
      {
	m_num_unverified.fetch_add (PAGES_PER_SECTOR);
      }
    m_num_sectors.store (s + 1, std::memory_order_release);
    n = s;
    return rec;
  }

  // caller holds m_grow_mutex: the inline sectors are in use and another sector is coming. The inline records stay
  // where they are; the tree and the radix index are built aside and published, then the inline pages are published
  // again so the tree gets their hint bits.
  bool
  bestspace_map::promote (bool use_spares)
  {
    std::atomic<rnode *> radix (nullptr);
    // reserve () keeps nothing for the promotion (see the class comment): its nodes are allocated here
    rootdir *no_spare = nullptr;
    rootdir *root = alloc_node (no_spare, use_spares);
    std::size_t s, off;

    if (root == nullptr)
      {
	return false;
      }
    if (!add_group (root, 0, use_spares))
      {
	// m_bytes is approximate after an allocation failure
	free_tree (root);
	return false;
      }
    for (s = 0; s < INLINE_SECTORS; s++)
      {
	if (!radix_insert (radix, radix_key (m_inline_keys[s]), s, use_spares))
	  {
	    free_radix (radix.load (), 0);
	    free_tree (root);
	    return false;
	  }
      }

    m_radix.store (radix.load ());
    m_root.store (root);
    for (s = 0; s < INLINE_SECTORS; s++)
      {
	for (off = 0; off < PAGES_PER_SECTOR; off++)
	  {
	    publish (&m_inline[s], s, off, false);
	  }
      }
    return true;
  }

  // caller holds m_grow_mutex
  bool
  bestspace_map::add_group (rootdir *root, std::size_t g, bool use_spares)
  {
    std::size_t a = g / (FANOUT * FANOUT), b = (g / FANOUT) % FANOUT, c = g % FANOUT;
    middir *mid = root->children[a].load (std::memory_order_relaxed);
    if (mid == nullptr)
      {
	mid = alloc_node (m_spare.mid, use_spares);
	if (mid == nullptr)
	  {
	    return false;
	  }
	root->children[a].store (mid, std::memory_order_release);
      }
    leafdir *leaf = mid->children[b].load (std::memory_order_relaxed);
    if (leaf == nullptr)
      {
	leaf = alloc_node (m_spare.leaf, use_spares);
	if (leaf == nullptr)
	  {
	    return false;
	  }
	mid->children[b].store (leaf, std::memory_order_release);
      }
    group *grp = alloc_node (m_spare.grp, use_spares);
    if (grp == nullptr)
      {
	return false;
      }
    leaf->children[c].store (grp, std::memory_order_release);
    return true;
  }

  // caller holds m_grow_mutex
  bool
  bestspace_map::radix_insert (std::atomic<rnode *> &root, std::uint64_t rk, std::size_t number, bool use_spares)
  {
    rnode *r = root.load (std::memory_order_relaxed);
    if (r == nullptr)
      {
	r = alloc_rnode (use_spares);
	if (r == nullptr)
	  {
	    return false;
	  }
	root.store (r, std::memory_order_release);
      }
    for (int level = 0; level < RADIX_LEVELS - 1; level++)
      {
	std::size_t slot = radix_slot (rk, level);
	rnode *next = static_cast<rnode *> (r->slots[slot].load (std::memory_order_relaxed));
	if (next == nullptr)
	  {
	    next = alloc_rnode (use_spares);
	    if (next == nullptr)
	      {
		return false;
	      }
	    r->slots[slot].store (next, std::memory_order_release);
	  }
	r = next;
      }
    r->slots[radix_slot (rk, RADIX_LEVELS - 1)].store (reinterpret_cast<void *> (static_cast<std::uintptr_t> (number + 1)),
	std::memory_order_release);
    return true;
  }

  // caller holds m_grow_mutex. A node from the reserve was counted in m_bytes when reserve () allocated it
  template <typename T>
  T *
  bestspace_map::alloc_node (T *&spare, bool use_spares)
  {
    T *node;

    if (use_spares && spare != nullptr)
      {
	node = spare;
	spare = nullptr;
	return node;
      }
    node = bsmap_alloc<T> ();
    if (node != nullptr)
      {
	m_bytes += sizeof (T);
	if (use_spares)
	  {
	    count (CNT_NODE_FALLBACK);
	  }
      }
    return node;
  }

  // caller holds m_grow_mutex
  bestspace_map::rnode *
  bestspace_map::alloc_rnode (bool use_spares)
  {
    rnode *node;

    if (use_spares && m_spare.num_rnodes > 0)
      {
	m_spare.num_rnodes--;
	node = m_spare.rnodes[m_spare.num_rnodes];
	m_spare.rnodes[m_spare.num_rnodes] = nullptr;
	return node;
      }
    node = bsmap_alloc<rnode> ();
    if (node != nullptr)
      {
	m_bytes += sizeof (rnode);
	if (use_spares)
	  {
	    count (CNT_NODE_FALLBACK);
	  }
      }
    return node;
  }

  // caller holds m_grow_mutex: the nodes adding sector number s needs, if its key is the one after the last sector's
  // (the file manager hands a growing file the next sectors of a volume). The group and its directories depend only
  // on s; the radix nodes depend on the key, so one spare radix node is kept anyway for a key in the next block.
  // Nothing is counted for an inline record, for the promotion (see the class comment) or past MAX_SECTORS
  void
  bestspace_map::count_needs (std::size_t s, node_counts &need)
  {
    need.mid = need.leaf = need.grp = need.rnodes = 0;
    rootdir *root = m_root.load (std::memory_order_relaxed);
    if (s < INLINE_SECTORS || root == nullptr || s >= MAX_SECTORS)
      {
	return;
      }

    std::size_t g = s / FANOUT;
    middir *mid = root->children[g / (FANOUT * FANOUT)].load (std::memory_order_relaxed);
    leafdir *leaf = mid != nullptr ? mid->children[ (g / FANOUT) % FANOUT].load (std::memory_order_relaxed) : nullptr;
    need.mid = mid == nullptr ? 1 : 0;
    need.leaf = leaf == nullptr ? 1 : 0;
    need.grp = (leaf == nullptr || leaf->children[g % FANOUT].load (std::memory_order_relaxed) == nullptr) ? 1 : 0;

    // the radix path of the next key: the nodes below the first missing slot are missing too
    std::uint64_t rk = radix_key (key_at (s - 1) + 1);
    rnode *r = m_radix.load (std::memory_order_relaxed);
    int level;
    for (level = 0; r != nullptr && level < RADIX_LEVELS - 1; level++)
      {
	r = static_cast<rnode *> (r->slots[radix_slot (rk, level)].load (std::memory_order_relaxed));
      }
    need.rnodes = r == nullptr ? static_cast<std::size_t> (RADIX_LEVELS - level) : 0;
    if (need.rnodes == 0)
      {
	need.rnodes = 1;
      }
    if (need.rnodes > MAX_SPARE_RNODES)
      {
	need.rnodes = MAX_SPARE_RNODES;
      }
  }

  void
  bestspace_map::reserve ()
  {
    node_counts need, got;
    middir *mid = nullptr;
    leafdir *leaf = nullptr;
    group *grp = nullptr;
    rnode *rnodes[MAX_SPARE_RNODES];
    std::size_t s, i, num_rnodes = 0, target_rnodes;
    bool complete;

    s = m_num_sectors.load (std::memory_order_acquire);
    if (m_reserved_for.load (std::memory_order_acquire) == s)
      {
	return;
      }

    // what the next sector add needs and the reserve does not hold yet
    {
      std::lock_guard<std::mutex> lock (m_grow_mutex);

      s = m_num_sectors.load (std::memory_order_relaxed);
      count_needs (s, need);
      target_rnodes = need.rnodes;
      need.mid = (need.mid > 0 && m_spare.mid == nullptr) ? 1 : 0;
      need.leaf = (need.leaf > 0 && m_spare.leaf == nullptr) ? 1 : 0;
      need.grp = (need.grp > 0 && m_spare.grp == nullptr) ? 1 : 0;
      need.rnodes = need.rnodes > m_spare.num_rnodes ? need.rnodes - m_spare.num_rnodes : 0;
    }

    // allocate without the mutex: set_resident () may wait for it under the heap header latch
    complete = true;
    if (need.mid > 0 && (mid = bsmap_alloc<middir> ()) == nullptr)
      {
	complete = false;
      }
    if (need.leaf > 0 && (leaf = bsmap_alloc<leafdir> ()) == nullptr)
      {
	complete = false;
      }
    if (need.grp > 0 && (grp = bsmap_alloc<group> ()) == nullptr)
      {
	complete = false;
      }
    for (i = 0; i < need.rnodes; i++)
      {
	rnodes[num_rnodes] = bsmap_alloc<rnode> ();
	if (rnodes[num_rnodes] == nullptr)
	  {
	    complete = false;
	    break;
	  }
	num_rnodes++;
      }

    // keep what the count above asked for; a concurrent reserve () may have filled the same slots meanwhile
    {
      std::lock_guard<std::mutex> lock (m_grow_mutex);

      got.mid = got.leaf = got.grp = got.rnodes = 0;
      if (mid != nullptr && m_spare.mid == nullptr)
	{
	  m_spare.mid = mid;
	  mid = nullptr;
	  got.mid = 1;
	}
      if (leaf != nullptr && m_spare.leaf == nullptr)
	{
	  m_spare.leaf = leaf;
	  leaf = nullptr;
	  got.leaf = 1;
	}
      if (grp != nullptr && m_spare.grp == nullptr)
	{
	  m_spare.grp = grp;
	  grp = nullptr;
	  got.grp = 1;
	}
      while (num_rnodes > 0 && m_spare.num_rnodes < target_rnodes)
	{
	  num_rnodes--;
	  m_spare.rnodes[m_spare.num_rnodes++] = rnodes[num_rnodes];
	  rnodes[num_rnodes] = nullptr;
	  got.rnodes++;
	}
      m_bytes += got.mid * sizeof (middir) + got.leaf * sizeof (leafdir) + got.grp * sizeof (group)
		 + got.rnodes * sizeof (rnode);
      count (CNT_NODE_RESERVED, got.mid + got.leaf + got.grp + got.rnodes);
      if (complete && m_num_sectors.load (std::memory_order_relaxed) == s)
	{
	  m_reserved_for.store (s, std::memory_order_release);
	}
    }

    // nodes a concurrent reserve () made unnecessary
    bsmap_free (mid);
    bsmap_free (leaf);
    bsmap_free (grp);
    for (i = 0; i < num_rnodes; i++)
      {
	bsmap_free (rnodes[i]);
      }
  }

  void
  bestspace_map::free_radix (rnode *r, int level)
  {
    if (r == nullptr)
      {
	return;
      }
    if (level < RADIX_LEVELS - 1)
      {
	for (std::size_t i = 0; i < FANOUT; i++)
	  {
	    free_radix (static_cast<rnode *> (r->slots[i].load ()), level + 1);
	  }
      }
    bsmap_free (r);
  }

  void
  bestspace_map::free_tree (rootdir *root)
  {
    std::size_t a, b, c;

    if (root == nullptr)
      {
	return;
      }
    for (a = 0; a < FANOUT; a++)
      {
	middir *mid = root->children[a].load ();
	if (mid == nullptr)
	  {
	    continue;
	  }
	for (b = 0; b < FANOUT; b++)
	  {
	    leafdir *leaf = mid->children[b].load ();
	    if (leaf == nullptr)
	      {
		continue;
	      }
	    for (c = 0; c < FANOUT; c++)
	      {
		bsmap_free (leaf->children[c].load ());
	      }
	    bsmap_free (leaf);
	  }
	bsmap_free (mid);
      }
    bsmap_free (root);
  }

  // ------------------------------------------------------------------ codes and hints

  void
  bestspace_map::set_code (record *rec, std::size_t n, std::size_t off, std::uint8_t code)
  {
    std::size_t word = off / 16, shift = (off % 16) * 4;
    std::uint64_t expected = rec->codes[word].load (), desired;
    std::uint8_t old_code;

    do
      {
	old_code = static_cast<std::uint8_t> ((expected >> shift) & 0xF);
	desired = (expected & ~ (0xFULL << shift)) | (static_cast<std::uint64_t> (code) << shift);
	if (desired == expected)
	  {
	    break;
	  }
      }
    while (!rec->codes[word].compare_exchange_weak (expected, desired));
    count_unverified_change (old_code, code);
    publish (rec, n, off, true);
  }

  void
  bestspace_map::count_unverified_change (std::uint8_t old_code, std::uint8_t new_code)
  {
    if (old_code == CODE_UNVERIFIED && new_code != CODE_UNVERIFIED)
      {
	m_num_unverified.fetch_sub (1);
      }
    else if (old_code != CODE_UNVERIFIED && new_code == CODE_UNVERIFIED)
      {
	m_num_unverified.fetch_add (1);
      }
  }

  void
  bestspace_map::set_bit (std::atomic<std::uint64_t> &w, std::uint64_t bit)
  {
    if ((w.load () & bit) == 0)
      {
	w.fetch_or (bit);
      }
  }

  // version first, then hint bits bottom-up, so a claim that clears a bit concurrently can detect the change
  void
  bestspace_map::publish (record *rec, std::size_t n, std::size_t off, bool mark_dirty)
  {
    rec->version.fetch_add (1);
    if (mark_dirty)
      {
	persist_word (n).fetch_or (1ULL << (n % FANOUT));
      }
    std::uint8_t code = get_code (rec, off);
    if (code < 1 || code > CODE_MAX_TIER || (rec->resident.load () & (1ULL << off)) != 0)
      {
	return;
      }
    rootdir *root = m_root.load ();
    if (root == nullptr)
      {
	// inline: a claim scans the records, there are no hint bits
	return;
      }
    std::size_t t = code - 1, g = n / FANOUT;
    set_bit (group_at (g)->h.bits[t], 1ULL << (n % FANOUT));
    set_above (3, g, 1U << t);
  }

  // A hint bit of each tier in 'tiers' (bit t: tier t + 1) was set in the word of a node: depth 1 middir, 2 leafdir,
  // 3 group, and index is the node's number at that depth. Set the bits above the node, bottom-up. A prune one level
  // up clears its bit when it finds the word empty, and a claim that clears a bit and then puts it back (see prune ()
  // and descend ()) leaves the word empty for a moment, so putting a bit back must set the bits above it again like
  // publish () does, or the hint of a page is lost for good.
  void
  bestspace_map::set_above (std::size_t depth, std::size_t index, std::uint32_t tiers)
  {
    rootdir *root = m_root.load ();
    std::size_t t, no;

    if (root == nullptr)
      {
	return;
      }
    for (t = 0; t < NUM_TIERS; t++)
      {
	if ((tiers & (1U << t)) == 0)
	  {
	    continue;
	  }
	no = index;
	if (depth >= 3)
	  {
	    middir *mid = root->children[no / (FANOUT * FANOUT)].load (std::memory_order_acquire);
	    leafdir *leaf = mid->children[ (no / FANOUT) % FANOUT].load (std::memory_order_acquire);
	    set_bit (leaf->h.bits[t], 1ULL << (no % FANOUT));
	    no /= FANOUT;
	  }
	if (depth >= 2)
	  {
	    middir *mid = root->children[no / FANOUT].load (std::memory_order_acquire);
	    set_bit (mid->h.bits[t], 1ULL << (no % FANOUT));
	    no /= FANOUT;
	  }
	if (depth >= 1)
	  {
	    set_bit (root->h.bits[t], 1ULL << no);
	  }
      }
  }

  std::uint64_t
  bestspace_map::mask_of (hint &h, std::uint8_t min_code)
  {
    std::uint64_t m = 0;
    for (std::uint8_t c = min_code; c <= CODE_MAX_TIER; c++)
      {
	m |= h.bits[c - 1].load ();
      }
    return m;
  }

  // after a child yielded nothing for tiers >= min_code: clear its bit where the child has no hint, then re-check the
  // child so a concurrent bottom-up setter is never lost. Returns the tiers (bit t: tier t + 1) whose bit was put
  // back: the word of the parent has a bit again, so the caller sets the bits above it (set_above ()).
  std::uint32_t
  bestspace_map::prune (hint &parent, hint &child, std::size_t i, std::uint8_t min_code)
  {
    std::uint64_t bit = 1ULL << i;
    std::uint32_t put_back = 0;
    for (std::uint8_t c = min_code; c <= CODE_MAX_TIER; c++)
      {
	if (child.bits[c - 1].load () == 0 && (parent.bits[c - 1].load () & bit) != 0)
	  {
	    parent.bits[c - 1].fetch_and (~bit);
	    if (child.bits[c - 1].load () != 0)
	      {
		parent.bits[c - 1].fetch_or (bit);
		put_back |= 1U << (c - 1);
	      }
	  }
      }
    return put_back;
  }

  // ------------------------------------------------------------------ updates

  bool
  bestspace_map::feedback (short volid, std::int32_t pageid, std::uint8_t code)
  {
    std::size_t n;
    record *rec = get_or_add (volid, pageid, n, ALL_UNVERIFIED, false);
    if (rec == nullptr)
      {
	return false;
      }
    std::size_t off = static_cast<std::uint32_t> (pageid) % PAGES_PER_SECTOR;
    // dirty first: a release that clears it after this point keeps this code instead of writing its older one
    rec->dirty.fetch_or (1ULL << off);
    set_code (rec, n, off, code);
    // and dirty again after the write: a claim clears the bit before it reads the code, so a claim that cleared the
    // first mark and read the code before this write holds the older code; the bit set now makes its release keep
    // this one
    rec->dirty.fetch_or (1ULL << off);
    count (CNT_FEEDBACK);
    return true;
  }

  bool
  bestspace_map::set_resident (short volid, std::int32_t pageid, std::uint8_t code)
  {
    std::size_t n;
    record *rec = get_or_add (volid, pageid, n, ALL_NOT_DATA, true);
    if (rec == nullptr)
      {
	return false;
      }
    std::size_t off = static_cast<std::uint32_t> (pageid) % PAGES_PER_SECTOR;
    std::uint64_t bit = 1ULL << off;
    rec->resident.fetch_or (bit);
    rec->dirty.fetch_and (~bit);
    set_code (rec, n, off, code);
    return true;
  }

  // A code fed back while the page was resident wins over the caller's, measured or not: the caller's value was taken
  // before the feedback (the page latch is released before this call), so writing it would hide space the feedback
  // reported, while keeping the fed-back value can only over-estimate, which a claim corrects.
  void
  bestspace_map::release (short volid, std::int32_t pageid, std::uint8_t code, bool measured)
  {
    std::size_t n;
    record *rec = get_or_add (volid, pageid, n, ALL_UNVERIFIED, false);
    if (rec == nullptr)
      {
	return;
      }
    std::size_t off = static_cast<std::uint32_t> (pageid) % PAGES_PER_SECTOR;
    std::size_t word = off / 16, shift = (off % 16) * 4;
    std::uint64_t bit = 1ULL << off;
    // read the code before the dirty bit is cleared: a feedback that sets dirty after the clear writes its code after
    // this read, so the exchange below sees a different code and leaves it
    std::uint64_t expected = rec->codes[word].load (), desired;
    std::uint8_t seen = static_cast<std::uint8_t> ((expected >> shift) & 0xF);
    bool dirty = (rec->dirty.fetch_and (~bit) & bit) != 0;
    bool replaced = false;

    rec->resident.fetch_and (~bit);
    if (!dirty)
      {
	// no feedback while resident: write the caller's code, unless a feedback changed the code since it was read
	while (static_cast<std::uint8_t> ((expected >> shift) & 0xF) == seen)
	  {
	    desired = (expected & ~ (0xFULL << shift)) | (static_cast<std::uint64_t> (code) << shift);
	    if (desired == expected || rec->codes[word].compare_exchange_weak (expected, desired))
	      {
		replaced = true;
		break;
	      }
	  }

	// a feedback that set dirty after the clear and wrote the same code as the one read above is invisible to the
	// exchange, which then replaced the fed-back code. The caller's code is lower here, so that would hide the space
	// the feedback reported: put the code read above back. If the feedback reported the caller's code instead,
	// this over-estimates, which a claim corrects; if it has not written yet, its code still wins. One rare
	// three-way race remains: a claim that takes the page between the exchange and this check reads the lowered
	// code and clears the dirty bit, so the code is not put back and stays low until the page's next feedback.
	if (replaced && code < seen && seen <= CODE_MAX_TIER && (rec->dirty.load () & bit) != 0)
	  {
	    expected = rec->codes[word].load ();
	    while (static_cast<std::uint8_t> ((expected >> shift) & 0xF) == code)
	      {
		desired = (expected & ~ (0xFULL << shift)) | (static_cast<std::uint64_t> (seen) << shift);
		if (rec->codes[word].compare_exchange_weak (expected, desired))
		  {
		    replaced = false;
		    break;
		  }
	      }
	  }
      }
    if (replaced)
      {
	count_unverified_change (seen, code);
      }
    // the page is not resident any more: publish the code it keeps (a feedback that changed it marked the sector)
    publish (rec, n, off, replaced);
    count (CNT_RELEASED);
  }

  void
  bestspace_map::forget (short volid, std::int32_t pageid)
  {
    std::size_t n;
    record *rec = find (volid, pageid, n);
    if (rec == nullptr)
      {
	return;
      }
    std::size_t off = static_cast<std::uint32_t> (pageid) % PAGES_PER_SECTOR;
    std::uint64_t bit = 1ULL << off;
    rec->resident.fetch_and (~bit);
    rec->dirty.fetch_and (~bit);
    set_code (rec, n, off, CODE_NOT_DATA);
  }

  // ------------------------------------------------------------------ search

  std::size_t
  bestspace_map::claim (std::uint8_t min_code, bestspace_map_page *out, std::size_t max_count)
  {
    std::size_t found = 0, budget = CLAIM_VISIT_LIMIT, n, s;

    if (min_code < 1)
      {
	min_code = 1;
      }
    n = m_num_sectors.load (std::memory_order_acquire);
    if (min_code > CODE_MAX_TIER || max_count == 0 || n == 0)
      {
	return 0;
      }

    rootdir *root = m_root.load ();
    if (root == nullptr)
      {
	for (s = 0; s < n && s < INLINE_SECTORS && found < max_count; s++)
	  {
	    (void) claim_sector (&m_inline[s], m_inline_keys[s], min_code, out, max_count, found);
	  }
      }
    else
      {
	claim_node<rootdir, middir> (*root, 0, min_code, out, max_count, found, budget);
      }
    count (CNT_CLAIMED, found);
    return found;
  }

  // claim pages of one sector; true if every page of the sector was looked at
  bool
  bestspace_map::claim_sector (record *rec, std::uint64_t key, std::uint8_t min_code, bestspace_map_page *out,
			       std::size_t max_count, std::size_t &found)
  {
    std::size_t off;

    for (off = 0; off < PAGES_PER_SECTOR && found < max_count; off++)
      {
	std::uint8_t c = get_code (rec, off);
	std::uint64_t bit = 1ULL << off;
	if (c < min_code || c > CODE_MAX_TIER || (rec->resident.load () & bit) != 0)
	  {
	    continue;
	  }
	if ((rec->resident.fetch_or (bit) & bit) != 0)
	  {
	    continue;
	  }
	rec->dirty.fetch_and (~bit);
	out[found].volid = key_volid (key);
	out[found].pageid = key_first_pageid (key) + static_cast<std::int32_t> (off);
	out[found].code = get_code (rec, off);
	found++;
      }
    return off == PAGES_PER_SECTOR;
  }

  template <typename Node, typename Child>
  void
  bestspace_map::claim_node (Node &nd, std::size_t base, std::uint8_t min_code, bestspace_map_page *out,
			     std::size_t max_count, std::size_t &found, std::size_t &budget)
  {
    // depth of this node as set_above () counts it: 0 rootdir, 1 middir, 2 leafdir
    constexpr std::size_t depth = std::is_same<Node, rootdir>::value ? 0 : (std::is_same<Node, middir>::value ? 1 : 2);
    std::uint64_t mask = mask_of (nd.h, min_code);

    while (mask != 0 && found < max_count && budget > 0)
      {
	std::size_t i = lowest_bit (mask);
	mask &= mask - 1;
	Child *ch = nd.children[i].load (std::memory_order_acquire);
	if (ch == nullptr)
	  {
	    continue;
	  }
	descend (*ch, base * FANOUT + i, min_code, out, max_count, found, budget);
	if (found < max_count)
	  {
	    std::uint32_t put_back = prune (nd.h, ch->h, i, min_code);
	    if (put_back != 0)
	      {
		set_above (depth, base, put_back);
	      }
	  }
      }
  }

  void
  bestspace_map::descend (middir &m, std::size_t base, std::uint8_t min_code, bestspace_map_page *out,
			  std::size_t max_count, std::size_t &found, std::size_t &budget)
  {
    claim_node<middir, leafdir> (m, base, min_code, out, max_count, found, budget);
  }

  void
  bestspace_map::descend (leafdir &l, std::size_t base, std::uint8_t min_code, bestspace_map_page *out,
			  std::size_t max_count, std::size_t &found, std::size_t &budget)
  {
    claim_node<leafdir, group> (l, base, min_code, out, max_count, found, budget);
  }

  // group level: g is the group number. A sector that yields nothing costs one unit of the budget, so a run of stale
  // hint bits is cleaned up across several claims instead of in one.
  void
  bestspace_map::descend (group &grp, std::size_t g, std::uint8_t min_code, bestspace_map_page *out,
			  std::size_t max_count, std::size_t &found, std::size_t &budget)
  {
    std::size_t n_total = m_num_sectors.load (std::memory_order_acquire);
    std::uint64_t mask = mask_of (grp.h, min_code);

    while (mask != 0 && found < max_count && budget > 0)
      {
	std::size_t si = lowest_bit (mask);
	mask &= mask - 1;
	std::size_t n = g * FANOUT + si;
	if (n >= n_total)
	  {
	    break;
	  }
	record *rec = sector_at (n);
	std::uint64_t version = rec->version.load ();
	std::size_t before = found;
	bool whole = claim_sector (rec, key_at (n), min_code, out, max_count, found);
	if (found == before)
	  {
	    budget--;
	  }
	if (!whole)
	  {
	    continue;
	  }
	// the whole sector was scanned: drop the hint bits of tiers with no claimable page left
	bool left[CODE_MAX_TIER + 1] = { false };
	std::uint64_t res = rec->resident.load ();
	for (std::size_t p = 0; p < PAGES_PER_SECTOR; p++)
	  {
	    if ((res & (1ULL << p)) == 0)
	      {
		std::uint8_t c = get_code (rec, p);
		if (c >= 1 && c <= CODE_MAX_TIER)
		  {
		    left[c] = true;
		  }
	      }
	  }
	std::uint64_t sbit = 1ULL << si;
	std::uint32_t put_back = 0;
	for (std::uint8_t c = min_code; c <= CODE_MAX_TIER; c++)
	  {
	    if (!left[c] && (grp.h.bits[c - 1].load () & sbit) != 0)
	      {
		grp.h.bits[c - 1].fetch_and (~sbit);
		if (rec->version.load () != version)
		  {
		    grp.h.bits[c - 1].fetch_or (sbit);
		    put_back |= 1U << (c - 1);
		  }
	      }
	  }
	if (put_back != 0)
	  {
	    set_above (3, g, put_back);
	  }
      }
  }

  bool
  bestspace_map::peek (short volid, std::int32_t pageid, std::uint8_t &code, bool &resident)
  {
    std::size_t n;
    record *rec = find (volid, pageid, n);
    if (rec == nullptr)
      {
	return false;
      }
    std::size_t off = static_cast<std::uint32_t> (pageid) % PAGES_PER_SECTOR;
    code = get_code (rec, off);
    resident = (rec->resident.load () & (1ULL << off)) != 0;
    return true;
  }

  std::uint64_t
  bestspace_map::resident_dirty_above (const bestspace_map_page *pages, std::size_t count)
  {
    std::uint64_t result = 0, last_key = 0, key, bit;
    record *rec = nullptr;
    std::size_t i, n, off;
    std::uint8_t code;

    assert (count <= 64);
    for (i = 0; i < count; i++)
      {
	if (pages[i].pageid < 0)
	  {
	    continue;
	  }
	// the slots of a shard often share sectors: look a sector up once per run
	key = make_key (pages[i].volid, pages[i].pageid);
	if (key != last_key)
	  {
	    rec = find (pages[i].volid, pages[i].pageid, n);
	    last_key = key;
	  }
	if (rec == nullptr)
	  {
	    continue;
	  }
	off = static_cast<std::uint32_t> (pages[i].pageid) % PAGES_PER_SECTOR;
	bit = 1ULL << off;
	if ((rec->resident.load () & bit) == 0 || (rec->dirty.load () & bit) == 0)
	  {
	    continue;
	  }
	code = get_code (rec, off);
	if (code > pages[i].code && code <= CODE_MAX_TIER)
	  {
	    result |= 1ULL << i;
	  }
      }
    return result;
  }

  // ------------------------------------------------------------------ persistence

  std::size_t
  bestspace_map::num_sectors () const
  {
    return m_num_sectors.load (std::memory_order_acquire);
  }

  std::size_t
  bestspace_map::num_groups () const
  {
    return (num_sectors () + FANOUT - 1) / FANOUT;
  }

  std::uint64_t
  bestspace_map::take_persist_dirty (std::size_t g)
  {
    std::uint64_t bits = 0;
    group *grp;

    if (g == 0)
      {
	bits |= m_inline_persist_dirty.exchange (0);
      }
    grp = group_ptr (g);
    if (grp != nullptr)
      {
	bits |= grp->persist_dirty.exchange (0);
      }
    return bits;
  }

  void
  bestspace_map::put_back_persist_dirty (std::size_t g, std::uint64_t bits)
  {
    const std::uint64_t inline_mask = (1ULL << INLINE_SECTORS) - 1;
    group *grp;

    if (g == 0)
      {
	m_inline_persist_dirty.fetch_or (bits & inline_mask);
	bits &= ~inline_mask;
      }
    if (bits == 0)
      {
	return;
      }
    grp = group_ptr (g);
    assert (grp != nullptr);
    if (grp != nullptr)
      {
	grp->persist_dirty.fetch_or (bits);
      }
  }

  void
  bestspace_map::snapshot_sector (std::size_t n, bestspace_map_entry &out)
  {
    record *rec = sector_at (n);
    out.key = key_at (n);
    for (int i = 0; i < 4; i++)
      {
	out.codes[i] = rec->codes[i].load ();
      }
  }

  bool
  bestspace_map::load_sector (const bestspace_map_entry &entry)
  {
    std::size_t n, w, p, off;
    std::size_t expected = num_sectors ();
    record *rec;

    if (entry.key == 0)
      {
	return false;
      }
    rec = get_or_add (key_volid (entry.key), key_first_pageid (entry.key), n, ALL_NOT_DATA, false);
    if (rec == nullptr || n != expected)
      {
	// out of memory, or a key that is already in the map (a duplicate on disk)
	return false;
      }
    for (w = 0; w < 4; w++)
      {
	std::uint64_t old_codes = rec->codes[w].exchange (entry.codes[w]);
	for (p = 0; p < 16; p++)
	  {
	    count_unverified_change (static_cast<std::uint8_t> ((old_codes >> (p * 4)) & 0xF),
				     static_cast<std::uint8_t> ((entry.codes[w] >> (p * 4)) & 0xF));
	  }
      }
    for (off = 0; off < PAGES_PER_SECTOR; off++)
      {
	publish (rec, n, off, false);
      }
    return true;
  }

  bool
  bestspace_map::merge_sector (const bestspace_map_entry &entry, std::uint64_t mask)
  {
    std::size_t n, off;
    record *rec = get_or_add (key_volid (entry.key), key_first_pageid (entry.key), n, ALL_UNVERIFIED, false);

    if (rec == nullptr)
      {
	return false;
      }
    while (mask != 0)
      {
	off = lowest_bit (mask);
	mask &= mask - 1;
	// marked before and after the write, like feedback ()
	rec->dirty.fetch_or (1ULL << off);
	set_code (rec, n, off, entry_code (entry, off));
	rec->dirty.fetch_or (1ULL << off);
      }
    return true;
  }

  bool
  bestspace_map::add_missing_sector (short volid, std::int32_t first_pageid, std::uint64_t allocated)
  {
    std::size_t n, off;
    std::size_t before = num_sectors ();
    record *rec = get_or_add (volid, first_pageid, n, ALL_NOT_DATA, false);

    if (rec == nullptr)
      {
	return false;
      }
    if (n < before)
      {
	// a sector the map already had: its codes stay
	return true;
      }
    while (allocated != 0)
      {
	off = lowest_bit (allocated);
	allocated &= allocated - 1;
	set_code (rec, n, off, CODE_UNVERIFIED);
      }
    // the new sector must reach the disk even when none of its pages is allocated
    persist_word (n).fetch_or (1ULL << (n % FANOUT));
    return true;
  }

  std::size_t
  bestspace_map::next_unverified (std::size_t &cursor, bestspace_map_page *out, std::size_t max_count)
  {
    std::size_t total = num_sectors () * PAGES_PER_SECTOR, found = 0;

    while (cursor < total && found < max_count)
      {
	std::size_t n = cursor / PAGES_PER_SECTOR, off = cursor % PAGES_PER_SECTOR;
	record *rec = sector_at (n);
	if (off == 0 && !has_unverified_nibble (rec->codes[0].load ()) && !has_unverified_nibble (rec->codes[1].load ())
	    && !has_unverified_nibble (rec->codes[2].load ()) && !has_unverified_nibble (rec->codes[3].load ()))
	  {
	    cursor += PAGES_PER_SECTOR;
	    continue;
	  }
	if (get_code (rec, off) == CODE_UNVERIFIED && (rec->resident.load () & (1ULL << off)) == 0)
	  {
	    std::uint64_t key = key_at (n);
	    out[found].volid = key_volid (key);
	    out[found].pageid = key_first_pageid (key) + static_cast<std::int32_t> (off);
	    out[found].code = CODE_UNVERIFIED;
	    found++;
	  }
	cursor++;
      }
    return found;
  }

  void
  bestspace_map::mark_all_unverified ()
  {
    std::size_t n = num_sectors (), s, off;

    for (s = 0; s < n; s++)
      {
	record *rec = sector_at (s);
	for (off = 0; off < PAGES_PER_SECTOR; off++)
	  {
	    if (get_code (rec, off) != CODE_NOT_DATA)
	      {
		set_code (rec, s, off, CODE_UNVERIFIED);
	      }
	  }
      }
  }

  // ------------------------------------------------------------------ state

  bool
  bestspace_map::is_inline () const
  {
    return m_root.load () == nullptr;
  }

  std::uint64_t
  bestspace_map::num_unverified () const
  {
    return m_num_unverified.load ();
  }

  bool
  bestspace_map::needs_retry () const
  {
    return m_needs_retry.load ();
  }

  void
  bestspace_map::clear_needs_retry ()
  {
    m_needs_retry.store (false);
  }

  void
  bestspace_map::count_codes (std::uint64_t counts[16])
  {
    std::size_t n = num_sectors (), s, off;

    for (off = 0; off < 16; off++)
      {
	counts[off] = 0;
      }
    for (s = 0; s < n; s++)
      {
	record *rec = sector_at (s);
	for (off = 0; off < PAGES_PER_SECTOR; off++)
	  {
	    counts[get_code (rec, off)]++;
	  }
      }
  }

  void
  bestspace_map::count (counter id, std::uint64_t value)
  {
    m_counters[id].fetch_add (value, std::memory_order_relaxed);
  }

  void
  bestspace_map::get_usage (std::size_t &bytes, std::uint64_t counters[CNT_LAST])
  {
    {
      std::lock_guard<std::mutex> lock (m_grow_mutex);
      bytes = m_bytes;
    }
    for (int i = 0; i < CNT_LAST; i++)
      {
	counters[i] = m_counters[i].load (std::memory_order_relaxed);
      }
  }

  //////////////////////////////////////////////////////////////////////////
  // bestspace_sector_table
  //////////////////////////////////////////////////////////////////////////

  bestspace_sector_table::bestspace_sector_table ()
    : m_size (0)
    , m_lost_mutex ()
    , m_num_lost (0)
    , m_lost_all (false)
  {
    for (std::size_t i = 0; i < STRIPES; i++)
      {
	m_stripes[i].buckets = nullptr;
	m_stripes[i].num_buckets = 0;
	m_stripes[i].count = 0;
      }
  }

  bestspace_sector_table::~bestspace_sector_table ()
  {
    entry *list = take_all ();

    free_list (list);
    for (std::size_t i = 0; i < STRIPES; i++)
      {
	if (m_stripes[i].buckets != nullptr)
	  {
	    free_and_init (m_stripes[i].buckets);
	  }
      }
  }

  // splitmix64 finalizer
  std::uint64_t
  bestspace_sector_table::mix (std::uint64_t v)
  {
    v ^= v >> 30;
    v *= 0xBF58476D1CE4E5B9ULL;
    v ^= v >> 27;
    v *= 0x94D049BB133111EBULL;
    v ^= v >> 31;
    return v;
  }

  // the entries of a group share a stripe, so take_group () visits one stripe; the recovery table (group 0) spreads
  // over the stripes by sector key
  std::size_t
  bestspace_sector_table::stripe_of (std::uint64_t group, std::uint64_t key)
  {
    return static_cast<std::size_t> (mix (group != 0 ? group : key) % STRIPES);
  }

  // a different hash input than stripe_of (), so the entries of one stripe spread over all of its buckets
  std::size_t
  bestspace_sector_table::bucket_of (const stripe &s, std::uint64_t group, std::uint64_t key)
  {
    return static_cast<std::size_t> (mix ((group * 0x9E3779B97F4A7C15ULL) ^ key ^ 0x5851F42D4C957F2DULL) % s.num_buckets);
  }

  // caller holds s.mutex; out of memory keeps the old array (longer chains, still correct)
  void
  bestspace_sector_table::grow (stripe &s)
  {
    std::size_t nb = s.num_buckets == 0 ? INITIAL_BUCKETS : s.num_buckets * 2, old_nb = s.num_buckets, i;
    entry **buckets = static_cast<entry **> (malloc (nb * sizeof (entry *)));
    entry **old = s.buckets;

    if (buckets == nullptr)
      {
	return;
      }
    std::memset (buckets, 0, nb * sizeof (entry *));
    s.buckets = buckets;
    s.num_buckets = nb;
    for (i = 0; i < old_nb; i++)
      {
	entry *e = old[i];
	while (e != nullptr)
	  {
	    entry *next = e->next;
	    std::size_t b = bucket_of (s, e->group, e->key);
	    e->next = buckets[b];
	    buckets[b] = e;
	    e = next;
	  }
      }
    if (old != nullptr)
      {
	free_and_init (old);
      }
  }

  bool
  bestspace_sector_table::record (std::uint64_t group, std::uint64_t key, std::size_t offset, std::uint8_t code,
				  std::uint64_t owner, std::uint64_t lsa)
  {
    stripe &s = m_stripes[stripe_of (group, key)];
    std::lock_guard<std::mutex> lock (s.mutex);

    return record_locked (s, group, key, offset, code, owner, lsa);
  }

  bestspace_sector_table::record_result
  bestspace_sector_table::record_if (std::uint64_t group, std::uint64_t key, std::size_t offset, std::uint8_t code,
				     bool (*check) (void *), void *arg)
  {
    stripe &s = m_stripes[stripe_of (group, key)];
    std::lock_guard<std::mutex> lock (s.mutex);

    if (check != nullptr && !check (arg))
      {
	return REJECTED;
      }
    return record_locked (s, group, key, offset, code, 0, 0) ? RECORDED : NO_MEMORY;
  }

  // caller holds s.mutex
  bool
  bestspace_sector_table::record_locked (stripe &s, std::uint64_t group, std::uint64_t key, std::size_t offset,
					 std::uint8_t code, std::uint64_t owner, std::uint64_t lsa)
  {
    entry *e;

    if (s.buckets == nullptr || s.count >= 4 * s.num_buckets)
      {
	grow (s);
	if (s.buckets == nullptr)
	  {
	    return false;
	  }
      }
    std::size_t b = bucket_of (s, group, key);
    for (e = s.buckets[b]; e != nullptr; e = e->next)
      {
	if (e->group == group && e->key == key)
	  {
	    break;
	  }
      }
    if (e == nullptr)
      {
	e = bsmap_alloc<entry> ();
	if (e == nullptr)
	  {
	    return false;
	  }
	e->group = group;
	e->key = key;
	e->owner = owner;
	e->owner_lsa = lsa;
	e->next = s.buckets[b];
	s.buckets[b] = e;
	s.count++;
	m_size.fetch_add (1);
      }
    else if (e->owner != owner)
      {
	if (lsa <= e->owner_lsa)
	  {
	    // an event of an earlier owner of the sector
	    return true;
	  }
	e->owner = owner;
	e->mask = 0;
	std::memset (e->codes, 0, sizeof (e->codes));
      }
    if (lsa > e->owner_lsa)
      {
	e->owner_lsa = lsa;
      }
    std::size_t shift = (offset % 16) * 4;
    e->codes[offset / 16] = (e->codes[offset / 16] & ~ (0xFULL << shift)) | (static_cast<std::uint64_t> (code) << shift);
    e->mask |= 1ULL << offset;
    return true;
  }

  bool
  bestspace_sector_table::record_sector (std::uint64_t group, std::uint64_t key, const std::uint64_t codes[4],
					 std::uint64_t mask)
  {
    std::size_t off;

    while (mask != 0)
      {
	off = lowest_bit (mask);
	mask &= mask - 1;
	if (!record (group, key, off, static_cast<std::uint8_t> ((codes[off / 16] >> ((off % 16) * 4)) & 0xF), 0, 0))
	  {
	    return false;
	  }
      }
    return true;
  }

  // caller holds s.mutex
  bestspace_sector_table::entry *
  bestspace_sector_table::detach_if (stripe &s, std::uint64_t group, bool all)
  {
    entry *list = nullptr;

    for (std::size_t i = 0; i < s.num_buckets; i++)
      {
	entry **pp = &s.buckets[i];
	while (*pp != nullptr)
	  {
	    entry *e = *pp;
	    if (all || e->group == group)
	      {
		*pp = e->next;
		e->next = list;
		list = e;
		s.count--;
		m_size.fetch_sub (1);
	      }
	    else
	      {
		pp = &e->next;
	      }
	  }
      }
    return list;
  }

  bestspace_sector_table::entry *
  bestspace_sector_table::take_group (std::uint64_t group)
  {
    assert (group != 0);
    stripe &s = m_stripes[stripe_of (group, 0)];
    std::lock_guard<std::mutex> lock (s.mutex);
    return detach_if (s, group, false);
  }

  bestspace_sector_table::entry *
  bestspace_sector_table::take_all ()
  {
    entry *list = nullptr;

    for (std::size_t i = 0; i < STRIPES; i++)
      {
	entry *part, *tail;
	{
	  std::lock_guard<std::mutex> lock (m_stripes[i].mutex);
	  part = detach_if (m_stripes[i], 0, true);
	}
	if (part == nullptr)
	  {
	    continue;
	  }
	for (tail = part; tail->next != nullptr; tail = tail->next)
	  {
	    ;
	  }
	tail->next = list;
	list = part;
      }
    return list;
  }

  void
  bestspace_sector_table::free_list (entry *&list)
  {
    while (list != nullptr)
      {
	entry *next = list->next;
	bsmap_free (list);
	list = next;
      }
  }

  std::size_t
  bestspace_sector_table::size () const
  {
    return m_size.load ();
  }

  void
  bestspace_sector_table::mark_lost (std::uint64_t group, std::uint64_t owner)
  {
    std::lock_guard<std::mutex> lock (m_lost_mutex);
    std::size_t i;

    for (i = 0; i < m_num_lost; i++)
      {
	if (m_lost[i][0] == group && m_lost[i][1] == owner)
	  {
	    return;
	  }
      }
    if (m_num_lost == MAX_LOST)
      {
	m_lost_all.store (true);
	return;
      }
    m_lost[m_num_lost][0] = group;
    m_lost[m_num_lost][1] = owner;
    m_num_lost++;
  }

  bool
  bestspace_sector_table::is_lost (std::uint64_t group, std::uint64_t owner)
  {
    std::lock_guard<std::mutex> lock (m_lost_mutex);

    if (m_lost_all.load ())
      {
	return true;
      }
    for (std::size_t i = 0; i < m_num_lost; i++)
      {
	if (m_lost[i][0] == group && m_lost[i][1] == owner)
	  {
	    return true;
	  }
      }
    return false;
  }

  bool
  bestspace_sector_table::take_lost (std::uint64_t &group, std::uint64_t &owner)
  {
    std::lock_guard<std::mutex> lock (m_lost_mutex);

    if (m_num_lost == 0)
      {
	return false;
      }
    m_num_lost--;
    group = m_lost[m_num_lost][0];
    owner = m_lost[m_num_lost][1];
    return true;
  }

  bool
  bestspace_sector_table::lost_all () const
  {
    return m_lost_all.load ();
  }

  void
  bestspace_sector_table::clear_lost (std::uint64_t group, std::uint64_t owner)
  {
    std::lock_guard<std::mutex> lock (m_lost_mutex);

    for (std::size_t i = 0; i < m_num_lost; i++)
      {
	if (m_lost[i][0] == group && m_lost[i][1] == owner)
	  {
	    m_num_lost--;
	    m_lost[i][0] = m_lost[m_num_lost][0];
	    m_lost[i][1] = m_lost[m_num_lost][1];
	    return;
	  }
      }
  }
}
