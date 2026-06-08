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
#include "page_buffer.h"
#include "slotted_page.h"
#include "error_manager.h"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace cubstorage
{
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
    assert (index < 8);

    m_bits |= (0x1 << index);
  }

  void
  bestspace::bitmap::clear (std::size_t index)
  {
    assert (index < 8);

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

  bestspace::status
  bestspace::shard::find (std::uint16_t size, std::size_t bias, PAGE_PTR &pgptr)
  {
    status error;
    tier minimum;

    // convert and advance
    minimum = size_to_tier (size);
    if (minimum < tier::FS8)
      {
	minimum++;
      }

    error = L3_find (minimum, size, bias, false, pgptr);
    if (error == status::CONTENDED)
      {
	error = L3_find (minimum, size, bias, true, pgptr);
      }

    assert (error != status::CONTENDED);

    if (error == status::FOUND || error == status::FAILURE)
      {
	return error;
      }

    assert (error == status::NOT_FOUND);

    // TODO: need to allocate
    return status::NOT_FOUND;
  }

  bestspace::status
  bestspace::shard::L3_find (tier minimum, std::uint16_t size, std::size_t bias, bool wait, PAGE_PTR &pgptr)
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
	    error = L2_find (minimum, size, pos[ (i + bias) % length], bias, wait, pgptr);
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
  bestspace::shard::L2_find (tier minimum, std::uint16_t size, std::size_t l2_index, std::size_t bias, bool wait,
			     PAGE_PTR &pgptr)
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
	    error = L1_find (size, l2_index, pos[ (i + bias) % length], wait, pgptr);
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

	l1 = m_L1[l2_index * 8 + l1_index].load ();
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
  bestspace::shard::L1_find (std::uint16_t size, std::size_t l2_index, std::size_t l1_index, bool wait, PAGE_PTR &pgptr)
  {
    cubthread::entry *thread_p;
    std::size_t freespace;
    VPID vpid, old_vpid;
    L1 expected, desired;

    // first, check the recorded free space
    expected = m_L1[l2_index * 8 + l1_index].load ();
    if (expected.get_freespace () < size)
      {
	// there is no enough space
	return status::NOT_FOUND;
      }

    // now, fix a page to check the actual free space
    thread_p = thread_get_thread_entry_info ();
    old_vpid = expected.get_vpid ();
    pgptr = pgbuf_fix (thread_p, &old_vpid, OLD_PAGE_MAYBE_DEALLOCATED, PGBUF_LATCH_WRITE,
		       wait ? PGBUF_UNCONDITIONAL_LATCH : PGBUF_CONDITIONAL_LATCH);
    if (pgptr == NULL)
      {
	switch (er_errid ())
	  {
	  case NO_ERROR:
	    return wait ? status::NOT_FOUND : status::CONTENDED;

	  case ER_PB_BAD_PAGEID:
	    L1_remove (l2_index, l1_index, expected);
	    [[fallthrough]];

	  case ER_LK_PAGE_TIMEOUT:
	    er_clear ();
	    return status::NOT_FOUND;

	  case ER_INTERRUPTED:
	    return status::FAILURE;
	  }

	return status::FAILURE;
      }

    // L1 might be changed
    expected = m_L1[l2_index * 8 + l1_index].load ();
    // newest
    vpid = expected.get_vpid ();
    // store the old and get the actual free space
    freespace = spage_max_space_for_new_record (thread_p, pgptr);
    if (freespace < size)
      {
	// there is no enough space and the free space information is wrong
	if (VPID_EQ (&vpid, &old_vpid))
	  {
	    desired = expected;
	    desired.set_freespace (freespace);

	    // and I'm the only one that can modify this L1
	    if (m_L1[l2_index * 8 + l1_index].compare_exchange_strong (expected, desired))
	      {
		L2_update (l2_index, l1_index);
	      }
	  }
	pgbuf_unfix (thread_p, pgptr);
	pgptr = NULL;

	return status::NOT_FOUND;
      }

    // there is enough space
    if (VPID_EQ (&vpid, &old_vpid))
      {
	desired = expected;
	desired.set_freespace (freespace - size);

	// and I'm the only one that can modify this L1
	if (m_L1[l2_index * 8 + l1_index].compare_exchange_strong (expected, desired))
	  {
	    L2_update (l2_index, l1_index);
	  }
      }
    return status::FOUND;
  }

  void
  bestspace::shard::L1_remove (std::size_t l2_index, std::size_t l1_index, L1 expected)
  {
    L1 desired;

    desired.set_freespace (0);
    desired.set_vpid (vpid_Null_vpid);
    if (m_L1[l2_index * 8 + l1_index].compare_exchange_strong (expected, desired))
      {
	L2_update (l2_index, l1_index);
      }
  }

  bestspace::bestspace () noexcept
    : m_shard ()
  {
  }

  int
  bestspace::find (cubthread::entry &thread_ref, std::uint16_t size, PAGE_PTR &pgptr)
  {
    std::size_t shard, bias;
    status error;
    int errid;
    std::size_t i;

    assert (size > 0 && size < DB_PAGESIZE);
    pgptr = NULL;

    // early return or clear stale error to avoid error corruption in below path
    errid = er_errid_if_has_error ();
    if (errid != NO_ERROR)
      {
	return errid;
      }
    er_clear ();

    shard = thread_ref.index % 8;
    bias = thread_ref.tran_index < 0 ? -thread_ref.tran_index : thread_ref.tran_index;

    for (i = 0; i < 8; i++)
      {
	error = m_shard[shard].find (size, bias, pgptr);
	if (error == status::FAILURE)
	  {
	    ASSERT_ERROR ();
	    return er_errid ();
	  }
      }
    // TODO: NOT FOUND? OR CAN'T ALLOCATE?

    // TODO: advance to next shard
    return NO_ERROR;
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

}
