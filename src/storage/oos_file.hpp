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

#include "dbtype_def.h"		/* DB_BIGINT */
#include "log_lsa.hpp"
#include "recovery.h"		/* LOG_RCV */
#include "span.hpp"
#include "storage_common.h"
#include "thread_compat.hpp"

#include <cstdint>
#include <vector>

struct oos_record_header
{
  int total_data_length;	/* total length of user data across all chunks (excluding OOS headers) */
  int chunk_index;		/* 0-based index of this chunk in the chain */
  OID next_chunk_oid;		/* OID of next chunk, or NULL OID if this is the last */
  LOG_LSA next_chunk_stamp;	/* identity stamp the next chunk carries, or NULL_LSA if this is the last.
				 * Chains are written tail first, so it is known when this link is written.
				 * The reclaimer follows a link only when the slot it names still carries
				 * this stamp, so a rerun never walks into a reused slot (ADR-0008). */
  LOG_LSA identity_stamp;	/* Identity stamp of this chunk (CBRD-26950): the page LSA observed under
				 * the write latch immediately before this chunk's insert was logged. The
				 * head chunk's value is also stored in the owning heap record's OOS inline
				 * stub, and oos_delete reclaims a chain only when the two are equal, so with
				 * logging enabled a reused (volid|pageid|slotid) is never mistaken for the
				 * chunk a stale OOS reference was created for. Distinct stamps across slot
				 * reuse need the skipped-append case to be impossible, so they are a
				 * logged-operation guarantee only; see the invariants at the issuing site in
				 * oos_file.cpp. Each chunk carries the LSA of its own page; only the head
				 * chunk's value reaches the stub. */
};
using OOS_RECORD_HEADER = struct oos_record_header;

#define OOS_RECORD_HEADER_SIZE ((int) sizeof (OOS_RECORD_HEADER))

/* Reference to one OOS value chain: the head OOS OID plus the identity stamp the chain was created
 * with. This is the parsed form of a heap record's OOS inline stub. Keeping the pair in one value
 * means it can never be split by accident on the way to oos_delete or oos_read (CBRD-26950). */
struct oos_chain_ref
{
  OID head_oid;
  LOG_LSA identity_stamp;
};

/* Reclaim intents (ADR-0008). A unit that decides chains are dead records them as a postpone, so the
 * chunks are deleted only after that unit commits, with redo-only records:
 *   RVOOS_RECLAIM_POSTPONE      : REMOVE (vacuum sysop) and the SA/non-MVCC eager path. The postpone
 *                                 runs oos_delete on each reference.
 *   RVOOS_UPDATE_DROP_POSTPONE  : an MVCC UPDATE. The postpone appends RVOOS_NOTIFY_VACUUM under the
 *                                 UPDATE's MVCCID; vacuum runs oos_delete once no snapshot can still
 *                                 read the old version.
 * An abort of the deciding unit, including a statement or savepoint rollback, discards the postpone.
 * A failed reclaim stops the server (fail-stop) so restart recovery reruns the durable postpone. */
enum oos_reclaim_intent_flag
{
  OOS_RECLAIM_DIAGNOSE_SKIPS = 0x1	/* the owner held the only reference; report a skipped chain */
};

extern int oos_log_reclaim_intent (THREAD_ENTRY *thread_p, const VFID &oos_vfid, const OID &owner_oid, int flags,
				   const char *op_ctx, const std::vector<oos_chain_ref> &refs);
extern int oos_log_update_drop_intent (THREAD_ENTRY *thread_p, const HFID &heap_hfid,
				       const std::vector<oos_chain_ref> &refs);
/* Vacuum's consumer of one RVOOS_NOTIFY_VACUUM record. emptied_vpids receives the pages the deletes
 * emptied. The notify record names its OOS file; it is returned through oos_vfid_out. Returns NO_ERROR or
 * ER_INTERRUPTED (shutdown); any other failure stops the server, so restart processes the block again.
 * notify_data is copied before the first page fix, so it may point into the caller's log page. */
extern int oos_reclaim_notified_chains (THREAD_ENTRY *thread_p, const char *notify_data, int notify_length,
					VFID *oos_vfid_out, std::vector<VPID> *emptied_vpids);

/* While one is in scope on a thread, the reclaim postpones that run on that thread report the pages
 * they empty to it instead of discarding them. Vacuum opens one around the sysop commit of a REMOVE,
 * then hands the pages to oos_reclaim_empty_pages after unfixing the heap page. */
class oos_emptied_page_sink
{
  public:
    explicit oos_emptied_page_sink (std::vector<VPID> &pages);
    ~oos_emptied_page_sink ();
    oos_emptied_page_sink (const oos_emptied_page_sink &) = delete;
    oos_emptied_page_sink &operator= (const oos_emptied_page_sink &) = delete;

  private:
    std::vector<VPID> *m_previous;
};

/* What oos_delete did with the chain a reference names. Every skip is a successful call that modified
 * nothing, set no error and reported no reclaim candidate: the chain the reference described is gone,
 * and whatever occupies its head location now belongs to another slot or page incarnation. The outcome
 * lets a caller tell a real reclamation from a skip without an occupancy probe, and name the reason when
 * it diagnoses the skip (CBRD-26950). A failed call reports OOS_DELETE_OUTCOME_UNKNOWN, so a caller that
 * reads the outcome before the return code is never told a chain was reclaimed when it was not. */
enum oos_delete_outcome
{
  OOS_DELETE_OUTCOME_UNKNOWN = 0,	/* the call failed; it reached no conclusion about the chain */
  OOS_DELETE_RECLAIMED,			/* the head carried the reference's stamp; every chunk was deleted */
  OOS_DELETE_SKIPPED_PAGE_GONE,		/* the head page is deallocated or no longer an OOS page */
  OOS_DELETE_SKIPPED_SLOT_EMPTY,	/* the head page is an OOS page, but the head slot holds no record */
  OOS_DELETE_SKIPPED_STAMP_MISMATCH	/* the head slot holds a chunk that carries another identity stamp */
};

/* Short, distinct English name of an outcome for diagnostics. */
extern const char *oos_delete_outcome_string (oos_delete_outcome outcome);

/* The OOS inline stub stores the identity stamp as one 64-bit integer written with the bigint
 * helpers, so the stub stays 8-byte aligned at 24 bytes: OID (8) + full length (8) + stamp (8).
 * The stock LSA helper spends 12 bytes and is not used. pageid takes the upper 48 bits and offset
 * the lower 16, so NULL_LSA (-1, -1) packs to -1 and every LOG_LSA round-trips exactly. These two
 * helpers are used only for the stub; the chunk header stores the raw LOG_LSA. */
inline DB_BIGINT
oos_pack_identity_stamp (const LOG_LSA &identity_stamp)
{
  return (DB_BIGINT) (((std::uint64_t) identity_stamp.pageid << 16)
		      | ((std::uint64_t) identity_stamp.offset & 0xFFFFu));
}

inline LOG_LSA
oos_unpack_identity_stamp (DB_BIGINT packed)
{
  return LOG_LSA ((std::int64_t) packed >> 16, (std::int16_t) ((std::uint64_t) packed & 0xFFFFu));
}

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
  LOG_LSA *identity_stamp_out;	/* optional (may be NULL): receives the head chunk's identity stamp */
};

struct oos_read_request
{
  oos_chain_ref ref;
  oos_buffer dest;
};

#define OOS_NUM_BEST_SPACESTATS 10

#define OOS_STATS_NEXT_BEST_INDEX(i) \
  (((i) + 1) % OOS_NUM_BEST_SPACESTATS)

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
/* Batch empty-page reclaim for an explicit candidate list (vacuum's fast path). Candidates are
 * sorted and deduped in place. Idempotent and zero-wait per page: busy, already-deallocated,
 * re-filled and sticky-first-page candidates are skipped. Stops at the first error (notably
 * ER_INTERRUPTED) and propagates it; unprocessed candidates stay allocated for a future pass.
 * Chunk deletes are redo-only and never undone (ADR-0008), so no undo can target an empty page. */
extern int oos_reclaim_empty_pages (THREAD_ENTRY *thread_p, const VFID &oos_vfid, std::vector<VPID> &candidates);
/* Inserts src.size() bytes; on multi-page payloads, oid is the head-chunk OID. identity_stamp_out
 * (optional) receives the head chunk's identity stamp; a caller that persists an OOS inline stub
 * must store it next to the head OOS OID (CBRD-26950). */
extern int oos_insert (THREAD_ENTRY *thread_p, const VFID &oos_vfid, oos_buffer src, OID &oid,
		       LOG_LSA *identity_stamp_out = NULL);
/* Inserts requests in logical order; each request receives its head OOS OID and, when asked, the
 * head chunk's identity stamp. */
extern int oos_insert_many (THREAD_ENTRY *thread_p, const VFID &oos_vfid, cubbase::span<oos_insert_request> requests);
/* Reads exactly dest.size() bytes of the chain ref names; the caller obtains the length from the
 * heap record's inline 8B field (or oos_get_length in tests) and sizes dest. A stale reference fails
 * with ER_HEAP_OOS_CORRUPTED_RECORD and never delivers another chain's bytes into dest: a
 * deallocated head page, a head page that is no longer an OOS page, a missing head slot, a head
 * chunk whose identity stamp differs from ref.identity_stamp, and a stamp-matching target that is
 * not a chain head are all rejected before any payload is copied. Identity is compared before any
 * other property of the current occupant is interpreted. Operational failures (I/O, interrupt)
 * keep their own error. The head page is fixed with the deallocation-tolerant fix, which adds a
 * sector-reservation check (two fixes of cached volume pages) per head page (CBRD-26950). */
extern int oos_read (THREAD_ENTRY *thread_p, const oos_chain_ref &ref, oos_buffer dest);
extern int oos_read_many (THREAD_ENTRY *thread_p, cubbase::span<oos_read_request> requests);
/* The OOS reclaimer (ADR-0008): deletes the OOS value chain ref names with redo-only records, so a
 * delete is never undone. Call it only after the unit that decided the chain is dead has committed:
 * from a reclaim postpone, or from vacuum's RVOOS_NOTIFY_VACUUM consumer. It proves target identity
 * first: the head chunk must carry ref.identity_stamp, and every link is followed only when the slot
 * it names carries the stamp stored in the link. It deletes from the tail and the head last, so an
 * interrupted reclaim leaves a linked prefix with an intact head and a rerun finishes it.
 * A deallocated head page, a head page that is no longer an OOS page, a missing head slot or a
 * mismatched head stamp is a successful no-op that modifies nothing, leaves the error stack clean
 * and reports no candidate, which gives every caller retry idempotency without extra state as long
 * as the chain was written with logging enabled; the page type is checked under the latch before the
 * page is read as a slotted page.
 * A stamp-matching reference to a continuation chunk is malformed and fails with
 * ER_HEAP_OOS_CORRUPTED_RECORD before any chunk is modified, and a reference with no head OOS OID at
 * all fails with ER_HEAP_OOS_INVALID_ARGUMENT. Operational failures keep their own error (CBRD-26950).
 * emptied_vpids (optional): every page this delete left with zero records is appended once, so
 * batch-boundary callers can feed oos_reclaim_empty_pages after committing. Pages that still
 * hold other chunks are not candidates and are not reported.
 * outcome_out (optional): receives what happened, see oos_delete_outcome. Vacuum passes nothing and
 * stays quiet, because a retry finding its target gone is expected there; the eager cleanup of a
 * non-MVCC UPDATE or DELETE asks and diagnoses every skip, because the row it completes was the only
 * reference and a vanished or reused target is unexpected. */
extern int oos_delete (THREAD_ENTRY *thread_p, const VFID &oos_vfid, const oos_chain_ref &ref,
		       std::vector<VPID> *emptied_vpids = NULL, oos_delete_outcome *outcome_out = NULL);
/* Occupancy probe for tests and diagnostics: *out_exists is true iff SOME record occupies the slot
 * at oid. A deallocated page, a page that is no longer an OOS page and a removed slot all report
 * "gone" with NO_ERROR; any other failure is propagated. It proves occupancy, not identity: it
 * cannot tell the chunk a reference was created for from a later occupant of the same slot, so it
 * must never gate a delete. oos_delete verifies identity itself (CBRD-26950). */
extern int oos_chunk_exists (THREAD_ENTRY *thread_p, const OID &oid, bool *out_exists);
/* Reads the identity stamp the head chunk at head_oid currently carries, for building a chain
 * reference in tests and diagnostics. Fails when the page is deallocated or no longer an OOS page,
 * or the slot is absent. */
extern int oos_get_identity_stamp (THREAD_ENTRY *thread_p, const OID &head_oid, LOG_LSA *identity_stamp_out);
extern int oos_get_length (THREAD_ENTRY *thread_p, const OID &oid);

extern int oos_rv_redo_delete (THREAD_ENTRY *thread_p, LOG_RCV *rcv);
extern int oos_rv_redo_insert (THREAD_ENTRY *thread_p, LOG_RCV *rcv);
extern int oos_rv_reclaim_on_postpone (THREAD_ENTRY *thread_p, LOG_RCV *rcv);
extern int oos_rv_update_drop_on_postpone (THREAD_ENTRY *thread_p, LOG_RCV *rcv);
extern void oos_rv_dump_reclaim_intent (FILE *fp, int length, void *data);

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
  int num_recs;			/* live chunk records found by walking the OOS data pages, not OOS_HDR_STATS */
  INT64 recs_sumlen;		/* sum of their slot lengths, each including its OOS_RECORD_HEADER_SIZE header */
};
using OOS_STATS_INFO = struct oos_stats_info;

extern int oos_get_stats_by_vfid (THREAD_ENTRY *thread_p, const VFID &oos_vfid, OOS_STATS_INFO *out);

#if defined(CUBRID_UNIT_TEST_ENABLED)
extern int xoos_get_stats_by_class_oid (THREAD_ENTRY *thread_p, const OID *class_oid, OOS_STATS_INFO *out);

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
/* Reclaim intents (ADR-0008): the payload RVOOS_RECLAIM_POSTPONE and RVOOS_NOTIFY_VACUUM carry; a one-shot
 * failure of the reclaimer after it performed n_deletes more chunk deletes (0 fails before the next one,
 * -1 disarms); and a hook that replaces the server stop of the fail-stop policy, so a test can observe it
 * (NULL restores the stop). */
extern std::vector<char> oos_test_make_reclaim_intent_payload (const VFID &oos_vfid,
    const std::vector<oos_chain_ref> &refs);
/* The RVOOS_NOTIFY_VACUUM payload of an UPDATE that did not resolve the OOS file: vacuum looks it up from heap_hfid. */
extern std::vector<char> oos_test_make_update_drop_notify_payload (const HFID &heap_hfid,
    const std::vector<oos_chain_ref> &refs);
extern void oos_test_fail_reclaim_after_chunk_deletes (int n_deletes);
extern void oos_test_set_reclaim_fail_stop_hook (void (*hook) (int error));
/* Stops the process with std::_Exit (exit_code) right after the n-th following UPDATE drop postpone appended its
 * RVOOS_NOTIFY_VACUUM, with the log flushed up to it: a crash in the middle of a commit's postpones (0 disarms). */
extern void oos_test_exit_after_update_drop_notifications (int n_notifications, int exit_code);
/* Removes the files the synthetic-owner oos_create_file overload created that still exist. */
extern void oos_test_remove_created_files (THREAD_ENTRY *thread_p);
#endif

#endif /* _OOS_FILE_HPP_ */
