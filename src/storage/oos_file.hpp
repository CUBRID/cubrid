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

[[nodiscard]] int oos_create (THREAD_ENTRY *thread_p, const HFID &hfid, VFID &oos_vfid);

[[nodiscard]] int oos_destroy (THREAD_ENTRY *thread_p, const VFID &oos_vfid);

[[nodiscard]] int
oos_insert (THREAD_ENTRY *thread_p, const VFID &oos_vfid, const RECDES &recdes, OID &oid);

/* oos_get
 * return error code
 */
[[nodiscard]] int
oos_read (THREAD_ENTRY *threnad_entry, const VFID &oos_vfid, const OID &oid, RECDES &recdes);

