/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */

//
// MVCC active transactions map
//

#include "mvcc_active_tran.hpp"

#include <cstring>

mvcc_active_tran::mvcc_active_tran ()
  : bit_area (NULL)
  , bit_area_start_mvccid (MVCCID_FIRST)
  , bit_area_length (0)
  , long_tran_mvccids (NULL)
  , long_tran_mvccids_length (0)
  , m_initialized (false)
{
}

mvcc_active_tran::~mvcc_active_tran ()
{
  delete bit_area;
  delete long_tran_mvccids;
}

void
mvcc_active_tran::initialize ()
{
  if (m_initialized)
    {
      return;
    }
  bit_area = new bit_area_unit_type[BITAREA_MAX_SIZE];
  bit_area_start_mvccid = MVCCID_FIRST;
  bit_area_length = 0;
  long_tran_mvccids = new MVCCID[long_tran_max_size ()];
  long_tran_mvccids_length = 0;
  m_initialized = true;
}

void
mvcc_active_tran::finalize ()
{
  delete bit_area;
  bit_area = NULL;

  delete long_tran_mvccids;
  long_tran_mvccids = NULL;

  m_initialized = false;
}

size_t
mvcc_active_tran::long_tran_max_size ()
{
  return logtb_get_number_of_total_tran_indices ();
}

size_t
mvcc_active_tran::bit_size_to_unit_size (size_t bit_count)
{
  return (bit_count + UNIT_TO_BITS_COUNT - 1) / UNIT_TO_BITS_COUNT;
}

size_t
mvcc_active_tran::units_to_bits (size_t unit_count)
{
  return unit_count * UNIT_TO_BITS_COUNT;
}

size_t
mvcc_active_tran::units_to_bytes (size_t unit_count)
{
  return unit_count * UNIT_TO_BYTE_COUNT;
}

unit_type
mvcc_active_tran::get_mask_of (size_t bit_offset)
{
  return ((unit_type) 1) << (bit_offset & 0x3F);
}

size_t
mvcc_active_tran::get_bit_offset (MVCCID mvccid)
{
  return static_cast<size_t> (mvccid - bit_area_start_mvccid);
}

MVCCID
mvcc_active_tran::get_mvccid (size_t bit_offset)
{
  return bit_area_start_mvccid + bit_offset;
}

unit_type *
mvcc_active_tran::get_unit_of (size_t bit_offset) const
{
  return bit_area + (bit_offset / UNIT_TO_BITS_COUNT);
}

bool
mvcc_active_tran::is_set (size_t bit_offset) const
{
  return ((*get_unit_of (bit_offset)) & get_mask_of (bit_offset)) != 0;
}

size_t
mvcc_active_tran::get_area_size ()
{
  return bit_size_to_unit_size (bit_area_length);
}

size_t
mvcc_active_tran::get_bit_area_memsize () const
{
  return units_to_bytes (get_area_size ());
}

size_t
mvcc_active_tran::get_long_tran_memsize () const
{
  return long_tran_mvccids_length * sizeof (MVCCID);
}

MVCCID
mvcc_active_tran::get_highest_completed_mvccid () const
{
  assert (bit_area != NULL && bit_area_start_mvccid > MVCCID_FIRST);

  if (bit_area_length == 0)
    {
      return bit_area_start_mvccid - 1;
    }

  /* compute highest highest_bit_pos and highest_completed_bit_area */
  size_t end_position = bit_area_length - 1;
  unit_type *highest_completed_bit_area;
  size_t highest_bit_position;
  unit_type bits;
  size_t bit_pos;
  size_t count_bits;

  for (highest_completed_bit_area = get_unit_of (end_position); highest_completed_bit_area >= bit_area;
       --highest_completed_bit_area)
    {
      bits = *highest_completed_bit_area;
      if (bits == 0)
	{
	  continue;
	}
      for (bit_pos = 0, count_bits = UNIT_TO_BITS_COUNT / 2; count_bits > 0; count_bits /= 2)
	{
	  if (bits >= (1ULL << count_bits))
	    {
	      bit_pos += count_bits;
	      bits >>= count_bits;
	    }
	}
      assert (bit_pos < MVCC_BITAREA_ELEMENT_BITS);
      highest_bit_position = bit_pos;
      break;
    }
}

MVCCID
mvcc_active_tran::get_lowest_active_mvccid ()
{
  assert (bit_area != NULL);

  if (long_tran_mvccids_length > 0 && long_tran_mvccids != NULL)
    {
      /* long time transactions are ordered */
      return long_tran_mvccids[0];
    }

  if (bit_area_length == 0)
    {
      return bit_area_start_mvccid;
    }

  /* find the lowest bit 0 */
  size_t end_position = bit_area_length - 1;
  unit_type *end_bit_area = get_unit_of (end_position);
  unit_type *lowest_active_bit_area = bit_area;
  size_t lowest_bit_pos = 0;
  unit_type bits;
  size_t bit_pos;
  size_t count_bits;
  unit_type mask;

  while (lowest_active_bit_area <= end_bit_area)
    {
      bits = *lowest_active_bit_area;
      if (bits == ALL_COMMITTED)
	{
	  lowest_bit_pos += UNIT_TO_BITS_COUNT;
	  continue;
	}
      /* find least significant bit 0 position */
      for (bit_pos = 0, count_bits = UNIT_TO_BITS_COUNT / 2; count_bits > 0; count_bits /= 2)
	{
	  mask = (1ULL << count_bits) - 1;
	  if ((bits & mask) == mask)
	    {
	      bit_pos += count_bits;
	      bits >>= count_bits;
	    }
	}
      lowest_bit_pos += bit_pos;
    }
  /* compute lowest_active_mvccid */
  if (lowest_active_bit_area > end_bit_area)
    {
      /* didn't fount 0 bit */
      return get_mvccid (bit_area_length);
    }
  else
    {
      return get_mvccid (lowest_bit_pos);
    }
}

void
mvcc_active_tran::copy_to (mvcc_active_tran &dest) const
{
  assert (m_initialized && dest.m_initialized);

  dest.bit_area_start_mvccid = bit_area_start_mvccid;
  dest.bit_area_length = bit_area_length;
  dest.long_tran_mvccids_length = long_tran_mvccids_length;

  if (dest.bit_area_length > 0)
    {
      std::memcpy (dest.bit_area, bit_area, dest.get_bit_area_memsize ());
    }
  if (dest.long_tran_mvccids_length > 0)
    {
      std::memcpy (dest.long_tran_mvccids, long_tran_mvccids, dest.get_long_tran_memsize ());
    }
}

bool
mvcc_active_tran::is_active (MVCCID mvccid) const
{
  version_type local_version = version.load ();
  MVCCID local_bit_area_start_mvccid = bit_area_start_mvccid.load ();
  size_t local_bit_area_length = bit_area_length.load ();

  if (MVCC_ID_PRECEDES (mvccid, local_bit_area_start_mvccid))
    {
      /* check long time transactions */
      if (long_tran_mvccids != NULL)
	{
	  for (size_t i = 0; i < long_tran_mvccids_length; i++)
	    {
	      if (mvccid == long_tran_mvccids[i])
		{
		  return true;
		}
	    }
	}
      // is committed
      return false;
    }
  else if (local_bit_area_length == 0)
    {
      return true;
    }
  else
    {
      size_t position = get_bit_offset (mvccid);
      if (position < local_bit_area_length)
	{
	  return is_set (position);
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
  assert (long_tran_mvccids_length > 0)
  for (size_t i = 0; i < long_tran_mvccids_length - 1; i++)
    {
      if (long_tran_mvccids[i] == mvccid)
	{
	  size_t memsize = (current_trans_status->long_tran_mvccids_length - i - 1) * sizeof (MVCCID);
	  std::memmove (&current_trans_status->long_tran_mvccids[i], &current_trans_status->long_tran_mvccids[i + 1],
			memsize);
	  break;
	}
    }
  assert ((i < long_tran_mvccids_length - 1) || long_tran_mvccids[i] == mvccid);
  --long_tran_mvccids_length;
}

void
mvcc_active_tran::add_long_transaction (MVCCID mvccid)
{
  assert (long_tran_mvccids_length < long_tran_max_size ());
  assert (long_tran_mvccids[long_tran_mvccids_length - 1] < mvccid);
  long_tran_mvccids[long_tran_mvccids_length++] = mvccid;
}

void
mvcc_active_tran::ltrim_area (size_t trim_size)
{
  if (trim_size == 0)
    {
      return;
    }
  size_t new_memsize = (get_area_size () - trim_size - 1) * sizeof (unit_type);
  if (new_memsize > 0)
    {
      std::memmove (bit_area, &bit_area[trim_size], new_memsize);
    }
  size_t trimmed_bits = units_to_bits (trim_size);
  bit_area_length -= trimmed_bits;
  bit_area_start_mvccid += trimmed_bits;

  assert (new_memsize == get_bit_area_memsize ());
}

void
mvcc_active_tran::set_bitarea_mvccid (MVCCID mvccid)
{
  const size_t CLEANUP_THRESHOLD = UNIT_TO_BITS_COUNT;
  const size_t LONG_TRAN_THRESHOLD = BITAREA_MAX_BITS - long_tran_max_size ();

  assert (mvccid >= bit_area_start_mvccid);
  size_t position = get_bit_offset (mvccid);
  if (position >= BITAREA_MAX_BITS)
    {
      // force cleanup
      cleanup ();
      position = get_bit_offset (mvccid);
      assert (position < BITAREA_MAX_BITS);   // is this a guaranteed?
    }

  unit_type mask = get_mask_of (position);
  unit_type *p_area = get_unit_of (position);
  *p_area |= mask;

  if (bit_area_length > CLEANUP_THRESHOLD)
    {
      // trim all committed units from bit_area
      size_t first_not_all_commited;
      for (first_not_all_commited = 0; i < get_area_size (); i++)
	{
	  if (bit_area[first_not_all_commited] != ALL_COMMITTED)
	    {
	      break;
	    }
	}
      ltrim_area (first_not_all_commited);
    }

  if (bit_area_length > LONG_TRAN_THRESHOLD)
    {
      cleanup ();
    }
}

void
mvcc_active_tran::cleanup ()
{
  const size_t BITAREA_SIZE_AFTER_CLEANUP = 16;
  size_t delete_count = get_area_size () - BITAREA_SIZE_AFTER_CLEANUP;
  unit_type bits;
  unit_type mask;
  size_t bit_pos;
  MVCCID long_tran_mvccid;

  for (size_t i = 0; i < delete_count; i++)
    {
      bits = bit_area[i];
      // iterate on bits and find active MVCCID's
      for (bit_pos = 0, mask = 1, long_tran_mvccid = get_mvccid (i * UNIT_TO_BITS_COUNT);
	   bit_pos < UNIT_TO_BITS_COUNT && bits != ALL_COMMITTED;
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
}

void
mvcc_active_tran::set_inactive_mvccid (MVCCID mvccid)
{
  /* check whether is long transaction */
  if (MVCC_ID_PRECEDES (mvccid, bit_area_start_mvccid))
    {
      remove_long_transaction (mvccid);
    }
  else
    {
      set_bitarea_mvccid (mvccid);
    }
}
