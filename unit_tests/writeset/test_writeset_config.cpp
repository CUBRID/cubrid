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
 * test_wset_config.cpp - the ha_writeset_history_size parameter really
 *                                drives the history's two capacity limits.
 *
 * initialize() caches the parameter into wset_History.history_size once,
 * and that single value bounds both the map (a flush clears the whole map when
 * publishing would push it past the limit) and each transaction (collecting past
 * the limit drops the writeset and degrades to commit order). Injecting a small
 * limit lets both paths be driven deterministically with a handful of keys,
 * which is what replaces the earlier millions-of-keys capacity stress sweep.
 */

#include "test_wset_common.hpp"

using namespace wstest;

TEST_CASE ("config: initialize caches ha_writeset_history_size", "[writeset]")
{
  ws_history_guard history (4242);
  REQUIRE (wset_History.history_size == 4242);
}

TEST_CASE ("config: map reaching capacity clears history and jumps the floor", "[writeset]")
{
  const int cap = 100;
  ws_history_guard history (cap);
  OID cls = oid_of (1, 100, 1);

  /* first commit stays under the cap: it publishes normally, the floor is still
   * unset because nothing has been evicted yet */
  log_tdes *t1 = make_tdes (1);
  for (int i = 0; i < 60; i++)
    {
      add_write_int (t1, &cls, i);
    }
  LOG_LSA l1 = lsa_of (10, 0);
  wset_publish_commit_to_history (t1, &l1);
  REQUIRE (wset_History.map.size () == (size_t) 60);
  REQUIRE (LSA_ISNULL (&wset_History.history_start));

  /* second commit would push map.size() + publish_count (60 + 60) over the cap:
   * the flush clears the whole map and raises the floor to this commit before
   * publishing, so only this transaction's keys remain */
  log_tdes *t2 = make_tdes (2);
  for (int i = 100; i < 160; i++)	/* disjoint from t1's keys */
    {
      add_write_int (t2, &cls, i);
    }
  LOG_LSA l2 = lsa_of (20, 0);
  wset_publish_commit_to_history (t2, &l2);
  REQUIRE (wset_History.map.size () == (size_t) 60);
  REQUIRE (LSA_EQ (&wset_History.history_start, &l2));

  /* a key evicted by the clear must not come back as independent: the floor
   * keeps a new writer of it serialized behind the clear commit */
  log_tdes *t3 = make_tdes (3);
  add_write_int (t3, &cls, 0);	/* key 0 belonged to t1, wiped by the clear */
  LOG_LSA dep = probe (t3);
  REQUIRE (LSA_EQ (&dep, &l2));

  delete t1;
  delete t2;
  delete t3;
}

TEST_CASE ("config: a transaction exceeding the key limit overflows and drops its writeset", "[writeset]")
{
  /* the per-transaction limit is the same cached ha_writeset_history_size; a
   * transaction overflows on the key whose arrival makes the collected count
   * reach the limit, and its already-collected keys are dropped */
  const int cap = 8;
  ws_history_guard history (cap);
  OID cls = oid_of (1, 100, 1);
  log_tdes *tx = make_tdes (1);

  int added = 0;
  while (!tx->wset_overflow)
    {
      add_write_int (tx, &cls, added++);
    }
  REQUIRE (added == cap + 1);
  REQUIRE (tx->wset_overflow);
  REQUIRE (tx->wset_hashes.empty ());

  /* an overflowed transaction stays overflowed: further keys are ignored */
  add_write_int (tx, &cls, 10000);
  REQUIRE (tx->wset_overflow);
  REQUIRE (tx->wset_hashes.empty ());

  delete tx;
}
