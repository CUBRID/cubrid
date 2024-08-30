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

//
// MVCC active transactions map
//

#include "mvcc_active_tran.hpp"

#include "log_impl.h"

#include <cstring>
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

mvcc_active_tran::mvcc_active_tran ()
  : m_bit_area (NULL)
  , m_bit_area_start_mvccid (MVCCID_FIRST)
  , m_bit_area_length (0)
  , m_long_tran_mvccids (NULL)
  , m_long_tran_mvccids_length (0)
  , m_initialized (false)
{
}

mvcc_active_tran::~mvcc_active_tran ()
{
  delete [] m_bit_area;
  delete [] m_long_tran_mvccids;
}

void
mvcc_active_tran::initialize ()
{
  if (m_initialized)
    {
      return;
    }
  m_bit_area = new unit_type[BITAREA_MAX_SIZE] ();
  m_bit_area_start_mvccid = MVCCID_FIRST;
  m_bit_area_length = 0;
  m_long_tran_mvccids = new MVCCID[long_tran_max_size ()] ();
  m_long_tran_mvccids_length = 0;
  m_initialized = true;
}

void
mvcc_active_tran::finalize ()
{
  delete [] m_bit_area;
  m_bit_area = NULL;

  delete [] m_long_tran_mvccids;
  m_long_tran_mvccids = NULL;

  m_initialized = false;
}

void
mvcc_active_tran::reset ()
{
  if (!m_initialized)
    {
      return;
    }
  if (m_bit_area_length > 0)
    {
      // clear bits
      std::memset (m_bit_area, 0, get_bit_area_memsize ());
    }
  m_bit_area_length = 0;
  m_bit_area_start_mvccid = MVCCID_NULL;
  m_long_tran_mvccids_length = 0;

  check_valid ();
}

MVCCID
mvcc_active_tran::get_bit_area_start_mvccid ()
{
  return m_bit_area_start_mvccid;
}

size_t
mvcc_active_tran::long_tran_max_size ()
{
  return logtb_get_number_of_total_tran_indices ();
}

size_t
mvcc_active_tran::bit_size_to_unit_size (size_t bit_count)
{
  return (bit_count + UNIT_BIT_COUNT - 1) / UNIT_BIT_COUNT;
}

size_t
mvcc_active_tran::units_to_bits (size_t unit_count)
{
  return unit_count * UNIT_BIT_COUNT;
}

size_t
mvcc_active_tran::units_to_bytes (size_t unit_count)
{
  return unit_count * UNIT_BYTE_COUNT;
}

mvcc_active_tran::unit_type
mvcc_active_tran::get_mask_of (size_t bit_offset)
{
  return ((unit_type) 1) << (bit_offset & 0x3F);
}

size_t
mvcc_active_tran::get_bit_offset (MVCCID mvccid) const
{
  return static_cast<size_t> (mvccid - m_bit_area_start_mvccid);
}

MVCCID
mvcc_active_tran::get_mvccid (size_t bit_offset) const
{
  return m_bit_area_start_mvccid + bit_offset;
}

mvcc_active_tran::unit_type *
mvcc_active_tran::get_unit_of (size_t bit_offset) const
{
  return m_bit_area + (bit_offset / UNIT_BIT_COUNT);
}

bool
mvcc_active_tran::is_set (size_t bit_offset) const
{
  return ((*get_unit_of (bit_offset)) & get_mask_of (bit_offset)) != 0;
}

size_t
mvcc_active_tran::get_area_size () const
{
  return bit_size_to_unit_size (m_bit_area_length);
}

size_t
mvcc_active_tran::get_bit_area_memsize () const
{
  return units_to_bytes (get_area_size ());
}

size_t
mvcc_active_tran::get_long_tran_memsize () const
{
  return m_long_tran_mvccids_length * sizeof (MVCCID);
}

MVCCID
mvcc_active_tran::compute_highest_completed_mvccid () const
{
  assert (m_bit_area != NULL && m_bit_area_start_mvccid >= MVCCID_FIRST);

  if (m_bit_area_length == 0)
    {
      return m_bit_area_start_mvccid - 1;
    }

  /* compute highest highest_bit_pos and highest_completed_bit_area */
  size_t end_position = m_bit_area_length - 1;
  unit_type *highest_completed_bit_area;
  size_t highest_bit_position;
  unit_type bits;
  size_t bit_pos;
  size_t count_bits;

  for (highest_completed_bit_area = get_unit_of (end_position); highest_completed_bit_area >= m_bit_area;
       --highest_completed_bit_area)
    {
      bits = *highest_completed_bit_area;
      if (bits == 0)
	{
	  continue;
	}
      for (bit_pos = 0, count_bits = UNIT_BIT_COUNT / 2; count_bits > 0; count_bits /= 2)
	{
	  if (bits >= (1ULL << count_bits))
	    {
	      bit_pos += count_bits;
	      bits >>= count_bits;
	    }
	}
      assert (bit_pos < UNIT_BIT_COUNT);
      highest_bit_position = bit_pos;
      break;
    }
  if (highest_completed_bit_area < m_bit_area)
    {
      // not found
      return m_bit_area_start_mvccid - 1;
    }
  else
    {
      return get_mvccid (units_to_bits (highest_completed_bit_area - m_bit_area) + highest_bit_position);
    }
}

MVCCID
mvcc_active_tran::compute_lowest_active_mvccid () const
{
  assert (m_bit_area != NULL);

  if (m_long_tran_mvccids_length > 0 && m_long_tran_mvccids != NULL)
    {
      /* long time transactions are ordered */
      return m_long_tran_mvccids[0];
    }

  if (m_bit_area_length == 0)
    {
      return m_bit_area_start_mvccid;
    }

  /* find the lowest bit 0 */
  size_t end_position = m_bit_area_length - 1;
  unit_type *end_bit_area = get_unit_of (end_position);
  unit_type *lowest_active_bit_area;
  size_t lowest_bit_pos = 0;
  unit_type bits;
  size_t bit_pos;
  size_t count_bits;
  unit_type mask;

  for (lowest_active_bit_area = m_bit_area; lowest_active_bit_area <= end_bit_area; ++lowest_active_bit_area)
    {
      bits = *lowest_active_bit_area;
      if (bits == ALL_COMMITTED)
	{
	  lowest_bit_pos += UNIT_BIT_COUNT;
	  continue;
	}
      /* find least significant bit 0 position */
      for (bit_pos = 0, count_bits = UNIT_BIT_COUNT / 2; count_bits > 0; count_bits /= 2)
	{
	  mask = (1ULL << count_bits) - 1;
	  if ((bits & mask) == mask)
	    {
	      bit_pos += count_bits;
	      bits >>= count_bits;
	    }
	}
      lowest_bit_pos += bit_pos;
      break;
    }
  /* compute lowest_active_mvccid */
  if (lowest_active_bit_area > end_bit_area)
    {
      /* didn't find 0 bit */
      return get_mvccid (m_bit_area_length);
    }
  else
    {
      return get_mvccid (lowest_bit_pos);
    }
}

void
mvcc_active_tran::copy_to (mvcc_active_tran &dest, copy_safety safety) const
{
  assert (m_initialized && dest.m_initialized);

  if (safety == copy_safety::THREAD_SAFE)
    {
      check_valid ();
      dest.check_valid ();
    }

  size_t new_bit_area_memsize = get_bit_area_memsize ();
  size_t old_bit_area_memsize = dest.get_bit_area_memsize ();
  char *dest_bit_area = (char *) dest.m_bit_area;

  if (new_bit_area_memsize > 0)
    {
      std::memcpy (dest_bit_area, m_bit_area, new_bit_area_memsize);
    }
  if (old_bit_area_memsize > new_bit_area_memsize)
    {
      // clear
      std::memset (dest_bit_area + new_bit_area_memsize, 0, old_bit_area_memsize - new_bit_area_memsize);
    }
  if (m_long_tran_mvccids_length > 0)
    {
      std::memcpy (dest.m_long_tran_mvccids, m_long_tran_mvccids, get_long_tran_memsize ());
    }

  dest.m_bit_area_start_mvccid = m_bit_area_start_mvccid;
  dest.m_bit_area_length = m_bit_area_length;
  dest.m_long_tran_mvccids_length = m_long_tran_mvccids_length;

  if (safety == copy_safety::THREAD_SAFE)
    {
      dest.check_valid ();
    }
}

bool
mvcc_active_tran::is_active (MVCCID mvccid) const
{
  if (MVCC_ID_PRECEDES (mvccid, m_bit_area_start_mvccid))
    {
      /* check long time transactions */
      if (m_long_tran_mvccids != NULL)
	{
	  for (size_t i = 0; i < m_long_tran_mvccids_length; i++)
	    {
	      if (mvccid == m_long_tran_mvccids[i])
		{
		  return true;
		}
	    }
	}
      // is committed
      return false;
    }
  else if (m_bit_area_length == 0)
    {
      return true;
    }
  else
    {
      size_t position = get_bit_offset (mvccid);
      if (position < m_bit_area_length)
	{
	  return !is_set (position);
	}
      else
	{
	  return true;
	}
    }
}

void
mvcc_active_tran::remove_long_transaction (MVCCID mvccid)
{
  /* Safe guard: */
  assert (m_long_tran_mvccids_length > 0);

  size_t i;
  for (i = 0; i < m_long_tran_mvccids_length - 1; i++)
    {
      if (m_long_tran_mvccids[i] == mvccid)
	{
	  size_t memsize = (m_long_tran_mvccids_length - i - 1) * sizeof (MVCCID);
	  std::memmove (&m_long_tran_mvccids[i], &m_long_tran_mvccids[i + 1], memsize);
	  break;
	}
    }
  assert ((i < m_long_tran_mvccids_length - 1) || m_long_tran_mvccids[i] == mvccid);
  --m_long_tran_mvccids_length;

  check_valid ();
}

void
mvcc_active_tran::add_long_transaction (MVCCID mvccid)
{
  assert (m_long_tran_mvccids_length < long_tran_max_size ());
  assert (m_long_tran_mvccids_length == 0 || m_long_tran_mvccids[m_long_tran_mvccids_length - 1] < mvccid);
  m_long_tran_mvccids[m_long_tran_mvccids_length++] = mvccid;
}

void
mvcc_active_tran::ltrim_area (size_t trim_size)
{
  if (trim_size == 0)
    {
      return;
    }
  size_t new_memsize = (get_area_size () - trim_size) * sizeof (unit_type);
  if (new_memsize > 0)
    {
      std::memmove (m_bit_area, &m_bit_area[trim_size], new_memsize);
    }
  size_t trimmed_bits = units_to_bits (trim_size);
  m_bit_area_length -= trimmed_bits;
  m_bit_area_start_mvccid += trimmed_bits;
  // clear moved units
  std::memset (&m_bit_area[get_area_size ()], ALL_ACTIVE, trim_size * sizeof (unit_type));
#if !defined (NDEBUG)
  // verify untouched units are also zero
  for (size_t i = get_area_size () + trim_size; i < BITAREA_MAX_SIZE; i++)
    {
      assert (m_bit_area[i] == ALL_ACTIVE);
    }
#endif // DEBUG

  assert (new_memsize == get_bit_area_memsize ());
}

void
mvcc_active_tran::set_bitarea_mvccid (MVCCID mvccid)
{
  const size_t CLEANUP_THRESHOLD = UNIT_BIT_COUNT;
  const size_t LONG_TRAN_THRESHOLD = BITAREA_MAX_BITS - long_tran_max_size ();

  assert (mvccid >= m_bit_area_start_mvccid);
  size_t position = get_bit_offset (mvccid);
  if (position >= BITAREA_MAX_BITS)
    {
      // force cleanup_migrate_to_long_transations
      cleanup_migrate_to_long_transations ();
      position = get_bit_offset (mvccid);
    }
  assert (position < BITAREA_MAX_BITS);   // is this a guaranteed?
  if (position >= m_bit_area_length)
    {
      // extend area size; it is enough to update bit_area_length since all data is already zero
      m_bit_area_length = position + 1;
    }

  unit_type mask = get_mask_of (position);
  unit_type *p_area = get_unit_of (position);
  *p_area |= mask;

  check_valid ();

  if (m_bit_area_length > CLEANUP_THRESHOLD)
    {
      // trim all committed units from bit_area
      size_t first_not_all_committed;
      for (first_not_all_committed = 0; first_not_all_committed < get_area_size (); first_not_all_committed++)
	{
	  if (m_bit_area[first_not_all_committed] != ALL_COMMITTED)
	    {
	      break;
	    }
	}
      ltrim_area (first_not_all_committed);
      check_valid ();
    }

  if (m_bit_area_length > LONG_TRAN_THRESHOLD)
    {
      cleanup_migrate_to_long_transations ();
    }
}

void
mvcc_active_tran::cleanup_migrate_to_long_transations ()
{
  const size_t BITAREA_SIZE_AFTER_CLEANUP = 16;
  size_t delete_count = get_area_size () - BITAREA_SIZE_AFTER_CLEANUP;
  unit_type bits;
  unit_type mask;
  size_t bit_pos;
  MVCCID long_tran_mvccid;

  for (size_t i = 0; i < delete_count; i++)
    {
      bits = m_bit_area[i];
      // iterate on bits and find active MVCCID's
      for (bit_pos = 0, mask = 1, long_tran_mvccid = get_mvccid (i * UNIT_BIT_COUNT);
	   bit_pos < UNIT_BIT_COUNT && bits != ALL_COMMITTED;
	   ++bit_pos, mask <<= 1, ++long_tran_mvccid)
	{
	  if ((bits & mask) == 0)
	    {
	      add_long_transaction (long_tran_mvccid);
	      /* set the bit to in order to break faster */
	      bits |= mask;
	    }
	}
    }
  ltrim_area (delete_count);

  check_valid ();
}

void
mvcc_active_tran::set_inactive_mvccid (MVCCID mvccid)
{
  /* check whether is long transaction */
  if (MVCC_ID_PRECEDES (mvccid, m_bit_area_start_mvccid))
    {
      remove_long_transaction (mvccid);
    }
  else
    {
      set_bitarea_mvccid (mvccid);
    }
}

void
mvcc_active_tran::reset_start_mvccid (MVCCID mvccid)
{
  m_bit_area_start_mvccid = mvccid;

  if (m_initialized)
    {
      check_valid ();
    }
}

void
mvcc_active_tran::reset_active_transactions ()
{
  std::memset (m_bit_area, 0, BITAREA_MAX_MEMSIZE);
  m_bit_area_length = 0;
  m_long_tran_mvccids_length = 0;
}

void
mvcc_active_tran::check_valid () const
{
#if !defined (NDEBUG)
  // all bits after bit_area_length must be 0
  if ((m_bit_area_length % UNIT_BIT_COUNT) != 0)
    {
      // we need to test bits after bit_area_length in same unit
      size_t last_bit_pos = m_bit_area_length - 1;
      unit_type last_unit = *get_unit_of (last_bit_pos);
      for (size_t i = (last_bit_pos + 1) ; i < UNIT_BIT_COUNT; i++)
	{
	  if ((get_mask_of (i) & last_unit) != 0)
	    {
	      assert (false);
	    }
	}
    }
  for (unit_type *p_area = get_unit_of (m_bit_area_length) + 1; p_area < m_bit_area + BITAREA_MAX_SIZE; ++p_area)
    {
      if (*p_area != ALL_ACTIVE)
	{
	  assert (false);
	}
    }

  // all long transaction should be ordered and smaller than bit_area_start_mvccid
  for (size_t i = 0; i < m_long_tran_mvccids_length; i++)
    {
      assert (MVCC_ID_PRECEDES (m_long_tran_mvccids[i], m_bit_area_start_mvccid));
      assert (i == 0 || MVCC_ID_PRECEDES (m_long_tran_mvccids[i - 1], m_long_tran_mvccids[i]));
    }
#endif // debug
}
