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
 * double_write_buffer.c - double write buffer
 */

#ident "$Id$"

#include <assert.h>
#include <math.h>

#include "double_write_buffer.h"

#include "system_parameter.h"
#include "thread_daemon.hpp"
#include "thread_entry_task.hpp"
#include "thread_lockfree_hash_map.hpp"
#include "thread_manager.hpp"
#include "log_append.hpp"
#include "log_impl.h"
#include "log_volids.hpp"
#include "boot_sr.h"
#include "perf_monitor.h"
#include "porting_inline.hpp"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"


#define DWB_SLOTS_HASH_SIZE		    1000
#define DWB_SLOTS_FREE_LIST_SIZE	    100

/* These values must be power of two. */
#define DWB_MIN_SIZE			    (512 * 1024)
#define DWB_MAX_SIZE			    (32 * 1024 * 1024)
#define DWB_MIN_BLOCKS			    1
#define DWB_MAX_BLOCKS			    32

/* The total number of blocks. */
#define DWB_NUM_TOTAL_BLOCKS	   (dwb_Global.num_blocks)

/* The total number of pages. */
#define DWB_NUM_TOTAL_PAGES	   (dwb_Global.num_pages)

/* The number of pages in each block. */
#define DWB_BLOCK_NUM_PAGES	   (dwb_Global.num_block_pages)

/* LOG2 from total number of blocks. */
#define DWB_LOG2_BLOCK_NUM_PAGES	   (dwb_Global.log2_num_block_pages)


/* Position mask. */
#define DWB_POSITION_MASK	    0x000000003fffffff

/* Block status mask. */
#define DWB_BLOCKS_STATUS_MASK	    0xffffffff00000000

/* Structure modification mask. */
#define DWB_MODIFY_STRUCTURE	    0x0000000080000000

/* Create mask. */
#define DWB_CREATE		    0x0000000040000000

/* Create or modify mask. */
#define DWB_CREATE_OR_MODIFY_MASK   (DWB_CREATE | DWB_MODIFY_STRUCTURE)

/* Flags mask. */
#define DWB_FLAG_MASK		    (DWB_BLOCKS_STATUS_MASK | DWB_MODIFY_STRUCTURE | DWB_CREATE)


/* Get DWB position. */
#define DWB_GET_POSITION(position_with_flags) \
  ((position_with_flags) & DWB_POSITION_MASK)

/* Reset DWB position. */
#define DWB_RESET_POSITION(position_with_flags) \
  ((position_with_flags) & DWB_FLAG_MASK)

/* Get DWB block status. */
#define DWB_GET_BLOCK_STATUS(position_with_flags) \
  ((position_with_flags) & DWB_BLOCKS_STATUS_MASK)

/* Get block number from DWB position. */
#define DWB_GET_BLOCK_NO_FROM_POSITION(position_with_flags) \
  ((unsigned int) DWB_GET_POSITION (position_with_flags) >> (DWB_LOG2_BLOCK_NUM_PAGES))

/* Checks whether writing in a DWB block was started. */
#define DWB_IS_BLOCK_WRITE_STARTED(position_with_flags, block_no) \
  (assert (block_no < DWB_MAX_BLOCKS), ((position_with_flags) & (1ULL << (63 - (block_no)))) != 0)

/* Checks whether any DWB block was started. */
#define DWB_IS_ANY_BLOCK_WRITE_STARTED(position_with_flags) \
  (((position_with_flags) & DWB_BLOCKS_STATUS_MASK) != 0)

/* Starts DWB block writing. */
#define DWB_STARTS_BLOCK_WRITING(position_with_flags, block_no) \
  (assert (block_no < DWB_MAX_BLOCKS), (position_with_flags) | (1ULL << (63 - (block_no))))

/* Ends DWB block writing. */
#define DWB_ENDS_BLOCK_WRITING(position_with_flags, block_no) \
  (assert (DWB_IS_BLOCK_WRITE_STARTED (position_with_flags, block_no)), \
  (position_with_flags) & ~(1ULL << (63 - (block_no))))

/* Starts modifying DWB structure. */
#define DWB_STARTS_MODIFYING_STRUCTURE(position_with_flags) \
  ((position_with_flags) | DWB_MODIFY_STRUCTURE)

/* Ends modifying DWB structure. */
#define DWB_ENDS_MODIFYING_STRUCTURE(position_with_flags) \
  (assert (DWB_IS_MODIFYING_STRUCTURE (position_with_flags)), (position_with_flags) & ~DWB_MODIFY_STRUCTURE)

/* Check whether the DWB structure is modifying. */
#define DWB_IS_MODIFYING_STRUCTURE(position_with_flags) \
  (((position_with_flags) & DWB_MODIFY_STRUCTURE) != 0)

/* Starts DWB creation. */
#define DWB_STARTS_CREATION(position_with_flags) \
  ((position_with_flags) | DWB_CREATE)

/* Starts DWB creation. */
#define DWB_ENDS_CREATION(position_with_flags) \
  (assert (DWB_IS_CREATED (position_with_flags)), (position_with_flags) & ~DWB_CREATE)

/* Check whether the DWB was created. */
#define DWB_IS_CREATED(position_with_flags) \
  (((position_with_flags) & DWB_CREATE) != 0)

/* Check whether the DWB structure is not created or is modifying. */
#define DWB_NOT_CREATED_OR_MODIFYING(position_with_flags) \
  (((position_with_flags) & DWB_CREATE_OR_MODIFY_MASK) != DWB_CREATE)

/* Get next DWB position with flags. */
#define DWB_GET_NEXT_POSITION_WITH_FLAGS(position_with_flags) \
  ((DWB_GET_POSITION (position_with_flags)) == (DWB_NUM_TOTAL_PAGES - 1) \
   ? ((position_with_flags) & DWB_FLAG_MASK) : ((position_with_flags) + 1))

/* Get position in DWB block from DWB position with flags. */
#define DWB_GET_POSITION_IN_BLOCK(position_with_flags) \
  ((DWB_GET_POSITION (position_with_flags)) & (DWB_BLOCK_NUM_PAGES - 1))

/* Get DWB previous block number. */
#define DWB_GET_PREV_BLOCK_NO(block_no) \
  ((block_no) > 0 ? ((block_no) - 1) : (DWB_NUM_TOTAL_BLOCKS - 1))

/* Get DWB previous block. */
#define DWB_GET_PREV_BLOCK(block_no) \
  (&(dwb_Global.blocks[DWB_GET_PREV_BLOCK_NO(block_no)]))

/* Get DWB next block number. */
#define DWB_GET_NEXT_BLOCK_NO(block_no) \
  ((block_no) == (DWB_NUM_TOTAL_BLOCKS - 1) ? 0 : ((block_no) + 1))

/* Get block version. */
#define DWB_GET_BLOCK_VERSION(block) \
  (ATOMIC_INC_64 (&block->version, 0ULL))

/* Queue entry. */
typedef struct double_write_wait_queue_entry DWB_WAIT_QUEUE_ENTRY;
struct double_write_wait_queue_entry
{
  void *data;			/* The data field. */
  DWB_WAIT_QUEUE_ENTRY *next;	/* The next queue entry field. */
};

/* DWB queue.  */
typedef struct double_write_wait_queue DWB_WAIT_QUEUE;
struct double_write_wait_queue
{
  DWB_WAIT_QUEUE_ENTRY *head;	/* Queue head. */
  DWB_WAIT_QUEUE_ENTRY *tail;	/* Queue tail. */
  DWB_WAIT_QUEUE_ENTRY *free_list;	/* Queue free list */

  int count;			/* Count queue elements. */
  int free_count;		/* Count free list elements. */
};
#define DWB_WAIT_QUEUE_INITIALIZER	{NULL, NULL, NULL, 0, 0}

/* Flush volume status. */
typedef enum
{
  VOLUME_NOT_FLUSHED,
  VOLUME_FLUSHED_BY_DWB_FILE_SYNC_HELPER_THREAD,
  VOLUME_FLUSHED_BY_DWB_FLUSH_THREAD
} FLUSH_VOLUME_STATUS;

typedef struct flush_volume_info FLUSH_VOLUME_INFO;
struct flush_volume_info
{
  int vdes;			/* The volume descriptor. */
  volatile int num_pages;	/* The number of pages to flush in volume. */
  volatile bool all_pages_written;	/* True, if all pages written. */
  volatile FLUSH_VOLUME_STATUS flushed_status;	/* Flush status. */
};

/* DWB block. */
typedef struct double_write_block DWB_BLOCK;
struct double_write_block
{
  FLUSH_VOLUME_INFO *flush_volumes_info;	/* Information about volumes to flush. */
  volatile unsigned int count_flush_volumes_info;	/* Count volumes to flush. */
  unsigned int max_to_flush_vdes;	/* Maximum volumes to flush. */

  pthread_mutex_t mutex;	/* The mutex to protect the queue. */
  DWB_WAIT_QUEUE wait_queue;	/* The wait queue for the current block. */

  char *write_buffer;		/* The block write buffer, used to write all pages once. */
  DWB_SLOT *slots;		/* The slots containing the data. Used to write individual pages. */
  volatile unsigned int count_wb_pages;	/* Count the pages added to write buffer. */

  unsigned int block_no;	/* The block number. */
  volatile UINT64 version;	/* The block version. */
  volatile bool all_pages_written;	/* True, if all pages are written */
};

/* Slots hash entry. */
typedef struct dwb_slots_hash_entry DWB_SLOTS_HASH_ENTRY;
struct dwb_slots_hash_entry
{
  VPID vpid;			/* Page VPID. */

  DWB_SLOTS_HASH_ENTRY *stack;	/* Used in freelist. */
  DWB_SLOTS_HASH_ENTRY *next;	/* Used in hash table. */
  pthread_mutex_t mutex;	/* The mutex. */
  UINT64 del_id;		/* Delete transaction ID (for lock free). */

  DWB_SLOT *slot;		/* DWB slot containing a page. */

  // *INDENT-OFF*
  dwb_slots_hash_entry ()
  {
    pthread_mutex_init (&mutex, NULL);
  }
  ~dwb_slots_hash_entry ()
  {
    pthread_mutex_destroy (&mutex);
  }
  // *INDENT-ON*
};

/* Hash that store all pages in DWB. */
// *INDENT-OFF*
using dwb_hashmap_type = cubthread::lockfree_hashmap<VPID, dwb_slots_hash_entry>;
// *INDENT-ON*

/* The double write buffer structure. */
typedef struct double_write_buffer DOUBLE_WRITE_BUFFER;
struct double_write_buffer
{
  bool logging_enabled;
  DWB_BLOCK *blocks;		/* The blocks in DWB. */
  unsigned int num_blocks;	/* The total number of blocks in DWB - power of 2. */
  unsigned int num_pages;	/* The total number of pages in DWB - power of 2. */
  unsigned int num_block_pages;	/* The number of pages in a block - power of 2. */
  unsigned int log2_num_block_pages;	/* Logarithm from block number of pages. */
  volatile unsigned int blocks_flush_counter;	/* The blocks flush counter. */
  volatile unsigned int next_block_to_flush;	/* Next block to flush */

  pthread_mutex_t mutex;	/* The mutex to protect the wait queue. */
  DWB_WAIT_QUEUE wait_queue;	/* The wait queue, used when the DWB structure changed. */

  UINT64 volatile position_with_flags;	/* The current position in double write buffer and flags. Flags keep the
					 * state of each block (started, ended), create DWB status, modify DWB status.
					 */
  dwb_hashmap_type slots_hashmap;	/* The slots hash. */
  int vdes;			/* The volume file descriptor. */

  DWB_BLOCK *volatile file_sync_helper_block;	/* The block that will be sync by helper thread. */

  // *INDENT-OFF*
  double_write_buffer ()
    : logging_enabled (false)
    , blocks (NULL)
    , num_blocks (0)
    , num_pages (0)
    , num_block_pages (0)
    , log2_num_block_pages (0)
    , blocks_flush_counter (0)
    , next_block_to_flush (0)
    , mutex PTHREAD_MUTEX_INITIALIZER
    , wait_queue DWB_WAIT_QUEUE_INITIALIZER
    , position_with_flags (0)
    , slots_hashmap {}
    , vdes (NULL_VOLDES)
    , file_sync_helper_block (NULL)
  {
  }
  // *INDENT-ON*
};

/* DWB volume name. */
char dwb_Volume_name[PATH_MAX];

/* DWB. */
static DOUBLE_WRITE_BUFFER dwb_Global;

#define dwb_Log dwb_Global.logging_enabled

#define dwb_check_logging() (dwb_Log = prm_get_bool_value (PRM_ID_DWB_LOGGING))
#define dwb_log(...) if (dwb_Log) _er_log_debug (ARG_FILE_LINE, "DWB: " __VA_ARGS__)
#define dwb_log_error(...) if (dwb_Log) _er_log_debug (ARG_FILE_LINE, "DWB ERROR: " __VA_ARGS__)

#if !defined(SERVER_MODE)
#define pthread_mutex_init(a, b)
#define pthread_mutex_destroy(a)
#define pthread_mutex_lock(a)	0
#define pthread_mutex_unlock(a)
#endif /* !SERVER_MODE */

/* DWB wait queue functions */
STATIC_INLINE void dwb_init_wait_queue (DWB_WAIT_QUEUE * wait_queue) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE DWB_WAIT_QUEUE_ENTRY *dwb_make_wait_queue_entry (DWB_WAIT_QUEUE * wait_queue,
							       void *data) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE DWB_WAIT_QUEUE_ENTRY *dwb_block_add_wait_queue_entry (DWB_WAIT_QUEUE * wait_queue,
								    void *data) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE DWB_WAIT_QUEUE_ENTRY *dwb_block_disconnect_wait_queue_entry (DWB_WAIT_QUEUE *
									   wait_queue, void *data)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void dwb_block_free_wait_queue_entry (DWB_WAIT_QUEUE * wait_queue,
						    DWB_WAIT_QUEUE_ENTRY * wait_queue_entry,
						    int (*func) (void *)) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void dwb_remove_wait_queue_entry (DWB_WAIT_QUEUE * wait_queue, pthread_mutex_t * mutex,
						void *data, int (*func) (void *)) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void dwb_signal_waiting_threads (DWB_WAIT_QUEUE * wait_queue, pthread_mutex_t * mutex)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void dwb_destroy_wait_queue (DWB_WAIT_QUEUE * wait_queue, pthread_mutex_t * mutex);

/* DWB functions */
STATIC_INLINE void dwb_power2_ceil (unsigned int min, unsigned int max, unsigned int *p_value)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE bool dwb_load_buffer_size (unsigned int *p_double_write_buffer_size) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE bool dwb_load_block_count (unsigned int *p_num_blocks) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int dwb_wait_for_block_completion (THREAD_ENTRY * thread_p, unsigned int block_no)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int dwb_signal_waiting_thread (void *data) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int dwb_set_status_resumed (void *data) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void dwb_signal_block_completion (THREAD_ENTRY * thread_p, DWB_BLOCK * dwb_block)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int dwb_block_create_ordered_slots (DWB_BLOCK * block, DWB_SLOT ** p_dwb_ordered_slots,
						  unsigned int *p_ordered_slots_length) __attribute__ ((ALWAYS_INLINE));
static int dwb_compare_vol_fd (const void *v1, const void *v2);
STATIC_INLINE FLUSH_VOLUME_INFO *dwb_add_volume_to_block_flush_area (THREAD_ENTRY * thread_p, DWB_BLOCK * block,
								     int vol_fd) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int dwb_write_block (THREAD_ENTRY * thread_p, DWB_BLOCK * block, DWB_SLOT * p_dwb_slots,
				   unsigned int ordered_slots_length, bool file_sync_helper_can_flush,
				   bool remove_from_hash) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int dwb_flush_block (THREAD_ENTRY * thread_p, DWB_BLOCK * block, bool file_sync_helper_can_flush,
				   UINT64 * current_position_with_flags) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void dwb_init_slot (DWB_SLOT * slot) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int dwb_acquire_next_slot (THREAD_ENTRY * thread_p, bool can_wait, DWB_SLOT ** p_dwb_slot)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void dwb_set_slot_data (THREAD_ENTRY * thread_p, DWB_SLOT * dwb_slot,
				      FILEIO_PAGE * io_page_p) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int dwb_wait_for_strucure_modification (THREAD_ENTRY * thread_p) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void dwb_signal_structure_modificated (THREAD_ENTRY * thread_p) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int dwb_starts_structure_modification (THREAD_ENTRY * thread_p, UINT64 * current_position_with_flags)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void dwb_ends_structure_modification (THREAD_ENTRY * thread_p, UINT64 current_position_with_flags)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void dwb_destroy_internal (THREAD_ENTRY * thread_p, UINT64 * current_position_with_flags)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void dwb_initialize_slot (DWB_SLOT * slot, FILEIO_PAGE * io_page,
					unsigned int position_in_block, unsigned int block_no)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void dwb_initialize_block (DWB_BLOCK * block, unsigned int block_no,
					 unsigned int count_wb_pages, char *write_buffer, DWB_SLOT * slots,
					 FLUSH_VOLUME_INFO * flush_volumes_info, unsigned int count_flush_volumes_info,
					 unsigned int max_to_flush_vdes) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int dwb_create_blocks (THREAD_ENTRY * thread_p, unsigned int num_blocks, unsigned int num_block_pages,
				     DWB_BLOCK ** p_blocks) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void dwb_finalize_block (DWB_BLOCK * block) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int dwb_create_internal (THREAD_ENTRY * thread_p, const char *dwb_volume_name,
				       UINT64 * current_position_with_flags) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void dwb_get_next_block_for_flush (THREAD_ENTRY * thread_p, unsigned int *block_no)
  __attribute__ ((ALWAYS_INLINE));

/* Slots hash functions. */
static void *dwb_slots_hash_entry_alloc (void);
static int dwb_slots_hash_entry_free (void *entry);
static int dwb_slots_hash_entry_init (void *entry);
static int dwb_slots_hash_key_copy (void *src, void *dest);
static int dwb_slots_hash_compare_key (void *key1, void *key2);
static unsigned int dwb_slots_hash_key (void *key, int hash_table_size);

STATIC_INLINE int dwb_slots_hash_insert (THREAD_ENTRY * thread_p, VPID * vpid, DWB_SLOT * slot, bool * inserted)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int dwb_slots_hash_delete (THREAD_ENTRY * thread_p, DWB_SLOT * slot);

// *INDENT-OFF*
#if defined (SERVER_MODE)
static cubthread::daemon *dwb_flush_block_daemon = NULL;
static cubthread::daemon *dwb_file_sync_helper_daemon = NULL;
#endif
// *INDENT-ON*

static bool dwb_is_flush_block_daemon_available (void);
static bool dwb_is_file_sync_helper_daemon_available (void);

static bool dwb_flush_block_daemon_is_running (void);
static bool dwb_file_sync_helper_daemon_is_running (void);

static int dwb_file_sync_helper (THREAD_ENTRY * thread_p);
static int dwb_flush_next_block (THREAD_ENTRY * thread_p);

#if !defined (NDEBUG)
static int dwb_debug_check_dwb (THREAD_ENTRY * thread_p, DWB_SLOT * p_dwb_ordered_slots, unsigned int num_pages);
#endif // DEBUG
static int dwb_check_data_page_is_sane (THREAD_ENTRY * thread_p, DWB_BLOCK * rcv_block, DWB_SLOT * p_dwb_ordered_slots,
					int *p_num_recoverable_pages);

/* Slots entry descriptor */
static LF_ENTRY_DESCRIPTOR slots_entry_Descriptor = {
  /* offsets */
  offsetof (DWB_SLOTS_HASH_ENTRY, stack),
  offsetof (DWB_SLOTS_HASH_ENTRY, next),
  offsetof (DWB_SLOTS_HASH_ENTRY, del_id),
  offsetof (DWB_SLOTS_HASH_ENTRY, vpid),
  offsetof (DWB_SLOTS_HASH_ENTRY, mutex),

  /* using mutex? */
  LF_EM_USING_MUTEX,

  LF_ENTRY_DESCRIPTOR_MAX_ALLOC,
  dwb_slots_hash_entry_alloc,
  dwb_slots_hash_entry_free,
  dwb_slots_hash_entry_init,
  NULL,
  dwb_slots_hash_key_copy,
  dwb_slots_hash_compare_key,
  dwb_slots_hash_key,
  NULL				/* no inserts */
};

/*
 * dwb_init_wait_queue () - Intialize wait queue.
 *
 * return   : Nothing.
 * wait_queue (in/out) : The wait queue.
 */
STATIC_INLINE void
dwb_init_wait_queue (DWB_WAIT_QUEUE * wait_queue)
{
  wait_queue->head = NULL;
  wait_queue->tail = NULL;
  wait_queue->count = 0;

  wait_queue->free_list = NULL;
  wait_queue->free_count = 0;
}

/*
 * dwb_make_wait_queue_entry () - Make wait queue entry.
 *
 * return   : The queue entry.
 * wait_queue (in/out) : The wait queue.
 * data (in): The data.
 */
STATIC_INLINE DWB_WAIT_QUEUE_ENTRY *
dwb_make_wait_queue_entry (DWB_WAIT_QUEUE * wait_queue, void *data)
{
  DWB_WAIT_QUEUE_ENTRY *wait_queue_entry;

  assert (wait_queue != NULL && data != NULL);

  if (wait_queue->free_list != NULL)
    {
      wait_queue_entry = wait_queue->free_list;
      wait_queue->free_list = wait_queue->free_list->next;
      wait_queue->free_count--;
    }
  else
    {
      wait_queue_entry = (DWB_WAIT_QUEUE_ENTRY *) malloc (sizeof (DWB_WAIT_QUEUE_ENTRY));
      if (wait_queue_entry == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (DWB_WAIT_QUEUE_ENTRY));
	  return NULL;
	}
    }

  wait_queue_entry->data = data;
  wait_queue_entry->next = NULL;
  return wait_queue_entry;
}

/*
 * dwb_block_add_wait_queue_entry () - Add entry to wait queue.
 *
 * return   : Added queue entry.
 * wait_queue (in/out) : The wait queue.
 * data (in): The data.
 *
 *  Note: Is user responsibility to protect against queue concurrent access.
 */
STATIC_INLINE DWB_WAIT_QUEUE_ENTRY *
dwb_block_add_wait_queue_entry (DWB_WAIT_QUEUE * wait_queue, void *data)
{
  DWB_WAIT_QUEUE_ENTRY *wait_queue_entry = NULL;

  assert (wait_queue != NULL && data != NULL);

  wait_queue_entry = dwb_make_wait_queue_entry (wait_queue, data);
  if (wait_queue_entry == NULL)
    {
      return NULL;
    }

  if (wait_queue->head == NULL)
    {
      wait_queue->tail = wait_queue->head = wait_queue_entry;
    }
  else
    {
      wait_queue->tail->next = wait_queue_entry;
      wait_queue->tail = wait_queue_entry;
    }
  wait_queue->count++;

  return wait_queue_entry;
}

/*
 * dwb_block_disconnect_wait_queue_entry () - Disconnect entry from wait queue.
 *
 * return   : Disconnected queue entry.
 * wait_queue (in/out) : The wait queue.
 * data (in): The data.
 *   Note: If data is NULL, then first entry will be disconnected.
 */
STATIC_INLINE DWB_WAIT_QUEUE_ENTRY *
dwb_block_disconnect_wait_queue_entry (DWB_WAIT_QUEUE * wait_queue, void *data)
{
  DWB_WAIT_QUEUE_ENTRY *wait_queue_entry = NULL, *prev_wait_queue_entry = NULL;

  assert (wait_queue != NULL);

  if (wait_queue->head == NULL)
    {
      return NULL;
    }

  prev_wait_queue_entry = NULL;
  wait_queue_entry = wait_queue->head;
  if (data != NULL)
    {
      /* search for data */
      while (wait_queue_entry != NULL)
	{
	  if (wait_queue_entry->data == data)
	    {
	      break;
	    }
	  prev_wait_queue_entry = wait_queue_entry;
	  wait_queue_entry = wait_queue_entry->next;
	}
    }

  if (wait_queue_entry == NULL)
    {
      return NULL;
    }

  if (prev_wait_queue_entry != NULL)
    {
      prev_wait_queue_entry->next = wait_queue_entry->next;
    }
  else
    {
      assert (wait_queue_entry == wait_queue->head);
      wait_queue->head = wait_queue_entry->next;
    }

  if (wait_queue_entry == wait_queue->tail)
    {
      wait_queue->tail = prev_wait_queue_entry;
    }

  wait_queue_entry->next = NULL;
  wait_queue->count--;

  return wait_queue_entry;
}

/*
 * dwb_block_free_wait_queue_entry () - Free a wait queue entry.
 *
 * return   : Nothing.
 * wait_queue (in/out) : The wait queue.
 * wait_queue_entry (in): The wait queue entry.
 * func(in): The function to apply on entry.
 */
STATIC_INLINE void
dwb_block_free_wait_queue_entry (DWB_WAIT_QUEUE * wait_queue, DWB_WAIT_QUEUE_ENTRY * wait_queue_entry,
				 int (*func) (void *))
{
  if (wait_queue_entry == NULL)
    {
      return;
    }

  if (func != NULL)
    {
      (void) func (wait_queue_entry);
    }

  /* Reuse the entry. Do not set data field to NULL. It may be used at debugging. */
  wait_queue_entry->next = wait_queue->free_list;
  wait_queue->free_list = wait_queue_entry;
  wait_queue->free_count++;
}

/*
 * dwb_remove_wait_queue_entry () - Remove the wait queue entry.
 *
 * return   : True, if entry removed, false otherwise.
 * wait_queue (in/out): The wait queue.
 * mutex (in): The mutex to protect the wait queue.
 * data (in): The data.
 * func(in): The function to apply on each entry.
 *
 *  Note: If the data is NULL, the first entry will be removed.
 */
STATIC_INLINE void
dwb_remove_wait_queue_entry (DWB_WAIT_QUEUE * wait_queue, pthread_mutex_t * mutex, void *data, int (*func) (void *))
{
  DWB_WAIT_QUEUE_ENTRY *wait_queue_entry = NULL;

  if (mutex != NULL)
    {
      (void) pthread_mutex_lock (mutex);
    }

  wait_queue_entry = dwb_block_disconnect_wait_queue_entry (wait_queue, data);
  if (wait_queue_entry != NULL)
    {
      dwb_block_free_wait_queue_entry (wait_queue, wait_queue_entry, func);
    }

  if (mutex != NULL)
    {
      pthread_mutex_unlock (mutex);
    }
}

/*
 * dwb_signal_waiting_threads () - Signal waiting threads.
 *
 * return   : Nothing.
 * wait_queue (in/out): The wait queue.
 * mutex(in): The mutex to protect the queue.
 */
STATIC_INLINE void
dwb_signal_waiting_threads (DWB_WAIT_QUEUE * wait_queue, pthread_mutex_t * mutex)
{
  assert (wait_queue != NULL);

  if (mutex != NULL)
    {
      (void) pthread_mutex_lock (mutex);
    }

  while (wait_queue->head != NULL)
    {
      dwb_remove_wait_queue_entry (wait_queue, NULL, NULL, dwb_signal_waiting_thread);
    }

  if (mutex != NULL)
    {
      pthread_mutex_unlock (mutex);
    }
}

/*
 * dwb_destroy_wait_queue () - Destroy the wait queue.
 *
 * return   : Nothing.
 * wait_queue (in/out): The wait queue.
 * mutex(in): The mutex to protect the queue.
 */
STATIC_INLINE void
dwb_destroy_wait_queue (DWB_WAIT_QUEUE * wait_queue, pthread_mutex_t * mutex)
{
  DWB_WAIT_QUEUE_ENTRY *wait_queue_entry;

  assert (wait_queue != NULL);

  if (mutex != NULL)
    {
      (void) pthread_mutex_lock (mutex);
    }

  dwb_signal_waiting_threads (wait_queue, NULL);

  if (wait_queue->free_count > 0)
    {
      while (wait_queue->free_list != NULL)
	{
	  wait_queue_entry = wait_queue->free_list;
	  wait_queue->free_list = wait_queue_entry->next;
	  free_and_init (wait_queue_entry);
	  wait_queue->free_count--;
	}
    }

  if (mutex != NULL)
    {
      pthread_mutex_unlock (mutex);
    }
}

/*
 * dwb_power2_ceil () - Adjust value to power of 2.
 *
 * return   : Error code.
 * min (in) : minimum of value to be adjusted.
 * max (in) : maximum of value to be adjusted.
 * p_value (in/out) : value to be adjusted.
 *
 *  Note: The adjusted value is a power of 2 that is greater than the value.
 */
STATIC_INLINE void
dwb_power2_ceil (unsigned int min, unsigned int max, unsigned int *p_value)
{
  unsigned int limit;

  if (*p_value < min)
    {
      *p_value = min;
    }
  else if (*p_value > max)
    {
      *p_value = max;
    }
  else if (!IS_POWER_OF_2 (*p_value))
    {
      limit = min << 1;

      while (limit < *p_value)
	{
	  limit = limit << 1;
	}

      *p_value = limit;
    }

  assert (IS_POWER_OF_2 (*p_value));
  assert (*p_value >= min && *p_value <= max);
}

/*
 * dwb_load_buffer_size () - Load the buffer size value of double write buffer.
 *
 * return   : true or false.
 * p_double_write_buffer_size (in/out): the buffer size of double write buffer.
 *
 *  Note: The buffer size must be a multiple of 512 K.
 */
STATIC_INLINE bool
dwb_load_buffer_size (unsigned int *p_double_write_buffer_size)
{
  assert (IS_POWER_OF_2 (DWB_MIN_SIZE) && IS_POWER_OF_2 (DWB_MAX_SIZE));
  assert (DWB_MAX_SIZE >= DWB_MIN_SIZE);

  *p_double_write_buffer_size = prm_get_integer_value (PRM_ID_DWB_SIZE);
  if (*p_double_write_buffer_size == 0)
    {
      /* Do not use double write buffer. */
      return false;
    }

  dwb_power2_ceil (DWB_MIN_SIZE, DWB_MAX_SIZE, p_double_write_buffer_size);

  return true;
}

/*
 * dwb_load_block_count () - Load the block count value of double write buffer.
 *
 * return   : true or false.
 * p_num_blocks (in/out): the block count of double write buffer.
 *
 *  Note: The number of blocks must be a power of 2.
 */
STATIC_INLINE bool
dwb_load_block_count (unsigned int *p_num_blocks)
{
  assert (IS_POWER_OF_2 (DWB_MIN_BLOCKS) && IS_POWER_OF_2 (DWB_MAX_BLOCKS));
  assert (DWB_MAX_BLOCKS >= DWB_MIN_BLOCKS);

  *p_num_blocks = prm_get_integer_value (PRM_ID_DWB_BLOCKS);
  if (*p_num_blocks == 0)
    {
      /* Do not use double write buffer. */
      return false;
    }

  dwb_power2_ceil (DWB_MIN_BLOCKS, DWB_MAX_BLOCKS, p_num_blocks);

  return true;
}

/*
 * dwb_starts_structure_modification () - Starts structure modifications.
 *
 * return   : Error code
 * thread_p (in): The thread entry.
 * current_position_with_flags(out): The current position with flags.
 *
 *  Note: This function must be called before changing structure of DWB.
 */
STATIC_INLINE int
dwb_starts_structure_modification (THREAD_ENTRY * thread_p, UINT64 * current_position_with_flags)
{
  UINT64 local_current_position_with_flags, new_position_with_flags, min_version;
  unsigned int block_no;
  int error_code = NO_ERROR;
  unsigned int start_block_no, blocks_count;
  DWB_BLOCK *file_sync_helper_block;

  assert (current_position_with_flags != NULL);

  do
    {
      local_current_position_with_flags = ATOMIC_INC_64 (&dwb_Global.position_with_flags, 0ULL);
      if (DWB_IS_MODIFYING_STRUCTURE (local_current_position_with_flags))
	{
	  /* Only one thread can change the structure */
	  return ER_FAILED;
	}

      new_position_with_flags = DWB_STARTS_MODIFYING_STRUCTURE (local_current_position_with_flags);
      /* Start structure modifications, the threads that want to flush afterwards, have to wait. */
    }
  while (!ATOMIC_CAS_64 (&dwb_Global.position_with_flags, local_current_position_with_flags, new_position_with_flags));

#if defined(SERVER_MODE)
  while ((ATOMIC_INC_32 (&dwb_Global.blocks_flush_counter, 0) > 0)
	 || dwb_flush_block_daemon_is_running () || dwb_file_sync_helper_daemon_is_running ())
    {
      /* Can't modify structure while flush thread can access DWB. */
      thread_sleep (20);
    }
#endif

  /* Since we set the modify structure flag, I'm the only thread that access the DWB. */
  file_sync_helper_block = dwb_Global.file_sync_helper_block;
  if (file_sync_helper_block != NULL)
    {
      /* All remaining blocks are flushed by me. */
      (void) ATOMIC_TAS_ADDR (&dwb_Global.file_sync_helper_block, (DWB_BLOCK *) NULL);
      dwb_log ("Structure modification, needs to flush DWB block = %d having version %lld\n",
	       file_sync_helper_block->block_no, file_sync_helper_block->version);
    }

  local_current_position_with_flags = ATOMIC_INC_64 (&dwb_Global.position_with_flags, 0ULL);

  /* Need to flush incomplete blocks, ordered by version. */
  start_block_no = DWB_NUM_TOTAL_BLOCKS;
  min_version = 0xffffffffffffffff;
  blocks_count = 0;
  for (block_no = 0; block_no < DWB_NUM_TOTAL_BLOCKS; block_no++)
    {
      if (DWB_IS_BLOCK_WRITE_STARTED (local_current_position_with_flags, block_no))
	{
	  if (dwb_Global.blocks[block_no].version < min_version)
	    {
	      min_version = dwb_Global.blocks[block_no].version;
	      start_block_no = block_no;
	    }
	  blocks_count++;
	}
    }

  block_no = start_block_no;
  while (blocks_count > 0)
    {
      if (DWB_IS_BLOCK_WRITE_STARTED (local_current_position_with_flags, block_no))
	{
	  /* Flush all pages from current block. I must flush all remaining data. */
	  error_code =
	    dwb_flush_block (thread_p, &dwb_Global.blocks[block_no], false, &local_current_position_with_flags);
	  if (error_code != NO_ERROR)
	    {
	      /* Something wrong happened. */
	      dwb_log_error ("Can't flush block = %d having version %lld\n", block_no,
			     dwb_Global.blocks[block_no].version);

	      return error_code;
	    }

	  dwb_log_error ("DWB flushed %d block having version %lld\n", block_no, dwb_Global.blocks[block_no].version);
	  blocks_count--;
	}

      block_no = (block_no + 1) % DWB_NUM_TOTAL_BLOCKS;
    }

  local_current_position_with_flags = ATOMIC_INC_64 (&dwb_Global.position_with_flags, 0ULL);
  assert (DWB_GET_BLOCK_STATUS (local_current_position_with_flags) == 0);

  *current_position_with_flags = local_current_position_with_flags;

  return NO_ERROR;
}

/*
 * dwb_ends_structure_modification () - Ends structure modifications.
 *
 * return   : Error code.
 * thread_p (in): The thread entry.
 * current_position_with_flags(in): The current position with flags.
 */
STATIC_INLINE void
dwb_ends_structure_modification (THREAD_ENTRY * thread_p, UINT64 current_position_with_flags)
{
  UINT64 new_position_with_flags;

  new_position_with_flags = DWB_ENDS_MODIFYING_STRUCTURE (current_position_with_flags);

  /* Ends structure modifications. */
  assert (dwb_Global.position_with_flags == current_position_with_flags);

  ATOMIC_TAS_64 (&dwb_Global.position_with_flags, new_position_with_flags);

  /* Signal the other threads. */
  dwb_signal_structure_modificated (thread_p);
}

/*
 * dwb_initialize_slot () - Initialize a DWB slot.
 *
 * return   : Error code.
 * slot (in/out) : The slot to initialize.
 * io_page (in) : The page.
 * position_in_block(in): The position in DWB block.
 * block_no(in): The number of the block where the slot reside.
 */
STATIC_INLINE void
dwb_initialize_slot (DWB_SLOT * slot, FILEIO_PAGE * io_page, unsigned int position_in_block, unsigned int block_no)
{
  assert (slot != NULL && io_page != NULL);

  slot->io_page = io_page;
  if (io_page != NULL)
    {
      VPID_SET (&slot->vpid, io_page->prv.volid, io_page->prv.pageid);
      LSA_COPY (&slot->lsa, &io_page->prv.lsa);
    }

  slot->position_in_block = position_in_block;
  slot->block_no = block_no;
}

/*
 * dwb_initialize_block () - Initialize a block.
 *
 * return   : Nothing.
 * block (in/out) : Double write buffer block.
 * block_no (in) : Database name.
 * count_wb_pages (in): Count DWB pages.
 * write_buffer(in): The write buffer.
 * slots(in): The slots.
 * flush_volumes_info(in): The area containing volume descriptors to flush.
 * count_flush_volumes_info(in): Count volumes to flush.
 * max_to_flush_vdes(in): The maximum volumes to flush.
 */
STATIC_INLINE void
dwb_initialize_block (DWB_BLOCK * block, unsigned int block_no, unsigned int count_wb_pages, char *write_buffer,
		      DWB_SLOT * slots, FLUSH_VOLUME_INFO * flush_volumes_info, unsigned int count_flush_volumes_info,
		      unsigned int max_to_flush_vdes)
{
  assert (block != NULL);

  block->flush_volumes_info = flush_volumes_info;
  block->count_flush_volumes_info = count_flush_volumes_info;	/* Current volume descriptors to flush. */
  block->max_to_flush_vdes = max_to_flush_vdes;	/* Maximum volume descriptors to flush. */

  pthread_mutex_init (&block->mutex, NULL);
  dwb_init_wait_queue (&block->wait_queue);

  block->write_buffer = write_buffer;
  block->slots = slots;
  block->count_wb_pages = count_wb_pages;
  block->block_no = block_no;
  block->version = 0;
  block->all_pages_written = false;
}

/*
 * dwb_create_blocks () - Create the blocks.
 *
 * return   : Error code.
 * thread_p (in) : The thread entry.
 * num_blocks(in): The number of blocks.
 * num_block_pages(in): The number of block pages.
 * p_blocks(out): The created blocks.
 */
STATIC_INLINE int
dwb_create_blocks (THREAD_ENTRY * thread_p, unsigned int num_blocks, unsigned int num_block_pages,
		   DWB_BLOCK ** p_blocks)
{
  DWB_BLOCK *blocks = NULL;
  char *blocks_write_buffer[DWB_MAX_BLOCKS];
  FLUSH_VOLUME_INFO *flush_volumes_info[DWB_MAX_BLOCKS];
  DWB_SLOT *slots[DWB_MAX_BLOCKS];
  unsigned int block_buffer_size, i, j;
  int error_code;
  FILEIO_PAGE *io_page;

  assert (num_blocks <= DWB_MAX_BLOCKS);

  *p_blocks = NULL;

  for (i = 0; i < DWB_MAX_BLOCKS; i++)
    {
      blocks_write_buffer[i] = NULL;
      slots[i] = NULL;
      flush_volumes_info[i] = NULL;
    }

  blocks = (DWB_BLOCK *) malloc (num_blocks * sizeof (DWB_BLOCK));
  if (blocks == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, num_blocks * sizeof (DWB_BLOCK));
      error_code = ER_OUT_OF_VIRTUAL_MEMORY;
      goto exit_on_error;
    }
  memset (blocks, 0, num_blocks * sizeof (DWB_BLOCK));

  block_buffer_size = num_block_pages * IO_PAGESIZE;
  for (i = 0; i < num_blocks; i++)
    {
      blocks_write_buffer[i] = (char *) malloc (block_buffer_size * sizeof (char));
      if (blocks_write_buffer[i] == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, block_buffer_size * sizeof (char));
	  error_code = ER_OUT_OF_VIRTUAL_MEMORY;
	  goto exit_on_error;
	}
      memset (blocks_write_buffer[i], 0, block_buffer_size * sizeof (char));
    }

  for (i = 0; i < num_blocks; i++)
    {
      slots[i] = (DWB_SLOT *) malloc (num_block_pages * sizeof (DWB_SLOT));
      if (slots[i] == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, num_block_pages * sizeof (DWB_SLOT));
	  error_code = ER_OUT_OF_VIRTUAL_MEMORY;
	  goto exit_on_error;
	}
      memset (slots[i], 0, num_block_pages * sizeof (DWB_SLOT));
    }

  for (i = 0; i < num_blocks; i++)
    {
      flush_volumes_info[i] = (FLUSH_VOLUME_INFO *) malloc (num_block_pages * sizeof (FLUSH_VOLUME_INFO));
      if (flush_volumes_info[i] == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
		  num_block_pages * sizeof (FLUSH_VOLUME_INFO));
	  error_code = ER_OUT_OF_VIRTUAL_MEMORY;
	  goto exit_on_error;
	}
      memset (flush_volumes_info[i], 0, num_block_pages * sizeof (FLUSH_VOLUME_INFO));
    }

  for (i = 0; i < num_blocks; i++)
    {
      /* No need to initialize FILEIO_PAGE header here, since is overwritten before flushing */
      for (j = 0; j < num_block_pages; j++)
	{
	  io_page = (FILEIO_PAGE *) (blocks_write_buffer[i] + j * IO_PAGESIZE);

	  fileio_initialize_res (thread_p, io_page, IO_PAGESIZE);
	  dwb_initialize_slot (&slots[i][j], io_page, j, i);
	}

      dwb_initialize_block (&blocks[i], i, 0, blocks_write_buffer[i], slots[i], flush_volumes_info[i], 0,
			    num_block_pages);
    }

  *p_blocks = blocks;

  return NO_ERROR;

exit_on_error:
  for (i = 0; i < DWB_MAX_BLOCKS; i++)
    {
      if (slots[i] != NULL)
	{
	  free_and_init (slots[i]);
	}

      if (blocks_write_buffer[i] != NULL)
	{
	  free_and_init (blocks_write_buffer[i]);
	}

      if (flush_volumes_info[i] != NULL)
	{
	  free_and_init (flush_volumes_info[i]);
	}
    }

  if (blocks != NULL)
    {
      free_and_init (blocks);
    }

  return error_code;
}

/*
 * dwb_finalize_block () - Finalize a block.
 *
 * return   : Nothing
 * block (in) : Double write buffer block.
 */
STATIC_INLINE void
dwb_finalize_block (DWB_BLOCK * block)
{
  if (block->slots != NULL)
    {
      free_and_init (block->slots);
    }
  /* destroy block write buffer */
  if (block->write_buffer != NULL)
    {
      free_and_init (block->write_buffer);
    }
  if (block->flush_volumes_info != NULL)
    {
      free_and_init (block->flush_volumes_info);
    }

  dwb_destroy_wait_queue (&block->wait_queue, &block->mutex);

  pthread_mutex_destroy (&block->mutex);
}

/*
 * dwb_create_internal () - Create double write buffer.
 *
 * return   : Error code.
 * thread_p (in): The thread entry.
 * dwb_volume_name (in) : The double write buffer volume name.
 * current_position_with_flags (in/out): Current position with flags.
 *
 *  Note: Is user responsibility to ensure that no other transaction can access DWB structure, during creation.
 */
STATIC_INLINE int
dwb_create_internal (THREAD_ENTRY * thread_p, const char *dwb_volume_name, UINT64 * current_position_with_flags)
{
  int error_code = NO_ERROR;
  unsigned int double_write_buffer_size, num_blocks = 0;
  unsigned int i, num_pages, num_block_pages;
  int vdes = NULL_VOLDES;
  DWB_BLOCK *blocks = NULL;
  UINT64 new_position_with_flags;

  const int freelist_block_count = 2;
  const int freelist_block_size = DWB_SLOTS_FREE_LIST_SIZE;

  assert (dwb_volume_name != NULL && current_position_with_flags != NULL);

  if (!dwb_load_buffer_size (&double_write_buffer_size) || !dwb_load_block_count (&num_blocks))
    {
      /* Do not use double write buffer. */
      return NO_ERROR;
    }

  num_pages = double_write_buffer_size / IO_PAGESIZE;
  num_block_pages = num_pages / num_blocks;

  assert (IS_POWER_OF_2 (num_blocks));
  assert (IS_POWER_OF_2 (num_pages));
  assert (IS_POWER_OF_2 (num_block_pages));
  assert (num_blocks <= DWB_MAX_BLOCKS);

  /* Create and open DWB volume first */
  vdes = fileio_format (thread_p, boot_db_full_name (), dwb_volume_name, LOG_DBDWB_VOLID, num_block_pages, true,
			false, false, IO_PAGESIZE, 0, false);
  if (vdes == NULL_VOLDES)
    {
      goto exit_on_error;
    }

  /* Needs to flush dirty page before activating DWB. */
  fileio_synchronize_all (thread_p, false);

  /* Create DWB blocks */
  error_code = dwb_create_blocks (thread_p, num_blocks, num_block_pages, &blocks);
  if (error_code != NO_ERROR)
    {
      goto exit_on_error;
    }

  dwb_Global.blocks = blocks;
  dwb_Global.num_blocks = num_blocks;
  dwb_Global.num_pages = num_pages;
  dwb_Global.num_block_pages = num_block_pages;
  dwb_Global.log2_num_block_pages = (unsigned int) (log ((float) num_block_pages) / log ((float) 2));
  dwb_Global.blocks_flush_counter = 0;
  dwb_Global.next_block_to_flush = 0;
  pthread_mutex_init (&dwb_Global.mutex, NULL);
  dwb_init_wait_queue (&dwb_Global.wait_queue);
  dwb_Global.vdes = vdes;
  dwb_Global.file_sync_helper_block = NULL;

  dwb_Global.slots_hashmap.init (dwb_slots_Ts, THREAD_TS_DWB_SLOTS, DWB_SLOTS_HASH_SIZE, freelist_block_size,
				 freelist_block_count, slots_entry_Descriptor);

  /* Set creation flag. */
  new_position_with_flags = DWB_RESET_POSITION (*current_position_with_flags);
  new_position_with_flags = DWB_STARTS_CREATION (new_position_with_flags);
  if (!ATOMIC_CAS_64 (&dwb_Global.position_with_flags, *current_position_with_flags, new_position_with_flags))
    {
      /* Impossible. */
      assert (false);
    }
  *current_position_with_flags = new_position_with_flags;

  return NO_ERROR;

exit_on_error:
  if (vdes != NULL_VOLDES)
    {
      fileio_dismount (thread_p, vdes);
      fileio_unformat (NULL, dwb_volume_name);
    }

  if (blocks != NULL)
    {
      for (i = 0; i < num_blocks; i++)
	{
	  dwb_finalize_block (&blocks[i]);
	}
      free_and_init (blocks);
    }

  return error_code;
}

/*
 * dwb_slots_hash_entry_alloc () - Allocate a new entry in slots hash.
 *
 *   returns: New pointer or NULL on error.
 */
static void *
dwb_slots_hash_entry_alloc (void)
{
  DWB_SLOTS_HASH_ENTRY *slots_hash_entry;

  slots_hash_entry = (DWB_SLOTS_HASH_ENTRY *) malloc (sizeof (DWB_SLOTS_HASH_ENTRY));
  if (slots_hash_entry == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (DWB_SLOTS_HASH_ENTRY));
      return NULL;
    }

  pthread_mutex_init (&slots_hash_entry->mutex, NULL);

  return (void *) slots_hash_entry;
}

/*
 * dwb_slots_hash_entry_free () - Free an entry in slots hash.
 *   returns: Error code.
 *   entry(in): The entry to free.
 */
static int
dwb_slots_hash_entry_free (void *entry)
{
  DWB_SLOTS_HASH_ENTRY *slots_hash_entry = (DWB_SLOTS_HASH_ENTRY *) entry;

  if (slots_hash_entry == NULL)
    {
      return ER_FAILED;
    }

  pthread_mutex_destroy (&slots_hash_entry->mutex);
  free (entry);

  return NO_ERROR;
}

/*
 * dwb_slots_hash_entry_init () - Initialize slots hash entry.
 *   returns: Error code.
 *   entry(in): The entry to initialize.
 */
static int
dwb_slots_hash_entry_init (void *entry)
{
  DWB_SLOTS_HASH_ENTRY *p_entry = (DWB_SLOTS_HASH_ENTRY *) entry;

  if (p_entry == NULL)
    {
      return ER_FAILED;
    }

  VPID_SET_NULL (&p_entry->vpid);
  p_entry->slot = NULL;

  return NO_ERROR;
}

/*
 * dwb_slots_hash_key_copy () - Copy a slots hash key.
 *   returns: Error code.
 *   src(in): The source.
 *   dest(out): The destination.
 */
static int
dwb_slots_hash_key_copy (void *src, void *dest)
{
  if (src == NULL || dest == NULL)
    {
      return ER_FAILED;
    }

  VPID_COPY ((VPID *) dest, (VPID *) src);
  return NO_ERROR;
}

/*
 * dwb_slots_hash_compare_key () - Compare slots hash keys.
 *
 * return : 0 if equal, -1 otherwise.
 * key1 (in) : Pointer to first VPID value.
 * key2 (in) : Pointer to second VPID value.
 */
static int
dwb_slots_hash_compare_key (void *key1, void *key2)
{
  if (VPID_EQ ((VPID *) key1, (VPID *) key2))
    {
      return 0;
    }

  return -1;
}

/*
 * dwb_slots_hash_key () - Slots hash function.
 *
 * return		: Hash index.
 * key (in)	        : Key value.
 * hash_table_size (in) : Hash size.
 */
static unsigned int
dwb_slots_hash_key (void *key, int hash_table_size)
{
  const VPID *vpid = (VPID *) key;

  return ((vpid->pageid | ((unsigned int) vpid->volid) << 24) % hash_table_size);
}

/*
 * dwb_slots_hash_insert () - Insert entry in slots hash.
 *
 * return   : Error code.
 * thread_p (in): The thread entry.
 * vpid(in): The page identifier.
 * slot(in): The DWB slot.
 * inserted (out): 1, if slot inserted in hash.
 */
STATIC_INLINE int
dwb_slots_hash_insert (THREAD_ENTRY * thread_p, VPID * vpid, DWB_SLOT * slot, bool * inserted)
{
  int error_code = NO_ERROR;
  DWB_SLOTS_HASH_ENTRY *slots_hash_entry = NULL;

  assert (vpid != NULL && slot != NULL && inserted != NULL);

  *inserted = dwb_Global.slots_hashmap.find_or_insert (thread_p, *vpid, slots_hash_entry);

  assert (VPID_EQ (&slots_hash_entry->vpid, &slot->vpid));
  assert (slots_hash_entry->vpid.pageid == slot->io_page->prv.pageid
	  && slots_hash_entry->vpid.volid == slot->io_page->prv.volid);

  if (!(*inserted))
    {
      assert (slots_hash_entry->slot != NULL);

      if (LSA_LT (&slot->lsa, &slots_hash_entry->slot->lsa))
	{
	  dwb_log ("DWB hash find key (%d, %d), the LSA=(%lld,%d), better than (%lld,%d): \n",
		   vpid->volid, vpid->pageid, slots_hash_entry->slot->lsa.pageid,
		   slots_hash_entry->slot->lsa.offset, slot->lsa.pageid, slot->lsa.offset);

	  /* The older slot is better than mine - leave it in hash. */
	  pthread_mutex_unlock (&slots_hash_entry->mutex);
	  return NO_ERROR;
	}
      else if (LSA_EQ (&slot->lsa, &slots_hash_entry->slot->lsa))
	{
	  /*
	   * If LSA's are equals, still replace slot in hash. We are in "flushing to disk without logging" case.
	   * The page was modified but not logged. We have to flush this version since is the latest one.
	   */
	  if (slots_hash_entry->slot->block_no == slot->block_no)
	    {
	      /* Invalidate the old slot, if is in the same block. We want to avoid duplicates in block at flush. */
	      assert (slots_hash_entry->slot->position_in_block < slot->position_in_block);
	      VPID_SET_NULL (&slots_hash_entry->slot->vpid);
	      fileio_initialize_res (thread_p, slots_hash_entry->slot->io_page, IO_PAGESIZE);

	      dwb_log ("Found same page with same LSA in same block - %d - at positions (%d, %d) \n",
		       slots_hash_entry->slot->position_in_block, slot->position_in_block);
	    }
	  else
	    {
#if !defined (NDEBUG)
	      int old_block_no = ATOMIC_INC_32 (&slots_hash_entry->slot->block_no, 0);
	      if (old_block_no > 0)
		{
		  /* Be sure that the block containing old page version is flushed first. */
		  DWB_BLOCK *old_block = &dwb_Global.blocks[old_block_no];
		  DWB_BLOCK *new_block = &dwb_Global.blocks[slot->block_no];

		  /* Maybe we will check that the slot is still in old block. */
		  assert ((old_block->version < new_block->version)
			  || (old_block->version == new_block->version && old_block->block_no < new_block->block_no));

		  dwb_log ("Found same page with same LSA in 2 different blocks old = (%d, %d), new = (%d,%d) \n",
			   old_block_no, slots_hash_entry->slot->position_in_block, new_block->block_no,
			   slot->position_in_block);
		}
#endif
	    }
	}

      dwb_log ("Replace hash key (%d, %d), the new LSA=(%lld,%d), the old LSA = (%lld,%d)",
	       vpid->volid, vpid->pageid, slot->lsa.pageid, slot->lsa.offset,
	       slots_hash_entry->slot->lsa.pageid, slots_hash_entry->slot->lsa.offset);
    }
  else
    {
      dwb_log ("Inserted hash key (%d, %d), LSA=(%lld,%d)", vpid->volid, vpid->pageid, slot->lsa.pageid,
	       slot->lsa.offset);
    }

  slots_hash_entry->slot = slot;
  pthread_mutex_unlock (&slots_hash_entry->mutex);
  *inserted = true;

  return NO_ERROR;
}

/*
 * dwb_destroy_internal () - Destroy DWB.
 *
 * return   : Error code.
 * thread_p (in): The thread entry.
 *
 *  Note: Is user responsibility to ensure that no other transaction can access DWB structure, during destroy.
 */
STATIC_INLINE void
dwb_destroy_internal (THREAD_ENTRY * thread_p, UINT64 * current_position_with_flags)
{
  UINT64 new_position_with_flags;
  unsigned int block_no;

  assert (current_position_with_flags != NULL);

  dwb_destroy_wait_queue (&dwb_Global.wait_queue, &dwb_Global.mutex);
  pthread_mutex_destroy (&dwb_Global.mutex);

  if (dwb_Global.blocks != NULL)
    {
      for (block_no = 0; block_no < DWB_NUM_TOTAL_BLOCKS; block_no++)
	{
	  dwb_finalize_block (&dwb_Global.blocks[block_no]);
	}
      free_and_init (dwb_Global.blocks);
    }

  dwb_Global.slots_hashmap.destroy ();

  if (dwb_Global.vdes != NULL_VOLDES)
    {
      fileio_dismount (thread_p, dwb_Global.vdes);
      dwb_Global.vdes = NULL_VOLDES;
      fileio_unformat (thread_p, dwb_Volume_name);
    }

  /* Set creation flag. */
  new_position_with_flags = DWB_RESET_POSITION (*current_position_with_flags);
  new_position_with_flags = DWB_ENDS_CREATION (new_position_with_flags);
  if (!ATOMIC_CAS_64 (&dwb_Global.position_with_flags, *current_position_with_flags, new_position_with_flags))
    {
      /* Impossible. */
      assert (false);
    }

  *current_position_with_flags = new_position_with_flags;
}

/*
 * dwb_set_status_resumed () - Set status resumed.
 *
 * return   : Error code.
 * data (in): The thread entry.
 */
STATIC_INLINE int
dwb_set_status_resumed (void *data)
{
#if defined (SERVER_MODE)
  DWB_WAIT_QUEUE_ENTRY *queue_entry = (DWB_WAIT_QUEUE_ENTRY *) data;
  THREAD_ENTRY *woken_thread_p;

  if (queue_entry != NULL)
    {
      woken_thread_p = (THREAD_ENTRY *) queue_entry->data;
      if (woken_thread_p != NULL)
	{
	  thread_lock_entry (woken_thread_p);
	  woken_thread_p->resume_status = THREAD_DWB_QUEUE_RESUMED;
	  thread_unlock_entry (woken_thread_p);
	}
    }

  return NO_ERROR;
#else /* !SERVER_MODE */
  return NO_ERROR;
#endif /* !SERVER_MODE */
}

/*
 * dwb_wait_for_block_completion () - Wait for DWB block to complete.
 *
 * return   : Error code.
 * thread_p (in): The thread entry.
 * dwb_block (in): The DWB block number.
 */
STATIC_INLINE int
dwb_wait_for_block_completion (THREAD_ENTRY * thread_p, unsigned int block_no)
{
#if defined (SERVER_MODE)
  int error_code = NO_ERROR;
  DWB_WAIT_QUEUE_ENTRY *double_write_queue_entry = NULL;
  DWB_BLOCK *dwb_block = NULL;
  PERF_UTIME_TRACKER time_track;
  UINT64 current_position_with_flags;
  int r;
  struct timeval timeval_crt, timeval_timeout;
  struct timespec to;
  bool save_check_interrupt;

  assert (thread_p != NULL && block_no < DWB_NUM_TOTAL_BLOCKS);

  PERF_UTIME_TRACKER_START (thread_p, &time_track);

  dwb_block = &dwb_Global.blocks[block_no];
  (void) pthread_mutex_lock (&dwb_block->mutex);

  thread_lock_entry (thread_p);

  /* Check again after acquiring mutexes. */
  current_position_with_flags = ATOMIC_INC_64 (&dwb_Global.position_with_flags, 0ULL);
  if (!DWB_IS_BLOCK_WRITE_STARTED (current_position_with_flags, block_no))
    {
      thread_unlock_entry (thread_p);
      pthread_mutex_unlock (&dwb_block->mutex);

      PERF_UTIME_TRACKER_TIME (thread_p, &time_track, PSTAT_DWB_WAIT_FLUSH_BLOCK_TIME_COUNTERS);

      return NO_ERROR;
    }

  double_write_queue_entry = dwb_block_add_wait_queue_entry (&dwb_block->wait_queue, thread_p);
  if (double_write_queue_entry == NULL)
    {
      /* allocation error */
      thread_unlock_entry (thread_p);
      pthread_mutex_unlock (&dwb_block->mutex);

      ASSERT_ERROR_AND_SET (error_code);
      return error_code;
    }

  pthread_mutex_unlock (&dwb_block->mutex);

  save_check_interrupt = logtb_set_check_interrupt (thread_p, false);
  /* Waits for maximum 20 milliseconds. */
  gettimeofday (&timeval_crt, NULL);
  timeval_add_msec (&timeval_timeout, &timeval_crt, 20);
  timeval_to_timespec (&to, &timeval_timeout);

  r = thread_suspend_timeout_wakeup_and_unlock_entry (thread_p, &to, THREAD_DWB_QUEUE_SUSPENDED);

  (void) logtb_set_check_interrupt (thread_p, save_check_interrupt);

  PERF_UTIME_TRACKER_TIME (thread_p, &time_track, PSTAT_DWB_WAIT_FLUSH_BLOCK_TIME_COUNTERS);

  if (r == ER_CSS_PTHREAD_COND_TIMEDOUT)
    {
      /* timeout, remove the entry from queue */
      dwb_remove_wait_queue_entry (&dwb_block->wait_queue, &dwb_block->mutex, thread_p, dwb_set_status_resumed);
      return r;
    }
  else if (thread_p->resume_status != THREAD_DWB_QUEUE_RESUMED)
    {
      /* interruption, remove the entry from queue */
      assert (thread_p->resume_status == THREAD_RESUME_DUE_TO_SHUTDOWN);

      dwb_remove_wait_queue_entry (&dwb_block->wait_queue, &dwb_block->mutex, thread_p, NULL);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INTERRUPTED, 0);
      return ER_INTERRUPTED;
    }
  else
    {
      assert (thread_p->resume_status == THREAD_DWB_QUEUE_RESUMED);
      return NO_ERROR;
    }
#else /* SERVER_MODE */
  return NO_ERROR;
#endif /* SERVER_MODE */
}

/*
 * dwb_signal_waiting_thread () - Signal waiting thread.
 *
 * return   : Error code.
 * data (in): Queue entry containing the waiting thread.
 */
STATIC_INLINE int
dwb_signal_waiting_thread (void *data)
{
#if defined (SERVER_MODE)
  THREAD_ENTRY *wait_thread_p;
  DWB_WAIT_QUEUE_ENTRY *wait_queue_entry = (DWB_WAIT_QUEUE_ENTRY *) data;

  assert (wait_queue_entry != NULL);

  wait_thread_p = (THREAD_ENTRY *) wait_queue_entry->data;
  if (wait_thread_p)
    {
      thread_lock_entry (wait_thread_p);
      if (wait_thread_p->resume_status == THREAD_DWB_QUEUE_SUSPENDED)
	{
	  thread_wakeup_already_had_mutex (wait_thread_p, THREAD_DWB_QUEUE_RESUMED);
	}
      thread_unlock_entry (wait_thread_p);
    }

  return NO_ERROR;
#else /* !SERVER_MODE */
  return NO_ERROR;
#endif /* !SERVER_MODE */
}

/*
 * dwb_signal_block_completion () - Signal double write buffer block completion.
 *
 * return   : Nothing.
 * thread_p (in): The thread entry.
 * dwb_block (in): The double write buffer block.
 */
STATIC_INLINE void
dwb_signal_block_completion (THREAD_ENTRY * thread_p, DWB_BLOCK * dwb_block)
{
  assert (dwb_block != NULL);

  /* There are blocked threads. Destroy the wait queue and release the blocked threads. */
  dwb_signal_waiting_threads (&dwb_block->wait_queue, &dwb_block->mutex);
}

/*
 * dwb_signal_structure_modificated () - Signal DWB structure changed.
 *
 * return   : Nothing.
 * thread_p (in): The thread entry.
 */
STATIC_INLINE void
dwb_signal_structure_modificated (THREAD_ENTRY * thread_p)
{
  /* There are blocked threads. Destroy the wait queue and release the blocked threads. */
  dwb_signal_waiting_threads (&dwb_Global.wait_queue, &dwb_Global.mutex);
}

/*
 * dwb_wait_for_strucure_modification () - Wait for double write buffer structure modification.
 *
 * return : Error code.
 * thread_p (in): The thread entry.
 */
STATIC_INLINE int
dwb_wait_for_strucure_modification (THREAD_ENTRY * thread_p)
{
#if defined (SERVER_MODE)
  int error_code = NO_ERROR;
  DWB_WAIT_QUEUE_ENTRY *double_write_queue_entry = NULL;
  UINT64 current_position_with_flags;
  int r;
  struct timeval timeval_crt, timeval_timeout;
  struct timespec to;
  bool save_check_interrupt;

  (void) pthread_mutex_lock (&dwb_Global.mutex);

  /* Check the actual flags, to avoids unnecessary waits. */
  current_position_with_flags = ATOMIC_INC_64 (&dwb_Global.position_with_flags, 0ULL);
  if (!DWB_IS_MODIFYING_STRUCTURE (current_position_with_flags))
    {
      pthread_mutex_unlock (&dwb_Global.mutex);
      return NO_ERROR;
    }

  thread_lock_entry (thread_p);

  double_write_queue_entry = dwb_block_add_wait_queue_entry (&dwb_Global.wait_queue, thread_p);
  if (double_write_queue_entry == NULL)
    {
      /* allocation error */
      thread_unlock_entry (thread_p);
      pthread_mutex_unlock (&dwb_Global.mutex);

      ASSERT_ERROR_AND_SET (error_code);
      return error_code;
    }

  pthread_mutex_unlock (&dwb_Global.mutex);

  save_check_interrupt = logtb_set_check_interrupt (thread_p, false);
  /* Waits for maximum 10 milliseconds. */
  gettimeofday (&timeval_crt, NULL);
  timeval_add_msec (&timeval_timeout, &timeval_crt, 10);
  timeval_to_timespec (&to, &timeval_timeout);

  r = thread_suspend_timeout_wakeup_and_unlock_entry (thread_p, &to, THREAD_DWB_QUEUE_SUSPENDED);

  (void) logtb_set_check_interrupt (thread_p, save_check_interrupt);
  if (r == ER_CSS_PTHREAD_COND_TIMEDOUT)
    {
      /* timeout, remove the entry from queue */
      dwb_remove_wait_queue_entry (&dwb_Global.wait_queue, &dwb_Global.mutex, thread_p, NULL);
      return r;
    }
  else if (thread_p->resume_status != THREAD_DWB_QUEUE_RESUMED)
    {
      /* interruption, remove the entry from queue */
      assert (thread_p->resume_status == THREAD_RESUME_DUE_TO_SHUTDOWN);

      dwb_remove_wait_queue_entry (&dwb_Global.wait_queue, &dwb_Global.mutex, thread_p, NULL);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INTERRUPTED, 0);
      return ER_INTERRUPTED;
    }
  else
    {
      assert (thread_p->resume_status == THREAD_DWB_QUEUE_RESUMED);
      return NO_ERROR;
    }
#else /* !SERVER_MODE */
  return NO_ERROR;
#endif /* !SERVER_MODE */
}

/*
 * dwb_compare_slots () - Compare DWB slots.
 *   return: arg1 - arg2.
 *   arg1(in): Slot 1.
 *   arg2(in): Slot 2.
 */
static int
dwb_compare_slots (const void *arg1, const void *arg2)
{
  int diff;
  INT64 diff2;
  DWB_SLOT *slot1, *slot2;

  assert (arg1 != NULL);
  assert (arg2 != NULL);

  slot1 = (DWB_SLOT *) arg1;
  slot2 = (DWB_SLOT *) arg2;

  diff = slot1->vpid.volid - slot2->vpid.volid;
  if (diff > 0)
    {
      return 1;
    }
  else if (diff < 0)
    {
      return -1;
    }

  diff = slot1->vpid.pageid - slot2->vpid.pageid;
  if (diff > 0)
    {
      return 1;
    }
  else if (diff < 0)
    {
      return -1;
    }

  diff2 = slot1->lsa.pageid - slot2->lsa.pageid;
  if (diff2 > 0)
    {
      return 1;
    }
  else if (diff2 < 0)
    {
      return -1;
    }

  diff2 = slot1->lsa.offset - slot2->lsa.offset;
  if (diff2 > 0)
    {
      return 1;
    }
  else if (diff2 < 0)
    {
      return -1;
    }

  return 0;
}

/*
 * dwb_block_create_ordered_slots () - Create ordered slots from block slots.
 *
 * return   : Error code.
 * block(in): The block.
 * p_dwb_ordered_slots(out): The ordered slots.
 * p_ordered_slots_length(out): The ordered slots array length.
 */
STATIC_INLINE int
dwb_block_create_ordered_slots (DWB_BLOCK * block, DWB_SLOT ** p_dwb_ordered_slots,
				unsigned int *p_ordered_slots_length)
{
  DWB_SLOT *p_local_dwb_ordered_slots = NULL;

  assert (block != NULL && p_dwb_ordered_slots != NULL);

  /* including sentinel */
  p_local_dwb_ordered_slots = (DWB_SLOT *) malloc ((block->count_wb_pages + 1) * sizeof (DWB_SLOT));
  if (p_local_dwb_ordered_slots == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      (block->count_wb_pages + 1) * sizeof (DWB_SLOT));
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  memcpy (p_local_dwb_ordered_slots, block->slots, block->count_wb_pages * sizeof (DWB_SLOT));

  /* init sentinel slot */
  dwb_init_slot (&p_local_dwb_ordered_slots[block->count_wb_pages]);

  /* Order pages by (VPID, LSA) */
  qsort ((void *) p_local_dwb_ordered_slots, block->count_wb_pages, sizeof (DWB_SLOT), dwb_compare_slots);

  *p_dwb_ordered_slots = p_local_dwb_ordered_slots;
  *p_ordered_slots_length = block->count_wb_pages + 1;

  return NO_ERROR;
}

/*
 * dwb_slots_hash_delete () - Delete entry in slots hash.
 *
 * return   : Error code.
 * thread_p (in): The thread entry.
 * slot(in): The DWB slot.
 */
STATIC_INLINE int
dwb_slots_hash_delete (THREAD_ENTRY * thread_p, DWB_SLOT * slot)
{
  int error_code = NO_ERROR;
  VPID *vpid;
  DWB_SLOTS_HASH_ENTRY *slots_hash_entry = NULL;

  vpid = &slot->vpid;
  if (VPID_ISNULL (vpid))
    {
      /* Nothing to do, the slot is not in hash. */
      return NO_ERROR;
    }

  /* Remove the old vpid from hash. */
  slots_hash_entry = dwb_Global.slots_hashmap.find (thread_p, *vpid);
  if (slots_hash_entry == NULL)
    {
      /* Already removed from hash by others, nothing to do. */
      return NO_ERROR;
    }

  assert (VPID_ISNULL (&(slots_hash_entry->slot->vpid)) || VPID_EQ (&(slots_hash_entry->slot->vpid), vpid));

  /* Check the slot. */
  if (slots_hash_entry->slot == slot)
    {
      assert (VPID_EQ (&(slots_hash_entry->slot->vpid), vpid));
      assert (slots_hash_entry->slot->io_page->prv.pageid == vpid->pageid
	      && slots_hash_entry->slot->io_page->prv.volid == vpid->volid);

      if (!dwb_Global.slots_hashmap.erase_locked (thread_p, *vpid, slots_hash_entry))
	{
	  assert_release (false);
	  /* Should not happen. */
	  pthread_mutex_unlock (&slots_hash_entry->mutex);
	  dwb_log_error ("DWB hash delete key (%d, %d) with %d error: \n", vpid->volid, vpid->pageid, error_code);
	  return error_code;
	}
    }
  else
    {
      pthread_mutex_unlock (&slots_hash_entry->mutex);
    }

  return NO_ERROR;
}

/*
 * dwb_compare_vol_fd () - Compare two volume file descriptors.
 *
 * return    : v1 - v2.
 * v1(in)    : Pointer to volume1.
 * v2(in)    : Pointer to volume2.
 */
static int
dwb_compare_vol_fd (const void *v1, const void *v2)
{
  int *vol_fd1 = (int *) v1;
  int *vol_fd2 = (int *) v2;

  assert (vol_fd1 != NULL && vol_fd2 != NULL);

  return ((*vol_fd1) - (*vol_fd2));
}

/*
 * dwb_add_volume_to_block_flush_area () - Add a volume to block flush area.
 *
 * return   :
 * thread_p (in): The thread entry.
 * block(in): The block where the flush area reside.
 * vol_fd(in): The volume to add.
 *
 * Note: The volume is added if is not already in block flush area.
 */
STATIC_INLINE FLUSH_VOLUME_INFO *
dwb_add_volume_to_block_flush_area (THREAD_ENTRY * thread_p, DWB_BLOCK * block, int vol_fd)
{
  FLUSH_VOLUME_INFO *flush_new_volume_info;
#if !defined (NDEBUG)
  unsigned int old_count_flush_volumes_info;
#endif

  assert (block != NULL);
  assert (vol_fd != NULL_VOLDES);

#if !defined (NDEBUG)
  old_count_flush_volumes_info = block->count_flush_volumes_info;
#endif

  flush_new_volume_info = &block->flush_volumes_info[block->count_flush_volumes_info];

  flush_new_volume_info->vdes = vol_fd;
  flush_new_volume_info->num_pages = 0;
  flush_new_volume_info->all_pages_written = false;
  flush_new_volume_info->flushed_status = VOLUME_NOT_FLUSHED;

  /* There is only a writer and several (currently 2) readers on flush_new_volume_info array.
   * I need to be sure that size is incremented after setting element. Uses atomic to prevent code reordering.
   */
  ATOMIC_INC_32 (&block->count_flush_volumes_info, 1);

  assert (((old_count_flush_volumes_info + 1) == block->count_flush_volumes_info)
	  && (block->count_flush_volumes_info <= block->max_to_flush_vdes));

  return flush_new_volume_info;
}

/*
 * dwb_write_block () - Write block pages in specified order.
 *
 * return   : Error code.
 * thread_p (in): The thread entry.
 * block(in): The block that is written.
 * p_dwb_ordered_slots(in): The slots that gives the pages flush order.
 * ordered_slots_length(in): The ordered slots array length.
 * remove_from_hash(in): True, if needs to remove entries from hash.
 * file_sync_helper_can_flush(in): True, if helper can flush.
 *
 *  Note: This function fills to_flush_vdes array with the volumes that must be flushed.
 */
STATIC_INLINE int
dwb_write_block (THREAD_ENTRY * thread_p, DWB_BLOCK * block, DWB_SLOT * p_dwb_ordered_slots,
		 unsigned int ordered_slots_length, bool file_sync_helper_can_flush, bool remove_from_hash)
{
  VOLID last_written_volid;
  unsigned int i;
  int last_written_vol_fd, vol_fd;
  VPID *vpid;
  int error_code = NO_ERROR;
  int count_writes = 0, num_pages_to_sync;
  FLUSH_VOLUME_INFO *current_flush_volume_info = NULL;
  bool can_flush_volume = false;

  assert (block != NULL && p_dwb_ordered_slots != NULL);

  /*
   * Write the whole slots data first and then remove it from hash. Is better to do in this way. Thus, the fileio_write
   * may be slow. While the current transaction has delays caused by fileio_write, the concurrent transaction still
   * can access the data from memory instead disk
   */

  assert (block->count_wb_pages < ordered_slots_length);
  assert (block->count_flush_volumes_info == 0);

  num_pages_to_sync = prm_get_integer_value (PRM_ID_PB_SYNC_ON_NFLUSH);

  last_written_volid = NULL_VOLID;
  last_written_vol_fd = NULL_VOLDES;

  for (i = 0; i < block->count_wb_pages; i++)
    {
      vpid = &p_dwb_ordered_slots[i].vpid;
      if (VPID_ISNULL (vpid))
	{
	  continue;
	}

      assert (VPID_ISNULL (&p_dwb_ordered_slots[i + 1].vpid) || VPID_LT (vpid, &p_dwb_ordered_slots[i + 1].vpid));

      if (last_written_volid != vpid->volid)
	{
	  /* Get the volume descriptor. */
	  if (current_flush_volume_info != NULL)
	    {
	      assert_release (current_flush_volume_info->vdes == last_written_vol_fd);
	      current_flush_volume_info->all_pages_written = true;
	      can_flush_volume = true;

	      current_flush_volume_info = NULL;	/* reset */
	    }

	  vol_fd = fileio_get_volume_descriptor (vpid->volid);
	  if (vol_fd == NULL_VOLDES)
	    {
	      /* probably it was removed meanwhile. skip it! */
	      continue;
	    }

	  last_written_volid = vpid->volid;
	  last_written_vol_fd = vol_fd;

	  current_flush_volume_info = dwb_add_volume_to_block_flush_area (thread_p, block, last_written_vol_fd);
	}

      assert (last_written_vol_fd != NULL_VOLDES);

      assert (p_dwb_ordered_slots[i].io_page->prv.p_reserve_2 == 0);
      assert (p_dwb_ordered_slots[i].vpid.pageid == p_dwb_ordered_slots[i].io_page->prv.pageid
	      && p_dwb_ordered_slots[i].vpid.volid == p_dwb_ordered_slots[i].io_page->prv.volid);

      /* Write the data. */
      if (fileio_write (thread_p, last_written_vol_fd, p_dwb_ordered_slots[i].io_page, vpid->pageid, IO_PAGESIZE,
			FILEIO_WRITE_NO_COMPENSATE_WRITE) == NULL)
	{
	  ASSERT_ERROR ();
	  dwb_log_error ("DWB write page VPID=(%d, %d) LSA=(%lld,%d) with %d error: \n",
			 vpid->volid, vpid->pageid, p_dwb_ordered_slots[i].io_page->prv.lsa.pageid,
			 (int) p_dwb_ordered_slots[i].io_page->prv.lsa.offset, er_errid ());
	  assert (false);
	  /* Something wrong happened. */
	  return ER_FAILED;
	}

      dwb_log ("dwb_write_block: written page = (%d,%d) LSA=(%lld,%d)\n",
	       vpid->volid, vpid->pageid, p_dwb_ordered_slots[i].io_page->prv.lsa.pageid,
	       (int) p_dwb_ordered_slots[i].io_page->prv.lsa.offset);

#if defined (SERVER_MODE)
      assert (current_flush_volume_info != NULL);

      ATOMIC_INC_32 (&current_flush_volume_info->num_pages, 1);
      count_writes++;

      if (file_sync_helper_can_flush && (count_writes >= num_pages_to_sync || can_flush_volume == true)
	  && dwb_is_file_sync_helper_daemon_available ())
	{
	  if (ATOMIC_CAS_ADDR (&dwb_Global.file_sync_helper_block, (DWB_BLOCK *) NULL, block))
	    {
	      dwb_file_sync_helper_daemon->wakeup ();
	    }

	  /* Add statistics. */
	  perfmon_add_stat (thread_p, PSTAT_PB_NUM_IOWRITES, count_writes);
	  count_writes = 0;
	  can_flush_volume = false;
	}
#endif
    }

  /* the last written volume */
  if (current_flush_volume_info != NULL)
    {
      current_flush_volume_info->all_pages_written = true;
    }

#if !defined (NDEBUG)
  for (i = 0; i < block->count_flush_volumes_info; i++)
    {
      assert (block->flush_volumes_info[i].all_pages_written == true);
      assert (block->flush_volumes_info[i].vdes != NULL_VOLDES);
    }
#endif

#if defined (SERVER_MODE)
  if (file_sync_helper_can_flush && (dwb_Global.file_sync_helper_block == NULL)
      && (block->count_flush_volumes_info > 0))
    {
      /* If file_sync_helper_block is NULL, it means that the file sync helper thread does not run and was not woken yet. */
      if (dwb_is_file_sync_helper_daemon_available ()
	  && ATOMIC_CAS_ADDR (&dwb_Global.file_sync_helper_block, (DWB_BLOCK *) NULL, block))
	{
	  dwb_file_sync_helper_daemon->wakeup ();
	}
    }
#endif

  /* Add statistics. */
  perfmon_add_stat (thread_p, PSTAT_PB_NUM_IOWRITES, count_writes);

  /* Remove the corresponding entries from hash. */
  if (remove_from_hash)
    {
      PERF_UTIME_TRACKER time_track;

      PERF_UTIME_TRACKER_START (thread_p, &time_track);

      for (i = 0; i < block->count_wb_pages; i++)
	{
	  vpid = &p_dwb_ordered_slots[i].vpid;
	  if (VPID_ISNULL (vpid))
	    {
	      continue;
	    }

	  assert (p_dwb_ordered_slots[i].position_in_block < DWB_BLOCK_NUM_PAGES);
	  error_code = dwb_slots_hash_delete (thread_p, &block->slots[p_dwb_ordered_slots[i].position_in_block]);
	  if (error_code != NO_ERROR)
	    {
	      return error_code;
	    }
	}

      PERF_UTIME_TRACKER_TIME (thread_p, &time_track, PSTAT_DWB_DECACHE_PAGES_AFTER_WRITE);
    }

  return NO_ERROR;
}

/*
 * dwb_flush_block () - Flush pages from specified block.
 *
 * return   : Error code.
 * thread_p (in): Thread entry.
 * block(in): The block that needs flush.
 * file_sync_helper_can_flush(in): True, if file sync helper thread can flush.
 * current_position_with_flags(out): Current position with flags.
 *
 *  Note: The block pages can't be modified by others during flush.
 */
STATIC_INLINE int
dwb_flush_block (THREAD_ENTRY * thread_p, DWB_BLOCK * block, bool file_sync_helper_can_flush,
		 UINT64 * current_position_with_flags)
{
  UINT64 local_current_position_with_flags, new_position_with_flags;
  int error_code = NO_ERROR;
  DWB_SLOT *p_dwb_ordered_slots = NULL;
  unsigned int i, ordered_slots_length;
  PERF_UTIME_TRACKER time_track;
  int num_pages;
  unsigned int current_block_to_flush, next_block_to_flush;
  int max_pages_to_sync;
#if defined (SERVER_MODE)
  bool flush = false;
  PERF_UTIME_TRACKER time_track_file_sync_helper;
#endif
#if !defined (NDEBUG)
  DWB_BLOCK *saved_file_sync_helper_block = NULL;
  LOG_LSA nxio_lsa;
#endif

  assert (block != NULL && block->count_wb_pages > 0 && dwb_is_created ());

  PERF_UTIME_TRACKER_START (thread_p, &time_track);

  /* Currently we allow only one block to be flushed. */
  ATOMIC_INC_32 (&dwb_Global.blocks_flush_counter, 1);
  assert (dwb_Global.blocks_flush_counter <= 1);

  /* Order slots by VPID, to flush faster. */
  error_code = dwb_block_create_ordered_slots (block, &p_dwb_ordered_slots, &ordered_slots_length);
  if (error_code != NO_ERROR)
    {
      error_code = ER_FAILED;
      goto end;
    }

  /* Remove duplicates */
  for (i = 0; i < block->count_wb_pages - 1; i++)
    {
      DWB_SLOT *s1, *s2;

      s1 = &p_dwb_ordered_slots[i];
      s2 = &p_dwb_ordered_slots[i + 1];

      assert (s1->io_page->prv.p_reserve_2 == 0);

      if (!VPID_ISNULL (&s1->vpid) && VPID_EQ (&s1->vpid, &s2->vpid))
	{
	  /* Next slot contains the same page, but that page is newer than the current one. Invalidate the VPID to
	   * avoid flushing the page twice. I'm sure that the current slot is not in hash.
	   */
	  assert (LSA_LE (&s1->lsa, &s2->lsa));

	  VPID_SET_NULL (&s1->vpid);

	  assert (s1->position_in_block < DWB_BLOCK_NUM_PAGES);
	  VPID_SET_NULL (&(block->slots[s1->position_in_block].vpid));

	  fileio_initialize_res (thread_p, s1->io_page, IO_PAGESIZE);
	}

      /* Check for WAL protocol. */
#if !defined (NDEBUG)
      if (s1->io_page->prv.pageid != NULL_PAGEID && logpb_need_wal (&s1->io_page->prv.lsa))
	{
	  /* Need WAL. Check whether log buffer pool was destroyed. */
	  nxio_lsa = log_Gl.append.get_nxio_lsa ();
	  assert (LSA_ISNULL (&nxio_lsa));
	}
#endif
    }

  PERF_UTIME_TRACKER_TIME (thread_p, &time_track, PSTAT_DWB_FLUSH_BLOCK_SORT_TIME_COUNTERS);

#if !defined (NDEBUG)
  saved_file_sync_helper_block = (DWB_BLOCK *) dwb_Global.file_sync_helper_block;
#endif

#if defined (SERVER_MODE)
  PERF_UTIME_TRACKER_START (thread_p, &time_track_file_sync_helper);

  while (dwb_Global.file_sync_helper_block != NULL)
    {
      flush = true;

      /* Be sure that the previous block was written on disk, before writing the current block. */
      if (dwb_is_file_sync_helper_daemon_available ())
	{
	  /* Wait for file sync helper. */
	  thread_sleep (1);
	}
      else
	{
	  /* Helper not available, flush the volumes from previous block. */
	  for (i = 0; i < dwb_Global.file_sync_helper_block->count_flush_volumes_info; i++)
	    {
	      assert (dwb_Global.file_sync_helper_block->flush_volumes_info[i].vdes != NULL_VOLDES);

	      if (ATOMIC_INC_32 (&(dwb_Global.file_sync_helper_block->flush_volumes_info[i].num_pages), 0) >= 0)
		{
		  (void) fileio_synchronize (thread_p,
					     dwb_Global.file_sync_helper_block->flush_volumes_info[i].vdes, NULL,
					     FILEIO_SYNC_ONLY);

		  dwb_log ("dwb_flush_block: Synchronized volume %d\n",
			   dwb_Global.file_sync_helper_block->flush_volumes_info[i].vdes);
		}
	    }
	  (void) ATOMIC_TAS_ADDR (&dwb_Global.file_sync_helper_block, (DWB_BLOCK *) NULL);
	}
    }

#if !defined (NDEBUG)
  if (saved_file_sync_helper_block)
    {
      for (i = 0; i < saved_file_sync_helper_block->count_flush_volumes_info; i++)
	{
	  assert (saved_file_sync_helper_block->flush_volumes_info[i].all_pages_written == true
		  && saved_file_sync_helper_block->flush_volumes_info[i].num_pages == 0);
	}
    }
#endif

  if (flush == true)
    {
      PERF_UTIME_TRACKER_TIME (thread_p, &time_track_file_sync_helper, PSTAT_DWB_WAIT_FILE_SYNC_HELPER_TIME_COUNTERS);
    }
#endif /* SERVER_MODE */

  ATOMIC_TAS_32 (&block->count_flush_volumes_info, 0);
  block->all_pages_written = false;

  /* First, write and flush the double write file buffer. */
  if (fileio_write_pages (thread_p, dwb_Global.vdes, block->write_buffer, 0, block->count_wb_pages,
			  IO_PAGESIZE, FILEIO_WRITE_NO_COMPENSATE_WRITE) == NULL)
    {
      /* Something wrong happened. */
      assert (false);
      error_code = ER_FAILED;
      goto end;
    }

  /* Increment statistics after writing in double write volume. */
  perfmon_add_stat (thread_p, PSTAT_PB_NUM_IOWRITES, block->count_wb_pages);

  if (fileio_synchronize (thread_p, dwb_Global.vdes, dwb_Volume_name, FILEIO_SYNC_ONLY) != dwb_Global.vdes)
    {
      assert (false);
      /* Something wrong happened. */
      error_code = ER_FAILED;
      goto end;
    }
  dwb_log ("dwb_flush_block: DWB synchronized\n");

  /* Now, write and flush the original location. */
  error_code =
    dwb_write_block (thread_p, block, p_dwb_ordered_slots, ordered_slots_length, file_sync_helper_can_flush, true);
  if (error_code != NO_ERROR)
    {
      assert (false);
      goto end;
    }

  max_pages_to_sync = prm_get_integer_value (PRM_ID_PB_SYNC_ON_NFLUSH) / 2;

  /* Now, flush only the volumes having pages in current block. */
  for (i = 0; i < block->count_flush_volumes_info; i++)
    {
      assert (block->flush_volumes_info[i].vdes != NULL_VOLDES);

      num_pages = ATOMIC_INC_32 (&block->flush_volumes_info[i].num_pages, 0);
      if (num_pages == 0)
	{
	  /* Flushed by helper. */
	  continue;
	}

#if defined (SERVER_MODE)
      if (file_sync_helper_can_flush == true)
	{
	  if ((num_pages > max_pages_to_sync) && dwb_is_file_sync_helper_daemon_available ())
	    {
	      /* Let the helper thread to flush volumes having many pages. */
	      assert (dwb_Global.file_sync_helper_block != NULL);
	      continue;
	    }
	}
      else
	{
	  assert (dwb_Global.file_sync_helper_block == NULL);
	}
#endif

      if (!ATOMIC_CAS_32 (&block->flush_volumes_info[i].flushed_status, VOLUME_NOT_FLUSHED,
			  VOLUME_FLUSHED_BY_DWB_FLUSH_THREAD))
	{
	  /* Flushed by helper. */
	  continue;
	}

      num_pages = ATOMIC_TAS_32 (&block->flush_volumes_info[i].num_pages, 0);
      assert (num_pages != 0);

      (void) fileio_synchronize (thread_p, block->flush_volumes_info[i].vdes, NULL, FILEIO_SYNC_ONLY);

      dwb_log ("dwb_flush_block: Synchronized volume %d\n", block->flush_volumes_info[i].vdes);
    }

  /* Allow to file sync helper thread to finish. */
  block->all_pages_written = true;

  if (perfmon_is_perf_tracking_and_active (PERFMON_ACTIVATION_FLAG_FLUSHED_BLOCK_VOLUMES))
    {
      perfmon_db_flushed_block_volumes (thread_p, block->count_flush_volumes_info);
    }

  /* The block is full or there is only one thread that access DWB. */
  assert (block->count_wb_pages == DWB_BLOCK_NUM_PAGES
	  || DWB_IS_MODIFYING_STRUCTURE (ATOMIC_INC_64 (&dwb_Global.position_with_flags, 0LL)));

  ATOMIC_TAS_32 (&block->count_wb_pages, 0);
  ATOMIC_INC_64 (&block->version, 1ULL);

  /* Reset block bit, since the block was flushed. */
reset_bit_position:
  local_current_position_with_flags = ATOMIC_INC_64 (&dwb_Global.position_with_flags, 0LL);
  new_position_with_flags = DWB_ENDS_BLOCK_WRITING (local_current_position_with_flags, block->block_no);

  if (!ATOMIC_CAS_64 (&dwb_Global.position_with_flags, local_current_position_with_flags, new_position_with_flags))
    {
      /* The position was changed by others, try again. */
      goto reset_bit_position;
    }

  /* Advance flushing to next block. */
  current_block_to_flush = dwb_Global.next_block_to_flush;
  next_block_to_flush = DWB_GET_NEXT_BLOCK_NO (current_block_to_flush);

  if (!ATOMIC_CAS_32 (&dwb_Global.next_block_to_flush, current_block_to_flush, next_block_to_flush))
    {
      /* I'm the only thread that can advance next block to flush. */
      assert_release (false);
    }

  /* Release locked threads, if any. */
  dwb_signal_block_completion (thread_p, block);
  if (current_position_with_flags)
    {
      *current_position_with_flags = new_position_with_flags;
    }

end:
  ATOMIC_INC_32 (&dwb_Global.blocks_flush_counter, -1);

  if (p_dwb_ordered_slots != NULL)
    {
      free_and_init (p_dwb_ordered_slots);
    }

  PERF_UTIME_TRACKER_TIME (thread_p, &time_track, PSTAT_DWB_FLUSH_BLOCK_TIME_COUNTERS);

  return error_code;
}

/*
 * dwb_acquire_next_slot () - Acquire the next slot in DWB.
 *
 * return   : Error code.
 * thread_p(in): The thread entry.
 * can_wait(in): True, if can wait to get the next slot.
 * p_dwb_slot(out): The pointer to the next slot in DWB.
 */
STATIC_INLINE int
dwb_acquire_next_slot (THREAD_ENTRY * thread_p, bool can_wait, DWB_SLOT ** p_dwb_slot)
{
  UINT64 current_position_with_flags, current_position_with_block_write_started, new_position_with_flags;
  unsigned int current_block_no, position_in_current_block;
  int error_code = NO_ERROR;
  DWB_BLOCK *block;

  assert (p_dwb_slot != NULL);
  *p_dwb_slot = NULL;

start:
  /* Get the current position in double write buffer. */
  current_position_with_flags = ATOMIC_INC_64 (&dwb_Global.position_with_flags, 0ULL);

  if (DWB_NOT_CREATED_OR_MODIFYING (current_position_with_flags))
    {
      /* Rarely happens. */
      if (DWB_IS_MODIFYING_STRUCTURE (current_position_with_flags))
	{
	  if (can_wait == false)
	    {
	      return NO_ERROR;
	    }

	  /* DWB structure change started, needs to wait. */
	  error_code = dwb_wait_for_strucure_modification (thread_p);
	  if (error_code != NO_ERROR)
	    {
	      if (error_code == ER_CSS_PTHREAD_COND_TIMEDOUT)
		{
		  /* timeout, try again */
		  goto start;
		}
	      return error_code;
	    }

	  /* Probably someone else advanced the position, try again. */
	  goto start;
	}
      else if (!DWB_IS_CREATED (current_position_with_flags))
	{
	  if (DWB_IS_ANY_BLOCK_WRITE_STARTED (current_position_with_flags))
	    {
	      /* Someone deleted the DWB, before flushing the data. */
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_DWB_DISABLED, 0);
	      return ER_DWB_DISABLED;
	    }

	  /* Someone deleted the DWB */
	  return NO_ERROR;
	}
      else
	{
	  assert (false);
	}
    }

  current_block_no = DWB_GET_BLOCK_NO_FROM_POSITION (current_position_with_flags);
  position_in_current_block = DWB_GET_POSITION_IN_BLOCK (current_position_with_flags);

  assert (current_block_no < DWB_NUM_TOTAL_BLOCKS && position_in_current_block < DWB_BLOCK_NUM_PAGES);

  if (position_in_current_block == 0)
    {
      /* This is the first write on current block. Before writing, check whether the previous iteration finished. */
      if (DWB_IS_BLOCK_WRITE_STARTED (current_position_with_flags, current_block_no))
	{
	  if (can_wait == false)
	    {
	      return NO_ERROR;
	    }

	  dwb_log ("Waits for flushing block=%d having version=%lld) \n",
		   current_block_no, dwb_Global.blocks[current_block_no].version);

	  /*
	   * The previous iteration didn't finished, needs to wait, in order to avoid buffer overwriting.
	   * Should happens relative rarely, except the case when the buffer consist in only one block.
	   */
	  error_code = dwb_wait_for_block_completion (thread_p, current_block_no);
	  if (error_code != NO_ERROR)
	    {
	      if (error_code == ER_CSS_PTHREAD_COND_TIMEDOUT)
		{
		  /* timeout, try again */
		  goto start;
		}

	      dwb_log_error ("Error %d while waiting for flushing block=%d having version %lld \n",
			     error_code, current_block_no, dwb_Global.blocks[current_block_no].version);
	      return error_code;
	    }

	  /* Probably someone else advanced the position, try again. */
	  goto start;
	}

      /* First write in the current block. */
      assert (!DWB_IS_BLOCK_WRITE_STARTED (current_position_with_flags, current_block_no));

      current_position_with_block_write_started =
	DWB_STARTS_BLOCK_WRITING (current_position_with_flags, current_block_no);

      new_position_with_flags = DWB_GET_NEXT_POSITION_WITH_FLAGS (current_position_with_block_write_started);
    }
  else
    {
      /* I'm sure that nobody else can delete the buffer */
      assert (DWB_IS_CREATED (dwb_Global.position_with_flags));
      assert (!DWB_IS_MODIFYING_STRUCTURE (dwb_Global.position_with_flags));

      /* Compute the next position with flags */
      new_position_with_flags = DWB_GET_NEXT_POSITION_WITH_FLAGS (current_position_with_flags);
    }

  /* Compute and advance the global position in double write buffer. */
  if (!ATOMIC_CAS_64 (&dwb_Global.position_with_flags, current_position_with_flags, new_position_with_flags))
    {
      /* Someone else advanced the global position in double write buffer, try again. */
      goto start;
    }

  block = dwb_Global.blocks + current_block_no;

  *p_dwb_slot = block->slots + position_in_current_block;

  /* Invalidate slot content. */
  VPID_SET_NULL (&(*p_dwb_slot)->vpid);

  assert ((*p_dwb_slot)->position_in_block == position_in_current_block);

  return NO_ERROR;
}

/*
 * dwb_set_slot_data () - Set DWB data at the location indicated by the slot.
 *
 * return   : Error code.
 * thread_p(in): Thread entry
 * dwb_slot(in/out): DWB slot that contains the location where the data must be set.
 * io_page_p(in): The data.
 */
STATIC_INLINE void
dwb_set_slot_data (THREAD_ENTRY * thread_p, DWB_SLOT * dwb_slot, FILEIO_PAGE * io_page_p)
{
  assert (dwb_slot != NULL && io_page_p != NULL);

  assert (io_page_p->prv.p_reserve_2 == 0);

  if (io_page_p->prv.pageid != NULL_PAGEID)
    {
      memcpy (dwb_slot->io_page, (char *) io_page_p, IO_PAGESIZE);
    }
  else
    {
      /* Initialize page for consistency. */
      fileio_initialize_res (thread_p, dwb_slot->io_page, IO_PAGESIZE);
    }

  assert (fileio_is_page_sane (io_page_p, IO_PAGESIZE));
  LSA_COPY (&dwb_slot->lsa, &io_page_p->prv.lsa);
  VPID_SET (&dwb_slot->vpid, io_page_p->prv.volid, io_page_p->prv.pageid);
}

/*
 * dwb_init_slot () - Initialize DWB slot.
 *
 * return   : Nothing.
 * slot (in/out) : The DWB slot.
 */
STATIC_INLINE void
dwb_init_slot (DWB_SLOT * slot)
{
  assert (slot != NULL);

  slot->io_page = NULL;
  VPID_SET_NULL (&slot->vpid);
  LSA_SET_NULL (&slot->lsa);
}

/*
 * dwb_get_next_block_for_flush(): Get next block for flush.
 *
 * returns: Nothing
 * thread_p (in): The thread entry.
 * block_no(out): The next block for flush if found, otherwise DWB_NUM_TOTAL_BLOCKS.
 */
STATIC_INLINE void
dwb_get_next_block_for_flush (THREAD_ENTRY * thread_p, unsigned int *block_no)
{
  assert (block_no != NULL);

  *block_no = DWB_NUM_TOTAL_BLOCKS;

  /* check whether the next block can be flushed. */
  if (dwb_Global.blocks[dwb_Global.next_block_to_flush].count_wb_pages != DWB_BLOCK_NUM_PAGES)
    {
      /* Next block is not full yet. */
      return;
    }

  *block_no = dwb_Global.next_block_to_flush;
}

/*
 * dwb_set_data_on_next_slot () - Sets data at the next DWB slot, if possible.
 *
 * return   : Error code.
 * thread_p(in): The thread entry.
 * io_page_p(in): The data that will be set on next slot.
 * can_wait(in): True, if waiting is allowed.
 * p_dwb_slot(out): Pointer to the next free DWB slot.
 */
int
dwb_set_data_on_next_slot (THREAD_ENTRY * thread_p, FILEIO_PAGE * io_page_p, bool can_wait, DWB_SLOT ** p_dwb_slot)
{
  int error_code;

  assert (p_dwb_slot != NULL && io_page_p != NULL);

  /* Acquire the slot before setting the data. */
  error_code = dwb_acquire_next_slot (thread_p, can_wait, p_dwb_slot);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  assert (can_wait == false || *p_dwb_slot != NULL);
  if (*p_dwb_slot == NULL)
    {
      /* Can't acquire next slot. */
      return NO_ERROR;
    }

  /* Set data on slot. */
  dwb_set_slot_data (thread_p, *p_dwb_slot, io_page_p);

  return NO_ERROR;
}

/*
 * dwb_add_page () - Add page content to DWB.
 *
 * return   : Error code.
 * thread_p (in): The thread entry.
 * io_page_p(in): In-memory address where the current content of page resides.
 * vpid(in): Page identifier.
 * p_dwb_slot(in/out): DWB slot where the page content must be added.
 *
 *  Note: thread may flush the block, if flush thread is not available or we are in stand alone.
 */
int
dwb_add_page (THREAD_ENTRY * thread_p, FILEIO_PAGE * io_page_p, VPID * vpid, DWB_SLOT ** p_dwb_slot)
{
  unsigned int count_wb_pages;
  int error_code = NO_ERROR;
  bool inserted = false;
  DWB_BLOCK *block = NULL;
  DWB_SLOT *dwb_slot = NULL;
  bool needs_flush;

  assert (p_dwb_slot != NULL && (io_page_p != NULL || (*p_dwb_slot)->io_page != NULL) && vpid != NULL);

  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  if (*p_dwb_slot == NULL)
    {
      error_code = dwb_set_data_on_next_slot (thread_p, io_page_p, true, p_dwb_slot);
      if (error_code != NO_ERROR)
	{
	  return error_code;
	}

      if (*p_dwb_slot == NULL)
	{
	  return NO_ERROR;
	}
    }

  dwb_slot = *p_dwb_slot;

  assert (VPID_EQ (vpid, &dwb_slot->vpid));
  if (!VPID_ISNULL (vpid))
    {
      error_code = dwb_slots_hash_insert (thread_p, vpid, dwb_slot, &inserted);
      if (error_code != NO_ERROR)
	{
	  return error_code;
	}

      if (!inserted)
	{
	  /* Invalidate the slot to avoid flushing the same data twice. */
	  VPID_SET_NULL (&dwb_slot->vpid);
	  fileio_initialize_res (thread_p, dwb_slot->io_page, IO_PAGESIZE);
	}
    }

  dwb_log ("dwb_add_page: added page = (%d,%d) on block (%d) position (%d)\n", vpid->volid, vpid->pageid,
	   dwb_slot->block_no, dwb_slot->position_in_block);

  block = &dwb_Global.blocks[dwb_slot->block_no];
  count_wb_pages = ATOMIC_INC_32 (&block->count_wb_pages, 1);
  assert_release (count_wb_pages <= DWB_BLOCK_NUM_PAGES);

  if (count_wb_pages < DWB_BLOCK_NUM_PAGES)
    {
      needs_flush = false;
    }
  else
    {
      needs_flush = true;
    }

  if (needs_flush == false)
    {
      return NO_ERROR;
    }

  /*
   * The blocks must be flushed in the order they are filled to have consistent data. The flush block thread knows
   * how to flush the blocks in the order they are filled. So, we don't care anymore about the flushing order here.
   * Initially, we waited here if the previous block was not flushed. That approach created delays.
   */

#if defined (SERVER_MODE)
  /*
   * Wake ups flush block thread to flush the current block. The current block will be flushed after flushing the
   * previous block.
   */
  if (dwb_is_flush_block_daemon_available ())
    {
      /* Wakeup the thread that will flush the block. */
      dwb_flush_block_daemon->wakeup ();

      return NO_ERROR;
    }
#endif /* SERVER_MODE */

  /* Flush all pages from current block */
  error_code = dwb_flush_block (thread_p, block, false, NULL);
  if (error_code != NO_ERROR)
    {
      dwb_log_error ("Can't flush block = %d having version %lld\n", block->block_no, block->version);

      return error_code;
    }

  dwb_log ("Successfully flushed DWB block = %d having version %lld\n", block->block_no, block->version);

  return NO_ERROR;
}

/*
 * dwb_is_created () - Checks whether double write buffer was created.
 *
 * return   : True, if created.
 */
bool
dwb_is_created (void)
{
  UINT64 position_with_flags = ATOMIC_INC_64 (&dwb_Global.position_with_flags, 0ULL);

  return DWB_IS_CREATED (position_with_flags);
}

/*
 * dwb_create () - Create DWB.
 *
 * return   : Error code.
 * thread_p (in): The thread entry.
 * dwb_path_p (in) : The double write buffer volume path.
 * db_name_p (in) : The database name.
 */
int
dwb_create (THREAD_ENTRY * thread_p, const char *dwb_path_p, const char *db_name_p)
{
  UINT64 current_position_with_flags;
  int error_code = NO_ERROR;

  error_code = dwb_starts_structure_modification (thread_p, &current_position_with_flags);
  if (error_code != NO_ERROR)
    {
      dwb_log_error ("Can't create DWB: error = %d\n", error_code);
      return error_code;
    }

  /* DWB structure modification started, no other transaction can modify the global position with flags */
  if (DWB_IS_CREATED (current_position_with_flags))
    {
      /* Already created, restore the modification flag. */
      goto end;
    }

  fileio_make_dwb_name (dwb_Volume_name, dwb_path_p, db_name_p);

  error_code = dwb_create_internal (thread_p, dwb_Volume_name, &current_position_with_flags);
  if (error_code != NO_ERROR)
    {
      dwb_log_error ("Can't create DWB: error = %d\n", error_code);
      goto end;
    }

end:
  /* Ends the modification, allowing to others to modify global position with flags. */
  dwb_ends_structure_modification (thread_p, current_position_with_flags);

  return error_code;
}

/*
 * dwb_recreate () - Recreate double write buffer with new user parameter values.
 *
 * return   : Error code.
 * thread_p (in): The thread entry.
 */
int
dwb_recreate (THREAD_ENTRY * thread_p)
{
  int error_code = NO_ERROR;
  UINT64 current_position_with_flags;

  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  error_code = dwb_starts_structure_modification (thread_p, &current_position_with_flags);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  /* DWB structure modification started, no other transaction can modify the global position with flags */
  if (DWB_IS_CREATED (current_position_with_flags))
    {
      dwb_destroy_internal (thread_p, &current_position_with_flags);
    }

  error_code = dwb_create_internal (thread_p, dwb_Volume_name, &current_position_with_flags);
  if (error_code != NO_ERROR)
    {
      goto end;
    }

end:
  /* Ends the modification, allowing to others to modify global position with flags. */
  dwb_ends_structure_modification (thread_p, current_position_with_flags);

  return error_code;
}

#if !defined (NDEBUG)
/*
 * dwb_debug_check_dwb () - check sanity of ordered slots
 *
 * return   : Error code
 * thread_p (in): The thread entry.
 * p_dwb_ordered_slots(in):
 * num_dwb_pages(in):
 *
 */
static int
dwb_debug_check_dwb (THREAD_ENTRY * thread_p, DWB_SLOT * p_dwb_ordered_slots, unsigned int num_dwb_pages)
{
  char page_buf[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT];
  FILEIO_PAGE *iopage;
  int error_code;
  unsigned int i;
  bool is_page_corrupted;

  iopage = (FILEIO_PAGE *) PTR_ALIGN (page_buf, MAX_ALIGNMENT);
  memset (iopage, 0, IO_PAGESIZE);

  /* Check for duplicates in DWB. */
  for (i = 1; i < num_dwb_pages; i++)
    {
      if (VPID_ISNULL (&p_dwb_ordered_slots[i].vpid))
	{
	  continue;
	}

      if (VPID_EQ (&p_dwb_ordered_slots[i].vpid, &p_dwb_ordered_slots[i - 1].vpid))
	{
	  /* Same VPID, at least one is corrupted. */
	  error_code = fileio_page_check_corruption (thread_p, p_dwb_ordered_slots[i - 1].io_page, &is_page_corrupted);
	  if (error_code != NO_ERROR)
	    {
	      return error_code;
	    }

	  if (is_page_corrupted)
	    {
	      continue;
	    }

	  error_code = fileio_page_check_corruption (thread_p, p_dwb_ordered_slots[i].io_page, &is_page_corrupted);
	  if (error_code != NO_ERROR)
	    {
	      return error_code;
	    }

	  if (is_page_corrupted)
	    {
	      continue;
	    }

	  if (memcmp (p_dwb_ordered_slots[i - 1].io_page, iopage, IO_PAGESIZE) == 0)
	    {
	      /* Skip not initialized pages. */
	      continue;
	    }

	  if (memcmp (p_dwb_ordered_slots[i].io_page, iopage, IO_PAGESIZE) == 0)
	    {
	      /* Skip not initialized pages. */
	      continue;
	    }

	  /* Found duplicates - something is wrong. We may still can check for same LSAs.
	   * But, duplicates occupies disk space so is better to avoid it.
	   */
	  assert (false);
	}
    }

  return NO_ERROR;
}
#endif // DEBUG

/*
 * dwb_check_data_page_is_sane () - Check whether the data page is corrupted.
 *
 * return   : Error code
 * thread_p (in): The thread entry.
 * block(in): DWB recovery block.
 * p_dwb_ordered_slots(in): DWB ordered slots
 * p_num_recoverable_pages(out): number of recoverable corrupted pages
 *
 */
static int
dwb_check_data_page_is_sane (THREAD_ENTRY * thread_p, DWB_BLOCK * rcv_block, DWB_SLOT * p_dwb_ordered_slots,
			     int *p_num_recoverable_pages)
{
  char page_buf[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT];
  FILEIO_PAGE *iopage;
  VPID *vpid;
  int vol_fd = NULL_VOLDES, temp_vol_fd = NULL_VOLDES, vol_pages = 0;
  INT16 volid;
  int error_code;
  unsigned int i;
  int num_recoverable_pages = 0;
  bool is_page_corrupted;

  assert (rcv_block != NULL && p_dwb_ordered_slots != NULL && p_num_recoverable_pages != NULL);
  iopage = (FILEIO_PAGE *) PTR_ALIGN (page_buf, MAX_ALIGNMENT);
  memset (iopage, 0, IO_PAGESIZE);

  volid = NULL_VOLID;

  /* Check whether the data page is corrupted. If true, replaced with the DWB page. */
  for (i = 0; i < rcv_block->count_wb_pages; i++)
    {
      vpid = &p_dwb_ordered_slots[i].vpid;
      if (VPID_ISNULL (vpid))
	{
	  continue;
	}

      if (volid != vpid->volid)
	{
	  /* Update the current VPID and get the volume descriptor. */
	  temp_vol_fd = fileio_get_volume_descriptor (vpid->volid);
	  if (temp_vol_fd == NULL_VOLDES)
	    {
	      continue;
	    }
	  vol_fd = temp_vol_fd;
	  volid = vpid->volid;
	  vol_pages = fileio_get_number_of_volume_pages (vol_fd, IO_PAGESIZE);
	}

      assert (vol_fd != NULL_VOLDES);

      if (vpid->pageid >= vol_pages)
	{
	  /* The page was written in DWB, not in data volume. */
	  continue;
	}

      /* Read the page from data volume. */
      if (fileio_read (thread_p, vol_fd, iopage, vpid->pageid, IO_PAGESIZE) == NULL)
	{
	  /* There was an error in reading the page. */
	  ASSERT_ERROR_AND_SET (error_code);
	  return error_code;
	}

      error_code = fileio_page_check_corruption (thread_p, iopage, &is_page_corrupted);
      if (error_code != NO_ERROR)
	{
	  return error_code;
	}

      if (!is_page_corrupted)
	{
	  /* The page in data volume is not corrupted. Do not overwrite its content - reset slot VPID. */
	  VPID_SET_NULL (&p_dwb_ordered_slots[i].vpid);
	  fileio_initialize_res (thread_p, p_dwb_ordered_slots[i].io_page, IO_PAGESIZE);
	  continue;
	}

      /* Corrupted page in data volume. Check DWB. */
      error_code = fileio_page_check_corruption (thread_p, p_dwb_ordered_slots[i].io_page, &is_page_corrupted);
      if (error_code != NO_ERROR)
	{
	  return error_code;
	}

      if (is_page_corrupted)
	{
	  /* The page is corrupted in data volume and DWB. Something wrong happened. */
	  assert_release (false);
	  dwb_log_error ("Can't recover page = (%d,%d)\n", vpid->volid, vpid->pageid);
	  return ER_FAILED;
	}

      /* The page content in data volume will be replaced later with the DWB page content. */
      dwb_log ("page = (%d,%d) is recovered with DWB.\n", vpid->volid, vpid->pageid);
      num_recoverable_pages++;
    }

  *p_num_recoverable_pages = num_recoverable_pages;
  return NO_ERROR;
}

/*
 * dwb_load_and_recover_pages () - Load and recover pages from DWB.
 *
 * return   : Error code.
 * thread_p (in): The thread entry.
 * dwb_path_p (in): The double write buffer path.
 * db_name_p (in): The database name.
 *
 *  Note: This function is called at recovery. The corrupted pages are recovered from double write volume buffer disk.
 *    Then, double write volume buffer disk is recreated according to user specifications.
 *    Currently we use a DWB block in memory to recover corrupted page.
 */
int
dwb_load_and_recover_pages (THREAD_ENTRY * thread_p, const char *dwb_path_p, const char *db_name_p)
{
  int error_code = NO_ERROR, read_fd = NULL_VOLDES;
  unsigned int num_dwb_pages, ordered_slots_length, i;
  DWB_BLOCK *rcv_block = NULL;
  DWB_SLOT *p_dwb_ordered_slots = NULL;
  FILEIO_PAGE *iopage;
  int num_recoverable_pages;

  assert (dwb_Global.vdes == NULL_VOLDES);

  dwb_check_logging ();

  fileio_make_dwb_name (dwb_Volume_name, dwb_path_p, db_name_p);

  if (fileio_is_volume_exist (dwb_Volume_name))
    {
      /* Open DWB volume first */
      read_fd = fileio_mount (thread_p, boot_db_full_name (), dwb_Volume_name, LOG_DBDWB_VOLID, false, false);
      if (read_fd == NULL_VOLDES)
	{
	  return ER_IO_MOUNT_FAIL;
	}

      num_dwb_pages = fileio_get_number_of_volume_pages (read_fd, IO_PAGESIZE);
      dwb_log ("dwb_load_and_recover_pages: The number of pages in DWB %d\n", num_dwb_pages);

      /* We are in recovery phase. The system may be restarted with DWB size different than parameter value.
       * There may be one of the following two reasons:
       *   - the user may intentionally modified the DWB size, before restarting.
       *   - DWB size didn't changed, but the previous DWB was created, partially flushed and the system crashed.
       * We know that a valid DWB size must be a power of 2. In this case we recover from DWB. Otherwise, skip
       * recovering from DWB - the modifications are not reflected in data pages.
       * Another approach would be to recover, even if the DWB size is not a power of 2 (DWB partially flushed).
       */
      if ((num_dwb_pages > 0) && IS_POWER_OF_2 (num_dwb_pages))
	{
	  /* Create DWB block for recovery purpose. */
	  error_code = dwb_create_blocks (thread_p, 1, num_dwb_pages, &rcv_block);
	  if (error_code != NO_ERROR)
	    {
	      goto end;
	    }

	  /* Read pages in block write area. This means that slot pages are set. */
	  if (fileio_read_pages (thread_p, read_fd, rcv_block->write_buffer, 0, num_dwb_pages, IO_PAGESIZE) == NULL)
	    {
	      error_code = ER_FAILED;
	      goto end;
	    }

	  /* Set slots VPID and LSA from pages. */
	  for (i = 0; i < num_dwb_pages; i++)
	    {
	      iopage = rcv_block->slots[i].io_page;

	      VPID_SET (&rcv_block->slots[i].vpid, iopage->prv.volid, iopage->prv.pageid);
	      LSA_COPY (&rcv_block->slots[i].lsa, &iopage->prv.lsa);
	    }
	  rcv_block->count_wb_pages = num_dwb_pages;

	  /* Order slots by VPID, to flush faster. */
	  error_code = dwb_block_create_ordered_slots (rcv_block, &p_dwb_ordered_slots, &ordered_slots_length);
	  if (error_code != NO_ERROR)
	    {
	      error_code = ER_FAILED;
	      goto end;
	    }

	  /* Remove duplicates. Normally, we do not expect duplicates in DWB. However, this happens if
	   * the system crashes in the middle of flushing into double write file. In this case, some pages in DWB
	   * are from the last DWB flush and the other from the previous DWB flush.
	   */
	  for (i = 0; i < rcv_block->count_wb_pages - 1; i++)
	    {
	      DWB_SLOT *s1, *s2;

	      s1 = &p_dwb_ordered_slots[i];
	      s2 = &p_dwb_ordered_slots[i + 1];

	      if (!VPID_ISNULL (&s1->vpid) && VPID_EQ (&s1->vpid, &s2->vpid))
		{
		  /* Next slot contains the same page. Search for the oldest version. */
		  assert (LSA_LE (&s1->lsa, &s2->lsa));

		  dwb_log ("dwb_load_and_recover_pages: Found duplicates in DWB at positions = (%d,%d) %d\n",
			   s1->position_in_block, s2->position_in_block);

		  if (LSA_LT (&s1->lsa, &s2->lsa))
		    {
		      /* Invalidate the oldest page version. */
		      VPID_SET_NULL (&s1->vpid);
		      dwb_log ("dwb_load_and_recover_pages: Invalidated the page at position = (%d)\n",
			       s1->position_in_block);
		    }
		  else
		    {
		      /* Same LSA. This is the case when page was modified without setting LSA.
		       * The first appearance in DWB contains the oldest page modification - last flush in DWB!
		       */
		      assert (s1->position_in_block != s2->position_in_block);

		      if (s1->position_in_block < s2->position_in_block)
			{
			  /* Page of s1 is valid. */
			  VPID_SET_NULL (&s2->vpid);
			  dwb_log ("dwb_load_and_recover_pages: Invalidated the page at position = (%d)\n",
				   s2->position_in_block);
			}
		      else
			{
			  /* Page of s2 is valid. */
			  VPID_SET_NULL (&s1->vpid);
			  dwb_log ("dwb_load_and_recover_pages: Invalidated the page at position = (%d)\n",
				   s1->position_in_block);
			}
		    }
		}
	    }

#if !defined (NDEBUG)
	  // check sanity of ordered slots
	  error_code = dwb_debug_check_dwb (thread_p, p_dwb_ordered_slots, num_dwb_pages);
	  if (error_code != NO_ERROR)
	    {
	      goto end;
	    }
#endif // DEBUG

	  /* Check whether the data page is corrupted. If the case, it will be replaced with the DWB page. */
	  error_code = dwb_check_data_page_is_sane (thread_p, rcv_block, p_dwb_ordered_slots, &num_recoverable_pages);
	  if (error_code != NO_ERROR)
	    {
	      goto end;
	    }

	  if (0 < num_recoverable_pages)
	    {
	      /* Replace the corrupted pages in data volume with the DWB content. */
	      error_code =
		dwb_write_block (thread_p, rcv_block, p_dwb_ordered_slots, ordered_slots_length, false, false);
	      if (error_code != NO_ERROR)
		{
		  goto end;
		}

	      /* Now, flush the volumes having pages in current block. */
	      for (i = 0; i < rcv_block->count_flush_volumes_info; i++)
		{
		  if (fileio_synchronize (thread_p, rcv_block->flush_volumes_info[i].vdes, NULL,
					  FILEIO_SYNC_ONLY) == NULL_VOLDES)
		    {
		      error_code = ER_FAILED;
		      goto end;
		    }

		  dwb_log ("dwb_load_and_recover_pages: Synchronized volume %d\n",
			   rcv_block->flush_volumes_info[i].vdes);
		}

	      rcv_block->count_flush_volumes_info = 0;
	    }

	  assert (rcv_block->count_flush_volumes_info == 0);
	}

      /* Dismount the file. */
      fileio_dismount (thread_p, read_fd);

      /* Destroy the old file, since data recovered. */
      fileio_unformat (thread_p, dwb_Volume_name);
      read_fd = NULL_VOLDES;
    }

  /* Since old file destroyed, now we can rebuild the new double write buffer with user specifications. */
  error_code = dwb_create (thread_p, dwb_path_p, db_name_p);
  if (error_code != NO_ERROR)
    {
      dwb_log_error ("Can't create DWB \n");
    }

end:
  /* Do not remove the old file if an error occurs. */
  if (p_dwb_ordered_slots != NULL)
    {
      free_and_init (p_dwb_ordered_slots);
    }

  if (rcv_block != NULL)
    {
      dwb_finalize_block (rcv_block);
      free_and_init (rcv_block);
    }

  return error_code;
}

/*
 * dwb_destroy () - Destroy DWB.
 *
 * return   : Error code.
 * thread_p (in): The thread entry.
 */
int
dwb_destroy (THREAD_ENTRY * thread_p)
{
  int error_code = NO_ERROR;
  UINT64 current_position_with_flags;

  error_code = dwb_starts_structure_modification (thread_p, &current_position_with_flags);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  /* DWB structure modification started, no other transaction can modify the global position with flags */
  if (!DWB_IS_CREATED (current_position_with_flags))
    {
      /* Not created, nothing to destroy, restore the modification flag. */
      goto end;
    }

  dwb_destroy_internal (thread_p, &current_position_with_flags);

end:
  /* Ends the modification, allowing to others to modify global position with flags. */
  dwb_ends_structure_modification (thread_p, current_position_with_flags);

  /* DWB is destroyed, */
#if defined(SERVER_MODE)
  dwb_daemons_destroy ();
#endif

  return error_code;
}

/*
 * dwb_get_volume_name - Get the double write volume name.
 * return   : DWB volume name.
 */
char *
dwb_get_volume_name (void)
{
  if (dwb_is_created ())
    {
      return dwb_Volume_name;
    }
  else
    {
      return NULL;
    }
}

/*
 * dwb_flush_next_block(): Flush next block.
 *
 *   returns: error code
 * thread_p (in): The thread entry.
 */
static int
dwb_flush_next_block (THREAD_ENTRY * thread_p)
{
  unsigned int block_no;
  DWB_BLOCK *flush_block = NULL;
  int error_code = NO_ERROR;
  UINT64 position_with_flags;

start:
  position_with_flags = ATOMIC_INC_64 (&dwb_Global.position_with_flags, 0ULL);
  if (!DWB_IS_CREATED (position_with_flags) || DWB_IS_MODIFYING_STRUCTURE (position_with_flags))
    {
      return NO_ERROR;
    }

  dwb_get_next_block_for_flush (thread_p, &block_no);
  if (block_no < DWB_NUM_TOTAL_BLOCKS)
    {
      flush_block = &dwb_Global.blocks[block_no];

      /* Flush all pages from current block */
      assert (flush_block != NULL && flush_block->count_wb_pages == DWB_BLOCK_NUM_PAGES);

      assert ((DWB_GET_PREV_BLOCK (flush_block->block_no)->version > flush_block->version) ||
	      (flush_block->block_no == 0
	       && (DWB_GET_PREV_BLOCK (flush_block->block_no)->version == flush_block->version))
	      || (flush_block->version == UINT64_MAX
		  && (DWB_GET_PREV_BLOCK (flush_block->block_no)->version < flush_block->version)));

      error_code = dwb_flush_block (thread_p, flush_block, true, NULL);
      if (error_code != NO_ERROR)
	{
	  /* Something wrong happened. */
	  dwb_log_error ("Can't flush block = %d having version %lld\n", flush_block->block_no, flush_block->version);

	  return error_code;
	}

      dwb_log ("Successfully flushed DWB block = %d having version %lld\n",
	       flush_block->block_no, flush_block->version);

      /* Check whether is another block available for flush. */
      goto start;
    }

  return NO_ERROR;
}

/*
 * dwb_flush_force () - Force flushing the current content of DWB.
 *
 * return   : Error code.
 * thread_p (in): The thread entry.
 * all_sync (out): True, if everything synchronized.
 */
int
dwb_flush_force (THREAD_ENTRY * thread_p, bool * all_sync)
{
  UINT64 initial_position_with_flags, current_position_with_flags, prev_position_with_flags;
  UINT64 initial_block_version, current_block_version;
  int initial_block_no, current_block_no = DWB_NUM_TOTAL_BLOCKS;
  char page_buf[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT];
  FILEIO_PAGE *iopage = NULL;
  VPID null_vpid = { NULL_VOLID, NULL_PAGEID };
  int error_code = NO_ERROR;
  DWB_SLOT *dwb_slot = NULL;
  unsigned int count_added_pages = 0, max_pages_to_add = 0, initial_num_pages = 0;
  DWB_BLOCK *initial_block;
  PERF_UTIME_TRACKER time_track;
  int block_no;

  assert (all_sync != NULL);

  PERF_UTIME_TRACKER_START (thread_p, &time_track);

  *all_sync = false;

start:
  initial_position_with_flags = ATOMIC_INC_64 (&dwb_Global.position_with_flags, 0ULL);
  dwb_log ("dwb_flush_force: Started with initital position = %lld\n", initial_position_with_flags);

#if !defined (NDEBUG)
  if (dwb_Global.blocks != NULL)
    {
      for (block_no = 0; block_no < (int) DWB_NUM_TOTAL_BLOCKS; block_no++)
	{
	  dwb_log_error ("dwb_flush_force start: Block %d, Num pages = %d, version = %lld\n",
			 block_no, dwb_Global.blocks[block_no].count_wb_pages, dwb_Global.blocks[block_no].version);
	}
    }
#endif

  if (DWB_NOT_CREATED_OR_MODIFYING (initial_position_with_flags))
    {
      if (!DWB_IS_CREATED (initial_position_with_flags))
	{
	  /* Nothing to do. Everything flushed. */
	  assert (dwb_Global.file_sync_helper_block == NULL);
	  dwb_log ("dwb_flush_force: Everything flushed\n");
	  goto end;
	}

      if (DWB_IS_MODIFYING_STRUCTURE (initial_position_with_flags))
	{
	  /* DWB structure change started, needs to wait for flush. */
	  error_code = dwb_wait_for_strucure_modification (thread_p);
	  if (error_code != NO_ERROR)
	    {
	      if (error_code == ER_CSS_PTHREAD_COND_TIMEDOUT)
		{
		  /* timeout, try again */
		  goto start;
		}
	      dwb_log_error ("dwb_flush_force : Error %d while waiting for structure modification=%lld\n",
			     error_code, ATOMIC_INC_64 (&dwb_Global.position_with_flags, 0ULL));
	      return error_code;
	    }
	}
    }

  if (DWB_GET_BLOCK_STATUS (initial_position_with_flags) == 0)
    {
      /* Check helper flush block. */
      initial_block_no = DWB_GET_BLOCK_NO_FROM_POSITION (initial_position_with_flags);
      initial_block = dwb_Global.file_sync_helper_block;
      if (initial_block == NULL)
	{
	  /* Nothing to flush. */
	  goto end;
	}

      goto wait_for_file_sync_helper_block;
    }

  /* Search for latest not flushed block - not flushed yet, having highest version. */
  initial_block_no = -1;
  initial_block_version = 0;
  for (block_no = 0; block_no < (int) DWB_NUM_TOTAL_BLOCKS; block_no++)
    {
      if (DWB_IS_BLOCK_WRITE_STARTED (initial_position_with_flags, block_no)
	  && (dwb_Global.blocks[block_no].version >= initial_block_version))
	{
	  initial_block_no = block_no;
	  initial_block_version = dwb_Global.blocks[initial_block_no].version;
	}
    }

  /* At least one block was not flushed. */
  assert (initial_block_no != -1);

  initial_num_pages = dwb_Global.blocks[initial_block_no].count_wb_pages;
  if (initial_position_with_flags != ATOMIC_INC_64 (&dwb_Global.position_with_flags, 0ULL))
    {
      /* The position_with_flags was modified meanwhile by concurrent threads. */
      goto start;
    }
  prev_position_with_flags = initial_position_with_flags;

  max_pages_to_add = DWB_BLOCK_NUM_PAGES - initial_num_pages;

  iopage = (FILEIO_PAGE *) PTR_ALIGN (page_buf, MAX_ALIGNMENT);
  memset (iopage, 0, IO_MAX_PAGE_SIZE);
  fileio_initialize_res (thread_p, iopage, IO_PAGESIZE);

  dwb_log ("dwb_flush_force: Waits for flushing the block %d having version %lld and %d pages\n",
	   initial_block_no, initial_block_version, initial_num_pages);

  /* Check whether the initial block was flushed */
check_flushed_blocks:

  assert (initial_block_no >= 0);
  if ((ATOMIC_INC_32 (&dwb_Global.blocks_flush_counter, 0) > 0)
      && (ATOMIC_INC_32 (&dwb_Global.next_block_to_flush, 0) == (unsigned int) initial_block_no)
      && (ATOMIC_INC_32 (&dwb_Global.blocks[initial_block_no].count_wb_pages, 0) == DWB_BLOCK_NUM_PAGES))
    {
      /* The initial block is currently flushing, wait for it. */
      error_code = dwb_wait_for_block_completion (thread_p, initial_block_no);
      if (error_code != NO_ERROR)
	{
	  if (error_code == ER_CSS_PTHREAD_COND_TIMEDOUT)
	    {
	      /* timeout, try again */
	      goto check_flushed_blocks;
	    }

	  dwb_log_error ("dwb_flush_force : Error %d while waiting for block completion = %lld\n",
			 error_code, ATOMIC_INC_64 (&dwb_Global.position_with_flags, 0ULL));
	  return error_code;
	}

      goto check_flushed_blocks;
    }

  /* Read again the current position and check whether initial block was flushed. */
  current_position_with_flags = ATOMIC_INC_64 (&dwb_Global.position_with_flags, 0ULL);
  if (DWB_NOT_CREATED_OR_MODIFYING (current_position_with_flags))
    {
      if (!DWB_IS_CREATED (current_position_with_flags))
	{
	  /* Nothing to do. Everything flushed. */
	  assert (dwb_Global.file_sync_helper_block == NULL);
	  dwb_log ("dwb_flush_force: Everything flushed\n");
	  goto end;
	}

      if (DWB_IS_MODIFYING_STRUCTURE (current_position_with_flags))
	{
	  /* DWB structure change started, needs to wait for flush. */
	  error_code = dwb_wait_for_strucure_modification (thread_p);
	  if (error_code != NO_ERROR)
	    {
	      if (error_code == ER_CSS_PTHREAD_COND_TIMEDOUT)
		{
		  /* timeout, try again */
		  goto check_flushed_blocks;
		}

	      dwb_log_error ("dwb_flush_force : Error %d while waiting for structure modification = %lld\n",
			     error_code, ATOMIC_INC_64 (&dwb_Global.position_with_flags, 0ULL));
	      return error_code;
	    }
	}
    }

  if (!DWB_IS_BLOCK_WRITE_STARTED (current_position_with_flags, initial_block_no))
    {
      /* Check helper flush block. */
      goto wait_for_file_sync_helper_block;
    }

  /* Check whether initial block content was overwritten. */
  current_block_no = DWB_GET_BLOCK_NO_FROM_POSITION (current_position_with_flags);
  current_block_version = dwb_Global.blocks[current_block_no].version;

  if ((current_block_no == initial_block_no) && (current_block_version != initial_block_version))
    {
      assert (current_block_version > initial_block_version);

      /* Check helper flush block. */
      goto wait_for_file_sync_helper_block;
    }

  if (current_position_with_flags == prev_position_with_flags && count_added_pages < max_pages_to_add)
    {
      /* The system didn't advanced, add null pages to force flush block. */
      dwb_slot = NULL;

      error_code = dwb_add_page (thread_p, iopage, &null_vpid, &dwb_slot);
      if (error_code != NO_ERROR)
	{
	  dwb_log_error ("dwb_flush_force : Error %d while adding page = %lld\n",
			 error_code, ATOMIC_INC_64 (&dwb_Global.position_with_flags, 0ULL));
	  return error_code;
	}
      else if (dwb_slot == NULL)
	{
	  /* DWB disabled meanwhile, everything flushed. */
	  assert (dwb_Global.file_sync_helper_block == NULL);
	  assert (!DWB_IS_CREATED (ATOMIC_INC_64 (&dwb_Global.position_with_flags, 0ULL)));
	  dwb_log ("dwb_flush_force: DWB disabled = %lld\n", ATOMIC_INC_64 (&dwb_Global.position_with_flags, 0ULL));
	  goto end;
	}

      count_added_pages++;
    }

  prev_position_with_flags = current_position_with_flags;
  goto check_flushed_blocks;

wait_for_file_sync_helper_block:
  dwb_log ("dwb_flush_force: Wait for helper flush = %lld\n", ATOMIC_INC_64 (&dwb_Global.position_with_flags, 0ULL));
  initial_block = &dwb_Global.blocks[initial_block_no];

#if defined (SERVER_MODE)
  while (dwb_Global.file_sync_helper_block == initial_block)
    {
      /* Wait for file sync helper thread to finish. */
      thread_sleep (1);
    }
#endif

end:
  *all_sync = true;

  dwb_log ("dwb_flush_force: Ended with position = %lld\n", ATOMIC_INC_64 (&dwb_Global.position_with_flags, 0ULL));
  PERF_UTIME_TRACKER_TIME_AND_RESTART (thread_p, &time_track, PSTAT_DWB_FLUSH_FORCE_TIME_COUNTERS);

#if !defined (NDEBUG)
  if (dwb_Global.blocks != NULL)
    {
      for (block_no = 0; block_no < (int) DWB_NUM_TOTAL_BLOCKS; block_no++)
	{
	  dwb_log_error ("dwb_flush_force end: Block %d, Num pages = %d, version = %lld\n",
			 block_no, dwb_Global.blocks[block_no].count_wb_pages, dwb_Global.blocks[block_no].version);
	}
    }
#endif

  return NO_ERROR;
}

/*
 * dwb_file_sync_helper () - Helps file sync.
 *
 * return   : Error code.
 * thread_p (in): Thread entry.
 */
static int
dwb_file_sync_helper (THREAD_ENTRY * thread_p)
{
  unsigned int i;
  int num_pages, num_pages2, num_pages_to_sync;
  DWB_BLOCK *block = NULL;
  UINT64 position_with_flags;
  FLUSH_VOLUME_INFO *current_flush_volume_info = NULL;
  unsigned int count_flush_volumes_info = 0;
  bool all_block_pages_written = false, need_wait = false, can_flush_volume = false;
  unsigned int start_flush_volume = 0;
  int first_partial_flushed_volume = -1;
  PERF_UTIME_TRACKER time_track;

  PERF_UTIME_TRACKER_START (thread_p, &time_track);
  num_pages_to_sync = prm_get_integer_value (PRM_ID_PB_SYNC_ON_NFLUSH);

  position_with_flags = ATOMIC_INC_64 (&dwb_Global.position_with_flags, 0ULL);
  if (!DWB_IS_CREATED (position_with_flags) || DWB_IS_MODIFYING_STRUCTURE (position_with_flags))
    {
      /* Needs to modify structure. Stop flushing. */
      return NO_ERROR;
    }

  block = (DWB_BLOCK *) dwb_Global.file_sync_helper_block;
  if (block == NULL)
    {
      return NO_ERROR;
    }

  do
    {
      count_flush_volumes_info = block->count_flush_volumes_info;
      all_block_pages_written = block->all_pages_written;
      need_wait = false;
      first_partial_flushed_volume = -1;

      for (i = start_flush_volume; i < count_flush_volumes_info; i++)
	{
	  current_flush_volume_info = &block->flush_volumes_info[i];

	  if (current_flush_volume_info->flushed_status != VOLUME_FLUSHED_BY_DWB_FILE_SYNC_HELPER_THREAD)
	    {
	      if (!ATOMIC_CAS_32 (&current_flush_volume_info->flushed_status, VOLUME_NOT_FLUSHED,
				  VOLUME_FLUSHED_BY_DWB_FILE_SYNC_HELPER_THREAD))
		{
		  /* Flushed by DWB flusher, skip it. */
		  assert_release (current_flush_volume_info->flushed_status == VOLUME_FLUSHED_BY_DWB_FLUSH_THREAD);
		  continue;
		}
	    }

	  /* I'm the flusher of the volume. */
	  assert_release (current_flush_volume_info->flushed_status == VOLUME_FLUSHED_BY_DWB_FILE_SYNC_HELPER_THREAD);

	  num_pages = ATOMIC_INC_32 (&current_flush_volume_info->num_pages, 0);
	  if (num_pages < num_pages_to_sync)
	    {
	      if (current_flush_volume_info->all_pages_written == true)
		{
		  if (num_pages == 0)
		    {
		      /* Already flushed. */
		      continue;
		    }

		  /* Needs flushing. */
		}
	      else
		{
		  /* Not enough pages, check the other volumes and retry. */
		  assert (all_block_pages_written == false);

		  if (first_partial_flushed_volume == -1)
		    {
		      first_partial_flushed_volume = i;
		    }

		  need_wait = true;
		  break;
		}
	    }
	  else if (current_flush_volume_info->all_pages_written == false)
	    {
	      if (first_partial_flushed_volume == -1)
		{
		  first_partial_flushed_volume = i;
		}
	    }

	  /* Reset the number of pages in volume. */
	  num_pages2 = ATOMIC_TAS_32 (&current_flush_volume_info->num_pages, 0);
	  assert_release (num_pages2 >= num_pages);

	  /*
	   * Flush the volume. If not all volume pages are available now, continue with next volume, if any,
	   * and then resume the current one.
	   */
	  (void) fileio_synchronize (thread_p, current_flush_volume_info->vdes, NULL, FILEIO_SYNC_ONLY);

	  dwb_log ("dwb_file_sync_helper: Synchronized volume %d\n", current_flush_volume_info->vdes);
	}

      /* Set next volume to flush. */
      if (first_partial_flushed_volume != -1)
	{
	  assert ((first_partial_flushed_volume >= 0)
		  && ((unsigned int) first_partial_flushed_volume < count_flush_volumes_info));

	  /* Continue with partial flushed volume. */
	  start_flush_volume = first_partial_flushed_volume;
	}
      else
	{
	  /* Continue with the next volume. */
	  start_flush_volume = count_flush_volumes_info;
	}

      can_flush_volume = false;
      if (count_flush_volumes_info != block->count_flush_volumes_info)
	{
	  /* Do not wait since a new volume arrived. */
	  can_flush_volume = true;
	}
      else if (all_block_pages_written == false)
	{
	  /* Not all pages written at the beginning of the iteration, check whether new data arrived. */
	  if (first_partial_flushed_volume != -1)
	    {
	      current_flush_volume_info = &block->flush_volumes_info[first_partial_flushed_volume];

	      if ((ATOMIC_INC_32 (&current_flush_volume_info->num_pages, 0) < num_pages_to_sync)
		  && (current_flush_volume_info->all_pages_written == false))
		{
		  /* Needs more data. */
		  need_wait = true;
		}
	      else
		{
		  /* New data arrived. */
		  can_flush_volume = true;
		}
	    }
	  else if (block->all_pages_written == false)
	    {
	      /* Not all pages were written and no volume available for flush yet. */
	      need_wait = true;
	    }
	  else
	    {
	      can_flush_volume = true;
	    }
	}

      if (!can_flush_volume)
	{
	  /* Can't flush a volume. Not enough data available or nothing to flush. */
	  if (need_wait == true)
	    {
	      /* Wait for new data. */
#if defined (SERVER_MODE)
	      thread_sleep (1);
#endif
	      /* Flush the new arrived data, if is the case. */
	      can_flush_volume = true;
	    }
	}
    }
  while (can_flush_volume == true);

#if !defined (NDEBUG)
  if (count_flush_volumes_info != 0)
    {
      assert (count_flush_volumes_info == block->count_flush_volumes_info);

      for (i = 0; i < count_flush_volumes_info; i++)
	{
	  current_flush_volume_info = &block->flush_volumes_info[i];

	  assert ((current_flush_volume_info->all_pages_written == true)
		  && (current_flush_volume_info->flushed_status != VOLUME_NOT_FLUSHED));
	}
    }
#endif

  /* Be sure that the helper flush block was not changed by other thread. */
  assert (block == dwb_Global.file_sync_helper_block);
  (void) ATOMIC_TAS_ADDR (&dwb_Global.file_sync_helper_block, (DWB_BLOCK *) NULL);

  PERF_UTIME_TRACKER_TIME (thread_p, &time_track, PSTAT_DWB_FILE_SYNC_HELPER_TIME_COUNTERS);

  return NO_ERROR;
}

/*
 * dwb_read_page () - Reads page from DWB.
 *
 * return   : Error code.
 * thread_p (in): The thread entry.
 * vpid(in): The page identifier.
 * io_page(out): In-memory address where the content of the page will be copied.
 * success(out): True, if found and read from DWB.
 */
int
dwb_read_page (THREAD_ENTRY * thread_p, const VPID * vpid, void *io_page, bool * success)
{
  DWB_SLOTS_HASH_ENTRY *slots_hash_entry = NULL;
  int error_code = NO_ERROR;

  assert (vpid != NULL && io_page != NULL && success != NULL);

  *success = false;

  if (!dwb_is_created ())
    {
      return NO_ERROR;
    }

  VPID key_vpid = *vpid;
  slots_hash_entry = dwb_Global.slots_hashmap.find (thread_p, key_vpid);
  if (slots_hash_entry != NULL)
    {
      assert (slots_hash_entry->slot->io_page != NULL);

      /* Check whether the slot data changed meanwhile. */
      if (VPID_EQ (&slots_hash_entry->slot->vpid, vpid))
	{
	  memcpy ((char *) io_page, (char *) slots_hash_entry->slot->io_page, IO_PAGESIZE);

	  /* Be sure that no other transaction has modified slot data meanwhile. */
	  assert (slots_hash_entry->slot->io_page->prv.pageid == vpid->pageid
		  && slots_hash_entry->slot->io_page->prv.volid == vpid->volid);

	  *success = true;
	}

      pthread_mutex_unlock (&slots_hash_entry->mutex);
    }

  return NO_ERROR;
}

// *INDENT-OFF*
#if defined(SERVER_MODE)
// class dwb_flush_block_daemon_task
//
//  description:
//    dwb flush block daemon task
//
class dwb_flush_block_daemon_task: public cubthread::entry_task
{
  private:
    PERF_UTIME_TRACKER m_perf_track;

  public:
    dwb_flush_block_daemon_task ()
    {
      PERF_UTIME_TRACKER_START (NULL, &m_perf_track);
    }

    void execute (cubthread::entry &thread_ref) override
    {
      if (!BO_IS_SERVER_RESTARTED ())
        {
	  // wait for boot to finish
	  return;
        }

      /* performance tracking */
      PERF_UTIME_TRACKER_TIME (NULL, &m_perf_track, PSTAT_DWB_FLUSH_BLOCK_COND_WAIT);

      /* flush pages as long as necessary */
      if (prm_get_bool_value (PRM_ID_ENABLE_DWB_FLUSH_THREAD) == true)
        {
	  if (dwb_flush_next_block (&thread_ref) != NO_ERROR)
	    {
	      assert_release (false);
	    }
        }

      PERF_UTIME_TRACKER_START (&thread_ref, &m_perf_track);
    }
};

// class dwb_file_sync_helper_daemon_task
//
//  description:
//    dwb file sync helper daemon task
//
void
dwb_file_sync_helper_execute (cubthread::entry &thread_ref)
{
  if (!BO_IS_SERVER_RESTARTED ())
    {
      // wait for boot to finish
      return;
    }

  /* flush pages as long as necessary */
  if (prm_get_bool_value (PRM_ID_ENABLE_DWB_FLUSH_THREAD) == true)
    {
      dwb_file_sync_helper (&thread_ref);
    }
}

/*
 * dwb_flush_block_daemon_init () - initialize DWB flush block daemon thread
 */
void
dwb_flush_block_daemon_init ()
{
  cubthread::looper looper = cubthread::looper (std::chrono::milliseconds (1));
  dwb_flush_block_daemon_task *daemon_task = new dwb_flush_block_daemon_task ();

  dwb_flush_block_daemon = cubthread::get_manager ()->create_daemon (looper, daemon_task);
}

/*
 * dwb_file_sync_helper_daemon_init () - initialize DWB file sync helper daemon thread
 */
void
dwb_file_sync_helper_daemon_init ()
{
  cubthread::looper looper = cubthread::looper (std::chrono::milliseconds (10));
  cubthread::entry_callable_task *daemon_task = new cubthread::entry_callable_task (dwb_file_sync_helper_execute);

  dwb_file_sync_helper_daemon = cubthread::get_manager ()->create_daemon (looper, daemon_task);
}

/*
 * dwb_daemons_init () - initialize DWB daemon threads
 */
void
dwb_daemons_init ()
{
  dwb_flush_block_daemon_init ();
  dwb_file_sync_helper_daemon_init ();
}

/*
 * dwb_daemons_destroy () - destroy DWB daemon threads
 */
void
dwb_daemons_destroy ()
{
  cubthread::get_manager ()->destroy_daemon (dwb_flush_block_daemon);
  cubthread::get_manager ()->destroy_daemon (dwb_file_sync_helper_daemon);
}
#endif /* SERVER_MODE */
// *INDENT-ON*

/*
 * dwb_is_flush_block_daemon_available () - Check if flush block daemon is available
 * return: true if flush block daemon is available, false otherwise
 */
static bool
dwb_is_flush_block_daemon_available (void)
{
#if defined (SERVER_MODE)
  return prm_get_bool_value (PRM_ID_ENABLE_DWB_FLUSH_THREAD) == true && dwb_flush_block_daemon != NULL;
#else
  return false;
#endif
}

/*
 * dwb_is_file_sync_helper_daemon_available () - Check if file sync helper daemon is available
 * return: true if file sync helper daemon is available, false otherwise
 */
static bool
dwb_is_file_sync_helper_daemon_available (void)
{
#if defined (SERVER_MODE)
  return prm_get_bool_value (PRM_ID_ENABLE_DWB_FLUSH_THREAD) == true && dwb_file_sync_helper_daemon != NULL;
#else
  return false;
#endif
}

/*
 * dwb_flush_block_daemon_is_running () - Check whether flush block daemon is running
 *
 *   return: true, if flush block thread is running
 */
static bool
dwb_flush_block_daemon_is_running (void)
{
#if defined (SERVER_MODE)
  return (prm_get_bool_value (PRM_ID_ENABLE_DWB_FLUSH_THREAD) == true && (dwb_flush_block_daemon != NULL)
	  && (dwb_flush_block_daemon->is_running ()));
#else
  return false;
#endif /* SERVER_MODE */
}

/*
 * dwb_file_sync_helper_daemon_is_running () - Check whether file sync helper daemon is running
 *
 *   return: true, if file sync helper thread is running
 */
static bool
dwb_file_sync_helper_daemon_is_running (void)
{
#if defined (SERVER_MODE)
  return (prm_get_bool_value (PRM_ID_ENABLE_DWB_FLUSH_THREAD) == true && (dwb_file_sync_helper_daemon != NULL)
	  && (dwb_file_sync_helper_daemon->is_running ()));
#else
  return false;
#endif /* SERVER_MODE */
}
