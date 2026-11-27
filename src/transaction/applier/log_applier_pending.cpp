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
 * log_applier_pending.cpp - implementation of pending task storage
 *
 * The coordinator parks a task here while its dependency is unsatisfied or the task queue is full, and
 * keeps reading the log instead of waiting. It retries ready tasks as dependencies and queue capacity
 * allow, oldest first. A parked task is always on the gate's
 * commit-order ring, so the list never holds more than the commit-order capacity.
 * Only the coordinator touches the list, so it has no lock.
 *
 * The list holds hundreds of thousands of tasks when the workers fall behind, and the coordinator asks about it
 * after every completion and every commit. Nothing here walks the whole list for such a question: the lowest
 * hold_lsa is kept in two heaps, and readiness is recorded only on the tasks a release pass visits.
 */

#include "config.h"

#include "log_applier_pending.hpp"
#include "log_applier_gate.hpp"
#include "log_applier_clock.hpp"
#include "error_manager.h"
#include "memory_alloc.h"	/* free_and_init */

#include <assert.h>
#include <stdlib.h>
#include <cstdint>
#include <functional>
#include <new>
#include <queue>
#include <vector>

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

/* Nodes of released tasks, kept for the next park instead of being freed: with several workers about half of the
 * tasks pass through the list, and a malloc and a free for each showed in the coordinator's profile. At most
 * LA_PENDING_SPARE_MAX are kept; la_pending_clear frees them. */
#define LA_PENDING_SPARE_MAX 4096
static LA_PENDING_NODE *la_Pending_spare = NULL;
static int la_Pending_spare_count = 0;

/* hold_lsa of the parked tasks, as a multiset kept in two min-heaps: every parked hold_lsa is pushed to
 * la_Pending_hold_added and pushed again to la_Pending_hold_removed when its task leaves the list. The lowest parked
 * one is the top of the first heap once equal tops have been dropped from both. When a heap cannot grow, the heaps
 * are marked stale and rebuilt from the list on the next question. */
typedef std::priority_queue<std::int64_t, std::vector<std::int64_t>, std::greater<std::int64_t>> LA_PENDING_HOLD_HEAP;
static LA_PENDING_HOLD_HEAP la_Pending_hold_added;
static LA_PENDING_HOLD_HEAP la_Pending_hold_removed;
static bool la_Pending_hold_stale = false;

static void la_pending_put_spare (LA_PENDING_NODE *node);
static void la_pending_take_out (LA_PENDING_NODE *prev, LA_PENDING_NODE *cur);
static void la_pending_note_ready (LA_TASK *task);
static void la_pending_hold_push (LA_PENDING_HOLD_HEAP &heap, const LA_TASK *task);
static void la_pending_hold_reset (void);

void
la_pending_reset (void)
{
  assert (la_Pending_head == NULL && la_Pending_tail == NULL);
  la_Pending_count = 0;
  la_Pending_max_count = 0;
  la_pending_hold_reset ();
}

/*
 * la_pending_push() - park a task waiting for its dependency or task-queue capacity
 *   return: NO_ERROR or ER_OUT_OF_VIRTUAL_MEMORY
 *   task(in): task to park; copied
 */
int
la_pending_push (const LA_TASK *task)
{
  LA_PENDING_NODE *node;

  assert (task != NULL);

  if (la_Pending_spare != NULL)
    {
      node = la_Pending_spare;
      la_Pending_spare = node->next;
      la_Pending_spare_count--;
    }
  else
    {
      node = (LA_PENDING_NODE *) malloc (sizeof (LA_PENDING_NODE));
      if (node == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (LA_PENDING_NODE));
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}
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
  la_pending_hold_push (la_Pending_hold_added, &node->task);

  return NO_ERROR;
}

/*
 * la_pending_pop_routable() - take the oldest parked task that may run now, and the worker it goes to
 *   return: true when a task was released, false when every parked task still waits
 *   task(out): the released task
 *   worker(out): the worker chosen by route
 *   route(in): decides for one parked task whether it may run now and on which worker
 *
 * Note: scans in parking order, so released tasks keep their relative order, while a task that still waits
 *       does not hold back a later one that may run.
 */
bool
la_pending_pop_routable (LA_TASK *task, int *worker, bool (*route) (const LA_TASK *task, int *worker))
{
  LA_PENDING_NODE *prev = NULL;
  LA_PENDING_NODE *cur;

  assert (task != NULL && worker != NULL && route != NULL);

  for (cur = la_Pending_head; cur != NULL; prev = cur, cur = cur->next)
    {
      la_pending_note_ready (&cur->task);
      if (!route (&cur->task, worker))
	{
	  continue;
	}

      *task = cur->task;
      la_pending_take_out (prev, cur);

      return true;
    }

  return false;
}

/*
 * la_pending_release_routable() - release, in one pass in parking order, every parked task that may run now
 *   return: NO_ERROR, or the error of release (the task it failed on is already out of the list)
 *   route(in): decides for one parked task whether it may run now and on which worker
 *   can_route(in): false when no task can be routed now (every worker queue is full); the pass stops there
 *   release(in): dispatches one released task to its worker
 *
 *   behind_parked(out): set when a task was left without asking route because it depends on a task the pass left
 *                       parked before it; such a task waits for the dispatch of that earlier task
 *
 * Note: one pass gives the same result as taking one task at a time from the head: a parked task waits only for
 *       earlier tasks (they come before it in the list and are released first in the pass), for completions (none
 *       arrive during the pass), or for queue room (a release only uses room up). Starting again from the head
 *       after every release scanned the tasks still waiting at the front once per released task.
 *
 *       A parked task is on the commit-order ring and has no worker. Every unit up to a task's dependency must be
 *       applied or on the one worker it follows, so a task whose dependency is at or past the first task the pass
 *       leaves parked cannot run before that one gets a worker; it is passed over with one comparison instead of
 *       a route. Under dependencies that park most tasks, routing every one of them on every pass was most of the
 *       coordinator's time.
 */
int
la_pending_release_routable (bool (*route) (const LA_TASK *task, int *worker), bool (*can_route) (void),
			     int (*release) (LA_TASK *task, int worker), bool *behind_parked)
{
  LA_PENDING_NODE *prev = NULL;
  LA_PENDING_NODE *cur = la_Pending_head;
  LA_PENDING_NODE *next;
  const LOG_LSA *first_left = NULL;	/* commit position of the first task this pass leaves parked */
  LA_TASK task;
  int worker;
  int error;

  assert (route != NULL && can_route != NULL && release != NULL && behind_parked != NULL);

  *behind_parked = false;
  /* room only shrinks during a pass (releases use it up), so it is asked again only after a release */
  bool room = can_route ();

  while (cur != NULL && room)
    {
      next = cur->next;

      if (first_left != NULL && !LSA_ISNULL (&cur->task.dependency_lsa)
	  && LSA_GE (&cur->task.dependency_lsa, first_left))
	{
	  *behind_parked = true;
	  prev = cur;
	  cur = next;
	  continue;
	}

      la_pending_note_ready (&cur->task);
      if (!route (&cur->task, &worker))
	{
	  if (first_left == NULL)
	    {
	      first_left = &cur->task.commit_lsa;
	    }
	  prev = cur;
	  cur = next;
	  continue;
	}

      task = cur->task;
      la_pending_take_out (prev, cur);

      error = release (&task, worker);
      if (error != NO_ERROR)
	{
	  return error;
	}

      room = can_route ();
      cur = next;
    }

  return NO_ERROR;
}

/*
 * la_pending_clear() - drop every parked task
 *
 * Note:
 *   Called when the applier shuts down. applylogdb restarts the applier inside the same process after
 *   the server goes down, and a parked task left on the list would point at an apply slot freed by the
 *   shutdown: the next run would read its start_lsa as required_lsa and could dispatch it to a worker.
 *   The tasks are not lost: their apply units lie after committed_lsa and the next run reads them again.
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
      assert (la_Pending_count > 0);
      la_Pending_count--;
    }
  la_Pending_tail = NULL;

  while (la_Pending_spare != NULL)
    {
      node = la_Pending_spare;
      la_Pending_spare = node->next;
      free_and_init (node);
    }
  la_Pending_spare_count = 0;
  la_pending_hold_reset ();
}

/*
 * la_pending_get_lowest_hold_lsa() - the lowest hold_lsa among the parked tasks
 *   lowest(out): that LSA, NULL when no parked task holds a log position
 */
void
la_pending_get_lowest_hold_lsa (LOG_LSA *lowest)
{
  LA_PENDING_NODE *node;

  assert (lowest != NULL);

  if (la_Pending_hold_stale)
    {
      /* a heap could not grow: answer from the list, and rebuild the heaps from it for the next question */
      LSA_SET_NULL (lowest);
      la_Pending_hold_stale = false;
      la_Pending_hold_added = LA_PENDING_HOLD_HEAP ();
      la_Pending_hold_removed = LA_PENDING_HOLD_HEAP ();
      for (node = la_Pending_head; node != NULL; node = node->next)
	{
	  if (!LSA_ISNULL (&node->task.hold_lsa) && (LSA_ISNULL (lowest) || LSA_LT (&node->task.hold_lsa, lowest)))
	    {
	      LSA_COPY (lowest, &node->task.hold_lsa);
	    }
	  la_pending_hold_push (la_Pending_hold_added, &node->task);
	}
      return;
    }

  while (!la_Pending_hold_removed.empty () && !la_Pending_hold_added.empty ()
	 && la_Pending_hold_added.top () == la_Pending_hold_removed.top ())
    {
      la_Pending_hold_added.pop ();
      la_Pending_hold_removed.pop ();
    }

  if (la_Pending_hold_added.empty ())
    {
      LSA_SET_NULL (lowest);
      return;
    }

  lowest->pageid = (decltype (lowest->pageid)) (la_Pending_hold_added.top () / 65536);
  lowest->offset = (decltype (lowest->offset)) (la_Pending_hold_added.top () % 65536);
}

/*
 * la_pending_take_out() - unlink a node, drop its hold_lsa from the lowest-hold heaps and keep it as a spare
 *   prev(in): the node before cur, NULL when cur is the head
 *   cur(in): the node to take out; its task must have been copied out already
 */
static void
la_pending_take_out (LA_PENDING_NODE *prev, LA_PENDING_NODE *cur)
{
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

  la_pending_hold_push (la_Pending_hold_removed, &cur->task);
  la_pending_put_spare (cur);
  assert (la_Pending_count > 0);
  la_Pending_count--;
}

/*
 * la_pending_note_ready() - timestamp a parked task whose dependency has become satisfied
 *   task(in/out): a task a release pass visits
 *
 * Note: readiness keeps dependency wait apart from worker-capacity wait in the statistics. It is recorded only on
 *       the tasks a pass visits: a task past the point where the pass stopped gets it on a later pass, so part of
 *       its capacity wait may be counted as dependency wait. Walking the whole list for it on every pass took most
 *       of the coordinator's CPU when hundreds of thousands of tasks were parked.
 */
static void
la_pending_note_ready (LA_TASK *task)
{
  if (task->ready_ns == 0 && la_gate_is_satisfied (&task->dependency_lsa))
    {
      task->ready_ns = la_monotonic_now_ns ();
    }
}

/*
 * la_pending_hold_push() - add a task's hold_lsa to one of the lowest-hold heaps
 *   heap(in/out): la_Pending_hold_added or la_Pending_hold_removed
 *   task(in): the task
 */
static void
la_pending_hold_push (LA_PENDING_HOLD_HEAP &heap, const LA_TASK *task)
{
  if (la_Pending_hold_stale || LSA_ISNULL (&task->hold_lsa))
    {
      return;
    }

  try
    {
      heap.push ((std::int64_t) task->hold_lsa.pageid * 65536 + (std::int64_t) (task->hold_lsa.offset & 0xffff));
    }
  catch (const std::bad_alloc &)
    {
      /* rebuilt from the list when the lowest is asked for next */
      la_Pending_hold_stale = true;
    }
}

/*
 * la_pending_hold_reset() - empty the lowest-hold heaps
 */
static void
la_pending_hold_reset (void)
{
  la_Pending_hold_added = LA_PENDING_HOLD_HEAP ();
  la_Pending_hold_removed = LA_PENDING_HOLD_HEAP ();
  la_Pending_hold_stale = false;
}

/*
 * la_pending_put_spare() - keep a released node for the next park, or free it when enough are kept
 *   node(in): a node already out of the list
 */
static void
la_pending_put_spare (LA_PENDING_NODE *node)
{
  if (la_Pending_spare_count >= LA_PENDING_SPARE_MAX)
    {
      free_and_init (node);
      return;
    }

  node->next = la_Pending_spare;
  la_Pending_spare = node;
  la_Pending_spare_count++;
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
