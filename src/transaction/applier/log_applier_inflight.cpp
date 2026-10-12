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
 * log_applier_inflight.cpp - in-flight list: tasks handed to the workers and not yet retired
 *
 * Workers finish out of order and return results on one shared queue. The coordinator records every task
 * it hands over here, attaches each result to its entry by dispatch number, and retires entries from the
 * head in the order they were handed over. The restart point does not follow this list: committed_lsa
 * follows the gate's frontier, since tasks are handed over out of commit order (a parked task goes later).
 * Only the coordinator touches this FIFO, so it needs no lock.
 */

#include "config.h"

#include "log_applier_inflight.hpp"
#include "log_applier_worker.hpp"
#include "error_manager.h"
#include "porting.h"

#include <assert.h>
#include <string.h>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

typedef struct la_inflight_list LA_INFLIGHT_LIST;
struct la_inflight_list
{
  LA_INFLIGHT_ENTRY *entries;
  int capacity;
  int head;
  int tail;
  int count;
};

static LA_INFLIGHT_LIST la_Inflight = { NULL, 0, 0, 0, 0 };
static UINT64 la_Dispatch_sequence = 0;	/* next dispatch number; never reused, so a result matches one entry */

static LA_INFLIGHT_ENTRY *la_inflight_find_by_seq (UINT64 seq);

/*
 * la_inflight_init() - allocate the FIFO for the commit-order capacity and restart dispatch numbering
 *   return: NO_ERROR or ER_OUT_OF_VIRTUAL_MEMORY
 *   capacity(in): most dispatched, unretired entries at once, the commit-order capacity
 *
 * Note:
 *   A previous allocation is released.
 */
int
la_inflight_init (int capacity)
{
  assert (capacity > 0);

  if (la_Inflight.entries != NULL)
    {
      free_and_init (la_Inflight.entries);
    }

  la_Inflight.entries = (LA_INFLIGHT_ENTRY *) malloc ((size_t) capacity * sizeof (LA_INFLIGHT_ENTRY));
  if (la_Inflight.entries == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      (size_t) capacity * sizeof (LA_INFLIGHT_ENTRY));
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  la_Inflight.capacity = capacity;
  la_Inflight.head = 0;
  la_Inflight.tail = 0;
  la_Inflight.count = 0;
  la_Dispatch_sequence = 0;

  return NO_ERROR;
}

/*
 * la_inflight_get_capacity() - return the most entries the FIFO holds
 */
int
la_inflight_get_capacity (void)
{
  return la_Inflight.capacity;
}

/*
 * la_inflight_push() - record one dispatched task as a new entry at the tail
 *   return: NO_ERROR, or ER_FAILED when the FIFO is full
 *   apply(in): repl_list slot the task applies
 *   tranid(in): transaction id
 *   rectype(in): LOG_COMMIT or LOG_SYSOP_END
 *   seq_out(out): dispatch number given to the task; may be NULL
 */
int
la_inflight_push (LA_APPLY *apply, int tranid, int rectype, UINT64 *seq_out)
{
  LA_INFLIGHT_ENTRY *entry;

  if (la_Inflight.count >= la_Inflight.capacity)
    {
      /* callers wait for a free slot with la_coordinator_wait_for_inflight_slot first */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HA_GENERIC_ERROR, 1, "in-flight list overflow");
      return ER_FAILED;
    }

  entry = &la_Inflight.entries[la_Inflight.tail];
  entry->seq = la_Dispatch_sequence++;
  entry->apply = apply;
  entry->tranid = tranid;
  entry->rectype = rectype;
  entry->result_ready = false;
  memset (&entry->result, 0, sizeof (entry->result));

  if (seq_out != NULL)
    {
      *seq_out = entry->seq;
    }

  la_Inflight.tail = (la_Inflight.tail + 1) % la_Inflight.capacity;
  la_Inflight.count++;

  return NO_ERROR;
}

/*
 * la_inflight_peek() - return the oldest unretired entry
 *   return: the head entry, or NULL when the FIFO is empty
 *
 * Note:
 *   The entry stays in place until la_inflight_pop, so the caller can check
 *   whether its result has arrived and read it before deciding to retire it.
 */
LA_INFLIGHT_ENTRY *
la_inflight_peek (void)
{
  if (la_Inflight.count == 0)
    {
      return NULL;
    }

  return &la_Inflight.entries[la_Inflight.head];
}

/*
 * la_inflight_pop() - drop the head entry after it retires
 */
void
la_inflight_pop (void)
{
  if (la_Inflight.count == 0)
    {
      return;
    }

  la_Inflight.head = (la_Inflight.head + 1) % la_Inflight.capacity;
  la_Inflight.count--;
}

/*
 * la_inflight_attach_result() - store a worker's result on the entry with the same dispatch number
 *   return: NO_ERROR, or ER_FAILED when no entry carries result->seq
 *   result(in): result taken from the result queue
 */
int
la_inflight_attach_result (const LA_RESULT *result)
{
  LA_INFLIGHT_ENTRY *entry;

  assert (result != NULL);

  entry = la_inflight_find_by_seq (result->seq);
  if (entry == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HA_GENERIC_ERROR, 1, "dispatch result seq mismatch");
      return ER_FAILED;
    }

  entry->result = *result;
  entry->result_ready = true;

  return NO_ERROR;
}

/*
 * la_inflight_get_count() - return the number of dispatched, unretired entries
 */
int
la_inflight_get_count (void)
{
  return la_Inflight.count;
}

/*
 * la_inflight_entry_at() - return the pos-th dispatched, unretired entry, oldest first
 *   return: the entry, or NULL when pos is beyond the count
 *   pos(in): 0 for the entry that retires next
 */
LA_INFLIGHT_ENTRY *
la_inflight_entry_at (int pos)
{
  if (pos < 0 || pos >= la_Inflight.count)
    {
      return NULL;
    }

  return &la_Inflight.entries[(la_Inflight.head + pos) % la_Inflight.capacity];
}

static LA_INFLIGHT_ENTRY *
la_inflight_find_by_seq (UINT64 seq)
{
  int i, idx;

  idx = la_Inflight.head;
  for (i = 0; i < la_Inflight.count; i++)
    {
      LA_INFLIGHT_ENTRY *entry = &la_Inflight.entries[idx];

      if (entry->seq == seq)
	{
	  return entry;
	}
      idx = (idx + 1) % la_Inflight.capacity;
    }

  return NULL;
}
