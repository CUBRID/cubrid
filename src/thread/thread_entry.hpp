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
 * thread_entry - interface of thread contextual cache. for backward compatibility it has the unintuitive name entry
 */

#ifndef _THREAD_ENTRY_HPP_
#define _THREAD_ENTRY_HPP_

#include "error_context.hpp"
#include "porting.h"        // for pthread_mutex_t, drand48_data
#include "system.h"         // for UINTPTR, INT64, HL_HEAPID

// forward definitions
// from adjustable_array.h
struct adj_array;
// from connection_defs.h
struct css_conn_entry;
// from fault_injection.h
struct fi_test_item;
// from log_compress.h
struct log_zip;
// from vacuum.h
struct vacuum_worker;

// from thread.h - FIXME
struct thread_resource_track;

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
  THREAD_TS_LAST
};
#define THREAD_TS_COUNT  THREAD_TS_LAST
struct lf_tran_entry;

// for what?? - FIXME
/* stats for event logging */
typedef struct event_stat EVENT_STAT;
struct event_stat
{
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

// FIXME
enum THREAD_TYPE
{
  TT_MASTER,
  TT_SERVER,
  TT_WORKER,
  TT_DAEMON,
  TT_VACUUM_MASTER,
  TT_VACUUM_WORKER,
  TT_NONE
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

      // public functions

      void request_lock_free_transactions (void);   // todo: lock-free refactoring

      // The rules of thumbs is to always use private members. Until a complete refactoring, these members will remain
      // public
      int index;			/* thread entry index */
      THREAD_TYPE type;		/* thread type */
      pthread_t tid;		/* thread id */
      pthread_t emulate_tid;	/* emulated thread id; applies to non-worker threads, when works on behalf of a worker
				   * thread */
      int client_id;		/* client id whom this thread is responding */
      int tran_index;		/* tran index to which this thread belongs */
      int private_lru_index;	/* private lru index when transaction quota is used */
      pthread_mutex_t tran_index_lock;
      unsigned int rid;		/* request id which this thread is processing */
      int status;			/* thread status */

      pthread_mutex_t th_entry_lock;	/* latch for this thread entry */
      pthread_cond_t wakeup_cond;	/* wakeup condition */

      HL_HEAPID private_heap_id;	/* id of thread private memory allocator */
      adj_array *cnv_adj_buffer[3];	/* conversion buffer */

      css_conn_entry *conn_entry;	/* conn entry ptr */

      ER_MSG ermsg;			/* error msg area */
      ER_MSG *er_Msg;		/* last error */
      char er_emergency_buf[ER_EMERGENCY_BUF_SIZE];	/* error msg buffer for emergency */

      void *xasl_unpack_info_ptr;	/* XASL_UNPACK_INFO * */
      int xasl_errcode;		/* xasl errorcode */
      int xasl_recursion_depth;

      unsigned int rand_seed;	/* seed for rand_r() */
      struct drand48_data rand_buf;	/* seed for lrand48_r(), drand48_r() */

      int resume_status;		/* resume status */
      int request_latch_mode;	/* for page latch support */
      int request_fix_count;
      bool victim_request_fail;
      bool interrupted;		/* is this request/transaction interrupted ? */
      bool shutdown;		/* is server going down? */
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

      int net_request_index;	/* request index of net server functions */

      struct vacuum_worker *vacuum_worker;	/* Vacuum worker info */

      /* resource track info */
      thread_resource_track *track;
      int track_depth;
      int track_threshold;		/* for future work, get PRM */
      thread_resource_track *track_free_list;

      bool sort_stats_active;

      EVENT_STAT event_stats;

      /* for query profile */
      int trace_format;
      bool on_trace;
      bool clear_trace;

      /* for lock free structures */
      lf_tran_entry *tran_entries[THREAD_TS_COUNT];

#if !defined(NDEBUG)
      fi_test_item *fi_test_array;

      int count_private_allocators;
#endif

      cuberr::context &get_error_context (void)
      {
	return m_error;
      }

    private:

      void clear_resources (void);

      // error manager context
      cuberr::context m_error;

      // TODO: move all members her
      bool m_cleared;
  };

} // namespace cubthread

#ifndef _THREAD_COMPAT_HPP_
// The whole code uses THREAD_ENTRY... It is ridiculous to change entire code to rename.
typedef cubthread::entry THREAD_ENTRY;
#endif // _THREAD_COMPAT_HPP_

#endif // _THREAD_ENTRY_HPP_
