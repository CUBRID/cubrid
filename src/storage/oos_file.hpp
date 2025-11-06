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

#pragma once

#include "dbtype_def.h"
#include "storage_common.h"
#include "thread_compat.hpp"
#include "error_manager.h"

static int oos_log_enabled = 0;

inline void oos_set_log_enabled (int enabled)
{
  oos_log_enabled = enabled;
}

#define oos_log(...) \
  if (oos_log_enabled) _er_log_debug (ARG_FILE_LINE, __VA_ARGS__)

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

static int oos_insert_within_page (THREAD_ENTRY *thread_p, const VFID &oos_vfid, RECDES &recdes,
				   const OOS_RECORD_HEADER &header, OID &oid);
static int oos_insert_across_pages (THREAD_ENTRY *thread_p, const VFID &oos_vfid, RECDES &recdes,
				    OID &oid);
static int
oos_read_within_page (THREAD_ENTRY *thread_p, const VFID &oos_vfid, const OID &oid, RECDES &recdes,
		      OOS_RECORD_HEADER &out_header);
static int
oos_read_across_pages (THREAD_ENTRY *thread_p, const VFID &oos_vfid, const OID &oid, RECDES &recdes,
		       OOS_RECORD_HEADER &out_header);

