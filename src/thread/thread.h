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
 * thread.h - threads wrapper for pthreads and WIN32 threads.
 */

#ifndef _THREAD_H_
#define _THREAD_H_

#ident "$Id$"

#if (!defined (SERVER_MODE) && !defined (SA_MODE)) || !defined (__cplusplus)
#error Does not belong in this context; maybe thread_compat.h can be included instead
#endif // not SERVER_MODE and not SA_MODE or not C++

#include "lock_free.h"
#include "thread_entry.hpp"

#if defined(SERVER_MODE)
#include "adjustable_array.h"
#include "dbtype_def.h"
#include "error_manager.h"
#include "log_compress.h"
#include "porting.h"
#include "system_parameter.h"
#endif /* SERVER_MODE */

#if defined(SERVER_MODE) && !defined (WINDOWS)
#include <pthread.h>
#endif // SERVER_MODE and not WINDOWS
#if defined(SERVER_MODE)
#include <sys/types.h>
#endif // SERVER_MODE

#if defined (SA_MODE)
#include "thread_manager.hpp"	// for thread_get_thread_entry_info ();
#endif // SA_MODE

enum
{
  TS_DEAD = 0, TS_FREE, TS_RUN, TS_WAIT, TS_CHECK
};

enum
{ THREAD_RESUME_NONE = 0,
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
};

#if !defined(SERVER_MODE)
#define THREAD_GET_CURRENT_ENTRY_INDEX(thrd) thread_get_current_entry_index()

extern int thread_Recursion_depth;

extern LF_TRAN_ENTRY thread_ts_decoy_entries[THREAD_TS_LAST];

/* *INDENT-OFF* */
STATIC_INLINE THREAD_ENTRY *thread_get_thread_entry_info (void) __attribute__ ((ALWAYS_INLINE));
THREAD_ENTRY *
thread_get_thread_entry_info (void)
{
  THREAD_ENTRY& te = cubthread::get_manager ()->get_entry ();
  return &te;
}
/* *INDENT-ON* */

#define thread_num_worker_threads()  (1)
#define thread_num_total_threads()   (1)
#define thread_get_current_entry_index() (0)
#define thread_get_current_session_id() (db_Session_id)
#define thread_set_check_interrupt(thread_p, flag) tran_set_check_interrupt (flag)
#define thread_get_check_interrupt(thread_p) tran_get_check_interrupt ()

#define thread_trace_on(thread_p)
#define thread_set_trace_format(thread_p, format)
#define thread_is_on_trace(thread_p) (false)
#define thread_set_clear_trace(thread_p, clear)
#define thread_need_clear_trace(thread_p) (false)
#define thread_get_sort_stats_active(thread_p) (false)
#define thread_set_sort_stats_active(thread_p, flag)

#define thread_get_recursion_depth(thread_p) (thread_Recursion_depth)
#define thread_inc_recursion_depth(thread_p) (thread_Recursion_depth ++)
#define thread_dec_recursion_depth(thread_p) (thread_Recursion_depth --)
#define thread_clear_recursion_depth(thread_p) (thread_Recursion_depth = 0)

#define thread_get_tran_entry(thread_p, entry_idx)  (&thread_ts_decoy_entries[entry_idx])

#else /* !SERVER_MODE */

#define THREAD_GET_CURRENT_ENTRY_INDEX(thrd) \
  ((thrd) ? (thrd)->index : thread_get_current_entry_index())

#if defined(HPUX)
#define thread_set_thread_entry_info(entry)
#endif /* HPUX */

enum thread_stop_type
{
  THREAD_STOP_WORKERS_EXCEPT_LOGWR,
  THREAD_STOP_LOGWR
};

/* Forward definition to fix compile error. */
struct vacuum_worker;
struct fi_test_item;

#define DOES_THREAD_RESUME_DUE_TO_SHUTDOWN(thread_p) \
  ((thread_p)->resume_status == THREAD_RESUME_DUE_TO_INTERRUPT && \
   (thread_p)->interrupted == true)

#if !defined(HPUX)
extern int thread_set_thread_entry_info (THREAD_ENTRY * entry);
#endif /* not HPUX */

extern THREAD_ENTRY *thread_get_thread_entry_info (void);

extern int thread_initialize_manager (size_t & total_thread_count);
extern void thread_final_manager (void);
extern void thread_slam_tran_index (THREAD_ENTRY * thread_p, int tran_index);
extern int thread_lock_entry (THREAD_ENTRY * entry);
extern int thread_unlock_entry (THREAD_ENTRY * p);
extern int thread_suspend_wakeup_and_unlock_entry (THREAD_ENTRY * p, int suspended_reason);
extern int thread_suspend_timeout_wakeup_and_unlock_entry (THREAD_ENTRY * p, struct timespec *t, int suspended_reason);
extern int thread_wakeup (THREAD_ENTRY * p, int resume_reason);
extern int thread_check_suspend_reason_and_wakeup (THREAD_ENTRY * thread_p, int resume_reason, int suspend_reason);

extern int thread_wakeup_already_had_mutex (THREAD_ENTRY * p, int resume_reason);
extern int thread_wakeup_with_tran_index (int tran_index, int resume_reason);

extern ADJ_ARRAY *css_get_cnv_adj_buffer (int idx);
extern void css_set_cnv_adj_buffer (int idx, ADJ_ARRAY * buffer);
extern int thread_is_manager_initialized (void);
#ifdef __cplusplus
extern "C"
{
#endif
  extern void thread_sleep (double);
#ifdef __cplusplus
}
#endif
extern void thread_get_info_threads (int *num_total_threads, int *num_worker_threads, int *num_free_threads,
				     int *num_suspended_threads);
extern int thread_num_worker_threads (void);
extern int thread_num_total_threads (void);
extern int thread_get_client_id (THREAD_ENTRY * thread_p);
extern unsigned int thread_get_comm_request_id (THREAD_ENTRY * thread_p);
extern THREAD_ENTRY *thread_find_entry_by_tran_index (int tran_index);
extern THREAD_ENTRY *thread_find_entry_by_tran_index_except_me (int tran_index);
extern int thread_get_current_entry_index (void);
extern unsigned int thread_get_current_session_id (void);
extern int thread_get_current_tran_index (void);
extern void thread_set_current_tran_index (THREAD_ENTRY * thread_p, int tran_index);
extern struct css_conn_entry *thread_get_current_conn_entry (void);
extern int thread_has_threads (THREAD_ENTRY * caller, int tran_index, int client_id);
extern bool thread_set_check_interrupt (THREAD_ENTRY * thread_p, bool flag);

extern THREAD_ENTRY *thread_find_first_lockwait_entry (int *thrd_index);
extern THREAD_ENTRY *thread_find_next_lockwait_entry (int *thrd_index);
extern THREAD_ENTRY *thread_find_entry_by_index (int thrd_index);
extern THREAD_ENTRY *thread_find_entry_by_tid (thread_id_t thrd_id);
extern int thread_get_lockwait_entry (int tran_index, THREAD_ENTRY ** array);

extern int thread_suspend_with_other_mutex (THREAD_ENTRY * p, pthread_mutex_t * mutexp, int timeout,
					    struct timespec *to, int suspended_reason);
extern int thread_return_all_transactions_entries (void);
extern bool thread_get_check_interrupt (THREAD_ENTRY * thread_p);

extern int xthread_kill_tran_index (THREAD_ENTRY * thread_p, int kill_tran_index, char *kill_user, char *kill_host,
				    int kill_pid);
extern int xthread_kill_or_interrupt_tran (THREAD_ENTRY * thread_p, int tran_id, bool is_dba_group_member,
					   bool interrupt_only);

extern HL_HEAPID css_get_private_heap (THREAD_ENTRY * thread_p);
extern HL_HEAPID css_set_private_heap (THREAD_ENTRY * thread_p, HL_HEAPID heap_id);

extern void thread_set_info (THREAD_ENTRY * thread_p, int client_id, int rid, int tran_index, int net_request_index);

extern bool thread_get_sort_stats_active (THREAD_ENTRY * thread_p);
extern bool thread_set_sort_stats_active (THREAD_ENTRY * thread_p, bool flag);

extern LF_TRAN_ENTRY *thread_get_tran_entry (THREAD_ENTRY * thread_p, int entry_idx);

extern void thread_trace_on (THREAD_ENTRY * thread_p);
extern void thread_set_trace_format (THREAD_ENTRY * thread_p, int format);
extern bool thread_is_on_trace (THREAD_ENTRY * thread_p);
extern void thread_set_clear_trace (THREAD_ENTRY * thread_p, bool clear);
extern bool thread_need_clear_trace (THREAD_ENTRY * thread_p);

extern int thread_get_recursion_depth (THREAD_ENTRY * thread_p);
extern void thread_inc_recursion_depth (THREAD_ENTRY * thread_p);
extern void thread_dec_recursion_depth (THREAD_ENTRY * thread_p);
extern void thread_clear_recursion_depth (THREAD_ENTRY * thread_p);

extern const char *thread_type_to_string (int type);
extern const char *thread_status_to_string (int status);
extern const char *thread_resume_status_to_string (int resume_status);

#if defined(WINDOWS)
/* There is no static mutex initializer - PTHREAD_MUTEX_INITIALIZER - in win32
 * threads. So all mutexes are initialized at the first time it used. This
 * variable is used to synchronize mutex initialization.
 */
extern pthread_mutex_t css_Internal_mutex_for_mutex_initialize;
#endif /* !WINDOWS */

extern THREAD_ENTRY *thread_iterate (THREAD_ENTRY * thread_p);

extern int thread_return_transaction_entry (THREAD_ENTRY * entry_p);

#if defined(HPUX)
#define thread_initialize_key()
#else
extern int thread_initialize_key (void);
#endif /* HPUX */
#endif /* SERVER_MODE */

#endif /* _THREAD_H_ */
