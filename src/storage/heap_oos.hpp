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
#include "storage_common.h"

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

/* Grouped inline-OOS Resolve for heap_attrinfo_read_dbvalues (heap_file.c dispatches into it). */

/* Parse an OOS-marked variable attribute's inline reference [OID (8B) | full_length (8B)]. */
extern int heap_oos_parse_inline_ref (RECDES *recdes, const char *inline_ptr, OID *oos_oid, DB_BIGINT *oos_len);

/* Return a pointer to the attribute's inline OOS reference, or NULL if it is not inline-OOS here. */
extern const char *heap_oos_attr_inline_ptr (RECDES *recdes, HEAP_ATTRVALUE *value);

/* Read every requested attribute of recdes, resolving all inline-OOS values through one grouped
 * oos_read_many() and reading the rest with the scalar reader. */
extern int heap_oos_read_dbvalues_grouped (THREAD_ENTRY *thread_p, RECDES *recdes, HEAP_CACHE_ATTRINFO *attr_info);

/* Eager OOS cleanup for the non-MVCC (!is_mvcc_op) heap delete/update paths. Deletes the OOS
 * records referenced by old_recdes and not referenced by new_recdes (NULL = delete all). */
extern int heap_oos_delete_unreferenced (THREAD_ENTRY *thread_p, HEAP_OPERATION_CONTEXT *context,
    const RECDES *old_recdes, const RECDES *new_recdes, const char *op_ctx);

#endif /* _HEAP_OOS_HPP_ */
