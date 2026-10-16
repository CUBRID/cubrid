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
// bestspace_fcmap.hpp - POC-2: full-coverage free space map (throwaway spike, not for merge)
//
// One record per disk sector (64 pages) of a heap file, numbered densely in the order the map first sees each
// sector. Records live in 64-record groups that are allocated as needed and never move. The directory that finds a
// group from its number (root -> mid -> leafdir -> group, 64 children each) also carries the search hints: every
// node keeps one 64-bit word per free-space tier whose bit i says "child i may hold a claimable page of this tier".
// Writers only set hint bits (bottom-up); a search clears a bit after it found nothing below it and re-checks the
// child, so a bit can be stale-on but never stale-off. A 64-ary radix tree maps (volid, sectid) to the dense number.
// Records and nodes only grow, so lock-free readers never see freed memory. Adding a sector takes a per-map mutex.
//

#ifndef _BESTSPACE_FCMAP_HPP_
#define _BESTSPACE_FCMAP_HPP_

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <new>

namespace cubstorage
{
  class fcmap
  {
    public:
      static constexpr std::size_t PAGES_PER_SECTOR = 64;
      static constexpr std::size_t FANOUT = 64;
      static constexpr std::size_t NUM_TIERS = 8;
      static constexpr std::uint8_t CODE_NONE = 0;
      static constexpr std::uint8_t CODE_MAX_TIER = 8;
      static constexpr std::uint8_t CODE_NOT_DATA = 0xE;
      // capacity: 64^3 groups x 64 sectors = 16.7M sectors (16 TiB with 16 KiB pages)
      static constexpr std::size_t MAX_SECTORS = FANOUT * FANOUT * FANOUT * FANOUT;

      enum counter
      {
	CNT_FEEDBACK,
	CNT_CLAIMED,
	CNT_VERIFY_FAIL,
	CNT_RELEASED,
	CNT_ALLOC_BATCH,
	CNT_REUSED,
	CNT_SCRUB_READ,
	CNT_SCRUB_FOUND,
	CNT_LAST
      };

      struct claimed
      {
	short volid;
	std::int32_t pageid;
	std::uint8_t code;
      };

    private:
      struct hint
      {
	std::atomic<std::uint64_t> bits[NUM_TIERS];
      };

      struct record
      {
	std::atomic<std::uint64_t> codes[4];	// 64 x 4-bit page codes
	std::atomic<std::uint64_t> resident;	// page is held by a shard L1 slot
	std::atomic<std::uint64_t> dirty;	// free space was fed back while resident
	std::atomic<std::uint64_t> version;	// bumped by every published change
      };

      struct group
      {
	hint h;				// bit s: sector s of this group
	std::atomic<std::uint64_t> persist_dirty;	// sectors changed since the last flush
	std::uint64_t keys[FANOUT];	// ((volid + 1) << 32) | sectid, immutable once published
	record sectors[FANOUT];
      };

      template <typename Child>
      struct node
      {
	hint h;
	std::atomic<Child *> children[FANOUT];
      };

      using leafdir = node<group>;
      using middir = node<leafdir>;
      using rootdir = node<middir>;

      // radix index: 40-bit key (15-bit volid, 25-bit sectid), 7 levels of 6 bits
      static constexpr int RADIX_LEVELS = 7;
      struct rnode
      {
	std::atomic<void *> slots[FANOUT];	// inner: child rnode*, leaf: uint32 number + 1 packed in pointer bits
      };

    public:
      fcmap ()
	: m_root ()
	, m_num_sectors (0)
	, m_radix (nullptr)
	, m_grow_mutex ()
	, m_bytes (sizeof (fcmap))
      {
	std::size_t i;

	for (i = 0; i < CNT_LAST; i++)
	  {
	    m_counters[i].store (0);
	  }
      }

      ~fcmap ()
      {
	std::size_t a, b, c;
	for (a = 0; a < FANOUT; a++)
	  {
	    middir *mid = m_root.children[a].load ();
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
		    delete leaf->children[c].load ();
		  }
		delete leaf;
	      }
	    delete mid;
	  }
	free_radix (m_radix.load (), 0);
      }

      fcmap (const fcmap &) = delete;
      fcmap &operator= (const fcmap &) = delete;

      void count (counter id, std::uint64_t value = 1)
      {
	m_counters[id].fetch_add (value, std::memory_order_relaxed);
      }

      // ------------------------------------------------------------------ updates
      // free space feedback (vacuum, delete, rollback, stats scan): records the code even while the page is resident
      bool feedback (short volid, std::int32_t pageid, std::uint8_t code)
      {
	std::size_t n;
	record *rec = get_or_add (volid, pageid, n);
	if (rec == nullptr)
	  {
	    return false;
	  }
	std::size_t off = static_cast<std::uint32_t> (pageid) % PAGES_PER_SECTOR;
	set_code (rec, n, off, code);
	rec->dirty.fetch_or (1ULL << off);
	count (CNT_FEEDBACK);
	return true;
      }

      // a page enters a shard (new allocation or snapshot)
      void set_resident (short volid, std::int32_t pageid, std::uint8_t code)
      {
	std::size_t n;
	record *rec = get_or_add (volid, pageid, n);
	if (rec == nullptr)
	  {
	    return;
	  }
	std::size_t off = static_cast<std::uint32_t> (pageid) % PAGES_PER_SECTOR;
	std::uint64_t bit = 1ULL << off;
	rec->resident.fetch_or (bit);
	rec->dirty.fetch_and (~bit);
	set_code (rec, n, off, code);
      }

      // a page leaves a shard; keep the fed-back code unless the caller measured the page
      void release (short volid, std::int32_t pageid, std::uint8_t code, bool measured)
      {
	std::size_t n;
	record *rec = get_or_add (volid, pageid, n);
	if (rec == nullptr)
	  {
	    return;
	  }
	std::size_t off = static_cast<std::uint32_t> (pageid) % PAGES_PER_SECTOR;
	std::uint64_t bit = 1ULL << off;
	bool dirty = (rec->dirty.fetch_and (~bit) & bit) != 0;
	rec->resident.fetch_and (~bit);
	if (measured || !dirty)
	  {
	    set_code (rec, n, off, code);
	  }
	else
	  {
	    publish (rec, n, off);
	  }
	count (CNT_RELEASED);
      }

      void remove (short volid, std::int32_t pageid)
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

      // restore a persisted sector (load): codes only, residency untouched
      bool load_sector (std::uint64_t key, const std::uint64_t codes[4])
      {
	std::size_t n, off;
	short volid = static_cast<short> ((key >> 32) - 1);
	std::int32_t pageid = static_cast<std::int32_t> ((key & 0xFFFFFFFFULL) * PAGES_PER_SECTOR);
	record *rec = get_or_add (volid, pageid, n);
	if (rec == nullptr)
	  {
	    return false;
	  }
	for (off = 0; off < 4; off++)
	  {
	    rec->codes[off].store (codes[off]);
	  }
	for (off = 0; off < PAGES_PER_SECTOR; off++)
	  {
	    publish (rec, n, off);
	  }
	return true;
      }

      // unclean restart: every allocated page of the file must be in the map so the scrub can measure it.
      // a page the map never saw becomes CODE_NONE; a page no longer allocated becomes CODE_NOT_DATA.
      bool merge_allocated (short volid, std::int32_t first_pageid, std::uint64_t alloc_bitmap)
      {
	std::size_t n, off;
	record *rec = get_or_add (volid, first_pageid, n);
	if (rec == nullptr)
	  {
	    return false;
	  }
	for (off = 0; off < PAGES_PER_SECTOR; off++)
	  {
	    std::uint8_t c = get_code (rec, off);
	    bool allocated = (alloc_bitmap & (1ULL << off)) != 0;
	    if (allocated && c == CODE_NOT_DATA)
	      {
		set_code (rec, n, off, CODE_NONE);
	      }
	    else if (!allocated && c != CODE_NOT_DATA && (rec->resident.load () & (1ULL << off)) == 0)
	      {
		set_code (rec, n, off, CODE_NOT_DATA);
	      }
	  }
	return true;
      }

      void clear_all_resident ()
      {
	std::size_t n = m_num_sectors.load (std::memory_order_acquire), s, off;
	for (s = 0; s < n; s++)
	  {
	    record *rec = sector_at (s);
	    std::uint64_t res = rec->resident.exchange (0);
	    rec->dirty.store (0);
	    while (res != 0)
	      {
		off = __builtin_ctzll (res);
		res &= res - 1;
		publish (rec, s, off);
	      }
	  }
      }

      // ------------------------------------------------------------------ search
      // claim up to max_count non-resident pages whose code >= min_code, lowest sector number first
      std::size_t claim (std::uint8_t min_code, claimed *out, std::size_t max_count)
      {
	std::size_t found = 0;
	if (min_code < 1)
	  {
	    min_code = 1;
	  }
	if (min_code > CODE_MAX_TIER || max_count == 0 || m_num_sectors.load (std::memory_order_acquire) == 0)
	  {
	    return 0;
	  }
	claim_node<rootdir, middir> (m_root, 3, 0, min_code, out, max_count, found);
	count (CNT_CLAIMED, found);
	return found;
      }

      // up to max_count non-resident pages with the highest codes, for the 128-entry snapshot in the heap header
      std::size_t collect (claimed *out, std::size_t max_count)
      {
	std::size_t n = m_num_sectors.load (std::memory_order_acquire), s, off, found = 0;
	int pass;
	for (pass = 0; pass < 2 && found < max_count; pass++)
	  {
	    std::uint8_t lo = pass == 0 ? 5 : 1, hi = pass == 0 ? CODE_MAX_TIER : 4;
	    for (s = 0; s < n && found < max_count; s++)
	      {
		record *rec = sector_at (s);
		std::uint64_t res = rec->resident.load ();
		for (off = 0; off < PAGES_PER_SECTOR && found < max_count; off++)
		  {
		    std::uint8_t c = get_code (rec, off);
		    if (c < lo || c > hi || (res & (1ULL << off)) != 0)
		      {
			continue;
		      }
		    out[found++] = to_claimed (key_at (s), off, c);
		  }
	      }
	  }
	return found;
      }

      // ------------------------------------------------------------------ persistence helpers
      std::size_t num_sectors () const
      {
	return m_num_sectors.load (std::memory_order_acquire);
      }

      // copy sector n for writing it out (key + codes)
      void snapshot_sector (std::size_t n, std::uint64_t &key, std::uint64_t codes[4])
      {
	record *rec = sector_at (n);
	key = key_at (n);
	for (int i = 0; i < 4; i++)
	  {
	    codes[i] = rec->codes[i].load ();
	  }
      }

      // take the persist-dirty bits of group g (64 sectors starting at g * 64)
      std::uint64_t take_dirty_group (std::size_t g)
      {
	return group_at (g)->persist_dirty.exchange (0);
      }

      void put_back_dirty_group (std::size_t g, std::uint64_t bits)
      {
	group_at (g)->persist_dirty.fetch_or (bits);
      }

      // ------------------------------------------------------------------ scrub (after an unclean restart)
      // pages that the map says have no space; the caller measures them and feeds back the real code
      std::size_t scrub_candidates (std::size_t &cursor, claimed *out, std::size_t max_count)
      {
	std::size_t n = m_num_sectors.load (std::memory_order_acquire), found = 0;
	while (cursor < n * PAGES_PER_SECTOR && found < max_count)
	  {
	    std::size_t s = cursor / PAGES_PER_SECTOR, off = cursor % PAGES_PER_SECTOR;
	    record *rec = sector_at (s);
	    if (get_code (rec, off) == CODE_NONE && (rec->resident.load () & (1ULL << off)) == 0)
	      {
		out[found++] = to_claimed (key_at (s), off, CODE_NONE);
	      }
	    cursor++;
	  }
	return found;
      }

      void get_usage (std::size_t &num_sectors, std::size_t &bytes, std::uint64_t counters[CNT_LAST])
      {
	num_sectors = m_num_sectors.load ();
	{
	  std::lock_guard<std::mutex> lock (m_grow_mutex);
	  bytes = m_bytes;
	}
	for (int i = 0; i < CNT_LAST; i++)
	  {
	    counters[i] = m_counters[i].load ();
	  }
      }

    private:
      rootdir m_root;
      std::atomic<std::size_t> m_num_sectors;
      std::atomic<rnode *> m_radix;
      std::mutex m_grow_mutex;
      std::size_t m_bytes;
      std::atomic<std::uint64_t> m_counters[CNT_LAST];

      static std::uint64_t make_key (short volid, std::int32_t pageid)
      {
	std::uint64_t sectid = static_cast<std::uint64_t> (static_cast<std::uint32_t> (pageid)) / PAGES_PER_SECTOR;
	return ((static_cast<std::uint64_t> (static_cast<std::uint16_t> (volid)) + 1) << 32) | sectid;
      }

      static claimed to_claimed (std::uint64_t key, std::size_t off, std::uint8_t code)
      {
	claimed c;
	c.volid = static_cast<short> ((key >> 32) - 1);
	c.pageid = static_cast<std::int32_t> ((key & 0xFFFFFFFFULL) * PAGES_PER_SECTOR + off);
	c.code = code;
	return c;
      }

      static std::uint8_t get_code (record *rec, std::size_t off)
      {
	return static_cast<std::uint8_t> ((rec->codes[off / 16].load () >> ((off % 16) * 4)) & 0xF);
      }

      // radix key: volid (15 bits) above sectid (25 bits) = 40 bits, consumed 6 bits per level from the top
      static std::uint64_t radix_key (std::uint64_t key)
      {
	std::uint64_t volid = (key >> 32) - 1;
	std::uint64_t sectid = key & 0x1FFFFFFULL;
	return (volid << 25) | sectid;
      }

      static std::size_t radix_slot (std::uint64_t rk, int level)
      {
	int shift = 6 * (RADIX_LEVELS - 1 - level);
	return static_cast<std::size_t> ((rk >> shift) & (FANOUT - 1));
      }

      void free_radix (rnode *r, int level)
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
	delete r;
      }

      group *group_at (std::size_t g)
      {
	middir *mid = m_root.children[g / (FANOUT * FANOUT)].load (std::memory_order_acquire);
	leafdir *leaf = mid->children[(g / FANOUT) % FANOUT].load (std::memory_order_acquire);
	return leaf->children[g % FANOUT].load (std::memory_order_acquire);
      }

      record *sector_at (std::size_t n)
      {
	return &group_at (n / FANOUT)->sectors[n % FANOUT];
      }

      std::uint64_t key_at (std::size_t n)
      {
	return group_at (n / FANOUT)->keys[n % FANOUT];
      }

      record *find (short volid, std::int32_t pageid, std::size_t &n)
      {
	std::uint64_t rk = radix_key (make_key (volid, pageid));
	rnode *r = m_radix.load (std::memory_order_acquire);
	for (int level = 0; r != nullptr && level < RADIX_LEVELS - 1; level++)
	  {
	    r = static_cast<rnode *> (r->slots[radix_slot (rk, level)].load (std::memory_order_acquire));
	  }
	if (r == nullptr)
	  {
	    return nullptr;
	  }
	std::uintptr_t v = reinterpret_cast<std::uintptr_t> (r->slots[radix_slot (rk, RADIX_LEVELS - 1)].load (
				   std::memory_order_acquire));
	if (v == 0)
	  {
	    return nullptr;
	  }
	n = static_cast<std::size_t> (v - 1);
	return sector_at (n);
      }

      record *get_or_add (short volid, std::int32_t pageid, std::size_t &n)
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
	if (s >= MAX_SECTORS)
	  {
	    return nullptr;
	  }
	std::size_t g = s / FANOUT;
	if (s % FANOUT == 0 && !add_group (g))
	  {
	    return nullptr;
	  }

	group *grp = group_at (g);
	std::uint64_t key = make_key (volid, pageid);
	grp->keys[s % FANOUT] = key;
	rec = &grp->sectors[s % FANOUT];
	for (int i = 0; i < 4; i++)
	  {
	    // pages of a new sector are not heap data pages until they are allocated or fed back
	    rec->codes[i].store (0xEEEEEEEEEEEEEEEEULL, std::memory_order_relaxed);
	  }
	rec->resident.store (0, std::memory_order_relaxed);
	rec->dirty.store (0, std::memory_order_relaxed);
	rec->version.store (0, std::memory_order_relaxed);

	if (!radix_insert (radix_key (key), s))
	  {
	    return nullptr;
	  }
	m_num_sectors.store (s + 1, std::memory_order_release);
	n = s;
	return rec;
      }

      // caller holds m_grow_mutex
      bool add_group (std::size_t g)
      {
	std::size_t a = g / (FANOUT * FANOUT), b = (g / FANOUT) % FANOUT, c = g % FANOUT;
	middir *mid = m_root.children[a].load (std::memory_order_relaxed);
	if (mid == nullptr)
	  {
	    mid = new (std::nothrow) middir ();
	    if (mid == nullptr)
	      {
		return false;
	      }
	    m_bytes += sizeof (middir);
	    m_root.children[a].store (mid, std::memory_order_release);
	  }
	leafdir *leaf = mid->children[b].load (std::memory_order_relaxed);
	if (leaf == nullptr)
	  {
	    leaf = new (std::nothrow) leafdir ();
	    if (leaf == nullptr)
	      {
		return false;
	      }
	    m_bytes += sizeof (leafdir);
	    mid->children[b].store (leaf, std::memory_order_release);
	  }
	group *grp = new (std::nothrow) group ();
	if (grp == nullptr)
	  {
	    return false;
	  }
	m_bytes += sizeof (group);
	leaf->children[c].store (grp, std::memory_order_release);
	return true;
      }

      // caller holds m_grow_mutex
      bool radix_insert (std::uint64_t rk, std::size_t number)
      {
	rnode *r = m_radix.load (std::memory_order_relaxed);
	if (r == nullptr)
	  {
	    r = new (std::nothrow) rnode ();
	    if (r == nullptr)
	      {
		return false;
	      }
	    m_bytes += sizeof (rnode);
	    m_radix.store (r, std::memory_order_release);
	  }
	for (int level = 0; level < RADIX_LEVELS - 1; level++)
	  {
	    std::size_t slot = radix_slot (rk, level);
	    rnode *next = static_cast<rnode *> (r->slots[slot].load (std::memory_order_relaxed));
	    if (next == nullptr)
	      {
		next = new (std::nothrow) rnode ();
		if (next == nullptr)
		  {
		    return false;
		  }
		m_bytes += sizeof (rnode);
		r->slots[slot].store (next, std::memory_order_release);
	      }
	    r = next;
	  }
	r->slots[radix_slot (rk, RADIX_LEVELS - 1)].store (reinterpret_cast<void *> (static_cast<std::uintptr_t> (number + 1)),
	    std::memory_order_release);
	return true;
      }

      void set_code (record *rec, std::size_t n, std::size_t off, std::uint8_t code)
      {
	std::size_t word = off / 16, shift = (off % 16) * 4;
	std::uint64_t expected = rec->codes[word].load (), desired;
	do
	  {
	    desired = (expected & ~(0xFULL << shift)) | (static_cast<std::uint64_t> (code) << shift);
	    if (desired == expected)
	      {
		break;
	      }
	  }
	while (!rec->codes[word].compare_exchange_weak (expected, desired));
	publish (rec, n, off);
      }

      static void set_bit (std::atomic<std::uint64_t> &w, std::uint64_t bit)
      {
	if ((w.load () & bit) == 0)
	  {
	    w.fetch_or (bit);
	  }
      }

      // version first, then hint bits bottom-up, so a search that clears a bit concurrently can detect the change
      void publish (record *rec, std::size_t n, std::size_t off)
      {
	rec->version.fetch_add (1);
	std::size_t g = n / FANOUT;
	group *grp = group_at (g);
	grp->persist_dirty.fetch_or (1ULL << (n % FANOUT));
	std::uint8_t code = get_code (rec, off);
	if (code < 1 || code > CODE_MAX_TIER || (rec->resident.load () & (1ULL << off)) != 0)
	  {
	    return;
	  }
	std::size_t t = code - 1;
	set_bit (grp->h.bits[t], 1ULL << (n % FANOUT));
	middir *mid = m_root.children[g / (FANOUT * FANOUT)].load (std::memory_order_acquire);
	leafdir *leaf = mid->children[(g / FANOUT) % FANOUT].load (std::memory_order_acquire);
	set_bit (leaf->h.bits[t], 1ULL << (g % FANOUT));
	set_bit (mid->h.bits[t], 1ULL << ((g / FANOUT) % FANOUT));
	set_bit (m_root.h.bits[t], 1ULL << (g / (FANOUT * FANOUT)));
      }

      static std::uint64_t mask_of (hint &h, std::uint8_t min_code)
      {
	std::uint64_t m = 0;
	for (std::uint8_t c = min_code; c <= CODE_MAX_TIER; c++)
	  {
	    m |= h.bits[c - 1].load ();
	  }
	return m;
      }

      // after a child yielded nothing for tiers >= min_code: clear its bit where the child has no hint, then
      // re-check the child so a concurrent bottom-up setter is never lost
      static void prune (hint &parent, hint &child, std::size_t i, std::uint8_t min_code)
      {
	std::uint64_t bit = 1ULL << i;
	for (std::uint8_t c = min_code; c <= CODE_MAX_TIER; c++)
	  {
	    if (child.bits[c - 1].load () == 0 && (parent.bits[c - 1].load () & bit) != 0)
	      {
		parent.bits[c - 1].fetch_and (~bit);
		if (child.bits[c - 1].load () != 0)
		  {
		    parent.bits[c - 1].fetch_or (bit);
		  }
	      }
	  }
      }

      template <typename Node, typename Child>
      void claim_node (Node &nd, int level, std::size_t base, std::uint8_t min_code, claimed *out, std::size_t max_count,
		       std::size_t &found)
      {
	std::uint64_t mask = mask_of (nd.h, min_code);
	while (mask != 0 && found < max_count)
	  {
	    std::size_t i = __builtin_ctzll (mask);
	    mask &= mask - 1;
	    Child *ch = nd.children[i].load (std::memory_order_acquire);
	    if (ch == nullptr)
	      {
		continue;
	      }
	    descend (*ch, level - 1, base * FANOUT + i, min_code, out, max_count, found);
	    if (found < max_count)
	      {
		prune (nd.h, ch->h, i, min_code);
	      }
	  }
      }

      void descend (middir &m, int level, std::size_t base, std::uint8_t min_code, claimed *out, std::size_t max_count,
		    std::size_t &found)
      {
	claim_node<middir, leafdir> (m, level, base, min_code, out, max_count, found);
      }

      void descend (leafdir &l, int level, std::size_t base, std::uint8_t min_code, claimed *out, std::size_t max_count,
		    std::size_t &found)
      {
	claim_node<leafdir, group> (l, level, base, min_code, out, max_count, found);
      }

      // group level: base is the group number
      void descend (group &grp, int, std::size_t g, std::uint8_t min_code, claimed *out, std::size_t max_count,
		    std::size_t &found)
      {
	std::size_t n_total = m_num_sectors.load (std::memory_order_acquire);
	std::uint64_t mask = mask_of (grp.h, min_code);
	while (mask != 0 && found < max_count)
	  {
	    std::size_t si = __builtin_ctzll (mask);
	    mask &= mask - 1;
	    std::size_t n = g * FANOUT + si;
	    if (n >= n_total)
	      {
		break;
	      }
	    record *rec = &grp.sectors[si];
	    std::uint64_t version = rec->version.load ();
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
		out[found++] = to_claimed (grp.keys[si], off, get_code (rec, off));
	      }
	    if (off == PAGES_PER_SECTOR)
	      {
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
		for (std::uint8_t c = min_code; c <= CODE_MAX_TIER; c++)
		  {
		    if (!left[c] && (grp.h.bits[c - 1].load () & sbit) != 0)
		      {
			grp.h.bits[c - 1].fetch_and (~sbit);
			if (rec->version.load () != version)
			  {
			    grp.h.bits[c - 1].fetch_or (sbit);
			  }
		      }
		  }
	      }
	  }
      }
  };
}

#endif // _BESTSPACE_FCMAP_HPP_
