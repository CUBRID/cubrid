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
 * XASL cache.
 */

#ident "$Id$"

#include "xasl_cache.h"

#define XCACHE_ENTRY_MARK_DELETED	    ((INT32) 0x80000000)

#define XCACHE_PTR_TO_KEY(ptr) ((XASL_ID *) ptr)
#define XCACHE_PTR_TO_ENTRY(ptr) ((XASL_CACHE_ENTRY *) ptr)

static bool xcache_Enabled = false;
static int xcache_Soft_capacity = 0;
static LF_HASH_TABLE xcache_Ht = LF_HASH_TABLE_INITIALIZER;
static LF_FREELIST xcache_Ht_freelist = LF_FREELIST_INITIALIZER;
/* TODO: Handle counter >= soft capacity. */
static int xcache_Entry_counter = 0;

/* xcache_Entry_descriptor - used for latch-free hash table.
 * we have to declare member functions before instantiating xcache_Entry_descriptor.
 */
static void * xcache_entry_alloc (void);
static int xcache_entry_free (void *entry);
static int xcache_entry_init (void *entry);
static int xcache_entry_uninit (void *entry);
static int xcache_copy_key (void *src, void *dest);
static int xcache_compare_key (void *key1, void *key2);
static unsigned int xcache_hash_key (void *key, int hash_table_size);

static LF_ENTRY_DESCRIPTOR xcache_Entry_descriptor = {
    offsetof (XASL_CACHE_ENTRY, stack),
    offsetof (XASL_CACHE_ENTRY, next),
    offsetof (XASL_CACHE_ENTRY, del_id),
    offsetof (XASL_CACHE_ENTRY, xasl_id),
    0,	/* No mutex. */

    /* mutex flags */
    LF_EM_NOT_USING_MUTEX,

    xcache_entry_alloc,
    xcache_entry_free,
    xcache_entry_init,
    xcache_entry_uninit,
    xcache_copy_key,
    xcache_compare_key,
    xcache_hash_key,
    NULL,		  /* duplicates not accepted. */
  };

static int xcache_find_internal (THREAD_ENTRY * thread_p, XASL_ID * xasl_id, XASL_CACHE_ENTRY ** xcache_entry);
static bool xcache_entry_increment_read_counter (XASL_ID * xid);

int
xcache_initialize (void)
{
  int error_code = NO_ERROR;

  xcache_Enabled = false;
  
  xcache_Soft_capacity = prm_get_integer_value (PRM_ID_XASL_MAX_PLAN_CACHE_ENTRIES);
  if (xcache_Soft_capacity <= 0)
    {
      return NO_ERROR;
    }

  error_code = lf_freelist_init (&xcache_Ht_freelist, 1, xcache_Soft_capacity, &xcache_Entry_descriptor, &xcache_Ts);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }
  
  error_code =
    lf_hash_init (&xcache_Ht, &xcache_Ht_freelist, xcache_Soft_capacity, &xcache_Entry_descriptor);
  if (error_code != NO_ERROR)
    {
      lf_freelist_destroy (&xcache_Ht_freelist);
      return error_code;
    }
  xcache_Entry_counter = 0;

  xcache_Enabled = true;
  return NO_ERROR;
}

void
xcache_finalize (void)
{
  if (!xcache_Enabled)
    {
      return;
    }

  lf_freelist_destroy (&xcache_Ht_freelist);
  lf_hash_destroy (&xcache_Ht);
}

static void *
xcache_entry_alloc (void)
{
  return malloc (sizeof (XASL_CACHE_ENTRY));
}

static int
xcache_entry_free (void *entry)
{
  free (entry);
  return NO_ERROR;
}

static int
xcache_entry_init (void *entry)
{
  XASL_CACHE_ENTRY *xcache_entry = XCACHE_PTR_TO_ENTRY (entry);
  /* Add here if anything should be initialized. */
  xcache_entry->class_locks = NULL;
  xcache_entry->class_oid_list = NULL;
  xcache_entry->tcard_list = NULL;

  XASL_ID_SET_NULL (&xcache_entry->xasl_id);
  return NO_ERROR;
}

static int
xcache_entry_uninit (void *entry)
{
  XASL_CACHE_ENTRY *xcache_entry = XCACHE_PTR_TO_ENTRY (entry);

  if (xcache_entry->class_locks != NULL)
    {
      free_and_init ((void *) xcache_entry->class_locks);
    }
  if (xcache_entry->class_oid_list != NULL)
    {
      free_and_init ((void *) xcache_entry->class_oid_list);
    }
  if (xcache_entry->tcard_list != NULL)
    {
      free_and_init ((void *) xcache_entry->tcard_list);
    }
  return NO_ERROR;
}

static int
xcache_copy_key (void *src, void *dest)
{
  XASL_ID_COPY (XCACHE_PTR_TO_KEY (dest), XCACHE_PTR_TO_KEY (src));
  XCACHE_PTR_TO_KEY (dest)->cache_flag = XCACHE_PTR_TO_KEY (src)->cache_flag;
  return NO_ERROR;
}

static int
xcache_compare_key (void *key1, void *key2)
{
  XASL_ID *lookup_key = XCACHE_PTR_TO_KEY (key1);
  XASL_ID *entry_key = XCACHE_PTR_TO_KEY (key2);
  INT32 cache_flag;

  if (SHA1Compare (&lookup_key->sha1, &entry_key->sha1) != 0)
    {
      /* Not the same query. */
      return -1;
    }
  /* SHA-1 hash matched. */
  if (lookup_key->time_stored.sec != 0)
    {
      /* This is an XASL_ID lookup. We need time stored to also match or it is a different entry than expected. */
      if (lookup_key->time_stored.sec != entry_key->time_stored.sec
	  || lookup_key->time_stored.usec != entry_key->time_stored.usec)
	{
	  /* Not the same XASL_ID. */
	  return -1;
	}
    }
  else
    {
      /* This lookup searches for any valid XASL_ID. */
    }
  /* Now we matched XASL_ID. */

  cache_flag = ATOMIC_INC_32 (&entry_key->cache_flag, 0);
  if (cache_flag | XCACHE_ENTRY_MARK_DELETED)
    {
      /* The entry was marked for deletion. */
      if (lookup_key->cache_flag == cache_flag)
	{
	  /* The deleter found the entry. */
	  return 0;
	}
      else
	{
	  /* This is not the deleter. Ignore this entry until it is removed from hash. */
	  return -1;
	}
    }
  else
    {
      if (!xcache_entry_increment_read_counter (entry_key))
	{
	  /* Entry was deleted. */
	  return -1;
	}
      /* Successfully marked as reader. */
      return 0;
    }

  /* This is a match. */
  return 0;
}

static unsigned int
xcache_hash_key (void *key, int hash_table_size)
{
  XASL_ID *xasl_id = XCACHE_PTR_TO_KEY (key);

  return ((unsigned int) xasl_id->sha1.h[0]) % hash_table_size;
}

static int
xcache_find_internal (THREAD_ENTRY * thread_p, XASL_ID * xid, XASL_CACHE_ENTRY ** xcache_entry)
{
  int error_code = NO_ERROR;
  LF_TRAN_ENTRY *t_entry = thread_get_tran_entry (thread_p, THREAD_TS_XCACHE);
  int oid_index;
  int lock_result;

  assert (xcache_entry != NULL && *xcache_entry == NULL);
  assert (xcache_Enabled);

  error_code = lf_hash_find (t_entry, &xcache_Ht, xid, xcache_entry);
  if (error_code != NO_ERROR)
    {
      /* Error! */
      ASSERT_ERROR ();
      return error_code;
    }

  if (*xcache_entry == NULL)
    {
      /* No match! */
      return NO_ERROR;
    }

  /* Found a match. */

  /* Get lock on all classes in xasl cache entry. */
  for (oid_index = 0; oid_index < (*xcache_entry)->n_oid_list; oid_index++)
    {
      if ((*xcache_entry)->class_locks[oid_index] <= NULL_LOCK)
	{
	  /* No lock. */
	  continue;
	}
      lock_result =
	lock_scan (thread_p, &(*xcache_entry)->class_oid_list[oid_index], LK_UNCOND_LOCK,
		   (*xcache_entry)->class_locks[oid_index]);
      if (lock_result != LK_GRANTED)
	{
	  ASSERT_ERROR_AND_SET (error_code);
	  xcache_entry_decrement_read_counter (thread_p, *xcache_entry);
	  *xcache_entry = NULL;
	  return error_code;
	}
    }
  if ((*xcache_entry)->xasl_id.cache_flag | XCACHE_ENTRY_MARK_DELETED)
    {
      /* Someone has marked entry as deleted. */
      xcache_entry_decrement_read_counter (thread_p, *xcache_entry);
      *xcache_entry = NULL;
    }

  return NO_ERROR;
}

int
xcache_find_sha1 (THREAD_ENTRY * thread_p, SHA1Hash * sha1, XASL_CACHE_ENTRY ** xcache_entry)
{
  XASL_ID lookup_key;
  int error_code = NO_ERROR;

  if (!xcache_Enabled)
    {
      return NO_ERROR;
    }

  XASL_ID_SET_NULL (&lookup_key);
  lookup_key.sha1 = *sha1;

  error_code = xcache_find_internal (thread_p, &lookup_key, xcache_entry);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }
  return NO_ERROR;
}

int
xcache_find_xasl_id (THREAD_ENTRY * thread_p, XASL_ID * xid, XASL_CACHE_ENTRY ** xcache_entry)
{
  int error_code = NO_ERROR;

  if (!xcache_Enabled)
    {
      return NO_ERROR;
    }

  error_code = xcache_find_internal (thread_p, xid, xcache_entry);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }
  return NO_ERROR;
}

static bool
xcache_entry_increment_read_counter (XASL_ID * xid)
{
  INT32 cache_flag;

  assert (xid != NULL);

  do
    {
      cache_flag = ATOMIC_INC_32 (&xid->cache_flag, 0);
      if (cache_flag | XCACHE_ENTRY_MARK_DELETED)
	{
	  /* It was deleted. */
	  return false;
	}
    } while (ATOMIC_CAS_32 (&xid->cache_flag, cache_flag, cache_flag + 1));
  /* Success */
  return true;
}

int
xcache_entry_decrement_read_counter (THREAD_ENTRY * thread_p, XASL_CACHE_ENTRY * xcache_entry)
{
  INT32 cache_flag = 0;
  INT32 new_cache_flag = 0;
  LF_TRAN_ENTRY *t_entry = thread_get_tran_entry (thread_p, THREAD_TS_XCACHE);
  int error_code = NO_ERROR;
  int success = 0;

  /* Decrement the number of users. */
  do
    {
      cache_flag = ATOMIC_INC_32 (&xcache_entry->xasl_id.cache_flag, 0);
      /* Remove myself as reader. */
      new_cache_flag = cache_flag - 1;
      /* Safe guard: we don't decrement readers more than necessary. */
      assert ((new_cache_flag != 0xFFFFFFFF) && (new_cache_flag != 0x7FFFFFFF));
    } while (!ATOMIC_CAS_32 (&xcache_entry->xasl_id.cache_flag, new_cache_flag + 1, new_cache_flag));

  if (new_cache_flag == XCACHE_ENTRY_MARK_DELETED)
    {
      /* I am last user after object was marked as deleted. */
      error_code = lf_hash_delete (t_entry, &xcache_Ht, &xcache_entry->xasl_id, &success);
      if (error_code != NO_ERROR)
	{
	  /* Errors are not expected. */
	  assert (false);
	  return error_code;
	}
      if (success == 0)
	{
	  /* Failure is not expected. */
	  assert (false);
	  return ER_FAILED;
	}
    }
  return NO_ERROR;
}

int
xcache_entry_mark_deleted (THREAD_ENTRY * thread_p, XASL_CACHE_ENTRY * xcache_entry)
{
  LF_TRAN_ENTRY *t_entry = thread_get_tran_entry (thread_p, THREAD_TS_XCACHE);
  INT32 cache_flag = 0;
  INT32 new_cache_flag;
  int error_code = NO_ERROR;
  int success = 0;

  do
    {
      /* Decrement myself as reader and mark entry as deleted. */
      cache_flag = ATOMIC_INC_32 (&xcache_entry->xasl_id.cache_flag, 0);
      new_cache_flag = (cache_flag - 1) | XCACHE_ENTRY_MARK_DELETED;
    } while (ATOMIC_CAS_32 (&xcache_entry->xasl_id.cache_flag, cache_flag, new_cache_flag));

  if (new_cache_flag == XCACHE_ENTRY_MARK_DELETED)
    {
      /* No one else is reading the entry. We can also remove it from hash. */
      error_code = lf_hash_delete (t_entry, &xcache_Ht, &xcache_entry->xasl_id, &success);
      if (error_code != NO_ERROR)
	{
	  /* Errors are not expected. */
	  assert (false);
	  return error_code;
	}
      if (success == 0)
	{
	  /* Failure is not expected. */
	  assert (false);
	  return ER_FAILED;
	}
    }
  /* Success */
  return NO_ERROR;
}


int
xcache_find_or_insert (THREAD_ENTRY * thread_p, SHA1Hash * sha1, XASL_STREAM * stream, const OID * oid, int n_oid,
		       const OID * class_oids, const int * class_locks, const int *tcards, int dbval_cnt,
		       XASL_CACHE_ENTRY ** xcache_entry)
{
  int error_code = NO_ERROR;
  LF_TRAN_ENTRY *t_entry = thread_get_tran_entry (thread_p, THREAD_TS_XCACHE);
  int inserted = 0;

  assert (xcache_entry != NULL);

  if (!xcache_Enabled)
    {
      return NO_ERROR;
    }
  error_code = xcache_find_sha1 (thread_p, sha1, xcache_entry);
  if (error_code != NO_ERROR)
    {
      assert (*xcache_entry == NULL);
      return ER_FAILED;
    }
  if (xcache_entry != NULL)
    {
      /* Entry already in cache. */
      assert (((*xcache_entry)->xasl_id.cache_flag & XCACHE_ENTRY_MARK_DELETED) == 0);
      XASL_ID_COPY (stream->xasl_id, &(*xcache_entry)->xasl_id);
      return NO_ERROR;
    }
  /* Entry not in cache. */
  while (true)
    {
      *xcache_entry = lf_freelist_claim (t_entry, &xcache_Ht_freelist);
      if (*xcache_entry == NULL)
	{
	  ASSERT_ERROR_AND_SET (error_code);
	  return error_code;
	}
      /* Initialize xcache_entry stuff. */
      XASL_ID_COPY (&(*xcache_entry)->xasl_id, stream->xasl_id);
      (*xcache_entry)->xasl_id.sha1 = *sha1;
      (*xcache_entry)->xasl_id.cache_flag = 1;	  /* Set myself as user. */
      /* ... */

      error_code = lf_hash_insert_given (t_entry, &xcache_Ht, &(*xcache_entry)->xasl_id, xcache_entry, &inserted);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  if (*xcache_entry != NULL)
	    {
	      (void) lf_freelist_retire (t_entry, &xcache_Ht_freelist, *xcache_entry);
	    }
	  return error_code;
	}
      assert (*xcache_entry != NULL);
      /* The entry was marked as deleted. Try again to insert again. */
    }
  assert (*xcache_entry != NULL);
  return NO_ERROR;
}

int
xcache_remove_by_oid (THREAD_ENTRY * thread_p, OID * oid)
{
  LF_HASH_TABLE_ITERATOR iter;
  LF_TRAN_ENTRY *t_entry = thread_get_tran_entry (thread_p, THREAD_TS_XCACHE);
  XASL_CACHE_ENTRY *xcache_entry;
  XASL_CACHE_ENTRY *del_prev_entry = NULL;
  int oid_idx;
  int error_code = NO_ERROR;
  int return_error = NO_ERROR;

  lf_hash_create_iterator (&iter, t_entry, &xcache_Ht);

  while (true)
    {
      /* Start by iterating to next hash entry. */
      xcache_entry = lf_hash_iterate (&iter);

      /* Check if previous hash entry must be deleted. */
      if (del_prev_entry != NULL)
	{
	  error_code = xcache_entry_mark_deleted (thread_p, del_prev_entry);
	  if (error_code != NO_ERROR)
	    {
	      assert (false);
	      return_error = error_code;
	    }
	  del_prev_entry = NULL;
	}

      if (xcache_entry == NULL)
	{
	  /* Finished hash iteration. */
	  break;
	}

      for (oid_idx = 0; oid_idx < xcache_entry->n_oid_list; oid_idx++)
	{
	  if (OID_EQ (&xcache_entry->class_oid_list[oid_idx], oid))
	    {
	      /* Save entry to be deleted after we advance to next hash entry. */
	      del_prev_entry = xcache_entry;
	      break;
	    }
	}
    }
  return return_error;
}

/* TODO more stuff
 * xcache_remove_when_full.
 * I have named xcache_Soft_capacity the size of xasl cache because I don't want it to be a hard limit. We will allow
 * it to overflow. When a new entry is inserted and overflow occurs, the inserter takes the responsibility to free up
 * some entries (by marking them as deleted).
 * The hard limit would have to block the xasl cache for a time. Not only that this hash-free table has no means of
 * being blocked, but it is really unnecessary.
 * Freeing up entries could be done in two ways:
 * 1. Similarly to xasl_remove_by_class, we could provide a condition to free entries in one iteration.
 * 2. We could collect a set of victims during first iterate and then delete all collected victims.
 * I don't like the second approach because is too complex, we would have to keep a sorted list of victims which gets
 * updated with each entry.
 * I'd rather like to find a condition based on how often and how recent the entry was used. Anything older than and
 * used less often than would be removed. If that did not remove enough entries, which I hope never happens, we could
 * mark everything as deleted - but the real problem would be that the xasl cache is really too small for the system
 * needs.
 *
 * xcache_check_tcard.
 * the current RT system specifics:
 * 1. it is called only prepare query. It is not called if prepared query execution is called directly (which is the
 * most likely usage).
 * 2. it is called only if original tcard is less than 50 pages.
 * 3. it doesn't care when it was last time called, so if preparing queries is usual, it may get called very often.
 * I think some of the stuff above are not ok.
 * 1. We should be able to check RT for both prepare and execute prepared. So forcing the check on getting xasl cache
 *    entry would be better.
 * 2. There should be no limit on tcard. We could treat the small/big tcards in two ways, but I think both can be
 *    handled.
 * 3. We can have some conditions of when to call it. If we limit the calls to every now and then, the file header page
 *    fix would not have any impact to the overall throughput.
 *
 * Cache clones: no longer used for xasl cache, just filter predicate.
 *
 * There is the pin xasl I managed to understand its overall scope, however I don't understand the issue it was
 * supposed to fix. Maybe we can find a better way to handle the original problem.
 */
