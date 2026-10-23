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
// bestspace.cpp - bestspace in memory
//

#include "bestspace.hpp"
#include "log_impl.h"
#include "heap_file.h"
#include "slotted_page.h"
#include "error_manager.h"
#include "system_parameter.h"
#include "fault_injection.h"

#include <mutex>
#include <utility>
#include <cstdint>
#include <chrono>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

#define STATS_INC(id, value) \
  do \
    { \
      if (m_stats.enabled.load ()) \
	{ \
	  m_stats.id.fetch_add (value); \
	} \
    } \
  while (0)

static std::uint64_t
monotonic_seconds () noexcept
{
  return static_cast<std::uint64_t> (std::chrono::duration_cast<std::chrono::seconds>
				     (std::chrono::steady_clock::now ().time_since_epoch ()).count ());
}


static int
wait_for_shard_allocation (THREAD_ENTRY *thread_p, void *args)
{
  std::size_t *retry;
  bool continue_check;

  retry = static_cast<std::size_t *> (args);
  assert (retry != nullptr);

  if (*retry < 20)
    {
      std::this_thread::yield ();
    }
  else
    {
      std::this_thread::sleep_for (std::chrono::microseconds (10));
    }
  (*retry)++;

  if (logtb_is_interrupted (thread_p, true, &continue_check))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INTERRUPTED, 0);
      return ER_INTERRUPTED;
    }

  return NO_ERROR;
}

namespace cubstorage
{
  //////////////////////////////////////////////////////////////////////////
  // base class
  //////////////////////////////////////////////////////////////////////////

  bestspace::bitmap::bitmap () noexcept
    : m_bits (0)
  {
  }

  bool
  bestspace::bitmap::empty ()
  {
    return m_bits == 0;
  }

  void
  bestspace::bitmap::set (std::size_t index)
  {
    assert (index < BITS_PER_BYTE);

    m_bits |= (0x1 << index);
  }

  void
  bestspace::bitmap::clear (std::size_t index)
  {
    assert (index < BITS_PER_BYTE);

    m_bits &= ~ (0x1 << index);
  }

  std::size_t
  bestspace::bitmap::find (std::array<std::size_t, BITS_PER_BYTE> &pos, std::size_t length)
  {
    std::size_t size;
    std::size_t i;

    size = 0;
    for (i = 0; i < length; i++)
      {
	if (m_bits & (0x1 << i))
	  {
	    pos[size++] = i;
	  }
      }

    return size;
  }

  bestspace::L1::L1 () noexcept
    : m_freespace (0)
    , m_volid (NULL_VOLID)
    , m_pageid (NULL_PAGEID)
  {
    static_assert (offsetof (L1, m_freespace) == offsetof (bestspace_entry, freespace), "offset must be same");
    static_assert (offsetof (L1, m_volid) == offsetof (bestspace_entry, volid), "offset must be same");
    static_assert (offsetof (L1, m_pageid) == offsetof (bestspace_entry, pageid), "offset must be same");
  }

  std::uint16_t
  bestspace::L1::get_freespace ()
  {
    return m_freespace;
  }

  void
  bestspace::L1::set_freespace (std::uint16_t size)
  {
    m_freespace = size;
  }

  VPID
  bestspace::L1::get_vpid ()
  {
    return { m_pageid, m_volid };
  }

  void
  bestspace::L1::set_vpid (VPID vpid)
  {
    m_pageid = vpid.pageid;
    m_volid = vpid.volid;
  }

  bestspace::L2::L2 () noexcept
    : m_freespace ()
  {
  }

  std::size_t
  bestspace::L2::find (tier minimum, std::array<std::size_t, BITS_PER_BYTE> &pos)
  {
    assert (minimum >= tier::FS1 && minimum <= tier::FS8);

    return m_freespace[static_cast<std::size_t> (minimum)].find (pos);
  }

  std::size_t
  bestspace::L2::collect (std::array<tier, BITS_PER_BYTE> &tiers)
  {
    std::size_t size;
    tier i;

    size = 0;
    for (i = tier::FS1; i <= tier::FS8; i++)
      {
	if (!m_freespace[static_cast<std::size_t> (i)].empty ())
	  {
	    tiers[size++] = i;
	  }
      }

    return size;
  }

  bool
  bestspace::L2::empty (tier fs)
  {
    assert (fs > tier::FS0);

    return m_freespace[static_cast<std::size_t> (fs)].empty ();
  }

  void
  bestspace::L2::clear ()
  {
    uint64_t val;

    val = 0;
    std::memcpy (static_cast<void *> (m_freespace.data ()), &val, sizeof (uint64_t));
  }

  void
  bestspace::L2::clear (std::size_t index)
  {
    uint64_t val;

    assert (index < BITS_PER_BYTE);

    std::memcpy (&val, m_freespace.data (), sizeof (uint64_t));
    val &= ~ (0x0101010101010101ULL << index);
    std::memcpy (static_cast<void *> (m_freespace.data ()), &val, sizeof (uint64_t));
  }

  void
  bestspace::L2::set (tier fs, std::size_t index)
  {
    assert (fs > tier::FS0);
    assert (index < BITS_PER_BYTE);

    m_freespace[static_cast<std::size_t> (fs)].set (index);
  }

  bestspace::L3::L3 () noexcept
    : m_freespace ()
  {
  }

  std::size_t
  bestspace::L3::find (tier minimum, std::array<std::size_t, BITS_PER_BYTE> &pos)
  {
    assert (minimum >= tier::FS1 && minimum <= tier::FS8);

    return m_freespace[static_cast<std::size_t> (minimum)].find (pos);
  }

  void
  bestspace::L3::clear ()
  {
    uint64_t val;

    val = 0;
    std::memcpy (static_cast<void *> (m_freespace.data ()), &val, sizeof (uint64_t));
  }

  void
  bestspace::L3::clear (std::size_t index)
  {
    uint64_t val;

    assert (index < BITS_PER_BYTE);

    std::memcpy (&val, m_freespace.data (), sizeof (uint64_t));
    val &= ~ (0x0101010101010101ULL << index);
    std::memcpy (static_cast<void *> (m_freespace.data ()), &val, sizeof (uint64_t));
  }

  void
  bestspace::L3::set (tier fs, std::size_t index)
  {
    assert (fs > tier::FS0);
    assert (index < BITS_PER_BYTE);

    m_freespace[static_cast<std::size_t> (fs)].set (index);
  }

  bestspace::shard::shard (bestspace &parent) noexcept
    : m_allocating (false)
    , m_L3 ()
    , m_L2 ()
    , m_L1 ()
    , m_parent (parent)
    , m_num_pages (0)
    , m_recs_num (0)
    , m_recs_sumlen (0)
    , m_stats { false, 0, 0, 0, 0, 0, 0, 0 }
  {
  }

  void
  bestspace::shard::reset (const bestspace_entry entries[ENTRIES_PER_SHARD])
  {
    std::array<tier, BITS_PER_BYTE> tiers;
    std::size_t length;
    std::size_t i, j;
    tier fs;
    L3 l3;
    L2 l2;
    L1 l1;

    // L1
    for (i = 0; i < ENTRIES_PER_SHARD; i++)
      {
	l1.set_vpid ({ entries[i].pageid, entries[i].volid });
	l1.set_freespace (entries[i].freespace);
	m_L1[i].store (l1);
      }
    // L2
    for (i = 0; i < L3_FANOUT; i++)
      {
	l2.clear ();
	for (j = 0; j < L2_FANOUT; j++)
	  {
	    fs = size_to_tier (entries[i * L2_FANOUT + j].freespace);
	    if (fs > tier::FS0)
	      {
		l2.set (fs, j);
	      }
	  }
	m_L2[i].store (l2);
      }
    // L3
    l3.clear ();
    for (i = 0; i < L3_FANOUT; i++)
      {
	l2 = m_L2[i].load ();
	length = l2.collect (tiers);
	for (j = 0; j < length; j++)
	  {
	    l3.set (tiers[j], i);
	  }
      }
    m_L3.store (l3);
  }

  bestspace::status
  bestspace::shard::find (OID *class_oid, HFID *hfid, std::uint16_t needed_size, std::uint16_t consume_size,
			  std::size_t bias, PGBUF_WATCHER &page_watcher)
  {
    status error;
    tier minimum;

    STATS_INC (request, 1);

    // FS0 is not indexed by L2/L3. search the same tier and let L1_find perform the exact size check.
    minimum = size_to_tier (needed_size);
    if (minimum == tier::FS0)
      {
	minimum = tier::FS1;
      }

    error = L3_find (class_oid, minimum, needed_size, consume_size, bias, page_watcher);
    if (error == status::FOUND || error == status::FAILURE)
      {
	return error;
      }

    assert (error == status::NOT_FOUND || error == status::CONTENDED);

    return allocate (class_oid, hfid, needed_size, consume_size, page_watcher);
  }

  void
  bestspace::shard::add_estimates (int num_pages, std::uint64_t recs_num, std::uint64_t recs_sumlen)
  {
    m_num_pages.fetch_add (num_pages);
    m_recs_num.fetch_add (recs_num);
    m_recs_sumlen.fetch_add (recs_sumlen);
  }

  void
  bestspace::shard::subtract_estimates (int num_pages, std::uint64_t recs_num, std::uint64_t recs_sumlen)
  {
    m_num_pages.fetch_sub (num_pages);
    m_recs_num.fetch_sub (recs_num);
    m_recs_sumlen.fetch_sub (recs_sumlen);
  }

  void
  bestspace::shard::get_estimates (int &num_pages, std::uint64_t &recs_num, std::uint64_t &recs_sumlen)
  {
    num_pages += m_num_pages.load ();
    recs_num += m_recs_num.load ();
    recs_sumlen += m_recs_sumlen.load ();
  }

  void
  bestspace::shard::get_stats (std::uint32_t &request, std::uint32_t &advanced_shard, std::uint32_t &fetch_L3,
			       std::uint32_t &fetch_L2, std::uint32_t &fetch_L1, std::uint32_t &found, std::uint32_t &allocated)
  {
    request += m_stats.request.load ();
    advanced_shard += m_stats.advance_shard.load ();
    fetch_L3 += m_stats.fetch_L3.load ();
    fetch_L2 += m_stats.fetch_L2.load ();
    fetch_L1 += m_stats.fetch_L1.load ();
    found += m_stats.found.load ();
    allocated += m_stats.allocated.load ();
  }

  void
  bestspace::shard::to_entries (bestspace_entry *entries)
  {
    std::size_t i;
    L1 l1;

    for (i = 0; i < ENTRIES_PER_SHARD; i++)
      {
	l1 = m_L1[i].load ();
	std::memcpy (&entries[i], &l1, sizeof (bestspace_entry));
      }
  }

  bestspace::status
  bestspace::shard::L3_find (OID *class_oid, tier minimum, std::uint16_t needed_size, std::uint16_t consume_size,
			     std::size_t bias, PGBUF_WATCHER &page_watcher)
  {
    std::array<std::size_t, BITS_PER_BYTE> pos;
    std::size_t length, i;
    bool contended = false;
    status error;
    L3 l3;

    assert (minimum > tier::FS0);

    for (; minimum <= tier::FS8; minimum++)
      {
	l3 = m_L3.load ();

	STATS_INC (fetch_L3, 1);

	length = l3.find (minimum, pos);
	for (i = 0; i < length; i++)
	  {
	    error = L2_find (class_oid, minimum, needed_size, consume_size, pos[ (i + bias) % length], bias,
			     page_watcher);
	    if (error == status::FOUND || error == status::FAILURE)
	      {
		return error;
	      }
	    if (error == status::CONTENDED)
	      {
		contended = true;
	      }
	  }
      }

    return contended ? status::CONTENDED : status::NOT_FOUND;
  }

  void
  bestspace::shard::L3_update (std::size_t l2_index)
  {
    std::array<tier, BITS_PER_BYTE> tiers;
    std::size_t length, i;
    L3 expected, desired;
    L2 l2;

    while (true)
      {
	expected = m_L3.load ();
	do
	  {
	    desired = expected;

	    l2 = m_L2[l2_index].load ();
	    length = l2.collect (tiers);
	    desired.clear (l2_index);
	    for (i = 0; i < length; i++)
	      {
		desired.set (tiers[i], l2_index);
	      }

	    if (desired == expected)
	      {
		return;
	      }
	  }
	while (!m_L3.compare_exchange_strong (expected, desired));

	if (l2 == m_L2[l2_index].load ())
	  {
	    break;
	  }
      }
  }

  bestspace::status
  bestspace::shard::L2_find (OID *class_oid, tier minimum, std::uint16_t needed_size, std::uint16_t consume_size,
			     std::size_t l2_index, std::size_t bias, PGBUF_WATCHER &page_watcher)
  {
    std::array<std::size_t, BITS_PER_BYTE> pos;
    std::size_t length, i;
    status error;
    bool contended = false;
    L2 l2;

    assert (minimum > tier::FS0);

    for (; minimum <= tier::FS8; minimum++)
      {
	l2 = m_L2[l2_index].load ();

	STATS_INC (fetch_L2, 1);

	length = l2.find (minimum, pos);
	for (i = 0; i < length; i++)
	  {
	    error = L1_find (class_oid, needed_size, consume_size, l2_index, pos[ (i + bias) % length], page_watcher);
	    if (error == status::FOUND || error == status::FAILURE)
	      {
		return error;
	      }
	    if (error == status::CONTENDED)
	      {
		contended = true;
	      }
	  }
      }

    return contended ? status::CONTENDED : status::NOT_FOUND;
  }

  void
  bestspace::shard::L2_update (std::size_t l2_index, std::size_t l1_index)
  {
    tier tier_to, tier_now;
    L2 expected, desired;
    L1 l1;

    while (true)
      {
	expected = m_L2[l2_index].load ();
	do
	  {
	    desired = expected;
	    desired.clear (l1_index);

	    l1 = m_L1[l2_index * L2_FANOUT + l1_index].load ();
	    tier_to = size_to_tier (l1.get_freespace ());
	    if (tier_to > tier::FS0)
	      {
		desired.set (tier_to, l1_index);
	      }

	    if (desired == expected)
	      {
		return;
	      }
	  }
	while (!m_L2[l2_index].compare_exchange_strong (expected, desired));

	tier_now = size_to_tier (m_L1[l2_index * L2_FANOUT + l1_index].load ().get_freespace ());
	if (tier_now == tier_to)
	  {
	    break;
	  }
      }

    L3_update (l2_index);
  }

  bestspace::status
  bestspace::shard::L1_find (OID *class_oid, std::uint16_t needed_size, std::uint16_t consume_size,
			     std::size_t l2_index, std::size_t l1_index, PGBUF_WATCHER &page_watcher)
  {
    cubthread::entry *thread_p = thread_get_thread_entry_info ();
    std::size_t freespace;
    VPID vpid, old_vpid;
    L1 expected, desired;
    OID page_class_oid;
    status error;

    STATS_INC (fetch_L1, 1);

    assert (PGBUF_IS_CLEAN_WATCHER (&page_watcher));

    // first, check the recorded free space
    expected = m_L1[l2_index * L2_FANOUT + l1_index].load ();
    old_vpid = expected.get_vpid ();
    if (VPID_ISNULL (&old_vpid))
      {
	return status::NOT_FOUND;
      }
    if (expected.get_freespace () < needed_size)
      {
	// there is no enough space
	return status::NOT_FOUND;
      }

    // now, fix a page to check the actual free space
    error = L1_fix (l2_index, l1_index, expected, old_vpid, page_watcher);
    if (error != status::SUCCESS)
      {
	return error;
      }

    // is this page still belongs to the class (class_oid) ?
    if (pgbuf_get_page_ptype (thread_p, page_watcher.pgptr) != PAGE_HEAP
	|| heap_page_is_not_in_heap (thread_p, page_watcher.pgptr)
	|| heap_get_class_oid_from_page (thread_p, page_watcher.pgptr, &page_class_oid) != NO_ERROR
	|| !OID_EQ (&page_class_oid, class_oid))
      {
	L1_remove (l2_index, l1_index, expected);
	pgbuf_ordered_unfix (thread_p, &page_watcher);
	return status::NOT_FOUND;
      }

    // L1 might be changed
    expected = m_L1[l2_index * L2_FANOUT + l1_index].load ();
    // newest
    vpid = expected.get_vpid ();
    // store the old and get the actual free space
    freespace = spage_max_space_for_new_record (thread_p, page_watcher.pgptr);
    if (freespace < needed_size)
      {
	// there is no enough space and the free space information is wrong
	if (VPID_EQ (&vpid, &old_vpid))
	  {
	    desired = expected;
	    desired.set_freespace (freespace);

	    // and I'm the only one that can modify this L1
	    if (m_L1[l2_index * L2_FANOUT + l1_index].compare_exchange_strong (expected, desired))
	      {
		L2_update (l2_index, l1_index);
	      }
	  }
	pgbuf_ordered_unfix (thread_p, &page_watcher);
	return status::NOT_FOUND;
      }

    // there is enough space
    if (VPID_EQ (&vpid, &old_vpid))
      {
	desired = expected;
	desired.set_freespace (freespace - consume_size);

	// and I'm the only one that can modify this L1
	if (m_L1[l2_index * L2_FANOUT + l1_index].compare_exchange_strong (expected, desired))
	  {
	    L2_update (l2_index, l1_index);
	  }
      }

    STATS_INC (found, 1);

    return status::FOUND;
  }

  bestspace::status
  bestspace::shard::L1_fix (std::size_t l2_index, std::size_t l1_index, L1 l1, VPID vpid, PGBUF_WATCHER &page_watcher)
  {
    cubthread::entry *thread_p;
    int wait_msecs;
    int error_code;

    thread_p = thread_get_thread_entry_info ();
    wait_msecs = logtb_set_thread_wait_msecs (thread_p, LK_FORCE_ZERO_WAIT);
    error_code = pgbuf_ordered_fix (thread_p, &vpid, OLD_PAGE_MAYBE_DEALLOCATED, PGBUF_LATCH_WRITE, &page_watcher);
    (void) logtb_set_thread_wait_msecs (thread_p, wait_msecs);
    if (error_code != NO_ERROR)
      {
	if (error_code == ER_LK_PAGE_TIMEOUT)
	  {
	    er_clear ();
	    return status::CONTENDED;
	  }
	if (error_code == ER_PB_BAD_PAGEID || er_errid () == ER_PB_BAD_PAGEID)
	  {
	    L1_remove (l2_index, l1_index, l1);
	    er_clear ();
	    return status::NOT_FOUND;
	  }
	if (er_errid () == NO_ERROR)
	  {
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FAILED, 0);
	  }
	return status::FAILURE;
      }
    return status::SUCCESS;
  }

  void
  bestspace::shard::L1_remove (std::size_t l2_index, std::size_t l1_index, L1 expected)
  {
    VPID removed_vpid;
    L1 desired;

    desired.set_freespace (0);
    desired.set_vpid (vpid_Null_vpid);
    if (m_L1[l2_index * L2_FANOUT + l1_index].compare_exchange_strong (expected, desired))
      {
	L2_update (l2_index, l1_index);

	// the page is not a data page of this heap any more (deallocated, removed, another class). it leaves the slot
	// without a release, so drop it from the map too; it would stay resident there for good otherwise
	removed_vpid = expected.get_vpid ();
	m_parent.m_map->forget (removed_vpid.volid, removed_vpid.pageid);
      }
  }

  bestspace::status
  bestspace::shard::allocate_mark ()
  {
    bool expected;

    expected = m_allocating.load ();
    do
      {
	if (expected)
	  {
	    return status::ALLOCATING;
	  }
      }
    while (!m_allocating.compare_exchange_strong (expected, true));

    return status::SUCCESS;
  }

  void
  bestspace::shard::allocate_unmark ()
  {
    assert (m_allocating.load ());

    m_allocating.store (false);
  }

  // Victims are the ALLOC_BATCH_SIZE slots a refill puts its pages into, chosen in this order:
  //   1. slots whose page was fed back (vacuum, delete, rollback) with more room than the slot records. No one sees
  //      that room otherwise: the map does not hand out a resident page, and the slot looks full to this shard;
  //   2. empty slots;
  //   3. the first slots, by number, that cannot serve this request (recorded free space below needed_size);
  //   4. if fewer slots than that qualify, the slots with the least free space.
  // Evicting a page loses nothing: the map keeps the code of every page and hands the page out again to a request it
  // fits. Taking the first slots keeps the pages being filled at the front, where L3_find () and L2_find () start:
  // they try every slot of the request's tier that is too small before they reach one that fits, so a page being
  // filled in a high slot makes every insert walk the full slots below it.
  // Returns how many victims, at the front, were fed back.
  std::size_t
  bestspace::shard::allocate_pick_victims (std::array<std::pair<std::uint16_t, std::uint16_t>, ALLOC_BATCH_SIZE> &victims,
      std::uint16_t needed_size)
  {
    constexpr std::size_t NUM_SLOTS = L3_FANOUT * L2_FANOUT;
    std::array<bestspace_map_page, NUM_SLOTS> pages;
    std::array<std::uint16_t, NUM_SLOTS> freespace;
    std::uint64_t dirty, chosen;
    std::size_t num_dirty, num_front, num_victims, pos, slot;
    int pass;
    VPID vpid;
    L1 l1;

    static_assert (NUM_SLOTS <= 64, "the dirty mask has one bit per slot");

    victims.fill (std::make_pair (std::numeric_limits<std::uint16_t>::max (), std::numeric_limits<std::uint16_t>::max ()));

    for (slot = 0; slot < NUM_SLOTS; slot++)
      {
	l1 = m_L1[slot].load ();
	vpid = l1.get_vpid ();
	freespace[slot] = l1.get_freespace ();
	pages[slot].volid = vpid.volid;
	pages[slot].pageid = VPID_ISNULL (&vpid) ? -1 : vpid.pageid;
	pages[slot].code = code_of (freespace[slot]);
      }
    dirty = m_parent.m_map->resident_dirty_above (pages.data (), NUM_SLOTS);

    num_dirty = 0;
    chosen = 0;
    for (slot = 0; slot < NUM_SLOTS && dirty != 0 && num_dirty < ALLOC_BATCH_SIZE; slot++)
      {
	if ((dirty & (1ULL << slot)) != 0)
	  {
	    victims[num_dirty++] = std::make_pair (slot, freespace[slot]);
	    chosen |= 1ULL << slot;
	  }
      }

    // then empty slots, then the first slots that cannot serve this request
    num_front = num_dirty;
    for (pass = 0; pass < 2; pass++)
      {
	for (slot = 0; slot < NUM_SLOTS && num_front < ALLOC_BATCH_SIZE; slot++)
	  {
	    if ((chosen & (1ULL << slot)) == 0 && (pass == 0 ? pages[slot].pageid < 0 : freespace[slot] < needed_size))
	      {
		victims[num_front++] = std::make_pair (slot, freespace[slot]);
		chosen |= 1ULL << slot;
	      }
	  }
      }

    // and, if those were not enough, the least free space among the rest (the request fits each of them), kept sorted
    // after the victims chosen above
    num_victims = num_front;
    for (slot = 0; slot < NUM_SLOTS && num_front < ALLOC_BATCH_SIZE; slot++)
      {
	if ((chosen & (1ULL << slot)) != 0)
	  {
	    continue;
	  }
	if (num_victims == ALLOC_BATCH_SIZE && freespace[slot] >= victims[ALLOC_BATCH_SIZE - 1].second)
	  {
	    continue;
	  }

	pos = num_victims < ALLOC_BATCH_SIZE ? num_victims++ : ALLOC_BATCH_SIZE - 1;
	while (pos > num_front && freespace[slot] < victims[pos - 1].second)
	  {
	    victims[pos] = victims[pos - 1];
	    pos--;
	  }

	victims[pos] = std::make_pair (slot, freespace[slot]);
      }
    assert (num_victims == ALLOC_BATCH_SIZE);

    return num_dirty;
  }

  // empty the first count victim slots now and write their pages back to the map, which keeps the fed-back code: the
  // claim of this same refill can take such a page again, now with its real tier
  void
  bestspace::shard::allocate_release_victims (std::size_t count,
      std::array<std::pair<std::uint16_t, std::uint16_t>, ALLOC_BATCH_SIZE> &victims)
  {
    std::size_t i, slot;
    VPID vpid;
    L1 expected, desired;

    desired.set_freespace (0);
    desired.set_vpid (vpid_Null_vpid);
    for (i = 0; i < count; i++)
      {
	slot = victims[i].first;
	// the slot is empty now: allocate_install () fills it without evicting anything
	victims[i].second = 0;

	expected = m_L1[slot].load ();
	vpid = expected.get_vpid ();
	// only this allocating thread puts a page into a slot; others change its free space or empty it (L1_remove)
	while (!VPID_ISNULL (&vpid) && !m_L1[slot].compare_exchange_strong (expected, desired))
	  {
	    vpid = expected.get_vpid ();
	  }
	if (VPID_ISNULL (&vpid))
	  {
	    continue;
	  }
	L2_update (slot / L2_FANOUT, slot % L2_FANOUT);
	m_parent.m_map->release (vpid.volid, vpid.pageid, code_of (expected.get_freespace ()), false);
      }
  }

  bestspace::status
  bestspace::shard::allocate_verify_actual_space (OID *class_oid, std::uint16_t needed_size, bestspace_entry &candidate,
      bool &valid, PGBUF_WATCHER &page_watcher)
  {
    cubthread::entry *thread_p = thread_get_thread_entry_info ();
    OID page_class_oid;
    VPID vpid;
    int error;

    assert (class_oid != nullptr);
    assert (PGBUF_IS_CLEAN_WATCHER (&page_watcher));

    valid = true;
    vpid.volid = candidate.volid;
    vpid.pageid = candidate.pageid;

    error = pgbuf_ordered_fix (thread_p, &vpid, OLD_PAGE_MAYBE_DEALLOCATED, PGBUF_LATCH_WRITE, &page_watcher);
    if (error != NO_ERROR)
      {
	if (error == ER_PB_BAD_PAGEID || er_errid () == ER_PB_BAD_PAGEID)
	  {
	    valid = false;
	    er_clear ();
	    return status::NOT_FOUND;
	  }
	if (er_errid () == NO_ERROR)
	  {
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FAILED, 0);
	  }
	return status::FAILURE;
      }

    if (pgbuf_get_page_ptype (thread_p, page_watcher.pgptr) != PAGE_HEAP
	|| heap_page_is_not_in_heap (thread_p, page_watcher.pgptr))
      {
	valid = false;
	pgbuf_ordered_unfix (thread_p, &page_watcher);
	return status::NOT_FOUND;
      }

    error = heap_get_class_oid_from_page (thread_p, page_watcher.pgptr, &page_class_oid);
    if (error != NO_ERROR)
      {
	valid = false;
	pgbuf_ordered_unfix (thread_p, &page_watcher);
	return status::FAILURE;
      }

    if (!OID_EQ (&page_class_oid, class_oid))
      {
	valid = false;
	pgbuf_ordered_unfix (thread_p, &page_watcher);
	return status::NOT_FOUND;
      }

    candidate.freespace = spage_max_space_for_new_record (thread_p, page_watcher.pgptr);
    if (candidate.freespace < needed_size)
      {
	pgbuf_ordered_unfix (thread_p, &page_watcher);
	return status::NOT_FOUND;
      }
    return status::FOUND;
  }

  int
  bestspace::shard::allocate_new_pages (HFID *hfid, std::size_t num_candidates,
					std::array<bestspace_entry, ALLOC_BATCH_SIZE> &candidates, PGBUF_WATCHER &page_watcher)
  {
    std::array<VPID, ALLOC_BATCH_SIZE> vpids;
    cubthread::entry *thread_p = thread_get_thread_entry_info ();
    int freespace;
    int error;
    int i;

    // the map registers the new pages under the heap header latch: let it allocate what a new sector needs first
    m_parent.m_map->reserve ();
    error = heap_alloc_new_pages (thread_p, hfid, ALLOC_BATCH_SIZE - num_candidates, vpids.data (), &page_watcher,
				  m_parent.m_map);
    if (error != NO_ERROR)
      {
	return error;
      }
    m_num_pages.fetch_add (ALLOC_BATCH_SIZE - num_candidates);

    STATS_INC (allocated, ALLOC_BATCH_SIZE - num_candidates);

    freespace = spage_max_space_for_new_record (thread_p, page_watcher.pgptr);
    // page_watcher.pgptr fixes the page pointer of the last candidates
    for (i = ALLOC_BATCH_SIZE - 1; i >= static_cast<int> (num_candidates); i--)
      {
	candidates[i].freespace = freespace;
	candidates[i].volid = vpids[ALLOC_BATCH_SIZE - 1 - i].volid;
	candidates[i].pageid = vpids[ALLOC_BATCH_SIZE - 1 - i].pageid;
      }
    return NO_ERROR;
  }

  void
  bestspace::shard::allocate_install (std::array<std::pair<std::uint16_t, std::uint16_t>, ALLOC_BATCH_SIZE> &victims,
				      std::array<bestspace_entry, ALLOC_BATCH_SIZE> &candidates, std::size_t num_candidates)
  {
    bestspace_map *map = m_parent.m_map;
    std::size_t i, slot;
    VPID old_vpid;
    L1 old, l1;

    for (i = 0; i < num_candidates; i++)
      {
	slot = victims[i].first;
	if (i == num_candidates - 1 && candidates[i].freespace <= victims[i].second)
	  {
	    // the returned page has less room than the victim: keep the victim, the page stays in the map
	    map->release (candidates[i].volid, candidates[i].pageid, code_of (candidates[i].freespace), true);
	    continue;
	  }

	l1.set_freespace (candidates[i].freespace);
	l1.set_vpid ({ candidates[i].pageid, candidates[i].volid });
	// one exchange: an L1_remove of the evicted page either empties the slot first (nothing to release here) or fails
	// its CAS (the page is not forgotten); with a load and a store it could forget the page and see it released
	old = m_L1[slot].exchange (l1);

	L2_update (slot / L2_FANOUT, slot % L2_FANOUT);

	// write the evicted page back to the map so it is never forgotten
	old_vpid = old.get_vpid ();
	if (!VPID_ISNULL (&old_vpid))
	  {
	    map->release (old_vpid.volid, old_vpid.pageid, code_of (old.get_freespace ()), false);
	  }
      }
  }

  bestspace::status
  bestspace::shard::allocate (OID *class_oid, HFID *hfid, std::uint16_t needed_size, std::uint16_t consume_size,
			      PGBUF_WATCHER &page_watcher)
  {
    std::array<std::pair<std::uint16_t, std::uint16_t>, ALLOC_BATCH_SIZE> victims; // index, freespace
    std::array<bestspace_entry, ALLOC_BATCH_SIZE> claimed;
    std::array<bestspace_entry, ALLOC_BATCH_SIZE> candidates;
    bestspace_map_page picked[ALLOC_BATCH_SIZE];
    bestspace_map *map = m_parent.m_map;
    std::size_t num_claimed, num_candidates, num_dirty, fit_index, i, j;
    std::uint8_t min_code;
    bool found, valid;
    status result;
    int error;

    if (allocate_mark () != status::SUCCESS)
      {
	STATS_INC (advance_shard, 1);
	return status::ALLOCATING;
      }

    // pick four slots to replace (see allocate_pick_victims): pages fed back with more room than their slot records,
    // empty slots, then the first slots that cannot serve this request. the fed-back pages go back to the map before
    // the claim, which can hand them out again with their tier
    num_dirty = allocate_pick_victims (victims, needed_size);
    allocate_release_victims (num_dirty, victims);

    // claim pages whose tier is above the request's: their lower bound fits it when the map is accurate. a page of
    // the same tier may or may not fit (tiers are 8~15% wide), so it is left for smaller requests; only a request in
    // the top tier has nothing above it and tries its own tier.
    min_code = code_of (needed_size);
    num_claimed = map->claim (min_code < bestspace_map::CODE_MAX_TIER ? min_code + 1 : bestspace_map::CODE_MAX_TIER,
			      picked, ALLOC_BATCH_SIZE);
    if (num_claimed > 0)
      {
	(void) FI_TEST (thread_get_thread_entry_info (), FI_TEST_BESTSPACE_DELAY_CLAIM, 0);
      }
    for (i = 0; i < num_claimed; i++)
      {
	claimed[i].volid = picked[i].volid;
	claimed[i].pageid = picked[i].pageid;
	claimed[i].freespace = optimistic_freespace (picked[i].code);
      }

    // verify the claimed pages in order until one really fits; the rest become unverified replacements.
    found = false;
    fit_index = 0;
    num_candidates = 0;
    for (i = 0; i < num_claimed; i++)
      {
	if (found)
	  {
	    candidates[num_candidates++] = claimed[i];
	    continue;
	  }

	result = allocate_verify_actual_space (class_oid, needed_size, claimed[i], valid, page_watcher);
	if (result == status::FOUND)
	  {
	    found = true;
	    fit_index = num_candidates;
	    candidates[num_candidates++] = claimed[i];
	    map->count (bestspace_map::CNT_REUSED);
	    continue;
	  }
	if (result == status::FAILURE)
	  {
	    if (valid)
	      {
		// the page could not be fixed (interrupted, I/O error): it may still have the space
		map->release (claimed[i].volid, claimed[i].pageid, code_of (claimed[i].freespace), false);
	      }
	    else
	      {
		// the class of the page could not be read: drop the page, or every insert that claims it fails the same way
		map->forget (claimed[i].volid, claimed[i].pageid);
	      }
	    for (j = i + 1; j < num_claimed; j++)
	      {
		map->release (claimed[j].volid, claimed[j].pageid, code_of (claimed[j].freespace), false);
	      }
	    allocate_unmark ();
	    return status::FAILURE;
	  }

	assert (result == status::NOT_FOUND);
	map->count (bestspace_map::CNT_VERIFY_FAIL);
	if (valid)
	  {
	    // the page was measured: record its exact tier
	    map->release (claimed[i].volid, claimed[i].pageid, code_of (claimed[i].freespace), true);
	  }
	else
	  {
	    // not a page of this heap any more (deallocated, removed, another class)
	    map->forget (claimed[i].volid, claimed[i].pageid);
	  }
      }

    if (found)
      {
	// the page returned to the caller is always stored at the last position.
	std::swap (candidates[fit_index], candidates[num_candidates - 1]);
      }
    else
      {
	assert (num_candidates == 0);

	// nothing in the map fits: extend the heap by one batch
	error = allocate_new_pages (hfid, 0, candidates, page_watcher);
	if (error != NO_ERROR)
	  {
	    allocate_unmark ();
	    return status::FAILURE;
	  }
	num_candidates = ALLOC_BATCH_SIZE;
	map->count (bestspace_map::CNT_ALLOC_BATCH);
	// only the latched page was measured; the others are new and empty
	for (i = 0; i < ALLOC_BATCH_SIZE - 1; i++)
	  {
	    candidates[i].freespace = optimistic_freespace (bestspace_map::CODE_MAX_TIER);
	  }

	if (candidates[ALLOC_BATCH_SIZE - 1].freespace < needed_size)
	  {
	    // heap_alloc_new_pages links the pages into the heap before it latches the first one again, so another
	    // inserter that reached it through an old hint may have used it meanwhile. keep the batch in the hot cache
	    // and serve the request from it instead.
	    cubthread::entry *thread_p = thread_get_thread_entry_info ();
	    tier minimum;

	    map->count (bestspace_map::CNT_VERIFY_FAIL);
	    pgbuf_ordered_unfix (thread_p, &page_watcher);
	    allocate_install (victims, candidates, ALLOC_BATCH_SIZE);
	    allocate_unmark ();

	    minimum = size_to_tier (needed_size);
	    if (minimum == tier::FS0)
	      {
		minimum = tier::FS1;
	      }
	    result = L3_find (class_oid, minimum, needed_size, consume_size, 0, page_watcher);
	    if (result == status::FOUND || result == status::FAILURE)
	      {
		return result;
	      }
	    // nothing usable right now: let the caller retry
	    return status::ALLOCATING;
	  }
      }

    // reserve the page
    candidates[num_candidates - 1].freespace -= consume_size;

    allocate_install (victims, candidates, num_candidates);

    allocate_unmark ();
    return status::FOUND;
  }

  bestspace::bestspace (std::size_t shard_count, int num_pages, std::uint64_t recs_num, std::uint64_t recs_sumlen,
			std::uint16_t unfill_space, bestspace_map *map)
    : m_shards ()
    , m_map (map)
    , m_distributed_insert (prm_get_bool_value (PRM_ID_BESTSPACE_DISTRIBUTED_INSERT))
    , m_unfill_space (unfill_space)
    , m_num_pages (num_pages)
    , m_recs_num (recs_num)
    , m_recs_sumlen (recs_sumlen)
  {
    assert (shard_count > 0);
    assert (map != nullptr);

    m_last_updated.store (monotonic_seconds ());
    for (std::size_t i = 0; i < shard_count; i++)
      {
	m_shards.emplace_back (*this);
      }
  }

  bestspace::~bestspace ()
  {
    bestspace_map::destroy (m_map);
  }

  bestspace_map *
  bestspace::get_map ()
  {
    return m_map;
  }

  void
  bestspace::reset (const bestspace_entry *entries, std::size_t num_entries)
  {
    std::array<bestspace_entry, ENTRIES_PER_SHARD> shard_entries;
    std::size_t entries_to_copy;
    std::size_t entry_index;
    std::size_t i, j;

    assert (num_entries <= m_shards.size () * ENTRIES_PER_SHARD);

    // shards
    entry_index = 0;
    for (i = 0; i < m_shards.size (); i++)
      {
	entries_to_copy = entry_index < num_entries ? num_entries - entry_index : 0;
	entries_to_copy = MIN (entries_to_copy, ENTRIES_PER_SHARD);
	if (entries_to_copy > 0)
	  {
	    std::memcpy (shard_entries.data (), entries + entry_index, entries_to_copy * sizeof (bestspace_entry));
	  }
	for (j = entries_to_copy; j < ENTRIES_PER_SHARD; j++)
	  {
	    shard_entries[j].set_null ();
	  }

	m_shards[i].reset (shard_entries.data ());
	entry_index += entries_to_copy;
      }

    // the restored L1 pages are resident in the map (C2 only: C3 starts with empty shards and removes reset)
    for (i = 0; i < num_entries; i++)
      {
	if (entries[i].pageid != NULL_PAGEID)
	  {
	    (void) m_map->set_resident (entries[i].volid, entries[i].pageid, code_of (entries[i].freespace));
	  }
      }
  }

  bool
  bestspace::updatable ()
  {
    constexpr std::uint64_t UPDATE_TIME_THRESHOLD = 30;
    std::uint64_t last_updated, now;

    last_updated = m_last_updated.load ();
    now = monotonic_seconds ();
    if (now >= last_updated && now - last_updated >= UPDATE_TIME_THRESHOLD)
      {
	return m_last_updated.compare_exchange_strong (last_updated, now);
      }
    return false;
  }

  int
  bestspace::find (cubthread::entry &thread_ref, OID *class_oid, HFID *hfid, std::uint16_t size, bool is_newrec,
		   PGBUF_WATCHER &page_watcher)
  {
    int consume_size, needed_size;
    std::size_t shard, bias;
    int errid;

    assert (size > 0 && size < DB_PAGESIZE);
    assert (!heap_is_big_length (size));

    // early return or clear stale error to avoid error corruption in below path
    errid = er_errid_if_has_error ();
    if (errid != NO_ERROR)
      {
	return errid;
      }
    er_clear ();

    if (hfid == NULL || HFID_IS_NULL (hfid) ||
	page_watcher.next != NULL || page_watcher.prev != NULL || page_watcher.pgptr != NULL)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FAILED, 0);
	return ER_FAILED;
      }

    // init
    PGBUF_INIT_WATCHER (&page_watcher, PGBUF_ORDERED_HEAP_NORMAL, hfid);

    // strategy
    consume_size = static_cast<int> (size) + SPAGE_SLOT_SIZE;
    needed_size = consume_size + m_unfill_space;
    if (needed_size > heap_nonheader_page_capacity ())
      {
	needed_size = consume_size;
      }
    if (m_distributed_insert)
      {
	shard = static_cast<std::size_t> (thread_ref.index) % m_shards.size ();
	bias = static_cast<std::size_t> (thread_ref.tran_index) % BITS_PER_BYTE;
      }
    else
      {
	shard = 0;
	bias = 0;
      }

    // find
    return find_from_shards (thread_ref, class_oid, hfid, shard, needed_size, consume_size, bias, is_newrec, page_watcher);
  }

  bestspace::tier
  bestspace::size_to_tier (std::uint16_t size)
  {
    assert (size <= DB_PAGESIZE);

    // FS0: 1-7%
    // FS1: 8-15%
    // FS2: 16-24%
    // FS3: 25-34%
    // FS4: 35-45%
    // FS5: 46-57%
    // FS6: 58-70%
    // FS7: 71-84%
    // FS8: 85-100%
    static constexpr std::int16_t threshold[] = { 7, 15, 24, 34, 45, 57, 70, 84 };
    std::int16_t percentage;
    std::int8_t i;

    percentage = size * 100 / DB_PAGESIZE;
    for (i = 0; i < static_cast<std::int8_t> (tier::FS8) + 1; i++)
      {
	if (percentage <= threshold[i])
	  {
	    return static_cast<tier> (i - 1);
	  }
      }
    return tier::FS8;
  }

  std::uint8_t
  bestspace::code_of (std::uint16_t freespace)
  {
    tier fs = size_to_tier (MIN (freespace, static_cast<std::uint16_t> (DB_PAGESIZE)));
    return fs == tier::FS0 ? bestspace_map::CODE_NONE : static_cast<std::uint8_t> (static_cast<std::int8_t> (fs) + 1);
  }

  std::uint16_t
  bestspace::optimistic_freespace (std::uint8_t code)
  {
    // the upper bound of each tier; a too-high value is corrected the first time the page is fixed
    static constexpr std::int16_t upper[] = { 0, 15, 24, 34, 45, 57, 70, 84, 100 };
    int bytes, capacity;

    if (code == bestspace_map::CODE_NONE || code > bestspace_map::CODE_MAX_TIER)
      {
	return 0;
      }
    bytes = upper[code] * DB_PAGESIZE / 100;
    capacity = heap_nonheader_page_capacity ();
    return static_cast<std::uint16_t> (MIN (bytes, capacity));
  }

  void
  bestspace::set_estimates (int num_pages, std::uint64_t recs_num, std::uint64_t recs_sumlen)
  {
    std::size_t i;
    int shard_num_pages;
    std::uint64_t shard_recs_num, shard_recs_sumlen;

    // It’s better for the estimates to be higher than the actual values rather than lower.
    m_num_pages.store (num_pages);
    m_recs_num.store (recs_num);
    m_recs_sumlen.store (recs_sumlen);

    // and sub
    for (i = 0; i < m_shards.size (); i++)
      {
	shard_num_pages = 0;
	shard_recs_num = 0;
	shard_recs_sumlen = 0;
	m_shards[i].get_estimates (shard_num_pages, shard_recs_num, shard_recs_sumlen);
	m_shards[i].subtract_estimates (shard_num_pages, shard_recs_num, shard_recs_sumlen);
      }
  }

  void
  bestspace::get_estimates (int &num_pages, std::uint64_t &recs_num, std::uint64_t &recs_sumlen)
  {
    std::size_t i;

    num_pages = m_num_pages.load ();
    recs_num = m_recs_num.load ();
    recs_sumlen = m_recs_sumlen.load ();
    for (i = 0; i < m_shards.size (); i++)
      {
	m_shards[i].get_estimates (num_pages, recs_num, recs_sumlen);
      }
  }

  void
  bestspace::get_stats (std::uint32_t &request, std::uint32_t &advanced_shard, std::uint32_t &fetch_L3,
			std::uint32_t &fetch_L2, std::uint32_t &fetch_L1, std::uint32_t &found, std::uint32_t &allocated)
  {
    std::size_t i;

    request = 0;
    advanced_shard = 0;
    fetch_L3 = 0;
    fetch_L2 = 0;
    fetch_L1 = 0;
    found = 0;
    allocated = 0;
    for (i = 0; i < m_shards.size (); i++)
      {
	m_shards[i].get_stats (request, advanced_shard, fetch_L3, fetch_L2, fetch_L1, found, allocated);
      }
  }

  std::size_t
  bestspace::get_num_shards ()
  {
    return m_shards.size ();
  }

  void
  bestspace::to_entries (bestspace_entry *entries)
  {
    std::size_t i;

    for (i = 0; i < m_shards.size (); i++)
      {
	m_shards[i].to_entries (entries + i * ENTRIES_PER_SHARD);
      }
  }

  int
  bestspace::find_from_shards (cubthread::entry &thread_ref, OID *class_oid, HFID *hfid, std::size_t shard,
			       std::uint16_t needed_size, std::uint16_t consume_size, std::size_t bias, bool is_newrec, PGBUF_WATCHER &page_watcher)
  {
    std::size_t retry;
    std::size_t i;
    status error;
    int errid;

    retry = 0;
    while (true)
      {
	for (i = 0; i < m_shards.size (); i++)
	  {
	    error = m_shards[ (shard + i) % m_shards.size ()].find (
			    class_oid,
			    hfid,
			    needed_size,
			    consume_size,
			    bias,
			    page_watcher);
	    assert (error == status::FOUND ||
		    error == status::ALLOCATING ||
		    error == status::FAILURE);
	    if (error == status::FOUND)
	      {
		m_shards[ (shard + i) % m_shards.size ()].add_estimates (0, is_newrec ? 1 : 0, consume_size - SPAGE_SLOT_SIZE);
		return NO_ERROR;
	      }
	    if (error == status::FAILURE)
	      {
		ASSERT_ERROR ();
		errid = er_errid ();
		return errid != NO_ERROR ? errid : ER_FAILED;
	      }
	  }

	assert (error == status::ALLOCATING);

	errid = pgbuf_ordered_callback (&thread_ref, wait_for_shard_allocation, &retry);
	if (errid != NO_ERROR)
	  {
	    return errid;
	  }
      }

    // impossible !
    assert (false);
    return ER_FAILED;
  }

  //////////////////////////////////////////////////////////////////////////
  // bestspace register/unregister
  //////////////////////////////////////////////////////////////////////////

  bestspace_registry::registry_cache::registry_cache ()
    : head (nullptr)
    , size (0)
    , generation (0)
  {
  }

  bestspace_registry::registry_cache::~registry_cache ()
  {
    registry_entry *next;

    while (head)
      {
	next = head->next;
	delete head;
	head = next;
      }
  }

  bestspace_registry::bestspace_registry ()
    : m_head (nullptr)
    , m_cache_count (0)
    , m_mutex ()
    , m_generation (1)
  {
  }

  bestspace_registry::~bestspace_registry ()
  {
    registry_entry *node;

    std::lock_guard<std::mutex> lock (m_mutex);

    while (m_head)
      {
	node = m_head;
	m_head = m_head->next;

	delete node->entry;
	delete node;
      }
  }

  bestspace *
  bestspace_registry::create (HFID *hfid, std::size_t shard_count, const bestspace_entry *entries,
			      std::size_t num_entries, int num_pages, std::uint64_t recs_num, std::uint64_t recs_sumlen,
			      std::uint16_t unfill_space, bestspace_map *map)
  {
    registry_entry *node;

    node = new registry_entry;
    if (node == nullptr)
      {
	bestspace_map::destroy (map);
	return nullptr;
      }
    node->hfid = *hfid;
    node->entry = new bestspace (shard_count, num_pages, recs_num, recs_sumlen, unfill_space, map);
    if (node->entry == nullptr)
      {
	bestspace_map::destroy (map);
	delete node;
	return nullptr;
      }
    node->entry->reset (entries, num_entries);

    std::lock_guard<std::mutex> lock (m_mutex);
    if (m_cache_count == 0)
      {
	m_cache_count = MAX (static_cast<std::size_t> (prm_get_integer_value (PRM_ID_BESTSPACE_CACHE_COUNT)),
			     static_cast<std::size_t> (1));
      }
    assert (!find_entry (m_head, hfid));
    insert_entry (m_head, node);
    return node->entry;
  }

  void
  bestspace_registry::destroy (const VFID *vfid)
  {
    registry_entry *node;

    std::lock_guard<std::mutex> lock (m_mutex);

    while ((node = get_node_from_list (m_head, vfid)))
      {
	m_generation.fetch_add (1);
	destroy_entry (node);
      }
  }

  void
  bestspace_registry::destroy (const HFID *hfid)
  {
    registry_entry *node;

    std::lock_guard<std::mutex> lock (m_mutex);

    while ((node = get_node_from_list (m_head, hfid)))
      {
	m_generation.fetch_add (1);
	destroy_entry (node);
      }
  }

  bestspace *
  bestspace_registry::find (HFID *hfid)
  {
    bestspace *entry;

    entry = find_from_cache (hfid);
    if (entry)
      {
	return entry;
      }
    return find_from_global (hfid);
  }

  int
  bestspace_registry::for_each (callback function, void *args)
  {
    registry_entry *node;
    int error, stored_error;

    assert (function != nullptr);

    std::lock_guard<std::mutex> lock (m_mutex);

    stored_error = NO_ERROR;
    for (node = m_head; node != nullptr; node = node->next)
      {
	if (stored_error != NO_ERROR)
	  {
	    er_stack_push ();
	  }

	error = function (&node->hfid, node->entry, args);
	if (stored_error != NO_ERROR)
	  {
	    er_stack_pop ();
	  }
	else if (error != NO_ERROR)
	  {
	    stored_error = error;
	  }
      }

    return stored_error;
  }

  bestspace *
  bestspace_registry::find_from_cache (HFID *hfid)
  {
    registry_entry *cache;
    std::uint64_t generation;

    generation = m_generation.load ();
    if (TLS_cache.generation != generation)
      {
	TLS_cache.generation = generation;
	invalidate_entries (TLS_cache.head);
	return nullptr;
      }

    cache = get_node_from_list (TLS_cache.head, hfid);
    if (!cache)
      {
	return nullptr;
      }

    // make this cache the first (LRU)
    insert_entry (TLS_cache.head, cache);
    return cache->entry;
  }

  bestspace *
  bestspace_registry::find_from_global (HFID *hfid)
  {
    registry_entry *cache;
    bestspace *entry;
    std::size_t cache_count;

    std::unique_lock<std::mutex> ulock (m_mutex);

    auto pair = find_entry (m_head, hfid);
    if (!pair)
      {
	// invalid class oid and hfid
	return nullptr;
      }
    entry = (pair->second)->entry;
    cache_count = m_cache_count;

    ulock.unlock ();

    // register in TLS list.
    // the entries are recycled, never freed, so TLS_cache.size always matches the number of nodes in the list. once
    // the size reaches the count the list cannot be empty, hence get_tail_from_list () cannot return null here.
    if (TLS_cache.size < cache_count)
      {
	cache = new registry_entry;
	TLS_cache.size++;
      }
    else
      {
	cache = get_tail_from_list (TLS_cache.head);
      }
    assert (cache != nullptr);
    cache->hfid = *hfid;
    cache->entry = entry;

    insert_entry (TLS_cache.head, cache);
    return entry;
  }

  void
  bestspace_registry::insert_entry (registry_entry *&head, registry_entry *entry)
  {
    entry->next = head;
    head = entry;
  }

  void
  bestspace_registry::destroy_entry (registry_entry *entry)
  {
    delete entry->entry;
    delete entry;
  }

  std::optional<std::pair<bestspace_registry::registry_entry *, bestspace_registry::registry_entry *>>
      bestspace_registry::find_entry (registry_entry *head, const VFID *vfid)
  {
    registry_entry *prev;

    for (prev = nullptr; head; prev = head, head = head->next)
      {
	if (VFID_EQ (&head->hfid.vfid, vfid))
	  {
	    return std::make_pair (prev, head);
	  }
      }
    return std::nullopt;
  }

  std::optional<std::pair<bestspace_registry::registry_entry *, bestspace_registry::registry_entry *>>
      bestspace_registry::find_entry (registry_entry *head, const HFID *hfid)
  {
    registry_entry *prev;

    for (prev = nullptr; head; prev = head, head = head->next)
      {
	if (HFID_EQ (&head->hfid, hfid))
	  {
	    return std::make_pair (prev, head);
	  }
      }
    return std::nullopt;
  }

  void
  bestspace_registry::invalidate_entries (registry_entry *head)
  {
    while (head)
      {
	HFID_SET_NULL (&head->hfid);
	head->entry = nullptr;
	head = head->next;
      }
  }

  bestspace_registry::registry_entry *
  bestspace_registry::get_node_from_list (registry_entry *&head, const VFID *vfid)
  {
    auto pair = find_entry (head, vfid);
    if (!pair)
      {
	return nullptr;
      }

    if (pair->first)
      {
	(pair->first)->next = (pair->second)->next;
      }
    else
      {
	head = (pair->second)->next;
      }
    return pair->second;
  }

  bestspace_registry::registry_entry *
  bestspace_registry::get_node_from_list (registry_entry *&head, const HFID *hfid)
  {
    auto pair = find_entry (head, hfid);
    if (!pair)
      {
	return nullptr;
      }

    if (pair->first)
      {
	(pair->first)->next = (pair->second)->next;
      }
    else
      {
	head = (pair->second)->next;
      }
    return pair->second;
  }

  bestspace_registry::registry_entry *
  bestspace_registry::get_tail_from_list (registry_entry *&head)
  {
    registry_entry *prev, *curr;

    if (!head)
      {
	return nullptr;
      }
    for (prev = nullptr, curr = head; curr->next; prev = curr, curr = curr->next);
    if (prev)
      {
	prev->next = nullptr;
      }
    else
      {
	head = nullptr;
      }
    return curr;
  }

  //////////////////////////////////////////////////////////////////////////
  // bestspace registry
  //////////////////////////////////////////////////////////////////////////

  bestspace_registry bestspaces;
}
