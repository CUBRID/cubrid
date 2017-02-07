/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */

/*
 * Filter predicate cache.
 */

#ident "$Id$"

#include "filter_pred_cache.h"
#include "lock_free.h"
#include "query_executor.h"

typedef struct fpcache_ent FPCACHE_ENTRY;
struct fpcache_ent
{
  BTID btid;			/* B-tree identifier. */

  /* Latch-free stuff. */
  FPCACHE_ENTRY *stack;		/* used in freelist */
  FPCACHE_ENTRY *next;		/* used in hash table */
  pthread_mutex_t mutex;	/* Mutex. */
  UINT64 del_id;		/* delete transaction ID (for lock free) */

  /* Entry info */
  OID class_oid;		/* Class OID. */
  struct timeval time_last_used;	/* when this entry used lastly */

  PRED_EXPR_WITH_CONTEXT **clone_stack;
  INT32 clone_stack_head;
};

#define FPCACHE_PTR_TO_KEY(ptr) ((BTID *) ptr)
#define FPCACHE_PTR_TO_ENTRY(ptr) ((FPCACHE_ENTRY *) ptr)

static bool fpcache_Enabled = false;
static INT32 fpcache_Soft_capacity = 0;
static LF_HASH_TABLE fpcache_Ht = LF_HASH_TABLE_INITIALIZER;
static LF_FREELIST fpcache_Ht_freelist = LF_FREELIST_INITIALIZER;
/* TODO: Handle counter >= soft capacity. */
static volatile INT32 fpcache_Entry_counter = 0;
static volatile INT32 fpcache_Clone_counter = 0;
static int fpcache_Clone_stack_size;

/* Cleanup */
typedef struct fpcache_cleanup_candidate FPCACHE_CLEANUP_CANDIDATE;
struct fpcache_cleanup_candidate
{
  BTID btid;
  struct timeval time_last_used;
};
INT32 fpcache_Cleanup_flag;
BINARY_HEAP *fpcache_Cleanup_bh;

#define FPCACHE_CLEANUP_RATIO 0.2

/* Statistics. */
static INT64 fpcache_Stat_lookup;
static INT64 fpcache_Stat_miss;
static INT64 fpcache_Stat_hit;
static INT64 fpcache_Stat_discard;
static INT64 fpcache_Stat_add;
static INT64 fpcache_Stat_clone_miss;
static INT64 fpcache_Stat_clone_hit;
static INT64 fpcache_Stat_clone_discard;
static INT64 fpcache_Stat_clone_add;
static INT64 fpcache_Stat_cleanup;
static INT64 fpcache_Stat_cleanup_entry;

/* fpcache_Entry_descriptor - used for latch-free hash table.
 * we have to declare member functions before instantiating fpcache_Entry_descriptor.
 */
static void *fpcache_entry_alloc (void);
static int fpcache_entry_free (void *entry);
static int fpcache_entry_init (void *entry);
static int fpcache_entry_uninit (void *entry);
static int fpcache_copy_key (void *src, void *dest);
static int fpcache_compare_key (void *key1, void *key2);
static unsigned int fpcache_hash_key (void *key, int hash_table_size);
static void fpcache_cleanup (THREAD_ENTRY * thread_p);
static BH_CMP_RESULT fpcache_compare_cleanup_candidates (const void *left, const void *right, BH_CMP_ARG ingore_arg);

static LF_ENTRY_DESCRIPTOR fpcache_Entry_descriptor = {
  offsetof (FPCACHE_ENTRY, stack),
  offsetof (FPCACHE_ENTRY, next),
  offsetof (FPCACHE_ENTRY, del_id),
  offsetof (FPCACHE_ENTRY, btid),
  offsetof (FPCACHE_ENTRY, mutex),

  /* using mutex */
  LF_EM_USING_MUTEX,

  fpcache_entry_alloc,
  fpcache_entry_free,
  fpcache_entry_init,
  fpcache_entry_uninit,
  fpcache_copy_key,
  btree_compare_btids,
  btree_hash_btid,
  NULL,				/* duplicates not accepted. */
};

/*
 * fpcache_initialize () - Initialize filter predicate cache.
 *
 * return        : Error code.
 * thread_p (in) : Thread entry.
 */
int
fpcache_initialize (THREAD_ENTRY * thread_p)
{
  int error_code = NO_ERROR;
  HL_HEAPID save_heapid;

  fpcache_Enabled = false;

  fpcache_Soft_capacity = prm_get_integer_value (PRM_ID_FILTER_PRED_MAX_CACHE_ENTRIES);
  if (fpcache_Soft_capacity <= 0)
    {
      /* Filter predicate cache disabled. */
      return NO_ERROR;
    }

  /* Initialize free list */
  error_code =
    lf_freelist_init (&fpcache_Ht_freelist, 1, fpcache_Soft_capacity, &fpcache_Entry_descriptor, &fpcache_Ts);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  /* Initialize hash table. */
  error_code = lf_hash_init (&fpcache_Ht, &fpcache_Ht_freelist, fpcache_Soft_capacity, &fpcache_Entry_descriptor);
  if (error_code != NO_ERROR)
    {
      lf_freelist_destroy (&fpcache_Ht_freelist);
      return error_code;
    }
  fpcache_Entry_counter = 0;
  fpcache_Clone_counter = 0;

  fpcache_Clone_stack_size = prm_get_integer_value (PRM_ID_FILTER_PRED_MAX_CACHE_CLONES);

  /* Cleanup */
  /* Use global heap to allocate binary heap. */
  save_heapid = db_change_private_heap (thread_p, 0);
  fpcache_Cleanup_flag = 0;
  fpcache_Cleanup_bh =
    bh_create (thread_p, (int) (FPCACHE_CLEANUP_RATIO * fpcache_Soft_capacity), sizeof (FPCACHE_CLEANUP_CANDIDATE),
	       fpcache_compare_cleanup_candidates, NULL);
  (void) db_change_private_heap (thread_p, save_heapid);
  if (fpcache_Cleanup_bh == NULL)
    {
      lf_freelist_destroy (&fpcache_Ht_freelist);
      lf_hash_destroy (&fpcache_Ht);
      ASSERT_ERROR_AND_SET (error_code);
      return error_code;
    }

  fpcache_Stat_lookup = 0;
  fpcache_Stat_miss = 0;
  fpcache_Stat_hit = 0;
  fpcache_Stat_discard = 0;
  fpcache_Stat_add = 0;
  fpcache_Stat_lookup = 0;
  fpcache_Stat_clone_miss = 0;
  fpcache_Stat_clone_hit = 0;
  fpcache_Stat_clone_discard = 0;
  fpcache_Stat_cleanup = 0;
  fpcache_Stat_cleanup_entry = 0;

  fpcache_Enabled = true;
  return NO_ERROR;
}

/*
 * fpcache_finalize () - Finalize filter predicate cache.
 *
 * return	     : Void.
 * thread_entry (in) : Thread entry.
 */
void
fpcache_finalize (THREAD_ENTRY * thread_p)
{
  HL_HEAPID save_heapid;

  if (!fpcache_Enabled)
    {
      return;
    }

  lf_freelist_destroy (&fpcache_Ht_freelist);
  lf_hash_destroy (&fpcache_Ht);

  /* Use global heap */
  save_heapid = db_change_private_heap (thread_p, 0);
  if (fpcache_Cleanup_bh != NULL)
    {
      bh_destroy (thread_p, fpcache_Cleanup_bh);
      fpcache_Cleanup_bh = NULL;
    }
  (void) db_change_private_heap (thread_p, save_heapid);

  fpcache_Enabled = false;
}

/*
 * fpcache_entry_alloc () - Allocate a filter predicate cache entry.
 *
 * return : Pointer to allocated memory.
 */
static void *
fpcache_entry_alloc (void)
{
  FPCACHE_ENTRY *fpcache_entry = (FPCACHE_ENTRY *) malloc (sizeof (FPCACHE_ENTRY));
  if (fpcache_entry == NULL)
    {
      return NULL;
    }
  pthread_mutex_init (&fpcache_entry->mutex, NULL);
  return fpcache_entry;
}

/*
 * fpcache_entry_free () - Free filter predicate cache entry.
 *
 * return     : NO_ERROR.
 * entry (in) : filter predicate cache entry.
 */
static int
fpcache_entry_free (void *entry)
{
  pthread_mutex_destroy (&((FPCACHE_ENTRY *) entry)->mutex);
  free (entry);
  return NO_ERROR;
}

/*
 * fpcache_entry_init () - Initialize filter predicate cache entry.
 *
 * return     : Error code.
 * entry (in) : filter predicate cache entry
 */
static int
fpcache_entry_init (void *entry)
{
  FPCACHE_ENTRY *fpcache_entry = FPCACHE_PTR_TO_ENTRY (entry);
  /* Add here if anything should be initialized. */
  /* Allocate clone stack. */
  fpcache_entry->clone_stack =
    (PRED_EXPR_WITH_CONTEXT **) malloc (fpcache_Clone_stack_size * sizeof (PRED_EXPR_WITH_CONTEXT));
  if (fpcache_entry->clone_stack == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
	      fpcache_Clone_stack_size * sizeof (PRED_EXPR_WITH_CONTEXT));
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  fpcache_entry->clone_stack_head = -1;
  return NO_ERROR;
}

/*
 * fpcache_entry_uninit () - Retire filter predicate cache entry.
 *
 * return     : NO_ERROR.
 * entry (in) : filter predicate cache entry.
 */
static int
fpcache_entry_uninit (void *entry)
{
  FPCACHE_ENTRY *fpcache_entry = FPCACHE_PTR_TO_ENTRY (entry);
  THREAD_ENTRY *thread_p = thread_get_thread_entry_info ();
  HL_HEAPID old_private_heap;
  PRED_EXPR_WITH_CONTEXT *pred_expr = NULL;
  int head;

  old_private_heap = db_change_private_heap (thread_p, 0);

  for (head = fpcache_entry->clone_stack_head; head >= 0; head--)
    {
      pred_expr = fpcache_entry->clone_stack[head];
      assert (pred_expr != NULL);

      qexec_clear_pred_context (thread_p, pred_expr, true);
      stx_free_additional_buff (thread_p, pred_expr->unpack_info);
      stx_free_xasl_unpack_info (pred_expr->unpack_info);
      db_private_free_and_init (thread_p, pred_expr->unpack_info);
    }

  (void) db_change_private_heap (thread_p, old_private_heap);
  fpcache_entry->clone_stack_head = -1;

  if (fpcache_entry->clone_stack != NULL)
    {
      free_and_init (fpcache_entry->clone_stack);
    }

  return NO_ERROR;
}

/*
 * fpcache_copy_key () - Copy filter predicate cache entry key (b-tree ID).
 *
 * return     : NO_ERROR.
 * src (in)   : Source b-tree ID.
 * dest (out) : Destination b-tree ID.
 */
static int
fpcache_copy_key (void *src, void *dest)
{
  BTID_COPY ((BTID *) dest, (BTID *) src);
  return NO_ERROR;
}

/*
 * fpcache_claim () - Claim a filter predicate expression from filter predicate cache. If no expression is available in
 *                    cache, a new one is generated.
 *
 * return            : Error code.
 * thread_p (in)     : Thread entry.
 * btid (in)         : B-tree ID.
 * or_pred (in)      : Filter predicate (string and stream).
 * filter_pred (out) : Filter predicate expression (with context - unpack buffer).
 */
int
fpcache_claim (THREAD_ENTRY * thread_p, BTID * btid, OR_PREDICATE * or_pred, PRED_EXPR_WITH_CONTEXT ** filter_pred)
{
  LF_TRAN_ENTRY *t_entry = thread_get_tran_entry (thread_p, THREAD_TS_FPCACHE);
  FPCACHE_ENTRY *fpcache_entry = NULL;
  int error_code = NO_ERROR;

  assert (filter_pred != NULL && *filter_pred == NULL);

  if (fpcache_Enabled)
    {
      /* Try to find available filter predicate expression in cache. */
      ATOMIC_INC_64 (&fpcache_Stat_lookup, 1);

      error_code = lf_hash_find (t_entry, &fpcache_Ht, btid, (void **) &fpcache_entry);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return error_code;
	}
      if (fpcache_entry == NULL)
	{
	  /* Entry not found. */
	  ATOMIC_INC_64 (&fpcache_Stat_miss, 1);
	  ATOMIC_INC_64 (&fpcache_Stat_clone_miss, 1);
	}
      else
	{
	  /* Hash-table entry found. Try to claim a filter predicate expression, if there is any available. */
	  ATOMIC_INC_64 (&fpcache_Stat_hit, 1);
	  if (fpcache_entry->clone_stack_head >= 0)
	    {
	      /* Available filter predicate expression. */
	      assert (fpcache_entry->clone_stack_head < fpcache_Clone_stack_size);
	      *filter_pred = fpcache_entry->clone_stack[fpcache_entry->clone_stack_head--];
	      ATOMIC_INC_64 (&fpcache_Stat_clone_hit, 1);
	      ATOMIC_INC_32 (&fpcache_Clone_counter, -1);
	    }
	  else
	    {
	      /* No filter predicate expression is available. */
	      ATOMIC_INC_64 (&fpcache_Stat_clone_miss, 1);
	    }
	  /* Unlock hash-table entry. */
	  pthread_mutex_unlock (&fpcache_entry->mutex);
	}
    }

  if (*filter_pred == NULL)
    {
      /* Allocate new filter predicate expression. */
      /* Use global heap as other threads may also use this filter predicate expression. */
      HL_HEAPID old_private_heap = db_change_private_heap (thread_p, 0);
      error_code =
	stx_map_stream_to_filter_pred (thread_p, filter_pred, or_pred->pred_stream, or_pred->pred_stream_size);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	}
      (void) db_change_private_heap (thread_p, old_private_heap);
    }
  return NO_ERROR;
}

/*
 * fpcache_retire () - Retire filter predicate expression; if the filter predicate hash entry is already at maximum
 *                     capacity, the predicate expression must be freed.
 *
 * return           : Error code.
 * thread_p (in)    : Thread entry.
 * class_oid (in)   : Class OID (of index).
 * btid (in)        : B-tree ID.
 * filter_pred (in) : Filter predicate expression.
 */
int
fpcache_retire (THREAD_ENTRY * thread_p, OID * class_oid, BTID * btid, PRED_EXPR_WITH_CONTEXT * filter_pred)
{
  LF_TRAN_ENTRY *t_entry = thread_get_tran_entry (thread_p, THREAD_TS_FPCACHE);
  FPCACHE_ENTRY *fpcache_entry = NULL;
  int error_code = NO_ERROR;
  int inserted = 0;

  if (fpcache_Enabled)
    {
      /* Try to retire in cache entry. */
      ATOMIC_INC_64 (&fpcache_Stat_add, 1);
      error_code = lf_hash_find_or_insert (t_entry, &fpcache_Ht, btid, (void **) &fpcache_entry, &inserted);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return error_code;
	}
      if (fpcache_entry != NULL)
	{
	  if (inserted)
	    {
	      /* Newly inserted. We must set class_oid. */
	      COPY_OID (&fpcache_entry->class_oid, class_oid);

	      ATOMIC_INC_32 (&fpcache_Entry_counter, 1);
	      ATOMIC_INC_64 (&fpcache_Stat_add, 1);

	      if (fpcache_Entry_counter >= fpcache_Soft_capacity)
		{
		  /* Try cleanup. */
		  fpcache_cleanup (thread_p);
		}
	    }
	  else
	    {
	      /* Entry is older. Safe-guard: class OID must match. */
	      assert (OID_EQ (&fpcache_entry->class_oid, class_oid));
	    }
	  /* save filter_pred for later usage. */
	  if (fpcache_entry->clone_stack_head < fpcache_Clone_stack_size - 1)
	    {
	      /* Can save filter predicate expression. */
	      fpcache_entry->clone_stack[++fpcache_entry->clone_stack_head] = filter_pred;
	      filter_pred = NULL;
	      ATOMIC_INC_64 (&fpcache_Stat_clone_add, 1);
	      ATOMIC_INC_32 (&fpcache_Clone_counter, 1);
	    }
	  else
	    {
	      /* No room for another filter predicate expression. */
	      ATOMIC_INC_64 (&fpcache_Stat_clone_discard, 1);
	    }
	  gettimeofday (&fpcache_entry->time_last_used, NULL);
	  pthread_mutex_unlock (&fpcache_entry->mutex);
	}
      else
	{
	  /* Unexpected. */
	  assert (false);
	  error_code = ER_FAILED;
	}
    }

  if (filter_pred != NULL)
    {
      /* Filter predicate expression could not be cached. Free it. */
      HL_HEAPID old_private_heap = db_change_private_heap (thread_p, 0);
      stx_free_additional_buff (thread_p, filter_pred->unpack_info);
      stx_free_xasl_unpack_info (filter_pred->unpack_info);
      db_private_free_and_init (thread_p, filter_pred->unpack_info);
      (void) db_change_private_heap (thread_p, old_private_heap);
    }
  return error_code;
}

/*
 * fpcache_remove_by_class () - Remove all filter predicate cache entries belonging to the given class.
 *
 * return         : Void.
 * thread_p (in)  : Thread entry.
 * class_oid (in) : Class OID.
 */
void
fpcache_remove_by_class (THREAD_ENTRY * thread_p, OID * class_oid)
{
#define FPCACHE_DELETE_BTIDS_SIZE 1024

  LF_HASH_TABLE_ITERATOR iter;
  LF_TRAN_ENTRY *t_entry = thread_get_tran_entry (thread_p, THREAD_TS_FPCACHE);
  FPCACHE_ENTRY *fpcache_entry;
  int success = 0;
  BTID delete_btids[FPCACHE_DELETE_BTIDS_SIZE];
  int n_delete_btids = 0;
  int btid_index = 0;
  bool finished = false;

  if (!fpcache_Enabled)
    {
      return;
    }

  while (!finished)
    {
      lf_hash_create_iterator (&iter, t_entry, &fpcache_Ht);

      while (true)
	{
	  /* Start by iterating to next hash entry. */
	  fpcache_entry = (FPCACHE_ENTRY *) lf_hash_iterate (&iter);

	  if (fpcache_entry == NULL)
	    {
	      /* Finished hash. */
	      finished = true;
	      break;
	    }

	  if (OID_EQ (&fpcache_entry->class_oid, class_oid))
	    {
	      /* Save entry to be deleted after the iteration.
	       * We cannot delete from hash while iterating. The lock-free transaction used by iterator cannot be used
	       * for delete too (and we have just one transaction for each thread).
	       */
	      delete_btids[n_delete_btids++] = fpcache_entry->btid;

	      if (n_delete_btids == FPCACHE_DELETE_BTIDS_SIZE)
		{
		  /* Free mutex. */
		  pthread_mutex_unlock (&fpcache_entry->mutex);
		  /* Full buffer. Interrupt iteration, delete entries collected so far and then start over. */
		  lf_tran_end_with_mb (t_entry);
		  break;
		}
	    }
	}

      /* Delete collected btids. */
      for (btid_index = 0; btid_index < n_delete_btids; btid_index++)
	{
	  if (lf_hash_delete (t_entry, &fpcache_Ht, &delete_btids[btid_index], &success) != NO_ERROR)
	    {
	      /* Unexpected. */
	      assert (false);
	    }
	  else if (success)
	    {
	      /* Successfully removed. */
	      ATOMIC_INC_32 (&fpcache_Entry_counter, -1);
	      ATOMIC_INC_64 (&fpcache_Stat_discard, 1);
	    }
	  else
	    {
	      /* Unexpected. */
	      assert (false);
	    }
	}
      n_delete_btids = 0;
    }

#undef FPCACHE_DELETE_BTIDS_SIZE
}

/*
 * fpcache_dump () - Dump filter predicate cache info.
 *
 * return        : Void.
 * thread_p (in) : Thread entry.
 * fp (out)      : Dump output.
 */
void
fpcache_dump (THREAD_ENTRY * thread_p, FILE * fp)
{
  LF_HASH_TABLE_ITERATOR iter;
  LF_TRAN_ENTRY *t_entry = thread_get_tran_entry (thread_p, THREAD_TS_FPCACHE);
  FPCACHE_ENTRY *fpcache_entry = NULL;

  assert (fp != NULL);

  fprintf (fp, "\n");

  if (!fpcache_Enabled)
    {
      fprintf (fp, "Filter predicate cache is disabled.\n");
      return;
    }

  /* NOTE: While dumping information, other threads are still free to modify the existing entries. */

  fprintf (fp, "Filter predicate cache\n");
  fprintf (fp, "Stats: \n");
  fprintf (fp, "Max size:                   %d\n", fpcache_Soft_capacity);
  fprintf (fp, "Current entry count:        %d\n", ATOMIC_INC_32 (&fpcache_Entry_counter, 0));
  fprintf (fp, "Current clone count:        %d\n", ATOMIC_INC_32 (&fpcache_Clone_counter, 0));
  fprintf (fp, "Lookups:                    %ld\n", ATOMIC_LOAD_64 (&fpcache_Stat_lookup));
  fprintf (fp, "Entry Hits:                 %ld\n", ATOMIC_LOAD_64 (&fpcache_Stat_hit));
  fprintf (fp, "Entry Miss:                 %ld\n", ATOMIC_LOAD_64 (&fpcache_Stat_miss));
  fprintf (fp, "Entry discards:             %ld\n", ATOMIC_LOAD_64 (&fpcache_Stat_discard));
  fprintf (fp, "Clone Hits:                 %ld\n", ATOMIC_LOAD_64 (&fpcache_Stat_clone_hit));
  fprintf (fp, "Clone Miss:                 %ld\n", ATOMIC_LOAD_64 (&fpcache_Stat_clone_miss));
  fprintf (fp, "Clone discards:             %ld\n", ATOMIC_LOAD_64 (&fpcache_Stat_clone_discard));
  fprintf (fp, "Adds:                       %ld\n", ATOMIC_LOAD_64 (&fpcache_Stat_add));
  fprintf (fp, "Clone adds:                 %ld\n", ATOMIC_LOAD_64 (&fpcache_Stat_clone_add));
  fprintf (fp, "Cleanups:                   %ld\n", ATOMIC_LOAD_64 (&fpcache_Stat_cleanup));
  fprintf (fp, "Cleaned entries:            %ld\n", ATOMIC_LOAD_64 (&fpcache_Stat_cleanup_entry));

  fprintf (fp, "\nEntries:\n");
  lf_hash_create_iterator (&iter, t_entry, &fpcache_Ht);
  while ((fpcache_entry = (FPCACHE_ENTRY *) lf_hash_iterate (&iter)) != NULL)
    {
      fprintf (fp, "\n  BTID = %d, %d|%d\n", fpcache_entry->btid.root_pageid, fpcache_entry->btid.vfid.volid,
	       fpcache_entry->btid.vfid.fileid);
      fprintf (fp, "  Clones = %d\n", fpcache_entry->clone_stack_head + 1);
    }
  /* TODO: add more. */
}

/*
* fpcache_cleanup () - Cleanup filter predicate cache when soft capacity is exceeded.
*
* return	 : Void.
* thread_p (in) : Thread entry.
 */
static void
fpcache_cleanup (THREAD_ENTRY * thread_p)
{
  LF_HASH_TABLE_ITERATOR iter;
  LF_TRAN_ENTRY *t_entry = thread_get_tran_entry (thread_p, THREAD_TS_FPCACHE);
  FPCACHE_ENTRY *fpcache_entry = NULL;
  FPCACHE_CLEANUP_CANDIDATE candidate;
  int candidate_index;
  int success;

  /* We can allow only one cleanup process at a time. There is no point in duplicating this work. Therefore, anyone
   * trying to do the cleanup should first try to set fpcache_Cleanup_flag. */
  if (!ATOMIC_CAS_32 (&fpcache_Cleanup_flag, 0, 1))
    {
      /* Somebody else does the cleanup. */
      return;
    }
  if (fpcache_Entry_counter <= fpcache_Soft_capacity)
    {
      /* Already cleaned up. */
      if (!ATOMIC_CAS_32 (&fpcache_Cleanup_flag, 1, 0))
	{
	  assert_release (false);
	  fpcache_Cleanup_flag = 0;
	}
      return;
    }

  /* Start cleanup. */

  /* The cleanup is a two-step process:
   * 1. Iterate through hash and select candidates for cleanup. The least recently used entries are sorted into a binary
   *    heap.
   *    NOTE: the binary heap does not story references to hash entries; it stores copies from the candidate keys and
   *    last used timer of course to sort the candidates.
   * 2. Remove collected candidates from hash. Entries must be unfix and no flags must be set.
   */

  assert (fpcache_Cleanup_bh->element_count == 0);
  fpcache_Cleanup_bh->element_count = 0;

  /* Collect candidates for cleanup. */
  lf_hash_create_iterator (&iter, t_entry, &fpcache_Ht);

  while ((fpcache_entry = (FPCACHE_ENTRY *) lf_hash_iterate (&iter)) != NULL)
    {
      candidate.btid = fpcache_entry->btid;
      candidate.time_last_used = fpcache_entry->time_last_used;

      (void) bh_try_insert (fpcache_Cleanup_bh, &candidate, NULL);
    }

  /* Remove candidates from filter predicate cache. */
  for (candidate_index = 0; candidate_index < fpcache_Cleanup_bh->element_count; candidate_index++)
    {
      /* Get candidate at candidate_index. */
      bh_element_at (fpcache_Cleanup_bh, candidate_index, &candidate);

      /* Try delete. */
      if (lf_hash_delete (t_entry, &fpcache_Ht, &candidate.btid, &success) != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  continue;
	}
      if (success)
	{
	  ATOMIC_INC_64 (&fpcache_Stat_cleanup_entry, 1);
	  ATOMIC_INC_64 (&fpcache_Stat_discard, 1);
	  ATOMIC_INC_32 (&fpcache_Entry_counter, -1);
	}
    }

  /* Reset binary heap. */
  fpcache_Cleanup_bh->element_count = 0;

  ATOMIC_INC_64 (&fpcache_Stat_cleanup, 1);
  if (!ATOMIC_CAS_32 (&fpcache_Cleanup_flag, 1, 0))
    {
      assert_release (false);
      fpcache_Cleanup_flag = 0;
    }
}

/*
 * fpcache_compare_cleanup_candidates () - Compare cleanup candidates by their time_last_used. Oldest candidates are
 *                                         considered "greater".
 *
 * return	   : BH_CMP_RESULT:
 *		     BH_GT if left is older.
 *		     BH_LT if right is older.
 *		     BH_EQ if left and right are equal.
 * left (in)	   : Left FPCACHE cleanup candidate.
 * right (in)	   : Right FPCACHE cleanup candidate.
 * ignore_arg (in) : Ignored.
 */
static BH_CMP_RESULT
fpcache_compare_cleanup_candidates (const void *left, const void *right, BH_CMP_ARG ingore_arg)
{
  struct timeval left_timeval = ((FPCACHE_CLEANUP_CANDIDATE *) left)->time_last_used;
  struct timeval right_timeval = ((FPCACHE_CLEANUP_CANDIDATE *) right)->time_last_used;

  /* Lesser means placed in binary heap. So return BH_LT for older timeval. */
  if (left_timeval.tv_sec < right_timeval.tv_sec)
    {
      return BH_LT;
    }
  else if (left_timeval.tv_sec == right_timeval.tv_sec)
    {
      return BH_EQ;
    }
  else
    {
      return BH_GT;
    }
}

/*
 * fpcache_drop_all () - Free all filter predicate cache entries.
 *
 * return	 : Void.
 * thread_p (in) : Thread entry.
 */
void
fpcache_drop_all (THREAD_ENTRY * thread_p)
{
  LF_TRAN_ENTRY *t_entry = thread_get_tran_entry (thread_p, THREAD_TS_FPCACHE);

  /* Reset fpcache_Entry_counter and fpcache_Clone_counter.
   * NOTE: If entries/clones are created concurrently to this, the counters may become a little off. However, exact
   *       counters are not mandatory.
   */
  ATOMIC_INC_64 (&fpcache_Stat_discard, fpcache_Entry_counter);
  fpcache_Entry_counter = 0;

  ATOMIC_INC_64 (&fpcache_Stat_clone_discard, fpcache_Clone_counter);
  fpcache_Clone_counter = 0;

  lf_hash_clear (t_entry, &fpcache_Ht);
}
