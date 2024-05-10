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

/*
 * thread_entry - implementation for thread contextual cache.
 */

#include "thread_entry.hpp"

#include "adjustable_array.h"
#include "critical_section.h"  // for INF_WAIT
#include "critical_section_tracker.hpp"
#include "error_manager.h"
#include "fault_injection.h"
#include "list_file.h"
#include "lock_free.h"
#include "lockfree_transaction_system.hpp"
#include "log_compress.h"
#include "log_system_tran.hpp"
#include "memory_alloc.h"
#include "page_buffer.h"
#include "resource_tracker.hpp"

#include <cstring>
#include <sstream>

#if !defined (WINDOWS)
#include <pthread.h>
#endif // WINDOWS
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace cubthread
{
  //////////////////////////////////////////////////////////////////////////
  // resource tracker dedicated section
  // todo - normally each tracker should be moved to its own module
  //////////////////////////////////////////////////////////////////////////

  // enable trackers in SERVER_MODE && debug
  static const bool ENABLE_TRACKERS =
#if !defined (NDEBUG) && defined (SERVER_MODE)
	  true;
#else // RELEASE or !SERVER_MODE
	  false;
#endif // RELEASE or !SERVER_MODE

  // tracker constants
  // alloc
  const char *ALLOC_TRACK_NAME = "Virtual Memory";
  const char *ALLOC_TRACK_RES_NAME = "res_ptr";
  const std::size_t ALLOC_TRACK_MAX_ITEMS = 32767;

  // page buffer
  const char *PGBUF_TRACK_NAME = "Page Buffer";
  const char *PGBUF_TRACK_RES_NAME = "pgptr";
  const std::size_t PGBUF_TRACK_MAX_ITEMS = 1024;
  const unsigned PGBUF_TRACK_MAX_AMOUNT = 16;       // re-fix is possible... how many to accept is debatable

  //////////////////////////////////////////////////////////////////////////
  // entry implementation
  //////////////////////////////////////////////////////////////////////////

  entry::entry ()
  // public:
    : index (-1)
    , type (TT_NONE)
    , emulate_tid ()
    , client_id (-1)
    , tran_index (NULL_TRAN_INDEX)
    , private_lru_index (-1)
    , tran_index_lock ()
    , rid (0)
    , m_status (status::TS_DEAD)
    , th_entry_lock ()
    , wakeup_cond ()
    , private_heap_id (0)
    , cnv_adj_buffer ()
    , conn_entry (NULL)
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
    , check_interrupt (true)
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
    , no_logging (false)
    , net_request_index (-1)
    , vacuum_worker (NULL)
    , sort_stats_active (false)
    , event_stats ()
    , trace_format (0)
    , on_trace (false)
    , clear_trace (false)
    , tran_entries ()
    , no_supplemental_log (false)
    , trigger_involved (false)
    , is_cdc_daemon (false)
#if !defined (NDEBUG)
    , fi_test_array (NULL)
    , count_private_allocators (0)
#endif /* DEBUG */
    , m_qlist_count (0)
    , read_ovfl_pages_count (0) // For Vacuum only.
    , m_loaddb_driver (NULL)
      // private:
    , m_id ()
    , m_error ()
    , m_cleared (false)
    , m_alloc_tracker (*new cubbase::alloc_tracker (ALLOC_TRACK_NAME, ENABLE_TRACKERS, ALLOC_TRACK_MAX_ITEMS,
		       ALLOC_TRACK_RES_NAME))
    , m_pgbuf_tracker (*new cubbase::pgbuf_tracker (PGBUF_TRACK_NAME, ENABLE_TRACKERS, PGBUF_TRACK_MAX_ITEMS,
		       PGBUF_TRACK_RES_NAME, PGBUF_TRACK_MAX_AMOUNT))
    , m_csect_tracker (*new cubsync::critical_section_tracker (ENABLE_TRACKERS))
    , m_systdes (NULL)
    , m_lf_tran_index (lockfree::tran::INVALID_INDEX)
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
    tran_entries[THREAD_TS_SPAGE_SAVING] = NULL;
    tran_entries[THREAD_TS_OBJ_LOCK_RES] = NULL;
    tran_entries[THREAD_TS_OBJ_LOCK_ENT] = NULL;
    tran_entries[THREAD_TS_CATALOG] = NULL;
    tran_entries[THREAD_TS_SESSIONS] = NULL;
    tran_entries[THREAD_TS_FREE_SORT_LIST] = NULL;
    tran_entries[THREAD_TS_GLOBAL_UNIQUE_STATS] = NULL;
    tran_entries[THREAD_TS_HFID_TABLE] = NULL;
    tran_entries[THREAD_TS_XCACHE] = NULL;
    tran_entries[THREAD_TS_FPCACHE] = NULL;

#if !defined (NDEBUG)
    fi_thread_init (this);
#endif /* DEBUG */
  }

  entry::~entry (void)
  {
    clear_resources ();

    delete &m_alloc_tracker;
    delete &m_pgbuf_tracker;
    delete &m_csect_tracker;
  }

  void
  entry::request_lock_free_transactions (void)
  {
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
    tran_entries[THREAD_TS_DWB_SLOTS] = lf_tran_request_entry (&dwb_slots_Ts);
  }

  void
  entry::clear_resources (void)
  {
    if (m_cleared)
      {
	return;
      }
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

    no_logging = false;

    no_supplemental_log = false;

    trigger_involved = false;

    is_cdc_daemon = false;

    end_resource_tracks ();

    db_destroy_private_heap (this, private_heap_id);

#if !defined (NDEBUG)
    for (int i = 0; i < THREAD_TS_COUNT; i++)
      {
	assert (tran_entries[i] == NULL);
      }
#endif // DEBUG

#if !defined (NDEBUG)
    fi_thread_final (this);
#endif // DEBUG

    assert (m_systdes == NULL);

    m_cleared = true;
  }

  thread_id_t
  entry::get_id ()
  {
    return m_id;
  }

  pthread_t
  entry::get_posix_id ()
  {
    pthread_t thread_id = 0;

    if (m_id != thread_id_t ())
      {
	std::ostringstream oss;
	oss << m_id;
	thread_id = (pthread_t) std::stoul (oss.str ());
      }

    return thread_id;
  }

  void
  entry::register_id ()
  {
    m_id = std::this_thread::get_id ();

#if defined (SERVER_MODE)
    // native thread identifier must be equal to identifier of std::this_thread
    assert (get_posix_id () == pthread_self ());
#endif /* SERVER_MODE */
  }

  void
  entry::unregister_id ()
  {
    m_id = thread_id_t ();
  }

  bool
  entry::is_on_current_thread () const
  {
    return m_id == std::this_thread::get_id ();
  }

  void
  entry::return_lock_free_transaction_entries (void)
  {
    for (std::size_t i = 0; i < THREAD_TS_COUNT; i++)
      {
	if (tran_entries[i] != NULL)
	  {
	    lf_tran_return_entry (tran_entries[i]);
	    tran_entries[i] = NULL;
	  }
      }
  }

  void
  entry::lock (void)
  {
    pthread_mutex_lock (&th_entry_lock);
  }

  void
  entry::unlock (void)
  {
    pthread_mutex_unlock (&th_entry_lock);
  }

  void
  entry::end_resource_tracks (void)
  {
    if (!ENABLE_TRACKERS)
      {
	// all trackers are activated by this flag
	return;
      }
    m_alloc_tracker.clear_all ();
    m_pgbuf_tracker.clear_all ();
    m_csect_tracker.clear_all ();
    m_qlist_count = 0;
  }

  void
  entry::push_resource_tracks (void)
  {
    if (!ENABLE_TRACKERS)
      {
	// all trackers are activated by this flag
	return;
      }
    m_alloc_tracker.push_track ();
    m_pgbuf_tracker.push_track ();
    m_csect_tracker.start ();
  }

  void
  entry::pop_resource_tracks (void)
  {
    if (!ENABLE_TRACKERS)
      {
	// all trackers are activated by this flag
	return;
      }
    m_alloc_tracker.pop_track ();
    m_pgbuf_tracker.pop_track ();
    m_csect_tracker.stop ();
  }

  void
  entry::claim_system_worker ()
  {
    assert (m_systdes == NULL);
    m_systdes = new log_system_tdes ();
    tran_index = LOG_SYSTEM_TRAN_INDEX;
  }

  void
  entry::retire_system_worker ()
  {
    delete m_systdes;
    m_systdes = NULL;
    tran_index = NULL_TRAN_INDEX;
  }

  void
  entry::assign_lf_tran_index (lockfree::tran::index idx)
  {
    m_lf_tran_index = idx;
  }

  lockfree::tran::index
  entry::pull_lf_tran_index ()
  {
    lockfree::tran::index ret = m_lf_tran_index;
    m_lf_tran_index = lockfree::tran::INVALID_INDEX;
    return ret;
  }

  lockfree::tran::index
  entry::get_lf_tran_index ()
  {
    return m_lf_tran_index;
  }

} // namespace cubthread

//////////////////////////////////////////////////////////////////////////
// legacy C functions
//////////////////////////////////////////////////////////////////////////

using thread_clock_type = std::chrono::system_clock;

static void thread_wakeup_internal (cubthread::entry *thread_p, thread_resume_suspend_status resume_reason,
				    bool had_mutex);
static void thread_check_suspend_reason_and_wakeup_internal (cubthread::entry *thread_p,
    thread_resume_suspend_status resume_reason,
    thread_resume_suspend_status suspend_reason,
    bool had_mutex);

// todo - remove timeval and use std::chrono
static void
thread_timeval_add_usec (const std::chrono::microseconds &usec, struct timeval &tv)
{
  const long ratio = 1000000;

  // add all usecs to tv_usec
  tv.tv_usec += (long) usec.count ();
  // move seconds from tv_usec to tv_sec
  tv.tv_sec = tv.tv_usec / ratio;
  tv.tv_usec = tv.tv_usec % ratio;
}

/*
 * thread_suspend_wakeup_and_unlock_entry() -
 *   return:
 *   thread_p(in):
 *   suspended_reason(in):
 *
 * Note: this function must be called by current thread also, the lock must have already been acquired.
 */
void
thread_suspend_wakeup_and_unlock_entry (cubthread::entry *thread_p, thread_resume_suspend_status suspended_reason)
{
  cubthread::entry::status old_status;

  thread_clock_type::time_point start_time_pt;
  std::chrono::microseconds usecs;

  assert (thread_p->m_status == cubthread::entry::status::TS_RUN
	  || thread_p->m_status == cubthread::entry::status::TS_CHECK);
  old_status = thread_p->m_status;
  thread_p->m_status = cubthread::entry::status::TS_WAIT;

  thread_p->resume_status = suspended_reason;

  if (thread_p->event_stats.trace_slow_query == true)
    {
      start_time_pt = thread_clock_type::now ();
    }

  pthread_cond_wait (&thread_p->wakeup_cond, &thread_p->th_entry_lock);

  if (thread_p->event_stats.trace_slow_query == true)
    {
      usecs = std::chrono::duration_cast<std::chrono::microseconds> (thread_clock_type::now () - start_time_pt);

      if (suspended_reason == THREAD_LOCK_SUSPENDED)
	{
	  thread_timeval_add_usec (usecs, thread_p->event_stats.lock_waits);
	}
      else if (suspended_reason == THREAD_PGBUF_SUSPENDED)
	{
	  thread_timeval_add_usec (usecs, thread_p->event_stats.latch_waits);
	}
    }

  thread_p->m_status = old_status;

  pthread_mutex_unlock (&thread_p->th_entry_lock);
}

/*
 * thread_suspend_timeout_wakeup_and_unlock_entry() -
 *   return:
 *   thread_p(in):
 *   time_p(in):
 *   suspended_reason(in):
 */
int
thread_suspend_timeout_wakeup_and_unlock_entry (cubthread::entry *thread_p, struct timespec *time_p,
    thread_resume_suspend_status suspended_reason)
{
  int r;
  cubthread::entry::status old_status;
  int error = NO_ERROR;

  assert (thread_p->m_status == cubthread::entry::status::TS_RUN
	  || thread_p->m_status == cubthread::entry::status::TS_CHECK);
  old_status = thread_p->m_status;
  thread_p->m_status = cubthread::entry::status::TS_WAIT;

  thread_p->resume_status = suspended_reason;

  r = pthread_cond_timedwait (&thread_p->wakeup_cond, &thread_p->th_entry_lock, time_p);

  if (r != 0 && r != ETIMEDOUT)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_COND_TIMEDWAIT, 0);
      return ER_CSS_PTHREAD_COND_TIMEDWAIT;
    }

  if (r == ETIMEDOUT)
    {
      error = ER_CSS_PTHREAD_COND_TIMEDOUT;
    }

  thread_p->m_status = old_status;

  pthread_mutex_unlock (&thread_p->th_entry_lock);

  return error;
}

/*
 * thread_wakeup_internal () -
 *   return:
 *   thread_p(in/out):
 *   resume_reason:
 */
static void
thread_wakeup_internal (cubthread::entry *thread_p, thread_resume_suspend_status resume_reason, bool had_mutex)
{
  if (had_mutex == false)
    {
      thread_lock_entry (thread_p);
    }

  pthread_cond_signal (&thread_p->wakeup_cond);
  thread_p->resume_status = resume_reason;

  if (had_mutex == false)
    {
      thread_unlock_entry (thread_p);
    }
}

/*
 * thread_check_suspend_reason_and_wakeup_internal () -
 *   return:
 *   thread_p(in):
 *   resume_reason:
 *   suspend_reason:
 *   had_mutex:
 */
static void
thread_check_suspend_reason_and_wakeup_internal (cubthread::entry *thread_p,
    thread_resume_suspend_status resume_reason,
    thread_resume_suspend_status suspend_reason, bool had_mutex)
{
  if (had_mutex == false)
    {
      thread_lock_entry (thread_p);
    }

  if (thread_p->resume_status != suspend_reason)
    {
      thread_unlock_entry (thread_p);
      return;
    }

  pthread_cond_signal (&thread_p->wakeup_cond);

  thread_p->resume_status = resume_reason;

  thread_unlock_entry (thread_p);
}

/*
 * thread_wakeup () -
 *   return:
 *   thread_p(in/out):
 *   resume_reason:
 */
void
thread_wakeup (cubthread::entry *thread_p, thread_resume_suspend_status resume_reason)
{
  thread_wakeup_internal (thread_p, resume_reason, false);
}

void
thread_check_suspend_reason_and_wakeup (cubthread::entry *thread_p, thread_resume_suspend_status resume_reason,
					thread_resume_suspend_status suspend_reason)
{
  thread_check_suspend_reason_and_wakeup_internal (thread_p, resume_reason, suspend_reason, false);
}

/*
 * thread_wakeup_already_had_mutex () -
 *   return:
 *   thread_p(in/out):
 *   resume_reason:
 */
void
thread_wakeup_already_had_mutex (cubthread::entry *thread_p, thread_resume_suspend_status resume_reason)
{
  thread_wakeup_internal (thread_p, resume_reason, true);
}

/*
 * thread_suspend_with_other_mutex() -
 *   return: 0 if no error, or error code
 *   thread_p(in):
 *   mutex_p():
 *   timeout(in):
 *   to(in):
 *   suspended_reason(in):
 */
int
thread_suspend_with_other_mutex (cubthread::entry *thread_p, pthread_mutex_t *mutex_p, int timeout,
				 struct timespec *to, thread_resume_suspend_status suspended_reason)
{
  int r = 0;
  cubthread::entry::status old_status;
  int error = NO_ERROR;

  assert (thread_p != NULL);
  old_status = thread_p->m_status;

  pthread_mutex_lock (&thread_p->th_entry_lock);

  thread_p->m_status = cubthread::entry::status::TS_WAIT;
  thread_p->resume_status = suspended_reason;

  pthread_mutex_unlock (&thread_p->th_entry_lock);

  if (timeout == INF_WAIT)
    {
      pthread_cond_wait (&thread_p->wakeup_cond, mutex_p);
    }
  else
    {
      r = pthread_cond_timedwait (&thread_p->wakeup_cond, mutex_p, to);
    }

  /* we should restore thread's status */
  if (r != NO_ERROR)
    {
      error = (r == ETIMEDOUT) ? ER_CSS_PTHREAD_COND_TIMEDOUT : ER_CSS_PTHREAD_COND_WAIT;
      if (timeout == INF_WAIT || r != ETIMEDOUT)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	}
    }

  pthread_mutex_lock (&thread_p->th_entry_lock);

  thread_p->m_status = old_status;

  pthread_mutex_unlock (&thread_p->th_entry_lock);

  assert (error == NO_ERROR || error == ER_CSS_PTHREAD_COND_TIMEDOUT);

  return error;
}

/*
 * thread_type_to_string () - Translate thread type into string
 *                            representation
 *   return:
 *   type(in): thread type
 */
const char *
thread_type_to_string (thread_type type)
{
  switch (type)
    {
    case TT_MASTER:
      return "MASTER";
    case TT_SERVER:
      return "SERVER";
    case TT_WORKER:
      return "WORKER";
    case TT_DAEMON:
      return "DAEMON";
    case TT_LOADDB:
      return "LOADDB";
    case TT_VACUUM_MASTER:
      return "VACUUM_MASTER";
    case TT_VACUUM_WORKER:
      return "VACUUM_WORKER";
    case TT_NONE:
      return "NONE";
    }
  return "UNKNOWN";
}

/*
 * thread_status_to_string () - Translate thread status into string
 *                              representation
 *   return:
 *   type(in): thread type
 */
const char *
thread_status_to_string (cubthread::entry::status status)
{
  switch (status)
    {
    case cubthread::entry::status::TS_DEAD:
      return "DEAD";
    case cubthread::entry::status::TS_FREE:
      return "FREE";
    case cubthread::entry::status::TS_RUN:
      return "RUN";
    case cubthread::entry::status::TS_WAIT:
      return "WAIT";
    case cubthread::entry::status::TS_CHECK:
      return "CHECK";
    }
  return "UNKNOWN";
}

/*
 * thread_resume_status_to_string () - Translate thread resume status into
 *                                     string representation
 *   return:
 *   type(in): thread type
 */
const char *
thread_resume_status_to_string (thread_resume_suspend_status resume_status)
{
  switch (resume_status)
    {
    case THREAD_RESUME_NONE:
      return "RESUME_NONE";
    case THREAD_RESUME_DUE_TO_INTERRUPT:
      return "RESUME_DUE_TO_INTERRUPT";
    case THREAD_RESUME_DUE_TO_SHUTDOWN:
      return "RESUME_DUE_TO_SHUTDOWN";
    case THREAD_PGBUF_SUSPENDED:
      return "PGBUF_SUSPENDED";
    case THREAD_PGBUF_RESUMED:
      return "PGBUF_RESUMED";
    case THREAD_JOB_QUEUE_SUSPENDED:
      return "JOB_QUEUE_SUSPENDED";
    case THREAD_JOB_QUEUE_RESUMED:
      return "JOB_QUEUE_RESUMED";
    case THREAD_CSECT_READER_SUSPENDED:
      return "CSECT_READER_SUSPENDED";
    case THREAD_CSECT_READER_RESUMED:
      return "CSECT_READER_RESUMED";
    case THREAD_CSECT_WRITER_SUSPENDED:
      return "CSECT_WRITER_SUSPENDED";
    case THREAD_CSECT_WRITER_RESUMED:
      return "CSECT_WRITER_RESUMED";
    case THREAD_CSECT_PROMOTER_SUSPENDED:
      return "CSECT_PROMOTER_SUSPENDED";
    case THREAD_CSECT_PROMOTER_RESUMED:
      return "CSECT_PROMOTER_RESUMED";
    case THREAD_CSS_QUEUE_SUSPENDED:
      return "CSS_QUEUE_SUSPENDED";
    case THREAD_CSS_QUEUE_RESUMED:
      return "CSS_QUEUE_RESUMED";
    case THREAD_HEAP_CLSREPR_SUSPENDED:
      return "HEAP_CLSREPR_SUSPENDED";
    case THREAD_HEAP_CLSREPR_RESUMED:
      return "HEAP_CLSREPR_RESUMED";
    case THREAD_LOCK_SUSPENDED:
      return "LOCK_SUSPENDED";
    case THREAD_LOCK_RESUMED:
      return "LOCK_RESUMED";
    case THREAD_LOGWR_SUSPENDED:
      return "LOGWR_SUSPENDED";
    case THREAD_LOGWR_RESUMED:
      return "LOGWR_RESUMED";
    case THREAD_ALLOC_BCB_SUSPENDED:
      return "ALLOC_BCB_SUSPENDED";
    case THREAD_ALLOC_BCB_RESUMED:
      return "ALLOC_BCB_RESUMED";
    case THREAD_DWB_QUEUE_SUSPENDED:
      return "DWB_BLOCK_QUEUE_SUSPENDED";
    case THREAD_DWB_QUEUE_RESUMED:
      return "DWB_BLOCK_QUEUE_RESUMED";
    }
  return "UNKNOWN";
}
