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
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <new>
#include <set>
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
#include "fault_injection.h"
#include "heap_oos.hpp"

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
			const OOS_RECORD_HEADER &header, OID &oid, LOG_LSA &identity_stamp_out);
static int
oos_insert_record_in_fixed_page (THREAD_ENTRY *thread_p, const VFID &oos_vfid, PAGE_PTR page_ptr,
				 const VPID &vpid, oos_buffer src, const OOS_RECORD_HEADER &header, OID &oid,
				 LOG_LSA &identity_stamp_out);
static int
oos_insert_across_pages (THREAD_ENTRY *thread_p, const VFID &oos_vfid, oos_buffer src,
			 OID &oid, LOG_LSA &identity_stamp_out);
static int
oos_insert_single_page_batch (THREAD_ENTRY *thread_p, const VFID &oos_vfid,
			      cubbase::span<oos_insert_request> requests,
			      int needed_space);
static int
oos_fix_page_if_oos (THREAD_ENTRY *thread_p, const OID &oid, PGBUF_LATCH_MODE latch_mode, PAGE_PTR &page_out);
static int
oos_read_chunk_in_page (THREAD_ENTRY *thread_p, PAGE_PTR page_ptr, const OID &oid,
			cubbase::byte_span_writer &writer, OOS_RECORD_HEADER &header_out,
			const oos_chain_ref *head_ref);
static int
oos_read_within_page (THREAD_ENTRY *thread_p, const OID &oid,
		      cubbase::byte_span_writer &writer, OOS_RECORD_HEADER &header_out);
static int
oos_read_across_pages (THREAD_ENTRY *thread_p, const OID &next_oid,
		       int total_data_length, cubbase::byte_span_writer &writer);
static int
oos_check_head_header (const OOS_RECORD_HEADER &header, int expected_length, const oos_chain_ref &ref);
static void
oos_log_insert_physical (THREAD_ENTRY *thread_p, PAGE_PTR page_p, VFID *vfid_p, OID *oid_p, RECDES *recdes_p);
static void
oos_log_delete_physical (THREAD_ENTRY *thread_p, PAGE_PTR page_p, VFID *vfid_p, PGSLOTID slotid);
static int
oos_delete_chain (THREAD_ENTRY *thread_p, const VFID &oos_vfid, const oos_chain_ref &ref,
		  std::vector<VPID> *emptied_vpids, oos_delete_outcome *outcome_out);

STATIC_INLINE __attribute__ ((ALWAYS_INLINE))
int oos_get_max_chunk_size_within_page ();

static bool
oos_needs_repl_tracking (THREAD_ENTRY *thread_p);

static void
oos_publish_oos_oid (THREAD_ENTRY *thread_p, const OID &oid, const LOG_LSA &identity_stamp);

static void
oos_cleanup_insert_publication_state_on_error (THREAD_ENTRY *thread_p) noexcept;

static auto_unfix_page_ptr
oos_file_alloc_new (THREAD_ENTRY *thread_p, const VFID &oos_vfid, VPID &vpid_out);

static const auto_unfix_page_ptr
oos_find_best_page (THREAD_ENTRY *thread_p, const VFID &oos_vfid, const int rec_length, VPID &vpid);

static void
oos_reclaim_add_recovery_emptied_locked (const VFID *oos_vfid, const VPID &vpid);

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

/* Set by an oos_emptied_page_sink in scope on this thread; see oos_file.hpp. */
static thread_local std::vector<VPID> *oos_Emptied_page_sink = NULL;

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
  OP (read_values_per_fixed_page) \
  OP (reclaim_growth_examined_pages) \
  OP (reclaim_growth_laps)

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
static std::atomic<bool> oos_Test_fail_next_reclaim_write_fix_armed { false };
static std::atomic<bool> oos_Test_fail_next_reclaim_read_fix_armed { false };
/* Chunk deletes the reclaimer still performs before it fails with ER_GENERIC_ERROR; -1 when disarmed. */
static std::atomic<int> oos_Test_fail_reclaim_after_chunk_deletes { -1 };
/* Files the synthetic-owner oos_create_file overload created and oos_remove_file has not removed. */
static std::vector<VFID> oos_Test_created_files;
static std::mutex oos_Test_created_files_mutex;
static void (*oos_Test_reclaim_fail_stop_hook) (int error) = NULL;
/* UPDATE drop postpones that still append their notification before the process stops; 0 when disarmed. */
static std::atomic<int> oos_Test_exit_after_update_drop_notifications { 0 };
static int oos_Test_exit_after_update_drop_notifications_code = 0;

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
oos_stats_del_bestspace_by_vpid (THREAD_ENTRY *thread_p, VPID *vpid, bool page_emptied_by_recovery = false)
{
  OOS_STATS_ENTRY *ent;

  assert (oos_Bestspace != NULL);

  (void) pthread_mutex_lock (&oos_Bestspace->bestspace_mutex);

  ent = (OOS_STATS_ENTRY *) mht_get (oos_Bestspace->vpid_ht, vpid);
  if (page_emptied_by_recovery)
    {
      oos_reclaim_add_recovery_emptied_locked (ent == NULL ? NULL : &ent->vfid, *vpid);
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

/* Non-logged: bestspace hints in the header page don't need WAL. */
static void
oos_hdr_page_set_dirty_unlogged (THREAD_ENTRY *thread_p, PAGE_PTR hdr_page)
{
  LOG_DATA_ADDR addr;
  addr.vfid = NULL;
  addr.pgptr = hdr_page;
  addr.offset = 0;
  log_skip_logging (thread_p, &addr);
  pgbuf_set_dirty (thread_p, hdr_page, DONT_FREE);
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
  VPID hdr_vpid;

  *out_pgptr = NULL;
  VPID_SET_NULL (out_vpid);

  /* Get the header page VPID to skip it */
  if (file_get_sticky_first_page (thread_p, vfid, &hdr_vpid) != NO_ERROR)
    {
      VPID_SET_NULL (&hdr_vpid);
    }

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

      DISK_ISVALID is_owned = file_is_vpid_in_file (thread_p, vfid, &candidate_vpid);
      if (is_owned == DISK_ERROR)
	{
	  pgbuf_unfix_and_init (thread_p, *out_pgptr);
	  found = OOS_FINDSPACE_ERROR;
	  break;
	}
      if (is_owned != DISK_VALID)
	{
	  oos_trace ("stale bestspace hint to foreign OOS page vpid={vol=%d,page=%d} — evicting",
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
oos_stats_update (THREAD_ENTRY *thread_p, PAGE_PTR pgptr, const VFID *vfid)
{
  int freespace;

  freespace = spage_get_free_space_without_saving (thread_p, pgptr);

  if (freespace > 0)
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

	  oos_hdr_page_set_dirty_unlogged (thread_p, hdr_page);
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

  OID hdr_oid = { hdr_vpid.pageid, (short) slotid, hdr_vpid.volid };
  oos_log_insert_physical (thread_p, hdr_page, &oos_vfid, &hdr_oid, &hdr_recdes);

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

  int error = oos_create_file (thread_p, test_owner_hfid, test_owner_class_oid, oos_vfid);
  if (error == NO_ERROR)
    {
      std::lock_guard<std::mutex> guard (oos_Test_created_files_mutex);
      oos_Test_created_files.push_back (oos_vfid);
    }
  return error;
}
#endif /* CUBRID_UNIT_TEST_ENABLED */

int
oos_remove_file (THREAD_ENTRY *thread_p, const VFID &oos_vfid)
{
#if defined(CUBRID_UNIT_TEST_ENABLED)
  {
    std::lock_guard<std::mutex> guard (oos_Test_created_files_mutex);
    oos_Test_created_files.erase (std::remove_if (oos_Test_created_files.begin (), oos_Test_created_files.end (),
				  [&oos_vfid] (const VFID &v)
    {
      return VFID_EQ (&v, &oos_vfid);
    }), oos_Test_created_files.end ());
  }
#endif
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
// exists right now (ADR-0009). Every site that leaves an allocated OOS data page empty adds the
// page to its file's emptied-page set (ADR-0010). The single growth point
// (oos_alloc_page_with_reclaim) checks the set before it allocates; the vacuum fast path
// (oos_reclaim_empty_pages) reclaims the pages a delete batch emptied without waiting for a growth.
// When the set may be incomplete (boot, out of memory, size limit), the file is marked unknown and
// its next growth walks the sector bitmap once. Truth is on disk; the set and the mark are hints
// whose loss costs one lap, never a page.
//
// A sweep failure is absorbed so reclaim never fails an INSERT. Idle files are never shrunk.
// ****************************************************************************

typedef enum
{
  OOS_RECLAIM_RECLAIMED = 0,	/* page deallocated back to the file manager */
  OOS_RECLAIM_BUSY,		/* another thread holds the page latch; it may be empty, so it stays a candidate */
  OOS_RECLAIM_SKIPPED		/* re-filled, already deallocated, or not an OOS data page; no longer a candidate */
} OOS_RECLAIM_RESULT;

// ****************************************************************************
// OOS empty-page reclaim — per-file emptied-page set (per-VFID side map)
// ****************************************************************************

struct oos_vpid_order
{
  bool operator () (const VPID &a, const VPID &b) const
  {
    return VPID_LT (&a, &b);
  }
};

/* Per-file reclaim state. In-memory hints over the truth on disk (sector bitmap + page emptiness),
 * so losing an entry costs one lap, never a page. */
struct oos_reclaim_file_state
{
  std::set<VPID, oos_vpid_order> emptied;	/* pages that became empty and were not reclaimed or refilled
						 * since; a page leaves only under a latch that proves it
						 * deallocated or non-empty */
  bool unknown = true;		/* emptied may miss an empty page (boot, out of memory, size limit);
				 * the next growth walks the sector bitmap once */
  bool sweep_in_progress = false;	/* single-flight guard: at most one growth step per file at a time */
  VPID sweep_cursor = VPID_INITIALIZER;	/* the last page a lap reclaimed; the next lap starts just after it */
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

/* Pages a file's set holds before further emptied pages mark it unknown instead. */
#define OOS_RECLAIM_EMPTIED_LIMIT 4096
#if defined(CUBRID_UNIT_TEST_ENABLED)
static std::size_t oos_Reclaim_emptied_limit = OOS_RECLAIM_EMPTIED_LIMIT;
#else
static const std::size_t oos_Reclaim_emptied_limit = OOS_RECLAIM_EMPTIED_LIMIT;
#endif

/* One entry per OOS file used since boot; a missing entry reads as unknown. Guarded by
 * oos_Bestspace->bestspace_mutex. */
static std::map<VFID, oos_reclaim_file_state, oos_vfid_order> oos_Reclaim_states;
/* A single condition variable is sufficient: waiters always re-check their own VFID predicate
 * under bestspace_mutex, and a broadcast lets exactly one waiter per file claim the next step. */
static pthread_cond_t oos_Reclaim_sweep_cond = PTHREAD_COND_INITIALIZER;

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

/* Records that vpid became empty. A page the set cannot take marks the file unknown instead.
 * Caller holds bestspace_mutex. */
static void
oos_reclaim_add_emptied_locked (const VFID &oos_vfid, const VPID &vpid)
{
  try
    {
      oos_reclaim_file_state &state = oos_Reclaim_states[oos_vfid];
      if (state.emptied.size () >= oos_Reclaim_emptied_limit && state.emptied.count (vpid) == 0)
	{
	  state.unknown = true;
	  return;
	}
      try
	{
	  state.emptied.insert (vpid);
	}
      catch (std::bad_alloc &)
	{
	  state.unknown = true;
	}
    }
  catch (std::bad_alloc &)
    {
      /* Entry stays absent, which reads as unknown. */
    }
}

static void
oos_reclaim_add_emptied (const VFID &oos_vfid, const VPID &vpid)
{
  assert (oos_Bestspace != NULL);

  (void) pthread_mutex_lock (&oos_Bestspace->bestspace_mutex);
  oos_reclaim_add_emptied_locked (oos_vfid, vpid);
  pthread_mutex_unlock (&oos_Bestspace->bestspace_mutex);
}

/* Adds the page when it holds no record. Caller holds the page latch. */
static void
oos_reclaim_add_if_emptied (const VFID &oos_vfid, const VPID &vpid, PAGE_PTR page_ptr)
{
  if (spage_number_of_records (page_ptr) == 0)
    {
      oos_reclaim_add_emptied (oos_vfid, vpid);
    }
}

/* Recovery knows the page but not always its file: the bestspace hint names the file when it is
 * cached, and otherwise every known file is marked unknown. Caller holds bestspace_mutex. */
static void
oos_reclaim_add_recovery_emptied_locked (const VFID *oos_vfid, const VPID &vpid)
{
  if (oos_vfid != NULL)
    {
      oos_reclaim_add_emptied_locked (*oos_vfid, vpid);
      return;
    }

  for (auto &state : oos_Reclaim_states)
    {
      state.second.unknown = true;
    }
}

/* Drops vpid from the file's set. Caller holds a page latch that proves the page non-empty, or the
 * WRITE latch it deallocates the page under, so no delete can empty it again before the drop. */
static void
oos_reclaim_drop_emptied (const VFID &oos_vfid, const VPID &vpid)
{
  assert (oos_Bestspace != NULL);

  (void) pthread_mutex_lock (&oos_Bestspace->bestspace_mutex);
  auto it = oos_Reclaim_states.find (oos_vfid);
  if (it != oos_Reclaim_states.end ())
    {
      it->second.emptied.erase (vpid);
    }
  pthread_mutex_unlock (&oos_Bestspace->bestspace_mutex);
}

/*
 * oos_reclaim_note_file_created () - Mark a file created THIS boot as known: it can hold no empty
 *   page from a previous boot, so its first growth skips the boot-rule lap.
 */
static void
oos_reclaim_note_file_created (const VFID &oos_vfid)
{
  assert (oos_Bestspace != NULL);

  (void) pthread_mutex_lock (&oos_Bestspace->bestspace_mutex);

  try
    {
      oos_reclaim_file_state state;
      state.unknown = false;
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
}

/*
 * oos_reclaim_fix_candidate () - Zero-wait, dealloc-tolerant fix of a reclaim candidate.
 *   page_out == NULL with NO_ERROR means the page is deallocated (is_gone) or busy; ER_INTERRUPTED
 *   is propagated.
 */
static int
oos_reclaim_fix_candidate (THREAD_ENTRY *thread_p, const VPID &vpid, PGBUF_LATCH_MODE latch_mode,
			   PAGE_PTR &page_out, bool &is_gone)
{
  is_gone = false;

#if defined(CUBRID_UNIT_TEST_ENABLED)
  std::atomic<bool> &fail_armed = latch_mode == PGBUF_LATCH_WRITE
				  ? oos_Test_fail_next_reclaim_write_fix_armed : oos_Test_fail_next_reclaim_read_fix_armed;
  if (fail_armed.exchange (false, std::memory_order_relaxed))
    {
      page_out = NULL;
      return NO_ERROR;
    }
#endif

  page_out = pgbuf_fix (thread_p, &vpid, OLD_PAGE_MAYBE_DEALLOCATED, latch_mode, PGBUF_CONDITIONAL_LATCH);
  if (page_out == NULL)
    {
      int err = er_errid ();
      if (err == ER_INTERRUPTED)
	{
	  return err;
	}
      is_gone = err == ER_PB_BAD_PAGEID;
      er_clear ();
    }
  return NO_ERROR;
}

static bool
oos_page_is_reclaimable_empty (THREAD_ENTRY *thread_p, PAGE_PTR page_ptr)
{
  return pgbuf_get_page_ptype (thread_p, page_ptr) == PAGE_OOS && spage_number_of_records (page_ptr) == 0;
}

/*
 * oos_try_reclaim_page_internal () - Try to return one fully emptied OOS data page to the file
 *   manager's partial sector table. Two-phase: phase 1 pre-qualifies under a zero-wait READ fix
 *   with the OOS stats header latch NOT held; phase 2 takes the header WRITE latch, re-validates
 *   under a zero-wait WRITE fix, and deallocates in its own immediately-committed sysop.
 *
 *   Idempotent and zero-wait. A page latched by another thread in either phase is
 *   OOS_RECLAIM_BUSY and stays in the file's emptied-page set, so a later growth retries it. A
 *   re-filled or non-OOS page, and the sticky first page, are OOS_RECLAIM_SKIPPED, and a reclaimed
 *   page is OOS_RECLAIM_RECLAIMED; both leave the set while the latch that proved their state is
 *   held, so a delete that empties the page again re-adds it after the drop. A page found already
 *   deallocated is SKIPPED but stays in the set: no latch proves it was not reallocated, emptied and
 *   re-added since. The next check after it is reallocated and filled drops it.
 *
 *   return: NO_ERROR or error code (ER_INTERRUPTED propagates so reclaim loops stop immediately)
 *   thread_p(in): thread entry
 *   oos_vfid(in): OOS file identifier
 *   vpid(in): candidate page
 *   hdr_vpid(in): the file's sticky first page (holds OOS_HDR_STATS), resolved once by the caller
 *   result(out): per-page verdict; meaningful only when NO_ERROR is returned
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
 *   4. No undo can write into an empty OOS page (ADR-0008, which removed the LSA reclaim gate):
 *      RVOOS_DELETE is redo-only, issued only after the deciding unit committed, so nothing ever
 *      restores a chunk; the undo of RVOOS_INSERT deletes, and its chunk keeps the page non-empty
 *      until then; page allocation commits in its own sysop. An empty page is reclaimable now.
 *
 * Why not the heap's OLD_PAGE_PREVENT_DEALLOC protocol: readers reach OOS chunk pages only via
 * the head OOS OIDs of heap record versions, and vacuum empties a chain's pages only after the
 * owning version is invisible to every active snapshot — no reader can legally be walking them.
 * The dealloc tolerance on lookups is defense in depth for stale hints, not a substitute for
 * pinning, and PREVENT_DEALLOC would reintroduce a waiting edge into a zero-wait path.
 */
static int
oos_try_reclaim_page_internal (THREAD_ENTRY *thread_p, const VFID &oos_vfid, const VPID &vpid,
			       const VPID &hdr_vpid, OOS_RECLAIM_RESULT &result)
{
  int err = NO_ERROR;

  result = OOS_RECLAIM_SKIPPED;

  if (VPID_EQ (&vpid, &hdr_vpid))
    {
      /* The sticky first page (OOS_HDR_STATS) must never be deallocated; file_dealloc only
       * asserts this in debug builds. */
      oos_reclaim_drop_emptied (oos_vfid, vpid);
      return NO_ERROR;
    }

  /* Phase 1: pre-qualify under a zero-wait READ fix, header latch NOT held. */
  PAGE_PTR page_ptr = NULL;
  bool is_gone = false;
  err = oos_reclaim_fix_candidate (thread_p, vpid, PGBUF_LATCH_READ, page_ptr, is_gone);
  if (err != NO_ERROR)
    {
      return err;
    }
  if (page_ptr == NULL)
    {
      if (!is_gone)
	{
	  result = OOS_RECLAIM_BUSY;
	}
      return NO_ERROR;
    }

  if (!oos_page_is_reclaimable_empty (thread_p, page_ptr))
    {
      oos_reclaim_drop_emptied (oos_vfid, vpid);
      pgbuf_unfix_and_init (thread_p, page_ptr);
      return NO_ERROR;
    }
  pgbuf_unfix_and_init (thread_p, page_ptr);

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

  err = oos_reclaim_fix_candidate (thread_p, vpid, PGBUF_LATCH_WRITE, page_ptr, is_gone);
  if (err != NO_ERROR)
    {
      return err;
    }
  if (page_ptr == NULL)
    {
      if (!is_gone)
	{
	  result = OOS_RECLAIM_BUSY;
	}
      return NO_ERROR;
    }

  /* Phase 2's verdict is binding: the page may have changed between the phases. Drop it from the
   * set now, under the WRITE latch, whether it was refilled or is about to be deallocated. */
  oos_reclaim_drop_emptied (oos_vfid, vpid);
  if (!oos_page_is_reclaimable_empty (thread_p, page_ptr))
    {
      pgbuf_unfix_and_init (thread_p, page_ptr);
      return NO_ERROR;
    }

  log_sysop_start (thread_p);

  err = file_dealloc (thread_p, &oos_vfid, &vpid, FILE_OOS);
  if (err != NO_ERROR)
    {
      oos_error ("file_dealloc failed for vpid={pageid=%d, volid=%d}", vpid.pageid, vpid.volid);
      oos_reclaim_add_emptied (oos_vfid, vpid);
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
	  oos_hdr_page_set_dirty_unlogged (thread_p, hdr_page);
	}
    }

  oos_trace ("reclaimed empty page {volid=%d, pageid=%d} in file %d|%d",
	     vpid.volid, vpid.pageid, VFID_AS_ARGS (&oos_vfid));

  result = OOS_RECLAIM_RECLAIMED;
  return NO_ERROR;
}

/* Orders VPIDs by (volid, pageid). oos_reclaim_empty_pages sorts its candidates with it. The growth lap
 * compares its cursor to each page with it, so it must match the ascending order in which the lap visits pages. */
static bool
oos_vpid_lt (const VPID &a, const VPID &b)
{
  return VPID_LT (&a, &b);
}

/*
 * oos_reclaim_empty_pages () - Reclaim the fully emptied pages of an explicit candidate list
 *   (vacuum's fast path). Public contract in oos_file.hpp.
 *
 *   Chunk deletes are redo-only and never undone (ADR-0008), so an empty page is reclaimable as
 *   soon as it is empty. Callers hold no heap page latch: deallocation takes the OOS header latch.
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

  int n_reclaimed = 0;
  int n_busy = 0;
  for (const VPID &vpid : candidates)
    {
      OOS_RECLAIM_RESULT result = OOS_RECLAIM_SKIPPED;
      err = oos_try_reclaim_page_internal (thread_p, oos_vfid, vpid, hdr_vpid, result);
      if (err != NO_ERROR)
	{
	  return err;
	}
      if (result == OOS_RECLAIM_RECLAIMED)
	{
	  n_reclaimed++;
	}
      else if (result == OOS_RECLAIM_BUSY)
	{
	  n_busy++;
	}
    }

  oos_trace ("reclaim batch on file %d|%d: %d reclaimed, %d busy of %d candidate(s)",
	     VFID_AS_ARGS (&oos_vfid), n_reclaimed, n_busy, (int) candidates.size ());

  return NO_ERROR;
}

/*
 * oos_reclaim_claim_growth () - Take the file's growth lock, waiting for another grower of the same
 *   file. The lock covers the reclaim step and the file_alloc after it, so no grower can reserve a
 *   sector between another grower's check and its allocation.
 *
 *   claimed(out): false when the entry could not be created; the caller then grows without the gate
 */
static int
oos_reclaim_claim_growth (const VFID &oos_vfid, bool &claimed)
{
  int err = NO_ERROR;

  claimed = false;
  assert (oos_Bestspace != NULL);
  (void) pthread_mutex_lock (&oos_Bestspace->bestspace_mutex);
  try
    {
      while (true)
	{
	  oos_reclaim_file_state &state = oos_Reclaim_states[oos_vfid];
	  if (!state.sweep_in_progress)
	    {
	      state.sweep_in_progress = true;
	      claimed = true;
	      break;
	    }
#if defined(CUBRID_UNIT_TEST_ENABLED)
	  oos_Test_reclaim_waiters.fetch_add (1, std::memory_order_relaxed);
#endif
	  int wait_error = pthread_cond_wait (&oos_Reclaim_sweep_cond, &oos_Bestspace->bestspace_mutex);
#if defined(CUBRID_UNIT_TEST_ENABLED)
	  oos_Test_reclaim_waiters.fetch_sub (1, std::memory_order_relaxed);
#endif
	  if (wait_error != 0)
	    {
	      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_COND_WAIT, 0);
	      err = ER_CSS_PTHREAD_COND_WAIT;
	      break;
	    }
	}
    }
  catch (std::bad_alloc &)
    {
      /* Entry not created — it still reads as unknown for the next growth. */
    }
  pthread_mutex_unlock (&oos_Bestspace->bestspace_mutex);
  return err;
}

static void
oos_reclaim_release_growth (const VFID &oos_vfid)
{
  (void) pthread_mutex_lock (&oos_Bestspace->bestspace_mutex);
  oos_reclaim_release_sweep_locked (oos_vfid);
  pthread_mutex_unlock (&oos_Bestspace->bestspace_mutex);
}

/*
 * oos_reclaim_check_emptied () - Try the members of the file's emptied-page set, including pages
 *   added while this runs, each at most once per growth. Stops at the first page reclaimed.
 */
static int
oos_reclaim_check_emptied (THREAD_ENTRY *thread_p, const VFID &oos_vfid, const VPID &hdr_vpid,
			   std::set<VPID, oos_vpid_order> &examined, bool &reclaimed)
{
  reclaimed = false;
  std::vector<VPID> batch;
  while (true)
    {
      batch.clear ();
      (void) pthread_mutex_lock (&oos_Bestspace->bestspace_mutex);
      try
	{
	  auto it = oos_Reclaim_states.find (oos_vfid);
	  if (it != oos_Reclaim_states.end ())
	    {
	      for (const VPID &vpid : it->second.emptied)
		{
		  if (examined.insert (vpid).second)
		    {
		      batch.push_back (vpid);
		    }
		}
	    }
	}
      catch (std::bad_alloc &)
	{
	  /* Pages not taken stay in the set; marking the file unknown makes the lap cover them. */
	  auto it = oos_Reclaim_states.find (oos_vfid);
	  if (it != oos_Reclaim_states.end ())
	    {
	      it->second.unknown = true;
	    }
	}
      pthread_mutex_unlock (&oos_Bestspace->bestspace_mutex);

      if (batch.empty ())
	{
	  return NO_ERROR;
	}
      for (const VPID &vpid : batch)
	{
	  OOS_RECLAIM_RESULT result = OOS_RECLAIM_SKIPPED;
	  OOS_COUNTER_INC (reclaim_growth_examined_pages);
	  int err = oos_try_reclaim_page_internal (thread_p, oos_vfid, vpid, hdr_vpid, result);
	  if (err != NO_ERROR)
	    {
	      return err;
	    }
	  if (result == OOS_RECLAIM_RECLAIMED)
	    {
	      oos_trace ("growth step on file %d|%d: reclaimed emptied page {volid=%d, pageid=%d}",
			 VFID_AS_ARGS (&oos_vfid), vpid.volid, vpid.pageid);
	      reclaimed = true;
	      return NO_ERROR;
	    }
	}
    }
}

/*
 * oos_reclaim_sweep_step () - The growth step, run under the file's growth lock before every OOS
 *   page allocation. It checks the file's emptied-page set and, when the file is marked unknown,
 *   walks the file's data pages from the saved cursor, then checks the set again for pages added
 *   during the lap. It stops at the FIRST page reclaimed. That page is back in the partial sector
 *   table when this returns, so the caller's file_alloc reuses it instead of growing. A known file
 *   whose set is empty costs no page fix.
 *
 *   The lap clears the unknown mark when it starts and restores it unless it walks the whole file
 *   without reclaiming. A set loss during the lap marks the file again, and that mark survives, so
 *   the next growth laps again. Pages the lap finds busy enter the set.
 *
 *   return: NO_ERROR or error code. ER_INTERRUPTED propagates; on any error the file stays
 *   unknown if it was, and unprocessed candidates stay in the set.
 */
static int
oos_reclaim_sweep_step (THREAD_ENTRY *thread_p, const VFID &oos_vfid)
{
  int err = NO_ERROR;

  (void) pthread_mutex_lock (&oos_Bestspace->bestspace_mutex);
  auto entry = oos_Reclaim_states.find (oos_vfid);
  const bool has_work = entry == oos_Reclaim_states.end () || entry->second.unknown || !entry->second.emptied.empty ();
  pthread_mutex_unlock (&oos_Bestspace->bestspace_mutex);
  if (!has_work)
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

  std::set<VPID, oos_vpid_order> examined;
  bool reclaimed = false;
  err = oos_reclaim_check_emptied (thread_p, oos_vfid, hdr_vpid, examined, reclaimed);
  if (err != NO_ERROR || reclaimed)
    {
      return err;
    }

  bool run_lap = false;
  VPID cursor;
  VPID_SET_NULL (&cursor);
  (void) pthread_mutex_lock (&oos_Bestspace->bestspace_mutex);
  auto state_it = oos_Reclaim_states.find (oos_vfid);
  if (state_it != oos_Reclaim_states.end ())
    {
      run_lap = state_it->second.unknown;
      state_it->second.unknown = false;
      cursor = state_it->second.sweep_cursor;
    }
  pthread_mutex_unlock (&oos_Bestspace->bestspace_mutex);

  if (!run_lap)
    {
      return NO_ERROR;
    }

  bool lap_completed = false;
  scope_exit unknown_restorer ([&] () noexcept
  {
    if (lap_completed)
      {
	return;
      }
    (void) pthread_mutex_lock (&oos_Bestspace->bestspace_mutex);
    auto it = oos_Reclaim_states.find (oos_vfid);
    if (it != oos_Reclaim_states.end ())
      {
	it->second.unknown = true;
      }
    pthread_mutex_unlock (&oos_Bestspace->bestspace_mutex);
  });

  OOS_COUNTER_INC (reclaim_growth_laps);

  FILE_FTAB_COLLECTOR collector = FILE_FTAB_COLLECTOR_INITIALIZER;
  err = file_get_all_data_sectors (thread_p, &oos_vfid, &collector);
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

  /* Sort sectors, not expanded page IDs. FILE_PARTIAL_SECTOR intentionally starts with VSID,
   * so disk_compare_vsids can compare these entries directly. */
  if (collector.nsects > 1)
    {
      std::qsort (collector.partsect_ftab, collector.nsects, sizeof (*collector.partsect_ftab), disk_compare_vsids);
    }

  int n_busy = 0;
  const int n_partitions = VPID_ISNULL (&cursor) ? 1 : 2;
  for (int partition = 0; partition < n_partitions; partition++)
    {
      /* The first partition visits VPIDs after the cursor; the second wraps and visits the
       * cursor and all preceding VPIDs. Sectors and bits are ascending, so each partition
       * visits pages in ascending VPID order. */
      for (int sector_index = 0; sector_index < collector.nsects; sector_index++)
	{
	  const FILE_PARTIAL_SECTOR &partsect = collector.partsect_ftab[sector_index];
	  for (int bit = 0; bit < FILE_ALLOC_BITMAP_NBITS; bit++)
	    {
	      if ((partsect.page_bitmap & (((FILE_ALLOC_BITMAP) 1) << bit)) == 0)
		{
		  continue;
		}

	      VPID vpid;
	      vpid.volid = partsect.vsid.volid;
	      vpid.pageid = SECTOR_FIRST_PAGEID (partsect.vsid.sectid) + bit;

	      if (!VPID_ISNULL (&cursor))
		{
		  const bool is_after_cursor = oos_vpid_lt (cursor, vpid);
		  if (is_after_cursor != (partition == 0))
		    {
		      continue;
		    }
		}

	      OOS_RECLAIM_RESULT result = OOS_RECLAIM_SKIPPED;
	      OOS_COUNTER_INC (reclaim_growth_examined_pages);
	      err = oos_try_reclaim_page_internal (thread_p, oos_vfid, vpid, hdr_vpid, result);
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

		  oos_trace ("growth lap on file %d|%d: reclaimed page {volid=%d, pageid=%d}, %d busy so far",
			     VFID_AS_ARGS (&oos_vfid), vpid.volid, vpid.pageid, n_busy);
		  return NO_ERROR;
		}
	      if (result == OOS_RECLAIM_BUSY)
		{
		  /* A latched page may be one a rollback is emptying (Work818); the set keeps it. */
		  oos_reclaim_add_emptied (oos_vfid, vpid);
		  n_busy++;
		}
	    }
	}
    }

  lap_completed = true;

  oos_trace ("growth lap on file %d|%d: full lap over %d page(s), 0 reclaimed, %d busy",
	     VFID_AS_ARGS (&oos_vfid), collector.npages, n_busy);

  /* A page that emptied behind the cursor during the lap is in the set, not in the lap. */
  return oos_reclaim_check_emptied (thread_p, oos_vfid, hdr_vpid, examined, reclaimed);
}

/*
 * oos_alloc_page_with_reclaim () - The single growth point of an OOS file.
 *
 *   INVARIANT: every new-page allocation for an OOS file goes through this helper, never
 *   through oos_file_alloc_new directly. The growth step runs first; a page it frees is
 *   back in the partial sector table before file_alloc runs, so no retry loop is needed.
 *
 *   The file's growth lock is held from the reclaim step through file_alloc.
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
  bool claimed = false;
  int err = oos_reclaim_claim_growth (oos_vfid, claimed);
  scope_exit growth_release ([&] () noexcept
  {
    if (claimed)
      {
	oos_reclaim_release_growth (oos_vfid);
      }
  });
  if (err == NO_ERROR && claimed)
    {
      err = oos_reclaim_sweep_step (thread_p, oos_vfid);
    }
  if (err == ER_INTERRUPTED)
    {
      return nullptr;
    }
  if (err != NO_ERROR)
    {
      oos_warn ("growth reclaim step failed with error %d on file %d|%d; allocating anyway "
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

/* Publishes one inserted chain for replication: the head OOS OID and the identity stamp it was
 * created with, so the HA applier can rewrite both fields of the slave's stub (CBRD-26950). */
static void
oos_publish_oos_oid (THREAD_ENTRY *thread_p, const OID &oid, const LOG_LSA &identity_stamp)
{
#if defined(CUBRID_UNIT_TEST_ENABLED)
  if (oos_Test_throw_bad_alloc_on_next_oid_publication.exchange (false, std::memory_order_relaxed))
    {
      throw std::bad_alloc ();
    }
#endif
  thread_p->oos_oids.push_back ({ oid, identity_stamp });
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
oos_insert (THREAD_ENTRY *thread_p, const VFID &oos_vfid, oos_buffer src, OID &oid, LOG_LSA *identity_stamp_out)
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

  LOG_LSA identity_stamp = NULL_LSA;

  try
    {
      if (src_len <= oos_get_max_chunk_size_within_page ())
	{
	  const OOS_RECORD_HEADER header{src_len, 0, OID_INITIALIZER, NULL_LSA, NULL_LSA};
	  err = oos_insert_within_page (thread_p, oos_vfid, src, header, oid, identity_stamp);
	}
      else
	{
	  err = oos_insert_across_pages (thread_p, oos_vfid, src, oid, identity_stamp);
	}

      if (err != NO_ERROR)
	{
	  return err;
	}

      oos_publish_oos_oid (thread_p, oid, identity_stamp);
    }
  catch (const std::bad_alloc &)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      MAX (sizeof (OID), sizeof (LOG_LSA)));
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  cleanup_publication_on_error.release ();
  if (identity_stamp_out != NULL)
    {
      *identity_stamp_out = identity_stamp;
    }
  oos_debug ("inserted to oid={vol=%d,page=%d,slot=%d} identity_stamp=%lld|%d", OID_AS_ARGS (&oid),
	     LSA_AS_ARGS (&identity_stamp));
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
      const OOS_RECORD_HEADER header{src_len, 0, OID_INITIALIZER, NULL_LSA, NULL_LSA};
      OID oid;
      LOG_LSA identity_stamp = NULL_LSA;

      err = oos_insert_record_in_fixed_page (thread_p, oos_vfid, page_ptr, vpid, request.src, header, oid,
					     identity_stamp);
      if (err != NO_ERROR)
	{
	  oos_reclaim_add_if_emptied (oos_vfid, vpid, page_ptr);
	  return err;
	}

      *request.oid_out = oid;
      if (request.identity_stamp_out != NULL)
	{
	  *request.identity_stamp_out = identity_stamp;
	}
      oos_publish_oos_oid (thread_p, oid, identity_stamp);
    }

  if (page_was_empty)
    {
      oos_reclaim_drop_emptied (oos_vfid, vpid);
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
	      LOG_LSA identity_stamp = NULL_LSA;
	      err = oos_insert_across_pages (thread_p, oos_vfid, requests[pos].src, oid, identity_stamp);
	      if (err == NO_ERROR)
		{
		  *requests[pos].oid_out = oid;
		  if (requests[pos].identity_stamp_out != NULL)
		    {
		      *requests[pos].identity_stamp_out = identity_stamp;
		    }
		  oos_publish_oos_oid (thread_p, oid, identity_stamp);
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
//     oos_oids             : [..., {oid_Null_oid, NULL_LSA}]   (boundary marker)
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
oos_insert_across_pages (THREAD_ENTRY *thread_p, const VFID &oos_vfid, oos_buffer src, OID &oid,
			 LOG_LSA &identity_stamp_out)
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
  LOG_LSA next_chunk_stamp = NULL_LSA;	// and a NULL stamp for it

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
      OOS_RECORD_HEADER header{total_data_length, i, next_chunk_oid, next_chunk_stamp, NULL_LSA};

      OID current_chunk_oid;
      LOG_LSA chunk_identity_stamp = NULL_LSA;
      error_code = oos_insert_within_page (thread_p, oos_vfid, chunk, header, current_chunk_oid,
					   chunk_identity_stamp);
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
      next_chunk_stamp = chunk_identity_stamp;
      /* Chains are written tail first, so the loop ends at i == 0, the head chunk: this is the
       * value the owning stub must carry. Each chunk carries the LSA of its own page, so no
       * chain-wide value exists when the tail chunks are stamped. */
      identity_stamp_out = chunk_identity_stamp;
    }
  assert (total_inserted_length == total_data_length);

  if (track_repl)
    {
      tdes->oos_insert_lsa_queue.push (dummy_lsa);
      tdes->oos_insert_lsa_queue.push (tail_chunk_lsa);
      thread_p->oos_oids.push_back ({ oid_Null_oid, NULL_LSA });
    }

  // update the out parameter 'oid' to give access to the first slot
  oid = next_chunk_oid;
  return NO_ERROR;
}


static int
oos_insert_record_in_fixed_page (THREAD_ENTRY *thread_p, const VFID &oos_vfid, PAGE_PTR page_ptr,
				 const VPID &vpid, oos_buffer src, const OOS_RECORD_HEADER &header, OID &oid,
				 LOG_LSA &identity_stamp_out)
{
  /* Issue the chunk's identity stamp (CBRD-26950): the page LSA read under the write latch the
   * caller already holds and keeps through the slotted-page insert and this chunk's own log append
   * below, so it is the page LSA immediately before this chunk's logging.
   *
   * Invariant 1: the stamp is the page LSA BEFORE this chunk's log append. Redo cannot learn the
   * LSA of the record it is replaying, but the stamp is part of the logged chunk image, so redo
   * (and the undo of a later delete) restores exactly what was written.
   *
   * Invariant 2, and it holds only while logging is enabled: at least one logged page operation
   * separates two incarnations of one slot. The first occupant's own logged insert already advances
   * the page LSA past its stamp, and page LSAs never regress in normal operation, so a later
   * occupant of the same slot always carries a different stamp. Today every chunk insert and every
   * chunk delete is its own log record, including batch inserts; a future optimization that merges
   * several inserts into one log record must preserve this. With logging disabled the append is
   * skipped and leaves the page LSA where it was, so two incarnations of one slot can carry the same
   * stamp; the accepted policy keeps documented no-logging bulk loads working and states the
   * precondition instead of rejecting the write, so a caller must not rely on stamp-based
   * stale-reference discrimination in that mode. Actual logging state is log_is_no_logging(), which
   * SA loaddb --no-logging turns on after startup, so the startup parameter does not answer it.
   *
   * Invariant 3: NULL is an ordinary stamp value with no special handling. Only the offline log
   * re-creation utility produces NULL page LSAs, and it also discards the log and therefore every
   * pending reclamation request, so no stale OOS reference survives into the restarted LSA space. */
  OOS_RECORD_HEADER stamped_header = header;
  stamped_header.identity_stamp = *pgbuf_get_lsa (page_ptr);

  OOS_RECDES oos_recdes{};
  int err = oos_prepend_header (src, stamped_header, oos_recdes);
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
  identity_stamp_out = stamped_header.identity_stamp;
  return NO_ERROR;
}

static int
oos_insert_within_page (THREAD_ENTRY *thread_p, const VFID &oos_vfid, oos_buffer src,
			const OOS_RECORD_HEADER &header,
			OID &oid, LOG_LSA &identity_stamp_out)
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
  const bool page_was_empty = (spage_number_of_records (page_ptr) == 0);
  err = oos_insert_record_in_fixed_page (thread_p, oos_vfid, page_ptr, vpid, src, header, oid, identity_stamp_out);
  if (err != NO_ERROR)
    {
      oos_reclaim_add_if_emptied (oos_vfid, vpid, page_ptr);
      return err;
    }
  if (page_was_empty)
    {
      oos_reclaim_drop_emptied (oos_vfid, vpid);
    }

  /* Update bestspace cache after insert — use spage_max_space_for_new_record
   * for consistency with the lookup check in oos_stats_find_page_in_bestspace */
  int freespace_after = spage_max_space_for_new_record (thread_p, page_ptr);
  (void) oos_stats_add_bestspace (thread_p, &oos_vfid, &vpid, freespace_after);

  return NO_ERROR;
}



/* Fixes the page an OOS OID names, for a read, a delete or a probe, and decides under the latch
 * whether that page can still hold the chunk the OID was issued for (CBRD-26950). An OOS OID is a
 * physical address, so since it was issued the page may have been deallocated, or it may have begun
 * a new page incarnation for another purpose and no longer be a slotted OOS page at all (the file
 * manager, for one, grows its own partial-sector table into a freed page of the same file). Both
 * are reported as page_out == NULL with NO_ERROR and nothing latched, so the caller can treat the
 * target as stale; the page type is checked BEFORE any slotted-page interpretation because the bytes
 * of a retyped page are not a slotted-page header. Only a live OOS page is returned latched; the
 * caller then inspects the slot and, for a chain head, the stamp. Operational failures (I/O,
 * interrupt) are propagated with their error set.
 * The deallocation-tolerant fix first proves the page's sector is still reserved, which is what
 * keeps the debug page-validation level from failing the fetch of a page whose sector was released;
 * that probe costs two extra fixes of cached volume pages per call, on the read path too. */
static int
oos_fix_page_if_oos (THREAD_ENTRY *thread_p, const OID &oid, PGBUF_LATCH_MODE latch_mode, PAGE_PTR &page_out)
{
  VPID vpid = {oid.pageid, oid.volid};

  page_out = NULL;
  int error = pgbuf_fix_if_not_deallocated (thread_p, &vpid, latch_mode, PGBUF_UNCONDITIONAL_LATCH, &page_out);
  if (error != NO_ERROR)
    {
      ASSERT_ERROR ();
      oos_error ("pgbuf_fix_if_not_deallocated failed at oid={vol=%d,page=%d,slot=%d}", OID_AS_ARGS (&oid));
      return error;
    }
  if (page_out == NULL)
    {
      oos_debug ("page deallocated at oid={vol=%d,page=%d,slot=%d}", OID_AS_ARGS (&oid));
      return NO_ERROR;
    }

  const PAGE_TYPE ptype = pgbuf_get_page_ptype (thread_p, page_out);
  if (ptype != PAGE_OOS)
    {
      oos_debug ("page is no longer an OOS page (ptype=%d) at oid={vol=%d,page=%d,slot=%d}", (int) ptype,
		 OID_AS_ARGS (&oid));
      pgbuf_unfix_and_init (thread_p, page_out);
      return NO_ERROR;
    }
  return NO_ERROR;
}


/* Reports a read through a stale reference whose head page is gone: deallocated, or no longer an
 * OOS page. The owning record's OOS metadata disagrees with storage, so both read APIs fail with
 * ER_HEAP_OOS_CORRUPTED_RECORD, the same error a stale slot or stamp produces (CBRD-26950). */
static int
oos_stale_head_page_read_error (const OID &head_oid)
{
  oos_error ("OOS read through a stale reference: head page deallocated or no longer an OOS page at"
	     " oid={vol=%d,page=%d,slot=%d}", OID_AS_ARGS (&head_oid));
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HEAP_OOS_CORRUPTED_RECORD, 0);
  return ER_HEAP_OOS_CORRUPTED_RECORD;
}


/* Reads one chunk from an already-fixed OOS page: copies the chain header to header_out and appends
 * the chunk payload to writer. For the head chunk of a chain the caller passes head_ref and the head
 * checks run on the header BEFORE the payload is copied, so a stale or malformed reference never
 * delivers a byte of the current occupant into the caller's buffer (CBRD-26950). Continuation
 * chunks pass NULL. */
static int
oos_read_chunk_in_page (THREAD_ENTRY *thread_p, PAGE_PTR page_ptr, const OID &oid,
			cubbase::byte_span_writer &writer, OOS_RECORD_HEADER &header_out, const oos_chain_ref *head_ref)
{
  OOS_RECDES oos_recdes;
  SCAN_CODE code = spage_get_record (thread_p, page_ptr, oid.slotid, &oos_recdes, PEEK);
  if (code == S_DOESNT_EXIST)
    {
      /* No record occupies the slot the reference or chain link names: a stale reference whose chunk
       * was reclaimed, or a broken link. Either way the owning record's OOS metadata disagrees with
       * storage. Reported, never asserted: a stale reference is a legal input here. */
      oos_error ("OOS slot holds no record at oid={vol=%d,page=%d,slot=%d}", OID_AS_ARGS (&oid));
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HEAP_OOS_CORRUPTED_RECORD, 0);
      return ER_HEAP_OOS_CORRUPTED_RECORD;
    }
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

  if (head_ref != NULL)
    {
      /* The head is always read into a fresh writer, so its remaining capacity is the caller's inline
       * length; a partially written writer here would silently change what the length check means. */
      assert (writer.written () == 0);
      int err = oos_check_head_header (header_out, static_cast<int> (writer.remaining ()), *head_ref);
      if (err != NO_ERROR)
	{
	  return err;
	}
    }

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



/* Fixes the page of a continuation chunk and reads it. Continuation pages keep the plain fix: the
 * caller has already verified the head chunk's identity, which proves the chain is live and its
 * links intact, so a vanished continuation page is corruption rather than a stale reference. */
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

  return oos_read_chunk_in_page (thread_p, page_ptr, oid, writer, header_out, NULL);
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



/* Head-chunk validation shared by oos_read and oos_read_many, run on the header BEFORE any payload is
 * copied (CBRD-26950). Identity comes first: only when the head chunk carries the stamp the caller's
 * reference was created with may the occupant's other properties be read as properties of the
 * referenced chain. A different stamp means the slot has begun a new slot incarnation, and whatever
 * occupies it now (its chunk index, its length) says nothing about this reference. With identity
 * proven, the chunk must be a chain head (a continuation chunk means a malformed reference) and the
 * caller's inline length must agree with the chain header. Nothing here is asserted: a stale
 * reference is a legal input, and an equal stamp does not prove the reference is well-formed
 * (no-logging operation can repeat stamps), so every mismatch is reported as
 * ER_HEAP_OOS_CORRUPTED_RECORD. */
static int
oos_check_head_header (const OOS_RECORD_HEADER &header, int expected_length, const oos_chain_ref &ref)
{
  const OID &oid = ref.head_oid;

  /* NULL is an ordinary stamp value and is compared like any other. */
  if (!LSA_EQ (&header.identity_stamp, &ref.identity_stamp))
    {
      oos_error ("OOS identity stamp mismatch: reference=%lld|%d head chunk=%lld|%d at oid={vol=%d,page=%d,slot=%d}",
		 LSA_AS_ARGS (&ref.identity_stamp), LSA_AS_ARGS (&header.identity_stamp), OID_AS_ARGS (&oid));
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HEAP_OOS_CORRUPTED_RECORD, 0);
      return ER_HEAP_OOS_CORRUPTED_RECORD;
    }

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



/* Reads the chain ref names into dest. The head page goes through oos_fix_page_if_oos, so a reference
 * whose page was deallocated or retyped fails with ER_HEAP_OOS_CORRUPTED_RECORD instead of tripping
 * the page buffer's dead-page assertion; a missing slot, a stamp mismatch and a non-head target fail
 * the same way inside the chunk read, before any byte is copied. dest.size() is cross-validated
 * against the chain header's total_data_length, and byte_span_writer guards each chunk against
 * payload overflow inside the loop. */
int
oos_read (THREAD_ENTRY *thread_p, const oos_chain_ref &ref, oos_buffer dest)
{
  assert (dest.data () != nullptr && dest.size () > 0);

  const OID &oid = ref.head_oid;
  const int expected_length = static_cast<int> (dest.size ());

  cubbase::byte_span_writer writer (dest);
  OOS_RECORD_HEADER first_header;

  PAGE_PTR page_ptr = NULL;
  int err = oos_fix_page_if_oos (thread_p, oid, PGBUF_LATCH_READ, page_ptr);
  if (err != NO_ERROR)
    {
      return err;
    }
  if (page_ptr == NULL)
    {
      return oos_stale_head_page_read_error (oid);
    }
  {
    scope_exit page_unfixer ([&]()
    {
      pgbuf_unfix_and_init_after_check (thread_p, page_ptr);
    });
    err = oos_read_chunk_in_page (thread_p, page_ptr, oid, writer, first_header, &ref);
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
 * only one OOS page stays fixed at a time (same as the scalar oos_read). A stale
 * reference fails the group with ER_HEAP_OOS_CORRUPTED_RECORD exactly as the scalar
 * read fails: at the head page fix when the page is deallocated or retyped, and in
 * the head checks (identity first) before any payload byte is copied otherwise.
 * The head page fix goes through oos_fix_page_if_oos, so each distinct head page costs
 * the deallocation-tolerant fix's sector probe once per group, not once per request. */
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
      if (OID_ISNULL (&requests[i].ref.head_oid) || requests[i].dest.data () == nullptr
	  || requests[i].dest.size () == 0 || requests[i].dest.size () > (std::size_t) INT_MAX)
	{
	  oos_error ("oos_read_many rejected invalid request %zu (oid={vol=%d,page=%d,slot=%d}, data=%p, size=%zu)",
		     i, OID_AS_ARGS (&requests[i].ref.head_oid), requests[i].dest.data (), requests[i].dest.size ());
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  return ER_GENERIC_ERROR;
	}
    }

  try
    {
      char small_done[8] = { 0 };
      std::vector<char> large_done;
      char *done = small_done;
      if (requests.size () > sizeof (small_done))
	{
	  large_done.resize (requests.size (), 0);
	  done = large_done.data ();
	}
      std::vector<oos_read_continuation> continuations;

      for (std::size_t i = 0; i < requests.size (); i++)
	{
	  if (done[i])
	    {
	      continue;
	    }

	  VPID vpid = { requests[i].ref.head_oid.pageid, requests[i].ref.head_oid.volid };
	  continuations.clear ();

	  {
	    PAGE_PTR page_ptr = NULL;
	    int fix_err = oos_fix_page_if_oos (thread_p, requests[i].ref.head_oid, PGBUF_LATCH_READ, page_ptr);
	    if (fix_err != NO_ERROR)
	      {
		return fix_err;
	      }
	    if (page_ptr == nullptr)
	      {
		return oos_stale_head_page_read_error (requests[i].ref.head_oid);
	      }
	    scope_exit page_unfixer ([&]()
	    {
	      pgbuf_unfix_and_init_after_check (thread_p, page_ptr);
	    });

	    OOS_COUNTER_INC (read_many_grouped_head_pages);

	    /* Resolve every request whose head chunk lives on this fixed page. */
	    for (std::size_t j = i; j < requests.size (); j++)
	      {
		const VPID request_vpid = { requests[j].ref.head_oid.pageid, requests[j].ref.head_oid.volid };
		if (done[j] || !VPID_EQ (&request_vpid, &vpid))
		  {
		    continue;
		  }
		done[j] = true;
		OOS_COUNTER_INC (read_values_per_fixed_page);

		cubbase::byte_span_writer writer (requests[j].dest);
		OOS_RECORD_HEADER header;

		int err = oos_read_chunk_in_page (thread_p, page_ptr, requests[j].ref.head_oid, writer, header,
						  &requests[j].ref);
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
			       writer.written (), requests[j].dest.size (), OID_AS_ARGS (&requests[j].ref.head_oid));
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
			     OID_AS_ARGS (&request.ref.head_oid));
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
	  oos_hdr_page_set_dirty_unlogged (thread_p, hdr_page);
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
      oos_hdr_page_set_dirty_unlogged (thread_p, hdr_page);
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

  /* The undo image is the file: the undo deletes the chunk, may empty the page, and must name the
   * file whose emptied-page set takes it (oos_rv_undo_insert). */
  VFID undo_vfid;
  std::memset (&undo_vfid, 0, sizeof (undo_vfid));
  undo_vfid.fileid = vfid_p->fileid;
  undo_vfid.volid = vfid_p->volid;
  LOG_CRUMB undo_crumb = { (int) sizeof (undo_vfid), &undo_vfid };
  LOG_CRUMB redo_crumbs[2] = { { (int) sizeof (recdes_p->type), &recdes_p->type },
    { recdes_p->length, recdes_p->data }
  };
  log_append_undoredo_crumbs (thread_p, RVOOS_INSERT, &log_addr, 1, 2, &undo_crumb, redo_crumbs);
}

/*
 * oos_log_delete_physical () - log the reclaimer's delete of one chunk: redo-only, with no undo image,
 *   because a reclaimed chunk is never written back (ADR-0008)
 *   thread_p(in): thread entry
 *   page_p(in): page where the delete is performed
 *   vfid_p(in): OOS file identifier
 *   slotid(in): slot id of the chunk to delete
 */
static void
oos_log_delete_physical (THREAD_ENTRY *thread_p, PAGE_PTR page_p, VFID *vfid_p, PGSLOTID slotid)
{
  LOG_DATA_ADDR log_addr;

  log_addr.vfid = vfid_p;
  log_addr.offset = slotid;
  log_addr.pgptr = page_p;

  log_append_redo_data (thread_p, RVOOS_DELETE, &log_addr, 0, NULL);
}

/* Where the chunk an expected reference names stands now. */
enum oos_chunk_probe
{
  OOS_CHUNK_PRESENT,		/* the slot holds a chunk carrying the expected stamp */
  OOS_CHUNK_PAGE_GONE,		/* the page is deallocated or no longer an OOS page */
  OOS_CHUNK_SLOT_EMPTY,		/* the page is an OOS page, but the slot holds no record */
  OOS_CHUNK_STAMP_MISMATCH	/* the slot holds a chunk that carries another stamp */
};

static oos_delete_outcome
oos_skip_outcome (oos_chunk_probe probe)
{
  switch (probe)
    {
    case OOS_CHUNK_PAGE_GONE:
      return OOS_DELETE_SKIPPED_PAGE_GONE;
    case OOS_CHUNK_SLOT_EMPTY:
      return OOS_DELETE_SKIPPED_SLOT_EMPTY;
    case OOS_CHUNK_STAMP_MISMATCH:
      return OOS_DELETE_SKIPPED_STAMP_MISMATCH;
    case OOS_CHUNK_PRESENT:
      break;
    }
  assert (false);
  return OOS_DELETE_OUTCOME_UNKNOWN;
}

/*
 * oos_fix_expected_chunk () - fix the page of one expected chunk and check that the chunk is still there
 *
 *   return: NO_ERROR or error code
 *   thread_p(in): thread entry
 *   expected(in): chunk OID and the identity stamp it must carry
 *   latch_mode(in): latch to hold on the page
 *   page_out(out): the page, latched, when probe_out is OOS_CHUNK_PRESENT; NULL otherwise
 *   header_out(out): the chunk header, when probe_out is OOS_CHUNK_PRESENT
 *   probe_out(out): where the chunk stands
 *
 * The page is fixed as possibly deallocated and its type is checked under the latch before it is read
 * as a slotted page, for the head and every continuation chunk alike: a rerun may follow a link whose
 * page was reclaimed and reused since the first run (ADR-0008).
 */
static int
oos_fix_expected_chunk (THREAD_ENTRY *thread_p, const oos_chain_ref &expected, PGBUF_LATCH_MODE latch_mode,
			PAGE_PTR &page_out, OOS_RECORD_HEADER &header_out, oos_chunk_probe &probe_out)
{
  page_out = NULL;
  int error = oos_fix_page_if_oos (thread_p, expected.head_oid, latch_mode, page_out);
  if (error != NO_ERROR)
    {
      return error;
    }
  if (page_out == NULL)
    {
      er_clear ();
      probe_out = OOS_CHUNK_PAGE_GONE;
      return NO_ERROR;
    }

  OOS_RECDES oos_recdes = RECDES_INITIALIZER;
  SCAN_CODE code = spage_get_record (thread_p, page_out, expected.head_oid.slotid, &oos_recdes, PEEK);
  if (code == S_DOESNT_EXIST)
    {
      er_clear ();
      pgbuf_unfix_and_init (thread_p, page_out);
      probe_out = OOS_CHUNK_SLOT_EMPTY;
      return NO_ERROR;
    }
  if (code != S_SUCCESS)
    {
      ASSERT_ERROR_AND_SET (error);
      oos_error ("spage_get_record failed for volid=%d, pageid=%d, slotid=%d", OID_AS_ARGS (&expected.head_oid));
      pgbuf_unfix_and_init (thread_p, page_out);
      return error;
    }
  if (oos_recdes.length < OOS_RECORD_HEADER_SIZE)
    {
      assert_release (false);
      oos_error ("OOS record at volid=%d pageid=%d slotid=%d has invalid length %d",
		 OID_AS_ARGS (&expected.head_oid), oos_recdes.length);
      pgbuf_unfix_and_init (thread_p, page_out);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HEAP_OOS_CORRUPTED_RECORD, 0);
      return ER_HEAP_OOS_CORRUPTED_RECORD;
    }

  std::memcpy (&header_out, oos_recdes.data, OOS_RECORD_HEADER_SIZE);
  if (!LSA_EQ (&header_out.identity_stamp, &expected.identity_stamp))
    {
      er_clear ();
      pgbuf_unfix_and_init (thread_p, page_out);
      probe_out = OOS_CHUNK_STAMP_MISMATCH;
      return NO_ERROR;
    }

  probe_out = OOS_CHUNK_PRESENT;
  return NO_ERROR;
}

/*
 * oos_collect_chain_prefix () - read the chunks of a chain that are still linked from its head
 *
 *   return: NO_ERROR or error code
 *   thread_p(in): thread entry
 *   ref(in): head OOS OID and identity stamp of the chain
 *   chain_out(out): the head and every chunk that can be reached from it, in chain order, each with
 *		     the stamp it carries; empty when the head itself is gone
 *   head_probe_out(out): where the head stands
 *
 * The walk follows a link only while the slot it names carries the stamp stored in the link, and stops
 * at the first absent, deallocated, retyped or mismatching slot. The reclaimer deletes tail first and
 * the head last, so after any interruption the chunks still present form exactly this prefix.
 */
static int
oos_collect_chain_prefix (THREAD_ENTRY *thread_p, const oos_chain_ref &ref, std::vector<oos_chain_ref> &chain_out,
			  oos_chunk_probe &head_probe_out)
{
  chain_out.clear ();

  oos_chain_ref expected = ref;
  /* A chain holds at most one chunk per DB_PAGESIZE of payload. The bound keeps a corrupted link cycle
   * from looping forever. */
  std::size_t max_chunks = 1;
  int expected_index = 0;

  while (true)
    {
      PAGE_PTR page_ptr = NULL;
      OOS_RECORD_HEADER header;
      oos_chunk_probe probe = OOS_CHUNK_PAGE_GONE;
      int error = oos_fix_expected_chunk (thread_p, expected, PGBUF_LATCH_READ, page_ptr, header, probe);
      if (error != NO_ERROR)
	{
	  return error;
	}
      if (chain_out.empty ())
	{
	  head_probe_out = probe;
	}
      if (probe != OOS_CHUNK_PRESENT)
	{
	  /* The head is gone (a skip), or the rest of the chain was already reclaimed. */
	  return NO_ERROR;
	}
      pgbuf_unfix_and_init (thread_p, page_ptr);

      if (header.chunk_index != expected_index)
	{
	  /* The stamp matched, so the slot holds the chunk this link was written for; another index means
	   * the reference or the chain is malformed. Reject before anything is modified. */
	  oos_error ("OOS reclaim rejected: chunk at oid={vol=%d,page=%d,slot=%d} has chunk_index=%d, expected %d",
		     OID_AS_ARGS (&expected.head_oid), header.chunk_index, expected_index);
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HEAP_OOS_CORRUPTED_RECORD, 0);
	  return ER_HEAP_OOS_CORRUPTED_RECORD;
	}
      if (expected_index == 0)
	{
	  max_chunks = (std::size_t) MAX (header.total_data_length, 0) / (std::size_t) (DB_PAGESIZE / 2) + 2;
	}
      if (chain_out.size () >= max_chunks)
	{
	  oos_error ("OOS reclaim rejected: chain at oid={vol=%d,page=%d,slot=%d} has more than %zu chunks",
		     OID_AS_ARGS (&ref.head_oid), max_chunks);
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HEAP_OOS_CORRUPTED_RECORD, 0);
	  return ER_HEAP_OOS_CORRUPTED_RECORD;
	}

      try
	{
	  chain_out.push_back (expected);
	}
      catch (std::bad_alloc &)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (oos_chain_ref));
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}

      if (OID_ISNULL (&header.next_chunk_oid))
	{
	  return NO_ERROR;
	}
      expected.head_oid = header.next_chunk_oid;
      expected.identity_stamp = header.next_chunk_stamp;
      expected_index++;
    }
}

/*
 * oos_delete_chain () - the OOS reclaimer: delete every chunk of a verified chain, tail first and the
 *   head last, with redo-only records (internal)
 *
 *   return: NO_ERROR or error code
 *   thread_p(in): thread entry
 *   oos_vfid(in): OOS file identifier
 *   ref(in): head OID of the chain plus the identity stamp the caller's OOS inline stub carries
 *   emptied_vpids(out): optional; every page that holds zero records after one of this chain's
 *			 chunks was deleted from it is appended, once. A page that still holds
 *			 other chunks is not a reclaim candidate and is not reported.
 *   outcome_out(out): OOS_DELETE_RECLAIMED when this call deleted the head, which stale condition made
 *		       the call a no-op, or OOS_DELETE_OUTCOME_UNKNOWN when the call failed
 *
 * NOTE: The caller has committed the decision that the chain is dead, so nothing will ever restore a
 *       deleted chunk and the slot it frees is reusable at once (ADR-0008 decision 1). The steps:
 *
 *       1. Walk from the head, comparing the head's stamp with ref.identity_stamp and each next chunk's
 *          stamp with the stamp stored in its link, and stop at the first absent, deallocated, retyped
 *          or mismatching slot (oos_collect_chain_prefix). A missing or mismatching head is a clean skip.
 *       2. Delete the collected chunks from the last to the head, each under the write latch that
 *          re-proves its stamp. A chunk already gone at that point was deleted by a concurrent
 *          reclaimer of the same chain (a duplicate notification or a block retry); it is passed over.
 *
 *       Deleting the head last keeps the invariant every rerun relies on: until the head is deleted,
 *       the chunks left form a linked prefix starting at an intact head, so a rerun after an error or a
 *       crash finds and finishes them; once the head is deleted, nothing is left. Link stamps keep a
 *       rerun from following a link into a slot that a later insert reused.
 *
 *       Target identity (CBRD-26950) needs the chain to have been written with logging enabled, which
 *       is what makes two occupants of one slot carry different stamps; see the invariants at the
 *       issuing site. A reference whose stamp matches but whose target is a continuation chunk is
 *       malformed and is rejected with ER_HEAP_OOS_CORRUPTED_RECORD before anything is modified.
 */
static int
oos_delete_chain (THREAD_ENTRY *thread_p, const VFID &oos_vfid, const oos_chain_ref &ref,
		  std::vector<VPID> *emptied_vpids, oos_delete_outcome *outcome_out)
{
  *outcome_out = OOS_DELETE_OUTCOME_UNKNOWN;

  if (OID_ISNULL (&ref.head_oid))
    {
      /* A chain reference must name a head chunk. The stub parser rejects a null head OOS OID with
       * ER_HEAP_OOS_BAD_INLINE_HEADER, so no heap-derived reference arrives here with one; a caller that
       * passes one has an invalid argument, not a stale target, and must not be told a chain was
       * reclaimed. */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HEAP_OOS_INVALID_ARGUMENT, 0);
      return ER_HEAP_OOS_INVALID_ARGUMENT;
    }

  std::vector<oos_chain_ref> chain;
  oos_chunk_probe head_probe = OOS_CHUNK_PAGE_GONE;
  int error = oos_collect_chain_prefix (thread_p, ref, chain, head_probe);
  if (error != NO_ERROR)
    {
      return error;
    }
  if (head_probe != OOS_CHUNK_PRESENT)
    {
      oos_debug ("delete no-op at oid={vol=%d,page=%d,slot=%d}: %s (expected identity_stamp %lld|%d)",
		 OID_AS_ARGS (&ref.head_oid), oos_delete_outcome_string (oos_skip_outcome (head_probe)),
		 LSA_AS_ARGS (&ref.identity_stamp));
      *outcome_out = oos_skip_outcome (head_probe);
      return NO_ERROR;
    }

  oos_chunk_probe last_probe = OOS_CHUNK_PRESENT;
  for (std::size_t i = chain.size (); i-- > 0;)
    {
      const oos_chain_ref &chunk = chain[i];
      PAGE_PTR page_ptr = NULL;
      OOS_RECORD_HEADER header;
      oos_chunk_probe probe = OOS_CHUNK_PAGE_GONE;

      error = oos_fix_expected_chunk (thread_p, chunk, PGBUF_LATCH_WRITE, page_ptr, header, probe);
      if (error != NO_ERROR)
	{
	  return error;
	}
      last_probe = probe;
      if (probe != OOS_CHUNK_PRESENT)
	{
	  continue;
	}

      scope_exit page_unfixer ([&] ()
      {
	pgbuf_unfix_and_init_after_check (thread_p, page_ptr);
      });

#if defined(CUBRID_UNIT_TEST_ENABLED)
      if (oos_Test_fail_reclaim_after_chunk_deletes.load (std::memory_order_relaxed) == 0)
	{
	  oos_Test_fail_reclaim_after_chunk_deletes.store (-1, std::memory_order_relaxed);
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  return ER_GENERIC_ERROR;
	}
      if (oos_Test_fail_reclaim_after_chunk_deletes.load (std::memory_order_relaxed) > 0)
	{
	  oos_Test_fail_reclaim_after_chunk_deletes.fetch_sub (1, std::memory_order_relaxed);
	}
#endif

      oos_log_delete_physical (thread_p, page_ptr, const_cast<VFID *> (&oos_vfid), chunk.head_oid.slotid);

      PGSLOTID deleted_slotid = spage_delete (thread_p, page_ptr, chunk.head_oid.slotid);
      if (deleted_slotid == NULL_SLOTID)
	{
	  ASSERT_ERROR_AND_SET (error);
	  oos_error ("spage_delete failed for volid=%d, pageid=%d, slotid=%d", OID_AS_ARGS (&chunk.head_oid));
	  return error;
	}
      pgbuf_set_dirty (thread_p, page_ptr, DONT_FREE);

      /* Update bestspace cache — page now has more free space after delete */
      oos_stats_update (thread_p, page_ptr, &oos_vfid);

      /* Report the page only when this delete emptied it: no reclaim can deallocate anything else,
       * and a page empties at most once per delete, so this also keeps the list free of
       * duplicates. */
      const VPID chunk_vpid = {chunk.head_oid.pageid, chunk.head_oid.volid};
      oos_reclaim_add_if_emptied (oos_vfid, chunk_vpid, page_ptr);
      if (emptied_vpids != NULL && spage_number_of_records (page_ptr) == 0)
	{
	  try
	    {
	      emptied_vpids->push_back (chunk_vpid);
	    }
	  catch (std::bad_alloc &)
	    {
	      /* The candidate list is only the fast path; the file's emptied-page set already holds
	       * the pages. Never fail a reclaim over it. */
	      oos_warn ("dropping empty-page reclaim hints for file %d|%d: out of memory growing "
			"the candidate list; the next growth reclaims the pages",
			VFID_AS_ARGS (&oos_vfid));
	      emptied_vpids->clear ();
	      emptied_vpids = NULL;
	    }
	}

      oos_debug ("deleted chunk %zu at oid={vol=%d,page=%d,slot=%d}", i, OID_AS_ARGS (&chunk.head_oid));
    }

  /* last_probe is the head's: the loop ends with it. A concurrent reclaimer of the same chain may have
   * deleted the head between the walk and here; then this call reclaimed nothing of its own. */
  *outcome_out = last_probe == OOS_CHUNK_PRESENT ? OOS_DELETE_RECLAIMED : oos_skip_outcome (last_probe);
  return NO_ERROR;
}

/*
 * oos_chunk_exists () - Occupancy probe: does SOME record still occupy the slot at oid? Kept for
 *   tests and diagnostics. It proves occupancy, not identity: OOS OIDs are physical addresses and
 *   freed slots are reused, so a "present" answer cannot tell the chunk a reference was created for
 *   from a later occupant of the same slot. It must never gate a delete; oos_delete verifies target
 *   identity itself, under the latch that performs the delete (CBRD-26950).
 *
 *   "Already gone" is narrowly defined:
 *     - oos_fix_page_if_oos reports no page (page deallocated, or no longer an OOS page), OR
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

  PAGE_PTR page_ptr = NULL;
  int error_code = oos_fix_page_if_oos (thread_p, oid, PGBUF_LATCH_READ, page_ptr);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }
  if (page_ptr == NULL)
    {
      /* Page legitimately deallocated or no longer an OOS page; no chunk can be there. */
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
 * oos_delete () - delete an OOS value chain (single-chunk or multi-chunk) after verifying that the
 *   head chunk is still the one the caller's reference was created for
 *
 *   return: NO_ERROR or error code
 *   thread_p(in): thread entry
 *   oos_vfid(in): OOS file identifier
 *   ref(in): head OOS OID plus the identity stamp stored in the caller's OOS inline stub
 *   emptied_vpids(out): optional reclaim candidate list, see oos_file.hpp
 *   outcome_out(out): optional; reclaimed, which stale condition was skipped, or unknown after a
 *		       failure, see oos_file.hpp
 *
 * NOTE: The delete happens only when the head chunk's stored identity stamp equals
 *       ref.identity_stamp and the chunk is a chain head. A deallocated head page, a head page that
 *       is no longer an OOS page (reclaimed and reused for another purpose), a missing head slot (a
 *       block retry re-deleting what an earlier committed sysop already reclaimed) and a stamp
 *       mismatch (the slot was freed and reused by a live chain) are all successful no-ops that
 *       modify nothing, leave the error stack clean and report no reclaim candidate. Without this
 *       identity check a retried delete would destroy the reusing chain's data (CBRD-26950). It
 *       discriminates the two occupants only for chains written with logging enabled: a skipped log
 *       append does not advance the page LSA, so two occupants of one slot can then carry the same
 *       stamp (see the invariants at the issuing site). A reference whose stamp matches a
 *       continuation chunk is malformed and fails with ER_HEAP_OOS_CORRUPTED_RECORD before any chunk
 *       is touched.
 *
 *       Every chunk delete is a redo-only record and is never undone, so the caller must have
 *       committed the decision that the chain is dead (ADR-0008): a reclaim postpone, or vacuum's
 *       RVOOS_NOTIFY_VACUUM consumer. An error leaves the linked prefix that a rerun finishes; the
 *       postpone callers stop the server rather than report the reclaim as done (fail-stop), so
 *       restart recovery reruns their durable postpone.
 *
 *       Page deallocation is NOT done here. Every page the delete empties enters the file's
 *       emptied-page set, which the next growth checks; vacuum also reclaims them at once via
 *       oos_reclaim_empty_pages after unfixing its heap page.
 */
int
oos_delete (THREAD_ENTRY *thread_p, const VFID &oos_vfid, const oos_chain_ref &ref,
	    std::vector<VPID> *emptied_vpids, oos_delete_outcome *outcome_out)
{
  oos_debug ("arguments: oid={vol=%d,page=%d,slot=%d}, expected identity_stamp=%lld|%d",
	     OID_AS_ARGS (&ref.head_oid), LSA_AS_ARGS (&ref.identity_stamp));

  oos_delete_outcome outcome = OOS_DELETE_OUTCOME_UNKNOWN;
  int err = oos_delete_chain (thread_p, oos_vfid, ref, emptied_vpids != NULL ? emptied_vpids : oos_Emptied_page_sink,
			      &outcome);
  if (outcome_out != NULL)
    {
      *outcome_out = outcome;
    }
  return err;
}

/*
 * oos_delete_outcome_string () - Short identifier-shaped name of an oos_delete outcome. It is embedded in
 *   a localized diagnostic message, so it stays a label a reader of any locale can match against this
 *   list, not an English sentence; the human-readable wording lives in the oos_debug lines above.
 */
const char *
oos_delete_outcome_string (oos_delete_outcome outcome)
{
  switch (outcome)
    {
    case OOS_DELETE_OUTCOME_UNKNOWN:
      return "unknown";
    case OOS_DELETE_RECLAIMED:
      return "reclaimed";
    case OOS_DELETE_SKIPPED_PAGE_GONE:
      return "page_gone";
    case OOS_DELETE_SKIPPED_SLOT_EMPTY:
      return "slot_empty";
    case OOS_DELETE_SKIPPED_STAMP_MISMATCH:
      return "stamp_mismatch";
    }

  /* No default label, so a new outcome without a name here is a -Wswitch warning rather than a surprise. */
  assert (false);
  return "unknown";
}

// ****************************************************************************
// Reclaim intents (ADR-0008)
// ****************************************************************************

/* Fixed part of a reclaim intent payload, followed by n_refs oos_chain_ref entries. The payload is
 * copied byte-wise in and out of log data, which carries no alignment guarantee. */
struct oos_reclaim_intent_header
{
  VFID oos_vfid;		/* NULL in an UPDATE's intent: vacuum resolves it from heap_hfid */
  HFID heap_hfid;		/* heap file of the owner; may be NULL when oos_vfid is set */
  OID owner_oid;		/* the heap record that held the references, for diagnostics; may be NULL */
  INT32 flags;			/* oos_reclaim_intent_flag */
  INT32 n_refs;
  char op_ctx[24];		/* call site that decided, for diagnostics; NUL-terminated, may be truncated */
};

/* Fixed prefix of an RVOOS_UPDATE_DROP_POSTPONE payload, followed by a reclaim intent payload. */
struct oos_update_drop_prefix
{
  MVCCID mvccid;		/* the UPDATE's MVCCID; vacuum reclaims once it precedes every snapshot */
};

oos_emptied_page_sink::oos_emptied_page_sink (std::vector<VPID> &pages)
  : m_previous (oos_Emptied_page_sink)
{
  oos_Emptied_page_sink = &pages;
}

oos_emptied_page_sink::~oos_emptied_page_sink ()
{
  oos_Emptied_page_sink = m_previous;
}

static int
oos_pack_reclaim_intent (const VFID &oos_vfid, const HFID &heap_hfid, const OID &owner_oid, int flags,
			 const char *op_ctx, const std::vector<oos_chain_ref> &refs, std::size_t prefix_size,
			 std::vector<char> &payload_out)
{
  oos_reclaim_intent_header header {};
  std::memset (&header, 0, sizeof (header));
  header.oos_vfid = oos_vfid;
  header.heap_hfid = heap_hfid;
  header.owner_oid = owner_oid;
  header.flags = flags;
  header.n_refs = (INT32) refs.size ();
  if (op_ctx != NULL)
    {
      std::strncpy (header.op_ctx, op_ctx, sizeof (header.op_ctx) - 1);
    }

  try
    {
      payload_out.resize (prefix_size + sizeof (header) + refs.size () * sizeof (oos_chain_ref));
    }
  catch (std::bad_alloc &)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      prefix_size + sizeof (header) + refs.size () * sizeof (oos_chain_ref));
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  std::memcpy (payload_out.data () + prefix_size, &header, sizeof (header));
  if (!refs.empty ())
    {
      std::memcpy (payload_out.data () + prefix_size + sizeof (header), refs.data (),
		   refs.size () * sizeof (oos_chain_ref));
    }
  return NO_ERROR;
}

/* Parses a reclaim intent payload. A malformed payload is corruption of the log record. */
static int
oos_unpack_reclaim_intent (const char *data, int length, oos_reclaim_intent_header &header_out,
			   std::vector<oos_chain_ref> &refs_out)
{
  if (data == NULL || length < (int) sizeof (header_out))
    {
      assert_release (false);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return ER_GENERIC_ERROR;
    }
  std::memcpy (&header_out, data, sizeof (header_out));
  header_out.op_ctx[sizeof (header_out.op_ctx) - 1] = '\0';
  if (header_out.n_refs < 0
      || (std::size_t) length != sizeof (header_out) + (std::size_t) header_out.n_refs * sizeof (oos_chain_ref))
    {
      assert_release (false);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return ER_GENERIC_ERROR;
    }
  try
    {
      refs_out.resize ((std::size_t) header_out.n_refs);
    }
  catch (std::bad_alloc &)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      (std::size_t) header_out.n_refs * sizeof (oos_chain_ref));
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  if (header_out.n_refs > 0)
    {
      std::memcpy (refs_out.data (), data + sizeof (header_out),
		   (std::size_t) header_out.n_refs * sizeof (oos_chain_ref));
    }
  return NO_ERROR;
}

/*
 * oos_log_reclaim_intent () - record, as a postpone of the current unit, that the chains refs name are
 *   dead and are reclaimed when the unit commits (REMOVE and the SA/non-MVCC eager path)
 *
 *   return: NO_ERROR or error code
 *   thread_p(in): thread entry
 *   oos_vfid(in): OOS file of the chains
 *   owner_oid(in): heap record that held the references, for diagnostics; may be NULL
 *   flags(in): oos_reclaim_intent_flag
 *   op_ctx(in): short call-site tag for diagnostics, e.g. "delete home"; may be NULL
 *   refs(in): chain references
 *
 * The postpone belongs to the innermost open system operation, or to the transaction when none is open.
 * Its commit runs oos_rv_reclaim_on_postpone; its abort, including a savepoint or statement rollback,
 * discards it and the chains stay. With logging off the postpone runs at once, as every postpone does.
 */
int
oos_log_reclaim_intent (THREAD_ENTRY *thread_p, const VFID &oos_vfid, const OID &owner_oid, int flags,
			const char *op_ctx, const std::vector<oos_chain_ref> &refs)
{
  if (refs.empty ())
    {
      return NO_ERROR;
    }
  assert (!VFID_ISNULL (&oos_vfid));

  std::vector<char> payload;
  HFID no_hfid = HFID_INITIALIZER;
  int error = oos_pack_reclaim_intent (oos_vfid, no_hfid, owner_oid, flags, op_ctx, refs, 0, payload);
  if (error != NO_ERROR)
    {
      return error;
    }

  LOG_DATA_ADDR addr (NULL, NULL, -1);
  log_append_postpone (thread_p, RVOOS_RECLAIM_POSTPONE, &addr, (int) payload.size (), payload.data ());
  return NO_ERROR;
}

/*
 * oos_log_update_drop_intent () - record, as a postpone of the current transaction, that its MVCC UPDATE
 *   dropped the chains refs name (A', ADR-0008 decision 2)
 *
 *   return: NO_ERROR or error code
 *   thread_p(in): thread entry
 *   heap_hfid(in): heap file of the updated record
 *   refs(in): references the old record held and the new one does not
 *
 * The postpone carries the UPDATE's MVCCID, because the transaction completes its MVCC info before its
 * postpones run. After the commit it appends RVOOS_NOTIFY_VACUUM under that MVCCID, and vacuum
 * reclaims the chains once no snapshot can still read the old version. A statement, savepoint or
 * transaction rollback discards the postpone, so a rolled-back UPDATE never reclaims its old chains.
 * The caller holds heap data pages; vacuum, which holds none, resolves the OOS file from heap_hfid, so the
 * UPDATE never fixes the heap header page here.
 */
int
oos_log_update_drop_intent (THREAD_ENTRY *thread_p, const HFID &heap_hfid, const std::vector<oos_chain_ref> &refs)
{
  if (refs.empty ())
    {
      return NO_ERROR;
    }
  assert (!HFID_IS_NULL (&heap_hfid));

  oos_update_drop_prefix prefix;
  prefix.mvccid = logtb_get_current_mvccid (thread_p);
  if (!MVCCID_IS_VALID (prefix.mvccid))
    {
      assert_release (false);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return ER_GENERIC_ERROR;
    }

  std::vector<char> payload;
  VFID no_oos_vfid = VFID_INITIALIZER;
  int error = oos_pack_reclaim_intent (no_oos_vfid, heap_hfid, oid_Null_oid, 0, "mvcc update", refs, sizeof (prefix),
				       payload);
  if (error != NO_ERROR)
    {
      return error;
    }
  std::memcpy (payload.data (), &prefix, sizeof (prefix));

  LOG_DATA_ADDR addr (NULL, NULL, -1);
  log_append_postpone (thread_p, RVOOS_UPDATE_DROP_POSTPONE, &addr, (int) payload.size (), payload.data ());
  return NO_ERROR;
}

/* Stops the server when a reclaim that the caller already committed cannot finish. The run-postpone
 * record that would mark the reclaim done is not written, so restart recovery reruns the durable
 * postpone and the reclaimer finishes the linked prefix it left (fail-stop policy, 2026-11-05).
 * Reporting the reclaim as done would leak the chunks for good. */
static void
oos_reclaim_fail_stop (THREAD_ENTRY *thread_p, int error, const VFID &oos_vfid, const oos_chain_ref &ref)
{
  oos_error ("OOS reclaim of committed intent failed (error %d) for file %d|%d chain oid={vol=%d,page=%d,slot=%d}"
	     " stamp %lld|%d; stopping so restart recovery reruns it", error, VFID_AS_ARGS (&oos_vfid),
	     OID_AS_ARGS (&ref.head_oid), LSA_AS_ARGS (&ref.identity_stamp));
#if defined(CUBRID_UNIT_TEST_ENABLED)
  if (oos_Test_reclaim_fail_stop_hook != NULL)
    {
      /* The test continues where the server would stop; keep the run-postpone system operation non-empty. */
      oos_Test_reclaim_fail_stop_hook (error);
      log_append_empty_record (thread_p, LOG_DUMMY_GENERIC, NULL);
      return;
    }
#endif
  logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "oos_reclaim: committed reclaim failed with error %d", error);
}

/*
 * oos_rv_reclaim_on_postpone () - run a committed reclaim intent (RVOOS_RECLAIM_POSTPONE)
 *
 *   return: NO_ERROR
 *   thread_p(in): thread entry
 *   rcv(in): the intent payload
 *
 * Runs after the deciding unit committed, at runtime or as a rerun by restart recovery. The reclaimer
 * converges on rerun, so a rerun of a finished or partly finished reclaim is safe. An error does not
 * return: it stops the server before the postpone is marked done (oos_reclaim_fail_stop).
 */
int
oos_rv_reclaim_on_postpone (THREAD_ENTRY *thread_p, LOG_RCV *rcv)
{
  oos_reclaim_intent_header header {};
  std::vector<oos_chain_ref> refs;
  int error = oos_unpack_reclaim_intent (rcv->data, rcv->length, header, refs);
  if (error != NO_ERROR)
    {
      oos_reclaim_fail_stop (thread_p, error, header.oos_vfid, oos_chain_ref {oid_Null_oid, NULL_LSA});
      return NO_ERROR;
    }

  /* A commit is not interruptible: an interrupt here would stop the server. For the same reason the page latches
   * wait even for a no-wait transaction (lock timeout 0), which would otherwise get a conditional latch and fail on
   * a busy page. */
  bool save_check_interrupt = logtb_set_check_interrupt (thread_p, false);
  int save_wait_msecs = pgbuf_force_latch_wait (thread_p);

  int skipped_count = 0;
  oos_chain_ref first_skipped_ref = {oid_Null_oid, NULL_LSA};
  oos_delete_outcome first_skipped_outcome = OOS_DELETE_OUTCOME_UNKNOWN;
  for (const oos_chain_ref &ref : refs)
    {
      oos_delete_outcome outcome = OOS_DELETE_OUTCOME_UNKNOWN;
      error = oos_delete (thread_p, header.oos_vfid, ref, NULL, &outcome);
      if (error != NO_ERROR)
	{
	  (void) logtb_set_thread_wait_msecs (thread_p, save_wait_msecs);
	  (void) logtb_set_check_interrupt (thread_p, save_check_interrupt);
	  oos_reclaim_fail_stop (thread_p, error, header.oos_vfid, ref);
	  return NO_ERROR;
	}
      if (outcome != OOS_DELETE_RECLAIMED)
	{
	  if (skipped_count++ == 0)
	    {
	      first_skipped_ref = ref;
	      first_skipped_outcome = outcome;
	    }
	}
    }

  (void) logtb_set_thread_wait_msecs (thread_p, save_wait_msecs);
  (void) logtb_set_check_interrupt (thread_p, save_check_interrupt);

  if (skipped_count == (int) refs.size ())
    {
      /* Nothing was deleted, as on the rerun of a finished reclaim. The run-postpone system operation around
       * this call must not be empty (log_sysop_commit_internal). */
      log_append_empty_record (thread_p, LOG_DUMMY_GENERIC, NULL);
    }

  /* A restart rerun finds the chains its first run reclaimed gone; that is expected and stays quiet. */
  if (skipped_count > 0 && (header.flags & OOS_RECLAIM_DIAGNOSE_SKIPS) && LOG_ISRESTARTED ())
    {
      heap_oos_diagnose_skipped_cleanup (header.owner_oid, header.op_ctx, first_skipped_ref, first_skipped_outcome,
					 skipped_count);
    }
  return NO_ERROR;
}

/*
 * oos_rv_update_drop_on_postpone () - publish a committed UPDATE's dropped chains to vacuum
 *   (RVOOS_UPDATE_DROP_POSTPONE)
 *
 *   return: NO_ERROR
 *   thread_p(in): thread entry
 *   rcv(in): the drop payload
 *
 * Appends RVOOS_NOTIFY_VACUUM under the UPDATE's MVCCID. The caller, log_execute_run_postpone, wraps
 * this in a system operation that it ends as the run of this postpone, so a crash before that end
 * reruns it. A rerun may append a second notification for the same chains; the second reclaim finds
 * them gone and skips.
 */
int
oos_rv_update_drop_on_postpone (THREAD_ENTRY *thread_p, LOG_RCV *rcv)
{
  oos_update_drop_prefix prefix;
  oos_reclaim_intent_header header {};
  std::vector<oos_chain_ref> refs;

  int error = ER_GENERIC_ERROR;
  if (rcv->length > (int) sizeof (prefix))
    {
      std::memcpy (&prefix, rcv->data, sizeof (prefix));
      error = oos_unpack_reclaim_intent (rcv->data + sizeof (prefix), rcv->length - (int) sizeof (prefix), header,
					 refs);
    }
  if (error != NO_ERROR || !MVCCID_IS_VALID (prefix.mvccid) || HFID_IS_NULL (&header.heap_hfid))
    {
      oos_reclaim_fail_stop (thread_p, error != NO_ERROR ? error : ER_GENERIC_ERROR, VFID {NULL_FILEID, NULL_VOLID},
			     oos_chain_ref {oid_Null_oid, NULL_LSA});
      return NO_ERROR;
    }

  /* vacuum_info.vfid is the heap file: vacuum skips the notification when the table was dropped. */
  LOG_DATA_ADDR addr (&header.heap_hfid.vfid, NULL, -1);
  log_append_mvcc_undo_data_with_mvccid (thread_p, RVOOS_NOTIFY_VACUUM, &addr, rcv->length - (int) sizeof (prefix),
					 rcv->data + sizeof (prefix), prefix.mvccid);
#if defined(CUBRID_UNIT_TEST_ENABLED)
  if (oos_Test_exit_after_update_drop_notifications.load (std::memory_order_relaxed) > 0
      && oos_Test_exit_after_update_drop_notifications.fetch_sub (1, std::memory_order_relaxed) == 1)
    {
      /* A crash in the middle of the commit's postpones, after this notification reached the disk. */
      logpb_force_flush_pages (thread_p);
      std::_Exit (oos_Test_exit_after_update_drop_notifications_code);
    }
#endif
  return NO_ERROR;
}

/*
 * oos_reclaim_notified_chains () - vacuum's consumer of one RVOOS_NOTIFY_VACUUM record
 *
 *   return: NO_ERROR; ER_INTERRUPTED (shutdown), which leaves the block to be processed again; or the error of
 *	     a failed heap header lookup for the OOS file (a latch timeout or a read error), which the caller
 *	     turns into a later run of the block, as vacuum does for a heap record (CBRD-27933). Any other
 *	     reclaim failure stops the server (fail-stop), so the block is processed again after restart
 *   thread_p(in): vacuum worker
 *   notify_data(in): undo data of the record
 *   notify_length(in): its length
 *   oos_vfid_out(out): OOS file the chains belong to
 *   heap_hfid_out(out): heap file the notification names (for the caller's report of a failed lookup)
 *   emptied_vpids(out): pages the deletes emptied
 *   deferred_heaps(in): heaps whose OOS file lookup already failed in the caller's run of the block; may be NULL.
 *			 A notification that needs one of them is left for the next run without waiting again.
 *
 * Vacuum reaches the record only once its MVCCID precedes every active snapshot, so no reader can still
 * reach the chains through the old version. A block retry or a rerun notification finds reclaimed
 * chains gone and skips them.
 *
 * notify_data is read only by the unpack, into private copies, before the first page fix. The caller may
 * pass a pointer into its log page, which the page fixes below can rotate out, and an allocation failure of
 * the copy fail-stops with the rest of the consumer.
 */
int
oos_reclaim_notified_chains (THREAD_ENTRY *thread_p, const char *notify_data, int notify_length, VFID *oos_vfid_out,
			     HFID *heap_hfid_out, std::vector<VPID> *emptied_vpids,
			     const std::vector<VFID> *deferred_heaps)
{
  oos_reclaim_intent_header header {};
  std::vector<oos_chain_ref> refs;
  int error = oos_unpack_reclaim_intent (notify_data, notify_length, header, refs);
  if (error != NO_ERROR)
    {
      oos_reclaim_fail_stop (thread_p, error, header.oos_vfid, oos_chain_ref {oid_Null_oid, NULL_LSA});
      return error;
    }
  *heap_hfid_out = header.heap_hfid;
  if (VFID_ISNULL (&header.oos_vfid))
    {
      if (deferred_heaps != NULL)
	{
	  for (const VFID &deferred : *deferred_heaps)
	    {
	      if (VFID_EQ (&deferred, &header.heap_hfid.vfid))
		{
		  /* The caller already left the block for a later run; that run reclaims these chains. */
		  return NO_ERROR;
		}
	    }
	}

      /* The UPDATE did not resolve the OOS file; vacuum holds no page and can fix the heap header. */
      bool found;
      if (FI_INSERTED (FI_TEST_VACUUM_OOS_HEADER_WAIT_TIMEOUT))
	{
	  /* A latch timeout stops debug builds in pgbuf_timed_sleep, so a busy header fails here at once instead. */
	  found = heap_oos_find_vfid_nowait (thread_p, &header.heap_hfid, &header.oos_vfid);
	  if (!found)
	    {
	      VPID header_vpid = { header.heap_hfid.hpgid, header.heap_hfid.vfid.volid };
	      (void) FI_TEST_ARG (thread_p, FI_TEST_VACUUM_OOS_HEADER_WAIT_TIMEOUT, &header_vpid, 0);
	    }
	}
      else
	{
	  found = heap_oos_find_vfid (thread_p, &header.heap_hfid, &header.oos_vfid, false);
	}
      if (!found)
	{
	  /* A false return sets the error. A busy or unreadable heap header is not a reclaim failure: the block runs
	   * again later, as vacuum does when it cannot look up the OOS file of a heap record (CBRD-27933). */
	  ASSERT_ERROR_AND_SET (error);
	  VFID_SET_NULL (oos_vfid_out);
	  return error;
	}
      else if (VFID_ISNULL (&header.oos_vfid))
	{
	  /* Chains exist, so the heap must have an OOS file; a dropped table never reaches here. The lookup
	   * succeeded and set no error, so er_errid () may name an older one. */
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HEAP_OOS_CORRUPTED_RECORD, 0);
	  error = ER_HEAP_OOS_CORRUPTED_RECORD;
	}
      if (error != NO_ERROR)
	{
	  oos_reclaim_fail_stop (thread_p, error, header.oos_vfid, refs.empty () ? oos_chain_ref {oid_Null_oid, NULL_LSA}
				 : refs[0]);
	  return error;
	}
    }
  *oos_vfid_out = header.oos_vfid;

  for (const oos_chain_ref &ref : refs)
    {
      error = oos_delete (thread_p, header.oos_vfid, ref, emptied_vpids);
      if (error == ER_INTERRUPTED)
	{
	  /* Vacuum is shutting down; the block is not marked done and is processed again after restart. */
	  return error;
	}
      if (error != NO_ERROR)
	{
	  /* Completing the block would mark the notification done and leak the chunks for good. */
	  oos_reclaim_fail_stop (thread_p, error, header.oos_vfid, ref);
	  return error;
	}
    }
  return NO_ERROR;
}

void
oos_rv_dump_reclaim_intent (FILE *fp, int length, void *data)
{
  oos_reclaim_intent_header header {};
  std::vector<oos_chain_ref> refs;
  if (oos_unpack_reclaim_intent ((const char *) data, length, header, refs) != NO_ERROR)
    {
      er_clear ();
      fprintf (fp, "  malformed OOS reclaim intent of %d bytes\n", length);
      return;
    }
  /* The arguments are spelled out rather than given with VFID_AS_ARGS and the like, whose expansion cppcheck does
   * not see when it counts them against the format. */
  fprintf (fp, "  OOS file %d|%d, owner %d|%d|%d, op %s, flags %d, %d chains:\n", (int) header.oos_vfid.volid,
	   (int) header.oos_vfid.fileid, (int) header.owner_oid.volid, (int) header.owner_oid.pageid,
	   (int) header.owner_oid.slotid, header.op_ctx, header.flags, header.n_refs);
  for (const oos_chain_ref &ref : refs)
    {
      fprintf (fp, "    head %d|%d|%d stamp %lld|%d\n", (int) ref.head_oid.volid, (int) ref.head_oid.pageid,
	       (int) ref.head_oid.slotid, (long long int) ref.identity_stamp.pageid, (int) ref.identity_stamp.offset);
    }
}

/*
 * oos_get_identity_stamp () - Read the identity stamp the head chunk at head_oid currently carries.
 *
 *   return: NO_ERROR, or an error when the page is deallocated or no longer an OOS page, or the slot
 *           is absent. Unlike oos_delete, an absent target is an error here: the caller asks about a
 *           chunk it knows to exist, to build a correct chain reference for a test or a diagnostic.
 *   thread_p(in): thread entry
 *   head_oid(in): OID of the head chunk
 *   identity_stamp_out(out): the stamp stored in the chunk header
 *
 * NOTE: Production callers never read the stamp back from storage: they take it from the insert
 *       request output when the chain is created and from the owning heap record's OOS inline stub
 *       afterwards. This accessor is not a pre-delete probe; oos_delete verifies identity itself.
 */
int
oos_get_identity_stamp (THREAD_ENTRY *thread_p, const OID &head_oid, LOG_LSA *identity_stamp_out)
{
  assert (identity_stamp_out != NULL);
  LSA_SET_NULL (identity_stamp_out);

  /* Tolerate a deallocated or retyped page the same way oos_get_length does: a vacuumed reference
   * must report an error rather than trip the dead-page assert inside pgbuf_fix or read a non-OOS
   * page as a slotted page. */
  PAGE_PTR page_ptr = NULL;
  int err = oos_fix_page_if_oos (thread_p, head_oid, PGBUF_LATCH_READ, page_ptr);
  if (err != NO_ERROR)
    {
      return err;
    }
  if (page_ptr == nullptr)
    {
      oos_error ("oos_get_identity_stamp: page deallocated or no longer an OOS page at oid={vol=%d,page=%d,slot=%d}",
		 OID_AS_ARGS (&head_oid));
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return ER_GENERIC_ERROR;
    }
  scope_exit page_unfixer ([&]()
  {
    pgbuf_unfix_and_init_after_check (thread_p, page_ptr);
  });

  OOS_RECDES oos_recdes = RECDES_INITIALIZER;
  SCAN_CODE code = spage_get_record (thread_p, page_ptr, head_oid.slotid, &oos_recdes, PEEK);
  if (code != S_SUCCESS)
    {
      oos_error ("oos_get_identity_stamp: spage_get_record failed (code=%d) at oid={vol=%d,page=%d,slot=%d}",
		 (int) code, OID_AS_ARGS (&head_oid));
      if (er_errid () == NO_ERROR)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	}
      return er_errid ();
    }

  if (oos_recdes.length < OOS_RECORD_HEADER_SIZE)
    {
      oos_error ("oos_get_identity_stamp: OOS record smaller than header (len=%d) at oid={vol=%d,page=%d,slot=%d}",
		 oos_recdes.length, OID_AS_ARGS (&head_oid));
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HEAP_OOS_CORRUPTED_RECORD, 0);
      return ER_HEAP_OOS_CORRUPTED_RECORD;
    }

  OOS_RECORD_HEADER header;
  std::memcpy (&header, oos_recdes.data, OOS_RECORD_HEADER_SIZE);
  *identity_stamp_out = header.identity_stamp;

  return NO_ERROR;
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

/* Deletes the chunk at rcv->offset, evicts the page's bestspace hint and, when the page is now
 * empty, adds it to the emptied-page set of oos_vfid, or of the file the hint names when oos_vfid is
 * NULL. */
static int
oos_rv_delete_chunk (THREAD_ENTRY *thread_p, LOG_RCV *rcv, const VFID *oos_vfid)
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

  VPID page_vpid;
  pgbuf_get_vpid (rcv->pgptr, &page_vpid);
  const bool emptied = spage_number_of_records (rcv->pgptr) == 0;
  if (emptied && oos_vfid != NULL)
    {
      oos_reclaim_add_emptied (*oos_vfid, page_vpid);
    }
  (void) oos_stats_del_bestspace_by_vpid (thread_p, &page_vpid, emptied && oos_vfid == NULL);

  return NO_ERROR;
}

/* Redo of RVOOS_DELETE. It runs at restart, when every file is unknown until its first lap. */
int
oos_rv_redo_delete (THREAD_ENTRY *thread_p, LOG_RCV *rcv)
{
  return oos_rv_delete_chunk (thread_p, rcv, NULL);
}

/* Undo of RVOOS_INSERT, at a rollback or at restart. The undo image names the file; a record without
 * one falls back to the bestspace hint. The page stays WRITE-latched until the undo ends, so a growth
 * that checks it meanwhile finds it busy and keeps it in the set (Work818). */
int
oos_rv_undo_insert (THREAD_ENTRY *thread_p, LOG_RCV *rcv)
{
  VFID oos_vfid;
  const bool has_vfid = rcv->length == (int) sizeof (oos_vfid);
  if (has_vfid)
    {
      std::memcpy (&oos_vfid, rcv->data, sizeof (oos_vfid));
    }
  return oos_rv_delete_chunk (thread_p, rcv, has_vfid ? &oos_vfid : NULL);
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

  /* Tolerate a deallocated or retyped page: vacuum may have reclaimed it and the file may have reused
   * it, so a vacuumed OID must report "gone" rather than trip the dead-page assert or read a non-OOS
   * page as a slotted page. */
  PAGE_PTR page_ptr = NULL;
  int err = oos_fix_page_if_oos (thread_p, oid, PGBUF_LATCH_READ, page_ptr);
  if (err != NO_ERROR)
    {
      /* An operational failure (I/O, interrupt) is not a bug, so it is reported and not asserted. */
      oos_error ("oos_get_length: page fix failed for volid=%d, pageid=%d", volid, pageid);
      assert_release_error (er_errid () != NO_ERROR);
      return -1;
    }
  if (page_ptr == nullptr)
    {
      oos_error ("oos_get_length: page deallocated or no longer an OOS page for volid=%d, pageid=%d", volid, pageid);
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

#if defined(CUBRID_UNIT_TEST_ENABLED)
/*
 * xoos_get_stats_by_class_oid () - Resolve a class OID to its OOS file for SQL unit tests.
 */
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
  bool hfid_found = false;
  HFID_SET_NULL (&hfid);
  if (heap_get_class_info (thread_p, class_oid, &hfid, NULL, &hfid_found) != NO_ERROR)
    {
      ASSERT_ERROR ();
      return er_errid ();
    }
  if (!hfid_found)
    {
      return NO_ERROR;
    }

  VFID oos_vfid;
  VFID_SET_NULL (&oos_vfid);
  if (!heap_oos_find_vfid (thread_p, &hfid, &oos_vfid, false))
    {
      int errid = er_errid ();
      return errid == NO_ERROR ? NO_ERROR : errid;
    }
  if (VFID_ISNULL (&oos_vfid))
    {
      return NO_ERROR;
    }

  return oos_get_stats_by_vfid (thread_p, oos_vfid, out);
}
#endif /* CUBRID_UNIT_TEST_ENABLED */


/*
 * oos_get_stats_by_vfid () - Collect live-record statistics for an OOS file.
 *   Used by SHOW HEAP OOS and by unit-test heaps.
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
  state.unknown = true;
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
  it->second.unknown = false;
  oos_reclaim_release_sweep_locked (oos_vfid);
  pthread_mutex_unlock (&oos_Bestspace->bestspace_mutex);
}

int
oos_test_reclaim_sweep_step (THREAD_ENTRY *thread_p, const VFID &oos_vfid)
{
  bool claimed = false;
  int err = oos_reclaim_claim_growth (oos_vfid, claimed);
  if (err != NO_ERROR || !claimed)
    {
      return err;
    }
  err = oos_reclaim_sweep_step (thread_p, oos_vfid);
  oos_reclaim_release_growth (oos_vfid);
  return err;
}

int
oos_test_reclaim_waiter_count ()
{
  return oos_Test_reclaim_waiters.load (std::memory_order_relaxed);
}

void
oos_test_fail_next_reclaim_write_fix ()
{
  oos_Test_fail_next_reclaim_write_fix_armed.store (true, std::memory_order_relaxed);
}

void
oos_test_fail_next_reclaim_read_fix ()
{
  oos_Test_fail_next_reclaim_read_fix_armed.store (true, std::memory_order_relaxed);
}

void
oos_test_reclaim_set_emptied_limit (std::size_t limit)
{
  assert (oos_Bestspace != NULL);

  (void) pthread_mutex_lock (&oos_Bestspace->bestspace_mutex);
  oos_Reclaim_emptied_limit = limit == 0 ? OOS_RECLAIM_EMPTIED_LIMIT : limit;
  pthread_mutex_unlock (&oos_Bestspace->bestspace_mutex);
}

std::size_t
oos_test_reclaim_emptied_count (const VFID &oos_vfid)
{
  assert (oos_Bestspace != NULL);

  (void) pthread_mutex_lock (&oos_Bestspace->bestspace_mutex);
  auto it = oos_Reclaim_states.find (oos_vfid);
  const std::size_t count = it == oos_Reclaim_states.end () ? 0 : it->second.emptied.size ();
  pthread_mutex_unlock (&oos_Bestspace->bestspace_mutex);
  return count;
}
#undef OOS_DEBUG_COUNTER_FIELDS

std::vector<char>
oos_test_make_reclaim_intent_payload (const VFID &oos_vfid, const std::vector<oos_chain_ref> &refs)
{
  std::vector<char> payload;
  HFID no_hfid = HFID_INITIALIZER;
  if (oos_pack_reclaim_intent (oos_vfid, no_hfid, oid_Null_oid, 0, "mvcc update", refs, 0, payload) != NO_ERROR)
    {
      payload.clear ();
    }
  return payload;
}

std::vector<char>
oos_test_make_update_drop_notify_payload (const HFID &heap_hfid, const std::vector<oos_chain_ref> &refs)
{
  std::vector<char> payload;
  VFID no_oos_vfid = VFID_INITIALIZER;
  if (oos_pack_reclaim_intent (no_oos_vfid, heap_hfid, oid_Null_oid, 0, "mvcc update", refs, 0, payload) != NO_ERROR)
    {
      payload.clear ();
    }
  return payload;
}

void
oos_test_fail_reclaim_after_chunk_deletes (int n_deletes)
{
  oos_Test_fail_reclaim_after_chunk_deletes.store (n_deletes, std::memory_order_relaxed);
}

void
oos_test_set_reclaim_fail_stop_hook (void (*hook) (int error))
{
  oos_Test_reclaim_fail_stop_hook = hook;
}

void
oos_test_exit_after_update_drop_notifications (int n_notifications, int exit_code)
{
  oos_Test_exit_after_update_drop_notifications_code = exit_code;
  oos_Test_exit_after_update_drop_notifications.store (n_notifications, std::memory_order_relaxed);
}

/* Removes the synthetic-owner test files that still exist, so a test binary that ends with a commit leaves no
 * orphan OOS file whose owner class the next binary's file tracker cannot resolve. A file that an abort already
 * destroyed is recognised by its deallocated or retyped header page and skipped. */
void
oos_test_remove_created_files (THREAD_ENTRY *thread_p)
{
  std::vector<VFID> files;
  {
    std::lock_guard<std::mutex> guard (oos_Test_created_files_mutex);
    files.swap (oos_Test_created_files);
  }
  if (log_is_no_logging ())
    {
      /* A binary that turns logging off owns its database (see unit_tests/oos/CMakeLists.txt), and a file
       * cannot be destroyed without logging. */
      return;
    }
  for (const VFID &vfid : files)
    {
      VPID header_vpid = { vfid.fileid, vfid.volid };
      PAGE_PTR page_ptr = NULL;
      if (pgbuf_fix_if_not_deallocated (thread_p, &header_vpid, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH, &page_ptr)
	  != NO_ERROR || page_ptr == NULL)
	{
	  er_clear ();
	  continue;
	}
      const bool is_file_header = pgbuf_get_page_ptype (thread_p, page_ptr) == PAGE_FTAB;
      pgbuf_unfix_and_init (thread_p, page_ptr);
      FILE_TYPE ftype = FILE_UNKNOWN_TYPE;
      FILE_DESCRIPTORS des;
      const OID test_owner_class_oid = { 1, 1, 1 };
      if (!is_file_header || file_get_type (thread_p, &vfid, &ftype) != NO_ERROR || ftype != FILE_OOS
	  || file_descriptor_get (thread_p, &vfid, &des) != NO_ERROR
	  || !OID_EQ (&des.heap_oos.class_oid, &test_owner_class_oid))
	{
	  er_clear ();
	  continue;
	}
      (void) oos_remove_file (thread_p, vfid);
      er_clear ();
    }
}
#endif
