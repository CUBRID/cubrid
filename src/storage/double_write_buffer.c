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
#include "perf_monitor.h"


#define DWB_SLOTS_HASH_SIZE		    1000
#define DWB_SLOTS_FREE_LIST_SIZE	    100

#define DWB_MIN_SIZE			    (512 * 1024)
#define DWB_MAX_SIZE			    (32 * 1024 * 1024)
#define DWB_MIN_BLOCKS			    1
#define DWB_MAX_BLOCKS			    32
#define DWB_CHECKSUM_ELEMENT_NO_BITS	    64
#define DWB_CHECKSUM_ELEMENT_LOG2_NO_BITS   6
#define DWB_CHECKSUM_ELEMENT_ALL_BITS	    0xffffffffffffffff
#define DWB_CHECKSUM_ELEMENT_NO_FROM_SLOT_POS(bit_pos) ((bit_pos) >> DWB_CHECKSUM_ELEMENT_LOG2_NO_BITS)
#define DWB_CHECKSUM_ELEMENT_BIT_FROM_SLOT_POS(bit_pos) ((bit_pos) & (DWB_CHECKSUM_ELEMENT_NO_BITS - 1))

/* The total number of blocks. */
#define DWB_NUM_TOTAL_BLOCKS	   (double_Write_Buffer.num_blocks)
/* The total number of pages. */
#define DWB_NUM_TOTAL_PAGES	   (double_Write_Buffer.num_pages)
/* The number of pages in each block. */
#define DWB_BLOCK_NUM_PAGES	   (double_Write_Buffer.num_block_pages)
/* LOG2 from total number of blocks. */
#define DWB_LOG2_BLOCK_NUM_PAGES	   (double_Write_Buffer.log2_num_block_pages)
/* The number of checksum elements in each block  */
#define DWB_CHECKSUM_NUM_ELEMENTS_IN_BLOCK   (double_Write_Buffer.checksum_info->num_checksum_elements_in_block)

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
  ((unsigned int)DWB_GET_POSITION (position_with_flags) >>  (DWB_LOG2_BLOCK_NUM_PAGES))

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
  (&(double_Write_Buffer.blocks[DWB_GET_PREV_BLOCK_NO(block_no)]))

/* Get DWB next block number. */
#define DWB_GET_NEXT_BLOCK_NO(block_no) \
  ((block_no) == (DWB_NUM_TOTAL_BLOCKS - 1) ? 0 : ((block_no) + 1))

/* Get DWB checksum start position for a block. */
#define DWB_BLOCK_GET_CHECKSUM_START_POSITION(block_no) \
  (DWB_CHECKSUM_NUM_ELEMENTS_IN_BLOCK * (block_no))

/* Get DWB requested checksum element. */
#define DWB_GET_REQUESTED_CHECKSUMS_ELEMENT(position) \
  (ATOMIC_INC_64 (&double_Write_Buffer.checksum_info->requested_checksums[position], 0ULL))

/* Add value to DWB requested checksum element. */
#define DWB_ADD_TO_REQUESTED_CHECKSUMS_ELEMENT(position, value) \
  (assert ((ATOMIC_INC_64 (&double_Write_Buffer.checksum_info->requested_checksums[position], 0ULL) & (value)) == 0), \
   ATOMIC_INC_64 (&double_Write_Buffer.checksum_info->requested_checksums[position], value))

/* Check whether a value was added to requested checksum element. */
#define DWB_IS_ADDED_TO_REQUESTED_CHECKSUMS_ELEMENT(position, value) \
  ((DWB_GET_REQUESTED_CHECKSUMS_ELEMENT (element_position) & (value)) == (value))

/* Reset a DWB requested checksum element. */
#define DWB_RESET_REQUESTED_CHECKSUMS_ELEMENT(position) \
  (ATOMIC_TAS_64 (&double_Write_Buffer.checksum_info->requested_checksums[position], 0ULL))

/* Get DWB computed checksum element. */
#define DWB_GET_COMPUTED_CHECKSUMS_ELEMENT(position) \
  (ATOMIC_INC_64 (&double_Write_Buffer.checksum_info->computed_checksums[position], 0ULL))

/* Add value to DWB computed checksum element. */
#define DWB_ADD_TO_COMPUTED_CHECKSUMS_ELEMENT(position, value) \
  (assert ((ATOMIC_INC_64 (&double_Write_Buffer.checksum_info->computed_checksums[position], 0ULL) & (value)) == 0), \
  ATOMIC_INC_64 (&double_Write_Buffer.checksum_info->computed_checksums[position], value))

/* Check whether a value was added to computed checksum element. */
#define DWB_IS_ADDED_TO_COMPUTED_CHECKSUMS_ELEMENT(position, value) \
  ((DWB_GET_COMPUTED_CHECKSUMS_ELEMENT (element_position) & (value)) == (value))

/* Reset a DWB computed checksum element. */
#define DWB_RESET_COMPUTED_CHECKSUMS_ELEMENT(position) \
  (ATOMIC_TAS_64 (&double_Write_Buffer.checksum_info->computed_checksums[position], 0ULL))

/* Get DWB completed checksum element. */
#define DWB_GET_COMPLETED_CHECKSUMS_ELEMENT(position) \
  (ATOMIC_INC_64 (&double_Write_Buffer.checksum_info->completed_checksums_mask[position], 0ULL))

/* Get DWB position in checksums element. */
#define DWB_GET_POSITION_IN_CHECKSUMS_ELEMENT(element_pos) \
  (ATOMIC_INC_32 (&double_Write_Buffer.checksum_info->positions[element_pos], 0))

/* Reset position in DWB checksums element. */
#define DWB_RESET_POSITION_IN_CHECKSUMS_ELEMENT(element_pos) \
  (ATOMIC_TAS_32 (&double_Write_Buffer.checksum_info->positions[element_pos], 0))

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

/* Slot checksum status. */
typedef enum
{
  DWB_SLOT_CHECKSUM_NOT_COMPUTED,	/* The checksum for data contained in slot was not computed. */
  DWB_SLOT_CHECKSUM_COMPUTED	/* The checksum for data contained in slot was computed. */
} DWB_SLOT_CHECKSUM_STATUS;

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

/* DWB checksum information. Used to allow parallel computation of checksums. */
typedef struct dwb_checksum_info DWB_CHECKSUM_INFO;
struct dwb_checksum_info
{
  volatile UINT64 *requested_checksums;	/* Bits array - 1 for each slot requiring checksum computation. */
  volatile UINT64 *computed_checksums;	/* Bits array - 1 for each slot having checksum computed. */
  volatile UINT64 *completed_checksums_mask;	/* Bits array - mask for block having all checksums computed. */
  volatile int *positions;	/* Positions in checksum arrays, used to search bits faster. */
  unsigned int length;		/* The length of checksum bits arrays. */
  unsigned int num_checksum_elements_in_block;	/* The number of checksum elements in each block */
};

typedef struct flush_volume_info FLUSH_VOLUME_INFO;
struct flush_volume_info
{
  int vdes;			/* The volume descriptor. */
  int num_pages;		/* The number of pages to flush in volume. */
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
};

/* Hash that store all pages in DWB. */
typedef struct dwb_slots_hash DWB_SLOTS_HASH;
struct dwb_slots_hash
{
  LF_HASH_TABLE ht;		/* Hash having VPID as key and DWB_SLOT as data. */
  LF_FREELIST freelist;		/* Used by hash. */
};

/* The double write buffer structure. */
typedef struct double_write_buffer DOUBLE_WRITE_BUFFER;
struct double_write_buffer
{
  DWB_BLOCK *blocks;		/* The blocks in DWB. */
  unsigned int num_blocks;	/* The total number of blocks in DWB - power of 2. */
  unsigned int num_pages;	/* The total number of pages in DWB - power of 2. */
  unsigned int num_block_pages;	/* The number of pages in a block - power of 2. */
  unsigned int log2_num_block_pages;	/* Logarithm from block number of pages. */
  unsigned int blocks_flush_counter;	/* The blocks flush counter. */
  volatile unsigned int next_block_to_flush;	/* Next block to flush */

  DWB_CHECKSUM_INFO *checksum_info;	/* The checksum info. */

  pthread_mutex_t mutex;	/* The mutex to protect the wait queue. */
  DWB_WAIT_QUEUE wait_queue;	/* The wait queue, used when the DWB structure changed. */

  UINT64 volatile position_with_flags;	/* The current position in double write buffer and flags. Flags keep the
					 * state of each block (started, ended), create DWB status, modify DWB status.                                   
					 */
  DWB_SLOTS_HASH *slots_hash;	/* The slots hash. */
  int vdes;			/* The volume file descriptor. */

  DWB_BLOCK *volatile helper_flush_block;	/* The block that will be flushed by helper thread. */
};

/* DWB volume name. */
char dwb_Volume_name[PATH_MAX];
/* DWB. */
static DOUBLE_WRITE_BUFFER double_Write_Buffer = { NULL, 0, 0, 0, 0, 0, 0, NULL, PTHREAD_MUTEX_INITIALIZER,
  DWB_WAIT_QUEUE_INITIALIZER, 0, NULL, NULL_VOLDES, NULL
};

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
STATIC_INLINE void dwb_adjust_write_buffer_values (unsigned int *p_double_write_buffer_size,
						   unsigned int *p_num_blocks) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int dwb_wait_for_block_completion (THREAD_ENTRY * thread_p, unsigned int block_no)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int dwb_signal_waiting_thread (void *data) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void dwb_signal_block_completion (THREAD_ENTRY * thread_p, DWB_BLOCK * dwb_block)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int dwb_block_create_ordered_slots (DWB_BLOCK * block, DWB_SLOT ** p_dwb_ordered_slots,
						  unsigned int *p_ordered_slots_length) __attribute__ ((ALWAYS_INLINE));
static int dwb_compare_vol_fd (const void *v1, const void *v2);
STATIC_INLINE void
dwb_add_volume_to_block_flush_area (THREAD_ENTRY * thread_p, DWB_BLOCK * block, int vol_fd,
				    FLUSH_VOLUME_INFO ** flush_volume_info) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int dwb_write_block (THREAD_ENTRY * thread_p, DWB_BLOCK * block, DWB_SLOT * p_dwb_slots,
				   unsigned int ordered_slots_length, bool remove_from_hash)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int dwb_flush_block (THREAD_ENTRY * thread_p, DWB_BLOCK * block, UINT64 * current_position_with_flags)
  __attribute__ ((ALWAYS_INLINE));
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
STATIC_INLINE void dwb_intialize_slot (DWB_SLOT * slot, FILEIO_PAGE * io_page,
				       unsigned int position_in_block, unsigned int block_no)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int dwb_create_slots_hash (THREAD_ENTRY * thread_p, DWB_SLOTS_HASH ** p_slots_hash)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void dwb_finalize_slots_hash (DWB_SLOTS_HASH * slots_hash) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void dwb_initialize_block (DWB_BLOCK * block, unsigned int block_no,
					 unsigned int count_wb_pages, char *write_buffer, DWB_SLOT * slots,
					 FLUSH_VOLUME_INFO * flush_volumes_info, unsigned int count_flush_volumes_info,
					 unsigned int max_to_flush_vdes) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int dwb_create_blocks (THREAD_ENTRY * thread_p, unsigned int num_blocks, unsigned int num_block_pages,
				     DWB_BLOCK ** p_blocks) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void dwb_finalize_block (DWB_BLOCK * block) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void dwb_initialize_checksum_info (DWB_CHECKSUM_INFO * checksum_info, UINT64 * requested_checksums,
						 UINT64 * computed_checksums, int *positions,
						 UINT64 * completed_checksums_mask, unsigned int checksum_length,
						 unsigned int num_checksum_elements_in_block)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int dwb_create_checksum_info (THREAD_ENTRY * thread_p, unsigned int num_blocks,
					    unsigned int num_block_pages, DWB_CHECKSUM_INFO ** p_checksum_info)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void dwb_add_checksum_computation_request (THREAD_ENTRY * thread_p, unsigned int block_no,
							 unsigned int position_in_block)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void dwb_mark_checksum_computed (THREAD_ENTRY * thread_p, unsigned int block_no,
					       unsigned int position_in_block) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void dwb_finalize_checksum_info (DWB_CHECKSUM_INFO * checksum_info) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE bool dwb_needs_speedup_checksum_computation (THREAD_ENTRY * thread_p) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int dwb_slot_compute_checksum (THREAD_ENTRY * thread_p, DWB_SLOT * slot, bool mark_checksum_computed,
					     bool * checksum_computed) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int dwb_create_internal (THREAD_ENTRY * thread_p, const char *dwb_volume_name,
				       UINT64 * current_position_with_flags) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void dwb_get_next_block_for_flush (THREAD_ENTRY * thread_p, unsigned int *block_no)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE bool dwb_block_has_all_checksums_computed (unsigned int block_no);
STATIC_INLINE int dwb_compute_block_checksums (THREAD_ENTRY * thread_p, DWB_BLOCK * block,
					       bool * block_slots_checksum_computed, bool * block_needs_flush)
  __attribute__ ((ALWAYS_INLINE));

/* Slots hash functions. */
static void *dwb_slots_hash_entry_alloc (void);
static int dwb_slots_hash_entry_free (void *entry);
static int dwb_slots_hash_entry_init (void *entry);
static int dwb_slots_hash_key_copy (void *src, void *dest);
static int dwb_slots_hash_compare_key (void *key1, void *key2);
static unsigned int dwb_slots_hash_key (void *key, int hash_table_size);

STATIC_INLINE int dwb_slots_hash_insert (THREAD_ENTRY * thread_p, VPID * vpid, DWB_SLOT * slot, int *inserted)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int dwb_slots_hash_delete (THREAD_ENTRY * thread_p, DWB_SLOT * slot);

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
      wait_queue->count++;
    }

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
  if (wait_queue_entry != NULL)
    {
      if (func != NULL)
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
  * dwb_adjust_write_buffer_values () - Adjust double write buffer values.
  *
  * return   : Error code.
  * p_double_write_buffer_size (in/out) : Double write buffer size.
  * p_num_blocks (in/out): The number of blocks.
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
	      assert (limit1 <= DWB_MAX_SIZE);
	      if (limit1 == DWB_MAX_SIZE)
		{
		  break;
		}
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
	      num_blocks = num_blocks & (num_blocks - 1);
	    }
	  while (!IS_POWER_OF_2 (num_blocks));

	  *p_num_blocks = num_blocks << 1;

	  assert (*p_num_blocks <= max_size);
	}
    }
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
  int retry_flush_iter = 0, retry_flush_max = 5;
  unsigned int start_block_no, blocks_count;

  assert (current_position_with_flags != NULL);

start_structure_modification:
  local_current_position_with_flags = ATOMIC_INC_64 (&double_Write_Buffer.position_with_flags, 0ULL);
  if (DWB_IS_MODIFYING_STRUCTURE (local_current_position_with_flags))
    {
      /* Only one thread can change the structure */
      return ER_FAILED;
    }

  new_position_with_flags = DWB_STARTS_MODIFYING_STRUCTURE (local_current_position_with_flags);

  /* Start structure modifications, the threads that want to flush afterwards, have to wait. */
  if (!ATOMIC_CAS_64 (&double_Write_Buffer.position_with_flags,
		      local_current_position_with_flags, new_position_with_flags))
    {
      /* Someone else advanced the position, try again. */
      goto start_structure_modification;
    }

#if defined(SERVER_MODE)
check_dwb_flush_thread_is_running:
  if (thread_dwb_flush_block_thread_is_running () || thread_dwb_flush_block_helper_thread_is_running ())
    {
      /* Can't modify structure while flush thread can access DWB. */
      thread_sleep (20);
      goto check_dwb_flush_thread_is_running;
    }
#endif

  /* Since we set the modify structure flag, I'm the only thread that access the DWB. */
  local_current_position_with_flags = ATOMIC_INC_64 (&double_Write_Buffer.position_with_flags, 0ULL);

  /* Need to flush incomplete blocks, ordered by version. */
  start_block_no = DWB_NUM_TOTAL_BLOCKS;
  min_version = 0xffffffffffffffff;
  blocks_count = 0;
  for (block_no = 0; block_no < DWB_NUM_TOTAL_BLOCKS; block_no++)
    {
      if (DWB_IS_BLOCK_WRITE_STARTED (local_current_position_with_flags, block_no))
	{
	  if (double_Write_Buffer.blocks[block_no].version < min_version)
	    {
	      min_version = double_Write_Buffer.blocks[block_no].version;
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
	flush_block:
	  /* Flush all pages from current block */
	  error_code = dwb_flush_block (thread_p, &double_Write_Buffer.blocks[block_no],
					&local_current_position_with_flags);
	  if (error_code != NO_ERROR)
	    {
	      /* Something wrong happened, sleep 10 msec and try again. */
	      if (retry_flush_iter < retry_flush_max)
		{
#if defined(SERVER_MODE)
		  thread_sleep (10);
#endif
		  retry_flush_iter++;
		  goto flush_block;
		}

	      if (prm_get_bool_value (PRM_ID_DWB_ENABLE_LOG))
		{
		  _er_log_debug (ARG_FILE_LINE, "DWB error: Can't flush block = %d having version %lld\n", block_no,
				 double_Write_Buffer.blocks[block_no].version);
		}

	      return error_code;
	    }

	  if (prm_get_bool_value (PRM_ID_DWB_ENABLE_LOG))
	    {
	      _er_log_debug (ARG_FILE_LINE, "DWB flushed %d block having version %lld\n",
			     block_no, double_Write_Buffer.blocks[block_no].version);
	    }
	  blocks_count--;
	}

      block_no = (block_no + 1) % DWB_NUM_TOTAL_BLOCKS;
    }

  local_current_position_with_flags = ATOMIC_INC_64 (&double_Write_Buffer.position_with_flags, 0ULL);
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
  assert (double_Write_Buffer.position_with_flags == current_position_with_flags);
  ATOMIC_TAS_64 (&double_Write_Buffer.position_with_flags, new_position_with_flags);

  /* Signal the other threads. */
  dwb_signal_structure_modificated (thread_p);
}

/*
 * dwb_intialize_slot () - Initialize a DWB slot.
 *
 * return   : Error code.
 * slot (in/out) : The slot to initialize.
 * io_page (in) : The page.
 * position_in_block(in): The position in DWB block.
 * block_no(in): The number of the block where the slot reside.
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
  slot->checksum_status = DWB_SLOT_CHECKSUM_NOT_COMPUTED;
}

/*
 * dwb_initialize_block () - Initialize a block.
 *
 * return : Nothing.
 * checksum_info (in/out) : The checksum info.
 * requested_checksums (in) : Bits array containing checksum requests for slot data.
 * computed_checksums (in): Bits array containing computed checksums for slot data.
 * positions (in): Positions in checksum arrays.
 * completed_checksums_mask(in): Mask for block having all checksums computed.
 * checksum_length(in): The length of checksum bits arrays.
 * num_checksum_elements_in_block(in): The number of checksum elements in each block.
 */
STATIC_INLINE void
dwb_initialize_checksum_info (DWB_CHECKSUM_INFO * checksum_info, UINT64 * requested_checksums,
			      UINT64 * computed_checksums, int *positions, UINT64 * completed_checksums_mask,
			      unsigned int checksum_length, unsigned int num_checksum_elements_in_block)
{
  assert (checksum_info != NULL);

  checksum_info->requested_checksums = requested_checksums;
  checksum_info->computed_checksums = computed_checksums;
  checksum_info->positions = positions;
  checksum_info->completed_checksums_mask = completed_checksums_mask;
  checksum_info->length = checksum_length;
  checksum_info->num_checksum_elements_in_block = num_checksum_elements_in_block;
}

/*
 * dwb_create_checksum_info () - Create checksum info.
 *
 * return   : Error code.
 * thread_p (in) : The thread entry.
 * num_blocks(in): The number of blocks.
 * num_block_pages(in): The number of block pages.
 * p_checksum_info(out): The created checksum info.
 */
STATIC_INLINE int
dwb_create_checksum_info (THREAD_ENTRY * thread_p, unsigned int num_blocks, unsigned int num_block_pages,
			  DWB_CHECKSUM_INFO ** p_checksum_info)
{
  UINT64 *requested_checksums = NULL, *computed_checksums = NULL, *completed_checksums_mask = NULL;
  int *positions = NULL;
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

  num_checksum_elements_in_block = (DB_ALIGN (num_block_pages, DWB_CHECKSUM_ELEMENT_NO_BITS)
				    / DWB_CHECKSUM_ELEMENT_NO_BITS);
  checksum_length = num_checksum_elements_in_block * num_blocks;

  /* requested_checksums */
  requested_checksums = (UINT64 *) malloc (checksum_length * sizeof (UINT64));
  if (requested_checksums == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, checksum_length * sizeof (UINT64));
      error_code = ER_OUT_OF_VIRTUAL_MEMORY;
      goto exit_on_error;
    }
  memset (requested_checksums, 0, checksum_length * sizeof (UINT64));

  /* computed_checksums */
  computed_checksums = (UINT64 *) malloc (checksum_length * sizeof (UINT64));
  if (computed_checksums == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, checksum_length * sizeof (UINT64));
      error_code = ER_OUT_OF_VIRTUAL_MEMORY;
      goto exit_on_error;
    }
  memset (computed_checksums, 0, checksum_length * sizeof (UINT64));

  /* positions */
  positions = (int *) malloc (checksum_length * sizeof (int));
  if (positions == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, checksum_length * sizeof (int));
      error_code = ER_OUT_OF_VIRTUAL_MEMORY;
      goto exit_on_error;
    }
  memset (positions, 0, checksum_length * sizeof (int));

  /* completed_checksums_mask */
  completed_checksums_mask = (UINT64 *) malloc (num_checksum_elements_in_block * sizeof (UINT64));
  if (completed_checksums_mask == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      num_checksum_elements_in_block * sizeof (UINT64));
      error_code = ER_OUT_OF_VIRTUAL_MEMORY;
      goto exit_on_error;
    }

  for (num_pages2 = num_block_pages, i = 0; num_pages2 >= DWB_CHECKSUM_ELEMENT_NO_BITS;
       num_pages2 -= DWB_CHECKSUM_ELEMENT_NO_BITS, i++)
    {
      completed_checksums_mask[i] = DWB_CHECKSUM_ELEMENT_ALL_BITS;
    }
  if (num_pages2 > 0)
    {
      assert (num_pages2 < DWB_CHECKSUM_ELEMENT_NO_BITS && i < num_checksum_elements_in_block);
      completed_checksums_mask[i] = (1 << num_pages2) - 1;
    }

  /* init checksum_info */
  dwb_initialize_checksum_info (checksum_info, requested_checksums, computed_checksums,
				positions, completed_checksums_mask, checksum_length, num_checksum_elements_in_block);

  *p_checksum_info = checksum_info;

  return NO_ERROR;

exit_on_error:
  if (completed_checksums_mask != NULL)
    {
      free_and_init (completed_checksums_mask);
    }

  if (positions != NULL)
    {
      free_and_init (positions);
    }

  if (requested_checksums != NULL)
    {
      free_and_init (requested_checksums);
    }

  if (computed_checksums != NULL)
    {
      free_and_init (computed_checksums);
    }

  if (checksum_info != NULL)
    {
      free_and_init (checksum_info);
    }

  return error_code;
}

/*
 * dwb_add_checksum_computation_request () - Add a checksum computation request.
 *
 * return   : Nothing.
 * thread_p(in): The thread entry.
 * block_no(in): The block number.
 * position_in_block(in): The position in block, where checksum computation is requested.
 */
STATIC_INLINE void
dwb_add_checksum_computation_request (THREAD_ENTRY * thread_p, unsigned int block_no, unsigned int position_in_block)
{
  unsigned int element_position;
  UINT64 value;

  assert (block_no < DWB_NUM_TOTAL_BLOCKS && position_in_block < DWB_BLOCK_NUM_PAGES);

  element_position = (DWB_BLOCK_GET_CHECKSUM_START_POSITION (block_no)
		      + DWB_CHECKSUM_ELEMENT_NO_FROM_SLOT_POS (position_in_block));

  value = 1ULL << DWB_CHECKSUM_ELEMENT_BIT_FROM_SLOT_POS (position_in_block);

  assert (!DWB_IS_ADDED_TO_COMPUTED_CHECKSUMS_ELEMENT (element_position, value));

  DWB_ADD_TO_REQUESTED_CHECKSUMS_ELEMENT (element_position, value);
}

/*
 * dwb_mark_checksum_computed () - Mark checksum computed into bits array.
 *
 * return   : Nothing.
 * thread_p(in): The thread entry.
 * block_no(in): The block number.
 * position_in_block(in): The position in block.
 */
STATIC_INLINE void
dwb_mark_checksum_computed (THREAD_ENTRY * thread_p, unsigned int block_no, unsigned int position_in_block)
{
  unsigned int element_position;
  UINT64 value;

  assert (block_no < DWB_NUM_TOTAL_BLOCKS && position_in_block < DWB_BLOCK_NUM_PAGES);

  element_position = (DWB_BLOCK_GET_CHECKSUM_START_POSITION (block_no)
		      + DWB_CHECKSUM_ELEMENT_NO_FROM_SLOT_POS (position_in_block));

  value = 1ULL << DWB_CHECKSUM_ELEMENT_BIT_FROM_SLOT_POS (position_in_block);

  assert (DWB_IS_ADDED_TO_REQUESTED_CHECKSUMS_ELEMENT (element_position, value));

  DWB_ADD_TO_COMPUTED_CHECKSUMS_ELEMENT (element_position, value);
}

/*
 * dwb_finalize_block () - Finalize checksum info.
 *
 * return   : Nothing.
 * checksum_info (in/out) : The checksum info.
 */
STATIC_INLINE void
dwb_finalize_checksum_info (DWB_CHECKSUM_INFO * checksum_info)
{
  assert (checksum_info != NULL);

  if (checksum_info->requested_checksums != NULL)
    {
      free_and_init (checksum_info->requested_checksums);
    }

  if (checksum_info->computed_checksums != NULL)
    {
      free_and_init (checksum_info->computed_checksums);
    }

  if (checksum_info->positions != NULL)
    {
      free_and_init (checksum_info->positions);
    }

  if (checksum_info->completed_checksums_mask != NULL)
    {
      free_and_init (checksum_info->completed_checksums_mask);
    }
}

/*
 * dwb_needs_speedup_checksum_computation () - Check whether checksum computation is too slow.
 *
 * return   : Error code.
 * thread_p (in) : thread entry
 *
 *  Note: This function checks whether checksum thread remains behind. Currently, we consider that it remains
 *    behind if at least three pages are waiting for checksum computation. This computation is relative, since
 *    while computing, the checksum thread may advance.
 */
STATIC_INLINE bool
dwb_needs_speedup_checksum_computation (THREAD_ENTRY * thread_p)
{
#define DWB_CHECKSUM_REQUESTS_THRESHOLD 16
  int position_in_checksum_element, position;
  UINT64 requested_checksums_elem, bit_mask;
  unsigned int element_position, num_elements, counter;

  num_elements = DWB_CHECKSUM_NUM_ELEMENTS_IN_BLOCK * DWB_NUM_TOTAL_BLOCKS;

  counter = 0;
  for (element_position = 0; element_position < num_elements; element_position++)
    {
      requested_checksums_elem = DWB_GET_REQUESTED_CHECKSUMS_ELEMENT (element_position);
      position_in_checksum_element = DWB_GET_POSITION_IN_CHECKSUMS_ELEMENT (element_position);
      if (position_in_checksum_element >= DWB_CHECKSUM_ELEMENT_NO_BITS)
	{
	  continue;
	}

      bit_mask = 1ULL << position_in_checksum_element;
      for (position = position_in_checksum_element; position < DWB_CHECKSUM_ELEMENT_NO_BITS; position++)
	{
	  if ((requested_checksums_elem & bit_mask) == 0)
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
 * dwb_slot_compute_checksum () - Compute checksum for page contained in a slot.
 *
 * return   : Error code
 * thread_p (in) : Thread entry.
 * slot(in/out): DWB slot
 * mark_checksum_computed(in): True, if computed checksum must be marked in bits array.
 * checksum_computed(out): True, if checksum is computed now.
 */
STATIC_INLINE int
dwb_slot_compute_checksum (THREAD_ENTRY * thread_p, DWB_SLOT * slot, bool mark_checksum_computed,
			   bool * checksum_computed)
{
  int error_code = NO_ERROR;
  TSC_TICKS start_tick, end_tick;
  TSCTIMEVAL tv_diff;
  UINT64 oldest_time;
  bool is_perf_tracking = false;

  assert (slot != NULL);
  *checksum_computed = false;
  if (!ATOMIC_CAS_32 (&slot->checksum_status, DWB_SLOT_CHECKSUM_NOT_COMPUTED, DWB_SLOT_CHECKSUM_COMPUTED))
    {
      /* Already computed */
      return NO_ERROR;
    }

  is_perf_tracking = perfmon_is_perf_tracking ();
  if (is_perf_tracking)
    {
      tsc_getticks (&start_tick);
    }

  error_code = fileio_set_page_checksum (thread_p, slot->io_page);
  if (error_code != NO_ERROR)
    {
      assert (false);
      /* Restore it. */
      ATOMIC_TAS_32 (&slot->checksum_status, DWB_SLOT_CHECKSUM_NOT_COMPUTED);
      return error_code;
    }

  if (is_perf_tracking)
    {
      tsc_getticks (&end_tick);
      tsc_elapsed_time_usec (&tv_diff, end_tick, start_tick);
      oldest_time = tv_diff.tv_sec * 1000000LL + tv_diff.tv_usec;
      if (oldest_time > 0)
	{
	  perfmon_add_stat (thread_p, PSTAT_DWB_PAGE_CHECKSUM_TIME_COUNTERS, oldest_time);
	}
    }

  if (mark_checksum_computed)
    {
      dwb_mark_checksum_computed (thread_p, slot->block_no, slot->position_in_block);
    }

  *checksum_computed = true;
  return NO_ERROR;
}

/*
 * dwb_create_slots_hash () - Create slots hash.
 *
 * return   : Error code.
 * thread_p (in) : Thread entry.
 * p_slots_hash(out): The created hash.
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
 * dwb_finalize_slots_hash () - Finalize slots hash.
 *
 *   return: Nothing.
 *   slots_hash(in) : Slots hash.
 */
STATIC_INLINE void
dwb_finalize_slots_hash (DWB_SLOTS_HASH * slots_hash)
{
  assert (slots_hash != NULL);

  lf_hash_destroy (&slots_hash->ht);
  lf_freelist_destroy (&slots_hash->freelist);
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

  *p_blocks = NULL;

  for (i = 0; i < num_blocks; i++)
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

      slots[i] = (DWB_SLOT *) malloc (num_block_pages * sizeof (DWB_SLOT));
      if (slots[i] == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, num_block_pages * sizeof (DWB_SLOT));
	  error_code = ER_OUT_OF_VIRTUAL_MEMORY;
	  goto exit_on_error;
	}
      memset (slots[i], 0, num_block_pages * sizeof (DWB_SLOT));

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

	  fileio_initialize_res (thread_p, &io_page->prv);
	  dwb_intialize_slot (&slots[i][j], io_page, j, i);
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
  DWB_CHECKSUM_INFO *checksum_info = NULL;
  DWB_SLOTS_HASH *slots_hash = NULL;
  UINT64 new_position_with_flags;

  assert (dwb_volume_name != NULL && current_position_with_flags != NULL);

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

  double_Write_Buffer.blocks = blocks;
  double_Write_Buffer.num_blocks = num_blocks;
  double_Write_Buffer.num_pages = num_pages;
  double_Write_Buffer.num_block_pages = num_block_pages;
  double_Write_Buffer.log2_num_block_pages = (unsigned int) (log ((float) num_block_pages) / log ((float) 2));
  double_Write_Buffer.blocks_flush_counter = 0;
  double_Write_Buffer.next_block_to_flush = 0;
  double_Write_Buffer.checksum_info = checksum_info;
  pthread_mutex_init (&double_Write_Buffer.mutex, NULL);
  dwb_init_wait_queue (&double_Write_Buffer.wait_queue);
  double_Write_Buffer.slots_hash = slots_hash;
  double_Write_Buffer.vdes = vdes;
  double_Write_Buffer.helper_flush_block = NULL;

  /* Set creation flag. */
  new_position_with_flags = DWB_RESET_POSITION (*current_position_with_flags);
  new_position_with_flags = DWB_STARTS_CREATION (new_position_with_flags);
  if (!ATOMIC_CAS_64 (&double_Write_Buffer.position_with_flags, *current_position_with_flags, new_position_with_flags))
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

  if (checksum_info != NULL)
    {
      dwb_finalize_checksum_info (checksum_info);
      free_and_init (checksum_info);
    }

  if (slots_hash != NULL)
    {
      dwb_finalize_slots_hash (slots_hash);
      free_and_init (slots_hash);
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
 * logtb_global_unique_stat_key_copy () - Copy a slots hash key.
 *   returns: Error code.
 *   src(in): The source.
 *   dest(in): The destination.
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
 * btree_compare_btids () - Compare slots hash keys.
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
dwb_slots_hash_insert (THREAD_ENTRY * thread_p, VPID * vpid, DWB_SLOT * slot, int *inserted)
{
  int error_code = NO_ERROR;
  DWB_SLOTS_HASH_ENTRY *slots_hash_entry = NULL;
  LF_TRAN_ENTRY *t_entry = thread_get_tran_entry (thread_p, THREAD_TS_DWB_SLOTS);

  assert (vpid != NULL && slot != NULL && inserted != NULL);
  error_code =
    lf_hash_find_or_insert (t_entry, &double_Write_Buffer.slots_hash->ht, vpid, (void **) &slots_hash_entry, inserted);
  if (error_code != NO_ERROR || slots_hash_entry == NULL)
    {
      ASSERT_ERROR ();
      if (prm_get_bool_value (PRM_ID_DWB_ENABLE_LOG))
	{
	  _er_log_debug (ARG_FILE_LINE, "DWB hash find/insert key (%d, %d) with %d error: \n", vpid->volid,
			 vpid->pageid, error_code);
	}
      return error_code;
    }
  assert (VPID_EQ (&slots_hash_entry->vpid, &slot->vpid));
  assert (slots_hash_entry->vpid.pageid == slot->io_page->prv.pageid
	  && slots_hash_entry->vpid.volid == slot->io_page->prv.volid);
  if (!(*inserted))
    {
      assert (slots_hash_entry->slot != NULL);
      if (LSA_LE (&slot->lsa, &slots_hash_entry->slot->lsa))
	{
	  if (prm_get_bool_value (PRM_ID_DWB_ENABLE_LOG))
	    {
	      _er_log_debug (ARG_FILE_LINE, "DWB hash find key (%d, %d), the LSA=(%lld,%d), better than (%lld,%d): \n",
			     vpid->volid, vpid->pageid, slots_hash_entry->slot->lsa.pageid,
			     slots_hash_entry->slot->lsa.offset, slot->lsa.pageid, slot->lsa.offset);
	    }

	  /* The older slot is same or better than mine - leave it in hash. */
	  pthread_mutex_unlock (&slots_hash_entry->mutex);
	  return NO_ERROR;
	}

      if (prm_get_bool_value (PRM_ID_DWB_ENABLE_LOG))
	{
	  _er_log_debug (ARG_FILE_LINE, "Replace hash key (%d, %d), the new LSA=(%lld,%d), the old LSA = (%lld,%d)",
			 vpid->volid, vpid->pageid, slot->lsa.pageid, slot->lsa.offset,
			 slots_hash_entry->slot->lsa.pageid, slots_hash_entry->slot->lsa.offset);
	}
    }
  else if (prm_get_bool_value (PRM_ID_DWB_ENABLE_LOG))
    {
      _er_log_debug (ARG_FILE_LINE, "Inserted hash key (%d, %d), LSA=(%lld,%d)",
		     vpid->volid, vpid->pageid, slot->lsa.pageid, slot->lsa.offset);
    }

  slots_hash_entry->slot = slot;
  pthread_mutex_unlock (&slots_hash_entry->mutex);
  *inserted = 1;

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
  dwb_destroy_wait_queue (&double_Write_Buffer.wait_queue, &double_Write_Buffer.mutex);
  pthread_mutex_destroy (&double_Write_Buffer.mutex);

  if (double_Write_Buffer.blocks != NULL)
    {
      for (block_no = 0; block_no < DWB_NUM_TOTAL_BLOCKS; block_no++)
	{
	  dwb_finalize_block (&double_Write_Buffer.blocks[block_no]);
	}
      free_and_init (double_Write_Buffer.blocks);
    }

  if (double_Write_Buffer.checksum_info)
    {
      dwb_finalize_checksum_info (double_Write_Buffer.checksum_info);
      free_and_init (double_Write_Buffer.checksum_info);
    }

  if (double_Write_Buffer.slots_hash)
    {
      dwb_finalize_slots_hash (double_Write_Buffer.slots_hash);
      free_and_init (double_Write_Buffer.slots_hash);
    }

  if (double_Write_Buffer.vdes != NULL_VOLDES)
    {
      fileio_dismount (thread_p, double_Write_Buffer.vdes);
      fileio_unformat (thread_p, dwb_Volume_name);
    }

  /* Set creation flag. */
  new_position_with_flags = DWB_RESET_POSITION (*current_position_with_flags);
  new_position_with_flags = DWB_ENDS_CREATION (new_position_with_flags);
  if (!ATOMIC_CAS_64 (&double_Write_Buffer.position_with_flags, *current_position_with_flags, new_position_with_flags))
    {
      /* Impossible. */
      assert (false);
    }
  *current_position_with_flags = new_position_with_flags;
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
  TSC_TICKS start_tick, end_tick;
  TSCTIMEVAL tv_diff;
  UINT64 oldest_time;
  bool is_perf_tracking = false;

  assert (thread_p != NULL && block_no < DWB_NUM_TOTAL_BLOCKS);

  is_perf_tracking = perfmon_is_perf_tracking ();
  if (is_perf_tracking)
    {
      tsc_getticks (&start_tick);
    }

  dwb_block = &double_Write_Buffer.blocks[block_no];
  (void) pthread_mutex_lock (&dwb_block->mutex);

  thread_lock_entry (thread_p);

  /* Check again after acquiring mutexes. */
  if (!DWB_IS_BLOCK_WRITE_STARTED (ATOMIC_INC_64 (&double_Write_Buffer.position_with_flags, 0ULL), block_no))
    {
      thread_unlock_entry (thread_p);
      pthread_mutex_unlock (&dwb_block->mutex);
      if (is_perf_tracking)
	{
	  tsc_getticks (&end_tick);
	  tsc_elapsed_time_usec (&tv_diff, end_tick, start_tick);
	  oldest_time = tv_diff.tv_sec * 1000000LL + tv_diff.tv_usec;
	  if (oldest_time > 0)
	    {
	      perfmon_add_stat (thread_p, PSTAT_DWB_WAIT_FLUSH_BLOCK_TIME_COUNTERS, oldest_time);
	    }
	}

      return NO_ERROR;
    }

  double_write_queue_entry = dwb_block_add_wait_queue_entry (&dwb_block->wait_queue, thread_p);
  if (double_write_queue_entry)
    {
      int r;
      struct timespec to;

      pthread_mutex_unlock (&dwb_block->mutex);
      to.tv_sec = (int) time (NULL) + 20;
      to.tv_nsec = 0;

      r = thread_suspend_timeout_wakeup_and_unlock_entry (thread_p, &to, THREAD_DWB_QUEUE_SUSPENDED);

      if (is_perf_tracking)
	{
	  tsc_getticks (&end_tick);
	  tsc_elapsed_time_usec (&tv_diff, end_tick, start_tick);
	  oldest_time = tv_diff.tv_sec * 1000000LL + tv_diff.tv_usec;
	  if (oldest_time > 0)
	    {
	      perfmon_add_stat (thread_p, PSTAT_DWB_WAIT_FLUSH_BLOCK_TIME_COUNTERS, oldest_time);
	    }
	}

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
	  return ER_INTERRUPTED;
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
  return error_code;
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
#endif /* SERVER_MODE */

  return NO_ERROR;
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
  if ((DWB_WAIT_QUEUE_ENTRY volatile *) (dwb_block->wait_queue.head) != NULL)
    {
      /* There are blocked threads. Destroy the wait queue and release the blocked threads. */
      dwb_signal_waiting_threads (&dwb_block->wait_queue, &dwb_block->mutex);
    }
}

/*
 * dwb_signal_block_completion () - Signal DWB structure changed.
 *
 * return   : Nothing.
 * thread_p (in): The thread entry.
 */
STATIC_INLINE void
dwb_signal_structure_modificated (THREAD_ENTRY * thread_p)
{
  if ((DWB_WAIT_QUEUE_ENTRY volatile *) (double_Write_Buffer.wait_queue.head) != NULL)
    {
      /* There are blocked threads. Destroy the wait queue and release the blocked threads. */
      dwb_signal_waiting_threads (&double_Write_Buffer.wait_queue, &double_Write_Buffer.mutex);
    }
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
  int error_code = NO_ERROR;
#if defined (SERVER_MODE)

  DWB_WAIT_QUEUE_ENTRY *double_write_queue_entry = NULL;
  UINT64 current_position_with_flags;

  (void) pthread_mutex_lock (&double_Write_Buffer.mutex);

  /* Check the actual flags, to avoids unnecessary waits. */
  current_position_with_flags = ATOMIC_INC_64 (&double_Write_Buffer.position_with_flags, 0ULL);
  if (DWB_IS_MODIFYING_STRUCTURE (current_position_with_flags))
    {
      thread_lock_entry (thread_p);

      double_write_queue_entry = dwb_block_add_wait_queue_entry (&double_Write_Buffer.wait_queue, thread_p);
      if (double_write_queue_entry)
	{
	  int r;
	  struct timespec to;

	  pthread_mutex_unlock (&double_Write_Buffer.mutex);
	  to.tv_sec = (int) time (NULL) + 10;
	  to.tv_nsec = 0;

	  r = thread_suspend_timeout_wakeup_and_unlock_entry (thread_p, &to, THREAD_DWB_QUEUE_SUSPENDED);
	  if (r == ER_CSS_PTHREAD_COND_TIMEDOUT)
	    {
	      /* timeout, remove the entry from queue */
	      dwb_remove_wait_queue_entry (&double_Write_Buffer.wait_queue, &double_Write_Buffer.mutex, thread_p, NULL);
	      return r;
	    }
	  else if (thread_p->resume_status != THREAD_DWB_QUEUE_RESUMED)
	    {
	      /* interruption, remove the entry from queue */
	      assert (thread_p->resume_status == THREAD_RESUME_DUE_TO_INTERRUPT
		      || thread_p->resume_status == THREAD_RESUME_DUE_TO_SHUTDOWN);

	      dwb_remove_wait_queue_entry (&double_Write_Buffer.wait_queue, &double_Write_Buffer.mutex, thread_p, NULL);
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INTERRUPTED, 0);
	      return ER_INTERRUPTED;
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
    }

  pthread_mutex_unlock (&double_Write_Buffer.mutex);
#endif /* SERVER_MODE */
  return error_code;
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
  LF_TRAN_ENTRY *t_entry = thread_get_tran_entry (thread_p, THREAD_TS_DWB_SLOTS);
  int success;
  VPID *vpid;
  DWB_SLOTS_HASH_ENTRY *slots_hash_entry = NULL;

  vpid = &slot->vpid;
  if (VPID_ISNULL (vpid))
    {
      /* Nothing to do, the slot is not in hash. */
      return NO_ERROR;
    }
  /* Remove the old vpid from hash. */
  error_code = lf_hash_find (t_entry, &double_Write_Buffer.slots_hash->ht, (void *) vpid, (void **) &slots_hash_entry);
  if (error_code != NO_ERROR)
    {
      /* Should not happen. */
      ASSERT_ERROR ();
      if (prm_get_bool_value (PRM_ID_DWB_ENABLE_LOG))
	{
	  _er_log_debug (ARG_FILE_LINE, "DWB hash find key (%d, %d) with %d error: \n", vpid->volid,
			 vpid->pageid, error_code);
	}
      return error_code;
    }

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

      error_code = lf_hash_delete_already_locked (t_entry, &double_Write_Buffer.slots_hash->ht, vpid,
						  slots_hash_entry, &success);
      if (error_code != NO_ERROR || !success)
	{
	  assert_release (false);
	  /* Should not happen. */
	  pthread_mutex_unlock (&slots_hash_entry->mutex);
	  if (prm_get_bool_value (PRM_ID_DWB_ENABLE_LOG))
	    {
	      _er_log_debug (ARG_FILE_LINE, "DWB hash delete key (%d, %d) with %d error: \n", vpid->volid,
			     vpid->pageid, error_code);
	    }
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
 * return   : Error code.
 * thread_p (in): The thread entry.
 * block(in): The block where the flush area reside.
 * vol_fd(in): The volume to add.
 * flush_volume_info(out): Information about the volumes to flush.
 *
 *  Note: The volume is added if is not already in block flush area.
 */
STATIC_INLINE void
dwb_add_volume_to_block_flush_area (THREAD_ENTRY * thread_p, DWB_BLOCK * block, int vol_fd,
				    FLUSH_VOLUME_INFO ** flush_volume_info)
{
  assert (flush_volume_info != NULL);
  *flush_volume_info = NULL;

  if (vol_fd != NULL_VOLDES)
    {
      FLUSH_VOLUME_INFO *flush_new_volume_info;

      flush_new_volume_info = &block->flush_volumes_info[block->count_flush_volumes_info];
      flush_new_volume_info->vdes = vol_fd;
      flush_new_volume_info->num_pages = 0;

      ATOMIC_INC_32 (&block->count_flush_volumes_info, 1);
      assert (block->count_flush_volumes_info < block->max_to_flush_vdes);

      *flush_volume_info = flush_new_volume_info;
    }
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
 *
 *  Note: This function fills to_flush_vdes array with the volumes that must be flushed.
 */
STATIC_INLINE int
dwb_write_block (THREAD_ENTRY * thread_p, DWB_BLOCK * block, DWB_SLOT * p_dwb_ordered_slots,
		 unsigned int ordered_slots_length, bool remove_from_hash)
{
  INT16 volid;
  unsigned int i;
  int vol_fd;
  VPID *vpid;
  int error_code = NO_ERROR;
  TSC_TICKS start_tick, end_tick;
  TSCTIMEVAL tv_diff;
  UINT64 oldest_time;
  bool is_perf_tracking = false;
  int count_writes = 0;
  FLUSH_VOLUME_INFO *current_flush_volume_info = NULL;

  assert (block != NULL && p_dwb_ordered_slots != NULL);

  /*
   * Write the whole slots data first and then remove it from hash. Is better to do in this way. Thus, the fileio_write
   * may be slow. While the current transaction has delays caused by fileio_write, the concurrent transaction still
   * can access the data from memory instead disk
   */
  volid = NULL_VOLID;
  assert (block->count_wb_pages < ordered_slots_length);
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
	  /* Get the volume descriptor. */
	  volid = vpid->volid;
	  vol_fd = fileio_get_volume_descriptor (volid);
	  dwb_add_volume_to_block_flush_area (thread_p, block, vol_fd, &current_flush_volume_info);
	}

      /* Check whether the volume was removed meanwhile. */
      if (vol_fd != NULL_VOLDES)
	{
#if 1
	  assert (p_dwb_ordered_slots[i].io_page->prv.pflag_reserve_1 == '\0');
	  assert (p_dwb_ordered_slots[i].io_page->prv.p_reserve_2 == 0);
	  assert (p_dwb_ordered_slots[i].io_page->prv.p_reserve_3 == 0);
	  assert (p_dwb_ordered_slots[i].vpid.pageid == p_dwb_ordered_slots[i].io_page->prv.pageid
		  && p_dwb_ordered_slots[i].vpid.volid == p_dwb_ordered_slots[i].io_page->prv.volid);
#endif

	  /* Write the data. */
	  if (fileio_write (thread_p, vol_fd, p_dwb_ordered_slots[i].io_page, vpid->pageid, IO_PAGESIZE, true) == NULL)
	    {
	      if (prm_get_bool_value (PRM_ID_DWB_ENABLE_LOG))
		{
		  _er_log_debug (ARG_FILE_LINE, "DWB write page VPID=(%d, %d) LSA=(%lld,%d) with %d error: \n",
				 vpid->volid, vpid->pageid, p_dwb_ordered_slots[i].io_page->prv.lsa.pageid,
				 (int) p_dwb_ordered_slots[i].io_page->prv.lsa.offset, er_errid ());
		}
	      assert (false);
	      /* Something wrong happened. */
	      return ER_FAILED;
	    }

#if defined (SERVER_MODE)
	  assert (current_flush_volume_info != NULL);
	  ATOMIC_INC_32 (&current_flush_volume_info->num_pages, 1);
	  count_writes++;
	  /* TODO - use parameter */
	  if (count_writes >= 200
	      && prm_get_integer_value (PRM_ID_DWB_CHECKSUM_THREADS) == 0
	      && thread_is_dwb_checksum_computation_thread_available ())
	    {
	      if (double_Write_Buffer.helper_flush_block == NULL)
		{
		  double_Write_Buffer.helper_flush_block = block;
		}

	      thread_wakeup_dwb_checksum_computation_thread ();
	      count_writes = 0;
	    }
#endif
	}
    }

  /* Remove the corresponding entries from hash. */
  is_perf_tracking = perfmon_is_perf_tracking ();
  if (is_perf_tracking)
    {
      tsc_getticks (&start_tick);
    }

  if (remove_from_hash)
    {
      for (i = 0; i < block->count_wb_pages; i++)
	{
	  vpid = &p_dwb_ordered_slots[i].vpid;
	  if (VPID_ISNULL (vpid))
	    {
	      continue;
	    }

	  error_code = dwb_slots_hash_delete (thread_p, &block->slots[p_dwb_ordered_slots[i].position_in_block]);
	  if (error_code != NO_ERROR)
	    {
	      return error_code;
	    }
	}
    }

  if (is_perf_tracking)
    {
      tsc_getticks (&end_tick);
      tsc_elapsed_time_usec (&tv_diff, end_tick, start_tick);
      oldest_time = tv_diff.tv_sec * 1000000LL + tv_diff.tv_usec;
      if (oldest_time > 0)
	{
	  perfmon_add_stat (thread_p, PSTAT_DWB_FLUSH_BLOCK_REMOVE_HASH_ENTRIES, oldest_time);
	}
    }

  return NO_ERROR;
}

/*
 * dwb_flush_block () - Flush pages from specified block.
 *
 * return   : Error code.
 * thread_p (in): Thread entry.
 * block(in): The block that needs flush.
 * current_position_with_flags(out): Current position with flags.
 *
 *  Note: The block pages can't be modified by others during flush.
 */
STATIC_INLINE int
dwb_flush_block (THREAD_ENTRY * thread_p, DWB_BLOCK * block, UINT64 * current_position_with_flags)
{
  UINT64 local_current_position_with_flags, new_position_with_flags;
  int error_code = NO_ERROR;
  DWB_SLOT *p_dwb_ordered_slots = NULL;
  unsigned int i, ordered_slots_length;
  unsigned int block_checksum_element_position, block_checksum_start_position, element_position;
  TSC_TICKS start_tick, end_tick;
  TSCTIMEVAL tv_diff;
  UINT64 oldest_time;
  bool is_perf_tracking = false;
  int num_pages;

  assert (block != NULL && block->count_wb_pages > 0 && dwb_is_created ());

  is_perf_tracking = perfmon_is_perf_tracking ();
  if (is_perf_tracking)
    {
      tsc_getticks (&start_tick);
    }

  /* Currently we allow only one block to be flushed. */
  ATOMIC_INC_32 (&double_Write_Buffer.blocks_flush_counter, 1);
  assert (double_Write_Buffer.blocks_flush_counter <= 1);

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
#if 1
      assert (p_dwb_ordered_slots[i].io_page->prv.pflag_reserve_1 == '\0');
      assert (p_dwb_ordered_slots[i].io_page->prv.p_reserve_2 == 0);
      assert (p_dwb_ordered_slots[i].io_page->prv.p_reserve_3 == 0);
#endif

      if (VPID_EQ (&p_dwb_ordered_slots[i].vpid, &p_dwb_ordered_slots[i + 1].vpid)
	  && !VPID_ISNULL (&p_dwb_ordered_slots[i].vpid))
	{
	  /* Next slot contains the same page, but that page is newer than the current one. Invalidate the VPID to
	   * avoid flushing the page twice. I'm sure that the current slot is not in hash.
	   */
	  assert (LSA_LE (&p_dwb_ordered_slots[i].lsa, &p_dwb_ordered_slots[i + 1].lsa));
	  VPID_SET_NULL (&p_dwb_ordered_slots[i].vpid);
	  VPID_SET_NULL (&(block->slots[p_dwb_ordered_slots[i].position_in_block].vpid));
	  fileio_initialize_res (thread_p, &(p_dwb_ordered_slots[i].io_page->prv));
	}
    }

  if (is_perf_tracking)
    {
      tsc_getticks (&end_tick);
      tsc_elapsed_time_usec (&tv_diff, end_tick, start_tick);
      oldest_time = tv_diff.tv_sec * 1000000LL + tv_diff.tv_usec;
      if (oldest_time > 0)
	{
	  perfmon_add_stat (thread_p, PSTAT_DWB_FLUSH_BLOCK_SORT_TIME_COUNTERS, oldest_time);
	}
    }

#if defined (SERVER_MODE)
retry:
  if (double_Write_Buffer.helper_flush_block != NULL)
    {
      /* Be sure that the previous block was written on disk, before writing the the current block. */
      if (thread_is_dwb_flush_block_helper_thread_available ())
	{
	  thread_sleep (10);
	  goto retry;
	}
      else
	{
	  /* Helper not available, flush the volumes from previous block. */
	  for (i = 0; i < double_Write_Buffer.helper_flush_block->count_flush_volumes_info; i++)
	    {
	      assert (double_Write_Buffer.helper_flush_block->flush_volumes_info[i].vdes != NULL_VOLDES);
	      if (ATOMIC_INC_32 (&(double_Write_Buffer.helper_flush_block->flush_volumes_info[i].num_pages), 0) >= 0)
		{
		  (void) fileio_synchronize (thread_p,
					     double_Write_Buffer.helper_flush_block->flush_volumes_info[i].vdes, NULL,
					     false);
		}
	    }
	  double_Write_Buffer.helper_flush_block = NULL;
	}
    }
#endif

  /* First, write and flush the double write file buffer. */
  if (fileio_write_pages (thread_p, double_Write_Buffer.vdes, block->write_buffer, 0, block->count_wb_pages,
			  IO_PAGESIZE, true) == NULL)
    {
      /* Something wrong happened. */
      assert (false);
      error_code = ER_FAILED;
      goto end;
    }

  if (fileio_synchronize (thread_p, double_Write_Buffer.vdes, dwb_Volume_name, false) != double_Write_Buffer.vdes)
    {
      assert (false);
      /* Something wrong happened. */
      error_code = ER_FAILED;
      goto end;
    }

  /* Now, write and flush the original location. */
  error_code = dwb_write_block (thread_p, block, p_dwb_ordered_slots, ordered_slots_length, true);
  if (error_code != NO_ERROR)
    {
      assert (false);
      goto end;
    }

  /* Now, flush only the volumes having pages in current block. */
  for (i = 0; i < block->count_flush_volumes_info; i++)
    {
      assert (block->flush_volumes_info[i].vdes != NULL_VOLDES);
      num_pages = ATOMIC_TAS_32 (&block->flush_volumes_info[i].num_pages, 0);

      if (num_pages == 0)
	{
	  /* Flushed by helper. */
	  continue;
	}

      (void) fileio_synchronize (thread_p, block->flush_volumes_info[i].vdes, NULL, false);
    }

  if (perfmon_is_perf_tracking_and_active (PERFMON_ACTIVE_FLUSHED_BLOCK_VOLUMES))
    {
      perfmon_db_flushed_block_volumes (thread_p, block->count_flush_volumes_info);
    }
  block->count_flush_volumes_info = 0;

  if (prm_get_integer_value (PRM_ID_DWB_CHECKSUM_THREADS) > 0)
    {
      block_checksum_start_position = DWB_BLOCK_GET_CHECKSUM_START_POSITION (block->block_no);
      for (block_checksum_element_position = 0; block_checksum_element_position < DWB_CHECKSUM_NUM_ELEMENTS_IN_BLOCK;
	   block_checksum_element_position++)
	{
	  element_position = block_checksum_start_position + block_checksum_element_position;
	  DWB_RESET_REQUESTED_CHECKSUMS_ELEMENT (element_position);
	  DWB_RESET_COMPUTED_CHECKSUMS_ELEMENT (element_position);
	  DWB_RESET_POSITION_IN_CHECKSUMS_ELEMENT (element_position);
	}
    }

#if defined (SERVER_MODE)
  assert (block->count_wb_pages == DWB_BLOCK_NUM_PAGES);
#endif
  ATOMIC_TAS_32 (&block->count_wb_pages, 0);
  ATOMIC_INC_64 (&block->version, 1ULL);

  /* Reset block bit, since the block was flushed. */
reset_bit_position:
  local_current_position_with_flags = ATOMIC_INC_64 (&double_Write_Buffer.position_with_flags, 0LL);
  new_position_with_flags = DWB_ENDS_BLOCK_WRITING (local_current_position_with_flags, block->block_no);
  if (!ATOMIC_CAS_64 (&double_Write_Buffer.position_with_flags, local_current_position_with_flags,
		      new_position_with_flags))
    {
      /* The position was changed by others, try again. */
      goto reset_bit_position;
    }

  /* Advance flushing to next block. */
  double_Write_Buffer.next_block_to_flush = DWB_GET_NEXT_BLOCK_NO (double_Write_Buffer.next_block_to_flush);

  /* Release locked threads, if any. */
  dwb_signal_block_completion (thread_p, block);
  if (current_position_with_flags)
    {
      *current_position_with_flags = new_position_with_flags;
    }

end:
  ATOMIC_INC_32 (&double_Write_Buffer.blocks_flush_counter, -1);
  if (p_dwb_ordered_slots != NULL)
    {
      free_and_init (p_dwb_ordered_slots);
    }

  if (is_perf_tracking)
    {
      tsc_getticks (&end_tick);
      tsc_elapsed_time_usec (&tv_diff, end_tick, start_tick);
      oldest_time = tv_diff.tv_sec * 1000000LL + tv_diff.tv_usec;
      if (oldest_time > 0)
	{
	  perfmon_add_stat (thread_p, PSTAT_DWB_FLUSH_BLOCK_TIME_COUNTERS, oldest_time);
	}
    }
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
  current_position_with_flags = ATOMIC_INC_64 (&double_Write_Buffer.position_with_flags, 0ULL);
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

	  if (prm_get_bool_value (PRM_ID_DWB_ENABLE_LOG))
	    {
	      _er_log_debug (ARG_FILE_LINE, "Waits for flushing block=%d having version=%lld) \n",
			     current_block_no, double_Write_Buffer.blocks[current_block_no].version);
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

	      if (prm_get_bool_value (PRM_ID_DWB_ENABLE_LOG))
		{
		  _er_log_debug (ARG_FILE_LINE, "Error %d while waiting for flushing block=%d having version %lld \n",
				 error_code, current_block_no, double_Write_Buffer.blocks[current_block_no].version);
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
      assert (DWB_IS_CREATED (double_Write_Buffer.position_with_flags));
      assert (!DWB_IS_MODIFYING_STRUCTURE (double_Write_Buffer.position_with_flags));

      /* Compute the next position with flags */
      new_position_with_flags = DWB_GET_NEXT_POSITION_WITH_FLAGS (current_position_with_flags);
    }

  /* Compute and advance the global position in double write buffer. */
  if (!ATOMIC_CAS_64 (&double_Write_Buffer.position_with_flags, current_position_with_flags, new_position_with_flags))
    {
      /* Someone else advanced the global position in double write buffer, try again. */
      goto start;
    }

  block = double_Write_Buffer.blocks + current_block_no;
  *p_dwb_slot = block->slots + position_in_current_block;
  /* Invalidate slot content. */
  VPID_SET_NULL (&(*p_dwb_slot)->vpid);
  assert ((*p_dwb_slot)->position_in_block == position_in_current_block);
  ATOMIC_TAS_32 (&(*p_dwb_slot)->checksum_status, DWB_SLOT_CHECKSUM_NOT_COMPUTED);

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

#if 1
  assert (io_page_p->prv.pflag_reserve_1 == '\0');
  assert (io_page_p->prv.p_reserve_2 == 0);
  assert (io_page_p->prv.p_reserve_3 == 0);
#endif

  if (io_page_p->prv.pageid != NULL_PAGEID)
    {
      memcpy (dwb_slot->io_page, (char *) io_page_p, IO_PAGESIZE);
    }
  else
    {
      /* Initialize page for consistency. */
      fileio_initialize_res (thread_p, &(dwb_slot->io_page->prv));
    }

  LSA_COPY (&dwb_slot->lsa, &io_page_p->prv.lsa);
  VPID_SET (&dwb_slot->vpid, io_page_p->prv.volid, io_page_p->prv.pageid);
}

/*
 * dwb_init_slot () - Intialize DWB slot.
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
 *   returns: Nothing
 * thread_p (in): The thread entry.
 * block_no(out): The next block for flush if found, otherwise DWB_NUM_TOTAL_BLOCKS.
 */
STATIC_INLINE void
dwb_get_next_block_for_flush (THREAD_ENTRY * thread_p, unsigned int *block_no)
{
  unsigned int block_checksum_start_position, block_checksum_element_position, element_position;
  bool found;

  assert (block_no != NULL);

  *block_no = DWB_NUM_TOTAL_BLOCKS;
  /* First, check whether the next block can be flushed. Then check whether its whole slots checksum were computed. */
  if (double_Write_Buffer.blocks[double_Write_Buffer.next_block_to_flush].count_wb_pages != DWB_BLOCK_NUM_PAGES)
    {
      /* Next block is not full yet. */
      return;
    }

  found = true;
  if (prm_get_integer_value (PRM_ID_DWB_CHECKSUM_THREADS) > 0)
    {
      block_checksum_start_position = DWB_BLOCK_GET_CHECKSUM_START_POSITION (double_Write_Buffer.next_block_to_flush);
      for (block_checksum_element_position = 0; block_checksum_element_position < DWB_CHECKSUM_NUM_ELEMENTS_IN_BLOCK;
	   block_checksum_element_position++)
	{
	  element_position = block_checksum_start_position + block_checksum_element_position;
	  if (DWB_GET_COMPUTED_CHECKSUMS_ELEMENT (element_position)
	      != DWB_GET_COMPLETED_CHECKSUMS_ELEMENT (block_checksum_element_position))
	    {
	      /* Needs to compute all checksums before flush. */
	      found = false;
	      break;
	    }
	}
    }

  if (found)
    {
      *block_no = double_Write_Buffer.next_block_to_flush;
    }
}

/*
 * dwb_block_has_all_checksums_computed(): Checks whether the block has all checksum computed.
 *
 *   returns: True, if all block checksums computed. 
 * block_no(out): The block number.
 */
STATIC_INLINE bool
dwb_block_has_all_checksums_computed (unsigned int block_no)
{
  unsigned int block_checksum_start_position, block_checksum_element_position, element_position;
  bool found;

  /* First, search for blocks that must be flushed, to avoid delays caused by checksum computation in other block. */
  found = true;
  block_checksum_start_position = DWB_BLOCK_GET_CHECKSUM_START_POSITION (block_no);
  for (block_checksum_element_position = 0; block_checksum_element_position < DWB_CHECKSUM_NUM_ELEMENTS_IN_BLOCK;
       block_checksum_element_position++)
    {
      element_position = block_checksum_start_position + block_checksum_element_position;
      if (DWB_GET_COMPUTED_CHECKSUMS_ELEMENT (element_position)
	  != DWB_GET_COMPLETED_CHECKSUMS_ELEMENT (block_checksum_element_position))
	{
	  found = false;
	  break;
	}
    }

  return found;
}

/*
 * dwb_compute_block_checksums(): Computes checksums for requested slots in specified block.
 *
 *   returns: Error code.
 * thread_p (in): The thread entry.
 * block(int): The DWB block.
 * block_slots_checksum_computed (out); True, if checksums of some slots in block are computed.
 * block_needs_flush (out): True, if block needs flush - checksum of all slots computed.
 */
STATIC_INLINE int
dwb_compute_block_checksums (THREAD_ENTRY * thread_p, DWB_BLOCK * block, bool * block_slots_checksum_computed,
			     bool * block_needs_flush)
{
  UINT64 computed_slots_checksum_elem, requested_checksums_elem, bit_mask, computed_checksum_bits;
  int error_code = NO_ERROR, position_in_checksum_element, position;
  unsigned int block_checksum_start_position, block_checksum_element_position, element_position, slot_position,
    slot_base;
  bool checksum_computed, slots_checksum_computed, all_slots_checksum_computed;
  UINT64 block_version;

  assert (block != NULL);

  slots_checksum_computed = false;
  all_slots_checksum_computed = true;
  block_checksum_start_position = DWB_BLOCK_GET_CHECKSUM_START_POSITION (block->block_no);
  block_version = DWB_GET_BLOCK_VERSION (block);
  for (block_checksum_element_position = 0; block_checksum_element_position < DWB_CHECKSUM_NUM_ELEMENTS_IN_BLOCK;
       block_checksum_element_position++)
    {
      element_position = block_checksum_start_position + block_checksum_element_position;
      while (true)
	{
	  requested_checksums_elem = DWB_GET_REQUESTED_CHECKSUMS_ELEMENT (element_position);
	  computed_slots_checksum_elem = DWB_GET_COMPUTED_CHECKSUMS_ELEMENT (element_position);

	  if (requested_checksums_elem == 0ULL || requested_checksums_elem == computed_slots_checksum_elem)
	    {
	      /* There are no other slots available for checksum computation. */
	      break;
	    }

	  /* The checksum bits modified meanwhile, we needs to compute new checksum. */
	  position_in_checksum_element = DWB_GET_POSITION_IN_CHECKSUMS_ELEMENT (element_position);
	  if (position_in_checksum_element >= DWB_CHECKSUM_ELEMENT_NO_BITS)
	    {
	      break;
	    }

	  slot_base = block_checksum_element_position * DWB_CHECKSUM_ELEMENT_NO_BITS;
	  bit_mask = 1ULL << position_in_checksum_element;
	  computed_checksum_bits = 0;
	  for (position = position_in_checksum_element; position < DWB_CHECKSUM_ELEMENT_NO_BITS; position++)
	    {
	      if ((requested_checksums_elem & bit_mask) == 0)
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
		  if (prm_get_bool_value (PRM_ID_DWB_ENABLE_LOG))
		    {
		      _er_log_debug (ARG_FILE_LINE, "DWB error: Can't compute checksum for slot %d in block %d\n",
				     block->slots[slot_position].position_in_block, block->block_no);
		    }
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
	      assert (DWB_IS_ADDED_TO_REQUESTED_CHECKSUMS_ELEMENT (element_position, computed_checksum_bits));

	      /* Update start bit position, if possible */
	      do
		{
		  position_in_checksum_element = DWB_GET_POSITION_IN_CHECKSUMS_ELEMENT (element_position);
		  if (position_in_checksum_element >= position)
		    {
		      /* Other transaction advanced before me, nothing to do. */
		      break;
		    }
		}
	      while (!ATOMIC_CAS_32 (&double_Write_Buffer.checksum_info->positions[element_position],
				     position_in_checksum_element, position));

	      assert (DWB_GET_BLOCK_VERSION (block) == block_version);
	      DWB_ADD_TO_COMPUTED_CHECKSUMS_ELEMENT (element_position, computed_checksum_bits);
	      if (prm_get_bool_value (PRM_ID_DWB_ENABLE_LOG))
		{
		  _er_log_debug (ARG_FILE_LINE, "Successfully computed checksums for slots %d in block %d\n",
				 computed_checksum_bits, block->block_no);
		}
	    }
	}

      if (DWB_GET_COMPUTED_CHECKSUMS_ELEMENT (element_position)
	  != DWB_GET_COMPLETED_CHECKSUMS_ELEMENT (block_checksum_element_position))
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
 *  Note: This thread may decide to compute checksum, in case that checksum thread remains behind.
 *        Also, this thread may flush the block, if flush thread is not available or we are in stand alone.
 */
int
dwb_add_page (THREAD_ENTRY * thread_p, FILEIO_PAGE * io_page_p, VPID * vpid, DWB_SLOT ** p_dwb_slot)
{
  unsigned int count_wb_pages;
  int error_code = NO_ERROR, inserted, retry_flush_iter = 0, retry_flush_max = 5, checksum_threads;
  DWB_BLOCK *block = NULL;
  bool checksum_computed;
  DWB_SLOT *dwb_slot = NULL;
  bool needs_flush;
#if defined (SERVER_MODE)
  bool checksum_computation_started;
#endif

  assert ((p_dwb_slot != NULL) && ((io_page_p != NULL) || ((*p_dwb_slot)->io_page != NULL)) && vpid != NULL);
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
	  fileio_initialize_res (thread_p, &(dwb_slot->io_page->prv));
	}
    }

  /* Reset checksum. */
  dwb_slot->io_page->prv.checksum = 0;

  block = &double_Write_Buffer.blocks[dwb_slot->block_no];
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

  checksum_threads = prm_get_integer_value (PRM_ID_DWB_CHECKSUM_THREADS);
  if (checksum_threads > 0)
    {
      dwb_add_checksum_computation_request (thread_p, block->block_no, dwb_slot->position_in_block);
#if defined (SERVER_MODE)
      checksum_computation_started = false;
      if (thread_is_dwb_checksum_computation_thread_available ())
	{
	  /* Wake up checksum thread to compute checksum. */
	  thread_wakeup_dwb_checksum_computation_thread ();
	  if (checksum_threads == 1)
	    {
	      if (needs_flush == false)
		{
		  return NO_ERROR;
		}
	      else
		{
		  goto start_flush_block;
		}
	    }
	  checksum_computation_started = true;
	}

      if ((checksum_computation_started == false) || (dwb_needs_speedup_checksum_computation (thread_p)))
#endif
	{
	  error_code = dwb_slot_compute_checksum (thread_p, dwb_slot, true, &checksum_computed);
	  if (error_code != NO_ERROR)
	    {
	      if (prm_get_bool_value (PRM_ID_DWB_ENABLE_LOG))
		{
		  _er_log_debug (ARG_FILE_LINE, "DWB error: Can't compute checksum for slot %d in block %d\n",
				 dwb_slot->position_in_block, dwb_slot->block_no);
		}
	      return error_code;
	    }

	  if (prm_get_bool_value (PRM_ID_DWB_ENABLE_LOG))
	    {
	      _er_log_debug (ARG_FILE_LINE, "Successfully computed checksums for slots %d in block %d\n",
			     dwb_slot->position_in_block, dwb_slot->block_no);
	    }
	}
    }

start_flush_block:
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
   * previous block and after all slots checksum of current block were computed by checksum threads.
   */
  if (thread_is_dwb_flush_block_thread_available ())
    {
      /* Wakeup the thread that will flush the block. */
      thread_wakeup_dwb_flush_block_thread ();
      return NO_ERROR;
    }

  /* This thread must flush the block, but has to wait for checksum computation. */
  if (checksum_threads > 0)
    {
    retry:
      if (!dwb_block_has_all_checksums_computed (block->block_no))
	{
	  /* Wait for checksum thread to finish. */
	  thread_sleep (10);
	  goto retry;
	}
    }
#endif /* SERVER_MODE */

  /* Flush all pages from current block */
  error_code = dwb_flush_block (thread_p, block, NULL);
  if (error_code != NO_ERROR)
    {
      /* Something wrong happened, sleep 10 msec and try again. */
      if (retry_flush_iter < retry_flush_max)
	{
#if defined(SERVER_MODE)
	  thread_sleep (10);
#endif
	  retry_flush_iter++;
	  goto start_flush_block;
	}

      if (prm_get_bool_value (PRM_ID_DWB_ENABLE_LOG))
	{
	  _er_log_debug (ARG_FILE_LINE, "DWB error: Can't flush block = %d having version %lld\n",
			 block->block_no, block->version);
	}

      return error_code;
    }

  if (prm_get_bool_value (PRM_ID_DWB_ENABLE_LOG))
    {
      _er_log_debug (ARG_FILE_LINE, "Successfully flushed DWB block = %d having version %lld\n",
		     block->block_no, block->version);
    }

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
  UINT64 position_with_flags = ATOMIC_INC_64 (&double_Write_Buffer.position_with_flags, 0ULL);
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

/*
 * dwb_load_and_recover () - Load and recover pages from DWB.
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
  unsigned int num_pages, ordered_slots_length, i;
  DWB_BLOCK *rcv_block = NULL;
  DWB_SLOT *p_dwb_ordered_slots = NULL;
  char page_buf[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT];
  FILEIO_PAGE *iopage;
  VPID *vpid;
  int vol_fd;
  INT16 volid;
  bool is_page_corrupted;

  assert (double_Write_Buffer.vdes == NULL_VOLDES);

  fileio_make_dwb_name (dwb_Volume_name, dwb_path_p, db_name_p);

  if (fileio_is_volume_exist (dwb_Volume_name))
    {
      /* Open DWB volume first */
      read_fd = fileio_mount (thread_p, boot_db_full_name (), dwb_Volume_name, LOG_DBDWB_VOLID, false, false);
      if (read_fd == NULL_VOLDES)
	{
	  return ER_IO_MOUNT_FAIL;
	}

      num_pages = fileio_get_number_of_volume_pages (read_fd, IO_PAGESIZE);

      assert (IS_POWER_OF_2 (num_pages));
      assert ((num_pages * IO_PAGESIZE) % (DWB_MIN_SIZE) == 0);

      /* Create DWB block for recovery purpose. */
      error_code = dwb_create_blocks (thread_p, 1, num_pages, &rcv_block);
      if (error_code != NO_ERROR)
	{
	  goto end;
	}

      /* Read pages in block write area. This means that slot pages are set. */
      if (fileio_read_pages (thread_p, read_fd, rcv_block->write_buffer, 0, num_pages, IO_PAGESIZE) == NULL)
	{
	  error_code = ER_FAILED;
	  goto end;
	}

      /* Set slots VPID and LSA from pages. */
      for (i = 0; i < num_pages; i++)
	{
	  iopage = rcv_block->slots[i].io_page;
	  VPID_SET (&rcv_block->slots[i].vpid, iopage->prv.volid, iopage->prv.pageid);
	  LSA_COPY (&rcv_block->slots[i].lsa, &iopage->prv.lsa);
	}
      rcv_block->count_wb_pages = num_pages;

      /* Order slots by VPID, to flush faster. */
      error_code = dwb_block_create_ordered_slots (rcv_block, &p_dwb_ordered_slots, &ordered_slots_length);
      if (error_code != NO_ERROR)
	{
	  error_code = ER_FAILED;
	  goto end;
	}

      volid = NULL_VOLID;
      iopage = (FILEIO_PAGE *) PTR_ALIGN (page_buf, MAX_ALIGNMENT);

      /* Check whether the data page is corrupted. If true, replaced with the DWB page. */
      for (i = 0; i < rcv_block->count_wb_pages; i++)
	{
	  vpid = &p_dwb_ordered_slots[i].vpid;
	  if (VPID_ISNULL (vpid))
	    {
	      continue;
	    }

	  assert (!VPID_EQ (vpid, &p_dwb_ordered_slots[i + 1].vpid));

	  if (volid != vpid->volid)
	    {
	      /* Update the current VPID and get the volume descriptor. */
	      volid = vpid->volid;
	      vol_fd = fileio_get_volume_descriptor (volid);
	    }

	  /* Read the page from data volume. */
	  if (fileio_read (thread_p, vol_fd, iopage, vpid->pageid, IO_PAGESIZE) == NULL)
	    {
	      /* There was an error in reading the page. */
	      ASSERT_ERROR ();
	      error_code = er_errid ();
	      goto end;
	    }

	  error_code = fileio_page_check_corruption (thread_p, iopage, &is_page_corrupted);
	  if (error_code != NO_ERROR)
	    {
	      /* Error in checksum computation. */
	      goto end;
	    }

	  if (!is_page_corrupted)
	    {
	      /* The page in data volume is not corrupted. Do not overwrite its content - reset slot VPID. */
	      VPID_SET_NULL (&p_dwb_ordered_slots[i].vpid);
	      fileio_initialize_res (thread_p, &(p_dwb_ordered_slots[i].io_page->prv));
	      continue;
	    }

	  /* Corrupted page in data volume. Check DWB. */
	  error_code = fileio_page_check_corruption (thread_p, p_dwb_ordered_slots[i].io_page, &is_page_corrupted);
	  if (error_code != NO_ERROR)
	    {
	      /* Error in checksum computation. */
	      goto end;
	    }

	  if (is_page_corrupted)
	    {
	      /* The page is corrupted in data volume and DWB. Something wrong happened. */
	      assert_release (false);
	      if (prm_get_bool_value (PRM_ID_DWB_ENABLE_LOG))
		{
		  _er_log_debug (ARG_FILE_LINE, "DWB error: Can't recover page = (%d,%d)\n", vpid->volid, vpid->pageid);
		}
	      error_code = ER_FAILED;
	      goto end;
	    }

	  /* The page content in data volume will be replaced later with the DWB page content. */
	}

      /* Replace the corrupted pages in data volume with the DWB content, if is the case. */
      error_code = dwb_write_block (thread_p, rcv_block, p_dwb_ordered_slots, ordered_slots_length, false);
      if (error_code != NO_ERROR)
	{
	  goto end;
	}

      /* Now, flush the volumes having pages in current block. */
      for (i = 0; i < rcv_block->count_flush_volumes_info; i++)
	{
	  if (fileio_synchronize (thread_p, rcv_block->flush_volumes_info[i].vdes, NULL, false) == NULL_VOLDES)
	    {
	      error_code = ER_FAILED;
	      goto end;
	    }
	}
      rcv_block->count_flush_volumes_info = 0;

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
      if (prm_get_bool_value (PRM_ID_DWB_ENABLE_LOG))
	{
	  _er_log_debug (ARG_FILE_LINE, "DWB error: Can't create DWB \n");
	}
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
int
dwb_flush_next_block (THREAD_ENTRY * thread_p)
{
  unsigned int block_no;
  DWB_BLOCK *flush_block = NULL;
  int error_code = NO_ERROR, retry_flush_iter = 0, retry_flush_max = 5;
  UINT64 position_with_flags;

start:
  position_with_flags = ATOMIC_INC_64 (&double_Write_Buffer.position_with_flags, 0ULL);
  if (!DWB_IS_CREATED (position_with_flags) || DWB_IS_MODIFYING_STRUCTURE (position_with_flags))
    {
      return NO_ERROR;
    }

  dwb_get_next_block_for_flush (thread_p, &block_no);
  if (block_no < DWB_NUM_TOTAL_BLOCKS)
    {
      flush_block = &double_Write_Buffer.blocks[block_no];
    start_flush_block:
      /* Flush all pages from current block */
      assert (flush_block != NULL && flush_block->count_wb_pages == DWB_BLOCK_NUM_PAGES);
      if (DWB_GET_PREV_BLOCK (flush_block->block_no)->version < flush_block->version
	  || ((DWB_GET_PREV_BLOCK (flush_block->block_no)->version == flush_block->version)
	      && (flush_block->block_no > DWB_GET_PREV_BLOCK_NO (flush_block->block_no))))
	{
	  if (DWB_GET_PREV_BLOCK (flush_block->block_no)->count_wb_pages != 0)
	    {
	      assert_release (false);
	    }
	}

      error_code = dwb_flush_block (thread_p, flush_block, NULL);
      if (error_code != NO_ERROR)
	{
	  /* Something wrong happened, sleep 10 msec and try again. */
	  if (retry_flush_iter < retry_flush_max)
	    {
#if defined(SERVER_MODE)
	      thread_sleep (10);
#endif /* SERVER_MODE */
	      retry_flush_iter++;
	      goto start_flush_block;
	    }

	  if (prm_get_bool_value (PRM_ID_DWB_ENABLE_LOG))
	    {
	      _er_log_debug (ARG_FILE_LINE, "DWB error: Can't flush block = %d having version %lld\n",
			     flush_block->block_no, flush_block->version);
	    }

	  return error_code;
	}

      if (prm_get_bool_value (PRM_ID_DWB_ENABLE_LOG))
	{
	  _er_log_debug (ARG_FILE_LINE, "Successfully flushed DWB block = %d having version %lld\n",
			 flush_block->block_no, flush_block->version);
	}

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
 *  Note: TO DO - optimize this function - force existing data, do not fills with null pages 
 */
int
dwb_flush_force (THREAD_ENTRY * thread_p, bool * all_sync)
{
  UINT64 position_with_flags, blocks_status, current_block_version;
  unsigned int current_block_no = DWB_NUM_TOTAL_BLOCKS, flush_block_no;
  char page_buf[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT];
  FILEIO_PAGE *iopage;
  VPID null_vpid = { NULL_VOLID, NULL_PAGEID };
  int error_code = NO_ERROR;
  DWB_SLOT *dwb_slot = NULL;
  DWB_BLOCK *flush_block = NULL;
  unsigned int num_total_blocks, diff_num_blocks;

  assert (all_sync != NULL);

  *all_sync = false;
start:
  position_with_flags = ATOMIC_INC_64 (&double_Write_Buffer.position_with_flags, 0ULL);

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
  /* Save the block version, to detect whether the block was overwritten. */
  current_block_version = double_Write_Buffer.blocks[current_block_no].version;
  num_total_blocks = DWB_NUM_TOTAL_BLOCKS;
  assert (current_block_no < num_total_blocks);

  if (position_with_flags != ATOMIC_INC_64 (&double_Write_Buffer.position_with_flags, 0ULL))
    {
      /* The position_with_flags was modified meanwhile by concurrent threads. */
      goto start;
    }

  /* Check whether the previous blocks were flushed */
check_flushed_blocks:
  flush_block_no = double_Write_Buffer.next_block_to_flush;
  flush_block = &double_Write_Buffer.blocks[flush_block_no];

#if defined (SERVER_MODE)

  /* Check whether the block was flushed. */
  if ((flush_block->version > current_block_version)
      || (flush_block->version == current_block_version && flush_block_no > current_block_no))
    {
      /* Nothing to do. Everything flushed. */
      return NO_ERROR;
    }

  if (current_block_no != flush_block_no)
    {
      if (current_block_no > flush_block_no)
	{
	  diff_num_blocks = current_block_no - flush_block_no + 1;
	}
      else
	{
	  diff_num_blocks = current_block_no + 1 + num_total_blocks - flush_block_no;
	}

      assert (num_total_blocks >= 1 && diff_num_blocks <= num_total_blocks);

      if (diff_num_blocks < num_total_blocks)
	{
	  /* Since there are empty blocks, add null pages to current block to force the flush. */
	}
      else
	{
	  if (thread_is_dwb_checksum_computation_thread_available ())
	    {
	      /* Wake up checksum thread to compute checksum. */
	      thread_wakeup_dwb_checksum_computation_thread ();
	    }

	  if (thread_is_dwb_flush_block_thread_available ())
	    {
	      /* Wake up flush thread. */
	      thread_wakeup_dwb_flush_block_thread ();
	    }

	  /* Check again whether flushing advanced. */
	  if (flush_block_no != double_Write_Buffer.next_block_to_flush)
	    {
	      goto check_flushed_blocks;
	    }

	  /* Too many blocks, wait for flush block threads. */
	  error_code = dwb_wait_for_block_completion (thread_p, flush_block_no);
	  if (error_code != NO_ERROR)
	    {
	      if (error_code == ER_CSS_PTHREAD_COND_TIMEDOUT)
		{
		  /* timeout, try again */
		  goto start;
		}

	      if (prm_get_bool_value (PRM_ID_DWB_ENABLE_LOG))
		{
		  _er_log_debug (ARG_FILE_LINE, "Error %d while waiting for flushing block=%d having version %lld \n",
				 error_code, flush_block_no, flush_block->version);
		}

	      return error_code;
	    }

	  goto check_flushed_blocks;
	}
    }
#endif

  /*
   * Add NULL pages to force block flushing. In this way, preserve also block flush order. This means that all
   * blocks are flushed - the entire DWB.
   */
  iopage = (FILEIO_PAGE *) PTR_ALIGN (page_buf, MAX_ALIGNMENT);
  memset (iopage, 0, IO_MAX_PAGE_SIZE);
  fileio_initialize_res (thread_p, &(iopage->prv));
  while ((double_Write_Buffer.blocks[current_block_no].count_wb_pages != DWB_BLOCK_NUM_PAGES)
	 && (current_block_version == double_Write_Buffer.blocks[current_block_no].version))
    {
      /* The block is not full yet. Add more null pages. */
      dwb_slot = NULL;
      error_code = dwb_add_page (thread_p, iopage, &null_vpid, &dwb_slot);
      if (error_code != NO_ERROR)
	{
	  return error_code;
	}
      else if (dwb_slot == NULL)
	{
	  /* DWB disabled meanwhile, everything flushed. */
	  return NO_ERROR;
	}
    }

  if (current_block_version == double_Write_Buffer.blocks[current_block_no].version)
    {
      /* Needs block flushing. */
      if (prm_get_bool_value (PRM_ID_DWB_ENABLE_LOG))
	{
	  _er_log_debug (ARG_FILE_LINE, "Waits for flushing block=%d having version=%lld) \n",
			 current_block_no, double_Write_Buffer.blocks[current_block_no].version);
	}

#if defined (SERVER_MODE)
      if (thread_is_dwb_checksum_computation_thread_available ())
	{
	  /* Wake up checksum thread to compute checksum. */
	  thread_wakeup_dwb_checksum_computation_thread ();
	}

      if (thread_is_dwb_flush_block_thread_available ())
	{
	  /* Wake up flush thread. */
	  thread_wakeup_dwb_flush_block_thread ();
	}

      /* Not flushed yet. Wait for current block flush. */
      error_code = dwb_wait_for_block_completion (thread_p, current_block_no);
      if (error_code != NO_ERROR)
	{
	  if (error_code == ER_CSS_PTHREAD_COND_TIMEDOUT)
	    {
	      /* timeout, try again */
	      goto start;
	    }

	  if (prm_get_bool_value (PRM_ID_DWB_ENABLE_LOG))
	    {
	      _er_log_debug (ARG_FILE_LINE, "Error %d while waiting for flushing block=%d having version %lld \n",
			     error_code, current_block_no, double_Write_Buffer.blocks[current_block_no].version);
	    }

	  return error_code;
	}
#endif /* SERVER_MODE */
    }

  *all_sync = true;

  return NO_ERROR;
}

/*
* dwb_flush_block_helper () - Helps block flushing.
*
* return   : Error code.
* thread_p (in): Thread entry.
*/
int
dwb_flush_block_helper (THREAD_ENTRY * thread_p)
{
  unsigned int i;
  int num_pages, num_pages2;
  DWB_BLOCK *block;

  block = double_Write_Buffer.helper_flush_block;
  if (block != NULL)
    {
      for (i = 0; i < block->count_flush_volumes_info; i++)
	{
	  do
	    {
	      num_pages = ATOMIC_INC_32 (&block->flush_volumes_info[i].num_pages, 0);
	      /* TODO - use parameter */
	      if (num_pages < 100)
		{
		  /* Not enough pages, do not flush yet. */
		  break;
		}

	      /* Reset the number of pages in volume. */
	      num_pages2 = ATOMIC_TAS_32 (&block->flush_volumes_info[i].num_pages, 0);
	      if (num_pages2 >= num_pages)
		{
		  (void) fileio_synchronize (thread_p, block->flush_volumes_info[i].vdes, NULL, false);
		}
	      else
		{
		  /* Flushed by DWB block flusher. */
		  assert (num_pages2 == 0);
		}
	    }
	  while (true);
	}

      /* Be sure that the helper flush block was not changed by other thread. */
      assert (block == double_Write_Buffer.helper_flush_block);
      double_Write_Buffer.helper_flush_block = NULL;
    }

  return NO_ERROR;
}

/*
 * dwb_flush_block_with_checksum(): Computes checksum for requested slots.
 *
 *   returns: Error code.
 * thread_p (in): The thread entry.
 */
int
dwb_compute_checksums (THREAD_ENTRY * thread_p)
{
  UINT64 position_with_flags;
  unsigned int num_block, start_block, end_block;
  bool block_slots_checksums_computed, block_needs_flush, checksum_computed;
  int error_code = NO_ERROR;

start:
  position_with_flags = ATOMIC_INC_64 (&double_Write_Buffer.position_with_flags, 0ULL);
  if (!DWB_IS_CREATED (position_with_flags) || DWB_IS_MODIFYING_STRUCTURE (position_with_flags))
    {
      return true;
    }

  start_block = double_Write_Buffer.next_block_to_flush;
  if (!dwb_block_has_all_checksums_computed (start_block))
    {
      /* Compute only for the block that must be flushed first, to avoid delays. */
      end_block = start_block + 1;
    }
  else
    {
      end_block = double_Write_Buffer.num_blocks;
    }

  /* Compute checksums and/or flush the block. */
  checksum_computed = false;
  for (num_block = start_block; num_block < end_block; num_block++)
    {
      error_code =
	dwb_compute_block_checksums (thread_p, &double_Write_Buffer.blocks[num_block],
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
	      thread_wakeup_dwb_flush_block_thread ();
	    }
	}
#endif /* SERVER_MODE */

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
  LF_TRAN_ENTRY *t_entry = thread_get_tran_entry (NULL, THREAD_TS_DWB_SLOTS);
  DWB_SLOTS_HASH_ENTRY *slots_hash_entry = NULL;
  int error_code = NO_ERROR;

  assert (vpid != NULL && io_page != NULL && success != NULL);

  *success = false;
  if (!dwb_is_created ())
    {
      return NO_ERROR;
    }

  error_code = lf_hash_find (t_entry, &double_Write_Buffer.slots_hash->ht, (void *) vpid, (void **) &slots_hash_entry);
  if (error_code != NO_ERROR)
    {
      /* Should not happen */
      ASSERT_ERROR ();
      if (prm_get_bool_value (PRM_ID_DWB_ENABLE_LOG))
	{
	  _er_log_debug (ARG_FILE_LINE, "DWB hash find key (%d, %d) with %d error: \n", vpid->volid, vpid->pageid,
			 error_code);
	}
      return error_code;
    }
  else if (slots_hash_entry != NULL)
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
