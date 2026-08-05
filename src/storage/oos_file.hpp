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

/* Caller-owned byte span for OOS payloads. size() is the authoritative length;
 * oos_insert only reads from it, oos_read only writes. Named alias because the
 * .c-file formatter mangles `cubbase::span<char>(...)`'s angle brackets. */
using oos_buffer = cubbase::span<char>;

struct oos_insert_request
{
  oos_buffer src;
  OID *oid_out;
};

struct oos_read_request
{
  OID oid;
  oos_buffer dest;
};

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

extern int oos_create_file (THREAD_ENTRY *thread_p, const HFID &heap_hfid, const OID &class_oid, VFID &oos_vfid);
#if defined (CUBRID_UNIT_TEST_ENABLED)
/* Low-level OOS tests use a synthetic, non-null owner descriptor while exercising storage in isolation. */
extern int oos_create_file (THREAD_ENTRY *thread_p, VFID &oos_vfid);
#endif /* CUBRID_UNIT_TEST_ENABLED */
extern int oos_remove_file (THREAD_ENTRY *thread_p, const VFID &oos_vfid);
extern int oos_remove_page (THREAD_ENTRY *thread_p, const VFID &oos_vfid, const VPID &vpid);
/* Inserts src.size() bytes; on multi-page payloads, oid is the head-chunk OID. */
extern int oos_insert (THREAD_ENTRY *thread_p, const VFID &oos_vfid, oos_buffer src, OID &oid);
/* Inserts requests in logical order; each request receives its head OOS OID. */
extern int oos_insert_many (THREAD_ENTRY *thread_p, const VFID &oos_vfid, cubbase::span<oos_insert_request> requests);
/* Reads exactly dest.size() bytes; the caller obtains the length from the
 * heap record's inline 8B field (or oos_get_length in tests) and sizes dest. */
extern int oos_read (THREAD_ENTRY *thread_p, const OID &oid, oos_buffer dest);
extern int oos_read_many (THREAD_ENTRY *thread_p, cubbase::span<oos_read_request> requests);
extern int oos_delete (THREAD_ENTRY *thread_p, const VFID &oos_vfid, const OID &oid);
/* Idempotency probe: *out_exists is true iff the chunk's slot is still present. A deallocated page
 * or a removed slot both report "gone" with NO_ERROR; any other failure is propagated. */
extern int oos_chunk_exists (THREAD_ENTRY *thread_p, const OID &oid, bool *out_exists);
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

struct oos_stats_info
{
  int has_oos_file;		/* 0 if class has no OOS file, 1 otherwise */
  VFID oos_vfid;
  int num_user_pages;		/* physical user pages allocated to OOS file */
  int page_size;		/* DB_PAGESIZE */
  int num_recs;			/* live OOS records tracked by OOS_HDR_STATS */
  INT64 recs_sumlen;		/* sum of live OOS record body bytes */
};
using OOS_STATS_INFO = struct oos_stats_info;

extern int xoos_get_stats_by_class_oid (THREAD_ENTRY *thread_p, const OID *class_oid, OOS_STATS_INFO *out);
extern int oos_get_stats_by_vfid (THREAD_ENTRY *thread_p, const VFID &oos_vfid, OOS_STATS_INFO *out);

#if defined(CUBRID_UNIT_TEST_ENABLED)
struct oos_debug_counters
{
  unsigned long long insert_many_calls;
  unsigned long long insert_many_requests;
  unsigned long long single_page_batch_count;
  unsigned long long insert_reused_pages;
  unsigned long long insert_fresh_pages;
  unsigned long long insert_values_per_fixed_page;
  unsigned long long read_many_calls;
  unsigned long long read_many_requests;
  unsigned long long read_many_grouped_head_pages;
  unsigned long long read_values_per_fixed_page;
};

/* One-shot publication failure seams used by focused SERVER_MODE tests. */
extern void oos_test_fail_insert_many_after_publications (int publication_count);
extern void oos_test_throw_bad_alloc_on_next_oid_publication ();
extern void oos_test_disarm_insert_publication_failures ();
#endif

#endif /* _OOS_FILE_HPP_ */
