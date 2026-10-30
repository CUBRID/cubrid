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
 * test_log_applier_gate.cpp - commit-order ring and frontier that decide when a dependent task may run
 */

#include "catch2/catch.hpp"

#include "test_log_applier_common.hpp"
#include "log_applier_gate.hpp"

#include "error_code.h"

TEST_CASE ("gate passes a task without dependency and holds others until the frontier is set",
	   "[log_applier_gate]")
{
  LOG_LSA nul = test_null_lsa ();
  LOG_LSA l10 = test_lsa (10);
  LOG_LSA frontier;

  REQUIRE (la_gate_init (8) == NO_ERROR);
  REQUIRE_FALSE (la_gate_get_frontier (&frontier));
  REQUIRE (la_gate_is_satisfied (&nul));
  REQUIRE_FALSE (la_gate_is_satisfied (&l10));
}

TEST_CASE ("gate starts the frontier at the restart point and keeps it", "[log_applier_gate]")
{
  LOG_LSA l10 = test_lsa (10), l9 = test_lsa (9), l11 = test_lsa (11), l50 = test_lsa (50);
  LOG_LSA frontier;

  REQUIRE (la_gate_init (8) == NO_ERROR);
  la_gate_set_initial_frontier (&l10);
  REQUIRE (la_gate_get_frontier (&frontier));
  REQUIRE (LSA_EQ (&frontier, &l10));
  REQUIRE (la_gate_is_satisfied (&l9));
  REQUIRE (la_gate_is_satisfied (&l10));
  REQUIRE_FALSE (la_gate_is_satisfied (&l11));

  /* called before every commit; only the first call sets it */
  la_gate_set_initial_frontier (&l50);
  REQUIRE (la_gate_get_frontier (&frontier));
  REQUIRE (LSA_EQ (&frontier, &l10));
}

TEST_CASE ("gate advances the frontier only over a gap-free prefix of completed commits", "[log_applier_gate]")
{
  LOG_LSA l10 = test_lsa (10), l20 = test_lsa (20), l30 = test_lsa (30), l40 = test_lsa (40);
  LOG_LSA frontier, tail;

  REQUIRE (la_gate_init (8) == NO_ERROR);
  la_gate_set_initial_frontier (&l10);
  REQUIRE (la_gate_order_push (&l20) == NO_ERROR);
  REQUIRE (la_gate_order_push (&l30) == NO_ERROR);
  REQUIRE (la_gate_order_push (&l40) == NO_ERROR);
  REQUIRE (la_gate_order_get_count () == 3);
  REQUIRE (la_gate_get_order_tail (&tail));
  REQUIRE (LSA_EQ (&tail, &l40));

  /* the later two finish first: the frontier must not skip the running one */
  la_gate_mark_completed (&l30);
  la_gate_mark_completed (&l40);
  la_gate_advance_frontier ();
  REQUIRE (la_gate_get_frontier (&frontier));
  REQUIRE (LSA_EQ (&frontier, &l10));
  REQUIRE (la_gate_order_get_count () == 3);

  la_gate_mark_completed (&l20);
  la_gate_advance_frontier ();
  REQUIRE (la_gate_get_frontier (&frontier));
  REQUIRE (LSA_EQ (&frontier, &l40));
  REQUIRE (la_gate_order_get_count () == 0);
  REQUIRE_FALSE (la_gate_get_order_tail (&tail));
}

TEST_CASE ("gate does not pass a dependency that finished above the frontier", "[log_applier_gate]")
{
  LOG_LSA l10 = test_lsa (10), l20 = test_lsa (20), l30 = test_lsa (30);

  /* the master reduces all conflicting predecessors to their maximum LSA, so l30 also stands for l20 */
  REQUIRE (la_gate_init (8) == NO_ERROR);
  la_gate_set_initial_frontier (&l10);
  REQUIRE (la_gate_order_push (&l20) == NO_ERROR);
  REQUIRE (la_gate_order_push (&l30) == NO_ERROR);

  la_gate_mark_completed (&l30);
  la_gate_advance_frontier ();
  REQUIRE_FALSE (la_gate_is_satisfied (&l30));

  la_gate_mark_completed (&l20);
  la_gate_advance_frontier ();
  REQUIRE (la_gate_is_satisfied (&l30));
}

TEST_CASE ("gate ignores completion of a commit not on the ring", "[log_applier_gate]")
{
  LOG_LSA l10 = test_lsa (10), l5 = test_lsa (5), l20 = test_lsa (20), l25 = test_lsa (25);
  LOG_LSA frontier;

  REQUIRE (la_gate_init (8) == NO_ERROR);
  la_gate_set_initial_frontier (&l10);
  REQUIRE (la_gate_order_push (&l20) == NO_ERROR);

  la_gate_mark_completed (&l5);	/* behind the frontier, already applied by an earlier run */
  la_gate_mark_completed (&l25);	/* never pushed */
  la_gate_advance_frontier ();
  REQUIRE (la_gate_get_frontier (&frontier));
  REQUIRE (LSA_EQ (&frontier, &l10));
  REQUIRE (la_gate_order_get_count () == 1);
}

TEST_CASE ("gate passes the first commit after a master restart whose label is the restart floor",
	   "[log_applier_gate]")
{
  /* The master raises the floor of its labels to the end of its recovered log when it restarts. That position
   * is no commit, so it never reaches the ring. p is the last commit applied before the restart, r the floor,
   * c1 the first commit after the restart, labelled r because nothing it wrote conflicts. Every commit at or
   * below r is applied, so c1 must be dispatched; if it is parked, the frontier can only pass r once c1 itself
   * is applied, and replication stops without an error. The coordinator lowers the label before it pushes c1,
   * as la_coordinator_submit_task does. */
  LOG_LSA p = test_lsa (100), r = test_lsa (105), c1 = test_lsa (106, 200);
  LOG_LSA dep = r;
  LOG_LSA frontier;

  REQUIRE (la_gate_init (8) == NO_ERROR);
  la_gate_set_initial_frontier (&p);
  REQUIRE (la_gate_bound_dependency (&dep));
  REQUIRE (LSA_EQ (&dep, &p));
  REQUIRE (la_gate_order_push (&c1) == NO_ERROR);

  REQUIRE (la_gate_is_satisfied (&dep));

  /* the frontier itself is left where it was */
  REQUIRE (la_gate_get_frontier (&frontier));
  REQUIRE (LSA_EQ (&frontier, &p));
}

TEST_CASE ("gate lowers a restart-floor label to the latest commit on the ring and waits for it",
	   "[log_applier_gate]")
{
  /* the commits before the restart are still being applied when c1 is read: c1 must wait for them, and only
   * for them */
  LOG_LSA start = test_lsa (80), x = test_lsa (90), p = test_lsa (100), r = test_lsa (105);
  LOG_LSA c1 = test_lsa (106, 200);
  LOG_LSA dep = r;

  REQUIRE (la_gate_init (8) == NO_ERROR);
  la_gate_set_initial_frontier (&start);
  REQUIRE (la_gate_order_push (&x) == NO_ERROR);
  REQUIRE (la_gate_order_push (&p) == NO_ERROR);

  REQUIRE (la_gate_bound_dependency (&dep));
  REQUIRE (LSA_EQ (&dep, &p));
  REQUIRE (la_gate_order_push (&c1) == NO_ERROR);
  REQUIRE_FALSE (la_gate_is_satisfied (&dep));

  la_gate_mark_completed (&p);
  la_gate_advance_frontier ();
  REQUIRE_FALSE (la_gate_is_satisfied (&dep));

  la_gate_mark_completed (&x);
  la_gate_advance_frontier ();
  REQUIRE (la_gate_is_satisfied (&dep));
}

TEST_CASE ("gate keeps a label that is a commit already read", "[log_applier_gate]")
{
  LOG_LSA start = test_lsa (80), x = test_lsa (90), p = test_lsa (100);
  LOG_LSA dep_tail = p, dep_older = x, dep_frontier = start, dep_null = test_null_lsa ();

  REQUIRE (la_gate_init (8) == NO_ERROR);
  la_gate_set_initial_frontier (&start);
  REQUIRE (la_gate_order_push (&x) == NO_ERROR);
  REQUIRE (la_gate_order_push (&p) == NO_ERROR);

  REQUIRE_FALSE (la_gate_bound_dependency (&dep_tail));
  REQUIRE (LSA_EQ (&dep_tail, &p));
  REQUIRE_FALSE (la_gate_bound_dependency (&dep_older));
  REQUIRE (LSA_EQ (&dep_older, &x));
  REQUIRE_FALSE (la_gate_bound_dependency (&dep_frontier));
  REQUIRE (LSA_EQ (&dep_frontier, &start));
  REQUIRE_FALSE (la_gate_bound_dependency (&dep_null));
  REQUIRE (LSA_ISNULL (&dep_null));
}

TEST_CASE ("gate leaves a label as is before anything is read", "[log_applier_gate]")
{
  /* no frontier and an empty ring: there is no commit to lower the label to */
  LOG_LSA r = test_lsa (105);
  LOG_LSA dep = r;

  REQUIRE (la_gate_init (8) == NO_ERROR);
  REQUIRE_FALSE (la_gate_bound_dependency (&dep));
  REQUIRE (LSA_EQ (&dep, &r));
}

TEST_CASE ("gate refuses a commit beyond the ring capacity", "[log_applier_gate]")
{
  LOG_LSA l1 = test_lsa (1), l2 = test_lsa (2), l3 = test_lsa (3);

  REQUIRE (la_gate_init (2) == NO_ERROR);
  REQUIRE (la_gate_order_push (&l1) == NO_ERROR);
  REQUIRE (la_gate_order_push (&l2) == NO_ERROR);
  REQUIRE (la_gate_order_push (&l3) == ER_FAILED);
}

TEST_CASE ("gate init clears the frontier and the ring left by a previous run", "[log_applier_gate]")
{
  LOG_LSA l10 = test_lsa (10), l20 = test_lsa (20), l30 = test_lsa (30), l15 = test_lsa (15);
  LOG_LSA frontier;

  /* a previous run in the same process left a frontier past its restart point and an unfinished commit */
  REQUIRE (la_gate_init (8) == NO_ERROR);
  la_gate_set_initial_frontier (&l10);
  REQUIRE (la_gate_order_push (&l20) == NO_ERROR);
  la_gate_mark_completed (&l20);
  la_gate_advance_frontier ();
  REQUIRE (la_gate_order_push (&l30) == NO_ERROR);

  /* the next run restarts from an older committed_lsa: it must start there, not at the old frontier */
  REQUIRE (la_gate_init (8) == NO_ERROR);
  REQUIRE_FALSE (la_gate_get_frontier (&frontier));
  REQUIRE (la_gate_order_get_count () == 0);
  la_gate_set_initial_frontier (&l15);
  REQUIRE (la_gate_get_frontier (&frontier));
  REQUIRE (LSA_EQ (&frontier, &l15));
  REQUIRE_FALSE (la_gate_is_satisfied (&l20));
}
