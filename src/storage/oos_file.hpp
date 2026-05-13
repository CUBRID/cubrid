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

#ifndef _OOS_FILE_HPP_
#define _OOS_FILE_HPP_

#include "span.hpp"
#include "storage_common.h"
#include "thread_compat.hpp"

struct oos_record_header
{
  int total_data_length;	/* total length of user data across all chunks (excluding OOS headers) */
  int chunk_index;		/* 0-based index of this chunk in the chain */
  OID next_chunk_oid;		/* OID of next chunk, or NULL OID if this is the last */
};
using OOS_RECORD_HEADER = struct oos_record_header;

#define OOS_RECORD_HEADER_SIZE ((int) sizeof (OOS_RECORD_HEADER))

/* Alias for a RECDES whose first OOS_RECORD_HEADER_SIZE bytes are the OOS header.
 * Documentation only — no compile-time distinction from RECDES. */
using OOS_RECDES = RECDES;

#define OOS_NUM_BEST_SPACESTATS 10

#define OOS_STATS_NEXT_BEST_INDEX(i) \
  (((i) + 1) % OOS_NUM_BEST_SPACESTATS)
#define OOS_STATS_PREV_BEST_INDEX(i) \
  (((i) == 0) ? (OOS_NUM_BEST_SPACESTATS - 1) : ((i) - 1))

typedef struct oos_bestspace OOS_BESTSPACE;
struct oos_bestspace
{
  VPID vpid;
  int freespace;
};

typedef struct oos_hdr_stats OOS_HDR_STATS;
struct oos_hdr_stats
{
  VFID oos_vfid;
  struct
  {
    int num_pages;
    int num_recs;
    float recs_sumlen;
    int num_other_high_best;
    int num_high_best;
    int num_substitutions;
    int num_second_best;
    int head_second_best;
    int tail_second_best;
    int head;
    VPID full_search_vpid;
    VPID second_best[OOS_NUM_BEST_SPACESTATS];
    OOS_BESTSPACE best[OOS_NUM_BEST_SPACESTATS];
  } estimates;

  int reserve0_for_future;
  int reserve1_for_future;
};

extern int oos_create_file (THREAD_ENTRY *thread_p, VFID &oos_vfid);
extern int oos_remove_file (THREAD_ENTRY *thread_p, const VFID &oos_vfid);
extern int oos_remove_page (THREAD_ENTRY *thread_p, const VFID &oos_vfid, const VPID &vpid);
extern int oos_insert (THREAD_ENTRY *thread_p, const VFID &oos_vfid, RECDES &recdes, OID &oid);
/* Read an OOS value into a caller-owned span.
 *
 * dest.size() is the authoritative expected payload length: the caller has
 * already obtained it (typically from the inline 8B length in the heap record
 * or from oos_get_length in test contexts) and sized the span accordingly.
 * The OOS chain header's total_data_length is cross-validated against
 * dest.size(); disagreement is treated as on-disk corruption.
 *
 * Span semantics decouple "expected payload length" from "underlying buffer
 * capacity", so the caller may back the span with a scratch buffer larger
 * than the payload without having to coerce a RECDES.area_size into a lie.
 */
extern int oos_read (THREAD_ENTRY *thread_p, const OID &oid, cubbase::span<char> dest);
extern int oos_delete (THREAD_ENTRY *thread_p, const VFID &oos_vfid, const OID &oid);
extern int oos_get_length (THREAD_ENTRY *thread_p, const OID &oid);

extern int oos_rv_redo_delete (THREAD_ENTRY *thread_p, LOG_RCV *rcv);
extern int oos_rv_redo_insert (THREAD_ENTRY *thread_p, LOG_RCV *rcv);

typedef enum
{
  OOS_FINDSPACE_FOUND = 0,
  OOS_FINDSPACE_NOTFOUND,
  OOS_FINDSPACE_ERROR
} OOS_FINDSPACE;

extern int oos_bestspace_initialize (void);
extern int oos_bestspace_finalize (void);

#ifdef __cplusplus
extern "C"
{
#endif

extern void oos_push_oos_oid (THREAD_ENTRY *thread_p, const OID *oid);

#ifdef __cplusplus
}
#endif

#endif /* _OOS_FILE_HPP_ */
