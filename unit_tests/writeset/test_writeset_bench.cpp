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
 * test_wset_bench.cpp - per-key unit-cost benchmarks for the writeset
 *                               collection and global commit history.
 *
 * These measure the pure CPU cost of a single key's pack+hash (collect), map
 * lookup (probe) and map publish (flush), with no server and no contention.
 * They are the lower bound the integration bench (doc 6) can only observe mixed
 * with cache misses and latch waits. Tag [benchmark]; run on a release build.
 */

#include <string>

#include "test_writeset_common.hpp"

using namespace wstest;

TEST_CASE ("hash unit cost benchmarks", "[benchmark]")
{
  ws_history_guard history;
  OID cls = oid_of (1, 100, 1);
  OID parent_cls = oid_of (1, 200, 1);
  TP_DOMAIN *int_domain = tp_domain_resolve_default (DB_TYPE_INTEGER);
  DB_VALUE v;
  int seq = 0;

  REQUIRE (int_domain != NULL);

  log_tdes *scratch = make_tdes (900);

  BENCHMARK ("WRITE add x1000 (pack+hash; read mean/1000 for per-key ns)")
  {
    for (int i = 0; i < 1000; i++)
      {
	db_make_int (&v, seq++);
	wset_add_write_key (scratch, &cls, &WS_DEFAULT_VFID, &v);
      }
    scratch->wset_hashes.clear ();
    return seq;
  };

  BENCHMARK ("REF add x1000 (cast+pack+hash; read mean/1000 for per-key ns)")
  {
    for (int i = 0; i < 1000; i++)
      {
	db_make_int (&v, seq++);
	wset_add_ref_key (scratch, &parent_cls, &WS_DEFAULT_VFID, &v, int_domain);
      }
    scratch->wset_hashes.clear ();
    return seq;
  };

  /* fill the history with 100k distinct keys so probe/flush run against a
   * realistically loaded map */
  {
    log_tdes *filler = make_tdes (901);
    LOG_LSA fill_lsa = lsa_of (100, 0);

    for (int i = 0; i < 100000; i++)
      {
	db_make_int (&v, 1000000 + i);
	wset_add_write_key (filler, &cls, &WS_DEFAULT_VFID, &v);
      }
    wset_publish_commit_to_history (filler, &fill_lsa);
    delete filler;
  }

  log_tdes *worker = make_tdes (902);
  for (int i = 0; i < 1000; i++)
    {
      db_make_int (&v, 1000000 + i);	/* all 1000 keys hit the map */
      wset_add_write_key (worker, &cls, &WS_DEFAULT_VFID, &v);
    }

  LOG_LSA dep;
  BENCHMARK ("probe x1000 keys vs 100k map (read mean/1000 for per-key ns)")
  {
    wset_find_dependency_from_history (worker, &dep);
    return dep.pageid;
  };

  LOG_LSA commit_lsa = lsa_of (200, 0);
  BENCHMARK ("flush x1000 keys vs 100k map (update; read mean/1000 for per-key ns)")
  {
    wset_publish_commit_to_history (worker, &commit_lsa);
    return 0;
  };

  delete worker;
  delete scratch;
}

/* A1: per-key probe cost as the map grows. A hash map keeps lookup O(1) on
 * average, so per-key probe time should stay flat while the map spans a wide
 * range of sizes. 1000 probing keys always hit, measured against maps of
 * increasing size. This case injects the parameter's upper limit (ten million)
 * as its own history capacity, so the sweep stays well under the cap and never
 * triggers a capacity-clear regardless of the process default. The
 * capacity-clear behaviour is exercised deterministically in
 * test_wset_config.cpp. */
TEST_CASE ("A1: probe per-key cost is flat across map size", "[benchmark]")
{
  const int map_sizes[] = { 10000, 100000, 500000, 1000000 };

  for (int msize : map_sizes)
    {
      ws_history_guard history (10000000);
      OID cls = oid_of (1, 100, 1);
      DB_VALUE v;

      /* load the map with msize distinct keys */
      log_tdes *filler = make_tdes (901);
      LOG_LSA fill_lsa = lsa_of (100, 0);
      for (int i = 0; i < msize; i++)
	{
	  db_make_int (&v, i);
	  wset_add_write_key (filler, &cls, &WS_DEFAULT_VFID, &v);
	}
      wset_publish_commit_to_history (filler, &fill_lsa);
      delete filler;

      /* 1000 keys that all hit the loaded map */
      log_tdes *worker = make_tdes (902);
      for (int i = 0; i < 1000; i++)
	{
	  db_make_int (&v, i);
	  wset_add_write_key (worker, &cls, &WS_DEFAULT_VFID, &v);
	}

      LOG_LSA dep;
      BENCHMARK ("probe x1000 vs map=" + std::to_string (msize) + " (read mean/1000 for per-key ns)")
      {
	wset_find_dependency_from_history (worker, &dep);
	return dep.pageid;
      };

      delete worker;
    }
}

/* F0: per-key unit cost of the FK-side branches. The case above measures
 * probe/flush over WRITE keys; a foreign key adds REF keys, and those take
 * different branches: at probe a REF key reads only the write slot (a WRITE key
 * also compares the read slot), and at flush a REF key updates the read slot
 * only when the commit LSA is newer (a WRITE key overwrites unconditionally).
 * Also measures the FK row shape (one WRITE + one REF per row) so the per-row
 * cost of an FK insert can be read directly. */
TEST_CASE ("F0: FK per-key unit cost (REF probe/flush, FK row)", "[benchmark]")
{
  ws_history_guard history;
  OID child_cls = oid_of (1, 100, 1);
  OID parent_cls = oid_of (1, 200, 1);
  TP_DOMAIN *int_domain = tp_domain_resolve_default (DB_TYPE_INTEGER);
  DB_VALUE v;

  REQUIRE (int_domain != NULL);

  /* load the map exactly as the WRITE-path case does (100k distinct keys), so
   * REF and WRITE unit costs are read against the same map; parent keys are the
   * rows the REF keys point at, child keys make the mixed probes hit too */
  {
    log_tdes *filler = make_tdes (901);
    LOG_LSA fill_lsa = lsa_of (100, 0);

    for (int i = 0; i < 100000; i++)
      {
	db_make_int (&v, i);
	wset_add_write_key (filler, &parent_cls, &WS_DEFAULT_VFID, &v);
      }
    wset_publish_commit_to_history (filler, &fill_lsa);
    delete filler;
  }
  {
    log_tdes *filler = make_tdes (902);
    LOG_LSA fill_lsa = lsa_of (101, 0);

    for (int i = 0; i < 1000; i++)
      {
	db_make_int (&v, i);
	wset_add_write_key (filler, &child_cls, &WS_DEFAULT_VFID, &v);
      }
    wset_publish_commit_to_history (filler, &fill_lsa);
    delete filler;
  }

  /* REF-only transaction: 1,000 references to loaded parent keys */
  log_tdes *ref_tx = make_tdes (903);
  for (int i = 0; i < 1000; i++)
    {
      db_make_int (&v, i);
      wset_add_ref_key (ref_tx, &parent_cls, &WS_DEFAULT_VFID, &v, int_domain);
    }

  LOG_LSA dep;
  BENCHMARK ("REF probe x1000 keys vs 100k map (read mean/1000 for per-key ns)")
  {
    wset_find_dependency_from_history (ref_tx, &dep);
    return dep.pageid;
  };

  /* the read slot only moves forward, so give every flush a newer commit LSA;
   * a repeated LSA would take the skip branch and undercount the update cost */
  INT64 ref_page = 200;
  BENCHMARK ("REF flush x1000 keys vs 100k map (read mean/1000 for per-key ns)")
  {
    LOG_LSA commit_lsa = lsa_of (ref_page++, 0);
    wset_publish_commit_to_history (ref_tx, &commit_lsa);
    return 0;
  };

  /* FK row transaction: 1,000 rows, each row = child WRITE + parent REF
   * (2,000 keys total; the mean/1000 readout is the per-ROW cost) */
  log_tdes *fk_tx = make_tdes (904);
  for (int i = 0; i < 1000; i++)
    {
      db_make_int (&v, i);
      wset_add_write_key (fk_tx, &child_cls, &WS_DEFAULT_VFID, &v);
      wset_add_ref_key (fk_tx, &parent_cls, &WS_DEFAULT_VFID, &v, int_domain);
    }

  BENCHMARK ("FK-row probe x1000 rows (2000 keys; read mean/1000 for per-row ns)")
  {
    wset_find_dependency_from_history (fk_tx, &dep);
    return dep.pageid;
  };

  INT64 fk_page = 100000;
  BENCHMARK ("FK-row flush x1000 rows (2000 keys; read mean/1000 for per-row ns)")
  {
    LOG_LSA commit_lsa = lsa_of (fk_page++, 0);
    wset_publish_commit_to_history (fk_tx, &commit_lsa);
    return 0;
  };

  delete ref_tx;
  delete fk_tx;
}
