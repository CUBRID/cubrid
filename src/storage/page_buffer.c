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

/* minimum number of buffers */
#define PGBUF_MINIMUM_BUFFERS		(MAX_NTRANS * 10)

/* BCB holder list related constants */

/* Each transaction has its own free BCB holder list.
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

#define PGBUF_LRU_1_ZONE_THRESHOLD         50

/* The victim candidate flusher (performed as a deamon) finds
   victim candidates(fcnt == 0) from the bottom of each LRU list.
   and flushes them if they are in dirty state. */
#define PGBUF_LRU_SIZE         ((int) (PRM_PB_NBUFFERS/pgbuf_Pool.num_LRU_list))

/* maximum number of try in case of failure in allocating a BCB */
#define PGBUF_SLEEP_MAX                    10

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
#define PGBUF_IOPAGE_BUFFER_SIZE     ((size_t)(offsetof(PGBUF_IOPAGE_BUFFER, iopage) + \
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
  ((PGBUF_IOPAGE_BUFFER *)((char *)&(pgbuf_Pool.iopage_table[0])+(PGBUF_IOPAGE_BUFFER_SIZE*(i))))

#define PGBUF_FIND_BUFFER_GUARD(bufptr) (&bufptr->iopage_buffer->iopage.page[DB_PAGESIZE])

/* macros for casting pointers */
#define CAST_PGPTR_TO_BFPTR(bufptr, pgptr)                              \
  do {                                                                  \
  (bufptr) = ((PGBUF_BCB *)((PGBUF_IOPAGE_BUFFER *)                  \
      ((char *)pgptr - offsetof(PGBUF_IOPAGE_BUFFER, iopage.page)))->bcb);           \
  } while (0)

#define CAST_PGPTR_TO_IOPGPTR(io_pgptr, pgptr)                          \
  do {                                                                  \
    (io_pgptr) = (FILEIO_PAGE *) ((char *) pgptr                        \
                                  - offsetof(FILEIO_PAGE, page));       \
  } while (0)

#define CAST_BFPTR_TO_PGPTR(pgptr, bufptr)                              \
  do {                                                                  \
    (pgptr) = ((PAGE_PTR)                                               \
               ((char *)(bufptr->iopage_buffer) 			\
			+ offsetof(PGBUF_IOPAGE_BUFFER, iopage.page))); \
  } while (0)

/* check whether the given volume is auxiliary volume */
#define PGBUF_IS_AUXILARY_VOLUME(volid)                                 \
  ((volid) < LOG_DBFIRST_VOLID ? true : false)

/* PGBUF_HASH_VALUE (original(old) version and modified version) */
#define PGBUF_HASH_RATIO  8
#define PGBUF_HASH_SIZE   ((size_t) pgbuf_Pool.num_buffers * PGBUF_HASH_RATIO)

#define PGBUF_HASH_VALUE(vpid)                                          \
  (((vpid)->pageid | ((unsigned int)(vpid)->volid) << 24) % PGBUF_HASH_SIZE)

#if defined(SERVER_MODE)

#define MUTEX_LOCK_VIA_BUSY_WAIT(rv, m) \
        do { \
            int _loop; \
            rv = EBUSY; \
            for (_loop = 0; _loop < PRM_MUTEX_BUSY_WAITING_CNT; _loop++) { \
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

#if defined(CUBRID_DEBUG)
#define DEBUG_CHECK_INVALID_PAGE(pgptr, ret) \
  do { \
    if (pgbuf_Expensive_debug_free == true) \
      { \
        if (!(pgbuf_is_valid_page_ptr ((pgptr)))) \
          { \
            return (ret); \
          } \
      } \
  } while (0)
#else
#define DEBUG_CHECK_INVALID_PAGE(pgptr, ret)
#endif

/* BCB zone */
enum
{
  PGBUF_LRU_1_ZONE = 0, PGBUF_LRU_2_ZONE, PGBUF_INVALID_ZONE, PGBUF_VOID_ZONE
};

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

typedef struct pgbuf_buffer_lock PGBUF_BUFFER_LOCK;
typedef struct pgbuf_buffer_hash PGBUF_BUFFER_HASH;

typedef struct pgbuf_lru_list PGBUF_LRU_LIST;
typedef struct pgbuf_invalid_list PGBUF_INVALID_LIST;
typedef struct pgbuf_victim_candidate_list PGBUF_VICTIM_CANDIDATE_LIST;

typedef struct pgbuf_buffer_pool PGBUF_BUFFER_POOL;

/* BCB holder entry */
struct pgbuf_holder
{
  int tran_index;		/* the tran index of the holder */
  int fix_count;		/* the count of fix by the holder */
  PGBUF_BCB *bufptr;		/* pointer to BCB */
  PGBUF_HOLDER *next_holder;	/* the next BCB holder entry in the holder list
				   of BCB or free BCB holder list of tran. */
  PGBUF_HOLDER *tran_link;	/* the next BCB holder entry in the BCB holder
				   list of tran. */
#if !defined(NDEBUG)
  char fixed_at[64 * 1024];
  int fixed_at_size;
#endif				/* NDEBUG */
};

/* transaction related BCB holder list (it is owned by each transaction) */
struct pgbuf_holder_anchor
{
#if defined(SERVER_MODE)
  pthread_mutex_t list_mutex;	/* mutex for both BCB holder list */
#endif				/* SERVER_MODE */
  int num_free_cnt;		/* # of free BCB holder entries */
  int num_hold_cnt;		/* # of used BCB holder entries */
  PGBUF_HOLDER *tran_free_list;	/* free BCB holder list */
  PGBUF_HOLDER *tran_hold_list;	/* used(or hold) BCB holder list */
};

/* the entry(array structure) of free BCB holder list shared by transactions */
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
  int ipool;			/* Buffer pool index */
  VPID vpid;			/* Volume and page identifier of resident page */
  int fcnt;			/* Fix count */
  int latch_mode;		/* page latch mode */
  PGBUF_HOLDER *holder_list;	/* BCB holder list */
#if defined(SERVER_MODE)
  THREAD_ENTRY *next_wait_thrd;	/* BCB waiting queue */
#endif				/* SERVER_MODE */
  PGBUF_BCB *hash_next;		/* next hash chain */
  PGBUF_BCB *prev_BCB;		/* prev LRU chain */
  PGBUF_BCB *next_BCB;		/* next LRU or Invalid(Free) chain */
  bool dirty;			/* Is page dirty ? */
  bool avoid_victim;
  bool async_flush_request;
  int zone;			/* BCB zone */
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
#elif !defined(LINUX) && !defined(WINDOWS)
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
  int num_LRU_list;
  int last_flushed_LRU_list_idx;
  PGBUF_LRU_LIST *buf_LRU_list;
  PGBUF_INVALID_LIST buf_invalid_list;	/* buffer invalid BCB list */

  /*
   * the structures for maintaining information on BCB holders.
   * 'tran_holder_info' has entries as many as the # of transactions and
   * each entry maintains free BCB holder list and used BCB holder list
   * of the corresponding transaction.
   * 'tran_reserved_holder' has memory space for all BCB holder entries.
   */
  PGBUF_HOLDER_ANCHOR *tran_holder_info;
  PGBUF_HOLDER *tran_reserved_holder;

  /*
   * free BCB holder list shared by all the transactions.
   * When a transaction needs more free BCB holder entries,
   * the transaction allocates them one by one from this list.
   * However, the transaction never return the entries into this list.
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
};

/* victim candidate list */
/* One deamon thread performs flush task for victim candidates.
 * The deamon find and saves victim candidats using following list.
 * And then, based on the list, the deamon performs actual flush task.
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

static bool pgbuf_Expensive_debug_free = false;
static bool pgbuf_Expensive_debug_fetch = true;
#endif /* CUBRID_DEBUG */

#if defined(PAGE_STATISTICS)
static PGBUF_PS_INFO ps_info;
#endif /* PAGE_STATISTICS */

static bool pgbuf_is_temporary_volume (VOLID volid);
static int pgbuf_initialize_bcb_table (void);
static int pgbuf_initialize_hash_table (void);
static int pgbuf_initialize_lock_table (void);
static int pgbuf_initialize_lru_list (void);
static int pgbuf_initialize_invalid_list (void);
static int pgbuf_initialize_tran_holder (void);
static PGBUF_HOLDER *pgbuf_allocate_tran_holder_entry (THREAD_ENTRY *
						       thread_p, int tidx);
static PGBUF_HOLDER *pgbuf_find_current_tran_holder (THREAD_ENTRY * thread_p,
						     PGBUF_BCB * bufptr);
static int pgbuf_remove_tran_holder (PGBUF_BCB * bufptr,
				     PGBUF_HOLDER * holder);
#if !defined(NDEBUG)
static int pgbuf_latch_bcb_upon_fix (THREAD_ENTRY * thread_p,
				     PGBUF_BCB * bufptr, int request_mode,
				     int buf_lock_acquired,
				     PGBUF_LATCH_CONDITION condition,
				     const char *caller_file,
				     int caller_line);
static int pgbuf_unlatch_bcb_upon_unfix (THREAD_ENTRY * thread_p,
					 PGBUF_BCB * bufptr,
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
					 PGBUF_BCB * bufptr);
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
static PGBUF_BCB *pgbuf_get_victim_from_lru_list (const VPID * vpid);
static int pgbuf_invalidate_bcb_from_lru (PGBUF_BCB * bufptr);
static int pgbuf_relocate_top_lru (PGBUF_BCB * bufptr);
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
static char *pgbuf_get_waiting_tran_users_as_string (PGBUF_HOLDER *
						     holder_list);
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

#if defined(CUBRID_DEBUG)
static void pgbuf_scramble (FILEIO_PAGE * iopage);
static DISK_ISVALID pgbuf_is_valid_page (const VPID * vpid,
					 DISK_ISVALID (*fun) (const VPID *
							      vpid,
							      void *args),
					 void *args);
static bool pgbuf_is_valid_page_ptr (const PAGE_PTR pgptr);
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
  pgbuf_Pool.num_buffers = PRM_PB_NBUFFERS;
  if (pgbuf_Pool.num_buffers < PGBUF_MINIMUM_BUFFERS)
    {
#if defined(CUBRID_DEBUG)
      er_log_debug (ARG_FILE_LINE, "pgbuf_initialize: WARNING "
		    "Num_buffers = %d is too small. %d was assumed",
		    pgbuf_Pool.num_buffers, PGBUF_MINIMUM_BUFFERS);
#endif /* CUBRID_DEBUG */
      pgbuf_Pool.num_buffers = PGBUF_MINIMUM_BUFFERS;
    }

#if defined(CUBRID_DEBUG)
  {
    const char *env_value;

    env_value = envvar_get ("PB_EXPENSIVE_DEBUG_FREE");
    if (env_value != NULL)
      {
	pgbuf_Expensive_debug_free = atoi (env_value) != 0 ? true : false;
      }

    env_value = envvar_get ("PB_EXPENSIVE_DEBUG_FETCH");
    if (env_value != NULL)
      {
	pgbuf_Expensive_debug_fetch = atoi (env_value) != 0 ? true : false;
      }
  }
#endif /* CUBRID_DEBUG */

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

  if (pgbuf_initialize_tran_holder () != NO_ERROR)
    {
      goto error;
    }

  pgbuf_Pool.check_for_interrupts = false;
  pthread_mutex_init (&pgbuf_Pool.volinfo_mutex, NULL);
  pgbuf_Pool.last_perm_volid = LOG_MAX_DBVOLID;
  pgbuf_Pool.num_permvols_tmparea = 0;
  pgbuf_Pool.size_permvols_tmparea_volids = 0;
  pgbuf_Pool.permvols_tmparea_volids = NULL;

#if defined(PAGE_STATISTICS)
  if (pgbuf_initialize_statistics () < 0)
    {
      fprintf (stderr, "pgbuf_initialize_statistics() failed\n");
      pgbuf_finalize ();
      return ER_FAILED;
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
  for (i = 0; i < pgbuf_Pool.num_LRU_list; i++)
    {
      pthread_mutex_destroy (&pgbuf_Pool.buf_LRU_list[i].LRU_mutex);
    }
  free_and_init (pgbuf_Pool.buf_LRU_list);

  /* final task for invalid BCB list */
  pthread_mutex_destroy (&pgbuf_Pool.buf_invalid_list.invalid_mutex);

  /* final task for tran_holder_info */
  for (i = 0; i < MAX_NTRANS; i++)
    {
      pthread_mutex_destroy (&pgbuf_Pool.tran_holder_info[i].list_mutex);
    }
  free_and_init (pgbuf_Pool.tran_holder_info);
  free_and_init (pgbuf_Pool.tran_reserved_holder);

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
  int buf_lock_acquired;
  int waitsecs;
#if defined(SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */

#if defined(CUBRID_DEBUG)
  /* Make sure that the page has been allocated (i.e., is a valid page) */
  if (pgbuf_is_valid_page (vpid, NULL, NULL) != DISK_VALID)
    {
      return NULL;
    }
#else /* CUBRID_DEBUG */

  /* Do a simple check in non debugging mode */
  if (vpid->pageid < 0)
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_PB_BAD_PAGEID, 2,
	      vpid->pageid, fileio_get_volume_label (vpid->volid));
      return NULL;
    }
#endif /* CUBRID_DEBUG */

  if (condition == PGBUF_UNCONDITIONAL_LATCH)
    {
      /* Check the waitsecs of current transaction.
       * If the waitsecs is zero wait that means no wait,
       * change current request as a conditional request.
       */
      waitsecs = logtb_find_current_wait_secs (thread_p);

      if (waitsecs == LK_ZERO_WAIT || waitsecs == LK_FORCE_ZERO_WAIT)
	{
	  condition = PGBUF_CONDITIONAL_LATCH;
	}
    }

try_again:

  /* interrupt check */
#if defined(SERVER_MODE)
  if (thread_get_check_interrupt (thread_p) == true)
#endif /* SERVER_MODE */
    if (logtb_is_interrupted
	(thread_p, true, &pgbuf_Pool.check_for_interrupts) == true)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INTERRUPTED, 0);
	return NULL;
      }

  /* Normal process */
  /* latch_mode = PGBUF_LATCH_READ/PGBUF_LATCH_WRITE */
  hash_anchor = &pgbuf_Pool.buf_hash_table[PGBUF_HASH_VALUE (vpid)];

  buf_lock_acquired = false;
  bufptr = pgbuf_search_hash_chain (hash_anchor, vpid);

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

      if (newpg != NEW_PAGE)
	{
	  /* Record number of reads in statistics */
	  mnt_pb_ioreads (thread_p);
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
	      return NULL;
	    }
	  if (pgbuf_is_temporary_volume (vpid->volid) == true
	      && !LSA_IS_INIT_TEMP (&bufptr->iopage_buffer->iopage.prv.lsa))
	    {
	      LSA_SET_INIT_TEMP (&bufptr->iopage_buffer->iopage.prv.lsa);
	      pgbuf_set_dirty_buffer_ptr (thread_p, bufptr);
	    }
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
	}
      buf_lock_acquired = true;
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

  /* In case of NO_ERROR, bufptr->BCB_mutex has been released. */

  /* Dirty Pages Table Registration Pass */

  /* Curretly, do nothing.
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

  /* Record number of fetches in statistics */
  mnt_pb_fetches (thread_p);

  return (PAGE_PTR) (&(bufptr->iopage_buffer->iopage.page[0]));
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
#if defined(SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */

#if defined(CUBRID_DEBUG)
  LOG_LSA restart_lsa;
#endif /* CUBRID_DEBUG */

#if defined (NDEBUG)
  if (pgptr == NULL)
    {
      return;
    }
#else /* NDEBUG */
  assert (pgptr != NULL);
#endif /* NDEBUG */

#if defined(CUBRID_DEBUG)
  if (pgbuf_Expensive_debug_free == true)
    {
      if (!pgbuf_is_valid_page_ptr (pgptr))
	{
	  return;
	}
    }

  /*
   * If the buffer is dirty and the log sequence address of the buffer
   * has not changed since the database restart, a warning is given about
   * lack of logging
   */
  if (bufptr->dirty == true
      && !LSA_IS_INIT_TEMP (&bufptr->iopage_buffer->iopage.prv.lsa)
      && PGBUF_IS_AUXILARY_VOLUME (bufptr->vpid.volid) == false
      && !log_is_logged_since_restart (&bufptr->iopage_buffer->iopage.prv.
				       lsa))
    {
      er_log_debug (ARG_FILE_LINE, "pgbuf_unfix: WARNING: No logging on"
		    " dirty pageid = %d of Volume = %s.\n Recovery problems"
		    " may happen\n",
		    bufptr->vpid.pageid,
		    fileio_get_volume_label (bufptr->vpid.volid));
      /*
       * Do not give warnings on this page any longer. Set the LSA of the
       * buffer for this purposes
       */
      (void)
	pgbuf_set_lsa ((PAGE_PTR) (&bufptr->iopage_buffer->iopage.page[0]),
		       log_get_restart_lsa ());
      (void)
	pgbuf_set_lsa ((PAGE_PTR) (&bufptr->iopage_buffer->iopage.page[0]),
		       &restart_lsa);
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
		    "Pb_debug_free_bufptr: SYSTEM ERROR Freeing"
		    " too much buffer of pageid = %d of Volume = %s\n",
		    bufptr->vpid.pageid,
		    fileio_get_volume_label (bufptr->vpid.volid));
    }
#endif /* CUBRID_DEBUG */

  /* Get the address of the buffer from the page and free the buffer */
  CAST_PGPTR_TO_BFPTR (bufptr, pgptr);
  assert (!VPID_ISNULL (&bufptr->vpid));

  rv = pthread_mutex_lock (&bufptr->BCB_mutex);
#if !defined(NDEBUG)
  (void) pgbuf_unlatch_bcb_upon_unfix (thread_p, bufptr,
				       caller_file, caller_line);
#else /* NDEBUG */
  (void) pgbuf_unlatch_bcb_upon_unfix (thread_p, bufptr);
#endif /* NDEBUG */
  /* bufptr->BCB_mutex has been released in above function. */

#if defined(CUBRID_DEBUG)
  /*
   * CONSISTENCIES AND SCRAMBLES
   * You may want to tailor the following debugging block
   * since its operations and their implications are very expensive.
   * Too much I/O
   */
  if (pgbuf_Expensive_debug_free == true)
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
		  (void) pgbuf_flush_bcb (bufptr, true,
					  caller_file, caller_line);
#else /* NDEBUG */
		  (void) pgbuf_flush_bcb (bufptr, true);
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
	       * replacemnet.
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
 *                  transaction at the time of transaction termination
 *   return: void
 *
 * Note: At the time of transaction termination(commit or abort), there must
 *       be no buffers that were fixed by the transaction. In current CUBRID
 *       system, however, above situation has occured. In some later time, our
 *       system must be corrected to prevent above situation from occurring.
 */
void
pgbuf_unfix_all (THREAD_ENTRY * thread_p)
{
  int tran_index;
  PAGE_PTR pgptr;
  PGBUF_HOLDER *holder;
  PGBUF_BCB *bufptr;
#if defined(NDEBUG)
#else /* NDEBUG */
#if defined(CUBRID_DEBUG)
  int consistent;
#endif /* CUBRID_DEBUG */
  const char *latch_mode_str, *zone_str, *consistent_str;
#endif /* NDEBUG */

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);

  if (pgbuf_Pool.tran_holder_info[tran_index].num_hold_cnt > 0)
    {
      /* For each BCB holder entry of transaction's holder list */
      holder = pgbuf_Pool.tran_holder_info[tran_index].tran_hold_list;
      while (holder != NULL)
	{
	  CAST_BFPTR_TO_PGPTR (pgptr, holder->bufptr);

#if defined(NDEBUG)
	  pgbuf_unfix_and_init (thread_p, pgptr);

	  /* Within the execution of pgbuf_unfix(), the BCB holder entry is
	   * moved from the holder list of BCB to the free holder list of
	   * transaction, and the BCB holder entry is removed from the holder
	   * list of the transaction
	   */
	  holder = pgbuf_Pool.tran_holder_info[tran_index].tran_hold_list;
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

	  holder = holder->tran_link;
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
 *       2. For reqular pages(used to save persistent data such as meta data
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
#if defined(SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */

  DEBUG_CHECK_INVALID_PAGE (pgptr, ER_FAILED);

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
      /* If the page has been fixed more than one time, just unfix it. */
#if !defined(NDEBUG)
      if (pgbuf_unlatch_bcb_upon_unfix (thread_p, bufptr,
					caller_file, caller_line) != NO_ERROR)
#else /* NDEBUG */
      if (pgbuf_unlatch_bcb_upon_unfix (thread_p, bufptr) != NO_ERROR)
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
	  return ER_FAILED;
	}
      /*
       * In case of success, bufptr->BCB_mutex is still held by the caller.
       * In case of failure, the cause is the write() system call error.
       * The write() system call can be invoked without holding BCB_mutex
       * in pgbuf_flush_page_with_wal() and when the invocation of the
       * system call turns out as failure, ER_FAILED is returned
       * immediately. So, in case of failure, do not need to release the
       * BCB_mutex.
       */
    }

  /* save the pageid of the page temporarily. */
  temp_vpid = bufptr->vpid;
#if !defined(NDEBUG)
  if (pgbuf_unlatch_bcb_upon_unfix (thread_p, bufptr,
				    caller_file, caller_line) != NO_ERROR)
#else /* NDEBUG */
  if (pgbuf_unlatch_bcb_upon_unfix (thread_p, bufptr) != NO_ERROR)
#endif /* NDEBUG */
    {
      return ER_FAILED;
    }
  /* bufptr->BCB_mutex has been released in above function. */

  /* hold BCB_mutex again to invalidate the BCB */
  MUTEX_LOCK_VIA_BUSY_WAIT (rv, bufptr->BCB_mutex);

  /* check if the page should be invalidated. */
  if (VPID_ISNULL (&bufptr->vpid)
      || !VPID_EQ (&temp_vpid, &bufptr->vpid) || bufptr->fcnt > 0)
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
	      || bufptr->fcnt > 0)
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
#if defined(SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */

  DEBUG_CHECK_INVALID_PAGE (pgptr, NULL);

  /* Get the address of the buffer from the page. */
  CAST_PGPTR_TO_BFPTR (bufptr, pgptr);
  assert (!VPID_ISNULL (&bufptr->vpid));

  /* the caller is holding a page latch with PGBUF_LATCH_WRITE mode. */
  MUTEX_LOCK_VIA_BUSY_WAIT (rv, bufptr->BCB_mutex);
  if (bufptr->dirty == true)
    {
      if (pgbuf_flush_page_with_wal (thread_p, bufptr) != NO_ERROR)
	{
	  return NULL;
	}
      /* If above function returns failure,
       * the caller does not hold bufptr->BCB_mutex.
       * Otherwise, the caller is still holding bufptr->BCB_mutex.
       */
    }

  /* the caller is holding bufptr->BCB_mutex. */
  if (free_page == FREE)
    {
#if !defined(NDEBUG)
      if (pgbuf_unlatch_bcb_upon_unfix (thread_p, bufptr,
					caller_file, caller_line) != NO_ERROR)
#else /* NDEBUG */
      if (pgbuf_unlatch_bcb_upon_unfix (thread_p, bufptr) != NO_ERROR)
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

  DEBUG_CHECK_INVALID_PAGE (pgptr, NULL);

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
	  return NULL;
	}
      /* If above function returns failure,
       * the caller does not hold bufptr->BCB_mutex.
       * Otherwise, the caller is still holding bufptr->BCB_mutex.
       */
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
  PGBUF_BCB *prev_bufptr;
  PGBUF_VICTIM_CANDIDATE_LIST *victim_cand_list;
  int i, clean_count, cand_count, check_count, total_flushed_count;
  int lru_idx, start_lru_idx;
#if defined(SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */

  mnt_pb_victims (thread_p);

  er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE,
	  ER_LOG_FLUSH_VICTIM_STARTED, 0);
  er_log_debug (ARG_FILE_LINE, "start flush victim candidates\n");

  assert (flush_ratio == PGBUF_VICTIM_FLUSH_MIN_RATIO);
  check_count = MAX (1, (int) (PGBUF_LRU_SIZE * flush_ratio));

  victim_cand_list = ((PGBUF_VICTIM_CANDIDATE_LIST *)
		      malloc (sizeof (PGBUF_VICTIM_CANDIDATE_LIST)
			      * check_count));
  if (victim_cand_list == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      sizeof (PGBUF_VICTIM_CANDIDATE_LIST) * check_count);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  total_flushed_count = 0;
  lru_idx = ((pgbuf_Pool.last_flushed_LRU_list_idx + 1)
	     % pgbuf_Pool.num_LRU_list);
  start_lru_idx = lru_idx;

  do
    {
      clean_count = cand_count = 0;

      MUTEX_LOCK_VIA_BUSY_WAIT (rv,
				pgbuf_Pool.buf_LRU_list[lru_idx].LRU_mutex);
      bufptr = pgbuf_Pool.buf_LRU_list[lru_idx].LRU_bottom;

      while ((bufptr != NULL) && (bufptr->zone != PGBUF_LRU_1_ZONE)
	     && ((clean_count + cand_count) < check_count))
	{
	  if (bufptr->fcnt != 0)
	    {			/* BCB is in fixed state */
	      /* disconnect bufptr from LRU list only for reducing
	       * the possibility of checking if it is a victim candidate.
	       */
	      prev_bufptr = bufptr->prev_BCB;
	      if (bufptr == pgbuf_Pool.buf_LRU_list[lru_idx].LRU_bottom)
		{
		  pgbuf_Pool.buf_LRU_list[lru_idx].LRU_bottom =
		    bufptr->prev_BCB;
		}

	      if (bufptr->prev_BCB != NULL)
		{
		  (bufptr->prev_BCB)->next_BCB = bufptr->next_BCB;
		}

	      if (bufptr->next_BCB != NULL)
		{
		  (bufptr->next_BCB)->prev_BCB = bufptr->prev_BCB;
		}

	      bufptr->prev_BCB = bufptr->next_BCB = NULL;
	      bufptr->zone = PGBUF_VOID_ZONE;
	      bufptr = prev_bufptr;
	      continue;
	    }

	  /* BCB is in unfixed state */
	  if ((bufptr->dirty == false)
	      || (bufptr->latch_mode == PGBUF_LATCH_FLUSH))
	    {
	      clean_count++;
	    }
	  else
	    {
	      if (cand_count >= check_count)
		{
		  assert (0);
		  break;
		}

	      /* save victim candidate information temporarily. */
	      victim_cand_list[cand_count].bufptr = bufptr;
	      victim_cand_list[cand_count].vpid = bufptr->vpid;
	      LSA_COPY (&victim_cand_list[cand_count].recLSA,
			&bufptr->oldest_unflush_lsa);
	      cand_count++;
	    }
	  bufptr = bufptr->prev_BCB;
	}
      pthread_mutex_unlock (&pgbuf_Pool.buf_LRU_list[lru_idx].LRU_mutex);

      /* for each victim candidate, do flush task */
      for (i = 0; i < cand_count; i++)
	{
	  bufptr = victim_cand_list[i].bufptr;

	  MUTEX_LOCK_VIA_BUSY_WAIT (rv, bufptr->BCB_mutex);
	  /* flush condition check */
	  if (!VPID_EQ (&bufptr->vpid, &victim_cand_list[i].vpid)
	      || bufptr->dirty == false
	      || (bufptr->zone != PGBUF_LRU_2_ZONE
		  && bufptr->zone != PGBUF_LRU_1_ZONE)
	      || bufptr->latch_mode != PGBUF_NO_LATCH
	      || !(LSA_EQ (&bufptr->oldest_unflush_lsa,
			   &victim_cand_list[i].recLSA)))
	    {
	      pthread_mutex_unlock (&bufptr->BCB_mutex);
	      continue;
	    }

	  /* pgbuf_flush_bcb() will release bufptr->BCB_mutex
	   * regardless of its return value.
	   */
#if !defined(NDEBUG)
	  if (pgbuf_flush_bcb (thread_p, bufptr, false,
			       caller_file, caller_line) != NO_ERROR)
#else /* NDEBUG */
	  if (pgbuf_flush_bcb (thread_p, bufptr, false) != NO_ERROR)
#endif /* NDEBUG */
	    {
	      if (victim_cand_list != NULL)
		{
		  free_and_init (victim_cand_list);
		}
	      er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE,
		      ER_LOG_FLUSH_VICTIM_FINISHED, 1, total_flushed_count);
	      return ER_FAILED;
	    }

	  total_flushed_count++;
	}

      /* Note that we don't hold the mutex, however it will not be an issue.
       * Also note that we are updating the last_flushed_LRU_list_idx
       * whether the list has flushed or not.
       */
      pgbuf_Pool.last_flushed_LRU_list_idx = lru_idx;

      lru_idx = (lru_idx + 1) % pgbuf_Pool.num_LRU_list;
    }
  while (lru_idx != start_lru_idx);	/* check if we've visited all of the lists */

  if (victim_cand_list != NULL)
    {
      free_and_init (victim_cand_list);
    }

  er_log_debug (ARG_FILE_LINE, "pgbuf_flush_victim_candidate: "
		"flush %d pages from (%d) to (%d) list.",
		total_flushed_count, start_lru_idx,
		pgbuf_Pool.last_flushed_LRU_list_idx);

  er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE,
	  ER_LOG_FLUSH_VICTIM_FINISHED, 1, total_flushed_count);
  er_log_debug (ARG_FILE_LINE, "end flush victim candidates\n");

  return NO_ERROR;
}

/*
 * pgbuf_flush_checkpoint () - Flush any unfixed dirty page whose lsa
 *                                      is smaller than the last checkpoint lsa
 *   return: void
 *   last_chkpt_lsa(in): Last checkpoint log sequence address
 *   smallest_lsa(out): Smallest LSA of a dirty buffer in buffer pool
 *
 * Note: The function flushes and dirty unfixed page whose LSA is smaller that
 *       the last_chkpt_lsa, it returns the smallest_lsa from the remaining
 *       dirty buffers which were not flushed.
 *       This function is used by the log and recovery manager when a
 *       checkpoint is issued.
 */
#if !defined(NDEBUG)
void
pgbuf_flush_checkpoint_debug (THREAD_ENTRY * thread_p,
			      const LOG_LSA * last_chkpt_lsa,
			      LOG_LSA * smallest_lsa,
			      const char *caller_file, int caller_line)
#else /* NDEBUG */
void
pgbuf_flush_checkpoint (THREAD_ENTRY * thread_p,
			const LOG_LSA * last_chkpt_lsa,
			LOG_LSA * smallest_lsa)
#endif				/* NDEBUG */
{
  PGBUF_BCB *bufptr;
  int bufid;
#if defined(SERVER_MODE)
  int sleep_nsecs;
  int rv;

  sleep_nsecs = (PRM_LOG_CHECKPOINT_SLEEP_MSECS * 1000);
#endif /* SERVER_MODE */

  /* Things must be truly flushed up to this lsa */
  logpb_flush_log_for_wal (thread_p, last_chkpt_lsa);
  LSA_SET_NULL (smallest_lsa);

  /* Now, flush all unfixed dirty buffers */
  for (bufid = 0; bufid < pgbuf_Pool.num_buffers; bufid++)
    {
      bufptr = PGBUF_FIND_BCB_PTR (bufid);
      MUTEX_LOCK_VIA_BUSY_WAIT (rv, bufptr->BCB_mutex);
      /* flush condition check */
      if (bufptr->dirty == false || LSA_ISNULL (&bufptr->oldest_unflush_lsa))
	{
	  pthread_mutex_unlock (&bufptr->BCB_mutex);
	  continue;
	}

      /* flush when buffer is not fixed or was fixed by reader */
      if (LSA_LE (&bufptr->oldest_unflush_lsa, last_chkpt_lsa)
	  && (bufptr->latch_mode == PGBUF_NO_LATCH
	      || bufptr->latch_mode == PGBUF_LATCH_READ
	      || bufptr->latch_mode == PGBUF_LATCH_FLUSH))
	{
#if !defined(NDEBUG)
	  (void) pgbuf_flush_bcb (thread_p, bufptr, true,
				  caller_file, caller_line);
#else /* NDEBUG */
	  (void) pgbuf_flush_bcb (thread_p, bufptr, true);
#endif /* NDEBUG */

#if defined(SERVER_MODE)
	  /* Checkpoint Thread is writing data pages slowly to avoid IO burst */
	  if (sleep_nsecs > 0)
	    {
	      thread_sleep (0, sleep_nsecs);
	    }
#endif
	}
      else
	{
	  /* get the smallest oldest_unflush_lsa */
	  if (LSA_ISNULL (smallest_lsa)
	      || LSA_LT (&bufptr->oldest_unflush_lsa, smallest_lsa))
	    {
	      LSA_COPY (smallest_lsa, &bufptr->oldest_unflush_lsa);
	    }

	  pthread_mutex_unlock (&bufptr->BCB_mutex);
	}
    }
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
#if defined(CUBRID_DEBUG)
	  if (pgbuf_is_valid_page (vpid, NULL, NULL) != DISK_VALID)
	    {
	      return NULL;
	    }
#endif /* CUBRID_DEBUG */

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
      pgptr = (PAGE_PTR) (&(bufptr->iopage_buffer->iopage.page[0]));
      memcpy (area, (char *) pgptr + start_offset, length);

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
#if defined(CUBRID_DEBUG)
	  if (pgbuf_is_valid_page (vpid, NULL, NULL) != DISK_VALID)
	    {
	      return NULL;
	    }
#endif /* CUBRID_DEBUG */

	  /* Record number of reads in statistics */
	  mnt_pb_iowrites (thread_p);

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

  pgptr = pgbuf_fix (thread_p, vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
		     PGBUF_UNCONDITIONAL_LATCH);
  if (pgptr != NULL)
    {
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

#if defined(CUBRID_DEBUG)
  if (pgbuf_Expensive_debug_free == true)
    {
      if (!(pgbuf_is_valid_page_ptr (pgptr)))
	{
	  return;
	}
    }
#endif /* CUBRID_DEBUG */

  /* Get the address of the buffer from the page and set buffer dirty */
  CAST_PGPTR_TO_BFPTR (bufptr, pgptr);
  assert (!VPID_ISNULL (&bufptr->vpid));

#if defined(SERVER_MODE) && !defined(NDEBUG)
  if (bufptr->vpid.pageid == 0)
    {
      DISK_VAR_HEADER *vhdr = (DISK_VAR_HEADER *) pgptr;
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

  DEBUG_CHECK_INVALID_PAGE (pgptr, NULL);

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

  DEBUG_CHECK_INVALID_PAGE (pgptr, NULL);

  /* NOTE: Does not need to hold BCB_mutex since the page is fixed */

  /* Get the address of the buffer from the page and set buffer dirty */
  CAST_PGPTR_TO_BFPTR (bufptr, pgptr);

  /*
   * Don't change LSA of temporary volumes or auxiliary volumes.
   * (e.g., those of copydb, backupdb).
   */
  if (LSA_IS_INIT_TEMP (&bufptr->iopage_buffer->iopage.prv.lsa)
      || PGBUF_IS_AUXILARY_VOLUME (bufptr->vpid.volid) == true)
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

#if defined(CUBRID_DEBUG)
  if (pgbuf_Expensive_debug_free == true)
    {
      if (!(pgbuf_is_valid_page_ptr (pgptr)))
	{
	  VPID_SET_NULL (vpid);
	  return;
	}
    }
#endif /* CUBRID_DEBUG */

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

  DEBUG_CHECK_INVALID_PAGE (pgptr, NULL);

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

  DEBUG_CHECK_INVALID_PAGE (pgptr, NULL_PAGEID);

  /* NOTE: Does not need to hold BCB_mutex since the page is fixed */

  CAST_PGPTR_TO_BFPTR (bufptr, pgptr);
  return bufptr->vpid.pageid;
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

  DEBUG_CHECK_INVALID_PAGE (pgptr, NULL_PAGEID);

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

  return fileio_get_volume_label (bufptr->vpid.volid);
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
	      1, (pgbuf_Pool.num_buffers * PGBUF_BCB_SIZE));
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
      bufptr->holder_list = NULL;

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
      bufptr->zone = PGBUF_INVALID_ZONE;
      LSA_SET_NULL (&bufptr->oldest_unflush_lsa);

      /* link BCB and iopage buffer */
      ioptr = PGBUF_FIND_IOPAGE_PTR (i);
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
  size_t size;

  /* allocate memory space for the buffer lock table */
  size = thread_num_total_threads () * PGBUF_BUFFER_LOCK_SIZE;
  pgbuf_Pool.buf_lock_table = (PGBUF_BUFFER_LOCK *) malloc (size);
  if (pgbuf_Pool.buf_lock_table == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
	      1, size);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  /* initialize each entry of the buffer lock table */
  for (i = 0; i < thread_num_total_threads (); i++)
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
  pgbuf_Pool.num_LRU_list = PRM_PB_NUM_LRU_CHAINS;
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
 * pgbuf_initialize_tran_holder () -
 *   return: NO_ERROR, or ER_code
 */
static int
pgbuf_initialize_tran_holder (void)
{
  int alloc_size;
  int i, j, idx;

  pgbuf_Pool.tran_holder_info = (PGBUF_HOLDER_ANCHOR *)
    malloc (PGBUF_HOLDER_ANCHOR_SIZE * MAX_NTRANS);
  if (pgbuf_Pool.tran_holder_info == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
	      1, PGBUF_HOLDER_ANCHOR_SIZE * MAX_NTRANS);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  /* phase 1: allocate memory space that is used for BCB holder entries */
  alloc_size = MAX_NTRANS * PGBUF_DEFAULT_FIX_COUNT * PGBUF_HOLDER_SIZE;
  pgbuf_Pool.tran_reserved_holder = (PGBUF_HOLDER *) malloc (alloc_size);
  if (pgbuf_Pool.tran_reserved_holder == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
	      1, alloc_size);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  /* phase 2: initialize all the BCB holder entries */

  /*
   * Each transaction has both free holder list and used(held) holder list.
   * The free holder list of each transaction is initialized to
   * have PGBUF_DEFAULT_FIX_COUNT entries and the used holder list of
   * each transaction is initialized to have no entry.
   */
  for (i = 0; i < MAX_NTRANS; i++)
    {
      pthread_mutex_init (&pgbuf_Pool.tran_holder_info[i].list_mutex, NULL);
      pgbuf_Pool.tran_holder_info[i].num_hold_cnt = 0;
      pgbuf_Pool.tran_holder_info[i].num_free_cnt = PGBUF_DEFAULT_FIX_COUNT;
      pgbuf_Pool.tran_holder_info[i].tran_hold_list = NULL;
      pgbuf_Pool.tran_holder_info[i].tran_free_list
	= &(pgbuf_Pool.tran_reserved_holder[i * PGBUF_DEFAULT_FIX_COUNT]);

      for (j = 0; j < PGBUF_DEFAULT_FIX_COUNT; j++)
	{
	  idx = (i * PGBUF_DEFAULT_FIX_COUNT) + j;
	  pgbuf_Pool.tran_reserved_holder[idx].tran_index = i;
	  pgbuf_Pool.tran_reserved_holder[idx].fix_count = 0;
	  pgbuf_Pool.tran_reserved_holder[idx].bufptr = NULL;
	  pgbuf_Pool.tran_reserved_holder[idx].tran_link = NULL;

	  if (j == (PGBUF_DEFAULT_FIX_COUNT - 1))
	    {
	      pgbuf_Pool.tran_reserved_holder[idx].next_holder = NULL;
	    }
	  else
	    {
	      pgbuf_Pool.tran_reserved_holder[idx].next_holder
		= &(pgbuf_Pool.tran_reserved_holder[idx + 1]);
	    }
	}
    }

  /* phase 3: initialize free BCB holder list shared by all transactions */
  pthread_mutex_init (&pgbuf_Pool.free_holder_set_mutex, NULL);
  pgbuf_Pool.free_holder_set = NULL;
  pgbuf_Pool.free_index = -1;	/* -1 means that there is no free holder entry */

  return NO_ERROR;
}

/*
 * pgbuf_allocate_tran_holder_entry () - Allocates one buffer holder entry
 *   			from the free holder list of given transaction
 *   return: pointer to holder entry or NULL
 *   tidx(in): transaction entry index
 *
 * Note: If tidx is -1, then allocate the buffer holder entry from the free
 *       holder list of current transaction. If the free holder list is empty,
 *       allocate it from the list of free holder arrays that is shared.
 */
static PGBUF_HOLDER *
pgbuf_allocate_tran_holder_entry (THREAD_ENTRY * thread_p, int tidx)
{
  int tran_index;
  PGBUF_HOLDER *holder;
  PGBUF_HOLDER_SET *holder_set;
#if defined(SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */

  /* the caller is holding bufptr->BCB_mutex */
  if (tidx == -1)
    {
      tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
    }
  else
    {
      tran_index = tidx;
    }

  rv =
    pthread_mutex_lock (&pgbuf_Pool.tran_holder_info[tran_index].list_mutex);

  if (pgbuf_Pool.tran_holder_info[tran_index].tran_free_list != NULL)
    {
      /* allocate a BCB holder entry
       * from the free BCB holder list of given transaction */
      holder = pgbuf_Pool.tran_holder_info[tran_index].tran_free_list;
      pgbuf_Pool.tran_holder_info[tran_index].tran_free_list =
	holder->next_holder;
      pgbuf_Pool.tran_holder_info[tran_index].num_free_cnt -= 1;
    }
  else
    {
      /* holder == NULL : free BCB holder list is empty */

      /* allocate a BCB holder entry
       * from the free BCB holder list shared by all transactions.
       */
      rv = pthread_mutex_lock (&pgbuf_Pool.free_holder_set_mutex);
      if (pgbuf_Pool.free_index == -1)
	{
	  /* no usable free holder entry */
	  /* expand the free BCB holder list shared by transactions */
	  holder_set = (PGBUF_HOLDER_SET *) malloc (PGBUF_HOLDER_SET_SIZE);
	  if (holder_set == NULL)
	    {
	      pthread_mutex_unlock (&pgbuf_Pool.free_holder_set_mutex);
	      pthread_mutex_unlock (&pgbuf_Pool.tran_holder_info[tran_index].
				    list_mutex);
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
      holder->tran_index = tran_index;
      holder->tran_link = NULL;
    }

  /* connect the BCB holder entry at the head of transaction's holder list */
  holder->tran_link = pgbuf_Pool.tran_holder_info[tran_index].tran_hold_list;
  pgbuf_Pool.tran_holder_info[tran_index].tran_hold_list = holder;
  pgbuf_Pool.tran_holder_info[tran_index].num_hold_cnt += 1;
  pthread_mutex_unlock (&pgbuf_Pool.tran_holder_info[tran_index].list_mutex);

  return holder;
}

/*
 * pgbuf_find_current_tran_holder () - Find the holder entry of current
 *                                     transaction on the BCB holder list of
 *                                     given BCB
 *   return: pointer to holder entry or NULL
 *   bufptr(in):
 */
static PGBUF_HOLDER *
pgbuf_find_current_tran_holder (THREAD_ENTRY * thread_p, PGBUF_BCB * bufptr)
{
  int tran_index;
  PGBUF_HOLDER *holder;

  /* the caller is holding bufptr->BCB_mutex */
  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  holder = bufptr->holder_list;

  while (holder != NULL)
    {
      if (tran_index == holder->tran_index)
	{
	  return holder;
	}
      holder = holder->next_holder;
    }

  return NULL;
}

/*
 * pgbuf_remove_tran_holder () - Remove holder entry from given BCB
 *   return: NO_ERROR, or ER_code
 *   bufptr(in): pointer to BCB
 *   holder(in): pointer to holder entry to be removed
 *
 * Note: This function removes the given holder entry from the holder list of
 *       given BCB, and then connect it to the free holder list of the
 *       corresponding transaction.
 */
static int
pgbuf_remove_tran_holder (PGBUF_BCB * bufptr, PGBUF_HOLDER * holder)
{
  PGBUF_HOLDER *prev;
  int found;
  int tran_index;
#if defined(SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */

  /* initialize transaction index */
  tran_index = holder->tran_index;

  /* the caller is holding bufptr->BCB_mutex */

  /* remove given BCB holder entry from the BCB holder list */
  if (bufptr->holder_list == (PGBUF_HOLDER *) holder)
    {
      /* first entry */
      bufptr->holder_list = holder->next_holder;
    }
  else
    {
      found = false;
      prev = bufptr->holder_list;

      while (prev->next_holder != NULL)
	{
	  if (prev->next_holder == (PGBUF_HOLDER *) holder)
	    {
	      prev->next_holder = holder->next_holder;
	      holder->next_holder = NULL;
	      found = true;
	      break;
	    }
	  prev = prev->next_holder;
	}

      if (found == false)
	{
	  return ER_FAILED;
	}
    }

  /* initialize the fix_count */
  /* holder->fix_count is always set to some meaningful value
     when the holder entry is allocated for use.
     So, at this time, we do not need to initialize it.
   */
  /* When worker threads finish servicing client requests,
     they send the result to the corresponding clients and then,
     they perform cleaning jobs such as unfix buffer pages.
     Therefore, durning the time interval
     another worker thread can be waken up to serve the same client
     and perform some tasks of the same transaction.
     So, list_mutex must be held.
   */
  /* connect the BCB holder entry into free BCB holder list of
     given transaction.
   */
  rv =
    pthread_mutex_lock (&pgbuf_Pool.tran_holder_info[tran_index].list_mutex);
  holder->next_holder =
    pgbuf_Pool.tran_holder_info[tran_index].tran_free_list;
  pgbuf_Pool.tran_holder_info[tran_index].tran_free_list = holder;
  pgbuf_Pool.tran_holder_info[tran_index].num_free_cnt += 1;

  /* remove the BCB holder entry from transaction's holder list */
  if (pgbuf_Pool.tran_holder_info[tran_index].tran_hold_list == NULL)
    {
      /* This situation must not be occurred. */
      pthread_mutex_unlock (&pgbuf_Pool.tran_holder_info[tran_index].
			    list_mutex);
      return ER_FAILED;
    }

  if (pgbuf_Pool.tran_holder_info[tran_index].tran_hold_list ==
      (PGBUF_HOLDER *) holder)
    {
      pgbuf_Pool.tran_holder_info[tran_index].tran_hold_list =
	holder->tran_link;
    }
  else
    {
      found = false;
      prev = pgbuf_Pool.tran_holder_info[tran_index].tran_hold_list;

      while (prev->tran_link != NULL)
	{
	  if (prev->tran_link == (PGBUF_HOLDER *) holder)
	    {
	      prev->tran_link = holder->tran_link;
	      holder->tran_link = NULL;
	      found = true;
	      break;
	    }
	  prev = prev->tran_link;
	}

      if (found == false)
	{
	  pthread_mutex_unlock (&pgbuf_Pool.tran_holder_info[tran_index].
				list_mutex);
	  return ER_FAILED;
	}
    }

  pgbuf_Pool.tran_holder_info[tran_index].num_hold_cnt -= 1;
  pthread_mutex_unlock (&pgbuf_Pool.tran_holder_info[tran_index].list_mutex);

  return NO_ERROR;
}

#if defined(SERVER_MODE)
/*
 * pgbuf_get_waiting_tran_users_as_string () -
 *   return:
 *   holder_list(in):
 */
static char *
pgbuf_get_waiting_tran_users_as_string (PGBUF_HOLDER * holder_list)
{
  char *client_prog_name;	/* Client program name for transaction */
  char *client_user_name;	/* Client user name for transaction */
  char *client_host_name;	/* Client host for transaction */
  int client_pid;		/* Client process identifier for transaction */
  char *trans_users = NULL;	/* A string of a set of user@host|pid. */
  char *ptr;
  const char *fmt;
  int holder_cnt = 0, buf_size = 0;
  PGBUF_HOLDER *cur_holder = holder_list;

  while (cur_holder != NULL)
    {
      holder_cnt++;
      cur_holder = cur_holder->next_holder;
    }

  if (holder_cnt > 0)
    {
      buf_size = holder_cnt * (LOG_USERNAME_MAX + MAXHOSTNAMELEN + 20 + 4);
      trans_users = (char *) malloc (buf_size);
      if (trans_users == NULL)
	{
	  return NULL;
	}

      for (cur_holder = holder_list, ptr = trans_users, fmt = "%s@%s|%d";
	   cur_holder != NULL;
	   cur_holder = cur_holder->next_holder, fmt = ", %s@%s|%d")
	{
	  (void) logtb_find_client_name_host_pid (cur_holder->tran_index,
						  &client_prog_name,
						  &client_user_name,
						  &client_host_name,
						  &client_pid);
	  sprintf (ptr, fmt, client_user_name, client_host_name, client_pid);
	  ptr += strlen (ptr);
	}
    }

  return trans_users;
}
#endif /* SERVER_MODE */

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
 *           (2) in case of unconditionl request, add transaction on the
 *               BCB queue and release mutex and block the transaction.
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
  PGBUF_HOLDER *holder;
  int request_fcnt = 1;

  /* the caller is holding bufptr->BCB_mutex */
  if (buf_lock_acquired || bufptr->latch_mode == PGBUF_NO_LATCH)
    {
      bufptr->latch_mode = request_mode;
      bufptr->fcnt = 1;

      /* allocate a BCB holder entry */
      holder = pgbuf_allocate_tran_holder_entry (thread_p, -1);
      if (holder == NULL)
	{
	  pthread_mutex_unlock (&bufptr->BCB_mutex);
	  return ER_FAILED;
	}
      /* connect it into the holder list of BCB and initialize it */
      holder->next_holder = bufptr->holder_list;
      bufptr->holder_list = holder;
      holder->fix_count = 1;
      holder->bufptr = bufptr;
#if !defined(NDEBUG)
      sprintf (holder->fixed_at, "%s:%d ", caller_file, caller_line);
      holder->fixed_at_size = strlen (holder->fixed_at);
#endif /* NDEBUG */

      pthread_mutex_unlock (&bufptr->BCB_mutex);
      return NO_ERROR;
    }

  if ((request_mode == PGBUF_LATCH_READ)
      && (bufptr->latch_mode == PGBUF_LATCH_READ
	  || bufptr->latch_mode == PGBUF_LATCH_FLUSH
	  || bufptr->latch_mode == PGBUF_LATCH_VICTIM))
    {
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

	  holder = pgbuf_find_current_tran_holder (thread_p, bufptr);
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
	      holder = pgbuf_allocate_tran_holder_entry (thread_p, -1);
	      if (holder == NULL)
		{
		  pthread_mutex_unlock (&bufptr->BCB_mutex);
		  return ER_FAILED;
		}

	      /* connect it into the holder list of BCB and initialize it */
	      holder->next_holder = bufptr->holder_list;
	      bufptr->holder_list = holder;
	      holder->fix_count = 1;
	      holder->bufptr = bufptr;
#if !defined(NDEBUG)
	      sprintf (holder->fixed_at, "%s:%d ", caller_file, caller_line);
	      holder->fixed_at_size = strlen (holder->fixed_at);
#endif /* NDEBUG */
	    }
#endif /* SERVER_MODE */

	  pthread_mutex_unlock (&bufptr->BCB_mutex);
	  return NO_ERROR;
	}
      else
	{
	  holder = pgbuf_find_current_tran_holder (thread_p, bufptr);
	  if (holder == NULL)
	    {
	      /* in case that the caller is not the holder */
	      goto do_block;
	    }

	  /* in case that the caller is the holder */
	  bufptr->fcnt += 1;
	  holder->fix_count += 1;
#if !defined(NDEBUG)
	  pgbuf_add_fixed_at (holder, caller_file, caller_line);
#endif /* NDEBUG */
	  pthread_mutex_unlock (&bufptr->BCB_mutex);
	  return NO_ERROR;
	}
    }

  holder = pgbuf_find_current_tran_holder (thread_p, bufptr);
  if (holder == NULL)
    {
      /* in case that the caller is not the holder */
      goto do_block;
    }

  /* in case that the caller is holder */
  if (bufptr->latch_mode == PGBUF_LATCH_WRITE)
    {				/* only the holder */
      bufptr->fcnt += 1;
      holder->fix_count += 1;
#if !defined(NDEBUG)
      pgbuf_add_fixed_at (holder, caller_file, caller_line);
#endif /* NDEBUG */
      pthread_mutex_unlock (&bufptr->BCB_mutex);
      return NO_ERROR;
    }
  if (bufptr->latch_mode == PGBUF_LATCH_READ)
    {
      if (bufptr->fcnt == holder->fix_count)
	{
	  bufptr->fcnt += 1;
	  holder->fix_count += 1;
	  bufptr->latch_mode = request_mode;	/* PGBUF_LATCH_WRITE */
#if !defined(NDEBUG)
	  pgbuf_add_fixed_at (holder, caller_file, caller_line);
#endif /* NDEBUG */
	  pthread_mutex_unlock (&bufptr->BCB_mutex);
	  return NO_ERROR;
	}
      else
	{
	  if (condition == PGBUF_CONDITIONAL_LATCH)
	    {
	      goto do_block;	/* will return immediately */
	    }

	  request_fcnt = holder->fix_count + 1;
	  bufptr->fcnt -= holder->fix_count;
	  if (pgbuf_remove_tran_holder (bufptr, holder) != NO_ERROR)
	    {
	      pthread_mutex_unlock (&bufptr->BCB_mutex);
	      return ER_FAILED;
	    }
	  goto do_block;
	}
    }

do_block:

  if (condition == PGBUF_CONDITIONAL_LATCH)
    {
      /* reject the request */
      int tran_index;
      int waitsec;
      tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
      waitsec = logtb_find_wait_secs (tran_index);

      if (waitsec == LK_ZERO_WAIT)
	{
	  char *waitfor_client_users;	/* Waitfor users */
	  char *client_prog_name;	/* Client program name for transaction */
	  char *client_user_name;	/* Client user name for transaction */
	  char *client_host_name;	/* Client host for transaction */
	  int client_pid;	/* Client process identifier for transaction */

	  /* setup timeout error, if waitsec == LK_ZERO_WAIT */
#if defined(SERVER_MODE)
	  waitfor_client_users =
	    pgbuf_get_waiting_tran_users_as_string (bufptr->holder_list);
#else /* SERVER_MODE */
	  waitfor_client_users = NULL;
#endif /* SERVER_MODE */
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
		  bufptr->vpid.volid, bufptr->vpid.pageid,
		  waitfor_client_users);

	  if (waitfor_client_users)
	    {
	      free_and_init (waitfor_client_users);
	    }
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
 *            transactions on the BCB waiting queue.
 *       If Flush_Request == FALSE
 *            if LatchMode == NO_LATCH,
 *            then, wake up the transactions on the BCB waiting queue.
 *       Before return, it releases BCB mutex.
 */
#if !defined(NDEBUG)
static int
pgbuf_unlatch_bcb_upon_unfix (THREAD_ENTRY * thread_p, PGBUF_BCB * bufptr,
			      const char *caller_file, int caller_line)
#else /* NDEBUG */
static int
pgbuf_unlatch_bcb_upon_unfix (THREAD_ENTRY * thread_p, PGBUF_BCB * bufptr)
#endif				/* NDEBUG */
{
  PGBUF_HOLDER *holder;

  /* the caller is holding bufptr->BCB_mutex */

  /* decrement the fix count */
  bufptr->fcnt--;
  if (bufptr->fcnt < 0)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_PB_UNFIXED_PAGEPTR, 3,
	      bufptr->iopage_buffer->iopage.page, bufptr->vpid.pageid,
	      fileio_get_volume_label (bufptr->vpid.volid));
      bufptr->fcnt = 0;
    }

  holder = pgbuf_find_current_tran_holder (thread_p, bufptr);
  if (holder == NULL)
    {
      /* the caller must be the holder */
      pthread_mutex_unlock (&bufptr->BCB_mutex);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_PB_UNFIXED_PAGEPTR, 3,
	      bufptr->iopage_buffer->iopage.page, bufptr->vpid.pageid,
	      fileio_get_volume_label (bufptr->vpid.volid));
      return ER_FAILED;
    }

  holder->fix_count--;
  if (holder->fix_count == 0)
    {
      /* remove its own BCB holder entry */
      if (pgbuf_remove_tran_holder (bufptr, holder) != NO_ERROR)
	{
	  pthread_mutex_unlock (&bufptr->BCB_mutex);
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_PB_UNFIXED_PAGEPTR, 3,
		  bufptr->iopage_buffer->iopage.page, bufptr->vpid.pageid,
		  fileio_get_volume_label (bufptr->vpid.volid));
	  return ER_FAILED;
	}
    }

  if (bufptr->fcnt == 0)
    {
      if (bufptr->latch_mode != PGBUF_LATCH_FLUSH
	  && bufptr->latch_mode != PGBUF_LATCH_VICTIM)
	{
	  bufptr->latch_mode = PGBUF_NO_LATCH;
	}

      /* there could be some synchronous flushers on the BCB queue */
      /* When the page buffer in LRU_1_Zone,
       * do not move the page buffer into the top of LRU.
       * This is an intention for performance.
       */
      if (bufptr->avoid_victim == false
	  && pgbuf_is_exist_blocked_reader_writer (bufptr) == false
	  && bufptr->zone != PGBUF_LRU_1_ZONE)
	{
	  (void) pgbuf_relocate_top_lru (bufptr);
	}
    }

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
	  return ER_FAILED;
	}

      /* If above function returns failure,
       * the caller does not hold bufptr->BCB_mutex.
       * Otherwise, the caller is still holding bufptr->BCB_mutex.
       */
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
 * pgbuf_block_bcb () - Adds it on the BCB waiting queue and block transaction
 *   return: NO_ERROR, or ER_code
 *   bufptr(in):
 *   request_mode(in):
 *   request_fcnt(in):
 *
 * Note: If latch_mode == BT_LT_FLUSH, then put the transaction to the top of
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
  int r;

  /* caller is holding bufptr->BCB_mutex */

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

	  MUTEX_LOCK_VIA_BUSY_WAIT (r, bufptr->BCB_mutex);
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
  int r;

  MUTEX_LOCK_VIA_BUSY_WAIT (r, bufptr->BCB_mutex);

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
	      assert (curr_thrd_entry->tran_index != NULL_TRAN_INDEX);
	      holder = pgbuf_allocate_tran_holder_entry (thread_p,
							 curr_thrd_entry->
							 tran_index);
	      if (holder == NULL)
		{
		  pthread_mutex_unlock (&bufptr->BCB_mutex);
		  thread_unlock_entry (curr_thrd_entry);
		  return ER_FAILED;
		}
	      bufptr->fcnt += curr_thrd_entry->request_fix_count;

	      holder->next_holder = bufptr->holder_list;
	      bufptr->holder_list = holder;
	      holder->fix_count = curr_thrd_entry->request_fix_count;
	      holder->bufptr = bufptr;
#if !defined(NDEBUG)
	      sprintf (holder->fixed_at, "%s:%d ", caller_file, caller_line);
	      holder->fixed_at_size = strlen (holder->fixed_at);
#endif /* NDEBUG */

	      bufptr->next_wait_thrd = curr_thrd_entry->next_wait_thrd;
	      curr_thrd_entry->next_wait_thrd = NULL;
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
  int waitsecs;
  int old_waitsecs;
  int save_request_latch_mode;
  char *waitfor_client_users;	/* Waitfor users */
  char *client_prog_name;	/* Client program name for transaction */
  char *client_user_name;	/* Client user name for transaction */
  char *client_host_name;	/* Client host for transaction */
  int client_pid;		/* Client process identifier for transaction */

  /* After holding the mutex associated with conditional variable,
     relese the bufptr->BCB_mutex. */
  thread_lock_entry (thrd_entry);
  pthread_mutex_unlock (&bufptr->BCB_mutex);

  old_waitsecs = waitsecs = logtb_find_current_wait_secs (thread_p);

  assert (waitsecs == LK_INFINITE_WAIT || waitsecs == LK_ZERO_WAIT
	  || waitsecs == LK_FORCE_ZERO_WAIT || waitsecs > 0);

  if (waitsecs != LK_ZERO_WAIT && waitsecs != LK_FORCE_ZERO_WAIT)
    {
      waitsecs = PGBUF_TIMEOUT;
    }

try_again:
  to.tv_sec = time (NULL) + waitsecs;
  to.tv_nsec = 0;

  thrd_entry->resume_status = THREAD_PGBUF_SUSPENDED;
  r =
    pthread_cond_timedwait (&thrd_entry->wakeup_cond,
			    &thrd_entry->th_entry_lock, &to);

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
  if (old_waitsecs == LK_INFINITE_WAIT)
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
  else if (old_waitsecs > 0)
    {
      waitfor_client_users =
	pgbuf_get_waiting_tran_users_as_string (bufptr->holder_list);

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
	      bufptr->vpid.pageid, waitfor_client_users);

      if (waitfor_client_users)
	{
	  free_and_init (waitfor_client_users);
	}
    }
  else
    {
      pthread_mutex_unlock (&bufptr->BCB_mutex);
    }

  return ER_FAILED;
}

/*
 * pgbuf_wakeup_bcb () - Wakes up blocked transactions on the BCB queue
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
  PGBUF_HOLDER *holder;

  /* the caller is holding bufptr->BCB_mutex */

  /* fcnt == 0, bufptr->latch_mode == PGBUF_NO_LATCH/PGBUF_LATCH_FLUSH_INVALID */
  /* there cannot be any blocked flusher */

  thrd_entry = bufptr->next_wait_thrd;
  if (thrd_entry->request_latch_mode == PGBUF_LATCH_VICTIM)
    {
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

		  /* allocate a BCB holder entry, connect it into the holder
		     list, and initialize it. */
		  assert (thrd_entry->tran_index != NULL_TRAN_INDEX);
		  holder =
		    pgbuf_allocate_tran_holder_entry (thread_p,
						      thrd_entry->tran_index);
		  if (holder == NULL)
		    {
		      pthread_mutex_unlock (&bufptr->BCB_mutex);
		      thread_unlock_entry (thrd_entry);
		      return ER_FAILED;
		    }

		  holder->next_holder = bufptr->holder_list;
		  bufptr->holder_list = holder;
		  holder->fix_count = thrd_entry->request_fix_count;
		  holder->bufptr = bufptr;
#if !defined(NDEBUG)
		  sprintf (holder->fixed_at, "%s:%d ", caller_file,
			   caller_line);
		  holder->fixed_at_size = strlen (holder->fixed_at);
#endif /* NDEBUG */

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
#if defined(SERVER_MODE)
  int rv;
  int loop_cnt;
#endif

/* one_phase: no hash-chain mutex */
one_phase:

  bufptr = hash_anchor->hash_next;
  while (bufptr != NULL)
    {
      if (VPID_EQ (&(bufptr->vpid), vpid))
	{
	  bufptr->avoid_victim = true;
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

	      if (loop_cnt++ < PRM_MUTEX_BUSY_WAITING_CNT)
		{
		  goto mutex_lock;
		}

	      /* An unconditional request is given for acquiring BCB_mutex */
	      rv = pthread_mutex_lock (&bufptr->BCB_mutex);
	    }
#else /* SERVER_MODE */
	  rv = pthread_mutex_lock (&bufptr->BCB_mutex);
#endif /* SERVER_MODE */

	  bufptr->avoid_victim = false;
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
	  bufptr->avoid_victim = true;
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

	      if (loop_cnt++ < PRM_MUTEX_BUSY_WAITING_CNT)
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

	  bufptr->avoid_victim = false;
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
 *       Before return, the transaction releases hash_anchor->hash_mutex.
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
     At first design, it had one entry for each transaction.
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
 *       Before return, the transaction releases the hash mutex on the hash
 *       anchor and wakes up all the transactions blocked on the queue of the
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
 *       is no free buffer, wait 100 msec and retry to allocate a BCB again.
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
  int i, sleep_count;

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
	  bufptr = pgbuf_get_victim_from_lru_list (&vpid);
	  if (bufptr != NULL)
	    {
	      /* the caller is holding bufptr->BCB_mutex. */

#if !defined(NDEBUG)
	      if (pgbuf_victimize_bcb (thread_p, bufptr,
				       caller_file, caller_line) != NO_ERROR)
#else /* NDEBUG */
	      if (pgbuf_victimize_bcb (thread_p, bufptr) != NO_ERROR)
#endif /* NDEBUG */
		{
		  continue;
		}

	      /* Above function holds bufptr->BCB_mutex at first operation.
	       * If the above function returns failure,
	       * the caller does not hold bufptr->BCB_mutex.
	       * Otherwise, the caller is still holding bufptr->BCB_mutex.
	       */
	      return bufptr;
	    }

#if 0
#if defined(SERVER_MODE)
	  thread_sleep (0, 1000);	/* 10 msec */
#endif /* SERVER_MODE */
#endif
	}

      /* At here, sleep_count >= PGBUF_SLEEP_MAX */
      er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE,
	      ER_PB_ALL_BUFFERS_FIXED, 1, pgbuf_get_lru_index (&vpid));
    }

  /* At here, i >= pgbuf_Pool.num_LRU_list */
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
  int rv;
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
      || bufptr->avoid_victim == true
      || bufptr->latch_mode == PGBUF_LATCH_VICTIM
      || bufptr->latch_mode == PGBUF_LATCH_VICTIM_INVALID
      || pgbuf_is_exist_blocked_reader_writer_victim (bufptr) == true)
    {
      pthread_mutex_unlock (&bufptr->BCB_mutex);
      return ER_FAILED;
    }

  /* flush pass */
  if (bufptr->latch_mode == PGBUF_NO_LATCH)
    {
      /* grant the request */
      bufptr->latch_mode = PGBUF_LATCH_VICTIM;
      if (bufptr->dirty == false)
	{
	  goto hash_chain_deletion_pass;
	}

      mnt_pb_replacements (thread_p);
      if (pgbuf_flush_page_with_wal (thread_p, bufptr) != NO_ERROR)
	{
	  return ER_FAILED;
	}
      /* If above function returns failure,
       * the caller does not hold bufptr->BCB_mutex.
       * Otherwise, the caller is still holding bufptr->BCB_mutex.
       */
    }
  else
    {
      /* block the request */
#if !defined(NDEBUG)
      if (pgbuf_block_bcb (thread_p, bufptr, PGBUF_LATCH_VICTIM, 0,
			   caller_file, caller_line) != NO_ERROR)
#else /* NDEBUG */
      if (pgbuf_block_bcb (thread_p, bufptr, PGBUF_LATCH_VICTIM, 0) !=
	  NO_ERROR)
#endif /* NDEBUG */
	{
	  return ER_FAILED;
	}

      /* Above function released bufptr->BCB_mutex */
#if defined(SERVER_MODE)
      if (cur_thrd_entry->victim_request_fail == true)
	{
	  /* according to the early wakeup of blocked victim selector
	     by any reader or writer upon fixing the page. */
	  cur_thrd_entry->victim_request_fail = false;
	  return ER_FAILED;
	}
#endif /* SERVER_MODE */

      /* hold bufptr->BCB_mutex again. */
      MUTEX_LOCK_VIA_BUSY_WAIT (rv, bufptr->BCB_mutex);
    }

#if defined(SERVER_MODE)
  if (bufptr->latch_mode == PGBUF_LATCH_VICTIM)
    {
      /* after-flush or after-block, victim condition check pass */
      if (bufptr->zone != PGBUF_VOID_ZONE
	  || bufptr->fcnt != 0
	  || bufptr->avoid_victim == true || bufptr->next_wait_thrd != NULL)
	{
	  /* bufptr->next_wait_thrd != NULL : there was/is some reader/writer */
	  if (bufptr->fcnt == 0)
	    {
	      bufptr->latch_mode = PGBUF_NO_LATCH;
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
	    }
	  else
	    {
	      bufptr->latch_mode = PGBUF_LATCH_READ;
	      pthread_mutex_unlock (&bufptr->BCB_mutex);
	    }
	  return ER_FAILED;
	}
    }
  else
    {
      if (bufptr->next_wait_thrd != NULL)
	{
	  bufptr->latch_mode = PGBUF_NO_LATCH;
#if !defined(NDEBUG)
	  (void) pgbuf_wakeup_bcb (thread_p, bufptr, caller_file,
				   caller_line);
#else /* NDEBUG */
	  (void) pgbuf_wakeup_bcb (thread_p, bufptr);
#endif /* NDEBUG */
	  /* Above function released bufptr->BCB_mutex unconditionally. */
	  return ER_FAILED;
	}
    }
#endif /* SERVER_MODE */

hash_chain_deletion_pass:
  /* the caller is holding bufptr->BCB_mutex */
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

  if (bufptr->latch_mode == PGBUF_LATCH_FLUSH_INVALID
      || bufptr->latch_mode == PGBUF_LATCH_VICTIM_INVALID
      || bufptr->latch_mode == PGBUF_LATCH_INVALID)
    {
      pthread_mutex_unlock (&bufptr->BCB_mutex);
      return NO_ERROR;
    }

  bufptr->dirty = false;
  LSA_SET_NULL (&bufptr->oldest_unflush_lsa);
  if (bufptr->zone != PGBUF_VOID_ZONE)
    {
      pgbuf_invalidate_bcb_from_lru (bufptr);
      /* bufptr->BCB_mutex is still held by the caller. */
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
	  return ER_FAILED;
	}

      /* If above function returns failure,
       * the caller does not hold bufptr->BCB_mutex.
       * Otherwise, the caller is still holding bufptr->BCB_mutex.
       */

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
	  holder = pgbuf_find_current_tran_holder (thread_p, bufptr);
	  if (holder != NULL)
	    {
	      if (pgbuf_flush_page_with_wal (thread_p, bufptr) != NO_ERROR)
		{
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
 *       and returns it. Before disconnection, the transaction must hold the
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

  lru_idx = vpid->pageid % pgbuf_Pool.num_LRU_list;

  return lru_idx;
}

/*
 * pgbuf_get_victim_from_lru_list () - Get victim BCB from the bottom of
 *				       LRU list
 *   return: If success, BCB, otherwise NULL
 *   vpid(in):
 *
 * Note: This fuction disconnects BCB from the bottom of the LRU list and
 *       returns it if its fcnt == 0. If its fcnt != 0, makes bufptr->PrevBCB
 *       LRU_bottom and retry. While this processing, the caller must be the
 *       holder of the LRU list.
 */
static PGBUF_BCB *
pgbuf_get_victim_from_lru_list (const VPID * vpid)
{
  PGBUF_BCB *bufptr, *tmp_buf_p;
  int lru_idx;
  int check_count;
#if defined(SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */

  lru_idx = pgbuf_get_lru_index (vpid);

  /* check if LRU list is empty */
  if (pgbuf_Pool.buf_LRU_list[lru_idx].LRU_bottom == NULL)
    {
      return NULL;
    }

  MUTEX_LOCK_VIA_BUSY_WAIT (rv, pgbuf_Pool.buf_LRU_list[lru_idx].LRU_mutex);

  while ((bufptr = pgbuf_Pool.buf_LRU_list[lru_idx].LRU_bottom) != NULL)
    {
      tmp_buf_p = bufptr;
      check_count =
	MAX (1, (int) (PGBUF_LRU_SIZE * PGBUF_VICTIM_FLUSH_MAX_RATIO));

      /* search for non dirty PGBUF */
      while (tmp_buf_p != NULL && check_count > 0)
	{
	  check_count--;
	  if (tmp_buf_p->dirty == false)
	    {
	      bufptr = tmp_buf_p;
	      break;
	    }
	  tmp_buf_p = tmp_buf_p->prev_BCB;
	}

      /* disconnect bufptr from the LRU list */
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

#if defined(SERVER_MODE)
      if (pgbuf_Pool.buf_LRU_list[lru_idx].LRU_bottom != NULL
	  && pgbuf_Pool.buf_LRU_list[lru_idx].LRU_bottom->dirty == true)
	{
	  thread_wakeup_page_flush_thread ();
	}
#endif /* SERVER_MODE */

      if (bufptr->fcnt == 0)
	{
	  break;
	}
    }

  pthread_mutex_unlock (&pgbuf_Pool.buf_LRU_list[lru_idx].LRU_mutex);

  if (bufptr != NULL)
    {
      MUTEX_LOCK_VIA_BUSY_WAIT (rv, bufptr->BCB_mutex);
    }

  return (bufptr);
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
 * pgbuf_relocate_top_lru () - Relocate given BCB into the top of LRU list
 *   return: NO_ERROR
 *   bufptr(in): pointer to buffer page
 *
 * Note: This function puts BCB to the top of the LRU list.
 *       While this processing, the caller must be the holder of the LRU list.
 */
static int
pgbuf_relocate_top_lru (PGBUF_BCB * bufptr)
{
  int lru_idx;
#if defined(SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */

  assert (bufptr->zone != PGBUF_LRU_1_ZONE);

  lru_idx = pgbuf_get_lru_index (&bufptr->vpid);

  /* the caller is holding bufptr->BCB_mutex */
  MUTEX_LOCK_VIA_BUSY_WAIT (rv, pgbuf_Pool.buf_LRU_list[lru_idx].LRU_mutex);

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

  bufptr->zone = PGBUF_LRU_1_ZONE;	/* OK.. */
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
	  pgbuf_Pool.buf_LRU_list[lru_idx].LRU_middle = temp_bufptr->prev_BCB;
	  pgbuf_Pool.buf_LRU_list[lru_idx].LRU_1_zone_cnt -= 1;
	  temp_bufptr = pgbuf_Pool.buf_LRU_list[lru_idx].LRU_middle;
	}
    }

  pthread_mutex_unlock (&pgbuf_Pool.buf_LRU_list[lru_idx].LRU_mutex);

  return NO_ERROR;
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

  /* the caller is holding bufptr->BCB_mutex */

  /* bufptr->latch_mode == PGBUF_LATCH_FLUSH/PGBUF_LATCH_VICTIM */
  bufptr->async_flush_request = false;
  pthread_mutex_unlock (&bufptr->BCB_mutex);

  /* confirm WAL protocol */
  /* force log record to disk */
  logpb_flush_log_for_wal (thread_p, &bufptr->iopage_buffer->iopage.prv.lsa);

  /* Record number of writes in statistics */
  mnt_pb_iowrites (thread_p);

  /* now, flush buffer page */
  if (fileio_write (thread_p,
		    fileio_get_volume_descriptor (bufptr->vpid.volid),
		    &bufptr->iopage_buffer->iopage,
		    bufptr->vpid.pageid, IO_PAGESIZE) == NULL)
    {
      return ER_FAILED;
    }

  /* bufptr->latch_mode == PGBUF_LATCH_FLUSH, PGBUF_LATCH_VICTIM,
   *                       PGBUF_LATCH_FLUSH_INVALID,
   *                       PGBUF_LATCH_VICTIM_INVALID
   */
  MUTEX_LOCK_VIA_BUSY_WAIT (rv, bufptr->BCB_mutex);
  bufptr->dirty = false;

  LSA_SET_NULL (&bufptr->oldest_unflush_lsa);

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

  /* check whether there exists any blocked reader/writer */
  thrd_entry = bufptr->next_wait_thrd;
  while (thrd_entry != NULL)
    {
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

  /* check whether there exists any blocked victim request */
  prev_thrd_entry = NULL;
  thrd_entry = bufptr->next_wait_thrd;
  while (thrd_entry != NULL)
    {
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
static DISK_ISVALID
pgbuf_is_valid_page (const VPID * vpid,
		     DISK_ISVALID (*fun) (const VPID * vpid, void *args),
		     void *args)
{
  DISK_ISVALID valid;

  if (pgbuf_Expensive_debug_fetch == true)
    {
      if (fileio_get_volume_label (vpid->volid) == NULL || VPID_ISNULL (vpid))
	{
	  return DISK_INVALID;
	}

      valid = disk_isvalid_page (vpid->volid, vpid->pageid);
      if (valid != DISK_VALID
	  || (fun != NULL && (valid = (*fun) (vpid, args)) != DISK_VALID))
	{
	  if (valid != DISK_ERROR)
	    {
	      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_PB_BAD_PAGEID, 2, vpid->pageid,
		      fileio_get_volume_label (vpid->volid));
	    }
	}

      return valid;
    }

  /* Do a simple check in non debugging mode */
  if (vpid->pageid < 0)
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_PB_BAD_PAGEID, 2,
	      vpid->pageid, fileio_get_volume_label (vpid->volid));
      return DISK_INVALID;
    }

  return DISK_VALID;
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
  int rv;

  /* NOTE: Does not need to hold BCB_mutex since the page is fixed */
  for (bufid = 0; bufid < pgbuf_Pool.num_buffers; bufid++)
    {
      bufptr = PGBUF_FIND_BCB_PTR (bufid);
      MUTEX_LOCK_VIA_BUSY_WAIT (rv, bufptr->BCB_mutex);

      if (((PAGE_PTR) (&(bufptr->iopage_buffer->iopage.page[0]))) == pgptr)
	{
	  if (bufptr->fcnt <= 0)
	    {
	      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_PB_UNFIXED_PAGEPTR, 3, pgptr,
		      bufptr->vpid.pageid,
		      fileio_get_volume_label (bufptr->vpid.volid));
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
  return false;
}

/*
 * pgbuf_dump_if_any_fixed () - Dump buffer pool if any page buffer is fixed
 *   return: void
 *
 * Note: This is a debugging function that can be used to verify if buffers
 *       were freed after a set of operations (e.g., a transaction or a API
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
		   " %6d|%4d %10s %p %p-%p\n",
		   bufptr->ipool, bufptr->vpid.volid, bufptr->vpid.pageid,
		   bufptr->fcnt, latch_mode_str, bufptr->dirty,
		   bufptr->avoid_victim, bufptr->async_flush_request,
		   zone_str, bufptr->iopage_buffer->iopage.prv.lsa.pageid,
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
      malloc_io_pgptr = (FILEIO_PAGE *) fileio_allocate_page (ARG_FILE_LINE);
      if (malloc_io_pgptr == NULL)
	{
	  return consistent;
	}

      /* Read the disk page into local page area */
      if (fileio_read (fileio_get_volume_descriptor (bufptr->vpid.volid),
		       malloc_io_pgptr, bufptr->vpid.pageid) == NULL)
	{
	  /* Unable to verify consistency of this page */
	  consistent = PGBUF_CONTENT_BAD;
	}
      else
	{
	  /* If page is dirty, it should be different from the one on disk */
	  if (!LSA_EQ
	      (&malloc_io_pgptr->prv.lsa,
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
      fileio_free_page (ARG_FILE_LINE, malloc_io_pgptr);
    }
  else
    {
      if (pgbuf_Expensive_debug_free == true && bufptr->fcnt <= 0)
	{
	  int i;
	  /* The page should be scrambled, otherwise some one step on it */
	  for (i = 0; i < DB_PAGESIZE; i++)
	    {
	      if (bufptr->iopage_buffer->iopage.page[i] != DB_MEM_SCRAMBLE)
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
