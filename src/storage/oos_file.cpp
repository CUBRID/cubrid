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

#if defined(CUBRID_UNIT_TEST_ENABLED)
#include <atomic>
#endif
#include <algorithm>
#include <cassert>
#include <cstring>
#include <map>
#include <new>
#include <vector>
#include "byte_span_writer.hpp"
#include "error_code.h"
#include "error_manager.h"
#include "file_manager.h"
#include "log_manager.h"
#include "memory_alloc.h"
#include "memory_hash.h"
#include "page_buffer.h"
#include "porting_inline.hpp"
#include "scope_exit.hpp"
#include "slotted_page.h"
#include "page_buffer_util.hpp"
#include "log_comm.h"
#include "log_impl.h"
#include "log_system_tran.hpp"
#include "xserver_interface.h"

#include "oos_file.hpp"
#include "oos_log.hpp"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

// ****************************************************************************
// static functions — forward declarations
// ****************************************************************************

static int
oos_vpid_init_new (THREAD_ENTRY *thread_p, PAGE_PTR page, void *args);

static int
oos_insert_within_page (THREAD_ENTRY *thread_p, const VFID &oos_vfid, oos_buffer src,
			const OOS_RECORD_HEADER &header, OID &oid);
static int
oos_insert_record_in_fixed_page (THREAD_ENTRY *thread_p, const VFID &oos_vfid, PAGE_PTR page_ptr,
				 const VPID &vpid, oos_buffer src, const OOS_RECORD_HEADER &header, OID &oid);
static int
oos_insert_across_pages (THREAD_ENTRY *thread_p, const VFID &oos_vfid, oos_buffer src,
			 OID &oid);
static int
oos_insert_single_page_batch (THREAD_ENTRY *thread_p, const VFID &oos_vfid,
			      cubbase::span<oos_insert_request> requests,
			      int needed_space);
static int
oos_read_chunk_in_page (THREAD_ENTRY *thread_p, PAGE_PTR page_ptr, const OID &oid,
			cubbase::byte_span_writer &writer, OOS_RECORD_HEADER &header_out);
static int
oos_read_within_page (THREAD_ENTRY *thread_p, const OID &oid,
		      cubbase::byte_span_writer &writer, OOS_RECORD_HEADER &header_out);
static int
oos_read_across_pages (THREAD_ENTRY *thread_p, const OID &next_oid,
		       int total_data_length, cubbase::byte_span_writer &writer);
static int
oos_check_head_header (const OOS_RECORD_HEADER &header, int expected_length, const OID &oid);
static void
oos_log_insert_physical (THREAD_ENTRY *thread_p, PAGE_PTR page_p, VFID *vfid_p, OID *oid_p, RECDES *recdes_p);
static void
oos_log_delete_physical (THREAD_ENTRY *thread_p, PAGE_PTR page_p, VFID *vfid_p, PGSLOTID slotid, RECDES *recdes_p);
static int
oos_delete_chain (THREAD_ENTRY *thread_p, const VFID &oos_vfid, const OID &oid,
		  std::vector<VPID> *touched_vpids);

STATIC_INLINE __attribute__ ((ALWAYS_INLINE))
int oos_get_max_chunk_size_within_page ();

static bool
oos_needs_repl_tracking (THREAD_ENTRY *thread_p);

static void
oos_publish_oos_oid (THREAD_ENTRY *thread_p, const OID &oid);

static void
oos_cleanup_insert_publication_state_on_error (THREAD_ENTRY *thread_p) noexcept;

static auto_unfix_page_ptr
oos_file_alloc_new (THREAD_ENTRY *thread_p, const VFID &oos_vfid, VPID &vpid_out);

static const auto_unfix_page_ptr
oos_find_best_page (THREAD_ENTRY *thread_p, const VFID &oos_vfid, const int rec_length, VPID &vpid);

static void
oos_reclaim_note_recovery_delete_locked (const VFID *oos_vfid, const LOG_LSA *rollback_delete_lsa);

static void
oos_reclaim_clear_all_states (void);

static void
oos_reclaim_note_file_created (const VFID &oos_vfid);

static void
oos_reclaim_forget_file (const VFID &oos_vfid);

// ****************************************************************************
// OOS Bestspace — constants
// ****************************************************************************

static constexpr int OOS_ALIGNMENT = MAX_ALIGNMENT;

#define OOS_BESTSPACE_CACHE_CAPACITY 1000
#define OOS_DROP_FREE_SPACE ((int)(DB_PAGESIZE * 0.3))
#define OOS_BESTSPACE_SYNC_THRESHOLD (0.1f)
#define BEST_PAGE_SEARCH_MAX_COUNT 100

// ****************************************************************************
// OOS Bestspace — cache data structures
// ****************************************************************************

typedef struct oos_stats_entry OOS_STATS_ENTRY;
struct oos_stats_entry
{
  VFID vfid;
  OOS_BESTSPACE best;
  OOS_STATS_ENTRY *next;
};

typedef struct oos_stats_bestspace_cache OOS_STATS_BESTSPACE_CACHE;
struct oos_stats_bestspace_cache
{
  int num_stats_entries;
  MHT_TABLE *vfid_ht;
  MHT_TABLE *vpid_ht;
  int free_list_count;
  OOS_STATS_ENTRY *free_list;
  pthread_mutex_t bestspace_mutex;
};

static OOS_STATS_BESTSPACE_CACHE oos_Bestspace_cache_area =
{ 0, NULL, NULL, 0, NULL, PTHREAD_MUTEX_INITIALIZER };
static OOS_STATS_BESTSPACE_CACHE *oos_Bestspace = NULL;

static const int oos_Find_best_page_limit = 100;

#if defined(CUBRID_UNIT_TEST_ENABLED)
#define OOS_DEBUG_COUNTER_FIELDS(OP) \
  OP (insert_many_calls) \
  OP (insert_many_requests) \
  OP (single_page_batch_count) \
  OP (insert_reused_pages) \
  OP (insert_fresh_pages) \
  OP (insert_values_per_fixed_page) \
  OP (read_many_calls) \
  OP (read_many_requests) \
  OP (read_many_grouped_head_pages) \
  OP (read_values_per_fixed_page)

struct oos_debug_atomic_counters
{
#define OOS_DEBUG_COUNTER_DECLARE(field) std::atomic<unsigned long long> field;
  OOS_DEBUG_COUNTER_FIELDS (OOS_DEBUG_COUNTER_DECLARE)
#undef OOS_DEBUG_COUNTER_DECLARE
};

static oos_debug_atomic_counters oos_Debug_counters = { };
static std::atomic<int> oos_Test_fail_insert_many_after_publications { -1 };
static std::atomic<bool> oos_Test_throw_bad_alloc_on_next_oid_publication { false };
static std::atomic<int> oos_Test_reclaim_waiters { 0 };

#define OOS_COUNTER_ADD(field, value) \
  do \
    { \
      oos_Debug_counters.field.fetch_add ((unsigned long long) (value), std::memory_order_relaxed); \
    } \
  while (0)
#define OOS_COUNTER_INC(field) OOS_COUNTER_ADD (field, 1)
#else
#define OOS_COUNTER_ADD(field, value) do { } while (0)
#define OOS_COUNTER_INC(field) do { } while (0)
#endif

// ****************************************************************************
// OOS Bestspace — hash and compare functions
// ****************************************************************************

static unsigned int
oos_hash_vpid (const void *key_vpid, unsigned int htsize)
{
  const VPID *vpid = (const VPID *) key_vpid;
  return ((vpid->pageid | ((unsigned int) vpid->volid) << 24) % htsize);
}

static int
oos_compare_vpid (const void *key_vpid1, const void *key_vpid2)
{
  const VPID *vpid1 = (const VPID *) key_vpid1;
  const VPID *vpid2 = (const VPID *) key_vpid2;
  return VPID_EQ (vpid1, vpid2);
}

static unsigned int
oos_hash_vfid (const void *key_vfid, unsigned int htsize)
{
  const VFID *vfid = (const VFID *) key_vfid;
  return ((vfid->fileid | ((unsigned int) vfid->volid) << 24) % htsize);
}

static int
oos_compare_vfid (const void *key_vfid1, const void *key_vfid2)
{
  const VFID *vfid1 = (const VFID *) key_vfid1;
  const VFID *vfid2 = (const VFID *) key_vfid2;
  return VFID_EQ (vfid1, vfid2);
}

// ****************************************************************************
// OOS Bestspace — entry management
// ****************************************************************************

static int
oos_stats_entry_free (THREAD_ENTRY *thread_p, void *data, void *args)
{
  OOS_STATS_ENTRY *ent = (OOS_STATS_ENTRY *) data;

  if (ent != NULL)
    {
      if (oos_Bestspace->free_list_count < OOS_BESTSPACE_CACHE_CAPACITY)
	{
	  ent->next = oos_Bestspace->free_list;
	  oos_Bestspace->free_list = ent;
	  oos_Bestspace->free_list_count++;
	}
      else
	{
	  free_and_init (ent);
	}
    }
  return NO_ERROR;
}

// ****************************************************************************
// OOS Bestspace — initialize / finalize
// ****************************************************************************

int
oos_bestspace_initialize (void)
{
  if (oos_Bestspace != NULL)
    {
      assert (false);
      (void) oos_bestspace_finalize ();
    }

  oos_Bestspace = &oos_Bestspace_cache_area;

  if (pthread_mutex_init (&oos_Bestspace->bestspace_mutex, NULL) != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_MUTEX_INIT, 0);
      return ER_CSS_PTHREAD_MUTEX_INIT;
    }

  oos_Bestspace->num_stats_entries = 0;
  oos_Bestspace->free_list_count = 0;
  oos_Bestspace->free_list = NULL;

  oos_Bestspace->vpid_ht = mht_create ("OOS best-space vpid hash table",
				       OOS_BESTSPACE_CACHE_CAPACITY,
				       oos_hash_vpid, oos_compare_vpid);
  if (oos_Bestspace->vpid_ht == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (MHT_TABLE));
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  oos_Bestspace->vfid_ht = mht_create ("OOS best-space vfid hash table",
				       OOS_BESTSPACE_CACHE_CAPACITY,
				       oos_hash_vfid, oos_compare_vfid);
  if (oos_Bestspace->vfid_ht == NULL)
    {
      mht_destroy (oos_Bestspace->vpid_ht);
      oos_Bestspace->vpid_ht = NULL;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (MHT_TABLE));
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  return NO_ERROR;
}

int
oos_bestspace_finalize (void)
{
  if (oos_Bestspace == NULL)
    {
      return NO_ERROR;
    }

  if (oos_Bestspace->vpid_ht != NULL)
    {
      (void) mht_map_no_key (NULL, oos_Bestspace->vpid_ht, oos_stats_entry_free, NULL);
      mht_destroy (oos_Bestspace->vpid_ht);
      oos_Bestspace->vpid_ht = NULL;
    }

  if (oos_Bestspace->vfid_ht != NULL)
    {
      mht_destroy (oos_Bestspace->vfid_ht);
      oos_Bestspace->vfid_ht = NULL;
    }

  /* Free the free list */
  OOS_STATS_ENTRY *ent = oos_Bestspace->free_list;
  while (ent != NULL)
    {
      OOS_STATS_ENTRY *next = ent->next;
      free_and_init (ent);
      ent = next;
    }
  oos_Bestspace->free_list = NULL;
  oos_Bestspace->free_list_count = 0;
  oos_Bestspace->num_stats_entries = 0;

  (void) pthread_mutex_lock (&oos_Bestspace->bestspace_mutex);
  oos_reclaim_clear_all_states ();
  pthread_mutex_unlock (&oos_Bestspace->bestspace_mutex);

  pthread_mutex_destroy (&oos_Bestspace->bestspace_mutex);
  oos_Bestspace = NULL;

  return NO_ERROR;
}

// ****************************************************************************
// OOS Bestspace — add / delete cache entries
// ****************************************************************************

static OOS_STATS_ENTRY *
oos_stats_add_bestspace (THREAD_ENTRY *thread_p, const VFID *vfid, VPID *vpid, int freespace)
{
  OOS_STATS_ENTRY *ent;

  assert (oos_Bestspace != NULL);

  (void) pthread_mutex_lock (&oos_Bestspace->bestspace_mutex);

  ent = (OOS_STATS_ENTRY *) mht_get (oos_Bestspace->vpid_ht, vpid);
  if (ent != NULL)
    {
      ent->best.freespace = freespace;
      pthread_mutex_unlock (&oos_Bestspace->bestspace_mutex);
      return ent;
    }

  if (oos_Bestspace->num_stats_entries >= OOS_BESTSPACE_CACHE_CAPACITY)
    {
      pthread_mutex_unlock (&oos_Bestspace->bestspace_mutex);
      return NULL;
    }

  if (oos_Bestspace->free_list_count > 0 && oos_Bestspace->free_list != NULL)
    {
      ent = oos_Bestspace->free_list;
      oos_Bestspace->free_list = ent->next;
      oos_Bestspace->free_list_count--;
    }
  else
    {
      ent = (OOS_STATS_ENTRY *) malloc (sizeof (OOS_STATS_ENTRY));
      if (ent == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (OOS_STATS_ENTRY));
	  pthread_mutex_unlock (&oos_Bestspace->bestspace_mutex);
	  return NULL;
	}
    }

  VFID_COPY (&ent->vfid, vfid);
  ent->best.vpid = *vpid;
  ent->best.freespace = freespace;
  ent->next = NULL;

  if (mht_put (oos_Bestspace->vpid_ht, &ent->best.vpid, ent) == NULL)
    {
      (void) oos_stats_entry_free (thread_p, ent, NULL);
      pthread_mutex_unlock (&oos_Bestspace->bestspace_mutex);
      return NULL;
    }

  if (mht_put_new (oos_Bestspace->vfid_ht, &ent->vfid, ent) == NULL)
    {
      (void) mht_rem (oos_Bestspace->vpid_ht, &ent->best.vpid, NULL, NULL);
      (void) oos_stats_entry_free (thread_p, ent, NULL);
      pthread_mutex_unlock (&oos_Bestspace->bestspace_mutex);
      return NULL;
    }

  oos_Bestspace->num_stats_entries++;
  pthread_mutex_unlock (&oos_Bestspace->bestspace_mutex);

  return ent;
}

static int
oos_stats_del_bestspace_by_vpid (THREAD_ENTRY *thread_p, VPID *vpid,
				 bool rearm_growth_sweep = false, const LOG_LSA *rollback_delete_lsa = NULL)
{
  OOS_STATS_ENTRY *ent;

  assert (oos_Bestspace != NULL);

  (void) pthread_mutex_lock (&oos_Bestspace->bestspace_mutex);

  ent = (OOS_STATS_ENTRY *) mht_get (oos_Bestspace->vpid_ht, vpid);
  if (rearm_growth_sweep)
    {
      oos_reclaim_note_recovery_delete_locked (ent == NULL ? NULL : &ent->vfid, rollback_delete_lsa);
    }
  if (ent == NULL)
    {
      pthread_mutex_unlock (&oos_Bestspace->bestspace_mutex);
      return NO_ERROR;
    }

  (void) mht_rem2 (oos_Bestspace->vfid_ht, &ent->vfid, ent, NULL, NULL);
  (void) mht_rem (oos_Bestspace->vpid_ht, vpid, NULL, NULL);
  (void) oos_stats_entry_free (thread_p, ent, NULL);
  oos_Bestspace->num_stats_entries--;

  pthread_mutex_unlock (&oos_Bestspace->bestspace_mutex);

  return NO_ERROR;
}

static int
oos_stats_del_bestspace_by_vfid (THREAD_ENTRY *thread_p, const VFID *vfid)
{
  OOS_STATS_ENTRY *ent;

  assert (oos_Bestspace != NULL);

  (void) pthread_mutex_lock (&oos_Bestspace->bestspace_mutex);

  while (true)
    {
      ent = (OOS_STATS_ENTRY *) mht_get (oos_Bestspace->vfid_ht, vfid);
      if (ent == NULL)
	{
	  break;
	}

      (void) mht_rem (oos_Bestspace->vpid_ht, &ent->best.vpid, NULL, NULL);
      (void) mht_rem2 (oos_Bestspace->vfid_ht, vfid, ent, NULL, NULL);
      (void) oos_stats_entry_free (thread_p, ent, NULL);
      oos_Bestspace->num_stats_entries--;
    }

  pthread_mutex_unlock (&oos_Bestspace->bestspace_mutex);

  return NO_ERROR;
}

// ****************************************************************************
// OOS Bestspace — header page helpers
// ****************************************************************************

static OOS_HDR_STATS *
oos_get_header_stats_ptr (THREAD_ENTRY *thread_p, PAGE_PTR page_header)
{
  RECDES recdes;
  if (spage_get_record (thread_p, page_header, 0, &recdes, PEEK) != S_SUCCESS)
    {
      assert (false);
      return NULL;
    }
  return (OOS_HDR_STATS *) recdes.data;
}

// ****************************************************************************
// OOS Bestspace — second_best helpers
// ****************************************************************************

static void
oos_stats_put_second_best (OOS_HDR_STATS *oos_hdr, VPID *vpid)
{
  if (++ (oos_hdr->estimates.num_substitutions) % 1000 == 0)
    {
      int tail = oos_hdr->estimates.tail_second_best;
      oos_hdr->estimates.second_best[tail] = *vpid;
      oos_hdr->estimates.tail_second_best = OOS_STATS_NEXT_BEST_INDEX (tail);

      if (oos_hdr->estimates.num_second_best < OOS_NUM_BEST_SPACESTATS)
	{
	  oos_hdr->estimates.num_second_best++;
	}
      else
	{
	  /* Overwrite head */
	  oos_hdr->estimates.head_second_best =
		  OOS_STATS_NEXT_BEST_INDEX (oos_hdr->estimates.head_second_best);
	}
    }
}

static bool
oos_stats_get_second_best (OOS_HDR_STATS *oos_hdr, VPID *vpid)
{
  if (oos_hdr->estimates.num_second_best <= 0)
    {
      VPID_SET_NULL (vpid);
      return false;
    }

  int head = oos_hdr->estimates.head_second_best;
  *vpid = oos_hdr->estimates.second_best[head];
  oos_hdr->estimates.head_second_best = OOS_STATS_NEXT_BEST_INDEX (head);
  oos_hdr->estimates.num_second_best--;

  return true;
}

// ****************************************************************************
// OOS Bestspace — find page in bestspace (hash + best[])
// ****************************************************************************

static void
oos_stats_evict_stale_hint (THREAD_ENTRY *thread_p, VPID *candidate_vpid, bool found_in_hash,
			    OOS_BESTSPACE *bestspace, int best_array_index)
{
  if (found_in_hash)
    {
      (void) oos_stats_del_bestspace_by_vpid (thread_p, candidate_vpid);
    }
  else if (bestspace != NULL && best_array_index >= 0 && best_array_index < OOS_NUM_BEST_SPACESTATS)
    {
      VPID_SET_NULL (&bestspace[best_array_index].vpid);
      bestspace[best_array_index].freespace = 0;
    }
}

static OOS_FINDSPACE
oos_stats_find_page_in_bestspace (THREAD_ENTRY *thread_p, const VFID *vfid,
				  OOS_BESTSPACE *bestspace, int *idx_badspace,
				  int needed_space, VPID *out_vpid,
				  PAGE_PTR *out_pgptr)
{
  OOS_FINDSPACE found = OOS_FINDSPACE_NOTFOUND;
  VPID candidate_vpid = VPID_INITIALIZER;
  int best_array_index = -1;
  int notfound_cnt = 0;
  int old_wait_msecs;
  VPID hdr_vpid;

  *out_pgptr = NULL;
  VPID_SET_NULL (out_vpid);

  /* Get the header page VPID to skip it */
  if (file_get_sticky_first_page (thread_p, vfid, &hdr_vpid) != NO_ERROR)
    {
      VPID_SET_NULL (&hdr_vpid);
    }

  /* Set zero-wait mode */
  old_wait_msecs = xlogtb_reset_wait_msecs (thread_p, LK_FORCE_ZERO_WAIT);

  while (found == OOS_FINDSPACE_NOTFOUND)
    {
      bool found_in_hash = false;
      VPID_SET_NULL (&candidate_vpid);

      /* Phase A: Search global hash table */
      if (oos_Bestspace != NULL)
	{
	  (void) pthread_mutex_lock (&oos_Bestspace->bestspace_mutex);

	  while (notfound_cnt < BEST_PAGE_SEARCH_MAX_COUNT)
	    {
	      OOS_STATS_ENTRY *ent;
	      ent = (OOS_STATS_ENTRY *) mht_get2 (oos_Bestspace->vfid_ht, vfid, NULL);
	      if (ent == NULL)
		{
		  break;
		}

	      if (ent->best.freespace >= needed_space)
		{
		  /* Skip header page */
		  if (!VPID_ISNULL (&hdr_vpid) && VPID_EQ (&ent->best.vpid, &hdr_vpid))
		    {
		      (void) mht_rem2 (oos_Bestspace->vfid_ht, vfid, ent, NULL, NULL);
		      (void) mht_rem (oos_Bestspace->vpid_ht, &ent->best.vpid, NULL, NULL);
		      (void) oos_stats_entry_free (thread_p, ent, NULL);
		      oos_Bestspace->num_stats_entries--;
		      continue;
		    }
		  candidate_vpid = ent->best.vpid;
		  found_in_hash = true;
		  break;
		}

	      /* Entry has insufficient space — remove it */
	      (void) mht_rem2 (oos_Bestspace->vfid_ht, vfid, ent, NULL, NULL);
	      (void) mht_rem (oos_Bestspace->vpid_ht, &ent->best.vpid, NULL, NULL);
	      (void) oos_stats_entry_free (thread_p, ent, NULL);
	      oos_Bestspace->num_stats_entries--;
	      notfound_cnt++;
	    }

	  pthread_mutex_unlock (&oos_Bestspace->bestspace_mutex);
	}

      /* Phase B: Search best[] array if hash yielded nothing */
      if (VPID_ISNULL (&candidate_vpid) && bestspace != NULL)
	{
	  best_array_index++;
	  for (; best_array_index < OOS_NUM_BEST_SPACESTATS; best_array_index++)
	    {
	      if (!VPID_ISNULL (&bestspace[best_array_index].vpid)
		  && bestspace[best_array_index].freespace >= needed_space)
		{
		  /* Skip header page */
		  if (!VPID_ISNULL (&hdr_vpid) && VPID_EQ (&bestspace[best_array_index].vpid, &hdr_vpid))
		    {
		      continue;
		    }
		  candidate_vpid = bestspace[best_array_index].vpid;
		  break;
		}
	    }
	}

      if (VPID_ISNULL (&candidate_vpid))
	{
	  break;
	}

      /* Phase C: Try to fix the candidate page with conditional latch. Hints can outlive the
       * page (reclaim deallocates), hence OLD_PAGE_MAYBE_DEALLOCATED. */
      *out_pgptr = pgbuf_fix (thread_p, &candidate_vpid, OLD_PAGE_MAYBE_DEALLOCATED,
			      PGBUF_LATCH_WRITE, PGBUF_CONDITIONAL_LATCH);
      if (*out_pgptr == NULL)
	{
	  int err = er_errid ();
	  if (err == ER_INTERRUPTED)
	    {
	      found = OOS_FINDSPACE_ERROR;
	      break;
	    }
	  if (err == ER_PB_BAD_PAGEID)
	    {
	      oos_trace ("stale bestspace hint to deallocated page vpid={vol=%d,page=%d} — evicting",
			 candidate_vpid.volid, candidate_vpid.pageid);
	      oos_stats_evict_stale_hint (thread_p, &candidate_vpid, found_in_hash, bestspace, best_array_index);
	      er_clear ();
	      notfound_cnt++;
	      continue;
	    }
	  /* Page is busy — skip it and try next; log unexpected errors for diagnostics */
	  if (err != NO_ERROR)
	    {
	      oos_trace ("conditional latch failed for vpid={vol=%d,page=%d}, er_errid=%d — skipping",
			 candidate_vpid.volid, candidate_vpid.pageid, err);
	      er_clear ();
	    }
	  notfound_cnt++;
	  continue;
	}

      /* A reclaimed page can be reallocated to another file (e.g. PAGE_FTAB); its fix then
       * succeeds and only the page type betrays the stale hint. Re-validate before using it. */
      if (pgbuf_get_page_ptype (thread_p, *out_pgptr) != PAGE_OOS)
	{
	  oos_trace ("stale bestspace hint to reallocated non-OOS page vpid={vol=%d,page=%d} — evicting",
		     candidate_vpid.volid, candidate_vpid.pageid);
	  pgbuf_unfix_and_init (thread_p, *out_pgptr);
	  oos_stats_evict_stale_hint (thread_p, &candidate_vpid, found_in_hash, bestspace, best_array_index);
	  notfound_cnt++;
	  continue;
	}

      /* Check actual free space */
      int actual_free = spage_max_space_for_new_record (thread_p, *out_pgptr);
      if (actual_free >= needed_space)
	{
	  *out_vpid = candidate_vpid;
	  found = OOS_FINDSPACE_FOUND;

	  /* Update cache with actual freespace */
	  if (found_in_hash)
	    {
	      (void) oos_stats_add_bestspace (thread_p, vfid, &candidate_vpid, actual_free);
	    }
	}
      else
	{
	  /* Not enough space — update hint and try next */
	  pgbuf_unfix_and_init (thread_p, *out_pgptr);
	  *out_pgptr = NULL;

	  if (found_in_hash)
	    {
	      (void) oos_stats_del_bestspace_by_vpid (thread_p, &candidate_vpid);
	    }
	  else if (best_array_index >= 0 && best_array_index < OOS_NUM_BEST_SPACESTATS)
	    {
	      /* best_array_index is meaningful only for a best[] candidate. */
	      bestspace[best_array_index].freespace = actual_free;
	    }
	  notfound_cnt++;
	}
    }

  /* Compute idx_badspace — the index in best[] with the worst (smallest) space */
  if (idx_badspace != NULL && bestspace != NULL)
    {
      int worst = INT_MAX;
      *idx_badspace = 0;
      for (int i = 0; i < OOS_NUM_BEST_SPACESTATS; i++)
	{
	  if (VPID_ISNULL (&bestspace[i].vpid))
	    {
	      *idx_badspace = i;
	      break;
	    }
	  if (bestspace[i].freespace < worst)
	    {
	      worst = bestspace[i].freespace;
	      *idx_badspace = i;
	    }
	}
    }

  /* Restore wait mode */
  (void) xlogtb_reset_wait_msecs (thread_p, old_wait_msecs);

  return found;
}

// ****************************************************************************
// OOS Bestspace — sync bestspace (scan pages to refill hints)
// ****************************************************************************

/*
 * oos_collect_data_page_vpids () - Collect the VPIDs of the OOS file's allocated data pages from
 *   its sector bitmaps.
 *
 *   The result is a frozen snapshot: pages may be deallocated or allocated while the caller
 *   iterates. Callers MUST fix sampled pages with OLD_PAGE_MAYBE_DEALLOCATED, skip
 *   ER_PB_BAD_PAGEID, and verify ptype == PAGE_OOS. The sticky first page IS included.
 */
static int
oos_collect_data_page_vpids (THREAD_ENTRY *thread_p, const VFID *vfid, std::vector<VPID> &vpids_out)
{
  FILE_FTAB_COLLECTOR collector = FILE_FTAB_COLLECTOR_INITIALIZER;

  int err = file_get_all_data_sectors (thread_p, vfid, &collector);
  if (err != NO_ERROR)
    {
      ASSERT_ERROR ();
      return err;
    }

  scope_exit collector_freer ([&] ()
  {
    if (collector.partsect_ftab != NULL)
      {
	db_private_free_and_init (thread_p, collector.partsect_ftab);
      }
  });

  try
    {
      vpids_out.reserve (vpids_out.size () + collector.npages);

      for (int i = 0; i < collector.nsects; i++)
	{
	  const FILE_PARTIAL_SECTOR &partsect = collector.partsect_ftab[i];
	  for (int bit = 0; bit < FILE_ALLOC_BITMAP_NBITS; bit++)
	    {
	      if ((partsect.page_bitmap & (((FILE_ALLOC_BITMAP) 1) << bit)) == 0)
		{
		  continue;
		}
	      VPID vpid;
	      vpid.volid = partsect.vsid.volid;
	      vpid.pageid = SECTOR_FIRST_PAGEID (partsect.vsid.sectid) + bit;
	      vpids_out.push_back (vpid);
	    }
	}
    }
  catch (std::bad_alloc &)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      (size_t) collector.npages * sizeof (VPID));
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  return NO_ERROR;
}

static int
oos_stats_sync_bestspace (THREAD_ENTRY *thread_p, const VFID *vfid,
			  OOS_HDR_STATS *oos_hdr, VPID *hdr_vpid,
			  bool scan_all)
{
  int num_high_best = 0;
  int num_other_high_best = 0;
  int num_pages = 0;
  int num_recs = 0;
  float recs_sumlen = 0.0;
  int max_iterations;
  int total_pages;
  int skipped_deallocated = 0;

  std::vector<VPID> scan_vpids;
  if (oos_collect_data_page_vpids (thread_p, vfid, scan_vpids) != NO_ERROR)
    {
      er_clear ();
      return 0;
    }

  total_pages = (int) scan_vpids.size ();
  if (total_pages <= 1)
    {
      return 0;
    }

  if (scan_all)
    {
      max_iterations = total_pages;
    }
  else
    {
      max_iterations = (int) (total_pages * 0.2);
      if (max_iterations < 10)
	{
	  max_iterations = 10;
	}
      if (max_iterations > oos_Find_best_page_limit)
	{
	  max_iterations = oos_Find_best_page_limit;
	}
    }

  int iterations = 0;
  int best_count = 0;

  /* Count existing valid best entries */
  for (int i = 0; i < OOS_NUM_BEST_SPACESTATS; i++)
    {
      if (!VPID_ISNULL (&oos_hdr->estimates.best[i].vpid))
	{
	  best_count++;
	}
    }

  for (int i = 0; i < total_pages && iterations < max_iterations; i++, iterations++)
    {
      VPID scan_vpid = scan_vpids[i];

      /* Skip header page (holds OOS_HDR_STATS, never a data page candidate) */
      if (!VPID_ISNULL (hdr_vpid) && VPID_EQ (&scan_vpid, hdr_vpid))
	{
	  continue;
	}

      PAGE_PTR page_ptr = pgbuf_fix (thread_p, &scan_vpid, OLD_PAGE_MAYBE_DEALLOCATED,
				     PGBUF_LATCH_READ, PGBUF_CONDITIONAL_LATCH);
      if (page_ptr == NULL)
	{
	  if (er_errid () == ER_PB_BAD_PAGEID)
	    {
	      skipped_deallocated++;
	    }
	  /* Otherwise the page is busy — skip */
	  er_clear ();
	  continue;
	}

      if (pgbuf_get_page_ptype (thread_p, page_ptr) != PAGE_OOS)
	{
	  pgbuf_unfix_and_init (thread_p, page_ptr);
	  continue;
	}

      int free_space = spage_max_space_for_new_record (thread_p, page_ptr);

      int page_npages = 0;
      int page_nrecs = 0;
      int page_recs_len = 0;
      spage_collect_statistics (page_ptr, &page_npages, &page_nrecs, &page_recs_len);

      pgbuf_unfix_and_init (thread_p, page_ptr);

      num_pages++;
      num_recs += page_nrecs;
      recs_sumlen += page_recs_len;

      if (free_space > OOS_DROP_FREE_SPACE)
	{
	  /* Add to global cache */
	  (void) oos_stats_add_bestspace (thread_p, vfid, &scan_vpid, free_space);

	  /* Add to best[] array if there's room */
	  if (best_count < OOS_NUM_BEST_SPACESTATS)
	    {
	      for (int j = 0; j < OOS_NUM_BEST_SPACESTATS; j++)
		{
		  if (VPID_ISNULL (&oos_hdr->estimates.best[j].vpid))
		    {
		      oos_hdr->estimates.best[j].vpid = scan_vpid;
		      oos_hdr->estimates.best[j].freespace = free_space;
		      best_count++;
		      num_high_best++;
		      break;
		    }
		}
	    }
	  else
	    {
	      num_other_high_best++;
	    }
	}

      /* estimates.full_search_vpid is no longer written; the field stays in OOS_HDR_STATS only
       * for on-disk layout stability. */
    }

  /* On full scan, clear stale best[] entries with freespace below the threshold.
   * This runs regardless of best_count so that full slots with stale low-freespace
   * values are cleaned up after a complete scan. */
  if (scan_all)
    {
      for (int i = 0; i < OOS_NUM_BEST_SPACESTATS; i++)
	{
	  if (!VPID_ISNULL (&oos_hdr->estimates.best[i].vpid)
	      && oos_hdr->estimates.best[i].freespace <= OOS_DROP_FREE_SPACE)
	    {
	      VPID_SET_NULL (&oos_hdr->estimates.best[i].vpid);
	      oos_hdr->estimates.best[i].freespace = 0;
	    }
	}
    }

  /* Update estimates */
  if (scan_all || oos_hdr->estimates.num_pages <= num_pages)
    {
      /* Full scan — reset all statistics */
      oos_hdr->estimates.num_other_high_best = num_other_high_best;
      oos_hdr->estimates.num_pages = num_pages;
      oos_hdr->estimates.num_recs = num_recs;
      oos_hdr->estimates.recs_sumlen = recs_sumlen;
    }
  else
    {
      /* Partial scan — preserve cumulative knowledge (heap pattern) */
      oos_hdr->estimates.num_other_high_best -= oos_hdr->estimates.num_high_best;

      if (oos_hdr->estimates.num_other_high_best < num_other_high_best)
	{
	  oos_hdr->estimates.num_other_high_best = num_other_high_best;
	}

      if (num_recs > oos_hdr->estimates.num_recs || recs_sumlen > oos_hdr->estimates.recs_sumlen)
	{
	  oos_hdr->estimates.num_pages = num_pages;
	  oos_hdr->estimates.num_recs = num_recs;
	  oos_hdr->estimates.recs_sumlen = recs_sumlen;
	}
    }
  oos_hdr->estimates.num_high_best = best_count;

  if (skipped_deallocated > 0)
    {
      oos_trace ("bestspace sync skipped %d concurrently deallocated page(s) in file %d|%d",
		 skipped_deallocated, VFID_AS_ARGS (vfid));
    }

  return num_high_best + num_other_high_best;
}

// ****************************************************************************
// OOS Bestspace — update stats after delete/insert
// ****************************************************************************

static void
oos_stats_update_internal (THREAD_ENTRY *thread_p, OOS_HDR_STATS *oos_hdr,
			   VPID *page_vpid, int freespace)
{
  int idx;

  /* Find worst entry in best[] to potentially replace */
  int worst_freespace = freespace;
  int worst_idx = -1;

  for (idx = 0; idx < OOS_NUM_BEST_SPACESTATS; idx++)
    {
      if (VPID_ISNULL (&oos_hdr->estimates.best[idx].vpid))
	{
	  worst_idx = idx;
	  break;
	}
      if (VPID_EQ (&oos_hdr->estimates.best[idx].vpid, page_vpid))
	{
	  /* Already in best[] — update freespace */
	  oos_hdr->estimates.best[idx].freespace = freespace;
	  return;
	}
      if (oos_hdr->estimates.best[idx].freespace < worst_freespace)
	{
	  worst_freespace = oos_hdr->estimates.best[idx].freespace;
	  worst_idx = idx;
	}
    }

  if (worst_idx >= 0)
    {
      /* Evict the worst entry to second_best if it has good space */
      if (!VPID_ISNULL (&oos_hdr->estimates.best[worst_idx].vpid)
	  && oos_hdr->estimates.best[worst_idx].freespace > OOS_DROP_FREE_SPACE)
	{
	  oos_stats_put_second_best (oos_hdr, &oos_hdr->estimates.best[worst_idx].vpid);
	  oos_hdr->estimates.num_other_high_best++;
	}

      oos_hdr->estimates.best[worst_idx].vpid = *page_vpid;
      oos_hdr->estimates.best[worst_idx].freespace = freespace;
    }
}

static void
oos_stats_update (THREAD_ENTRY *thread_p, PAGE_PTR pgptr, const VFID *vfid, int prev_freespace)
{
  int freespace;

  freespace = spage_get_free_space_without_saving (thread_p, pgptr);

  if (freespace > prev_freespace)
    {
      /* Space was freed — update global cache */
      VPID page_vpid;
      pgbuf_get_vpid (pgptr, &page_vpid);
      (void) oos_stats_add_bestspace (thread_p, vfid, &page_vpid, freespace);
    }

  if (freespace > OOS_DROP_FREE_SPACE)
    {
      /* Try to update header page best[] — conditional latch (zero wait) */
      VPID hdr_vpid;
      if (file_get_sticky_first_page (thread_p, vfid, &hdr_vpid) != NO_ERROR)
	{
	  return;
	}

      PAGE_PTR hdr_page = pgbuf_fix (thread_p, &hdr_vpid, OLD_PAGE,
				     PGBUF_LATCH_WRITE, PGBUF_CONDITIONAL_LATCH);
      if (hdr_page == NULL)
	{
	  /* Header is busy — defer update */
	  er_clear ();
	  return;
	}

      OOS_HDR_STATS *oos_hdr = oos_get_header_stats_ptr (thread_p, hdr_page);
      if (oos_hdr != NULL)
	{
	  VPID page_vpid;
	  pgbuf_get_vpid (pgptr, &page_vpid);
	  oos_stats_update_internal (thread_p, oos_hdr, &page_vpid, freespace);

	  /* Non-logged update — hints don't need WAL */
	  LOG_DATA_ADDR addr;
	  addr.vfid = NULL;
	  addr.pgptr = hdr_page;
	  addr.offset = 0;
	  log_skip_logging (thread_p, &addr);
	  pgbuf_set_dirty (thread_p, hdr_page, DONT_FREE);
	}

      pgbuf_unfix_and_init (thread_p, hdr_page);
    }
}

// ****************************************************************************
// OOS File operations
// ****************************************************************************

static int
oos_create_file_internal (THREAD_ENTRY *thread_p, FILE_DESCRIPTORS des, VFID &oos_vfid)
{
  int err = NO_ERROR;
  FILE_TABLESPACE tablespace;

  memset (&tablespace, 0, sizeof (FILE_TABLESPACE));

  tablespace.initial_size = DB_PAGESIZE;
  tablespace.expand_ratio = (float) 0.01;
  tablespace.expand_min_size = DISK_SECTOR_NPAGES * DB_PAGESIZE;
  tablespace.expand_max_size = DISK_SECTOR_NPAGES * DB_PAGESIZE * 1024;

  /* Empty-page reclaim requires a non-numerable OOS file. Enumeration uses the sector bitmap. */
  err = file_create (thread_p, FILE_OOS, &tablespace, &des,
		     false /* is_temp */, false /* is_numerable */, &oos_vfid);
  if (err != NO_ERROR)
    {
      oos_error ("file_create failed");
      assert_release_error (er_errid () != NO_ERROR);
      assert (false);
      return err;
    }

  /* Allocate sticky first page for header */
  VPID hdr_vpid;
  PAGE_PTR hdr_page = NULL;
  PAGE_TYPE page_type = PAGE_OOS;

  log_sysop_start (thread_p);

  err = file_alloc_sticky_first_page (thread_p, &oos_vfid, oos_vpid_init_new,
				      &page_type, &hdr_vpid, &hdr_page);
  if (err != NO_ERROR || hdr_page == NULL)
    {
      oos_error ("file_alloc_sticky_first_page failed");
      log_sysop_abort (thread_p);
      return (err != NO_ERROR) ? err : ER_FAILED;
    }

  /* Initialize header stats */
  OOS_HDR_STATS hdr_stats;
  memset (&hdr_stats, 0, sizeof (OOS_HDR_STATS));
  hdr_stats.oos_vfid = oos_vfid;
  VPID_SET_NULL (&hdr_stats.estimates.full_search_vpid);
  for (int i = 0; i < OOS_NUM_BEST_SPACESTATS; i++)
    {
      VPID_SET_NULL (&hdr_stats.estimates.best[i].vpid);
      VPID_SET_NULL (&hdr_stats.estimates.second_best[i]);
    }

  RECDES hdr_recdes;
  hdr_recdes.area_size = hdr_recdes.length = sizeof (OOS_HDR_STATS);
  hdr_recdes.type = REC_HOME;
  hdr_recdes.data = (char *) &hdr_stats;

  PGSLOTID slotid;
  int sp_status = spage_insert (thread_p, hdr_page, &hdr_recdes, &slotid);
  if (sp_status != SP_SUCCESS)
    {
      oos_error ("spage_insert for header failed with status %d", sp_status);
      pgbuf_unfix_and_init (thread_p, hdr_page);
      log_sysop_abort (thread_p);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return ER_GENERIC_ERROR;
    }
  assert (slotid == 0);

  /* Log the header record insertion. This is OOS file metadata, not a logical OOS value publication;
   * keep its RVOOS_INSERT LSA out of the value-publication queue. */
  const int tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  LOG_TDES *tdes = LOG_FIND_TDES (tran_index);
  const bool restore_oos_lsa_suppression = tdes != NULL && !tdes->oos_suppress_insert_lsa_queueing;
  if (restore_oos_lsa_suppression)
    {
      tdes->oos_suppress_insert_lsa_queueing = true;
    }
  scope_exit restore_oos_lsa_queueing ([&] () noexcept
  {
    if (restore_oos_lsa_suppression)
      {
	tdes->oos_suppress_insert_lsa_queueing = false;
      }
  });

  LOG_DATA_ADDR log_addr;
  log_addr.vfid = &oos_vfid;
  log_addr.pgptr = hdr_page;
  log_addr.offset = slotid;
  log_append_undoredo_recdes (thread_p, RVOOS_INSERT, &log_addr, NULL, &hdr_recdes);

  pgbuf_set_dirty (thread_p, hdr_page, FREE);

  log_sysop_commit (thread_p);

  oos_reclaim_note_file_created (oos_vfid);

  oos_trace ("created OOS file {fileid=%d, volid=%d} with header page {pageid=%d}",
	     oos_vfid.fileid, oos_vfid.volid, hdr_vpid.pageid);

  return NO_ERROR;
}

int
oos_create_file (THREAD_ENTRY *thread_p, const HFID &heap_hfid, const OID &class_oid, VFID &oos_vfid)
{
  FILE_DESCRIPTORS des;

  memset (&des, 0, sizeof (FILE_DESCRIPTORS));
  HFID_COPY (&des.heap_oos.hfid, &heap_hfid);
  COPY_OID (&des.heap_oos.class_oid, &class_oid);

  return oos_create_file_internal (thread_p, des, oos_vfid);
}

#if defined (CUBRID_UNIT_TEST_ENABLED)
int
oos_create_file (THREAD_ENTRY *thread_p, VFID &oos_vfid)
{
  const HFID test_owner_hfid = { { 1, 1 }, 1 };
  const OID test_owner_class_oid = { 1, 1, 1 };

  return oos_create_file (thread_p, test_owner_hfid, test_owner_class_oid, oos_vfid);
}
#endif /* CUBRID_UNIT_TEST_ENABLED */

int
oos_remove_file (THREAD_ENTRY *thread_p, const VFID &oos_vfid)
{
  /* Clean up bestspace cache entries for this file */
  (void) oos_stats_del_bestspace_by_vfid (thread_p, &oos_vfid);
  oos_reclaim_forget_file (oos_vfid);

  file_postpone_destroy (thread_p, &oos_vfid);

  return NO_ERROR;
}

// ****************************************************************************
// OOS empty-page reclaim
//
// INVARIANT: an OOS file reserves a new sector only when no safely reclaimable empty page
// exists right now. Two paths deliver it: the vacuum fast path (oos_reclaim_empty_pages)
// reclaims the pages a committed delete batch touched; the growth-gate sweep
// (oos_reclaim_sweep_step, run from the single growth point oos_alloc_page_with_reclaim) is
// the backstop that rediscovers the rest from the sector bitmap. Truth is on disk (sector
// bitmap + page emptiness); the in-memory counter and cursor are hints whose loss costs one
// lap, never a page.
//
// Bounded deferrals within the invariant: a page emptied by a still-active deleter is
// LSA-gated; a sweep failure is absorbed so reclaim never fails an INSERT. Idle files are never
// shrunk.
// ****************************************************************************

typedef enum
{
  OOS_RECLAIM_RECLAIMED = 0,	/* page deallocated back to the file manager */
  OOS_RECLAIM_DEFERRED,		/* empty, but LSA-gated from deallocation for now */
  OOS_RECLAIM_SKIPPED		/* busy, re-filled, already deallocated, or not an OOS data page */
} OOS_RECLAIM_RESULT;

// ****************************************************************************
// OOS empty-page reclaim — growth-gate sweep bookkeeping (per-VFID side map)
// ****************************************************************************

/* Per-file bookkeeping for the growth-gate sweep. Every field is an in-memory HINT — the truth
 * is on disk (sector bitmap + page emptiness), so losing an entry costs one boot-rule lap,
 * never a page. Written by the growth sweep only. */
struct oos_reclaim_file_state
{
  INT64 pending_deletes = 0;	/* deletes not yet accounted for by a completed sweep lap; > 0
				 * arms the growth gate (only the zero/non-zero verdict matters) */
  bool swept_this_boot = false;	/* a sweep lap has completed since boot (or the file was created
				 * this boot); while false, the boot rule forces a sweep on every
				 * growth regardless of the counter, absorbing hint loss */
  bool sweep_in_progress = false;	/* single-flight guard: at most one sweep per file at a time */
  VPID sweep_cursor = VPID_INITIALIZER;	/* the last page a sweep reclaimed; the next lap starts
					 * just after it */
};

struct oos_vfid_order
{
  bool operator () (const VFID &a, const VFID &b) const
  {
    if (a.volid != b.volid)
      {
	return a.volid < b.volid;
      }
    return a.fileid < b.fileid;
  }
};

/* Non-evicting by design (an evicted counter would silently disarm the growth gate); one entry
 * per live OOS file. Guarded by oos_Bestspace->bestspace_mutex. */
static std::map<VFID, oos_reclaim_file_state, oos_vfid_order> oos_Reclaim_states;
/* A single condition variable is sufficient: waiters always re-check their own VFID predicate
 * under bestspace_mutex, and a broadcast lets exactly one waiter per file claim the next step. */
static pthread_cond_t oos_Reclaim_sweep_cond = PTHREAD_COND_INITIALIZER;
/* The greatest rollback-delete CLR is enough: earlier CLR LSAs pass the ordinary strict gate
 * once the horizon reaches this one. */
static LOG_LSA oos_Last_rollback_delete_lsa = NULL_LSA;

/* Caller holds bestspace_mutex. */
static void
oos_reclaim_release_sweep_locked (const VFID &oos_vfid)
{
  auto it = oos_Reclaim_states.find (oos_vfid);
  if (it != oos_Reclaim_states.end ())
    {
      it->second.sweep_in_progress = false;
    }
  (void) pthread_cond_broadcast (&oos_Reclaim_sweep_cond);
}

/* Caller holds bestspace_mutex. */
static void
oos_reclaim_note_delete_locked (const VFID &oos_vfid)
{
  try
    {
      auto it = oos_Reclaim_states.find (oos_vfid);
      if (it != oos_Reclaim_states.end ())
	{
	  it->second.pending_deletes++;
	}
      else
	{
	  oos_reclaim_file_state state;
	  state.pending_deletes = 1;
	  oos_Reclaim_states.emplace (oos_vfid, state);
	}
    }
  catch (std::bad_alloc &)
    {
      /* Entry stays absent — the boot rule keeps the growth gate armed. */
    }
}

/* Recovery has no owning VFID: when it is unknown, conservatively re-arm every known file.
 * Caller holds bestspace_mutex. */
static void
oos_reclaim_note_recovery_delete_locked (const VFID *oos_vfid, const LOG_LSA *rollback_delete_lsa)
{
  if (rollback_delete_lsa != NULL
      && (LSA_ISNULL (&oos_Last_rollback_delete_lsa)
	  || LSA_LT (&oos_Last_rollback_delete_lsa, rollback_delete_lsa)))
    {
      oos_Last_rollback_delete_lsa = *rollback_delete_lsa;
    }

  if (oos_vfid != NULL)
    {
      oos_reclaim_note_delete_locked (*oos_vfid);
      return;
    }

  for (auto &state : oos_Reclaim_states)
    {
      state.second.pending_deletes++;
    }
}

static void
oos_reclaim_note_delete (const VFID &oos_vfid)
{
  assert (oos_Bestspace != NULL);

  (void) pthread_mutex_lock (&oos_Bestspace->bestspace_mutex);
  oos_reclaim_note_delete_locked (oos_vfid);
  pthread_mutex_unlock (&oos_Bestspace->bestspace_mutex);
}

/*
 * oos_reclaim_note_file_created () - Mark a file created THIS boot as already swept: it can hold
 *   no empty page from a previous boot, so its first growth skips the boot-rule lap.
 */
static void
oos_reclaim_note_file_created (const VFID &oos_vfid)
{
  assert (oos_Bestspace != NULL);

  (void) pthread_mutex_lock (&oos_Bestspace->bestspace_mutex);

  try
    {
      oos_reclaim_file_state state;
      state.swept_this_boot = true;
      oos_Reclaim_states[oos_vfid] = state;
    }
  catch (std::bad_alloc &)
    {
      /* Entry stays absent — the file's first growth pays one boot-rule lap. */
    }

  pthread_mutex_unlock (&oos_Bestspace->bestspace_mutex);
}

static void
oos_reclaim_forget_file (const VFID &oos_vfid)
{
  assert (oos_Bestspace != NULL);

  (void) pthread_mutex_lock (&oos_Bestspace->bestspace_mutex);
  oos_Reclaim_states.erase (oos_vfid);
  pthread_mutex_unlock (&oos_Bestspace->bestspace_mutex);
}

/* Caller must hold oos_Bestspace->bestspace_mutex. */
static void
oos_reclaim_clear_all_states (void)
{
  oos_Reclaim_states.clear ();
  LSA_SET_NULL (&oos_Last_rollback_delete_lsa);
}

/*
 * oos_reclaim_sample_horizon () - Sample the reclaim gate horizon: the smallest head LSA over
 *   every live undo source. Two scans, the same pairing recovery undo uses
 *   (logtb_rv_read_only_map_undo_tdes): active regular transactions (logtb_find_smallest_lsa)
 *   plus active system-worker sysops (log_system_tdes::map_all_tdes), because a vacuum worker's
 *   sysop abort also replays RVOOS_DELETE undo.
 *
 *   The result is clamped to the append LSA sampled BEFORE the scans: head_lsa assignment is
 *   not synchronized with them, so a transaction whose first append lands mid-scan can be
 *   missed. The clamp can only lower the horizon — toward deferring, never toward an unsafe
 *   dealloc.
 *
 *   Callers MUST sample once per reclaim call, before any page is examined, and reuse the
 *   sample for every candidate.
 */
static void
oos_reclaim_sample_horizon (THREAD_ENTRY *thread_p, LOG_LSA &horizon_out)
{
  const LOG_LSA append_lsa_at_sample = *log_get_append_lsa ();

  LSA_SET_NULL (&horizon_out);
  logtb_find_smallest_lsa (thread_p, &horizon_out);

  log_system_tdes::map_all_tdes ([&horizon_out] (log_tdes &tdes)
  {
    if (!LSA_ISNULL (&tdes.head_lsa) && (LSA_ISNULL (&horizon_out) || LSA_LT (&tdes.head_lsa, &horizon_out)))
      {
	horizon_out = tdes.head_lsa;
      }
  });

  if (LSA_ISNULL (&horizon_out) || LSA_LT (&append_lsa_at_sample, &horizon_out))
    {
      horizon_out = append_lsa_at_sample;
    }
}

/*
 * oos_reclaim_fix_candidate () - Zero-wait, dealloc-tolerant fix of a reclaim candidate.
 *   page_out == NULL with NO_ERROR means "skip" (busy, or already deallocated); ER_INTERRUPTED
 *   is propagated.
 */
static int
oos_reclaim_fix_candidate (THREAD_ENTRY *thread_p, const VPID &vpid, PGBUF_LATCH_MODE latch_mode,
			   PAGE_PTR &page_out)
{
  page_out = pgbuf_fix (thread_p, &vpid, OLD_PAGE_MAYBE_DEALLOCATED, latch_mode, PGBUF_CONDITIONAL_LATCH);
  if (page_out == NULL)
    {
      int err = er_errid ();
      if (err == ER_INTERRUPTED)
	{
	  return err;
	}
      er_clear ();
    }
  return NO_ERROR;
}

static bool
oos_page_is_reclaimable_empty (THREAD_ENTRY *thread_p, PAGE_PTR page_ptr)
{
  return pgbuf_get_page_ptype (thread_p, page_ptr) == PAGE_OOS && spage_number_of_records (page_ptr) == 0;
}

/* An INSERT rollback's CLR is redo-only, but its page LSA may equal the append horizon. Equality
 * is safe only after the live-writer horizon reaches that exact LSA; an abort still in progress
 * keeps the horizon at its earlier transaction head. */
static bool
oos_reclaim_lsa_gate_passes (const LOG_LSA &page_lsa, const LOG_LSA &horizon,
			     const LOG_LSA &rollback_delete_lsa)
{
  return LSA_LT (&page_lsa, &horizon)
	 || (!LSA_ISNULL (&rollback_delete_lsa) && LSA_EQ (&page_lsa, &horizon)
	     && LSA_EQ (&page_lsa, &rollback_delete_lsa));
}

/*
 * oos_try_reclaim_page_internal () - Try to return one fully emptied OOS data page to the file
 *   manager's partial sector table. Two-phase: phase 1 pre-qualifies under a zero-wait READ fix
 *   with the OOS stats header latch NOT held; phase 2 takes the header WRITE latch, re-validates
 *   under a zero-wait WRITE fix, and deallocates in its own immediately-committed sysop.
 *
 *   Idempotent and zero-wait: every "cannot reclaim right now" outcome — busy, already
 *   deallocated, re-filled — is OOS_RECLAIM_SKIPPED with NO_ERROR; an empty page whose last
 *   writer may still be active is OOS_RECLAIM_DEFERRED. The sticky-first-page invariant is the
 *   caller's job.
 *
 *   return: NO_ERROR or error code (ER_INTERRUPTED propagates so reclaim loops stop immediately)
 *   thread_p(in): thread entry
 *   oos_vfid(in): OOS file identifier
 *   vpid(in): candidate page
 *   hdr_vpid(in): the file's sticky first page (holds OOS_HDR_STATS), resolved once by the caller
 *   horizon(in): LSA gate horizon, sampled once by the caller BEFORE examining any page
 *   result(out): per-page verdict; meaningful only when NO_ERROR is returned
 *   rollback_delete_lsa(in): latest redo-only INSERT-rollback CLR, or NULL_LSA
 *
 * Safety argument (why the emptiness check cannot race a writer, and why undo can never reach a
 * deallocated page):
 *   1. The header page is WRITE-latched for all of phase 2, from the re-validation until after
 *      the deallocation. Every insert-side page discovery starts under that latch in
 *      oos_find_best_page, so no NEW writer can be handed this page while we hold it.
 *   2. A writer that already claimed the page keeps it continuously WRITE-latched until its
 *      write completes, so our conditional fix fails and we skip.
 *   3. file_dealloc's RVFL_DEALLOC postpone runs at log_sysop_commit, still under the header
 *      latch. The page must be unfixed before that commit (pgbuf_dealloc_page requires a single
 *      fixer); read-only sync sampling may slip into that gap, hence hint eviction after the
 *      commit and OLD_PAGE_MAYBE_DEALLOCATED on lookups.
 *   4. LSA gate: the page is deallocated only when its LSA is older than the horizon (or is the
 *      exact redo-only rollback CLR). Emptiness alone cannot tell "emptied by committed deletes"
 *      from "emptied by a still-active deleter", whose abort replays RVOOS_DELETE undo into the
 *      page. Every write of a live undo source carries a page LSA at or above that source's
 *      head LSA, so a page passing the gate cannot be an undo target of anyone still running.
 *
 * Why not the heap's OLD_PAGE_PREVENT_DEALLOC protocol: readers reach OOS chunk pages only via
 * the head OOS OIDs of heap record versions, and vacuum empties a chain's pages only after the
 * owning version is invisible to every active snapshot — no reader can legally be walking them.
 * The dealloc tolerance on lookups is defense in depth for stale hints, not a substitute for
 * pinning, and PREVENT_DEALLOC would reintroduce a waiting edge into a zero-wait path.
 */
static int
oos_try_reclaim_page_internal (THREAD_ENTRY *thread_p, const VFID &oos_vfid, const VPID &vpid,
			       const VPID &hdr_vpid, const LOG_LSA &horizon, OOS_RECLAIM_RESULT &result,
			       const LOG_LSA &rollback_delete_lsa)
{
  int err = NO_ERROR;

  result = OOS_RECLAIM_SKIPPED;

  if (VPID_EQ (&vpid, &hdr_vpid))
    {
      /* The sticky first page (OOS_HDR_STATS) must never be deallocated; file_dealloc only
       * asserts this in debug builds. */
      return NO_ERROR;
    }

  /* Phase 1: pre-qualify under a zero-wait READ fix, header latch NOT held. */
  PAGE_PTR page_ptr = NULL;
  err = oos_reclaim_fix_candidate (thread_p, vpid, PGBUF_LATCH_READ, page_ptr);
  if (err != NO_ERROR)
    {
      return err;
    }
  if (page_ptr == NULL)
    {
      return NO_ERROR;
    }

  bool is_empty = oos_page_is_reclaimable_empty (thread_p, page_ptr);
  bool lsa_gate_passes = is_empty
			 && oos_reclaim_lsa_gate_passes (*pgbuf_get_lsa (page_ptr), horizon, rollback_delete_lsa);
  pgbuf_unfix_and_init (thread_p, page_ptr);
  if (!is_empty)
    {
      return NO_ERROR;
    }
  if (!lsa_gate_passes)
    {
      result = OOS_RECLAIM_DEFERRED;
      return NO_ERROR;
    }

  /* Phase 2: serialize against insert-side page discovery via the stats header latch. */
  PAGE_PTR hdr_page = pgbuf_fix (thread_p, &hdr_vpid, OLD_PAGE,
				 PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (hdr_page == NULL)
    {
      ASSERT_ERROR_AND_SET (err);
      return err;
    }

  scope_exit hdr_unfixer ([&] ()
  {
    pgbuf_unfix_and_init_after_check (thread_p, hdr_page);
  });

  err = oos_reclaim_fix_candidate (thread_p, vpid, PGBUF_LATCH_WRITE, page_ptr);
  if (err != NO_ERROR)
    {
      return err;
    }
  if (page_ptr == NULL)
    {
      return NO_ERROR;
    }

  if (!oos_page_is_reclaimable_empty (thread_p, page_ptr))
    {
      /* The page changed in the unlatched window between the phases; phase 2's verdict is
       * binding. */
      pgbuf_unfix_and_init (thread_p, page_ptr);
      return NO_ERROR;
    }

  if (!oos_reclaim_lsa_gate_passes (*pgbuf_get_lsa (page_ptr), horizon, rollback_delete_lsa))
    {
      /* Binding LSA-gate verdict (safety item 4): the last writer may still be active. */
      pgbuf_unfix_and_init (thread_p, page_ptr);
      result = OOS_RECLAIM_DEFERRED;
      return NO_ERROR;
    }

  log_sysop_start (thread_p);

  err = file_dealloc (thread_p, &oos_vfid, &vpid, FILE_OOS);
  if (err != NO_ERROR)
    {
      oos_error ("file_dealloc failed for vpid={pageid=%d, volid=%d}", vpid.pageid, vpid.volid);
      pgbuf_unfix_and_init (thread_p, page_ptr);
      log_sysop_abort (thread_p);
      return err;
    }

  /* The RVFL_DEALLOC postpone registered above runs at this sysop commit, and its
   * pgbuf_dealloc_page asserts a single fixer — so unfix before committing. */
  pgbuf_unfix_and_init (thread_p, page_ptr);
  log_sysop_commit (thread_p);

  /* Evict hints only AFTER the dealloc commit: a concurrent sync sampling the page in the
   * unfix-to-commit gap could have re-added it. The header page is still latched. */
  (void) oos_stats_del_bestspace_by_vpid (thread_p, const_cast<VPID *> (&vpid));

  OOS_HDR_STATS *oos_hdr = oos_get_header_stats_ptr (thread_p, hdr_page);
  if (oos_hdr != NULL)
    {
      bool hdr_changed = false;
      for (int i = 0; i < OOS_NUM_BEST_SPACESTATS; i++)
	{
	  if (VPID_EQ (&oos_hdr->estimates.best[i].vpid, &vpid))
	    {
	      VPID_SET_NULL (&oos_hdr->estimates.best[i].vpid);
	      oos_hdr->estimates.best[i].freespace = 0;
	      hdr_changed = true;
	    }
	  if (VPID_EQ (&oos_hdr->estimates.second_best[i], &vpid))
	    {
	      VPID_SET_NULL (&oos_hdr->estimates.second_best[i]);
	      hdr_changed = true;
	    }
	}
      if (hdr_changed)
	{
	  /* Non-logged update — hints don't need WAL */
	  LOG_DATA_ADDR addr;
	  addr.vfid = NULL;
	  addr.pgptr = hdr_page;
	  addr.offset = 0;
	  log_skip_logging (thread_p, &addr);
	  pgbuf_set_dirty (thread_p, hdr_page, DONT_FREE);
	}
    }

  oos_trace ("reclaimed empty page {volid=%d, pageid=%d} in file %d|%d",
	     vpid.volid, vpid.pageid, VFID_AS_ARGS (&oos_vfid));

  result = OOS_RECLAIM_RECLAIMED;
  return NO_ERROR;
}

/* The lap sort and the cursor's upper_bound MUST use this same order. */
static bool
oos_vpid_lt (const VPID &a, const VPID &b)
{
  return VPID_LT (&a, &b);
}

/*
 * oos_reclaim_empty_pages () - Reclaim the fully emptied pages of an explicit candidate list
 *   (vacuum's fast path). Public contract in oos_file.hpp.
 *
 *   Call only AFTER the deletes that emptied the pages are committed: a live undo could
 *   otherwise restore chunks onto a deallocated page. Pages whose deleter is still active are
 *   deferred by the LSA gate, not deallocated.
 *
 *   return: NO_ERROR or error code
 *   thread_p(in): thread entry
 *   oos_vfid(in): OOS file identifier
 *   candidates(in/out): candidate pages, duplicates allowed; sorted and deduped in place
 */
int
oos_reclaim_empty_pages (THREAD_ENTRY *thread_p, const VFID &oos_vfid, std::vector<VPID> &candidates)
{
  int err = NO_ERROR;

  if (candidates.empty ())
    {
      return NO_ERROR;
    }

  VPID hdr_vpid;
  err = file_get_sticky_first_page (thread_p, &oos_vfid, &hdr_vpid);
  if (err != NO_ERROR)
    {
      ASSERT_ERROR ();
      return err;
    }

  /* Dedupe: a value chain usually deletes several chunks from the same page. */
  std::sort (candidates.begin (), candidates.end (), oos_vpid_lt);
  candidates.erase (std::unique (candidates.begin (), candidates.end (),
				 [] (const VPID &a, const VPID &b)
  {
    return VPID_EQ (&a, &b);
  }), candidates.end ());

  LOG_LSA horizon;
  oos_reclaim_sample_horizon (thread_p, horizon);

  int n_reclaimed = 0;
  int n_deferred = 0;
  for (const VPID &vpid : candidates)
    {
      OOS_RECLAIM_RESULT result = OOS_RECLAIM_SKIPPED;
      err = oos_try_reclaim_page_internal (thread_p, oos_vfid, vpid, hdr_vpid, horizon, result, NULL_LSA);
      if (err != NO_ERROR)
	{
	  return err;
	}
      if (result == OOS_RECLAIM_RECLAIMED)
	{
	  n_reclaimed++;
	}
      else if (result == OOS_RECLAIM_DEFERRED)
	{
	  n_deferred++;
	}
    }

  oos_trace ("reclaim batch on file %d|%d: %d reclaimed, %d deferred of %d candidate(s)",
	     VFID_AS_ARGS (&oos_vfid), n_reclaimed, n_deferred, (int) candidates.size ());

  return NO_ERROR;
}

/*
 * oos_reclaim_settle_lap () - Account a completed sweep lap. Subtract only the deletes counted
 *   at lap start: a delete noted mid-lap may have emptied a page the lap already passed, so its
 *   hint must survive. Deferrals stay pending so a later growth retries them.
 */
static void
oos_reclaim_settle_lap (const VFID &oos_vfid, INT64 pending_at_start, INT64 n_deferred)
{
  (void) pthread_mutex_lock (&oos_Bestspace->bestspace_mutex);
  auto it = oos_Reclaim_states.find (oos_vfid);
  if (it != oos_Reclaim_states.end ())
    {
      it->second.pending_deletes -= pending_at_start;
      if (it->second.pending_deletes < 0)
	{
	  assert (false);
	  it->second.pending_deletes = 0;
	}
      it->second.pending_deletes += n_deferred;
      it->second.swept_this_boot = true;
    }
  pthread_mutex_unlock (&oos_Bestspace->bestspace_mutex);
}

/*
 * oos_reclaim_sweep_step () - The growth-gate incremental sweep, run on every OOS page
 *   allocation. If the side map says work is due (pending deletes, or the boot rule: no entry /
 *   no lap completed since boot), walk the file's data pages from the saved cursor and stop at
 *   the FIRST page reclaimed. That page is back in the partial sector table when this returns,
 *   so the caller's file_alloc reuses it instead of growing.
 *
 *   Concurrency: at most one sweep per file (single-flight flag). A concurrent grower waits for
 *   the active step, then re-checks the state and claims the next step when work remains. It
 *   cannot bypass the growth gate while the active sweep may still find a reclaimable page.
 *
 *   return: NO_ERROR or error code. ER_INTERRUPTED propagates; on any error the claimed
 *   bookkeeping is released untouched, so the next growth retries the same region.
 */
static int
oos_reclaim_sweep_step (THREAD_ENTRY *thread_p, const VFID &oos_vfid)
{
  int err = NO_ERROR;

  INT64 pending_at_start = 0;
  VPID cursor;
  VPID_SET_NULL (&cursor);
  LOG_LSA rollback_delete_lsa;
  LSA_SET_NULL (&rollback_delete_lsa);
  bool claimed = false;

  assert (oos_Bestspace != NULL);
  (void) pthread_mutex_lock (&oos_Bestspace->bestspace_mutex);
  try
    {
      while (true)
	{
	  auto it = oos_Reclaim_states.find (oos_vfid);
	  if (it == oos_Reclaim_states.end ())
	    {
	      /* Boot rule: an unknown file may hold empty pages from a previous boot. */
	      oos_reclaim_file_state state;
	      state.sweep_in_progress = true;
	      oos_Reclaim_states.emplace (oos_vfid, state);
	      claimed = true;
	      break;
	    }
	  if (it->second.sweep_in_progress)
	    {
#if defined(CUBRID_UNIT_TEST_ENABLED)
	      oos_Test_reclaim_waiters.fetch_add (1, std::memory_order_relaxed);
#endif
	      int wait_error = pthread_cond_wait (&oos_Reclaim_sweep_cond,
						  &oos_Bestspace->bestspace_mutex);
#if defined(CUBRID_UNIT_TEST_ENABLED)
	      oos_Test_reclaim_waiters.fetch_sub (1, std::memory_order_relaxed);
#endif
	      if (wait_error != 0)
		{
		  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_COND_WAIT, 0);
		  err = ER_CSS_PTHREAD_COND_WAIT;
		  break;
		}
	      continue;
	    }
	  if (it->second.pending_deletes > 0 || !it->second.swept_this_boot)
	    {
	      it->second.sweep_in_progress = true;
	      pending_at_start = it->second.pending_deletes;
	      cursor = it->second.sweep_cursor;
	      claimed = true;
	    }
	  break;
	}
      if (claimed)
	{
	  rollback_delete_lsa = oos_Last_rollback_delete_lsa;
	}
    }
  catch (std::bad_alloc &)
    {
      /* Entry not created — the boot rule keeps the gate armed for the next growth. */
    }
  pthread_mutex_unlock (&oos_Bestspace->bestspace_mutex);

  if (err != NO_ERROR)
    {
      return err;
    }
  if (!claimed)
    {
      return NO_ERROR;
    }

  scope_exit sweep_release ([&] () noexcept
  {
    (void) pthread_mutex_lock (&oos_Bestspace->bestspace_mutex);
    oos_reclaim_release_sweep_locked (oos_vfid);
    pthread_mutex_unlock (&oos_Bestspace->bestspace_mutex);
  });

  VPID hdr_vpid;
  err = file_get_sticky_first_page (thread_p, &oos_vfid, &hdr_vpid);
  if (err != NO_ERROR)
    {
      ASSERT_ERROR ();
      return err;
    }

  std::vector<VPID> pages;
  err = oos_collect_data_page_vpids (thread_p, &oos_vfid, pages);
  if (err != NO_ERROR)
    {
      ASSERT_ERROR ();
      return err;
    }
  std::sort (pages.begin (), pages.end (), oos_vpid_lt);

  std::size_t start = 0;
  if (!VPID_ISNULL (&cursor) && !pages.empty ())
    {
      auto after_cursor = std::upper_bound (pages.begin (), pages.end (), cursor, oos_vpid_lt);
      start = (after_cursor == pages.end ()) ? 0 : (std::size_t) (after_cursor - pages.begin ());
    }

  LOG_LSA horizon;
  oos_reclaim_sample_horizon (thread_p, horizon);

  INT64 n_deferred = 0;
  for (std::size_t i = 0; i < pages.size (); i++)
    {
      const VPID &vpid = pages[ (start + i) % pages.size ()];

      OOS_RECLAIM_RESULT result = OOS_RECLAIM_SKIPPED;
      err = oos_try_reclaim_page_internal (thread_p, oos_vfid, vpid, hdr_vpid, horizon, result,
					   rollback_delete_lsa);
      if (err != NO_ERROR)
	{
	  return err;
	}
      if (result == OOS_RECLAIM_RECLAIMED)
	{
	  (void) pthread_mutex_lock (&oos_Bestspace->bestspace_mutex);
	  auto it = oos_Reclaim_states.find (oos_vfid);
	  if (it != oos_Reclaim_states.end ())
	    {
	      it->second.sweep_cursor = vpid;
	    }
	  pthread_mutex_unlock (&oos_Bestspace->bestspace_mutex);

	  oos_trace ("growth-gate sweep on file %d|%d: reclaimed page {volid=%d, pageid=%d}, "
		     "%lld deferred so far", VFID_AS_ARGS (&oos_vfid), vpid.volid, vpid.pageid,
		     (long long) n_deferred);
	  return NO_ERROR;
	}
      if (result == OOS_RECLAIM_DEFERRED)
	{
	  n_deferred++;
	}
    }

  oos_reclaim_settle_lap (oos_vfid, pending_at_start, n_deferred);

  oos_trace ("growth-gate sweep on file %d|%d: full lap over %d page(s), 0 reclaimed, %lld deferred",
	     VFID_AS_ARGS (&oos_vfid), (int) pages.size (), (long long) n_deferred);
  return NO_ERROR;
}

/*
 * oos_alloc_page_with_reclaim () - The single growth point of an OOS file.
 *
 *   INVARIANT: every new-page allocation for an OOS file goes through this helper, never
 *   through oos_file_alloc_new directly. The growth-gate sweep runs first; a page it frees is
 *   back in the partial sector table before file_alloc runs, so no retry loop is needed.
 *
 *   PRECONDITION: call with no OOS page latched — the sweep's reclaim primitive takes the OOS
 *   stats header WRITE latch unconditionally.
 *
 *   An interrupt from the sweep propagates; any other sweep failure is absorbed, because a
 *   space hint must never turn a healthy INSERT into an error.
 */
static auto_unfix_page_ptr
oos_alloc_page_with_reclaim (THREAD_ENTRY *thread_p, const VFID &oos_vfid, VPID &vpid_out)
{
  int err = oos_reclaim_sweep_step (thread_p, oos_vfid);
  if (err == ER_INTERRUPTED)
    {
      return nullptr;
    }
  if (err != NO_ERROR)
    {
      oos_warn ("growth-gate sweep failed with error %d on file %d|%d; allocating anyway "
		"(unreclaimed pages stay discoverable on disk)", err, VFID_AS_ARGS (&oos_vfid));
      er_clear ();
    }

  return oos_file_alloc_new (thread_p, oos_vfid, vpid_out);
}


static int
oos_prepend_header (oos_buffer src, const OOS_RECORD_HEADER &oos_header, OOS_RECDES &oos_recdes)
{
  // Prepends the OOS header to user data, producing the on-page record.
  // Allocates a new data area for oos_recdes; caller must free it.

  const int src_len = static_cast<int> (src.size ());
  int err;
  err = recdes_allocate_data_area (&oos_recdes, src_len + OOS_RECORD_HEADER_SIZE);
  if (err != NO_ERROR)
    {
      oos_error ("recdes_allocate_data_area failed in oos_prepend_header");
      assert_release_error (er_errid () != NO_ERROR);
      assert (false);
      return err;
    }

  oos_recdes.type = REC_HOME;
  oos_recdes.length = src_len + OOS_RECORD_HEADER_SIZE;
  std::memcpy (oos_recdes.data, &oos_header, OOS_RECORD_HEADER_SIZE);
  std::memcpy (oos_recdes.data + OOS_RECORD_HEADER_SIZE, src.data (), src.size ());

  return NO_ERROR;
}

static void
oos_publish_oos_oid (THREAD_ENTRY *thread_p, const OID &oid)
{
#if defined(CUBRID_UNIT_TEST_ENABLED)
  if (oos_Test_throw_bad_alloc_on_next_oid_publication.exchange (false, std::memory_order_relaxed))
    {
      throw std::bad_alloc ();
    }
#endif
  thread_p->oos_oids.push_back (oid);
}

static void
oos_cleanup_insert_publication_state_on_error (THREAD_ENTRY *thread_p) noexcept
{
  thread_p->oos_oids.clear ();

  const int tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  LOG_TDES *tdes = LOG_FIND_TDES (tran_index);
  if (tdes != NULL)
    {
      tdes->oos_insert_lsa_queue.clear ();
    }
}

int
oos_insert (THREAD_ENTRY *thread_p, const VFID &oos_vfid, oos_buffer src, OID &oid)
{
  oos_debug ("arguments: oos_vfid={fileid=%d, volid=%d}, src.size=%zu",
	     oos_vfid.fileid, oos_vfid.volid, src.size ());
  int err = NO_ERROR;

  /* Guards the narrowing cast below against wrap-around from a corrupt caller. */
  if (src.data () == nullptr || src.size () == 0 || src.size () > (std::size_t) INT_MAX)
    {
      oos_error ("oos_insert rejected invalid src (data=%p, size=%zu)", src.data (), src.size ());
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HEAP_OOS_INVALID_ARGUMENT, 0);
      return ER_HEAP_OOS_INVALID_ARGUMENT;
    }

  const int src_len = static_cast<int> (src.size ());
  scope_exit cleanup_publication_on_error ([&] () noexcept
  {
    oos_cleanup_insert_publication_state_on_error (thread_p);
  });

  // TODO: Once the OOS_RECORD_HEADER spec is finalized (first segment header and rest segment header),
  // review whether it is possible to generate the segment headers inside the oos_insert_within_page() and
  // oos_insert_across_pages() functions.

  try
    {
      if (src_len <= oos_get_max_chunk_size_within_page ())
	{
	  const OOS_RECORD_HEADER header{src_len, 0, OID_INITIALIZER};
	  err = oos_insert_within_page (thread_p, oos_vfid, src, header, oid);
	}
      else
	{
	  err = oos_insert_across_pages (thread_p, oos_vfid, src, oid);
	}

      if (err != NO_ERROR)
	{
	  return err;
	}

      oos_publish_oos_oid (thread_p, oid);
    }
  catch (const std::bad_alloc &)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      MAX (sizeof (OID), sizeof (LOG_LSA)));
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  cleanup_publication_on_error.release ();
  oos_debug ("inserted to oid={vol=%d,page=%d,slot=%d}", OID_AS_ARGS (&oid));
  return NO_ERROR;
}

static int
oos_insert_single_page_batch (THREAD_ENTRY *thread_p, const VFID &oos_vfid,
			      cubbase::span<oos_insert_request> requests,
			      int needed_space)
{
  int err = NO_ERROR;
  VPID vpid;

  auto auto_page_ptr = oos_find_best_page (thread_p, oos_vfid, needed_space, vpid);
  if (auto_page_ptr == nullptr)
    {
      ASSERT_ERROR_AND_SET (err);
      return err;
    }

  PAGE_PTR page_ptr = auto_page_ptr.get ();
  /* Freshly allocated pages and fully emptied reused pages both give the batch a clean page. */
  const bool page_was_empty = (spage_number_of_records (page_ptr) == 0);

  for (std::size_t i = 0; i < requests.size (); i++)
    {
      oos_insert_request &request = requests[i];
      const int src_len = static_cast<int> (request.src.size ());
      const OOS_RECORD_HEADER header{src_len, 0, OID_INITIALIZER};
      OID oid;

      err = oos_insert_record_in_fixed_page (thread_p, oos_vfid, page_ptr, vpid, request.src, header, oid);
      if (err != NO_ERROR)
	{
	  return err;
	}

      *request.oid_out = oid;
      oos_publish_oos_oid (thread_p, oid);
    }

  int freespace_after = spage_max_space_for_new_record (thread_p, page_ptr);
  (void) oos_stats_add_bestspace (thread_p, &oos_vfid, &vpid, freespace_after);

  OOS_COUNTER_INC (single_page_batch_count);
  OOS_COUNTER_ADD (insert_values_per_fixed_page, requests.size ());
  if (page_was_empty)
    {
      OOS_COUNTER_INC (insert_fresh_pages);
    }
  else
    {
      OOS_COUNTER_INC (insert_reused_pages);
    }

  return NO_ERROR;
}

int
oos_insert_many (THREAD_ENTRY *thread_p, const VFID &oos_vfid, cubbase::span<oos_insert_request> requests)
{
  OOS_COUNTER_INC (insert_many_calls);
  OOS_COUNTER_ADD (insert_many_requests, requests.size ());

  for (std::size_t i = 0; i < requests.size (); i++)
    {
      if (requests[i].src.data () == nullptr || requests[i].src.size () == 0
	  || requests[i].src.size () > (std::size_t) INT_MAX || requests[i].oid_out == NULL)
	{
	  oos_error ("oos_insert_many rejected invalid request %zu (data=%p, size=%zu, oid_out=%p)",
		     i, requests[i].src.data (), requests[i].src.size (), (void *) requests[i].oid_out);
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  return ER_GENERIC_ERROR;
	}
    }

  scope_exit cleanup_publication_on_error ([&] () noexcept
  {
    oos_cleanup_insert_publication_state_on_error (thread_p);
  });

  try
    {
      const int max_chunk_size = oos_get_max_chunk_size_within_page ();
      const int page_capacity = DB_ALIGN_BELOW (spage_max_record_size (), OOS_ALIGNMENT);
      const auto required_space = [] (const oos_insert_request &request)
      {
	return DB_ALIGN (static_cast<int> (request.src.size ()) + OOS_RECORD_HEADER_SIZE, OOS_ALIGNMENT);
      };

      std::size_t pos = 0;
      std::size_t publication_count = 0;
      while (pos < requests.size ())
	{
	  int err;

#if defined(CUBRID_UNIT_TEST_ENABLED)
	  int fail_after = oos_Test_fail_insert_many_after_publications.load (std::memory_order_relaxed);
	  if (fail_after >= 0 && publication_count >= (std::size_t) fail_after
	      && oos_Test_fail_insert_many_after_publications.compare_exchange_strong (
		      fail_after, -1, std::memory_order_relaxed))
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	      return ER_GENERIC_ERROR;
	    }
#endif

	  if (requests[pos].src.size () > (std::size_t) max_chunk_size)
	    {
	      OID oid;
	      err = oos_insert_across_pages (thread_p, oos_vfid, requests[pos].src, oid);
	      if (err == NO_ERROR)
		{
		  *requests[pos].oid_out = oid;
		  oos_publish_oos_oid (thread_p, oid);
		  pos++;
		  publication_count++;
		}
	    }
	  else
	    {
	      /* Greedy batch: extend while the next single-chunk request still fits the same page. */
	      int needed_space = required_space (requests[pos]);
	      std::size_t batch_end = pos + 1;

	      assert (needed_space <= page_capacity);
	      while (batch_end < requests.size ()
		     && requests[batch_end].src.size () <= (std::size_t) max_chunk_size
		     && needed_space + (int) SPAGE_SLOT_SIZE + required_space (requests[batch_end]) <= page_capacity)
		{
		  needed_space += (int) SPAGE_SLOT_SIZE + required_space (requests[batch_end]);
		  batch_end++;
		}

	      const std::size_t batch_size = batch_end - pos;
	      err = oos_insert_single_page_batch (thread_p, oos_vfid, requests.subspan (pos, batch_size),
						  needed_space);
	      if (err == NO_ERROR)
		{
		  pos = batch_end;
		  publication_count += batch_size;
		}
	    }

	  if (err != NO_ERROR)
	    {
	      return err;
	    }
	}
    }
  catch (const std::bad_alloc &)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      MAX (sizeof (OID), sizeof (LOG_LSA)));
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  cleanup_publication_on_error.release ();
  return NO_ERROR;
}


//
// Multi-chunk OOS insert with replication boundary tracking:
//
//   Layout per multi-chunk record (chunks logged in reverse order: tail first, head last):
//     LOG_DUMMY_OOS_RECORD    <- boundary marker, does not carry data
//     RVOOS_INSERT (chunk N-1, tail)
//     ...
//     RVOOS_INSERT (chunk 1)
//     RVOOS_INSERT (chunk 0, head)    <- carries final next_chunk_oid chain
//
//   Per-transaction queue/vector invariant (for the slave applier to reassemble):
//     oos_insert_lsa_queue : [..., dummy_lsa, tail_chunk_lsa]
//     oos_oids             : [..., oid_Null_oid]
//
//   The public insert API pushes the real head-chunk OID after this function returns,
//   so the final pairing becomes oos_oids=[..., null, real_oid]
//   with queue=[..., dummy_lsa, tail_chunk_lsa]. The replication path then emits
//   one RVREPL_DUMMY_OOS_RECORD for the null OID (pops dummy_lsa) followed by one
//   RVREPL_OOS_INSERT for the real OID (pops tail_chunk_lsa). Intermediate
//   chunks are not enqueued; tdes->oos_suppress_insert_lsa_queueing suppresses the
//   auto-push in log_append_{undo,}redo_crumbs while this function runs.
//
static int
oos_insert_across_pages (THREAD_ENTRY *thread_p, const VFID &oos_vfid, oos_buffer src, OID &oid)
{
  int error_code = NO_ERROR;
  LOG_TDES *tdes = NULL;
  LOG_LSA dummy_lsa = NULL_LSA;
  LOG_LSA tail_chunk_lsa = NULL_LSA;
  bool track_repl = false;

  // split the payload to multiple chunks and insert them one by one
  const int max_chunk_size = oos_get_max_chunk_size_within_page ();
  const int total_data_length = static_cast<int> (src.size ());
  /* Both expressions below are rewritten to avoid adding to total_data_length,
   * which can be near INT_MAX (entry-guard bound) and would signed-overflow. */
  assert (total_data_length > max_chunk_size - OOS_RECORD_HEADER_SIZE);

  int required_page_nums = total_data_length / max_chunk_size;
  if (total_data_length % max_chunk_size != 0)
    {
      ++required_page_nums;
    }
  assert (required_page_nums > 1);

  int total_inserted_length = 0;
  OID next_chunk_oid = OID_INITIALIZER; // the last chunk has null OID as next_chunk_oid

  track_repl = oos_needs_repl_tracking (thread_p);
  if (track_repl)
    {
      const int tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
      tdes = LOG_FIND_TDES (tran_index);
      if (tdes == NULL)
	{
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_UNKNOWN_TRANINDEX, 1, tran_index);
	  return ER_LOG_UNKNOWN_TRANINDEX;
	}

      log_append_empty_record (thread_p, LOG_DUMMY_OOS_RECORD, NULL);
      LSA_COPY (&dummy_lsa, &tdes->tail_lsa);

      tdes->oos_suppress_insert_lsa_queueing = true;
    }

  scope_exit clear_oos_repl_state ([&]()
  {
    if (track_repl && tdes != NULL)
      {
	tdes->oos_suppress_insert_lsa_queueing = false;
      }
  });

  // this loop inserts chunks in reverse order so that next_chunk_oid is always known
  for (int i = required_page_nums - 1; i >= 0; --i)
    {
      // subspan clamps count to the remaining size, so the tail chunk's shorter length is automatic.
      oos_buffer chunk = src.subspan (static_cast<std::size_t> (i * max_chunk_size),
				      static_cast<std::size_t> (max_chunk_size));
      total_inserted_length += static_cast<int> (chunk.size ());

      // Keep total_data_length in each chunk so the log applier can validate all pieces before reassembly.
      OOS_RECORD_HEADER header{total_data_length, i, next_chunk_oid};

      OID current_chunk_oid;
      error_code = oos_insert_within_page (thread_p, oos_vfid, chunk, header, current_chunk_oid);
      if (error_code != NO_ERROR)
	{
	  oos_error ("could not insert chunk index=%d of length %zu.", i, chunk.size ());
	  assert_release_error (er_errid () != NO_ERROR);
	  // Partially inserted chunks are cleaned up when the caller aborts the transaction
	  // (individual undo records replay in reverse). The caller MUST NOT continue
	  // the transaction after this error.
	  return error_code;
	}

      if (track_repl && i == required_page_nums - 1)
	{
	  LSA_COPY (&tail_chunk_lsa, &tdes->tail_lsa);
	}

      next_chunk_oid = current_chunk_oid;
    }
  assert (total_inserted_length == total_data_length);

  if (track_repl)
    {
      tdes->oos_insert_lsa_queue.push (dummy_lsa);
      tdes->oos_insert_lsa_queue.push (tail_chunk_lsa);
      thread_p->oos_oids.push_back (oid_Null_oid);
    }

  // update the out parameter 'oid' to give access to the first slot
  oid = next_chunk_oid;
  return NO_ERROR;
}


static int
oos_insert_record_in_fixed_page (THREAD_ENTRY *thread_p, const VFID &oos_vfid, PAGE_PTR page_ptr,
				 const VPID &vpid, oos_buffer src, const OOS_RECORD_HEADER &header, OID &oid)
{
  OOS_RECDES oos_recdes{};
  int err = oos_prepend_header (src, header, oos_recdes);
  if (err != NO_ERROR)
    {
      oos_error ("oos_prepend_header failed");
      return err;
    }

  scope_exit defer_oos_recdes_free ([&]()
  {
    recdes_free_data_area (&oos_recdes);
  });

  PGSLOTID slotid = NULL_SLOTID;
  int sp_status = spage_insert (thread_p, page_ptr, &oos_recdes, &slotid);
  if (sp_status != SP_SUCCESS)
    {
      oos_error ("spage_insert failed with status %d", sp_status);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return ER_GENERIC_ERROR;
    }

  assert (slotid != NULL_SLOTID);

  oid.pageid = vpid.pageid;
  oid.slotid = slotid;
  oid.volid = vpid.volid;

  oos_log_insert_physical (thread_p, page_ptr, const_cast<VFID *> (&oos_vfid), &oid, &oos_recdes);
  return NO_ERROR;
}

static int
oos_insert_within_page (THREAD_ENTRY *thread_p, const VFID &oos_vfid, oos_buffer src,
			const OOS_RECORD_HEADER &header,
			OID &oid)
{
  int err = NO_ERROR;
  VPID vpid;

  const int src_len = static_cast<int> (src.size ());
  assert (src_len <= oos_get_max_chunk_size_within_page ());

  int required_length = src_len + OOS_RECORD_HEADER_SIZE;

  assert (required_length <= DB_ALIGN_BELOW (spage_max_record_size (), OOS_ALIGNMENT));

  auto auto_page_ptr = oos_find_best_page (thread_p, oos_vfid, required_length, vpid);
  if (auto_page_ptr == nullptr)
    {
      ASSERT_ERROR_AND_SET (err);
      return err;
    }
  PAGE_PTR page_ptr = auto_page_ptr.get ();
  err = oos_insert_record_in_fixed_page (thread_p, oos_vfid, page_ptr, vpid, src, header, oid);
  if (err != NO_ERROR)
    {
      return err;
    }

  /* Update bestspace cache after insert — use spage_max_space_for_new_record
   * for consistency with the lookup check in oos_stats_find_page_in_bestspace */
  int freespace_after = spage_max_space_for_new_record (thread_p, page_ptr);
  (void) oos_stats_add_bestspace (thread_p, &oos_vfid, &vpid, freespace_after);

  return NO_ERROR;
}


/* Reads one chunk from an already-fixed OOS page: copies the chain header to
 * header_out and appends the chunk payload to writer. */
static int
oos_read_chunk_in_page (THREAD_ENTRY *thread_p, PAGE_PTR page_ptr, const OID &oid,
			cubbase::byte_span_writer &writer, OOS_RECORD_HEADER &header_out)
{
  OOS_RECDES oos_recdes;
  SCAN_CODE code = spage_get_record (thread_p, page_ptr, oid.slotid, &oos_recdes, PEEK);
  if (code != S_SUCCESS)
    {
      oos_error ("spage_get_record failed (code=%d) at oid={vol=%d,page=%d,slot=%d}", (int) code, OID_AS_ARGS (&oid));
      /* Some SCAN_CODE failures leave er_errid()==NO_ERROR; ensure caller observes an error. */
      if (er_errid () == NO_ERROR)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	}
      return er_errid ();
    }

  /* Every OOS slot carries at least the chain header; shorter is on-disk corruption. */
  assert (oos_recdes.length >= OOS_RECORD_HEADER_SIZE);
  if (oos_recdes.length < OOS_RECORD_HEADER_SIZE)
    {
      oos_error ("OOS slot smaller than header (len=%d) at oid={vol=%d,page=%d,slot=%d}",
		 oos_recdes.length, OID_AS_ARGS (&oid));
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HEAP_OOS_CORRUPTED_RECORD, 0);
      return ER_HEAP_OOS_CORRUPTED_RECORD;
    }

  std::memcpy (&header_out, oos_recdes.data, OOS_RECORD_HEADER_SIZE);

  const int payload_len = oos_recdes.length - OOS_RECORD_HEADER_SIZE;
  if (!writer.append (oos_recdes.data + OOS_RECORD_HEADER_SIZE, static_cast<std::size_t> (payload_len)))
    {
      oos_error ("OOS chunk overflows caller buffer (payload=%d, remaining=%zu) at oid={vol=%d,page=%d,slot=%d}",
		 payload_len, writer.remaining (), OID_AS_ARGS (&oid));
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HEAP_OOS_CORRUPTED_RECORD, 0);
      return ER_HEAP_OOS_CORRUPTED_RECORD;
    }
  return NO_ERROR;
}


static int
oos_read_within_page (THREAD_ENTRY *thread_p, const OID &oid,
		      cubbase::byte_span_writer &writer, OOS_RECORD_HEADER &header_out)
{
  auto vpid = VPID{oid.pageid, oid.volid};

  PAGE_PTR page_ptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
  if (page_ptr == nullptr)
    {
      oos_error ("pgbuf_fix failed at oid={vol=%d,page=%d,slot=%d}", OID_AS_ARGS (&oid));
      assert_release_error (er_errid () != NO_ERROR);
      return er_errid ();
    }
  scope_exit page_unfixer ([&]()
  {
    pgbuf_unfix_and_init_after_check (thread_p, page_ptr);
  });

  return oos_read_chunk_in_page (thread_p, page_ptr, oid, writer, header_out);
}


static int
oos_read_across_pages (THREAD_ENTRY *thread_p, const OID &next_oid,
		       int total_data_length, cubbase::byte_span_writer &writer)
{
  int idx = 1;
  OID current = next_oid;
  while (!OID_ISNULL (&current))
    {
      OOS_RECORD_HEADER header;
      const std::size_t before = writer.written ();

      int err = oos_read_within_page (thread_p, current, writer, header);
      if (err != NO_ERROR)
	{
	  return err;
	}

      /* chunk_index must increment from 1 and total_data_length must match the head. */
      assert (idx == header.chunk_index);
      assert (header.total_data_length == total_data_length);
      if (idx != header.chunk_index || header.total_data_length != total_data_length)
	{
	  oos_error ("OOS chain inconsistency at idx=%d: header.chunk_index=%d, header.total_data_length=%d,"
		     " expected_total=%d at oid={vol=%d,page=%d,slot=%d}",
		     idx, header.chunk_index, header.total_data_length, total_data_length, OID_AS_ARGS (&current));
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HEAP_OOS_CORRUPTED_RECORD, 0);
	  return ER_HEAP_OOS_CORRUPTED_RECORD;
	}

      /* A 0-byte chunk would let a cyclic next_chunk_oid loop forever. */
      if (writer.written () == before)
	{
	  oos_error ("OOS empty chunk at idx=%d, oid={vol=%d,page=%d,slot=%d}", idx, OID_AS_ARGS (&current));
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HEAP_OOS_CORRUPTED_RECORD, 0);
	  return ER_HEAP_OOS_CORRUPTED_RECORD;
	}

      current = header.next_chunk_oid;
      idx++;
    }
  return NO_ERROR;
}


/* Head-chunk validation shared by oos_read and oos_read_many: the caller's OID must be
 * the chain head (a mid-chain target means a corrupted inline OID), and the caller's
 * inline length (dest.size()) must agree with the chain header. */
static int
oos_check_head_header (const OOS_RECORD_HEADER &header, int expected_length, const OID &oid)
{
  assert (header.chunk_index == 0);
  if (header.chunk_index != 0)
    {
      oos_error ("OOS read at non-head chunk: chunk_index=%d at oid={vol=%d,page=%d,slot=%d}",
		 header.chunk_index, OID_AS_ARGS (&oid));
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HEAP_OOS_CORRUPTED_RECORD, 0);
      return ER_HEAP_OOS_CORRUPTED_RECORD;
    }

  if (header.total_data_length != expected_length)
    {
      oos_error ("OOS length mismatch: caller=%d header=%d at oid={vol=%d,page=%d,slot=%d}",
		 expected_length, header.total_data_length, OID_AS_ARGS (&oid));
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HEAP_OOS_CORRUPTED_RECORD, 0);
      return ER_HEAP_OOS_CORRUPTED_RECORD;
    }
  return NO_ERROR;
}


/* Cross-validates dest.size() against the chain header's total_data_length;
 * mismatch (corruption) is rejected. byte_span_writer guards each chunk
 * against payload_len overflow inside the loop. */
int
oos_read (THREAD_ENTRY *thread_p, const OID &oid, oos_buffer dest)
{
  assert (dest.data () != nullptr && dest.size () > 0);

  const int expected_length = static_cast<int> (dest.size ());

  cubbase::byte_span_writer writer (dest);
  OOS_RECORD_HEADER first_header;

  int err = oos_read_within_page (thread_p, oid, writer, first_header);
  if (err == NO_ERROR)
    {
      err = oos_check_head_header (first_header, expected_length, oid);
    }
  if (err != NO_ERROR)
    {
      return err;
    }

  if (!OID_ISNULL (&first_header.next_chunk_oid))
    {
      err = oos_read_across_pages (thread_p, first_header.next_chunk_oid,
				   expected_length, writer);
      if (err != NO_ERROR)
	{
	  return err;
	}
    }

  if (!writer.full ())
    {
      oos_error ("OOS final length mismatch: written=%zu expected=%d at oid={vol=%d,page=%d,slot=%d}",
		 writer.written (), expected_length, OID_AS_ARGS (&oid));
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HEAP_OOS_CORRUPTED_RECORD, 0);
      return ER_HEAP_OOS_CORRUPTED_RECORD;
    }
  return NO_ERROR;
}

/* Grouped OOS read: requests whose head chunks share a page are resolved under one
 * page fix. Multi-chunk chains are continued after the shared page is unfixed so
 * only one OOS page stays fixed at a time (same as the scalar oos_read). */
int
oos_read_many (THREAD_ENTRY *thread_p, cubbase::span<oos_read_request> requests)
{
  struct oos_read_continuation
  {
    oos_read_request *request;
    OID next_oid;
    std::size_t head_payload_size;
  };

  OOS_COUNTER_INC (read_many_calls);
  OOS_COUNTER_ADD (read_many_requests, requests.size ());

  for (std::size_t i = 0; i < requests.size (); i++)
    {
      if (OID_ISNULL (&requests[i].oid) || requests[i].dest.data () == nullptr || requests[i].dest.size () == 0
	  || requests[i].dest.size () > (std::size_t) INT_MAX)
	{
	  oos_error ("oos_read_many rejected invalid request %zu (oid={vol=%d,page=%d,slot=%d}, data=%p, size=%zu)",
		     i, OID_AS_ARGS (&requests[i].oid), requests[i].dest.data (), requests[i].dest.size ());
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  return ER_GENERIC_ERROR;
	}
    }

  try
    {
      std::vector<bool> done (requests.size (), false);
      std::vector<oos_read_continuation> continuations;

      for (std::size_t i = 0; i < requests.size (); i++)
	{
	  if (done[i])
	    {
	      continue;
	    }

	  VPID vpid = { requests[i].oid.pageid, requests[i].oid.volid };
	  continuations.clear ();

	  {
	    PAGE_PTR page_ptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
	    if (page_ptr == nullptr)
	      {
		oos_error ("pgbuf_fix failed for grouped OOS read at vpid={vol=%d,page=%d}", vpid.volid, vpid.pageid);
		assert_release_error (er_errid () != NO_ERROR);
		return er_errid ();
	      }
	    scope_exit page_unfixer ([&]()
	    {
	      pgbuf_unfix_and_init_after_check (thread_p, page_ptr);
	    });

	    OOS_COUNTER_INC (read_many_grouped_head_pages);

	    /* Resolve every request whose head chunk lives on this fixed page. */
	    for (std::size_t j = i; j < requests.size (); j++)
	      {
		const VPID request_vpid = { requests[j].oid.pageid, requests[j].oid.volid };
		if (done[j] || !VPID_EQ (&request_vpid, &vpid))
		  {
		    continue;
		  }
		done[j] = true;
		OOS_COUNTER_INC (read_values_per_fixed_page);

		cubbase::byte_span_writer writer (requests[j].dest);
		OOS_RECORD_HEADER header;

		int err = oos_read_chunk_in_page (thread_p, page_ptr, requests[j].oid, writer, header);
		if (err == NO_ERROR)
		  {
		    err = oos_check_head_header (header, static_cast<int> (requests[j].dest.size ()), requests[j].oid);
		  }
		if (err != NO_ERROR)
		  {
		    return err;
		  }

		if (!OID_ISNULL (&header.next_chunk_oid))
		  {
		    continuations.push_back ({ &requests[j], header.next_chunk_oid, writer.written () });
		  }
		else if (!writer.full ())
		  {
		    oos_error ("OOS final length mismatch: written=%zu expected=%zu at oid={vol=%d,page=%d,slot=%d}",
			       writer.written (), requests[j].dest.size (), OID_AS_ARGS (&requests[j].oid));
		    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HEAP_OOS_CORRUPTED_RECORD, 0);
		    return ER_HEAP_OOS_CORRUPTED_RECORD;
		  }
	      }
	  }

	  for (const oos_read_continuation &continuation : continuations)
	    {
	      oos_read_request &request = *continuation.request;
	      cubbase::byte_span_writer writer (request.dest.subspan (continuation.head_payload_size));

	      int err = oos_read_across_pages (thread_p, continuation.next_oid,
					       static_cast<int> (request.dest.size ()), writer);
	      if (err != NO_ERROR)
		{
		  return err;
		}

	      if (!writer.full ())
		{
		  oos_error ("OOS final continuation length mismatch: written=%zu expected_remaining=%zu"
			     " at oid={vol=%d,page=%d,slot=%d}",
			     writer.written (), request.dest.size () - continuation.head_payload_size,
			     OID_AS_ARGS (&request.oid));
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HEAP_OOS_CORRUPTED_RECORD, 0);
		  return ER_HEAP_OOS_CORRUPTED_RECORD;
		}
	    }
	}
    }
  catch (std::bad_alloc &)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      requests.size () * sizeof (oos_read_continuation));
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  return NO_ERROR;
}


// ****************************************************************************
// OOS Page allocation
// ****************************************************************************

/* Raw page allocation. Growth invariant: reach this ONLY through oos_alloc_page_with_reclaim —
 * a direct call could grow the file while a safely reclaimable page exists. */
static auto_unfix_page_ptr
oos_file_alloc_new (THREAD_ENTRY *thread_p, const VFID &oos_vfid,
		    VPID &vpid_out)
{
  int err = NO_ERROR;
  PAGE_TYPE page_type = PAGE_OOS;
  PAGE_PTR page = NULL;

  log_sysop_start (thread_p);
  err = file_alloc (thread_p, &oos_vfid, oos_vpid_init_new, &page_type, &vpid_out, &page);
  if (err != NO_ERROR || page == NULL)
    {
      oos_error ("file_alloc failed");
      assert_release_error (er_errid () != NO_ERROR);
      assert (false);
      log_sysop_abort (thread_p);
      return nullptr;
    }

  oos_trace ("allocated new page {volid=%d, pageid=%d}",
	     vpid_out.volid, vpid_out.pageid);

  log_sysop_commit (thread_p);

  /* Keep the WRITE latch acquired at allocation: a latch-free gap would let
   * oos_try_reclaim_page_internal deallocate the freshly allocated (still empty) page. */
  return auto_unfix_page_ptr (page, page_auto_unfix {thread_p});
}


static const auto_unfix_page_ptr
oos_find_best_page (THREAD_ENTRY *thread_p, const VFID &oos_vfid, const int rec_length, VPID &vpid)
{
  int err = NO_ERROR;
  int idx_badspace = 0;
  int try_find = 0;

  /* Get header page to access best[] hints */
  VPID hdr_vpid;
  err = file_get_sticky_first_page (thread_p, &oos_vfid, &hdr_vpid);
  if (err != NO_ERROR || VPID_ISNULL (&hdr_vpid))
    {
      /* No header page — fall back to allocating new page */
      return oos_alloc_page_with_reclaim (thread_p, oos_vfid, vpid);
    }

  PAGE_PTR hdr_page = pgbuf_fix (thread_p, &hdr_vpid, OLD_PAGE,
				 PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (hdr_page == NULL)
    {
      return oos_alloc_page_with_reclaim (thread_p, oos_vfid, vpid);
    }

  OOS_HDR_STATS *oos_hdr = oos_get_header_stats_ptr (thread_p, hdr_page);
  if (oos_hdr == NULL)
    {
      pgbuf_unfix_and_init (thread_p, hdr_page);
      return oos_alloc_page_with_reclaim (thread_p, oos_vfid, vpid);
    }

  int total_space = rec_length;

  /* 3-tier search: hash -> best[] -> sync -> alloc */
  PAGE_PTR found_page = NULL;
  OOS_FINDSPACE result;

  while (try_find < 2)
    {
      result = oos_stats_find_page_in_bestspace (thread_p, &oos_vfid,
	       oos_hdr->estimates.best,
	       &idx_badspace, total_space,
	       &vpid, &found_page);
      if (result == OOS_FINDSPACE_FOUND)
	{
	  break;
	}
      if (result == OOS_FINDSPACE_ERROR)
	{
	  ASSERT_ERROR ();
	  pgbuf_unfix_and_init (thread_p, hdr_page);
	  return nullptr;
	}

      /* Not found — try sync only if we have a hint that free pages exist
       * somewhere in the file. This mirrors heap_stats_find_best_page (heap_file.c):
       * the heap scans for free pages with heap_stats_sync_bestspace ONLY when
       * (num_other_high_best / num_pages) >= the sync threshold, i.e. only when it
       * already knows a reusable page exists. The previous "|| try_find == 0" forced
       * an unconditional sync scan on the first attempt of every insert. For a
       * blob-heavy workload each OOS chunk nearly fills a page, so no partially-free
       * page can ever satisfy a chunk; num_other_high_best stays ~0, yet every chunk
       * still paid for a full (and futile) bestspace sync scan. As the OOS file grew
       * this made per-row INSERT time climb several-fold (CBRD-26824). */
      float ratio = 0;
      if (oos_hdr->estimates.num_pages > 0)
	{
	  ratio = (float) oos_hdr->estimates.num_other_high_best / (float) oos_hdr->estimates.num_pages;
	}

      if (ratio >= OOS_BESTSPACE_SYNC_THRESHOLD)
	{
	  /* Release header latch before sync scan to reduce contention.
	   * Sync only updates the global hash cache (not best[]),
	   * so we don't need the header page during the scan. */
	  LOG_DATA_ADDR addr;
	  addr.vfid = NULL;
	  addr.pgptr = hdr_page;
	  addr.offset = 0;
	  log_skip_logging (thread_p, &addr);
	  pgbuf_set_dirty (thread_p, hdr_page, DONT_FREE);
	  pgbuf_unfix_and_init (thread_p, hdr_page);
	  oos_hdr = NULL;

	  /* Sync scan — populates global cache only.
	   * We pass a temporary OOS_HDR_STATS to collect scan stats,
	   * and discard the header-specific updates. */
	  OOS_HDR_STATS tmp_hdr;
	  memset (&tmp_hdr, 0, sizeof (tmp_hdr));
	  (void) oos_stats_sync_bestspace (thread_p, &oos_vfid, &tmp_hdr, &hdr_vpid, false);

	  /* Re-acquire header latch after sync */
	  hdr_page = pgbuf_fix (thread_p, &hdr_vpid, OLD_PAGE,
				PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
	  if (hdr_page == NULL)
	    {
	      if (er_errid () == ER_INTERRUPTED)
		{
		  return nullptr;
		}
	      return oos_alloc_page_with_reclaim (thread_p, oos_vfid, vpid);
	    }
	  oos_hdr = oos_get_header_stats_ptr (thread_p, hdr_page);
	  if (oos_hdr == NULL)
	    {
	      pgbuf_unfix_and_init (thread_p, hdr_page);
	      return oos_alloc_page_with_reclaim (thread_p, oos_vfid, vpid);
	    }
	}
      else
	{
	  break;
	}
      try_find++;
    }

  if (found_page != NULL)
    {
      /* Found a page — update header stats (non-logged) */
      LOG_DATA_ADDR addr;
      addr.vfid = NULL;
      addr.pgptr = hdr_page;
      addr.offset = 0;
      log_skip_logging (thread_p, &addr);
      pgbuf_set_dirty (thread_p, hdr_page, DONT_FREE);
      pgbuf_unfix_and_init (thread_p, hdr_page);

      /* Adopt the WRITE latch acquired at validation (Phase C). Safety invariant for reclaim: a
       * page an inserter has claimed is never latch-free, so oos_try_reclaim_page_internal's
       * conditional fix fails instead of deallocating it. */
      return auto_unfix_page_ptr (found_page, page_auto_unfix {thread_p});
    }

  /* No existing page found — allocate new */
  pgbuf_unfix_and_init_after_check (thread_p, hdr_page);

  auto new_page = oos_alloc_page_with_reclaim (thread_p, oos_vfid, vpid);

  /* Update bestspace cache with the new page — use spage_max_space_for_new_record
   * for consistency with the lookup check in oos_stats_find_page_in_bestspace */
  if (new_page != nullptr)
    {
      int free_space = spage_max_space_for_new_record (thread_p, new_page.get());
      (void) oos_stats_add_bestspace (thread_p, &oos_vfid, &vpid, free_space);
    }

  return new_page;
}


static int
oos_vpid_init_new (THREAD_ENTRY *thread_p, PAGE_PTR page, void *args)
{
  PAGE_TYPE ptype = * (PAGE_TYPE *) args;
  int err = NO_ERROR;

  pgbuf_set_page_ptype (thread_p, page, ptype);

  spage_initialize (thread_p, page, ANCHORED, OOS_ALIGNMENT, false);

  // (PGLENGTH)ptype is used in pgbuf_rv_new_page_redo to set page type during redo
  log_append_undoredo_data2 (thread_p, RVPGBUF_NEW_PAGE, NULL, page, (PGLENGTH) ptype, 0, SPAGE_HEADER_SIZE, NULL,
			     (SPAGE_HEADER *) page);

  pgbuf_set_dirty (thread_p, page, DONT_FREE);
  return err;
}

/*
 * oos_log_insert_physical () - add logging information for physical insertion
 *   thread_p(in): thread entry
 *   page_p(in): page where insert was performed
 *   vfid_p(in): virtual file id
 *   oid_p(in): newly inserted object id
 *   recdes_p(in): record descriptor of inserted record
 */
static void
oos_log_insert_physical (THREAD_ENTRY *thread_p, PAGE_PTR page_p, VFID *vfid_p, OID *oid_p, RECDES *recdes_p)
{
  LOG_DATA_ADDR log_addr;

  /* populate address field */
  log_addr.vfid = vfid_p;
  log_addr.offset = oid_p->slotid;
  log_addr.pgptr = page_p;

  log_append_undoredo_recdes (thread_p, RVOOS_INSERT, &log_addr, NULL, recdes_p);
}

/*
 * oos_log_delete_physical () - add logging information for physical deletion
 *   thread_p(in): thread entry
 *   page_p(in): page where delete will be performed
 *   slotid(in): slot id of the record to delete
 *   recdes_p(in): record descriptor of the record being deleted (undo data)
 */
static void
oos_log_delete_physical (THREAD_ENTRY *thread_p, PAGE_PTR page_p, VFID *vfid_p, PGSLOTID slotid, RECDES *recdes_p)
{
  LOG_DATA_ADDR log_addr;

  log_addr.vfid = vfid_p;
  log_addr.offset = slotid;
  log_addr.pgptr = page_p;

  log_append_undoredo_recdes (thread_p, RVOOS_DELETE, &log_addr, recdes_p, NULL);
}

// TODO: concurrency — this function assumes the caller holds a row-level lock (e.g., X_LOCK from heap layer)
//       to prevent concurrent deletion of the same OOS chain. Verify this assumption when wiring callers.

/*
 * oos_delete_chain () - delete all chunks in an OOS record chain (internal)
 *
 *   return: NO_ERROR or error code
 *   thread_p(in): thread entry
 *   oos_vfid(in): OOS file identifier
 *   oid(in): head OID of the OOS record chain
 *   touched_vpids(out): optional; every page a chunk was deleted from is appended (duplicates
 *			 allowed — the reclaim batch dedupes).
 *
 * NOTE: This is the inner workhorse called by oos_delete(). Each chunk
 *       deletion is logged individually with undo data, so transaction
 *       abort restores all deleted chunks in reverse order.
 */
static int
oos_delete_chain (THREAD_ENTRY *thread_p, const VFID &oos_vfid, const OID &oid,
		  std::vector<VPID> *touched_vpids)
{
  int error = NO_ERROR;
  OID current_oid = oid;
  while (!OID_ISNULL (&current_oid))
    {
      VPID vpid = {current_oid.pageid, current_oid.volid};

      PAGE_PTR page_ptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
      if (page_ptr == nullptr)
	{
	  ASSERT_ERROR_AND_SET (error);
	  oos_error ("pgbuf_fix failed for volid=%d, pageid=%d", current_oid.volid, current_oid.pageid);
	  return error;
	}

      scope_exit page_unfixer ([&] ()
      {
	pgbuf_unfix_and_init_after_check (thread_p, page_ptr);
      });

      OOS_RECDES oos_recdes = RECDES_INITIALIZER;
      SCAN_CODE code = spage_get_record (thread_p, page_ptr, current_oid.slotid, &oos_recdes, PEEK);
      if (code != S_SUCCESS)
	{
	  ASSERT_ERROR_AND_SET (error);
	  oos_error ("spage_get_record failed for volid=%d, pageid=%d, slotid=%d",
		     OID_AS_ARGS (&current_oid));
	  return error;
	}

      if (oos_recdes.length < (int) sizeof (OOS_RECORD_HEADER))
	{
	  assert_release (false);
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HEAP_OOS_CORRUPTED_RECORD, 0);
	  oos_error ("OOS record at volid=%d pageid=%d slotid=%d has invalid length %d",
		     OID_AS_ARGS (&current_oid), oos_recdes.length);
	  return ER_HEAP_OOS_CORRUPTED_RECORD;
	}
      OOS_RECORD_HEADER header;
      std::memcpy (&header, oos_recdes.data, sizeof (OOS_RECORD_HEADER));
      OID next_chunk_oid = header.next_chunk_oid;

      oos_log_delete_physical (thread_p, page_ptr, const_cast<VFID *> (&oos_vfid), current_oid.slotid,
			       &oos_recdes);

      PGSLOTID deleted_slotid = spage_delete (thread_p, page_ptr, current_oid.slotid);
      if (deleted_slotid == NULL_SLOTID)
	{
	  ASSERT_ERROR_AND_SET (error);
	  oos_error ("spage_delete failed for volid=%d, pageid=%d, slotid=%d",
		     OID_AS_ARGS (&current_oid));
	  return error;
	}
      pgbuf_set_dirty (thread_p, page_ptr, DONT_FREE);

      /* Update bestspace cache — page now has more free space after delete */
      oos_stats_update (thread_p, page_ptr, &oos_vfid, 0);

      if (touched_vpids != NULL)
	{
	  try
	    {
	      touched_vpids->push_back (vpid);
	    }
	  catch (std::bad_alloc &)
	    {
	      /* The candidate list is only a hint; the deletes themselves are logged and the
	       * growth-gate sweep rediscovers the pages. Never fail a delete batch over it. */
	      oos_warn ("dropping empty-page reclaim hints for file %d|%d: out of memory growing "
			"the candidate list; the growth-gate sweep will rediscover the pages",
			VFID_AS_ARGS (&oos_vfid));
	      touched_vpids->clear ();
	      touched_vpids = NULL;
	    }
	}

      oos_debug ("deleted chunk at oid={vol=%d,page=%d,slot=%d}, next={vol=%d,page=%d,slot=%d}",
		 OID_AS_ARGS (&current_oid), OID_AS_ARGS (&next_chunk_oid));

      current_oid = next_chunk_oid;
    }

  return NO_ERROR;
}

/*
 * oos_chunk_exists () - Probe whether the OOS chunk at oid still exists. Read-only companion to
 *   oos_delete for idempotent callers (e.g. vacuum forward-walk block retry, which must skip OIDs
 *   whose chunks a previously committed sysop already removed instead of tripping the
 *   S_DOESNT_EXIST hard error inside oos_delete_chain).
 *
 *   "Already gone" is narrowly defined:
 *     - pgbuf_fix_if_not_deallocated returns NO_ERROR with page_ptr==NULL (page deallocated), OR
 *     - spage_get_record returns S_DOESNT_EXIST (slot removed but page still alive).
 *
 *   Any other failure (real pgbuf_fix error from I/O / interrupt / buffer corruption, or
 *   spage_get_record returning S_ERROR) is propagated as the probe's return value; callers must
 *   treat that as a failure rather than a successful "gone".
 */
int
oos_chunk_exists (THREAD_ENTRY *thread_p, const OID &oid, bool *out_exists)
{
  *out_exists = false;

  VPID vpid;
  vpid.volid = oid.volid;
  vpid.pageid = oid.pageid;

  PAGE_PTR page_ptr = NULL;
  int error_code = pgbuf_fix_if_not_deallocated (thread_p, &vpid, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH,
		   &page_ptr);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }
  if (page_ptr == NULL)
    {
      /* Page legitimately deallocated; chunk is gone. */
      return NO_ERROR;
    }

  RECDES probe = RECDES_INITIALIZER;
  SCAN_CODE code = spage_get_record (thread_p, page_ptr, oid.slotid, &probe, PEEK);
  pgbuf_unfix_and_init (thread_p, page_ptr);

  if (code == S_SUCCESS)
    {
      *out_exists = true;
      return NO_ERROR;
    }
  if (code == S_DOESNT_EXIST)
    {
      /* Slot already removed; chunk is gone. */
      return NO_ERROR;
    }
  ASSERT_ERROR ();
  int errid = er_errid ();
  return errid != NO_ERROR ? errid : ER_FAILED;
}

/*
 * oos_delete () - delete an OOS record (single-chunk or multi-chunk chain)
 *
 *   return: NO_ERROR or error code
 *   thread_p(in): thread entry
 *   oos_vfid(in): OOS file identifier
 *   oid(in): head OID of the OOS record
 *
 * NOTE: No sysop is used. Each chunk deletion is logged individually
 *       (RVOOS_DELETE with full record as undo data).
 *
 *       Why this is safe:
 *       - Transaction abort: undo records are replayed in reverse order,
 *         restoring all deleted chunks to their original slotids via
 *         spage_insert_for_recovery. The chain is fully restored.
 *       - Crash recovery: same mechanism — incomplete transaction's undo
 *         records are replayed during recovery, restoring the chain.
 *       - Error mid-chain: partial deletes have undo records in the
 *         transaction log. The caller must abort the transaction to
 *         restore consistency.
 *
 *       Limitation: the caller MUST NOT continue the transaction after
 *       this function returns an error. If the caller ignores the error
 *       and commits, partially deleted chunks become permanent while
 *       remaining chunks are orphaned. This is acceptable because storage
 *       layer errors always propagate up and result in transaction abort.
 *
 *       Page deallocation is NOT done here. Vacuum reclaims emptied pages
 *       via oos_reclaim_empty_pages after its deletes are committed; every
 *       successful chain delete also arms the growth-gate sweep, the
 *       backstop for paths with no vacuum candidate list.
 */
int
oos_delete (THREAD_ENTRY *thread_p, const VFID &oos_vfid, const OID &oid,
	    std::vector<VPID> *touched_vpids)
{
  oos_debug ("arguments: oid={vol=%d,page=%d,slot=%d}", OID_AS_ARGS (&oid));

  int err = oos_delete_chain (thread_p, oos_vfid, oid, touched_vpids);
  if (err == NO_ERROR)
    {
      oos_reclaim_note_delete (oos_vfid);
    }
  return err;
}

// TODO: since this value never changes, we can make it a constant or static variable,
// and make it initialized only once in something like oos_boot().
STATIC_INLINE __attribute__ ((ALWAYS_INLINE)) int
oos_get_max_chunk_size_within_page ()
{
  // TODO: fix bug for spage_max_record_size returning incorrect size, which is out of scope for OOS project.
  const int actual_upper_limit = DB_ALIGN_BELOW (spage_max_record_size (), OOS_ALIGNMENT);

  return actual_upper_limit - (int)sizeof (OOS_RECORD_HEADER);
}

/*
 * oos_needs_repl_tracking () - check whether OOS replication boundary markers should be logged
 *
 * return: true if the master should emit OOS replication markers
 *
 *   thread_p(in): thread entry
 *
 * Note:
 *   Only the master writes replication boundary markers. The log applier replays
 *   OOS inserts on the slave, but it must not generate another dummy OOS record
 *   while applying replicated data.
 */
static bool
oos_needs_repl_tracking (THREAD_ENTRY *thread_p)
{
  return !LOG_CHECK_LOG_APPLIER (thread_p) && log_does_allow_replication () == true;
}

int
oos_rv_redo_delete (THREAD_ENTRY *thread_p, LOG_RCV *rcv)
{
  INT16 slotid;

  slotid = rcv->offset;
  PGSLOTID deleted_slotid = spage_delete (thread_p, rcv->pgptr, slotid);
  if (deleted_slotid == NULL_SLOTID)
    {
      assert (false);
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return er_errid ();
    }
  pgbuf_set_dirty (thread_p, rcv->pgptr, DONT_FREE);

  /* Only a runtime rollback's page LSA is redo-only and thus eligible for the reclaim gate's
   * narrow equality case. */
  VPID page_vpid;
  pgbuf_get_vpid (rcv->pgptr, &page_vpid);
  LOG_TDES *tdes = LOG_FIND_CURRENT_TDES (thread_p);
  const LOG_LSA *rollback_delete_lsa = LOG_ISRESTARTED () && tdes != NULL && LOG_ISTRAN_ABORTED (tdes)
				       ? pgbuf_get_lsa (rcv->pgptr) : NULL;
  (void) oos_stats_del_bestspace_by_vpid (thread_p, &page_vpid, true, rollback_delete_lsa);

  return NO_ERROR;
}

int
oos_rv_redo_insert (THREAD_ENTRY *thread_p, LOG_RCV *rcv)
{
  INT16 slotid;
  RECDES recdes;
  int sp_success;

  slotid = rcv->offset;
  recdes.type = * (INT16 *) (rcv->data);
  recdes.data = (char *) (rcv->data) + sizeof (recdes.type);
  recdes.area_size = recdes.length = rcv->length - sizeof (recdes.type);

  assert (recdes.type == REC_HOME);

  sp_success = spage_insert_for_recovery (thread_p, rcv->pgptr, slotid, &recdes);
  pgbuf_set_dirty (thread_p, rcv->pgptr, DONT_FREE);

  if (sp_success != SP_SUCCESS)
    {
      /* Unable to redo insertion */
      if (sp_success != SP_ERROR)
	{
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	}
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  return NO_ERROR;
}

int
oos_get_length (THREAD_ENTRY *thread_p, const OID &oid)
{
  const auto [pageid, slotid, volid] = oid;
  auto vpid = VPID{pageid, volid};

  /* Tolerate a deallocated page: vacuum may have reclaimed it, so a vacuumed OID must report
   * "gone" rather than trip the dead-page assert. */
  PAGE_PTR page_ptr = NULL;
  int err = pgbuf_fix_if_not_deallocated (thread_p, &vpid, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH, &page_ptr);
  if (err != NO_ERROR)
    {
      oos_error ("oos_get_length: pgbuf_fix failed for volid=%d, pageid=%d", volid, pageid);
      assert_release_error (er_errid () != NO_ERROR);
      assert (false);
      return -1;
    }
  if (page_ptr == nullptr)
    {
      oos_error ("oos_get_length: page deallocated for volid=%d, pageid=%d", volid, pageid);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return -1;
    }

  auto page_unfixer = make_scope_exit ([&]()
  {
    pgbuf_unfix_and_init_after_check (thread_p, page_ptr);
  });

  OOS_RECDES oos_recdes;
  SCAN_CODE code = spage_get_record (thread_p, page_ptr, slotid, &oos_recdes, PEEK);
  if (code != S_SUCCESS)
    {
      oos_error ("oos_get_length: spage_get_record failed for volid=%d, pageid=%d, slotid=%d",
		 volid, pageid, slotid);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return -1;
    }

  assert (oos_recdes.length >= OOS_RECORD_HEADER_SIZE);
  if (oos_recdes.length < OOS_RECORD_HEADER_SIZE)
    {
      oos_error ("oos_get_length: OOS record smaller than header (len=%d) at oid={vol=%d,page=%d,slot=%d}",
		 oos_recdes.length, OID_AS_ARGS (&oid));
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HEAP_OOS_CORRUPTED_RECORD, 0);
      return -1;
    }

  OOS_RECORD_HEADER header;
  std::memcpy (&header, oos_recdes.data, sizeof (OOS_RECORD_HEADER));

  return header.total_data_length;
}


// ****************************************************************************
// OOS stats for session-command verification (dev/debug)
// ****************************************************************************

int
xoos_get_stats_by_class_oid (THREAD_ENTRY *thread_p, const OID *class_oid, OOS_STATS_INFO *out)
{
  if (class_oid == NULL || out == NULL || OID_ISNULL (class_oid))
    {
      return ER_OBJ_INVALID_ARGUMENTS;
    }

  memset (out, 0, sizeof (*out));
  out->page_size = DB_PAGESIZE;
  VFID_SET_NULL (&out->oos_vfid);

  HFID hfid;
  HFID_SET_NULL (&hfid);
  if (heap_get_class_info (thread_p, class_oid, &hfid, NULL, NULL) != NO_ERROR)
    {
      ASSERT_ERROR ();
      return er_errid ();
    }
  if (HFID_IS_NULL (&hfid))
    {
      /* Class has no heap file yet (empty). Treat as "no OOS file". */
      return NO_ERROR;
    }

  VFID oos_vfid;
  VFID_SET_NULL (&oos_vfid);
  if (!heap_oos_find_vfid (thread_p, &hfid, &oos_vfid, false))
    {
      /* false return is overloaded: real read errors set er_errid; the
       * legitimate "no OOS file" path (docreate=false, NULL in heap header)
       * does not. Propagate if set, else treat as no-OOS. */
      int errid = er_errid ();
      if (errid != NO_ERROR)
	{
	  return errid;
	}
      return NO_ERROR;
    }
  if (VFID_ISNULL (&oos_vfid))
    {
      return NO_ERROR;
    }

  return oos_get_stats_by_vfid (thread_p, oos_vfid, out);
}

/*
 * oos_get_stats_by_vfid () - Collect live-record statistics for an OOS file.
 *   Core of xoos_get_stats_by_class_oid; also usable for OOS files that are
 *   not attached to a catalogued class (e.g. unit-test heaps).
 */
int
oos_get_stats_by_vfid (THREAD_ENTRY *thread_p, const VFID &oos_vfid, OOS_STATS_INFO *out)
{
  if (out == NULL || VFID_ISNULL (&oos_vfid))
    {
      return ER_OBJ_INVALID_ARGUMENTS;
    }

  memset (out, 0, sizeof (*out));
  out->page_size = DB_PAGESIZE;
  out->has_oos_file = 1;
  out->oos_vfid = oos_vfid;

  int num_user_pages = 0;
  if (file_get_num_user_pages (thread_p, &oos_vfid, &num_user_pages) != NO_ERROR)
    {
      ASSERT_ERROR ();
      return er_errid ();
    }
  out->num_user_pages = num_user_pages;

  /* OOS_HDR_STATS.estimates is a lazy best-space hint (updated only by sync scan),
   * so for accurate live counts we walk every page and sum spage statistics. */
  VPID hdr_vpid;
  VPID_SET_NULL (&hdr_vpid);
  if (file_get_sticky_first_page (thread_p, &oos_vfid, &hdr_vpid) != NO_ERROR)
    {
      ASSERT_ERROR ();
      return er_errid ();
    }

  std::vector<VPID> scan_vpids;
  if (oos_collect_data_page_vpids (thread_p, &oos_vfid, scan_vpids) != NO_ERROR)
    {
      ASSERT_ERROR ();
      return er_errid ();
    }

  INT64 total_recs = 0;
  INT64 total_sumlen = 0;
  for (const VPID &scan_vpid : scan_vpids)
    {
      if (!VPID_ISNULL (&hdr_vpid) && VPID_EQ (&scan_vpid, &hdr_vpid))
	{
	  continue;		/* skip header page — no user records */
	}

      PAGE_PTR page_ptr = pgbuf_fix (thread_p, &scan_vpid, OLD_PAGE_MAYBE_DEALLOCATED,
				     PGBUF_LATCH_READ, PGBUF_CONDITIONAL_LATCH);
      if (page_ptr == NULL)
	{
	  er_clear ();
	  continue;		/* page busy or concurrently deallocated — accept a slight undercount */
	}

      if (pgbuf_get_page_ptype (thread_p, page_ptr) != PAGE_OOS)
	{
	  pgbuf_unfix_and_init (thread_p, page_ptr);
	  continue;
	}

      /* Walk slots explicitly: spage_collect_statistics skips slot 0 (a heap-page
       * assumption where slot 0 holds the header record), but OOS data pages keep
       * records starting at slot 0, so it undercounts by one record per page. */
      PGSLOTID slotid = -1;
      RECDES slot_recdes;
      while (spage_next_record (page_ptr, &slotid, &slot_recdes, PEEK) == S_SUCCESS)
	{
	  total_recs++;
	  total_sumlen += slot_recdes.length;
	}
      pgbuf_unfix_and_init (thread_p, page_ptr);
    }

  out->num_recs = (int) total_recs;
  out->recs_sumlen = total_sumlen;

  return NO_ERROR;
}

#if defined(CUBRID_UNIT_TEST_ENABLED)
int
bridge_oos_get_max_chunk_size_within_page ()
{
  return oos_get_max_chunk_size_within_page ();
}

int
bridge_oos_vpid_init_new (THREAD_ENTRY *thread_p, PAGE_PTR page, void *args)
{
  return oos_vpid_init_new (thread_p, page, args);
}

const auto_unfix_page_ptr
bridge_oos_find_best_page (THREAD_ENTRY *thread_p, const VFID &oos_vfid, const int rec_length,
			   VPID &vpid)
{
  return oos_find_best_page (thread_p, oos_vfid, rec_length, vpid);
}

OOS_STATS_ENTRY *
bridge_oos_stats_add_bestspace (THREAD_ENTRY *thread_p, const VFID *vfid, VPID *vpid, int freespace)
{
  return oos_stats_add_bestspace (thread_p, vfid, vpid, freespace);
}

int
bridge_oos_stats_del_bestspace_by_vpid (THREAD_ENTRY *thread_p, VPID *vpid)
{
  return oos_stats_del_bestspace_by_vpid (thread_p, vpid);
}

int
bridge_oos_stats_del_bestspace_by_vfid (THREAD_ENTRY *thread_p, const VFID *vfid)
{
  return oos_stats_del_bestspace_by_vfid (thread_p, vfid);
}

OOS_HDR_STATS *
bridge_oos_get_header_stats_ptr (THREAD_ENTRY *thread_p, PAGE_PTR page_header)
{
  return oos_get_header_stats_ptr (thread_p, page_header);
}

void
bridge_oos_stats_put_second_best (OOS_HDR_STATS *oos_hdr, VPID *vpid)
{
  oos_stats_put_second_best (oos_hdr, vpid);
}

bool
bridge_oos_stats_get_second_best (OOS_HDR_STATS *oos_hdr, VPID *vpid)
{
  return oos_stats_get_second_best (oos_hdr, vpid);
}

int
bridge_oos_stats_sync_bestspace (THREAD_ENTRY *thread_p, const VFID *vfid,
				 OOS_HDR_STATS *oos_hdr, VPID *hdr_vpid,
				 bool scan_all)
{
  return oos_stats_sync_bestspace (thread_p, vfid, oos_hdr, hdr_vpid, scan_all);
}

OOS_FINDSPACE
bridge_oos_stats_find_page_in_bestspace (THREAD_ENTRY *thread_p, const VFID *vfid,
    OOS_BESTSPACE *bestspace, int *idx_badspace,
    int needed_space, VPID *out_vpid,
    PAGE_PTR *out_pgptr)
{
  return oos_stats_find_page_in_bestspace (thread_p, vfid, bestspace, idx_badspace,
	 needed_space, out_vpid, out_pgptr);
}

void
bridge_oos_debug_counters_reset ()
{
#define OOS_DEBUG_COUNTER_RESET(field) \
  oos_Debug_counters.field.store (0, std::memory_order_relaxed);
  OOS_DEBUG_COUNTER_FIELDS (OOS_DEBUG_COUNTER_RESET)
#undef OOS_DEBUG_COUNTER_RESET
}

oos_debug_counters
bridge_oos_debug_counters_get ()
{
  oos_debug_counters counters = { };

#define OOS_DEBUG_COUNTER_LOAD(field) \
  counters.field = oos_Debug_counters.field.load (std::memory_order_relaxed);
  OOS_DEBUG_COUNTER_FIELDS (OOS_DEBUG_COUNTER_LOAD)
#undef OOS_DEBUG_COUNTER_LOAD

  return counters;
}

void
oos_test_fail_insert_many_after_publications (int publication_count)
{
  oos_Test_fail_insert_many_after_publications.store (publication_count, std::memory_order_relaxed);
}

void
oos_test_throw_bad_alloc_on_next_oid_publication ()
{
  oos_Test_throw_bad_alloc_on_next_oid_publication.store (true, std::memory_order_relaxed);
}

void
oos_test_disarm_insert_publication_failures ()
{
  oos_Test_fail_insert_many_after_publications.store (-1, std::memory_order_relaxed);
  oos_Test_throw_bad_alloc_on_next_oid_publication.store (false, std::memory_order_relaxed);
}

void
oos_test_reclaim_reset_side_map ()
{
  assert (oos_Bestspace != NULL);

  (void) pthread_mutex_lock (&oos_Bestspace->bestspace_mutex);
  oos_reclaim_clear_all_states ();
  pthread_mutex_unlock (&oos_Bestspace->bestspace_mutex);
}

void
oos_test_reclaim_force_sweep_in_progress (const VFID &oos_vfid)
{
  assert (oos_Bestspace != NULL);

  (void) pthread_mutex_lock (&oos_Bestspace->bestspace_mutex);
  oos_reclaim_file_state &state = oos_Reclaim_states[oos_vfid];
  state.pending_deletes = 1;
  state.swept_this_boot = true;
  state.sweep_in_progress = true;
  pthread_mutex_unlock (&oos_Bestspace->bestspace_mutex);
}

void
oos_test_reclaim_release_sweep (const VFID &oos_vfid)
{
  assert (oos_Bestspace != NULL);

  (void) pthread_mutex_lock (&oos_Bestspace->bestspace_mutex);
  auto it = oos_Reclaim_states.find (oos_vfid);
  assert (it != oos_Reclaim_states.end ());
  it->second.pending_deletes = 0;
  it->second.swept_this_boot = true;
  oos_reclaim_release_sweep_locked (oos_vfid);
  pthread_mutex_unlock (&oos_Bestspace->bestspace_mutex);
}

int
oos_test_reclaim_sweep_step (THREAD_ENTRY *thread_p, const VFID &oos_vfid)
{
  return oos_reclaim_sweep_step (thread_p, oos_vfid);
}

int
oos_test_reclaim_waiter_count ()
{
  return oos_Test_reclaim_waiters.load (std::memory_order_relaxed);
}
#undef OOS_DEBUG_COUNTER_FIELDS
#endif
