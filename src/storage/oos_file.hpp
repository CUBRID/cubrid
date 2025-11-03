#pragma once

#include "dbtype_def.h"
#include "storage_common.h"
#include "thread_compat.hpp"
#include <vector>

struct OosCreateResult
{
  int error_code;
  VFID oos_vfid;
};

/*
 * oos_create ()
 * return OosCreateResult
 * contains error_code in case of error
 * contains output oos_vfid and oos_vpid in case of success
 */
[[nodiscard]] OosCreateResult oos_create (THREAD_ENTRY *thread_p, const HFID &hfid);

/*
 * oos_destroy ()
 * return error code: int
 */
[[nodiscard]] int oos_destroy (THREAD_ENTRY *thread_p, const HFID &hfid);

[[nodiscard]] int oos_destroy (THREAD_ENTRY *thread_p, const VFID &oos_vfid);

struct OosInsertResult
{
  int error_code;
  std::vector<OID> oids;
};

/*
 * oos_insert ()
 * return OosInsertResult
 * contains error_code in case of error
 * contains vector of OIDs of inserted objects in case of success
 */
[[nodiscard]] OosInsertResult
oos_insert (const THREAD_ENTRY *thread_p, const VFID &oos_vfid, const std::vector<RECDES> &inserted_recs);

/* oos_get
 * return error code
 */
[[nodiscard]] int
oos_get (const THREAD_ENTRY *threnad_entry, const VFID &oos_vfid, const OID &oid, RECDES &recdes);

