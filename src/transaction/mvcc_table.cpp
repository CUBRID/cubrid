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
#include "mvcc.h"
#include "perf_monitor.h"

#include <cassert>

mvcc_trans_status::mvcc_trans_status ()
  : m_active_mvccs ()
  , version (0)
  , lowest_active_mvccid (MVCCID_FIRST)
{
}

mvcc_trans_status::~mvcc_trans_status ()
{
}

void
mvcc_trans_status::initialize ()
{
  m_active_mvccs.initialize ();
  version = 0;
  lowest_active_mvccid = MVCCID_FIRST;
}

void
mvcc_trans_status::finalize ()
{
  m_active_mvccs.finalize ();
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
  trans_status_history = new mvcc_trans_status[HISTORY_MAX_SIZE];
  for (size_t idx = 0; idx < HISTORY_MAX_SIZE; idx++)
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
mvcctable::build_mvcc_info (log_tdes &tdes)
{
  MVCCID tx_lowest_active;
  MVCCID crt_status_lowest_active;
  size_t index;
  mvcc_trans_status::version_type trans_status_version;

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
  tdes.mvccinfo.snapshot.active_mvccs.initialize ();

  transaction_lowest_active_mvccids[tdes.tran_index].load (tx_lowest_active);

  // repeat steps until a trans_status can be read successfully without a version change
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
      assert (index < HISTORY_MAX_SIZE);

      const mvcc_trans_status &trans_status = trans_status_history[index];
      trans_status.m_active_mvccs.copy_to (tdes.mvccinfo.snapshot.m_active_mvccs);
      /* load statistics temporary disabled need to be enabled when activate count optimization */
#if 0
      /* load global statistics. This must take place here and nowhere else. */
      if (logtb_load_global_statistics_to_tran (thread_p) != NO_ERROR)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_MVCC_CANT_GET_SNAPSHOT, 0);
	  error_code = ER_MVCC_CANT_GET_SNAPSHOT;
	}
#endif

      if (trans_status_version == trans_status.version.load ())
	{
	  // no version change; copying status was successful
	  break;
	}
    }

  highest_completed_mvccid = tdes.mvccinfo.snapshot.active_mvccs.get_highest_completed_mvccid ();
  MVCCID_FORWARD (highest_completed_mvccid);

  /* update lowest active mvccid computed for the most recent snapshot */
  tdes.mvccinfo.recent_snapshot_lowest_active_mvccid = crt_status_lowest_active;

  /* update remaining snapshot data */
  tdes.mvccinfo.snapshot.snapshot_fnc = mvcc_satisfies_snapshot;
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
      ret_active = trans_status_history[index].m_active_mvccs.is_active (mvccid);
    }
  while (index != trans_status_history[index].load ());

  return ret_active;
}
