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
#include "lockfree_circular_queue.hpp"
#include "log_append.hpp"
#include "log_manager.h"
#include "log_impl.h"
#include "log_volids.hpp"
#include "transaction_sr.h"
#include "memory_hash.h"
#include "critical_section.h"
#include "perf_monitor.h"
#include "porting_inline.hpp"
#include "environment_variable.h"
#include "thread_daemon.hpp"
#include "thread_entry_task.hpp"
#include "thread_manager.hpp"
#include "list_file.h"
#include "tsc_timer.h"
#include "query_manager.h"
#include "xserver_interface.h"
#include "btree_load.h"
#include "boot_sr.h"
#include "double_write_buffer.h"
#include "resource_tracker.hpp"
#include "tde.h"
#include "show_scan.h"
#include "numeric_opfunc.h"
#include "dbtype.h"

#if defined(SERVER_MODE)
#include "connection_error.h"
#endif /* SERVER_MODE */
#if defined(ENABLE_SYSTEMTAP)
#include "probes.h"
#endif /* ENABLE_SYSTEMTAP */
#include "thread_entry.hpp"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

const VPID vpid_Null_vpid = { NULL_PAGEID, NULL_VOLID };

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
/* TODO: do we need to do this? */
#define pthread_mutex_init(a, b)
#define pthread_mutex_destroy(a)
#define pthread_mutex_lock(a)	0
#define pthread_mutex_unlock(a)
static int rv;
#endif /* !SERVER_MODE */

/* default timeout seconds for infinite wait */
#define PGBUF_TIMEOUT                      300	/* timeout seconds */
#define PGBUF_FIX_COUNT_THRESHOLD           64	/* fix count threshold. used as indicator for hot pages. */

/* size of io page */
#if defined(CUBRID_DEBUG)
#define SIZEOF_IOPAGE_PAGESIZE_AND_GUARD() (IO_PAGESIZE + sizeof (pgbuf_Guard))
#else /* CUBRID_DEBUG */
#define SIZEOF_IOPAGE_PAGESIZE_AND_GUARD() (IO_PAGESIZE)
#endif /* CUBRID_DEBUG */

/* size of one buffer page <BCB, page> */
#define PGBUF_BCB_SIZEOF       (sizeof (PGBUF_BCB))
#define PGBUF_IOPAGE_BUFFER_SIZE \
  ((size_t)(offsetof (PGBUF_IOPAGE_BUFFER, iopage) + \
  SIZEOF_IOPAGE_PAGESIZE_AND_GUARD()))
/* size of buffer hash entry */
#define PGBUF_BUFFER_HASH_SIZEOF       (sizeof (PGBUF_BUFFER_HASH))
/* size of buffer lock record */
#define PGBUF_BUFFER_LOCK_SIZEOF       (sizeof (PGBUF_BUFFER_LOCK))
/* size of one LRU list structure */
#define PGBUF_LRU_LIST_SIZEOF       (sizeof (PGBUF_LRU_LIST))
/* size of BCB holder entry */
#define PGBUF_HOLDER_SIZEOF        (sizeof (PGBUF_HOLDER))
/* size of BCB holder array that is allocated in one time */
#define PGBUF_HOLDER_SET_SIZEOF    (sizeof (PGBUF_HOLDER_SET))
/* size of BCB holder anchor */
#define PGBUF_HOLDER_ANCHOR_SIZEOF (sizeof (PGBUF_HOLDER_ANCHOR))

/* get memory address(pointer) */
#define PGBUF_FIND_BCB_PTR(i) \
  ((PGBUF_BCB *) ((char *) &(pgbuf_Pool.BCB_table[0]) + (PGBUF_BCB_SIZEOF * (i))))

#define PGBUF_FIND_IOPAGE_PTR(i) \
  ((PGBUF_IOPAGE_BUFFER *) ((char *) &(pgbuf_Pool.iopage_table[0]) + (PGBUF_IOPAGE_BUFFER_SIZE * (i))))

#define PGBUF_FIND_BUFFER_GUARD(bufptr) \
  (&bufptr->iopage_buffer->iopage.page[DB_PAGESIZE])

/* macros for casting pointers */
#define CAST_PGPTR_TO_BFPTR(bufptr, pgptr) \
  do { \
    (bufptr) = ((PGBUF_BCB *) ((PGBUF_IOPAGE_BUFFER *) \
      ((char *) pgptr - offsetof (PGBUF_IOPAGE_BUFFER, iopage.page)))->bcb); \
    assert ((bufptr) == (bufptr)->iopage_buffer->bcb); \
  } while (0)

#define CAST_PGPTR_TO_IOPGPTR(io_pgptr, pgptr) \
  do { \
    (io_pgptr) = (FILEIO_PAGE *) ((char *) pgptr - offsetof (FILEIO_PAGE, page)); \
  } while (0)

#define CAST_IOPGPTR_TO_PGPTR(pgptr, io_pgptr) \
  do { \
    (pgptr) = (PAGE_PTR) ((char *) (io_pgptr)->page); \
  } while (0)

#define CAST_BFPTR_TO_PGPTR(pgptr, bufptr) \
  do { \
    assert ((bufptr) == (bufptr)->iopage_buffer->bcb); \
    (pgptr) = ((PAGE_PTR) ((char *) (bufptr->iopage_buffer) + offsetof (PGBUF_IOPAGE_BUFFER, iopage.page))); \
  } while (0)

/* check whether the given volume is auxiliary volume */
#define PGBUF_IS_AUXILIARY_VOLUME(volid) ((volid) < LOG_DBFIRST_VOLID ? true : false)

/************************************************************************/
/* Page buffer zones section                                            */
/************************************************************************/

/* (bcb flags + zone = 2 bytes) + (lru index = 2 bytes); lru index values start from 0. */
/* if that changes, make the right updates here. */
#define PGBUF_LRU_NBITS 16
#define PGBUF_LRU_LIST_MAX_COUNT ((int) 1 << PGBUF_LRU_NBITS)	/* 64k */
#define PGBUF_LRU_INDEX_MASK (PGBUF_LRU_LIST_MAX_COUNT - 1)	/* 0x0000FFFF */

/* PGBUF_ZONE - enumeration with all page buffer zones */
typedef enum
{
  /* zone values start after reserved values for lru indexes */
  /* LRU zones explained:
   * 1. This is hottest zone and this is where most fixed/unfixed bcb's are found. We'd like to keep the page unfix
   *    complexity to a minimum, therefore no boost to top are done here. This zone's bcb's cannot be victimized.
   * 2. This is a buffer between the hot lru 1 zone and the victimization lru 3 zone. The buffer zone gives bcb's that
   *    fall from first zone a chance to be boosted back to top (if they are still hot). Victimization is still not
   *    allowed.
   * 3. Third zone is the victimization zone. BCB's can still be boosted if fixed/unfixed, but in aggressive victimizing
   *    systems, non-dirty bcb's rarely survive here.
   */
  PGBUF_LRU_1_ZONE = 1 << PGBUF_LRU_NBITS,
  PGBUF_LRU_2_ZONE = 2 << PGBUF_LRU_NBITS,
  PGBUF_LRU_3_ZONE = 3 << PGBUF_LRU_NBITS,
  /* make sure lru zone mask covers all lru zone values */
  PGBUF_LRU_ZONE_MASK = PGBUF_LRU_1_ZONE | PGBUF_LRU_2_ZONE | PGBUF_LRU_3_ZONE,

  /* other zone values must have a completely different mask than lru zone. so also skip the two bits used for
   * PGBUF_LRU_ZONE_MASK */
  PGBUF_INVALID_ZONE = 1 << (PGBUF_LRU_NBITS + 2),	/* invalid zone */
  PGBUF_VOID_ZONE = 2 << (PGBUF_LRU_NBITS + 2),	/* void zone: temporary zone after reading bcb from disk until and
						 * until adding to a lru list, or after removing from lru list and
						 * until victimizing. */

  /* zone mask should cover all zone values */
  PGBUF_ZONE_MASK = (PGBUF_LRU_ZONE_MASK | PGBUF_INVALID_ZONE | PGBUF_VOID_ZONE),
} PGBUF_ZONE;

#define PGBUF_MAKE_ZONE(list_id, zone) ((list_id) | (zone))
#define PGBUF_GET_ZONE(flags) ((PGBUF_ZONE) ((flags) & PGBUF_ZONE_MASK))
#define PGBUF_GET_LRU_INDEX(flags) ((flags) & PGBUF_LRU_INDEX_MASK)

/************************************************************************/
/* Page buffer BCB section                                              */
/************************************************************************/

/* bcb flags */
/* dirty: false initially, is set to true when page is modified. set to false again when flushed to disk. */
#define PGBUF_BCB_DIRTY_FLAG                ((int) 0x80000000)
/* is flushing: set to true when someone intends to flush the bcb to disk. dirty flag is usually set to false, but
 * bcb cannot be yet victimized. flush must succeed first. */
#define PGBUF_BCB_FLUSHING_TO_DISK_FLAG     ((int) 0x40000000)
/* flag to mark bcb was directly victimized. we can have certain situations when victimizations fail. the thread goes
 * to sleep then and waits to be awaken by another thread, which also assigns it a bcb directly. there can be multiple
 * providers of such bcb's.
 * there is a small window of opportunity for active workers to fix this bcb. when fixing a direct victim, we need to
 * replace the flag with PGBUF_BCB_INVALIDATE_DIRECT_VICTIM_FLAG. there is not point of victimizing this bcb to fix it
 * again. The thread waiting for the bcb will know it was fixed again and will request another bcb. */
#define PGBUF_BCB_VICTIM_DIRECT_FLAG        ((int) 0x20000000)
#define PGBUF_BCB_INVALIDATE_DIRECT_VICTIM_FLAG    ((int) 0x10000000)
/* flag for unlatch bcb to move it to the bottom of lru when fix count is 0. usually set when page is deallocated */
#define PGBUF_BCB_MOVE_TO_LRU_BOTTOM_FLAG   ((int) 0x08000000)
/* flag for pages that should be vacuumed. */
#define PGBUF_BCB_TO_VACUUM_FLAG            ((int) 0x04000000)
/* flag for asynchronous flush request */
#define PGBUF_BCB_ASYNC_FLUSH_REQ           ((int) 0x02000000)

/* add all flags here */
#define PGBUF_BCB_FLAGS_MASK \
  (PGBUF_BCB_DIRTY_FLAG \
   | PGBUF_BCB_FLUSHING_TO_DISK_FLAG \
   | PGBUF_BCB_VICTIM_DIRECT_FLAG \
   | PGBUF_BCB_INVALIDATE_DIRECT_VICTIM_FLAG \
   | PGBUF_BCB_MOVE_TO_LRU_BOTTOM_FLAG \
   | PGBUF_BCB_TO_VACUUM_FLAG \
   | PGBUF_BCB_ASYNC_FLUSH_REQ)

/* add flags that invalidate a victim candidate here */
/* 1. dirty bcb's cannot be victimized.
 * 2. bcb's that are in the process of being flushed cannot be victimized. flush must succeed!
 * 3. bcb's that are already assigned as victims are not valid victim candidates.
 */
#define PGBUF_BCB_INVALID_VICTIM_CANDIDATE_MASK \
  (PGBUF_BCB_DIRTY_FLAG \
   | PGBUF_BCB_FLUSHING_TO_DISK_FLAG \
   | PGBUF_BCB_VICTIM_DIRECT_FLAG \
   | PGBUF_BCB_INVALIDATE_DIRECT_VICTIM_FLAG)

/* bcb has no flag initially and is in invalid zone */
#define PGBUF_BCB_INIT_FLAGS PGBUF_INVALID_ZONE

/* fix & avoid dealloc counter... we have one integer and each uses two bytes. fix counter is offset by two bytes. */
#define PGBUF_BCB_COUNT_FIX_SHIFT_BITS          16
#define PGBUF_BCB_AVOID_DEALLOC_MASK            ((int) 0x0000FFFF)

/* Activity on each LRU is probed and cumulated;
 * to avoid long history cumulation effect, the activity indicator is limited (PGBUF_TRAN_MAX_ACTIVITY);
 * Inactivity threshold is defined: private LRU dropping beneath this threshold are destroyed and its BCBs will be
 * victimized.
 */
#define PGBUF_TRAN_THRESHOLD_ACTIVITY (pgbuf_Pool.num_buffers / 4)
#define PGBUF_TRAN_MAX_ACTIVITY (10 * PGBUF_TRAN_THRESHOLD_ACTIVITY)

#define PGBUF_AOUT_NOT_FOUND  -2

#if defined (SERVER_MODE)
/* vacuum workers and checkpoint thread should not contribute to promoting a bcb as active/hot */
#define PGBUF_THREAD_SHOULD_IGNORE_UNFIX(th) VACUUM_IS_THREAD_VACUUM_WORKER (th)
#else
#define PGBUF_THREAD_SHOULD_IGNORE_UNFIX(th) false
#endif

#define HASH_SIZE_BITS 20
#define PGBUF_HASH_SIZE (1 << HASH_SIZE_BITS)

#define PGBUF_HASH_VALUE(vpid) pgbuf_hash_func_mirror(vpid)

/* Maximum overboost flush multiplier: controls the maximum factor to apply to configured flush ratio,
 * when the miss rate (victim_request/fix_request) increases.
 */
#define PGBUF_FLUSH_VICTIM_BOOST_MULT 10

#define PGBUF_NEIGHBOR_FLUSH_NONDIRTY \
  (prm_get_bool_value (PRM_ID_PB_NEIGHBOR_FLUSH_NONDIRTY))

#define PGBUF_MAX_NEIGHBOR_PAGES 32
#define PGBUF_NEIGHBOR_PAGES \
  (prm_get_integer_value (PRM_ID_PB_NEIGHBOR_FLUSH_PAGES))

#define PGBUF_NEIGHBOR_POS(idx) (PGBUF_NEIGHBOR_PAGES - 1 + (idx))

/* maximum number of simultaneous fixes a thread may have on the same page */
#define PGBUF_MAX_PAGE_WATCHERS 64
/* maximum number of simultaneous fixed pages from a single thread */
#define PGBUF_MAX_PAGE_FIXED_BY_TRAN 64

/* max and min flush rate in pages/sec during checkpoint */
#define PGBUF_CHKPT_MAX_FLUSH_RATE  1200
#define PGBUF_CHKPT_MIN_FLUSH_RATE  50

/* default pages to flush in each interval during log checkpoint */
#define PGBUF_CHKPT_BURST_PAGES 16

#define INIT_HOLDER_STAT(perf_stat) \
  do \
    { \
      (perf_stat)->dirty_before_hold = 0; \
      (perf_stat)->dirtied_by_holder = 0; \
      (perf_stat)->hold_has_write_latch = 0; \
      (perf_stat)->hold_has_read_latch = 0; \
    } \
  while (0)

/* use define PGBUF_ORDERED_DEBUG to enable extended debug for ordered fix */
// todo - is it better to replace with a system parameter?
#undef PGBUF_ORDERED_DEBUG

#define PGBUF_LRU_ZONE_MIN_RATIO 0.05f
#define PGBUF_LRU_ZONE_MAX_RATIO 0.90f

/* buffer lock return value */
enum
{
  PGBUF_LOCK_WAITER = 0, PGBUF_LOCK_HOLDER
};

/* constants to indicate the content state of buffers */
enum
{
  PGBUF_CONTENT_BAD = 0,	/* A bug in the system */
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
typedef struct pgbuf_aout_list PGBUF_AOUT_LIST;
typedef struct pgbuf_seq_flusher PGBUF_SEQ_FLUSHER;

typedef struct pgbuf_invalid_list PGBUF_INVALID_LIST;
typedef struct pgbuf_victim_candidate_list PGBUF_VICTIM_CANDIDATE_LIST;

typedef struct pgbuf_buffer_pool PGBUF_BUFFER_POOL;

typedef struct pgbuf_monitor_bcb_mutex PGBUF_MONITOR_BCB_MUTEX;

typedef struct pgbuf_holder_info PGBUF_HOLDER_INFO;

typedef struct pgbuf_status PGBUF_STATUS;
typedef struct pgbuf_status_snapshot PGBUF_STATUS_SNAPSHOT;
typedef struct pgbuf_status_old PGBUF_STATUS_OLD;

struct pgbuf_status
{
  unsigned long long num_hit;
  unsigned long long num_page_request;
  unsigned long long num_pages_created;
  unsigned long long num_pages_written;
  unsigned long long num_pages_read;
  unsigned int num_flusher_waiting_threads;
  unsigned int dummy;
};

struct pgbuf_status_snapshot
{
  unsigned int free_pages;
  unsigned int victim_candidate_pages;
  unsigned int clean_pages;
  unsigned int dirty_pages;
  unsigned int num_index_pages;
  unsigned int num_data_pages;
  unsigned int num_system_pages;
  unsigned int num_temp_pages;
};

struct pgbuf_status_old
{
  unsigned long long num_hit;
  unsigned long long num_page_request;
  unsigned long long num_pages_created;
  unsigned long long num_pages_written;
  unsigned long long num_pages_read;
  time_t print_out_time;
};

struct pgbuf_holder_info
{
  VPID vpid;			/* page to which holder refers */
  PGBUF_ORDERED_GROUP group_id;	/* group (VPID of heap header ) of the page */
  int rank;			/* rank of page (PGBUF_ORDERED_RANK) */
  int watch_count;		/* number of watchers on this holder */
  PGBUF_WATCHER *watcher[PGBUF_MAX_PAGE_WATCHERS];	/* pointers to all watchers to this holder */
  PGBUF_LATCH_MODE latch_mode;	/* aggregate latch mode of all watchers */
  PAGE_TYPE ptype;		/* page type (should be HEAP or OVERFLOW) */
  bool prevent_dealloc;		/* page is prevented from being deallocated. */
};

typedef struct pgbuf_holder_stat PGBUF_HOLDER_STAT;

/* Holder flags used by perf module */
struct pgbuf_holder_stat
{
  unsigned dirty_before_hold:1;	/* page was dirty before holder was acquired */
  unsigned dirtied_by_holder:1;	/* page was dirtied by holder */
  unsigned hold_has_write_latch:1;	/* page has/had write latch */
  unsigned hold_has_read_latch:1;	/* page has/had read latch */
};

typedef struct pgbuf_batch_flush_helper PGBUF_BATCH_FLUSH_HELPER;

struct pgbuf_batch_flush_helper
{
  int npages;
  int fwd_offset;
  int back_offset;
  PGBUF_BCB *pages_bufptr[2 * PGBUF_MAX_NEIGHBOR_PAGES - 1];
  VPID vpids[2 * PGBUF_MAX_NEIGHBOR_PAGES - 1];
};

/* BCB holder entry */
struct pgbuf_holder
{
  int fix_count;		/* the count of fix by the holder */
  PGBUF_BCB *bufptr;		/* pointer to BCB */
  PGBUF_HOLDER *thrd_link;	/* the next BCB holder entry in the BCB holder list of thread */
  PGBUF_HOLDER *next_holder;	/* free BCB holder list of thread */
  PGBUF_HOLDER_STAT perf_stat;
#if !defined(NDEBUG)
  char fixed_at[64 * 1024];
  int fixed_at_size;
#endif				/* NDEBUG */

  int watch_count;
  PGBUF_WATCHER *first_watcher;
  PGBUF_WATCHER *last_watcher;
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
  pthread_mutex_t mutex;	/* BCB mutex */
  int owner_mutex;		/* mutex owner */
#endif				/* SERVER_MODE */
  VPID vpid;			/* Volume and page identifier of resident page */
  int fcnt;			/* Fix count */
  PGBUF_LATCH_MODE latch_mode;	/* page latch mode */
  volatile int flags;
#if defined(SERVER_MODE)
  THREAD_ENTRY *next_wait_thrd;	/* BCB waiting queue */
#endif				/* SERVER_MODE */
  PGBUF_BCB *hash_next;		/* next hash chain */
  PGBUF_BCB *prev_BCB;		/* prev LRU chain */
  PGBUF_BCB *next_BCB;		/* next LRU or Invalid(Free) chain */
  int tick_lru_list;		/* age of lru list when this BCB was inserted into. used to decide when bcb has aged
				 * enough to boost to top. */
  int tick_lru3;		/* position in lru zone 3. small numbers are at the bottom. used to update LRU victim
				 * hint. */
  volatile int count_fix_and_avoid_dealloc;	/* two-purpose field:
						 * 1. count fixes up to a threshold (to detect hot pages).
						 * 2. avoid deallocation count.
						 * we don't use two separate shorts because avoid deallocation needs to
						 * be changed atomically... 2-byte sized atomic operations are not
						 * common. */
  int hit_age;			/* age of last hit (used to compute activities and quotas) */

  LOG_LSA oldest_unflush_lsa;	/* The oldest LSA record of the page that has not been written to disk */
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
 */
struct pgbuf_buffer_hash
{
#if defined(SERVER_MODE)
  pthread_mutex_t hash_mutex;	/* hash mutex for the integrity of buffer hash chain and buffer lock chain. */
#endif				/* SERVER_MODE */
  PGBUF_BCB *hash_next;		/* the anchor of buffer hash chain */
  PGBUF_BUFFER_LOCK *lock_next;	/* the anchor of buffer lock chain */
};

/* buffer LRU list structure : double linked list */
struct pgbuf_lru_list
{
#if defined(SERVER_MODE)
  pthread_mutex_t mutex;	/* LRU mutex for the integrity of LRU list. */
#endif				/* SERVER_MODE */
  PGBUF_BCB *top;		/* top of the LRU list */
  PGBUF_BCB *bottom;		/* bottom of the LRU list */
  PGBUF_BCB *bottom_1;		/* the last of LRU_1_Zone. NULL if lru1 zone is empty */
  PGBUF_BCB *bottom_2;		/* the last of LRU_2_Zone. NULL if lru2 zone is empty */
  PGBUF_BCB *volatile victim_hint;	/* hint to start searching for victims in lru list. everything below the hint
					 * should be dirty, but the hint is not always the first bcb that can be
					 * victimized. */
  /* TODO: I have noticed while investigating core files from TPCC that hint is
   *       sometimes before first bcb that can be victimized. this means there is
   *       a logic error somewhere. I don't know where, but there must be. */

  /* zone counters */
  int count_lru1;
  int count_lru2;
  int count_lru3;

  /* victim candidate counter */
  int count_vict_cand;

  /* zone thresholds. we only need for zones one and two */
  int threshold_lru1;
  int threshold_lru2;

  /* quota (private lists only) */
  int quota;

  /* list tick. incremented when new bcb's are added to the list or when bcb's are boosted to top */
  int tick_list;		/* tick incremented whenever bcb is added or moved in list */
  int tick_lru3;		/* tick incremented whenever bcb's fall to zone three */

  volatile int flags;		/* LRU list flags */

  int index;			/* LRU list index */
};

/* buffer invalid BCB list : single linked list */
struct pgbuf_invalid_list
{
#if defined(SERVER_MODE)
  pthread_mutex_t invalid_mutex;	/* invalid mutex for the integrity of invalid BCB list. */
#endif				/* SERVER_MODE */
  PGBUF_BCB *invalid_top;	/* top of the invalid BCB list */
  int invalid_cnt;		/* # of entries in invalid BCB list */
};

/* The page replacement algorithm is LRU + Aout of 2Q. This algorithm uses two linked lists as follows:
 *  - LRU list: this is a list of BCBs managed as a Least Recently Used queue
 *  - Aout list: this is a list on VPIDs managed as a FIFO queue
 * The LRU list manages the "hot" pages, Aout list holds a short term history of pages which have been victimized.
 */
/* Aout list node */
struct pgbuf_aout_buf
{
  VPID vpid;			/* page VPID */
  int lru_idx;
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

  PGBUF_AOUT_BUF *bufarray;	/* Array holding all the nodes in the list. Since Aout has a predefined fixed size, it
				 * makes more sense to preallocate all the nodes */
  int num_hashes;		/* number of hash tables */
  MHT_TABLE **aout_buf_ht;	/* hash table for fast history lookup. */

  int max_count;		/* maximum size of the Aout queue */
};

/* Generic structure to manage sequential flush with flush rate control:
 * Flush rate control is achieved by breaking each 1 second into intervals, and attempt to flush an equal number of
 * pages in each interval.
 * Compensation is applied across all intervals in one second to achieve overall flush rate.
 * In each interval, the pages are flushed either in burst mode or equally time spread during the entire interval.
 */
struct pgbuf_seq_flusher
{
  PGBUF_VICTIM_CANDIDATE_LIST *flush_list;	/* flush list */
  LOG_LSA flush_upto_lsa;	/* newest of the oldest LSA record of the pages which will be written to disk */

  int control_intervals_cnt;	/* intervals passed */
  int control_flushed;		/* number of pages flushed since the 1 second super-interval started */

  int interval_msec;		/* duration of one interval */
  int flush_max_size;		/* max size of elements, set only on init */
  int flush_cnt;		/* current count of elements in flush_list */
  int flush_idx;		/* index of current element to flush */
  int flushed_pages;		/* cnt of flushed pages (return parameter) */
  float flush_rate;		/* maximum rate of flushing (negative if none should be used) */

  bool burst_mode;		/* config : flush in burst or flush one page and wait */
};

typedef struct pgbuf_page_monitor PGBUF_PAGE_MONITOR;
struct pgbuf_page_monitor
{
  INT64 dirties_cnt;		/* Number of dirty buffers. */

  int *lru_hits;		/* Current hits in LRU1 per LRU */
  int *lru_activity;		/* Activity level per LRU */

  /* Overall counters */
  volatile int lru_shared_pgs_cnt;	/* count of BCBs in all shared LRUs */
  int pg_unfix_cnt;		/* Count of page unfixes; used for refreshing quota adjustment */
  int lru_victim_req_cnt;	/* number of victim requests from all LRUs */
  int fix_req_cnt;		/* number of fix requests */

#if defined (SERVER_MODE)
  PGBUF_MONITOR_BCB_MUTEX *bcb_locks;	/* track bcb mutex usage. */
#endif				/* SERVER_MODE */

  bool victim_rich;		/* true if page buffer pool has many victims. pgbuf_adjust_quotas will update this
				 * value. */
};

typedef struct pgbuf_page_quota PGBUF_PAGE_QUOTA;
struct pgbuf_page_quota
{
  int num_private_LRU_list;	/* number of private LRU lists */

  /* Real-time tunning: */
  float *lru_victim_flush_priority_per_lru;	/* priority to flush from this LRU */

  int *private_lru_session_cnt;	/* Number of active session for each private LRU:  Contains only private lists ! */
  float private_pages_ratio;	/* Ratio of all private BCBs among total BCBs */

  /* TODO: remove me --> */
  unsigned int add_shared_lru_idx;	/* circular index of shared LRU for relocating to shared */
  int avoid_shared_lru_idx;	/* index of shared LRU to avoid when relocating to shared;
				 * this is usually the index of shared LRU with maximum number of BCBs;
				 * transaction will avoid this list when relocating to shared LRU (like when moving from
				 * a garbage LRU); such LRU list returns to normal size through victimization */

  TSC_TICKS last_adjust_time;
  INT32 adjust_age;
  int is_adjusting;
};

#if defined (SERVER_MODE)
/* PGBUF_DIRECT_VICTIM - system used to optimize the victim assignment without searching and burning CPU uselessly.
 * threads are waiting to be assigned a victim directly and woken up.
 */
typedef struct pgbuf_direct_victim PGBUF_DIRECT_VICTIM;
struct pgbuf_direct_victim
{
  PGBUF_BCB **bcb_victims;
  /* *INDENT-OFF* */
  lockfree::circular_queue<THREAD_ENTRY *> *waiter_threads_high_priority;
  lockfree::circular_queue<THREAD_ENTRY *> *waiter_threads_low_priority;
  /* *INDENT-ON* */
};
#define PGBUF_FLUSHED_BCBS_BUFFER_SIZE (8 * 1024)	/* 8k */
#endif /* SERVER_MODE */

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
  int num_LRU_list;		/* number of shared LRU lists */
  float ratio_lru1;		/* ratio for lru 1 zone */
  float ratio_lru2;		/* ratio for lru 2 zone */
  PGBUF_LRU_LIST *buf_LRU_list;	/* LRU lists. When Page quota is enabled, first 'num_LRU_list' store shared pages;
				 * the next 'num_garbage_LRU_list' lists store shared garbage pages;
				 * the last 'num_private_LRU_list' are private lists.
				 * When page quota is disabled only shared lists are used */
  PGBUF_AOUT_LIST buf_AOUT_list;	/* Aout list */
  PGBUF_INVALID_LIST buf_invalid_list;	/* buffer invalid BCB list */

  PGBUF_VICTIM_CANDIDATE_LIST *victim_cand_list;
  PGBUF_SEQ_FLUSHER seq_chkpt_flusher;

  PGBUF_PAGE_MONITOR monitor;
  PGBUF_PAGE_QUOTA quota;

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

  /* 'check_for_interrupt' is set true when interrupts must be checked. Log manager set and clears this value while
   * holding TR_TABLE_CS. */
  bool check_for_interrupts;

#if defined(SERVER_MODE)
  bool is_flushing_victims;	/* flag set true when pgbuf flush thread is flushing victim candidates */
  bool is_checkpoint;		/* flag set true when checkpoint is running */
#endif				/* SERVER_MODE */

  /* *INDENT-OFF* */
#if defined (SERVER_MODE)
  PGBUF_DIRECT_VICTIM direct_victims;	/* direct victim assignment */
  lockfree::circular_queue<PGBUF_BCB *> *flushed_bcbs;	/* post-flush processing */
#endif				/* SERVER_MODE */
  lockfree::circular_queue<int> *private_lrus_with_victims;
  lockfree::circular_queue<int> *big_private_lrus_with_victims;
  lockfree::circular_queue<int> *shared_lrus_with_victims;
  /* *INDENT-ON* */

  PGBUF_STATUS *show_status;
  PGBUF_STATUS_OLD show_status_old;
  PGBUF_STATUS_SNAPSHOT show_status_snapshot;
#if defined (SERVER_MODE)
  pthread_mutex_t show_status_mutex;
#endif
};

/* victim candidate list */
/* One daemon thread performs flush task for victim candidates.
 * The daemon finds and saves victim candidates using following list.
 * And then, based on the list, the daemon performs actual flush task.
 */
struct pgbuf_victim_candidate_list
{
  PGBUF_BCB *bufptr;		/* selected BCB as victim candidate */
  VPID vpid;			/* page id of the page managed by the BCB */
};

static PGBUF_BUFFER_POOL pgbuf_Pool;	/* The buffer Pool */
static PGBUF_BATCH_FLUSH_HELPER pgbuf_Flush_helper;

HFID *pgbuf_ordered_null_hfid = NULL;

#if defined(CUBRID_DEBUG)
/* A buffer guard to detect over runs .. */
static char pgbuf_Guard[8] = { MEM_REGION_GUARD_MARK, MEM_REGION_GUARD_MARK, MEM_REGION_GUARD_MARK,
  MEM_REGION_GUARD_MARK,
  MEM_REGION_GUARD_MARK, MEM_REGION_GUARD_MARK, MEM_REGION_GUARD_MARK,
  MEM_REGION_GUARD_MARK
};
#endif /* CUBRID_DEBUG */

#define AOUT_HASH_DIVIDE_RATIO 1000
#define AOUT_HASH_IDX(vpid, list) ((vpid)->pageid % list->num_hashes)

/* pgbuf_monitor_bcb_mutex - debug tool to monitor bcb mutex usage (and leaks). */
struct pgbuf_monitor_bcb_mutex
{
  PGBUF_BCB *bcb;
  PGBUF_BCB *bcb_second;
  int line;
  int line_second;
};
#if defined (SERVER_MODE)
static bool pgbuf_Monitor_locks = false;
#endif /* SERVER_MODE */

#if defined (SERVER_MODE)
#define PGBUF_BCB_LOCK(bcb) \
  (pgbuf_Monitor_locks ? pgbuf_bcbmon_lock (bcb, __LINE__) : (void) pthread_mutex_lock (&(bcb)->mutex))
#define PGBUF_BCB_TRYLOCK(bcb) \
  (pgbuf_Monitor_locks ? pgbuf_bcbmon_trylock (bcb, __LINE__) : pthread_mutex_trylock (&(bcb)->mutex))
#define PGBUF_BCB_UNLOCK(bcb) \
  (pgbuf_Monitor_locks ? pgbuf_bcbmon_unlock (bcb) : (void) pthread_mutex_unlock (&(bcb)->mutex))
#define PGBUF_BCB_CHECK_OWN(bcb) if (pgbuf_Monitor_locks) pgbuf_bcbmon_check_own (bcb)
#define PGBUF_BCB_CHECK_MUTEX_LEAKS() if (pgbuf_Monitor_locks) pgbuf_bcbmon_check_mutex_leaks ()
#else	/* !SERVER_MODE */		   /* SA_MODE */
/* single-threaded does not require mutexes, nor does it need to check them */
#define PGBUF_BCB_LOCK(bcb)
#define PGBUF_BCB_TRYLOCK(bcb) (0)
#define PGBUF_BCB_UNLOCK(bcb)
#define PGBUF_BCB_CHECK_OWN(bcb) (true)
#define PGBUF_BCB_CHECK_MUTEX_LEAKS()
#endif /* SA_MODE */

/* helper to collect performance in page fix functions */
typedef struct pgbuf_fix_perf PGBUF_FIX_PERF;
struct pgbuf_fix_perf
{
  bool is_perf_tracking;
  TSC_TICKS start_tick;
  TSC_TICKS end_tick;
  TSC_TICKS start_holder_tick;
  PERF_PAGE_MODE perf_page_found;
  PERF_HOLDER_LATCH perf_latch_mode;
  PERF_CONDITIONAL_FIX_TYPE perf_cond_type;
  PERF_PAGE_TYPE perf_page_type;
  TSCTIMEVAL tv_diff;
  UINT64 lock_wait_time;
  UINT64 holder_wait_time;
  UINT64 fix_wait_time;
};

/* in FILEIO_PAGE_RESERVED */
typedef struct pgbuf_dealloc_undo_data PGBUF_DEALLOC_UNDO_DATA;
struct pgbuf_dealloc_undo_data
{
  INT32 pageid;			/* Page identifier */
  INT16 volid;			/* Volume identifier where the page reside */
  unsigned char ptype;		/* Page type */
  unsigned char pflag;
};

/************************************************************************/
/* Page buffer LRU section                                              */
/************************************************************************/
#define PGBUF_GET_LRU_LIST(lru_idx) (&pgbuf_Pool.buf_LRU_list[lru_idx])

#define PGBUF_IS_BCB_IN_LRU_VICTIM_ZONE(bcb) (pgbuf_bcb_get_zone (bcb) == PGBUF_LRU_3_ZONE)
#define PGBUF_IS_BCB_IN_LRU(bcb) ((pgbuf_bcb_get_zone (bcb) & PGBUF_LRU_ZONE_MASK) != 0)

/* How old is a BCB (bcb_age) related to age of list to which it belongs */
#define PGBUF_AGE_DIFF(bcb_age,list_age) \
  (((list_age) >= (bcb_age)) ? ((list_age) - (bcb_age)) : (DB_INT32_MAX - ((bcb_age) - (list_age))))
/* is bcb old enough. we use it as indicator of the buffer lru zone. when bcb falls more than half of this buffer zone,
 * it is considered old */
#define PGBUF_IS_BCB_OLD_ENOUGH(bcb, lru_list) \
  (PGBUF_AGE_DIFF ((bcb)->tick_lru_list, (lru_list)->tick_list) >= ((lru_list)->count_lru2 / 2))
/* zone counts & thresholds */
#define PGBUF_LRU_ZONE_ONE_TWO_COUNT(list) ((list)->count_lru1 + (list)->count_lru2)
#define PGBUF_LRU_LIST_COUNT(list) (PGBUF_LRU_ZONE_ONE_TWO_COUNT(list) + (list)->count_lru3)
#define PGBUF_LRU_VICTIM_ZONE_COUNT(list) ((list)->count_lru3)

#define PGBUF_LRU_IS_ZONE_ONE_OVER_THRESHOLD(list) ((list)->threshold_lru1 < (list)->count_lru1)
#define PGBUF_LRU_IS_ZONE_TWO_OVER_THRESHOLD(list) ((list)->threshold_lru2 < (list)->count_lru2)
#define PGBUF_LRU_ARE_ZONES_ONE_TWO_OVER_THRESHOLD(list) \
  ((list)->threshold_lru1 + (list)->threshold_lru2 < PGBUF_LRU_ZONE_ONE_TWO_COUNT(list))

/* macros for retrieving info on shared and private LRUs */

/* Limits for private chains */
#define PGBUF_PRIVATE_LRU_MIN_COUNT 4
#define PGBUF_PRIVATE_LRU_MAX_HARD_QUOTA 5000

/* Lower limit for number of pages in shared LRUs: used to compute number of private lists and number of shared lists */
#define PGBUF_MIN_PAGES_IN_SHARED_LIST 1000
#define PGBUF_MIN_SHARED_LIST_ADJUST_SIZE 50

#define PGBUF_PAGE_QUOTA_IS_ENABLED (pgbuf_Pool.quota.num_private_LRU_list > 0)

/* macros for retrieving id of private chains of thread (to use actual LRU index use PGBUF_LRU_INDEX_FROM_PRIVATE on
 * this result.
 */
#if defined (SERVER_MODE)
#define PGBUF_PRIVATE_LRU_FROM_THREAD(thread_p) \
  ((thread_p) != NULL) ? ((thread_p)->private_lru_index) : (0)
static bool
PGBUF_THREAD_HAS_PRIVATE_LRU (THREAD_ENTRY * thread_p)
{
  return PGBUF_PAGE_QUOTA_IS_ENABLED && (thread_p) != NULL && (thread_p)->private_lru_index != -1;
}
#else
#define PGBUF_PRIVATE_LRU_FROM_THREAD(thread_p) 0
#define PGBUF_THREAD_HAS_PRIVATE_LRU(thread_p) false
#endif

#define PGBUF_SHARED_LRU_COUNT (pgbuf_Pool.num_LRU_list)
#define PGBUF_PRIVATE_LRU_COUNT (pgbuf_Pool.quota.num_private_LRU_list)
#define PGBUF_TOTAL_LRU_COUNT (PGBUF_SHARED_LRU_COUNT + PGBUF_PRIVATE_LRU_COUNT)

#define PGBUF_PRIVATE_LIST_FROM_LRU_INDEX(i) ((i) - PGBUF_SHARED_LRU_COUNT)
#define PGBUF_LRU_INDEX_FROM_PRIVATE(private_id) (PGBUF_SHARED_LRU_COUNT + (private_id))

#define PGBUF_IS_SHARED_LRU_INDEX(lru_idx) ((lru_idx) < PGBUF_SHARED_LRU_COUNT)
#define PGBUF_IS_PRIVATE_LRU_INDEX(lru_idx) ((lru_idx) >= PGBUF_SHARED_LRU_COUNT)

#define PGBUF_LRU_LIST_IS_OVER_QUOTA(list) (PGBUF_LRU_LIST_COUNT (list) > (list)->quota)
#define PGBUF_LRU_LIST_IS_ONE_TWO_OVER_QUOTA(list) ((PGBUF_LRU_ZONE_ONE_TWO_COUNT (list) > (list)->quota))
#define PGBUF_LRU_LIST_OVER_QUOTA_COUNT(list) (PGBUF_LRU_LIST_COUNT (list) - (list)->quota)

#define PGBUF_IS_PRIVATE_LRU_OVER_QUOTA(lru_idx) \
  (PGBUF_IS_PRIVATE_LRU_INDEX (lru_idx) && PGBUF_LRU_LIST_IS_OVER_QUOTA (PGBUF_GET_LRU_LIST (lru_idx)))
#define PGBUF_IS_PRIVATE_LRU_ONE_TWO_OVER_QUOTA(lru_idx) \
  (PGBUF_IS_PRIVATE_LRU_INDEX (lru_idx) && PGBUF_LRU_LIST_IS_ONE_TWO_OVER_QUOTA (PGBUF_GET_LRU_LIST (lru_idx)))

#define PGBUF_OVER_QUOTA_BUFFER(quota) MAX (10, (int) (quota * 0.01f))
#define PGBUF_LRU_LIST_IS_OVER_QUOTA_WITH_BUFFER(list) \
  (PGBUF_LRU_LIST_COUNT (list) > (list)->quota + PGBUF_OVER_QUOTA_BUFFER ((list)->quota))

#define PBGUF_BIG_PRIVATE_MIN_SIZE 100

/* LRU flags */
#define PGBUF_LRU_VICTIM_LFCQ_FLAG ((int) 0x80000000)

#if defined (NDEBUG)
/* note: release bugs can be hard to debug due to compile optimization. the crash call-stack may point to a completely
 *       different code than the one that caused the crash. my workaround is to save the line of code in this global
 *       variable pgbuf_Abort_release_line.
 *
 *       careful about overusing this. the code may not be fully optimized when using it. */
static int pgbuf_Abort_release_line = 0;
#define PGBUF_ABORT_RELEASE() do { pgbuf_Abort_release_line = __LINE__; abort (); } while (false)
#else /* DEBUG */
#define PGBUF_ABORT_RELEASE() assert (false)
#endif /* DEBUG */

static INLINE unsigned int pgbuf_hash_func_mirror (const VPID * vpid) __attribute__ ((ALWAYS_INLINE));

static INLINE bool pgbuf_is_temporary_volume (VOLID volid) __attribute__ ((ALWAYS_INLINE));
static int pgbuf_initialize_bcb_table (void);
static int pgbuf_initialize_hash_table (void);
static int pgbuf_initialize_lock_table (void);
static int pgbuf_initialize_lru_list (void);
static int pgbuf_initialize_aout_list (void);
static int pgbuf_initialize_invalid_list (void);
static int pgbuf_initialize_page_quota_parameters (void);
static int pgbuf_initialize_page_quota (void);
static int pgbuf_initialize_page_monitor (void);
static int pgbuf_initialize_thrd_holder (void);
STATIC_INLINE PGBUF_HOLDER *pgbuf_allocate_thrd_holder_entry (THREAD_ENTRY * thread_p) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE PGBUF_HOLDER *pgbuf_find_thrd_holder (THREAD_ENTRY * thread_p, PGBUF_BCB * bufptr)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int pgbuf_remove_thrd_holder (THREAD_ENTRY * thread_p, PGBUF_HOLDER * holder)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int pgbuf_unlatch_thrd_holder (THREAD_ENTRY * thread_p, PGBUF_BCB * bufptr,
					     PGBUF_HOLDER_STAT * holder_perf_stat_p) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int pgbuf_unlatch_bcb_upon_unfix (THREAD_ENTRY * thread_p, PGBUF_BCB * bufptr, int holder_status)
  __attribute__ ((ALWAYS_INLINE));
static void pgbuf_unlatch_void_zone_bcb (THREAD_ENTRY * thread_p, PGBUF_BCB * bcb, int thread_private_lru_index);
STATIC_INLINE bool pgbuf_should_move_private_to_shared (THREAD_ENTRY * thread_p, PGBUF_BCB * bcb,
							int thread_private_lru_index) __attribute__ ((ALWAYS_INLINE));
static int pgbuf_block_bcb (THREAD_ENTRY * thread_p, PGBUF_BCB * bufptr, PGBUF_LATCH_MODE request_mode,
			    int request_fcnt, bool as_promote);
STATIC_INLINE int pgbuf_latch_bcb_upon_fix (THREAD_ENTRY * thread_p, PGBUF_BCB * bufptr, PGBUF_LATCH_MODE request_mode,
					    int buf_lock_acquired, PGBUF_LATCH_CONDITION condition,
					    bool * is_latch_wait) __attribute__ ((ALWAYS_INLINE));
static int pgbuf_latch_idle_page (THREAD_ENTRY * thread_p, PGBUF_BCB * bufptr, PGBUF_LATCH_MODE request_mode);

STATIC_INLINE PGBUF_BCB *pgbuf_search_hash_chain (THREAD_ENTRY * thread_p, PGBUF_BUFFER_HASH * hash_anchor,
						  const VPID * vpid) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int pgbuf_insert_into_hash_chain (THREAD_ENTRY * thread_p, PGBUF_BUFFER_HASH * hash_anchor,
						PGBUF_BCB * bufptr) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int pgbuf_delete_from_hash_chain (THREAD_ENTRY * thread_p, PGBUF_BCB * bufptr)
  __attribute__ ((ALWAYS_INLINE));
static int pgbuf_lock_page (THREAD_ENTRY * thread_p, PGBUF_BUFFER_HASH * hash_anchor, const VPID * vpid);
static int pgbuf_unlock_page (THREAD_ENTRY * thread_p, PGBUF_BUFFER_HASH * hash_anchor, const VPID * vpid,
			      int need_hash_mutex);
static PGBUF_BCB *pgbuf_allocate_bcb (THREAD_ENTRY * thread_p, const VPID * src_vpid);
static PGBUF_BCB *pgbuf_claim_bcb_for_fix (THREAD_ENTRY * thread_p, const VPID * vpid, PAGE_FETCH_MODE fetch_mode,
					   PGBUF_BUFFER_HASH * hash_anchor, PGBUF_FIX_PERF * perf, bool * try_again);
static int pgbuf_victimize_bcb (THREAD_ENTRY * thread_p, PGBUF_BCB * bufptr);
static int pgbuf_bcb_safe_flush_internal (THREAD_ENTRY * thread_p, PGBUF_BCB * bufptr, bool synchronous, bool * locked);
static int pgbuf_invalidate_bcb (THREAD_ENTRY * thread_p, PGBUF_BCB * bufptr);
static int pgbuf_bcb_safe_flush_force_lock (THREAD_ENTRY * thread_p, PGBUF_BCB * bufptr, bool synchronous);
static int pgbuf_bcb_safe_flush_force_unlock (THREAD_ENTRY * thread_p, PGBUF_BCB * bufptr, bool synchronous);
static PGBUF_BCB *pgbuf_get_bcb_from_invalid_list (THREAD_ENTRY * thread_p);
static int pgbuf_put_bcb_into_invalid_list (THREAD_ENTRY * thread_p, PGBUF_BCB * bufptr);

STATIC_INLINE int pgbuf_get_shared_lru_index_for_add (void) __attribute__ ((ALWAYS_INLINE));
static int pgbuf_get_victim_candidates_from_lru (THREAD_ENTRY * thread_p, int check_count,
						 float lru_sum_flush_priority, bool * assigned_directly);
static PGBUF_BCB *pgbuf_get_victim (THREAD_ENTRY * thread_p);
static PGBUF_BCB *pgbuf_get_victim_from_lru_list (THREAD_ENTRY * thread_p, const int lru_idx);
#if defined (SERVER_MODE)
static int pgbuf_panic_assign_direct_victims_from_lru (THREAD_ENTRY * thread_p, PGBUF_LRU_LIST * lru_list,
						       PGBUF_BCB * bcb_start);
STATIC_INLINE void pgbuf_lfcq_assign_direct_victims (THREAD_ENTRY * thread_p, int lru_idx, int *nassign_inout)
  __attribute__ ((ALWAYS_INLINE));
#endif /* SERVER_MODE */
STATIC_INLINE void pgbuf_add_vpid_to_aout_list (THREAD_ENTRY * thread_p, const VPID * vpid, const int lru_idx)
  __attribute__ ((ALWAYS_INLINE));
static int pgbuf_remove_vpid_from_aout_list (THREAD_ENTRY * thread_p, const VPID * vpid);
static int pgbuf_remove_private_from_aout_list (const int lru_idx);
STATIC_INLINE void pgbuf_remove_from_lru_list (THREAD_ENTRY * thread_p, PGBUF_BCB * bufptr, PGBUF_LRU_LIST * lru_list)
  __attribute__ ((ALWAYS_INLINE));

STATIC_INLINE void pgbuf_lru_add_bcb_to_top (THREAD_ENTRY * thread_p, PGBUF_BCB * bcb, PGBUF_LRU_LIST * lru_list)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void pgbuf_lru_add_bcb_to_middle (THREAD_ENTRY * thread_p, PGBUF_BCB * bcb, PGBUF_LRU_LIST * lru_list)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void pgbuf_lru_add_bcb_to_bottom (THREAD_ENTRY * thread_p, PGBUF_BCB * bcb, PGBUF_LRU_LIST * lru_list)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void pgbuf_lru_adjust_zone1 (THREAD_ENTRY * thread_p, PGBUF_LRU_LIST * lru_list, bool min_one)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void pgbuf_lru_adjust_zone2 (THREAD_ENTRY * thread_p, PGBUF_LRU_LIST * lru_list, bool min_one)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void pgbuf_lru_adjust_zones (THREAD_ENTRY * thread_p, PGBUF_LRU_LIST * lru_list, bool min_one)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void pgbuf_lru_fall_bcb_to_zone_3 (THREAD_ENTRY * thread_p, PGBUF_BCB * bcb, PGBUF_LRU_LIST * lru_list)
  __attribute__ ((ALWAYS_INLINE));
static void pgbuf_lru_boost_bcb (THREAD_ENTRY * thread_p, PGBUF_BCB * bcb);
STATIC_INLINE void pgbuf_lru_add_new_bcb_to_top (THREAD_ENTRY * thread_p, PGBUF_BCB * bcb, int lru_idx)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void pgbuf_lru_add_new_bcb_to_middle (THREAD_ENTRY * thread_p, PGBUF_BCB * bcb, int lru_idx)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void pgbuf_lru_add_new_bcb_to_bottom (THREAD_ENTRY * thread_p, PGBUF_BCB * bcb, int lru_idx)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void pgbuf_lru_remove_bcb (THREAD_ENTRY * thread_p, PGBUF_BCB * bcb) __attribute__ ((ALWAYS_INLINE));
static void pgbuf_lru_move_from_private_to_shared (THREAD_ENTRY * thread_p, PGBUF_BCB * bcb);
static void pgbuf_move_bcb_to_bottom_lru (THREAD_ENTRY * thread_p, PGBUF_BCB * bcb);

STATIC_INLINE int pgbuf_bcb_flush_with_wal (THREAD_ENTRY * thread_p, PGBUF_BCB * bufptr, bool is_page_flush_thread,
					    bool * is_bcb_locked) __attribute__ ((ALWAYS_INLINE));
static void pgbuf_wake_flush_waiters (THREAD_ENTRY * thread_p, PGBUF_BCB * bcb);
STATIC_INLINE bool pgbuf_is_exist_blocked_reader_writer (PGBUF_BCB * bufptr) __attribute__ ((ALWAYS_INLINE));
static int pgbuf_flush_all_helper (THREAD_ENTRY * thread_p, VOLID volid, bool is_only_fixed, bool is_set_lsa_as_null);

#if defined(SERVER_MODE)
static int pgbuf_timed_sleep_error_handling (THREAD_ENTRY * thread_p, PGBUF_BCB * bufptr, THREAD_ENTRY * thrd_entry);
static int pgbuf_timed_sleep (THREAD_ENTRY * thread_p, PGBUF_BCB * bufptr, THREAD_ENTRY * thrd_entry);
STATIC_INLINE void pgbuf_wakeup_reader_writer (THREAD_ENTRY * thread_p, PGBUF_BCB * bufptr)
  __attribute__ ((ALWAYS_INLINE));
#endif /* SERVER_MODE */

STATIC_INLINE bool pgbuf_get_check_page_validation_level (int page_validation_level) __attribute__ ((ALWAYS_INLINE));
static bool pgbuf_is_valid_page_ptr (const PAGE_PTR pgptr);
STATIC_INLINE void pgbuf_set_bcb_page_vpid (PGBUF_BCB * bufptr) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE bool pgbuf_check_bcb_page_vpid (PGBUF_BCB * bufptr, bool maybe_deallocated)
  __attribute__ ((ALWAYS_INLINE));

#if defined(CUBRID_DEBUG)
static void pgbuf_scramble (FILEIO_PAGE * iopage);
static void pgbuf_dump (void);
static int pgbuf_is_consistent (const PGBUF_BCB * bufptr, int likely_bad_after_fixcnt);
#endif /* CUBRID_DEBUG */

#if !defined(NDEBUG)
static void pgbuf_add_fixed_at (PGBUF_HOLDER * holder, const char *caller_file, int caller_line, bool reset);
#endif

#if defined(SERVER_MODE)
static void pgbuf_sleep (THREAD_ENTRY * thread_p, pthread_mutex_t * mutex_p);
STATIC_INLINE int pgbuf_wakeup (THREAD_ENTRY * thread_p) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int pgbuf_wakeup_uncond (THREAD_ENTRY * thread_p) __attribute__ ((ALWAYS_INLINE));
#endif /* SERVER_MODE */
STATIC_INLINE void pgbuf_set_dirty_buffer_ptr (THREAD_ENTRY * thread_p, PGBUF_BCB * bufptr)
  __attribute__ ((ALWAYS_INLINE));
static int pgbuf_compare_victim_list (const void *p1, const void *p2);
static void pgbuf_wakeup_page_flush_daemon (THREAD_ENTRY * thread_p);
STATIC_INLINE bool pgbuf_check_page_ptype_internal (PAGE_PTR pgptr, PAGE_TYPE ptype, bool no_error)
  __attribute__ ((ALWAYS_INLINE));
#if defined (SERVER_MODE)
static bool pgbuf_is_thread_high_priority (THREAD_ENTRY * thread_p);
#endif /* SERVER_MODE */
static int pgbuf_flush_page_and_neighbors_fb (THREAD_ENTRY * thread_p, PGBUF_BCB * bufptr, int *flushed_pages);
STATIC_INLINE void pgbuf_add_bufptr_to_batch (PGBUF_BCB * bufptr, int idx) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int pgbuf_flush_neighbor_safe (THREAD_ENTRY * thread_p, PGBUF_BCB * bufptr, VPID * expected_vpid,
					     bool * flushed) __attribute__ ((ALWAYS_INLINE));

static int pgbuf_get_groupid_and_unfix (THREAD_ENTRY * thread_p, const VPID * req_vpid, PAGE_PTR * pgptr,
					VPID * groupid, bool do_unfix);
#if !defined(NDEBUG)
STATIC_INLINE void pgbuf_add_watch_instance_internal (PGBUF_HOLDER * holder, PAGE_PTR pgptr, PGBUF_WATCHER * watcher,
						      const PGBUF_LATCH_MODE latch_mode, const bool clear_unfix_flag,
						      const char *caller_file, const int caller_line)
  __attribute__ ((ALWAYS_INLINE));
#else
STATIC_INLINE void pgbuf_add_watch_instance_internal (PGBUF_HOLDER * holder, PAGE_PTR pgptr, PGBUF_WATCHER * watcher,
						      const PGBUF_LATCH_MODE latch_mode, const bool clear_unfix_flag)
  __attribute__ ((ALWAYS_INLINE));
#endif
static PGBUF_HOLDER *pgbuf_get_holder (THREAD_ENTRY * thread_p, PAGE_PTR pgptr);
static void pgbuf_remove_watcher (PGBUF_HOLDER * holder, PGBUF_WATCHER * watcher_object);
static int pgbuf_flush_chkpt_seq_list (THREAD_ENTRY * thread_p, PGBUF_SEQ_FLUSHER * seq_flusher,
				       const LOG_LSA * prev_chkpt_redo_lsa, LOG_LSA * chkpt_smallest_lsa);
static int pgbuf_flush_seq_list (THREAD_ENTRY * thread_p, PGBUF_SEQ_FLUSHER * seq_flusher, struct timeval *limit_time,
				 const LOG_LSA * prev_chkpt_redo_lsa, LOG_LSA * chkpt_smallest_lsa, int *time_rem);
static int pgbuf_initialize_seq_flusher (PGBUF_SEQ_FLUSHER * seq_flusher, PGBUF_VICTIM_CANDIDATE_LIST * f_list,
					 const int cnt);
static const char *pgbuf_latch_mode_str (PGBUF_LATCH_MODE latch_mode);
static const char *pgbuf_zone_str (PGBUF_ZONE zone);
static const char *pgbuf_consistent_str (int consistent);

static void pgbuf_compute_lru_vict_target (float *lru_sum_flush_priority);

STATIC_INLINE bool pgbuf_is_bcb_victimizable (PGBUF_BCB * bcb, bool has_mutex_lock) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE bool pgbuf_is_bcb_fixed_by_any (PGBUF_BCB * bcb, bool has_mutex_lock) __attribute__ ((ALWAYS_INLINE));

STATIC_INLINE bool pgbuf_assign_direct_victim (THREAD_ENTRY * thread_p, PGBUF_BCB * bcb)
  __attribute__ ((ALWAYS_INLINE));
#if defined (SERVER_MODE)
STATIC_INLINE bool pgbuf_get_thread_waiting_for_direct_victim (REFPTR (THREAD_ENTRY, waiting_thread_out))
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE PGBUF_BCB *pgbuf_get_direct_victim (THREAD_ENTRY * thread_p) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE bool pgbuf_is_any_thread_waiting_for_direct_victim (void) __attribute__ ((ALWAYS_INLINE));
#endif /* SERVER_MODE */

STATIC_INLINE void pgbuf_lru_add_victim_candidate (THREAD_ENTRY * thread_p, PGBUF_LRU_LIST * lru_list, PGBUF_BCB * bcb)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void pgbuf_lru_remove_victim_candidate (THREAD_ENTRY * thread_p, PGBUF_LRU_LIST * lru_list,
						      PGBUF_BCB * bcb) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void pgbuf_lru_advance_victim_hint (THREAD_ENTRY * thread_p, PGBUF_LRU_LIST * lru_list,
						  PGBUF_BCB * bcb_prev_hint, PGBUF_BCB * bcb_new_hint,
						  bool was_vict_count_updated) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE PGBUF_LRU_LIST *pgbuf_lru_list_from_bcb (const PGBUF_BCB * bcb) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void pgbuf_bcb_register_hit_for_lru (PGBUF_BCB * bcb) __attribute__ ((ALWAYS_INLINE));

STATIC_INLINE void pgbuf_bcb_update_flags (THREAD_ENTRY * thread_p, PGBUF_BCB * bcb, int set_flags, int clear_flags)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void pgbuf_bcb_change_zone (THREAD_ENTRY * thread_p, PGBUF_BCB * bcb, int lru_idx, PGBUF_ZONE zone)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE PGBUF_ZONE pgbuf_bcb_get_zone (const PGBUF_BCB * bcb) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int pgbuf_bcb_get_lru_index (const PGBUF_BCB * bcb) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int pgbuf_bcb_get_pool_index (const PGBUF_BCB * bcb) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE bool pgbuf_bcb_is_dirty (const PGBUF_BCB * bcb) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE bool pgbuf_bcb_mark_is_flushing (THREAD_ENTRY * thread_p, PGBUF_BCB * bcb)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE bool pgbuf_bcb_is_flushing (const PGBUF_BCB * bcb) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE bool pgbuf_bcb_is_direct_victim (const PGBUF_BCB * bcb) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE bool pgbuf_bcb_is_invalid_direct_victim (const PGBUF_BCB * bcb) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE bool pgbuf_bcb_is_async_flush_request (const PGBUF_BCB * bcb) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE bool pgbuf_bcb_is_to_vacuum (const PGBUF_BCB * bcb) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE bool pgbuf_bcb_should_be_moved_to_bottom_lru (const PGBUF_BCB * bcb) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE bool pgbuf_bcb_avoid_victim (const PGBUF_BCB * bcb) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void pgbuf_bcb_set_dirty (THREAD_ENTRY * thread_p, PGBUF_BCB * bcb) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void pgbuf_bcb_clear_dirty (THREAD_ENTRY * thread_p, PGBUF_BCB * bcb) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void pgbuf_bcb_mark_was_flushed (THREAD_ENTRY * thread_p, PGBUF_BCB * bcb)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void pgbuf_bcb_mark_was_not_flushed (THREAD_ENTRY * thread_p, PGBUF_BCB * bcb, bool mark_dirty)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void pgbuf_bcb_register_avoid_deallocation (PGBUF_BCB * bcb) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void pgbuf_bcb_unregister_avoid_deallocation (PGBUF_BCB * bcb) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE bool pgbuf_bcb_should_avoid_deallocation (const PGBUF_BCB * bcb) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void pgbuf_bcb_check_and_reset_fix_and_avoid_dealloc (PGBUF_BCB * bcb, const char *file, int line)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void pgbuf_bcb_register_fix (PGBUF_BCB * bcb) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE bool pgbuf_bcb_is_hot (const PGBUF_BCB * bcb) __attribute__ ((ALWAYS_INLINE));

#if defined (SERVER_MODE)
static void pgbuf_bcbmon_lock (PGBUF_BCB * bcb, int caller_line);
static int pgbuf_bcbmon_trylock (PGBUF_BCB * bcb, int caller_line);
static void pgbuf_bcbmon_unlock (PGBUF_BCB * bcb);
static void pgbuf_bcbmon_check_own (PGBUF_BCB * bcb);
static void pgbuf_bcbmon_check_mutex_leaks (void);
#endif /* SERVER_MODE */

STATIC_INLINE bool pgbuf_lfcq_add_lru_with_victims (PGBUF_LRU_LIST * lru_list) __attribute__ ((ALWAYS_INLINE));
static PGBUF_BCB *pgbuf_lfcq_get_victim_from_private_lru (THREAD_ENTRY * thread_p, bool restricted);
static PGBUF_BCB *pgbuf_lfcq_get_victim_from_shared_lru (THREAD_ENTRY * thread_p, bool multi_threaded);

STATIC_INLINE bool pgbuf_is_hit_ratio_low (void);

static void pgbuf_flags_mask_sanity_check (void);
static void pgbuf_lru_sanity_check (const PGBUF_LRU_LIST * lru);

// TODO: find a better place for this, but not log_impl.h
STATIC_INLINE int pgbuf_find_current_wait_msecs (THREAD_ENTRY * thread_p) __attribute__ ((ALWAYS_INLINE));

static bool pgbuf_is_temp_lsa (const log_lsa & lsa);
static void pgbuf_init_temp_page_lsa (FILEIO_PAGE * io_page, PGLENGTH page_size);

static void pgbuf_scan_bcb_table (THREAD_ENTRY * thread_p);

#if defined (SERVER_MODE)
// *INDENT-OFF*
static cubthread::daemon *pgbuf_Page_maintenance_daemon = NULL;
static cubthread::daemon *pgbuf_Page_flush_daemon = NULL;
static cubthread::daemon *pgbuf_Page_post_flush_daemon = NULL;
static cubthread::daemon *pgbuf_Flush_control_daemon = NULL;
// *INDENT-ON*
#endif /* SERVER_MODE */

static bool pgbuf_is_page_flush_daemon_available ();

/*
 * pgbuf_hash_func_mirror () - Hash VPID into hash anchor
 *   return: hash value
 *   key_vpid(in): VPID to hash
 */
STATIC_INLINE unsigned int
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

  if (vpid1->volid == vpid2->volid)
    {
      return vpid1->pageid - vpid2->pageid;
    }
  else
    {
      return vpid1->volid - vpid2->volid;
    }
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
  pgbuf_flags_mask_sanity_check ();

  memset (&pgbuf_Pool, 0, sizeof (pgbuf_Pool));

  pgbuf_Pool.num_buffers = prm_get_integer_value (PRM_ID_PB_NBUFFERS);
  if (pgbuf_Pool.num_buffers < PGBUF_MINIMUM_BUFFERS)
    {
#if defined(CUBRID_DEBUG)
      er_log_debug (ARG_FILE_LINE, "pgbuf_initialize: WARNING Num_buffers = %d is too small. %d was assumed",
		    pgbuf_Pool.num_buffers, PGBUF_MINIMUM_BUFFERS);
#endif /* CUBRID_DEBUG */
      pgbuf_Pool.num_buffers = PGBUF_MINIMUM_BUFFERS;
    }
#if defined (SERVER_MODE)
#if defined (NDEBUG)
  pgbuf_Monitor_locks = prm_get_bool_value (PRM_ID_PB_MONITOR_LOCKS);
#else /* !NDEBUG */
  pgbuf_Monitor_locks = true;
#endif /* !NDEBUG */
#endif /* SERVER_MODE */

  /* set ratios for lru zones */
  pgbuf_Pool.ratio_lru1 = prm_get_float_value (PRM_ID_PB_LRU_HOT_RATIO);
  pgbuf_Pool.ratio_lru2 = prm_get_float_value (PRM_ID_PB_LRU_BUFFER_RATIO);
  pgbuf_Pool.ratio_lru1 = MAX (pgbuf_Pool.ratio_lru1, PGBUF_LRU_ZONE_MIN_RATIO);
  pgbuf_Pool.ratio_lru1 = MIN (pgbuf_Pool.ratio_lru1, PGBUF_LRU_ZONE_MAX_RATIO);
  pgbuf_Pool.ratio_lru2 = MAX (pgbuf_Pool.ratio_lru2, PGBUF_LRU_ZONE_MIN_RATIO);
  pgbuf_Pool.ratio_lru2 = MIN (pgbuf_Pool.ratio_lru2, 1.0f - PGBUF_LRU_ZONE_MIN_RATIO - pgbuf_Pool.ratio_lru1);
  assert (pgbuf_Pool.ratio_lru2 >= PGBUF_LRU_ZONE_MIN_RATIO && pgbuf_Pool.ratio_lru2 <= PGBUF_LRU_ZONE_MAX_RATIO);
  assert ((pgbuf_Pool.ratio_lru1 + pgbuf_Pool.ratio_lru2) >= 0.099f
	  && (pgbuf_Pool.ratio_lru1 + pgbuf_Pool.ratio_lru2) <= 0.951f);

  /* keep page quota parameter initializer first */
  if (pgbuf_initialize_page_quota_parameters () != NO_ERROR)
    {
      goto error;
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

  if (pgbuf_initialize_aout_list () != NO_ERROR)
    {
      goto error;
    }

  if (pgbuf_initialize_thrd_holder () != NO_ERROR)
    {
      goto error;
    }

  /* keep page quota initializer first */
  if (pgbuf_initialize_page_quota () != NO_ERROR)
    {
      goto error;
    }

  if (pgbuf_initialize_page_monitor () != NO_ERROR)
    {
      goto error;
    }

  pgbuf_Pool.check_for_interrupts = false;

  pgbuf_Pool.victim_cand_list =
    ((PGBUF_VICTIM_CANDIDATE_LIST *) malloc (pgbuf_Pool.num_buffers * sizeof (PGBUF_VICTIM_CANDIDATE_LIST)));
  if (pgbuf_Pool.victim_cand_list == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      (pgbuf_Pool.num_buffers * sizeof (PGBUF_VICTIM_CANDIDATE_LIST)));
      goto error;
    }

#if defined (SERVER_MODE)
  pgbuf_Pool.is_flushing_victims = false;
  pgbuf_Pool.is_checkpoint = false;
#endif

  {
    int cnt;
    cnt = (int) (0.25f * pgbuf_Pool.num_buffers);
    cnt = MIN (cnt, 65536);

    if (pgbuf_initialize_seq_flusher (&(pgbuf_Pool.seq_chkpt_flusher), NULL, cnt) != NO_ERROR)
      {
	goto error;
      }
  }

  /* TODO[arnia] : not required, if done in monitor initialization */
  pgbuf_Pool.monitor.dirties_cnt = 0;

#if defined (SERVER_MODE)
  pgbuf_Pool.direct_victims.bcb_victims = (PGBUF_BCB **) malloc (thread_num_total_threads () * sizeof (PGBUF_BCB *));
  if (pgbuf_Pool.direct_victims.bcb_victims == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      thread_num_total_threads () * sizeof (PGBUF_BCB *));
      goto error;
    }
  memset (pgbuf_Pool.direct_victims.bcb_victims, 0, thread_num_total_threads () * sizeof (PGBUF_BCB *));

  /* *INDENT-OFF* */
  pgbuf_Pool.direct_victims.waiter_threads_high_priority =
    new lockfree::circular_queue<THREAD_ENTRY *> (thread_num_total_threads ());
  /* *INDENT-ON* */
  if (pgbuf_Pool.direct_victims.waiter_threads_high_priority == NULL)
    {
      ASSERT_ERROR ();
      goto error;
    }

  /* *INDENT-OFF* */
  pgbuf_Pool.direct_victims.waiter_threads_low_priority =
    new lockfree::circular_queue<THREAD_ENTRY *> (2 * thread_num_total_threads ());
  /* *INDENT-ON* */
  if (pgbuf_Pool.direct_victims.waiter_threads_low_priority == NULL)
    {
      ASSERT_ERROR ();
      goto error;
    }

  /* *INDENT-OFF* */
  pgbuf_Pool.flushed_bcbs = new lockfree::circular_queue<PGBUF_BCB *> (PGBUF_FLUSHED_BCBS_BUFFER_SIZE);
  /* *INDENT-ON* */
  if (pgbuf_Pool.flushed_bcbs == NULL)
    {
      ASSERT_ERROR ();
      goto error;
    }
#endif /* SERVER_MODE */

  if (PGBUF_PAGE_QUOTA_IS_ENABLED)
    {
      /* *INDENT-OFF* */
      pgbuf_Pool.private_lrus_with_victims = new lockfree::circular_queue<int> (PGBUF_PRIVATE_LRU_COUNT * 2);
      /* *INDENT-ON* */
      if (pgbuf_Pool.private_lrus_with_victims == NULL)
	{
	  ASSERT_ERROR ();
	  goto error;
	}

      /* *INDENT-OFF* */
      pgbuf_Pool.big_private_lrus_with_victims = new lockfree::circular_queue<int> (PGBUF_PRIVATE_LRU_COUNT * 2);
      /* *INDENT-ON* */
      if (pgbuf_Pool.big_private_lrus_with_victims == NULL)
	{
	  ASSERT_ERROR ();
	  goto error;
	}
    }

  /* *INDENT-OFF* */
  pgbuf_Pool.shared_lrus_with_victims = new lockfree::circular_queue<int> (PGBUF_SHARED_LRU_COUNT * 2);
  /* *INDENT-ON* */
  if (pgbuf_Pool.shared_lrus_with_victims == NULL)
    {
      ASSERT_ERROR ();
      goto error;
    }

  pgbuf_Pool.show_status = (PGBUF_STATUS *) malloc (sizeof (PGBUF_STATUS) * (MAX_NTRANS + 1));
  if (pgbuf_Pool.show_status == NULL)
    {
      ASSERT_ERROR ();
      goto error;
    }

  memset (pgbuf_Pool.show_status, 0, sizeof (PGBUF_STATUS) * (MAX_NTRANS + 1));

  pgbuf_Pool.show_status_old.print_out_time = time (NULL);

#if defined(SERVER_MODE)
  pthread_mutex_init (&pgbuf_Pool.show_status_mutex, NULL);
#endif

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
  PGBUF_HOLDER_SET *holder_set;
  int i;
  size_t hash_size, j;

#if defined(CUBRID_DEBUG)
  pgbuf_dump_if_any_fixed ();
#endif /* CUBRID_DEBUG */

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
	  pthread_mutex_destroy (&bufptr->mutex);
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
      for (i = 0; i < PGBUF_TOTAL_LRU_COUNT; i++)
	{
	  pthread_mutex_destroy (&pgbuf_Pool.buf_LRU_list[i].mutex);
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

  if (pgbuf_Pool.victim_cand_list != NULL)
    {
      free_and_init (pgbuf_Pool.victim_cand_list);
    }

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

  if (pgbuf_Pool.seq_chkpt_flusher.flush_list != NULL)
    {
      free_and_init (pgbuf_Pool.seq_chkpt_flusher.flush_list);
    }

  /* Free quota structure data */
  if (pgbuf_Pool.quota.lru_victim_flush_priority_per_lru != NULL)
    {
      free_and_init (pgbuf_Pool.quota.lru_victim_flush_priority_per_lru);
    }
  if (pgbuf_Pool.quota.private_lru_session_cnt != NULL)
    {
      free_and_init (pgbuf_Pool.quota.private_lru_session_cnt);
    }

  /* Free monitor structure data */
  if (pgbuf_Pool.monitor.lru_hits != NULL)
    {
      free_and_init (pgbuf_Pool.monitor.lru_hits);
    }
  if (pgbuf_Pool.monitor.lru_activity != NULL)
    {
      free_and_init (pgbuf_Pool.monitor.lru_activity);
    }

#if defined (SERVER_MODE)
  if (pgbuf_Pool.monitor.bcb_locks != NULL)
    {
      free_and_init (pgbuf_Pool.monitor.bcb_locks);
    }

  if (pgbuf_Pool.direct_victims.bcb_victims != NULL)
    {
      free_and_init (pgbuf_Pool.direct_victims.bcb_victims);
    }
  if (pgbuf_Pool.direct_victims.waiter_threads_high_priority != NULL)
    {
      delete pgbuf_Pool.direct_victims.waiter_threads_high_priority;
      pgbuf_Pool.direct_victims.waiter_threads_high_priority = NULL;
    }
  if (pgbuf_Pool.direct_victims.waiter_threads_low_priority != NULL)
    {
      delete pgbuf_Pool.direct_victims.waiter_threads_low_priority;
      pgbuf_Pool.direct_victims.waiter_threads_low_priority = NULL;
    }
  if (pgbuf_Pool.flushed_bcbs != NULL)
    {
      delete pgbuf_Pool.flushed_bcbs;
      pgbuf_Pool.flushed_bcbs = NULL;
    }
#endif /* SERVER_MODE */

  if (pgbuf_Pool.private_lrus_with_victims != NULL)
    {
      delete pgbuf_Pool.private_lrus_with_victims;
      pgbuf_Pool.private_lrus_with_victims = NULL;
    }
  if (pgbuf_Pool.big_private_lrus_with_victims != NULL)
    {
      delete pgbuf_Pool.big_private_lrus_with_victims;
      pgbuf_Pool.big_private_lrus_with_victims = NULL;
    }
  if (pgbuf_Pool.shared_lrus_with_victims != NULL)
    {
      delete pgbuf_Pool.shared_lrus_with_victims;
      pgbuf_Pool.shared_lrus_with_victims = NULL;
    }

  if (pgbuf_Pool.show_status != NULL)
    {
      free (pgbuf_Pool.show_status);
      pgbuf_Pool.show_status = NULL;
    }

#if defined(SERVER_MODE)
  pthread_mutex_destroy (&pgbuf_Pool.show_status_mutex);
#endif
}

/*
 * pgbuf_fix_with_retry () -
 *   return: Pointer to the page or NULL
 *   vpid(in): Complete Page identifier
 *   fetch_mode(in): Page fetch mode
 *   request_mode(in): Lock request_mode
 *   retry(in): Retry count
 */
PAGE_PTR
pgbuf_fix_with_retry (THREAD_ENTRY * thread_p, const VPID * vpid, PAGE_FETCH_MODE fetch_mode,
		      PGBUF_LATCH_MODE request_mode, int retry)
{
  PAGE_PTR pgptr;
  int i = 0;
  bool noretry = false;

  while ((pgptr = pgbuf_fix (thread_p, vpid, fetch_mode, request_mode, PGBUF_UNCONDITIONAL_LATCH)) == NULL)
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
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_PAGE_LATCH_ABORTED, 2, vpid->volid, vpid->pageid);
	  break;
	}
    }

  return pgptr;
}

/*
 * pgbuf_fix () -
 *   return: Pointer to the page or NULL
 *   vpid(in): Complete Page identifier
 *   fetch_mode(in): Page fetch mode.
 *   request_mode(in): Page latch mode.
 *   condition(in): Page latch condition.
 */
#if !defined(NDEBUG)
PAGE_PTR
pgbuf_fix_debug (THREAD_ENTRY * thread_p, const VPID * vpid, PAGE_FETCH_MODE fetch_mode, PGBUF_LATCH_MODE request_mode,
		 PGBUF_LATCH_CONDITION condition, const char *caller_file, int caller_line, const char *caller_func)
#else /* NDEBUG */
PAGE_PTR
pgbuf_fix_release (THREAD_ENTRY * thread_p, const VPID * vpid, PAGE_FETCH_MODE fetch_mode,
		   PGBUF_LATCH_MODE request_mode, PGBUF_LATCH_CONDITION condition)
#endif				/* NDEBUG */
{
  PGBUF_BUFFER_HASH *hash_anchor;
  PGBUF_BCB *bufptr;
  PAGE_PTR pgptr;
  int wait_msecs;
#if defined(ENABLE_SYSTEMTAP)
  bool pgbuf_hit = false;
#endif /* ENABLE_SYSTEMTAP */
  PGBUF_HOLDER *holder;
  PGBUF_WATCHER *watcher;
  bool buf_lock_acquired = false;
  bool is_latch_wait = false;
  bool retry = false;
#if !defined (NDEBUG)
  bool had_holder = false;
#endif /* !NDEBUG */
  PGBUF_FIX_PERF perf;
  bool maybe_deallocated;
  int tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  PGBUF_STATUS *show_status = &pgbuf_Pool.show_status[tran_index];

  perf.perf_page_found = PERF_PAGE_MODE_OLD_IN_BUFFER;

  /* parameter validation */
  if (request_mode != PGBUF_LATCH_READ && request_mode != PGBUF_LATCH_WRITE)
    {
      assert_release (false);
      return NULL;
    }
  if (condition != PGBUF_UNCONDITIONAL_LATCH && condition != PGBUF_CONDITIONAL_LATCH)
    {
      assert_release (false);
      return NULL;
    }

  ATOMIC_INC_32 (&pgbuf_Pool.monitor.fix_req_cnt, 1);

  if (pgbuf_get_check_page_validation_level (PGBUF_DEBUG_PAGE_VALIDATION_FETCH))
    {
      /* Make sure that the page has been allocated (i.e., is a valid page) */
      /* Suppress errors if fetch mode is OLD_PAGE_IF_IN_BUFFER. */
      if (pgbuf_is_valid_page (thread_p, vpid, fetch_mode == OLD_PAGE_IF_IN_BUFFER, NULL, NULL) != DISK_VALID)
	{
	  return NULL;
	}
    }

  /* Do a simple check in non debugging mode */
  if (vpid->pageid < 0)
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_PB_BAD_PAGEID, 2, vpid->pageid,
	      fileio_get_volume_label (vpid->volid, PEEK));
      return NULL;
    }

  if (condition == PGBUF_UNCONDITIONAL_LATCH)
    {
      /* Check the wait_msecs of current transaction. If the wait_msecs is zero wait that means no wait, change current
       * request as a conditional request. */
      wait_msecs = pgbuf_find_current_wait_msecs (thread_p);

      if (wait_msecs == LK_ZERO_WAIT || wait_msecs == LK_FORCE_ZERO_WAIT)
	{
	  condition = PGBUF_CONDITIONAL_LATCH;
	}
    }

  perf.lock_wait_time = 0;
  perf.is_perf_tracking = perfmon_is_perf_tracking ();

  if (perf.is_perf_tracking)
    {
      tsc_getticks (&perf.start_tick);
    }

try_again:

  /* interrupt check */
  if (logtb_get_check_interrupt (thread_p) == true)
    {
      if (logtb_is_interrupted (thread_p, true, &pgbuf_Pool.check_for_interrupts) == true)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INTERRUPTED, 0);
	  PGBUF_BCB_CHECK_MUTEX_LEAKS ();
	  return NULL;
	}
    }

  /* Normal process */
  /* latch_mode = PGBUF_LATCH_READ/PGBUF_LATCH_WRITE */
  hash_anchor = &pgbuf_Pool.buf_hash_table[PGBUF_HASH_VALUE (vpid)];

  buf_lock_acquired = false;
  bufptr = pgbuf_search_hash_chain (thread_p, hash_anchor, vpid);
  if (bufptr != NULL && pgbuf_bcb_is_direct_victim (bufptr))
    {
      /* we need to notify the thread that is waiting for this bcb to victimize that it cannot use it. */
      pgbuf_bcb_update_flags (thread_p, bufptr, PGBUF_BCB_INVALIDATE_DIRECT_VICTIM_FLAG, PGBUF_BCB_VICTIM_DIRECT_FLAG);
    }
  if (bufptr != NULL)
    {
#if defined (ENABLE_SYSTEMTAP)
      CUBRID_PGBUF_HIT ();
      pgbuf_hit = true;
#endif /* ENABLE_SYSTEMTAP */

      show_status->num_hit++;

      if (fetch_mode == NEW_PAGE)
	{
	  /* Fix a page as NEW_PAGE, when oldest_unflush_lsa of the page is not NULL_LSA, it should be dirty. */
	  assert (LSA_ISNULL (&bufptr->oldest_unflush_lsa) || pgbuf_bcb_is_dirty (bufptr));

	  /* The page may be invalidated and has been remained in the buffer and it is going to be used again as a new
	   * page. */
	}
    }
  else if (fetch_mode == OLD_PAGE_IF_IN_BUFFER)
    {
      /* we don't need to fix page */
      pthread_mutex_unlock (&hash_anchor->hash_mutex);
      return NULL;
    }
  else
    {
      bufptr = pgbuf_claim_bcb_for_fix (thread_p, vpid, fetch_mode, hash_anchor, &perf, &retry);
      if (bufptr == NULL)
	{
	  if (retry)
	    {
	      retry = false;
	      goto try_again;
	    }
	  ASSERT_ERROR ();
	  return NULL;
	}
      buf_lock_acquired = true;

#if defined(ENABLE_SYSTEMTAP)
      if (fetch_mode == NEW_PAGE && pgbuf_hit == false)
	{
	  pgbuf_hit = true;
	}
      if (fetch_mode != NEW_PAGE)
	{
	  CUBRID_PGBUF_MISS ();
	}
#endif /* ENABLE_SYSTEMTAP */
    }
  assert (!pgbuf_bcb_is_direct_victim (bufptr));

  /* At this place, the caller is holding bufptr->mutex */

  pgbuf_bcb_register_fix (bufptr);

  /* Set Page identifier if needed */
  // Redo recovery may find an immature page which should be set.
  pgbuf_set_bcb_page_vpid (bufptr);

  maybe_deallocated = (fetch_mode == OLD_PAGE_MAYBE_DEALLOCATED);
  if (pgbuf_check_bcb_page_vpid (bufptr, maybe_deallocated) != true)
    {
      if (buf_lock_acquired)
	{
	  /* bufptr->mutex will be released in the following function. */
	  pgbuf_put_bcb_into_invalid_list (thread_p, bufptr);

	  /*
	   * Now, caller is not holding any mutex.
	   * the last argument of pgbuf_unlock_page () is true that
	   * means hash_mutex must be held before unlocking page.
	   */
	  (void) pgbuf_unlock_page (thread_p, hash_anchor, vpid, true);
	}
      else
	{
	  PGBUF_BCB_UNLOCK (bufptr);
	}

      PGBUF_BCB_CHECK_MUTEX_LEAKS ();
      return NULL;
    }

  if (fetch_mode == OLD_PAGE_PREVENT_DEALLOC)
    {
      pgbuf_bcb_register_avoid_deallocation (bufptr);
    }

  /* At this place, the caller is holding bufptr->mutex */
  if (perf.is_perf_tracking)
    {
      tsc_getticks (&perf.start_holder_tick);
    }

  /* Latch Pass */
#if !defined (NDEBUG)
  had_holder = pgbuf_find_thrd_holder (thread_p, bufptr) != NULL;
#endif /* NDEBUG */
  if (pgbuf_latch_bcb_upon_fix (thread_p, bufptr, request_mode, buf_lock_acquired, condition, &is_latch_wait)
      != NO_ERROR)
    {
      /* bufptr->mutex has been released, error was set in the function, */

      if (buf_lock_acquired)
	{
	  /* hold bufptr->mutex again */
	  PGBUF_BCB_LOCK (bufptr);

	  /* bufptr->mutex will be released in the following function. */
	  pgbuf_put_bcb_into_invalid_list (thread_p, bufptr);

	  /*
	   * Now, caller is not holding any mutex.
	   * the last argument of pgbuf_unlock_page () is true that
	   * means hash_mutex must be held before unlocking page.
	   */
	  (void) pgbuf_unlock_page (thread_p, hash_anchor, vpid, true);
	}

      PGBUF_BCB_CHECK_MUTEX_LEAKS ();
      return NULL;
    }

#if !defined (NDEBUG)
  pgbuf_add_fixed_at (pgbuf_find_thrd_holder (thread_p, bufptr), caller_file, caller_line, !had_holder);
#endif /* NDEBUG */

  if (perf.is_perf_tracking && is_latch_wait)
    {
      tsc_getticks (&perf.end_tick);
      tsc_elapsed_time_usec (&perf.tv_diff, perf.end_tick, perf.start_holder_tick);
      perf.holder_wait_time = perf.tv_diff.tv_sec * 1000000LL + perf.tv_diff.tv_usec;
    }

  assert (bufptr == bufptr->iopage_buffer->bcb);

  /* In case of NO_ERROR, bufptr->mutex has been released. */

  /* Dirty Pages Table Registration Pass */

  /* Currently, do nothing. Whenever the fixed page becomes dirty, oldest_unflush_lsa is set. */

  /* Hash Chain Connection Pass */
  if (buf_lock_acquired)
    {
      pgbuf_insert_into_hash_chain (thread_p, hash_anchor, bufptr);

      /*
       * the caller is holding hash_anchor->hash_mutex.
       * Therefore, the third argument of pgbuf_unlock_page () is false
       * that means hash mutex does not need to be held.
       */
      (void) pgbuf_unlock_page (thread_p, hash_anchor, vpid, false);
    }

  CAST_BFPTR_TO_PGPTR (pgptr, bufptr);

#if !defined (NDEBUG)
  assert (pgptr != NULL);

  holder = pgbuf_get_holder (thread_p, pgptr);
  assert (holder != NULL);

  watcher = holder->last_watcher;
  while (watcher != NULL)
    {
      assert (watcher->magic == PGBUF_WATCHER_MAGIC_NUMBER);
      watcher = watcher->prev;
    }
#endif

  if (fetch_mode == OLD_PAGE_PREVENT_DEALLOC)
    {
      /* latch is obtained, no need for avoidance of dealloc */
      pgbuf_bcb_unregister_avoid_deallocation (bufptr);
    }

#if !defined (NDEBUG)
  thread_p->get_pgbuf_tracker ().increment (caller_file, caller_line, pgptr);
#endif // !NDEBUG

  if (bufptr->iopage_buffer->iopage.prv.ptype == PAGE_UNKNOWN)
    {
      /* deallocated page */
      switch (fetch_mode)
	{
	case NEW_PAGE:
	case OLD_PAGE_DEALLOCATED:
	case OLD_PAGE_IF_IN_BUFFER:
	case RECOVERY_PAGE:
	  /* fixing deallocated page is expected. fall through to return it. */
	  break;
	case OLD_PAGE:
	case OLD_PAGE_PREVENT_DEALLOC:
	default:
	  /* caller does not expect any deallocated pages. this is an invalid page. */
	  assert (false);
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_PB_BAD_PAGEID, 2, vpid->pageid,
		  fileio_get_volume_label (vpid->volid, PEEK));
	  /* fall through to unfix */
	  PGBUF_BCB_CHECK_MUTEX_LEAKS ();
	  pgbuf_unfix (thread_p, pgptr);
	  return NULL;
	case OLD_PAGE_MAYBE_DEALLOCATED:
	  /* OLD_PAGE_MAYBE_DEALLOCATED is called when deallocated page may be fixed. The caller wants the page only if
	   * it is not deallocated. However, if it is deallocated, no error is required. */
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_PB_BAD_PAGEID, 2, vpid->pageid,
		  fileio_get_volume_label (vpid->volid, PEEK));
	  /* fall through to unfix */
	  PGBUF_BCB_CHECK_MUTEX_LEAKS ();
	  pgbuf_unfix (thread_p, pgptr);
	  return NULL;
	}

      /* note: maybe we could check this in an earlier stage, but would have been a lot more complicated. the only
       *       interesting case here is OLD_PAGE_MAYBE_DEALLOCATED. However, even this is used in cases where the vast
       *       majority of pages will not be deallocated! So in terms of performance, the loss is insignificant.
       *       However, it is safer and easier to treat the case here, where we have latch to prevent concurrent
       *       deallocations. */
    }
  else
    {
      /* this cannot be a new page or a deallocated page.
       * note: temporary pages are not strictly handled in regard with their deallocation status. */
      assert (fetch_mode != NEW_PAGE || pgbuf_is_lsa_temporary (pgptr));
    }

  show_status->num_page_request++;

  /* Record number of fetches in statistics */
  if (perf.is_perf_tracking)
    {
      perf.perf_page_type = pgbuf_get_page_type_for_stat (thread_p, pgptr);

      perfmon_inc_stat (thread_p, PSTAT_PB_NUM_FETCHES);
      if (request_mode == PGBUF_LATCH_READ)
	{
	  perf.perf_latch_mode = PERF_HOLDER_LATCH_READ;
	}
      else
	{
	  assert (request_mode == PGBUF_LATCH_WRITE);
	  perf.perf_latch_mode = PERF_HOLDER_LATCH_WRITE;
	}

      if (condition == PGBUF_UNCONDITIONAL_LATCH)
	{
	  if (is_latch_wait)
	    {
	      perf.perf_cond_type = PERF_UNCONDITIONAL_FIX_WITH_WAIT;
	      if (perf.holder_wait_time > 0)
		{
		  perfmon_pbx_hold_acquire_time (thread_p, perf.perf_page_type, perf.perf_page_found,
						 perf.perf_latch_mode, perf.holder_wait_time);
		}
	    }
	  else
	    {
	      perf.perf_cond_type = PERF_UNCONDITIONAL_FIX_NO_WAIT;
	    }
	}
      else
	{
	  perf.perf_cond_type = PERF_CONDITIONAL_FIX;
	}

      perfmon_pbx_fix (thread_p, perf.perf_page_type, perf.perf_page_found, perf.perf_latch_mode, perf.perf_cond_type);
      if (perf.lock_wait_time > 0)
	{
	  perfmon_pbx_lock_acquire_time (thread_p, perf.perf_page_type, perf.perf_page_found, perf.perf_latch_mode,
					 perf.perf_cond_type, perf.lock_wait_time);
	}

      tsc_getticks (&perf.end_tick);
      tsc_elapsed_time_usec (&perf.tv_diff, perf.end_tick, perf.start_tick);
      perf.fix_wait_time = perf.tv_diff.tv_sec * 1000000LL + perf.tv_diff.tv_usec;

      if (perf.fix_wait_time > 0)
	{
	  perfmon_pbx_fix_acquire_time (thread_p, perf.perf_page_type, perf.perf_page_found, perf.perf_latch_mode,
					perf.perf_cond_type, perf.fix_wait_time);
	}
    }

  if (VACUUM_IS_THREAD_VACUUM_WORKER (thread_p))
    {
      pgbuf_bcb_update_flags (thread_p, bufptr, 0, PGBUF_BCB_TO_VACUUM_FLAG);
    }

  PGBUF_BCB_CHECK_MUTEX_LEAKS ();

  return pgptr;
}

/*
 * pgbuf_promote_read_latch () - Promote read latch to write latch
 *   return: error code or NO_ERROR
 *   pgptr(in/out): page pointer
 *   condition(in): promotion condition (single reader holder/shared reader holder)
 */
#if !defined (NDEBUG)
int
pgbuf_promote_read_latch_debug (THREAD_ENTRY * thread_p, PAGE_PTR * pgptr_p, PGBUF_PROMOTE_CONDITION condition,
				const char *caller_file, int caller_line, const char *caller_func)
#else /* NDEBUG */
int
pgbuf_promote_read_latch_release (THREAD_ENTRY * thread_p, PAGE_PTR * pgptr_p, PGBUF_PROMOTE_CONDITION condition)
#endif				/* NDEBUG */
{
  PGBUF_BCB *bufptr;
#if defined(SERVER_MODE)
  PGBUF_HOLDER *holder;
  VPID vpid;
  TSC_TICKS start_tick, end_tick;
  TSCTIMEVAL tv_diff;
  UINT64 promote_wait_time;
  bool is_perf_tracking;
  PERF_PAGE_TYPE perf_page_type = PERF_PAGE_UNKNOWN;
  PERF_PROMOTE_CONDITION perf_promote_cond_type = PERF_PROMOTE_ONLY_READER;
  PERF_HOLDER_LATCH perf_holder_latch = PERF_HOLDER_LATCH_READ;
  int stat_success = 0;
  int rv = NO_ERROR;
#endif /* SERVER_MODE */

#if !defined (NDEBUG)
  assert (pgptr_p != NULL);
  assert (*pgptr_p != NULL);

  if (pgbuf_get_check_page_validation_level (PGBUF_DEBUG_PAGE_VALIDATION_FREE))
    {
      if (pgbuf_is_valid_page_ptr (*pgptr_p) == false)
	{
	  return ER_FAILED;
	}
    }
#else /* !NDEBUG */
  if (*pgptr_p == NULL)
    {
      return ER_FAILED;
    }
#endif /* !NDEBUG */

  /* fetch BCB from page pointer */
  CAST_PGPTR_TO_BFPTR (bufptr, *pgptr_p);
  assert (!VPID_ISNULL (&bufptr->vpid));

  /* check latch mode - no need for BCB mutex, page is already latched */
  if (bufptr->latch_mode == PGBUF_LATCH_WRITE)
    {
      /* this is a redundant call */
      return NO_ERROR;
    }
  else if (bufptr->latch_mode != PGBUF_LATCH_READ)
    {
      assert_release (false);
      return ER_FAILED;
    }

  /* check condition */
  if (condition != PGBUF_PROMOTE_ONLY_READER && condition != PGBUF_PROMOTE_SHARED_READER)
    {
      assert_release (false);
      return ER_FAILED;
    }

#if defined(SERVER_MODE)	/* SERVER_MODE */
  /* performance tracking - get start counter */
  is_perf_tracking = perfmon_is_perf_tracking ();
  if (is_perf_tracking)
    {
      tsc_getticks (&start_tick);
    }

  PGBUF_BCB_LOCK (bufptr);

  /* save info for performance tracking */
  vpid = bufptr->vpid;
  if (is_perf_tracking)
    {
      perf_page_type = pgbuf_get_page_type_for_stat (thread_p, *pgptr_p);

      /* promote condition */
      if (condition == PGBUF_PROMOTE_ONLY_READER)
	{
	  perf_promote_cond_type = PERF_PROMOTE_ONLY_READER;
	}
      else
	{
	  perf_promote_cond_type = PERF_PROMOTE_SHARED_READER;
	}

      /* latch mode - NOTE: MIX will be always zero */
      if (bufptr->latch_mode == PGBUF_LATCH_READ)
	{
	  perf_holder_latch = PERF_HOLDER_LATCH_READ;
	}
      else
	{
	  perf_holder_latch = PERF_HOLDER_LATCH_WRITE;
	}
    }

  /* check if we're the single read latch holder */
  holder = pgbuf_find_thrd_holder (thread_p, bufptr);
  assert_release (holder != NULL);
  if (holder->fix_count == bufptr->fcnt)
    {
      assert (bufptr->latch_mode == PGBUF_LATCH_READ);

      /* check for waiters for promotion */
      if (bufptr->next_wait_thrd != NULL && bufptr->next_wait_thrd->wait_for_latch_promote)
	{
	  PGBUF_BCB_UNLOCK (bufptr);
	  rv = ER_PAGE_LATCH_PROMOTE_FAIL;
#if !defined(NDEBUG)
	  er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_PAGE_LATCH_PROMOTE_FAIL, 2, vpid.pageid, vpid.volid);
#endif
	  goto end;
	}

      /* we're the single holder of the read latch, do an in-place promotion */
      bufptr->latch_mode = PGBUF_LATCH_WRITE;
      holder->perf_stat.hold_has_write_latch = 1;
      /* NOTE: no need to set the promoted flag as long as we don't wait */
      PGBUF_BCB_UNLOCK (bufptr);
    }
  else
    {
      if ((condition == PGBUF_PROMOTE_ONLY_READER)
	  || (bufptr->next_wait_thrd != NULL && bufptr->next_wait_thrd->wait_for_latch_promote))
	{
	  /*
	   * CASE #1: first waiter is from a latch promotion - we can't
	   * guarantee both will see the same page they initially fixed so
	   * we'll abort the current promotion
	   * CASE #2: PGBUF_PROMOTE_ONLY_READER condition, we're only allowed
	   * to promote if we're the only reader; this is not the case
	   */
	  PGBUF_BCB_UNLOCK (bufptr);
	  rv = ER_PAGE_LATCH_PROMOTE_FAIL;
#if !defined(NDEBUG)
	  er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_PAGE_LATCH_PROMOTE_FAIL, 2, vpid.pageid, vpid.volid);
#endif
	  goto end;
	}
      else
	{
	  int fix_count = holder->fix_count;
	  PGBUF_HOLDER_STAT perf_stat = holder->perf_stat;

	  bufptr->fcnt -= fix_count;
	  holder->fix_count = 0;
	  if (pgbuf_remove_thrd_holder (thread_p, holder) != NO_ERROR)
	    {
	      /* We unfixed the page, but failed to remove holder entry; consider the page as unfixed */
	      *pgptr_p = NULL;

	      /* shouldn't happen */
	      PGBUF_BCB_UNLOCK (bufptr);
	      assert_release (false);
	      return ER_FAILED;
	    }
	  holder = NULL;
	  /* NOTE: at this point the page is unfixed */

	  /* flag this thread as promoter */
	  thread_p->wait_for_latch_promote = true;

	  /* register as first blocker */
	  if (pgbuf_block_bcb (thread_p, bufptr, PGBUF_LATCH_WRITE, fix_count, true) != NO_ERROR)
	    {
	      *pgptr_p = NULL;	/* we didn't get a new latch */
	      thread_p->wait_for_latch_promote = false;
	      return ER_FAILED;
	    }

	  /* NOTE: BCB mutex is no longer held at this point */

	  /* remove promote flag */
	  thread_p->wait_for_latch_promote = false;

	  /* new holder entry */
	  assert (pgbuf_find_thrd_holder (thread_p, bufptr) == NULL);
	  holder = pgbuf_allocate_thrd_holder_entry (thread_p);
	  if (holder == NULL)
	    {
	      /* We have new latch, but can't add a holder entry; consider the page as fixed */
	      /* This situation must not be occurred. */
	      assert_release (false);
	      return ER_FAILED;
	    }
	  holder->fix_count = fix_count;
	  holder->bufptr = bufptr;
	  holder->perf_stat = perf_stat;
	  if (bufptr->latch_mode == PGBUF_LATCH_WRITE)
	    {
	      holder->perf_stat.hold_has_write_latch = 1;
	    }
	  else if (bufptr->latch_mode == PGBUF_LATCH_READ)
	    {
	      holder->perf_stat.hold_has_read_latch = 1;
	    }
#if !defined(NDEBUG)
	  pgbuf_add_fixed_at (holder, caller_file, caller_line, true);
#endif /* NDEBUG */
	}
    }

end:
  assert (rv == NO_ERROR || rv == ER_PAGE_LATCH_PROMOTE_FAIL);

  /* performance tracking */
  if (is_perf_tracking)
    {
      /* compute time */
      tsc_getticks (&end_tick);
      tsc_elapsed_time_usec (&tv_diff, end_tick, start_tick);
      promote_wait_time = tv_diff.tv_sec * 1000000LL + tv_diff.tv_usec;

      /* determine success or fail */
      if (rv == NO_ERROR)
	{
	  stat_success = 1;
	}

      /* aggregate success/fail */
      perfmon_pbx_promote (thread_p, perf_page_type, perf_promote_cond_type, perf_holder_latch, stat_success,
			   promote_wait_time);
    }

  /* all successful */
  return rv;

#else /* SERVER_MODE */
  bufptr->latch_mode = PGBUF_LATCH_WRITE;
  return NO_ERROR;
#endif
}

/*
 * pgbuf_unfix () - Free the buffer where the page associated with pgptr resides
 *   return: void
 *   pgptr(in): Pointer to page
 *
 * Note: The page is subject to replacement, if not fixed by other thread of execution.
 */
#if !defined (NDEBUG)
void
pgbuf_unfix_debug (THREAD_ENTRY * thread_p, PAGE_PTR pgptr, const char *caller_file, int caller_line,
		   const char *caller_func)
#else /* NDEBUG */
void
pgbuf_unfix (THREAD_ENTRY * thread_p, PAGE_PTR pgptr)
#endif				/* NDEBUG */
{
  PGBUF_BCB *bufptr;
  int holder_status;
  PERF_HOLDER_LATCH perf_holder_latch;
  PGBUF_HOLDER *holder;
  PGBUF_WATCHER *watcher;
  PGBUF_HOLDER_STAT holder_perf_stat;
  PERF_PAGE_TYPE perf_page_type = PERF_PAGE_UNKNOWN;
  bool is_perf_tracking;

#if defined(CUBRID_DEBUG)
  LOG_LSA restart_lsa;
#endif /* CUBRID_DEBUG */

#if !defined (NDEBUG)
  assert (pgptr != NULL);

  if (pgbuf_get_check_page_validation_level (PGBUF_DEBUG_PAGE_VALIDATION_FREE))
    {
      if (pgbuf_is_valid_page_ptr (pgptr) == false)
	{
	  return;
	}
    }

  holder = pgbuf_get_holder (thread_p, pgptr);

  assert (holder != NULL);

  watcher = holder->last_watcher;
  while (watcher != NULL)
    {
      assert (watcher->magic == PGBUF_WATCHER_MAGIC_NUMBER);
      watcher = watcher->prev;
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
  if (pgbuf_bcb_is_dirty (bufptr) && !pgbuf_is_temp_lsa (bufptr->iopage_buffer->iopage.prv.lsa)
      && PGBUF_IS_AUXILIARY_VOLUME (bufptr->vpid.volid) == false
      && !log_is_logged_since_restart (&bufptr->iopage_buffer->iopage.prv.lsa))
    {
      er_log_debug (ARG_FILE_LINE,
		    "pgbuf_unfix: WARNING: No logging on dirty pageid = %d of Volume = %s.\n Recovery problems"
		    " may happen\n", bufptr->vpid.pageid, fileio_get_volume_label (bufptr->vpid.volid, PEEK));
      /*
       * Do not give warnings on this page any longer. Set the LSA of the
       * buffer for this purposes
       */
      pgbuf_set_lsa (thread_p, pgptr, log_get_restart_lsa ());
      pgbuf_set_lsa (thread_p, pgptr, &restart_lsa);
      LSA_COPY (&bufptr->oldest_unflush_lsa, &bufptr->iopage_buffer->iopage.prv.lsa);
    }

  /* Check for over runs */
  if (memcmp (PGBUF_FIND_BUFFER_GUARD (bufptr), pgbuf_Guard, sizeof (pgbuf_Guard)) != 0)
    {
      er_log_debug (ARG_FILE_LINE, "pgbuf_unfix: SYSTEM ERROR buffer of pageid = %d|%d has been OVER RUN",
		    bufptr->vpid.volid, bufptr->vpid.pageid);
      memcpy (PGBUF_FIND_BUFFER_GUARD (bufptr), pgbuf_Guard, sizeof (pgbuf_Guard));
    }

  /* Give a warning if the page is not consistent */
  if (bufptr->fcnt <= 0)
    {
      er_log_debug (ARG_FILE_LINE,
		    "pgbuf_unfix: SYSTEM ERROR Freeing too much buffer of pageid = %d of Volume = %s\n",
		    bufptr->vpid.pageid, fileio_get_volume_label (bufptr->vpid.volid, PEEK));
    }
#endif /* CUBRID_DEBUG */

  is_perf_tracking = perfmon_is_perf_tracking ();
  if (is_perf_tracking)
    {
      perf_page_type = pgbuf_get_page_type_for_stat (thread_p, pgptr);
    }
  INIT_HOLDER_STAT (&holder_perf_stat);
  holder_status = pgbuf_unlatch_thrd_holder (thread_p, bufptr, &holder_perf_stat);

  assert (holder_perf_stat.hold_has_write_latch == 1 || holder_perf_stat.hold_has_read_latch == 1);

  if (is_perf_tracking)
    {
      if (holder_perf_stat.hold_has_read_latch && holder_perf_stat.hold_has_write_latch)
	{
	  perf_holder_latch = PERF_HOLDER_LATCH_MIXED;
	}
      else if (holder_perf_stat.hold_has_read_latch)
	{
	  perf_holder_latch = PERF_HOLDER_LATCH_READ;
	}
      else
	{
	  assert (holder_perf_stat.hold_has_write_latch);
	  perf_holder_latch = PERF_HOLDER_LATCH_WRITE;
	}
      perfmon_pbx_unfix (thread_p, perf_page_type, holder_perf_stat.dirty_before_hold,
			 holder_perf_stat.dirtied_by_holder, perf_holder_latch);
    }

  PGBUF_BCB_LOCK (bufptr);

#if !defined (NDEBUG)
  thread_p->get_pgbuf_tracker ().decrement (pgptr);
#endif // !NDEBUG
  (void) pgbuf_unlatch_bcb_upon_unfix (thread_p, bufptr, holder_status);
  /* bufptr->mutex has been released in above function. */

  PGBUF_BCB_CHECK_MUTEX_LEAKS ();

#if defined(CUBRID_DEBUG)
  /*
   * CONSISTENCIES AND SCRAMBLES
   * You may want to tailor the following debugging block
   * since its operations and their implications are very expensive.
   * Too much I/O
   */
  if (pgbuf_get_check_page_validation_level (PGBUF_DEBUG_PAGE_VALIDATION_ALL))
    {
      /*
       * Check if the content of the page is consistent and then scramble
       * the page to detect illegal access to the page in the future.
       */
      PGBUF_BCB_LOCK (bufptr);
      if (bufptr->fcnt == 0)
	{
	  /* Check for consistency */
	  if (!VPID_ISNULL (&bufptr->vpid) && pgbuf_is_consistent (bufptr, 0) == PGBUF_CONTENT_BAD)
	    {
	      er_log_debug (ARG_FILE_LINE, "pgbuf_unfix: WARNING Pageid = %d|%d seems inconsistent",
			    bufptr->vpid.volid, bufptr->vpid.pageid);
	      /* some problems in the consistency of the given buffer page */
	      pgbuf_dump ();
	    }
	  else
	    {
	      /* the given buffer page is consistent */

	      /* Flush the page if it is dirty */
	      if (pgbuf_bcb_is_dirty (bufptr))
		{
		  /* flush the page with PGBUF_LATCH_FLUSH mode */
		  (void) pgbuf_bcb_safe_flush_force_unlock (thread_p, bufptr, true);
		  /*
		   * Since above function releases bufptr->mutex,
		   * the caller must hold bufptr->mutex again.
		   */
		  PGBUF_BCB_LOCK (bufptr);
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
		  (void) pgbuf_invalidate_bcb (thread_p, bufptr);
		  /*
		   * Since above function releases mutex after flushing,
		   * the caller must hold bufptr->mutex again.
		   */
		  PGBUF_BCB_LOCK (bufptr);
		}

	      pgbuf_scramble (&bufptr->iopage_buffer->iopage);

	      /*
	       * Note that the buffer is not declared for immediate
	       * replacement.
	       * wait for a while to see if an invalid access is found.
	       */
	    }
	}
      PGBUF_BCB_UNLOCK (bufptr);
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

  thrd_index = thread_get_entry_index (thread_p);

  thrd_holder_info = &(pgbuf_Pool.thrd_holder_info[thrd_index]);

  if (thrd_holder_info->num_hold_cnt > 0)
    {
      /* For each BCB holder entry of thread's holder list */
      holder = thrd_holder_info->thrd_hold_list;
      while (holder != NULL)
	{
	  assert (false);

	  CAST_BFPTR_TO_PGPTR (pgptr, holder->bufptr);

#if defined(NDEBUG)
	  pgbuf_unfix_and_init (thread_p, pgptr);

	  /* Within the execution of pgbuf_unfix(), the BCB holder entry is moved from the holder list of BCB to the
	   * free holder list of thread, and the BCB holder entry is removed from the holder list of the thread. */
	  holder = thrd_holder_info->thrd_hold_list;
#else /* NDEBUG */
	  CAST_PGPTR_TO_BFPTR (bufptr, pgptr);
	  assert (!VPID_ISNULL (&bufptr->vpid));

	  latch_mode_str = pgbuf_latch_mode_str (bufptr->latch_mode);
	  zone_str = pgbuf_zone_str (pgbuf_bcb_get_zone (bufptr));

	  /* check if the content of current buffer page is consistent. */
#if defined(CUBRID_DEBUG)
	  consistent = pgbuf_is_consistent (bufptr, 0);
	  consistenet_str = pgbuf_consistent_str (consistent);
#else /* CUBRID_DEBUG */
	  consistent_str = "UNKNOWN";
#endif /* CUBRID_DEBUG */
	  er_log_debug (ARG_FILE_LINE,
			"pgbuf_unfix_all: WARNING %4d %5d %6d %4d %9s %1d %1d %1d %11s %6d|%4d %10s %p %p-%p\n",
			pgbuf_bcb_get_pool_index (bufptr), bufptr->vpid.volid, bufptr->vpid.pageid, bufptr->fcnt,
			latch_mode_str, (int) pgbuf_bcb_is_dirty (bufptr), (int) pgbuf_bcb_is_flushing (bufptr),
			(int) pgbuf_bcb_is_async_flush_request (bufptr), zone_str,
			LSA_AS_ARGS (&bufptr->iopage_buffer->iopage.prv.lsa), consistent_str, (void *) bufptr,
			(void *) (&bufptr->iopage_buffer->iopage.page[0]),
			(void *) (&bufptr->iopage_buffer->iopage.page[DB_PAGESIZE - 1]));

	  holder = holder->thrd_link;
#endif /* NDEBUG */
	}
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
pgbuf_invalidate_debug (THREAD_ENTRY * thread_p, PAGE_PTR pgptr, const char *caller_file, int caller_line)
#else /* NDEBUG */
int
pgbuf_invalidate (THREAD_ENTRY * thread_p, PAGE_PTR pgptr)
#endif				/* NDEBUG */
{
  PGBUF_BCB *bufptr;
  VPID temp_vpid;
  int holder_status;

  if (pgbuf_get_check_page_validation_level (PGBUF_DEBUG_PAGE_VALIDATION_ALL))
    {
      if (pgbuf_is_valid_page_ptr (pgptr) == false)
	{
	  return ER_FAILED;
	}
    }

  /* Get the address of the buffer from the page and invalidate buffer */
  CAST_PGPTR_TO_BFPTR (bufptr, pgptr);
  assert (!VPID_ISNULL (&bufptr->vpid));

  PGBUF_BCB_LOCK (bufptr);

  /*
   * This function is called by the caller while it is fixing the page
   * with PGBUF_LATCH_WRITE mode in CUBRID environment. Therefore,
   * the caller must unfix the page and then invalidate the page.
   */
  if (bufptr->fcnt > 1)
    {
      holder_status = pgbuf_unlatch_thrd_holder (thread_p, bufptr, NULL);

#if !defined (NDEBUG)
      thread_p->get_pgbuf_tracker ().decrement (pgptr);
#endif // !NDEBUG
      /* If the page has been fixed more than one time, just unfix it. */
      /* todo: is this really safe? */
      if (pgbuf_unlatch_bcb_upon_unfix (thread_p, bufptr, holder_status) != NO_ERROR)
	{
	  return ER_FAILED;
	}

      return NO_ERROR;
      /* bufptr->mutex hash been released in above function. */
    }

  /* bufptr->fcnt == 1 */
  /* Currently, bufptr->latch_mode is PGBUF_LATCH_WRITE */
  if (pgbuf_bcb_safe_flush_force_lock (thread_p, bufptr, true) != NO_ERROR)
    {
      ASSERT_ERROR ();
      return ER_FAILED;
    }

  /* save the pageid of the page temporarily. */
  temp_vpid = bufptr->vpid;

  holder_status = pgbuf_unlatch_thrd_holder (thread_p, bufptr, NULL);

#if !defined (NDEBUG)
  thread_p->get_pgbuf_tracker ().decrement (pgptr);
#endif // !NDEBUG
  if (pgbuf_unlatch_bcb_upon_unfix (thread_p, bufptr, holder_status) != NO_ERROR)
    {
      return ER_FAILED;
    }
  /* bufptr->mutex has been released in above function. */

  /* hold mutex again to invalidate the BCB */
  PGBUF_BCB_LOCK (bufptr);

  /* check if the page should be invalidated. */
  if (VPID_ISNULL (&bufptr->vpid) || !VPID_EQ (&temp_vpid, &bufptr->vpid) || bufptr->fcnt > 0
      || pgbuf_bcb_avoid_victim (bufptr))
    {
      PGBUF_BCB_UNLOCK (bufptr);
      return NO_ERROR;
    }

#if defined(CUBRID_DEBUG)
  pgbuf_scramble (&bufptr->iopage_buffer->iopage);
#endif /* CUBRID_DEBUG */

  /* Now, invalidation task is performed after holding a page latch with PGBUF_LATCH_INVALID mode. */
  if (pgbuf_invalidate_bcb (thread_p, bufptr) != NO_ERROR)
    {
      return ER_FAILED;
    }

  /* bufptr->mutex has been released in above function. */
  return NO_ERROR;
}

/*
 * pgbuf_invalidate_all () - Invalidate all unfixed buffers corresponding to the given volume
 *   return: NO_ERROR, or ER_code
 *   volid(in): Permanent Volume Identifier or NULL_VOLID
 *
 * Note: The pages in these buffers are disassociated from the buffers.
 * If a page was dirty, it is flushed before the buffer is invalidated.
 */
#if !defined(NDEBUG)
int
pgbuf_invalidate_all_debug (THREAD_ENTRY * thread_p, VOLID volid, const char *caller_file, int caller_line)
#else /* NDEBUG */
int
pgbuf_invalidate_all (THREAD_ENTRY * thread_p, VOLID volid)
#endif				/* NDEBUG */
{
  PGBUF_BCB *bufptr;
  VPID temp_vpid;
  int bufid;

  /*
   * While searching all the buffer pages or corresponding buffer pages,
   * the caller flushes each buffer page if it is dirty and
   * invalidates the buffer page if it is not fixed on the buffer.
   */
  for (bufid = 0; bufid < pgbuf_Pool.num_buffers; bufid++)
    {
      bufptr = PGBUF_FIND_BCB_PTR (bufid);
      if (VPID_ISNULL (&bufptr->vpid) || (volid != NULL_VOLID && volid != bufptr->vpid.volid))
	{
	  continue;
	}

      PGBUF_BCB_LOCK (bufptr);
      if (VPID_ISNULL (&bufptr->vpid) || (volid != NULL_VOLID && volid != bufptr->vpid.volid) || bufptr->fcnt > 0)
	{
	  /* PGBUF_LATCH_READ/PGBUF_LATCH_WRITE */
	  PGBUF_BCB_UNLOCK (bufptr);
	  continue;
	}

      if (pgbuf_bcb_is_dirty (bufptr))
	{
	  temp_vpid = bufptr->vpid;
	  if (pgbuf_bcb_safe_flush_force_lock (thread_p, bufptr, true) != NO_ERROR)
	    {
	      return ER_FAILED;
	    }

	  /* check if page invalidation should be performed on the page */
	  if (VPID_ISNULL (&bufptr->vpid) || !VPID_EQ (&temp_vpid, &bufptr->vpid)
	      || (volid != NULL_VOLID && volid != bufptr->vpid.volid) || bufptr->fcnt > 0)
	    {
	      PGBUF_BCB_UNLOCK (bufptr);
	      continue;
	    }
	}

      if (pgbuf_bcb_avoid_victim (bufptr))
	{
	  PGBUF_BCB_UNLOCK (bufptr);
	  continue;
	}

#if defined(CUBRID_DEBUG)
      pgbuf_scramble (&bufptr->iopage_buffer->iopage);
#endif /* CUBRID_DEBUG */

      /* Now, page invalidation task is performed while holding a page latch with PGBUF_LATCH_INVALID mode. */
      (void) pgbuf_invalidate_bcb (thread_p, bufptr);
      /* bufptr->mutex has been released in above function. */
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
void
pgbuf_flush (THREAD_ENTRY * thread_p, PAGE_PTR pgptr, bool free_page)
{
  /* caller flushes page but does not really care if page really makes it to disk. or doesn't know what to do in that
   * case... I recommend against using it. */
  if (pgbuf_flush_with_wal (thread_p, pgptr) == NULL)
    {
      ASSERT_ERROR ();
    }
  if (free_page == FREE)
    {
      pgbuf_unfix (thread_p, pgptr);
    }
}

/*
 * pgbuf_flush_with_wal () - Flush a page out to disk after following the wal rule
 *   return: pgptr on success, NULL on failure
 *   pgptr(in): Page pointer
 *
 * Note: The page associated with pgptr is written out to disk (ONLY when the page is dirty)
 *       Before the page is flushed, the WAL rule of the log manager is called.
 */
PAGE_PTR
pgbuf_flush_with_wal (THREAD_ENTRY * thread_p, PAGE_PTR pgptr)
{
  PGBUF_BCB *bufptr;

  if (pgbuf_get_check_page_validation_level (PGBUF_DEBUG_PAGE_VALIDATION_ALL))
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
  assert (bufptr->latch_mode >= PGBUF_LATCH_READ && pgbuf_find_thrd_holder (thread_p, bufptr) != NULL);
  PGBUF_BCB_LOCK (bufptr);

  /* Flush the page only when it is dirty */
  if (pgbuf_bcb_safe_flush_force_unlock (thread_p, bufptr, true) != NO_ERROR)
    {
      ASSERT_ERROR ();
      return NULL;
    }

  return pgptr;
}

/*
 * pgbuf_flush_if_requested () - flush page if needed. this function is used for permanently latched pages. the thread
 *                               holding should periodically check if flush is requested (usually by checkpoint thread).
 *
 * return        : void
 * thread_p (in) : thread entry
 * page (in)     : page
 */
void
pgbuf_flush_if_requested (THREAD_ENTRY * thread_p, PAGE_PTR page)
{
  PGBUF_BCB *bcb;

  if (pgbuf_get_check_page_validation_level (PGBUF_DEBUG_PAGE_VALIDATION_ALL))
    {
      if (pgbuf_is_valid_page_ptr (page) == false)
	{
	  assert (false);
	  return;
	}
    }

  /* NOTE: the page is fixed */
  /* Get the address of the buffer from the page. */
  CAST_PGPTR_TO_BFPTR (bcb, page);
  assert (!VPID_ISNULL (&bcb->vpid));

  /* caller should have write latch, otherwise there is no point in calling this function */
  assert (bcb->latch_mode == PGBUF_LATCH_WRITE && pgbuf_find_thrd_holder (thread_p, bcb) != NULL);

  if (pgbuf_bcb_is_async_flush_request (bcb))
    {
      PGBUF_BCB_LOCK (bcb);
      if (pgbuf_bcb_safe_flush_force_unlock (thread_p, bcb, false) != NO_ERROR)
	{
	  assert (false);
	}
    }

  PGBUF_BCB_CHECK_MUTEX_LEAKS ();
}

static int
pgbuf_flush_all_helper (THREAD_ENTRY * thread_p, VOLID volid, bool is_unfixed_only, bool is_set_lsa_as_null)
{
  PGBUF_BCB *bufptr;
  int i, ret = NO_ERROR;

  /* Flush all unfixed dirty buffers */
  for (i = 0; i < pgbuf_Pool.num_buffers; i++)
    {
      bufptr = PGBUF_FIND_BCB_PTR (i);
      if (!pgbuf_bcb_is_dirty (bufptr) || (volid != NULL_VOLID && volid != bufptr->vpid.volid))
	{
	  continue;
	}

      PGBUF_BCB_LOCK (bufptr);
      /* flush condition check */
      if (!pgbuf_bcb_is_dirty (bufptr) || (is_unfixed_only && bufptr->fcnt > 0)
	  || (volid != NULL_VOLID && volid != bufptr->vpid.volid))
	{
	  PGBUF_BCB_UNLOCK (bufptr);
	  continue;
	}

      if (is_set_lsa_as_null)
	{
	  /* set PageLSA as NULL value */
	  fileio_init_lsa_of_page (&bufptr->iopage_buffer->iopage, IO_PAGESIZE);
	}

      /* flush */
      if (pgbuf_bcb_safe_flush_force_unlock (thread_p, bufptr, true) != NO_ERROR)
	{
	  /* best efforts */
	  assert (false);
	  ret = ER_FAILED;
	}
      /* Above function released mutex regardless of its return value. */
    }

  return ret;
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
int
pgbuf_flush_all (THREAD_ENTRY * thread_p, VOLID volid)
{
  return pgbuf_flush_all_helper (thread_p, volid, false, false);
}

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
int
pgbuf_flush_all_unfixed (THREAD_ENTRY * thread_p, VOLID volid)
{
  return pgbuf_flush_all_helper (thread_p, volid, true, false);
}

/*
 * pgbuf_flush_all_unfixed_and_set_lsa_as_null () - Set lsa to null and flush all unfixed dirty pages out to disk
 *   return: NO_ERROR, or ER_code
 *   volid(in): Permanent Volume Identifier or NULL_VOLID
 *
 * Note: Every dirty page of the specified volume which is unfixed is written
 *       out after its lsa is initialized to a null lsa. If volid is equal to
 *       NULL_VOLID, all dirty pages of all volumes that are unfixed are
 *       flushed to disk after its lsa is initialized to null.
 *       Its use is recommended by only the log and recovery manager.
 */
int
pgbuf_flush_all_unfixed_and_set_lsa_as_null (THREAD_ENTRY * thread_p, VOLID volid)
{
  return pgbuf_flush_all_helper (thread_p, volid, true, true);
}

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
 * pgbuf_get_victim_candidates_from_lru () - get victim candidates from LRU list
 * return                  : number of victims found
 * thread_p (in)           : thread entry
 * check_count (in)        : number of items to verify before abandoning search
 * flush_ratio (in)        : flush ratio
 * assigned_directly (out) : output true if a bcb was assigned directly.
 */
static int
pgbuf_get_victim_candidates_from_lru (THREAD_ENTRY * thread_p, int check_count, float lru_sum_flush_priority,
				      bool * assigned_directly)
{
  int lru_idx, victim_cand_count, i;
  PGBUF_BCB *bufptr;
  int check_count_this_lru;
  float victim_flush_priority_this_lru;
  int count_checked_lists = 0;
#if defined (SERVER_MODE)
  /* as part of handling a rare case when there are rare direct victim waiters although there are plenty victims, flush
   * thread assigns one bcb per iteration directly. this will add only a little overhead in general cases. */
  bool try_direct_assign = true;
#endif /* SERVER_MODE */

  /* init */
  victim_cand_count = 0;
  for (lru_idx = 0; lru_idx < PGBUF_TOTAL_LRU_COUNT; lru_idx++)
    {
      victim_flush_priority_this_lru = pgbuf_Pool.quota.lru_victim_flush_priority_per_lru[lru_idx];
      if (victim_flush_priority_this_lru <= 0)
	{
	  /* no target for this list. */
	  continue;
	}
      ++count_checked_lists;

      check_count_this_lru = (int) (victim_flush_priority_this_lru * (float) check_count / lru_sum_flush_priority);
      check_count_this_lru = MAX (check_count_this_lru, 1);

      i = check_count_this_lru;

      (void) pthread_mutex_lock (&pgbuf_Pool.buf_LRU_list[lru_idx].mutex);

      for (bufptr = pgbuf_Pool.buf_LRU_list[lru_idx].bottom;
	   bufptr != NULL && PGBUF_IS_BCB_IN_LRU_VICTIM_ZONE (bufptr) && i > 0; bufptr = bufptr->prev_BCB, i--)
	{
	  if (pgbuf_bcb_is_dirty (bufptr))
	    {
	      /* save victim candidate information temporarily. */
	      pgbuf_Pool.victim_cand_list[victim_cand_count].bufptr = bufptr;
	      pgbuf_Pool.victim_cand_list[victim_cand_count].vpid = bufptr->vpid;
	      victim_cand_count++;
	    }
#if defined (SERVER_MODE)
	  else if (try_direct_assign && pgbuf_is_any_thread_waiting_for_direct_victim ()
		   && pgbuf_is_bcb_victimizable (bufptr, false) && PGBUF_BCB_TRYLOCK (bufptr) == 0)
	    {
	      if (pgbuf_is_bcb_victimizable (bufptr, true) && pgbuf_assign_direct_victim (thread_p, bufptr))
		{
		  /* assigned directly. don't try any other. */
		  try_direct_assign = false;
		  *assigned_directly = true;
		  perfmon_inc_stat (thread_p, PSTAT_PB_VICTIM_ASSIGN_DIRECT_SEARCH_FOR_FLUSH);
		}
	      PGBUF_BCB_UNLOCK (bufptr);
	    }
#endif /* SERVER_MODE */
	}
      pthread_mutex_unlock (&pgbuf_Pool.buf_LRU_list[lru_idx].mutex);
    }

  if (prm_get_bool_value (PRM_ID_LOG_PGBUF_VICTIM_FLUSH))
    {
      _er_log_debug (ARG_FILE_LINE,
		     "pgbuf_flush_victim_candidates: pgbuf_get_victim_candidates_from_lru %d candidates in %d lists \n",
		     victim_cand_count, count_checked_lists);
    }

  return victim_cand_count;
}

/*
 * pgbuf_flush_victim_candidates () - collect & flush victim candidates
 *
 * return                : error code
 * thread_p (in)         : thread entry
 * flush_ratio (in)      : desired flush ratio
 * perf_tracker (in/out) : time tracker for performance statistics
 * stop (out)            : output to stop looping
 */
int
pgbuf_flush_victim_candidates (THREAD_ENTRY * thread_p, float flush_ratio, PERF_UTIME_TRACKER * perf_tracker,
			       bool * stop)
{
  PGBUF_BCB *bufptr;
  PGBUF_VICTIM_CANDIDATE_LIST *victim_cand_list;
  int i, victim_count = 0;
  int check_count_lru;
  int cfg_check_cnt;
  int total_flushed_count;
  int error = NO_ERROR;
  float lru_miss_rate;
  float lru_dynamic_flush_adj = 1.0f;
  int lru_victim_req_cnt, fix_req_cnt;
  float lru_sum_flush_priority;
  int count_need_wal = 0;
  LOG_LSA lsa_need_wal = LSA_INITIALIZER;
#if defined(SERVER_MODE)
  LOG_LSA save_lsa_need_wal = LSA_INITIALIZER;
  static THREAD_ENTRY *page_flush_thread = NULL;
  bool repeated = false;
#endif /* SERVER_MODE */
  bool is_bcb_locked = false;
  bool detailed_perf = perfmon_is_perf_tracking_and_active (PERFMON_ACTIVATION_FLAG_PB_VICTIMIZATION);
  bool assigned_directly = false;
#if !defined (NDEBUG) && defined (SERVER_MODE)
  bool empty_flushed_bcb_queue = false;
  bool direct_victim_waiters = false;
#endif /* DEBUG && SERVER_MODE */

  // stats
  UINT64 num_skipped_already_flushed = 0;
  UINT64 num_skipped_fixed_or_hot = 0;
  UINT64 num_skipped_need_wal = 0;
  UINT64 num_skipped_flush = 0;

  bool logging = prm_get_bool_value (PRM_ID_LOG_PGBUF_VICTIM_FLUSH);

  er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_LOG_FLUSH_VICTIM_STARTED, 0);
  if (logging)
    {
      _er_log_debug (ARG_FILE_LINE, "pgbuf_flush_victim_candidates: start flush victim candidates\n");
    }

#if !defined(NDEBUG) && defined(SERVER_MODE)
  if (pgbuf_is_page_flush_daemon_available ())
    {
      if (page_flush_thread == NULL)
	{
	  page_flush_thread = thread_p;
	}

      /* This should be fixed */
      assert (page_flush_thread == thread_p);
    }
#endif

  PGBUF_BCB_CHECK_MUTEX_LEAKS ();

  *stop = false;

  pgbuf_compute_lru_vict_target (&lru_sum_flush_priority);

  victim_cand_list = pgbuf_Pool.victim_cand_list;

  victim_count = 0;
  total_flushed_count = 0;
  check_count_lru = 0;

  lru_victim_req_cnt = ATOMIC_TAS_32 (&pgbuf_Pool.monitor.lru_victim_req_cnt, 0);
  fix_req_cnt = ATOMIC_TAS_32 (&pgbuf_Pool.monitor.fix_req_cnt, 0);

  if (fix_req_cnt > lru_victim_req_cnt)
    {
      lru_miss_rate = (float) lru_victim_req_cnt / (float) fix_req_cnt;
    }
  else
    {
      /* overflow of fix counter, we ignore miss rate */
      lru_miss_rate = 0;
    }

  cfg_check_cnt = (int) (pgbuf_Pool.num_buffers * flush_ratio);

  /* Victims will only be flushed, not decached. */

#if defined (SERVER_MODE)
  /* do not apply flush boost during checkpoint; since checkpoint is already flushing pages we expect some of the victim
   * candidates are already flushed by checkpoint */
  if (pgbuf_Pool.is_checkpoint == false)
    {
      lru_dynamic_flush_adj = MAX (1.0f, 1 + (PGBUF_FLUSH_VICTIM_BOOST_MULT - 1) * lru_miss_rate);
      lru_dynamic_flush_adj = MIN (PGBUF_FLUSH_VICTIM_BOOST_MULT, lru_dynamic_flush_adj);
    }
  else
#endif
    {
      lru_dynamic_flush_adj = 1.0f;
    }

  check_count_lru = (int) (cfg_check_cnt * lru_dynamic_flush_adj);
  /* limit the checked BCBs to equivalent of 200 M */
  check_count_lru = MIN (check_count_lru, (200 * 1024 * 1024) / DB_PAGESIZE);

#if !defined (NDEBUG) && defined (SERVER_MODE)
  empty_flushed_bcb_queue = pgbuf_Pool.flushed_bcbs->is_empty ();
  direct_victim_waiters = pgbuf_is_any_thread_waiting_for_direct_victim ();
#endif /* DEBUG && SERVER_MODE */

  if (check_count_lru > 0 && lru_sum_flush_priority > 0)
    {
      victim_count =
	pgbuf_get_victim_candidates_from_lru (thread_p, check_count_lru, lru_sum_flush_priority, &assigned_directly);
    }
  if (victim_count == 0)
    {
      /* We didn't find any victims */
      PERF_UTIME_TRACKER_TIME_AND_RESTART (thread_p, perf_tracker, PSTAT_PB_FLUSH_COLLECT);
      /* if pgbuf_get_victim_candidates_from_lru failed to provide candidates, it means we already flushed enough.
       * give threads looking for victims a chance to find them before looping again. output hint to stop looping. */
      *stop = check_count_lru > 0 && lru_sum_flush_priority > 0;
      goto end;
    }

#if defined (SERVER_MODE)
  /* wake up log flush thread. we need log up to date to be able to flush pages */
  if (log_is_log_flush_daemon_available ())
    {
      log_wakeup_log_flush_daemon ();
    }
  else
#endif /* SERVER_MODE */
    {
      logpb_force_flush_pages (thread_p);
    }

  if (prm_get_bool_value (PRM_ID_PB_SEQUENTIAL_VICTIM_FLUSH) == true)
    {
      qsort ((void *) victim_cand_list, victim_count, sizeof (PGBUF_VICTIM_CANDIDATE_LIST), pgbuf_compare_victim_list);
    }

#if defined (SERVER_MODE)
  pgbuf_Pool.is_flushing_victims = true;
#endif

  if (logging)
    {
      _er_log_debug (ARG_FILE_LINE, "pgbuf_flush_victim_candidates: start flushing collected victim candidates\n");
    }
  if (perf_tracker->is_perf_tracking)
    {
      UINT64 utime;
      tsc_getticks (&perf_tracker->end_tick);
      utime = tsc_elapsed_utime (perf_tracker->end_tick, perf_tracker->start_tick);
      perfmon_time_stat (thread_p, PSTAT_PB_FLUSH_COLLECT, utime);
      if (detailed_perf)
	{
	  perfmon_time_bulk_stat (thread_p, PSTAT_PB_FLUSH_COLLECT_PER_PAGE, utime, victim_count);
	}
      perf_tracker->start_tick = perf_tracker->end_tick;
    }
#if defined (SERVER_MODE)
repeat:
#endif
  count_need_wal = 0;

  /* temporary disable second iteration */
  /* for each victim candidate, do flush task */
  for (i = 0; i < victim_count; i++)
    {
      int flushed_pages = 0;

      bufptr = victim_cand_list[i].bufptr;

      PGBUF_BCB_LOCK (bufptr);

      /* check flush conditions */

      if (!VPID_EQ (&bufptr->vpid, &victim_cand_list[i].vpid) || !pgbuf_bcb_is_dirty (bufptr)
	  || pgbuf_bcb_is_flushing (bufptr))
	{
	  /* must be already flushed or currently flushing */
	  PGBUF_BCB_UNLOCK (bufptr);
	  ++num_skipped_already_flushed;
	  continue;
	}

      if (!PGBUF_IS_BCB_IN_LRU_VICTIM_ZONE (bufptr) || bufptr->latch_mode != PGBUF_NO_LATCH)
	{
	  /* page was fixed or became hot after selected as victim. do not flush it. */
	  PGBUF_BCB_UNLOCK (bufptr);
	  ++num_skipped_fixed_or_hot;
	  continue;
	}

      if (logpb_need_wal (&bufptr->iopage_buffer->iopage.prv.lsa))
	{
	  /* we cannot flush a page unless log has been flushed up until page LSA. otherwise we might have recovery
	   * issues. */
	  count_need_wal++;
	  if (LSA_ISNULL (&lsa_need_wal) || LSA_LE (&lsa_need_wal, &(bufptr->iopage_buffer->iopage.prv.lsa)))
	    {
	      LSA_COPY (&lsa_need_wal, &(bufptr->iopage_buffer->iopage.prv.lsa));
	    }
	  PGBUF_BCB_UNLOCK (bufptr);
	  ++num_skipped_need_wal;
#if defined (SERVER_MODE)
	  log_wakeup_log_flush_daemon ();
#endif /* SERVER_MODE */
	  continue;
	}

      if (PGBUF_NEIGHBOR_PAGES > 1)
	{
	  error = pgbuf_flush_page_and_neighbors_fb (thread_p, bufptr, &flushed_pages);
	  /* BCB mutex already unlocked by neighbor flush function */
	}
      else
	{
	  error = pgbuf_bcb_flush_with_wal (thread_p, bufptr, true, &is_bcb_locked);
	  if (is_bcb_locked)
	    {
	      PGBUF_BCB_UNLOCK (bufptr);
	    }
	  flushed_pages = 1;
	}
      if (error != NO_ERROR)
	{
	  /* if this shows up in statistics or log, consider it a red flag */
	  if (logging)
	    {
	      _er_log_debug (ARG_FILE_LINE, "pgbuf_flush_victim_candidates: error during flush");
	    }
	  goto end;
	}
      total_flushed_count += flushed_pages;
    }

  num_skipped_flush = num_skipped_need_wal + num_skipped_fixed_or_hot + num_skipped_already_flushed;
  if (perf_tracker->is_perf_tracking)
    {
      perfmon_add_stat (thread_p, PSTAT_PB_NUM_SKIPPED_FLUSH, num_skipped_flush);
      if (detailed_perf)
	{
	  perfmon_add_stat (thread_p, PSTAT_PB_NUM_SKIPPED_NEED_WAL, num_skipped_need_wal);
	  perfmon_add_stat (thread_p, PSTAT_PB_NUM_SKIPPED_FIXED_OR_HOT, num_skipped_fixed_or_hot);
	  perfmon_add_stat (thread_p, PSTAT_PB_NUM_SKIPPED_ALREADY_FLUSHED, num_skipped_already_flushed);
	}

      UINT64 utime;
      tsc_getticks (&perf_tracker->end_tick);
      utime = tsc_elapsed_utime (perf_tracker->end_tick, perf_tracker->start_tick);
      perfmon_time_stat (thread_p, PSTAT_PB_FLUSH_FLUSH, utime);
      if (detailed_perf)
	{
	  perfmon_time_bulk_stat (thread_p, PSTAT_PB_FLUSH_FLUSH_PER_PAGE, utime, total_flushed_count);
	}
      perf_tracker->start_tick = perf_tracker->end_tick;
    }

end:

#if defined (SERVER_MODE)
  if (pgbuf_is_any_thread_waiting_for_direct_victim () && victim_count != 0 && count_need_wal == victim_count)
    {
      /* log flush thread did not wake up in time. we must make sure log is flushed and retry. */
      if (repeated)
	{
	  /* already waited and failed again? all bcb's must have changed again (confirm by comparing save_lsa_need_wal
	   * and lsa_need_wal. */
	  assert (LSA_LT (&save_lsa_need_wal, &lsa_need_wal));
	}
      else
	{
	  repeated = true;
	  save_lsa_need_wal = lsa_need_wal;
	  logpb_flush_log_for_wal (thread_p, &lsa_need_wal);
	  goto repeat;
	}
    }

  pgbuf_Pool.is_flushing_victims = false;
#endif /* SERVER_MODE */

  if (logging)
    {
      _er_log_debug (ARG_FILE_LINE,
		     "pgbuf_flush_victim_candidates: flush %d pages from lru lists.\n"
		     "\tvictim_count = %d\n"
		     "\tcheck_count_lru = %d\n"
		     "\tnum_skipped_need_wal = %d\n"
		     "\tnum_skipped_fixed_or_hot = %d\n"
		     "\tnum_skipped_already_flushed = %d\n",
		     total_flushed_count, victim_count, check_count_lru, num_skipped_need_wal, num_skipped_fixed_or_hot,
		     num_skipped_already_flushed);
    }
  er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_LOG_FLUSH_VICTIM_FINISHED, 1, total_flushed_count);

  perfmon_add_stat (thread_p, PSTAT_PB_NUM_FLUSHED, total_flushed_count);

  return error;
}

/*
 * pgbuf_flush_checkpoint () - Flush any unfixed dirty page whose lsa is smaller than the last checkpoint lsa
 *   return:error code or NO_ERROR
 *   flush_upto_lsa(in):
 *   prev_chkpt_redo_lsa(in): Redo_LSA of previous checkpoint
 *   smallest_lsa(out): Smallest LSA of a dirty buffer in buffer pool
 *   flushed_page_cnt(out): The number of flushed pages
 *
 * Note: The function flushes and dirty unfixed page whose LSA is smaller that the last_chkpt_lsa,
 *       it returns the smallest_lsa from the remaining dirty buffers which were not flushed.
 *       This function is used by the log and recovery manager when a checkpoint is issued.
 */
int
pgbuf_flush_checkpoint (THREAD_ENTRY * thread_p, const LOG_LSA * flush_upto_lsa, const LOG_LSA * prev_chkpt_redo_lsa,
			LOG_LSA * smallest_lsa, int *flushed_page_cnt)
{
#define detailed_er_log(...) if (detailed_logging) _er_log_debug (ARG_FILE_LINE, __VA_ARGS__)
  PGBUF_BCB *bufptr;
  int bufid;
  int flushed_page_cnt_local = 0;
  PGBUF_SEQ_FLUSHER *seq_flusher;
  PGBUF_VICTIM_CANDIDATE_LIST *f_list;
  int collected_bcbs;
  int error = NO_ERROR;
  bool detailed_logging = prm_get_bool_value (PRM_ID_LOG_CHKPT_DETAILED);

  detailed_er_log ("pgbuf_flush_checkpoint start : flush_upto_LSA:%d, prev_chkpt_redo_LSA:%d\n",
		   flush_upto_lsa->pageid, (prev_chkpt_redo_lsa ? prev_chkpt_redo_lsa->pageid : -1));

  if (flushed_page_cnt != NULL)
    {
      *flushed_page_cnt = -1;
    }

  /* Things must be truly flushed up to this lsa */
  logpb_flush_log_for_wal (thread_p, flush_upto_lsa);
  LSA_SET_NULL (smallest_lsa);

  seq_flusher = &(pgbuf_Pool.seq_chkpt_flusher);
  f_list = seq_flusher->flush_list;

  LSA_COPY (&seq_flusher->flush_upto_lsa, flush_upto_lsa);

  detailed_er_log ("pgbuf_flush_checkpoint start : start\n");

  collected_bcbs = 0;

#if defined (SERVER_MODE)
  pgbuf_Pool.is_checkpoint = true;
#endif

  for (bufid = 0; bufid < pgbuf_Pool.num_buffers; bufid++)
    {
      if (collected_bcbs >= seq_flusher->flush_max_size)
	{
	  /* flush exiting list */
	  seq_flusher->flush_cnt = collected_bcbs;
	  seq_flusher->flush_idx = 0;

	  qsort (f_list, seq_flusher->flush_cnt, sizeof (f_list[0]), pgbuf_compare_victim_list);

	  error = pgbuf_flush_chkpt_seq_list (thread_p, seq_flusher, prev_chkpt_redo_lsa, smallest_lsa);
	  if (error != NO_ERROR)
	    {
#if defined (SERVER_MODE)
	      pgbuf_Pool.is_checkpoint = false;
#endif
	      return error;
	    }

	  flushed_page_cnt_local += seq_flusher->flushed_pages;

	  collected_bcbs = 0;
	}

      bufptr = PGBUF_FIND_BCB_PTR (bufid);
      PGBUF_BCB_LOCK (bufptr);

      /* flush condition check */
      if (!pgbuf_bcb_is_dirty (bufptr)
	  || (!LSA_ISNULL (&bufptr->oldest_unflush_lsa) && LSA_GT (&bufptr->oldest_unflush_lsa, flush_upto_lsa))
	  || pgbuf_is_temporary_volume (bufptr->vpid.volid))
	{
	  PGBUF_BCB_UNLOCK (bufptr);
	  continue;
	}

      if (!LSA_ISNULL (&bufptr->oldest_unflush_lsa) && prev_chkpt_redo_lsa != NULL && !LSA_ISNULL (prev_chkpt_redo_lsa))
	{
	  if (LSA_LT (&bufptr->oldest_unflush_lsa, prev_chkpt_redo_lsa))
	    {
	      er_stack_push ();
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_CHECKPOINT_SKIP_INVALID_PAGE, 6, bufptr->vpid.pageid,
		      fileio_get_volume_label (bufptr->vpid.volid, PEEK), bufptr->oldest_unflush_lsa.pageid,
		      bufptr->oldest_unflush_lsa.offset, prev_chkpt_redo_lsa->pageid, prev_chkpt_redo_lsa->offset);
	      er_stack_pop ();

	      assert (false);
	    }
	}

      /* add to flush list */
      f_list[collected_bcbs].bufptr = bufptr;
      VPID_COPY (&f_list[collected_bcbs].vpid, &bufptr->vpid);
      PGBUF_BCB_UNLOCK (bufptr);

      collected_bcbs++;

#if defined(SERVER_MODE)
      if (thread_p != NULL && thread_p->shutdown == true)
	{
	  pgbuf_Pool.is_checkpoint = false;
	  return ER_FAILED;
	}
#endif
    }

  if (collected_bcbs > 0)
    {
      /* flush exiting list */
      seq_flusher->flush_cnt = collected_bcbs;
      seq_flusher->flush_idx = 0;

      qsort (f_list, seq_flusher->flush_cnt, sizeof (f_list[0]), pgbuf_compare_victim_list);

      error = pgbuf_flush_chkpt_seq_list (thread_p, seq_flusher, prev_chkpt_redo_lsa, smallest_lsa);
      flushed_page_cnt_local += seq_flusher->flushed_pages;
    }

#if defined (SERVER_MODE)
  pgbuf_Pool.is_checkpoint = false;
#endif

  detailed_er_log ("pgbuf_flush_checkpoint END flushed:%d\n", flushed_page_cnt_local);

  if (flushed_page_cnt != NULL)
    {
      *flushed_page_cnt = flushed_page_cnt_local;
    }

  return error;

#undef  detailed_er_log
}

/*
 * pgbuf_flush_chkpt_seq_list () - flush a sequence of pages during checkpoint
 *   return:error code or NO_ERROR
 *   thread_p(in):
 *   seq_flusher(in): container for list of pages
 *   prev_chkpt_redo_lsa(in): LSA of previous checkpoint
 *   chkpt_smallest_lsa(out): smallest LSA found in a page
 *
 */
static int
pgbuf_flush_chkpt_seq_list (THREAD_ENTRY * thread_p, PGBUF_SEQ_FLUSHER * seq_flusher,
			    const LOG_LSA * prev_chkpt_redo_lsa, LOG_LSA * chkpt_smallest_lsa)
{
#define WAIT_FLUSH_VICTIMS_MAX_MSEC	1500.0f
  int error = NO_ERROR;
  struct timeval *p_limit_time;
  int total_flushed;
  int time_rem;
#if defined (SERVER_MODE)
  int flush_interval, sleep_msecs;
  float wait_victims;
  float chkpt_flush_rate;
  struct timeval limit_time = { 0, 0 };
  struct timeval cur_time = { 0, 0 };
#endif

#if defined (SERVER_MODE)
  sleep_msecs = prm_get_integer_value (PRM_ID_LOG_CHECKPOINT_SLEEP_MSECS);
  if (sleep_msecs > 0)
    {
      chkpt_flush_rate = 1000.0f / (float) sleep_msecs;
    }
  else
    {
      chkpt_flush_rate = 1000.0f;
    }

  flush_interval = (int) (1000.0f * PGBUF_CHKPT_BURST_PAGES / chkpt_flush_rate);
  seq_flusher->interval_msec = flush_interval;
#endif

  total_flushed = 0;
  seq_flusher->control_flushed = 0;
  seq_flusher->control_intervals_cnt = 0;
  while (seq_flusher->flush_idx < seq_flusher->flush_cnt)
    {
#if defined (SERVER_MODE)
      if (thread_p != NULL && thread_p->shutdown)
	{
	  // stop
	  return ER_FAILED;
	}

      gettimeofday (&cur_time, NULL);

      /* compute time limit for allowed flush interval */
      timeval_add_msec (&limit_time, &cur_time, flush_interval);

      seq_flusher->flush_rate = chkpt_flush_rate;
      p_limit_time = &limit_time;
#else
      p_limit_time = NULL;
#endif

#if defined (SERVER_MODE)
      wait_victims = 0;
      while (pgbuf_Pool.is_flushing_victims == true && wait_victims < WAIT_FLUSH_VICTIMS_MAX_MSEC)
	{
	  /* wait 100 micro-seconds */
	  thread_sleep (0.1f);
	  wait_victims += 0.1f;
	}
#endif

      error = pgbuf_flush_seq_list (thread_p, seq_flusher, p_limit_time, prev_chkpt_redo_lsa, chkpt_smallest_lsa,
				    &time_rem);
      total_flushed += seq_flusher->flushed_pages;

      if (error != NO_ERROR)
	{
	  seq_flusher->flushed_pages = total_flushed;
	  return error;
	}

#if defined (SERVER_MODE)
      if (time_rem > 0)
	{
	  thread_sleep (time_rem);
	}
#endif
    }

  seq_flusher->flushed_pages = total_flushed;

  return error;
#undef WAIT_FLUSH_VICTIMS_MAX_MSEC
}

/*
 * pgbuf_flush_seq_list () - flushes a sequence of pages
 *   return:error code or NO_ERROR
 *   thread_p(in):
 *   seq_flusher(in): container for list of pages
 *   limit_time(in): absolute time limit allowed for this call
 *   prev_chkpt_redo_lsa(in): LSA of previous checkpoint
 *   chkpt_smallest_lsa(out): smallest LSA found in a page
 *   time_rem(in): time remaining until limit time expires
 *
 *  Note : burst_mode from seq_flusher container controls how the flush is performed:
 *	    - if enabled, an amount of pages is flushed as soon as possible,
 *	      according to desired flush rate and time limit
 *	    - if disabled, the same amount of pages is flushed, but with a
 *	      pause between each flushed page.
 *	   Since data flush is concurrent with other IO, burst mode increases
 *	   the chance that data and other IO sequences do not mix at IO
 *	   scheduler level and break each-other's sequentiality.
 */
static int
pgbuf_flush_seq_list (THREAD_ENTRY * thread_p, PGBUF_SEQ_FLUSHER * seq_flusher, struct timeval *limit_time,
		      const LOG_LSA * prev_chkpt_redo_lsa, LOG_LSA * chkpt_smallest_lsa, int *time_rem)
{
#define detailed_er_log(...) if (detailed_logging) _er_log_debug (ARG_FILE_LINE, __VA_ARGS__)
  PGBUF_BCB *bufptr;
  PGBUF_VICTIM_CANDIDATE_LIST *f_list;
  int error = NO_ERROR;
  int avail_time_msec = 0, time_rem_msec = 0;
#if defined (SERVER_MODE)
  double sleep_msecs = 0;
  struct timeval cur_time = { 0, 0 };
#endif /* SERVER_MODE */
  int flush_per_interval;
  int cnt_writes;
  int dropped_pages;
  bool done_flush;
  float control_est_flush_total = 0;
  int control_total_cnt_intervals = 0;
  bool ignore_time_limit = false;
  bool flush_if_already_flushed;
  bool locked_bcb = false;
  bool detailed_logging = prm_get_bool_value (PRM_ID_LOG_CHKPT_DETAILED);

  assert (seq_flusher != NULL);
  f_list = seq_flusher->flush_list;

#if defined (SERVER_MODE)
  gettimeofday (&cur_time, NULL);

  if (seq_flusher->burst_mode == true)
    {
      assert_release (limit_time != NULL);
    }

  *time_rem = 0;
  if (limit_time != NULL)
    {
      /* limited time job: amount to flush in this interval */
      avail_time_msec = (int) timeval_diff_in_msec (limit_time, &cur_time);

      control_total_cnt_intervals = (int) (1000.f / (float) seq_flusher->interval_msec + 0.5f);

      if (seq_flusher->control_intervals_cnt > 0)
	{
	  control_est_flush_total =
	    (seq_flusher->flush_rate * (float) (seq_flusher->control_intervals_cnt + 1) /
	     (float) control_total_cnt_intervals);

	  flush_per_interval = (int) (control_est_flush_total - seq_flusher->control_flushed);
	}
      else
	{
	  flush_per_interval = (int) (seq_flusher->flush_rate / control_total_cnt_intervals);
	  if (seq_flusher->control_intervals_cnt < 0)
	    {
	      flush_per_interval -= seq_flusher->control_flushed;
	    }
	}
    }
  else
    {
      /* flush all */
      avail_time_msec = -1;
      flush_per_interval = seq_flusher->flush_cnt;
    }

  flush_per_interval =
    (int) MAX (flush_per_interval, (PGBUF_CHKPT_MIN_FLUSH_RATE * seq_flusher->interval_msec) / 1000.0f);
#else
  flush_per_interval = seq_flusher->flush_cnt;
#endif /* SERVER_MODE */

  detailed_er_log ("pgbuf_flush_seq_list (%s): start_idx:%d, flush_cnt:%d, LSA_flush:%d, "
		   "flush_rate:%.2f, control_flushed:%d, this_interval:%d, "
		   "Est_tot_flush:%.2f, control_intervals:%d, %d Avail_time:%d\n", "chkpt",
		   seq_flusher->flush_idx, seq_flusher->flush_cnt, seq_flusher->flush_upto_lsa.pageid,
		   seq_flusher->flush_rate, seq_flusher->control_flushed, flush_per_interval, control_est_flush_total,
		   seq_flusher->control_intervals_cnt, control_total_cnt_intervals, avail_time_msec);

  /* Start to flush */
  cnt_writes = 0;
  dropped_pages = 0;
  seq_flusher->flushed_pages = 0;

  for (; seq_flusher->flush_idx < seq_flusher->flush_cnt && seq_flusher->flushed_pages < flush_per_interval;
       seq_flusher->flush_idx++)
    {
      bufptr = f_list[seq_flusher->flush_idx].bufptr;

      /* prefer sequentiality to an unnecessary flush; skip already flushed page if is the last in list or if there is
       * already a gap due to missing next page */
      flush_if_already_flushed = true;
      if (seq_flusher->flush_idx + 1 >= seq_flusher->flush_cnt
	  || f_list[seq_flusher->flush_idx].vpid.pageid + 1 != f_list[seq_flusher->flush_idx + 1].vpid.pageid)
	{
	  flush_if_already_flushed = false;
	}

      PGBUF_BCB_LOCK (bufptr);
      locked_bcb = true;

      if (!VPID_EQ (&bufptr->vpid, &f_list[seq_flusher->flush_idx].vpid) || !pgbuf_bcb_is_dirty (bufptr)
	  || (flush_if_already_flushed == false && !LSA_ISNULL (&bufptr->oldest_unflush_lsa)
	      && LSA_GT (&bufptr->oldest_unflush_lsa, &seq_flusher->flush_upto_lsa)))
	{
	  PGBUF_BCB_UNLOCK (bufptr);
	  dropped_pages++;
	  continue;
	}

      done_flush = false;
      if (pgbuf_bcb_safe_flush_force_lock (thread_p, bufptr, true) == NO_ERROR)
	{
	  if (!LSA_ISNULL (&bufptr->oldest_unflush_lsa)
	      && LSA_LE (&bufptr->oldest_unflush_lsa, &seq_flusher->flush_upto_lsa))
	    {
	      /* I am not sure if this is really possible. But let's assume that bcb was already flushing before
	       * checkpoint reached it. And that it was modified again. And that the new oldest_unflush_lsa is less than
	       * flush_upto_lsa. It may seem that many planets should align, but let's be conservative and flush again.
	       */
	      detailed_er_log ("pgbuf_flush_seq_list: flush again %d|%d; oldest_unflush_lsa=%lld|%d, "
			       "flush_upto_lsa=%lld|%d \n", VPID_AS_ARGS (&bufptr->vpid),
			       LSA_AS_ARGS (&bufptr->oldest_unflush_lsa), LSA_AS_ARGS (&seq_flusher->flush_upto_lsa));
	      if (pgbuf_bcb_safe_flush_internal (thread_p, bufptr, true, &locked_bcb) == NO_ERROR)
		{
		  /* now we should be ok. */
		  assert (LSA_ISNULL (&bufptr->oldest_unflush_lsa)
			  || LSA_GT (&bufptr->oldest_unflush_lsa, &seq_flusher->flush_upto_lsa));
		  done_flush = true;
		}
	      else
		{
		  assert (false);
		}
	    }
	  else
	    {
	      done_flush = true;
	    }
	}
      else
	{
	  assert (false);
	  locked_bcb = false;
	}

      if (done_flush)
	{
	  seq_flusher->flushed_pages++;
	}
      else
	{
	  assert (false);

	  if (!locked_bcb)
	    {
	      PGBUF_BCB_LOCK (bufptr);
	      locked_bcb = true;
	    }

	  /* get the smallest oldest_unflush_lsa */
	  if (!LSA_ISNULL (&bufptr->oldest_unflush_lsa)
	      && (LSA_ISNULL (chkpt_smallest_lsa) || LSA_LT (&bufptr->oldest_unflush_lsa, chkpt_smallest_lsa)))
	    {
	      LSA_COPY (chkpt_smallest_lsa, &bufptr->oldest_unflush_lsa);
	    }
	}

      if (locked_bcb)
	{
	  PGBUF_BCB_UNLOCK (bufptr);
	  locked_bcb = false;
	}

#if defined(SERVER_MODE)
      if (limit_time != NULL && ignore_time_limit == false)
	{
	  gettimeofday (&cur_time, NULL);
	  if (cur_time.tv_sec > limit_time->tv_sec
	      || (cur_time.tv_sec == limit_time->tv_sec && cur_time.tv_usec >= limit_time->tv_usec))
	    {
	      *time_rem = -1;
	      break;
	    }
	}

      if (seq_flusher->burst_mode == false && seq_flusher->flush_rate > 0
	  && seq_flusher->flushed_pages < flush_per_interval && ignore_time_limit == false)
	{
	  if (limit_time != NULL)
	    {
	      time_rem_msec = (int) timeval_diff_in_msec (limit_time, &cur_time);
	      sleep_msecs = time_rem_msec / (flush_per_interval - seq_flusher->flushed_pages);
	    }
	  else
	    {
	      sleep_msecs = 1000.0f / (double) (seq_flusher->flush_rate);
	    }

	  if (sleep_msecs > (1000.0f / PGBUF_CHKPT_MAX_FLUSH_RATE))
	    {
	      thread_sleep (sleep_msecs);
	    }
	}

      if (thread_p && thread_p->shutdown == true)
	{
	  return ER_FAILED;
	}
#endif /* SERVER_MODE */
    }

#if defined (SERVER_MODE)
  gettimeofday (&cur_time, NULL);
  if (limit_time != NULL)
    {
      time_rem_msec = (int) timeval_diff_in_msec (limit_time, &cur_time);
      *time_rem = time_rem_msec;

      seq_flusher->control_intervals_cnt++;
      if (seq_flusher->control_intervals_cnt >= control_total_cnt_intervals || ignore_time_limit == true)
	{
	  seq_flusher->control_intervals_cnt = 0;
	}

      if (seq_flusher->control_intervals_cnt == 0)
	{
	  seq_flusher->control_flushed = 0;
	}
      else
	{
	  seq_flusher->control_flushed += seq_flusher->flushed_pages;
	}
    }
#endif /* SERVER_MODE */

  detailed_er_log ("pgbuf_flush_seq_list end (%s): %s %s pages : %d written/%d dropped, "
		   "Remaining_time:%d, Avail_time:%d, Curr:%d/%d,", "ckpt",
		   ((time_rem_msec <= 0) ? "[Expired] " : ""), (ignore_time_limit ? "[boost]" : ""),
		   seq_flusher->flushed_pages, dropped_pages, time_rem_msec, avail_time_msec, seq_flusher->flush_idx,
		   seq_flusher->flush_cnt);

  return error;
#undef detailed_er_log
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
pgbuf_copy_to_area (THREAD_ENTRY * thread_p, const VPID * vpid, int start_offset, int length, void *area, bool do_fetch)
{
  PGBUF_BUFFER_HASH *hash_anchor;
  PGBUF_BCB *bufptr;
  PAGE_PTR pgptr;

  if (logtb_is_interrupted (thread_p, true, &pgbuf_Pool.check_for_interrupts) == true)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INTERRUPTED, 0);
      return NULL;
    }

#if defined(CUBRID_DEBUG)
  if (start_offset < 0 || (start_offset + length) > DB_PAGESIZE)
    {
      er_log_debug (ARG_FILE_LINE,
		    "pgbuf_copy_to_area: SYSTEM ERROR.. Trying to copy"
		    " from beyond page boundary limits. Start_offset = %d, length = %d\n", start_offset, length);
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return NULL;
    }
#endif /* CUBRID_DEBUG */

  /* Is this a resident page ? */
  hash_anchor = &(pgbuf_Pool.buf_hash_table[PGBUF_HASH_VALUE (vpid)]);
  bufptr = pgbuf_search_hash_chain (thread_p, hash_anchor, vpid);

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
	  pgptr = pgbuf_fix (thread_p, vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
	  if (pgptr != NULL)
	    {
#if !defined (NDEBUG)
	      (void) pgbuf_check_page_ptype (thread_p, pgptr, PAGE_AREA);
#endif /* !NDEBUG */

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
	  if (pgbuf_get_check_page_validation_level (PGBUF_DEBUG_PAGE_VALIDATION_ALL))
	    {
	      if (pgbuf_is_valid_page (thread_p, vpid, false NULL, NULL) != DISK_VALID)
		{
		  return NULL;
		}
	    }

	  /* Record number of reads in statistics */
	  perfmon_inc_stat (thread_p, PSTAT_PB_NUM_IOREADS);

	  if (fileio_read_user_area (thread_p, fileio_get_volume_descriptor (vpid->volid), vpid->pageid, start_offset,
				     length, area) == NULL)
	    {
	      area = NULL;
	    }
	}
#endif
    }
  else
    {
      /* the caller is holding only bufptr->mutex. */
      CAST_BFPTR_TO_PGPTR (pgptr, bufptr);

#if !defined (NDEBUG)
      (void) pgbuf_check_page_ptype (thread_p, pgptr, PAGE_AREA);
#endif /* !NDEBUG */

      memcpy (area, (char *) pgptr + start_offset, length);

      if (thread_get_sort_stats_active (thread_p))
	{
	  perfmon_inc_stat (thread_p, PSTAT_SORT_NUM_DATA_PAGES);
	}

      /* release mutex */
      PGBUF_BCB_UNLOCK (bufptr);
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
pgbuf_copy_from_area (THREAD_ENTRY * thread_p, const VPID * vpid, int start_offset, int length, void *area,
		      bool do_fetch, TDE_ALGORITHM tde_algo)
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
  bufptr = pgbuf_search_hash_chain (thread_p, hash_anchor, vpid);

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
	  /* Do not cache the page in the page buffer pool. Write the desired portion of the page directly to disk */
	  if (pgbuf_get_check_page_validation_level (PGBUF_DEBUG_PAGE_VALIDATION_ALL))
	    {
	      if (pgbuf_is_valid_page (thread_p, vpid, false NULL, NULL) != DISK_VALID)
		{
		  return NULL;
		}
	    }

	  /* Record number of reads in statistics */
	  perfmon_inc_stat (thread_p, PSTAT_PB_NUM_IOWRITES);

	  vol_fd = fileio_get_volume_descriptor (vpid->volid);
	  if (fileio_write_user_area (thread_p, vol_fd, vpid->pageid, start_offset, length, area) == NULL)
	    {
	      area = NULL;
	    }

	  return area;
	}
#endif
    }
  else
    {
      /* the caller is holding only bufptr->mutex. */
      PGBUF_BCB_UNLOCK (bufptr);
    }

  pgptr = pgbuf_fix (thread_p, vpid, NEW_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (pgptr != NULL)
    {
      (void) pgbuf_set_page_ptype (thread_p, pgptr, PAGE_AREA);
      pgbuf_set_tde_algorithm (thread_p, pgptr, tde_algo, true);

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
 * pgbuf_set_dirty () - Mark as modified the buffer associated with pgptr and optionally free the page
 *   return: void
 *   pgptr(in): Pointer to page
 *   free_page(in): Free the page too ?
 */
#if !defined(NDEBUG)
void
pgbuf_set_dirty_debug (THREAD_ENTRY * thread_p, PAGE_PTR pgptr, bool free_page, const char *caller_file,
		       int caller_line, const char *caller_func)
#else
void
pgbuf_set_dirty (THREAD_ENTRY * thread_p, PAGE_PTR pgptr, bool free_page)
#endif
{
  PGBUF_BCB *bufptr;

  if (pgbuf_get_check_page_validation_level (PGBUF_DEBUG_PAGE_VALIDATION_ALL))
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
      disk_volheader_check_magic (thread_p, pgptr);
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

  if (pgbuf_get_check_page_validation_level (PGBUF_DEBUG_PAGE_VALIDATION_ALL))
    {
      if (pgbuf_is_valid_page_ptr (pgptr) == false)
	{
	  return NULL;
	}
    }

  /* NOTE: Does not need to hold mutex since the page is fixed */

  CAST_PGPTR_TO_IOPGPTR (io_pgptr, pgptr);
  return &io_pgptr->prv.lsa;
}

/*
 * pgbuf_set_lsa () - Set the log sequence address of the page to the given lsa
 *   return: page lsa or NULL
 *   pgptr(in): Pointer to page
 *   lsa_ptr(in): Log Sequence address
 *
 * Note: This function is for the exclusive use of the log and recovery manager.
 */
#if !defined(NDEBUG)
const LOG_LSA *
pgbuf_set_lsa_debug (THREAD_ENTRY * thread_p, PAGE_PTR pgptr, const LOG_LSA * lsa_ptr, const char *caller_file,
		     int caller_line, const char *caller_func)
#else
const LOG_LSA *
pgbuf_set_lsa (THREAD_ENTRY * thread_p, PAGE_PTR pgptr, const LOG_LSA * lsa_ptr)
#endif
{
  PGBUF_BCB *bufptr;

  if (pgbuf_get_check_page_validation_level (PGBUF_DEBUG_PAGE_VALIDATION_ALL))
    {
      if (pgbuf_is_valid_page_ptr (pgptr) == false)
	{
	  return NULL;
	}
    }

  assert (lsa_ptr != NULL);

  /* NOTE: Does not need to hold mutex since the page is fixed */

  /* Get the address of the buffer from the page and set buffer dirty */
  CAST_PGPTR_TO_BFPTR (bufptr, pgptr);

  /*
   * Don't change LSA of temporary volumes or auxiliary volumes.
   * (e.g., those of copydb, backupdb).
   */
  if (pgbuf_is_temp_lsa (bufptr->iopage_buffer->iopage.prv.lsa)
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
      pgbuf_init_temp_page_lsa (&bufptr->iopage_buffer->iopage, IO_PAGESIZE);
      if (logtb_is_current_active (thread_p))
	{
	  return NULL;
	}
    }

  fileio_set_page_lsa (&bufptr->iopage_buffer->iopage, lsa_ptr, IO_PAGESIZE);

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
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_CHECKPOINT_SKIP_INVALID_PAGE, 6, bufptr->vpid.pageid,
		      fileio_get_volume_label (bufptr->vpid.volid, PEEK), lsa_ptr->pageid, lsa_ptr->offset,
		      log_Gl.chkpt_redo_lsa.pageid, log_Gl.chkpt_redo_lsa.offset);
	      er_stack_pop ();

	      assert (false);
	    }

	}
      LSA_COPY (&bufptr->oldest_unflush_lsa, lsa_ptr);
    }

#if defined (NDEBUG)
  /* We expect the page was or will be set as dirty before unfix. However, there might be a missing case to set dirty.
   * It is correct to set dirty here. Note that we have set lsa of the page and it should be also flushed.
   * But we also want to find missing cases and fix them. Make everything sure for release builds.
   */
  pgbuf_set_dirty_buffer_ptr (thread_p, bufptr);
#endif /* NDEBUG */

  return lsa_ptr;
}

/*
 * pgbuf_reset_temp_lsa () -  Reset LSA of temp volume to special temp LSA (-2,-2)
 *   return: void
 *   pgptr(in): Pointer to page
 */
void
pgbuf_reset_temp_lsa (PAGE_PTR pgptr)
{
  PGBUF_BCB *bufptr;

  CAST_PGPTR_TO_BFPTR (bufptr, pgptr);
  pgbuf_init_temp_page_lsa (&bufptr->iopage_buffer->iopage, IO_PAGESIZE);
}

/*
 * pgbuf_set_tde_algorithm () - set tde encryption algorithm to the page
 *   return: void
 *   thread_p (in)  : Thread entry
 *   pgptr(in): Page pointer
 *   tde_algo (in) : encryption algorithm - NONE, AES, ARIA
 */
void
pgbuf_set_tde_algorithm (THREAD_ENTRY * thread_p, PAGE_PTR pgptr, TDE_ALGORITHM tde_algo, bool skip_logging)
{
  FILEIO_PAGE *iopage = NULL;
  TDE_ALGORITHM prev_tde_algo = TDE_ALGORITHM_NONE;

  assert (tde_is_loaded () || tde_algo == TDE_ALGORITHM_NONE);

  prev_tde_algo = pgbuf_get_tde_algorithm (pgptr);

  if (prev_tde_algo == tde_algo)
    {
      return;
    }

  CAST_PGPTR_TO_IOPGPTR (iopage, pgptr);

  tde_er_log ("pgbuf_set_tde_algorithm(): VPID = %d|%d, tde_algorithm = %s\n", iopage->prv.volid,
	      iopage->prv.pageid, tde_get_algorithm_name (tde_algo));

  if (!skip_logging)
    {
      log_append_undoredo_data2 (thread_p, RVPGBUF_SET_TDE_ALGORITHM, NULL, pgptr, 0, sizeof (TDE_ALGORITHM),
				 sizeof (TDE_ALGORITHM), &prev_tde_algo, &tde_algo);
    }

  /* clear tde encryption bits */
  iopage->prv.pflag &= ~FILEIO_PAGE_FLAG_ENCRYPTED_MASK;

  switch (tde_algo)
    {
    case TDE_ALGORITHM_AES:
      iopage->prv.pflag |= FILEIO_PAGE_FLAG_ENCRYPTED_AES;
      break;
    case TDE_ALGORITHM_ARIA:
      iopage->prv.pflag |= FILEIO_PAGE_FLAG_ENCRYPTED_ARIA;
      break;
    case TDE_ALGORITHM_NONE:
      break;			// do nothing, already cleared
    default:
      assert (false);
    }

  pgbuf_set_dirty (thread_p, pgptr, DONT_FREE);
}

/*
 * pgbuf_rv_set_tde_algorithm () - recovery setting tde encryption algorithm to the page
 *   return        : NO_ERROR, or ER_code
 *   thread_p (in)  : Thread entry
 *   pgptr(in): Page pointer
 *   tde_algo (in) : encryption algorithm - NONE, AES, ARIA
 */
int
pgbuf_rv_set_tde_algorithm (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  FILEIO_PAGE *iopage = NULL;
  PAGE_PTR pgptr = rcv->pgptr;
  TDE_ALGORITHM tde_algo = *((TDE_ALGORITHM *) rcv->data);

  assert (rcv->length == sizeof (TDE_ALGORITHM));

  pgbuf_set_tde_algorithm (thread_p, pgptr, tde_algo, true);

  return NO_ERROR;
}

/*
 * pgbuf_get_tde_algorithm () - get tde encryption algorithm of the page
 *   return: TDE_ALGORITHM
 *   pgptr(in): Page pointer
 *   tde_algo (out) : encryption algorithm - NONE, AES, ARIA
 */
TDE_ALGORITHM
pgbuf_get_tde_algorithm (PAGE_PTR pgptr)
{
  FILEIO_PAGE *iopage = NULL;

  CAST_PGPTR_TO_IOPGPTR (iopage, pgptr);

  // encryption algorithms are exclusive
  assert (!((iopage->prv.pflag & FILEIO_PAGE_FLAG_ENCRYPTED_AES) &&
	    (iopage->prv.pflag & FILEIO_PAGE_FLAG_ENCRYPTED_ARIA)));

  if (iopage->prv.pflag & FILEIO_PAGE_FLAG_ENCRYPTED_AES)
    {
      return TDE_ALGORITHM_AES;
    }
  else if (iopage->prv.pflag & FILEIO_PAGE_FLAG_ENCRYPTED_ARIA)
    {
      return TDE_ALGORITHM_ARIA;
    }
  else
    {
      return TDE_ALGORITHM_NONE;
    }
}

/*
 * pgbuf_get_vpid () - Find the volume and page identifier associated with the passed buffer
 *   return: void
 *   pgptr(in): Page pointer
 *   vpid(out): Volume and page identifier
 */
void
pgbuf_get_vpid (PAGE_PTR pgptr, VPID * vpid)
{
  PGBUF_BCB *bufptr;

  if (pgbuf_get_check_page_validation_level (PGBUF_DEBUG_PAGE_VALIDATION_ALL))
    {
      if (pgbuf_is_valid_page_ptr (pgptr) == false)
	{
	  VPID_SET_NULL (vpid);
	  return;
	}
    }

  /* NOTE: Does not need to hold mutex since the page is fixed */

  CAST_PGPTR_TO_BFPTR (bufptr, pgptr);
  *vpid = bufptr->vpid;
}

/*
 * pgbuf_get_vpid_ptr () - Find the volume and page identifier associated with the passed buffer
 *   return: pointer to vpid
 *   pgptr(in): Page pointer
 *
 * Note: Once the buffer is freed, the content of the vpid pointer may be
 *       updated by the page buffer manager, thus a lot of care should be taken.
 *       The values of the vpid pointer must not be altered by the caller.
 *       Once the page is freed, the vpid pointer should not be used any longer.
 */
VPID *
pgbuf_get_vpid_ptr (PAGE_PTR pgptr)
{
  PGBUF_BCB *bufptr;

  if (pgbuf_get_check_page_validation_level (PGBUF_DEBUG_PAGE_VALIDATION_ALL))
    {
      if (pgbuf_is_valid_page_ptr (pgptr) == false)
	{
	  return NULL;
	}
    }

  /* NOTE: Does not need to hold mutex since the page is fixed */

  CAST_PGPTR_TO_BFPTR (bufptr, pgptr);
  return &(bufptr->vpid);
}

/*
 * pgbuf_get_latch_mode () - Find the latch mode associated with the passed buffer
 *   return: latch mode
 *   pgptr(in): Page pointer
 */
PGBUF_LATCH_MODE
pgbuf_get_latch_mode (PAGE_PTR pgptr)
{
  PGBUF_BCB *bufptr;

  if (pgbuf_get_check_page_validation_level (PGBUF_DEBUG_PAGE_VALIDATION_ALL))
    {
      if (pgbuf_is_valid_page_ptr (pgptr) == false)
	{
	  return PGBUF_LATCH_INVALID;
	}
    }

  /* NOTE: Does not need to hold mutex since the page is fixed */

  CAST_PGPTR_TO_BFPTR (bufptr, pgptr);
  return bufptr->latch_mode;
}

/*
 * pgbuf_get_page_id () - Find the page identifier associated with the passed buffer
 *   return: PAGEID
 *   pgptr(in): Page pointer
 */
PAGEID
pgbuf_get_page_id (PAGE_PTR pgptr)
{
  PGBUF_BCB *bufptr;

  /* NOTE: Does not need to hold mutex since the page is fixed */

  CAST_PGPTR_TO_BFPTR (bufptr, pgptr);
  assert (pgbuf_check_bcb_page_vpid (bufptr, false) == true);

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

  if (pgbuf_get_check_page_validation_level (PGBUF_DEBUG_PAGE_VALIDATION_ALL))
    {
      if (pgbuf_is_valid_page_ptr (pgptr) == false)
	{
	  return PAGE_UNKNOWN;	/* TODO - need to return error_code */
	}
    }

  /* NOTE: Does not need to hold mutex since the page is fixed */

  CAST_PGPTR_TO_BFPTR (bufptr, pgptr);
  assert_release (pgbuf_check_bcb_page_vpid (bufptr, false) == true);

  ptype = (PAGE_TYPE) (bufptr->iopage_buffer->iopage.prv.ptype);

  assert (PAGE_UNKNOWN <= (int) ptype);
  assert (ptype <= PAGE_LAST);

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

  if (pgbuf_get_check_page_validation_level (PGBUF_DEBUG_PAGE_VALIDATION_ALL))
    {
      if (pgbuf_is_valid_page_ptr (pgptr) == false)
	{
	  return NULL_VOLID;
	}
    }

  /* NOTE: Does not need to hold mutex since the page is fixed */

  CAST_PGPTR_TO_BFPTR (bufptr, pgptr);
  return bufptr->vpid.volid;
}

/*
 * pgbuf_get_volume_label () - Find the name of the volume associated with the passed buffer
 *   return: Volume label
 *   pgptr(in): Page pointer
 */
const char *
pgbuf_get_volume_label (PAGE_PTR pgptr)
{
  PGBUF_BCB *bufptr;

  /* NOTE: Does not need to hold mutex since the page is fixed */

  CAST_PGPTR_TO_BFPTR (bufptr, pgptr);
  assert (!VPID_ISNULL (&bufptr->vpid));

  return fileio_get_volume_label (bufptr->vpid.volid, PEEK);
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
      && logtb_is_interrupted (thread_p, true, &pgbuf_Pool.check_for_interrupts) == true)
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
 * pgbuf_set_lsa_as_temporary () - The log sequence address of the page is set to temporary lsa address
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

  pgbuf_init_temp_page_lsa (&bufptr->iopage_buffer->iopage, IO_PAGESIZE);
  pgbuf_set_dirty_buffer_ptr (thread_p, bufptr);
}

/*
 * pgbuf_set_bcb_page_vpid () -
 *   return: void
 *   bufptr(in): pointer to buffer page
 *
 */
STATIC_INLINE void
pgbuf_set_bcb_page_vpid (PGBUF_BCB * bufptr)
{
  if (bufptr == NULL || VPID_ISNULL (&bufptr->vpid))
    {
      assert (bufptr != NULL);
      assert (!VPID_ISNULL (&bufptr->vpid));
      return;
    }

  /* perm volume */
  if (bufptr->vpid.volid > NULL_VOLID)
    {
      /* Check if is the first time */
      if (bufptr->iopage_buffer->iopage.prv.pageid == NULL_PAGEID
	  && bufptr->iopage_buffer->iopage.prv.volid == NULL_VOLID)
	{
	  /* Set Page identifier */
	  bufptr->iopage_buffer->iopage.prv.pageid = bufptr->vpid.pageid;
	  bufptr->iopage_buffer->iopage.prv.volid = bufptr->vpid.volid;

	  bufptr->iopage_buffer->iopage.prv.ptype = PAGE_UNKNOWN;
	  bufptr->iopage_buffer->iopage.prv.p_reserve_1 = 0;
	  bufptr->iopage_buffer->iopage.prv.p_reserve_2 = 0;
	  bufptr->iopage_buffer->iopage.prv.tde_nonce = 0;
	}
      else
	{
	  /* values not reset upon page deallocation */
	  assert (bufptr->iopage_buffer->iopage.prv.volid == bufptr->vpid.volid);
	  assert (bufptr->iopage_buffer->iopage.prv.pageid == bufptr->vpid.pageid);
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
pgbuf_set_page_ptype (THREAD_ENTRY * thread_p, PAGE_PTR pgptr, PAGE_TYPE ptype)
{
  PGBUF_BCB *bufptr;

  assert (pgptr != NULL);

  if (pgbuf_get_check_page_validation_level (PGBUF_DEBUG_PAGE_VALIDATION_ALL))
    {
      if (pgbuf_is_valid_page_ptr (pgptr) == false)
	{
	  assert (false);
	  return;
	}
    }

  CAST_PGPTR_TO_BFPTR (bufptr, pgptr);
  assert (!VPID_ISNULL (&bufptr->vpid));

  /* Set Page identifier if needed */
  pgbuf_set_bcb_page_vpid (bufptr);

  if (pgbuf_check_bcb_page_vpid (bufptr, false) != true)
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

  if (pgbuf_is_temp_lsa (bufptr->iopage_buffer->iopage.prv.lsa)
      || pgbuf_is_temporary_volume (bufptr->vpid.volid) == true)
    {
      return true;
    }
  else
    {
      return false;
    }
}

/*
 * pgbuf_is_temporary_volume () - Find if the given permanent volume has been declared for temporary storage purposes
 *   return: true/false
 *   volid(in): Volume identifier of last allocated permanent volume
 */
STATIC_INLINE bool
pgbuf_is_temporary_volume (VOLID volid)
{
  /* TODO: I don't know why page buffer should care about temporary files and what this does, but it is really annoying.
   * until database is loaded and restarted, I will return false always. */
  if (!LOG_ISRESTARTED ())
    {
      return false;
    }
  return xdisk_get_purpose (NULL, volid) == DB_TEMPORARY_DATA_PURPOSE;
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
  long long unsigned alloc_size;

  /* allocate space for page buffer BCB table */
  alloc_size = (long long unsigned) pgbuf_Pool.num_buffers * PGBUF_BCB_SIZEOF;
  if (!MEM_SIZE_IS_VALID (alloc_size))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_PRM_BAD_VALUE, 1, "data_buffer_pages");
      return ER_PRM_BAD_VALUE;
    }
  pgbuf_Pool.BCB_table = (PGBUF_BCB *) malloc ((size_t) alloc_size);
  if (pgbuf_Pool.BCB_table == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) alloc_size);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  /* allocate space for io page buffers */
  alloc_size = (long long unsigned) pgbuf_Pool.num_buffers * PGBUF_IOPAGE_BUFFER_SIZE;
  if (!MEM_SIZE_IS_VALID (alloc_size))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_PRM_BAD_VALUE, 1, "data_buffer_pages");
      if (pgbuf_Pool.BCB_table != NULL)
	{
	  free_and_init (pgbuf_Pool.BCB_table);
	}
      return ER_PRM_BAD_VALUE;
    }
  pgbuf_Pool.iopage_table = (PGBUF_IOPAGE_BUFFER *) malloc ((size_t) alloc_size);
  if (pgbuf_Pool.iopage_table == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) alloc_size);
      if (pgbuf_Pool.BCB_table != NULL)
	{
	  free_and_init (pgbuf_Pool.BCB_table);
	}
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  /* initialize each entry of the buffer BCB table */
  for (i = 0; i < pgbuf_Pool.num_buffers; i++)
    {
      bufptr = PGBUF_FIND_BCB_PTR (i);
      pthread_mutex_init (&bufptr->mutex, NULL);
#if defined (SERVER_MODE)
      bufptr->owner_mutex = -1;
#endif /* SERVER_MODE */
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

      bufptr->flags = PGBUF_BCB_INIT_FLAGS;
      bufptr->count_fix_and_avoid_dealloc = 0;
      bufptr->hit_age = 0;
      LSA_SET_NULL (&bufptr->oldest_unflush_lsa);

      bufptr->tick_lru3 = 0;
      bufptr->tick_lru_list = 0;

      /* link BCB and iopage buffer */
      ioptr = PGBUF_FIND_IOPAGE_PTR (i);

      fileio_init_lsa_of_page (&ioptr->iopage, IO_PAGESIZE);

      /* Init Page identifier */
      ioptr->iopage.prv.pageid = -1;
      ioptr->iopage.prv.volid = -1;

      ioptr->iopage.prv.ptype = (unsigned char) PAGE_UNKNOWN;
      ioptr->iopage.prv.pflag = '\0';
      ioptr->iopage.prv.p_reserve_1 = 0;
      ioptr->iopage.prv.p_reserve_2 = 0;
      ioptr->iopage.prv.tde_nonce = 0;

      bufptr->iopage_buffer = ioptr;
      ioptr->bcb = bufptr;

#if defined(CUBRID_DEBUG)
      /* Reinitizalize the buffer */
      pgbuf_scramble (&bufptr->iopage_buffer->iopage);
      memcpy (PGBUF_FIND_BUFFER_GUARD (bufptr), pgbuf_Guard, sizeof (pgbuf_Guard));
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
  pgbuf_Pool.buf_hash_table = (PGBUF_BUFFER_HASH *) malloc (hashsize * PGBUF_BUFFER_HASH_SIZEOF);
  if (pgbuf_Pool.buf_hash_table == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (hashsize * PGBUF_BUFFER_HASH_SIZEOF));
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
  size_t i;
  size_t thrd_num_total;
  size_t alloc_size;

  /* allocate memory space for the buffer lock table */
  thrd_num_total = thread_num_total_threads ();
#if defined(SERVER_MODE)
  assert ((int) thrd_num_total > MAX_NTRANS * 2);
#else /* !SERVER_MODE */
  assert (thrd_num_total == 1);
#endif /* !SERVER_MODE */

  alloc_size = thrd_num_total * PGBUF_BUFFER_LOCK_SIZEOF;
  pgbuf_Pool.buf_lock_table = (PGBUF_BUFFER_LOCK *) malloc (alloc_size);
  if (pgbuf_Pool.buf_lock_table == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, alloc_size);
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
  pgbuf_Pool.num_LRU_list = prm_get_integer_value (PRM_ID_PB_NUM_LRU_CHAINS);
  if (pgbuf_Pool.num_LRU_list == 0)
    {
      /* Default value of shared lists : # of transactions */
      pgbuf_Pool.num_LRU_list = (int) MAX_NTRANS;
      assert (pgbuf_Pool.num_LRU_list > 0);

      if (pgbuf_Pool.num_buffers / pgbuf_Pool.num_LRU_list < PGBUF_MIN_PAGES_IN_SHARED_LIST)
	{
	  pgbuf_Pool.num_LRU_list = pgbuf_Pool.num_buffers / PGBUF_MIN_PAGES_IN_SHARED_LIST;
	}

      /* should have at least 4 shared LRUs */
      pgbuf_Pool.num_LRU_list = MAX (pgbuf_Pool.num_LRU_list, 4);
    }

  /* allocate memory space for the page buffer LRU lists */
  pgbuf_Pool.buf_LRU_list = (PGBUF_LRU_LIST *) malloc (PGBUF_TOTAL_LRU_COUNT * PGBUF_LRU_LIST_SIZEOF);
  if (pgbuf_Pool.buf_LRU_list == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      (PGBUF_TOTAL_LRU_COUNT * PGBUF_LRU_LIST_SIZEOF));
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  /* initialize the page buffer LRU lists */
  for (i = 0; i < PGBUF_TOTAL_LRU_COUNT; i++)
    {
      pgbuf_Pool.buf_LRU_list[i].index = i;

      pthread_mutex_init (&pgbuf_Pool.buf_LRU_list[i].mutex, NULL);
      pgbuf_Pool.buf_LRU_list[i].top = NULL;
      pgbuf_Pool.buf_LRU_list[i].bottom = NULL;
      pgbuf_Pool.buf_LRU_list[i].bottom_1 = NULL;
      pgbuf_Pool.buf_LRU_list[i].bottom_2 = NULL;
      pgbuf_Pool.buf_LRU_list[i].count_lru1 = 0;
      pgbuf_Pool.buf_LRU_list[i].count_lru2 = 0;
      pgbuf_Pool.buf_LRU_list[i].count_lru3 = 0;
      pgbuf_Pool.buf_LRU_list[i].count_vict_cand = 0;
      pgbuf_Pool.buf_LRU_list[i].victim_hint = NULL;
      pgbuf_Pool.buf_LRU_list[i].tick_list = 0;
      pgbuf_Pool.buf_LRU_list[i].tick_lru3 = 0;

      pgbuf_Pool.buf_LRU_list[i].threshold_lru1 = 0;
      pgbuf_Pool.buf_LRU_list[i].threshold_lru2 = 0;
      pgbuf_Pool.buf_LRU_list[i].quota = 0;

      pgbuf_Pool.buf_LRU_list[i].flags = 0;
    }

  return NO_ERROR;
}

/*
 * pgbuf_initialize_aout_list () - initialize the Aout list
 * return : error code or NO_ERROR
 */
static int
pgbuf_initialize_aout_list (void)
{
/* limit Aout size to equivalent of 512M */
#define PGBUF_LIMIT_AOUT_BUFFERS 32768
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

  list->max_count = MIN (list->max_count, PGBUF_LIMIT_AOUT_BUFFERS);
  alloc_size = list->max_count * sizeof (PGBUF_AOUT_BUF);

  list->bufarray = (PGBUF_AOUT_BUF *) malloc (alloc_size);
  if (list->bufarray == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, alloc_size);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  list->Aout_free = &list->bufarray[0];

  for (i = 0; i < list->max_count; i++)
    {
      VPID_SET_NULL (&list->bufarray[i].vpid);
      list->bufarray[i].lru_idx = PGBUF_AOUT_NOT_FOUND;
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
  list->aout_buf_ht = (MHT_TABLE **) malloc (alloc_size);
  if (list->aout_buf_ht == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, alloc_size);
      goto error_return;
    }

  memset (list->aout_buf_ht, 0, alloc_size);

  for (i = 0; i < list->num_hashes; i++)
    {
      list->aout_buf_ht[i] = mht_create ("PGBUF_AOUT_HASH", list->max_count, pgbuf_hash_vpid, pgbuf_compare_vpid);

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

  if (list->aout_buf_ht != NULL)
    {
      for (i = 0; list->aout_buf_ht[i] != NULL; i++)
	{
	  mht_destroy (list->aout_buf_ht[i]);
	}
      free_and_init (list->aout_buf_ht);
    }

  pthread_mutex_destroy (&list->Aout_mutex);

  return ER_FAILED;
#undef PGBUF_LIMIT_AOUT_BUFFERS
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
  size_t thrd_num_total;
  size_t alloc_size;
  size_t i, j, idx;

  thrd_num_total = thread_num_total_threads ();
#if defined(SERVER_MODE)
  assert ((int) thrd_num_total > MAX_NTRANS * 2);
#else /* !SERVER_MODE */
  assert (thrd_num_total == 1);
#endif /* !SERVER_MODE */

  pgbuf_Pool.thrd_holder_info = (PGBUF_HOLDER_ANCHOR *) malloc (thrd_num_total * PGBUF_HOLDER_ANCHOR_SIZEOF);
  if (pgbuf_Pool.thrd_holder_info == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      thrd_num_total * PGBUF_HOLDER_ANCHOR_SIZEOF);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  /* phase 1: allocate memory space that is used for BCB holder entries */
  alloc_size = thrd_num_total * PGBUF_DEFAULT_FIX_COUNT * PGBUF_HOLDER_SIZEOF;
  pgbuf_Pool.thrd_reserved_holder = (PGBUF_HOLDER *) malloc (alloc_size);
  if (pgbuf_Pool.thrd_reserved_holder == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, alloc_size);
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
      pgbuf_Pool.thrd_holder_info[i].thrd_free_list = &(pgbuf_Pool.thrd_reserved_holder[i * PGBUF_DEFAULT_FIX_COUNT]);

      for (j = 0; j < PGBUF_DEFAULT_FIX_COUNT; j++)
	{
	  idx = (i * PGBUF_DEFAULT_FIX_COUNT) + j;
	  pgbuf_Pool.thrd_reserved_holder[idx].fix_count = 0;
	  pgbuf_Pool.thrd_reserved_holder[idx].bufptr = NULL;
	  pgbuf_Pool.thrd_reserved_holder[idx].thrd_link = NULL;
	  INIT_HOLDER_STAT (&(pgbuf_Pool.thrd_reserved_holder[idx].perf_stat));
	  pgbuf_Pool.thrd_reserved_holder[idx].first_watcher = NULL;
	  pgbuf_Pool.thrd_reserved_holder[idx].last_watcher = NULL;
	  pgbuf_Pool.thrd_reserved_holder[idx].watch_count = 0;

	  if (j == (PGBUF_DEFAULT_FIX_COUNT - 1))
	    {
	      pgbuf_Pool.thrd_reserved_holder[idx].next_holder = NULL;
	    }
	  else
	    {
	      pgbuf_Pool.thrd_reserved_holder[idx].next_holder = &(pgbuf_Pool.thrd_reserved_holder[idx + 1]);
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
STATIC_INLINE PGBUF_HOLDER *
pgbuf_allocate_thrd_holder_entry (THREAD_ENTRY * thread_p)
{
  int thrd_index;
  PGBUF_HOLDER_ANCHOR *thrd_holder_info;
  PGBUF_HOLDER *holder;
  PGBUF_HOLDER_SET *holder_set;
#if defined(SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */

  thrd_index = thread_get_entry_index (thread_p);

  thrd_holder_info = &(pgbuf_Pool.thrd_holder_info[thrd_index]);

  if (thrd_holder_info->thrd_free_list != NULL)
    {
      /* allocate a BCB holder entry from the free BCB holder list of given thread */
      holder = thrd_holder_info->thrd_free_list;
      thrd_holder_info->thrd_free_list = holder->next_holder;
      thrd_holder_info->num_free_cnt -= 1;
    }
  else
    {
      /* holder == NULL : free BCB holder list is empty */

      /* allocate a BCB holder entry from the free BCB holder list shared by all threads. */
      rv = pthread_mutex_lock (&pgbuf_Pool.free_holder_set_mutex);
      if (pgbuf_Pool.free_index == -1)
	{
	  /* no usable free holder entry */
	  /* expand the free BCB holder list shared by threads */
	  holder_set = (PGBUF_HOLDER_SET *) malloc (PGBUF_HOLDER_SET_SIZEOF);
	  if (holder_set == NULL)
	    {
	      /* This situation must not be occurred. */
	      assert (false);
	      pthread_mutex_unlock (&pgbuf_Pool.free_holder_set_mutex);
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, PGBUF_HOLDER_SET_SIZEOF);
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

  holder->first_watcher = NULL;
  holder->last_watcher = NULL;
  holder->watch_count = 0;

  return holder;
}

/*
 * pgbuf_find_thrd_holder () - Find the holder entry of current thread on the BCB holder list of given BCB
 *   return: pointer to holder entry or NULL
 *   bufptr(in):
 */
STATIC_INLINE PGBUF_HOLDER *
pgbuf_find_thrd_holder (THREAD_ENTRY * thread_p, PGBUF_BCB * bufptr)
{
  int thrd_index;
  PGBUF_HOLDER *holder;

  assert (bufptr != NULL);

  thrd_index = thread_get_entry_index (thread_p);

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
 * pgbuf_unlatch_thrd_holder () - decrements fix_count by one to the holder entry of current thread on the BCB holder
 *                                list of given BCB
 *   return: pointer to holder entry or NULL
 *   bufptr(in):
 */
STATIC_INLINE int
pgbuf_unlatch_thrd_holder (THREAD_ENTRY * thread_p, PGBUF_BCB * bufptr, PGBUF_HOLDER_STAT * holder_perf_stat_p)
{
  int err = NO_ERROR;
  PGBUF_HOLDER *holder;
  PAGE_PTR pgptr;

  assert (bufptr != NULL);

  CAST_BFPTR_TO_PGPTR (pgptr, bufptr);

  holder = pgbuf_find_thrd_holder (thread_p, bufptr);
  if (holder == NULL)
    {
      /* This situation must not be occurred. */
      assert (false);
      err = ER_PB_UNFIXED_PAGEPTR;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err, 3, pgptr, bufptr->vpid.pageid,
	      fileio_get_volume_label (bufptr->vpid.volid, PEEK));

      goto exit_on_error;
    }

  if (holder_perf_stat_p != NULL)
    {
      *holder_perf_stat_p = holder->perf_stat;
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
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err, 3, pgptr, bufptr->vpid.pageid,
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
STATIC_INLINE int
pgbuf_remove_thrd_holder (THREAD_ENTRY * thread_p, PGBUF_HOLDER * holder)
{
  int err = NO_ERROR;
  int thrd_index;
  PGBUF_HOLDER_ANCHOR *thrd_holder_info;
  PGBUF_HOLDER *prev;
  int found;

  assert (holder != NULL);
  assert (holder->fix_count == 0);

  assert (holder->watch_count == 0);

  /* holder->fix_count is always set to some meaningful value when the holder entry is allocated for use. So, at this
   * time, we do not need to initialize it. connect the BCB holder entry into free BCB holder list of given thread. */

  thrd_index = thread_get_entry_index (thread_p);

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

static int
pgbuf_latch_idle_page (THREAD_ENTRY * thread_p, PGBUF_BCB * bufptr, PGBUF_LATCH_MODE request_mode)
{
  PGBUF_HOLDER *holder = NULL;
  bool buf_is_dirty;

  buf_is_dirty = pgbuf_bcb_is_dirty (bufptr);

  bufptr->latch_mode = request_mode;
  bufptr->fcnt = 1;

  PGBUF_BCB_UNLOCK (bufptr);

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
  holder->perf_stat.dirtied_by_holder = 0;
  if (request_mode == PGBUF_LATCH_WRITE)
    {
      holder->perf_stat.hold_has_write_latch = 1;
      holder->perf_stat.hold_has_read_latch = 0;
    }
  else
    {
      holder->perf_stat.hold_has_read_latch = 1;
      holder->perf_stat.hold_has_write_latch = 0;
    }
  holder->perf_stat.dirty_before_hold = buf_is_dirty;

  return NO_ERROR;
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
STATIC_INLINE int
pgbuf_latch_bcb_upon_fix (THREAD_ENTRY * thread_p, PGBUF_BCB * bufptr, PGBUF_LATCH_MODE request_mode,
			  int buf_lock_acquired, PGBUF_LATCH_CONDITION condition, bool * is_latch_wait)
{
  PGBUF_HOLDER *holder = NULL;
  int request_fcnt = 1;
  bool is_page_idle;
  bool buf_is_dirty;

  /* parameter validation */
  assert (request_mode == PGBUF_LATCH_READ || request_mode == PGBUF_LATCH_WRITE);
  assert (condition == PGBUF_UNCONDITIONAL_LATCH || condition == PGBUF_CONDITIONAL_LATCH);
  assert (is_latch_wait != NULL);

  *is_latch_wait = false;

  buf_is_dirty = pgbuf_bcb_is_dirty (bufptr);

  /* the caller is holding bufptr->mutex */
  is_page_idle = false;
  if (buf_lock_acquired || bufptr->latch_mode == PGBUF_NO_LATCH)
    {
      is_page_idle = true;
    }
#if defined (SA_MODE)
  else
    {
      holder = pgbuf_find_thrd_holder (thread_p, bufptr);
      if (holder == NULL)
	{
	  /* It means bufptr->latch_mode was leaked by the previous holder, since there should be no user except me in
	   * SA_MODE. */
	  assert (0);
	  is_page_idle = true;
	}
    }
#endif

  if (is_page_idle == true)
    {
      return pgbuf_latch_idle_page (thread_p, bufptr, request_mode);
    }

  if (request_mode == PGBUF_LATCH_READ && bufptr->latch_mode == PGBUF_LATCH_READ)
    {
      if (pgbuf_is_exist_blocked_reader_writer (bufptr) == false)
	{
	  /* there is not any blocked reader/writer. */
	  /* grant the request */

	  /* increment the fix count */
	  bufptr->fcnt++;
	  assert (0 < bufptr->fcnt);

	  PGBUF_BCB_UNLOCK (bufptr);

	  /* allocate a BCB holder entry */

	  holder = pgbuf_find_thrd_holder (thread_p, bufptr);
	  if (holder != NULL)
	    {
	      /* the caller is the holder of the buffer page */
	      holder->fix_count++;
	      /* holder->dirty_before_holder not changed */
	      if (request_mode == PGBUF_LATCH_WRITE)
		{
		  holder->perf_stat.hold_has_write_latch = 1;
		}
	      else
		{
		  holder->perf_stat.hold_has_read_latch = 1;
		}
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
	      if (request_mode == PGBUF_LATCH_WRITE)
		{
		  holder->perf_stat.hold_has_write_latch = 1;
		  holder->perf_stat.hold_has_read_latch = 0;
		}
	      else
		{
		  holder->perf_stat.hold_has_read_latch = 1;
		  holder->perf_stat.hold_has_write_latch = 0;
		}
	      holder->perf_stat.dirtied_by_holder = 0;
	      holder->perf_stat.dirty_before_hold = buf_is_dirty;
	    }
#endif /* SERVER_MODE */

	  return NO_ERROR;
	}

#if defined (SA_MODE)
      /* It is impossible to have a blocked waiter under SA_MODE. */
      assert (0);
#endif /* SA_MODE */

      /* at here, there is some blocked reader/writer. */

      holder = pgbuf_find_thrd_holder (thread_p, bufptr);
      if (holder == NULL)
	{
	  /* in case that the caller is not the holder */
	  goto do_block;
	}

      /* in case that the caller is the holder */
      bufptr->fcnt++;
      assert (0 < bufptr->fcnt);

      PGBUF_BCB_UNLOCK (bufptr);

      /* set BCB holder entry */

      holder->fix_count++;
      /* holder->dirty_before_holder not changed */
      if (request_mode == PGBUF_LATCH_WRITE)
	{
	  holder->perf_stat.hold_has_write_latch = 1;
	}
      else
	{
	  holder->perf_stat.hold_has_read_latch = 1;
	}

      return NO_ERROR;
    }

  holder = pgbuf_find_thrd_holder (thread_p, bufptr);
  if (holder == NULL)
    {
      /* in case that the caller is not the holder */
#if defined (SA_MODE)
      assert (0);
#endif
      goto do_block;
    }

  /* in case that the caller is holder */

  if (bufptr->latch_mode != PGBUF_LATCH_WRITE)
    {
      /* check iff nested write mode fix */
      assert_release (request_mode != PGBUF_LATCH_WRITE);

#if !defined(NDEBUG)
      if (request_mode == PGBUF_LATCH_WRITE)
	{
	  /* This situation must not be occurred. */
	  assert (false);

	  PGBUF_BCB_UNLOCK (bufptr);

	  return ER_FAILED;
	}
#endif
    }

  if (bufptr->latch_mode == PGBUF_LATCH_WRITE)
    {				/* only the holder */
      assert (bufptr->fcnt == holder->fix_count);

      bufptr->fcnt++;
      assert (0 < bufptr->fcnt);

      PGBUF_BCB_UNLOCK (bufptr);

      /* set BCB holder entry */

      holder->fix_count++;
      /* holder->dirty_before_holder not changed */
      if (request_mode == PGBUF_LATCH_WRITE)
	{
	  holder->perf_stat.hold_has_write_latch = 1;
	}
      else
	{
	  holder->perf_stat.hold_has_read_latch = 1;
	}

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
	  bufptr->fcnt++;
	  assert (0 < bufptr->fcnt);

	  PGBUF_BCB_UNLOCK (bufptr);

	  /* set BCB holder entry */

	  holder->fix_count++;
	  /* holder->dirty_before_holder not changed */
	  if (request_mode == PGBUF_LATCH_WRITE)
	    {
	      holder->perf_stat.hold_has_write_latch = 1;
	    }
	  else
	    {
	      holder->perf_stat.hold_has_read_latch = 1;
	    }

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

      INIT_HOLDER_STAT (&holder->perf_stat);

      if (pgbuf_remove_thrd_holder (thread_p, holder) != NO_ERROR)
	{
	  /* This situation must not be occurred. */
	  assert (false);

	  PGBUF_BCB_UNLOCK (bufptr);

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

#if defined (SA_MODE)
  assert (0);
#endif

  if (condition == PGBUF_CONDITIONAL_LATCH)
    {
      /* reject the request */
      int tran_index;
      int wait_msec;

      tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
      wait_msec = logtb_find_wait_msecs (tran_index);

      if (wait_msec == LK_ZERO_WAIT)
	{
	  const char *client_prog_name;	/* Client program name for tran */
	  const char *client_user_name;	/* Client user name for tran */
	  const char *client_host_name;	/* Client host for tran */
	  int client_pid;	/* Client process identifier for tran */

	  /* setup timeout error, if wait_msec == LK_ZERO_WAIT */

	  PGBUF_BCB_UNLOCK (bufptr);

	  (void) logtb_find_client_name_host_pid (tran_index, &client_prog_name, &client_user_name, &client_host_name,
						  &client_pid);

	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LK_PAGE_TIMEOUT, 8, tran_index, client_user_name,
		  client_host_name, client_pid, (request_mode == PGBUF_LATCH_READ ? "READ" : "WRITE"),
		  bufptr->vpid.volid, bufptr->vpid.pageid, NULL);
	}
      else
	{
	  PGBUF_BCB_UNLOCK (bufptr);
	}

      return ER_FAILED;
    }
  else
    {
      /* block the request */

      if (pgbuf_block_bcb (thread_p, bufptr, request_mode, request_fcnt, false) != NO_ERROR)
	{
	  return ER_FAILED;
	}
      /* Above function released bufptr->mutex unconditionally */

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
      if (request_mode == PGBUF_LATCH_WRITE)
	{
	  holder->perf_stat.hold_has_write_latch = 1;
	}
      else if (request_mode == PGBUF_LATCH_READ)
	{
	  holder->perf_stat.hold_has_read_latch = 1;
	}
      holder->perf_stat.dirtied_by_holder = 0;
      holder->perf_stat.dirty_before_hold = buf_is_dirty;
      *is_latch_wait = true;

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
STATIC_INLINE int
pgbuf_unlatch_bcb_upon_unfix (THREAD_ENTRY * thread_p, PGBUF_BCB * bufptr, int holder_status)
{
  PAGE_PTR pgptr;
  int th_lru_idx;
  PGBUF_ZONE zone;
  int error_code = NO_ERROR;

  assert (holder_status == NO_ERROR);

  /* the caller is holding bufptr->mutex */

  assert (!VPID_ISNULL (&bufptr->vpid));
  assert (pgbuf_check_bcb_page_vpid (bufptr, false) == true);

  CAST_BFPTR_TO_PGPTR (pgptr, bufptr);

  /* decrement the fix count */
  bufptr->fcnt--;
  if (bufptr->fcnt < 0)
    {
      /* This situation must not be occurred. */
      assert (false);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_PB_UNFIXED_PAGEPTR, 3, pgptr, bufptr->vpid.pageid,
	      fileio_get_volume_label (bufptr->vpid.volid, PEEK));
      bufptr->fcnt = 0;
    }

  if (holder_status != NO_ERROR)
    {
      /* This situation must not be occurred. */
      assert (false);
      PGBUF_BCB_UNLOCK (bufptr);
      return ER_FAILED;
    }

  if (bufptr->fcnt == 0)
    {
      /* When oldest_unflush_lsa of a page is set, its dirty mark should also be set */
      assert (LSA_ISNULL (&bufptr->oldest_unflush_lsa) || pgbuf_bcb_is_dirty (bufptr));

      /* there could be some synchronous flushers on the BCB queue */
      /* When the page buffer in LRU_1_Zone, do not move the page buffer into the top of LRU. This is an intention for
       * performance. */
      if (pgbuf_bcb_should_be_moved_to_bottom_lru (bufptr))
	{
	  pgbuf_move_bcb_to_bottom_lru (thread_p, bufptr);
	}
      else if (pgbuf_is_exist_blocked_reader_writer (bufptr) == false)
	{
	  ATOMIC_INC_32 (&pgbuf_Pool.monitor.pg_unfix_cnt, 1);

	  if (PGBUF_THREAD_HAS_PRIVATE_LRU (thread_p))
	    {
	      th_lru_idx = PGBUF_LRU_INDEX_FROM_PRIVATE (PGBUF_PRIVATE_LRU_FROM_THREAD (thread_p));
	    }
	  else
	    {
	      th_lru_idx = -1;
	    }

	  zone = pgbuf_bcb_get_zone (bufptr);
	  switch (zone)
	    {
	    case PGBUF_VOID_ZONE:
	      /* bcb was recently allocated. the case may vary from never being used (or almost never), to up to few
	       * percent (when hit ratio is very low). in any case, this is not needed to be very optimized here,
	       * so the code was moved outside unlatch... do not inline it */
	      pgbuf_unlatch_void_zone_bcb (thread_p, bufptr, th_lru_idx);
	      break;

	    case PGBUF_LRU_1_ZONE:
	      /* note: this is most often accessed code and must be highly optimized! */
	      if (PGBUF_THREAD_SHOULD_IGNORE_UNFIX (thread_p))
		{
		  /* do nothing */
		  /* ... except collecting statistics */
		  perfmon_inc_stat (thread_p, PSTAT_PB_UNFIX_LRU_ONE_KEEP_VAC);
		  break;
		}
	      if (pgbuf_should_move_private_to_shared (thread_p, bufptr, th_lru_idx))
		{
		  /* move to shared */
		  pgbuf_lru_move_from_private_to_shared (thread_p, bufptr);
		  perfmon_inc_stat (thread_p, PSTAT_PB_UNFIX_LRU_ONE_PRV_TO_SHR_MID);
		  break;
		}
	      /* do not move or boost */
	      if (PGBUF_IS_PRIVATE_LRU_INDEX (pgbuf_bcb_get_lru_index (bufptr)))
		{
		  perfmon_inc_stat (thread_p, PSTAT_PB_UNFIX_LRU_ONE_PRV_KEEP);
		}
	      else
		{
		  perfmon_inc_stat (thread_p, PSTAT_PB_UNFIX_LRU_ONE_SHR_KEEP);
		}
	      pgbuf_bcb_register_hit_for_lru (bufptr);
	      break;

	    case PGBUF_LRU_2_ZONE:
	      /* this is the buffer zone between hot and victimized. is less hot than zone one and we allow boosting
	       * (if bcb's are old enough). */
	      if (PGBUF_THREAD_SHOULD_IGNORE_UNFIX (thread_p))
		{
		  /* do nothing */
		  /* ... except collecting statistics */
		  perfmon_inc_stat (thread_p, PSTAT_PB_UNFIX_LRU_TWO_KEEP_VAC);
		  break;
		}
	      if (pgbuf_should_move_private_to_shared (thread_p, bufptr, th_lru_idx))
		{
		  /* move to shared */
		  pgbuf_lru_move_from_private_to_shared (thread_p, bufptr);
		  perfmon_inc_stat (thread_p, PSTAT_PB_UNFIX_LRU_TWO_PRV_TO_SHR_MID);
		  break;
		}
	      if (PGBUF_IS_BCB_OLD_ENOUGH (bufptr, pgbuf_lru_list_from_bcb (bufptr)))
		{
		  /* boost */
		  pgbuf_lru_boost_bcb (thread_p, bufptr);
		}
	      else
		{
		  /* bcb is too new to tell if it really deserves a boost */
		  if (PGBUF_IS_PRIVATE_LRU_INDEX (pgbuf_bcb_get_lru_index (bufptr)))
		    {
		      perfmon_inc_stat (thread_p, PSTAT_PB_UNFIX_LRU_TWO_PRV_KEEP);
		    }
		  else
		    {
		      perfmon_inc_stat (thread_p, PSTAT_PB_UNFIX_LRU_TWO_SHR_KEEP);
		    }
		}
	      pgbuf_bcb_register_hit_for_lru (bufptr);
	      break;

	    case PGBUF_LRU_3_ZONE:
	      if (PGBUF_THREAD_SHOULD_IGNORE_UNFIX (thread_p))
		{
		  if (!pgbuf_bcb_avoid_victim (bufptr) && pgbuf_assign_direct_victim (thread_p, bufptr))
		    {
		      /* assigned victim directly */
		      if (perfmon_is_perf_tracking_and_active (PERFMON_ACTIVATION_FLAG_PB_VICTIMIZATION))
			{
			  perfmon_inc_stat (thread_p, PSTAT_PB_VICTIM_ASSIGN_DIRECT_VACUUM_LRU);
			}
		    }
		  else
		    {
		      perfmon_inc_stat (thread_p, PSTAT_PB_UNFIX_LRU_THREE_KEEP_VAC);
		    }
		  break;
		}
	      if (pgbuf_should_move_private_to_shared (thread_p, bufptr, th_lru_idx))
		{
		  /* move to shared */
		  pgbuf_lru_move_from_private_to_shared (thread_p, bufptr);
		  perfmon_inc_stat (thread_p, PSTAT_PB_UNFIX_LRU_THREE_PRV_TO_SHR_MID);
		  break;
		}
	      /* boost */
	      pgbuf_lru_boost_bcb (thread_p, bufptr);
	      pgbuf_bcb_register_hit_for_lru (bufptr);
	      break;

	    default:
	      /* unexpected */
	      assert (false);
	      break;
	    }
	}

      bufptr->latch_mode = PGBUF_NO_LATCH;
#if defined(SERVER_MODE)
      pgbuf_wakeup_reader_writer (thread_p, bufptr);
#endif /* SERVER_MODE */
    }

  assert (bufptr->latch_mode != PGBUF_LATCH_FLUSH);

  if (pgbuf_bcb_is_async_flush_request (bufptr))
    {
      /* PGBUF_LATCH_READ is possible, when a reader and a flusher was blocked by a writer.
       * Blocked readers are already wakened by the ex-owner.
       */
      assert (bufptr->fcnt == 0 || bufptr->latch_mode == PGBUF_LATCH_WRITE || bufptr->latch_mode == PGBUF_LATCH_READ);

      /* we need to flush bcb. we won't need the bcb mutex afterwards */
      error_code = pgbuf_bcb_safe_flush_force_unlock (thread_p, bufptr, false);
      /* what to do with the error? we failed to flush it... */
      if (error_code != NO_ERROR)
	{
	  er_clear ();
	  error_code = NO_ERROR;
	}
    }
  else
    {
      PGBUF_BCB_UNLOCK (bufptr);
    }

  return NO_ERROR;
}

/*
 * pgbuf_unlatch_void_zone_bcb () - unlatch bcb that is currently in void zone.
 *
 * return                        : void
 * thread_p (in)                 : thread entry
 * bcb (in)                      : void zone bcb to unlatch
 * thread_private_lru_index (in) : thread's private lru index. -1 if thread does not have any private list.
 *
 * note: this is part of unlatch/unfix algorithm.
 */
static void
pgbuf_unlatch_void_zone_bcb (THREAD_ENTRY * thread_p, PGBUF_BCB * bcb, int thread_private_lru_index)
{
  bool aout_enabled = false;
  int aout_list_id = PGBUF_AOUT_NOT_FOUND;

  assert (pgbuf_bcb_get_zone (bcb) == PGBUF_VOID_ZONE);

  if (pgbuf_Pool.buf_AOUT_list.max_count > 0)
    {
      aout_enabled = true;
      aout_list_id = pgbuf_remove_vpid_from_aout_list (thread_p, &bcb->vpid);
    }

  if (PGBUF_THREAD_SHOULD_IGNORE_UNFIX (thread_p))
    {
      /* we are not registering unfix for activity and we are not boosting or moving bcb's */
      if (aout_list_id == PGBUF_AOUT_NOT_FOUND)
	{
	  perfmon_inc_stat (thread_p, PSTAT_PB_UNFIX_VOID_AOUT_NOT_FOUND_VAC);
	}
      else
	{
	  perfmon_inc_stat (thread_p, PSTAT_PB_UNFIX_VOID_AOUT_FOUND_VAC);
	}

      /* can we feed direct victims? */
      if (!pgbuf_bcb_avoid_victim (bcb) && pgbuf_assign_direct_victim (thread_p, bcb))
	{
	  /* assigned victim directly */
	  if (perfmon_is_perf_tracking_and_active (PERFMON_ACTIVATION_FLAG_PB_VICTIMIZATION))
	    {
	      perfmon_inc_stat (thread_p, PSTAT_PB_VICTIM_ASSIGN_DIRECT_VACUUM_VOID);
	    }

	  /* add to AOUT */
	  if (pgbuf_Pool.buf_AOUT_list.max_count > 0)
	    {
	      pgbuf_add_vpid_to_aout_list (thread_p, &bcb->vpid, aout_list_id);
	    }
	  return;
	}

      /* reset aout_list_id */
      aout_list_id = PGBUF_AOUT_NOT_FOUND;
    }
  else
    {
      if (aout_list_id == PGBUF_AOUT_NOT_FOUND)
	{
	  perfmon_inc_stat (thread_p, PSTAT_PB_UNFIX_VOID_AOUT_NOT_FOUND);
	}
      else
	{
	  perfmon_inc_stat (thread_p, PSTAT_PB_UNFIX_VOID_AOUT_FOUND);
	}
    }

  if (thread_private_lru_index != -1)
    {
      if (PGBUF_THREAD_SHOULD_IGNORE_UNFIX (thread_p))
	{
	  /* add to top of current private list */
	  pgbuf_lru_add_new_bcb_to_top (thread_p, bcb, thread_private_lru_index);
	  perfmon_inc_stat (thread_p, PSTAT_PB_UNFIX_VOID_TO_PRIVATE_TOP_VAC);
	  return;
	}

      if (!aout_enabled || thread_private_lru_index == aout_list_id)
	{
	  /* add to top of current private list */
	  pgbuf_lru_add_new_bcb_to_top (thread_p, bcb, thread_private_lru_index);
	  perfmon_inc_stat (thread_p, PSTAT_PB_UNFIX_VOID_TO_PRIVATE_TOP);
	  pgbuf_bcb_register_hit_for_lru (bcb);
	  return;
	}

      if (aout_list_id == PGBUF_AOUT_NOT_FOUND)
	{
	  /* add to middle of current private list */
	  pgbuf_lru_add_new_bcb_to_middle (thread_p, bcb, thread_private_lru_index);
	  perfmon_inc_stat (thread_p, PSTAT_PB_UNFIX_VOID_TO_PRIVATE_MID);
	  pgbuf_bcb_register_hit_for_lru (bcb);
	  return;
	}

      /* fall through to add to shared */
    }
  /* add to middle of shared list. */
  pgbuf_lru_add_new_bcb_to_middle (thread_p, bcb, pgbuf_get_shared_lru_index_for_add ());
  perfmon_inc_stat (thread_p, PSTAT_PB_UNFIX_VOID_TO_SHARED_MID);
  if (!PGBUF_THREAD_SHOULD_IGNORE_UNFIX (thread_p))
    {
      pgbuf_bcb_register_hit_for_lru (bcb);
    }
}

/*
 * pgbuf_should_move_private_to_shared () - return true if bcb belongs to private lru list and if should be moved to a
 *                                          shared lru list.
 *
 * return                        : true if move from private to shared is needed.
 * thread_p (in)                 : thread entry
 * bcb (in)                      : bcb
 * thread_private_lru_index (in) : thread's private lru index. -1 if thread does not have any private list.
 */
STATIC_INLINE bool
pgbuf_should_move_private_to_shared (THREAD_ENTRY * thread_p, PGBUF_BCB * bcb, int thread_private_lru_index)
{
  int bcb_lru_idx = pgbuf_bcb_get_lru_index (bcb);

  if (PGBUF_IS_SHARED_LRU_INDEX (bcb_lru_idx))
    {
      /* not a private list */
      return false;
    }

  /* two conditions to move from private to shared:
   * 1. bcb is fixed by more than one transaction.
   * 2. bcb is very hot and old enough. */

  /* cond 1 */
  if (thread_private_lru_index != bcb_lru_idx)
    {
      return true;
    }
  /* cond 2 */
  if (!pgbuf_bcb_is_hot (bcb))
    {
      /* not hot enough */
      return false;
    }
  if (!PGBUF_IS_BCB_OLD_ENOUGH (bcb, PGBUF_GET_LRU_LIST (bcb_lru_idx)))
    {
      /* not old enough */
      return false;
    }
  /* hot and old enough */
  return true;
}

/*
 * pgbuf_block_bcb () - Adds it on the BCB waiting queue and block thread
 *   return: NO_ERROR, or ER_code
 *   bufptr(in):
 *   request_mode(in):
 *   request_fcnt(in):
 *   as_promote(in): if true, will wait as first promoter
 *
 * Note: Promoter will be the first waiter. Others will be appended to waiting queue.
 */
static int
pgbuf_block_bcb (THREAD_ENTRY * thread_p, PGBUF_BCB * bufptr, PGBUF_LATCH_MODE request_mode, int request_fcnt,
		 bool as_promote)
{
#if defined(SERVER_MODE)
  THREAD_ENTRY *cur_thrd_entry, *thrd_entry;

  /* caller is holding bufptr->mutex */
  /* request_mode == PGBUF_LATCH_READ/PGBUF_LATCH_WRITE/PGBUF_LATCH_FLUSH */
  assert (request_mode == PGBUF_LATCH_READ || request_mode == PGBUF_LATCH_WRITE || request_mode == PGBUF_LATCH_FLUSH);

  if (thread_p == NULL)
    {
      assert (thread_p != NULL);
      thread_p = thread_get_thread_entry_info ();
    }

  cur_thrd_entry = thread_p;
  cur_thrd_entry->request_latch_mode = request_mode;
  cur_thrd_entry->request_fix_count = request_fcnt;	/* SPECIAL_NOTE */

  if (as_promote)
    {
      /* place cur_thrd_entry as first in BCB waiting queue */

      /* Safe guard: there can be only one promoter. */
      assert (bufptr->next_wait_thrd == NULL || !bufptr->next_wait_thrd->wait_for_latch_promote);

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

  if (request_mode == PGBUF_LATCH_FLUSH)
    {
      /* is it safe to use infinite wait instead of timed sleep? */
      thread_lock_entry (cur_thrd_entry);
      PGBUF_BCB_UNLOCK (bufptr);
      thread_suspend_wakeup_and_unlock_entry (thread_p, THREAD_PGBUF_SUSPENDED);

      if (cur_thrd_entry->resume_status != THREAD_PGBUF_RESUMED)
	{
	  /* interrupt operation */
	  THREAD_ENTRY *thrd_entry, *prev_thrd_entry = NULL;

	  PGBUF_BCB_LOCK (bufptr);
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
		      prev_thrd_entry->next_wait_thrd = thrd_entry->next_wait_thrd;
		    }

		  thrd_entry->next_wait_thrd = NULL;
		  PGBUF_BCB_UNLOCK (bufptr);
		  return ER_FAILED;
		}

	      prev_thrd_entry = thrd_entry;
	      thrd_entry = thrd_entry->next_wait_thrd;
	    }
	  PGBUF_BCB_UNLOCK (bufptr);
	}
    }
  else
    {
      /*
       * We do not guarantee that there is no deadlock between page latches.
       * So, we made a decision that when read/write buffer fix request is
       * not granted immediately, block the request with timed sleep method.
       * That is, unless the request is not waken up by other threads within
       * some time interval, the request will be waken up by timeout.
       * When the request is waken up, the request is treated as a victim.
       */
      if (pgbuf_timed_sleep (thread_p, bufptr, cur_thrd_entry) != NO_ERROR)
	{
	  return ER_FAILED;
	}

#if !defined (NDEBUG)
      /* To hold mutex is not required because I hold the latch. This means at least my fix count is kept. */
      assert (0 < bufptr->fcnt);
#endif
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
static int
pgbuf_timed_sleep_error_handling (THREAD_ENTRY * thread_p, PGBUF_BCB * bufptr, THREAD_ENTRY * thrd_entry)
{
  THREAD_ENTRY *prev_thrd_entry;
  THREAD_ENTRY *curr_thrd_entry;

  PGBUF_BCB_LOCK (bufptr);

  /* case 1 : empty waiting queue */
  if (bufptr->next_wait_thrd == NULL)
    {
      /* The thread entry has been already removed from the BCB waiting queue by another thread. */
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
      if (bufptr->latch_mode == PGBUF_LATCH_READ && curr_thrd_entry->request_latch_mode == PGBUF_LATCH_READ)
	{
	  /* grant the request */
	  thread_lock_entry (curr_thrd_entry);
	  if (curr_thrd_entry->request_latch_mode == PGBUF_LATCH_READ)
	    {
	      bufptr->fcnt += curr_thrd_entry->request_fix_count;

	      /* do not handle BCB holder entry, at here. refer pgbuf_latch_bcb_upon_fix () */

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
static int
pgbuf_timed_sleep (THREAD_ENTRY * thread_p, PGBUF_BCB * bufptr, THREAD_ENTRY * thrd_entry)
{
  int r;
  struct timespec to;
  int wait_secs;
  int old_wait_msecs;
  int save_request_latch_mode;
  const char *client_prog_name;	/* Client program name for trans */
  const char *client_user_name;	/* Client user name for tran */
  const char *client_host_name;	/* Client host for tran */
  int client_pid;		/* Client process identifier for tran */

  TSC_TICKS start_tick, end_tick;
  TSCTIMEVAL tv_diff;

  /* After holding the mutex associated with conditional variable, release the bufptr->mutex. */
  thread_lock_entry (thrd_entry);
  PGBUF_BCB_UNLOCK (bufptr);

  old_wait_msecs = wait_secs = pgbuf_find_current_wait_msecs (thread_p);

  assert (wait_secs == LK_INFINITE_WAIT || wait_secs == LK_ZERO_WAIT || wait_secs == LK_FORCE_ZERO_WAIT
	  || wait_secs > 0);

  if (wait_secs == LK_ZERO_WAIT || wait_secs == LK_FORCE_ZERO_WAIT)
    {
      wait_secs = 0;
    }
  else
    {
      wait_secs = PGBUF_TIMEOUT;
    }

try_again:
  to.tv_sec = (int) time (NULL) + wait_secs;
  to.tv_nsec = 0;

  if (thrd_entry->event_stats.trace_slow_query == true)
    {
      tsc_getticks (&start_tick);
    }

  thrd_entry->resume_status = THREAD_PGBUF_SUSPENDED;
  r = pthread_cond_timedwait (&thrd_entry->wakeup_cond, &thrd_entry->th_entry_lock, &to);

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

      if (pgbuf_timed_sleep_error_handling (thread_p, bufptr, thrd_entry) == NO_ERROR)
	{
	  PGBUF_BCB_UNLOCK (bufptr);
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
      /* request_latch_mode == PGBUF_NO_LATCH means that the thread has waken up by timeout. This value must be set
       * before release the mutex. */
      save_request_latch_mode = thrd_entry->request_latch_mode;
      thrd_entry->request_latch_mode = PGBUF_NO_LATCH;
      thread_unlock_entry (thrd_entry);

      if (pgbuf_timed_sleep_error_handling (thread_p, bufptr, thrd_entry) == NO_ERROR)
	{
	  goto er_set_return;
	}

      return ER_FAILED;
    }
  else
    {
      thread_unlock_entry (thrd_entry);
      /* error setting */
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_COND_TIMEDWAIT, 0);
      return ER_FAILED;
    }

er_set_return:
  /* error setting */
  if (old_wait_msecs == LK_INFINITE_WAIT)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_PAGE_LATCH_TIMEDOUT, 2, bufptr->vpid.volid, bufptr->vpid.pageid);

      /* FIXME: remove it. temporarily added for debugging */
      assert (0);

      PGBUF_BCB_UNLOCK (bufptr);
      if (logtb_is_current_active (thread_p) == true)
	{
	  const char *client_prog_name;	/* Client user name for transaction */
	  const char *client_user_name;	/* Client user name for transaction */
	  const char *client_host_name;	/* Client host for transaction */
	  int client_pid;	/* Client process identifier for transaction */
	  int tran_index;

	  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
	  (void) logtb_find_client_name_host_pid (tran_index, &client_prog_name, &client_user_name, &client_host_name,
						  &client_pid);

	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LK_UNILATERALLY_ABORTED, 4, tran_index, client_user_name,
		  client_host_name, client_pid);
	}
      else
	{
	  /*
	   * We are already aborting, fall through. Don't do
	   * double aborts that could cause an infinite loop.
	   */
	  er_log_debug (ARG_FILE_LINE,
			"pgbuf_timed_sleep: Likely a system error. Trying to abort a transaction twice.\n");
	  /* We can release all the page latches held by current thread. */
	}
    }
  else if (old_wait_msecs > 0)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_PAGE_LATCH_TIMEDOUT, 2, bufptr->vpid.volid, bufptr->vpid.pageid);

      PGBUF_BCB_UNLOCK (bufptr);

      (void) logtb_find_client_name_host_pid (thrd_entry->tran_index, &client_prog_name, &client_user_name,
					      &client_host_name, &client_pid);

      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LK_PAGE_TIMEOUT, 8, thrd_entry->tran_index, client_user_name,
	      client_host_name, client_pid, (save_request_latch_mode == PGBUF_LATCH_READ ? "READ" : "WRITE"),
	      bufptr->vpid.volid, bufptr->vpid.pageid, NULL);
    }
  else
    {
      PGBUF_BCB_UNLOCK (bufptr);
    }

  return ER_FAILED;
}

/*
 * pgbuf_wakeup_reader_writer () - Wakes up blocked threads on the BCB queue with read or write latch mode
 *
 * return        : error code
 * thread_p (in) : thread entry
 * bufptr (in)   : bcb
 */
STATIC_INLINE void
pgbuf_wakeup_reader_writer (THREAD_ENTRY * thread_p, PGBUF_BCB * bufptr)
{
  THREAD_ENTRY *thrd_entry = NULL;
  THREAD_ENTRY *prev_thrd_entry = NULL;
  THREAD_ENTRY *next_thrd_entry = NULL;

  /* the caller is holding bufptr->mutex */

  assert (bufptr->latch_mode == PGBUF_NO_LATCH && bufptr->fcnt == 0);

  /* fcnt == 0, bufptr->latch_mode == PGBUF_NO_LATCH */

  /* how it works:
   *
   * we can have here multiple types of waiters:
   * 1. PGBUF_NO_LATCH - thread gave up waiting for bcb (interrupted or timed out). just remove it from list.
   * 2. PGBUF_LATCH_FLUSH - thread is waiting for bcb to be flushed. this is not actually a latch and thread is not
   *    awaken here. bcb must be either marked to be flushed asynchronously or is currently in process of being flushed.
   * 3. PGBUF_LATCH_READ - multiple threads can be waked at once (all readers at the head of the list).
   * 4. PGBUF_LATCH_WRITE - only first waiter is waked.
   */

  for (thrd_entry = bufptr->next_wait_thrd; thrd_entry != NULL; thrd_entry = next_thrd_entry)
    {
      next_thrd_entry = thrd_entry->next_wait_thrd;

      /* if thrd_entry->request_latch_mode is PGBUF_NO_LATCH, it means the corresponding thread has been waken up
       * by timeout. */
      if (thrd_entry->request_latch_mode == PGBUF_NO_LATCH)
	{
	  if (prev_thrd_entry == NULL)
	    {
	      bufptr->next_wait_thrd = next_thrd_entry;
	    }
	  else
	    {
	      prev_thrd_entry->next_wait_thrd = next_thrd_entry;
	    }
	  thrd_entry->next_wait_thrd = NULL;
	  continue;
	}

      if (thrd_entry->request_latch_mode == PGBUF_LATCH_FLUSH)
	{
	  /* must wait for flush. we do not wake it until flush is executed. */
	  assert (pgbuf_bcb_is_async_flush_request (bufptr) || pgbuf_bcb_is_flushing (bufptr));

	  /* leave it in the wait list */
	  prev_thrd_entry = thrd_entry;
	  continue;
	}

      if ((bufptr->latch_mode == PGBUF_NO_LATCH)
	  || (bufptr->latch_mode == PGBUF_LATCH_READ && thrd_entry->request_latch_mode == PGBUF_LATCH_READ))
	{
	  thread_lock_entry (thrd_entry);

	  if (thrd_entry->request_latch_mode != PGBUF_NO_LATCH)
	    {
	      /* grant the request */
	      bufptr->latch_mode = (PGBUF_LATCH_MODE) thrd_entry->request_latch_mode;
	      bufptr->fcnt += thrd_entry->request_fix_count;

	      /* do not handle BCB holder entry, at here. refer pgbuf_latch_bcb_upon_fix () */

	      /* remove thrd_entry from BCB waiting queue. */
	      if (prev_thrd_entry == NULL)
		{
		  bufptr->next_wait_thrd = next_thrd_entry;
		}
	      else
		{
		  prev_thrd_entry->next_wait_thrd = next_thrd_entry;
		}
	      thrd_entry->next_wait_thrd = NULL;

	      /* wake up the thread */
	      pgbuf_wakeup (thrd_entry);
	    }
	  else
	    {
	      if (prev_thrd_entry == NULL)
		{
		  bufptr->next_wait_thrd = next_thrd_entry;
		}
	      else
		{
		  prev_thrd_entry->next_wait_thrd = next_thrd_entry;
		}
	      thrd_entry->next_wait_thrd = NULL;
	      thread_unlock_entry (thrd_entry);
	    }
	}
      else if (bufptr->latch_mode == PGBUF_LATCH_READ)
	{
	  /* Look for other readers. */
	  prev_thrd_entry = thrd_entry;
	  continue;
	}
      else
	{
	  assert (bufptr->latch_mode == PGBUF_LATCH_WRITE);
	  break;
	}
    }
}
#endif /* SERVER_MODE */

/*
 * pgbuf_search_hash_chain () - searches the buffer hash chain to find a BCB with page identifier
 *   return: if success, BCB pointer, otherwise NULL
 *   hash_anchor(in):
 *   vpid(in):
 */
STATIC_INLINE PGBUF_BCB *
pgbuf_search_hash_chain (THREAD_ENTRY * thread_p, PGBUF_BUFFER_HASH * hash_anchor, const VPID * vpid)
{
  PGBUF_BCB *bufptr;
  int mbw_cnt;
#if defined(SERVER_MODE)
  int rv;
  int loop_cnt;
#endif
  TSC_TICKS start_tick, end_tick;
  UINT64 lock_wait_time = 0;

  mbw_cnt = 0;

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

	  rv = PGBUF_BCB_TRYLOCK (bufptr);
	  if (rv == 0)
	    {
	      /* OK. go ahead */
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

	      /* An unconditional request is given for acquiring mutex */
	      PGBUF_BCB_LOCK (bufptr);
	    }
#else /* SERVER_MODE */
	  PGBUF_BCB_LOCK (bufptr);
#endif /* SERVER_MODE */

	  if (!VPID_EQ (&(bufptr->vpid), vpid))
	    {
	      /* updated or replaced */
	      PGBUF_BCB_UNLOCK (bufptr);
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

  if (perfmon_is_perf_tracking_and_active (PERFMON_ACTIVATION_FLAG_PB_HASH_ANCHOR))
    {
      tsc_getticks (&start_tick);
    }

  rv = pthread_mutex_lock (&hash_anchor->hash_mutex);

  if (perfmon_is_perf_tracking_and_active (PERFMON_ACTIVATION_FLAG_PB_HASH_ANCHOR))
    {
      tsc_getticks (&end_tick);
      lock_wait_time = tsc_elapsed_utime (end_tick, start_tick);
      perfmon_inc_stat (thread_p, PSTAT_PB_NUM_HASH_ANCHOR_WAITS);
      perfmon_add_stat (thread_p, PSTAT_PB_TIME_HASH_ANCHOR_WAIT, lock_wait_time);
    }

  bufptr = hash_anchor->hash_next;
  while (bufptr != NULL)
    {
      if (VPID_EQ (&(bufptr->vpid), vpid))
	{
#if defined(SERVER_MODE)
	  loop_cnt = 0;

	mutex_lock2:

	  rv = PGBUF_BCB_TRYLOCK (bufptr);
	  if (rv == 0)
	    {
	      /* bufptr->mutex is held */
	      pthread_mutex_unlock (&hash_anchor->hash_mutex);
	    }
	  else
	    {
	      if (rv != EBUSY)
		{
		  er_set_with_oserror (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_MUTEX_TRYLOCK, 0);
		  return NULL;
		}

	      if (loop_cnt++ < mbw_cnt)
		{
		  goto mutex_lock2;
		}

	      /* ret == EBUSY : bufptr->mutex is not held */
	      /* An unconditional request is given for acquiring mutex after releasing hash_mutex. */
	      pthread_mutex_unlock (&hash_anchor->hash_mutex);
	      PGBUF_BCB_LOCK (bufptr);
	    }
#else /* SERVER_MODE */
	  pthread_mutex_unlock (&hash_anchor->hash_mutex);
	  PGBUF_BCB_LOCK (bufptr);
#endif /* SERVER_MODE */

	  if (!VPID_EQ (&(bufptr->vpid), vpid))
	    {
	      /* updated or replaced */
	      PGBUF_BCB_UNLOCK (bufptr);
	      goto try_again;
	    }
	  break;
	}
      bufptr = bufptr->hash_next;
    }
  /* at this point, if (bufptr != NULL) caller holds bufptr->mutex but not hash_anchor->hash_mutex if (bufptr ==
   * NULL) caller holds hash_anchor->hash_mutex. */
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
 *       The mutex of the hash anchor will be released in the next call of pgbuf_unlock_page ().
 */
STATIC_INLINE int
pgbuf_insert_into_hash_chain (THREAD_ENTRY * thread_p, PGBUF_BUFFER_HASH * hash_anchor, PGBUF_BCB * bufptr)
{
#if defined(SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */
  TSC_TICKS start_tick, end_tick;
  UINT64 lock_wait_time = 0;

  if (perfmon_get_activation_flag () & PERFMON_ACTIVATION_FLAG_PB_HASH_ANCHOR)
    {
      if (perfmon_is_perf_tracking ())
	{
	  tsc_getticks (&start_tick);
	}
    }

  /* Note that the caller is not holding bufptr->mutex */
  rv = pthread_mutex_lock (&hash_anchor->hash_mutex);

  if (perfmon_is_perf_tracking_and_active (PERFMON_ACTIVATION_FLAG_PB_HASH_ANCHOR))
    {
      tsc_getticks (&end_tick);
      lock_wait_time = tsc_elapsed_utime (end_tick, start_tick);
      perfmon_inc_stat (thread_p, PSTAT_PB_NUM_HASH_ANCHOR_WAITS);
      perfmon_add_stat (thread_p, PSTAT_PB_TIME_HASH_ANCHOR_WAIT, lock_wait_time);
    }

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
STATIC_INLINE int
pgbuf_delete_from_hash_chain (THREAD_ENTRY * thread_p, PGBUF_BCB * bufptr)
{
  PGBUF_BUFFER_HASH *hash_anchor;
  PGBUF_BCB *prev_bufptr;
  PGBUF_BCB *curr_bufptr;
#if defined(SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */
  TSC_TICKS start_tick, end_tick;
  UINT64 lock_wait_time = 0;

  if (perfmon_get_activation_flag () & PERFMON_ACTIVATION_FLAG_PB_HASH_ANCHOR)
    {
      if (perfmon_is_perf_tracking ())
	{
	  tsc_getticks (&start_tick);
	}
    }

  /* the caller is holding bufptr->mutex */

  /* fcnt==0, next_wait_thrd==NULL, latch_mode==PGBUF_NO_LATCH */
  /* if (bufptr->latch_mode==PGBUF_NO_LATCH) invoked by an invalidator */
  hash_anchor = &(pgbuf_Pool.buf_hash_table[PGBUF_HASH_VALUE (&(bufptr->vpid))]);
  rv = pthread_mutex_lock (&hash_anchor->hash_mutex);

  if (perfmon_is_perf_tracking_and_active (PERFMON_ACTIVATION_FLAG_PB_HASH_ANCHOR))
    {
      tsc_getticks (&end_tick);
      lock_wait_time = tsc_elapsed_utime (end_tick, start_tick);
      perfmon_inc_stat (thread_p, PSTAT_PB_NUM_HASH_ANCHOR_WAITS);
      perfmon_add_stat (thread_p, PSTAT_PB_TIME_HASH_ANCHOR_WAIT, lock_wait_time);
    }

  if (pgbuf_bcb_is_flushing (bufptr))
    {
      assert (false);

      /* Someone tries to fix the current buffer page. So, give up selecting current buffer page as a victim. */
      pthread_mutex_unlock (&hash_anchor->hash_mutex);
      bufptr->latch_mode = PGBUF_NO_LATCH;
      PGBUF_BCB_UNLOCK (bufptr);
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

	  /* Now, the caller is holding bufptr->mutex. */
	  /* bufptr->mutex will be released in following function. */
	  pgbuf_put_bcb_into_invalid_list (thread_p, bufptr);

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
      pgbuf_bcb_check_and_reset_fix_and_avoid_dealloc (bufptr, ARG_FILE_LINE);

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
pgbuf_lock_page (THREAD_ENTRY * thread_p, PGBUF_BUFFER_HASH * hash_anchor, const VPID * vpid)
{
#if defined(SERVER_MODE)
  PGBUF_BUFFER_LOCK *cur_buffer_lock;
  THREAD_ENTRY *cur_thrd_entry;
  TSC_TICKS start_tick, end_tick;
  UINT64 lock_wait_time = 0;

  /* the caller is holding hash_anchor->hash_mutex */
  /* check whether the page is in the Buffer Lock Chain */

  if (thread_p == NULL)
    {
      assert (thread_p != NULL);
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

	      if (perfmon_is_perf_tracking_and_active (PERFMON_ACTIVATION_FLAG_PB_HASH_ANCHOR))
		{
		  tsc_getticks (&start_tick);
		}

	      r = pthread_mutex_lock (&hash_anchor->hash_mutex);

	      if (perfmon_is_perf_tracking_and_active (PERFMON_ACTIVATION_FLAG_PB_HASH_ANCHOR))
		{
		  tsc_getticks (&end_tick);
		  lock_wait_time = tsc_elapsed_utime (end_tick, start_tick);
		  perfmon_inc_stat (thread_p, PSTAT_PB_NUM_HASH_ANCHOR_WAITS);
		  perfmon_add_stat (thread_p, PSTAT_PB_TIME_HASH_ANCHOR_WAIT, lock_wait_time);
		}

	      thrd_entry = cur_buffer_lock->next_wait_thrd;

	      while (thrd_entry != NULL)
		{
		  if (thrd_entry == cur_thrd_entry)
		    {
		      if (prev_thrd_entry == NULL)
			{
			  cur_buffer_lock->next_wait_thrd = thrd_entry->next_wait_thrd;
			}
		      else
			{
			  prev_thrd_entry->next_wait_thrd = thrd_entry->next_wait_thrd;
			}

		      thrd_entry->next_wait_thrd = NULL;
		      pthread_mutex_unlock (&hash_anchor->hash_mutex);

		      perfmon_inc_stat (thread_p, PSTAT_LK_NUM_WAITED_ON_PAGES);	/* monitoring */
		      return PGBUF_LOCK_WAITER;
		    }
		  prev_thrd_entry = thrd_entry;
		  thrd_entry = thrd_entry->next_wait_thrd;
		}
	      pthread_mutex_unlock (&hash_anchor->hash_mutex);
	    }
	  perfmon_inc_stat (thread_p, PSTAT_LK_NUM_WAITED_ON_PAGES);	/* monitoring */
	  return PGBUF_LOCK_WAITER;
	}
      cur_buffer_lock = cur_buffer_lock->lock_next;
    }

  /* buf_lock_table is implemented to have one entry for each thread. At first design, it had one entry for each
   * thread. cur_thrd_entry->index : thread entry index cur_thrd_entry->tran_index : transaction entry index */

  /* vpid is not found in the Buffer Lock Chain */
  cur_buffer_lock = &(pgbuf_Pool.buf_lock_table[cur_thrd_entry->index]);
  cur_buffer_lock->vpid = *vpid;
  cur_buffer_lock->next_wait_thrd = NULL;
  cur_buffer_lock->lock_next = hash_anchor->lock_next;
  hash_anchor->lock_next = cur_buffer_lock;
  pthread_mutex_unlock (&hash_anchor->hash_mutex);
#endif /* SERVER_MODE */

  perfmon_inc_stat (thread_p, PSTAT_LK_NUM_ACQUIRED_ON_PAGES);	/* monitoring */
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
pgbuf_unlock_page (THREAD_ENTRY * thread_p, PGBUF_BUFFER_HASH * hash_anchor, const VPID * vpid, int need_hash_mutex)
{
#if defined(SERVER_MODE)
  int rv;

  TSC_TICKS start_tick, end_tick;
  UINT64 lock_wait_time = 0;

  PGBUF_BUFFER_LOCK *prev_buffer_lock, *cur_buffer_lock;
  THREAD_ENTRY *cur_thrd_entry;

  if (need_hash_mutex)
    {
      if (perfmon_get_activation_flag () & PERFMON_ACTIVATION_FLAG_PB_HASH_ANCHOR)
	{
	  if (perfmon_is_perf_tracking ())
	    {
	      tsc_getticks (&start_tick);
	    }
	}
      rv = pthread_mutex_lock (&hash_anchor->hash_mutex);

      if (perfmon_is_perf_tracking_and_active (PERFMON_ACTIVATION_FLAG_PB_HASH_ANCHOR))
	{
	  tsc_getticks (&end_tick);
	  lock_wait_time = tsc_elapsed_utime (end_tick, start_tick);
	  perfmon_inc_stat (thread_p, PSTAT_PB_NUM_HASH_ANCHOR_WAITS);
	  perfmon_add_stat (thread_p, PSTAT_PB_TIME_HASH_ANCHOR_WAIT, lock_wait_time);
	}
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
 * Note: This function allocates a BCB from the buffer invalid list or the LRU list.
 *       It is invoked only when a page is not in buffer.
 */
static PGBUF_BCB *
pgbuf_allocate_bcb (THREAD_ENTRY * thread_p, const VPID * src_vpid)
{
  PGBUF_BCB *bufptr;
  PERF_UTIME_TRACKER time_tracker_alloc_bcb = PERF_UTIME_TRACKER_INITIALIZER;
  PERF_UTIME_TRACKER time_tracker_alloc_search_and_wait = PERF_UTIME_TRACKER_INITIALIZER;
  bool detailed_perf = perfmon_is_perf_tracking_and_active (PERFMON_ACTIVATION_FLAG_PB_VICTIMIZATION);
  int tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  PGBUF_STATUS *show_status = &pgbuf_Pool.show_status[tran_index];

#if defined (SERVER_MODE)
  struct timespec to;
  int r = 0;
  PERF_STAT_ID pstat_cond_wait;
  bool high_priority = false;
#endif /* SERVER_MODE */

  /* how it works: we need to free a bcb for new VPID.
   * 1. first source should be invalid list. initially, all bcb's will be in this list. sometimes, bcb's can be added to
   *    this list during runtime. in any case, these bcb's are not used by anyone, do not need any flush or other
   *    actions and are the best option for allocating a bcb.
   * 2. search the bcb in lru lists by calling pgbuf_get_victim.
   * 3. if search failed then:
   *    SERVER_MODE: thread is added to one of two queues: high priority waiting threads queue or low priority waiting
   *                 threads queue. high priority is usually populated by vacuum threads or by threads holding latch
   *                 on very hot pages (b-tree roots, heap headers, volume header or file headers).
   *                 thread will then be assigned a victim directly (there are multiple ways this can happen) and woken
   *                 up.
   *                 TODO: we have one big vulnerability with waiting threads. what if, for any reason, no one feeds the
   *                       waiting thread with a victim. page flush thread may be sleeping and no one wakes it, and the
   *                       activity may be so reduced that no adjustments are made to lists. thread ends up with
   *                       timeout. right now, after we added the victim rich hack, this may not happen. we could
   *                       consider a backup plan to generate victims for a forgotten waiter.
   *    SA_MODE: pages are flushed and victim is searched again (and we expect this time to find a victim).
   *
   * note: SA_MODE approach also applies to server-mode recovery (or in any circumstance which has page flush thread
   *       unavailable).
   */

  /* allocate a BCB from invalid BCB list */
  bufptr = pgbuf_get_bcb_from_invalid_list (thread_p);
  if (bufptr != NULL)
    {
      return bufptr;
    }

  PERF_UTIME_TRACKER_START (thread_p, &time_tracker_alloc_bcb);
  if (detailed_perf)
    {
      PERF_UTIME_TRACKER_START (thread_p, &time_tracker_alloc_search_and_wait);
    }

  /* search lru lists */
  bufptr = pgbuf_get_victim (thread_p);
  PERF_UTIME_TRACKER_TIME_AND_RESTART (thread_p, &time_tracker_alloc_search_and_wait, PSTAT_PB_ALLOC_BCB_SEARCH_VICTIM);
  if (bufptr != NULL)
    {
      goto end;
    }

#if defined (SERVER_MODE)
  if (pgbuf_is_page_flush_daemon_available ())
    {
    retry:
      high_priority = high_priority || VACUUM_IS_THREAD_VACUUM (thread_p) || pgbuf_is_thread_high_priority (thread_p);

      /* add to waiters thread list to be assigned victim directly */
      to.tv_sec = (int) time (NULL) + PGBUF_TIMEOUT;
      to.tv_nsec = 0;

      thread_lock_entry (thread_p);

      assert (pgbuf_Pool.direct_victims.bcb_victims[thread_p->index] == NULL);

      /* push to waiter thread list */
      if (high_priority)
	{
	  if (detailed_perf && VACUUM_IS_THREAD_VACUUM (thread_p))
	    {
	      perfmon_inc_stat (thread_p, PSTAT_PB_ALLOC_BCB_PRIORITIZE_VACUUM);
	    }
	  if (!pgbuf_Pool.direct_victims.waiter_threads_high_priority->produce (thread_p))
	    {
	      assert (false);
	      thread_unlock_entry (thread_p);
	      return NULL;
	    }
	  pstat_cond_wait = PSTAT_PB_ALLOC_BCB_COND_WAIT_HIGH_PRIO;
	}
      else
	{
	  if (!pgbuf_Pool.direct_victims.waiter_threads_low_priority->produce (thread_p))
	    {
	      /* ok, we have this very weird case when a consumer can be preempted for a very long time (which prevents
	       * producers from being able to push to queue). I don't know how is this even possible, I just know I
	       * found a case. I cannot tell exactly how long the consumer is preempted, but I know the time difference
	       * between the producer still waiting to be waken by that consumer and the producer failing to add was 93
	       * milliseconds. Which is huge if you ask me.
	       * I doubled the size of the queue, but theoretically, this is still possible. I also removed the
	       * ABORT_RELEASE, but we may have to think of a way to handle this preempted consumer case. */

	      /* we do a hack for this case. we add the thread to high-priority instead, which is usually less used and
	       * the same case is (almost) impossible to happen. */
	      if (!pgbuf_Pool.direct_victims.waiter_threads_high_priority->produce (thread_p))
		{
		  assert (false);
		  thread_unlock_entry (thread_p);
		  goto end;
		}
	      pstat_cond_wait = PSTAT_PB_ALLOC_BCB_COND_WAIT_HIGH_PRIO;
	    }
	  else
	    {
	      pstat_cond_wait = PSTAT_PB_ALLOC_BCB_COND_WAIT_LOW_PRIO;
	    }
	}

      /* make sure at least flush will feed us with bcb's. */
      // before migration of the page_flush_daemon it was a try_wakeup, check if still needed
      pgbuf_wakeup_page_flush_daemon (thread_p);

      show_status->num_flusher_waiting_threads++;

      r = thread_suspend_timeout_wakeup_and_unlock_entry (thread_p, &to, THREAD_ALLOC_BCB_SUSPENDED);

      show_status->num_flusher_waiting_threads--;

      PERF_UTIME_TRACKER_TIME (thread_p, &time_tracker_alloc_search_and_wait, pstat_cond_wait);

      if (r == NO_ERROR)
	{
	  if (thread_p->resume_status == THREAD_ALLOC_BCB_RESUMED)
	    {
	      bufptr = pgbuf_get_direct_victim (thread_p);
	      if (bufptr == NULL)
		{
		  /* bcb was fixed again */
		  high_priority = true;
		  goto retry;
		}
	      goto end;
	    }

	  /* no bcb should be allocated. */
	  /* interrupted */
	  assert (thread_p->resume_status == THREAD_RESUME_DUE_TO_INTERRUPT
		  || thread_p->resume_status == THREAD_RESUME_DUE_TO_SHUTDOWN);
	  if (pgbuf_Pool.direct_victims.bcb_victims[thread_p->index] != NULL)
	    {
	      /* a bcb was assigned before being interrupted. it must be "unassigned" */
	      pgbuf_bcb_update_flags (thread_p, pgbuf_Pool.direct_victims.bcb_victims[thread_p->index], 0,
				      PGBUF_BCB_VICTIM_DIRECT_FLAG | PGBUF_BCB_INVALIDATE_DIRECT_VICTIM_FLAG);
	      pgbuf_Pool.direct_victims.bcb_victims[thread_p->index] = NULL;
	    }
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INTERRUPTED, 0);
	}
      else
	{
	  /* should not timeout! */
	  assert (r != ER_CSS_PTHREAD_COND_TIMEDOUT);

	  thread_p->resume_status = THREAD_ALLOC_BCB_RESUMED;
	  thread_unlock_entry (thread_p);

	  if (r == ER_CSS_PTHREAD_COND_TIMEDOUT)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_COND_TIMEDOUT, 0);
	    }
	}
    }
#endif /* SERVER_MODE */
  else
    {
      /* flush */
      pgbuf_wakeup_page_flush_daemon (thread_p);

      /* search lru lists again */
      bufptr = pgbuf_get_victim (thread_p);
      PERF_UTIME_TRACKER_TIME (thread_p, &time_tracker_alloc_search_and_wait, PSTAT_PB_ALLOC_BCB_SEARCH_VICTIM);

      assert (bufptr != NULL);
    }

end:
  if (bufptr != NULL)
    {
      /* victimize the buffer */
      if (pgbuf_victimize_bcb (thread_p, bufptr) != NO_ERROR)
	{
	  assert (false);
	  bufptr = NULL;
	}
    }
  else
    {
      if (er_errid () == NO_ERROR)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_PB_ALL_BUFFERS_DIRTY, 1, 0);
	}
    }

  PERF_UTIME_TRACKER_TIME (thread_p, &time_tracker_alloc_bcb, PSTAT_PB_ALLOC_BCB);

  return bufptr;
}

/*
 * pgbuf_claim_bcb_for_fix () - function used for page fix to claim a bcb when page is not found in buffer
 *
 * return               : claimed BCB
 * thread_p (in)        : thread entry
 * vpid (in)            : page identifier
 * fetch_mode (in)      : fetch mode
 * hash_anchor (in/out) : hash anchor
 * perf (in/out)        : page fix performance monitoring helper
 * try_again (out)      : output true to trying getting bcb again
 */
static PGBUF_BCB *
pgbuf_claim_bcb_for_fix (THREAD_ENTRY * thread_p, const VPID * vpid, PAGE_FETCH_MODE fetch_mode,
			 PGBUF_BUFFER_HASH * hash_anchor, PGBUF_FIX_PERF * perf, bool * try_again)
{
  PGBUF_BCB *bufptr = NULL;
  PAGE_PTR pgptr = NULL;
  TDE_ALGORITHM tde_algo = TDE_ALGORITHM_NONE;
  bool success;
  int tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  PGBUF_STATUS *show_status = &pgbuf_Pool.show_status[tran_index];

#if defined (ENABLE_SYSTEMTAP)
  bool monitored = false;
  QUERY_ID query_id = NULL_QUERY_ID;
#endif /* ENABLE_SYSTEMTAP */

  assert (fetch_mode != OLD_PAGE_IF_IN_BUFFER);

  /* The page is not found in the hash chain the caller is holding hash_anchor->hash_mutex */
  if (er_errid () == ER_CSS_PTHREAD_MUTEX_TRYLOCK)
    {
      pthread_mutex_unlock (&hash_anchor->hash_mutex);
      PGBUF_BCB_CHECK_MUTEX_LEAKS ();
      return NULL;
    }

  /* In this case, the caller is holding only hash_anchor->hash_mutex. The hash_anchor->hash_mutex is to be
   * released in pgbuf_lock_page (). */
  if (pgbuf_lock_page (thread_p, hash_anchor, vpid) != PGBUF_LOCK_HOLDER)
    {
      if (perf->is_perf_tracking)
	{
	  tsc_getticks (&perf->end_tick);
	  tsc_elapsed_time_usec (&perf->tv_diff, perf->end_tick, perf->start_tick);
	  perf->lock_wait_time = perf->tv_diff.tv_sec * 1000000LL + perf->tv_diff.tv_usec;
	}

      if (fetch_mode == NEW_PAGE)
	{
	  perf->perf_page_found = PERF_PAGE_MODE_NEW_LOCK_WAIT;
	}
      else
	{
	  perf->perf_page_found = PERF_PAGE_MODE_OLD_LOCK_WAIT;
	}
      *try_again = true;
      return NULL;
    }

  if (perf->perf_page_found != PERF_PAGE_MODE_NEW_LOCK_WAIT && perf->perf_page_found != PERF_PAGE_MODE_OLD_LOCK_WAIT)
    {
      if (fetch_mode == NEW_PAGE)
	{
	  perf->perf_page_found = PERF_PAGE_MODE_NEW_NO_WAIT;
	}
      else
	{
	  perf->perf_page_found = PERF_PAGE_MODE_OLD_NO_WAIT;
	}
    }

  /* Now, the caller is not holding any mutex. */
  bufptr = pgbuf_allocate_bcb (thread_p, vpid);
  if (bufptr == NULL)
    {
      ASSERT_ERROR ();
      (void) pgbuf_unlock_page (thread_p, hash_anchor, vpid, true);
      PGBUF_BCB_CHECK_MUTEX_LEAKS ();
      return NULL;
    }

  /* Currently, caller has one allocated BCB and is holding mutex */

  /* initialize the BCB */
  bufptr->vpid = *vpid;
  assert (!pgbuf_bcb_avoid_victim (bufptr));
  bufptr->latch_mode = PGBUF_NO_LATCH;
  pgbuf_bcb_update_flags (thread_p, bufptr, 0, PGBUF_BCB_ASYNC_FLUSH_REQ);	/* todo: why this?? */
  pgbuf_bcb_check_and_reset_fix_and_avoid_dealloc (bufptr, ARG_FILE_LINE);
  LSA_SET_NULL (&bufptr->oldest_unflush_lsa);

  if (fetch_mode != NEW_PAGE)
    {
      /* Record number of reads in statistics */
      perfmon_inc_stat (thread_p, PSTAT_PB_NUM_IOREADS);
      show_status->num_pages_read++;

#if defined(ENABLE_SYSTEMTAP)
      query_id = qmgr_get_current_query_id (thread_p);
      if (query_id != NULL_QUERY_ID)
	{
	  monitored = true;
	  CUBRID_IO_READ_START (query_id);
	}
#endif /* ENABLE_SYSTEMTAP */

      if (dwb_read_page (thread_p, vpid, &bufptr->iopage_buffer->iopage, &success) != NO_ERROR)
	{
	  /* Should not happen */
	  assert (false);
	  return NULL;
	}
      else if (success == true)
	{
	  /* Nothing to do, copied from DWB */
	}
      else if (fileio_read (thread_p, fileio_get_volume_descriptor (vpid->volid), &bufptr->iopage_buffer->iopage,
			    vpid->pageid, IO_PAGESIZE) == NULL)
	{
	  /* There was an error in reading the page. Clean the buffer... since it may have been corrupted */
	  ASSERT_ERROR ();

	  /* bufptr->mutex will be released in following function. */
	  pgbuf_put_bcb_into_invalid_list (thread_p, bufptr);

	  /*
	   * Now, caller is not holding any mutex.
	   * the last argument of pgbuf_unlock_page () is true that
	   * means hash_mutex must be held before unlocking page.
	   */
	  (void) pgbuf_unlock_page (thread_p, hash_anchor, vpid, true);

#if defined(ENABLE_SYSTEMTAP)
	  if (monitored == true)
	    {
	      CUBRID_IO_READ_END (query_id, IO_PAGESIZE, 1);
	    }
#endif /* ENABLE_SYSTEMTAP */

	  PGBUF_BCB_CHECK_MUTEX_LEAKS ();
	  return NULL;
	}

      CAST_IOPGPTR_TO_PGPTR (pgptr, &bufptr->iopage_buffer->iopage);
      tde_algo = pgbuf_get_tde_algorithm (pgptr);
      if (tde_algo != TDE_ALGORITHM_NONE)
	{
	  if (tde_decrypt_data_page
	      (&bufptr->iopage_buffer->iopage, tde_algo, pgbuf_is_temporary_volume (vpid->volid),
	       &bufptr->iopage_buffer->iopage) != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	      pgbuf_put_bcb_into_invalid_list (thread_p, bufptr);
	      (void) pgbuf_unlock_page (thread_p, hash_anchor, vpid, true);
	      PGBUF_BCB_CHECK_MUTEX_LEAKS ();
	      return NULL;
	    }
	}

#if defined(ENABLE_SYSTEMTAP)
      if (monitored == true)
	{
	  CUBRID_IO_READ_END (query_id, IO_PAGESIZE, 0);
	}
#endif /* ENABLE_SYSTEMTAP */
      if (pgbuf_is_temporary_volume (vpid->volid) == true)
	{
	  /* Check if the first time to access */
	  if (!pgbuf_is_temp_lsa (bufptr->iopage_buffer->iopage.prv.lsa))
	    {
	      pgbuf_init_temp_page_lsa (&bufptr->iopage_buffer->iopage, IO_PAGESIZE);
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

      if (thread_get_sort_stats_active (thread_p))
	{
	  perfmon_inc_stat (thread_p, PSTAT_SORT_NUM_IO_PAGES);
	}
    }
  else
    {
      /* the caller is holding bufptr->mutex */

#if defined(CUBRID_DEBUG)
      pgbuf_scramble (&bufptr->iopage_buffer->iopage);
#endif /* CUBRID_DEBUG */

      /* Don't need to read page from disk since it is a new page. */
      if (pgbuf_is_temporary_volume (vpid->volid) == true)
	{
	  pgbuf_init_temp_page_lsa (&bufptr->iopage_buffer->iopage, IO_PAGESIZE);
	}
      else
	{
	  fileio_init_lsa_of_page (&bufptr->iopage_buffer->iopage, IO_PAGESIZE);
	}

      /* perm volume */
      if (bufptr->vpid.volid > NULL_VOLID)
	{
	  /* Init Page identifier of NEW_PAGE */
	  bufptr->iopage_buffer->iopage.prv.pageid = -1;
	  bufptr->iopage_buffer->iopage.prv.volid = -1;
	}

      if (thread_get_sort_stats_active (thread_p))
	{
	  perfmon_inc_stat (thread_p, PSTAT_SORT_NUM_DATA_PAGES);
	}

      show_status->num_pages_created++;
      show_status->num_hit++;
    }

  return bufptr;
}

/*
 * pgbuf_victimize_bcb () - Victimize given buffer page
 *   return: NO_ERROR, or ER_code
 *   bufptr(in): pointer to buffer page
 */
static int
pgbuf_victimize_bcb (THREAD_ENTRY * thread_p, PGBUF_BCB * bufptr)
{
#if defined(SERVER_MODE)
  if (thread_p == NULL)
    {
      assert (thread_p != NULL);
      thread_p = thread_get_thread_entry_info ();
    }
#endif /* SERVER_MODE */

  /* the caller is holding bufptr->mutex */

  /* before-flush, check victim condition again */
  if (!pgbuf_is_bcb_victimizable (bufptr, true))
    {
      assert (false);
      PGBUF_BCB_UNLOCK (bufptr);
      return ER_FAILED;
    }

  if (pgbuf_bcb_is_to_vacuum (bufptr))
    {
      pgbuf_bcb_update_flags (thread_p, bufptr, 0, PGBUF_BCB_TO_VACUUM_FLAG);
    }
  assert (bufptr->latch_mode == PGBUF_NO_LATCH);

  /* a safe victim */
  if (pgbuf_delete_from_hash_chain (thread_p, bufptr) != NO_ERROR)
    {
      return ER_FAILED;
    }

  /* If above function returns success, the caller is still holding bufptr->mutex.
   * Otherwise, the caller does not hold bufptr->mutex.
   */

  /* at this point, the caller is holding bufptr->mutex */

  return NO_ERROR;
}

/*
 * pgbuf_invalidate_bcb () - Invalidates BCB
 *   return: NO_ERROR, or ER_code
 *   bufptr(in): pointer to buffer page
 */
static int
pgbuf_invalidate_bcb (THREAD_ENTRY * thread_p, PGBUF_BCB * bufptr)
{
  /* the caller is holding bufptr->mutex */
  /* be sure that there is not any reader/writer */

  if (bufptr->latch_mode == PGBUF_LATCH_INVALID)
    {
      PGBUF_BCB_UNLOCK (bufptr);
      return NO_ERROR;
    }

  if (pgbuf_bcb_is_direct_victim (bufptr))
    {
      /* bcb is already assigned as direct victim, should be victimized soon, so there is no point in invalidating it
       * here */
      PGBUF_BCB_UNLOCK (bufptr);
      return NO_ERROR;
    }

  pgbuf_bcb_clear_dirty (thread_p, bufptr);

  LSA_SET_NULL (&bufptr->oldest_unflush_lsa);

  /* bufptr->mutex is still held by the caller. */
  switch (pgbuf_bcb_get_zone (bufptr))
    {
    case PGBUF_VOID_ZONE:
      break;

    default:
      assert (PGBUF_IS_BCB_IN_LRU (bufptr));
      pgbuf_lru_remove_bcb (thread_p, bufptr);
      break;
    }

  if (bufptr->latch_mode == PGBUF_NO_LATCH)
    {
      if (pgbuf_delete_from_hash_chain (thread_p, bufptr) != NO_ERROR)
	{
	  return ER_FAILED;
	}

      /* If above function returns failure, the caller does not hold bufptr->mutex. Otherwise, the caller is
       * holding bufptr->mutex. */

      /* Now, the caller is holding bufptr->mutex. */
      /* bufptr->mutex will be released in following function. */
      pgbuf_put_bcb_into_invalid_list (thread_p, bufptr);
    }
  else
    {
      /* todo: what to do? */
      assert (false);
      bufptr->latch_mode = PGBUF_NO_LATCH;
      PGBUF_BCB_UNLOCK (bufptr);
    }

  return NO_ERROR;
}

/*
 * pgbuf_bcb_safe_flush_force_unlock () - safe-flush bcb and make sure it does not remain locked.
 *
 * return           : error code
 * thread_p (in)    : thread entry
 * bufptr (in)      : bcb to flush
 * synchronous (in) : true if caller wants to wait for bcb to be flushed (if it cannot flush immediately it gets
 *                    blocked). if false, the caller will only request flush and continue.
 */
static int
pgbuf_bcb_safe_flush_force_unlock (THREAD_ENTRY * thread_p, PGBUF_BCB * bufptr, bool synchronous)
{
  int error_code = NO_ERROR;
  bool locked = true;

  error_code = pgbuf_bcb_safe_flush_internal (thread_p, bufptr, synchronous, &locked);
  if (locked)
    {
      PGBUF_BCB_UNLOCK (bufptr);
    }
  return error_code;
}

/*
 * pgbuf_bcb_safe_flush_force_lock () - safe-flush bcb and make sure it remains locked.
 *
 * return           : error code
 * thread_p (in)    : thread entry
 * bufptr (in)      : bcb to flush
 * synchronous (in) : true if caller wants to wait for bcb to be flushed (if it cannot flush immediately it gets
 *                    blocked). if false, the caller will only request flush and continue.
 */
static int
pgbuf_bcb_safe_flush_force_lock (THREAD_ENTRY * thread_p, PGBUF_BCB * bufptr, bool synchronous)
{
  int error_code = NO_ERROR;
  bool locked = true;

  error_code = pgbuf_bcb_safe_flush_internal (thread_p, bufptr, synchronous, &locked);
  if (error_code != NO_ERROR)
    {
      if (locked)
	{
	  PGBUF_BCB_UNLOCK (bufptr);
	}
      return error_code;
    }
  if (!locked)
    {
      PGBUF_BCB_LOCK (bufptr);
    }
  return NO_ERROR;
}

/*
 * pgbuf_bcb_safe_flush_internal () - safe-flush bcb. function will do all the necessary checks. flush is executed only
 *                                    bcb is dirty. function is safe in regard with concurrent latches and flushes.
 *
 * return           : error code
 * thread_p (in)    : thread entry
 * bufptr (in)      : bcb to flush
 * synchronous (in) : true if caller wants to wait for bcb to be flushed (if it cannot flush immediately it gets
 *                    blocked). if false, the caller will only request flush and continue.
 * locked (out)     : output if bcb is locked.
 */
static int
pgbuf_bcb_safe_flush_internal (THREAD_ENTRY * thread_p, PGBUF_BCB * bufptr, bool synchronous, bool * locked)
{
  int error_code = NO_ERROR;

  assert (bufptr->latch_mode != PGBUF_LATCH_FLUSH);

  PGBUF_BCB_CHECK_OWN (bufptr);
  *locked = true;

  /* the caller is holding bufptr->mutex */
  if (!pgbuf_bcb_is_dirty (bufptr))
    {
      /* not dirty; flush is not required */
      return NO_ERROR;
    }

  /* there are two cases when we cannot flush immediately:
   * 1. page is write latched. we cannot know when the latcher makes modifications, so it is not safe to flush the page.
   * 2. another thread is already flushing. allowing multiple concurrent flushes is not safe (we cannot guarantee the
   *    order of disk writing, therefore it is theoretically possible to write an old version over a newer version of
   *    the page).
   *
   * for the first case, we use the PGBUF_BCB_ASYNC_FLUSH_REQ flag to request a flush from the thread holding latch.
   * for the second case, we know the bcb is already being flushed. if we need to be sure page is flushed, we'll put
   * ourselves in bcb's waiting list (and a thread doing flush should wake us).
   */

  if (!pgbuf_bcb_is_flushing (bufptr)
      && (bufptr->latch_mode == PGBUF_NO_LATCH || bufptr->latch_mode == PGBUF_LATCH_READ
	  || (bufptr->latch_mode == PGBUF_LATCH_WRITE && pgbuf_find_thrd_holder (thread_p, bufptr) != NULL)))
    {
      /* don't have to wait for writer/flush */
      return pgbuf_bcb_flush_with_wal (thread_p, bufptr, false, locked);
    }

  /* page is write latched. notify the holder to flush it on unfix. */
  assert (pgbuf_bcb_is_flushing (bufptr) || bufptr->latch_mode == PGBUF_LATCH_WRITE);
  if (!pgbuf_bcb_is_flushing (bufptr))
    {
      pgbuf_bcb_update_flags (thread_p, bufptr, PGBUF_BCB_ASYNC_FLUSH_REQ, 0);
    }

  if (synchronous == true)
    {
      /* wait for bcb to be flushed. */
      *locked = false;
      error_code = pgbuf_block_bcb (thread_p, bufptr, PGBUF_LATCH_FLUSH, 0, false);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	}
      return error_code;
    }

  /* don't wait for flush */
  return NO_ERROR;
}

/*
 * pgbuf_get_bcb_from_invalid_list () - Get BCB from buffer invalid list
 *
 * return: If success, a newly allocated BCB, otherwise NULL
 * thread_p (in)     : thread entry
 *
 * Note: This function disconnects a BCB on the top of the buffer invalid list
 *       and returns it. Before disconnection, the thread must hold the
 *       invalid list mutex and after disconnection, release the mutex.
 */
static PGBUF_BCB *
pgbuf_get_bcb_from_invalid_list (THREAD_ENTRY * thread_p)
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

      PGBUF_BCB_LOCK (bufptr);
      bufptr->next_BCB = NULL;
      pgbuf_bcb_change_zone (thread_p, bufptr, 0, PGBUF_VOID_ZONE);

      perfmon_inc_stat (thread_p, PSTAT_PB_VICTIM_USE_INVALID_BCB);
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
pgbuf_put_bcb_into_invalid_list (THREAD_ENTRY * thread_p, PGBUF_BCB * bufptr)
{
#if defined(SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */

  /* the caller is holding bufptr->mutex */
  VPID_SET_NULL (&bufptr->vpid);
  bufptr->latch_mode = PGBUF_LATCH_INVALID;
  assert ((bufptr->flags & PGBUF_BCB_FLAGS_MASK) == 0);
  pgbuf_bcb_change_zone (thread_p, bufptr, 0, PGBUF_INVALID_ZONE);
  pgbuf_bcb_check_and_reset_fix_and_avoid_dealloc (bufptr, ARG_FILE_LINE);

  rv = pthread_mutex_lock (&pgbuf_Pool.buf_invalid_list.invalid_mutex);
  bufptr->next_BCB = pgbuf_Pool.buf_invalid_list.invalid_top;
  pgbuf_Pool.buf_invalid_list.invalid_top = bufptr;
  pgbuf_Pool.buf_invalid_list.invalid_cnt += 1;
  PGBUF_BCB_UNLOCK (bufptr);
  pthread_mutex_unlock (&pgbuf_Pool.buf_invalid_list.invalid_mutex);

  return NO_ERROR;
}

/*
 * pgbuf_get_shared_lru_index_for_add () - get a shared index to add a new bcb. we'll use a round-robin way to choose
 *                                         next list, but we'll avoid biggest list (just to keep things balanced).
 *
 * return : shared lru index
 */
STATIC_INLINE int
pgbuf_get_shared_lru_index_for_add (void)
{
#define PAGE_ADD_REFRESH_STAT \
  MAX (2 * pgbuf_Pool.num_buffers / PGBUF_SHARED_LRU_COUNT, 10000)

  int i;
  unsigned int lru_idx, refresh_stat_cnt;

  lru_idx = ATOMIC_INC_32 (&pgbuf_Pool.quota.add_shared_lru_idx, 1);
  refresh_stat_cnt = lru_idx % PAGE_ADD_REFRESH_STAT;

  /* check if there is an in-balance BCBs distribution across shared LRUs */
  if (refresh_stat_cnt == 0)
    {
      int shared_lru_bcb_sum;
      int max_bcb, min_bcb;
      int lru_idx_with_max;
      int this_lru_cnt;
      int curr_avoid_lru_idx;

      shared_lru_bcb_sum = 0;
      max_bcb = 0;
      min_bcb = pgbuf_Pool.num_buffers;
      lru_idx_with_max = -1;
      /* update unbalanced LRU idx */
      for (i = 0; i < PGBUF_SHARED_LRU_COUNT; i++)
	{
	  this_lru_cnt = PGBUF_LRU_LIST_COUNT (PGBUF_GET_LRU_LIST (i));
	  shared_lru_bcb_sum += this_lru_cnt;

	  if (this_lru_cnt > max_bcb)
	    {
	      max_bcb = this_lru_cnt;
	      lru_idx_with_max = i;
	    }

	  if (this_lru_cnt < min_bcb)
	    {
	      min_bcb = this_lru_cnt;
	    }
	}

      if (shared_lru_bcb_sum > pgbuf_Pool.num_buffers / 10
	  && (max_bcb > (int) (1.3f * shared_lru_bcb_sum) / PGBUF_SHARED_LRU_COUNT || max_bcb > 2 * min_bcb))
	{
	  ATOMIC_TAS_32 (&pgbuf_Pool.quota.avoid_shared_lru_idx, lru_idx_with_max);
	}
      else
	{
	  curr_avoid_lru_idx = pgbuf_Pool.quota.avoid_shared_lru_idx;
	  if (curr_avoid_lru_idx == -1
	      || (PGBUF_LRU_LIST_COUNT (PGBUF_GET_LRU_LIST (curr_avoid_lru_idx))
		  < shared_lru_bcb_sum / PGBUF_SHARED_LRU_COUNT))
	    {
	      ATOMIC_TAS_32 (&pgbuf_Pool.quota.avoid_shared_lru_idx, -1);
	    }
	}
    }

  lru_idx = lru_idx % PGBUF_SHARED_LRU_COUNT;

  /* avoid to add in shared LRU idx having too many BCBs */
  if (pgbuf_Pool.quota.avoid_shared_lru_idx == (int) lru_idx)
    {
      lru_idx = ATOMIC_INC_32 (&pgbuf_Pool.quota.add_shared_lru_idx, 1);
      lru_idx = lru_idx % PGBUF_SHARED_LRU_COUNT;
    }

  return lru_idx;
#undef PAGE_ADD_REFRESH_STAT
}

/*
 * pgbuf_get_victim () - get a victim bcb from page buffer.
 *
 * return        : victim candidate or NULL if no candidate was found
 * thread_p (in) : thread entry
 *
 * Note: If a victim BCB is found, this function will already lock it. This means that the caller will have exclusive
 *       access to the returned BCB.
 */
static PGBUF_BCB *
pgbuf_get_victim (THREAD_ENTRY * thread_p)
{
#define PERF(id) if (detailed_perf) perfmon_inc_stat (thread_p, id)

  PGBUF_BCB *victim = NULL;
  bool detailed_perf = perfmon_is_perf_tracking_and_active (PERFMON_ACTIVATION_FLAG_PB_VICTIMIZATION);
  bool has_flush_thread = pgbuf_is_page_flush_daemon_available ();
  int nloops = 0;		/* used as safe-guard against infinite loops */
  int private_lru_idx;
  PGBUF_LRU_LIST *lru_list = NULL;
  bool restrict_other = false;
  bool searched_own = false;
  UINT64 initial_consume_cursor, current_consume_cursor;
  PERF_UTIME_TRACKER perf_tracker = PERF_UTIME_TRACKER_INITIALIZER;

  ATOMIC_INC_32 (&pgbuf_Pool.monitor.lru_victim_req_cnt, 1);

  /* how this works:
   * we need to find a victim in one of all lru lists. we have two lru list types: private and shared. private are pages
   * fixed by a single transaction, while shared are pages fix by multiple transactions. we usually prioritize the
   * private lists.
   * the order we look for victimize is this:
   * 1. first search in own private list if it is not under quota.
   * 2. look in another private list.
   * 3. look in a shared list.
   *
   * normally, if the system does not lack victims, one of the three searches should provide a victim candidate.
   * however, we can be unlucky and not find a candidate with the three steps. this is especially possible when we have
   * only one active transaction, with long transactions, and many vacuum workers trying to catch up. all candidates
   * are found in a single private list, which means that many vacuum workers may not find the lists in lru queue.
   * for this case, we loop the three searches, as long as pgbuf_Pool.monitor.victim_rich is true.
   *
   * note: if quota is disabled (although this is not recommended), only shared lists are searched.
   *
   * note: if all above failed to produce a victim, we'll try to victimize from own private even if it is under quota.
   *       we found a strange particular case when all private lists were on par with their quota's (but just below),
   *       shared lists had no lru 3 zone and nothing could be victimized or flushed.
   */

  /* 1. search own private list */
  if (PGBUF_THREAD_HAS_PRIVATE_LRU (thread_p))
    {
      /* first try my own private list */
      private_lru_idx = PGBUF_LRU_INDEX_FROM_PRIVATE (PGBUF_PRIVATE_LRU_FROM_THREAD (thread_p));
      lru_list = PGBUF_GET_LRU_LIST (private_lru_idx);

      /* don't victimize from own list if it is under quota */
      if (PGBUF_LRU_LIST_IS_ONE_TWO_OVER_QUOTA (lru_list)
	  || (PGBUF_LRU_LIST_IS_OVER_QUOTA (lru_list) && lru_list->count_vict_cand > 0))
	{
	  if (detailed_perf)
	    {
	      PERF_UTIME_TRACKER_START (thread_p, &perf_tracker);
	    }
	  victim = pgbuf_get_victim_from_lru_list (thread_p, private_lru_idx);
	  if (victim != NULL)
	    {
	      PERF (PSTAT_PB_OWN_VICTIM_PRIVATE_LRU_SUCCESS);
	      if (detailed_perf)
		{
		  PERF_UTIME_TRACKER_TIME (thread_p, &perf_tracker, PSTAT_PB_VICTIM_SEARCH_OWN_PRIVATE_LISTS);
		}
	      return victim;
	    }
	  /* failed */
	  PERF (PSTAT_PB_VICTIM_OWN_PRIVATE_LRU_FAIL);
	  if (detailed_perf)
	    {
	      PERF_UTIME_TRACKER_TIME (thread_p, &perf_tracker, PSTAT_PB_VICTIM_SEARCH_OWN_PRIVATE_LISTS);
	    }

	  /* if over quota, we are not allowed to search in other lru lists. we'll wait for victim.
	   * note: except vacuum threads who ignore unfixes and have no quota. */
	  if (!PGBUF_THREAD_SHOULD_IGNORE_UNFIX (thread_p))
	    {
	      /* still, offer a chance to those that are just slightly over quota. this actually targets new
	       * transactions that do not have a quota yet... let them get a few bcb's first until their activity
	       * becomes relevant. */
	      restrict_other = PGBUF_LRU_LIST_IS_OVER_QUOTA_WITH_BUFFER (lru_list);
	    }
	  searched_own = true;
	}
    }

  /* 2. search other private list.
   *
   * note: in single-thread context, the only list is mine. no point in trying to victimize again
   * note: if restrict_other is true, only other big private lists can be used for victimization
   */
  if (PGBUF_PAGE_QUOTA_IS_ENABLED && has_flush_thread)
    {
      if (detailed_perf)
	{
	  PERF_UTIME_TRACKER_START (thread_p, &perf_tracker);
	}
      victim = pgbuf_lfcq_get_victim_from_private_lru (thread_p, restrict_other);
      if (victim != NULL)
	{
	  if (detailed_perf)
	    {
	      PERF_UTIME_TRACKER_TIME (thread_p, &perf_tracker, PSTAT_PB_VICTIM_SEARCH_OTHERS_PRIVATE_LISTS);
	    }
	  return victim;
	}
      if (detailed_perf)
	{
	  PERF_UTIME_TRACKER_TIME (thread_p, &perf_tracker, PSTAT_PB_VICTIM_SEARCH_OTHERS_PRIVATE_LISTS);
	}
    }

  /* loop:
   *
   * DOESN'T HAVE FLUSH THREAD: one iteration could fail, because the shared list's last victims have been set dirty.
   * however, if there are other lists having victims, we should find them.
   * it is possible to not have any victims, in which case the shared list queue should become empty. we'll have to do a
   * flush and search again.
   * we'd like to avoid looping infinitely (if there's a bug), so we use the nloops safe-guard. Each shared list should
   * be removed after a failed search, so the maximum accepted number of loops is pgbuf_Pool.num_LRU_list.
   */

  if (detailed_perf)
    {
      PERF_UTIME_TRACKER_START (thread_p, &perf_tracker);
    }

  initial_consume_cursor = pgbuf_Pool.shared_lrus_with_victims->get_consumer_cursor ();
  do
    {
      /* 3. search a shared list. */
      victim = pgbuf_lfcq_get_victim_from_shared_lru (thread_p, has_flush_thread);
      if (victim != NULL)
	{
	  if (detailed_perf)
	    {
	      PERF_UTIME_TRACKER_TIME (thread_p, &perf_tracker, PSTAT_PB_VICTIM_SEARCH_SHARED_LISTS);
	    }
	  return victim;
	}
      current_consume_cursor = pgbuf_Pool.shared_lrus_with_victims->get_consumer_cursor ();
    }
  while (!has_flush_thread && !pgbuf_Pool.shared_lrus_with_victims->is_empty ()
	 && ((int) (current_consume_cursor - initial_consume_cursor) <= pgbuf_Pool.num_LRU_list)
	 && (++nloops <= pgbuf_Pool.num_LRU_list));
  /* todo: maybe we can find a less complicated condition of looping. Probably no need to use nloops <= pgbuf_Pool.num_LRU_list. */
  if (detailed_perf)
    {
      PERF_UTIME_TRACKER_TIME (thread_p, &perf_tracker, PSTAT_PB_VICTIM_SEARCH_SHARED_LISTS);
    }

  /* no victim found... */
  assert (victim == NULL);

  PERF (PSTAT_PB_VICTIM_ALL_LRU_FAIL);

  if (PGBUF_THREAD_HAS_PRIVATE_LRU (thread_p) && !searched_own)
    {
      /* try on own private even if it is under quota. */
      private_lru_idx = PGBUF_LRU_INDEX_FROM_PRIVATE (PGBUF_PRIVATE_LRU_FROM_THREAD (thread_p));
      lru_list = PGBUF_GET_LRU_LIST (private_lru_idx);

      victim = pgbuf_get_victim_from_lru_list (thread_p, private_lru_idx);
      if (victim != NULL)
	{
	  PERF (PSTAT_PB_OWN_VICTIM_PRIVATE_LRU_SUCCESS);
	  return victim;
	}
      /* failed */
      if (detailed_perf)
	{
	  PERF (PSTAT_PB_VICTIM_OWN_PRIVATE_LRU_FAIL);
	}
    }
  assert (victim == NULL);

  return victim;

#undef PERF
}

/*
 * pgbuf_is_bcb_fixed_by_any () - is page fixed by any thread?
 *
 * return               : NO_ERROR
 * PGBUF_BCB * bcb (in) : bcb
 * has_mutex_lock (in)  : true if current thread has lock on bcb
 *
 * note: if has_mutex_lock is true, even if bcb->latch_mode is not PGBUF_NO_LATCH, we consider this to be temporary.
 *       this must be during pgbuf_unfix and latch_mode will be set to PGBUF_NO_LATCH before bcb mutex is released.
 */
STATIC_INLINE bool
pgbuf_is_bcb_fixed_by_any (PGBUF_BCB * bcb, bool has_mutex_lock)
{
#if defined (SERVER_MODE)
  if (has_mutex_lock)
    {
      PGBUF_BCB_CHECK_OWN (bcb);
    }

  /* note: sometimes, the next wait thread could only be threads waiting for flush. however, these are exceptional
   *       cases. we'd rather miss a few good bcb's from time to time, rather than processing the waiting list for
   *       every bcb. */

  return bcb->fcnt > 0 || bcb->next_wait_thrd != NULL || (!has_mutex_lock && bcb->latch_mode != PGBUF_NO_LATCH);
#else /* !SERVER_MODE */
  return bcb->fcnt != 0;
#endif /* !SERVER_MODE */
}

/*
 * pgbuf_is_bcb_victimizable () - check whether bcb can be victimized.
 *
 * return              : true if bcb can be victimized, false otherwise
 * bcb (in)            : bcb
 * has_mutex_lock (in) : true if bcb mutex is owned
 */
STATIC_INLINE bool
pgbuf_is_bcb_victimizable (PGBUF_BCB * bcb, bool has_mutex_lock)
{
  /* must not be dirty */
  if (pgbuf_bcb_avoid_victim (bcb))
    {
      return false;
    }

#if defined (SERVER_MODE)
  /* must not be fixed and must not have waiters. */
  if (pgbuf_is_bcb_fixed_by_any (bcb, has_mutex_lock))
    {
      return false;
    }
#endif /* SERVER_MODE */

  /* valid */
  return true;
}

/*
 * pgbuf_get_victim_from_lru_list () - Get victim BCB from the bottom of LRU list
 *   return: If success, BCB, otherwise NULL
 *   lru_idx (in)     : index of LRU list
 *
 * Note: This function disconnects BCB from the bottom of the LRU list and returns it if its fcnt == 0.
 *       If its fcnt != 0, makes bufptr->PrevBCB bottom and retry.
 *       While this processing, the caller must be the holder of the LRU list.
 */
static PGBUF_BCB *
pgbuf_get_victim_from_lru_list (THREAD_ENTRY * thread_p, const int lru_idx)
{
#define PERF(pstatid) if (perf_tracking) perfmon_inc_stat (thread_p, pstatid)
#define MAX_DEPTH 1000

  PGBUF_BCB *bufptr;
  int found_victim_cnt = 0;
  int search_cnt = 0;
  int lru_victim_cnt = 0;
  PGBUF_LRU_LIST *lru_list;
  PGBUF_BCB *bufptr_victimizable = NULL;
  PGBUF_BCB *bufptr_start = NULL;
  PGBUF_BCB *victim_hint = NULL;

  bool perf_tracking = perfmon_is_perf_tracking_and_active (PERFMON_ACTIVATION_FLAG_PB_VICTIMIZATION);

  lru_list = &pgbuf_Pool.buf_LRU_list[lru_idx];

  PERF (PSTAT_PB_VICTIM_GET_FROM_LRU);

  /* check if LRU list is empty */
  if (lru_list->count_vict_cand == 0)
    {
      PERF (PSTAT_PB_VICTIM_GET_FROM_LRU_LIST_WAS_EMPTY);
      return NULL;
    }

  pthread_mutex_lock (&lru_list->mutex);
  if (lru_list->bottom == NULL || !PGBUF_IS_BCB_IN_LRU_VICTIM_ZONE (lru_list->bottom))
    {
      /* no zone 3 */
      PERF (PSTAT_PB_VICTIM_GET_FROM_LRU_LIST_WAS_EMPTY);
      pthread_mutex_unlock (&lru_list->mutex);
      return NULL;
    }

  if (PGBUF_IS_PRIVATE_LRU_ONE_TWO_OVER_QUOTA (lru_idx))
    {
      /* first adjust lru1 zone */
      pgbuf_lru_adjust_zones (thread_p, lru_list, false);
    }

  /* search for non dirty bcb */
  lru_victim_cnt = lru_list->count_vict_cand;
  if (lru_victim_cnt <= 0)
    {
      /* no victims */
      PERF (PSTAT_PB_VICTIM_GET_FROM_LRU_LIST_WAS_EMPTY);
      assert (lru_victim_cnt == 0);
      pthread_mutex_unlock (&lru_list->mutex);
      return NULL;
    }

  if (!pgbuf_bcb_is_dirty (lru_list->bottom) && lru_list->victim_hint != lru_list->bottom)
    {
      /* update hint to bottom. sometimes it may be out of sync. */
      assert (PGBUF_IS_BCB_IN_LRU_VICTIM_ZONE (lru_list->bottom));
      if (PGBUF_IS_BCB_IN_LRU_VICTIM_ZONE (lru_list->bottom))
	{
	  (void) ATOMIC_TAS_ADDR (&lru_list->victim_hint, lru_list->bottom);
	}
      else
	{
	  (void) ATOMIC_TAS_ADDR (&lru_list->victim_hint, (PGBUF_BCB *) NULL);
	}
    }

  /* we will search */
  found_victim_cnt = 0;
  bufptr_victimizable = NULL;

  /* start searching with victim hint */
  victim_hint = lru_list->victim_hint;
  if (victim_hint == NULL)
    {
      bufptr_start = lru_list->bottom;
    }
  else
    {
      bufptr_start = victim_hint;
    }

  for (bufptr = bufptr_start; bufptr != NULL && PGBUF_IS_BCB_IN_LRU_VICTIM_ZONE (bufptr) && search_cnt < MAX_DEPTH;
       bufptr = bufptr->prev_BCB, search_cnt++)
    {
      /* must not be any other case that invalidates a victim: is flushing, direct victim */
      if (pgbuf_bcb_avoid_victim (bufptr))
	{
	  /* this bcb is not valid for victimization */
	  continue;
	}

      /* must not be fixed */
      if (pgbuf_is_bcb_fixed_by_any (bufptr, false))
	{
	  /* this bcb cannot be used now, but it is a valid victim candidate. maybe we should update victim hint */
	  if (bufptr_victimizable == NULL)
	    {
	      bufptr_victimizable = bufptr;

	      /* update hint if this is not bufptr_start and hint has not changed in the meantime. */
	      if (bufptr_victimizable != victim_hint
		  && ATOMIC_CAS_ADDR (&lru_list->victim_hint, victim_hint, bufptr_victimizable))
		{
		  /* hint advanced */
		}

	      assert (lru_list->victim_hint == NULL || PGBUF_IS_BCB_IN_LRU_VICTIM_ZONE (lru_list->victim_hint));
	    }

	  found_victim_cnt++;
	  if (found_victim_cnt >= lru_victim_cnt)
	    {
	      /* early out: probably we won't find others */
	      break;
	    }
	  continue;
	}

      /* a victim candidate. we need to lock its BCB, but since we have LRU mutex, we can only do it conditionally.
       * chances are we'll get the mutex. */
      if (PGBUF_BCB_TRYLOCK (bufptr) == 0)
	{
	  if (pgbuf_is_bcb_victimizable (bufptr, true))
	    {
	      if (bufptr_victimizable == NULL)
		{
		  /* try to update hint to bufptr->prev_BCB */
		  pgbuf_lru_advance_victim_hint (thread_p, lru_list, victim_hint, bufptr->prev_BCB, false);
		}
	      pgbuf_remove_from_lru_list (thread_p, bufptr, lru_list);

#if defined (SERVER_MODE)
	      /* todo: this is a hack */
	      if (pgbuf_Pool.direct_victims.waiter_threads_low_priority->size ()
		  >= (5 + (thread_num_total_threads () / 20)))
		{
		  pgbuf_panic_assign_direct_victims_from_lru (thread_p, lru_list, bufptr->prev_BCB);
		}
#endif /* SERVER_MODE */

	      if (lru_list->bottom != NULL && pgbuf_bcb_is_dirty (lru_list->bottom)
		  && pgbuf_is_page_flush_daemon_available ())
		{
		  /* new bottom is dirty... make sure that flush will wake up */
		  pgbuf_wakeup_page_flush_daemon (thread_p);
		}
	      pthread_mutex_unlock (&lru_list->mutex);

	      pgbuf_add_vpid_to_aout_list (thread_p, &bufptr->vpid, lru_idx);

	      return bufptr;
	    }
	  else
	    {
	      PGBUF_BCB_UNLOCK (bufptr);
	    }
	}
      else
	{
	  /* failed try lock in single-threaded? impossible */
	  assert (pgbuf_is_page_flush_daemon_available ());

	  /* save the avoid victim bufptr. maybe it will be reset until we finish the search */
	  if (bufptr_victimizable == NULL)
	    {
	      bufptr_victimizable = bufptr;
	      /* try to replace victim if it was not already changed. */
	      if (bufptr != victim_hint && ATOMIC_CAS_ADDR (&lru_list->victim_hint, victim_hint, bufptr_victimizable))
		{
		  /* modified hint */
		}

	      assert (lru_list->victim_hint == NULL || PGBUF_IS_BCB_IN_LRU_VICTIM_ZONE (lru_list->victim_hint));
	    }
	  found_victim_cnt++;
	  if (found_victim_cnt >= lru_victim_cnt)
	    {
	      /* early out: probably we won't find others */
	      break;
	    }
	}
    }

  PERF (PSTAT_PB_VICTIM_GET_FROM_LRU_FAIL);
  if (bufptr_victimizable == NULL && victim_hint != NULL)
    {
      /* we had a hint and we failed to find any victim candidates. */
      PERF (PSTAT_PB_VICTIM_GET_FROM_LRU_BAD_HINT);
      assert (PGBUF_IS_BCB_IN_LRU_VICTIM_ZONE (lru_list->bottom));
      if (lru_list->count_vict_cand > 0 && PGBUF_IS_BCB_IN_LRU_VICTIM_ZONE (lru_list->bottom))
	{
	  /* set victim hint to bottom */
	  (void) ATOMIC_CAS_ADDR (&lru_list->victim_hint, victim_hint, lru_list->bottom);
	}
      else
	{
	  /* no hint */
	  (void) ATOMIC_CAS_ADDR (&lru_list->victim_hint, victim_hint, (PGBUF_BCB *) NULL);
	}
    }

  pthread_mutex_unlock (&lru_list->mutex);

  /* we need more victims */
  pgbuf_wakeup_page_flush_daemon (thread_p);
  /* failed finding victim in single-threaded, although the number of victim candidates is positive? impossible!
   * note: not really impossible. the thread may have the victimizable fixed. but bufptr_victimizable must not be
   * NULL. */
  assert (pgbuf_is_page_flush_daemon_available () || (bufptr_victimizable != NULL) || (search_cnt == MAX_DEPTH));
  return NULL;

#undef PERF
#undef MAX_DEPTH
}

#if defined (SERVER_MODE)
/*
 * pgbuf_panic_assign_direct_victims_from_lru () - panic assign direct victims from lru.
 *
 * return         : number of assigned victims.
 * thread_p (in)  : thread entry
 * lru_list (in)  : lru list
 * bcb_start (in) : starting bcb
 */
static int
pgbuf_panic_assign_direct_victims_from_lru (THREAD_ENTRY * thread_p, PGBUF_LRU_LIST * lru_list, PGBUF_BCB * bcb_start)
{
#define MAX_DEPTH 1000
  PGBUF_BCB *bcb = NULL;
  int n_assigned = 0;
  int count = 0;

  /* statistics shows not useful */

  if (bcb_start == NULL)
    {
      return 0;
    }
  assert (pgbuf_bcb_get_lru_index (bcb_start) == lru_list->index);

  /* panic victimization function */

  for (bcb = bcb_start;
       bcb != NULL && PGBUF_IS_BCB_IN_LRU_VICTIM_ZONE (bcb) && lru_list->count_vict_cand > 0 && count < MAX_DEPTH;
       bcb = bcb->prev_BCB, count++)
    {
      assert (pgbuf_bcb_get_lru_index (bcb) == lru_list->index);
      if (!pgbuf_is_bcb_victimizable (bcb, false))
	{
	  continue;
	}

      /* lock mutex. just try. */
      if (PGBUF_BCB_TRYLOCK (bcb) != 0)
	{
	  continue;
	}
      if (!pgbuf_is_bcb_victimizable (bcb, true))
	{
	  PGBUF_BCB_UNLOCK (bcb);
	  continue;
	}
      if (!pgbuf_assign_direct_victim (thread_p, bcb))
	{
	  /* no more waiting threads */
	  PGBUF_BCB_UNLOCK (bcb);
	  break;
	}
      /* assigned directly */
      PGBUF_BCB_UNLOCK (bcb);
      if (perfmon_is_perf_tracking_and_active (PERFMON_ACTIVATION_FLAG_PB_VICTIMIZATION))
	{
	  perfmon_inc_stat (thread_p, PSTAT_PB_VICTIM_ASSIGN_DIRECT_PANIC);
	}
      n_assigned++;
    }

  return n_assigned;

#undef MAX_DEPTH
}

/*
 * pgbuf_direct_victims_maintenance () - assign direct victims via searching. the purpose of function is to make sure a
 *                                       victim is assigned even when system has low to no activity, which prevents
 *                                       bcb's from being assigned to a waiting thread. basically, this is the backup
 *                                       plan.
 *
 * return        : void
 * thread_p (in) : thread entry
 */
void
pgbuf_direct_victims_maintenance (THREAD_ENTRY * thread_p)
{
#define DEFAULT_ASSIGNS_PER_ITERATION 5
  int nassigns = DEFAULT_ASSIGNS_PER_ITERATION;
  bool restarted;
  int index;

  /* note this is designed for single-threaded use only. the static values are used for pick lists with a round-robin
   * system */
  static int prv_index = 0;
  static int shr_index = 0;

  /* privates */
  for (index = prv_index, restarted = false;
       pgbuf_is_any_thread_waiting_for_direct_victim () && nassigns > 0 && index != prv_index && !restarted;
       (index == PGBUF_PRIVATE_LRU_COUNT - 1) ? index = 0, restarted = true : index++)
    {
      pgbuf_lfcq_assign_direct_victims (thread_p, PGBUF_LRU_INDEX_FROM_PRIVATE (index), &nassigns);
    }
  prv_index = index;

  /* shared */
  for (index = shr_index, restarted = false;
       pgbuf_is_any_thread_waiting_for_direct_victim () && nassigns > 0 && index != shr_index && !restarted;
       (index == PGBUF_SHARED_LRU_COUNT - 1) ? index = 0, restarted = true : index++)
    {
      pgbuf_lfcq_assign_direct_victims (thread_p, index, &nassigns);
    }
  shr_index = index;

#undef DEFAULT_ASSIGNS_PER_ITERATION
}

/*
 * pgbuf_lfcq_assign_direct_victims () - get list from queue and assign victims directly.
 *
 * return                 : void
 * thread_p (in)          : thread entry
 * lru_idx (in)           : lru index
 * nassign_inout (in/out) : update the number of victims to assign
 */
STATIC_INLINE void
pgbuf_lfcq_assign_direct_victims (THREAD_ENTRY * thread_p, int lru_idx, int *nassign_inout)
{
  PGBUF_LRU_LIST *lru_list;
  PGBUF_BCB *victim_hint = NULL;
  int nassigned = 0;

  lru_list = PGBUF_GET_LRU_LIST (lru_idx);
  if (lru_list->count_vict_cand > 0)
    {
      pthread_mutex_lock (&lru_list->mutex);
      victim_hint = lru_list->victim_hint;
      nassigned = pgbuf_panic_assign_direct_victims_from_lru (thread_p, lru_list, victim_hint);
      if (nassigned == 0 && lru_list->count_vict_cand > 0 && pgbuf_is_any_thread_waiting_for_direct_victim ())
	{
	  /* maybe hint was bad? that's most likely case. reset the hint to bottom. */
	  assert (PGBUF_IS_BCB_IN_LRU_VICTIM_ZONE (lru_list->bottom));
	  if (PGBUF_IS_BCB_IN_LRU_VICTIM_ZONE (lru_list->bottom))
	    {
	      (void) ATOMIC_CAS_ADDR (&lru_list->victim_hint, victim_hint, lru_list->bottom);
	    }
	  else
	    {
	      (void) ATOMIC_CAS_ADDR (&lru_list->victim_hint, victim_hint, (PGBUF_BCB *) NULL);
	    }

	  /* check from bottom anyway */
	  nassigned = pgbuf_panic_assign_direct_victims_from_lru (thread_p, lru_list, lru_list->bottom);
	}
      pthread_mutex_unlock (&lru_list->mutex);

      (*nassign_inout) -= nassigned;
    }
}
#endif /* SERVER_MODE */

/*
 * pgbuf_lru_add_bcb_to_top () - add a bcb to lru list top
 *
 * return        : void
 * thread_p (in) : thread entry
 * bcb (in)      : bcb added to top
 * lru_list (in) : lru list
 */
STATIC_INLINE void
pgbuf_lru_add_bcb_to_top (THREAD_ENTRY * thread_p, PGBUF_BCB * bcb, PGBUF_LRU_LIST * lru_list)
{
  /* there will be no previous BCB */
  bcb->prev_BCB = NULL;

  /* next bcb is current top */
  bcb->next_BCB = lru_list->top;

  /* is list empty? */
  if (lru_list->top == NULL)
    {
      /* yeah. bottom should also be NULL */
      assert (lru_list->bottom == NULL);
      /* bcb is top and bottom of list */
      lru_list->bottom = bcb;
    }
  else
    {
      /* update previous top link and change top */
      lru_list->top->prev_BCB = bcb;
    }
  /* we have new top */
  lru_list->top = bcb;

  if (lru_list->bottom_1 == NULL)
    {
      /* empty lru 1 zone */
      assert (lru_list->count_lru1 == 0);
      /* set middle to this bcb */
      lru_list->bottom_1 = bcb;
    }

  /* increment list tick when adding to top */
  if (++lru_list->tick_list >= DB_INT32_MAX)
    {
      lru_list->tick_list = 0;
    }

  pgbuf_bcb_change_zone (thread_p, bcb, lru_list->index, PGBUF_LRU_1_ZONE);
}

/*
 * pgbuf_lru_add_bcb_to_middle () - add a bcb to lru list middle
 *
 * return        : void
 * thread_p (in) : thread entry
 * bcb (in)      : bcb added to middle
 * lru_list (in) : lru list
 */
STATIC_INLINE void
pgbuf_lru_add_bcb_to_middle (THREAD_ENTRY * thread_p, PGBUF_BCB * bcb, PGBUF_LRU_LIST * lru_list)
{
  /* is lru 1 zone empty? */
  if (lru_list->bottom_1 == NULL)
    {
      /* yes, zone 1 is empty */
      /* is list empty? */
      if (lru_list->top == NULL)
	{
	  /* yes, list is empty. set top and bottom to this bcb. */
	  assert (lru_list->bottom == NULL);
	  lru_list->top = bcb;
	  lru_list->bottom = bcb;

	  /* null prev/next links */
	  bcb->prev_BCB = NULL;
	  bcb->next_BCB = NULL;
	}
      else
	{
	  /* no. we should add the bcb before top. */
	  assert (pgbuf_bcb_get_zone (lru_list->top) != PGBUF_LRU_1_ZONE);
	  assert (lru_list->bottom != NULL);

	  /* link current top with new bcb */
	  lru_list->top->prev_BCB = bcb;
	  bcb->next_BCB = lru_list->top;

	  /* no previous bcb's */
	  bcb->prev_BCB = NULL;

	  /* update top */
	  lru_list->top = bcb;
	}
    }
  else
    {
      /* no, zone 1 is not empty */
      PGBUF_BCB *bcb_next = lru_list->bottom_1->next_BCB;

      assert (lru_list->top != NULL);
      assert (lru_list->bottom != NULL);

      /* insert after middle */
      lru_list->bottom_1->next_BCB = bcb;
      bcb->prev_BCB = lru_list->bottom_1;

      /* and before bcb_next */
      bcb->next_BCB = bcb_next;
      /* are zones 2/3 empty? */
      if (bcb_next == NULL)
	{
	  /* yes. */
	  /* middle must be also bottom */
	  assert (lru_list->bottom == lru_list->bottom_1);

	  /* update bottom */
	  lru_list->bottom = bcb;
	}
      else
	{
	  bcb_next->prev_BCB = bcb;
	}
    }
  if (lru_list->bottom_2 == NULL)
    {
      assert (lru_list->count_lru2 == 0);
      lru_list->bottom_2 = bcb;
    }

  /* save and increment list tick */
  if (++lru_list->tick_list >= DB_INT32_MAX)
    {
      lru_list->tick_list = 0;
    }

  pgbuf_bcb_change_zone (thread_p, bcb, lru_list->index, PGBUF_LRU_2_ZONE);
}

/*
 * pgbuf_lru_add_bcb_to_bottom () - add a bcb to lru list bottom
 *
 * return        : void
 * thread_p (in) : thread entry
 * bcb (in)      : bcb added to bottom
 * lru_list (in) : lru list
 */
STATIC_INLINE void
pgbuf_lru_add_bcb_to_bottom (THREAD_ENTRY * thread_p, PGBUF_BCB * bcb, PGBUF_LRU_LIST * lru_list)
{
  /* is list empty? */
  if (lru_list->bottom == NULL)
    {
      /* yes, list is empty. top must be NULL */
      assert (lru_list->top == NULL);

      /* update bottom and top */
      lru_list->bottom = bcb;
      lru_list->top = bcb;
      bcb->prev_BCB = NULL;
      bcb->next_BCB = NULL;

      /* get tick_lru3 */
      bcb->tick_lru3 = lru_list->tick_lru3 - 1;
    }
  else
    {
      /* no, list is not empty. added after current bottom. */
      lru_list->bottom->next_BCB = bcb;
      bcb->prev_BCB = lru_list->bottom;
      bcb->next_BCB = NULL;

      /* set tick_lru3 smaller that current bottom's */
      bcb->tick_lru3 =
	PGBUF_IS_BCB_IN_LRU_VICTIM_ZONE (lru_list->bottom) ? lru_list->bottom->tick_lru3 - 1 : lru_list->tick_lru3 - 1;

      /* update bottom */
      lru_list->bottom = bcb;
    }
  /* make sure tick_lru3 is not negative */
  if (bcb->tick_lru3 < 0)
    {
      bcb->tick_lru3 += DB_INT32_MAX;
    }

  pgbuf_bcb_change_zone (thread_p, bcb, lru_list->index, PGBUF_LRU_3_ZONE);
}

/*
 * pgbuf_lru_adjust_zone1 () - adjust zone 1 of lru list
 *
 * return        : void
 * thread_p (in) : thread entry
 * lru_list (in) : lru list
 * min_one (in)  : true to stop to at least one entry.
 */
STATIC_INLINE void
pgbuf_lru_adjust_zone1 (THREAD_ENTRY * thread_p, PGBUF_LRU_LIST * lru_list, bool min_one)
{
  int threshold;
  PGBUF_BCB *bcb_bottom;

  threshold = lru_list->threshold_lru1;
  if (min_one)
    {
      threshold = MAX (1, threshold);
    }
  if (threshold >= lru_list->count_lru1)
    {
      /* no adjustments can be made */
      return;
    }

  assert (lru_list->count_lru1 > 0);
  assert (lru_list->bottom_1 != NULL);

  /* change bcb zones from 1 to 2 until lru 1 zone count is down to zone 1 desired threshold.
   * note: if zone 1 desired threshold is bigger, its bottom is not moved. */
  if (lru_list->bottom_2 == NULL)
    {
      /* bottom 1 will become bottom 2. */
      lru_list->bottom_2 = lru_list->bottom_1;
    }

  for (bcb_bottom = lru_list->bottom_1; threshold < lru_list->count_lru1; bcb_bottom = bcb_bottom->prev_BCB)
    {
      pgbuf_bcb_change_zone (thread_p, bcb_bottom, lru_list->index, PGBUF_LRU_2_ZONE);
    }

  /* update bottom of lru 1 */
  if (lru_list->count_lru1 == 0)
    {
      lru_list->bottom_1 = NULL;
    }
  else
    {
      assert (bcb_bottom != NULL && pgbuf_bcb_get_zone (bcb_bottom) == PGBUF_LRU_1_ZONE);
      lru_list->bottom_1 = bcb_bottom;
    }
}

/*
 * pgbuf_lru_adjust_zone2 () - adjust zone 2 of lru list based on desired threshold.
 *
 * return        : void
 * thread_p (in) : thread entry
 * lru_list (in) : lru list
 * min_one (in)  : true to stop to at least one entry.
 */
STATIC_INLINE void
pgbuf_lru_adjust_zone2 (THREAD_ENTRY * thread_p, PGBUF_LRU_LIST * lru_list, bool min_one)
{
  PGBUF_BCB *bcb_bottom;
  PGBUF_BCB *bcb_prev;
  int threshold;

  threshold = lru_list->threshold_lru2;
  if (min_one)
    {
      threshold = MAX (1, threshold);
    }
  if (threshold >= lru_list->count_lru2)
    {
      /* no adjustments can be made */
      return;
    }

  assert (lru_list->count_lru2 > 0);
  assert (lru_list->bottom_2 != NULL);
  assert (pgbuf_bcb_get_zone (lru_list->bottom_2) == PGBUF_LRU_2_ZONE);

  /* change bcb zones from 2 to 3 until lru 2 zone count is down to zone 2 desired threshold. */
  for (bcb_bottom = lru_list->bottom_2; threshold < lru_list->count_lru2; bcb_bottom = bcb_prev)
    {
      /* save prev BCB in case this is removed from list */
      bcb_prev = bcb_bottom->prev_BCB;
      assert (bcb_bottom != NULL && pgbuf_bcb_get_zone (bcb_bottom) == PGBUF_LRU_2_ZONE);
      pgbuf_lru_fall_bcb_to_zone_3 (thread_p, bcb_bottom, lru_list);
    }
  /* update bottom of lru 2 */
  if (lru_list->count_lru2 == 0)
    {
      lru_list->bottom_2 = NULL;
    }
  else
    {
      assert (bcb_bottom != NULL && pgbuf_bcb_get_zone (bcb_bottom) == PGBUF_LRU_2_ZONE);
      lru_list->bottom_2 = bcb_bottom;
    }
}

/*
 * pgbuf_lru_adjust_zones () - adjust the middle of lru list and update bcb zones
 *
 * return        : void
 * thread_p (in) : thread entry
 * lru_list (in) : lru list
 * min_one (in)  : true to keep at least one entry in 1&2 zones.
 */
STATIC_INLINE void
pgbuf_lru_adjust_zones (THREAD_ENTRY * thread_p, PGBUF_LRU_LIST * lru_list, bool min_one)
{
  PGBUF_BCB *bcb_bottom;
  PGBUF_BCB *bcb_prev;
  int threshold;

  /* first adjust zone 1 & 2 and convert to zone 3. then we'll adjust zone 1 (and convert to 2) */
  threshold = lru_list->threshold_lru1 + lru_list->threshold_lru2;
  if (min_one)
    {
      threshold = MAX (1, threshold);
    }
  if (threshold >= PGBUF_LRU_ZONE_ONE_TWO_COUNT (lru_list))
    {
      /* just try to adjust zone 1. */
      pgbuf_lru_adjust_zone1 (thread_p, lru_list, min_one);
      return;
    }

  assert (PGBUF_LRU_ZONE_ONE_TWO_COUNT (lru_list) > 0);
  assert (lru_list->bottom_1 != NULL || lru_list->bottom_2 != NULL);

  for (bcb_bottom = lru_list->bottom_2 != NULL ? lru_list->bottom_2 : lru_list->bottom_1;
       threshold < PGBUF_LRU_ZONE_ONE_TWO_COUNT (lru_list); bcb_bottom = bcb_prev)
    {
      /* save prev BCB in case this is removed from list */
      bcb_prev = bcb_bottom->prev_BCB;

      assert (bcb_bottom != NULL && pgbuf_bcb_get_zone (bcb_bottom) != PGBUF_LRU_3_ZONE);

      pgbuf_lru_fall_bcb_to_zone_3 (thread_p, bcb_bottom, lru_list);
    }

  if (lru_list->count_lru2 == 0)
    {
      lru_list->bottom_2 = NULL;
      if (lru_list->count_lru1 == 0)
	{
	  lru_list->bottom_1 = NULL;
	}
      else
	{
	  assert (bcb_bottom != NULL && pgbuf_bcb_get_zone (bcb_bottom) == PGBUF_LRU_1_ZONE);
	  lru_list->bottom_1 = bcb_bottom;
	}
    }
  else
    {
      assert (bcb_bottom != NULL && pgbuf_bcb_get_zone (bcb_bottom) == PGBUF_LRU_2_ZONE);
      lru_list->bottom_2 = bcb_bottom;
    }

  pgbuf_lru_sanity_check (lru_list);

  pgbuf_lru_adjust_zone1 (thread_p, lru_list, min_one);
}

/*
 * pgbuf_lru_fall_bcb_to_zone_3 () - bcb falls to zone 3 of lru list
 *
 * return        : void
 * thread_p (in) : thread entry
 * bcb (in)      : bcb in lru list
 * lru_list (in) : lru list
 */
STATIC_INLINE void
pgbuf_lru_fall_bcb_to_zone_3 (THREAD_ENTRY * thread_p, PGBUF_BCB * bcb, PGBUF_LRU_LIST * lru_list)
{
  assert (pgbuf_bcb_get_zone (bcb) == PGBUF_LRU_1_ZONE || pgbuf_bcb_get_zone (bcb) == PGBUF_LRU_2_ZONE);

#if defined (SERVER_MODE)
  /* can we assign this directly as victim? */

  if (pgbuf_is_bcb_victimizable (bcb, false) && pgbuf_is_any_thread_waiting_for_direct_victim ())
    {
      if (pgbuf_bcb_is_to_vacuum (bcb))
	{
	  if (perfmon_is_perf_tracking_and_active (PERFMON_ACTIVATION_FLAG_PB_VICTIMIZATION))
	    {
	      perfmon_inc_stat (thread_p, PSTAT_PB_VICTIM_ASSIGN_DIRECT_ADJUST_TO_VACUUM);
	    }
	  /* fall through */
	}
      else
	{
	  /* we first need mutex on bcb. however, we'd normally first get mutex on bcb and then on list. since we don't
	   * want to over complicate things, just try a conditional lock on mutex. if it fails, we'll just give up
	   * assigning the bcb directly as victim */
	  if (PGBUF_BCB_TRYLOCK (bcb) == 0)
	    {
	      VPID vpid_copy = bcb->vpid;
	      if (pgbuf_is_bcb_victimizable (bcb, true) && pgbuf_assign_direct_victim (thread_p, bcb))
		{
		  if (perfmon_is_perf_tracking_and_active (PERFMON_ACTIVATION_FLAG_PB_VICTIMIZATION))
		    {
		      perfmon_inc_stat (thread_p, PSTAT_PB_VICTIM_ASSIGN_DIRECT_ADJUST);
		    }

		  /* since bcb is going to be removed from list and I have both lru and bcb mutex, why not do it now. */
		  pgbuf_remove_from_lru_list (thread_p, bcb, lru_list);

		  PGBUF_BCB_UNLOCK (bcb);

		  pgbuf_add_vpid_to_aout_list (thread_p, &vpid_copy, lru_list->index);
		  return;
		}
	      /* not assigned. unlock bcb mutex and fall through */
	      PGBUF_BCB_UNLOCK (bcb);
	    }
	  else
	    {
	      /* don't try too hard. it will be victimized eventually. */
	      /* fall through */
	    }
	}
    }
  /* not assigned directly */
#endif /* SERVER_MODE */

  /* tick_lru3 */
  bcb->tick_lru3 = lru_list->tick_lru3;
  if (++lru_list->tick_lru3 >= DB_INT32_MAX)
    {
      lru_list->tick_lru3 = 0;
    }
  pgbuf_bcb_change_zone (thread_p, bcb, lru_list->index, PGBUF_LRU_3_ZONE);
}

/*
 * pgbuf_lru_boost_bcb () - boost bcb.
 *
 * return        : void
 * thread_p (in) : thread entry
 * bcb (in)      : bcb to move to top
 */
static void
pgbuf_lru_boost_bcb (THREAD_ENTRY * thread_p, PGBUF_BCB * bcb)
{
  PGBUF_LRU_LIST *lru_list;
  PGBUF_ZONE zone = pgbuf_bcb_get_zone (bcb);
  bool is_private;

  assert (PGBUF_IS_BCB_IN_LRU (bcb));

  lru_list = pgbuf_lru_list_from_bcb (bcb);
  is_private = PGBUF_IS_PRIVATE_LRU_INDEX (lru_list->index);

  /* rules to boosting bcb's in lru lists (also see code in pgbuf_unlatch_bcb_upon_unfix):
   * 1. never boost bcb's in zone 1. this is usually the hottest part of the lists and should have a big hit ratio.
   *    we'd like to avoid locking list mutex and making changes, these bcb's are in no danger of being victimized,
   *    so we just don't move them.
   * 2. avoid boosting new and cold bcb's. a bcb can be fixed/unfixed several times and still be cold. many operations
   *    will fix a page at least twice (once to read and once to write), and we'd like to avoid boosting the bcb on
   *    second unfix. we do have a trick to detect such cases. we keep the list tick whenever new bcb's are inserted
   *    to zones 1 and 2. if a page is quickly fixed several times, its "age" is really small (age being the difference
   *    between the bcb's saved tick and current list tick), and we don't boost it. it should be unfixed again after
   *    aging a little before being boosted to top.
   * 3. always boost from third zone, since these are decently old.
   *
   * note: early outs should be handled in pgbuf_unlatch_bcb_upon_unfix.
   */

  assert (zone != PGBUF_LRU_1_ZONE);

  /* we'll boost. collect stats */
  if (zone == PGBUF_LRU_2_ZONE)
    {
      perfmon_inc_stat (thread_p, is_private ? PSTAT_PB_UNFIX_LRU_TWO_PRV_TO_TOP : PSTAT_PB_UNFIX_LRU_TWO_SHR_TO_TOP);
    }
  else
    {
      assert (zone == PGBUF_LRU_3_ZONE);
      perfmon_inc_stat (thread_p,
			is_private ? PSTAT_PB_UNFIX_LRU_THREE_PRV_TO_TOP : PSTAT_PB_UNFIX_LRU_THREE_SHR_TO_TOP);
    }

  /* lock list */
  pthread_mutex_lock (&lru_list->mutex);

  /* remove from current position */
  pgbuf_remove_from_lru_list (thread_p, bcb, lru_list);

  /* add to top */
  pgbuf_lru_add_bcb_to_top (thread_p, bcb, lru_list);

  /* since we added a new bcb to lru 1, we should adjust zones */
  if (zone == PGBUF_LRU_2_ZONE)
    {
      /* adjust only zone 1 */
      pgbuf_lru_adjust_zone1 (thread_p, lru_list, true);
    }
  else
    {
      pgbuf_lru_adjust_zones (thread_p, lru_list, true);
    }

  pgbuf_lru_sanity_check (lru_list);

  /* unlock list */
  pthread_mutex_unlock (&lru_list->mutex);
}

/*
 * pgbuf_lru_add_new_bcb_to_top () - add a new bcb to top of lru list
 *
 * return        : void
 * thread_p (in) : thread entry
 * bcb (in)      : new bcb
 * lru_idx (in)  : lru list index
 */
STATIC_INLINE void
pgbuf_lru_add_new_bcb_to_top (THREAD_ENTRY * thread_p, PGBUF_BCB * bcb, int lru_idx)
{
  PGBUF_LRU_LIST *lru_list;

  /* this is not meant for changes in this list */
  assert (!PGBUF_IS_BCB_IN_LRU (bcb));

  /* lock list */
  lru_list = &pgbuf_Pool.buf_LRU_list[lru_idx];
  pthread_mutex_lock (&lru_list->mutex);

  /* add to top */
  /* this is new bcb, we must init its list tick */
  bcb->tick_lru_list = lru_list->tick_list;
  pgbuf_lru_add_bcb_to_top (thread_p, bcb, lru_list);

  pgbuf_lru_sanity_check (lru_list);

  /* since we added a new bcb to lru 1, we should adjust zones */
  pgbuf_lru_adjust_zones (thread_p, lru_list, true);

  pgbuf_lru_sanity_check (lru_list);

  /* unlock list */
  pthread_mutex_unlock (&lru_list->mutex);
}

/*
 * pgbuf_lru_add_new_bcb_to_middle () - add a new bcb to middle of lru list
 *
 * return        : void
 * thread_p (in) : thread entry
 * bcb (in)      : new bcb
 * lru_idx (in)  : lru list index
 */
STATIC_INLINE void
pgbuf_lru_add_new_bcb_to_middle (THREAD_ENTRY * thread_p, PGBUF_BCB * bcb, int lru_idx)
{
  PGBUF_LRU_LIST *lru_list;

  /* this is not meant for changes in this list */
  assert (!PGBUF_IS_BCB_IN_LRU (bcb));

  lru_list = &pgbuf_Pool.buf_LRU_list[lru_idx];
  pthread_mutex_lock (&lru_list->mutex);

  bcb->tick_lru_list = lru_list->tick_list;
  pgbuf_lru_add_bcb_to_middle (thread_p, bcb, lru_list);

  pgbuf_lru_sanity_check (lru_list);

  /* adjust zone 2 */
  pgbuf_lru_adjust_zone2 (thread_p, lru_list, true);

  pgbuf_lru_sanity_check (lru_list);

  pthread_mutex_unlock (&lru_list->mutex);
}

/*
 * pgbuf_lru_add_new_bcb_to_bottom () - add a new bcb to bottom of lru list
 *
 * return        : void
 * thread_p (in) : thread entry
 * bcb (in)      : new bcb
 * lru_idx (in)  : lru list index
 */
STATIC_INLINE void
pgbuf_lru_add_new_bcb_to_bottom (THREAD_ENTRY * thread_p, PGBUF_BCB * bcb, int lru_idx)
{
  PGBUF_LRU_LIST *lru_list;

  /* this is not meant for changes in this list */
  assert (!PGBUF_IS_BCB_IN_LRU (bcb));

  if (pgbuf_is_bcb_victimizable (bcb, true) && pgbuf_assign_direct_victim (thread_p, bcb))
    {
      /* assigned directly */
      /* TODO: add stat. this is actually not used for now. */
      return;
    }

  /* lock list */
  lru_list = &pgbuf_Pool.buf_LRU_list[lru_idx];
  pthread_mutex_lock (&lru_list->mutex);

  bcb->tick_lru_list = lru_list->tick_list;
  pgbuf_lru_add_bcb_to_bottom (thread_p, bcb, lru_list);

  pgbuf_lru_sanity_check (lru_list);

  /* unlock list */
  pthread_mutex_unlock (&lru_list->mutex);
}

/*
 * pgbuf_lru_remove_bcb () - remove bcb from lru list
 *
 * return        : void
 * thread_p (in) : thread entry
 * bcb (in)      : bcb
 */
STATIC_INLINE void
pgbuf_lru_remove_bcb (THREAD_ENTRY * thread_p, PGBUF_BCB * bcb)
{
  PGBUF_LRU_LIST *lru_list;

  assert (PGBUF_IS_BCB_IN_LRU (bcb));

  lru_list = pgbuf_lru_list_from_bcb (bcb);

  /* lock list */
  pthread_mutex_lock (&lru_list->mutex);

  /* remove bcb from list */
  pgbuf_remove_from_lru_list (thread_p, bcb, lru_list);

  pgbuf_lru_sanity_check (lru_list);

  /* unlock list */
  pthread_mutex_unlock (&lru_list->mutex);
}

/*
 * pgbuf_lru_move_from_private_to_shared () - move a bcb from private list to shared list
 *
 * return        : void
 * thread_p (in) : thread entry
 * bcb (in)      : private list bcb
 */
static void
pgbuf_lru_move_from_private_to_shared (THREAD_ENTRY * thread_p, PGBUF_BCB * bcb)
{
  /* bcb must be in private list */
  assert (PGBUF_IS_PRIVATE_LRU_INDEX (pgbuf_bcb_get_lru_index (bcb)));

  /* note: from statistics analysis, moves from private to shared are very rare, so we don't inline the function */

  /* remove bcb from its lru list */
  pgbuf_lru_remove_bcb (thread_p, bcb);

  /* add bcb to middle of shared list */
  pgbuf_lru_add_new_bcb_to_middle (thread_p, bcb, pgbuf_get_shared_lru_index_for_add ());

  pgbuf_bcb_register_hit_for_lru (bcb);
}

/*
 * pgbuf_remove_from_lru_list () - Remove a BCB from the LRU list
 * return : void
 * bufptr (in) : BCB
 * lru_list (in) : LRU list to which BVB currently belongs to
 *
 *  Note: The caller MUST hold the LRU list mutex.
 */
STATIC_INLINE void
pgbuf_remove_from_lru_list (THREAD_ENTRY * thread_p, PGBUF_BCB * bufptr, PGBUF_LRU_LIST * lru_list)
{
  PGBUF_BCB *bcb_prev = NULL;

  if (lru_list->top == bufptr)
    {
      lru_list->top = bufptr->next_BCB;
    }

  if (lru_list->bottom == bufptr)
    {
      lru_list->bottom = bufptr->prev_BCB;
    }

  if (lru_list->bottom_1 == bufptr)
    {
      lru_list->bottom_1 = bufptr->prev_BCB;
    }

  if (lru_list->bottom_2 == bufptr)
    {
      if (bufptr->prev_BCB != NULL && pgbuf_bcb_get_zone (bufptr->prev_BCB) == PGBUF_LRU_2_ZONE)
	{
	  lru_list->bottom_2 = bufptr->prev_BCB;
	}
      else
	{
	  assert (lru_list->count_lru2 == 1);
	  lru_list->bottom_2 = NULL;
	}
    }

  if (bufptr->next_BCB != NULL)
    {
      (bufptr->next_BCB)->prev_BCB = bufptr->prev_BCB;
    }

  bcb_prev = bufptr->prev_BCB;
  if (bcb_prev != NULL)
    {
      bcb_prev->next_BCB = bufptr->next_BCB;
    }

  bufptr->prev_BCB = NULL;
  bufptr->next_BCB = NULL;

  /* we need to update the victim hint now, since bcb has been disconnected from list.
   * pgbuf_lru_remove_victim_candidate will not which is the previous BCB. we cannot change the hint before
   * disconnecting the bcb from list, we need to be sure no one else sets the hint to this bcb. */
  pgbuf_lru_advance_victim_hint (thread_p, lru_list, bufptr, bcb_prev, false);

  /* update zone */
  pgbuf_bcb_change_zone (thread_p, bufptr, 0, PGBUF_VOID_ZONE);
}

/*
 * pgbuf_move_bcb_to_bottom_lru () - move a bcb to the bottom of its lru (or other lru if it is in the void zone).
 *
 * return        : void
 * thread_p (in) : thread entry
 * bcb (in)      : bcb
 */
static void
pgbuf_move_bcb_to_bottom_lru (THREAD_ENTRY * thread_p, PGBUF_BCB * bcb)
{
  PGBUF_ZONE zone = pgbuf_bcb_get_zone (bcb);
  int lru_idx;
  PGBUF_LRU_LIST *lru_list;

  pgbuf_bcb_update_flags (thread_p, bcb, 0, PGBUF_BCB_MOVE_TO_LRU_BOTTOM_FLAG);

  if (zone == PGBUF_VOID_ZONE)
    {
      /* move to the bottom of a lru list so it can be found by flush thread */
      if (PGBUF_THREAD_HAS_PRIVATE_LRU (thread_p))
	{
	  lru_idx = PGBUF_LRU_INDEX_FROM_PRIVATE (PGBUF_PRIVATE_LRU_FROM_THREAD (thread_p));
	}
      else
	{
	  lru_idx = pgbuf_get_shared_lru_index_for_add ();
	}
      pgbuf_lru_add_new_bcb_to_bottom (thread_p, bcb, lru_idx);
    }
  else if (zone & PGBUF_LRU_ZONE_MASK)
    {
      lru_idx = pgbuf_bcb_get_lru_index (bcb);
      lru_list = PGBUF_GET_LRU_LIST (lru_idx);
      if (bcb == lru_list->bottom)
	{
	  /* early out */
	  return;
	}
      pthread_mutex_lock (&lru_list->mutex);
      pgbuf_remove_from_lru_list (thread_p, bcb, lru_list);
      pgbuf_lru_add_bcb_to_bottom (thread_p, bcb, lru_list);
      pthread_mutex_unlock (&lru_list->mutex);
    }
  else
    {
      assert (false);
    }
}

/*
 * pgbuf_add_vpid_to_aout_list () - add VPID to Aout list
 * return : void
 * thread_p (in) :
 * vpid (in) :
 * lru_idx (in) : LRU index in which the VPID had been
 */
STATIC_INLINE void
pgbuf_add_vpid_to_aout_list (THREAD_ENTRY * thread_p, const VPID * vpid, const int lru_idx)
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

  assert (!VPID_ISNULL (vpid));

  list = &pgbuf_Pool.buf_AOUT_list;

  rv = pthread_mutex_lock (&pgbuf_Pool.buf_AOUT_list.Aout_mutex);

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
  aout_buf->lru_idx = lru_idx;
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
 * pgbuf_remove_vpid_from_aout_list () - Search for VPID in Aout and remove it from the queue
 * return : identifier of list from which was removed:
 *	    0 and positive: LRU list
 *	    PGBUF_AOUT_NOT_FOUND: not found in Aout list
 * thread_p (in) :
 * vpid (in) :
 */
static int
pgbuf_remove_vpid_from_aout_list (THREAD_ENTRY * thread_p, const VPID * vpid)
{
#if defined(SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */
  PGBUF_AOUT_BUF *aout_buf;
  int hash_idx;
  int aout_list_id = PGBUF_AOUT_NOT_FOUND;

  if (pgbuf_Pool.buf_AOUT_list.max_count <= 0)
    {
      /* Aout list not used */
      return PGBUF_AOUT_NOT_FOUND;
    }

  hash_idx = AOUT_HASH_IDX (vpid, (&pgbuf_Pool.buf_AOUT_list));

  rv = pthread_mutex_lock (&pgbuf_Pool.buf_AOUT_list.Aout_mutex);
  /* Search the vpid in the hash table */
  aout_buf = (PGBUF_AOUT_BUF *) mht_get (pgbuf_Pool.buf_AOUT_list.aout_buf_ht[hash_idx], vpid);
  if (aout_buf == NULL)
    {
      /* Not there, just return */
      pthread_mutex_unlock (&pgbuf_Pool.buf_AOUT_list.Aout_mutex);
      return PGBUF_AOUT_NOT_FOUND;
    }

  /* We can assume that aout_buf is what we're looking for if it still has the same VPID as before acquiring the mutex.
   * The reason for this is that nobody can change it while we're holding the mutex. Any changes must be visible before
   * we acquire this mutex */
  aout_list_id = aout_buf->lru_idx;
  if (aout_buf == pgbuf_Pool.buf_AOUT_list.Aout_bottom)
    {
      pgbuf_Pool.buf_AOUT_list.Aout_bottom = pgbuf_Pool.buf_AOUT_list.Aout_bottom->prev;

      if (pgbuf_Pool.buf_AOUT_list.Aout_bottom != NULL)
	{
	  pgbuf_Pool.buf_AOUT_list.Aout_bottom->next = NULL;
	}
      aout_buf->prev = NULL;
    }

  if (aout_buf == pgbuf_Pool.buf_AOUT_list.Aout_top)
    {
      pgbuf_Pool.buf_AOUT_list.Aout_top = pgbuf_Pool.buf_AOUT_list.Aout_top->next;

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
  aout_buf->lru_idx = PGBUF_AOUT_NOT_FOUND;
  aout_buf->next = NULL;
  aout_buf->prev = NULL;

  aout_buf->next = pgbuf_Pool.buf_AOUT_list.Aout_free;
  pgbuf_Pool.buf_AOUT_list.Aout_free = aout_buf;

  pthread_mutex_unlock (&pgbuf_Pool.buf_AOUT_list.Aout_mutex);

  return aout_list_id;
}

/*
 * pgbuf_remove_private_from_aout_list () - Search for VPID in Aout and removes all VPIDs having a specific LRU idx
 *
 * return : number of VPIDs removed
 * lru_idx (in) :
 */
static int
pgbuf_remove_private_from_aout_list (const int lru_idx)
{
  PGBUF_AOUT_BUF *aout_buf;
  PGBUF_AOUT_BUF *aout_buf_next;
  int hash_idx;
  int cnt_removed = 0;

  if (pgbuf_Pool.buf_AOUT_list.max_count <= 0)
    {
      /* Aout list not used */
      return cnt_removed;
    }

  pthread_mutex_lock (&pgbuf_Pool.buf_AOUT_list.Aout_mutex);
  aout_buf = pgbuf_Pool.buf_AOUT_list.Aout_top;
  while (aout_buf != NULL)
    {
      if (aout_buf->lru_idx != lru_idx)
	{
	  aout_buf = aout_buf->next;
	  continue;
	}

      aout_buf_next = aout_buf->next;

      /* remove this item */
      if (aout_buf == pgbuf_Pool.buf_AOUT_list.Aout_bottom)
	{
	  pgbuf_Pool.buf_AOUT_list.Aout_bottom = pgbuf_Pool.buf_AOUT_list.Aout_bottom->prev;

	  if (pgbuf_Pool.buf_AOUT_list.Aout_bottom != NULL)
	    {
	      pgbuf_Pool.buf_AOUT_list.Aout_bottom->next = NULL;
	    }
	  aout_buf->prev = NULL;
	}

      if (aout_buf == pgbuf_Pool.buf_AOUT_list.Aout_top)
	{
	  pgbuf_Pool.buf_AOUT_list.Aout_top = pgbuf_Pool.buf_AOUT_list.Aout_top->next;

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

      hash_idx = AOUT_HASH_IDX (&aout_buf->vpid, (&pgbuf_Pool.buf_AOUT_list));
      mht_rem (pgbuf_Pool.buf_AOUT_list.aout_buf_ht[hash_idx], &aout_buf->vpid, NULL, NULL);

      /* add to free list */
      VPID_SET_NULL (&aout_buf->vpid);
      aout_buf->lru_idx = PGBUF_AOUT_NOT_FOUND;
      aout_buf->next = NULL;
      aout_buf->prev = NULL;

      aout_buf->next = pgbuf_Pool.buf_AOUT_list.Aout_free;
      pgbuf_Pool.buf_AOUT_list.Aout_free = aout_buf;

      aout_buf = aout_buf_next;
      cnt_removed++;
    }

  pthread_mutex_unlock (&pgbuf_Pool.buf_AOUT_list.Aout_mutex);

  return cnt_removed;
}

/*
 * pgbuf_bcb_flush_with_wal () - write a buffer page to disk.
 *
 * return                    : error code
 * thread_p (in)             : thread entry
 * bufptr (in)               : bcb
 * is_page_flush_thread (in) : true if caller is page flush thread. false otherwise.
 * is_bcb_locked (out)       : output whether bcb remains locked or not.
 */
STATIC_INLINE int
pgbuf_bcb_flush_with_wal (THREAD_ENTRY * thread_p, PGBUF_BCB * bufptr, bool is_page_flush_thread, bool * is_bcb_locked)
{
  char page_buf[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT];
  FILEIO_PAGE *iopage = NULL;
  PAGE_PTR pgptr = NULL;
  LOG_LSA oldest_unflush_lsa;
  int error = NO_ERROR;
#if defined(ENABLE_SYSTEMTAP)
  QUERY_ID query_id = NULL_QUERY_ID;
  bool monitored = false;
#endif /* ENABLE_SYSTEMTAP */
  bool was_dirty = false, uses_dwb;
  DWB_SLOT *dwb_slot = NULL;
  LOG_LSA lsa;
  FILEIO_WRITE_MODE write_mode;
  bool is_temp = pgbuf_is_temporary_volume (bufptr->vpid.volid);
  TDE_ALGORITHM tde_algo = TDE_ALGORITHM_NONE;
  int tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  PGBUF_STATUS *show_status = &pgbuf_Pool.show_status[tran_index];


  PGBUF_BCB_CHECK_OWN (bufptr);

  /* the caller is holding bufptr->mutex */
  *is_bcb_locked = true;

  assert (bufptr->latch_mode == PGBUF_NO_LATCH || bufptr->latch_mode == PGBUF_LATCH_READ
	  || bufptr->latch_mode == PGBUF_LATCH_WRITE);
#if !defined (NDEBUG) && defined (SERVER_MODE)
  if (bufptr->latch_mode == PGBUF_LATCH_WRITE)
    {
      /* I must be the owner, or else we'll be in trouble. */
      int thread_index = thread_p->index;
      PGBUF_HOLDER_ANCHOR *thrd_holder_info = &pgbuf_Pool.thrd_holder_info[thread_index];
      PGBUF_HOLDER *holder = NULL;

      /* Search for bufptr in current thread holder list. */
      for (holder = thrd_holder_info->thrd_hold_list; holder != NULL; holder = holder->thrd_link)
	{
	  if (holder->bufptr == bufptr)
	    {
	      break;
	    }
	}
      /* Safe guard: I must be the bufptr holder. */
      assert (holder != NULL);
    }
#endif /* !NDEBUG */

  /* how this works:
   *
   * caller should already have bcb locked. we don't do checks of opportunity or correctness here (that's up to the
   * caller).
   *
   * we copy the page and save oldest_unflush_lsa and then we try to write the page to disk. if writing fails, we
   * "revert" changes (restore dirty flag and oldest_unflush_lsa).
   *
   * if successful, we choose one of the paths:
   * 1. send the page to post-flush to process it and assign it directly (if this is page flush thread and victimization
   *    system is stressed).
   * 2. lock bcb again, clear is flushing status, wake up of threads waiting for flush and return.
   */

  if (pgbuf_check_bcb_page_vpid (bufptr, false) != true)
    {
      assert (false);
      return ER_FAILED;
    }

  was_dirty = pgbuf_bcb_mark_is_flushing (thread_p, bufptr);

  uses_dwb = dwb_is_created () && !is_temp;

start_copy_page:
  iopage = (FILEIO_PAGE *) PTR_ALIGN (page_buf, MAX_ALIGNMENT);
  CAST_BFPTR_TO_PGPTR (pgptr, bufptr);
  tde_algo = pgbuf_get_tde_algorithm (pgptr);
  if (tde_algo != TDE_ALGORITHM_NONE)
    {
      error = tde_encrypt_data_page (&bufptr->iopage_buffer->iopage, tde_algo, is_temp, iopage);
      if (error != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return error;
	}
    }
  else
    {
      memcpy ((void *) iopage, (void *) (&bufptr->iopage_buffer->iopage), IO_PAGESIZE);
    }
  if (uses_dwb)
    {
      error = dwb_set_data_on_next_slot (thread_p, iopage, false, &dwb_slot);
      if (error != NO_ERROR)
	{
	  return error;
	}
      if (dwb_slot != NULL)
	{
	  iopage = NULL;
	  goto copy_unflushed_lsa;
	}
    }

copy_unflushed_lsa:
  LSA_COPY (&lsa, &(bufptr->iopage_buffer->iopage.prv.lsa));
  LSA_COPY (&oldest_unflush_lsa, &bufptr->oldest_unflush_lsa);
  LSA_SET_NULL (&bufptr->oldest_unflush_lsa);

  PGBUF_BCB_UNLOCK (bufptr);
  *is_bcb_locked = false;

  if (!LSA_ISNULL (&oldest_unflush_lsa))
    {
      /* confirm WAL protocol */
      /* force log record to disk */
      logpb_flush_log_for_wal (thread_p, &lsa);
    }
  else
    {
      /* if page was changed, the change was not logged. this is a rare case, but can happen. */
      if (!pgbuf_is_temporary_volume (bufptr->vpid.volid))
	{
	  er_log_debug (ARG_FILE_LINE, "flushing page %d|%d to disk without logging.\n", VPID_AS_ARGS (&bufptr->vpid));
	}
    }

#if defined(ENABLE_SYSTEMTAP)
  query_id = qmgr_get_current_query_id (thread_p);
  if (query_id != NULL_QUERY_ID)
    {
      monitored = true;
      CUBRID_IO_WRITE_START (query_id);
    }
#endif /* ENABLE_SYSTEMTAP */

  /* Activating/deactivating DWB while the server is alive, needs additional work. For now, we don't care about
   * this case, we can use it to test performance differences.
   */
  if (uses_dwb)
    {
      error = dwb_add_page (thread_p, iopage, &bufptr->vpid, &dwb_slot);
      if (error == NO_ERROR)
	{
	  if (dwb_slot == NULL)
	    {
	      /* DWB disabled meanwhile, try again without DWB. */
	      uses_dwb = false;
	      PGBUF_BCB_LOCK (bufptr);
	      *is_bcb_locked = true;
	      goto start_copy_page;
	    }
	}
    }
  else
    {
      show_status->num_pages_written++;

      /* Record number of writes in statistics */
      write_mode = (dwb_is_created () == true ? FILEIO_WRITE_NO_COMPENSATE_WRITE : FILEIO_WRITE_DEFAULT_WRITE);

      perfmon_inc_stat (thread_p, PSTAT_PB_NUM_IOWRITES);
      if (fileio_write (thread_p, fileio_get_volume_descriptor (bufptr->vpid.volid), iopage, bufptr->vpid.pageid,
			IO_PAGESIZE, write_mode) == NULL)
	{
	  error = ER_FAILED;
	}
    }

#if defined(ENABLE_SYSTEMTAP)
  if (monitored == true)
    {
      CUBRID_IO_WRITE_END (query_id, IO_PAGESIZE, (error != NO_ERROR));
    }
#endif /* ENABLE_SYSTEMTAP */

  if (error != NO_ERROR)
    {
      PGBUF_BCB_LOCK (bufptr);
      *is_bcb_locked = true;
      pgbuf_bcb_mark_was_not_flushed (thread_p, bufptr, was_dirty);
      LSA_COPY (&bufptr->oldest_unflush_lsa, &oldest_unflush_lsa);

#if defined (SERVER_MODE)
      if (bufptr->next_wait_thrd != NULL)
	{
	  pgbuf_wake_flush_waiters (thread_p, bufptr);
	}
#endif

      return ER_FAILED;
    }

  assert (bufptr->latch_mode != PGBUF_LATCH_FLUSH);

#if defined (SERVER_MODE)
  /* if the flush thread is under pressure, we'll move some of the workload to post-flush thread. */
  if (is_page_flush_thread && (pgbuf_Page_post_flush_daemon != NULL)
      && pgbuf_is_any_thread_waiting_for_direct_victim () && pgbuf_Pool.flushed_bcbs->produce (bufptr))
    {
      /* page buffer maintenance thread will try to assign this bcb directly as victim. */
      pgbuf_Page_post_flush_daemon->wakeup ();
      if (perfmon_is_perf_tracking_and_active (PERFMON_ACTIVATION_FLAG_PB_VICTIMIZATION))
	{
	  perfmon_inc_stat (thread_p, PSTAT_PB_FLUSH_SEND_DIRTY_TO_POST_FLUSH);
	}
    }
  else
#endif /* SERVER_MODE */
    {
      PGBUF_BCB_LOCK (bufptr);
      *is_bcb_locked = true;
      pgbuf_bcb_mark_was_flushed (thread_p, bufptr);

#if defined (SERVER_MODE)
      if (bufptr->next_wait_thrd != NULL)
	{
	  pgbuf_wake_flush_waiters (thread_p, bufptr);
	}
#endif
    }

  if (perfmon_is_perf_tracking_and_active (PERFMON_ACTIVATION_FLAG_PB_VICTIMIZATION))
    {
      perfmon_inc_stat (thread_p, PSTAT_PB_FLUSH_PAGE_FLUSHED);
    }

  return NO_ERROR;
}

/*
 * pgbuf_wake_flush_waiters () - wake up all threads waiting for flush
 *
 * return        : void
 * thread_p (in) : thread entry
 * bcb (in)      : flushed bcb
 */
static void
pgbuf_wake_flush_waiters (THREAD_ENTRY * thread_p, PGBUF_BCB * bcb)
{
#if defined (SERVER_MODE)
  THREAD_ENTRY *prev_waiter = NULL;
  THREAD_ENTRY *crt_waiter = NULL;
  THREAD_ENTRY *save_next_waiter = NULL;
  PERF_UTIME_TRACKER timetr;

  PERF_UTIME_TRACKER_START (thread_p, &timetr);

  PGBUF_BCB_CHECK_OWN (bcb);

  for (crt_waiter = bcb->next_wait_thrd; crt_waiter != NULL; crt_waiter = save_next_waiter)
    {
      save_next_waiter = crt_waiter->next_wait_thrd;

      if (crt_waiter->request_latch_mode == PGBUF_LATCH_FLUSH)
	{
	  /* wakeup and remove from list */
	  if (prev_waiter != NULL)
	    {
	      prev_waiter->next_wait_thrd = save_next_waiter;
	    }
	  else
	    {
	      bcb->next_wait_thrd = save_next_waiter;
	    }

	  crt_waiter->next_wait_thrd = NULL;
	  pgbuf_wakeup_uncond (crt_waiter);
	}
      else
	{
	  prev_waiter = crt_waiter;
	}
    }

  PERF_UTIME_TRACKER_TIME (thread_p, &timetr, PSTAT_PB_WAKE_FLUSH_WAITER);
#endif /* SERVER_MODE */
}

/*
 * pgbuf_is_exist_blocked_reader_writer () - checks whether there exists any blocked reader/writer
 *   return: if found, true, otherwise, false
 *   bufptr(in): pointer to buffer page
 */
STATIC_INLINE bool
pgbuf_is_exist_blocked_reader_writer (PGBUF_BCB * bufptr)
{
#if defined(SERVER_MODE)
  THREAD_ENTRY *thrd_entry;

  /* check whether there exists any blocked reader/writer */
  thrd_entry = bufptr->next_wait_thrd;
  while (thrd_entry != NULL)
    {
      if (thrd_entry->request_latch_mode == PGBUF_LATCH_READ || thrd_entry->request_latch_mode == PGBUF_LATCH_WRITE)
	{
	  return true;
	}

      thrd_entry = thrd_entry->next_wait_thrd;
    }
#endif /* SERVER_MODE */

  return false;
}

/*
 * pgbuf_get_check_page_validation_level -
 *   return:
 *
 */
STATIC_INLINE bool
pgbuf_get_check_page_validation_level (int page_validation_level)
{
#if !defined(NDEBUG)
  return prm_get_integer_value (PRM_ID_PB_DEBUG_PAGE_VALIDATION_LEVEL) >= page_validation_level;
#else /* NDEBUG */
  return false;
#endif /* NDEBUG */
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
 *       capabilities.
 */
DISK_ISVALID
pgbuf_is_valid_page (THREAD_ENTRY * thread_p, const VPID * vpid, bool no_error,
		     DISK_ISVALID (*fun) (const VPID * vpid, void *args), void *args)
{
  DISK_ISVALID valid;

  /* TODO: fix me */

  if (fileio_get_volume_label (vpid->volid, PEEK) == NULL || VPID_ISNULL (vpid))
    {
      assert (no_error);

      return DISK_INVALID;
    }

  /*valid = disk_isvalid_page (thread_p, vpid->volid, vpid->pageid); */
  valid = disk_is_page_sector_reserved_with_debug_crash (thread_p, vpid->volid, vpid->pageid, !no_error);
  if (valid != DISK_VALID || (fun != NULL && (valid = (*fun) (vpid, args)) != DISK_VALID))
    {
      if (valid != DISK_ERROR && !no_error)
	{
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_PB_BAD_PAGEID, 2, vpid->pageid,
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

  assert (pgptr != NULL);

  /* NOTE: Does not need to hold mutex since the page is fixed */
  for (bufid = 0; bufid < pgbuf_Pool.num_buffers; bufid++)
    {
      bufptr = PGBUF_FIND_BCB_PTR (bufid);
      PGBUF_BCB_LOCK (bufptr);

      if (((PAGE_PTR) (&(bufptr->iopage_buffer->iopage.page[0]))) == pgptr)
	{
	  if (bufptr->fcnt <= 0)
	    {
	      /* This situation must not be occurred. */
	      assert (false);
	      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_PB_UNFIXED_PAGEPTR, 3, pgptr, bufptr->vpid.pageid,
		      fileio_get_volume_label (bufptr->vpid.volid, PEEK));
	      PGBUF_BCB_UNLOCK (bufptr);

	      return false;
	    }
	  else
	    {
	      PGBUF_BCB_UNLOCK (bufptr);

	      return true;
	    }
	}
      else
	{
	  PGBUF_BCB_UNLOCK (bufptr);
	}
    }

  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_PB_UNKNOWN_PAGEPTR, 1, pgptr);

  assert (false);

  return false;
}

/*
 * pgbuf_check_page_type () - Check the page type is as expected. If it isn't an assert will be hit.
 *
 * return	 : True if the page type is as expected.
 * thread_p (in) : Thread entry.
 * pgptr (in)	 : Pointer to buffer page.
 * ptype (in)	 : Expected page type.
 */
bool
pgbuf_check_page_ptype (THREAD_ENTRY * thread_p, PAGE_PTR pgptr, PAGE_TYPE ptype)
{
  return pgbuf_check_page_ptype_internal (pgptr, ptype, false);
}

/*
 * pgbuf_check_page_type_no_error () - Return if the page type is the expected type given as argument. No assert is
 *				       hit if not.
 *
 * return	 : True if the page type is as expected.
 * thread_p (in) : Thread entry.
 * pgptr (in)	 : Pointer to buffer page.
 * ptype (in)	 : Expected page type.
 */
bool
pgbuf_check_page_type_no_error (THREAD_ENTRY * thread_p, PAGE_PTR pgptr, PAGE_TYPE ptype)
{
  return pgbuf_check_page_ptype_internal (pgptr, ptype, true);
}

/*
 * pgbuf_check_page_ptype_internal () -
 *   return: true/false
 *   bufptr(in): pointer to buffer page
 *   ptype(in): page type
 *
 * Note: Verify if the given page's ptype is valid.
 *       This function is used for debugging purposes.
 */
STATIC_INLINE bool
pgbuf_check_page_ptype_internal (PAGE_PTR pgptr, PAGE_TYPE ptype, bool no_error)
{
  PGBUF_BCB *bufptr;

  if (pgptr == NULL)
    {
      assert (false);
      return false;
    }

/* TODO - do not delete me */
#if defined(NDEBUG)
  if (log_is_in_crash_recovery ())
    {
      return true;
    }
#endif

  if (pgbuf_get_check_page_validation_level (PGBUF_DEBUG_PAGE_VALIDATION_ALL))
    {
      if (pgbuf_is_valid_page_ptr (pgptr) == false)
	{
	  return false;
	}
    }

  /* NOTE: Does not need to hold mutex since the page is fixed */

  CAST_PGPTR_TO_BFPTR (bufptr, pgptr);
  assert (!VPID_ISNULL (&bufptr->vpid));

  if (pgbuf_check_bcb_page_vpid (bufptr, false) == true)
    {
      if (bufptr->iopage_buffer->iopage.prv.ptype != PAGE_UNKNOWN && bufptr->iopage_buffer->iopage.prv.ptype != ptype)
	{
	  assert_release (no_error);
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
 *   maybe_deallocated(in) : true, if page may be deallocated
 *
 * Note: Verify if the given page's prv is valid.
 *       This function is used for debugging purposes.
 */
STATIC_INLINE bool
pgbuf_check_bcb_page_vpid (PGBUF_BCB * bufptr, bool maybe_deallocated)
{
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
      assert ((maybe_deallocated && log_is_in_crash_recovery_and_not_yet_completes_redo ())
	      || (bufptr->vpid.pageid == bufptr->iopage_buffer->iopage.prv.pageid
		  && bufptr->vpid.volid == bufptr->iopage_buffer->iopage.prv.volid));

      assert (bufptr->iopage_buffer->iopage.prv.p_reserve_1 == 0);
      assert (bufptr->iopage_buffer->iopage.prv.p_reserve_2 == 0);

      return (bufptr->vpid.pageid == bufptr->iopage_buffer->iopage.prv.pageid
	      && bufptr->vpid.volid == bufptr->iopage_buffer->iopage.prv.volid);
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
  fileio_init_lsa_of_page (iopage, IO_PAGESIZE);

  /* Init Page identifier */
  iopage->prv.pageid = -1;
  iopage->prv.volid = -1;

  iopage->prv.ptype = (unsigned char) PAGE_UNKNOWN;
  iopage->prv.pflag = '\0';
  iopage->prv.p_reserve_1 = 0;
  iopage->prv.p_reserve_2 = 0;
  iopage->prv.tde_nonce = 0;
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
      PGBUF_BCB_LOCK (bufptr);

      if (bufptr->latch_mode != PGBUF_LATCH_INVALID && bufptr->fcnt > 0)
	{
	  /* The buffer is not unfixed */
	  PGBUF_BCB_UNLOCK (bufptr);
	  pgbuf_dump ();
	  return;
	}

      consistent = pgbuf_is_consistent (bufptr, 0);
      PGBUF_BCB_UNLOCK (bufptr);

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
  (void) fprintf (stdout, "Lastperm volid = %d, Num permvols of tmparea = %d\n", pgbuf_Pool.last_perm_volid,
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
	  (void) fprintf (stdout, "%d", pgbuf_Pool.permvols_tmparea_volids[i]);
	}
      (void) fprintf (stdout, "\n");
    }
  pthread_mutex_unlock (&pgbuf_Pool.volinfo_mutex);

  /* Now, dump all buffer pages */
  (void) fprintf (stdout,
		  " Buf Volid Pageid Fcnt LatchMode D A F        Zone      Lsa    consistent Bufaddr   Usrarea\n");

  for (bufid = 0; bufid < pgbuf_Pool.num_buffers; bufid++)
    {
      bufptr = PGBUF_FIND_BCB_PTR (bufid);
      PGBUF_BCB_LOCK (bufptr);

      if (bufptr->fcnt > 0)
	{
	  nfetched++;
	}

      if (pgbuf_bcb_is_dirty (bufptr))
	{
	  ndirty++;
	}

      /* check if the content of current buffer page is consistent. */
      consistent = pgbuf_is_consistent (bufptr, 0);
      if (!pgbuf_bcb_is_dirty (bufptr) && bufptr->fcnt == 0 && consistent != PGBUF_CONTENT_BAD)
	{
	  PGBUF_BCB_UNLOCK (bufptr);
	  continue;
	}
      else
	{
	  latch_mode_str = pgbuf_latch_mode_str (bufptr->latch_mode);
	  zone_str = pgbuf_latch_mode_str (bufptr->zone);
	  consistenet_str = pgbuf_consistent_str (consistent);

	  fprintf (stdout, "%4d %5d %6d %4d %9s %1d %1d %1d %11s %lld|%4d %10s %p %p-%p\n",
		   pgbuf_bcb_get_pool_index (bufptr), VPID_AS_ARGS (&bufptr->vpid), bufptr->fcnt, latch_mode_str,
		   pgbuf_bcb_is_dirty (bufptr), (int) pgbuf_bcb_is_flushing (bufptr),
		   (int) pgbuf_bcb_is_async_flush_request (bufptr), zone_str,
		   LSA_AS_ARGS (&bufptr->iopage_buffer->iopage.prv.lsa), consistent_str, (void *) bufptr,
		   (void *) (&bufptr->iopage_buffer->iopage.page[0]),
		   (void *) (&bufptr->iopage_buffer->iopage.page[DB_PAGESIZE - 1]));
	}
      PGBUF_BCB_UNLOCK (bufptr);
    }

  (void) fprintf (stdout, "Number of fetched buffers = %d\nNumber of dirty buffers = %d\n", nfetched, ndirty);
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
  bool is_page_corrupted;

  /* the caller is holding bufptr->mutex */
  if (memcmp (PGBUF_FIND_BUFFER_GUARD (bufptr), pgbuf_Guard, sizeof (pgbuf_Guard)) != 0)
    {
      er_log_debug (ARG_FILE_LINE, "SYSTEM ERROR buffer of pageid = %d|%d has been OVER RUN", bufptr->vpid.volid,
		    bufptr->vpid.pageid);
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
      if (fileio_read (NULL, fileio_get_volume_descriptor (bufptr->vpid.volid), malloc_io_pgptr, bufptr->vpid.pageid,
		       IO_PAGESIZE) == NULL)
	{
	  /* Unable to verify consistency of this page */
	  consistent = PGBUF_CONTENT_BAD;
	}
      else
	{
	  /* If page is dirty, it should be different from the one on disk */
	  if (!LSA_EQ (&malloc_io_pgptr->prv.lsa, &bufptr->iopage_buffer->iopage.prv.lsa)
	      || memcmp (malloc_io_pgptr->page, bufptr->iopage_buffer->iopage.page, DB_PAGESIZE) != 0)
	    {
	      consistent = (pgbuf_bcb_is_dirty (bufptr) ? PGBUF_CONTENT_GOOD : PGBUF_CONTENT_BAD);

	      /* If fix count is greater than likely_bad_after_fixcnt, the function cannot state that the page is bad */
	      if (consistent == PGBUF_CONTENT_BAD && bufptr->fcnt > likely_bad_after_fixcnt)
		{
		  consistent = PGBUF_CONTENT_LIKELY_BAD;
		}
	    }
	  else
	    {
	      consistent = (pgbuf_bcb_is_dirty (bufptr) ? PGBUF_CONTENT_LIKELY_BAD : PGBUF_CONTENT_GOOD);
	    }
	}

      if (consistent != PGBUF_CONTENT_GOOD)
	{
	  if (fileio_page_check_corruption (thread_get_thread_entry_info (), malloc_io_pgptr,
					    &is_page_corrupted) != NO_ERROR || is_page_corrupted)
	    {
	      consistent = PGBUF_CONTENT_BAD;
	    }
	}

      free_and_init (malloc_io_pgptr);
    }
  else
    {
      if (bufptr->fcnt <= 0 && pgbuf_get_check_page_validation_level (PGBUF_DEBUG_PAGE_VALIDATION_ALL))
	{
	  int i;
	  /* The page should be scrambled, otherwise some one step on it */
	  for (i = 0; i < DB_PAGESIZE; i++)
	    {
	      if (bufptr->iopage_buffer->iopage.page[i] != MEM_REGION_SCRAMBLE_MARK)
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

#if !defined(NDEBUG)
static void
pgbuf_add_fixed_at (PGBUF_HOLDER * holder, const char *caller_file, int caller_line, bool reset)
{
  char buf[256];
  const char *p;

  p = caller_file + strlen (caller_file);
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

  if (reset)
    {
      sprintf (holder->fixed_at, "%s:%d ", p, caller_line);
      holder->fixed_at_size = (int) strlen (holder->fixed_at);
    }
  else
    {
      sprintf (buf, "%s:%d ", p, caller_line);
      if (strstr (holder->fixed_at, buf) == NULL)
	{
	  strcat (holder->fixed_at, buf);
	  holder->fixed_at_size += (int) strlen (buf);
	  assert (holder->fixed_at_size < (64 * 1024));
	}
    }

  return;
}
#endif /* NDEBUG */

#if defined(SERVER_MODE)
static void
pgbuf_sleep (THREAD_ENTRY * thread_p, pthread_mutex_t * mutex_p)
{
  thread_lock_entry (thread_p);
  pthread_mutex_unlock (mutex_p);

  thread_suspend_wakeup_and_unlock_entry (thread_p, THREAD_PGBUF_SUSPENDED);
}

STATIC_INLINE int
pgbuf_wakeup (THREAD_ENTRY * thread_p)
{
  int r = NO_ERROR;

  if (thread_p->request_latch_mode != PGBUF_NO_LATCH)
    {
      thread_p->resume_status = THREAD_PGBUF_RESUMED;

      r = pthread_cond_signal (&thread_p->wakeup_cond);
      if (r != 0)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_COND_SIGNAL, 0);
	  thread_unlock_entry (thread_p);
	  return ER_CSS_PTHREAD_COND_SIGNAL;
	}
    }
  else
    {
      er_log_debug (ARG_FILE_LINE, "thread_entry (%d, %ld) already timedout\n", thread_p->tran_index,
		    thread_p->get_posix_id ());
    }

  thread_unlock_entry (thread_p);

  return r;
}

STATIC_INLINE int
pgbuf_wakeup_uncond (THREAD_ENTRY * thread_p)
{
  int r;

  thread_lock_entry (thread_p);
  thread_p->resume_status = THREAD_PGBUF_RESUMED;

  r = pthread_cond_signal (&thread_p->wakeup_cond);
  if (r != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_COND_SIGNAL, 0);
      thread_unlock_entry (thread_p);
      return ER_CSS_PTHREAD_COND_SIGNAL;
    }

  thread_unlock_entry (thread_p);

  return r;
}
#endif /* SERVER_MODE */

STATIC_INLINE void
pgbuf_set_dirty_buffer_ptr (THREAD_ENTRY * thread_p, PGBUF_BCB * bufptr)
{
  PGBUF_HOLDER *holder;

  assert (bufptr != NULL);

  pgbuf_bcb_set_dirty (thread_p, bufptr);

  holder = pgbuf_find_thrd_holder (thread_p, bufptr);
  assert (bufptr->latch_mode == PGBUF_LATCH_WRITE);
  assert (holder != NULL);
  if (holder != NULL && holder->perf_stat.dirtied_by_holder == 0)
    {
      holder->perf_stat.dirtied_by_holder = 1;
    }

  /* Record number of dirties in statistics */
  perfmon_inc_stat (thread_p, PSTAT_PB_NUM_DIRTIES);
}

/*
 * pgbuf_wakeup_page_flush_daemon () - Wakeup the flushing daemon thread to flush some
 *				  of the dirty pages in buffer pool to disk
 * return : void
 * thread_p (in) :
 */
static void
pgbuf_wakeup_page_flush_daemon (THREAD_ENTRY * thread_p)
{
#if defined (SERVER_MODE)
  if (pgbuf_is_page_flush_daemon_available ())
    {
      pgbuf_Page_flush_daemon->wakeup ();
      return;
    }
#endif

  PERF_UTIME_TRACKER dummy_time_tracker;
  bool stop = false;

  /* single-threaded environment. do flush on our own. */
  dummy_time_tracker.is_perf_tracking = false;
  pgbuf_flush_victim_candidates (thread_p, prm_get_float_value (PRM_ID_PB_BUFFER_FLUSH_RATIO), &dummy_time_tracker,
				 &stop);
  assert (!stop);
}

/*
 * pgbuf_has_perm_pages_fixed () -
 *
 * return	       : The number of pages fixed by the thread.
 * thread_p (in)       : Thread entry.
 *
 */
bool
pgbuf_has_perm_pages_fixed (THREAD_ENTRY * thread_p)
{
  int thrd_idx = thread_get_entry_index (thread_p);
  PGBUF_HOLDER *holder = NULL;

  if (pgbuf_Pool.thrd_holder_info[thrd_idx].num_hold_cnt == 0)
    {
      return false;
    }

  for (holder = pgbuf_Pool.thrd_holder_info[thrd_idx].thrd_hold_list; holder != NULL; holder = holder->thrd_link)
    {
      if (holder->bufptr->iopage_buffer->iopage.prv.ptype != PAGE_QRESULT)
	{
	  return true;
	}
    }
  return false;
}

#if defined (SERVER_MODE)
/*
 * pgbuf_is_thread_high_priority () -
 *
 * return	       : true if the threads has any fixed pages and the other is waiting on any of them or
 *			 it has an important hot page such as volume header, file header, index root and heap header.
 * thread_p (in)       : Thread entry.
 */
static bool
pgbuf_is_thread_high_priority (THREAD_ENTRY * thread_p)
{
  int thrd_idx = thread_get_entry_index (thread_p);
  PGBUF_HOLDER *holder = NULL;

  if (pgbuf_Pool.thrd_holder_info[thrd_idx].num_hold_cnt == 0)
    {
      /* not owns any page */
      return false;
    }

  for (holder = pgbuf_Pool.thrd_holder_info[thrd_idx].thrd_hold_list; holder != NULL; holder = holder->thrd_link)
    {
      if (holder->bufptr->next_wait_thrd != NULL)
	{
	  /* someone is waiting for the thread */
	  return true;
	}

      if (holder->bufptr->iopage_buffer->iopage.prv.ptype == PAGE_VOLHEADER)
	{
	  /* has volume header */
	  return true;
	}
      if (holder->bufptr->iopage_buffer->iopage.prv.ptype == PAGE_FTAB)
	{
	  /* holds a file header page */
	  return true;
	}
      if (holder->bufptr->iopage_buffer->iopage.prv.ptype == PAGE_BTREE
	  && (btree_get_perf_btree_page_type (thread_p, holder->bufptr->iopage_buffer->iopage.page)
	      == PERF_PAGE_BTREE_ROOT))
	{
	  /* holds b-tree root */
	  return true;
	}
      if (holder->bufptr->iopage_buffer->iopage.prv.ptype == PAGE_HEAP
	  && heap_is_page_header (thread_p, holder->bufptr->iopage_buffer->iopage.page))
	{
	  /* heap file header */
	  return true;
	}
    }

  return false;
}
#endif /* SERVER_MODE */

enum
{
  NEIGHBOR_ABORT_RANGE = 1,

  NEIGHBOR_ABORT_NOTFOUND_NONDIRTY_BACK,
  NEIGHBOR_ABORT_NOTFOUND_DIRTY_BACK,

  NEIGHBOR_ABORT_LATCH_NONDIRTY_BACK,
  NEIGHBOR_ABORT_LATCH_DIRTY_BACK,

  NEIGHBOR_ABORT_NONDIRTY_NOT_ALLOWED,
  NEIGHBOR_ABORT_TWO_CONSECTIVE_NONDIRTIES,
  NEIGHBOR_ABORT_TOO_MANY_NONDIRTIES
};

/*
 * pgbuf_flush_page_and_neighbors_fb () - Flush page pointed to by the supplied BCB and also flush neighbor pages
 *
 * return : error code or NO_ERROR
 * thread_p (in) : thread entry
 * bufptr (in)	 : BCB to flush
 * flushed_pages(out): actual number of flushed pages
 *
 * todo: too big to be inlined. maybe we can optimize it.
 */
static int
pgbuf_flush_page_and_neighbors_fb (THREAD_ENTRY * thread_p, PGBUF_BCB * bufptr, int *flushed_pages)
{
#define PGBUF_PAGES_COUNT_THRESHOLD 4
  int error = NO_ERROR, i;
  int save_first_error = NO_ERROR;
  LOG_LSA log_newest_oldest_unflush_lsa;
  VPID first_vpid, vpid;
  PGBUF_BUFFER_HASH *hash_anchor;
  PGBUF_BATCH_FLUSH_HELPER *helper = &pgbuf_Flush_helper;
  bool prev_page_dirty = true;
  int dirty_pages_cnt = 0;
  int pos;
  bool forward;
  bool search_nondirty;
  int written_pages;
  int abort_reason;
  bool was_page_flushed = false;
#if defined(ENABLE_SYSTEMTAP)
  QUERY_ID query_id = -1;
  bool monitored = false;
#endif /* ENABLE_SYSTEMTAP */

#if defined(ENABLE_SYSTEMTAP)
  query_id = qmgr_get_current_query_id (thread_p);
  if (query_id != NULL_QUERY_ID)
    {
      monitored = true;
      CUBRID_IO_WRITE_START (query_id);
    }
#endif /* ENABLE_SYSTEMTAP */

  /* init */
  helper->npages = 0;
  helper->fwd_offset = 0;
  helper->back_offset = 0;

  /* add bufptr as middle page */
  pgbuf_add_bufptr_to_batch (bufptr, 0);
  VPID_COPY (&first_vpid, &bufptr->vpid);
  LSA_COPY (&log_newest_oldest_unflush_lsa, &bufptr->oldest_unflush_lsa);
  PGBUF_BCB_UNLOCK (bufptr);

  VPID_COPY (&vpid, &first_vpid);

  /* Now search around bufptr->vpid for neighbors. */
  forward = true;
  search_nondirty = false;
  abort_reason = 0;
  for (i = 1; i < PGBUF_NEIGHBOR_PAGES;)
    {
      if (forward == true)
	{
	  if (first_vpid.pageid <= PAGEID_MAX - (helper->fwd_offset + 1))
	    {
	      vpid.pageid = first_vpid.pageid + helper->fwd_offset + 1;
	    }
	  else
	    {
	      abort_reason = NEIGHBOR_ABORT_RANGE;
	      break;
	    }
	}
      else
	{
	  if (first_vpid.pageid >= helper->back_offset + 1)
	    {
	      vpid.pageid = first_vpid.pageid - helper->back_offset - 1;
	    }
	  else if (PGBUF_NEIGHBOR_FLUSH_NONDIRTY == false || search_nondirty == true)
	    {
	      abort_reason = NEIGHBOR_ABORT_RANGE;
	      break;
	    }
	  else
	    {
	      search_nondirty = true;
	      forward = true;
	      continue;
	    }
	}

      hash_anchor = &pgbuf_Pool.buf_hash_table[PGBUF_HASH_VALUE (&vpid)];

      bufptr = pgbuf_search_hash_chain (thread_p, hash_anchor, &vpid);
      if (bufptr == NULL)
	{
	  /* Page not found: change direction or abandon batch */
	  pthread_mutex_unlock (&hash_anchor->hash_mutex);
	  if (search_nondirty == true)
	    {
	      if (forward == false)
		{
		  abort_reason = NEIGHBOR_ABORT_NOTFOUND_NONDIRTY_BACK;
		  break;
		}
	      else
		{
		  forward = false;
		  continue;
		}
	    }
	  else
	    {
	      if (forward == true)
		{
		  forward = false;
		  continue;
		}
	      else if (PGBUF_NEIGHBOR_FLUSH_NONDIRTY == true)
		{
		  search_nondirty = true;
		  forward = true;
		  continue;
		}
	      else
		{
		  abort_reason = NEIGHBOR_ABORT_NOTFOUND_DIRTY_BACK;
		  break;
		}
	    }
	}

      /* Abandon batch for: fixed pages, latched pages or with 'avoid_victim' */
      if (pgbuf_bcb_is_flushing (bufptr) || bufptr->latch_mode > PGBUF_LATCH_READ)
	{
	  PGBUF_BCB_UNLOCK (bufptr);
	  if (search_nondirty == true)
	    {
	      if (forward == false)
		{
		  abort_reason = NEIGHBOR_ABORT_LATCH_NONDIRTY_BACK;
		  break;
		}
	      else
		{
		  forward = false;
		  continue;
		}
	    }
	  else
	    {
	      if (forward == true)
		{
		  forward = false;
		  continue;
		}
	      else if (PGBUF_NEIGHBOR_FLUSH_NONDIRTY == true)
		{
		  search_nondirty = true;
		  forward = true;
		  continue;
		}
	      else
		{
		  abort_reason = NEIGHBOR_ABORT_LATCH_DIRTY_BACK;
		  break;
		}
	    }
	}

      if (!pgbuf_bcb_is_dirty (bufptr))
	{
	  if (search_nondirty == false)
	    {
	      PGBUF_BCB_UNLOCK (bufptr);
	      if (forward == true)
		{
		  forward = false;
		  continue;
		}
	      else if (PGBUF_NEIGHBOR_FLUSH_NONDIRTY == true)
		{
		  search_nondirty = true;
		  forward = true;
		  continue;
		}
	      abort_reason = NEIGHBOR_ABORT_NONDIRTY_NOT_ALLOWED;
	      break;
	    }

	  if (prev_page_dirty == false)
	    {
	      /* two consecutive non-dirty pages */
	      PGBUF_BCB_UNLOCK (bufptr);
	      abort_reason = NEIGHBOR_ABORT_TWO_CONSECTIVE_NONDIRTIES;
	      break;
	    }
	}
      else
	{
	  if (LSA_LT (&log_newest_oldest_unflush_lsa, &bufptr->oldest_unflush_lsa))
	    {
	      LSA_COPY (&log_newest_oldest_unflush_lsa, &bufptr->oldest_unflush_lsa);
	    }
	  dirty_pages_cnt++;
	}

      if (helper->npages > PGBUF_PAGES_COUNT_THRESHOLD && ((2 * dirty_pages_cnt) < helper->npages))
	{
	  /* too many non dirty pages */
	  PGBUF_BCB_UNLOCK (bufptr);
	  helper->npages = 1;
	  abort_reason = NEIGHBOR_ABORT_TOO_MANY_NONDIRTIES;
	  break;
	}

      prev_page_dirty = pgbuf_bcb_is_dirty (bufptr);

      /* add bufptr to batch */
      pgbuf_add_bufptr_to_batch (bufptr, vpid.pageid - first_vpid.pageid);
      PGBUF_BCB_UNLOCK (bufptr);
      i++;
    }

  if (prev_page_dirty == true)
    {
      if (helper->fwd_offset > 0 && !pgbuf_bcb_is_dirty (helper->pages_bufptr[PGBUF_NEIGHBOR_POS (helper->fwd_offset)]))
	{
	  helper->fwd_offset--;
	  helper->npages--;
	}
      if (helper->back_offset > 0
	  && !pgbuf_bcb_is_dirty (helper->pages_bufptr[PGBUF_NEIGHBOR_POS (-helper->back_offset)]))
	{
	  helper->back_offset--;
	  helper->npages--;
	}
    }

  if (helper->npages <= 1)
    {
      /* flush only first page */
      pos = PGBUF_NEIGHBOR_POS (0);
      bufptr = helper->pages_bufptr[pos];

      error = pgbuf_flush_neighbor_safe (thread_p, bufptr, &helper->vpids[pos], &was_page_flushed);
      if (error != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return error;
	}
      if (was_page_flushed)
	{
	  *flushed_pages = 1;
	}
      return NO_ERROR;
    }

  /* WAL protocol: force log record to disk */
  logpb_flush_log_for_wal (thread_p, &log_newest_oldest_unflush_lsa);

  written_pages = 0;
  for (pos = PGBUF_NEIGHBOR_POS (-helper->back_offset); pos <= PGBUF_NEIGHBOR_POS (helper->fwd_offset); pos++)
    {
      bufptr = helper->pages_bufptr[pos];

      error = pgbuf_flush_neighbor_safe (thread_p, bufptr, &helper->vpids[pos], &was_page_flushed);
      if (error != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  if (save_first_error == NO_ERROR)
	    {
	      save_first_error = error;
	    }
	  continue;
	}
      if (was_page_flushed)
	{
	  written_pages++;
	}
    }

  if (prm_get_bool_value (PRM_ID_LOG_PGBUF_VICTIM_FLUSH))
    {
      _er_log_debug (ARG_FILE_LINE,
		     "pgbuf_flush_page_and_neighbors_fb: collected_pages:%d, written:%d, back_offset:%d, fwd_offset%d, "
		     "abort_reason:%d", helper->npages, written_pages, helper->back_offset, helper->fwd_offset,
		     abort_reason);
    }

  *flushed_pages = written_pages;
  helper->npages = 0;

  return save_first_error;
#undef PGBUF_PAGES_COUNT_THRESHOLD
}

/*
 * pgbuf_add_bufptr_to_batch () - Add a page to the flush helper
 * return : void
 * bufptr (in) : BCB of page to add
 */
STATIC_INLINE void
pgbuf_add_bufptr_to_batch (PGBUF_BCB * bufptr, int idx)
{
  PGBUF_BATCH_FLUSH_HELPER *helper = &pgbuf_Flush_helper;
  int pos;

  assert (bufptr->latch_mode == PGBUF_NO_LATCH || bufptr->latch_mode == PGBUF_LATCH_READ
	  || bufptr->latch_mode == PGBUF_LATCH_WRITE);

  assert (idx > -PGBUF_NEIGHBOR_PAGES && idx < PGBUF_NEIGHBOR_PAGES);
  pos = PGBUF_NEIGHBOR_POS (idx);

  VPID_COPY (&helper->vpids[pos], &bufptr->vpid);
  helper->pages_bufptr[pos] = bufptr;

  helper->npages++;
  if (idx > 0)
    {
      helper->fwd_offset++;
    }
  else if (idx < 0)
    {
      helper->back_offset++;
    }
}

/*
 * pgbuf_flush_neighbor_safe () - Flush collected page for neighbor flush if it's safe:
 *				  1. VPID of bufptr has not changed.
 *				  2. Page has no latch or is only latched for read.
 *
 * return	      : Error code.
 * thread_p (in)      : Thread entry.
 * bufptr (in)	      : Buffered page collected for neighbor flush.
 * expected_vpid (in) : Expected VPID for bufptr.
 * flushed (out)      : Output true if page was flushed.
 */
STATIC_INLINE int
pgbuf_flush_neighbor_safe (THREAD_ENTRY * thread_p, PGBUF_BCB * bufptr, VPID * expected_vpid, bool * flushed)
{
  int error = NO_ERROR;
  bool is_bcb_locked = true;

  assert (bufptr != NULL);
  assert (expected_vpid != NULL && !VPID_ISNULL (expected_vpid));
  assert (flushed != NULL);

  *flushed = false;

  PGBUF_BCB_LOCK (bufptr);
  if (!VPID_EQ (&bufptr->vpid, expected_vpid))
    {
      PGBUF_BCB_UNLOCK (bufptr);
      return NO_ERROR;
    }

  if (pgbuf_bcb_is_flushing (bufptr) || bufptr->latch_mode > PGBUF_LATCH_READ)
    {
      PGBUF_BCB_UNLOCK (bufptr);
      return NO_ERROR;
    }

  /* flush even if it is not dirty. todo: is this necessary? */
  error = pgbuf_bcb_flush_with_wal (thread_p, bufptr, true, &is_bcb_locked);
  if (is_bcb_locked)
    {
      PGBUF_BCB_UNLOCK (bufptr);
    }
  if (error == NO_ERROR)
    {
      *flushed = true;
    }
  else
    {
      ASSERT_ERROR ();
    }
  return error;
}

/*
 * pgbuf_compare_hold_vpid_for_sort () - Compare the vpid for sort
 *   return: p1 - p2
 *   p1(in): victim candidate list 1
 *   p2(in): victim candidate list 2
 */
static int
pgbuf_compare_hold_vpid_for_sort (const void *p1, const void *p2)
{
  PGBUF_HOLDER_INFO *h1, *h2;
  int diff;

  h1 = (PGBUF_HOLDER_INFO *) p1;
  h2 = (PGBUF_HOLDER_INFO *) p2;

  if (h1 == h2)
    {
      return 0;
    }

  /* Pages with NULL GROUP sort last */
  if (VPID_ISNULL (&h1->group_id) && !VPID_ISNULL (&h2->group_id))
    {
      return 1;
    }
  else if (!VPID_ISNULL (&h1->group_id) && VPID_ISNULL (&h2->group_id))
    {
      return -1;
    }

  diff = h1->group_id.volid - h2->group_id.volid;
  if (diff != 0)
    {
      return diff;
    }

  diff = h1->group_id.pageid - h2->group_id.pageid;
  if (diff != 0)
    {
      return diff;
    }

  diff = h1->rank - h2->rank;
  if (diff != 0)
    {
      return diff;
    }

  diff = h1->vpid.volid - h2->vpid.volid;
  if (diff != 0)
    {
      return diff;
    }

  diff = h1->vpid.pageid - h2->vpid.pageid;
  if (diff != 0)
    {
      return diff;
    }

  return diff;
}

/*
 * pgbuf_ordered_fix () - Fix page in VPID order; other previously fixed pages may be unfixed and re-fixed again.
 *   return: error code
 *   thread_p(in):
 *   req_vpid(in):
 *   fetch_mode(in): old or new page
 *   request_mode(in): latch mode
 *   req_watcher(in/out): page watcher object, also holds output page pointer
 *
 *  Note: If fails to re-fix previously fixed pages (unfixed with this request), the requested page is unfixed
 *        (if fixed) and error is returned. In such case, older some pages may be re-fixed, other not : the caller
 *	  should check page pointer of watchers before using them in case of error.
 *
 *  Note2: If any page re-fix occurs for previously fixed pages, their 'unfix' flag in their watcher is set.
 *         (caller is responsible to check this flag)
 *
 */
#if !defined(NDEBUG)
int
pgbuf_ordered_fix_debug (THREAD_ENTRY * thread_p, const VPID * req_vpid, PAGE_FETCH_MODE fetch_mode,
			 const PGBUF_LATCH_MODE request_mode, PGBUF_WATCHER * req_watcher, const char *caller_file,
			 int caller_line, const char *caller_func)
#else /* NDEBUG */
int
pgbuf_ordered_fix_release (THREAD_ENTRY * thread_p, const VPID * req_vpid, PAGE_FETCH_MODE fetch_mode,
			   const PGBUF_LATCH_MODE request_mode, PGBUF_WATCHER * req_watcher)
#endif				/* NDEBUG */
{
  int er_status = NO_ERROR;
  PGBUF_HOLDER *holder, *next_holder;
  PAGE_PTR pgptr, ret_pgptr;
  int i, thrd_idx;
  int saved_pages_cnt = 0;
  PGBUF_LATCH_MODE curr_request_mode;
  PAGE_FETCH_MODE curr_fetch_mode;
  PGBUF_HOLDER_INFO ordered_holders_info[PGBUF_MAX_PAGE_FIXED_BY_TRAN];
  PGBUF_HOLDER_INFO req_page_holder_info;
  bool req_page_has_watcher;
  bool req_page_has_group = false;
  int er_status_get_hfid = NO_ERROR;
  VPID req_page_groupid;
  bool has_dealloc_prevent_flag = false;
  PGBUF_LATCH_CONDITION latch_condition;
  PGBUF_BCB *bufptr = NULL;
#if defined(PGBUF_ORDERED_DEBUG)
  static unsigned int global_ordered_fix_id = 0;
  unsigned int ordered_fix_id;
#endif

  assert (req_watcher != NULL);

#if defined(PGBUF_ORDERED_DEBUG)
  ordered_fix_id = global_ordered_fix_id++;
#endif

#if !defined(NDEBUG)
  assert (req_watcher->magic == PGBUF_WATCHER_MAGIC_NUMBER);
#endif

  ret_pgptr = NULL;

  req_page_has_watcher = false;
  if (req_watcher->pgptr != NULL)
    {
      assert_release (false);
      er_status = ER_FAILED_ASSERTION;
      goto exit;
    }

  /* set or promote current page rank */
  if (VPID_EQ (&req_watcher->group_id, req_vpid))
    {
      req_watcher->curr_rank = PGBUF_ORDERED_HEAP_HDR;
    }
  else
    {
      req_watcher->curr_rank = req_watcher->initial_rank;
    }

  req_page_has_group = VPID_ISNULL (&req_watcher->group_id) ? false : true;
  if (req_page_has_group == false)
    {
      VPID_SET_NULL (&req_page_groupid);
    }

  VPID_COPY (&req_page_holder_info.group_id, &req_watcher->group_id);
  req_page_holder_info.rank = req_watcher->curr_rank;
  VPID_COPY (&req_page_holder_info.vpid, req_vpid);
  req_page_holder_info.watch_count = 1;
  req_page_holder_info.watcher[0] = req_watcher;

  thrd_idx = thread_get_entry_index (thread_p);
  holder = pgbuf_Pool.thrd_holder_info[thrd_idx].thrd_hold_list;
  if ((holder == NULL) || ((holder->thrd_link == NULL) && (VPID_EQ (req_vpid, &(holder->bufptr->vpid)))))
    {
      /* There are no other fixed pages or only the requested page was already fixed */
      latch_condition = PGBUF_UNCONDITIONAL_LATCH;
    }
  else
    {
      latch_condition = PGBUF_CONDITIONAL_LATCH;
    }

#if !defined(NDEBUG)
  ret_pgptr =
    pgbuf_fix_debug (thread_p, req_vpid, fetch_mode, request_mode, latch_condition, caller_file, caller_line,
		     caller_func);
#else
  ret_pgptr = pgbuf_fix_release (thread_p, req_vpid, fetch_mode, request_mode, latch_condition);
#endif

  if (ret_pgptr != NULL)
    {
      for (holder = pgbuf_Pool.thrd_holder_info[thrd_idx].thrd_hold_list; holder != NULL; holder = holder->thrd_link)
	{
	  CAST_BFPTR_TO_PGPTR (ret_pgptr, holder->bufptr);

	  if (VPID_EQ (req_vpid, &(holder->bufptr->vpid)))
	    {
	      assert (PGBUF_IS_ORDERED_PAGETYPE (holder->bufptr->iopage_buffer->iopage.prv.ptype));

	      if (req_page_has_group == false && holder->first_watcher != NULL)
		{
		  /* special case : already have fix on this page with an watcher; get group id from existing watcher */
		  assert (holder->watch_count > 0);
		  assert (!VPID_ISNULL (&holder->first_watcher->group_id));
		  VPID_COPY (&req_watcher->group_id, &holder->first_watcher->group_id);
		}
	      else if (req_page_has_group == false && pgbuf_get_page_ptype (thread_p, ret_pgptr) == PAGE_HEAP)
		{
		  er_status = pgbuf_get_groupid_and_unfix (thread_p, req_vpid, &ret_pgptr, &req_page_groupid, false);
		  if (er_status != NO_ERROR)
		    {
		      er_status_get_hfid = er_status;
		      goto exit;
		    }
		  assert (!VPID_ISNULL (&req_page_groupid));
		  VPID_COPY (&req_watcher->group_id, &req_page_groupid);
		}
#if !defined(NDEBUG)
	      pgbuf_add_watch_instance_internal (holder, ret_pgptr, req_watcher, request_mode, true, caller_file,
						 caller_line);
#else
	      pgbuf_add_watch_instance_internal (holder, ret_pgptr, req_watcher, request_mode, true);
#endif
	      req_page_has_watcher = true;
	      goto exit;
	    }
	}

      assert_release (false);

      er_status = ER_FAILED_ASSERTION;
      goto exit;
    }
  else
    {
      int wait_msecs;

      assert (ret_pgptr == NULL);

      er_status = er_errid_if_has_error ();
      if (er_status == ER_PB_BAD_PAGEID || er_status == ER_INTERRUPTED)
	{
	  goto exit;
	}

      wait_msecs = pgbuf_find_current_wait_msecs (thread_p);
      if (wait_msecs == LK_ZERO_WAIT || wait_msecs == LK_FORCE_ZERO_WAIT)
	{
	  /* attempts to unfix-refix old page may fail since CONDITIONAL latch will be enforced; just return page
	   * cannot be fixed */
	  if (er_status == NO_ERROR)
	    {
	      /* LK_FORCE_ZERO_WAIT is used in some page scan functions (e.g. heap_stats_find_page_in_bestspace) to
	       * skip busy pages; here we return an error code (which means the page was not fixed), however no error
	       * is set : this allows scan of pages to continue */
	      assert (wait_msecs == LK_FORCE_ZERO_WAIT);
	      er_status = ER_LK_PAGE_TIMEOUT;
	    }
	  goto exit;
	}

      if (latch_condition == PGBUF_UNCONDITIONAL_LATCH)
	{
	  /* continue */
	  er_status = er_errid ();
	  if (er_status == NO_ERROR)
	    {
	      er_status = ER_FAILED;
	    }
	  goto exit;
	}

      /* to proceed ordered fix the pages, forget any underlying error. */
      er_status = NO_ERROR;
    }

  if (fetch_mode == OLD_PAGE_PREVENT_DEALLOC)
    {
      has_dealloc_prevent_flag = true;
      fetch_mode = OLD_PAGE;
    }

  holder = pgbuf_Pool.thrd_holder_info[thrd_idx].thrd_hold_list;
  while (holder != NULL)
    {
      next_holder = holder->thrd_link;
      if (holder->watch_count <= 0)
	{
	  /* cannot perform unfix-ordered fix without watcher; we assume that this holder's page will not trigger a
	   * latch deadlock and ignore it */
	  holder = next_holder;
	  continue;
	}

      assert (PGBUF_IS_ORDERED_PAGETYPE (holder->bufptr->iopage_buffer->iopage.prv.ptype));

      if (saved_pages_cnt >= PGBUF_MAX_PAGE_FIXED_BY_TRAN)
	{
	  assert_release (false);

	  er_status = ER_FAILED_ASSERTION;
	  goto exit;
	}
      else if (VPID_EQ (req_vpid, &(holder->bufptr->vpid)))
	{
	  /* already have a fix on this page, should not be here */
	  if (pgbuf_is_valid_page (thread_p, req_vpid, false, NULL, NULL) != DISK_VALID)
	    {
#if defined(PGBUF_ORDERED_DEBUG)
	      _er_log_debug (__FILE__, __LINE__,
			     "ORDERED_FIX(%u): page VPID:(%d,%d) (GROUP:%d,%d; rank:%d/%d) "
			     "invalid, while having holder: %X ", ordered_fix_id, req_vpid->volid, req_vpid->pageid,
			     req_watcher->group_id.volid, req_watcher->group_id.pageid, req_watcher->curr_rank,
			     req_watcher->initial_rank, holder);
#endif
	      er_status = er_errid ();
	    }
	  else
	    {
	      er_status = ER_FAILED_ASSERTION;
	    }
	  assert_release (false);

	  goto exit;
	}
      else
	{
	  int holder_fix_cnt;
	  int j, diff;
	  PAGE_PTR save_page_ptr = NULL;
	  PGBUF_WATCHER *pg_watcher;
	  int page_rank;
	  PGBUF_ORDERED_GROUP group_id;

	  page_rank = PGBUF_ORDERED_RANK_UNDEFINED;
	  VPID_SET_NULL (&group_id);
	  holder_fix_cnt = holder->fix_count;

	  if (holder_fix_cnt != holder->watch_count)
	    {
	      /* this page was fixed without watcher, without being unfixed before another page fix ; we do not allow
	       * this */
	      assert_release (false);

	      er_status = ER_FAILED_ASSERTION;
	      goto exit;
	    }

	  assert (holder->watch_count < PGBUF_MAX_PAGE_WATCHERS);

	  ordered_holders_info[saved_pages_cnt].latch_mode = PGBUF_LATCH_READ;
	  pg_watcher = holder->first_watcher;
	  j = 0;
	  ordered_holders_info[saved_pages_cnt].prevent_dealloc = false;

	  /* add all watchers */
	  while (pg_watcher != NULL)
	    {
#if !defined(NDEBUG)
	      CAST_BFPTR_TO_PGPTR (pgptr, holder->bufptr);

	      assert (pg_watcher->magic == PGBUF_WATCHER_MAGIC_NUMBER);
	      assert (pg_watcher->pgptr == pgptr);
	      assert (pg_watcher->curr_rank < PGBUF_ORDERED_RANK_UNDEFINED);
	      assert (!VPID_ISNULL (&pg_watcher->group_id));
#endif
	      if (page_rank == PGBUF_ORDERED_RANK_UNDEFINED)
		{
		  page_rank = pg_watcher->curr_rank;
		}
	      else if (page_rank != pg_watcher->curr_rank)
		{
		  /* all watchers on this page should have the same rank */
		  char additional_msg[128];
		  snprintf (additional_msg, sizeof (additional_msg) - 1, "different page ranks:%d,%d", page_rank,
			    pg_watcher->curr_rank);

		  er_status = ER_PB_ORDERED_INCONSISTENCY;
		  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, er_status, 5, req_vpid->volid, req_vpid->pageid,
			  holder->bufptr->vpid.volid, holder->bufptr->vpid.pageid, additional_msg);
		  goto exit;
		}

	      if (VPID_ISNULL (&group_id))
		{
		  VPID_COPY (&group_id, &pg_watcher->group_id);
		}
	      else if (!VPID_EQ (&group_id, &pg_watcher->group_id))
		{
		  char additional_msg[128];
		  snprintf (additional_msg, sizeof (additional_msg) - 1, "different GROUP_ID : (%d,%d) and (%d,%d)",
			    group_id.volid, group_id.pageid, pg_watcher->group_id.volid, pg_watcher->group_id.pageid);

		  /* all watchers on this page should have the same group id */
		  er_status = ER_PB_ORDERED_INCONSISTENCY;
		  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, er_status, 5, req_vpid->volid, req_vpid->pageid,
			  holder->bufptr->vpid.volid, holder->bufptr->vpid.pageid, additional_msg);
		  goto exit;
		}

	      if (save_page_ptr == NULL)
		{
		  save_page_ptr = pg_watcher->pgptr;
		}
	      else
		{
		  assert (save_page_ptr == pg_watcher->pgptr);
		}

	      ordered_holders_info[saved_pages_cnt].watcher[j] = pg_watcher;
	      if (pg_watcher->latch_mode == PGBUF_LATCH_WRITE)
		{
		  ordered_holders_info[saved_pages_cnt].latch_mode = PGBUF_LATCH_WRITE;
		}
	      j++;

#if defined(PGBUF_ORDERED_DEBUG)
	      _er_log_debug (__FILE__, __LINE__,
			     "ordered_fix(%u): check_watcher: pgptr:%X, VPID:(%d,%d), GROUP:%d,%d, rank:%d/%d, "
			     "holder_fix_count:%d, holder_watch_count:%d, holder_fixed_at:%s", ordered_fix_id,
			     pg_watcher->pgptr, holder->bufptr->vpid.volid, holder->bufptr->vpid.pageid,
			     pg_watcher->group_id.volid, pg_watcher->group_id.pageid, pg_watcher->curr_rank,
			     pg_watcher->initial_rank, holder->fix_count, holder->watch_count, holder->fixed_at);
#endif
	      pg_watcher = pg_watcher->next;
	    }

	  assert (j == holder->watch_count);

	  VPID_COPY (&ordered_holders_info[saved_pages_cnt].group_id, &group_id);
	  ordered_holders_info[saved_pages_cnt].rank = page_rank;
	  VPID_COPY (&(ordered_holders_info[saved_pages_cnt].vpid), &(holder->bufptr->vpid));

	  if (req_page_has_group == true)
	    {
	      diff = pgbuf_compare_hold_vpid_for_sort (&req_page_holder_info, &ordered_holders_info[saved_pages_cnt]);
	    }
	  else
	    {
	      /* page needs to be unfixed */
	      diff = -1;
	    }

	  if (diff < 0)
	    {
	      ordered_holders_info[saved_pages_cnt].watch_count = holder->watch_count;
	      ordered_holders_info[saved_pages_cnt].ptype = (PAGE_TYPE) holder->bufptr->iopage_buffer->iopage.prv.ptype;

#if defined(PGBUF_ORDERED_DEBUG)
	      _er_log_debug (__FILE__, __LINE__,
			     "ordered_fix(%u):  save_watchers (%d): pgptr:%X, VPID:(%d,%d), "
			     "GROUP:(%d,%d), rank:%d(page_rank:%d), holder_fix_count:%d, holder_watch_count:%d",
			     ordered_fix_id, ordered_holders_info[saved_pages_cnt].watch_count, save_page_ptr,
			     ordered_holders_info[saved_pages_cnt].vpid.volid,
			     ordered_holders_info[saved_pages_cnt].vpid.pageid,
			     ordered_holders_info[saved_pages_cnt].group_id.volid,
			     ordered_holders_info[saved_pages_cnt].group_id.pageid,
			     ordered_holders_info[saved_pages_cnt].rank, page_rank, holder_fix_cnt,
			     holder->watch_count);
#endif
	      saved_pages_cnt++;
	    }
	  else if (diff == 0)
	    {
	      assert_release (false);

	      er_status = ER_FAILED_ASSERTION;
	      goto exit;
	    }
	  else
	    {
	      assert (diff > 0);
	      /* this page is correctly fixed before new requested page, the accumulated watchers are just ignored */
#if defined(PGBUF_ORDERED_DEBUG)
	      _er_log_debug (__FILE__, __LINE__,
			     "ordered_fix(%u): ignore:    pgptr:%X, VPID:(%d,%d) "
			     "GROUP:(%d,%d), rank:%d  --- ignored", ordered_fix_id, save_page_ptr,
			     ordered_holders_info[saved_pages_cnt].vpid.volid,
			     ordered_holders_info[saved_pages_cnt].vpid.pageid,
			     ordered_holders_info[saved_pages_cnt].group_id.volid,
			     ordered_holders_info[saved_pages_cnt].group_id.pageid,
			     ordered_holders_info[saved_pages_cnt].rank);
#endif
	    }
	}
      holder = next_holder;
    }

  holder = pgbuf_Pool.thrd_holder_info[thrd_idx].thrd_hold_list;
  /* unfix pages which do not fulfill the VPID order */
  for (i = 0; i < saved_pages_cnt; i++)
    {
      int j, holder_fix_cnt;
#if defined(PGBUF_ORDERED_DEBUG)
      int holder_fix_cnt_save;
#endif

      while (holder != NULL && !VPID_EQ (&(ordered_holders_info[i].vpid), &(holder->bufptr->vpid)))
	{
	  holder = holder->thrd_link;
	}

      if (holder == NULL)
	{
	  assert_release (false);
	  er_status = ER_FAILED_ASSERTION;
	  goto exit;
	}

      next_holder = holder->thrd_link;
      /* not necessary to remove each watcher since the holder will be removed completely */

      holder->watch_count = 0;
      holder->first_watcher = NULL;
      holder->last_watcher = NULL;
      holder_fix_cnt = holder->fix_count;
#if defined(PGBUF_ORDERED_DEBUG)
      holder_fix_cnt_save = holder_fix_cnt;
#endif

      CAST_BFPTR_TO_PGPTR (pgptr, holder->bufptr);
      assert (holder_fix_cnt > 0);
      /* prevent deallocate. */
      pgbuf_bcb_register_avoid_deallocation (holder->bufptr);
      ordered_holders_info[i].prevent_dealloc = true;
      while (holder_fix_cnt-- > 0)
	{
	  pgbuf_unfix (thread_p, pgptr);
	}

      for (j = 0; j < ordered_holders_info[i].watch_count; j++)
	{
	  PGBUF_WATCHER *pg_watcher;

	  pg_watcher = ordered_holders_info[i].watcher[j];

	  assert (pg_watcher->pgptr == pgptr);
	  assert (pg_watcher->curr_rank < PGBUF_ORDERED_RANK_UNDEFINED);

#if defined(PGBUF_ORDERED_DEBUG)
	  _er_log_debug (__FILE__, __LINE__,
			 "ordered_fix(%u):  unfix & clear_watcher(%d/%d): pgptr:%X, VPID:(%d,%d), GROUP:%d,%d, "
			 "rank:%d/%d, latch_mode:%d, holder_fix_cnt:%d", ordered_fix_id, j + 1,
			 ordered_holders_info[i].watch_count, pg_watcher->pgptr, ordered_holders_info[i].vpid.volid,
			 ordered_holders_info[i].vpid.pageid, pg_watcher->group_id.volid, pg_watcher->group_id.pageid,
			 pg_watcher->curr_rank, pg_watcher->initial_rank, pg_watcher->latch_mode, holder_fix_cnt_save);
#endif
	  PGBUF_CLEAR_WATCHER (pg_watcher);
	  pg_watcher->page_was_unfixed = true;

#if !defined(NDEBUG)
	  pgbuf_watcher_init_debug (pg_watcher, caller_file, caller_line, true);
#endif
	}
      holder = next_holder;
    }

  /* the following code assumes that if the class OID is deleted, after the requested page is unlatched, the HFID page
   * is not reassigned to an ordinary page; in such case, a page deadlock may occur in worst case. Example of scenario
   * when such situation may occur : We assume an existing latch on VPID1 (0, 90) 1. Fix requested page VPID2 (0, 100),
   * get class_oid from page 2. Unfix requested page 3. Get HFID from schema : < between 2 and 3, other threads drop
   * the class, and HFID page is reused, along with current page which may be allocated to the HFID of another class >
   * 4. Still assuming that HFID is valid, this thread starts latching pages: In order VPID1, VPID2 At the same time,
   * another thread, starts latching pages VPID1 and VPID2, but since this later thread knows that VPID2 is a HFID,
   * will use the order VPID2, VPID1. */
  if (req_page_has_group == false)
    {
#if !defined(NDEBUG)
      /* all previous pages with watcher have been unfixed */
      holder = pgbuf_Pool.thrd_holder_info[thrd_idx].thrd_hold_list;
      while (holder != NULL)
	{
	  assert (holder->watch_count == 0);
	  holder = holder->thrd_link;
	}
      pgptr =
	pgbuf_fix_debug (thread_p, req_vpid, fetch_mode, request_mode, PGBUF_UNCONDITIONAL_LATCH, caller_file,
			 caller_line, caller_func);
#else
      pgptr = pgbuf_fix_release (thread_p, req_vpid, fetch_mode, request_mode, PGBUF_UNCONDITIONAL_LATCH);
#endif
      if (pgptr != NULL)
	{
	  if (has_dealloc_prevent_flag == true)
	    {
	      CAST_PGPTR_TO_BFPTR (bufptr, pgptr);
	      pgbuf_bcb_unregister_avoid_deallocation (bufptr);
	      has_dealloc_prevent_flag = false;
	    }
	  if (pgbuf_get_page_ptype (thread_p, pgptr) == PAGE_HEAP)
	    {
	      er_status = pgbuf_get_groupid_and_unfix (thread_p, req_vpid, &pgptr, &req_page_groupid, true);
	      if (er_status != NO_ERROR)
		{
		  er_status_get_hfid = er_status;
		  /* continue (re-latch old pages) */
		}
	    }
	}
      else
	{
	  /* continue */
	  er_status_get_hfid = er_errid ();
	  if (er_status_get_hfid == NO_ERROR)
	    {
	      er_status_get_hfid = ER_FAILED;
	    }
	}
    }

#if defined(PGBUF_ORDERED_DEBUG)
  _er_log_debug (__FILE__, __LINE__,
		 "ordered_fix(%u) : restore_pages: %d, req_VPID(%d,%d), GROUP(%d,%d), rank:%d/%d", ordered_fix_id,
		 saved_pages_cnt, req_vpid->volid, req_vpid->pageid, req_watcher->group_id.volid,
		 req_watcher->group_id.pageid, req_watcher->curr_rank, req_watcher->initial_rank);
#endif

  /* add requested page, watch instance is added after page is fixed */
  if (req_page_has_group == true || er_status_get_hfid == NO_ERROR)
    {
      if (req_page_has_group)
	{
	  VPID_COPY (&(ordered_holders_info[saved_pages_cnt].group_id), &req_watcher->group_id);
	}
      else
	{
	  assert (!VPID_ISNULL (&req_page_groupid));
	  VPID_COPY (&req_watcher->group_id, &req_page_groupid);
	  VPID_COPY (&(ordered_holders_info[saved_pages_cnt].group_id), &req_page_groupid);
	}
      VPID_COPY (&(ordered_holders_info[saved_pages_cnt].vpid), req_vpid);
      if (req_page_has_group)
	{
	  ordered_holders_info[saved_pages_cnt].rank = req_watcher->curr_rank;
	}
      else
	{
	  if (VPID_EQ (&(ordered_holders_info[saved_pages_cnt].group_id), req_vpid))
	    {
	      ordered_holders_info[saved_pages_cnt].rank = PGBUF_ORDERED_HEAP_HDR;
	    }
	  else
	    {
	      /* leave rank set by user */
	      ordered_holders_info[saved_pages_cnt].rank = req_watcher->curr_rank;
	    }
	}
      ordered_holders_info[saved_pages_cnt].prevent_dealloc = false;
      saved_pages_cnt++;
    }

  if (saved_pages_cnt > 1)
    {
      qsort (ordered_holders_info, saved_pages_cnt, sizeof (ordered_holders_info[0]), pgbuf_compare_hold_vpid_for_sort);
    }

  /* restore fixes on previously unfixed pages and fix the requested page */
  for (i = 0; i < saved_pages_cnt; i++)
    {
      if (VPID_EQ (req_vpid, &(ordered_holders_info[i].vpid)))
	{
	  curr_request_mode = request_mode;
	  curr_fetch_mode = fetch_mode;
	}
      else
	{
	  curr_request_mode = ordered_holders_info[i].latch_mode;
	  curr_fetch_mode = OLD_PAGE;
	}

#if !defined(NDEBUG)
      pgptr =
	pgbuf_fix_debug (thread_p, &(ordered_holders_info[i].vpid), curr_fetch_mode, curr_request_mode,
			 PGBUF_UNCONDITIONAL_LATCH, caller_file, caller_line, caller_func);
#else
      pgptr =
	pgbuf_fix_release (thread_p, &(ordered_holders_info[i].vpid), curr_fetch_mode, curr_request_mode,
			   PGBUF_UNCONDITIONAL_LATCH);
#endif

      if (pgptr == NULL)
	{
	  er_status = er_errid ();
	  if (er_status == ER_INTERRUPTED)
	    {
	      /* this is expected */
	      goto exit;
	    }
	  if (er_status == ER_PB_BAD_PAGEID)
	    {
	      /* page was probably deallocated? so has the impossible indeed happen?? */
	      assert (false);
	      er_log_debug (ARG_FILE_LINE, "pgbuf_ordered_fix: page %d|%d was deallocated an we told it not to!\n",
			    VPID_AS_ARGS (&ordered_holders_info[i].vpid));
	    }
	  if (!VPID_EQ (req_vpid, &(ordered_holders_info[i].vpid)))
	    {
	      int prev_er_status = er_status;
	      er_status = ER_PB_ORDERED_REFIX_FAILED;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, er_status, 3, ordered_holders_info[i].vpid.volid,
		      ordered_holders_info[i].vpid.pageid, prev_er_status);
	    }
	  goto exit;
	}

      /* get holder of last fix: last fixed pages is in top of holder list, we use parse code just for safety */
      for (holder = pgbuf_Pool.thrd_holder_info[thrd_idx].thrd_hold_list; holder != NULL; holder = holder->thrd_link)
	{
	  if (VPID_EQ (&(holder->bufptr->vpid), &(ordered_holders_info[i].vpid)))
	    {
	      break;
	    }
	}

      assert (holder != NULL);

      if (VPID_EQ (req_vpid, &(ordered_holders_info[i].vpid)))
	{
	  ret_pgptr = pgptr;

	  if (has_dealloc_prevent_flag == true)
	    {
	      CAST_PGPTR_TO_BFPTR (bufptr, pgptr);
	      pgbuf_bcb_unregister_avoid_deallocation (bufptr);
	      has_dealloc_prevent_flag = false;
	    }

	  if (req_watcher != NULL)
	    {
#if !defined(NDEBUG)
	      pgbuf_add_watch_instance_internal (holder, pgptr, req_watcher, request_mode, true, caller_file,
						 caller_line);
#else
	      pgbuf_add_watch_instance_internal (holder, pgptr, req_watcher, request_mode, true);
#endif
	      req_page_has_watcher = true;

#if defined(PGBUF_ORDERED_DEBUG)
	      _er_log_debug (__FILE__, __LINE__,
			     "ordered_fix(%u) : fixed req page, VPID:(%d,%d), GROUP:%d,%d, "
			     "rank:%d, pgptr:%X, holder_fix_count:%d, holder_watch_count:%d, holder_fixed_at:%s, ",
			     ordered_fix_id, ordered_holders_info[i].vpid.volid, ordered_holders_info[i].vpid.pageid,
			     ordered_holders_info[i].group_id.volid, ordered_holders_info[i].group_id.pageid,
			     ordered_holders_info[i].rank, pgptr, holder->fix_count, holder->watch_count,
			     holder->fixed_at);
#endif
	    }
	}
      else
	{
	  int j;

	  /* page is fixed, therefore avoiding deallocate is no longer necessary */
	  assert (ordered_holders_info[i].prevent_dealloc);
	  ordered_holders_info[i].prevent_dealloc = false;
	  CAST_PGPTR_TO_BFPTR (bufptr, pgptr);
	  pgbuf_bcb_unregister_avoid_deallocation (bufptr);

#if !defined (NDEBUG)
	  /* page after re-fix should have the same type as before unfix */
	  (void) pgbuf_check_page_ptype (thread_p, pgptr, ordered_holders_info[i].ptype);
#endif /* !NDEBUG */

#if defined(PGBUF_ORDERED_DEBUG)
	  _er_log_debug (__FILE__, __LINE__,
			 "ordered_fix(%u) : restore_holder:%X, VPID:(%d,%d), pgptr:%X, holder_fix_count:%d, "
			 "holder_watch_count:%d, holder_fixed_at:%s, saved_fix_cnt:%d, saved_watch_cnt:%d",
			 ordered_fix_id, holder, ordered_holders_info[i].vpid.volid,
			 ordered_holders_info[i].vpid.pageid, pgptr, holder->fix_count, holder->watch_count,
			 holder->fixed_at, ordered_holders_info[i].fix_cnt, ordered_holders_info[i].watch_count);
#endif

	  /* restore number of fixes for previously fixed page: just use pgbuf_fix since it is safer */
	  for (j = 1; j < ordered_holders_info[i].watch_count; j++)
	    {
#if !defined(NDEBUG)
	      pgptr =
		pgbuf_fix_debug (thread_p, &(ordered_holders_info[i].vpid), curr_fetch_mode, curr_request_mode,
				 PGBUF_UNCONDITIONAL_LATCH, caller_file, caller_line, caller_func);
#else
	      pgptr =
		pgbuf_fix_release (thread_p, &(ordered_holders_info[i].vpid), curr_fetch_mode, curr_request_mode,
				   PGBUF_UNCONDITIONAL_LATCH);
#endif
	      if (pgptr == NULL)
		{
		  assert_release (false);
		  er_status = ER_FAILED_ASSERTION;
		  goto exit;
		}
	    }

	  for (j = 0; j < ordered_holders_info[i].watch_count; j++)
	    {
#if !defined(NDEBUG)
	      pgbuf_add_watch_instance_internal (holder, pgptr, ordered_holders_info[i].watcher[j],
						 (PGBUF_LATCH_MODE) ordered_holders_info[i].watcher[j]->latch_mode,
						 false, caller_file, caller_line);
#else
	      pgbuf_add_watch_instance_internal (holder, pgptr, ordered_holders_info[i].watcher[j],
						 (PGBUF_LATCH_MODE) ordered_holders_info[i].watcher[j]->latch_mode,
						 false);
#endif
#if defined(PGBUF_ORDERED_DEBUG)
	      _er_log_debug (__FILE__, __LINE__,
			     "ordered_fix(%u) : restore_watcher:%X, GROUP:%d,%d, rank:%d/%d,"
			     " pgptr:%X, holder_fix_count:%d, holder_watch_count:%d, holder_fixed_at:%s",
			     ordered_fix_id, ordered_holders_info[i].watcher[j],
			     ordered_holders_info[i].watcher[j]->group_id.volid,
			     ordered_holders_info[i].watcher[j]->group_id.pageid,
			     ordered_holders_info[i].watcher[j]->curr_rank,
			     ordered_holders_info[i].watcher[j]->initial_rank,
			     ordered_holders_info[i].watcher[j]->pgptr, holder->fix_count, holder->watch_count,
			     holder->fixed_at);
#endif /* PGBUF_ORDERED_DEBUG */
	    }
	}
    }

exit:
  if (er_status_get_hfid != NO_ERROR && er_status == NO_ERROR)
    {
      er_status = er_status_get_hfid;
    }

  assert (er_status != NO_ERROR || !VPID_ISNULL (&(req_watcher->group_id)));

  if (ret_pgptr != NULL && er_status != NO_ERROR)
    {
      if (req_page_has_watcher)
	{
	  pgbuf_ordered_unfix_and_init (thread_p, ret_pgptr, req_watcher);
	}
      else
	{
	  pgbuf_unfix_and_init (thread_p, ret_pgptr);
	}
    }

  if (req_page_has_group == false && ret_pgptr != NULL && req_watcher->curr_rank != PGBUF_ORDERED_HEAP_HDR
      && VPID_EQ (&req_watcher->group_id, req_vpid))
    {
      req_watcher->curr_rank = PGBUF_ORDERED_HEAP_HDR;
    }

  for (i = 0; i < saved_pages_cnt; i++)
    {
      if (ordered_holders_info[i].prevent_dealloc)
	{
	  /* we need to remove prevent deallocate. */
	  PGBUF_BUFFER_HASH *hash_anchor = &pgbuf_Pool.buf_hash_table[PGBUF_HASH_VALUE (&ordered_holders_info[i].vpid)];
	  bufptr = pgbuf_search_hash_chain (thread_p, hash_anchor, &ordered_holders_info[i].vpid);

	  if (bufptr == NULL)
	    {
	      /* oops... no longer in buffer?? */
	      assert (false);
	      pthread_mutex_unlock (&hash_anchor->hash_mutex);
	      continue;
	    }
	  if (!pgbuf_bcb_should_avoid_deallocation (bufptr))
	    {
	      /* oops... deallocate not prevented */
	      assert (false);
	    }
	  else
	    {
	      pgbuf_bcb_unregister_avoid_deallocation (bufptr);
	    }
	  PGBUF_BCB_UNLOCK (bufptr);
	}
    }

  return er_status;
}

/*
 * pgbuf_get_groupid_and_unfix () - retrieves group identifier of page and performs unlatch if requested.
 *   return: error code
 *   req_vpid(in): id of page for which the group is needed (for debug)
 *   pgptr(in): page (already latched); only heap page allowed
 *   groupid(out): group identifier (VPID of HFID)
 *   do_unfix(in): if true, it unfixes the page.
 *
 * Note : helper function of ordered fix.
 */
static int
pgbuf_get_groupid_and_unfix (THREAD_ENTRY * thread_p, const VPID * req_vpid, PAGE_PTR * pgptr, VPID * groupid,
			     bool do_unfix)
{
  OID cls_oid;
  HFID hfid;
  int er_status = NO_ERROR;
  int thrd_idx;

  assert (pgptr != NULL && *pgptr != NULL);
  assert (groupid != NULL);

  VPID_SET_NULL (groupid);

  thrd_idx = thread_get_entry_index (thread_p);

  /* get class oid and hfid */
  er_status = heap_get_class_oid_from_page (thread_p, *pgptr, &cls_oid);

  if (do_unfix == true)
    {
      /* release requested page to avoid deadlocks with catalog pages */
      pgbuf_unfix_and_init (thread_p, *pgptr);
    }

  if (er_status != NO_ERROR)
    {
      return er_status;
    }

  assert (do_unfix == false || *pgptr == NULL);

  if (OID_IS_ROOTOID (&cls_oid))
    {
      boot_find_root_heap (&hfid);
    }
  else
    {
      er_status = heap_get_class_info (thread_p, &cls_oid, &hfid, NULL, NULL);
    }

  if (er_status == NO_ERROR)
    {
      if (HFID_IS_NULL (&hfid))
	{
	  /* the requested page does not belong to a heap */
	  er_status = ER_PB_ORDERED_NO_HEAP;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, er_status, 2, req_vpid->volid, req_vpid->pageid);
	}
      else
	{
	  groupid->volid = hfid.vfid.volid;
	  groupid->pageid = hfid.hpgid;
	  assert (!VPID_ISNULL (groupid));
	}
    }

  return er_status;
}

/*
 * pgbuf_ordered_unfix () - Unfix a page which was previously fixed with ordered_fix (has a page watcher)
 *   return: void
 *   thread_p(in):
 *   watcher_object(in/out): page watcher
 *
 */
#if !defined (NDEBUG)
void
pgbuf_ordered_unfix_debug (THREAD_ENTRY * thread_p, PGBUF_WATCHER * watcher_object, const char *caller_file,
			   int caller_line, const char *caller_func)
#else /* NDEBUG */
void
pgbuf_ordered_unfix (THREAD_ENTRY * thread_p, PGBUF_WATCHER * watcher_object)
#endif				/* NDEBUG */
{
  PGBUF_HOLDER *holder;
  PAGE_PTR pgptr;
  PGBUF_WATCHER *watcher;

  assert (watcher_object != NULL);

#if !defined(NDEBUG)
  assert (watcher_object->magic == PGBUF_WATCHER_MAGIC_NUMBER);
#endif

  if (watcher_object->pgptr == NULL)
    {
      assert_release (false);
      return;
    }

  pgptr = watcher_object->pgptr;

  assert (pgptr != NULL);

  holder = pgbuf_get_holder (thread_p, pgptr);

  assert_release (holder != NULL);

  watcher = holder->last_watcher;
  while (watcher != NULL)
    {
      if (watcher == watcher_object)
	{
	  /* found */
	  break;
	}
      watcher = watcher->prev;
    }

  assert_release (watcher != NULL);

  assert (holder->fix_count >= holder->watch_count);

  pgbuf_remove_watcher (holder, watcher_object);

#if !defined(NDEBUG)
  pgbuf_watcher_init_debug (watcher_object, caller_file, caller_line, false);
  pgbuf_unfix_debug (thread_p, pgptr, caller_file, caller_line, caller_func);
#else
  pgbuf_unfix (thread_p, pgptr);
#endif
}

/*
 * pgbuf_add_watch_instance_internal () - Adds a page watcher for a fixed page
 *   holder(in): holder object
 *   pgptr(in): holder object
 *   watcher(in/out): page watcher
 *   latch_mode(in): latch mode used for fixing the page
 *   clear_unfix_flag(in): True to reset page_was_unfixed flag, false otherwise.
 *
 */
#if !defined(NDEBUG)
STATIC_INLINE void
pgbuf_add_watch_instance_internal (PGBUF_HOLDER * holder, PAGE_PTR pgptr, PGBUF_WATCHER * watcher,
				   const PGBUF_LATCH_MODE latch_mode, const bool clear_unfix_flag,
				   const char *caller_file, const int caller_line)
#else
STATIC_INLINE void
pgbuf_add_watch_instance_internal (PGBUF_HOLDER * holder, PAGE_PTR pgptr, PGBUF_WATCHER * watcher,
				   const PGBUF_LATCH_MODE latch_mode, const bool clear_unfix_flag)
#endif
{
#if !defined(NDEBUG)
  char *p;
#endif
  assert (watcher != NULL);
  assert (pgptr != NULL);
  assert (holder != NULL);

  assert (holder->watch_count < PGBUF_MAX_PAGE_WATCHERS);

  assert (watcher->pgptr == NULL);
  assert (watcher->next == NULL);
  assert (watcher->prev == NULL);

  if (holder->last_watcher == NULL)
    {
      assert (holder->first_watcher == NULL);
      holder->first_watcher = watcher;
      holder->last_watcher = watcher;
    }
  else
    {
      watcher->prev = holder->last_watcher;
      (holder->last_watcher)->next = watcher;
      holder->last_watcher = watcher;
    }

  watcher->pgptr = pgptr;
  watcher->latch_mode = latch_mode;
  if (clear_unfix_flag)
    {
      watcher->page_was_unfixed = false;
    }

  holder->watch_count += 1;

#if !defined(NDEBUG)
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

  snprintf (watcher->watched_at, sizeof (watcher->watched_at) - 1, "%s:%d", p, caller_line);
#endif
}

/*
 * pgbuf_attach_watcher () - Add a watcher to a fixed page.
 *
 * return	   : Void.
 * thread_p (in)   : Thread entry.
 * pgptr (in)	   : Fixed page pointer.
 * latch_mode (in) : Latch mode.
 * hfid (in)	   : Heap file identifier.
 * watcher (out)   : Page water.
 */
#if !defined (NDEBUG)
void
pgbuf_attach_watcher_debug (THREAD_ENTRY * thread_p, PAGE_PTR pgptr, PGBUF_LATCH_MODE latch_mode, HFID * hfid,
			    PGBUF_WATCHER * watcher, const char *caller_file, const int caller_line)
#else /* NDEBUG */
void
pgbuf_attach_watcher (THREAD_ENTRY * thread_p, PAGE_PTR pgptr, PGBUF_LATCH_MODE latch_mode, HFID * hfid,
		      PGBUF_WATCHER * watcher)
#endif				/* NDEBUG */
{
  PGBUF_HOLDER *holder = NULL;
  VPID header_vpid = VPID_INITIALIZER;
  PGBUF_ORDERED_RANK rank;

  assert (pgptr != NULL);
  assert (watcher != NULL);
  assert (hfid != NULL && !HFID_IS_NULL (hfid));

  header_vpid.volid = hfid->vfid.volid;
  header_vpid.pageid = hfid->hpgid;

  /* Set current rank based on page being heap header or not. */
  if (VPID_EQ (&header_vpid, pgbuf_get_vpid_ptr (pgptr)))
    {
      rank = PGBUF_ORDERED_HEAP_HDR;
    }
  else
    {
      rank = PGBUF_ORDERED_HEAP_NORMAL;
    }

  PGBUF_INIT_WATCHER (watcher, rank, hfid);
  watcher->curr_rank = rank;

  holder = pgbuf_get_holder (thread_p, pgptr);
  assert (holder != NULL);

#if !defined (NDEBUG)
  pgbuf_add_watch_instance_internal (holder, pgptr, watcher, latch_mode, true, caller_file, caller_line);
#else
  pgbuf_add_watch_instance_internal (holder, pgptr, watcher, latch_mode, true);
#endif
}

/*
 * pgbuf_get_holder () - Searches holder of fixed page
 *   Return : holder object or NULL if not found
 *   thread_p(in):
 *   pgptr(in): pgptr
 */
static PGBUF_HOLDER *
pgbuf_get_holder (THREAD_ENTRY * thread_p, PAGE_PTR pgptr)
{
  int thrd_idx;
  PGBUF_BCB *bufptr;
  PGBUF_HOLDER *holder;

  assert (pgptr != NULL);
  thrd_idx = thread_get_entry_index (thread_p);

  CAST_PGPTR_TO_BFPTR (bufptr, pgptr);

  for (holder = pgbuf_Pool.thrd_holder_info[thrd_idx].thrd_hold_list; holder != NULL; holder = holder->thrd_link)
    {
      if (bufptr == holder->bufptr)
	{
	  return holder;
	}
    }

  return NULL;
}

/*
 * pgbuf_remove_watcher () - Removes a page watcher
 *   holder(in): holder object
 *   watcher_object(in): watcher object
 */
static void
pgbuf_remove_watcher (PGBUF_HOLDER * holder, PGBUF_WATCHER * watcher_object)
{
  PAGE_PTR pgptr;

  assert (watcher_object != NULL);
  assert (holder != NULL);

#if !defined(NDEBUG)
  assert (watcher_object->magic == PGBUF_WATCHER_MAGIC_NUMBER);
#endif

  pgptr = watcher_object->pgptr;

  if (holder->first_watcher == watcher_object)
    {
      assert (watcher_object->prev == NULL);
      holder->first_watcher = watcher_object->next;
    }
  else if (watcher_object->prev != NULL)
    {
      (watcher_object->prev)->next = watcher_object->next;
    }

  if (holder->last_watcher == watcher_object)
    {
      assert (watcher_object->next == NULL);
      holder->last_watcher = watcher_object->prev;
    }
  else if (watcher_object->next != NULL)
    {
      (watcher_object->next)->prev = watcher_object->prev;
    }
  watcher_object->next = NULL;
  watcher_object->prev = NULL;
  watcher_object->pgptr = NULL;
  watcher_object->curr_rank = PGBUF_ORDERED_RANK_UNDEFINED;
  holder->watch_count -= 1;
}

/*
 * pgbuf_replace_watcher () - Replaces a page watcher with another page watcher
 *   thread_p(in):
 *   old_watcher(in/out): current page watcher to replace
 *   new_watcher(in/out): new page watcher to use
 *
 */
#if !defined(NDEBUG)
void
pgbuf_replace_watcher_debug (THREAD_ENTRY * thread_p, PGBUF_WATCHER * old_watcher, PGBUF_WATCHER * new_watcher,
			     const char *caller_file, const int caller_line)
#else
void
pgbuf_replace_watcher (THREAD_ENTRY * thread_p, PGBUF_WATCHER * old_watcher, PGBUF_WATCHER * new_watcher)
#endif
{
  PGBUF_HOLDER *holder;
  PAGE_PTR page_ptr;
  PGBUF_LATCH_MODE latch_mode;

  assert (old_watcher != NULL);
  assert (PGBUF_IS_CLEAN_WATCHER (new_watcher));

#if !defined(NDEBUG)
  assert (old_watcher->magic == PGBUF_WATCHER_MAGIC_NUMBER);
  assert (new_watcher->magic == PGBUF_WATCHER_MAGIC_NUMBER);
#endif

  assert (old_watcher->pgptr != NULL);

  holder = pgbuf_get_holder (thread_p, old_watcher->pgptr);

  assert_release (holder != NULL);

  page_ptr = old_watcher->pgptr;
  latch_mode = (PGBUF_LATCH_MODE) old_watcher->latch_mode;
  new_watcher->initial_rank = old_watcher->initial_rank;
  new_watcher->curr_rank = old_watcher->curr_rank;
  VPID_COPY (&new_watcher->group_id, &old_watcher->group_id);

  pgbuf_remove_watcher (holder, old_watcher);

#if !defined(NDEBUG)
  pgbuf_watcher_init_debug (old_watcher, caller_file, caller_line, false);
  pgbuf_add_watch_instance_internal (holder, page_ptr, new_watcher, latch_mode, true, caller_file, caller_line);
#else
  pgbuf_add_watch_instance_internal (holder, page_ptr, new_watcher, latch_mode, true);
#endif
}

/*
 * pgbuf_ordered_set_dirty_and_free () - Mark as modified the buffer associated and unfixes the page
 *                                       (previously fixed with ordered fix)
 *   return: void
 *   thread_p(in):
 *   pg_watcher(in): page watcher holding the page to dirty and unfix
 */
void
pgbuf_ordered_set_dirty_and_free (THREAD_ENTRY * thread_p, PGBUF_WATCHER * pg_watcher)
{
  pgbuf_set_dirty (thread_p, pg_watcher->pgptr, DONT_FREE);
  pgbuf_ordered_unfix (thread_p, pg_watcher);
}

/*
 * pgbuf_get_condition_for_ordered_fix () - returns the condition which should
 *  be used to latch (vpid_new_page) knowing that we already have a latch on
 *  (vpid_fixed_page)
 *
 *   return: latch condition (PGBUF_LATCH_CONDITION)
 *   vpid_new_page(in):
 *   vpid_fixed_page(in):
 *   hfid(in): HFID of both pages
 *
 *  Note: This is intended only for HEAP/HEAP_OVERFLOW pages.
 *	  The user should make sure both pages belong to the same heap.
 *	  To be used when pgbuf_ordered_fix is not possible:
 *	  In vacuum context, unfixing a older page to prevent deadlatch,
 *	  requires flushing of the old page first - this is not possible with
 *	  pgbuf_ordered_fix.
 */
int
pgbuf_get_condition_for_ordered_fix (const VPID * vpid_new_page, const VPID * vpid_fixed_page, const HFID * hfid)
{
  PGBUF_HOLDER_INFO new_page_holder_info;
  PGBUF_HOLDER_INFO fixed_page_holder_info;

  new_page_holder_info.group_id.volid = hfid->vfid.volid;
  new_page_holder_info.group_id.pageid = hfid->hpgid;
  fixed_page_holder_info.group_id.volid = hfid->vfid.volid;
  fixed_page_holder_info.group_id.pageid = hfid->hpgid;

  VPID_COPY (&new_page_holder_info.vpid, vpid_new_page);
  VPID_COPY (&fixed_page_holder_info.vpid, vpid_fixed_page);

  if (VPID_EQ (&new_page_holder_info.group_id, &new_page_holder_info.vpid))
    {
      new_page_holder_info.rank = PGBUF_ORDERED_HEAP_HDR;
    }
  else
    {
      new_page_holder_info.rank = PGBUF_ORDERED_HEAP_NORMAL;
    }

  if (VPID_EQ (&fixed_page_holder_info.group_id, &fixed_page_holder_info.vpid))
    {
      fixed_page_holder_info.rank = PGBUF_ORDERED_HEAP_HDR;
    }
  else
    {
      fixed_page_holder_info.rank = PGBUF_ORDERED_HEAP_NORMAL;
    }

  if (pgbuf_compare_hold_vpid_for_sort (&new_page_holder_info, &fixed_page_holder_info) < 0)
    {
      return PGBUF_CONDITIONAL_LATCH;
    }

  return PGBUF_UNCONDITIONAL_LATCH;
}

#if !defined(NDEBUG)
/*
 * pgbuf_watcher_init_debug () -
 *   return: void
 *   watcher(in/out):
 *   add(in): if add or reset the "init" field
 */
void
pgbuf_watcher_init_debug (PGBUF_WATCHER * watcher, const char *caller_file, const int caller_line, bool add)
{
  char *p;

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

  if (add)
    {
      char prev_init[256];
      strncpy (prev_init, watcher->init_at, sizeof (watcher->init_at) - 1);
      prev_init[sizeof (prev_init) - 1] = '\0';
      snprintf_dots_truncate (watcher->init_at, sizeof (watcher->init_at) - 1, "%s:%d %s", p, caller_line, prev_init);
    }
  else
    {
      snprintf (watcher->init_at, sizeof (watcher->init_at) - 1, "%s:%d", p, caller_line);
    }
}

/*
 * pgbuf_is_page_fixed_by_thread () -
 *   return: true if page is already fixed, false otherwise
 *   thread_p(in): thread entry
 *   vpid_p(in): virtual page id
 */
bool
pgbuf_is_page_fixed_by_thread (THREAD_ENTRY * thread_p, const VPID * vpid_p)
{
  int thrd_index;
  PGBUF_HOLDER_ANCHOR *thrd_holder_info;
  PGBUF_HOLDER *thrd_holder;
  assert (vpid_p != NULL);

  /* walk holders and try to find page */
  thrd_index = thread_get_entry_index (thread_p);
  thrd_holder_info = &(pgbuf_Pool.thrd_holder_info[thrd_index]);
  for (thrd_holder = thrd_holder_info->thrd_hold_list; thrd_holder != NULL; thrd_holder = thrd_holder->next_holder)
    {
      if (VPID_EQ (&thrd_holder->bufptr->vpid, vpid_p))
	{
	  return true;
	}
    }
  return false;
}
#endif

/*
 * pgbuf_initialize_page_quota_parameters () - Initializes page quota parameters
 *
 *   return: NO_ERROR, or ER_code
 *
 *   Note: Call this before any LRU initialization
 */
static int
pgbuf_initialize_page_quota_parameters (void)
{
  PGBUF_PAGE_QUOTA *quota;

  quota = &(pgbuf_Pool.quota);
  memset (quota, 0, sizeof (PGBUF_PAGE_QUOTA));

  tsc_getticks (&quota->last_adjust_time);
  quota->adjust_age = 0;
  quota->is_adjusting = 0;

#if defined (SERVER_MODE)
  quota->num_private_LRU_list = prm_get_integer_value (PRM_ID_PB_NUM_PRIVATE_CHAINS);
  if (quota->num_private_LRU_list == -1)
    {
      /* set value automatically to maximum number of workers (active and vacuum). */
      quota->num_private_LRU_list = MAX_NTRANS + VACUUM_MAX_WORKER_COUNT;
    }
  else if (quota->num_private_LRU_list == 0)
    {
      /* disabled */
    }
  else
    {
      /* set number of workers to the number desired by user (or to minimum accepted) */
      if (quota->num_private_LRU_list < PGBUF_PRIVATE_LRU_MIN_COUNT)
	{
	  /* set to minimum count */
	  quota->num_private_LRU_list = PGBUF_PRIVATE_LRU_MIN_COUNT;
	}
    }
#else	/* !SERVER_MODE */		   /* SA_MODE */
  /* stand-alone quota is disabled */
  quota->num_private_LRU_list = 0;
#endif /* SA_MODE */

  return NO_ERROR;
}

/*
 * pgbuf_initialize_page_quota () - Initializes page quota
 *   return: NO_ERROR, or ER_code
 */
static int
pgbuf_initialize_page_quota (void)
{
  PGBUF_PAGE_QUOTA *quota;
  int i;
  int error_status = NO_ERROR;

  quota = &(pgbuf_Pool.quota);

  quota->lru_victim_flush_priority_per_lru =
    (float *) malloc (PGBUF_TOTAL_LRU_COUNT * sizeof (quota->lru_victim_flush_priority_per_lru[0]));
  if (quota->lru_victim_flush_priority_per_lru == NULL)
    {
      error_status = ER_OUT_OF_VIRTUAL_MEMORY;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
	      1, (PGBUF_TOTAL_LRU_COUNT * sizeof (quota->lru_victim_flush_priority_per_lru[0])));
      goto exit;
    }

  quota->private_lru_session_cnt =
    (int *) malloc (PGBUF_PRIVATE_LRU_COUNT * sizeof (quota->private_lru_session_cnt[0]));
  if (quota->private_lru_session_cnt == NULL)
    {
      error_status = ER_OUT_OF_VIRTUAL_MEMORY;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
	      1, (PGBUF_TOTAL_LRU_COUNT * sizeof (quota->private_lru_session_cnt[0])));
      goto exit;
    }

  /* initialize the quota data for each LRU */
  for (i = 0; i < PGBUF_TOTAL_LRU_COUNT; i++)
    {
      quota->lru_victim_flush_priority_per_lru[i] = 0;

      if (PGBUF_IS_PRIVATE_LRU_INDEX (i))
	{
	  quota->private_lru_session_cnt[PGBUF_PRIVATE_LIST_FROM_LRU_INDEX (i)] = 0;
	}
    }

  if (PGBUF_PAGE_QUOTA_IS_ENABLED)
    {
      quota->private_pages_ratio = 1.0f;
    }
  else
    {
      quota->private_pages_ratio = 0;
    }

  quota->add_shared_lru_idx = 0;
  quota->avoid_shared_lru_idx = -1;

exit:
  return error_status;
}

/*
 * pgbuf_initialize_page_monitor () - Initializes page monitor
 *   return: NO_ERROR, or ER_code
 */
static int
pgbuf_initialize_page_monitor (void)
{
  PGBUF_PAGE_MONITOR *monitor;
  int i;
  int error_status = NO_ERROR;
#if defined (SERVER_MODE)
  size_t count_threads = thread_num_total_threads ();
#endif /* SERVER_MODE */

  monitor = &(pgbuf_Pool.monitor);

  memset (monitor, 0, sizeof (PGBUF_PAGE_MONITOR));

  monitor->lru_hits = (int *) malloc (PGBUF_TOTAL_LRU_COUNT * sizeof (monitor->lru_hits[0]));
  if (monitor->lru_hits == NULL)
    {
      error_status = ER_OUT_OF_VIRTUAL_MEMORY;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
	      1, (PGBUF_TOTAL_LRU_COUNT * sizeof (monitor->lru_hits[0])));
      goto exit;
    }

  monitor->lru_activity = (int *) malloc (PGBUF_TOTAL_LRU_COUNT * sizeof (monitor->lru_activity[0]));
  if (monitor->lru_activity == NULL)
    {
      error_status = ER_OUT_OF_VIRTUAL_MEMORY;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
	      1, (PGBUF_TOTAL_LRU_COUNT * sizeof (monitor->lru_activity[0])));
      goto exit;
    }

  /* initialize the monitor data for each LRU */
  for (i = 0; i < PGBUF_TOTAL_LRU_COUNT; i++)
    {
      monitor->lru_hits[i] = 0;
      monitor->lru_activity[i] = 0;
    }

  monitor->lru_victim_req_cnt = 0;
  monitor->fix_req_cnt = 0;
  monitor->pg_unfix_cnt = 0;
  monitor->lru_shared_pgs_cnt = 0;

#if defined (SERVER_MODE)
  if (pgbuf_Monitor_locks)
    {
      monitor->bcb_locks = (PGBUF_MONITOR_BCB_MUTEX *) calloc (count_threads, sizeof (PGBUF_MONITOR_BCB_MUTEX));
      if (monitor->bcb_locks == NULL)
	{
	  error_status = ER_OUT_OF_VIRTUAL_MEMORY;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
		  count_threads * sizeof (PGBUF_MONITOR_BCB_MUTEX));
	  goto exit;
	}
    }
#endif /* SERVER_MDOE */

  /* no bcb's, no victims */
  monitor->victim_rich = false;

exit:
  return error_status;
}

/*
 * pgbuf_compute_lru_vict_target () -
 *
 * lru_sum_flush_priority(out) : sum of all flush priorities of all LRUs
 * return : void
 */
static void
pgbuf_compute_lru_vict_target (float *lru_sum_flush_priority)
{
  int i;

  float prv_quota;
  float prv_real_ratio;
  float diff;
  float prv_flush_ratio;
  float shared_flush_ratio;

  bool use_prv_size = false;

  int total_prv_target = 0;
  int this_prv_target = 0;

  PGBUF_LRU_LIST *lru_list;

  assert (lru_sum_flush_priority != NULL);

  *lru_sum_flush_priority = 0;

  prv_quota = pgbuf_Pool.quota.private_pages_ratio;
  assert (pgbuf_Pool.monitor.lru_shared_pgs_cnt >= 0
	  && pgbuf_Pool.monitor.lru_shared_pgs_cnt <= pgbuf_Pool.num_buffers);

  prv_real_ratio = 1.0f - ((float) pgbuf_Pool.monitor.lru_shared_pgs_cnt / pgbuf_Pool.num_buffers);
  diff = prv_quota - prv_real_ratio;

  prv_flush_ratio = prv_real_ratio * (1.0f - diff);
  prv_flush_ratio = MIN (1.0f, prv_flush_ratio);

  for (i = PGBUF_LRU_INDEX_FROM_PRIVATE (0); i < PGBUF_TOTAL_LRU_COUNT; i++)
    {
      lru_list = PGBUF_GET_LRU_LIST (i);

      /* note: we target especially over quota private lists or close to quota. we cannot target only over quota lists
       * (I tried), because you may find yourself in the peculiar case where quota's are on par with list size, while
       * shared are right below minimum desired size... and flush will not find anything.
       */
      this_prv_target = PGBUF_LRU_LIST_COUNT (lru_list) - (int) (lru_list->quota * 0.9);
      this_prv_target = MIN (this_prv_target, lru_list->count_lru3);
      if (this_prv_target > 0)
	{
	  total_prv_target += this_prv_target;
	}
    }
  if (total_prv_target == 0)
    {
      /* can we victimize from shared? */
      if (pgbuf_Pool.monitor.lru_shared_pgs_cnt
	  <= (int) (pgbuf_Pool.num_LRU_list * PGBUF_MIN_SHARED_LIST_ADJUST_SIZE
		    * (pgbuf_Pool.ratio_lru1 + pgbuf_Pool.ratio_lru2)))
	{
	  /* we won't be able to victimize from shared. this is a backup hack, I don't like to rely on it. let's
	   * find smarter ways to avoid the case. */
	  /* right now, considering we target all bcb's beyond 90% of quota, but total_prv_target is 0, that means all
	   * private bcb's must be less than 90% of buffer. that means shared bcb's have to be 10% or more of buffer.
	   * PGBUF_MIN_SHARED_LIST_ADJUST_SIZE is currently set to 50, which is 5% to targeted 1k shared list size.
	   * we shouldn't be here unless I messed up the calculus. */
	  if (pgbuf_Pool.buf_invalid_list.invalid_cnt > 0)
	    {
	      /* This is not really an interesting case.
	       * Probably both shared and private are small and most of buffers in invalid list.
	       * We don't really need flush for the case, since BCB could be allocated from invalid list.
	       */
	      return;
	    }

	  assert (false);
	  use_prv_size = true;
	  prv_flush_ratio = 1.0f;
	  /* we can compute the zone 3 total size (for privates, zones 1 & 2 are both set to minimum ratio). */
	  total_prv_target =
	    (int) ((pgbuf_Pool.num_buffers - pgbuf_Pool.monitor.lru_shared_pgs_cnt)
		   * (1.0f - 2 * PGBUF_LRU_ZONE_MIN_RATIO));
	}
    }
  shared_flush_ratio = 1.0f - prv_flush_ratio;

  for (i = 0; i < PGBUF_TOTAL_LRU_COUNT; i++)
    {
      lru_list = PGBUF_GET_LRU_LIST (i);
      if (PGBUF_IS_SHARED_LRU_INDEX (i))
	{
	  pgbuf_Pool.quota.lru_victim_flush_priority_per_lru[i] = shared_flush_ratio / (float) PGBUF_SHARED_LRU_COUNT;
	}
      else if (PGBUF_IS_PRIVATE_LRU_INDEX (i))
	{
	  if (prv_flush_ratio == 0.0f)
	    {
	      pgbuf_Pool.quota.lru_victim_flush_priority_per_lru[i] = 0.0f;
	    }
	  else
	    {
	      if (use_prv_size)
		{
		  /* back plan: use zone 3 size instead of computed target based on quota. */
		  this_prv_target = lru_list->count_lru3;
		}
	      else
		{
		  /* use bcb's over 90% of quota as flush target */
		  this_prv_target = PGBUF_LRU_LIST_COUNT (lru_list) - (int) (lru_list->quota * 0.9);
		  this_prv_target = MIN (this_prv_target, lru_list->count_lru3);
		}
	      if (this_prv_target > 0)
		{
		  pgbuf_Pool.quota.lru_victim_flush_priority_per_lru[i] =
		    prv_flush_ratio * ((float) this_prv_target / (float) total_prv_target);
		}
	      else
		{
		  pgbuf_Pool.quota.lru_victim_flush_priority_per_lru[i] = 0.0f;
		}
	    }
	}
      else
	{
	  pgbuf_Pool.quota.lru_victim_flush_priority_per_lru[i] = 0.0f;
	}
      *lru_sum_flush_priority += pgbuf_Pool.quota.lru_victim_flush_priority_per_lru[i];
    }
}

/*
 * pgbuf_adjust_quotas () - Adjusts the quotas for private LRU's. The quota's are decided based on thread activities on
 *                          private and shared lists. Activity is counted as number of accessed pages.
 *                          Based on quota's, the thread also sets zone thresholds for each LRU.
 *
 * return        : void
 * thread_p (in) : thread entry
 */
void
pgbuf_adjust_quotas (THREAD_ENTRY * thread_p)
{
#define MAX_PRIVATE_RATIO 0.998f
#define MIN_PRIVATE_RATIO 0.01f

  PGBUF_PAGE_QUOTA *quota;
  PGBUF_PAGE_MONITOR *monitor;
  int i;
  int all_private_quota;
  int sum_private_lru_activity_total = 0;
  TSC_TICKS curr_tick;
  INT64 diff_usec;
  int lru_hits;
  int lru_shared_hits = 0;
  int lru_private_hits = 0;
  float private_ratio;
  int avg_shared_lru_size;
  int shared_threshold_lru1;
  int shared_threshold_lru2;
  int new_quota;
  float new_lru_ratio;
  const INT64 onesec_usec = 1000000LL;
  const INT64 tensec_usec = 10 * onesec_usec;
  int total_victims = 0;
  bool low_overall_activity = false;

  PGBUF_LRU_LIST *lru_list;

  if (thread_p == NULL)
    {
      assert (thread_p != NULL);
      thread_p = thread_get_thread_entry_info ();
    }

  quota = &(pgbuf_Pool.quota);
  monitor = &(pgbuf_Pool.monitor);

  if (!PGBUF_PAGE_QUOTA_IS_ENABLED || quota->is_adjusting)
    {
      return;
    }

  quota->is_adjusting = 1;

  tsc_getticks (&curr_tick);
  diff_usec = tsc_elapsed_utime (curr_tick, quota->last_adjust_time);
  if (diff_usec < 1000LL)
    {
      /* less than 1 msec. stop */
      quota->is_adjusting = 0;
      return;
    }

  /* quota adjust if :
   * - or more than 500 msec since last adjustment and activity is more than threshold
   * - or more than 5 min since last adjustment and activity is more 1% of threshold
   * Activity of page buffer is measured in number of page unfixes
   */
  if (pgbuf_Pool.monitor.pg_unfix_cnt < PGBUF_TRAN_THRESHOLD_ACTIVITY && diff_usec < 500000LL)
    {
      quota->is_adjusting = 0;
      return;
    }
  if (ATOMIC_TAS_32 (&monitor->pg_unfix_cnt, 0) < PGBUF_TRAN_THRESHOLD_ACTIVITY / 100)
    {
      low_overall_activity = true;
    }

  quota->last_adjust_time = curr_tick;

  (void) ATOMIC_INC_32 (&quota->adjust_age, 1);

  /* process hits since last adjust:
   * 1. collect lru_private_hits and lru_shared_hits.
   * 2. update each private list activity.
   * 3. collect total activity.
   */
  for (i = 0; i < PGBUF_TOTAL_LRU_COUNT; i++)
    {
      /* get hits since last adjust and reset */
      lru_hits = ATOMIC_TAS_32 (&monitor->lru_hits[i], 0);
      /* compute hits per second */
      lru_hits = (int) (onesec_usec * lru_hits / diff_usec);

      if (PGBUF_IS_PRIVATE_LRU_INDEX (i))
	{
	  /* adjust private lru activity. for convenience reasons, we consider that previous lru_activity value was same
	   * for 10 seconds minus the time since last adjustment. if previous adjustment is more than 10 seconds old
	   * then we set new activity. */
	  if (diff_usec >= tensec_usec)
	    {
	      /* set current activity */
	      monitor->lru_activity[i] = lru_hits;
	    }
	  else
	    {
	      /* interpolate old activity with new activity */
	      monitor->lru_activity[i] =
		(int) (((tensec_usec - diff_usec) * monitor->lru_activity[i] + diff_usec * lru_hits) / tensec_usec);
	    }
	  /* collect to total activity */
	  sum_private_lru_activity_total += monitor->lru_activity[i];

	  /* collect to total private hits */
	  lru_private_hits += lru_hits;
	}
      else
	{
	  /* collect to total shared hits */
	  lru_shared_hits += lru_hits;
	}

      lru_list = PGBUF_GET_LRU_LIST (i);
      total_victims += lru_list->count_vict_cand;
    }

  /* compute private ratio */
  if (low_overall_activity)
    {
      private_ratio = MIN_PRIVATE_RATIO;
    }
  else
    {
      /* avoid division by 0 */
      lru_shared_hits = MAX (1, lru_shared_hits);
      private_ratio = (float) (lru_private_hits) / (float) (lru_private_hits + lru_shared_hits);
      private_ratio = MIN (MAX_PRIVATE_RATIO, private_ratio);
      private_ratio = MAX (MIN_PRIVATE_RATIO, private_ratio);
    }
  if (diff_usec >= tensec_usec)
    {
      quota->private_pages_ratio = private_ratio;
    }
  else
    {
      quota->private_pages_ratio =
	((quota->private_pages_ratio * (float) (tensec_usec - diff_usec) + private_ratio * (float) diff_usec)
	 / (float) tensec_usec);
    }

  if (sum_private_lru_activity_total == 0)
    {
      /* no private activity */
      /* well I guess we can just set all quota's to 0. */
      all_private_quota = 0;
      for (i = PGBUF_SHARED_LRU_COUNT; i < PGBUF_TOTAL_LRU_COUNT; i++)
	{
	  lru_list = PGBUF_GET_LRU_LIST (i);

	  lru_list->quota = 0;
	  lru_list->threshold_lru1 = 0;
	  lru_list->threshold_lru2 = 0;
	  if (lru_list->count_lru1 + lru_list->count_lru2 > 0)
	    {
	      pthread_mutex_lock (&lru_list->mutex);
	      pgbuf_lru_adjust_zones (thread_p, lru_list, false);
	      pthread_mutex_unlock (&lru_list->mutex);
	      PGBUF_BCB_CHECK_MUTEX_LEAKS ();
	    }
	  if (lru_list->count_vict_cand > 0 && PGBUF_LRU_LIST_IS_OVER_QUOTA (lru_list))
	    {
	      /* make sure this is added to victim list */
	      if (pgbuf_lfcq_add_lru_with_victims (lru_list)
		  && perfmon_is_perf_tracking_and_active (PERFMON_ACTIVATION_FLAG_PB_VICTIMIZATION))
		{
		  /* added to queue of lru lists having victims. */
		}
	    }
	}
    }
  else
    {
      /* compute all_private_quota in number of bcb's */
      all_private_quota =
	(int) ((pgbuf_Pool.num_buffers - pgbuf_Pool.buf_invalid_list.invalid_cnt) * quota->private_pages_ratio);

      /* split private bcb's quota's based on activity */
      for (i = PGBUF_SHARED_LRU_COUNT; i < PGBUF_TOTAL_LRU_COUNT; i++)
	{
	  if (monitor->lru_activity[i] > 0)
	    {
	      new_lru_ratio = (float) monitor->lru_activity[i] / (float) sum_private_lru_activity_total;
	    }
	  else
	    {
	      new_lru_ratio = 0.0f;
	    }

	  new_quota = (int) (new_lru_ratio * all_private_quota);
	  new_quota = MIN (new_quota, PGBUF_PRIVATE_LRU_MAX_HARD_QUOTA);
	  new_quota = MIN (new_quota, pgbuf_Pool.num_buffers / 2);

	  lru_list = PGBUF_GET_LRU_LIST (i);
	  lru_list->quota = new_quota;
	  lru_list->threshold_lru1 = (int) (new_quota * PGBUF_LRU_ZONE_MIN_RATIO);
	  lru_list->threshold_lru2 = (int) (new_quota * PGBUF_LRU_ZONE_MIN_RATIO);

	  if (PGBUF_LRU_LIST_IS_ONE_TWO_OVER_QUOTA (lru_list))
	    {
	      pthread_mutex_lock (&lru_list->mutex);
	      pgbuf_lru_adjust_zones (thread_p, lru_list, false);
	      pthread_mutex_unlock (&lru_list->mutex);

	      PGBUF_BCB_CHECK_MUTEX_LEAKS ();
	    }
	  if (lru_list->count_vict_cand > 0 && PGBUF_LRU_LIST_IS_OVER_QUOTA (lru_list))
	    {
	      /* make sure this is added to victim list */
	      if (pgbuf_lfcq_add_lru_with_victims (lru_list)
		  && perfmon_is_perf_tracking_and_active (PERFMON_ACTIVATION_FLAG_PB_VICTIMIZATION))
		{
		  /* added to queue of lru lists having victims. */
		}
	    }
	}
    }

  /* set shared target size */
  avg_shared_lru_size = (pgbuf_Pool.num_buffers - all_private_quota) / pgbuf_Pool.num_LRU_list;
  avg_shared_lru_size = MAX (avg_shared_lru_size, PGBUF_MIN_SHARED_LIST_ADJUST_SIZE);
  shared_threshold_lru1 = (int) (avg_shared_lru_size * pgbuf_Pool.ratio_lru1);
  shared_threshold_lru2 = (int) (avg_shared_lru_size * pgbuf_Pool.ratio_lru2);
  for (i = 0; i < PGBUF_SHARED_LRU_COUNT; i++)
    {
      lru_list = PGBUF_GET_LRU_LIST (i);
      lru_list->threshold_lru1 = shared_threshold_lru1;
      lru_list->threshold_lru2 = shared_threshold_lru2;

      if (PGBUF_LRU_ARE_ZONES_ONE_TWO_OVER_THRESHOLD (lru_list))
	{
	  pthread_mutex_lock (&lru_list->mutex);
	  pgbuf_lru_adjust_zones (thread_p, lru_list, false);
	  pthread_mutex_unlock (&lru_list->mutex);
	}

      if (lru_list->count_vict_cand > 0)
	{
	  /* make sure this is added to victim list */
	  if (pgbuf_lfcq_add_lru_with_victims (lru_list)
	      && perfmon_is_perf_tracking_and_active (PERFMON_ACTIVATION_FLAG_PB_VICTIMIZATION))
	    {
	      /* added to queue of lru lists having victims. */
	    }
	}
    }

  /* is pool victim rich? we consider this true if the victim count is more than 10% of page buffer. I think we could
   * lower the bar a little bit */
  pgbuf_Pool.monitor.victim_rich = total_victims >= (int) (0.1 * pgbuf_Pool.num_buffers);

  quota->is_adjusting = 0;
}

/*
 * pgbuf_assign_private_lru () -
 *
 *   return: NO_ERROR
 */
int
pgbuf_assign_private_lru (THREAD_ENTRY * thread_p)
{
  int i;
  int min_activitity;
  int min_bcbs;
  int lru_cand_idx, lru_cand_zero_sessions;
  int private_idx;
  int cnt_lru;
  PGBUF_PAGE_MONITOR *monitor;
  PGBUF_PAGE_QUOTA *quota;
  int retry_cnt = 0;

  if (!PGBUF_PAGE_QUOTA_IS_ENABLED)
    {
      return -1;
    }

  monitor = &pgbuf_Pool.monitor;
  quota = &pgbuf_Pool.quota;

  /* Priority for choosing a private list :
   * 1. the list with zero sessions having the least number of pages
   * 2. the list having least activity */

retry:
  lru_cand_zero_sessions = -1;
  lru_cand_idx = -1;
  min_bcbs = pgbuf_Pool.num_buffers;
  min_activitity = PGBUF_TRAN_MAX_ACTIVITY;
  for (i = PGBUF_SHARED_LRU_COUNT; i < PGBUF_TOTAL_LRU_COUNT; i++)
    {
      if (quota->private_lru_session_cnt[PGBUF_PRIVATE_LIST_FROM_LRU_INDEX (i)] == 0)
	{
	  cnt_lru = PGBUF_LRU_LIST_COUNT (PGBUF_GET_LRU_LIST (i));
	  if (cnt_lru < min_bcbs)
	    {
	      min_bcbs = cnt_lru;
	      lru_cand_zero_sessions = i;

	      if (min_bcbs <= 0)
		{
		  break;
		}
	    }
	}
      if (monitor->lru_activity[i] < min_activitity)
	{
	  min_activitity = monitor->lru_activity[i];
	  lru_cand_idx = i;
	}
    }

  if (lru_cand_zero_sessions != -1)
    {
      lru_cand_idx = lru_cand_zero_sessions;
    }

  assert (lru_cand_idx != -1);

  cnt_lru = PGBUF_LRU_LIST_COUNT (PGBUF_GET_LRU_LIST (lru_cand_idx));

  private_idx = PGBUF_PRIVATE_LIST_FROM_LRU_INDEX (lru_cand_idx);

  if (lru_cand_zero_sessions != -1)
    {
      if (ATOMIC_INC_32 (&quota->private_lru_session_cnt[private_idx], 1) > 1)
	{
	  /* another thread stole this lru, retry */
	  if (retry_cnt++ < 5)
	    {
	      ATOMIC_INC_32 (&quota->private_lru_session_cnt[private_idx], -1);
	      goto retry;
	    }
	}
    }
  else
    {
      ATOMIC_INC_32 (&quota->private_lru_session_cnt[private_idx], 1);
    }

  /* TODO: is this necessary? */
  pgbuf_adjust_quotas (thread_p);

  return private_idx;
}

/*
 * pgbuf_release_private_lru () -
 *   return: NO_ERROR
 *   bufptr(in): pointer to buffer page
 *
 * Note: This function puts BCB to the bottom of the LRU list.
 */
int
pgbuf_release_private_lru (THREAD_ENTRY * thread_p, const int private_idx)
{
  if (PGBUF_PAGE_QUOTA_IS_ENABLED && private_idx >= 0 && private_idx < PGBUF_PRIVATE_LRU_COUNT
      && pgbuf_Pool.num_buffers > 0)
    {
      if (ATOMIC_INC_32 (&pgbuf_Pool.quota.private_lru_session_cnt[private_idx], -1) <= 0)
	{
	  ATOMIC_TAS_32 (&pgbuf_Pool.monitor.lru_activity[PGBUF_LRU_INDEX_FROM_PRIVATE (private_idx)], 0);
	  /* TODO: is this necessary? */
	  pgbuf_adjust_quotas (thread_p);
	}
    }
  return NO_ERROR;
}

/*
 * pgbuf_initialize_seq_flusher () - Initializes sequential flusher on a list of pages to be flushed
 *
 *   return: error code
 *   seq_flusher(in/out):
 *   f_list(in/out): flush list to use or NULL if needs to be allocated
 *   cnt(in/out): size of flush list
 */
static int
pgbuf_initialize_seq_flusher (PGBUF_SEQ_FLUSHER * seq_flusher, PGBUF_VICTIM_CANDIDATE_LIST * f_list, const int cnt)
{
  int alloc_size;

  memset (seq_flusher, 0, sizeof (*seq_flusher));
  seq_flusher->flush_max_size = cnt;

  if (f_list != NULL)
    {
      seq_flusher->flush_list = f_list;
    }
  else
    {
      alloc_size = seq_flusher->flush_max_size * sizeof (seq_flusher->flush_list[0]);
      seq_flusher->flush_list = (PGBUF_VICTIM_CANDIDATE_LIST *) malloc (alloc_size);
      if (seq_flusher->flush_list == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, alloc_size);
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}
    }
  seq_flusher->flush_cnt = 0;
  seq_flusher->flush_idx = 0;
  seq_flusher->burst_mode = true;

  seq_flusher->control_intervals_cnt = 0;
  seq_flusher->control_flushed = 0;

  return NO_ERROR;
}

/*
 * pgbuf_has_any_waiters () - Quick check if page has any waiters.
 *
 * return     : True if page has any waiters, false otherwise.
 * pgptr (in) : Page pointer.
 */
bool
pgbuf_has_any_waiters (PAGE_PTR pgptr)
{
#if defined (SERVER_MODE)
  PGBUF_BCB *bufptr = NULL;
  bool has_waiter;

  /* note: we rule out flush waiters here */

  assert (pgptr != NULL);
  CAST_PGPTR_TO_BFPTR (bufptr, pgptr);

  PGBUF_BCB_LOCK (bufptr);
  has_waiter = pgbuf_is_exist_blocked_reader_writer (bufptr);
  PGBUF_BCB_UNLOCK (bufptr);
  return has_waiter;
#else
  return false;
#endif
}

/*
 * pgbuf_has_any_non_vacuum_waiters () - Check if page has any non-vacuum waiters.
 *
 * return     : True if page has waiters, false otherwise.
 * pgptr (in) : Page pointer.
 */
bool
pgbuf_has_any_non_vacuum_waiters (PAGE_PTR pgptr)
{
#if defined (SERVER_MODE)
  PGBUF_BCB *bufptr = NULL;
  THREAD_ENTRY *thread_entry_p;

  assert (pgptr != NULL);
  CAST_PGPTR_TO_BFPTR (bufptr, pgptr);

  thread_entry_p = bufptr->next_wait_thrd;
  while (thread_entry_p != NULL)
    {
      if (thread_entry_p->type != TT_VACUUM_WORKER)
	{
	  return true;
	}
      thread_entry_p = thread_entry_p->next_wait_thrd;
    }

  return false;
#else
  return false;
#endif
}

/*
 * pgbuf_has_prevent_dealloc () - Quick check if page has any scanners.
 *
 * return     : True if page has any waiters, false otherwise.
 * pgptr (in) : Page pointer.
 */
bool
pgbuf_has_prevent_dealloc (PAGE_PTR pgptr)
{
#if defined (SERVER_MODE)
  PGBUF_BCB *bufptr = NULL;

  assert (pgptr != NULL);
  CAST_PGPTR_TO_BFPTR (bufptr, pgptr);

  return pgbuf_bcb_should_avoid_deallocation (bufptr);
#else
  return false;
#endif
}

void
pgbuf_peek_stats (UINT64 * fixed_cnt, UINT64 * dirty_cnt, UINT64 * lru1_cnt, UINT64 * lru2_cnt, UINT64 * lru3_cnt,
		  UINT64 * victim_candidates, UINT64 * avoid_dealloc_cnt, UINT64 * avoid_victim_cnt,
		  UINT64 * private_quota, UINT64 * private_cnt, UINT64 * alloc_bcb_waiter_high,
		  UINT64 * alloc_bcb_waiter_med, UINT64 * flushed_bcbs_waiting_direct_assign,
		  UINT64 * lfcq_big_prv_num, UINT64 * lfcq_prv_num, UINT64 * lfcq_shr_num)
{
  PGBUF_BCB *bufptr;
  int i;
  int bcb_flags;
  PGBUF_ZONE zone;

  *fixed_cnt = 0;
  *dirty_cnt = 0;
  *lru1_cnt = 0;
  *lru2_cnt = 0;
  *lru3_cnt = 0;
  *avoid_dealloc_cnt = 0;
  *avoid_victim_cnt = 0;
  *private_cnt = 0;
  *victim_candidates = 0;

  for (i = 0; i < pgbuf_Pool.num_buffers; i++)
    {
      bufptr = PGBUF_FIND_BCB_PTR (i);
      if (bufptr->fcnt > 0)
	{
	  *fixed_cnt = *fixed_cnt + 1;
	}

      /* copy flags. we do not lock the bcb and we can be affected by concurrent changes. */
      bcb_flags = bufptr->flags;
      if (bcb_flags & PGBUF_BCB_DIRTY_FLAG)
	{
	  *dirty_cnt = *dirty_cnt + 1;
	}

      zone = PGBUF_GET_ZONE (bcb_flags);
      if (zone == PGBUF_LRU_1_ZONE)
	{
	  *lru1_cnt = *lru1_cnt + 1;
	}
      else if (zone == PGBUF_LRU_2_ZONE)
	{
	  *lru2_cnt = *lru2_cnt + 1;
	}
      else if (zone == PGBUF_LRU_3_ZONE)
	{
	  *lru3_cnt = *lru3_cnt + 1;
	}

      if (pgbuf_bcb_should_avoid_deallocation (bufptr))
	{
	  *avoid_dealloc_cnt = *avoid_dealloc_cnt + 1;
	}

      if (bcb_flags & PGBUF_BCB_FLUSHING_TO_DISK_FLAG)
	{
	  *avoid_victim_cnt = *avoid_victim_cnt + 1;
	}

      if (zone & PGBUF_LRU_ZONE_MASK)
	{
	  if (PGBUF_IS_PRIVATE_LRU_INDEX (bcb_flags & PGBUF_LRU_INDEX_MASK))
	    {
	      *private_cnt = *private_cnt + 1;
	    }
	}
    }
  for (i = 0; i < PGBUF_TOTAL_LRU_COUNT; i++)
    {
      *victim_candidates = *victim_candidates + pgbuf_Pool.buf_LRU_list[i].count_vict_cand;
    }

  *private_quota = (UINT64) (pgbuf_Pool.quota.private_pages_ratio * pgbuf_Pool.num_buffers);

#if defined (SERVER_MODE)
  *alloc_bcb_waiter_high = pgbuf_Pool.direct_victims.waiter_threads_high_priority->size ();
  *alloc_bcb_waiter_med = pgbuf_Pool.direct_victims.waiter_threads_low_priority->size ();
  *flushed_bcbs_waiting_direct_assign = pgbuf_Pool.flushed_bcbs->size ();
#else /* !SERVER_MODE */
  *alloc_bcb_waiter_high = 0;
  *alloc_bcb_waiter_med = 0;
  *flushed_bcbs_waiting_direct_assign = 0;
#endif /* !SERVER_MODE */

  if (pgbuf_Pool.big_private_lrus_with_victims != NULL)
    {
      *lfcq_big_prv_num = pgbuf_Pool.big_private_lrus_with_victims->size ();
    }

  if (pgbuf_Pool.private_lrus_with_victims != NULL)
    {
      *lfcq_prv_num = pgbuf_Pool.private_lrus_with_victims->size ();
    }

  *lfcq_shr_num = pgbuf_Pool.shared_lrus_with_victims->size ();
}

/*
 * pgbuf_flush_control_from_dirty_ratio () - Try to control adaptive flush aggressiveness based on the
 *					     page buffer "dirtiness".
 *
 * return : Suggested number to increase flush rate.
 */
int
pgbuf_flush_control_from_dirty_ratio (void)
{
  static int prev_dirties_cnt = 0;
  int crt_dirties_cnt = (int) pgbuf_Pool.monitor.dirties_cnt;
  int desired_dirty_cnt = pgbuf_Pool.num_buffers / 2;
  int adapt_flush_rate = 0;

  /* If the dirty ratio is now above the desired level, try to suggest a more aggressive flush to bring it back. */
  if (crt_dirties_cnt > desired_dirty_cnt)
    {
      /* Try to get dirties count back to dirty desired ratio. */
      /* Accelerate the rate when dirties count is higher. */
      int dirties_above_desired_cnt = crt_dirties_cnt - desired_dirty_cnt;
      int total_above_desired_cnt = pgbuf_Pool.num_buffers - desired_dirty_cnt;

      adapt_flush_rate = dirties_above_desired_cnt * dirties_above_desired_cnt / total_above_desired_cnt;
    }

  /* Now consider dirty growth rate. Even if page buffer dirty ratio is not yet reached, try to avoid a sharp growth.
   * Flush may be not be aggressive enough and may require time to get there. In the meantime, the dirty ratio could go
   * well beyond the desired ratio. */
  if (crt_dirties_cnt > prev_dirties_cnt)
    {
      int diff = crt_dirties_cnt - prev_dirties_cnt;

      /* Set a weight on the difference based on the dirty rate of buffer. */
      adapt_flush_rate += diff * crt_dirties_cnt / pgbuf_Pool.num_buffers;

      prev_dirties_cnt = crt_dirties_cnt;
    }

  return adapt_flush_rate;
}

/*
 * pgbuf_rv_flush_page () - Flush page during recovery. Some changes must be flushed immediately to provide
 *			    consistency, in case server crashes again during recovery.
 *
 * return	 : Error code.
 * thread_p (in) : Thread entry.
 * rcv (in)	 : Recovery data (VPID of page to flush).
 */
int
pgbuf_rv_flush_page (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  PAGE_PTR page_to_flush = NULL;
  VPID vpid_to_flush = VPID_INITIALIZER;
  LOG_DATA_ADDR addr = LOG_DATA_ADDR_INITIALIZER;

  assert (rcv->pgptr == NULL);
  assert (rcv->length == sizeof (VPID));

  VPID_COPY (&vpid_to_flush, (VPID *) rcv->data);
  page_to_flush =
    pgbuf_fix (thread_p, &vpid_to_flush, OLD_PAGE_MAYBE_DEALLOCATED, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (page_to_flush == NULL)
    {
      /* Page no longer exist. */
      er_clear ();
      return NO_ERROR;
    }
  /* Flush page and unfix. */
  /* add a log or else the end of logical system operation will complain */
  log_append_empty_record (thread_p, LOG_DUMMY_GENERIC, &addr);
  pgbuf_set_dirty (thread_p, page_to_flush, DONT_FREE);
  pgbuf_flush (thread_p, page_to_flush, DONT_FREE);
  pgbuf_unfix (thread_p, page_to_flush);

  return NO_ERROR;
}

/*
 * pgbuf_rv_flush_page_dump () - Dump data for recovery page flush.
 *
 * return      : Void.
 * fp (in)     : Output target.
 * length (in) : Length of recovery data.
 * data (in)   : Recovery data (VPID of page to flush).
 */
void
pgbuf_rv_flush_page_dump (FILE * fp, int length, void *data)
{
  VPID vpid_to_flush = VPID_INITIALIZER;

  assert (length == sizeof (VPID));

  VPID_COPY (&vpid_to_flush, (VPID *) data);
  fprintf (fp, "Page to flush: %d|%d. \n", vpid_to_flush.volid, vpid_to_flush.pageid);
}

/*
 * pgbuf_latch_mode_str () - print latch_mode
 *
 * return          : const char *
 * latch_mode (in) :
 */
static const char *
pgbuf_latch_mode_str (PGBUF_LATCH_MODE latch_mode)
{
  const char *latch_mode_str;

  switch (latch_mode)
    {
    case PGBUF_NO_LATCH:
      latch_mode_str = "No Latch";
      break;
    case PGBUF_LATCH_READ:
      latch_mode_str = "Read";
      break;
    case PGBUF_LATCH_WRITE:
      latch_mode_str = "Write";
      break;
    case PGBUF_LATCH_FLUSH:
      latch_mode_str = "Flush";
      break;
    default:
      latch_mode_str = "Fault";
      break;
    }

  return latch_mode_str;
}

/*
 * pgbuf_zone_str () - print zone info
 *
 * return          : const char *
 * zone (in) :
 */
static const char *
pgbuf_zone_str (PGBUF_ZONE zone)
{
  const char *zone_str;

  switch (zone)
    {
    case PGBUF_LRU_1_ZONE:
      zone_str = "LRU_1_Zone";
      break;
    case PGBUF_LRU_2_ZONE:
      zone_str = "LRU_2_Zone";
      break;
    case PGBUF_LRU_3_ZONE:
      zone_str = "LRU_3_Zone";
      break;
    case PGBUF_INVALID_ZONE:
      zone_str = "INVALID_Zone";
      break;
    default:
      zone_str = "VOID_Zone";
      break;
    }

  return zone_str;
}

/*
 * pgbuf_consistent_str () - print consistent info
 *
 * return          : const char *
 * consistent (in) :
 */
static const char *
pgbuf_consistent_str (int consistent)
{
  const char *consistent_str;

  switch (consistent)
    {
    case PGBUF_CONTENT_GOOD:
      consistent_str = "GOOD";
      break;
    case PGBUF_CONTENT_BAD:
      consistent_str = "BAD";
      break;
    default:
      consistent_str = "LIKELY BAD";
      break;
    }

  return consistent_str;
}

/*
 * pgbuf_get_fix_count () - Get page fix count.
 *
 * return     : Fix count.
 * pgptr (in) : Page pointer.
 */
int
pgbuf_get_fix_count (PAGE_PTR pgptr)
{
  PGBUF_BCB *bufptr = NULL;

  assert (pgptr != NULL);

  CAST_PGPTR_TO_BFPTR (bufptr, pgptr);

  return bufptr->fcnt;
}

/*
 * pgbuf_get_hold_count () - Get hold count for current thread.
 *
 * return        : Hold count
 * thread_p (in) : Thread entry
 */
int
pgbuf_get_hold_count (THREAD_ENTRY * thread_p)
{
  int me = thread_get_entry_index (thread_p);
  return pgbuf_Pool.thrd_holder_info[me].num_hold_cnt;
}

/*
 * pgbuf_get_page_type_for_stat () - Return the page type for current page
 *
 * return        : page type
 * pgptr (in)    : pointer to a page
 */
PERF_PAGE_TYPE
pgbuf_get_page_type_for_stat (THREAD_ENTRY * thread_p, PAGE_PTR pgptr)
{
  PERF_PAGE_TYPE perf_page_type;
  FILEIO_PAGE *io_pgptr;

  CAST_PGPTR_TO_IOPGPTR (io_pgptr, pgptr);
  if ((io_pgptr->prv.ptype == PAGE_BTREE)
      && (perfmon_get_activation_flag () & PERFMON_ACTIVATION_FLAG_DETAILED_BTREE_PAGE))
    {
      perf_page_type = btree_get_perf_btree_page_type (thread_p, pgptr);
    }
  else
    {
      perf_page_type = (PERF_PAGE_TYPE) io_pgptr->prv.ptype;
    }

  return perf_page_type;
}

/*
 * pgbuf_log_new_page () - log new page being created
 *
 * return         : error code
 * thread_p (in)  : thread entry
 * page_new (in)  : new page
 * data_size (in) : size of page data
 * ptype_new (in) : new page type
 */
void
pgbuf_log_new_page (THREAD_ENTRY * thread_p, PAGE_PTR page_new, int data_size, PAGE_TYPE ptype_new)
{
  assert (ptype_new != PAGE_UNKNOWN);
  assert (page_new != NULL);
  assert (data_size > 0);

  log_append_undoredo_data2 (thread_p, RVPGBUF_NEW_PAGE, NULL, page_new, (PGLENGTH) ptype_new, 0, data_size, NULL,
			     page_new);
  pgbuf_set_dirty (thread_p, page_new, DONT_FREE);
}

void
pgbuf_log_redo_new_page (THREAD_ENTRY * thread_p, PAGE_PTR page_new, int data_size, PAGE_TYPE ptype_new)
{
  assert (ptype_new != PAGE_UNKNOWN);
  assert (page_new != NULL);
  assert (data_size > 0);

  log_append_redo_data2 (thread_p, RVPGBUF_NEW_PAGE, NULL, page_new, (PGLENGTH) ptype_new, data_size, page_new);
  pgbuf_set_dirty (thread_p, page_new, DONT_FREE);
}

/*
 * log_redo_page () - Apply redo for changing entire page (or at least its first part).
 *
 * return	 : NO_ERROR.
 * thread_p (in) : Thread entry.
 * rcv (in)	 : Recovery data.
 */
int
pgbuf_rv_new_page_redo (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  PAGE_TYPE set_page_type;
  assert (rcv->pgptr != NULL);
  assert (rcv->length >= 0);
  assert (rcv->length <= DB_PAGESIZE);

  if (rcv->length > 0)
    {
      memcpy (rcv->pgptr, rcv->data, rcv->length);
    }

  set_page_type = (PAGE_TYPE) rcv->offset;
  if (set_page_type != PAGE_UNKNOWN)
    {
      pgbuf_set_page_ptype (thread_p, rcv->pgptr, set_page_type);
    }
  else
    {
      assert (false);
    }

  pgbuf_set_dirty (thread_p, rcv->pgptr, DONT_FREE);
  return NO_ERROR;
}

/*
 * pgbuf_rv_new_page_undo () - undo new page (by resetting its page type to PAGE_UNKNOWN)
 *
 * return        : NO_ERROR
 * thread_p (in) : thread entry
 * rcv (in)      : recovery data
 */
int
pgbuf_rv_new_page_undo (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  pgbuf_set_page_ptype (thread_p, rcv->pgptr, PAGE_UNKNOWN);
  pgbuf_set_dirty (thread_p, rcv->pgptr, DONT_FREE);
  return NO_ERROR;
}

/*
 * pgbuf_dealloc_page () - deallocate a page
 *
 * return        : error code
 * thread_p (in) : thread entry
 * page (in)     : page to deallocate
 */
void
pgbuf_dealloc_page (THREAD_ENTRY * thread_p, PAGE_PTR page_dealloc)
{
  PGBUF_BCB *bcb = NULL;
  PAGE_TYPE ptype;
  FILEIO_PAGE_RESERVED *prv;
  PGBUF_DEALLOC_UNDO_DATA udata;
  char undo_data[8];		// pageid(4) + volid(2) + pyte(1) + pflag(1)
  int holder_status;

  /* how it works: page is "deallocated" by resetting its type to PAGE_UNKNOWN. also prepare bcb for victimization.
   *
   * note: the bcb used to be invalidated. but that means flushing page to disk and waiting for IO write. that may be
   *       too slow. if we add the bcb to the bottom of a lru list, it will be eventually flushed by flush thread and
   *       victimized. */

  CAST_PGPTR_TO_BFPTR (bcb, page_dealloc);
  assert (bcb->fcnt == 1);

  prv = &bcb->iopage_buffer->iopage.prv;
  assert (prv->ptype != PAGE_UNKNOWN);

  udata.pageid = prv->pageid;
  udata.volid = prv->volid;
  udata.ptype = prv->ptype;
  udata.pflag = prv->pflag;

  log_append_undoredo_data2 (thread_p, RVPGBUF_DEALLOC, NULL, page_dealloc, 0, sizeof (udata), 0, &udata, NULL);

  PGBUF_BCB_LOCK (bcb);

#if !defined(NDEBUG)
  if (bcb->iopage_buffer->iopage.prv.pflag & FILEIO_PAGE_FLAG_ENCRYPTED_MASK)
    {
      tde_er_log ("pgbuf_dealloc_page(): clear tde bit in pflag, VPID = %d|%d, tde_algorithm = %s\n",
		  VPID_AS_ARGS (&bcb->vpid), tde_get_algorithm_name (pgbuf_get_tde_algorithm (page_dealloc)));
    }
#endif /* !NDEBUG */

  /* set unknown type */
  bcb->iopage_buffer->iopage.prv.ptype = (unsigned char) PAGE_UNKNOWN;
  /* clear page flags (now only tde algorithm) */
  bcb->iopage_buffer->iopage.prv.pflag = (unsigned char) 0;

  /* set dirty and mark to move to the bottom of lru */
  pgbuf_bcb_update_flags (thread_p, bcb, PGBUF_BCB_DIRTY_FLAG | PGBUF_BCB_MOVE_TO_LRU_BOTTOM_FLAG, 0);

  holder_status = pgbuf_unlatch_thrd_holder (thread_p, bcb, NULL);

#if !defined (NDEBUG)
  thread_p->get_pgbuf_tracker ().decrement (page_dealloc);
#endif // !NDEBUG
  (void) pgbuf_unlatch_bcb_upon_unfix (thread_p, bcb, holder_status);
  /* bufptr->mutex has been released in above function. */
}

/*
 * pgbuf_rv_dealloc_redo () - redo page deallocate (by resetting page type to unknown).
 *
 * return        : NO_ERROR
 * thread_p (in) : thread entry
 * rcv (in)      : recovery data
 */
int
pgbuf_rv_dealloc_redo (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  pgbuf_set_page_ptype (thread_p, rcv->pgptr, PAGE_UNKNOWN);
  pgbuf_set_tde_algorithm (thread_p, rcv->pgptr, TDE_ALGORITHM_NONE, true);
  pgbuf_set_dirty (thread_p, rcv->pgptr, DONT_FREE);
  return NO_ERROR;
}

/*
 * pgbuf_rv_dealloc_undo () - undo page deallocation. the page is validated by setting its page type back.
 *
 * return        : error code
 * thread_p (in) : thread entry
 * rcv (in)      : recovery data
 *
 * note: we had to make this function logical, because if a page is deallocated, it cannot be fixed, unless we use
 *       fetch type OLD_PAGE_DEALLOCATED.
 */
int
pgbuf_rv_dealloc_undo (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  PAGE_PTR page_deallocated = NULL;
  PGBUF_DEALLOC_UNDO_DATA *udata = (PGBUF_DEALLOC_UNDO_DATA *) rcv->data;
  VPID vpid;
  FILEIO_PAGE *iopage;

  vpid.pageid = udata->pageid;
  vpid.volid = udata->volid;

  assert (rcv->length == sizeof (PGBUF_DEALLOC_UNDO_DATA));
  assert (udata->ptype > PAGE_UNKNOWN && udata->ptype <= PAGE_LAST);

  /* fix deallocated page */
  page_deallocated = pgbuf_fix (thread_p, &vpid, OLD_PAGE_DEALLOCATED, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (page_deallocated == NULL)
    {
      assert_release (false);
      return ER_FAILED;
    }
  assert (pgbuf_get_page_ptype (thread_p, page_deallocated) == PAGE_UNKNOWN);
  pgbuf_set_page_ptype (thread_p, page_deallocated, (PAGE_TYPE) udata->ptype);

  CAST_PGPTR_TO_IOPGPTR (iopage, page_deallocated);
  iopage->prv.pflag = udata->pflag;

#if !defined(NDEBUG)
  if (iopage->prv.pflag & FILEIO_PAGE_FLAG_ENCRYPTED_MASK)
    {
      tde_er_log ("pgbuf_rv_dealloc_page(): reset tde bit in pflag, VPID = %d|%d, tde_algorithm = %s\n",
		  VPID_AS_ARGS (&vpid), tde_get_algorithm_name (pgbuf_get_tde_algorithm (page_deallocated)));
    }
#endif /* !NDEBUG */

  log_append_compensate_with_undo_nxlsa (thread_p, RVPGBUF_COMPENSATE_DEALLOC, &vpid, 0, page_deallocated,
					 sizeof (PGBUF_DEALLOC_UNDO_DATA), udata, LOG_FIND_CURRENT_TDES (thread_p),
					 &rcv->reference_lsa);
  pgbuf_set_dirty_and_free (thread_p, page_deallocated);
  return NO_ERROR;
}

/*
 * pgbuf_rv_dealloc_undo_compensate () - compensation for undo page deallocation. the page is validated by setting its page type back.
 *
 * return        : error code
 * thread_p (in) : thread entry
 * rcv (in)      : recovery data
 *
 */
int
pgbuf_rv_dealloc_undo_compensate (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  PGBUF_DEALLOC_UNDO_DATA *udata = (PGBUF_DEALLOC_UNDO_DATA *) rcv->data;
  VPID vpid;
  FILEIO_PAGE *iopage;

  assert (rcv->pgptr != NULL);
  assert (rcv->length == sizeof (PGBUF_DEALLOC_UNDO_DATA));
  assert (udata->ptype > PAGE_UNKNOWN && udata->ptype <= PAGE_LAST);

  CAST_PGPTR_TO_IOPGPTR (iopage, rcv->pgptr);

  pgbuf_set_page_ptype (thread_p, rcv->pgptr, (PAGE_TYPE) udata->ptype);
  iopage->prv.pflag = udata->pflag;

#if !defined(NDEBUG)
  if (iopage->prv.pflag & FILEIO_PAGE_FLAG_ENCRYPTED_MASK)
    {
      tde_er_log ("pgbuf_rv_dealloc_page(): reset tde bit in pflag, VPID = %d|%d, tde_algorithm = %s\n",
		  VPID_AS_ARGS (&vpid), tde_get_algorithm_name (pgbuf_get_tde_algorithm (rcv->pgptr)));
    }
#endif /* !NDEBUG */

  return NO_ERROR;
}

/*
 * pgbuf_fix_if_not_deallocated () - fix a page if it is not deallocated. the difference compared to regulat page fix
 *                                   finding deallocated pages is expected. if the page is indeed deallocated, it will
 *                                   not fix it
 *
 * return               : error code
 * thread_p (in)        : thread entry
 * vpid (in)            : page identifier
 * latch_mode (in)      : latch mode
 * latch_condition (in) : latch condition
 * page (out)           : output fixed page if not deallocated. output NULL if deallocated.
 * caller_file (in)     : caller file name
 * caller_line (in)     : caller line
 */
int
pgbuf_fix_if_not_deallocated_with_caller (THREAD_ENTRY * thread_p, const VPID * vpid, PGBUF_LATCH_MODE latch_mode,
					  PGBUF_LATCH_CONDITION latch_condition, PAGE_PTR * page
#if !defined (NDEBUG)
					  , const char *caller_file, int caller_line, const char *caller_func
#endif
  )
{
  DISK_ISVALID isvalid;
  int error_code = NO_ERROR;

  assert (vpid != NULL && !VPID_ISNULL (vpid));
  assert (page != NULL);
  *page = NULL;

  /* First, checks whether the file was destroyed. Such check may create performance issues.
   * This function must be adapted. Thus, if the transaction has a lock on table, we can skip
   * the code that checks whether the file was destroyed.
   */
  isvalid = disk_is_page_sector_reserved (thread_p, vpid->volid, vpid->pageid);
  if (isvalid == DISK_INVALID)
    {
      /* deallocated */
      return NO_ERROR;
    }
  else if (isvalid == DISK_ERROR)
    {
      ASSERT_ERROR_AND_SET (error_code);
      return error_code;
    }
  assert (isvalid == DISK_VALID);

  /* is reserved */
#if defined (NDEBUG)
  *page = pgbuf_fix_release (thread_p, vpid, OLD_PAGE_MAYBE_DEALLOCATED, latch_mode, latch_condition);
#else /* !NDEBUG */
  *page =
    pgbuf_fix_debug (thread_p, vpid, OLD_PAGE_MAYBE_DEALLOCATED, latch_mode, latch_condition, caller_file, caller_line,
		     caller_func);
#endif /* !NDEBUG */
  if (*page == NULL && !log_is_in_crash_recovery_and_not_yet_completes_redo ())
    {
      ASSERT_ERROR_AND_SET (error_code);
      if (error_code == ER_PB_BAD_PAGEID)
	{
	  /* deallocated */
	  er_clear ();
	  error_code = NO_ERROR;
	}
    }
  return error_code;
}

#if defined (SERVER_MODE)
/*
 * pgbuf_keep_victim_flush_thread_running () - keep flush thread running
 *
 * return    : true to keep flush thread running, false otherwise
 */
bool
pgbuf_keep_victim_flush_thread_running (void)
{
  return (pgbuf_is_any_thread_waiting_for_direct_victim () || pgbuf_is_hit_ratio_low ());
}
#endif /* SERVER_MDOE */

/*
 * pgbuf_assign_direct_victim () - try to assign bcb directly to a thread waiting for victim. bcb must be a valid victim
 *                                 candidate
 *
 * return        : true if bcb was assigned directly as victim, false otherwise
 * thread_p (in) : thread entry
 * bcb (in)      : bcb to assign as victim
 */
STATIC_INLINE bool
pgbuf_assign_direct_victim (THREAD_ENTRY * thread_p, PGBUF_BCB * bcb)
{
#if defined (SERVER_MODE)
  THREAD_ENTRY *waiter_thread = NULL;

  PERF_UTIME_TRACKER timetr;

  PERF_UTIME_TRACKER_START (thread_p, &timetr);

  /* must hold bcb mutex and victimization should be possible. the only victim-candidate invalidating flag allowed here
   * is PGBUF_BCB_FLUSHING_TO_DISK_FLAG (because flush also calls this). */
  assert (!pgbuf_bcb_is_direct_victim (bcb));
  assert (!pgbuf_bcb_is_invalid_direct_victim (bcb));
  assert (!pgbuf_bcb_is_dirty (bcb));
  assert (!pgbuf_is_bcb_fixed_by_any (bcb, true));

  PGBUF_BCB_CHECK_OWN (bcb);

  /* is flushing is expected, since this is called from flush too. caller should make sure no other case should get
   * here with is flushing true. */
  /* if marked as victim candidate, we are sorry for the one that marked it. we'll override the flag. */

  /* do we have any waiter threads? */
  while (pgbuf_get_thread_waiting_for_direct_victim (waiter_thread))
    {
      assert (waiter_thread != NULL);

      thread_lock_entry (waiter_thread);

      if (waiter_thread->resume_status != THREAD_ALLOC_BCB_SUSPENDED)
	{
	  /* it is not waiting for us anymore */
	  thread_unlock_entry (waiter_thread);
	  continue;
	}

      /* wakeup suspended thread */
      thread_wakeup_already_had_mutex (waiter_thread, THREAD_ALLOC_BCB_RESUMED);

      /* assign bcb to thread */
      pgbuf_bcb_update_flags (thread_p, bcb, PGBUF_BCB_VICTIM_DIRECT_FLAG, PGBUF_BCB_FLUSHING_TO_DISK_FLAG);

      pgbuf_Pool.direct_victims.bcb_victims[waiter_thread->index] = bcb;

      thread_unlock_entry (waiter_thread);

      PERF_UTIME_TRACKER_TIME (thread_p, &timetr, PSTAT_PB_ASSIGN_DIRECT_BCB);

      /* bcb was assigned */
      return true;
    }
  PERF_UTIME_TRACKER_TIME (thread_p, &timetr, PSTAT_PB_ASSIGN_DIRECT_BCB);
#endif /* SERVER_MODE */

  /* no waiting threads */
  return false;
}

#if defined (SERVER_MODE)

/*
 * pgbuf_assign_flushed_pages () - assign flushed pages directly. or just mark them as flushed if it cannot be assigned.
 *
 * return        : void
 * thread_p (in) : thread entry
 */
bool
pgbuf_assign_flushed_pages (THREAD_ENTRY * thread_p)
{
  PGBUF_BCB *bcb_flushed = NULL;
  bool detailed_perf = perfmon_is_perf_tracking_and_active (PERFMON_ACTIVATION_FLAG_PB_VICTIMIZATION);
  bool not_empty = false;
  /* invalidation flag for direct victim assignment: any flag invalidating victim candidates, except is flushing flag */
  int invalidate_flag = (PGBUF_BCB_INVALID_VICTIM_CANDIDATE_MASK & (~PGBUF_BCB_FLUSHING_TO_DISK_FLAG));

  /* consume all flushed bcbs queue */
  while (pgbuf_Pool.flushed_bcbs->consume (bcb_flushed))
    {
      not_empty = true;

      /* we need to lock mutex */
      PGBUF_BCB_LOCK (bcb_flushed);

      if ((bcb_flushed->flags & invalidate_flag) != 0)
	{
	  /* dirty bcb is not a valid victim */
	}
      else if (pgbuf_is_bcb_fixed_by_any (bcb_flushed, true))
	{
	  /* bcb is fixed. we cannot assign it as victim */
	}
      else if (!PGBUF_IS_BCB_IN_LRU_VICTIM_ZONE (bcb_flushed))
	{
	  /* bcb is hot. don't assign it as victim */
	}
      else if (PGBUF_IS_PRIVATE_LRU_INDEX (pgbuf_bcb_get_lru_index (bcb_flushed))
	       && !PGBUF_LRU_LIST_IS_OVER_QUOTA (pgbuf_lru_list_from_bcb (bcb_flushed)))
	{
	  /* bcb belongs to a private list under quota. give it a chance. */
	}
      else if (pgbuf_assign_direct_victim (thread_p, bcb_flushed))
	{
	  /* assigned directly */
	  if (detailed_perf)
	    {
	      perfmon_inc_stat (thread_p, PSTAT_PB_VICTIM_ASSIGN_DIRECT_FLUSH);
	    }
	}
      else
	{
	  /* not assigned directly */
	  assert (!pgbuf_bcb_is_direct_victim (bcb_flushed));
	  /* could not assign it directly. there must be no waiters */
	}

      /* make sure bcb is no longer marked as flushing */
      pgbuf_bcb_mark_was_flushed (thread_p, bcb_flushed);

      /* wakeup thread waiting for flush */
      if (bcb_flushed->next_wait_thrd != NULL)
	{
	  pgbuf_wake_flush_waiters (thread_p, bcb_flushed);
	}

      PGBUF_BCB_UNLOCK (bcb_flushed);
    }

  return not_empty;
}

/*
 * pgbuf_get_thread_waiting_for_direct_victim () - get one of the threads waiting
 *
 * return                   : true if got thread, false otherwise
 * waiting_thread_out (out) : output thread waiting for victim
 */
STATIC_INLINE bool
pgbuf_get_thread_waiting_for_direct_victim (REFPTR (THREAD_ENTRY, waiting_thread_out))
{
  static INT64 count = 0;
  INT64 my_count = ATOMIC_INC_64 (&count, 1);

  /* every now and then, force getting waiting threads from queues with lesser priority */
  if (my_count % 4 == 0)
    {
      if (pgbuf_Pool.direct_victims.waiter_threads_low_priority->consume (waiting_thread_out))
	{
	  return true;
	}
    }
  /* try queue in their priority order */
  if (pgbuf_Pool.direct_victims.waiter_threads_high_priority->consume (waiting_thread_out))
    {
      return true;
    }
  if (pgbuf_Pool.direct_victims.waiter_threads_low_priority->consume (waiting_thread_out))
    {
      return true;
    }
  return false;
}

/*
 * pgbuf_get_direct_victim () - get victim assigned directly.
 *
 * return        : pointer to victim bcb
 * thread_p (in) : thread entry
 */
STATIC_INLINE PGBUF_BCB *
pgbuf_get_direct_victim (THREAD_ENTRY * thread_p)
{
  PGBUF_BCB *bcb =
    (PGBUF_BCB *) ATOMIC_TAS_ADDR (&pgbuf_Pool.direct_victims.bcb_victims[thread_p->index], (PGBUF_BCB *) NULL);
  int lru_idx;

  assert (bcb != NULL);

  PGBUF_BCB_LOCK (bcb);

  if (pgbuf_bcb_is_invalid_direct_victim (bcb))
    {
      /* somebody fixed the page again. */
      pgbuf_bcb_update_flags (thread_p, bcb, 0, PGBUF_BCB_INVALIDATE_DIRECT_VICTIM_FLAG);
      PGBUF_BCB_UNLOCK (bcb);
      return NULL;
    }

  assert (pgbuf_bcb_is_direct_victim (bcb));

  /* clear direct victim flag */
  pgbuf_bcb_update_flags (thread_p, bcb, 0, PGBUF_BCB_VICTIM_DIRECT_FLAG);

  if (!pgbuf_is_bcb_victimizable (bcb, true))
    {
      /* should not happen */
      assert (false);
      PGBUF_BCB_UNLOCK (bcb);
      return NULL;
    }

  switch (pgbuf_bcb_get_zone (bcb))
    {
    case PGBUF_VOID_ZONE:
      break;
    case PGBUF_INVALID_ZONE:
      /* should not be here */
      assert (false);
      break;
    default:
      /* lru zones */
      assert (PGBUF_IS_BCB_IN_LRU (bcb));
      lru_idx = pgbuf_bcb_get_lru_index (bcb);

      /* remove bcb from lru list */
      pgbuf_lru_remove_bcb (thread_p, bcb);

      /* add to AOUT */
      pgbuf_add_vpid_to_aout_list (thread_p, &bcb->vpid, lru_idx);
      break;
    }

  assert (pgbuf_bcb_get_zone (bcb) == PGBUF_VOID_ZONE);
  return bcb;
}

/*
 * pgbuf_is_any_thread_waiting_for_direct_victim () - is any thread waiting to allocate bcb?
 *
 * return : true/false
 */
STATIC_INLINE bool
pgbuf_is_any_thread_waiting_for_direct_victim (void)
{
  return (!pgbuf_Pool.direct_victims.waiter_threads_high_priority->is_empty ()
	  || !pgbuf_Pool.direct_victims.waiter_threads_low_priority->is_empty ());
}
#endif /* SERVER_MODE */

/*
 * pgbuf_lru_increment_victim_candidates () - increment lru list victim candidate counter
 *
 * return        : void
 * lru_list (in) : lru list
 */
STATIC_INLINE void
pgbuf_lru_add_victim_candidate (THREAD_ENTRY * thread_p, PGBUF_LRU_LIST * lru_list, PGBUF_BCB * bcb)
{
  PGBUF_BCB *old_victim_hint;
  int list_tick;

  /* first, let's update the victim hint. */
  /* We don't own the LRU mutex here, so after we read the victim_hint, another thread may change that BCB,
   * or the victim_hint pointer itself.
   * All changes of lru_list->victim_hint, must be precedeed by changing the new hint BCB to LRU3 zone, the checks must
   * be repetead here in the same sequence:
   *  1. read lru_list->victim_hint
   *  2. stop if old_victim_hint is still in LRU3 and is older than proposed to be hint
   *  3. atomically change the hint
   * (old_victim_hint may suffer other changes including relocating to another LRU, this is protected by the atomic op)
   */
  do
    {
      /* replace current victim hint only if this candidate is better. that is if its age in zone 3 is greater that of
       * current hint's */
      old_victim_hint = lru_list->victim_hint;
      list_tick = lru_list->tick_lru3;
      if (old_victim_hint != NULL && PGBUF_IS_BCB_IN_LRU_VICTIM_ZONE (old_victim_hint)
	  && (PGBUF_AGE_DIFF (old_victim_hint->tick_lru3, list_tick) > PGBUF_AGE_DIFF (bcb->tick_lru3, list_tick)))
	{
	  /* current hint is older. */
	  break;
	}

      /* compare & swap. if it fails, the hint must have been updated by someone else (it is possible even if we hold
       * lru and bcb mutexes, see pgbuf_set_dirty). we try until we succeed changing the hint or until the current hint
       * is better. */
    }
  while (!ATOMIC_CAS_ADDR (&lru_list->victim_hint, old_victim_hint, bcb));

  /* update victim counter. */
  /* add to lock-free circular queue so victimizers can find it... if this is not a private list under quota. */
  ATOMIC_INC_32 (&lru_list->count_vict_cand, 1);
  if (PGBUF_IS_SHARED_LRU_INDEX (lru_list->index) || PGBUF_LRU_LIST_IS_OVER_QUOTA (lru_list))
    {
      if (pgbuf_lfcq_add_lru_with_victims (lru_list)
	  && perfmon_is_perf_tracking_and_active (PERFMON_ACTIVATION_FLAG_PB_VICTIMIZATION))
	{
	  /* added to queue of lru lists having victims. */
	}
    }
}

/*
 * pgbuf_lru_decrement_victim_candidates () - decrement lru list victim candidate counter
 *
 * return        : void
 * lru_list (in) : lru list
 */
STATIC_INLINE void
pgbuf_lru_remove_victim_candidate (THREAD_ENTRY * thread_p, PGBUF_LRU_LIST * lru_list, PGBUF_BCB * bcb)
{
  /* first update victim counter */
  if (ATOMIC_INC_32 (&lru_list->count_vict_cand, -1) == 0)
    {
      /* we cannot remove an entry from lock-free circular queue easily. we just hope that this does not happen too
       * often. do nothing here. */
    }
}

/*
 * pgbuf_lru_advance_victim_hint () - invalidate bcb_prev_hint as victim hint and advance to bcb_new_hint (if possible).
 *                                    in the case we'd reset hint to NULL, but we know victim candidates still exist,
 *                                    hint is set to list bottom.
 *
 * return                      : void
 * thread_p (in)               : thread entry
 * lru_list (in)               : LRU list
 * bcb_prev_hint (in)          : bcb being invalidated as hint
 * bcb_new_hint (in)           : new desired hint (can be adjusted to NULL or bottom)
 * was_vict_count_updated (in) : was victim count updated? (false if bcb_prev_hint is still counted as victim candidate)
 */
STATIC_INLINE void
pgbuf_lru_advance_victim_hint (THREAD_ENTRY * thread_p, PGBUF_LRU_LIST * lru_list, PGBUF_BCB * bcb_prev_hint,
			       PGBUF_BCB * bcb_new_hint, bool was_vict_count_updated)
{
  PGBUF_BCB *new_victim_hint = NULL;

  /* note: caller must have lock on lru list! */
  /* todo: add watchers on lru list mutexes */

  /* new victim hint should be either NULL or in the victimization zone */
  new_victim_hint = (bcb_new_hint != NULL && PGBUF_IS_BCB_IN_LRU_VICTIM_ZONE (bcb_new_hint)) ? bcb_new_hint : NULL;

  /* restart from bottom if hint is NULL but we have victim candidates */
  new_victim_hint = ((new_victim_hint == NULL && lru_list->count_vict_cand > (was_vict_count_updated ? 0 : 1))
		     ? lru_list->bottom : new_victim_hint);

  new_victim_hint = ((new_victim_hint != NULL && PGBUF_IS_BCB_IN_LRU_VICTIM_ZONE (new_victim_hint))
		     ? new_victim_hint : NULL);

  /* update hint (if it was not already updated) */
  assert (new_victim_hint == NULL || pgbuf_bcb_get_lru_index (new_victim_hint) == lru_list->index);
  if (ATOMIC_CAS_ADDR (&lru_list->victim_hint, bcb_prev_hint, new_victim_hint))
    {
      /* updated hint */
    }

  assert (lru_list->victim_hint == NULL || PGBUF_IS_BCB_IN_LRU_VICTIM_ZONE (lru_list->victim_hint));
}

/*
 * pgbuf_bcb_update_flags () - update bcb flags (not zone and not lru index)
 *
 * return           : void
 * bcb (in)         : bcb
 * set_flags (in)   : flags to set
 * clear_flags (in) : flags to clear
 *
 * note: this makes sure the bcb flags field (which is actually flags + zone + lru index) is modified atomically. it
 *       also handles changes of victim candidates.
 */
STATIC_INLINE void
pgbuf_bcb_update_flags (THREAD_ENTRY * thread_p, PGBUF_BCB * bcb, int set_flags, int clear_flags)
{
  int old_flags;
  int new_flags;
  bool old_dirty, new_dirty;

  /* sanity checks */
  assert (bcb != NULL);
  assert ((set_flags & (~PGBUF_BCB_FLAGS_MASK)) == 0);
  assert ((clear_flags & (~PGBUF_BCB_FLAGS_MASK)) == 0);

  /* update flags by making sure that other flags + zone + lru_index are not modified. */
  do
    {
      old_flags = bcb->flags;
      new_flags = old_flags | set_flags;
      new_flags = new_flags & (~clear_flags);

      if (old_flags == new_flags)
	{
	  /* no changes are required. */
	  return;
	}
    }
  while (!ATOMIC_CAS_32 (&bcb->flags, old_flags, new_flags));

  if (PGBUF_GET_ZONE (old_flags) == PGBUF_LRU_3_ZONE)
    {
      /* bcb is in lru zone that can be victimized. some flags invalidate the victimization candidacy of a bcb;
       * therefore we need to check if the bcb status regarding victimization is changed. */
      bool is_old_invalid_victim_candidate = (old_flags & PGBUF_BCB_INVALID_VICTIM_CANDIDATE_MASK) != 0;
      bool is_new_invalid_victim_candidate = (new_flags & PGBUF_BCB_INVALID_VICTIM_CANDIDATE_MASK) != 0;
      PGBUF_LRU_LIST *lru_list;

      lru_list = pgbuf_lru_list_from_bcb (bcb);

      if (is_old_invalid_victim_candidate && !is_new_invalid_victim_candidate)
	{
	  /* bcb has become a victim candidate */
	  pgbuf_lru_add_victim_candidate (thread_p, lru_list, bcb);
	}
      else if (!is_old_invalid_victim_candidate && is_new_invalid_victim_candidate)
	{
	  /* bcb is no longer a victim candidate */
	  pgbuf_lru_remove_victim_candidate (thread_p, lru_list, bcb);
	}
      else
	{
	  /* bcb status remains the same */
	}
    }

  old_dirty = (old_flags & PGBUF_BCB_DIRTY_FLAG) != 0;
  new_dirty = (new_flags & PGBUF_BCB_DIRTY_FLAG) != 0;

  if (old_dirty && !new_dirty)
    {
      /* cleared dirty flag. */
      ATOMIC_INC_64 (&pgbuf_Pool.monitor.dirties_cnt, -1);
    }
  else if (!old_dirty && new_dirty)
    {
      /* added dirty flag */
      ATOMIC_INC_64 (&pgbuf_Pool.monitor.dirties_cnt, 1);
    }

  assert (pgbuf_Pool.monitor.dirties_cnt >= 0 && pgbuf_Pool.monitor.dirties_cnt <= pgbuf_Pool.num_buffers);
}

/*
 * pgbuf_bcb_change_zone () - change the zone and lru index of bcb, but keep the bcb flags. also handles the zone
 *                            counters, victim counter and victim hint for lru lists.
 *
 * return       : void
 * bcb (in)     : bcb
 * lru_idx (in) : lru index (0 if not in any lru zone)
 * zone (in)    : zone
 *
 * this is called whenever the bcb is moved from a logical zone to another. possible transitions:
 *
 * FIXME: correct the following description
 * 1. get from invalid list                 invalid      => void  (bcb is locked)
 * 2. get victim                            lru          => void  (list & bcb are locked)
 * 3. unfix                                 void/lru     => lru   (list & bcb are locked)
 * 4. lru adjust zones                      lru          => lru   (list is locked)
 *
 * note: two simultaneous change zones on the same bcb should not be possible. the only case when bcb is not locked
 *       is case 4, however list is locked. other possible cases that can call change zone on same bcb must have lock
 *       on lru mutex.
 *
 * note: bcb->flags is changed here and simultaneous calls of pgbuf_bcb_update_flags is possible. in some cases, the
 *       flags may change even with no mutex (pgbuf_set_dirty). since we have to handle victim counter and hint for
 *       lru lists, we must do atomic operations to modify the zone, and keep previous and new flag values. based on
 *       these flags, we then update lru zone counters, lru victim counter and lru victim hint. lru zone counters can
 *       only be modified by other calls pgbuf_bcb_change_zone in same lru and are protected by lru mutex, so they can
 *       be modified without atomic operations.
 */
STATIC_INLINE void
pgbuf_bcb_change_zone (THREAD_ENTRY * thread_p, PGBUF_BCB * bcb, int new_lru_idx, PGBUF_ZONE new_zone)
{
  int old_flags;
  int new_flags;
  int new_zone_idx = PGBUF_MAKE_ZONE (new_lru_idx, new_zone);
  bool is_valid_victim_candidate;
  PGBUF_LRU_LIST *lru_list;

  /* note: make sure the zones from and to are changing are blocked */

  /* sanity checks */
  assert (bcb != NULL);
  assert (new_lru_idx == 0 || new_zone == PGBUF_LRU_1_ZONE || new_zone == PGBUF_LRU_2_ZONE
	  || new_zone == PGBUF_LRU_3_ZONE);

  /* update bcb->flags. make sure we are only changing the values for zone and lru index, but we preserve the flags. */
  do
    {
      /* get current value of bcb->flags */
      old_flags = bcb->flags;

      /* now set new flags to same bcb flags + new zone & lru index */
      new_flags = (old_flags & PGBUF_BCB_FLAGS_MASK) | new_zone_idx;

      /* compare & swap. if we fail, we have to try again. until we succeed. */
    }
  while (!ATOMIC_CAS_32 (&bcb->flags, old_flags, new_flags));

  /* was bcb a valid victim candidate (we only consider flags, not fix counters or zone)? note that this is still true
   * after the change of zone. */
  is_valid_victim_candidate = (old_flags & PGBUF_BCB_INVALID_VICTIM_CANDIDATE_MASK) == 0;

  if (old_flags & PGBUF_LRU_ZONE_MASK)
    {
      /* bcb was in a lru list. we need to update zone counters. */
      int lru_idx = PGBUF_GET_LRU_INDEX (old_flags);
      lru_list = PGBUF_GET_LRU_LIST (lru_idx);

      /* hint should have been changed already if the BCB was in LRU3; otherwise (if downgraded, we may expect that
       * victim hint is changed by other thread (checkpoint->pgbuf_bcb_update_flags) */
      assert (lru_list->victim_hint != bcb || PGBUF_GET_ZONE (old_flags) != PGBUF_LRU_3_ZONE);

      if (PGBUF_IS_SHARED_LRU_INDEX (PGBUF_GET_LRU_INDEX (old_flags)))
	{
	  ATOMIC_INC_32 (&pgbuf_Pool.monitor.lru_shared_pgs_cnt, -1);
	}

      switch (PGBUF_GET_ZONE (old_flags))
	{
	case PGBUF_LRU_1_ZONE:
	  lru_list->count_lru1--;
	  break;
	case PGBUF_LRU_2_ZONE:
	  lru_list->count_lru2--;
	  break;
	case PGBUF_LRU_3_ZONE:
	  lru_list->count_lru3--;
	  if (is_valid_victim_candidate)
	    {
	      /* bcb was a valid victim and in the zone that could be victimized. update victim counter & hint */
	      pgbuf_lru_remove_victim_candidate (thread_p, lru_list, bcb);
	    }
	  break;
	default:
	  assert (false);
	  break;
	}
    }
  if (new_zone & PGBUF_LRU_ZONE_MASK)
    {
      lru_list = PGBUF_GET_LRU_LIST (new_lru_idx);

      if (PGBUF_IS_SHARED_LRU_INDEX (PGBUF_GET_LRU_INDEX (new_flags)))
	{
	  ATOMIC_INC_32 (&pgbuf_Pool.monitor.lru_shared_pgs_cnt, 1);
	}

      switch (new_zone)
	{
	case PGBUF_LRU_1_ZONE:
	  lru_list->count_lru1++;
	  break;
	case PGBUF_LRU_2_ZONE:
	  lru_list->count_lru2++;
	  break;
	case PGBUF_LRU_3_ZONE:
	  lru_list->count_lru3++;
	  if (is_valid_victim_candidate)
	    {
	      pgbuf_lru_add_victim_candidate (thread_p, lru_list, bcb);
	    }
	  break;
	default:
	  assert (false);
	  break;
	}
    }
}

/*
 * pgbuf_bcb_get_zone () - get zone of bcb
 *
 * return   : PGBUF_ZONE
 * bcb (in) : bcb
 */
STATIC_INLINE PGBUF_ZONE
pgbuf_bcb_get_zone (const PGBUF_BCB * bcb)
{
  return PGBUF_GET_ZONE (bcb->flags);
}

/*
 * pgbuf_bcb_get_lru_index () - get lru index of bcb. make sure bcb is in lru zones.
 *
 * return   : lru index
 * bcb (in) : bcb
 */
STATIC_INLINE int
pgbuf_bcb_get_lru_index (const PGBUF_BCB * bcb)
{
  assert (PGBUF_IS_BCB_IN_LRU (bcb));
  return PGBUF_GET_LRU_INDEX (bcb->flags);
}

/*
 * pgbuf_bcb_is_dirty () - is bcb dirty?
 *
 * return   : true/false
 * bcb (in) : bcb
 */
STATIC_INLINE bool
pgbuf_bcb_is_dirty (const PGBUF_BCB * bcb)
{
  return (bcb->flags & PGBUF_BCB_DIRTY_FLAG) != 0;
}

/*
 * pgbuf_bcb_set_dirty () - set dirty flag to bcb
 *
 * return   : void
 * bcb (in) : bcb
 */
STATIC_INLINE void
pgbuf_bcb_set_dirty (THREAD_ENTRY * thread_p, PGBUF_BCB * bcb)
{
  /* set dirty flag and clear none */
  /* note: we usually use pgbuf_bcb_update_flags function. we do an exception for pgbuf_bcb_set_dirty to since it is the
   *       most used case and the code should be as optimal as possible. */
  int old_flags;

  do
    {
      old_flags = bcb->flags;
      if (old_flags & PGBUF_BCB_DIRTY_FLAG)
	{
	  /* already dirty */
	  return;
	}
    }
  while (!ATOMIC_CAS_32 (&bcb->flags, old_flags, old_flags | PGBUF_BCB_DIRTY_FLAG));

  /* was changed to dirty */
  ATOMIC_INC_64 (&pgbuf_Pool.monitor.dirties_cnt, 1);
  assert (pgbuf_Pool.monitor.dirties_cnt >= 0 && pgbuf_Pool.monitor.dirties_cnt <= pgbuf_Pool.num_buffers);

  if (PGBUF_GET_ZONE (old_flags) == PGBUF_LRU_3_ZONE && (old_flags & PGBUF_BCB_INVALID_VICTIM_CANDIDATE_MASK) == 0)
    {
      /* invalidate victim */
      pgbuf_lru_remove_victim_candidate (thread_p, pgbuf_lru_list_from_bcb (bcb), bcb);
    }
}

/*
 * pgbuf_bcb_clear_dirty () - clear dirty flag from bcb
 *
 * return   : void
 * bcb (in) : bcb
 */
STATIC_INLINE void
pgbuf_bcb_clear_dirty (THREAD_ENTRY * thread_p, PGBUF_BCB * bcb)
{
  /* set no flag and clear dirty */
  pgbuf_bcb_update_flags (thread_p, bcb, 0, PGBUF_BCB_DIRTY_FLAG);
}

/*
 * pgbuf_bcb_mark_is_flushing () - mark page is being flushed. dirty flag is also cleared because while the page is
 *                                 flushed to disk, another thread may fix the page and modify it. the new change must
 *                                 be tracked.
 *
 * return   : void
 * bcb (in) : bcb
 */
STATIC_INLINE bool
pgbuf_bcb_mark_is_flushing (THREAD_ENTRY * thread_p, PGBUF_BCB * bcb)
{
  if (pgbuf_bcb_is_dirty (bcb))
    {
      /* set flushing flag and clear dirty */
      pgbuf_bcb_update_flags (thread_p, bcb, PGBUF_BCB_FLUSHING_TO_DISK_FLAG,
			      PGBUF_BCB_DIRTY_FLAG | PGBUF_BCB_ASYNC_FLUSH_REQ);
      return true;
    }
  else
    {
      pgbuf_bcb_update_flags (thread_p, bcb, PGBUF_BCB_FLUSHING_TO_DISK_FLAG, PGBUF_BCB_ASYNC_FLUSH_REQ);
      return false;
    }
}

/*
 * pgbuf_bcb_mark_was_flushed () - mark page was flushed to disk
 *
 * return   : void
 * bcb (in) : bcb
 */
STATIC_INLINE void
pgbuf_bcb_mark_was_flushed (THREAD_ENTRY * thread_p, PGBUF_BCB * bcb)
{
  /* set no flag and clear flushing */
  pgbuf_bcb_update_flags (thread_p, bcb, 0, PGBUF_BCB_FLUSHING_TO_DISK_FLAG);
}

/*
 * pgbuf_bcb_mark_was_not_flushed () - page flush failed
 *
 * return   : void
 * bcb (in) : bcb
 * mark_dirty(in): true if BCB needs to be marked dirty
 */
STATIC_INLINE void
pgbuf_bcb_mark_was_not_flushed (THREAD_ENTRY * thread_p, PGBUF_BCB * bcb, bool mark_dirty)
{
  /* set dirty flag and clear flushing */
  pgbuf_bcb_update_flags (thread_p, bcb, mark_dirty ? PGBUF_BCB_DIRTY_FLAG : 0, PGBUF_BCB_FLUSHING_TO_DISK_FLAG);
}

/*
 * pgbuf_bcb_is_flushing () - is page being flushed to disk?
 *
 * return   : true/false
 * bcb (in) : bcb
 */
STATIC_INLINE bool
pgbuf_bcb_is_flushing (const PGBUF_BCB * bcb)
{
  return (bcb->flags & PGBUF_BCB_FLUSHING_TO_DISK_FLAG) != 0;
}

/*
 * pgbuf_bcb_is_direct_victim () - is bcb assigned as victim directly?
 *
 * return   : true/false
 * bcb (in) : bcb
 */
STATIC_INLINE bool
pgbuf_bcb_is_direct_victim (const PGBUF_BCB * bcb)
{
  return (bcb->flags & PGBUF_BCB_VICTIM_DIRECT_FLAG) != 0;
}

/*
 * pgbuf_bcb_is_invalid_direct_victim () - is bcb assigned as victim directly, but invalidated after?
 *
 * return   : true/false
 * bcb (in) : bcb
 */
STATIC_INLINE bool
pgbuf_bcb_is_invalid_direct_victim (const PGBUF_BCB * bcb)
{
  return (bcb->flags & PGBUF_BCB_INVALIDATE_DIRECT_VICTIM_FLAG) != 0;
}

/*
 * pgbuf_bcb_is_async_flush_request () - is bcb async flush requested?
 *
 * return   : true/false
 * bcb (in) : bcb
 */
STATIC_INLINE bool
pgbuf_bcb_is_async_flush_request (const PGBUF_BCB * bcb)
{
  return (bcb->flags & PGBUF_BCB_ASYNC_FLUSH_REQ) != 0;
}

/*
 * pgbuf_bcb_should_be_moved_to_bottom_lru () - is bcb supposed to be moved to the bottom of lru?
 *
 * return   : true/false
 * bcb (in) : bcb
 */
STATIC_INLINE bool
pgbuf_bcb_should_be_moved_to_bottom_lru (const PGBUF_BCB * bcb)
{
  return (bcb->flags & PGBUF_BCB_MOVE_TO_LRU_BOTTOM_FLAG) != 0;
}

/*
 * pgbuf_set_to_vacuum () - notify that page will likely be accessed by vacuum
 *
 * return        : void
 * thread_p (in) : thread entry
 * page (in)     : page
 */
void
pgbuf_notify_vacuum_follows (THREAD_ENTRY * thread_p, PAGE_PTR page)
{
  PGBUF_BCB *bcb;

  CAST_PGPTR_TO_BFPTR (bcb, page);
  pgbuf_bcb_update_flags (thread_p, bcb, PGBUF_BCB_TO_VACUUM_FLAG, 0);
}

/*
 * pgbuf_bcb_is_flushing () - is page going to be accessed by vacuum?
 *
 * return   : true/false
 * bcb (in) : bcb
 */
STATIC_INLINE bool
pgbuf_bcb_is_to_vacuum (const PGBUF_BCB * bcb)
{
  return (bcb->flags & PGBUF_BCB_TO_VACUUM_FLAG) != 0;
}

/*
 * pgbuf_bcb_avoid_victim () - should bcb be avoid for victimization?
 *
 * return   : true/false
 * bcb (in) : bcb
 *
 * note: no flag that invalidates a bcb victim candidacy
 */
STATIC_INLINE bool
pgbuf_bcb_avoid_victim (const PGBUF_BCB * bcb)
{
  return (bcb->flags & PGBUF_BCB_INVALID_VICTIM_CANDIDATE_MASK) != 0;
}

/*
 * pgbuf_bcb_get_pool_index () - get bcb pool index
 *
 * return   : pool index
 * bcb (in) : BCB
 */
STATIC_INLINE int
pgbuf_bcb_get_pool_index (const PGBUF_BCB * bcb)
{
  return (int) (bcb - pgbuf_Pool.BCB_table);
}

/*
 * pgbuf_bcb_register_avoid_deallocation () - avoid deallocating bcb's page.
 *
 * return   : void
 * bcb (in) : bcb
 */
STATIC_INLINE void
pgbuf_bcb_register_avoid_deallocation (PGBUF_BCB * bcb)
{
  assert ((bcb->count_fix_and_avoid_dealloc & 0x00008000) == 0);
  (void) ATOMIC_INC_32 (&bcb->count_fix_and_avoid_dealloc, 1);
}

/*
 * pgbuf_bcb_unregister_avoid_deallocation () - avoiding page deallocation no longer required
 *
 * return   : void
 * bcb (in) : bcb
 */
STATIC_INLINE void
pgbuf_bcb_unregister_avoid_deallocation (PGBUF_BCB * bcb)
{
  int count_crt;
  do
    {
      /* get bcb->count_fix_and_avoid_dealloc (volatile) */
      count_crt = bcb->count_fix_and_avoid_dealloc;
      assert ((count_crt & 0x00008000) == 0);
      if ((count_crt & PGBUF_BCB_AVOID_DEALLOC_MASK) > 0)
	{
	  /* we can decrement counter */
	}
      else
	{
	  /* interestingly enough, this case can happen. how?
	   *
	   * well, pgbuf_ordered_fix may be forced to unfix all pages currently held by transaction to fix a new page.
	   * all pages that are "less" than new page are marked to avoid deallocation and unfixed. then transaction is
	   * blocked on latching new page, which may take a while, pages previously unfixed can be victimized.
	   * when pgbuf_ordered_fix tries to fix back these pages, it will load them from disk and tadaa, the avoid
	   * deallocation count is 0. so we expect the case.
	   *
	   * note: avoid deallocation count is supposed to prevent vacuum workers from deallocating these pages.
	   *       so, victimizing a bcb marked to avoid deallocation is not perfectly safe. however, the likelihood of
	   *       page really getting deallocated is ... almost zero. the alternative of avoiding victimization when
	   *       bcb's are marked for deallocation is much more complicated and poses serious risks (what if we leak
	   *       the counter and prevent bcb from being victimized indefinitely?). so, we prefer the existing risks.
	   */
	  er_log_debug (ARG_FILE_LINE,
			"pgbuf_bcb_unregister_avoid_deallocation: bcb %p, vpid = %d|%d was probably victimized.\n",
			bcb, VPID_AS_ARGS (&bcb->vpid));
	  break;
	}
    }
  while (!ATOMIC_CAS_32 (&bcb->count_fix_and_avoid_dealloc, count_crt, count_crt - 1));
}

/*
 * pgbuf_bcb_should_avoid_deallocation () - should avoid deallocating page?
 *
 * return   : true/false
 * bcb (in) : bcb
 */
STATIC_INLINE bool
pgbuf_bcb_should_avoid_deallocation (const PGBUF_BCB * bcb)
{
  assert (bcb->count_fix_and_avoid_dealloc >= 0);
  assert ((bcb->count_fix_and_avoid_dealloc & 0x00008000) == 0);
  return (bcb->count_fix_and_avoid_dealloc & PGBUF_BCB_AVOID_DEALLOC_MASK) != 0;
}

/*
 * pgbuf_bcb_check_and_reset_fix_and_avoid_dealloc () - check avoid deallocation is 0 and reset the whole bcb field.
 *
 * return    : void
 * bcb (in)  : bcb
 * file (in) : caller file
 * line (in) : caller line
 *
 * note: avoid deallocation is allowed to be non-zero due to pgbuf_ordered_fix and the possibility of victimizing its
 *       bcb. avoid crashing the server and just issue a warning.
 */
STATIC_INLINE void
pgbuf_bcb_check_and_reset_fix_and_avoid_dealloc (PGBUF_BCB * bcb, const char *file, int line)
{
  if (pgbuf_bcb_should_avoid_deallocation (bcb))
    {
      er_log_debug (file, line, "warning: bcb %p, vpid = %d|%d, should not have avoid deallocation marker.\n",
		    bcb, VPID_AS_ARGS (&bcb->vpid));
    }
  bcb->count_fix_and_avoid_dealloc = 0;
}

/*
 * pgbuf_bcb_register_fix () - register page fix
 *
 * return   : void
 * bcb (in) : bcb
 */
STATIC_INLINE void
pgbuf_bcb_register_fix (PGBUF_BCB * bcb)
{
  /* note: we only register to detect hot pages. once we hit the threshold, we are no longer required to fix it. */
  if (bcb->count_fix_and_avoid_dealloc < (PGBUF_FIX_COUNT_THRESHOLD << PGBUF_BCB_COUNT_FIX_SHIFT_BITS))
    {
#if !defined (NDEBUG)
      int newval =
#endif /* !NDEBUG */
	ATOMIC_INC_32 (&bcb->count_fix_and_avoid_dealloc, 1 << PGBUF_BCB_COUNT_FIX_SHIFT_BITS);
      assert (newval >= (1 << PGBUF_BCB_COUNT_FIX_SHIFT_BITS));
      assert (bcb->count_fix_and_avoid_dealloc >= (1 << PGBUF_BCB_COUNT_FIX_SHIFT_BITS));
    }
}

/*
 * pgbuf_bcb_is_hot () - is bcb hot (was fixed more then threshold times?)
 *
 * return   : true/false
 * bcb (in) : bcb
 */
STATIC_INLINE bool
pgbuf_bcb_is_hot (const PGBUF_BCB * bcb)
{
  assert (bcb->count_fix_and_avoid_dealloc >= 0);
  return bcb->count_fix_and_avoid_dealloc >= (PGBUF_FIX_COUNT_THRESHOLD << PGBUF_BCB_COUNT_FIX_SHIFT_BITS);
}

/*
 * pgbuf_lfcq_add_lru_with_victims () - add lru list to queue of lists that can be victimized. this queue was designed
 *                                      so victimizers can find a list with victims quickly without iterating through
 *                                      many lists that are full.
 *
 * return        : true if list was added, false if it was already added by someone else.
 * lru_list (in) : lru list
 */
STATIC_INLINE bool
pgbuf_lfcq_add_lru_with_victims (PGBUF_LRU_LIST * lru_list)
{
  int old_flags = lru_list->flags;

  if (old_flags & PGBUF_LRU_VICTIM_LFCQ_FLAG)
    {
      /* already added. */
      return false;
    }

  /* use compare & swap because we cannot allow two threads adding same list in queue */
  if (ATOMIC_CAS_32 (&lru_list->flags, old_flags, old_flags | PGBUF_LRU_VICTIM_LFCQ_FLAG))
    {
      /* add to queues. we keep private and shared lists separated. */
      if (PGBUF_IS_PRIVATE_LRU_INDEX (lru_list->index))
	{
	  /* private list */
	  if (pgbuf_Pool.private_lrus_with_victims->produce (lru_list->index))
	    {
	      return true;
	    }
	}
      else
	{
	  /* shared list */
	  if (pgbuf_Pool.shared_lrus_with_victims->produce (lru_list->index))
	    {
	      return true;
	    }
	}
      /* clear the flag */
      lru_list->flags &= ~PGBUF_LRU_VICTIM_LFCQ_FLAG;
    }

  /* not added */
  return false;
}

/*
 * pgbuf_lfcq_get_victim_from_private_lru () - get a victim from a private list in lock-free queues.
 *
 * return               : victim or NULL
 * thread_p (in)        : thread entry
 * restricted (in)      : true if victimizing is restricted to big private lists
 */
static PGBUF_BCB *
pgbuf_lfcq_get_victim_from_private_lru (THREAD_ENTRY * thread_p, bool restricted)
{
#define PERF(id) if (detailed_perf) perfmon_inc_stat (thread_p, id)

  int lru_idx;
  PGBUF_LRU_LIST *lru_list;
  PGBUF_BCB *victim = NULL;
  bool detailed_perf = perfmon_is_perf_tracking_and_active (PERFMON_ACTIVATION_FLAG_PB_VICTIMIZATION);
  bool added_back = false;

  if (pgbuf_Pool.private_lrus_with_victims == NULL)
    {
      return NULL;
    }
  assert (pgbuf_Pool.big_private_lrus_with_victims != NULL);

  if (pgbuf_Pool.big_private_lrus_with_victims->consume (lru_idx))
    {
      /* prioritize big lists */
      PERF (PSTAT_PB_LFCQ_LRU_PRV_GET_CALLS);
      PERF (PSTAT_PB_LFCQ_LRU_PRV_GET_BIG);
    }
  else
    {
      if (restricted)
	{
	  return NULL;
	}
      PERF (PSTAT_PB_LFCQ_LRU_PRV_GET_CALLS);
      if (!pgbuf_Pool.private_lrus_with_victims->consume (lru_idx))
	{
	  /* empty handed */
	  PERF (PSTAT_PB_LFCQ_LRU_PRV_GET_EMPTY);
	  return NULL;
	}
    }
  assert (PGBUF_IS_PRIVATE_LRU_INDEX (lru_idx));

  lru_list = PGBUF_GET_LRU_LIST (lru_idx);
  if (PGBUF_LRU_LIST_COUNT (lru_list) > PBGUF_BIG_PRIVATE_MIN_SIZE
      && PGBUF_LRU_LIST_COUNT (lru_list) > 2 * lru_list->quota && lru_list->count_vict_cand > 1)
    {
      /* add big private lists back immediately */
      if (pgbuf_Pool.big_private_lrus_with_victims->produce (lru_idx))
	{
	  added_back = true;
	}
    }

  /* get victim from list */
  victim = pgbuf_get_victim_from_lru_list (thread_p, lru_idx);
  PERF (victim != NULL ? PSTAT_PB_VICTIM_OTHER_PRIVATE_LRU_SUCCESS : PSTAT_PB_VICTIM_OTHER_PRIVATE_LRU_FAIL);

  if (added_back)
    {
      /* already added back to queue */
      return victim;
    }

  if (lru_list->count_vict_cand > 0 && PGBUF_LRU_LIST_IS_OVER_QUOTA (lru_list))
    {
      if (pgbuf_Pool.private_lrus_with_victims->produce (lru_idx))
	{
	  return victim;
	}
    }

  /* we're not adding the list back to the queue... so we need to reflect that in the list flags. next time when a new
   * candidate is added, lru list should also be added to the queue.
   *
   * note: we can have a race here. candidates are 0 now and incremented before we manage to change the victim
   *       counter. we should not worry that much, the list will be added by pgbuf_adjust_quotas eventually.
   */
  assert ((lru_list->flags & PGBUF_LRU_VICTIM_LFCQ_FLAG) != 0);
  /* note: we are not using an atomic operation here, because this is the only flag and we are certain no one else
   *       changes it from set to cleared. however, if more flags are added, or more cases that should clear the flag,
   *       then consider replacing with some atomic operation. */
  lru_list->flags &= ~PGBUF_LRU_VICTIM_LFCQ_FLAG;

  return victim;

#undef PERF
}

/*
 * pgbuf_lfcq_get_victim_from_shared_lru () - get a victim from a shared list in lock-free queues.
 *
 * return              : victim or NULL
 * thread_p (in)       : thread entry
 * multi_threaded (in) : true if multi-threaded system
 */
static PGBUF_BCB *
pgbuf_lfcq_get_victim_from_shared_lru (THREAD_ENTRY * thread_p, bool multi_threaded)
{
#define PERF(id) if (detailed_perf) perfmon_inc_stat (thread_p, id)

  int lru_idx;
  PGBUF_LRU_LIST *lru_list;
  PGBUF_BCB *victim = NULL;
  bool detailed_perf = perfmon_is_perf_tracking_and_active (PERFMON_ACTIVATION_FLAG_PB_VICTIMIZATION);

  PERF (PSTAT_PB_LFCQ_LRU_SHR_GET_CALLS);

  if (!pgbuf_Pool.shared_lrus_with_victims->consume (lru_idx))
    {
      /* no list has candidates! */
      PERF (PSTAT_PB_LFCQ_LRU_SHR_GET_EMPTY);
      return NULL;
    }
  /* popped a list with victim candidates from queue */
  assert (PGBUF_IS_SHARED_LRU_INDEX (lru_idx));

  lru_list = PGBUF_GET_LRU_LIST (lru_idx);
  victim = pgbuf_get_victim_from_lru_list (thread_p, lru_idx);
  PERF (victim != NULL ? PSTAT_PB_VICTIM_SHARED_LRU_SUCCESS : PSTAT_PB_VICTIM_SHARED_LRU_FAIL);

  /* no victim found in first step, but flush thread ran and candidates can be found, try again */
  if (victim == NULL && multi_threaded == false && lru_list->count_vict_cand > 0)
    {
      victim = pgbuf_get_victim_from_lru_list (thread_p, lru_idx);
      PERF (victim != NULL ? PSTAT_PB_VICTIM_SHARED_LRU_SUCCESS : PSTAT_PB_VICTIM_SHARED_LRU_FAIL);
    }

  if ((multi_threaded || victim != NULL) && lru_list->count_vict_cand > 0)
    {
      /* add lru list back to queue */
      if (pgbuf_Pool.shared_lrus_with_victims->produce (lru_idx))
	{
	  return victim;
	}
      else
	{
	  /* we couldn't add to queue. it usually does not happen, but a consumer can be preempted for a long time,
	   * temporarily creating the impression that queue is full. it will be added later, when a new victim
	   * candidate shows up or when adjust quota checks it. */
	  /* fall through */
	}
    }

  /* we're not adding the list back to the queue... so we need to reflect that in the list flags. next time when a new
   * candidate is added, lru list should also be added to the queue.
   *
   * note: we can have a race here. candidates are 0 now and incremented before we manage to change the victim
   *       counter. we should not worry that much, the list will be added by pgbuf_adjust_quotas eventually.
   */
  assert ((lru_list->flags & PGBUF_LRU_VICTIM_LFCQ_FLAG) != 0);
  /* note: we are not using an atomic operation here, because this is the only flag and we are certain no one else
   *       changes it from set to cleared. however, if more flags are added, or more cases that should clear the flag,
   *       then consider replacing with some atomic operation. */
  lru_list->flags &= ~PGBUF_LRU_VICTIM_LFCQ_FLAG;

  return victim;

#undef PERF
}

/*
 * pgbuf_lru_list_from_bcb () - get lru list of bcb
 *
 * return   : lru list
 * bcb (in) : bcb
 */
STATIC_INLINE PGBUF_LRU_LIST *
pgbuf_lru_list_from_bcb (const PGBUF_BCB * bcb)
{
  assert (PGBUF_IS_BCB_IN_LRU (bcb));

  return PGBUF_GET_LRU_LIST (pgbuf_bcb_get_lru_index (bcb));
}

/*
 * pgbuf_bcb_register_hit_for_lru () - register hit when bcb is unfixed for its current lru.
 *
 * return   : void
 * bcb (in) : BCB
 */
STATIC_INLINE void
pgbuf_bcb_register_hit_for_lru (PGBUF_BCB * bcb)
{
  assert (PGBUF_IS_BCB_IN_LRU (bcb));

  if (bcb->hit_age < pgbuf_Pool.quota.adjust_age)
    {
      pgbuf_Pool.monitor.lru_hits[pgbuf_bcb_get_lru_index (bcb)]++;
      bcb->hit_age = pgbuf_Pool.quota.adjust_age;
    }
}

/*
 * pgbuf_is_io_stressful () - is io stressful (are pages waiting for victims?)
 *
 * return    : true/false
 */
bool
pgbuf_is_io_stressful (void)
{
#if defined (SERVER_MODE)
  /* we consider the IO stressful if threads end up waiting for victims */
  return !pgbuf_Pool.direct_victims.waiter_threads_low_priority->is_empty ();
#else /* !SERVER_MODE */
  return false;
#endif /* !SERVER_MODE */
}

/*
 * pgbuf_is_hit_ratio_low () - is page buffer hit ratio low? currently target is set to 99.9%.
 *
 * return : true/false
 */
STATIC_INLINE bool
pgbuf_is_hit_ratio_low (void)
{
#define PGBUF_MIN_VICTIM_REQ                10	/* set a minimum number of requests */
#define PGBUF_DESIRED_HIT_VS_MISS_RATE      1000	/* 99.9% hit ratio */

  return (pgbuf_Pool.monitor.lru_victim_req_cnt > PGBUF_MIN_VICTIM_REQ
	  && pgbuf_Pool.monitor.lru_victim_req_cnt * PGBUF_DESIRED_HIT_VS_MISS_RATE > pgbuf_Pool.monitor.fix_req_cnt);

#undef PGBUF_DESIRED_HIT_VS_MISS_RATE
#undef PGBUF_MIN_VICTIM_REQ
}

#if defined (SERVER_MODE)
/*
 * pgbuf_bcbmon_lock () - monitor and lock bcb mutex
 *
 * return           : void
 * bcb (in)         : BCB to lock
 * caller_line (in) : caller line
 */
static void
pgbuf_bcbmon_lock (PGBUF_BCB * bcb, int caller_line)
{
  int index = thread_get_current_entry_index ();
  PGBUF_MONITOR_BCB_MUTEX *monitor_bcb_mutex = &pgbuf_Pool.monitor.bcb_locks[index];

  assert_release (pgbuf_Monitor_locks);

  if (monitor_bcb_mutex->bcb != NULL)
    {
      /* already have a bcb mutex. we cannot lock another one unless try lock is used. */
      PGBUF_ABORT_RELEASE ();
    }
  if (monitor_bcb_mutex->bcb_second != NULL)
    {
      /* already have a bcb mutex. we cannot lock another one unless try lock is used. */
      PGBUF_ABORT_RELEASE ();
    }
  if (bcb->owner_mutex == index)
    {
      /* double lock */
      PGBUF_ABORT_RELEASE ();
    }
  /* ok, we can lock */
  (void) pthread_mutex_lock (&bcb->mutex);
  if (bcb->owner_mutex >= 0)
    {
      /* somebody else has mutex? */
      PGBUF_ABORT_RELEASE ();
    }
  monitor_bcb_mutex->bcb = bcb;
  monitor_bcb_mutex->line = caller_line;
  bcb->owner_mutex = index;
}

/*
 * pgbuf_bcbmon_trylock () - monitor and try locking bcb mutex. do not wait if it is already locked
 *
 * return           : try lock result
 * bcb (in)         : BCB to lock
 * caller_line (in) : caller line
 */
static int
pgbuf_bcbmon_trylock (PGBUF_BCB * bcb, int caller_line)
{
  int index = thread_get_current_entry_index ();
  int rv;
  PGBUF_MONITOR_BCB_MUTEX *monitor_bcb_mutex = &pgbuf_Pool.monitor.bcb_locks[index];

  assert_release (pgbuf_Monitor_locks);

  if (bcb->owner_mutex == index)
    {
      /* double lock */
      PGBUF_ABORT_RELEASE ();
    }
  if (monitor_bcb_mutex->bcb != NULL && monitor_bcb_mutex->bcb_second != NULL)
    {
      /* two bcb's are already locked. */
      PGBUF_ABORT_RELEASE ();
    }
  if (monitor_bcb_mutex->bcb != NULL && monitor_bcb_mutex->bcb == bcb)
    {
      /* same bcb is already locked?? */
      PGBUF_ABORT_RELEASE ();
    }
  /* try lock */
  rv = pthread_mutex_trylock (&bcb->mutex);
  if (rv == 0)
    {
      /* success. monitor it. */
      if (monitor_bcb_mutex->bcb == NULL)
	{
	  monitor_bcb_mutex->bcb = bcb;
	  monitor_bcb_mutex->line = caller_line;
	}
      else
	{
	  monitor_bcb_mutex->bcb_second = bcb;
	  monitor_bcb_mutex->line_second = caller_line;
	}
      bcb->owner_mutex = index;
    }
  else
    {
      /* failed */
    }
  return rv;
}

/*
 * pgbuf_bcbmon_unlock () - monitor and unlock BCB mutex
 *
 * return   : void
 * bcb (in) : BCB to unlock
 */
static void
pgbuf_bcbmon_unlock (PGBUF_BCB * bcb)
{
  int index = thread_get_current_entry_index ();
  PGBUF_MONITOR_BCB_MUTEX *monitor_bcb_mutex = &pgbuf_Pool.monitor.bcb_locks[index];

  assert_release (pgbuf_Monitor_locks);

  /* should be monitored */
  if (bcb->owner_mutex != index)
    {
      /* I did not lock it?? */
      PGBUF_ABORT_RELEASE ();
    }
  bcb->owner_mutex = -1;

  if (monitor_bcb_mutex->bcb == bcb)
    {
      /* remove bcb from monitor. */
      monitor_bcb_mutex->bcb = NULL;
    }
  else if (monitor_bcb_mutex->bcb_second == bcb)
    {
      /* remove bcb from monitor */
      monitor_bcb_mutex->bcb_second = NULL;
    }
  else
    {
      /* I did not monitor it?? */
      PGBUF_ABORT_RELEASE ();
    }

  pthread_mutex_unlock (&bcb->mutex);
}

/*
 * pgbuf_bcbmon_check_own () - check current thread owns bcb mutex.
 *
 * return   : void
 * bcb (in) : BCB
 *
 * note: monitoring page buffer locks must be activated
 */
static void
pgbuf_bcbmon_check_own (PGBUF_BCB * bcb)
{
  int index = thread_get_current_entry_index ();
  PGBUF_MONITOR_BCB_MUTEX *monitor_bcb_mutex = &pgbuf_Pool.monitor.bcb_locks[index];

  assert_release (pgbuf_Monitor_locks);

  if (bcb->owner_mutex != index)
    {
      /* not owned */
      PGBUF_ABORT_RELEASE ();
    }
  if (monitor_bcb_mutex->bcb != bcb && monitor_bcb_mutex->bcb_second != bcb)
    {
      /* not monitored? */
      PGBUF_ABORT_RELEASE ();
    }
}

/*
 * pgbuf_bcbmon_check_mutex_leaks () - check for mutex leaks. must be called on exit points where no BCB should be
 *                                     locked.
 *
 * note: only works if page buffer lock monitoring is enabled.
 */
static void
pgbuf_bcbmon_check_mutex_leaks (void)
{
  int index = thread_get_current_entry_index ();
  PGBUF_MONITOR_BCB_MUTEX *monitor_bcb_mutex = &pgbuf_Pool.monitor.bcb_locks[index];

  assert_release (pgbuf_Monitor_locks);

  if (monitor_bcb_mutex->bcb != NULL)
    {
      PGBUF_ABORT_RELEASE ();
    }
  if (monitor_bcb_mutex->bcb_second != NULL)
    {
      PGBUF_ABORT_RELEASE ();
    }
}
#endif /* SERVER_MODE */

/*
 * pgbuf_flags_mask_sanity_check () - check flags mask do not overlap!
 *
 */
static void
pgbuf_flags_mask_sanity_check (void)
{
  /* sanity check: make sure the masks for bcb flags, zone and lru index do not overlap. this should be immediately
   * caught, so abort the server whenever happens. */
  if (PGBUF_BCB_FLAGS_MASK & PGBUF_ZONE_MASK)
    {
      PGBUF_ABORT_RELEASE ();
    }
  if (PGBUF_BCB_FLAGS_MASK & PGBUF_LRU_INDEX_MASK)
    {
      PGBUF_ABORT_RELEASE ();
    }
  if (PGBUF_ZONE_MASK & PGBUF_LRU_INDEX_MASK)
    {
      PGBUF_ABORT_RELEASE ();
    }
  if ((PGBUF_INVALID_ZONE | PGBUF_VOID_ZONE) & PGBUF_LRU_ZONE_MASK)
    {
      PGBUF_ABORT_RELEASE ();
    }
}

/*
 * pgbuf_lru_sanity_check () - check lru list is sane
 *
 * return   : void
 * lru (in) : lru list
 */
static void
pgbuf_lru_sanity_check (const PGBUF_LRU_LIST * lru)
{
#if !defined (NDEBUG)
  if (lru->top == NULL)
    {
      /* empty list */
      assert (lru->count_lru1 == 0 && lru->count_lru2 == 0 && lru->count_lru3 == 0 && lru->bottom == NULL
	      && lru->bottom_1 == NULL && lru->bottom_2 == NULL);
      return;
    }

  /* not empty */
  assert (lru->bottom != NULL);
  assert (lru->count_lru1 != 0 || lru->count_lru2 != 0 || lru->count_lru3 != 0);

  /* zone 1 */
  assert ((lru->count_lru1 == 0) == (lru->bottom_1 == NULL));
  if (lru->bottom_1 != NULL)
    {
      assert (pgbuf_bcb_get_zone (lru->bottom_1) == PGBUF_LRU_1_ZONE);
      assert (pgbuf_bcb_get_zone (lru->top) == PGBUF_LRU_1_ZONE);
      if (lru->bottom_1->next_BCB != NULL)
	{
	  if (pgbuf_bcb_get_zone (lru->bottom_1->next_BCB) == PGBUF_LRU_1_ZONE)
	    {
	      assert (false);
	    }
	  else if (pgbuf_bcb_get_zone (lru->bottom_1->next_BCB) == PGBUF_LRU_2_ZONE)
	    {
	      assert (lru->count_lru2 != 0 && lru->bottom_2 != NULL);
	    }
	  else
	    {
	      assert (lru->count_lru3 != 0);
	    }
	}
      else
	{
	  assert (lru->count_lru2 == 0 && lru->count_lru3 == 0 && lru->bottom_2 == NULL
		  && lru->bottom == lru->bottom_1);
	}
    }

  /* zone 2 */
  assert ((lru->count_lru2 == 0) == (lru->bottom_2 == NULL));
  if (lru->bottom_2 != NULL)
    {
      assert (pgbuf_bcb_get_zone (lru->bottom_2) == PGBUF_LRU_2_ZONE);
      assert (lru->bottom_2 != NULL || pgbuf_bcb_get_zone (lru->top) == PGBUF_LRU_2_ZONE);
      if (lru->bottom_2->next_BCB != NULL)
	{
	  if (pgbuf_bcb_get_zone (lru->bottom_2->next_BCB) == PGBUF_LRU_2_ZONE)
	    {
	      assert (false);
	    }
	  else if (pgbuf_bcb_get_zone (lru->bottom_2->next_BCB) == PGBUF_LRU_1_ZONE)
	    {
	      assert (false);
	    }
	  else if (lru->count_lru3 == 0)
	    {
	      assert (false);
	    }
	}
      else
	{
	  assert (lru->count_lru3 == 0 && lru->bottom == lru->bottom_2);
	}
    }
#endif /* !NDEBUG */
}

// TODO: find a better place for this, but not log_impl.h
/*
 * pgbuf_find_current_wait_msecs - find waiting times for current transaction
 *
 * return : wait_msecs...
 *
 * Note: Find the waiting time for the current transaction.
 */
STATIC_INLINE int
pgbuf_find_current_wait_msecs (THREAD_ENTRY * thread_p)
{
  LOG_TDES *tdes;		/* Transaction descriptor */
  int tran_index;

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tdes = LOG_FIND_TDES (tran_index);
  if (tdes != NULL)
    {
      return tdes->wait_msecs;
    }
  else
    {
      return 0;
    }
}

/*
 * pgbuf_get_page_flush_interval () - setup page flush daemon period based on system parameter
 */
void
pgbuf_get_page_flush_interval (bool & is_timed_wait, cubthread::delta_time & period)
{
  int page_flush_interval_msecs = prm_get_integer_value (PRM_ID_PAGE_BG_FLUSH_INTERVAL_MSECS);

  assert (page_flush_interval_msecs >= 0);

  if (page_flush_interval_msecs > 0)
    {
      // if page_flush_interval_msecs > 0 (zero) then loop for fixed interval
      is_timed_wait = true;
      period = std::chrono::milliseconds (page_flush_interval_msecs);
    }
  else
    {
      // infinite wait
      is_timed_wait = false;
    }
}

// *INDENT-OFF*
#if defined (SERVER_MODE)
static void
pgbuf_page_maintenance_execute (cubthread::entry & thread_ref)
{
  if (!BO_IS_SERVER_RESTARTED ())
    {
      // wait for boot to finish
      return;
    }

  /* page buffer maintenance thread adjust quota's based on thread activity. */
  pgbuf_adjust_quotas (&thread_ref);

  /* search lists and assign victims directly */
  pgbuf_direct_victims_maintenance (&thread_ref);
}
#endif /* SERVER_MODE */

#if defined (SERVER_MODE)
// class pgbuf_page_flush_daemon_task
//
//  description:
//    page flush daemon task
//
class pgbuf_page_flush_daemon_task : public cubthread::entry_task
{
  private:
    PERF_UTIME_TRACKER m_perf_track;

  public:
    pgbuf_page_flush_daemon_task ()
    {
      PERF_UTIME_TRACKER_START (NULL, &m_perf_track);
    }

    void execute (cubthread::entry & thread_ref) override
    {
      if (!BO_IS_SERVER_RESTARTED ())
	{
	  // wait for boot to finish
	  return;
	}

      // did not timeout, someone requested flush... run at least once
      bool force_one_run = pgbuf_Page_flush_daemon->was_woken_up ();
      bool stop_iteration = false;

      /* flush pages as long as necessary */
      while (force_one_run || pgbuf_keep_victim_flush_thread_running ())
	{
	  pgbuf_flush_victim_candidates (&thread_ref, prm_get_float_value (PRM_ID_PB_BUFFER_FLUSH_RATIO), &m_perf_track,
					 &stop_iteration);
	  force_one_run = false;
	  if (stop_iteration)
	    {
	      break;
	    }
	}

      /* performance tracking */
      if (m_perf_track.is_perf_tracking)
	{
	  /* register sleep time. */
	  PERF_UTIME_TRACKER_TIME_AND_RESTART (&thread_ref, &m_perf_track, PSTAT_PB_FLUSH_SLEEP);

	  /* update is_perf_tracking */
	  m_perf_track.is_perf_tracking = perfmon_is_perf_tracking ();
	}
      else
	{
	  /* update is_perf_tracking and start timer if it became true */
	  PERF_UTIME_TRACKER_START (&thread_ref, &m_perf_track);
	}
    }
};
#endif /* SERVER_MODE */

#if defined (SERVER_MODE)
static void
pgbuf_page_post_flush_execute (cubthread::entry & thread_ref)
{
  if (!BO_IS_SERVER_RESTARTED ())
    {
      // wait for boot to finish
      return;
    }

  /* assign flushed pages */
  if (pgbuf_assign_flushed_pages (&thread_ref))
    {
      /* reset daemon looper and be prepared to start over */
      pgbuf_Page_post_flush_daemon->reset_looper ();
    }
}
#endif /* SERVER_MODE */

#if defined (SERVER_MODE)
// class pgbuf_flush_control_daemon_task
//
//  description:
//    flush control daemon task
//
class pgbuf_flush_control_daemon_task : public cubthread::entry_task
{
  private:
    struct timeval m_end;
    bool m_first_run;

  public:
    pgbuf_flush_control_daemon_task ()
      : m_end ({0, 0})
      , m_first_run (true)
    {
    }

    int initialize ()
    {
      return fileio_flush_control_initialize ();
    }

    void execute (cubthread::entry & thread_ref) override
    {
      if (!BO_IS_SERVER_RESTARTED ())
	{
	  // wait for boot to finish
	  return;
	}

      if (m_first_run)
	{
	  gettimeofday (&m_end, NULL);
	  m_first_run = false;
	  return;
	}

      struct timeval begin, diff;
      int token_gen, token_consumed;

      gettimeofday (&begin, NULL);
      perfmon_diff_timeval (&diff, &m_end, &begin);

      int64_t diff_usec = diff.tv_sec * 1000000LL + diff.tv_usec;
      fileio_flush_control_add_tokens (&thread_ref, diff_usec, &token_gen, &token_consumed);

      gettimeofday (&m_end, NULL);
    }

    void retire (void) override
    {
      fileio_flush_control_finalize ();
      delete this;
    }
};
#endif /* SERVER_MODE */

#if defined (SERVER_MODE)
/*
 * pgbuf_page_maintenance_daemon_init () - initialize page maintenance daemon thread
 */
void
pgbuf_page_maintenance_daemon_init ()
{
  assert (pgbuf_Page_maintenance_daemon == NULL);

  cubthread::looper looper = cubthread::looper (std::chrono::milliseconds (100));
  cubthread::entry_callable_task *daemon_task = new cubthread::entry_callable_task (pgbuf_page_maintenance_execute);

  pgbuf_Page_maintenance_daemon = cubthread::get_manager ()->create_daemon (looper, daemon_task,
                                                                            "pgbuf_page_maintenance");
}
#endif /* SERVER_MODE */

#if defined (SERVER_MODE)
/*
 * pgbuf_page_flush_daemon_init () - initialize page flush daemon thread
 */
void
pgbuf_page_flush_daemon_init ()
{
  assert (pgbuf_Page_flush_daemon == NULL);

  cubthread::looper looper = cubthread::looper (pgbuf_get_page_flush_interval);
  pgbuf_page_flush_daemon_task *daemon_task = new pgbuf_page_flush_daemon_task ();

  pgbuf_Page_flush_daemon = cubthread::get_manager ()->create_daemon (looper, daemon_task, "pgbuf_page_flush");
}
#endif /* SERVER_MODE */

#if defined (SERVER_MODE)
/*
 * pgbuf_page_post_flush_daemon_init () - initialize page post flush daemon thread
 */
void
pgbuf_page_post_flush_daemon_init ()
{
  assert (pgbuf_Page_post_flush_daemon == NULL);

  std::array<cubthread::delta_time, 3> looper_interval {{
      std::chrono::milliseconds (1),
      std::chrono::milliseconds (10),
      std::chrono::milliseconds (100)
    }};

  cubthread::looper looper = cubthread::looper (looper_interval);
  cubthread::entry_callable_task *daemon_task = new cubthread::entry_callable_task (pgbuf_page_post_flush_execute);

  pgbuf_Page_post_flush_daemon = cubthread::get_manager ()->create_daemon (looper, daemon_task,
                                                                           "pgbuf_page_post_flush");
}
#endif /* SERVER_MODE */

#if defined (SERVER_MODE)
/*
 * pgbuf_flush_control_daemon_init () - initialize flush control daemon thread
 */
void
pgbuf_flush_control_daemon_init ()
{
  assert (pgbuf_Flush_control_daemon == NULL);

  pgbuf_flush_control_daemon_task *daemon_task = new pgbuf_flush_control_daemon_task ();

  if (daemon_task->initialize () != NO_ERROR)
    {
      delete daemon_task;
      return;
    }

  cubthread::looper looper = cubthread::looper (std::chrono::milliseconds (50));
  pgbuf_Flush_control_daemon = cubthread::get_manager ()->create_daemon (looper, daemon_task,
                                                                         "pgbuf_flush_control");
}
#endif /* SERVER_MODE */

#if defined (SERVER_MODE)
/*
 * pgbuf_daemons_init () - initialize page buffer daemon threads
 */
void
pgbuf_daemons_init ()
{
  pgbuf_page_maintenance_daemon_init ();
  pgbuf_page_flush_daemon_init ();
  pgbuf_page_post_flush_daemon_init ();
  pgbuf_flush_control_daemon_init ();
}
#endif /* SERVER_MODE */

#if defined (SERVER_MODE)
/*
 * pgbuf_daemons_destroy () - destroy page buffer daemon threads
 */
void
pgbuf_daemons_destroy ()
{
  cubthread::get_manager ()->destroy_daemon (pgbuf_Page_maintenance_daemon);
  cubthread::get_manager ()->destroy_daemon (pgbuf_Page_flush_daemon);
  cubthread::get_manager ()->destroy_daemon (pgbuf_Page_post_flush_daemon);
  cubthread::get_manager ()->destroy_daemon (pgbuf_Flush_control_daemon);
}
#endif /* SERVER_MODE */

void
pgbuf_daemons_get_stats (UINT64 * stats_out)
{
#if defined (SERVER_MODE)
  UINT64 *statsp = stats_out;

  if (pgbuf_Page_flush_daemon != NULL)
    {
      pgbuf_Page_flush_daemon->get_stats (statsp);
    }
  statsp += cubthread::daemon::get_stats_value_count ();

  if (pgbuf_Page_post_flush_daemon != NULL)
    {
      pgbuf_Page_post_flush_daemon->get_stats (statsp);
    }
  statsp += cubthread::daemon::get_stats_value_count ();

  if (pgbuf_Flush_control_daemon != NULL)
    {
      pgbuf_Flush_control_daemon->get_stats (statsp);
    }
  statsp += cubthread::daemon::get_stats_value_count ();

  if (pgbuf_Page_maintenance_daemon != NULL)
    {
      pgbuf_Page_maintenance_daemon->get_stats (statsp);
    }
#endif
}
// *INDENT-ON*

/*
 * pgbuf_is_page_flush_daemon_available () - check if page flush daemon is available
 * return: true if page flush daemon is available, false otherwise
 */
static bool
pgbuf_is_page_flush_daemon_available ()
{
#if defined (SERVER_MODE)
  return pgbuf_Page_flush_daemon != NULL;
#else
  return false;
#endif
}

static bool
pgbuf_is_temp_lsa (const log_lsa & lsa)
{
  return lsa == PGBUF_TEMP_LSA;
}

static void
pgbuf_init_temp_page_lsa (FILEIO_PAGE * io_page, PGLENGTH page_size)
{
  io_page->prv.lsa = PGBUF_TEMP_LSA;

  FILEIO_PAGE_WATERMARK *prv2 = fileio_get_page_watermark_pos (io_page, page_size);
  prv2->lsa = PGBUF_TEMP_LSA;
}

/*
 * pgbuf_scan_bcb_table () - scan bcb table to count snapshot data with no bcb mutex
 */
static void
pgbuf_scan_bcb_table ()
{
  int bufid;
  int flags;
  PGBUF_BCB *bufptr;
  PAGE_TYPE page_type;
  VPID vpid;
  PGBUF_STATUS_SNAPSHOT *show_status_snapshot = &pgbuf_Pool.show_status_snapshot;

  memset (show_status_snapshot, 0, sizeof (PGBUF_STATUS_SNAPSHOT));

  for (bufid = 0; bufid < pgbuf_Pool.num_buffers; bufid++)
    {
      bufptr = PGBUF_FIND_BCB_PTR (bufid);
      page_type = (PAGE_TYPE) (bufptr->iopage_buffer->iopage.prv.ptype);
      vpid = bufptr->vpid;
      flags = bufptr->flags;

      if ((flags & PGBUF_BCB_DIRTY_FLAG) != 0)
	{
	  show_status_snapshot->dirty_pages++;
	}
      else
	{
	  show_status_snapshot->clean_pages++;
	}

      if ((flags & PGBUF_INVALID_ZONE) != 0)
	{
	  show_status_snapshot->free_pages++;
	  continue;
	}

      if ((PGBUF_GET_ZONE (flags) == PGBUF_LRU_3_ZONE) && (flags & PGBUF_BCB_DIRTY_FLAG) != 0)
	{
	  show_status_snapshot->victim_candidate_pages++;
	}

      /* count temporary and permanent pages */
      if (pgbuf_is_temporary_volume (vpid.volid) == true)
	{
	  show_status_snapshot->num_temp_pages++;

	  assert ((page_type == PAGE_UNKNOWN) ||	/* dealloc pages, we don't know page type */
		  (page_type == PAGE_AREA) || (page_type == PAGE_QRESULT) ||	/* temporary page type */
		  (page_type == PAGE_EHASH) || (page_type == PAGE_VOLHEADER)	/* It can be temporary or permanent pages */
		  || (page_type == PAGE_VOLBITMAP) || (page_type == PAGE_FTAB));	/* It can be temporary or permanent pages */
	}
      else
	{
	  switch (page_type)
	    {
	    case PAGE_BTREE:
	      show_status_snapshot->num_index_pages++;
	      break;
	    case PAGE_OVERFLOW:
	    case PAGE_HEAP:
	      show_status_snapshot->num_data_pages++;
	      break;
	    case PAGE_CATALOG:
	    case PAGE_VOLBITMAP:
	    case PAGE_VOLHEADER:
	    case PAGE_FTAB:
	    case PAGE_EHASH:
	    case PAGE_VACUUM_DATA:
	    case PAGE_DROPPED_FILES:
	      show_status_snapshot->num_system_pages++;
	      break;
	    default:
	      /* dealloc pages, we don't know page type */
	      assert (page_type == PAGE_UNKNOWN);
	      break;
	    }
	}
    }
}

/*
 * pgbuf_start_scan () - start scan function for show page buffer status
 *   return: NO_ERROR, or ER_code
 *
 *   thread_p(in):
 *   type (in):
 *   arg_values(in):
 *   arg_cnt(in):
 *   ptr(in/out):
 */
int
pgbuf_start_scan (THREAD_ENTRY * thread_p, int type, DB_VALUE ** arg_values, int arg_cnt, void **ptr)
{
  SHOWSTMT_ARRAY_CONTEXT *ctx = NULL;
  const int num_cols = 19;
  time_t cur_time;
  int idx, i;
  int error = NO_ERROR;
  DB_VALUE *vals = NULL, db_val;
  unsigned long long delta, hit_delta, request_delta;
  double time_delta;
  double hit_rate;
  DB_DATA_STATUS data_status;
  PGBUF_STATUS status_accumulated = { };
  PGBUF_STATUS_SNAPSHOT *status_snapshot = &pgbuf_Pool.show_status_snapshot;
  PGBUF_STATUS_OLD *status_old = &pgbuf_Pool.show_status_old;

  *ptr = NULL;

#if defined(SERVER_MODE)
  (void) pthread_mutex_lock (&pgbuf_Pool.show_status_mutex);
#endif

  pgbuf_scan_bcb_table ();

  for (i = 0; i <= MAX_NTRANS; i++)
    {
      status_accumulated.num_hit += pgbuf_Pool.show_status[i].num_hit;
      status_accumulated.num_page_request += pgbuf_Pool.show_status[i].num_page_request;
      status_accumulated.num_pages_created += pgbuf_Pool.show_status[i].num_pages_created;
      status_accumulated.num_pages_written += pgbuf_Pool.show_status[i].num_pages_written;
      status_accumulated.num_pages_read += pgbuf_Pool.show_status[i].num_pages_read;
      status_accumulated.num_flusher_waiting_threads += pgbuf_Pool.show_status[i].num_flusher_waiting_threads;
    }

  ctx = showstmt_alloc_array_context (thread_p, 1, num_cols);
  if (ctx == NULL)
    {
      error = er_errid ();
      return error;
    }

  vals = showstmt_alloc_tuple_in_context (thread_p, ctx);
  if (vals == NULL)
    {
      error = er_errid ();
      goto exit_on_error;
    }

  cur_time = time (NULL);

  time_delta = difftime (cur_time, status_old->print_out_time) + 0.0001;	// avoid dividing by 0

  idx = 0;

  hit_rate = (status_accumulated.num_hit - status_old->num_hit) /
    ((status_accumulated.num_page_request - status_old->num_page_request) + 0.0000000000001);
  hit_rate = hit_rate * 100;

  db_make_double (&db_val, hit_rate);
  db_value_domain_init (&vals[idx], DB_TYPE_NUMERIC, 13, 10);
  error = numeric_db_value_coerce_to_num (&db_val, &vals[idx], &data_status);
  idx++;
  if (error != NO_ERROR)
    {
      goto exit_on_error;
    }

  delta = status_accumulated.num_hit - status_old->num_hit;
  db_make_bigint (&vals[idx], delta);
  idx++;

  delta = status_accumulated.num_page_request - status_old->num_page_request;
  db_make_bigint (&vals[idx], delta);
  idx++;

  db_make_int (&vals[idx], pgbuf_Pool.num_buffers);
  idx++;

  db_make_int (&vals[idx], PGBUF_IOPAGE_BUFFER_SIZE);
  idx++;

  db_make_int (&vals[idx], status_snapshot->free_pages);
  idx++;

  db_make_int (&vals[idx], status_snapshot->victim_candidate_pages);
  idx++;

  db_make_int (&vals[idx], status_snapshot->clean_pages);
  idx++;

  db_make_int (&vals[idx], status_snapshot->dirty_pages);
  idx++;

  db_make_int (&vals[idx], status_snapshot->num_index_pages);
  idx++;

  db_make_int (&vals[idx], status_snapshot->num_data_pages);
  idx++;

  db_make_int (&vals[idx], status_snapshot->num_system_pages);
  idx++;

  db_make_int (&vals[idx], status_snapshot->num_temp_pages);
  idx++;

  delta = status_accumulated.num_pages_created - status_old->num_pages_created;
  db_make_bigint (&vals[idx], delta);
  idx++;

  delta = status_accumulated.num_pages_written - status_old->num_pages_written;
  db_make_bigint (&vals[idx], delta);
  idx++;

  db_make_double (&db_val, delta / time_delta);
  db_value_domain_init (&vals[idx], DB_TYPE_NUMERIC, 20, 10);
  error = numeric_db_value_coerce_to_num (&db_val, &vals[idx], &data_status);
  idx++;
  if (error != NO_ERROR)
    {
      goto exit_on_error;
    }

  delta = status_accumulated.num_pages_read - status_old->num_pages_read;
  db_make_bigint (&vals[idx], delta);
  idx++;

  db_make_double (&db_val, delta / time_delta);
  db_value_domain_init (&vals[idx], DB_TYPE_NUMERIC, 20, 10);
  error = numeric_db_value_coerce_to_num (&db_val, &vals[idx], &data_status);
  idx++;
  if (error != NO_ERROR)
    {
      goto exit_on_error;
    }

  db_make_int (&vals[idx], status_accumulated.num_flusher_waiting_threads);
  idx++;

  assert (idx == num_cols);

  /* set now data to old data */
  status_old->num_hit = status_accumulated.num_hit;
  status_old->num_page_request = status_accumulated.num_page_request;
  status_old->num_pages_created = status_accumulated.num_pages_created;
  status_old->num_pages_written = status_accumulated.num_pages_written;
  status_old->num_pages_read = status_accumulated.num_pages_read;
  status_old->print_out_time = cur_time;

  *ptr = ctx;

#if defined(SERVER_MODE)
  pthread_mutex_unlock (&pgbuf_Pool.show_status_mutex);
#endif

  return NO_ERROR;

exit_on_error:

  if (ctx != NULL)
    {
      showstmt_free_array_context (thread_p, ctx);
    }

#if defined(SERVER_MODE)
  pthread_mutex_unlock (&pgbuf_Pool.show_status_mutex);
#endif

  return error;
}
