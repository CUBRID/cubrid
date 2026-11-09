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
 * log_applier_pending.cpp - implementation of dependency-waiting task storage
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
#include <time.h>

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
static int la_Pending_count = 0;
static int la_Pending_max_count = 0;

void
la_pending_reset (void)
{
  assert (la_Pending_head == NULL && la_Pending_tail == NULL);
  la_Pending_count = 0;
  la_Pending_max_count = 0;
}

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

  la_Pending_count++;
  la_Pending_max_count = MAX (la_Pending_max_count, la_Pending_count);

  return NO_ERROR;
}

/*
 * la_pending_mark_ready() - timestamp parked tasks whose dependency has become satisfied
 *   now_ns(in): current CLOCK_MONOTONIC time
 *
 * Note:
 *   This is separate from pop: a ready task may remain parked while the task queue is full. Recording
 *   readiness here keeps dependency wait separate from worker-capacity wait.
 */
void
la_pending_mark_ready (UINT64 now_ns)
{
  LA_PENDING_NODE *node;

  for (node = la_Pending_head; node != NULL; node = node->next)
    {
      if (node->task.ready_ns == 0 && la_gate_is_satisfied (&node->task.dependency_seq))
	{
	  node->task.ready_ns = now_ns;
	}
    }
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
      if (cur->task.ready_ns == 0 && la_gate_is_satisfied (&cur->task.dependency_seq) == false)
	{
	  continue;
	}

      if (cur->task.ready_ns == 0)
	{
	  /* Defensive fallback for callers that did not mark readiness before popping. */
	  struct timespec ts;
	  clock_gettime (CLOCK_MONOTONIC, &ts);
	  cur->task.ready_ns = (UINT64) ts.tv_sec * 1000000000ULL + (UINT64) ts.tv_nsec;
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
      assert (la_Pending_count > 0);
      la_Pending_count--;

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
      if (node->task.merged_lsa != NULL)
	{
	  free_and_init (node->task.merged_lsa);
	}
      free_and_init (node);
      assert (la_Pending_count > 0);
      la_Pending_count--;
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

int
la_pending_get_count (void)
{
  return la_Pending_count;
}

int
la_pending_get_max_count (void)
{
  return la_Pending_max_count;
}

/*
 * la_pending_foreach() - call fn for every parked task, oldest first
 *   fn(in): callback; must not push or pop while iterating
 *   arg(in): passed through to fn
 */
void
la_pending_foreach (void (*fn) (const LA_TASK *task, void *arg), void *arg)
{
  LA_PENDING_NODE *node;

  for (node = la_Pending_head; node != NULL; node = node->next)
    {
      fn (&node->task, arg);
    }
}

/*
 * la_pending_find_merge_target() - find the parked fragment task a later fragment of the same key can join
 *   return: the parked task, or NULL
 *   dependency_seq(in): the later fragment's dependency
 *   unit_key_hash(in): the later fragment's single key hash, not 0
 *
 * Note:
 *   The later fragment's dependency is the SYSOP_END of the previous change of its key. When that change is the
 *   last fragment of a parked task, which has not gone to a worker yet, the later fragment can be applied by the
 *   same task right after it.
 */
LA_TASK *
la_pending_find_merge_target (const LOG_LSA *dependency_seq, UINT64 unit_key_hash)
{
  LA_PENDING_NODE *node;

  assert (dependency_seq != NULL);

  if (LSA_ISNULL (dependency_seq))
    {
      return NULL;
    }

  for (node = la_Pending_head; node != NULL; node = node->next)
    {
      const LOG_LSA *last;

      if (node->task.unit_key_hash == 0 || node->task.unit_key_hash != unit_key_hash)
	{
	  continue;
	}

      last = (node->task.merged_count > 0) ? &node->task.merged_lsa[node->task.merged_count - 1]
	: &node->task.commit_lsa;
      if (LSA_EQ (last, dependency_seq))
	{
	  return &node->task;
	}
    }

  return NULL;
}
