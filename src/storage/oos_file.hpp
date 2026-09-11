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

#include <vector>

struct oos_record_header
{
  INT64 total_data_length;	/* total length of user data across all chunks (excluding OOS headers) */
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

typedef struct log_rcv LOG_RCV;

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
/* Internal LOB storage reuses the OOS file layout under its own file type; the owner descriptor is required so
 * FILE_INTERNAL_LOB stays traversable and lock-protectable exactly like FILE_OOS. */
extern int oos_create_file_with_type (THREAD_ENTRY *thread_p, int file_type, const HFID &heap_hfid,
				      const OID &class_oid, VFID &oos_vfid);
#if defined (CUBRID_UNIT_TEST_ENABLED)
/* Low-level OOS tests use a synthetic, non-null owner descriptor while exercising storage in isolation. */
extern int oos_create_file (THREAD_ENTRY *thread_p, VFID &oos_vfid);
#endif /* CUBRID_UNIT_TEST_ENABLED */
extern int oos_remove_file (THREAD_ENTRY *thread_p, const VFID &oos_vfid);
/* Batch empty-page reclaim for an explicit candidate list (vacuum's fast path). Candidates are
 * sorted and deduped in place. Idempotent and zero-wait per page: busy, already-deallocated,
 * re-filled and sticky-first-page candidates are skipped, and a page whose last writer may still
 * be active is deferred until a later call. Stops at the first error (notably ER_INTERRUPTED)
 * and propagates it; unprocessed candidates stay allocated for a future pass.
 * Call only AFTER the deletes that emptied the pages are committed — a live undo could otherwise
 * restore chunks onto a deallocated page. */
extern int oos_reclaim_empty_pages (THREAD_ENTRY *thread_p, const VFID &oos_vfid, std::vector<VPID> &candidates);
/* Inserts src.size() bytes; on multi-page payloads, oid is the head-chunk OID. */
extern int oos_insert (THREAD_ENTRY *thread_p, const VFID &oos_vfid, oos_buffer src, OID &oid);
/* Inserts requests in logical order; each request receives its head OOS OID. */
extern int oos_insert_many (THREAD_ENTRY *thread_p, const VFID &oos_vfid, cubbase::span<oos_insert_request> requests);

/* ---- Incremental chain writer ----------------------------------------------------------------
 *
 * oos_insert () needs the whole value contiguous in memory, so a value larger than memory cannot
 * go through it.  This writer builds the SAME chunk chain incrementally: the caller supplies the
 * chunks TAIL-FIRST (the value's last chunk first), and each chunk's header is stamped with the
 * OID of the chunk that follows it - which is known precisely because it was written already.
 * The final chunk inserted is the chain head (chunk_index 0) and its OID addresses the value.
 *
 * Each chunk is inserted and published for replication exactly like a standalone single-chunk
 * record: no boundary markers, so the applier never reassembles the whole value in memory.
 *
 * The chunk count must be known up front so chunk_index can be stamped; the caller derives it
 * from the value's total length (see oos_chain_max_chunk_payload).
 */
struct oos_chain_writer
{
  VFID oos_vfid;
  INT64 total_data_length;	/* stamped into every chunk header */
  int next_index;		/* chunk_index of the next chunk to insert; counts down to 0 (head) */
  OID next_chunk_oid;		/* the already-written following chunk; NULL while writing the tail */
};
using OOS_CHAIN_WRITER = struct oos_chain_writer;

/* Usable payload bytes in one chunk (the chain header is already excluded). */
extern int oos_chain_max_chunk_payload (void);
/* total_chunks: how many chunks the value will be split into (>= 1). */
extern void oos_chain_insert_begin (const VFID &oos_vfid, INT64 total_data_length, int total_chunks,
				    OOS_CHAIN_WRITER &writer);
/* Inserts the next chunk (tail-first order) and returns its OID. */
extern int oos_chain_insert_next (THREAD_ENTRY *thread_p, OOS_CHAIN_WRITER &writer, oos_buffer chunk, OID &oid);
/* Inserts one chunk with a caller-supplied chain position.  Used where the position is dictated from
 * outside instead of a local countdown - notably HA apply, which re-inserts a replicated chunk keeping
 * the master's chunk_index but relinking next_chunk_oid to the slave's own preceding chunk. */
extern int oos_chain_insert_chunk (THREAD_ENTRY *thread_p, const VFID &oos_vfid, oos_buffer chunk,
				   INT64 total_data_length, int chunk_index, const OID &next_chunk_oid, OID &oid);
/* Reads one chunk addressed directly by OID (the chain walk is the caller's), returning its chain
 * header and payload.  dest may be empty to fetch only the header, which is how a chain is walked
 * without copying payload.  Complements oos_read_open/oos_read_pull, which stream a whole chain from
 * its head and give the caller no access to individual chunk positions. */
extern int oos_chain_read_chunk (THREAD_ENTRY *thread_p, const OID &oid, oos_buffer dest, int &payload_len,
				 OOS_RECORD_HEADER &header);
/* True when the next chunk to insert is the chain head (chunk_index 0). */
extern bool oos_chain_insert_is_head_next (const OOS_CHAIN_WRITER &writer);
/* True once every chunk has been inserted (the last returned OID is the head). */
extern bool oos_chain_insert_done (const OOS_CHAIN_WRITER &writer);
/* Reads exactly dest.size() bytes; the caller obtains the length from the
 * heap record's inline 8B field (or oos_get_length in tests) and sizes dest. */
extern int oos_read (THREAD_ENTRY *thread_p, const OID &oid, oos_buffer dest);
extern int oos_read_many (THREAD_ENTRY *thread_p, cubbase::span<oos_read_request> requests);
/* touched_vpids (optional): pages that lost a chunk are appended (with duplicates) so
 * batch-boundary callers can feed oos_reclaim_empty_pages after committing. */
extern int oos_delete (THREAD_ENTRY *thread_p, const VFID &oos_vfid, const OID &oid,
		       std::vector<VPID> *touched_vpids = NULL);
/* Idempotency probe: *out_exists is true iff the chunk's slot is still present. A deallocated page
 * or a removed slot both report "gone" with NO_ERROR; any other failure is propagated. */
extern int oos_chunk_exists (THREAD_ENTRY *thread_p, const OID &oid, bool *out_exists);
extern INT64 oos_get_length (THREAD_ENTRY *thread_p, const OID &oid);

/* Forward-only streaming reader over an OOS chunk chain. Lets a caller pull an
 * arbitrarily large payload in bounded pieces without materializing the whole
 * value: open with oos_read_open on the head OID, then call oos_read_pull
 * repeatedly until it reports nread == 0 (chain exhausted). Sequential pulls walk
 * the chain once (O(total)), unlike repeated oos_read calls from the head. */
struct oos_reader
{
  OID current;			/* chunk currently being read; NULL OID once exhausted */
  int chunk_consumed;		/* bytes already returned from current chunk's payload */
  int next_index;		/* expected chunk_index of `current` (0 at head) */
};
using OOS_READER = struct oos_reader;

extern int oos_read_open (THREAD_ENTRY *thread_p, const OID &head_oid, OOS_READER &reader);
extern int oos_read_pull (THREAD_ENTRY *thread_p, OOS_READER &reader, oos_buffer dest, int &nread);

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
/* Simulates a process restart for the reclaim bookkeeping: the next growth of each file falls
 * under the boot rule. */
extern void oos_test_reclaim_reset_side_map ();
/* Deterministic concurrency seams for the growth-gate single-flight contract. */
extern void oos_test_reclaim_force_sweep_in_progress (const VFID &oos_vfid);
extern void oos_test_reclaim_release_sweep (const VFID &oos_vfid);
extern int oos_test_reclaim_sweep_step (THREAD_ENTRY *thread_p, const VFID &oos_vfid);
extern int oos_test_reclaim_waiter_count ();
extern void oos_test_fail_next_reclaim_write_fix ();
#endif

#endif /* _OOS_FILE_HPP_ */
