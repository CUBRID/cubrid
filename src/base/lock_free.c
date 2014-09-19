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
 * lock_free.c : Lock-free structures.
 */

#include "lock_free.h"
#include "error_manager.h"
#include <assert.h>

/*
 * Lock-Free Circular Queue section -
 * Lock free circular queue algorithm is based on two cursors, one for
 * consuming entries and one for producing new entries and a state for each
 * entry in queue.
 *
 * Requirements:
 * 1. Queue must have a fixed maximum size. After reaching that size,
 *    producing new entries will be refused.
 *
 * Characteristics:
 * 1. Queue should perform well under any level of contention.
 * 2. Queue is guaranteed to be consistent. No produced entries are skipped
 *    by consume and no produced entries can be consumed twice.
 * 3. Queue doesn't guarantee the order of consume or the order of produce.
 *    This means that if thread 1 calls consume before thread 2, it is not
 *    guaranteed that the entry consumed by thread 1 was produced before
 *    the entry consumed by thread 2.
 *    Same for two concurrent producers.
 * 4. Consume/produce never spins on the same entry (next loop is
 *    guaranteed to be on a different cursor).
 */

/* States for circular queue entries */
#define READY_FOR_PRODUCE		  (INT32) 0
#define RESERVED_FOR_PRODUCE		  (INT32) 1
#define READY_FOR_CONSUME		  (INT32) 2
#define RESERVED_FOR_CONSUME		  (INT32) 3

/*
 * lock_free_circular_queue_produce () - Add new entry to queue.
 *
 * return     : True if the entry was added, false otherwise.
 * queue (in) : Lock-free circular queue.
 * data (in)  : New entry data.
 */
bool
lock_free_circular_queue_produce (LOCK_FREE_CIRCULAR_QUEUE * queue,
				  void *data)
{
  int entry_index;
  INT32 produce_cursor;

  /* Loop until a free entry for produce is found or queue is full. */
  /* Since this may be done under concurrency with no locks, a produce cursor
   * and an entry state for the cursor are used to synchronize producing data.
   * After reading the produce cursor, since there is no lock to protect it,
   * other producer may race to use it for its own produced data.
   * The producer can gain an entry only if it successfully changes the state
   * from READY_FOR_PRODUCE to RESERVED_FOR_PRODUCE (using compare & swap).
   */
  while (true)
    {
      if (LOCK_FREE_CIRCULAR_QUEUE_IS_FULL (queue))
	{
	  /* The queue is full, cannot produce new entries */
	  return false;
	}

      /* Get current produce_cursor */
      produce_cursor = queue->produce_cursor;
      /* Compute entry's index in circular queue */
      entry_index = produce_cursor % queue->capacity;

      if (ATOMIC_CAS_32 (&queue->entry_state[entry_index], READY_FOR_PRODUCE,
			 RESERVED_FOR_PRODUCE))
	{
	  /* Entry was successfully allocated for producing data, break the
	   * loop now.
	   */
	  break;
	}
      /* Produce must be tried again with a different cursor */
      if (queue->entry_state[entry_index] == RESERVED_FOR_PRODUCE)
	{
	  /* The entry was already reserved by another producer, but the
	   * produce cursor may be the same. Try to increment the cursor to
	   * avoid being spin-locked on same cursor value. The increment will
	   * fail if the cursor was already incremented.
	   */
	  (void) ATOMIC_CAS_32 (&queue->produce_cursor, produce_cursor,
				produce_cursor + 1);
	}
      else if (queue->entry_state[entry_index] == RESERVED_FOR_CONSUME)
	{
	  /* Consumer incremented the consumer cursor but didn't change the
	   * state to READY_FOR_PRODUCE. In this case, the list is considered
	   * full, and producer must fail.
	   */
	  return false;
	}
      /* For all other states, the producer which used current cursor
       * already incremented it.
       */
      /* Try again */
    }

  /* Successfully allocated entry for new data */

  /* Copy produced data to allocated entry */
  memcpy (queue->data + (entry_index * queue->data_size), data,
	  queue->data_size);
  /* Set entry as readable. Since other should no longer race for this entry
   * after it was allocated, we don't need an atomic CAS operation.
   */
  assert (queue->entry_state[entry_index] == RESERVED_FOR_PRODUCE);

  /* Try to increment produce cursor. If this thread was preempted after
   * allocating entry and before increment, it may have been already
   * incremented.
   */
  ATOMIC_CAS_32 (&queue->produce_cursor, produce_cursor, produce_cursor + 1);
  queue->entry_state[entry_index] = READY_FOR_CONSUME;

  /* Successfully produced a new entry */
  return true;
}

/*
 * lock_free_circular_queue_consume () - Pop one entry from queue.
 *
 * return     : First queue entry or NULL if the queue is empty.
 * queue (in) : Lock-free circular queue.
 * data (out) : Pointer where to save popped data.
 */
bool
lock_free_circular_queue_consume (LOCK_FREE_CIRCULAR_QUEUE * queue,
				  void *data)
{
  int entry_index;
  INT32 consume_cursor;

  /* Loop until an entry can be consumed or until queue is empty */
  /* Since there may be more than one consumer and no locks is used, a consume
   * cursor and entry states are used to synchronize all consumers. If several
   * threads race to consume same entry, only the one that successfully
   * changes state from READY_FOR_CONSUME to RESERVED_FOR_CONSUME can consume
   * the entry.
   * Others will have to retry with a different entry.
   */
  while (true)
    {
      if (LOCK_FREE_CIRCULAR_QUEUE_IS_EMPTY (queue))
	{
	  /* Queue is empty, nothing to consume */
	  return false;
	}

      /* Get current consume cursor */
      consume_cursor = queue->consume_cursor;

      /* Compute entry's index in circular queue */
      entry_index = consume_cursor % queue->capacity;

      /* Try to set entry state from READY_FOR_CONSUME to
       * RESERVED_FOR_CONSUME.
       */
      if (ATOMIC_CAS_32 (&queue->entry_state[entry_index], READY_FOR_CONSUME,
			 RESERVED_FOR_CONSUME))
	{
	  /* Entry was successfully reserved for consume. Break loop. */
	  break;
	}

      /* Consume must be tried again with a different cursor */
      if (queue->entry_state[entry_index] == RESERVED_FOR_CONSUME)
	{
	  /* The entry was already reserved by another consumer, but the
	   * consume cursor may be the same. Try to increment the cursor to
	   * avoid being spin-locked on same cursor value. The increment will
	   * fail if the cursor was already incremented.
	   */
	  ATOMIC_CAS_32 (&queue->consume_cursor, consume_cursor,
			 consume_cursor + 1);
	}
      else if (queue->entry_state[entry_index] == RESERVED_FOR_PRODUCE)
	{
	  /* Producer didn't finish yet, consider that list is empty and there
	   * is nothing to consume.
	   */
	  return false;
	}
      /* For all other states, the producer which used current cursor
       * already incremented it.
       */
      /* Try again */
    }

  /* Successfully reserved entry to consume */

  /* Consume the data found in entry */
  memcpy (data, queue->data + (entry_index * queue->data_size),
	  queue->data_size);

  /* Try to increment consume cursor. If this thread was preempted after
   * reserving the entry and before incrementing the cursor, another consumer
   * may have already incremented it.
   */
  ATOMIC_CAS_32 (&queue->consume_cursor, consume_cursor, consume_cursor + 1);

  /* Change state to READY_TO_PRODUCE */
  /* Nobody can race us on changing this value, so CAS is not necessary */
  assert (queue->entry_state[entry_index] == RESERVED_FOR_CONSUME);
  queue->entry_state[entry_index] = READY_FOR_PRODUCE;

  return true;
}

/*
 * lock_free_circular_queue_create () - Allocate, initialize and return
 *					a new lock-free circular queue.
 *
 * return	       : Lock-free circular queue.
 * capacity (in)       : The maximum queue capacity.
 * data_size (in)      : Size of queue entry data.
 */
LOCK_FREE_CIRCULAR_QUEUE *
lock_free_circular_queue_create (INT32 capacity, int data_size)
{
  /* Allocate queue */
  LOCK_FREE_CIRCULAR_QUEUE *queue =
    (LOCK_FREE_CIRCULAR_QUEUE *) malloc (sizeof (LOCK_FREE_CIRCULAR_QUEUE));
  if (queue == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      sizeof (LOCK_FREE_CIRCULAR_QUEUE));
      return NULL;
    }

  /* Allocate queue data buffer */
  queue->data = malloc (capacity * data_size);
  if (queue->data == NULL)
    {
      free (queue);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      capacity * data_size);
      return NULL;
    }

  /* Allocate the array of entry state */
  queue->entry_state = malloc (capacity * sizeof (INT32));
  if (queue->entry_state == NULL)
    {
      free (queue->data);
      free (queue);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      capacity * sizeof (INT32));
      return NULL;
    }
  /* Initialize all entries as READY_TO_PRODUCE */
  memset (queue->entry_state, 0, capacity * sizeof (INT32));

  /* Initialize data size and capacity */
  queue->data_size = data_size;
  queue->capacity = capacity;

  /* Initialize cursors */
  queue->consume_cursor = queue->produce_cursor = 0;

  /* Return initialized queue */
  return queue;
}

/*
 * lock_free_circular_queue_destroy () - Destroy a lock-free circular queue.
 *
 * return     : Void.
 * queue (in) : Lock-free circular queue.
 */
void
lock_free_circular_queue_destroy (LOCK_FREE_CIRCULAR_QUEUE * queue)
{
  if (queue == NULL)
    {
      /* Nothing to do */
      return;
    }

  /* Free data buffer */
  if (queue->data != NULL)
    {
      free (queue->data);
    }

  /* Free the array of entry state */
  if (queue->entry_state != NULL)
    {
      free (queue->entry_state);
    }

  /* Free queue */
  free (queue);
}
