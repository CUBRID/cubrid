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
 * log_applier_dispatch_order.cpp - dispatch-order FIFO matching worker results to dispatched tasks
 *
 * Workers finish out of order and return results on one shared queue. The coordinator records every
 * dispatch here, attaches each result to its entry by dispatch number, and retires entries strictly
 * from the head, so the restart point only advances over a gap-free prefix. Only the coordinator
 * touches this FIFO, so it needs no lock.
 *
 * TODO: Rename this ledger. "Dispatch order" reads like the verb, so la_dispatch_order_push inside
 *       la_coordinator_dispatch_task looks like a second dispatch, while the ledger exists to match results
 *       and retire in order. Candidates: retire order, in-flight list. Rename the module, its functions and
 *       types, and the design documents together.
 */

#include "config.h"

#include "log_applier_dispatch_order.hpp"
#include "log_applier_worker.hpp"
#include "error_manager.h"
#include "porting.h"

#include <assert.h>
#include <string.h>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

typedef struct la_dispatch_order LA_DISPATCH_ORDER;
struct la_dispatch_order
{
  LA_DISPATCH_ENTRY *entries;
  int capacity;
  int head;
  int tail;
  int count;
};

static LA_DISPATCH_ORDER la_Dispatch_order = { NULL, 0, 0, 0, 0 };
static UINT64 la_Dispatch_sequence = 0;	/* next dispatch number; never reused, so a result matches one entry */

static LA_DISPATCH_ENTRY *la_dispatch_order_find_by_seq (UINT64 seq);

/*
 * la_dispatch_order_init() - allocate the FIFO for the commit-order capacity and restart dispatch numbering
 *   return: NO_ERROR or ER_OUT_OF_VIRTUAL_MEMORY
 *   capacity(in): most dispatched, unretired entries at once, the commit-order capacity
 *
 * Note:
 *   A previous allocation is released.
 */
int
la_dispatch_order_init (int capacity)
{
  assert (capacity > 0);

  if (la_Dispatch_order.entries != NULL)
    {
      free_and_init (la_Dispatch_order.entries);
    }

  la_Dispatch_order.entries = (LA_DISPATCH_ENTRY *) malloc ((size_t) capacity * sizeof (LA_DISPATCH_ENTRY));
  if (la_Dispatch_order.entries == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      (size_t) capacity * sizeof (LA_DISPATCH_ENTRY));
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  la_Dispatch_order.capacity = capacity;
  la_Dispatch_order.head = 0;
  la_Dispatch_order.tail = 0;
  la_Dispatch_order.count = 0;
  la_Dispatch_sequence = 0;

  return NO_ERROR;
}

/*
 * la_dispatch_order_get_capacity() - return the most entries the FIFO holds
 */
int
la_dispatch_order_get_capacity (void)
{
  return la_Dispatch_order.capacity;
}

/*
 * la_dispatch_order_push() - record one dispatched task as a new entry at the tail
 *   return: NO_ERROR, or ER_FAILED when the FIFO is full
 *   apply(in): repl_list slot the task applies
 *   tranid(in): transaction id
 *   rectype(in): LOG_COMMIT or LOG_SYSOP_END
 *   seq_out(out): dispatch number given to the task; may be NULL
 */
int
la_dispatch_order_push (LA_APPLY *apply, int tranid, int rectype, UINT64 *seq_out)
{
  LA_DISPATCH_ENTRY *entry;

  if (la_Dispatch_order.count >= la_Dispatch_order.capacity)
    {
      /* callers wait for a free slot with la_coordinator_wait_for_dispatch_slot first */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HA_GENERIC_ERROR, 1, "dispatch order queue overflow");
      return ER_FAILED;
    }

  entry = &la_Dispatch_order.entries[la_Dispatch_order.tail];
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

  la_Dispatch_order.tail = (la_Dispatch_order.tail + 1) % la_Dispatch_order.capacity;
  la_Dispatch_order.count++;

  return NO_ERROR;
}

/*
 * la_dispatch_order_peek() - return the oldest unretired entry
 *   return: the head entry, or NULL when the FIFO is empty
 *
 * Note:
 *   The entry stays in place until la_dispatch_order_pop, so the caller can check
 *   whether its result has arrived and read it before deciding to retire it.
 */
LA_DISPATCH_ENTRY *
la_dispatch_order_peek (void)
{
  if (la_Dispatch_order.count == 0)
    {
      return NULL;
    }

  return &la_Dispatch_order.entries[la_Dispatch_order.head];
}

/*
 * la_dispatch_order_pop() - drop the head entry after it retires
 */
void
la_dispatch_order_pop (void)
{
  if (la_Dispatch_order.count == 0)
    {
      return;
    }

  la_Dispatch_order.head = (la_Dispatch_order.head + 1) % la_Dispatch_order.capacity;
  la_Dispatch_order.count--;
}

/*
 * la_dispatch_order_attach_result() - store a worker's result on the entry with the same dispatch number
 *   return: NO_ERROR, or ER_FAILED when no entry carries result->seq
 *   result(in): result taken from the result queue
 */
int
la_dispatch_order_attach_result (const LA_RESULT *result)
{
  LA_DISPATCH_ENTRY *entry;

  assert (result != NULL);

  entry = la_dispatch_order_find_by_seq (result->seq);
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
 * la_dispatch_order_get_count() - return the number of dispatched, unretired entries
 */
int
la_dispatch_order_get_count (void)
{
  return la_Dispatch_order.count;
}

static LA_DISPATCH_ENTRY *
la_dispatch_order_find_by_seq (UINT64 seq)
{
  int i, idx;

  idx = la_Dispatch_order.head;
  for (i = 0; i < la_Dispatch_order.count; i++)
    {
      LA_DISPATCH_ENTRY *entry = &la_Dispatch_order.entries[idx];

      if (entry->seq == seq)
	{
	  return entry;
	}
      idx = (idx + 1) % la_Dispatch_order.capacity;
    }

  return NULL;
}
