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
 * lock_free.h : Lock-free structures interface.
 */

#ifndef _LOCK_FREE_H_
#define _LOCK_FREE_H_

#include "porting.h"

/*
 * Lock-free Circular Queue
 */

/* Lock-free Circular Queue is actually an array of entries, where the last
 * entry in the array is considered as preceding the first entry in the array.
 * The two ends of the queue are stored (consume_cursor and produce_cursor).
 * New entries are "produced" at the end of the queue, while "consumers" will
 * pop entries from the consume_cursor of the queue.
 *
 * The queue has a fixed maximum capacity. When there is no more room for new
 * entries, the push function will return false.
 *
 * The size for entry data must be fixed.
 */
typedef struct lock_free_circular_queue LOCK_FREE_CIRCULAR_QUEUE;
struct lock_free_circular_queue
{
  char *data;
  INT32 *entry_state;
  int data_size;
  INT32 consume_cursor;
  INT32 produce_cursor;
  INT32 capacity;
};

/* Macro's to inspect queue status and size. Note that their results is not
 * guaranteed to be precise.
 */
/* Check if queue is empty */
/* Macro can sometimes return true even if the queue is not empty (if
 * concurrent transactions consume entries at the same time). However it will
 * always return true if the queue is empty.
 */
#define LOCK_FREE_CIRCULAR_QUEUE_IS_EMPTY(queue) \
  (queue->produce_cursor <= queue->consume_cursor)
/* Check if queue is full */
/* Macro can return true even if the queue is not full. However it will never
 * return false if the queue is really full.
 */
#define LOCK_FREE_CIRCULAR_QUEUE_IS_FULL(queue) \
  (queue->consume_cursor <= queue->produce_cursor - queue->capacity + 1)
/* Get queue size */
/* The functions will return the exact size if there are no concurrent
 * consumers/producers. Concurrent consumers/producers may alter the result.
 */
#define LOCK_FREE_CIRCULAR_QUEUE_APPROX_SIZE(queue) \
  (queue->produce_cursor - queue->consume_cursor)

extern bool lock_free_circular_queue_produce (LOCK_FREE_CIRCULAR_QUEUE *
					      queue, void *data);
extern bool lock_free_circular_queue_consume (LOCK_FREE_CIRCULAR_QUEUE *
					      queue, void *data);
extern LOCK_FREE_CIRCULAR_QUEUE *lock_free_circular_queue_create (INT32
								  capacity,
								  int
								  data_size);
extern void lock_free_circular_queue_destroy (LOCK_FREE_CIRCULAR_QUEUE *
					      queue);

#endif /* _LOCK_FREE_H_ */
