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
  // POC diagnostics
  static thread_local VPID fc_dbg_vpid;
  static thread_local int fc_dbg_source;
  static thread_local int fc_dbg_measured;
  static thread_local int fc_dbg_needed;

  static void
  fc_dbg_set (VPID vpid, int source, int measured, int needed)
  {
    fc_dbg_vpid = vpid;
    fc_dbg_source = source;
    fc_dbg_measured = measured;
    fc_dbg_needed = needed;
  }

  void
  bestspace_debug_last_found (VPID *vpid, int *source, int *measured, int *needed)
  {
    *vpid = fc_dbg_vpid;
    *source = fc_dbg_source;
    *measured = fc_dbg_measured;
    *needed = fc_dbg_needed;
  }

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

  bestspace::status
  bestspace::shard::find_candidate (OID *class_oid, std::uint16_t needed_size, std::uint16_t consume_size,
				    bestspace_entry &candidate, bool &valid, PGBUF_WATCHER &page_watcher)
  {
    status result;

    result = allocate_verify_actual_space (class_oid, needed_size, candidate, valid, page_watcher);
    if (result == status::FOUND)
      {
	fc_dbg_set ({ candidate.pageid, candidate.volid }, 2, candidate.freespace, needed_size);
	candidate.freespace -= consume_size;
	STATS_INC (found, 1);
      }
    return result;
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
	    error = L1_find (class_oid, needed_size, consume_size, l2_index, pos[ (i + bias) % length], page_watcher,
			     false);
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
			     std::size_t l2_index, std::size_t l1_index, PGBUF_WATCHER &page_watcher,
			     bool force_check)
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
    if (!force_check && expected.get_freespace () < needed_size)
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
    fc_dbg_set (old_vpid, 1, (int) freespace, needed_size);

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
    L1 desired;

    desired.set_freespace (0);
    desired.set_vpid (vpid_Null_vpid);
    if (m_L1[l2_index * L2_FANOUT + l1_index].compare_exchange_strong (expected, desired))
      {
	L2_update (l2_index, l1_index);
	if (m_parent.m_fcmap != nullptr)
	  {
	    // the page left the heap (or this class): it is no longer a data page of this heap
	    m_parent.m_fcmap->remove (expected.get_vpid ());
	  }
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

  void
  bestspace::shard::allocate_pick_victims (std::array<VPID, L3_FANOUT * L2_FANOUT + ALLOC_BATCH_SIZE> &residents,
      std::array<std::pair<std::uint16_t, std::uint16_t>, ALLOC_BATCH_SIZE> &victims)
  {
    std::size_t pos;
    std::size_t i;
    int freespace;
    L1 l1;

    victims.fill (std::make_pair (std::numeric_limits<std::uint16_t>::max (), std::numeric_limits<std::uint16_t>::max ()));
    for (i = 0; i < L3_FANOUT * L2_FANOUT; i++)
      {
	l1 = m_L1[i].load ();
	freespace = l1.get_freespace ();

	// add residents list
	residents[i] = l1.get_vpid ();

	if (freespace >= victims[ALLOC_BATCH_SIZE - 1].second)
	  {
	    continue;
	  }

	pos = ALLOC_BATCH_SIZE - 1;
	while (pos > 0 && freespace < victims[pos - 1].second)
	  {
	    victims[pos] = victims[pos - 1];
	    pos--;
	  }

	victims[pos] = std::make_pair (i, freespace);
      }
  }

  std::size_t
  bestspace::shard::allocate_pick_candidates (std::array<VPID, L3_FANOUT * L2_FANOUT + ALLOC_BATCH_SIZE> &residents,
      std::array<std::pair<std::uint16_t, std::uint16_t>, ALLOC_BATCH_SIZE> &victims,
      std::array<bestspace_entry, ALLOC_BATCH_SIZE> &candidates,
      std::array<std::pair<bestspace_entry, std::uint16_t>, ALLOC_BATCH_SIZE> &resident_candidates,
      std::size_t &num_resident_candidates, std::uint16_t needed_size)
  {
    std::size_t num_residents = L3_FANOUT * L2_FANOUT;
    bestspace_entry buffer[ALLOC_BATCH_SIZE];
    std::size_t num_buffer;
    std::size_t num_candidates;
    std::size_t i, j;

    num_buffer = m_parent.pop_candidates (buffer, victims[ALLOC_BATCH_SIZE - 1].second, needed_size);

    num_candidates = 0;
    num_resident_candidates = 0;
    for (i = 0; i < num_buffer; i++)
      {
	for (j = 0; j < num_residents; j++)
	  {
	    if (buffer[i].volid == residents[j].volid &&
		buffer[i].pageid == residents[j].pageid)
	      {
		break;
	      }
	  }
	if (j < L3_FANOUT * L2_FANOUT)
	  {
	    resident_candidates[num_resident_candidates] =
		    std::make_pair (buffer[i], static_cast<std::uint16_t> (j));
	    num_resident_candidates++;
	  }
	else if (j == num_residents)
	  {
	    candidates[num_candidates] = buffer[i];
	    num_candidates++;

	    residents[num_residents].volid = buffer[i].volid;
	    residents[num_residents].pageid = buffer[i].pageid;
	    num_residents++;
	  }
      }
    return num_candidates;
  }

  bestspace::status
  bestspace::shard::allocate_get_candidates_or_update_residents (OID *class_oid, std::uint16_t needed_size,
      std::uint16_t consume_size, std::array<VPID, L3_FANOUT * L2_FANOUT + ALLOC_BATCH_SIZE> &residents,
      std::array<std::pair<std::uint16_t, std::uint16_t>, ALLOC_BATCH_SIZE> &victims,
      std::array<bestspace_entry, ALLOC_BATCH_SIZE> &candidates, std::size_t &num_candidates, PGBUF_WATCHER &page_watcher)
  {
    std::array<std::pair<bestspace_entry, std::uint16_t>, ALLOC_BATCH_SIZE> resident_candidates;
    std::size_t num_resident_candidates;
    std::size_t i, j;
    status error;

    // pick four replacement candidates with more freespace than the victim pages above.
    num_candidates = allocate_pick_candidates (residents, victims, candidates, resident_candidates, num_resident_candidates,
		     needed_size);
    assert (num_candidates <= ALLOC_BATCH_SIZE);
    assert (num_candidates + num_resident_candidates <= ALLOC_BATCH_SIZE);

    // force-check candidates already resident in this shard and reuse them when possible.
    for (i = 0; i < num_resident_candidates; i++)
      {
	error = L1_find (class_oid, needed_size, consume_size,
			 resident_candidates[i].second / L2_FANOUT,
			 resident_candidates[i].second % L2_FANOUT, page_watcher, true);
	if (error == status::FOUND)
	  {
	    m_parent.push_candidates (candidates.data (), num_candidates);
	    for (j = i + 1; j < num_resident_candidates; j++)
	      {
		m_parent.push_candidates (&resident_candidates[j].first, 1);
	      }
	    return status::FOUND;
	  }
	if (error == status::FAILURE)
	  {
	    m_parent.push_candidates (candidates.data (), num_candidates);
	    for (j = i; j < num_resident_candidates; j++)
	      {
		m_parent.push_candidates (&resident_candidates[j].first, 1);
	      }
	    return status::FAILURE;
	  }
	if (error == status::CONTENDED)
	  {
	    m_parent.push_candidates (&resident_candidates[i].first, 1);
	  }
      }

    return status::NOT_FOUND;
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

  bestspace::status
  bestspace::shard::allocate_verify_or_allocate (OID *class_oid, HFID *hfid, std::uint16_t needed_size,
      std::array<bestspace_entry, ALLOC_BATCH_SIZE> &candidates, std::size_t &num_candidates, PGBUF_WATCHER &page_watcher)
  {
    bool candidate_valid;
    status result;
    int error;

    // check the biggest free space of candidates is enough
    if (num_candidates == ALLOC_BATCH_SIZE)
      {
	// first candidate is biggest
	result = allocate_verify_actual_space (class_oid, needed_size, candidates[0], candidate_valid, page_watcher);
	if (result == status::FAILURE)
	  {
	    return status::FAILURE;
	  }
	if (result == status::FOUND)
	  {
	    // the page returned to the caller is always stored at the last position.
	    std::swap (candidates[0], candidates[ALLOC_BATCH_SIZE - 1]);
	  }
	else
	  {
	    assert (result == status::NOT_FOUND);
	    assert (PGBUF_IS_CLEAN_WATCHER (&page_watcher));

	    if (candidate_valid)
	      {
		m_parent.push_candidates (&candidates[0], 1);
	      }
	    std::memmove (candidates.data (), candidates.data () + 1, sizeof (bestspace_entry) * (ALLOC_BATCH_SIZE - 1));
	    num_candidates--;
	  }
      }

    // allocate the pages at least one if the freespace of candidates is smaller than consume size
    if (num_candidates < ALLOC_BATCH_SIZE)
      {
	error = allocate_new_pages (hfid, num_candidates, candidates, page_watcher);
	if (error != NO_ERROR)
	  {
	    return status::FAILURE;
	  }
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

    error = heap_alloc_new_pages (thread_p, hfid, ALLOC_BATCH_SIZE - num_candidates, vpids.data (), &page_watcher);
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
  bestspace::shard::allocate_replace_pages (std::array<std::pair<std::uint16_t, std::uint16_t>, ALLOC_BATCH_SIZE>
      &victims, std::array<bestspace_entry, ALLOC_BATCH_SIZE> &candidates)
  {
    std::size_t i;
    L1 l1;

    // renew the L1s
    for (i = 0; i < ALLOC_BATCH_SIZE - 1; i++)
      {
	l1.set_freespace (candidates[i].freespace);
	l1.set_vpid ({ candidates[i].pageid, candidates[i].volid });
	m_L1[victims[i].first].store (l1);

	L2_update (victims[i].first / L2_FANOUT, victims[i].first % L2_FANOUT);
      }
    if (candidates[i].freespace > victims[i].second)
      {
	l1.set_freespace (candidates[i].freespace);
	l1.set_vpid ({ candidates[i].pageid, candidates[i].volid });
	m_L1[victims[i].first].store (l1);

	L2_update (victims[i].first / L2_FANOUT, victims[i].first % L2_FANOUT);
      }
  }

  bestspace::status
  bestspace::shard::allocate (OID *class_oid, HFID *hfid, std::uint16_t needed_size, std::uint16_t consume_size,
			      PGBUF_WATCHER &page_watcher)
  {
    std::array<VPID, (L3_FANOUT * L2_FANOUT) + ALLOC_BATCH_SIZE> residents;
    std::array<std::pair<std::uint16_t, std::uint16_t>, ALLOC_BATCH_SIZE> victims; // index, freespace
    std::array<bestspace_entry, ALLOC_BATCH_SIZE> candidates;
    std::size_t num_candidates;
    status result;

    if (m_parent.m_fcmap != nullptr)
      {
	return allocate_fc (class_oid, hfid, needed_size, consume_size, page_watcher);
      }

    // set allcating bit
    if (allocate_mark () != status::SUCCESS)
      {
	STATS_INC (advance_shard, 1);

	return status::ALLOCATING;
      }

    // pick four pages with the smallest free space as replacement victims.
    allocate_pick_victims (residents, victims);

    // use candidate as a target page or update a residents
    result = allocate_get_candidates_or_update_residents (class_oid, needed_size, consume_size, residents, victims,
	     candidates, num_candidates, page_watcher);
    if (result == status::FOUND || result == status::FAILURE)
      {
	allocate_unmark ();
	return result;
      }

    // verify the page has enough freespace and allocate new pages if not
    result = allocate_verify_or_allocate (class_oid, hfid, needed_size, candidates, num_candidates, page_watcher);
    if (result == status::FAILURE)
      {
	allocate_unmark ();
	return result;
      }

    // reserve the page
    candidates[ALLOC_BATCH_SIZE - 1].freespace -= consume_size;

    // replace L1s
    allocate_replace_pages (victims, candidates);

    allocate_unmark ();
    return status::FOUND;
  }

  bestspace::status
  bestspace::shard::allocate_fc (OID *class_oid, HFID *hfid, std::uint16_t needed_size, std::uint16_t consume_size,
				 PGBUF_WATCHER &page_watcher)
  {
    std::array<VPID, (L3_FANOUT * L2_FANOUT) + ALLOC_BATCH_SIZE> residents;
    std::array<std::pair<std::uint16_t, std::uint16_t>, ALLOC_BATCH_SIZE> victims; // index, freespace
    std::array<bestspace_entry, ALLOC_BATCH_SIZE> claimed;
    std::array<bestspace_entry, ALLOC_BATCH_SIZE> candidates;
    fcmap *map = m_parent.m_fcmap;
    std::size_t num_claimed, num_candidates, fit_index, i, j;
    std::uint8_t min_code;
    bool found, valid;
    status result;
    int error;

    if (allocate_mark () != status::SUCCESS)
      {
	STATS_INC (advance_shard, 1);

	return status::ALLOCATING;
      }

    // pick four pages with the smallest free space as replacement victims.
    allocate_pick_victims (residents, victims);

    // claim pages whose tier is above the request's: their lower bound fits it when the map is accurate.
    // a page of the same tier may or may not fit (tiers are 8~15% wide), so it is left for smaller requests;
    // only a request in the top tier has nothing above it and tries its own tier.
    min_code = fcmap::code_of (needed_size);
    if (min_code < fcmap::CODE_MAX_TIER)
      {
	num_claimed = map->claim (min_code + 1, claimed.data (), ALLOC_BATCH_SIZE);
      }
    else
      {
	num_claimed = map->claim (fcmap::CODE_MAX_TIER, claimed.data (), ALLOC_BATCH_SIZE);
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
	    fc_dbg_set ({ claimed[i].pageid, claimed[i].volid }, 3, claimed[i].freespace, needed_size);
	    found = true;
	    fit_index = num_candidates;
	    candidates[num_candidates++] = claimed[i];
	    map->count (fcmap::CNT_REUSED);
	    continue;
	  }
	if (result == status::FAILURE)
	  {
	    for (j = i; j < num_claimed; j++)
	      {
		map->release ({ claimed[j].pageid, claimed[j].volid }, fcmap::code_of (claimed[j].freespace), false);
	      }
	    allocate_unmark ();
	    return status::FAILURE;
	  }

	assert (result == status::NOT_FOUND);
	map->count (fcmap::CNT_VERIFY_FAIL);
	if (valid)
	  {
	    // the page was measured: record its exact tier
	    map->release ({ claimed[i].pageid, claimed[i].volid }, fcmap::code_of (claimed[i].freespace), true);
	  }
	else
	  {
	    map->remove ({ claimed[i].pageid, claimed[i].volid });
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
	fc_dbg_set ({ candidates[ALLOC_BATCH_SIZE - 1].pageid, candidates[ALLOC_BATCH_SIZE - 1].volid }, 4,
		    candidates[ALLOC_BATCH_SIZE - 1].freespace, needed_size);
	map->count (fcmap::CNT_ALLOC_BATCH);
	// only the latched page was measured; the others are new and empty
	for (i = 0; i < ALLOC_BATCH_SIZE - 1; i++)
	  {
	    candidates[i].freespace = fcmap::optimistic_freespace (fcmap::CODE_MAX_TIER);
	  }
	for (i = 0; i < ALLOC_BATCH_SIZE; i++)
	  {
	    map->set_resident ({ candidates[i].pageid, candidates[i].volid }, fcmap::code_of (candidates[i].freespace));
	  }

	if (candidates[ALLOC_BATCH_SIZE - 1].freespace < needed_size)
	  {
	    // heap_alloc_new_pages links the pages into the heap before it latches the first one again, so another
	    // inserter that reached it through an old hint may have used it meanwhile. keep the batch in the hot
	    // cache and serve the request from it instead.
	    cubthread::entry *thread_p = thread_get_thread_entry_info ();
	    tier minimum;

	    map->count (fcmap::CNT_VERIFY_FAIL);
	    pgbuf_ordered_unfix (thread_p, &page_watcher);
	    allocate_install_fc (victims, candidates, ALLOC_BATCH_SIZE);
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

    allocate_install_fc (victims, candidates, num_candidates);

    allocate_unmark ();
    return status::FOUND;
  }

  void
  bestspace::shard::allocate_install_fc (std::array<std::pair<std::uint16_t, std::uint16_t>, ALLOC_BATCH_SIZE> &victims,
					 std::array<bestspace_entry, ALLOC_BATCH_SIZE> &candidates, std::size_t num_candidates)
  {
    fcmap *map = m_parent.m_fcmap;
    std::size_t i, slot;
    VPID old_vpid;
    L1 old, l1;

    for (i = 0; i < num_candidates; i++)
      {
	slot = victims[i].first;
	if (i == num_candidates - 1 && candidates[i].freespace <= victims[i].second)
	  {
	    // the returned page has less room than the victim: keep the victim, the page stays in the map
	    map->release ({ candidates[i].pageid, candidates[i].volid }, fcmap::code_of (candidates[i].freespace), true);
	    continue;
	  }

	old = m_L1[slot].load ();
	l1.set_freespace (candidates[i].freespace);
	l1.set_vpid ({ candidates[i].pageid, candidates[i].volid });
	m_L1[slot].store (l1);

	L2_update (slot / L2_FANOUT, slot % L2_FANOUT);

	// write the evicted page back to the map so it is never forgotten
	old_vpid = old.get_vpid ();
	if (!VPID_ISNULL (&old_vpid))
	  {
	    map->release (old_vpid, fcmap::code_of (old.get_freespace ()), false);
	  }
      }
  }

  //////////////////////////////////////////////////////////////////////////
  // POC full coverage map
  //////////////////////////////////////////////////////////////////////////

  static inline std::size_t
  fcmap_floor_log2 (std::size_t v)
  {
    return 63 - __builtin_clzll (static_cast<unsigned long long> (v));
  }

  static inline std::uint64_t
  fcmap_hash (std::uint64_t x)
  {
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
  }

  bestspace::fcmap::fcmap ()
    : m_num_sectors (0)
    , m_index (nullptr)
    , m_retired ()
    , m_grow_mutex ()
    , m_cursor (0)
    , m_bytes (0)
  {
    std::size_t i;

    for (i = 0; i < MAX_SEGMENTS; i++)
      {
	m_segments[i].store (nullptr);
      }
    for (i = 0; i < CNT_LAST; i++)
      {
	m_counters[i].store (0);
      }
  }

  bestspace::fcmap::~fcmap ()
  {
    std::size_t i;
    index_table *table;

    for (i = 0; i < MAX_SEGMENTS; i++)
      {
	delete[] m_segments[i].load ();
      }
    table = m_index.load ();
    if (table != nullptr)
      {
	m_retired.push_back (table);
      }
    for (index_table *t : m_retired)
      {
	delete[] t->keys;
	delete[] t->values;
	delete t;
      }
  }

  std::uint8_t
  bestspace::fcmap::code_of (std::uint16_t freespace)
  {
    tier fs;

    fs = size_to_tier (MIN (freespace, static_cast<std::uint16_t> (DB_PAGESIZE)));
    if (fs == tier::FS0)
      {
	return CODE_NONE;
      }
    return static_cast<std::uint8_t> (static_cast<std::int8_t> (fs) + 1);
  }

  std::uint16_t
  bestspace::fcmap::optimistic_freespace (std::uint8_t code)
  {
    // the upper bound of each tier; a too-high value is corrected the first time the page is fixed
    static constexpr std::int16_t upper[] = { 0, 15, 24, 34, 45, 57, 70, 84, 100 };
    int bytes, capacity;

    if (code == CODE_NONE || code > CODE_MAX_TIER)
      {
	return 0;
      }
    bytes = upper[code] * DB_PAGESIZE / 100;
    capacity = heap_nonheader_page_capacity ();
    return static_cast<std::uint16_t> (MIN (bytes, capacity));
  }

  std::uint64_t
  bestspace::fcmap::make_key (VPID vpid)
  {
    std::uint64_t sectid;

    sectid = static_cast<std::uint64_t> (static_cast<std::uint32_t> (vpid.pageid)) / PAGES_PER_SECTOR;
    return ((static_cast<std::uint64_t> (static_cast<std::uint16_t> (vpid.volid)) + 1) << 32) | sectid;
  }

  std::uint8_t
  bestspace::fcmap::get_code (record *rec, std::size_t offset)
  {
    return static_cast<std::uint8_t> ((rec->codes[offset / 16].load () >> ((offset % 16) * 4)) & 0xF);
  }

  bestspace::fcmap::group *
  bestspace::fcmap::group_at (std::size_t group_index)
  {
    std::size_t k;
    group *segment;

    k = fcmap_floor_log2 (group_index + 1);
    segment = m_segments[k].load (std::memory_order_acquire);
    assert (segment != nullptr);
    return segment + (group_index + 1 - (static_cast<std::size_t> (1) << k));
  }

  bestspace::fcmap::record *
  bestspace::fcmap::sector_at (std::size_t sector_index)
  {
    return &group_at (sector_index / SECTORS_PER_GROUP)->sectors[sector_index % SECTORS_PER_GROUP];
  }

  bestspace::fcmap::record *
  bestspace::fcmap::find_sector (std::uint64_t key, std::size_t &sector_index)
  {
    index_table *table;
    std::uint64_t k;
    std::size_t i;

    table = m_index.load (std::memory_order_acquire);
    if (table == nullptr)
      {
	return nullptr;
      }
    for (i = fcmap_hash (key) & table->mask; ; i = (i + 1) & table->mask)
      {
	k = table->keys[i].load (std::memory_order_acquire);
	if (k == key)
	  {
	    sector_index = table->values[i];
	    return sector_at (sector_index);
	  }
	if (k == 0)
	  {
	    return nullptr;
	  }
      }
  }

  void
  bestspace::fcmap::index_insert (std::uint64_t key, std::uint32_t value)
  {
    index_table *table, *grown;
    std::size_t entries, capacity, i, pos;
    std::uint64_t k;

    // caller holds m_grow_mutex
    table = m_index.load (std::memory_order_relaxed);
    entries = m_num_sectors.load (std::memory_order_relaxed) + 1;
    if (table == nullptr || entries * 2 > table->mask + 1)
      {
	capacity = (table == nullptr) ? 64 : (table->mask + 1) * 2;
	while (entries * 2 > capacity)
	  {
	    capacity *= 2;
	  }
	grown = new index_table;
	grown->mask = capacity - 1;
	grown->keys = new std::atomic<std::uint64_t>[capacity] ();
	grown->values = new std::uint32_t[capacity] ();
	m_bytes += sizeof (index_table) + capacity * (sizeof (std::uint64_t) + sizeof (std::uint32_t));
	if (table != nullptr)
	  {
	    for (i = 0; i <= table->mask; i++)
	      {
		k = table->keys[i].load (std::memory_order_relaxed);
		if (k == 0)
		  {
		    continue;
		  }
		for (pos = fcmap_hash (k) & grown->mask; grown->keys[pos].load (std::memory_order_relaxed) != 0;
		     pos = (pos + 1) & grown->mask);
		grown->values[pos] = table->values[i];
		grown->keys[pos].store (k, std::memory_order_relaxed);
	      }
	    m_retired.push_back (table);	// readers may still hold it; freed with the map
	  }
	m_index.store (grown, std::memory_order_release);
	table = grown;
      }

    for (pos = fcmap_hash (key) & table->mask; table->keys[pos].load (std::memory_order_relaxed) != 0;
	 pos = (pos + 1) & table->mask);
    table->values[pos] = value;
    table->keys[pos].store (key, std::memory_order_release);
  }

  bestspace::fcmap::record *
  bestspace::fcmap::get_or_add_sector (std::uint64_t key, std::size_t &sector_index)
  {
    std::size_t s, g, k, n, i;
    group *segment;
    record *rec;

    rec = find_sector (key, sector_index);
    if (rec != nullptr)
      {
	return rec;
      }

    std::lock_guard<std::mutex> lock (m_grow_mutex);

    rec = find_sector (key, sector_index);
    if (rec != nullptr)
      {
	return rec;
      }

    s = m_num_sectors.load (std::memory_order_relaxed);
    g = s / SECTORS_PER_GROUP;
    k = fcmap_floor_log2 (g + 1);
    if (k >= MAX_SEGMENTS)
      {
	return nullptr;
      }
    if (m_segments[k].load (std::memory_order_relaxed) == nullptr)
      {
	n = static_cast<std::size_t> (1) << k;
	segment = new group[n] ();
	if (segment == nullptr)
	  {
	    return nullptr;
	  }
	m_bytes += n * sizeof (group);
	m_segments[k].store (segment, std::memory_order_release);
      }

    rec = sector_at (s);
    rec->key = key;
    for (i = 0; i < 4; i++)
      {
	// pages of a new sector are not heap data pages until they are allocated or fed back
	rec->codes[i].store (0xEEEEEEEEEEEEEEEEULL, std::memory_order_relaxed);
      }
    rec->resident.store (0, std::memory_order_relaxed);
    rec->dirty.store (0, std::memory_order_relaxed);
    rec->version.store (0, std::memory_order_relaxed);

    index_insert (key, static_cast<std::uint32_t> (s));
    m_num_sectors.store (s + 1, std::memory_order_release);

    sector_index = s;
    return rec;
  }

  void
  bestspace::fcmap::publish (record *rec, std::size_t sector_index, std::size_t offset)
  {
    std::uint8_t code;

    // bump the version before the hint bit so a concurrent claim that clears the bit can see the change
    rec->version.fetch_add (1);
    code = get_code (rec, offset);
    if (code >= 1 && code <= CODE_MAX_TIER && (rec->resident.load () & (1ULL << offset)) == 0)
      {
	group_at (sector_index / SECTORS_PER_GROUP)->bits[code - 1].fetch_or (1ULL << (sector_index % SECTORS_PER_GROUP));
      }
  }

  void
  bestspace::fcmap::set_code (record *rec, std::size_t sector_index, std::size_t offset, std::uint8_t code)
  {
    std::size_t word, shift;
    std::uint64_t expected, desired;

    word = offset / 16;
    shift = (offset % 16) * 4;
    expected = rec->codes[word].load ();
    do
      {
	desired = (expected & ~(0xFULL << shift)) | (static_cast<std::uint64_t> (code) << shift);
	if (desired == expected)
	  {
	    break;
	  }
      }
    while (!rec->codes[word].compare_exchange_weak (expected, desired));

    publish (rec, sector_index, offset);
  }

  void
  bestspace::fcmap::feedback (VPID vpid, std::uint8_t code)
  {
    std::size_t s, offset;
    record *rec;

    rec = get_or_add_sector (make_key (vpid), s);
    if (rec == nullptr)
      {
	return;
      }
    offset = static_cast<std::uint32_t> (vpid.pageid) % PAGES_PER_SECTOR;
    set_code (rec, s, offset, code);
    // a shard that releases this page later keeps this value instead of its own stale one
    rec->dirty.fetch_or (1ULL << offset);
    count (CNT_FEEDBACK);
  }

  void
  bestspace::fcmap::set_resident (VPID vpid, std::uint8_t code)
  {
    std::size_t s, offset;
    std::uint64_t bit;
    record *rec;

    rec = get_or_add_sector (make_key (vpid), s);
    if (rec == nullptr)
      {
	return;
      }
    offset = static_cast<std::uint32_t> (vpid.pageid) % PAGES_PER_SECTOR;
    bit = 1ULL << offset;
    rec->resident.fetch_or (bit);
    rec->dirty.fetch_and (~bit);
    set_code (rec, s, offset, code);
  }

  void
  bestspace::fcmap::release (VPID vpid, std::uint8_t code, bool measured)
  {
    std::size_t s, offset;
    std::uint64_t bit;
    bool dirty;
    record *rec;

    rec = get_or_add_sector (make_key (vpid), s);
    if (rec == nullptr)
      {
	return;
      }
    offset = static_cast<std::uint32_t> (vpid.pageid) % PAGES_PER_SECTOR;
    bit = 1ULL << offset;
    dirty = (rec->dirty.fetch_and (~bit) & bit) != 0;
    rec->resident.fetch_and (~bit);
    if (measured || !dirty)
      {
	set_code (rec, s, offset, code);
      }
    else
      {
	publish (rec, s, offset);
      }
    count (CNT_RELEASED);
  }

  void
  bestspace::fcmap::remove (VPID vpid)
  {
    std::size_t s, offset;
    std::uint64_t bit;
    record *rec;

    rec = find_sector (make_key (vpid), s);
    if (rec == nullptr)
      {
	return;
      }
    offset = static_cast<std::uint32_t> (vpid.pageid) % PAGES_PER_SECTOR;
    bit = 1ULL << offset;
    rec->resident.fetch_and (~bit);
    rec->dirty.fetch_and (~bit);
    set_code (rec, s, offset, CODE_NOT_DATA);
  }

  void
  bestspace::fcmap::clear_all_resident ()
  {
    std::size_t n, s, offset;
    std::uint64_t resident;
    record *rec;

    n = m_num_sectors.load (std::memory_order_acquire);
    for (s = 0; s < n; s++)
      {
	rec = sector_at (s);
	resident = rec->resident.exchange (0);
	rec->dirty.store (0);
	while (resident != 0)
	  {
	    offset = __builtin_ctzll (resident);
	    resident &= resident - 1;
	    publish (rec, s, offset);
	  }
      }
  }

  std::size_t
  bestspace::fcmap::claim (std::uint8_t min_code, bestspace_entry *out, std::size_t max_count)
  {
    std::size_t n, num_groups, start, found, gi, g, si, s, offset, p;
    std::uint64_t mask, bit, sbit, resident, version;
    bool left[CODE_MAX_TIER + 1];
    std::uint8_t c;
    group *grp;
    record *rec;

    assert (min_code >= 1 && min_code <= CODE_MAX_TIER);

    n = m_num_sectors.load (std::memory_order_acquire);
    if (n == 0 || max_count == 0)
      {
	return 0;
      }
    num_groups = (n + SECTORS_PER_GROUP - 1) / SECTORS_PER_GROUP;
    start = m_cursor.load (std::memory_order_relaxed);
    if (start >= num_groups)
      {
	start = 0;
      }

    found = 0;
    for (gi = 0; gi < num_groups && found < max_count; gi++)
      {
	g = (start + gi) % num_groups;
	grp = group_at (g);
	mask = 0;
	for (c = min_code; c <= CODE_MAX_TIER; c++)
	  {
	    mask |= grp->bits[c - 1].load ();
	  }

	while (mask != 0 && found < max_count)
	  {
	    si = __builtin_ctzll (mask);
	    mask &= mask - 1;
	    s = g * SECTORS_PER_GROUP + si;
	    if (s >= n)
	      {
		break;
	      }
	    rec = &grp->sectors[si];
	    version = rec->version.load ();

	    for (offset = 0; offset < PAGES_PER_SECTOR && found < max_count; offset++)
	      {
		c = get_code (rec, offset);
		bit = 1ULL << offset;
		if (c < min_code || c > CODE_MAX_TIER || (rec->resident.load () & bit) != 0)
		  {
		    continue;
		  }
		if ((rec->resident.fetch_or (bit) & bit) != 0)
		  {
		    continue;
		  }
		rec->dirty.fetch_and (~bit);
		c = get_code (rec, offset);
		out[found].volid = static_cast<short> ((rec->key >> 32) - 1);
		out[found].pageid = static_cast<std::int32_t> ((rec->key & 0xFFFFFFFFULL) * PAGES_PER_SECTOR + offset);
		out[found].freespace = optimistic_freespace (c);
		found++;
	      }

	    if (offset == PAGES_PER_SECTOR)
	      {
		// the whole sector was scanned: drop the hint bits of codes with no claimable page left
		for (c = 0; c <= CODE_MAX_TIER; c++)
		  {
		    left[c] = false;
		  }
		resident = rec->resident.load ();
		for (p = 0; p < PAGES_PER_SECTOR; p++)
		  {
		    if ((resident & (1ULL << p)) == 0)
		      {
			c = get_code (rec, p);
			if (c >= 1 && c <= CODE_MAX_TIER)
			  {
			    left[c] = true;
			  }
		      }
		  }
		sbit = 1ULL << si;
		for (c = min_code; c <= CODE_MAX_TIER; c++)
		  {
		    if (!left[c] && (grp->bits[c - 1].load () & sbit) != 0)
		      {
			grp->bits[c - 1].fetch_and (~sbit);
			if (rec->version.load () != version)
			  {
			    grp->bits[c - 1].fetch_or (sbit);
			  }
		      }
		  }
	      }
	  }

	if (found > 0)
	  {
	    m_cursor.store (g, std::memory_order_relaxed);
	  }
      }

    count (CNT_CLAIMED, found);
    return found;
  }

  std::size_t
  bestspace::fcmap::collect (bestspace_entry *out, std::size_t max_count)
  {
    std::size_t n, s, offset, found;
    std::uint8_t c, low, high;
    std::uint64_t resident;
    record *rec;
    int pass;

    n = m_num_sectors.load (std::memory_order_acquire);
    found = 0;
    for (pass = 0; pass < 2 && found < max_count; pass++)
      {
	low = (pass == 0) ? 5 : 1;
	high = (pass == 0) ? CODE_MAX_TIER : 4;
	for (s = 0; s < n && found < max_count; s++)
	  {
	    rec = sector_at (s);
	    resident = rec->resident.load ();
	    for (offset = 0; offset < PAGES_PER_SECTOR && found < max_count; offset++)
	      {
		c = get_code (rec, offset);
		if (c < low || c > high || (resident & (1ULL << offset)) != 0)
		  {
		    continue;
		  }
		out[found].volid = static_cast<short> ((rec->key >> 32) - 1);
		out[found].pageid = static_cast<std::int32_t> ((rec->key & 0xFFFFFFFFULL) * PAGES_PER_SECTOR + offset);
		out[found].freespace = optimistic_freespace (c);
		found++;
	      }
	  }
      }
    return found;
  }

  void
  bestspace::fcmap::get_usage (std::size_t &num_sectors, std::size_t &bytes, std::uint64_t counters[CNT_LAST])
  {
    std::size_t i;

    num_sectors = m_num_sectors.load ();
    {
      std::lock_guard<std::mutex> lock (m_grow_mutex);
      bytes = m_bytes;
    }
    for (i = 0; i < CNT_LAST; i++)
      {
	counters[i] = m_counters[i].load ();
      }
  }

  bestspace::candidate_queue::candidate_queue ()
    : m_size (0)
    , m_max_freespace (0)
    , m_mutex ()
  {
    std::size_t i;

    std::lock_guard<std::mutex> lock (m_mutex);

    for (i = 0; i < MAX_CANDIDATES_QUEUE_SIZE; i++)
      {
	m_array[i].set_null ();
      }
  }

  void
  bestspace::candidate_queue::reset ()
  {
    std::size_t i;

    std::lock_guard<std::mutex> lock (m_mutex);

    for (i = 0; i < MAX_CANDIDATES_QUEUE_SIZE; i++)
      {
	m_array[i].set_null ();
      }
    m_size = 0;
    m_max_freespace.store (0, std::memory_order_release);
  }

  bool
  bestspace::candidate_queue::try_push (bestspace_entry candidate)
  {
    std::unique_lock<std::mutex> ulock (m_mutex, std::try_to_lock);

    // try to acquire
    if (!ulock.owns_lock ())
      {
	return false;
      }

    remove_if_exist (candidate);

    // not enough space to be candidate
    if (m_size == MAX_CANDIDATES_QUEUE_SIZE && candidate.freespace <= m_array[0].freespace)
      {
	return true;
      }

    insert (candidate);
    m_max_freespace.store (m_array[m_size - 1].freespace, std::memory_order_release);

    return true;
  }

  void
  bestspace::candidate_queue::push (bestspace_entry candidate)
  {
    std::lock_guard<std::mutex> lock (m_mutex);

    remove_if_exist (candidate);

    // not enough space to be candidate
    if (m_size == MAX_CANDIDATES_QUEUE_SIZE && candidate.freespace <= m_array[0].freespace)
      {
	return;
      }

    insert (candidate);
    m_max_freespace.store (m_array[m_size - 1].freespace, std::memory_order_release);
  }

  bool
  bestspace::candidate_queue::pop (bestspace_entry &candidate, std::uint16_t needed_size)
  {
    if (m_max_freespace.load (std::memory_order_acquire) < needed_size)
      {
	return false;
      }

    std::lock_guard<std::mutex> lock (m_mutex);

    if (m_size == 0 || m_array[m_size - 1].freespace < needed_size)
      {
	return false;
      }

    candidate = m_array[m_size - 1];
    m_size--;
    m_max_freespace.store (m_size == 0 ? 0 : m_array[m_size - 1].freespace, std::memory_order_release);
    return true;
  }

  std::size_t
  bestspace::candidate_queue::pop (bestspace_entry *candidates, std::uint16_t minimum, std::uint16_t needed_size)
  {
    std::size_t num;
    std::size_t i;

    if (m_max_freespace.load (std::memory_order_acquire) <= minimum)
      {
	return 0;
      }

    std::lock_guard<std::mutex> lock (m_mutex);

    if (m_size == 0)
      {
	return 0;
      }

    num = (m_array[m_size - 1].freespace >= needed_size) ? ALLOC_BATCH_SIZE : ALLOC_BATCH_SIZE - 1;
    for (i = 0; i < num && m_size > 0 && m_array[m_size - 1].freespace > minimum; i++)
      {
	candidates[i] = m_array[m_size - 1];
	m_size--;
      }
    m_max_freespace.store (m_size == 0 ? 0 : m_array[m_size - 1].freespace, std::memory_order_release);
    return i;
  }

  std::size_t
  bestspace::candidate_queue::to_entries (bestspace_entry *candidates)
  {
    std::size_t i;

    std::lock_guard<std::mutex> lock (m_mutex);

    assert (candidates != nullptr);
    assert (m_size <= MAX_CANDIDATES_QUEUE_SIZE);

    for (i = 0; i < m_size; i++)
      {
	candidates[i] = m_array[m_size - i - 1];
      }
    return m_size;
  }

  void
  bestspace::candidate_queue::remove_if_exist (bestspace_entry &candidate)
  {
    int i;

    // remove the duplicate if the candidate already exists
    for (i = 0; i < static_cast<int> (m_size); i++)
      {
	if (candidate.volid == m_array[i].volid && candidate.pageid == m_array[i].pageid)
	  {
	    std::memmove (m_array.data () + i, m_array.data () + i + 1, sizeof (bestspace_entry) * (m_size - i - 1));
	    m_size--;
	    break;
	  }
      }
  }

  void
  bestspace::candidate_queue::insert (bestspace_entry &candidate)
  {
    int i;

    // remove the duplicate if the candidate already exists
    for (i = 0; i < static_cast<int> (m_size); i++)
      {
	if (candidate.freespace < m_array[i].freespace)
	  {
	    break;
	  }
      }

    if (m_size == MAX_CANDIDATES_QUEUE_SIZE)
      {
	assert (i != 0);

	if (i > 1)
	  {
	    std::memmove (m_array.data (), m_array.data () + 1, sizeof (bestspace_entry) * (i - 1));
	  }
	m_array[i - 1] = candidate;
      }
    else
      {
	if (i != static_cast<int> (m_size))
	  {
	    std::memmove (m_array.data () + i + 1, m_array.data () + i, sizeof (bestspace_entry) * (m_size - i));
	  }
	m_array[i] = candidate;
	m_size++;
      }
  }

  bestspace::bestspace (std::size_t shard_count, int num_pages, std::uint64_t recs_num, std::uint64_t recs_sumlen,
			std::uint16_t unfill_space)
    : m_shards ()
    , m_fcmap (prm_get_bool_value (PRM_ID_BESTSPACE_FULL_COVERAGE) ? new fcmap () : nullptr)
    , m_distributed_insert (prm_get_bool_value (PRM_ID_BESTSPACE_DISTRIBUTED_INSERT))
    , m_unfill_space (unfill_space)
    , m_num_pages (num_pages)
    , m_recs_num (recs_num)
    , m_recs_sumlen (recs_sumlen)
  {
    assert (shard_count > 0);

    // last updated time
    m_last_updated.store (monotonic_seconds ());

    // create shards
    for (std::size_t i = 0; i < shard_count; i++)
      {
	m_shards.emplace_back (*this);
      }
  }

  bestspace::~bestspace ()
  {
    delete m_fcmap;
  }

  void
  bestspace::get_fc_usage (std::size_t &num_sectors, std::size_t &bytes, std::uint64_t *counters)
  {
    std::size_t i;

    static_assert (FC_NUM_COUNTERS == static_cast<std::size_t> (fcmap::CNT_LAST), "FC_NUM_COUNTERS must match");

    num_sectors = 0;
    bytes = 0;
    for (i = 0; i < fcmap::CNT_LAST; i++)
      {
	counters[i] = 0;
      }
    if (m_fcmap != nullptr)
      {
	m_fcmap->get_usage (num_sectors, bytes, counters);
      }
  }

  void
  bestspace::forget_page (VPID vpid)
  {
    if (m_fcmap != nullptr)
      {
	m_fcmap->remove (vpid);
      }
  }

  void
  bestspace::reset (const bestspace_entry *entries, std::size_t num_entries)
  {
    std::array<bestspace_entry, ENTRIES_PER_SHARD> shard_entries;
    std::size_t entries_to_copy;
    std::size_t entry_index;
    std::size_t i, j;

    assert (num_entries <= m_shards.size () * ENTRIES_PER_SHARD);

    // candidates
    m_candidates.reset ();

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

    if (m_fcmap != nullptr)
      {
	m_fcmap->clear_all_resident ();
	for (i = 0; i < num_entries; i++)
	  {
	    if (entries[i].pageid != NULL_PAGEID && entries[i].volid != NULL_VOLID)
	      {
		m_fcmap->set_resident ({ entries[i].pageid, entries[i].volid }, fcmap::code_of (entries[i].freespace));
	      }
	  }
      }
  }

  void
  bestspace::try_push_candidates (bestspace_entry *candidates, std::size_t num_candidates)
  {
    std::size_t i;

    if (m_fcmap != nullptr)
      {
	push_candidates (candidates, num_candidates);
	return;
      }

    for (i = 0; i < num_candidates; i++)
      {
	m_candidates.try_push (candidates[i]);
      }
  }

  void
  bestspace::push_candidates (bestspace_entry *candidates, std::size_t num_candidates)
  {
    std::size_t i;

    if (m_fcmap != nullptr)
      {
	for (i = 0; i < num_candidates; i++)
	  {
	    m_fcmap->feedback ({ candidates[i].pageid, candidates[i].volid }, fcmap::code_of (candidates[i].freespace));
	  }
	return;
      }

    for (i = 0; i < num_candidates; i++)
      {
	m_candidates.push (candidates[i]);
      }
  }

  std::size_t
  bestspace::pop_candidates (bestspace_entry *candidates, std::uint16_t minimum, std::uint16_t needed_size)
  {
    return m_candidates.pop (candidates, minimum, needed_size);
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
    std::size_t num_checked_candidates;
    bestspace_entry candidate;
    bool candidate_valid;
    status result;
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

    num_checked_candidates = 0;
    while (m_fcmap == nullptr && num_checked_candidates < MAX_CANDIDATES_QUEUE_SIZE
	   && m_candidates.pop (candidate, needed_size))
      {
	result =
		m_shards[shard].find_candidate (class_oid, needed_size, consume_size, candidate, candidate_valid, page_watcher);
	if (result == status::FOUND)
	  {
	    m_candidates.push (candidate);
	    m_shards[shard].add_estimates (0, is_newrec ? 1 : 0, consume_size - SPAGE_SLOT_SIZE);
	    return NO_ERROR;
	  }
	if (result == status::FAILURE)
	  {
	    ASSERT_ERROR ();
	    errid = er_errid ();
	    return errid != NO_ERROR ? errid : ER_FAILED;
	  }

	assert (result == status::NOT_FOUND);
	assert (PGBUF_IS_CLEAN_WATCHER (&page_watcher));
	if (candidate_valid)
	  {
	    m_candidates.push (candidate);
	  }
	num_checked_candidates++;
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
  bestspace::to_entries (bestspace_entry *entries, bestspace_entry *candidates, std::size_t &num_candidates)
  {
    std::size_t i;

    for (i = 0; i < m_shards.size (); i++)
      {
	m_shards[i].to_entries (entries + i * ENTRIES_PER_SHARD);
      }

    if (m_fcmap != nullptr)
      {
	num_candidates = m_fcmap->collect (candidates, MAX_CANDIDATES_QUEUE_SIZE);
	return;
      }
    num_candidates = m_candidates.to_entries (candidates);
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

  void
  bestspace_registry::create (HFID *hfid, std::size_t shard_count, bestspace_entry *entries, std::size_t num_entries,
			      bestspace_entry *candidates, std::size_t num_candidates, int num_pages, std::uint64_t recs_num,
			      std::uint64_t recs_sumlen, std::uint16_t unfill_space)
  {
    registry_entry *node;

    node = new registry_entry;
    node->hfid = *hfid;
    node->entry = new bestspace (shard_count, num_pages, recs_num, recs_sumlen, unfill_space);
    node->entry->reset (entries, num_entries);
    node->entry->push_candidates (candidates, num_candidates);

    std::lock_guard<std::mutex> lock (m_mutex);

    if (m_cache_count == 0)
      {
	m_cache_count = MAX (static_cast<std::size_t> (prm_get_integer_value (PRM_ID_BESTSPACE_CACHE_COUNT)),
			     static_cast<std::size_t> (1));
      }

    assert (!find_entry (m_head, hfid));
    insert_entry (m_head, node);
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
