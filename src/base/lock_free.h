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
 * The two ends of the queue are stored (head and tail). New entries are
 * "produced" at the end of the queue, while "consumers" will pop entries from
 * the head of the queue.
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
  INT32 head;
  INT32 read_tail;
  INT32 tail;
  INT32 capacity;
  int data_size;
};

/* Check if queue is empty */
#define LOCK_FREE_CIRCULAR_QUEUE_IS_EMPTY(queue) \
  (queue->head == queue->read_tail)
/* Check if queue is full */
#define LOCK_FREE_CIRCULAR_QUEUE_IS_FULL(queue) \
  (((queue->tail + 1) % queue->capacity) == queue->head)

extern bool lock_free_circular_queue_push (LOCK_FREE_CIRCULAR_QUEUE * queue,
					   void *data);
extern bool lock_free_circular_queue_pop (LOCK_FREE_CIRCULAR_QUEUE * queue,
					  void *data);
extern LOCK_FREE_CIRCULAR_QUEUE *lock_free_circular_queue_create (INT32
								  capacity,
								  int
								  data_size);
extern void lock_free_circular_queue_destroy (LOCK_FREE_CIRCULAR_QUEUE *
					      queue);

#endif /* _LOCK_FREE_H_ */
