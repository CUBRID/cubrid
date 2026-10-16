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
 * test_log_applier_pending.cpp - pending list of tasks parked until the gate passes their dependency
 */

#include "catch2/catch.hpp"

#include "test_log_applier_common.hpp"
#include "log_applier_gate.hpp"
#include "log_applier_pending.hpp"

#include "error_code.h"

#include <vector>

static void
test_collect_tranid (const LA_TASK *task, void *arg)
{
  ((std::vector<int> *) arg)->push_back (task->tranid);
}

static void
test_reset (const LOG_LSA *restart_lsa)
{
  la_pending_clear ();
  REQUIRE (la_gate_init (8) == NO_ERROR);
  la_gate_set_initial_frontier (restart_lsa);
}

TEST_CASE ("pending list releases ready tasks in parking order without waiting for an earlier blocked one",
	   "[log_applier_pending]")
{
  LOG_LSA l10 = test_lsa (10), l20 = test_lsa (20), l5 = test_lsa (5), nul = test_null_lsa ();
  LA_TASK out;

  test_reset (&l10);
  REQUIRE (la_gate_order_push (&l20) == NO_ERROR);	/* the predecessor of task 1, still running */

  LA_TASK a = test_task (1, test_lsa (30), l20);
  LA_TASK b = test_task (2, test_lsa (31), l5);
  LA_TASK c = test_task (3, test_lsa (32), nul);
  REQUIRE (la_pending_push (&a) == NO_ERROR);
  REQUIRE (la_pending_push (&b) == NO_ERROR);
  REQUIRE (la_pending_push (&c) == NO_ERROR);

  REQUIRE (la_pending_pop_ready (&out));
  REQUIRE (out.tranid == 2);
  REQUIRE (la_pending_pop_ready (&out));
  REQUIRE (out.tranid == 3);
  REQUIRE_FALSE (la_pending_pop_ready (&out));
  REQUIRE_FALSE (la_pending_is_empty ());

  la_gate_mark_completed (&l20);
  la_gate_advance_frontier ();
  REQUIRE (la_pending_pop_ready (&out));
  REQUIRE (out.tranid == 1);
  REQUIRE (LSA_EQ (&out.dependency_seq, &l20));
  REQUIRE (la_pending_is_empty ());
}

TEST_CASE ("pending list visits parked tasks oldest first", "[log_applier_pending]")
{
  LOG_LSA l10 = test_lsa (10);
  std::vector<int> seen;

  test_reset (&l10);
  for (int t = 1; t <= 4; t++)
    {
      LA_TASK task = test_task (t, test_lsa (100 + t), test_lsa (90));
      REQUIRE (la_pending_push (&task) == NO_ERROR);
    }

  la_pending_foreach (test_collect_tranid, &seen);
  REQUIRE (seen == std::vector<int> ({1, 2, 3, 4}));
  la_pending_clear ();
}

TEST_CASE ("pending list clear drops every parked task and the list is usable again", "[log_applier_pending]")
{
  LOG_LSA l10 = test_lsa (10), nul = test_null_lsa ();
  LA_TASK out;
  std::vector<int> seen;

  /* an in-process restart drops the tasks a previous run parked: their apply slots are gone */
  test_reset (&l10);
  for (int t = 1; t <= 3; t++)
    {
      LA_TASK task = test_task (t, test_lsa (100 + t), test_lsa (90));
      REQUIRE (la_pending_push (&task) == NO_ERROR);
    }
  la_pending_clear ();
  REQUIRE (la_pending_is_empty ());
  REQUIRE_FALSE (la_pending_pop_ready (&out));
  la_pending_foreach (test_collect_tranid, &seen);
  REQUIRE (seen.empty ());

  LA_TASK again = test_task (9, test_lsa (200), nul);
  REQUIRE (la_pending_push (&again) == NO_ERROR);
  REQUIRE (la_pending_pop_ready (&out));
  REQUIRE (out.tranid == 9);
  REQUIRE (la_pending_is_empty ());
}
