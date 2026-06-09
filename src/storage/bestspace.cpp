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
// bestspace.cpp
//

#include "bestspace.hpp"
#include "xserver_interface.h"
#include "heap_file.h"
#include "slotted_page.h"
#include "error_manager.h"

#include <mutex>
#include <utility>
#include <cstdint>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

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

    return m_freespace[static_cast<std::size_t> (minimum)].find (pos, BITS_PER_BYTE - 1);
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

    assert (index < BITS_PER_BYTE - 1);

    std::memcpy (&val, m_freespace.data (), sizeof (uint64_t));
    val &= ~ (0x0101010101010101ULL << index);
    std::memcpy (static_cast<void *> (m_freespace.data ()), &val, sizeof (uint64_t));
  }

  void
  bestspace::L3::set (tier fs, std::size_t index)
  {
    assert (fs > tier::FS0);
    assert (index < BITS_PER_BYTE - 1);

    m_freespace[static_cast<std::size_t> (fs)].set (index);
  }

  bestspace::shard::shard () noexcept
    : m_L3 ()
    , m_L2 ()
    , m_L1 ()
    , m_recs_num (0)
    , m_recs_sumlen (0)
  {
  }

  bool
  bestspace::L3::is_allocating ()
  {
    uint64_t value;

    std::memcpy (&value, m_freespace.data (), sizeof (uint64_t));

    return (value & FLAG_ALLOCATING);
  }

  void
  bestspace::L3::clear_allocating ()
  {
    uint64_t value;

    std::memcpy (&value, m_freespace.data (), sizeof (uint64_t));
    value &= ~FLAG_ALLOCATING;
    std::memcpy (static_cast<void *> (m_freespace.data ()), &value, sizeof (uint64_t));
  }

  void
  bestspace::L3::set_allocating ()
  {
    uint64_t value;

    std::memcpy (&value, m_freespace.data (), sizeof (uint64_t));
    value |= FLAG_ALLOCATING;
    std::memcpy (static_cast<void *> (m_freespace.data ()), &value, sizeof (uint64_t));
  }

  void
  bestspace::shard::initialize_by_entries (bestspace_entry entries[L3_FANOUT * L2_FANOUT])
  {
    std::array<tier, BITS_PER_BYTE> tiers;
    std::size_t length;
    std::size_t i, j;
    tier fs;
    L3 l3;
    L2 l2;
    L1 l1;

    // L1
    for (i = 0; i < L3_FANOUT * L2_FANOUT; i++)
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
  bestspace::shard::find (HFID *hfid, std::uint16_t size, std::size_t bias, PGBUF_WATCHER &page_watcher)
  {
    status error;
    tier minimum;

    // convert and advance
    minimum = size_to_tier (size);
    if (minimum < tier::FS8)
      {
	minimum++;
      }

    error = L3_find (minimum, size, bias, page_watcher);
    if (error == status::FOUND || error == status::FAILURE)
      {
	return error;
      }

    assert (error == status::NOT_FOUND || error == status::CONTENDED);

    return allocate (hfid, size, page_watcher);
  }

  bestspace::status
  bestspace::shard::L3_find (tier minimum, std::uint16_t size, std::size_t bias, PGBUF_WATCHER &page_watcher)
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
	length = l3.find (minimum, pos);
	for (i = 0; i < length; i++)
	  {
	    error = L2_find (minimum, size, pos[ (i + bias) % length], bias, page_watcher);
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
  }

  bestspace::status
  bestspace::shard::L2_find (tier minimum, std::uint16_t size, std::size_t l2_index, std::size_t bias,
			     PGBUF_WATCHER &page_watcher)
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
	length = l2.find (minimum, pos);
	for (i = 0; i < length; i++)
	  {
	    error = L1_find (size, l2_index, pos[ (i + bias) % length], page_watcher);
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
    L2 expected, desired;
    L1 l1;
    tier tier_to;

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

    L3_update (l2_index);
  }

  bestspace::status
  bestspace::shard::L1_find (std::uint16_t size, std::size_t l2_index, std::size_t l1_index, PGBUF_WATCHER &page_watcher)
  {
    cubthread::entry *thread_p;
    std::size_t freespace;
    VPID vpid, old_vpid;
    L1 expected, desired;
    status error;

    assert (PGBUF_IS_CLEAN_WATCHER (&page_watcher));

    // first, check the recorded free space
    expected = m_L1[l2_index * L2_FANOUT + l1_index].load ();
    if (expected.get_freespace () < size)
      {
	// there is no enough space
	return status::NOT_FOUND;
      }

    // now, fix a page to check the actual free space
    old_vpid = expected.get_vpid ();

    error = L1_fix (l2_index, l1_index, expected, old_vpid, page_watcher);
    if (error != status::SUCCESS)
      {
	return error;
      }

    // L1 might be changed
    expected = m_L1[l2_index * L2_FANOUT + l1_index].load ();
    // newest
    vpid = expected.get_vpid ();
    // store the old and get the actual free space
    thread_p = thread_get_thread_entry_info ();
    freespace = spage_max_space_for_new_record (thread_p, page_watcher.pgptr);
    if (freespace < size)
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
	desired.set_freespace (freespace - size);

	// and I'm the only one that can modify this L1
	if (m_L1[l2_index * L2_FANOUT + l1_index].compare_exchange_strong (expected, desired))
	  {
	    L2_update (l2_index, l1_index);
	  }
      }
    return status::FOUND;
  }

  bestspace::status
  bestspace::shard::L1_fix (std::size_t l2_index, std::size_t l1_index, L1 l1, VPID vpid, PGBUF_WATCHER &page_watcher)
  {
    cubthread::entry *thread_p;
    int wait_msecs;
    int error_code;

    thread_p = thread_get_thread_entry_info ();
    wait_msecs = xlogtb_reset_wait_msecs (thread_p, LK_FORCE_ZERO_WAIT);
    error_code = pgbuf_ordered_fix (thread_p, &vpid, OLD_PAGE_MAYBE_DEALLOCATED, PGBUF_LATCH_WRITE, &page_watcher);
    (void) xlogtb_reset_wait_msecs (thread_p, wait_msecs);
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
      }
  }

  bestspace::status
  bestspace::shard::allocate_mark ()
  {
    L3 expected, desired;

    expected = m_L3.load ();
    do
      {
	desired = expected;
	if (desired.is_allocating ())
	  {
	    return status::ALLOCATING;
	  }
	desired.set_allocating ();
      }
    while (!m_L3.compare_exchange_strong (expected, desired));

    return status::SUCCESS;
  }

  void
  bestspace::shard::allocate_unmark ()
  {
    // update L3 and clear allocating bit
    L3 expected, desired;

    expected = m_L3.load ();
    do
      {
	desired = expected;
	assert (desired.is_allocating ());
	desired.clear_allocating ();
      }
    while (!m_L3.compare_exchange_strong (expected, desired));
  }

  void
  bestspace::shard::allocate_pages (cubthread::entry &thread_ref, std::uint16_t size,
				    std::array<VPID, ALLOC_BATCH_SIZE> &vpids, PGBUF_WATCHER &page_watcher)
  {
    std::array<std::pair<std::uint16_t, std::uint16_t>, ALLOC_BATCH_SIZE> result;
    std::size_t pos, offset;
    std::size_t i;
    int freespace;
    L1 l1;

    // get 8 indices from L1, ordered by smallest free space
    result.fill (std::make_pair (std::numeric_limits<std::uint16_t>::max (), std::numeric_limits<std::uint16_t>::max ()));
    for (i = 0; i < L3_FANOUT * L2_FANOUT; i++)
      {
	l1 = m_L1[i].load ();
	freespace = l1.get_freespace ();

	if (freespace >= result[ALLOC_BATCH_SIZE - 1].second)
	  {
	    continue;
	  }

	pos = ALLOC_BATCH_SIZE - 1;
	while (pos > 0 && freespace < result[pos - 1].second)
	  {
	    result[pos] = result[pos - 1];
	    pos--;
	  }

	result[pos] = std::make_pair (i, freespace);
      }

    // renew the L1s
    offset = 0;
    freespace = spage_max_space_for_new_record (&thread_ref, page_watcher.pgptr);
    if (freespace - static_cast<int> (size) > static_cast<int> (result[0].second))
      {
	l1.set_freespace (freespace - size);
	l1.set_vpid (vpids[0]);
	m_L1[result[0].first].store (l1);

	L2_update (result[0].first / L2_FANOUT, result[0].first % L2_FANOUT);
	offset++;
      }
    for (i = 1; i < ALLOC_BATCH_SIZE; i++)
      {
	l1.set_freespace (freespace);
	l1.set_vpid (vpids[i]);
	m_L1[result[i - (1 - offset)].first].store (l1);

	L2_update (result[i - (1 - offset)].first / L2_FANOUT, result[i - (1 - offset)].first % L2_FANOUT);
      }
  }

  bestspace::status
  bestspace::shard::allocate (HFID *hfid, std::uint16_t size, PGBUF_WATCHER &page_watcher)
  {
    std::array<VPID, ALLOC_BATCH_SIZE> vpids;
    cubthread::entry *thread_p;
    int error;

    // set allcating bit
    if (allocate_mark () != status::SUCCESS)
      {
	return status::ALLOCATING;
      }

    thread_p = thread_get_thread_entry_info ();
    error = heap_alloc_new_pages (thread_p, hfid, ALLOC_BATCH_SIZE, vpids.data (), &page_watcher);
    if (error != NO_ERROR)
      {
	allocate_unmark ();
	return status::FAILURE;
      }

    allocate_pages (*thread_p, size, vpids, page_watcher);
    allocate_unmark ();
    return status::FOUND;
  }

  bestspace::bestspace () noexcept
    : m_shard ()
  {
  }

  void
  bestspace::initialize_by_entries (bestspace_entry entries[SHARD_COUNT][L3_FANOUT * L2_FANOUT])
  {
    std::size_t i;

    for (i = 0; i < SHARD_COUNT; i++)
      {
	m_shard[i].initialize_by_entries (entries[i]);
      }
  }

  int
  bestspace::find (cubthread::entry &thread_ref, HFID *hfid, std::uint16_t size, PGBUF_WATCHER &page_watcher)
  {
    std::size_t shard, bias;
    std::size_t i;
    std::size_t retry;
    status error;
    int errid;

    assert (size > 0 && size < DB_PAGESIZE);

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

    PGBUF_INIT_WATCHER (&page_watcher, PGBUF_ORDERED_HEAP_NORMAL, hfid);

    retry = 0;
    shard = thread_ref.index % SHARD_COUNT;
    bias = thread_ref.tran_index < 0 ? -thread_ref.tran_index : thread_ref.tran_index;
    while (true)
      {
	for (i = 0; i < SHARD_COUNT; i++)
	  {
	    error = m_shard[ (shard + i) % SHARD_COUNT].find (hfid, size, bias, page_watcher);
	    assert (error == status::FOUND ||
		    error == status::ALLOCATING ||
		    error == status::FAILURE);
	    if (error == status::FOUND)
	      {
		return NO_ERROR;
	      }
	    if (error == status::FAILURE)
	      {
		ASSERT_ERROR ();
		return er_errid ();
	      }
	  }

	assert (error == status::ALLOCATING);

	// NOT FOUND AND CAN'T ALLOCATE
	if (retry < 20)
	  {
	    std::this_thread::yield ();
	  }
	else
	  {
	    std::this_thread::sleep_for (std::chrono::microseconds (10));
	  }
	retry++;
      }

    // impossible !
    assert (false);
    return ER_FAILED;
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

  //////////////////////////////////////////////////////////////////////////
  // bestspace register/unregister
  //////////////////////////////////////////////////////////////////////////

  bestspace_registry::bestspace_registry ()
    : m_head (nullptr)
    , m_mutex ()
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
  bestspace_registry::create (OID *class_oid, HFID *hfid)
  {
    registry_entry *node;

    node = new registry_entry;
    node->class_oid = *class_oid;
    node->hfid = *hfid;
    node->entry = new bestspace;

    std::lock_guard<std::mutex> lock (m_mutex);

    assert (!find_entry (m_head, class_oid, hfid));
    insert_entry (m_head, node);
  }

  void
  bestspace_registry::create (OID *class_oid, HFID *hfid,
			      bestspace_entry entries[bestspace::SHARD_COUNT][bestspace::L3_FANOUT * bestspace::L2_FANOUT])
  {
    registry_entry *node;

    node = new registry_entry;
    node->class_oid = *class_oid;
    node->hfid = *hfid;
    node->entry = new bestspace;
    node->entry->initialize_by_entries (entries);

    std::lock_guard<std::mutex> lock (m_mutex);

    assert (!find_entry (m_head, class_oid, hfid));
    insert_entry (m_head, node);
  }

  void
  bestspace_registry::destroy (OID *class_oid, VFID *vfid)
  {
    registry_entry *node;

    std::lock_guard<std::mutex> lock (m_mutex);

    node = get_node_from_list (m_head, class_oid, vfid);
    if (node)
      {
	destroy_entry (node);
      }
  }

  void
  bestspace_registry::destroy (OID *class_oid, HFID *hfid)
  {
    registry_entry *node;

    std::lock_guard<std::mutex> lock (m_mutex);

    node = get_node_from_list (m_head, class_oid, hfid);
    if (node)
      {
	destroy_entry (node);
      }
  }

  int
  bestspace_registry::find (cubthread::entry &thread_ref, HFID *hfid, std::uint16_t size, PGBUF_WATCHER &page_watcher)
  {
    if (heap_stats_find_best_page (&thread_ref, hfid, size, true, NULL, &page_watcher) == NULL)
      {
	return er_errid ();
      }
    return NO_ERROR;
  }

  int
  bestspace_registry::find (cubthread::entry &thread_ref, OID *class_oid, HFID *hfid, std::uint16_t size,
			    PGBUF_WATCHER &page_watcher)
  {
    int error;

    if (!class_oid || OID_ISNULL (class_oid) || class_oid->pageid == 193)
      {
	return find (thread_ref, hfid, size, page_watcher);
      }

    error = find_from_cache (thread_ref, class_oid, hfid, size, page_watcher);
    if (error != ER_MHT_NOTFOUND)
      {
	return error;
      }

    return find_from_global (thread_ref, class_oid, hfid, size, page_watcher);
  }

  int
  bestspace_registry::find_from_cache (cubthread::entry &thread_ref, OID *class_oid, HFID *hfid, std::uint16_t size,
				       PGBUF_WATCHER &page_watcher)
  {
    registry_entry *cache;

    cache = get_node_from_list (TLS_head, class_oid, hfid);
    if (!cache)
      {
	return ER_MHT_NOTFOUND;
      }

    // make this cache the first (LRU)
    insert_entry (TLS_head, cache);
    return (cache->entry)->find (thread_ref, hfid, size, page_watcher);
  }

  int
  bestspace_registry::find_from_global (cubthread::entry &thread_ref, OID *class_oid, HFID *hfid, std::uint16_t size,
					PGBUF_WATCHER &page_watcher)
  {
    registry_entry *cache;
    bestspace *entry;

    std::unique_lock<std::mutex> ulock (m_mutex);

    auto pair = find_entry (m_head, class_oid, hfid);
    if (!pair)
      {
	// invalid class oid and hfid
	return ER_MHT_NOTFOUND;
      }
    entry = (pair->second)->entry;

    ulock.unlock ();

    // register in TLS list
    if (TLS_size < TLS_MAX_SIZE)
      {
	cache = new registry_entry;
	cache->class_oid = *class_oid;
	cache->hfid = *hfid;
	cache->entry = entry;
	TLS_size++;
      }
    else
      {
	cache = get_tail_from_list (TLS_head);
	cache->class_oid = *class_oid;
	cache->hfid = *hfid;
	cache->entry = entry;
      }
    insert_entry (TLS_head, cache);
    return entry->find (thread_ref, hfid, size, page_watcher);
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
      bestspace_registry::find_entry (registry_entry *head, OID *class_oid, VFID *vfid)
  {
    registry_entry *prev;

    for (prev = nullptr; head; prev = head, head = head->next)
      {
	if (OID_EQ (&head->class_oid, class_oid) && VFID_EQ (&head->hfid.vfid, vfid))
	  {
	    return std::make_pair (prev, head);
	  }
      }
    return std::nullopt;
  }

  std::optional<std::pair<bestspace_registry::registry_entry *, bestspace_registry::registry_entry *>>
      bestspace_registry::find_entry (registry_entry *head, OID *class_oid, HFID *hfid)
  {
    registry_entry *prev;

    for (prev = nullptr; head; prev = head, head = head->next)
      {
	if (OID_EQ (&head->class_oid, class_oid) && HFID_EQ (&head->hfid, hfid))
	  {
	    return std::make_pair (prev, head);
	  }
      }
    return std::nullopt;
  }

  bestspace_registry::registry_entry *
  bestspace_registry::get_node_from_list (registry_entry *&head, OID *class_oid, VFID *vfid)
  {
    auto pair = find_entry (head, class_oid, vfid);
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
  bestspace_registry::get_node_from_list (registry_entry *&head, OID *class_oid, HFID *hfid)
  {
    auto pair = find_entry (head, class_oid, hfid);
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
