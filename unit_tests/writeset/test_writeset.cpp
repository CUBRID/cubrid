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
 * test_writeset.cpp - behavior tests for the writeset collection and global
 *                         commit history.
 *
 * Behavior cases pin down the dependency rules the parallel slave relies on:
 * who waits behind whom, what is published into the history, and how overflow
 * degrades to commit order. The per-key micro-benchmarks live in
 * test_writeset_bench.cpp; the contention sweeps in
 * test_writeset_stress.cpp.
 */

#include "test_writeset_common.hpp"

#include <atomic>
#include <cerrno>
#include <chrono>

using namespace wstest;

static void
require_write_ref_same_hash (TP_DOMAIN *parent_domain, TP_DOMAIN *child_domain, DB_VALUE *source)
{
  OID parent_class = oid_of (1, 200, 1);
  DB_VALUE parent_value;
  DB_VALUE child_value;
  log_tdes *parent = make_tdes (1);
  log_tdes *child = make_tdes (2);

  REQUIRE (parent_domain != NULL);
  REQUIRE (child_domain != NULL);

  db_make_null (&parent_value);
  db_make_null (&child_value);

  REQUIRE (tp_value_cast (source, &parent_value, parent_domain, false) == DOMAIN_COMPATIBLE);
  REQUIRE (tp_value_cast (source, &child_value, child_domain, false) == DOMAIN_COMPATIBLE);

  REQUIRE (wset_add_write_key (parent, &parent_class, &WS_DEFAULT_VFID, &parent_value) == NO_ERROR);
  REQUIRE (wset_add_ref_key (child, &parent_class, &WS_DEFAULT_VFID, &child_value,
			     parent_domain) == NO_ERROR);

  REQUIRE (parent->wset_hashes.size () == 1);
  REQUIRE (child->wset_hashes.size () == 1);
  REQUIRE (parent->wset_hashes[0].kind == LOG_WSET_KIND_WRITE);
  REQUIRE (child->wset_hashes[0].kind == LOG_WSET_KIND_REF);
  REQUIRE (parent->wset_hashes[0].hash == child->wset_hashes[0].hash);

  pr_clear_value (&parent_value);
  pr_clear_value (&child_value);
  delete parent;
  delete child;
}

TEST_CASE ("parent WRITE and child REF produce the same collision hash", "[writeset]")
{
  ws_history_guard history;
  TP_DOMAIN *char_default = tp_domain_resolve_default (DB_TYPE_CHAR);
  TP_DOMAIN *char10;
  TP_DOMAIN *char20;
  TP_DOMAIN *numeric_10_2;
  TP_DOMAIN *numeric_15_4;
  DB_VALUE source;
  int collation_id;

  REQUIRE (char_default != NULL);
  collation_id = char_default->collation_id;
  char10 = tp_domain_resolve (DB_TYPE_CHAR, NULL, 10, 0, NULL, collation_id);
  char20 = tp_domain_resolve (DB_TYPE_CHAR, NULL, 20, 0, NULL, collation_id);
  numeric_10_2 = tp_domain_resolve (DB_TYPE_NUMERIC, NULL, 10, 2, NULL, 0);
  numeric_15_4 = tp_domain_resolve (DB_TYPE_NUMERIC, NULL, 15, 4, NULL, 0);

  db_make_int (&source, 42);
  require_write_ref_same_hash (tp_domain_resolve_default (DB_TYPE_INTEGER),
			       tp_domain_resolve_default (DB_TYPE_INTEGER), &source);
  pr_clear_value (&source);

  db_make_bigint (&source, (DB_BIGINT) 1234567890123LL);
  require_write_ref_same_hash (tp_domain_resolve_default (DB_TYPE_BIGINT),
			       tp_domain_resolve_default (DB_TYPE_BIGINT), &source);
  pr_clear_value (&source);

  db_make_date (&source, 12, 25, 2024);
  require_write_ref_same_hash (tp_domain_resolve_default (DB_TYPE_DATE),
			       tp_domain_resolve_default (DB_TYPE_DATE), &source);
  pr_clear_value (&source);

  db_make_string (&source, "abc");
  require_write_ref_same_hash (char10, char10, &source);
  require_write_ref_same_hash (char10, char20, &source);
  pr_clear_value (&source);

  db_make_string (&source, "123.45");
  require_write_ref_same_hash (numeric_10_2, numeric_15_4, &source);
  pr_clear_value (&source);
}

TEST_CASE ("composite parent WRITE and child REF produce the same collision hash", "[writeset]")
{
  ws_history_guard history;
  OID parent_class = oid_of (1, 201, 1);
  log_tdes *parent = make_tdes (1);
  log_tdes *child = make_tdes (2);
  TP_DOMAIN *first_domain;
  TP_DOMAIN *second_domain;
  TP_DOMAIN *child_first_domain;
  TP_DOMAIN *child_second_domain;
  TP_DOMAIN *parent_domain;
  TP_DOMAIN *child_domain;
  DB_VALUE parent_elements[2];
  DB_VALUE child_elements[2];
  DB_MIDXKEY parent_midxkey;
  DB_MIDXKEY child_midxkey;
  DB_VALUE parent_key;
  DB_VALUE child_key;
  LOG_LSA parent_commit = lsa_of (10, 0);
  LOG_LSA child_commit = lsa_of (20, 0);

  first_domain = tp_domain_copy (tp_domain_resolve_default (DB_TYPE_INTEGER), false);
  second_domain = tp_domain_copy (tp_domain_resolve_default (DB_TYPE_BIGINT), false);
  REQUIRE (first_domain != NULL);
  REQUIRE (second_domain != NULL);
  first_domain->next = second_domain;
  parent_domain = tp_domain_construct (DB_TYPE_MIDXKEY, NULL, 2, 0, first_domain);
  REQUIRE (parent_domain != NULL);
  parent_domain = tp_domain_cache (parent_domain);
  REQUIRE (parent_domain != NULL);

  child_first_domain = tp_domain_copy (tp_domain_resolve_default (DB_TYPE_SHORT), false);
  child_second_domain = tp_domain_copy (tp_domain_resolve_default (DB_TYPE_INTEGER), false);
  REQUIRE (child_first_domain != NULL);
  REQUIRE (child_second_domain != NULL);
  child_first_domain->next = child_second_domain;
  child_domain = tp_domain_construct (DB_TYPE_MIDXKEY, NULL, 2, 0, child_first_domain);
  REQUIRE (child_domain != NULL);
  child_domain = tp_domain_cache (child_domain);
  REQUIRE (child_domain != NULL);

  db_make_int (&parent_elements[0], 7);
  db_make_bigint (&parent_elements[1], 11);
  db_make_short (&child_elements[0], 7);
  db_make_int (&child_elements[1], 11);
  db_make_null (&parent_key);
  db_make_null (&child_key);

  parent_midxkey.buf = NULL;
  parent_midxkey.domain = parent_domain;
  parent_midxkey.ncolumns = 0;
  parent_midxkey.size = 0;
  parent_midxkey.min_max_val.position = -1;
  db_make_midxkey (&parent_key, &parent_midxkey);
  parent_key.need_clear = true;
  REQUIRE (pr_midxkey_add_elements (&parent_key, parent_elements, 2, parent_domain->setdomain) == NO_ERROR);

  child_midxkey.buf = NULL;
  child_midxkey.domain = child_domain;
  child_midxkey.ncolumns = 0;
  child_midxkey.size = 0;
  child_midxkey.min_max_val.position = -1;
  db_make_midxkey (&child_key, &child_midxkey);
  child_key.need_clear = true;
  REQUIRE (pr_midxkey_add_elements (&child_key, child_elements, 2, child_domain->setdomain) == NO_ERROR);

  REQUIRE (wset_add_write_key (parent, &parent_class, &WS_DEFAULT_VFID, &parent_key) == NO_ERROR);
  REQUIRE (wset_add_ref_key (child, &parent_class, &WS_DEFAULT_VFID, &child_key, parent_domain) == NO_ERROR);
  REQUIRE (parent->wset_hashes.size () == 1);
  REQUIRE (child->wset_hashes.size () == 1);
  REQUIRE (parent->wset_hashes[0].hash == child->wset_hashes[0].hash);

  /* The equal hashes must also drive both dependency directions through the global history. */
  wset_publish_commit_to_history (parent, &parent_commit);
  LOG_LSA child_dependency = probe (child);
  REQUIRE (LSA_EQ (&child_dependency, &parent_commit));
  REQUIRE (!child->wset_dependency_is_ref);

  wset_publish_commit_to_history (child, &child_commit);
  log_tdes *later_parent = make_tdes (3);
  REQUIRE (wset_add_write_key (later_parent, &parent_class, &WS_DEFAULT_VFID, &parent_key) == NO_ERROR);
  LOG_LSA later_parent_dependency = probe (later_parent);
  REQUIRE (LSA_EQ (&later_parent_dependency, &child_commit));
  REQUIRE (later_parent->wset_dependency_is_ref);

  pr_clear_value (&parent_key);
  pr_clear_value (&child_key);
  delete parent;
  delete child;
  delete later_parent;
}

TEST_CASE ("independent transaction gets a NULL dependency", "[writeset]")
{
  ws_history_guard history;
  OID cls = oid_of (1, 100, 1);
  log_tdes *t1 = make_tdes (1);

  add_write_int (t1, &cls, 1);

  LOG_LSA dep = probe (t1);
  REQUIRE (LSA_ISNULL (&dep));

  delete t1;
}

TEST_CASE ("same key waits behind its previous writer", "[writeset]")
{
  ws_history_guard history;
  OID cls = oid_of (1, 100, 1);
  LOG_LSA l1 = lsa_of (10, 0);

  log_tdes *t1 = make_tdes (1);
  add_write_int (t1, &cls, 1);
  wset_publish_commit_to_history (t1, &l1);

  log_tdes *t2 = make_tdes (2);
  add_write_int (t2, &cls, 1);

  LOG_LSA dep = probe (t2);
  REQUIRE (LSA_EQ (&dep, &l1));
  REQUIRE (!t2->wset_dependency_is_ref);	/* write-origin: exact completion of l1 is sufficient */

  delete t1;
  delete t2;
}

TEST_CASE ("a different key is not serialized behind an unrelated commit", "[writeset]")
{
  ws_history_guard history;
  OID cls = oid_of (1, 100, 1);
  LOG_LSA l1 = lsa_of (10, 0);

  log_tdes *t1 = make_tdes (1);
  add_write_int (t1, &cls, 1);
  wset_publish_commit_to_history (t1, &l1);

  log_tdes *t2 = make_tdes (2);
  add_write_int (t2, &cls, 2);

  LOG_LSA dep = probe (t2);
  REQUIRE (LSA_ISNULL (&dep));

  delete t1;
  delete t2;
}

TEST_CASE ("dependency is the newest commit among all touched keys", "[writeset]")
{
  ws_history_guard history;
  OID cls = oid_of (1, 100, 1);
  LOG_LSA l1 = lsa_of (10, 0);
  LOG_LSA l2 = lsa_of (20, 0);

  log_tdes *t1 = make_tdes (1);
  add_write_int (t1, &cls, 1);
  wset_publish_commit_to_history (t1, &l1);

  log_tdes *t2 = make_tdes (2);
  add_write_int (t2, &cls, 2);
  wset_publish_commit_to_history (t2, &l2);

  log_tdes *t3 = make_tdes (3);
  add_write_int (t3, &cls, 1);
  add_write_int (t3, &cls, 2);

  LOG_LSA dep = probe (t3);
  REQUIRE (LSA_EQ (&dep, &l2));

  delete t1;
  delete t2;
  delete t3;
}

TEST_CASE ("foreign-key reference waits behind the parent's writer", "[writeset]")
{
  ws_history_guard history;
  OID parent_cls = oid_of (1, 200, 1);
  LOG_LSA l1 = lsa_of (10, 0);

  /* parent INSERT publishes its primary key */
  log_tdes *parent = make_tdes (1);
  add_write_int (parent, &parent_cls, 7);
  wset_publish_commit_to_history (parent, &l1);

  /* child references the same value through its foreign key: the REF hash must
   * collide with the parent's WRITE hash (byte-identical packing invariant) */
  log_tdes *child = make_tdes (2);
  add_ref_int (child, &parent_cls, 7);

  LOG_LSA dep = probe (child);
  REQUIRE (LSA_EQ (&dep, &l1));

  delete parent;
  delete child;
}

TEST_CASE ("references stamp the read slot: siblings stay parallel, a later parent write waits", "[writeset]")
{
  ws_history_guard history;
  OID parent_cls = oid_of (1, 200, 1);
  LOG_LSA l1 = lsa_of (10, 0);
  LOG_LSA l2 = lsa_of (20, 0);

  log_tdes *parent = make_tdes (1);
  add_write_int (parent, &parent_cls, 7);
  wset_publish_commit_to_history (parent, &l1);

  /* first child references the parent and commits: its own dependency is the
   * parent's writer (write slot), and it stamps the read slot at flush */
  log_tdes *child1 = make_tdes (2);
  add_ref_int (child1, &parent_cls, 7);
  LOG_LSA dep1 = probe (child1);
  REQUIRE (LSA_EQ (&dep1, &l1));
  REQUIRE (!child1->wset_dependency_is_ref);
  wset_publish_commit_to_history (child1, &l2);

  /* a sibling referencing the same parent still depends on the parent only:
   * references consult the write slot, never the read slot, so child1's stamp
   * does not chain the siblings */
  log_tdes *child2 = make_tdes (3);
  add_ref_int (child2, &parent_cls, 7);
  LOG_LSA dep2 = probe (child2);
  REQUIRE (LSA_EQ (&dep2, &l1));
  REQUIRE (!child2->wset_dependency_is_ref);

  /* a later write to the parent row now sees the newest referencer (l2), not
   * just the previous writer (l1): the reverse-order blind spot of doc 5 is
   * closed. The label carries the read-origin flag so the slave gate waits for
   * the gap-free frontier instead of that one transaction's completion. */
  log_tdes *parent_delete = make_tdes (4);
  add_write_int (parent_delete, &parent_cls, 7);
  LOG_LSA dep3 = probe (parent_delete);
  REQUIRE (LSA_EQ (&dep3, &l2));
  REQUIRE (parent_delete->wset_dependency_is_ref);

  delete parent;
  delete child1;
  delete child2;
  delete parent_delete;
}

TEST_CASE ("read slot on a never-written key: parallel siblings, monotonic stamp, flagged writer", "[writeset]")
{
  ws_history_guard history;
  OID parent_cls = oid_of (1, 200, 1);
  LOG_LSA l5 = lsa_of (50, 0);
  LOG_LSA l3 = lsa_of (30, 0);

  /* the referenced key has no write history (e.g. the parent row predates the
   * current history window): the reference creates the entry with only the
   * read slot filled */
  log_tdes *child1 = make_tdes (1);
  add_ref_int (child1, &parent_cls, 9);
  LOG_LSA dep1 = probe (child1);
  REQUIRE (LSA_ISNULL (&dep1));
  wset_publish_commit_to_history (child1, &l5);

  /* a sibling still sees an empty write slot: independent */
  log_tdes *child2 = make_tdes (2);
  add_ref_int (child2, &parent_cls, 9);
  LOG_LSA dep2 = probe (child2);
  REQUIRE (LSA_ISNULL (&dep2));

  /* an out-of-order (older) reference flush must not move the read slot backwards */
  log_tdes *child3 = make_tdes (3);
  add_ref_int (child3, &parent_cls, 9);
  wset_publish_commit_to_history (child3, &l3);

  /* the parent's writer waits for the newest referencer (l5, not l3) with the
   * read-origin flag set */
  log_tdes *parent_delete = make_tdes (4);
  add_write_int (parent_delete, &parent_cls, 9);
  LOG_LSA dep3 = probe (parent_delete);
  REQUIRE (LSA_EQ (&dep3, &l5));
  REQUIRE (parent_delete->wset_dependency_is_ref);

  delete child1;
  delete child2;
  delete child3;
  delete parent_delete;
}

TEST_CASE ("write slot keeps the newest commit when publishes arrive out of order", "[writeset]")
{
  ws_history_guard history;
  OID cls = oid_of (1, 100, 1);
  LOG_LSA newer_lsa = lsa_of (50, 0);
  LOG_LSA older_lsa = lsa_of (30, 0);

  log_tdes *newer = make_tdes (1);
  add_write_int (newer, &cls, 7);
  wset_publish_commit_to_history (newer, &newer_lsa);

  log_tdes *delayed_older = make_tdes (2);
  add_write_int (delayed_older, &cls, 7);
  wset_publish_commit_to_history (delayed_older, &older_lsa);

  log_tdes *successor = make_tdes (3);
  add_write_int (successor, &cls, 7);
  LOG_LSA dependency = probe (successor);
  REQUIRE (LSA_EQ (&dependency, &newer_lsa));

  delete newer;
  delete delayed_older;
  delete successor;
}

TEST_CASE ("overflow demotes to commit order and raises the history floor", "[writeset]")
{
  ws_history_guard history;
  OID cls = oid_of (1, 100, 1);
  LOG_LSA l1 = lsa_of (10, 0);
  LOG_LSA l3 = lsa_of (30, 0);

  log_tdes *t1 = make_tdes (1);
  add_write_int (t1, &cls, 1);
  wset_publish_commit_to_history (t1, &l1);

  /* an overflowed transaction waits for everything before it (= prev commit) */
  log_tdes *ovf = make_tdes (2);
  ovf->wset_overflow = true;
  LOG_LSA dep = probe (ovf);
  REQUIRE (LSA_EQ (&dep, &l1));
  REQUIRE (!ovf->wset_dependency_is_ref);

  /* and its flush clears the history and raises the floor, so everything
   * after waits for the overflow commit */
  wset_publish_commit_to_history (ovf, &l3);

  log_tdes *t3 = make_tdes (3);
  add_write_int (t3, &cls, 99);
  LOG_LSA dep3 = probe (t3);
  REQUIRE (LSA_EQ (&dep3, &l3));

  delete t1;
  delete ovf;
  delete t3;
}

TEST_CASE ("delayed overflow clear preserves a newer published commit as the history floor", "[writeset]")
{
  ws_history_guard history;
  OID cls = oid_of (1, 100, 1);
  LOG_LSA older_lsa = lsa_of (100, 0);
  LOG_LSA newer_lsa = lsa_of (110, 0);

  log_tdes *newer = make_tdes (2);
  add_write_int (newer, &cls, 7);
  wset_publish_commit_to_history (newer, &newer_lsa);

  log_tdes *delayed_overflow = make_tdes (1);
  delayed_overflow->wset_overflow = true;
  wset_publish_commit_to_history (delayed_overflow, &older_lsa);

  REQUIRE (LSA_EQ (&wset_History.history_start, &newer_lsa));
  REQUIRE (wset_History.map.size () == 0);

  log_tdes *successor = make_tdes (3);
  add_write_int (successor, &cls, 7);
  LOG_LSA dependency = probe (successor);
  REQUIRE (LSA_EQ (&dependency, &newer_lsa));

  delete newer;
  delete delayed_overflow;
  delete successor;
}

TEST_CASE ("probe keeps the sequence lock until the history snapshot is acquired", "[writeset]")
{
  ws_history_guard history;
  OID cls = oid_of (1, 100, 1);
  log_tdes *tx = make_tdes (1);
  LOG_LSA dependency;
  std::atomic<bool> probe_entered {false};

  add_write_int (tx, &cls, 7);

  /* Hold the history lock so the probe has to stop between acquiring seq_lock and taking its
   * history snapshot. The fixed protocol retains seq_lock at that point; the former mixed-snapshot
   * protocol released it before waiting for history_lock. */
  pthread_rwlock_wrlock (&wset_History.history_lock);
  std::thread worker ([&] ()
  {
    probe_entered.store (true, std::memory_order_release);
    wset_find_dependency_from_history (tx, &dependency);
  });

  while (!probe_entered.load (std::memory_order_acquire))
    {
      std::this_thread::yield ();
    }

  bool observed_seq_lock = false;
  bool retained_seq_lock = false;
  int unexpected_lock_result = 0;
  auto observe_deadline = std::chrono::steady_clock::now () + std::chrono::seconds (1);
  while (std::chrono::steady_clock::now () < observe_deadline)
    {
      int lock_result = pthread_mutex_trylock (&wset_History.seq_lock);
      if (lock_result == EBUSY)
	{
	  observed_seq_lock = true;
	  break;
	}
      if (lock_result != 0)
	{
	  unexpected_lock_result = lock_result;
	  break;
	}
      pthread_mutex_unlock (&wset_History.seq_lock);
      std::this_thread::yield ();
    }

  if (observed_seq_lock)
    {
      retained_seq_lock = true;
      auto retain_deadline = std::chrono::steady_clock::now () + std::chrono::milliseconds (10);
      while (std::chrono::steady_clock::now () < retain_deadline)
	{
	  int lock_result = pthread_mutex_trylock (&wset_History.seq_lock);
	  if (lock_result == 0)
	    {
	      pthread_mutex_unlock (&wset_History.seq_lock);
	      retained_seq_lock = false;
	      break;
	    }
	  if (lock_result != EBUSY)
	    {
	      unexpected_lock_result = lock_result;
	      retained_seq_lock = false;
	      break;
	    }
	  std::this_thread::yield ();
	}
    }

  pthread_rwlock_unlock (&wset_History.history_lock);
  worker.join ();

  REQUIRE (unexpected_lock_result == 0);
  REQUIRE (observed_seq_lock);
  REQUIRE (retained_seq_lock);
  REQUIRE (LSA_ISNULL (&dependency));

  delete tx;
}

TEST_CASE ("probe never lowers a dependency below the observed history floor", "[writeset]")
{
  ws_history_guard history;
  OID cls = oid_of (1, 100, 1);
  LOG_LSA old_baseline = lsa_of (100, 0);
  LOG_LSA new_floor = lsa_of (200, 0);
  log_tdes *tx = make_tdes (1);

  /* Inject the mixed values that exposed R02 to pin the final defensive floor independently of the
   * locking protocol. A normal execution cannot create this pair once snapshot locking is correct. */
  pthread_mutex_lock (&wset_History.seq_lock);
  pthread_rwlock_wrlock (&wset_History.history_lock);
  LSA_COPY (&wset_History.prev_commit_lsa, &old_baseline);
  LSA_COPY (&wset_History.history_start, &new_floor);
  pthread_rwlock_unlock (&wset_History.history_lock);
  pthread_mutex_unlock (&wset_History.seq_lock);

  add_write_int (tx, &cls, 7);
  LOG_LSA dependency = probe (tx);

  REQUIRE (LSA_EQ (&dependency, &new_floor));

  delete tx;
}

TEST_CASE ("per-transaction key limit flips the transaction to overflow", "[writeset]")
{
  /* The per-transaction key limit is the runtime ha_writeset_history_size (its
   * production default is ten million). Inject a small cap so the overflow path
   * is reached deterministically after a handful of keys instead of collecting
   * millions. Adding keys until the flip lands on the key whose arrival makes
   * the collected count reach the cap. */
  const int cap = 8;
  ws_history_guard history (cap);
  OID cls = oid_of (1, 100, 1);
  log_tdes *t1 = make_tdes (1);

  int added = 0;
  while (!t1->wset_overflow)
    {
      add_write_int (t1, &cls, added++);
    }
  REQUIRE (added == cap + 1);	/* the (cap+1)-th key is the one rejected into overflow */
  REQUIRE (t1->wset_overflow);
  REQUIRE (t1->wset_hashes.empty ());	/* the collected writeset is dropped */

  delete t1;
}

TEST_CASE ("the same key under two different index vfids stays independent (PK vs UNIQUE of one table)",
	   "[writeset]")
{
  /* A primary key and a unique key on the same table can hold the same value and pack to the same
   * bytes; the index VFID folded into the hash keeps them in separate history slots. This pins that
   * a write under one index does not create a dependency for a write of the same value under the
   * other index, and that each index keeps its own writer. */
  ws_history_guard history;
  OID cls = oid_of (1, 100, 1);
  VFID pk_vfid = vfid_of (0, 1000);	/* the table's primary-key index */
  VFID uk_vfid = vfid_of (0, 1001);	/* a unique index on the same table */
  LOG_LSA lpk = lsa_of (10, 0);
  LOG_LSA luk = lsa_of (20, 0);

  /* publish a WRITE of value 5 under the primary-key index only */
  log_tdes *t1 = make_tdes (1);
  add_write_int (t1, &cls, 5, &pk_vfid);
  wset_publish_commit_to_history (t1, &lpk);

  /* a WRITE of the same value 5 under the unique index must not inherit the primary-key write's
   * dependency: a different index vfid means a different history slot */
  log_tdes *t2 = make_tdes (2);
  add_write_int (t2, &cls, 5, &uk_vfid);
  LOG_LSA dep2 = probe (t2);
  REQUIRE (LSA_ISNULL (&dep2));
  wset_publish_commit_to_history (t2, &luk);

  /* and the reverse: a later write of value 5 under the primary-key index waits behind lpk (the
   * primary-key slot), not luk (the unique slot) - the two indexes are tracked separately */
  log_tdes *t3 = make_tdes (3);
  add_write_int (t3, &cls, 5, &pk_vfid);
  LOG_LSA dep3 = probe (t3);
  REQUIRE (LSA_EQ (&dep3, &lpk));

  delete t1;
  delete t2;
  delete t3;
}

/* ------------------------------------------------------------------------- *
 * Concurrency correctness: concurrent commits must produce the same slots as
 * if they ran one at a time. The global history map is a concurrent hash map
 * with a shared/exclusive history lock, so these cases confirm that per-key
 * publishing and probing race safely (no lost update on a disjoint key, and the
 * read slot keeps the newest referencer under concurrent stamps). Workers call
 * only probe/flush; the writeset is collected up front on the main thread, so
 * no worker touches the cast path (db_private_alloc / TLS THREAD_ENTRY). See
 * test_wset_concurrency.cpp for the throughput/latency comparison.
 * ------------------------------------------------------------------------- */

TEST_CASE ("concurrent flushes on disjoint keys all publish (no lost update)", "[writeset]")
{
  ws_history_guard history;
  OID cls = oid_of (1, 100, 1);
  const int nthreads = 8;
  const int per_thread = 500;

  /* each thread flushes its own disjoint key range with a distinct commit LSA */
  std::vector<log_tdes *> tx (nthreads);
  for (int t = 0; t < nthreads; t++)
    {
      tx[t] = make_tdes (t + 1);
      for (int i = 0; i < per_thread; i++)
	{
	  add_write_int (tx[t], &cls, t * per_thread + i);
	}
    }

  std::vector<std::thread> workers;
  for (int t = 0; t < nthreads; t++)
    {
      LOG_LSA commit = lsa_of (10 + t, 0);
      workers.emplace_back ([tx, t, commit] ()
      {
	wset_publish_commit_to_history (tx[t], &commit);
      });
    }
  for (auto &w : workers)
    {
      w.join ();
    }

  /* every disjoint key from every thread must be present: nthreads * per_thread
   * entries, none lost to a race on the map */
  REQUIRE (wset_History.map.size () == (size_t) (nthreads * per_thread));

  for (int t = 0; t < nthreads; t++)
    {
      delete tx[t];
    }
}

TEST_CASE ("concurrent siblings referencing one parent stay parallel", "[writeset]")
{
  ws_history_guard history;
  OID parent_cls = oid_of (1, 200, 1);
  LOG_LSA lp = lsa_of (10, 0);
  const int nsiblings = 16;

  /* parent published, then many children reference the same parent key */
  log_tdes *parent = make_tdes (1);
  add_write_int (parent, &parent_cls, 7);
  wset_publish_commit_to_history (parent, &lp);

  std::vector<log_tdes *> child (nsiblings);
  for (int i = 0; i < nsiblings; i++)
    {
      child[i] = make_tdes (100 + i);
      add_ref_int (child[i], &parent_cls, 7);
    }

  /* all siblings probe concurrently; each must depend only on the parent's
   * writer (references consult the write slot only), never on one another */
  std::vector<LOG_LSA> dep (nsiblings);
  std::vector<char> is_ref (nsiblings);
  std::vector<std::thread> workers;
  for (int i = 0; i < nsiblings; i++)
    {
      workers.emplace_back ([&child, &dep, &is_ref, i] ()
      {
	LOG_LSA d;
	wset_find_dependency_from_history (child[i], &d);
	dep[i] = d;
	is_ref[i] = child[i]->wset_dependency_is_ref;
      });
    }
  for (auto &w : workers)
    {
      w.join ();
    }

  for (int i = 0; i < nsiblings; i++)
    {
      REQUIRE (LSA_EQ (&dep[i], &lp));
      REQUIRE (!is_ref[i]);
      delete child[i];
    }
  delete parent;
}

TEST_CASE ("concurrent references then a parent write waits behind the newest referencer", "[writeset]")
{
  ws_history_guard history;
  OID parent_cls = oid_of (1, 200, 1);
  const int nsiblings = 16;
  LOG_LSA lp = lsa_of (10, 0);

  log_tdes *parent = make_tdes (1);
  add_write_int (parent, &parent_cls, 7);
  wset_publish_commit_to_history (parent, &lp);

  /* children flush references to the same parent concurrently, each with a
   * distinct commit LSA; the newest among them is lseq(nsiblings) */
  std::vector<log_tdes *> child (nsiblings);
  LOG_LSA newest = lsa_of (0, 0);
  for (int i = 0; i < nsiblings; i++)
    {
      child[i] = make_tdes (100 + i);
      add_ref_int (child[i], &parent_cls, 7);
      LOG_LSA c = lsa_of (20 + i, 0);
      if (LSA_GT (&c, &newest))
	{
	  newest = c;
	}
    }

  std::vector<std::thread> workers;
  for (int i = 0; i < nsiblings; i++)
    {
      LOG_LSA commit = lsa_of (20 + i, 0);
      workers.emplace_back ([&child, i, commit] ()
      {
	wset_publish_commit_to_history (child[i], &commit);
      });
    }
  for (auto &w : workers)
    {
      w.join ();
    }

  /* a later write to the parent row must wait for the newest referencer (read
   * slot holds the max under concurrent stamps: no lost update, monotonic) */
  log_tdes *parent_delete = make_tdes (900);
  add_write_int (parent_delete, &parent_cls, 7);
  LOG_LSA dep = probe (parent_delete);
  REQUIRE (LSA_EQ (&dep, &newest));
  REQUIRE (parent_delete->wset_dependency_is_ref);

  for (int i = 0; i < nsiblings; i++)
    {
      delete child[i];
    }
  delete parent;
  delete parent_delete;
}
