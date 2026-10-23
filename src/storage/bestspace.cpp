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
#include <cstddef>
#include <cstdint>
#include <cstdlib>
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
    std::size_t available, freespace;
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

    // is this page still belongs to the class (class_oid) ? A map page that reuses a slot's page looks empty at first
    if (pgbuf_get_page_ptype (thread_p, page_watcher.pgptr) != PAGE_HEAP
	|| heap_page_is_not_in_heap (thread_p, page_watcher.pgptr)
	|| heap_page_is_bestspace (thread_p, page_watcher.pgptr)
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
    // the fit uses the room this transaction has. The slot keeps the room without saved space, which is free once its
    // transactions end; the room, not the free space, so that the pre-check above does not pass a page without room
    available = spage_max_space_for_new_record (thread_p, page_watcher.pgptr);
    freespace = spage_max_space_for_new_record_without_saving (thread_p, page_watcher.pgptr);
    if (available < needed_size)
      {
	// there is not enough space now: record the measured free space
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

	// no longer a data page of this heap: drop it from the map too, or it stays resident there for good
	removed_vpid = expected.get_vpid ();
	m_parent.m_map->forget (removed_vpid.volid, removed_vpid.pageid);
      }
  }

  // a finder that read the slot earlier finds the page out of the heap (or back in it, usable) and fails its CAS
  bool
  bestspace::shard::drop_page (VPID vpid)
  {
    std::size_t slot;
    L1 expected, desired;
    VPID held;
    bool dropped = false;

    desired.set_freespace (0);
    desired.set_vpid (vpid_Null_vpid);
    for (slot = 0; slot < L3_FANOUT * L2_FANOUT; slot++)
      {
	expected = m_L1[slot].load ();
	held = expected.get_vpid ();
	// a finder may change the slot's free space meanwhile: try again while the slot still holds the page
	while (VPID_EQ (&held, &vpid) && !m_L1[slot].compare_exchange_strong (expected, desired))
	  {
	    held = expected.get_vpid ();
	  }
	if (VPID_EQ (&held, &vpid))
	  {
	    L2_update (slot / L2_FANOUT, slot % L2_FANOUT);
	    dropped = true;
	  }
      }
    return dropped;
  }

  // The allocating mark keeps refills out, so no slot's page is released while its code is written. A finder may
  // change a slot meanwhile; the code then written is harmless: only the checkpoint image uses a resident page's
  // code. The mark is waited for briefly: an allocation can hold it over a disk read.
  bool
  bestspace::shard::publish_slot_codes (std::size_t &changed)
  {
    constexpr int MAX_TRIES = 64;
    std::size_t slot;
    VPID vpid;
    int tries;
    L1 l1;

    for (tries = 0; allocate_mark () != status::SUCCESS; tries++)
      {
	if (tries == MAX_TRIES)
	  {
	    return false;
	  }
	std::this_thread::yield ();
      }
    for (slot = 0; slot < L3_FANOUT * L2_FANOUT; slot++)
      {
	l1 = m_L1[slot].load ();
	vpid = l1.get_vpid ();
	if (!VPID_ISNULL (&vpid)
	    && m_parent.m_map->set_resident_code (vpid.volid, vpid.pageid, code_of_room (l1.get_freespace ())))
	  {
	    changed++;
	  }
      }
    allocate_unmark ();
    return true;
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

  // Victims, in this order: slots whose page was fed back with more room than the slot records (no one sees that
  // room otherwise), empty slots, the first slots whose free space is below needed_size, then the least free space.
  // The first slots are taken because the finders start there: a page filled in a high slot would make every insert
  // walk the full slots below it. An evicted page keeps its code in the map.
  // Returns how many victims, at the front, were fed back
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
	pages[slot].code = code_of_room (freespace[slot]);
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

    // then the least free space among the rest, kept sorted after the victims above
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

  // Empty the first count victim slots and give their pages back to the map for this refill's claim. The fed-back
  // code may be too high (the slot went on filling the page) and release () keeps it, so a page in the buffer and not
  // latched is measured again and its code replaces the fed-back one (release_latched ()).
  // FAILURE: a fix failed with an error, which stays set; the slots are emptied and their pages given back anyway
  bestspace::status
  bestspace::shard::allocate_release_victims (OID *class_oid, HFID *hfid, std::size_t count,
      std::array<std::pair<std::uint16_t, std::uint16_t>, ALLOC_BATCH_SIZE> &victims, PGBUF_WATCHER &page_watcher)
  {
    cubthread::entry *thread_p = thread_get_thread_entry_info ();
    bestspace_map *map = m_parent.m_map;
    std::size_t i, slot;
    OID page_class_oid;
    std::uint8_t code;
    status result, fixed;
    VPID vpid;
    L1 expected, desired;

    result = status::SUCCESS;
    desired.set_freespace (0);
    desired.set_vpid (vpid_Null_vpid);
    for (i = 0; i < count; i++)
      {
	slot = victims[i].first;
	// the slot is empty now: allocate_install () fills it without evicting anything
	victims[i].second = 0;

	expected = m_L1[slot].load ();
	vpid = expected.get_vpid ();
	// only this thread puts a page into a slot; others change or empty it by CAS (L1_remove (), drop_page ())
	while (!VPID_ISNULL (&vpid) && !m_L1[slot].compare_exchange_strong (expected, desired))
	  {
	    vpid = expected.get_vpid ();
	  }
	if (VPID_ISNULL (&vpid))
	  {
	    continue;
	  }
	L2_update (slot / L2_FANOUT, slot % L2_FANOUT);

	fixed = (result == status::SUCCESS) ? fix_in_buffer (hfid, vpid, page_watcher) : status::DEFERRED;
	if (fixed == status::SUCCESS)
	  {
	    if (heap_bestspace_classify_page (thread_p, page_watcher.pgptr, &page_class_oid, &code)
		&& OID_EQ (&page_class_oid, class_oid))
	      {
		map->release_latched (vpid.volid, vpid.pageid, code);
		map->count (bestspace_map::CNT_VICTIM_MEASURED);
	      }
	    else
	      {
		map->forget (vpid.volid, vpid.pageid);
	      }
	    pgbuf_ordered_unfix (thread_p, &page_watcher);
	    continue;
	  }
	if (fixed == status::NOT_FOUND)
	  {
	    map->forget (vpid.volid, vpid.pageid);
	    continue;
	  }
	if (fixed == status::FAILURE)
	  {
	    result = status::FAILURE;
	  }
	map->release (vpid.volid, vpid.pageid, code_of_room (expected.get_freespace ()), false);
      }
    return result;
  }

  // Fix the page only if it is in the buffer and not latched: no I/O and no wait, so no latch order is needed and the
  // caller may hold other pages of the heap. The watcher is set up as an ordered fix sets it.
  bestspace::status
  bestspace::shard::fix_in_buffer (HFID *hfid, VPID &vpid, PGBUF_WATCHER &page_watcher)
  {
    cubthread::entry *thread_p = thread_get_thread_entry_info ();
    unsigned int initial_rank;
    PAGE_PTR pgptr;
    int wait_msecs;
    int error;

    assert (hfid != nullptr);
    assert (PGBUF_IS_CLEAN_WATCHER (&page_watcher));
    // the watcher belongs to this heap: the fix attaches it again from hfid
    assert (VPID_ISNULL (&page_watcher.group_id)
	    || (page_watcher.group_id.volid == hfid->vfid.volid && page_watcher.group_id.pageid == hfid->hpgid));

    // as L1_fix (): fail at once, with no error, on a latch another thread holds, whatever the lock timeout
    wait_msecs = logtb_set_thread_wait_msecs (thread_p, LK_FORCE_ZERO_WAIT);
    pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE_IF_IN_BUFFER, PGBUF_LATCH_WRITE, PGBUF_CONDITIONAL_LATCH);
    (void) logtb_set_thread_wait_msecs (thread_p, wait_msecs);
    if (pgptr == NULL)
      {
	error = er_errid_if_has_error ();
	if (error == NO_ERROR || error == ER_LK_PAGE_TIMEOUT)
	  {
	    // not in the buffer, or latched by another thread
	    er_clear ();
	    return status::DEFERRED;
	  }
	if (error == ER_PB_BAD_PAGEID)
	  {
	    er_clear ();
	    return status::NOT_FOUND;
	  }
	return status::FAILURE;
      }

    // the attach also resets the initial rank (HEAP_HDR for the header page). Keep the caller's, as an ordered fix
    // does: later fixes take their rank from it, and a heap page ranked as the header would be fixed out of order
    initial_rank = page_watcher.initial_rank;
    pgbuf_attach_watcher (thread_p, pgptr, PGBUF_LATCH_WRITE, hfid, &page_watcher);
    page_watcher.initial_rank = initial_rank;
    if (!VPID_EQ (&page_watcher.group_id, &vpid))
      {
	// the header page keeps the HEAP_HDR rank the attach gave its fix, as an ordered fix gives it
	page_watcher.curr_rank = initial_rank;
      }
    return status::SUCCESS;
  }

  // Fix a claimed page and check that the record fits. valid: false when it is no longer a data page of the heap.
  // in_buffer_only: fix with fix_in_buffer () (DEFERRED when it cannot); the checks after the fix are the same.
  bestspace::status
  bestspace::shard::allocate_verify_actual_space (OID *class_oid, HFID *hfid, std::uint16_t needed_size,
      bestspace_entry &candidate, bool &valid, bool in_buffer_only, PGBUF_WATCHER &page_watcher)
  {
    cubthread::entry *thread_p = thread_get_thread_entry_info ();
    OID page_class_oid;
    VPID vpid;
    unsigned int initial_rank;
    int available, freespace;
    status result;
    int error;

    assert (class_oid != nullptr);
    assert (hfid != nullptr);
    assert (PGBUF_IS_CLEAN_WATCHER (&page_watcher));

    valid = true;
    vpid.volid = candidate.volid;
    vpid.pageid = candidate.pageid;
    initial_rank = page_watcher.initial_rank;

    if (in_buffer_only)
      {
	// errors are not deferred: a bad page leaves the map, any other error fails the insert
	result = fix_in_buffer (hfid, vpid, page_watcher);
	if (result == status::NOT_FOUND)
	  {
	    valid = false;
	  }
	if (result != status::SUCCESS)
	  {
	    return result;
	  }
      }
    else
      {
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
      }
    assert (page_watcher.initial_rank == initial_rank);

    // a map page is never a data page (see L1_find ())
    if (pgbuf_get_page_ptype (thread_p, page_watcher.pgptr) != PAGE_HEAP
	|| heap_page_is_not_in_heap (thread_p, page_watcher.pgptr)
	|| heap_page_is_bestspace (thread_p, page_watcher.pgptr))
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

    // the fit uses the room this transaction has; what the caller keeps excludes saved space (see L1_find ())
    available = spage_max_space_for_new_record (thread_p, page_watcher.pgptr);
    if (available < needed_size)
      {
	// the caller records this value in the map as the page's measured tier: measure it as every feedback does
	freespace = spage_get_free_space_without_saving (thread_p, page_watcher.pgptr);
	candidate.freespace = static_cast<std::uint16_t> (MIN (MAX (freespace, 0), DB_PAGESIZE));
	pgbuf_ordered_unfix (thread_p, &page_watcher);
	return status::NOT_FOUND;
      }
    // the room a slot keeps (see L1_find ()); the caller takes the record off
    freespace = spage_max_space_for_new_record_without_saving (thread_p, page_watcher.pgptr);
    candidate.freespace = static_cast<std::uint16_t> (freespace);
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
				  &m_parent);
    if (error != NO_ERROR)
      {
	return error;
      }
    m_num_pages.fetch_add (ALLOC_BATCH_SIZE - num_candidates);

    STATS_INC (allocated, ALLOC_BATCH_SIZE - num_candidates);

    // the room a slot keeps (see L1_find ()); allocate () checks the fit with this transaction's room
    freespace = spage_max_space_for_new_record_without_saving (thread_p, page_watcher.pgptr);
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
	    map->release (candidates[i].volid, candidates[i].pageid, code_of_room (candidates[i].freespace), true);
	    continue;
	  }

	l1.set_freespace (candidates[i].freespace);
	l1.set_vpid ({ candidates[i].pageid, candidates[i].volid });
	// one exchange, not a load and a store: an L1_remove () or drop_page () of the evicted page then either empties
	// the slot first or fails its CAS, so the page is never both forgotten and released
	old = m_L1[slot].exchange (l1);

	L2_update (slot / L2_FANOUT, slot % L2_FANOUT);

	old_vpid = old.get_vpid ();
	if (!VPID_ISNULL (&old_vpid))
	  {
	    map->release (old_vpid.volid, old_vpid.pageid, code_of_room (old.get_freespace ()), false);
	  }
      }
  }

  bestspace::status
  bestspace::shard::allocate (OID *class_oid, HFID *hfid, std::uint16_t needed_size, std::uint16_t consume_size,
			      PGBUF_WATCHER &page_watcher)
  {
    // a claimed page: not looked at, deferred by the first pass, back in the map (released or forgotten), or the fit
    enum class claim_state
    {
      UNCHECKED,
      DEFERRED,
      SETTLED,
      FIT
    };
    std::array<std::pair<std::uint16_t, std::uint16_t>, ALLOC_BATCH_SIZE> victims; // index, freespace
    std::array<bestspace_entry, ALLOC_BATCH_SIZE> claimed;
    std::array<claim_state, ALLOC_BATCH_SIZE> state;
    std::array<bestspace_entry, ALLOC_BATCH_SIZE> candidates;
    bestspace_map_page picked[ALLOC_BATCH_SIZE];
    bestspace_entry entry;
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

    // pick the victims; the fed-back ones go back to the map first, so that this claim can hand them out again
    num_dirty = allocate_pick_victims (victims, needed_size);
    if (allocate_release_victims (class_oid, hfid, num_dirty, victims, page_watcher) == status::FAILURE)
      {
	allocate_unmark ();
	return status::FAILURE;
      }

    // claim pages of a higher tier, whose lower bound fits the request; a page of the same tier may not fit and is left
    // for smaller requests. A request in the top tier claims its own tier
    min_code = code_of (needed_size);
    num_claimed = map->claim (min_code < bestspace_map::CODE_MAX_TIER ? min_code + 1 : bestspace_map::CODE_MAX_TIER,
			      picked, ALLOC_BATCH_SIZE);
    if (num_claimed > 0)
      {
	(void) FI_TEST (thread_get_thread_entry_info (), FI_TEST_BESTSPACE_DELAY_CLAIM, 0);
      }

    // highest tier first: a page with more room takes more of the rows before the next refill
    for (i = 0; i < num_claimed; i++)
      {
	entry.volid = picked[i].volid;
	entry.pageid = picked[i].pageid;
	entry.freespace = optimistic_freespace (picked[i].code);
	for (j = i; j > 0 && claimed[j - 1].freespace < entry.freespace; j--)
	  {
	    claimed[j] = claimed[j - 1];
	  }
	claimed[j] = entry;
	state[i] = claim_state::UNCHECKED;
      }

    // record what a check found in the map. false: the insert fails, and every claimed page not settled yet goes back.
    // A measured page goes back with the code of its free space, another with the code of its tier bound
    auto settle = [&] (std::size_t index, status outcome, bool page_valid) -> bool
    {
      std::size_t k;

      if (outcome == status::FOUND)
	{
	  state[index] = claim_state::FIT;
	  map->count (bestspace_map::CNT_REUSED);
	  return true;
	}

      state[index] = claim_state::SETTLED;
      if (outcome == status::FAILURE)
	{
	  if (page_valid)
	    {
	      // the page could not be fixed (interrupted, I/O error): it may still have the space
	      map->release (claimed[index].volid, claimed[index].pageid, code_of_room (claimed[index].freespace),
			    false);
	    }
	  else
	    {
	      // its class could not be read: drop it, or every insert that claims it fails the same way
	      map->forget (claimed[index].volid, claimed[index].pageid);
	    }
	  for (k = 0; k < num_claimed; k++)
	    {
	      if (state[k] == claim_state::UNCHECKED || state[k] == claim_state::DEFERRED)
		{
		  state[k] = claim_state::SETTLED;
		  map->release (claimed[k].volid, claimed[k].pageid, code_of_room (claimed[k].freespace), false);
		}
	    }
	  return false;
	}

      assert (outcome == status::NOT_FOUND);
      map->count (bestspace_map::CNT_VERIFY_FAIL);
      if (page_valid)
	{
	  map->release (claimed[index].volid, claimed[index].pageid, code_of (claimed[index].freespace), true);
	}
      else
	{
	  // not a page of this heap any more (deallocated, removed, another class)
	  map->forget (claimed[index].volid, claimed[index].pageid);
	}
      return true;
    };

    // first pass: only pages in the buffer and not latched (no I/O, no wait), until one fits; defer the others
    found = false;
    fit_index = 0;
    for (i = 0; i < num_claimed && !found; i++)
      {
	result = allocate_verify_actual_space (class_oid, hfid, needed_size, claimed[i], valid, true, page_watcher);
	if (result == status::DEFERRED)
	  {
	    state[i] = claim_state::DEFERRED;
	    map->count (bestspace_map::CNT_DEFERRED);
	    continue;
	  }
	if (!settle (i, result, valid))
	  {
	    allocate_unmark ();
	    return status::FAILURE;
	  }
	found = (result == status::FOUND);
	fit_index = i;
      }

    // none fits: one disk fix per refill at most (the highest deferred tier), so a large heap is not read through
    for (i = 0; i < num_claimed && !found; i++)
      {
	if (state[i] != claim_state::DEFERRED)
	  {
	    continue;
	  }

	map->count (bestspace_map::CNT_DISK_VERIFY);
	result = allocate_verify_actual_space (class_oid, hfid, needed_size, claimed[i], valid, false, page_watcher);
	assert (result != status::DEFERRED);
	if (!settle (i, result, valid))
	  {
	    allocate_unmark ();
	    return status::FAILURE;
	  }
	found = (result == status::FOUND);
	fit_index = i;
	break;
      }

    num_candidates = 0;
    if (found)
      {
	// the unchecked pages become unverified replacements; the page returned to the caller goes last
	for (i = 0; i < num_claimed; i++)
	  {
	    if (state[i] == claim_state::UNCHECKED || state[i] == claim_state::DEFERRED)
	      {
		candidates[num_candidates++] = claimed[i];
	      }
	  }
	assert (state[fit_index] == claim_state::FIT);
	candidates[num_candidates++] = claimed[fit_index];
      }
    else
      {
	// the deferred pages go back unread; a code fed back meanwhile wins (release ())
	for (i = 0; i < num_claimed; i++)
	  {
	    assert (state[i] == claim_state::DEFERRED || state[i] == claim_state::SETTLED);
	    if (state[i] == claim_state::DEFERRED)
	      {
		map->release (claimed[i].volid, claimed[i].pageid, code_of_room (claimed[i].freespace), false);
	      }
	  }

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

	if (spage_max_space_for_new_record (thread_get_thread_entry_info (), page_watcher.pgptr) < needed_size)
	  {
	    // heap_alloc_new_pages () links the pages before it latches the first one again, so an inserter may have
	    // used it meanwhile: install the batch and serve the request from the slots
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
			std::uint16_t unfill_space, bestspace_map *map, bool map_inline, VPID *map_pages,
			std::size_t map_num_pages)
    : m_verify_cursor (0)
    , m_orphan_num_follow (0)
    , m_orphan_followed (0)
    , m_shards ()
    , m_map (map)
    , m_absorbed (false)
    , m_orphan_suspect (false)
    , m_layout_mutex ()
    , m_orphan_num_hints (0)
    , m_orphan_walk (false)
    , m_map_inline (map_inline)
    , m_map_pages (map_pages)
    , m_map_num_pages (map_pages != nullptr ? map_num_pages : 0)
    , m_map_pages_alloc (map_pages != nullptr ? map_num_pages : 0)
    , m_distributed_insert (prm_get_bool_value (PRM_ID_BESTSPACE_DISTRIBUTED_INSERT))
    , m_unfill_space (unfill_space)
    , m_num_pages (num_pages)
    , m_recs_num (recs_num)
    , m_recs_sumlen (recs_sumlen)
  {
    assert (shard_count > 0);
    assert (map != nullptr);

    VPID_SET_NULL (&m_orphan_walk_next);
    VPID_SET_NULL (&m_orphan_walk_prev);
    for (std::size_t i = 0; i < shard_count; i++)
      {
	m_shards.emplace_back (*this);
      }
  }

  bestspace::~bestspace ()
  {
    bestspace_map::destroy (m_map);
    if (m_map_pages != nullptr)
      {
	free_and_init (m_map_pages);
      }
  }

  bool
  bestspace::drop_from_slots (VPID vpid)
  {
    bool dropped = false;

    for (shard &s : m_shards)
      {
	dropped = s.drop_page (vpid) || dropped;
      }
    return dropped;
  }

  std::size_t
  bestspace::publish_slot_codes ()
  {
    std::size_t changed = 0;

    for (shard &s : m_shards)
      {
	(void) s.publish_slot_codes (changed);
      }
    return changed;
  }

  bestspace_map *
  bestspace::get_map ()
  {
    return m_map;
  }

  void
  bestspace::set_orphan_suspect (const VPID &hint)
  {
    std::lock_guard<std::mutex> lock (m_layout_mutex);

    if (VPID_ISNULL (&hint) || m_orphan_num_hints == ORPHAN_HINTS)
      {
	m_orphan_walk = true;
      }
    else
      {
	m_orphan_hints[m_orphan_num_hints++] = hint;
      }
    m_orphan_suspect.store (true);
    bestspace_map::note_work ();
  }

  bool
  bestspace::orphan_suspect () const
  {
    return m_orphan_suspect.load ();
  }

  bool
  bestspace::take_orphan_suspect (VPID hints[ORPHAN_HINTS], std::size_t &num_hints, bool &walk)
  {
    std::lock_guard<std::mutex> lock (m_layout_mutex);

    if (!m_orphan_suspect.exchange (false))
      {
	return false;
      }
    for (num_hints = 0; num_hints < m_orphan_num_hints; num_hints++)
      {
	hints[num_hints] = m_orphan_hints[num_hints];
      }
    walk = m_orphan_walk;
    m_orphan_num_hints = 0;
    m_orphan_walk = false;
    return true;
  }

  // acquire: a writer that sees true also sees every code the loader merged before set_absorbed ()
  bool
  bestspace::is_absorbed () const
  {
    return m_absorbed.load (std::memory_order_acquire);
  }

  void
  bestspace::set_absorbed ()
  {
    m_absorbed.store (true, std::memory_order_release);
  }

  std::size_t
  bestspace::map_capacity_sectors (std::size_t per_page)
  {
    std::lock_guard<std::mutex> lock (m_layout_mutex);
    return m_map_inline ? bestspace_map::INLINE_SECTORS : m_map_num_pages * per_page;
  }

  bool
  bestspace::map_page_at (std::size_t page_index, VPID &vpid, bool &is_inline)
  {
    std::lock_guard<std::mutex> lock (m_layout_mutex);
    is_inline = m_map_inline;
    if (m_map_inline || page_index >= m_map_num_pages)
      {
	return false;
      }
    vpid = m_map_pages[page_index];
    return true;
  }

  bool
  bestspace::map_is_inline ()
  {
    std::lock_guard<std::mutex> lock (m_layout_mutex);
    return m_map_inline;
  }

  std::size_t
  bestspace::map_num_pages ()
  {
    std::lock_guard<std::mutex> lock (m_layout_mutex);
    return m_map_num_pages;
  }

  bool
  bestspace::map_grow_page_slots ()
  {
    std::size_t alloc = m_map_pages_alloc == 0 ? 4 : m_map_pages_alloc * 2;
    VPID *pages = (VPID *) realloc (m_map_pages, alloc * sizeof (VPID));

    if (pages == nullptr)
      {
	return false;
      }
    m_map_pages = pages;
    m_map_pages_alloc = alloc;
    return true;
  }

  // the caller holds the heap header write latch, so the reserved slot stays free until its map_append_page ()
  bool
  bestspace::map_reserve_page_slot ()
  {
    std::lock_guard<std::mutex> lock (m_layout_mutex);
    return m_map_num_pages < m_map_pages_alloc || map_grow_page_slots ();
  }

  bool
  bestspace::map_append_page (VPID vpid)
  {
    std::lock_guard<std::mutex> lock (m_layout_mutex);
    if (m_map_num_pages == m_map_pages_alloc && !map_grow_page_slots ())
      {
	return false;
      }
    m_map_pages[m_map_num_pages++] = vpid;
    m_map_inline = false;
    return true;
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

  // A slot keeps the room: the free space less a slot entry (when the page has no unused one), aligned down to
  // HEAP_MAX_ALIGN. The map holds the free space's tier, so the room is first raised by the most it can hide; else a
  // page just above a tier boundary goes back one tier low, for good if it is never fed back. The raise (a slot
  // entry and 7 bytes) is below the 1% of a page between tier bounds, so a slot's optimistic bound keeps its tier
  std::uint8_t
  bestspace::code_of_room (std::uint16_t room)
  {
    int freespace = room + SSIZEOF (SPAGE_SLOT) + HEAP_MAX_ALIGN - 1;

    return code_of (static_cast<std::uint16_t> (MIN (freespace, DB_PAGESIZE)));
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
    , m_quiesced ()
    , m_work (nullptr)
    , m_generation (1)
    , m_creates (0)
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

	destroy_entry (node);
      }
  }

  bestspace *
  bestspace_registry::create (HFID *hfid, std::size_t shard_count, int num_pages, std::uint64_t recs_num,
			      std::uint64_t recs_sumlen, std::uint16_t unfill_space, bestspace_map *map, bool map_inline,
			      VPID *map_pages, std::size_t map_num_pages)
  {
    // the engine does not use exceptions, and in SERVER_MODE memory_wrapper.hpp turns 'new' into a macro that does not
    // take std::nothrow: malloc and construct in place (destroy_entry () undoes both)
    static_assert (alignof (bestspace) <= alignof (std::max_align_t), "malloc must align a bestspace");
    registry_entry *node = (registry_entry *) malloc (sizeof (registry_entry));
    void *area = malloc (sizeof (bestspace));

    if (node == nullptr || area == nullptr)
      {
	// the bestspace would have owned the map and the pages
	bestspace_map::destroy (map);
	if (map_pages != nullptr)
	  {
	    free_and_init (map_pages);
	  }
	if (area != nullptr)
	  {
	    free_and_init (area);
	  }
	if (node != nullptr)
	  {
	    free_and_init (node);
	  }
	return nullptr;
      }
    node->hfid = *hfid;
    node->next = nullptr;
    node->pins = 0;
    node->dropped = false;
    node->retired = false;
    node->entry = placement_new (static_cast<bestspace *> (area), shard_count, num_pages, recs_num, recs_sumlen,
				 unfill_space, map, map_inline, map_pages, map_num_pages);

    std::lock_guard<std::mutex> lock (m_mutex);
    if (m_cache_count == 0)
      {
	m_cache_count = MAX (static_cast<std::size_t> (prm_get_integer_value (PRM_ID_BESTSPACE_CACHE_COUNT)),
			     static_cast<std::size_t> (1));
      }
    assert (!find_entry (m_head, hfid));
    insert_entry (m_head, node);
    // under the mutex of the insert, which find_from_global () also takes: a miss is ordered against the count
    m_creates.fetch_add (1);
    // after the insert: the daemon reads the epoch before it pins, so it pins this one now or in its next round
    bestspace_map::note_work ();
    return node->entry;
  }

  std::uint64_t
  bestspace_registry::create_count () const
  {
    return m_creates.load ();
  }

  // the heap is dropped or reused: wait for its work in progress; a pinned bestspace is only marked dropped
  void
  bestspace_registry::destroy (const VFID *vfid)
  {
    registry_entry *node;

    std::unique_lock<std::mutex> lock (m_mutex);

    while ((node = get_node_from_list (m_head, vfid)))
      {
	m_generation.fetch_add (1);
	destroy_or_drop_entry (node);
      }
    wait_work_locked (lock, vfid);
  }

  void
  bestspace_registry::destroy (const HFID *hfid)
  {
    registry_entry *node;

    std::unique_lock<std::mutex> lock (m_mutex);

    while ((node = get_node_from_list (m_head, hfid)))
      {
	m_generation.fetch_add (1);
	destroy_or_drop_entry (node);
      }
    wait_work_locked (lock, &hfid->vfid);
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

  int
  bestspace_registry::pin_all (pinned *&out, std::size_t &count)
  {
    std::lock_guard<std::mutex> lock (m_mutex);
    registry_entry *node;
    std::size_t n = 0, i = 0;

    out = nullptr;
    count = 0;
    for (node = m_head; node != nullptr; node = node->next)
      {
	n++;
      }
    if (n == 0)
      {
	return NO_ERROR;
      }
    out = (pinned *) malloc (n * sizeof (pinned));
    if (out == nullptr)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, n * sizeof (pinned));
	return ER_OUT_OF_VIRTUAL_MEMORY;
      }
    for (node = m_head; node != nullptr; node = node->next, i++)
      {
	node->pins++;
	out[i].hfid = node->hfid;
	out[i].entry = node->entry;
	out[i].node = node;
      }
    count = n;
    return NO_ERROR;
  }

  void
  bestspace_registry::unpin_all (pinned *&pins, std::size_t count)
  {
    {
      std::lock_guard<std::mutex> lock (m_mutex);

      for (std::size_t i = 0; i < count; i++)
	{
	  unpin_locked ((registry_entry *) pins[i].node);
	}
    }
    if (pins != nullptr)
      {
	free_and_init (pins);
      }
  }

  bool
  bestspace_registry::pin (const HFID *hfid, pinned &out)
  {
    std::lock_guard<std::mutex> lock (m_mutex);
    auto pair = find_entry (m_head, hfid);

    if (!pair)
      {
	return false;
      }
    pair->second->pins++;
    out.hfid = pair->second->hfid;
    out.entry = pair->second->entry;
    out.node = pair->second;
    return true;
  }

  void
  bestspace_registry::unpin (pinned &pin)
  {
    std::lock_guard<std::mutex> lock (m_mutex);

    unpin_locked ((registry_entry *) pin.node);
    pin.entry = nullptr;
    pin.node = nullptr;
  }

  bool
  bestspace_registry::is_dropped (const pinned &pin)
  {
    std::lock_guard<std::mutex> lock (m_mutex);

    return ((registry_entry *) pin.node)->dropped;
  }

  bool
  bestspace_registry::begin_work (const pinned &pin, work &w, bool *quiesced)
  {
    std::lock_guard<std::mutex> lock (m_mutex);
    registry_entry *node = (registry_entry *) pin.node;
    quiesced_file *q = find_quiesced_locked (&pin.hfid.vfid);

    if (quiesced != nullptr)
      {
	*quiesced = q != nullptr && !node->dropped && !node->retired;
      }
    if (q != nullptr)
      {
	q->refused = true;
      }
    if (node->dropped || node->retired || q != nullptr)
      {
	return false;
      }
    w.vfid = pin.hfid.vfid;
    w.next = m_work;
    m_work = &w;
    return true;
  }

  bool
  bestspace_registry::begin_work (const VFID *vfid, work &w)
  {
    std::lock_guard<std::mutex> lock (m_mutex);
    quiesced_file *q = find_quiesced_locked (vfid);

    if (q != nullptr)
      {
	q->refused = true;
	return false;
      }
    w.vfid = *vfid;
    w.next = m_work;
    m_work = &w;
    return true;
  }

  void
  bestspace_registry::end_work (work &w)
  {
    {
      std::lock_guard<std::mutex> lock (m_mutex);
      work **link;

      for (link = &m_work; *link != &w; link = & (*link)->next)
	{
	  assert (*link != nullptr);
	}
      *link = w.next;
      w.next = nullptr;
    }
    m_work_cv.notify_all ();
  }

  bool
  bestspace_registry::may_work (const pinned &pin)
  {
    std::lock_guard<std::mutex> lock (m_mutex);
    registry_entry *node = (registry_entry *) pin.node;
    quiesced_file *q = find_quiesced_locked (&pin.hfid.vfid);

    if (q != nullptr)
      {
	// the daemon leaves the heap's work for later
	q->refused = true;
      }
    return !node->dropped && !node->retired && q == nullptr;
  }

  void
  bestspace_registry::quiesce (const VFID *vfid, bool retire)
  {
    std::unique_lock<std::mutex> lock (m_mutex);
    quiesced_file *slot = nullptr;
    registry_entry *node;

    // the file's slot, or a free one
    m_work_cv.wait (lock, [this, vfid, &slot]
    {
      quiesced_file *free_slot = nullptr;

      for (quiesced_file &q : m_quiesced)
	{
	  if (q.count > 0 && VFID_EQ (&q.vfid, vfid))
	    {
	      slot = &q;
	      return true;
	    }
	  if (q.count == 0 && free_slot == nullptr)
	    {
	      free_slot = &q;
	    }
	}
      slot = free_slot;
      return slot != nullptr;
    });
    if (slot->count == 0)
      {
	slot->vfid = *vfid;
	slot->refused = false;
      }
    slot->count++;
    if (retire)
      {
	for (node = m_head; node != nullptr; node = node->next)
	  {
	    if (VFID_EQ (&node->hfid.vfid, vfid))
	      {
		node->retired = true;
	      }
	  }
      }
    wait_work_locked (lock, vfid);
  }

  void
  bestspace_registry::end_quiesce (const VFID *vfid)
  {
    bool refused = false;

    {
      std::lock_guard<std::mutex> lock (m_mutex);
      quiesced_file *q = find_quiesced_locked (vfid);

      assert (q != nullptr);
      if (q != nullptr && --q->count == 0)
	{
	  refused = q->refused;
	}
    }
    // a quiesce () waiting for a slot
    m_work_cv.notify_all ();
    if (refused)
      {
	// the maintenance daemon left the heap's work for later, and may have gone idle since
	bestspace_map::note_work ();
      }
  }

  bool
  bestspace_registry::empty ()
  {
    std::lock_guard<std::mutex> lock (m_mutex);

    return m_head == nullptr;
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

  // an entry of the global list, made by create (). The entries of TLS_cache are separate nodes (new/delete)
  void
  bestspace_registry::destroy_entry (registry_entry *entry)
  {
    entry->entry->~bestspace ();
    free_and_init (entry->entry);
    free_and_init (entry);
  }

  // a node already taken out of the global list: a pinned one stays until its last unpin
  void
  bestspace_registry::destroy_or_drop_entry (registry_entry *entry)
  {
    if (entry->pins > 0)
      {
	entry->dropped = true;
      }
    else
      {
	destroy_entry (entry);
      }
  }

  bestspace_registry::quiesced_file *
  bestspace_registry::find_quiesced_locked (const VFID *vfid)
  {
    for (quiesced_file &q : m_quiesced)
      {
	if (q.count > 0 && VFID_EQ (&q.vfid, vfid))
	  {
	    return &q;
	  }
      }
    return nullptr;
  }

  // a worker waits only for latches of the heap's pages and file header, which the caller holds none of
  void
  bestspace_registry::wait_work_locked (std::unique_lock<std::mutex> &lock, const VFID *vfid)
  {
    m_work_cv.wait (lock, [this, vfid]
    {
      for (const work *w = m_work; w != nullptr; w = w->next)
	{
	  if (VFID_EQ (&w->vfid, vfid))
	    {
	      return false;
	    }
	}
      return true;
    });
  }

  void
  bestspace_registry::unpin_locked (registry_entry *entry)
  {
    assert (entry->pins > 0);
    if (--entry->pins == 0 && entry->dropped)
      {
	destroy_entry (entry);
      }
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
  bestspace_sector_table bestspace_pending;
  bestspace_sector_table bestspace_recovery;
}
