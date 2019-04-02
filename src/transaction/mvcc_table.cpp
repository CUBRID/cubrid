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
#include "perf_monitor.h"

#include <cassert>
#include <cstring>

// bit area
const size_t MVCC_BITAREA_ELEMENT_BITS = 64;
const mvcc_trans_status::bit_area_unit_type MVCC_BITAREA_ELEMENT_ALL_COMMITTED = 0xffffffffffffffffULL;
const mvcc_trans_status::bit_area_unit_type MVCC_BITAREA_BIT_COMMITTED = 1;
const mvcc_trans_status::bit_area_unit_type MVCC_BITAREA_BIT_ACTIVE = 0;

const size_t MVCC_BITAREA_ELEMENTS_AFTER_FULL_CLEANUP = 16;
const size_t MVCC_BITAREA_MAXIMUM_ELEMENTS = 500;
const size_t MVCC_BITAREA_MAXIMUM_BITS = 32000;

// history
const size_t TRANS_STATUS_HISTORY_MAX_SIZE = 2048;

static void mvcctbl_get_highest_completed_mvccid (mvcc_trans_status::bit_area_unit_type *bit_area,
    size_t bit_area_length, MVCCID bit_area_start_mvccid,
    MVCCID &highest_completed_mvccid);

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

static char *
MVCC_GET_BITAREA_ELEMENT_PTR (UINT64 *bitareaptr, size_t position)
{
  bitareaptr + (position / MVCC_BITAREA_ELEMENT_BITS);
}

static mvcc_trans_status::bit_area_unit_type
MVCC_BITAREA_MASK (size_t position)
{
  bit_area_unit_type unit = 1;
  return unit << (position & 63);
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

bool
mvcc_trans_status::is_active (MVCCID mvccid) const
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
      size_t position = mvccid - local_bit_area_start_mvccid;
      if (position < local_bit_area_length)
	{
	  bit_area_unit_type *p_area = MVCC_GET_BITAREA_ELEMENT_PTR (bit_area, position);
	  return ((*p_area) & MVCC_BITAREA_MASK (position)) != 0;
	}
      else
	{
	  return true;
	}
    }
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
  transaction_lowest_active_mvccids = new lowest_active_mvccid_type[num_tx];   // all MVCCID_NULL
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

void
mvcctable::allocate_mvcc_snapshot_data (mvcc_snapshot &snapshot)
{
  if (snapshot.long_tran_mvccids == NULL)
    {
      snapshot.long_tran_mvccids = new MVCCID[logtb_get_number_of_total_tran_indices ()];
    }
  if (snapshot.bit_area == NULL)
    {
      snapshot.bit_area = new mvcc_trans_status::bit_area_unit_type[MVCC_BITAREA_MAXIMUM_ELEMENTS];
    }
}

void
mvcctable::build_mvcc_snapshot (log_tdes &tdes)
{
  MVCCID tx_lowest_active;
  MVCCID crt_status_lowest_active;

  size_t index;

  mvcc_trans_status::version_type trans_status_version;
  MVCCID bit_area_start_mvccid;
  size_t bit_area_length;

  MVCCID highest_completed_mvccid;

  bool is_perf_tracking = perfmon_is_perf_tracking ();
  TSC_TICKS start_tick, end_tick;
  TSCTIMEVAL tv_diff;
  UINT64 snapshot_wait_time;
  UINT64 snapshot_retry_count = 0;

  assert (tdes.tran_index >= 0 && tdes.tran_index < logtb_get_number_of_total_tran_indices ());

  if (is_perf_tracking)
    {
      tsc_getticks (&start_tick);
    }

  // make sure snapshot has allocated data
  allocate_mvcc_snapshot_data (tdes.mvccinfo.snapshot);

  transaction_lowest_active_mvccids[tdes.tran_index].load (tx_lowest_active);

  while (true)
    {
      snapshot_retry_count++;

      if (!MVCCID_IS_VALID (tx_lowest_active))
	{
	  /*
	   * First, by setting MVCCID_ALL_VISIBLE we will tell to VACUUM that transaction lowest MVCCID will be set
	   * soon.
	   * This is needed since setting p_transaction_lowest_active_mvccid is not an atomic operation (global
	   * lowest_active_mvccid must be obtained first). We want to avoid a possible scenario (even if the chances
	   * are minimal) like the following one:
	   *    - the snapshot thread reads the initial value of global lowest active MVCCID but the thread is
	   * suspended (due to thread switching) just before setting p_transaction_lowest_active_mvccid
	   *    - the transaction having global lowest active MVCCID commits, so the global value is updated (advanced)
	   *    - the VACCUM thread computes the MVCCID threshold as the updated global lowest active MVCCID
	   *    - the snapshot thread resumes and p_transaction_lowest_active_mvccid is set to initial value of global
	   * lowest active MVCCID
	   *    - the VACUUM thread computes the threshold again and found a value (initial global lowest active MVCCID)
	   * less than the previously threshold
	   */
	  transaction_lowest_active_mvccids[tdes.tran_index].store (MVCCID_ALL_VISIBLE);

	  /*
	   * Is important that between next two code lines to not have delays (to not execute any other code).
	   * Otherwise, VACUUM may delay, waiting more in logtb_get_oldest_active_mvccid.
	   */
	  crt_status_lowest_active = current_trans_status.lowest_active_mvccid.load ();
	  transaction_lowest_active_mvccids[tdes.tran_index].store (crt_status_lowest_active);
	}
      else
	{
	  crt_status_lowest_active = current_trans_status.lowest_active_mvccid.load ();
	}

      index = transaction_lowest_active_mvccids->load ();
      assert (index < TRANS_STATUS_HISTORY_MAX_SIZE);

      const mvcc_trans_status &trans_status = trans_status_history[index];
      trans_status_version = trans_status.version.load ();
      bit_area_start_mvccid = trans_status.bit_area_start_mvccid.load ();
      bit_area_length = trans_status.bit_area_length.load ();

      if (bit_area_length > 0)
	{
	  std::memcpy (tdes.mvccinfo.snapshot.bit_area, trans_status.bit_area,
		       MVCC_BITAREA_BITS_TO_BYTES (bit_area_length));
	}
      if (trans_status.long_tran_mvccids_length > 0)
	{
	  tdes.mvccinfo.snapshot.long_tran_mvccids_length = trans_status.long_tran_mvccids_length;
	  std::memcpy (tdes.mvccinfo.snapshot.long_tran_mvccids, trans_status.long_tran_mvccids,
		       trans_status.long_tran_mvccids_length * sizeof (MVCCID));
	}

      /* load statistics temporary disabled need to be enabled when activate count optimization */
#if 0
      /* load global statistics. This must take place here and no where else. */
      if (logtb_load_global_statistics_to_tran (thread_p) != NO_ERROR)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_MVCC_CANT_GET_SNAPSHOT, 0);
	  error_code = ER_MVCC_CANT_GET_SNAPSHOT;
	}
#endif

      if (trans_status_version != trans_status.version.load ())
	{
	  /* The transaction status version overwritten, need to read again */
	}
      else
	{
	  // successfully built snapshot
	  break;
	}
    }

  mvcctbl_get_highest_completed_mvccid (tdes.mvccinfo.snapshot.bit_area, bit_area_length, bit_area_start_mvccid,
					highest_completed_mvccid);
  MVCCID_FORWARD (highest_completed_mvccid);

  /* update lowest active mvccid computed for the most recent snapshot */
  tdes.mvccinfo.recent_snapshot_lowest_active_mvccid = crt_status_lowest_active;

  /* update remaining snapshot data */
  tdes.mvccinfo.snapshot.snapshot_fnc = mvcc_satisfies_snapshot;
  tdes.mvccinfo.snapshot.bit_area_start_mvccid = bit_area_start_mvccid;
  tdes.mvccinfo.snapshot.bit_area_length = bit_area_length;
  tdes.mvccinfo.snapshot.lowest_active_mvccid = crt_status_lowest_active;
  tdes.mvccinfo.snapshot.highest_completed_mvccid = highest_completed_mvccid;
  tdes.mvccinfo.snapshot.valid = true;

  if (is_perf_tracking)
    {
      tsc_getticks (&end_tick);
      tsc_elapsed_time_usec (&tv_diff, end_tick, start_tick);
      snapshot_wait_time = tv_diff.tv_sec * 1000000LL + tv_diff.tv_usec;
      if (snapshot_wait_time > 0)
	{
	  perfmon_add_stat (thread_p, PSTAT_LOG_SNAPSHOT_TIME_COUNTERS, snapshot_wait_time);
	}
      if (snapshot_retry_cnt > 1)
	{
	  perfmon_add_stat (thread_p, PSTAT_LOG_SNAPSHOT_RETRY_COUNTERS, snapshot_retry_cnt - 1);
	}
    }
}

static void
mvcctbl_get_highest_completed_mvccid (mvcc_trans_status::bit_area_unit_type *bit_area, size_t bit_area_length,
				      MVCCID bit_area_start_mvccid, MVCCID &highest_completed_mvccid)
{
  UINT64 *highest_completed_bit_area = NULL;
  UINT64 bits;
  size_t end_position;
  size_t highest_bit_position;
  size_t bit_pos;
  size_t count_bits;

  assert (bit_area != NULL && bit_area_start_mvccid > MVCCID_FIRST);

  if (bit_area_length == 0)
    {
      highest_completed_mvccid = bit_area_start_mvccid - 1;
      return;
    }

  /* compute highest highest_bit_pos and highest_completed_bit_area */
  end_position = bit_area_length - 1;
  for (highest_completed_bit_area = MVCC_GET_BITAREA_ELEMENT_PTR (bit_area, end_position);
       highest_completed_bit_area >= bit_area; --highest_completed_bit_area)
    {
      bits = *highest_completed_bit_area;
      if (bits == 0)
	{
	  continue;
	}
      for (bit_pos = 0, count_bits = MVCC_BITAREA_ELEMENT_BITS / 2; count_bits > 0; count_bits /= 2)
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

  if (highest_completed_bit_area < bit_area)
    {
      // not found
      highest_completed_mvccid = bit_area_start_mvccid - 1;
    }
  else
    {
      count_bits = MVCC_BITAREA_ELEMENTS_TO_BITS (highest_completed_bit_area - bit_area);
      highest_completed_mvccid = bit_area_start_mvccid + count_bits + highest_completed_mvccid;
    }
}

bool
mvcctable::is_active (MVCCID mvccid) const
{
  size_t index = 0;
  bool ret_active = false;
  // entry at trans_status_history_position must remain same while is_active () is computed. otherwise it must be
  // repeated
  do
    {
      index = trans_status_history_position.load ();
      ret_active = trans_status_history[index].is_active (mvccid);
    }
  while (index != trans_status_history[index].load ());

  return ret_active;
}
