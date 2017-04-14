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

#if defined(SERVER_MODE)
#include <sys/types.h>
#if !defined(WINDOWS)
#include <pthread.h>
#endif /* !WINDOWS */

#include "porting.h"
#include "error_manager.h"
#include "adjustable_array.h"
#include "system_parameter.h"
#include "dbtype.h"
#endif /* SERVER_MODE */

#include "lock_free.h"

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

#if !defined(SERVER_MODE)
#define THREAD_GET_CURRENT_ENTRY_INDEX(thrd) thread_get_current_entry_index()

extern int thread_Recursion_depth;

extern LF_TRAN_ENTRY thread_ts_decoy_entries[THREAD_TS_LAST];

#define thread_get_thread_entry_info()  (NULL)
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

typedef void THREAD_ENTRY;

typedef void *CSS_THREAD_ARG;

typedef int (*CSS_THREAD_FN) (THREAD_ENTRY * thrd, CSS_THREAD_ARG);

#define thread_rc_track_need_to_trace(thread_p) (false)
#define thread_rc_track_enter(thread_p) (-1)
#define thread_rc_track_exit(thread_p, idx) (NO_ERROR)
#define thread_rc_track_amount_pgbuf(thread_p) (0)
#define thread_rc_track_amount_qlist(thread_p) (0)
#define thread_rc_track_dump_all(thread_p, outfp)
#define thread_rc_track_meter(thread_p, file, line, amount, ptr, rc_idx, mgr_idx)

#define thread_get_tran_entry(thread_p, entry_idx)  (&thread_ts_decoy_entries[entry_idx])

#define thread_start_scan (NULL)

#define thread_is_page_flush_thread_available()	(false)

#else /* !SERVER_MODE */

#define THREAD_GET_CURRENT_ENTRY_INDEX(thrd) \
  ((thrd) ? (thrd)->index : thread_get_current_entry_index())

#if defined(HPUX)
#define thread_set_thread_entry_info(entry)
#endif /* HPUX */

enum
{ TS_DEAD = 0, TS_FREE, TS_RUN, TS_WAIT, TS_CHECK };
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

typedef enum
{
  TT_MASTER,
  TT_SERVER,
  TT_WORKER,
  TT_DAEMON,
  TT_VACUUM_MASTER,
  TT_VACUUM_WORKER,
  TT_NONE
} THREAD_TYPE;

enum
{ THREAD_STOP_WORKERS_EXCEPT_LOGWR, THREAD_STOP_LOGWR };

/*
 * thread resource track info matrix: thread_p->track.meter[RC][MGR]
 * +------------+-----+-------+------+
 * |RC/MGR      | DEF | BTREE | LAST |
 * +------------+-----+-------+------+
 * | VMEM       |     |   X   |   X  |
 * +------------+-----+-------+------+
 * | PGBUF      |     |   X   |   X  |
 * +------------+-----+-------+------+
 * | PGBUF_TEMP |     |   X   |   X  |
 * +------------+-----+-------+------+
 * | QLIST      |     |   X   |   X  |
 * +------------+-----+-------+------+
 * | CS         |     |   X   |   X  |
 * +------------+-----+-------+------+
 * | LAST       |  X  |   X   |   X  |
 * +------------+-----+-------+------+
 */

/* resource track meters */
enum
{ RC_VMEM = 0, RC_PGBUF, RC_QLIST, RC_CS, RC_LAST };

/* resource track managers */
enum
{ MGR_DEF = 0, MGR_LAST };

/* resource track critical section enter mode */
enum
{
  THREAD_TRACK_CSECT_ENTER_AS_READER = 1,
  THREAD_TRACK_CSECT_ENTER_AS_WRITER,
  THREAD_TRACK_CSECT_PROMOTE,
  THREAD_TRACK_CSECT_DEMOTE,
  THREAD_TRACK_CSECT_EXIT
};

#if !defined (NDEBUG)
#define THREAD_TRACKED_RES_CALLER_FILE_MAX_SIZE	    20
/* THREAD_TRACKED_RESOURCE - Used to track allocated resources.
 * When a resource is used first time, a structure like this one is generated
 * and file name and line are saved. Any other usages will update the amount.
 * When the amount becomes 0, the resource is considered "freed".
 */
typedef struct thread_tracked_resource THREAD_TRACKED_RESOURCE;
struct thread_tracked_resource
{
  void *res_ptr;
  int caller_line;
  INT32 amount;
  char caller_file[THREAD_TRACKED_RES_CALLER_FILE_MAX_SIZE];
};
#endif /* !NDEBUG */

typedef struct thread_resource_meter THREAD_RC_METER;
struct thread_resource_meter
{
  INT32 m_amount;		/* resource hold counter */
  INT32 m_threshold;		/* for future work, get PRM */
  const char *m_add_file_name;	/* last add file name, line number */
  INT32 m_add_line_no;
  const char *m_sub_file_name;	/* last sub file name, line number */
  INT32 m_sub_line_no;
#if !defined(NDEBUG)
  char m_hold_buf[ONE_K];	/* used specially for each meter */
  char m_rwlock_buf[ONE_K];	/* the rwlock for each thread in CS */
  INT32 m_hold_buf_size;

  THREAD_TRACKED_RESOURCE *m_tracked_res;
  INT32 m_tracked_res_capacity;
  INT32 m_tracked_res_count;
#endif				/* !NDEBUG */
};

typedef struct thread_resource_track THREAD_RC_TRACK;
struct thread_resource_track
{
  HL_HEAPID private_heap_id;	/* id of thread private memory allocator */
  THREAD_RC_METER meter[RC_LAST][MGR_LAST];
  THREAD_RC_TRACK *prev;

#if !defined (NDEBUG)
  THREAD_TRACKED_RESOURCE *tracked_resources;
#endif
};

/* Forward definition to fix compile error. */
struct vacuum_worker;
struct fi_test_item;


typedef struct thread_entry THREAD_ENTRY;

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

struct thread_entry
{
#if defined(WINDOWS)
  UINTPTR thread_handle;	/* thread handle */
#endif				/* WINDOWS */
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
  ADJ_ARRAY *cnv_adj_buffer[3];	/* conversion buffer */

  struct css_conn_entry *conn_entry;	/* conn entry ptr */

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
  struct thread_entry *next_wait_thrd;

  void *lockwait;
  INT64 lockwait_stime;		/* time in milliseconds */
  int lockwait_msecs;		/* time in milliseconds */
  int lockwait_state;
  void *query_entry;
  struct thread_entry *tran_next_wait;
  struct thread_entry *worker_thrd_list;	/* worker thrd on jobq list */

  void *log_zip_undo;
  void *log_zip_redo;
  char *log_data_ptr;
  int log_data_length;

  int net_request_index;	/* request index of net server functions */

  struct vacuum_worker *vacuum_worker;	/* Vacuum worker info */

  /* resource track info */
  THREAD_RC_TRACK *track;
  int track_depth;
  int track_threshold;		/* for future work, get PRM */
  THREAD_RC_TRACK *track_free_list;

  bool sort_stats_active;

  EVENT_STAT event_stats;

  /* for query profile */
  int trace_format;
  bool on_trace;
  bool clear_trace;

  /* for lock free structures */
  LF_TRAN_ENTRY *tran_entries[THREAD_TS_COUNT];

#if !defined(NDEBUG)
  struct fi_test_item *fi_test_array;
#endif
};

#define DOES_THREAD_RESUME_DUE_TO_SHUTDOWN(thread_p) \
  ((thread_p)->resume_status == THREAD_RESUME_DUE_TO_INTERRUPT && \
   (thread_p)->interrupted == true)

typedef struct daemon_thread_monitor DAEMON_THREAD_MONITOR;
struct daemon_thread_monitor
{
  int thread_index;
  bool is_available;
  bool is_running;
  int nrequestors;
  pthread_mutex_t lock;
  pthread_cond_t cond;
};

#define DAEMON_THREAD_MONITOR_INITIALIZER  \
  {0, false, false, 0, PTHREAD_MUTEX_INITIALIZER,PTHREAD_COND_INITIALIZER}

typedef void *CSS_THREAD_ARG;

typedef int (*CSS_THREAD_FN) (THREAD_ENTRY * thrd, CSS_THREAD_ARG);

#if !defined(HPUX)
extern int thread_set_thread_entry_info (THREAD_ENTRY * entry);
#endif /* not HPUX */

extern THREAD_ENTRY *thread_get_thread_entry_info (void);

extern int thread_initialize_manager (void);
extern int thread_start_workers (void);
extern int thread_stop_active_workers (unsigned short stop_phase);
extern int thread_stop_active_daemons (void);
extern int thread_stop_vacuum_daemons (void);
extern int thread_kill_all_workers (void);
extern void thread_final_manager (void);
extern void thread_slam_tran_index (THREAD_ENTRY * thread_p, int tran_index);
extern int thread_lock_entry (THREAD_ENTRY * entry);
#if defined (ENABLE_UNUSED_FUNCTION)
extern int thread_lock_entry_with_tran_index (int tran_index);
#endif
extern int thread_unlock_entry (THREAD_ENTRY * p);
extern int thread_suspend_wakeup_and_unlock_entry (THREAD_ENTRY * p, int suspended_reason);
extern int thread_suspend_timeout_wakeup_and_unlock_entry (THREAD_ENTRY * p, struct timespec *t, int suspended_reason);
#if defined (ENABLE_UNUSED_FUNCTION)
extern int thread_suspend_wakeup_and_unlock_entry_with_tran_index (int tran_index, int suspended_reason);
#endif
extern int thread_wakeup (THREAD_ENTRY * p, int resume_reason);
extern int thread_check_suspend_reason_and_wakeup (THREAD_ENTRY * thread_p, int resume_reason, int suspend_reason);

extern int thread_wakeup_already_had_mutex (THREAD_ENTRY * p, int resume_reason);
extern int thread_wakeup_with_tran_index (int tran_index, int resume_reason);

extern ADJ_ARRAY *css_get_cnv_adj_buffer (int idx);
extern void css_set_cnv_adj_buffer (int idx, ADJ_ARRAY * buffer);
extern int thread_is_manager_initialized (void);
extern void thread_waiting_for_function (THREAD_ENTRY * thread_p, CSS_THREAD_FN func, CSS_THREAD_ARG arg);
#if defined(ENABLE_UNUSED_FUNCTION)
extern void thread_exit (int exit_code);
#endif
extern void thread_sleep (double);
extern void thread_get_info_threads (int *num_total_threads, int *num_worker_threads, int *num_free_threads,
				     int *num_suspended_threads);
extern int thread_num_worker_threads (void);
extern int thread_num_total_threads (void);
extern int thread_get_client_id (THREAD_ENTRY * thread_p);
extern unsigned int thread_get_comm_request_id (THREAD_ENTRY * thread_p);
#if defined (ENABLE_UNUSED_FUNCTION)
extern void thread_set_comm_request_id (unsigned int rid);
#endif
extern THREAD_ENTRY *thread_find_entry_by_tran_index (int tran_index);
extern THREAD_ENTRY *thread_find_entry_by_tran_index_except_me (int tran_index);
extern int thread_get_current_entry_index (void);
extern unsigned int thread_get_current_session_id (void);
extern int thread_get_current_tran_index (void);
extern void thread_set_current_tran_index (THREAD_ENTRY * thread_p, int tran_index);
#if defined (ENABLE_UNUSED_FUNCTION)
extern void thread_set_tran_index (THREAD_ENTRY * thread_p, int tran_index);
extern void thread_wakeup_session_control_thread (void);
extern void thread_wakeup_check_ha_delay_info_thread (void);
#endif
extern struct css_conn_entry *thread_get_current_conn_entry (void);
extern int thread_has_threads (THREAD_ENTRY * caller, int tran_index, int client_id);
extern bool thread_set_check_interrupt (THREAD_ENTRY * thread_p, bool flag);

/* wakeup functions */
extern void thread_wakeup_deadlock_detect_thread (void);
extern void thread_wakeup_log_flush_thread (void);
extern void thread_wakeup_page_flush_thread (void);
extern void thread_try_wakeup_page_flush_thread (void);
extern void thread_wakeup_page_buffer_maintenance_thread (void);
extern void thread_wakeup_page_post_flush_thread (void);
extern void thread_wakeup_flush_control_thread (void);
extern void thread_wakeup_checkpoint_thread (void);
extern void thread_wakeup_purge_archive_logs_thread (void);
extern void thread_wakeup_oob_handler_thread (void);
extern void thread_wakeup_auto_volume_expansion_thread (void);
extern void thread_wakeup_vacuum_master_thread (void);
extern void thread_wakeup_vacuum_worker_threads (int n_workers);

/* is available functions */
extern bool thread_is_page_flush_thread_available (void);
extern bool thread_is_page_post_flush_thread_available (void);
extern bool thread_is_log_flush_thread_available (void);

/* is running tunfions */
extern bool thread_auto_volume_expansion_thread_is_running (void);

extern THREAD_ENTRY *thread_find_first_lockwait_entry (int *thrd_index);
extern THREAD_ENTRY *thread_find_next_lockwait_entry (int *thrd_index);
extern THREAD_ENTRY *thread_find_entry_by_index (int thrd_index);
extern THREAD_ENTRY *thread_find_entry_by_tid (pthread_t thrd_id);
extern int thread_get_lockwait_entry (int tran_index, THREAD_ENTRY ** array);


extern int thread_suspend_with_other_mutex (THREAD_ENTRY * p, pthread_mutex_t * mutexp, int timeout,
					    struct timespec *to, int suspended_reason);
extern void thread_print_entry_info (THREAD_ENTRY * p);
extern int thread_return_all_transactions_entries (void);
extern void thread_dump_threads (void);
extern bool thread_get_check_interrupt (THREAD_ENTRY * thread_p);

extern int xthread_kill_tran_index (THREAD_ENTRY * thread_p, int kill_tran_index, char *kill_user, char *kill_host,
				    int kill_pid);
extern int xthread_kill_or_interrupt_tran (THREAD_ENTRY * thread_p, int tran_id, bool is_dba_group_member,
					   bool interrupt_only);

extern HL_HEAPID css_get_private_heap (THREAD_ENTRY * thread_p);
extern HL_HEAPID css_set_private_heap (THREAD_ENTRY * thread_p, HL_HEAPID heap_id);

extern void thread_set_info (THREAD_ENTRY * thread_p, int client_id, int rid, int tran_index, int net_request_index);

extern bool thread_rc_track_need_to_trace (THREAD_ENTRY * thread_p);
extern int thread_rc_track_enter (THREAD_ENTRY * thread_p);
extern int thread_rc_track_exit (THREAD_ENTRY * thread_p, int id);
extern int thread_rc_track_amount_pgbuf (THREAD_ENTRY * thread_p);
extern int thread_rc_track_amount_qlist (THREAD_ENTRY * thread_p);
extern void thread_rc_track_dump_all (THREAD_ENTRY * thread_p, FILE * outfp);
extern void thread_rc_track_meter (THREAD_ENTRY * thread_p, const char *file_name, const int line_no, int amount,
				   void *ptr, int rc_idx, int mgr_idx);
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

extern INT64 thread_get_log_clock_msec (void);

extern int thread_start_scan (THREAD_ENTRY * thread_p, int type, DB_VALUE ** arg_values, int arg_cnt, void **ctx);

#if defined(WINDOWS)
extern unsigned __stdcall thread_worker (void *);

/* There is no static mutex initializer - PTHREAD_MUTEX_INITIALIZER - in win32
 * threads. So all mutexes are initialized at the first time it used. This
 * variable is used to syncronize mutex initialization.
 */
extern pthread_mutex_t css_Internal_mutex_for_mutex_initialize;
#else /* WINDOWS */
extern void *thread_worker (void *);
#endif /* !WINDOWS */

extern int thread_first_vacuum_worker_thread_index (void);

extern bool thread_is_auto_volume_expansion_thread_available (void);

#if !defined (NDEBUG)
extern THREAD_ENTRY *thread_iterate (THREAD_ENTRY * thread_p);
#endif /* !NDEBUG */
#endif /* SERVER_MODE */

#endif /* _THREAD_H_ */
