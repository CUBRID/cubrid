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

/* *INDENT-OFF* */

#include "thread_manager.hpp"

#if !defined(SERVER_MODE)

// todo - this is more tricky to solve due to dependency on transaction_cl
#define thread_set_check_interrupt(thread_p, flag) tran_set_check_interrupt (flag)
#define thread_get_check_interrupt(thread_p) tran_get_check_interrupt ()

// todo - implement resource tracker independent of threading
#define thread_rc_track_need_to_trace(thread_p) (false)
#define thread_rc_track_enter(thread_p) (-1)
#define thread_rc_track_exit(thread_p, idx) (NO_ERROR)
#define thread_rc_track_amount_pgbuf(thread_p) (0)
#define thread_rc_track_amount_qlist(thread_p) (0)
#define thread_rc_track_dump_all(thread_p, outfp)
#define thread_rc_track_meter(thread_p, file, line, amount, ptr, rc_idx, mgr_idx)

#else /* !SERVER_MODE */

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

extern int thread_has_threads (THREAD_ENTRY * caller, int tran_index, int client_id);
extern bool thread_set_check_interrupt (THREAD_ENTRY * thread_p, bool flag);

extern THREAD_ENTRY *thread_find_first_lockwait_entry (int *thrd_index);
extern THREAD_ENTRY *thread_find_next_lockwait_entry (int *thrd_index);
extern THREAD_ENTRY *thread_find_entry_by_index (int thrd_index);
extern THREAD_ENTRY *thread_find_entry_by_tid (thread_id_t thrd_id);
extern int thread_get_lockwait_entry (int tran_index, THREAD_ENTRY ** array);

extern bool thread_get_check_interrupt (THREAD_ENTRY * thread_p);

extern bool thread_rc_track_need_to_trace (THREAD_ENTRY * thread_p);
extern int thread_rc_track_enter (THREAD_ENTRY * thread_p);
extern int thread_rc_track_exit (THREAD_ENTRY * thread_p, int id);
extern int thread_rc_track_amount_pgbuf (THREAD_ENTRY * thread_p);
extern int thread_rc_track_amount_qlist (THREAD_ENTRY * thread_p);
extern void thread_rc_track_dump_all (THREAD_ENTRY * thread_p, FILE * outfp);
extern void thread_rc_track_meter (THREAD_ENTRY * thread_p, const char *file_name, const int line_no, int amount,
				   void *ptr, int rc_idx, int mgr_idx);
extern void thread_rc_track_initialize (THREAD_ENTRY * thread_p);
extern void thread_rc_track_finalize (THREAD_ENTRY * thread_p);
extern void thread_rc_track_clear_all (THREAD_ENTRY * thread_p);

extern const char *thread_type_to_string (int type);
extern const char *thread_status_to_string (cubthread::entry::status status);
extern const char *thread_resume_status_to_string (int resume_status);

extern THREAD_ENTRY *thread_iterate (THREAD_ENTRY * thread_p);
#endif /* SERVER_MODE */

/* *INDENT-ON* */

#endif /* _THREAD_H_ */
