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
 * thread_impl.h - Thread manager module at server
 */

#ifndef _THREAD_IMPL_H_
#define _THREAD_IMPL_H_

#ident "$Id$"

/* TODO: Explain main concept or logic of your code
 contained in this file if possible */

#if defined(SERVER_MODE)
#include <sys/types.h>
#if !defined(WINDOWS)
#include <pthread.h>
#endif /* !WINDOWS */

#include "thread.h"
#include "error_manager.h"
#include "adjustable_array.h"
#include "system_parameter.h"
#endif /* SERVER_MODE */

#if !defined(SERVER_MODE)
#define thread_get_thread_entry_info()  (NULL)
#define thread_num_worker_threads()  (1)
#define thread_num_total_threads()   (1)

typedef void THREAD_ENTRY;
#else /* !SERVER_MODE */

#define THREAD_SET_INFO(thrd, clnt_id, r, tran_idx) \
        do { \
            (thrd)->client_id = (clnt_id); \
            (thrd)->rid = (r); \
            (thrd)->tran_index = (tran_idx); \
            (thrd)->victim_request_fail = false; \
            (thrd)->next_wait_thrd = NULL; \
            (thrd)->lockwait = NULL; \
            (thrd)->lockwait_state = -1; \
            (thrd)->query_entry = NULL; \
            (thrd)->tran_next_wait = NULL; \
        } while (0)

#define THREAD_SET_TRAN_INDEX(thrd, tran_idx) \
        (thrd)->tran_index = (tran_idx)

#if defined(HPUX)
#define thread_set_thread_entry_info(entry)
#endif /* HPUX */

typedef struct thread_entry THREAD_ENTRY;

enum
{ TS_DEAD = 0, TS_FREE, TS_RUN, TS_WAIT, TS_CHECK };
enum
{ RESUME_NOT_OK = -1, RESUME_OK = 0 };
enum
{ TT_MASTER, TT_SERVER, TT_WORKER, TT_DAEMON, TT_NONE };

struct thread_entry
{
#if defined(WINDOWS)
  UINTPTR thread_handle;	/* thread handle */
#endif				/* WINDOWS */
  int index;			/* thread entry index */
  int type;			/* thread type */
  THREAD_T tid;			/* thread id */
  int client_id;		/* client id whom this thread is responding */
  int tran_index;		/* tran index to which this thread belongs */
  MUTEX_T tran_index_lock;
  unsigned int rid;		/* request id which this thread is processing */
  int status;			/* thread status */

  MUTEX_T th_entry_lock;	/* latch for this thread entry */
  COND_T wakeup_cond;		/* wakeup condition */

  HL_HEAPID private_heap_id;	/* id of thread private memory allocator */
  ADJ_ARRAY *cnv_adj_buffer[3];	/* conversion buffer */

  struct css_conn_entry *conn_entry;	/* conn entry ptr */

  ER_MSG ermsg;			/* error msg area */
  ER_MSG *er_Msg;		/* last error */
  char er_emergency_buf[256];	/* error msg buffer for emergency */

  void *xasl_unpack_info_ptr;	/* XASL_UNPACK_INFO * */
  int xasl_errcode;		/* xasl errorcode */

  unsigned int rand_seed;	/* seed for rand_r() */

  char qp_num_buf[81];		/* buffer which contains number as
				   string form;
				   used in the qp/numeric_db_value_print() */

  int resume_status;		/* resume status */
  int request_latch_mode;	/* for page latch support */
  int request_fix_count;
  bool victim_request_fail;
  bool interrupted;		/* is this request/transaction interrupted ? */
  bool shutdown;		/* is server going down? */
  bool check_interrupt;		/* check_interrupt == false, during
				   fl_alloc* function call. */
  struct thread_entry *next_wait_thrd;

  void *lockwait;
  double lockwait_stime;	/* time in miliseconds */
  int lockwait_nsecs;
  int lockwait_state;
  void *query_entry;
  struct thread_entry *tran_next_wait;
  struct thread_entry *worker_thrd_list;	/* worker thrd on jobq list */
};

typedef struct daemon_thread_monitor DAEMON_THREAD_MONITOR;
struct daemon_thread_monitor
{
  int thread_index;
  bool is_valid;
  bool is_running;
  bool is_log_flush_force;
  MUTEX_T lock;
  COND_T cond;
};

typedef void *CSS_THREAD_ARG;

typedef int (*CSS_THREAD_FN) (THREAD_ENTRY * thrd, CSS_THREAD_ARG);

extern DAEMON_THREAD_MONITOR thread_Log_flush_thread;

#if !defined(HPUX)
extern int thread_set_thread_entry_info (THREAD_ENTRY * entry);
#endif /* not HPUX */

extern THREAD_ENTRY *thread_get_thread_entry_info (void);

extern int thread_initialize_manager (void);
extern int thread_start_workers (void);
extern int thread_stop_active_workers (void);
extern int thread_stop_active_daemons (void);
extern int thread_kill_all_workers (void);
extern void thread_final_manager (void);

extern int thread_lock_entry (THREAD_ENTRY * entry);
#if defined (ENABLE_UNUSED_FUNCTION)
extern int thread_lock_entry_with_tran_index (int tran_index);
#endif
extern int thread_unlock_entry (THREAD_ENTRY * p);
extern int thread_suspend_wakeup_and_unlock_entry (THREAD_ENTRY * p);
extern int
thread_suspend_timeout_wakeup_and_unlock_entry (THREAD_ENTRY * p, void *t);
#if defined (ENABLE_UNUSED_FUNCTION)
extern int thread_suspend_wakeup_and_unlock_entry_with_tran_index (int
								   tran_index);
#endif
extern int thread_wakeup (THREAD_ENTRY * p);
extern int thread_wakeup_with_tran_index (int tran_index);

extern ADJ_ARRAY *css_get_cnv_adj_buffer (int idx);
extern void css_set_cnv_adj_buffer (int idx, ADJ_ARRAY * buffer);
extern int thread_is_manager_initialized (void);

extern void thread_wait (THREAD_ENTRY * thread_p, CSS_THREAD_FN func,
			 CSS_THREAD_ARG arg);
extern void thread_exit (int exit_code);
extern void thread_sleep (int, int);
extern void thread_get_info_threads (int *num_total_threads,
				     int *num_worker_threads,
				     int *num_free_threads,
				     int *num_suspended_threads);
extern int thread_num_worker_threads (void);
extern int thread_num_total_threads (void);
extern int thread_get_client_id (THREAD_ENTRY * thread_p);
extern unsigned int thread_get_comm_request_id (THREAD_ENTRY * thread_p);
#if defined (ENABLE_UNUSED_FUNCTION)
extern void thread_set_comm_request_id (unsigned int rid);
#endif
extern int thread_get_current_entry_index (void);
extern int thread_get_current_tran_index (void);
extern void thread_set_current_tran_index (THREAD_ENTRY * thread_p,
					   int tran_index);
#if defined (ENABLE_UNUSED_FUNCTION)
extern void thread_set_tran_index (THREAD_ENTRY * thread_p, int tran_index);
#endif
extern struct css_conn_entry *thread_get_current_conn_entry (void);
extern int thread_has_threads (int tran_index, int client_id);
extern bool thread_set_check_interrupt (THREAD_ENTRY * thread_p, bool flag);
extern void thread_wakeup_deadlock_detect_thread (void);
extern void thread_wakeup_log_flush_thread (void);
extern void thread_wakeup_page_flush_thread (void);
extern THREAD_ENTRY *thread_find_first_lockwait_entry (int *thrd_index);
extern THREAD_ENTRY *thread_find_next_lockwait_entry (int *thrd_index);
extern THREAD_ENTRY *thread_find_entry_by_index (int thrd_index);
extern int thread_get_lockwait_entry (int tran_index, THREAD_ENTRY ** array);
extern void thread_wakeup_checkpoint_thread (void);
extern void thread_wakeup_oob_handler_thread (void);

#if defined (WINDOWS)
extern int thread_suspend_with_other_mutex (THREAD_ENTRY * p,
					    MUTEX_T * mutexp,
					    int timeout, int *to);
#else /* WINDOWS */
extern int thread_suspend_with_other_mutex (THREAD_ENTRY * p,
					    MUTEX_T * mutexp,
					    int timeout, struct timespec *to);
#endif /* WINDOWS */
extern void thread_print_entry_info (THREAD_ENTRY * p);
extern void thread_dump_threads (void);
extern bool thread_get_check_interrupt (THREAD_ENTRY * thread_p);

extern int xthread_kill_tran_index (THREAD_ENTRY * thread_p,
				    int kill_tran_index, char *kill_user,
				    char *kill_host, int kill_pid);

extern HL_HEAPID css_get_private_heap (THREAD_ENTRY * thread_p);
extern HL_HEAPID css_set_private_heap (THREAD_ENTRY * thread_p,
				       HL_HEAPID heap_id);

#if defined(WINDOWS)
extern unsigned __stdcall thread_worker (void *);

/* There is no static mutex initializer - PTHREAD_MUTEX_INITIALIZER - in win32
 * threads. So all mutexes are initialized at the first time it used. This
 * variable is used to syncronize mutex initialization.
 */
extern HANDLE css_Internal_mutex_for_mutex_initialize;
#else /* WINDOWS */
extern void *thread_worker (void *);
#endif /* !WINDOWS */
#endif /* SERVER_MODE */

#endif /* _THREAD_IMPL_H_ */
