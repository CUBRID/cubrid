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
 * log_applier_gate.cpp - dependency gate that holds a transaction back until what it conflicts with is applied
 *
 * The master labels each transaction with the commit LSA of the latest earlier transaction it conflicts
 * with. Workers running in parallel would still finish conflicting transactions out of order, so the gate
 * holds a transaction back until the gap-free frontier has reached its label (see la_gate_is_satisfied).
 * Only conflict order is enforced: independent transactions are dispatched at once and commute, so the slave
 * converges on the master without keeping commit order. A transaction held back waits in the pending list
 * (log_applier_pending.cpp) until this gate lets it pass.
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

/* Commit-order ring entry: one commit read from the log. completed is set when it is applied, possibly out of
 * order. */
typedef struct la_gate_order LA_GATE_ORDER;
struct la_gate_order
{
  LOG_LSA commit_lsa;
  bool completed;
};

/* Gap-free completion frontier: every commit at or below it is applied. */
static LOG_LSA la_Gate_frontier = LSA_INITIALIZER;	/* NULL until the first commit starts it */

/* Commit-order ring: commits read from the log that the frontier has not passed, oldest first. Commits are
 * pushed in log order, so the ring is sorted by commit LSA. The coordinator stops reading the log while the
 * ring is full (LA_COMMIT_ORDER_CAPACITY). */
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
 *   run restarts from, and la_gate_set_initial_frontier would keep it. Transactions re-applied from
 *   committed_lsa would then pass the gate on predecessors this run has not applied yet. The ring of that
 *   run may also still hold commits, so it is emptied here rather than asserted empty.
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
 * la_gate_order_get_count() - number of commits the frontier has not passed yet, finished or not
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
 *   Commits at or below the restart point never enter the commit-order ring. Starting the frontier
 *   there lets a transaction that depends on one of them be dispatched instead of waiting forever.
 *   Called before every commit is pushed on the commit-order ring.
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
 *   return: true if the frontier is known, false before the first commit starts it
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
 * la_gate_get_order_tail() - read the latest commit on the commit-order ring
 *   return: true if the ring holds a commit, false when it is empty
 *   commit_lsa(out): commit LSA of the last commit on the ring
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
 * la_gate_bound_dependency() - lower a label that lies above the latest commit read to that commit
 *   return: true if the label was lowered
 *   dep(in/out): the transaction's dependency_seq, the master's label
 *
 * Note:
 *   Called for a transaction before its own commit is pushed on the commit-order ring.
 *   A label means that every commit at or below it is applied, and it is normally a commit LSA. When the
 *   master restarts, it raises the floor of its labels to the end of its recovered log, which is no commit.
 *   The frontier only stops at commits, so it could pass such a label only after the next commit, the
 *   labelled transaction itself, is applied, and that transaction would wait for itself.
 *   The log is read in order and a label lies below its own commit, so no commit lies between the latest
 *   commit read and a label above it. Lowering the label to that commit keeps its meaning. The latest
 *   commit read is the tail of the ring, or the frontier when the ring is empty. A label that is a commit
 *   is never above it and is left as is.
 */
bool
la_gate_bound_dependency (LOG_LSA *dep)
{
  LOG_LSA previous;

  assert (dep != NULL);

  if (LSA_ISNULL (dep))
    {
      return false;
    }

  if (!la_gate_get_order_tail (&previous) && !la_gate_get_frontier (&previous))
    {
      return false;
    }

  if (!LSA_GT (dep, &previous))
    {
      return false;
    }

  LSA_COPY (dep, &previous);

  return true;
}

/*
 * la_gate_order_push() - record a commit read from the log on the commit-order ring
 *   return: NO_ERROR, or ER_FAILED when the ring is full
 *   commit_lsa(in): commit LSA of the transaction; above every commit pushed before it
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
 *   Stops at the first commit that is not applied yet, so the frontier never skips a running commit
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
		    (long long) la_Gate_frontier.pageid, (int) la_Gate_frontier.offset, la_Gate_order_count);
    }
}

/*
 * la_gate_mark_completed() - record that a commit on the commit-order ring is applied
 *   commit_seq(in): commit LSA of the applied transaction
 *
 * Note:
 *   A commit that is not on the ring (already behind the frontier) needs nothing and is ignored.
 */
void
la_gate_mark_completed (const LOG_LSA *commit_seq)
{
  LA_GATE_ORDER *entry;

  assert (commit_seq != NULL && !LSA_ISNULL (commit_seq));

  entry = la_gate_order_find (commit_seq);
  if (entry != NULL)
    {
      entry->completed = true;
    }
}

/*
 * la_gate_is_satisfied() - decide whether a transaction may be dispatched now
 *   return: true if everything the label stands for is applied, false if it must be parked in the pending list
 *   dep(in): the transaction's dependency_seq, the master's label
 *
 * Note:
 *   Passes only when dep is NULL (nothing earlier conflicts) or dep <= frontier (every commit up to
 *   dep is applied with no hole).
 *   The labeled predecessor having finished above the frontier is not a pass. The master reduces all
 *   of a transaction's conflicting predecessors to their maximum LSA, so dep names one predecessor
 *   while standing for the rest, and the others may still be running; passing would let the
 *   transaction run beside them on the same rows. A read-slot label is no different: it names only
 *   the last of several referencers running in parallel, so the gate does not need to know which slot
 *   the label came from.
 *
 *   TODO: Verify parent/child ordering under this rule with dedicated tests: children of one parent run
 *         in parallel once the parent is applied, a parent change after its children waits for all of
 *         them (parent -> child -> parent), and each foreign-key action (RESTRICT, CASCADE, SET NULL)
 *         gives the same slave result as the master.
 */
bool
la_gate_is_satisfied (const LOG_LSA *dep)
{
  assert (dep != NULL);

  if (LSA_ISNULL (dep))
    {
      return true;
    }

  return !LSA_ISNULL (&la_Gate_frontier) && LSA_LE (dep, &la_Gate_frontier);
}

/*
 * la_gate_order_at() - entry of the commit-order ring by position from the oldest
 *   pos(in): 0 for the oldest commit on the ring, la_Gate_order_count - 1 for the latest
 */
static LA_GATE_ORDER *
la_gate_order_at (int pos)
{
  int slot = (la_Gate_order_head + pos) % la_Gate_order_capacity;

  assert (pos >= 0 && pos < la_Gate_order_capacity);

  return &la_Gate_order_ring[slot];
}

/*
 * la_gate_order_find() - look up a commit on the commit-order ring
 *   return: the ring entry, or NULL when the commit is not on the ring
 *   commit_lsa(in): commit LSA to look for
 *
 * Note:
 *   The ring is sorted by commit LSA, so a binary search finds it.
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
