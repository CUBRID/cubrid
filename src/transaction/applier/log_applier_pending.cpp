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
 * log_applier_pending.cpp - transactions held back by the dependency gate until their predecessors are applied
 *
 * The coordinator parks a transaction here when the gate does not let it pass yet, and keeps reading the log
 * instead of waiting. Every time a completion moves the gate's frontier, the coordinator takes out the parked
 * transactions the gate now lets pass, oldest first. A parked transaction is always on the gate's
 * commit-order ring, so the list never holds more than the commit-order capacity.
 * Only the coordinator touches the list, so it has no lock.
 */

#include "config.h"

#include "log_applier_pending.hpp"
#include "log_applier_gate.hpp"
#include "error_manager.h"
#include "memory_alloc.h"	/* free_and_init */

#include <assert.h>
#include <stdlib.h>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

typedef struct la_pending_node LA_PENDING_NODE;
struct la_pending_node
{
  LA_TASK task;
  LA_PENDING_NODE *next;
};

static LA_PENDING_NODE *la_Pending_head = NULL;
static LA_PENDING_NODE *la_Pending_tail = NULL;

/*
 * la_pending_push() - park a transaction whose predecessor is not applied yet
 *   return: NO_ERROR or ER_OUT_OF_VIRTUAL_MEMORY
 *   task(in): task to park; copied
 */
int
la_pending_push (const LA_TASK *task)
{
  LA_PENDING_NODE *node;

  assert (task != NULL);

  node = (LA_PENDING_NODE *) malloc (sizeof (LA_PENDING_NODE));
  if (node == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (LA_PENDING_NODE));
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  node->task = *task;
  node->next = NULL;

  if (la_Pending_tail == NULL)
    {
      la_Pending_head = node;
      la_Pending_tail = node;
    }
  else
    {
      la_Pending_tail->next = node;
      la_Pending_tail = node;
    }

  return NO_ERROR;
}

/*
 * la_pending_pop_ready() - take the oldest parked task whose predecessor is now applied
 *   return: true when a task was released, false when every parked task still waits
 *   task(out): the released task
 *
 * Note:
 *   Scans in parking order, so released tasks keep their relative order, while a task that still
 *   waits does not hold back a later one that is ready.
 */
bool
la_pending_pop_ready (LA_TASK *task)
{
  LA_PENDING_NODE *prev = NULL;
  LA_PENDING_NODE *cur;

  assert (task != NULL);

  for (cur = la_Pending_head; cur != NULL; prev = cur, cur = cur->next)
    {
      if (la_gate_is_satisfied (&cur->task.dependency_seq) == false)
	{
	  continue;
	}

      if (prev == NULL)
	{
	  la_Pending_head = cur->next;
	}
      else
	{
	  prev->next = cur->next;
	}
      if (cur == la_Pending_tail)
	{
	  la_Pending_tail = prev;
	}

      *task = cur->task;
      free_and_init (cur);

      return true;
    }

  return false;
}

/*
 * la_pending_clear() - drop every parked task
 *
 * Note:
 *   Called when the applier shuts down. applylogdb restarts the applier inside the same process after
 *   the server goes down, and a parked task left on the list would point at an apply slot freed by the
 *   shutdown: the next run would read its start_lsa as required_lsa and could dispatch it to a worker.
 *   The tasks are not lost: their commits lie after committed_lsa and the next run reads them again.
 */
void
la_pending_clear (void)
{
  LA_PENDING_NODE *node;

  while (la_Pending_head != NULL)
    {
      node = la_Pending_head;
      la_Pending_head = node->next;
      free_and_init (node);
    }
  la_Pending_tail = NULL;
}

/*
 * la_pending_is_empty() - check whether no task is parked
 */
bool
la_pending_is_empty (void)
{
  return la_Pending_head == NULL;
}

/*
 * la_pending_foreach() - call fn for every parked task, oldest first
 *   fn(in): callback; must not push or pop while iterating
 *   arg(in): passed through to fn
 */
void
la_pending_foreach (void (*fn) (const LA_TASK * task, void *arg), void *arg)
{
  LA_PENDING_NODE *node;

  for (node = la_Pending_head; node != NULL; node = node->next)
    {
      fn (&node->task, arg);
    }
}
