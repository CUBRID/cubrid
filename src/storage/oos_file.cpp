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

#include <cassert>
#include <cstring>
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
oos_insert_within_page (THREAD_ENTRY *thread_p, const VFID &oos_vfid, RECDES &recdes,
			const OOS_RECORD_HEADER &header, OID &oid);
static int
oos_insert_across_pages (THREAD_ENTRY *thread_p, const VFID &oos_vfid, RECDES &recdes,
			 OID &oid);
static int
oos_read_within_page (THREAD_ENTRY *thread_p, const OID &oid, RECDES &recdes,
		      OOS_RECORD_HEADER &out_header);
static int
oos_read_across_pages (THREAD_ENTRY *thread_p, const OID &oid, RECDES &recdes,
		       OOS_RECORD_HEADER &out_header);
static void
oos_log_insert_physical (THREAD_ENTRY *thread_p, PAGE_PTR page_p, VFID *vfid_p, OID *oid_p, RECDES *recdes_p);
static void
oos_log_delete_physical (THREAD_ENTRY *thread_p, PAGE_PTR page_p, VFID *vfid_p, PGSLOTID slotid, RECDES *recdes_p);
static int
oos_delete_chain (THREAD_ENTRY *thread_p, const VFID &oos_vfid, const OID &oid);

STATIC_INLINE __attribute__ ((ALWAYS_INLINE))
int oos_get_max_chunk_size_within_page ();

static bool
oos_should_track_multi_chunk_for_replication (THREAD_ENTRY *thread_p);

static auto_unfix_page_ptr
oos_file_alloc_new (THREAD_ENTRY *thread_p, const VFID &oos_vfid, VPID &vpid_out);

static const auto_unfix_page_ptr
oos_find_best_page (THREAD_ENTRY *thread_p, const VFID &oos_vfid, const int rec_length, VPID &vpid);

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
oos_stats_del_bestspace_by_vpid (THREAD_ENTRY *thread_p, VPID *vpid)
{
  OOS_STATS_ENTRY *ent;

  assert (oos_Bestspace != NULL);

  (void) pthread_mutex_lock (&oos_Bestspace->bestspace_mutex);

  ent = (OOS_STATS_ENTRY *) mht_get (oos_Bestspace->vpid_ht, vpid);
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

      /* Phase C: Try to fix the candidate page with conditional latch */
      *out_pgptr = pgbuf_fix (thread_p, &candidate_vpid, OLD_PAGE,
			      PGBUF_LATCH_WRITE, PGBUF_CONDITIONAL_LATCH);
      if (*out_pgptr == NULL)
	{
	  /* Page is busy — skip it and try next */
	  int err = er_errid ();
	  if (err == ER_INTERRUPTED)
	    {
	      found = OOS_FINDSPACE_ERROR;
	      break;
	    }
	  /* Clear error and continue — log unexpected errors for diagnostics */
	  if (err != NO_ERROR)
	    {
	      oos_trace ("conditional latch failed for vpid={vol=%d,page=%d}, er_errid=%d — skipping",
			 candidate_vpid.volid, candidate_vpid.pageid, err);
	      er_clear ();
	    }
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
	  if (best_array_index >= 0 && best_array_index < OOS_NUM_BEST_SPACESTATS)
	    {
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
  int start_idx = 1; /* Skip page 0 (header page) */
  int max_iterations;
  int total_pages;

  int err_sync = file_get_num_user_pages (thread_p, vfid, &total_pages);
  if (err_sync != NO_ERROR || total_pages <= 1)
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

  /* Determine start position */
  if (!VPID_ISNULL (&oos_hdr->estimates.full_search_vpid))
    {
      /* TODO: ideally find the index of full_search_vpid; for now start from 1 */
      start_idx = 1;
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

  for (int i = start_idx; i < total_pages && iterations < max_iterations; i++, iterations++)
    {
      VPID scan_vpid;
      int err = file_numerable_find_nth (thread_p, vfid, i, false, NULL, NULL, &scan_vpid);
      if (err != NO_ERROR || VPID_ISNULL (&scan_vpid))
	{
	  break;
	}

      /* Skip header page (safety check) */
      if (!VPID_ISNULL (hdr_vpid) && VPID_EQ (&scan_vpid, hdr_vpid))
	{
	  continue;
	}

      PAGE_PTR page_ptr = pgbuf_fix (thread_p, &scan_vpid, OLD_PAGE,
				     PGBUF_LATCH_READ, PGBUF_CONDITIONAL_LATCH);
      if (page_ptr == NULL)
	{
	  /* Page is busy — skip */
	  er_clear ();
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

      /* Save resume point */
      oos_hdr->estimates.full_search_vpid = scan_vpid;
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

  freespace = spage_get_free_space_without_saving (thread_p, pgptr, NULL);

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

int
oos_create_file (THREAD_ENTRY *thread_p, VFID &oos_vfid)
{
  int err = NO_ERROR;
  FILE_DESCRIPTORS des;
  FILE_TABLESPACE tablespace;

  memset (&des, 0, sizeof (FILE_DESCRIPTORS));
  memset (&tablespace, 0, sizeof (FILE_TABLESPACE));

  tablespace.initial_size = DB_PAGESIZE;
  tablespace.expand_ratio = (float) 0.01;
  tablespace.expand_min_size = DISK_SECTOR_NPAGES * DB_PAGESIZE;
  tablespace.expand_max_size = DISK_SECTOR_NPAGES * DB_PAGESIZE * 1024;

  err = file_create (thread_p, FILE_OOS, &tablespace, &des,
		     false /* is_temp */, true /* is_numerable */, &oos_vfid);
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

  /* Log the header record insertion */
  LOG_DATA_ADDR log_addr;
  log_addr.vfid = &oos_vfid;
  log_addr.pgptr = hdr_page;
  log_addr.offset = slotid;
  log_append_undoredo_recdes (thread_p, RVOOS_INSERT, &log_addr, NULL, &hdr_recdes);

  pgbuf_set_dirty (thread_p, hdr_page, FREE);

  log_sysop_commit (thread_p);

  oos_trace ("created OOS file {fileid=%d, volid=%d} with header page {pageid=%d}",
	     oos_vfid.fileid, oos_vfid.volid, hdr_vpid.pageid);

  return NO_ERROR;
}

int
oos_remove_file (THREAD_ENTRY *thread_p, const VFID &oos_vfid)
{
  /* Clean up bestspace cache entries for this file */
  (void) oos_stats_del_bestspace_by_vfid (thread_p, &oos_vfid);

  file_postpone_destroy (thread_p, &oos_vfid);

  return NO_ERROR;
}

// TODO: will be called by vacuum when OOS vacuum is implemented
int
oos_remove_page (THREAD_ENTRY *thread_p, const VFID &oos_vfid, const VPID &vpid)
{
  int err = file_dealloc (thread_p, &oos_vfid, &vpid, FILE_OOS);
  if (err != NO_ERROR)
    {
      oos_error ("file_dealloc failed for vpid={pageid=%d, volid=%d}", vpid.pageid, vpid.volid);
      return err;
    }

  return NO_ERROR;
}


static int
oos_prepend_header (const RECDES &recdes, const OOS_RECORD_HEADER &oos_header, OOS_RECDES &oos_recdes)
{
  // Prepends the OOS header to user data, producing the on-page record.
  // Allocates a new data area for oos_recdes; caller must free it.

  int err;
  err = recdes_allocate_data_area (&oos_recdes, recdes.length + (int)sizeof (OOS_RECORD_HEADER));
  if (err != NO_ERROR)
    {
      oos_error ("recdes_allocate_data_area failed in oos_prepend_header");
      assert_release_error (er_errid () != NO_ERROR);
      assert (false);
      return err;
    }

  oos_recdes.type = REC_HOME;
  oos_recdes.length = recdes.length + (int)sizeof (OOS_RECORD_HEADER);
  std::memcpy (oos_recdes.data, &oos_header, (int)sizeof (OOS_RECORD_HEADER));
  std::memcpy (oos_recdes.data + (int)sizeof (OOS_RECORD_HEADER), recdes.data, recdes.length);

  return NO_ERROR;
}


static int
oos_strip_header (const OOS_RECDES &oos_recdes, OOS_RECORD_HEADER &header_out, RECDES &recdes)
{
  assert (oos_recdes.length >= (int)sizeof (OOS_RECORD_HEADER));
  assert (&oos_recdes != &recdes);

  int err;

  err = recdes_allocate_data_area (&recdes, oos_recdes.length - (int)sizeof (OOS_RECORD_HEADER));
  if (err != NO_ERROR)
    {
      oos_error ("recdes_allocate_data_area failed");
      assert_release_error (er_errid () != NO_ERROR);
      assert (false);
      return err;
    }

  recdes.type = REC_HOME;
  recdes.length = oos_recdes.length - (int)sizeof (OOS_RECORD_HEADER);
  std::memcpy (&header_out, oos_recdes.data, (int)sizeof (OOS_RECORD_HEADER));
  std::memcpy (recdes.data, oos_recdes.data + (int)sizeof (OOS_RECORD_HEADER), recdes.length);

  return NO_ERROR;
}


int
oos_insert (THREAD_ENTRY *thread_p, const VFID &oos_vfid, RECDES &recdes, OID &oid)
{
  oos_debug ("arguments: oos_vfid={fileid=%d, volid=%d}, recdes.length=%d",
	     oos_vfid.fileid, oos_vfid.volid, recdes.length);
  int err = NO_ERROR;

  // TODO: otherwise spage assert type <= REC_UNKNOWN fails
  assert (recdes.type == REC_HOME);
  assert (recdes.length > 0);

  // TODO: Once the OOS_RECORD_HEADER spec is finalized (first segment header and rest segment header),
  // review whether it is possible to generate the segment headers inside the oos_insert_within_page() and
  // oos_insert_across_pages() functions.

  if (recdes.length <= oos_get_max_chunk_size_within_page ())
    {
      const OOS_RECORD_HEADER header{recdes.length, 0, OID_INITIALIZER};
      err = oos_insert_within_page (thread_p, oos_vfid, recdes, header, oid);
    }
  else
    {
      err = oos_insert_across_pages (thread_p, oos_vfid, recdes, oid);
    }

  oos_debug ("inserted to oid={vol=%d,page=%d,slot=%d}", OID_AS_ARGS (&oid));
  return err;
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
//     oos_insert_lsa_queue : [..., dummy_lsa, first_logged_chunk_lsa]
//     oos_oids             : [..., oid_Null_oid]
//
//   The immediate caller (heap_file.c) will push the real head-chunk OID after this
//   function returns, so the final pairing becomes oos_oids=[..., null, real_oid]
//   with queue=[..., dummy_lsa, first_logged_chunk_lsa]. The replication path then emits
//   one RVREPL_DUMMY_OOS_RECORD for the null OID (pops dummy_lsa) followed by one
//   RVREPL_OOS_INSERT for the real OID (pops first_logged_chunk_lsa). Intermediate
//   chunks are not enqueued; tdes->suppress_oos_insert_lsa_queueing suppresses the
//   auto-push in log_append_{undo,}redo_crumbs while this function runs.
//
static int
oos_insert_across_pages (THREAD_ENTRY *thread_p, const VFID &oos_vfid, RECDES &recdes, OID &oid)
{
  int err = NO_ERROR;
  LOG_TDES *tdes = NULL;
  LOG_LSA dummy_lsa = NULL_LSA;
  LOG_LSA first_logged_chunk_lsa = NULL_LSA;
  bool should_track_multi_chunk = false;

  // split the recdes to multiple chunks and insert them one by one
  const int max_chunk_size = oos_get_max_chunk_size_within_page ();
  assert (recdes.length + (int)sizeof (OOS_RECORD_HEADER) > max_chunk_size);

  int required_page_nums = (recdes.length + max_chunk_size - 1) / max_chunk_size;
  assert (required_page_nums > 1);

  const int total_data_length = recdes.length;

  int total_inserted_length = 0;
  OID next_chunk_oid = OID_INITIALIZER; // the last chunk has null OID as next_chunk_oid

  should_track_multi_chunk = oos_should_track_multi_chunk_for_replication (thread_p);
  if (should_track_multi_chunk)
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

      tdes->suppress_oos_insert_lsa_queueing = true;
    }

  scope_exit clear_oos_repl_state ([&]()
  {
    if (should_track_multi_chunk && tdes != NULL)
      {
	tdes->suppress_oos_insert_lsa_queueing = false;
      }
  });

  // this loop inserts chunks in reverse order so that next_chunk_oid is always known
  for (int i = required_page_nums - 1; i >= 0; --i)
    {
      RECDES chunk_recdes{};
      chunk_recdes.type = REC_HOME;
      chunk_recdes.length = std::min (max_chunk_size, total_data_length - i * max_chunk_size);
      total_inserted_length += chunk_recdes.length;
      chunk_recdes.data = recdes.data + i * max_chunk_size;

      // Keep total_data_length in each chunk so the log applier can validate all pieces before reassembly.
      OOS_RECORD_HEADER header{total_data_length, i, next_chunk_oid};

      OID current_chunk_oid;
      err = oos_insert_within_page (thread_p, oos_vfid, chunk_recdes, header, current_chunk_oid);
      if (err != NO_ERROR)
	{
	  oos_error ("could not insert chunk index=%d of length %d.", i, chunk_recdes.length);
	  assert_release_error (er_errid () != NO_ERROR);
	  // Partially inserted chunks are cleaned up when the caller aborts the transaction
	  // (individual undo records replay in reverse). The caller MUST NOT continue
	  // the transaction after this error.
	  return err;
	}

      if (should_track_multi_chunk && i == required_page_nums - 1)
	{
	  LSA_COPY (&first_logged_chunk_lsa, &tdes->tail_lsa);
	}

      next_chunk_oid = current_chunk_oid;
    }
  assert (total_inserted_length == recdes.length);

  if (should_track_multi_chunk)
    {
      tdes->oos_insert_lsa_queue.push (dummy_lsa);
      tdes->oos_insert_lsa_queue.push (first_logged_chunk_lsa);
      thread_p->oos_oids.push_back (oid_Null_oid);
    }

  // update the out parameter 'oid' to give access to the first slot
  oid = next_chunk_oid;
  return NO_ERROR;
}


static int
oos_insert_within_page (THREAD_ENTRY *thread_p, const VFID &oos_vfid, RECDES &recdes,
			const OOS_RECORD_HEADER &header,
			OID &oid)
{
  int err = NO_ERROR;
  VPID vpid;

  assert (recdes.length <= oos_get_max_chunk_size_within_page ());

  int required_length = recdes.length + (int)sizeof (OOS_RECORD_HEADER);

  assert (required_length <= DB_ALIGN_BELOW (spage_max_record_size (), OOS_ALIGNMENT));

  auto auto_page_ptr = oos_find_best_page (thread_p, oos_vfid, required_length, vpid);

  OOS_RECDES oos_recdes{};
  {
    err = oos_prepend_header (recdes, header, oos_recdes);
    if (err != NO_ERROR)
      {
	oos_error ("oos_prepend_header failed");
	assert_release_error (er_errid () != NO_ERROR);
	assert (false);
	return err;
      }

    // oos_prepend_header allocates data area for oos_recdes
    // therefore, we need to free it after use
    scope_exit defer_oos_recdes_free ([&]()
    {
      recdes_free_data_area (&oos_recdes);
    });

    PGSLOTID slotid = NULL_SLOTID;
    PAGE_PTR page_ptr = auto_page_ptr.get();
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

    /* Update bestspace cache after insert — use spage_max_space_for_new_record
     * for consistency with the lookup check in oos_stats_find_page_in_bestspace */
    int freespace_after = spage_max_space_for_new_record (thread_p, page_ptr);
    (void) oos_stats_add_bestspace (thread_p, &oos_vfid, &vpid, freespace_after);
  }
  assert (oos_recdes.data == nullptr); // should be freed by scope_exit

  return NO_ERROR;
}


static int
oos_read_across_pages (THREAD_ENTRY *thread_p, const OID &oid,
		       const OOS_RECORD_HEADER &first_chunk_header, RECDES &recdes)
{
  int err = NO_ERROR;
  const int total_data_length = first_chunk_header.total_data_length;
  assert (first_chunk_header.chunk_index == 0);

  assert (total_data_length > oos_get_max_chunk_size_within_page ());
  oos_trace ("total_data_length=%d", total_data_length);

  err = recdes_allocate_data_area (&recdes, total_data_length);
  if (err != NO_ERROR)
    {
      oos_error ("recdes_allocate_data_area failed in oos_read_across_pages, total_data_length=%d", total_data_length);
      assert_release_error (er_errid () != NO_ERROR);
      assert (false);
      return err;
    }

  recdes.type = REC_HOME;
  recdes.length = total_data_length;

  int idx = 0;
  OID current_chunk_oid = oid;
  char *buf = recdes.data;
  int total_read_length = 0;
  while (current_chunk_oid.pageid != NULL_PAGEID)
    {
      OOS_RECORD_HEADER header;
      RECDES chunk_recdes;
      {
	int err = oos_read_within_page (thread_p, current_chunk_oid, chunk_recdes, header);
	if (err != NO_ERROR)
	  {
	    oos_error ("oos_read_within_page failed for chunk index=%d", idx);
	    assert_release_error (er_errid () != NO_ERROR);
	    assert (false);
	    recdes_free_data_area (&recdes);
	    return err;
	  }

	scope_exit defer_free_chunk_recdes ([&]()
	{
	  recdes_free_data_area (&chunk_recdes);
	});

	assert (idx == header.chunk_index);
	assert (total_data_length == header.total_data_length);

	total_read_length += chunk_recdes.length;
	std::memcpy (buf, chunk_recdes.data, chunk_recdes.length);
	buf += chunk_recdes.length;

	current_chunk_oid = header.next_chunk_oid;
      }
      assert (chunk_recdes.data == nullptr); // should be freed by scope_exit

      idx++;
    }

  assert (total_read_length == total_data_length);
  assert (buf == total_data_length + recdes.data);

  return NO_ERROR;
}


static int
oos_read_within_page (THREAD_ENTRY *thread_p, const OID &oid, RECDES &recdes,
		      OOS_RECORD_HEADER &header_out)
{
  int err = NO_ERROR;
  const auto [pageid, slotid, volid] = oid;
  auto vpid = VPID{pageid, volid};

  PAGE_PTR page_ptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
  if (page_ptr == nullptr)
    {
      oos_error ("oos_read_within_page: pgbuf_fix failed for volid=%d, pageid=%d", volid, pageid);
      assert_release_error (er_errid () != NO_ERROR);
      assert (false);
      return er_errid ();
    }

  scope_exit page_unfixer ([&]()
  {
    pgbuf_unfix_and_init_after_check (thread_p, page_ptr);
  });

  OOS_RECDES oos_recdes;
  SCAN_CODE code = spage_get_record (thread_p, page_ptr, slotid, &oos_recdes, PEEK);
  if (code != S_SUCCESS)
    {
      oos_error ("oos_read_within_page: spage_get_record failed for volid=%d, pageid=%d, slotid=%d",
		 volid, pageid, slotid);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return ER_GENERIC_ERROR;
    }

  // TODO: Ensure OOS_RECORD_HEADER always fits within a single page

  err = oos_strip_header (oos_recdes, header_out, recdes);
  if (err != NO_ERROR)
    {
      oos_error ("oos_strip_header failed for volid=%d, pageid=%d, slotid=%d",
		 volid, pageid, slotid);
      assert_release_error (er_errid () != NO_ERROR);
      assert (false);
      return err;
    }

  return NO_ERROR;
}


/* oos_read -
 *
 * return:
 *
 *   oos_vfid(in):
 *   oid(in):
 *   recdes(out):
 *
 *   important notes: recdes.data is allocated inside this function.
 *   It sholud be freed by the caller using recdes_free_data_area().
 */
int
oos_read (THREAD_ENTRY *thread_p, const OID &oid, RECDES &recdes)
{
  oos_debug ("reading from oid={vol=%d,page=%d,slot=%d}", OID_AS_ARGS (&oid));

  int err = NO_ERROR;

  // try reading just one slot
  OOS_RECORD_HEADER first_chunk_header;
  RECDES first_chunk_recdes;

  err = oos_read_within_page (thread_p, oid, first_chunk_recdes, first_chunk_header);
  if (err != NO_ERROR)
    {
      oos_error ("oos_read_within_page failed");
      return err;
    }

  // check if we need to read all the chunks
  if (first_chunk_header.next_chunk_oid.slotid != NULL_SLOTID)
    {
      err = oos_read_across_pages (thread_p, oid, first_chunk_header, recdes);
      // CASE 1: we do not need first_chunk_recdes anymore
      recdes_free_data_area (&first_chunk_recdes);
      if (err != NO_ERROR)
	{
	  oos_error ("oos_read_across_pages failed");
	  assert_release_error (er_errid () != NO_ERROR);
	  assert (false);
	  return err;
	}
    }
  else
    {
      // CASE 2: we use first_chunk_recdes as the final output
      recdes = std::move (first_chunk_recdes);
    }
  oos_trace ("read completed, total_data_length=%d", first_chunk_header.total_data_length);

  return NO_ERROR;
}


// ****************************************************************************
// OOS Page allocation
// ****************************************************************************

static auto_unfix_page_ptr
oos_file_alloc_new (THREAD_ENTRY *thread_p, const VFID &oos_vfid,
		    VPID &vpid_out)
{
  int err = NO_ERROR;
  PAGE_TYPE page_type = PAGE_OOS;

  log_sysop_start (thread_p);
  err = file_alloc (thread_p, &oos_vfid, oos_vpid_init_new, &page_type, &vpid_out, nullptr);
  if (err != NO_ERROR)
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

  return pgbuf_fix_auto_unfix (thread_p, &vpid_out, OLD_PAGE,
			       PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
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
      return oos_file_alloc_new (thread_p, oos_vfid, vpid);
    }

  PAGE_PTR hdr_page = pgbuf_fix (thread_p, &hdr_vpid, OLD_PAGE,
				 PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (hdr_page == NULL)
    {
      return oos_file_alloc_new (thread_p, oos_vfid, vpid);
    }

  OOS_HDR_STATS *oos_hdr = oos_get_header_stats_ptr (thread_p, hdr_page);
  if (oos_hdr == NULL)
    {
      pgbuf_unfix_and_init (thread_p, hdr_page);
      return oos_file_alloc_new (thread_p, oos_vfid, vpid);
    }

  int total_space = rec_length + (int) sizeof (SPAGE_SLOT);

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

      /* Not found — try sync if worthwhile */
      float ratio = 0;
      if (oos_hdr->estimates.num_pages > 0)
	{
	  ratio = (float) oos_hdr->estimates.num_other_high_best / (float) oos_hdr->estimates.num_pages;
	}

      if (ratio >= OOS_BESTSPACE_SYNC_THRESHOLD || try_find == 0)
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
	      return oos_file_alloc_new (thread_p, oos_vfid, vpid);
	    }
	  oos_hdr = oos_get_header_stats_ptr (thread_p, hdr_page);
	  if (oos_hdr == NULL)
	    {
	      pgbuf_unfix_and_init (thread_p, hdr_page);
	      return oos_file_alloc_new (thread_p, oos_vfid, vpid);
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

      /* Unfix the conditional-latch page and re-fix as auto_unfix with unconditional latch.
       * Between unfix and re-fix, another thread may fill the page (race window). */
      pgbuf_unfix_and_init (thread_p, found_page);

      auto result_page = pgbuf_fix_auto_unfix (thread_p, &vpid, OLD_PAGE,
			 PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
      if (result_page == nullptr)
	{
	  /* Re-fix failed — propagate error if it is not a benign latch timeout */
	  int refix_err = er_errid ();
	  if (refix_err == ER_INTERRUPTED)
	    {
	      return nullptr;
	    }
	  /* Fall through to allocate new page */
	}
      else
	{
	  /* Re-check free space after unconditional re-fix (race window protection) */
	  int actual_free = spage_max_space_for_new_record (thread_p, result_page.get ());
	  if (actual_free >= total_space)
	    {
	      return result_page;
	    }
	  /* Page was filled by another thread — fall through to allocate new */
	  result_page.reset ();
	}
    }

  /* No existing page found — allocate new */
  pgbuf_unfix_and_init_after_check (thread_p, hdr_page);

  auto new_page = oos_file_alloc_new (thread_p, oos_vfid, vpid);

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
 *
 * NOTE: This is the inner workhorse called by oos_delete(). Each chunk
 *       deletion is logged individually with undo data, so transaction
 *       abort restores all deleted chunks in reverse order.
 */
static int
oos_delete_chain (THREAD_ENTRY *thread_p, const VFID &oos_vfid, const OID &oid)
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
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  oos_error ("OOS record at volid=%d pageid=%d slotid=%d has invalid length %d",
		     OID_AS_ARGS (&current_oid), oos_recdes.length);
	  return ER_GENERIC_ERROR;
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

      oos_debug ("deleted chunk at oid={vol=%d,page=%d,slot=%d}, next={vol=%d,page=%d,slot=%d}",
		 OID_AS_ARGS (&current_oid), OID_AS_ARGS (&next_chunk_oid));

      current_oid = next_chunk_oid;
    }

  return NO_ERROR;
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
 *       Page deallocation is NOT done here. Empty pages will be reclaimed
 *       by vacuum after the transaction commits.
 */
int
oos_delete (THREAD_ENTRY *thread_p, const VFID &oos_vfid, const OID &oid)
{
  oos_debug ("arguments: oid={vol=%d,page=%d,slot=%d}", OID_AS_ARGS (&oid));

  return oos_delete_chain (thread_p, oos_vfid, oid);
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

static bool
oos_should_track_multi_chunk_for_replication (THREAD_ENTRY *thread_p)
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

  /* Remove stale bestspace cache entry for this page (rollback path) */
  VPID page_vpid;
  pgbuf_get_vpid (rcv->pgptr, &page_vpid);
  (void) oos_stats_del_bestspace_by_vpid (thread_p, &page_vpid);

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

  PAGE_PTR page_ptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
  if (page_ptr == nullptr)
    {
      oos_error ("oos_get_length: pgbuf_fix failed for volid=%d, pageid=%d", volid, pageid);
      assert_release_error (er_errid () != NO_ERROR);
      assert (false);
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

  OOS_RECORD_HEADER header;
  std::memcpy (&header, oos_recdes.data, sizeof (OOS_RECORD_HEADER));

  return header.total_data_length;
}


void
oos_push_oos_oid (THREAD_ENTRY *thread_p, const OID *oid)
{
  thread_p->oos_oids.push_back (*oid);
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
#endif
