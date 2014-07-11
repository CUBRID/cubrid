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
 * page_buffer.c - Page buffer management module (at the server)
 */

#ident "$Id$"

#include "config.h"

#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>

#include "page_buffer.h"
#include "storage_common.h"
#include "memory_alloc.h"
#include "system_parameter.h"
#include "error_manager.h"
#include "file_io.h"
#include "log_manager.h"
#include "log_impl.h"
#include "transaction_sr.h"
#include "memory_hash.h"
#include "critical_section.h"
#include "perf_monitor.h"
#include "environment_variable.h"
#include "thread.h"
#include "list_file.h"
#include "tsc_timer.h"
#include "query_manager.h"

#if defined(CUBRID_DEBUG)
#include "disk_manager.h"
#endif /* CUBRID_DEBUG */

#if defined(SERVER_MODE)
#include "connection_error.h"

#endif /* SERVER_MODE */

#if defined(PAGE_STATISTICS)
#include "disk_manager.h"
#include "boot_sr.h"
#endif /* PAGE_STATISTICS */

#if defined(ENABLE_SYSTEMTAP)
#include "probes.h"
#endif /* ENABLE_SYSTEMTAP */

/* minimum number of buffers */
#define PGBUF_MINIMUM_BUFFERS		(MAX_NTRANS * 10)

/* BCB holder list related constants */

/* Each thread has its own free BCB holder list.
   The list has PGBUF_DEFAULT_FIX_COUNT entries by default. */
#define PGBUF_DEFAULT_FIX_COUNT    7

/* Each BCB holder array, that is allocated from OS,
   has PGBUF_NUM_ALLOC_HOLDER elements(BCB holder entries). */
#define PGBUF_NUM_ALLOC_HOLDER     10

#if !defined(SERVER_MODE)
#define pthread_mutex_init(a, b)
#define pthread_mutex_destroy(a)
#define pthread_mutex_lock(a)	0
#define pthread_mutex_unlock(a)
static int rv;
#endif /* !SERVER_MODE */

#define PGBUF_LRU_1_ZONE_THRESHOLD  (pgbuf_Pool.num_LRU1_zone_threshold)

/* The victim candidate flusher (performed as a daemon) finds
   victim candidates(fcnt == 0) from the bottom of each LRU list.
   and flushes them if they are in dirty state. */
#define PGBUF_LRU_SIZE \
  ((int) (prm_get_integer_value (PRM_ID_PB_NBUFFERS)/pgbuf_Pool.num_LRU_list))

#define PGBUF_MIN_NUM_VICTIMS (MAX (1, (int) (PGBUF_LRU_SIZE * 0.1)))

/* maximum number of try in case of failure in allocating a BCB */
#define PGBUF_SLEEP_MAX                    1

/* default timeout seconds for infinite wait */
#define PGBUF_TIMEOUT                      300	/* timeout seconds */

/* size of io page */
#if defined(CUBRID_DEBUG)
#define SIZEOF_IOPAGE_PAGESIZE_AND_GUARD() (IO_PAGESIZE + sizeof(pgbuf_Guard))
#else /* CUBRID_DEBUG */
#define SIZEOF_IOPAGE_PAGESIZE_AND_GUARD() (IO_PAGESIZE)
#endif /* CUBRID_DEBUG */

/* size of one buffer page <BCB, page> */
#define PGBUF_BCB_SIZE       (sizeof(PGBUF_BCB))
#define PGBUF_IOPAGE_BUFFER_SIZE \
  ((size_t)(offsetof(PGBUF_IOPAGE_BUFFER, iopage) + \
  SIZEOF_IOPAGE_PAGESIZE_AND_GUARD()))
/* size of buffer hash entry */
#define PGBUF_BUFFER_HASH_SIZE       (sizeof(PGBUF_BUFFER_HASH))
/* size of buffer lock record */
#define PGBUF_BUFFER_LOCK_SIZE       (sizeof(PGBUF_BUFFER_LOCK))
/* size of one LRU list structure */
#define PGBUF_LRU_LIST_SIZE       (sizeof(PGBUF_LRU_LIST))
/* size of BCB holder entry */
#define PGBUF_HOLDER_SIZE        (sizeof(PGBUF_HOLDER))
/* size of BCB holder array that is allocated in one time */
#define PGBUF_HOLDER_SET_SIZE    (sizeof(PGBUF_HOLDER_SET))
/* size of BCB holder anchor */
#define PGBUF_HOLDER_ANCHOR_SIZE (sizeof(PGBUF_HOLDER_ANCHOR))

/* get memory address(pointer) */
#define PGBUF_FIND_BCB_PTR(i) \
  ((PGBUF_BCB *)((char *)&(pgbuf_Pool.BCB_table[0])+(PGBUF_BCB_SIZE*(i))))

#define PGBUF_FIND_IOPAGE_PTR(i) \
  ((PGBUF_IOPAGE_BUFFER *)((char *)&(pgbuf_Pool.iopage_table[0]) \
  +(PGBUF_IOPAGE_BUFFER_SIZE*(i))))

#define PGBUF_FIND_BUFFER_GUARD(bufptr) \
  (&bufptr->iopage_buffer->iopage.page[DB_PAGESIZE])

/* macros for casting pointers */
#define CAST_PGPTR_TO_BFPTR(bufptr, pgptr)                              \
  do {                                                                  \
    (bufptr) = ((PGBUF_BCB *)((PGBUF_IOPAGE_BUFFER *)                   \
      ((char *)pgptr - offsetof(PGBUF_IOPAGE_BUFFER, iopage.page)))->bcb); \
    assert ((bufptr) == (bufptr)->iopage_buffer->bcb);                  \
  } while (0)

#define CAST_PGPTR_TO_IOPGPTR(io_pgptr, pgptr)                          \
  do {                                                                  \
    (io_pgptr) = (FILEIO_PAGE *) ((char *) pgptr                        \
                                  - offsetof(FILEIO_PAGE, page));       \
  } while (0)

#define CAST_BFPTR_TO_PGPTR(pgptr, bufptr)                              \
  do {                                                                  \
    assert ((bufptr) == (bufptr)->iopage_buffer->bcb);                  \
    (pgptr) = ((PAGE_PTR)                                               \
               ((char *)(bufptr->iopage_buffer) 			\
			+ offsetof(PGBUF_IOPAGE_BUFFER, iopage.page))); \
  } while (0)

/* check whether the given volume is auxiliary volume */
#define PGBUF_IS_AUXILIARY_VOLUME(volid)                                 \
  ((volid) < LOG_DBFIRST_VOLID ? true : false)

#define HASH_SIZE_BITS 20
#define PGBUF_HASH_SIZE (1 << HASH_SIZE_BITS)

#define PGBUF_HASH_VALUE(vpid) pgbuf_hash_func_mirror(vpid)

/* Maximum overboost flush multiplier : controls the maximum factor to apply
 * to configured flush ratio, when the miss rate (victim_request/fix_request)
 * increases.
 */
#define PGBUF_FLUSH_VICTIM_BOOST_MULT 10

#if defined(SERVER_MODE)

#define MUTEX_LOCK_VIA_BUSY_WAIT(rv, m) \
        do { \
            int _loop; \
            rv = EBUSY; \
            for (_loop = 0; \
		 _loop < \
		 prm_get_integer_value (PRM_ID_MUTEX_BUSY_WAITING_CNT); \
		 _loop++) \
	      { \
                rv = pthread_mutex_trylock (&(m)); \
                if (rv == 0) { \
                    rv = NO_ERROR; \
                    break; \
              } \
            } \
            if (rv != 0) { \
                rv = pthread_mutex_lock (&m); \
            } \
        } while (0)
#else /* SERVER_MODE */
#define MUTEX_LOCK_VIA_BUSY_WAIT(rv, m)
#endif /* SERVER_MODE */

#if defined(PAGE_STATISTICS)
#define PGBUF_LATCH_MODE_COUNT  (PGBUF_LATCH_VICTIM_INVALID-PGBUF_NO_LATCH+1)
#endif /* PAGE_STATISTICS */

/* BCB zone */
typedef enum
{
  PGBUF_LRU_1_ZONE = 0,
  PGBUF_LRU_2_ZONE,
  PGBUF_INVALID_ZONE,
  PGBUF_VOID_ZONE,
  PGBUF_AIN_ZONE
} PGBUF_ZONE;

/* buffer lock return value */
enum
{
  PGBUF_LOCK_WAITER = 0, PGBUF_LOCK_HOLDER
};

/* constants to indicate the content state of buffers */
enum
{
  PGBUF_CONTENT_BAD = 0,	/* A bug in the system  */
  PGBUF_CONTENT_GOOD,		/* Content is consistent */
  PGBUF_CONTENT_LIKELY_BAD,	/* Maybe a bug in the system */
  PGBUF_CONTENT_ERROR		/* Some kind of error */
};

typedef struct pgbuf_holder PGBUF_HOLDER;
typedef struct pgbuf_holder_anchor PGBUF_HOLDER_ANCHOR;
typedef struct pgbuf_holder_set PGBUF_HOLDER_SET;

typedef struct pgbuf_bcb PGBUF_BCB;
typedef struct pgbuf_iopage_buffer PGBUF_IOPAGE_BUFFER;
typedef struct pgbuf_aout_buf PGBUF_AOUT_BUF;

typedef struct pgbuf_buffer_lock PGBUF_BUFFER_LOCK;
typedef struct pgbuf_buffer_hash PGBUF_BUFFER_HASH;

typedef struct pgbuf_lru_list PGBUF_LRU_LIST;
typedef struct pgbuf_ain_list PGBUF_AIN_LIST;
typedef struct pgbuf_aout_list PGBUF_AOUT_LIST;

#define PGBUF_IS_2Q_ENABLED (pgbuf_Pool.buf_AIN_list.max_count != 0)

typedef struct pgbuf_invalid_list PGBUF_INVALID_LIST;
typedef struct pgbuf_victim_candidate_list PGBUF_VICTIM_CANDIDATE_LIST;

typedef struct pgbuf_buffer_pool PGBUF_BUFFER_POOL;

/* BCB holder entry */
struct pgbuf_holder
{
  int fix_count;		/* the count of fix by the holder */
  PGBUF_BCB *bufptr;		/* pointer to BCB */
  PGBUF_HOLDER *thrd_link;	/* the next BCB holder entry in the BCB holder
				 * list of thread */
  PGBUF_HOLDER *next_holder;	/* free BCB holder list of thread */
#if !defined(NDEBUG)
  char fixed_at[64 * 1024];
  int fixed_at_size;
#endif				/* NDEBUG */
};

/* thread related BCB holder list (it is owned by each thread) */
struct pgbuf_holder_anchor
{
  int num_free_cnt;		/* # of free BCB holder entries */
  int num_hold_cnt;		/* # of used BCB holder entries */
  PGBUF_HOLDER *thrd_free_list;	/* free BCB holder list */
  PGBUF_HOLDER *thrd_hold_list;	/* used(or hold) BCB holder list */
};

/* the entry(array structure) of free BCB holder list shared by threads */
struct pgbuf_holder_set
{
  PGBUF_HOLDER element[PGBUF_NUM_ALLOC_HOLDER];	/* BCB holder array */
  PGBUF_HOLDER_SET *next_set;	/* next array */
};

/* BCB structure */
struct pgbuf_bcb
{
#if defined(SERVER_MODE)
  pthread_mutex_t BCB_mutex;	/* BCB mutex */
#endif				/* SERVER_MODE */
  VPID vpid;			/* Volume and page identifier of resident page */
  int ipool;			/* Buffer pool index */
  int fcnt;			/* Fix count */
  int latch_mode;		/* page latch mode */
  int zone;			/* BCB zone */
#if defined(SERVER_MODE)
  THREAD_ENTRY *next_wait_thrd;	/* BCB waiting queue */
#endif				/* SERVER_MODE */
  PGBUF_BCB *hash_next;		/* next hash chain */
  PGBUF_BCB *prev_BCB;		/* prev LRU chain */
  PGBUF_BCB *next_BCB;		/* next LRU or Invalid(Free) chain */
  int ain_tick;			/* age of AIN when this BCB was inserted into
				 * AIN list */
  unsigned dirty:1;		/* Is page dirty ? */
  unsigned avoid_victim:1;
  unsigned async_flush_request:1;
  unsigned victim_candidate:1;
  LOG_LSA oldest_unflush_lsa;	/* The oldest LSA record of the page
				   that has not been written to disk */
  PGBUF_IOPAGE_BUFFER *iopage_buffer;	/* pointer to iopage buffer structure */
};

/* iopage buffer structure */
struct pgbuf_iopage_buffer
{
  PGBUF_BCB *bcb;		/* pointer to BCB structure */
#if (__WORDSIZE == 32)
  int dummy;			/* for 8byte align of iopage */
#elif !defined(LINUX) && !defined(WINDOWS) && !defined(AIX)
#error "you must check that iopage is aligned by 8byte !!"
#endif
  FILEIO_PAGE iopage;		/* The actual buffered io page */
};

/* buffer lock record (or entry) structure
 *
 * buffer lock table is the array of buffer lock records
 * # of buffer lock records is fixed as the total # of threads.
 */
struct pgbuf_buffer_lock
{
  VPID vpid;			/* buffer-locked page id */
  PGBUF_BUFFER_LOCK *lock_next;	/* next buffer lock record */
#if defined(SERVER_MODE)
  THREAD_ENTRY *next_wait_thrd;	/* buffer-lock waiting queue */
#endif				/* SERVER_MODE */
};

/* buffer hash entry structure
 *
 * buffer hash table is the array of buffer hash entries.
 * Now, # of buffer hash entries is (8 * # of buffer frames)
 */
struct pgbuf_buffer_hash
{
#if defined(SERVER_MODE)
  pthread_mutex_t hash_mutex;	/* hash mutex for the integrity of buffer hash
				   chain and buffer lock chain. */
#endif				/* SERVER_MODE */
  PGBUF_BCB *hash_next;		/* the anchor of buffer hash chain */
  PGBUF_BUFFER_LOCK *lock_next;	/* the anchor of buffer lock chain */
};

/* buffer LRU list structure : double linked list */
struct pgbuf_lru_list
{
#if defined(SERVER_MODE)
  pthread_mutex_t LRU_mutex;	/* LRU mutex for the integrity of LRU list. */
#endif				/* SERVER_MODE */
  PGBUF_BCB *LRU_top;		/* top of the LRU list */
  PGBUF_BCB *LRU_bottom;	/* bottom of the LRU list */
  PGBUF_BCB *LRU_middle;	/* the last of LRU_1_Zone */
  int LRU_1_zone_cnt;
};

/* buffer invalid BCB list : single linked list */
struct pgbuf_invalid_list
{
#if defined(SERVER_MODE)
  pthread_mutex_t invalid_mutex;	/* invalid mutex for the integrity of invalid
					   BCB list. */
#endif				/* SERVER_MODE */
  PGBUF_BCB *invalid_top;	/* top of the invalid BCB list */
  int invalid_cnt;		/* # of entries in invalid BCB list */
};

/* The page replacement algorithm is 2Q. This algorithm uses three linked
 * lists as follows:
 *  * LRU list : this is a list of BCBs managed as a Least Recently Used queue
 *  * Ain list : this is a list of BCBs managed as a FIFO queue
 *  * Aout list : this is a list on VPIDs managed as a FIFO queue
 * The LRU list manages the "hot" pages, Aout list holds a short term history
 * of pages which have been victimized and the Ain list manages pages (already
 * in memory) for which the status is uncertain (not enough information to
 * consider them hot yet).
 */
/* Aout list node */
struct pgbuf_aout_buf
{
  VPID vpid;			/* page VPID */
  PGBUF_AOUT_BUF *next;		/* next element in list */
  PGBUF_AOUT_BUF *prev;		/* prev element in list */
};

/* Aout list */
struct pgbuf_aout_list
{
#if defined(SERVER_MODE)
  pthread_mutex_t Aout_mutex;	/* Aout mutex for the integrity of Aout list. */
#endif				/* SERVER_MODE */
  PGBUF_AOUT_BUF *Aout_top;	/* top of the queue */
  PGBUF_AOUT_BUF *Aout_bottom;	/* bottom of the queue */

  PGBUF_AOUT_BUF *Aout_free;	/* a free list of Aout nodes */

  PGBUF_AOUT_BUF *bufarray;	/* Array holding all the nodes in the list.
				 * Since Aout has a predefined fixed size, it
				 * makes more sense to preallocate all the
				 * nodes
				 */
  int num_hashes;		/* number of hash tables */
  MHT_TABLE **aout_buf_ht;	/* hash table for fast history lookup. */

  int max_count;		/* maximum size of the Aout queue */
};

/* Ain list */
struct pgbuf_ain_list
{
#if defined(SERVER_MODE)
  pthread_mutex_t Ain_mutex;	/* Ain mutex for the integrity of Ain list. */
#endif				/* SERVER_MODE */
  PGBUF_BCB *Ain_top;		/* top of the queue */
  PGBUF_BCB *Ain_bottom;	/* bottom of the queue */
  int ain_count;		/* number of elements in the queue */
  int max_count;		/* configured maximum number of elements the
				 * Ain list should hold.
				 * Note that it is possible that ain_count is
				 * greater than max_count. This is not an
				 * error, it just means that victims should be
				 * taken from the Ain list and not the LRU
				 * list.
				 */
  int tick;			/* age of queue in number of buffers inserted
				 * in this list */
};

/* The buffer Pool */
struct pgbuf_buffer_pool
{
  /* total # of buffer frames on the buffer (fixed value: 10 * num_trans) */
  int num_buffers;

  /* buffer related tables and lists (the essential structures) */

  PGBUF_BCB *BCB_table;		/* BCB table */
  PGBUF_BUFFER_HASH *buf_hash_table;	/* buffer hash table */
  PGBUF_BUFFER_LOCK *buf_lock_table;	/* buffer lock table */
  PGBUF_IOPAGE_BUFFER *iopage_table;	/* IO page table */
  int num_LRU_list;		/* number of LRU lists */
  int num_LRU1_zone_threshold;	/* target number of pages in LRU1 zone */
  int last_flushed_LRU_list_idx;	/* index of the last flushed LRU
					 * list */
  PGBUF_LRU_LIST *buf_LRU_list;	/* LRU lists */
  PGBUF_AIN_LIST buf_AIN_list;	/* Ain list */
  PGBUF_AOUT_LIST buf_AOUT_list;	/* Aout list */
  PGBUF_INVALID_LIST buf_invalid_list;	/* buffer invalid BCB list */

  PGBUF_VICTIM_CANDIDATE_LIST *victim_cand_list;

  /*
   * the structures for maintaining information on BCB holders.
   * 'thrd_holder_info' has entries as many as the # of threads and
   * each entry maintains free BCB holder list and used BCB holder list
   * of the corresponding thread.
   * 'thrd_reserved_holder' has memory space for all BCB holder entries.
   */
  PGBUF_HOLDER_ANCHOR *thrd_holder_info;
  PGBUF_HOLDER *thrd_reserved_holder;

  /*
   * free BCB holder list shared by all the threads.
   * When a thread needs more free BCB holder entries,
   * the thread allocates them one by one from this list.
   * However, the thread never return the entries into this list.
   * The structure is a list of the arrays of BCB holder entries.
   * 'free_holder_set' points to the first array that has free entries
   * and 'free_index' indicates the first free entry in the array.
   */
#if defined(SERVER_MODE)
  pthread_mutex_t free_holder_set_mutex;
#endif				/* SERVER_MODE */
  PGBUF_HOLDER_SET *free_holder_set;
  int free_index;

  /* 'check_for_interrupt' is set true when interrupts must be checked.
     Log manager set and clears this value while holding TR_TABLE_CS. */
  bool check_for_interrupts;

#if defined(SERVER_MODE)
  pthread_mutex_t volinfo_mutex;
#endif				/* SERVER_MODE */
  VOLID last_perm_volid;	/* last perm. volume id */
  int num_permvols_tmparea;	/* # of perm. vols for temp */
  int size_permvols_tmparea_volids;	/* size of the array */
  VOLID *permvols_tmparea_volids;	/* the volids array */

  int lru_victim_req_cnt;	/* number of victim request from this queue */
  int ain_victim_req_cnt;

  int fix_req_cnt;
};

/* victim candidate list */
/* One daemon thread performs flush task for victim candidates.
 * The daemon find and saves victim candidates using following list.
 * And then, based on the list, the daemon performs actual flush task.
 */
struct pgbuf_victim_candidate_list
{
  PGBUF_BCB *bufptr;		/* selected BCB as victim candidate */
  VPID vpid;			/* page id of the page managed by the BCB */
  LOG_LSA recLSA;		/* oldest_unflush_lsa of the page */
};

#if defined(PAGE_STATISTICS)
typedef struct pgbuf_page_stat PGBUF_PAGE_STAT;
struct pgbuf_page_stat
{
  int volid;
  int pageid;
  int latch_cnt[PGBUF_LATCH_MODE_COUNT];
  struct timeval latch_time[PGBUF_LATCH_MODE_COUNT];
};

typedef struct pgbuf_vol_stat PGBUF_VOL_STAT;
struct pgbuf_vol_stat
{
  int volid;
  int npages;
  PGBUF_PAGE_STAT *page_stat;
};

typedef struct pgbuf_ps_info PGBUF_PS_INFO;
struct pgbuf_ps_info
{
  int nvols;
  int ps_init_called;
  PGBUF_VOL_STAT *vol_stat;
};
#endif /* PAGE_STATISTICS */

static PGBUF_BUFFER_POOL pgbuf_Pool;	/* The buffer Pool */

#if defined(CUBRID_DEBUG)
/* A buffer guard to detect over runs .. */
static char pgbuf_Guard[8] =
  { MEM_REGION_GUARD_MARK, MEM_REGION_GUARD_MARK, MEM_REGION_GUARD_MARK,
  MEM_REGION_GUARD_MARK,
  MEM_REGION_GUARD_MARK, MEM_REGION_GUARD_MARK, MEM_REGION_GUARD_MARK,
  MEM_REGION_GUARD_MARK
};
#endif /* CUBRID_DEBUG */

#if defined(PAGE_STATISTICS)
static PGBUF_PS_INFO ps_info;
#endif /* PAGE_STATISTICS */

#define AOUT_HASH_DIVIDE_RATIO 1000
#define AOUT_HASH_IDX(vpid, list) ((vpid)->pageid % list->num_hashes)

static unsigned int pgbuf_hash_func_mirror (const VPID * vpid);
static bool pgbuf_is_temporary_volume (VOLID volid);
static int pgbuf_initialize_bcb_table (void);
static int pgbuf_initialize_hash_table (void);
static int pgbuf_initialize_lock_table (void);
static int pgbuf_initialize_lru_list (void);
static int pgbuf_initialize_ain_list (void);
static int pgbuf_initialize_aout_list (void);
static int pgbuf_initialize_invalid_list (void);
static int pgbuf_initialize_thrd_holder (void);
static PGBUF_HOLDER *pgbuf_allocate_thrd_holder_entry (THREAD_ENTRY *
						       thread_p);
static PGBUF_HOLDER *pgbuf_find_thrd_holder (THREAD_ENTRY * thread_p,
					     PGBUF_BCB * bufptr);
static int pgbuf_remove_thrd_holder (THREAD_ENTRY * thread_p,
				     PGBUF_HOLDER * holder);
static int pgbuf_unlatch_thrd_holder (THREAD_ENTRY * thread_p,
				      PGBUF_BCB * bufptr);
#if !defined(NDEBUG)
static int pgbuf_latch_bcb_upon_fix (THREAD_ENTRY * thread_p,
				     PGBUF_BCB * bufptr, int request_mode,
				     int buf_lock_acquired,
				     PGBUF_LATCH_CONDITION condition,
				     const char *caller_file,
				     int caller_line);
static int pgbuf_unlatch_bcb_upon_unfix (THREAD_ENTRY * thread_p,
					 PGBUF_BCB * bufptr,
					 int holder_status,
					 const char *caller_file,
					 int caller_line);
static int pgbuf_block_bcb (THREAD_ENTRY * thread_p, PGBUF_BCB * bufptr,
			    int request_mode, int request_fcnt,
			    const char *caller_file, int caller_line);
#else /* NDEBUG */
static int pgbuf_latch_bcb_upon_fix (THREAD_ENTRY * thread_p,
				     PGBUF_BCB * bufptr, int request_mode,
				     int buf_lock_acquired,
				     PGBUF_LATCH_CONDITION condition);
static int pgbuf_unlatch_bcb_upon_unfix (THREAD_ENTRY * thread_p,
					 PGBUF_BCB * bufptr,
					 int holder_status);
static int pgbuf_block_bcb (THREAD_ENTRY * thread_p, PGBUF_BCB * bufptr,
			    int request_mode, int request_fcnt);
#endif /* NDEBUG */
static PGBUF_BCB *pgbuf_search_hash_chain (PGBUF_BUFFER_HASH * hash_anchor,
					   const VPID * vpid);
static int pgbuf_insert_into_hash_chain (PGBUF_BUFFER_HASH * hash_anchor,
					 PGBUF_BCB * bufptr);
static int pgbuf_delete_from_hash_chain (PGBUF_BCB * bufptr);
static int pgbuf_lock_page (THREAD_ENTRY * thread_p,
			    PGBUF_BUFFER_HASH * hash_anchor,
			    const VPID * vpid);
static int pgbuf_unlock_page (PGBUF_BUFFER_HASH * hash_anchor,
			      const VPID * vpid, int need_hash_mutex);
#if !defined(NDEBUG)
static PGBUF_BCB *pgbuf_allocate_bcb (THREAD_ENTRY * thread_p,
				      const VPID * src_vpid,
				      const char *caller_file,
				      int caller_line);
static int pgbuf_victimize_bcb (THREAD_ENTRY * thread_p,
				PGBUF_BCB * bufptr,
				const char *caller_file, int caller_line);
static int pgbuf_flush_bcb (THREAD_ENTRY * thread_p, PGBUF_BCB * bufptr,
			    int synchronous,
			    const char *caller_file, int caller_line);
#else /* NDEBUG */
static PGBUF_BCB *pgbuf_allocate_bcb (THREAD_ENTRY * thread_p,
				      const VPID * src_vpid);
static int pgbuf_victimize_bcb (THREAD_ENTRY * thread_p, PGBUF_BCB * bufptr);
static int pgbuf_flush_bcb (THREAD_ENTRY * thread_p, PGBUF_BCB * bufptr,
			    int synchronous);
#endif /* NDEBUG */
static int pgbuf_invalidate_bcb (PGBUF_BCB * bufptr);
static PGBUF_BCB *pgbuf_get_bcb_from_invalid_list (void);
static int pgbuf_put_bcb_into_invalid_list (PGBUF_BCB * bufptr);
static int pgbuf_get_lru_index (const VPID * vpid);
static int pgbuf_get_victim_candidates_from_ain (int check_count);
static int pgbuf_get_victim_candidates_from_lru (int check_count,
						 int victim_count);
static PGBUF_BCB *pgbuf_get_victim (THREAD_ENTRY * thread_p,
				    const VPID * vpid, int max_count);
static PGBUF_BCB *pgbuf_get_victim_from_ain_list (THREAD_ENTRY * thread_p,
						  int max_count);
static PGBUF_BCB *pgbuf_get_victim_from_lru_list (THREAD_ENTRY * thread_p,
						  const VPID * vpid,
						  int max_count);
static void pgbuf_add_vpid_to_aout_list (THREAD_ENTRY * thread_p,
					 const VPID * vpid);
static bool pgbuf_remove_vpid_from_aout_list (THREAD_ENTRY * thread_p,
					      const VPID * vpid);
static int pgbuf_invalidate_bcb_from_lru (PGBUF_BCB * bufptr);
static int pgbuf_invalidate_bcb_from_ain (PGBUF_BCB * bufptr);
static int pgbuf_relocate_top_lru (PGBUF_BCB * bufptr, int dest_zone);

static int pgbuf_relocate_bottom_lru (PGBUF_BCB * bufptr);
static int pgbuf_relocate_top_ain (PGBUF_BCB * bufptr);
static void pgbuf_remove_from_lru_list (PGBUF_BCB * bufptr,
					PGBUF_LRU_LIST * lru_list);
static void pgbuf_remove_from_ain_list (PGBUF_BCB * bufptr);
static void pgbuf_move_from_ain_to_lru (PGBUF_BCB * bufptr);

static int pgbuf_flush_page_with_wal (THREAD_ENTRY * thread_p,
				      PGBUF_BCB * bufptr);
static bool pgbuf_is_exist_blocked_reader_writer (PGBUF_BCB * bufptr);
static bool pgbuf_is_exist_blocked_reader_writer_victim (PGBUF_BCB * bufptr);
#if !defined(NDEBUG)
static int pgbuf_flush_all_helper (THREAD_ENTRY * thread_p, VOLID volid,
				   bool is_only_fixed,
				   bool is_set_lsa_as_null,
				   const char *caller_file, int caller_line);
#else /* NDEBUG */
static int pgbuf_flush_all_helper (THREAD_ENTRY * thread_p, VOLID volid,
				   bool is_only_fixed,
				   bool is_set_lsa_as_null);
#endif /* NDEBUG */

#if defined(SERVER_MODE)
static THREAD_ENTRY *pgbuf_kickoff_blocked_victim_request (PGBUF_BCB *
							   bufptr);
#if !defined(NDEBUG)
static int pgbuf_wakeup_bcb (THREAD_ENTRY * thread_p, PGBUF_BCB * bufptr,
			     const char *caller_file, int caller_line);
static int pgbuf_timed_sleep_error_handling (THREAD_ENTRY * thread_p,
					     PGBUF_BCB * bufptr,
					     THREAD_ENTRY * thrd_entry,
					     const char *caller_file,
					     int caller_line);
static int pgbuf_timed_sleep (THREAD_ENTRY * thread_p, PGBUF_BCB * bufptr,
			      THREAD_ENTRY * thrd_entry,
			      const char *caller_file, int caller_line);
#else /* NDEBUG */
static int pgbuf_wakeup_bcb (THREAD_ENTRY * thread_p, PGBUF_BCB * bufptr);
static int pgbuf_timed_sleep_error_handling (THREAD_ENTRY * thread_p,
					     PGBUF_BCB * bufptr,
					     THREAD_ENTRY * thrd_entry);
static int pgbuf_timed_sleep (THREAD_ENTRY * thread_p, PGBUF_BCB * bufptr,
			      THREAD_ENTRY * thrd_entry);
#endif /* NDEBUG */
#endif /* SERVER_MODE */

static bool pgbuf_get_check_page_validation (THREAD_ENTRY * thread_p,
					     int page_validation_level);
static bool pgbuf_is_valid_page_ptr (const PAGE_PTR pgptr);
static void pgbuf_set_bcb_page_vpid (THREAD_ENTRY * thread_p,
				     PGBUF_BCB * bufptr);
static bool pgbuf_check_bcb_page_vpid (THREAD_ENTRY * thread_p,
				       PGBUF_BCB * bufptr);

#if defined(CUBRID_DEBUG)
static void pgbuf_scramble (FILEIO_PAGE * iopage);
static void pgbuf_dump (void);
static int pgbuf_is_consistent (const PGBUF_BCB * bufptr,
				int likely_bad_after_fixcnt);

#endif /* CUBRID_DEBUG */

#if defined(PAGE_STATISTICS)
static int pgbuf_initialize_statistics (void);
static int pgbuf_finalize_statistics (void);
static void pgbuf_dump_statistics (FILE * ps_log);
#endif /* PAGE_STATISTICS */

#if !defined(NDEBUG)
static void pgbuf_add_fixed_at (PGBUF_HOLDER * holder,
				const char *caller_file, int caller_line);
#endif

#if defined(SERVER_MODE)
static int pgbuf_sleep (THREAD_ENTRY * thread_p, pthread_mutex_t * mutex_p);
static int pgbuf_wakeup (THREAD_ENTRY * thread_p);
static int pgbuf_wakeup_uncond (THREAD_ENTRY * thread_p);
#endif /* SERVER_MODE */
static void pgbuf_set_dirty_buffer_ptr (THREAD_ENTRY * thread_p,
					PGBUF_BCB * bufptr);
static int pgbuf_compare_victim_list (const void *p1, const void *p2);
static void pgbuf_wakeup_flush_thread (THREAD_ENTRY * thread_p);

/*
 * pgbuf_hash_func_mirror () - Hash VPID into hash anchor
 *   return: hash value
 *   key_vpid(in): VPID to hash
 */
static unsigned int
pgbuf_hash_func_mirror (const VPID * vpid)
{
#define VOLID_LSB_BITS 8
  int i;
  unsigned int hash_val;
  unsigned int volid_lsb;
  unsigned int reversed_volid_lsb = 0;
  unsigned int lsb_mask;
  unsigned int reverse_mask;

  volid_lsb = vpid->volid;

  lsb_mask = 1;
  reverse_mask = 1 << (HASH_SIZE_BITS - 1);

  for (i = VOLID_LSB_BITS; i > 0; i--)
    {
      if (volid_lsb & lsb_mask)
	{
	  reversed_volid_lsb |= reverse_mask;
	}
      reverse_mask = reverse_mask >> 1;
      lsb_mask = lsb_mask << 1;
    }

  hash_val = vpid->pageid ^ reversed_volid_lsb;
  hash_val = hash_val & ((1 << HASH_SIZE_BITS) - 1);

  return hash_val;
#undef VOLID_LSB_BITS
}

/*
 * pgbuf_hash_vpid () - Hash a volume_page identifier
 *   return: hash value
 *   key_vpid(in): VPID to hash
 *   htsize(in): Size of hash table
 */
unsigned int
pgbuf_hash_vpid (const void *key_vpid, unsigned int htsize)
{
  const VPID *vpid = (VPID *) key_vpid;

  return ((vpid->pageid | ((unsigned int) vpid->volid) << 24) % htsize);
}

/*
 * pgbuf_compare_vpid () - Compare two vpids keys for hashing
 *   return: int (key_vpid1 == key_vpid2 ?)
 *   key_vpid1(in): First key
 *   key_vpid2(in): Second key
 */
int
pgbuf_compare_vpid (const void *key_vpid1, const void *key_vpid2)
{
  const VPID *vpid1 = (VPID *) key_vpid1;
  const VPID *vpid2 = (VPID *) key_vpid2;

  return VPID_EQ (vpid1, vpid2);
}

/*
 * pgbuf_initialize () - Initialize the page buffer pool
 *   return: NO_ERROR, or ER_code
 *
 * Note: Function invalidates any resident page, creates a hash table for easy
 *       lookup of pages in the page buffer pool, and resets the clock tick for
 *       the  page replacement algorithm.
 */
int
pgbuf_initialize (void)
{
  pgbuf_Pool.num_buffers = prm_get_integer_value (PRM_ID_PB_NBUFFERS);
  if (pgbuf_Pool.num_buffers < PGBUF_MINIMUM_BUFFERS)
    {
#if defined(CUBRID_DEBUG)
      er_log_debug (ARG_FILE_LINE, "pgbuf_initialize: WARNING "
		    "Num_buffers = %d is too small. %d was assumed",
		    pgbuf_Pool.num_buffers, PGBUF_MINIMUM_BUFFERS);
#endif /* CUBRID_DEBUG */
      pgbuf_Pool.num_buffers = PGBUF_MINIMUM_BUFFERS;
    }

  if (pgbuf_initialize_bcb_table () != NO_ERROR)
    {
      goto error;
    }

  if (pgbuf_initialize_hash_table () != NO_ERROR)
    {
      goto error;
    }

  if (pgbuf_initialize_lock_table () != NO_ERROR)
    {
      goto error;
    }

  if (pgbuf_initialize_lru_list () != NO_ERROR)
    {
      goto error;
    }

  if (pgbuf_initialize_invalid_list () != NO_ERROR)
    {
      goto error;
    }

  if (pgbuf_initialize_ain_list () != NO_ERROR)
    {
      goto error;
    }

  if (pgbuf_initialize_aout_list () != NO_ERROR)
    {
      goto error;
    }

  if (pgbuf_initialize_thrd_holder () != NO_ERROR)
    {
      goto error;
    }

  pgbuf_Pool.check_for_interrupts = false;
  pthread_mutex_init (&pgbuf_Pool.volinfo_mutex, NULL);
  pgbuf_Pool.last_perm_volid = LOG_MAX_DBVOLID;
  pgbuf_Pool.num_permvols_tmparea = 0;
  pgbuf_Pool.size_permvols_tmparea_volids = 0;
  pgbuf_Pool.permvols_tmparea_volids = NULL;

  pgbuf_Pool.victim_cand_list = ((PGBUF_VICTIM_CANDIDATE_LIST *)
				 malloc (pgbuf_Pool.num_buffers *
					 sizeof
					 (PGBUF_VICTIM_CANDIDATE_LIST)));
  if (pgbuf_Pool.victim_cand_list == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
	      1, (pgbuf_Pool.num_buffers *
		  sizeof (PGBUF_VICTIM_CANDIDATE_LIST)));
      goto error;
    }

  pgbuf_Pool.lru_victim_req_cnt = 0;
  pgbuf_Pool.ain_victim_req_cnt = 0;
  pgbuf_Pool.fix_req_cnt = 0;

#if defined(PAGE_STATISTICS)
  if (pgbuf_initialize_statistics () < 0)
    {
      fprintf (stderr, "pgbuf_initialize_statistics() failed\n");
      goto error;
    }
#endif /* PAGE_STATISTICS */

  return NO_ERROR;

error:
  /* destroy mutexes and deallocate all the allocated memory */
  pgbuf_finalize ();
  return ER_FAILED;
}

/*
 * pgbuf_finalize () - Terminate the page buffer manager
 *   return: void
 *
 * Note: Function invalidates any resident page, destroys the hash table used
 *       for lookup of pages in the page buffer pool.
 */
void
pgbuf_finalize (void)
{
  PGBUF_BCB *bufptr;
  void *area;
  PGBUF_HOLDER_SET *holder_set;
  int i;
  size_t hash_size, j;

#if defined(CUBRID_DEBUG)
  pgbuf_dump_if_any_fixed ();
#endif /* CUBRID_DEBUG */

#if defined(PAGE_STATISTICS)
  if (pgbuf_finalize_statistics () < 0)
    {
      fprintf (stderr, "pgbuf_finalize_statistics() failed\n");
    }
#endif /* PAGE_STATISTICS */

  /* final task for buffer hash table */
  if (pgbuf_Pool.buf_hash_table != NULL)
    {
      hash_size = PGBUF_HASH_SIZE;
      for (j = 0; j < hash_size; j++)
	{
	  pthread_mutex_destroy (&pgbuf_Pool.buf_hash_table[j].hash_mutex);
	}
      free_and_init (pgbuf_Pool.buf_hash_table);
    }

  /* final task for buffer lock table */
  if (pgbuf_Pool.buf_lock_table != NULL)
    {
      free_and_init (pgbuf_Pool.buf_lock_table);
    }

  /* final task for BCB table */
  if (pgbuf_Pool.BCB_table != NULL)
    {
      for (i = 0; i < pgbuf_Pool.num_buffers; i++)
	{
	  bufptr = PGBUF_FIND_BCB_PTR (i);
	  pthread_mutex_destroy (&bufptr->BCB_mutex);
	}
      free_and_init (pgbuf_Pool.BCB_table);
      pgbuf_Pool.num_buffers = 0;
    }

  if (pgbuf_Pool.iopage_table != NULL)
    {
      free_and_init (pgbuf_Pool.iopage_table);
    }

  /* final task for LRU list */
  if (pgbuf_Pool.buf_LRU_list != NULL)
    {
      for (i = 0; i < pgbuf_Pool.num_LRU_list; i++)
	{
	  pthread_mutex_destroy (&pgbuf_Pool.buf_LRU_list[i].LRU_mutex);
	}
      free_and_init (pgbuf_Pool.buf_LRU_list);
    }

  /* final task for invalid BCB list */
  pthread_mutex_destroy (&pgbuf_Pool.buf_invalid_list.invalid_mutex);

  /* final task for thrd_holder_info */
  if (pgbuf_Pool.thrd_holder_info != NULL)
    {
      free_and_init (pgbuf_Pool.thrd_holder_info);
    }

  if (pgbuf_Pool.thrd_reserved_holder != NULL)
    {
      free_and_init (pgbuf_Pool.thrd_reserved_holder);
    }

  /* final task for free holder set */
  pthread_mutex_destroy (&pgbuf_Pool.free_holder_set_mutex);
  while (pgbuf_Pool.free_holder_set != NULL)
    {
      holder_set = pgbuf_Pool.free_holder_set;
      pgbuf_Pool.free_holder_set = holder_set->next_set;
      free_and_init (holder_set);
    }

  /* final task for volume info */
  pthread_mutex_destroy (&pgbuf_Pool.volinfo_mutex);
  area = pgbuf_Pool.permvols_tmparea_volids;
  if (area != NULL)
    {
      pgbuf_Pool.num_permvols_tmparea = 0;
      pgbuf_Pool.size_permvols_tmparea_volids = 0;
      pgbuf_Pool.permvols_tmparea_volids = NULL;
      free_and_init (area);
    }

  if (pgbuf_Pool.victim_cand_list != NULL)
    {
      free_and_init (pgbuf_Pool.victim_cand_list);
    }

  pthread_mutex_destroy (&pgbuf_Pool.buf_AIN_list.Ain_mutex);

  if (pgbuf_Pool.buf_AOUT_list.bufarray != NULL)
    {
      free_and_init (pgbuf_Pool.buf_AOUT_list.bufarray);
    }

  if (pgbuf_Pool.buf_AOUT_list.aout_buf_ht != NULL)
    {
      for (i = 0; i < pgbuf_Pool.buf_AOUT_list.num_hashes; i++)
	{
	  mht_destroy (pgbuf_Pool.buf_AOUT_list.aout_buf_ht[i]);
	}
      free_and_init (pgbuf_Pool.buf_AOUT_list.aout_buf_ht);

      pgbuf_Pool.buf_AOUT_list.num_hashes = 0;
    }


  pthread_mutex_destroy (&pgbuf_Pool.buf_AOUT_list.Aout_mutex);

  pgbuf_Pool.buf_AOUT_list.aout_buf_ht = NULL;
  pgbuf_Pool.buf_AOUT_list.Aout_bottom = NULL;
  pgbuf_Pool.buf_AOUT_list.Aout_top = NULL;
  pgbuf_Pool.buf_AOUT_list.Aout_free = NULL;
  pgbuf_Pool.buf_AOUT_list.max_count = 0;
}

/*
 * pgbuf_fix_with_retry () -
 *   return: Pointer to the page or NULL
 *   vpid(in): Complete Page identifier
 *   newpg(in): Is this a newly allocated page ?
 *   lock(in): Lock mode
 *   retry(in): Retry count
 */
PAGE_PTR
pgbuf_fix_with_retry (THREAD_ENTRY * thread_p, const VPID * vpid, int newpg,
		      int mode, int retry)
{
  PAGE_PTR pgptr;
  int i = 0;
  bool noretry = false;

  while ((pgptr = pgbuf_fix (thread_p, vpid, newpg, mode,
			     PGBUF_UNCONDITIONAL_LATCH)) == NULL)
    {
      switch (er_errid ())
	{
	case NO_ERROR:		/* interrupt */
	case ER_INTERRUPTED:
	  break;
	case ER_LK_UNILATERALLY_ABORTED:	/* timeout */
	case ER_LK_PAGE_TIMEOUT:
	case ER_PAGE_LATCH_TIMEDOUT:
	  i++;
	  break;
	default:
	  noretry = true;
	  break;
	}

      if (noretry || i > retry)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_PAGE_LATCH_ABORTED, 2, vpid->volid, vpid->pageid);
	  break;
	}
    }

  return pgptr;
}

/*
 * below two functions are dummies for Windows build
 * (which defined at cubridsa.def)
 */
#if defined(WINDOWS)
#if !defined(NDEBUG)
PAGE_PTR
pgbuf_fix_release (THREAD_ENTRY * thread_p, const VPID * vpid, int newpg,
		   int request_mode, PGBUF_LATCH_CONDITION condition)
{
  return NULL;
}
#else
PAGE_PTR
pgbuf_fix_debug (THREAD_ENTRY * thread_p, const VPID * vpid, int newpg,
		 int request_mode, PGBUF_LATCH_CONDITION condition,
		 const char *caller_file, int caller_line)
{
  return NULL;
}
#endif
#endif

#if !defined(NDEBUG)
PAGE_PTR
pgbuf_fix_without_validation_debug (THREAD_ENTRY * thread_p,
				    const VPID * vpid, int newpg,
				    int request_mode,
				    PGBUF_LATCH_CONDITION condition,
				    const char *caller_file, int caller_line)
{
  PAGE_PTR pgptr;
  bool old_check_page_validation;

  old_check_page_validation =
    thread_set_check_page_validation (thread_p, false);

  pgptr = pgbuf_fix_debug (thread_p, vpid, newpg, request_mode, condition,
			   caller_file, caller_line);

  thread_set_check_page_validation (thread_p, old_check_page_validation);

  return pgptr;
}
#else /* NDEBUG */
PAGE_PTR
pgbuf_fix_without_validation_release (THREAD_ENTRY * thread_p,
				      const VPID * vpid, int newpg,
				      int request_mode,
				      PGBUF_LATCH_CONDITION condition)
{
  PAGE_PTR pgptr;
  bool old_check_page_validation;

  old_check_page_validation =
    thread_set_check_page_validation (thread_p, false);

  pgptr = pgbuf_fix_release (thread_p, vpid, newpg, request_mode, condition);

  thread_set_check_page_validation (thread_p, old_check_page_validation);

  return pgptr;
}
#endif /* NDEBUG */

/*
 * pgbuf_fix () -
 *   return: Pointer to the page or NULL
 *   vpid(in): Complete Page identifier
 *   newpg(in): Is this a newly allocated page ?
 *   request_mode(in):
 *   condition(in):
 */
#if !defined(NDEBUG)
PAGE_PTR
pgbuf_fix_debug (THREAD_ENTRY * thread_p, const VPID * vpid, int newpg,
		 int request_mode, PGBUF_LATCH_CONDITION condition,
		 const char *caller_file, int caller_line)
#else /* NDEBUG */
PAGE_PTR
pgbuf_fix_release (THREAD_ENTRY * thread_p, const VPID * vpid, int newpg,
		   int request_mode, PGBUF_LATCH_CONDITION condition)
#endif				/* NDEBUG */
{
  PGBUF_BUFFER_HASH *hash_anchor;
  PGBUF_BCB *bufptr;
  PAGE_PTR pgptr;
  int buf_lock_acquired;
  int wait_msecs;
#if defined(SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */
#if defined(ENABLE_SYSTEMTAP)
  bool pgbuf_hit = false;
  int tran_index;
  QMGR_TRAN_ENTRY *tran_entry;
  QUERY_ID query_id = -1;
  bool monitored = false;
#endif /* ENABLE_SYSTEMTAP */

  /* paramter validation */
  if (request_mode != PGBUF_LATCH_READ && request_mode != PGBUF_LATCH_WRITE)
    {
      assert_release (false);
      return NULL;
    }
  if (condition != PGBUF_UNCONDITIONAL_LATCH
      && condition != PGBUF_CONDITIONAL_LATCH)
    {
      assert_release (false);
      return NULL;
    }

  ATOMIC_INC_32 (&pgbuf_Pool.fix_req_cnt, 1);

  if (pgbuf_get_check_page_validation (thread_p,
				       PGBUF_DEBUG_PAGE_VALIDATION_FETCH))
    {
      /* Make sure that the page has been allocated (i.e., is a valid page) */
      if (pgbuf_is_valid_page (thread_p, vpid, false,
			       NULL, NULL) != DISK_VALID)
	{
	  return NULL;
	}
    }

  /* Do a simple check in non debugging mode */
  if (vpid->pageid < 0)
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_PB_BAD_PAGEID, 2,
	      vpid->pageid, fileio_get_volume_label (vpid->volid, PEEK));
      return NULL;
    }

  if (condition == PGBUF_UNCONDITIONAL_LATCH)
    {
      /* Check the wait_msecs of current transaction.
       * If the wait_msecs is zero wait that means no wait,
       * change current request as a conditional request.
       */
      wait_msecs = logtb_find_current_wait_msecs (thread_p);

      if (wait_msecs == LK_ZERO_WAIT || wait_msecs == LK_FORCE_ZERO_WAIT)
	{
	  condition = PGBUF_CONDITIONAL_LATCH;
	}
    }

try_again:

  /* interrupt check */
  if (thread_get_check_interrupt (thread_p) == true)
    {
      if (logtb_is_interrupted (thread_p, true,
				&pgbuf_Pool.check_for_interrupts) == true)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INTERRUPTED, 0);
	  return NULL;
	}
    }

  /* Normal process */
  /* latch_mode = PGBUF_LATCH_READ/PGBUF_LATCH_WRITE */
  hash_anchor = &pgbuf_Pool.buf_hash_table[PGBUF_HASH_VALUE (vpid)];

  buf_lock_acquired = false;
  bufptr = pgbuf_search_hash_chain (hash_anchor, vpid);
#if defined(ENABLE_SYSTEMTAP)
  if (bufptr != NULL)
    {
      CUBRID_PGBUF_HIT ();
      pgbuf_hit = true;
    }
#endif /* ENABLE_SYSTEMTAP */

  if (bufptr == NULL)
    {
      /* The page is not found in the hash chain
       * the caller is holding hash_anchor->hash_mutex
       */
      if (er_errid () == ER_CSS_PTHREAD_MUTEX_TRYLOCK)
	{
	  pthread_mutex_unlock (&hash_anchor->hash_mutex);
	  return NULL;
	}

      /* In this case, the caller is holding only hash_anchor->hash_mutex.
       * The hash_anchor->hash_mutex is to be released in pgbuf_lock_page ().
       */
      if (pgbuf_lock_page (thread_p, hash_anchor, vpid) != PGBUF_LOCK_HOLDER)
	{
	  goto try_again;
	}

      /* Now, the caller is not holding any mutex. */
#if !defined(NDEBUG)
      bufptr = pgbuf_allocate_bcb (thread_p, vpid, caller_file, caller_line);
#else /* NDEBUG */
      bufptr = pgbuf_allocate_bcb (thread_p, vpid);
#endif /* NDEBUG */
      if (bufptr == NULL)
	{
	  (void) pgbuf_unlock_page (hash_anchor, vpid, true);
	  return NULL;
	}

      /* Currently, caller has one allocated BCB and is holding BCB_mutex */

      /* initialize the BCB */
      bufptr->vpid = *vpid;
      bufptr->dirty = false;
      bufptr->latch_mode = PGBUF_NO_LATCH;
      bufptr->async_flush_request = false;
      LSA_SET_NULL (&bufptr->oldest_unflush_lsa);

#if defined(ENABLE_SYSTEMTAP)
      if (newpg == NEW_PAGE && pgbuf_hit == false)
	{
	  pgbuf_hit = true;
	}
      if (newpg != NEW_PAGE)
	{
	  CUBRID_PGBUF_MISS ();
	}
#endif /* ENABLE_SYSTEMTAP */

      if (newpg != NEW_PAGE)
	{
	  /* Record number of reads in statistics */
	  mnt_pb_ioreads (thread_p);

#if defined(ENABLE_SYSTEMTAP)
	  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
	  tran_entry = qmgr_get_tran_entry (tran_index);
	  if (tran_entry->query_entry_list_p)
	    {
	      query_id = tran_entry->query_entry_list_p->query_id;
	      monitored = true;
	      CUBRID_IO_READ_START (query_id);
	    }
#endif /* ENABLE_SYSTEMTAP */

	  if (fileio_read (thread_p,
			   fileio_get_volume_descriptor (vpid->volid),
			   &bufptr->iopage_buffer->iopage, vpid->pageid,
			   IO_PAGESIZE) == NULL)
	    {
	      /* There was an error in reading the page.
	         Clean the buffer... since it may have been corrupted */

	      /* bufptr->BCB_mutex will be released in following function. */
	      pgbuf_put_bcb_into_invalid_list (bufptr);

	      /*
	       * Now, caller is not holding any mutex.
	       * the last argument of pgbuf_unlock_page () is true that
	       * means hash_mutex must be held before unlocking page.
	       */
	      (void) pgbuf_unlock_page (hash_anchor, vpid, true);

#if defined(ENABLE_SYSTEMTAP)
	      if (monitored == true)
		{
		  CUBRID_IO_READ_END (query_id, IO_PAGESIZE, 1);
		}
#endif /* ENABLE_SYSTEMTAP */

	      return NULL;
	    }

#if defined(ENABLE_SYSTEMTAP)
	  if (monitored == true)
	    {
	      CUBRID_IO_READ_END (query_id, IO_PAGESIZE, 0);
	    }
#endif /* ENABLE_SYSTEMTAP */
	  if (pgbuf_is_temporary_volume (vpid->volid) == true)
	    {
	      /* Check iff the first time to access */
	      if (!LSA_IS_INIT_TEMP (&bufptr->iopage_buffer->iopage.prv.lsa))
		{
		  LSA_SET_INIT_TEMP (&bufptr->iopage_buffer->iopage.prv.lsa);
		  pgbuf_set_dirty_buffer_ptr (thread_p, bufptr);
		}
	    }

#if !defined (NDEBUG)
	  /* perm volume */
	  if (bufptr->vpid.volid > NULL_VOLID)
	    {
	      if (!log_is_in_crash_recovery ())
		{
		  if (!LSA_ISNULL (&bufptr->iopage_buffer->iopage.prv.lsa))
		    {
		      assert (bufptr->iopage_buffer->iopage.prv.pageid != -1);
		      assert (bufptr->iopage_buffer->iopage.prv.volid != -1);
		    }
		}
	    }
#endif /* NDEBUG */

	  mnt_sort_io_pages (thread_p);
	}
      else
	{
	  /* the caller is holding bufptr->BCB_mutex */

#if defined(CUBRID_DEBUG)
	  pgbuf_scramble (&bufptr->iopage_buffer->iopage);
#endif /* CUBRID_DEBUG */

	  /* Don't need to read page from disk since it is a new page. */
	  if (pgbuf_is_temporary_volume (vpid->volid) == true)
	    {
	      LSA_SET_INIT_TEMP (&bufptr->iopage_buffer->iopage.prv.lsa);
	    }
	  else
	    {
	      LSA_SET_INIT_NONTEMP (&bufptr->iopage_buffer->iopage.prv.lsa);
	    }

	  /* perm volume */
	  if (bufptr->vpid.volid > NULL_VOLID)
	    {
	      /* Init Page identifier of NEW_PAGE */
	      bufptr->iopage_buffer->iopage.prv.pageid = -1;
	      bufptr->iopage_buffer->iopage.prv.volid = -1;
	    }

	  mnt_sort_data_pages (thread_p);
	}
      buf_lock_acquired = true;
    }

  /* At this place, the caller is holding bufptr->BCB_mutex */

  /* Set Page identifier iff needed */
  (void) pgbuf_set_bcb_page_vpid (thread_p, bufptr);

  if (pgbuf_check_bcb_page_vpid (thread_p, bufptr) != true)
    {
      if (buf_lock_acquired)
	{
	  /* bufptr->BCB_mutex will be released in the following function. */
	  pgbuf_put_bcb_into_invalid_list (bufptr);

	  /*
	   * Now, caller is not holding any mutex.
	   * the last argument of pgbuf_unlock_page () is true that
	   * means hash_mutex must be held before unlocking page.
	   */
	  (void) pgbuf_unlock_page (hash_anchor, vpid, true);
	}
      else
	{
	  pthread_mutex_unlock (&bufptr->BCB_mutex);
	}

      return NULL;
    }

  /* At this place, the caller is holding bufptr->BCB_mutex */

  /* Latch Pass */
#if !defined (NDEBUG)
  if (pgbuf_latch_bcb_upon_fix (thread_p, bufptr, request_mode,
				buf_lock_acquired, condition,
				caller_file, caller_line) != NO_ERROR)
#else /* NDEBUG */
  if (pgbuf_latch_bcb_upon_fix (thread_p, bufptr, request_mode,
				buf_lock_acquired, condition) != NO_ERROR)
#endif /* NDEBUG */
    {
      /* bufptr->BCB_mutex has been released,
         error was set in the function, */

      if (buf_lock_acquired)
	{
	  /* hold bufptr->BCB_mutex again */
	  rv = pthread_mutex_lock (&bufptr->BCB_mutex);

	  /* bufptr->BCB_mutex will be released in the following function. */
	  pgbuf_put_bcb_into_invalid_list (bufptr);

	  /*
	   * Now, caller is not holding any mutex.
	   * the last argument of pgbuf_unlock_page () is true that
	   * means hash_mutex must be held before unlocking page.
	   */
	  (void) pgbuf_unlock_page (hash_anchor, vpid, true);
	}

      return NULL;
    }

  assert (bufptr == bufptr->iopage_buffer->bcb);

  /* In case of NO_ERROR, bufptr->BCB_mutex has been released. */

  /* Dirty Pages Table Registration Pass */

  /* Currently, do nothing.
     Whenever the fixed page becomes dirty, oldest_unflush_lsa is set. */

  /* Hash Chain Connection Pass */
  if (buf_lock_acquired)
    {
      pgbuf_insert_into_hash_chain (hash_anchor, bufptr);

      /*
       * the caller is holding hash_anchor->hash_mutex.
       * Therefore, the third argument of pgbuf_unlock_page () is false
       * that means hash mutex does not need to be held.
       */
      (void) pgbuf_unlock_page (hash_anchor, vpid, false);
    }

  CAST_BFPTR_TO_PGPTR (pgptr, bufptr);

#if !defined (NDEBUG)
  thread_rc_track_meter (thread_p, caller_file, caller_line, 1, pgptr,
			 RC_PGBUF, MGR_DEF);
  if (pgbuf_is_lsa_temporary (pgptr))
    {
      thread_rc_track_meter (thread_p, caller_file, caller_line, 1, pgptr,
			     RC_PGBUF_TEMP, MGR_DEF);
    }
#endif /* NDEBUG */

  /* Record number of fetches in statistics */
  mnt_pb_fetches (thread_p);

  return pgptr;
}

/*
 * pgbuf_unfix () - Free the buffer where the page associated with pgptr resides
 *   return: void
 *   pgptr(in): Pointer to page
 *
 * Note: The page is subject to replacement, if not fixed by other thread of
 *       execution.
 */
#if !defined (NDEBUG)
void
pgbuf_unfix_debug (THREAD_ENTRY * thread_p, PAGE_PTR pgptr,
		   const char *caller_file, int caller_line)
#else /* NDEBUG */
void
pgbuf_unfix (THREAD_ENTRY * thread_p, PAGE_PTR pgptr)
#endif				/* NDEBUG */
{
  PGBUF_BCB *bufptr;
  int holder_status;
#if defined(SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */

#if defined(CUBRID_DEBUG)
  LOG_LSA restart_lsa;
#endif /* CUBRID_DEBUG */

#if !defined (NDEBUG)
  assert (pgptr != NULL);

  if (pgbuf_get_check_page_validation (thread_p,
				       PGBUF_DEBUG_PAGE_VALIDATION_FREE))
    {
      if (pgbuf_is_valid_page_ptr (pgptr) == false)
	{
	  return;
	}
    }
#else /* !NDEBUG */
  if (pgptr == NULL)
    {
      return;
    }
#endif /* !NDEBUG */

  /* Get the address of the buffer from the page and free the buffer */
  CAST_PGPTR_TO_BFPTR (bufptr, pgptr);
  assert (!VPID_ISNULL (&bufptr->vpid));

#if defined(CUBRID_DEBUG)
  /*
   * If the buffer is dirty and the log sequence address of the buffer
   * has not changed since the database restart, a warning is given about
   * lack of logging
   */
  if (bufptr->dirty == true
      && !LSA_IS_INIT_TEMP (&bufptr->iopage_buffer->iopage.prv.lsa)
      && PGBUF_IS_AUXILIARY_VOLUME (bufptr->vpid.volid) == false
      && !log_is_logged_since_restart (&bufptr->iopage_buffer->iopage.prv.
				       lsa))
    {
      er_log_debug (ARG_FILE_LINE, "pgbuf_unfix: WARNING: No logging on"
		    " dirty pageid = %d of Volume = %s.\n Recovery problems"
		    " may happen\n",
		    bufptr->vpid.pageid,
		    fileio_get_volume_label (bufptr->vpid.volid, PEEK));
      /*
       * Do not give warnings on this page any longer. Set the LSA of the
       * buffer for this purposes
       */
      pgbuf_set_lsa (thread_p, pgptr, log_get_restart_lsa ());
      pgbuf_set_lsa (thread_p, pgptr, &restart_lsa);
      LSA_COPY (&bufptr->oldest_unflush_lsa,
		&bufptr->iopage_buffer->iopage.prv.lsa);
    }

  /* Check for over runs */
  if (memcmp (PGBUF_FIND_BUFFER_GUARD (bufptr), pgbuf_Guard,
	      sizeof (pgbuf_Guard)) != 0)
    {
      er_log_debug (ARG_FILE_LINE, "pgbuf_unfix: SYSTEM ERROR"
		    "buffer of pageid = %d|%d has been OVER RUN",
		    bufptr->vpid.volid, bufptr->vpid.pageid);
      memcpy (PGBUF_FIND_BUFFER_GUARD (bufptr), pgbuf_Guard,
	      sizeof (pgbuf_Guard));
    }

  /* Give a warning if the page is not consistent */
  if (bufptr->fcnt <= 0)
    {
      er_log_debug (ARG_FILE_LINE,
		    "pgbuf_unfix: SYSTEM ERROR Freeing"
		    " too much buffer of pageid = %d of Volume = %s\n",
		    bufptr->vpid.pageid,
		    fileio_get_volume_label (bufptr->vpid.volid, PEEK));
    }
#endif /* CUBRID_DEBUG */

  holder_status = pgbuf_unlatch_thrd_holder (thread_p, bufptr);

  rv = pthread_mutex_lock (&bufptr->BCB_mutex);

#if !defined(NDEBUG)
  (void) pgbuf_unlatch_bcb_upon_unfix (thread_p, bufptr, holder_status,
				       caller_file, caller_line);
#else /* NDEBUG */
  (void) pgbuf_unlatch_bcb_upon_unfix (thread_p, bufptr, holder_status);
#endif /* NDEBUG */
  /* bufptr->BCB_mutex has been released in above function. */

#if defined(CUBRID_DEBUG)
  /*
   * CONSISTENCIES AND SCRAMBLES
   * You may want to tailor the following debugging block
   * since its operations and their implications are very expensive.
   * Too much I/O
   */
  if (pgbuf_get_check_page_validation (thread_p,
				       PGBUF_DEBUG_PAGE_VALIDATION_ALL))
    {
      /*
       * Check if the content of the page is consistent and then scramble
       * the page to detect illegal access to the page in the future.
       */
      rv = pthread_mutex_lock (&bufptr->BCB_mutex);
      if (bufptr->fcnt == 0)
	{
	  /* Check for consistency */
	  if (!VPID_ISNULL (&bufptr->vpid)
	      && pgbuf_is_consistent (bufptr, 0) == PGBUF_CONTENT_BAD)
	    {
	      er_log_debug (ARG_FILE_LINE, "pgbuf_unfix: WARNING"
			    " Pageid = %d|%d seems inconsistent",
			    bufptr->vpid.volid, bufptr->vpid.pageid);
	      /* some problems in the consistency of the given buffer page */
	      pgbuf_dump ();
	    }
	  else
	    {
	      /* the given buffer page is consistent */

	      /* Flush the page if it is dirty */
	      if (bufptr->dirty == true)
		{
		  /* flush the page with PGBUF_LATCH_FLUSH mode */
#if !defined(NDEBUG)
		  (void) pgbuf_flush_bcb (thread_p, bufptr, true,
					  caller_file, caller_line);
#else /* NDEBUG */
		  (void) pgbuf_flush_bcb (thread_p, bufptr, true);
#endif /* NDEBUG */
		  /*
		   * Since above function releases bufptr->BCB_mutex,
		   * the caller must hold bufptr->BCB_mutex again.
		   */
		  rv = pthread_mutex_lock (&bufptr->BCB_mutex);
		}

	      /*
	       * If the buffer is associated with a page (i.e., if the buffer
	       * is not used as a working area --malloc--), invalidate the
	       * page on this buffer.
	       * Detach the buffer area or scramble tha area.
	       */
	      if (!VPID_ISNULL (&bufptr->vpid))
		{
		  /* invalidate the page with PGBUF_LATCH_INVALID mode */
		  (void) pgbuf_invalidate_bcb (bufptr);
		  /*
		   * Since above function releases BCB_mutex after flushing,
		   * the caller must hold bufptr->BCB_mutex again.
		   */
		  rv = pthread_mutex_lock (&bufptr->BCB_mutex);
		}

	      pgbuf_scramble (&bufptr->iopage_buffer->iopage);

	      /*
	       * Note that the buffer is not declared for immediate
	       * replacement.
	       * wait for a while to see if an invalid access is found.
	       */
	    }
	}
      pthread_mutex_unlock (&bufptr->BCB_mutex);
    }
#endif /* CUBRID_DEBUG */
}

/*
 * pgbuf_unfix_all () - Unfixes all the buffers that have been fixed by current
 *                  thread at the time of request termination
 *   return: void
 *
 * Note: At the time of request termination, there must
 *       be no buffers that were fixed by the thread. In current CUBRID
 *       system, however, above situation has occurred. In some later time,
 *       our system must be corrected to prevent above situation from
 *	 occurring.
 */
void
pgbuf_unfix_all (THREAD_ENTRY * thread_p)
{
  int thrd_index;
  PAGE_PTR pgptr;
  PGBUF_HOLDER_ANCHOR *thrd_holder_info;
  PGBUF_HOLDER *holder;
#if defined(NDEBUG)
#else /* NDEBUG */
  PGBUF_BCB *bufptr;
#if defined(CUBRID_DEBUG)
  int consistent;
#endif /* CUBRID_DEBUG */
  const char *latch_mode_str, *zone_str, *consistent_str;
#endif /* NDEBUG */

  thrd_index = THREAD_GET_CURRENT_ENTRY_INDEX (thread_p);

  thrd_holder_info = &(pgbuf_Pool.thrd_holder_info[thrd_index]);

  if (thrd_holder_info->num_hold_cnt > 0)
    {
      /* For each BCB holder entry of thread's holder list */
      holder = thrd_holder_info->thrd_hold_list;
      while (holder != NULL)
	{
	  CAST_BFPTR_TO_PGPTR (pgptr, holder->bufptr);

#if defined(NDEBUG)
	  pgbuf_unfix_and_init (thread_p, pgptr);

	  /* Within the execution of pgbuf_unfix(), the BCB holder entry is
	   * moved from the holder list of BCB to the free holder list of
	   * thread, and the BCB holder entry is removed from the holder
	   * list of the thread.
	   */
	  holder = thrd_holder_info->thrd_hold_list;
#else /* NDEBUG */
	  CAST_PGPTR_TO_BFPTR (bufptr, pgptr);
	  assert (!VPID_ISNULL (&bufptr->vpid));

	  latch_mode_str =
	    (bufptr->latch_mode == PGBUF_NO_LATCH) ? "No Latch" :
	    (bufptr->latch_mode == PGBUF_LATCH_READ) ? "Read" :
	    (bufptr->latch_mode == PGBUF_LATCH_WRITE) ? "Write" :
	    (bufptr->latch_mode == PGBUF_LATCH_FLUSH) ? "Flush" :
	    (bufptr->latch_mode == PGBUF_LATCH_VICTIM) ? "Victim" :
	    (bufptr->latch_mode == PGBUF_LATCH_FLUSH_INVALID) ? "FlushInv" :
	    (bufptr->latch_mode ==
	     PGBUF_LATCH_VICTIM_INVALID) ? "VictimInv" : "Fault";

	  zone_str = ((bufptr->zone == PGBUF_LRU_1_ZONE) ? "LRU_1_Zone" :
		      (bufptr->zone == PGBUF_LRU_2_ZONE) ? "LRU_2_Zone" :
		      (bufptr->zone ==
		       PGBUF_INVALID_ZONE) ? "INVALID_Zone" : "VOID_Zone");

	  /* check if the content of current buffer page is consistent. */
#if defined(CUBRID_DEBUG)
	  consistent = pgbuf_is_consistent (bufptr, 0);
	  consistent_str = ((consistent == PGBUF_CONTENT_GOOD) ? "GOOD" :
			    (consistent == PGBUF_CONTENT_BAD) ? "BAD" :
			    "LIKELY BAD");
#else /* CUBRID_DEBUG */
	  consistent_str = "UNKNOWN";
#endif /* CUBRID_DEBUG */
	  er_log_debug (ARG_FILE_LINE, "pgbuf_unfix_all: WARNING"
			" %4d %5d %6d %4d %9s %1d %1d %1d %11s"
			" %6d|%4d %10s %p %p-%p\n",
			bufptr->ipool, bufptr->vpid.volid,
			bufptr->vpid.pageid, bufptr->fcnt, latch_mode_str,
			bufptr->dirty, bufptr->avoid_victim,
			bufptr->async_flush_request, zone_str,
			bufptr->iopage_buffer->iopage.prv.lsa.pageid,
			bufptr->iopage_buffer->iopage.prv.lsa.offset,
			consistent_str, (void *) bufptr,
			(void *) (&bufptr->iopage_buffer->iopage.page[0]),
			(void *) (&bufptr->iopage_buffer->iopage.
				  page[DB_PAGESIZE - 1]));

	  holder = holder->thrd_link;
#endif /* NDEBUG */
	}

      assert (false);
    }
}

/*
 * pgbuf_invalidate () - Invalidate page in buffer
 *   return: NO_ERROR, or ER_code
 *   pgptr(in): Pointer to page
 *
 * Note: Invalidate the buffer corresponding to page associated with pgptr when
 *       the page has been fixed only once, otherwise, the page is only
 *       unfixed. If the page is invalidated, the page will not be associated
 *       with the buffer any longer and the buffer can be used for the buffer
 *       allocation immediately.
 *
 *       The page invalidation task is executed only for performance
 *       enhancement. This task is irrespective of correctness. That is, If
 *       this task is not performed, there is no problem in the correctness of
 *       the system. When page invalidation task is used, however, following
 *       things must be kept to prevent incorrectness incurred by using page
 *       invalidation task.
 *
 *       1. For temporary pages, page invalidation can be performed at any
 *          time.
 *       2. For regular pages(used to save persistent data such as meta data
 *          and user data), page invalidation must be performed as postpone
 *          operation that is executed after the commit decision of transaction
 *          has been made. The reason will be explained in the
 *          document[TM-2001-04].
 */
#if !defined(NDEBUG)
int
pgbuf_invalidate_debug (THREAD_ENTRY * thread_p, PAGE_PTR pgptr,
			const char *caller_file, int caller_line)
#else /* NDEBUG */
int
pgbuf_invalidate (THREAD_ENTRY * thread_p, PAGE_PTR pgptr)
#endif				/* NDEBUG */
{
  PGBUF_BCB *bufptr;
  VPID temp_vpid;
  int holder_status;
#if defined(SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */

  if (pgbuf_get_check_page_validation (thread_p,
				       PGBUF_DEBUG_PAGE_VALIDATION_ALL))
    {
      if (pgbuf_is_valid_page_ptr (pgptr) == false)
	{
	  return ER_FAILED;
	}
    }

  /* Get the address of the buffer from the page and invalidate buffer */
  CAST_PGPTR_TO_BFPTR (bufptr, pgptr);
  assert (!VPID_ISNULL (&bufptr->vpid));

  MUTEX_LOCK_VIA_BUSY_WAIT (rv, bufptr->BCB_mutex);
  /*
   * This function is called by the caller while it is fixing the page
   * with PGBUF_LATCH_WRITE mode in CUBRID environment. Therefore,
   * the caller must unfix the page and then invalidate the page.
   */
  if (bufptr->fcnt > 1)
    {
      holder_status = pgbuf_unlatch_thrd_holder (thread_p, bufptr);

      /* If the page has been fixed more than one time, just unfix it. */
#if !defined(NDEBUG)
      if (pgbuf_unlatch_bcb_upon_unfix (thread_p, bufptr, holder_status,
					caller_file, caller_line) != NO_ERROR)
#else /* NDEBUG */
      if (pgbuf_unlatch_bcb_upon_unfix (thread_p, bufptr,
					holder_status) != NO_ERROR)
#endif /* NDEBUG */
	{
	  return ER_FAILED;
	}

      return NO_ERROR;
      /* bufptr->BCB_mutex hash been released in above function. */
    }

  /* bufptr->fcnt == 1 */
  /* Currently, bufptr->latch_mode is PGBUF_LATCH_WRITE */
  if (bufptr->dirty == true)
    {
      /*
       * Even in case of invalidating a page image on the buffer,
       * if the page image has been dirtied,
       * the page image should be flushed to disk space.
       * What is the reason ? reference the document.
       */
      if (pgbuf_flush_page_with_wal (thread_p, bufptr) != NO_ERROR)
	{
	  pthread_mutex_unlock (&bufptr->BCB_mutex);

	  return ER_FAILED;
	}
    }

  /* save the pageid of the page temporarily. */
  temp_vpid = bufptr->vpid;

  holder_status = pgbuf_unlatch_thrd_holder (thread_p, bufptr);

#if !defined(NDEBUG)
  if (pgbuf_unlatch_bcb_upon_unfix (thread_p, bufptr, holder_status,
				    caller_file, caller_line) != NO_ERROR)
#else /* NDEBUG */
  if (pgbuf_unlatch_bcb_upon_unfix (thread_p, bufptr,
				    holder_status) != NO_ERROR)
#endif /* NDEBUG */
    {
      return ER_FAILED;
    }
  /* bufptr->BCB_mutex has been released in above function. */

  /* hold BCB_mutex again to invalidate the BCB */
  MUTEX_LOCK_VIA_BUSY_WAIT (rv, bufptr->BCB_mutex);

  /* check if the page should be invalidated. */
  if (VPID_ISNULL (&bufptr->vpid)
      || !VPID_EQ (&temp_vpid, &bufptr->vpid) || bufptr->fcnt > 0
      || bufptr->avoid_victim == true)
    {
      pthread_mutex_unlock (&bufptr->BCB_mutex);
      return NO_ERROR;
    }

#if defined(CUBRID_DEBUG)
  pgbuf_scramble (&bufptr->iopage_buffer->iopage);
#endif /* CUBRID_DEBUG */

  /*
   * Now, invalidation task is performed
   * after holding a page latch with PGBUF_LATCH_INVALID mode.
   */
  if (pgbuf_invalidate_bcb (bufptr) != NO_ERROR)
    {
      return ER_FAILED;
    }

  /* bufptr->BCB_mutex has been released in above function. */
  return NO_ERROR;
}

/*
 * pgbuf_invalidate_temporary_file () -
 *   return:
 *   volid(in):
 *   first_pageid(in):
 *   npages(in):
 *   need_invalidate(in):
 */
void
pgbuf_invalidate_temporary_file (VOLID volid, PAGEID first_pageid,
				 DKNPAGES npages, bool need_invalidate)
{
  PGBUF_BUFFER_HASH *hash_anchor;
  PGBUF_BCB *bufptr;
  PAGE_PTR pgptr;
  VPID vpid;
  int i;
  bool is_last_page = false;

#if 1				/* at here, do not invalidate page buffer - NEED FUTURE WORK */
  need_invalidate = false;
#endif

  vpid.volid = volid;
  for (i = 0; i < npages; i++)
    {
      vpid.pageid = first_pageid + i;

      /* fix page */
      hash_anchor = &pgbuf_Pool.buf_hash_table[PGBUF_HASH_VALUE (&vpid)];
      bufptr = pgbuf_search_hash_chain (hash_anchor, &vpid);
      if (bufptr == NULL)
	{
	  pthread_mutex_unlock (&hash_anchor->hash_mutex);
	  continue;
	}

      /* if this query was executed in asynchronous mode, a page may have
       * positive(1) fcnt(get_list_file_page performs pgbuf_fix()).
       */
      if (bufptr->fcnt > 0)
	{
	  pthread_mutex_unlock (&bufptr->BCB_mutex);
	  continue;
	}

      /* check if page should be invalidated */
      if (VPID_ISNULL (&bufptr->vpid) || !VPID_EQ (&vpid, &bufptr->vpid))
	{
	  pthread_mutex_unlock (&bufptr->BCB_mutex);
	  continue;
	}

      /* check if this page is the last page of the list file */
      CAST_BFPTR_TO_PGPTR (pgptr, bufptr);
      if (qfile_has_next_page (pgptr) == false)
	{
	  is_last_page = true;
	}

      /* Even though pgbuf_invalidate_bcb() will reset dirty and
       * oldest_unflush_lsa field, the function may fail before reset these.
       */
      bufptr->dirty = false;
      LSA_SET_NULL (&bufptr->oldest_unflush_lsa);

#if 0				/* BTS CUBRIDSUS-3627 */
      if (need_invalidate == true)
	{
	  (void) pgbuf_invalidate_bcb (bufptr);
	}
      else
#endif
	{
	  pthread_mutex_unlock (&bufptr->BCB_mutex);
	}

      if (is_last_page)
	{
	  break;
	}
    }
}

/*
 * pgbuf_invalidate_all () - Invalidate all unfixed buffers corresponding to
 *                           the given volume
 *   return: NO_ERROR, or ER_code
 *   volid(in): Permanent Volume Identifier or NULL_VOLID
 *
 * Note: The pages in these buffers are disassociated from the buffers. If a
 *       page was dirty, it is flushed before the buffer is invalidated.
 */
#if !defined(NDEBUG)
int
pgbuf_invalidate_all_debug (THREAD_ENTRY * thread_p, VOLID volid,
			    const char *caller_file, int caller_line)
#else /* NDEBUG */
int
pgbuf_invalidate_all (THREAD_ENTRY * thread_p, VOLID volid)
#endif				/* NDEBUG */
{
  PGBUF_BCB *bufptr;
  VPID temp_vpid;
  int bufid;
#if defined(SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */

  /*
   * While searching all the buffer pages or corresponding buffer pages,
   * the caller flushes each buffer page if it is dirty and
   * invalidates the buffer page if it is not fixed on the buffer.
   */
  for (bufid = 0; bufid < pgbuf_Pool.num_buffers; bufid++)
    {
      bufptr = PGBUF_FIND_BCB_PTR (bufid);
      if (VPID_ISNULL (&bufptr->vpid)
	  || (volid != NULL_VOLID && volid != bufptr->vpid.volid))
	{
	  continue;
	}

      MUTEX_LOCK_VIA_BUSY_WAIT (rv, bufptr->BCB_mutex);
      if (VPID_ISNULL (&bufptr->vpid)
	  || (volid != NULL_VOLID && volid != bufptr->vpid.volid)
	  || bufptr->fcnt > 0)
	{
	  /* PGBUF_LATCH_READ/PGBUF_LATCH_WRITE */
	  pthread_mutex_unlock (&bufptr->BCB_mutex);
	  continue;
	}

      if (bufptr->dirty == true)
	{
	  temp_vpid = bufptr->vpid;
#if !defined(NDEBUG)
	  if (pgbuf_flush_bcb (thread_p, bufptr, true,
			       caller_file, caller_line) != NO_ERROR)
#else /* NDEBUG */
	  if (pgbuf_flush_bcb (thread_p, bufptr, true) != NO_ERROR)
#endif /* NDEBUG */
	    {
	      return ER_FAILED;
	    }

	  /*
	   * Since above function releases bufptr->BCB_mutex,
	   * the caller must hold bufptr->BCB_mutex again to invalidate the BCB.
	   */
	  MUTEX_LOCK_VIA_BUSY_WAIT (rv, bufptr->BCB_mutex);

	  /* check if page invalidation should be performed on the page */
	  if (VPID_ISNULL (&bufptr->vpid)
	      || !VPID_EQ (&temp_vpid, &bufptr->vpid)
	      || (volid != NULL_VOLID && volid != bufptr->vpid.volid)
	      || bufptr->fcnt > 0 || bufptr->avoid_victim == true)
	    {
	      pthread_mutex_unlock (&bufptr->BCB_mutex);
	      continue;
	    }
	}

#if defined(CUBRID_DEBUG)
      pgbuf_scramble (&bufptr->iopage_buffer->iopage);
#endif /* CUBRID_DEBUG */

      /* Now, page invalidation task is performed
         while holding a page latch with PGBUF_LATCH_INVALID mode. */
      (void) pgbuf_invalidate_bcb (bufptr);
      /* bufptr->BCB_mutex has been released in above function. */
    }

  return NO_ERROR;
}

/*
 * pgbuf_flush () - Flush a page out to disk
 *   return: pgptr on success, NULL on failure
 *   pgptr(in): Page pointer
 *   free_page(in): Free the page too ?
 *
 * Note: The page associated with pgptr is written out to disk (ONLY when the
 *       page is dirty) and optionally is freed (See pb_free). The interface
 *       requires the pgptr instead of vpid to avoid hashing.
 *
 *       The page flush task is also executed only for performance enhancement
 *       like page invalidation task. And, this task can be performed at any
 *       time unlike page invalidation task.
 */
#if !defined(NDEBUG)
PAGE_PTR
pgbuf_flush_debug (THREAD_ENTRY * thread_p, PAGE_PTR pgptr, int free_page,
		   const char *caller_file, int caller_line)
#else /* NDEBUG */
PAGE_PTR
pgbuf_flush (THREAD_ENTRY * thread_p, PAGE_PTR pgptr, int free_page)
#endif				/* NDEBUG */
{
  PGBUF_BCB *bufptr;
  int holder_status;
#if defined(SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */

  if (pgbuf_get_check_page_validation (thread_p,
				       PGBUF_DEBUG_PAGE_VALIDATION_ALL))
    {
      if (pgbuf_is_valid_page_ptr (pgptr) == false)
	{
	  return NULL;
	}
    }

  /* Get the address of the buffer from the page. */
  CAST_PGPTR_TO_BFPTR (bufptr, pgptr);
  assert (!VPID_ISNULL (&bufptr->vpid));

  /* the caller is holding a page latch with PGBUF_LATCH_WRITE mode. */
  MUTEX_LOCK_VIA_BUSY_WAIT (rv, bufptr->BCB_mutex);

  if (bufptr->dirty == true)
    {
      if (pgbuf_flush_page_with_wal (thread_p, bufptr) != NO_ERROR)
	{
	  pthread_mutex_unlock (&bufptr->BCB_mutex);

	  return NULL;
	}
    }

  /* the caller is holding bufptr->BCB_mutex. */
  if (free_page == FREE)
    {
      holder_status = pgbuf_unlatch_thrd_holder (thread_p, bufptr);

#if !defined(NDEBUG)
      if (pgbuf_unlatch_bcb_upon_unfix (thread_p, bufptr, holder_status,
					caller_file, caller_line) != NO_ERROR)
#else /* NDEBUG */
      if (pgbuf_unlatch_bcb_upon_unfix (thread_p, bufptr,
					holder_status) != NO_ERROR)
#endif /* NDEBUG */
	{
	  return NULL;
	}
      /* bufptr->BCB_mutex has been released in above function. */
    }
  else
    {
      pthread_mutex_unlock (&bufptr->BCB_mutex);
    }

  return pgptr;
}

/*
 * pgbuf_flush_with_wal () - Flush a page out to disk after following the wal
 *                           rule
 *   return: pgptr on success, NULL on failure
 *   pgptr(in): Page pointer
 *
 * Note: The page associated with pgptr is written out to disk (ONLY when the
 *       page is dirty) Before the page is flushed, the WAL rule of the log
 *       manger is called.
 */
PAGE_PTR
pgbuf_flush_with_wal (THREAD_ENTRY * thread_p, PAGE_PTR pgptr)
{
  PGBUF_BCB *bufptr;
#if defined(SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */

  if (pgbuf_get_check_page_validation (thread_p,
				       PGBUF_DEBUG_PAGE_VALIDATION_ALL))
    {
      if (pgbuf_is_valid_page_ptr (pgptr) == false)
	{
	  return NULL;
	}
    }

  /* NOTE: the page is fixed */
  /* Get the address of the buffer from the page. */
  CAST_PGPTR_TO_BFPTR (bufptr, pgptr);
  assert (!VPID_ISNULL (&bufptr->vpid));

  /* In CUBRID, the caller is holding WRITE page latch */
  MUTEX_LOCK_VIA_BUSY_WAIT (rv, bufptr->BCB_mutex);

  /* Flush the page only when it is dirty */
  if (bufptr->dirty == true)
    {
      if (pgbuf_flush_page_with_wal (thread_p, bufptr) != NO_ERROR)
	{
	  pthread_mutex_unlock (&bufptr->BCB_mutex);

	  return NULL;
	}
    }
  pthread_mutex_unlock (&bufptr->BCB_mutex);

  return pgptr;
}

#if !defined(NDEBUG)
static int
pgbuf_flush_all_helper (THREAD_ENTRY * thread_p, VOLID volid,
			bool is_unfixed_only, bool is_set_lsa_as_null,
			const char *caller_file, int caller_line)
#else /* NDEBUG */
static int
pgbuf_flush_all_helper (THREAD_ENTRY * thread_p, VOLID volid,
			bool is_unfixed_only, bool is_set_lsa_as_null)
#endif				/* NDEBUG */
{
  PGBUF_BCB *bufptr;
  int i;
#if defined(SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */

  /* Flush all unfixed dirty buffers */
  for (i = 0; i < pgbuf_Pool.num_buffers; i++)
    {
      bufptr = PGBUF_FIND_BCB_PTR (i);
      if ((bufptr->dirty == false)
	  || (volid != NULL_VOLID && volid != bufptr->vpid.volid))
	{
	  continue;
	}

      MUTEX_LOCK_VIA_BUSY_WAIT (rv, bufptr->BCB_mutex);
      /* flush condition check */
      if ((bufptr->dirty == false)
	  || (is_unfixed_only && bufptr->fcnt > 0)
	  || (volid != NULL_VOLID && volid != bufptr->vpid.volid))
	{
	  pthread_mutex_unlock (&bufptr->BCB_mutex);
	  continue;
	}

      if (is_set_lsa_as_null)
	{
	  /* set PageLSA as NULL value */
	  LSA_SET_INIT_NONTEMP (&bufptr->iopage_buffer->iopage.prv.lsa);
	}

      /* the caller is holding bufptr->BCB_mutex */
      /* flush the page with PGBUF_LATCH_FLUSH mode */
#if !defined(NDEBUG)
      if (pgbuf_flush_bcb (thread_p, bufptr, true, caller_file, caller_line)
	  != NO_ERROR)
#else /* NDEBUG */
      if (pgbuf_flush_bcb (thread_p, bufptr, true) != NO_ERROR)
#endif /* NDEBUG */
	{
	  return ER_FAILED;
	}
      /* Above function released BCB_mutex regardless of its return value. */
    }

  return NO_ERROR;
}

/*
 * pgbuf_flush_all () - Flush all dirty pages out to disk
 *   return: NO_ERROR, or ER_code
 *   volid(in): Permanent Volume Identifier or NULL_VOLID
 *
 * Note: Every dirty page of the specified volume is written out to disk.
 *       If volid is equal to NULL_VOLID, all dirty pages of all volumes are
 *       written out to disk. Its use is recommended by only the log and
 *       recovery manager.
 */
#if !defined(NDEBUG)
int
pgbuf_flush_all_debug (THREAD_ENTRY * thread_p, VOLID volid,
		       const char *caller_file, int caller_line)
{
  return pgbuf_flush_all_helper (thread_p, volid, false, false,
				 caller_file, caller_line);
}
#else /* NDEBUG */
int
pgbuf_flush_all (THREAD_ENTRY * thread_p, VOLID volid)
{
  return pgbuf_flush_all_helper (thread_p, volid, false, false);
}
#endif /* NDEBUG */

/*
 * pgbuf_flush_all_unfixed () - Flush all unfixed dirty pages out to disk
 *   return: NO_ERROR, or ER_code
 *   volid(in): Permanent Volume Identifier or NULL_VOLID
 *
 * Note: Every dirty page of the specified volume which is unfixed is written
 *       out to disk. If volid is equal to NULL_VOLID, all dirty pages of all
 *       volumes that are unfixed are written out to disk.
 *       Its use is recommended by only the log and recovery manager.
 */
#if !defined(NDEBUG)
int
pgbuf_flush_all_unfixed_debug (THREAD_ENTRY * thread_p, VOLID volid,
			       const char *caller_file, int caller_line)
{
  return pgbuf_flush_all_helper (thread_p, volid, true, false,
				 caller_file, caller_line);
}
#else /* NDEBUG */
int
pgbuf_flush_all_unfixed (THREAD_ENTRY * thread_p, VOLID volid)
{
  return pgbuf_flush_all_helper (thread_p, volid, true, false);
}
#endif /* NDEBUG */

/*
 * pgbuf_flush_all_unfixed_and_set_lsa_as_null () - Set lsa to null and flush
 *                                     all unfixed dirty pages out to disk
 *   return: NO_ERROR, or ER_code
 *   volid(in): Permanent Volume Identifier or NULL_VOLID
 *
 * Note: Every dirty page of the specified volume which is unfixed is written
 *       out after its lsa is initialized to a null lsa. If volid is equal to
 *       NULL_VOLID, all dirty pages of all volumes that are unfixed are
 *       flushed to disk after its lsa is initialized to null.
 *       Its use is recommended by only the log and recovery manager.
 */
#if !defined(NDEBUG)
int
pgbuf_flush_all_unfixed_and_set_lsa_as_null_debug (THREAD_ENTRY * thread_p,
						   VOLID volid,
						   const char *caller_file,
						   int caller_line)
{
  return pgbuf_flush_all_helper (thread_p, volid, true, true,
				 caller_file, caller_line);
}
#else /* NDEBUG */
int
pgbuf_flush_all_unfixed_and_set_lsa_as_null (THREAD_ENTRY * thread_p,
					     VOLID volid)
{
  return pgbuf_flush_all_helper (thread_p, volid, true, true);
}
#endif /* NDEBUG */

/*
 * pgbuf_compare_victim_list () - Compare the vpid of victim candidate list
 *   return: p1 - p2
 *   p1(in): victim candidate list 1
 *   p2(in): victim candidate list 2
 */
static int
pgbuf_compare_victim_list (const void *p1, const void *p2)
{
  PGBUF_VICTIM_CANDIDATE_LIST *node1, *node2;
  int diff;

  node1 = (PGBUF_VICTIM_CANDIDATE_LIST *) p1;
  node2 = (PGBUF_VICTIM_CANDIDATE_LIST *) p2;

  if (node1 == node2)
    {
      return 0;
    }

  diff = node1->vpid.volid - node2->vpid.volid;
  if (diff != 0)
    {
      return diff;
    }
  else
    {
      return (node1->vpid.pageid - node2->vpid.pageid);
    }
}

/*
 * pgbuf_get_victim_candidates_from_ain () - get victim candidates from the
 *					     Ain list
 * return : error code or NO_ERROR
 * check_count (in) : maximum number of elements to visit in the queue
 *
 */
static int
pgbuf_get_victim_candidates_from_ain (int check_count)
{
#if defined (SERVER_MODE)
  int rv;
#endif
  PGBUF_BCB *bufptr;
  int victim_count;
  PGBUF_VICTIM_CANDIDATE_LIST *victim_list;

  victim_list = pgbuf_Pool.victim_cand_list;
  victim_count = 0;

  MUTEX_LOCK_VIA_BUSY_WAIT (rv, pgbuf_Pool.buf_AIN_list.Ain_mutex);
  bufptr = pgbuf_Pool.buf_AIN_list.Ain_bottom;

  while ((bufptr != NULL) && (check_count > 0))
    {
      if ((bufptr->fcnt == 0) && (bufptr->dirty == true)
	  && (bufptr->latch_mode != PGBUF_LATCH_FLUSH))
	{
	  /* save victim candidate information temporarily. */
	  victim_list[victim_count].bufptr = bufptr;
	  VPID_COPY (&victim_list[victim_count].vpid, &bufptr->vpid);
	  LSA_COPY (&victim_list[victim_count].recLSA,
		    &bufptr->oldest_unflush_lsa);
	  victim_count++;
	}

      bufptr = bufptr->prev_BCB;
      check_count--;
    }
  pthread_mutex_unlock (&pgbuf_Pool.buf_AIN_list.Ain_mutex);

  return victim_count;
}

/*
 * pgbuf_get_victim_candidates_from_lru () - get victim candidates from LRU
 *					     list
 * return : number of victims found
 * check_count (in)  : number of items to verify before abandoning search
 * victim_count (in) : number of victims already selected
 * flush_ratio (in)  : flush ratio
 */
static int
pgbuf_get_victim_candidates_from_lru (int check_count, int victim_count)
{
#if defined(SERVER_MODE)
  int rv;
#endif
  PGBUF_VICTIM_CANDIDATE_LIST *victim_cand_list = NULL;
  int lru_idx, start_lru_idx, victim_cand_count, i;
  PGBUF_BCB *bufptr;

  /* init */
  lru_idx = ((pgbuf_Pool.last_flushed_LRU_list_idx + 1)
	     % pgbuf_Pool.num_LRU_list);
  start_lru_idx = lru_idx;
  victim_cand_count = victim_count;
  victim_cand_list = pgbuf_Pool.victim_cand_list;

  do
    {
      i = check_count;
      MUTEX_LOCK_VIA_BUSY_WAIT (rv,
				pgbuf_Pool.buf_LRU_list[lru_idx].LRU_mutex);
      bufptr = pgbuf_Pool.buf_LRU_list[lru_idx].LRU_bottom;

      while ((bufptr != NULL) && (bufptr->zone != PGBUF_LRU_1_ZONE)
	     && (i > 0))
	{
	  if ((bufptr->fcnt == 0) && (bufptr->dirty == true)
	      && (bufptr->latch_mode != PGBUF_LATCH_FLUSH))
	    {
	      /* save victim candidate information temporarily. */
	      victim_cand_list[victim_cand_count].bufptr = bufptr;
	      victim_cand_list[victim_cand_count].vpid = bufptr->vpid;
	      LSA_COPY (&victim_cand_list[victim_cand_count].recLSA,
			&bufptr->oldest_unflush_lsa);
	      victim_cand_count++;
	    }

	  bufptr = bufptr->prev_BCB;
	  i--;
	}
      pthread_mutex_unlock (&pgbuf_Pool.buf_LRU_list[lru_idx].LRU_mutex);

      /* Note that we don't hold the mutex, however it will not be an issue.
       * Also note that we are updating the last_flushed_LRU_list_idx
       * whether the list has flushed or not.
       */
      pgbuf_Pool.last_flushed_LRU_list_idx = lru_idx;

      lru_idx = (lru_idx + 1) % pgbuf_Pool.num_LRU_list;
    }
  while (lru_idx != start_lru_idx);	/* check if we've visited all of the lists */

  return victim_cand_count - victim_count;
}

/*
 * pgbuf_flush_victim_candidate () - Flush victim candidates
 *   return: NO_ERROR, or ER_code
 *
 * Note: This function flushes at most VictimCleanCount buffers that might
 *       become victim candidates in the near future.
 */
#if !defined(NDEBUG)
int
pgbuf_flush_victim_candidate_debug (THREAD_ENTRY * thread_p,
				    float flush_ratio,
				    const char *caller_file, int caller_line)
#else /* NDEBUG */
int
pgbuf_flush_victim_candidate (THREAD_ENTRY * thread_p, float flush_ratio)
#endif				/* NDEBUG */
{
  PGBUF_BCB *bufptr;
  PGBUF_VICTIM_CANDIDATE_LIST *victim_cand_list;
  int i, victim_count, victim_count_ain, victim_count_lru;
  int check_count_one_lru, check_count_ain, check_count_lru;
  int cfg_check_cnt, lru_min_check_cnt, ain_min_check_cnt;
  int total_flushed_count;
  int cnt_in_ain;
  int lru_idx, start_lru_idx;
  int error;
  int num_tries;
  float lru_miss_rate, ain_miss_rate;
  float lru_dynamic_flush_adj = 1.0f, ain_dynamic_flush_adj = 1.0f;
  float lru_to_ain_req_ratio;
  int lru_victim_req_cnt, ain_victim_req_cnt, fix_req_cnt;
#if defined(SERVER_MODE)
  int rv;
  static THREAD_ENTRY *page_flush_thread = NULL;
#endif /* SERVER_MODE */

  mnt_pb_victims (thread_p);

  er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE,
	  ER_LOG_FLUSH_VICTIM_STARTED, 0);
  er_log_debug (ARG_FILE_LINE, "start flush victim candidates\n");

#if !defined(NDEBUG) && defined(SERVER_MODE)
  if (thread_is_page_flush_thread_available ())
    {
      if (page_flush_thread == NULL)
	{
	  page_flush_thread = thread_p;
	}

      /* This should be fixed */
      assert (page_flush_thread == thread_p);
    }
#endif

  victim_cand_list = pgbuf_Pool.victim_cand_list;

  lru_idx = ((pgbuf_Pool.last_flushed_LRU_list_idx + 1)
	     % pgbuf_Pool.num_LRU_list);
  start_lru_idx = lru_idx;

  victim_count = 0;
  victim_count_ain = 0;
  victim_count_lru = 0;
  total_flushed_count = 0;
  check_count_ain = 0;
  check_count_lru = 0;
  check_count_one_lru = 0;

  lru_victim_req_cnt = pgbuf_Pool.lru_victim_req_cnt;
  ain_victim_req_cnt = pgbuf_Pool.ain_victim_req_cnt;
  fix_req_cnt = pgbuf_Pool.fix_req_cnt;
  ATOMIC_TAS_32 (&pgbuf_Pool.lru_victim_req_cnt, 0);
  ATOMIC_TAS_32 (&pgbuf_Pool.ain_victim_req_cnt, 0);
  ATOMIC_TAS_32 (&pgbuf_Pool.fix_req_cnt, 0);

  if (fix_req_cnt > lru_victim_req_cnt && fix_req_cnt > ain_victim_req_cnt)
    {
      lru_miss_rate = (float) lru_victim_req_cnt / (float) fix_req_cnt;
      ain_miss_rate = (float) ain_victim_req_cnt / (float) fix_req_cnt;
    }
  else
    {
      /* overflow of fix counter, we ignore miss rate */
      lru_miss_rate = 0;
      ain_miss_rate = 0;
    }

  cfg_check_cnt = (int) (pgbuf_Pool.num_buffers * flush_ratio);
  cnt_in_ain = pgbuf_Pool.buf_AIN_list.ain_count;

  if (lru_victim_req_cnt > 0 || ain_victim_req_cnt > 0)
    {
      lru_to_ain_req_ratio =
	((float) lru_victim_req_cnt
	 / (float) (lru_victim_req_cnt + ain_victim_req_cnt));
    }
  else
    {
      /* This may happen when a previous run of flush thread occurred between
       * moment of incrementing LRU or AIN victim request counters and
       * the call to wakeup the flush thread;
       * We don't know which list invoked the flush thread, so we flush
       * according to current AIN count.
       */
      lru_to_ain_req_ratio =
	(float) (cfg_check_cnt - (cnt_in_ain
				  - pgbuf_Pool.buf_AIN_list.max_count))
	/ (2 * cfg_check_cnt);

      lru_to_ain_req_ratio = MIN (lru_to_ain_req_ratio, 1.0f);
      lru_to_ain_req_ratio = MAX (lru_to_ain_req_ratio, 0);
    }

  /* Victims will only be flushed, not decached. The page replacement
   * algorithm invokes this function when no victims are found;
   * At page request, we measure the requests for victims from LRU and AIN;
   * we split the flush amount between the two queues according to the ratio
   * between those counters.
   * We also apply a flush boost (increase of number of pages to be flushed
   * over the configured parameter), when miss rate is high.
   * During flush phase, we can abort the flushing if detecting a change in
   * victimization pattern (LRU vs AIN).
   */

  lru_dynamic_flush_adj = MAX (1.0f,
			       1 + (PGBUF_FLUSH_VICTIM_BOOST_MULT - 1)
			       * lru_miss_rate);
  lru_dynamic_flush_adj = MIN (PGBUF_FLUSH_VICTIM_BOOST_MULT,
			       lru_dynamic_flush_adj);

  ain_dynamic_flush_adj = MAX (1.0f,
			       1 + (PGBUF_FLUSH_VICTIM_BOOST_MULT - 1)
			       * ain_miss_rate);
  ain_dynamic_flush_adj = MIN (PGBUF_FLUSH_VICTIM_BOOST_MULT,
			       ain_dynamic_flush_adj);

  if (PGBUF_IS_2Q_ENABLED)
    {
      lru_min_check_cnt = (int) (cfg_check_cnt * lru_to_ain_req_ratio);
      ain_min_check_cnt = cfg_check_cnt - lru_min_check_cnt;

      check_count_ain = (int) (ain_min_check_cnt * ain_dynamic_flush_adj);
      check_count_ain = MIN (check_count_ain,
			     pgbuf_Pool.buf_AIN_list.max_count / 2);

      check_count_lru = (int) (lru_min_check_cnt * lru_dynamic_flush_adj);
    }
  else
    {
      check_count_lru = (int) (cfg_check_cnt * lru_dynamic_flush_adj);
      ain_min_check_cnt = 0;
    }

  if (check_count_lru > 0)
    {
      check_count_one_lru = (check_count_lru + pgbuf_Pool.num_LRU_list)
	/ pgbuf_Pool.num_LRU_list;
    }

  if (check_count_ain > 0)
    {
      victim_count_ain =
	pgbuf_get_victim_candidates_from_ain (check_count_ain);
    }

  if (check_count_one_lru > 0)
    {
      victim_count_lru =
	pgbuf_get_victim_candidates_from_lru (check_count_one_lru,
					      victim_count_ain);
    }
  victim_count = victim_count_lru + victim_count_ain;
  if (victim_count == 0)
    {
      /* We didn't find any victims */
      goto end;
    }

  qsort ((void *) victim_cand_list, victim_count,
	 sizeof (PGBUF_VICTIM_CANDIDATE_LIST), pgbuf_compare_victim_list);

  num_tries = 1;
  while (total_flushed_count <= 0 && num_tries <= 2)
    {
      /* for each victim candidate, do flush task */
      for (i = 0; i < victim_count; i++)
	{
	  bufptr = victim_cand_list[i].bufptr;

	  MUTEX_LOCK_VIA_BUSY_WAIT (rv, bufptr->BCB_mutex);
	  /* flush condition check */
	  if (!VPID_EQ (&bufptr->vpid, &victim_cand_list[i].vpid)
	      || bufptr->dirty == false
	      || (bufptr->zone != PGBUF_LRU_2_ZONE
		  && bufptr->zone != PGBUF_AIN_ZONE)
	      || bufptr->latch_mode != PGBUF_NO_LATCH
	      || !(LSA_EQ (&bufptr->oldest_unflush_lsa,
			   &victim_cand_list[i].recLSA))
	      || bufptr->avoid_victim == true)
	    {
	      pthread_mutex_unlock (&bufptr->BCB_mutex);
	      continue;
	    }

	  /* In the first try, we will flush pages which do not need WAL. */
	  if (num_tries == 1
	      && logpb_need_wal (&(bufptr->iopage_buffer->iopage.prv.lsa)))
	    {
	      pthread_mutex_unlock (&bufptr->BCB_mutex);
	      continue;
	    }

	  error = pgbuf_flush_page_with_wal (thread_p, bufptr);
	  pthread_mutex_unlock (&bufptr->BCB_mutex);

	  if (error != NO_ERROR)
	    {
	      er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE,
		      ER_LOG_FLUSH_VICTIM_FINISHED, 1, total_flushed_count);
	      return ER_FAILED;
	    }

	  total_flushed_count++;

	  if (total_flushed_count > cfg_check_cnt
	      && ((pgbuf_Pool.lru_victim_req_cnt
		   > pgbuf_Pool.ain_victim_req_cnt
		   && check_count_lru < check_count_ain)
		  || (pgbuf_Pool.lru_victim_req_cnt
		      < pgbuf_Pool.ain_victim_req_cnt
		      && check_count_lru > check_count_ain)))
	    {
	      /* the victimization pattern has changed and we flushed enough
	       * pages;
	       * after we abort this, the flush thread will retry this
	       * function with new parameters */
	      goto end;
	    }
	}

      num_tries++;
    }

end:
  er_log_debug (ARG_FILE_LINE, "pgbuf_flush_victim_candidate: "
		"flush %d pages from (%d) to (%d) list. "
		"Found AIN:%d/%d/%d, Found LRU:%d/%d",
		total_flushed_count, start_lru_idx,
		pgbuf_Pool.last_flushed_LRU_list_idx,
		victim_count_ain, check_count_ain, cnt_in_ain,
		victim_count_lru, check_count_lru);

  er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE,
	  ER_LOG_FLUSH_VICTIM_FINISHED, 1, total_flushed_count);

  return NO_ERROR;
}

/*
 * pgbuf_flush_checkpoint () - Flush any unfixed dirty page whose lsa
 *                                      is smaller than the last checkpoint lsa
 *   return:error code or NO_ERROR
 *   flush_upto_lsa(in):
 *   prev_chkpt_redo_lsa(in): Redo_LSA of previous checkpoint
 *   smallest_lsa(out): Smallest LSA of a dirty buffer in buffer pool
 *   flushed_page_cnt(out): The number of flushed pages
 *
 * Note: The function flushes and dirty unfixed page whose LSA is smaller that
 *       the last_chkpt_lsa, it returns the smallest_lsa from the remaining
 *       dirty buffers which were not flushed.
 *       This function is used by the log and recovery manager when a
 *       checkpoint is issued.
 */
#if !defined(NDEBUG)
int
pgbuf_flush_checkpoint_debug (THREAD_ENTRY * thread_p,
			      const LOG_LSA * flush_upto_lsa,
			      const LOG_LSA * prev_chkpt_redo_lsa,
			      LOG_LSA * smallest_lsa, int *flushed_page_cnt,
			      const char *caller_file, int caller_line)
#else /* NDEBUG */
int
pgbuf_flush_checkpoint (THREAD_ENTRY * thread_p,
			const LOG_LSA * flush_upto_lsa,
			const LOG_LSA * prev_chkpt_redo_lsa,
			LOG_LSA * smallest_lsa, int *flushed_page_cnt)
#endif				/* NDEBUG */
{
  PGBUF_BCB *bufptr;
  int bufid;
  bool done_flush;
  PAGE_PTR pgptr;
  VPID vpid;
  int flushed_page_cnt_local = 0;
#if defined(SERVER_MODE)
  int sleep_msecs;
  int rv;

  sleep_msecs = prm_get_integer_value (PRM_ID_LOG_CHECKPOINT_SLEEP_MSECS);
#endif /* SERVER_MODE */

  if (flushed_page_cnt != NULL)
    {
      *flushed_page_cnt = -1;
    }

  /* Things must be truly flushed up to this lsa */
  logpb_flush_log_for_wal (thread_p, flush_upto_lsa);
  LSA_SET_NULL (smallest_lsa);

  /* Now, flush all unfixed dirty buffers */
  for (bufid = 0; bufid < pgbuf_Pool.num_buffers; bufid++)
    {
      bufptr = PGBUF_FIND_BCB_PTR (bufid);
      MUTEX_LOCK_VIA_BUSY_WAIT (rv, bufptr->BCB_mutex);

      /* flush condition check */
      if (bufptr->dirty == false
	  || (!LSA_ISNULL (&bufptr->oldest_unflush_lsa)
	      && LSA_GT (&bufptr->oldest_unflush_lsa, flush_upto_lsa)))
	{
	  pthread_mutex_unlock (&bufptr->BCB_mutex);
	  continue;
	}

      if (!LSA_ISNULL (&bufptr->oldest_unflush_lsa)
	  && prev_chkpt_redo_lsa != NULL && !LSA_ISNULL (prev_chkpt_redo_lsa))
	{
	  if (LSA_LT (&bufptr->oldest_unflush_lsa, prev_chkpt_redo_lsa))
	    {
	      er_stack_push ();
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_LOG_CHECKPOINT_SKIP_INVALID_PAGE, 6,
		      bufptr->vpid.pageid,
		      fileio_get_volume_label (bufptr->vpid.volid, PEEK),
		      bufptr->oldest_unflush_lsa.pageid,
		      bufptr->oldest_unflush_lsa.offset,
		      prev_chkpt_redo_lsa->pageid,
		      prev_chkpt_redo_lsa->offset);
	      er_stack_pop ();

	      assert (false);
	    }
	}

      /* flush when buffer is not fixed or was fixed by reader */
      done_flush = false;
      if ((LSA_ISNULL (&bufptr->oldest_unflush_lsa)
	   || LSA_LE (&bufptr->oldest_unflush_lsa, flush_upto_lsa))
	  && bufptr->avoid_victim == false
	  && (bufptr->latch_mode == PGBUF_NO_LATCH
	      || bufptr->latch_mode == PGBUF_LATCH_READ
	      || bufptr->latch_mode == PGBUF_LATCH_FLUSH))
	{
	  if (pgbuf_flush_page_with_wal (thread_p, bufptr) == NO_ERROR)
	    {
	      flushed_page_cnt_local++;
	      done_flush = true;
	    }
	}

      if (done_flush == true)
	{
	  pthread_mutex_unlock (&bufptr->BCB_mutex);

#if defined(SERVER_MODE)
	  /* Checkpoint Thread is writing data pages slowly to avoid IO burst */
	  if (sleep_msecs > 0)
	    {
	      thread_sleep (sleep_msecs);
	    }
#endif
	}
      else
	{
	  if (LSA_ISNULL (&bufptr->oldest_unflush_lsa))
	    {
	      /* this page skipped logging.(log_skip_logging()) */
	      pthread_mutex_unlock (&bufptr->BCB_mutex);
	    }
	  else if (prev_chkpt_redo_lsa != NULL
		   && !LSA_ISNULL (prev_chkpt_redo_lsa)
		   && LSA_LT (&bufptr->oldest_unflush_lsa,
			      prev_chkpt_redo_lsa))
	    {
	      /* invalid page */
	      pthread_mutex_unlock (&bufptr->BCB_mutex);

	      assert (false);
	    }
	  else
	    {
	      /*
	       * bufptr->avoid_victim will be released
	       * in pgbuf_flush_with_wal() function.
	       */
	      bufptr->avoid_victim = true;
	      vpid = bufptr->vpid;
	      pthread_mutex_unlock (&bufptr->BCB_mutex);

	      pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ,
				 PGBUF_UNCONDITIONAL_LATCH);
	      if (pgptr == NULL
		  || pgbuf_flush_with_wal (thread_p, pgptr) == NULL)
		{
#if !defined(NDEBUG)
		  if (pgptr != NULL)
		    {
		      PGBUF_BCB *pgptr_bufptr;

		      CAST_PGPTR_TO_BFPTR (pgptr_bufptr, pgptr);
		      assert (pgptr_bufptr == bufptr);
		    }
#endif
		  MUTEX_LOCK_VIA_BUSY_WAIT (rv, bufptr->BCB_mutex);

		  /* get the smallest oldest_unflush_lsa */
		  if (LSA_ISNULL (smallest_lsa)
		      || LSA_LT (&bufptr->oldest_unflush_lsa, smallest_lsa))
		    {
		      LSA_COPY (smallest_lsa, &bufptr->oldest_unflush_lsa);
		    }

		  if (pgptr == NULL)
		    {
		      bufptr->avoid_victim = false;
		    }

		  pthread_mutex_unlock (&bufptr->BCB_mutex);
		}
	      else		/* If the result of pgbuf_flush_with_wal() is success, then */
		{
		  flushed_page_cnt_local++;
		}

	      if (pgptr != NULL)
		{
		  pgbuf_unfix_and_init (thread_p, pgptr);
		}
	    }
	}

#if defined(SERVER_MODE)
      if (thread_p->shutdown == true)
	{
	  return ER_FAILED;
	}
#endif /* SERVER_MODE */
    }

  if (flushed_page_cnt != NULL)
    {
      *flushed_page_cnt = flushed_page_cnt_local;
    }

  return NO_ERROR;
}

/*
 * pgbuf_copy_to_area () - Copy a portion of a page to the given area
 *   return: area or NULL
 *   vpid(in): Complete Page identifier
 *   start_offset(in): Start offset of interested content in page
 *   length(in): Length of the content of page to copy
 *   area(in): Area where to copy the needed content of the page
 *   do_fetch(in): Do we want to cache the page in the buffer pool when it is
 *                 not already cached?
 *
 * Note: If the page is not in the page buffer pool, it is only buffered when
 *       the value of "do_fetch" is false.
 *
 *       WARNING:
 *       The user should be very careful on deciding wheater or not to allow
 *       buffering of pages. If the page is going to be used in the short
 *       future, it is better to allow buffering the page to avoid extra I/O.
 *       It is better to avoid I/Os than to avoid memcpys.
 */
void *
pgbuf_copy_to_area (THREAD_ENTRY * thread_p, const VPID * vpid,
		    int start_offset, int length, void *area, bool do_fetch)
{
  PGBUF_BUFFER_HASH *hash_anchor;
  PGBUF_BCB *bufptr;
  PAGE_PTR pgptr;

  if (logtb_is_interrupted (thread_p, true, &pgbuf_Pool.check_for_interrupts)
      == true)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INTERRUPTED, 0);
      return NULL;
    }

#if defined(CUBRID_DEBUG)
  if (start_offset < 0 || (start_offset + length) > DB_PAGESIZE)
    {
      er_log_debug (ARG_FILE_LINE,
		    "pgbuf_copy_to_area: SYSTEM ERROR.. Trying to copy"
		    " from beyond page boundary limits. Start_offset = %d,"
		    " length = %d\n", start_offset, length);
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return NULL;
    }
#endif /* CUBRID_DEBUG */

  /* Is this a resident page ? */
  hash_anchor = &(pgbuf_Pool.buf_hash_table[PGBUF_HASH_VALUE (vpid)]);
  bufptr = pgbuf_search_hash_chain (hash_anchor, vpid);

  if (bufptr == NULL)
    {
      /* the caller is holding only hash_anchor->hash_mutex. */
      /* release hash mutex */
      pthread_mutex_unlock (&hash_anchor->hash_mutex);

      if (er_errid () == ER_CSS_PTHREAD_MUTEX_TRYLOCK)
	{
	  return NULL;
	}

      /* The page is not on the buffer pool. Do we want to cache the page ? */
      if (do_fetch == true)
	{
	  pgptr = pgbuf_fix (thread_p, vpid, OLD_PAGE, PGBUF_LATCH_READ,
			     PGBUF_UNCONDITIONAL_LATCH);
	  if (pgptr != NULL)
	    {
	      (void) pgbuf_check_page_ptype (thread_p, pgptr, PAGE_AREA);

	      memcpy (area, (char *) pgptr + start_offset, length);
	      pgbuf_unfix_and_init (thread_p, pgptr);
	    }
	  else
	    {
	      area = NULL;
	    }
	}
#if defined(ENABLE_UNUSED_FUNCTION)
      else
	{
	  /*
	   * Do not cache the page in the page buffer pool.
	   * Read the needed portion of the page directly from disk
	   */
	  if (pgbuf_get_check_page_validation (thread_p,
					       PGBUF_DEBUG_PAGE_VALIDATION_ALL))
	    {
	      if (pgbuf_is_valid_page (thread_p, vpid, false
				       NULL, NULL) != DISK_VALID)
		{
		  return NULL;
		}
	    }

	  /* Record number of reads in statistics */
	  mnt_pb_ioreads (thread_p);

	  if (fileio_read_user_area (thread_p,
				     fileio_get_volume_descriptor
				     (vpid->volid), vpid->pageid,
				     start_offset, length, area) == NULL)
	    {
	      area = NULL;
	    }
	}
#endif
    }
  else
    {
      /* the caller is holding only bufptr->BCB_mutex. */
      CAST_BFPTR_TO_PGPTR (pgptr, bufptr);

      (void) pgbuf_check_page_ptype (thread_p, pgptr, PAGE_AREA);

      memcpy (area, (char *) pgptr + start_offset, length);

      mnt_sort_data_pages (thread_p);

      /* release BCB_mutex */
      pthread_mutex_unlock (&bufptr->BCB_mutex);
    }

  return area;
}

/*
 * pgbuf_copy_from_area () - Copy area to a portion of given page
 *   return: area or NULL
 *   vpid(in): Complete Page identifier
 *   start_offset(in): Start offset of interested content in page
 *   length(in): Length of the content of page to copy
 *   area(in): Area where to copy the needed content of the page
 *   do_fetch(in): Do we want to cache the page in the buffer pool when it is
 *                 not already cached?
 *
 * Note: Copy the content of the given area to the page starting at the given
 *       offset. If the page is not in the page buffer pool, it is only
 *       buffered when the value of "do_fetch" is not false.
 *
 *       WARNING:
 *       The user should be very careful on deciding wheater or not to allow
 *       buffering of pages. If the page is going to be used in the short
 *       future, it is better to allow buffering the page to avoid extra I/O.
 *       If you do not buffer the page, not header recovery information is
 *       copied along with the write of the page. In this case, the page may
 *       not be able to be recovered.
 *       DO NOT USE THIS FEATURE IF YOU LOGGED ANYTHING RELATED TO THIS PAGE.
 */
void *
pgbuf_copy_from_area (THREAD_ENTRY * thread_p, const VPID * vpid,
		      int start_offset, int length, void *area, bool do_fetch)
{
  PGBUF_BUFFER_HASH *hash_anchor;
  PGBUF_BCB *bufptr;
  PAGE_PTR pgptr;
  LOG_DATA_ADDR addr;
#if defined(ENABLE_UNUSED_FUNCTION)
  int vol_fd;
#endif

  assert (start_offset >= 0 && (start_offset + length) <= DB_PAGESIZE);

  /* Is this a resident page ? */
  hash_anchor = &(pgbuf_Pool.buf_hash_table[PGBUF_HASH_VALUE (vpid)]);
  bufptr = pgbuf_search_hash_chain (hash_anchor, vpid);

  if (bufptr == NULL)
    {
      /* the caller is holding only hash_anchor->hash_mutex. */

      pthread_mutex_unlock (&hash_anchor->hash_mutex);

      if (er_errid () == ER_CSS_PTHREAD_MUTEX_TRYLOCK)
	{
	  return NULL;
	}

#if defined(ENABLE_UNUSED_FUNCTION)
      if (do_fetch == false)
	{
	  /* Do not cache the page in the page buffer pool.
	   * Write the desired portion of the page directly to disk
	   */
	  if (pgbuf_get_check_page_validation (thread_p,
					       PGBUF_DEBUG_PAGE_VALIDATION_ALL))
	    {
	      if (pgbuf_is_valid_page (thread_p, vpid, false
				       NULL, NULL) != DISK_VALID)
		{
		  return NULL;
		}
	    }

	  /* Record number of reads in statistics */
	  mnt_pb_iowrites (thread_p, 1);

	  vol_fd = fileio_get_volume_descriptor (vpid->volid);
	  if (fileio_write_user_area (thread_p, vol_fd, vpid->pageid,
				      start_offset, length, area) == NULL)
	    {
	      area = NULL;
	    }

	  return area;
	}
#endif
    }
  else
    {
      /* the caller is holding only bufptr->BCB_mutex. */
      pthread_mutex_unlock (&bufptr->BCB_mutex);
    }

  pgptr = pgbuf_fix (thread_p, vpid, NEW_PAGE, PGBUF_LATCH_WRITE,
		     PGBUF_UNCONDITIONAL_LATCH);
  if (pgptr != NULL)
    {
      (void) pgbuf_set_page_ptype (thread_p, pgptr, PAGE_AREA);

      memcpy ((char *) pgptr + start_offset, area, length);
      /* Inform log manager that there is no need to log this page */
      addr.vfid = NULL;
      addr.pgptr = pgptr;
      addr.offset = 0;
      log_skip_logging (thread_p, &addr);
      pgbuf_set_dirty (thread_p, pgptr, FREE);
    }
  else
    {
      area = NULL;
    }

  return area;
}

/*
 * pgbuf_set_dirty () - Mark as modified the buffer associated with pgptr and
 *                  optionally free the page
 *   return: void
 *   pgptr(in): Pointer to page
 *   free_page(in): Free the page too ?
 */
void
pgbuf_set_dirty (THREAD_ENTRY * thread_p, PAGE_PTR pgptr, int free_page)
{
  PGBUF_BCB *bufptr;

  if (pgbuf_get_check_page_validation (thread_p,
				       PGBUF_DEBUG_PAGE_VALIDATION_ALL))
    {
      if (pgbuf_is_valid_page_ptr (pgptr) == false)
	{
	  return;
	}
    }

  /* Get the address of the buffer from the page and set buffer dirty */
  CAST_PGPTR_TO_BFPTR (bufptr, pgptr);
  assert (!VPID_ISNULL (&bufptr->vpid));

#if defined(SERVER_MODE) && !defined(NDEBUG)
  if (bufptr->vpid.pageid == 0)
    {
      DISK_VAR_HEADER *vhdr;

      (void) pgbuf_check_page_ptype (thread_p, pgptr, PAGE_VOLHEADER);

      vhdr = (DISK_VAR_HEADER *) pgptr;
      if (strncmp (vhdr->magic, CUBRID_MAGIC_DATABASE_VOLUME,
		   CUBRID_MAGIC_MAX_LENGTH) != 0)
	{
	  assert (0);
	}
    }
#endif

  pgbuf_set_dirty_buffer_ptr (thread_p, bufptr);

  /* If free request is given, unfix the page. */
  if (free_page == FREE)
    {
      pgbuf_unfix (thread_p, pgptr);
    }
}

/*
 * pgbuf_get_lsa () - Find the log sequence address of the given page
 *   return: page lsa
 *   pgptr(in): Pointer to page
 */
LOG_LSA *
pgbuf_get_lsa (PAGE_PTR pgptr)
{
  FILEIO_PAGE *io_pgptr;

  if (pgbuf_get_check_page_validation (NULL, PGBUF_DEBUG_PAGE_VALIDATION_ALL))
    {
      if (pgbuf_is_valid_page_ptr (pgptr) == false)
	{
	  return NULL;
	}
    }

  /* NOTE: Does not need to hold BCB_mutex since the page is fixed */

  CAST_PGPTR_TO_IOPGPTR (io_pgptr, pgptr);
  return &io_pgptr->prv.lsa;
}

/*
 * pgbuf_set_lsa () - Set the log sequence address of the page to the given lsa
 *   return: page lsa or NULL
 *   pgptr(in): Pointer to page
 *   lsa_ptr(in): Log Sequence address
 *
 * Note: This function is for the exclusive use of the log and recovery
 *       manager.
 */
const LOG_LSA *
pgbuf_set_lsa (THREAD_ENTRY * thread_p, PAGE_PTR pgptr,
	       const LOG_LSA * lsa_ptr)
{
  PGBUF_BCB *bufptr;

  if (pgbuf_get_check_page_validation (thread_p,
				       PGBUF_DEBUG_PAGE_VALIDATION_ALL))
    {
      if (pgbuf_is_valid_page_ptr (pgptr) == false)
	{
	  return NULL;
	}
    }

  assert (lsa_ptr != NULL);

  /* NOTE: Does not need to hold BCB_mutex since the page is fixed */

  /* Get the address of the buffer from the page and set buffer dirty */
  CAST_PGPTR_TO_BFPTR (bufptr, pgptr);

  /*
   * Don't change LSA of temporary volumes or auxiliary volumes.
   * (e.g., those of copydb, backupdb).
   */
  if (LSA_IS_INIT_TEMP (&bufptr->iopage_buffer->iopage.prv.lsa)
      || PGBUF_IS_AUXILIARY_VOLUME (bufptr->vpid.volid) == true)
    {
      return NULL;
    }

  /*
   * Always set the lsa of temporary volumes to the special
   * temp lsa, if it was somehow changed.
   */
  if (pgbuf_is_temporary_volume (bufptr->vpid.volid) == true)
    {
      LSA_SET_INIT_TEMP (&bufptr->iopage_buffer->iopage.prv.lsa);
      if (logtb_is_current_active (thread_p))
	{
	  return NULL;
	}
    }
  LSA_COPY (&bufptr->iopage_buffer->iopage.prv.lsa, lsa_ptr);

  /*
   * If this is the first time the page is set dirty, record the new LSA
   * of the page as the oldest_unflush_lsa for the page.
   * We could have placed these feature when the page is set dirty,
   * unfortunately, some pages are set dirty before an LSA is set.
   */
  if (LSA_ISNULL (&bufptr->oldest_unflush_lsa))
    {
      if (LSA_LT (lsa_ptr, &log_Gl.chkpt_redo_lsa))
	{
	  LOG_LSA chkpt_redo_lsa;
	  int rc;

	  rc = pthread_mutex_lock (&log_Gl.chkpt_lsa_lock);
	  LSA_COPY (&chkpt_redo_lsa, &log_Gl.chkpt_redo_lsa);
	  pthread_mutex_unlock (&log_Gl.chkpt_lsa_lock);

	  if (LSA_LT (lsa_ptr, &chkpt_redo_lsa))
	    {
	      er_stack_push ();
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_LOG_CHECKPOINT_SKIP_INVALID_PAGE, 6,
		      bufptr->vpid.pageid,
		      fileio_get_volume_label (bufptr->vpid.volid, PEEK),
		      lsa_ptr->pageid, lsa_ptr->offset,
		      log_Gl.chkpt_redo_lsa.pageid,
		      log_Gl.chkpt_redo_lsa.offset);
	      er_stack_pop ();

	      assert (false);
	    }

	}
      LSA_COPY (&bufptr->oldest_unflush_lsa, lsa_ptr);
    }

  return lsa_ptr;
}

/*
 * pgbuf_reset_temp_lsa () -  Reset LSA of temp volume to
 *                            special temp LSA (-2,-2)
 *   return: void
 *   pgptr(in): Pointer to page
 */
void
pgbuf_reset_temp_lsa (PAGE_PTR pgptr)
{
  PGBUF_BCB *bufptr;

  CAST_PGPTR_TO_BFPTR (bufptr, pgptr);
  LSA_SET_INIT_TEMP (&bufptr->iopage_buffer->iopage.prv.lsa);
}

/*
 * pgbuf_get_vpid () - Find the volume and page identifier associated with
 *                     the passed buffer
 *   return: void
 *   pgptr(in): Page pointer
 *   vpid(out): Volume and page identifier
 */
void
pgbuf_get_vpid (PAGE_PTR pgptr, VPID * vpid)
{
  PGBUF_BCB *bufptr;

  if (pgbuf_get_check_page_validation (NULL, PGBUF_DEBUG_PAGE_VALIDATION_ALL))
    {
      if (pgbuf_is_valid_page_ptr (pgptr) == false)
	{
	  VPID_SET_NULL (vpid);
	  return;
	}
    }

  /* NOTE: Does not need to hold BCB_mutex since the page is fixed */

  CAST_PGPTR_TO_BFPTR (bufptr, pgptr);
  *vpid = bufptr->vpid;
}

/*
 * pgbuf_get_vpid_ptr () - Find the volume and page identifier associated
 *                         with the passed buffer
 *   return: pointer to vpid
 *   pgptr(in): Page pointer
 *
 * Note: Once the buffer is freed, the content of the vpid pointer may be
 *       updated by the page buffer manager, thus a lot of care should be
 *       taken.
 *       The values of the vpid pointer must not be altered by the caller.
 *       Once the page is freed, the vpid pointer should not be used any longer.
 */
VPID *
pgbuf_get_vpid_ptr (PAGE_PTR pgptr)
{
  PGBUF_BCB *bufptr;

  if (pgbuf_get_check_page_validation (NULL, PGBUF_DEBUG_PAGE_VALIDATION_ALL))
    {
      if (pgbuf_is_valid_page_ptr (pgptr) == false)
	{
	  return NULL;
	}
    }

  /* NOTE: Does not need to hold BCB_mutex since the page is fixed */

  CAST_PGPTR_TO_BFPTR (bufptr, pgptr);
  return &(bufptr->vpid);
}

/*
 * pgbuf_get_page_id () - Find the page identifier associated with the
 *                        passed buffer
 *   return: PAGEID
 *   pgptr(in): Page pointer
 */
PAGEID
pgbuf_get_page_id (PAGE_PTR pgptr)
{
  PGBUF_BCB *bufptr;

  if (pgbuf_get_check_page_validation (NULL, PGBUF_DEBUG_PAGE_VALIDATION_ALL))
    {
      if (pgbuf_is_valid_page_ptr (pgptr) == false)
	{
	  return NULL_PAGEID;
	}
    }

  /* NOTE: Does not need to hold BCB_mutex since the page is fixed */

  CAST_PGPTR_TO_BFPTR (bufptr, pgptr);
  assert (pgbuf_check_bcb_page_vpid (NULL, bufptr) == true);

  return bufptr->vpid.pageid;
}

/*
 * pgbuf_get_page_ptype () -
 *   return:
 *   pgptr(in): Pointer to page
 */
PAGE_TYPE
pgbuf_get_page_ptype (THREAD_ENTRY * thread_p, PAGE_PTR pgptr)
{
  PGBUF_BCB *bufptr;
  PAGE_TYPE ptype;

  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  if (pgbuf_get_check_page_validation (thread_p,
				       PGBUF_DEBUG_PAGE_VALIDATION_ALL))
    {
      if (pgbuf_is_valid_page_ptr (pgptr) == false)
	{
	  return PAGE_UNKNOWN;	/* TODO - need to return error_code */
	}
    }

  /* NOTE: Does not need to hold BCB_mutex since the page is fixed */

  CAST_PGPTR_TO_BFPTR (bufptr, pgptr);
  assert_release (pgbuf_check_bcb_page_vpid (thread_p, bufptr) == true);

  ptype = (PAGE_TYPE) (bufptr->iopage_buffer->iopage.prv.ptype);

  assert (PAGE_UNKNOWN <= ptype);
  assert (ptype < PAGE_LAST);

  return ptype;
}

/*
 * pgbuf_get_volume_id () - Find the volume associated with the passed buffer
 *   return: VOLID
 *   pgptr(in): Page pointer
 */
VOLID
pgbuf_get_volume_id (PAGE_PTR pgptr)
{
  PGBUF_BCB *bufptr;

  if (pgbuf_get_check_page_validation (NULL, PGBUF_DEBUG_PAGE_VALIDATION_ALL))
    {
      if (pgbuf_is_valid_page_ptr (pgptr) == false)
	{
	  return NULL_VOLID;
	}
    }

  /* NOTE: Does not need to hold BCB_mutex since the page is fixed */

  CAST_PGPTR_TO_BFPTR (bufptr, pgptr);
  return bufptr->vpid.volid;
}

/*
 * pgbuf_get_volume_label () - Find the name of the volume associated with
 *                             the passed buffer
 *   return: Volume label
 *   pgptr(in): Page pointer
 */
const char *
pgbuf_get_volume_label (PAGE_PTR pgptr)
{
  PGBUF_BCB *bufptr;

  /* NOTE: Does not need to hold BCB_mutex since the page is fixed */

  CAST_PGPTR_TO_BFPTR (bufptr, pgptr);
  assert (!VPID_ISNULL (&bufptr->vpid));

  return fileio_get_volume_label (bufptr->vpid.volid, PEEK);
}

/*
 * pgbuf_refresh_max_permanent_volume_id () - Refresh the maxmim permanent
 *                             volume identifier cached by the page buffer pool
 *   return: void
 *   volid(in): Volume identifier of last allocated permanent volume
 *
 * Note: This is needed to initialize the lsa of a page that it is fetched as
 *       new. The lsa need to be initialized according to the storage purpose
 *       of the page.
 */
void
pgbuf_refresh_max_permanent_volume_id (VOLID volid)
{
#if defined(SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */

  rv = pthread_mutex_lock (&pgbuf_Pool.volinfo_mutex);
  pgbuf_Pool.last_perm_volid = volid;
  pthread_mutex_unlock (&pgbuf_Pool.volinfo_mutex);
}

/*
 * pgbuf_cache_permanent_volume_for_temporary () - The given permanent volume
 *                                  is remembered for temporary storage purposes
 *   return: void
 *   volid(in): Volume identifier of last allocated permanent volume
 *
 * Note: This declaration is needed since newly allocated pages can be fetched
 *       as "new". That is, the page buffer manager will not read the page
 *       from disk.
 */
void
pgbuf_cache_permanent_volume_for_temporary (VOLID volid)
{
  void *area;
  int num_entries, nbytes;
#if defined(SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */

  rv = pthread_mutex_lock (&pgbuf_Pool.volinfo_mutex);
  if (pgbuf_Pool.permvols_tmparea_volids == NULL)
    {
      num_entries = 10;
      nbytes = num_entries * sizeof (pgbuf_Pool.permvols_tmparea_volids);

      area = malloc (nbytes);
      if (area == NULL)
	{
	  pthread_mutex_unlock (&pgbuf_Pool.volinfo_mutex);
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_OUT_OF_VIRTUAL_MEMORY, 1, nbytes);
	  return;
	}
      pgbuf_Pool.permvols_tmparea_volids = (VOLID *) area;
      pgbuf_Pool.size_permvols_tmparea_volids = num_entries;
      /* pgbuf_Pool.num_permvols_tmparea is initialized to 0 in pgbuf_initialize(). */
    }
  else if (pgbuf_Pool.size_permvols_tmparea_volids ==
	   pgbuf_Pool.num_permvols_tmparea)
    {
      /* We need to expand the array */
      num_entries = pgbuf_Pool.size_permvols_tmparea_volids + 10;
      nbytes = num_entries * sizeof (pgbuf_Pool.permvols_tmparea_volids);

      area = realloc (pgbuf_Pool.permvols_tmparea_volids, nbytes);
      if (area == NULL)
	{
	  pthread_mutex_unlock (&pgbuf_Pool.volinfo_mutex);
	  return;
	}
      pgbuf_Pool.permvols_tmparea_volids = (VOLID *) area;
      pgbuf_Pool.size_permvols_tmparea_volids = num_entries;
    }

  pgbuf_Pool.permvols_tmparea_volids[pgbuf_Pool.num_permvols_tmparea] = volid;
  pgbuf_Pool.num_permvols_tmparea++;
  pthread_mutex_unlock (&pgbuf_Pool.volinfo_mutex);
}

/*
 * pgbuf_force_to_check_for_interrupts () - Force the page buffer manager
 *      to check for possible interrupts when pages are fetched
 *   return: void
 *   void(in):
 */
void
pgbuf_force_to_check_for_interrupts (void)
{
  pgbuf_Pool.check_for_interrupts = true;
}

/*
 * pgbuf_is_log_check_for_interrupts () - Force the page buffer manager to
 *      check for possible interrupts when pages are fetched
 *   return: if there is interrupt, return true, otherwise return false
 *   void(in):
 */
bool
pgbuf_is_log_check_for_interrupts (THREAD_ENTRY * thread_p)
{
  if (pgbuf_Pool.check_for_interrupts == true
      && logtb_is_interrupted (thread_p, true,
			       &pgbuf_Pool.check_for_interrupts) == true)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INTERRUPTED, 0);
      return true;
    }
  else
    {
      return false;
    }
}

/*
 * pgbuf_set_lsa_as_temporary () - The log sequence address of the page
 *                                        is set to temporary lsa address
 *   return: void
 *   pgptr(in): Pointer to page
 *
 * Note: Set the log sequence address of the page to the non recoverable LSA
 *       address. In this case the page is declared a non recoverable page
 *       (temporary page). Logging must not be done in a temporary page,
 *       however it is not enforced. A warning message is issued if someone
 *       logs something. This warning will indicate a potential bug.
 *
 *       This function is used for debugging.
 */
void
pgbuf_set_lsa_as_temporary (THREAD_ENTRY * thread_p, PAGE_PTR pgptr)
{
  PGBUF_BCB *bufptr;

  CAST_PGPTR_TO_BFPTR (bufptr, pgptr);
  assert (!VPID_ISNULL (&bufptr->vpid));

  LSA_SET_INIT_TEMP (&bufptr->iopage_buffer->iopage.prv.lsa);
  pgbuf_set_dirty_buffer_ptr (thread_p, bufptr);
}

/*
 * pgbuf_set_lsa_as_permanent () - The log sequence address of the page
 *                                        is set to permanent lsa address
 *   return: void
 *   pgptr(in): Pointer to page
 *
 * Note: Set the log sequence address of the page to a permananet LSA address.
 *       Logging can be done in this page.
 *
 *       This function is used for debugging.
 */
void
pgbuf_set_lsa_as_permanent (THREAD_ENTRY * thread_p, PAGE_PTR pgptr)
{
  PGBUF_BCB *bufptr;
  LOG_LSA *restart_lsa;

  CAST_PGPTR_TO_BFPTR (bufptr, pgptr);
  assert (!VPID_ISNULL (&bufptr->vpid));

  if (LSA_IS_INIT_TEMP (&bufptr->iopage_buffer->iopage.prv.lsa)
      || LSA_IS_INIT_NONTEMP (&bufptr->iopage_buffer->iopage.prv.lsa))
    {
      restart_lsa = logtb_find_current_tran_lsa (thread_p);
      if (restart_lsa == NULL || LSA_ISNULL (restart_lsa))
	{
	  restart_lsa = log_get_restart_lsa ();
	}

      LSA_COPY (&bufptr->iopage_buffer->iopage.prv.lsa, restart_lsa);
      pgbuf_set_dirty_buffer_ptr (thread_p, bufptr);
    }
}

/*
 * pgbuf_set_bcb_page_vpid () -
 *   return: void
 *   bufptr(in): pointer to buffer page
 *
 * Note: This function is used for debugging.
 */
static void
pgbuf_set_bcb_page_vpid (THREAD_ENTRY * thread_p, PGBUF_BCB * bufptr)
{
  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  if (bufptr == NULL || VPID_ISNULL (&bufptr->vpid))
    {
      assert (bufptr != NULL);
      assert (!VPID_ISNULL (&bufptr->vpid));
      return;
    }

  /* perm volume */
  if (bufptr->vpid.volid > NULL_VOLID)
    {
      /* Check iff is the first time */
      if (bufptr->iopage_buffer->iopage.prv.pageid == -1
	  && bufptr->iopage_buffer->iopage.prv.volid == -1)
	{
	  /* Set Page identifier */
	  bufptr->iopage_buffer->iopage.prv.pageid = bufptr->vpid.pageid;
	  bufptr->iopage_buffer->iopage.prv.volid = bufptr->vpid.volid;

	  bufptr->iopage_buffer->iopage.prv.ptype = '\0';
	  bufptr->iopage_buffer->iopage.prv.pflag_reserve_1 = '\0';
	  bufptr->iopage_buffer->iopage.prv.p_reserve_2 = 0;
	  bufptr->iopage_buffer->iopage.prv.p_reserve_3 = 0;
	}
    }

}

/*
 * pgbuf_set_page_ptype () -
 *   return: void
 *   pgptr(in): Pointer to page
 *   ptype(in): page type
 *
 * Note: This function is used for debugging.
 */
void
pgbuf_set_page_ptype (THREAD_ENTRY * thread_p, PAGE_PTR pgptr,
		      PAGE_TYPE ptype)
{
  PGBUF_BCB *bufptr;

  assert (pgptr != NULL);

  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  if (pgbuf_get_check_page_validation (thread_p,
				       PGBUF_DEBUG_PAGE_VALIDATION_ALL))
    {
      if (pgbuf_is_valid_page_ptr (pgptr) == false)
	{
	  assert (false);
	  return;
	}
    }

  CAST_PGPTR_TO_BFPTR (bufptr, pgptr);
  assert (!VPID_ISNULL (&bufptr->vpid));

  /* Set Page identifier iff needed */
  (void) pgbuf_set_bcb_page_vpid (thread_p, bufptr);

  if (pgbuf_check_bcb_page_vpid (thread_p, bufptr) != true)
    {
      assert (false);
      return;
    }

  bufptr->iopage_buffer->iopage.prv.ptype = (unsigned char) ptype;

  assert_release (bufptr->iopage_buffer->iopage.prv.ptype == ptype);
}

/*
 * pgbuf_is_lsa_temporary () - Find if the page is a temporary one
 *   return: true/false
 *   pgptr(in): Pointer to page
 */
bool
pgbuf_is_lsa_temporary (PAGE_PTR pgptr)
{
  PGBUF_BCB *bufptr;

  CAST_PGPTR_TO_BFPTR (bufptr, pgptr);

  if (LSA_IS_INIT_TEMP (&bufptr->iopage_buffer->iopage.prv.lsa) ||
      pgbuf_is_temporary_volume (bufptr->vpid.volid) == true)
    {
      return true;
    }
  else
    {
      return false;
    }
}

/*
 * pgbuf_isvolume_for_tmparea () - Find if the given permanent volume has been
 *                              declared for temporary storage purposes
 *   return: true/false
 *   volid(in): Volume identifier of last allocated permanent volume
 */
static bool
pgbuf_is_temporary_volume (VOLID volid)
{
  int i;
#if defined(SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */

  rv = pthread_mutex_lock (&pgbuf_Pool.volinfo_mutex);
  if (volid > pgbuf_Pool.last_perm_volid)
    {
      /* it means temporary volumes */
      pthread_mutex_unlock (&pgbuf_Pool.volinfo_mutex);
      return true;
    }

  /* find the given volid
     from the volid set of permanent volumes with temporary purpose. */
  for (i = 0; i < pgbuf_Pool.num_permvols_tmparea; i++)
    {
      if (volid == pgbuf_Pool.permvols_tmparea_volids[i])
	{
	  pthread_mutex_unlock (&pgbuf_Pool.volinfo_mutex);
	  return true;
	}
    }

  pthread_mutex_unlock (&pgbuf_Pool.volinfo_mutex);
  return false;
}

/*
 * pgbuf_init_BCB_table () - Initializes page buffer BCB table
 *   return: NO_ERROR, or ER_code
 */
static int
pgbuf_initialize_bcb_table (void)
{
  PGBUF_BCB *bufptr;
  PGBUF_IOPAGE_BUFFER *ioptr;
  int i;
  size_t alloc_size;

  /* allocate space for page buffer BCB table */
  alloc_size = (size_t) pgbuf_Pool.num_buffers * PGBUF_BCB_SIZE;
  pgbuf_Pool.BCB_table = (PGBUF_BCB *) malloc (alloc_size);
  if (pgbuf_Pool.BCB_table == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
	      1, alloc_size);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  /* allocate space for io page buffers */
  alloc_size = (size_t) pgbuf_Pool.num_buffers * PGBUF_IOPAGE_BUFFER_SIZE;
  pgbuf_Pool.iopage_table = (PGBUF_IOPAGE_BUFFER *) malloc (alloc_size);
  if (pgbuf_Pool.iopage_table == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
	      1, alloc_size);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  /* initialize each entry of the buffer BCB table */
  for (i = 0; i < pgbuf_Pool.num_buffers; i++)
    {
      bufptr = PGBUF_FIND_BCB_PTR (i);
      pthread_mutex_init (&bufptr->BCB_mutex, NULL);
      bufptr->ipool = i;
      VPID_SET_NULL (&bufptr->vpid);
      bufptr->fcnt = 0;
      bufptr->latch_mode = PGBUF_LATCH_INVALID;

#if defined(SERVER_MODE)
      bufptr->next_wait_thrd = NULL;
#endif /* SERVER_MODE */

      bufptr->hash_next = NULL;
      bufptr->prev_BCB = NULL;

      if (i == (pgbuf_Pool.num_buffers - 1))
	{
	  bufptr->next_BCB = NULL;
	}
      else
	{
	  bufptr->next_BCB = PGBUF_FIND_BCB_PTR (i + 1);
	}

      bufptr->dirty = false;
      bufptr->avoid_victim = false;
      bufptr->async_flush_request = false;
      bufptr->victim_candidate = false;
      bufptr->zone = PGBUF_INVALID_ZONE;
      LSA_SET_NULL (&bufptr->oldest_unflush_lsa);

      /* link BCB and iopage buffer */
      ioptr = PGBUF_FIND_IOPAGE_PTR (i);

      LSA_SET_NULL (&ioptr->iopage.prv.lsa);

      /* Init Page identifier */
      ioptr->iopage.prv.pageid = -1;
      ioptr->iopage.prv.volid = -1;

#if 1				/* do not delete me */
      ioptr->iopage.prv.ptype = '\0';
      ioptr->iopage.prv.pflag_reserve_1 = '\0';
      ioptr->iopage.prv.p_reserve_2 = 0;
      ioptr->iopage.prv.p_reserve_3 = 0;
#endif

      bufptr->iopage_buffer = ioptr;
      ioptr->bcb = bufptr;

#if defined(CUBRID_DEBUG)
      /* Reinitizalize the buffer */
      pgbuf_scramble (&bufptr->iopage_buffer->iopage);
      memcpy (PGBUF_FIND_BUFFER_GUARD (bufptr), pgbuf_Guard,
	      sizeof (pgbuf_Guard));
#endif /* CUBRID_DEBUG */
    }

  return NO_ERROR;
}

/*
 * pgbuf_initialize_hash_table () - Initializes page buffer hash table
 *   return: NO_ERROR, or ER_code
 */
static int
pgbuf_initialize_hash_table (void)
{
  size_t hashsize, i;

  /* allocate space for the buffer hash table */
  hashsize = PGBUF_HASH_SIZE;
  pgbuf_Pool.buf_hash_table =
    (PGBUF_BUFFER_HASH *) malloc (hashsize * PGBUF_BUFFER_HASH_SIZE);
  if (pgbuf_Pool.buf_hash_table == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
	      1, (hashsize * PGBUF_BUFFER_HASH_SIZE));
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  /* initialize each entry of the buffer hash table */
  for (i = 0; i < hashsize; i++)
    {
      pthread_mutex_init (&pgbuf_Pool.buf_hash_table[i].hash_mutex, NULL);
      pgbuf_Pool.buf_hash_table[i].hash_next = NULL;
      pgbuf_Pool.buf_hash_table[i].lock_next = NULL;
    }

  return NO_ERROR;
}

/*
 * pgbuf_initialize_lock_table () - Initializes page buffer lock table
 *   return: NO_ERROR, or ER_code
 */
static int
pgbuf_initialize_lock_table (void)
{
  int i;
  int thrd_num_total;
  size_t alloc_size;

  /* allocate memory space for the buffer lock table */
  thrd_num_total = thread_num_total_threads ();
#if defined(SERVER_MODE)
  assert (thrd_num_total > MAX_NTRANS * 2);
#else /* !SERVER_MODE */
  assert (thrd_num_total == 1);
#endif /* !SERVER_MODE */

  alloc_size = thrd_num_total * PGBUF_BUFFER_LOCK_SIZE;
  pgbuf_Pool.buf_lock_table = (PGBUF_BUFFER_LOCK *) malloc (alloc_size);
  if (pgbuf_Pool.buf_lock_table == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
	      1, alloc_size);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  /* initialize each entry of the buffer lock table */
  for (i = 0; i < thrd_num_total; i++)
    {
      VPID_SET_NULL (&pgbuf_Pool.buf_lock_table[i].vpid);
      pgbuf_Pool.buf_lock_table[i].lock_next = NULL;
#if defined(SERVER_MODE)
      pgbuf_Pool.buf_lock_table[i].next_wait_thrd = NULL;
#endif /* SERVER_MODE */
    }

  return NO_ERROR;
}

/*
 * pgbuf_initialize_lru_list () - Initializes the page buffer LRU list
 *   return: NO_ERROR, or ER_code
 */
static int
pgbuf_initialize_lru_list (void)
{
  int i;

  /* set the number of LRU lists */
  pgbuf_Pool.last_flushed_LRU_list_idx = -1;
  pgbuf_Pool.num_LRU_list = prm_get_integer_value (PRM_ID_PB_NUM_LRU_CHAINS);
  if (pgbuf_Pool.num_LRU_list == 0)
    {
      /* system define it as an optimal value internally. */
      /* estimates: 1000 buffer frames per one LRU list will be good. */
      pgbuf_Pool.num_LRU_list = ((pgbuf_Pool.num_buffers - 1) / 1000) + 1;
    }

  /* allocate memory space for the page buffer LRU lists */
  pgbuf_Pool.buf_LRU_list =
    (PGBUF_LRU_LIST *) malloc (pgbuf_Pool.num_LRU_list * PGBUF_LRU_LIST_SIZE);
  if (pgbuf_Pool.buf_LRU_list == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
	      1, (pgbuf_Pool.num_LRU_list * PGBUF_LRU_LIST_SIZE));
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  /* initialize the page buffer LRU lists */
  for (i = 0; i < pgbuf_Pool.num_LRU_list; i++)
    {
      pthread_mutex_init (&pgbuf_Pool.buf_LRU_list[i].LRU_mutex, NULL);
      pgbuf_Pool.buf_LRU_list[i].LRU_top = NULL;
      pgbuf_Pool.buf_LRU_list[i].LRU_bottom = NULL;
      pgbuf_Pool.buf_LRU_list[i].LRU_middle = NULL;
      pgbuf_Pool.buf_LRU_list[i].LRU_1_zone_cnt = 0;
    }

  return NO_ERROR;
}

/*
 * pgbuf_initialize_ain_list () - initialize the Ain list
 * return : NO_ERROR or error code
 */
static int
pgbuf_initialize_ain_list (void)
{
  float ain_ratio = prm_get_float_value (PRM_ID_PB_AIN_RATIO);
  float lru1_ratio = prm_get_float_value (PRM_ID_PB_LRU_HOT_RATIO);

  pgbuf_Pool.buf_AIN_list.max_count =
    (int) (pgbuf_Pool.num_buffers * ain_ratio);
  pgbuf_Pool.buf_AIN_list.ain_count = 0;
  pgbuf_Pool.buf_AIN_list.Ain_top = NULL;
  pgbuf_Pool.buf_AIN_list.Ain_bottom = NULL;
  pgbuf_Pool.buf_AIN_list.tick = 0;

  pthread_mutex_init (&pgbuf_Pool.buf_AIN_list.Ain_mutex, NULL);

  if (ain_ratio <= 0)
    {
      /* 2Q is disabled, just use LRU */
      pgbuf_Pool.buf_AIN_list.max_count = 0;
      ain_ratio = 0;
    }

  pgbuf_Pool.num_LRU1_zone_threshold = (int)
    (PGBUF_LRU_SIZE * (1.0f - ain_ratio) * lru1_ratio);
  pgbuf_Pool.num_LRU1_zone_threshold =
    MAX (pgbuf_Pool.num_LRU1_zone_threshold, (int) (PGBUF_LRU_SIZE * 0.05f));
  pgbuf_Pool.num_LRU1_zone_threshold =
    MIN (pgbuf_Pool.num_LRU1_zone_threshold, (int) (PGBUF_LRU_SIZE * 0.95f));

  assert_release (pgbuf_Pool.num_LRU1_zone_threshold
		  < (PGBUF_LRU_SIZE * (1.0f - ain_ratio)));

  return NO_ERROR;
}

/*
 * pgbuf_initialize_aout_list () - initialize the Aout list
 * return : error code or NO_ERROR
 */
static int
pgbuf_initialize_aout_list (void)
{
  int i;
  float aout_ratio;
  size_t alloc_size = 0;
  PGBUF_AOUT_LIST *list = &pgbuf_Pool.buf_AOUT_list;

  aout_ratio = prm_get_float_value (PRM_ID_PB_AOUT_RATIO);

  list->max_count = (int) (pgbuf_Pool.num_buffers * aout_ratio);
  list->Aout_top = NULL;
  list->Aout_bottom = NULL;
  list->bufarray = NULL;
  list->aout_buf_ht = NULL;

  pthread_mutex_init (&list->Aout_mutex, NULL);

  if (aout_ratio <= 0)
    {
      /* not using Aout list */
      list->max_count = 0;
      return NO_ERROR;
    }

  alloc_size = list->max_count * sizeof (PGBUF_AOUT_BUF);

  list->bufarray = (PGBUF_AOUT_BUF *) malloc (alloc_size);
  if (list->bufarray == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
	      1, alloc_size);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  list->Aout_free = &list->bufarray[0];

  for (i = 0; i < list->max_count; i++)
    {
      VPID_SET_NULL (&list->bufarray[i].vpid);
      if (i != list->max_count - 1)
	{
	  list->bufarray[i].next = &list->bufarray[i + 1];
	}
      else
	{
	  list->bufarray[i].next = NULL;
	}
      list->bufarray[i].prev = NULL;
    }

  list->num_hashes = MAX (list->max_count / AOUT_HASH_DIVIDE_RATIO, 1);

  alloc_size = list->num_hashes * sizeof (MHT_TABLE *);
  list->aout_buf_ht = malloc (alloc_size);
  if (list->aout_buf_ht == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
	      1, alloc_size);
      goto error_return;
    }

  memset (list->aout_buf_ht, 0, alloc_size);

  for (i = 0; i < list->num_hashes; i++)
    {
      list->aout_buf_ht[i] = mht_create ("PGBUF_AOUT_HASH",
					 list->max_count, pgbuf_hash_vpid,
					 pgbuf_compare_vpid);

      if (list->aout_buf_ht[i] == NULL)
	{
	  goto error_return;
	}
    }

  return NO_ERROR;

error_return:
  list->Aout_free = NULL;
  if (list->bufarray != NULL)
    {
      free_and_init (list->bufarray);
    }

  if (list->aout_buf_ht == NULL)
    {
      for (i = 0; list->aout_buf_ht[i] != NULL; i++)
	{
	  mht_destroy (list->aout_buf_ht[i]);
	}
      free_and_init (list->aout_buf_ht);
    }

  pthread_mutex_destroy (&list->Aout_mutex);

  return ER_FAILED;
}

/*
 * pgbuf_initialize_invalid_list () - Initializes the page buffer invalid list
 *   return: NO_ERROR
 */
static int
pgbuf_initialize_invalid_list (void)
{
  /* initialize the invalid BCB list */
  pthread_mutex_init (&pgbuf_Pool.buf_invalid_list.invalid_mutex, NULL);
  pgbuf_Pool.buf_invalid_list.invalid_top = PGBUF_FIND_BCB_PTR (0);
  pgbuf_Pool.buf_invalid_list.invalid_cnt = pgbuf_Pool.num_buffers;

  return NO_ERROR;
}

/*
 * pgbuf_initialize_thrd_holder () -
 *   return: NO_ERROR, or ER_code
 */
static int
pgbuf_initialize_thrd_holder (void)
{
  int thrd_num_total;
  size_t alloc_size;
  int i, j, idx;

  thrd_num_total = thread_num_total_threads ();
#if defined(SERVER_MODE)
  assert (thrd_num_total > MAX_NTRANS * 2);
#else /* !SERVER_MODE */
  assert (thrd_num_total == 1);
#endif /* !SERVER_MODE */

  pgbuf_Pool.thrd_holder_info = (PGBUF_HOLDER_ANCHOR *)
    malloc (thrd_num_total * PGBUF_HOLDER_ANCHOR_SIZE);
  if (pgbuf_Pool.thrd_holder_info == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
	      1, thrd_num_total * PGBUF_HOLDER_ANCHOR_SIZE);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  /* phase 1: allocate memory space that is used for BCB holder entries */
  alloc_size = thrd_num_total * PGBUF_DEFAULT_FIX_COUNT * PGBUF_HOLDER_SIZE;
  pgbuf_Pool.thrd_reserved_holder = (PGBUF_HOLDER *) malloc (alloc_size);
  if (pgbuf_Pool.thrd_reserved_holder == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
	      1, alloc_size);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  /* phase 2: initialize all the BCB holder entries */

  /*
   * Each thread has both free holder list and used(held) holder list.
   * The free holder list of each thread is initialized to
   * have PGBUF_DEFAULT_FIX_COUNT entries and the used holder list of
   * each thread is initialized to have no entry.
   */
  for (i = 0; i < thrd_num_total; i++)
    {
      pgbuf_Pool.thrd_holder_info[i].num_hold_cnt = 0;
      pgbuf_Pool.thrd_holder_info[i].num_free_cnt = PGBUF_DEFAULT_FIX_COUNT;
      pgbuf_Pool.thrd_holder_info[i].thrd_hold_list = NULL;
      pgbuf_Pool.thrd_holder_info[i].thrd_free_list
	= &(pgbuf_Pool.thrd_reserved_holder[i * PGBUF_DEFAULT_FIX_COUNT]);

      for (j = 0; j < PGBUF_DEFAULT_FIX_COUNT; j++)
	{
	  idx = (i * PGBUF_DEFAULT_FIX_COUNT) + j;
	  pgbuf_Pool.thrd_reserved_holder[idx].fix_count = 0;
	  pgbuf_Pool.thrd_reserved_holder[idx].bufptr = NULL;
	  pgbuf_Pool.thrd_reserved_holder[idx].thrd_link = NULL;

	  if (j == (PGBUF_DEFAULT_FIX_COUNT - 1))
	    {
	      pgbuf_Pool.thrd_reserved_holder[idx].next_holder = NULL;
	    }
	  else
	    {
	      pgbuf_Pool.thrd_reserved_holder[idx].next_holder
		= &(pgbuf_Pool.thrd_reserved_holder[idx + 1]);
	    }
	}
    }

  /* phase 3: initialize free BCB holder list shared by all threads */
  pthread_mutex_init (&pgbuf_Pool.free_holder_set_mutex, NULL);
  pgbuf_Pool.free_holder_set = NULL;
  pgbuf_Pool.free_index = -1;	/* -1 means that there is no free holder entry */

  return NO_ERROR;
}

/*
 * pgbuf_allocate_thrd_holder_entry () - Allocates one buffer holder entry
 *   			from the free holder list of given thread
 *   return: pointer to holder entry or NULL
 *
 * Note: If the free holder list is empty,
 *       allocate it from the list of free holder arrays that is shared.
 */
static PGBUF_HOLDER *
pgbuf_allocate_thrd_holder_entry (THREAD_ENTRY * thread_p)
{
  int thrd_index;
  PGBUF_HOLDER_ANCHOR *thrd_holder_info;
  PGBUF_HOLDER *holder;
  PGBUF_HOLDER_SET *holder_set;
#if defined(SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */

  thrd_index = THREAD_GET_CURRENT_ENTRY_INDEX (thread_p);

  thrd_holder_info = &(pgbuf_Pool.thrd_holder_info[thrd_index]);

  if (thrd_holder_info->thrd_free_list != NULL)
    {
      /* allocate a BCB holder entry
       * from the free BCB holder list of given thread */
      holder = thrd_holder_info->thrd_free_list;
      thrd_holder_info->thrd_free_list = holder->next_holder;
      thrd_holder_info->num_free_cnt -= 1;
    }
  else
    {
      /* holder == NULL : free BCB holder list is empty */

      /* allocate a BCB holder entry
       * from the free BCB holder list shared by all threads.
       */
      rv = pthread_mutex_lock (&pgbuf_Pool.free_holder_set_mutex);
      if (pgbuf_Pool.free_index == -1)
	{
	  /* no usable free holder entry */
	  /* expand the free BCB holder list shared by threads */
	  holder_set = (PGBUF_HOLDER_SET *) malloc (PGBUF_HOLDER_SET_SIZE);
	  if (holder_set == NULL)
	    {
	      /* This situation must not be occurred. */
	      assert (false);
	      pthread_mutex_unlock (&pgbuf_Pool.free_holder_set_mutex);
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_OUT_OF_VIRTUAL_MEMORY, 1, PGBUF_HOLDER_SET_SIZE);
	      return NULL;
	    }

	  holder_set->next_set = pgbuf_Pool.free_holder_set;
	  pgbuf_Pool.free_holder_set = holder_set;
	  pgbuf_Pool.free_index = 0;
	}

      holder = &(pgbuf_Pool.free_holder_set->element[pgbuf_Pool.free_index]);
      pgbuf_Pool.free_index += 1;

      if (pgbuf_Pool.free_index == PGBUF_NUM_ALLOC_HOLDER)
	{
	  pgbuf_Pool.free_index = -1;
	}
      pthread_mutex_unlock (&pgbuf_Pool.free_holder_set_mutex);

      /* initialize the newly allocated BCB holder entry */
      holder->thrd_link = NULL;
    }

  holder->next_holder = NULL;	/* disconnect from free BCB holder list */

  /* connect the BCB holder entry at the head of thread's holder list */
  holder->thrd_link = thrd_holder_info->thrd_hold_list;
  thrd_holder_info->thrd_hold_list = holder;
  thrd_holder_info->num_hold_cnt += 1;

  return holder;
}

/*
 * pgbuf_find_thrd_holder () - Find the holder entry of current thread
 *                             on the BCB holder list of given BCB
 *   return: pointer to holder entry or NULL
 *   bufptr(in):
 */
static PGBUF_HOLDER *
pgbuf_find_thrd_holder (THREAD_ENTRY * thread_p, PGBUF_BCB * bufptr)
{
  int thrd_index;
  PGBUF_HOLDER *holder;
#if defined(SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */

  assert (bufptr != NULL);

  thrd_index = THREAD_GET_CURRENT_ENTRY_INDEX (thread_p);

  /* For each BCB holder entry of thread's holder list */
  holder = pgbuf_Pool.thrd_holder_info[thrd_index].thrd_hold_list;

  while (holder != NULL)
    {
      assert (holder->next_holder == NULL);

      if (holder->bufptr == bufptr)
	{
	  break;		/* found */
	}

      holder = holder->thrd_link;
    }

  return holder;
}

/*
 * pgbuf_unlatch_thrd_holder () - decrements fix_count by one to the holder
 *                                entry of current thread on the BCB holder
 *                                list of given BCB
 *   return: pointer to holder entry or NULL
 *   bufptr(in):
 */
static int
pgbuf_unlatch_thrd_holder (THREAD_ENTRY * thread_p, PGBUF_BCB * bufptr)
{
  int err = NO_ERROR;
  PGBUF_HOLDER *holder;
  PAGE_PTR pgptr;
#if defined(SERVER_MODE)
  int rv;
#endif

  assert (bufptr != NULL);

  CAST_BFPTR_TO_PGPTR (pgptr, bufptr);

  holder = pgbuf_find_thrd_holder (thread_p, bufptr);
  if (holder == NULL)
    {
      /* This situation must not be occurred. */
      assert (false);
      err = ER_PB_UNFIXED_PAGEPTR;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err, 3,
	      pgptr, bufptr->vpid.pageid,
	      fileio_get_volume_label (bufptr->vpid.volid, PEEK));

      goto exit_on_error;
    }

  holder->fix_count--;

  if (holder->fix_count == 0)
    {
      /* remove its own BCB holder entry */
      if (pgbuf_remove_thrd_holder (thread_p, holder) != NO_ERROR)
	{
	  /* This situation must not be occurred. */
	  assert (false);
	  err = ER_PB_UNFIXED_PAGEPTR;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err, 3,
		  pgptr, bufptr->vpid.pageid,
		  fileio_get_volume_label (bufptr->vpid.volid, PEEK));

	  goto exit_on_error;
	}
    }

  assert (err == NO_ERROR);

exit_on_error:

  return err;
}

/*
 * pgbuf_remove_thrd_holder () - Remove holder entry from given BCB
 *   return: NO_ERROR, or ER_code
 *   holder(in): pointer to holder entry to be removed
 *
 * Note: This function removes the given holder entry from the holder list of
 *       given BCB, and then connect it to the free holder list of the
 *       corresponding thread.
 */
static int
pgbuf_remove_thrd_holder (THREAD_ENTRY * thread_p, PGBUF_HOLDER * holder)
{
  int err = NO_ERROR;
  int thrd_index;
  PGBUF_HOLDER_ANCHOR *thrd_holder_info;
  PGBUF_HOLDER *prev;
  int found;
#if defined(SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */

  assert (holder != NULL);
  assert (holder->fix_count == 0);

  /* holder->fix_count is always set to some meaningful value
   * when the holder entry is allocated for use.
   * So, at this time, we do not need to initialize it.
   * 
   * connect the BCB holder entry into free BCB holder list of
   * given thread.
   */

  thrd_index = THREAD_GET_CURRENT_ENTRY_INDEX (thread_p);

  thrd_holder_info = &(pgbuf_Pool.thrd_holder_info[thrd_index]);

  holder->next_holder = thrd_holder_info->thrd_free_list;
  thrd_holder_info->thrd_free_list = holder;
  thrd_holder_info->num_free_cnt += 1;

  /* remove the BCB holder entry from thread's holder list */
  if (thrd_holder_info->thrd_hold_list == NULL)
    {
      /* This situation must not be occurred. */
      assert (false);
      err = ER_FAILED;
      goto exit_on_error;
    }

  if (thrd_holder_info->thrd_hold_list == (PGBUF_HOLDER *) holder)
    {
      thrd_holder_info->thrd_hold_list = holder->thrd_link;
    }
  else
    {
      found = false;
      prev = thrd_holder_info->thrd_hold_list;

      while (prev->thrd_link != NULL)
	{
	  assert (prev->next_holder == NULL);
	  if (prev->thrd_link == (PGBUF_HOLDER *) holder)
	    {
	      prev->thrd_link = holder->thrd_link;
	      holder->thrd_link = NULL;
	      found = true;
	      break;
	    }
	  prev = prev->thrd_link;
	}

      if (found == false)
	{
	  /* This situation must not be occurred. */
	  assert (false);
	  err = ER_FAILED;
	  goto exit_on_error;
	}
    }

  thrd_holder_info->num_hold_cnt -= 1;

  assert (err == NO_ERROR);

exit_on_error:

  return err;
}

/*
 * pgbuf_latch_bcb_upon_fix () -
 *   return: NO_ERROR, or ER_code
 *   bufptr(in):
 *   request_mode(in):
 *   buf_lock_acquired(in):
 *   condition(in):
 *
 * Note: This function latches BCB with latch mode LatchMode as far as
 *       LatchMode is compatible with bcb->LatchMode and there is not any
 *       blocked reader or writer.
 *       If it cannot latch the BCB right away,
 *           (1) in case of conditional request,
 *               release mutex and return eERROR.
 *           (2) in case of unconditional request, add thread on the
 *               BCB queue and release mutex and block the thread.
 *       In any case, if LeafLatchMode is not NO_LATCH and the PageType
 *       of the page that BCB points is P_BPLEAF, latch BCB with latch
 *       mode LeafLatchMode.
 */
#if !defined(NDEBUG)
static int
pgbuf_latch_bcb_upon_fix (THREAD_ENTRY * thread_p, PGBUF_BCB * bufptr,
			  int request_mode, int buf_lock_acquired,
			  PGBUF_LATCH_CONDITION condition,
			  const char *caller_file, int caller_line)
#else /* NDEBUG */
static int
pgbuf_latch_bcb_upon_fix (THREAD_ENTRY * thread_p, PGBUF_BCB * bufptr,
			  int request_mode, int buf_lock_acquired,
			  PGBUF_LATCH_CONDITION condition)
#endif				/* NDEBUG */
{
#if defined(SERVER_MODE)
  THREAD_ENTRY *victim_thrd_entry;
#endif /* SERVER_MODE */
  PGBUF_HOLDER *holder = NULL;
  int request_fcnt = 1;
#if defined(SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */

  /* parameter validation */
  assert (request_mode == PGBUF_LATCH_READ
	  || request_mode == PGBUF_LATCH_WRITE);
  assert (condition == PGBUF_UNCONDITIONAL_LATCH
	  || condition == PGBUF_CONDITIONAL_LATCH);

  /* the caller is holding bufptr->BCB_mutex */
  if (buf_lock_acquired || bufptr->latch_mode == PGBUF_NO_LATCH)
    {
      bufptr->latch_mode = request_mode;
      bufptr->fcnt = 1;

      pthread_mutex_unlock (&bufptr->BCB_mutex);

      /* allocate a BCB holder entry */

      assert (pgbuf_find_thrd_holder (thread_p, bufptr) == NULL);

      holder = pgbuf_allocate_thrd_holder_entry (thread_p);
      if (holder == NULL)
	{
	  /* This situation must not be occurred. */
	  assert (false);
	  return ER_FAILED;
	}

      holder->fix_count = 1;
      holder->bufptr = bufptr;
#if !defined(NDEBUG)
      sprintf (holder->fixed_at, "%s:%d ", caller_file, caller_line);
      holder->fixed_at_size = strlen (holder->fixed_at);
#endif /* NDEBUG */

      return NO_ERROR;
    }

  if ((request_mode == PGBUF_LATCH_READ)
      && (bufptr->latch_mode == PGBUF_LATCH_READ
	  || bufptr->latch_mode == PGBUF_LATCH_FLUSH
	  || bufptr->latch_mode == PGBUF_LATCH_VICTIM))
    {
      assert (bufptr->latch_mode != PGBUF_LATCH_VICTIM);	/* TODO */
      assert (bufptr->latch_mode != PGBUF_LATCH_VICTIM_INVALID);

      if (pgbuf_is_exist_blocked_reader_writer (bufptr) == false)
	{
	  /* there is not any blocked reader/writer. */
	  /* grant the request */
#if defined(SERVER_MODE)
	  if (bufptr->latch_mode == PGBUF_LATCH_FLUSH
	      && bufptr->next_wait_thrd != NULL)
	    {
	      /* early wakeup of any blocked victim selector */
	      victim_thrd_entry =
		pgbuf_kickoff_blocked_victim_request (bufptr);
	      if (victim_thrd_entry != NULL)
		{
		  pgbuf_wakeup_uncond (victim_thrd_entry);
		}
	    }
#endif /* SERVER_MODE */

	  /* increment the fix count */
	  bufptr->fcnt += 1;

	  pthread_mutex_unlock (&bufptr->BCB_mutex);

	  /* allocate a BCB holder entry */

	  holder = pgbuf_find_thrd_holder (thread_p, bufptr);
	  if (holder != NULL)
	    {
	      /* the caller is the holder of the buffer page */
	      holder->fix_count += 1;
#if !defined(NDEBUG)
	      pgbuf_add_fixed_at (holder, caller_file, caller_line);
#endif /* NDEBUG */
	    }
#if defined(SERVER_MODE)
	  else
	    {
	      /* the caller is not the holder of the buffer page */
	      /* allocate a BCB holder entry */
	      holder = pgbuf_allocate_thrd_holder_entry (thread_p);
	      if (holder == NULL)
		{
		  /* This situation must not be occurred. */
		  assert (false);
		  return ER_FAILED;
		}

	      holder->fix_count = 1;
	      holder->bufptr = bufptr;
#if !defined(NDEBUG)
	      sprintf (holder->fixed_at, "%s:%d ", caller_file, caller_line);
	      holder->fixed_at_size = strlen (holder->fixed_at);
#endif /* NDEBUG */
	    }
#endif /* SERVER_MODE */

	  return NO_ERROR;
	}

      /* at here, there is some blocked reader/writer. */

      holder = pgbuf_find_thrd_holder (thread_p, bufptr);
      if (holder == NULL)
	{
	  /* in case that the caller is not the holder */
	  goto do_block;
	}

      /* in case that the caller is the holder */
      bufptr->fcnt += 1;

      pthread_mutex_unlock (&bufptr->BCB_mutex);

      /* set BCB holder entry */

      holder->fix_count += 1;
#if !defined(NDEBUG)
      pgbuf_add_fixed_at (holder, caller_file, caller_line);
#endif /* NDEBUG */

      return NO_ERROR;
    }

  holder = pgbuf_find_thrd_holder (thread_p, bufptr);
  if (holder == NULL)
    {
      /* in case that the caller is not the holder */
      goto do_block;
    }

  /* in case that the caller is holder */

  if (bufptr->latch_mode == PGBUF_LATCH_WRITE)
    {				/* only the holder */
      assert (bufptr->fcnt == holder->fix_count);

      bufptr->fcnt += 1;

      pthread_mutex_unlock (&bufptr->BCB_mutex);

      /* set BCB holder entry */

      holder->fix_count += 1;
#if !defined(NDEBUG)
      pgbuf_add_fixed_at (holder, caller_file, caller_line);
#endif /* NDEBUG */

      return NO_ERROR;
    }
  else if (bufptr->latch_mode == PGBUF_LATCH_READ)
    {
#if 0				/* TODO: do not delete me */
      assert (false);
#endif

      assert (request_mode == PGBUF_LATCH_WRITE);

      if (bufptr->fcnt == holder->fix_count)
	{
	  bufptr->latch_mode = request_mode;	/* PGBUF_LATCH_WRITE */
	  bufptr->fcnt += 1;

	  pthread_mutex_unlock (&bufptr->BCB_mutex);

	  /* set BCB holder entry */

	  holder->fix_count += 1;
#if !defined(NDEBUG)
	  pgbuf_add_fixed_at (holder, caller_file, caller_line);
#endif /* NDEBUG */

	  return NO_ERROR;
	}

      assert (bufptr->fcnt > holder->fix_count);

      if (condition == PGBUF_CONDITIONAL_LATCH)
	{
	  goto do_block;	/* will return immediately */
	}

      assert (request_fcnt == 1);

      request_fcnt += holder->fix_count;
      bufptr->fcnt -= holder->fix_count;
      holder->fix_count = 0;

      if (pgbuf_remove_thrd_holder (thread_p, holder) != NO_ERROR)
	{
	  /* This situation must not be occurred. */
	  assert (false);

	  pthread_mutex_unlock (&bufptr->BCB_mutex);

	  return ER_FAILED;
	}

      /* at here, goto do_block; */
    }
  else
    {
#if 0				/* TODO: do not delete me */
      assert (false);
#endif

      /* at here, goto do_block; */
    }

do_block:

  if (condition == PGBUF_CONDITIONAL_LATCH)
    {
      /* reject the request */
      int tran_index;
      int wait_msec;

      tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
      wait_msec = logtb_find_wait_msecs (tran_index);

      if (wait_msec == LK_ZERO_WAIT)
	{
	  char *client_prog_name;	/* Client program name for tran */
	  char *client_user_name;	/* Client user name for tran */
	  char *client_host_name;	/* Client host for tran */
	  int client_pid;	/* Client process identifier for tran */

	  /* setup timeout error, if wait_msec == LK_ZERO_WAIT */

	  pthread_mutex_unlock (&bufptr->BCB_mutex);

	  (void) logtb_find_client_name_host_pid (tran_index,
						  &client_prog_name,
						  &client_user_name,
						  &client_host_name,
						  &client_pid);

	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LK_PAGE_TIMEOUT, 8,
		  tran_index, client_user_name, client_host_name,
		  client_pid,
		  (request_mode == PGBUF_LATCH_READ ? "READ" : "WRITE"),
		  bufptr->vpid.volid, bufptr->vpid.pageid, NULL);
	}
      else
	{
	  pthread_mutex_unlock (&bufptr->BCB_mutex);
	}

      return ER_FAILED;
    }
  else
    {
      /* block the request */
#if defined(SERVER_MODE)
      if (bufptr->latch_mode == PGBUF_LATCH_FLUSH
	  && bufptr->next_wait_thrd != NULL)
	{
	  /* early wakeup of any blocked victim selector */
	  victim_thrd_entry = pgbuf_kickoff_blocked_victim_request (bufptr);
	  if (victim_thrd_entry != NULL)
	    {
	      pgbuf_wakeup_uncond (victim_thrd_entry);
	    }
	}
#endif /* SERVER_MODE */

#if !defined(NDEBUG)
      if (pgbuf_block_bcb (thread_p, bufptr, request_mode, request_fcnt,
			   caller_file, caller_line) != NO_ERROR)
#else /* NDEBUG */
      if (pgbuf_block_bcb (thread_p, bufptr, request_mode, request_fcnt) !=
	  NO_ERROR)
#endif /* NDEBUG */
	{
	  return ER_FAILED;
	}
      /* Above function released bufptr->BCB_mutex unconditionally */

      assert (pgbuf_find_thrd_holder (thread_p, bufptr) == NULL);

      holder = pgbuf_allocate_thrd_holder_entry (thread_p);
      if (holder == NULL)
	{
	  /* This situation must not be occurred. */
	  assert (false);
	  return ER_FAILED;
	}

      /* set BCB holder entry */

      holder->fix_count = request_fcnt;
      holder->bufptr = bufptr;
#if !defined(NDEBUG)
      sprintf (holder->fixed_at, "%s:%d ", caller_file, caller_line);
      holder->fixed_at_size = strlen (holder->fixed_at);
#endif /* NDEBUG */

      return NO_ERROR;
    }
}

/*
 * pgbuf_unlatch_bcb_upon_unfix () - Unlatches BCB
 *   return: NO_ERROR, or ER_code
 *   bufptr(in):
 *
 * Note: It decrements FixCount by one.
 *       If FixCount becomes 0,
 *            (1) if LatchMode != FLUSH and LatchMode != VICTIM,
 *                set LatchMode = NO_LATCH.
 *            (2) if BCB waiting queue is empty and Wait is false,
 *                replace the BCB to the top of LRU list.
 *       If Flush_Request == TRUE,
 *            set LatchMode = FLUSH,
 *            flush the buffer by WAL protocol and wake up
 *            threads on the BCB waiting queue.
 *       If Flush_Request == FALSE
 *            if LatchMode == NO_LATCH,
 *            then, wake up the threads on the BCB waiting queue.
 *       Before return, it releases BCB mutex.
 */
#if !defined(NDEBUG)
static int
pgbuf_unlatch_bcb_upon_unfix (THREAD_ENTRY * thread_p, PGBUF_BCB * bufptr,
			      int holder_status,
			      const char *caller_file, int caller_line)
#else /* NDEBUG */
static int
pgbuf_unlatch_bcb_upon_unfix (THREAD_ENTRY * thread_p, PGBUF_BCB * bufptr,
			      int holder_status)
#endif				/* NDEBUG */
{
  PAGE_PTR pgptr;
  int ain_age;

  assert (holder_status == NO_ERROR);

  /* the caller is holding bufptr->BCB_mutex */

  assert (!VPID_ISNULL (&bufptr->vpid));
  assert (pgbuf_check_bcb_page_vpid (thread_p, bufptr) == true);

  CAST_BFPTR_TO_PGPTR (pgptr, bufptr);

  /* decrement the fix count */
  bufptr->fcnt--;
  if (bufptr->fcnt < 0)
    {
      /* This situation must not be occurred. */
      assert (false);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_PB_UNFIXED_PAGEPTR, 3,
	      pgptr, bufptr->vpid.pageid,
	      fileio_get_volume_label (bufptr->vpid.volid, PEEK));
      bufptr->fcnt = 0;
    }

#if !defined(NDEBUG)
  thread_rc_track_meter (thread_p, caller_file, caller_line, -1, pgptr,
			 RC_PGBUF, MGR_DEF);
  if (pgbuf_is_lsa_temporary (pgptr))
    {
      /* for the defense of add vol */
      if (thread_rc_track_amount_pgbuf_temp (thread_p) > 0)
	{
	  thread_rc_track_meter (thread_p, caller_file, caller_line, -1,
				 pgptr, RC_PGBUF_TEMP, MGR_DEF);
	}
    }
#endif /* NDEBUG */

  if (holder_status != NO_ERROR)
    {
      /* This situation must not be occurred. */
      assert (false);
      return ER_FAILED;
    }

  if (bufptr->fcnt == 0)
    {
      assert (bufptr->latch_mode != PGBUF_LATCH_VICTIM);
      assert (bufptr->latch_mode != PGBUF_LATCH_VICTIM_INVALID);

      /* there could be some synchronous flushers on the BCB queue */
      /* When the page buffer in LRU_1_Zone,
       * do not move the page buffer into the top of LRU.
       * This is an intention for performance.
       */
      if (pgbuf_is_exist_blocked_reader_writer (bufptr) == false)
	{
	  switch (bufptr->zone)
	    {
	    case PGBUF_LRU_1_ZONE:
	      /* do nothing in this case */
	      break;

	    case PGBUF_AIN_ZONE:
	      ain_age = pgbuf_Pool.buf_AIN_list.tick - bufptr->ain_tick;
	      ain_age = (ain_age > 0) ? (ain_age) : (DB_INT32_MAX);

	      if (ain_age > pgbuf_Pool.buf_AIN_list.max_count / 2)
		{
		  /* Correlated references are contrainted to half of the AIN
		   * list. If a page in AIN is referenced again and is older
		   * than half the AIN size, we consider it hot and relocate
		   * it to LRU.
		   * This aims to avoid discarding soon-to-become hot pages:
		   * in classic 2Q, a page is discarded from AIN only when
		   * removed from page buffer, it becomes hot page (put in
		   * LRU) only after is loaded a second time and still resides
		   * in AOUT list.
		   */
		  pgbuf_move_from_ain_to_lru (bufptr);
		}
	      break;

	    case PGBUF_VOID_ZONE:
	      if (!PGBUF_IS_2Q_ENABLED
		  || pgbuf_remove_vpid_from_aout_list (thread_p,
						       &bufptr->vpid))
		{
		  /* Put BCB in "middle" of LRU if VPID is in Aout list or is
		   * the first reference and 2Q is disabled.
		   * The page is not yet hot.
		   */
		  pgbuf_relocate_top_lru (bufptr, PGBUF_LRU_2_ZONE);
		}
	      else
		{
		  /* 2Q enabled and is first time the page is referenced:
		   * we put it in AIN to filter out correlated references */
		  pgbuf_relocate_top_ain (bufptr);
		}
	      break;

	    case PGBUF_LRU_2_ZONE:
	      (void) pgbuf_relocate_top_lru (bufptr, PGBUF_LRU_1_ZONE);
	      break;

	    default:
	      assert (false);
	      break;
	    }
	}

      if (bufptr->latch_mode != PGBUF_LATCH_FLUSH
	  && bufptr->latch_mode != PGBUF_LATCH_VICTIM)
	{
	  bufptr->latch_mode = PGBUF_NO_LATCH;
	}
    }

  assert (bufptr->latch_mode != PGBUF_LATCH_VICTIM);
  assert (bufptr->latch_mode != PGBUF_LATCH_VICTIM_INVALID);
  /* bufptr->latch_mode == PGBUF_NO_LATCH, PGBUF_LATCH_READ,
   *                       PGBUF_LATCH_FLUSH, PGBUF_LATCH_VICTIM
   */
  if (bufptr->async_flush_request == true)
    {
      /* Note that bufptr->async_flush_request is set
       * only when an asynchronous flusher has requested the BCB
       * while the latch_mode is PGBUF_LATCH_WRITE.
       * At this point, bufptr->latch_mode is PGBUF_NO_LATCH
       */
      bufptr->latch_mode = PGBUF_LATCH_FLUSH;
      if (pgbuf_flush_page_with_wal (thread_p, bufptr) != NO_ERROR)
	{
	  pthread_mutex_unlock (&bufptr->BCB_mutex);

	  return ER_FAILED;
	}

      if (bufptr->fcnt == 0)
	{
	  bufptr->latch_mode = PGBUF_NO_LATCH;
	}
      else
	{
	  bufptr->latch_mode = PGBUF_LATCH_READ;
	}
    }

  if (bufptr->latch_mode == PGBUF_NO_LATCH)
    {
#if defined(SERVER_MODE)
      if (bufptr->next_wait_thrd != NULL)
	{
#if !defined(NDEBUG)
	  if (pgbuf_wakeup_bcb (thread_p, bufptr,
				caller_file, caller_line) != NO_ERROR)
#else /* NDEBUG */
	  if (pgbuf_wakeup_bcb (thread_p, bufptr) != NO_ERROR)
#endif /* NDEBUG */
	    {
	      return ER_FAILED;
	    }
	  /* Above function released bufptr->BCB_mutex unconditionally. */
	}
      else
	{
	  pthread_mutex_unlock (&bufptr->BCB_mutex);
	}
#else /* SERVER_MODE */
      pthread_mutex_unlock (&bufptr->BCB_mutex);
#endif /* SERVER_MODE */
    }
  else
    {
      pthread_mutex_unlock (&bufptr->BCB_mutex);
    }

  return NO_ERROR;
}

/*
 * pgbuf_block_bcb () - Adds it on the BCB waiting queue and block thread
 *   return: NO_ERROR, or ER_code
 *   bufptr(in):
 *   request_mode(in):
 *   request_fcnt(in):
 *
 * Note: If latch_mode == BT_LT_FLUSH, then put the thread to the top of
 *       the BCB queue else append it to the BCB queue. Before return, it
 *       releases the BCB mutex.
 */
#if !defined(NDEBUG)
static int
pgbuf_block_bcb (THREAD_ENTRY * thread_p, PGBUF_BCB * bufptr,
		 int request_mode, int request_fcnt,
		 const char *caller_file, int caller_line)
#else /* NDEBUG */
static int
pgbuf_block_bcb (THREAD_ENTRY * thread_p, PGBUF_BCB * bufptr,
		 int request_mode, int request_fcnt)
#endif				/* NDEBUG */
{
#if defined(SERVER_MODE)
  THREAD_ENTRY *cur_thrd_entry, *thrd_entry;
  int rv;

  /* caller is holding bufptr->BCB_mutex */

  assert (bufptr->latch_mode != PGBUF_LATCH_VICTIM);
  assert (bufptr->latch_mode != PGBUF_LATCH_VICTIM_INVALID);
  assert (request_mode != PGBUF_LATCH_VICTIM);
  /* request_mode == PGBUF_LATCH_READ/PGBUF_LATCH_WRITE/PGBUF_LATCH_FLUSH/PGBUF_LATCH_VICTIM */

  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  cur_thrd_entry = thread_p;
  cur_thrd_entry->request_latch_mode = request_mode;
  cur_thrd_entry->request_fix_count = request_fcnt;	/* SPECIAL_NOTE */

  if (request_mode == PGBUF_LATCH_FLUSH)
    {
      /* put cur_thrd_entry to the top of the BCB waiting queue */
      cur_thrd_entry->next_wait_thrd = bufptr->next_wait_thrd;
      bufptr->next_wait_thrd = cur_thrd_entry;
    }
  else
    {
      /* append cur_thrd_entry to the BCB waiting queue */
      cur_thrd_entry->next_wait_thrd = NULL;
      thrd_entry = bufptr->next_wait_thrd;
      if (thrd_entry == NULL)
	{
	  bufptr->next_wait_thrd = cur_thrd_entry;
	}
      else
	{
	  while (thrd_entry->next_wait_thrd != NULL)
	    {
	      thrd_entry = thrd_entry->next_wait_thrd;
	    }
	  thrd_entry->next_wait_thrd = cur_thrd_entry;
	}
    }

  if (request_mode == PGBUF_LATCH_FLUSH || request_mode == PGBUF_LATCH_VICTIM)
    {
      pgbuf_sleep (cur_thrd_entry, &bufptr->BCB_mutex);

      if (cur_thrd_entry->resume_status != THREAD_PGBUF_RESUMED)
	{
	  /* interrupt operation */
	  THREAD_ENTRY *thrd_entry, *prev_thrd_entry = NULL;

	  MUTEX_LOCK_VIA_BUSY_WAIT (rv, bufptr->BCB_mutex);
	  thrd_entry = bufptr->next_wait_thrd;

	  while (thrd_entry != NULL)
	    {
	      if (thrd_entry == cur_thrd_entry)
		{
		  if (prev_thrd_entry == NULL)
		    {
		      bufptr->next_wait_thrd = thrd_entry->next_wait_thrd;
		    }
		  else
		    {
		      prev_thrd_entry->next_wait_thrd =
			thrd_entry->next_wait_thrd;
		    }

		  thrd_entry->next_wait_thrd = NULL;
		  pthread_mutex_unlock (&bufptr->BCB_mutex);
		  return ER_FAILED;
		}

	      prev_thrd_entry = thrd_entry;
	      thrd_entry = thrd_entry->next_wait_thrd;
	    }
	  pthread_mutex_unlock (&bufptr->BCB_mutex);
	}
    }
  else
    {
      /*
       * We do not quarantee that there is no deadlock between page latches.
       * So, we made a decision that when read/write buffer fix request is
       * not granted immediately, block the request with timed sleep method.
       * That is, unless the request is not waken up by other threads within
       * some time interval, the request will be waken up by timeout.
       * When the request is waken up, the request is treated as a victim.
       */
#if !defined(NDEBUG)
      if (pgbuf_timed_sleep (thread_p, bufptr, cur_thrd_entry,
			     caller_file, caller_line) != NO_ERROR)
#else /* NDEBUG */
      if (pgbuf_timed_sleep (thread_p, bufptr, cur_thrd_entry) != NO_ERROR)
#endif /* NDEBUG */
	{
	  return ER_FAILED;
	}
    }
#endif /* SERVER_MODE */

  return NO_ERROR;
}

#if defined(SERVER_MODE)
/*
 * pgbuf_timed_sleep_error_handling () -
 *   return:
 *   bufptr(in):
 *   thrd_entry(in):
 */
#if !defined(NDEBUG)
static int
pgbuf_timed_sleep_error_handling (THREAD_ENTRY * thread_p,
				  PGBUF_BCB * bufptr,
				  THREAD_ENTRY * thrd_entry,
				  const char *caller_file, int caller_line)
#else /* NDEBUG */
static int
pgbuf_timed_sleep_error_handling (THREAD_ENTRY * thread_p,
				  PGBUF_BCB * bufptr,
				  THREAD_ENTRY * thrd_entry)
#endif				/* NDEBUG */
{
  THREAD_ENTRY *prev_thrd_entry;
  THREAD_ENTRY *curr_thrd_entry;
  PGBUF_HOLDER *holder;
#if defined(SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */

  MUTEX_LOCK_VIA_BUSY_WAIT (rv, bufptr->BCB_mutex);

  /* case 1 : empty waiting queue */
  if (bufptr->next_wait_thrd == NULL)
    {
      /* The thread entry has been alredy removed from
         the BCB waiting queue by another thread. */
      return NO_ERROR;
    }

  /* case 2 : first waiting thread != thrd_entry */
  if (bufptr->next_wait_thrd != thrd_entry)
    {
      prev_thrd_entry = bufptr->next_wait_thrd;
      while (prev_thrd_entry->next_wait_thrd != NULL)
	{
	  if (prev_thrd_entry->next_wait_thrd == thrd_entry)
	    {
	      prev_thrd_entry->next_wait_thrd = thrd_entry->next_wait_thrd;
	      thrd_entry->next_wait_thrd = NULL;
	      break;
	    }
	  prev_thrd_entry = prev_thrd_entry->next_wait_thrd;
	}
      return NO_ERROR;
    }

  /* case 3 : first waiting thread == thrd_entry */
  bufptr->next_wait_thrd = thrd_entry->next_wait_thrd;
  thrd_entry->next_wait_thrd = NULL;
  while (bufptr->next_wait_thrd != NULL)
    {
      curr_thrd_entry = bufptr->next_wait_thrd;
      if (bufptr->latch_mode == PGBUF_LATCH_READ
	  && curr_thrd_entry->request_latch_mode == PGBUF_LATCH_READ)
	{
	  /* grant the request */
	  thread_lock_entry (curr_thrd_entry);
	  if (curr_thrd_entry->request_latch_mode == PGBUF_LATCH_READ)
	    {
	      bufptr->fcnt += curr_thrd_entry->request_fix_count;

	      /* do not handle BCB holder entry, at here.
	       * refer pgbuf_latch_bcb_upon_fix ()
	       */

	      /* remove thrd_entry from BCB waiting queue. */
	      bufptr->next_wait_thrd = curr_thrd_entry->next_wait_thrd;
	      curr_thrd_entry->next_wait_thrd = NULL;

	      /* wake up the thread */
	      pgbuf_wakeup (curr_thrd_entry);
	    }
	  else
	    {
	      thread_unlock_entry (curr_thrd_entry);
	      break;
	    }
	}
      else
	{
	  break;
	}
    }

  return NO_ERROR;
}

/*
 * pgbuf_timed_sleep () -
 *   return: NO_ERROR, or ER_code
 *   bufptr(in):
 *   thrd_entry(in):
 */
#if !defined(NDEBUG)
static int
pgbuf_timed_sleep (THREAD_ENTRY * thread_p, PGBUF_BCB * bufptr,
		   THREAD_ENTRY * thrd_entry,
		   const char *caller_file, int caller_line)
#else /* NDEBUG */
static int
pgbuf_timed_sleep (THREAD_ENTRY * thread_p, PGBUF_BCB * bufptr,
		   THREAD_ENTRY * thrd_entry)
#endif				/* NDEBUG */
{
  int r;
  struct timespec to;
  int wait_secs;
  int old_wait_msecs;
  int save_request_latch_mode;
  char *client_prog_name;	/* Client program name for trans */
  char *client_user_name;	/* Client user name for tran */
  char *client_host_name;	/* Client host for tran */
  int client_pid;		/* Client process identifier for tran */

  TSC_TICKS start_tick, end_tick;
  TSCTIMEVAL tv_diff;

  /* After holding the mutex associated with conditional variable,
     release the bufptr->BCB_mutex. */
  thread_lock_entry (thrd_entry);
  pthread_mutex_unlock (&bufptr->BCB_mutex);

  old_wait_msecs = wait_secs = logtb_find_current_wait_msecs (thread_p);

  assert (wait_secs == LK_INFINITE_WAIT || wait_secs == LK_ZERO_WAIT
	  || wait_secs == LK_FORCE_ZERO_WAIT || wait_secs > 0);

  if (wait_secs == LK_ZERO_WAIT || wait_secs == LK_FORCE_ZERO_WAIT)
    {
      wait_secs = 0;
    }
  else
    {
      wait_secs = PGBUF_TIMEOUT;
    }

try_again:
  to.tv_sec = time (NULL) + wait_secs;
  to.tv_nsec = 0;

  if (thrd_entry->event_stats.trace_slow_query == true)
    {
      tsc_getticks (&start_tick);
    }

  thrd_entry->resume_status = THREAD_PGBUF_SUSPENDED;
  r = pthread_cond_timedwait (&thrd_entry->wakeup_cond,
			      &thrd_entry->th_entry_lock, &to);

  if (thrd_entry->event_stats.trace_slow_query == true)
    {
      tsc_getticks (&end_tick);
      tsc_elapsed_time_usec (&tv_diff, end_tick, start_tick);
      TSC_ADD_TIMEVAL (thrd_entry->event_stats.latch_waits, tv_diff);
    }

  if (r == 0)
    {
      /* someone wakes up me */
      if (thrd_entry->resume_status == THREAD_PGBUF_RESUMED)
	{
	  thread_unlock_entry (thrd_entry);
	  return NO_ERROR;
	}

      /* interrupt operation */
      thrd_entry->request_latch_mode = PGBUF_NO_LATCH;
      thrd_entry->resume_status = THREAD_PGBUF_RESUMED;
      thread_unlock_entry (thrd_entry);

#if !defined(NDEBUG)
      if (pgbuf_timed_sleep_error_handling (thread_p, bufptr, thrd_entry,
					    caller_file, caller_line)
	  == NO_ERROR)
#else /* NDEBUG */
      if (pgbuf_timed_sleep_error_handling (thread_p, bufptr, thrd_entry) ==
	  NO_ERROR)
#endif /* NDEBUG */
	{
	  pthread_mutex_unlock (&bufptr->BCB_mutex);
	}

      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INTERRUPTED, 0);
      return ER_FAILED;
    }
  else if (r == ETIMEDOUT)
    {
      /* rollback operation, postpone operation, etc. */
      if (thrd_entry->resume_status == THREAD_PGBUF_RESUMED)
	{
	  thread_unlock_entry (thrd_entry);
	  return NO_ERROR;
	}

      if (logtb_is_current_active (thread_p) == false)
	{
	  goto try_again;
	}

      /* buffer page deadlock victim by timeout */
      /* following order of execution is important. */
      /* request_latch_mode == PGBUF_NO_LATCH means
         that the thread has waken up by timeout.
         This value must be set before release the mutex.
       */
      save_request_latch_mode = thrd_entry->request_latch_mode;
      thrd_entry->request_latch_mode = PGBUF_NO_LATCH;
      thread_unlock_entry (thrd_entry);

#if !defined(NDEBUG)
      if (pgbuf_timed_sleep_error_handling (thread_p, bufptr, thrd_entry,
					    caller_file, caller_line) ==
	  NO_ERROR)
#else /* NDEBUG */
      if (pgbuf_timed_sleep_error_handling (thread_p, bufptr, thrd_entry) ==
	  NO_ERROR)
#endif /* NDEBUG */
	{
	  goto er_set_return;
	}

      return ER_FAILED;
    }
  else
    {
      thread_unlock_entry (thrd_entry);
      /* error setting */
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_CSS_PTHREAD_COND_TIMEDWAIT, 0);
      return ER_FAILED;
    }

er_set_return:
  /* error setting */
  if (old_wait_msecs == LK_INFINITE_WAIT)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_PAGE_LATCH_TIMEDOUT, 2, bufptr->vpid.volid,
	      bufptr->vpid.pageid);

      pthread_mutex_unlock (&bufptr->BCB_mutex);
      if (logtb_is_current_active (thread_p) == true)
	{
	  char *client_prog_name;	/* Client user name for transaction */
	  char *client_user_name;	/* Client user name for transaction */
	  char *client_host_name;	/* Client host for transaction */
	  int client_pid;	/* Client process identifier for transaction */
	  int tran_index;

	  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
	  (void) logtb_find_client_name_host_pid (tran_index,
						  &client_prog_name,
						  &client_user_name,
						  &client_host_name,
						  &client_pid);

	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_LK_UNILATERALLY_ABORTED, 4,
		  tran_index, client_user_name, client_host_name, client_pid);
	}
      else
	{
	  /*
	   * We are already aborting, fall through. Don't do
	   * double aborts that could cause an infinite loop.
	   */
	  er_log_debug (ARG_FILE_LINE,
			"pgbuf_timed_sleep: Likely a system error."
			"Trying to abort a transaction twice.\n");
	  /* We can release all the page latches held by current thread. */
	}
    }
  else if (old_wait_msecs > 0)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_PAGE_LATCH_TIMEDOUT, 2, bufptr->vpid.volid,
	      bufptr->vpid.pageid);

      pthread_mutex_unlock (&bufptr->BCB_mutex);

      (void) logtb_find_client_name_host_pid (thrd_entry->tran_index,
					      &client_prog_name,
					      &client_user_name,
					      &client_host_name, &client_pid);

      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LK_PAGE_TIMEOUT, 8,
	      thrd_entry->tran_index, client_user_name, client_host_name,
	      client_pid,
	      (save_request_latch_mode ==
	       PGBUF_LATCH_READ ? "READ" : "WRITE"), bufptr->vpid.volid,
	      bufptr->vpid.pageid, NULL);
    }
  else
    {
      pthread_mutex_unlock (&bufptr->BCB_mutex);
    }

  return ER_FAILED;
}

/*
 * pgbuf_wakeup_bcb () - Wakes up blocked threads on the BCB queue
 *   return: NO_ERROR, or ER_code
 *   bufptr(in):
 */
#if !defined(NDEBUG)
static int
pgbuf_wakeup_bcb (THREAD_ENTRY * thread_p, PGBUF_BCB * bufptr,
		  const char *caller_file, int caller_line)
#else /* NDEBUG */
static int
pgbuf_wakeup_bcb (THREAD_ENTRY * thread_p, PGBUF_BCB * bufptr)
#endif				/* NDEBUG */
{
  THREAD_ENTRY *thrd_entry;

  /* the caller is holding bufptr->BCB_mutex */

  /* fcnt == 0, bufptr->latch_mode == PGBUF_NO_LATCH/PGBUF_LATCH_FLUSH_INVALID */
  /* there cannot be any blocked flusher */
  assert (bufptr->latch_mode != PGBUF_LATCH_VICTIM);
  assert (bufptr->latch_mode != PGBUF_LATCH_VICTIM_INVALID);

  thrd_entry = bufptr->next_wait_thrd;
  if (thrd_entry->request_latch_mode == PGBUF_LATCH_VICTIM)
    {
      assert (false);

      thrd_entry = bufptr->next_wait_thrd;
      bufptr->next_wait_thrd = thrd_entry->next_wait_thrd;
      thrd_entry->next_wait_thrd = NULL;
      bufptr->latch_mode = PGBUF_LATCH_VICTIM;

      /* wake up the thread */
      (void) thread_lock_entry (thrd_entry);
      pgbuf_wakeup (thrd_entry);
    }
  else
    {
      /* PGBUF_NO_LATCH => PGBUF_LATCH_READ, PGBUF_LATCH_WRITE
         there cannot be any blocked PGBUF_LATCH_FLUSH, PGBUF_LATCH_VICTIM */
      while ((thrd_entry = bufptr->next_wait_thrd) != NULL)
	{
	  /* if thrd_entry->request_latch_mode is PGBUF_NO_LATCH,
	     it means the corresponding thread has been waken up by timeout. */
	  if (thrd_entry->request_latch_mode == PGBUF_NO_LATCH)
	    {
	      bufptr->next_wait_thrd = thrd_entry->next_wait_thrd;
	      thrd_entry->next_wait_thrd = NULL;
	      continue;
	    }

	  if ((bufptr->latch_mode == PGBUF_NO_LATCH)
	      || (bufptr->latch_mode == PGBUF_LATCH_READ
		  && thrd_entry->request_latch_mode == PGBUF_LATCH_READ))
	    {

	      (void) thread_lock_entry (thrd_entry);
	      if (thrd_entry->request_latch_mode != PGBUF_NO_LATCH)
		{
		  /* grant the request */
		  bufptr->latch_mode = thrd_entry->request_latch_mode;
		  bufptr->fcnt += thrd_entry->request_fix_count;

		  /* do not handle BCB holder entry, at here.
		   * refer pgbuf_latch_bcb_upon_fix ()
		   */

		  /* remove thrd_entry from BCB waiting queue. */
		  bufptr->next_wait_thrd = thrd_entry->next_wait_thrd;
		  thrd_entry->next_wait_thrd = NULL;

		  /* wake up the thread */
		  pgbuf_wakeup (thrd_entry);
		}
	      else
		{
		  (void) thread_unlock_entry (thrd_entry);
		}
	    }
	  else
	    {
	      break;
	    }
	}
    }

  pthread_mutex_unlock (&bufptr->BCB_mutex);

  /* at this point, the caller does not hold bufptr->BCB_mutex */
  return NO_ERROR;
}
#endif /* SERVER_MODE */

/*
 * pgbuf_search_hash_chain () - searches the buffer hash chain to find
 *				a BCB with page identifier
 *   return: if success, BCB pointer, otherwise NULL
 *   hash_anchor(in):
 *   vpid(in):
 */
static PGBUF_BCB *
pgbuf_search_hash_chain (PGBUF_BUFFER_HASH * hash_anchor, const VPID * vpid)
{
  PGBUF_BCB *bufptr;
  int mbw_cnt;
#if defined(SERVER_MODE)
  int rv;
  int loop_cnt;
#endif

  mbw_cnt = prm_get_integer_value (PRM_ID_MUTEX_BUSY_WAITING_CNT);

/* one_phase: no hash-chain mutex */
one_phase:

  bufptr = hash_anchor->hash_next;
  while (bufptr != NULL)
    {
      if (VPID_EQ (&(bufptr->vpid), vpid))
	{
#if defined(SERVER_MODE)
	  loop_cnt = 0;

	mutex_lock:

	  rv = pthread_mutex_trylock (&bufptr->BCB_mutex);
	  if (rv == 0)
	    {
	      ;			/* OK. go ahead */
	    }
	  else
	    {
	      if (rv != EBUSY)
		{
		  /* give up one_phase */
		  goto two_phase;
		}

	      if (loop_cnt++ < mbw_cnt)
		{
		  goto mutex_lock;
		}

	      /* An unconditional request is given for acquiring BCB_mutex */
	      rv = pthread_mutex_lock (&bufptr->BCB_mutex);
	    }
#else /* SERVER_MODE */
	  rv = pthread_mutex_lock (&bufptr->BCB_mutex);
#endif /* SERVER_MODE */

	  if (!VPID_EQ (&(bufptr->vpid), vpid))
	    {
	      /* updated or replaced */
	      pthread_mutex_unlock (&bufptr->BCB_mutex);
	      /* retry one_phase */
	      goto one_phase;
	    }
	  break;
	}
      bufptr = bufptr->hash_next;
    }

  if (bufptr != NULL)
    {
      return bufptr;
    }

#if defined(SERVER_MODE)
/* two_phase: hold hash-chain mutex */
two_phase:
#endif

try_again:

  rv = pthread_mutex_lock (&hash_anchor->hash_mutex);
  bufptr = hash_anchor->hash_next;
  while (bufptr != NULL)
    {
      if (VPID_EQ (&(bufptr->vpid), vpid))
	{
#if defined(SERVER_MODE)
	  loop_cnt = 0;

	mutex_lock2:

	  rv = pthread_mutex_trylock (&bufptr->BCB_mutex);
	  if (rv == 0)
	    {
	      /* bufptr->BCB_mutex is held */
	      pthread_mutex_unlock (&hash_anchor->hash_mutex);
	    }
	  else
	    {
	      if (rv != EBUSY)
		{
		  er_set_with_oserror (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
				       ER_CSS_PTHREAD_MUTEX_TRYLOCK, 0);
		  return NULL;
		}

	      if (loop_cnt++ < mbw_cnt)
		{
		  goto mutex_lock2;
		}

	      /* ret == EBUSY : bufptr->BCB_mutex is not held */
	      /* An unconditional request is given for acquiring BCB_mutex
	         after releasing hash_mutex. */
	      pthread_mutex_unlock (&hash_anchor->hash_mutex);
	      rv = pthread_mutex_lock (&bufptr->BCB_mutex);
	    }
#else /* SERVER_MODE */
	  pthread_mutex_unlock (&hash_anchor->hash_mutex);
	  rv = pthread_mutex_lock (&bufptr->BCB_mutex);
#endif /* SERVER_MODE */

	  if (!VPID_EQ (&(bufptr->vpid), vpid))
	    {
	      /* updated or replaced */
	      pthread_mutex_unlock (&bufptr->BCB_mutex);
	      goto try_again;
	    }
	  break;
	}
      bufptr = bufptr->hash_next;
    }
  /* at this point,
     if (bufptr != NULL)
     caller holds bufptr->BCB_mutex but not hash_anchor->hash_mutex
     if (bufptr == NULL)
     caller holds hash_anchor->hash_mutex.
   */
  return bufptr;
}

/*
 * pgbuf_insert_into_hash_chain () - Inserts BCB into the hash chain
 *   return: NO_ERROR
 *   hash_anchor(in): hash anchor
 *   bufptr(in): pointer to buffer page (BCB)
 *
 * Note: Before insertion, it must hold the mutex of the hash anchor.
 *       It doesn't release the mutex of the hash anchor.
 *       The mutex of the hash anchor will be released in the next call of
 *       pgbuf_unlock_page ().
 */
static int
pgbuf_insert_into_hash_chain (PGBUF_BUFFER_HASH * hash_anchor,
			      PGBUF_BCB * bufptr)
{
#if defined(SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */

  /* Note that the caller is not holding bufptr->BCB_mutex */
  rv = pthread_mutex_lock (&hash_anchor->hash_mutex);
  bufptr->hash_next = hash_anchor->hash_next;
  hash_anchor->hash_next = bufptr;

  /*
   * hash_anchor->hash_mutex is not released at this place.
   * The current BCB is the newly allocated BCB by the caller and
   * it is connected into the corresponding buffer hash chain, now.
   * hash_anchor->hahs_mutex will be released in pgbuf_unlock_page ()
   * after releasing the acquired buffer lock on the BCB.
   */
  return NO_ERROR;
}

/*
 * pgbuf_delete_from_hash_chain () - Deletes BCB from the hash chain
 *   return: NO_ERROR, or ER_code
 *   bufptr(in): pointer to buffer page
 */
static int
pgbuf_delete_from_hash_chain (PGBUF_BCB * bufptr)
{
  PGBUF_BUFFER_HASH *hash_anchor;
  PGBUF_BCB *prev_bufptr;
  PGBUF_BCB *curr_bufptr;
#if defined(SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */

  /* the caller is holding bufptr->BCB_mutex */

  /* fcnt==0, next_wait_thrd==NULL, latch_mode==PGBUF_NO_LATCH/PGBUF_LATCH_VICTIM */
  /* if (bufptr->latch_mode==PGBUF_NO_LATCH) invoked by an invalidator
     if (bufptr->latch_mode==PGBUF_LATCH_VICTIM) invoked by a victim selector
   */
  hash_anchor =
    &(pgbuf_Pool.buf_hash_table[PGBUF_HASH_VALUE (&(bufptr->vpid))]);
  rv = pthread_mutex_lock (&hash_anchor->hash_mutex);
  if (bufptr->avoid_victim == true)
    {
      assert (false);

      /* Someone tries to fix the current buffer page.
       * So, give up selecting current buffer page as a victim.
       */
      pthread_mutex_unlock (&hash_anchor->hash_mutex);
      bufptr->latch_mode = PGBUF_NO_LATCH;
      pthread_mutex_unlock (&bufptr->BCB_mutex);
      return ER_FAILED;
    }
  else
    {
      /* find current BCB in buffer hash chain */
      prev_bufptr = NULL;
      curr_bufptr = hash_anchor->hash_next;

      while (curr_bufptr != NULL)
	{
	  if (curr_bufptr == bufptr)
	    {
	      break;
	    }
	  prev_bufptr = curr_bufptr;
	  curr_bufptr = curr_bufptr->hash_next;
	}

      if (curr_bufptr == NULL)
	{
	  assert (false);

	  pthread_mutex_unlock (&hash_anchor->hash_mutex);

	  /* Now, the caller is holding bufptr->BCB_mutex. */
	  /* bufptr->BCB_mutex will be released in following function. */
	  pgbuf_put_bcb_into_invalid_list (bufptr);

	  return ER_FAILED;
	}

      /* disconnect the BCB from the buffer hash chain */
      if (prev_bufptr == NULL)
	{
	  hash_anchor->hash_next = curr_bufptr->hash_next;
	}
      else
	{
	  prev_bufptr->hash_next = curr_bufptr->hash_next;
	}

      curr_bufptr->hash_next = NULL;
      pthread_mutex_unlock (&hash_anchor->hash_mutex);
      VPID_SET_NULL (&(bufptr->vpid));

      return NO_ERROR;
    }
}

/*
 * pgbuf_lock_page () - Puts a buffer lock on the buffer lock chain
 *   return: If success, PGBUF_LOCK_HOLDER, otherwise PGBUF_LOCK_WAITER
 *   hash_anchor(in):
 *   vpid(in):
 *
 * Note: This function is invoked only when the page is not in the buffer hash
 *       chain. The caller is holding hash_anchor->hash_mutex.
 *       Before return, the thread releases hash_anchor->hash_mutex.
 */
static int
pgbuf_lock_page (THREAD_ENTRY * thread_p, PGBUF_BUFFER_HASH * hash_anchor,
		 const VPID * vpid)
{
#if defined(SERVER_MODE)
  PGBUF_BUFFER_LOCK *cur_buffer_lock;
  THREAD_ENTRY *cur_thrd_entry;

  /* the caller is holding hash_anchor->hash_mutex */
  /* check whether the page is in the Buffer Lock Chain */

  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  cur_thrd_entry = thread_p;
  cur_buffer_lock = hash_anchor->lock_next;

  /* find vpid in buffer lock chain */
  while (cur_buffer_lock != NULL)
    {
      if (VPID_EQ (&(cur_buffer_lock->vpid), vpid))
	{
	  /* found */
	  cur_thrd_entry->next_wait_thrd = cur_buffer_lock->next_wait_thrd;
	  cur_buffer_lock->next_wait_thrd = cur_thrd_entry;
	  pgbuf_sleep (cur_thrd_entry, &hash_anchor->hash_mutex);

	  if (cur_thrd_entry->resume_status != THREAD_PGBUF_RESUMED)
	    {
	      /* interrupt operation */
	      THREAD_ENTRY *thrd_entry, *prev_thrd_entry = NULL;
	      int r;

	      r = pthread_mutex_lock (&hash_anchor->hash_mutex);
	      thrd_entry = cur_buffer_lock->next_wait_thrd;

	      while (thrd_entry != NULL)
		{
		  if (thrd_entry == cur_thrd_entry)
		    {
		      if (prev_thrd_entry == NULL)
			{
			  cur_buffer_lock->next_wait_thrd =
			    thrd_entry->next_wait_thrd;
			}
		      else
			{
			  prev_thrd_entry->next_wait_thrd =
			    thrd_entry->next_wait_thrd;
			}

		      thrd_entry->next_wait_thrd = NULL;
		      pthread_mutex_unlock (&hash_anchor->hash_mutex);

		      mnt_lk_waited_on_pages (thread_p);	/* monitoring */
		      return PGBUF_LOCK_WAITER;
		    }
		  prev_thrd_entry = thrd_entry;
		  thrd_entry = thrd_entry->next_wait_thrd;
		}
	      pthread_mutex_unlock (&hash_anchor->hash_mutex);
	    }
	  mnt_lk_waited_on_pages (thread_p);	/* monitoring */
	  return PGBUF_LOCK_WAITER;
	}
      cur_buffer_lock = cur_buffer_lock->lock_next;
    }

  /* buf_lock_table is implemented to have one entry for each thread.
     At first design, it had one entry for each thread.
     cur_thrd_entry->index      : thread entry index
     cur_thrd_entry->tran_index : transaction entry index
   */

  /* vpid is not found in the Buffer Lock Chain */
  cur_buffer_lock = &(pgbuf_Pool.buf_lock_table[cur_thrd_entry->index]);
  cur_buffer_lock->vpid = *vpid;
  cur_buffer_lock->next_wait_thrd = NULL;
  cur_buffer_lock->lock_next = hash_anchor->lock_next;
  hash_anchor->lock_next = cur_buffer_lock;
  pthread_mutex_unlock (&hash_anchor->hash_mutex);
#endif /* SERVER_MODE */

  mnt_lk_acquired_on_pages (thread_p);	/* monitoring */
  return PGBUF_LOCK_HOLDER;
}

/*
 * pgbuf_unlock_page () - Deletes a buffer lock from the buffer lock chain
 *   return: NO_ERROR
 *   hash_anchor(in):
 *   vpid(in):
 *   need_hash_mutex(in):
 *
 * Note: This function is invoked only after the page is read into buffer and
 *       the BCB is connected into its corresponding buffer hash chain.
 *       Before return, the thread releases the hash mutex on the hash
 *       anchor and wakes up all the threads blocked on the queue of the
 *       buffer lock record.
 */
static int
pgbuf_unlock_page (PGBUF_BUFFER_HASH * hash_anchor, const VPID * vpid,
		   int need_hash_mutex)
{
#if defined(SERVER_MODE)
  int rv;

  PGBUF_BUFFER_LOCK *prev_buffer_lock, *cur_buffer_lock;
  THREAD_ENTRY *cur_thrd_entry;

  if (need_hash_mutex)
    {
      rv = pthread_mutex_lock (&hash_anchor->hash_mutex);
    }

  /* check whether the page is in the Buffer Lock Chain */
  prev_buffer_lock = NULL;
  cur_buffer_lock = hash_anchor->lock_next;

  while (cur_buffer_lock != NULL)
    {
      if (VPID_EQ (&(cur_buffer_lock->vpid), vpid))
	{
	  break;
	}

      prev_buffer_lock = cur_buffer_lock;
      cur_buffer_lock = cur_buffer_lock->lock_next;
    }

  if (cur_buffer_lock != NULL)
    {
      if (prev_buffer_lock == NULL)
	{
	  hash_anchor->lock_next = cur_buffer_lock->lock_next;
	}
      else
	{
	  prev_buffer_lock->lock_next = cur_buffer_lock->lock_next;
	}

      cur_buffer_lock->lock_next = NULL;
      pthread_mutex_unlock (&hash_anchor->hash_mutex);

      while ((cur_thrd_entry = cur_buffer_lock->next_wait_thrd) != NULL)
	{
	  cur_buffer_lock->next_wait_thrd = cur_thrd_entry->next_wait_thrd;
	  cur_thrd_entry->next_wait_thrd = NULL;
	  pgbuf_wakeup_uncond (cur_thrd_entry);
	}
    }
  else
    {
      pthread_mutex_unlock (&hash_anchor->hash_mutex);
    }
#endif /* SERVER_MODE */

  return NO_ERROR;
}

/*
 * pgbuf_allocate_bcb () - Allocates a BCB
 *   return:  If success, a newly allocated BCB, otherwise NULL
 *   src_vpid(in):
 *
 * Note: This function allocates a BCB from the buffer invalid list or the
 *       LRU list. It is invoked only when a page is not in buffer. If there
 *       is no non-dirty buffer, wait 1 microsecond and retry to allocate a BCB again.
 */
#if !defined(NDEBUG)
static PGBUF_BCB *
pgbuf_allocate_bcb (THREAD_ENTRY * thread_p, const VPID * src_vpid,
		    const char *caller_file, int caller_line)
#else /* NDEBUG */
static PGBUF_BCB *
pgbuf_allocate_bcb (THREAD_ENTRY * thread_p, const VPID * src_vpid)
#endif				/* NDEBUG */
{
  VPID vpid;
  PGBUF_BCB *bufptr;
  int i, sleep_count, loop_count, check_count;

  loop_count = 0;

  check_count =
    MAX (PGBUF_MIN_NUM_VICTIMS,
	 (int) (PGBUF_LRU_SIZE *
		prm_get_float_value (PRM_ID_PB_BUFFER_FLUSH_RATIO)));

  while (loop_count++ < INT_MAX)
    {
      vpid = *src_vpid;
      for (i = 0; i < pgbuf_Pool.num_LRU_list; i++, vpid.pageid++)
	{
	  for (sleep_count = 0; sleep_count < PGBUF_SLEEP_MAX; sleep_count++)
	    {
	      /* allocate a BCB from invalid BCB list */
	      bufptr = pgbuf_get_bcb_from_invalid_list ();
	      if (bufptr != NULL)
		{
		  return bufptr;
		}

	      /* If the caller allocates a BCB successfully,
	       * the caller is holding bufptr->BCB_mutex.
	       */

	      /* If the allocation of BCB from invalid BCB list fails,
	       * that is, invalid BCB list is empty,
	       * allocate a BCB from the bottom of LRU list
	       */
	      bufptr = pgbuf_get_victim (thread_p, &vpid, check_count);
	      if (bufptr != NULL)
		{
		  /* the caller is holding bufptr->BCB_mutex. */

#if !defined(NDEBUG)
		  if (pgbuf_victimize_bcb (thread_p, bufptr,
					   caller_file,
					   caller_line) != NO_ERROR)
#else /* NDEBUG */
		  if (pgbuf_victimize_bcb (thread_p, bufptr) != NO_ERROR)
#endif /* NDEBUG */
		    {
		      assert (false);
		      continue;
		    }

		  /* Above function holds bufptr->BCB_mutex at first operation.
		   * If the above function returns failure,
		   * the caller does not hold bufptr->BCB_mutex.
		   * Otherwise, the caller is still holding bufptr->BCB_mutex.
		   */
		  return bufptr;
		}

#if defined(SERVER_MODE)
	      thread_sleep (0.001);	/* 1 microsecond */
#endif /* SERVER_MODE */
	    }
	}

      /* interrupt check */
      if (thread_get_check_interrupt (thread_p) == true)
	{
	  if (logtb_is_interrupted (thread_p, true,
				    &pgbuf_Pool.check_for_interrupts) == true)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INTERRUPTED, 0);
	      return NULL;
	    }
	}

      er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE,
	      ER_PB_ALL_BUFFERS_DIRTY, 1, check_count);

      check_count = PGBUF_LRU_SIZE - PGBUF_LRU_1_ZONE_THRESHOLD;
    }

  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_PB_ALL_BUFFERS_FIXED, 1, -1);
  assert (false);

  return NULL;
}

/*
 * pgbuf_victimize_bcb () - Victimize given buffer page
 *   return: NO_ERROR, or ER_code
 *   bufptr(in): pointer to buffer page
 *
 * Note: If zone == VOIDZone, fcnt == 0, wait == false,
 *       latch_mode != PGBUF_LATCH_VICTIM and there is no reader, writer,
 *       or victim selector on the BCB queue,
 *       then select it as a replacement victim.
 *       else release the BCB mutex and return ER_FAILED.
 *
 *       In case of success, if the page is dirty, flush it
 *       according to the WAL protocol.
 *       Even though success, we must check again the above condition.
 */
#if !defined(NDEBUG)
static int
pgbuf_victimize_bcb (THREAD_ENTRY * thread_p, PGBUF_BCB * bufptr,
		     const char *caller_file, int caller_line)
#else /* NDEBUG */
static int
pgbuf_victimize_bcb (THREAD_ENTRY * thread_p, PGBUF_BCB * bufptr)
#endif				/* NDEBUG */
{
#if defined(SERVER_MODE)
  THREAD_ENTRY *cur_thrd_entry;

  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  cur_thrd_entry = thread_p;
#endif /* SERVER_MODE */

  /* the caller is holding bufptr->BCB_mutex */

  /* before-flush, check victim condition again */
  if (bufptr->zone != PGBUF_VOID_ZONE
      || bufptr->fcnt != 0
      || bufptr->dirty == true
      || bufptr->avoid_victim == true
      || bufptr->latch_mode != PGBUF_NO_LATCH
      || pgbuf_is_exist_blocked_reader_writer_victim (bufptr) == true)
    {
      assert (false);

      return ER_FAILED;
    }

  /* grant the request */
  bufptr->latch_mode = PGBUF_LATCH_VICTIM;

  /* a safe victim */
  if (pgbuf_delete_from_hash_chain (bufptr) != NO_ERROR)
    {
      return ER_FAILED;
    }

  /*
   * If above function returns success,
   * the caller is still holding bufptr->BCB_mutex.
   * Otherwise, the caller does not hold bufptr->BCB_mutex.
   */

  /* at this point, the caller is holding bufptr->BCB_mutex */

#if defined(DIAG_DEVEL) && defined(SERVER_MODE)
  SET_DIAG_VALUE (diag_executediag, DIAG_OBJ_TYPE_QUERY_OPENED_PAGE, 1,
		  DIAG_VAL_SETTYPE_INC, NULL);
#endif /* DIAG_DEVEL && SERVER_MODE */

  return NO_ERROR;
}

/*
 * pgbuf_invalidate_bcb () - Invalidates BCB
 *   return: NO_ERROR, or ER_code
 *   bufptr(in): pointer to buffer page
 */
static int
pgbuf_invalidate_bcb (PGBUF_BCB * bufptr)
{
  /* the caller is holding bufptr->BCB_mutex */
  /* be sure that there is not any reader/writer */

  assert (bufptr->latch_mode != PGBUF_LATCH_VICTIM);
  assert (bufptr->latch_mode != PGBUF_LATCH_VICTIM_INVALID);

  if (bufptr->latch_mode == PGBUF_LATCH_FLUSH_INVALID
      || bufptr->latch_mode == PGBUF_LATCH_VICTIM_INVALID
      || bufptr->latch_mode == PGBUF_LATCH_INVALID)
    {
      pthread_mutex_unlock (&bufptr->BCB_mutex);
      return NO_ERROR;
    }

  bufptr->dirty = false;
  LSA_SET_NULL (&bufptr->oldest_unflush_lsa);

  /* bufptr->BCB_mutex is still held by the caller. */
  switch (bufptr->zone)
    {
    case PGBUF_VOID_ZONE:
      break;

    case PGBUF_AIN_ZONE:
      pgbuf_invalidate_bcb_from_ain (bufptr);
      break;

    default:
      pgbuf_invalidate_bcb_from_lru (bufptr);
      break;
    }

  if (bufptr->latch_mode == PGBUF_NO_LATCH)
    {
      if (pgbuf_delete_from_hash_chain (bufptr) != NO_ERROR)
	{
	  return ER_FAILED;
	}

      /* If above function returns failure,
       * the caller does not hold bufptr->BCB_mutex.
       * Otherwise, the caller is holding bufptr->BCB_mutex.
       */

      /* Now, the caller is holding bufptr->BCB_mutex. */
      /* bufptr->BCB_mutex will be released in following function. */
      pgbuf_put_bcb_into_invalid_list (bufptr);

#if defined(DIAG_DEVEL) && defined(SERVER_MODE)
      SET_DIAG_VALUE (diag_executediag, DIAG_OBJ_TYPE_QUERY_OPENED_PAGE, 1,
		      DIAG_VAL_SETTYPE_INC, NULL);
#endif /* DIAG_DEVEL && SERVER_MODE */
    }
  else
    {
      if (bufptr->latch_mode == PGBUF_LATCH_FLUSH)
	{
	  bufptr->latch_mode = PGBUF_LATCH_FLUSH_INVALID;
	}
      else
	{
	  bufptr->latch_mode = PGBUF_LATCH_VICTIM_INVALID;
	}
      pthread_mutex_unlock (&bufptr->BCB_mutex);
    }

  return NO_ERROR;
}

/*
 * pgbuf_flush_bcb () - Flushes the buffer
 *   return: NO_ERROR, or ER_code
 *   bufptr(in): pointer to buffer page
 *   synchronous(in): synchronous flush or asynchronous flush
 *
 * Note: If there is a writer, just set async_flush_request = true.
 *       else if sharp is true, block the BCB.
 *
 *       Before return, it releases BCB mutex.
 */
#if !defined(NDEBUG)
static int
pgbuf_flush_bcb (THREAD_ENTRY * thread_p, PGBUF_BCB * bufptr,
		 int synchronous, const char *caller_file, int caller_line)
#else /* NDEBUG */
static int
pgbuf_flush_bcb (THREAD_ENTRY * thread_p, PGBUF_BCB * bufptr, int synchronous)
#endif				/* NDEBUG */
{
  PGBUF_HOLDER *holder;
#if defined(SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */

  assert (bufptr->latch_mode != PGBUF_LATCH_VICTIM);
  assert (bufptr->latch_mode != PGBUF_LATCH_VICTIM_INVALID);

  /* the caller is holding bufptr->BCB_mutex */

  if (bufptr->latch_mode == PGBUF_LATCH_INVALID
      || bufptr->latch_mode == PGBUF_LATCH_FLUSH_INVALID
      || bufptr->latch_mode == PGBUF_LATCH_VICTIM_INVALID)
    {
      pthread_mutex_unlock (&bufptr->BCB_mutex);
      return NO_ERROR;
    }

  if (bufptr->latch_mode == PGBUF_NO_LATCH
      || bufptr->latch_mode == PGBUF_LATCH_READ)
    {
      bufptr->latch_mode = PGBUF_LATCH_FLUSH;
      if (pgbuf_flush_page_with_wal (thread_p, bufptr) != NO_ERROR)
	{
	  pthread_mutex_unlock (&bufptr->BCB_mutex);

	  return ER_FAILED;
	}

      /* all the blocked flushers on the BCB waiting queue
       * have been waken up in pgbuf_flush_page_with_wal().
       */
      if (bufptr->fcnt == 0)
	{
#if defined(SERVER_MODE)
	  if (bufptr->latch_mode == PGBUF_LATCH_FLUSH_INVALID)
	    {
	      if (bufptr->next_wait_thrd == NULL)
		{
		  /* There is no blocked threads(or transactions). */
		  /* Therefore, invalidate the BCB */
		  if (pgbuf_delete_from_hash_chain (bufptr) != NO_ERROR)
		    {
		      return ER_FAILED;
		    }

		  /*
		   * If above function returns success,
		   * the caller is still holding bufptr->BCB_mutex.
		   * Otherwise, the caller does not hold bufptr->BCB_mutex.
		   */

		  /* Now, the caller is holding bufptr->BCB_mutex. */
		  /* bufptr->BCB_mutex will be released in following function. */
		  pgbuf_put_bcb_into_invalid_list (bufptr);
		}
	      else
		{
		  /* only writer might be blocked */
		  bufptr->latch_mode = PGBUF_NO_LATCH;
		  if (bufptr->next_wait_thrd != NULL)
		    {
		      /* wake up blocked threads(or transactions). */
#if !defined(NDEBUG)
		      if (pgbuf_wakeup_bcb (thread_p, bufptr,
					    caller_file,
					    caller_line) != NO_ERROR)
#else /* NDEBUG */
		      if (pgbuf_wakeup_bcb (thread_p, bufptr) != NO_ERROR)
#endif /* NDEBUG */
			{
			  return ER_FAILED;
			}

		      /* Above function released BCB_mutex unconditionally. */
		    }
		  else
		    {
		      pthread_mutex_unlock (&bufptr->BCB_mutex);
		    }
		}
	    }
	  else
	    {
	      bufptr->latch_mode = PGBUF_NO_LATCH;
	      if (bufptr->next_wait_thrd != NULL)
		{
		  /* wake up blocked threads(or transactions) */
#if !defined(NDEBUG)
		  if (pgbuf_wakeup_bcb (thread_p, bufptr,
					caller_file, caller_line) != NO_ERROR)
#else /* NDEBUG */
		  if (pgbuf_wakeup_bcb (thread_p, bufptr) != NO_ERROR)
#endif /* NDEBUG */
		    {
		      return ER_FAILED;
		    }
		  /* Above function released BCB_mutex unconditionally. */
		}
	      else
		{
		  pthread_mutex_unlock (&bufptr->BCB_mutex);
		}
	    }
#else /* SERVER_MODE */
	  bufptr->latch_mode = PGBUF_NO_LATCH;
	  pthread_mutex_unlock (&bufptr->BCB_mutex);
#endif /* SERVER_MODE */
	}
      else
	{
	  bufptr->latch_mode = PGBUF_LATCH_READ;
	  pthread_mutex_unlock (&bufptr->BCB_mutex);
	}
    }
  else
    {
      if (bufptr->latch_mode == PGBUF_LATCH_WRITE)
	{
	  /* In CUBRID system, page flush could be performed
	     while the flusher is holding X latch on the page. */

	  holder = pgbuf_find_thrd_holder (thread_p, bufptr);
	  if (holder != NULL)
	    {
	      if (pgbuf_flush_page_with_wal (thread_p, bufptr) != NO_ERROR)
		{
		  pthread_mutex_unlock (&bufptr->BCB_mutex);

		  return ER_FAILED;
		}

	      /* If above function returns failure,
	       * the caller does not hold bufptr->BCB_mutex.
	       * Otherwise, the caller is still holding bufptr->BCB_mutex.
	       */
	      pthread_mutex_unlock (&bufptr->BCB_mutex);

	      return NO_ERROR;
	    }
	  else
	    {
	      bufptr->async_flush_request = true;
	    }
	}

      /* Currently, the caller is holding bufper->BCB_mutex */
      if (synchronous == true)
	{
	  /* After releasing bufptr->BCB_mutex, the caller sleeps. */
#if !defined(NDEBUG)
	  if (pgbuf_block_bcb (thread_p, bufptr, PGBUF_LATCH_FLUSH, 0,
			       caller_file, caller_line) != NO_ERROR)
#else /* NDEBUG */
	  if (pgbuf_block_bcb (thread_p, bufptr, PGBUF_LATCH_FLUSH, 0) !=
	      NO_ERROR)
#endif /* NDEBUG */
	    {
	      return ER_FAILED;
	    }
	}
      else
	{
	  pthread_mutex_unlock (&bufptr->BCB_mutex);
	}
    }

  return NO_ERROR;
}

/*
 * pgbuf_get_bcb_from_invalid_list () - Get BCB from buffer invalid list
 *   return: If success, a newly allocated BCB, otherwise NULL
 *
 * Note: This function disconnects a BCB on the top of the buffer invalid list
 *       and returns it. Before disconnection, the thread must hold the
 *       invalid list mutex and after disconnection, release the mutex.
 */
static PGBUF_BCB *
pgbuf_get_bcb_from_invalid_list (void)
{
  PGBUF_BCB *bufptr;
#if defined(SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */

  /* check if invalid BCB list is empty (step 1) */
  if (pgbuf_Pool.buf_invalid_list.invalid_top == NULL)
    {
      return NULL;
    }

  rv = pthread_mutex_lock (&pgbuf_Pool.buf_invalid_list.invalid_mutex);

  /* check if invalid BCB list is empty (step 2) */
  if (pgbuf_Pool.buf_invalid_list.invalid_top == NULL)
    {
      /* invalid BCB list is empty */
      pthread_mutex_unlock (&pgbuf_Pool.buf_invalid_list.invalid_mutex);
      return NULL;
    }
  else
    {
      /* invalid BCB list is not empty */
      bufptr = pgbuf_Pool.buf_invalid_list.invalid_top;
      pgbuf_Pool.buf_invalid_list.invalid_top = bufptr->next_BCB;
      pgbuf_Pool.buf_invalid_list.invalid_cnt -= 1;
      pthread_mutex_unlock (&pgbuf_Pool.buf_invalid_list.invalid_mutex);

      MUTEX_LOCK_VIA_BUSY_WAIT (rv, bufptr->BCB_mutex);
      bufptr->next_BCB = NULL;
      bufptr->zone = PGBUF_VOID_ZONE;
      return bufptr;
    }
}

/*
 * pgbuf_put_bcb_into_invalid_list () - Put BCB into buffer invalid list
 *   return: NO_ERROR
 *   bufptr(in):
 *
 * Note: This function connects BCB to the top of the buffer invalid list and
 *       makes its zone PB_INVALIDZone. Before connection, must hold the
 *       invalid list mutex and after connection, release the mutex.
 */
static int
pgbuf_put_bcb_into_invalid_list (PGBUF_BCB * bufptr)
{
#if defined(SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */

  /* the caller is holding bufptr->BCB_mutex */
  VPID_SET_NULL (&bufptr->vpid);
  bufptr->latch_mode = PGBUF_LATCH_INVALID;
  bufptr->zone = PGBUF_INVALID_ZONE;

  rv = pthread_mutex_lock (&pgbuf_Pool.buf_invalid_list.invalid_mutex);
  bufptr->next_BCB = pgbuf_Pool.buf_invalid_list.invalid_top;
  pgbuf_Pool.buf_invalid_list.invalid_top = bufptr;
  pgbuf_Pool.buf_invalid_list.invalid_cnt += 1;
  pthread_mutex_unlock (&bufptr->BCB_mutex);
  pthread_mutex_unlock (&pgbuf_Pool.buf_invalid_list.invalid_mutex);

  return NO_ERROR;
}

/*
 * pgbuf_get_lru_index () - Get the index of the LRU list for the given VPID
 *   return: the index of the LRU index
 *   vpid(in): VPID
 */
static int
pgbuf_get_lru_index (const VPID * vpid)
{
  int lru_idx;

  lru_idx = (vpid->pageid ^ vpid->volid) % pgbuf_Pool.num_LRU_list;

  return lru_idx;
}

/*
 * pgbuf_get_victim () - find a victim BCB
 * return : victim candidate or NULL if no candidate was found
 * thread_p (in) :
 * vpid (in) :
 * max_count (in) :
 *
 * Note: If a victim BCB is found, this function will already lock it. This
 *     means that the caller will have exclusive access to the returned BCB.
 */
static PGBUF_BCB *
pgbuf_get_victim (THREAD_ENTRY * thread_p, const VPID * vpid, int max_count)
{
  PGBUF_BCB *victim = NULL;

  if (pgbuf_Pool.buf_AIN_list.ain_count >= pgbuf_Pool.buf_AIN_list.max_count
      && PGBUF_IS_2Q_ENABLED)
    {
      ATOMIC_INC_32 (&pgbuf_Pool.ain_victim_req_cnt, 1);
      victim = pgbuf_get_victim_from_ain_list (thread_p, max_count);
    }

  if (victim == NULL)
    {
      /* if we don't find one in the Ain list, we are going to find one 
       * in LRU lists. This may lead a hot page is evicted from buffer.
       * This is intended for response time of workers that want to victimize one.
       * We may choose a policy to wait for one only in AIN list, however, 
       * the worker thread must wait for the flush thread flushes the dirty pages
       * from AIN list while iterating the AIN list multiple times.
       */
      ATOMIC_INC_32 (&pgbuf_Pool.lru_victim_req_cnt, 1);
      victim = pgbuf_get_victim_from_lru_list (thread_p, vpid, max_count);
    }

  return victim;
}

/*
 * pgbuf_get_victim_from_ain_list () - Get BCB victim from Ain list
 * return : victim
 * thread_p (in)  :
 * max_count (in) :
 */
static PGBUF_BCB *
pgbuf_get_victim_from_ain_list (THREAD_ENTRY * thread_p, int max_count)
{
  PGBUF_BCB *bufptr;
  PGBUF_AIN_LIST *ain_list = NULL;
  bool found = false;
  int check_count, check_count_max;
  int dirty_count;
#if defined(SERVER_MODE)
  int rv;
#endif

  ain_list = &pgbuf_Pool.buf_AIN_list;

  assert (PGBUF_IS_2Q_ENABLED);

  MUTEX_LOCK_VIA_BUSY_WAIT (rv, ain_list->Ain_mutex);
  if (ain_list->Ain_bottom == NULL)
    {
      pthread_mutex_unlock (&ain_list->Ain_mutex);
      return NULL;
    }

  /* maximum amount to check in AIN is same as maximum amount to check in
   * all LRU lists, but limited to half of target size of AIN list */
  check_count_max = max_count * pgbuf_Pool.num_LRU_list;
  check_count_max = MIN (check_count_max, ain_list->max_count / 2);
  check_count = MAX (check_count_max, 1);

  dirty_count = 0;
  bufptr = ain_list->Ain_bottom;

  while (bufptr != NULL && check_count > 0)
    {
      if (!bufptr->dirty && !bufptr->avoid_victim && bufptr->fcnt == 0
	  && bufptr->latch_mode == PGBUF_NO_LATCH
	  && bufptr->victim_candidate == false
	  && !pgbuf_is_exist_blocked_reader_writer_victim (bufptr))
	{
	  bufptr->victim_candidate = true;
	  found = true;
	  break;
	}

      if (bufptr->dirty)
	{
	  dirty_count++;
	}

      bufptr = bufptr->prev_BCB;
      check_count--;
    }

  if (!found)
    {
      bufptr = NULL;
    }

  pthread_mutex_unlock (&ain_list->Ain_mutex);

  if (bufptr == NULL || dirty_count > max_count / 2)
    {
      /* Flush some pages. We do this either because we have too many dirty
       * pages or we didn't find a victim.
       */
      pgbuf_wakeup_flush_thread (thread_p);

      if (bufptr == NULL)
	{
	  /* We didn't find any victim. */
	  return NULL;
	}
    }

  MUTEX_LOCK_VIA_BUSY_WAIT (rv, bufptr->BCB_mutex);

  /* Since this is the first time we actually get exclusive access to
   * this bufptr, we have to reevaluate our choice
   */
  if (bufptr->dirty == true
      || bufptr->avoid_victim == true
      || bufptr->fcnt != 0
      || bufptr->latch_mode != PGBUF_NO_LATCH
      || bufptr->zone != PGBUF_AIN_ZONE
      || pgbuf_is_exist_blocked_reader_writer_victim (bufptr) == true)
    {
      /* Oops! While we were playing around with the Ain list, somebody
       * else fixed this page and we can't know if we should victimize it
       * or not. Release bufptr mutex and return NULL. We will find
       * another victim at next pass.
       */
      bufptr->victim_candidate = false;
      pthread_mutex_unlock (&bufptr->BCB_mutex);

      return NULL;
    }
  else
    {
      MUTEX_LOCK_VIA_BUSY_WAIT (rv, ain_list->Ain_mutex);
      pgbuf_remove_from_ain_list (bufptr);
      ain_list->ain_count -= 1;
      pthread_mutex_unlock (&ain_list->Ain_mutex);

      bufptr->victim_candidate = false;
      bufptr->zone = PGBUF_VOID_ZONE;

      /* Add vpid to AOUT list. We won't be able to do it when we actually
       * victimize the page because we will not know where the BCB came from.
       */
      pgbuf_add_vpid_to_aout_list (thread_p, &bufptr->vpid);

      return bufptr;
    }
}

/*
 * pgbuf_get_victim_from_lru_list () - Get victim BCB from the bottom of
 *				       LRU list
 *   return: If success, BCB, otherwise NULL
 *   vpid (in)	      : VPID used for determining resident LRU list
 *   max_count (in)   : maximum number of elements to consider
 *
 * Note: This function disconnects BCB from the bottom of the LRU list and
 *       returns it if its fcnt == 0. If its fcnt != 0, makes bufptr->PrevBCB
 *       LRU_bottom and retry. While this processing, the caller must be the
 *       holder of the LRU list.
 */
static PGBUF_BCB *
pgbuf_get_victim_from_lru_list (THREAD_ENTRY * thread_p, const VPID * vpid,
				int max_count)
{
#if defined(SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */

  PGBUF_BCB *bufptr;
  int lru_idx;
  int check_count;
  bool found;
  bool list_bottom_dirty = false;
  PGBUF_LRU_LIST *lru_list;

  lru_idx = pgbuf_get_lru_index (vpid);
  lru_list = &pgbuf_Pool.buf_LRU_list[lru_idx];

  /* check if LRU list is empty */
  if (lru_list->LRU_bottom == NULL)
    {
      return NULL;
    }

  found = false;
  check_count = max_count;

  MUTEX_LOCK_VIA_BUSY_WAIT (rv, lru_list->LRU_mutex);
  bufptr = lru_list->LRU_bottom;

  /* search for non dirty PGBUF */
  while (bufptr != NULL && check_count > 0
	 && bufptr->zone == PGBUF_LRU_2_ZONE)
    {
      if (!bufptr->dirty && !bufptr->avoid_victim && bufptr->fcnt == 0
	  && bufptr->latch_mode == PGBUF_NO_LATCH
	  && bufptr->victim_candidate == false
	  && !pgbuf_is_exist_blocked_reader_writer_victim (bufptr))
	{
	  bufptr->victim_candidate = true;
	  found = true;
	  break;
	}

      bufptr = bufptr->prev_BCB;
      check_count--;
    }

  if (!found)
    {
      bufptr = NULL;
    }

  if (lru_list->LRU_bottom != NULL && lru_list->LRU_bottom->dirty == true)
    {
      list_bottom_dirty = true;
    }

  pthread_mutex_unlock (&lru_list->LRU_mutex);

  if (list_bottom_dirty == true)
    {
      /* flush dirty pages */
      pgbuf_wakeup_flush_thread (thread_p);
    }

  if (bufptr == NULL)
    {
      return NULL;
    }

  MUTEX_LOCK_VIA_BUSY_WAIT (rv, bufptr->BCB_mutex);

  if (bufptr->dirty == true || bufptr->avoid_victim == true
      || bufptr->zone != PGBUF_LRU_2_ZONE
      || bufptr->fcnt != 0
      || bufptr->latch_mode != PGBUF_NO_LATCH
      || pgbuf_is_exist_blocked_reader_writer_victim (bufptr) == true)
    {
      bufptr->victim_candidate = false;
      pthread_mutex_unlock (&bufptr->BCB_mutex);

      return NULL;
    }
  else
    {
      MUTEX_LOCK_VIA_BUSY_WAIT (rv, lru_list->LRU_mutex);
      /* disconnect bufptr from the LRU list */
      pgbuf_remove_from_lru_list (bufptr, lru_list);
      pthread_mutex_unlock (&lru_list->LRU_mutex);

      bufptr->victim_candidate = false;
      bufptr->zone = PGBUF_VOID_ZONE;

      return bufptr;
    }
}

/*
 * pgbuf_invalidate_bcb_from_lru () - Disconnects BCB from the LRU list
 *   return: NO_ERROR
 *   bufptr(in): pointer to buffer page
 *
 * Note: While this processing, the caller must be the holder of the LRU list.
 */
static int
pgbuf_invalidate_bcb_from_lru (PGBUF_BCB * bufptr)
{
  int lru_idx;
#if defined(SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */

  lru_idx = pgbuf_get_lru_index (&bufptr->vpid);

  /* the caller is holding bufptr->BCB_mutex */
  /* delete the bufptr from the LRU list */
  MUTEX_LOCK_VIA_BUSY_WAIT (rv, pgbuf_Pool.buf_LRU_list[lru_idx].LRU_mutex);

  if (pgbuf_Pool.buf_LRU_list[lru_idx].LRU_top == bufptr)
    {
      pgbuf_Pool.buf_LRU_list[lru_idx].LRU_top = bufptr->next_BCB;
    }

  if (pgbuf_Pool.buf_LRU_list[lru_idx].LRU_bottom == bufptr)
    {
      pgbuf_Pool.buf_LRU_list[lru_idx].LRU_bottom = bufptr->prev_BCB;
    }

  if (pgbuf_Pool.buf_LRU_list[lru_idx].LRU_middle == bufptr)
    {
      pgbuf_Pool.buf_LRU_list[lru_idx].LRU_middle = bufptr->prev_BCB;
    }

  if (bufptr->next_BCB != NULL)
    {
      (bufptr->next_BCB)->prev_BCB = bufptr->prev_BCB;
    }

  if (bufptr->prev_BCB != NULL)
    {
      (bufptr->prev_BCB)->next_BCB = bufptr->next_BCB;
    }

  bufptr->prev_BCB = bufptr->next_BCB = NULL;
  if (bufptr->zone == PGBUF_LRU_1_ZONE)
    {
      pgbuf_Pool.buf_LRU_list[lru_idx].LRU_1_zone_cnt -= 1;
    }

  bufptr->zone = PGBUF_VOID_ZONE;

  pthread_mutex_unlock (&pgbuf_Pool.buf_LRU_list[lru_idx].LRU_mutex);

  return NO_ERROR;
}


/*
 * pgbuf_invalidate_bcb_from_ain () - disconnects a BCB from the Ain list
 * return : error code or NO_ERROR
 * bufptr (in) : BCB to invalidate
 */
static int
pgbuf_invalidate_bcb_from_ain (PGBUF_BCB * bufptr)
{
#if defined(SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */
  PGBUF_AIN_LIST *ain_list;
  assert (bufptr->zone == PGBUF_AIN_ZONE);

  MUTEX_LOCK_VIA_BUSY_WAIT (rv, pgbuf_Pool.buf_AIN_list.Ain_mutex);
  ain_list = &pgbuf_Pool.buf_AIN_list;
  if (ain_list->Ain_top == bufptr)
    {
      ain_list->Ain_top = bufptr->next_BCB;
    }
  if (ain_list->Ain_bottom == bufptr)
    {
      ain_list->Ain_bottom = bufptr->prev_BCB;
    }

  if (bufptr->next_BCB != NULL)
    {
      bufptr->next_BCB->prev_BCB = bufptr->prev_BCB;
    }
  if (bufptr->prev_BCB != NULL)
    {
      bufptr->prev_BCB->next_BCB = bufptr->next_BCB;
    }
  ain_list->ain_count -= 1;

  bufptr->next_BCB = NULL;
  bufptr->prev_BCB = NULL;
  bufptr->zone = PGBUF_VOID_ZONE;

  pthread_mutex_unlock (&ain_list->Ain_mutex);
  return NO_ERROR;
}

/*
 * pgbuf_relocate_top_lru () - Relocate given BCB into the LRU list
 *   return: NO_ERROR
 *   bufptr(in): pointer to buffer page
 *   dest_zone(in): zone part of LRU to relocate to
 *
 * Note: This function puts BCB to the top of the LRU list.
 */
static int
pgbuf_relocate_top_lru (PGBUF_BCB * bufptr, int dest_zone)
{
  int lru_idx;
  PGBUF_BCB *ref_bufptr;
#if defined(SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */

  assert (bufptr->zone != PGBUF_LRU_1_ZONE);

  lru_idx = pgbuf_get_lru_index (&bufptr->vpid);

  /* the caller is holding bufptr->BCB_mutex */
  MUTEX_LOCK_VIA_BUSY_WAIT (rv, pgbuf_Pool.buf_LRU_list[lru_idx].LRU_mutex);

  if (dest_zone == PGBUF_LRU_2_ZONE
      && (pgbuf_Pool.buf_LRU_list[lru_idx].LRU_bottom == NULL
	  || pgbuf_Pool.buf_LRU_list[lru_idx].LRU_middle == NULL
	  || pgbuf_Pool.buf_LRU_list[lru_idx].LRU_top == NULL
	  || bufptr->zone == PGBUF_LRU_2_ZONE))
    {
      dest_zone = PGBUF_LRU_1_ZONE;
    }

  if (bufptr->zone == PGBUF_LRU_2_ZONE)
    {
      /* delete bufptr from LRU list */
      if (pgbuf_Pool.buf_LRU_list[lru_idx].LRU_top == bufptr)
	{
	  pthread_mutex_unlock (&pgbuf_Pool.buf_LRU_list[lru_idx].LRU_mutex);
	  return NO_ERROR;
	}

      if (pgbuf_Pool.buf_LRU_list[lru_idx].LRU_bottom == bufptr)
	{
	  pgbuf_Pool.buf_LRU_list[lru_idx].LRU_bottom = bufptr->prev_BCB;
	}

      if (bufptr->next_BCB != NULL)
	{
	  (bufptr->next_BCB)->prev_BCB = bufptr->prev_BCB;
	}

      if (bufptr->prev_BCB != NULL)
	{
	  (bufptr->prev_BCB)->next_BCB = bufptr->next_BCB;
	}
    }

  if (dest_zone == PGBUF_LRU_2_ZONE)
    {
      ref_bufptr = pgbuf_Pool.buf_LRU_list[lru_idx].LRU_middle;

      bufptr->next_BCB = ref_bufptr->next_BCB;
      bufptr->prev_BCB = ref_bufptr;

      if (ref_bufptr->next_BCB != NULL)
	{
	  (ref_bufptr->next_BCB)->prev_BCB = bufptr;
	}
      else
	{
	  pgbuf_Pool.buf_LRU_list[lru_idx].LRU_bottom = bufptr;
	}
      ref_bufptr->next_BCB = bufptr;

      bufptr->zone = PGBUF_LRU_2_ZONE;
    }
  else
    {
      assert (dest_zone == PGBUF_LRU_1_ZONE);

      /* put BCB into the top of the LRU list */
      bufptr->prev_BCB = NULL;
      bufptr->next_BCB = pgbuf_Pool.buf_LRU_list[lru_idx].LRU_top;
      if (pgbuf_Pool.buf_LRU_list[lru_idx].LRU_top == NULL)
	{
	  pgbuf_Pool.buf_LRU_list[lru_idx].LRU_bottom = bufptr;
	}
      else
	{
	  (pgbuf_Pool.buf_LRU_list[lru_idx].LRU_top)->prev_BCB = bufptr;
	}

      pgbuf_Pool.buf_LRU_list[lru_idx].LRU_top = bufptr;
      if (pgbuf_Pool.buf_LRU_list[lru_idx].LRU_middle == NULL)
	{
	  pgbuf_Pool.buf_LRU_list[lru_idx].LRU_middle = bufptr;
	}

      bufptr->zone = PGBUF_LRU_1_ZONE;	/* OK. */
      pgbuf_Pool.buf_LRU_list[lru_idx].LRU_1_zone_cnt += 1;
      if (pgbuf_Pool.buf_LRU_list[lru_idx].LRU_1_zone_cnt >
	  PGBUF_LRU_1_ZONE_THRESHOLD)
	{
	  PGBUF_BCB *temp_bufptr;

	  temp_bufptr = pgbuf_Pool.buf_LRU_list[lru_idx].LRU_middle;
	  while (temp_bufptr != NULL
		 && (pgbuf_Pool.buf_LRU_list[lru_idx].LRU_1_zone_cnt >
		     PGBUF_LRU_1_ZONE_THRESHOLD))
	    {
	      temp_bufptr->zone = PGBUF_LRU_2_ZONE;
	      pgbuf_Pool.buf_LRU_list[lru_idx].LRU_middle =
		temp_bufptr->prev_BCB;
	      pgbuf_Pool.buf_LRU_list[lru_idx].LRU_1_zone_cnt -= 1;
	      temp_bufptr = pgbuf_Pool.buf_LRU_list[lru_idx].LRU_middle;
	    }
	}
    }

  pthread_mutex_unlock (&pgbuf_Pool.buf_LRU_list[lru_idx].LRU_mutex);

  return NO_ERROR;
}

/*
 * pgbuf_relocate_bottom_lru () - Relocate given BCB into the bottom of LRU list
 *   return: NO_ERROR
 *   bufptr(in): pointer to buffer page
 *
 * Note: This function puts BCB to the bottom of the LRU list.
 */
static int
pgbuf_relocate_bottom_lru (PGBUF_BCB * bufptr)
{
  int lru_idx;
#if defined(SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */

  assert (bufptr->zone != PGBUF_LRU_1_ZONE
	  && bufptr->zone != PGBUF_INVALID_ZONE);

  lru_idx = pgbuf_get_lru_index (&bufptr->vpid);

  /* the caller is holding bufptr->BCB_mutex */
  MUTEX_LOCK_VIA_BUSY_WAIT (rv, pgbuf_Pool.buf_LRU_list[lru_idx].LRU_mutex);

  if (bufptr->zone == PGBUF_LRU_2_ZONE)
    {
      assert (false);

      /* delete bufptr from LRU list */
      if (pgbuf_Pool.buf_LRU_list[lru_idx].LRU_bottom == bufptr)
	{
	  pthread_mutex_unlock (&pgbuf_Pool.buf_LRU_list[lru_idx].LRU_mutex);
	  return NO_ERROR;
	}

      if (pgbuf_Pool.buf_LRU_list[lru_idx].LRU_top == bufptr)
	{
	  pgbuf_Pool.buf_LRU_list[lru_idx].LRU_top = bufptr->next_BCB;
	}

      if (bufptr->next_BCB != NULL)
	{
	  (bufptr->next_BCB)->prev_BCB = bufptr->prev_BCB;
	}

      if (bufptr->prev_BCB != NULL)
	{
	  (bufptr->prev_BCB)->next_BCB = bufptr->next_BCB;
	}
    }

  /* put BCB into the bottom of the LRU list */
  bufptr->next_BCB = NULL;
  bufptr->prev_BCB = pgbuf_Pool.buf_LRU_list[lru_idx].LRU_bottom;
  if (pgbuf_Pool.buf_LRU_list[lru_idx].LRU_bottom == NULL)
    {
      pgbuf_Pool.buf_LRU_list[lru_idx].LRU_top = bufptr;
    }
  else
    {
      (pgbuf_Pool.buf_LRU_list[lru_idx].LRU_bottom)->next_BCB = bufptr;
    }

  pgbuf_Pool.buf_LRU_list[lru_idx].LRU_bottom = bufptr;
  if (pgbuf_Pool.buf_LRU_list[lru_idx].LRU_middle == NULL)
    {
      pgbuf_Pool.buf_LRU_list[lru_idx].LRU_middle = bufptr;
    }

  bufptr->zone = PGBUF_LRU_2_ZONE;	/* OK. */

  pthread_mutex_unlock (&pgbuf_Pool.buf_LRU_list[lru_idx].LRU_mutex);

  return NO_ERROR;
}

/*
 * pgbuf_remove_from_lru_list () - Remove a BCB from the LRU list
 * return : void
 * bufptr (in) : BCB
 *
 *  Note: The caller MUST hold the LRU list mutex.
 */
static void
pgbuf_remove_from_lru_list (PGBUF_BCB * bufptr, PGBUF_LRU_LIST * lru_list)
{
  if (lru_list->LRU_top == bufptr)
    {
      lru_list->LRU_top = bufptr->next_BCB;
    }

  if (lru_list->LRU_bottom == bufptr)
    {
      lru_list->LRU_bottom = bufptr->prev_BCB;
    }

  if (lru_list->LRU_middle == bufptr)
    {
      lru_list->LRU_middle = bufptr->prev_BCB;
    }

  if (bufptr->next_BCB != NULL)
    {
      (bufptr->next_BCB)->prev_BCB = bufptr->prev_BCB;
    }

  if (bufptr->prev_BCB != NULL)
    {
      (bufptr->prev_BCB)->next_BCB = bufptr->next_BCB;
    }

  bufptr->prev_BCB = NULL;
  bufptr->next_BCB = NULL;
}

/*
 * pgbuf_relocate_top_ain () - relocate bcb to top of Ain queue
 * return : int
 * bufptr (in) :
 */
static int
pgbuf_relocate_top_ain (PGBUF_BCB * bufptr)
{
#if defined(SERVER_MODE)
  int rv;
#endif
  PGBUF_AIN_LIST *list;

  /* Ain should only relocate to top new buffers */
  assert (bufptr->zone == PGBUF_VOID_ZONE
	  || (bufptr->zone == PGBUF_AIN_ZONE && bufptr->dirty));

  MUTEX_LOCK_VIA_BUSY_WAIT (rv, pgbuf_Pool.buf_AIN_list.Ain_mutex);

  list = &pgbuf_Pool.buf_AIN_list;

  if (bufptr->zone == PGBUF_AIN_ZONE)
    {
      if (list->Ain_top == bufptr)
	{
	  /* Already at the top */
	  goto cleanup;
	}
      pgbuf_remove_from_ain_list (bufptr);
      list->ain_count -= 1;
    }

  if (list->Ain_top == NULL)
    {
      /* this is the only page in the LRU list */
      assert (list->Ain_bottom == NULL && list->ain_count == 0);

      list->Ain_top = bufptr;
      list->Ain_bottom = bufptr;
    }
  else
    {
      if (list->Ain_top == bufptr)
	{
	  goto cleanup;
	}
      bufptr->prev_BCB = NULL;
      bufptr->next_BCB = list->Ain_top;
      list->Ain_top->prev_BCB = bufptr;
      list->Ain_top = bufptr;
    }

  bufptr->zone = PGBUF_AIN_ZONE;

  /* No sense in verifying Ain count here. We might end up holding a lot more
   * buffers in Ain list than the limit we have set, but this just means that
   * we will be victimize this list more. Hot pages will end up in LRU
   * anyway (eventually).
   */
  list->ain_count += 1;
  bufptr->ain_tick = list->tick;

cleanup:
  list->tick++;
  if (list->tick >= DB_INT32_MAX)
    {
      list->tick = 0;
    }

  pthread_mutex_unlock (&list->Ain_mutex);
  return NO_ERROR;
}

/*
 * pgbuf_move_from_ain_to_lru () - Relocate a BCB from Ain list to top LRU
 * return : void
 * bufptr (in) : BCB
 *
 * Note: this should only be called if the page is dirty. Also, the caller
 * must hold the BCB mutex.
 */
static void
pgbuf_move_from_ain_to_lru (PGBUF_BCB * bufptr)
{
#if defined(SERVER_MODE)
  int rv;
#endif
  PGBUF_AIN_LIST *list;

  /* should only relocate dirty buffers from Ain */
  assert (bufptr->zone == PGBUF_AIN_ZONE);

  MUTEX_LOCK_VIA_BUSY_WAIT (rv, pgbuf_Pool.buf_AIN_list.Ain_mutex);

  list = &pgbuf_Pool.buf_AIN_list;

  if (bufptr->zone == PGBUF_AIN_ZONE)
    {
      pgbuf_remove_from_ain_list (bufptr);
      list->ain_count -= 1;
    }

  pthread_mutex_unlock (&list->Ain_mutex);

  bufptr->zone = PGBUF_VOID_ZONE;

  pgbuf_relocate_top_lru (bufptr, PGBUF_LRU_2_ZONE);
}

/*
 * pgbuf_remove_from_ain_list () - Remove a BCB from the Ain list
 * return : void
 * bufptr (in) : BCB
 *
 *  Note: The caller MUST hold the Ain list mutex. This is a bit confusing
 *    because it looks like we're modifying bufptr. Actually, we're only
 *    removing it from a list, and, since the access to the is synchronized,
 *    nobody can modify the next/prev pointers.
 */
static void
pgbuf_remove_from_ain_list (PGBUF_BCB * bufptr)
{
  PGBUF_AIN_LIST *ain_list = &pgbuf_Pool.buf_AIN_list;

  if (ain_list->Ain_top == bufptr)
    {
      ain_list->Ain_top = bufptr->next_BCB;
    }
  if (ain_list->Ain_bottom == bufptr)
    {
      ain_list->Ain_bottom = bufptr->prev_BCB;
    }

  if (bufptr->next_BCB != NULL)
    {
      bufptr->next_BCB->prev_BCB = bufptr->prev_BCB;
    }
  if (bufptr->prev_BCB != NULL)
    {
      bufptr->prev_BCB->next_BCB = bufptr->next_BCB;
    }

  bufptr->prev_BCB = NULL;
  bufptr->next_BCB = NULL;
}

/*
 * pgbuf_add_vpid_to_aout_list () - add VPID to Aout list
 * return : void
 * thread_p (in) :
 * vpid (in) :
 */
static void
pgbuf_add_vpid_to_aout_list (THREAD_ENTRY * thread_p, const VPID * vpid)
{
#if defined(SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */
  PGBUF_AOUT_LIST *list;
  PGBUF_AOUT_BUF *aout_buf;
  int hash_idx = 0;

  if (pgbuf_Pool.buf_AOUT_list.max_count <= 0)
    {
      return;
    }

  list = &pgbuf_Pool.buf_AOUT_list;

  MUTEX_LOCK_VIA_BUSY_WAIT (rv, pgbuf_Pool.buf_AOUT_list.Aout_mutex);

  if (list->Aout_free == NULL)
    {
      assert (list->Aout_bottom != NULL);
      /* disconnect the bottom */
      aout_buf = list->Aout_bottom;
      if (list->Aout_bottom->prev == NULL)
	{
	  assert (false);
	}
      list->Aout_bottom = list->Aout_bottom->prev;
      list->Aout_bottom->next = NULL;

      /* also remove entry from hash table */
      hash_idx = AOUT_HASH_IDX (&aout_buf->vpid, list);
      mht_rem (list->aout_buf_ht[hash_idx], &aout_buf->vpid, NULL, NULL);
    }
  else
    {
      aout_buf = list->Aout_free;
      list->Aout_free = list->Aout_free->next;
    }

  aout_buf->next = NULL;
  aout_buf->prev = NULL;
  VPID_COPY (&aout_buf->vpid, vpid);

  /* add to hash */
  hash_idx = AOUT_HASH_IDX (&aout_buf->vpid, list);
  mht_put (list->aout_buf_ht[hash_idx], &aout_buf->vpid, aout_buf);

  if (list->Aout_top == NULL)
    {
      /* this is the only page in the Aout list */
      assert (list->Aout_bottom == NULL);

      aout_buf->next = NULL;
      aout_buf->prev = NULL;

      list->Aout_top = aout_buf;
      list->Aout_bottom = aout_buf;
    }
  else
    {
      aout_buf->next = list->Aout_top;
      list->Aout_top->prev = aout_buf;
      list->Aout_top = aout_buf;
    }

  pthread_mutex_unlock (&pgbuf_Pool.buf_AOUT_list.Aout_mutex);

}

/*
 * pgbuf_remove_vpid_from_aout_list () - Search for VPID in Aout and remove it
 *					 from the queue
 * return : true if found, false otherwise
 * thread_p (in) :
 * vpid (in) :
 */
static bool
pgbuf_remove_vpid_from_aout_list (THREAD_ENTRY * thread_p, const VPID * vpid)
{
#if defined(SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */
  PGBUF_AOUT_BUF *aout_buf;
  int hash_idx;

  if (pgbuf_Pool.buf_AOUT_list.max_count <= 0)
    {
      /* Aout list not used */
      return false;
    }

  hash_idx = AOUT_HASH_IDX (vpid, (&pgbuf_Pool.buf_AOUT_list));

  aout_buf = mht_get (pgbuf_Pool.buf_AOUT_list.aout_buf_ht[hash_idx], vpid);
  if (aout_buf == NULL)
    {
      /* vpid not in Aout */
      return false;
    }

  /* Remove it from Aout. We were optimistic and assumed we will not find it.
   * Unfortunately, after we get exclusive access to Aout list, we have to
   * search for it again because somebody else might have thrown it out.
   */
  MUTEX_LOCK_VIA_BUSY_WAIT (rv, pgbuf_Pool.buf_AOUT_list.Aout_mutex);
  if (!VPID_EQ (&aout_buf->vpid, vpid))
    {
      /* Somebody else pushed our vpid out of the list. Search again */
      aout_buf = mht_get (pgbuf_Pool.buf_AOUT_list.aout_buf_ht[hash_idx],
			  vpid);
      if (aout_buf == NULL)
	{
	  /* Not there anymore, just return */
	  pthread_mutex_unlock (&pgbuf_Pool.buf_AOUT_list.Aout_mutex);
	  return false;
	}
    }

  /* We can assume that aout_buf is what we're looking for if it still has the
   * same VPID as before acquiring the mutex. The reason for this is that
   * nobody can change it while we're holding the mutex. Any changes must
   * be visible before we acquire this mutex
   */

  if (aout_buf == pgbuf_Pool.buf_AOUT_list.Aout_bottom)
    {
      pgbuf_Pool.buf_AOUT_list.Aout_bottom =
	pgbuf_Pool.buf_AOUT_list.Aout_bottom->prev;

      if (pgbuf_Pool.buf_AOUT_list.Aout_bottom != NULL)
	{
	  pgbuf_Pool.buf_AOUT_list.Aout_bottom->next = NULL;
	}
      aout_buf->prev = NULL;
    }

  if (aout_buf == pgbuf_Pool.buf_AOUT_list.Aout_top)
    {
      pgbuf_Pool.buf_AOUT_list.Aout_top =
	pgbuf_Pool.buf_AOUT_list.Aout_top->next;

      if (pgbuf_Pool.buf_AOUT_list.Aout_top != NULL)
	{
	  pgbuf_Pool.buf_AOUT_list.Aout_top->prev = NULL;
	}
      aout_buf->next = NULL;
    }

  if (aout_buf->prev != NULL)
    {
      aout_buf->prev->next = aout_buf->next;
    }
  if (aout_buf->next != NULL)
    {
      aout_buf->next->prev = aout_buf->prev;
    }

  /* remove vpid from hash */
  mht_rem (pgbuf_Pool.buf_AOUT_list.aout_buf_ht[hash_idx], vpid, NULL, NULL);

  /* add to free list */
  VPID_SET_NULL (&aout_buf->vpid);
  aout_buf->next = NULL;
  aout_buf->prev = NULL;

  aout_buf->next = pgbuf_Pool.buf_AOUT_list.Aout_free;
  pgbuf_Pool.buf_AOUT_list.Aout_free = aout_buf;

  pthread_mutex_unlock (&pgbuf_Pool.buf_AOUT_list.Aout_mutex);

  return true;
}

/*
 * pgbuf_flush_page_with_wal () - Writes the buffer image into the disk
 *   return: NO_ERROR, or ER_code
 *   bufptr(in): pointer to buffer page
 *
 * Note: When a page is about to be copied to the disk, must first force all
 *       the log records up to and including the page's LSN.
 *       After flushing, remove the page from the dirty pages table.
 */
static int
pgbuf_flush_page_with_wal (THREAD_ENTRY * thread_p, PGBUF_BCB * bufptr)
{
#if defined(SERVER_MODE)
  THREAD_ENTRY *thrd_entry;
  int rv;
#endif /* SERVER_MODE */
  char page_buf[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT];
  FILEIO_PAGE *iopage;
  LOG_LSA oldest_unflush_lsa;
#if defined(ENABLE_SYSTEMTAP)
  int tran_index;
  QMGR_TRAN_ENTRY *tran_entry;
  QUERY_ID query_id = -1;
  bool monitored = false;
#endif /* ENABLE_SYSTEMTAP */

  /* the caller is holding bufptr->BCB_mutex */

  assert (bufptr->latch_mode != PGBUF_LATCH_VICTIM);
  assert (bufptr->latch_mode != PGBUF_LATCH_VICTIM_INVALID);

  assert (bufptr->latch_mode == PGBUF_NO_LATCH
	  || bufptr->latch_mode == PGBUF_LATCH_FLUSH
	  || bufptr->latch_mode == PGBUF_LATCH_READ
	  || bufptr->latch_mode == PGBUF_LATCH_WRITE);

  if (pgbuf_check_bcb_page_vpid (thread_p, bufptr) != true)
    {
      return ER_FAILED;
    }

  bufptr->avoid_victim = true;

  bufptr->async_flush_request = false;

  iopage = (FILEIO_PAGE *) PTR_ALIGN (page_buf, MAX_ALIGNMENT);

  memcpy ((void *) iopage, (void *) (&bufptr->iopage_buffer->iopage),
	  IO_PAGESIZE);
  bufptr->dirty = false;
  LSA_COPY (&oldest_unflush_lsa, &bufptr->oldest_unflush_lsa);
  LSA_SET_NULL (&bufptr->oldest_unflush_lsa);

  pthread_mutex_unlock (&bufptr->BCB_mutex);

  /* confirm WAL protocol */
  /* force log record to disk */
  logpb_flush_log_for_wal (thread_p, &iopage->prv.lsa);

  /* Record number of writes in statistics */
  mnt_pb_iowrites (thread_p, 1);

#if defined(ENABLE_SYSTEMTAP)
  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tran_entry = qmgr_get_tran_entry (tran_index);
  if (tran_entry->query_entry_list_p)
    {
      query_id = tran_entry->query_entry_list_p->query_id;
      monitored = true;
      CUBRID_IO_WRITE_START (query_id);
    }
#endif /* ENABLE_SYSTEMTAP */

  /* now, flush buffer page */
  if (fileio_write (thread_p,
		    fileio_get_volume_descriptor (bufptr->vpid.volid),
		    iopage, bufptr->vpid.pageid, IO_PAGESIZE) == NULL)
    {
      MUTEX_LOCK_VIA_BUSY_WAIT (rv, bufptr->BCB_mutex);
      bufptr->dirty = true;
      LSA_COPY (&bufptr->oldest_unflush_lsa, &oldest_unflush_lsa);

      bufptr->avoid_victim = false;

#if defined(ENABLE_SYSTEMTAP)
      if (monitored == true)
	{
	  CUBRID_IO_WRITE_END (query_id, IO_PAGESIZE, 1);
	}
#endif /* ENABLE_SYSTEMTAP */

      return ER_FAILED;
    }

#if defined(ENABLE_SYSTEMTAP)
  if (monitored == true)
    {
      CUBRID_IO_WRITE_END (query_id, IO_PAGESIZE, 0);
    }
#endif /* ENABLE_SYSTEMTAP */

  assert (bufptr->latch_mode != PGBUF_LATCH_VICTIM);

  MUTEX_LOCK_VIA_BUSY_WAIT (rv, bufptr->BCB_mutex);

  bufptr->avoid_victim = false;

#if defined(SERVER_MODE)
  /* wakeup blocked flushers */
  while (((thrd_entry = bufptr->next_wait_thrd) != NULL)
	 && (thrd_entry->request_latch_mode == PGBUF_LATCH_FLUSH))
    {
      bufptr->next_wait_thrd = thrd_entry->next_wait_thrd;
      thrd_entry->next_wait_thrd = NULL;
      pgbuf_wakeup_uncond (thrd_entry);
    }
#endif /* SERVER_MODE */

  return NO_ERROR;
}

/*
 * pgbuf_is_exist_blocked_reader_writer () - checks whether there exists any
 *                                           blocked reader/writer
 *   return: if found, true, otherwise, false
 *   bufptr(in): pointer to buffer page
 */
static bool
pgbuf_is_exist_blocked_reader_writer (PGBUF_BCB * bufptr)
{
#if defined(SERVER_MODE)
  THREAD_ENTRY *thrd_entry;

  /* check whether there exists any blocked reader/writer */
  thrd_entry = bufptr->next_wait_thrd;
  while (thrd_entry != NULL)
    {
      if (thrd_entry->request_latch_mode == PGBUF_LATCH_READ
	  || thrd_entry->request_latch_mode == PGBUF_LATCH_WRITE)
	{
	  return true;
	}

      thrd_entry = thrd_entry->next_wait_thrd;
    }
#endif /* SERVER_MODE */

  return false;
}

/*
 * pgbuf_is_exist_blocked_reader_writer_victim () - Checks whether there exists
 * 					any blocked reader/writer/victim request
 *   return: if found, true, otherwise, false
 *   bufptr(in): pointer to buffer page
 */
static bool
pgbuf_is_exist_blocked_reader_writer_victim (PGBUF_BCB * bufptr)
{
#if defined(SERVER_MODE)
  THREAD_ENTRY *thrd_entry;

  assert (bufptr->latch_mode != PGBUF_LATCH_VICTIM);
  assert (bufptr->latch_mode != PGBUF_LATCH_VICTIM_INVALID);

  /* check whether there exists any blocked reader/writer */
  thrd_entry = bufptr->next_wait_thrd;
  while (thrd_entry != NULL)
    {
      assert (thrd_entry->request_latch_mode != PGBUF_LATCH_VICTIM);

      if (thrd_entry->request_latch_mode == PGBUF_LATCH_READ
	  || thrd_entry->request_latch_mode == PGBUF_LATCH_WRITE
	  || thrd_entry->request_latch_mode == PGBUF_LATCH_VICTIM)
	{
	  return true;
	}
      thrd_entry = thrd_entry->next_wait_thrd;
    }
#endif /* SERVER_MODE */

  return false;
}

#if defined(SERVER_MODE)
/*
 * pgbuf_kickoff_blocked_victim_request () - Disconnects blocked victim request
 *                                           from the waiting queue of given
 *                                           buffer
 *   return: pointer to thread entry
 *   bufptr(in):  pointer to buffer page
 */
static THREAD_ENTRY *
pgbuf_kickoff_blocked_victim_request (PGBUF_BCB * bufptr)
{
  THREAD_ENTRY *prev_thrd_entry;
  THREAD_ENTRY *thrd_entry;

  assert (bufptr->latch_mode != PGBUF_LATCH_VICTIM);
  assert (bufptr->latch_mode != PGBUF_LATCH_VICTIM_INVALID);

  /* check whether there exists any blocked victim request */
  prev_thrd_entry = NULL;
  thrd_entry = bufptr->next_wait_thrd;
  while (thrd_entry != NULL)
    {
      assert (thrd_entry->request_latch_mode != PGBUF_LATCH_VICTIM);

      if (thrd_entry->request_latch_mode == PGBUF_LATCH_VICTIM)
	{
	  /* found the blocked victim request */
	  if (prev_thrd_entry == NULL)
	    {
	      bufptr->next_wait_thrd = thrd_entry->next_wait_thrd;
	    }
	  else
	    {
	      prev_thrd_entry->next_wait_thrd = thrd_entry->next_wait_thrd;
	    }

	  thrd_entry->next_wait_thrd = NULL;
	  thrd_entry->victim_request_fail = true;
	  break;
	}

      prev_thrd_entry = thrd_entry;
      thrd_entry = thrd_entry->next_wait_thrd;
    }

  return thrd_entry;
}
#endif /* SERVER_MODE */

/*
 * pgbuf_get_check_page_validation -
 *   return:
 *
 */
static bool
pgbuf_get_check_page_validation (THREAD_ENTRY * thread_p,
				 int page_validation_level)
{
#if !defined(NDEBUG)
  if (prm_get_integer_value (PRM_ID_PB_DEBUG_PAGE_VALIDATION_LEVEL) >=
      page_validation_level)
    {
      if (thread_get_check_page_validation (thread_p) == true)
	{
	  return true;
	}
    }
#endif

  return false;
}

/*
 * pgbuf_is_valid_page () - Verify if given page is a valid one
 *   return: either: DISK_INVALID, DISK_VALID, DISK_ERROR
 *   vpid(in): Complete Page identifier
 *   fun(in): A second function to call to verify if the above page is valid
 *            The function is called on vpid, and arguments
 *   args(in): Additional argument for fun
 *
 * Note: Verify that the given page is valid according to functions:
 *         1) disk_isvalid_page
 *         2) given fun2 is any
 *       The function is a NOOP if we are not running with full debugging
 *       capacbilities.
 */
DISK_ISVALID
pgbuf_is_valid_page (THREAD_ENTRY * thread_p, const VPID * vpid,
		     bool no_error, DISK_ISVALID (*fun) (const VPID * vpid,
							 void *args),
		     void *args)
{
  DISK_ISVALID valid;

  if (fileio_get_volume_label (vpid->volid, PEEK) == NULL
      || VPID_ISNULL (vpid))
    {
      assert (no_error);

      return DISK_INVALID;
    }

  valid = disk_isvalid_page (thread_p, vpid->volid, vpid->pageid);
  if (valid != DISK_VALID
      || (fun != NULL && (valid = (*fun) (vpid, args)) != DISK_VALID))
    {
      if (valid != DISK_ERROR && !no_error)
	{
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_PB_BAD_PAGEID, 2, vpid->pageid,
		  fileio_get_volume_label (vpid->volid, PEEK));

	  assert (false);
	}
    }

  return valid;
}

/*
 * pgbuf_is_valid_page_ptr () - Validate an in-memory page pointer
 *   return: true/false
 *   pgptr(in): Pointer to page
 *
 * Note: Verify if the given page pointer points to the beginning of a
 *       in-memory page pointer. This function is used for debugging purposes.
 */
static bool
pgbuf_is_valid_page_ptr (const PAGE_PTR pgptr)
{
  PGBUF_BCB *bufptr;
  int bufid;
#if defined(SERVER_MODE)
  int rv;
#endif

  assert (pgptr != NULL);

  /* NOTE: Does not need to hold BCB_mutex since the page is fixed */
  for (bufid = 0; bufid < pgbuf_Pool.num_buffers; bufid++)
    {
      bufptr = PGBUF_FIND_BCB_PTR (bufid);
      MUTEX_LOCK_VIA_BUSY_WAIT (rv, bufptr->BCB_mutex);

      if (((PAGE_PTR) (&(bufptr->iopage_buffer->iopage.page[0]))) == pgptr)
	{
	  if (bufptr->fcnt <= 0)
	    {
	      /* This situation must not be occurred. */
	      assert (false);
	      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_PB_UNFIXED_PAGEPTR, 3, pgptr,
		      bufptr->vpid.pageid,
		      fileio_get_volume_label (bufptr->vpid.volid, PEEK));
	      pthread_mutex_unlock (&bufptr->BCB_mutex);

	      return false;
	    }
	  else
	    {
	      pthread_mutex_unlock (&bufptr->BCB_mutex);

	      return true;
	    }
	}
      else
	{
	  pthread_mutex_unlock (&bufptr->BCB_mutex);
	}
    }

  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_PB_UNKNOWN_PAGEPTR, 1,
	  pgptr);

  assert (false);

  return false;
}

/*
 * pgbuf_check_page_ptype () -
 *   return: true/false
 *   bufptr(in): pointer to buffer page
 *   ptype(in): page type
 *
 * Note: Verify if the given page's ptype is valid.
 *       This function is used for debugging purposes.
 */
bool
pgbuf_check_page_ptype (THREAD_ENTRY * thread_p, PAGE_PTR pgptr,
			PAGE_TYPE ptype)
{
  PGBUF_BCB *bufptr;

  if (pgptr == NULL)
    {
      assert (false);
      return false;
    }

#if 1				/* TODO - do not delete me */
#if defined(NDEBUG)
  if (log_is_in_crash_recovery ())
    {
      return true;
    }
#endif
#endif

  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  if (pgbuf_get_check_page_validation (thread_p,
				       PGBUF_DEBUG_PAGE_VALIDATION_ALL))
    {
      if (pgbuf_is_valid_page_ptr (pgptr) == false)
	{
	  return false;
	}
    }

  /* NOTE: Does not need to hold BCB_mutex since the page is fixed */

  CAST_PGPTR_TO_BFPTR (bufptr, pgptr);
  assert (!VPID_ISNULL (&bufptr->vpid));

  if (pgbuf_check_bcb_page_vpid (thread_p, bufptr) == true)
    {
      if (bufptr->iopage_buffer->iopage.prv.ptype != PAGE_UNKNOWN
	  && bufptr->iopage_buffer->iopage.prv.ptype != ptype)
	{
	  assert_release (false);
	  return false;
	}
    }
  else
    {
      assert_release (false);
      return false;
    }

  return true;
}

/*
 * pgbuf_check_bcb_page_vpid () - Validate an FILEIO_PAGE prv
 *   return: true/false
 *   bufptr(in): pointer to buffer page
 *
 * Note: Verify if the given page's prv is valid.
 *       This function is used for debugging purposes.
 */
static bool
pgbuf_check_bcb_page_vpid (THREAD_ENTRY * thread_p, PGBUF_BCB * bufptr)
{
  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  if (bufptr == NULL || VPID_ISNULL (&bufptr->vpid))
    {
      assert (bufptr != NULL);
      assert (!VPID_ISNULL (&bufptr->vpid));
      return false;
    }

  /* perm volume */
  if (bufptr->vpid.volid > NULL_VOLID)
    {
      /* Check Page identifier */
      assert (bufptr->vpid.pageid ==
	      bufptr->iopage_buffer->iopage.prv.pageid);
      assert (bufptr->vpid.volid == bufptr->iopage_buffer->iopage.prv.volid);

#if 1				/* TODO - do not delete me */
      assert (bufptr->iopage_buffer->iopage.prv.pflag_reserve_1 == '\0');
      assert (bufptr->iopage_buffer->iopage.prv.p_reserve_2 == 0);
      assert (bufptr->iopage_buffer->iopage.prv.p_reserve_3 == 0);
#endif

      return (bufptr->vpid.pageid == bufptr->iopage_buffer->iopage.prv.pageid
	      && bufptr->vpid.volid ==
	      bufptr->iopage_buffer->iopage.prv.volid);
    }
  else
    {
      return true;		/* nop */
    }
}

#if defined(CUBRID_DEBUG)
/*
 * pgbuf_scramble () - Scramble the content of the buffer
 *   return: void
 *   iopage(in): Pointer to page portion
 *
 * Note: This is done for debugging reasons to make sure that a user of a
 *       buffer does not assume that buffers are initialized to zero. For safty
 *       reasons, the buffers are initialized to zero, instead of scrambled,
 *       when running in production mode.
 */
static void
pgbuf_scramble (FILEIO_PAGE * iopage)
{
  MEM_REGION_INIT (iopage, IO_PAGESIZE);
  LSA_SET_NULL (&iopage->prv.lsa);

  /* Init Page identifier */
  iopage->prv.pageid = -1;
  iopage->prv.volid = -1;

  iopage->prv.ptype = '\0';
  iopage->prv.pflag_reserve_1 = '\0';
  iopage->prv.p_reserve_2 = 0;
  iopage->prv.p_reserve_3 = 0;
}

/*
 * pgbuf_dump_if_any_fixed () - Dump buffer pool if any page buffer is fixed
 *   return: void
 *
 * Note: This is a debugging function that can be used to verify if buffers
 *       were freed after a set of operations (e.g., a request or a API
 *       function).
 *       This function will not give you good results when there are multiple
 *       users in the system (multiprocessing)
 */
void
pgbuf_dump_if_any_fixed (void)
{
  PGBUF_BCB *bufptr;
  int bufid;
  int consistent = PGBUF_CONTENT_GOOD;
#if defined(SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */

  /* Make sure that each buffer is unfixed and consistent */
  for (bufid = 0; bufid < pgbuf_Pool.num_buffers; bufid++)
    {
      bufptr = PGBUF_FIND_BCB_PTR (bufid);
      MUTEX_LOCK_VIA_BUSY_WAIT (rv, bufptr->BCB_mutex);

      if (bufptr->latch_mode != PGBUF_LATCH_INVALID && bufptr->fcnt > 0)
	{
	  /* The buffer is not unfixed */
	  pthread_mutex_unlock (&bufptr->BCB_mutex);
	  pgbuf_dump ();
	  return;
	}

      consistent = pgbuf_is_consistent (bufptr, 0);
      pthread_mutex_unlock (&bufptr->BCB_mutex);

      if (consistent == PGBUF_CONTENT_BAD)
	{
	  break;
	}
    }

  if (consistent != PGBUF_CONTENT_GOOD)
    {
      pgbuf_dump ();
    }
}

/*
 * pgbuf_dump () - Dump the system area of each buffer
 *   return: void
 *
 * Note: This function is used for debugging purposes
 */
static void
pgbuf_dump (void)
{
  PGBUF_BCB *bufptr;
  int bufid, i;
  int consistent;
  int nfetched = 0;
  int ndirty = 0;
  const char *latch_mode_str, *zone_str, *consistent_str;
#if defined(SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */

  (void) fflush (stderr);
  (void) fflush (stdout);
  (void) fprintf (stdout, "\n\n");
  (void) fprintf (stdout, "Num buffers = %d\n", pgbuf_Pool.num_buffers);

  /* Dump info cached about perm and tmp volume identifiers */
  rv = pthread_mutex_lock (&pgbuf_Pool.volinfo_mutex);
  (void) fprintf (stdout,
		  "Lastperm volid = %d, Num permvols of tmparea = %d\n",
		  pgbuf_Pool.last_perm_volid,
		  pgbuf_Pool.num_permvols_tmparea);

  if (pgbuf_Pool.permvols_tmparea_volids != NULL)
    {
      (void) fprintf (stdout, "Permanent volumes with tmp area: ");
      for (i = 0; i < pgbuf_Pool.num_permvols_tmparea; i++)
	{
	  if (i != 0)
	    {
	      (void) fprintf (stdout, ", ");
	    }
	  (void) fprintf (stdout, "%d",
			  pgbuf_Pool.permvols_tmparea_volids[i]);
	}
      (void) fprintf (stdout, "\n");
    }
  pthread_mutex_unlock (&pgbuf_Pool.volinfo_mutex);

  /* Now, dump all buffer pages */
  (void) fprintf (stdout, " Buf Volid Pageid Fcnt LatchMode D A F        Zone"
		  "      Lsa    consistent Bufaddr   Usrarea\n");

  for (bufid = 0; bufid < pgbuf_Pool.num_buffers; bufid++)
    {
      bufptr = PGBUF_FIND_BCB_PTR (bufid);
      MUTEX_LOCK_VIA_BUSY_WAIT (rv, bufptr->BCB_mutex);

      if (bufptr->fcnt > 0)
	{
	  nfetched++;
	}

      if (bufptr->dirty == true)
	{
	  ndirty++;
	}

      /* check if the content of current buffer page is consistent. */
      consistent = pgbuf_is_consistent (bufptr, 0);
      if (bufptr->dirty == false && bufptr->fcnt == 0
	  && consistent != PGBUF_CONTENT_BAD)
	{
	  pthread_mutex_unlock (&bufptr->BCB_mutex);
	  continue;
	}
      else
	{
	  latch_mode_str =
	    (bufptr->latch_mode == PGBUF_NO_LATCH) ? "No Latch" :
	    (bufptr->latch_mode == PGBUF_LATCH_READ) ? "Read" :
	    (bufptr->latch_mode == PGBUF_LATCH_WRITE) ? "Write" :
	    (bufptr->latch_mode == PGBUF_LATCH_FLUSH) ? "Flush" :
	    (bufptr->latch_mode == PGBUF_LATCH_VICTIM) ? "Victim" :
	    (bufptr->latch_mode == PGBUF_LATCH_FLUSH_INVALID) ? "FlushInv" :
	    (bufptr->latch_mode == PGBUF_LATCH_VICTIM_INVALID) ? "VictimInv" :
	    "Fault";

	  zone_str = ((bufptr->zone == PGBUF_LRU_1_ZONE) ? "LRU_1_Zone" :
		      (bufptr->zone == PGBUF_LRU_2_ZONE) ? "LRU_2_Zone" :
		      (bufptr->zone == PGBUF_INVALID_ZONE) ? "INVALID_Zone" :
		      "VOID_Zone");

	  consistent_str = ((consistent == PGBUF_CONTENT_GOOD) ? "GOOD" :
			    (consistent == PGBUF_CONTENT_BAD) ? "BAD" :
			    "LIKELY BAD");

	  fprintf (stdout, "%4d %5d %6d %4d %9s %1d %1d %1d %11s"
		   " %lld|%4d %10s %p %p-%p\n",
		   bufptr->ipool, bufptr->vpid.volid, bufptr->vpid.pageid,
		   bufptr->fcnt, latch_mode_str, bufptr->dirty,
		   bufptr->avoid_victim, bufptr->async_flush_request,
		   zone_str,
		   (long long) bufptr->iopage_buffer->iopage.prv.lsa.pageid,
		   bufptr->iopage_buffer->iopage.prv.lsa.offset,
		   consistent_str, (void *) bufptr,
		   (void *) (&bufptr->iopage_buffer->iopage.page[0]),
		   (void *) (&bufptr->iopage_buffer->iopage.
			     page[DB_PAGESIZE - 1]));
	}
      pthread_mutex_unlock (&bufptr->BCB_mutex);
    }

  (void) fprintf (stdout, "Number of fetched buffers = %d\n"
		  "Number of dirty buffers = %d\n", nfetched, ndirty);
}

/*
 * pgbuf_is_consistent () - Check if a page is consistent
 *   return:
 *   bufptr(in): Pointer to buffer
 *   likely_bad_after_fixcnt(in): Don't tell me that he page is bad if
 *                                fixcnt is greater that this
 *
 * Note: Consistency rule:
 *         If memory page is dirty, the content of page should be different to
 *         the content of the page on disk, otherwise, page is considered
 *         inconsistent. That is, someone set a page dirty without updating
 *         the page. This rule may fail since a page can be updated with the
 *         same content that the page on disk, however, this is a remote case.
 *
 *         If memory page is not dirty, the content of page should be identical
 *         to the content of the page on disk, otherwise, page is considered
 *         inconsistent. This is the case that someone updates the page without
 *         setting it dirty.
 */
static int
pgbuf_is_consistent (const PGBUF_BCB * bufptr, int likely_bad_after_fixcnt)
{
  int consistent = PGBUF_CONTENT_GOOD;
  FILEIO_PAGE *malloc_io_pgptr;

  /* the caller is holding bufptr->BCB_mutex */
  if (memcmp (PGBUF_FIND_BUFFER_GUARD (bufptr), pgbuf_Guard,
	      sizeof (pgbuf_Guard)) != 0)
    {
      er_log_debug (ARG_FILE_LINE, "SYSTEM ERROR"
		    "buffer of pageid = %d|%d has been OVER RUN",
		    bufptr->vpid.volid, bufptr->vpid.pageid);
      return PGBUF_CONTENT_BAD;
    }

  if (!VPID_ISNULL (&bufptr->vpid))
    {
      malloc_io_pgptr = (FILEIO_PAGE *) malloc (IO_PAGESIZE);
      if (malloc_io_pgptr == NULL)
	{
	  return consistent;
	}

      /* Read the disk page into local page area */
      if (fileio_read
	  (NULL, fileio_get_volume_descriptor (bufptr->vpid.volid),
	   malloc_io_pgptr, bufptr->vpid.pageid, IO_PAGESIZE) == NULL)
	{
	  /* Unable to verify consistency of this page */
	  consistent = PGBUF_CONTENT_BAD;
	}
      else
	{
	  /* If page is dirty, it should be different from the one on disk */
	  if (!LSA_EQ (&malloc_io_pgptr->prv.lsa,
		       &bufptr->iopage_buffer->iopage.prv.lsa)
	      || memcmp (malloc_io_pgptr->page,
			 bufptr->iopage_buffer->iopage.page,
			 DB_PAGESIZE) != 0)
	    {
	      consistent = ((bufptr->dirty == true) ?
			    PGBUF_CONTENT_GOOD : PGBUF_CONTENT_BAD);

	      /* If fix count is greater than likely_bad_after_fixcnt,
	         the function cannot state that the page is bad */
	      if (consistent == PGBUF_CONTENT_BAD
		  && bufptr->fcnt > likely_bad_after_fixcnt)
		{
		  consistent = PGBUF_CONTENT_LIKELY_BAD;
		}
	    }
	  else
	    {
	      consistent = ((bufptr->dirty == true) ?
			    PGBUF_CONTENT_LIKELY_BAD : PGBUF_CONTENT_GOOD);
	    }
	}
      free_and_init (malloc_io_pgptr);
    }
  else
    {
      if (bufptr->fcnt <= 0
	  && pgbuf_get_check_page_validation (NULL,
					      PGBUF_DEBUG_PAGE_VALIDATION_ALL))
	{
	  int i;
	  /* The page should be scrambled, otherwise some one step on it */
	  for (i = 0; i < DB_PAGESIZE; i++)
	    {
	      if (bufptr->iopage_buffer->iopage.page[i]
		  != MEM_REGION_SCRAMBLE_MARK)
		{
		  /* The page has been stepped by someone */
		  consistent = PGBUF_CONTENT_BAD;
		  break;
		}
	    }
	}
    }

  /* The I/O executed for pgbuf_is_consistent is not recorded... */
  return consistent;
}
#endif /* CUBRID_DEBUG */

#if defined(PAGE_STATISTICS)
/*
 * pgbuf_initialize_statistics () -
 *   return:
 *   void(in):
 */
static int
pgbuf_initialize_statistics (void)
{
  int volid = -1, pageid = -1;
  PGBUF_PAGE_STAT *ps;
  PGBUF_VOL_STAT *vs;

  fprintf (stderr, "o Initialize page statistics structure\n");

  if (ps_info.ps_init_called == 1)
    {
      pgbuf_finalize_statistics ();
    }

  ps_info.nvols = xboot_find_number_permanent_volumes ();
  fprintf (stderr, "o ps_info.nvols = %d\n", ps_info.nvols);
  if (ps_info.nvols == 0)
    {
      return 0;
    }

  ps_info.vol_stat =
    (PGBUF_VOL_STAT *) malloc (sizeof (PGBUF_VOL_STAT) * ps_info.nvols);
  if (ps_info.vol_stat == NULL)
    {
      return -1;
    }

  for (volid = LOG_DBFIRST_VOLID; volid < ps_info.nvols; volid++)
    {
      vs = &ps_info.vol_stat[volid];
      vs->volid = volid;
      vs->npages = xdisk_get_total_numpages (volid);
      fprintf (stderr, "volid(%d) : npages(%d)\n", vs->volid, vs->npages);

      vs->page_stat =
	(PGBUF_PAGE_STAT *) malloc (sizeof (PGBUF_PAGE_STAT) * vs->npages);
      if (vs->page_stat == NULL)
	{
	  return -1;
	}

      for (pageid = 0; pageid < vs->npages; pageid++)
	{
	  ps = &vs->page_stat[pageid];
	  memset (ps, 0, sizeof (PGBUF_PAGE_STAT));
	  ps->volid = volid;
	  ps->pageid = pageid;
	}
    }

  ps_info.ps_init_called = 1;
  return 0;
}

/*
 * pgbuf_finalize_statistics () -
 *   return:
 */
static int
pgbuf_finalize_statistics (void)
{
  int volid = -1, pageid = -1;
  PGBUF_PAGE_STAT *ps;
  PGBUF_VOL_STAT *vs;
  FILE *ps_log;
  char ps_log_filename[128];

  fprintf (stderr, "o Finalize page statistics structure\n");

  sprintf (ps_log_filename, "ps.log", getpid ());
  ps_log = fopen (ps_log_filename, "w");

  if (ps_log != NULL)
    {
      /* write ps_info */
      fwrite (&ps_info, sizeof (ps_info), 1, ps_log);
      fwrite (ps_info.vol_stat, sizeof (PGBUF_VOL_STAT), ps_info.nvols,
	      ps_log);

      for (volid = LOG_DBFIRST_VOLID; volid < ps_info.nvols; volid++)
	{
	  vs = &ps_info.vol_stat[volid];
	  fwrite (vs->page_stat, sizeof (PGBUF_PAGE_STAT), vs->npages,
		  ps_log);
	}

      fclose (ps_log);
    }
  else
    {
      fprintf (stderr, "Cannot create %s\n", ps_log_filename);
    }

  if (ps_info.ps_init_called == 0)
    {
      return 0;
    }

  for (volid = LOG_DBFIRST_VOLID; volid < ps_info.nvols; volid++)
    {
      vs = &ps_info.vol_stat[volid];
      if (vs->page_stat)
	{
	  free_and_init (vs->page_stat);
	}
    }

  if (ps_info.vol_stat)
    {
      free_and_init (ps_info.vol_stat);
    }

  ps_info.nvols = 0;
  ps_info.ps_init_called = 0;

  return 0;
}

/*
 * pgbuf_dump_statistics () -
 *   return:
 *   ps_log(in):
 */
static void
pgbuf_dump_statistics (FILE * ps_log)
{
  int volid = -1, pageid = -1;
  PGBUF_PAGE_STAT *ps;
  PGBUF_VOL_STAT *vs;

  fprintf (stderr, "o Dump page statistics structure\n");

  /* write ps_info */
  fwrite (&ps_info, sizeof (ps_info), 1, ps_log);
  fwrite (ps_info.vol_stat, sizeof (PGBUF_VOL_STAT), ps_info.nvols, ps_log);

  for (volid = LOG_DBFIRST_VOLID; volid < ps_info.nvols; volid++)
    {
      vs = &ps_info.vol_stat[volid];
      fwrite (vs->page_stat, sizeof (PGBUF_PAGE_STAT), vs->npages, ps_log);

      for (pageid = 0; pageid < vs->npages; pageid++)
	{
	  ps = &vs->page_stat[pageid];
	  memset (ps, 0, sizeof (PGBUF_PAGE_STAT));
	  ps->volid = volid;
	  ps->pageid = pageid;
	}
    }
}
#endif /* PAGE_STATISTICS */

#if !defined(NDEBUG)
static void
pgbuf_add_fixed_at (PGBUF_HOLDER * holder, const char *caller_file,
		    int caller_line)
{
  char buf[256];
  const char *p;

  p = (char *) caller_file + strlen (caller_file);
  while (p)
    {
      if (p == caller_file)
	{
	  break;
	}

      if (*p == '/' || *p == '\\')
	{
	  p++;
	  break;
	}

      p--;
    }

  sprintf (buf, "%s:%d ", p, caller_line);
  if (strstr (holder->fixed_at, buf) == NULL)
    {
      strcat (holder->fixed_at, buf);
      holder->fixed_at_size += strlen (buf);
      assert (holder->fixed_at_size < (64 * 1024));
    }

  return;
}
#endif /* NDEBUG */

#if defined(SERVER_MODE)
static int
pgbuf_sleep (THREAD_ENTRY * thread_p, pthread_mutex_t * mutex_p)
{
  int r;

  r = thread_lock_entry (thread_p);
  if (r == NO_ERROR)
    {
      pthread_mutex_unlock (mutex_p);

      r = thread_suspend_wakeup_and_unlock_entry (thread_p,
						  THREAD_PGBUF_SUSPENDED);
    }

  return r;
}

static int
pgbuf_wakeup (THREAD_ENTRY * thread_p)
{
  int r;

  if (thread_p->request_latch_mode != PGBUF_NO_LATCH)
    {
      thread_p->resume_status = THREAD_PGBUF_RESUMED;

      r = pthread_cond_signal (&thread_p->wakeup_cond);
      if (r != 0)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			       ER_CSS_PTHREAD_COND_SIGNAL, 0);
	  thread_unlock_entry (thread_p);
	  return ER_CSS_PTHREAD_COND_SIGNAL;
	}
    }
  else
    {
      er_log_debug (ARG_FILE_LINE,
		    "thread_entry (%d, %ld) already timedout\n",
		    thread_p->tran_index, thread_p->tid);
    }

  r = thread_unlock_entry (thread_p);

  return r;
}

static int
pgbuf_wakeup_uncond (THREAD_ENTRY * thread_p)
{
  int r;

  r = thread_lock_entry (thread_p);
  if (r == NO_ERROR)
    {
      thread_p->resume_status = THREAD_PGBUF_RESUMED;

      r = pthread_cond_signal (&thread_p->wakeup_cond);
      if (r != 0)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			       ER_CSS_PTHREAD_COND_SIGNAL, 0);
	  thread_unlock_entry (thread_p);
	  return ER_CSS_PTHREAD_COND_SIGNAL;
	}

      r = thread_unlock_entry (thread_p);
    }

  return r;
}
#endif /* SERVER_MODE */

static void
pgbuf_set_dirty_buffer_ptr (THREAD_ENTRY * thread_p, PGBUF_BCB * bufptr)
{
  assert (bufptr != NULL);

  if (bufptr->dirty == false)
    {
      bufptr->dirty = true;
    }

  /* Record number of dirties in statistics */
  mnt_pb_dirties (thread_p);
}

/*
 * pgbuf_wakeup_flush_thread () - Wakeup the flushing thread to flush some
 *				  of the dirty pages in buffer pool to disk
 * return : void
 * thread_p (in) :
 */
static void
pgbuf_wakeup_flush_thread (THREAD_ENTRY * thread_p)
{
#if defined(SERVER_MODE)
  if (thread_is_page_flush_thread_available ())
    {
      thread_wakeup_page_flush_thread ();
      return;
    }
#endif

  pgbuf_flush_victim_candidate (thread_p,
				prm_get_float_value
				(PRM_ID_PB_BUFFER_FLUSH_RATIO));
}
