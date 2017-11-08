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

/*
 * thread_entry - implementation for thread contextual cache.
 */

#include "thread_entry.hpp"

#include "fault_injection.h"
#include "memory_alloc.h"
#include "page_buffer.h"
#include "thread.h"

#include <cstring>

namespace thread
{

entry::entry ()
  : index (-1)
  , type (TT_WORKER)
  , tid (0)
  , emulate_tid (0)
  , client_id (-1)
  , tran_index (-1)
  , private_lru_index (-1)
  , tran_index_lock ()
  , rid (0)
  , status (TS_DEAD)
  , th_entry_lock ()
  , wakeup_cond ()
  , private_heap_id (0)
  , cnv_adj_buffer ()
  , conn_entry (NULL)
  , ermsg ()
  , er_Msg (NULL)
  , er_emergency_buf ()
  , xasl_unpack_info_ptr (NULL)
  , xasl_errcode (0)
  , xasl_recursion_depth (0)
  , rand_seed (0)
  , rand_buf ()
  , resume_status (THREAD_RESUME_NONE)
  , request_latch_mode (PGBUF_NO_LATCH)
  , request_fix_count (0)
  , victim_request_fail (false)
  , interrupted (false)
  , shutdown (false)
  , check_interrupt (false)
  , wait_for_latch_promote (false)
  , next_wait_thrd (NULL)
  , lockwait (NULL)
  , lockwait_stime (0)
  , lockwait_msecs (0)
  , lockwait_state (-1)
  , query_entry (NULL)
  , tran_next_wait (NULL)
  , worker_thrd_list (NULL)
  , log_zip_undo (NULL)
  , log_zip_redo (NULL)
  , log_data_ptr (NULL)
  , log_data_length (0)
  , net_request_index (-1)
  , track (NULL)
  , track_depth (-1)
  , track_threshold (0x7f)  // 127
  , track_free_list (NULL)
  , sort_stats_active (false)
  , event_stats ()
  , trace_format (0)
  , on_trace (false)
  , clear_trace (false)
  , tran_entries ()
  , vacuum_worker (NULL)
#if !defined (NDEBUG)
  , fi_test_array (NULL)
  , count_private_allocators (0)
#endif /* DEBUG */
{
  if (pthread_mutex_init (&tran_index_lock, NULL) != 0)
    {
      // cannot recover from this
      assert (false);
    }
  if (pthread_mutex_init (&th_entry_lock, NULL) != 0)
    {
      // cannot recover from this
      assert (false);
    }
  if (pthread_cond_init (&wakeup_cond, NULL) != 0)
    {
      // cannot recover from this
      assert (false);
    }

  private_heap_id = db_create_private_heap ();

  cnv_adj_buffer[0] = NULL;
  cnv_adj_buffer[1] = NULL;
  cnv_adj_buffer[2] = NULL;

  struct timeval t;
  gettimeofday (&t, NULL);
  rand_seed = (unsigned int) t.tv_usec;
  srand48_r ((long) t.tv_usec, &rand_buf);

  std::memset (&event_stats, 0, sizeof (event_stats));

  /* lock-free transaction entries */
  tran_entries[THREAD_TS_SPAGE_SAVING] = lf_tran_request_entry (&spage_saving_Ts);
  tran_entries[THREAD_TS_OBJ_LOCK_RES] = lf_tran_request_entry (&obj_lock_res_Ts);
  tran_entries[THREAD_TS_OBJ_LOCK_ENT] = lf_tran_request_entry (&obj_lock_ent_Ts);
  tran_entries[THREAD_TS_CATALOG] = lf_tran_request_entry (&catalog_Ts);
  tran_entries[THREAD_TS_SESSIONS] = lf_tran_request_entry (&sessions_Ts);
  tran_entries[THREAD_TS_FREE_SORT_LIST] = lf_tran_request_entry (&free_sort_list_Ts);
  tran_entries[THREAD_TS_GLOBAL_UNIQUE_STATS] = lf_tran_request_entry (&global_unique_stats_Ts);
  tran_entries[THREAD_TS_HFID_TABLE] = lf_tran_request_entry (&hfid_table_Ts);
  tran_entries[THREAD_TS_XCACHE] = lf_tran_request_entry (&xcache_Ts);
  tran_entries[THREAD_TS_FPCACHE] = lf_tran_request_entry (&fpcache_Ts);

#if !defined (NDEBUG)
  fi_thread_init (this);
#endif /* DEBUG */
}

entry::~entry ()
{
  for (int i = 0; i < 3; i++)
    {
      if (cnv_adj_buffer[i] != NULL)
        {
          adj_ar_free (cnv_adj_buffer[i]);
        }
    }
  if (pthread_mutex_destroy (&tran_index_lock) != 0)
    {
      assert (false);
    }
  if (pthread_mutex_destroy (&th_entry_lock) != 0)
    {
      assert (false);
    }
  if (pthread_cond_destroy (&wakeup_cond) != 0)
    {
      assert (false);
    }

  if (log_zip_undo != NULL)
    {
      log_zip_free ((LOG_ZIP *) log_zip_undo);
    }
  if (log_zip_redo != NULL)
    {
      log_zip_free ((LOG_ZIP *) log_zip_redo);
    }
  if (log_data_ptr != NULL)
    {
      free (log_data_ptr);
    }

  thread_rc_track_finalize (this);

  db_destroy_private_heap (this, private_heap_id);

  if (thread_return_transaction_entry (this) != NO_ERROR)
    {
      assert (false);
    }

#if !defined (NDEBUG)
  fi_thread_final (this);
#endif // DEBUG
}

} // namespace thread
