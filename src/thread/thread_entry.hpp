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
 * thread_entry - interface of thread contextual cache. for backward compatibility it has the unintuitive name entry
 */

#ifndef _THREAD_ENTRY_HPP_
#define _THREAD_ENTRY_HPP_

#if !defined (SERVER_MODE) && !defined (SA_MODE)
#error Wrong module
#endif // not SERVER_MODE and not SA_MODE

#include "error_context.hpp"
#include "lockfree_transaction_def.hpp"
#include "porting.h"        // for pthread_mutex_t, drand48_data
#include "system.h"         // for UINTPTR, INT64, HL_HEAPID

#include <atomic>
#include <thread>

#include <cassert>

// forward definitions
// from adjustable_array.h
struct adj_array;
// from connection_defs.h
struct css_conn_entry;
// from fault_injection.h
struct fi_test_item;
// from log_system_tran.hpp
class log_system_tdes;
// from log_compress.h
struct log_zip;
// from vacuum.h
struct vacuum_worker;
// from xasl_unpack_info.hpp
struct xasl_unpack_info;

// forward resource trackers
namespace cubbase
{
  template <typename Res>
  class resource_tracker;

  // trackers
  // memory allocations
  using alloc_tracker = resource_tracker<const void *>;
  // page fix
  using pgbuf_tracker = resource_tracker<const char *>;
}
namespace cubsync
{
  class critical_section_tracker;
}
namespace cubload
{
  class driver;
}

// for lock-free - FIXME
enum
{
  THREAD_TS_SPAGE_SAVING = 0,
  THREAD_TS_OBJ_LOCK_RES,
  THREAD_TS_OBJ_LOCK_ENT,
  THREAD_TS_CATALOG,
  THREAD_TS_SESSIONS,
  THREAD_TS_FREE_SORT_LIST,
  THREAD_TS_GLOBAL_UNIQUE_STATS,
  THREAD_TS_HFID_TABLE,
  THREAD_TS_XCACHE,
  THREAD_TS_FPCACHE,
  THREAD_TS_DWB_SLOTS,
  THREAD_TS_LAST
};
#define THREAD_TS_COUNT  THREAD_TS_LAST
struct lf_tran_entry;

// for what?? - FIXME
/* stats for event logging */
typedef struct event_stat EVENT_STAT;
struct event_stat
{
  // todo - replace timeval with std::chrono::milliseconds
  /* slow query stats */
  struct timeval cs_waits;
  struct timeval lock_waits;
  struct timeval latch_waits;

  /* temp volume expand stats */
  struct timeval temp_expand_time;
  int temp_expand_pages;

  /* save PRM_ID_SQL_TRACE_SLOW_MSECS for performance */
  bool trace_slow_query;

  /* log flush thread wait time */
  int trace_log_flush_time;
};

typedef std::thread::id thread_id_t;

// FIXME - move these enum to cubthread::entry
enum thread_type
{
  TT_MASTER,
  TT_SERVER,
  TT_WORKER,
  TT_DAEMON,
  TT_LOADDB,
  TT_VACUUM_MASTER,
  TT_VACUUM_WORKER,
  TT_NONE
};

enum thread_resume_suspend_status
{
  THREAD_RESUME_NONE = 0,
  THREAD_RESUME_DUE_TO_INTERRUPT = 1,
  THREAD_RESUME_DUE_TO_SHUTDOWN = 2,
  THREAD_PGBUF_SUSPENDED = 3,
  THREAD_PGBUF_RESUMED = 4,
  THREAD_JOB_QUEUE_SUSPENDED = 5,
  THREAD_JOB_QUEUE_RESUMED = 6,
  THREAD_CSECT_READER_SUSPENDED = 7,
  THREAD_CSECT_READER_RESUMED = 8,
  THREAD_CSECT_WRITER_SUSPENDED = 9,
  THREAD_CSECT_WRITER_RESUMED = 10,
  THREAD_CSECT_PROMOTER_SUSPENDED = 11,
  THREAD_CSECT_PROMOTER_RESUMED = 12,
  THREAD_CSS_QUEUE_SUSPENDED = 13,
  THREAD_CSS_QUEUE_RESUMED = 14,
  THREAD_HEAP_CLSREPR_SUSPENDED = 15,
  THREAD_HEAP_CLSREPR_RESUMED = 16,
  THREAD_LOCK_SUSPENDED = 17,
  THREAD_LOCK_RESUMED = 18,
  THREAD_LOGWR_SUSPENDED = 19,
  THREAD_LOGWR_RESUMED = 20,
  THREAD_ALLOC_BCB_SUSPENDED = 21,
  THREAD_ALLOC_BCB_RESUMED = 22,
  THREAD_DWB_QUEUE_SUSPENDED = 23,
  THREAD_DWB_QUEUE_RESUMED = 24
};

namespace cubthread
{

  // cubthread::entry
  //
  //  description
  //    this is the thread context used by most server module functions to access thread-specific information quickly
  //
  //  note
  //    in CUBRID, thread entries are pooled and dispatched by cubthread::manager. see thread_manager.hpp for details.
  //
  //    this is an implementation in progress. for backward compatibility, all legacy members in this class are public;
  //    however, they will be gradually converted into private members with access functions
  //
  //    this header is paired with thread_compat.hpp which is used for compatibility between modules. only server
  //    module (with its SERVER_MODE and SA_MODE versions) has access to the content of this entry. client modules only
  //    sees a void pointer.
  //
  //    to avoid major refactoring, the THREAD_ENTRY alias is kept
  //
  //  todo
  //    make member variable private
  //
  //    remove content that does not belong here
  //
  //    migrate here thread entry related functionality from thread.c/h
  //
  class entry
  {
    public:
      entry ();
      ~entry ();

      // enumerations
      enum class status
      {
	TS_DEAD,
	TS_FREE,
	TS_RUN,
	TS_WAIT,
	TS_CHECK
      };

      // public functions
      void request_lock_free_transactions (void);   // todo: lock-free refactoring

      // The rules of thumbs is to always use private members. Until a complete refactoring, these members will remain
      // public
      int index;			/* thread entry index */
      thread_type type;		/* thread type */
      thread_id_t emulate_tid;	/* emulated thread id; applies to non-worker threads, when works on behalf of a worker
				   * thread */
      int client_id;		/* client id whom this thread is responding */
      int tran_index;		/* tran index to which this thread belongs */
      int private_lru_index;	/* private lru index when transaction quota is used */
      pthread_mutex_t tran_index_lock;
      unsigned int rid;		/* request id which this thread is processing */
      status m_status;			/* thread status */

      pthread_mutex_t th_entry_lock;	/* latch for this thread entry */
      pthread_cond_t wakeup_cond;	/* wakeup condition */

      HL_HEAPID private_heap_id;	/* id of thread private memory allocator */
      adj_array *cnv_adj_buffer[3];	/* conversion buffer */

      css_conn_entry *conn_entry;	/* conn entry ptr */

      xasl_unpack_info *xasl_unpack_info_ptr;     /* XASL_UNPACK_INFO * */
      int xasl_errcode;		/* xasl errorcode */
      int xasl_recursion_depth;

      unsigned int rand_seed;	/* seed for rand_r() */
      struct drand48_data rand_buf;	/* seed for lrand48_r(), drand48_r() */

      thread_resume_suspend_status resume_status;		/* resume status */
      int request_latch_mode;	/* for page latch support */
      int request_fix_count;
      bool victim_request_fail;
      bool interrupted;		/* is this request/transaction interrupted ? */
      std::atomic_bool shutdown;		/* is server going down? */
      bool check_interrupt;		/* check_interrupt == false, during fl_alloc* function call. */
      bool wait_for_latch_promote;	/* this thread is waiting for latch promotion */
      entry *next_wait_thrd;

      void *lockwait;
      INT64 lockwait_stime;		/* time in milliseconds */
      int lockwait_msecs;		/* time in milliseconds */
      int lockwait_state;
      void *query_entry;
      entry *tran_next_wait;
      entry *worker_thrd_list;	/* worker thread on job queue */

      struct log_zip *log_zip_undo;
      struct log_zip *log_zip_redo;
      char *log_data_ptr;
      int log_data_length;

      bool no_logging;

      int net_request_index;	/* request index of net server functions */

      struct vacuum_worker *vacuum_worker;	/* Vacuum worker info */

      bool sort_stats_active;

      EVENT_STAT event_stats;

      /* for query profile */
      int trace_format;
      bool on_trace;
      bool clear_trace;

      /* for lock free structures */
      lf_tran_entry *tran_entries[THREAD_TS_COUNT];

      /* for supplemental log */
      bool no_supplemental_log;
      bool trigger_involved;
      bool is_cdc_daemon;

#if !defined(NDEBUG)
      fi_test_item *fi_test_array;

      int count_private_allocators;
#endif
      int m_qlist_count;

      cubload::driver *m_loaddb_driver;

      thread_id_t get_id ();
      pthread_t get_posix_id ();
      void register_id ();
      void unregister_id ();
      bool is_on_current_thread () const;

      void return_lock_free_transaction_entries (void);

      void lock (void);
      void unlock (void);

      cuberr::context &get_error_context (void)
      {
	return m_error;
      }

      cubbase::alloc_tracker &get_alloc_tracker (void)
      {
	return m_alloc_tracker;
      }
      cubbase::pgbuf_tracker &get_pgbuf_tracker (void)
      {
	return m_pgbuf_tracker;
      }
      cubsync::critical_section_tracker &get_csect_tracker (void)
      {
	return m_csect_tracker;
      }

      log_system_tdes *get_system_tdes (void)
      {
	return m_systdes;
      }
      void set_system_tdes (log_system_tdes *sys_tdes)
      {
	m_systdes = sys_tdes;
      }
      void reset_system_tdes (void)
      {
	m_systdes = NULL;
      }
      void claim_system_worker ();
      void retire_system_worker ();

      void end_resource_tracks (void);
      void push_resource_tracks (void);
      void pop_resource_tracks (void);

      void assign_lf_tran_index (lockfree::tran::index idx);
      lockfree::tran::index pull_lf_tran_index ();
      lockfree::tran::index get_lf_tran_index ();

    private:
      void clear_resources (void);

      thread_id_t m_id;

      // error manager context
      cuberr::context m_error;

      // TODO: move all members her
      bool m_cleared;

      // trackers
      cubbase::alloc_tracker &m_alloc_tracker;
      cubbase::pgbuf_tracker &m_pgbuf_tracker;
      cubsync::critical_section_tracker &m_csect_tracker;
      log_system_tdes *m_systdes;

      lockfree::tran::index m_lf_tran_index;
  };

} // namespace cubthread

#ifndef _THREAD_COMPAT_HPP_
// The whole code uses THREAD_ENTRY... It is ridiculous to change entire code to rename.
typedef cubthread::entry THREAD_ENTRY;
typedef std::thread::id thread_id_t;
#endif // _THREAD_COMPAT_HPP_

//////////////////////////////////////////////////////////////////////////
// alias functions for C legacy code
//
// use inline functions instead definitions
//////////////////////////////////////////////////////////////////////////

inline int
thread_get_recursion_depth (cubthread::entry *thread_p)
{
  return thread_p->xasl_recursion_depth;
}

inline void
thread_inc_recursion_depth (cubthread::entry *thread_p)
{
  thread_p->xasl_recursion_depth++;
}

inline void
thread_dec_recursion_depth (cubthread::entry *thread_p)
{
  thread_p->xasl_recursion_depth--;
}

inline void
thread_clear_recursion_depth (cubthread::entry *thread_p)
{
  thread_p->xasl_recursion_depth = 0;
}

inline void
thread_trace_on (cubthread::entry *thread_p)
{
  thread_p->on_trace = true;
}

inline void
thread_trace_off (cubthread::entry *thread_p)
{
  thread_p->on_trace = true;
}

inline void
thread_set_trace_format (cubthread::entry *thread_p, int format)
{
  thread_p->trace_format = format;
}

inline bool
thread_is_on_trace (cubthread::entry *thread_p)
{
  return thread_p->on_trace;
}

inline void
thread_set_clear_trace (cubthread::entry *thread_p, bool clear)
{
  thread_p->clear_trace = clear;
}

inline bool
thread_need_clear_trace (cubthread::entry *thread_p)
{
  return thread_p->clear_trace;
}

inline bool
thread_get_sort_stats_active (cubthread::entry *thread_p)
{
  return thread_p->sort_stats_active;
}

inline bool
thread_set_sort_stats_active (cubthread::entry *thread_p, bool new_flag)
{
  bool old_flag = thread_p->sort_stats_active;
  thread_p->sort_stats_active = new_flag;
  return old_flag;
}

inline void
thread_lock_entry (cubthread::entry *thread_p)
{
  thread_p->lock ();
}

inline void
thread_unlock_entry (cubthread::entry *thread_p)
{
  thread_p->unlock ();
}

void thread_suspend_wakeup_and_unlock_entry (cubthread::entry *p, thread_resume_suspend_status suspended_reason);
int thread_suspend_timeout_wakeup_and_unlock_entry (cubthread::entry *p, struct timespec *t,
    thread_resume_suspend_status suspended_reason);
void thread_wakeup (cubthread::entry *p, thread_resume_suspend_status resume_reason);
void thread_check_suspend_reason_and_wakeup (cubthread::entry *thread_p, thread_resume_suspend_status resume_reason,
    thread_resume_suspend_status suspend_reason);
void thread_wakeup_already_had_mutex (cubthread::entry *p, thread_resume_suspend_status resume_reason);
int thread_suspend_with_other_mutex (cubthread::entry *p, pthread_mutex_t *mutexp, int timeout, struct timespec *to,
				     thread_resume_suspend_status suspended_reason);

const char *thread_type_to_string (thread_type type);
const char *thread_status_to_string (cubthread::entry::status status);
const char *thread_resume_status_to_string (thread_resume_suspend_status resume_status);
#endif // _THREAD_ENTRY_HPP_
