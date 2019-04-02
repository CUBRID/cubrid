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
// MVCC table - transaction information required for multi-version concurrency control system
//

#include "mvcc_table.hpp"

#include "log_impl.h"

// bit area
const size_t MVCC_BITAREA_ELEMENT_BITS = 64;
const mvcc_trans_status::bit_area_unit_type MVCC_BITAREA_ELEMENT_ALL_COMMITTED = 0xffffffffffffffffULL;
const mvcc_trans_status::bit_area_unit_type MVCC_BITAREA_BIT_COMMITTED = 1;
const mvcc_trans_status::bit_area_unit_type MVCC_BITAREA_BIT_ACTIVE = 0;

const size_t MVCC_BITAREA_ELEMENTS_AFTER_FULL_CLEANUP = 16;
const size_t MVCC_BITAREA_MAXIMUM_ELEMENTS = 500;
const size_t MVCC_BITAREA_MAXIMUM_BITS = 32000;

// history
const TRANS_STATUS_HISTORY_MAX_SIZE = 2048;

static size_t
MVCC_BITAREA_BITS_TO_ELEMENTS (size_t count)
{
  return (count + 63) >> 6; // division by 64 round up
}

static size_t
MVCC_BITAREA_ELEMENTS_TO_BYTES (size_t count)
{
  return count << 3; // multiply by 8
}

static size_t
MVCC_BITAREA_BITS_TO_BYTES (size_t count)
{
  return MVCC_BITAREA_ELEMENTS_TO_BYTES (MVCC_BITAREA_BITS_TO_ELEMENTS (count));
}

static size_t
MVCC_BITAREA_ELEMENTS_TO_BITS (size_t count)
{
  return count << 6; // multiply by 64
}

mvcc_trans_status::mvcc_trans_status ()
  : bit_area (NULL)
  , bit_area_start_mvccid (MVCCID_FIRST)
  , bit_area_length (0)
  , long_tran_mvccids (NULL)
  , long_tran_mvccids_length (0)
  , version (0)
  , lowest_active_mvccid (MVCCID_FIRST)
{
}

mvcc_trans_status::~mvcc_trans_status ()
{
  delete bit_area;
  delete long_tran_mvccids;
}

void
mvcc_trans_status::initialize ()
{
  bit_area = new bit_area_unit_type[MVCC_BITAREA_MAXIMUM_ELEMENTS];
  bit_area_start_mvccid = MVCCID_FIRST;
  bit_area_length = 0;
  long_tran_mvccids = new MVCCID[logtb_get_number_of_total_tran_indices ()];
  long_tran_mvccids_length = 0;
  version = 0;
  lowest_active_mvccid = MVCCID_FIRST;
}

void
mvcc_trans_status::finalize ()
{
  delete bit_area;
  bit_area = NULL;

  delete long_tran_mvccids;
  long_tran_mvccids = NULL;
}

mvcctable::mvcctable ()
  : current_trans_status ()
  , transaction_lowest_active_mvccids (NULL)
  , trans_status_history (NULL)
  , trans_status_history_position (0)
  , new_mvccid_lock ()
  , active_trans_mutex ()
{
}

mvcctable::~mvcctable ()
{
  delete transaction_lowest_active_mvccids;
  delete trans_status_history;
}

void
mvcctable::initialize ()
{
  current_trans_status.initialize ();
  trans_status_history = new mvcc_trans_status[TRANS_STATUS_HISTORY_MAX_SIZE];
  for (size_t idx = 0; idx < TRANS_STATUS_HISTORY_MAX_SIZE; idx++)
    {
      trans_status_history[idx].initialize ();
    }
  trans_status_history_position = 0;

  size_t num_tx = logtb_get_number_of_total_tran_indices ();
  transaction_lowest_active_mvccids = new MVCCID[num_tx];   // all MVCCID_NULL
}

void
mvcctable::finalize ()
{
  current_trans_status.finalize ();

  delete trans_status_history;
  trans_status_history = NULL;

  delete transaction_lowest_active_mvccids;
  transaction_lowest_active_mvccids = NULL;
}
