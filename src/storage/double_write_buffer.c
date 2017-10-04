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
 * dwb.c - double write buffer
 */

#ident "$Id$"

#include <assert.h>
#include <math.h>

#include "double_write_buffer.h"
#include "thread.h"
#include "log_impl.h"
#include "boot_sr.h"


#define DWB_SLOTS_HASH_SIZE		    1000
#define DWB_SLOTS_FREE_LIST_SIZE		    100

#define DWB_MIN_SIZE			    (512 * 1024)
#define DWB_MAX_SIZE			    (4 * 1024 * 1024)
#define DWB_MIN_BLOCKS			    1
#define DWB_MAX_BLOCKS			    8
#define DWB_CHECKSUM_ELEMENT_NO_BITS	    64
#define DWB_CHECKSUM_ELEMENT_LOG2_NO_BITS	    6
#define DWB_CHECKSUM_ELEMENT_ALL_BITS	    0xffffffffffffffff
#define DWB_CHECKSUM_ELEMENT_NO_FROM_SLOT_POS(bit_pos) (bit_pos >> DWB_CHECKSUM_ELEMENT_LOG2_NO_BITS)
#define DWB_CHECKSUM_ELEMENT_BIT_FROM_SLOT_POS(bit_pos) (bit_pos & (DWB_CHECKSUM_ELEMENT_NO_BITS - 1))

/* The total number of blocks. The blocks are numbered from 0 to DWB_NUM_TOTAL_BLOCKS - 1 */
#define DWB_NUM_TOTAL_BLOCKS	   (pgbuf_Double_Write.num_blocks)
/* The total number of pages. */
#define DWB_NUM_TOTAL_PAGES	   (pgbuf_Double_Write.num_pages)
/* The number of pages/block - blocks have the same number of pages. */
#define DWB_BLOCK_NUM_PAGES	   (pgbuf_Double_Write.num_block_pages)
/* LOG2 from total number of blocks. */
#define DWB_LOG2_BLOCK_NUM_PAGES	   (pgbuf_Double_Write.log2_num_block_pages)
/* The number of checksum elements in each block  */
#define DWB_CHECKSUM_NUM_ELEMENTS_IN_BLOCK   (pgbuf_Double_Write.checksum_info->num_checksum_elements_in_block)

/* position mask - least significant 32 bits*/
#define DWB_POSITION_MASK		    0x00000000ffffffff
/* flags mask - most significant 8 bits */
#define DWB_BLOCKS_STATUS_MASK	    0xff00000000000000
/* structure change mask - bit 55 */
#define DWB_MODIFY_STRUCTURE	    0x0080000000000000
/* created mask - bit 55 */
#define DWB_CREATE		    0x0040000000000000
/* structure change mask - bit 55 */
#define DWB_CREATE_OR_MODIFY_MASK	    (DWB_CREATE | DWB_MODIFY_STRUCTURE)
/* flag mask */
#define DWB_FLAG_MASK		    (DWB_BLOCKS_STATUS_MASK | DWB_MODIFY_STRUCTURE	\
  | DWB_CREATE)

/* Get DWB position. */
#define DWB_GET_POSITION(position_with_flags) \
  ((position_with_flags) & DWB_POSITION_MASK)

/* Reset position. */
#define DWB_RESET_POSITION(position_with_flags) \
  ((position_with_flags) & DWB_FLAG_MASK)

/* Get DWB block status. */
#define DWB_GET_BLOCK_STATUS(position_with_flags) \
  ((position_with_flags) & DWB_BLOCKS_STATUS_MASK)

/* Get block number from DWB position. */
#define DWB_GET_BLOCK_NO_FROM_POSITION(position_with_flags) \
  ((unsigned int)DWB_GET_POSITION (position_with_flags) >>  (DWB_LOG2_BLOCK_NUM_PAGES))

/* Check whether the write in specified DWB block was started. */
#define DWB_IS_BLOCK_WRITE_STARTED(position_with_flags, block_no) \
  (((position_with_flags) & (1ULL << (63 - (block_no)))) != 0)

/* Start DWB block writing. */
#define DWB_STARTS_BLOCK_WRITING(position_with_flags, block_no) \
  (assert (block_no < DWB_MAX_BLOCKS), (position_with_flags) | (1ULL << (63 - (block_no))))

/* Ends DWB block writing. */
#define DWB_ENDS_BLOCK_WRITING(position_with_flags, block_no) \
  (assert (DWB_IS_BLOCK_WRITE_STARTED (position_with_flags, block_no)), \
  (position_with_flags) & ~(1ULL << (63 - (block_no))))

/* Check whether the write in specified DWB block was ending. */
#define DWB_IS_BLOCK_WRITE_ENDED(position_with_flags, block_no) \
  (((position_with_flags) & (1ULL << (63 - (block_no)))) == 0)

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
  (((position_with_flags) & DWB_CREATE_OR_MODIFY_MASK) == DWB_MODIFY_STRUCTURE)

/* Get next DWB position with flags */
#define DWB_GET_NEXT_POSITION_WITH_FLAGS(position_with_flags) \
  ((DWB_GET_POSITION (position_with_flags)) == (DWB_NUM_TOTAL_PAGES - 1) \
  ? ((position_with_flags) & DWB_FLAG_MASK) : ((position_with_flags) + 1))

/* Get position in DWB block from global DWB position with flags. */
#define DWB_GET_POSITION_IN_BLOCK(position_with_flags) \
  ((DWB_GET_POSITION (position_with_flags)) & (DWB_BLOCK_NUM_PAGES - 1))

/* Get DWB previous block position. */
#define DWB_GET_PREV_BLOCK_NO(block_no) \
  ((block_no) > 0 ? ((block_no) - 1) : (DWB_NUM_TOTAL_BLOCKS - 1))

typedef struct pgbuf_double_write_wait_queue_entry DWB_WAIT_QUEUE_ENTRY;
struct pgbuf_double_write_wait_queue_entry
{
  void *data;			/* queue entry data */
  DWB_WAIT_QUEUE_ENTRY *next;	/* next queue entry */
};

typedef enum
{
  PGBUF_SLOT_CHECKSUM_NOT_COMPUTED,
  PGBUF_SLOT_CHECKSUM_COMPUTED
} PGBUF_SLOT_CHECKSUM_STATUS;

/* Double write queue  */
typedef struct pgbuf_double_write_wait_queue DWB_WAIT_QUEUE;
struct pgbuf_double_write_wait_queue
{
  DWB_WAIT_QUEUE_ENTRY *head;	/* queue head */
  DWB_WAIT_QUEUE_ENTRY *tail;	/* queue tail */
  DWB_WAIT_QUEUE_ENTRY *free_list;	/* queue free list */

  int count;			/* count */
  int free_count;		/* free count */
};
#define DWB_WAIT_QUEUE_INITIALIZER	{NULL, NULL, NULL, 0, 0}

typedef struct dwb_checksum_info DWB_CHECKSUM_INFO;
struct dwb_checksum_info
{
  volatile UINT64 *slot_data_checksum_requests;	/* checksum requests for slot data - 1 bit for corresponding slot */
  volatile UINT64 *slot_data_checksum_computed;	/* computed checksums for slot data - 1 bit for corresponding slot */
  volatile int *first_diff_bit_positions;	/* first bit position that is in requests but not in computed */
  unsigned int length;		/* the length of checksum bits arrays */
  UINT64 *all_block_slots_data_checksum_computed;	/* mask for block having checksum computed for all its slots data */
  unsigned int num_checksum_elements_in_block;	/* the number of checksum elements in each block */
};

/* a block in double write buffer */
typedef struct dwb_block DWB_BLOCK;
struct dwb_block
{
  pthread_mutex_t mutex;	/* mutex to protect the queue */
  DWB_WAIT_QUEUE wait_queue;	/* wait queue for the current block */

  char *write_buffer;		/* the block write buffer - contiguous area where the pages are written */
  DWB_SLOT *slots;		/* the slots */
  volatile unsigned int count_wb_pages;	/* count the pages added to write buffer */

  unsigned int block_no;	/* the block number */
  volatile UINT64 version;	/* block version TODO - global blocks version ??? */
};

/* Hash to store all pages in double write buffer */
typedef struct dwb_slots_hash_entry DWB_SLOTS_HASH_ENTRY;
struct dwb_slots_hash_entry
{
  VPID vpid;			/* page VPID */

  DWB_SLOTS_HASH_ENTRY *stack;	/* used in freelist */
  DWB_SLOTS_HASH_ENTRY *next;	/* used in hash table */
  pthread_mutex_t mutex;	/* state mutex */
  UINT64 del_id;		/* delete transaction ID (for lock free) */

  DWB_SLOT *slot;		/* DWB slot containing page */
};

typedef struct dwb_slots_hash DWB_SLOTS_HASH;
struct dwb_slots_hash
{
  LF_HASH_TABLE ht;		/* hash having VPID as key and DWB_SLOT as data */
  LF_FREELIST freelist;		/* used by hash */
};

/* The double write buffer type */
typedef struct pgbuf_double_write_buffer PGBUF_DWB;
struct pgbuf_double_write_buffer
{
  DWB_BLOCK *blocks;		/* the double write blocks */
  unsigned int num_blocks;	/* the total number of blocks in double write buffer, power of 2 */
  unsigned int num_pages;	/* the total number of pages in double write buffer, power of 2 */
  unsigned int num_block_pages;	/* the number of pages in a block, power of 2 */
  unsigned int log2_num_block_pages;	/* log2 from block number of pages */
  unsigned int blocks_flush_counter;	/* blocks flush counter */

  DWB_CHECKSUM_INFO *checksum_info;	/* checksum info */

  pthread_mutex_t mutex;	/* mutex to protect the wait queue */
  DWB_WAIT_QUEUE wait_queue;	/* wait queue, used when the PGBUF_DWB structure changed */
  UINT64 volatile position_with_flags;	/* the current position in double write buffer, the most significant
					 * bits keep the state of each blocks in double write buffer - 1
					 * started, 0 ended
					 */

  DWB_SLOTS_HASH *slots_hash;	/* slots hash */

  int vdes;			/* volume file descriptor */
};

char dwb_volume_name[PATH_MAX];
static PGBUF_DWB pgbuf_Double_Write = { NULL, 0, 0, 0, 0, 0, NULL, PTHREAD_MUTEX_INITIALIZER,
  DWB_WAIT_QUEUE_INITIALIZER, 0, NULL, NULL_VOLDES
};

#if !defined(SERVER_MODE)
/* TODO: do we need to do this? */
#define pthread_mutex_init(a, b)
#define pthread_mutex_destroy(a)
#define pthread_mutex_lock(a)	0
#define pthread_mutex_unlock(a)
static int rv;
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
						    bool needs_wakeup) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void dwb_remove_wait_queue_entry (DWB_WAIT_QUEUE * wait_queue, pthread_mutex_t * mutex,
						void *data, int (*func) (void *)) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void dwb_destroy_wait_queue (DWB_WAIT_QUEUE * wait_queue, pthread_mutex_t * mutex,
					   int (*func) (void *)) __attribute__ ((ALWAYS_INLINE));
/* DWB functions */
STATIC_INLINE void dwb_adjust_write_buffer_values (unsigned int *p_double_write_buffer_size,
						   unsigned int *p_num_blocks) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int dwb_wait_for_block_completion (THREAD_ENTRY * thread_p, unsigned int block_no)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int dwb_signal_waiting_thread (void *data) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void dwb_signal_block_completion (THREAD_ENTRY * thread_p, DWB_BLOCK * dwb_block)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int dwb_block_create_ordered_slots (DWB_BLOCK * block, DWB_SLOT ** p_dwb_ordered_slots)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int dwb_write_block (THREAD_ENTRY * thread_p, DWB_BLOCK * block, DWB_SLOT * p_dwb_slots)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int dwb_flush_block (THREAD_ENTRY * thread_p, DWB_BLOCK * block,
				   UINT64 * current_position_with_flags) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void dwb_init_slot (DWB_SLOT * slot);
STATIC_INLINE int dwb_acquire_next_slot (THREAD_ENTRY * thread_p, bool can_wait,
					 DWB_SLOT ** dwb_slot) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void dwb_set_slot_data (DWB_SLOT * dwb_slot, FILEIO_PAGE * io_page_p) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int dwb_wait_for_strucure_modification (THREAD_ENTRY * thread_p) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void dwb_signal_structure_modificated (THREAD_ENTRY * thread_p) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int dwb_starts_structure_modification (THREAD_ENTRY * thread_p,
						     UINT64 *
						     current_position_with_structure_modification_started)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void dwb_ends_structure_modification (THREAD_ENTRY * thread_p,
						    UINT64 * current_position_with_structure_modification_started)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void dwb_destroy_internal (THREAD_ENTRY * thread_p) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void dwb_intialize_slot (DWB_SLOT * slot, FILEIO_PAGE * io_page,
				       unsigned int position_in_block, unsigned int block_no)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int dwb_create_slots_hash (THREAD_ENTRY * thread_p, DWB_SLOTS_HASH ** p_slots_hash)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void dwb_finalize_slots_hash (DWB_SLOTS_HASH * slots_hash) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void dwb_initialize_block (DWB_BLOCK * block, unsigned int block_no,
					 unsigned int count_wb_pages, char *write_buffer, DWB_SLOT * slots)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int dwb_create_blocks (THREAD_ENTRY * thread_p, unsigned int num_blocks,
				     unsigned int num_block_pages, DWB_BLOCK ** p_blocks)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void dwb_finalize_block (DWB_BLOCK * block) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void dwb_initialize_checksum_info (DWB_CHECKSUM_INFO * checksum_info,
						 UINT64 * slot_data_checksum_requests,
						 UINT64 * slot_data_checksum_computed,
						 int *first_diff_bit_positions,
						 UINT64 * all_block_slots_data_checksum_computed,
						 unsigned int checksum_length,
						 unsigned int num_checksum_elements_in_block)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int dwb_create_checksum_info (THREAD_ENTRY * thread_p, unsigned int num_blocks,
					    unsigned int num_block_pages,
					    DWB_CHECKSUM_INFO ** p_checksum_info) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void dwb_add_checksum_computation_request (THREAD_ENTRY * thread_p, unsigned int block_no,
							 unsigned int position_in_block)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void dwb_finalize_checksum_info (DWB_CHECKSUM_INFO * checksum_info) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE bool dwb_needs_speedup_checksum_computation (THREAD_ENTRY * thread_p) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int dwb_slot_compute_checksum (THREAD_ENTRY * thread_p, DWB_SLOT * slot,
					     bool mark_checksum_computed, bool * checksum_computed)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int dwb_create_internal (THREAD_ENTRY * thread_p, const char *dwb_path_p, const char *db_name_p)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void
pgbuf_find_block_with_all_checksums_computed (THREAD_ENTRY * thread_p, unsigned int *block_no)
__attribute__ ((ALWAYS_INLINE));
STATIC_INLINE bool pgbuf_block_has_all_checksums_computed (THREAD_ENTRY * thread_p, unsigned int block_no);
STATIC_INLINE void
pgbuf_find_block_with_all_checksums_requested (THREAD_ENTRY * thread_p, unsigned int *block_no)
__attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int
dwb_compute_block_checksums (THREAD_ENTRY * thread_p, DWB_BLOCK * block,
			     bool * block_slots_checksum_computed, bool * block_needs_flush)
__attribute__ ((ALWAYS_INLINE));

/* Slots hash functions. */
static void *dwb_slots_hash_entry_alloc (void);
static int dwb_slots_hash_entry_free (void *entry);
static int dwb_slots_hash_entry_init (void *entry);
static int dwb_slots_hash_key_copy (void *src, void *dest);
static int dwb_slots_hash_compare_key (void *key1, void *key2);
static unsigned int dwb_slots_hash_key (void *key, int hash_table_size);

STATIC_INLINE int dwb_slots_hash_insert (THREAD_ENTRY * thread_p, VPID * vpid, DWB_SLOT * slot)
  __attribute__ ((ALWAYS_INLINE));

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
 * dwb_init_wait_queue () - intialize wait queue
 *
 * return   : nothing
 * wait_queue (in) : wait queue
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
 * dwb_make_wait_queue_entry () - make wait queue entry
 *
 * return   : queue entry
 * wait_queue (in) : wait queue
 * data (in): data
 *
 *  Note: Currently we add only thread entry in queue entry. In future we may add several informations.
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
  * dwb_block_add_wait_queue_entry () - make add entry to wait queue
  *
  * return   : added queue entry
  * wait_queue (in) : the wait queue
  * data (in): data
  *
  *  Note: Currently we add only thread entry in queue entry. In future we may add other informations.
  *        Is user responsibility to protect against queue concurrent access.
  */
STATIC_INLINE DWB_WAIT_QUEUE_ENTRY *
dwb_block_add_wait_queue_entry (DWB_WAIT_QUEUE * wait_queue, void *data)
{
  DWB_WAIT_QUEUE_ENTRY *wait_queue_entry = NULL;

  assert (wait_queue != NULL && data != NULL);
  wait_queue_entry = dwb_make_wait_queue_entry (wait_queue, data);
  if (wait_queue_entry)
    {
      if (wait_queue->head == NULL)
	{
	  wait_queue->tail = wait_queue->head = wait_queue_entry;
	}
      else
	{
	  wait_queue->tail->next = wait_queue_entry;
	  wait_queue->tail = wait_queue_entry;
	}
    }

  return wait_queue_entry;
}

/*
  * dwb_block_disconnect_wait_queue_entry () - disconnect entry from wait queue
  *
  * return   : removed queue entry
  * wait_queue (in) : the wait queue
  * thread_p (in): thread entry
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
  * dwb_block_free_wait_queue_entry () - free wait queue entry
  *
  * return   : void
  * wait_queue (in) : the wait queue
  * wait_queue_entry (in): the wait queue entry
  * func(in): function to apply on entry
  */
STATIC_INLINE void
dwb_block_free_wait_queue_entry (DWB_WAIT_QUEUE * wait_queue,
				 DWB_WAIT_QUEUE_ENTRY * wait_queue_entry, int (*func) (void *))
{
  THREAD_ENTRY *wait_thread_p = NULL;
  if (wait_queue_entry != NULL)
    {
      if (func)
	{
	  (void) func (wait_queue_entry);
	}

      /* Reuse the entry. Do not set data field to NULL. It may be used at debugging. */
      wait_queue_entry->next = wait_queue->free_list;
      wait_queue->free_list = wait_queue_entry;
      wait_queue->free_count++;
    }
}

/*
  * dwb_remove_wait_queue_entry () - remove wait queue entry
  *
  * return   : true, if entry removed, false otherwise
  * wait_queue (in): the wait queue
  * mutex (in): mutex to protect the wait queue
  * data (in): data  
  * func(in): function to apply on each entry
  *
  *  Note: Currently we add only thread entry in queue entry. In future we may add other informations.  
  *        If the data is NULL, the first entry will be removed.
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
  if (wait_queue_entry)
    {
      dwb_block_free_wait_queue_entry (wait_queue, wait_queue_entry, func);
    }

  if (mutex != NULL)
    {
      pthread_mutex_unlock (mutex);
    }
}

/*
 * dwb_destroy_wait_queue () - destroy wait queue
 *
 * return   : nothing
 * wait_queue (in): wait queue
 * mutex(in): mutex to protect the queue
 *
 *  Note: Currently we add only thread entry in queue entry. In future we may add other informations.
 *        Is user responsibility to protect against queue concurrent access.
 *        If the data is NULL, the first entry will be removed.
 */
STATIC_INLINE void
dwb_destroy_wait_queue (DWB_WAIT_QUEUE * wait_queue, pthread_mutex_t * mutex, int (*func) (void *))
{
  assert (wait_queue != NULL);
  if (mutex != NULL)
    {
      (void) pthread_mutex_lock (mutex);
    }

  while (wait_queue->head != NULL)
    {
      dwb_remove_wait_queue_entry (wait_queue, NULL, NULL, func);
    }

  if (mutex != NULL)
    {
      pthread_mutex_unlock (mutex);
    }
}

/*
  * dwb_adjust_write_buffer_values () - find valid double write buffer values
  *
  * return   : error code
  * p_double_write_buffer_size (in/out) : double write buffer size
  * p_num_blocks (in/out): the number of blocks
  * 
  *  Note: The buffer size must be a multiple of 512 K. The number of blocks must be a power of 2.  
  */
STATIC_INLINE void
dwb_adjust_write_buffer_values (unsigned int *p_double_write_buffer_size, unsigned int *p_num_blocks)
{
  unsigned int min_size;
  unsigned int max_size;

  assert (p_double_write_buffer_size != NULL && p_num_blocks != NULL
	  && *p_double_write_buffer_size > 0 && *p_num_blocks > 0);

  min_size = DWB_MIN_SIZE;
  max_size = DWB_MAX_SIZE;
  if (*p_double_write_buffer_size < min_size)
    {
      *p_double_write_buffer_size = min_size;
    }
  else if (*p_double_write_buffer_size > min_size)
    {
      if (*p_double_write_buffer_size > max_size)
	{
	  *p_double_write_buffer_size = max_size;
	}
      else
	{
	  /* find smallest number multiple of 512 k */
	  unsigned int limit1 = min_size;
	  while (*p_double_write_buffer_size > limit1)
	    {
	      limit1 = limit1 << 1;
	    }
	  *p_double_write_buffer_size = limit1;
	}
    }

  min_size = DWB_MIN_BLOCKS;
  max_size = DWB_MAX_BLOCKS;
  assert (*p_num_blocks >= min_size);
  if (*p_num_blocks > min_size)
    {
      if (*p_num_blocks > max_size)
	{
	  *p_num_blocks = max_size;
	}
      else if (!IS_POWER_OF_2 (*p_num_blocks))
	{
	  unsigned int num_blocks = *p_num_blocks;
	  do
	    {
	      num_blocks = num_blocks && (num_blocks - 1);
	    }
	  while (!IS_POWER_OF_2 (num_blocks));
	  *p_num_blocks = num_blocks << 1;
	}
    }
}

/*
 * dwb_starts_structure_modification () - Starts structure modifications
 *
 * return   : error code
 * thread_p (in): thread entry
 * current_position_with_flags(out): current position with flags
 *
 *  Note: This function must be called before changing structure of DWB. This function waits for other threads
 *    to complete blocks flush, before changing the structure.
 */
STATIC_INLINE int
dwb_starts_structure_modification (THREAD_ENTRY * thread_p, UINT64 * current_position_with_flags)
{
  UINT64 local_current_position_with_flags, new_position_with_flags;
  unsigned int block_no;
  int error_code = NO_ERROR;
  bool force_flush = false;
  int retry_flush_iter = 0, retry_flush_max = 5;

  assert (current_position_with_flags != NULL);

start_structure_modification:
  local_current_position_with_flags = ATOMIC_INC_64 (&pgbuf_Double_Write.position_with_flags, 0ULL);
  if (DWB_IS_MODIFYING_STRUCTURE (local_current_position_with_flags))
    {
      /* Only one thread can change the structure */
      return ER_FAILED;
    }

  new_position_with_flags = DWB_STARTS_MODIFYING_STRUCTURE (local_current_position_with_flags);

  /* Start structure modifications, the threads that want to flush afterwards, have to wait. */
  if (!ATOMIC_CAS_64 (&pgbuf_Double_Write.position_with_flags,
		      local_current_position_with_flags, new_position_with_flags))
    {
      /* Someone else advanced the position, try again. */
      goto start_structure_modification;
    }

#if defined(SERVER_MODE)
check_dwb_flush_thread_is_running:
  if (thread_dwb_flush_block_with_checksum_thread_is_running ())
    {
      /* Can't modify structure while flush thread can access DWB. */
      thread_sleep (20);
      goto check_dwb_flush_thread_is_running;
    }
#endif

  local_current_position_with_flags = ATOMIC_INC_64 (&pgbuf_Double_Write.position_with_flags, 0ULL);

  /* Now, I'm the unique thread that modify DWB. Need to flush incomplete blocks. */
  for (block_no = 0; block_no < DWB_NUM_TOTAL_BLOCKS; block_no++)
    {
      if (DWB_IS_BLOCK_WRITE_STARTED (local_current_position_with_flags, block_no))
	{
	flush_block:
	  /* Flush all pages from current block */
	  error_code = dwb_flush_block (thread_p, &pgbuf_Double_Write.blocks[block_no],
					&local_current_position_with_flags);
	  if (error_code != NO_ERROR)
	    {
	      /* Something wrong happens, sleep 10 msec and try again. */
	      if (retry_flush_iter < retry_flush_max)
		{
#if defined(SERVER_MODE)
		  thread_sleep (10);
#endif
		  retry_flush_iter++;
		  goto flush_block;
		}

	      return error_code;
	    }
	}
    }

  local_current_position_with_flags = ATOMIC_INC_64 (&pgbuf_Double_Write.position_with_flags, 0ULL);
  assert (DWB_GET_BLOCK_STATUS (local_current_position_with_flags) == 0);

  *current_position_with_flags = local_current_position_with_flags;

  return NO_ERROR;
}

/*
 * dwb_ends_structure_modification () - Ends structure modifications
 *
 * return   : error code
 * thread_p (in): thread entry
 * current_position_with_flags(in): current position with flags
 *
 *  Note: This function must be called before changing structure of DWB. This function waits for other threads
 *    to complete blocks flush, before changing the structure.
 */
STATIC_INLINE void
dwb_ends_structure_modification (THREAD_ENTRY * thread_p, UINT64 current_position_with_flags)
{
  UINT64 new_position_with_flags;
  new_position_with_flags = DWB_ENDS_MODIFYING_STRUCTURE (current_position_with_flags);

  /* Ends structure modifications. */
  assert (pgbuf_Double_Write.position_with_flags == current_position_with_flags);
  ATOMIC_TAS_64 (&pgbuf_Double_Write.position_with_flags, new_position_with_flags);

  /* Signal the other threads. */
  dwb_signal_structure_modificated (thread_p);
}

/*
 * dwb_initialize_block () - initialize a block
 *
 * return   : error code
 * slot (in) : the slot to initialize
 * io_page (in) : the page
 * position_in_block(in): the position in DWB block
 * block_no(in): the block where the slot reside
 */
STATIC_INLINE void
dwb_intialize_slot (DWB_SLOT * slot, FILEIO_PAGE * io_page, unsigned int position_in_block, unsigned int block_no)
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
  slot->checksum_status = PGBUF_SLOT_CHECKSUM_NOT_COMPUTED;
}

/*
 * dwb_initialize_block () - initialize a block
 *
 * return : nothing
 * checksum_info (in) : checksum info
 * slot_data_checksum_requests (in) : bits array containing checksum requests for slot data
 * slot_data_checksum_computed (in): bits array containing checksum computed for slot data
 * first_diff_bit_positions (in): first bit position that is in requests but not in computed
 * all_block_slots_data_checksum_computed(in): bits array containing checksum computed for all block slots data
 * checksum_length(in): the length of checksum bits arrays
 * num_checksum_elements_in_block(in): the number of checksum elements in each block
 */
STATIC_INLINE void
dwb_initialize_checksum_info (DWB_CHECKSUM_INFO * checksum_info, UINT64 * slot_data_checksum_requests,
			      UINT64 * slot_data_checksum_computed, int *first_diff_bit_positions,
			      UINT64 * all_block_slots_data_checksum_computed,
			      unsigned int checksum_length, unsigned int num_checksum_elements_in_block)
{
  assert (checksum_info != NULL);
  checksum_info->slot_data_checksum_requests = slot_data_checksum_requests;
  checksum_info->slot_data_checksum_computed = slot_data_checksum_computed;
  checksum_info->first_diff_bit_positions = first_diff_bit_positions;
  checksum_info->all_block_slots_data_checksum_computed = all_block_slots_data_checksum_computed;
  checksum_info->length = checksum_length;
  checksum_info->num_checksum_elements_in_block = num_checksum_elements_in_block;
}

/*
 * dwb_finalize_block () - create blocks
 *
 * return   : error code
 * thread_p (in) : thread entry
 * num_blocks(in): the number of blocks
 * num_block_pages(in): the number of block pages
 * p_checksum_info(out): the created checksum info
 */
STATIC_INLINE int
dwb_create_checksum_info (THREAD_ENTRY * thread_p, unsigned int num_blocks, unsigned int num_block_pages,
			  DWB_CHECKSUM_INFO ** p_checksum_info)
{
  UINT64 *slot_data_checksum_requests = NULL, *slot_data_checksum_computed = NULL,
    *all_block_slots_data_checksum_computed = NULL;
  int *first_diff_bit_positions = NULL;
  unsigned int checksum_length, num_checksum_elements_in_block;
  unsigned int i, num_pages2;
  DWB_CHECKSUM_INFO *checksum_info = NULL;
  int error_code;

  checksum_info = (DWB_CHECKSUM_INFO *) malloc (sizeof (DWB_CHECKSUM_INFO));
  if (checksum_info == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (DWB_CHECKSUM_INFO));
      error_code = ER_OUT_OF_VIRTUAL_MEMORY;
      goto exit_on_error;
    }

  num_checksum_elements_in_block =
    DB_ALIGN (num_block_pages, DWB_CHECKSUM_ELEMENT_NO_BITS) / DWB_CHECKSUM_ELEMENT_NO_BITS;
  checksum_length = num_checksum_elements_in_block * num_blocks;
  slot_data_checksum_requests = (UINT64 *) malloc (checksum_length * sizeof (UINT64));
  if (slot_data_checksum_requests == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, checksum_length * sizeof (UINT64));
      error_code = ER_OUT_OF_VIRTUAL_MEMORY;
      goto exit_on_error;
    }
  memset (slot_data_checksum_requests, 0, checksum_length * sizeof (UINT64));

  slot_data_checksum_computed = (UINT64 *) malloc (checksum_length * sizeof (UINT64));
  if (slot_data_checksum_computed == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, checksum_length * sizeof (UINT64));
      error_code = ER_OUT_OF_VIRTUAL_MEMORY;
      goto exit_on_error;
    }
  memset (slot_data_checksum_computed, 0, checksum_length * sizeof (UINT64));

  first_diff_bit_positions = (int *) malloc (checksum_length * sizeof (int));
  if (first_diff_bit_positions == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, checksum_length * sizeof (int));
      error_code = ER_OUT_OF_VIRTUAL_MEMORY;
      goto exit_on_error;
    }
  memset (first_diff_bit_positions, 0, checksum_length * sizeof (int));

  all_block_slots_data_checksum_computed = (UINT64 *) malloc (num_checksum_elements_in_block * sizeof (UINT64));
  if (all_block_slots_data_checksum_computed == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      num_checksum_elements_in_block * sizeof (UINT64));
      error_code = ER_OUT_OF_VIRTUAL_MEMORY;
      goto exit_on_error;
    }
  num_pages2 = num_block_pages;
  i = 0;
  while (num_pages2 >= DWB_CHECKSUM_ELEMENT_NO_BITS)
    {
      all_block_slots_data_checksum_computed[i++] = DWB_CHECKSUM_ELEMENT_ALL_BITS;
      num_pages2 -= DWB_CHECKSUM_ELEMENT_NO_BITS;
    }
  if (num_pages2 > 0)
    {
      assert (num_pages2 < DWB_CHECKSUM_ELEMENT_NO_BITS && i < num_checksum_elements_in_block);
      all_block_slots_data_checksum_computed[i] = (1 << num_pages2) - 1;
    }

  dwb_initialize_checksum_info (checksum_info, slot_data_checksum_requests, slot_data_checksum_computed,
				first_diff_bit_positions, all_block_slots_data_checksum_computed,
				checksum_length, num_checksum_elements_in_block);
  *p_checksum_info = checksum_info;

  return NO_ERROR;

exit_on_error:
  if (all_block_slots_data_checksum_computed != NULL)
    {
      free_and_init (all_block_slots_data_checksum_computed);
    }

  if (slot_data_checksum_requests != NULL)
    {
      free_and_init (slot_data_checksum_requests);
    }

  if (slot_data_checksum_computed != NULL)
    {
      free_and_init (slot_data_checksum_computed);
    }

  return error_code;
}

/*
 * dwb_add_checksum_computation_request () - add checksum computation request
 *
 * return   : nothing
 * thread_p(in): thread entry
 * block_no(in): block number
 * position_in_block(in): the slot position in block, where to compute the checksum
 */
STATIC_INLINE void
dwb_add_checksum_computation_request (THREAD_ENTRY * thread_p, unsigned int block_no, unsigned int position_in_block)
{
  volatile UINT64 *slot_data_checksum_requests = NULL;
  unsigned int position_in_checksum_bits;

  assert (block_no < DWB_NUM_TOTAL_BLOCKS && position_in_block < DWB_BLOCK_NUM_PAGES);

  slot_data_checksum_requests = pgbuf_Double_Write.checksum_info->slot_data_checksum_requests
    + DWB_CHECKSUM_NUM_ELEMENTS_IN_BLOCK * block_no + DWB_CHECKSUM_ELEMENT_NO_FROM_SLOT_POS (position_in_block);
  position_in_checksum_bits = DWB_CHECKSUM_ELEMENT_BIT_FROM_SLOT_POS (position_in_block);
  assert ((*slot_data_checksum_requests & (1ULL << position_in_checksum_bits)) == 0);
  ATOMIC_INC_64 (slot_data_checksum_requests, 1ULL << position_in_checksum_bits);
}

/*
 * dwb_finalize_block () - finalize checksum info
 *
 * return   : nothing
 * checksum_info (in) : checksum info
 */
STATIC_INLINE void
dwb_finalize_checksum_info (DWB_CHECKSUM_INFO * checksum_info)
{
  assert (checksum_info != NULL);
  if (checksum_info->slot_data_checksum_requests != NULL)
    {
      free_and_init (checksum_info->slot_data_checksum_requests);
    }

  if (checksum_info->slot_data_checksum_computed != NULL)
    {
      free_and_init (checksum_info->slot_data_checksum_computed);
    }

  if (checksum_info->first_diff_bit_positions != NULL)
    {
      free_and_init (checksum_info->first_diff_bit_positions);
    }

  if (checksum_info->all_block_slots_data_checksum_computed)
    {
      free_and_init (checksum_info->all_block_slots_data_checksum_computed);
    }
}

/*
 * dwb_needs_speedup_checksum_computation () - Check whether checksum computation is too slow.
 *
 * return   : error code
 * thread_p (in) : thread entry
 *
 *  Note: This function checks whether checksum thread remains behind. Currently, we consider that it remains
 *    behind if at least three pages are waiting for checksum computation. This computation is relative, since 
 *    while computing, the checksum thread may advance.
 */
STATIC_INLINE bool
dwb_needs_speedup_checksum_computation (THREAD_ENTRY * thread_p)
{
#define DWB_CHECKSUM_REQUESTS_THRESHOLD 3
  volatile UINT64 *slot_data_checksum_requests = NULL;
  volatile int *first_diff_bit_positions = NULL;
  int error_code = NO_ERROR, position_in_element, position;
  UINT64 slot_data_checksum_requests_elem = NULL, bit_mask;
  unsigned int element_position, num_elements, counter;

  slot_data_checksum_requests = pgbuf_Double_Write.checksum_info->slot_data_checksum_requests;
  first_diff_bit_positions = pgbuf_Double_Write.checksum_info->first_diff_bit_positions;

  num_elements = DWB_CHECKSUM_NUM_ELEMENTS_IN_BLOCK * DWB_NUM_TOTAL_BLOCKS;

  counter = 0;
  for (element_position = 0; element_position < num_elements; element_position++)
    {
      slot_data_checksum_requests_elem = ATOMIC_INC_64 (&slot_data_checksum_requests[element_position], 0ULL);
      position_in_element = ATOMIC_INC_32 (&first_diff_bit_positions[element_position], 0);
      if (position_in_element >= DWB_CHECKSUM_ELEMENT_NO_BITS)
	{
	  continue;
	}

      bit_mask = 1ULL << position_in_element;
      for (position = position_in_element; position < DWB_CHECKSUM_ELEMENT_NO_BITS; position++)
	{
	  if ((slot_data_checksum_requests_elem & bit_mask) == 0)
	    {
	      /* Stop searching bits */
	      break;
	    }

	  counter++;
	  bit_mask = bit_mask << 1;
	}

      /* Check counter after each element. */
      if (counter >= DWB_CHECKSUM_REQUESTS_THRESHOLD)
	{
	  return true;
	}
    }

  return false;

#undef DWB_CHECKSUM_REQUESTS_THRESHOLD
}

/*
 * dwb_slot_compute_checksum () - compute checksum for slot data
 *
 * return   : error code
 * thread_p (in) : thread entry
 * slot(in/out): DWB slot
 * mark_checksum_computed(in): true, if mark computed checksum
 * checksum_computed(out): true, if checksum computed now
 */
STATIC_INLINE int
dwb_slot_compute_checksum (THREAD_ENTRY * thread_p, DWB_SLOT * slot, bool mark_checksum_computed,
			   bool * checksum_computed)
{
  int error_code = NO_ERROR;
  unsigned int element_position, bit_position;
  volatile UINT64 *slot_data_checksum_computed;

  assert (slot != NULL);
  *checksum_computed = false;
  if (!ATOMIC_CAS_32 (&slot->checksum_status, PGBUF_SLOT_CHECKSUM_NOT_COMPUTED, PGBUF_SLOT_CHECKSUM_COMPUTED))
    {
      /* Already computed */
      return NO_ERROR;
    }

  error_code = fileio_set_page_checksum (slot->io_page);
  if (error_code != NO_ERROR)
    {
      /* Restore it. */
      ATOMIC_TAS_32 (&slot->checksum_status, PGBUF_SLOT_CHECKSUM_NOT_COMPUTED);
      return error_code;
    }

  if (mark_checksum_computed)
    {
      /* TO DO - macros */
      slot_data_checksum_computed = pgbuf_Double_Write.checksum_info->slot_data_checksum_computed;
      element_position = DWB_CHECKSUM_NUM_ELEMENTS_IN_BLOCK * slot->block_no
	+ slot->position_in_block / DWB_CHECKSUM_ELEMENT_NO_BITS;
      bit_position = slot->position_in_block & (DWB_CHECKSUM_ELEMENT_NO_BITS - 1);
      /* Check that no other transaction computed the current slot checksum. */
      assert ((ATOMIC_INC_64 (&slot_data_checksum_computed[element_position], 0ULL) & (1ULL << bit_position)) == 0);
      assert ((ATOMIC_INC_64 (&pgbuf_Double_Write.checksum_info->slot_data_checksum_requests[element_position], 0ULL)
	       & (1ULL << bit_position)) == (1ULL << bit_position));
      ATOMIC_INC_64 (&pgbuf_Double_Write.checksum_info->slot_data_checksum_computed[element_position],
		     1ULL << bit_position);
    }

  *checksum_computed = true;
  return NO_ERROR;
}

/*
 * dwb_create_slots_hash () - create slots hash
 *
 * return   : error code
 * thread_p (in) : thread entry
 * p_slots_hash(out): the created hash
 */
STATIC_INLINE int
dwb_create_slots_hash (THREAD_ENTRY * thread_p, DWB_SLOTS_HASH ** p_slots_hash)
{
  DWB_SLOTS_HASH *slots_hash = NULL;
  int error_code = NO_ERROR;

  slots_hash = (DWB_SLOTS_HASH *) malloc (sizeof (DWB_SLOTS_HASH));
  if (slots_hash == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (DWB_SLOTS_HASH));
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  memset (slots_hash, 0, sizeof (DWB_SLOTS_HASH));

  /* initialize freelist */
  error_code = lf_freelist_init (&slots_hash->freelist, 1, DWB_SLOTS_FREE_LIST_SIZE, &slots_entry_Descriptor,
				 &dwb_slots_Ts);
  if (error_code != NO_ERROR)
    {
      free_and_init (slots_hash);
      return error_code;
    }

  /* initialize hash table */
  error_code = lf_hash_init (&slots_hash->ht, &slots_hash->freelist, DWB_SLOTS_HASH_SIZE, &slots_entry_Descriptor);
  if (error_code != NO_ERROR)
    {
      lf_freelist_destroy (&slots_hash->freelist);
      free_and_init (slots_hash);
      return error_code;
    }

  *p_slots_hash = slots_hash;

  return NO_ERROR;
}

/*
 * logtb_finalize_global_unique_stats_table () - Finalize slots hash
 *
 *   return: error code
 *   slots_hash(in) : slots hash
 */
STATIC_INLINE void
dwb_finalize_slots_hash (DWB_SLOTS_HASH * slots_hash)
{
  assert (slots_hash != NULL);

  lf_hash_destroy (&slots_hash->ht);
  lf_freelist_destroy (&slots_hash->freelist);
}

/*
 * dwb_initialize_block () - initialize a block
 *
 * return   : nothing
 * block (in) : double write buffer volume block
 * block_no (in) : database name
 * count_wb_pages (in): count pages
 * write_buffer(in): the write buffer
 * slots(in): the slots
 */
STATIC_INLINE void
dwb_initialize_block (DWB_BLOCK * block, unsigned int block_no, unsigned int count_wb_pages,
		      char *write_buffer, DWB_SLOT * slots)
{
  assert (block != NULL);

  pthread_mutex_init (&block->mutex, NULL);
  dwb_init_wait_queue (&block->wait_queue);

  block->write_buffer = write_buffer;
  block->slots = slots;
  block->count_wb_pages = count_wb_pages;
  block->block_no = block_no;
  block->version = 0;
}

/*
 * dwb_create_blocks () - create blocks
 *
 * return   : error code
 * thread_p (in) : thread entry
 * num_blocks(in): the number of blocks
 * num_block_pages(in): the number of block pages
 * p_blocks(out): the created blocks
 */
STATIC_INLINE int
dwb_create_blocks (THREAD_ENTRY * thread_p, unsigned int num_blocks, unsigned int num_block_pages,
		   DWB_BLOCK ** p_blocks)
{
  DWB_BLOCK *blocks = NULL;
  char *blocks_write_buffer[DWB_MAX_BLOCKS];
  DWB_SLOT *slots[DWB_MAX_BLOCKS], *slot = NULL;
  unsigned int block_buffer_size, i, j;
  int error_code;

  *p_blocks = NULL;

  blocks = (DWB_BLOCK *) malloc (num_blocks * sizeof (DWB_BLOCK));
  if (blocks == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, num_blocks * sizeof (DWB_BLOCK));
      error_code = ER_OUT_OF_VIRTUAL_MEMORY;
      goto exit_on_error;
    }
  memset (blocks, 0, num_blocks * sizeof (blocks));

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
      /* No need to initialize FILEIO_PAGE header here, since is overwritten before flushing */
      for (j = 0; j < num_block_pages; j++)
	{
	  dwb_intialize_slot (&slots[i][j], (FILEIO_PAGE *) (blocks_write_buffer[i] + j * IO_PAGESIZE), j, i);
	}

      dwb_initialize_block (&blocks[i], i, 0, blocks_write_buffer[i], slots[i]);
    }

  *p_blocks = blocks;

  return NO_ERROR;

exit_on_error:
  if (slots != NULL)
    {
      for (i = 0; i < DWB_MAX_BLOCKS; i++)
	{
	  if (slots[i] != NULL)
	    {
	      free_and_init (slots[i]);
	    }
	}
    }

  if (blocks_write_buffer)
    {
      for (i = 0; i < DWB_MAX_BLOCKS; i++)
	{
	  if (blocks_write_buffer[i] != NULL)
	    {
	      free_and_init (blocks_write_buffer[i]);
	    }
	}
    }

  if (blocks != NULL)
    {
      free_and_init (blocks);
    }

  return error_code;
}

/*
 * dwb_finalize_block () - finalize block
 *
 * return   : nothing
 * block (in) : double write buffer volume block
 */
STATIC_INLINE void
dwb_finalize_block (DWB_BLOCK * block)
{
  if (block->slots != NULL)
    {
      free_and_init (block->slots);
    }
  /* destroy block write buffer */
  if (block->write_buffer)
    {
      free_and_init (block->write_buffer);
    }
  dwb_destroy_wait_queue (&block->wait_queue, &block->mutex, dwb_signal_waiting_thread);
  pthread_mutex_destroy (&block->mutex);
}

/*
 * dwb_create_internal () - create double write buffer
 *
 * return   : error code
 * thread_p (in): thread entry
 * dwb_path_p (in) : double write buffer volume block
 * db_name_p (in) : database name
 */
STATIC_INLINE int
dwb_create_internal (THREAD_ENTRY * thread_p, const char *dwb_path_p, const char *db_name_p)
{
  int error_code = NO_ERROR;
  unsigned double_write_buffer_size, num_blocks = 0;
  VPID *vpids = NULL;
  unsigned i, num_pages, num_block_pages;
  int vdes = -1;
  DWB_BLOCK *blocks = NULL;
  DWB_CHECKSUM_INFO *checksum_info = NULL;
  DWB_SLOTS_HASH *slots_hash = NULL;

  double_write_buffer_size = prm_get_integer_value (PRM_ID_DWB_SIZE);
  num_blocks = prm_get_integer_value (PRM_ID_DWB_BLOCKS);
  if (double_write_buffer_size == 0 || num_blocks == 0)
    {
      /* Do not use double write buffer. */
      return NO_ERROR;
    }

  dwb_adjust_write_buffer_values (&double_write_buffer_size, &num_blocks);
  num_pages = double_write_buffer_size / IO_PAGESIZE;
  num_block_pages = num_pages / num_blocks;
  assert (IS_POWER_OF_2 (num_blocks));
  assert (IS_POWER_OF_2 (num_pages));
  assert (IS_POWER_OF_2 (num_block_pages));
  assert (num_blocks <= DWB_MAX_BLOCKS);

  /* Create and open DWB volume first */
  fileio_make_dwb_name (dwb_volume_name, dwb_path_p, db_name_p);

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
  if (error_code != NULL)
    {
      goto exit_on_error;
    }

  error_code = dwb_create_checksum_info (thread_p, num_blocks, num_block_pages, &checksum_info);
  if (error_code != NO_ERROR)
    {
      goto exit_on_error;
    }

  error_code = dwb_create_slots_hash (thread_p, &slots_hash);
  if (error_code != NO_ERROR)
    {
      goto exit_on_error;
    }

  pgbuf_Double_Write.blocks = blocks;
  pgbuf_Double_Write.num_blocks = num_blocks;
  pgbuf_Double_Write.num_pages = num_pages;
  pgbuf_Double_Write.num_block_pages = num_block_pages;
  pgbuf_Double_Write.log2_num_block_pages = (unsigned int) (log ((float) num_block_pages) / log ((float) 2));
  pgbuf_Double_Write.blocks_flush_counter = 0;
  pgbuf_Double_Write.checksum_info = checksum_info;
  pthread_mutex_init (&pgbuf_Double_Write.mutex, NULL);
  dwb_init_wait_queue (&pgbuf_Double_Write.wait_queue);
  pgbuf_Double_Write.slots_hash = slots_hash;
  pgbuf_Double_Write.vdes = vdes;

  /* Do not set position_with_flags here. */

  return NO_ERROR;

exit_on_error:
  if (vdes != -1)
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

  if (checksum_info != NULL)
    {
      dwb_finalize_checksum_info (checksum_info);
      free_and_init (checksum_info);
    }

  if (slots_hash)
    {
      dwb_finalize_slots_hash (slots_hash);
      free_and_init (slots_hash);
    }

  return error_code;
}

/*
 * dwb_slots_hash_entry_alloc () - allocate a new entry in slots hash
 *
 *   returns: new pointer or NULL on error
 */
static void *
dwb_slots_hash_entry_alloc (void)
{
  DWB_SLOTS_HASH_ENTRY *slots_hash_entry;
  slots_hash_entry = (DWB_SLOTS_HASH_ENTRY *) malloc (sizeof (DWB_SLOTS_HASH_ENTRY));
  if (slots_hash_entry == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (GLOBAL_UNIQUE_STATS));
      return NULL;
    }
  pthread_mutex_init (&slots_hash_entry->mutex, NULL);

  return (void *) slots_hash_entry;
}

/*
 * dwb_slots_hash_entry_free () - free an entry in slots hash
 *   returns: error code or NO_ERROR
 *   entry(in): entry to free
 */
static int
dwb_slots_hash_entry_free (void *entry)
{
  DWB_SLOTS_HASH_ENTRY *slots_hash_entry = (DWB_SLOTS_HASH_ENTRY *) entry;
  if (entry != NULL)
    {
      pthread_mutex_destroy (&slots_hash_entry->mutex);
      free (entry);
      return NO_ERROR;
    }

  return ER_FAILED;
}

/*
 * dwb_slots_hash_entry_init () - initialize slots hash entry
 *   returns: error code or NO_ERROR
 *   entry(in): entry to initialize
 */
static int
dwb_slots_hash_entry_init (void *entry)
{
  DWB_SLOTS_HASH_ENTRY *p_entry = (DWB_SLOTS_HASH_ENTRY *) entry;
  if (p_entry != NULL)
    {
      VPID_SET_NULL (&p_entry->vpid);
      p_entry->slot = NULL;
      return NO_ERROR;
    }

  return ER_FAILED;
}


/*
 * logtb_global_unique_stat_key_copy () - copy a slots hash key
 *   returns: error code or NO_ERROR
 *   src(in): source
 *   dest(in): destination
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
 * btree_compare_btids () - compare a slots hash keys
 *
 * return	  : 0 if equal, -1 otherwise
 *
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
 * dwb_slots_hash_insert () - insert entry in slots hash
 *
 * return   : error code
 * thread_p (in): thread entry
 * vpid(in): page identifier
 * slot(in): DWB slot
 */
STATIC_INLINE int
dwb_slots_hash_insert (THREAD_ENTRY * thread_p, VPID * vpid, DWB_SLOT * slot)
{
  int error_code = NO_ERROR;
  DWB_SLOTS_HASH_ENTRY *slots_hash_entry = NULL;
  LF_TRAN_ENTRY *t_entry = thread_get_tran_entry (thread_p, THREAD_TS_DWB_SLOTS);
  int inserted, success;

  assert (vpid != NULL && slot != NULL);

start_slot_hash_insert:
  error_code =
    lf_hash_find_or_insert (t_entry, &pgbuf_Double_Write.slots_hash->ht, vpid, (void **) &slots_hash_entry, &inserted);
  if (error_code != NO_ERROR || slots_hash_entry == NULL)
    {
      assert_release (false);
      return error_code;
    }
  if (!inserted)
    {
      if ((slots_hash_entry->slot == NULL) || (LSA_GE (&slot->lsa, &slots_hash_entry->slot->lsa)))
	{
	  /* Found an older slot, worse than mine - remove it. */
	  if (lf_hash_delete_already_locked (t_entry, &pgbuf_Double_Write.slots_hash->ht, vpid,
					     slots_hash_entry, &success) != NO_ERROR || !success)
	    {
	      /* Should not happen. */
	      pthread_mutex_unlock (&slots_hash_entry->mutex);
	      assert_release (false);
	      return ER_FAILED;
	    }

	  goto start_slot_hash_insert;
	}
      else
	{
	  /* TO DO - er_log_debug */

	  /* The older slot is better than mine - leave it in hash. */
	  pthread_mutex_unlock (&slots_hash_entry->mutex);
	  return NO_ERROR;
	}
    }

  /* If the old entry exists, overwrite it. */
  VPID_COPY (&slots_hash_entry->vpid, vpid);
  slots_hash_entry->slot = slot;

  pthread_mutex_unlock (&slots_hash_entry->mutex);

  return NO_ERROR;
}

/*
 * dwb_destroy_internal () - destroy free double write buffer
 *
 * return   : error code
 * thread_p (in): thread entry 
 */
STATIC_INLINE void
dwb_destroy_internal (THREAD_ENTRY * thread_p)
{
  unsigned int block_no;

  dwb_destroy_wait_queue (&pgbuf_Double_Write.wait_queue, &pgbuf_Double_Write.mutex, dwb_signal_waiting_thread);
  pthread_mutex_destroy (&pgbuf_Double_Write.mutex);

  if (pgbuf_Double_Write.blocks != NULL)
    {
      for (block_no = 0; block_no < DWB_NUM_TOTAL_BLOCKS; block_no++)
	{
	  dwb_finalize_block (&pgbuf_Double_Write.blocks[block_no]);
	}
      free_and_init (pgbuf_Double_Write.blocks);
    }

  if (pgbuf_Double_Write.checksum_info)
    {
      dwb_finalize_checksum_info (pgbuf_Double_Write.checksum_info);
      free_and_init (pgbuf_Double_Write.checksum_info);
    }

  if (pgbuf_Double_Write.slots_hash)
    {
      dwb_finalize_slots_hash (pgbuf_Double_Write.slots_hash);
      free_and_init (pgbuf_Double_Write.slots_hash);
    }

  if (pgbuf_Double_Write.vdes != -1)
    {
      fileio_dismount (thread_p, pgbuf_Double_Write.vdes);
      fileio_unformat (NULL, dwb_volume_name);
    }
}

/*
 * dwb_wait_for_block_completion () - wait for double write buffer block to complete
 *
 * return   : error code
 * thread_p (in): thread entry
 * dwb_block (in): double write buffer block
 */
STATIC_INLINE int
dwb_wait_for_block_completion (THREAD_ENTRY * thread_p, unsigned int block_no)
{
  int error_code = NO_ERROR;
#if defined (SERVER_MODE)

  DWB_WAIT_QUEUE_ENTRY *double_write_queue_entry = NULL;
  DWB_BLOCK *dwb_block = NULL;

  assert (thread_p != NULL && block_no >= 0 && block_no < DWB_NUM_TOTAL_BLOCKS);
  dwb_block = &pgbuf_Double_Write.blocks[block_no];
  (void) pthread_mutex_lock (&dwb_block->mutex);

  thread_lock_entry (thread_p);

  double_write_queue_entry = dwb_block_add_wait_queue_entry (&dwb_block->wait_queue, thread_p);
  if (double_write_queue_entry)
    {
      int r;
      struct timespec to;

      pthread_mutex_unlock (&dwb_block->mutex);
      to.tv_sec = (int) time (NULL) + 20;
      to.tv_nsec = 0;

      r = thread_suspend_timeout_wakeup_and_unlock_entry (thread_p, &to, THREAD_DWB_QUEUE_SUSPENDED);
      if (r == ER_CSS_PTHREAD_COND_TIMEDOUT)
	{
	  /* timeout, remove the entry from queue */
	  dwb_remove_wait_queue_entry (&dwb_block->wait_queue, &dwb_block->mutex, thread_p, NULL);
	  return r;
	}
      else if (thread_p->resume_status != THREAD_DWB_QUEUE_RESUMED)
	{
	  /* interruption, remove the entry from queue */
	  assert (thread_p->resume_status == THREAD_RESUME_DUE_TO_INTERRUPT
		  || thread_p->resume_status == THREAD_RESUME_DUE_TO_SHUTDOWN);

	  dwb_remove_wait_queue_entry (&dwb_block->wait_queue, &dwb_block->mutex, thread_p, NULL);
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INTERRUPTED, 0);
	  return ER_FAILED;
	}
      else
	{
	  assert (thread_p->resume_status == THREAD_DWB_QUEUE_RESUMED);
	  return NO_ERROR;
	}
    }
  else
    {
      /* allocation error */
      thread_unlock_entry (thread_p);
      error_code = er_errid ();
      assert (error_code != NO_ERROR);
    }

  pthread_mutex_unlock (&dwb_block->mutex);
#endif /* SERVER_MODE */
  return error_code;
}

/*
 * dwb_signal_waiting_thread () - signal waiting thread
 *
 * return   : error code
 * data (in): queue entry containing waiting thread 
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
      assert (wait_thread_p->resume_status == THREAD_DWB_QUEUE_SUSPENDED);
      thread_wakeup_already_had_mutex (wait_thread_p, THREAD_DWB_QUEUE_RESUMED);
      thread_unlock_entry (wait_thread_p);
    }
#endif /* SERVER_MODE */

  return NO_ERROR;
}

/*
 * dwb_signal_block_completion () - signal double write buffer block completion
 *
 * return   : nothing
 * thread_p (in): thread entry
 * dwb_block (in): double write buffer block
 */
STATIC_INLINE void
dwb_signal_block_completion (THREAD_ENTRY * thread_p, DWB_BLOCK * dwb_block)
{
  DWB_WAIT_QUEUE_ENTRY *double_write_queue_entry = NULL;
  assert (dwb_block != NULL);
  if ((DWB_WAIT_QUEUE_ENTRY volatile *) (dwb_block->wait_queue.head) != NULL)
    {
      /* There are blocked threads. Destroy the wait queue and release the blocked threads. */
      dwb_destroy_wait_queue (&dwb_block->wait_queue, &dwb_block->mutex, dwb_signal_waiting_thread);
    }
}

/*
 * dwb_signal_block_completion () - signal double write buffer structure changed
 *
 * return   : nothing
 * thread_p (in): thread entry
 * dwb_block (in): double write buffer block
 */
STATIC_INLINE void
dwb_signal_structure_modificated (THREAD_ENTRY * thread_p)
{
  DWB_WAIT_QUEUE_ENTRY *double_write_queue_entry = NULL;
  if ((DWB_WAIT_QUEUE_ENTRY volatile *) (pgbuf_Double_Write.wait_queue.head) != NULL)
    {
      /* There are blocked threads. Destroy the wait queue and release the blocked threads. */
      dwb_destroy_wait_queue (&pgbuf_Double_Write.wait_queue, &pgbuf_Double_Write.mutex, dwb_signal_waiting_thread);
    }
}

/*
 * dwb_wait_for_strucure_modification () - wait for double write buffer structure modification
 *
 * return   : error code
 * thread_p (in): thread entry
 * dwb_block (in): double write buffer block
 */
STATIC_INLINE int
dwb_wait_for_strucure_modification (THREAD_ENTRY * thread_p)
{
  /* TO DO - check SA */
#if defined (SERVER_MODE)

  DWB_WAIT_QUEUE_ENTRY *double_write_queue_entry = NULL;
  UINT64 current_position_with_flags;

  (void) pthread_mutex_lock (&pgbuf_Double_Write.mutex);

  /* Check the actual flags, to avoids unnecessary waits. */
  current_position_with_flags = ATOMIC_INC_64 (&pgbuf_Double_Write.position_with_flags, 0ULL);
  if (DWB_IS_MODIFYING_STRUCTURE (current_position_with_flags))
    {
      thread_lock_entry (thread_p);

      double_write_queue_entry = dwb_block_add_wait_queue_entry (&pgbuf_Double_Write.wait_queue, thread_p);
      if (double_write_queue_entry)
	{
	  int r;
	  struct timespec to;

	  pthread_mutex_unlock (&pgbuf_Double_Write.mutex);
	  to.tv_sec = (int) time (NULL) + 10;
	  to.tv_nsec = 0;

	  r = thread_suspend_timeout_wakeup_and_unlock_entry (thread_p, &to, THREAD_DWB_QUEUE_SUSPENDED);
	  if (r == ER_CSS_PTHREAD_COND_TIMEDOUT)
	    {
	      /* timeout, remove the entry from queue */
	      dwb_remove_wait_queue_entry (&pgbuf_Double_Write.wait_queue, &pgbuf_Double_Write.mutex, thread_p, NULL);
	      return r;
	    }
	  else if (thread_p->resume_status != THREAD_DWB_QUEUE_RESUMED)
	    {
	      /* interruption, remove the entry from queue */
	      assert (thread_p->resume_status == THREAD_RESUME_DUE_TO_INTERRUPT
		      || thread_p->resume_status == THREAD_RESUME_DUE_TO_SHUTDOWN);

	      dwb_remove_wait_queue_entry (&pgbuf_Double_Write.wait_queue, &pgbuf_Double_Write.mutex, thread_p, NULL);
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INTERRUPTED, 0);
	      return ER_FAILED;
	    }
	  else
	    {
	      assert (thread_p->resume_status == THREAD_DWB_QUEUE_RESUMED);
	    }
	}
      else
	{
	  /* allocation error */
	  thread_unlock_entry (thread_p);
	  pthread_mutex_unlock (&pgbuf_Double_Write.mutex);
	  assert (er_errid () != NO_ERROR);
	  return er_errid ();
	}
    }

  pthread_mutex_unlock (&pgbuf_Double_Write.mutex);
#endif /* SERVER_MODE */
  return NO_ERROR;
}

/*
 * dwb_compare_slots () - Compare DWB slots
 *   return: arg1 - arg2
 *   arg1(in): slot 1
 *   arg2(in): slot 2
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
 * dwb_block_create_ordered_slots () - create ordered slots for a block
 *
 * return   : error code
 * thread_p (in): thread entry.
 * block_no(in): The block number that to flush.
 * p_dwb_ordered_slots(out): The ordered slots
 *
 */
STATIC_INLINE int
dwb_block_create_ordered_slots (DWB_BLOCK * block, DWB_SLOT ** p_dwb_ordered_slots)
{
  DWB_SLOT *p_local_dwb_ordered_slots = NULL;

  assert (block != NULL && p_dwb_ordered_slots != NULL);

  p_local_dwb_ordered_slots = (DWB_SLOT *) malloc ((block->count_wb_pages + 1) * sizeof (DWB_SLOT));
  if (p_local_dwb_ordered_slots == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      (block->count_wb_pages + 1) * sizeof (DWB_SLOT));
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  memcpy (p_local_dwb_ordered_slots, block->slots, block->count_wb_pages * sizeof (DWB_SLOT));
  dwb_init_slot (&p_local_dwb_ordered_slots[block->count_wb_pages]);

  /* Order pages by (VPID, LSA) */
  qsort ((void *) p_local_dwb_ordered_slots, block->count_wb_pages, sizeof (DWB_SLOT), dwb_compare_slots);

  *p_dwb_ordered_slots = p_local_dwb_ordered_slots;

  return NO_ERROR;
}


/*
 * dwb_write_block () - write pages from specified block in specified order
 *
 * return   : error code
 * thread_p (in): thread entry.
 * block(in): The block that is written.
 * p_dwb_ordered_slots(in): The slots that gives the order in which the page are flushed
 */
STATIC_INLINE int
dwb_write_block (THREAD_ENTRY * thread_p, DWB_BLOCK * block, DWB_SLOT * p_dwb_ordered_slots)
{
  INT16 volid;
  unsigned int i;
  int vol_fd;
  VPID *vpid;
  int error_code = NO_ERROR;
  LF_TRAN_ENTRY *t_entry = thread_get_tran_entry (thread_p, THREAD_TS_DWB_SLOTS);
  int success;
  DWB_SLOTS_HASH_ENTRY *slots_hash_entry = NULL;

  assert (block != NULL && p_dwb_ordered_slots != NULL);

  volid = NULL_VOLID;
  for (i = 0; i < block->count_wb_pages; i++)
    {
      vpid = &p_dwb_ordered_slots[i].vpid;
      if (VPID_ISNULL (vpid))
	{
	  continue;
	}

      assert (VPID_ISNULL (&p_dwb_ordered_slots[i + 1].vpid) || VPID_LT (vpid, &p_dwb_ordered_slots[i + 1].vpid));

      if (volid != vpid->volid)
	{
	  /* Update the current VPID and get the volume descriptor. */
	  volid = vpid->volid;
	  vol_fd = fileio_get_volume_descriptor (volid);
	}

      /* Check whether the volume was removed meanwhile. */
      if (vol_fd != NULL_VOLDES)
	{
	  /* Write the data. */
	  if (fileio_write (thread_p, vol_fd, p_dwb_ordered_slots[i].io_page, vpid->pageid, IO_PAGESIZE, true) == NULL)
	    {
	      /* Something wrong happens. */
	      return ER_FAILED;
	    }

	  /* Since the page was written, remove it from hash. */
	  if (lf_hash_find (t_entry, &pgbuf_Double_Write.slots_hash->ht, (void *) vpid, (void **) &slots_hash_entry)
	      != NO_ERROR)
	    {
	      /* Should not happen */
	      assert (false);
	      return ER_FAILED;
	    }

	  if (slots_hash_entry == NULL)
	    {
	      /* Already removed from hash by others, continue with the next slot. */
	      continue;
	    }

	  /* Check the slot. */
	  if (slots_hash_entry->slot == &(block->slots[p_dwb_ordered_slots[i].position_in_block]))
	    {
	      if (lf_hash_delete_already_locked (t_entry, &pgbuf_Double_Write.slots_hash->ht, vpid,
						 slots_hash_entry, &success) != NO_ERROR || !success)
		{
		  /* Should not happen. */
		  pthread_mutex_unlock (&slots_hash_entry->mutex);
		  assert_release (false);
		  return ER_FAILED;
		}

	    }
	  else
	    {
	      pthread_mutex_unlock (&slots_hash_entry->mutex);
	    }
	}
    }

  return NO_ERROR;
}

/*
 * dwb_flush_block () - flush pages from specified block
 *
 * return   : error code
 * thread_p (in): thread entry.
 * block(in): The block that needs flush.
 * current_position_with_flags(out): Current position with flags
 *
 *  Note: The block pages can't be modified by others during flush.
 */
STATIC_INLINE int
dwb_flush_block (THREAD_ENTRY * thread_p, DWB_BLOCK * block, UINT64 * current_position_with_flags)
{
  UINT64 local_current_position_with_flags, new_position_with_flags;
  VPID *vpid = NULL;
  int error_code = NO_ERROR;
  char *write_buffer = NULL;
  DWB_SLOT *p_dwb_ordered_slots = NULL;
  unsigned int i;
  FILEIO_PAGE *io_page = NULL;
  unsigned int block_element_position, block_start_position, element_position;

  assert (block != NULL && block->count_wb_pages > 0 && dwb_is_created (thread_p));

  /* Currently we allow only one block to be flushed. */
  ATOMIC_INC_32 (&pgbuf_Double_Write.blocks_flush_counter, 1);
  assert (pgbuf_Double_Write.blocks_flush_counter <= 1);

  /* Order slots by VPID, to flush faster. */
  error_code = dwb_block_create_ordered_slots (block, &p_dwb_ordered_slots);
  if (error_code != NO_ERROR)
    {
      error_code = ER_FAILED;
      goto end;
    }

  /* Remove duplicates */
  for (i = 0; i < block->count_wb_pages - 1; i++)
    {
      if (VPID_EQ (&p_dwb_ordered_slots[i].vpid, &p_dwb_ordered_slots[i + 1].vpid))
	{
	  /* Next slot contains the same page, but that page is newer than the current one. Invalidate the VPID to
	   * avoid flushing the page twice.
	   */
	  assert (LSA_LE (&p_dwb_ordered_slots[i].lsa, &p_dwb_ordered_slots[i + 1].lsa));
	  VPID_SET_NULL (&p_dwb_ordered_slots[i].vpid);
	  io_page = p_dwb_ordered_slots[i].io_page;
	  io_page->prv.pageid = -1;
	  io_page->prv.volid = -1;
	}
    }

  /* First, write and flush the double write file buffer. */
  if (fileio_write_pages (thread_p, pgbuf_Double_Write.vdes, block->write_buffer, 0, block->count_wb_pages,
			  IO_PAGESIZE, true) == NULL)
    {
      /* Something wrong happens. */
      error_code = ER_FAILED;
      goto end;
    }
  if (fileio_synchronize (thread_p, pgbuf_Double_Write.vdes, dwb_volume_name) != pgbuf_Double_Write.vdes)
    {
      /* Something wrong happens. */
      error_code = ER_FAILED;
      goto end;
    }

  /* Now, write and flush the original location. */
  error_code = dwb_write_block (thread_p, block, p_dwb_ordered_slots);
  if (error_code != NO_ERROR)
    {
      goto end;
    }
  fileio_synchronize_all (thread_p, false);

  /* TO DO - function to reset */
  block_start_position = DWB_CHECKSUM_NUM_ELEMENTS_IN_BLOCK * block->block_no;
  for (block_element_position = 0; block_element_position < DWB_CHECKSUM_NUM_ELEMENTS_IN_BLOCK;
       block_element_position++)
    {
      element_position = block_start_position + block_element_position;
      ATOMIC_TAS_64 (&pgbuf_Double_Write.checksum_info->slot_data_checksum_requests[element_position], 0ULL);
      ATOMIC_TAS_64 (&pgbuf_Double_Write.checksum_info->slot_data_checksum_computed[element_position], 0ULL);
      pgbuf_Double_Write.checksum_info->first_diff_bit_positions[element_position] = 0;
    }
  ATOMIC_TAS_32 (&block->count_wb_pages, 0);
  ATOMIC_INC_64 (&block->version, 1ULL);

  /* Reset block bit position, since the block was flushed. */
reset_bit_position:
  /* Get the actual position with flags. */
  local_current_position_with_flags = ATOMIC_INC_64 (&pgbuf_Double_Write.position_with_flags, 0LL);
  new_position_with_flags = DWB_ENDS_BLOCK_WRITING (local_current_position_with_flags, block->block_no);
  /* Reset the bit if possible. */
  if (!ATOMIC_CAS_64 (&pgbuf_Double_Write.position_with_flags, local_current_position_with_flags,
		      new_position_with_flags))
    {
      /* The position was changed by others, try again */
      goto reset_bit_position;
    }

  /* Release locked threads, if any. */
  dwb_signal_block_completion (thread_p, block);
  if (current_position_with_flags)
    {
      *current_position_with_flags = new_position_with_flags;
    }

end:
  ATOMIC_INC_32 (&pgbuf_Double_Write.blocks_flush_counter, -1);
  if (p_dwb_ordered_slots != NULL)
    {
      free_and_init (p_dwb_ordered_slots);
    }
  return error_code;
}

/*
 * dwb_acquire_next_slot () - acquire the next slot in DWB
 *
 * return   : error code
 * thread_p(in): thread entry
 * can_wait(in): true, if can wait to get the next slot
 * dwb_slot(out): Pointer to the next slot in DWB 
 */
STATIC_INLINE int
dwb_acquire_next_slot (THREAD_ENTRY * thread_p, bool can_wait, DWB_SLOT ** dwb_slot)
{
  UINT64 current_position_with_flags, current_position_with_block_write_started, new_position_with_flags;
  unsigned int current_block_no, position_in_current_block;
  int error_code = NO_ERROR;
  DWB_BLOCK *block;

  assert (dwb_slot != NULL);
  *dwb_slot = NULL;
start:
  /* Get the current position in double write buffer. */
  current_position_with_flags = ATOMIC_INC_64 (&pgbuf_Double_Write.position_with_flags, 0ULL);
  if (DWB_NOT_CREATED_OR_MODIFYING (current_position_with_flags))
    {
      /* Rarely happens. */
      if (!DWB_IS_CREATED (current_position_with_flags))
	{
	  /* Someone deleted the DWB */
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_DWB_DISABLED, 0);
	  return ER_DWB_DISABLED;
	}
      else if (DWB_IS_MODIFYING_STRUCTURE (current_position_with_flags))
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
    }

  current_block_no = DWB_GET_BLOCK_NO_FROM_POSITION (current_position_with_flags);
  position_in_current_block = DWB_GET_POSITION_IN_BLOCK (current_position_with_flags);
  assert (current_block_no < DWB_NUM_TOTAL_BLOCKS && position_in_current_block < DWB_BLOCK_NUM_PAGES);
  if (position_in_current_block == 0)
    {
      /* This is the first write on current block.
       * Before start writing, check whether the previous iteration finished.
       */
      if (DWB_IS_BLOCK_WRITE_STARTED (current_position_with_flags, current_block_no))
	{
	  if (can_wait == false)
	    {
	      return NO_ERROR;
	    }
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
      assert (DWB_IS_CREATED (pgbuf_Double_Write.position_with_flags));
      assert (!DWB_IS_MODIFYING_STRUCTURE (pgbuf_Double_Write.position_with_flags));

      /* Compute the next position with flags */
      new_position_with_flags = DWB_GET_NEXT_POSITION_WITH_FLAGS (current_position_with_flags);
    }

  /* Compute and advance the global position in double write buffer. */
  if (!ATOMIC_CAS_64 (&pgbuf_Double_Write.position_with_flags, current_position_with_flags, new_position_with_flags))
    {
      /* Someone else advanced the global position in double write buffer, try again. */
      goto start;
    }

  block = pgbuf_Double_Write.blocks + current_block_no;
  *dwb_slot = block->slots + position_in_current_block;
  assert ((*dwb_slot)->position_in_block == position_in_current_block);
  ATOMIC_TAS_32 (&(*dwb_slot)->checksum_status, PGBUF_SLOT_CHECKSUM_NOT_COMPUTED);

  return NO_ERROR;
}

/*
 * dwb_set_slot_data () - set DWB data at the location indicated by the slot
 *
 * return   : error code
 * dwb_slot(in): DWB slot that contains the location where the data must be set
 * io_page_p(in): The data 
 */
STATIC_INLINE void
dwb_set_slot_data (DWB_SLOT * dwb_slot, FILEIO_PAGE * io_page_p)
{
  assert (dwb_slot != NULL && io_page_p != NULL);

  if (io_page_p->prv.pageid != NULL_PAGEID)
    {
      memcpy (dwb_slot->io_page, (char *) io_page_p, IO_PAGESIZE);
    }

  VPID_SET (&dwb_slot->vpid, io_page_p->prv.volid, io_page_p->prv.pageid);
  LSA_COPY (&dwb_slot->lsa, &io_page_p->prv.lsa);
}

/*
 * dwb_init_slot () - intialize DWB slot
 *
 * return   : nothing
 * slot (in) : the DWB slot
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
 * pgbuf_find_block_with_all_checksums_computed(): find block with all cheksums computed
 *
 *   returns: nothing
 * thread_p (in): thread entry
 * block_no(out): the block number if is found, otherwise DWB_NUM_TOTAL_BLOCKS
 */
STATIC_INLINE void
pgbuf_find_block_with_all_checksums_computed (THREAD_ENTRY * thread_p, unsigned int *block_no)
{
  unsigned int block_start_position, block_element_position, element_position, start_block, end_block, num_block;
  UINT64 slot_data_checksum_computed_elem = NULL;
  bool found;

  assert (block_no != NULL);

  *block_no = DWB_NUM_TOTAL_BLOCKS;
  start_block = 0;
  end_block = pgbuf_Double_Write.num_blocks;
  /* First, search for blocks that must be flushed, to avoid delays caused by checksum computation in other block. */
  for (num_block = 0; num_block < DWB_NUM_TOTAL_BLOCKS; num_block++)
    {
      found = true;
      block_start_position = DWB_CHECKSUM_NUM_ELEMENTS_IN_BLOCK * num_block;
      for (block_element_position = 0; block_element_position < DWB_CHECKSUM_NUM_ELEMENTS_IN_BLOCK;
	   block_element_position++)
	{
	  element_position = block_start_position + block_element_position;
	  slot_data_checksum_computed_elem =
	    ATOMIC_INC_64 (&pgbuf_Double_Write.checksum_info->slot_data_checksum_computed[element_position], 0LL);
	  if (slot_data_checksum_computed_elem
	      != pgbuf_Double_Write.checksum_info->all_block_slots_data_checksum_computed[block_element_position])
	    {
	      found = false;
	      break;
	    }
	}

      if (found)
	{
	  /* Needs to flush the block, after computing checksums. */
	  *block_no = num_block;
	  break;
	}
    }
}

/*
 * pgbuf_block_has_all_checksums_computed(): check whether the block has all checksum computed
 *
 *   returns: true, if all block checksums computed
 * thread_p (in): thread entry
 * block_no(out): the block number
 */
STATIC_INLINE bool
pgbuf_block_has_all_checksums_computed (THREAD_ENTRY * thread_p, unsigned int block_no)
{
  unsigned int block_start_position, block_element_position, element_position;
  UINT64 slot_data_checksum_computed_elem = NULL;
  bool found;

  /* First, search for blocks that must be flushed, to avoid delays caused by checksum computation in other block. */

  found = true;
  block_start_position = DWB_CHECKSUM_NUM_ELEMENTS_IN_BLOCK * block_no;
  for (block_element_position = 0; block_element_position < DWB_CHECKSUM_NUM_ELEMENTS_IN_BLOCK;
       block_element_position++)
    {
      element_position = block_start_position + block_element_position;
      slot_data_checksum_computed_elem =
	ATOMIC_INC_64 (&pgbuf_Double_Write.checksum_info->slot_data_checksum_computed[element_position], 0LL);
      if (slot_data_checksum_computed_elem
	  != pgbuf_Double_Write.checksum_info->all_block_slots_data_checksum_computed[block_element_position])
	{
	  found = false;
	  break;
	}
    }

  return found;
}

/*
 * pgbuf_find_block_with_all_checksums_requested(): find block with all cheksums requested
 *
 *   returns: nothing
 * thread_p (in): thread entry
 * block_no(out): the block number if is found, otherwise DWB_NUM_TOTAL_BLOCKS
 */
STATIC_INLINE void
pgbuf_find_block_with_all_checksums_requested (THREAD_ENTRY * thread_p, unsigned int *block_no)
{
  unsigned int block_start_position, block_element_position, element_position, start_block, end_block, num_block;
  UINT64 slot_data_checksum_computed_elem = NULL, slot_data_checksum_requests_elem = NULL;
  bool found;

  assert (block_no != NULL);

  *block_no = DWB_NUM_TOTAL_BLOCKS;
  start_block = 0;
  end_block = pgbuf_Double_Write.num_blocks;
  /* First, search for blocks that must be flushed, to avoid delays caused by checksum computation in other block. */
  for (num_block = 0; num_block < DWB_NUM_TOTAL_BLOCKS; num_block++)
    {
      found = true;
      block_start_position = DWB_CHECKSUM_NUM_ELEMENTS_IN_BLOCK * num_block;
      for (block_element_position = 0; block_element_position < DWB_CHECKSUM_NUM_ELEMENTS_IN_BLOCK;
	   block_element_position++)
	{
	  element_position = block_start_position + block_element_position;
	  slot_data_checksum_requests_elem =
	    ATOMIC_INC_64 (&pgbuf_Double_Write.checksum_info->slot_data_checksum_requests[element_position], 0LL);
	  if (slot_data_checksum_requests_elem
	      != pgbuf_Double_Write.checksum_info->all_block_slots_data_checksum_computed[block_element_position])
	    {
	      found = false;
	      break;
	    }
	}

      if (found)
	{
	  /* Needs to flush the block, after computing checksums. */
	  *block_no = num_block;
	  break;
	}
    }
}

/*
 * dwb_compute_block_checksums(): compute checksum for requested slots in specified block
 *
 *   returns: error code
 * thread_p (in): thread entry
 * block(int): DWB block
 * block_slots_checksum_computed (out); true, if checksums of some slots in block are computed
 * block_needs_flush (out); true, if block needs flush - all slots checksum computed
 */
STATIC_INLINE int
dwb_compute_block_checksums (THREAD_ENTRY * thread_p, DWB_BLOCK * block,
			     bool * block_slots_checksum_computed, bool * block_needs_flush)
{
  volatile UINT64 *slot_data_checksum_computed = NULL, *slot_data_checksum_requests = NULL;
  UINT64 slot_data_checksum_computed_elem = NULL, slot_data_checksum_requests_elem = NULL, bit_mask,
    computed_checksum_bits;
  volatile int *first_diff_bit_positions = NULL;
  int error_code = NO_ERROR, position_in_element, position;
  unsigned int block_start_position, block_element_position, element_position, slot_position, slot_base;
  bool checksum_computed, slots_checksum_computed, all_slots_checksum_computed;

  assert (block != NULL);

  slot_data_checksum_computed = pgbuf_Double_Write.checksum_info->slot_data_checksum_computed;
  slot_data_checksum_requests = pgbuf_Double_Write.checksum_info->slot_data_checksum_requests;
  first_diff_bit_positions = pgbuf_Double_Write.checksum_info->first_diff_bit_positions;

  slots_checksum_computed = false;
  all_slots_checksum_computed = true;
  /* TO DO - macros */
  block_start_position = DWB_CHECKSUM_NUM_ELEMENTS_IN_BLOCK * block->block_no;
  for (block_element_position = 0; block_element_position < DWB_CHECKSUM_NUM_ELEMENTS_IN_BLOCK;
       block_element_position++)
    {
      element_position = block_start_position + block_element_position;
      while (true)
	{
	  slot_data_checksum_requests_elem = ATOMIC_INC_64 (&slot_data_checksum_requests[element_position], 0ULL);
	  slot_data_checksum_computed_elem = ATOMIC_INC_64 (&slot_data_checksum_computed[element_position], 0ULL);

	  if (slot_data_checksum_requests_elem == 0ULL
	      || slot_data_checksum_requests_elem == slot_data_checksum_computed_elem)
	    {
	      /* There are no other slots available for checksum computation. */
	      break;
	    }

	  /* The checksum bits modified meanwhile, we needs to compute new checksum */
	  position_in_element = ATOMIC_INC_32 (&first_diff_bit_positions[element_position], 0);
	  if (position_in_element >= DWB_CHECKSUM_ELEMENT_NO_BITS)
	    {
	      break;
	    }

	  slot_base = block_element_position * DWB_CHECKSUM_ELEMENT_NO_BITS;
	  bit_mask = 1ULL << position_in_element;
	  computed_checksum_bits = 0;
	  for (position = position_in_element; position < DWB_CHECKSUM_ELEMENT_NO_BITS; position++)
	    {
	      if ((slot_data_checksum_requests_elem & bit_mask) == 0)
		{
		  /* Stop searching bits */
		  break;
		}

	      slot_position = slot_base + position;
	      assert (slot_position < DWB_BLOCK_NUM_PAGES);
	      error_code = dwb_slot_compute_checksum (thread_p, &block->slots[slot_position], false,
						      &checksum_computed);
	      if (error_code != NO_ERROR)
		{
		  return error_code;
		}

	      /* Add the bit, if checksum was computed by me. */
	      if (checksum_computed)
		{
		  computed_checksum_bits |= bit_mask;
		  slots_checksum_computed = true;
		}

	      bit_mask = bit_mask << 1;
	    }

	  /* Update computed bits */
	  if (computed_checksum_bits == 0)
	    {
	      break;
	    }
	  else
	    {
	      /* Check that no other transaction computed the current slot checksum. */
	      assert ((ATOMIC_INC_64 (&slot_data_checksum_computed[element_position], 0ULL)
		       & computed_checksum_bits) == 0);
	      assert ((ATOMIC_INC_64 (&slot_data_checksum_requests[element_position], 0ULL)
		       & computed_checksum_bits) == computed_checksum_bits);
	      ATOMIC_INC_64 (&slot_data_checksum_computed[element_position], computed_checksum_bits);
	    }

	  /* Update start bit position, if possible */
	  do
	    {
	      position_in_element = ATOMIC_INC_32 (&first_diff_bit_positions[element_position], 0);
	      if (position_in_element >= position)
		{
		  /* Other transaction advanced before me, nothing to do. */
		  break;
		}
	    }
	  while (!ATOMIC_CAS_32 (&first_diff_bit_positions[element_position], position_in_element, position));
	}

      if (slot_data_checksum_computed_elem
	  != pgbuf_Double_Write.checksum_info->all_block_slots_data_checksum_computed[block_element_position])
	{
	  /* The checksum was not computed for all block slots. */
	  all_slots_checksum_computed = false;
	}
    }

  if (block_slots_checksum_computed)
    {
      *block_slots_checksum_computed = slots_checksum_computed;
    }

  if (block_needs_flush)
    {
      *block_needs_flush = all_slots_checksum_computed;
    }

  return NO_ERROR;
}

/*
 * dwb_set_data_on_next_slot () - set DWB data at the next slot, if possible
 *
 * return   : error code
 * thread_p(in): thread entry
 * io_page_p(in): The data that will be set on next slot
 * can_wait(in): True, if can wait
 * dwb_slot(out): Pointer to the next free DWB slot
 */
int
dwb_set_data_on_next_slot (THREAD_ENTRY * thread_p, FILEIO_PAGE * io_page_p, bool can_wait, DWB_SLOT ** dwb_slot)
{
  int error_code;
  assert (dwb_slot != NULL && io_page_p != NULL);

  /* Acquire the slot before setting the data. */
  error_code = dwb_acquire_next_slot (thread_p, can_wait, dwb_slot);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  assert (can_wait == false || *dwb_slot != NULL);
  if (*dwb_slot == NULL)
    {
      return NO_ERROR;
    }

  /* Set data on slot. */
  dwb_set_slot_data (*dwb_slot, io_page_p);

  return NO_ERROR;
}

/*
 * dwb_add_page () - add page content to double write buffer
 *
 * return   : error code
 * thread_p (in): thread entry
 * io_page_p(in): In-memory address where the current content of page resides
 * vpid(in): Page identifier
 * dwb_slot(in): DWB slot that contains the location where the page content must be added
 */
int
dwb_add_page (THREAD_ENTRY * thread_p, FILEIO_PAGE * io_page_p, VPID * vpid, DWB_SLOT * dwb_slot)
{
  unsigned int prev_block_no, count_wb_pages;
  UINT64 position_with_flags;
  char *write_buffer = NULL;
  int error_code = NO_ERROR;
  int retry_flush_iter = 0, retry_flush_max = 5;
  DWB_BLOCK *block = NULL, *prev_block = NULL;
  LF_TRAN_ENTRY *t_entry = thread_get_tran_entry (thread_p, THREAD_TS_DWB_SLOTS);
  DWB_SLOTS_HASH_ENTRY *slots_hash_entry = NULL;
  bool checksum_computed, checksum_computation_started = false;
  int checksum_threads;

  assert ((io_page_p != NULL || dwb_slot->io_page != NULL) && vpid != NULL);
  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  if (dwb_slot == NULL)
    {
      error_code = dwb_set_data_on_next_slot (thread_p, io_page_p, true, &dwb_slot);
      if (error_code != NO_ERROR || dwb_slot == NULL)
	{
	  assert (false);
	  return error_code;
	}
    }

  if (!VPID_ISNULL (vpid))
    {
      error_code = dwb_slots_hash_insert (thread_p, vpid, dwb_slot);
      if (error_code != NO_ERROR)
	{
	  return error_code;
	}
    }

  block = &pgbuf_Double_Write.blocks[dwb_slot->block_no];
  count_wb_pages = ATOMIC_INC_32 (&block->count_wb_pages, 1);

  checksum_threads = prm_get_integer_value (PRM_ID_DWB_CHECKSUM_THREADS);
  if (count_wb_pages < DWB_BLOCK_NUM_PAGES)
    {
      if (checksum_threads == 0)
	{
	  return NO_ERROR;
	}

      /* Add checksum computation request, first. */
      dwb_add_checksum_computation_request (thread_p, block->block_no, dwb_slot->position_in_block);
      checksum_computation_started = false;
#if defined (SERVER_MODE)
      if (thread_is_dwb_checksum_computation_thread_available ())
	{
	  /* Wake up checksum thread to compute checksum. */
	  thread_wakeup_dwb_checksum_computation_thread ();
	  checksum_computation_started = true;

	  if (checksum_threads == 1)
	    {
	      return NO_ERROR;
	    }
	}

      if (thread_is_dwb_flush_block_thread_available ())
	{
	  if (dwb_needs_speedup_checksum_computation (thread_p))
	    {
	      /* Wake up flush with checksum thread to parallelize checksums computation. */
	      thread_wakeup_dwb_flush_block_with_checksum_thread ();
	      checksum_computation_started = true;
	    }
	}

      if (checksum_computation_started == false)
#endif
	{
	  error_code = dwb_slot_compute_checksum (thread_p, dwb_slot, true, &checksum_computed);
	  if (error_code != NO_ERROR)
	    {
	      return error_code;
	    }
	}
    }
  else if (count_wb_pages == DWB_BLOCK_NUM_PAGES)
    {
      prev_block_no = DWB_GET_PREV_BLOCK_NO (block->block_no);
    start_flush_block:
      /*
       * Full block. Need to flush the current block. First, check whether the previous block was flushed,
       * when we advanced the global position.
       */
      position_with_flags = ATOMIC_INC_64 (&pgbuf_Double_Write.position_with_flags, 0ULL);
      if (DWB_IS_BLOCK_WRITE_STARTED (position_with_flags, prev_block_no))
	{
	  prev_block = &pgbuf_Double_Write.blocks[prev_block_no];
	  if ((prev_block->version < block->version)
	      || (prev_block->version == block->version && prev_block->block_no < block->block_no))
	    {
	      /*
	       * The previous block was not flushed yet. Needs to wait for it to be flushed.
	       * We may improve this - if the blocks are independent (different VPIDs) then do not wait.
	       * Should happens relative rarely, except the case when the buffer consist in only one block.
	       */
	      error_code = dwb_wait_for_block_completion (thread_p, prev_block_no);
	      if (error_code != NO_ERROR)
		{
		  if (error_code == ER_CSS_PTHREAD_COND_TIMEDOUT)
		    {
		      /* timeout, try again */
		      goto start_flush_block;
		    }
		  return error_code;
		}
	    }
	}

      checksum_computation_started = false;
      if (checksum_threads > 0)
	{
	  dwb_add_checksum_computation_request (thread_p, block->block_no, dwb_slot->position_in_block);
#if defined (SERVER_MODE)
	  if (checksum_threads == 1)
	    {
	      if (thread_is_dwb_checksum_computation_thread_available ())
		{
		  /* Wake up checksum thread to compute checksum. */
		  thread_wakeup_dwb_checksum_computation_thread ();
		  checksum_computation_started = true;
		}
	    }
#endif
	}

      /* Now it's safe to flush the block. The flush thread can compute the checksum for latest slot. */
#if defined (SERVER_MODE)
      if (thread_is_dwb_flush_block_thread_available ())
	{
	  /* Wakeup the thread to flush the block. */
	  thread_wakeup_dwb_flush_block_with_checksum_thread ();
	}
      else
#endif
	{
	  if ((checksum_threads > 1) || ((checksum_threads == 1 && checksum_computation_started == false)))
	    {
	      error_code = dwb_slot_compute_checksum (thread_p, dwb_slot, true, &checksum_computed);
	      if (error_code != NO_ERROR)
		{
		  return error_code;
		}
	    }

	  if (checksum_threads > 0)
	    {
	    retry:
	      if (!pgbuf_block_has_all_checksums_computed (thread_p, block->block_no))
		{
		  /* Wait for checksum thread to finish */
#if defined (SERVER_MODE)
		  thread_sleep (10);
		  goto retry;
#endif
		}
	    }

	flush_block:
	  /* Flush all pages from current block */
	  error_code = dwb_flush_block (thread_p, block, NULL);
	  if (error_code != NO_ERROR)
	    {
	      /* Something wrong happens, sleep 10 msec and try again. */
	      if (retry_flush_iter < retry_flush_max)
		{
#if defined(SERVER_MODE)
		  thread_sleep (10);
#endif
		  retry_flush_iter++;
		  goto flush_block;
		}

	      return error_code;
	    }
	}
    }
#if 0
  else
    {
      /* Impossible */
      assert (false);
    }
#endif

  return NO_ERROR;
}

/*
 * dwb_is_created () - check whether double write buffer was created
 *
 * return   : error code
 * thread_p (in): thread entry
 */
bool
dwb_is_created (THREAD_ENTRY * thread_p)
{
  UINT64 position_with_flags = ATOMIC_INC_64 (&pgbuf_Double_Write.position_with_flags, 0ULL);
  return DWB_IS_CREATED (position_with_flags);
}

/*
 * dwb_create () - create double write buffer
 *
 * return   : error code
 * thread_p (in): thread entry
 * dwb_path_p (in) : double write buffer volume block
 * db_name_p (in) : database name
 */
int
dwb_create (THREAD_ENTRY * thread_p, const char *dwb_path_p, const char *db_name_p)
{
  UINT64 current_position_with_flags, new_position_with_flags;
  int error_code = NO_ERROR;

  error_code = dwb_starts_structure_modification (thread_p, &current_position_with_flags);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  /* DWB structure modification started, no other transaction can modify the global position with flags */
  if (DWB_IS_CREATED (current_position_with_flags))
    {
      /* Already created, restore the modification flag. */
      goto end;
    }

  error_code = dwb_create_internal (thread_p, dwb_path_p, db_name_p);
  if (error_code != NO_ERROR)
    {
      goto end;
    }

  /* Set creation flag. */
  new_position_with_flags = DWB_STARTS_CREATION (current_position_with_flags);
  if (!ATOMIC_CAS_64 (&pgbuf_Double_Write.position_with_flags, current_position_with_flags, new_position_with_flags))
    {
      /* Impossible. */
      assert (false);
    }
  current_position_with_flags = new_position_with_flags;

end:
  /* Ends the modification, allowing to others to modify global position with flags. */
  dwb_ends_structure_modification (thread_p, current_position_with_flags);
  return error_code;
}

/*
 * dwb_load_and_recover () - load and recover pages from double write buffer
 *
 * return   : error code
 * thread_p (in): thread entry
 * dwb_path_p (in) : double write buffer volume block
 * db_name_p (in) : database name
 *
 *  Note: This function is called at recovery. The corrupted pages are recovered from double write volume buffer disk.
 *    Then, double write volume buffer disk is recreated according to user specifications. 
 */
int
dwb_load_and_recover_pages (THREAD_ENTRY * thread_p, const char *dwb_path_p, const char *db_name_p)
{
  int error_code = NO_ERROR, read_fd = NULL_VOLDES, write_fd = NULL_VOLDES;
  unsigned int num_pages, buffer_size /*min_buffer_size, max_buffer_size, */ ;
  char *buffer = NULL;
  VPID *vpid = NULL;
  DWB_BLOCK *rcv_block = NULL;
  DWB_SLOT *p_dwb_ordered_slots = NULL;

  assert (pgbuf_Double_Write.vdes == NULL_VOLDES);
  fileio_make_dwb_name (dwb_volume_name, dwb_path_p, db_name_p);

  if (fileio_is_volume_exist (dwb_volume_name))
    {
      /* Open DWB volume first */
      read_fd = fileio_mount (thread_p, boot_db_full_name (), dwb_volume_name, LOG_DBDWB_VOLID, false, false);
      if (read_fd == NULL_VOLDES)
	{
	  return ER_IO_MOUNT_FAIL;
	}

      num_pages = fileio_get_number_of_volume_pages (read_fd, IO_PAGESIZE);
      buffer_size = num_pages * IO_PAGESIZE;
      // if (sysprm_get_range (prm_get_name (PRM_ID_DWB_SIZE), &min_buffer_size,
      //              &max_buffer_size) != NO_ERROR)
      //   if (buffer_size < min_buffer_size || buffer_size > max_buffer_size)
      //     {
      ///* TODO - er_set */
      //goto exit_on_error;
      //     }
      //   else if ((buffer_size % (512 * 1024)) != 0)       /* TO DO - macro */
      //     {
      ///* TODO - er_set */
      //goto exit_on_error;
      //     }

      assert (buffer_size % (512 * 1024) == 0);

      buffer = (char *) malloc (buffer_size * sizeof (char));
      if (buffer == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, buffer_size * sizeof (char));
	  error_code = ER_OUT_OF_VIRTUAL_MEMORY;
	  goto exit_on_error;
	}

      if (fileio_read_pages (thread_p, read_fd, buffer, 0, num_pages, IO_PAGESIZE) == NULL)
	{
	  error_code = ER_FAILED;
	  goto exit_on_error;
	}

      error_code = dwb_create_blocks (thread_p, 1, num_pages, &rcv_block);
      if (error_code != NO_ERROR)
	{
	  goto exit_on_error;
	}

      /* Order slots by VPID, to flush faster. */
      error_code = dwb_block_create_ordered_slots (rcv_block, &p_dwb_ordered_slots);
      if (error_code != NO_ERROR)
	{
	  error_code = ER_FAILED;
	  goto exit_on_error;
	}

      error_code = dwb_write_block (thread_p, rcv_block, p_dwb_ordered_slots);
      if (error_code != NO_ERROR)
	{
	  goto exit_on_error;
	}
      fileio_synchronize_all (thread_p, false);

      /* Dismount the file. */
      fileio_dismount (thread_p, read_fd);

      /* Destroy the old file. */
      fileio_unformat (thread_p, dwb_volume_name);
    }

  /* Since old file destroyed, now we can rebuild the new double write buffer with user specifications. */
  error_code = dwb_create (thread_p, dwb_path_p, db_name_p);
  if (error_code != NO_ERROR)
    {
      goto exit_on_error;
    }

end:
  if (p_dwb_ordered_slots != NULL)
    {
      free_and_init (p_dwb_ordered_slots);
    }

  if (rcv_block != NULL)
    {
      dwb_finalize_block (rcv_block);
      free_and_init (rcv_block);
    }

  if (buffer != NULL)
    {
      free_and_init (buffer);
    }

  return error_code;

exit_on_error:

  if (read_fd != NULL_VOLDES)
    {
      fileio_dismount (thread_p, read_fd);
    }
  goto end;
}

/*
 * dwb_destroy () - destroy double write buffer
 *
 * return   : error code
 * thread_p (in): thread entry 
 */
int
dwb_destroy (THREAD_ENTRY * thread_p)
{
  int error_code = NO_ERROR;
  UINT64 current_position_with_flags, new_position_with_flags;

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

  dwb_destroy_internal (thread_p);

  /* Set creation flag. */
  new_position_with_flags = DWB_ENDS_CREATION (current_position_with_flags);
  new_position_with_flags = DWB_RESET_POSITION (new_position_with_flags);
  if (!ATOMIC_CAS_64 (&pgbuf_Double_Write.position_with_flags, current_position_with_flags, new_position_with_flags))
    {
      /* Impossible. */
      assert (false);
    }
  current_position_with_flags = new_position_with_flags;

end:
  /* Ends the modification, allowing to others to modify global position with flags. */
  dwb_ends_structure_modification (thread_p, current_position_with_flags);
  return error_code;
}


/*
 * dwb_get_volume_name - return the double write volume name
 */
char *
dwb_get_volume_name ()
{
  return dwb_volume_name;
}

/*
 * dwb_flush_block_with_checksum(): flush the block
 *
 *   returns:
 *
 * Note:
 */
int
dwb_flush_block_with_checksum (THREAD_ENTRY * thread_p)
{
  unsigned int block_no;
  DWB_BLOCK *flush_block = NULL;
  int error_code = NO_ERROR, retry_flush_iter = 0, retry_flush_max = 5;
  UINT64 position_with_flags;
  bool block_slots_checksum_computed, block_needs_flush;
  int checksum_threads;

  position_with_flags = ATOMIC_INC_64 (&pgbuf_Double_Write.position_with_flags, 0ULL);
  if (!DWB_IS_CREATED (position_with_flags) || DWB_IS_MODIFYING_STRUCTURE (position_with_flags))
    {
      return NO_ERROR;
    }

  flush_block = NULL;

  checksum_threads = prm_get_integer_value (PRM_ID_DWB_CHECKSUM_THREADS);
  if (checksum_threads == 0)
    {
      unsigned int i;
      block_no = DWB_NUM_TOTAL_BLOCKS;
      for (i = 0; i < DWB_NUM_TOTAL_BLOCKS; i++)
	{
	  if (pgbuf_Double_Write.blocks[i].count_wb_pages == DWB_BLOCK_NUM_PAGES)
	    {
	      block_no = i;
	      break;
	    }
	}
    }
  else
    {
      pgbuf_find_block_with_all_checksums_computed (thread_p, &block_no);
    }

  if (block_no < DWB_NUM_TOTAL_BLOCKS)
    {
      flush_block = &pgbuf_Double_Write.blocks[block_no];
    start_flush_block:
      /* Flush all pages from current block */
      assert (flush_block != NULL && flush_block->count_wb_pages == DWB_BLOCK_NUM_PAGES);
      error_code = dwb_flush_block (thread_p, flush_block, NULL);
      if (error_code != NO_ERROR)
	{
	  /* Something wrong happens, sleep 10 msec and try again. */
	  if (retry_flush_iter < retry_flush_max)
	    {
#if defined(SERVER_MODE)
	      thread_sleep (10);
#endif
	      retry_flush_iter++;
	      goto start_flush_block;
	    }

	  return error_code;
	}

      return NO_ERROR;
    }

  if (checksum_threads <= 1)
    {
      return NO_ERROR;
    }

  /* Couldn't find the block for flush. However, we can computes some checksums to reach flushing point faster. */
  for (block_no = 0; block_no < pgbuf_Double_Write.num_blocks; block_no++)
    {
      error_code = dwb_compute_block_checksums (thread_p, &pgbuf_Double_Write.blocks[block_no],
						&block_slots_checksum_computed, &block_needs_flush);
      if (error_code != NO_ERROR)
	{
	  assert (false);
	  return error_code;
	}

      if (block_needs_flush)
	{
	  flush_block = &pgbuf_Double_Write.blocks[block_no];
	  goto start_flush_block;
	}
    }

  return NO_ERROR;
}

/*
 * dwb_flush_force () - force flushing the current content of DWB
 *
 * return   : error code
 * thread_p (in): thread entry.
 * all_sync (out): true, if everything synchronized
 */
int
dwb_flush_force (THREAD_ENTRY * thread_p, bool * all_sync)
{
  UINT64 position_with_flags, blocks_status, block_version;
  unsigned int current_block_no = DWB_NUM_TOTAL_BLOCKS;
  char page_buf[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT];
  FILEIO_PAGE *iopage;
  VPID null_vpid = { NULL_VOLID, NULL_PAGEID };
  int error_code = NO_ERROR;

  assert (all_sync != NULL);

  *all_sync = false;
start:
  position_with_flags = ATOMIC_INC_64 (&pgbuf_Double_Write.position_with_flags, 0ULL);

  if (!DWB_IS_CREATED (position_with_flags))
    {
      /* Nothing to do. Everything flushed. */
      return NO_ERROR;
    }

  if (DWB_IS_MODIFYING_STRUCTURE (position_with_flags))
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
	  return error_code;
	}
    }

  blocks_status = DWB_GET_BLOCK_STATUS (position_with_flags);
  if (blocks_status == 0)
    {
      /* Nothing to do. Everything flushed. */
      return NO_ERROR;
    }

  current_block_no = DWB_GET_BLOCK_NO_FROM_POSITION (position_with_flags);
  block_version = pgbuf_Double_Write.blocks[current_block_no].version;
  assert (current_block_no < DWB_NUM_TOTAL_BLOCKS);

  if (position_with_flags != ATOMIC_INC_64 (&pgbuf_Double_Write.position_with_flags, 0ULL))
    {
      /* The position_with_flags modified meanwhile by concurrent threads. */
      goto start;
    }

  /*
   * Add NULL pages to force block flushing. In this way, preserve also block flush order. This means that all
   * blocks are flushed - the entire DWB.
   */
  iopage = (FILEIO_PAGE *) PTR_ALIGN (page_buf, MAX_ALIGNMENT);
  while ((pgbuf_Double_Write.blocks[current_block_no].count_wb_pages != DWB_BLOCK_NUM_PAGES)
	 && (block_version == pgbuf_Double_Write.blocks[current_block_no].version))
    {
      /* The block is not full yet. Add more null pages. */
      error_code = dwb_add_page (thread_p, iopage, &null_vpid, NULL);
      if (error_code != NO_ERROR)
	{
	  if (error_code == ER_DWB_DISABLED)
	    {
	      er_clear ();
	      /* DWB disabled meanwhile, everything flushed. */
	      return NO_ERROR;
	    }
	}
    }

  if (block_version == pgbuf_Double_Write.blocks[current_block_no].version)
    {
      /* Not flushed yet. Wait for current block flush. */
      error_code = dwb_wait_for_block_completion (thread_p, current_block_no);
      if (error_code != NO_ERROR)
	{
	  if (error_code == ER_CSS_PTHREAD_COND_TIMEDOUT)
	    {
	      /* timeout, try again */
	      goto start;
	    }
	  return error_code;
	}
    }

  *all_sync = true;

  return NO_ERROR;
}

/*
 * dwb_flush_block_with_checksum(): compute checksum for requested slots
 *
 *   returns:
 *
 * Note:
 */
int
dwb_compute_checksums (THREAD_ENTRY * thread_p)
{
  UINT64 position_with_flags;
  unsigned int num_block, start_block, end_block;
  bool found = false, block_slots_checksums_computed, block_needs_flush, checksum_computed;
  int error_code = NO_ERROR;

start:
  position_with_flags = ATOMIC_INC_64 (&pgbuf_Double_Write.position_with_flags, 0ULL);
  if (!DWB_IS_CREATED (position_with_flags) || DWB_IS_MODIFYING_STRUCTURE (position_with_flags))
    {
      return true;
    }

  pgbuf_find_block_with_all_checksums_requested (thread_p, &start_block);
  if (start_block < DWB_NUM_TOTAL_BLOCKS)
    {
      end_block = start_block + 1;
    }
  else
    {
      start_block = 0;
      end_block = pgbuf_Double_Write.num_blocks;
    }

  /* Compute checksums and/or flush the block. */
  checksum_computed = false;
  for (num_block = start_block; num_block < end_block; num_block++)
    {
      error_code =
	dwb_compute_block_checksums (thread_p, &pgbuf_Double_Write.blocks[num_block],
				     &block_slots_checksums_computed, &block_needs_flush);
      if (error_code != NO_ERROR)
	{
	  assert (false);
	  return error_code;
	}
#if defined(SERVER_MODE)
      if (block_needs_flush)
	{
	  if (thread_is_dwb_flush_block_thread_available ())
	    {
	      /* Wakeup the thread to flush the block. */
	      thread_wakeup_dwb_flush_block_with_checksum_thread ();
	    }
	}
#endif

      if (block_slots_checksums_computed)
	{
	  checksum_computed = true;
	}
    }

  if (checksum_computed)
    {
      /* Check again whether we can compute other checksums, requested meanwhile by concurrent transaction. */
      goto start;
    }

  return NO_ERROR;
}

/*
 * dwb_read_page () - read page from DWB
 *
 * return   : error code
 * thread_p (in): thread entry
 * vpid(in): Page identifier
 * io_page_p(in): In-memory address where the content of the page will be copied 
 * success(in): true, if found and read from DWB
 */
int
dwb_read_page (THREAD_ENTRY * thread_p, const VPID * vpid, void *io_page, bool * success)
{
  LF_TRAN_ENTRY *t_entry = thread_get_tran_entry (NULL, THREAD_TS_DWB_SLOTS);
  DWB_SLOTS_HASH_ENTRY *slots_hash_entry = NULL;

  assert (vpid != NULL && io_page != NULL && success != NULL);

  *success = false;
  if (!dwb_is_created (thread_p))
    {
      return NO_ERROR;
    }

  if (lf_hash_find (t_entry, &pgbuf_Double_Write.slots_hash->ht, (void *) vpid, (void **) &slots_hash_entry) !=
      NO_ERROR)
    {
      /* Should not happen */
      assert (false);
      return ER_FAILED;
    }
  else if (slots_hash_entry != NULL)
    {
      /* read from DWB - TO DO volatile access + debug code */
      assert (slots_hash_entry->slot->io_page != NULL && slots_hash_entry->slot->io_page->prv.pageid == vpid->pageid
	      && slots_hash_entry->slot->io_page->prv.volid == vpid->volid);

      memcpy ((char *) io_page, (char *) slots_hash_entry->slot->io_page, IO_PAGESIZE);
      pthread_mutex_unlock (&slots_hash_entry->mutex);
      *success = true;
    }

  return NO_ERROR;
}
