#pragma once

#include "dbtype_def.h"
#include "page_buffer.h"
#include "storage_common.h"
#include "thread_compat.hpp"
#include <tuple>
#include <vector>
#include <optional>

struct OosInitResult
{
  VFID oos_vfid;
  VPID oos_vpid;
  int error_code;
};

/*
 * oos_init ()
 * return OosInitResult
 * contains error_code in case of error
 * contains output oos_vfid and oos_vpid in case of success
 */
[[nodiscard]] OosInitResult
oos_init (const THREAD_ENTRY &thread_entry, const HFID &hfid);

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
oos_insert (THREAD_ENTRY &thread_entry, const std::vector<DB_VALUE *> &db_values);

/* oos_get
 * return error code
 */
[[nodiscard]] int
oos_get (const OID &oid, RECDES &recdes);


