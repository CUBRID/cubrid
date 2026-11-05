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
 * sidecar.cpp - the bodies behind the Sidecar hooks of the heap (sidecar.hpp)
 *
 * P1a: an INSERT moves values to the store under the AUTO policy, every fetch exit expands a record with refs into
 * an inline record, UPDATE and DELETE of such a record fail, and checkdb verifies both directions of the link.
 */

#include "config.h"

#include "sidecar.hpp"

#include "connection_defs.h"
#include "error_code.h"
#include "error_manager.h"
#include "fault_injection.h"
#include "file_manager.h"
#include "heap_file.h"
#include "lock_manager.h"
#include "log_impl.h"
#include "memory_alloc.h"
#include "mvcc.h"
#include "object_domain.h"
#include "object_representation.h"
#include "object_representation_sr.h"
#include "oid.h"
#include "overflow_file.h"
#include "page_buffer.h"
#include "slotted_page.h"
#include "system_parameter.h"
#include "thread_lockfree_hash_map.hpp"
#include "vacuum.h"

#include <map>
#include <utility>
#include <vector>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

int sidecar_Policy_min_length = 4060;

/*
 * The store of each hot heap, keyed by the hot VFID.  The hot header's link is the truth: an entry is filled from it
 * with no data page latched, refilled after a miss, and erased when the heap is dropped.
 */
struct sidecar_store_entry
{
  VFID hot_vfid;		/* key */
  HFID store_hfid;		/* NULL_HFID: the heap has no store */

  sidecar_store_entry *stack;	/* lock-free freelist */
  sidecar_store_entry *next;	/* lock-free hash chain */
  pthread_mutex_t mutex;
  UINT64 del_id;
};

static void *
sidecar_store_entry_alloc (void)
{
  sidecar_store_entry *entry = (sidecar_store_entry *) malloc (sizeof (sidecar_store_entry));

  if (entry != NULL)
    {
      pthread_mutex_init (&entry->mutex, NULL);
    }
  return entry;
}

static int
sidecar_store_entry_free (void *entry)
{
  pthread_mutex_destroy (& ((sidecar_store_entry *) entry)->mutex);
  free (entry);
  return NO_ERROR;
}

static int
sidecar_store_entry_init (void *entry)
{
  HFID_SET_NULL (& ((sidecar_store_entry *) entry)->store_hfid);
  return NO_ERROR;
}

static int
sidecar_store_entry_key_copy (void *src, void *dest)
{
  VFID_COPY ((VFID *) dest, (VFID *) src);
  return NO_ERROR;
}

static int
sidecar_store_entry_key_compare (void *key1, void *key2)
{
  return VFID_EQ ((VFID *) key1, (VFID *) key2) ? 0 : 1;
}

static unsigned int
sidecar_store_entry_key_hash (void *key, int htsize)
{
  const VFID *vfid = (const VFID *) key;

  return ((unsigned int) vfid->fileid * 31 + (unsigned int) vfid->volid) % (unsigned int) htsize;
}

static LF_ENTRY_DESCRIPTOR sidecar_Store_entry_descriptor =
{
  offsetof (sidecar_store_entry, stack),
  offsetof (sidecar_store_entry, next),
  offsetof (sidecar_store_entry, del_id),
  offsetof (sidecar_store_entry, hot_vfid),
  offsetof (sidecar_store_entry, mutex),
  LF_EM_USING_MUTEX,
  LF_ENTRY_DESCRIPTOR_MAX_ALLOC,
  sidecar_store_entry_alloc,
  sidecar_store_entry_free,
  sidecar_store_entry_init,
  NULL,
  sidecar_store_entry_key_copy,
  sidecar_store_entry_key_compare,
  sidecar_store_entry_key_hash,
  NULL
};

#define SIDECAR_STORE_HASH_SIZE 1024

using sidecar_store_map = cubthread::lockfree_hashmap<VFID, sidecar_store_entry>;

static sidecar_store_map sidecar_Stores;
static bool sidecar_Stores_initialized = false;

/*
 * The write plan of one INSERT: the values that move to the store, and the hot record the heap stores instead of
 * the caller's image.  context->sc_plan points at it from sidecar_plan_insert () to sidecar_op_end ().
 */
struct sidecar_plan
{
  RECDES *client_recdes;	/* the caller's inline image, which context->recdes_p points at again at the end */
  int n_variable;		/* of the image's representation */
  int n_slots;
  SIDECAR_SLOT *slots;		/* ascending locations; sidecar_apply_plan () fills the refs */
  int *attrids;			/* the attribute of each slot */
  RECDES hot;
};

int
sidecar_initialize (void)
{
  sidecar_refresh_parameters ();
  if (sidecar_Stores_initialized)
    {
      return NO_ERROR;
    }

  int error_code = sidecar_Stores.init_as_new (cubthread::get_thread_entry_lftransys (), SIDECAR_STORE_HASH_SIZE,
		   100, 100, sidecar_Store_entry_descriptor);
  if (error_code == NO_ERROR)
    {
      sidecar_Stores_initialized = true;
    }
  return error_code;
}

void
sidecar_finalize (void)
{
  if (sidecar_Stores_initialized)
    {
      sidecar_Stores.destroy ();
      sidecar_Stores_initialized = false;
    }
}

void
sidecar_refresh_parameters (void)
{
  sidecar_Policy_min_length = (prm_get_bool_value (PRM_ID_SIDECAR_FORCE_EXTERNAL)
			       ? SIDECAR_FORCE_MIN_VALUE_SIZE : prm_get_integer_value (PRM_ID_SIDECAR_INLINE_TARGET));
}

static SIDECAR_POLICY
sidecar_policy_now (void)
{
  SIDECAR_POLICY policy;
  int max_slotted_length;

  (void) xheap_get_maxslotted_reclength (max_slotted_length);
  policy.inline_target = prm_get_integer_value (PRM_ID_SIDECAR_INLINE_TARGET);
  policy.min_value_size = prm_get_integer_value (PRM_ID_SIDECAR_MIN_VALUE_SIZE);
  policy.force_external = prm_get_bool_value (PRM_ID_SIDECAR_FORCE_EXTERNAL);
  policy.headroom = max_slotted_length - OR_MVCC_MAX_HEADER_SIZE;
  return policy;
}

/* the cached store of a hot heap; false on a miss */
static bool
sidecar_cache_find (THREAD_ENTRY *thread_p, const VFID *hot_vfid, HFID *store_hfid)
{
  VFID key = *hot_vfid;
  sidecar_store_entry *entry = sidecar_Stores.find (thread_p, key);

  if (entry == NULL)
    {
      return false;
    }
  *store_hfid = entry->store_hfid;
  sidecar_Stores.unlock (thread_p, entry);
  return true;
}

static void
sidecar_cache_put (THREAD_ENTRY *thread_p, const VFID *hot_vfid, const HFID *store_hfid)
{
  VFID key = *hot_vfid;
  sidecar_store_entry *entry = NULL;

  (void) sidecar_Stores.find_or_insert (thread_p, key, entry);
  if (entry == NULL)
    {
      /* the header stays the truth, so a failed fill only costs the next lookup a header read */
      er_clear ();
      return;
    }
  entry->store_hfid = *store_hfid;
  sidecar_Stores.unlock (thread_p, entry);
}

/* the store of a hot heap, created on its first value; the caller holds no data page */
static int
sidecar_store_for (THREAD_ENTRY *thread_p, const HFID *hfid, const OID *class_oid, HFID *store_hfid)
{
  int error_code;

  /* No other transaction can use a class that this one holds SCH_M on, a class it creates or truncates among them.
   * Its store is created with the transaction, so that an abort, which may destroy the heap itself, destroys the
   * store and its link too.  Until the transaction ends the store is not cached, since the cache outlives an abort. */
  if (lock_has_lock_on_object (class_oid, oid_Root_class_oid, SCH_M_LOCK) == 1)
    {
      error_code = heap_sidecar_get_store (thread_p, hfid, store_hfid, NULL);
      if (error_code != NO_ERROR || !HFID_IS_NULL (store_hfid))
	{
	  return error_code;
	}
      return heap_sidecar_create_store (thread_p, hfid, class_oid, true, store_hfid);
    }

  if (sidecar_cache_find (thread_p, &hfid->vfid, store_hfid))
    {
      if (!HFID_IS_NULL (store_hfid))
	{
	  return NO_ERROR;
	}
    }
  else
    {
      error_code = heap_sidecar_get_store (thread_p, hfid, store_hfid, NULL);
      if (error_code != NO_ERROR)
	{
	  return error_code;
	}
      sidecar_cache_put (thread_p, &hfid->vfid, store_hfid);
      if (!HFID_IS_NULL (store_hfid))
	{
	  return NO_ERROR;
	}
    }

  error_code = heap_sidecar_create_store (thread_p, hfid, class_oid, false, store_hfid);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }
  sidecar_cache_put (thread_p, &hfid->vfid, store_hfid);
  return NO_ERROR;
}

static bool
sidecar_type_is_eligible (DB_TYPE type)
{
  return !TP_IS_SET_TYPE (type) && type != DB_TYPE_BLOB && type != DB_TYPE_CLOB && type != DB_TYPE_ELO;
}

static int
sidecar_set_bad_ref (const OID *class_oid)
{
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SIDECAR_BAD_REF, 3, OID_AS_ARGS (class_oid));
  return ER_SIDECAR_BAD_REF;
}

static int
sidecar_set_dangling (const SIDECAR_REF *ref, int attrid)
{
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SIDECAR_DANGLING_REF, 4, ref->volid, ref->pageid, ref->slotid, attrid);
  return ER_SIDECAR_DANGLING_REF;
}

__attribute__ ((noinline)) int
sidecar_plan_insert (THREAD_ENTRY *thread_p, HEAP_OPERATION_CONTEXT *context)
{
  const RECDES *image = context->recdes_p;
  SIDECAR_IMAGE_FACTS facts;

  assert (context->type == HEAP_OPERATION_INSERT && context->sc_plan == NULL);

  if (sidecar_record_has_refs (image))
    {
      /* I-3: refs come only from the module */
      return sidecar_set_bad_ref (&context->class_oid);
    }

  facts.length = image->length;
  facts.header_size = OR_HEADER_SIZE (image->data);
  facts.mvcc_user_class = (!mvcc_is_mvcc_disabled_class (&context->class_oid)
			   && !oid_is_system_class (&context->class_oid));
  facts.supplemental_log = prm_get_integer_value (PRM_ID_SUPPLEMENTAL_LOG) > 0;
  facts.ha_enabled = !HA_DISABLED ();
  facts.has_delid = (OR_GET_MVCC_FLAG (image->data) & OR_MVCC_FLAG_VALID_DELID) != 0;
  facts.page_held = pgbuf_has_perm_pages_fixed (thread_p);
  if (!sidecar_policy_admits (&facts))
    {
      return NO_ERROR;
    }

  int idx_incache = -1;
  OR_CLASSREP *rep = heap_classrepr_get (thread_p, &context->class_oid, NULL, OR_GET_MVCC_REPID (image->data),
					 &idx_incache);
  if (rep == NULL)
    {
      int error_code;

      ASSERT_ERROR_AND_SET (error_code);
      return error_code;
    }
  if (rep->n_variable == 0)
    {
      heap_classrepr_free_and_init (rep, &idx_incache);
      return NO_ERROR;
    }

  const SIDECAR_POLICY policy = sidecar_policy_now ();
  std::vector<SIDECAR_CANDIDATE> candidates;
  std::vector<int> chosen (rep->n_variable);

  for (int i = 0; i < rep->n_attributes; i++)
    {
      const OR_ATTRIBUTE *att = &rep->attributes[i];

      if (!att->is_fixed)
	{
	  const int length = sidecar_var_slot_length (image, att->location);

	  if (length > 0)
	    {
	      candidates.push_back ({ att->location, length, sidecar_type_is_eligible (att->type), false });
	    }
	}
    }

  const int n_chosen = sidecar_policy_choose (&policy, &facts, candidates.data (), (int) candidates.size (),
		       chosen.data ());
  if (n_chosen == 0)
    {
      heap_classrepr_free_and_init (rep, &idx_incache);
      return NO_ERROR;
    }

  const size_t size = sizeof (sidecar_plan) + n_chosen * (sizeof (SIDECAR_SLOT) + sizeof (int));
  sidecar_plan *plan = (sidecar_plan *) db_private_alloc (thread_p, size);
  if (plan == NULL)
    {
      heap_classrepr_free_and_init (rep, &idx_incache);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, size);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  plan->client_recdes = NULL;
  plan->n_variable = rep->n_variable;
  plan->n_slots = n_chosen;
  plan->slots = (SIDECAR_SLOT *) (plan + 1);
  plan->attrids = (int *) (plan->slots + n_chosen);
  plan->hot.data = NULL;
  plan->hot.area_size = plan->hot.length = 0;
  for (int k = 0; k < n_chosen; k++)
    {
      plan->slots[k].location = chosen[k];
      for (int i = 0; i < rep->n_attributes; i++)
	{
	  if (!rep->attributes[i].is_fixed && rep->attributes[i].location == chosen[k])
	    {
	      plan->attrids[k] = rep->attributes[i].id;
	      break;
	    }
	}
    }
  heap_classrepr_free_and_init (rep, &idx_incache);

  context->sc_plan = plan;
  return NO_ERROR;
}

/* I-1 and I-7: each value becomes a store record of its own, written through heap_insert_logical () with no data
 * page held, before the hot record */
static int
sidecar_insert_values (THREAD_ENTRY *thread_p, HEAP_OPERATION_CONTEXT *context, sidecar_plan *plan,
		       HFID *store_hfid)
{
  const RECDES *image = context->recdes_p;
  LOG_TDES *tdes = LOG_FIND_CURRENT_TDES (thread_p);
  const LOG_LSA saved_insert_lsa = tdes->repl_insert_lsa;
  const LOG_LSA saved_update_lsa = tdes->repl_update_lsa;
  OID class_oid = context->class_oid;
  int max_length = 0;
  int error_code = NO_ERROR;

  for (int k = 0; k < plan->n_slots; k++)
    {
      max_length = MAX (max_length, sidecar_var_slot_length (image, plan->slots[k].location));
    }

  /* room for the header the heap grows: an INSID, or the maximum header of a REC_BIGONE record */
  const int area_size = SIDECAR_STORE_RECORD_LENGTH (max_length) + OR_MVCC_MAX_HEADER_SIZE;
  char *area = (char *) db_private_alloc (thread_p, area_size);
  HEAP_OPERATION_CONTEXT *store_context =
	  (HEAP_OPERATION_CONTEXT *) db_private_alloc (thread_p, sizeof (HEAP_OPERATION_CONTEXT));
  if (area == NULL || store_context == NULL)
    {
      error_code = ER_OUT_OF_VIRTUAL_MEMORY;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_code, 1, (size_t) area_size);
      goto end;
    }

  for (int k = 0; k < plan->n_slots + (FI_INSERTED (FI_TEST_SIDECAR_ORPHAN_STORE_RECORD) ? 1 : 0); k++)
    {
      const int slot = k < plan->n_slots ? k : 0;
      const int location = plan->slots[slot].location;
      const int length = sidecar_var_slot_length (image, location);
      RECDES record;

      record.data = area;
      record.area_size = area_size;
      sidecar_store_record_build (plan->attrids[slot], image->data + OR_VAR_OFFSET (image->data, location), length,
				  &record);

      heap_create_insert_context (store_context, store_hfid, &class_oid, &record, NULL);
      store_context->sidecar_store_op = true;
      store_context->is_bulk_op = context->is_bulk_op;
      store_context->use_bulk_logging = context->use_bulk_logging;

      assert (!pgbuf_has_perm_pages_fixed (thread_p));
      error_code = heap_insert_logical (thread_p, store_context, NULL);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto end;
	}
      if (k >= plan->n_slots)
	{
	  /* the fault injection's orphan: no ref names it */
	  continue;
	}

      SIDECAR_REF *ref = &plan->slots[k].ref;
      ref->pageid = store_context->res_oid.pageid;
      ref->volid = store_context->res_oid.volid;
      ref->slotid = store_context->res_oid.slotid;
      ref->length = length;
      ref->flags = store_context->recdes_p->type == REC_BIGONE ? SIDECAR_REF_BIGONE : 0;
      ref->attrid_check = (UINT16) (plan->attrids[k] & 0xFFFF);
    }

  if (FI_INSERTED (FI_TEST_SIDECAR_DANGLING_REF))
    {
      plan->slots[0].ref.slotid = SHRT_MAX;
    }

end:
  tdes->repl_insert_lsa = saved_insert_lsa;
  tdes->repl_update_lsa = saved_update_lsa;
  if (area != NULL)
    {
      db_private_free (thread_p, area);
    }
  if (store_context != NULL)
    {
      db_private_free (thread_p, store_context);
    }
  return error_code;
}

__attribute__ ((noinline)) int
sidecar_apply_plan (THREAD_ENTRY *thread_p, HEAP_OPERATION_CONTEXT *context)
{
  sidecar_plan *plan = context->sc_plan;
  HFID store_hfid;
  int error_code;

  if (plan == NULL)
    {
      return NO_ERROR;
    }
  assert (context->type == HEAP_OPERATION_INSERT);

  error_code = sidecar_store_for (thread_p, &context->hfid, &context->class_oid, &store_hfid);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = sidecar_insert_values (thread_p, context, plan, &store_hfid);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  const int length = sidecar_hot_length (context->recdes_p, plan->n_variable, plan->n_slots, plan->slots);
  if (length < 0)
    {
      assert_release (false);
      return sidecar_set_bad_ref (&context->class_oid);
    }

  /* heap_insert_adjust_recdes_header () may grow the header in place */
  plan->hot.area_size = length + OR_MVCC_MAX_HEADER_SIZE;
  plan->hot.data = (char *) db_private_alloc (thread_p, plan->hot.area_size);
  if (plan->hot.data == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) plan->hot.area_size);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  if (sidecar_build_hot (context->recdes_p, plan->n_variable, plan->n_slots, plan->slots, &plan->hot) != NO_ERROR)
    {
      assert_release (false);
      return sidecar_set_bad_ref (&context->class_oid);
    }

  plan->client_recdes = context->recdes_p;
  context->recdes_p = &plan->hot;
  return NO_ERROR;
}

/* P1a has no UPDATE or DELETE plan, so neither may rewrite a row with refs: that would orphan its store records */
__attribute__ ((noinline)) int
sidecar_plan_update (THREAD_ENTRY *thread_p, HEAP_OPERATION_CONTEXT *context, const RECDES *old_stored)
{
  if (old_stored != NULL && sidecar_record_has_refs (old_stored))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SIDECAR_NOT_SUPPORTED, 1, "UPDATE");
      return ER_SIDECAR_NOT_SUPPORTED;
    }
  if (sidecar_record_has_refs (context->recdes_p))
    {
      return sidecar_set_bad_ref (&context->class_oid);
    }
  return NO_ERROR;
}

__attribute__ ((noinline)) int
sidecar_plan_delete (THREAD_ENTRY *thread_p, HEAP_OPERATION_CONTEXT *context, const RECDES *old_stored)
{
  assert (sidecar_record_has_refs (old_stored));
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SIDECAR_NOT_SUPPORTED, 1, "DELETE");
  return ER_SIDECAR_NOT_SUPPORTED;
}

__attribute__ ((noinline)) void
sidecar_op_end_plan (THREAD_ENTRY *thread_p, HEAP_OPERATION_CONTEXT *context, int rc)
{
  sidecar_plan *plan = context->sc_plan;

  if (plan->client_recdes != NULL)
    {
      context->recdes_p = plan->client_recdes;
    }
  if (plan->hot.data != NULL)
    {
      db_private_free (thread_p, plan->hot.data);
    }
  db_private_free (thread_p, plan);
  context->sc_plan = NULL;
}

/* nothing marks a value HEAP_KEPT_REF_ATTRVALUE while no record has refs */
__attribute__ ((noinline)) SCAN_CODE
sidecar_write_kept_ref (HEAP_CACHE_ATTRINFO *attr_info, int value_index, OR_BUF *buf, char **ptr_varvals,
			int offset_size, int header_size)
{
  assert (false);
  return S_ERROR;
}

/* the value bytes of a store record, when it is the record the ref names */
static const char *
sidecar_store_value (const RECDES *record, const SIDECAR_REF *ref)
{
  SIDECAR_VALUE_HEADER header;
  const char *value;
  int length;

  if (sidecar_store_record_parse (record, &header, &value, &length) != NO_ERROR
      || (UINT16) (header.attrid & 0xFFFF) != ref->attrid_check || length != ref->length)
    {
      return NULL;
    }
  return value;
}

/* the whole store record a REC_BIGONE home slot forwards to, read from the store's overflow file */
static int
sidecar_read_bigone (THREAD_ENTRY *thread_p, const VPID *ovf_vpid, std::vector<char> &bytes, RECDES *record)
{
  const int length = overflow_get_length (thread_p, ovf_vpid);

  if (length < 0)
    {
      return ER_FAILED;
    }
  bytes.resize (length);
  record->data = bytes.data ();
  record->area_size = length;
  if (overflow_get (thread_p, ovf_vpid, record, NULL) != S_SUCCESS)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}

/*
 * Copies the value a ref names to out.  Readers latch the hot page, then the store page with a plain fix, and hold
 * one store page at most (I-7): a REC_BIGONE home page is released before its overflow pages are read.  A store
 * record that is absent or holds another value is ER_SIDECAR_DANGLING_REF; without stamps nothing removes a store
 * record that a hot record names, so the retry rule of 5.6 (c) waits for P1b.
 */
static int
sidecar_read_value_into (THREAD_ENTRY *thread_p, const SIDECAR_REF *ref, int attrid, char *out)
{
  VPID vpid;
  RECDES record;
  PAGE_PTR page;

  vpid.volid = ref->volid;
  vpid.pageid = ref->pageid;
  page = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
  if (page == NULL)
    {
      if (er_errid () == ER_INTERRUPTED)
	{
	  return ER_INTERRUPTED;
	}
      er_clear ();
      return sidecar_set_dangling (ref, attrid);
    }

  const INT16 type = (pgbuf_get_page_ptype (thread_p, page) == PAGE_HEAP
		      ? spage_get_record_type (page, ref->slotid) : (INT16) REC_UNKNOWN);
  const bool bigone = (ref->flags & SIDECAR_REF_BIGONE) != 0;

  if (type == REC_HOME && !bigone && spage_get_record (thread_p, page, ref->slotid, &record, PEEK) == S_SUCCESS)
    {
      const char *value = sidecar_store_value (&record, ref);

      if (value != NULL)
	{
	  memcpy (out, value, ref->length);
	}
      pgbuf_unfix_and_init (thread_p, page);
      return value != NULL ? NO_ERROR : sidecar_set_dangling (ref, attrid);
    }
  if (type != REC_BIGONE || !bigone || spage_get_record (thread_p, page, ref->slotid, &record, PEEK) != S_SUCCESS)
    {
      pgbuf_unfix_and_init (thread_p, page);
      return sidecar_set_dangling (ref, attrid);
    }

  OID ovf_oid;
  VPID ovf_vpid;

  COPY_OID (&ovf_oid, (OID *) record.data);
  pgbuf_unfix_and_init (thread_p, page);
  ovf_vpid.volid = ovf_oid.volid;
  ovf_vpid.pageid = ovf_oid.pageid;

  std::vector<char> bytes;
  if (sidecar_read_bigone (thread_p, &ovf_vpid, bytes, &record) != NO_ERROR)
    {
      if (er_errid () == ER_INTERRUPTED)
	{
	  return ER_INTERRUPTED;
	}
      er_clear ();
      return sidecar_set_dangling (ref, attrid);
    }

  const char *value = sidecar_store_value (&record, ref);
  if (value == NULL)
    {
      return sidecar_set_dangling (ref, attrid);
    }
  memcpy (out, value, ref->length);
  return NO_ERROR;
}

/*
 * No reader opts in to lazy refs in P1a, so every fetch exit expands.  The expanded record goes where a REC_BIGONE
 * record would: into the scan cache area for PEEK or when the record is already there, else into the caller's
 * area, which returns S_DOESNT_FIT with -needed when it is too small.  The trailer says how many variable attributes
 * the record has, so the expansion needs no class representation and fixes no page but one store page at a time.
 */
__attribute__ ((noinline)) SCAN_CODE
sidecar_on_fetch (THREAD_ENTRY *thread_p, HEAP_SCANCACHE *scan_cache, RECDES *recdes)
{
  const OID *class_oid = scan_cache != NULL ? &scan_cache->node.class_oid : &oid_Null_oid;

  std::vector<char> hot_bytes (recdes->data, recdes->data + recdes->length);
  RECDES hot;

  hot.data = hot_bytes.data ();
  hot.area_size = hot.length = recdes->length;
  hot.type = recdes->type;

  const int needed = sidecar_expanded_length (&hot);
  if (needed < 0)
    {
      (void) sidecar_set_bad_ref (class_oid);
      return S_ERROR;
    }

  if (scan_cache != NULL && (recdes->area_size < 0 || scan_cache->is_recdes_assigned_to_area (*recdes)))
    {
      scan_cache->assign_recdes_to_area (*recdes, (size_t) needed);
    }
  else if (recdes->area_size < needed)
    {
      recdes->length = -needed;
      return S_DOESNT_FIT;
    }

  const int n_refs = sidecar_trailer_n_refs (&hot);
  std::vector<int> value_offsets (n_refs);
  if (sidecar_expand_frame (&hot, recdes, value_offsets.data ()) != NO_ERROR)
    {
      assert_release (false);
      (void) sidecar_set_bad_ref (class_oid);
      return S_ERROR;
    }

  for (int i = 0; i < n_refs; i++)
    {
      SIDECAR_REF ref;

      sidecar_hot_get_ref (&hot, i, &ref);
      if (sidecar_read_value_into (thread_p, &ref, ref.attrid_check, recdes->data + value_offsets[i]) != NO_ERROR)
	{
	  return S_ERROR;
	}
    }
  return S_SUCCESS;
}

/* no reader keeps refs lazy in P1a, so the attribute layer never meets a record with refs */
__attribute__ ((noinline)) bool
sidecar_point_variable (const HEAP_REC_LAYOUT *layout, HEAP_CACHE_ATTRINFO *attr_info,
			const OR_ATTRIBUTE *attrepr, RECDES *raw)
{
  assert (false);
  return false;
}

__attribute__ ((noinline)) char *
sidecar_midxkey_point (HEAP_CACHE_ATTRINFO *attr_info, const RECDES *recdes, const OR_ATTRIBUTE *att,
		       char *disk_data)
{
  assert (false);
  return disk_data;
}

__attribute__ ((noinline)) void
sidecar_cursor_release (THREAD_ENTRY *thread_p, HEAP_SCANCACHE *scan_cache)
{
}

void
sidecar_store_forget (THREAD_ENTRY *thread_p, const VFID *hot_vfid)
{
  VFID key = *hot_vfid;

  if (sidecar_Stores_initialized)
    {
      (void) sidecar_Stores.erase (thread_p, key);
    }
}

/*
 * The store goes with its hot heap: its link is cleared with a logged change, which an abort undoes, its files are
 * destroyed when the drop commits, vacuum skips its records, and the cache entry is erased (I-10).
 */
int
sidecar_store_drop (THREAD_ENTRY *thread_p, const HFID *hfid)
{
  HFID store_hfid;
  VFID ovf_vfid;
  int error_code = heap_sidecar_unlink_store (thread_p, hfid, &store_hfid);

  if (error_code != NO_ERROR)
    {
      return error_code;
    }
  sidecar_store_forget (thread_p, &hfid->vfid);
  if (HFID_IS_NULL (&store_hfid))
    {
      return NO_ERROR;
    }

  vacuum_log_add_dropped_file (thread_p, &store_hfid.vfid, NULL, VACUUM_LOG_ADD_DROPPED_FILE_POSTPONE);
  if (heap_ovf_find_vfid (thread_p, &store_hfid, &ovf_vfid, false, PGBUF_UNCONDITIONAL_LATCH) != NULL)
    {
      file_postpone_destroy (thread_p, &ovf_vfid);
    }
  file_postpone_destroy (thread_p, &store_hfid.vfid);
  return NO_ERROR;
}

/*
 * The verifier.  G13: a live store record (no DELID) must be named by exactly one current hot record, and every
 * ref of a current hot record must name a live store record of the same attribute and length.  The store is read
 * into a map first, so the hot scan never holds a store page.
 */
struct sidecar_verify_record
{
  int attrid;
  int length;			/* of the value */
  bool bigone;
  bool live;
  int named;
};

struct sidecar_oid_less
{
  bool operator() (const OID &a, const OID &b) const
  {
    return oid_compare (&a, &b) < 0;
  }
};

using sidecar_verify_records = std::map<OID, sidecar_verify_record, sidecar_oid_less>;

struct sidecar_verify
{
  const HFID *hfid;
  const HFID *store_hfid;
  OID class_oid;
  sidecar_verify_records records;
  bool valid;

  void invalid (const char *reason, const OID *oid)
  {
    char text[256];

    if (oid != NULL)
      {
	snprintf (text, sizeof (text), "%s (record %d|%d|%d)", reason, OID_AS_ARGS (oid));
	reason = text;
      }
    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SIDECAR_STORE_INVALID, 5, store_hfid->vfid.volid,
	    store_hfid->vfid.fileid, hfid->vfid.volid, hfid->vfid.fileid, reason);
    valid = false;
  }
};

/* visits every record of a heap's pages, header page included, one page fixed at a time; like every heap scan it
 * skips the pages that hold the heap's bestspace storage */
template <typename Visit>
static int
sidecar_verify_walk (THREAD_ENTRY *thread_p, const HFID *hfid, Visit &&visit)
{
  VPID vpid;

  vpid.volid = hfid->vfid.volid;
  vpid.pageid = hfid->hpgid;
  while (!VPID_ISNULL (&vpid))
    {
      PAGE_PTR page = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
      VPID next_vpid;

      if (page == NULL)
	{
	  ASSERT_ERROR ();
	  return er_errid ();
	}
      if (heap_vpid_next (thread_p, hfid, page, &next_vpid) != NO_ERROR)
	{
	  pgbuf_unfix_and_init (thread_p, page);
	  return ER_FAILED;
	}

      PGSLOTID slotid = NULL_SLOTID;
      RECDES record;
      const bool is_bestspace = heap_page_is_bestspace (thread_p, page);

      while (!is_bestspace && spage_next_record (page, &slotid, &record, PEEK) == S_SUCCESS)
	{
	  if (slotid == HEAP_HEADER_AND_CHAIN_SLOTID)
	    {
	      continue;
	    }

	  OID oid;

	  oid.volid = vpid.volid;
	  oid.pageid = vpid.pageid;
	  oid.slotid = slotid;
	  visit (oid, spage_get_record_type (page, slotid), record);
	}
      pgbuf_unfix_and_init (thread_p, page);
      vpid = next_vpid;
    }
  return NO_ERROR;
}

/* the record carries the DELID of a transaction; a REC_BIGONE header always has the field, MVCCID_NULL until then */
static bool
sidecar_record_is_deleted (const RECDES *record)
{
  MVCC_REC_HEADER header;
  RECDES copy = *record;

  return or_mvcc_get_header (&copy, &header) == NO_ERROR && MVCC_IS_HEADER_DELID_VALID (&header);
}

static void
sidecar_verify_add_store_record (sidecar_verify &v, const OID &oid, const RECDES &record, bool bigone)
{
  SIDECAR_VALUE_HEADER header;
  const char *value;
  int length;

  if (sidecar_store_record_parse (&record, &header, &value, &length) != NO_ERROR)
    {
      v.invalid ("a record of the store is not a store record", &oid);
      return;
    }
  v.records[oid] = { header.attrid, length, bigone, !sidecar_record_is_deleted (&record), 0 };
}

static int
sidecar_verify_scan_store (THREAD_ENTRY *thread_p, sidecar_verify &v)
{
  std::vector<std::pair<OID, OID>> bigones;

  int error_code = sidecar_verify_walk (thread_p, v.store_hfid,[&] (const OID & oid, INT16 type, const RECDES & record)
  {
    if (type == REC_HOME)
      {
	sidecar_verify_add_store_record (v, oid, record, false);
      }
    else if (type == REC_BIGONE)
      {
	bigones.push_back ({ oid, * (OID *) record.data});
      }
    else if (type != REC_UNKNOWN)
      {
	/* a store record is written once and, in P1a, never deleted, so it is never relocated */
	v.invalid ("the store holds a record of a type P1a never writes", &oid);
      }
  });
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  for (const auto &bigone : bigones)
    {
      std::vector<char> bytes;
      RECDES record;
      VPID ovf_vpid;

      ovf_vpid.volid = bigone.second.volid;
      ovf_vpid.pageid = bigone.second.pageid;
      if (sidecar_read_bigone (thread_p, &ovf_vpid, bytes, &record) != NO_ERROR)
	{
	  ASSERT_ERROR_AND_SET (error_code);
	  return error_code;
	}
      sidecar_verify_add_store_record (v, bigone.first, record, true);
    }
  return NO_ERROR;
}

/* what the verifier keeps of a hot record with refs, so it can look up representations with no page fixed */
struct sidecar_verify_hot
{
  OID oid;
  int repid;
  int n_variable;		/* from the trailer */
  bool deleted;
  std::vector<std::pair<int, SIDECAR_REF>> refs;	/* location and ref */
};

static int
sidecar_verify_scan_hot (THREAD_ENTRY *thread_p, sidecar_verify &v)
{
  std::vector<sidecar_verify_hot> hots;

  int error_code = sidecar_verify_walk (thread_p, v.hfid,[&] (const OID & oid, INT16 type, const RECDES & record)
  {
    if ((type != REC_HOME && type != REC_NEWHOME) || !sidecar_record_has_refs (&record))
      {
	return;
      }
    if (!sidecar_hot_is_well_formed (&record))
      {
	v.invalid ("a record of the heap has the Sidecar flag and no well-formed trailer", &oid);
	return;
      }

    sidecar_verify_hot hot;
    hot.oid = oid;
    hot.repid = OR_GET_MVCC_REPID (record.data);
    hot.n_variable = sidecar_trailer_n_variable (&record);
    hot.deleted = sidecar_record_is_deleted (&record);
    for (int i = 0; i < sidecar_trailer_n_refs (&record); i++)
      {
	SIDECAR_REF ref;

	sidecar_hot_get_ref (&record, i, &ref);
	hot.refs.push_back ({ sidecar_trailer_location (&record, i), ref });
      }
    hots.push_back (std::move (hot));
  });
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  /* the attribute at each variable location, by representation: read with no page fixed, as the class record or the
   * catalog may have to be */
  std::map<int, std::vector<int>> attrids_by_repid;

  for (const sidecar_verify_hot &hot : hots)
    {
      auto found = attrids_by_repid.find (hot.repid);
      if (found == attrids_by_repid.end ())
	{
	  int idx_incache = -1;
	  OR_CLASSREP *rep = heap_classrepr_get (thread_p, &v.class_oid, NULL, hot.repid, &idx_incache);
	  if (rep == NULL)
	    {
	      ASSERT_ERROR_AND_SET (error_code);
	      return error_code;
	    }
	  std::vector<int> attrids (rep->n_variable, NULL_ATTRID);
	  for (int i = 0; i < rep->n_attributes; i++)
	    {
	      if (!rep->attributes[i].is_fixed)
		{
		  attrids[rep->attributes[i].location] = rep->attributes[i].id;
		}
	    }
	  heap_classrepr_free_and_init (rep, &idx_incache);
	  found = attrids_by_repid.emplace (hot.repid, attrids).first;
	}

      const std::vector<int> &attrids = found->second;
      if (hot.n_variable != (int) attrids.size ())
	{
	  v.invalid ("the trailer's count of variable attributes is not its representation's", &hot.oid);
	  continue;
	}

      for (const auto &slot : hot.refs)
	{
	  const SIDECAR_REF &ref = slot.second;
	  const int attrid = attrids[slot.first];
	  OID store_oid;

	  store_oid.volid = ref.volid;
	  store_oid.pageid = ref.pageid;
	  store_oid.slotid = ref.slotid;

	  auto named = v.records.find (store_oid);
	  if (named == v.records.end () || named->second.attrid != attrid
	      || (UINT16) (attrid & 0xFFFF) != ref.attrid_check || named->second.length != ref.length
	      || named->second.bigone != ((ref.flags & SIDECAR_REF_BIGONE) != 0)
	      || (!named->second.live && !hot.deleted))
	    {
	      (void) sidecar_set_dangling (&ref, attrid);
	      v.valid = false;
	      continue;
	    }
	  named->second.named++;
	}
    }
  return NO_ERROR;
}

DISK_ISVALID
sidecar_verify_heap (THREAD_ENTRY *thread_p, const HFID *hfid)
{
  HFID store_hfid;
  VFID owner_vfid;
  sidecar_verify v;

  if (heap_sidecar_get_store (thread_p, hfid, &store_hfid, &v.class_oid) != NO_ERROR)
    {
      return DISK_ERROR;
    }
  if (HFID_IS_NULL (&store_hfid))
    {
      return DISK_VALID;
    }

  v.hfid = hfid;
  v.store_hfid = &store_hfid;
  v.valid = true;

  if (heap_sidecar_get_owner (thread_p, &store_hfid, &owner_vfid) != NO_ERROR)
    {
      return DISK_ERROR;
    }
  if (!VFID_EQ (&owner_vfid, &hfid->vfid))
    {
      v.invalid ("the store's header does not name the heap as its owner", NULL);
      return DISK_INVALID;
    }

  /* writers of the class add a store record before its hot record, so keep them out while the two scans run */
  if (lock_object (thread_p, &v.class_oid, oid_Root_class_oid, S_LOCK, LK_UNCOND_LOCK) != LK_GRANTED)
    {
      return DISK_ERROR;
    }

  int error_code = sidecar_verify_scan_store (thread_p, v);
  if (error_code == NO_ERROR)
    {
      error_code = sidecar_verify_scan_hot (thread_p, v);
    }
  lock_unlock_object (thread_p, &v.class_oid, oid_Root_class_oid, S_LOCK, true);
  if (error_code != NO_ERROR)
    {
      return DISK_ERROR;
    }

  for (const auto &entry : v.records)
    {
      const sidecar_verify_record &r = entry.second;

      if (r.live && r.named != 1)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SIDECAR_ORPHAN_RECORD, 5, entry.first.volid, entry.first.pageid,
		  entry.first.slotid, r.attrid, r.named);
	  v.valid = false;
	}
    }

  return v.valid ? DISK_VALID : DISK_INVALID;
}

#if !defined (NDEBUG)
void
sidecar_assert_record_format (const RECDES *recdes)
{
  if ((recdes->type != REC_HOME && recdes->type != REC_NEWHOME) || recdes->length < OR_MVCC_REP_SIZE
      || !sidecar_record_has_refs (recdes))
    {
      return;
    }

  assert (sidecar_hot_is_well_formed (recdes));
  assert (!heap_is_big_length (recdes->length - OR_HEADER_SIZE (recdes->data) + OR_MVCC_MAX_HEADER_SIZE));
}
#endif /* !NDEBUG */
