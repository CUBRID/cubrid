#pragma once

#include "dbtype_def.h"
#include "storage_common.h"
#include "thread_compat.hpp"

struct oos_record_header
{
  int total_size;
  int chunk_index;
  OID next_chunk_oid;
};
using OOS_RECORD_HEADER = struct oos_record_header;

int oos_create (THREAD_ENTRY *thread_p, VFID &oos_vfid);

int oos_destroy (THREAD_ENTRY *thread_p, const VFID &oos_vfid);

int
oos_insert (THREAD_ENTRY *thread_p, const VFID &oos_vfid, RECDES &recdes, OID &oid);

int
oos_read (THREAD_ENTRY *thread_p, const VFID &oos_vfid, const OID &oid, RECDES &recdes);

int
oos_find_best_page (THREAD_ENTRY *thread_p, const VFID &oos_vfid, const int rec_length, VPID &vpid);

static int
oos_vpid_init_new (THREAD_ENTRY *thread_p, PAGE_PTR page, void *args);
