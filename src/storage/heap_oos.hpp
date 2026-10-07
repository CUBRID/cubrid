/*
 *
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

/*
 * heap_oos.hpp - Heap-level OOS (Out-of-row Overflow Storage) expansion and eager cleanup
 */

#ifndef _HEAP_OOS_HPP_
#define _HEAP_OOS_HPP_

#include "heap_file.h"
#include "oos_file.hpp"
#include "storage_common.h"

#include <vector>

class heap_pending_record;

/* Decoded reference, not the packed record format. Both alternatives copy into
 * caller-owned storage; callers never borrow a pending payload or a page. */
class heap_oos_value_ref
{
  public:
    heap_oos_value_ref () : m_kind (kind::disk), m_length (0), m_value () {}
    static int decode (const RECDES &record, int location, heap_oos_value_ref &ref,
		       const heap_pending_record *pending = nullptr);
    static void encode_pending (char *stub, DB_BIGINT length, int index);
    std::size_t length () const
    {
      return m_length;
    }
    int read_into (THREAD_ENTRY *thread_p, oos_buffer destination) const;

  private:
    /* The caller locates and bounds-checks the field before decoding its bytes. */
    static int decode_stub (const RECDES &record, char *stub, heap_oos_value_ref &ref,
			    const heap_pending_record *pending);
    enum class kind { memory, disk };
    kind m_kind;
    std::size_t m_length;
    union value
    {
      const char *memory;
      oos_chain_ref disk;
      value () : disk {} {}
    } m_value;
    friend int heap_oos_read_grouped_payloads (THREAD_ENTRY *, RECDES *, HEAP_CACHE_ATTRINFO *,
	std::vector<RECDES> &, bool *, const heap_pending_record *);
    friend int heap_oos_finalize_record (THREAD_ENTRY *, const OID *, RECDES *, heap_pending_record *);
};

/* Finalize a locally prepared record in place after routing, before heap/index
 * writes. A failed call must be rolled back; it is not a retryable insertion. */
extern int heap_oos_finalize_record (THREAD_ENTRY *thread_p, const OID *destination, RECDES *record,
				     heap_pending_record *pending = nullptr);
/* Heap-row storage contract; generic descriptors and slotted-page metadata are not row inputs. */
extern int heap_oos_validate_disk_record (THREAD_ENTRY *thread_p, const OID *class_oid, const RECDES *record);
extern int heap_prepare_oos_record (THREAD_ENTRY *thread_p, const OID *source_class, RECDES *source,
				    heap_pending_record *pending);

enum heap_oos_demote_priority
{
  HEAP_OOS_DEMOTE_NORMAL = 0,
  HEAP_OOS_DEMOTE_PREFER_INLINE = 1
};

struct heap_oos_demote_candidate
{
  heap_oos_demote_priority priority;
  int size;
  int attr_index;
};

inline heap_oos_demote_priority
heap_oos_get_demote_priority (bool prefer_inline)
{
  return prefer_inline ? HEAP_OOS_DEMOTE_PREFER_INLINE : HEAP_OOS_DEMOTE_NORMAL;
}

inline bool
heap_oos_demote_candidate_precedes (const heap_oos_demote_candidate &a, const heap_oos_demote_candidate &b)
{
  if (a.priority != b.priority)
    {
      return a.priority < b.priority;
    }
  if (a.size != b.size)
    {
      return a.size > b.size;
    }
  return a.attr_index > b.attr_index;
}

extern SCAN_CODE heap_record_replace_oos_oids (THREAD_ENTRY *thread_p, HEAP_GET_CONTEXT *context);

/* Grouped lazy OOS Resolve for heap_attrinfo_read_dbvalues (heap_file.c dispatches into it). */

/* Parse the OOS inline stub [OID (8B) | full_length (8B) | identity stamp (8B)] of OOS-marked variable
 * attribute `location` into the chain reference oos_read consumes and the value's full length. The
 * attribute's field is checked to be exactly one stub inside the record before any of it is read
 * (CBRD-26950). */
extern int heap_oos_parse_inline_ref (const RECDES *recdes, int location, oos_chain_ref *oos_ref,
				      DB_BIGINT *oos_len);

/* Prefetch requested OOS-marked attributes of an OOS-bearing record through a single oos_read_many()
 * when grouped Resolve applies. The caller filters non-OOS records before entering this helper.
 * oos_payloads[i].data then holds attribute i's raw OOS bytes (NULL when attr i is not OOS);
 * heap_file.c's grouped read loop transforms them and calls heap_oos_free_grouped_payloads(). */
extern int heap_oos_read_grouped_payloads (THREAD_ENTRY *thread_p, RECDES *recdes,
    HEAP_CACHE_ATTRINFO *attr_info, std::vector<RECDES> &oos_payloads, bool *grouped_applied,
    const heap_pending_record *pending = nullptr);
extern void heap_oos_free_grouped_payloads (std::vector<RECDES> &oos_payloads);

/* Begin one logical heap-record OOS insert preparation by clearing its OID/LSA publication state.
 * Resolves the current LOG_TDES before clearing either side, so failure leaves both containers untouched. */
extern SCAN_CODE heap_oos_begin_insert_publication (THREAD_ENTRY *thread_p);

/* Insert already-serialized attribute values into the class OOS file. Attribute serialization and
 * the logical-start publication reset stay in heap_file.c; OOS lookup and oos_insert_many live here. */
extern SCAN_CODE heap_oos_insert_serialized_values (THREAD_ENTRY *thread_p, const OID *class_oid,
    cubbase::span<oos_insert_request> requests);

#if defined(CUBRID_UNIT_TEST_ENABLED)
/* One-shot failure seam immediately before the OOS VFID lookup owned by the heap insert wrapper. */
extern void heap_oos_test_fail_preparation_once ();
extern void heap_oos_test_fail_heap_insert_once ();
extern void heap_oos_test_fail_before_vfid_lookup_once ();
extern void heap_oos_test_disarm_fail_before_vfid_lookup ();
#endif

/* Eager OOS cleanup for the non-MVCC (!is_mvcc_op) heap delete/update paths. Deletes the OOS value
 * chains referenced by old_recdes and not referenced by new_recdes (NULL = delete all). A reference
 * whose target is gone or reused (deallocated or retyped head page, empty head slot, identity stamp
 * mismatch) is skipped: the DML completes and the error stack stays clean. Skips are reported once per
 * call as a notification in the server error log, naming how many chains were skipped and describing
 * the first. A stamp-matching non-head target and operational failures are errors (CBRD-26950). */
extern int heap_oos_delete_unreferenced (THREAD_ENTRY *thread_p, HEAP_OPERATION_CONTEXT *context,
    const RECDES *old_recdes, const RECDES *new_recdes, const char *op_ctx);

#if defined(CUBRID_UNIT_TEST_ENABLED)
/* Observability of the skipped-cleanup diagnostic: how many notifications heap_oos_delete_unreferenced
 * emitted since the last reset, and the first outcome (-1 when none) and chain count (0 when none) the
 * last one carried. */
extern int heap_oos_test_skipped_cleanup_notifications ();
extern int heap_oos_test_last_skipped_cleanup_outcome ();
extern int heap_oos_test_last_skipped_cleanup_count ();
extern void heap_oos_test_reset_skipped_cleanup_diagnostics ();
#endif

#endif /* _HEAP_OOS_HPP_ */
