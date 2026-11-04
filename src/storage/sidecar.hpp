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

/*
 * sidecar.hpp - large variable values kept out of the heap record, in a store file of the record's heap
 *
 * The heap calls this module from its own entry and exit points.  A hook tests a value its function already has,
 * inside unlikely (), and only then calls out; the inline predicates below are those tests.  Every body is a
 * noinline function in sidecar.cpp, so a table that never uses the feature pays for the tests alone.
 */

#ifndef _SIDECAR_HPP_
#define _SIDECAR_HPP_

#if !defined (SERVER_MODE) && !defined (SA_MODE)
#error Belongs to server module
#endif /* !defined (SERVER_MODE) && !defined (SA_MODE) */

#include "heap_attrinfo.h"
#include "heap_file.h"
#include "sidecar_format.h"
#include "storage_common.h"
#include "thread_compat.hpp"

/* The shortest image the write policy can move a value out of: sidecar_inline_target, or the minimum value size
 * under sidecar_force_external. */
extern int sidecar_Policy_min_length;

/*
 * Writes: heap_insert_logical, heap_update_logical and heap_delete_logical (storage/heap_file.c), so every
 * producer of rows reaches them.  A plan made by one of them is released by sidecar_op_end () at every exit.
 */

/* before the INSERT image's header is adjusted, with no page held; a ref in an INSERT image is an error */
extern int sidecar_plan_insert (THREAD_ENTRY *thread_p, HEAP_OPERATION_CONTEXT *context);
/* writes the plan's store records, then points context->recdes_p at the hot record; no data page held */
extern int sidecar_apply_plan (THREAD_ENTRY *thread_p, HEAP_OPERATION_CONTEXT *context);
/* under the hot page latch, reads only; old_stored is the version being replaced, the home record or the forward
 * record of a relocated row */
extern int sidecar_plan_update (THREAD_ENTRY *thread_p, HEAP_OPERATION_CONTEXT *context,
				const RECDES *old_stored);
extern int sidecar_plan_delete (THREAD_ENTRY *thread_p, HEAP_OPERATION_CONTEXT *context,
				const RECDES *old_stored);
extern void sidecar_op_end_plan (THREAD_ENTRY *thread_p, HEAP_OPERATION_CONTEXT *context, int rc);
/* the UPDATE column writer, for a value in state HEAP_KEPT_REF_ATTRVALUE: copies the old record's ref */
extern SCAN_CODE sidecar_write_kept_ref (HEAP_CACHE_ATTRINFO *attr_info, int value_index,
    OR_BUF *buf, char **ptr_varvals, int offset_size, int header_size);

/*
 * Reads: the exits of the heap fetch functions and the attribute layer.  Only a record with
 * OR_MVCC_FLAG_HAS_SIDECAR reaches them.
 */

/* a fetched record with refs: expands it into an inline record unless the reader may keep it lazy.  Asking the
 * caller to read the row's current version again is a scan code of its own, so the exits hold no out-parameter
 * (an address-taken local grows the frame of every function the exit is inlined into). */
extern SCAN_CODE sidecar_on_fetch (THREAD_ENTRY *thread_p, HEAP_SCANCACHE *scan_cache, RECDES *recdes);
/* a variable attribute of a record with refs: true when its slot holds a ref and raw now points at the value;
 * false leaves the attribute to the ordinary pointer */
extern bool sidecar_point_variable (const HEAP_REC_LAYOUT *layout, HEAP_CACHE_ATTRINFO *attr_info,
				    const OR_ATTRIBUTE *attrepr, RECDES *raw);
/* a bound variable attribute of a record with refs, for a key: the bytes to decode, disk_data unless its slot
 * holds a ref */
extern char *sidecar_midxkey_point (HEAP_CACHE_ATTRINFO *attr_info, const RECDES *recdes,
				    const OR_ATTRIBUTE *att, char *disk_data);
/* before a scan fixes another hot page: releases the store page its cursor keeps, since nobody fixes a hot page
 * while holding a store page */
extern void sidecar_cursor_release (THREAD_ENTRY *thread_p, HEAP_SCANCACHE *scan_cache);

/* A stored hot record has OR_MVCC_FLAG_HAS_SIDECAR exactly when it ends with a trailer, and a header that grows
 * to OR_MVCC_MAX_HEADER_SIZE never makes it REC_BIGONE.  Checked after heap_insert_physical (),
 * heap_update_physical () and every DELID write. */
#if !defined (NDEBUG)
extern void sidecar_assert_record_format (const RECDES *recdes);
#define SIDECAR_ASSERT_RECORD_FORMAT(recdes) sidecar_assert_record_format (recdes)
#else /* NDEBUG */
#define SIDECAR_ASSERT_RECORD_FORMAT(recdes) ((void) 0)
#endif /* NDEBUG */

/* an image the write policy must see: long enough to move a value out of, or holding refs already */
STATIC_INLINE bool
sidecar_record_needs_policy (const RECDES *image)
{
  return image->length > sidecar_Policy_min_length || sidecar_record_has_refs (image);
}

/* the module's own store writes pass through heap_insert_logical unplanned */
STATIC_INLINE bool
sidecar_insert_needs_plan (const HEAP_OPERATION_CONTEXT *context)
{
  return !context->sidecar_store_op && sidecar_record_needs_policy (context->recdes_p);
}

/* The old home record has refs, or the new image needs the policy.  The forward record of a relocated row is
 * tested in heap_update_relocation (), where it is read. */
STATIC_INLINE bool
sidecar_update_needs_plan (const HEAP_OPERATION_CONTEXT *context)
{
  return ((context->record_type == REC_HOME && sidecar_record_has_refs (&context->home_recdes))
	  || sidecar_record_needs_policy (context->recdes_p));
}

/* the old home record has refs; heap_delete_relocation () tests a relocated row's forward record */
STATIC_INLINE bool
sidecar_delete_needs_plan (const HEAP_OPERATION_CONTEXT *context)
{
  return context->record_type == REC_HOME && sidecar_record_has_refs (&context->home_recdes);
}

STATIC_INLINE void
sidecar_op_end (THREAD_ENTRY *thread_p, HEAP_OPERATION_CONTEXT *context, int rc)
{
  if (unlikely (context->sc_plan != NULL))
    {
      sidecar_op_end_plan (thread_p, context, rc);
    }
}

#endif /* _SIDECAR_HPP_ */
