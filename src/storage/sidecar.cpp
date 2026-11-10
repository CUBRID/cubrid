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
 * an inline record, and checkdb verifies both directions of the link.
 * P1b1: a DELETE stamps the row's store records and an UPDATE keeps, compares, drops and moves out values, both with
 * no data page held while the store is written; an UPDATE reads the old record as stored, and the attribute layer
 * reads the values it decodes through their refs; a committed-snapshot fetch that finds a store record absent retries
 * and then reads the row under an S lock.
 * P1b2: the in-place UPDATEs of compactdb and domain changes compare values across representations by attribute id,
 * a row that moves to another partition takes its values inline, and debug builds check where every record with refs
 * that the attribute layer decodes came from and which pages its store reads find fixed.
 */

#include "config.h"

#include "sidecar.hpp"

#include "connection_defs.h"
#include "dbtype.h"
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
#include "perf_monitor.h"
#include "slotted_page.h"
#include "system_parameter.h"
#include "thread_lockfree_hash_map.hpp"
#include "vacuum.h"

#include <algorithm>
#include <atomic>
#include <climits>
#include <map>
#include <new>
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
 * A write plan: the store records an INSERT, UPDATE or DELETE writes, and the hot record the heap writes instead of
 * the caller's image.  context->sc_plan points at it from the first plan hook to sidecar_op_end ().
 *
 *   INSERT  sidecar_plan_insert () decides with no page held; sidecar_apply_plan () applies at once.
 *   UPDATE  sidecar_prepare_update () reads the image's representation with no page held, when the image may move
 *           values out; sidecar_plan_update () decides under the hot latch.  A plan that writes no store record is
 *           applied there and then.  Otherwise the heap releases its pages, sidecar_apply_plan () applies, and the heap
 *           locates the row again; the plan hook then finds the plan applied.
 *   DELETE  sidecar_plan_delete () decides under the hot latch; the heap releases its pages, sidecar_apply_plan ()
 *           stamps, and the heap locates the row again.
 *
 * An UPDATE whose image has another representation than the old version, from compactdb or a domain change, needs
 * the image's attribute ids to compare values.  When the prepare step did not read them, the plan asks for them:
 * the heap releases its pages as for a store write, sidecar_apply_plan () reads the representation, and the plan
 * hook decides on the next location step.
 *
 * The row's X lock keeps the row as the plan saw it while no latch is held.
 */
enum sidecar_plan_state
{
  SIDECAR_PLAN_PREPARED,
  SIDECAR_PLAN_NEEDS_LAYOUT,
  SIDECAR_PLAN_DECIDED,
  SIDECAR_PLAN_APPLIED
};

/* a ref of the hot record: a value that moves to the store in this operation (fresh), whose ref the apply fills, or
 * a store record that the replaced version names and the new one keeps */
struct sidecar_plan_slot
{
  SIDECAR_SLOT slot;
  int attrid;			/* of a fresh value */
  bool fresh;
};

struct sidecar_plan
{
  sidecar_plan_state state;
  RECDES *client_recdes;	/* the caller's image, which context->recdes_p points at again at the end */

  /* the image's representation, read with no page held; what a fresh value needs */
  bool admits;			/* the policy gates let values of this image move out */
  SIDECAR_IMAGE_FACTS facts;
  bool layout_read;		/* n_variable, attrids and eligible come from the image's representation */
  int n_variable;		/* of the hot record; -1 while unknown */
  std::vector<int> attrids;	/* by variable location */
  std::vector<char> eligible;	/* by variable location */

  std::vector<sidecar_plan_slot> slots;	/* every ref of the hot record, by ascending location */
  std::vector<SIDECAR_REF> stamps;	/* store records the new version no longer names (I-4) */
  bool inline_all;		/* the new version keeps no ref, because its hot record would pass the headroom (I-9) */
  bool rebuild;			/* the hot record is not the image as it is */

  std::vector<char> hot_bytes;
  RECDES hot;

  sidecar_plan ()
    : state (SIDECAR_PLAN_PREPARED), client_recdes (NULL), admits (false), facts (), layout_read (false),
      n_variable (-1), attrids (), eligible (), slots (), stamps (), inline_all (false), rebuild (false), hot_bytes (),
      hot ()
  {
  }

  bool has_fresh () const
  {
    for (const sidecar_plan_slot &s : slots)
      {
	if (s.fresh)
	  {
	    return true;
	  }
      }
    return false;
  }

  bool writes_store () const
  {
    return !stamps.empty () || has_fresh ();
  }
};

struct sidecar_oid_less
{
  bool operator() (const OID &a, const OID &b) const
  {
    return oid_compare (&a, &b) < 0;
  }
};

/* The attribute layer's view of records with refs (HEAP_CACHE_ATTRINFO.sc): per attribute, the bytes of the value it
 * read last through a ref and of the one before, with their store record's OID.  A DB_VALUE decoded from them points
 * into them, so a buffer is never read into twice: a second read of the same ref, from the same record or from the
 * UPDATE image that kept it, finds the bytes there.  A read of another ref for the same attribute is the next row of a
 * scan; the value before it stays one row longer, as a peeked value of develop stays valid while its page does.  So a
 * scan holds at most two values per attribute.  They are freed when the attribute information's values are, in
 * heap_attrinfo_clear_dbvalues () and heap_attrinfo_end (). */
struct sidecar_attr_value
{
  OID oid;
  std::vector<char> bytes;
};

struct sidecar_attr_values
{
  sidecar_attr_value last;
  sidecar_attr_value before;
};

struct sidecar_attr_state
{
  std::map<int, sidecar_attr_values> values;	/* by attribute id */
};

/* What the module does is counted in the performance monitor (Num_sidecar_*, cubrid statdump).  The global counters
 * always count, so the verifier's line and a stand-alone process can report them; a transaction's own counters count
 * only while someone watches, as for every other statistic. */
static void
sidecar_count (THREAD_ENTRY *thread_p, PERF_STAT_ID psid, UINT64 amount = 1)
{
  if (perfmon_is_perf_tracking ())
    {
      perfmon_add_stat (thread_p, psid, amount);
    }
  else
    {
      perfmon_add_stat_to_global (psid, amount);
    }
}

static unsigned long long
sidecar_counted (PERF_STAT_ID psid)
{
  if (!pstat_Global.initialized)
    {
      return 0;
    }
  return (unsigned long long) pstat_Global.global_stats[pstat_Metadata[psid].start_offset];
}

#if !defined (NDEBUG)
/* The stored-form window of the thread: from the old-record fetch of an UPDATE under its X lock to the end of
 * locator_update_force ().  The attribute layer meets a record with refs only inside it, and only one of the records
 * admitted below: the old record that fetch kept as stored (B6), or the UPDATE image the column writer gave kept
 * refs.  A record with refs from anywhere else fails the hooks' asserts even inside the window. */
static thread_local int sidecar_Stored_form_window = 0;

struct sidecar_stored_form_record
{
  const char *begin;
  const char *end;
};

/* An UPDATE admits two records.  Past this many, the oldest is overwritten.  The ranges are dropped when the outermost
 * UPDATE window closes. */
#define SIDECAR_STORED_FORM_RECORDS 16
static thread_local sidecar_stored_form_record sidecar_Stored_form_records[SIDECAR_STORED_FORM_RECORDS];
static thread_local int sidecar_Stored_form_admitted = 0;

static void
sidecar_stored_form_admit (const char *data, int length)
{
  sidecar_stored_form_record &r = sidecar_Stored_form_records[sidecar_Stored_form_admitted
				  % SIDECAR_STORED_FORM_RECORDS];

  if (sidecar_Stored_form_admitted > 0)
    {
      const sidecar_stored_form_record &l = sidecar_Stored_form_records[ (sidecar_Stored_form_admitted - 1)
					    % SIDECAR_STORED_FORM_RECORDS];

      if (l.begin <= data && data + length <= l.end)
	{
	  /* already admitted by the previous record */
	  return;
	}
    }
  r.begin = data;
  r.end = data + length;
  sidecar_Stored_form_admitted++;
}

/* What each open lazy scan of the thread keeps as stored: its latest record, batch page and batch copies (P1c).  An
 * outer scan's keep stays admitted however many rows an inner scan keeps meanwhile; the scan's end drops its own. */
enum sidecar_lazy_kind
{
  SIDECAR_LAZY_RECORD,
  SIDECAR_LAZY_PAGE,
  SIDECAR_LAZY_COPIES
};

struct sidecar_lazy_admitted
{
  const HEAP_SCANCACHE *scan;
  sidecar_lazy_kind kind;
  const char *begin;
  const char *end;
};

static thread_local std::vector<sidecar_lazy_admitted> sidecar_Lazy_admitted;

static void
sidecar_lazy_admit (const HEAP_SCANCACHE *scan_cache, sidecar_lazy_kind kind, const char *data, size_t length)
{
  for (sidecar_lazy_admitted &a : sidecar_Lazy_admitted)
    {
      if (a.scan == scan_cache && a.kind == kind)
	{
	  a.begin = data;
	  a.end = data + length;
	  return;
	}
    }
  sidecar_Lazy_admitted.push_back ({ scan_cache, kind, data, data + length });
}

static void
sidecar_lazy_admitted_drop (const HEAP_SCANCACHE *scan_cache)
{
  sidecar_Lazy_admitted.erase (std::remove_if (sidecar_Lazy_admitted.begin (), sidecar_Lazy_admitted.end (),
			       [scan_cache] (const sidecar_lazy_admitted &a)
  {
    return a.scan == scan_cache;
  }), sidecar_Lazy_admitted.end ());
}

/* inside one of the admitted records */
static bool
sidecar_stored_form_is_admitted (const char *p)
{
  const int n = MIN (sidecar_Stored_form_admitted, SIDECAR_STORED_FORM_RECORDS);

  for (int i = 0; i < n; i++)
    {
      if (sidecar_Stored_form_records[i].begin <= p && p < sidecar_Stored_form_records[i].end)
	{
	  return true;
	}
    }
  for (const sidecar_lazy_admitted &a : sidecar_Lazy_admitted)
    {
      if (a.begin <= p && p < a.end)
	{
	  return true;
	}
    }
  return false;
}
#endif /* !NDEBUG */

/* the retry bound of 5.6 (c): a committed-snapshot fetch reads the row's current version at most this many times
 * more before it asks its locator-level caller for the lock fallback (guess) */
#define SIDECAR_RETRY_BOUND 3

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
  /* a stand-alone utility such as compactdb -S leaves no server to ask */
  er_log_debug (ARG_FILE_LINE, "Sidecar counters at shutdown: store reads %llu (fetch expansion %llu) writes %llu",
		sidecar_counted (PSTAT_SC_STORE_READS), sidecar_counted (PSTAT_SC_EXPAND),
		sidecar_counted (PSTAT_SC_EXTERNALIZE) + sidecar_counted (PSTAT_SC_DROP));
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

__attribute__ ((noinline)) bool
sidecar_class_may_externalize (const OID *class_oid)
{
  SIDECAR_IMAGE_FACTS facts = SIDECAR_IMAGE_FACTS ();

  facts.mvcc_user_class = !mvcc_is_mvcc_disabled_class (class_oid) && !oid_is_system_class (class_oid);
  facts.supplemental_log = prm_get_integer_value (PRM_ID_SUPPLEMENTAL_LOG) > 0;
  facts.ha_enabled = !HA_DISABLED ();
  return sidecar_policy_admits (&facts);
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

static int
sidecar_set_store_invalid (const char *reason)
{
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SIDECAR_STORE_INVALID, 5, NULL_VOLID, NULL_FILEID, NULL_VOLID,
	  NULL_FILEID, reason);
  return ER_SIDECAR_STORE_INVALID;
}

/* the STL containers of this module throw on allocation failure; the entry points catch it here and return the
 * engine's error, since the heap frames above them hold page latches and have no unwinding cleanup */
static int
sidecar_out_of_memory (void)
{
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) 0);
  return ER_OUT_OF_VIRTUAL_MEMORY;
}

static bool
sidecar_ref_equal (const SIDECAR_REF *a, const SIDECAR_REF *b)
{
  return (a->pageid == b->pageid && a->volid == b->volid && a->slotid == b->slotid && a->length == b->length
	  && a->flags == b->flags && a->attrid_check == b->attrid_check);
}

/*
 * Reading the store.  Readers fix the hot page, then store pages with plain fixes (I-7).  A store record that is
 * REC_BIGONE is read with its home page held, as every reader and writer of an overflow record fixes the home page
 * first; vacuum and the undo of a stamp need that page exclusively before they free its overflow pages.  A store
 * record that a stamp relocated is read through its forward page after its home page is released, one store page at a
 * time, since a reader holding two store pages can close a cycle with store vacuum (12A).  Its home page is then read
 * once more: vacuum frees the forward slot in the same operation that makes the home slot REC_MARKDELETED, and the
 * undo of the stamp writes the record back home, so a home that still forwards to the same place shows that the
 * bytes read were the record's.  A home that changed but still holds the record is read again.
 */
enum sidecar_read_result
{
  SIDECAR_READ_PRESENT,
  SIDECAR_READ_ABSENT,		/* no record of the ref at its slot: vacuum removed it, or a bug */
  SIDECAR_READ_FAILED		/* an error is set */
};

/* how many times a reader follows a relocated store record that moves while it reads (guess) */
#define SIDECAR_REREAD_BOUND 8

static sidecar_read_result
sidecar_fix_store_page (THREAD_ENTRY *thread_p, const OID *oid, PAGE_PTR *page)
{
  VPID vpid;

  vpid.volid = oid->volid;
  vpid.pageid = oid->pageid;
  sidecar_count (thread_p, PSTAT_SC_STORE_PAGE_FIXES);
  *page = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
  if (*page == NULL)
    {
      if (er_errid () != ER_PB_BAD_PAGEID)
	{
	  ASSERT_ERROR ();
	  return SIDECAR_READ_FAILED;
	}
      /* a ref to a page the store never had */
      er_clear ();
      return SIDECAR_READ_ABSENT;
    }
  if (pgbuf_get_page_ptype (thread_p, *page) != PAGE_HEAP)
    {
      pgbuf_unfix_and_init (thread_p, *page);
      return SIDECAR_READ_ABSENT;
    }
  return SIDECAR_READ_PRESENT;
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

static void
sidecar_copy_record (const RECDES *peeked, std::vector<char> &bytes, RECDES *record)
{
  bytes.assign (peeked->data, peeked->data + peeked->length);
  record->data = bytes.data ();
  record->area_size = record->length = (int) bytes.size ();
  record->type = REC_HOME;
}

/* the store record at oid, into bytes; record points into them; page is the store page fixed at each moment */
static sidecar_read_result
sidecar_read_store_record_at (THREAD_ENTRY *thread_p, const OID *oid, std::vector<char> &bytes, RECDES *record,
			      PAGE_PTR &page)
{
  for (int round = 0; round < SIDECAR_REREAD_BOUND; round++)
    {
      RECDES peeked;
      sidecar_read_result result = sidecar_fix_store_page (thread_p, oid, &page);

      if (result != SIDECAR_READ_PRESENT)
	{
	  return result;
	}

      const INT16 type = spage_get_record_type (page, oid->slotid);
      if ((type != REC_HOME && type != REC_RELOCATION && type != REC_BIGONE)
	  || spage_get_record (thread_p, page, oid->slotid, &peeked, PEEK) != S_SUCCESS)
	{
	  pgbuf_unfix_and_init (thread_p, page);
	  return SIDECAR_READ_ABSENT;
	}
      if (type == REC_HOME)
	{
	  sidecar_copy_record (&peeked, bytes, record);
	  pgbuf_unfix_and_init (thread_p, page);
	  return SIDECAR_READ_PRESENT;
	}

      OID forward;
      COPY_OID (&forward, (OID *) peeked.data);

      if (type == REC_BIGONE)
	{
	  VPID ovf_vpid;

	  ovf_vpid.volid = forward.volid;
	  ovf_vpid.pageid = forward.pageid;
	  const int error_code = sidecar_read_bigone (thread_p, &ovf_vpid, bytes, record);
	  pgbuf_unfix_and_init (thread_p, page);
	  if (error_code != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	      return SIDECAR_READ_FAILED;
	    }
	  record->type = REC_HOME;
	  return SIDECAR_READ_PRESENT;
	}

      pgbuf_unfix_and_init (thread_p, page);
      result = sidecar_fix_store_page (thread_p, &forward, &page);
      if (result == SIDECAR_READ_FAILED)
	{
	  return result;
	}
      bool read = false;
      if (result == SIDECAR_READ_PRESENT)
	{
	  if (spage_get_record_type (page, forward.slotid) == REC_NEWHOME
	      && spage_get_record (thread_p, page, forward.slotid, &peeked, PEEK) == S_SUCCESS)
	    {
	      sidecar_copy_record (&peeked, bytes, record);
	      read = true;
	    }
	  pgbuf_unfix_and_init (thread_p, page);
	}

      /* the home still forwards to what was read */
      result = sidecar_fix_store_page (thread_p, oid, &page);
      if (result != SIDECAR_READ_PRESENT)
	{
	  return result;
	}
      const bool same = (read && spage_get_record_type (page, oid->slotid) == REC_RELOCATION
			 && spage_get_record (thread_p, page, oid->slotid, &peeked, PEEK) == S_SUCCESS
			 && OID_EQ ((OID *) peeked.data, &forward));
      pgbuf_unfix_and_init (thread_p, page);
      if (same)
	{
	  return SIDECAR_READ_PRESENT;
	}
    }
  return SIDECAR_READ_ABSENT;
}

/* the copies into bytes may throw while a store page is fixed; the page is released before the exception goes on to
 * the entry point's catch */
static sidecar_read_result
sidecar_read_store_record (THREAD_ENTRY *thread_p, const OID *oid, std::vector<char> &bytes, RECDES *record)
{
  PAGE_PTR page = NULL;

  /* one store page at a time (I-7) */
  sidecar_cursor_release (thread_p);
  sidecar_count (thread_p, PSTAT_SC_STORE_READS);
  try
    {
      return sidecar_read_store_record_at (thread_p, oid, bytes, record, page);
    }
  catch (const std::bad_alloc &)
    {
      if (page != NULL)
	{
	  pgbuf_unfix_and_init (thread_p, page);
	}
      throw;
    }
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

/* the value a ref names, into out (ref->length bytes) */
static sidecar_read_result
sidecar_read_value (THREAD_ENTRY *thread_p, const SIDECAR_REF *ref, char *out)
{
  OID oid;
  std::vector<char> bytes;
  RECDES record;

  oid.volid = ref->volid;
  oid.pageid = ref->pageid;
  oid.slotid = ref->slotid;

  const sidecar_read_result result = sidecar_read_store_record (thread_p, &oid, bytes, &record);
  if (result != SIDECAR_READ_PRESENT)
    {
      return result;
    }

  const char *value = sidecar_store_value (&record, ref);
  if (value == NULL)
    {
      return SIDECAR_READ_ABSENT;
    }
  memcpy (out, value, ref->length);
  return SIDECAR_READ_PRESENT;
}

/* a value read by a row-lock holder: every stamp of the row's store records needs the row's X lock (I-4), so an
 * absent record is a bug */
static int
sidecar_read_value_locked (THREAD_ENTRY *thread_p, const SIDECAR_REF *ref, char *out)
{
  switch (sidecar_read_value (thread_p, ref, out))
    {
    case SIDECAR_READ_PRESENT:
      return NO_ERROR;
    case SIDECAR_READ_ABSENT:
      return sidecar_set_dangling (ref, ref->attrid_check);
    default:
    {
      int error_code;

      ASSERT_ERROR_AND_SET (error_code);
      return error_code;
    }
    }
}

/* the inline record of a hot record, every value read by a row-lock holder */
static int
sidecar_expand_locked (THREAD_ENTRY *thread_p, const RECDES *hot, std::vector<char> &bytes, RECDES *out)
{
  const int length = sidecar_expanded_length (hot);

  if (length < 0)
    {
      assert_release (false);
      return sidecar_set_store_invalid ("a hot record with refs does not expand");
    }

  const int n_refs = sidecar_trailer_n_refs (hot);
  std::vector<int> value_offsets (n_refs);

  bytes.resize (length + OR_MVCC_MAX_HEADER_SIZE);
  out->data = bytes.data ();
  out->area_size = (int) bytes.size ();
  if (sidecar_expand_frame (hot, out, value_offsets.data ()) != NO_ERROR)
    {
      assert_release (false);
      return sidecar_set_store_invalid ("a hot record with refs does not expand");
    }
  for (int i = 0; i < n_refs; i++)
    {
      SIDECAR_REF ref;

      sidecar_hot_get_ref (hot, i, &ref);
      const int error_code = sidecar_read_value_locked (thread_p, &ref, out->data + value_offsets[i]);
      if (error_code != NO_ERROR)
	{
	  return error_code;
	}
    }
  return NO_ERROR;
}

/*
 * Plans
 */

/* the policy facts of an image, and with them, when the gates admit it or need_layout asks for it, the
 * representation facts a fresh value or the compare by attribute id needs; no page is held, as the class
 * representation may have to be read */
static int
sidecar_plan_read_facts (THREAD_ENTRY *thread_p, const HEAP_OPERATION_CONTEXT *context, sidecar_plan *plan,
			 bool need_layout)
{
  const RECDES *image = context->recdes_p;
  SIDECAR_IMAGE_FACTS &facts = plan->facts;

  facts.length = image->length;
  facts.header_size = OR_HEADER_SIZE (image->data);
  facts.mvcc_user_class = (!mvcc_is_mvcc_disabled_class (&context->class_oid)
			   && !oid_is_system_class (&context->class_oid));
  facts.supplemental_log = prm_get_integer_value (PRM_ID_SUPPLEMENTAL_LOG) > 0;
  facts.ha_enabled = !HA_DISABLED ();
  facts.has_delid = (OR_GET_MVCC_FLAG (image->data) & OR_MVCC_FLAG_VALID_DELID) != 0;
  facts.page_held = pgbuf_has_perm_pages_fixed (thread_p);
  plan->admits = sidecar_policy_admits (&facts);
  if (!plan->admits && ! (need_layout && facts.mvcc_user_class))
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

  plan->n_variable = rep->n_variable;
  plan->attrids.assign (rep->n_variable, NULL_ATTRID);
  plan->eligible.assign (rep->n_variable, 0);
  for (int i = 0; i < rep->n_attributes; i++)
    {
      const OR_ATTRIBUTE *att = &rep->attributes[i];

      if (!att->is_fixed && att->location < rep->n_variable)
	{
	  plan->attrids[att->location] = att->id;
	  plan->eligible[att->location] = sidecar_type_is_eligible (att->type) ? 1 : 0;
	}
    }
  heap_classrepr_free_and_init (rep, &idx_incache);
  plan->admits = plan->admits && plan->n_variable > 0;
  plan->layout_read = true;
  return NO_ERROR;
}

/* the image's variable location of the eligible attribute whose id ends in attrid_check, or -1 */
static int
sidecar_plan_attr_location (const sidecar_plan *plan, UINT16 attrid_check)
{
  for (int location = 0; location < (int) plan->attrids.size (); location++)
    {
      if (plan->attrids[location] != NULL_ATTRID && plan->eligible[location] != 0
	  && (UINT16) (plan->attrids[location] & 0xFFFF) == attrid_check)
	{
	  return location;
	}
    }
  return -1;
}

static bool
sidecar_plan_has_slot (const sidecar_plan *plan, int location)
{
  for (const sidecar_plan_slot &s : plan->slots)
    {
      if (s.slot.location == location)
	{
	  return true;
	}
    }
  return false;
}

/* the AUTO policy over the inline values of the image that are not refs of the plan yet; the chosen ones become
 * fresh slots */
static void
sidecar_plan_choose_fresh (sidecar_plan *plan, const RECDES *image, int hot_length)
{
  const SIDECAR_POLICY policy = sidecar_policy_now ();
  std::vector<SIDECAR_CANDIDATE> candidates;

  for (int location = 0; location < plan->n_variable; location++)
    {
      if (sidecar_plan_has_slot (plan, location))
	{
	  continue;
	}

      const int length = sidecar_var_slot_length (image, location);
      if (length > 0)
	{
	  candidates.push_back ({ location, length, plan->eligible[location] != 0, false });
	}
    }
  if (candidates.empty ())
    {
      return;
    }

  SIDECAR_IMAGE_FACTS facts = plan->facts;
  std::vector<int> chosen (candidates.size ());

  facts.length = hot_length;
  const int n_chosen = sidecar_policy_choose (&policy, &facts, candidates.data (), (int) candidates.size (),
		       chosen.data ());
  for (int k = 0; k < n_chosen; k++)
    {
      sidecar_plan_slot s;

      s.slot.location = chosen[k];
      memset (&s.slot.ref, 0, sizeof (s.slot.ref));
      s.attrid = plan->attrids[chosen[k]];
      s.fresh = true;
      plan->slots.push_back (s);
    }
  if (n_chosen > 0)
    {
      plan->rebuild = true;
    }
}

static void
sidecar_plan_sort_slots (sidecar_plan *plan)
{
  std::sort (plan->slots.begin (), plan->slots.end (),[] (const sidecar_plan_slot & a, const sidecar_plan_slot & b)
  {
    return a.slot.location < b.slot.location;
  });
}

/* the length of the hot record the plan's slots make of image, or SIDECAR_FORMAT_ERROR */
static int
sidecar_plan_hot_length (const sidecar_plan *plan, const RECDES *image)
{
  std::vector<SIDECAR_SLOT> slots;

  for (const sidecar_plan_slot &s : plan->slots)
    {
      slots.push_back (s.slot);
    }
  return sidecar_hot_length (image, plan->n_variable, (int) slots.size (), slots.data ());
}

/* builds plan->hot from image with the plan's slots; the area leaves room for the header the heap grows */
static int
sidecar_plan_build_hot (sidecar_plan *plan, const RECDES *image, const OID *class_oid)
{
  std::vector<SIDECAR_SLOT> slots;

  for (const sidecar_plan_slot &s : plan->slots)
    {
      slots.push_back (s.slot);
    }

  const int length = sidecar_hot_length (image, plan->n_variable, (int) slots.size (), slots.data ());
  if (length < 0)
    {
      assert_release (false);
      return sidecar_set_bad_ref (class_oid);
    }
  plan->hot_bytes.resize (length + OR_MVCC_MAX_HEADER_SIZE);
  plan->hot.data = plan->hot_bytes.data ();
  plan->hot.area_size = (int) plan->hot_bytes.size ();
  if (sidecar_build_hot (image, plan->n_variable, (int) slots.size (), slots.data (), &plan->hot) != NO_ERROR)
    {
      assert_release (false);
      return sidecar_set_bad_ref (class_oid);
    }
  return NO_ERROR;
}

/* the heap writes plan->hot instead of the caller's image from here on */
static void
sidecar_plan_use_hot (HEAP_OPERATION_CONTEXT *context, sidecar_plan *plan)
{
  if (plan->client_recdes == NULL)
    {
      plan->client_recdes = context->recdes_p;
    }
  context->recdes_p = &plan->hot;
}

static int
sidecar_plan_insert_body (THREAD_ENTRY *thread_p, HEAP_OPERATION_CONTEXT *context)
{
  const RECDES *image = context->recdes_p;

  assert (context->type == HEAP_OPERATION_INSERT && context->sc_plan == NULL);

  if (sidecar_record_has_refs (image))
    {
      /* I-3: refs come only from the module */
      return sidecar_set_bad_ref (&context->class_oid);
    }

  sidecar_plan *plan = new sidecar_plan ();
  int error_code = sidecar_plan_read_facts (thread_p, context, plan, false);
  if (error_code != NO_ERROR || !plan->admits)
    {
      delete plan;
      return error_code;
    }

  sidecar_plan_choose_fresh (plan, image, image->length);
  if (plan->slots.empty ())
    {
      delete plan;
      return NO_ERROR;
    }

  plan->state = SIDECAR_PLAN_DECIDED;
  context->sc_plan = plan;
  return NO_ERROR;
}

__attribute__ ((noinline)) int
sidecar_plan_insert (THREAD_ENTRY *thread_p, HEAP_OPERATION_CONTEXT *context)
{
  /* the module's own store reads and writes, and the policy's page test, find no store page the cursor kept */
  sidecar_cursor_release (thread_p);
  try
    {
      return sidecar_plan_insert_body (thread_p, context);
    }
  catch (const std::bad_alloc &)
    {
      return sidecar_out_of_memory ();
    }
}

static int
sidecar_prepare_update_body (THREAD_ENTRY *thread_p, HEAP_OPERATION_CONTEXT *context)
{
  assert (context->type == HEAP_OPERATION_UPDATE && context->sc_plan == NULL);
  assert (!OID_ISNULL (&context->class_oid));

  sidecar_plan *plan = new sidecar_plan ();
  const int error_code = sidecar_plan_read_facts (thread_p, context, plan, false);

  if (error_code != NO_ERROR || (!plan->admits && !sidecar_record_has_refs (context->recdes_p)))
    {
      /* the plan hook decides from the old version alone */
      delete plan;
      return error_code;
    }
  context->sc_plan = plan;
  return NO_ERROR;
}

__attribute__ ((noinline)) int
sidecar_prepare_update (THREAD_ENTRY *thread_p, HEAP_OPERATION_CONTEXT *context)
{
  /* the module's own store reads and writes, and the policy's page test, find no store page the cursor kept */
  sidecar_cursor_release (thread_p);
  try
    {
      return sidecar_prepare_update_body (thread_p, context);
    }
  catch (const std::bad_alloc &)
    {
      return sidecar_out_of_memory ();
    }
}

/* the old version's refs, with whether the new version keeps them */
struct sidecar_old_ref
{
  int location;
  SIDECAR_REF ref;
  bool kept;
};

/*
 * The equal-length compare: an inline value of the image in a slot where the old version held a ref of the same
 * length is read from the store and compared; equal bytes keep the ref, so an image with no kept-ref information
 * (a client flush) rewrites only the values that changed.  Under the hot latch, one store page at a time (I-7).
 */
static int
sidecar_plan_compare (THREAD_ENTRY *thread_p, const RECDES *image, int location, const SIDECAR_REF *ref, bool *equal)
{
  std::vector<char> value (ref->length);
  const int error_code = sidecar_read_value_locked (thread_p, ref, value.data ());

  if (error_code != NO_ERROR)
    {
      return error_code;
    }
  *equal = memcmp (image->data + OR_VAR_OFFSET (image->data, location), value.data (), ref->length) == 0;
  return NO_ERROR;
}

static int
sidecar_plan_update_body (THREAD_ENTRY *thread_p, HEAP_OPERATION_CONTEXT *context, const RECDES *old_stored)
{
  sidecar_plan *plan = context->sc_plan;
  const RECDES *image = context->recdes_p;

  if (plan != NULL && plan->state == SIDECAR_PLAN_APPLIED)
    {
      /* the location step after the store writes */
      return NO_ERROR;
    }

  const bool old_has_refs = old_stored != NULL && sidecar_record_has_refs (old_stored);
  const bool image_has_refs = sidecar_record_has_refs (image);

  if (!old_has_refs)
    {
      if (image_has_refs)
	{
	  /* I-3: a kept ref must be one of the current version's */
	  return sidecar_set_bad_ref (&context->class_oid);
	}
      if (plan == NULL || !plan->admits)
	{
	  return NO_ERROR;
	}
    }
  else if ((OR_GET_MVCC_FLAG (old_stored->data) & OR_MVCC_FLAG_VALID_DELID) != 0)
    {
      /* A version whose delete is stamped, which only a domain change rewrites (UPDATE_INPLACE_OLD_MVCCID keeps the
       * DELID): its deleter stamped every store record it names (I-4).  Stamping them again would replace that DELID
       * with one that a rollback clears, leaving records no version names.  The image goes in inline, and the store
       * records wait for vacuum. */
      if (image_has_refs)
	{
	  return sidecar_set_bad_ref (&context->class_oid);
	}
      if (plan != NULL)
	{
	  plan->state = SIDECAR_PLAN_APPLIED;
	}
      return NO_ERROR;
    }

  if (plan == NULL)
    {
      plan = new sidecar_plan ();
      context->sc_plan = plan;
    }

  const bool same_repid = old_stored != NULL && OR_GET_MVCC_REPID (image->data) == OR_GET_MVCC_REPID (old_stored->data);
  if (old_has_refs && !same_repid && !plan->layout_read)
    {
      /* the compare by attribute id needs the image's representation, which is read with no page held */
      plan->state = SIDECAR_PLAN_NEEDS_LAYOUT;
      return NO_ERROR;
    }
  plan->state = SIDECAR_PLAN_DECIDED;

  std::vector<sidecar_old_ref> olds;
  if (old_has_refs)
    {
      if (!sidecar_hot_is_well_formed (old_stored))
	{
	  assert_release (false);
	  return sidecar_set_store_invalid ("the replaced version has the Sidecar flag and no well-formed trailer");
	}
      for (int i = 0; i < sidecar_trailer_n_refs (old_stored); i++)
	{
	  sidecar_old_ref o;

	  o.location = sidecar_trailer_location (old_stored, i);
	  sidecar_hot_get_ref (old_stored, i, &o.ref);
	  o.kept = false;
	  olds.push_back (o);
	}
    }

  if (image_has_refs)
    {
      if (!sidecar_hot_is_well_formed (image))
	{
	  return sidecar_set_bad_ref (&context->class_oid);
	}
      assert (plan->n_variable < 0 || plan->n_variable == sidecar_trailer_n_variable (image));
      plan->n_variable = sidecar_trailer_n_variable (image);
    }
  else if (plan->n_variable < 0 && old_has_refs && same_repid)
    {
      plan->n_variable = sidecar_trailer_n_variable (old_stored);
    }

  /* each kept ref is a ref of the current version, and no two slots keep the same one (I-3, I-2) */
  for (int i = 0; image_has_refs && i < sidecar_trailer_n_refs (image); i++)
    {
      sidecar_plan_slot s;
      bool found = false;

      s.slot.location = sidecar_trailer_location (image, i);
      sidecar_hot_get_ref (image, i, &s.slot.ref);
      s.attrid = NULL_ATTRID;
      s.fresh = false;
      for (sidecar_old_ref &o : olds)
	{
	  if (!o.kept && sidecar_ref_equal (&o.ref, &s.slot.ref))
	    {
	      o.kept = found = true;
	      break;
	    }
	}
      if (!found || (!plan->attrids.empty ()
		     && (UINT16) (plan->attrids[s.slot.location] & 0xFFFF) != s.slot.ref.attrid_check))
	{
	  return sidecar_set_bad_ref (&context->class_oid);
	}
      plan->slots.push_back (s);
    }

  /* An image of the old version's representation names an attribute by the same location.  An image of another one,
   * from compactdb or a domain change, is matched by attribute id, which the image's representation gives for the
   * image and the ref's tripwire for the old value. */
  const bool by_attrid = !same_repid && plan->layout_read;
  if ((same_repid || by_attrid) && plan->n_variable > 0)
    {
      for (sidecar_old_ref &o : olds)
	{
	  const int location = same_repid ? o.location : sidecar_plan_attr_location (plan, o.ref.attrid_check);

	  if (o.kept || location < 0 || location >= plan->n_variable || sidecar_plan_has_slot (plan, location)
	      || (image_has_refs && sidecar_trailer_find (image, location) >= 0)
	      || sidecar_var_slot_length (image, location) != o.ref.length)
	    {
	      continue;
	    }

	  bool equal = false;
	  const int error_code = sidecar_plan_compare (thread_p, image, location, &o.ref, &equal);
	  if (error_code != NO_ERROR)
	    {
	      return error_code;
	    }
	  if (equal)
	    {
	      sidecar_plan_slot s;

	      sidecar_count (thread_p, PSTAT_SC_COMPARE_KEPT);

	      s.slot.location = location;
	      s.slot.ref = o.ref;
	      s.attrid = NULL_ATTRID;
	      s.fresh = false;
	      plan->slots.push_back (s);
	      plan->rebuild = true;
	      o.kept = true;
	    }
	}
    }
  sidecar_plan_sort_slots (plan);

  if (plan->admits)
    {
      int hot_length = image->length;

      if (!plan->slots.empty ())
	{
	  hot_length = sidecar_plan_hot_length (plan, image);
	  if (hot_length < 0)
	    {
	      assert_release (false);
	      return sidecar_set_bad_ref (&context->class_oid);
	    }
	  hot_length -= SIDECAR_TRAILER_SIZE ((int) plan->slots.size ());
	}
      sidecar_plan_choose_fresh (plan, image, hot_length);
      sidecar_plan_sort_slots (plan);
    }

  /* I-9: a hot record with refs never passes the headroom; above it the row keeps no ref and takes the stock path */
  if (!plan->slots.empty ())
    {
      const int hot_length = sidecar_plan_hot_length (plan, image);
      if (hot_length < 0)
	{
	  assert_release (false);
	  return sidecar_set_bad_ref (&context->class_oid);
	}
      if (hot_length - OR_HEADER_SIZE (image->data) > sidecar_policy_now ().headroom)
	{
	  plan->slots.clear ();
	  plan->inline_all = image_has_refs;
	  plan->rebuild = false;
	  for (sidecar_old_ref &o : olds)
	    {
	      o.kept = false;
	    }
	}
      else if (!image_has_refs)
	{
	  plan->rebuild = true;
	}
    }

  for (const sidecar_old_ref &o : olds)
    {
      if (!o.kept)
	{
	  plan->stamps.push_back (o.ref);
	}
    }

  if (!plan->writes_store () && !plan->inline_all)
    {
      /* a small-column UPDATE, or an image whose values all compared equal: the hot latch stays held */
      if (plan->rebuild)
	{
	  const int error_code = sidecar_plan_build_hot (plan, image, &context->class_oid);
	  if (error_code != NO_ERROR)
	    {
	      return error_code;
	    }
	  sidecar_plan_use_hot (context, plan);
	}
      plan->state = SIDECAR_PLAN_APPLIED;
    }
  return NO_ERROR;
}

__attribute__ ((noinline)) int
sidecar_plan_update (THREAD_ENTRY *thread_p, HEAP_OPERATION_CONTEXT *context, const RECDES *old_stored)
{
  try
    {
      return sidecar_plan_update_body (thread_p, context, old_stored);
    }
  catch (const std::bad_alloc &)
    {
      return sidecar_out_of_memory ();
    }
}

static int
sidecar_plan_delete_body (THREAD_ENTRY *thread_p, HEAP_OPERATION_CONTEXT *context, const RECDES *old_stored)
{
  sidecar_plan *plan = context->sc_plan;

  if (plan != NULL && plan->state == SIDECAR_PLAN_APPLIED)
    {
      return NO_ERROR;
    }
  assert (plan == NULL && sidecar_record_has_refs (old_stored));
  if (!sidecar_hot_is_well_formed (old_stored))
    {
      assert_release (false);
      return sidecar_set_store_invalid ("the deleted version has the Sidecar flag and no well-formed trailer");
    }

  plan = new sidecar_plan ();
  plan->state = SIDECAR_PLAN_DECIDED;
  for (int i = 0; i < sidecar_trailer_n_refs (old_stored); i++)
    {
      SIDECAR_REF ref;

      sidecar_hot_get_ref (old_stored, i, &ref);
      plan->stamps.push_back (ref);
    }
  context->sc_plan = plan;
  return NO_ERROR;
}

__attribute__ ((noinline)) int
sidecar_plan_delete (THREAD_ENTRY *thread_p, HEAP_OPERATION_CONTEXT *context, const RECDES *old_stored)
{
  try
    {
      return sidecar_plan_delete_body (thread_p, context, old_stored);
    }
  catch (const std::bad_alloc &)
    {
      return sidecar_out_of_memory ();
    }
}

__attribute__ ((noinline)) bool
sidecar_plan_writes_store (const HEAP_OPERATION_CONTEXT *context)
{
  const sidecar_plan *plan = context->sc_plan;

  return plan != NULL && ((plan->state == SIDECAR_PLAN_DECIDED && (plan->writes_store () || plan->inline_all))
			  || plan->state == SIDECAR_PLAN_NEEDS_LAYOUT);
}

/* I-1 and I-7: each fresh value becomes a store record of its own, written through heap_insert_logical () with no
 * data page held, before the hot record */
static int
sidecar_insert_values (THREAD_ENTRY *thread_p, HEAP_OPERATION_CONTEXT *context, sidecar_plan *plan,
		       HFID *store_hfid, HEAP_OPERATION_CONTEXT *store_context)
{
  const RECDES *image = context->recdes_p;
  OID class_oid = context->class_oid;
  std::vector<char> area;
  bool orphan = FI_INSERTED (FI_TEST_SIDECAR_ORPHAN_STORE_RECORD) && context->type == HEAP_OPERATION_INSERT;
  bool dangling = FI_INSERTED (FI_TEST_SIDECAR_DANGLING_REF) && context->type == HEAP_OPERATION_INSERT;

  for (size_t k = 0; k < plan->slots.size (); k++)
    {
      sidecar_plan_slot &s = plan->slots[k];

      if (!s.fresh)
	{
	  continue;
	}

      const int location = s.slot.location;
      const int length = sidecar_var_slot_length (image, location);
      RECDES record;

      /* room for the header the heap grows: an INSID, or the maximum header of a REC_BIGONE record */
      area.resize (SIDECAR_STORE_RECORD_LENGTH (length) + OR_MVCC_MAX_HEADER_SIZE);
      record.data = area.data ();
      record.area_size = (int) area.size ();
      sidecar_store_record_build (s.attrid, image->data + OR_VAR_OFFSET (image->data, location), length, &record);

      /* the fault injection's orphan is a second copy of the first value, which no ref names */
      for (int copy = 0; copy < (orphan ? 2 : 1); copy++)
	{
	  heap_create_insert_context (store_context, store_hfid, &class_oid, &record, NULL);
	  store_context->sidecar_store_op = true;
	  store_context->is_bulk_op = context->is_bulk_op;
	  store_context->use_bulk_logging = context->use_bulk_logging;

	  assert (!pgbuf_has_perm_pages_fixed (thread_p));
	  sidecar_count (thread_p, PSTAT_SC_EXTERNALIZE);
	  const int error_code = heap_insert_logical (thread_p, store_context, NULL);
	  if (error_code != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	      return error_code;
	    }
	  if (copy > 0)
	    {
	      continue;
	    }

	  SIDECAR_REF *ref = &s.slot.ref;
	  ref->pageid = store_context->res_oid.pageid;
	  ref->volid = store_context->res_oid.volid;
	  ref->slotid = store_context->res_oid.slotid;
	  ref->length = length;
	  ref->flags = store_context->recdes_p->type == REC_BIGONE ? SIDECAR_REF_BIGONE : 0;
	  ref->attrid_check = (UINT16) (s.attrid & 0xFFFF);
	}
      if (dangling)
	{
	  s.slot.ref.slotid = SHRT_MAX;
	}
      orphan = dangling = false;
    }
  return NO_ERROR;
}

/* I-4: each store record the new version no longer names gets the transaction's DELID, through
 * heap_delete_logical () with no data page held, before the hot record.  Vacuum removes it from its own DELID
 * (I-5); in SA mode the delete is physical and its undo holds the record. */
static int
sidecar_stamp_values (THREAD_ENTRY *thread_p, HEAP_OPERATION_CONTEXT *context, sidecar_plan *plan, HFID *store_hfid,
		      HEAP_OPERATION_CONTEXT *store_context)
{
  OID class_oid = context->class_oid;

  for (const SIDECAR_REF &ref : plan->stamps)
    {
      OID oid;

      oid.volid = ref.volid;
      oid.pageid = ref.pageid;
      oid.slotid = ref.slotid;
      heap_create_delete_context (store_context, store_hfid, &oid, &class_oid, NULL);
      store_context->sidecar_store_op = true;

      assert (!pgbuf_has_perm_pages_fixed (thread_p));
      sidecar_count (thread_p, PSTAT_SC_DROP);
      const int error_code = heap_delete_logical (thread_p, store_context);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return error_code;
	}
    }
  return NO_ERROR;
}

static int
sidecar_apply_plan_body (THREAD_ENTRY *thread_p, HEAP_OPERATION_CONTEXT *context)
{
  sidecar_plan *plan = context->sc_plan;
  HFID store_hfid;

  if (plan == NULL || plan->state == SIDECAR_PLAN_APPLIED)
    {
      return NO_ERROR;
    }

  /* I-7: the store is written, and a representation read, with no data page held.  The heap released its own pages,
   * and an INSERT keeps its values inline when its caller holds one (P1a D8).  A caller of an UPDATE or DELETE that
   * holds a page in this thread fails instead, with an assertion in debug builds and ER_SIDECAR_NOT_SUPPORTED in
   * release builds: an ordered fix of a store page may release and fix again a page the caller watches, which would
   * invalidate the record the caller peeked (P1b2 decision C5).  A stored procedure's statements run on a worker of
   * their own, with no page of the calling scan.  Fault 700006 takes the release path in a debug build. */
  const bool page_held = pgbuf_has_perm_pages_fixed (thread_p);
  if (page_held || (context->type != HEAP_OPERATION_INSERT && FI_INSERTED (FI_TEST_SIDECAR_PAGE_HELD_AT_APPLY)))
    {
#if !defined (NDEBUG)
      if (page_held)
	{
	  pgbuf_log_perm_pages_fixed (thread_p, "Sidecar: a store write");
	}
#endif /* !NDEBUG */
      assert (!page_held);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SIDECAR_NOT_SUPPORTED, 1,
	      "A store write while the caller holds a data page");
      return ER_SIDECAR_NOT_SUPPORTED;
    }

  if (plan->state == SIDECAR_PLAN_NEEDS_LAYOUT)
    {
      /* the plan hook decides on the next location step, with the image's attribute ids; it asks once, and without
       * them it rewrites the values instead of comparing them */
      const int error_code = sidecar_plan_read_facts (thread_p, context, plan, true);

      plan->state = SIDECAR_PLAN_PREPARED;
      plan->layout_read = true;
      return error_code;
    }
  assert (plan->state == SIDECAR_PLAN_DECIDED);

  int error_code = sidecar_store_for (thread_p, &context->hfid, &context->class_oid, &store_hfid);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  if (plan->inline_all)
    {
      /* the kept values come back into the image before their store records are stamped */
      error_code = sidecar_expand_locked (thread_p, context->recdes_p, plan->hot_bytes, &plan->hot);
      if (error_code != NO_ERROR)
	{
	  return error_code;
	}
    }

  /* the store writes are not this row's replication targets */
  LOG_TDES *tdes = LOG_FIND_CURRENT_TDES (thread_p);
  const LOG_LSA saved_insert_lsa = tdes->repl_insert_lsa;
  const LOG_LSA saved_update_lsa = tdes->repl_update_lsa;
  HEAP_OPERATION_CONTEXT *store_context =
	  (HEAP_OPERATION_CONTEXT *) db_private_alloc (thread_p, sizeof (HEAP_OPERATION_CONTEXT));

  if (store_context == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (HEAP_OPERATION_CONTEXT));
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  error_code = sidecar_insert_values (thread_p, context, plan, &store_hfid, store_context);
  if (error_code == NO_ERROR)
    {
      error_code = sidecar_stamp_values (thread_p, context, plan, &store_hfid, store_context);
    }
  if (error_code == NO_ERROR && context->type != HEAP_OPERATION_INSERT
      && FI_INSERTED (FI_TEST_SIDECAR_FAIL_AFTER_STORE_WRITES))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SIDECAR_FAULT_INJECTED, 1, FI_TEST_SIDECAR_FAIL_AFTER_STORE_WRITES);
      error_code = ER_SIDECAR_FAULT_INJECTED;
    }
  tdes->repl_insert_lsa = saved_insert_lsa;
  tdes->repl_update_lsa = saved_update_lsa;
  db_private_free (thread_p, store_context);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  if (plan->inline_all)
    {
      sidecar_plan_use_hot (context, plan);
    }
  else if (plan->rebuild)
    {
      error_code = sidecar_plan_build_hot (plan, context->recdes_p, &context->class_oid);
      if (error_code != NO_ERROR)
	{
	  return error_code;
	}
      sidecar_plan_use_hot (context, plan);
    }
  plan->state = SIDECAR_PLAN_APPLIED;
  return NO_ERROR;
}

__attribute__ ((noinline)) int
sidecar_apply_plan (THREAD_ENTRY *thread_p, HEAP_OPERATION_CONTEXT *context)
{
  /* the module's own store reads and writes, and the policy's page test, find no store page the cursor kept */
  sidecar_cursor_release (thread_p);
  try
    {
      return sidecar_apply_plan_body (thread_p, context);
    }
  catch (const std::bad_alloc &)
    {
      return sidecar_out_of_memory ();
    }
}

__attribute__ ((noinline)) void
sidecar_op_end_plan (THREAD_ENTRY *thread_p, HEAP_OPERATION_CONTEXT *context, int rc)
{
  sidecar_plan *plan = context->sc_plan;

  if (plan->client_recdes != NULL)
    {
      context->recdes_p = plan->client_recdes;
    }
  delete plan;
  context->sc_plan = NULL;
}

/*
 * A row that moves to another heap takes no ref along (I-3): the store it names belongs to the heap it leaves.  Each
 * kept ref of the moved image must be a ref of the version the move replaces, which the mover's X lock keeps current,
 * and its value is read back as a row-lock holder (5.6 b), one store page at a time (I-7).
 * The INSERT hook of the new heap then applies that heap's own policy, and the DELETE of the old row stamps every
 * store record the old version names.
 */
static int
sidecar_inline_kept_body (THREAD_ENTRY *thread_p, const OID *class_oid, const RECDES *old_stored, const RECDES *image,
			  RECDES *out)
{
  if (old_stored == NULL || !sidecar_record_has_refs (old_stored) || !sidecar_hot_is_well_formed (old_stored)
      || !sidecar_hot_is_well_formed (image))
    {
      return sidecar_set_bad_ref (class_oid);
    }

  const int n_old = sidecar_trailer_n_refs (old_stored);
  const int n_refs = sidecar_trailer_n_refs (image);
  std::vector<char> matched (n_old, 0);

  for (int i = 0; i < n_refs; i++)
    {
      SIDECAR_REF ref;
      int j;

      sidecar_hot_get_ref (image, i, &ref);
      for (j = 0; j < n_old; j++)
	{
	  SIDECAR_REF old_ref;

	  sidecar_hot_get_ref (old_stored, j, &old_ref);
	  if (matched[j] == 0 && sidecar_ref_equal (&old_ref, &ref))
	    {
	      matched[j] = 1;
	      break;
	    }
	}
      if (j == n_old)
	{
	  return sidecar_set_bad_ref (class_oid);
	}
    }

  const int length = sidecar_expanded_length (image);
  if (length < 0)
    {
      assert_release (false);
      return sidecar_set_store_invalid ("a moved image with kept refs does not expand");
    }

  std::vector<char> bytes (length + OR_MVCC_MAX_HEADER_SIZE);
  std::vector<int> value_offsets (n_refs);
  RECDES expanded;

  expanded.data = bytes.data ();
  expanded.area_size = (int) bytes.size ();
  if (sidecar_expand_frame (image, &expanded, value_offsets.data ()) != NO_ERROR)
    {
      assert_release (false);
      return sidecar_set_store_invalid ("a moved image with kept refs does not expand");
    }

  /* the mover holds no page here */
  assert (!pgbuf_has_perm_pages_fixed (thread_p));
  for (int i = 0; i < n_refs; i++)
    {
      SIDECAR_REF ref;

      sidecar_hot_get_ref (image, i, &ref);
      const int error_code = sidecar_read_value_locked (thread_p, &ref, expanded.data + value_offsets[i]);
      if (error_code != NO_ERROR)
	{
	  return error_code;
	}
    }

  /* the caller frees it, so it is allocated last, after everything that may throw */
  *out = expanded;
  out->data = (char *) db_private_alloc (thread_p, expanded.area_size);
  if (out->data == NULL)
    {
      return sidecar_out_of_memory ();
    }
  memcpy (out->data, expanded.data, expanded.length);
  return NO_ERROR;
}

__attribute__ ((noinline)) int
sidecar_inline_kept (THREAD_ENTRY *thread_p, const OID *class_oid, const RECDES *old_stored, const RECDES *image,
		     RECDES *out)
{
  /* the module's own store reads and writes, and the policy's page test, find no store page the cursor kept */
  sidecar_cursor_release (thread_p);
  try
    {
      return sidecar_inline_kept_body (thread_p, class_oid, old_stored, image, out);
    }
  catch (const std::bad_alloc &)
    {
      return sidecar_out_of_memory ();
    }
}

/*
 * Kept refs.  An UPDATE that does not assign an attribute whose slot in the old record holds a ref keeps the ref:
 * heap_attrinfo_set_uninitialized () marks the value HEAP_KEPT_REF_ATTRVALUE with the ref's 16 bytes in its
 * dbvalue, so HEAP_ATTRVALUE gains no field, and the column writer copies them into the new image with a trailer.
 * No store page is read.  sidecar_plan_update () checks each kept ref against the current version (I-3).
 */
__attribute__ ((noinline)) bool
sidecar_keep_ref (HEAP_ATTRVALUE *value, const RECDES *old_recdes)
{
  const OR_ATTRIBUTE *read = value->read_attrepr;

  assert (sidecar_Stored_form_window > 0 && sidecar_stored_form_is_admitted (old_recdes->data));
  if (read == NULL || read->is_fixed || value->attr_type != HEAP_INSTANCE_ATTR || value->last_attrepr == NULL
      || value->last_attrepr->is_fixed || tp_domain_match (read->domain, value->last_attrepr->domain,
	  TP_EXACT_MATCH) == 0
      || sidecar_trailer_find (old_recdes, read->location) < 0)
    {
      return false;
    }

  char *bytes = (char *) db_private_alloc (NULL, SIDECAR_REF_SIZE);
  if (bytes == NULL)
    {
      /* the value is read through the ref instead: slower, as correct */
      er_clear ();
      return false;
    }
  memcpy (bytes, old_recdes->data + OR_VAR_OFFSET (old_recdes->data, read->location), SIDECAR_REF_SIZE);
  db_make_varbit (&value->dbvalue, SIDECAR_REF_SIZE * CHAR_BIT, bytes, SIDECAR_REF_SIZE * CHAR_BIT);
  value->dbvalue.need_clear = true;
  value->state = HEAP_KEPT_REF_ATTRVALUE;
  sidecar_count (NULL, PSTAT_SC_KEEP);
  return true;
}

static void
sidecar_kept_ref_decode (const HEAP_ATTRVALUE *value, SIDECAR_REF *ref)
{
  int bits;

  sidecar_ref_decode (db_get_bit (&value->dbvalue, &bits), ref);
}

/* the inline length of a kept value, so that the offset size the writer picks also fits the record expanded
 * (sidecar_expand_frame () keeps the offset size) */
__attribute__ ((noinline)) int
sidecar_kept_ref_length (const HEAP_ATTRVALUE *value)
{
  SIDECAR_REF ref;

  sidecar_kept_ref_decode (value, &ref);
  return ref.length;
}

__attribute__ ((noinline)) SCAN_CODE
sidecar_write_kept_ref (HEAP_CACHE_ATTRINFO *attr_info, int value_index, OR_BUF *buf, char **ptr_varvals,
			int offset_size, int header_size)
{
  const HEAP_ATTRVALUE *value = &attr_info->values[value_index];
  int bits;

  buf->ptr = (char *) (OR_VAR_ELEMENT_PTR (buf->buffer, value->last_attrepr->location));
  if (buf->ptr + offset_size > buf->endptr)
    {
      return S_DOESNT_FIT;
    }
  or_put_offset_internal (buf, CAST_BUFLEN (*ptr_varvals - buf->buffer - header_size), offset_size);

  if (*ptr_varvals + SIDECAR_REF_SIZE > buf->endptr)
    {
      return S_DOESNT_FIT;
    }
  memcpy (*ptr_varvals, db_get_bit (&value->dbvalue, &bits), SIDECAR_REF_SIZE);
  *ptr_varvals += SIDECAR_REF_SIZE;
  return S_SUCCESS;
}

/* after the variable area of an image with kept refs: the trailer, and the flag in the header word */
static SCAN_CODE
sidecar_write_trailer_body (HEAP_CACHE_ATTRINFO *attr_info, OR_BUF *buf, char **ptr_varvals)
{
  std::vector<int> locations;

  for (int i = 0; i < attr_info->num_values; i++)
    {
      if (attr_info->values[i].state == HEAP_KEPT_REF_ATTRVALUE)
	{
	  locations.push_back (attr_info->values[i].last_attrepr->location);
	}
    }
  std::sort (locations.begin (), locations.end ());

  const int n_refs = (int) locations.size ();
  const int size = SIDECAR_TRAILER_SIZE (n_refs);
  if (n_refs == 0 || *ptr_varvals + size > buf->endptr)
    {
      return n_refs == 0 ? S_SUCCESS : S_DOESNT_FIT;
    }

  for (int i = 0; i < SIDECAR_TRAILER_N_PAD (n_refs); i++)
    {
      OR_PUT_SHORT (*ptr_varvals + i * OR_SHORT_SIZE, i < n_refs ? locations[i] : SIDECAR_TRAILER_PAD);
    }
  sidecar_trailer_put_counts (*ptr_varvals + size, attr_info->last_classrepr->n_variable, n_refs);
  *ptr_varvals += size;
  OR_PUT_INT (buf->buffer, OR_GET_INT (buf->buffer) | (OR_MVCC_FLAG_HAS_SIDECAR << OR_MVCC_FLAG_SHIFT_BITS));
#if !defined (NDEBUG)
  assert (sidecar_Stored_form_window > 0);
  sidecar_stored_form_admit (buf->buffer, CAST_BUFLEN (*ptr_varvals - buf->buffer));
#endif /* !NDEBUG */
  return S_SUCCESS;
}

__attribute__ ((noinline)) SCAN_CODE
sidecar_write_trailer (HEAP_CACHE_ATTRINFO *attr_info, OR_BUF *buf, char **ptr_varvals)
{
  try
    {
      return sidecar_write_trailer_body (attr_info, buf, ptr_varvals);
    }
  catch (const std::bad_alloc &)
    {
      (void) sidecar_out_of_memory ();
      return S_ERROR;
    }
}

/*
 * Fetches.  A reader of a record with refs is one of the classes of 5.6:
 *   PROTECTED  its scan cache holds the transaction's registered snapshot (a);
 *   HOLDER     it reads the last version under the row lock (b);
 *   COMMITTED  a snapshot that may take a version from the undo log after the hot latch was released (c), the
 *              committed snapshot of xlocator_fetch () or a snapshot struct nobody registered;
 *   LATCHED    a dirty or NULL snapshot, which resolves the current version under the hot latch (c).
 */
enum sidecar_reader
{
  SIDECAR_READER_PROTECTED,
  SIDECAR_READER_HOLDER,
  SIDECAR_READER_COMMITTED,
  SIDECAR_READER_LATCHED
};

static sidecar_reader
sidecar_classify (THREAD_ENTRY *thread_p, const HEAP_SCANCACHE *scan_cache, SIDECAR_FETCH fetch)
{
  if (fetch == SIDECAR_FETCH_LAST_VERSION)
    {
      return SIDECAR_READER_HOLDER;
    }
  if (fetch == SIDECAR_FETCH_RECORD_INFO)
    {
      /* the record-info scan reads every record of a page, visible or not, under the hot latch */
      return SIDECAR_READER_LATCHED;
    }

  const MVCC_SNAPSHOT *snapshot = scan_cache != NULL ? scan_cache->mvcc_snapshot : NULL;
  if (snapshot == NULL || snapshot->snapshot_fnc == NULL || snapshot->snapshot_fnc == mvcc_satisfies_dirty)
    {
      return SIDECAR_READER_LATCHED;
    }
  if (snapshot->snapshot_fnc == mvcc_satisfies_snapshot)
    {
      const LOG_TDES *tdes = LOG_FIND_CURRENT_TDES (thread_p);

      if (tdes != NULL && snapshot == &tdes->mvccinfo.snapshot && tdes->mvccinfo.snapshot.valid)
	{
	  return SIDECAR_READER_PROTECTED;
	}
    }
  return SIDECAR_READER_COMMITTED;
}

/*
 * What the thread last kept lazy for a query scan: the record, the batch page or the batch's copies it lies in, the
 * scan, and the transaction that fetched it.  A lazy record is protected only within that transaction: its commit or
 * abort resets the transaction's lowest visible MVCCID, and vacuum may then remove a store record the record names
 * (decision E19).  The attribute layer refuses to read a ref of such a record after the transaction ended.
 */
struct sidecar_lazy_keep
{
  const HEAP_SCANCACHE *scan;
  const char *begin;
  const char *end;
  TRANID trid;
  bool cursor;			/* the store cursor may keep its page for the record's values (E18) */
};

static thread_local sidecar_lazy_keep sidecar_Lazy = { NULL, NULL, NULL, NULL_TRANID, false };

/* The transaction of the thread's first lazy keep since it last had no open scan that kept one, and the number of
 * those scans.  A newer keep replaces sidecar_Lazy while an older one may still be read, e.g. the next copied row of a
 * filter iterator driving a join after the inner scan kept its row (scan_batch_iter_next ()).  A keep can be read only
 * while its scan is open, and trids only advance on a thread, so the first keep is the oldest one still readable:
 * while it is the current transaction, so is every keep that can still be read.  Once every scan that kept ended, no
 * keep is left to read, and the next keep starts over: a later transaction of the same request or of an SA_MODE
 * process, which has no request boundary, is not checked against an ended one (P3 868). */
static thread_local TRANID sidecar_Lazy_first_trid = NULL_TRANID;
static thread_local int sidecar_Lazy_scans = 0;

static TRANID
sidecar_current_trid (THREAD_ENTRY *thread_p)
{
  const LOG_TDES *tdes = LOG_FIND_CURRENT_TDES (thread_p);

  return tdes != NULL ? tdes->trid : NULL_TRANID;
}

static bool
sidecar_lazy_holds (const char *p)
{
  return sidecar_Lazy.begin != NULL && sidecar_Lazy.begin <= p && p < sidecar_Lazy.end;
}

/* Records [begin, end) as kept lazy by scan_cache.  The store cursor may keep a page for its values only for a fixed
 * heap scan that holds its hot page between rows (cache_last_fix_page, not a local-cache read): only there does develop
 * itself hold a page across the processing of a row, and only there is the store page held no longer than the hot
 * page (E18).  Any other keep releases the cursor. */
static void
sidecar_lazy_note (THREAD_ENTRY *thread_p, HEAP_SCANCACHE *scan_cache, const char *begin, size_t length,
		   bool page_held)
{
  const bool cursor = (page_held && scan_cache->sidecar_lazy == HEAP_SIDECAR_LAZY_SCAN
		       && scan_cache->cache_last_fix_page && scan_cache->read_mode != HEAP_SCAN_READ_LOCAL_CACHE);

  if (!cursor || thread_p->sidecar_cursor_owner != scan_cache)
    {
      sidecar_cursor_release (thread_p);
    }
  sidecar_Lazy.scan = scan_cache;
  sidecar_Lazy.begin = begin;
  sidecar_Lazy.end = begin + length;
  sidecar_Lazy.trid = sidecar_current_trid (thread_p);
  if (!scan_cache->sidecar_kept)
    {
      scan_cache->sidecar_kept = true;
      sidecar_Lazy_scans++;
    }
  if (sidecar_Lazy_first_trid == NULL_TRANID)
    {
      sidecar_Lazy_first_trid = sidecar_Lazy.trid;
    }
  sidecar_Lazy.cursor = cursor;
  thread_p->sidecar_cursor_owner = cursor ? scan_cache : NULL;
}

/* A record a query scan keeps as stored for the attribute layer. */
static void
sidecar_keep_lazy (THREAD_ENTRY *thread_p, HEAP_SCANCACHE *scan_cache, const RECDES *recdes)
{
#if !defined (NDEBUG)
  sidecar_lazy_admit (scan_cache, SIDECAR_LAZY_RECORD, recdes->data, (size_t) recdes->length);
#endif /* !NDEBUG */
  sidecar_lazy_note (thread_p, scan_cache, recdes->data, (size_t) recdes->length, true);
  sidecar_count (thread_p, PSTAT_SC_LAZY_RECORDS);
}

__attribute__ ((noinline)) bool
sidecar_scan_is_protected (THREAD_ENTRY *thread_p, const HEAP_SCANCACHE *scan_cache)
{
  return ((scan_cache->sidecar_lazy == HEAP_SIDECAR_LAZY_SCAN
	   || scan_cache->sidecar_lazy == HEAP_SIDECAR_LAZY_INDEX_SCAN)
	  && sidecar_classify (thread_p, scan_cache, SIDECAR_FETCH_VISIBLE) == SIDECAR_READER_PROTECTED);
}

__attribute__ ((noinline)) bool
sidecar_scan_keeps_lazy (THREAD_ENTRY *thread_p, HEAP_SCANCACHE *scan_cache, const RECDES *peeked)
{
  if (!sidecar_scan_is_protected (thread_p, scan_cache))
    {
      return false;
    }
  sidecar_keep_lazy (thread_p, scan_cache, peeked);
  return true;
}

__attribute__ ((noinline)) void
sidecar_batch_keeps (THREAD_ENTRY *thread_p, HEAP_SCANCACHE *scan_cache, PAGE_PTR page)
{
  /* the caller made the batch keep it (scan_batch_collect_chunk ()), knowing the scan is protected */
  assert (sidecar_scan_is_protected (thread_p, scan_cache));
#if !defined (NDEBUG)
  /* the rows are peeked from the page */
  sidecar_lazy_admit (scan_cache, SIDECAR_LAZY_PAGE, page, DB_PAGESIZE);
#endif /* !NDEBUG */
  if (!sidecar_lazy_holds (page) || sidecar_Lazy.scan != scan_cache
      || sidecar_Lazy.trid != sidecar_current_trid (thread_p))
    {
      sidecar_lazy_note (thread_p, scan_cache, page, DB_PAGESIZE, true);
    }
  sidecar_count (thread_p, PSTAT_SC_BATCH_ROWS);
}

__attribute__ ((noinline)) void
sidecar_batch_copied (THREAD_ENTRY *thread_p, HEAP_SCANCACHE *scan_cache, const char *data, size_t length)
{
  if (length == 0)
    {
      return;
    }
#if !defined (NDEBUG)
  sidecar_lazy_admit (scan_cache, SIDECAR_LAZY_COPIES, data, length);
#endif /* !NDEBUG */
  /* the page is let go while the copies' rows go out: no store page is kept for them */
  sidecar_lazy_note (thread_p, scan_cache, data, length, false);
}

/*
 * The store cursor (I-7).  Per thread, since the rule it keeps is per thread: a reader holds at most one store page,
 * releases it before it fixes another, and nobody fixes a hot page while holding one.  The attribute layer reads a
 * value of a REC_HOME store record through it.  The page stays fixed after the read only while a lazy query scan owns
 * the cursor, so that the next values on the same page cost no fix; the heap releases it before the thread fixes a
 * hot page (sidecar_cursor_before_hot_fix ()), the scan's end releases it, and every other store read and every store
 * write of the module releases it first.  A value of another store page releases it before the next fix.
 */
static thread_local VPID sidecar_Cursor_released = { NULL_PAGEID, NULL_VOLID };

/* unfixes the page the cursor keeps, the owner staying: the cursor moving to another store page of the same keep */
static void
sidecar_cursor_unfix (THREAD_ENTRY *thread_p)
{
  if (thread_p->sidecar_cursor_page != NULL)
    {
      PAGE_PTR page = (PAGE_PTR) thread_p->sidecar_cursor_page;

      thread_p->sidecar_cursor_page = NULL;
      pgbuf_get_vpid (page, &sidecar_Cursor_released);
      pgbuf_unfix_and_init (thread_p, page);
    }
}

__attribute__ ((noinline)) void
sidecar_cursor_release (THREAD_ENTRY *thread_p)
{
  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }
  sidecar_cursor_unfix (thread_p);
  /* a page is kept again only after the next lazy keep names its owner */
  thread_p->sidecar_cursor_owner = NULL;
}

__attribute__ ((noinline)) void
sidecar_scan_end (THREAD_ENTRY *thread_p, HEAP_SCANCACHE *scan_cache)
{
  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }
  if (thread_p->sidecar_cursor_owner == scan_cache)
    {
      sidecar_cursor_release (thread_p);
      VPID_SET_NULL (&sidecar_Cursor_released);
    }
  if (sidecar_Lazy.scan == scan_cache)
    {
      sidecar_Lazy = { NULL, NULL, NULL, NULL_TRANID, false };
    }
  if (scan_cache->sidecar_kept)
    {
      scan_cache->sidecar_kept = false;
      /* the thread that kept ends the scan */
      assert (sidecar_Lazy_scans > 0);
      if (sidecar_Lazy_scans > 0 && --sidecar_Lazy_scans == 0)
	{
	  sidecar_Lazy_first_trid = NULL_TRANID;
	}
#if !defined (NDEBUG)
      sidecar_lazy_admitted_drop (scan_cache);
#endif /* !NDEBUG */
    }
}

/* Request boundaries (net_server_request ()): no store page, owner or lazy keep crosses them.  A scan whose end was
 * skipped (a parallel spec marked ended) leaves nothing behind. */
__attribute__ ((noinline)) void
sidecar_request_end (THREAD_ENTRY *thread_p)
{
  sidecar_cursor_release (thread_p);
  sidecar_Lazy = { NULL, NULL, NULL, NULL_TRANID, false };
  sidecar_Lazy_first_trid = NULL_TRANID;
  sidecar_Lazy_scans = 0;
#if !defined (NDEBUG)
  sidecar_Lazy_admitted.clear ();
#endif /* !NDEBUG */
}

#if !defined (NDEBUG)
void
sidecar_assert_request_start (THREAD_ENTRY *thread_p)
{
  assert (thread_p->sidecar_cursor_page == NULL && thread_p->sidecar_cursor_owner == NULL);
  assert (sidecar_Lazy.scan == NULL && sidecar_Lazy.begin == NULL && sidecar_Lazy.trid == NULL_TRANID);
  assert (sidecar_Lazy_first_trid == NULL_TRANID && sidecar_Lazy_scans == 0);
}
#endif /* !NDEBUG */

/* the value of a ref, read by the attribute layer: as a row-lock holder or a protected reader, it always finds the
 * store record (5.6 a and b) */
static int
sidecar_cursor_read_value (THREAD_ENTRY *thread_p, const char *slot, const SIDECAR_REF *ref, char *out)
{
  PAGE_PTR page = (PAGE_PTR) thread_p->sidecar_cursor_page;
  VPID vpid;
  OID oid;
  RECDES peeked;

  oid.volid = ref->volid;
  oid.pageid = ref->pageid;
  oid.slotid = ref->slotid;
  vpid.volid = ref->volid;
  vpid.pageid = ref->pageid;

  if (page != NULL && VPID_EQ (pgbuf_get_vpid_ptr (page), &vpid))
    {
      thread_p->sidecar_cursor_page = NULL;
      sidecar_count (thread_p, PSTAT_SC_CURSOR_HITS);
    }
  else
    {
      sidecar_cursor_unfix (thread_p);
      switch (sidecar_fix_store_page (thread_p, &oid, &page))
	{
	case SIDECAR_READ_PRESENT:
	  break;
	case SIDECAR_READ_ABSENT:
	  /* a protected reader always finds its store records (5.6 a and b); only fault 700001 makes a ref name none */
	  assert (FI_INSERTED (FI_TEST_SIDECAR_DANGLING_REF));
	  return sidecar_set_dangling (ref, ref->attrid_check);
	default:
	{
	  int error_code;

	  ASSERT_ERROR_AND_SET (error_code);
	  return error_code;
	}
	}
      if (VPID_EQ (&vpid, &sidecar_Cursor_released))
	{
	  sidecar_count (thread_p, PSTAT_SC_CURSOR_REFIX);
	}
    }

  if (spage_get_record_type (page, oid.slotid) != REC_HOME
      || spage_get_record (thread_p, page, oid.slotid, &peeked, PEEK) != S_SUCCESS)
    {
      /* relocated, REC_BIGONE or gone: the reader of B10, one store page at a time */
      pgbuf_unfix_and_init (thread_p, page);
      return sidecar_read_value_locked (thread_p, ref, out);
    }

  sidecar_count (thread_p, PSTAT_SC_STORE_READS);
  const char *value = sidecar_store_value (&peeked, ref);
  if (value != NULL)
    {
      memcpy (out, value, ref->length);
    }
  /* the page stays only for the next values of the record the owning fixed heap scan keeps (E18, E20) */
  if (value != NULL && thread_p->sidecar_cursor_owner != NULL && sidecar_Lazy.cursor
      && thread_p->sidecar_cursor_owner == sidecar_Lazy.scan && sidecar_lazy_holds (slot))
    {
      thread_p->sidecar_cursor_page = (char *) page;
    }
  else
    {
      pgbuf_unfix_and_init (thread_p, page);
    }
  return value != NULL ? NO_ERROR : sidecar_set_dangling (ref, ref->attrid_check);
}

/* the retry rule of 5.6 (c), for a version of which a store record was found absent */
static SCAN_CODE
sidecar_on_absent (THREAD_ENTRY *thread_p, const HEAP_SCANCACHE *scan_cache, const RECDES *hot, sidecar_reader reader,
		   int attempt, const SIDECAR_REF *ref)
{
  MVCC_REC_HEADER header;
  RECDES copy = *hot;

  if (reader != SIDECAR_READER_PROTECTED && or_mvcc_get_header (&copy, &header) == NO_ERROR
      && MVCC_IS_HEADER_DELID_VALID (&header) && !log_Gl.mvcc_table.is_active (MVCC_GET_DELID (&header)))
    {
      /* the row's delete committed, and vacuum may remove the row at any moment, as on develop */
      return S_DOESNT_EXIST;
    }
  if (reader != SIDECAR_READER_COMMITTED)
    {
      /* a protected reader, a lock holder and a latched reader always find the records (5.6) */
      (void) sidecar_set_dangling (ref, ref->attrid_check);
      return S_ERROR;
    }
  if (attempt < SIDECAR_RETRY_BOUND)
    {
      sidecar_count (thread_p, PSTAT_SC_RESOLVE_RETRY);
      er_log_debug (ARG_FILE_LINE, "Sidecar: retry %d of a committed-snapshot fetch, store record %d|%d|%d absent",
		    attempt + 1, ref->volid, ref->pageid, ref->slotid);
      return S_SIDECAR_RETRY;
    }
  if (scan_cache->mvcc_snapshot->snapshot_fnc == mvcc_satisfies_committed)
    {
      /* only xlocator_fetch () reads with the committed snapshot, and it reads the row again under an S lock */
      er_log_debug (ARG_FILE_LINE, "Sidecar: retries exhausted after %d reads, the fetch takes the lock fallback",
		    attempt + 1);
      return S_SIDECAR_LOCK_NEEDED;
    }
  assert (false);
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SIDECAR_RESOLVE_RETRY, 1, attempt + 1);
  return S_ERROR;
}

/*
 * The fetch exits.  A lock holder whose scan cache opted in keeps the record as stored, and the attribute layer
 * reads the values it decodes (sidecar_point_variable ()).  Every other reader gets the record expanded where a
 * REC_BIGONE record would go: into the scan cache area for PEEK or when the record is already there, else into the
 * caller's area, which returns S_DOESNT_FIT with -needed when it is too small.  The trailer says how many variable
 * attributes the record has, so the expansion fixes no page but one store page at a time.
 */
static SCAN_CODE
sidecar_on_fetch_body (THREAD_ENTRY *thread_p, HEAP_SCANCACHE *scan_cache, RECDES *recdes, SIDECAR_FETCH fetch,
		       int attempt)
{
  const OID *class_oid = scan_cache != NULL ? &scan_cache->node.class_oid : &oid_Null_oid;
  const sidecar_reader reader = sidecar_classify (thread_p, scan_cache, fetch);

  if (scan_cache != NULL
      && (scan_cache->sidecar_lazy == HEAP_SIDECAR_LAZY_SCAN
	  || scan_cache->sidecar_lazy == HEAP_SIDECAR_LAZY_INDEX_SCAN)
      && (reader == SIDECAR_READER_PROTECTED || reader == SIDECAR_READER_HOLDER))
    {
      /* a query scan's protected read: the attribute layer reads the values the query decodes (5.6 a and b) */
      sidecar_keep_lazy (thread_p, scan_cache, recdes);
      return S_SUCCESS;
    }
  if (reader == SIDECAR_READER_HOLDER && scan_cache != NULL && scan_cache->sidecar_lazy == HEAP_SIDECAR_LAZY_LOCKED)
    {
      /* the old record of an UPDATE under its X lock (B18) */
#if !defined (NDEBUG)
      assert (sidecar_Stored_form_window > 0);
      sidecar_stored_form_admit (recdes->data, recdes->length);
#endif /* !NDEBUG */
      sidecar_count (thread_p, PSTAT_SC_LAZY_RECORDS);
      return S_SUCCESS;
    }

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

  /* debug: the first store read of a committed-snapshot fetch, or every one, finds its record absent */
  const bool fault = (reader == SIDECAR_READER_COMMITTED
		      && ((attempt == 0 && FI_INSERTED (FI_TEST_SIDECAR_ABSENT_ONCE))
			  || FI_INSERTED (FI_TEST_SIDECAR_ABSENT_ALWAYS)));

  for (int i = 0; i < n_refs; i++)
    {
      SIDECAR_REF ref;

      sidecar_hot_get_ref (&hot, i, &ref);
      if (i != 0 || !fault)
	{
	  sidecar_count (thread_p, PSTAT_SC_EXPAND);
	}
      if (fetch == SIDECAR_FETCH_LAST_VERSION)
	{
	  sidecar_count (thread_p, PSTAT_SC_EXPAND_LAST_VERSION);
	}
      const sidecar_read_result result = ((i == 0 && fault) ? SIDECAR_READ_ABSENT
					  : sidecar_read_value (thread_p, &ref, recdes->data + value_offsets[i]));
      if (result == SIDECAR_READ_FAILED)
	{
	  return S_ERROR;
	}
      if (result == SIDECAR_READ_ABSENT)
	{
	  return sidecar_on_absent (thread_p, scan_cache, &hot, reader, attempt, &ref);
	}
    }
  return S_SUCCESS;
}

__attribute__ ((noinline)) SCAN_CODE
sidecar_on_fetch (THREAD_ENTRY *thread_p, HEAP_SCANCACHE *scan_cache, RECDES *recdes, SIDECAR_FETCH fetch,
		  int attempt)
{
  try
    {
      return sidecar_on_fetch_body (thread_p, scan_cache, recdes, fetch, attempt);
    }
  catch (const std::bad_alloc &)
    {
      (void) sidecar_out_of_memory ();
      return S_ERROR;
    }
}

/*
 * The lock fallback of 5.6 (c): a committed-snapshot fetch that found a store record absent after the retry bound.
 * Each retry was paid for by a distinct stamping commit, so the row may well be live.  Read it as a lock holder: the
 * S lock waits until no writer holds the row, and every stamp needs its X lock (I-4), so the read needs no retry.
 * The lock is released unless the transaction held a lock on the row before.
 */
SCAN_CODE
sidecar_fetch_locked (THREAD_ENTRY *thread_p, const OID *oid, OID *class_oid, HEAP_SCANCACHE *scan_cache,
		      RECDES *recdes)
{
  const MVCC_SNAPSHOT *snapshot = scan_cache->mvcc_snapshot;
  const bool held = lock_get_object_lock (oid, class_oid) != NULL_LOCK;
  HEAP_GET_CONTEXT context;
  SCAN_CODE scan;

  assert (snapshot != NULL && snapshot->snapshot_fnc == mvcc_satisfies_committed);

  if (lock_object (thread_p, oid, class_oid, S_LOCK, LK_UNCOND_LOCK) != LK_GRANTED)
    {
      return S_ERROR;
    }
  sidecar_count (thread_p, PSTAT_SC_RESOLVE_RETRY_EXHAUSTED);

  heap_init_get_context (thread_p, &context, oid, class_oid, recdes, scan_cache, COPY, NULL_CHN);
  scan = heap_get_last_version (thread_p, &context);
  heap_clean_get_context (thread_p, &context);

  if (scan == S_SUCCESS)
    {
      MVCC_REC_HEADER header;

      if (or_mvcc_get_header (recdes, &header) != NO_ERROR)
	{
	  scan = S_ERROR;
	}
      else
	{
	  switch (snapshot->snapshot_fnc (thread_p, &header, (MVCC_SNAPSHOT *) snapshot))
	    {
	    case SNAPSHOT_SATISFIED:
	      break;
	    case TOO_OLD_FOR_SNAPSHOT:
	      scan = S_DOESNT_EXIST;
	      break;
	    default:
	      /* the last version is this transaction's own; the committed one is in the log, and only this transaction
	       * can have stamped its store records */
	      scan = heap_get_visible_version (thread_p, oid, class_oid, recdes, scan_cache, COPY, NULL_CHN);
	      if (scan == S_SIDECAR_LOCK_NEEDED)
		{
		  assert (false);
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SIDECAR_RESOLVE_RETRY, 1, SIDECAR_RETRY_BOUND + 1);
		  scan = S_ERROR;
		}
	      break;
	    }
	}
    }

  if (!held)
    {
      lock_unlock_object (thread_p, oid, class_oid, S_LOCK, true);
    }
  er_log_debug (ARG_FILE_LINE, "Sidecar: lock fallback read %d|%d|%d under an S lock: scan code %d",
		OID_AS_ARGS (oid), (int) scan);
  return scan;
}

/*
 * The attribute layer.  A record that reaches it with refs is one a lock holder read lazily, or an UPDATE image with
 * kept refs of the row it locked; both are read as a row-lock holder (5.6 b).  A slot that holds a value is decoded
 * as usual.
 */
/* I-7 for the attribute layer: when it reads the store, the thread holds no page but at most one page of the row's
 * own hot heap (a reader latches hot, then store).  An index page or a page of another file would be a latch order
 * I-7 does not cover. */
static bool
sidecar_attr_pages_ok (THREAD_ENTRY *thread_p, const HEAP_CACHE_ATTRINFO *attr_info)
{
#if !defined (NDEBUG)
  PAGE_PTR page = NULL;
  OID page_class;
  /* the store page the cursor keeps is the reader's own */
  const int n_fixed = pgbuf_count_perm_pages_fixed (thread_p, (PAGE_PTR) thread_p->sidecar_cursor_page, &page);

  if (n_fixed == 0)
    {
      return true;
    }
  if (n_fixed == 1 && pgbuf_get_page_ptype (thread_p, page) == PAGE_HEAP && !heap_page_is_sidecar_store (thread_p, page)
      && heap_get_class_oid_from_page (thread_p, page, &page_class) == NO_ERROR
      && OID_EQ (&page_class, &attr_info->class_oid))
    {
      return true;
    }
  pgbuf_log_perm_pages_fixed (thread_p, "Sidecar: an attribute-layer store read");
  return false;
#else /* NDEBUG */
  return true;
#endif /* NDEBUG */
}

/* the value bytes a ref slot of attribute attrid names, read from the store as a row-lock holder or a protected
 * reader, or found from an earlier read of the same ref; NULL with the error set when the read fails */
static const std::vector<char> *
sidecar_resolve_slot (HEAP_CACHE_ATTRINFO *attr_info, int attrid, const char *slot)
{
  THREAD_ENTRY *thread_p = thread_get_thread_entry_info ();
  SIDECAR_REF ref;
  OID oid;

  sidecar_ref_decode (slot, &ref);
  oid.volid = ref.volid;
  oid.pageid = ref.pageid;
  oid.slotid = ref.slotid;
  if (attr_info->sc == NULL)
    {
      attr_info->sc = new sidecar_attr_state ();
    }

  sidecar_attr_values &values = attr_info->sc->values[attrid];
  if (OID_EQ (&values.last.oid, &oid) && !values.last.bytes.empty ())
    {
      assert ((int) values.last.bytes.size () == ref.length);
      return &values.last.bytes;
    }
  if (OID_EQ (&values.before.oid, &oid) && !values.before.bytes.empty ())
    {
      assert ((int) values.before.bytes.size () == ref.length);
      return &values.before.bytes;
    }

  sidecar_count (thread_p, PSTAT_SC_RESOLVE);
  if (pgbuf_count_perm_pages_fixed (thread_p, (PAGE_PTR) thread_p->sidecar_cursor_page, NULL) > 0)
    {
      sidecar_count (thread_p, PSTAT_SC_RESOLVE_PAGE_HELD);
    }
  assert (sidecar_attr_pages_ok (thread_p, attr_info));
  std::vector<char> bytes (MAX (ref.length, 1));
  /* the latest keep's own transaction for its records; for any other record, that of the oldest keep, which may be
   * the record's (P3 868) */
  const TRANID kept_trid = sidecar_lazy_holds (slot) ? sidecar_Lazy.trid : sidecar_Lazy_first_trid;
  if (kept_trid != NULL_TRANID && kept_trid != sidecar_current_trid (thread_p))
    {
      /* the transaction that kept the record lazy ended (a COMMIT inside a stored procedure), and with it the
       * protection of its store records: never dereference (E19) */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SIDECAR_LAZY_AFTER_TRAN_END, 2, kept_trid,
	      sidecar_current_trid (thread_p));
      return NULL;
    }
  if (sidecar_cursor_read_value (thread_p, slot, &ref, bytes.data ()) != NO_ERROR)
    {
      return NULL;
    }
  bytes.resize (ref.length);
  values.before = std::move (values.last);
  values.last.oid = oid;
  values.last.bytes = std::move (bytes);
  return &values.last.bytes;
}

static bool
sidecar_point_variable_body (const HEAP_REC_LAYOUT *layout, HEAP_CACHE_ATTRINFO *attr_info,
			     const OR_ATTRIBUTE *attrepr, RECDES *raw)
{
  const char *end = layout->var_table + layout->trailer_end;

  /* only a record a protected reader kept as stored (an UPDATE's old record under its X lock, a lazy scan's record)
   * and an UPDATE image with kept refs carry refs here */
  assert (sidecar_stored_form_is_admitted (layout->var_table));
  /* P1a D1: the trailer's count is the record's representation's */
  assert ((unsigned short) OR_GET_SHORT (end - 3 * OR_SHORT_SIZE) == attr_info->read_classrepr->n_variable);
  if (sidecar_trailer_find_from_end (end, attrepr->location) < 0)
    {
      return false;
    }

  const char *slot = layout->var_table + OR_VAR_TABLE_ELEMENT_OFFSET_INTERNAL (layout->var_table, attrepr->location,
		     layout->offset_size);
  const std::vector<char> *bytes = sidecar_resolve_slot (attr_info, attrepr->id, slot);

  if (bytes == NULL)
    {
      /* heap_attrvalue_read () returns the error */
      raw->data = NULL;
      raw->length = -1;
      return true;
    }
  raw->data = (char *) bytes->data ();
  raw->length = (int) bytes->size ();
  return true;
}

__attribute__ ((noinline)) bool
sidecar_point_variable (const HEAP_REC_LAYOUT *layout, HEAP_CACHE_ATTRINFO *attr_info,
			const OR_ATTRIBUTE *attrepr, RECDES *raw)
{
  try
    {
      return sidecar_point_variable_body (layout, attr_info, attrepr, raw);
    }
  catch (const std::bad_alloc &)
    {
      (void) sidecar_out_of_memory ();
      raw->data = NULL;
      raw->length = -1;
      return true;
    }
}

static char *
sidecar_midxkey_point_body (HEAP_CACHE_ATTRINFO *attr_info, const RECDES *recdes, const OR_ATTRIBUTE *att,
			    char *disk_data)
{
  assert (sidecar_stored_form_is_admitted (recdes->data));
  assert (sidecar_trailer_n_variable (recdes) == attr_info->read_classrepr->n_variable);
  if (sidecar_trailer_find (recdes, att->location) < 0)
    {
      return disk_data;
    }

  const std::vector<char> *bytes = sidecar_resolve_slot (attr_info, att->id, disk_data);
  return bytes == NULL ? NULL : (char *) bytes->data ();
}

__attribute__ ((noinline)) char *
sidecar_midxkey_point (HEAP_CACHE_ATTRINFO *attr_info, const RECDES *recdes, const OR_ATTRIBUTE *att,
		       char *disk_data)
{
  try
    {
      return sidecar_midxkey_point_body (attr_info, recdes, att, disk_data);
    }
  catch (const std::bad_alloc &)
    {
      (void) sidecar_out_of_memory ();
      return NULL;
    }
}

void
sidecar_attr_state_free (HEAP_CACHE_ATTRINFO *attr_info)
{
  delete attr_info->sc;
  attr_info->sc = NULL;
}

#if !defined (NDEBUG)
void
sidecar_stored_form_begin (void)
{
  sidecar_Stored_form_window++;
}

void
sidecar_stored_form_end (void)
{
  sidecar_Stored_form_window--;
  assert (sidecar_Stored_form_window >= 0);
  if (sidecar_Stored_form_window == 0)
    {
      sidecar_Stored_form_admitted = 0;
    }
}
#endif /* !NDEBUG */


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
 * The verifier.  G13: a live store record, one without a committed DELID, must be named by exactly one current hot
 * record, and every ref of a current hot record must name a live store record of the same attribute and length.  A
 * dead store record, stamped by a committed transaction, waits for vacuum and may be named by nothing.  A hot record
 * whose delete committed is not current: vacuum may already have removed its store records.  The store is read into
 * a map first, so the hot scan never holds a store page.
 */
struct sidecar_verify_record
{
  int attrid;
  int length;			/* of the value */
  bool live;
  int named;
};

using sidecar_verify_records = std::map<OID, sidecar_verify_record, sidecar_oid_less>;

struct sidecar_verify
{
  const HFID *hfid;
  const HFID *store_hfid;
  OID class_oid;
  sidecar_verify_records records;
  bool valid;
  int relocated;		/* store records a stamp relocated */
  int bigone;			/* REC_BIGONE store records */
  int hot_rows;			/* hot records with refs */
  int hot_deleted;		/* of them, the ones whose delete committed */

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
 * skips the pages that hold the heap's bestspace storage.  Every page of a store says it is one, and no page of a
 * hot heap does (heap_page_is_sidecar_store ()). */
template <typename Visit>
static int
sidecar_verify_walk (THREAD_ENTRY *thread_p, sidecar_verify &v, const HFID *hfid, bool store, Visit &&visit)
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

      if (!is_bestspace && heap_page_is_sidecar_store (thread_p, page) != store)
	{
	  OID page_oid;

	  page_oid.volid = vpid.volid;
	  page_oid.pageid = vpid.pageid;
	  page_oid.slotid = NULL_SLOTID;
	  v.invalid (store ? "a page of the store does not say it is a store page"
		     : "a page of the heap says it is a store page", &page_oid);
	}

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

/* the record carries the DELID of a transaction that committed: a hot row whose delete committed, or a dead store
 * record.  A REC_BIGONE header always has the field, MVCCID_NULL until a delete. */
static bool
sidecar_record_delete_committed (const RECDES *record)
{
  MVCC_REC_HEADER header;
  RECDES copy = *record;

  return (or_mvcc_get_header (&copy, &header) == NO_ERROR && MVCC_IS_HEADER_DELID_VALID (&header)
	  && !log_Gl.mvcc_table.is_active (MVCC_GET_DELID (&header)));
}

static void
sidecar_verify_add_store_record (sidecar_verify &v, const OID &oid, const RECDES &record)
{
  SIDECAR_VALUE_HEADER header;
  const char *value;
  int length;

  if (sidecar_store_record_parse (&record, &header, &value, &length) != NO_ERROR)
    {
      v.invalid ("a record of the store is not a store record", &oid);
      return;
    }
  v.records[oid] = { header.attrid, length, !sidecar_record_delete_committed (&record), 0 };
}

/* the type of a store slot now, after the walk: vacuum, which the class lock does not keep out, may have removed
 * the record since */
static INT16
sidecar_verify_slot_type (THREAD_ENTRY *thread_p, const OID &oid)
{
  PAGE_PTR page;

  if (sidecar_fix_store_page (thread_p, &oid, &page) != SIDECAR_READ_PRESENT)
    {
      er_clear ();
      return REC_UNKNOWN;
    }
  const INT16 type = spage_get_record_type (page, oid.slotid);
  pgbuf_unfix_and_init (thread_p, page);
  return type;
}

static int
sidecar_verify_scan_store (THREAD_ENTRY *thread_p, sidecar_verify &v)
{
  struct forwarded
  {
    OID home;
    INT16 type;
    OID forward;
  };
  std::vector<forwarded> forwards;
  std::map<OID, int, sidecar_oid_less> newhomes;	/* REC_NEWHOME records, and how many homes forward to each */

  int error_code = sidecar_verify_walk (thread_p, v, v.store_hfid, true,
					[&] (const OID & oid, INT16 type, const RECDES & record)
  {
    if (type == REC_HOME)
      {
	sidecar_verify_add_store_record (v, oid, record);
      }
    else if (type == REC_RELOCATION || type == REC_BIGONE)
      {
	forwards.push_back ({ oid, type, * (OID *) record.data});
      }
    else if (type == REC_NEWHOME)
      {
	newhomes.emplace (oid, 0);
      }
    else if (type != REC_UNKNOWN)
      {
	v.invalid ("the store holds a record of a type a store never has", &oid);
      }
  });
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  /* read as a reader does, one store page at a time; a record that vanished since the walk was vacuumed */
  for (const forwarded &f : forwards)
    {
      std::vector<char> bytes;
      RECDES record;

      switch (sidecar_read_store_record (thread_p, &f.home, bytes, &record))
	{
	case SIDECAR_READ_PRESENT:
	  (f.type == REC_RELOCATION ? v.relocated : v.bigone)++;
	  if (f.type == REC_RELOCATION)
	    {
	      auto newhome = newhomes.find (f.forward);
	      if (newhome != newhomes.end ())
		{
		  newhome->second++;
		}
	    }
	  sidecar_verify_add_store_record (v, f.home, record);
	  break;
	case SIDECAR_READ_ABSENT:
	  if (sidecar_verify_slot_type (thread_p, f.home) == f.type)
	    {
	      v.invalid ("a store record forwards to nothing", &f.home);
	    }
	  break;
	default:
	  ASSERT_ERROR_AND_SET (error_code);
	  return error_code;
	}
    }

  for (const auto &newhome : newhomes)
    {
      if (newhome.second != 1 && (newhome.second > 1
				  || sidecar_verify_slot_type (thread_p, newhome.first) == REC_NEWHOME))
	{
	  v.invalid ("a REC_NEWHOME record of the store is not the forward record of exactly one home",
		     &newhome.first);
	}
    }
  return NO_ERROR;
}

/* what the verifier keeps of a hot record with refs, so it can look up representations with no page fixed */
struct sidecar_verify_hot
{
  OID oid;
  int repid;
  int n_variable;		/* from the trailer */
  bool deleted;			/* its delete committed: not a current version */
  std::vector<std::pair<int, SIDECAR_REF>> refs;	/* location and ref */
};

static int
sidecar_verify_scan_hot (THREAD_ENTRY *thread_p, sidecar_verify &v)
{
  std::vector<sidecar_verify_hot> hots;

  int error_code = sidecar_verify_walk (thread_p, v, v.hfid, false,
					[&] (const OID & oid, INT16 type, const RECDES & record)
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
    hot.deleted = sidecar_record_delete_committed (&record);
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
      v.hot_rows++;
      v.hot_deleted += hot.deleted ? 1 : 0;
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
      if (hot.deleted)
	{
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
	      || !named->second.live)
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

  sidecar_cursor_release (thread_p);
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
  v.relocated = v.bigone = v.hot_rows = v.hot_deleted = 0;

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

  int error_code;
  try
    {
      error_code = sidecar_verify_scan_store (thread_p, v);
      if (error_code == NO_ERROR)
	{
	  error_code = sidecar_verify_scan_hot (thread_p, v);
	}
    }
  catch (const std::bad_alloc &)
    {
      error_code = sidecar_out_of_memory ();
    }
  lock_unlock_object (thread_p, &v.class_oid, oid_Root_class_oid, S_LOCK, true);
  if (error_code != NO_ERROR)
    {
      return DISK_ERROR;
    }

  int live = 0;
  for (const auto &entry : v.records)
    {
      const sidecar_verify_record &r = entry.second;

      live += r.live ? 1 : 0;
      if (r.live && r.named != 1)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SIDECAR_ORPHAN_RECORD, 5, entry.first.volid, entry.first.pageid,
		  entry.first.slotid, r.attrid, r.named);
	  v.valid = false;
	}
    }

  er_log_debug (ARG_FILE_LINE, "Sidecar verify heap %d|%d|%d: store live %d dead %d relocated %d bigone %d; hot rows "
		"with refs %d, deleted %d; store reads %llu (fetch expansion %llu) writes %llu retries %llu fallbacks %llu; "
		"expansion by last-version reads %llu; store page fixes %llu; %s; attribute-layer reads %llu, with a page "
		"held %llu", HFID_AS_ARGS (hfid), live, (int) v.records.size () - live, v.relocated, v.bigone, v.hot_rows,
		v.hot_deleted, sidecar_counted (PSTAT_SC_STORE_READS), sidecar_counted (PSTAT_SC_EXPAND),
		sidecar_counted (PSTAT_SC_EXTERNALIZE) + sidecar_counted (PSTAT_SC_DROP),
		sidecar_counted (PSTAT_SC_RESOLVE_RETRY), sidecar_counted (PSTAT_SC_RESOLVE_RETRY_EXHAUSTED),
		sidecar_counted (PSTAT_SC_EXPAND_LAST_VERSION), sidecar_counted (PSTAT_SC_STORE_PAGE_FIXES),
		v.valid ? "valid" : "INVALID", sidecar_counted (PSTAT_SC_RESOLVE),
		sidecar_counted (PSTAT_SC_RESOLVE_PAGE_HELD));
  return v.valid ? DISK_VALID : DISK_INVALID;
}

int
sidecar_store_apply_tde (THREAD_ENTRY *thread_p, const HFID *hfid, TDE_ALGORITHM tde_algo)
{
  HFID store_hfid;
  VFID ovf_vfid;
  int error_code = heap_sidecar_get_store (thread_p, hfid, &store_hfid, NULL);

  if (error_code != NO_ERROR || HFID_IS_NULL (&store_hfid))
    {
      return error_code;
    }
  error_code = file_apply_tde_algorithm (thread_p, &store_hfid.vfid, tde_algo);
  if (error_code == NO_ERROR
      && heap_ovf_find_vfid (thread_p, &store_hfid, &ovf_vfid, false, PGBUF_UNCONDITIONAL_LATCH) != NULL)
    {
      error_code = file_apply_tde_algorithm (thread_p, &ovf_vfid, tde_algo);
    }
  return error_code;
}

/* what a heap's store holds, one store page fixed at a time; a relocated or REC_BIGONE record is read through its
 * home as a reader reads it, after the walk */
static int
sidecar_heap_stats_body (THREAD_ENTRY *thread_p, const HFID *hfid, SIDECAR_HEAP_STATS *stats)
{
  std::vector<std::pair<OID, INT16>> forwards;
  int error_code = heap_sidecar_get_store (thread_p, hfid, &stats->store_hfid, NULL);

  if (error_code != NO_ERROR || HFID_IS_NULL (&stats->store_hfid))
    {
      return error_code;
    }

  VPID vpid;
  vpid.volid = stats->store_hfid.vfid.volid;
  vpid.pageid = stats->store_hfid.hpgid;
  while (!VPID_ISNULL (&vpid))
    {
      PAGE_PTR page = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
      VPID next_vpid;

      if (page == NULL)
	{
	  ASSERT_ERROR_AND_SET (error_code);
	  return error_code;
	}
      if (heap_vpid_next (thread_p, &stats->store_hfid, page, &next_vpid) != NO_ERROR)
	{
	  pgbuf_unfix_and_init (thread_p, page);
	  ASSERT_ERROR_AND_SET (error_code);
	  return error_code;
	}
      stats->pages++;
      if (!heap_page_is_bestspace (thread_p, page))
	{
	  const int n_slots = spage_number_of_slots (page);

	  for (PGSLOTID slotid = HEAP_HEADER_AND_CHAIN_SLOTID + 1; slotid < n_slots; slotid++)
	    {
	      /* spage_get_record_type () reports a REC_MARKDELETED slot as REC_UNKNOWN */
	      const SPAGE_SLOT *slot = spage_get_slot (page, slotid);
	      const INT16 type = spage_get_record_type (page, slotid);
	      RECDES record;
	      OID oid;

	      oid.volid = vpid.volid;
	      oid.pageid = vpid.pageid;
	      oid.slotid = slotid;
	      if (slot != NULL && slot->record_type == REC_MARKDELETED)
		{
		  stats->markdeleted++;
		}
	      else if (type == REC_HOME && spage_get_record (thread_p, page, slotid, &record, PEEK) == S_SUCCESS)
		{
		  (sidecar_record_delete_committed (&record) ? stats->dead : stats->live)++;
		}
	      else if (type == REC_RELOCATION || type == REC_BIGONE)
		{
		  forwards.emplace_back (oid, type);
		}
	    }
	}
      pgbuf_unfix_and_init (thread_p, page);
      vpid = next_vpid;
    }

  for (const auto &f : forwards)
    {
      std::vector<char> bytes;
      RECDES record;

      switch (sidecar_read_store_record (thread_p, &f.first, bytes, &record))
	{
	case SIDECAR_READ_PRESENT:
	  (f.second == REC_RELOCATION ? stats->relocated : stats->bigone)++;
	  (sidecar_record_delete_committed (&record) ? stats->dead : stats->live)++;
	  break;
	case SIDECAR_READ_ABSENT:
	  /* vacuumed since the walk */
	  break;
	default:
	  ASSERT_ERROR_AND_SET (error_code);
	  return error_code;
	}
    }
  return NO_ERROR;
}

int
sidecar_heap_stats (THREAD_ENTRY *thread_p, const HFID *hfid, SIDECAR_HEAP_STATS *stats)
{
  *stats = SIDECAR_HEAP_STATS ();
  HFID_SET_NULL (&stats->store_hfid);
  sidecar_cursor_release (thread_p);
  try
    {
      return sidecar_heap_stats_body (thread_p, hfid, stats);
    }
  catch (const std::bad_alloc &)
    {
      return sidecar_out_of_memory ();
    }
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
