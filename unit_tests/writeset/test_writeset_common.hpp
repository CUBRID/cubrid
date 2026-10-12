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
 * test_writeset_common.hpp - shared fixtures and helpers for the writeset
 *                                unit test translation units (behavior,
 *                                benchmark, stress, concurrency, config).
 *                                Helpers are inline in the wstest namespace so
 *                                an unused one in any single translation unit
 *                                does not warn.
 */

#ifndef _TEST_LOG_WSET_COMMON_HPP_
#define _TEST_LOG_WSET_COMMON_HPP_

#include <thread>
#include <vector>

#include "catch2/catch.hpp"

#include "dbtype.h"
#include "log_impl.h"
#include "object_primitive.h"
#include "writeset.hpp"
#include "object_domain.h"
#include "oid.h"
#include "system_parameter.h"

namespace wstest
{
  /* every case runs on a freshly initialized global history; initialize resets the
   * map, history_start and the commit-order baseline, so cases stay isolated.
   *
   * The default constructor uses whatever ha_writeset_history_size the process
   * currently has (its compiled default). The size-taking constructor injects a
   * small capacity before initialize so the per-transaction and map-capacity
   * paths can be exercised deterministically with a handful of keys instead of
   * the ten-million-key production default; the previous parameter value is put
   * back at teardown so other cases are unaffected. Injection has to happen
   * before initialize because initialize caches the parameter into
   * wset_History.history_size once and never re-reads it. */
  struct ws_history_guard
  {
    ws_history_guard ()
      : m_restore_size (false), m_saved_size (0)
    {
      wset_history_initialize ();
    }

    explicit ws_history_guard (int history_size)
      : m_restore_size (true), m_saved_size (prm_get_integer_value (PRM_ID_HA_WRITESET_HISTORY_SIZE))
    {
      prm_set_integer_value (PRM_ID_HA_WRITESET_HISTORY_SIZE, history_size);
      wset_history_initialize ();
    }

    ~ws_history_guard ()
    {
      wset_history_finalize ();
      if (m_restore_size)
	{
	  prm_set_integer_value (PRM_ID_HA_WRITESET_HISTORY_SIZE, m_saved_size);
	}
    }

    bool m_restore_size;	/* the size-taking constructor injected a parameter to undo */
    int m_saved_size;		/* parameter value to restore at teardown */
  };

  inline LOG_LSA
  lsa_of (INT64 pageid, short offset)
  {
    LOG_LSA lsa;

    lsa.pageid = pageid;
    lsa.offset = offset;
    return lsa;
  }

  inline OID
  oid_of (short volid, int pageid, short slotid)
  {
    OID oid;

    oid.volid = volid;
    oid.pageid = pageid;
    oid.slotid = slotid;
    return oid;
  }

  inline VFID
  vfid_of (short volid, int fileid)
  {
    VFID vfid;

    vfid.fileid = fileid;
    vfid.volid = volid;
    return vfid;
  }

  /* One fixed index identity used by the default WRITE/REF helpers. Using the same VFID for both a
   * WRITE and a same-valued REF makes the child reference land in the parent write's history slot,
   * which is what the foreign-key cases rely on. Cases that need to distinguish two indexes of one
   * table (a primary key versus a unique key) pass their own VFID explicitly. */
  inline const VFID WS_DEFAULT_VFID = vfid_of (0, 1000);

  /* a transaction descriptor with only the writeset-related fields prepared;
   * the writeset functions touch nothing else of log_tdes */
  inline log_tdes *
  make_tdes (int trid)
  {
    log_tdes *tdes = new log_tdes ();

    tdes->trid = trid;
    tdes->wset_hashes.clear ();
    tdes->wset_overflow = false;
    tdes->wset_dependency_is_ref = false;
    LSA_SET_NULL (&tdes->wset_dependency_seq);
    return tdes;
  }

  inline void
  add_write_int (log_tdes *tdes, const OID *cls, int key, const VFID *vfid = &WS_DEFAULT_VFID)
  {
    DB_VALUE pk;

    db_make_int (&pk, key);
    REQUIRE (wset_add_write_key (tdes, cls, vfid, &pk) == NO_ERROR);
  }

  inline void
  add_ref_int (log_tdes *tdes, const OID *parent_cls, int key, const VFID *vfid = &WS_DEFAULT_VFID)
  {
    DB_VALUE fk;
    TP_DOMAIN *int_domain = tp_domain_resolve_default (DB_TYPE_INTEGER);

    REQUIRE (int_domain != NULL);
    db_make_int (&fk, key);
    REQUIRE (wset_add_ref_key (tdes, parent_cls, vfid, &fk, int_domain) == NO_ERROR);
  }

  inline LOG_LSA
  probe (log_tdes *tdes)
  {
    LOG_LSA dep;

    wset_find_dependency_from_history (tdes, &dep);
    return dep;
  }
}

#endif /* _TEST_LOG_WSET_COMMON_HPP_ */
