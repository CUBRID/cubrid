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
 * vacuum.c - Vacuuming system implementation.
 *
 */
#include "system.h"
#include "vacuum.h"

#include "base_flag.hpp"
#include "boot_sr.h"
#include "btree.h"
#include "dbtype.h"
#include "heap_file.h"
#include "lockfree_circular_queue.hpp"
#include "log_append.hpp"
#include "log_compress.h"
#include "log_lsa.hpp"
#include "log_impl.h"
#include "mvcc.h"
#include "mvcc_table.hpp"
#include "object_representation.h"
#include "object_representation_sr.h"
#include "overflow_file.h"
#include "page_buffer.h"
#include "perf_monitor.h"
#include "resource_shared_pool.hpp"
#include "thread_entry_task.hpp"
#if defined (SERVER_MODE)
#include "thread_daemon.hpp"
#endif /* SERVER_MODE */
#include "thread_looper.hpp"
#include "thread_manager.hpp"
#if defined (SERVER_MODE)
#include "thread_worker_pool.hpp"
#include "monitor_vacuum_ovfp_threshold.hpp"
#endif // SERVER_MODE
#include "util_func.h"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <stack>

#include <cstring>
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

/* The maximum number of slots in a page if all of them are empty.
 * IO_MAX_PAGE_SIZE is used for page size and any headers are ignored (it
 * wouldn't bring a significant difference).
 */
#define MAX_SLOTS_IN_PAGE (IO_MAX_PAGE_SIZE / sizeof (SPAGE_SLOT))

/* The default number of cached entries in a vacuum statistics cache */
#define VACUUM_STATS_CACHE_SIZE 100

/* Get first log page identifier in a log block */
#define VACUUM_FIRST_LOG_PAGEID_IN_BLOCK(blockid) \
  ((blockid) * vacuum_Data.log_block_npages)
/* Get last log page identifier in a log block */
#define VACUUM_LAST_LOG_PAGEID_IN_BLOCK(blockid) \
  (VACUUM_FIRST_LOG_PAGEID_IN_BLOCK (blockid + 1) - 1)

/*
 * Vacuum data section.
 * Vacuum data contains useful information for the vacuum process. There are
 * several fields, among which a table of entries which describe the progress
 * of processing log data for vacuum.
 *
 * Vacuum data is organized as a queue of VACUUM_DATA_PAGE pages. Each page has a header and an array of
 * VACUUM_DATA_ENTRY.
 *
 * The vacuum_Data global variable keeps useful meta-data which does not required disk storage.
 */

/* Vacuum log block data.
 *
 * Stores information on a block of log data relevant for vacuum.c
 */
typedef struct vacuum_data_entry VACUUM_DATA_ENTRY;
struct vacuum_data_entry
{
  // *INDENT-OFF*
  VACUUM_LOG_BLOCKID blockid;       // blockid and flags
  LOG_LSA start_lsa;                // lsa of last mvcc op log record in block
  MVCCID oldest_visible_mvccid;     // oldest visible MVCCID while block was logged
  MVCCID newest_mvccid;             // newest MVCCID in log block

  vacuum_data_entry () = default;
  vacuum_data_entry (const log_lsa & lsa, MVCCID oldest, MVCCID newest);
  vacuum_data_entry (const log_header & hdr);

  VACUUM_LOG_BLOCKID get_blockid () const;

  bool is_available () const;
  bool is_vacuumed () const;
  bool is_job_in_progress () const;
  bool was_interrupted () const;

  void set_vacuumed ();
  void set_job_in_progress ();
  void set_interrupted ();

  // *INDENT-ON*
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

/* Access fields in a vacuum data table entry */
/* Get blockid (use mask to cancel flag bits) */
#define VACUUM_BLOCKID_WITHOUT_FLAGS(blockid) \
  ((blockid) & VACUUM_DATA_ENTRY_BLOCKID_MASK)

/* Get flags from blockid. */
#define VACUUM_BLOCKID_GET_FLAGS(blockid) \
  ((blockid) & VACUUM_DATA_ENTRY_FLAG_MASK)

/* Vacuum block status: requested means that vacuum data has assigned it as
 * a job, but no worker started it yet; running means that a work is currently
 * vacuuming based on this entry's block.
 */
/* Get vacuum block status */
#define VACUUM_BLOCK_STATUS(blockid) \
  ((blockid) & VACUUM_BLOCK_STATUS_MASK)

/* Check vacuum block status */
#define VACUUM_BLOCK_STATUS_IS_VACUUMED(blockid) \
  (VACUUM_BLOCK_STATUS (blockid) == VACUUM_BLOCK_STATUS_VACUUMED)
#define VACUUM_BLOCK_STATUS_IS_IN_PROGRESS(blockid) \
  (VACUUM_BLOCK_STATUS (blockid) == VACUUM_BLOCK_STATUS_IN_PROGRESS_VACUUM)
#define VACUUM_BLOCK_STATUS_IS_AVAILABLE(blockid) \
  (VACUUM_BLOCK_STATUS (blockid) == VACUUM_BLOCK_STATUS_AVAILABLE)

/* Set vacuum block status */
#define VACUUM_BLOCK_STATUS_SET_VACUUMED(blockid) \
  ((blockid) = ((blockid) & ~VACUUM_BLOCK_STATUS_MASK) | VACUUM_BLOCK_STATUS_VACUUMED)
#define VACUUM_BLOCK_STATUS_SET_IN_PROGRESS(blockid) \
  ((blockid) = ((blockid) & ~VACUUM_BLOCK_STATUS_MASK) | VACUUM_BLOCK_STATUS_IN_PROGRESS_VACUUM)
#define VACUUM_BLOCK_STATUS_SET_AVAILABLE(blockid) \
  ((blockid) = ((blockid) & ~VACUUM_BLOCK_STATUS_MASK) | VACUUM_BLOCK_STATUS_AVAILABLE)

#define VACUUM_BLOCK_IS_INTERRUPTED(blockid) \
  (((blockid) & VACUUM_BLOCK_FLAG_INTERRUPTED) != 0)
#define VACUUM_BLOCK_SET_INTERRUPTED(blockid) \
  ((blockid) |= VACUUM_BLOCK_FLAG_INTERRUPTED)
#define VACUUM_BLOCK_CLEAR_INTERRUPTED(blockid) \
  ((blockid) &= ~VACUUM_BLOCK_FLAG_INTERRUPTED)

/* Vacuum data page.
 *
 * One page of vacuum data file.
 */
// *INDENT-OFF*
typedef struct vacuum_data_page VACUUM_DATA_PAGE;
struct vacuum_data_page
{
  VPID next_page;
  INT16 index_unvacuumed;
  INT16 index_free;

  /* First vacuum data entry in page. It is followed by other entries based on the page capacity. */
  VACUUM_DATA_ENTRY data[1];

  static const INT16 INDEX_NOT_FOUND = -1;

  bool is_empty () const;
  bool is_index_valid (INT16 index) const;
  INT16 get_index_of_blockid (VACUUM_LOG_BLOCKID blockid) const;

  VACUUM_LOG_BLOCKID get_first_blockid () const;
};
// *INDENT-ON*
#define VACUUM_DATA_PAGE_HEADER_SIZE (offsetof (VACUUM_DATA_PAGE, data))

/*
 * Overwritten versions of pgbuf_fix, pgbuf_unfix and pgbuf_set_dirty, adapted for the needs of vacuum data.
 *
 * NOTE: These macro's should make sure that first/last vacuum data pages are not unfixed or re-fixed.
 */

/* Fix a vacuum data page. If the VPID matches first or last vacuum data page, then the respective page is returned.
 * Otherwise, the page is fixed from page buffer.
 */
#define vacuum_fix_data_page(thread_p, vpidp)									  \
  /* Check if page is vacuum_Data.first_page */									  \
  (vacuum_Data.first_page != NULL && VPID_EQ (pgbuf_get_vpid_ptr ((PAGE_PTR) vacuum_Data.first_page), vpidp) ?	  \
   /* True: vacuum_Data.first_page */										  \
   vacuum_Data.first_page :											  \
   /* False: check if page is vacuum_Data.last_page. */								  \
   vacuum_Data.last_page != NULL && VPID_EQ (pgbuf_get_vpid_ptr ((PAGE_PTR) vacuum_Data.last_page), vpidp) ?	  \
   /* True: vacuum_Data.last_page */										  \
   vacuum_Data.last_page :											  \
   /* False: fix the page. */									  		  \
   (VACUUM_DATA_PAGE *) pgbuf_fix (thread_p, vpidp, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH))

/* Unfix vacuum data page. If the page is first or last in vacuum data, it is not unfixed. */
#define vacuum_unfix_data_page(thread_p, data_page) \
  do \
    { \
      if ((data_page) != vacuum_Data.first_page && (data_page) != vacuum_Data.last_page) \
	{ \
	  /* Do not unfix first or last page. */ \
	  pgbuf_unfix (thread_p, (PAGE_PTR) (data_page)); \
	} \
      (data_page) = NULL; \
    } while (0)

/* Set page dirty [and free it]. First and last vacuum data page are not freed. */
#define vacuum_set_dirty_data_page(thread_p, data_page, free) \
  do \
    { \
      if ((data_page) != vacuum_Data.first_page && (data_page) != vacuum_Data.last_page) \
	{ \
	  pgbuf_set_dirty (thread_p, (PAGE_PTR) (data_page), free); \
	} \
      else  \
	{ \
	  /* Do not unfix first or last page. */ \
	  pgbuf_set_dirty (thread_p, (PAGE_PTR) (data_page), DONT_FREE); \
	} \
      if ((free) == FREE) \
	{ \
	  (data_page) = NULL; \
	} \
    } while (0)

static inline void
vacuum_set_dirty_data_page_dont_free (cubthread::entry * thread_p, vacuum_data_page * data_page)
{
  assert (data_page != NULL);
  pgbuf_set_dirty (thread_p, (PAGE_PTR) (data_page), DONT_FREE);
}

/* Unfix first and last vacuum data page. */
#define vacuum_unfix_first_and_last_data_page(thread_p) \
  do \
    { \
      if (vacuum_Data.last_page != NULL && vacuum_Data.last_page != vacuum_Data.first_page) \
	{ \
	  pgbuf_unfix (thread_p, (PAGE_PTR) vacuum_Data.last_page); \
	} \
      vacuum_Data.last_page = NULL; \
      if (vacuum_Data.first_page != NULL) \
	{ \
	  pgbuf_unfix (thread_p, (PAGE_PTR) vacuum_Data.first_page); \
	} \
      vacuum_Data.first_page = NULL; \
    } while (0)

// *INDENT-OFF*

//
// vacuum_job_cursor is a class that helps tracking job generation progress. its main indicative of progress is the
//    blockid; however, after removing/adding log blocks to vacuum data this blockid can be relocated to a different
//    page. it is cursor's job to maintain the correct position of blocks.
//
class vacuum_job_cursor
{
  public:
    vacuum_job_cursor ();
    ~vacuum_job_cursor ();

    bool is_valid () const;       // return true if cursor valid (get_current_entry can be called)
    bool is_loaded () const;      // return true if cursor page/index are loaded

    void increment_blockid ();                  // increment cursor blockid
    void set_on_vacuum_data_start ();           // set cursor blockid to first block in vacuum data
    void readjust_to_vacuum_data_changes ();    // readjust cursor blockid after changes to vacuum data

    // getters
    VACUUM_LOG_BLOCKID get_blockid () const;
    const VPID &get_page_vpid () const;
    vacuum_data_page *get_page () const;
    INT16 get_index () const;

    const vacuum_data_entry &get_current_entry () const;    // get current entry; cursor must be valid
    void start_job_on_current_entry () const;

    void force_data_update ();
    void unload ();                                   // unload page/index
    void load ();                                     // load page/index

  private:
    void change_blockid (VACUUM_LOG_BLOCKID blockid);       // reset m_blockid to argument
    void reload ();                                         // reload; if a page is loaded and if it contains current
                                                            // blockid, current configuration is kept
    void search ();                                         // search page/index of cursor blockid

    VACUUM_LOG_BLOCKID m_blockid;     // current cursor blockid
    VACUUM_DATA_PAGE *m_page;         // loaded page of blockid or null
    INT16 m_index;                    // loaded index of blockid or INDEX_NOT_FOUND
};

// helper macros for printing vacuum_job_cursor
#define vacuum_job_cursor_print_format "vacuum_job_cursor(%lld, %d|%d|%d)"
#define vacuum_job_cursor_print_args(cursor) \
  (long long int) (cursor).get_blockid (), VPID_AS_ARGS (&(cursor).get_page_vpid ()), (int) (cursor).get_index ()

class vacuum_shutdown_sequence
{
  public:
    vacuum_shutdown_sequence ();

    void request_shutdown ();
    bool is_shutdown_requested ();
    bool check_shutdown_request ();

  private:
    enum state
    {
      NO_SHUTDOWN,
#if defined (SERVER_MODE)
      SHUTDOWN_REQUESTED,
#endif // SERVER_MODE
      SHUTDOWN_REGISTERED
    };
    state m_state;
#if defined (SERVER_MODE)
    std::mutex m_state_mutex;
    std::condition_variable m_condvar;
#endif // SERVER_MODE
};

/* Vacuum data.
 *
 * Stores data required for vacuum. It is also stored on disk in the first
 * database volume.
 */
typedef struct vacuum_data VACUUM_DATA;
struct vacuum_data
{
  public:
    VFID vacuum_data_file;	/* Vacuum data file VFID. */
    LOG_PAGEID keep_from_log_pageid;	/* Smallest LOG_PAGEID that vacuum may still need for its jobs. */

    MVCCID oldest_unvacuumed_mvccid;	/* Global oldest MVCCID not vacuumed (yet). */

    VACUUM_DATA_PAGE *first_page;	/* Cached first vacuum data page. Usually used to generate new jobs. */
    VACUUM_DATA_PAGE *last_page;	/* Cached last vacuum data page. Usually used to receive new data. */

    int page_data_max_count;	/* Maximum data entries fitting one vacuum data page. */

    int log_block_npages;		/* The number of pages in a log block. */

    bool is_loaded;		/* True if vacuum data is loaded. */
    vacuum_shutdown_sequence shutdown_sequence;
    bool is_archive_removal_safe;	/* Set to true after keep_from_log_pageid is updated. */

    LOG_LSA recovery_lsa;		/* This is the LSA where recovery starts. It will be used to go backward in the log
				   * if data on log blocks must be recovered.
				   */
    bool is_restoredb_session;

  #if defined (SA_MODE)
    bool is_vacuum_complete;
  #endif				/* SA_MODE */

    vacuum_data ()
      : vacuum_data_file (VFID_INITIALIZER)
      , keep_from_log_pageid (NULL_PAGEID)
      , oldest_unvacuumed_mvccid (MVCCID_NULL)
      , first_page (NULL)
      , last_page (NULL)
      , page_data_max_count (0)
      , log_block_npages (0)
      , is_loaded (false)
      , shutdown_sequence ()
      , is_archive_removal_safe (false)
      , recovery_lsa (LSA_INITIALIZER)
      , is_restoredb_session (false)
  #if defined (SA_MODE)
      , is_vacuum_complete (false)
  #endif // SA_MODE
      , m_last_blockid (VACUUM_NULL_LOG_BLOCKID)
    {
    }

    bool is_empty () const;	// returns true if vacuum data has no blocks
    bool has_one_page () const;	// returns true if vacuum data has one page only

    VACUUM_LOG_BLOCKID get_last_blockid () const;	// get last blockid of vacuum data
    VACUUM_LOG_BLOCKID get_first_blockid () const;	// get first blockid of vacuum data; if vacuum data is empty

    // same as last blockid
    void set_last_blockid (VACUUM_LOG_BLOCKID blockid);	// set new value for last blockid of vacuum data

    void update ();
    void set_oldest_unvacuumed_on_boot ();

  private:
    const VACUUM_DATA_ENTRY &get_first_entry () const;
    void upgrade_oldest_unvacuumed (MVCCID mvccid);

    VACUUM_LOG_BLOCKID m_last_blockid;	/* Block id for last vacuum data entry... This entry is actually the id of last
					  * added block which may not even be in vacuum data (being already vacuumed).
					  */
};
static VACUUM_DATA vacuum_Data;
// *INDENT-ON*

/* vacuum data load */
typedef struct vacuum_data_load VACUUM_DATA_LOAD;
struct vacuum_data_load
{
  VPID vpid_first;
  VPID vpid_last;
};
VACUUM_DATA_LOAD vacuum_Data_load = { VPID_INITIALIZER, VPID_INITIALIZER };

/* Vacuum worker structure used by vacuum master thread. */
/* This VACUUM_WORKER structure was designed for the needs of the vacuum workers. However, since the design of
 * vacuum data was changed, and since vacuum master may have to allocate or deallocate disk pages, it needed to make
 * use of system operations and transaction descriptor in similar ways with the workers.
 * To extend that functionality in an easy way and to benefit from the postpone cache optimization, master was also
 * assigned this VACUUM_WORKER.
 */
VACUUM_WORKER vacuum_Master;

/*
 * Vacuum worker/job related structures.
 */
/* A lock-free buffer used for communication between logger transactions and
 * auto-vacuum master. It is advisable to avoid synchronizing running
 * transactions with vacuum threads and for this reason the block data is not
 * added directly to vacuum data.
 */
/* *INDENT-OFF* */
lockfree::circular_queue<vacuum_data_entry> *vacuum_Block_data_buffer = NULL;
/* *INDENT-ON* */
#define VACUUM_BLOCK_DATA_BUFFER_CAPACITY 1024

/* A lock free queue of vacuum jobs. Master will add jobs based on vacuum data
 * and workers will execute the jobs one by one.
 */
/* *INDENT-OFF* */
lockfree::circular_queue<VACUUM_LOG_BLOCKID> *vacuum_Finished_job_queue = NULL;
/* *INDENT-ON* */

/* number or log pages on each block of buffer log prefetch */
#define VACUUM_PREFETCH_LOG_BLOCK_BUFFER_PAGES ((size_t) (1 + vacuum_Data.log_block_npages))

#if defined(SERVER_MODE)
#define VACUUM_MAX_TASKS_IN_WORKER_POOL ((size_t) (3 * prm_get_integer_value (PRM_ID_VACUUM_WORKER_COUNT)))
#endif /* SERVER_MODE */

#define VACUUM_FINISHED_JOB_QUEUE_CAPACITY  2048

#define VACUUM_LOG_BLOCK_BUFFER_INVALID (-1)

/* Convert vacuum worker TRANID to an index in vacuum worker's array */
#define VACUUM_WORKER_INDEX_TO_TRANID(index) \
  (-index + LOG_LAST_VACUUM_WORKER_TRANID)

/* Convert index in vacuum worker's array to TRANID */
#define VACUUM_WORKER_TRANID_TO_INDEX(trid) \
  (-trid + LOG_LAST_VACUUM_WORKER_TRANID)

/* Static array of vacuum workers */
VACUUM_WORKER vacuum_Workers[VACUUM_MAX_WORKER_COUNT];

/* VACUUM_HEAP_HELPER -
 * Structure used by vacuum heap functions.
 */
typedef struct vacuum_heap_helper VACUUM_HEAP_HELPER;
struct vacuum_heap_helper
{
  PAGE_PTR home_page;		/* Home page for objects being vacuumed. */
  VPID home_vpid;		/* VPID of home page. */
  PAGE_PTR forward_page;	/* Used to keep forward page of REC_RELOCATION or first overflow page of REC_BIGONE. */
  OID forward_oid;		/* Link to forward page. */
  PGSLOTID crt_slotid;		/* Slot ID of current record being vacuumed. */
  INT16 record_type;		/* Current record type. */
  RECDES record;		/* Current record data. */

  /* buffer of current record (used by HOME and NEW_HOME) */
  char rec_buf[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT];

  MVCC_REC_HEADER mvcc_header;	/* MVCC header. */

  HFID hfid;			/* Heap file identifier. */
  VFID overflow_vfid;		/* Overflow file identifier. */
  bool reusable;		/* True if heap file has reusable slots. */

  MVCC_SATISFIES_VACUUM_RESULT can_vacuum;	/* Result of vacuum check. */

  /* Collect data on vacuum. */
  PGSLOTID slots[MAX_SLOTS_IN_PAGE];	/* Slot ID's. */
  MVCC_SATISFIES_VACUUM_RESULT results[MAX_SLOTS_IN_PAGE];	/* Vacuum check results. */

  OID forward_link;		/* REC_BIGONE, REC_RELOCATION forward links. (buffer for forward_recdes) */
  RECDES forward_recdes;	/* Record descriptor to read forward links. */

  int n_bulk_vacuumed;		/* Number of vacuumed objects to be logged in bulk mode. */
  int n_vacuumed;		/* Number of vacuumed objects. */
  int initial_home_free_space;	/* Free space in home page before vacuum */

  /* Performance tracking. */
  PERF_UTIME_TRACKER time_track;
};

#define VACUUM_PERF_HEAP_START(thread_p, helper) \
  PERF_UTIME_TRACKER_START (thread_p, &(helper)->time_track);
#define VACUUM_PERF_HEAP_TRACK_PREPARE(thread_p, helper) \
  PERF_UTIME_TRACKER_TIME_AND_RESTART (thread_p, &(helper)->time_track, \
				       PSTAT_HEAP_VACUUM_PREPARE)
#define VACUUM_PERF_HEAP_TRACK_EXECUTE(thread_p, helper) \
  PERF_UTIME_TRACKER_TIME_AND_RESTART (thread_p, &(helper)->time_track, \
				       PSTAT_HEAP_VACUUM_EXECUTE)
#define VACUUM_PERF_HEAP_TRACK_LOGGING(thread_p, helper) \
  PERF_UTIME_TRACKER_TIME_AND_RESTART (thread_p, &(helper)->time_track, \
				       PSTAT_HEAP_VACUUM_LOG)

/* Flags used to mark rcv->offset with hints about recovery process. */
/* Flags for reusable heap files. */
#define VACUUM_LOG_VACUUM_HEAP_REUSABLE	      0x8000
/* Flag if page is entirely vacuumed. */
#define VACUUM_LOG_VACUUM_HEAP_ALL_VACUUMED   0x4000
/* Mask. */
#define VACUUM_LOG_VACUUM_HEAP_MASK	      0xC000

/* The buffer size of collected heap objects during a vacuum job. */
#define VACUUM_DEFAULT_HEAP_OBJECT_BUFFER_SIZE  4000

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
  OID class_oid;
};

bool vacuum_Is_booted = false;
#if defined (SERVER_MODE)
class ovfp_threshold_mgr g_ovfp_threshold_mgr;
#endif

/* Logging */
#define VACUUM_LOG_DATA_ENTRY_MSG(name) \
  "name = {blockid = %lld, flags = %lld, start_lsa = %lld|%d, oldest_visible_mvccid=%llu, newest_mvccid=%llu }"
#define VACUUM_LOG_DATA_ENTRY_AS_ARGS(data) \
  (long long) VACUUM_BLOCKID_WITHOUT_FLAGS ((data)->blockid), (long long) VACUUM_BLOCKID_GET_FLAGS ((data)->blockid), \
  LSA_AS_ARGS (&(data)->start_lsa), (unsigned long long) (data)->oldest_visible_mvccid, \
  (unsigned long long) (data)->newest_mvccid

/* Vacuum static functions. */
static void vacuum_update_keep_from_log_pageid (THREAD_ENTRY * thread_p);
static int vacuum_compare_blockids (const void *ptr1, const void *ptr2);
static void vacuum_data_mark_finished (THREAD_ENTRY * thread_p);
static void vacuum_data_empty_page (THREAD_ENTRY * thread_p, VACUUM_DATA_PAGE * prev_data_page,
				    VACUUM_DATA_PAGE ** data_page);
static void vacuum_data_initialize_new_page (THREAD_ENTRY * thread_p, VACUUM_DATA_PAGE * data_page);
static void vacuum_init_data_page_with_last_blockid (THREAD_ENTRY * thread_p, VACUUM_DATA_PAGE * data_page,
						     VACUUM_LOG_BLOCKID blockid);
static int vacuum_recover_lost_block_data (THREAD_ENTRY * thread_p);

static int vacuum_process_log_block (THREAD_ENTRY * thread_p, VACUUM_DATA_ENTRY * block_data,
				     bool sa_mode_partial_block);
static int vacuum_process_log_record (THREAD_ENTRY * thread_p, VACUUM_WORKER * worker, LOG_LSA * log_lsa_p,
				      LOG_PAGE * log_page_p, LOG_DATA * log_record_data, MVCCID * mvccid,
				      char **undo_data_ptr, int *undo_data_size, LOG_VACUUM_INFO * vacuum_info,
				      bool * is_file_dropped, bool stop_after_vacuum_info);
static void vacuum_read_log_aligned (THREAD_ENTRY * thread_entry, LOG_LSA * log_lsa, LOG_PAGE * log_page);
static void vacuum_read_log_add_aligned (THREAD_ENTRY * thread_entry, size_t size, LOG_LSA * log_lsa,
					 LOG_PAGE * log_page);
static void vacuum_read_advance_when_doesnt_fit (THREAD_ENTRY * thread_entry, size_t size, LOG_LSA * log_lsa,
						 LOG_PAGE * log_page);
static void vacuum_copy_data_from_log (THREAD_ENTRY * thread_p, char *area, int length, LOG_LSA * log_lsa,
				       LOG_PAGE * log_page);
static void vacuum_finished_block_vacuum (THREAD_ENTRY * thread_p, VACUUM_DATA_ENTRY * block_data,
					  bool is_vacuum_complete);
static bool vacuum_is_work_in_progress (THREAD_ENTRY * thread_p);
static int vacuum_worker_allocate_resources (THREAD_ENTRY * thread_p, VACUUM_WORKER * worker);
static void vacuum_finalize_worker (THREAD_ENTRY * thread_p, VACUUM_WORKER * worker_info);

static int vacuum_compare_heap_object (const void *a, const void *b);
static int vacuum_collect_heap_objects (THREAD_ENTRY * thread_p, VACUUM_WORKER * worker, OID * oid, VFID * vfid);
static void vacuum_cleanup_collected_by_vfid (VACUUM_WORKER * worker, VFID * vfid);
static int vacuum_heap (THREAD_ENTRY * thread_p, VACUUM_WORKER * worker, MVCCID threshold_mvccid, bool was_interrupted);
static int vacuum_heap_prepare_record (THREAD_ENTRY * thread_p, VACUUM_HEAP_HELPER * helper);
static int vacuum_heap_record_insid_and_prev_version (THREAD_ENTRY * thread_p, VACUUM_HEAP_HELPER * helper);
static int vacuum_heap_record (THREAD_ENTRY * thread_p, VACUUM_HEAP_HELPER * helper);
static int vacuum_heap_get_hfid_and_file_type (THREAD_ENTRY * thread_p, VACUUM_HEAP_HELPER * helper, const VFID * vfid);
static void vacuum_heap_page_log_and_reset (THREAD_ENTRY * thread_p, VACUUM_HEAP_HELPER * helper,
					    bool update_best_space_stat, bool unlatch_page);
static void vacuum_log_vacuum_heap_page (THREAD_ENTRY * thread_p, PAGE_PTR page_p, int n_slots, PGSLOTID * slots,
					 MVCC_SATISFIES_VACUUM_RESULT * results, bool reusable, bool all_vacuumed);
static void vacuum_log_remove_ovf_insid (THREAD_ENTRY * thread_p, PAGE_PTR ovfpage);
static void vacuum_log_redoundo_vacuum_record (THREAD_ENTRY * thread_p, PAGE_PTR page_p, PGSLOTID slotid,
					       RECDES * undo_recdes, bool reusable);
static int vacuum_log_prefetch_vacuum_block (THREAD_ENTRY * thread_p, VACUUM_DATA_ENTRY * entry);
static int vacuum_fetch_log_page (THREAD_ENTRY * thread_p, LOG_PAGEID log_pageid, LOG_PAGE * log_page);

static int vacuum_compare_dropped_files (const void *a, const void *b);
#if defined (SERVER_MODE)
static int vacuum_compare_dropped_files_version (INT32 version_a, INT32 version_b);
#endif // SERVER_MODE
static int vacuum_add_dropped_file (THREAD_ENTRY * thread_p, VFID * vfid, MVCCID mvccid);
static int vacuum_cleanup_dropped_files (THREAD_ENTRY * thread_p);
static int vacuum_find_dropped_file (THREAD_ENTRY * thread_p, bool * is_file_dropped, VFID * vfid, MVCCID mvccid);
static void vacuum_log_cleanup_dropped_files (THREAD_ENTRY * thread_p, PAGE_PTR page_p, INT16 * indexes,
					      INT16 n_indexes);
static void vacuum_dropped_files_set_next_page (THREAD_ENTRY * thread_p, VACUUM_DROPPED_FILES_PAGE * page_p,
						VPID * next_page);
static int vacuum_get_first_page_dropped_files (THREAD_ENTRY * thread_p, VPID * first_page_vpid);
static void vacuum_notify_all_workers_dropped_file (const VFID & vfid_dropped, MVCCID mvccid);

static bool is_not_vacuumed_and_lost (THREAD_ENTRY * thread_p, MVCC_REC_HEADER * rec_header);
static void print_not_vacuumed_to_log (OID * oid, OID * class_oid, MVCC_REC_HEADER * rec_header, int btree_node_type);

static bool vacuum_is_empty (void);
static void vacuum_convert_thread_to_master (THREAD_ENTRY * thread_p, thread_type & save_type);
static void vacuum_convert_thread_to_worker (THREAD_ENTRY * thread_p, VACUUM_WORKER * worker, thread_type & save_type);
static void vacuum_restore_thread (THREAD_ENTRY * thread_p, thread_type save_type);

static void vacuum_data_load_first_and_last_page (THREAD_ENTRY * thread_p);
static void vacuum_data_unload_first_and_last_page (THREAD_ENTRY * thread_p);

static void vacuum_data_empty_update_last_blockid (THREAD_ENTRY * thread_p);

#if defined (SA_MODE)
static void vacuum_sa_run_job (THREAD_ENTRY * thread_p, const VACUUM_DATA_ENTRY & data_entry, bool is_partial,
			       PERF_UTIME_TRACKER & perf_tracker);
#endif // SA_MODE

#if !defined (NDEBUG)
/* Debug function to verify vacuum data. */
static void vacuum_verify_vacuum_data_debug (THREAD_ENTRY * thread_p);
static void vacuum_verify_vacuum_data_page_fix_count (THREAD_ENTRY * thread_p);
#define VACUUM_VERIFY_VACUUM_DATA(thread_p) vacuum_verify_vacuum_data_debug (thread_p)
#else /* NDEBUG */
#define VACUUM_VERIFY_VACUUM_DATA(thread_p)
#endif /* NDEBUG */
static void vacuum_check_shutdown_interruption (const THREAD_ENTRY * thread_p, int error_code);

/* *INDENT-OFF* */
void
vacuum_init_thread_context (cubthread::entry &context, thread_type type, VACUUM_WORKER *worker)
{
  assert (worker != NULL);

  context.type = type;
  context.vacuum_worker = worker;
  context.check_interrupt = false;

  assert (context.get_system_tdes () == NULL);
  context.claim_system_worker ();
}

// class vacuum_master_context_manager
//
//  description:
//    extend entry_manager to override context construction and retirement
//
class vacuum_master_context_manager : public cubthread::daemon_entry_manager
{
  private:
    void on_daemon_create (cubthread::entry &context) final
    {
      // set vacuum master in execute state
      assert (vacuum_Master.state == VACUUM_WORKER_STATE_EXECUTE);
      vacuum_Master.state = VACUUM_WORKER_STATE_EXECUTE;

      vacuum_init_thread_context (context, TT_VACUUM_MASTER, &vacuum_Master);
    }

    void on_daemon_retire (cubthread::entry &context) final
    {
      vacuum_finalize (&context);    // todo: is this the rightful place?

      context.retire_system_worker ();

      if (context.vacuum_worker != NULL)
	{
	  assert (context.vacuum_worker == &vacuum_Master);
	  context.vacuum_worker = NULL;
	}
      else
	{
	  assert (false);
	}
    }
};

class vacuum_master_task : public cubthread::entry_task
{
  public:
    vacuum_master_task () = default;

    void execute (cubthread::entry &thread_ref) final;

  private:
    bool check_shutdown () const;
    bool is_task_queue_full () const;
    bool should_interrupt_iteration () const;         // conditions to interrupt an iteration and go to sleep
    bool is_cursor_entry_ready_to_vacuum () const;    // check if conditions to vacuum cursor entry are met
    bool is_cursor_entry_available () const;          // check if cursor entry is available and can generate a new job
    void start_job_on_cursor_entry () const;          // start job on cursor entry
    bool should_force_data_update () const;           // conditions to force a vacuum data update

    vacuum_job_cursor m_cursor;                       // cursor that iterates through vacuum data entries
    MVCCID m_oldest_visible_mvccid;                   // saved oldest visible mvccid (recomputed on each iteration)
};

// class vacuum_worker_context_manager
//
//  description:
//    extern entry manager to override construction/retirement of vacuum worker context
//
class vacuum_worker_context_manager : public cubthread::entry_manager
{
  public:
    vacuum_worker_context_manager () : cubthread::entry_manager ()
    {
      m_pool = new resource_shared_pool<VACUUM_WORKER> (vacuum_Workers, VACUUM_MAX_WORKER_COUNT);
    }

    ~vacuum_worker_context_manager ()
    {
      delete m_pool;
    }

    VACUUM_WORKER *claim_worker ()
    {
      return m_pool->claim ();
    }
    void retire_worker (VACUUM_WORKER & worker)
    {
      return m_pool->retire (worker);
    }

  private:

    void on_create (cubthread::entry & context) final
    {
      context.tran_index = 0;

      vacuum_init_thread_context (context, TT_VACUUM_WORKER, m_pool->claim ());

      if (vacuum_worker_allocate_resources (&context, context.vacuum_worker) != NO_ERROR)
	{
	  assert (false);
	}

      // get private LRU index
      context.private_lru_index = context.vacuum_worker->private_lru_index;
    }

    void on_retire (cubthread::entry & context) final
    {
      context.retire_system_worker ();

      if (context.vacuum_worker != NULL)
	{
	  context.vacuum_worker->state = VACUUM_WORKER_STATE::VACUUM_WORKER_STATE_INACTIVE;
	  m_pool->retire (*context.vacuum_worker);
	  context.vacuum_worker = NULL;
	}
      else
	{
	  assert (false);
	}

      // reset private LRU index
      context.private_lru_index = -1;
    }

    void on_recycle (cubthread::entry & context) final
    {
      // reset tran_index (it is recycled as NULL_TRAN_INDEX)
      context.tran_index = LOG_SYSTEM_TRAN_INDEX;
    }

    // members
    resource_shared_pool<VACUUM_WORKER>* m_pool;
};

// class vacuum_worker_task
//
//  description:
//    vacuum worker task
//
class vacuum_worker_task : public cubthread::entry_task
{
  public:
    vacuum_worker_task (const VACUUM_DATA_ENTRY & entry_ref)
      : m_data (entry_ref)
    {
    }

    void execute (cubthread::entry & thread_ref) final
    {
      // safe-guard - check interrupt is always false
      assert (!thread_ref.check_interrupt);
      vacuum_process_log_block (&thread_ref, &m_data, false);
    }

  private:
    vacuum_worker_task ();

    VACUUM_DATA_ENTRY m_data;
};

// vacuum master globals
static cubthread::daemon *vacuum_Master_daemon = NULL;                       // daemon thread
static vacuum_master_context_manager *vacuum_Master_context_manager = NULL;  // context manager

// vacuum worker globals
static cubthread::entry_workpool *vacuum_Worker_threads = NULL;              // thread pool
static vacuum_worker_context_manager *vacuum_Worker_context_manager = NULL;  // context manager

/* *INDENT-ON* */

#if defined (SA_MODE)
static void
vacuum_sa_run_job (THREAD_ENTRY * thread_p, const VACUUM_DATA_ENTRY & data_entry, bool is_partial,
		   PERF_UTIME_TRACKER & perf_tracker)
{
  PERF_UTIME_TRACKER_TIME (thread_p, &perf_tracker, PSTAT_VAC_MASTER);

  VACUUM_WORKER *worker_p = vacuum_Worker_context_manager->claim_worker ();
  thread_type save_type = thread_type::TT_NONE;
  vacuum_convert_thread_to_worker (thread_p, worker_p, save_type);
  assert (save_type == thread_type::TT_VACUUM_MASTER);

  VACUUM_DATA_ENTRY copy_data_entry = data_entry;
  vacuum_process_log_block (thread_p, &copy_data_entry, is_partial);

  vacuum_convert_thread_to_master (thread_p, save_type);
  assert (save_type == thread_type::TT_VACUUM_WORKER);
  vacuum_Worker_context_manager->retire_worker (*worker_p);

  PERF_UTIME_TRACKER_START (thread_p, &perf_tracker);
}
#endif // SA_MODE

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
#if defined(SERVER_MODE)
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_VACUUM_CS_NOT_AVAILABLE, 0);
  return ER_VACUUM_CS_NOT_AVAILABLE;
#else	/* !SERVER_MODE */		   /* SA_MODE */
  thread_type save_type = thread_type::TT_NONE;

  if (prm_get_bool_value (PRM_ID_DISABLE_VACUUM) || vacuum_Data.is_vacuum_complete)
    {
      return NO_ERROR;
    }

  /* Assign worker and allocate required resources. */
  vacuum_convert_thread_to_master (thread_p, save_type);

  /* Process vacuum data and run vacuum. */
  vacuum_job_cursor cursor;
  PERF_UTIME_TRACKER perf_tracker;

  bool dummy_continue_check_interrupt;

  int error_code = NO_ERROR;

  er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_STAND_ALONE_VACUUM_START, 0);
  er_log_debug (ARG_FILE_LINE, "Stand-alone vacuum start.\n");

  PERF_UTIME_TRACKER_START (thread_p, &perf_tracker);

  vacuum_data_load_first_and_last_page (thread_p);

  cursor.set_on_vacuum_data_start ();
  cursor.load ();
  vacuum_er_log (VACUUM_ER_LOG_MASTER, "Start searching jobs at " vacuum_job_cursor_print_format,
		 vacuum_job_cursor_print_args (cursor));

  // must start with empty vacuum_Block_data_buffer
  if (!vacuum_Block_data_buffer->is_empty ())
    {
      // start by updating vacuum data
      cursor.force_data_update ();
    }
  assert (vacuum_Block_data_buffer->is_empty ());

  // consume all vacuum data blocks
  while (cursor.is_valid ())
    {
      if (logtb_is_interrupted (thread_p, true, &dummy_continue_check_interrupt))
	{
	  cursor.unload ();
	  vacuum_Data.update ();
	  return NO_ERROR;
	}

      if (cursor.get_current_entry ().is_available ())
	{
	  cursor.start_job_on_current_entry ();
	  // job will be executed immediately
	  vacuum_sa_run_job (thread_p, cursor.get_current_entry (), false, perf_tracker);
	}
      else
	{
	  // skip
	  assert (cursor.get_current_entry ().is_vacuumed ());
	  vacuum_er_log (VACUUM_ER_LOG_JOBS,
			 "Job for blockid = %lld %s. Skip.",
			 (long long int) cursor.get_current_entry ().get_blockid (),
			 cursor.get_current_entry ().is_vacuumed ()? "was executed" : "is in progress");
	}
      cursor.increment_blockid ();

      if (!vacuum_Block_data_buffer->is_empty ()	// there is a new block
	  || vacuum_Finished_job_queue->is_full ()	// finished queue is full and must be consumed
	  || !cursor.is_valid ()	// cursor is at the end; we might still get another block by vacuum data update
	)
	{
	  // force an update; cursor must not be loaded
	  cursor.force_data_update ();
	}
    }

  assert (!cursor.is_loaded ());
  assert (vacuum_Data.is_empty ());
  assert (vacuum_Block_data_buffer->is_empty ());

#if !defined (NDEBUG)
  vacuum_verify_vacuum_data_page_fix_count (thread_p);
#endif /* !NDEBUG */

  /* Complete vacuum for SA_MODE. This means also vacuuming based on last block being logged. */
  if (log_Gl.hdr.does_block_need_vacuum)
    {
      // *INDENT-OFF*
      vacuum_data_entry partial_entry { log_Gl.hdr };
      // *INDENT-ON*
      log_Gl.hdr.does_block_need_vacuum = false;

      // can't be interrupted
      bool save_check_interrupt = logtb_set_check_interrupt (thread_p, false);
      vacuum_sa_run_job (thread_p, partial_entry, true, perf_tracker);
      (void) logtb_set_check_interrupt (thread_p, save_check_interrupt);
    }

  /* All vacuum complete. */
  vacuum_Data.oldest_unvacuumed_mvccid = log_Gl.hdr.mvcc_next_id;

  log_append_redo_data2 (thread_p, RVVAC_COMPLETE, NULL, (PAGE_PTR) vacuum_Data.first_page, 0,
			 sizeof (log_Gl.hdr.mvcc_next_id), &log_Gl.hdr.mvcc_next_id);
  vacuum_set_dirty_data_page (thread_p, vacuum_Data.first_page, DONT_FREE);
  logpb_force_flush_pages (thread_p);

  /* Cleanup dropped files. */
  vacuum_cleanup_dropped_files (thread_p);

  /* Reset log header information saved for vacuum. */
  logpb_vacuum_reset_log_header_cache (thread_p, &log_Gl.hdr);

  er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_STAND_ALONE_VACUUM_END, 0);
  er_log_debug (ARG_FILE_LINE, "Stand-alone vacuum end.\n");

  /* Vacuum structures no longer needed. */
  vacuum_finalize (thread_p);

  vacuum_Data.is_vacuum_complete = true;

  PERF_UTIME_TRACKER_TIME (thread_p, &perf_tracker, PSTAT_VAC_MASTER);

  vacuum_restore_thread (thread_p, save_type);

  return NO_ERROR;
#endif /* SA_MODE */
}

/*
 * xvacuum_dump - Dump the contents of vacuum
 *
 * return: nothing
 *
 *   outfp(in): FILE stream where to dump the vacuum. If NULL is given,
 *            it is dumped to stdout.
 */
void
xvacuum_dump (THREAD_ENTRY * thread_p, FILE * outfp)
{
  LOG_PAGEID min_log_pageid = NULL_PAGEID;
  int archive_number;

  assert (outfp != NULL);

  if (!vacuum_Is_booted)
    {
      fprintf (outfp, "vacuum did not boot properly.\n");
      return;
    }

  min_log_pageid = vacuum_min_log_pageid_to_keep (thread_p);
  if (min_log_pageid == NULL_PAGEID)
    {
      /* this is an assertion case but ignore. */
      fprintf (outfp, "vacuum did not boot properly.\n");
      return;
    }

  fprintf (outfp, "\n");
  fprintf (outfp, "*** Vacuum Dump ***\n");
  fprintf (outfp, "First log page ID referenced = %lld ", min_log_pageid);

  if (logpb_is_page_in_archive (min_log_pageid))
    {
      LOG_CS_ENTER_READ_MODE (thread_p);
      archive_number = logpb_get_archive_number (thread_p, min_log_pageid);
      if (archive_number < 0)
	{
	  /* this is an assertion case but ignore. */
	  fprintf (outfp, "\n");
	}
      else
	{
	  fprintf (outfp, "(in %s%s%03d)\n", log_Prefix, FILEIO_SUFFIX_LOGARCHIVE, archive_number);
	}
      LOG_CS_EXIT (thread_p);
    }
  else
    {
      fprintf (outfp, "(in %s)\n", fileio_get_base_file_name (log_Name_active));
    }
#if defined (SERVER_MODE)
  g_ovfp_threshold_mgr.dump (thread_p, outfp);
#endif
}

/*
 * vacuum_initialize () - Initialize necessary structures for vacuum.
 *
 * return			: Void.
 * thread_p (in)	        : Thread entry.
 * vacuum_log_block_npages (in) : Number of log pages in a block.
 * vacuum_data_vfid (in)	: Vacuum data VFID.
 * dropped_files_vfid (in)	: Dropped files VFID.
 */
int
vacuum_initialize (THREAD_ENTRY * thread_p, int vacuum_log_block_npages, VFID * vacuum_data_vfid,
		   VFID * dropped_files_vfid, bool is_restore)
{
  int error_code = NO_ERROR;
  int i;

  if (prm_get_bool_value (PRM_ID_DISABLE_VACUUM))
    {
      return NO_ERROR;
    }

  /* Initialize vacuum data */
  vacuum_Data.is_restoredb_session = is_restore;
  /* Save vacuum data VFID. */
  VFID_COPY (&vacuum_Data.vacuum_data_file, vacuum_data_vfid);
  /* Save vacuum log block size in pages. */
  vacuum_Data.log_block_npages = vacuum_log_block_npages;
  /* Compute the capacity of one vacuum data page. */
  vacuum_Data.page_data_max_count = (DB_PAGESIZE - VACUUM_DATA_PAGE_HEADER_SIZE) / sizeof (VACUUM_DATA_ENTRY);

#if defined (SA_MODE)
  vacuum_Data.is_vacuum_complete = false;
#endif

  /* Initialize vacuum dropped files */
  vacuum_Dropped_files_loaded = false;
  VFID_COPY (&vacuum_Dropped_files_vfid, dropped_files_vfid);

  /* Save first page vpid. */
  if (vacuum_get_first_page_dropped_files (thread_p, &vacuum_Dropped_files_vpid) != NO_ERROR)
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

  /* Initialize the log block data buffer */
  /* *INDENT-OFF* */
  vacuum_Block_data_buffer = new lockfree::circular_queue<vacuum_data_entry> (VACUUM_BLOCK_DATA_BUFFER_CAPACITY);
  /* *INDENT-ON* */
  if (vacuum_Block_data_buffer == NULL)
    {
      goto error;
    }

  /* Initialize finished job queue. */
  /* *INDENT-OFF* */
  vacuum_Finished_job_queue = new lockfree::circular_queue<VACUUM_LOG_BLOCKID> (VACUUM_FINISHED_JOB_QUEUE_CAPACITY);
  /* *INDENT-ON* */
  if (vacuum_Finished_job_queue == NULL)
    {
      goto error;
    }

  /* Initialize master worker. */
  vacuum_Master.drop_files_version = 0;
  vacuum_Master.state = VACUUM_WORKER_STATE_EXECUTE;	/* Master is always in execution state. */
  vacuum_Master.log_zip_p = NULL;
  vacuum_Master.undo_data_buffer = NULL;
  vacuum_Master.undo_data_buffer_capacity = 0;
  vacuum_Master.private_lru_index = -1;
  vacuum_Master.heap_objects = NULL;
  vacuum_Master.heap_objects_capacity = 0;
  vacuum_Master.prefetch_log_buffer = NULL;
  vacuum_Master.prefetch_first_pageid = NULL_PAGEID;
  vacuum_Master.prefetch_last_pageid = NULL_PAGEID;
  vacuum_Master.allocated_resources = false;
  vacuum_Master.idx = -1;
#if defined (SERVER_MODE)
  g_ovfp_threshold_mgr.init ();
#endif

  /* Initialize workers */
  for (i = 0; i < VACUUM_MAX_WORKER_COUNT; i++)
    {
      vacuum_Workers[i].drop_files_version = 0;
      vacuum_Workers[i].state = VACUUM_WORKER_STATE_INACTIVE;
      vacuum_Workers[i].log_zip_p = NULL;
      vacuum_Workers[i].undo_data_buffer = NULL;
      vacuum_Workers[i].undo_data_buffer_capacity = 0;
      vacuum_Workers[i].private_lru_index = pgbuf_assign_private_lru (thread_p);
      vacuum_Workers[i].heap_objects = NULL;
      vacuum_Workers[i].heap_objects_capacity = 0;
      vacuum_Workers[i].prefetch_log_buffer = NULL;
      vacuum_Workers[i].prefetch_first_pageid = NULL_PAGEID;
      vacuum_Workers[i].prefetch_last_pageid = NULL_PAGEID;
      vacuum_Workers[i].allocated_resources = false;
      vacuum_Workers[i].idx = i;
    }

  return NO_ERROR;

error:
  vacuum_finalize (thread_p);
  return (error_code == NO_ERROR) ? ER_FAILED : error_code;
}

int
vacuum_boot (THREAD_ENTRY * thread_p)
{
  int error_code = NO_ERROR;

  assert (!vacuum_Is_booted);	// only boot once

  if (prm_get_bool_value (PRM_ID_DISABLE_VACUUM))
    {
      /* for debug only */
      (void) log_Gl.mvcc_table.update_global_oldest_visible ();

      return NO_ERROR;
    }

  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  /* first things first... load vacuum data and do some recovery if required */
  error_code = vacuum_data_load_and_recover (thread_p);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }

  /* load dropped files from disk */
  error_code = vacuum_load_dropped_files_from_disk (thread_p);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }

  // create context managers
  vacuum_Master_context_manager = new vacuum_master_context_manager ();
  vacuum_Worker_context_manager = new vacuum_worker_context_manager ();

#if defined (SERVER_MODE)

  // get thread manager
  cubthread::manager * thread_manager = cubthread::get_manager ();

  // get logging flag for vacuum worker pool
  /* *INDENT-OFF* */
  bool log_vacuum_worker_pool =
    cubthread::is_logging_configured (cubthread::LOG_WORKER_POOL_VACUUM)
    || flag<int>::is_flag_set (prm_get_integer_value (PRM_ID_ER_LOG_VACUUM), VACUUM_ER_LOG_WORKER);

  // create thread pool
  vacuum_Worker_threads =
    thread_manager->create_worker_pool (prm_get_integer_value (PRM_ID_VACUUM_WORKER_COUNT),
					VACUUM_MAX_TASKS_IN_WORKER_POOL, "vacuum workers",
					vacuum_Worker_context_manager, 1, log_vacuum_worker_pool);
  assert (vacuum_Worker_threads != NULL);

  int vacuum_master_wakeup_interval_msec = prm_get_integer_value (PRM_ID_VACUUM_MASTER_WAKEUP_INTERVAL);
  cubthread::looper looper = cubthread::looper (std::chrono::milliseconds (vacuum_master_wakeup_interval_msec));

  // create vacuum master thread
  vacuum_Master_daemon =
    thread_manager->create_daemon (looper, new vacuum_master_task (), "vacuum_master", vacuum_Master_context_manager);

  /* *INDENT-ON* */
#endif /* SERVER_MODE */

  vacuum_Is_booted = true;

  return NO_ERROR;
}

void
vacuum_stop_workers (THREAD_ENTRY * thread_p)
{
  if (!vacuum_Is_booted)
    {
      // not booted
      return;
    }

  // notify master to stop generating new jobs
  vacuum_notify_server_shutdown ();

  // stop work pool
  if (vacuum_Worker_threads != NULL)
    {
#if defined (SERVER_MODE)
      vacuum_Worker_threads->er_log_stats ();
      vacuum_Worker_threads->stop_execution ();
#endif // SERVER_MODE

      cubthread::get_manager ()->destroy_worker_pool (vacuum_Worker_threads);
    }

  delete vacuum_Worker_context_manager;
  vacuum_Worker_context_manager = NULL;
}

void
vacuum_stop_master (THREAD_ENTRY * thread_p)
{
  if (!vacuum_Is_booted)
    {
      // not booted
      return;
    }

  // stop master daemon
  if (vacuum_Master_daemon != NULL)
    {
      cubthread::get_manager ()->destroy_daemon (vacuum_Master_daemon);
    }
  delete vacuum_Master_context_manager;
  vacuum_Master_context_manager = NULL;

  vacuum_Is_booted = false;
}

/*
* vacuum_finalize () - Finalize structures used for vacuum.
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

  assert (!vacuum_is_work_in_progress (thread_p));

  /* Make sure all finished job queues are consumed. */
  if (vacuum_Finished_job_queue != NULL)
    {
      vacuum_data_mark_finished (thread_p);
      if (!vacuum_Finished_job_queue->is_empty ())
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  assert (0);
	}
      delete vacuum_Finished_job_queue;
      vacuum_Finished_job_queue = NULL;
    }

  if (vacuum_Block_data_buffer != NULL)
    {
      while (!vacuum_Block_data_buffer->is_empty ())
	{
	  // consume log block buffer; we need to do this in a loop because vacuum_consume_buffer_log_blocks adds new
	  // log entries and may generate new log blocks

	  if (!vacuum_Data.is_loaded)
	    {
	      // safe-guard check: cannot consume if data is not loaded. should never happen
	      assert (false);
	      break;
	    }
	  if (vacuum_consume_buffer_log_blocks (thread_p) != NO_ERROR)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	      assert (0);
	    }
	}
      delete vacuum_Block_data_buffer;
      vacuum_Block_data_buffer = NULL;
    }

#if !defined(SERVER_MODE)	/* SA_MODE */
  vacuum_data_empty_update_last_blockid (thread_p);
#endif

  /* Finalize vacuum data. */
  vacuum_data_unload_first_and_last_page (thread_p);
  /* We should have unfixed all pages. Double-check. */
  pgbuf_unfix_all (thread_p);

  /* Free all resources allocated for vacuum workers */
  for (i = 0; i < VACUUM_MAX_WORKER_COUNT; i++)
    {
      vacuum_finalize_worker (thread_p, &vacuum_Workers[i]);
    }
  vacuum_finalize_worker (thread_p, &vacuum_Master);

  /* Unlock data */
  pthread_mutex_destroy (&vacuum_Dropped_files_mutex);
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
vacuum_heap (THREAD_ENTRY * thread_p, VACUUM_WORKER * worker, MVCCID threshold_mvccid, bool was_interrupted)
{
  VACUUM_HEAP_OBJECT *page_ptr;
  VACUUM_HEAP_OBJECT *obj_ptr;
  int error_code = NO_ERROR;
  VFID vfid = VFID_INITIALIZER;
  HFID hfid = HFID_INITIALIZER;
  bool reusable = false;
  int object_count = 0;

  if (worker->n_heap_objects == 0)
    {
      return NO_ERROR;
    }

  /* Set state to execute mode. */
  worker->state = VACUUM_WORKER_STATE_EXECUTE;

  /* Sort all objects. Sort function will order all objects first by VFID then by OID. All objects belonging to one
   * file will be consecutive. Also, all objects belonging to one page will be consecutive. Vacuum will be called for
   * each different heap page. */
  qsort (worker->heap_objects, worker->n_heap_objects, sizeof (VACUUM_HEAP_OBJECT), vacuum_compare_heap_object);

  /* Start parsing array. Vacuum objects page by page. */
  for (page_ptr = worker->heap_objects; page_ptr < worker->heap_objects + worker->n_heap_objects;)
    {
      if (!VFID_EQ (&vfid, &page_ptr->vfid))
	{
	  VFID_COPY (&vfid, &page_ptr->vfid);
	  /* Reset HFID */
	  HFID_SET_NULL (&hfid);
	}

      /* Find all objects for this page. */
      object_count = 1;
      for (obj_ptr = page_ptr + 1;
	   obj_ptr < worker->heap_objects + worker->n_heap_objects && obj_ptr->oid.pageid == page_ptr->oid.pageid
	   && obj_ptr->oid.volid == page_ptr->oid.volid; obj_ptr++)
	{
	  object_count++;
	}
      /* Vacuum page. */
      error_code =
	vacuum_heap_page (thread_p, page_ptr, object_count, threshold_mvccid, &hfid, &reusable, was_interrupted);
      if (error_code != NO_ERROR)
	{
	  vacuum_check_shutdown_interruption (thread_p, error_code);

	  vacuum_er_log_error (VACUUM_ER_LOG_HEAP, "Vacuum heap page %d|%d, error_code=%d.",
			       page_ptr->oid.volid, page_ptr->oid.pageid);

#if defined (NDEBUG)
	  if (!thread_p->shutdown)
	    {
	      // unexpected case
	      // debug crashes; but can release do about it? just try to clean as much as possible
	      er_clear ();
	      error_code = NO_ERROR;
	      continue;
	    }
#endif // not DEBUG

	  return error_code;
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
 * hfid (in/out)         : Heap file identifier
 * reusable (in/out)	 : True if object slots are reusable.
 * was_interrutped (in)  : True if same job was executed and interrupted.
 */
int
vacuum_heap_page (THREAD_ENTRY * thread_p, VACUUM_HEAP_OBJECT * heap_objects, int n_heap_objects,
		  MVCCID threshold_mvccid, HFID * hfid, bool * reusable, bool was_interrupted)
{
  VACUUM_HEAP_HELPER helper;	/* Vacuum heap helper. */
  HEAP_PAGE_VACUUM_STATUS page_vacuum_status;	/* Current page vacuum status. */
  int error_code = NO_ERROR;	/* Error code. */
  int obj_index = 0;		/* Index used to iterate the object array. */

  /* Assert expected arguments. */
  assert (heap_objects != NULL);
  assert (n_heap_objects > 0);
  assert (MVCCID_IS_NORMAL (threshold_mvccid));

  VACUUM_PERF_HEAP_START (thread_p, &helper);

  /* Get page from first object. */
  VPID_GET_FROM_OID (&helper.home_vpid, &heap_objects->oid);

#if !defined (NDEBUG)
  /* Check all objects belong to same page. */
  {
    int i = 0;

    assert (HEAP_ISVALID_OID (thread_p, &heap_objects->oid) != DISK_INVALID);
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
  helper.home_page = NULL;
  helper.forward_page = NULL;
  helper.n_vacuumed = 0;
  helper.n_bulk_vacuumed = 0;
  helper.initial_home_free_space = -1;
  VFID_SET_NULL (&helper.overflow_vfid);

  /* Fix heap page. */
  if (was_interrupted)
    {
      PAGE_TYPE ptype;
      error_code =
	pgbuf_fix_if_not_deallocated (thread_p, &helper.home_vpid, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH,
				      &helper.home_page);
      if (error_code != NO_ERROR)
	{
	  vacuum_check_shutdown_interruption (thread_p, error_code);
	  vacuum_er_log_error (VACUUM_ER_LOG_HEAP, "Failed to fix page %d|%d.",
			       helper.home_vpid.volid, helper.home_vpid.pageid);
	  return error_code;
	}
      if (helper.home_page == NULL)
	{
	  /* deallocated */
	  /* Safe guard: this was possible if there was only one object to be vacuumed. */
	  assert (n_heap_objects == 1);

	  vacuum_er_log_warning (VACUUM_ER_LOG_HEAP, "Heap page %d|%d was deallocated during previous run",
				 VPID_AS_ARGS (&helper.home_vpid));
	  return NO_ERROR;
	}
      ptype = pgbuf_get_page_ptype (thread_p, helper.home_page);
      if (ptype != PAGE_HEAP)
	{
	  /* page was deallocated and reused as file table. */
	  assert (ptype == PAGE_FTAB);
	  /* Safe guard: this was possible if there was only one object to be vacuumed. */
	  assert (n_heap_objects == 1);

	  vacuum_er_log_warning (VACUUM_ER_LOG_HEAP,
				 "Heap page %d|%d was deallocated during previous run and reused as file table page",
				 VPID_AS_ARGS (&helper.home_vpid));

	  pgbuf_unfix_and_init (thread_p, helper.home_page);
	  return NO_ERROR;
	}
    }
  else
    {
      helper.home_page =
	pgbuf_fix (thread_p, &helper.home_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
      if (helper.home_page == NULL)
	{
	  ASSERT_ERROR_AND_SET (error_code);
	  vacuum_check_shutdown_interruption (thread_p, error_code);
	  vacuum_er_log_error (VACUUM_ER_LOG_HEAP, "Failed to fix page %d|%d.",
			       helper.home_vpid.volid, helper.home_vpid.pageid);
	  return error_code;
	}
    }

#if !defined (NDEBUG)
  (void) pgbuf_check_page_ptype (thread_p, helper.home_page, PAGE_HEAP);
#endif /* !NDEBUG */

  helper.initial_home_free_space = spage_get_free_space_without_saving (thread_p, helper.home_page, NULL);

  if (HFID_IS_NULL (hfid))
    {
      /* file has changed and we must get HFID and file type */
      error_code = vacuum_heap_get_hfid_and_file_type (thread_p, &helper, &heap_objects[0].vfid);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  vacuum_check_shutdown_interruption (thread_p, error_code);
	  vacuum_er_log_error (VACUUM_ER_LOG_HEAP, "%s", "Failed to get hfid.");
	  return error_code;
	}
      /* we need to also output to avoid checking again for other objects */
      *reusable = helper.reusable;
      *hfid = helper.hfid;
    }
  else
    {
      helper.reusable = *reusable;
      helper.hfid = *hfid;
    }

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

      /* Prepare record for vacuum (get all required pages, info and MVCC header). */
      error_code = vacuum_heap_prepare_record (thread_p, &helper);
      if (error_code != NO_ERROR)
	{
	  vacuum_er_log_error (VACUUM_ER_LOG_HEAP,
			       "Could not prepare vacuum for object %d|%d|%d.",
			       heap_objects[obj_index].oid.volid, heap_objects[obj_index].oid.pageid,
			       heap_objects[obj_index].oid.slotid);

	  vacuum_check_shutdown_interruption (thread_p, error_code);
	  if (helper.forward_page != NULL)
	    {
	      pgbuf_unfix_and_init (thread_p, helper.forward_page);
	    }

	  /* release build will give up */
	  goto end;
	}
      /* Safe guard. */
      assert (helper.home_page != NULL);

      switch (helper.record_type)
	{
	case REC_RELOCATION:
	case REC_HOME:
	case REC_BIGONE:

	  /* Check if record can be vacuumed. */
	  helper.can_vacuum = mvcc_satisfies_vacuum (thread_p, &helper.mvcc_header, threshold_mvccid);
	  if (helper.can_vacuum == VACUUM_RECORD_REMOVE)
	    {
	      /* Record has been deleted and it can be removed. */
	      error_code = vacuum_heap_record (thread_p, &helper);
	    }
	  else if (helper.can_vacuum == VACUUM_RECORD_DELETE_INSID_PREV_VER)
	    {
	      /* Record insert MVCCID and prev version lsa can be removed. */
	      error_code = vacuum_heap_record_insid_and_prev_version (thread_p, &helper);
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
	      vacuum_er_log_error (VACUUM_ER_LOG_HEAP,
				   "Failed to vacuum object at %d|%d|%d.", helper.home_vpid.volid,
				   helper.home_vpid.pageid, helper.crt_slotid);

	      /* Debug should hit assert. Release should continue. */
	      assert_release (false);

	      if (helper.home_page == NULL)
		{
		  goto end;
		}
	      else
		{
		  continue;
		}
	    }
	  break;

	default:
	  /* Object cannot be vacuumed. Most likely it was already vacuumed by another worker or it was rollbacked and
	   * reused. */
	  assert (helper.forward_page == NULL);
	  break;
	}

      assert (!VACUUM_IS_THREAD_VACUUM_MASTER (thread_p));
      if (!VACUUM_IS_THREAD_VACUUM_WORKER (thread_p))
	{
	  continue;
	}

      /* Check page vacuum status. */
      page_vacuum_status = heap_page_get_vacuum_status (thread_p, helper.home_page);
      /* Safe guard. */
      assert (page_vacuum_status != HEAP_PAGE_VACUUM_NONE || (was_interrupted && helper.n_vacuumed == 0));

      /* Page can be removed if no other worker will access this page. If this worker is the only one expected, then it
       * can remove the page. It is also possible that this job was previously executed and interrupted due to
       * shutdown or crash. This case is a little more complicated. There are two scenarios: 1. Current page status is
       * vacuum none. This means all vacuum was already executed. 2. Current page status is vacuum once. This means a
       * vacuum is expected, but we cannot tell if current vacuum worker was interrupted and re-executes an old vacuum
       * task or if it is executing the task expected by page status.  Take next scenario: 1. Insert new object at
       * OID1. page status is vacuum once.  2. Block with above operations is finished and vacuum job is started.  3.
       * Vacuum insert MVCCID at OID1. status is now vacuum none.  4. Delete object at OID1. page status is set to
       * vacuum once.  5. Crash.  6. Job on block at step #2 is restarted.  7. Vacuum is executed on object OID1.
       * Object can be removed.  8. Vacuum is executed for delete operation at #4.  It would be incorrect to change
       * page status from vacuum once to none, since it will be followed by another vacuum task. Since vacuum none
       * status means page might be deallocated, it is better to be paranoid about it. */
      if ((page_vacuum_status == HEAP_PAGE_VACUUM_ONCE && !was_interrupted)
	  || (page_vacuum_status == HEAP_PAGE_VACUUM_NONE && was_interrupted))
	{
	  assert (n_heap_objects == 1);
	  assert (helper.n_vacuumed <= 1);
	  if (page_vacuum_status == HEAP_PAGE_VACUUM_ONCE)
	    {
	      heap_page_set_vacuum_status_none (thread_p, helper.home_page);

	      vacuum_er_log (VACUUM_ER_LOG_HEAP,
			     "Changed vacuum status of heap page %d|%d, lsa=%lld|%d from once to none.",
			     PGBUF_PAGE_STATE_ARGS (helper.home_page));

	      VACUUM_PERF_HEAP_TRACK_EXECUTE (thread_p, &helper);

	      vacuum_log_vacuum_heap_page (thread_p, helper.home_page, helper.n_bulk_vacuumed, helper.slots,
					   helper.results, helper.reusable, true);

	      VACUUM_PERF_HEAP_TRACK_LOGGING (thread_p, &helper);
	    }

	  /* Reset n_vacuumed since they have been logged already. */
	  helper.n_vacuumed = 0;
	  helper.n_bulk_vacuumed = 0;

	  /* Set page dirty. */
	  pgbuf_set_dirty (thread_p, helper.home_page, DONT_FREE);

	  if (spage_number_of_records (helper.home_page) <= 1 && helper.reusable)
	    {
	      /* Try to remove page from heap. */

	      /* HFID is required. */
	      assert (!HFID_IS_NULL (&helper.hfid));
	      VACUUM_PERF_HEAP_TRACK_PREPARE (thread_p, &helper);

	      if (pgbuf_has_prevent_dealloc (helper.home_page) == false
		  && heap_remove_page_on_vacuum (thread_p, &helper.home_page, &helper.hfid))
		{
		  /* Successfully removed page. */
		  assert (helper.home_page == NULL);

		  vacuum_er_log (VACUUM_ER_LOG_WORKER | VACUUM_ER_LOG_HEAP,
				 "Successfully removed page %d|%d from heap file (%d, %d|%d).",
				 VPID_AS_ARGS (&helper.home_vpid), HFID_AS_ARGS (&helper.hfid));

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

      if (pgbuf_has_any_non_vacuum_waiters (helper.home_page) && obj_index < n_heap_objects - 1)
	{
	  /* release latch to favor other threads */
	  vacuum_heap_page_log_and_reset (thread_p, &helper, false, true);
	  assert (helper.home_page == NULL);
	  assert (helper.forward_page == NULL);

	  helper.home_page =
	    pgbuf_fix (thread_p, &helper.home_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
	  if (helper.home_page == NULL)
	    {
	      ASSERT_ERROR_AND_SET (error_code);
	      vacuum_check_shutdown_interruption (thread_p, error_code);
	      vacuum_er_log_error (VACUUM_ER_LOG_HEAP, "Failed to fix page %d|%d.",
				   helper.home_vpid.volid, helper.home_vpid.pageid);
	      goto end;
	    }
#if !defined (NDEBUG)
	  (void) pgbuf_check_page_ptype (thread_p, helper.home_page, PAGE_HEAP);
#endif /* !NDEBUG */
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
 * vacuum_heap_prepare_record () - Prepare all required information to vacuum heap record. Possible requirements:
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
vacuum_heap_prepare_record (THREAD_ENTRY * thread_p, VACUUM_HEAP_HELPER * helper)
{
  SPAGE_SLOT *slotp;		/* Slot at helper->crt_slotid or NULL. */
  VPID forward_vpid;		/* Forward page VPID. */
  int error_code = NO_ERROR;	/* Error code. */
  PGBUF_LATCH_CONDITION fwd_condition;	/* Condition to latch forward page for REC_RELOCATION. */

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

  /* Get required pages and MVCC header in the three interesting cases: 1. REC_RELOCATION. 2. REC_BIGONE. 3. REC_HOME. */
  switch (helper->record_type)
    {
    case REC_RELOCATION:
      /* Required info: forward page, forward OID, REC_NEWHOME record, MVCC header and HFID. */
      assert (!HFID_IS_NULL (&helper->hfid));

      /* Get forward OID. */
      helper->forward_recdes.data = (char *) &helper->forward_link;
      helper->forward_recdes.area_size = sizeof (helper->forward_link);
      if (spage_get_record (thread_p, helper->home_page, helper->crt_slotid, &helper->forward_recdes, COPY) !=
	  S_SUCCESS)
	{
	  assert_release (false);
	  return ER_FAILED;
	}
      COPY_OID (&helper->forward_oid, &helper->forward_link);

      /* Get forward page. */
      VPID_GET_FROM_OID (&forward_vpid, &helper->forward_link);
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
	  /* The condition used to fix forward page depends on its VPID and home page VPID. Unconditional latch can be
	   * used if the order is home before forward. If the order is forward before home, try conditional latch, and
	   * if it fails, fix pages in reversed order. */
	  fwd_condition =
	    (PGBUF_LATCH_CONDITION) pgbuf_get_condition_for_ordered_fix (&forward_vpid, &helper->home_vpid,
									 &helper->hfid);
	  helper->forward_page = pgbuf_fix (thread_p, &forward_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, fwd_condition);
	}
      if (helper->forward_page == NULL)
	{
	  /* Fix failed. */
	  if (fwd_condition == PGBUF_UNCONDITIONAL_LATCH)
	    {
	      /* Fix should have worked. */
	      ASSERT_ERROR_AND_SET (error_code);
	      vacuum_er_log_error (VACUUM_ER_LOG_HEAP, "Failed to fix page %d|%d", VPID_AS_ARGS (&forward_vpid));
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
	    pgbuf_fix (thread_p, &forward_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
	  if (helper->forward_page == NULL)
	    {
	      ASSERT_ERROR_AND_SET (error_code);
	      vacuum_check_shutdown_interruption (thread_p, error_code);
	      vacuum_er_log_error (VACUUM_ER_LOG_HEAP, "Failed to fix page %d|%d", VPID_AS_ARGS (&forward_vpid));
	      return error_code;
	    }
	  /* Fix home page. */
	  helper->home_page =
	    pgbuf_fix (thread_p, &helper->home_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
	  if (helper->home_page == NULL)
	    {
	      ASSERT_ERROR_AND_SET (error_code);
	      vacuum_check_shutdown_interruption (thread_p, error_code);
	      vacuum_er_log_error (VACUUM_ER_LOG_HEAP, "Failed to fix page %d|d.", VPID_AS_ARGS (&forward_vpid));
	      return error_code;
	    }
	  /* Both pages fixed. */

	  /* While home has been unfixed, it is possible that current record was changed. It could be returned to home,
	   * link could be changed, or it could be vacuumed. Repeat getting record. */
	  goto retry_prepare;
	}
      assert (VPID_EQ (pgbuf_get_vpid_ptr (helper->forward_page), &forward_vpid));
      /* COPY (needed for UNDO logging) REC_NEWHOME record. */
      helper->record.data = PTR_ALIGN (helper->rec_buf, MAX_ALIGNMENT);
      helper->record.area_size = sizeof (helper->rec_buf);
      if (spage_get_record (thread_p, helper->forward_page, helper->forward_oid.slotid, &helper->record, COPY) !=
	  S_SUCCESS)
	{
	  assert_release (false);
	  return ER_FAILED;
	}

      /* Get MVCC header to check whether the record can be vacuumed. */
      error_code = or_mvcc_get_header (&helper->record, &helper->mvcc_header);
      if (error_code != NO_ERROR)
	{
	  assert_release (false);
	  return error_code;
	}
      return NO_ERROR;

    case REC_BIGONE:
      /* Required info: forward oid, forward page, MVCC header, HFID and overflow VFID. */

      if (helper->forward_page != NULL)
	{
	  /* Retry from REC_RELOCATION. This forward_page cannot be good for REC_BIGONE. */
	  pgbuf_unfix_and_init (thread_p, helper->forward_page);
	}

      assert (!HFID_IS_NULL (&helper->hfid));

      /* Overflow VFID is required to remove overflow pages. */
      if (VFID_ISNULL (&helper->overflow_vfid))
	{
	  if (heap_ovf_find_vfid (thread_p, &helper->hfid, &helper->overflow_vfid, false, PGBUF_CONDITIONAL_LATCH)
	      == NULL)
	    {
	      /* Failed conditional latch. Unfix heap page and try again using unconditional latch. */
	      VACUUM_PERF_HEAP_TRACK_PREPARE (thread_p, helper);

	      vacuum_heap_page_log_and_reset (thread_p, helper, false, true);

	      if (heap_ovf_find_vfid (thread_p, &helper->hfid, &helper->overflow_vfid, false, PGBUF_UNCONDITIONAL_LATCH)
		  == NULL || VFID_ISNULL (&helper->overflow_vfid))
		{
		  assert_release (false);
		  return ER_FAILED;
		}
	      helper->home_page =
		pgbuf_fix (thread_p, &helper->home_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
	      if (helper->home_page == NULL)
		{
		  ASSERT_ERROR_AND_SET (error_code);
		  vacuum_check_shutdown_interruption (thread_p, error_code);
		  vacuum_er_log_error (VACUUM_ER_LOG_HEAP, "Failed to fix page %d|%d.",
				       VPID_AS_ARGS (&helper->home_vpid));
		  return error_code;
		}
	      /* While home has been unfixed, it is possible that current record was changed. It could be vacuumed.
	       * Repeat getting record. */
	      goto retry_prepare;
	    }
	}
      assert (!VFID_ISNULL (&helper->overflow_vfid));
      assert (helper->home_page != NULL);

      /* Get forward OID. */
      helper->forward_recdes.data = (char *) &helper->forward_link;
      helper->forward_recdes.area_size = sizeof (helper->forward_link);
      if (spage_get_record (thread_p, helper->home_page, helper->crt_slotid, &helper->forward_recdes, COPY) !=
	  S_SUCCESS)
	{
	  assert_release (false);
	  return ER_FAILED;
	}

      COPY_OID (&helper->forward_oid, &helper->forward_link);

      /* Fix first overflow page (forward_page). */
      VPID_GET_FROM_OID (&forward_vpid, &helper->forward_link);
      helper->forward_page =
	pgbuf_fix (thread_p, &forward_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
      if (helper->forward_page == NULL)
	{
	  ASSERT_ERROR_AND_SET (error_code);
	  vacuum_check_shutdown_interruption (thread_p, error_code);
	  vacuum_er_log_error (VACUUM_ER_LOG_HEAP, "Failed to fix page %d|%d", VPID_AS_ARGS (&forward_vpid));
	  return error_code;
	}

      /* Read MVCC header from first overflow page. */
      error_code = heap_get_mvcc_rec_header_from_overflow (helper->forward_page, &helper->mvcc_header, NULL);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  vacuum_er_log_error (VACUUM_ER_LOG_HEAP,
			       "Failed to get MVCC header from overflow page %d|%d.", VPID_AS_ARGS (&forward_vpid));
	  return error_code;
	}
      break;

    case REC_HOME:
      /* Required info: record data and MVCC header. */

      if (helper->forward_page != NULL)
	{
	  /* Retry from REC_RELOCATION. This forward_page cannot be good for REC_HOME. */
	  pgbuf_unfix_and_init (thread_p, helper->forward_page);
	}

      helper->record.data = PTR_ALIGN (helper->rec_buf, MAX_ALIGNMENT);
      helper->record.area_size = sizeof (helper->rec_buf);

      /* Peek record. */
      if (spage_get_record (thread_p, helper->home_page, helper->crt_slotid, &helper->record, COPY) != S_SUCCESS)
	{
	  assert_release (false);
	  return ER_FAILED;
	}

      /* Get MVCC header to check whether the record can be vacuumed. */
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
	  /* Retry from REC_RELOCATION. This forward_page cannot be good for vacuumed/deleted slot. */
	  pgbuf_unfix_and_init (thread_p, helper->forward_page);
	}
      break;
    }

  /* Assert forward page is fixed if and only if record type is either REC_RELOCATION or REC_BIGONE. */
  assert ((helper->record_type == REC_RELOCATION
	   || helper->record_type == REC_BIGONE) == (helper->forward_page != NULL));

  VACUUM_PERF_HEAP_TRACK_PREPARE (thread_p, helper);

  /* Success. */
  return NO_ERROR;
}

/*
 * vacuum_heap_record_insid_and_prev_version () - Remove insert MVCCID and prev version lsa from record.
 *
 * return	 : Error code.
 * thread_p (in) : Thread entry.
 * helper (in)	 : Vacuum heap helper.
 */
static int
vacuum_heap_record_insid_and_prev_version (THREAD_ENTRY * thread_p, VACUUM_HEAP_HELPER * helper)
{
  RECDES *update_record;
  int error_code = NO_ERROR;
  char *start_p, *existing_data_p, *new_data_p;
  int repid_and_flag_bits = 0, mvcc_flags = 0;

  /* Assert expected arguments. */
  assert (helper != NULL);
  assert (helper->can_vacuum == VACUUM_RECORD_DELETE_INSID_PREV_VER);
  assert (MVCC_IS_HEADER_INSID_NOT_ALL_VISIBLE (&helper->mvcc_header));

  switch (helper->record_type)
    {
    case REC_RELOCATION:
      /* Remove insert MVCCID from REC_NEWHOME in forward_page. */

      /* Forward page and OID are required. */
      assert (helper->forward_page != NULL);
      assert (!OID_ISNULL (&helper->forward_oid));
      assert (helper->record.type == REC_NEWHOME);

      /* Remove insert MVCCID and prev version lsa. */
      update_record = &helper->record;
      start_p = update_record->data;
      repid_and_flag_bits = OR_GET_MVCC_REPID_AND_FLAG (start_p);
      mvcc_flags = (repid_and_flag_bits >> OR_MVCC_FLAG_SHIFT_BITS) & OR_MVCC_FLAG_MASK;

      /* Skip bytes up to insid_offset. */
      existing_data_p = start_p + mvcc_header_size_lookup[mvcc_flags];
      new_data_p = start_p + OR_MVCC_INSERT_ID_OFFSET;
      if (mvcc_flags & OR_MVCC_FLAG_VALID_DELID)
	{
	  /* Has MVCC DELID. */
	  if (mvcc_flags & OR_MVCC_FLAG_VALID_INSID)
	    {
	      /* Copy MVCC DELID over INSID (INSID is removed). */
	      memcpy (new_data_p, new_data_p + OR_MVCCID_SIZE, OR_MVCCID_SIZE);
	    }
	  /* Skip DELID. */
	  new_data_p += OR_MVCCID_SIZE;
	}

      /* Clear flag for valid insert MVCCID and prev version lsa. */
      repid_and_flag_bits &= ~((OR_MVCC_FLAG_VALID_INSID | OR_MVCC_FLAG_VALID_PREV_VERSION) << OR_MVCC_FLAG_SHIFT_BITS);
      OR_PUT_INT (start_p, repid_and_flag_bits);

      /* Expect new_data_p != existing_data_p in most of the cases. */
      assert (existing_data_p >= new_data_p);
      memmove (new_data_p, existing_data_p, update_record->length - CAST_BUFLEN (existing_data_p - start_p));
      update_record->length -= CAST_BUFLEN (existing_data_p - new_data_p);
      assert (update_record->length > 0);

      /* Update record in page. */
      if (spage_update (thread_p, helper->forward_page, helper->forward_oid.slotid, update_record) != SP_SUCCESS)
	{
	  assert_release (false);
	  return ER_FAILED;
	}

      /* Since forward page was vacuumed, log it immediately. Then unfix forward page. */
      vacuum_log_vacuum_heap_page (thread_p, helper->forward_page, 1, &helper->forward_oid.slotid, &helper->can_vacuum,
				   helper->reusable, false);
      pgbuf_set_dirty (thread_p, helper->forward_page, FREE);
      helper->forward_page = NULL;

      perfmon_inc_stat (thread_p, PSTAT_HEAP_REL_VACUUMS);
      break;

    case REC_BIGONE:
      /* First overflow page is required. */
      assert (helper->forward_page != NULL);

      /* Replace current insert MVCCID with MVCCID_ALL_VISIBLE. Header must remain the same size. */
      MVCC_SET_INSID (&helper->mvcc_header, MVCCID_ALL_VISIBLE);
      LSA_SET_NULL (&helper->mvcc_header.prev_version_lsa);
      error_code = heap_set_mvcc_rec_header_on_overflow (helper->forward_page, &helper->mvcc_header);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  vacuum_er_log_error (VACUUM_ER_LOG_HEAP,
			       "set mvcc header (flag=%d, repid=%d, chn=%d, insid=%llu, "
			       "delid=%llu, forward object %d|%d|%d with record of type=%d and size=%d",
			       (int) MVCC_GET_FLAG (&helper->mvcc_header), (int) MVCC_GET_REPID (&helper->mvcc_header),
			       MVCC_GET_CHN (&helper->mvcc_header), MVCC_GET_INSID (&helper->mvcc_header),
			       MVCC_GET_DELID (&helper->mvcc_header), helper->home_vpid.volid, helper->home_vpid.pageid,
			       helper->crt_slotid, REC_BIGONE, helper->record.length);
	  return error_code;
	}
      /* Log changes and unfix first overflow page. */
      vacuum_log_remove_ovf_insid (thread_p, helper->forward_page);
      pgbuf_set_dirty (thread_p, helper->forward_page, FREE);
      helper->forward_page = NULL;

      perfmon_inc_stat (thread_p, PSTAT_HEAP_BIG_VACUUMS);
      break;

    case REC_HOME:
      /* Remove insert MVCCID and prev version lsa. */

      assert (helper->record.type == REC_HOME);
      update_record = &helper->record;
      start_p = update_record->data;
      repid_and_flag_bits = OR_GET_MVCC_REPID_AND_FLAG (start_p);
      mvcc_flags = (repid_and_flag_bits >> OR_MVCC_FLAG_SHIFT_BITS) & OR_MVCC_FLAG_MASK;

      /* Skip bytes up to insid_offset */
      existing_data_p = start_p + mvcc_header_size_lookup[mvcc_flags];
      new_data_p = start_p + OR_MVCC_INSERT_ID_OFFSET;
      if (mvcc_flags & OR_MVCC_FLAG_VALID_DELID)
	{
	  /* Has MVCC DELID. */
	  if (mvcc_flags & OR_MVCC_FLAG_VALID_INSID)
	    {
	      /* Copy MVCC DELID over INSID (INSID is removed). */
	      memcpy (new_data_p, new_data_p + OR_MVCCID_SIZE, OR_MVCCID_SIZE);
	    }
	  /* Skip DELID. */
	  new_data_p += OR_MVCCID_SIZE;
	}

      /* Clear flag for valid insert MVCCID and prev version lsa. */
      repid_and_flag_bits &= ~((OR_MVCC_FLAG_VALID_INSID | OR_MVCC_FLAG_VALID_PREV_VERSION) << OR_MVCC_FLAG_SHIFT_BITS);
      OR_PUT_INT (start_p, repid_and_flag_bits);

      /* Expect new_data_p != existing_data_p in most of the cases. */
      assert (existing_data_p >= new_data_p);
      memmove (new_data_p, existing_data_p, update_record->length - CAST_BUFLEN (existing_data_p - start_p));
      update_record->length -= CAST_BUFLEN (existing_data_p - new_data_p);
      assert (update_record->length > 0);

      if (spage_update (thread_p, helper->home_page, helper->crt_slotid, update_record) != SP_SUCCESS)
	{
	  assert_release (false);
	  return ER_FAILED;
	}
      /* Collect vacuum data to be logged later. */
      helper->slots[helper->n_bulk_vacuumed] = helper->crt_slotid;
      helper->results[helper->n_bulk_vacuumed] = VACUUM_RECORD_DELETE_INSID_PREV_VER;
      helper->n_bulk_vacuumed++;

      perfmon_inc_stat (thread_p, PSTAT_HEAP_HOME_VACUUMS);
      break;

    default:
      /* Should not be here. */
      assert_release (false);
      return ER_FAILED;
    }

  helper->n_vacuumed++;

  perfmon_inc_stat (thread_p, PSTAT_HEAP_INSID_VACUUMS);

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

  if (helper->record_type == REC_RELOCATION || helper->record_type == REC_BIGONE)
    {
      /* HOME record of rel/big records are performed as a single operation: flush all existing vacuumed slots before
       * starting a system op for current record */
      vacuum_heap_page_log_and_reset (thread_p, helper, false, false);
      log_sysop_start (thread_p);
    }
  else
    {
      assert (helper->record_type == REC_HOME);
      /* Collect home page changes. */
      helper->slots[helper->n_bulk_vacuumed] = helper->crt_slotid;
      helper->results[helper->n_bulk_vacuumed] = VACUUM_RECORD_REMOVE;
    }

  /* Vacuum REC_HOME/REC_RELOCATION/REC_BIGONE */
  spage_vacuum_slot (thread_p, helper->home_page, helper->crt_slotid, helper->reusable);

  if (helper->reusable)
    {
      perfmon_inc_stat (thread_p, PSTAT_HEAP_REMOVE_VACUUMS);
    }

  if (helper->record_type != REC_HOME)
    {
      /* We try to keep the same amount of pgbuf_set_dirty and logged changes; Changes on REC_HOME records are logged
       * in bulk and page is set dirty along with that log record */
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

      vacuum_log_redoundo_vacuum_record (thread_p, helper->home_page, helper->crt_slotid, &helper->forward_recdes,
					 helper->reusable);

      VACUUM_PERF_HEAP_TRACK_LOGGING (thread_p, helper);

      spage_vacuum_slot (thread_p, helper->forward_page, helper->forward_oid.slotid, true);

      if (prm_get_integer_value (PRM_ID_HF_MAX_BESTSPACE_ENTRIES) > 0)
	{
	  int freespace = spage_get_free_space_without_saving (thread_p, helper->forward_page, NULL);

	  if (freespace > HEAP_DROP_FREE_SPACE)
	    {
	      /*
	       * NOTE:
	       * By checking the freespace > HEAP_DROP_FREE_SPACE condition, heap_Bestspace->bestspace_mutex contention is reduced
	       * and the unnecessarily frequent extraction from heap_Bestspace->vpid_ht due to small free space is prevented in heap_stats_find_page_in_bestspace().
	       * And Passing the prev_freespace argument to 0 is a trick to get heap_stats_add_bestspace() called from heap_stats_update().
	       *
	       * This part will be refactored right away in the related issue, at which time this comment will be removed.
	       */
	      heap_stats_update (thread_p, helper->forward_page, &helper->hfid, 0);
	    }
	}

      VACUUM_PERF_HEAP_TRACK_EXECUTE (thread_p, helper);

      /* Log changes in forward page immediately. */
      vacuum_log_redoundo_vacuum_record (thread_p, helper->forward_page, helper->forward_oid.slotid, &helper->record,
					 true);

      pgbuf_set_dirty (thread_p, helper->forward_page, FREE);
      helper->forward_page = NULL;

      log_sysop_commit (thread_p);

      VACUUM_PERF_HEAP_TRACK_LOGGING (thread_p, helper);

      perfmon_inc_stat (thread_p, PSTAT_HEAP_REL_VACUUMS);
      break;

    case REC_BIGONE:
      assert (helper->forward_page != NULL);
      /* Overflow first page is required. */
      assert (!VFID_ISNULL (&helper->overflow_vfid));

      VACUUM_PERF_HEAP_TRACK_EXECUTE (thread_p, helper);

      vacuum_log_redoundo_vacuum_record (thread_p, helper->home_page, helper->crt_slotid, &helper->forward_recdes,
					 helper->reusable);

      VACUUM_PERF_HEAP_TRACK_LOGGING (thread_p, helper);

      /* Unfix first overflow page. */
      pgbuf_unfix_and_init (thread_p, helper->forward_page);

      if (heap_ovf_delete (thread_p, &helper->hfid, &helper->forward_oid, &helper->overflow_vfid) == NULL)
	{
	  /* Failed to delete. */
	  assert_release (false);
	  log_sysop_abort (thread_p);
	  return ER_FAILED;
	}

      VACUUM_PERF_HEAP_TRACK_EXECUTE (thread_p, helper);

      log_sysop_commit (thread_p);

      VACUUM_PERF_HEAP_TRACK_LOGGING (thread_p, helper);

      perfmon_inc_stat (thread_p, PSTAT_HEAP_BIG_VACUUMS);
      break;

    case REC_HOME:
      helper->n_bulk_vacuumed++;

      perfmon_inc_stat (thread_p, PSTAT_HEAP_HOME_VACUUMS);
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
 * vfid (in)     : file identifier
 */
static int
vacuum_heap_get_hfid_and_file_type (THREAD_ENTRY * thread_p, VACUUM_HEAP_HELPER * helper, const VFID * vfid)
{
  int error_code = NO_ERROR;	/* Error code. */
  OID class_oid = OID_INITIALIZER;	/* Class OID. */
  FILE_DESCRIPTORS file_descriptor;
  FILE_TYPE ftype;

  assert (helper != NULL);
  assert (helper->home_page != NULL);
  assert (vfid != NULL && !VFID_ISNULL (vfid));

  /* Get class OID from heap page. */
  error_code = heap_get_class_oid_from_page (thread_p, helper->home_page, &class_oid);
  if (error_code != NO_ERROR)
    {
      vacuum_er_log_error (VACUUM_ER_LOG_HEAP,
			   "Failed to obtain class_oid from heap page %d|%d.",
			   PGBUF_PAGE_VPID_AS_ARGS (helper->home_page));

      assert_release (false);
      return error_code;
    }
  assert (!OID_ISNULL (&class_oid));

  /* Get HFID for class OID. */
  error_code = file_descriptor_get (thread_p, vfid, &file_descriptor);
  if (error_code != NO_ERROR)
    {
      assert_release (false);
    }
  else
    {
      helper->hfid = file_descriptor.heap.hfid;
      error_code = file_get_type (thread_p, vfid, &ftype);
      if (error_code != NO_ERROR)
	{
	  assert_release (false);
	}
    }
  if (error_code != NO_ERROR)
    {
      vacuum_er_log_error (VACUUM_ER_LOG_HEAP,
			   "Failed to obtain heap file identifier for class %d|%d|%d)", OID_AS_ARGS (&class_oid));

      assert_release (false);
      return error_code;
    }
  if (HFID_IS_NULL (&helper->hfid) || (ftype != FILE_HEAP && ftype != FILE_HEAP_REUSE_SLOTS))
    {
      vacuum_er_log_error (VACUUM_ER_LOG_HEAP,
			   "Invalid hfid (%d, %d|%d) or ftype = %s ", HFID_AS_ARGS (&helper->hfid),
			   file_type_to_string (ftype));
      assert_release (false);
      return ER_FAILED;
    }

  /* reusable */
  helper->reusable = ftype == FILE_HEAP_REUSE_SLOTS;

  /* Success. */
  return NO_ERROR;
}

/*
 * vacuum_heap_page_log_and_reset () - Logs the vacuumed slots from page and reset page pointer and number of
 *				       vacuumed slots.
 *
 * return	 : Void.
 * thread_p (in) : Thread entry.
 * helper (in)	 : Vacuum heap helper.
 * update_best_space_stat (in)	 :
 * unlatch_page (in) :
 */
static void
vacuum_heap_page_log_and_reset (THREAD_ENTRY * thread_p, VACUUM_HEAP_HELPER * helper, bool update_best_space_stat,
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
      spage_compact (thread_p, helper->home_page);
    }

  /* Update statistics only for home pages; We assume that fwd pages (from relocated records) are home pages for other
   * OIDs and their statistics are updated in that context */
  if (update_best_space_stat == true && helper->initial_home_free_space != -1)
    {
      assert (!HFID_IS_NULL (&helper->hfid));
      heap_stats_update (thread_p, helper->home_page, &helper->hfid, helper->initial_home_free_space);
    }

  VACUUM_PERF_HEAP_TRACK_EXECUTE (thread_p, helper);

  /* Log vacuumed slots */
  vacuum_log_vacuum_heap_page (thread_p, helper->home_page, helper->n_bulk_vacuumed, helper->slots, helper->results,
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
 * reusable (in)      :
 *
 * NOTE: Some values in slots array are modified and set to negative values.
 */
static void
vacuum_log_vacuum_heap_page (THREAD_ENTRY * thread_p, PAGE_PTR page_p, int n_slots, PGSLOTID * slots,
			     MVCC_SATISFIES_VACUUM_RESULT * results, bool reusable, bool all_vacuumed)
{
  LOG_DATA_ADDR addr;
  int packed_size = 0, i = 0;
  char *ptr = NULL, *buffer_p = NULL;
  char buffer[MAX_SLOTS_IN_PAGE * (sizeof (PGSLOTID) + 2 * OR_OID_SIZE) + (MAX_ALIGNMENT * 2)];

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
	  assert (results[i] == VACUUM_RECORD_DELETE_INSID_PREV_VER || results[i] == VACUUM_RECORD_REMOVE);

	  assert (slots[i] > 0);

	  if (results[i] == VACUUM_RECORD_REMOVE)
	    {
	      /* Use negative slot ID to mark that object has been completely removed. */
	      slots[i] = -slots[i];
	    }
	}
      memcpy (ptr, slots, n_slots * sizeof (PGSLOTID));
      ptr += n_slots * sizeof (PGSLOTID);
    }

  assert ((ptr - buffer_p) == packed_size);

  /* Append new redo log rebuild_record */
  log_append_redo_data (thread_p, RVVAC_HEAP_PAGE_VACUUM, &addr, packed_size, buffer_p);
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
  int i = 0;
  INT16 n_slots;
  PGSLOTID *slotids = NULL;
  PAGE_PTR page_p = NULL;
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
		     "Change vacuum status for heap page %d|%d, lsa=%lld|%d, from once to none.",
		     PGBUF_PAGE_STATE_ARGS (rcv->pgptr));
    }

  if (n_slots == 0)
    {
      /* No slots have been vacuumed, but header must be changed from one vacuum required to no vacuum required. */
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
	  spage_vacuum_slot (thread_p, page_p, slotids[i], reusable);
	}
      else
	{
	  /* Only insert MVCCID has been removed */
	  if (spage_get_record (thread_p, rcv->pgptr, slotids[i], &peek_record, PEEK) != S_SUCCESS)
	    {
	      vacuum_er_log_error (VACUUM_ER_LOG_HEAP | VACUUM_ER_LOG_RECOVERY, "Failed to get record at %d|%d|%d",
				   PGBUF_PAGE_VPID_AS_ARGS (rcv->pgptr), slotids[i]);
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
	  old_header_size = mvcc_header_size_lookup[MVCC_GET_FLAG (&rec_header)];
	  /* Clear insert MVCCID. */
	  MVCC_CLEAR_FLAG_BITS (&rec_header, OR_MVCC_FLAG_VALID_INSID);
	  /* Clear previous version. */
	  MVCC_CLEAR_FLAG_BITS (&rec_header, OR_MVCC_FLAG_VALID_PREV_VERSION);
	  new_header_size = mvcc_header_size_lookup[MVCC_GET_FLAG (&rec_header)];

	  /* Rebuild record */
	  rebuild_record.type = peek_record.type;
	  rebuild_record.length = peek_record.length;
	  memcpy (rebuild_record.data, peek_record.data, peek_record.length);

	  /* Set new header */
	  or_mvcc_set_header (&rebuild_record, &rec_header);
	  /* Copy record data */
	  memcpy (rebuild_record.data + new_header_size, peek_record.data + old_header_size,
		  peek_record.length - old_header_size);

	  if (spage_update (thread_p, rcv->pgptr, slotids[i], &rebuild_record) != SP_SUCCESS)
	    {
	      assert_release (false);
	      return ER_FAILED;
	    }
	}
    }

  if (spage_need_compact (thread_p, rcv->pgptr) == true)
    {
      (void) spage_compact (thread_p, rcv->pgptr);
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
  log_append_redo_data2 (thread_p, RVVAC_REMOVE_OVF_INSID, NULL, ovfpage, 0, 0, NULL);
}

/*
 * vacuum_rv_redo_remove_ovf_insid () - Redo removing insert MVCCID from big record.
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

  error = heap_get_mvcc_rec_header_from_overflow (rcv->pgptr, &rec_header, NULL);
  if (error != NO_ERROR)
    {
      return error;
    }

  MVCC_SET_INSID (&rec_header, MVCCID_ALL_VISIBLE);
  LSA_SET_NULL (&rec_header.prev_version_lsa);

  error = heap_set_mvcc_rec_header_on_overflow (rcv->pgptr, &rec_header);
  if (error != NO_ERROR)
    {
      return error;
    }

  pgbuf_set_dirty (thread_p, rcv->pgptr, DONT_FREE);

  return NO_ERROR;
}

/*
 * vacuum_produce_log_block_data () - After logging a block of log data, useful information for vacuum is passed by log
 *				      manager and should be saved in lock-free buffer.
 *
 * return	      : Void.
 * thread_p (in)      : Thread entry.
 * start_lsa (in)     : Log block starting LSA.
 * oldest_mvccid (in) : Log block oldest MVCCID.
 * newest_mvccid (in) : Log block newest MVCCID.
 */
void
vacuum_produce_log_block_data (THREAD_ENTRY * thread_p)
{
  if (prm_get_bool_value (PRM_ID_DISABLE_VACUUM))
    {
      return;
    }
  assert (log_Gl.hdr.does_block_need_vacuum == true);
  // *INDENT-OFF*
  VACUUM_DATA_ENTRY block_data { log_Gl.hdr };
  // *INDENT-ON*

  // reset info for next block
  log_Gl.hdr.does_block_need_vacuum = false;
  log_Gl.hdr.newest_block_mvccid = MVCCID_NULL;

  if (vacuum_Block_data_buffer == NULL)
    {
      assert (false);
      return;
    }

  vacuum_er_log (VACUUM_ER_LOG_LOGGING | VACUUM_ER_LOG_VACUUM_DATA,
		 "vacuum_produce_log_block_data: blockid=(%lld) start_lsa=(%lld, %d) old_mvccid=(%llu) "
		 "new_mvccid=(%llu)", (long long) block_data.blockid, LSA_AS_ARGS (&block_data.start_lsa),
		 (unsigned long long int) block_data.oldest_visible_mvccid,
		 (unsigned long long int) block_data.newest_mvccid);

  /* Push new block into block data buffer */
  if (!vacuum_Block_data_buffer->produce (block_data))
    {
      /* Push failed, the buffer must be full */
      /* TODO: Set a new message error for full block data buffer */
      /* TODO: Probably this case should be avoided... Make sure that we do not lose vacuum data so there has to be
       * enough space to keep it. */
      vacuum_er_log_error (VACUUM_ER_LOG_ERROR, "%s", "Cannot produce new log block data! The buffer is already full.");
      assert (false);
      return;
    }

  perfmon_add_stat (thread_p, PSTAT_VAC_NUM_TO_VACUUM_LOG_PAGES, vacuum_Data.log_block_npages);
}

static void
vacuum_data_load_first_and_last_page (THREAD_ENTRY * thread_p)
{
  if (vacuum_Data.is_loaded)
    {
      return;
    }
  assert (vacuum_Data.first_page == NULL && vacuum_Data.last_page == NULL);
  vacuum_Data.first_page = vacuum_fix_data_page (thread_p, &vacuum_Data_load.vpid_first);
  if (vacuum_Data.first_page == NULL)
    {
      assert_release (false);
      return;
    }
  if (VPID_EQ (&vacuum_Data_load.vpid_first, &vacuum_Data_load.vpid_last))
    {
      vacuum_Data.last_page = vacuum_Data.first_page;
    }
  else
    {
      vacuum_Data.last_page = vacuum_fix_data_page (thread_p, &vacuum_Data_load.vpid_last);
      if (vacuum_Data.last_page == NULL)
	{
	  vacuum_unfix_first_and_last_data_page (thread_p);
	  assert_release (false);
	  return;
	}
    }
  vacuum_Data.is_loaded = true;
}

static void
vacuum_data_unload_first_and_last_page (THREAD_ENTRY * thread_p)
{
  if (!vacuum_Data.is_loaded)
    {
      return;
    }

  // save VPID's in case we need to reload
  pgbuf_get_vpid ((PAGE_PTR) vacuum_Data.first_page, &vacuum_Data_load.vpid_first);
  pgbuf_get_vpid ((PAGE_PTR) vacuum_Data.last_page, &vacuum_Data_load.vpid_last);

  vacuum_unfix_first_and_last_data_page (thread_p);
  vacuum_Data.is_loaded = false;
}

#if defined (SERVER_MODE)
// *INDENT-OFF*
void
vacuum_master_task::execute (cubthread::entry &thread_ref)
{
  PERF_UTIME_TRACKER perf_tracker;

  if (prm_get_bool_value (PRM_ID_DISABLE_VACUUM))
    {
      return;
    }

  if (check_shutdown ())
    {
      // stop on shutdown
      return;
    }

  if (!BO_IS_SERVER_RESTARTED ())
    {
      // check if boot is aborted
      return;
    }

  PERF_UTIME_TRACKER_START (&thread_ref, &perf_tracker);

  m_oldest_visible_mvccid = log_Gl.mvcc_table.update_global_oldest_visible ();
  vacuum_er_log (VACUUM_ER_LOG_MASTER, "update oldest_visible = %lld", (long long int) m_oldest_visible_mvccid);

  if (!vacuum_Data.is_loaded)
    {
      /* Load vacuum data. */
      /* This was initially in boot_restart_server. However, the "commit" of boot_restart_server will complain
       * about vacuum data first and last page not being unfixed (and it will also unfix them).
       * So, we have to load the data here (vacuum master never commits).
       */
      vacuum_data_load_first_and_last_page (&thread_ref);

      m_cursor.set_on_vacuum_data_start ();
    }

  pgbuf_flush_if_requested (&thread_ref, (PAGE_PTR) vacuum_Data.first_page);
  pgbuf_flush_if_requested (&thread_ref, (PAGE_PTR) vacuum_Data.last_page);

  m_cursor.force_data_update ();
  vacuum_er_log (VACUUM_ER_LOG_MASTER | VACUUM_ER_LOG_JOBS, "Start searching jobs at " vacuum_job_cursor_print_format,
                 vacuum_job_cursor_print_args (m_cursor));
  for (; m_cursor.is_valid () && !should_interrupt_iteration (); m_cursor.increment_blockid ())
    {
      if (!is_cursor_entry_ready_to_vacuum ())
        {
          // next entries cannot be ready if current entry is not ready; stop this iteration
          break;
        }

      if (!is_cursor_entry_available ())
        {
          // try next block
          continue;
        }
      start_job_on_cursor_entry ();

      if (should_force_data_update ())
        {
          m_cursor.force_data_update ();
        }
    }
  m_cursor.unload ();
#if !defined (NDEBUG)
  vacuum_verify_vacuum_data_page_fix_count (&thread_ref);
#endif /* !NDEBUG */
  PERF_UTIME_TRACKER_TIME (&thread_ref, &perf_tracker, PSTAT_VAC_MASTER);
}

bool
vacuum_master_task::check_shutdown() const
{
  if (vacuum_Data.shutdown_sequence.check_shutdown_request ())
    {
      // stop on shutdown
      vacuum_er_log (VACUUM_ER_LOG_MASTER, "%s", "Interrupt iteration: shutdown");
      return true;
    }
  return false;
}

bool
vacuum_master_task::is_task_queue_full() const
{
  if (cubthread::get_manager ()->is_pool_full (vacuum_Worker_threads))
    {
      // stop if worker pool is full
      vacuum_er_log (VACUUM_ER_LOG_MASTER, "%s", "Interrupt iteration: full worker pool");
      return true;
    }
  return false;
}

bool
vacuum_master_task::should_interrupt_iteration () const
{
  return check_shutdown () || is_task_queue_full ();
}

bool
vacuum_master_task::is_cursor_entry_ready_to_vacuum () const
{
  assert (m_cursor.is_valid ());

  if (m_cursor.get_current_entry ().newest_mvccid >= m_oldest_visible_mvccid)
    {
      // if entry newest MVCCID is still visible, it cannot be vacuumed
      vacuum_er_log (VACUUM_ER_LOG_JOBS,
                     "Cannot generate job for " VACUUM_LOG_DATA_ENTRY_MSG ("entry") ". "
                     "global oldest visible mvccid = %llu.",
                     VACUUM_LOG_DATA_ENTRY_AS_ARGS (&m_cursor.get_current_entry ()),
                     (unsigned long long int) m_oldest_visible_mvccid);
      return false;
    }

  if (m_cursor.get_current_entry ().start_lsa.pageid + 1 >= log_Gl.append.prev_lsa.pageid)
    {
      // too close to end of log; let more log be appended before trying to vacuum the block
      vacuum_er_log (VACUUM_ER_LOG_JOBS,
                       "Cannot generate job for " VACUUM_LOG_DATA_ENTRY_MSG ("entry") ". "
                       "log_Gl.append.prev_lsa.pageid = %d.",
                       VACUUM_LOG_DATA_ENTRY_AS_ARGS (&m_cursor.get_current_entry ()),
                       (long long int) log_Gl.append.prev_lsa.pageid);
      return false;
    }

  return true;
}

bool
vacuum_master_task::is_cursor_entry_available () const
{
  const vacuum_data_entry &entry = m_cursor.get_current_entry ();
  if (entry.is_available ())
    {
      return true;
    }
  else
    {
      // already vacuumed or entry job is in progress
      assert (entry.is_vacuumed () || entry.is_job_in_progress ());
      vacuum_er_log (VACUUM_ER_LOG_JOBS,
                     "Job for blockid = %lld %s. Skip.", (long long int) entry.get_blockid (),
                     entry.is_vacuumed () ? "was executed" : "is in progress");
      return false;
    }
}

void
vacuum_master_task::start_job_on_cursor_entry () const
{
  m_cursor.start_job_on_current_entry ();
  cubthread::get_manager ()->push_task (vacuum_Worker_threads,
                                        new vacuum_worker_task (m_cursor.get_current_entry ()));
}

bool
vacuum_master_task::should_force_data_update () const
{
  if (vacuum_Finished_job_queue->is_half_full ())
    {
      // don't wait until it's full
      return true;
    }
  if (vacuum_Block_data_buffer->is_half_full ())
    {
      // don't wait until it's full
      return true;
    }

  return false;
}
// *INDENT-ON*
#endif // SERVER_MODE

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
  vacuum_Data.oldest_unvacuumed_mvccid = oldest_newest_mvccid;

  /* Reset log header information saved for vacuum. */
  logpb_vacuum_reset_log_header_cache (thread_p, &log_Gl.hdr);

  pgbuf_set_dirty (thread_p, rcv->pgptr, DONT_FREE);

  return NO_ERROR;
}

/*
 * vacuum_process_log_block () - Vacuum heap and b-tree entries using log information found in a block of pages.
 *
 * return		      : Error code.
 * thread_p (in)	      : Thread entry.
 * data (in)		      : Block data.
 * block_log_buffer (in)      : Block log page buffer identifier
 * sa_mode_partial_block (in) : True when SA_MODE vacuum based on partial block information from log header.
 *				Logging is skipped if true.
 */
static int
vacuum_process_log_block (THREAD_ENTRY * thread_p, VACUUM_DATA_ENTRY * data, bool sa_mode_partial_block)
{
  VACUUM_WORKER *worker = vacuum_get_vacuum_worker (thread_p);
  LOG_LSA log_lsa;
  LOG_LSA rcv_lsa;
  LOG_PAGEID first_block_pageid = VACUUM_FIRST_LOG_PAGEID_IN_BLOCK (data->get_blockid ());
  int error_code = NO_ERROR;
  LOG_DATA log_record_data;
  char *undo_data = NULL;
  int undo_data_size;
  char *es_uri = NULL;
  char log_pgbuf[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT];
  LOG_PAGE *log_page_p = NULL;
  BTID_INT btid_int;
  BTID sys_btid;
  OID class_oid, oid;
  BTREE_OBJECT_INFO old_version;
  BTREE_OBJECT_INFO new_version;
  MVCCID threshold_mvccid = log_Gl.mvcc_table.get_global_oldest_visible ();
  BTREE_MVCC_INFO mvcc_info;
  MVCCID mvccid;
  LOG_VACUUM_INFO log_vacuum;
  OID heap_object_oid;
  bool vacuum_complete = false;
  bool was_interrupted = false;
  bool is_file_dropped = false;

  PERF_UTIME_TRACKER perf_tracker;
  PERF_UTIME_TRACKER job_time_tracker;
#if defined (SA_MODE)
  bool dummy_continue_check = false;
#endif /* SA_MODE */

  if (prm_get_bool_value (PRM_ID_DISABLE_VACUUM))
    {
      return NO_ERROR;
    }
  assert (thread_p != NULL);
  assert (thread_p->get_system_tdes () != NULL);

  assert (worker != NULL);
  assert (!LOG_FIND_CURRENT_TDES (thread_p)->is_under_sysop ());

  PERF_UTIME_TRACKER_START (thread_p, &perf_tracker);
  PERF_UTIME_TRACKER_START (thread_p, &job_time_tracker);

  /* Initialize log_vacuum */
  LSA_SET_NULL (&log_vacuum.prev_mvcc_op_log_lsa);
  VFID_SET_NULL (&log_vacuum.vfid);

  /* Set sys_btid pointer for internal b-tree block */
  btid_int.sys_btid = &sys_btid;

  /* Check starting lsa is not null and that it really belong to this block */
  assert (!LSA_ISNULL (&data->start_lsa) && (data->get_blockid () == vacuum_get_log_blockid (data->start_lsa.pageid)));

  /* Fetch the page where start_lsa is located */
  log_page_p = (LOG_PAGE *) PTR_ALIGN (log_pgbuf, MAX_ALIGNMENT);
  log_page_p->hdr.logical_pageid = NULL_PAGEID;
  log_page_p->hdr.offset = NULL_OFFSET;

  vacuum_er_log (VACUUM_ER_LOG_WORKER | VACUUM_ER_LOG_JOBS,
		 "vacuum_process_log_block (): " VACUUM_LOG_DATA_ENTRY_MSG ("block"),
		 VACUUM_LOG_DATA_ENTRY_AS_ARGS (data));

  if (!sa_mode_partial_block)
    {
      error_code = vacuum_log_prefetch_vacuum_block (thread_p, data);
      if (error_code != NO_ERROR)
	{
	  return error_code;
	}
    }
  else
    {
      // block is not entirely logged and we cannot prefetch it.
    }

  /* Initialize stored heap objects. */
  worker->n_heap_objects = 0;

  /* set was_interrupted flag to tell vacuum_heap_page that some safe-guard have to behave differently. interruptions
   * are usually marked in blockid, however sa_mode_partial_block can also be interrupted and will no flag is set in
   * blockid. */
  was_interrupted = data->was_interrupted () || sa_mode_partial_block;

  /* Follow the linked records starting with start_lsa */
  for (LSA_COPY (&log_lsa, &data->start_lsa); !LSA_ISNULL (&log_lsa) && log_lsa.pageid >= first_block_pageid;
       LSA_COPY (&log_lsa, &log_vacuum.prev_mvcc_op_log_lsa))
    {
#if defined(SERVER_MODE)
      if (thread_p->shutdown)
	{
	  /* Server shutdown was requested, stop vacuuming. */
	  goto end;
	}
#else	/* !SERVER_MODE */		   /* SA_MODE */
      if (logtb_get_check_interrupt (thread_p) && logtb_is_interrupted (thread_p, true, &dummy_continue_check))
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INTERRUPTED, 0);
	  error_code = ER_INTERRUPTED;
	  goto end;
	}
#endif /* SERVER_MODE */

      vacuum_er_log (VACUUM_ER_LOG_WORKER, "process log entry at log_lsa %lld|%d", LSA_AS_ARGS (&log_lsa));

      worker->state = VACUUM_WORKER_STATE_PROCESS_LOG;
      PERF_UTIME_TRACKER_TIME_AND_RESTART (thread_p, &perf_tracker, PSTAT_VAC_WORKER_EXECUTE);

      LSA_COPY (&rcv_lsa, &log_lsa);

      if (log_page_p->hdr.logical_pageid != log_lsa.pageid)
	{
	  error_code = vacuum_fetch_log_page (thread_p, log_lsa.pageid, log_page_p);
	  if (error_code != NO_ERROR)
	    {
	      assert_release (false);
	      logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "vacuum_process_log_block");
	      goto end;
	    }
	}

      /* Process log entry and obtain relevant information for vacuum. */
      error_code =
	vacuum_process_log_record (thread_p, worker, &log_lsa, log_page_p, &log_record_data, &mvccid, &undo_data,
				   &undo_data_size, &log_vacuum, &is_file_dropped, false);
      if (error_code != NO_ERROR)
	{
	  vacuum_check_shutdown_interruption (thread_p, error_code);
	  goto end;
	}

      worker->state = VACUUM_WORKER_STATE_EXECUTE;
      PERF_UTIME_TRACKER_TIME_AND_RESTART (thread_p, &perf_tracker, PSTAT_VAC_WORKER_PROCESS_LOG);

      if (is_file_dropped)
	{
	  /* No need to vacuum */
	  vacuum_er_log (VACUUM_ER_LOG_WORKER | VACUUM_ER_LOG_DROPPED_FILES,
			 "Skip vacuuming based on %lld|%d in file %d|%d. Log record info: rcvindex=%d.",
			 (long long int) rcv_lsa.pageid, (int) rcv_lsa.offset, log_vacuum.vfid.volid,
			 log_vacuum.vfid.fileid, log_record_data.rcvindex);
	  continue;
	}

#if !defined (NDEBUG)
      if (MVCC_ID_FOLLOW_OR_EQUAL (mvccid, threshold_mvccid) || MVCC_ID_PRECEDES (mvccid, data->oldest_visible_mvccid)
	  || MVCC_ID_PRECEDES (data->newest_mvccid, mvccid))
	{
	  /* threshold_mvccid or mvccid or block data may be invalid */
	  assert (0);
	  logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "vacuum_process_log_block");
	  goto end;
	}
#endif /* !NDEBUG */

      if (LOG_IS_MVCC_HEAP_OPERATION (log_record_data.rcvindex))
	{
	  /* Collect heap object to be vacuumed at the end of the job. */
	  heap_object_oid.pageid = log_record_data.pageid;
	  heap_object_oid.volid = log_record_data.volid;
	  heap_object_oid.slotid = heap_rv_remove_flags_from_offset (log_record_data.offset);

	  error_code = vacuum_collect_heap_objects (thread_p, worker, &heap_object_oid, &log_vacuum.vfid);
	  if (error_code != NO_ERROR)
	    {
	      assert_release (false);
	      vacuum_er_log_error (VACUUM_ER_LOG_WORKER | VACUUM_ER_LOG_HEAP, "%s", "vacuum_collect_heap_objects.");
	      /* Release should not stop. */
	      er_clear ();
	      error_code = NO_ERROR;
	      continue;
	    }
	  vacuum_er_log (VACUUM_ER_LOG_HEAP | VACUUM_ER_LOG_WORKER,
			 "collected oid %d|%d|%d, in file %d|%d, based on %lld|%d", OID_AS_ARGS (&heap_object_oid),
			 VFID_AS_ARGS (&log_vacuum.vfid), LSA_AS_ARGS (&rcv_lsa));
	}
      else if (LOG_IS_MVCC_BTREE_OPERATION (log_record_data.rcvindex))
	{
	  /* Find b-tree entry and vacuum it */
	  OR_BUF key_buf;

	  assert (undo_data != NULL);

	  if (log_record_data.rcvindex == RVBT_MVCC_INSERT_OBJECT_UNQ)
	    {
	      btree_rv_read_keybuf_two_objects (thread_p, undo_data, undo_data_size, &btid_int, &old_version,
						&new_version, &key_buf);
	      COPY_OID (&oid, &old_version.oid);
	      COPY_OID (&class_oid, &old_version.class_oid);
	    }
	  else
	    {
	      btree_rv_read_keybuf_nocopy (thread_p, undo_data, undo_data_size, &btid_int, &class_oid, &oid, &mvcc_info,
					   &key_buf);
	    }
	  assert (!OID_ISNULL (&oid));

	  thread_p->read_ovfl_pages_count = 0;

	  /* Vacuum based on rcvindex. */
	  if (log_record_data.rcvindex == RVBT_MVCC_NOTIFY_VACUUM)
	    {
	      /* The notification comes from loading index. The object may be both inserted or deleted (load index
	       * considers all objects for visibility reasons). Vacuum must also decide to remove insert MVCCID or the
	       * entire object. */
	      if (MVCCID_IS_VALID (mvcc_info.delete_mvccid))
		{
		  vacuum_er_log (VACUUM_ER_LOG_BTREE | VACUUM_ER_LOG_WORKER,
				 "vacuum from b-tree: btidp(%d, %d|%d) oid(%d|%d|%d) "
				 "class_oid(%d|%d|%d), purpose=rem_object, mvccid=%llu, based on %lld|%d",
				 BTID_AS_ARGS (btid_int.sys_btid), OID_AS_ARGS (&oid), OID_AS_ARGS (&class_oid),
				 (unsigned long long int) mvcc_info.delete_mvccid, LSA_AS_ARGS (&rcv_lsa));
		  error_code =
		    btree_vacuum_object (thread_p, btid_int.sys_btid, &key_buf, &oid, &class_oid,
					 mvcc_info.delete_mvccid);
		}
	      else if (MVCCID_IS_VALID (mvcc_info.insert_mvccid) && mvcc_info.insert_mvccid != MVCCID_ALL_VISIBLE)
		{
		  vacuum_er_log (VACUUM_ER_LOG_BTREE | VACUUM_ER_LOG_WORKER,
				 "vacuum from b-tree: btidp(%d, %d|%d) oid(%d|%d|%d) class_oid(%d|%d|%d), "
				 "purpose=rem_insid, mvccid=%llu, based on %lld|%d",
				 BTID_AS_ARGS (btid_int.sys_btid), OID_AS_ARGS (&oid), OID_AS_ARGS (&class_oid),
				 (unsigned long long int) mvcc_info.insert_mvccid, LSA_AS_ARGS (&rcv_lsa));
		  error_code =
		    btree_vacuum_insert_mvccid (thread_p, btid_int.sys_btid, &key_buf, &oid, &class_oid,
						mvcc_info.insert_mvccid);
		}
	      else
		{
		  /* impossible case */
		  vacuum_er_log_error (VACUUM_ER_LOG_BTREE | VACUUM_ER_LOG_WORKER,
				       "invalid vacuum case for RVBT_MVCC_NOTIFY_VACUUM btid(%d, %d|%d) "
				       "oid(%d|%d|%d) class_oid(%d|%d|%d), based on %lld|%d",
				       BTID_AS_ARGS (btid_int.sys_btid), OID_AS_ARGS (&oid), OID_AS_ARGS (&class_oid),
				       LSA_AS_ARGS (&rcv_lsa));
		  assert_release (false);
		  continue;
		}
	    }
	  else if (log_record_data.rcvindex == RVBT_MVCC_DELETE_OBJECT)
	    {
	      /* Object was deleted and must be completely removed. */
	      vacuum_er_log (VACUUM_ER_LOG_BTREE | VACUUM_ER_LOG_WORKER,
			     "vacuum from b-tree: btidp(%d, %d|%d) oid(%d|%d|%d) "
			     "class_oid(%d|%d|%d), purpose=rem_object, mvccid=%llu, based on %lld|%d",
			     BTID_AS_ARGS (btid_int.sys_btid), OID_AS_ARGS (&oid), OID_AS_ARGS (&class_oid),
			     (unsigned long long int) mvccid, LSA_AS_ARGS (&rcv_lsa));
	      error_code = btree_vacuum_object (thread_p, btid_int.sys_btid, &key_buf, &oid, &class_oid, mvccid);
	    }
	  else if (log_record_data.rcvindex == RVBT_MVCC_INSERT_OBJECT
		   || log_record_data.rcvindex == RVBT_MVCC_INSERT_OBJECT_UNQ)
	    {
	      /* Object was inserted and only its insert MVCCID must be removed. */
	      vacuum_er_log (VACUUM_ER_LOG_BTREE | VACUUM_ER_LOG_WORKER,
			     "vacuum from b-tree: btidp(%d, (%d %d)) oid(%d, %d, %d) "
			     "class_oid(%d, %d, %d), purpose=rem_insid, mvccid=%llu, based on %lld|%d",
			     BTID_AS_ARGS (btid_int.sys_btid), OID_AS_ARGS (&oid), OID_AS_ARGS (&class_oid),
			     (unsigned long long int) mvccid, LSA_AS_ARGS (&rcv_lsa));
	      error_code = btree_vacuum_insert_mvccid (thread_p, btid_int.sys_btid, &key_buf, &oid, &class_oid, mvccid);
	    }
	  else
	    {
	      /* Unexpected. */
	      assert_release (false);
	    }

#if defined (SERVER_MODE)
	  if (thread_p->read_ovfl_pages_count >= g_ovfp_threshold_mgr.get_threshold_page_cnt ())
	    {
	      g_ovfp_threshold_mgr.add_read_pages_count (thread_p, worker->idx, btid_int.sys_btid,
							 thread_p->read_ovfl_pages_count);
	    }
#endif

	  /* Did we have any errors? */
	  if (error_code != NO_ERROR)
	    {
	      if (thread_p->shutdown)
		{
		  // interrupted on shutdown
		  goto end;
		}
	      // unexpected case
	      assert_release (false);
	      vacuum_er_log (VACUUM_ER_LOG_BTREE | VACUUM_ER_LOG_WORKER,
			     "Error deleting object or insert MVCCID: error_code=%d", error_code);
	      er_clear ();
	      error_code = NO_ERROR;
	      /* Release should not stop. Continue. */
	    }
	}
      else if (log_record_data.rcvindex == RVES_NOTIFY_VACUUM)
	{
	  /* A lob file must be deleted */
	  (void) or_unpack_string (undo_data, &es_uri);
	  vacuum_er_log (VACUUM_ER_LOG_WORKER, "Delete lob %s based on %lld|%d", es_uri, LSA_AS_ARGS (&rcv_lsa));
	  if (es_delete_file (es_uri) != NO_ERROR)
	    {
	      er_clear ();
	    }
	  else
	    {
	      ASSERT_NO_ERROR ();
	    }
	  db_private_free_and_init (thread_p, es_uri);
	}
      else
	{
	  /* Safeguard code */
	  assert_release (false);
	}

      /* do not leak system ops */
      assert (worker->state == VACUUM_WORKER_STATE_EXECUTE);
      assert (!LOG_FIND_CURRENT_TDES (thread_p)->is_under_sysop ());
    }

  assert (worker->state == VACUUM_WORKER_STATE_EXECUTE);
  assert (!LOG_FIND_CURRENT_TDES (thread_p)->is_under_sysop ());

  error_code = vacuum_heap (thread_p, worker, threshold_mvccid, was_interrupted);
  if (error_code != NO_ERROR)
    {
      vacuum_check_shutdown_interruption (thread_p, error_code);
      goto end;
    }
  assert (worker->state == VACUUM_WORKER_STATE_EXECUTE);
  assert (!LOG_FIND_CURRENT_TDES (thread_p)->is_under_sysop ());

  perfmon_add_stat (thread_p, PSTAT_VAC_NUM_VACUUMED_LOG_PAGES, vacuum_Data.log_block_npages);

  vacuum_complete = true;

end:

  assert (!LOG_FIND_CURRENT_TDES (thread_p)->is_under_sysop ());

  worker->state = VACUUM_WORKER_STATE_INACTIVE;
  if (!sa_mode_partial_block)
    {
      /* TODO: Check that if start_lsa can be set to a different value when vacuum is not complete, to avoid processing
       * the same log data again. */
      vacuum_finished_block_vacuum (thread_p, data, vacuum_complete);
    }

#if defined (SERVER_MODE)
  /* Unfix all pages now. Normally all pages should already be unfixed. */
  pgbuf_unfix_all (thread_p);
#else	/* !SERVER_MODE */		   /* SA_MODE */
  /* Do not unfix all in stand-alone. Not yet. We need to keep vacuum data pages fixed. */
#endif /* SA_MODE */

  PERF_UTIME_TRACKER_TIME_AND_RESTART (thread_p, &perf_tracker, PSTAT_VAC_WORKER_EXECUTE);
  PERF_UTIME_TRACKER_TIME (thread_p, &job_time_tracker, PSTAT_VAC_JOB);

  return error_code;
}

/*
 * vacuum_worker_allocate_resources () - Assign a vacuum worker to current thread.
 *
 * return	 : Error code.
 * thread_p (in) : Thread entry.
 *
 * NOTE: This is protected by vacuum data lock.
 */
static int
vacuum_worker_allocate_resources (THREAD_ENTRY * thread_p, VACUUM_WORKER * worker)
{
  size_t size_worker_prefetch_log_buffer;

  assert (worker->state == VACUUM_WORKER_STATE::VACUUM_WORKER_STATE_INACTIVE);

  if (worker->allocated_resources)
    {
      return NO_ERROR;
    }

  /* Allocate log_zip */
  worker->log_zip_p = log_zip_alloc (IO_PAGESIZE);
  if (worker->log_zip_p == NULL)
    {
      vacuum_er_log_error (VACUUM_ER_LOG_WORKER, "%s", "Could not allocate log zip.");
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "vacuum_worker_allocate_resources");
      return ER_FAILED;
    }

  /* Allocate heap objects buffer */
  worker->heap_objects_capacity = VACUUM_DEFAULT_HEAP_OBJECT_BUFFER_SIZE;
  worker->heap_objects = (VACUUM_HEAP_OBJECT *) malloc (worker->heap_objects_capacity * sizeof (VACUUM_HEAP_OBJECT));
  if (worker->heap_objects == NULL)
    {
      vacuum_er_log_error (VACUUM_ER_LOG_WORKER, "%s", "Could not allocate files and objects buffer.");
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "vacuum_worker_allocate_resources");
      goto error;
    }

  /* Allocate undo data buffer */
  worker->undo_data_buffer = (char *) malloc (IO_PAGESIZE);
  if (worker->undo_data_buffer == NULL)
    {
      vacuum_er_log_error (VACUUM_ER_LOG_WORKER, "%s", "Could not allocate undo data buffer.");
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "vacuum_worker_allocate_resources");
      goto error;
    }
  worker->undo_data_buffer_capacity = IO_PAGESIZE;

  size_worker_prefetch_log_buffer = VACUUM_PREFETCH_LOG_BLOCK_BUFFER_PAGES * LOG_PAGESIZE;
  worker->prefetch_log_buffer = (char *) malloc (size_worker_prefetch_log_buffer);
  if (worker->prefetch_log_buffer == NULL)
    {
      vacuum_er_log_error (VACUUM_ER_LOG_WORKER, "%s", "Could not allocate prefetch buffer.");
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "vacuum_worker_allocate_resources");
      goto error;
    }

  /* Safe guard - it is assumed that transaction descriptor is already initialized. */
  assert (logtb_get_system_tdes (thread_p) != NULL);

  worker->allocated_resources = true;

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
  if (worker_info->prefetch_log_buffer != NULL)
    {
      free_and_init (worker_info->prefetch_log_buffer);
    }
}

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
vacuum_finished_block_vacuum (THREAD_ENTRY * thread_p, VACUUM_DATA_ENTRY * data, bool is_vacuum_complete)
{
  VACUUM_LOG_BLOCKID blockid;

  if (is_vacuum_complete)
    {
      /* Set status as vacuumed. Vacuum master will remove it from table */
      data->set_vacuumed ();

      vacuum_er_log (VACUUM_ER_LOG_WORKER | VACUUM_ER_LOG_VACUUM_DATA | VACUUM_ER_LOG_JOBS,
		     "Processing log block %lld is complete. Notify master.", (long long int) data->get_blockid ());
    }
  else
    {
      /* We expect that worker job is abandoned during shutdown. But all other cases are error cases. */
      int error_level =
#if defined (SERVER_MODE)
	thread_p->shutdown ? VACUUM_ER_LOG_WARNING : VACUUM_ER_LOG_ERROR;

#if !defined (NDEBUG)
      /* Interrupting jobs without shutdown is unacceptable. */
      assert (thread_p->shutdown);
      assert (vacuum_Data.shutdown_sequence.is_shutdown_requested ());
#endif /* !NDEBUG */

#else /* !SERVER_MODE */
	er_errid () == ER_INTERRUPTED ? VACUUM_ER_LOG_WARNING : VACUUM_ER_LOG_ERROR;
      assert (er_errid () == ER_INTERRUPTED);
#endif /* !SERVER_MODE */

      /* Vacuum will have to be re-run */
      data->set_interrupted ();

      /* Copy new block data */
      /* The only relevant information is in fact the updated start_lsa if it has changed. */
      if (error_level == VACUUM_ER_LOG_ERROR)
	{
	  vacuum_er_log_error (VACUUM_ER_LOG_VACUUM_DATA | VACUUM_ER_LOG_JOBS,
			       "Processing log block %lld is interrupted!", (long long int) data->get_blockid ());
	}
      else
	{
	  vacuum_er_log_warning (VACUUM_ER_LOG_VACUUM_DATA | VACUUM_ER_LOG_JOBS,
				 "Processing log block %lld is interrupted!", (long long int) data->get_blockid ());
	}
    }

  /* Notify master the job is finished. */
  blockid = data->blockid;
  if (!vacuum_Finished_job_queue->produce (blockid))
    {
      assert_release (false);
      vacuum_er_log_error (VACUUM_ER_LOG_WORKER | VACUUM_ER_LOG_JOBS, "%s", "Finished job queue is full!!!");
    }

#if defined (SERVER_MODE)
  /* Hurry master wakeup if finished job queue is getting filled. */
  if (vacuum_Finished_job_queue->is_half_full ())
    {
      /* Wakeup master to process finished jobs. */
      vacuum_Master_daemon->wakeup ();
    }
#endif /* SERVER_MODE */
}

/*
 * vacuum_read_log_aligned () - clone of LOG_READ_ALIGN based on vacuum_fetch_log_page
 *
 * thread_entry (in) : thread entry
 * log_lsa (in/out)  : log lsa
 * log_page (in/out) : log page
 */
static void
vacuum_read_log_aligned (THREAD_ENTRY * thread_entry, LOG_LSA * log_lsa, LOG_PAGE * log_page)
{
  // align offset
  log_lsa->offset = DB_ALIGN (log_lsa->offset, DOUBLE_ALIGNMENT);
  while (log_lsa->offset >= (int) LOGAREA_SIZE)
    {
      log_lsa->pageid++;
      if (vacuum_fetch_log_page (thread_entry, log_lsa->pageid, log_page) != NO_ERROR)
	{
	  // we cannot recover from this
	  logpb_fatal_error (thread_entry, true, ARG_FILE_LINE, "vacuum_read_log_aligned");
	}

      log_lsa->offset = DB_ALIGN (log_lsa->offset - (int) LOGAREA_SIZE, DOUBLE_ALIGNMENT);
    }
}

/*
 * vacuum_read_log_add_aligned () - clone of LOG_READ_ADD_ALIGN based on vacuum_fetch_log_page
 *
 * thread_entry (in) : thread entry
 * size (in)         : size to add
 * log_lsa (in/out)  : log lsa
 * log_page (in/out) : log page
 */
static void
vacuum_read_log_add_aligned (THREAD_ENTRY * thread_entry, size_t size, LOG_LSA * log_lsa, LOG_PAGE * log_page)
{
  log_lsa->offset += (int) size;
  vacuum_read_log_aligned (thread_entry, log_lsa, log_page);
}

/*
 * vacuum_read_advance_when_doesnt_fit () - clone of LOG_READ_ADVANCE_WHEN_DOESNT_FIT based on vacuum_fetch_log_page
 *
 * thread_entry (in) : thread entry
 * size (in)         : size to add
 * log_lsa (in/out)  : log lsa
 * log_page (in/out) : log page
 */
static void
vacuum_read_advance_when_doesnt_fit (THREAD_ENTRY * thread_entry, size_t size, LOG_LSA * log_lsa, LOG_PAGE * log_page)
{
  if (log_lsa->offset + (int) size >= (int) LOGAREA_SIZE)
    {
      log_lsa->offset = (int) LOGAREA_SIZE;	// force fetching next page
      vacuum_read_log_aligned (thread_entry, log_lsa, log_page);
      log_lsa->offset = 0;
    }
}

/*
 * vacuum_copy_data_from_log () - clone of logpb_copy_from_log based on vacuum_fetch_log_page
 *
 * thread_entry (in) : thread entry
 * area (out)        : where to copy log data
 * length (in)       : how much log data to copy
 * size (in)         : size to add
 * log_lsa (in/out)  : log lsa
 * log_page (in/out) : log page
 */
static void
vacuum_copy_data_from_log (THREAD_ENTRY * thread_p, char *area, int length, LOG_LSA * log_lsa, LOG_PAGE * log_page)
{
  if (log_lsa->offset + length < (int) LOGAREA_SIZE)
    {
      // the log data is contiguous
      std::memcpy (area, log_page->area + log_lsa->offset, length);
      log_lsa->offset += length;
    }
  else
    {
      int copy_length = 0;
      int area_offset = 0;

      while (length > 0)
	{
	  vacuum_read_advance_when_doesnt_fit (thread_p, 0, log_lsa, log_page);
	  if (log_lsa->offset + length < (int) LOGAREA_SIZE)
	    {
	      copy_length = length;
	    }
	  else
	    {
	      copy_length = LOGAREA_SIZE - (int) log_lsa->offset;
	    }
	  std::memcpy (area + area_offset, log_page->area + log_lsa->offset, copy_length);
	  length -= copy_length;
	  area_offset += copy_length;
	  log_lsa->offset += copy_length;
	}
    }
}

/*
 * vacuum_process_log_record () - Process one log record for vacuum.
 *
 * return			  : Error code.
 * worker (in)			  : Vacuum worker.
 * thread_p (in)		  : Thread entry.
 * log_lsa_p (in/out)		  : Input is the start of undo data. Output is the end of undo data.
 * log_page_p (in/out)		  : The log page for log_lsa_p.
 * mvccid (out)			  : Log entry MVCCID.
 * undo_data_ptr (out)		  : Undo data pointer.
 * undo_data_size (out)		  : Undo data size.
 * is_file_dropped (out)	  : True if the file corresponding to log entry was dropped.
 * stop_after_vacuum_info (in)	  : True if only vacuum info must be obtained from log record.
 */
static int
vacuum_process_log_record (THREAD_ENTRY * thread_p, VACUUM_WORKER * worker, LOG_LSA * log_lsa_p, LOG_PAGE * log_page_p,
			   LOG_DATA * log_record_data, MVCCID * mvccid, char **undo_data_ptr, int *undo_data_size,
			   LOG_VACUUM_INFO * vacuum_info, bool * is_file_dropped, bool stop_after_vacuum_info)
{
  LOG_RECORD_HEADER *log_rec_header = NULL;
  LOG_REC_MVCC_UNDOREDO *mvcc_undoredo = NULL;
  LOG_REC_MVCC_UNDO *mvcc_undo = NULL;
  LOG_REC_SYSOP_END *sysop_end = NULL;
  int ulength;
  char *new_undo_data_buffer = NULL;
  bool is_zipped = false;
  volatile LOG_RECTYPE log_rec_type = LOG_SMALLER_LOGREC_TYPE;

  int error_code = NO_ERROR;

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
  vacuum_read_log_add_aligned (thread_p, sizeof (*log_rec_header), log_lsa_p, log_page_p);

  if (log_rec_type == LOG_MVCC_UNDO_DATA)
    {
      /* Get log record mvcc_undo information */
      vacuum_read_advance_when_doesnt_fit (thread_p, sizeof (*mvcc_undo), log_lsa_p, log_page_p);
      mvcc_undo = (LOG_REC_MVCC_UNDO *) (log_page_p->area + log_lsa_p->offset);

      /* Get MVCCID */
      *mvccid = mvcc_undo->mvccid;

      /* Get record log data */
      *log_record_data = mvcc_undo->undo.data;

      /* Get undo data length */
      ulength = mvcc_undo->undo.length;

      /* Copy LSA for next MVCC operation */
      LSA_COPY (&vacuum_info->prev_mvcc_op_log_lsa, &mvcc_undo->vacuum_info.prev_mvcc_op_log_lsa);
      VFID_COPY (&vacuum_info->vfid, &mvcc_undo->vacuum_info.vfid);

      vacuum_read_log_add_aligned (thread_p, sizeof (*mvcc_undo), log_lsa_p, log_page_p);
    }
  else if (log_rec_type == LOG_MVCC_UNDOREDO_DATA || log_rec_type == LOG_MVCC_DIFF_UNDOREDO_DATA)
    {
      /* Get log record undoredo information */
      vacuum_read_advance_when_doesnt_fit (thread_p, sizeof (*mvcc_undoredo), log_lsa_p, log_page_p);
      mvcc_undoredo = (LOG_REC_MVCC_UNDOREDO *) (log_page_p->area + log_lsa_p->offset);

      /* Get MVCCID */
      *mvccid = mvcc_undoredo->mvccid;

      /* Get record log data */
      *log_record_data = mvcc_undoredo->undoredo.data;

      /* Get undo data length */
      ulength = mvcc_undoredo->undoredo.ulength;

      /* Copy LSA for next MVCC operation */
      LSA_COPY (&vacuum_info->prev_mvcc_op_log_lsa, &mvcc_undoredo->vacuum_info.prev_mvcc_op_log_lsa);
      VFID_COPY (&vacuum_info->vfid, &mvcc_undoredo->vacuum_info.vfid);

      vacuum_read_log_add_aligned (thread_p, sizeof (*mvcc_undoredo), log_lsa_p, log_page_p);
    }
  else if (log_rec_type == LOG_SYSOP_END)
    {
      /* Get system op mvcc undo information */
      vacuum_read_advance_when_doesnt_fit (thread_p, sizeof (*sysop_end), log_lsa_p, log_page_p);
      sysop_end = (LOG_REC_SYSOP_END *) (log_page_p->area + log_lsa_p->offset);
      if (sysop_end->type != LOG_SYSOP_END_LOGICAL_MVCC_UNDO)
	{
	  assert (false);
	  vacuum_er_log_error (VACUUM_ER_LOG_LOGGING, "%s", "invalid record type!");
	  return ER_FAILED;
	}

      mvcc_undo = &sysop_end->mvcc_undo;

      /* Get MVCCID */
      *mvccid = mvcc_undo->mvccid;

      /* Get record log data */
      *log_record_data = mvcc_undo->undo.data;

      /* Get undo data length */
      ulength = mvcc_undo->undo.length;

      /* Copy LSA for next MVCC operation */
      LSA_COPY (&vacuum_info->prev_mvcc_op_log_lsa, &mvcc_undo->vacuum_info.prev_mvcc_op_log_lsa);
      VFID_COPY (&vacuum_info->vfid, &mvcc_undo->vacuum_info.vfid);

      vacuum_read_log_add_aligned (thread_p, sizeof (*sysop_end), log_lsa_p, log_page_p);
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
	  /* New files have been dropped. Droppers must wait until all running workers have been notified. Save new
	   * version to let dropper know this worker noticed the changes. */

	  /* But first, cleanup collected heap objects. */
	  VFID vfid;
	  VFID_COPY (&vfid, &vacuum_Last_dropped_vfid);
	  vacuum_cleanup_collected_by_vfid (worker, &vfid);

	  worker->drop_files_version = vacuum_Dropped_files_version;
	  vacuum_er_log (VACUUM_ER_LOG_DROPPED_FILES | VACUUM_ER_LOG_WORKER,
			 "update min version to %d", worker->drop_files_version);
	}

      /* Check if file is dropped */
      error_code = vacuum_is_file_dropped (thread_p, is_file_dropped, &vacuum_info->vfid, *mvccid);
      if (error_code != NO_ERROR)
	{
	  vacuum_check_shutdown_interruption (thread_p, error_code);
	  return error_code;
	}
      if (*is_file_dropped == true)
	{
	  return NO_ERROR;
	}
    }

  /* We are here because the file that will be vacuumed is not dropped. */
  if (!LOG_IS_MVCC_BTREE_OPERATION (log_record_data->rcvindex) && log_record_data->rcvindex != RVES_NOTIFY_VACUUM)
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
      /* Undo data is found on several pages and needs to be copied to a contiguous area. */
      if (worker->undo_data_buffer_capacity < *undo_data_size)
	{
	  /* Not enough space to save all undo data. Expand worker's undo data buffer. */
	  new_undo_data_buffer = (char *) realloc (worker->undo_data_buffer, *undo_data_size);
	  if (new_undo_data_buffer == NULL)
	    {
	      vacuum_er_log_error (VACUUM_ER_LOG_WORKER, "Could not expand undo data buffer to %d.", *undo_data_size);
	      logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "vacuum_process_log_record");
	      return ER_FAILED;
	    }
	  /* Expand was successful, update worker. */
	  worker->undo_data_buffer = new_undo_data_buffer;
	  worker->undo_data_buffer_capacity = *undo_data_size;
	}
      /* Set undo data pointer to worker's undo data buffer. */
      *undo_data_ptr = worker->undo_data_buffer;

      /* Copy data to buffer. */
      vacuum_copy_data_from_log (thread_p, *undo_data_ptr, *undo_data_size, log_lsa_p, log_page_p);
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
	  vacuum_er_log_error (VACUUM_ER_LOG_WORKER, "%s", "Could not unzip undo data.");
	  logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "vacuum_process_log_record");
	  return ER_FAILED;
	}
    }

  return NO_ERROR;
}

#if defined (SERVER_MODE)
/*
 * vacuum_get_worker_min_dropped_files_version () - Get current minimum dropped files version seen by active
 *						    vacuum workers.
 *
 * return : Minimum dropped files version.
 */
static INT32
vacuum_get_worker_min_dropped_files_version (void)
{
  int i;
  INT32 min_version = -1;

  for (i = 0; i < VACUUM_MAX_WORKER_COUNT; i++)
    {
      /* Update minimum version if worker is active and its seen version is smaller than current minimum version (or if
       * minimum version is not initialized). */
      if (vacuum_Workers[i].state != VACUUM_WORKER_STATE_INACTIVE
	  && (min_version == -1
	      || vacuum_compare_dropped_files_version (min_version, vacuum_Workers[i].drop_files_version) > 0))
	{
	  /* Update overall minimum version. */
	  min_version = vacuum_Workers[i].drop_files_version;
	}
    }
  return min_version;
}
#endif // SERVER_MODE

/*
 * vacuum_compare_blockids () - Comparator function for blockid's stored in vacuum data. The comparator knows to
 *				filter any flags that mark block status.
 *
 * return    : 0 if entries are equal, negative if first entry is smaller and
 *	       positive if first entry is bigger.
 * ptr1 (in) : Pointer to first blockid.
 * ptr2 (in) : Pointer to second blockid.
 */
static int
vacuum_compare_blockids (const void *ptr1, const void *ptr2)
{
  /* Compare blockid's by removing any other flags. */
  return (int) (VACUUM_BLOCKID_WITHOUT_FLAGS (*((VACUUM_LOG_BLOCKID *) ptr1))
		- VACUUM_BLOCKID_WITHOUT_FLAGS (*((VACUUM_LOG_BLOCKID *) ptr2)));
}

/*
 * vacuum_data_load_and_recover () - Loads vacuum data from disk and recovers it.
 *
 * return	 : Error code.
 * thread_p (in) : Thread entry.
 *
 * NOTE: Loading vacuum data should be done when the database is started,
 *	 before starting other vacuum routines.
 */
int
vacuum_data_load_and_recover (THREAD_ENTRY * thread_p)
{
  int error_code = NO_ERROR;
  VACUUM_DATA_ENTRY *entry = NULL;
  VACUUM_DATA_PAGE *data_page = NULL;
  VPID next_vpid;
  int i = 0;
  bool is_page_dirty;
  FILE_DESCRIPTORS fdes;

  assert_release (!VFID_ISNULL (&vacuum_Data.vacuum_data_file));

  error_code = file_descriptor_get (thread_p, &vacuum_Data.vacuum_data_file, &fdes);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR_AND_SET (error_code);
      goto end;
    }

  assert (!VPID_ISNULL (&fdes.vacuum_data.vpid_first));

  data_page = vacuum_fix_data_page (thread_p, &fdes.vacuum_data.vpid_first);
  if (data_page == NULL)
    {
      ASSERT_ERROR_AND_SET (error_code);
      goto end;
    }
  vacuum_Data.first_page = data_page;
  vacuum_Data.oldest_unvacuumed_mvccid = MVCCID_NULL;

  while (true)
    {
      is_page_dirty = false;
      if (data_page->index_unvacuumed >= 0)
	{
	  assert (data_page->index_unvacuumed < vacuum_Data.page_data_max_count);
	  assert (data_page->index_unvacuumed <= data_page->index_free);
	  for (i = data_page->index_unvacuumed; i < data_page->index_free; i++)
	    {
	      entry = &data_page->data[i];
	      if (entry->is_job_in_progress ())
		{
		  /* Reset in progress flag, mark the job as interrupted and update last_blockid. */
		  entry->set_interrupted ();
		  is_page_dirty = true;
		}
	    }
	}
      if (is_page_dirty)
	{
	  vacuum_set_dirty_data_page (thread_p, data_page, DONT_FREE);
	}
      VPID_COPY (&next_vpid, &data_page->next_page);
      if (VPID_ISNULL (&next_vpid))
	{
	  break;
	}
      vacuum_unfix_data_page (thread_p, data_page);
      data_page = vacuum_fix_data_page (thread_p, &next_vpid);
      if (data_page == NULL)
	{
	  ASSERT_ERROR_AND_SET (error_code);
	  goto end;
	}
    }
  assert (data_page != NULL);
  /* Save last_page. */
  vacuum_Data.last_page = data_page;
  data_page = NULL;
  /* Get last_blockid. */
  if (vacuum_is_empty ())
    {
      VACUUM_LOG_BLOCKID log_blockid = logpb_last_complete_blockid ();

      if (log_blockid < 0)
	{
	  // we can be here if log has not yet passed first block. one case may be soon after copydb.
	  assert (log_blockid == VACUUM_NULL_LOG_BLOCKID);
	  vacuum_er_log (VACUUM_ER_LOG_VACUUM_DATA | VACUUM_ER_LOG_RECOVERY,
			 "vacuum_data_load_and_recover: do not update last_blockid; prev_lsa = %lld|%d",
			 LSA_AS_ARGS (&log_Gl.append.prev_lsa));
	}
      else if (LSA_ISNULL (&vacuum_Data.recovery_lsa) && LSA_ISNULL (&log_Gl.hdr.mvcc_op_log_lsa))
	{
	  /* No recovery needed. This is used for 10.1 version to keep the functionality of the database.
	   * In this case, we are updating the last_blockid of the vacuum to the last block that was logged.
	   */
	  vacuum_Data.set_last_blockid (log_blockid);

	  vacuum_er_log (VACUUM_ER_LOG_VACUUM_DATA | VACUUM_ER_LOG_RECOVERY,
			 "vacuum_data_load_and_recover: set last_blockid = %lld to logpb_last_complete_blockid ()",
			 (long long int) vacuum_Data.get_last_blockid ());
	}
      else
	{
	  /* Get the maximum between what is currently stored in vacuum and the value stored
	   * in the log_Gl header. After a long session in SA_MODE, the vacuum_Data.last_page->data->blockid will
	   * be outdated. Instead, SA_MODE updates log_Gl.hdr.vacuum_last_blockid before removing old archives.
	   */
	  vacuum_Data.set_last_blockid (MAX (log_Gl.hdr.vacuum_last_blockid, vacuum_Data.last_page->data->blockid));

	  vacuum_er_log (VACUUM_ER_LOG_VACUUM_DATA | VACUUM_ER_LOG_RECOVERY,
			 "vacuum_data_load_and_recover: set last_blockid = %lld to MAX("
			 "log_Gl.hdr.vacuum_last_blockid=%lld, vacuum_Data.last_page->data->blockid=%lld)",
			 (long long int) vacuum_Data.get_last_blockid (),
			 (long long int) log_Gl.hdr.vacuum_last_blockid,
			 (long long int) vacuum_Data.last_page->data->blockid);
	}
    }
  else
    {
      /* Get last_blockid from last vacuum data entry. */
      INT16 last_block_index = (vacuum_Data.last_page->index_free <= 0) ? 0 : vacuum_Data.last_page->index_free - 1;
      vacuum_Data.set_last_blockid (vacuum_Data.last_page->data[last_block_index].blockid);

      vacuum_er_log (VACUUM_ER_LOG_VACUUM_DATA | VACUUM_ER_LOG_RECOVERY,
		     "vacuum_data_load_and_recover: set last_blockid = %lld to last data blockid = %lld",
		     (long long int) vacuum_Data.get_last_blockid (),
		     (long long int) vacuum_Data.last_page->data[last_block_index].blockid);
    }

  vacuum_Data.is_loaded = true;

  /* get global oldest active MVCCID. */
  (void) log_Gl.mvcc_table.update_global_oldest_visible ();

  error_code = vacuum_recover_lost_block_data (thread_p);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      goto end;
    }
  LSA_SET_NULL (&vacuum_Data.recovery_lsa);

  vacuum_Data.set_oldest_unvacuumed_on_boot ();
  vacuum_update_keep_from_log_pageid (thread_p);

#if !defined (NDEBUG)
  vacuum_verify_vacuum_data_page_fix_count (thread_p);
#endif /* !NDEBUG */

  /* note: this is called when server is started, after recovery. however, pages cannot remain fixed by current thread,
   *       they must be fixed by vacuum master. therefore, we'll save first and last vpids to vacuum_Data_load and unfix
   *       them here. */
  pgbuf_get_vpid ((PAGE_PTR) vacuum_Data.first_page, &vacuum_Data_load.vpid_first);
  pgbuf_get_vpid ((PAGE_PTR) vacuum_Data.last_page, &vacuum_Data_load.vpid_last);

end:

  if (data_page != NULL)
    {
      vacuum_unfix_data_page (thread_p, data_page);
    }
  vacuum_data_unload_first_and_last_page (thread_p);

  return error_code;
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
      page = vacuum_fix_dropped_entries_page (thread_p, &vpid, PGBUF_LATCH_READ);
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
      track_new = (VACUUM_TRACK_DROPPED_FILES *) malloc (VACUUM_TRACK_DROPPED_FILES_SIZE);
      if (track_new == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, VACUUM_TRACK_DROPPED_FILES_SIZE);
	  for (track_new = track_head; track_new != NULL; track_new = save_next)
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
 * vacuum_create_file_for_vacuum_data () - Create a disk file to keep vacuum data.
 *
 * return		   : Error code.
 * thread_p (in)	   : Thread entry.
 * vacuum_data_npages (in) : Number of vacuum data disk pages.
 * vacuum_data_vfid (out)  : Created file VFID.
 */
int
vacuum_create_file_for_vacuum_data (THREAD_ENTRY * thread_p, VFID * vacuum_data_vfid)
{
  VPID first_page_vpid;
  VACUUM_DATA_PAGE *data_page = NULL;
  PAGE_TYPE ptype = PAGE_VACUUM_DATA;
  FILE_DESCRIPTORS fdes;

  int error_code = NO_ERROR;

  /* Create disk file to keep vacuum data */
  error_code = file_create_with_npages (thread_p, FILE_VACUUM_DATA, 1, NULL, vacuum_data_vfid);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }
  error_code = file_alloc (thread_p, vacuum_data_vfid, file_init_page_type, &ptype, &first_page_vpid,
			   (PAGE_PTR *) (&data_page));
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }
  if (data_page == NULL)
    {
      assert_release (false);
      return ER_FAILED;
    }

  /* save in file descriptors to load when database is restarted */
  fdes.vacuum_data.vpid_first = first_page_vpid;
  error_code = file_descriptor_update (thread_p, vacuum_data_vfid, &fdes);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }

  vacuum_init_data_page_with_last_blockid (thread_p, data_page, 0);
  vacuum_unfix_data_page (thread_p, data_page);

  return NO_ERROR;
}

/*
 * vacuum_data_initialize_new_page () - Create new vacuum data page.
 *
 * return	      :	Void.
 * thread_p (in)      : Thread entry.
 * data_page (in)     : New vacuum data page pointer.
 * first_blockid (in) : Starting blockid.
 */
static void
vacuum_data_initialize_new_page (THREAD_ENTRY * thread_p, VACUUM_DATA_PAGE * data_page)
{
  memset (data_page, 0, DB_PAGESIZE);

  VPID_SET_NULL (&data_page->next_page);
  data_page->index_unvacuumed = 0;
  data_page->index_free = 0;

  pgbuf_set_page_ptype (thread_p, (PAGE_PTR) data_page, PAGE_VACUUM_DATA);

  vacuum_er_log (VACUUM_ER_LOG_VACUUM_DATA, "Initialized " PGBUF_PAGE_STATE_MSG ("vacuum data page"),
		 PGBUF_PAGE_STATE_ARGS ((PAGE_PTR) data_page));
}

/*
 * vacuum_rv_redo_initialize_data_page () - Redo initialize vacuum data page.
 *
 * return	 : NO_ERROR.
 * thread_p (in) : Thread entry.
 * rcv (in)	 : Recovery data.
 */
int
vacuum_rv_redo_initialize_data_page (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  VACUUM_DATA_PAGE *data_page = (VACUUM_DATA_PAGE *) rcv->pgptr;
  VACUUM_LOG_BLOCKID last_blockid = VACUUM_NULL_LOG_BLOCKID;

  assert (data_page != NULL);
  assert (rcv->length == sizeof (last_blockid));
  last_blockid = *((VACUUM_LOG_BLOCKID *) rcv->data);

  vacuum_data_initialize_new_page (thread_p, data_page);
  data_page->data->blockid = last_blockid;

  pgbuf_set_dirty (thread_p, rcv->pgptr, DONT_FREE);
  return NO_ERROR;
}

/*
 * vacuum_create_file_for_dropped_files () - Create a disk file to track dropped files for vacuum.
 *
 * return		  : Error code.
 * thread_p (in)	  : Thread entry.
 * vacuum_data_vfid (out) : Created file VFID.
 */
int
vacuum_create_file_for_dropped_files (THREAD_ENTRY * thread_p, VFID * dropped_files_vfid)
{
  VPID first_page_vpid;
  VACUUM_DROPPED_FILES_PAGE *dropped_files_page = NULL;
  PAGE_TYPE ptype = PAGE_DROPPED_FILES;
  int error_code = NO_ERROR;

  /* Create disk file to keep dropped files */
  error_code = file_create_with_npages (thread_p, FILE_DROPPED_FILES, 1, NULL, dropped_files_vfid);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }
  error_code = file_alloc_sticky_first_page (thread_p, dropped_files_vfid, file_init_page_type, &ptype,
					     &first_page_vpid, (PAGE_PTR *) (&dropped_files_page));
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }
  if (dropped_files_page == NULL)
    {
      assert (false);
      return ER_FAILED;
    }

  /* Initialize dropped files */
  /* Pack VPID of next page as NULL OID and count as 0 */
  VPID_SET_NULL (&dropped_files_page->next_page);
  dropped_files_page->n_dropped_files = 0;

  pgbuf_set_page_ptype (thread_p, (PAGE_PTR) dropped_files_page, PAGE_DROPPED_FILES);

  /* Set dirty page and free */
  vacuum_set_dirty_dropped_entries_page (thread_p, dropped_files_page, FREE);

  return NO_ERROR;
}

/*
 * vacuum_is_work_in_progress () - Returns true if there are any vacuum jobs running.
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
#if defined (SERVER_MODE)
  int i;

  for (i = 0; i < VACUUM_MAX_WORKER_COUNT; i++)
    {
      if (vacuum_Workers[i].state != VACUUM_WORKER_STATE_INACTIVE)
	{
	  return true;
	}
    }

  /* No running jobs, return false */
  return false;
#else // not SERVER_MODE = SA_MODE
  return false;
#endif // not SERVER_MODE = SA_MODE
}

/*
 * vacuum_data_mark_finished () - Mark blocks already vacuumed (or interrupted).
 *
 * return	 : Void.
 * thread_p (in) : Thread entry.
 */
static void
vacuum_data_mark_finished (THREAD_ENTRY * thread_p)
{
#define TEMP_BUFFER_SIZE VACUUM_FINISHED_JOB_QUEUE_CAPACITY
  VACUUM_LOG_BLOCKID finished_blocks[TEMP_BUFFER_SIZE];
  VACUUM_LOG_BLOCKID blockid;
  VACUUM_LOG_BLOCKID page_unvacuumed_blockid;
  VACUUM_LOG_BLOCKID page_free_blockid;
  VACUUM_DATA_PAGE *data_page = NULL;
  VACUUM_DATA_PAGE *prev_data_page = NULL;
  VACUUM_DATA_ENTRY *data = NULL;
  VACUUM_DATA_ENTRY *page_unvacuumed_data = NULL;
  INT16 n_finished_blocks = 0;
  INT16 index = 0;
  INT16 page_start_index = 0;
  VPID next_vpid = VPID_INITIALIZER;

  /* Consume finished block ID's from queue. */
  /* Stop if too many blocks have been collected (> TEMP_BUFFER_SIZE). */
  while (n_finished_blocks < TEMP_BUFFER_SIZE
	 && vacuum_Finished_job_queue->consume (finished_blocks[n_finished_blocks]))
    {
      /* Increment consumed finished blocks. */
      vacuum_er_log (VACUUM_ER_LOG_VACUUM_DATA, "Consumed from finished job queue %lld (flags %lld).",
		     (long long int) VACUUM_BLOCKID_WITHOUT_FLAGS (finished_blocks[n_finished_blocks]),
		     VACUUM_BLOCKID_GET_FLAGS (finished_blocks[n_finished_blocks]));
      ++n_finished_blocks;
    }
  if (n_finished_blocks == 0)
    {
      /* No blocks. */
      return;
    }
  /* Sort consumed blocks. */
  qsort (finished_blocks, n_finished_blocks, sizeof (VACUUM_LOG_BLOCKID), vacuum_compare_blockids);

  /* Mark finished blocks in vacuum data. */

  /* Loop to mark all finished blocks in all affected pages. */
  index = 0;
  data_page = vacuum_Data.first_page;
  page_start_index = 0;
  assert (data_page->index_unvacuumed >= 0);
  page_unvacuumed_data = data_page->data + data_page->index_unvacuumed;
  page_unvacuumed_blockid = page_unvacuumed_data->get_blockid ();
  page_free_blockid = page_unvacuumed_blockid + (data_page->index_free - data_page->index_unvacuumed);
  assert (page_free_blockid == data_page->data[data_page->index_free - 1].get_blockid () + 1);
  while (true)
    {
      /* Loop until all blocks from current pages are marked. */
      while ((index < n_finished_blocks)
	     && ((blockid = VACUUM_BLOCKID_WITHOUT_FLAGS (finished_blocks[index])) < page_free_blockid))
	{
	  /* Update status for block. */
	  data = page_unvacuumed_data + (blockid - page_unvacuumed_blockid);
	  assert (data->get_blockid () == blockid);
	  assert (data->is_job_in_progress ());
	  if (VACUUM_BLOCK_STATUS_IS_VACUUMED (finished_blocks[index]))
	    {
	      /* Block has been vacuumed. */
	      data->set_vacuumed ();

	      vacuum_er_log (VACUUM_ER_LOG_VACUUM_DATA | VACUUM_ER_LOG_JOBS,
			     "Mark block %lld as vacuumed.", (long long int) data->get_blockid ());
	    }
	  else
	    {
	      /* Block was not completely vacuumed. Job was interrupted. */
	      data->set_interrupted ();

	      vacuum_er_log_warning (VACUUM_ER_LOG_VACUUM_DATA | VACUUM_ER_LOG_JOBS,
				     "Mark block %lld as interrupted.", (long long int) data->get_blockid ());
	    }
	  index++;
	}
      /* Finished marking blocks. */

      if (index == page_start_index)
	{
	  /* No changes in page. Nothing to do. */
	  /* Fall through. */
	}
      else
	{
	  /* Some blocks in page were changed. */

	  /* Update index_unvacuumed. */
	  while (data_page->index_unvacuumed < data_page->index_free && page_unvacuumed_data->is_vacuumed ())
	    {
	      page_unvacuumed_data++;
	      data_page->index_unvacuumed++;
	    }

	  if (data_page->index_unvacuumed == data_page->index_free)
	    {
	      /* Nothing left in page to be vacuumed. */

	      vacuum_data_empty_page (thread_p, prev_data_page, &data_page);
	      /* Should have advanced on next page. */
	      if (data_page == NULL)
		{
		  /* No next page */
		  if (prev_data_page != NULL)
		    {
		      vacuum_unfix_data_page (thread_p, prev_data_page);
		    }
		  if (n_finished_blocks > index)
		    {
		      assert (false);
		      vacuum_er_log_error (VACUUM_ER_LOG_VACUUM_DATA, "%s",
					   "Finished blocks not found in vacuum data!!!!");
		      return;
		    }
		  else
		    {
		      /* Break loop. */
		      break;
		    }
		}
	      else
		{
		  /* Continue with new page. */
		  page_start_index = index;
		  assert (data_page->index_unvacuumed >= 0);
		  page_unvacuumed_data = data_page->data + data_page->index_unvacuumed;
		  page_unvacuumed_blockid = page_unvacuumed_data->get_blockid ();
		  page_free_blockid = page_unvacuumed_blockid + (data_page->index_free - data_page->index_unvacuumed);
		  continue;
		}
	    }
	  else
	    {
	      /* Page still has some data. */

	      if (VPID_ISNULL (&data_page->next_page))
		{
		  /* We remove first blocks that have been vacuumed. */
		  if (data_page->index_unvacuumed > 0)
		    {
		      /* Relocate everything at the start of the page. */
		      memmove (data_page->data, data_page->data + data_page->index_unvacuumed,
			       (data_page->index_free - data_page->index_unvacuumed) * sizeof (VACUUM_DATA_ENTRY));
		      data_page->index_free -= data_page->index_unvacuumed;
		      data_page->index_unvacuumed = 0;
		    }
		}

	      /* Log changes. */
	      log_append_redo_data2 (thread_p, RVVAC_DATA_FINISHED_BLOCKS, NULL, (PAGE_PTR) data_page, 0,
				     (index - page_start_index) * sizeof (VACUUM_LOG_BLOCKID),
				     &finished_blocks[page_start_index]);
	      vacuum_set_dirty_data_page (thread_p, data_page, DONT_FREE);
	    }
	}

      if (prev_data_page != NULL)
	{
	  vacuum_unfix_data_page (thread_p, prev_data_page);
	}
      if (index == n_finished_blocks)
	{
	  /* All finished blocks have been consumed. */
	  vacuum_unfix_data_page (thread_p, data_page);
	  break;
	}
      if (VPID_ISNULL (&data_page->next_page))
	{
	  assert (false);
	  vacuum_er_log_error (VACUUM_ER_LOG_VACUUM_DATA, "%s", "Finished blocks not found in vacuum data!!!!");
	  vacuum_unfix_data_page (thread_p, data_page);
	  return;
	}

      prev_data_page = data_page;
      VPID_COPY (&next_vpid, &data_page->next_page);
      data_page = vacuum_fix_data_page (thread_p, &next_vpid);
      if (data_page == NULL)
	{
	  assert_release (false);
	  vacuum_unfix_data_page (thread_p, prev_data_page);
	  return;
	}
      page_start_index = index;
      assert (data_page->index_unvacuumed >= 0);
      page_unvacuumed_data = data_page->data + data_page->index_unvacuumed;
      page_unvacuumed_blockid = page_unvacuumed_data->get_blockid ();
      page_free_blockid = page_unvacuumed_blockid + (data_page->index_free - data_page->index_unvacuumed);
    }
  assert (prev_data_page == NULL);

  /* We need to update vacuum_Data.keep_from_log_pageid in case archives must be purged. */
  vacuum_update_keep_from_log_pageid (thread_p);

  VACUUM_VERIFY_VACUUM_DATA (thread_p);
#if !defined (NDEBUG)
  vacuum_verify_vacuum_data_page_fix_count (thread_p);
#endif /* !NDEBUG */

#undef TEMP_BUFFER_SIZE
}

/*
 * vacuum_data_empty_page () - Handle empty vacuum data page.
 *
 * return	       : Void.
 * thread_p (in)       : Thread entry.
 * prev_data_page (in) : Previous vacuum data page.
 * data_page (in/out)  : Empty page as input, prev page as output..
 */
static void
vacuum_data_empty_page (THREAD_ENTRY * thread_p, VACUUM_DATA_PAGE * prev_data_page, VACUUM_DATA_PAGE ** data_page)
{
  FILE_DESCRIPTORS fdes_update;

  /* We can have three expected cases here:
   * 1. This is the last page. We won't deallocate, just reset the page (even if it is also first page).
   * 2. This is the first page and there are other pages too (case #1 covers first page = last page case).
   *    We will deallocate the page and update the first page.
   * 3. Page is not first and is not last. It must be deallocated.
   */
  assert (data_page != NULL && *data_page != NULL);
  assert ((*data_page)->index_unvacuumed == (*data_page)->index_free);

  if (*data_page == vacuum_Data.last_page)
    {
      /* Case 1. */
      /* Reset page. */
      vacuum_init_data_page_with_last_blockid (thread_p, *data_page, vacuum_Data.get_last_blockid ());

      vacuum_er_log (VACUUM_ER_LOG_VACUUM_DATA,
		     "Last page, vpid = %d|%d, is empty and was reset. %s",
		     pgbuf_get_vpid_ptr ((PAGE_PTR) (*data_page))->volid,
		     pgbuf_get_vpid_ptr ((PAGE_PTR) (*data_page))->pageid,
		     vacuum_Data.first_page == vacuum_Data.last_page ?
		     "This is also first page." : "This is different from first page.");

      /* No next page */
      *data_page = NULL;
    }
  else if (*data_page == vacuum_Data.first_page)
    {
      /* Case 2. */
      VACUUM_DATA_PAGE *save_first_page = vacuum_Data.first_page;
      VPID save_first_vpid;

      *data_page = vacuum_fix_data_page (thread_p, &((*data_page)->next_page));
      if (*data_page == NULL)
	{
	  /* Unexpected. */
	  assert_release (false);
	  vacuum_er_log_error (VACUUM_ER_LOG_VACUUM_DATA, "%s", "Invalid vacuum data next_page!!!");
	  *data_page = vacuum_Data.first_page;
	  return;
	}

      /* save vpid of first page */
      pgbuf_get_vpid ((PAGE_PTR) save_first_page, &save_first_vpid);

      log_sysop_start (thread_p);

      /* update file descriptor for persistence */
      fdes_update.vacuum_data.vpid_first = save_first_page->next_page;
      if (file_descriptor_update (thread_p, &vacuum_Data.vacuum_data_file, &fdes_update) != NO_ERROR)
	{
	  assert_release (false);
	  vacuum_er_log_error (VACUUM_ER_LOG_VACUUM_DATA,
			       "Failed to update file descriptor!!!", save_first_vpid.volid, save_first_vpid.pageid);
	  log_sysop_abort (thread_p);

	  return;
	}

      /* change first_page */
      vacuum_Data.first_page = *data_page;
      vacuum_Data_load.vpid_first = save_first_page->next_page;

      /* Make sure the new first page is marked as dirty */
      vacuum_set_dirty_data_page (thread_p, vacuum_Data.first_page, DONT_FREE);
      /* Unfix old first page. */
      vacuum_unfix_data_page (thread_p, save_first_page);
      if (file_dealloc (thread_p, &vacuum_Data.vacuum_data_file, &save_first_vpid, FILE_VACUUM_DATA) != NO_ERROR)
	{
	  assert_release (false);
	  vacuum_er_log_error (VACUUM_ER_LOG_VACUUM_DATA,
			       "Failed to deallocate first page from vacuum data - %d|%d!!!",
			       save_first_vpid.volid, save_first_vpid.pageid);
	  log_sysop_abort (thread_p);

	  /* Revert first page change
	   * - this is just to handle somehow the case in release. Should never happen anyway.
	   */
	  save_first_page = vacuum_Data.first_page;
	  vacuum_Data.first_page = vacuum_fix_data_page (thread_p, &save_first_vpid);
	  vacuum_Data_load.vpid_first = save_first_vpid;
	  vacuum_unfix_data_page (thread_p, save_first_page);
	  *data_page = vacuum_Data.first_page;
	  return;
	}

      log_sysop_commit (thread_p);

      vacuum_er_log (VACUUM_ER_LOG_VACUUM_DATA, "Changed first VPID from %d|%d to %d|%d.",
		     VPID_AS_ARGS (&save_first_vpid), VPID_AS_ARGS (&fdes_update.vacuum_data.vpid_first));
    }
  else
    {
      /* Case 3 */
      VPID save_page_vpid = VPID_INITIALIZER;
      VPID save_next_vpid = VPID_INITIALIZER;

      assert (*data_page != vacuum_Data.first_page && *data_page != vacuum_Data.last_page);

      /* We must have prev_data_page. */
      if (prev_data_page == NULL)
	{
	  assert_release (false);
	  vacuum_er_log_error (VACUUM_ER_LOG_VACUUM_DATA, "%s", "No previous data page is unexpected!!!");
	  vacuum_unfix_data_page (thread_p, *data_page);
	  return;
	}

      log_sysop_start (thread_p);

      /* Save link to next page. */
      VPID_COPY (&save_next_vpid, &(*data_page)->next_page);
      /* Save data page VPID. */
      pgbuf_get_vpid ((PAGE_PTR) (*data_page), &save_page_vpid);
      /* Unfix data page. */
      vacuum_unfix_data_page (thread_p, *data_page);
      /* Deallocate data page. */
      if (file_dealloc (thread_p, &vacuum_Data.vacuum_data_file, &save_page_vpid, FILE_VACUUM_DATA) != NO_ERROR)
	{
	  assert_release (false);
	  vacuum_er_log_error (VACUUM_ER_LOG_VACUUM_DATA,
			       "Failed to deallocate page from vacuum data - %d|%d!!!",
			       save_page_vpid.volid, save_page_vpid.pageid);
	  log_sysop_abort (thread_p);
	  return;
	}

      /* Update link in previous page. */
      log_append_undoredo_data2 (thread_p, RVVAC_DATA_SET_LINK, NULL, (PAGE_PTR) prev_data_page, 0, sizeof (VPID),
				 sizeof (VPID), &prev_data_page->next_page, &save_next_vpid);
      VPID_COPY (&prev_data_page->next_page, &save_next_vpid);
      vacuum_set_dirty_data_page (thread_p, prev_data_page, DONT_FREE);

      log_sysop_commit (thread_p);

      assert (*data_page == NULL);
      /* Move *data_page to next page. */
      assert (!VPID_ISNULL (&prev_data_page->next_page));
      *data_page = vacuum_fix_data_page (thread_p, &prev_data_page->next_page);
      assert (*data_page != NULL);
    }
}

/*
 * vacuum_rv_redo_data_finished () - Redo setting vacuum jobs as finished (or interrupted).
 *
 * return	 : NO_ERROR.
 * thread_p (in) : Thread entry.
 * rcv (in)	 : Recovery data.
 */
int
vacuum_rv_redo_data_finished (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  const char *rcv_data_ptr = rcv->data;
  VACUUM_LOG_BLOCKID blockid;
  VACUUM_LOG_BLOCKID blockid_with_flags;
  VACUUM_LOG_BLOCKID page_unvacuumed_blockid;
  VACUUM_DATA_PAGE *data_page = (VACUUM_DATA_PAGE *) rcv->pgptr;
  int data_index;

  assert (data_page != NULL);

  page_unvacuumed_blockid = data_page->data[data_page->index_unvacuumed].get_blockid ();

  if (rcv_data_ptr != NULL)
    {
      while (rcv_data_ptr < (char *) rcv->data + rcv->length)
	{
	  assert (rcv_data_ptr + sizeof (VACUUM_LOG_BLOCKID) <= rcv->data + rcv->length);
	  blockid_with_flags = *((VACUUM_LOG_BLOCKID *) rcv_data_ptr);
	  blockid = VACUUM_BLOCKID_WITHOUT_FLAGS (blockid_with_flags);

	  assert (blockid >= page_unvacuumed_blockid);
	  data_index = (int) (blockid - page_unvacuumed_blockid) + data_page->index_unvacuumed;
	  assert (data_index < data_page->index_free);

	  if (VACUUM_BLOCK_STATUS_IS_VACUUMED (blockid_with_flags))
	    {
	      data_page->data[data_index].set_vacuumed ();
	    }
	  else
	    {
	      data_page->data[data_index].set_interrupted ();
	    }

	  rcv_data_ptr += sizeof (VACUUM_LOG_BLOCKID);
	}
      assert (rcv_data_ptr == rcv->data + rcv->length);
    }

  while (data_page->index_unvacuumed < data_page->index_free
	 && data_page->data[data_page->index_unvacuumed].is_vacuumed ())
    {
      data_page->index_unvacuumed++;
    }
  if (VPID_ISNULL (&data_page->next_page) && data_page->index_unvacuumed > 0)
    {
      /* Remove all vacuumed blocks. */
      if (data_page->index_free > data_page->index_unvacuumed)
	{
	  memmove (data_page->data, data_page->data + data_page->index_unvacuumed,
		   (data_page->index_free - data_page->index_unvacuumed) * sizeof (VACUUM_DATA_ENTRY));
	}
      data_page->index_free -= data_page->index_unvacuumed;
      data_page->index_unvacuumed = 0;
    }

  pgbuf_set_dirty (thread_p, rcv->pgptr, DONT_FREE);
  return NO_ERROR;
}

/*
 * vacuum_rv_redo_data_finished_dump () - Dump redo for setting vacuum jobs finished or interrupted.
 *
 * return      : Void.
 * fp (in)     : Output target.
 * length (in) : Recovery data length.
 * data (in)   : Recovery data.
 */
void
vacuum_rv_redo_data_finished_dump (FILE * fp, int length, void *data)
{
  const char *rcv_data_ptr = (const char *) data;
  VACUUM_LOG_BLOCKID blockid;
  VACUUM_LOG_BLOCKID blockid_with_flags;

  if (rcv_data_ptr != NULL)
    {
      fprintf (fp, " Set block status for vacuum data to : \n");
      while (rcv_data_ptr < (char *) data + length)
	{
	  assert (rcv_data_ptr + sizeof (VACUUM_LOG_BLOCKID) <= (char *) data + length);

	  blockid_with_flags = *((VACUUM_LOG_BLOCKID *) rcv_data_ptr);
	  blockid = VACUUM_BLOCKID_WITHOUT_FLAGS (blockid_with_flags);

	  if (VACUUM_BLOCK_STATUS_IS_VACUUMED (blockid_with_flags))
	    {
	      fprintf (fp, "   Blockid %lld: vacuumed. \n", (long long int) blockid);
	    }
	  else
	    {
	      fprintf (fp, "   Blockid %lld: available and interrupted. \n", (long long int) blockid);
	    }
	  rcv_data_ptr += sizeof (VACUUM_LOG_BLOCKID);
	}
    }
}

/*
 * vacuum_consume_buffer_log_blocks () - Append new blocks from log block data from buffer (if any).
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
#define MAX_PAGE_MAX_DATA_ENTRIES (IO_MAX_PAGE_SIZE / sizeof (VACUUM_DATA_ENTRY))
  VACUUM_DATA_ENTRY consumed_data;
  VACUUM_DATA_PAGE *data_page = NULL;
  VACUUM_DATA_ENTRY *page_free_data = NULL;
  VACUUM_DATA_ENTRY *save_page_free_data = NULL;
  VACUUM_LOG_BLOCKID next_blockid;
  PAGE_TYPE ptype = PAGE_VACUUM_DATA;
  bool is_sysop = false;
  bool was_vacuum_data_empty = false;

  int error_code = NO_ERROR;

  if (prm_get_bool_value (PRM_ID_DISABLE_VACUUM))
    {
      return NO_ERROR;
    }
  if (vacuum_Block_data_buffer == NULL)
    {
      /* Not initialized */
      assert (false);
      return NO_ERROR;
    }

  if (vacuum_Block_data_buffer->is_empty ())
    {
      /* empty */
      if (vacuum_is_empty ())
	{
	  // don't let vacuum data go too far back; try to update last blockid
	  // need to make sure that log_Gl.hdr.does_block_need_vacuum is not true; safest choice is to also hold
	  // log_Gl.prior_info.prior_lsa_mutex while doing it

	  if (log_Gl.hdr.does_block_need_vacuum)
	    {
	      // cannot update
	      return NO_ERROR;
	    }

          // *INDENT-OFF*
          std::unique_lock<std::mutex> ulock { log_Gl.prior_info.prior_lsa_mutex };
          // *INDENT-ON*
	  // need to double check log_Gl.hdr.does_block_need_vacuum while holding mutex
	  if (log_Gl.hdr.does_block_need_vacuum)
	    {
	      // cannot update
	      return NO_ERROR;
	    }
	  // check buffer again, it is possible that a new block was added
	  if (vacuum_Block_data_buffer->is_empty ())
	    {
	      // update last blockid
	      LOG_LSA log_lsa = log_Gl.prior_info.prior_lsa;
	      ulock.unlock ();	// unlock after reading prior_lsa

	      const VACUUM_LOG_BLOCKID LOG_BLOCK_TRAILING_DIFF = 2;
	      VACUUM_LOG_BLOCKID log_blockid = vacuum_get_log_blockid (log_lsa.pageid);

	      if (log_blockid > vacuum_Data.get_last_blockid () + LOG_BLOCK_TRAILING_DIFF)
		{
		  vacuum_Data.set_last_blockid (log_blockid - LOG_BLOCK_TRAILING_DIFF);
		  vacuum_data_empty_update_last_blockid (thread_p);
		  vacuum_update_keep_from_log_pageid (thread_p);
		  vacuum_er_log (VACUUM_ER_LOG_VACUUM_DATA, "update last_blockid to %lld",
				 (long long int) vacuum_Data.get_last_blockid ());
		}
	      return NO_ERROR;
	    }
	  else
	    {
	      // fall through to consume buffer
	    }
	}
      else
	{
	  // last blockid remains last in vacuum data
	  return NO_ERROR;
	}
    }

  if (vacuum_Data.last_page == NULL)
    {
      assert_release (false);
      return ER_FAILED;
    }

  data_page = vacuum_Data.last_page;
  page_free_data = data_page->data + data_page->index_free;
  save_page_free_data = page_free_data;

  was_vacuum_data_empty = vacuum_is_empty ();

  while (vacuum_Block_data_buffer->consume (consumed_data))
    {
      assert (vacuum_Data.get_last_blockid () < consumed_data.blockid);

      /* Add all blocks after vacuum_Data.last_blockid to consumed_data.blockid. */
      for (next_blockid = vacuum_Data.get_last_blockid () + 1; next_blockid <= consumed_data.blockid; next_blockid++)
	{
	  if (data_page->index_free == vacuum_Data.page_data_max_count)
	    {
	      /* This page is full. */
	      /* Append a new page to vacuum data. */
	      VPID next_vpid = VPID_INITIALIZER;
	      VACUUM_DATA_PAGE *save_last_page = NULL;

	      /* Log changes in this page. */
	      if (page_free_data > save_page_free_data)
		{
		  log_append_redo_data2 (thread_p, RVVAC_DATA_APPEND_BLOCKS, NULL, (PAGE_PTR) data_page,
					 (PGLENGTH) (save_page_free_data - data_page->data),
					 CAST_BUFLEN (((char *) page_free_data)
						      - (char *) save_page_free_data), save_page_free_data);

		  vacuum_set_dirty_data_page (thread_p, data_page, DONT_FREE);
		}
	      else
		{
		  /* No changes in current page. */
		}

	      if (is_sysop)
		{
		  // more than one page in one iteration, now that's a performance
		  log_sysop_commit (thread_p);
		}

	      log_sysop_start (thread_p);
	      is_sysop = true;

	      error_code = file_alloc (thread_p, &vacuum_Data.vacuum_data_file, file_init_page_type, &ptype, &next_vpid,
				       (PAGE_PTR *) (&data_page));
	      if (error_code != NO_ERROR)
		{
		  /* Could not allocate new page. */
		  vacuum_er_log_error (VACUUM_ER_LOG_VACUUM_DATA, "%s",
				       "Could not allocate new page for vacuum data!!!!");
		  assert_release (false);
		  log_sysop_abort (thread_p);
		  return error_code;
		}
	      if (data_page == NULL)
		{
		  assert_release (false);
		  log_sysop_abort (thread_p);
		  return ER_FAILED;
		}
	      vacuum_init_data_page_with_last_blockid (thread_p, data_page, vacuum_Data.get_last_blockid ());

	      /* Change link in last page. */
	      VPID_COPY (&vacuum_Data.last_page->next_page, &next_vpid);
	      log_append_undoredo_data2 (thread_p, RVVAC_DATA_SET_LINK, NULL, (PAGE_PTR) vacuum_Data.last_page, 0,
					 0, sizeof (VPID), NULL, &next_vpid);
	      save_last_page = vacuum_Data.last_page;
	      vacuum_Data.last_page = data_page;
	      vacuum_set_dirty_data_page (thread_p, save_last_page, FREE);

	      // we cannot commit here. we should append some data blocks first.

	      page_free_data = data_page->data + data_page->index_free;
	      save_page_free_data = page_free_data;
	    }
	  assert (data_page->index_free < vacuum_Data.page_data_max_count);

	  if (data_page->index_unvacuumed == data_page->index_free && next_blockid < consumed_data.blockid)
	    {
	      /* Page is empty. We don't want to add a new block that does not require vacuum. */
	      assert (data_page->index_unvacuumed == 0);
	      next_blockid = consumed_data.blockid - 1;	// for will increment it to consumed_data.blockid
	      continue;
	    }

	  page_free_data->blockid = next_blockid;
	  if (next_blockid == consumed_data.blockid)
	    {
	      /* Copy block information from consumed data. */
	      assert (page_free_data->is_available ());	// starts as available
	      LSA_COPY (&page_free_data->start_lsa, &consumed_data.start_lsa);
	      page_free_data->newest_mvccid = consumed_data.newest_mvccid;
	      page_free_data->oldest_visible_mvccid = consumed_data.oldest_visible_mvccid;
	      assert (log_Gl.mvcc_table.get_global_oldest_visible () >= page_free_data->oldest_visible_mvccid);
#if !defined (NDEBUG)
	      /* Check that oldest_mvccid is not decreasing. */
	      if (data_page->index_free > 0)
		{
		  assert ((page_free_data - 1)->oldest_visible_mvccid <= page_free_data->oldest_visible_mvccid);
		  assert ((page_free_data - 1)->get_blockid () + 1 == page_free_data->get_blockid ());
		}
#endif /* !NDEBUG */

	      vacuum_er_log (VACUUM_ER_LOG_VACUUM_DATA,
			     "Add block %lld, start_lsa=%lld|%d, oldest_visible_mvccid=%llu, newest_mvccid=%llu. "
			     "Hdr last blockid = %lld\n",
			     (long long int) page_free_data->get_blockid (),
			     (long long int) page_free_data->start_lsa.pageid, (int) page_free_data->start_lsa.offset,
			     (unsigned long long int) page_free_data->oldest_visible_mvccid,
			     (unsigned long long int) page_free_data->newest_mvccid,
			     (long long int) log_Gl.hdr.vacuum_last_blockid);
	    }
	  else
	    {
	      /* Mark the blocks with no MVCC operations as already vacuumed. */
	      page_free_data->set_vacuumed ();
	      LSA_SET_NULL (&page_free_data->start_lsa);
	      page_free_data->oldest_visible_mvccid = MVCCID_NULL;
	      page_free_data->newest_mvccid = MVCCID_NULL;

	      vacuum_er_log (VACUUM_ER_LOG_VACUUM_DATA,
			     "Block %lld has no MVCC ops and is skipped (marked as vacuumed).", next_blockid);
	    }
	  vacuum_Data.set_last_blockid (next_blockid);

	  /* Move to next free data */
	  page_free_data++;
	  data_page->index_free++;
	}
    }

  if (was_vacuum_data_empty)
    {
      vacuum_update_keep_from_log_pageid (thread_p);
    }

  assert (data_page == vacuum_Data.last_page);
  if (save_page_free_data < page_free_data)
    {
      /* Log changes in current page. */
      log_append_redo_data2 (thread_p, RVVAC_DATA_APPEND_BLOCKS, NULL, (PAGE_PTR) data_page,
			     (PGLENGTH) (save_page_free_data - data_page->data),
			     CAST_BUFLEN (((char *) page_free_data) - (char *) save_page_free_data),
			     save_page_free_data);
      if (is_sysop)
	{
	  log_sysop_commit (thread_p);
	}
      vacuum_set_dirty_data_page (thread_p, data_page, DONT_FREE);
    }
  else
    {
      // no change
      if (is_sysop)
	{
	  // invalid situation; don't leak sysop
	  assert (false);
	  log_sysop_commit (thread_p);
	}
    }

  VACUUM_VERIFY_VACUUM_DATA (thread_p);
#if !defined (NDEBUG)
  vacuum_verify_vacuum_data_page_fix_count (thread_p);
#endif /* !NDEBUG */

  return NO_ERROR;
}

/*
 * vacuum_rv_undoredo_data_set_link () - Undoredo set link in vacuum data page.
 *
 * return	 : NO_ERROR.
 * thread_p (in) : Thread entry.
 * rcv (in)	 : Recovery data.
 */
int
vacuum_rv_undoredo_data_set_link (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  VACUUM_DATA_PAGE *data_page = (VACUUM_DATA_PAGE *) rcv->pgptr;
  VPID *next_vpid = (VPID *) rcv->data;

  assert (data_page != NULL);

  if (next_vpid == NULL)
    {
      /* NULL link */
      VPID_SET_NULL (&data_page->next_page);
    }
  else
    {
      assert (rcv->length == sizeof (*next_vpid));
      VPID_COPY (&data_page->next_page, next_vpid);
    }
  pgbuf_set_dirty (thread_p, rcv->pgptr, DONT_FREE);
  return NO_ERROR;
}

/*
 * vacuum_rv_redo_data_set_link_dump () - Dump redo set link in vacuum data page
 *
 * return      : Void.
 * fp (in)     : Output target.
 * length (in) : Recovery data length.
 * data (in)   : Recovery data.
 */
void
vacuum_rv_undoredo_data_set_link_dump (FILE * fp, int length, void *data)
{
  if (data == NULL)
    {
      fprintf (fp, " Reset link in vacuum data page to -1|-1. \n");
    }
  else
    {
      fprintf (fp, " Set link in vacuum data page to %d|%d. \n", ((VPID *) data)->volid, ((VPID *) data)->pageid);
    }
}

/*
 * vacuum_rv_redo_append_data () - Redo append blocks to vacuum data.
 *
 * return	 : NO_ERROR.
 * thread_p (in) : Thread entry.
 * rcv (in)	 : Recovery data.
 */
int
vacuum_rv_redo_append_data (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  VACUUM_DATA_PAGE *data_page = (VACUUM_DATA_PAGE *) rcv->pgptr;
  int n_blocks = rcv->length / sizeof (VACUUM_DATA_ENTRY);

  assert (data_page != NULL);
  assert (rcv->length > 0);
  assert ((n_blocks * (int) sizeof (VACUUM_DATA_ENTRY)) == rcv->length);
  assert (rcv->offset == data_page->index_free);

  memcpy (data_page->data + rcv->offset, rcv->data, n_blocks * sizeof (VACUUM_DATA_ENTRY));
  data_page->index_free += n_blocks;
  pgbuf_set_dirty (thread_p, rcv->pgptr, DONT_FREE);
  return NO_ERROR;
}

/*
 * vacuum_rv_redo_append_data_dump () - Dump redo append blocks to vacuum data.
 *
 * return      : Void.
 * fp (in)     : Output target.
 * length (in) : Recovery data length.
 * data (in)   : Recovery data.
 */
void
vacuum_rv_redo_append_data_dump (FILE * fp, int length, void *data)
{
  VACUUM_DATA_ENTRY *vacuum_data_entry = NULL;

  fprintf (fp, " Append log blocks to vacuum data : \n");
  vacuum_data_entry = (VACUUM_DATA_ENTRY *) data;
  while ((char *) vacuum_data_entry < (char *) data + length)
    {
      assert ((char *) (vacuum_data_entry + 1) <= (char *) data + length);

      fprintf (fp, "  { Blockid = %lld, Start_Lsa = %lld|%d, Oldest_MVCCID = %llu, Newest_MVCCID = %llu } \n",
	       (long long int) vacuum_data_entry->blockid, (long long int) vacuum_data_entry->start_lsa.pageid,
	       (int) vacuum_data_entry->start_lsa.offset,
	       (unsigned long long int) vacuum_data_entry->oldest_visible_mvccid,
	       (unsigned long long int) vacuum_data_entry->newest_mvccid);

      vacuum_data_entry++;
    }
}

/*
 * vacuum_recover_lost_block_data () - If and when server crashed, the block data buffer may have not been empty.
 *				       These blocks must be recovered by processing MVCC op log records and must be
 *				       added back to vacuum data.
 *
 * return	 : Error code.
 * thread_p (in) : Thread entry.
 */
static int
vacuum_recover_lost_block_data (THREAD_ENTRY * thread_p)
{
  int error_code = NO_ERROR;
  char log_page_buf[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT];
  LOG_LSA log_lsa;
  LOG_RECORD_HEADER log_rec_header;
  LOG_PAGE *log_page_p = NULL;
  LOG_PAGEID stop_at_pageid;
  VACUUM_DATA_ENTRY data;
  LOG_DATA dummy_log_data;
  LOG_VACUUM_INFO vacuum_info;
  MVCCID mvccid;
  VACUUM_LOG_BLOCKID crt_blockid;
  LOG_LSA mvcc_op_log_lsa = LSA_INITIALIZER;

  vacuum_er_log (VACUUM_ER_LOG_VACUUM_DATA | VACUUM_ER_LOG_RECOVERY,
		 "vacuum_recover_lost_block_data, lsa = %lld|%d, global_oldest_visible_mvccid = %llu",
		 LSA_AS_ARGS (&vacuum_Data.recovery_lsa),
		 (unsigned long long int) log_Gl.mvcc_table.get_global_oldest_visible ());
  if (LSA_ISNULL (&vacuum_Data.recovery_lsa))
    {
      /* No recovery was done. */
      return NO_ERROR;
    }
  /* Recovery was done. */

  /* Initialize log_page_p. */
  log_page_p = (LOG_PAGE *) PTR_ALIGN (log_page_buf, MAX_ALIGNMENT);
  log_page_p->hdr.logical_pageid = NULL_PAGEID;
  log_page_p->hdr.offset = NULL_OFFSET;

  if (LSA_ISNULL (&log_Gl.hdr.mvcc_op_log_lsa))
    {
      /* We need to search for an MVCC op log record to start recovering lost blocks. */
      vacuum_er_log (VACUUM_ER_LOG_VACUUM_DATA | VACUUM_ER_LOG_RECOVERY, "%s",
		     "vacuum_recover_lost_block_data, log_Gl.hdr.mvcc_op_log_lsa is null ");

      LSA_COPY (&log_lsa, &vacuum_Data.recovery_lsa);
      /* todo: Find a better stopping point for this!! */
      /* Stop search if search reaches blocks already in vacuum data. */
      stop_at_pageid = VACUUM_LAST_LOG_PAGEID_IN_BLOCK (vacuum_Data.get_last_blockid ());
      while (log_lsa.pageid > stop_at_pageid)
	{
	  if (log_page_p->hdr.logical_pageid != log_lsa.pageid)
	    {
	      /* Get log page. */
	      error_code = logpb_fetch_page (thread_p, &log_lsa, LOG_CS_SAFE_READER, log_page_p);
	      if (error_code != NO_ERROR)
		{
		  ASSERT_ERROR ();
		  logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "vacuum_recover_lost_block_data");
		  return ER_FAILED;
		}
	    }
	  log_rec_header = *LOG_GET_LOG_RECORD_HEADER (log_page_p, &log_lsa);
	  if (log_rec_header.type == LOG_MVCC_UNDO_DATA || log_rec_header.type == LOG_MVCC_UNDOREDO_DATA
	      || log_rec_header.type == LOG_MVCC_DIFF_UNDOREDO_DATA)
	    {
	      LSA_COPY (&mvcc_op_log_lsa, &log_lsa);
	      vacuum_er_log (VACUUM_ER_LOG_VACUUM_DATA | VACUUM_ER_LOG_RECOVERY,
			     "vacuum_recover_lost_block_data, found mvcc op at lsa = %lld|%d ",
			     LSA_AS_ARGS (&mvcc_op_log_lsa));
	      break;
	    }
	  else if (log_rec_header.type == LOG_SYSOP_END)
	    {
	      /* we need to check if it is a logical MVCC undo */
	      LOG_REC_SYSOP_END *sysop_end = NULL;
	      LOG_LSA copy_lsa = log_lsa;

	      LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_RECORD_HEADER), &copy_lsa, log_page_p);
	      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (LOG_REC_SYSOP_END), &copy_lsa, log_page_p);
	      sysop_end = (LOG_REC_SYSOP_END *) (log_page_p->area + copy_lsa.offset);
	      if (sysop_end->type == LOG_SYSOP_END_LOGICAL_MVCC_UNDO)
		{
		  LSA_COPY (&mvcc_op_log_lsa, &log_lsa);
		  vacuum_er_log (VACUUM_ER_LOG_VACUUM_DATA | VACUUM_ER_LOG_RECOVERY,
				 "vacuum_recover_lost_block_data, found mvcc op at lsa = %lld|%d ",
				 LSA_AS_ARGS (&mvcc_op_log_lsa));
		  break;
		}
	    }
	  else if (log_rec_header.type == LOG_REDO_DATA)
	    {
	      /* is vacuum complete? */
	      LOG_REC_REDO *redo = NULL;
	      LOG_LSA copy_lsa = log_lsa;

	      LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_RECORD_HEADER), &copy_lsa, log_page_p);
	      LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (LOG_REC_REDO), &copy_lsa, log_page_p);
	      redo = (LOG_REC_REDO *) (log_page_p->area + copy_lsa.offset);
	      if (redo->data.rcvindex == RVVAC_COMPLETE)
		{
		  /* stop looking */
		  vacuum_er_log (VACUUM_ER_LOG_VACUUM_DATA | VACUUM_ER_LOG_RECOVERY, "%s",
				 "vacuum_recover_lost_block_data, complete vacuum ");
		  break;
		}
	    }

	  LSA_COPY (&log_lsa, &log_rec_header.back_lsa);
	}
      if (LSA_ISNULL (&mvcc_op_log_lsa))
	{
	  /* Vacuum data was reached, so there is nothing to recover. */
	  vacuum_er_log (VACUUM_ER_LOG_VACUUM_DATA | VACUUM_ER_LOG_RECOVERY, "%s",
			 "vacuum_recover_lost_block_data, nothing to recovery ");
	  return NO_ERROR;
	}
    }
  else if (vacuum_get_log_blockid (log_Gl.hdr.mvcc_op_log_lsa.pageid) <= vacuum_Data.get_last_blockid ())
    {
      /* Already in vacuum data. */
      vacuum_er_log (VACUUM_ER_LOG_VACUUM_DATA | VACUUM_ER_LOG_RECOVERY,
		     "vacuum_recover_lost_block_data, mvcc_op_log_lsa %lld|%d is already in vacuum data "
		     "(last blockid = %lld) ", LSA_AS_ARGS (&log_Gl.hdr.mvcc_op_log_lsa),
		     (long long int) vacuum_Data.get_last_blockid ());
      logpb_vacuum_reset_log_header_cache (thread_p, &log_Gl.hdr);
      return NO_ERROR;
    }
  else
    {
      LSA_COPY (&mvcc_op_log_lsa, &log_Gl.hdr.mvcc_op_log_lsa);
    }
  assert (!LSA_ISNULL (&mvcc_op_log_lsa));

  // reset header; info will be restored if last block is not consumed.
  logpb_vacuum_reset_log_header_cache (thread_p, &log_Gl.hdr);

  vacuum_er_log (VACUUM_ER_LOG_VACUUM_DATA | VACUUM_ER_LOG_RECOVERY,
		 "vacuum_recover_lost_block_data, start recovering from %lld|%d ", LSA_AS_ARGS (&mvcc_op_log_lsa));

  /* Start recovering blocks. */
  crt_blockid = vacuum_get_log_blockid (mvcc_op_log_lsa.pageid);
  LSA_COPY (&log_lsa, &mvcc_op_log_lsa);

  // stack used to produce in reverse order data for vacuum_Block_data_buffer circular queue
  /* *INDENT-OFF* */
  std::stack<VACUUM_DATA_ENTRY> vacuum_block_data_buffer_stack;
  /* *INDENT-ON* */

  /* we don't reset data.oldest_visible_mvccid between blocks. we need to maintain ordered oldest_visible_mvccid's, and
   * if a block + 1 MVCCID is smaller than all MVCCID's in block, then it must have been active (and probably suspended)
   * while block was logged. therefore, we must keep it. */
  data.oldest_visible_mvccid = MVCCID_NULL;
  while (crt_blockid > vacuum_Data.get_last_blockid ())
    {
      /* Stop recovering this block when previous block is reached. */
      stop_at_pageid = VACUUM_FIRST_LOG_PAGEID_IN_BLOCK (crt_blockid) - 1;
      /* Initialize this block data. */
      data.blockid = crt_blockid;
      LSA_COPY (&data.start_lsa, &log_lsa);
      /* inherit data.oldest_visible_mvccid */
      data.newest_mvccid = MVCCID_NULL;
      /* Loop through MVCC op log records in this block. */
      while (log_lsa.pageid > stop_at_pageid)
	{
	  if (log_page_p->hdr.logical_pageid != log_lsa.pageid)
	    {
	      error_code = logpb_fetch_page (thread_p, &log_lsa, LOG_CS_SAFE_READER, log_page_p);
	      if (error_code != NO_ERROR)
		{
		  ASSERT_ERROR ();
		  logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "vacuum_recover_lost_block_data");
		  return ER_FAILED;
		}
	    }
	  /* Process this log record. */
	  error_code =
	    vacuum_process_log_record (thread_p, NULL, &log_lsa, log_page_p, &dummy_log_data, &mvccid, NULL, NULL,
				       &vacuum_info, NULL, true);
	  if (error_code != NO_ERROR)
	    {
	      logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "vacuum_recover_lost_block_data");
	      return error_code;
	    }
	  /* Update oldest/newest MVCCID. */
	  if (data.oldest_visible_mvccid == MVCCID_NULL || MVCC_ID_PRECEDES (mvccid, data.oldest_visible_mvccid))
	    {
	      data.oldest_visible_mvccid = mvccid;
	    }
	  if (data.newest_mvccid == MVCCID_NULL || MVCC_ID_PRECEDES (data.newest_mvccid, mvccid))
	    {
	      data.newest_mvccid = mvccid;
	    }
	  LSA_COPY (&log_lsa, &vacuum_info.prev_mvcc_op_log_lsa);
	}

      if (data.blockid == vacuum_get_log_blockid (log_Gl.prior_info.prior_lsa.pageid))
	{
	  log_Gl.hdr.oldest_visible_mvccid = data.oldest_visible_mvccid;
	  log_Gl.hdr.newest_block_mvccid = data.newest_mvccid;
	  log_Gl.hdr.does_block_need_vacuum = true;
	  log_Gl.hdr.mvcc_op_log_lsa = mvcc_op_log_lsa;

	  vacuum_er_log (VACUUM_ER_LOG_VACUUM_DATA | VACUUM_ER_LOG_RECOVERY,
			 "Restore log global cached info: \n\t mvcc_op_log_lsa = %lld|%d \n"
			 "\t oldest_visible_mvccid = %llu \n\t newest_block_mvccid = %llu ",
			 LSA_AS_ARGS (&log_Gl.hdr.mvcc_op_log_lsa),
			 (unsigned long long int) log_Gl.hdr.oldest_visible_mvccid,
			 (unsigned long long int) log_Gl.hdr.newest_block_mvccid);
	}
      else
	{
	  vacuum_block_data_buffer_stack.push (data);
	}

      crt_blockid = vacuum_get_log_blockid (log_lsa.pageid);
    }

  /* Produce recovered blocks. */
  while (!vacuum_block_data_buffer_stack.empty ())
    {
      vacuum_Block_data_buffer->produce (vacuum_block_data_buffer_stack.top ());
      vacuum_block_data_buffer_stack.pop ();
    }

  /* Consume recovered blocks. */
  thread_type tt;
  vacuum_convert_thread_to_master (thread_p, tt);
  error_code = vacuum_consume_buffer_log_blocks (thread_p);
  if (error_code != NO_ERROR)
    {
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "vacuum_recover_lost_block_data");
    }
  vacuum_restore_thread (thread_p, tt);

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
  if (prm_get_bool_value (PRM_ID_DISABLE_VACUUM) || pageid == NULL_PAGEID)
    {
      return VACUUM_NULL_LOG_BLOCKID;
    }

  assert (vacuum_Data.log_block_npages != 0);

  return pageid / vacuum_Data.log_block_npages;
}

/*
 * vacuum_min_log_pageid_to_keep () - Get the minimum log pageid required to execute vacuum.
 *				      See vacuum_update_keep_from_log_pageid.
 *
 * return	 : LOG Page identifier for first log page that should be processed by vacuum.
 * thread_p (in) : Thread entry.
 */
LOG_PAGEID
vacuum_min_log_pageid_to_keep (THREAD_ENTRY * thread_p)
{
  /* Return first pageid from first block in vacuum data table */
  if (prm_get_bool_value (PRM_ID_DISABLE_VACUUM))
    {
      /* this is for debug, suppress log archive purging. */
      return 0;
    }
#if defined (SA_MODE)
  if (vacuum_Data.is_vacuum_complete)
    {
      /* no log archives are needed for vacuum any longer. */
      return NULL_PAGEID;
    }
#endif /* defined (SA_MODE) */
  return vacuum_Data.keep_from_log_pageid;
}

/*
 * vacuum_is_safe_to_remove_archives () - Is safe to remove archives? Not until keep_from_log_pageid has been updated
 *                                        at least once.
 *
 * return    : is safe?
 */
bool
vacuum_is_safe_to_remove_archives (void)
{
  return vacuum_Data.is_archive_removal_safe;
}

/*
 * vacuum_rv_redo_start_job () - Redo start vacuum job.
 *
 * return	 : Error code.
 * thread_p (in) : Thread entry.
 * rcv (in)	 : Recovery data.
 */
int
vacuum_rv_redo_start_job (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  VACUUM_DATA_PAGE *data_page = (VACUUM_DATA_PAGE *) rcv->pgptr;

  assert (data_page != NULL);
  assert (rcv->offset >= 0 && rcv->offset < vacuum_Data.page_data_max_count);

  /* Start job is marked by in progress flag in blockid. */
  data_page->data[rcv->offset].set_job_in_progress ();

  pgbuf_set_dirty (thread_p, rcv->pgptr, DONT_FREE);

  return NO_ERROR;
}

/*
 * vacuum_update_keep_from_log_pageid () - Update vacuum_Data.keep_from_log_pageid.
 *
 * return	 : Void.
 * thread_p (in) : Thread entry.
 */
static void
vacuum_update_keep_from_log_pageid (THREAD_ENTRY * thread_p)
{
  /* vacuum_Data.keep_from_log_pageid should keep first page in first block not yet vacuumed, so that archive purger
   * does not remove log required for vacuum.
   * If vacuum data is empty, then all blocks until (and including) vacuum_Data.last_blockid have been
   * vacuumed, and first page belonging to next block must be preserved (this is most likely in the active area of the
   * log, but not always).
   * If vacuum data is not empty, then we need to preserve the log starting with the first page of first unvacuumed
   * block.
   */
  if (vacuum_is_empty ())
    {
      // keep starting with next after last_blockid ()
      vacuum_Data.keep_from_log_pageid = VACUUM_FIRST_LOG_PAGEID_IN_BLOCK (vacuum_Data.get_last_blockid () + 1);
    }
  else
    {
      vacuum_Data.keep_from_log_pageid = VACUUM_FIRST_LOG_PAGEID_IN_BLOCK (vacuum_Data.get_first_blockid ());
    }

  vacuum_er_log (VACUUM_ER_LOG_VACUUM_DATA,
		 "Update keep_from_log_pageid to %lld ", (long long int) vacuum_Data.keep_from_log_pageid);

  if (!vacuum_Data.is_archive_removal_safe)
    {
      /* remove archives that have been blocked up to this point. */
      vacuum_Data.is_archive_removal_safe = true;
    }
}

/*
 * vacuum_compare_dropped_files () - Compare two file identifiers.
 *
 * return    : Positive if the first argument is bigger, negative if it is smaller and 0 if arguments are equal.
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
 */
static int
vacuum_add_dropped_file (THREAD_ENTRY * thread_p, VFID * vfid, MVCCID mvccid)
{
  MVCCID save_mvccid = MVCCID_NULL;
  VPID vpid = VPID_INITIALIZER, prev_vpid = VPID_INITIALIZER;
  int page_count = 0, mem_size = 0;
  VACUUM_DROPPED_FILES_PAGE *page = NULL, *new_page = NULL;
  INT16 position = -1;
  LOG_DATA_ADDR addr = LOG_DATA_ADDR_INITIALIZER;
  LOG_TDES *tdes = LOG_FIND_CURRENT_TDES (thread_p);
  bool found = false;
  PAGE_TYPE ptype = PAGE_DROPPED_FILES;

#if !defined (NDEBUG)
  VACUUM_TRACK_DROPPED_FILES *track_page = NULL;
  VACUUM_TRACK_DROPPED_FILES *new_track_page = NULL;
#endif

  int error_code = NO_ERROR;

  assert (tdes != NULL);

  if (!vacuum_Dropped_files_loaded)
    {
      /* Normally, dropped files are loaded after recovery, in order to provide a consistent state of its pages.
       * Actually, the consistent state should be reached after all run postpone and compensate undo records are
       * applied. However, this may be called from log_recovery_finish_all_postpone or from log_recovery_undo. Because
       * there is no certain code that is executed after applying redo and before calling these function, the dropped
       * files are loaded when needed. */

      /* This must be recover, otherwise the files should have been loaded. */
      assert (!LOG_ISRESTARTED ());

      if (vacuum_load_dropped_files_from_disk (thread_p) != NO_ERROR)
	{
	  vacuum_er_log_error (VACUUM_ER_LOG_DROPPED_FILES | VACUUM_ER_LOG_RECOVERY, "%s",
			       "Failed to load dropped files during recovery!");

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
      page = vacuum_fix_dropped_entries_page (thread_p, &vpid, PGBUF_LATCH_WRITE);
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

      /* binary search */
      position =
	util_bsearch (vfid, page->dropped_files, page_count, sizeof (VACUUM_DROPPED_FILE), vacuum_compare_dropped_files,
		      &found);

      if (found)
	{
	  /* Same entry was already dropped, replace previous MVCCID */
	  VACUUM_DROPPED_FILE undo_data;

	  /* Replace MVCCID */
	  undo_data = page->dropped_files[position];
	  save_mvccid = page->dropped_files[position].mvccid;
	  page->dropped_files[position].mvccid = mvccid;

	  assert_release (MVCC_ID_FOLLOW_OR_EQUAL (mvccid, save_mvccid));

	  /* log changes */
	  addr.pgptr = (PAGE_PTR) page;
	  addr.offset = position;
	  log_append_undoredo_data (thread_p, RVVAC_DROPPED_FILE_REPLACE, &addr, sizeof (VACUUM_DROPPED_FILE),
				    sizeof (VACUUM_DROPPED_FILE), &undo_data, &page->dropped_files[position]);

#if !defined (NDEBUG)
	  if (track_page != NULL)
	    {
	      memcpy (&track_page->dropped_data_page, page, DB_PAGESIZE);
	    }
#endif
	  vacuum_er_log (VACUUM_ER_LOG_DROPPED_FILES,
			 "add dropped file: found duplicate vfid %d|%d at position=%d, "
			 "replace mvccid=%llu with mvccid=%llu. Page is %d|%d with lsa %lld|%d."
			 "Page count=%d, global count=%d", VFID_AS_ARGS (&page->dropped_files[position].vfid), position,
			 (unsigned long long int) save_mvccid,
			 (unsigned long long int) page->dropped_files[position].mvccid,
			 PGBUF_PAGE_STATE_ARGS ((PAGE_PTR) page), page->n_dropped_files, vacuum_Dropped_files_count);

	  vacuum_set_dirty_dropped_entries_page (thread_p, page, FREE);

	  return NO_ERROR;
	}

      /* not a duplicate. can we add? */
      if (VACUUM_DROPPED_FILES_PAGE_CAPACITY <= page_count)
	{
	  assert (VACUUM_DROPPED_FILES_PAGE_CAPACITY == page_count);

	  /* No room left for new entries, try next page */

#if !defined (NDEBUG)
	  if (track_page != NULL && !VPID_ISNULL (&vpid))
	    {
	      /* Don't advance from last track page. A new page will be added and we need to set a link between
	       * last track page and new track page. */
	      track_page = track_page->next_tracked_page;
	    }
#endif
	  continue;
	}

      /* add to position to keep the order */
      if (page_count > position)
	{
	  mem_size = (page_count - position) * sizeof (VACUUM_DROPPED_FILE);
	  memmove (&page->dropped_files[position + 1], &page->dropped_files[position], mem_size);
	}

      /* Increment page count */
      page->n_dropped_files++;

      /* Increment total count */
      ATOMIC_INC_32 (&vacuum_Dropped_files_count, 1);

      VFID_COPY (&page->dropped_files[position].vfid, vfid);
      page->dropped_files[position].mvccid = mvccid;

      addr.pgptr = (PAGE_PTR) page;
      addr.offset = position;
      log_append_undoredo_data (thread_p, RVVAC_DROPPED_FILE_ADD, &addr, 0, sizeof (VACUUM_DROPPED_FILE), NULL,
				&page->dropped_files[position]);

#if !defined (NDEBUG)
      if (track_page != NULL)
	{
	  memcpy (&track_page->dropped_data_page, page, DB_PAGESIZE);
	}
#endif

      vacuum_er_log (VACUUM_ER_LOG_DROPPED_FILES,
		     "added new dropped file %d|%d and mvccid=%llu at position=%d. "
		     "Page is %d|%d with lsa %lld|%d. Page count=%d, global count=%d",
		     VFID_AS_ARGS (&page->dropped_files[position].vfid),
		     (unsigned long long int) page->dropped_files[position].mvccid, position,
		     PGBUF_PAGE_STATE_ARGS ((PAGE_PTR) page), page->n_dropped_files, vacuum_Dropped_files_count);

      vacuum_set_dirty_dropped_entries_page (thread_p, page, FREE);

      return NO_ERROR;
    }

  /* The entry couldn't fit in any of the current pages. */
  /* Allocate a new page */

  /* Last page must be fixed */
  assert (page != NULL);

  /* Extend file */
  error_code = file_alloc (thread_p, &vacuum_Dropped_files_vfid, file_init_page_type, &ptype, &vpid,
			   (PAGE_PTR *) (&new_page));
  if (error_code != NO_ERROR)
    {
      assert (false);
      vacuum_unfix_dropped_entries_page (thread_p, page);
      return ER_FAILED;
    }
  if (new_page == NULL)
    {
      assert_release (false);
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
	  new_track_page = (VACUUM_TRACK_DROPPED_FILES *) malloc (VACUUM_TRACK_DROPPED_FILES_SIZE);
	  if (new_track_page == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, VACUUM_TRACK_DROPPED_FILES_SIZE);
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

  pgbuf_set_page_ptype (thread_p, (PAGE_PTR) new_page, PAGE_DROPPED_FILES);
  log_append_redo_data2 (thread_p, RVPGBUF_NEW_PAGE, NULL, (PAGE_PTR) new_page, (PGLENGTH) PAGE_DROPPED_FILES,
			 sizeof (VACUUM_DROPPED_FILES_PAGE), new_page);

  vacuum_er_log (VACUUM_ER_LOG_DROPPED_FILES,
		 "added new dropped file %d|%d and mvccid=%llu to at position=%d. "
		 "Page is %d|%d with lsa %lld|%d. Page count=%d, global count=%d",
		 VFID_AS_ARGS (&new_page->dropped_files[0].vfid),
		 (unsigned long long int) new_page->dropped_files[0].mvccid, 0,
		 PGBUF_PAGE_STATE_ARGS ((PAGE_PTR) new_page), new_page->n_dropped_files, vacuum_Dropped_files_count);

  /* Unfix new page */
  vacuum_set_dirty_dropped_entries_page (thread_p, new_page, FREE);

  /* Save a link to the new page in last page */
  vacuum_dropped_files_set_next_page (thread_p, page, &vpid);
#if !defined(NDEBUG)
  if (track_page != NULL)
    {
      VPID_COPY (&track_page->dropped_data_page.next_page, &vpid);
    }
#endif

  /* unfix last page */
  vacuum_unfix_dropped_entries_page (thread_p, page);
  return NO_ERROR;
}

/*
 * vacuum_log_add_dropped_file () - Append postpone/undo log for notifying vacuum of a file being dropped. Postpone
 *				    is added when a class or index is dropped and undo when a class or index is created.
 *
 * return	 : Void.
 * thread_p (in) : Thread entry.
 * vfid (in)	 : Dropped file identifier.
 * class_oid(in) : class OID
 */
void
vacuum_log_add_dropped_file (THREAD_ENTRY * thread_p, const VFID * vfid, const OID * class_oid, bool pospone_or_undo)
{
  LOG_DATA_ADDR addr;
  VACUUM_DROPPED_FILES_RCV_DATA rcv_data;

  vacuum_er_log (VACUUM_ER_LOG_DROPPED_FILES, "Append %s log from dropped file %d|%d.",
		 pospone_or_undo ? "postpone" : "undo", vfid->volid, vfid->fileid);

  /* Initialize recovery data */
  VFID_COPY (&rcv_data.vfid, vfid);
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
      log_append_postpone (thread_p, RVVAC_NOTIFY_DROPPED_FILE, &addr, sizeof (rcv_data), &rcv_data);
    }
  else
    {
      log_append_undo_data (thread_p, RVVAC_NOTIFY_DROPPED_FILE, &addr, sizeof (rcv_data), &rcv_data);
    }
}

/*
 * vacuum_rv_redo_add_dropped_file () - Redo recovery used for adding dropped files.
 *
 * return	 : Error code.
 * thread_p (in) : Thread entry.
 * rcv (in)	 : Recovery data.
 */
int
vacuum_rv_redo_add_dropped_file (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  VACUUM_DROPPED_FILES_PAGE *page = NULL;
  INT16 position = rcv->offset;
  int mem_size;
  VACUUM_DROPPED_FILE *dropped_file;

  assert (rcv->length == sizeof (VACUUM_DROPPED_FILE));
  dropped_file = ((VACUUM_DROPPED_FILE *) rcv->data);

  assert_release (!VFID_ISNULL (&dropped_file->vfid));
  assert_release (MVCCID_IS_VALID (dropped_file->mvccid));

  page = (VACUUM_DROPPED_FILES_PAGE *) rcv->pgptr;

  if (position > page->n_dropped_files)
    {
      /* Error! */
      vacuum_er_log_error (VACUUM_ER_LOG_DROPPED_FILES | VACUUM_ER_LOG_RECOVERY,
			   "Dropped files recovery error: Invalid position %d (only %d entries in page) while "
			   "inserting new entry vfid=%d|%d mvccid=%llu. Page is %d|%d at lsa %lld|%d. ",
			   position, page->n_dropped_files, VFID_AS_ARGS (&dropped_file->vfid),
			   (unsigned long long) dropped_file->mvccid, PGBUF_PAGE_STATE_ARGS (rcv->pgptr));

      assert_release (false);
      return ER_FAILED;
    }

  if (position < page->n_dropped_files)
    {
      /* Make room for new record */
      mem_size = (page->n_dropped_files - position) * sizeof (VACUUM_DROPPED_FILE);
      memmove (&page->dropped_files[position + 1], &page->dropped_files[position], mem_size);
    }

  /* Copy new dropped file */
  VFID_COPY (&page->dropped_files[position].vfid, &dropped_file->vfid);
  page->dropped_files[position].mvccid = dropped_file->mvccid;

  /* Increment number of files */
  page->n_dropped_files++;

  vacuum_er_log (VACUUM_ER_LOG_DROPPED_FILES | VACUUM_ER_LOG_RECOVERY,
		 "Dropped files redo recovery, insert new entry "
		 "vfid=%d|%d, mvccid=%llu at position %d. Page is %d|%d at lsa %lld|%d.",
		 VFID_AS_ARGS (&dropped_file->vfid), (unsigned long long) dropped_file->mvccid, position,
		 PGBUF_PAGE_STATE_ARGS (rcv->pgptr));

  /* Make sure the mvcc_next_id is also updated, since this is the marker used by dropped files. */
  if (!MVCC_ID_PRECEDES (dropped_file->mvccid, log_Gl.hdr.mvcc_next_id))
    {
      log_Gl.hdr.mvcc_next_id = dropped_file->mvccid;
      MVCCID_FORWARD (log_Gl.hdr.mvcc_next_id);
    }

  /* Page was modified, so set it dirty */
  pgbuf_set_dirty (thread_p, rcv->pgptr, DONT_FREE);
  return NO_ERROR;
}

/*
 * vacuum_rv_undo_add_dropped_file () - Undo recovery used for adding dropped files.
 *
 * return	 : Error code.
 * thread_p (in) : Thread entry.
 * rcv (in)	 : Recovery data.
 */
int
vacuum_rv_undo_add_dropped_file (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  VACUUM_DROPPED_FILES_PAGE *page = NULL;
  INT16 position = rcv->offset;
  int mem_size;

  page = (VACUUM_DROPPED_FILES_PAGE *) rcv->pgptr;

  if (position >= page->n_dropped_files)
    {
      assert_release (false);
      return ER_FAILED;
    }

  mem_size = (page->n_dropped_files - 1 - position) * sizeof (VACUUM_DROPPED_FILE);
  if (mem_size > 0)
    {
      memmove (&page->dropped_files[position], &page->dropped_files[position + 1], mem_size);
    }
  page->n_dropped_files--;

  /* Page was modified, so set it dirty */
  pgbuf_set_dirty (thread_p, rcv->pgptr, DONT_FREE);
  return NO_ERROR;
}

/*
 * vacuum_rv_replace_dropped_file () - replace dropped file for recovery
 *
 * return        : error code
 * thread_p (in) : thread entry
 * rcv (in)      : recovery data
 */
int
vacuum_rv_replace_dropped_file (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  VACUUM_DROPPED_FILES_PAGE *page = NULL;
  INT16 position = rcv->offset;
  VACUUM_DROPPED_FILE *dropped_file;

  assert (rcv->length == sizeof (VACUUM_DROPPED_FILE));
  dropped_file = (VACUUM_DROPPED_FILE *) rcv->data;

  page = (VACUUM_DROPPED_FILES_PAGE *) rcv->pgptr;

  /* Should be the same VFID */
  if (position >= page->n_dropped_files)
    {
      /* Error! */
      vacuum_er_log_error (VACUUM_ER_LOG_DROPPED_FILES | VACUUM_ER_LOG_RECOVERY,
			   "Dropped files recovery error: Invalid position %d (only %d entries in page) while "
			   "replacing old entry with vfid=%d|%d mvccid=%llu. Page is %d|%d at lsa %lld|%d. ",
			   position, page->n_dropped_files, VFID_AS_ARGS (&dropped_file->vfid),
			   (unsigned long long) dropped_file->mvccid, PGBUF_PAGE_STATE_ARGS (rcv->pgptr));

      assert_release (false);
      return ER_FAILED;
    }

  if (!VFID_EQ (&dropped_file->vfid, &page->dropped_files[position].vfid))
    {
      /* Error! */
      vacuum_er_log_error (VACUUM_ER_LOG_DROPPED_FILES | VACUUM_ER_LOG_RECOVERY,
			   "Dropped files recovery error: expected to "
			   "find vfid %d|%d at position %d and found %d|%d with MVCCID=%d. "
			   "Page is %d|%d at lsa %lld|%d. ", VFID_AS_ARGS (&dropped_file->vfid), position,
			   VFID_AS_ARGS (&page->dropped_files[position].vfid),
			   (unsigned long long) page->dropped_files[position].mvccid,
			   PGBUF_PAGE_STATE_ARGS (rcv->pgptr));

      assert_release (false);
      return ER_FAILED;
    }

  vacuum_er_log (VACUUM_ER_LOG_DROPPED_FILES | VACUUM_ER_LOG_RECOVERY,
		 "Dropped files redo recovery, replace MVCCID for"
		 " file %d|%d with %llu (position=%d). Page is %d|%d at lsa %lld|%d.",
		 VFID_AS_ARGS (&dropped_file->vfid), (unsigned long long) dropped_file->mvccid, position,
		 PGBUF_PAGE_STATE_ARGS (rcv->pgptr));
  page->dropped_files[position].mvccid = dropped_file->mvccid;

  /* Make sure the mvcc_next_id is also updated, since this is the marker used by dropped files. */
  if (!MVCC_ID_PRECEDES (dropped_file->mvccid, log_Gl.hdr.mvcc_next_id))
    {
      log_Gl.hdr.mvcc_next_id = dropped_file->mvccid;
      MVCCID_FORWARD (log_Gl.hdr.mvcc_next_id);
    }

  /* Page was modified, so set it dirty */
  pgbuf_set_dirty (thread_p, rcv->pgptr, DONT_FREE);
  return NO_ERROR;
}

/*
 * vacuum_notify_all_workers_dropped_file () - notify all vacuum workers that given file was dropped
 *
 * vfid_dropped (in) : VFID of dropped file
 * mvccid (in)       : MVCCID marker for dropped file
 */
static void
vacuum_notify_all_workers_dropped_file (const VFID & vfid_dropped, MVCCID mvccid)
{
#if defined (SERVER_MODE)
  if (!LOG_ISRESTARTED ())
    {
      // workers are not running during recovery
      return;
    }

  INT32 my_version, workers_min_version;

  /* Before notifying vacuum workers there is one last thing we have to do. Running workers must also be notified of
   * the VFID being dropped to cleanup their collected heap object arrays. Since must done one file at a time, so a
   * mutex is used for protection, in case there are several transactions doing file drops. */
  pthread_mutex_lock (&vacuum_Dropped_files_mutex);
  assert (VFID_ISNULL (&vacuum_Last_dropped_vfid));
  VFID_COPY (&vacuum_Last_dropped_vfid, &vfid_dropped);

  /* Increment dropped files version and save a version for current change. It is not important to keep the version
   * synchronized with the changes. It is only used to make sure that all workers have seen current change. */
  my_version = ++vacuum_Dropped_files_version;

  vacuum_er_log (VACUUM_ER_LOG_DROPPED_FILES,
		 "Added dropped file - vfid=%d|%d, mvccid=%llu - "
		 "Wait for all workers to see my_version=%d", VFID_AS_ARGS (&vfid_dropped), mvccid, my_version);

  /* Wait until all workers have been notified of this change */
  for (workers_min_version = vacuum_get_worker_min_dropped_files_version ();
       workers_min_version != -1 && workers_min_version < my_version;
       workers_min_version = vacuum_get_worker_min_dropped_files_version ())
    {
      vacuum_er_log (VACUUM_ER_LOG_DROPPED_FILES,
		     "not all workers saw my changes, workers min version=%d. Sleep and retry.", workers_min_version);

      thread_sleep (1);
    }

  vacuum_er_log (VACUUM_ER_LOG_DROPPED_FILES, "All workers have been notified, min_version=%d", workers_min_version);

  VFID_SET_NULL (&vacuum_Last_dropped_vfid);
  pthread_mutex_unlock (&vacuum_Dropped_files_mutex);
#endif // SERVER_MODE
}

/*
 * vacuum_rv_notify_dropped_file () - Add drop file used in recovery phase. Can be used in two ways: at run postpone phase
 *				   for dropped heap files and indexes (if postpone_ref_lsa in not null); or at undo
 *				   phase for created heap files and indexes.
 *
 * return		: Error code.
 * thread_p (in)	: Thread entry.
 * rcv (in)		: Recovery data.
 * pospone_ref_lsa (in) : Reference LSA for running postpone. NULL if this is
 *			  an undo for created heap files and indexes.
 */
int
vacuum_rv_notify_dropped_file (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  int error = NO_ERROR;
  OID *class_oid;
  MVCCID mvccid;
  VACUUM_DROPPED_FILES_RCV_DATA *rcv_data;

  /* Copy VFID from current log recovery data but set MVCCID at this point. We will use the log_Gl.hdr.mvcc_next_id as
   * borderline to distinguish this file from newer files. 1. All changes on this file must be done by transaction that
   * have already committed which means their MVCCID will be less than current log_Gl.hdr.mvcc_next_id. 2. All changes
   * on a new file that reused VFID must be done by transaction that start after this call, which means their MVCCID's
   * will be at least equal to current log_Gl.hdr.mvcc_next_id. */

  mvccid = ATOMIC_LOAD_64 (&log_Gl.hdr.mvcc_next_id);

  /* Add dropped file to current list */
  rcv_data = (VACUUM_DROPPED_FILES_RCV_DATA *) rcv->data;
  error = vacuum_add_dropped_file (thread_p, &rcv_data->vfid, mvccid);
  if (error != NO_ERROR)
    {
      return error;
    }

  // make sure vacuum workers will not access dropped file
  vacuum_notify_all_workers_dropped_file (rcv_data->vfid, mvccid);

  /* vacuum is notified of the file drop, it is safe to remove from cache */
  class_oid = &rcv_data->class_oid;
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
 * NOTE: All entries with an MVCCID older than vacuum_Data->oldest_unvacuumed_mvccid are removed.
 *	 All records belonging to these entries must be either vacuumed or skipped after drop.
 */
static int
vacuum_cleanup_dropped_files (THREAD_ENTRY * thread_p)
{
  VPID vpid = VPID_INITIALIZER;
  VACUUM_DROPPED_FILES_PAGE *page = NULL;
  int page_count = 0, mem_size = 0;
  VPID last_page_vpid = VPID_INITIALIZER, last_non_empty_page_vpid = VPID_INITIALIZER;
  INT16 removed_entries[VACUUM_DROPPED_FILES_MAX_PAGE_CAPACITY];
  INT16 n_removed_entries = 0, i;
#if !defined (NDEBUG)
  VACUUM_TRACK_DROPPED_FILES *track_page = (VACUUM_TRACK_DROPPED_FILES *) vacuum_Track_dropped_files;
#endif

  vacuum_er_log (VACUUM_ER_LOG_DROPPED_FILES, "%s", "Start cleanup dropped files.");

  if (!LOG_ISRESTARTED ())
    {
      /* Skip cleanup during recovery */
      vacuum_er_log (VACUUM_ER_LOG_RECOVERY | VACUUM_ER_LOG_DROPPED_FILES, "%s", "Skip cleanup during recovery.");
      return NO_ERROR;
    }

  assert_release (!VFID_ISNULL (&vacuum_Dropped_files_vfid));
  assert_release (!VPID_ISNULL (&vacuum_Dropped_files_vpid));

  if (vacuum_Dropped_files_count == 0)
    {
      /* Nothing to clean */
      vacuum_er_log (VACUUM_ER_LOG_DROPPED_FILES, "%s", "Cleanup skipped, no current entries.");
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
      page = vacuum_fix_dropped_entries_page (thread_p, &vpid, PGBUF_LATCH_WRITE);
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
	  if (MVCC_ID_PRECEDES (page->dropped_files[i].mvccid, vacuum_Data.oldest_unvacuumed_mvccid))
	    {
	      /* Remove entry */
	      removed_entries[n_removed_entries++] = i;
	      if (i < page_count - 1)
		{
		  mem_size = (page_count - i - 1) * sizeof (VACUUM_DROPPED_FILE);
		  memmove (&page->dropped_files[i], &page->dropped_files[i + 1], mem_size);
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
	  vacuum_log_cleanup_dropped_files (thread_p, (PAGE_PTR) page, removed_entries, n_removed_entries);

	  vacuum_er_log (VACUUM_ER_LOG_DROPPED_FILES,
			 "cleanup dropped files. Page is %d|%d with lsa %lld|%d. "
			 "Page count=%d, global count=%d", PGBUF_PAGE_STATE_ARGS ((PAGE_PTR) page),
			 page->n_dropped_files, vacuum_Dropped_files_count);

	  /* todo: new pages are allocated but old pages are never deallocated. it looks like they are leaked. */

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

  if (!VPID_ISNULL (&last_non_empty_page_vpid) && !VPID_EQ (&last_non_empty_page_vpid, &last_page_vpid))
    {
      /* Update next page link in the last non-empty page to NULL, to avoid fixing empty pages in the future. */
      vacuum_er_log (VACUUM_ER_LOG_DROPPED_FILES,
		     "Cleanup dropped files must remove pages to the of page %d|%d... Cut off link.",
		     last_non_empty_page_vpid.volid, last_non_empty_page_vpid.pageid);

      page = vacuum_fix_dropped_entries_page (thread_p, &last_non_empty_page_vpid, PGBUF_LATCH_WRITE);
      if (page == NULL)
	{
	  assert (false);
	  return ER_FAILED;
	}

      vacuum_dropped_files_set_next_page (thread_p, page, &page->next_page);
      vacuum_unfix_dropped_entries_page (thread_p, page);

      /* todo: tracker? */
    }

  vacuum_er_log (VACUUM_ER_LOG_DROPPED_FILES, "%s", "Finished cleanup dropped files.");
  return NO_ERROR;
}

/*
 * vacuum_is_file_dropped () - Check whether file is considered dropped.
 *
 * return	        : error code.
 * thread_p (in)        : Thread entry.
 * is_file_dropped(out) : True if file is considered dropped. False, otherwise.
 * vfid (in)	        : File identifier.
 * mvccid (in)	        : MVCCID.
 */
int
vacuum_is_file_dropped (THREAD_ENTRY * thread_p, bool * is_file_dropped, VFID * vfid, MVCCID mvccid)
{
  if (prm_get_bool_value (PRM_ID_DISABLE_VACUUM))
    {
      *is_file_dropped = false;
      return NO_ERROR;
    }

  return vacuum_find_dropped_file (thread_p, is_file_dropped, vfid, mvccid);
}

/*
 * vacuum_find_dropped_file () - Find the dropped file and check whether the given MVCCID is older than or equal to the
 *				 MVCCID of dropped file. Used by vacuum to detect records that belong to dropped files.
 *
 * return	        : error code.
 * thread_p (in)        : Thread entry.
 * is_file_dropped(out) : True if file is considered dropped. False, otherwise.
 * vfid (in)	        : File identifier.
 * mvccid (in)	        : MVCCID of checked record.
 */
static int
vacuum_find_dropped_file (THREAD_ENTRY * thread_p, bool * is_file_dropped, VFID * vfid, MVCCID mvccid)
{
  VACUUM_DROPPED_FILES_PAGE *page = NULL;
  VACUUM_DROPPED_FILE *dropped_file = NULL;
  VPID vpid;
  INT16 page_count;
  int error;

  if (vacuum_Dropped_files_count == 0)
    {
      /* No dropped files */
      *is_file_dropped = false;
      return NO_ERROR;
    }

  assert_release (!VPID_ISNULL (&vacuum_Dropped_files_vpid));

  /* Search for dropped file in all pages. */
  VPID_COPY (&vpid, &vacuum_Dropped_files_vpid);

  while (!VPID_ISNULL (&vpid))
    {
      /* Fix current page */
      page = vacuum_fix_dropped_entries_page (thread_p, &vpid, PGBUF_LATCH_READ);
      if (page == NULL)
	{
	  *is_file_dropped = false;	/* actually unknown but unimportant */

	  assert (!VACUUM_IS_THREAD_VACUUM_MASTER (thread_p));
	  ASSERT_ERROR_AND_SET (error);
	  assert (error == ER_INTERRUPTED);

	  if (VACUUM_IS_THREAD_VACUUM_WORKER (thread_p))
	    {
	      assert (thread_p->shutdown);
	    }
	  return error;
	}

      /* dropped files page are never boosted. mark them that vacuum will fix to at least postpone victimization */
      pgbuf_notify_vacuum_follows (thread_p, (PAGE_PTR) page);

      /* Copy next page VPID */
      VPID_COPY (&vpid, &page->next_page);
      page_count = page->n_dropped_files;

      /* Use compare VFID to find a matching entry */
      dropped_file =
	(VACUUM_DROPPED_FILE *) bsearch (vfid, page->dropped_files, page_count, sizeof (VACUUM_DROPPED_FILE),
					 vacuum_compare_dropped_files);
      if (dropped_file != NULL)
	{
	  /* Found matching entry. Compare the given MVCCID with the MVCCID of dropped file. */
	  if (MVCC_ID_PRECEDES (mvccid, dropped_file->mvccid))
	    {
	      /* The record must belong to the dropped file */
	      vacuum_er_log (VACUUM_ER_LOG_DROPPED_FILES,
			     "found dropped file: vfid=%d|%d mvccid=%llu in page %d|%d. "
			     "Entry at position %d, vfid=%d|%d mvccid=%llu. The vacuumed file is dropped.",
			     VFID_AS_ARGS (vfid), (unsigned long long int) mvccid,
			     PGBUF_PAGE_VPID_AS_ARGS ((PAGE_PTR) page), dropped_file - page->dropped_files,
			     VFID_AS_ARGS (&dropped_file->vfid), (unsigned long long int) dropped_file->mvccid);

	      vacuum_unfix_dropped_entries_page (thread_p, page);

	      *is_file_dropped = true;
	      return NO_ERROR;
	    }
	  else
	    {
	      /* The record belongs to an entry with the same identifier, but is newer. */
	      vacuum_er_log (VACUUM_ER_LOG_DROPPED_FILES,
			     "found dropped file: vfid=%d|%d mvccid=%llu in page %d|%d. "
			     "Entry at position %d, vfid=%d|%d mvccid=%llu. The vacuumed file is newer.",
			     VFID_AS_ARGS (vfid), (unsigned long long int) mvccid,
			     PGBUF_PAGE_VPID_AS_ARGS ((PAGE_PTR) page), dropped_file - page->dropped_files,
			     VFID_AS_ARGS (&dropped_file->vfid), (unsigned long long int) dropped_file->mvccid);

	      vacuum_unfix_dropped_entries_page (thread_p, page);

	      *is_file_dropped = false;
	      return NO_ERROR;
	    }
	}

      /* Do not log this unless you think it is useful. It spams the log file. */
      vacuum_er_log (VACUUM_ER_LOG_NONE,
		     "didn't find dropped file: vfid=%d|%d mvccid=%llu in page (%d, %d).", VFID_AS_ARGS (vfid),
		     (unsigned long long int) mvccid, PGBUF_PAGE_VPID_AS_ARGS ((PAGE_PTR) page));

      vacuum_unfix_dropped_entries_page (thread_p, page);
    }

  /* Entry not found */
  *is_file_dropped = false;
  return NO_ERROR;
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
 * NOTE: Consider not logging cleanup. Cleanup can be done at database restart.
 */
static void
vacuum_log_cleanup_dropped_files (THREAD_ENTRY * thread_p, PAGE_PTR page_p, INT16 * indexes, INT16 n_indexes)
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

  log_append_redo_crumbs (thread_p, RVVAC_DROPPED_FILE_CLEANUP, &addr, n_redo_crumbs, redo_crumbs);
}

/*
 * vacuum_rv_redo_cleanup_dropped_files () - Recover dropped files cleanup.
 *
 * return	 : Error code.
 * thread_p (in) : Thread entry,
 * rcv (in)	 : Recovery data.
 *
 * NOTE: Consider not logging cleanup. Cleanup can be done at database restart.
 */
int
vacuum_rv_redo_cleanup_dropped_files (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  int offset = 0, mem_size;
  VACUUM_DROPPED_FILES_PAGE *page = (VACUUM_DROPPED_FILES_PAGE *) rcv->pgptr;
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
		     "Recovery of dropped classes: remove file %d|%d, mvccid=%llu at position %d.",
		     (int) page->dropped_files[indexes[i]].vfid.volid,
		     (int) page->dropped_files[indexes[i]].vfid.fileid, page->dropped_files[indexes[i]].mvccid,
		     (int) indexes[i]);
      mem_size = (page->n_dropped_files - indexes[i]) * sizeof (VACUUM_DROPPED_FILE);

      assert (mem_size >= 0);
      if (mem_size > 0)
	{
	  memmove (&page->dropped_files[indexes[i]], &page->dropped_files[indexes[i] + 1], mem_size);
	}

      /* Update dropped files page counter */
      page->n_dropped_files--;
    }

  pgbuf_set_dirty (thread_p, rcv->pgptr, DONT_FREE);

  return NO_ERROR;
}

/*
 * vacuum_dropped_files_set_next_page () - Set dropped files next page link and log it.
 *
 * return	  : Void.
 * thread_p (in)  : Thread entry.
 * page_p (in)	  : Dropped files page.
 * next_page (in) : Next page VPID.
 */
static void
vacuum_dropped_files_set_next_page (THREAD_ENTRY * thread_p, VACUUM_DROPPED_FILES_PAGE * page_p, VPID * next_page)
{
  LOG_DATA_ADDR addr;

  /* Initialize log data address */
  addr.pgptr = (PAGE_PTR) page_p;
  addr.vfid = NULL;
  addr.offset = 0;

  /* log and change */
  log_append_undoredo_data (thread_p, RVVAC_DROPPED_FILE_NEXT_PAGE, &addr, sizeof (VPID), sizeof (VPID),
			    &page_p->next_page, next_page);
  page_p->next_page = *next_page;

  vacuum_set_dirty_dropped_entries_page (thread_p, page_p, DONT_FREE);
}

/*
 * vacuum_rv_set_next_page_dropped_files () - Recover setting link to next page for dropped files.
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
  assert (rcv->length == sizeof (VPID));

  vacuum_er_log (VACUUM_ER_LOG_RECOVERY, "Set link for dropped files from page %d|%d to page %d|%d.",
		 pgbuf_get_vpid_ptr (rcv->pgptr)->pageid, pgbuf_get_vpid_ptr (rcv->pgptr)->volid, page->next_page.volid,
		 page->next_page.pageid);

  pgbuf_set_dirty (thread_p, rcv->pgptr, DONT_FREE);

  return NO_ERROR;
}

/*
 * vacuum_compare_heap_object () - Compare two heap objects to be vacuumed. HFID compare has priority against OID
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
 * vacuum_collect_heap_objects () - Collect the heap object to be later vacuumed.
 *
 * return		  : Error code.
 * thread_p (in)          : thread entry
 * worker (in/out)	  : Vacuum worker structure.
 * oid (in)		  : Heap object OID.
 * vfid (in)		  : Heap file ID.
 */
static int
vacuum_collect_heap_objects (THREAD_ENTRY * thread_p, VACUUM_WORKER * worker, OID * oid, VFID * vfid)
{
  /* Collect both file ID and object OID to vacuum at the end of the job. Heap file ID is required to know whether
   * objects are reusable or not, OID is to point vacuum where data needs to be removed. */

  /* Make sure we have enough storage. */
  if (worker->n_heap_objects >= worker->heap_objects_capacity)
    {
      /* Expand buffer. */
      VACUUM_HEAP_OBJECT *new_buffer = NULL;
      int new_capacity = worker->heap_objects_capacity * 2;

      new_buffer = (VACUUM_HEAP_OBJECT *) realloc (worker->heap_objects, new_capacity * sizeof (VACUUM_HEAP_OBJECT));
      if (new_buffer == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
		  new_capacity * sizeof (VACUUM_HEAP_OBJECT));
	  vacuum_er_log_error (VACUUM_ER_LOG_WORKER,
			       "Could not expact the files and objects capacity to %d.", new_capacity);
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
 * vacuum_cleanup_collected_by_vfid () - Cleanup entries collected from dropped file.
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
  qsort (worker->heap_objects, worker->n_heap_objects, sizeof (VACUUM_HEAP_OBJECT), vacuum_compare_heap_object);

  /* Find first entry for file */
  for (start = 0; start < worker->n_heap_objects && !VFID_EQ (&worker->heap_objects[start].vfid, vfid); start++);
  if (start == worker->n_heap_objects)
    {
      /* VFID doesn't exist. */
      return;
    }
  /* Find first entry for other file. */
  for (end = start + 1; end < worker->n_heap_objects && VFID_EQ (&worker->heap_objects[end].vfid, vfid); end++);
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

#if defined (SERVER_MODE)
/*
 * vacuum_compare_dropped_files_version () - Compare two versions ID's of dropped files. Take into consideration that
 *					     versions can overflow max value of INT32.
 *
 * return	  : Positive value if first version is considered bigger,
 *		    negative if it is considered smaller and 0 if they are
 *		    equal.
 * version_a (in) : First version.
 * version_b (in) : Second version.
 */
static int
vacuum_compare_dropped_files_version (INT32 version_a, INT32 version_b)
{
  INT32 max_int32_div_2 = 0x3FFFFFFF;

  /* If both are positive or if both are negative return a-b */
  if ((version_a >= 0 && version_b >= 0) || (version_a < 0 && version_b < 0))
    {
      return (int) (version_a - version_b);
    }

  /* If one is positive and the other negative we have to consider the case when version overflowed INT32 and the case
   * when one just passed 0. In the first case, the positive value is considered smaller, while in the second case the
   * negative value is considered smaller. The INT32 domain of values is split into 4 ranges: [-MAX_INT32,
   * -MAX_INT32/2], [-MAX_INT32/2, 0], [0, MAX_INT32/2] and [MAX_INT32/2, MAX_INT32]. We will consider the case when
   * one value is in [-MAX_INT32, -MAX_INT32/2] and the other in [MAX_INT32/2, MAX_INT32] and the second case when the
   * values are in [-MAX_INT32/2, 0] and [0, MAX_INT32]. If the values are not in these ranges, the algorithm is
   * flawed. */
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
#endif // SERVER_MODE

#if !defined (NDEBUG)
/*
 * vacuum_verify_vacuum_data_debug () - Vacuum data sanity check.
 *
 * return    : Void.
 */
static void
vacuum_verify_vacuum_data_debug (THREAD_ENTRY * thread_p)
{
  int i;
  VACUUM_DATA_PAGE *data_page = NULL;
  VACUUM_DATA_ENTRY *entry = NULL;
  VACUUM_DATA_ENTRY *last_unvacuumed = NULL;
  VPID next_vpid;
  int in_progress_distance = 0;
  bool found_in_progress = false;

  data_page = vacuum_Data.first_page;

  /* First page is same as last page if and only if first page link to next page is NULL. */
  assert ((vacuum_Data.first_page == vacuum_Data.last_page) == (VPID_ISNULL (&vacuum_Data.first_page->next_page)));

  /* Loop sanity check for each vacuum data page. */
  while (true)
    {
      /* Check index_unvacuumed and index_unavaliable have valid values. */
      assert (data_page->index_unvacuumed >= 0 && data_page->index_unvacuumed < vacuum_Data.page_data_max_count);
      assert (data_page->index_free >= 0 && data_page->index_free <= vacuum_Data.page_data_max_count);
      assert (data_page->index_unvacuumed <= data_page->index_free);

      /* Check page has valid data. */
      for (i = data_page->index_unvacuumed; i < data_page->index_free; i++)
	{
	  /* Check page entries. */
	  entry = &data_page->data[i];

	  if (entry->is_vacuumed ())
	    {
	      assert (i != data_page->index_unvacuumed);
	      if (found_in_progress && !LSA_ISNULL (&data_page->data[i].start_lsa))
		{
		  in_progress_distance++;
		}
	      continue;
	    }

	  assert (entry->is_available () || entry->is_job_in_progress ());
	  assert (entry->oldest_visible_mvccid <= log_Gl.mvcc_table.get_global_oldest_visible ());
	  assert (vacuum_Data.oldest_unvacuumed_mvccid <= entry->oldest_visible_mvccid);
	  assert (entry->get_blockid () <= vacuum_Data.get_last_blockid ());
	  assert (vacuum_get_log_blockid (entry->start_lsa.pageid) == entry->get_blockid ());
	  assert (last_unvacuumed == NULL
		  || !MVCC_ID_PRECEDES (entry->oldest_visible_mvccid, last_unvacuumed->oldest_visible_mvccid));

	  if (i > data_page->index_unvacuumed)
	    {
	      assert (entry->get_blockid () == ((entry - 1)->get_blockid () + 1));
	    }

	  last_unvacuumed = entry;

	  if (entry->is_job_in_progress ())
	    {
	      found_in_progress = true;
	      in_progress_distance++;
	    }
	}
      if (VPID_ISNULL (&data_page->next_page))
	{
	  /* This was last page. Stop. */
	  data_page = NULL;
	  break;
	}
      /* Fix next page. */
      VPID_COPY (&next_vpid, &data_page->next_page);
      vacuum_unfix_data_page (thread_p, data_page);
      data_page = vacuum_fix_data_page (thread_p, &next_vpid);
      assert (data_page != NULL);
      last_unvacuumed = NULL;
    }

  if (in_progress_distance > 500)
    {
      /* In progress distance is computed starting with first in progress entry found and by counting all following
       * in progress or vacuumed jobs. The goal of this count is to find potential job leaks: jobs marked as in progress
       * but that never start or that are never marked as finished. We will assume that if this distance goes beyond some
       * value, then something bad must have happened.
       *
       * Theoretically, if a worker is blocked for long enough this value can be any size. However, we set a value unlikely
       * to be reached in normal circumstances.
       */

      /* It was an assertion but we have not seen a case that vacuum is blocked. */
      vacuum_er_log_warning (VACUUM_ER_LOG_WORKER | VACUUM_ER_LOG_VACUUM_DATA | VACUUM_ER_LOG_JOBS,
			     "vacuum is behind or blocked. distance is %d.", in_progress_distance);
    }
}
#endif /* !NDEBUG */

/*
 * vacuum_log_prefetch_vacuum_block () - Pre-fetches from log page buffer or from disk, (almost) all log pages
 *					 required by a vacuum block
 * thread_p (in):
 * entry (in): vacuum data entry
 *
 * Note : this function does not handle cases when last log entry in 'start_lsa'
 *	  page of vacuum data entry spans for more than extra one log page.
 *	  Only one extra page is loaded after the 'start_lsa' page.
 *	  Please note that 'start_lsa' page is the last log page (logically),
 *	  the vacuum will require log pages before this one.
 */
static int
vacuum_log_prefetch_vacuum_block (THREAD_ENTRY * thread_p, VACUUM_DATA_ENTRY * entry)
{
  VACUUM_WORKER *worker = vacuum_get_vacuum_worker (thread_p);
  int error = NO_ERROR;
  LOG_LSA req_lsa;
  LOG_PAGEID log_pageid;
  LOG_PAGE *log_page;

  req_lsa.offset = LOG_PAGESIZE;

  assert (entry != NULL);

  worker->prefetch_first_pageid = VACUUM_FIRST_LOG_PAGEID_IN_BLOCK (entry->get_blockid ());
  worker->prefetch_last_pageid = worker->prefetch_first_pageid + VACUUM_PREFETCH_LOG_BLOCK_BUFFER_PAGES - 1;

  for (log_pageid = worker->prefetch_first_pageid, log_page = (LOG_PAGE *) worker->prefetch_log_buffer;
       log_pageid <= worker->prefetch_last_pageid;
       log_pageid++, log_page = (LOG_PAGE *) (((char *) log_page) + LOG_PAGESIZE))
    {
      req_lsa.pageid = log_pageid;
      error = logpb_fetch_page (thread_p, &req_lsa, LOG_CS_SAFE_READER, log_page);
      if (error != NO_ERROR)
	{
	  assert (false);	// failure is not acceptable
	  vacuum_er_log_error (VACUUM_ER_LOG_ERROR, "cannot prefetch log page %d", log_pageid);

	  error = ER_FAILED;
	  goto end;
	}
    }

  vacuum_er_log (VACUUM_ER_LOG_MASTER, "VACUUM : prefetched %d log pages from %lld to %lld",
		 VACUUM_PREFETCH_LOG_BLOCK_BUFFER_PAGES, (long long int) worker->prefetch_first_pageid,
		 (long long int) worker->prefetch_last_pageid);

end:
  return error;
}


/*
 * vacuum_fetch_log_page () - Loads a log page to be processed by vacuum from vacuum block buffer or log page buffer or
 *			      disk log archive.
 *
 * thread_p (in):
 * log_pageid (in): log page logical id
 * log_page_p (in/out): pre-allocated buffer to store one log page
 *
 */
static int
vacuum_fetch_log_page (THREAD_ENTRY * thread_p, LOG_PAGEID log_pageid, LOG_PAGE * log_page_p)
{
  int error = NO_ERROR;

  if (vacuum_is_thread_vacuum (thread_p))
    {
      // try to fetch from prefetched pages
      VACUUM_WORKER *worker = vacuum_get_vacuum_worker (thread_p);

      assert (worker != NULL);
      assert (log_page_p != NULL);

      perfmon_inc_stat (thread_p, PSTAT_VAC_NUM_PREFETCH_REQUESTS_LOG_PAGES);

      if (worker->prefetch_first_pageid <= log_pageid && log_pageid <= worker->prefetch_last_pageid)
	{
	  /* log page is cached */
	  size_t page_index = log_pageid - worker->prefetch_first_pageid;
	  memcpy (log_page_p, worker->prefetch_log_buffer + page_index * LOG_PAGESIZE, LOG_PAGESIZE);

	  assert (log_page_p->hdr.logical_pageid == log_pageid);	// should be the correct page

	  perfmon_inc_stat (thread_p, PSTAT_VAC_NUM_PREFETCH_HITS_LOG_PAGES);
	  return NO_ERROR;
	}
      else
	{
	  vacuum_er_log (VACUUM_ER_LOG_WARNING | VACUUM_ER_LOG_LOGGING,
			 "log page %lld is not in prefetched range %lld - %lld",
			 log_pageid, worker->prefetch_first_pageid, worker->prefetch_last_pageid);
	}
      // fall through
    }
  else
    {
      // there are two possible paths here
      // 1. vacuum_process_log_block (when caller must be vacuum worker)
      // 2. vacuum_recover_lost_block_data (when caller is boot thread)
      // this must be second case
    }
  // need to fetch from log

  LOG_LSA req_lsa;
  req_lsa.pageid = log_pageid;
  req_lsa.offset = LOG_PAGESIZE;
  error = logpb_fetch_page (thread_p, &req_lsa, LOG_CS_SAFE_READER, log_page_p);
  if (error != NO_ERROR)
    {
      assert (false);		// failure is not acceptable
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "vacuum_fetch_log_page");
      error = ER_FAILED;
    }

  return error;
}

/*
 * print_not_vacuumed_to_log () - prints to log info related to a not vacuumed OID (either from HEAP or BTREE)
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
print_not_vacuumed_to_log (OID * oid, OID * class_oid, MVCC_REC_HEADER * rec_header, int btree_node_type)
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
    sprintf (p, " with oid=%d|%d|%d, class_oid=%d|%d|%d", (int) oid->volid, oid->pageid, (int) oid->slotid,
	     (int) class_oid->volid, class_oid->pageid, (int) class_oid->slotid);
  if (MVCC_IS_FLAG_SET (rec_header, OR_MVCC_FLAG_VALID_INSID))
    {
      p += sprintf (p, ", insert_id=%llu", (unsigned long long int) MVCC_GET_INSID (rec_header));
    }
  else
    {
      p += sprintf (p, ", insert_id=missing");
    }
  if (MVCC_IS_HEADER_DELID_VALID (rec_header))
    {
      p += sprintf (p, ", delete_id=%llu", (unsigned long long int) MVCC_GET_DELID (rec_header));
    }
  else
    {
      p += sprintf (p, ", delete_id=missing");
    }
  p += sprintf (p, ", oldest_mvcc_id=%llu", (unsigned long long int) vacuum_Data.oldest_unvacuumed_mvccid);
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
 * vacuum_check_not_vacuumed_recdes () - checks if an OID should've been vacuumed (using a record descriptor)
 *
 * return: DISK_INVALID if the OID was not vacuumed, DISK_VALID if it was
 *	   and DISK_ERROR in case of an error.
 * thread_p (in):
 * oid (in): The not vacuumed instance OID
 * class_oid (in): The class to which the oid belongs
 * recdes (in): The not vacuumed record
 * btree_node_type (in): If the oid is not vacuumed from BTREE then this is
 *			 the type node. If <0 then the OID comes from heap.
 *
 */
DISK_ISVALID
vacuum_check_not_vacuumed_recdes (THREAD_ENTRY * thread_p, OID * oid, OID * class_oid, RECDES * recdes,
				  int btree_node_type)
{
  MVCC_REC_HEADER rec_header;

  if (or_mvcc_get_header (recdes, &rec_header) != NO_ERROR)
    {
      return DISK_ERROR;
    }

  return vacuum_check_not_vacuumed_rec_header (thread_p, oid, class_oid, &rec_header, btree_node_type);
}

/*
 * is_not_vacuumed_and_lost () - checks if a record should've been vacuumed (using a record header)
 *
 * return: true if the record was not vacuumed and is completely lost.
 * thread_p (in):
 * rec_header (in): The header of the record to be checked
 *
 */
static bool
is_not_vacuumed_and_lost (THREAD_ENTRY * thread_p, MVCC_REC_HEADER * rec_header)
{
  MVCC_SATISFIES_VACUUM_RESULT res;

  res = mvcc_satisfies_vacuum (thread_p, rec_header, vacuum_Data.oldest_unvacuumed_mvccid);
  switch (res)
    {
    case VACUUM_RECORD_REMOVE:
      /* Record should have been vacuumed by now. */
      return true;

    case VACUUM_RECORD_DELETE_INSID_PREV_VER:
      /* Record insert & previous version should have been vacuumed by now. */
      return true;

    case VACUUM_RECORD_CANNOT_VACUUM:
      return false;

    default:
      return false;
    }
}

/*
 * vacuum_check_not_vacuumed_rec_header () - checks if an OID should've been vacuumed (using a record header)
 *
 * return: DISK_INVALID if the OID was not vacuumed, DISK_VALID if it was
 *	   and DISK_ERROR in case of an error.
 * thread_p (in):
 * oid (in): The not vacuumed instance OID
 * class_oid (in): The class to which belongs the oid
 * rec_header (in): The not vacuumed record header
 * btree_node_type (in): If the oid is not vacuumed from BTREE then this is
 *			 the type node. If <0 then the OID comes from heap.
 *
 */
DISK_ISVALID
vacuum_check_not_vacuumed_rec_header (THREAD_ENTRY * thread_p, OID * oid, OID * class_oid, MVCC_REC_HEADER * rec_header,
				      int btree_node_type)
{
  if (is_not_vacuumed_and_lost (thread_p, rec_header))
    {
      OID cls_oid;
      if (class_oid == NULL || OID_ISNULL (class_oid))
	{
	  if (heap_get_class_oid (thread_p, oid, &cls_oid) != S_SUCCESS)
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
 * vacuum_get_first_page_dropped_files () - Get the first allocated vpid of vacuum_Dropped_files_vfid.
 *
 * return    : VPID *
 * thread_p (in):
 * first_page_vpid (out):
 *
 */
static int
vacuum_get_first_page_dropped_files (THREAD_ENTRY * thread_p, VPID * first_page_vpid)
{
  assert (!VFID_ISNULL (&vacuum_Dropped_files_vfid));
  return file_get_sticky_first_page (thread_p, &vacuum_Dropped_files_vfid, first_page_vpid);
}

/*
 * vacuum_is_mvccid_vacuumed () - Return true if MVCCID should be vacuumed.
 *				  It must be older than vacuum_Data->oldest_unvacuumed_mvccid.
 *
 * return  : True/false.
 * id (in) : MVCCID to check.
 */
bool
vacuum_is_mvccid_vacuumed (MVCCID id)
{
  if (id < vacuum_Data.oldest_unvacuumed_mvccid)
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
 * reusable (in)      :
 *
 * NOTE: Some values in slots array are modified and set to negative values.
 */
static void
vacuum_log_redoundo_vacuum_record (THREAD_ENTRY * thread_p, PAGE_PTR page_p, PGSLOTID slotid, RECDES * undo_recdes,
				   bool reusable)
{
  LOG_DATA_ADDR addr;
  LOG_CRUMB undo_crumbs[2];
  int num_undo_crumbs;

  assert (slotid >= 0 && slotid < ((SPAGE_HEADER *) page_p)->num_slots);

  /* Initialize log data. */
  addr.offset = slotid;
  addr.pgptr = page_p;
  addr.vfid = NULL;

  if (reusable)
    {
      addr.offset |= VACUUM_LOG_VACUUM_HEAP_REUSABLE;
    }

  undo_crumbs[0].length = sizeof (undo_recdes->type);
  undo_crumbs[0].data = (char *) &undo_recdes->type;
  undo_crumbs[1].length = undo_recdes->length;
  undo_crumbs[1].data = undo_recdes->data;
  num_undo_crumbs = 2;

  /* Log undoredo with NULL redo crumbs - the redo function (vacuum_rv_redo_vacuum_heap_record) require only
   * the object's address to re-vacuum */
  log_append_undoredo_crumbs (thread_p, RVVAC_HEAP_RECORD_VACUUM, &addr, num_undo_crumbs, 0, undo_crumbs, NULL);
}

/*
 * vacuum_rv_undo_vacuum_heap_record () - undo function for RVVAC_HEAP_RECORD_VACUUM
 *
 * return	 : Error code.
 * thread_p (in) : Thread entry.
 * rcv (in)	 : Recovery structure.
 */
int
vacuum_rv_undo_vacuum_heap_record (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  rcv->offset = (rcv->offset & (~VACUUM_LOG_VACUUM_HEAP_MASK));

  return heap_rv_redo_insert (thread_p, rcv);
}

/*
 * vacuum_rv_redo_vacuum_heap_record () - redo function for RVVAC_HEAP_RECORD_VACUUM
 *
 * return	 : Error code.
 * thread_p (in) : Thread entry.
 * rcv (in)	 : Recovery structure.
 */
int
vacuum_rv_redo_vacuum_heap_record (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  INT16 slotid;
  bool reusable;

  slotid = (rcv->offset & (~VACUUM_LOG_VACUUM_HEAP_MASK));
  reusable = (rcv->offset & VACUUM_LOG_VACUUM_HEAP_REUSABLE) != 0;

  spage_vacuum_slot (thread_p, rcv->pgptr, slotid, reusable);

  if (spage_need_compact (thread_p, rcv->pgptr) == true)
    {
      (void) spage_compact (thread_p, rcv->pgptr);
    }

  pgbuf_set_dirty (thread_p, rcv->pgptr, DONT_FREE);

  return NO_ERROR;
}

/*
 * vacuum_notify_server_crashed () - Notify vacuum that server has crashed and that recovery is running. After
 *				     recovery, when vacuum data is being loaded, vacuum will also recover the
 *				     block data buffer that had not been saved to vacuum data before crash.
 *				     The recovery LSA argument is used in case no MVCC operation log record is found
 *				     during recovery.
 *
 * return	     : Void.
 * recovery_lsa (in) : Recovery starting LSA.
 */
void
vacuum_notify_server_crashed (LOG_LSA * recovery_lsa)
{
  LSA_COPY (&vacuum_Data.recovery_lsa, recovery_lsa);
}

/*
 * vacuum_notify_server_shutdown () - Notify vacuum that server shutdown was requested. It should stop executing new
 *				      jobs.
 *
 * return : Void.
 */
void
vacuum_notify_server_shutdown (void)
{
  vacuum_Data.shutdown_sequence.request_shutdown ();
}

#if !defined (NDEBUG)
/*
 * vacuum_data_check_page_fix () - Check fix counts on vacuum data pages are not off.
 *
 * return	 : Void.
 * thread_p (in) : Thread entry.
 */
static void
vacuum_verify_vacuum_data_page_fix_count (THREAD_ENTRY * thread_p)
{
  assert (pgbuf_get_fix_count ((PAGE_PTR) vacuum_Data.first_page) == 1);
  assert (vacuum_Data.last_page == vacuum_Data.first_page
	  || pgbuf_get_fix_count ((PAGE_PTR) vacuum_Data.last_page) == 1);
  if (vacuum_Data.first_page == vacuum_Data.last_page)
    {
      assert (pgbuf_get_hold_count (thread_p) == 1);
    }
  else
    {
      assert (pgbuf_get_fix_count ((PAGE_PTR) vacuum_Data.last_page) == 1);
      assert (pgbuf_get_hold_count (thread_p) == 2);
    }
}
#endif /* !NDEBUG */

/*
 * vacuum_rv_check_at_undo () - check and modify undo record header to satisfy vacuum status
 *
 * return	 : Error code.
 * thread_p (in) : Thread entry.
 * pgptr (in)	 : Page where record resides.
 * slotid (in)   : Record slot.
 * rec_type (in) : Expected record type.
 *
 * Note: This function will update the record to be valid in terms of vacuuming. Insert ID and prev version
 *       must be removed from the record at undo, if the record was subject to vacuuming but skipped
 *       during an update/delete operation. This happens when the record is changed before vacuum reaches it,
 *       and when it is reached its new header is different and not qualified for vacuum anymore.
 */
int
vacuum_rv_check_at_undo (THREAD_ENTRY * thread_p, PAGE_PTR pgptr, INT16 slotid, INT16 rec_type)
{
  MVCC_REC_HEADER rec_header;
  MVCC_SATISFIES_VACUUM_RESULT can_vacuum;
  RECDES recdes = RECDES_INITIALIZER;
  char data_buffer[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT];

  /* get record header according to record type */
  if (rec_type == REC_BIGONE)
    {
      if (heap_get_mvcc_rec_header_from_overflow (pgptr, &rec_header, &recdes) != NO_ERROR)
	{
	  assert_release (false);
	  return ER_FAILED;
	}
      recdes.type = REC_BIGONE;
    }
  else
    {
      recdes.data = PTR_ALIGN (data_buffer, MAX_ALIGNMENT);
      recdes.area_size = DB_PAGESIZE;
      if (spage_get_record (thread_p, pgptr, slotid, &recdes, COPY) != S_SUCCESS)
	{
	  assert_release (false);
	  return ER_FAILED;
	}

      if (or_mvcc_get_header (&recdes, &rec_header) != NO_ERROR)
	{
	  assert_release (false);
	  return ER_FAILED;
	}
    }

  assert (recdes.type == rec_type);

  if (log_is_in_crash_recovery ())
    {
      /* always clear flags when recovering from crash - all the objects are visible anyway */
      if (MVCC_IS_FLAG_SET (&rec_header, OR_MVCC_FLAG_VALID_INSID))
	{
	  /* Note: PREV_VERSION flag should be set only if VALID_INSID flag is set  */
	  can_vacuum = VACUUM_RECORD_DELETE_INSID_PREV_VER;
	}
      else
	{
	  assert (!MVCC_IS_FLAG_SET (&rec_header, OR_MVCC_FLAG_VALID_PREV_VERSION));
	  can_vacuum = VACUUM_RECORD_CANNOT_VACUUM;
	}
    }
  else
    {
      can_vacuum = mvcc_satisfies_vacuum (thread_p, &rec_header, log_Gl.mvcc_table.get_global_oldest_visible ());
    }

  /* it is impossible to restore a record that should be removed by vacuum */
  assert (can_vacuum != VACUUM_RECORD_REMOVE);

  if (can_vacuum == VACUUM_RECORD_DELETE_INSID_PREV_VER)
    {
      /* the undo/redo record was qualified to have its insid and prev version vacuumed;
       * do this here because it is possible that vacuum have missed it during update/delete operation */
      if (rec_type == REC_BIGONE)
	{
	  assert (MVCC_IS_FLAG_SET (&rec_header, OR_MVCC_FLAG_VALID_INSID | OR_MVCC_FLAG_VALID_PREV_VERSION));
	  MVCC_SET_INSID (&rec_header, MVCCID_ALL_VISIBLE);
	  LSA_SET_NULL (&rec_header.prev_version_lsa);

	  if (heap_set_mvcc_rec_header_on_overflow (pgptr, &rec_header) != NO_ERROR)
	    {
	      assert_release (false);
	      return ER_FAILED;
	    }
	}
      else
	{
	  MVCC_CLEAR_FLAG_BITS (&rec_header, OR_MVCC_FLAG_VALID_INSID | OR_MVCC_FLAG_VALID_PREV_VERSION);

	  if (or_mvcc_set_header (&recdes, &rec_header) != NO_ERROR)
	    {
	      assert_release (false);
	      return ER_FAILED;
	    }

	  /* update the record */
	  if (spage_update (thread_p, pgptr, slotid, &recdes) != SP_SUCCESS)
	    {
	      assert_release (false);
	      return ER_FAILED;
	    }
	}

      pgbuf_set_dirty (thread_p, pgptr, DONT_FREE);
    }

  return NO_ERROR;
}

/*
 * vacuum_is_empty() - Checks if the vacuum is empty.
 *
 * return :- true or false
 */
bool
vacuum_is_empty (void)
{
  if (vacuum_Data.first_page->index_unvacuumed == vacuum_Data.first_page->index_free)
    {
      assert (vacuum_Data.first_page == vacuum_Data.last_page);
      assert (vacuum_Data.last_page->index_unvacuumed == 0);
      return true;
    }

  return false;
}

/*
 *  vacuum_sa_reflect_last_blockid () - Update vacuum last blockid on SA_MODE
 *
 *  thread_p(in) :- Thread context.
 */
void
vacuum_sa_reflect_last_blockid (THREAD_ENTRY * thread_p)
{
  if (VPID_ISNULL (&vacuum_Data_load.vpid_first))
    {
      // database is freshly created or boot was aborted without doing anything
      return;
    }
  if (vacuum_Data.is_restoredb_session)
    {
      // restoredb doesn't vacuum; we cannot do this here
      return;
    }

  vacuum_data_load_first_and_last_page (thread_p);

  VACUUM_LOG_BLOCKID last_blockid = logpb_last_complete_blockid ();

  vacuum_er_log (VACUUM_ER_LOG_VACUUM_DATA,
		 "vacuum_sa_reflect_last_blockid: last_blockid=%lld, append_prev_pageid=%d\n",
		 (long long int) last_blockid, (int) log_Gl.append.prev_lsa.pageid);
  if (last_blockid == VACUUM_NULL_LOG_BLOCKID)
    {
      vacuum_data_unload_first_and_last_page (thread_p);
      return;
    }

  vacuum_Data.set_last_blockid (last_blockid);
  log_Gl.hdr.vacuum_last_blockid = last_blockid;
  vacuum_data_empty_update_last_blockid (thread_p);

  vacuum_data_unload_first_and_last_page (thread_p);
}

static void
vacuum_data_empty_update_last_blockid (THREAD_ENTRY * thread_p)
{
  assert (vacuum_is_empty ());

  VACUUM_DATA_PAGE *data_page = vacuum_Data.first_page;
  assert (data_page != NULL);

  /* We should have only 1 page in vacuum_Data. */
  assert (vacuum_Data.first_page == vacuum_Data.last_page);

  vacuum_init_data_page_with_last_blockid (thread_p, vacuum_Data.first_page, vacuum_Data.get_last_blockid ());

  vacuum_er_log (VACUUM_ER_LOG_VACUUM_DATA,
		 "vacuum_data_empty_update_last_blockid: update last_blockid=%lld in page %d|%d at lsa %lld|%d",
		 (long long int) vacuum_Data.get_last_blockid (), PGBUF_PAGE_STATE_ARGS ((PAGE_PTR) (data_page)));
}

/*
 * vacuum_convert_thread_to_master () - convert thread to vacuum master
 *
 * thread_p (in)   : thread entry
 * save_type (out) : thread entry old type
 */
static void
vacuum_convert_thread_to_master (THREAD_ENTRY * thread_p, thread_type & save_type)
{
  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }
  save_type = thread_p->type;
  thread_p->type = TT_VACUUM_MASTER;
  thread_p->vacuum_worker = &vacuum_Master;
  if (thread_p->get_system_tdes () == NULL)
    {
      thread_p->claim_system_worker ();
    }
}

/*
 * vacuum_convert_thread_to_worker - convert this thread to a vacuum worker
 *
 * thread_p (in)   : thread entry
 * worker (in)     : vacuum worker context
 * save_type (out) : save previous thread type
 */
static void
vacuum_convert_thread_to_worker (THREAD_ENTRY * thread_p, VACUUM_WORKER * worker, thread_type & save_type)
{
  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }
  save_type = thread_p->type;
  thread_p->type = TT_VACUUM_WORKER;
  thread_p->vacuum_worker = worker;
  if (vacuum_worker_allocate_resources (thread_p, thread_p->vacuum_worker) != NO_ERROR)
    {
      assert_release (false);
    }
  if (thread_p->get_system_tdes () == NULL)
    {
      thread_p->claim_system_worker ();
    }
}

/*
 * vacuum_restore_thread - restore thread previously converted to a vacuum worker
 *
 * thread_p (in)  : thread entry
 * save_type (in) : saved type of thread entry
 */
static void
vacuum_restore_thread (THREAD_ENTRY * thread_p, thread_type save_type)
{
  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }
  thread_p->type = save_type;
  thread_p->vacuum_worker = NULL;
  thread_p->retire_system_worker ();
  thread_p->tran_index = LOG_SYSTEM_TRAN_INDEX;	// restore tran_index
}

/*
 * vacuum_rv_es_nop () - Skip recovery operation for external storage.
 *
 * return	 : NO_ERROR.
 * thread_p (in) : Thread entry.
 * rcv (in)	 : Recovery data.
 */
int
vacuum_rv_es_nop (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  /* Do nothing */
  return NO_ERROR;
}

#if defined (SERVER_MODE)
/*
 * vacuum_notify_es_deleted () - External storage file cannot be deleted
 *				    when transaction is ended and MVCC is
 *				    used. Vacuum must be notified instead and
 *				    file is deleted when it is no longer
 *				    visible.
 *
 * return	 : Void.
 * thread_p (in) : Thread entry.
 * uri (in)	 : File location URI.
 */
void
vacuum_notify_es_deleted (THREAD_ENTRY * thread_p, const char *uri)
{
#define ES_NOTIFY_VACUUM_FOR_DELETE_BUFFER_SIZE \
  (INT_ALIGNMENT +	/* Aligning buffer start */	      \
   OR_INT_SIZE +	/* String length */		      \
   ES_MAX_URI_LEN +	/* URI string */	  	      \
   INT_ALIGNMENT)		/* Alignment of packed string */

  LOG_DATA_ADDR addr;
  int length;
  char data_buf[ES_NOTIFY_VACUUM_FOR_DELETE_BUFFER_SIZE];
  char *data = NULL;

  addr.offset = -1;
  addr.pgptr = NULL;
  addr.vfid = NULL;

  /* Compute the total length required to pack string */
  length = or_packed_string_length (uri, NULL);

  /* Check there is enough space in data buffer to pack the string */
  assert (length <= (int) (ES_NOTIFY_VACUUM_FOR_DELETE_BUFFER_SIZE - INT_ALIGNMENT));

  /* Align buffer to prepare for packing string */
  data = PTR_ALIGN (data_buf, INT_ALIGNMENT);

  /* Pack string */
  (void) or_pack_string (data, uri);

  /* This is not actually ever undone, but vacuum will process undo data of log entry. */
  log_append_undo_data (thread_p, RVES_NOTIFY_VACUUM, &addr, length, data);
}
#endif /* SERVER_MODE */

//
// vacuum_check_shutdown_interruption () - check error occurs due to shutdown interrupting
//
// thread_p (in)   : thread entry
// error_code (in) : error code
//
static void
vacuum_check_shutdown_interruption (const THREAD_ENTRY * thread_p, int error_code)
{
  ASSERT_ERROR ();
  // interrupted is accepted if:
  // 1. this is an active worker thread
  // 2. or server is shutting down
  assert (!vacuum_is_thread_vacuum_worker (thread_p) || (thread_p->shutdown && error_code == ER_INTERRUPTED));
}

//
// vacuum_reset_data_after_copydb () - reset vacuum data after copydb. since complete vacuum is run on copied database
//                                     there should be no actual data; however, last_blockid remains set in first
//                                     data entry
//
int
vacuum_reset_data_after_copydb (THREAD_ENTRY * thread_p)
{
  assert (vacuum_Data.first_page == NULL && vacuum_Data.last_page == NULL);
  assert (!VFID_ISNULL (&vacuum_Data.vacuum_data_file));

  int error_code = NO_ERROR;
  FILE_DESCRIPTORS fdes;

  error_code = file_descriptor_get (thread_p, &vacuum_Data.vacuum_data_file, &fdes);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }
  assert (!VPID_ISNULL (&fdes.vacuum_data.vpid_first));

  vacuum_Data.first_page = vacuum_fix_data_page (thread_p, &fdes.vacuum_data.vpid_first);
  if (vacuum_Data.first_page == NULL)
    {
      ASSERT_ERROR_AND_SET (error_code);
      return error_code;
    }

  // there should be no data
  assert (VPID_ISNULL (&vacuum_Data.first_page->next_page));
  assert (vacuum_Data.first_page->index_free == 0);

  vacuum_init_data_page_with_last_blockid (thread_p, vacuum_Data.first_page, VACUUM_NULL_LOG_BLOCKID);

  vacuum_er_log (VACUUM_ER_LOG_VACUUM_DATA, "Reset vacuum data page %d|%d, lsa %lld|%d, after copydb",
		 PGBUF_PAGE_STATE_ARGS ((PAGE_PTR) vacuum_Data.first_page));

  vacuum_unfix_first_and_last_data_page (thread_p);

  return NO_ERROR;
}

static void
vacuum_init_data_page_with_last_blockid (THREAD_ENTRY * thread_p, VACUUM_DATA_PAGE * data_page,
					 VACUUM_LOG_BLOCKID blockid)
{
  vacuum_data_initialize_new_page (thread_p, data_page);
  data_page->data->blockid = blockid;
  log_append_redo_data2 (thread_p, RVVAC_DATA_INIT_NEW_PAGE, NULL, (PAGE_PTR) data_page, 0, sizeof (blockid), &blockid);
  vacuum_set_dirty_data_page (thread_p, data_page, DONT_FREE);
}

// *INDENT-OFF*
//
// C++
//

//
// vacuum_data
//
bool
vacuum_data::is_empty () const
{
  return has_one_page () && first_page->is_empty ();
}

bool
vacuum_data::has_one_page () const
{
  return first_page == last_page;
}

VACUUM_LOG_BLOCKID
vacuum_data::get_last_blockid (void) const
{
  return m_last_blockid;
}

VACUUM_LOG_BLOCKID
vacuum_data::get_first_blockid () const
{
  if (is_empty ())
    {
      return m_last_blockid;
    }
  return first_page->get_first_blockid ();
}

const VACUUM_DATA_ENTRY &
vacuum_data::get_first_entry () const
{
  assert (!is_empty ());
  return first_page->data[0];
}

void
vacuum_data::set_last_blockid (VACUUM_LOG_BLOCKID blockid)
{
  // first, make sure we string flags
  blockid = VACUUM_BLOCKID_WITHOUT_FLAGS (blockid);

#if !defined (NDEBUG)
  // sanity check - last_blockid should be less than last LSA's block
  LOG_LSA log_lsa = log_Gl.prior_info.prior_lsa;
  VACUUM_LOG_BLOCKID log_blockid = vacuum_get_log_blockid (log_lsa.pageid);
  assert (blockid < log_blockid);
#endif // NDEBUG

  m_last_blockid = blockid;
}

void
vacuum_data::update ()
{
  cubthread::entry *thread_p = &cubthread::get_entry ();
  bool updated_oldest_unvacuumed = false;

  // three major operations need to be done here:
  //
  // 1. mark finished jobs as vacuumed/interrupted
  // 2. consume new blocks from log
  // 3. maintain oldest unvacuumed mvccid (for sanity checks)

  // For 3rd part, when vacuum data is not empty, the operation is trivial - just set to first block data oldest mvccid.
  // (the algorithm ensures that entries oldest mvccid is always ascending)
  // When vacuum data is empty, just don't update oldest_unvacuumed

  // first remove vacuumed blocks
  vacuum_data_mark_finished (thread_p);

  // then consume new generated blocks
  vacuum_consume_buffer_log_blocks (thread_p);

  if (!vacuum_Data.is_empty ())
    {
      // buffer was not empty, we can trivially update to first entry oldest mvccid
      upgrade_oldest_unvacuumed (get_first_entry ().oldest_visible_mvccid);
    }
}

void
vacuum_data::set_oldest_unvacuumed_on_boot ()
{
  // no thread safety needs to be considered here
  if (!log_Gl.hdr.does_block_need_vacuum)
    {
      // log_Gl.hdr.oldest_visible_mvccid may not remain uninitialized
      log_Gl.hdr.oldest_visible_mvccid = log_Gl.hdr.mvcc_next_id;
    }
  if (vacuum_Data.is_empty ())
    {
      oldest_unvacuumed_mvccid = log_Gl.hdr.oldest_visible_mvccid;
    }
  else
    {
      // set on first block oldest mvccid
      oldest_unvacuumed_mvccid = first_page->data[0].oldest_visible_mvccid;
      assert (oldest_unvacuumed_mvccid <= log_Gl.hdr.oldest_visible_mvccid);
    }
}

void
vacuum_data::upgrade_oldest_unvacuumed (MVCCID mvccid)
{
  assert (oldest_unvacuumed_mvccid <= mvccid);
  oldest_unvacuumed_mvccid = mvccid;
}

//
// vacuum_data_entry
//
vacuum_data_entry::vacuum_data_entry (const log_lsa &lsa, MVCCID oldest, MVCCID newest)
  : blockid (VACUUM_NULL_LOG_BLOCKID)
  , start_lsa (lsa)
  , oldest_visible_mvccid (oldest)
  , newest_mvccid (newest)
{
  assert (!lsa.is_null ());
  assert (MVCCID_IS_VALID (oldest));
  assert (MVCCID_IS_VALID (newest));
  assert (oldest <= newest);
  blockid = vacuum_get_log_blockid (start_lsa.pageid);
}

vacuum_data_entry::vacuum_data_entry (const log_header &hdr)
  : vacuum_data_entry (hdr.mvcc_op_log_lsa, hdr.oldest_visible_mvccid, hdr.newest_block_mvccid)
{
}

VACUUM_LOG_BLOCKID
vacuum_data_entry::get_blockid () const
{
  return VACUUM_BLOCKID_WITHOUT_FLAGS (blockid);
}

bool
vacuum_data_entry::is_available () const
{
  return VACUUM_BLOCK_STATUS_IS_AVAILABLE (blockid);
}

bool
vacuum_data_entry::is_vacuumed () const
{
  return VACUUM_BLOCK_STATUS_IS_VACUUMED (blockid);
}

bool
vacuum_data_entry::is_job_in_progress () const
{
  return VACUUM_BLOCK_STATUS_IS_IN_PROGRESS (blockid);
}

bool
vacuum_data_entry::was_interrupted () const
{
  return VACUUM_BLOCK_IS_INTERRUPTED (blockid);
}

void
vacuum_data_entry::set_vacuumed ()
{
  VACUUM_BLOCK_STATUS_SET_VACUUMED (blockid);
  VACUUM_BLOCK_CLEAR_INTERRUPTED (blockid);
}

void
vacuum_data_entry::set_job_in_progress ()
{
  VACUUM_BLOCK_STATUS_SET_IN_PROGRESS (blockid);
}

void
vacuum_data_entry::set_interrupted ()
{
  VACUUM_BLOCK_STATUS_SET_AVAILABLE (blockid);
  VACUUM_BLOCK_SET_INTERRUPTED (blockid);
}

//
// vacuum_data_page
//
bool
vacuum_data_page::is_empty () const
{
  return index_unvacuumed == index_free;
}

bool
vacuum_data_page::is_index_valid (INT16 index) const
{
  return index >= index_unvacuumed && index < index_free;
}

INT16
vacuum_data_page::get_index_of_blockid (VACUUM_LOG_BLOCKID blockid) const
{
  if (is_empty ())
    {
      return INDEX_NOT_FOUND;
    }

  VACUUM_LOG_BLOCKID first_blockid = data[index_unvacuumed].get_blockid ();
  if (first_blockid > blockid)
    {
      return INDEX_NOT_FOUND;
    }
  VACUUM_LOG_BLOCKID last_blockid = data[index_free - 1].get_blockid ();
  if (last_blockid < blockid)
    {
      return INDEX_NOT_FOUND;
    }
  INT16 index_of_blockid = (INT16) (blockid - first_blockid) + index_unvacuumed;
  assert (data[index_of_blockid].get_blockid () == blockid);
  return index_of_blockid;
}

VACUUM_LOG_BLOCKID
vacuum_data_page::get_first_blockid () const
{
  assert (!is_empty ());
  return data[index_unvacuumed].get_blockid ();
}

//
// vacuum_job_cursor
//
vacuum_job_cursor::vacuum_job_cursor ()
  : m_blockid (VACUUM_NULL_LOG_BLOCKID)
  , m_page (NULL)
  , m_index (vacuum_data_page::INDEX_NOT_FOUND)
{
}

vacuum_job_cursor::~vacuum_job_cursor ()
{
  // check it was unloaded
  assert (m_page == NULL);
}

bool
vacuum_job_cursor::is_valid () const
{
  return is_loaded ();   // if loaded, must be valid
}

bool
vacuum_job_cursor::is_loaded () const
{
  assert (m_page == NULL || m_page->is_index_valid (m_index));
  return m_page != NULL;
}

VACUUM_LOG_BLOCKID
vacuum_job_cursor::get_blockid () const
{
  return m_blockid;
}

const VPID &
vacuum_job_cursor::get_page_vpid () const
{
  return m_page != NULL ? *pgbuf_get_vpid_ptr ((PAGE_PTR) m_page) : vpid_Null_vpid;
}

vacuum_data_page *
vacuum_job_cursor::get_page () const
{
  assert (m_page != NULL);
  return m_page;
}

INT16
vacuum_job_cursor::get_index () const
{
  return m_index;
}

const vacuum_data_entry &
vacuum_job_cursor::get_current_entry () const
{
  assert (is_valid ());

  return m_page->data[m_index];
}

void
vacuum_job_cursor::start_job_on_current_entry () const
{
  assert (is_valid ());
  cubthread::entry * thread_p = &cubthread::get_entry ();
  vacuum_data_entry &entry = m_page->data[m_index];
  entry.set_job_in_progress ();
  if (!entry.was_interrupted ())
    {
      /* Log that a new job is starting. After recovery, the system will then know this job was partially executed.
       * Logging the start of a job already interrupted is not necessary. We do it here rather than when vacuum job
       * is really started to avoid locking vacuum data again (logging vacuum data cannot be done without locking).
       */
      LOG_DATA_ADDR addr { NULL, (PAGE_PTR) m_page, (PGLENGTH) m_index };
      log_append_redo_data (thread_p, RVVAC_START_JOB, &addr, 0, NULL);
    }
  vacuum_set_dirty_data_page_dont_free (thread_p, m_page);
}

void
vacuum_job_cursor::force_data_update ()
{
  unload ();   // can't be loaded while updating
  vacuum_Data.update ();
  readjust_to_vacuum_data_changes ();
  load ();
}

void
vacuum_job_cursor::change_blockid (VACUUM_LOG_BLOCKID blockid)
{
  // can only increment
  assert (m_blockid <= blockid);

  m_blockid = blockid;

  // make sure m_page/m_index point to right blockid
  if (m_blockid > vacuum_Data.get_last_blockid ())
    {
      // cursor consumed all data
      assert (m_blockid == vacuum_Data.get_last_blockid () + 1);
      unload ();
    }
  else
    {
      assert (m_blockid >= vacuum_Data.get_first_blockid ());
      reload ();
    }
}

void
vacuum_job_cursor::increment_blockid ()
{
  change_blockid (m_blockid + 1);
  vacuum_er_log (VACUUM_ER_LOG_JOBS, "incremented " vacuum_job_cursor_print_format,
                 vacuum_job_cursor_print_args (*this));
}

void
vacuum_job_cursor::set_on_vacuum_data_start ()
{
  m_blockid = vacuum_Data.get_first_blockid ();
}

void
vacuum_job_cursor::readjust_to_vacuum_data_changes ()
{
  if (vacuum_Data.is_empty ())
    {
      // it doesn't matter
      return;
    }

  VACUUM_LOG_BLOCKID first_blockid = vacuum_Data.get_first_blockid ();
  if (m_blockid < first_blockid)
    {
      // cursor was left behind
      vacuum_er_log (VACUUM_ER_LOG_JOBS, "readjust cursor blockid from %lld to %lld",
                     (long long int) m_blockid, (long long int) first_blockid);
      m_blockid = first_blockid;
    }
}

void
vacuum_job_cursor::unload ()
{
  vacuum_er_log (VACUUM_ER_LOG_JOBS, "unload " vacuum_job_cursor_print_format, vacuum_job_cursor_print_args (*this));
  if (m_page != NULL)
    {
      vacuum_unfix_data_page (&cubthread::get_entry (), m_page);
    }
  m_index = vacuum_data_page::INDEX_NOT_FOUND;
}

void
vacuum_job_cursor::load ()
{
  assert (!is_loaded ());   // would not be optimal if already loaded

  search ();
  vacuum_er_log (VACUUM_ER_LOG_JOBS, "load " vacuum_job_cursor_print_format, vacuum_job_cursor_print_args (*this));
}

void
vacuum_job_cursor::reload ()
{
  if (m_page != NULL)
    {
      // check currently pointed page
      m_index = m_page->get_index_of_blockid (m_blockid);
      if (m_index == vacuum_data_page::INDEX_NOT_FOUND)
        {
          // not in page
          unload ();
        }
      else
        {
          // found in page, reload finished
          return;
        }
    }
  // must search for blockid
  search ();
}

void
vacuum_job_cursor::search ()
{
  assert (m_page == NULL);

  vacuum_data_page *data_page = vacuum_Data.first_page;
  assert (data_page != NULL);

  while (true)
    {
      m_index = data_page->get_index_of_blockid (m_blockid);
      if (m_index != vacuum_data_page::INDEX_NOT_FOUND)
        {
          m_page = data_page;
          return;
        }

      // advance to next page
      VPID next_vpid = data_page->next_page;
      vacuum_unfix_data_page (&cubthread::get_entry (), data_page);
      if (VPID_ISNULL (&next_vpid))
        {
          // no next page
          return;
        }
      data_page = vacuum_fix_data_page (&cubthread::get_entry (), &next_vpid);
    }
}

//
// vacuum_shutdown_sequence
//
vacuum_shutdown_sequence::vacuum_shutdown_sequence ()
  : m_state (NO_SHUTDOWN)
#if defined (SERVER_MODE)
  , m_state_mutex ()
  , m_condvar ()
#endif // SERVER_MODE
{
}

void
vacuum_shutdown_sequence::request_shutdown ()
{
#if defined (SERVER_MODE)
  if (m_state == SHUTDOWN_REGISTERED)
    {
      return;
    }
  std::unique_lock<std::mutex> ulock { m_state_mutex };
  assert (m_state == NO_SHUTDOWN);
  m_state = SHUTDOWN_REQUESTED;
  // must wait until shutdown is registered
  m_condvar.wait (ulock, [this] ()
    {
      return m_state == SHUTDOWN_REGISTERED || vacuum_Master_daemon == NULL;
    });
  if (m_state == SHUTDOWN_REQUESTED && vacuum_Master_daemon == NULL)
    {
      // no one to register, but myself
      m_state = SHUTDOWN_REGISTERED;
    }
  assert (m_state == SHUTDOWN_REGISTERED);
#else // SA_MODE
  m_state = SHUTDOWN_REGISTERED;
#endif // SA_MODE
}

bool
vacuum_shutdown_sequence::is_shutdown_requested ()
{
  return m_state != NO_SHUTDOWN;
}

bool
vacuum_shutdown_sequence::check_shutdown_request ()
{
  if (m_state == NO_SHUTDOWN)
    {
      return false;
    }
  else if (m_state == SHUTDOWN_REGISTERED)
    {
      return true;
    }
  else
    {
#if defined (SA_MODE)
      assert (false);
      return true;
#else // SERVER_MODE
      // register
      std::unique_lock<std::mutex> ulock { m_state_mutex };
      assert (m_state == SHUTDOWN_REQUESTED);
      m_state = SHUTDOWN_REGISTERED;
      ulock.unlock ();
      m_condvar.notify_one ();
      return true;
#endif
    }
}
// *INDENT-ON*
