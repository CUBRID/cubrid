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

/* The longest image the write policy lets through untouched: sidecar_inline_target, or less than the minimum value
 * size under sidecar_force_external.  sidecar_refresh_parameters () keeps it in step with the parameters. */
extern int sidecar_Policy_min_length;

/* Module lifetime: heap_manager_initialize () and heap_manager_finalize () */
extern int sidecar_initialize (void);
extern void sidecar_finalize (void);
/* after a change of sidecar_inline_target or sidecar_force_external (sysprm_change_parameter_values ()) */
extern void sidecar_refresh_parameters (void);

/*
 * Writes: heap_insert_logical, heap_update_logical and heap_delete_logical (storage/heap_file.c), so every
 * producer of rows reaches them.  A plan made by one of them is released by sidecar_op_end () at every exit.
 */

/* before the INSERT image's header is adjusted, with no page held; a ref in an INSERT image is an error */
extern int sidecar_plan_insert (THREAD_ENTRY *thread_p, HEAP_OPERATION_CONTEXT *context);
/* before an UPDATE locates its row, with no page held, for an image that may move values out or holds kept refs:
 * reads what the plan needs of the image's representation */
extern int sidecar_prepare_update (THREAD_ENTRY *thread_p, HEAP_OPERATION_CONTEXT *context);
/* Under the hot page latch, reads only; old_stored is the version being replaced, the home record or the forward
 * record of a relocated row, NULL for a REC_BIGONE row.  Checks the kept refs (I-3), compares equal-length values,
 * lists the dropped refs and picks the values to move out.  A plan that writes no store record is applied here. */
extern int sidecar_plan_update (THREAD_ENTRY *thread_p, HEAP_OPERATION_CONTEXT *context,
				const RECDES *old_stored);
/* under the hot page latch: every store record of old_stored is to be stamped */
extern int sidecar_plan_delete (THREAD_ENTRY *thread_p, HEAP_OPERATION_CONTEXT *context,
				const RECDES *old_stored);
/* the plan writes the store, so the heap releases its pages, calls sidecar_apply_plan () and locates the row again */
extern bool sidecar_plan_writes_store (const HEAP_OPERATION_CONTEXT *context);
/* With no data page held: writes the plan's store records, fresh values first, then the stamps (I-4, I-7), and
 * points context->recdes_p at the hot record.  Once applied, the plan hooks let the next location step through. */
extern int sidecar_apply_plan (THREAD_ENTRY *thread_p, HEAP_OPERATION_CONTEXT *context);
extern void sidecar_op_end_plan (THREAD_ENTRY *thread_p, HEAP_OPERATION_CONTEXT *context, int rc);

/* locator_move_record (): a row that moves to another heap takes no ref along (I-3).  image holds kept refs, each one
 * of old_stored, the version the move replaces; out gets the image with their values read back as a row-lock holder,
 * in a buffer the caller frees with db_private_free () */
extern int sidecar_inline_kept (THREAD_ENTRY *thread_p, const OID *class_oid, const RECDES *old_stored,
				const RECDES *image, RECDES *out);

/* heap_attrinfo_set_uninitialized (): a value the UPDATE does not assign whose slot in old_recdes holds a ref keeps
 * it, in state HEAP_KEPT_REF_ATTRVALUE; false reads the value as usual */
extern bool sidecar_keep_ref (HEAP_ATTRVALUE *value, const RECDES *old_recdes);
/* the inline length of a kept value, so that the offset size fits the record expanded */
extern int sidecar_kept_ref_length (const HEAP_ATTRVALUE *value);
/* the UPDATE column writer, for a value in state HEAP_KEPT_REF_ATTRVALUE: copies the old record's ref */
extern SCAN_CODE sidecar_write_kept_ref (HEAP_CACHE_ATTRINFO *attr_info, int value_index,
    OR_BUF *buf, char **ptr_varvals, int offset_size, int header_size);
/* after the variable area of an image with kept refs: the trailer and the flag */
extern SCAN_CODE sidecar_write_trailer (HEAP_CACHE_ATTRINFO *attr_info, OR_BUF *buf, char **ptr_varvals);

/*
 * Reads: the exits of the heap fetch functions and the attribute layer.  Only a record with
 * OR_MVCC_FLAG_HAS_SIDECAR reaches them.
 */

/* which fetch exit read the record: it decides the reader class of 5.6 */
enum sidecar_fetch
{
  SIDECAR_FETCH_VISIBLE,	/* heap_get_visible_version_internal (): by the scan cache's snapshot */
  SIDECAR_FETCH_LAST_VERSION,	/* heap_get_last_version (): a row-lock holder */
  SIDECAR_FETCH_RECORD_INFO	/* heap_get_record_info (): under the hot latch, visible or not */
};
typedef enum sidecar_fetch SIDECAR_FETCH;

/* A fetched record with refs: expands it into an inline record unless a lock holder's scan cache opted in to keep
 * it as stored (heap_scancache_allow_lazy_sidecar ()).  Asking the caller to read the row's current version again
 * is a scan code of its own, so the exits hold no out-parameter (an address-taken local grows the frame of every
 * function the exit is inlined into).  Returns S_SUCCESS, S_DOESNT_FIT with recdes->length = -needed when the
 * caller's area is too small, S_DOESNT_EXIST when the row's delete committed and a store record is gone, S_ERROR,
 * and for a committed-snapshot fetch that found a store record absent, S_SIDECAR_RETRY while attempt is below the
 * bound and S_SIDECAR_LOCK_NEEDED after it (5.6 c). */
extern SCAN_CODE sidecar_on_fetch (THREAD_ENTRY *thread_p, HEAP_SCANCACHE *scan_cache, RECDES *recdes,
				   SIDECAR_FETCH fetch, int attempt);
/* the lock fallback of xlocator_fetch (): the row read again under an S lock as a lock holder, expanded into recdes */
extern SCAN_CODE sidecar_fetch_locked (THREAD_ENTRY *thread_p, const OID *oid, OID *class_oid,
				       HEAP_SCANCACHE *scan_cache, RECDES *recdes);
/* A variable attribute of a record with refs: true when its slot holds a ref and raw now points at the value, read
 * from the store as a row-lock holder; raw->length is -1 when that read failed, with the error set.  False leaves
 * the attribute to the ordinary pointer. */
extern bool sidecar_point_variable (const HEAP_REC_LAYOUT *layout, HEAP_CACHE_ATTRINFO *attr_info,
				    const OR_ATTRIBUTE *attrepr, RECDES *raw);
/* a bound variable attribute of a record with refs, for a key: the bytes to decode, disk_data unless its slot
 * holds a ref; NULL when the store read failed, with the error set */
extern char *sidecar_midxkey_point (HEAP_CACHE_ATTRINFO *attr_info, const RECDES *recdes,
				    const OR_ATTRIBUTE *att, char *disk_data);
/* heap_attrinfo_clear_dbvalues () and heap_attrinfo_end (): the value bytes the attribute layer read through refs */
extern void sidecar_attr_state_free (HEAP_CACHE_ATTRINFO *attr_info);

/* locator_attribute_info_force (): an UPDATE's old record is read as stored under its X lock and decoded until
 * locator_update_force () returns.  Debug builds assert that the attribute layer meets refs only inside. */
#if !defined (NDEBUG)
extern void sidecar_stored_form_begin (void);
extern void sidecar_stored_form_end (void);
#else /* NDEBUG */
#define sidecar_stored_form_begin() ((void) 0)
#define sidecar_stored_form_end() ((void) 0)
#endif /* NDEBUG */
/* before a scan fixes another hot page: releases the store page its cursor keeps, since nobody fixes a hot page
 * while holding a store page */
extern void sidecar_cursor_release (THREAD_ENTRY *thread_p, HEAP_SCANCACHE *scan_cache);

/*
 * Lifecycle and checks
 */

/* heap_drop_dependent_files () only: unlinks the store of a heap being dropped and postpones its destroy (I-10) */
/* heap_create_internal (): a new heap may have the VFID of a destroyed one, whose cache entry must go */
extern void sidecar_store_forget (THREAD_ENTRY *thread_p, const VFID *hot_vfid);
extern int sidecar_store_drop (THREAD_ENTRY *thread_p, const HFID *hfid);
/* checkdb (heap_check_heap_file ()): both directions of I-2 for a heap with a store, its class or partition */
extern DISK_ISVALID sidecar_verify_heap (THREAD_ENTRY *thread_p, const HFID *hfid);

/* A stored hot record has OR_MVCC_FLAG_HAS_SIDECAR exactly when it ends with a trailer, and a header that grows
 * to OR_MVCC_MAX_HEADER_SIZE never makes it REC_BIGONE.  Checked after heap_insert_physical (),
 * heap_update_physical () and every DELID write. */
#if !defined (NDEBUG)
extern void sidecar_assert_record_format (const RECDES *recdes);
#define SIDECAR_ASSERT_RECORD_FORMAT(recdes) sidecar_assert_record_format (recdes)
#else /* NDEBUG */
#define SIDECAR_ASSERT_RECORD_FORMAT(recdes) ((void) 0)
#endif /* NDEBUG */

/* locator_multi_insert_force (), once per call: the class-wide gates of the write policy (an MVCC user class, no
 * supplemental logging, no HA), so that a long record leaves the batch only when the policy could plan it */
extern bool sidecar_class_may_externalize (const OID *class_oid);

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

/* the UPDATE image needs sidecar_prepare_update () before the row is located */
STATIC_INLINE bool
sidecar_update_needs_prepare (const HEAP_OPERATION_CONTEXT *context)
{
  return sidecar_record_needs_policy (context->recdes_p);
}

/* The prepared plan, an old home record with refs, or a relocated row, whose forward record the plan hook reads
 * (heap_sidecar_plan ()).  A relocated row already costs a forward page fix; the hook takes it earlier. */
STATIC_INLINE bool
sidecar_update_needs_plan (const HEAP_OPERATION_CONTEXT *context)
{
  return (context->sc_plan != NULL || context->record_type == REC_RELOCATION
	  || (context->record_type == REC_HOME && sidecar_record_has_refs (&context->home_recdes)));
}

/* an old home record with refs, or a relocated row; the module's own stamps pass through */
STATIC_INLINE bool
sidecar_delete_needs_plan (const HEAP_OPERATION_CONTEXT *context)
{
  return (!context->sidecar_store_op
	  && (context->record_type == REC_RELOCATION
	      || (context->record_type == REC_HOME && sidecar_record_has_refs (&context->home_recdes))));
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
