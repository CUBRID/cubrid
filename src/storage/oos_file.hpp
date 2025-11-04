#pragma once

#include "dbtype_def.h"
#include "storage_common.h"
#include "thread_compat.hpp"

int oos_create (THREAD_ENTRY *thread_p, VFID &oos_vfid);

int oos_destroy (THREAD_ENTRY *thread_p, const VFID &oos_vfid);

int
oos_insert (THREAD_ENTRY *thread_p, const VFID &oos_vfid, const RECDES &recdes, OID &oid);

int
oos_read (THREAD_ENTRY *thread_p, const VFID &oos_vfid, const OID &oid, RECDES &recdes);

int
oos_find_best_page (THREAD_ENTRY *thread_p, const VFID &oos_vfid, int rec_length, VPID &vpid);
