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
 * log_applier_gate.cpp - implementation of dependency and completion-frontier tracking
 *
 * The gate compares an apply unit's dependency_lsa with the gap-free frontier for parallel dispatch.
 * Dependencies cover key conflicts and conservative ordering boundaries such as commit-order fallback.
 * The ring records COMMIT and independent SYSOP_END positions in log order; later completions cannot
 * move the frontier past an unfinished earlier unit. Tasks waiting for their dependency stay pending.
 * Only the coordinator touches the gate, so it has no lock.
 */

#include "config.h"

#include "log_applier_gate.hpp"
#include "log_applier_debug.hpp"	/* LA_DEBUG_LOG */
#include "error_manager.h"
#include "memory_alloc.h"	/* free_and_init */
#include "system_parameter.h"	/* PRM_ID_ER_LOG_DEBUG, read by LA_DEBUG_LOG */
#include "porting.h"

#include <assert.h>
#include <stdlib.h>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

/* Commit-order ring entry: one COMMIT or independent SYSOP_END, possibly completed out of order. */
typedef struct la_gate_order LA_GATE_ORDER;
struct la_gate_order
{
  LOG_LSA commit_lsa;
  bool completed;
};

/* Gap-free completion frontier: the last COMMIT or independent SYSOP_END through which every apply unit
 * is applied. A later unit finishing first does not advance this boundary. */
static LOG_LSA la_Gate_frontier = LSA_INITIALIZER;	/* NULL until the completion baseline is set */

/* Commit-order ring: apply units the frontier has not passed, oldest first. COMMIT and independent
 * SYSOP_END positions are pushed in log order. The coordinator stops reading while the ring is full. */
static LA_GATE_ORDER *la_Gate_order_ring = NULL;
static int la_Gate_order_capacity = 0;
static int la_Gate_order_head = 0;
static int la_Gate_order_count = 0;

static LA_GATE_ORDER *la_gate_order_at (int pos);
static LA_GATE_ORDER *la_gate_order_find (const LOG_LSA *commit_lsa);

/*
 * la_gate_init() - reset the gate before the workers start
 *   return: NO_ERROR, or ER_OUT_OF_VIRTUAL_MEMORY when the commit-order ring cannot be allocated
 *   commit_order_capacity(in): size of the commit-order ring (see LA_COMMIT_ORDER_CAPACITY)
 *
 * Note:
 *   The frontier is cleared as well. applylogdb restarts the applier inside the same process after the
 *   server goes down; a frontier kept from the previous run usually lies past the committed_lsa the new
 *   run restarts from, and la_gate_set_initial_frontier would keep it. Apply units read again after
 *   committed_lsa would then pass the gate on predecessors this run has not applied yet. The ring of that
 *   run may also still hold apply units, so it is emptied here rather than asserted empty.
 */
int
la_gate_init (int commit_order_capacity)
{
  size_t ring_size = (size_t) commit_order_capacity * sizeof (LA_GATE_ORDER);

  assert (commit_order_capacity > 0);

  if (la_Gate_order_ring != NULL)
    {
      free_and_init (la_Gate_order_ring);
    }

  la_Gate_order_ring = (LA_GATE_ORDER *) malloc (ring_size);
  if (la_Gate_order_ring == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, ring_size);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  la_Gate_order_capacity = commit_order_capacity;
  la_Gate_order_head = 0;
  la_Gate_order_count = 0;
  LSA_SET_NULL (&la_Gate_frontier);

  return NO_ERROR;
}

/*
 * la_gate_order_get_count() - number of apply units the frontier has not passed yet, finished or not
 */
int
la_gate_order_get_count (void)
{
  return la_Gate_order_count;
}

/*
 * la_gate_set_initial_frontier() - set the frontier to the restart point unless it is already set
 *   restart_lsa(in): restart point; everything at or below it was applied by the previous run
 *
 * Note:
 *   Apply units at or below the restart point never enter the commit-order ring. Starting the frontier
 *   there lets a task that depends on one of them proceed instead of waiting forever.
 *   Called before each apply unit is pushed on the commit-order ring.
 */
void
la_gate_set_initial_frontier (const LOG_LSA *restart_lsa)
{
  assert (restart_lsa != NULL);

  if (!LSA_ISNULL (&la_Gate_frontier))
    {
      return;
    }

  LSA_COPY (&la_Gate_frontier, restart_lsa);
}

/*
 * la_gate_get_frontier() - read the frontier
 *   return: true if the completion baseline is known, false otherwise
 *   frontier(out): the frontier
 */
bool
la_gate_get_frontier (LOG_LSA *frontier)
{
  assert (frontier != NULL);

  if (LSA_ISNULL (&la_Gate_frontier))
    {
      return false;
    }

  LSA_COPY (frontier, &la_Gate_frontier);

  return true;
}

/*
 * la_gate_get_order_tail() - read the latest apply-unit position on the commit-order ring
 *   return: true if the ring holds an apply unit, false when it is empty
 *   commit_lsa(out): COMMIT or independent SYSOP_END position of the last unit on the ring
 */
bool
la_gate_get_order_tail (LOG_LSA *commit_lsa)
{
  assert (commit_lsa != NULL);

  if (la_Gate_order_count == 0)
    {
      return false;
    }

  LSA_COPY (commit_lsa, &la_gate_order_at (la_Gate_order_count - 1)->commit_lsa);

  return true;
}

/*
 * la_gate_bound_dependency() - bound dependency_lsa by the latest previously read apply unit
 *   return: true if dependency_lsa was lowered
 *   dependency_lsa(in/out): dependency position decoded from LOG_DUMMY_WSET_LABEL
 *
 * Note:
 *   Called before the current unit enters the ring. After a master restart, dependency_lsa can point
 *   to the recovered-log end rather than a COMMIT or independent SYSOP_END. The frontier could reach
 *   that position only after the current unit completed, making that unit wait for itself.
 *   Logs are read in order, so no earlier apply unit lies between the latest previously read unit and
 *   a dependency above it. Bound it by that unit's position (the ring tail, or frontier when empty).
 */
bool
la_gate_bound_dependency (LOG_LSA *dependency_lsa)
{
  LOG_LSA previous;

  assert (dependency_lsa != NULL);

  if (LSA_ISNULL (dependency_lsa))
    {
      return false;
    }

  if (!la_gate_get_order_tail (&previous) && !la_gate_get_frontier (&previous))
    {
      return false;
    }

  if (!LSA_GT (dependency_lsa, &previous))
    {
      return false;
    }

  LSA_COPY (dependency_lsa, &previous);

  return true;
}

/*
 * la_gate_order_push() - record an apply unit read from the log on the commit-order ring
 *   return: NO_ERROR, or ER_FAILED when the ring is full
 *   commit_lsa(in): COMMIT or independent SYSOP_END position; above every unit pushed before it
 *
 * Note:
 *   The ring cannot actually fill (see la_Gate_order_ring); the failure guards against a counting bug.
 */
int
la_gate_order_push (const LOG_LSA *commit_lsa)
{
  LA_GATE_ORDER *entry;

  assert (la_Gate_order_ring != NULL);

  if (la_Gate_order_count >= la_Gate_order_capacity)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HA_GENERIC_ERROR, 1,
	      "writeset apply gate: commit-order ring full");
      return ER_FAILED;
    }

  assert (la_Gate_order_count == 0 || LSA_GT (commit_lsa, &la_gate_order_at (la_Gate_order_count - 1)->commit_lsa));

  entry = la_gate_order_at (la_Gate_order_count);
  LSA_COPY (&entry->commit_lsa, commit_lsa);
  entry->completed = false;
  la_Gate_order_count++;

  return NO_ERROR;
}

/*
 * la_gate_advance_frontier() - advance the frontier over the completed head of the commit-order ring
 *
 * Note:
 *   Stops at the first apply unit that is not applied yet, so the frontier never skips an unfinished unit
 *   even when later ones have finished.
 */
void
la_gate_advance_frontier (void)
{
  int advanced = 0;

  while (la_Gate_order_count > 0 && la_Gate_order_ring[la_Gate_order_head].completed)
    {
      LSA_COPY (&la_Gate_frontier, &la_Gate_order_ring[la_Gate_order_head].commit_lsa);

      la_Gate_order_head = (la_Gate_order_head + 1) % la_Gate_order_capacity;
      la_Gate_order_count--;
      advanced++;
    }

  if (advanced > 0)
    {
      LA_DEBUG_LOG (ARG_FILE_LINE, "ws_gate frontier advanced by %d -> %lld|%d (ring=%d)\n", advanced,
		    LSA_AS_ARGS (&la_Gate_frontier), la_Gate_order_count);
    }
}

/*
 * la_gate_mark_completed() - record that an apply unit on the commit-order ring is applied
 *   commit_lsa(in): COMMIT or independent SYSOP_END position of the applied unit
 *
 * Note:
 *   An apply unit that is not on the ring (already behind the frontier) needs nothing and is ignored.
 */
void
la_gate_mark_completed (const LOG_LSA *commit_lsa)
{
  LA_GATE_ORDER *entry;

  assert (commit_lsa != NULL && !LSA_ISNULL (commit_lsa));

  entry = la_gate_order_find (commit_lsa);
  if (entry != NULL)
    {
      entry->completed = true;
    }
}

/*
 * la_gate_is_satisfied() - check whether an apply unit's dependency is satisfied
 *   return: true if all preceding work through dependency_lsa is applied, false otherwise
 *   dependency_lsa(in): dependency position decoded from LOG_DUMMY_WSET_LABEL
 *
 * Note:
 *   Passes when dependency_lsa is NULL or dependency_lsa <= frontier. Completing only the unit at
 *   dependency_lsa does not prove that all earlier predecessors finished. This also applies to a
 *   REF-derived maximum over parallel referencers, so the gate uses the gap-free frontier regardless
 *   of which history slot supplied the dependency.
 *
 *   TODO: Verify parent/child ordering under this rule with dedicated tests: children of one parent run
 *         in parallel once the parent is applied, a parent change after its children waits for all of
 *         them (parent -> child -> parent), and each foreign-key action (RESTRICT, CASCADE, SET NULL)
 *         gives the same slave result as the master.
 */
bool
la_gate_is_satisfied (const LOG_LSA *dependency_lsa)
{
  assert (dependency_lsa != NULL);

  if (LSA_ISNULL (dependency_lsa))
    {
      return true;
    }

  return !LSA_ISNULL (&la_Gate_frontier) && LSA_LE (dependency_lsa, &la_Gate_frontier);
}

/*
 * la_gate_order_at() - entry of the commit-order ring by position from the oldest
 *   pos(in): 0 for the oldest apply unit on the ring, la_Gate_order_count - 1 for the latest
 */
static LA_GATE_ORDER *
la_gate_order_at (int pos)
{
  int slot = (la_Gate_order_head + pos) % la_Gate_order_capacity;

  assert (pos >= 0 && pos < la_Gate_order_capacity);

  return &la_Gate_order_ring[slot];
}

/*
 * la_gate_order_find() - look up an apply unit on the commit-order ring
 *   return: the ring entry, or NULL when the unit is not on the ring
 *   commit_lsa(in): COMMIT or independent SYSOP_END position to look for
 *
 * Note:
 *   The ring is sorted by apply-unit position, so a binary search finds it.
 */
static LA_GATE_ORDER *
la_gate_order_find (const LOG_LSA *commit_lsa)
{
  int low = 0;
  int high = la_Gate_order_count - 1;

  while (low <= high)
    {
      int mid = low + (high - low) / 2;
      LA_GATE_ORDER *entry = la_gate_order_at (mid);

      if (LSA_EQ (&entry->commit_lsa, commit_lsa))
	{
	  return entry;
	}
      if (LSA_LT (&entry->commit_lsa, commit_lsa))
	{
	  low = mid + 1;
	}
      else
	{
	  high = mid - 1;
	}
    }

  return NULL;
}
