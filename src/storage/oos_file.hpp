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

#include "storage_common.h"
#include "thread_compat.hpp"

struct oos_record_header
{
  int total_size;
  int chunk_index;
  OID next_chunk_oid;
};
using OOS_RECORD_HEADER = struct oos_record_header;

#define OOS_RECORD_HEADER_SIZE ((int) sizeof (OOS_RECORD_HEADER))

extern int oos_file_create (THREAD_ENTRY *thread_p, VFID &oos_vfid);
extern int oos_remove_file (THREAD_ENTRY *thread_p, const VFID &oos_vfid);
extern int oos_remove_page (THREAD_ENTRY *thread_p, const VFID &oos_vfid, const VPID &vpid);
extern int oos_insert (THREAD_ENTRY *thread_p, const VFID &oos_vfid, RECDES &recdes, OID &oid);
extern int oos_read (THREAD_ENTRY *thread_p, const OID &oid, RECDES &recdes);
extern int oos_delete (THREAD_ENTRY *thread_p, const VFID &oos_vfid, const OID &oid);
extern int oos_get_length (THREAD_ENTRY *thread_p, const OID &oid);

extern int oos_rv_redo_delete (THREAD_ENTRY *thread_p, LOG_RCV *rcv);
extern int oos_rv_redo_insert (THREAD_ENTRY *thread_p, LOG_RCV *rcv);


#ifdef __cplusplus
extern "C"
{
#endif

extern void oos_push_oos_oid (THREAD_ENTRY *thread_p, const OID *oid);

#ifdef __cplusplus
}
#endif