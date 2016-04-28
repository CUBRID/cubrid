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
  BTID btid;		/* B-tree identifier. */
  
  /* Latch-free stuff. */
  FPCACHE_ENTRY *stack;	/* used in freelist */
  FPCACHE_ENTRY *next;	/* used in hash table */
  pthread_mutex_t mutex;	/* Mutex. */
  UINT64 del_id;		/* delete transaction ID (for lock free) */
  const OID class_oid;		/* Class OID. */
  struct timeval time_created;	/* when this entry created */
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
static INT32 fpcache_Entry_counter = 0;
static INT32 fpcache_Clone_counter = 0;
static int fpcache_Clone_stack_size;

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

/* fpcache_Entry_descriptor - used for latch-free hash table.
 * we have to declare member functions before instantiating fpcache_Entry_descriptor.
 */
static void * fpcache_entry_alloc (void);
static int fpcache_entry_free (void *entry);
static int fpcache_entry_init (void *entry);
static int fpcache_entry_uninit (void *entry);
static int fpcache_copy_key (void *src, void *dest);
static int fpcache_compare_key (void *key1, void *key2);
static unsigned int fpcache_hash_key (void *key, int hash_table_size);

static LF_ENTRY_DESCRIPTOR fpcache_Entry_descriptor = {
    offsetof (FPCACHE_ENTRY, stack),
    offsetof (FPCACHE_ENTRY, next),
    offsetof (FPCACHE_ENTRY, del_id),
    offsetof (FPCACHE_ENTRY, btid),
    offsetof (FPCACHE_ENTRY, mutex),	/* No mutex. */

    /* using mutex? */
    LF_EM_USING_MUTEX,

    fpcache_entry_alloc,
    fpcache_entry_free,
    fpcache_entry_init,
    fpcache_entry_uninit,
    fpcache_copy_key,
    btree_compare_btids,
    btree_hash_btid,
    NULL,		  /* duplicates not accepted. */
  };

int
fpcache_initialize (void)
{
  int error_code = NO_ERROR;

  fpcache_Enabled = false;
  
  fpcache_Soft_capacity = prm_get_integer_value (PRM_ID_FILTER_PRED_MAX_CACHE_ENTRIES);
  if (fpcache_Soft_capacity <= 0)
    {
      return NO_ERROR;
    }

  /* Here */
  error_code =
    lf_freelist_init (&fpcache_Ht_freelist, 1, fpcache_Soft_capacity, &fpcache_Entry_descriptor, &fpcache_Ts);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }
  
  error_code =
    lf_hash_init (&fpcache_Ht, &fpcache_Ht_freelist, fpcache_Soft_capacity, &fpcache_Entry_descriptor);
  if (error_code != NO_ERROR)
    {
      lf_freelist_destroy (&fpcache_Ht_freelist);
      return error_code;
    }
  fpcache_Entry_counter = 0;
  fpcache_Clone_counter = 0;

  fpcache_Clone_stack_size = prm_get_integer_value (PRM_ID_FILTER_PRED_MAX_CACHE_CLONES);

  fpcache_Stat_lookup = 0;
  fpcache_Stat_miss = 0;
  fpcache_Stat_hit = 0;
  fpcache_Stat_discard = 0;
  fpcache_Stat_add = 0;
  fpcache_Stat_lookup = 0;
  fpcache_Stat_clone_miss = 0;
  fpcache_Stat_clone_hit = 0;
  fpcache_Stat_clone_discard = 0;

  fpcache_Enabled = true;
  return NO_ERROR;
}

void
fpcache_finalize (void)
{
  if (!fpcache_Enabled)
    {
      return;
    }

  lf_freelist_destroy (&fpcache_Ht_freelist);
  lf_hash_destroy (&fpcache_Ht);
}

static void *
fpcache_entry_alloc (void)
{
  return malloc (sizeof (FPCACHE_ENTRY));
}

static int
fpcache_entry_free (void *entry)
{
  free (entry);
  return NO_ERROR;
}

static int
fpcache_entry_init (void *entry)
{
  FPCACHE_ENTRY *fpcache_entry = FPCACHE_PTR_TO_ENTRY (entry);
  /* Add here if anything should be initialized. */
  /* Allocate clone stack. */
  fpcache_entry->clone_stack = malloc (fpcache_Clone_stack_size * sizeof (PRED_EXPR_WITH_CONTEXT));
  if (fpcache_entry->clone_stack == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
	      fpcache_Clone_stack_size * sizeof (PRED_EXPR_WITH_CONTEXT));
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  fpcache_entry->clone_stack_head = -1;
  return NO_ERROR;
}

static int
fpcache_entry_uninit (void *entry)
{
  FPCACHE_ENTRY *fpcache_entry = FPCACHE_PTR_TO_ENTRY (entry);
  THREAD_ENTRY *thread_p = thread_get_thread_entry_info ();
  HL_HEAPID old_private_heap = db_change_private_heap (thread_p, 0);
  PRED_EXPR_WITH_CONTEXT *pred_expr = NULL;
  int head;

  for (head = fpcache_entry->clone_stack_head; head >= 0; head--)
    {
      pred_expr = fpcache_entry->clone_stack[head];
      assert (pred_expr != NULL);
      stx_free_additional_buff (thread_p, pred_expr->unpack_info);
      stx_free_xasl_unpack_info (pred_expr->unpack_info);
      db_private_free_and_init (thread_p, pred_expr->unpack_info);
    }

  (void) db_change_private_heap (thread_p, old_private_heap);

  free (fpcache_entry->clone_stack);
  return NO_ERROR;
}

static int
fpcache_copy_key (void *src, void *dest)
{
  BTID_COPY ((BTID *) dest, (BTID *) src);
  return NO_ERROR;
}

int
fpcache_claim (THREAD_ENTRY * thread_p, BTID * btid, OR_PREDICATE * or_pred, PRED_EXPR_WITH_CONTEXT ** filter_pred)
{
  LF_TRAN_ENTRY *t_entry = thread_get_tran_entry (thread_p, THREAD_TS_FPCACHE);
  FPCACHE_ENTRY *fpcache_entry = NULL;
  int error_code = NO_ERROR;

  assert (filter_pred != NULL && *filter_pred == NULL);

  if (fpcache_Enabled)
    {
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
	  ATOMIC_INC_64 (&fpcache_Stat_hit, 1);
	  if (fpcache_entry->clone_stack_head >= 0)
	    {
	      assert (fpcache_entry->clone_stack_head < fpcache_Clone_stack_size);
	      *filter_pred = fpcache_entry->clone_stack[fpcache_entry->clone_stack_head--];
	      ATOMIC_INC_64 (&fpcache_Stat_clone_hit, 1);
	      ATOMIC_INC_32 (&fpcache_Clone_counter, -1);
	    }
	  else
	    {
	      ATOMIC_INC_64 (&fpcache_Stat_clone_miss, 1);
	    }
	  pthread_mutex_unlock (&fpcache_entry->mutex);
	}
    }

  if (*filter_pred == NULL)
    {
      /* Allocate new. */
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

int
fpcache_retire (THREAD_ENTRY * thread_p, BTID * btid, PRED_EXPR_WITH_CONTEXT * filter_pred)
{
  LF_TRAN_ENTRY *t_entry = thread_get_tran_entry (thread_p, THREAD_TS_FPCACHE);
  FPCACHE_ENTRY *fpcache_entry = NULL;
  int error_code = NO_ERROR;

  if (fpcache_Enabled)
    {
      ATOMIC_INC_64 (&fpcache_Stat_add, 1);
      error_code = lf_hash_find_or_insert (t_entry, &fpcache_Ht, btid, (void **) &fpcache_entry, NULL);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return ER_FAILED;
	}
      if (fpcache_entry != NULL)
	{
	  /* save filter_pred for later usage. */
	  if (fpcache_entry->clone_stack_head < fpcache_Clone_stack_size - 1)
	    {
	      fpcache_entry->clone_stack[fpcache_Clone_stack_size++] = filter_pred;
	      filter_pred = NULL;
	      ATOMIC_INC_64 (&fpcache_Stat_clone_add, 1);
	      ATOMIC_INC_32 (&fpcache_Clone_counter, 1);
	    }
	  else
	    {
	      ATOMIC_INC_64 (&fpcache_Stat_clone_discard, 1);
	    }
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
      /* Free filter_pred. */
      HL_HEAPID old_private_heap = db_change_private_heap (thread_p, 0);
      stx_free_additional_buff (thread_p, filter_pred->unpack_info);
      stx_free_xasl_unpack_info (filter_pred->unpack_info);
      db_private_free_and_init (thread_p, filter_pred->unpack_info);
      (void) db_change_private_heap (thread_p, old_private_heap);
    }
  return error_code;
}

void
fpcache_remove_by_class (THREAD_ENTRY * thread_p, OID * class_oid)
{
#define FPCACHE_DELETE_BTIDS_SIZE 1024

  LF_HASH_TABLE_ITERATOR iter;
  LF_TRAN_ENTRY *t_entry = thread_get_tran_entry (thread_p, THREAD_TS_FPCACHE);
  FPCACHE_ENTRY *fpcache_entry;
  int dummy_success = 0;
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
	  fpcache_entry = lf_hash_iterate (&iter);

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
		  /* Full buffer. Interrupt iteration, delete entries collected so far and then start over. */
		  lf_tran_end_with_mb (t_entry);
		  break;
		}
	    }
	}

      /* Delete collected btids. */
      for (btid_index = 0; btid_index < n_delete_btids; btid_index++)
	{
	  if (lf_hash_delete (t_entry, &fpcache_Ht, &delete_btids[btid_index], &dummy_success) != NO_ERROR)
	    {
	      assert (false);
	    }
	}
      n_delete_btids = 0;
    }

#undef FPCACHE_DELETE_BTIDS_SIZE
}

void
fpcache_dump (THREAD_ENTRY * thread_p, FILE * fp)
{
  LF_HASH_TABLE_ITERATOR iter;
  LF_TRAN_ENTRY *t_entry = thread_get_tran_entry (thread_p, THREAD_TS_FPCACHE);
  FPCACHE_ENTRY *fpcache_entry = NULL;

  assert (fp);

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
  fprintf (fp, "Lookups:                    %ld\n", ATOMIC_INC_64 (&fpcache_Stat_lookup, 0));
  fprintf (fp, "Entry Hits:                 %ld\n", ATOMIC_INC_64 (&fpcache_Stat_hit, 0));
  fprintf (fp, "Entry Miss:                 %ld\n", ATOMIC_INC_64 (&fpcache_Stat_miss, 0));
  fprintf (fp, "Entry discards:             %ld\n", ATOMIC_INC_64 (&fpcache_Stat_discard, 0));
  fprintf (fp, "Clone Hits:                 %ld\n", ATOMIC_INC_64 (&fpcache_Stat_clone_hit, 0));
  fprintf (fp, "Clone Miss:                 %ld\n", ATOMIC_INC_64 (&fpcache_Stat_clone_miss, 0));
  fprintf (fp, "Clone discards:             %ld\n", ATOMIC_INC_64 (&fpcache_Stat_clone_discard, 0));
  fprintf (fp, "Adds:                       %ld\n", ATOMIC_INC_64 (&fpcache_Stat_add, 0));
  fprintf (fp, "Clone adds:                 %ld\n", ATOMIC_INC_64 (&fpcache_Stat_clone_add, 0));

  fprintf (fp, "\nEntries:\n");
  lf_hash_create_iterator (&iter, t_entry, &fpcache_Ht);
  while ((fpcache_entry = lf_hash_iterate (&iter)) != NULL)
    {
      fprintf (fp, "\n  BTID = %d, %d|%d\n", fpcache_entry->btid.root_pageid, fpcache_entry->btid.vfid.volid,
	       fpcache_entry->btid.vfid.fileid);
      fprintf (fp, "  Clones = %d\n", fpcache_entry->clone_stack_head + 1);
    }
  /* TODO: add more. */
}

/* TODO: Keep maximum size stable. */
