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
 * vacuum.c - Vacuuming system implementation.
 *
 */
#include "vacuum.h"
#include "thread.h"
#include "mvcc.h"
#include "page_buffer.h"
#include "heap_file.h"
#include "boot_sr.h"
#include "system_parameter.h"
#include "btree.h"
#include "log_compress.h"
#include "overflow_file.h"
#include "lock_free.h"
#include "perf_monitor.h"
#include "dbtype.h"
#include "transaction_cl.h"

/* The maximum number of slots in a page if all of them are empty.
 * IO_MAX_PAGE_SIZE is used for page size and any headers are ignored (it
 * wouldn't bring a significant difference).
 */
#define MAX_SLOTS_IN_PAGE (IO_MAX_PAGE_SIZE / sizeof (SPAGE_SLOT))

/* The maximum size of OID buffer where the records vacuumed from heap file
 * are collected to be removed from blockid b-trees.
 */
#define REMOVE_OIDS_BUFFER_SIZE (16 * IO_DEFAULT_PAGE_SIZE / OR_OID_SIZE)

/* The default number of cached entries in a vacuum statistics cache */
#define VACUUM_STATS_CACHE_SIZE 100

/* Get first log page identifier in a log block */
#define VACUUM_FIRST_LOG_PAGEID_IN_BLOCK(blockid) \
  ((blockid) * vacuum_Data->log_block_npages)
/* Get last log page identifier in a log block */
#define VACUUM_LAST_LOG_PAGEID_IN_BLOCK(blockid) \
  (VACUUM_FIRST_LOG_PAGEID_IN_BLOCK (blockid + 1) - 1)

/* Maximum count of blocks that can be recovered in one iteration */
#define VACUUM_RECOVER_BLOCKS_MAX_COUNT \
  ((int) (IO_PAGESIZE / sizeof (VACUUM_DATA_ENTRY)))
/* Maximum possible size used to buffer recovered blocks from log */
#define VACUUM_RECOVER_BLOCKS_BUFFER_MAX_SIZE \
  ((int) (IO_MAX_PAGE_SIZE / sizeof (VACUUM_DATA_ENTRY)))

/*
 * Vacuum data section.
 * Vacuum data contains useful information for the vacuum process. There are
 * several fields, among which a table of entries which describe the progress
 * of processing log data for vacuum.
 * This information is kept on disk too and requires redo logging.
 */

/* Macro's for locking/unlocking vacuum data */
#define VACUUM_LOCK_DATA() \
  pthread_mutex_lock (&vacuum_Data_mutex)
#define VACUUM_UNLOCK_DATA() \
  pthread_mutex_unlock (&vacuum_Data_mutex)

/* Vacuum log block data.
 *
 * Stores information on a block of log data relevant for vacuum.c
 */
typedef struct vacuum_data_entry VACUUM_DATA_ENTRY;
struct vacuum_data_entry
{
  VACUUM_LOG_BLOCKID blockid;
  LOG_LSA start_lsa;
  MVCCID oldest_mvccid;
  MVCCID newest_mvccid;
};

/* One flag is required for entries currently being vacuumed. In order to
 * avoid using an extra-field and because blockid will not use all its 64 bits
 * first bit will be used for this flag.
 */
/* Bits used for flag */
#define VACUUM_DATA_ENTRY_FLAG_MASK	  0xE000000000000000
/* Bits used for blockid */
#define VACUUM_DATA_ENTRY_BLOCKID_MASK	  0x1FFFFFFFFFFFFFFF

/* Flags */
/* The represented block is being vacuumed */
#define VACUUM_BLOCK_STATUS_MASK		  0xC000000000000000
#define VACUUM_BLOCK_STATUS_VACUUMED		  0x8000000000000000
#define VACUUM_BLOCK_STATUS_IN_PROGRESS_VACUUM	  0x4000000000000000
#define VACUUM_BLOCK_STATUS_AVAILABLE		  0x0000000000000000

#define VACUUM_BLOCK_FLAG_INTERRUPTED		  0x2000000000000000

/* Vacuum data.
 *
 * Stores data required for vacuum. It is also stored on disk in the first
 * database volume.
 */
typedef struct vacuum_data VACUUM_DATA;
struct vacuum_data
{
  LOG_LSA crt_lsa;		/* Data is stored on disk and requires
				 * logging, therefore it also requires a log
				 * lsa.
				 */

  VACUUM_LOG_BLOCKID last_blockid;	/* Block id for last vacuum data
					 * entry... This entry is actually the
					 * id of last added block which may
					 * not even be in vacuum data
					 * (being already vacuumed).
					 */
  MVCCID oldest_mvccid;		/* Oldest MVCCID found in current vacuum
				 * data.
				 */
  MVCCID newest_mvccid;		/* Newest MVCCID found in current vacuum
				 * data.
				 */

  /* First and last blocks that couldn't fit into vacuum data and must be
   * recovered from log. The blocks are linked between them using LOG_LSA
   * location.
   */
  LOG_LSA first_blocks_to_recover;
  LOG_LSA last_blocks_to_recover;
  int n_blocks_to_recover;	/* Total number of blocks saved to log that
				 * need to be recovered.
				 */
  int n_first_blocks_to_recover;	/* Number of blocks to recover in
					 * first log entry of saved blocks.
					 * Used for early out when it doesn't
					 * fit in vacuum data.
					 */

  int log_block_npages;		/* Number of log pages in one block of log
				 * data.
				 */

  int n_table_entries;		/* Number of entries in log block table */

  /* NOTE: Leave the table at the end of vacuum data structure */
  VACUUM_DATA_ENTRY vacuum_data_table[1];	/* Array containing log block
						 * entries and skipped blocks
						 * entries. This is a dynamic
						 * array, new blocks will be
						 * appended at the end, while
						 * vacuumed blocks will be
						 * removed when finished.
						 */
};

typedef struct block_log_buffer BLOCK_LOG_BUFFER;
struct block_log_buffer
{
  int buffer_id;
  LOG_PAGEID start_page;	/* start page (sequence order) */
  LOG_PAGEID last_page;		/* last page (including) */
};

typedef struct vacuum_job_entry VACUUM_JOB_ENTRY;

struct vacuum_job_entry
{
  VACUUM_DATA_ENTRY vacuum_data_entry;	/* copy of data to avoid mutex usage */
#if defined(SERVER_MODE)
  BLOCK_LOG_BUFFER block_log_buffer;
#endif				/* SERVER_MODE */
};

/* Pointer to vacuum data */
VACUUM_DATA *vacuum_Data = NULL;

/* VFID of the vacuum data file */
VFID vacuum_Data_vfid;

/* VPID of the first vacuum data page */
VPID vacuum_Data_vpid;

/* The maximum allowed size for vacuum data */
int vacuum_Data_max_size;
/* Number of pages in vacuum data */
int vacuum_Data_npages;

/* Mutex used to synchronize vacuum data for auto-vacuum master and vacuum
 * workers.
 */
pthread_mutex_t vacuum_Data_mutex;

/* True when master processes log to recover blocks which didn't fit vacuum
 * data.
 */
bool vacuum_Master_is_process_log_phase = false;

/* Oldest MVCCID considered active by a running transaction.
 * Considered as threshold by vacuum workers.
 */
MVCCID vacuum_Global_oldest_active_mvccid;
int vacuum_Global_oldest_active_blockers_counter;

/* Size of vacuum data header (all data that precedes log block data table) */
#define VACUUM_DATA_HEADER_SIZE \
  ((int) offsetof (VACUUM_DATA, vacuum_data_table))
/* The maximum allowed size for the log block table */
#define VACUUM_DATA_TABLE_MAX_SIZE	\
  ((vacuum_Data_max_size - VACUUM_DATA_HEADER_SIZE) \
   / ((int) sizeof (VACUUM_DATA_ENTRY)))

/* Get vacuum data entry table at the given index */
#define VACUUM_DATA_GET_ENTRY(block_index) \
  (&vacuum_Data->vacuum_data_table[block_index])

/* Access fields in a vacuum data table entry */
/* Get blockid (use mask to cancel flag bits) */
#define VACUUM_DATA_ENTRY_BLOCKID(entry) \
  (((entry)->blockid) & VACUUM_DATA_ENTRY_BLOCKID_MASK)
/* Get start vacuum lsa */
#define VACUUM_DATA_ENTRY_START_LSA(entry) \
  ((entry)->start_lsa)
#define VACUUM_DATA_ENTRY_OLDEST_MVCCID(entry) \
  ((entry)->oldest_mvccid)
#define VACUUM_DATA_ENTRY_NEWEST_MVCCID(entry) \
  ((entry)->newest_mvccid)

/* Get data entry flags */
#define VACUUM_DATA_ENTRY_FLAG(entry) \
  (((entry)->blockid) & VACUUM_DATA_ENTRY_FLAG_MASK)

/* Vacuum block status: requested means that vacuum data has assigned it as
 * a job, but no worker started it yet; running means that a work is currently
 * vacuuming based on this entry's block.
 */
/* Get vacuum block status */
#define VACUUM_BLOCK_STATUS(entry) \
  (((entry)->blockid) & VACUUM_BLOCK_STATUS_MASK)

/* Check vacuum block status */
#define VACUUM_BLOCK_STATUS_IS_VACUUMED(entry) \
  (VACUUM_BLOCK_STATUS (entry) == VACUUM_BLOCK_STATUS_VACUUMED)
#define VACUUM_BLOCK_STATUS_IS_IN_PROGRESS(entry) \
  (VACUUM_BLOCK_STATUS (entry) == VACUUM_BLOCK_STATUS_IN_PROGRESS_VACUUM)
#define VACUUM_BLOCK_STATUS_IS_AVAILABLE(entry) \
  (VACUUM_BLOCK_STATUS (entry) == VACUUM_BLOCK_STATUS_AVAILABLE)

/* Set vacuum block status */
#define VACUUM_BLOCK_STATUS_SET_VACUUMED(entry) \
  ((entry)->blockid = \
  ((entry)->blockid & ~VACUUM_BLOCK_STATUS_MASK) \
  | VACUUM_BLOCK_STATUS_VACUUMED)
#define VACUUM_BLOCK_STATUS_SET_IN_PROGRESS(entry) \
  ((entry)->blockid = \
  ((entry)->blockid & ~VACUUM_BLOCK_STATUS_MASK) \
  | VACUUM_BLOCK_STATUS_IN_PROGRESS_VACUUM)
#define VACUUM_BLOCK_STATUS_SET_AVAILABLE(entry) \
  ((entry)->blockid = \
  ((entry)->blockid & ~VACUUM_BLOCK_STATUS_MASK) \
  | VACUUM_BLOCK_STATUS_AVAILABLE)

#define VACUUM_BLOCK_IS_INTERRUPTED(entry) \
  ((entry->blockid & VACUUM_BLOCK_FLAG_INTERRUPTED) != 0)
#define VACUUM_BLOCK_SET_INTERRUPTED(entry) \
  (entry->blockid |= VACUUM_BLOCK_FLAG_INTERRUPTED)
#define VACUUM_BLOCK_CLEAR_INTERRUPTED(entry) \
  (entry->blockid &= ~VACUUM_BLOCK_FLAG_INTERRUPTED)

#if defined (SERVER_MODE)
/* A block entry in vacuum data can be vacuumed if:
 * 1. block is marked as available (is not assigned to any thread and no
 *    worker is currently processing the block).
 * 2. the newest block MVCCID is older than the oldest active MVCCID (meaning
 *    that all changes logged in block are now "all visible").
 * 3. for safety: make sure that there is at least a page between start_lsa
 *    and the page currently used for logging. It is possible that the log
 *    entry at start_lsa is "spilled" on the next page. Fetching the currently
 *    logged page requires an exclusive log lock which is prohibited for
 *    vacuum workers.
 */
#define VACUUM_LOG_BLOCK_CAN_VACUUM(entry, mvccid) \
  (VACUUM_BLOCK_STATUS_IS_AVAILABLE (entry) \
   && MVCC_ID_PRECEDES (VACUUM_DATA_ENTRY_NEWEST_MVCCID (entry), mvccid) \
   && entry->start_lsa.pageid + 1 < log_Gl.append.prev_lsa.pageid)
#else	/* !SERVER_MODE */		   /* SA_MODE */
/* Stand-alone mode can always vacuum a block (no concurrency) */
#define VACUUM_LOG_BLOCK_CAN_VACUUM(entry, mvccid) \
  (true)
#endif

/*
 * Vacuum oldest not flushed lsa section.
 * Server must keep track of the oldest change on vacuum data that is not
 * yet flushed to disk. Used for log checkpoint to give vacuum data logging
 * a behavior similar to logging database page changes.
 */
LOG_LSA vacuum_Data_oldest_not_flushed_lsa;

/* A lock-free buffer used for communication between logger transactions and
 * auto-vacuum master. It is advisable to avoid synchronizing running
 * transactions with vacuum threads and for this reason the block data is not
 * added directly to vacuum data.
 */
LOCK_FREE_CIRCULAR_QUEUE *vacuum_Block_data_buffer = NULL;
#define VACUUM_BLOCK_DATA_BUFFER_CAPACITY 1024

/* A lock free queue of vacuum jobs. Master will add jobs based on vacuum data
 * and workers will execute the jobs one by one.
 */
LOCK_FREE_CIRCULAR_QUEUE *vacuum_Job_queue = NULL;

#if defined(SERVER_MODE)
/* Vacuum prefetch log block buffers */
LOG_PAGE *vacuum_Prefetch_log_buffer = NULL;
LF_BITMAP vacuum_Prefetch_free_buffers_bitmap;
int vacuum_Prefetch_log_pages = -1;
int vacuum_Prefetch_log_mode = VACUUM_PREFETCH_LOG_MODE_MASTER;

/* Count of pretetch log buffer blocks : number of blocks in job queue
 * + number of blocks being processed by vacuum workers */
#define VACUUM_PREFETCH_LOG_PAGES (vacuum_Prefetch_log_pages)

/* Number of prefetch log buffer blocks */
#define VACUUM_PREFETCH_LOG_BLOCK_BUFFERS_COUNT \
  (VACUUM_PREFETCH_LOG_PAGES / VACUUM_PREFETCH_LOG_BLOCK_BUFFER_PAGES)

/* number or log pages on each block of buffer log prefetch */
#define VACUUM_PREFETCH_LOG_BLOCK_BUFFER_PAGES \
  (1 + vacuum_Data->log_block_npages)

#define VACUUM_JOB_QUEUE_SAFETY_BUFFER 10
#define VACUUM_JOB_QUEUE_MIN_CAPACITY \
  (prm_get_integer_value (PRM_ID_VACUUM_WORKER_COUNT) \
   + VACUUM_JOB_QUEUE_SAFETY_BUFFER)
#define VACUUM_JOB_QUEUE_CAPACITY \
  (VACUUM_PREFETCH_LOG_BLOCK_BUFFERS_COUNT - \
   prm_get_integer_value (PRM_ID_VACUUM_WORKER_COUNT))

#define VACUUM_PREFETCH_LOG_BLOCK_BUFFER(i) \
  ((char *) (PTR_ALIGN (vacuum_Prefetch_log_buffer, MAX_ALIGNMENT)) \
   + (((size_t) (i)) * ((size_t) LOG_PAGESIZE) \
      * ((size_t) VACUUM_PREFETCH_LOG_BLOCK_BUFFER_PAGES)))

#else /* SERVER_MODE */
#define VACUUM_JOB_QUEUE_CAPACITY (1024)
#endif /* SERVER_MODE */

#define VACUUM_INIT_PREFETCH_BLOCK(block) \
  do \
    { \
      (block)->buffer_id = VACUUM_LOG_BLOCK_BUFFER_INVALID; \
      (block)->start_page = NULL_PAGEID; \
      (block)->last_page = NULL_PAGEID; \
    } while (0)

/*
 * Dropped files section.
 */

static bool vacuum_Dropped_files_loaded = false;

/* Identifier for the file where dropped file list is kept */
static VFID vacuum_Dropped_files_vfid;

/* Identifier for first page in dropped files */
static VPID vacuum_Dropped_files_vpid;

/* Total count of dropped files */
static INT32 vacuum_Dropped_files_count = 0;

/* Dropped file entry */
typedef struct vacuum_dropped_file VACUUM_DROPPED_FILE;
struct vacuum_dropped_file
{
  VFID vfid;
  MVCCID mvccid;
};

/* A page of dropped files entries */
typedef struct vacuum_dropped_files_page VACUUM_DROPPED_FILES_PAGE;
struct vacuum_dropped_files_page
{
  VPID next_page;		/* VPID of next dropped files page. */
  INT16 n_dropped_files;	/* Number of entries on page */

  /* Leave the dropped files at the end of the structure */
  VACUUM_DROPPED_FILE dropped_files[1];	/* Dropped files. */
};

/* Size of dropped file page header */
#define VACUUM_DROPPED_FILES_PAGE_HEADER_SIZE \
  (offsetof (VACUUM_DROPPED_FILES_PAGE, dropped_files))

/* Capacity of dropped file page */
#define VACUUM_DROPPED_FILES_PAGE_CAPACITY \
  ((INT16) ((DB_PAGESIZE - VACUUM_DROPPED_FILES_PAGE_HEADER_SIZE) \
	    / sizeof (VACUUM_DROPPED_FILE)))
/* Capacity of dropped file page when page size is max */
#define VACUUM_DROPPED_FILES_MAX_PAGE_CAPACITY \
  ((INT16) ((IO_MAX_PAGE_SIZE - VACUUM_DROPPED_FILES_PAGE_HEADER_SIZE) \
	    / sizeof (VACUUM_DROPPED_FILE)))

#define VACUUM_DROPPED_FILE_FLAG_DUPLICATE 0x8000

/* Overwritten versions of pgbuf_fix, pgbuf_unfix and pgbuf_set_dirty,
 * adapted for the needs of vacuum and its dropped files pages.
 */
#define vacuum_fix_dropped_entries_page(thread_p, vpidp, latch) \
  ((VACUUM_DROPPED_FILES_PAGE *) pgbuf_fix (thread_p, vpidp, OLD_PAGE, \
                                            latch, \
                                            PGBUF_UNCONDITIONAL_LATCH))
#define vacuum_unfix_dropped_entries_page(thread_p, dropped_page) \
  do \
    { \
      pgbuf_unfix (thread_p, (PAGE_PTR) (dropped_page)); \
      (dropped_page) = NULL; \
    } while (0)
#define vacuum_set_dirty_dropped_entries_page(thread_p, dropped_page, free) \
  do \
    { \
      pgbuf_set_dirty (thread_p, (PAGE_PTR) (dropped_page), free); \
      if ((free) == FREE) \
	{ \
	  (dropped_page) = NULL; \
	} \
    } while (0)

/* Dropped files recovery flags */
#define VACUUM_DROPPED_FILES_RV_FLAG_DUPLICATE	  0x8000
#define VACUUM_DROPPED_FILES_RV_FLAG_NEWPAGE	  0x4000
#define VACUUM_DROPPED_FILES_RV_CLEAR_MASK		  0x3FFF

#if !defined (NDEBUG)
/* Track pages allocated for dropped files. Used for debugging only, for
 * easy observation of the lists of dropped files at any time.
 */
typedef struct vacuum_track_dropped_files VACUUM_TRACK_DROPPED_FILES;
struct vacuum_track_dropped_files
{
  VACUUM_TRACK_DROPPED_FILES *next_tracked_page;
  VACUUM_DROPPED_FILES_PAGE dropped_data_page;
};
VACUUM_TRACK_DROPPED_FILES *vacuum_Track_dropped_files;
#define VACUUM_TRACK_DROPPED_FILES_SIZE \
  (DB_PAGESIZE + sizeof (VACUUM_TRACK_DROPPED_FILES *))
#endif /* !NDEBUG */

INT32 vacuum_Dropped_files_version = 0;
pthread_mutex_t vacuum_Dropped_files_mutex;
VFID vacuum_Last_dropped_vfid;

typedef struct vacuum_dropped_files_rcv_data VACUUM_DROPPED_FILES_RCV_DATA;
struct vacuum_dropped_files_rcv_data
{
  VFID vfid;
  MVCCID mvccid;
  OID class_oid;
};

#define VACUUM_DEFAULT_HEAP_OBJECT_BUFFER_SIZE  4000

#define VACUUM_LOG_BLOCK_BUFFER_INVALID (-1)

/*
 * Vacuum worker section
 */

/* Convert vacuum worker TRANID to an index in vacuum worker's array */
#define VACUUM_WORKER_INDEX_TO_TRANID(index) \
  (-index + NULL_TRANID - 1)

/* Convert index in vacuum worker's array to TRANID */
#define VACUUM_WORKER_TRANID_TO_INDEX(trid) \
  (-trid + NULL_TRANID - 1)

/* Static array of vacuum workers */
VACUUM_WORKER vacuum_Workers[VACUUM_MAX_WORKER_COUNT];
#if defined (SA_MODE)
/* Vacuum worker structure used to execute vacuum jobs in SA_MODE.
 * TODO: Implement vacuum execution for SA_MODE.
 */
VACUUM_WORKER *vacuum_Worker_sa_mode;
#endif

/* Number of worker threads that have a worker structure assigned. */
int vacuum_Assigned_workers_count = 0;
INT32 vacuum_Running_workers_count = 0;

#if defined (SERVER_MODE)
#define VACUUM_GET_WORKER(thread_p) \
  ((VACUUM_WORKER *) ((thread_p)->vacuum_worker))
#else /* !SERVER_MODE */
#define VACUUM_GET_WORKER(thread_p) NULL
#endif /* !SERVER_MODE */

/* VACUUM_HEAP_HELPER -
 * Structure used by vacuum heap functions.
 */
typedef struct vacuum_heap_helper VACUUM_HEAP_HELPER;
struct vacuum_heap_helper
{
  PAGE_PTR home_page;		/* Home page for objects
				 * being vacuumed.
				 */
  VPID home_vpid;		/* VPID of home page. */
  PAGE_PTR forward_page;	/* Used to keep forward page
				 * of REC_RELOCATION or first
				 * overflow page of
				 * REC_BIGONE.
				 */
  OID forward_oid;		/* Link to forward page. */
  PGSLOTID crt_slotid;		/* Slot ID of current record
				 * being vacuumed.
				 */
  INT16 record_type;		/* Current record type. */
  RECDES record;		/* Current record data. */

  /* buffer of current record (used by NEW_HOME) */
  char rec_buf[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT];

  MVCC_REC_HEADER mvcc_header;	/* MVCC header. */

  HFID hfid;			/* Heap file identifier. */
  VFID overflow_vfid;		/* Overflow file identifier.
				 */
  bool reusable;		/* True if heap file has
				 * reusable slots.
				 */

  MVCC_SATISFIES_VACUUM_RESULT can_vacuum;	/* Result of vacuum check. */

  /* Collect data on vacuum. */
  PGSLOTID slots[MAX_SLOTS_IN_PAGE];	/* Slot ID's. */
  MVCC_SATISFIES_VACUUM_RESULT results[MAX_SLOTS_IN_PAGE];	/* Vacuum check
								 * results.
								 */
  OID next_versions[MAX_SLOTS_IN_PAGE];	/* Next version links. */
  OID partition_links[MAX_SLOTS_IN_PAGE];	/* Partition links. */

  OID forward_links[2];		/* REC_BIGONE, REC_RELOCATION forward links.
				 * (buffer for forward_recdes) */
  RECDES forward_recdes;	/* Record descriptor to read forward links. */

  int n_bulk_vacuumed;		/* Number of vacuumed objects to be logged in
				 * bulk mode.
				 */
  int n_vacuumed;		/* Number of vacuumed objects.
				 */
  int initial_home_free_space;	/* Free space in home page before vacuum */

  /* Performance tracking. */
  PERF_UTIME_TRACKER time_track;
};

#define VACUUM_PERF_HEAP_START(thread_p, helper) \
  PERF_UTIME_TRACKER_START (thread_p, &(helper)->time_track);
#define VACUUM_PERF_HEAP_TRACK_PREPARE(thread_p, helper) \
  PERF_UTIME_TRACKER_TIME_AND_RESTART (thread_p, &(helper)->time_track, \
				       mnt_heap_vacuum_prepare_time)
#define VACUUM_PERF_HEAP_TRACK_EXECUTE(thread_p, helper) \
  PERF_UTIME_TRACKER_TIME_AND_RESTART (thread_p, &(helper)->time_track, \
				       mnt_heap_vacuum_execute_time)
#define VACUUM_PERF_HEAP_TRACK_LOGGING(thread_p, helper) \
  PERF_UTIME_TRACKER_TIME_AND_RESTART (thread_p, &(helper)->time_track, \
				       mnt_heap_vacuum_log_time)

/* Flags used to mark rcv->offset with hints about recovery process. */
/* Flas for reusable heap files. */
#define VACUUM_LOG_VACUUM_HEAP_REUSABLE	      0x8000
/* Flag if page is entirely vacuumed. */
#define VACUUM_LOG_VACUUM_HEAP_ALL_VACUUMED   0x4000
/* Mask. */
#define VACUUM_LOG_VACUUM_HEAP_MASK	      0xC000

static void vacuum_log_vacuum_heap_page (THREAD_ENTRY * thread_p,
					 PAGE_PTR page_p,
					 int n_slots, PGSLOTID * slots,
					 MVCC_SATISFIES_VACUUM_RESULT *
					 results, OID * next_versions,
					 OID * partition_oids,
					 bool reusable, bool all_vacuumed);
static void vacuum_log_remove_ovf_insid (THREAD_ENTRY * thread_p,
					 PAGE_PTR ovfpage);

static int vacuum_init_master_prefetch (THREAD_ENTRY * thread_p);

static int vacuum_heap (THREAD_ENTRY * thread_p, VACUUM_WORKER * worker,
			MVCCID threshold_mvccid, bool was_interrupted);
static int vacuum_heap_prepare_record (THREAD_ENTRY * thread_p,
				       VACUUM_HEAP_HELPER * helper);
static int vacuum_heap_record_insert_mvccid (THREAD_ENTRY * thread_p,
					     VACUUM_HEAP_HELPER * helper);
static int vacuum_heap_record (THREAD_ENTRY * thread_p,
			       VACUUM_HEAP_HELPER * helper);
static int vacuum_heap_get_hfid (THREAD_ENTRY * thread_p,
				 VACUUM_HEAP_HELPER * helper);
static void vacuum_heap_page_log_and_reset (THREAD_ENTRY * thread_p,
					    VACUUM_HEAP_HELPER * helper,
					    bool update_best_space_stat,
					    bool unlatch_page);

static void vacuum_process_vacuum_data (THREAD_ENTRY * thread_p);
static int vacuum_process_log_block (THREAD_ENTRY * thread_p,
				     VACUUM_DATA_ENTRY * block_data,
				     BLOCK_LOG_BUFFER * block_log_buffer,
				     bool sa_mode_partial_block);
static void vacuum_finished_block_vacuum (THREAD_ENTRY * thread_p,
					  VACUUM_DATA_ENTRY * block_data,
					  bool is_vacuum_complete);

static int vacuum_process_log_record (THREAD_ENTRY * thread_p,
				      VACUUM_WORKER * worker,
				      LOG_LSA * log_lsa_p,
				      LOG_PAGE * log_page_p,
				      struct log_data *log_record_data,
				      MVCCID * mvccid,
				      char **undo_data_ptr,
				      int *undo_data_size,
				      struct log_vacuum_info *vacuum_info,
				      bool * is_file_dropped,
				      bool stop_after_vacuum_info);
static int vacuum_compare_data_entries (const void *ptr1, const void *ptr2);
static int vacuum_load_data_from_disk (THREAD_ENTRY * thread_p);
static VACUUM_DATA_ENTRY *vacuum_get_vacuum_data_entry (VACUUM_LOG_BLOCKID
							blockid);
static bool vacuum_is_work_in_progress (THREAD_ENTRY * thread_p);
static void vacuum_data_remove_finished_entries (THREAD_ENTRY * thread_p);
static void vacuum_data_remove_entries (THREAD_ENTRY * thread_p,
					int n_removed_entries,
					int *removed_entries);

static void vacuum_log_remove_data_entries (THREAD_ENTRY * thread_p,
					    int *removed_indexes,
					    int n_removed_indexes);
static void vacuum_log_append_block_data (THREAD_ENTRY * thread_p,
					  VACUUM_DATA_ENTRY * new_entries,
					  int n_new_entries,
					  LOG_LSA * first_blocks_to_recover,
					  int n_first_blocks_to_recover);

static int vacuum_compare_dropped_files (const void *a, const void *b);
static int vacuum_add_dropped_file (THREAD_ENTRY * thread_p, VFID * vfid,
				    MVCCID mvccid, LOG_RCV * rcv,
				    LOG_LSA * postpone_ref_lsa);
static int vacuum_cleanup_dropped_files (THREAD_ENTRY * thread_p);
static bool vacuum_find_dropped_file (THREAD_ENTRY * thread_p, VFID * vfid,
				      MVCCID mvccid);
static void vacuum_log_cleanup_dropped_files (THREAD_ENTRY * thread_p,
					      PAGE_PTR page_p,
					      INT16 * indexes,
					      INT16 n_indexes);
static void vacuum_log_dropped_files_set_next_page (THREAD_ENTRY * thread_p,
						    PAGE_PTR page_p,
						    VPID * next_page);

static void vacuum_update_oldest_mvccid (THREAD_ENTRY * thread_p);

static int vacuum_compare_heap_object (const void *a, const void *b);
static int vacuum_collect_heap_objects (VACUUM_WORKER * worker, OID * oid,
					VFID * vfid);
static void vacuum_cleanup_collected_by_vfid (VACUUM_WORKER * worker,
					      VFID * vfid);

static void vacuum_log_blocks_to_recover (THREAD_ENTRY * thread_p);
static int vacuum_recover_blocks_from_log (THREAD_ENTRY * thread_p);
static void vacuum_rv_discard_recovered_blocks_from_buffer (THREAD_ENTRY *
							    thread_p);

static VPID *vacuum_get_first_page_dropped_files (THREAD_ENTRY * thread_p,
						  VPID * first_page_vpid);
static VPID *vacuum_get_first_page_vacuum_data (THREAD_ENTRY * thread_p,
						VPID * first_page_vpid);

static int vacuum_assign_worker (THREAD_ENTRY * thread_p);
static void vacuum_finalize_worker (THREAD_ENTRY * thread_p,
				    VACUUM_WORKER * worker_info);
static INT32 vacuum_get_worker_min_dropped_files_version (void);

#if !defined (NDEBUG)
/* Debug function to verify vacuum data. */
static void vacuum_verify_vacuum_data_debug (void);
#define VACUUM_VERIFY_VACUUM_DATA() vacuum_verify_vacuum_data_debug ()
#else /* NDEBUG */
#define VACUUM_VERIFY_VACUUM_DATA()
#endif /* NDEBUG */

#if defined (SERVER_MODE)
static int vacuum_log_prefetch_vacuum_block (THREAD_ENTRY * thread_p,
					     VACUUM_DATA_ENTRY * entry,
					     BLOCK_LOG_BUFFER
					     * block_log_buffer);
#endif /* SERVER_MODE */
static int vacuum_copy_log_page (THREAD_ENTRY * thread_p,
				 LOG_PAGEID log_pageid,
				 BLOCK_LOG_BUFFER * block_log_buffer,
				 LOG_PAGE * log_page);

static bool is_not_vacuumed_and_lost (THREAD_ENTRY * thread_p,
				      MVCC_REC_HEADER * rec_header);
static void print_not_vacuumed_to_log (OID * oid, OID * class_oid,
				       MVCC_REC_HEADER * rec_header,
				       int btree_node_type);
static void vacuum_log_redoundo_vacuum_record (THREAD_ENTRY * thread_p,
					       PAGE_PTR page_p,
					       PGSLOTID slotid,
					       RECDES * undo_recdes,
					       OID * next_version_oid,
					       OID * partition_oid,
					       bool reusable);

/*
 * xvacuum () - Vacuumes database
 *
 * return	    : Error code.
 * thread_p(in)	    :
 *
 * NOTE: CS mode temporary disabled.
 */
int
xvacuum (THREAD_ENTRY * thread_p)
{
  int dummy_save_type = 0;

#if defined(SERVER_MODE)
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_VACUUM_CS_NOT_AVAILABLE, 0);
  return ER_VACUUM_CS_NOT_AVAILABLE;
#else	/* !SERVER_MODE */		   /* SA_MODE */

  if (vacuum_Data->n_table_entries == 0
      && LSA_ISNULL (&vacuum_Data->first_blocks_to_recover)
      && LOCK_FREE_CIRCULAR_QUEUE_IS_EMPTY (vacuum_Block_data_buffer)
      && LSA_ISNULL (&log_Gl.hdr.mvcc_op_log_lsa))
    {
      /* Nothing to vacuum. */
      return NO_ERROR;
    }

  assert (vacuum_Assigned_workers_count <= 1);
  VACUUM_CONVERT_THREAD_TO_VACUUM_WORKER (thread_p, &vacuum_Workers[0],
					  dummy_save_type);
  if (vacuum_Assigned_workers_count == 0)
    {
      /* Assign worker and allocate required resources. */
      vacuum_assign_worker (thread_p);
    }

  /* Process vacuum data and run vacuum . */
  vacuum_process_vacuum_data (thread_p);

  VACUUM_RESTORE_THREAD (thread_p, dummy_save_type);

  return NO_ERROR;
#endif /* SA_MODE */
}

/*
 * vacuum_initialize () - Initialize necessary structures for vacuum.
 *
 * return		   : Void.
 * thread_p (in)	   : Thread entry.
 * vacuum_data_npages (in) : Number of vacuum data pages.
 * vacuum_data_vfid (in)   : Vacuum data VFID.
 * dropped_files_vfid (in) : Dropped files VFID.
 */
int
vacuum_initialize (THREAD_ENTRY * thread_p, int vacuum_data_npages,
		   VFID * vacuum_data_vfid, VFID * dropped_files_vfid)
{
  int error_code = NO_ERROR;
  int i;

  if (prm_get_bool_value (PRM_ID_DISABLE_VACUUM))
    {
      return NO_ERROR;
    }

  /* Initialize vacuum data */
  vacuum_Data = NULL;
  (void) pthread_mutex_init (&vacuum_Data_mutex, NULL);
  VFID_COPY (&vacuum_Data_vfid, vacuum_data_vfid);
  VPID_SET_NULL (&vacuum_Data_vpid);

  /* Initialize vacuum dropped files */
  vacuum_Dropped_files_loaded = false;
  VFID_COPY (&vacuum_Dropped_files_vfid, dropped_files_vfid);

  /* Save first page vpid. */
  if (vacuum_get_first_page_dropped_files (thread_p,
					   &vacuum_Dropped_files_vpid)
      == NULL)
    {
      assert (false);
      goto error;
    }
  assert (!VPID_ISNULL (&vacuum_Dropped_files_vpid));

  vacuum_Dropped_files_version = 0;
  vacuum_Dropped_files_count = 0;
  pthread_mutex_init (&vacuum_Dropped_files_mutex, NULL);
  VFID_SET_NULL (&vacuum_Last_dropped_vfid);
#if !defined (NDEBUG)
  vacuum_Track_dropped_files = NULL;
#endif

  /* Save number of disk pages in vacuum data */
  vacuum_Data_npages = vacuum_data_npages;

  /* load vacuum data (also sets vacuum_Data->log_block_npages which must be
   * preserved after server restarts) */
  if (vacuum_load_data_from_disk (thread_p) != NO_ERROR)
    {
      goto error;
    }

  /* Initialize the log block data buffer */
  vacuum_Block_data_buffer =
    lf_circular_queue_create (VACUUM_BLOCK_DATA_BUFFER_CAPACITY,
			      sizeof (VACUUM_DATA_ENTRY));
  if (vacuum_Block_data_buffer == NULL)
    {
      goto error;
    }

#if defined(SERVER_MODE)
  vacuum_Prefetch_log_pages =
    prm_get_integer_value (PRM_ID_VACUUM_PREFETCH_LOG_NBUFFERS);

  if (VACUUM_JOB_QUEUE_CAPACITY < VACUUM_JOB_QUEUE_MIN_CAPACITY)
    {
      vacuum_Prefetch_log_pages =
	(prm_get_integer_value (PRM_ID_VACUUM_WORKER_COUNT)
	 + VACUUM_JOB_QUEUE_MIN_CAPACITY)
	* VACUUM_PREFETCH_LOG_BLOCK_BUFFER_PAGES;
      assert (VACUUM_JOB_QUEUE_CAPACITY >=
	      prm_get_integer_value (PRM_ID_VACUUM_WORKER_COUNT));
    }

  vacuum_Prefetch_log_mode =
    prm_get_integer_value (PRM_ID_VACUUM_PREFETCH_LOG_MODE);
  if (vacuum_Prefetch_log_mode == VACUUM_PREFETCH_LOG_MODE_MASTER)
    {
      error_code = vacuum_init_master_prefetch (thread_p);
      if (error_code != NO_ERROR)
	{
	  goto error;
	}
    }

  /* Initialize job queue */
  vacuum_Job_queue =
    lf_circular_queue_create (VACUUM_JOB_QUEUE_CAPACITY,
			      sizeof (VACUUM_JOB_ENTRY));
  if (vacuum_Job_queue == NULL)
    {
      goto error;
    }
#endif /* SERVER_MODE */

  /* Initialize worker counters */
  vacuum_Assigned_workers_count = 0;
  vacuum_Running_workers_count = 0;
  /* Initialize workers */
  for (i = 0; i < VACUUM_MAX_WORKER_COUNT; i++)
    {
      vacuum_Workers[i].drop_files_version = 0;
      vacuum_Workers[i].state = VACUUM_WORKER_STATE_INACTIVE;
      vacuum_Workers[i].log_zip_p = NULL;
      vacuum_Workers[i].undo_data_buffer = NULL;
      vacuum_Workers[i].undo_data_buffer_capacity = 0;
      vacuum_Workers[i].heap_objects = NULL;
      vacuum_Workers[i].heap_objects_capacity = 0;
      vacuum_Workers[i].tdes = NULL;
#if defined (SERVER_MODE)
      vacuum_Workers[i].prefetch_log_buffer = NULL;
#endif /* SERVER_MODE */
      vacuum_Workers[i].postpone_cache_status = VACUUM_CACHE_POSTPONE_NO;
      vacuum_Workers[i].postpone_redo_data_ptr = NULL;
      vacuum_Workers[i].postpone_redo_data_buffer = NULL;
      vacuum_Workers[i].postpone_cached_entries_count = 0;
    }

  /* Allocate transaction descriptors for vacuum workers. */
  for (i = 0; i < VACUUM_MAX_WORKER_COUNT; i++)
    {
      vacuum_Workers[i].tdes = (LOG_TDES *) malloc (sizeof (LOG_TDES));
      if (vacuum_Workers[i].tdes == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
		  1, sizeof (LOG_TDES));
	  error_code = ER_OUT_OF_VIRTUAL_MEMORY;
	  goto error;
	}
      logtb_initialize_vacuum_worker_tdes (vacuum_Workers[i].tdes,
					   VACUUM_WORKER_INDEX_TO_TRANID (i));
    }

  vacuum_Global_oldest_active_blockers_counter = 0;

  return NO_ERROR;

error:
  vacuum_finalize (thread_p);
  return (error_code == NO_ERROR) ? ER_FAILED : error_code;
}

#if defined(SERVER_MODE)
/*
 * vacuum_init_master_prefetch () - Initialize necessary structures for
 *				    prefetching log data with vacuum master
 *				    thread
 *
 * return		   : Void.
 * thread_p (in)	   : Thread entry.
 * vacuum_data_npages (in) : Number of vacuum data pages.
 * vacuum_data_vfid (in)   : Vacuum data VFID.
 * dropped_files_vfid (in) : Dropped files VFID.
 */
static int
vacuum_init_master_prefetch (THREAD_ENTRY * thread_p)
{
  int error_code = NO_ERROR;
  long long unsigned size_vacuum_prefetch_log_buffer;

  size_vacuum_prefetch_log_buffer =
    (((long long unsigned) VACUUM_PREFETCH_LOG_PAGES) * LOG_PAGESIZE)
    + MAX_ALIGNMENT;

  vacuum_er_log (VACUUM_ER_LOG_MASTER,
		 "VACUUM INIT: prefetch pages:%d, log_page_size:%d, "
		 "prefetch buffer size:%llu, job_queue_capacity:%d.",
		 (int) VACUUM_PREFETCH_LOG_PAGES,
		 (int) LOG_PAGESIZE,
		 size_vacuum_prefetch_log_buffer,
		 (int) VACUUM_JOB_QUEUE_CAPACITY);

  if (!MEM_SIZE_IS_VALID (size_vacuum_prefetch_log_buffer))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_PRM_BAD_VALUE, 1,
	      "vacuum_prefetch_log_pages");
      error_code = ER_PRM_BAD_VALUE;
      goto error;
    }

  vacuum_Prefetch_log_buffer =
    (LOG_PAGE *) malloc ((size_t) size_vacuum_prefetch_log_buffer);
  if (vacuum_Prefetch_log_buffer == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
	      1, (size_t) size_vacuum_prefetch_log_buffer);
      error_code = ER_OUT_OF_VIRTUAL_MEMORY;
      goto error;
    }

  error_code =
    lf_bitmap_init (&vacuum_Prefetch_free_buffers_bitmap,
		    LF_BITMAP_ONE_CHUNK,
		    VACUUM_PREFETCH_LOG_BLOCK_BUFFERS_COUNT,
		    LF_BITMAP_FULL_USAGE_RATIO);
  if (error_code != NO_ERROR)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
	      1, VACUUM_JOB_QUEUE_CAPACITY * sizeof (unsigned int)
	      / LF_BITFIELD_WORD_SIZE);
      error_code = ER_OUT_OF_VIRTUAL_MEMORY;
      goto error;
    }

error:
  return error_code;
}
#endif /* SERVER_MODE */

/*
* vacuum_finalize () - Finalize necessary structures for vacuum and
*		       auto-vacuum.
*
* return	: Void.
* thread_p (in) : Thread entry.
 */
void
vacuum_finalize (THREAD_ENTRY * thread_p)
{
  int i;

  if (prm_get_bool_value (PRM_ID_DISABLE_VACUUM))
    {
      return;
    }
  VACUUM_LOCK_DATA ();

#if defined(SERVER_MODE)
  while (vacuum_is_work_in_progress (thread_p))
    {
      /* Must wait for vacuum workers to finish */
      VACUUM_UNLOCK_DATA ();
      thread_sleep (0);
      VACUUM_LOCK_DATA ();
    }
#endif

  if (vacuum_Data != NULL)
    {
      /* Flush vacuum data to disk */
      (void) vacuum_flush_data (thread_p, NULL, NULL, NULL, true);

      if (!LOCK_FREE_CIRCULAR_QUEUE_IS_EMPTY (vacuum_Block_data_buffer))
	{
	  /* Block data is lost, this should never happen */
	  /* TODO: Make sure vacuum data is big enough to handle all buffered
	   *       blocks.
	   */
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  assert (0);
	}

      /* Free vacuum data */
      if (vacuum_Data != NULL)
	{
	  free_and_init (vacuum_Data);
	}
    }

  /* Destroy log blocks data buffer */
  if (vacuum_Block_data_buffer != NULL)
    {
      lf_circular_queue_destroy (vacuum_Block_data_buffer);
      vacuum_Block_data_buffer = NULL;
    }

  /* Destroy job queue */
  if (vacuum_Job_queue != NULL)
    {
      lf_circular_queue_destroy (vacuum_Job_queue);
      vacuum_Job_queue = NULL;
    }

  /* Free all resources allocated for vacuum workers */
  for (i = 0; i < VACUUM_MAX_WORKER_COUNT; i++)
    {
      vacuum_finalize_worker (thread_p, &vacuum_Workers[i]);
    }

#if defined(SERVER_MODE)
  if (vacuum_Prefetch_log_mode == VACUUM_PREFETCH_LOG_MODE_MASTER)
    {
      if (vacuum_Prefetch_log_buffer != NULL)
	{
	  free_and_init (vacuum_Prefetch_log_buffer);
	}
      lf_bitmap_destroy (&vacuum_Prefetch_free_buffers_bitmap);
    }
#endif /* SERVER_MODE */

  /* Unlock data */
  VACUUM_UNLOCK_DATA ();
}

/*
 * vacuum_heap () - Vacuum heap objects.
 *
 * return		 : Error code.
 * thread_p (in)	 : Thread entry.
 * heap_objects (in)	 : Array of heap objects (VFID & OID).
 * n_heap_objects (in)	 : Number of heap objects.
 * threshold_mvccid (in) : Threshold MVCCID used for vacuum check.
 * was_interrutped (in)  : True if same job was executed and interrupted.
 */
static int
vacuum_heap (THREAD_ENTRY * thread_p, VACUUM_WORKER * worker,
	     MVCCID threshold_mvccid, bool was_interrupted)
{
  VACUUM_HEAP_OBJECT *page_ptr;
  VACUUM_HEAP_OBJECT *obj_ptr;
  int error_code = NO_ERROR;
  VFID vfid = VFID_INITIALIZER;
  bool reusable = false;
  int object_count = 0;

  if (worker->n_heap_objects == 0)
    {
      return NO_ERROR;
    }

  /* Set state to execute mode. */
  worker->state = VACUUM_WORKER_STATE_EXECUTE;

  /* Sort all objects. Sort function will order all objects first by VFID
   * then by OID. All objects belonging to one file will be consecutive.
   * Also, all objects belonging to one page will be consecutive.
   * Vacuum will be called for each different heap page.
   */
  qsort (worker->heap_objects, worker->n_heap_objects,
	 sizeof (VACUUM_HEAP_OBJECT), vacuum_compare_heap_object);

  /* Start parsing array. Vacuum objects page by page. */
  for (page_ptr = worker->heap_objects;
       page_ptr < worker->heap_objects + worker->n_heap_objects;)
    {
      if (!VFID_EQ (&vfid, &page_ptr->vfid))
	{
	  /* Update VFID. */
	  VFID_COPY (&vfid, &page_ptr->vfid);
	  /* Update reusable. */
	  reusable = file_get_type (thread_p, &vfid) == FILE_HEAP_REUSE_SLOTS;
	}

      /* Find all objects for this page. */
      object_count = 1;
      for (obj_ptr = page_ptr + 1;
	   obj_ptr < worker->heap_objects + worker->n_heap_objects
	   && obj_ptr->oid.pageid == page_ptr->oid.pageid
	   && obj_ptr->oid.volid == page_ptr->oid.volid; obj_ptr++)
	{
	  object_count++;
	}
      /* Vacuum page. */
      error_code =
	vacuum_heap_page (thread_p, page_ptr, object_count, threshold_mvccid,
			  reusable, was_interrupted);
      if (error_code != NO_ERROR)
	{
	  vacuum_er_log (VACUUM_ER_LOG_ERROR | VACUUM_ER_LOG_HEAP,
			 "VACUUM ERROR: Vacuum heap page %d|%d, "
			 "error_code=%d.\n",
			 page_ptr->oid.volid, page_ptr->oid.pageid);

	  assert_release (false);
	  er_clear ();
	  error_code = NO_ERROR;
	  /* Release should not stop. Continue. */
	}
      /* Advance to next page. */
      page_ptr = obj_ptr;
    }
  return NO_ERROR;
}

/*
 * vacuum_heap_page () - Vacuum objects in one heap page.
 *
 * return		 : Error code.
 * thread_p (in)	 : Thread entry.
 * heap_objects (in)	 : Array of objects to vacuum.
 * n_heap_objects (in)	 : Number of objects.
 * threshold_mvccid (in) : Threshold MVCCID used to vacuum.
 * reusable (in)	 : True if object slots are reusable.
 * was_interrutped (in)  : True if same job was executed and interrupted.
 */
int
vacuum_heap_page (THREAD_ENTRY * thread_p, VACUUM_HEAP_OBJECT * heap_objects,
		  int n_heap_objects, MVCCID threshold_mvccid, bool reusable,
		  bool was_interrupted)
{
  VACUUM_HEAP_HELPER helper;	/* Vacuum heap helper. */
  HEAP_PAGE_VACUUM_STATUS page_vacuum_status;	/* Current page vacuum status.
						 */
  int error_code = NO_ERROR;	/* Error code. */
  int obj_index = 0;		/* Index used to iterate the object
				 * array.
				 */

  /* Assert expected arguments. */
  assert (heap_objects != NULL);
  assert (n_heap_objects > 0);
  assert (MVCCID_IS_NORMAL (threshold_mvccid));

  VACUUM_PERF_HEAP_START (thread_p, &helper);

  /* Get page from first object. */
  VPID_GET_FROM_OID (&helper.home_vpid, &heap_objects->oid);
  if (was_interrupted)
    {
      DISK_ISVALID valid =
	disk_isvalid_page (thread_p, helper.home_vpid.volid,
			   helper.home_vpid.pageid);
      if (valid == DISK_INVALID)
	{
	  /* Page was already deallocated in previous job run. */
	  /* Safe guard: this was possible if there was only one object to
	   *             be vacuumed.
	   */
	  assert (n_heap_objects == 1);
	  return NO_ERROR;
	}
      else if (valid == DISK_ERROR)
	{
	  assert_release (false);
	  return ER_FAILED;
	}
      /* Valid page. Proceed to vacuum. */
    }

#if !defined (NDEBUG)
  /* Check all objects belong to same page. */
  {
    int i = 0;
    assert (HEAP_ISVALID_OID (&heap_objects->oid) != DISK_INVALID);
    for (i = 1; i < n_heap_objects; i++)
      {
	assert (heap_objects[i].oid.volid == heap_objects[0].oid.volid
		&& heap_objects[i].oid.pageid == heap_objects[0].oid.pageid);
	assert (heap_objects[i].oid.slotid > 0);
	assert (heap_objects[i].vfid.fileid == heap_objects[0].vfid.fileid
		&& heap_objects[i].vfid.volid == heap_objects[0].vfid.volid);
      }
  }
#endif /* !NDEBUG */

  /* Initialize helper. */
  helper.reusable = reusable;
  helper.home_page = NULL;
  helper.forward_page = NULL;
  helper.n_vacuumed = 0;
  helper.n_bulk_vacuumed = 0;
  helper.initial_home_free_space = -1;
  HFID_SET_NULL (&helper.hfid);
  VFID_SET_NULL (&helper.overflow_vfid);

  /* Fix heap page. */
  helper.home_page =
    pgbuf_fix (thread_p, &helper.home_vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
	       PGBUF_UNCONDITIONAL_LATCH);
  if (helper.home_page == NULL)
    {
      ASSERT_ERROR_AND_SET (error_code);
      vacuum_er_log (VACUUM_ER_LOG_ERROR | VACUUM_ER_LOG_HEAP,
		     "VACUUM ERROR: Failed to fix page %d|%d.\n",
		     helper.home_vpid.volid, helper.home_vpid.pageid);
      return error_code;
    }
  (void) pgbuf_check_page_ptype (thread_p, helper.home_page, PAGE_HEAP);

  helper.initial_home_free_space =
    spage_get_free_space_without_saving (thread_p, helper.home_page, NULL);

  helper.crt_slotid = -1;
  for (obj_index = 0; obj_index < n_heap_objects; obj_index++)
    {
      if (helper.crt_slotid == heap_objects[obj_index].oid.slotid)
	{
	  /* Same object. Do not check it twice. */
	  continue;
	}
      /* Set current slotid. */
      helper.crt_slotid = heap_objects[obj_index].oid.slotid;

      /* Prepare record for vacuum (get all required pages, info and MVCC
       * header).
       */
      error_code = vacuum_heap_prepare_record (thread_p, &helper);
      if (error_code != NO_ERROR)
	{
	  vacuum_er_log (VACUUM_ER_LOG_ERROR | VACUUM_ER_LOG_HEAP,
			 "VACUUM ERROR: Could not prepare vacuum for "
			 "object %d|%d|%d.\n",
			 heap_objects[obj_index].oid.volid,
			 heap_objects[obj_index].oid.pageid,
			 heap_objects[obj_index].oid.slotid);

	  assert_release (false);
	  if (helper.forward_page != NULL)
	    {
	      pgbuf_unfix_and_init (thread_p, helper.forward_page);
	    }
	  er_clear ();
	  error_code = NO_ERROR;
	  continue;
	}
      /* Safe guard. */
      assert (helper.home_page != NULL);

      switch (helper.record_type)
	{
	case REC_RELOCATION:
	case REC_HOME:
	case REC_BIGONE:

	  /* Check if record can be vacuumed. */
	  helper.can_vacuum =
	    mvcc_satisfies_vacuum (thread_p, &helper.mvcc_header,
				   threshold_mvccid);
	  if (helper.can_vacuum == VACUUM_RECORD_REMOVE)
	    {
	      /* Record has been deleted and it can be removed. */
	      error_code = vacuum_heap_record (thread_p, &helper);
	    }
	  else if (helper.can_vacuum == VACUUM_RECORD_DELETE_INSID)
	    {
	      /* Record insert MVCCID can be removed. */
	      error_code =
		vacuum_heap_record_insert_mvccid (thread_p, &helper);
	    }
	  else
	    {
	      /* Object could not be vacuumed. */
	    }
	  if (helper.forward_page != NULL)
	    {
	      pgbuf_unfix_and_init (thread_p, helper.forward_page);
	    }
	  if (error_code != NO_ERROR)
	    {
	      vacuum_er_log (VACUUM_ER_LOG_ERROR | VACUUM_ER_LOG_HEAP,
			     "VACUUM ERROR: Failed to vacuum object at "
			     "%d|%d|%d.\n",
			     helper.home_vpid.volid, helper.home_vpid.pageid,
			     helper.crt_slotid);

	      /* Debug should hit assert. Release should continue. */
	      assert_release (false);
	      er_clear ();
	      error_code = NO_ERROR;
	      continue;
	    }
	  break;

	default:
	  /* Object cannot be vacuumed. Most likely it was already vacuumed
	   * by another worker or it was rollbacked and reused.
	   */
	  assert (helper.forward_page == NULL);
	  break;
	}

      if (!VACUUM_IS_THREAD_VACUUM_WORKER (thread_p))
	{
	  continue;
	}

      /* Check page vacuum status. */
      page_vacuum_status =
	heap_page_get_vacuum_status (thread_p, helper.home_page);
      /* Safe guard. */
      assert (page_vacuum_status != HEAP_PAGE_VACUUM_NONE
	      || (was_interrupted && helper.n_vacuumed == 0));

      /* Page can be removed if no other worker will access this page.
       * If this worker is the only one expected, then it can remove the page.
       *
       * It is also possible that this job was previously executed and
       * interrupted due to shutdown or crash. This case is a little more
       * complicated. There are two scenarios:
       * 1. Current page status is vacuum none. This means all vacuum was
       *    already executed.
       * 2. Current page status is vacuum once. This means a vacuum is
       *    expected, but we cannot tell if current vacuum worker was
       *    interrupted and re-executes an old vacuum task or if it is
       *    executing the task expected by page status.
       *    Take next scenario:
       *    1. Insert new object at OID1. page status is vacuum once.
       *    2. Block with above operations is finished and vacuum job is
       *       started.
       *    3. Vacuum insert MVCCID at OID1. status is now vacuum none.
       *    4. Delete object at OID1. page status is set to vacuum once.
       *    5. Crash.
       *    6. Job on block at step #2 is restarted.
       *    7. Vacuum is executed on object OID1. Object can be removed.
       *    8. Vacuum is executed for delete operation at #4.
       *    It would be incorrect to change page status from vacuum once to
       *    none, since it will be followed by another vacuum task. Since
       *    vacuum none status means page might be deallocated, it is better
       *    to be paranoid about it.
       */
      if ((page_vacuum_status == HEAP_PAGE_VACUUM_ONCE && !was_interrupted)
	  || (page_vacuum_status == HEAP_PAGE_VACUUM_NONE && was_interrupted))
	{
	  assert (n_heap_objects == 1);
	  assert (helper.n_vacuumed <= 1);
	  if (page_vacuum_status == HEAP_PAGE_VACUUM_ONCE)
	    {
	      heap_page_set_vacuum_status_none (thread_p, helper.home_page);

	      vacuum_er_log (VACUUM_ER_LOG_HEAP,
			     "VACUUM: Changed vacuum status of heap page "
			     "%d|%d, lsa=%lld|%d from once to none.\n",
			     pgbuf_get_volume_id (helper.home_page),
			     pgbuf_get_page_id (helper.home_page),
			     (long long int)
			     pgbuf_get_lsa (helper.home_page)->pageid,
			     (int) pgbuf_get_lsa (helper.home_page)->offset);

	      VACUUM_PERF_HEAP_TRACK_EXECUTE (thread_p, &helper);

	      vacuum_log_vacuum_heap_page (thread_p, helper.home_page,
					   helper.n_bulk_vacuumed,
					   helper.slots, helper.results,
					   helper.next_versions,
					   helper.partition_links,
					   helper.reusable, true);

	      VACUUM_PERF_HEAP_TRACK_LOGGING (thread_p, &helper);
	    }

	  /* Reset n_vacuumed since they have been logged already. */
	  helper.n_vacuumed = 0;
	  helper.n_bulk_vacuumed = 0;

	  /* Set page dirty. */
	  pgbuf_set_dirty (thread_p, helper.home_page, DONT_FREE);

	  if (spage_number_of_records (helper.home_page) <= 1
	      && helper.reusable)
	    {
	      /* Try to remove page from heap. */

	      /* HFID is required. */
	      error_code = vacuum_heap_get_hfid (thread_p, &helper);
	      if (error_code != NO_ERROR)
		{
		  /* Give up. */
		  pgbuf_unfix_and_init (thread_p, helper.home_page);
		  goto end;
		}
	      assert (!HFID_IS_NULL (&helper.hfid));
	      VACUUM_PERF_HEAP_TRACK_PREPARE (thread_p, &helper);

	      if (pgbuf_has_prevent_dealloc (helper.home_page) == false
		  && heap_remove_page_on_vacuum (thread_p, &helper.home_page,
						 &helper.hfid))
		{
		  /* Successfully removed page. */
		  assert (helper.home_page == NULL);

		  vacuum_er_log (VACUUM_ER_LOG_WORKER | VACUUM_ER_LOG_HEAP,
				 "VACUUM: Successfully removed page %d|%d "
				 "from heap file (%d, %d|%d).\n",
				 helper.home_vpid.volid,
				 helper.home_vpid.pageid,
				 helper.hfid.hpgid,
				 helper.hfid.vfid.volid,
				 helper.hfid.vfid.fileid);

		  VACUUM_PERF_HEAP_TRACK_EXECUTE (thread_p, &helper);
		  goto end;
		}
	      else if (helper.home_page != NULL)
		{
		  /* Unfix page. */
		  pgbuf_unfix_and_init (thread_p, helper.home_page);
		}
	      /* Fall through and go to end. */
	    }
	  else
	    {
	      /* Finished vacuuming page. Unfix the page and go to end. */
	      pgbuf_unfix_and_init (thread_p, helper.home_page);
	    }
	  goto end;
	}

      if (pgbuf_has_any_non_vacuum_waiters (helper.home_page))
	{
	  /* release latch to favor other threads */
	  vacuum_heap_page_log_and_reset (thread_p, &helper, false, true);

	  helper.home_page =
	    pgbuf_fix (thread_p, &helper.home_vpid, OLD_PAGE,
		       PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
	  if (helper.home_page == NULL)
	    {
	      ASSERT_ERROR_AND_SET (error_code);
	      vacuum_er_log (VACUUM_ER_LOG_ERROR | VACUUM_ER_LOG_HEAP,
			     "VACUUM ERROR: Failed to fix page %d|%d.\n",
			     helper.home_vpid.volid, helper.home_vpid.pageid);
	      assert (helper.forward_page == NULL);
	      return error_code;
	    }
	  (void) pgbuf_check_page_ptype (thread_p, helper.home_page,
					 PAGE_HEAP);
	}
      /* Continue to next object. */
    }
  /* Finished processing all objects. */

end:
  assert (helper.forward_page == NULL);
  if (helper.home_page != NULL)
    {
      vacuum_heap_page_log_and_reset (thread_p, &helper, true, true);
    }

  return error_code;
}

/*
 * vacuum_heap_prepare_record () - Prepare all required information to vacuum
 *				   heap record. Possible requirements:
 *				   - Record type (always).
 *				   - Peeked record data: REC_HOME,
 *				     REC_RELOCATION
 *				   - Forward page: REC_BIGONE, REC_RELOCATION
 *				   - Forward OID: REC_BIGONE, REC_RELOCATION
 *				   - HFID: REC_BIGONE, REC_RELOCATION
 *				   - Overflow VFID: REC_BIGONE
 *				   - MVCC header: REC_HOME, REC_BIGONE,
 *				     REC_RELOCATION
 *
 * return	 : Error code.
 * thread_p (in) : Thread entry.
 * helper (in)	 : Vacuum heap helper.
 */
static int
vacuum_heap_prepare_record (THREAD_ENTRY * thread_p,
			    VACUUM_HEAP_HELPER * helper)
{
  SPAGE_SLOT *slotp;		/* Slot at helper->crt_slotid or NULL. */
  VPID forward_vpid;		/* Forward page VPID. */
  int error_code = NO_ERROR;	/* Error code. */
  PGBUF_LATCH_CONDITION fwd_condition;	/* Condition to latch forward page
					 * for REC_RELOCATION.
					 */

  /* Assert expected arguments. */
  assert (helper != NULL);
  assert (helper->home_page != NULL);
  assert (helper->forward_page == NULL);
  assert (helper->crt_slotid > 0);

retry_prepare:

  /* Get slot. */
  slotp = spage_get_slot (helper->home_page, helper->crt_slotid);
  if (slotp == NULL)
    {
      /* Slot must have been deleted. */
      helper->record_type = REC_MARKDELETED;
      return NO_ERROR;
    }
  helper->record_type = slotp->record_type;

  /* Get required pages and MVCC header in the three interesting cases:
   * 1. REC_RELOCATION.
   * 2. REC_BIGONE.
   * 3. REC_HOME.
   */
  switch (helper->record_type)
    {
    case REC_RELOCATION:
      /* Required info: forward page, forward OID, REC_NEWHOME record,
       *                MVCC header and HFID.
       */

      /* Before getting forward page, we need to get HFID. It must be known
       * to help deciding the order of fixing pages.
       */
      error_code = vacuum_heap_get_hfid (thread_p, helper);
      if (error_code != NO_ERROR)
	{
	  /* Debug should have hit assert. Release should give up. */
	  return error_code;
	}
      assert (!HFID_IS_NULL (&helper->hfid));

      /* Get forward OID. */
      helper->forward_recdes.data = (char *) helper->forward_links;
      helper->forward_recdes.area_size = sizeof (helper->forward_links);
      if (spage_get_record (helper->home_page, helper->crt_slotid,
			    &helper->forward_recdes, COPY) != S_SUCCESS)
	{
	  assert_release (false);
	  return ER_FAILED;
	}
      COPY_OID (&helper->forward_oid, &helper->forward_links[0]);

      /* Get forward page. */
      VPID_GET_FROM_OID (&forward_vpid, &helper->forward_links[0]);
      if (helper->forward_page != NULL)
	{
	  VPID crt_fwd_vpid = VPID_INITIALIZER;

	  pgbuf_get_vpid (helper->forward_page, &crt_fwd_vpid);
	  assert (!VPID_ISNULL (&crt_fwd_vpid));
	  if (!VPID_EQ (&crt_fwd_vpid, &forward_vpid))
	    {
	      /* Unfix current forward page. */
	      pgbuf_unfix_and_init (thread_p, helper->forward_page);
	    }
	}
      if (helper->forward_page == NULL)
	{
	  /* The condition used to fix forward page depends on its VPID and
	   * home page VPID. Unconditional latch can be used if the order is
	   * home before forward. If the order is forward before home, try
	   * conditional latch, and if it fails, fix pages in reversed order.
	   */
	  fwd_condition =
	    pgbuf_get_condition_for_ordered_fix (&forward_vpid,
						 &helper->home_vpid,
						 &helper->hfid);
	  helper->forward_page =
	    pgbuf_fix (thread_p, &forward_vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
		       fwd_condition);
	}
      if (helper->forward_page == NULL)
	{
	  /* Fix failed. */
	  if (fwd_condition == PGBUF_UNCONDITIONAL_LATCH)
	    {
	      /* Fix should have worked. */
	      ASSERT_ERROR_AND_SET (error_code);
	      vacuum_er_log (VACUUM_ER_LOG_ERROR | VACUUM_ER_LOG_HEAP,
			     "VACUUM ERROR: Failed to fix page (%d, %d).",
			     forward_vpid.volid, forward_vpid.pageid);
	      return error_code;
	    }
	  /* Conditional latch. Unfix home, and fix in reversed order. */

	  VACUUM_PERF_HEAP_TRACK_PREPARE (thread_p, helper);

	  /* Make sure all current changes on home are logged. */
	  vacuum_heap_page_log_and_reset (thread_p, helper, false, true);
	  assert (helper->home_page == NULL);

	  /* Fix pages in reversed order. */
	  /* Fix forward page. */
	  helper->forward_page =
	    pgbuf_fix (thread_p, &forward_vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
		       PGBUF_UNCONDITIONAL_LATCH);
	  if (helper->forward_page == NULL)
	    {
	      ASSERT_ERROR_AND_SET (error_code);
	      vacuum_er_log (VACUUM_ER_LOG_ERROR | VACUUM_ER_LOG_HEAP,
			     "VACUUM ERROR: Failed to fix page (%d, %d).",
			     forward_vpid.volid, forward_vpid.pageid);
	      return error_code;
	    }
	  /* Fix home page. */
	  helper->home_page =
	    pgbuf_fix (thread_p, &helper->home_vpid, OLD_PAGE,
		       PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
	  if (helper->home_page == NULL)
	    {
	      ASSERT_ERROR_AND_SET (error_code);
	      vacuum_er_log (VACUUM_ER_LOG_ERROR | VACUUM_ER_LOG_HEAP,
			     "VACUUM ERROR: Failed to fix page (%d, %d).",
			     forward_vpid.volid, forward_vpid.pageid);
	      return error_code;
	    }
	  /* Both pages fixed. */

	  /* While home has been unfixed, it is possible that current record
	   * was changed. It could be returned to home, link could be changed,
	   * or it could be vacuumed.
	   * Repeat getting record.
	   */
	  goto retry_prepare;
	}
      assert (VPID_EQ (pgbuf_get_vpid_ptr (helper->forward_page),
		       &forward_vpid));
      /* COPY (needed for UNDO logging) REC_NEWHOME record. */
      helper->record.data = PTR_ALIGN (helper->rec_buf, MAX_ALIGNMENT);
      helper->record.area_size = sizeof (helper->rec_buf);
      if (spage_get_record (helper->forward_page, helper->forward_oid.slotid,
			    &helper->record, COPY) != S_SUCCESS)
	{
	  assert_release (false);
	  return ER_FAILED;
	}
      /* Get MVCC header. */
      error_code = or_mvcc_get_header (&helper->record, &helper->mvcc_header);
      if (error_code != NO_ERROR)
	{
	  assert_release (false);
	  return error_code;
	}
      return NO_ERROR;

    case REC_BIGONE:
      /* Required info: forward oid, forward page, MVCC header, HFID and
       *                overflow VFID.
       */

      if (helper->forward_page != NULL)
	{
	  /* Retry from REC_RELOCATION. This forward_page cannot be good for
	   * REC_BIGONE.
	   */
	  pgbuf_unfix_and_init (thread_p, helper->forward_page);
	}

      /* HFID is required to obtain overflow VFID. */
      error_code = vacuum_heap_get_hfid (thread_p, helper);
      if (error_code != NO_ERROR)
	{
	  /* Debug should have hit assert. Release should give up. */
	  return error_code;
	}
      assert (!HFID_IS_NULL (&helper->hfid));

      /* Overflow VFID is required to remove overflow pages. */
      if (VFID_ISNULL (&helper->overflow_vfid))
	{
	  if (heap_ovf_find_vfid (thread_p, &helper->hfid,
				  &helper->overflow_vfid, false,
				  PGBUF_CONDITIONAL_LATCH) == NULL)
	    {
	      /* Failed conditional latch. Unfix heap page and try again using
	       * unconditional latch.
	       */
	      VACUUM_PERF_HEAP_TRACK_PREPARE (thread_p, helper);

	      vacuum_heap_page_log_and_reset (thread_p, helper, false, true);

	      if (heap_ovf_find_vfid (thread_p, &helper->hfid,
				      &helper->overflow_vfid, false,
				      PGBUF_UNCONDITIONAL_LATCH) == NULL
		  || VFID_ISNULL (&helper->overflow_vfid))
		{
		  assert_release (false);
		  return ER_FAILED;
		}
	      helper->home_page =
		pgbuf_fix (thread_p, &helper->home_vpid, OLD_PAGE,
			   PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
	      if (helper->home_page == NULL)
		{
		  ASSERT_ERROR_AND_SET (error_code);
		  vacuum_er_log (VACUUM_ER_LOG_ERROR | VACUUM_ER_LOG_HEAP,
				 "VACUUM ERROR: Failed to fix page (%d, %d).",
				 helper->home_vpid.volid,
				 helper->home_vpid.pageid);
		  return error_code;
		}
	      /* While home has been unfixed, it is possible that current
	       * record was changed. It could be vacuumed.
	       * Repeat getting record.
	       */
	      goto retry_prepare;
	    }
	}
      assert (!VFID_ISNULL (&helper->overflow_vfid));
      assert (helper->home_page != NULL);

      /* Get forward OID. */
      helper->forward_recdes.data = (char *) helper->forward_links;
      helper->forward_recdes.area_size = sizeof (helper->forward_links);
      if (spage_get_record (helper->home_page, helper->crt_slotid,
			    &helper->forward_recdes, COPY) != S_SUCCESS)
	{
	  assert_release (false);
	  return ER_FAILED;
	}

      COPY_OID (&helper->forward_oid, &helper->forward_links[0]);

      /* Fix first overflow page (forward_page). */
      VPID_GET_FROM_OID (&forward_vpid, &helper->forward_links[0]);
      helper->forward_page =
	pgbuf_fix (thread_p, &forward_vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
		   PGBUF_UNCONDITIONAL_LATCH);
      if (helper->forward_page == NULL)
	{
	  ASSERT_ERROR_AND_SET (error_code);
	  vacuum_er_log (VACUUM_ER_LOG_ERROR | VACUUM_ER_LOG_HEAP,
			 "VACUUM ERROR: Failed to fix page (%d, %d).",
			 forward_vpid.volid, forward_vpid.pageid);
	  return error_code;
	}

      /* Read MVCC header from first overflow page. */
      error_code =
	heap_get_mvcc_rec_header_from_overflow (helper->forward_page,
						&helper->mvcc_header, NULL);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  vacuum_er_log (VACUUM_ER_LOG_ERROR | VACUUM_ER_LOG_HEAP,
			 "VACUUM ERROR: Failed to get MVCC header from "
			 "overflow page %d|%d.",
			 forward_vpid.volid, forward_vpid.pageid);
	  return error_code;
	}
      break;

    case REC_HOME:
      /* Required info: record data and MVCC header. */

      if (helper->forward_page != NULL)
	{
	  /* Retry from REC_RELOCATION. This forward_page cannot be good for
	   * REC_HOME.
	   */
	  pgbuf_unfix_and_init (thread_p, helper->forward_page);
	}

      /* Peek record. */
      if (spage_get_record (helper->home_page, helper->crt_slotid,
			    &helper->record, PEEK) != S_SUCCESS)
	{
	  assert_release (false);
	  return ER_FAILED;
	}

      /* Get MVCC header. */
      error_code = or_mvcc_get_header (&helper->record, &helper->mvcc_header);
      if (error_code != NO_ERROR)
	{
	  assert_release (false);
	  return ER_FAILED;
	}
      break;

    default:
      /* No information is required other than record type. */

      if (helper->forward_page != NULL)
	{
	  /* Retry from REC_RELOCATION. This forward_page cannot be good for
	   * vacuumed/deleted slot.
	   */
	  pgbuf_unfix_and_init (thread_p, helper->forward_page);
	}
      break;
    }

  /* Assert forward page is fixed if and only if record type is either
   * REC_RELOCATION or REC_BIGONE.
   */
  assert ((helper->record_type == REC_RELOCATION
	   || helper->record_type == REC_BIGONE)
	  == (helper->forward_page != NULL));

  VACUUM_PERF_HEAP_TRACK_PREPARE (thread_p, helper);

  /* Success. */
  return NO_ERROR;
}

/*
 * vacuum_heap_record_insert_mvccid () - Remove insert MVCCID from record.
 *
 * return	 : Error code.
 * thread_p (in) : Thread entry.
 * helper (in)	 : Vacuum heap helper.
 */
static int
vacuum_heap_record_insert_mvccid (THREAD_ENTRY * thread_p,
				  VACUUM_HEAP_HELPER * helper)
{
  RECDES update_record;		/* Record to build updated version without
				 * insert MVCCID.
				 */
  /* Buffer for update_record data. */
  char update_record_buffer[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT];
  int error_code = NO_ERROR;	/* Error code. */

  /* Assert expected arguments. */
  assert (helper != NULL);
  assert (helper->can_vacuum == VACUUM_RECORD_DELETE_INSID);
  assert (MVCC_IS_HEADER_INSID_NOT_ALL_VISIBLE (&helper->mvcc_header));

  switch (helper->record_type)
    {
    case REC_RELOCATION:
      /* Remove insert MVCCID from REC_NEWHOME in forward_page. */

      /* Forward page and OID are required. */
      assert (helper->forward_page != NULL);
      assert (!OID_ISNULL (&helper->forward_oid));

      /* Clear flag for valid insert MVCCID and get new header size */
      MVCC_CLEAR_FLAG_BITS (&helper->mvcc_header, OR_MVCC_FLAG_VALID_INSID);

      /* Create updated record. */
      update_record.data = PTR_ALIGN (update_record_buffer, MAX_ALIGNMENT);
      update_record.area_size = DB_PAGESIZE;
      update_record.type = REC_NEWHOME;
      memcpy (update_record.data, helper->record.data, helper->record.length);
      update_record.length = helper->record.length;

      /* Modify header. It will move data inside record if required. */
      error_code = or_mvcc_set_header (&update_record, &helper->mvcc_header);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  vacuum_er_log (VACUUM_ER_LOG_ERROR | VACUUM_ER_LOG_HEAP,
			 "Vacuum error: set mvcc header "
			 "(flag=%d, repid=%d, chn=%d, insid=%llu, "
			 "delid=%llu, next_v=%d|%d|%d to object "
			 "%d|%d|%d with record of type=%d and "
			 "size=%d",
			 (int) MVCC_GET_FLAG (&helper->mvcc_header),
			 (int) MVCC_GET_REPID (&helper->mvcc_header),
			 MVCC_GET_CHN (&helper->mvcc_header),
			 MVCC_GET_INSID (&helper->mvcc_header),
			 MVCC_GET_DELID (&helper->mvcc_header),
			 helper->mvcc_header.next_version.volid,
			 helper->mvcc_header.next_version.pageid,
			 helper->mvcc_header.next_version.slotid,
			 helper->forward_oid.volid,
			 helper->forward_oid.pageid,
			 helper->forward_oid.slotid, REC_NEWHOME,
			 helper->record.length);
	  return error_code;
	}

      /* Update record in page. */
      if (spage_update (thread_p, helper->forward_page,
			helper->forward_oid.slotid, &update_record)
	  != SP_SUCCESS)
	{
	  assert_release (false);
	  return ER_FAILED;
	}

      /* Since forward page was vacuumed, log it immediately. Then unfix
       * forward page.
       */
      vacuum_log_vacuum_heap_page (thread_p, helper->forward_page, 1,
				   &helper->forward_oid.slotid,
				   &helper->can_vacuum, NULL, NULL,
				   helper->reusable, false);
      pgbuf_set_dirty (thread_p, helper->forward_page, FREE);
      helper->forward_page = NULL;

      mnt_heap_rel_vacuums (thread_p);
      break;

    case REC_BIGONE:
      /* First overflow page is required. */
      assert (helper->forward_page != NULL);

      /* Replace current insert MVCCID with MVCCID_ALL_VISIBLE. Header must
       * remain the same size.
       */
      MVCC_SET_INSID (&helper->mvcc_header, MVCCID_ALL_VISIBLE);
      error_code =
	heap_set_mvcc_rec_header_on_overflow (helper->forward_page,
					      &helper->mvcc_header);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  vacuum_er_log (VACUUM_ER_LOG_ERROR | VACUUM_ER_LOG_HEAP,
			 "Vacuum error: set mvcc header "
			 "(flag=%d, repid=%d, chn=%d, insid=%llu, "
			 "delid=%llu, next_v=%d|%d|%d to object "
			 "%d|%d|%d with record of type=%d and "
			 "size=%d",
			 (int) MVCC_GET_FLAG (&helper->mvcc_header),
			 (int) MVCC_GET_REPID (&helper->mvcc_header),
			 MVCC_GET_CHN (&helper->mvcc_header),
			 MVCC_GET_INSID (&helper->mvcc_header),
			 MVCC_GET_DELID (&helper->mvcc_header),
			 helper->mvcc_header.next_version.volid,
			 helper->mvcc_header.next_version.pageid,
			 helper->mvcc_header.next_version.slotid,
			 helper->home_vpid.volid, helper->home_vpid.pageid,
			 helper->crt_slotid, REC_BIGONE,
			 helper->record.length);
	  return error_code;
	}
      /* Log changes and unfix first overflow page. */
      vacuum_log_remove_ovf_insid (thread_p, helper->forward_page);
      pgbuf_set_dirty (thread_p, helper->forward_page, FREE);
      helper->forward_page = NULL;

      mnt_heap_big_vacuums (thread_p);
      break;

    case REC_HOME:
      /* Remove insert MVCCID */

      /* Clear valid insert MVCCID flag */
      MVCC_CLEAR_FLAG_BITS (&helper->mvcc_header, OR_MVCC_FLAG_VALID_INSID);

      /* Create updated record. */
      update_record.data = PTR_ALIGN (update_record_buffer, MAX_ALIGNMENT);
      update_record.area_size = DB_PAGESIZE;
      update_record.type = REC_HOME;
      memcpy (update_record.data, helper->record.data, helper->record.length);
      update_record.length = helper->record.length;

      /* Modify header. It will move data inside record if required. */
      error_code = or_mvcc_set_header (&update_record, &helper->mvcc_header);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  vacuum_er_log (VACUUM_ER_LOG_ERROR | VACUUM_ER_LOG_HEAP,
			 "Vacuum error: set mvcc header "
			 "(flag=%d, repid=%d, chn=%d, insid=%llu, "
			 "delid=%llu, next_v=%d|%d|%d to object "
			 "%d|%d|%d with record of type=%d and "
			 "size=%d",
			 (int) MVCC_GET_FLAG (&helper->mvcc_header),
			 (int) MVCC_GET_REPID (&helper->mvcc_header),
			 MVCC_GET_CHN (&helper->mvcc_header),
			 MVCC_GET_INSID (&helper->mvcc_header),
			 MVCC_GET_DELID (&helper->mvcc_header),
			 helper->mvcc_header.next_version.volid,
			 helper->mvcc_header.next_version.pageid,
			 helper->mvcc_header.next_version.slotid,
			 helper->home_vpid.volid, helper->home_vpid.pageid,
			 helper->crt_slotid, REC_HOME, helper->record.length);
	  return error_code;
	}
      if (spage_update (thread_p, helper->home_page, helper->crt_slotid,
			&update_record) != SP_SUCCESS)
	{
	  assert_release (false);
	  return ER_FAILED;
	}
      /* Collect vacuum data to be logged later. */
      helper->slots[helper->n_bulk_vacuumed] = helper->crt_slotid;
      helper->results[helper->n_bulk_vacuumed] = VACUUM_RECORD_DELETE_INSID;
      OID_SET_NULL (&helper->next_versions[helper->n_bulk_vacuumed]);
      OID_SET_NULL (&helper->partition_links[helper->n_bulk_vacuumed]);
      helper->n_bulk_vacuumed++;

      mnt_heap_home_vacuums (thread_p);
      break;

    default:
      /* Should not be here. */
      assert_release (false);
      return ER_FAILED;
    }

  helper->n_vacuumed++;

  mnt_heap_insid_vacuums (thread_p);

  /* Success. */
  return NO_ERROR;
}

/*
 * vacuum_heap_record () - Vacuum heap record.
 *
 * return	 : Error code.
 * thread_p (in) : Thread entry.
 * helper (in)	 : Vacuum heap helper.
 */
static int
vacuum_heap_record (THREAD_ENTRY * thread_p, VACUUM_HEAP_HELPER * helper)
{
  /* Assert expected arguments. */
  assert (helper != NULL);
  assert (helper->can_vacuum == VACUUM_RECORD_REMOVE);
  assert (helper->home_page != NULL);
  assert (MVCC_IS_HEADER_DELID_VALID (&helper->mvcc_header));

  if (helper->record_type == REC_RELOCATION
      || helper->record_type == REC_BIGONE)
    {
      /* HOME record of rel/big records are performed as a single operation:
       * flush all existing vacuumed slots before starting a system op for
       * current record */
      vacuum_heap_page_log_and_reset (thread_p, helper, false, false);

      if (log_start_system_op (thread_p) == NULL)
	{
	  assert_release (false);
	  return ER_FAILED;
	}
    }
  else
    {
      assert (helper->record_type == REC_HOME);
      /* Collect home page changes. */
      helper->slots[helper->n_bulk_vacuumed] = helper->crt_slotid;
      helper->results[helper->n_bulk_vacuumed] = VACUUM_RECORD_REMOVE;
      if (!helper->reusable)
	{
	  /* Save next version and partition links. */
	  COPY_OID (&helper->next_versions[helper->n_bulk_vacuumed],
		    &MVCC_GET_NEXT_VERSION (&helper->mvcc_header));
	  COPY_OID (&helper->partition_links[helper->n_bulk_vacuumed],
		    &MVCC_GET_PARTITION_OID (&helper->mvcc_header));
	}
      else
	{
	  /* Links not logged. */
	}
    }

  /* Vacuum REC_HOME/REC_RELOCATION/REC_BIGONE */
  if (spage_vacuum_slot (thread_p, helper->home_page, helper->crt_slotid,
			 &MVCC_GET_NEXT_VERSION (&helper->mvcc_header),
			 &MVCC_GET_PARTITION_OID (&helper->mvcc_header),
			 helper->reusable) != NO_ERROR)
    {
      if (helper->record_type == REC_RELOCATION
	  || helper->record_type == REC_BIGONE)
	{
	  log_end_system_op (thread_p, LOG_RESULT_TOPOP_ABORT);
	}
      return ER_FAILED;
    }

  if (helper->reusable
      || OID_ISNULL (&MVCC_GET_NEXT_VERSION (&helper->mvcc_header)))
    {
      mnt_heap_remove_vacuums (thread_p);
    }
  else
    {
      mnt_heap_next_ver_vacuums (thread_p);
    }

  if (helper->record_type != REC_HOME)
    {
      /* We try to keep the same amount of pgbuf_set_dirty and logged changes;
       * Changes on REC_HOME records are logged in bulk and page is set dirty
       * along with that log record
       */
      pgbuf_set_dirty (thread_p, helper->home_page, DONT_FREE);
    }

  switch (helper->record_type)
    {
    case REC_RELOCATION:
      /* Remove REC_NEWHOME. */
      assert (helper->forward_page != NULL);
      assert (!OID_ISNULL (&helper->forward_oid));
      assert (!HFID_IS_NULL (&helper->hfid));
      assert (!OID_ISNULL (&helper->forward_oid));

      VACUUM_PERF_HEAP_TRACK_EXECUTE (thread_p, helper);

      vacuum_log_redoundo_vacuum_record (thread_p, helper->home_page,
					 helper->crt_slotid,
					 &helper->forward_recdes,
					 &MVCC_GET_NEXT_VERSION (&helper->
								 mvcc_header),
					 &MVCC_GET_PARTITION_OID (&helper->
								  mvcc_header),
					 helper->reusable);

      VACUUM_PERF_HEAP_TRACK_LOGGING (thread_p, helper);

      if (spage_vacuum_slot (thread_p, helper->forward_page,
			     helper->forward_oid.slotid, NULL, NULL, true)
	  != NO_ERROR)
	{
	  log_end_system_op (thread_p, LOG_RESULT_TOPOP_ABORT);
	  return ER_FAILED;
	}

      VACUUM_PERF_HEAP_TRACK_EXECUTE (thread_p, helper);

      /* Log changes in forward page immediately. */
      vacuum_log_redoundo_vacuum_record (thread_p, helper->forward_page,
					 helper->forward_oid.slotid,
					 &helper->record, NULL, NULL, true);

      pgbuf_set_dirty (thread_p, helper->forward_page, FREE);
      helper->forward_page = NULL;

      log_end_system_op (thread_p, LOG_RESULT_TOPOP_COMMIT);

      VACUUM_PERF_HEAP_TRACK_LOGGING (thread_p, helper);

      mnt_heap_rel_vacuums (thread_p);
      break;

    case REC_BIGONE:
      assert (helper->forward_page != NULL);
      /* Overflow first page is required. */
      assert (!VFID_ISNULL (&helper->overflow_vfid));

      VACUUM_PERF_HEAP_TRACK_EXECUTE (thread_p, helper);

      vacuum_log_redoundo_vacuum_record (thread_p, helper->home_page,
					 helper->crt_slotid,
					 &helper->forward_recdes,
					 &MVCC_GET_NEXT_VERSION (&helper->
								 mvcc_header),
					 &MVCC_GET_PARTITION_OID (&helper->
								  mvcc_header),
					 helper->reusable);

      VACUUM_PERF_HEAP_TRACK_LOGGING (thread_p, helper);

      /* Unfix first overflow page. */
      pgbuf_unfix_and_init (thread_p, helper->forward_page);

      if (heap_ovf_delete (thread_p, &helper->hfid, &helper->forward_oid,
			   &helper->overflow_vfid) == NULL)
	{
	  /* Failed to delete. */
	  assert_release (false);
	  log_end_system_op (thread_p, LOG_RESULT_TOPOP_ABORT);
	  return ER_FAILED;
	}

      VACUUM_PERF_HEAP_TRACK_EXECUTE (thread_p, helper);

      log_end_system_op (thread_p, LOG_RESULT_TOPOP_COMMIT);

      VACUUM_PERF_HEAP_TRACK_LOGGING (thread_p, helper);

      mnt_heap_big_vacuums (thread_p);
      break;

    case REC_HOME:
      helper->n_bulk_vacuumed++;

      mnt_heap_home_vacuums (thread_p);
      break;

    default:
      /* Unexpected. */
      assert_release (false);
      return ER_FAILED;
    }

  helper->n_vacuumed++;

  assert (helper->forward_page == NULL);

  VACUUM_PERF_HEAP_TRACK_EXECUTE (thread_p, helper);

  return NO_ERROR;
}

/*
 * vacuum_heap_get_hfid () - Get heap file identifier.
 *
 * return	 : Error code.
 * thread_p (in) : Thread entry.
 * helper (in)	 : Vacuum heap helper.
 */
static int
vacuum_heap_get_hfid (THREAD_ENTRY * thread_p, VACUUM_HEAP_HELPER * helper)
{
  int error_code = NO_ERROR;	/* Error code. */
  OID class_oid = OID_INITIALIZER;	/* Class OID. */

  assert (helper != NULL);
  assert (helper->home_page != NULL);

  if (!HFID_IS_NULL (&helper->hfid))
    {
      /* HFID is already known. */
      return NO_ERROR;
    }

  /* Get class OID from heap page. */
  error_code =
    heap_get_class_oid_from_page (thread_p, helper->home_page, &class_oid);
  if (error_code != NO_ERROR)
    {
      vacuum_er_log (VACUUM_ER_LOG_ERROR | VACUUM_ER_LOG_HEAP,
		     "VACUUM WARNING: Failed to obtain class_oid from "
		     "heap page %d|%d.\n",
		     pgbuf_get_volume_id (helper->home_page),
		     pgbuf_get_page_id (helper->home_page));

      assert_release (false);
      return error_code;
    }
  assert (!OID_ISNULL (&class_oid));

  /* Get HFID for class OID. */
  error_code =
    heap_get_hfid_from_class_oid (thread_p, &class_oid, &helper->hfid);
  if (error_code != NO_ERROR)
    {
      vacuum_er_log (VACUUM_ER_LOG_ERROR | VACUUM_ER_LOG_HEAP,
		     "VACUUM ERROR: Failed to obtain heap file "
		     "identifier for class (%d, %d, %d)",
		     class_oid.volid, class_oid.pageid, class_oid.slotid);

      assert_release (false);
      return error_code;
    }

  /* Success. */
  return NO_ERROR;
}

/*
 * vacuum_heap_page_log_and_reset () - Logs the vacuumed slots from page and
 *				       reset page pointer and number of
 *				       vacuumed slots.
 *
 * return	 : Void.
 * thread_p (in) : Thread entry.
 * helper (in)	 : Vacuum heap helper.
 * update_best_space_stat (in)	 :
 * unlatch_page (in) :
 */
static void
vacuum_heap_page_log_and_reset (THREAD_ENTRY * thread_p,
				VACUUM_HEAP_HELPER * helper,
				bool update_best_space_stat,
				bool unlatch_page)
{
  assert (helper != NULL);
  assert (helper->home_page != NULL);

  if (helper->n_bulk_vacuumed == 0)
    {
      /* No logging is required. */
      if (unlatch_page == true)
	{
	  pgbuf_unfix_and_init (thread_p, helper->home_page);
	}
      return;
    }

  if (spage_need_compact (thread_p, helper->home_page) == true)
    {
      /* Compact page data */
      spage_compact (helper->home_page);
    }

  /* Update statistics only for home pages;
   * We assume that fwd pages (from relocated records) are home pages for other
   * OIDs and their statistics are updated in that context */
  if (update_best_space_stat == true && helper->initial_home_free_space != -1)
    {
      if (HFID_IS_NULL (&helper->hfid))
	{
	  int freespace;

	  freespace = spage_get_free_space_without_saving (thread_p,
							   helper->home_page,
							   NULL);
	  if (heap_should_try_update_stat (freespace,
					   helper->initial_home_free_space)
	      == true)
	    {
	      (void) vacuum_heap_get_hfid (thread_p, helper);
	    }
	}

      if (!HFID_IS_NULL (&helper->hfid))
	{
	  heap_stats_update (thread_p, helper->home_page, &helper->hfid,
			     helper->initial_home_free_space);
	}
    }

  VACUUM_PERF_HEAP_TRACK_EXECUTE (thread_p, helper);

  /* Log vacuumed slots */
  vacuum_log_vacuum_heap_page (thread_p, helper->home_page,
			       helper->n_bulk_vacuumed, helper->slots,
			       helper->results, helper->next_versions,
			       helper->partition_links,
			       helper->reusable, false);

  /* Mark page as dirty and unfix */
  pgbuf_set_dirty (thread_p, helper->home_page, DONT_FREE);
  if (unlatch_page == true)
    {
      pgbuf_unfix_and_init (thread_p, helper->home_page);
    }

  /* Reset the number of vacuumed slots */
  helper->n_bulk_vacuumed = 0;

  VACUUM_PERF_HEAP_TRACK_LOGGING (thread_p, helper);
}


/*
 * vacuum_log_vacuum_heap_page () - Log removing OID's from heap page.
 *
 * return	      : Error code.
 * thread_p (in)      : Thread entry.
 * page_p (in)	      : Page pointer.
 * n_slots (in)	      : OID count in slots.
 * slots (in/out)     : Array of slots removed from heap page.
 * results (in)	      : Satisfies vacuum result.
 * next_versions (in) : OID of next versions.
 * partition_oids (in): partition OIDs.
 * reusable (in)      :
 *
 * NOTE: Some values in slots array are modified and set to negative values.
 */
static void
vacuum_log_vacuum_heap_page (THREAD_ENTRY * thread_p, PAGE_PTR page_p,
			     int n_slots, PGSLOTID * slots,
			     MVCC_SATISFIES_VACUUM_RESULT * results,
			     OID * next_versions, OID * partition_oids,
			     bool reusable, bool all_vacuumed)
{
  LOG_DATA_ADDR addr;
  int packed_size = 0, i = 0;
  char *ptr = NULL, *buffer_p = NULL;
  char buffer[MAX_SLOTS_IN_PAGE * (sizeof (PGSLOTID) + 2 * OR_OID_SIZE)
	      + (MAX_ALIGNMENT * 2)];
  bool has_next_versions = false;
  bool has_partition_oids = false;

  assert (n_slots >= 0 && n_slots <= ((SPAGE_HEADER *) page_p)->num_slots);
  assert (n_slots > 0 || all_vacuumed);

  /* Initialize log data. */
  addr.offset = n_slots;	/* Save number of slots in offset. */
  addr.pgptr = page_p;
  addr.vfid = NULL;

  /* Compute recovery data size */

  /* slots & results */
  packed_size += n_slots * sizeof (PGSLOTID);

  if (reusable)
    {
      addr.offset |= VACUUM_LOG_VACUUM_HEAP_REUSABLE;
    }
  else
    {
      if (next_versions != NULL)
	{
	  /* next versions */
	  /* is there any non-null next version? */
	  for (i = 0; i < n_slots; i++)
	    {
	      if (!OID_ISNULL (&next_versions[i]))
		{
		  has_next_versions = true;
		  break;
		}
	    }
	  if (has_next_versions)
	    {
	      /* Found non-null next version. */
	      packed_size = DB_ALIGN (packed_size, OR_OID_SIZE);
	      packed_size += n_slots * OR_OID_SIZE;
	    }

	  if (partition_oids != NULL)
	    {
	      /* next version partition oids */
	      for (i = 0; i < n_slots; i++)
		{
		  if (!OID_ISNULL (&partition_oids[i]))
		    {
		      has_partition_oids = true;
		      break;
		    }
		}
	      if (has_partition_oids)
		{
		  /* Found non-null partition oids. */
		  packed_size += n_slots * OR_OID_SIZE;
		}
	    }
	}
    }
  if (all_vacuumed)
    {
      addr.offset |= VACUUM_LOG_VACUUM_HEAP_ALL_VACUUMED;
    }

  assert (packed_size <= (int) sizeof (buffer));

  buffer_p = PTR_ALIGN (buffer, MAX_ALIGNMENT);
  ptr = buffer_p;

  if (n_slots > 0)
    {
      /* Pack slot ID's and results */
      for (i = 0; i < n_slots; i++)
	{
	  assert (results[i] == VACUUM_RECORD_DELETE_INSID
		  || results[i] == VACUUM_RECORD_REMOVE);

	  assert (slots[i] > 0);

	  if (results[i] == VACUUM_RECORD_REMOVE)
	    {
	      /* Use negative slot ID to mark that object has been completely
	       * removed.
	       */
	      slots[i] = -slots[i];
	    }
	}
      memcpy (ptr, slots, n_slots * sizeof (PGSLOTID));
      ptr += n_slots * sizeof (PGSLOTID);
    }

  if (has_next_versions)
    {
      /* Pack next versions */
      ptr = PTR_ALIGN (ptr, OR_OID_SIZE);
      memcpy (ptr, next_versions, n_slots * OR_OID_SIZE);

      ptr += n_slots * OR_OID_SIZE;

      if (has_partition_oids)
	{
	  /* Pack partition OIDs */
	  memcpy (ptr, partition_oids, n_slots * OR_OID_SIZE);

	  ptr += n_slots * OR_OID_SIZE;
	}
    }

  assert ((ptr - buffer_p) == packed_size);

  /* Append new redo log rebuild_record */
  log_append_redo_data (thread_p, RVVAC_HEAP_PAGE_VACUUM, &addr, packed_size,
			buffer_p);
}

/*
 * vacuum_rv_redo_vacuum_heap_page () - Redo vacuum remove oids from heap page.
 *
 * return	 : Error code.
 * thread_p (in) : Thread entry.
 * rcv (in)	 : Recovery structure.
 */
int
vacuum_rv_redo_vacuum_heap_page (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  int offset = 0, i = 0;
  INT16 n_slots;
  PGSLOTID *slotids = NULL;
  PAGE_PTR page_p = NULL;
  MVCC_SATISFIES_VACUUM_RESULT *results = NULL;
  OID *next_versions = NULL;
  OID *partition_oids = NULL;
  RECDES rebuild_record, peek_record;
  int old_header_size, new_header_size;
  MVCC_REC_HEADER rec_header;
  char *ptr = NULL;
  char data_buf[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT];
  bool reusable;
  bool all_vacuumed;

  page_p = rcv->pgptr;

  ptr = (char *) rcv->data;

  /* Get n_slots and flags */
  n_slots = (rcv->offset & (~VACUUM_LOG_VACUUM_HEAP_MASK));
  reusable = (rcv->offset & VACUUM_LOG_VACUUM_HEAP_REUSABLE) != 0;
  all_vacuumed = (rcv->offset & VACUUM_LOG_VACUUM_HEAP_ALL_VACUUMED) != 0;

  assert (n_slots < ((SPAGE_HEADER *) page_p)->num_slots);

  if (all_vacuumed)
    {
      vacuum_er_log (VACUUM_ER_LOG_HEAP | VACUUM_ER_LOG_RECOVERY,
		     "VACUUM: Change vacuum status for heap page %d|%d, "
		     "lsa=%lld|%d, from once to none.\n",
		     pgbuf_get_volume_id (rcv->pgptr),
		     pgbuf_get_page_id (rcv->pgptr),
		     (long long int) pgbuf_get_lsa (rcv->pgptr)->pageid,
		     (int) pgbuf_get_lsa (rcv->pgptr)->offset);
    }

  if (n_slots == 0)
    {
      /* No slots have been vacuumed, but header must be changed from one
       * vacuum required to no vacuum required.
       */
      assert (all_vacuumed);

      if (all_vacuumed)
	{
	  heap_page_set_vacuum_status_none (thread_p, rcv->pgptr);
	}

      pgbuf_set_dirty (thread_p, page_p, DONT_FREE);

      return NO_ERROR;
    }

  /* Get slot ID's and result types */
  slotids = (PGSLOTID *) ptr;
  ptr += n_slots * sizeof (PGSLOTID);

  if (ptr < rcv->data + rcv->length)
    {
      /* Next versions must be obtained */
      assert (!reusable);
      ptr = PTR_ALIGN (ptr, OR_OID_SIZE);
      next_versions = (OID *) ptr;

      ptr += n_slots * OR_OID_SIZE;

      if (ptr < rcv->data + rcv->length)
	{
	  /* partition OIDs must be obtained */
	  partition_oids = (OID *) ptr;

	  ptr += n_slots * OR_OID_SIZE;
	}
    }

  /* Safeguard for correct unpacking of recovery data */
  assert (ptr == rcv->data + rcv->length);

  /* Initialize rebuild_record for deleting INSERT MVCCID's */
  rebuild_record.area_size = IO_MAX_PAGE_SIZE;
  rebuild_record.data = PTR_ALIGN (data_buf, MAX_ALIGNMENT);

  /* Vacuum slots */
  for (i = 0; i < n_slots; i++)
    {
      if (slotids[i] < 0)
	{
	  /* Record was removed completely */
	  slotids[i] = -slotids[i];
	  if (spage_vacuum_slot (thread_p, page_p, slotids[i],
				 next_versions != NULL ?
				 &next_versions[i] : NULL,
				 partition_oids != NULL ?
				 &partition_oids[i] : NULL, reusable)
	      != NO_ERROR)
	    {
	      assert_release (false);
	      return ER_FAILED;
	    }
	}
      else
	{
	  /* Only insert MVCCID has been removed */
	  if (spage_get_record (rcv->pgptr, slotids[i], &peek_record, PEEK)
	      != S_SUCCESS)
	    {
	      vacuum_er_log (VACUUM_ER_LOG_ERROR | VACUUM_ER_LOG_HEAP
			     | VACUUM_ER_LOG_RECOVERY,
			     "VACUUM ERROR: Failed to get record at "
			     "(%d, %d, %d).",
			     pgbuf_get_volume_id (rcv->pgptr),
			     pgbuf_get_page_id (rcv->pgptr), slotids[i]);
	      assert_release (false);
	      return ER_FAILED;
	    }

	  if (peek_record.type != REC_HOME && peek_record.type != REC_NEWHOME)
	    {
	      /* Unexpected */
	      assert_release (false);
	      return ER_FAILED;
	    }

	  /* Remove insert MVCCID */
	  or_mvcc_get_header (&peek_record, &rec_header);
	  old_header_size =
	    or_mvcc_header_size_from_flags (MVCC_GET_FLAG (&rec_header));
	  MVCC_CLEAR_FLAG_BITS (&rec_header, OR_MVCC_FLAG_VALID_INSID);
	  new_header_size =
	    or_mvcc_header_size_from_flags (MVCC_GET_FLAG (&rec_header));

	  /* Rebuild record */
	  rebuild_record.type = peek_record.type;
	  rebuild_record.length = peek_record.length;
	  memcpy (rebuild_record.data, peek_record.data, peek_record.length);

	  /* Set new header */
	  or_mvcc_set_header (&rebuild_record, &rec_header);
	  /* Copy record data */
	  memcpy (rebuild_record.data + new_header_size,
		  peek_record.data + old_header_size,
		  peek_record.length - old_header_size);

	  if (spage_update (thread_p, rcv->pgptr, slotids[i], &rebuild_record)
	      != SP_SUCCESS)
	    {
	      assert_release (false);
	      return ER_FAILED;
	    }
	}
    }

  if (spage_need_compact (thread_p, rcv->pgptr) == true)
    {
      (void) spage_compact (rcv->pgptr);
    }

  if (all_vacuumed)
    {
      heap_page_set_vacuum_status_none (thread_p, rcv->pgptr);
    }

  pgbuf_set_dirty (thread_p, page_p, DONT_FREE);

  return NO_ERROR;
}

/*
 * vacuum_log_remove_ovf_insid () - Log removing insert MVCCID from big record.
 *
 * return	 : Void.
 * thread_p (in) : Thread entry.
 * ovfpage (in)  : Big record first overflow page.
 */
static void
vacuum_log_remove_ovf_insid (THREAD_ENTRY * thread_p, PAGE_PTR ovfpage)
{
  log_append_redo_data2 (thread_p, RVVAC_REMOVE_OVF_INSID, NULL, ovfpage, 0,
			 0, NULL);
}

/*
 * vacuum_rv_redo_remove_ovf_insid () - Redo removing insert MVCCID from big
 *					record.
 *
 * return	 : Error code.
 * thread_p (in) : Thread entry.
 * rcv (in)	 : Recovery data.
 */
int
vacuum_rv_redo_remove_ovf_insid (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  MVCC_REC_HEADER rec_header;
  int error = NO_ERROR;

  error = heap_get_mvcc_rec_header_from_overflow (rcv->pgptr, &rec_header,
						  NULL);
  if (error != NO_ERROR)
    {
      return error;
    }

  MVCC_SET_INSID (&rec_header, MVCCID_ALL_VISIBLE);

  error = heap_set_mvcc_rec_header_on_overflow (rcv->pgptr, &rec_header);
  if (error != NO_ERROR)
    {
      return error;
    }

  pgbuf_set_dirty (thread_p, rcv->pgptr, DONT_FREE);

  return NO_ERROR;
}

/*
 * vacuum_produce_log_block_data () - After logging a block of log data,
 *				      useful information for vacuum is passed
 *				      by log manager and should be saved in
 *				      lock-free buffer.
 *
 * return	      : Void.
 * thread_p (in)      : Thread entry.
 * start_lsa (in)     : Log block starting LSA.
 * oldest_mvccid (in) : Log block oldest MVCCID.
 * newest_mvccid (in) : Log block newest MVCCID.
 */
void
vacuum_produce_log_block_data (THREAD_ENTRY * thread_p, LOG_LSA * start_lsa,
			       MVCCID oldest_mvccid, MVCCID newest_mvccid)
{
  VACUUM_DATA_ENTRY block_data;

  if (prm_get_bool_value (PRM_ID_DISABLE_VACUUM))
    {
      return;
    }

  if (vacuum_Block_data_buffer == NULL)
    {
      /* TODO: Right now, the vacuum is not working when a database is
       *       created, which means we will "leak" some MVCC operations.
       * There are two possible solutions:
       * 1. Initialize vacuum just to collect information on MVCC operations
       *    done while creating the database.
       * 2. Disable MVCC operation while creating database. No concurrency,
       *    no MVCC is required.
       * Option #2 is best, however the dynamic MVCC headers for heap records
       * are required. Postpone solution until then, and just set a warning
       * here.
       * Update:
       * Alex is going disable MVCC when the server will work in stand-alone
       * mode with the implementation for Dynamic MVCC header for heap.
       */
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return;
    }

  /* Set blockid */
  block_data.blockid = vacuum_get_log_blockid (start_lsa->pageid);

  /* Check the blockid data is not corrupted */
  assert (block_data.blockid >= 0);
  assert (MVCCID_IS_VALID (oldest_mvccid));
  assert (MVCCID_IS_VALID (newest_mvccid));
  assert (!MVCC_ID_PRECEDES (newest_mvccid, oldest_mvccid));

  /* Set start lsa for block */
  LSA_COPY (&block_data.start_lsa, start_lsa);
  /* Set oldest and newest MVCCID */
  block_data.oldest_mvccid = oldest_mvccid;
  block_data.newest_mvccid = newest_mvccid;

  vacuum_er_log (VACUUM_ER_LOG_LOGGING | VACUUM_ER_LOG_VACUUM_DATA,
		 "VACUUM, thread %d calls: "
		 "vacuum_produce_log_block_data: blockid=(%lld) "
		 "start_lsa=(%lld, %d) old_mvccid=(%llu) new_mvccid=(%llu)\n",
		 thread_get_current_entry_index (),
		 block_data.blockid,
		 (long long int) block_data.start_lsa.pageid,
		 (int) block_data.start_lsa.offset, block_data.oldest_mvccid,
		 block_data.newest_mvccid);

  /* Push new block into block data buffer */
  if (!lf_circular_queue_produce (vacuum_Block_data_buffer, &block_data))
    {
      /* Push failed, the buffer must be full */
      /* TODO: Set a new message error for full block data buffer */
      /* TODO: Probably this case should be avoided... Make sure that we
       *       do not lose vacuum data so there has to be enough space to
       *       keep it.
       */
      vacuum_er_log (VACUUM_ER_LOG_ERROR,
		     "VACUUM_ERROR: Cannot produce new log block data! "
		     "The buffer is already full.");
      assert (false);
      return;
    }

  mnt_vac_log_to_vacuum_pages (thread_p, vacuum_Data->log_block_npages);
}

#if defined (SERVER_MODE)
/*
 * vacuum_master_start () - Base function for auto vacuum routine. It will
 *			  process vacuum data and assign vacuum jobs to
 *			  workers. Each job will process a block of log data
 *			  and vacuum records from b-trees and heap files.
 *
 * return : Void.
 *
 * TODO: Current vacuum algorithm cannot handle REC_MVCC_NEXT_VERSION slot
 *	 types. We may need to add a new routine that will only vacuum
 *	 classes with referable objects. Currently we can only do this by
 *	 scanning the entire heap file and update REC_MVCC_NEXT_VERSION slots.
 *	 It should be called rarely and may require keeping some statistics.
 */
void
vacuum_master_start (void)
{
  THREAD_ENTRY *thread_p = thread_get_thread_entry_info ();

  /* Start a new vacuum iteration that processes log to create vacuum jobs */
  vacuum_process_vacuum_data (thread_p);
}
#endif /* SERVER_MODE */

/*
 * vacuum_process_vacuum_data () - Start a new vacuum iteration that processes
 *				   vacuum data and identifies blocks candidate
 *				   to assign as jobs for vacuum workers.
 *
 * return	 : Void.
 * thread_p (in) : Thread entry.
 */
static void
vacuum_process_vacuum_data (THREAD_ENTRY * thread_p)
{
  int i;
  VACUUM_DATA_ENTRY *entry = NULL;
  int vacuum_entries;
  MVCCID local_oldest_active_mvccid;
#if defined (SERVER_MODE)
  VACUUM_JOB_ENTRY vacuum_job_entry;
  int n_wakeup_workers;
  int n_available_workers;
  int n_jobs;
  int new_created_jobs;
  INT64 vacuum_block_data_buffer_aprox_size;
#else	/* !SERVER_MODE */		   /* SA_MODE */
  VACUUM_DATA_ENTRY vacuum_data_entry;
  bool save_check_interrupt;
#endif /* SA_MODE */

  PERF_UTIME_TRACKER perf_tracker;

#if defined (SA_MODE)
  er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE,
	  ER_STAND_ALONE_VACUUM_START, 0);
  er_log_debug (ARG_FILE_LINE, "Stand-alone vacuum start.\n");
#endif

  PERF_UTIME_TRACKER_START (thread_p, &perf_tracker);

  if (ATOMIC_INC_32 (&vacuum_Global_oldest_active_blockers_counter, 0) == 0)
    {
      local_oldest_active_mvccid = logtb_get_oldest_active_mvccid (thread_p);

      /* check again, maybe concurrent thread has modified the counter value */
      if (ATOMIC_INC_32 (&vacuum_Global_oldest_active_blockers_counter, 0)
	  == 0)
	{
	  ATOMIC_TAS_64 (&vacuum_Global_oldest_active_mvccid,
			 local_oldest_active_mvccid);
	}
    }

  if (vacuum_Data == NULL
      || (vacuum_Data->n_table_entries <= 0
	  && LOCK_FREE_CIRCULAR_QUEUE_IS_EMPTY (vacuum_Block_data_buffer)
	  && LSA_ISNULL (&vacuum_Data->first_blocks_to_recover)))
    {
      /* Vacuum data was not loaded yet from disk or it doesn't have any
       * entries.
       */
#if defined (SA_MODE)
      goto finish_sa_mode;
#else	/* !SA_MODE */	       /* SERVER_MODE */

      PERF_UTIME_TRACKER_TIME (thread_p, &perf_tracker, mnt_vac_master_time);
      return;
#endif /* SERVER_MODE */
    }

  if (prm_get_bool_value (PRM_ID_DISABLE_VACUUM))
    {
      return;
    }

#if defined (SA_MODE)
  /* Restart if we have to recover blocks from log. */
restart:
#endif

  /* Lock vacuum data */
  VACUUM_LOCK_DATA ();

  /* Remove vacuumed entries */
  vacuum_data_remove_finished_entries (thread_p);

  /* Append newly logged blocks at the end of the vacuum data table */
  (void) vacuum_consume_buffer_log_blocks (thread_p);

  /* Update oldest MVCCID */
  vacuum_update_oldest_mvccid (thread_p);

  vacuum_entries = vacuum_Data->n_table_entries;

  /* Unlock data */
  VACUUM_UNLOCK_DATA ();

  /* Search for blocks ready to be vacuumed */
  /* Vacuum mutex is not hold here: but we search at most n_table_entries as it
   * was during mutex hold; vacuum master is the only thread which
   * removes vacuum entries, adding entries is performed only at the end of
   * vacuum data array;
   * For job queue, we use a copy of entry data, since after compacting vacuum
   * data array, pointers would be invalidated */
#if defined(SERVER_MODE)
  for (i = 0, new_created_jobs = 0;
       i < vacuum_entries
       && LOCK_FREE_CIRCULAR_QUEUE_APPROX_SIZE (vacuum_Job_queue)
       < VACUUM_JOB_QUEUE_CAPACITY - VACUUM_JOB_QUEUE_SAFETY_BUFFER; i++)
#else
  for (i = 0; i < vacuum_entries; i++)
#endif
    {
      int error_code;
      entry = VACUUM_DATA_GET_ENTRY (i);
      if (!VACUUM_LOG_BLOCK_CAN_VACUUM (entry,
					vacuum_Global_oldest_active_mvccid))
	{
	  /* This block cannot be vacuumed */
	  continue;
	}

      /* Flag block as being in progress in order to avoid adding the job again
       * on next iteration.
       */
      VACUUM_BLOCK_STATUS_SET_IN_PROGRESS (entry);
      if (!VACUUM_BLOCK_IS_INTERRUPTED (entry))
	{
	  /* Log that a new job is starting. After recovery, the system will
	   * then know this job was partially executed. Logging the start of a
	   * job already interrupted is not necessary.
	   * We do it here rather than when vacuum job is really started to
	   * avoid locking vacuum data again (logging vacuum data cannot be
	   * done without locking).
	   */
	  log_append_redo_data2 (thread_p, RVVAC_START_OR_END_JOB, NULL, NULL,
				 0, sizeof (entry->blockid), &entry->blockid);
	}

#if defined (SERVER_MODE)
      vacuum_job_entry.vacuum_data_entry = *entry;

      if (vacuum_Prefetch_log_mode == VACUUM_PREFETCH_LOG_MODE_MASTER)
	{
	  error_code =
	    vacuum_log_prefetch_vacuum_block (thread_p, entry,
					      &vacuum_job_entry.
					      block_log_buffer);
	  if (error_code != NO_ERROR)
	    {
	      return;
	    }
	}
      else
	{
	  VACUUM_INIT_PREFETCH_BLOCK (&vacuum_job_entry.block_log_buffer);
	}

      if (!lf_circular_queue_produce (vacuum_Job_queue, &vacuum_job_entry))
	{
	  /* Queue is full, abort creating new jobs. */
	  vacuum_er_log (VACUUM_ER_LOG_WARNING | VACUUM_ER_LOG_MASTER,
			 "VACUUM ERROR: Could not push new job.");

	  PERF_UTIME_TRACKER_TIME (thread_p, &perf_tracker,
				   mnt_vac_master_time);
	  return;
	}

      new_created_jobs++;

      /* Wakeup threads to start working on current threads. Try not to wake up
       * more workers than necessary.
       */
      n_jobs = (int) LOCK_FREE_CIRCULAR_QUEUE_APPROX_SIZE (vacuum_Job_queue);
      n_available_workers =
	prm_get_integer_value (PRM_ID_VACUUM_WORKER_COUNT)
	- vacuum_Running_workers_count;
      n_wakeup_workers = MIN (n_jobs, n_available_workers);
      if (n_wakeup_workers > 0)
	{
	  /* Wakeup more workers */
	  thread_wakeup_vacuum_worker_threads (n_wakeup_workers);
	}

      vacuum_block_data_buffer_aprox_size =
	LOCK_FREE_CIRCULAR_QUEUE_APPROX_SIZE (vacuum_Block_data_buffer);
      if (vacuum_Prefetch_log_mode == VACUUM_PREFETCH_LOG_MODE_MASTER
	  && ((new_created_jobs >
	       prm_get_integer_value (PRM_ID_VACUUM_WORKER_COUNT)
	       && vacuum_block_data_buffer_aprox_size
	       > vacuum_Block_data_buffer->capacity / 2)
	      || (new_created_jobs >= 1
		  && vacuum_block_data_buffer_aprox_size
		  > 9 * vacuum_Block_data_buffer->capacity / 10)))
	{
	  vacuum_er_log (VACUUM_ER_LOG_MASTER,
			 "VACUUM abort job creation : "
			 "vacuum_Block_data_buffer size:%lld/%lld",
			 vacuum_block_data_buffer_aprox_size,
			 vacuum_Block_data_buffer->capacity);

	  PERF_UTIME_TRACKER_TIME (thread_p, &perf_tracker,
				   mnt_vac_master_time);
	  return;
	}
#else
      PERF_UTIME_TRACKER_TIME (thread_p, &perf_tracker, mnt_vac_master_time);

      vacuum_data_entry = *entry;
      error_code =
	vacuum_process_log_block (thread_p, &vacuum_data_entry, NULL, false);
      assert (error_code == NO_ERROR);

      er_log_debug (ARG_FILE_LINE,
		    "Stand-alone vacuum finished block %lld.\n",
		    (long long int)
		    VACUUM_DATA_ENTRY_BLOCKID (&vacuum_data_entry));

      PERF_UTIME_TRACKER_START (thread_p, &perf_tracker);
#endif
    }

#if defined (SA_MODE)
  if (!LSA_ISNULL (&vacuum_Data->last_blocks_to_recover))
    {
      /* Not all vacuum data was processed. We must recover blocks from log
       * and execute vacuum again. It is unlikely (and extremely unwanted)
       * to reach this, but it is important to handle it.
       */
      goto restart;
    }

  assert (LOCK_FREE_CIRCULAR_QUEUE_IS_EMPTY (vacuum_Block_data_buffer));

  /* Remove vacuumed entries */
  vacuum_data_remove_finished_entries (thread_p);
  /* Update oldest MVCCID */
  vacuum_update_oldest_mvccid (thread_p);

  assert (vacuum_Data->n_table_entries == 0);

finish_sa_mode:

  /* We don't want to interrupt next operation. */
  save_check_interrupt = thread_set_check_interrupt (thread_p, false);

  /* Can we generate another block on information cached in log header? */
  if (vacuum_get_log_blockid (log_Gl.hdr.mvcc_op_log_lsa.pageid)
      > vacuum_Data->last_blockid)
    {
      /* Execute vacuum based on the block not generated yet. */

      /* Create vacuum data entry for the job. */
      vacuum_data_entry.blockid =
	vacuum_get_log_blockid (log_Gl.hdr.mvcc_op_log_lsa.pageid);
      LSA_COPY (&vacuum_data_entry.start_lsa, &log_Gl.hdr.mvcc_op_log_lsa);
      vacuum_data_entry.oldest_mvccid = log_Gl.hdr.last_block_oldest_mvccid;
      vacuum_data_entry.newest_mvccid = log_Gl.hdr.last_block_newest_mvccid;
      VACUUM_BLOCK_STATUS_SET_IN_PROGRESS (&vacuum_data_entry);

      /* Update vacuum_Data as if it would contain only this entry.
       * Do not update last_blockid since it may be generated again.
       */
      vacuum_Data->oldest_mvccid = vacuum_data_entry.oldest_mvccid;
      vacuum_Data->newest_mvccid = vacuum_data_entry.newest_mvccid;
      vacuum_Data->n_table_entries = 1;
      vacuum_Data->vacuum_data_table[0] = vacuum_data_entry;

      /* We do not log here. We could have a problem if the server crashes,
       * because same vacuum tasks may be re-executed. The worst that can
       * happen is to hit an assert in debug mode.
       * Instead of doing a voodoo fix here, it is better to live with the
       * bug.
       */

      PERF_UTIME_TRACKER_TIME (thread_p, &perf_tracker, mnt_vac_master_time);

      /* Execute vacuum. */
      (void) vacuum_process_log_block (thread_p, &vacuum_data_entry, NULL,
				       true);

      PERF_UTIME_TRACKER_START (thread_p, &perf_tracker);
    }

  (void) thread_set_check_interrupt (thread_p, save_check_interrupt);

  /* All vacuum complete. */
  vacuum_Data->n_table_entries = 0;
  vacuum_Data->oldest_mvccid = log_Gl.hdr.mvcc_next_id;
  vacuum_Data->newest_mvccid = log_Gl.hdr.mvcc_next_id;
  assert (vacuum_Data->n_blocks_to_recover == 0);
  assert (LSA_ISNULL (&vacuum_Data->first_blocks_to_recover));

  log_append_redo_data2 (thread_p, RVVAC_COMPLETE, NULL, NULL, 0,
			 sizeof (log_Gl.hdr.mvcc_next_id),
			 &log_Gl.hdr.mvcc_next_id);
  logpb_flush_pages_direct (thread_p);

  /* Cleanup dropped files. */
  vacuum_cleanup_dropped_files (thread_p);

  /* Reset log header information saved for vacuum. */
  LSA_SET_NULL (&log_Gl.hdr.mvcc_op_log_lsa);
  log_Gl.hdr.last_block_oldest_mvccid = MVCCID_NULL;
  log_Gl.hdr.last_block_newest_mvccid = MVCCID_NULL;

  er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE,
	  ER_STAND_ALONE_VACUUM_END, 0);
  er_log_debug (ARG_FILE_LINE, "Stand-alone vacuum end.\n");

  PERF_UTIME_TRACKER_TIME (thread_p, &perf_tracker, mnt_vac_master_time);
#endif
}

/*
 * vacuum_rv_redo_vacuum_complete () - Redo recovery of vacuum complete.
 *
 * return	 : NO_ERROR.
 * thread_p (in) : Thread entry.
 * rcv (in)	 : Recovery data.
 */
int
vacuum_rv_redo_vacuum_complete (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  MVCCID oldest_newest_mvccid = MVCCID_NULL;

  assert (rcv->data != NULL && rcv->length == sizeof (MVCCID));

  oldest_newest_mvccid = *((MVCCID *) rcv->data);

  /* All vacuum complete. */
  vacuum_Data->n_table_entries = 0;
  vacuum_Data->oldest_mvccid = oldest_newest_mvccid;
  vacuum_Data->newest_mvccid = oldest_newest_mvccid;
  assert (vacuum_Data->n_blocks_to_recover == 0);
  assert (LSA_ISNULL (&vacuum_Data->first_blocks_to_recover));

  /* Reset log header information saved for vacuum. */
  LSA_SET_NULL (&log_Gl.hdr.mvcc_op_log_lsa);
  log_Gl.hdr.last_block_oldest_mvccid = MVCCID_NULL;
  log_Gl.hdr.last_block_newest_mvccid = MVCCID_NULL;

  return NO_ERROR;
}

/*
 * vacuum_process_log_block () - Vacuum heap and b-tree entries using log
 *				 information found in a block of pages.
 *
 * return		      : Error code.
 * thread_p (in)	      : Thread entry.
 * data (in)		      : Block data.
 * block_log_buffer (in)      : Block log page buffer identifier
 * sa_mode_partial_block (in) : True when SA_MODE vacuum based on partial
 *				block information from log header.
 *				Logging is skipped if true.
 */
static int
vacuum_process_log_block (THREAD_ENTRY * thread_p, VACUUM_DATA_ENTRY * data,
			  BLOCK_LOG_BUFFER * block_log_buffer,
			  bool sa_mode_partial_block)
{
  VACUUM_WORKER *worker = VACUUM_GET_VACUUM_WORKER (thread_p);
  LOG_LSA log_lsa;
  LOG_LSA rcv_lsa;
  LOG_PAGEID first_block_pageid =
    VACUUM_FIRST_LOG_PAGEID_IN_BLOCK (VACUUM_DATA_ENTRY_BLOCKID (data));
  int error_code = NO_ERROR;
  struct log_data log_record_data;
  char *undo_data_buffer = NULL, *undo_data = NULL;
  int undo_data_size;
  char *es_uri = NULL;
  char log_pgbuf[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT];
  LOG_PAGE *log_page_p = NULL;
  BTID_INT btid_int;
  BTID sys_btid;
  OID class_oid, oid;
  BTREE_OBJECT_INFO old_version;
  BTREE_OBJECT_INFO new_version;
  MVCCID threshold_mvccid = vacuum_Global_oldest_active_mvccid;
  BTREE_MVCC_INFO mvcc_info;
  MVCCID mvccid;
  struct log_vacuum_info log_vacuum;
  OID heap_object_oid;
  bool vacuum_complete = false;
  bool was_interrupted = false;
  bool is_file_dropped = false;
  bool page_found = false;

  PERF_UTIME_TRACKER perf_tracker;

  if (prm_get_bool_value (PRM_ID_DISABLE_VACUUM))
    {
      return NO_ERROR;
    }

  assert (worker != NULL);

  PERF_UTIME_TRACKER_START (thread_p, &perf_tracker);

  /* Initialize log_vacuum */
  LSA_SET_NULL (&log_vacuum.prev_mvcc_op_log_lsa);
  VFID_SET_NULL (&log_vacuum.vfid);

  /* Set sys_btid pointer for internal b-tree block */
  btid_int.sys_btid = &sys_btid;

  /* Check starting lsa is not null and that it really belong to this block */
  assert (!LSA_ISNULL (&VACUUM_DATA_ENTRY_START_LSA (data))
	  && (VACUUM_DATA_ENTRY_BLOCKID (data)
	      ==
	      vacuum_get_log_blockid (VACUUM_DATA_ENTRY_START_LSA (data).
				      pageid)));

  /* Fetch the page where start_lsa is located */
  log_page_p = (LOG_PAGE *) PTR_ALIGN (log_pgbuf, MAX_ALIGNMENT);
  log_page_p->hdr.logical_pageid = NULL_PAGEID;
  log_page_p->hdr.offset = NULL_OFFSET;

  vacuum_er_log (VACUUM_ER_LOG_WORKER,
		 "VACUUM: thread(%d): vacuum_process_log_block ():"
		 "blockid(%lld) start_lsa(%lld, %d) old_mvccid(%llu)"
		 " new_mvccid(%llu)\n", thread_get_current_entry_index (),
		 data->blockid, (long long int) data->start_lsa.pageid,
		 (int) data->start_lsa.offset, data->oldest_mvccid,
		 data->newest_mvccid);

#if defined (SERVER_MODE)
  if (vacuum_Prefetch_log_mode == VACUUM_PREFETCH_LOG_MODE_WORKERS)
    {
      error_code =
	vacuum_log_prefetch_vacuum_block (thread_p, data, block_log_buffer);
      if (error_code != NO_ERROR)
	{
	  return error_code;
	}
    }
#endif /* SERVER_MODE */

  /* Initialize stored heap objects. */
  worker->n_heap_objects = 0;

  was_interrupted = VACUUM_BLOCK_IS_INTERRUPTED (data);

  /* Follow the linked records starting with start_lsa */
  for (LSA_COPY (&log_lsa, &VACUUM_DATA_ENTRY_START_LSA (data));
       !LSA_ISNULL (&log_lsa) && log_lsa.pageid >= first_block_pageid;
       LSA_COPY (&log_lsa, &log_vacuum.prev_mvcc_op_log_lsa))
    {
#if defined(SERVER_MODE)
      if (thread_p->shutdown)
	{
	  /* Server shutdown was requested, stop vacuuming. */
	  goto end;
	}
#endif /* SERVER_MODE */

      vacuum_er_log (VACUUM_ER_LOG_WORKER,
		     "VACUUM: thread(%d): process log entry at log_lsa "
		     "(%lld, %d)\n", thread_get_current_entry_index (),
		     (long long int) log_lsa.pageid, (int) log_lsa.offset);

      worker->state = VACUUM_WORKER_STATE_PROCESS_LOG;
      PERF_UTIME_TRACKER_TIME_AND_RESTART (thread_p, &perf_tracker,
					   mnt_vac_worker_execute_time);

      LSA_COPY (&rcv_lsa, &log_lsa);

      if (log_page_p->hdr.logical_pageid != log_lsa.pageid)
	{
	  error_code = vacuum_copy_log_page (thread_p, log_lsa.pageid,
					     block_log_buffer, log_page_p);
	  if (error_code != NO_ERROR)
	    {
	      assert_release (false);
	      logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
				 "vacuum_process_log_block");
	      goto end;
	    }
	}

      /* Process log entry and obtain relevant information for vacuum. */
      error_code =
	vacuum_process_log_record (thread_p, worker, &log_lsa, log_page_p,
				   &log_record_data, &mvccid, &undo_data,
				   &undo_data_size, &log_vacuum,
				   &is_file_dropped, false);
      if (error_code != NO_ERROR)
	{
	  assert_release (false);
	  logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
			     "vacuum_process_log_block");
	  goto end;
	}

      if (is_file_dropped)
	{
	  /* No need to vacuum */
	  vacuum_er_log (VACUUM_ER_LOG_WORKER | VACUUM_ER_LOG_DROPPED_FILES,
			 "VACUUM: Skip vacuuming based on (%lld, %d) in "
			 "file %d|%d. Log record info: rcvindex=%d.\n",
			 (long long int) rcv_lsa.pageid, (int) rcv_lsa.offset,
			 log_vacuum.vfid.volid, log_vacuum.vfid.fileid,
			 log_record_data.rcvindex);
	  continue;
	}

      worker->state = VACUUM_WORKER_STATE_EXECUTE;
      PERF_UTIME_TRACKER_TIME_AND_RESTART (thread_p, &perf_tracker,
					   mnt_vac_worker_process_log_time);

#if !defined (NDEBUG)
      if (MVCC_ID_FOLLOW_OR_EQUAL (mvccid, threshold_mvccid)
	  || MVCC_ID_PRECEDES (mvccid, data->oldest_mvccid)
	  || MVCC_ID_PRECEDES (data->newest_mvccid, mvccid))
	{
	  /* threshold_mvccid or mvccid or block data may be invalid */
	  assert (0);
	  logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
			     "vacuum_process_log_block");
	  goto end;
	}
#endif /* !NDEBUG */

      if (LOG_IS_MVCC_HEAP_OPERATION (log_record_data.rcvindex))
	{
	  /* Collect heap object to be vacuumed at the end of the job. */
	  heap_object_oid.pageid = log_record_data.pageid;
	  heap_object_oid.volid = log_record_data.volid;
	  heap_object_oid.slotid =
	    heap_rv_remove_flags_from_offset (log_record_data.offset);

	  error_code =
	    vacuum_collect_heap_objects (worker, &heap_object_oid,
					 &log_vacuum.vfid);
	  if (error_code != NO_ERROR)
	    {
	      assert_release (false);
	      vacuum_er_log (VACUUM_ER_LOG_ERROR | VACUUM_ER_LOG_WORKER
			     | VACUUM_ER_LOG_HEAP,
			     "VACUUM ERROR: vacuum_collect_heap_objects.\n");
	      /* Release should not stop. */
	      er_clear ();
	      error_code = NO_ERROR;
	      continue;
	    }
	}
      else if (LOG_IS_MVCC_BTREE_OPERATION (log_record_data.rcvindex))
	{
	  /* Find b-tree entry and vacuum it */
	  OR_BUF key_buf;

	  assert (undo_data != NULL);

	  if (log_record_data.rcvindex == RVBT_MVCC_UPDATE_SAME_KEY
	      || log_record_data.rcvindex
	      == RVBT_MVCC_INSERT_OBJECT_UNQ_MULTIUPD)
	    {
	      btree_rv_read_keybuf_two_objects (thread_p, undo_data,
						undo_data_size, &btid_int,
						&old_version, &new_version,
						&key_buf);
	    }
	  else
	    {
	      btree_rv_read_keybuf_nocopy (thread_p, undo_data,
					   undo_data_size, &btid_int,
					   &class_oid, &oid, &mvcc_info,
					   &key_buf);
	    }

	  /* Vacuum based on rcvindex. */
	  if (log_record_data.rcvindex == RVBT_MVCC_UPDATE_SAME_KEY)
	    {
	      vacuum_er_log (VACUUM_ER_LOG_BTREE | VACUUM_ER_LOG_WORKER,
			     "VACUUM: thread(%d): vacuum mvcc update same "
			     "key from b-tree: btidp(%d, (%d %d)), "
			     "old object oid(%d, %d, %d), "
			     "class_oid(%d, %d, %d), "
			     "new object oid(%d, %d, %d), "
			     "class_oid(%d, %d, %d), mvccid=%llu.\n",
			     thread_get_current_entry_index (),
			     btid_int.sys_btid->root_pageid,
			     btid_int.sys_btid->vfid.fileid,
			     btid_int.sys_btid->vfid.volid,
			     old_version.oid.volid, old_version.oid.pageid,
			     old_version.oid.slotid,
			     old_version.class_oid.volid,
			     old_version.class_oid.pageid,
			     old_version.class_oid.slotid,
			     new_version.oid.volid, new_version.oid.pageid,
			     new_version.oid.slotid,
			     new_version.class_oid.volid,
			     new_version.class_oid.pageid,
			     new_version.class_oid.slotid,
			     (unsigned long long int) mvccid);
	      error_code =
		btree_vacuum_mvcc_update_same_key (thread_p,
						   btid_int.sys_btid,
						   &key_buf, &old_version,
						   &new_version, mvccid);
	    }
	  else if (log_record_data.rcvindex == RVBT_MVCC_NOTIFY_VACUUM)
	    {
	      /* The notification comes from loading index. The object may be
	       * both inserted or deleted (load index considers all objects
	       * for visibility reasons). Vacuum must also decide to remove
	       * insert MVCCID or the entire object.
	       */
	      if (MVCCID_IS_VALID (mvcc_info.delete_mvccid))
		{
		  vacuum_er_log (VACUUM_ER_LOG_BTREE | VACUUM_ER_LOG_WORKER,
				 "VACUUM: thread(%d): vacuum from b-tree: "
				 "btidp(%d, (%d %d)) oid(%d, %d, %d) "
				 "class_oid(%d, %d, %d), purpose=rem_object, "
				 "mvccid=%llu\n",
				 thread_get_current_entry_index (),
				 btid_int.sys_btid->root_pageid,
				 btid_int.sys_btid->vfid.fileid,
				 btid_int.sys_btid->vfid.volid,
				 oid.volid, oid.pageid, oid.slotid,
				 class_oid.volid, class_oid.pageid,
				 class_oid.slotid, mvcc_info.delete_mvccid);
		  error_code =
		    btree_vacuum_object (thread_p, btid_int.sys_btid,
					 &key_buf, &oid, &class_oid,
					 mvcc_info.delete_mvccid);
		}
	      else if (MVCCID_IS_VALID (mvcc_info.insert_mvccid)
		       && mvcc_info.insert_mvccid != MVCCID_ALL_VISIBLE)
		{
		  vacuum_er_log (VACUUM_ER_LOG_BTREE | VACUUM_ER_LOG_WORKER,
				 "VACUUM: thread(%d): vacuum from b-tree: "
				 "btidp(%d, (%d %d)) oid(%d, %d, %d) "
				 "class_oid(%d, %d, %d), purpose=rem_insid, "
				 "mvccid=%llu\n",
				 thread_get_current_entry_index (),
				 btid_int.sys_btid->root_pageid,
				 btid_int.sys_btid->vfid.fileid,
				 btid_int.sys_btid->vfid.volid,
				 oid.volid, oid.pageid, oid.slotid,
				 class_oid.volid, class_oid.pageid,
				 class_oid.slotid, mvcc_info.insert_mvccid);
		  error_code =
		    btree_vacuum_insert_mvccid (thread_p, btid_int.sys_btid,
						&key_buf, &oid, &class_oid,
						mvcc_info.insert_mvccid);
		}
	      else
		{
		  /* impossible case */
		  vacuum_er_log (VACUUM_ER_LOG_BTREE | VACUUM_ER_LOG_WORKER |
				 VACUUM_ER_LOG_ERROR,
				 "VACUUM ERROR: invalid vacuum case for "
				 "RVBT_MVCC_NOTIFY_VACUUM btid(%d, (%d %d)) "
				 "oid(%d, %d, %d) class_oid(%d, %d, %d)",
				 btid_int.sys_btid->root_pageid,
				 btid_int.sys_btid->vfid.fileid,
				 btid_int.sys_btid->vfid.volid, oid.volid,
				 oid.pageid, oid.slotid, class_oid.volid,
				 class_oid.pageid, class_oid.slotid);
		  assert_release (false);
		  continue;
		}
	    }
	  else if (log_record_data.rcvindex == RVBT_MVCC_DELETE_OBJECT)
	    {
	      /* Object was deleted and must be completely removed. */
	      vacuum_er_log (VACUUM_ER_LOG_BTREE | VACUUM_ER_LOG_WORKER,
			     "VACUUM: thread(%d): vacuum from b-tree: "
			     "btidp(%d, (%d %d)) oid(%d, %d, %d) "
			     "class_oid(%d, %d, %d), purpose=rem_object, "
			     "mvccid=%llu\n",
			     thread_get_current_entry_index (),
			     btid_int.sys_btid->root_pageid,
			     btid_int.sys_btid->vfid.fileid,
			     btid_int.sys_btid->vfid.volid,
			     oid.volid, oid.pageid, oid.slotid,
			     class_oid.volid, class_oid.pageid,
			     class_oid.slotid, mvccid);
	      error_code =
		btree_vacuum_object (thread_p, btid_int.sys_btid, &key_buf,
				     &oid, &class_oid, mvccid);
	    }
	  else if (log_record_data.rcvindex == RVBT_MVCC_INSERT_OBJECT
		   || log_record_data.rcvindex ==
		   RVBT_MVCC_INSERT_OBJECT_UNQ_MULTIUPD)
	    {
	      /* Object was inserted and only its insert MVCCID must be
	       * removed.
	       */
	      if (log_record_data.rcvindex
		  == RVBT_MVCC_INSERT_OBJECT_UNQ_MULTIUPD)
		{
		  COPY_OID (&oid, &old_version.oid);
		  COPY_OID (&class_oid, &old_version.class_oid);
		}
	      vacuum_er_log (VACUUM_ER_LOG_BTREE | VACUUM_ER_LOG_WORKER,
			     "VACUUM: thread(%d): vacuum from b-tree: "
			     "btidp(%d, (%d %d)) oid(%d, %d, %d) "
			     "class_oid(%d, %d, %d), purpose=rem_insid, "
			     "mvccid=%llu\n",
			     thread_get_current_entry_index (),
			     btid_int.sys_btid->root_pageid,
			     btid_int.sys_btid->vfid.fileid,
			     btid_int.sys_btid->vfid.volid,
			     oid.volid, oid.pageid, oid.slotid,
			     class_oid.volid, class_oid.pageid,
			     class_oid.slotid, mvccid);
	      error_code =
		btree_vacuum_insert_mvccid (thread_p, btid_int.sys_btid,
					    &key_buf, &oid, &class_oid,
					    mvccid);
	    }
	  else
	    {
	      /* Unexpected. */
	      assert_release (false);
	    }
	  /* Did we have any errors? */
	  if (error_code != NO_ERROR)
	    {
	      assert_release (false);
	      vacuum_er_log (VACUUM_ER_LOG_BTREE | VACUUM_ER_LOG_WORKER,
			     "VACUUM: thread(%d): Error deleting object or "
			     "insert MVCCID: error_code=%d",
			     thread_get_current_entry_index (), error_code);
	      er_clear ();
	      error_code = NO_ERROR;
	      /* Release should not stop. Continue. */
	    }
	}
      else if (log_record_data.rcvindex == RVES_NOTIFY_VACUUM)
	{
	  /* A lob file must be deleted */
	  (void) or_unpack_string (undo_data, &es_uri);
	  vacuum_er_log (VACUUM_ER_LOG_WORKER,
			 "VACUUM: Delete lob %s.", es_uri);
	  (void) es_delete_file (es_uri);
	  db_private_free_and_init (thread_p, es_uri);
	}
      else
	{
	  /* Safeguard code */
	  assert_release (false);
	}
    }

  error_code =
    vacuum_heap (thread_p, worker, threshold_mvccid, was_interrupted);
  if (error_code != NO_ERROR)
    {
      assert_release (false);
      /* Release should not stop. Continue. */
    }

  mnt_vac_log_vacuumed_pages (thread_p, vacuum_Data->log_block_npages);

  vacuum_complete = true;

end:
#if defined(SERVER_MODE)
  if (vacuum_Prefetch_log_mode == VACUUM_PREFETCH_LOG_MODE_MASTER
      && block_log_buffer != NULL)
    {
      lf_bitmap_free_entry (&vacuum_Prefetch_free_buffers_bitmap,
			    block_log_buffer->buffer_id);
    }
#endif /* SERVER_MODE */

  worker->state = VACUUM_WORKER_STATE_INACTIVE;
  if (!sa_mode_partial_block)
    {
      /* TODO: Check that if start_lsa can be set to a different value when
       *       vacuum is not complete, to avoid processing the same log data
       *       again.
       */
      vacuum_finished_block_vacuum (thread_p, data, vacuum_complete);
    }

  /* Unfix all pages now. Normally all pages should already be unfixed. */
  pgbuf_unfix_all (thread_p);

  PERF_UTIME_TRACKER_TIME_AND_RESTART (thread_p, &perf_tracker,
				       mnt_vac_worker_execute_time);

  return error_code;
}

/*
 * vacuum_assign_worker () - Assign a vacuum worker to current thread.
 *
 * return	 : Error code.
 * thread_p (in) : Thread entry.
 *
 * NOTE: This is protected by vacuum data lock.
 */
static int
vacuum_assign_worker (THREAD_ENTRY * thread_p)
{
  /* Get first unassigned worker */
  VACUUM_WORKER *worker = &vacuum_Workers[vacuum_Assigned_workers_count];
#if defined (SERVER_MODE)
  long long unsigned size_worker_prefetch_log_buffer;
#endif /* SERVER_MODE */

#if defined (SERVER_MODE)
  assert (thread_p->vacuum_worker == NULL);
  assert (thread_p->type == TT_VACUUM_WORKER);
  assert (vacuum_Assigned_workers_count < VACUUM_MAX_WORKER_COUNT);
#endif /* SERVER_MODE */

  /* Initialize worker state */
  worker->state = VACUUM_WORKER_STATE_INACTIVE;

  /* Allocate log_zip */
  worker->log_zip_p = log_zip_alloc (IO_PAGESIZE, false);
  if (worker->log_zip_p == NULL)
    {
      vacuum_er_log (VACUUM_ER_LOG_ERROR | VACUUM_ER_LOG_WORKER,
		     "VACUUM ERROR: Could not allocate log zip.");
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
			 "vacuum_assign_worker");
      return ER_FAILED;
    }

  /* Allocate heap objects buffer */
  worker->heap_objects_capacity = VACUUM_DEFAULT_HEAP_OBJECT_BUFFER_SIZE;
  worker->heap_objects =
    (VACUUM_HEAP_OBJECT *)
    malloc (worker->heap_objects_capacity * sizeof (VACUUM_HEAP_OBJECT));
  if (worker->heap_objects == NULL)
    {
      vacuum_er_log (VACUUM_ER_LOG_ERROR | VACUUM_ER_LOG_WORKER,
		     "VACUUM ERROR: Could not allocate files and objects "
		     "buffer.\n");
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
			 "vacuum_assign_worker");
      goto error;
    }

  /* Allocate undo data buffer */
  worker->undo_data_buffer = (char *) malloc (IO_PAGESIZE);
  if (worker->undo_data_buffer == NULL)
    {
      vacuum_er_log (VACUUM_ER_LOG_ERROR | VACUUM_ER_LOG_WORKER,
		     "VACUUM ERROR: Could not allocate undo data buffer.");
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
			 "vacuum_assign_worker");
      goto error;
    }
  worker->undo_data_buffer_capacity = IO_PAGESIZE;

  worker->postpone_redo_data_buffer = (char *) malloc (IO_PAGESIZE);
  if (worker->postpone_redo_data_buffer == NULL)
    {
      vacuum_er_log (VACUUM_ER_LOG_ERROR | VACUUM_ER_LOG_WORKER,
		     "VACUUM ERROR: Could not allocate "
		     "postpone_redo_data_buffer.");
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
			 "vacuum_assign_worker");
      goto error;
    }

#if defined (SERVER_MODE)
  /* Save worker to thread entry */
  thread_p->vacuum_worker = worker;

  if (vacuum_Prefetch_log_mode == VACUUM_PREFETCH_LOG_MODE_WORKERS
      && worker->prefetch_log_buffer == NULL)
    {
      size_worker_prefetch_log_buffer =
	(((long long unsigned) VACUUM_PREFETCH_LOG_BLOCK_BUFFER_PAGES)
	 * LOG_PAGESIZE) + MAX_ALIGNMENT;

      worker->prefetch_log_buffer =
	(char *) malloc ((size_t) size_worker_prefetch_log_buffer);
      if (worker->prefetch_log_buffer == NULL)
	{
	  vacuum_er_log (VACUUM_ER_LOG_ERROR | VACUUM_ER_LOG_WORKER,
			 "VACUUM ERROR: Could not allocate prefetch buffer.");
	  logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
			     "vacuum_assign_worker");
	  goto error;
	}
    }
#endif

  /* Safe guard - it is assumed that transaction descriptor is already
   * initialized.
   */
  assert (worker->tdes != NULL);

  /* Increment number of assigned workers */
  vacuum_Assigned_workers_count++;
  return NO_ERROR;

error:
  /* Free worker resources. */
  vacuum_finalize_worker (thread_p, worker);
  return ER_FAILED;
}

/*
 * vacuum_finalize_worker () - Free resources allocated for vacuum worker.
 *
 * return	    : Void.
 * worker_info (in) : Vacuum worker.
 */
static void
vacuum_finalize_worker (THREAD_ENTRY * thread_p, VACUUM_WORKER * worker_info)
{
  if (worker_info->log_zip_p != NULL)
    {
      log_zip_free (worker_info->log_zip_p);
      worker_info->log_zip_p = NULL;
    }
  if (worker_info->heap_objects != NULL)
    {
      free_and_init (worker_info->heap_objects);
    }
  if (worker_info->undo_data_buffer != NULL)
    {
      free_and_init (worker_info->undo_data_buffer);
    }
  if (worker_info->postpone_redo_data_buffer != NULL)
    {
      free_and_init (worker_info->postpone_redo_data_buffer);
    }
  if (worker_info->tdes != NULL)
    {
      logtb_finalize_tdes (thread_p, worker_info->tdes);

      free_and_init (worker_info->tdes);
    }
#if defined (SERVER_MODE)
  if (worker_info->prefetch_log_buffer != NULL)
    {
      free_and_init (worker_info->prefetch_log_buffer);
    }
#endif /* SERVER_MODE */
}

/*
 * vacuum_rv_get_worker_by_trid () - Get vacuum worker identified by TRANID
 *				     to recover its system operations.
 *
 * return	    : Transaction descriptor.
 * thread_p (in)    : Thread entry.
 * TRANID trid (in) : Transaction identifier.
 *
 * NOTE: This is currently only called during recovery.
 */
VACUUM_WORKER *
vacuum_rv_get_worker_by_trid (THREAD_ENTRY * thread_p, TRANID trid)
{
  /* Convert trid to vacuum worker index */
  int worker_index = VACUUM_WORKER_TRANID_TO_INDEX (trid);

  /* Check valid TRANID/index */
  assert (worker_index >= 0 && worker_index < VACUUM_MAX_WORKER_COUNT);
  /* Check this is called under recovery context. */
  assert (!LOG_ISRESTARTED ());

  /* Return worker identifier by TRANID */
  return &vacuum_Workers[worker_index];
}

/*
 * vacuum_rv_finish_worker_recovery () - Reset worker after finishing
 *					 recovering its work.
 *
 * return	 : Void.
 * thread_p (in) : Thread entry.
 * trid (in)	 : Transaction identifier.
 */
void
vacuum_rv_finish_worker_recovery (THREAD_ENTRY * thread_p, TRANID trid)
{
  /* Convert trid to vacuum worker index */
  int worker_index = VACUUM_WORKER_TRANID_TO_INDEX (trid);

  /* Check valid TRANID/index */
  assert (worker_index >= 0 && worker_index < VACUUM_MAX_WORKER_COUNT);
  /* Check this is called under recovery context. */
  assert (!LOG_ISRESTARTED ());

  vacuum_er_log (VACUUM_ER_LOG_RECOVERY | VACUUM_ER_LOG_TOPOPS,
		 "VACUUM: Finished recovery for vacuum worker with "
		 "tdes->trid=%d.", trid);

  /* Reset vacuum worker state */
  vacuum_Workers[worker_index].state = VACUUM_WORKER_STATE_INACTIVE;
  /* Reset vacuum worker transaction descriptor */
  vacuum_Workers[worker_index].tdes->state = TRAN_ACTIVE;
}

#if defined (SA_MODE)
/*
 * vacuum_get_worker_sa_mode () - Get vacuum worker for stand-alone mode.
 *
 * return    : Vacuum worker for stand-alone mode.
 */
VACUUM_WORKER *
vacuum_get_worker_sa_mode (void)
{
  return vacuum_Worker_sa_mode;
}

/*
 * vacuum_set_worker_sa_mode () - Set vacuum worker for stand-alone mode.
 *
 * return      : Void.
 * worker (in) : Vacuum worker.
 */
void
vacuum_set_worker_sa_mode (VACUUM_WORKER * worker)
{
  vacuum_Worker_sa_mode = worker;
}
#endif

#if defined (SERVER_MODE)
/*
 * vacuum_start_new_job () - Start a vacuum job which process one block of
 *			     log data.
 *
 * return	 : Void.
 * thread_p (in) : Thread entry.
 * blockid (in)	 : Block of log data identifier.
 */
void
vacuum_start_new_job (THREAD_ENTRY * thread_p)
{
  VACUUM_DATA_ENTRY *entry = NULL;
  VACUUM_WORKER *worker_info = NULL;
  VACUUM_JOB_ENTRY vacuum_job_entry;

  assert (thread_p->type == TT_VACUUM_WORKER);

  /* The table where data about log blocks is kept can be modified.
   * Lock vacuum data, identify the right block data and copy them.
   */
  VACUUM_LOCK_DATA ();

  worker_info = VACUUM_GET_VACUUM_WORKER (thread_p);
  if (worker_info == NULL)
    {
      /* Assign the tread a vacuum worker. */
      if (vacuum_assign_worker (thread_p) != NO_ERROR)
	{
	  VACUUM_UNLOCK_DATA ();
	  return;
	}
      /* Check assignment was successful. */
      assert (VACUUM_GET_VACUUM_WORKER (thread_p) != NULL);
    }
  VACUUM_UNLOCK_DATA ();

  /* Increment running workers */
  ATOMIC_INC_32 (&vacuum_Running_workers_count, 1);

  /* Loop as long as job queue is not empty */
  while (lf_circular_queue_consume (vacuum_Job_queue, &vacuum_job_entry))
    {
      /* Execute vacuum job */
      /* entry is only a copy for vacuum data */
      entry = &vacuum_job_entry.vacuum_data_entry;

      /* Safe guard */
      assert (VACUUM_BLOCK_STATUS_IS_IN_PROGRESS (entry));

      /* Run vacuum */
      (void) vacuum_process_log_block (thread_p, entry,
				       &vacuum_job_entry.block_log_buffer,
				       false);
    }

  /* Decrement running workers */
  ATOMIC_INC_32 (&vacuum_Running_workers_count, -1);

  /* No jobs in queue */
  if (vacuum_Data->n_table_entries > 0
      || !LSA_ISNULL (&vacuum_Data->last_blocks_to_recover)
      || !LOCK_FREE_CIRCULAR_QUEUE_IS_EMPTY (vacuum_Block_data_buffer))
    {
      /* Wakeup master to assign new jobs */
      thread_wakeup_vacuum_master_thread ();
    }
  return;
}
#endif /* SERVER_MODE */

/*
 * vacuum_finished_block_vacuum () - Called when vacuuming a block is stopped.
 *
 * return		   : Void.
 * thread_p (in)	   : Thread entry.
 * data (in)		   : Vacuum block data.
 * is_vacuum_complete (in) : True if the entire block was processed.
 *
 * NOTE: The block data received here is not a block in vacuum data table.
 *	 It is just a copy (because the table can be changed and the data
 *	 can be moved). First obtain the block data in the table and copy the
 *	 block data received as argument.
 */
static void
vacuum_finished_block_vacuum (THREAD_ENTRY * thread_p,
			      VACUUM_DATA_ENTRY * data,
			      bool is_vacuum_complete)
{
  VACUUM_DATA_ENTRY *table_entry = NULL;
  VACUUM_LOG_BLOCKID blockid;

  /* Lock vacuum data to add changes */
  VACUUM_LOCK_DATA ();

  /* Clear running flag on copied entry data */
  VACUUM_BLOCK_STATUS_SET_AVAILABLE (data);

  /* Find the equivalent block in vacuum data table */
  blockid = VACUUM_DATA_ENTRY_BLOCKID (data);
  table_entry = vacuum_get_vacuum_data_entry (blockid);
  if (table_entry == NULL)
    {
      /* Impossible, safeguard code */
      assert (0);
      VACUUM_UNLOCK_DATA ();
      return;
    }

  assert (VACUUM_BLOCK_STATUS_IS_IN_PROGRESS (table_entry));

  if (is_vacuum_complete)
    {
      /* Set status as vacuumed. Vacuum master will remove it from table */
      VACUUM_BLOCK_STATUS_SET_VACUUMED (table_entry);
      VACUUM_BLOCK_CLEAR_INTERRUPTED (table_entry);
    }
  else
    {
      /* We expect that worker job is abandoned during shutdown. But all other
       * cases are error cases.
       */
      int error_level =
#if defined (SERVER_MODE)
	thread_p->shutdown ? VACUUM_ER_LOG_WARNING : VACUUM_ER_LOG_ERROR;
#else /* !SERVER_MODE */
	VACUUM_ER_LOG_ERROR;
#endif /* !SERVER_MODE */

      /* Vacuum will have to be re-run */
      VACUUM_BLOCK_STATUS_SET_AVAILABLE (table_entry);
      /* Copy new block data */
      /* The only relevant information is in fact the updated start_lsa
       * if it has changed.
       */

      vacuum_er_log (error_level | VACUUM_ER_LOG_WORKER,
		     "VACUUM %s: Processing log block %lld is not complete!",
		     error_level == VACUUM_ER_LOG_ERROR ?
		     "ERROR" : "WARNING", blockid);

      /* Job was not fully executed. */
      VACUUM_BLOCK_SET_INTERRUPTED (table_entry);
    }

  /* TODO: Do we want to log start_lsa here? */
  log_append_redo_data2 (thread_p, RVVAC_START_OR_END_JOB, NULL, NULL, 0,
			 sizeof (table_entry->blockid),
			 &table_entry->blockid);

  /* Unlock vacuum data */
  VACUUM_UNLOCK_DATA ();
}

/*
 * vacuum_process_log_record () - Process one log record for vacuum.
 *
 * return			  : Error code.
 * worker (in)			  : Vacuum worker.
 * thread_p (in)		  : Thread entry.
 * log_lsa_p (in/out)		  : Input is the start of undo data. Output is
 *				    the end of undo data.
 * log_page_p (in/out)		  : The log page for log_lsa_p.
 * mvccid (out)			  : Log entry MVCCID.
 * undo_data_ptr (out)		  : Undo data pointer.
 * undo_data_size (out)		  : Undo data size.
 * is_file_dropped (out)	  : True if the file corresponding to log
 *				    entry was dropped.
 * stop_after_vacuum_info (in)	  : True if only vacuum info must be obtained
 *				    from log record.
 */
static int
vacuum_process_log_record (THREAD_ENTRY * thread_p,
			   VACUUM_WORKER * worker,
			   LOG_LSA * log_lsa_p,
			   LOG_PAGE * log_page_p,
			   struct log_data *log_record_data,
			   MVCCID * mvccid,
			   char **undo_data_ptr, int *undo_data_size,
			   struct log_vacuum_info *vacuum_info,
			   bool * is_file_dropped,
			   bool stop_after_vacuum_info)
{
  LOG_RECORD_HEADER *log_rec_header = NULL;
  struct log_mvcc_undoredo *mvcc_undoredo = NULL;
  struct log_mvcc_undo *mvcc_undo = NULL;
  struct log_undoredo *undoredo = NULL;
  struct log_undo *undo = NULL;
  int ulength;
  char *new_undo_data_buffer = NULL;
  bool is_zipped = false;
  volatile LOG_RECTYPE log_rec_type = LOG_SMALLER_LOGREC_TYPE;

  assert (log_lsa_p != NULL && log_page_p != NULL);
  assert (log_record_data != NULL);
  assert (mvccid != NULL);
  assert (stop_after_vacuum_info || is_file_dropped != NULL);
  assert (stop_after_vacuum_info || worker != NULL);
  assert (stop_after_vacuum_info || undo_data_ptr != NULL);
  assert (stop_after_vacuum_info || undo_data_size != NULL);

  if (!stop_after_vacuum_info)
    {
      *undo_data_ptr = NULL;
      *undo_data_size = 0;
    }

  LSA_SET_NULL (&vacuum_info->prev_mvcc_op_log_lsa);
  VFID_SET_NULL (&vacuum_info->vfid);

  /* Get log record header */
  log_rec_header = LOG_GET_LOG_RECORD_HEADER (log_page_p, log_lsa_p);
  log_rec_type = log_rec_header->type;
  LOG_READ_ADD_ALIGN (thread_p, sizeof (*log_rec_header), log_lsa_p,
		      log_page_p);

  if (log_rec_type == LOG_MVCC_UNDO_DATA)
    {
      /* Get log record mvcc_undo information */
      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (*mvcc_undo),
					log_lsa_p, log_page_p);
      mvcc_undo =
	(struct log_mvcc_undo *) (log_page_p->area + log_lsa_p->offset);

      /* Get MVCCID */
      *mvccid = mvcc_undo->mvccid;

      /* Get record log data */
      *log_record_data = mvcc_undo->undo.data;

      /* Get undo data length */
      ulength = mvcc_undo->undo.length;

      /* Copy LSA for next MVCC operation */
      LSA_COPY (&vacuum_info->prev_mvcc_op_log_lsa,
		&mvcc_undo->vacuum_info.prev_mvcc_op_log_lsa);
      VFID_COPY (&vacuum_info->vfid, &mvcc_undo->vacuum_info.vfid);

      LOG_READ_ADD_ALIGN (thread_p, sizeof (*mvcc_undo), log_lsa_p,
			  log_page_p);
    }
  else if (log_rec_type == LOG_MVCC_UNDOREDO_DATA
	   || log_rec_type == LOG_MVCC_DIFF_UNDOREDO_DATA)
    {
      /* Get log record undoredo information */
      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (*mvcc_undoredo),
					log_lsa_p, log_page_p);
      mvcc_undoredo =
	(struct log_mvcc_undoredo *) (log_page_p->area + log_lsa_p->offset);

      /* Get MVCCID */
      *mvccid = mvcc_undoredo->mvccid;

      /* Get record log data */
      *log_record_data = mvcc_undoredo->undoredo.data;

      /* Get undo data length */
      ulength = mvcc_undoredo->undoredo.ulength;

      /* Copy LSA for next MVCC operation */
      LSA_COPY (&vacuum_info->prev_mvcc_op_log_lsa,
		&mvcc_undoredo->vacuum_info.prev_mvcc_op_log_lsa);
      VFID_COPY (&vacuum_info->vfid, &mvcc_undoredo->vacuum_info.vfid);

      LOG_READ_ADD_ALIGN (thread_p, sizeof (*mvcc_undoredo), log_lsa_p,
			  log_page_p);
    }
  else
    {
      /* Unexpected case */
      assert (false);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return ER_FAILED;
    }

  if (stop_after_vacuum_info)
    {
      /* Vacuum info was already obtained. */
      return NO_ERROR;
    }

  if (!VFID_ISNULL (&vacuum_info->vfid))
    {
      /* Check if file was dropped. */
      if (worker->drop_files_version != vacuum_Dropped_files_version)
	{
	  /* New files have been dropped. Droppers must wait until all running
	   * workers have been notified. Save new version to let dropper know
	   * this worker noticed the changes.
	   */

	  /* But first, cleanup collected heap objects. */
	  VFID vfid;
	  VFID_COPY (&vfid, &vacuum_Last_dropped_vfid);
	  vacuum_cleanup_collected_by_vfid (worker, &vfid);

	  worker->drop_files_version = vacuum_Dropped_files_version;
	  vacuum_er_log (VACUUM_ER_LOG_DROPPED_FILES | VACUUM_ER_LOG_WORKER,
			 "VACUUM: Worker(%d) update min version to %d",
			 thread_get_current_tran_index (),
			 worker->drop_files_version);
	}

      /* Check if file is dropped */
      *is_file_dropped = vacuum_is_file_dropped (thread_p, &vacuum_info->vfid,
						 *mvccid);
      if (*is_file_dropped)
	{
	  return NO_ERROR;
	}
    }

  /* We are here because the file that will be vacuumed is not dropped. */
  if (!LOG_IS_MVCC_BTREE_OPERATION (log_record_data->rcvindex)
      && log_record_data->rcvindex != RVES_NOTIFY_VACUUM)
    {
      /* No need to unpack undo data */
      return NO_ERROR;
    }

  /* We are here because undo data must be unpacked. */
  if (ZIP_CHECK (ulength))
    {
      /* Get real size */
      *undo_data_size = (int) GET_ZIP_LEN (ulength);
      is_zipped = true;
    }
  else
    {
      *undo_data_size = ulength;
    }

  if (log_lsa_p->offset + *undo_data_size < (int) LOGAREA_SIZE)
    {
      /* Set undo data pointer directly to log data */
      *undo_data_ptr = (char *) log_page_p->area + log_lsa_p->offset;
    }
  else
    {
      /* Undo data is found on several pages and needs to be copied to a
       * contiguous area.
       */
      if (worker->undo_data_buffer_capacity < *undo_data_size)
	{
	  /* Not enough space to save all undo data. Expand worker's undo data
	   * buffer.
	   */
	  new_undo_data_buffer =
	    (char *) realloc (worker->undo_data_buffer, *undo_data_size);
	  if (new_undo_data_buffer == NULL)
	    {
	      vacuum_er_log (VACUUM_ER_LOG_ERROR | VACUUM_ER_LOG_WORKER,
			     "VACUUM ERROR: Could not expand undo data "
			     "buffer to %d.", *undo_data_size);
	      logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
				 "vacuum_process_log_record");
	      return ER_FAILED;
	    }
	  /* Expand was successful, update worker. */
	  worker->undo_data_buffer = new_undo_data_buffer;
	  worker->undo_data_buffer_capacity = *undo_data_size;
	}
      /* Set undo data pointer to worker's undo data buffer. */
      *undo_data_ptr = worker->undo_data_buffer;

      /* Copy data to buffer. */
      logpb_copy_from_log (thread_p, *undo_data_ptr, *undo_data_size,
			   log_lsa_p, log_page_p);
    }

  if (is_zipped)
    {
      /* Unzip data */
      if (log_unzip (worker->log_zip_p, *undo_data_size, *undo_data_ptr))
	{
	  /* Update undo data pointer and size after unzipping. */
	  *undo_data_size = (int) worker->log_zip_p->data_length;
	  *undo_data_ptr = (char *) worker->log_zip_p->log_data;
	}
      else
	{
	  vacuum_er_log (VACUUM_ER_LOG_ERROR | VACUUM_ER_LOG_WORKER,
			 "VACUUM ERROR: Could not unzip undo data.");
	  logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
			     "vacuum_process_log_record");
	  return ER_FAILED;
	}
    }

  return NO_ERROR;
}

/*
 * vacuum_get_worker_min_dropped_files_version () - Get current minimum
 *						    dropped files
 *						    version seen by active
 *						    vacuum workers.
 *
 * return : Minimum dropped files version.
 */
static INT32
vacuum_get_worker_min_dropped_files_version (void)
{
  int i;
  INT32 min_version = -1;

  for (i = 0; i < vacuum_Assigned_workers_count; i++)
    {
      /* Update minimum version if worker is active and its seen version is
       * smaller than current minimum version (or if minimum version is not
       * initialized).
       */
      if (vacuum_Workers[i].state != VACUUM_WORKER_STATE_INACTIVE
	  && (min_version == -1
	      || vacuum_compare_dropped_files_version (min_version,
						       vacuum_Workers[i].
						       drop_files_version) >
	      0))
	{
	  /* Update overall minimum version. */
	  min_version = vacuum_Workers[i].drop_files_version;
	}
    }
  return min_version;
}

/*
 * vacuum_get_vacuum_data_entry () - Search for blockid in vacuum data table
 *				     and return pointer to the equivalent
 *				     entry.
 *
 * return	: Pointer to data entry if blockid is found, NULL otherwise.
 * blockid (in) : Log block identifier.
 */
static VACUUM_DATA_ENTRY *
vacuum_get_vacuum_data_entry (VACUUM_LOG_BLOCKID blockid)
{
  return (VACUUM_DATA_ENTRY *) bsearch (&blockid,
					vacuum_Data->vacuum_data_table,
					vacuum_Data->n_table_entries,
					sizeof (VACUUM_DATA_ENTRY),
					vacuum_compare_data_entries);
}

/*
 * vacuum_compare_data_entries () - Comparator function for vacuum data
 *				    entries. The entries' block id's are
 *				    compared.
 *
 * return    : 0 if entries are equal, negative if first entry is smaller and
 *	       positive if first entry is bigger.
 * ptr1 (in) : Pointer to first vacuum data entry.
 * ptr2 (in) : Pointer to second vacuum data entry.
 */
static int
vacuum_compare_data_entries (const void *ptr1, const void *ptr2)
{
  return (int) (VACUUM_DATA_ENTRY_BLOCKID ((VACUUM_DATA_ENTRY *) ptr1)
		- VACUUM_DATA_ENTRY_BLOCKID ((VACUUM_DATA_ENTRY *) ptr2));
}

/*
 * vacuum_load_data_from_disk () - Loads vacuum data from disk.
 *
 * return	 : Error code.
 * thread_p (in) : Thread entry.
 *
 * NOTE: Loading vacuum data should be done when the database is started,
 *	 before starting other vacuum routines.
 */
static int
vacuum_load_data_from_disk (THREAD_ENTRY * thread_p)
{
  int error_code = NO_ERROR;
  int vol_fd;
  VACUUM_DATA_ENTRY *entry = NULL;

  assert_release (vacuum_Data == NULL);
  assert_release (!VFID_ISNULL (&vacuum_Data_vfid));

  /* Data is being loaded from disk so all data that is flushed */
  LSA_SET_NULL (&vacuum_Data_oldest_not_flushed_lsa);

  vacuum_Data_max_size = IO_PAGESIZE * vacuum_Data_npages;
  vacuum_Data = (VACUUM_DATA *) malloc (vacuum_Data_max_size);
  if (vacuum_Data == NULL)
    {
      assert (false);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  /* Get the first vacuum data page vpid */
  if (vacuum_get_first_page_vacuum_data (thread_p, &vacuum_Data_vpid) == NULL)
    {
      assert (false);
      return ER_FAILED;
    }

  assert (!VPID_ISNULL (&vacuum_Data_vpid));

  vol_fd = fileio_get_volume_descriptor (vacuum_Data_vpid.volid);

  /* Read vacuum data from disk */
  /* Do not use page buffer */
  /* All vacuum data pages are contiguous */
  if (fileio_read_pages (thread_p, vol_fd, (char *) vacuum_Data,
			 vacuum_Data_vpid.pageid, vacuum_Data_npages,
			 IO_PAGESIZE) == NULL)
    {
      assert (false);
      return ER_FAILED;
    }

  return NO_ERROR;
}

/*
 * vacuum_check_interrupted_jobs () - Check vacuum data entries before
 *				      starting server and change the state of
 *				      all running jobs as available. Also mark
 *				      the jobs as interrupted.
 *
 * return	 : Void.
 * thread_p (in) : Thread entry.
 */
void
vacuum_check_interrupted_jobs (THREAD_ENTRY * thread_p)
{
  int i = 0;

  /* Make sure no entry is marked as being vacuumed. */
  /* When vacuum data was last flushed, it is possible that some blocks
   * were being vacuumed. Set their status as available and mark them as
   * interrupted.
   */
  for (i = 0; i < vacuum_Data->n_table_entries; i++)
    {
      if (VACUUM_BLOCK_STATUS_IS_IN_PROGRESS (VACUUM_DATA_GET_ENTRY (i)))
	{
	  VACUUM_BLOCK_STATUS_SET_AVAILABLE (VACUUM_DATA_GET_ENTRY (i));
	  VACUUM_BLOCK_SET_INTERRUPTED (VACUUM_DATA_GET_ENTRY (i));
	}
    }
}

/*
 * vacuum_load_dropped_files_from_disk () - Loads dropped files from disk.
 *
 * return	 : Error code.
 * thread_p (in) : Thread entry.
 * vfid (in)	 : File identifier.
 */
int
vacuum_load_dropped_files_from_disk (THREAD_ENTRY * thread_p)
{
  VACUUM_DROPPED_FILES_PAGE *page = NULL;
  VPID vpid;
  INT16 page_count;
#if !defined (NDEBUG)
  VACUUM_TRACK_DROPPED_FILES *track_head = NULL, *track_tail = NULL;
  VACUUM_TRACK_DROPPED_FILES *track_new = NULL, *save_next = NULL;
#endif

  assert_release (!VFID_ISNULL (&vacuum_Dropped_files_vfid));

  if (vacuum_Dropped_files_loaded)
    {
      /* Already loaded */
      assert_release (!VPID_ISNULL (&vacuum_Dropped_files_vpid));
#if !defined (NDEBUG)
      assert_release (vacuum_Track_dropped_files != NULL);
#endif
      return NO_ERROR;
    }

  assert (!VPID_ISNULL (&vacuum_Dropped_files_vpid));

  /* Save total count. */
  if (vacuum_Dropped_files_count != 0)
    {
      assert (false);
      vacuum_Dropped_files_count = 0;
    }

  VPID_COPY (&vpid, &vacuum_Dropped_files_vpid);

  while (!VPID_ISNULL (&vpid))
    {
      page = vacuum_fix_dropped_entries_page (thread_p, &vpid,
					      PGBUF_LATCH_READ);
      if (page == NULL)
	{
	  assert (false);
	  return ER_FAILED;
	}

      /* Get next page VPID and current page count */
      VPID_COPY (&vpid, &page->next_page);

      page_count = page->n_dropped_files;
      vacuum_Dropped_files_count += (INT32) page_count;

#if !defined (NDEBUG)
      track_new =
	(VACUUM_TRACK_DROPPED_FILES *)
	malloc (VACUUM_TRACK_DROPPED_FILES_SIZE);
      if (track_new == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
		  1, VACUUM_TRACK_DROPPED_FILES_SIZE);
	  for (track_new = track_head; track_new != NULL;
	       track_new = save_next)
	    {
	      save_next = track_new->next_tracked_page;
	      free_and_init (track_new);
	    }
	  vacuum_unfix_dropped_entries_page (thread_p, page);
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}

      memcpy (&track_new->dropped_data_page, page, DB_PAGESIZE);
      track_new->next_tracked_page = NULL;

      if (track_head == NULL)
	{
	  track_head = track_tail = track_new;
	}
      else
	{
	  assert (track_tail != NULL);
	  track_tail->next_tracked_page = track_new;
	  track_tail = track_new;
	}
#endif
      vacuum_unfix_dropped_entries_page (thread_p, page);
    }

#if !defined(NDEBUG)
  vacuum_Track_dropped_files = track_head;
#endif

  vacuum_Dropped_files_loaded = true;
  return NO_ERROR;
}

/*
 * vacuum_flush_data () - Flush vacuum data to disk. Only used pages are
 *			  flushed.
 *
 * return			: Error code.
 * thread_p (in)		: Thread entry.
 * flush_to_lsa (in)		: New target checkpoint lsa for flush.
 * prev_chkpt_lsa (in)		: Previous checkpoint lsa.
 * oldest_not_flushed_lsa (out) : If flush is failed, the oldest lsa that is
 *				  not yet flushed to disk must be sent to
 *				  caller.
 * is_vacuum_data_locked (in)	: True if vacuum data is locked when this
 *				  function is called. If false, it will first
 *				  lock vacuum data, and will unlock it before
 *				  exiting the function.
 */
int
vacuum_flush_data (THREAD_ENTRY * thread_p, LOG_LSA * flush_to_lsa,
		   LOG_LSA * prev_chkpt_lsa, LOG_LSA * oldest_not_flushed_lsa,
		   bool is_vacuum_data_locked)
{
  int n_pages;
  int vacuum_data_actual_size;
  int error_code = NO_ERROR;
  int vol_fd;

  if (vacuum_Data == NULL)
    {
      /* The vacuum data is not loaded yet, therefore there are no changes */
      return NO_ERROR;
    }

  if (prm_get_bool_value (PRM_ID_DISABLE_VACUUM))
    {
      return NO_ERROR;
    }

  if (!is_vacuum_data_locked)
    {
      VACUUM_LOCK_DATA ();
    }

  /* Make sure that vacuum data is up to date before flushing it */
  if (vacuum_consume_buffer_log_blocks (thread_p) != NO_ERROR)
    {
      if (!is_vacuum_data_locked)
	{
	  VACUUM_UNLOCK_DATA ();
	}
      return ER_FAILED;
    }

  if (LSA_ISNULL (&vacuum_Data_oldest_not_flushed_lsa))
    {
      vacuum_er_log (VACUUM_ER_LOG_WARNING | VACUUM_ER_LOG_VACUUM_DATA,
		     "VACUUM: Vacuum data is not flushed to disk because "
		     "there are no changes since last flush.");
      if (!is_vacuum_data_locked)
	{
	  VACUUM_UNLOCK_DATA ();
	}

      /* No changes, nothing to flush */
      return NO_ERROR;
    }

  if (flush_to_lsa != NULL
      && LSA_GT (&vacuum_Data_oldest_not_flushed_lsa, flush_to_lsa))
    {
      vacuum_er_log (VACUUM_ER_LOG_WARNING | VACUUM_ER_LOG_VACUUM_DATA,
		     "VACUUM: Vacuum data is not flused to disk because "
		     "flush_to_lsa (%lld, %d) is less than oldest vacuum "
		     "data unflushed lsa (%lld, %d).",
		     (long long int) flush_to_lsa->pageid,
		     (int) flush_to_lsa->pageid,
		     (long long int)
		     vacuum_Data_oldest_not_flushed_lsa.pageid,
		     (int) vacuum_Data_oldest_not_flushed_lsa.offset);
      if (!is_vacuum_data_locked)
	{
	  VACUUM_UNLOCK_DATA ();
	}

      /* Skip flushing */
      return NO_ERROR;
    }

  if (prev_chkpt_lsa != NULL
      && LSA_LT (&vacuum_Data_oldest_not_flushed_lsa, prev_chkpt_lsa))
    {
      /* Conservative safety check */
      vacuum_er_log (VACUUM_ER_LOG_ERROR | VACUUM_ER_LOG_VACUUM_DATA,
		     "VACUUM ERROR: Vacuum data oldest unflused lsa is older "
		     "than previous checkpoint!");
      if (!is_vacuum_data_locked)
	{
	  VACUUM_UNLOCK_DATA ();
	}

      assert (false);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return ER_FAILED;
    }

  /* Make sure the log is flushed up to vacuum_Data->crt_lsa. */
  logpb_flush_log_for_wal (thread_p, &vacuum_Data->crt_lsa);

  vacuum_data_actual_size =
    VACUUM_DATA_HEADER_SIZE +
    (vacuum_Data->n_table_entries * sizeof (VACUUM_DATA_ENTRY));
  n_pages = CEIL_PTVDIV (vacuum_data_actual_size, IO_PAGESIZE);

  /* Flush to disk */
  vol_fd = fileio_get_volume_descriptor (vacuum_Data_vpid.volid);
  if (fileio_write_pages (thread_p, vol_fd, (char *) vacuum_Data,
			  vacuum_Data_vpid.pageid, n_pages,
			  IO_PAGESIZE) == NULL)
    {
      vacuum_er_log (VACUUM_ER_LOG_ERROR | VACUUM_ER_LOG_VACUUM_DATA,
		     "VACUUM: Flushing data failed! Could not write to disk.");

      if (oldest_not_flushed_lsa != NULL
	  && (LSA_ISNULL (oldest_not_flushed_lsa)
	      || LSA_LT (&vacuum_Data_oldest_not_flushed_lsa,
			 oldest_not_flushed_lsa)))
	{
	  /* If flush is failed, noticing the caller may be required */
	  LSA_COPY (oldest_not_flushed_lsa,
		    &vacuum_Data_oldest_not_flushed_lsa);
	}

      if (!is_vacuum_data_locked)
	{
	  VACUUM_UNLOCK_DATA ();
	}

      return ER_FAILED;
    }

  /* Successful flush, reset vacuum_Data_oldest_not_flushed_lsa */
  LSA_SET_NULL (&vacuum_Data_oldest_not_flushed_lsa);

  vacuum_er_log (VACUUM_ER_LOG_VACUUM_DATA,
		 "VACUUM: Vacuum data was successfully flushed at LSA "
		 "(%lld, %d). Reset vacuum_Data_oldest_not_flushed_lsa to "
		 "(%lld, %d).",
		 (long long int) vacuum_Data->crt_lsa.pageid,
		 (int) vacuum_Data->crt_lsa.offset,
		 (long long int) vacuum_Data_oldest_not_flushed_lsa.pageid,
		 (int) vacuum_Data_oldest_not_flushed_lsa.offset);

  if (!is_vacuum_data_locked)
    {
      VACUUM_UNLOCK_DATA ();
    }

  return NO_ERROR;
}

/*
 * vacuum_create_file_for_vacuum_data () - Create a disk file to keep vacuum
 *					   data.
 *
 * return		   : Error code.
 * thread_p (in)	   : Thread entry.
 * vacuum_data_npages (in) : Number of vacuum data disk pages.
 * vacuum_data_vfid (out)  : Created file VFID.
 */
int
vacuum_create_file_for_vacuum_data (THREAD_ENTRY * thread_p,
				    int vacuum_data_npages,
				    VFID * vacuum_data_vfid)
{
  int vol_fd;
  VPID first_page_vpid;
  VACUUM_DATA *vacuum_data_p;
  char vacuum_data_buf[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT];

  /* Create disk file to keep vacuum data */
  if (file_create (thread_p, vacuum_data_vfid, vacuum_data_npages,
		   FILE_VACUUM_DATA, NULL, &first_page_vpid,
		   -vacuum_data_npages) == NULL)
    {
      assert (false);
      return ER_FAILED;
    }

  /* Initialize vacuum data */
  vacuum_data_p = (VACUUM_DATA *) PTR_ALIGN (vacuum_data_buf, MAX_ALIGNMENT);

  LSA_SET_NULL (&vacuum_data_p->crt_lsa);
  vacuum_data_p->last_blockid = VACUUM_NULL_LOG_BLOCKID;
  vacuum_data_p->newest_mvccid = MVCCID_ALL_VISIBLE;
  vacuum_data_p->oldest_mvccid = MVCCID_ALL_VISIBLE;
  vacuum_data_p->n_table_entries = 0;

  LSA_SET_NULL (&vacuum_data_p->first_blocks_to_recover);
  LSA_SET_NULL (&vacuum_data_p->last_blocks_to_recover);
  vacuum_data_p->n_blocks_to_recover = 0;
  vacuum_data_p->n_first_blocks_to_recover = 0;

  vacuum_data_p->log_block_npages =
    prm_get_integer_value (PRM_ID_VACUUM_LOG_BLOCK_PAGES);

  /* Write initialized vacuum data to disk */
  vol_fd = fileio_get_volume_descriptor (first_page_vpid.volid);
  if (fileio_write (thread_p, vol_fd, vacuum_data_p, first_page_vpid.pageid,
		    IO_PAGESIZE) == NULL)
    {
      assert (false);
      return ER_FAILED;
    }

  return NO_ERROR;
}

/*
 * vacuum_create_file_for_dropped_files () - Create a disk file to track
 *					     dropped files for vacuum.
 *
 * return		  : Error code.
 * thread_p (in)	  : Thread entry.
 * vacuum_data_vfid (out) : Created file VFID.
 */
int
vacuum_create_file_for_dropped_files (THREAD_ENTRY * thread_p,
				      VFID * dropped_files_vfid)
{
  VPID first_page_vpid;
  VACUUM_DROPPED_FILES_PAGE *dropped_files_page = NULL;

  /* Create disk file to keep dropped files */
  if (file_create (thread_p, dropped_files_vfid, 1, FILE_DROPPED_FILES, NULL,
		   &first_page_vpid, 1) == NULL)
    {
      assert (false);
      return ER_FAILED;
    }

  /* Load first page of file */
  dropped_files_page =
    vacuum_fix_dropped_entries_page (thread_p, &first_page_vpid,
				     PGBUF_LATCH_WRITE);
  if (dropped_files_page == NULL)
    {
      assert (false);
      return ER_FAILED;
    }

  /* Initialize dropped files */
  /* Pack VPID of next page as NULL OID and count as 0 */
  VPID_SET_NULL (&dropped_files_page->next_page);
  dropped_files_page->n_dropped_files = 0;

  pgbuf_set_page_ptype (thread_p, (PAGE_PTR) dropped_files_page,
			PAGE_DROPPED_FILES);

  /* Set dirty page and free */
  vacuum_set_dirty_dropped_entries_page (thread_p, dropped_files_page, FREE);

  return NO_ERROR;
}

/*
 * vacuum_set_vacuum_data_lsa () - Called by log manager, sets vacuum data log
 *				   lsa whenever changes on vacuum data are
 *				   logged.
 *
 * return		: Void.
 * thread_p (in)	: Thread entry.
 * vacuum_data_lsa (in) : Log lsa for the latest change on vacuum data.
 * rcvindex (in)	: Recovery index for operation on vacuum data.
 */
void
vacuum_set_vacuum_data_lsa (THREAD_ENTRY * thread_p,
			    LOG_LSA * vacuum_data_lsa, LOG_RCVINDEX rcvindex)
{
  if (prm_get_bool_value (PRM_ID_DISABLE_VACUUM))
    {
      return;
    }

  assert (vacuum_data_lsa != NULL);

  if (rcvindex == RVVAC_LOG_BLOCK_SAVE)
    {
      /* Also update first/last blocks to recover */
      if (LSA_ISNULL (&vacuum_Data->first_blocks_to_recover))
	{
	  /* Initialize first blocks to recover from log */
	  LSA_COPY (&vacuum_Data->first_blocks_to_recover, vacuum_data_lsa);
	}
      /* Initialize/Update last blocks to recover from log */
      LSA_COPY (&vacuum_Data->last_blocks_to_recover, vacuum_data_lsa);

      vacuum_er_log (VACUUM_ER_LOG_VACUUM_DATA,
		     "VACUUM: Vacuum data crt_lsa = (%lld, %d) - "
		     "first_blocks_to_recover = (%lld, %d), "
		     "last_blocks_to_recover = (%lld, %d)",
		     (long long int) vacuum_Data->crt_lsa.pageid,
		     (int) vacuum_Data->crt_lsa.offset,
		     (long long int)
		     vacuum_Data->first_blocks_to_recover.pageid,
		     (int) vacuum_Data->first_blocks_to_recover.offset,
		     (long long int)
		     vacuum_Data->last_blocks_to_recover.pageid,
		     (int) vacuum_Data->last_blocks_to_recover.offset);
    }

  /* Update current vacuum data LSA */
  LSA_COPY (&vacuum_Data->crt_lsa, vacuum_data_lsa);

  vacuum_er_log (VACUUM_ER_LOG_VACUUM_DATA,
		 "VACUUM: Set vacuum data lsa to (%lld, %d).",
		 (long long int) vacuum_data_lsa->pageid,
		 (int) vacuum_data_lsa->offset);

  assert (!LSA_ISNULL (&vacuum_Data->crt_lsa));

  if (LSA_ISNULL (&vacuum_Data_oldest_not_flushed_lsa))
    {
      LSA_COPY (&vacuum_Data_oldest_not_flushed_lsa, &vacuum_Data->crt_lsa);

      vacuum_er_log (VACUUM_ER_LOG_VACUUM_DATA,
		     "VACUUM: Updated vacuum data's oldest unflushed lsa to "
		     "(%lld, %d).",
		     (long long int)
		     vacuum_Data_oldest_not_flushed_lsa.pageid,
		     (int) vacuum_Data_oldest_not_flushed_lsa.offset);
    }
  else
    {
      vacuum_er_log (VACUUM_ER_LOG_VACUUM_DATA,
		     "VACUUM: No need to update vacuum data oldest unflushed "
		     "lsa. It already is (%lld, %d).",
		     (long long int)
		     vacuum_Data_oldest_not_flushed_lsa.pageid,
		     (int) vacuum_Data_oldest_not_flushed_lsa.offset);

      assert (LSA_LT (&vacuum_Data_oldest_not_flushed_lsa,
		      &vacuum_Data->crt_lsa));
    }
}

/*
 * vacuum_get_vacuum_data_lsa () - Called by log manager to check vacuum data
 *				   lsa for recovery.
 *
 * return		 : Void.
 * thread_p (in)	 : Thread entry.
 * vacuum_data_lsa (out) : Pointer to log lsa where vacuum data lsa is saved.
 */
void
vacuum_get_vacuum_data_lsa (THREAD_ENTRY * thread_p,
			    LOG_LSA * vacuum_data_lsa)
{
  if (prm_get_bool_value (PRM_ID_DISABLE_VACUUM))
    {
      LSA_SET_NULL (vacuum_data_lsa);
      return;
    }

  assert (vacuum_data_lsa != NULL);

  LSA_COPY (vacuum_data_lsa, &vacuum_Data->crt_lsa);
}

/*
 * vacuum_is_work_in_progress () - Returns true if there are any vacuum jobs
 *				   running.
 *
 * return	 : True if there is any job in progress, false otherwise.
 * thread_p (in) : Thread entry.
 *
 * NOTE: If this is not called by the auto vacuum master thread, it is
 *	 recommended to obtain lock on vacuum data first.
 */
static bool
vacuum_is_work_in_progress (THREAD_ENTRY * thread_p)
{
  int i;

  if (vacuum_Data == NULL)
    {
      /* Vacuum data was not loaded */
      return false;
    }

  for (i = 0; i < vacuum_Assigned_workers_count; i++)
    {
      if (vacuum_Workers[i].state != VACUUM_WORKER_STATE_INACTIVE)
	{
	  return true;
	}
    }

  /* No running jobs, return false */
  return false;
}

/*
 * vacuum_data_remove_entries () - Remove given vacuum data entries.
 *
 * return		  : Void.
 * thread_p (in)	  : Thread entry.
 * n_removed_entries (in) : Number of entries to be removed.
 * removed_entries (in)	  : Indexes of entries to be removed.
 */
static void
vacuum_data_remove_entries (THREAD_ENTRY * thread_p, int n_removed_entries,
			    int *removed_entries)
{
#define TEMP_BUFFER_SIZE 1024
  VACUUM_DATA_ENTRY temp_buffer[TEMP_BUFFER_SIZE], *entry = NULL;
  int mem_size = 0, temp_buffer_mem_size = sizeof (temp_buffer);
  int i, table_index, j;
  int start_index = 0, n_successive = 1;

  if (n_removed_entries == 0)
    {
      return;
    }

  vacuum_er_log (VACUUM_ER_LOG_VACUUM_DATA,
		 "VACUUM: Vacuum data crt_lsa = (%lld, %d) - "
		 "n_table_entries = %d before removing entries.",
		 (long long int) vacuum_Data->crt_lsa.pageid,
		 (int) vacuum_Data->crt_lsa.offset,
		 vacuum_Data->n_table_entries);

  for (i = 0; i < n_removed_entries; i++)
    {
      /* Get table index of entry being removed */
      table_index = removed_entries[i];

      /* Make sure that indexes in removed entries are in descending order */
      assert (i == (n_removed_entries - 1)
	      || table_index > removed_entries[i + 1]);

      /* Get entry at table_index */
      entry = VACUUM_DATA_GET_ENTRY (table_index);

      if (i < n_removed_entries - 1
	  && table_index == removed_entries[i + 1] + 1)
	{
	  /* Successive entries are being removed. Group their removal. */
	  n_successive++;
	  continue;
	}

      /* Get starting index for current group. If no successive entries were
       * found, starting index will be same as table index and n_successive
       * will be 1.
       */
      start_index = table_index;

      if (VACUUM_IS_ER_LOG_LEVEL_SET (VACUUM_ER_LOG_VACUUM_DATA))
	{
	  for (j = start_index; j < n_successive; j++)
	    {
	      vacuum_er_log (VACUUM_ER_LOG_VACUUM_DATA,
			     "VACUUM: Vacuum data crt_lsa = (%lld, %d) - "
			     "remove block = "
			     "{start_lsa = (%lld, %d), "
			     "oldest_mvccid = %llu, newest_mvccid = %llu, "
			     "blockid = %lld",
			     (long long int) vacuum_Data->crt_lsa.pageid,
			     (int) vacuum_Data->crt_lsa.offset,
			     (long long int)
			     VACUUM_DATA_GET_ENTRY (j)->start_lsa.pageid,
			     (int)
			     VACUUM_DATA_GET_ENTRY (j)->start_lsa.offset,
			     VACUUM_DATA_GET_ENTRY (j)->oldest_mvccid,
			     VACUUM_DATA_GET_ENTRY (j)->newest_mvccid,
			     VACUUM_DATA_ENTRY_BLOCKID
			     (VACUUM_DATA_GET_ENTRY (j)));
	    }
	}

      /* Compute the size of memory data being moved */
      mem_size =
	(vacuum_Data->n_table_entries - (start_index + n_successive))
	* sizeof (VACUUM_DATA_ENTRY);

      assert (mem_size >= 0);

      if (mem_size > 0)
	{
	  /* Move memory data */
	  if (mem_size <= temp_buffer_mem_size)
	    {
	      /* Use temporary buffer to move memory data */
	      memcpy (temp_buffer,
		      VACUUM_DATA_GET_ENTRY (start_index + n_successive),
		      mem_size);
	      memcpy (VACUUM_DATA_GET_ENTRY (start_index), temp_buffer,
		      mem_size);
	    }
	  else
	    {
	      /* Use memmove */
	      memmove (VACUUM_DATA_GET_ENTRY (start_index),
		       VACUUM_DATA_GET_ENTRY (start_index + n_successive),
		       mem_size);
	    }
	}

      vacuum_Data->n_table_entries -= n_successive;

      /* Reset successive removed entries to 1 */
      n_successive = 1;
    }

  vacuum_er_log (VACUUM_ER_LOG_VACUUM_DATA,
		 "VACUUM: Vacuum data crt_lsa = (%lld, %d) - "
		 "n_table_entries = %d after removing entries.",
		 (long long int) vacuum_Data->crt_lsa.pageid,
		 (int) vacuum_Data->crt_lsa.offset,
		 vacuum_Data->n_table_entries);

  VACUUM_VERIFY_VACUUM_DATA ();
}

/*
 * vacuum_data_remove_finished_entries () - Remove vacuumed entries from
 *					    vacuum data table.
 *
 * return	 : Void.
 * thread_p (in) : Index of entry being removed.
 */
static void
vacuum_data_remove_finished_entries (THREAD_ENTRY * thread_p)
{
#define TEMP_BUFFER_SIZE 1024
  int removed_indexes[TEMP_BUFFER_SIZE];
  int index, n_removed_indexes = 0;
  int n_removed_indexes_capacity = TEMP_BUFFER_SIZE;
  int *removed_indexes_p = NULL, *new_removed_indexes = NULL;
  int mem_size;

  removed_indexes_p = removed_indexes;

  /* Search for vacuumed blocks and remove them for vacuum data */
  for (index = vacuum_Data->n_table_entries - 1; index >= 0; index--)
    {
      if (VACUUM_BLOCK_STATUS_IS_VACUUMED (VACUUM_DATA_GET_ENTRY (index)))
	{
	  /* Save index of entry to be removed */
	  if (n_removed_indexes >= n_removed_indexes_capacity)
	    {
	      /* Realloc removed indexes buffer */
	      mem_size = sizeof (int) * 2 * n_removed_indexes_capacity;

	      if (removed_indexes == removed_indexes_p)
		{
		  new_removed_indexes = (int *) malloc (mem_size);
		  if (new_removed_indexes != NULL)
		    {
		      /* copy values from stack array to alloced array */
		      memcpy (new_removed_indexes, removed_indexes,
			      n_removed_indexes *
			      sizeof (removed_indexes[0]));
		    }
		}
	      else
		{
		  new_removed_indexes =
		    (int *) realloc (removed_indexes_p, mem_size);
		}

	      if (new_removed_indexes == NULL)
		{
		  assert_release (false);
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			  ER_OUT_OF_VIRTUAL_MEMORY, 1,
			  sizeof (int) * 2 * n_removed_indexes_capacity);
		  goto end;
		}

	      removed_indexes_p = new_removed_indexes;
	      n_removed_indexes_capacity *= 2;
	    }

	  removed_indexes_p[n_removed_indexes++] = index;
	}
    }

  if (n_removed_indexes > 0)
    {
      /* Remove entries from vacuum data */
      vacuum_data_remove_entries (thread_p, n_removed_indexes,
				  removed_indexes_p);

      /* Log removed data entries */
      vacuum_log_remove_data_entries (thread_p, removed_indexes_p,
				      n_removed_indexes);
    }

end:
  if (removed_indexes_p != removed_indexes)
    {
      free (removed_indexes_p);
    }
}

/*
 * vacuum_consume_buffer_log_blocks () - Append new blocks from log block
 *					     data from buffer (if any).
 *
 * return	 : error code.
 * thread_p (in) : Thread entry.
 *
 * NOTE: In order to avoid synchronizing access on vacuum data for log
 *	 manager, information on new blocks is appended into a lock-free
 *	 buffer. This information can be later obtained and appended to
 *	 vacuum data.
 */
int
vacuum_consume_buffer_log_blocks (THREAD_ENTRY * thread_p)
{
  int save_n_entries;
  VACUUM_DATA_ENTRY *entry = NULL;

  if (prm_get_bool_value (PRM_ID_DISABLE_VACUUM))
    {
      return NO_ERROR;
    }

  if (vacuum_Block_data_buffer == NULL)
    {
      /* Not initialized */
      return NO_ERROR;
    }

  if (vacuum_recover_blocks_from_log (thread_p) != NO_ERROR)
    {
      /* Failed to recover blocks from log */
      assert_release (false);
      return ER_FAILED;
    }

  if (LOCK_FREE_CIRCULAR_QUEUE_IS_EMPTY (vacuum_Block_data_buffer))
    {
      /* Nothing else to consume */
      return NO_ERROR;
    }

  /* If there are no blocks saved to log, consume buffer directly to vacuum
   * data.
   */
  if (LSA_ISNULL (&vacuum_Data->last_blocks_to_recover))
    {
      /* Consume blocks data from buffer and append them to vacuum data */
      save_n_entries = vacuum_Data->n_table_entries;
      while (vacuum_Data->n_table_entries < VACUUM_DATA_TABLE_MAX_SIZE)
	{
	  /* Position entry pointer at the end of the vacuum data table */
	  entry = VACUUM_DATA_GET_ENTRY (vacuum_Data->n_table_entries);

	  /* Get a block from buffer */
	  if (!lf_circular_queue_consume (vacuum_Block_data_buffer, entry))
	    {
	      /* Buffer is empty */
	      break;
	    }

	  if (VACUUM_DATA_ENTRY_BLOCKID (entry) <= vacuum_Data->last_blockid)
	    {
	      /* Duplicates are not expected, something is wrong */
	      assert (false);
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	      return ER_FAILED;
	    }

	  vacuum_er_log (VACUUM_ER_LOG_VACUUM_DATA,
			 "VACUUM: thread(%d) calls: "
			 "vacuum_consume_buffer_log_blocks (): "
			 "blockid=(%lld), start_lsa=(%lld, %d), "
			 "old_mvccid=(%llu), new_mvccid=(%llu)\n",
			 thread_get_current_entry_index (),
			 entry->blockid,
			 (long long int) entry->start_lsa.pageid,
			 (int) entry->start_lsa.offset, entry->oldest_mvccid,
			 entry->newest_mvccid);

	  vacuum_Data->n_table_entries++;

	  assert (vacuum_Data->last_blockid < entry->blockid);

	  vacuum_Data->last_blockid = entry->blockid;

	  if (MVCC_ID_PRECEDES (vacuum_Data->newest_mvccid,
				entry->newest_mvccid))
	    {
	      vacuum_Data->newest_mvccid = entry->newest_mvccid;
	    }
	}

      if (save_n_entries < vacuum_Data->n_table_entries)
	{
	  /* New blocks have been appended and must be logged */
	  vacuum_log_append_block_data (thread_p,
					VACUUM_DATA_GET_ENTRY
					(save_n_entries),
					vacuum_Data->n_table_entries -
					save_n_entries, NULL, 0);
	}
    }

  if (vacuum_Data->n_table_entries == VACUUM_DATA_TABLE_MAX_SIZE)
    {
      while (!LOCK_FREE_CIRCULAR_QUEUE_IS_EMPTY (vacuum_Block_data_buffer))
	{
	  /* Couldn't fit entire buffer into vacuum data. Save blocks to log
	   * to recover later.
	   */
	  /* Blocks are saved in chunks and the buffer may not be consumed
	   * entirely in one iteration. Loop until the entire buffer is
	   * consumed.
	   */
	  vacuum_log_blocks_to_recover (thread_p);
	}
    }

  VACUUM_VERIFY_VACUUM_DATA ();

  return NO_ERROR;
}

/*
 * vacuum_rv_discard_recovered_blocks_from_buffer () - Called during recovery
 *						       to discard blocks
 *						       already recovered by
 *						       vacuum data from the
 *						       buffer.
 *
 * return	 : Void.
 * thread_p (in) : Thread entry.
 *
 * NOTE: This is a recovery function.
 */
static void
vacuum_rv_discard_recovered_blocks_from_buffer (THREAD_ENTRY * thread_p)
{
  VACUUM_DATA_ENTRY *entry = NULL;

  assert (!LOG_ISRESTARTED ());

  /* Discard all blocks before vacuum_Data->last_blockid. */
  while (!LOCK_FREE_CIRCULAR_QUEUE_IS_EMPTY (vacuum_Block_data_buffer))
    {
      entry =
	(VACUUM_DATA_ENTRY *)
	lf_circular_queue_async_peek (vacuum_Block_data_buffer);
      assert (entry != NULL);
      if (entry->blockid > vacuum_Data->last_blockid)
	{
	  /* Stop discarding blocks. */
	  break;
	}
      /* Remove from buffer. */
      vacuum_er_log (VACUUM_ER_LOG_RECOVERY | VACUUM_ER_LOG_VACUUM_DATA,
		     "VACUUM: Discard recovered block from buffer - "
		     "blockid = %lld, start_lsa = (%lld, %d), "
		     "oldest_mvccid = %lld, newest_mvccid = %lld.",
		     entry->blockid, (long long int) entry->start_lsa.pageid,
		     (int) entry->start_lsa.offset, entry->oldest_mvccid,
		     entry->newest_mvccid);
      (void) lf_circular_queue_consume (vacuum_Block_data_buffer, NULL);
    }
}

/*
 * vacuum_rv_finish_vacuum_data_recovery () - 1. Recover block buffer to
 *						 vacuum data. Checkpoint block
 *						 may require additional log
 *						 processing if it was not
 *						 already recovered.
 *					      2. Update vacuum data oldest
 *						 MVCCID.
 *
 * return			  : Error code.
 * thread_p (in)		  : Thread entry.
 * is_chkpt_block_incomplete (in) : True if checkpoint block may be
 *				    incompletely recovered.
 * chkpt_blockid (in)		  : Checkpoint blockid.
 * chkpt_lsa (in)		  : Checkpoint lsa.
 * chkpt_block_first_lsa (in)	  : First recovered log record of checkpoint
 *				    block.
 * chkpt_block_start_lsa (in)	  : Start LSA of partially recovered
 *				    checkpoint block.
 * chkpt_block_oldest_mvccid (in) : Oldest MVCCID of partially recovered
 *				    checkpoint block.
 * chkpt_block_newest_mvccid (in) : Newest MVCCID of partially recovered
 *				    checkpoint block.
 */
int
vacuum_rv_finish_vacuum_data_recovery (THREAD_ENTRY * thread_p,
				       bool is_chkpt_block_incomplete,
				       VACUUM_LOG_BLOCKID chkpt_blockid,
				       const LOG_LSA * chkpt_lsa,
				       LOG_LSA * chkpt_block_first_lsa,
				       LOG_LSA * chkpt_block_start_lsa,
				       MVCCID chkpt_block_oldest_mvccid,
				       MVCCID chkpt_block_newest_mvccid)
{
  int error_code = NO_ERROR;
  char log_page_buf[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT];
  LOG_LSA log_lsa;
  LOG_RECORD_HEADER *log_rec_header = NULL;
  LOG_PAGE *log_page_p = NULL;
  LOG_PAGEID stop_at_pageid;
  VACUUM_DATA_ENTRY chkpt_entry;
  struct log_data dummy_log_data;
  struct log_vacuum_info vacuum_info;
  MVCCID mvccid;

  assert (chkpt_lsa != NULL);
  assert (chkpt_block_first_lsa != NULL);
  assert (chkpt_block_start_lsa != NULL);
  assert (!LOG_ISRESTARTED ());

  /* Initialize vacuum_Global_oldest_active_mvccid. Since we don't have any
   * active transactions yet, initialize it as maximum of
   * vacuum_Data->newest_mvccid and log_Gl.hdr.last_block_oldest_mvccid .
   */
  vacuum_Global_oldest_active_mvccid = vacuum_Data->newest_mvccid;
  if (!LSA_ISNULL (&log_Gl.hdr.mvcc_op_log_lsa))
    {
      vacuum_Global_oldest_active_mvccid =
	MAX (vacuum_Global_oldest_active_mvccid,
	     log_Gl.hdr.last_block_oldest_mvccid);
    }

  /* Initialize log_page_p. */
  log_page_p = (LOG_PAGE *) PTR_ALIGN (log_page_buf, MAX_ALIGNMENT);
  log_page_p->hdr.logical_pageid = NULL_PAGEID;
  log_page_p->hdr.offset = NULL_OFFSET;

  vacuum_er_log (VACUUM_ER_LOG_RECOVERY | VACUUM_ER_LOG_VACUUM_DATA,
		 "VACUUM: Finish recovery of vacuum data: "
		 "is_chkpt_block_incomplete = %s, chkpt_blockid = %lld, "
		 "chkpt_lsa = %lld|%d, chkpt_block_first_lsa = %lld|%d, "
		 "chkpt_block_start_lsa = %lld|%d, "
		 "chkpt_block_oldest_mvccid = %llu, "
		 "chkpt_block_newest_mvccid = %llu.\n",
		 is_chkpt_block_incomplete ? "true" : "false",
		 (long long int) chkpt_blockid,
		 (long long int) chkpt_lsa->pageid,
		 (int) chkpt_lsa->offset,
		 (long long int) chkpt_block_first_lsa->pageid,
		 (int) chkpt_block_first_lsa->offset,
		 (long long int) chkpt_block_start_lsa->pageid,
		 (int) chkpt_block_start_lsa->offset,
		 (unsigned long long int) chkpt_block_oldest_mvccid,
		 (unsigned long long int) chkpt_block_newest_mvccid);

  if (!is_chkpt_block_incomplete
      || chkpt_blockid <= vacuum_Data->last_blockid)
    {
      /* Checkpoint_block was completely recovered. Just consume remaining
       * entries in buffer.
       */
      error_code = vacuum_consume_buffer_log_blocks (thread_p);
      if (error_code != NO_ERROR)
	{
	  logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
			     "vacuum_rv_finish_vacuum_data_recovery");
	  return error_code;
	}

      /* Finished vacuum data recover. Update oldest MVCCID. */
      vacuum_update_oldest_mvccid (thread_p);
      return NO_ERROR;
    }

  /* Checkpoint block is not completely recovered. */
  chkpt_entry.blockid = chkpt_blockid;
  /* Set stop condition for block recovery. */
  stop_at_pageid = VACUUM_FIRST_LOG_PAGEID_IN_BLOCK (chkpt_blockid);

  if (LSA_ISNULL (chkpt_block_first_lsa))
    {
      /* No MVCC operations was found in checkpoint block after checkpoint
       * LSA. Try to find one before checkpoint LSA.
       */
      LSA_SET_NULL (&chkpt_entry.start_lsa);
      chkpt_entry.oldest_mvccid = MVCCID_NULL;
      chkpt_entry.newest_mvccid = MVCCID_NULL;

      /* Search checkpoint block for MVCC undo or undoredo log records. Stop
       * when found one.
       */
      LSA_COPY (&log_lsa, chkpt_lsa);
      while (log_lsa.pageid >= stop_at_pageid)
	{
	  if (log_page_p->hdr.logical_pageid != log_lsa.pageid)
	    {
	      /* Get log page. */
	      if (logpb_fetch_page (thread_p, log_lsa.pageid, log_page_p)
		  == NULL)
		{
		  logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
				     "vacuum_rv_finish_vacuum_data_recovery");
		  return ER_FAILED;
		}
	    }
	  log_rec_header = LOG_GET_LOG_RECORD_HEADER (log_page_p, &log_lsa);
	  if (log_rec_header->type == LOG_MVCC_UNDO_DATA
	      || log_rec_header->type == LOG_MVCC_UNDOREDO_DATA
	      || log_rec_header->type == LOG_MVCC_DIFF_UNDOREDO_DATA)
	    {
	      LSA_COPY (chkpt_block_first_lsa, &log_lsa);
	      LSA_COPY (&chkpt_entry.start_lsa, &log_lsa);
	      break;
	    }
	  LSA_COPY (&log_lsa, &log_rec_header->back_lsa);
	}
      if (LSA_ISNULL (chkpt_block_first_lsa))
	{
	  /* Nothing to recover from checkpoint block. No MVCC op log records
	   * have been found.
	   */
	  return NO_ERROR;
	}
    }
  else
    {
      /* Start processing log from chkpt_block_first_lsa. Initialize the
       * block data with already recovered data.
       */
      assert (!LSA_ISNULL (chkpt_block_start_lsa));
      assert (MVCCID_IS_VALID (chkpt_block_oldest_mvccid));
      assert (MVCCID_IS_VALID (chkpt_block_newest_mvccid));

      LSA_COPY (&chkpt_entry.start_lsa, chkpt_block_start_lsa);
      chkpt_entry.oldest_mvccid = chkpt_block_oldest_mvccid;
      chkpt_entry.newest_mvccid = chkpt_block_newest_mvccid;
    }

  /* We have at least one MVCC op log record. Check all remaining block
   * records and update block data.
   */
  LSA_COPY (&log_lsa, chkpt_block_first_lsa);
  while (log_lsa.pageid >= stop_at_pageid)
    {
      if (log_page_p->hdr.logical_pageid != log_lsa.pageid)
	{
	  if (logpb_fetch_page (thread_p, log_lsa.pageid, log_page_p) == NULL)
	    {
	      logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
				 "vacuum_rv_finish_vacuum_data_recovery");
	      return ER_FAILED;
	    }
	}
      error_code =
	vacuum_process_log_record (thread_p, NULL, &log_lsa, log_page_p,
				   &dummy_log_data, &mvccid, NULL, NULL,
				   &vacuum_info, NULL, true);
      if (chkpt_entry.oldest_mvccid == MVCCID_NULL
	  || MVCC_ID_PRECEDES (mvccid, chkpt_entry.oldest_mvccid))
	{
	  chkpt_entry.oldest_mvccid = mvccid;
	}
      if (chkpt_entry.newest_mvccid == MVCCID_NULL
	  || MVCC_ID_PRECEDES (chkpt_entry.newest_mvccid, mvccid))
	{
	  chkpt_entry.newest_mvccid = mvccid;
	}
      LSA_COPY (&log_lsa, &vacuum_info.prev_mvcc_op_log_lsa);
    }

  if (LSA_EQ (&log_Gl.hdr.mvcc_op_log_lsa, chkpt_block_start_lsa))
    {
      /* Do not produce the block yet. Update header oldest and newest MVCCID
       * and the block will be produced when a different block is created.
       */
      assert (LOCK_FREE_CIRCULAR_QUEUE_IS_EMPTY (vacuum_Block_data_buffer));
      log_Gl.hdr.last_block_oldest_mvccid = chkpt_entry.oldest_mvccid;
      log_Gl.hdr.last_block_newest_mvccid = chkpt_entry.newest_mvccid;
      return NO_ERROR;
    }

  assert (vacuum_get_log_blockid (log_Gl.hdr.mvcc_op_log_lsa.pageid)
	  > chkpt_blockid);
  /* Add recovered data in buffer before current entries (to keep entries
   * ordered by blockid).
   */
  if (!lf_circular_queue_async_push_ahead (vacuum_Block_data_buffer,
					   &chkpt_entry))
    {
      /* Full buffer */
      vacuum_er_log (VACUUM_ER_LOG_ERROR | VACUUM_ER_LOG_VACUUM_DATA
		     | VACUUM_ER_LOG_RECOVERY,
		     "VACUUM ERROR: Couldn't produce checkpoint block. "
		     "Block buffer is full.");
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
			 "vacuum_rv_finish_vacuum_data_recovery");
      return ER_FAILED;
    }

  /* Add all blocks to vacuum data. */
  error_code = vacuum_consume_buffer_log_blocks (thread_p);
  if (error_code != NO_ERROR)
    {
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
			 "vacuum_rv_finish_vacuum_data_recovery");
      return error_code;
    }

  /* Finished vacuum data recover. Update oldest MVCCID. */
  vacuum_update_oldest_mvccid (thread_p);
  return NO_ERROR;
}

/*
 * vacuum_log_blocks_to_recover () - Log vacuum data entries that couldn't fit
 *				     into vacuum data. They will be recovered
 *				     when there is enough free space.
 *
 * return		 : Void.
 * thread_p (in)	 : Thread entry.
 */
static void
vacuum_log_blocks_to_recover (THREAD_ENTRY * thread_p)
{
#define VACUUM_LOG_BLOCKS_TO_RECOVER_REDO_CRUMBS_MAX 2

  int n_blocks = 0;
  VACUUM_DATA_ENTRY recover_blocks[VACUUM_BLOCK_DATA_BUFFER_CAPACITY];
  LOG_CRUMB redo_crumbs[VACUUM_LOG_BLOCKS_TO_RECOVER_REDO_CRUMBS_MAX];
  int n_redo_crumbs = 0;
  LOG_DATA_ADDR addr;

  /* Consume up to VACUUM_RECOVER_BLOCKS_MAX_COUNT entries from buffer.
   * The buffer is not consumed entirely in one iteration, to avoid adding
   * too much log data at once.
   */
  while (n_blocks < VACUUM_RECOVER_BLOCKS_MAX_COUNT)
    {
      if (!lf_circular_queue_consume
	  (vacuum_Block_data_buffer, &recover_blocks[n_blocks]))
	{
	  /* No more entries */
	  break;
	}
      if (recover_blocks[n_blocks].blockid <= vacuum_Data->last_blockid)
	{
	  /* Duplicates are not expected */
	  vacuum_er_log (VACUUM_ER_LOG_ERROR | VACUUM_ER_LOG_VACUUM_DATA,
			 "VACUUM ERROR: Duplicate block ID.");
	  assert_release (false);
	  return;
	}

      /* Update last_blockid since we still need it to skip duplicate objects
       * when block data buffer is recovered.
       */
      vacuum_Data->last_blockid = recover_blocks[n_blocks].blockid;

      /* Increment number of consumed blocks */
      n_blocks++;
    }

  if (n_blocks == 0)
    {
      /* No entries */
      return;
    }

  vacuum_er_log (VACUUM_ER_LOG_WARNING | VACUUM_ER_LOG_VACUUM_DATA,
		 "VACUUM WARNING: %d vacuum data entries saved to log.",
		 n_blocks);

  /* Update total number of blocks to be recovered */
  vacuum_Data->n_blocks_to_recover += n_blocks;
  if (LSA_ISNULL (&vacuum_Data->first_blocks_to_recover))
    {
      /* This is the first block, update also n_first_blocks_to_recover */
      vacuum_Data->n_first_blocks_to_recover = n_blocks;
    }

  vacuum_er_log (VACUUM_ER_LOG_VACUUM_DATA,
		 "VACUUM: Vacuum data crt_lsa = (%lld, %d) - "
		 "n_blocks_to_recover = %d, n_first_blocks_to_recover = %d.",
		 (long long int) vacuum_Data->crt_lsa.pageid,
		 (int) vacuum_Data->crt_lsa.offset,
		 vacuum_Data->n_blocks_to_recover,
		 vacuum_Data->n_first_blocks_to_recover);

  /* Initialize log address data */
  addr.pgptr = NULL;
  addr.vfid = NULL;
  addr.offset = n_blocks;

  /* Initialize redo crumbs */

  /* Add LOG_LSA of previous blocks to recover */
  redo_crumbs[n_redo_crumbs].length = sizeof (LOG_LSA);
  redo_crumbs[n_redo_crumbs++].data = &vacuum_Data->last_blocks_to_recover;

  /* Add blocks that need to be saved to log and recovered later */
  redo_crumbs[n_redo_crumbs].length = n_blocks * sizeof (VACUUM_DATA_ENTRY);
  redo_crumbs[n_redo_crumbs++].data = recover_blocks;

  /* Safe guard */
  assert (n_redo_crumbs <= VACUUM_LOG_BLOCKS_TO_RECOVER_REDO_CRUMBS_MAX);

  /* Log the entries to recover later. Note that when vacuum data LSA is
   * updated, also LSA of first/last blocks to recover will be updated.
   */
  log_append_redo_crumbs (thread_p, RVVAC_LOG_BLOCK_SAVE, &addr,
			  n_redo_crumbs, redo_crumbs);
}

/*
 * vacuum_rv_redo_save_blocks () - Update number of blocks that need to be
 *				   recovered.
 *
 * return	 : NO_ERROR.
 * thread_p (in) : Thread entry.
 * rcv (in)	 : Recovery data.
 */
int
vacuum_rv_redo_save_blocks (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  VACUUM_DATA_ENTRY *entry;
  int offset = 0;
  assert (rcv->offset > 0);

  /* Update the number of blocks that need to be recovered */
  vacuum_Data->n_blocks_to_recover += rcv->offset;

  if (LSA_ISNULL (&vacuum_Data->first_blocks_to_recover))
    {
      /* Since this must be the first log entry for saved blocks, update also
       * vacuum_Data->n_first_blocks_to_recover.
       */
      vacuum_Data->n_first_blocks_to_recover = rcv->offset;
    }

  /* Update vacuum_Data->last_blockid. Get last logged blockid. */
  /* Skip LOG_LSA. */
  offset += sizeof (LOG_LSA);
  /* Skip rcv->offset - 1 blocks */
  offset += (rcv->offset - 1) * sizeof (VACUUM_DATA_ENTRY);
  /* Get last entry blockid and save to vacuum_Data->last_blockid. */
  entry = (VACUUM_DATA_ENTRY *) (rcv->data + offset);
  vacuum_Data->last_blockid = VACUUM_DATA_ENTRY_BLOCKID (entry);
  /* Discard recovered blocks from buffer. */
  vacuum_rv_discard_recovered_blocks_from_buffer (thread_p);

  vacuum_er_log (VACUUM_ER_LOG_VACUUM_DATA | VACUUM_ER_LOG_RECOVERY,
		 "VACUUM: Vacuum data crt_lsa = (%lld, %d) - "
		 "n_blocks_to_recover = %d, n_first_blocks_to_recover = %d.",
		 (long long int) vacuum_Data->crt_lsa.pageid,
		 (int) vacuum_Data->crt_lsa.offset,
		 vacuum_Data->n_blocks_to_recover,
		 vacuum_Data->n_first_blocks_to_recover);

  /* LSA's of first/last block to be recovered are set when vacuum data LSA is
   * set.
   */
  return NO_ERROR;
}

/*
 * vacuum_recover_blocks_from_log () - Recover blocks saved to log.
 *
 * return	 : Error code.
 * thread_p (in) : Thread entry.
 */
static int
vacuum_recover_blocks_from_log (THREAD_ENTRY * thread_p)
{
  LOG_LSA crt_lsa, log_lsa;
  LOG_PAGE *log_page_p = NULL;
  LOG_ZIP *log_unzip_p = NULL;
  LOG_RECORD_HEADER *log_rec_header_p = NULL;
  struct log_redo *redo = NULL;
  char log_pgbuf[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT];
  VACUUM_DATA_ENTRY recover_blocks[VACUUM_RECOVER_BLOCKS_BUFFER_MAX_SIZE];
  LOG_LSA first_not_recovered_blocks_lsa;
  int first_not_recovered_n_blocks;
  int remaining_blocks = vacuum_Data->n_blocks_to_recover;
  int crt_n_blocks;
  int recover_index = VACUUM_RECOVER_BLOCKS_MAX_COUNT;
  int n_recovered_blocks;
  int redo_data_length;
  int prev_n_table_entries;
  int error_code = NO_ERROR;
  int recover_blocks_max_count;
  int i;
  char
    redo_data_buffer[(VACUUM_RECOVER_BLOCKS_BUFFER_MAX_SIZE *
		      sizeof (VACUUM_DATA_ENTRY)) + sizeof (LOG_LSA) +
		     MAX_ALIGNMENT];
  char *redo_data_p = NULL;
  bool is_zip = false;

  if (LSA_ISNULL (&vacuum_Data->last_blocks_to_recover))
    {
      /* Nothing to recover */
      return NO_ERROR;
    }

  /* Compute the number of blocks we need to recover:
   * 1. VACUUM_RECOVER_BLOCKS_MAX_COUNT if they fit in vacuum data.
   * 2. The number of blocks that fit in vacuum data.
   */
  recover_blocks_max_count =
    MIN (VACUUM_RECOVER_BLOCKS_MAX_COUNT,
	 VACUUM_DATA_TABLE_MAX_SIZE - vacuum_Data->n_table_entries);

  if (recover_blocks_max_count < vacuum_Data->n_first_blocks_to_recover)
    {
      /* Freed space cannot even handle first blocks. Give up for now. */
      return NO_ERROR;
    }

  /* Allocate space to unzip redo data */
  log_unzip_p = log_zip_alloc (IO_PAGESIZE, false);
  if (log_unzip_p == NULL)
    {
      vacuum_er_log (VACUUM_ER_LOG_ERROR | VACUUM_ER_LOG_VACUUM_DATA,
		     "VACUUM ERROR: Could not allocate log zip.");
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
			 "vacuum_recover_blocks_from_log");
      error_code = ER_FAILED;
      goto end;
    }

#if defined (SERVER_MODE)
  if (thread_p != NULL && thread_p->type == TT_VACUUM_MASTER)
    {
      /* Vacuum master will start to process log */
      vacuum_Master_is_process_log_phase = true;
    }
#endif /* !SERVER_MODE */

  log_page_p = (LOG_PAGE *) PTR_ALIGN (log_pgbuf, MAX_ALIGNMENT);
  log_page_p->hdr.logical_pageid = NULL_PAGEID;
  log_page_p->hdr.offset = NULL_OFFSET;

  /* Loop to recover blocks. We need to recover first recover_blocks_max_count
   * blocks. Process the log entries in reversed order using LOG_LSA links.
   * When remaining number of blocks are less than recover_blocks_max_count,
   * copy the blocks found in log entries. Next first_blocks_to_recover LSA
   * will be updated to exclude the recovered blocks on next iteration. If
   * all blocks have been recovered, first/last blocks to recover LSA will
   * both be NULL.
   */
  LSA_SET_NULL (&first_not_recovered_blocks_lsa);
  first_not_recovered_n_blocks = 0;
  LSA_COPY (&log_lsa, &vacuum_Data->last_blocks_to_recover);
  do
    {
      /* Save current entry log_lsa */
      LSA_COPY (&crt_lsa, &log_lsa);

      /* Fetch current log page */
      if (log_page_p->hdr.logical_pageid != log_lsa.pageid)
	{
	  if (logpb_fetch_page (thread_p, crt_lsa.pageid, log_page_p) == NULL)
	    {
	      vacuum_er_log (VACUUM_ER_LOG_ERROR | VACUUM_ER_LOG_VACUUM_DATA,
			     "VACUUM ERROR: Failed to fetch page from log.");
	      logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
				 "vacuum_recover_blocks_from_log");
	      error_code = ER_FAILED;
	      goto end;
	    }
	}

      /* Get record header */
      log_rec_header_p = LOG_GET_LOG_RECORD_HEADER (log_page_p, &log_lsa);
      assert (log_rec_header_p->type == LOG_REDO_DATA);

      LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_RECORD_HEADER), &log_lsa,
			  log_page_p);

      /* Get redo structure */
      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (*redo), &log_lsa,
					log_page_p);
      redo = (struct log_redo *) ((char *) log_page_p->area + log_lsa.offset);
      assert (redo->data.rcvindex == RVVAC_LOG_BLOCK_SAVE);
      /* Consume required information from log redo structure before
       * advancing to redo recovery data.
       */
      /* Number of blocks is stored in offset */
      crt_n_blocks = redo->data.offset;
      redo_data_length = redo->length;
      LOG_READ_ADD_ALIGN (thread_p, sizeof (*redo), &log_lsa, log_page_p);

      /* Check if zipped */
      if (ZIP_CHECK (redo_data_length))
	{
	  is_zip = true;
	  redo_data_length = (int) GET_ZIP_LEN (redo_data_length);
	}
      else
	{
	  is_zip = false;
	}

      /* Get redo recovery data */
      if (log_lsa.offset + redo_data_length < (int) LOGAREA_SIZE)
	{
	  redo_data_p = (char *) log_page_p->area + log_lsa.offset;
	}
      else
	{
	  assert (redo_data_length <= (int) sizeof (redo_data_buffer));
	  redo_data_p = redo_data_buffer;
	  logpb_copy_from_log (thread_p, redo_data_p, redo_data_length,
			       &log_lsa, log_page_p);
	}

      if (is_zip)
	{
	  /* Unzip redo data */
	  if (log_unzip (log_unzip_p, redo_data_length, redo_data_p))
	    {
	      redo_data_length = (int) log_unzip_p->data_length;
	      redo_data_p = (char *) log_unzip_p->log_data;
	    }
	  else
	    {
	      vacuum_er_log (VACUUM_ER_LOG_ERROR | VACUUM_ER_LOG_VACUUM_DATA,
			     "VACUUM ERROR: Failed to unzip data.");
	      logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
				 "vacuum_recover_blocks_from_log");
	      error_code = ER_FAILED;
	      goto end;
	    }
	}

      /* Safe guard */
      assert (redo_data_length =
	      sizeof (LOG_LSA) + crt_n_blocks * sizeof (VACUUM_DATA_ENTRY));

      /* Copy LSA of previous blocks to recover */
      LSA_COPY (&log_lsa, (LOG_LSA *) redo_data_p);
      redo_data_p += sizeof (LOG_LSA);

      if (remaining_blocks <= recover_blocks_max_count)
	{
	  /* Also read blocks */
	  recover_index -= crt_n_blocks;
	  memcpy (&recover_blocks[recover_index], redo_data_p,
		  crt_n_blocks * sizeof (VACUUM_DATA_ENTRY));
	}
      else
	{
	  /* These blocks are not recovered in this iteration */
	  /* Update first not recovered blocks LSA */
	  LSA_COPY (&first_not_recovered_blocks_lsa, &crt_lsa);
	  first_not_recovered_n_blocks = crt_n_blocks;
	}

      /* Update remaining blocks */
      remaining_blocks -= crt_n_blocks;
    }
  while (!LSA_EQ (&crt_lsa, &vacuum_Data->first_blocks_to_recover));

  /* Finished processing log to recover blocks */
#if defined (SERVER_MODE)
  if (thread_p != NULL && thread_p->type == TT_VACUUM_MASTER)
    {
      /* Vacuum master completes to process log */
      vacuum_Master_is_process_log_phase = false;
    }
#endif /* !SERVER_MODE */

  /* Safe guard: all expected blocks have been processed */
  assert (remaining_blocks == 0);

  /* Add recovered blocks */
  prev_n_table_entries = vacuum_Data->n_table_entries;
  n_recovered_blocks = VACUUM_RECOVER_BLOCKS_MAX_COUNT - recover_index;
  memcpy (VACUUM_DATA_GET_ENTRY (prev_n_table_entries),
	  &recover_blocks[recover_index],
	  n_recovered_blocks * sizeof (VACUUM_DATA_ENTRY));
  vacuum_Data->n_table_entries += n_recovered_blocks;

  /* Update LSA of first blocks to recover */
  LSA_COPY (&vacuum_Data->first_blocks_to_recover,
	    &first_not_recovered_blocks_lsa);
  if (LSA_ISNULL (&first_not_recovered_blocks_lsa))
    {
      /* No blocks left to recover. Set last blocks to recover to NULL */
      LSA_SET_NULL (&vacuum_Data->last_blocks_to_recover);
    }
  /* Update the number of blocks to recover */
  vacuum_Data->n_blocks_to_recover -= n_recovered_blocks;
  assert (vacuum_Data->n_blocks_to_recover >= 0);
  assert (vacuum_Data->n_blocks_to_recover == 0
	  || !LSA_ISNULL (&vacuum_Data->last_blocks_to_recover));
  vacuum_Data->n_first_blocks_to_recover = first_not_recovered_n_blocks;

  /* Update vacuum_Data->newest_mvccid */
  for (i = prev_n_table_entries; i < vacuum_Data->n_table_entries; i++)
    {
      if (MVCC_ID_PRECEDES (vacuum_Data->newest_mvccid,
			    VACUUM_DATA_GET_ENTRY (i)->newest_mvccid))
	{
	  vacuum_Data->newest_mvccid =
	    VACUUM_DATA_GET_ENTRY (i)->newest_mvccid;
	}
    }

  vacuum_er_log (VACUUM_ER_LOG_VACUUM_DATA,
		 "VACUUM: Vacuum data crt_lsa = (%lld, %d) - "
		 "first_blocks_to_recover = (%lld, %d), "
		 "last_blocks_to_recover = (%lld, %d), "
		 "n_blocks_to_recover = %d, n_first_blocks_to_recover = %d.",
		 (long long int) vacuum_Data->crt_lsa.pageid,
		 (int) vacuum_Data->crt_lsa.offset,
		 (long long int) vacuum_Data->first_blocks_to_recover.pageid,
		 (int) vacuum_Data->first_blocks_to_recover.offset,
		 (long long int) vacuum_Data->last_blocks_to_recover.pageid,
		 (int) vacuum_Data->last_blocks_to_recover.offset,
		 vacuum_Data->n_blocks_to_recover,
		 vacuum_Data->n_first_blocks_to_recover);

  /* Log changes */
  vacuum_log_append_block_data (thread_p,
				VACUUM_DATA_GET_ENTRY (prev_n_table_entries),
				n_recovered_blocks,
				&first_not_recovered_blocks_lsa,
				first_not_recovered_n_blocks);
end:
#if defined (SERVER_MODE)
  if (thread_p != NULL && thread_p->type == TT_VACUUM_MASTER)
    {
      /* Vacuum master completes to process log */
      vacuum_Master_is_process_log_phase = false;
    }
#endif /* !SERVER_MODE */

  if (log_unzip_p != NULL)
    {
      log_zip_free (log_unzip_p);
    }
  return error_code;
}

/*
 * vacuum_get_log_blockid () - Compute blockid for given log pageid.
 *
 * return      : Log blockid.
 * pageid (in) : Log pageid.
 */
VACUUM_LOG_BLOCKID
vacuum_get_log_blockid (LOG_PAGEID pageid)
{
  assert (vacuum_Data != NULL);
  return ((pageid ==
	   NULL_PAGEID) ? VACUUM_NULL_LOG_BLOCKID : (pageid /
						     vacuum_Data->
						     log_block_npages));
}

/*
 * vacuum_data_get_first_log_pageid () - Get the first pageid in first block
 *					 found in vacuum data. If vacuum has
 *					 no entries return NULL_PAGEID.
 *
 * return	 : LOG Page identifier for first log page that should be
 *		   processed by vacuum.
 * thread_p (in) : Thread entry.
 */
LOG_PAGEID
vacuum_data_get_first_log_pageid (THREAD_ENTRY * thread_p)
{
  /* Return first pageid from first block in vacuum data table */
  VACUUM_LOG_BLOCKID blockid = VACUUM_NULL_LOG_BLOCKID;
  bool locked_vacuum_data = false;

  if (prm_get_bool_value (PRM_ID_DISABLE_VACUUM))
    {
      return NULL_PAGEID;
    }

  if (vacuum_Data->n_table_entries == 0)
    {
      VACUUM_LOCK_DATA ();
      locked_vacuum_data = true;
      vacuum_consume_buffer_log_blocks (thread_p);
    }

  if (vacuum_Data->n_table_entries == 0)
    {
      /* No entries, no log pageid */
      blockid = vacuum_get_log_blockid (log_Gl.hdr.mvcc_op_log_lsa.pageid);
    }
  else
    {
      /* Get blockid of first entry in vacuum data table */
      blockid = VACUUM_DATA_ENTRY_BLOCKID (vacuum_Data->vacuum_data_table);
    }

  if (locked_vacuum_data)
    {
      VACUUM_UNLOCK_DATA ();
    }

  if (blockid == VACUUM_NULL_LOG_BLOCKID)
    {
      return NULL_PAGEID;
    }

  /* Return first pageid for blockid */
  return VACUUM_FIRST_LOG_PAGEID_IN_BLOCK (blockid);
}

/*
 * vacuum_data_get_last_blockid () - Get the last block ID from vacuum data.
 *
 * return	 : The page identifier for the last log page which is has
 *		   the statistics stored in vacuum data.
 * thread_p (in) : Thread entry.
 *
 * NOTE: Used for recovery (to know where to start the vacuum data recovery).
 */
VACUUM_LOG_BLOCKID
vacuum_data_get_last_blockid (THREAD_ENTRY * thread_p)
{
  /* Return last pageid from last block in vacuum data table */
  if (prm_get_bool_value (PRM_ID_DISABLE_VACUUM))
    {
      return VACUUM_NULL_LOG_BLOCKID;
    }

  assert (vacuum_Data != NULL);

  return vacuum_Data->last_blockid;
}

/*
 * vacuum_log_remove_data_entries () - Log when an entry is removed from vacuum data
 *				(after being vacuumed).
 *
 * return		  : Void.
 * thread_p (in)	  : Thread entry.
 * removed_indexes (in)	  : Indexes of removed vacuum data entries.
 * n_removed_indexes (in) : Removed entries number.
 */
static void
vacuum_log_remove_data_entries (THREAD_ENTRY * thread_p,
				int *removed_indexes, int n_removed_indexes)
{
#define MAX_LOG_DISCARD_BLOCK_DATA_CRUMBS 2

  VFID null_vfid;
  LOG_DATA_ADDR addr;
  LOG_CRUMB redo_crumbs[MAX_LOG_DISCARD_BLOCK_DATA_CRUMBS];
  int n_redo_crumbs = 0;

#if !defined (NDEBUG)
  LOG_LSA prev_lsa;

  LSA_COPY (&prev_lsa, &vacuum_Data->crt_lsa);
#endif

  /* Initialize addr */
  addr.pgptr = NULL;
  addr.offset = 0;
  VFID_SET_NULL (&null_vfid);
  addr.vfid = &null_vfid;

  /* Append the number of removed blocks to crumbs */
  redo_crumbs[n_redo_crumbs].data = &n_removed_indexes;
  redo_crumbs[n_redo_crumbs].length = sizeof (n_removed_indexes);
  n_redo_crumbs++;

  /* Append removed blocks indexes to crumbs */
  redo_crumbs[n_redo_crumbs].data = removed_indexes;
  redo_crumbs[n_redo_crumbs].length =
    n_removed_indexes * sizeof (*removed_indexes);
  n_redo_crumbs++;

  /* Safeguard check */
  assert (n_redo_crumbs <= MAX_LOG_DISCARD_BLOCK_DATA_CRUMBS);

  /* Append redo log record */
  log_append_redo_crumbs (thread_p, RVVAC_LOG_BLOCK_REMOVE, &addr,
			  n_redo_crumbs, redo_crumbs);

  assert (LSA_LT (&prev_lsa, &vacuum_Data->crt_lsa));
}

/*
 * vacuum_rv_redo_remove_data_entries () - Redo removing entry from vacuum data.
 *
 * return	 : Error code.
 * thread_p (in) : Thread entry.
 * rcv (in)	 : Log recovery data.
 */
int
vacuum_rv_redo_remove_data_entries (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  int *removed_entries = NULL;
  int n_removed_entries;
  int offset = 0;

  if (vacuum_Data == NULL)
    {
      /* Vacuum data is not initialized */
      assert (0);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return ER_GENERIC_ERROR;
    }

  /* Get the number of removed entries */
  n_removed_entries = *((int *) rcv->data);
  offset += sizeof (n_removed_entries);

  /* Get the removed entries indexes */
  removed_entries = (int *) (rcv->data + offset);
  offset += sizeof (*removed_entries) * n_removed_entries;

  /* Safeguard */
  assert (rcv->length == offset);

  /* Is lock required during recovery? */
  VACUUM_LOCK_DATA ();

  vacuum_data_remove_entries (thread_p, n_removed_entries, removed_entries);

  VACUUM_VERIFY_VACUUM_DATA ();
  VACUUM_UNLOCK_DATA ();

  return NO_ERROR;
}

/*
 * vacuum_log_append_block_data () - Log append of new blocks data.
 *
 * return			: Void.
 * thread_p (in)		: Thread entry.
 * new_entries (in)		: Appended blocks data.
 * n_new_entries (in)		: Number of appended blocks.
 * first_blocks_to_recover (in) : Not NULL value if the blocks have been
 *				  recovered from log and NULL value if the
 *				  blocks have been consumed from
 *				  vacuum_Block_data_buffer.
 */
static void
vacuum_log_append_block_data (THREAD_ENTRY * thread_p,
			      VACUUM_DATA_ENTRY * new_entries,
			      int n_new_entries,
			      LOG_LSA * first_blocks_to_recover,
			      int n_first_blocks_to_recover)
{
#define MAX_LOG_APPEND_BLOCK_DATA_CRUMBS 3

  VFID null_vfid;
  LOG_DATA_ADDR addr;
  LOG_CRUMB redo_crumbs[MAX_LOG_APPEND_BLOCK_DATA_CRUMBS];
  int n_redo_crumbs = 0, i;

#if !defined (NDEBUG)
  LOG_LSA prev_lsa;

  LSA_COPY (&prev_lsa, &vacuum_Data->crt_lsa);
#endif

  /* Initialize addr */
  addr.pgptr = NULL;
  /* Save number of logged entries to offset */
  addr.offset = n_new_entries;
  VFID_SET_NULL (&null_vfid);
  addr.vfid = &null_vfid;

  /* Append block data to crumbs */
  redo_crumbs[n_redo_crumbs].data = new_entries;
  redo_crumbs[n_redo_crumbs].length = sizeof (*new_entries) * n_new_entries;
  n_redo_crumbs++;

  /* If first_blocks_to_recover must be updated, log it */
  if (first_blocks_to_recover != NULL)
    {
      /* Log LSA of updated first blocks to recover */
      redo_crumbs[n_redo_crumbs].data = first_blocks_to_recover;
      redo_crumbs[n_redo_crumbs].length = sizeof (*first_blocks_to_recover);
      n_redo_crumbs++;

      /* Log number of blocks in the log entry of first blocks to recover */
      redo_crumbs[n_redo_crumbs].data = &n_first_blocks_to_recover;
      redo_crumbs[n_redo_crumbs].length = sizeof (n_first_blocks_to_recover);
      n_redo_crumbs++;
    }

  /* Safe guard */
  assert (n_redo_crumbs <= MAX_LOG_APPEND_BLOCK_DATA_CRUMBS);

  if (VACUUM_IS_ER_LOG_LEVEL_SET (VACUUM_ER_LOG_VACUUM_DATA))
    {
      vacuum_er_log (VACUUM_ER_LOG_VACUUM_DATA,
		     "VACUUM: Vacuum data crt_lsa = (%lld, %d) - "
		     "n_table_entries = %d before append.",
		     (long long int) vacuum_Data->crt_lsa.pageid,
		     (int) vacuum_Data->crt_lsa.offset,
		     vacuum_Data->n_table_entries - n_new_entries);

      for (i = 0; i < n_new_entries; i++)
	{
	  vacuum_er_log (VACUUM_ER_LOG_VACUUM_DATA,
			 "VACUUM: Vacuum data crt_lsa = (%lld, %d) - "
			 "Append block = "
			 "{start_lsa=(%lld, %d), oldest_mvccid=%llu, "
			 "newest_mvccid=%llu, blockid=%lld}",
			 (long long int) vacuum_Data->crt_lsa.pageid,
			 (int) vacuum_Data->crt_lsa.offset,
			 (long long int) new_entries[i].start_lsa.pageid,
			 (int) new_entries[i].start_lsa.offset,
			 new_entries[i].oldest_mvccid,
			 new_entries[i].newest_mvccid,
			 new_entries[i].blockid);
	}

      vacuum_er_log (VACUUM_ER_LOG_VACUUM_DATA,
		     "VACUUM: Vacuum data crt_lsa = (%lld, %d) - "
		     "n_table_entries = %d after append.",
		     (long long int) vacuum_Data->crt_lsa.pageid,
		     (int) vacuum_Data->crt_lsa.offset,
		     vacuum_Data->n_table_entries);
    }

  log_append_redo_crumbs (thread_p, RVVAC_LOG_BLOCK_APPEND, &addr,
			  n_redo_crumbs, redo_crumbs);

  assert (LSA_LT (&prev_lsa, &vacuum_Data->crt_lsa));
}

/*
 * vacuum_rv_redo_append_block_data () - Redo append new blocks data.
 *
 * return	 : Error code.
 * thread_p (in) : Thread entry.
 * rcv (in)	 : Recovery data.
 */
int
vacuum_rv_redo_append_block_data (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  int n_entries, i, data_offset = 0;

  if (vacuum_Data == NULL)
    {
      /* Vacuum data is not initialized */
      assert (0);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return ER_GENERIC_ERROR;
    }

  /* Number of blocks is saved to offset field of recovery */
  n_entries = rcv->offset;

  /* Append new blocks at the end of log block table */
  memcpy (VACUUM_DATA_GET_ENTRY (vacuum_Data->n_table_entries), rcv->data,
	  n_entries * sizeof (VACUUM_DATA_ENTRY));
  data_offset += n_entries * sizeof (VACUUM_DATA_ENTRY);

  /* Update log block table number of entries */
  vacuum_Data->n_table_entries += n_entries;

  /* Update last_blockid as the blockid of the last entry */
  vacuum_Data->last_blockid =
    VACUUM_DATA_ENTRY_BLOCKID (VACUUM_DATA_GET_ENTRY
			       (vacuum_Data->n_table_entries - 1));
  /* Discard blocks already recovered from blocks buffer. */
  vacuum_rv_discard_recovered_blocks_from_buffer (thread_p);

  if (data_offset < rcv->length)
    {
      /* First blocks to recover must be updated */
      LSA_COPY (&vacuum_Data->first_blocks_to_recover,
		(LOG_LSA *) (rcv->data + data_offset));
      if (LSA_ISNULL (&vacuum_Data->first_blocks_to_recover))
	{
	  /* Also set last blocks to recover to NULL */
	  LSA_SET_NULL (&vacuum_Data->last_blocks_to_recover);
	}

      /* Update the number of recovered blocks */
      vacuum_Data->n_blocks_to_recover -= n_entries;
      assert (vacuum_Data->n_blocks_to_recover >= 0);
      assert (vacuum_Data->n_blocks_to_recover == 0
	      || !LSA_ISNULL (&vacuum_Data->last_blocks_to_recover));

      data_offset += sizeof (LOG_LSA);

      /* Update number of blocks in first log entry of blocks to recover */
      vacuum_Data->n_first_blocks_to_recover =
	*((int *) (rcv->data + data_offset));
      data_offset += sizeof (vacuum_Data->n_first_blocks_to_recover);

      vacuum_er_log (VACUUM_ER_LOG_VACUUM_DATA,
		     "VACUUM: Vacuum data crt_lsa = (%lld, %d) - "
		     "first_blocks_to_recover = (%lld, %d), "
		     "last_blocks_to_recover = (%lld, %d), "
		     "n_blocks_to_recover = %d, "
		     "n_first_blocks_to_recover = %d.",
		     (long long int) vacuum_Data->crt_lsa.pageid,
		     (int) vacuum_Data->crt_lsa.offset,
		     (long long int)
		     vacuum_Data->first_blocks_to_recover.pageid,
		     (int) vacuum_Data->first_blocks_to_recover.offset,
		     (long long int)
		     vacuum_Data->last_blocks_to_recover.pageid,
		     (int) vacuum_Data->last_blocks_to_recover.offset,
		     vacuum_Data->n_blocks_to_recover,
		     vacuum_Data->n_first_blocks_to_recover);
    }

  /* Safe guard */
  assert (data_offset == rcv->length);

  vacuum_er_log (VACUUM_ER_LOG_RECOVERY | VACUUM_ER_LOG_VACUUM_DATA,
		 "VACUUM: Vacuum data crt_lsa = (%lld, %d) - "
		 "n_table_entries = %d before append.",
		 (long long int) vacuum_Data->crt_lsa.pageid,
		 (int) vacuum_Data->crt_lsa.offset,
		 vacuum_Data->n_table_entries - n_entries);

  /* Update newest MVCCID */
  for (i = vacuum_Data->n_table_entries - n_entries;
       i < vacuum_Data->n_table_entries; i++)
    {
      vacuum_er_log (VACUUM_ER_LOG_RECOVERY | VACUUM_ER_LOG_VACUUM_DATA,
		     "VACUUM: Vacuum data crt_lsa = (%lld, %d) - "
		     "Append block = "
		     "{start_lsa=(%lld, %d), oldest_mvccid=%llu, "
		     "newest_mvccid=%llu, blockid=%lld}",
		     (long long int) vacuum_Data->crt_lsa.pageid,
		     (int) vacuum_Data->crt_lsa.offset,
		     (long long int)
		     (VACUUM_DATA_GET_ENTRY (i)->start_lsa.pageid),
		     (int) (VACUUM_DATA_GET_ENTRY (i)->start_lsa.offset),
		     VACUUM_DATA_GET_ENTRY (i)->oldest_mvccid,
		     VACUUM_DATA_GET_ENTRY (i)->newest_mvccid,
		     VACUUM_DATA_GET_ENTRY (i)->blockid);

      if (MVCC_ID_PRECEDES (vacuum_Data->newest_mvccid,
			    VACUUM_DATA_ENTRY_NEWEST_MVCCID
			    (VACUUM_DATA_GET_ENTRY (i))))
	{
	  vacuum_Data->newest_mvccid =
	    VACUUM_DATA_ENTRY_NEWEST_MVCCID (VACUUM_DATA_GET_ENTRY (i));
	}
    }

  vacuum_er_log (VACUUM_ER_LOG_RECOVERY | VACUUM_ER_LOG_VACUUM_DATA,
		 "VACUUM: Vacuum data crt_lsa = (%lld, %d) - "
		 "n_table_entries = %d after append.",
		 (long long int) vacuum_Data->crt_lsa.pageid,
		 (int) vacuum_Data->crt_lsa.offset,
		 vacuum_Data->n_table_entries);

  VACUUM_VERIFY_VACUUM_DATA ();

  return NO_ERROR;
}

/*
 * vacuum_rv_redo_start_or_end_job () - Redo start/end vacuum job.
 *
 * return	 : Error code.
 * thread_p (in) : Thread entry.
 * rcv (in)	 : Recovery data.
 */
int
vacuum_rv_redo_start_or_end_job (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  VACUUM_DATA_ENTRY *table_entry = NULL;
  VACUUM_LOG_BLOCKID rv_blockid;

  assert (vacuum_Data != NULL);
  assert (rcv->data != NULL);
  assert (rcv->length == sizeof (VACUUM_LOG_BLOCKID));

  /* Is lock required during recovery? */
  VACUUM_LOCK_DATA ();

  /* Get blockid */
  rv_blockid = *((VACUUM_LOG_BLOCKID *) rcv->data);

  /* Recover table entry data */
  table_entry = vacuum_get_vacuum_data_entry (rv_blockid);
  assert (table_entry != NULL);
  table_entry->blockid = rv_blockid;

  VACUUM_UNLOCK_DATA ();

  return NO_ERROR;
}

void
vacuum_rv_redo_start_or_end_job_dump (FILE * fp, int length, void *data)
{
  VACUUM_LOG_BLOCKID rv_blockid;
  VACUUM_LOG_BLOCKID blockid;
  VACUUM_LOG_BLOCKID status;
  bool was_interrupted;

  rv_blockid = *((VACUUM_LOG_BLOCKID *) data);

  status = rv_blockid & VACUUM_BLOCK_STATUS_MASK;
  was_interrupted = (rv_blockid & VACUUM_BLOCK_FLAG_INTERRUPTED) != 0;
  blockid = rv_blockid & VACUUM_DATA_ENTRY_BLOCKID_MASK;

  fprintf (fp, " Set status %s, interrupted = %s for blockid = %lld.\n",
	   (status == (INT64) VACUUM_BLOCK_STATUS_VACUUMED) ?
	   "vacuumed" :
	   ((status == (INT64) VACUUM_BLOCK_STATUS_AVAILABLE) ?
	    "available" : "running"),
	   was_interrupted ? "true" : "false", (long long int) blockid);
}

/*
 * vacuum_update_oldest_mvccid () - Obtains the oldest mvccid from all blocks
 *				    found in vacuum data. Since there may
 *				    have been transactions blocked for a long
 *				    time, we need to also check
 *				    vacuum_Global_oldest_active_mvccid and
 *				    log_Gl.hdr.last_block_oldest_mvccid.
 *				    This should be called after
 *				    removing vacuumed blocks and after
 *				    consuming all block buffered blocks.
 *
 * return	 : Void.
 * thread_p (in) : Update oldest MVCCID.
 */
static void
vacuum_update_oldest_mvccid (THREAD_ENTRY * thread_p)
{
  int i;
  MVCCID oldest_mvccid;
  MVCCID log_header_oldest_mvccid = log_Gl.hdr.last_block_oldest_mvccid;

  vacuum_er_log (VACUUM_ER_LOG_VACUUM_DATA,
		 "VACUUM: Try to update oldest_mvccid.");

  /* Initialize oldest MVCCID as minimum between
   * vacuum_Global_oldest_active_mvccid and oldest MVCCID in the block being
   * logged but not produced yet. This is a defensive way of handling an old
   * generated MVCCID that has been blocked before doing any actual changes.
   * This MVCCID may be missing from several blocks (depending on how long the
   * transaction is blocked). When these blocks are vacuumed, the oldest
   * existing MVCCID in vacuum data may be bigger than the MVCCID of this
   * transaction. Oldest MVCCID should always be smaller or equal to any
   * unvacuumed MVCCID's!
   */
  if (MVCC_ID_PRECEDES (vacuum_Global_oldest_active_mvccid,
			log_header_oldest_mvccid)
      || LSA_ISNULL (&log_Gl.hdr.mvcc_op_log_lsa))
    {
      oldest_mvccid = vacuum_Global_oldest_active_mvccid;
    }
  else
    {
      oldest_mvccid = log_header_oldest_mvccid;
    }

  /* Now compare with entries in vacuum data. */
  for (i = 0; i < vacuum_Data->n_table_entries; i++)
    {
      if (MVCC_ID_PRECEDES (VACUUM_DATA_ENTRY_OLDEST_MVCCID
			    (VACUUM_DATA_GET_ENTRY (i)), oldest_mvccid))
	{
	  oldest_mvccid =
	    VACUUM_DATA_ENTRY_OLDEST_MVCCID (VACUUM_DATA_GET_ENTRY (i));
	}
    }

  if (vacuum_Data->oldest_mvccid != oldest_mvccid
      && (vacuum_Data->oldest_mvccid != MVCCID_ALL_VISIBLE
	  || oldest_mvccid != MVCCID_NULL))
    {
      /* Oldest MVCCID has changed. Update it and run cleanup on dropped
       * files.
       */
      vacuum_er_log (VACUUM_ER_LOG_VACUUM_DATA,
		     "VACUUM: Update oldest mvccid from %llu to %llu.",
		     vacuum_Data->oldest_mvccid, oldest_mvccid);

      /* If oldest MVCCID is changed, it should always become greater, not
       * smaller.
       */
      assert (MVCC_ID_PRECEDES (vacuum_Data->oldest_mvccid, oldest_mvccid));

      vacuum_Data->oldest_mvccid = oldest_mvccid;
      vacuum_cleanup_dropped_files (thread_p);

      /* Verify that vacuum data is still consistent. */
      VACUUM_VERIFY_VACUUM_DATA ();
    }
  else
    {
      vacuum_er_log (VACUUM_ER_LOG_VACUUM_DATA,
		     "VACUUM: Oldest MVCCID remains %llu.",
		     vacuum_Data->oldest_mvccid);
    }
}

/*
 * vacuum_compare_dropped_files () - Compare two file identifiers.
 *
 * return    : Positive if the first argument is bigger, negative if it is
 *	       smaller and 0 if arguments are equal.
 * a (in)    : Pointer to a file identifier.
 * b (in)    : Pointer to a a file identifier.
 */
static int
vacuum_compare_dropped_files (const void *a, const void *b)
{
  VFID *file_a = (VFID *) a;
  VFID *file_b = (VFID *) b;
  INT32 diff_fileid;

  assert (a != NULL && b != NULL);

  diff_fileid = file_a->fileid - file_b->fileid;
  if (diff_fileid != 0)
    {
      return (int) diff_fileid;
    }

  return (int) (file_a->volid - file_b->volid);
}

/*
 * vacuum_add_dropped_file () - Add new dropped file.
 *
 * return	 : Error code.
 * thread_p (in) : Thread entry.
 * vfid (in)     : Class OID or B-tree identifier.
 * mvccid (in)	 : MVCCID.
 * type (in)	 : Dropped file.
 */
static int
vacuum_add_dropped_file (THREAD_ENTRY * thread_p, VFID * vfid, MVCCID mvccid,
			 LOG_RCV * rcv, LOG_LSA * postpone_ref_lsa)
{
  MVCCID save_mvccid;
  VPID vpid, prev_vpid;
  int page_count, mem_size, compare;
  char *ptr = NULL;
  VACUUM_DROPPED_FILES_PAGE *page = NULL, *new_page = NULL;
  INT16 min, max, mid, position;
  LOG_DATA_ADDR addr;
  LOG_TDES *tdes = LOG_FIND_CURRENT_TDES (thread_p);

#if !defined (NDEBUG)
  VACUUM_TRACK_DROPPED_FILES *track_page = NULL;
  VACUUM_TRACK_DROPPED_FILES *new_track_page = NULL;
#endif

  assert (tdes != NULL);

  if (!vacuum_Dropped_files_loaded)
    {
      /* Normally, dropped files are loaded after recovery, in order to
       * provide a consistent state of its pages. Actually, the consistent
       * state should be reached after all run postpone and compensate undo
       * records are applied.
       * However, this may be called from log_recovery_finish_all_postpone
       * or from log_recovery_undo. Because there is no certain code that is
       * executed after applying redo and before calling these function, the
       * dropped files are loaded when needed.
       */

      /* This must be recover, otherwise the files should have been loaded. */
      assert (!LOG_ISRESTARTED ());

      if (vacuum_load_dropped_files_from_disk (thread_p) != NO_ERROR)
	{
	  vacuum_er_log (VACUUM_ER_LOG_ERROR | VACUUM_ER_LOG_DROPPED_FILES
			 | VACUUM_ER_LOG_RECOVERY,
			 "VACUUM: Failed to load dropped files during "
			 "recovery!");

	  assert_release (false);
	  return ER_FAILED;
	}
    }

  assert_release (!VFID_ISNULL (&vacuum_Dropped_files_vfid));
  assert_release (!VPID_ISNULL (&vacuum_Dropped_files_vpid));

#if !defined (NDEBUG)
  assert (vacuum_Track_dropped_files != NULL);

  track_page = vacuum_Track_dropped_files;
#endif /* !NDEBUG */

  addr.vfid = NULL;
  addr.offset = -1;

  VPID_COPY (&vpid, &vacuum_Dropped_files_vpid);
  while (!VPID_ISNULL (&vpid))
    {
      /* Unfix previous page */
      if (page != NULL)
	{
	  vacuum_unfix_dropped_entries_page (thread_p, page);
	}

      /* Fix current page */
      page = vacuum_fix_dropped_entries_page (thread_p, &vpid,
					      PGBUF_LATCH_WRITE);
      if (page == NULL)
	{
	  assert (false);
	  return ER_FAILED;
	}

      /* Save current vpid to prev_vpid */
      VPID_COPY (&prev_vpid, &vpid);

      /* Get next vpid and page count */
      VPID_COPY (&vpid, &page->next_page);
      page_count = page->n_dropped_files;

      /* dropped files must be ordered. Look for the right position for the
       * new entry.
       * The algorithm considers possible to have duplicate values in case of
       * dropped indexes, because btid may be reused. Although unlikely, it
       * is theoretically possible to drop index with same btid in a
       * relatively short time, without being removed from the list of dropped
       * indexes.
       *
       * Set position variable for adding new dropped file.
       */
      if (page_count > 0)
	{
	  /* dropped files are kept in ascending order */
	  /* Use a binary search to find the right position for the new
	   * dropped file. If a duplicate is found, just replace the previous
	   * MVCCID.
	   */
	  min = 0;
	  max = page_count;

	  /* Initialize compare with a non-zero value. If page_count is 1,
	   * the while loop is skipped and then we must compare with the new
	   * entry with the only existing value (which is only done if compare
	   * is not 0).
	   */
	  compare = -1;
	  while ((min + 1) < max)
	    {
	      /* Stop when next mid is the same with min */
	      mid = (min + max) / 2;

	      /* Compare with mid value */
	      compare =
		vacuum_compare_dropped_files (vfid,
					      &page->dropped_files[mid].vfid);
	      if (compare == 0)
		{
		  /* Duplicate found, break loop */
		  break;
		}

	      if (compare < 0)
		{
		  /* Keep searching in the min-mid range */
		  max = mid;
		}
	      else		/* compare > 0 */
		{
		  /* Keep searching in mid-max range */
		  min = mid;
		}
	    }

	  if (compare != 0 && min == 0)
	    {
	      /* This code can be reached in two cases:
	       * 1. There was previously only one value and the while loop was
	       *    skipped.
	       * 2. All compared entries have been bigger than the new value.
	       *    The last remaining range is min = 0, max = 1 and the new
	       *    value still must be compared with the first entry.
	       */
	      compare =
		vacuum_compare_dropped_files (vfid,
					      &page->dropped_files[0].vfid);
	      if (compare == 0)
		{
		  /* Set mid to 0 to replace the MVCCID */
		  mid = 0;
		}
	      else if (compare < 0)
		{
		  /* Add new entry before all existing entries */
		  position = 0;
		}
	      else		/* compare > 0 */
		{
		  /* Add new entry after the first entry */
		  position = 1;
		}
	    }
	  else
	    {
	      /* min is certainly smaller the the new entry. max is either
	       * bigger or is the equal to the number of existing entries.
	       * The position of the new entry must be max.
	       */
	      position = max;
	    }

	  if (compare == 0)
	    {
	      /* Same entry was already dropped, replace previous MVCCID */
	      /* The equal entry must be at the current mid value */

	      /* Replace MVCCID */
	      save_mvccid = page->dropped_files[mid].mvccid;
	      page->dropped_files[mid].mvccid = mvccid;

	      assert_release (MVCC_ID_FOLLOW_OR_EQUAL (mvccid, save_mvccid));

	      if (postpone_ref_lsa != NULL)
		{
		  /* Append run postpone */
		  addr.pgptr = (PAGE_PTR) page;
		  addr.offset = mid | VACUUM_DROPPED_FILES_RV_FLAG_DUPLICATE;
		  log_append_run_postpone (thread_p, RVVAC_DROPPED_FILE_ADD,
					   &addr,
					   pgbuf_get_vpid_ptr (addr.pgptr),
					   rcv->length, rcv->data,
					   postpone_ref_lsa);
		}
	      else
		{
		  /* Append compensate for undo record */
		  addr.pgptr = (PAGE_PTR) page;
		  addr.offset = mid | VACUUM_DROPPED_FILES_RV_FLAG_DUPLICATE;

		  log_append_compensate (thread_p, RVVAC_DROPPED_FILE_ADD,
					 pgbuf_get_vpid_ptr (addr.pgptr),
					 addr.offset, addr.pgptr,
					 rcv->length, rcv->data, tdes);
		}

#if !defined (NDEBUG)
	      if (track_page != NULL)
		{
		  memcpy (&track_page->dropped_data_page, page, DB_PAGESIZE);
		}
#endif
	      vacuum_er_log (VACUUM_ER_LOG_DROPPED_FILES,
			     "VACUUM: thread(%d): add dropped file: found "
			     "duplicate vfid(%d, %d) at position=%d, "
			     "replace mvccid=%llu with mvccid=%llu. "
			     "Page is (%d, %d) with lsa (%lld, %d)."
			     "Page count=%d, global count=%d",
			     thread_get_current_entry_index (),
			     page->dropped_files[mid].vfid.volid,
			     page->dropped_files[mid].vfid.fileid, mid,
			     save_mvccid, page->dropped_files[mid].mvccid,
			     pgbuf_get_volume_id ((PAGE_PTR) page),
			     pgbuf_get_page_id ((PAGE_PTR) page),
			     (long long int)
			     pgbuf_get_lsa ((PAGE_PTR) page)->pageid,
			     (int) pgbuf_get_lsa ((PAGE_PTR) page)->offset,
			     page->n_dropped_files,
			     vacuum_Dropped_files_count);

	      vacuum_set_dirty_dropped_entries_page (thread_p, page, FREE);

	      return NO_ERROR;
	    }

	  /* Not a duplicate */
	  if (VACUUM_DROPPED_FILES_PAGE_CAPACITY <= page_count)
	    {
	      /* No room left for new entries, try next page */

#if !defined (NDEBUG)
	      if (track_page != NULL && !VPID_ISNULL (&vpid))
		{
		  /* Don't advance from last track page. A new page will be
		   * added and we need to set a link between last track page
		   * and new track page.
		   */
		  track_page = track_page->next_tracked_page;
		}
#endif
	      continue;
	    }
	}
      else
	{
	  /* Set position to 0 */
	  position = 0;
	}

      /* Add new entry at position */
      mem_size = (page_count - position) * sizeof (VACUUM_DROPPED_FILE);
      if (mem_size > 0)
	{
	  memmove (&page->dropped_files[position + 1],
		   &page->dropped_files[position], mem_size);
	}

      /* Increment page count */
      page->n_dropped_files++;

      /* Increment total count */
      ATOMIC_INC_32 (&vacuum_Dropped_files_count, 1);

      VFID_COPY (&page->dropped_files[position].vfid, vfid);
      page->dropped_files[position].mvccid = mvccid;

      if (postpone_ref_lsa != NULL)
	{
	  /* Append run postpone */
	  addr.pgptr = (PAGE_PTR) page;
	  addr.offset = position;
	  log_append_run_postpone (thread_p, RVVAC_DROPPED_FILE_ADD, &addr,
				   pgbuf_get_vpid_ptr (addr.pgptr),
				   rcv->length, rcv->data, postpone_ref_lsa);
	}
      else
	{
	  /* Append compensate for undo record */
	  addr.pgptr = (PAGE_PTR) page;
	  addr.offset = position;

	  log_append_compensate (thread_p, RVVAC_DROPPED_FILE_ADD,
				 pgbuf_get_vpid_ptr (addr.pgptr),
				 addr.offset, addr.pgptr, rcv->length,
				 rcv->data, tdes);
	}

#if !defined (NDEBUG)
      if (track_page != NULL)
	{
	  memcpy (&track_page->dropped_data_page, page, DB_PAGESIZE);
	}
#endif

      vacuum_er_log (VACUUM_ER_LOG_DROPPED_FILES,
		     "VACUUM: thread(%d): added new dropped "
		     "file(%d, %d) and mvccid=%llu at position=%d. "
		     "Page is (%d, %d) with lsa (%lld, %d)."
		     "Page count=%d, global count=%d",
		     thread_get_current_entry_index (),
		     page->dropped_files[position].vfid.volid,
		     page->dropped_files[position].vfid.fileid,
		     page->dropped_files[position].mvccid, position,
		     pgbuf_get_volume_id ((PAGE_PTR) page),
		     pgbuf_get_page_id ((PAGE_PTR) page),
		     (long long int) pgbuf_get_lsa ((PAGE_PTR) page)->pageid,
		     (int) pgbuf_get_lsa ((PAGE_PTR) page)->offset,
		     page->n_dropped_files, vacuum_Dropped_files_count);

      vacuum_set_dirty_dropped_entries_page (thread_p, page, FREE);

      return NO_ERROR;
    }

  /* The entry couldn't fit in any of the current pages. */
  /* Allocate a new page */

  /* Last page must be fixed */
  assert (page != NULL);

  /* Extend file */
  if (file_alloc_pages (thread_p, &vacuum_Dropped_files_vfid, &vpid, 1,
			&prev_vpid, NULL, NULL) == NULL)
    {
      assert (false);
      vacuum_unfix_dropped_entries_page (thread_p, page);
      return ER_FAILED;
    }

  /* Add new entry to new page */
  new_page = vacuum_fix_dropped_entries_page (thread_p, &vpid,
					      PGBUF_LATCH_WRITE);
  if (new_page == NULL)
    {
      assert (false);
      vacuum_unfix_dropped_entries_page (thread_p, page);
      return ER_FAILED;
    }

  /* Set page header: next page as NULL and count as 1 */
  VPID_SET_NULL (&new_page->next_page);
  new_page->n_dropped_files = 1;

  /* Set vfid */
  VFID_COPY (&new_page->dropped_files[0].vfid, vfid);

  /* Set MVCCID */
  new_page->dropped_files[0].mvccid = mvccid;

  ATOMIC_INC_32 (&vacuum_Dropped_files_count, 1);

#if !defined(NDEBUG)
  if (track_page != NULL)
    {
      if (track_page->next_tracked_page == NULL)
	{
	  new_track_page =
	    (VACUUM_TRACK_DROPPED_FILES *)
	    malloc (VACUUM_TRACK_DROPPED_FILES_SIZE);
	  if (new_track_page == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_OUT_OF_VIRTUAL_MEMORY, 1,
		      VACUUM_TRACK_DROPPED_FILES_SIZE);
	      vacuum_unfix_dropped_entries_page (thread_p, page);
	      vacuum_unfix_dropped_entries_page (thread_p, new_page);
	      return ER_FAILED;
	    }
	}
      else
	{
	  new_track_page = track_page->next_tracked_page;
	}

      memcpy (&new_track_page->dropped_data_page, new_page, DB_PAGESIZE);
      new_track_page->next_tracked_page = NULL;
      track_page->next_tracked_page = new_track_page;
    }
#endif

  if (postpone_ref_lsa != NULL)
    {
      /* Append run postpone */
      addr.pgptr = (PAGE_PTR) new_page;
      addr.offset = VACUUM_DROPPED_FILES_RV_FLAG_NEWPAGE;
      log_append_run_postpone (thread_p, RVVAC_DROPPED_FILE_ADD, &addr,
			       pgbuf_get_vpid_ptr (addr.pgptr), rcv->length,
			       rcv->data, postpone_ref_lsa);
    }
  else
    {
      /* Append compensate for undo record */
      addr.pgptr = (PAGE_PTR) new_page;
      addr.offset = VACUUM_DROPPED_FILES_RV_FLAG_NEWPAGE;

      log_append_compensate (thread_p, RVVAC_DROPPED_FILE_ADD,
			     pgbuf_get_vpid_ptr (addr.pgptr),
			     addr.offset, addr.pgptr, rcv->length, rcv->data,
			     tdes);
    }
  pgbuf_set_page_ptype (thread_p, (PAGE_PTR) new_page, PAGE_DROPPED_FILES);

  vacuum_er_log (VACUUM_ER_LOG_DROPPED_FILES,
		 "VACUUM: thread(%d): added new dropped "
		 "file(%d, %d) and mvccid=%llu to at position=%d. "
		 "Page is (%d, %d) with lsa (%lld, %d)."
		 "Page count=%d, global count=%d",
		 thread_get_current_entry_index (),
		 new_page->dropped_files[0].vfid.volid,
		 new_page->dropped_files[0].vfid.fileid,
		 new_page->dropped_files[0].mvccid, 0,
		 pgbuf_get_volume_id ((PAGE_PTR) new_page),
		 pgbuf_get_page_id ((PAGE_PTR) new_page),
		 (long long int) pgbuf_get_lsa ((PAGE_PTR) new_page)->pageid,
		 (int) pgbuf_get_lsa ((PAGE_PTR) new_page)->offset,
		 new_page->n_dropped_files, vacuum_Dropped_files_count);

  /* Set dirty and unfix new page */
  vacuum_set_dirty_dropped_entries_page (thread_p, new_page, FREE);

  /* Save a link to the new page in last page */
  VPID_COPY (&page->next_page, &vpid);
  vacuum_log_dropped_files_set_next_page (thread_p, (PAGE_PTR) page, &vpid);

#if !defined(NDEBUG)
  if (track_page != NULL)
    {
      VPID_COPY (&track_page->dropped_data_page.next_page, &vpid);
    }
#endif

  /* Set dirty and unfix last page */
  vacuum_set_dirty_dropped_entries_page (thread_p, page, FREE);
  return NO_ERROR;
}

/*
 * vacuum_log_add_dropped_file () - Append postpone/undo log for notifying
 *				    vacuum of a file being dropped. Postpone
 *				    is added when a class or index is dropped
 *				    and undo when a class or index is created.
 *
 * return	 : Void.
 * thread_p (in) : Thread entry.
 * vfid (in)	 : Dropped file identifier.
 * class_oid(in) : class OID
 */
void
vacuum_log_add_dropped_file (THREAD_ENTRY * thread_p, const VFID * vfid,
			     const OID * class_oid, bool pospone_or_undo)
{
  LOG_DATA_ADDR addr;
  VACUUM_DROPPED_FILES_RCV_DATA rcv_data;

  vacuum_er_log (VACUUM_ER_LOG_DROPPED_FILES,
		 "VACUUM: Append %s log from dropped file (%d, %d).",
		 pospone_or_undo ? "postpone" : "undo",
		 vfid->volid, vfid->fileid);

  /* Initialize recovery data */
  VFID_COPY (&rcv_data.vfid, vfid);
  rcv_data.mvccid = MVCCID_NULL;	/* Not really used here */
  if (class_oid != NULL)
    {
      COPY_OID (&rcv_data.class_oid, class_oid);
    }
  else
    {
      OID_SET_NULL (&rcv_data.class_oid);
    }

  addr.offset = -1;
  addr.pgptr = NULL;
  addr.vfid = NULL;

  if (pospone_or_undo == VACUUM_LOG_ADD_DROPPED_FILE_POSTPONE)
    {
      log_append_postpone (thread_p, RVVAC_DROPPED_FILE_ADD, &addr,
			   sizeof (rcv_data), &rcv_data);
    }
  else
    {
      log_append_undo_data (thread_p, RVVAC_DROPPED_FILE_ADD, &addr,
			    sizeof (rcv_data), &rcv_data);
    }
}

/*
 * vacuum_rv_undoredo_add_dropped_file () - Redo recovery used for adding dropped
 *					files.
 *
 * return	 : Error code.
 * thread_p (in) : Thread entry.
 * rcv (in)	 : Recovery data.
 */
int
vacuum_rv_undoredo_add_dropped_file (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  VACUUM_DROPPED_FILES_PAGE *page = NULL;
  int error = NO_ERROR, offset = 0;
  INT16 position = rcv->offset & VACUUM_DROPPED_FILES_RV_CLEAR_MASK;
  int mem_size;
  VACUUM_DROPPED_FILES_RCV_DATA *rcv_data = NULL;
  bool replace = (rcv->offset & VACUUM_DROPPED_FILES_RV_FLAG_DUPLICATE) != 0;
  bool is_new_page =
    (rcv->offset & VACUUM_DROPPED_FILES_RV_FLAG_NEWPAGE) != 0;

  /* We cannot have a new page and a duplicate at the same time */
  assert (!replace || !is_new_page);

  rcv_data = ((VACUUM_DROPPED_FILES_RCV_DATA *) rcv->data);

  assert_release (rcv->length == sizeof (*rcv_data));
  assert_release (!VFID_ISNULL (&rcv_data->vfid));
  assert_release (MVCCID_IS_VALID (rcv_data->mvccid));

  page = (VACUUM_DROPPED_FILES_PAGE *) rcv->pgptr;

  if (is_new_page)
    {
      /* Initialize new page */
      VPID_SET_NULL (&page->next_page);
      page->n_dropped_files = 0;
      pgbuf_set_page_ptype (thread_p, (PAGE_PTR) page, PAGE_DROPPED_FILES);
    }

  if (replace)
    {
      /* Should be the same VFID */
      if (position >= page->n_dropped_files)
	{
	  /* Error! */
	  vacuum_er_log (VACUUM_ER_LOG_ERROR | VACUUM_ER_LOG_DROPPED_FILES
			 | VACUUM_ER_LOG_RECOVERY,
			 "VACUUM: Dropped files recovery error: Invalid "
			 "position %d (only %d entries in page) while "
			 "replacing old entry with vfid=(%d, %d) mvccid=%llu."
			 " Page is (%d, %d) at lsa (%lld, %d). ",
			 position, page->n_dropped_files,
			 rcv_data->vfid.volid, rcv_data->vfid.fileid,
			 rcv_data->mvccid,
			 pgbuf_get_volume_id (rcv->pgptr),
			 pgbuf_get_page_id (rcv->pgptr),
			 (long long int) pgbuf_get_lsa (rcv->pgptr)->pageid,
			 (int) pgbuf_get_lsa (rcv->pgptr)->offset);

	  assert_release (false);
	  return ER_FAILED;
	}

      if (!VFID_EQ (&rcv_data->vfid, &page->dropped_files[position].vfid))
	{
	  /* Error! */
	  vacuum_er_log (VACUUM_ER_LOG_ERROR | VACUUM_ER_LOG_DROPPED_FILES
			 | VACUUM_ER_LOG_RECOVERY,
			 "VACUUM: Dropped files recovery error: expected to "
			 "find vfid (%d, %d) at position %d and found "
			 "(%d, %d) with MVCCID=%d. "
			 "Page is (%d, %d) at lsa (%lld, %d). ",
			 rcv_data->vfid.volid, rcv_data->vfid.fileid,
			 position,
			 page->dropped_files[position].vfid.volid,
			 page->dropped_files[position].vfid.fileid,
			 page->dropped_files[position].mvccid,
			 pgbuf_get_volume_id (rcv->pgptr),
			 pgbuf_get_page_id (rcv->pgptr),
			 (long long int) pgbuf_get_lsa (rcv->pgptr)->pageid,
			 (int) pgbuf_get_lsa (rcv->pgptr)->offset);

	  assert_release (false);
	  return ER_FAILED;
	}

      vacuum_er_log (VACUUM_ER_LOG_DROPPED_FILES | VACUUM_ER_LOG_RECOVERY,
		     "VACUUM: Dropped files redo recovery, replace MVCCID for"
		     " file (%d, %d) with %lld (position=%d). "
		     "Page is (%d, %d) at lsa (%lld, %d).",
		     rcv_data->vfid.volid, rcv_data->vfid.fileid,
		     rcv_data->mvccid, position,
		     pgbuf_get_volume_id (rcv->pgptr),
		     pgbuf_get_page_id (rcv->pgptr),
		     (long long int) pgbuf_get_lsa (rcv->pgptr)->pageid,
		     (int) pgbuf_get_lsa (rcv->pgptr)->offset);
      page->dropped_files[position].mvccid = rcv_data->mvccid;
    }
  else
    {
      if (position > page->n_dropped_files)
	{
	  /* Error! */
	  vacuum_er_log (VACUUM_ER_LOG_ERROR | VACUUM_ER_LOG_DROPPED_FILES
			 | VACUUM_ER_LOG_RECOVERY,
			 "VACUUM: Dropped files recovery error: Invalid "
			 "position %d (only %d entries in page) while "
			 "inserting new entry vfid=(%d, %d) mvccid=%llu. "
			 "Page is (%d, %d) at lsa (%lld, %d). ",
			 position, page->n_dropped_files,
			 rcv_data->vfid.volid, rcv_data->vfid.fileid,
			 rcv_data->mvccid,
			 pgbuf_get_volume_id (rcv->pgptr),
			 pgbuf_get_page_id (rcv->pgptr),
			 (long long int) pgbuf_get_lsa (rcv->pgptr)->pageid,
			 (int) pgbuf_get_lsa (rcv->pgptr)->offset);

	  assert_release (false);
	  return ER_FAILED;
	}

      if (position < page->n_dropped_files)
	{
	  /* Make room for new record */
	  mem_size =
	    (page->n_dropped_files - position) * sizeof (VACUUM_DROPPED_FILE);
	  memmove (&page->dropped_files[position + 1],
		   &page->dropped_files[position], mem_size);
	}

      /* Copy new dropped file */
      VFID_COPY (&page->dropped_files[position].vfid, &rcv_data->vfid);
      page->dropped_files[position].mvccid = rcv_data->mvccid;

      /* Increment number of files */
      page->n_dropped_files++;

      vacuum_er_log (VACUUM_ER_LOG_DROPPED_FILES | VACUUM_ER_LOG_RECOVERY,
		     "VACUUM: Dropped files redo recovery, insert new entry "
		     "vfid=(%d, %d), mvccid=%llu at position %d. "
		     "Page is (%d, %d) at lsa (%lld, %d).",
		     rcv_data->vfid.volid, rcv_data->vfid.fileid,
		     rcv_data->mvccid, position,
		     pgbuf_get_volume_id (rcv->pgptr),
		     pgbuf_get_page_id (rcv->pgptr),
		     (long long int) pgbuf_get_lsa (rcv->pgptr)->pageid,
		     (int) pgbuf_get_lsa (rcv->pgptr)->offset);
    }

  /* Make sure the mvcc_next_id is also updated, since this is the marker used
   * by dropped files.
   */
  if (!MVCC_ID_PRECEDES (rcv_data->mvccid, log_Gl.hdr.mvcc_next_id))
    {
      log_Gl.hdr.mvcc_next_id = rcv_data->mvccid;
      MVCCID_FORWARD (log_Gl.hdr.mvcc_next_id);
    }

  /* Page was modified, so set it dirty */
  pgbuf_set_dirty (thread_p, rcv->pgptr, DONT_FREE);
  return NO_ERROR;
}

/*
 * vacuum_notify_dropped_file () - Add drop file used in recovery phase. Can
 *				   be used in two ways: at run postpone phase
 *				   for dropped heap files and indexes (if
 *				   postpone_ref_lsa in not null); or at undo
 *				   phase for created heap files and indexes.
 *
 * return		: Error code.
 * thread_p (in)	: Thread entry.
 * rcv (in)		: Recovery data.
 * pospone_ref_lsa (in) : Reference LSA for running postpone. NULL if this is
 *			  an undo for created heap files and indexes.
 */
int
vacuum_notify_dropped_file (THREAD_ENTRY * thread_p, LOG_RCV * rcv,
			    LOG_LSA * pospone_ref_lsa)
{
  int error = NO_ERROR;
  LOG_RCV new_rcv;
  VACUUM_DROPPED_FILES_RCV_DATA new_rcv_data;
  OID *class_oid;
#if defined (SERVER_MODE)
  INT32 my_version, workers_min_version;
#endif

  new_rcv.mvcc_id = rcv->mvcc_id;
  new_rcv.offset = rcv->offset;
  new_rcv.pgptr = rcv->pgptr;
  new_rcv.data = (char *) &new_rcv_data;
  new_rcv.length = sizeof (new_rcv_data);

  /* Copy VFID from current log recovery data but set MVCCID at this point.
   * We will use the log_Gl.hdr.mvcc_next_id as borderline to distinguish
   * this file from newer files.
   * 1. All changes on this file must be done by transaction that have already
   *    committed which means their MVCCID will be less than current
   *    log_Gl.hdr.mvcc_next_id.
   * 2. All changes on a new file that reused VFID must be done by transaction
   *    that start after this call, which means their MVCCID's will be at
   *    least equal to current log_Gl.hdr.mvcc_next_id.
   */

  VFID_COPY (&new_rcv_data.vfid,
	     &((VACUUM_DROPPED_FILES_RCV_DATA *) rcv->data)->vfid);
  new_rcv_data.mvccid = log_Gl.hdr.mvcc_next_id;
  OID_SET_NULL (&new_rcv_data.class_oid);

  assert (!VFID_ISNULL (&new_rcv_data.vfid));
  assert (MVCCID_IS_VALID (new_rcv_data.mvccid));

  /* Add dropped file to current list */
  error = vacuum_add_dropped_file (thread_p, &new_rcv_data.vfid,
				   new_rcv_data.mvccid, &new_rcv,
				   pospone_ref_lsa);
  if (error != NO_ERROR)
    {
      return error;
    }

#if defined (SERVER_MODE)
  /* Before notifying vacuum workers there is one last thing we have to do.
   * Running workers must also be notified of the VFID being dropped to
   * cleanup their collected heap object arrays.
   * Since must done one file at a time, so a mutex is used for protection,
   * in case there are several transactions doing file drops.
   */
  pthread_mutex_lock (&vacuum_Dropped_files_mutex);
  assert (VFID_ISNULL (&vacuum_Last_dropped_vfid));
  VFID_COPY (&vacuum_Last_dropped_vfid, &new_rcv_data.vfid);

  /* Increment dropped files version and save a version for current change.
   * It is not important to keep the versioning synchronized with the changes.
   * It is only used to make sure that all workers have seen current change.
   */
  my_version = ++vacuum_Dropped_files_version;

  vacuum_er_log (VACUUM_ER_LOG_DROPPED_FILES,
		 "VACUUM: Added dropped file - vfid=(%d, %d), mvccid=%llu - "
		 "Wait for all workers to see my_version=%d",
		 new_rcv_data.vfid.volid, new_rcv_data.vfid.fileid,
		 new_rcv_data.mvccid, my_version);

  /* Wait until all workers have been notified of this change */
  for (workers_min_version = vacuum_get_worker_min_dropped_files_version ();
       workers_min_version != -1 && workers_min_version < my_version;
       workers_min_version = vacuum_get_worker_min_dropped_files_version ())
    {
      vacuum_er_log (VACUUM_ER_LOG_DROPPED_FILES,
		     "VACUUM: not all workers saw my changes, "
		     "workers min version=%d. Sleep and retry.",
		     workers_min_version);

      thread_sleep (1);
    }

  vacuum_er_log (VACUUM_ER_LOG_DROPPED_FILES,
		 "VACUUM: All workers have been notified, min_version=%d",
		 workers_min_version);

  VFID_SET_NULL (&vacuum_Last_dropped_vfid);
  pthread_mutex_unlock (&vacuum_Dropped_files_mutex);
#endif /* SERVER_MODE */

  /* vacuum is notified of the file drop, it is safe to remove from cache */
  class_oid = &((VACUUM_DROPPED_FILES_RCV_DATA *) rcv->data)->class_oid;
  if (!OID_ISNULL (class_oid))
    {
      (void) heap_delete_hfid_from_cache (thread_p, class_oid);
    }

  /* Success */
  return NO_ERROR;
}

/*
 * vacuum_cleanup_dropped_files () - Clean unnecessary dropped files.
 *
 * return	 : Error code.
 * thread_p (in) : Thread entry.
 *
 * NOTE: All entries with an MVCCID older than vacuum_Data->oldest_mvccid are
 *	 removed. All records belonging to these entries must be either
 *	 vacuumed or skipped after drop.
 */
static int
vacuum_cleanup_dropped_files (THREAD_ENTRY * thread_p)
{
  VPID vpid;
  VACUUM_DROPPED_FILES_PAGE *page = NULL;
  int page_count, mem_size;
  VPID last_page_vpid, last_non_empty_page_vpid;
  INT16 removed_entries[VACUUM_DROPPED_FILES_MAX_PAGE_CAPACITY];
  INT16 n_removed_entries, i;
#if !defined (NDEBUG)
  VACUUM_TRACK_DROPPED_FILES *track_page =
    (VACUUM_TRACK_DROPPED_FILES *) vacuum_Track_dropped_files;
#endif

  vacuum_er_log (VACUUM_ER_LOG_DROPPED_FILES,
		 "VACUUM: Start cleanup dropped files.");

  if (!LOG_ISRESTARTED ())
    {
      /* Skip cleanup during recovery */
      vacuum_er_log (VACUUM_ER_LOG_RECOVERY | VACUUM_ER_LOG_DROPPED_FILES,
		     "VACUUM: Skip cleanup during recovery.");
      return NO_ERROR;
    }

  assert_release (!VFID_ISNULL (&vacuum_Dropped_files_vfid));
  assert_release (!VPID_ISNULL (&vacuum_Dropped_files_vpid));

  if (vacuum_Dropped_files_count == 0)
    {
      /* Nothing to clean */
      vacuum_er_log (VACUUM_ER_LOG_DROPPED_FILES,
		     "VACUUM: Cleanup skipped, no current entries.");
      return NO_ERROR;
    }

  /* Clean each page of dropped files */
  VPID_COPY (&vpid, &vacuum_Dropped_files_vpid);
  VPID_COPY (&last_non_empty_page_vpid, &vacuum_Dropped_files_vpid);

  while (!VPID_ISNULL (&vpid))
    {
      /* Reset n_removed_entries */
      n_removed_entries = 0;

      /* Track the last page found */
      VPID_COPY (&last_page_vpid, &vpid);

      /* Fix current page */
      page = vacuum_fix_dropped_entries_page (thread_p, &vpid,
					      PGBUF_LATCH_WRITE);
      if (page == NULL)
	{
	  assert (false);
	  return ER_FAILED;
	}

      /* Get next page VPID */
      VPID_COPY (&vpid, &page->next_page);

      page_count = page->n_dropped_files;
      if (page_count == 0)
	{
	  /* Page is empty */
	  vacuum_unfix_dropped_entries_page (thread_p, page);
	  continue;
	}

      /* Page is not empty, track the last non-empty page found */
      VPID_COPY (&last_non_empty_page_vpid, &vpid);

      /* Check entries for cleaning. Start from the end of the array */
      for (i = page_count - 1; i >= 0; i--)
	{
	  if (MVCC_ID_PRECEDES (page->dropped_files[i].mvccid,
				vacuum_Data->oldest_mvccid))
	    {
	      /* Remove entry */
	      removed_entries[n_removed_entries++] = i;
	      if (i < page_count - 1)
		{
		  mem_size =
		    (page_count - i - 1) * sizeof (VACUUM_DROPPED_FILE);
		  memmove (&page->dropped_files[i],
			   &page->dropped_files[i + 1], mem_size);
		}
	    }
	}

      if (n_removed_entries > 0)
	{
	  /* Update dropped files global counter */
	  ATOMIC_INC_32 (&vacuum_Dropped_files_count, -n_removed_entries);

	  /* Update dropped files page counter */
	  page->n_dropped_files -= n_removed_entries;

	  /* Log changes */
	  vacuum_log_cleanup_dropped_files (thread_p, (PAGE_PTR) page,
					    removed_entries,
					    n_removed_entries);

	  vacuum_er_log (VACUUM_ER_LOG_DROPPED_FILES,
			 "VACUUM: cleanup dropped files. "
			 "Page is (%d %d) with lsa (%lld, %d). "
			 "Page count=%d, global count=%d",
			 pgbuf_get_volume_id ((PAGE_PTR) page),
			 pgbuf_get_page_id ((PAGE_PTR) page),
			 (long long int)
			 pgbuf_get_lsa ((PAGE_PTR) page)->pageid,
			 (int) pgbuf_get_lsa ((PAGE_PTR) page)->offset,
			 page->n_dropped_files, vacuum_Dropped_files_count);

#if !defined (NDEBUG)
	  /* Copy changes to tracker */
	  memcpy (&track_page->dropped_data_page, page, DB_PAGESIZE);
#endif
	  vacuum_set_dirty_dropped_entries_page (thread_p, page, FREE);
	}
      else
	{
	  /* No changes */
	  vacuum_unfix_dropped_entries_page (thread_p, page);
	}

#if !defined (NDEBUG)
      track_page = track_page->next_tracked_page;
#endif
    }

  if (!VPID_ISNULL (&last_non_empty_page_vpid)
      && !VPID_EQ (&last_non_empty_page_vpid, &last_page_vpid))
    {
      /* Update next page link in the last non-empty page to NULL, to avoid
       * fixing empty pages in the future.
       */
      vacuum_er_log (VACUUM_ER_LOG_DROPPED_FILES,
		     "VACUUM: Cleanup dropped files must remove pages to the "
		     "of page (%d, %d)... Cut off link.",
		     last_non_empty_page_vpid.volid,
		     last_non_empty_page_vpid.pageid);

      page = vacuum_fix_dropped_entries_page (thread_p,
					      &last_non_empty_page_vpid,
					      PGBUF_LATCH_WRITE);
      if (page == NULL)
	{
	  assert (false);
	  return ER_FAILED;
	}

      VPID_SET_NULL (&page->next_page);
      vacuum_log_dropped_files_set_next_page (thread_p, (PAGE_PTR) page,
					      &page->next_page);

      vacuum_set_dirty_dropped_entries_page (thread_p, page, FREE);
    }

  vacuum_er_log (VACUUM_ER_LOG_DROPPED_FILES,
		 "VACUUM: Finished cleanup dropped files.");
  return NO_ERROR;
}

/*
 * vacuum_is_file_dropped () - Check whether file is considered dropped.
 *
 * return	 : True if file is considered dropped.
 * thread_p (in) : Thread entry.
 * vfid (in)	 : File identifier.
 * mvccid (in)	 : MVCCID.
 */
bool
vacuum_is_file_dropped (THREAD_ENTRY * thread_p, VFID * vfid, MVCCID mvccid)
{
  if (prm_get_bool_value (PRM_ID_DISABLE_VACUUM))
    {
      return false;
    }

  return vacuum_find_dropped_file (thread_p, vfid, mvccid);
}

/*
 * vacuum_find_dropped_file () - Find the dropped file and check whether the
 *				 given MVCCID is older than or equal to the
 *				 MVCCID of dropped file.
 *				 Used by vacuum to detect records that
 *				 belong to dropped files.
 *
 * return	 : True if record belong to a dropped file.
 * thread_p (in) : Thread entry.
 * vfid (in)	 : File identifier.
 * mvccid (in)	 : MVCCID of checked record.
 */
static bool
vacuum_find_dropped_file (THREAD_ENTRY * thread_p, VFID * vfid, MVCCID mvccid)
{
  VACUUM_DROPPED_FILES_PAGE *page = NULL;
  VACUUM_DROPPED_FILE *dropped_file = NULL;
  VPID vpid;
  INT16 page_count;

  if (vacuum_Dropped_files_count == 0)
    {
      /* No dropped files */
      return false;
    }

  assert_release (!VPID_ISNULL (&vacuum_Dropped_files_vpid));

  /* Search for dropped file in all pages. */
  VPID_COPY (&vpid, &vacuum_Dropped_files_vpid);

  while (!VPID_ISNULL (&vpid))
    {
      /* Fix current page */
      page = vacuum_fix_dropped_entries_page (thread_p, &vpid,
					      PGBUF_LATCH_READ);
      if (page == NULL)
	{
	  if (VACUUM_IS_THREAD_VACUUM_WORKER (thread_p)
	      || er_errid () != ER_INTERRUPTED)
	    {
	      assert (false);
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	    }
	  return false;
	}

      /* Copy next page VPID */
      VPID_COPY (&vpid, &page->next_page);
      page_count = page->n_dropped_files;

      /* Use compare VFID to find a matching entry */
      dropped_file =
	(VACUUM_DROPPED_FILE *) bsearch (vfid, page->dropped_files,
					 page_count,
					 sizeof (VACUUM_DROPPED_FILE),
					 vacuum_compare_dropped_files);
      if (dropped_file != NULL)
	{
	  /* Found matching entry.
	   * Compare the given MVCCID with the MVCCID of dropped file.
	   */
	  if (MVCC_ID_PRECEDES (mvccid, dropped_file->mvccid))
	    {
	      /* The record must belong to the dropped file */
	      vacuum_er_log (VACUUM_ER_LOG_DROPPED_FILES,
			     "VACUUM: found dropped file: vfid=(%d, %d) "
			     "mvccid=%d in page (%d, %d). Entry at position "
			     "%d, vfid=(%d, %d) mvccid=%d. "
			     "The vacuumed file is dropped.",
			     vfid->volid, vfid->fileid, mvccid,
			     pgbuf_get_volume_id ((PAGE_PTR) page),
			     pgbuf_get_page_id ((PAGE_PTR) page),
			     dropped_file - page->dropped_files,
			     dropped_file->vfid.volid,
			     dropped_file->vfid.fileid, dropped_file->mvccid);

	      vacuum_unfix_dropped_entries_page (thread_p, page);
	      return true;
	    }
	  else
	    {
	      /* The record belongs to an entry with the same identifier, but
	       * is newer.
	       */
	      vacuum_er_log (VACUUM_ER_LOG_DROPPED_FILES,
			     "VACUUM: found dropped file: vfid=(%d, %d) "
			     "mvccid=%d in page (%d, %d). Entry at position "
			     "%d, vfid=(%d, %d) mvccid=%d. "
			     "The vacuumed file is newer.",
			     vfid->volid, vfid->fileid, mvccid,
			     pgbuf_get_volume_id ((PAGE_PTR) page),
			     pgbuf_get_page_id ((PAGE_PTR) page),
			     dropped_file - page->dropped_files,
			     dropped_file->vfid.volid,
			     dropped_file->vfid.fileid, dropped_file->mvccid);

	      vacuum_unfix_dropped_entries_page (thread_p, page);
	      return false;
	    }
	}

      /* Do not log this unless you think it is useful. It spams the log
       * file.
       */
      vacuum_er_log (VACUUM_ER_LOG_NONE,
		     "VACUUM: didn't find dropped file: vfid=(%d, %d)"
		     " mvccid=%d in page (%d, %d).",
		     vfid->volid, vfid->fileid, mvccid,
		     pgbuf_get_volume_id ((PAGE_PTR) page),
		     pgbuf_get_page_id ((PAGE_PTR) page));

      vacuum_unfix_dropped_entries_page (thread_p, page);
    }

  /* Entry not found */
  return false;
}

/*
 * vacuum_log_cleanup_dropped_files () - Log dropped files cleanup.
 *
 * return	  : Void.
 * thread_p (in)  : Thread entry.
 * page_p (in)	  : Page pointer.
 * indexes (in)	  : Indexes of cleaned up dropped files.
 * n_indexes (in) : Total count of dropped files.
 *
 * NOTE: Consider not logging cleanup. Cleanup can be done at database
 *	 restart.
 */
static void
vacuum_log_cleanup_dropped_files (THREAD_ENTRY * thread_p, PAGE_PTR page_p,
				  INT16 * indexes, INT16 n_indexes)
{
#define VACUUM_CLEANUP_DROPPED_FILES_MAX_REDO_CRUMBS 3
  LOG_CRUMB redo_crumbs[VACUUM_CLEANUP_DROPPED_FILES_MAX_REDO_CRUMBS];
  LOG_DATA_ADDR addr;
  int n_redo_crumbs = 0;

  /* Add n_indexes */
  redo_crumbs[n_redo_crumbs].data = &n_indexes;
  redo_crumbs[n_redo_crumbs++].length = sizeof (n_indexes);

  /* Add indexes */
  redo_crumbs[n_redo_crumbs].data = indexes;
  redo_crumbs[n_redo_crumbs++].length = n_indexes * sizeof (*indexes);

  assert (n_redo_crumbs <= VACUUM_CLEANUP_DROPPED_FILES_MAX_REDO_CRUMBS);

  /* Initialize log data address */
  addr.pgptr = page_p;
  addr.vfid = &vacuum_Dropped_files_vfid;
  addr.offset = 0;

  log_append_redo_crumbs (thread_p, RVVAC_DROPPED_FILE_CLEANUP, &addr,
			  n_redo_crumbs, redo_crumbs);
}

/*
 * vacuum_rv_redo_cleanup_dropped_files () - Recover dropped files cleanup.
 *
 * return	 : Error code.
 * thread_p (in) : Thread entry,
 * rcv (in)	 : Recovery data.
 *
 * NOTE: Consider not logging cleanup. Cleanup can be done at database
 *	 restart.
 */
int
vacuum_rv_redo_cleanup_dropped_files (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  int offset = 0, mem_size;
  VACUUM_DROPPED_FILES_PAGE *page = (VACUUM_DROPPED_FILES_PAGE *) rcv->pgptr;
  INT32 *countp = NULL;
  INT16 *indexes;
  INT16 n_indexes, i;

  /* Get recovery information */

  /* Get n_indexes */
  n_indexes = *((INT16 *) rcv->data);
  offset += sizeof (n_indexes);

  /* Get indexes */
  indexes = (INT16 *) (rcv->data + offset);
  offset += sizeof (*indexes) * n_indexes;

  /* Check that all recovery data has been processed */
  assert (offset == rcv->length);

  /* Cleanup starting from last entry */
  for (i = 0; i < n_indexes; i++)
    {
      /* Remove entry at indexes[i] */
      vacuum_er_log (VACUUM_ER_LOG_RECOVERY | VACUUM_ER_LOG_DROPPED_FILES,
		     "Recovery of dropped classes: remove "
		     "file(%d, %d), mvccid=%llu at position %d.",
		     (int) page->dropped_files[indexes[i]].vfid.volid,
		     (int) page->dropped_files[indexes[i]].vfid.fileid,
		     page->dropped_files[indexes[i]].mvccid,
		     (int) indexes[i]);
      mem_size =
	(page->n_dropped_files - indexes[i]) * sizeof (VACUUM_DROPPED_FILE);

      assert (mem_size >= 0);
      if (mem_size > 0)
	{
	  memmove (&page->dropped_files[indexes[i]],
		   &page->dropped_files[indexes[i] + 1], mem_size);
	}

      /* Update dropped files page counter */
      page->n_dropped_files--;
    }

  pgbuf_set_dirty (thread_p, rcv->pgptr, DONT_FREE);

  return NO_ERROR;
}

/*
 * vacuum_log_dropped_files_set_next_page () - Log changing link to next
 *					       page for dropped files.
 *
 * return	  : Void.
 * thread_p (in)  : Thread entry.
 * page_p (in)	  : Page pointer.
 * next_page (in) : Next page VPID.
 */
static void
vacuum_log_dropped_files_set_next_page (THREAD_ENTRY * thread_p,
					PAGE_PTR page_p, VPID * next_page)
{
  LOG_DATA_ADDR addr;

  /* Initialize log data address */
  addr.pgptr = page_p;
  addr.vfid = &vacuum_Dropped_files_vfid;
  addr.offset = 0;

  /* Append log redo */
  log_append_redo_data (thread_p, RVVAC_DROPPED_FILE_NEXT_PAGE, &addr,
			sizeof (*next_page), next_page);
}

/*
 * vacuum_rv_set_next_page_dropped_files () - Recover setting link to next
 *					      page for dropped files.
 *
 * return	 : Error code.
 * thread_p (in) : Thread entry.
 * rcv (in)	 : Recovery data.
 */
int
vacuum_rv_set_next_page_dropped_files (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  VACUUM_DROPPED_FILES_PAGE *page = (VACUUM_DROPPED_FILES_PAGE *) rcv->pgptr;

  /* Set next page VPID */
  VPID_COPY (&page->next_page, (VPID *) rcv->data);

  /* Check recovery data is as expected */
  assert (rcv->length = sizeof (VPID));

  vacuum_er_log (VACUUM_ER_LOG_RECOVERY,
		 "Set link for dropped files from page(%d, %d) to "
		 "page(%d, %d).", pgbuf_get_vpid_ptr (rcv->pgptr)->pageid,
		 pgbuf_get_vpid_ptr (rcv->pgptr)->volid,
		 page->next_page.volid, page->next_page.pageid);

  pgbuf_set_dirty (thread_p, rcv->pgptr, DONT_FREE);

  return NO_ERROR;
}

/*
 * vacuum_compare_heap_object () - Compare two heap objects to be vacuumed.
 *				   HFID compare has priority against OID
 *				   compare.
 *
 * return : Compare result. 
 * a (in) : First object.
 * b (in) : Second object.
 */
static int
vacuum_compare_heap_object (const void *a, const void *b)
{
  VACUUM_HEAP_OBJECT *file_obj_a = (VACUUM_HEAP_OBJECT *) a;
  VACUUM_HEAP_OBJECT *file_obj_b = (VACUUM_HEAP_OBJECT *) b;
  int diff;

  /* First compare VFID, then OID. */

  /* Compare VFID file ID's. */
  diff = (int) (file_obj_a->vfid.fileid - file_obj_b->vfid.fileid);
  if (diff != 0)
    {
      return diff;
    }

  /* Compare VFID volume ID's. */
  diff = (int) (file_obj_a->vfid.volid - file_obj_b->vfid.volid);
  if (diff != 0)
    {
      return diff;
    }

  /* Compare OID page ID's. */
  diff = (int) (file_obj_a->oid.pageid - file_obj_b->oid.pageid);
  if (diff != 0)
    {
      return diff;
    }

  /* Compare OID volume ID's. */
  diff = (int) (file_obj_a->oid.volid - file_obj_b->oid.volid);
  if (diff != 0)
    {
      return diff;
    }

  /* Compare OID slot ID's. */
  return (int) (file_obj_a->oid.slotid - file_obj_b->oid.slotid);
}

/*
 * vacuum_collect_heap_objects () - document me!
 *
 * return		  : Error code.
 * worker (in/out)	  : Vacuum worker structure.
 * oid (in)		  : Heap object OID.
 * vfid (in)		  : Heap file ID.
 */
static int
vacuum_collect_heap_objects (VACUUM_WORKER * worker, OID * oid, VFID * vfid)
{
  /* Collect both file ID and object OID to vacuum at the end of the job.
   * Heap file ID is required to know whether objects are reusable or not,
   * OID is to point vacuum where data needs to be removed.
   */

  /* Make sure we have enough storage. */
  if (worker->n_heap_objects >= worker->heap_objects_capacity)
    {
      /* Expand buffer. */
      VACUUM_HEAP_OBJECT *new_buffer = NULL;
      int new_capacity = worker->heap_objects_capacity * 2;

      new_buffer =
	(VACUUM_HEAP_OBJECT *) realloc (worker->heap_objects,
					new_capacity *
					sizeof (VACUUM_HEAP_OBJECT));
      if (new_buffer == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
		  1, new_capacity * sizeof (VACUUM_HEAP_OBJECT));
	  vacuum_er_log (VACUUM_ER_LOG_ERROR | VACUUM_ER_LOG_WORKER,
			 "VACUUM ERROR: Could not expact the files and "
			 "objects capacity to %d.\n", new_capacity);
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}
      worker->heap_objects = new_buffer;
      worker->heap_objects_capacity = new_capacity;
    }

  /* Add new heap object (HFID & OID). */
  VFID_COPY (&worker->heap_objects[worker->n_heap_objects].vfid, vfid);
  COPY_OID (&worker->heap_objects[worker->n_heap_objects].oid, oid);
  /* Increment object count. */
  worker->n_heap_objects++;

  /* Success. */
  return NO_ERROR;
}

/*
 * vacuum_cleanup_collected_by_vfid () - Cleanup entries collected from
 *					 dropped file.
 *
 * return      : Void.
 * worker (in) : Vacuum worker.
 * vfid (in)   : VFID of dropped file.
 */
static void
vacuum_cleanup_collected_by_vfid (VACUUM_WORKER * worker, VFID * vfid)
{
  int start, end;

  /* Sort collected. */
  qsort (worker->heap_objects, worker->n_heap_objects,
	 sizeof (VACUUM_HEAP_OBJECT), vacuum_compare_heap_object);

  /* Find first entry for file */
  for (start = 0;
       start < worker->n_heap_objects
       && !VFID_EQ (&worker->heap_objects[start].vfid, vfid); start++);
  if (start == worker->n_heap_objects)
    {
      /* VFID doesn't exist. */
      return;
    }
  /* Find first entry for other file. */
  for (end = start + 1;
       end < worker->n_heap_objects
       && VFID_EQ (&worker->heap_objects[end].vfid, vfid); end++);
  /* Remove all between start and end. */
  if (end == worker->n_heap_objects)
    {
      /* Just update the number of objects. */
      worker->n_heap_objects = start;
    }
  else
    {
      /* Move objects after end */
      memmove (&worker->heap_objects[start], &worker->heap_objects[end],
	       (worker->n_heap_objects - end) * sizeof (VACUUM_HEAP_OBJECT));
      /* Update number of objects. */
      worker->n_heap_objects -= (end - start);
    }
}

/*
 * vacuum_compare_dropped_files_version () - Compare two versions ID's of
 *					     dropped files. Take into
 *					     consideration that versions can
 *					     overflow max value of INT32.
 *
 * return	  : Positive value if first version is considered bigger,
 *		    negative if it is considered smaller and 0 if they are
 *		    equal.
 * version_a (in) : First version.
 * version_b (in) : Second version.
 */
int
vacuum_compare_dropped_files_version (INT32 version_a, INT32 version_b)
{
  INT32 max_int32_div_2 = 0x3FFFFFFF;

  /* If both are positive or if both are negative return a-b */
  if ((version_a >= 0 && version_b >= 0) || (version_a < 0 && version_b < 0))
    {
      return (int) (version_a - version_b);
    }

  /* If one is positive and the other negative we have to consider the case
   * when version overflowed INT32 and the case when one just passed 0.
   * In the first case, the positive value is considered smaller, while in the
   * second case the negative value is considered smaller.
   * The INT32 domain of values is split into 4 ranges:
   * [-MAX_INT32, -MAX_INT32/2], [-MAX_INT32/2, 0], [0, MAX_INT32/2] and
   * [MAX_INT32/2, MAX_INT32].
   * We will consider the case when one value is in [-MAX_INT32, -MAX_INT32/2]
   * and the other in [MAX_INT32/2, MAX_INT32] and the second case when the
   * values are in [-MAX_INT32/2, 0] and [0, MAX_INT32].
   * If the values are not in these ranges, the algorithm is flawed.
   */
  if (version_a >= 0)
    {
      /* 0x3FFFFFFF is MAX_INT32/2 */
      if (version_a >= max_int32_div_2)
	{
	  assert (version_b <= -max_int32_div_2);
	  /* In this case, version_a is considered smaller */
	  return -1;
	}
      else
	{
	  assert (version_b >= -max_int32_div_2);
	  /* In this case, version_b is considered smaller */
	  return 1;
	}
    }
  else
    {
      if (version_b >= max_int32_div_2)
	{
	  assert (version_a <= -max_int32_div_2);
	  /* In this case, version_a is considered bigger */
	  return 1;
	}
      else
	{
	  assert (version_a >= -max_int32_div_2);
	  /* In this case, version_b is considered bigger */
	  return -1;
	}
    }

  /* We shouldn't be here */
  assert (false);
}

/*
 * vacuum_is_page_of_vacuum_data () - Weather or not a page is vacuum data.
 *
 * return    : true - Yes, false - No
 * vpid (in) :
 */
bool
vacuum_is_page_of_vacuum_data (VPID * vpid)
{
  int diff;

  if (vacuum_Data_vpid.volid != vpid->volid)
    {
      return false;
    }

  diff = vpid->pageid - vacuum_Data_vpid.pageid;
  return 0 <= diff && diff < vacuum_Data_npages;
}

#if !defined (NDEBUG)
/*
 * vacuum_verify_vacuum_data_debug () - Debugging function that can detect
 *					vacuum data anomalies.
 *
 * return    : Void.
 */
static void
vacuum_verify_vacuum_data_debug (void)
{
  int i;
  VACUUM_DATA_ENTRY *entry = NULL;
  VACUUM_LOG_BLOCKID blockid;

  for (i = 0; i < vacuum_Data->n_table_entries; i++)
    {
      entry = VACUUM_DATA_GET_ENTRY (i);
      blockid = VACUUM_DATA_ENTRY_BLOCKID (entry);

      /* Check entry oldest/newest MVCCID are included in the interval formed
       * by vacuum data aggregated oldest/newest MVCID.
       */
      assert (!MVCC_ID_PRECEDES (entry->oldest_mvccid,
				 vacuum_Data->oldest_mvccid));
      assert (!MVCC_ID_PRECEDES (vacuum_Data->newest_mvccid,
				 entry->newest_mvccid));

      /* Check start_lsa matched blockid. */
      assert (vacuum_get_log_blockid (entry->start_lsa.pageid) == blockid);

      if (i < vacuum_Data->n_table_entries - 1)
	{
	  /* Check order of blocks is correct. */
	  assert (blockid
		  <
		  VACUUM_DATA_ENTRY_BLOCKID (VACUUM_DATA_GET_ENTRY (i + 1)));
	}
      else
	{
	  /* Check vacuum data last_blockid is not less than biggest blockid
	   * in table.
	   */
	  assert (blockid <= vacuum_Data->last_blockid);
	}
    }
}
#endif /* !NDEBUG */

#if defined(SERVER_MODE)
/*
 * vacuum_log_prefetch_vacuum_block () - Pre-fetches from log page buffer or
 *				         from disk, (almost) all log pages
 *					 required by a vacuum block
 * thread_p (in):
 * entry (in): vacuum data entry
 * block_log_buffer (in/out): block log buffer identifier
 *
 * Note : this function does not handle cases when last log entry in 'start_lsa'
 *	  page of vacuum data entry spans for more than extra one log page.
 *	  Only one extra page is loaded after the 'start_lsa' page.
 *	  Please note that 'start_lsa' page is the last log page (logically),
 *	  the vacuum will require log pages before this one.
 */
static int
vacuum_log_prefetch_vacuum_block (THREAD_ENTRY * thread_p,
				  VACUUM_DATA_ENTRY * entry,
				  BLOCK_LOG_BUFFER * block_log_buffer)
{
  int i;
  char *buffer_block_start_ptr;
  char *log_page;
  LOG_PAGEID start_log_pageid, log_pageid;
  VACUUM_WORKER *worker = VACUUM_GET_VACUUM_WORKER (thread_p);
  int error = NO_ERROR;

  assert (entry != NULL);
  assert (block_log_buffer != NULL);

  if (vacuum_Prefetch_log_mode == VACUUM_PREFETCH_LOG_MODE_MASTER)
    {
      block_log_buffer->buffer_id =
	lf_bitmap_get_entry (&vacuum_Prefetch_free_buffers_bitmap);
      if (block_log_buffer->buffer_id < 0)
	{
	  assert (false);
	  vacuum_er_log (VACUUM_ER_LOG_ERROR | VACUUM_ER_LOG_MASTER,
			 "VACUUM ERROR: Could not prefetch. "
			 "No more free log block buffers.");
	  return ER_FAILED;
	}
      buffer_block_start_ptr =
	VACUUM_PREFETCH_LOG_BLOCK_BUFFER (block_log_buffer->buffer_id);
    }
  else
    {
      buffer_block_start_ptr = worker->prefetch_log_buffer;
    }

  start_log_pageid =
    VACUUM_FIRST_LOG_PAGEID_IN_BLOCK (VACUUM_DATA_ENTRY_BLOCKID (entry));
  log_pageid = start_log_pageid;

  log_page = buffer_block_start_ptr;
  for (i = 0; i < VACUUM_PREFETCH_LOG_BLOCK_BUFFER_PAGES
       && log_pageid <= entry->start_lsa.pageid + 1; i++, log_pageid++)
    {
      if (logpb_fetch_page (thread_p, log_pageid,
			    (LOG_PAGE *) log_page) == NULL)
	{
	  vacuum_er_log (VACUUM_ER_LOG_ERROR,
			 "VACUUM ERROR : cannot prefetch log page %d",
			 log_pageid);
	  if (vacuum_Prefetch_log_mode == VACUUM_PREFETCH_LOG_MODE_MASTER)
	    {
	      lf_bitmap_free_entry (&vacuum_Prefetch_free_buffers_bitmap,
				    block_log_buffer->buffer_id);
	      block_log_buffer->buffer_id = VACUUM_LOG_BLOCK_BUFFER_INVALID;
	    }

	  error = ER_FAILED;
	  goto end;
	}

      log_page += LOG_PAGESIZE;
    }

  block_log_buffer->start_page = start_log_pageid;
  block_log_buffer->last_page = start_log_pageid + i - 1;

  vacuum_er_log (VACUUM_ER_LOG_MASTER,
		 "VACUUM : prefetched %d log pages from %lld to %lld", i,
		 (long long int) block_log_buffer->start_page,
		 (long long int) block_log_buffer->last_page);

end:
  return error;
}
#endif /* SERVER_MODE */

/*
 * vacuum_copy_log_page () - Loads a log page to be processed by vacuum from
 *			     vacuum block buffer or log page buffer or disk log
 *			     archive.
 *
 * thread_p (in):
 * log_pageid (in): log page logical id
 * block_log_buffer (in): block log buffer identifier
 * log_page_p (in/out): pre-allocated buffer to store one log page
 *
 */
static int
vacuum_copy_log_page (THREAD_ENTRY * thread_p, LOG_PAGEID log_pageid,
		      BLOCK_LOG_BUFFER * block_log_buffer,
		      LOG_PAGE * log_page_p)
{
#if defined (SERVER_MODE)
  char *buffer_block_start_ptr;
  char *buffer_page_start_ptr;
  VACUUM_WORKER *worker = VACUUM_GET_VACUUM_WORKER (thread_p);
#endif /* SERVER_MODE */
  int error = NO_ERROR;

  assert (log_page_p != NULL);

#if defined (SERVER_MODE)
  if (block_log_buffer != NULL
      && log_pageid >= block_log_buffer->start_page
      && log_pageid <= block_log_buffer->last_page)
    {
      if (vacuum_Prefetch_log_mode == VACUUM_PREFETCH_LOG_MODE_MASTER)
	{
	  buffer_block_start_ptr =
	    VACUUM_PREFETCH_LOG_BLOCK_BUFFER (block_log_buffer->buffer_id);
	}
      else
	{
	  buffer_block_start_ptr = worker->prefetch_log_buffer;
	}
      buffer_page_start_ptr =
	buffer_block_start_ptr
	+ (log_pageid - block_log_buffer->start_page) * LOG_PAGESIZE;

      /* log page is cached */
      memcpy (log_page_p, buffer_page_start_ptr, LOG_PAGESIZE);

      mnt_vac_prefetch_log_hits_pages (thread_p);
    }
  else
#endif /* SERVER_MODE */
    {
      if (logpb_fetch_page (thread_p, log_pageid, log_page_p) == NULL)
	{
	  logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
			     "vacuum_copy_log_page");
	  error = ER_FAILED;
	}
    }

  mnt_vac_prefetch_log_requests_pages (thread_p);

  return error;
}

/*
 * print_not_vacuumed_to_log () - prints to log info related to a not vacuumed
 *				  OID (either from HEAP or BTREE)
 *
 * rerturn: void.
 * oid (in): The not vacuumed instance OID
 * class_oid (in): The class to which belongs the oid
 * rec_header (in): The record header of the not vacuumed record
 * btree_node_type (in): If the oid is not vacuumed from BTREE then this is
 *			 the type node. If <0 then the OID comes from heap. 
 *
 */
static void
print_not_vacuumed_to_log (OID * oid, OID * class_oid,
			   MVCC_REC_HEADER * rec_header, int btree_node_type)
{
#define TEMP_BUFFER_SIZE 1024
  char mess[TEMP_BUFFER_SIZE], *p = mess;
  bool is_btree = (btree_node_type >= 0 ? true : false);

  if (is_btree)
    {
      p += sprintf (p, "Found not vacuumed BTREE record");
    }
  else
    {
      p += sprintf (p, "Found not vacuumed HEAP record");
    }
  p +=
    sprintf (p, " with oid=%d|%d|%d, class_oid=%d|%d|%d", (int) oid->volid,
	     oid->pageid, (int) oid->slotid, (int) class_oid->volid,
	     class_oid->pageid, (int) class_oid->slotid);
  if (MVCC_IS_FLAG_SET (rec_header, OR_MVCC_FLAG_VALID_INSID))
    {
      p +=
	sprintf (p, ", insert_id=%llu",
		 (unsigned long long int) MVCC_GET_INSID (rec_header));
    }
  else
    {
      p += sprintf (p, ", insert_id=missing");
    }
  if (MVCC_IS_FLAG_SET (rec_header, OR_MVCC_FLAG_VALID_DELID))
    {
      p +=
	sprintf (p, ", delete_id=%llu",
		 (unsigned long long int) MVCC_GET_DELID (rec_header));
    }
  else
    {
      p += sprintf (p, ", delete_id=missing");
    }
  p +=
    sprintf (p, ", oldest_mvcc_id=%llu",
	     (unsigned long long int) vacuum_Data->oldest_mvccid);
  if (is_btree)
    {
      const char *type_str = NULL;

      switch (btree_node_type)
	{
	case BTREE_LEAF_NODE:
	  type_str = "LEAF";
	  break;
	case BTREE_NON_LEAF_NODE:
	  type_str = "NON_LEAF";
	  break;
	case BTREE_OVERFLOW_NODE:
	  type_str = "OVERFLOW";
	  break;
	default:
	  type_str = "UNKNOWN";
	  break;
	}
      p += sprintf (p, ", node_type=%s", type_str);
    }
  p += sprintf (p, "\n");

  er_log_debug (ARG_FILE_LINE, mess);
}

/*
 * vacuum_check_not_vacuumed_recdes () - checks if an OID should've been
 *					 vacuumed (using a record descriptor)
 *
 * return: DISK_INVALID if the OID was not vacuumed, DISK_VALID if it was
 *	   and DISK_ERROR in case of an error.
 * thread_p (in):
 * oid (in): The not vacuumed instance OID
 * class_oid (in): The class to which belongs the oid
 * recdes (in): The not vacuumed record
 * btree_node_type (in): If the oid is not vacuumed from BTREE then this is
 *			 the type node. If <0 then the OID comes from heap. 
 *
 */
DISK_ISVALID
vacuum_check_not_vacuumed_recdes (THREAD_ENTRY * thread_p, OID * oid,
				  OID * class_oid, RECDES * recdes,
				  int btree_node_type)
{
  MVCC_REC_HEADER rec_header;

  if (or_mvcc_get_header (recdes, &rec_header) != NO_ERROR)
    {
      return DISK_ERROR;
    }

  return vacuum_check_not_vacuumed_rec_header (thread_p, oid, class_oid,
					       &rec_header, btree_node_type);
}

/*
 * is_not_vacuumed_and_lost () - checks if a record should've been vacuumed
 *				 (using a record header)
 *
 * return: true if the record was not vacuumed and is completely lost.
 * thread_p (in):
 * recdes (in): The header of the record to be checked
 *
 */
static bool
is_not_vacuumed_and_lost (THREAD_ENTRY * thread_p,
			  MVCC_REC_HEADER * rec_header)
{
  MVCC_SATISFIES_VACUUM_RESULT res;
  bool not_vacuumed = false;

  res =
    mvcc_satisfies_vacuum (thread_p, rec_header, vacuum_Data->oldest_mvccid);
  switch (res)
    {
    case VACUUM_RECORD_REMOVE:
      return true;

    case VACUUM_RECORD_DELETE_INSID:
      return MVCC_IS_FLAG_SET (rec_header, OR_MVCC_FLAG_VALID_INSID);

    case VACUUM_RECORD_CANNOT_VACUUM:
      return false;

    default:
      return false;
    }
}

/*
 * vacuum_check_not_vacuumed_rec_header () - checks if an OID should've been
 *					     vacuumed (using a record header)
 *
 * return: DISK_INVALID if the OID was not vacuumed, DISK_VALID if it was
 *	   and DISK_ERROR in case of an error.
 * thread_p (in):
 * oid (in): The not vacuumed instance OID
 * class_oid (in): The class to which belongs the oid
 * recdes (in): The not vacuumed record header
 * btree_node_type (in): If the oid is not vacuumed from BTREE then this is
 *			 the type node. If <0 then the OID comes from heap. 
 *
 */
DISK_ISVALID
vacuum_check_not_vacuumed_rec_header (THREAD_ENTRY * thread_p, OID * oid,
				      OID * class_oid,
				      MVCC_REC_HEADER * rec_header,
				      int btree_node_type)
{
  if (is_not_vacuumed_and_lost (thread_p, rec_header))
    {
      OID cls_oid;
      if (class_oid == NULL || OID_ISNULL (class_oid))
	{
	  if (heap_get_class_oid (thread_p, &cls_oid, oid) != S_SUCCESS)
	    {
	      ASSERT_ERROR ();
	      return DISK_ERROR;
	    }
	  class_oid = &cls_oid;
	}
      print_not_vacuumed_to_log (oid, class_oid, rec_header, btree_node_type);

      assert (false);
      return DISK_INVALID;
    }

  return DISK_VALID;
}

/*
 * vacuum_get_first_page_dropped_files () - Get the first allocated vpid of 
 *			     vacuum_Dropped_files_vfid.
 *
 * return    : VPID *
 * thread_p (in):
 * first_page_vpid (out): 
 *
 */
static VPID *
vacuum_get_first_page_dropped_files (THREAD_ENTRY * thread_p,
				     VPID * first_page_vpid)
{
  VPID *vpid;

  assert (!VFID_ISNULL (&vacuum_Dropped_files_vfid));

  vpid = file_get_first_alloc_vpid (thread_p, &vacuum_Dropped_files_vfid,
				    first_page_vpid);

  assert (!VPID_ISNULL (first_page_vpid));

  return vpid;
}

/*
 * vacuum_get_first_page_vacuum_data () - Get the first allocated vpid of
 *			     vacuum_Data_vfid.
 *
 * return    : VPID *
 * thread_p (in):
 * first_page_vpid (out): 
 *
 */
static VPID *
vacuum_get_first_page_vacuum_data (THREAD_ENTRY * thread_p,
				   VPID * first_page_vpid)
{
  VPID *vpid;

  assert (!VFID_ISNULL (&vacuum_Data_vfid));

  vpid = file_get_first_alloc_vpid (thread_p, &vacuum_Data_vfid,
				    first_page_vpid);

  assert (!VPID_ISNULL (first_page_vpid));

  return vpid;
}

/*
 * vacuum_is_mvccid_vacuumed () - Return true if MVCCID should be vacuumed.
 *				  It must be older than
 *				  vacuum_Data->oldest_mvccid.
 *
 * return  : True/false.
 * id (in) : MVCCID to check.
 */
bool
vacuum_is_mvccid_vacuumed (MVCCID id)
{
  if (id < vacuum_Data->oldest_mvccid)
    {
      return true;
    }

  return false;
}

/*
 * vacuum_log_redoundo_vacuum_record () - Log vacuum of a REL or BIG heap record
 *
 * return	      : Error code.
 * thread_p (in)      : Thread entry.
 * page_p (in)	      : Page pointer.
 * slotid (in)	      : slot id
 * undo_recdes (in)   : record descriptor before vacuuming
 * results (in)	      : Satisfies vacuum result.
 * next_versions (in) : OID of next versions.
 * partition_oids (in): partition OIDs.
 * reusable (in)      :
 *
 * NOTE: Some values in slots array are modified and set to negative values.
 */
static void
vacuum_log_redoundo_vacuum_record (THREAD_ENTRY * thread_p, PAGE_PTR page_p,
				   PGSLOTID slotid, RECDES * undo_recdes,
				   OID * next_version_oid,
				   OID * partition_oid, bool reusable)
{
  char *ptr = NULL, *buffer_p = NULL;
  char buffer[2 * OR_OID_SIZE + MAX_ALIGNMENT];
  LOG_DATA_ADDR addr;
  LOG_CRUMB crumbs[4];
  LOG_CRUMB *undo_crumbs = &crumbs[0];
  LOG_CRUMB *redo_crumbs = &crumbs[2];
  int num_undo_crumbs;
  int num_redo_crumbs;
  int packed_size;

  assert (slotid >= 0 && slotid < ((SPAGE_HEADER *) page_p)->num_slots);

  /* Initialize log data. */
  addr.offset = slotid;
  addr.pgptr = page_p;
  addr.vfid = NULL;

  num_redo_crumbs = 0;
  if (reusable)
    {
      addr.offset |= VACUUM_LOG_VACUUM_HEAP_REUSABLE;
    }
  else if (next_version_oid != NULL && !OID_ISNULL (next_version_oid))
    {
      buffer_p = PTR_ALIGN (buffer, MAX_ALIGNMENT);
      ptr = buffer_p;
      ptr = PTR_ALIGN (ptr, OR_OID_SIZE);
      memcpy (ptr, next_version_oid, OR_OID_SIZE);

      ptr += OR_OID_SIZE;
      packed_size = OR_OID_SIZE;

      if (partition_oid != NULL && !OID_ISNULL (partition_oid))
	{
	  /* Pack partition OIDs */
	  memcpy (ptr, partition_oid, OR_OID_SIZE);
	  ptr += OR_OID_SIZE;
	  packed_size += OR_OID_SIZE;
	}

      redo_crumbs[0].length = packed_size;
      redo_crumbs[0].data = buffer_p;
      num_redo_crumbs = 1;
    }

  undo_crumbs[0].length = sizeof (undo_recdes->type);
  undo_crumbs[0].data = (char *) &undo_recdes->type;
  undo_crumbs[1].length = undo_recdes->length;
  undo_crumbs[1].data = undo_recdes->data;
  num_undo_crumbs = 2;

  log_append_undoredo_crumbs (thread_p, RVVAC_HEAP_RECORD_VACUUM, &addr,
			      num_undo_crumbs, num_redo_crumbs, undo_crumbs,
			      (num_redo_crumbs == 0) ? NULL : redo_crumbs);
}

/*
 * vacuum_rv_undo_vacuum_heap_record () - undo function for
 *					  RVVAC_HEAP_RECORD_VACUUM
 *
 * return	 : Error code.
 * thread_p (in) : Thread entry.
 * rcv (in)	 : Recovery structure.
 */
int
vacuum_rv_undo_vacuum_heap_record (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  INT16 recdes_type;

  rcv->offset = (rcv->offset & (~VACUUM_LOG_VACUUM_HEAP_MASK));

  if (spage_is_slot_exist (rcv->pgptr, rcv->offset))
    {
      recdes_type = *(INT16 *) (rcv->data);

      assert ((recdes_type == REC_RELOCATION || recdes_type == REC_BIGONE)
	      && (spage_get_record_type (rcv->pgptr, rcv->offset)
		  == REC_MVCC_NEXT_VERSION));

      return heap_rv_undoredo_update (thread_p, rcv);
    }
  else
    {
      return heap_rv_redo_insert (thread_p, rcv);
    }
}

/*
 * vacuum_rv_redo_vacuum_heap_record () - redo function for
 *					  RVVAC_HEAP_RECORD_VACUUM
 *
 * return	 : Error code.
 * thread_p (in) : Thread entry.
 * rcv (in)	 : Recovery structure.
 */
int
vacuum_rv_redo_vacuum_heap_record (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  OID *next_version_oid = NULL;
  OID *partition_oid = NULL;
  char *ptr;
  INT16 slotid;
  bool reusable;

  slotid = (rcv->offset & (~VACUUM_LOG_VACUUM_HEAP_MASK));
  reusable = (rcv->offset & VACUUM_LOG_VACUUM_HEAP_REUSABLE) != 0;

  if (rcv->data != NULL && rcv->length > 0)
    {
      assert (!reusable);

      ptr = (char *) rcv->data;
      ptr = PTR_ALIGN (ptr, OR_OID_SIZE);
      next_version_oid = (OID *) ptr;

      ptr += OR_OID_SIZE;

      if (ptr < rcv->data + rcv->length)
	{
	  partition_oid = (OID *) ptr;
	  ptr += OR_OID_SIZE;
	}
    }

  if (spage_vacuum_slot (thread_p, rcv->pgptr, slotid, next_version_oid,
			 partition_oid, reusable) != NO_ERROR)
    {
      assert_release (false);
      return ER_FAILED;
    }

  if (spage_need_compact (thread_p, rcv->pgptr) == true)
    {
      (void) spage_compact (rcv->pgptr);
    }

  pgbuf_set_dirty (thread_p, rcv->pgptr, DONT_FREE);

  return NO_ERROR;
}

/*
 * vacuum_cache_log_postpone_redo_data () - Cache redo data for log postpone
 *					    of vacuum operation.
 *
 * return		: Void.
 * thread_p (in)	: Thread entry.
 * data_header (in)	: Recovery data header (struct log_redo).
 * rcv_data (in)	: Recovery redo data.
 * rcv_data_length (in) : Recovery data size.
 */
void
vacuum_cache_log_postpone_redo_data (THREAD_ENTRY * thread_p,
				     char *data_header, char *rcv_data,
				     int rcv_data_length)
{
#if defined (SERVER_MODE)
  VACUUM_WORKER *worker = VACUUM_GET_VACUUM_WORKER (thread_p);
  VACUUM_CACHE_POSTPONE_ENTRY *new_entry = NULL;
  int total_data_size = 0;

  assert (worker != NULL);
  assert (data_header != NULL);
  assert (rcv_data_length == 0 || rcv_data != NULL);

  if (worker->postpone_cache_status == VACUUM_CACHE_POSTPONE_OVERFLOW)
    {
      /* Cannot cache postpones. */
      return;
    }

  if (worker->postpone_cache_status == VACUUM_CACHE_POSTPONE_NO)
    {
      /* Initialize data to cache postpones. */
      worker->postpone_cached_entries_count = 0;
      worker->postpone_redo_data_ptr = worker->postpone_redo_data_buffer;
      worker->postpone_cache_status = VACUUM_CACHE_POSTPONE_YES;
    }

  assert (worker->postpone_cached_entries_count
	  <= VACUUM_CACHE_POSTPONE_ENTRIES_MAX_COUNT);
  assert (worker->postpone_redo_data_ptr != NULL);
  assert (worker->postpone_redo_data_ptr
	  >= worker->postpone_redo_data_buffer);
  assert (CAST_BUFLEN (worker->postpone_redo_data_ptr
		       - worker->postpone_redo_data_buffer) <= IO_PAGESIZE);
  ASSERT_ALIGN (worker->postpone_redo_data_ptr, MAX_ALIGNMENT);

  if (worker->postpone_cached_entries_count
      == VACUUM_CACHE_POSTPONE_ENTRIES_MAX_COUNT)
    {
      /* Could not store all postpone records. */
      worker->postpone_cache_status = VACUUM_CACHE_POSTPONE_OVERFLOW;
      return;
    }

  /* Check if recovery data fits in preallocated buffer. */
  total_data_size =
    CAST_BUFLEN (worker->postpone_redo_data_ptr
		 - worker->postpone_redo_data_buffer);
  total_data_size += sizeof (struct log_redo);
  total_data_size += rcv_data_length;
  total_data_size += 2 * MAX_ALIGNMENT;
  if (total_data_size > IO_PAGESIZE)
    {
      /* Cannot store all recovery data. */
      worker->postpone_cache_status = VACUUM_CACHE_POSTPONE_OVERFLOW;
      return;
    }

  /* Cache a new postpone log record entry. */
  new_entry =
    &worker->postpone_cached_entries[worker->postpone_cached_entries_count];
  new_entry->redo_data = worker->postpone_redo_data_ptr;

  /* Cache struct log_redo from data_header */
  memcpy (worker->postpone_redo_data_ptr, data_header,
	  sizeof (struct log_redo));
  worker->postpone_redo_data_ptr += sizeof (struct log_redo);
  worker->postpone_redo_data_ptr =
    PTR_ALIGN (worker->postpone_redo_data_ptr, MAX_ALIGNMENT);

  /* Cache recovery data. */
  assert (((struct log_redo *) data_header)->length == rcv_data_length);
  if (rcv_data_length > 0)
    {
      memcpy (worker->postpone_redo_data_ptr, rcv_data, rcv_data_length);
      worker->postpone_redo_data_ptr += rcv_data_length;
      worker->postpone_redo_data_ptr =
	PTR_ALIGN (worker->postpone_redo_data_ptr, MAX_ALIGNMENT);
    }

  /* LSA will be saved later. */
  LSA_SET_NULL (&new_entry->lsa);
#endif /* SERVER_MODE */
}

/*
 * vacuum_cache_log_postpone_lsa () - Save LSA of postpone operations.
 *
 * return	 : Void.
 * thread_p (in) : Thread entry.
 * lsa (in)	 : Log postpone LSA.
 *
 * NOTE: This saves LSA after a new entry and its redo data have already been
 *	 added. They couldn't both be added in the same step.
 */
void
vacuum_cache_log_postpone_lsa (THREAD_ENTRY * thread_p, LOG_LSA * lsa)
{
#if defined (SERVER_MODE)
  VACUUM_WORKER *worker = VACUUM_GET_VACUUM_WORKER (thread_p);
  VACUUM_CACHE_POSTPONE_ENTRY *new_entry = NULL;

  assert (lsa != NULL && !LSA_ISNULL (lsa));
  assert (worker != NULL);
  assert (worker->postpone_cache_status != VACUUM_CACHE_POSTPONE_NO);

  if (worker->postpone_cache_status == VACUUM_CACHE_POSTPONE_OVERFLOW)
    {
      return;
    }
  assert (worker->postpone_cached_entries_count >= 0);
  assert (worker->postpone_cached_entries_count
	  < VACUUM_CACHE_POSTPONE_ENTRIES_MAX_COUNT);
  new_entry =
    &worker->postpone_cached_entries[worker->postpone_cached_entries_count];
  LSA_COPY (&new_entry->lsa, lsa);

  /* Now that all needed data is saved, increment cached entries counter. */
  worker->postpone_cached_entries_count++;
#endif /* SERVER_MODE */
}

/*
 * vacuum_do_postpone_from_cache () - Do postpone from vacuum worker's cached
 *				      postpone entries.
 *
 * return		   : True if postpone was run from cached entries,
 *			     false otherwise.
 * thread_p (in)	   : Thread entry.
 * start_postpone_lsa (in) : Start postpone LSA.
 */
bool
vacuum_do_postpone_from_cache (THREAD_ENTRY * thread_p,
			       LOG_LSA * start_postpone_lsa)
{
#if defined (SERVER_MODE)
  VACUUM_WORKER *worker = VACUUM_GET_VACUUM_WORKER (thread_p);
  VACUUM_CACHE_POSTPONE_ENTRY *entry = NULL;
  struct log_redo *redo = NULL;
  char *rcv_data = NULL;
  int i;
  int start_index = -1;

  assert (start_postpone_lsa != NULL && !LSA_ISNULL (start_postpone_lsa));
  assert (worker != NULL);
  assert (worker->postpone_cache_status != VACUUM_CACHE_POSTPONE_NO);

  if (worker->postpone_cache_status == VACUUM_CACHE_POSTPONE_OVERFLOW)
    {
      /* Cache is not usable. */
      worker->postpone_cache_status = VACUUM_CACHE_POSTPONE_NO;
      return false;
    }
  /* First cached postpone entry at start_postpone_lsa. */
  for (i = 0; i < worker->postpone_cached_entries_count; i++)
    {
      entry = &worker->postpone_cached_entries[i];
      if (LSA_EQ (&entry->lsa, start_postpone_lsa))
	{
	  /* Found start lsa. */
	  start_index = i;
	  break;
	}
    }
  if (start_index < 0)
    {
      /* Start LSA was not found. Unexpected situation. */
      assert (false);
      return false;
    }

  /* Run all postpones after start_index. */
  for (i = start_index; i < worker->postpone_cached_entries_count; i++)
    {
      entry = &worker->postpone_cached_entries[i];
      /* Get redo data header. */
      redo = (struct log_redo *) entry->redo_data;
      /* Get recovery data. */
      rcv_data = entry->redo_data + sizeof (struct log_redo);
      rcv_data = PTR_ALIGN (rcv_data, MAX_ALIGNMENT);
      (void) log_execute_run_postpone (thread_p, &entry->lsa, redo, rcv_data);
    }
  /* Finished running postpones. */
  if (start_index == 0)
    {
      /* All postpone entries were run. */
      worker->postpone_cache_status = VACUUM_CACHE_POSTPONE_NO;
    }
  else
    {
      /* Only some postpone entries were run. Update the number of entries
       * which should be run on next commit.
       */
      worker->postpone_cached_entries_count = start_index;
    }
  return true;
#else /* !SERVER_MODE */
  return false;
#endif /* !SERVER_MODE */
}
