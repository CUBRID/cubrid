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
  return NO_ERROR;
}

static int
xcache_compare_key (void *key1, void *key2)
{
  XASL_ID *lookup_key = XCACHE_PTR_TO_KEY (key1);
  XASL_ID *entry_key = XCACHE_PTR_TO_KEY (key2);

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

  if ((entry_key->cache_flag | XCACHE_ENTRY_MARK_DELETED) && lookup_key->cache_flag != entry_key->cache_flag)
    {
      /* We are not the deleter. */
      return -1;
    }
  else
    {
      assert ((entry_key->cache_flag | XCACHE_ENTRY_MARK_DELETED) == 0 || lookup_key->cache_flag == 0x80000000);
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
  int retry_count = 0;
  int error_code = NO_ERROR;
  LF_TRAN_ENTRY *t_entry = thread_get_tran_entry (thread_p, THREAD_TS_XCACHE);
#if defined (SERVER_MODE)
  INT32 cache_flag;
  int class_index;
  int lock_result;
#endif /* SERVER_MODE */

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

#if defined (SERVER_MODE)
  /* Found a match. */
  /* We cannot yet get entry until we mark it as used, by incrementing user counter in cache_flag. */

  /* We need to consider that in the short time between finding entry and setting user counter, it is possible that
   * someone marks the entry as deleted, prohibiting use from using it from now on.
   */
  do
    {
      /* xasl_id.cache_flag field is used to manage thread concurrency. */
      cache_flag = ATOMIC_INC_32 (&(*xcache_entry)->xasl_id.cache_flag, 0);
      if (cache_flag & XCACHE_ENTRY_MARK_DELETED)
	{
	  /* Someone marked the entry as deleted after we found it.
	   * It is very unlikely that someone already added a new entry for this key, in this short time.
	   * Therefore, it's better to give up.
	   */
	  /* TODO: investigate is_xasl_pinned_reference. */
	  *xcache_entry = NULL;
	  return NO_ERROR;
	}
      /* Now try to increment the user counter. */
    } while (!ATOMIC_CAS_32 (&(*xcache_entry)->xasl_id.cache_flag, cache_flag, cache_flag + 1));
  /* Successfully incremented user counter. */

  /* Get lock on all classes in xasl cache entry. */
  for (class_index = 0; class_index < (*xcache_entry)->n_oid_list; class_index++)
    {
      lock_result =
	lock_scan (thread_p, &(*xcache_entry)->class_oid_list[class_index], LK_UNCOND_LOCK,
		   (*xcache_entry)->class_locks[class_index]);
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
#else /* !SERVER_MODE */ /* SA_MODE */
  assert ((((*xcache_entry)->xasl_id.cache_flag & XCACHE_ENTRY_MARK_DELETED) == 0));
#endif /* SA_MODE */

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

int
xcache_entry_decrement_read_counter (THREAD_ENTRY * thread_p, XASL_CACHE_ENTRY * xcache_entry)
{
  INT32 new_cache_flag = 0;
  LF_TRAN_ENTRY *t_entry = thread_get_tran_entry (thread_p, THREAD_TS_XCACHE);
  int error_code = NO_ERROR;
  int success = 0;

  /* Decrement the number of users. */
  do
    {
      new_cache_flag = ATOMIC_INC_32 (&xcache_entry->xasl_id.cache_flag, 0) - 1;
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
xcache_find_or_insert (THREAD_ENTRY * thread_p, SHA1Hash * sha1, XASL_STREAM * stream, const OID * oid, int n_oid,
		       const OID * class_oids, const int * class_locks, const int *tcards, int dbval_cnt,
		       XASL_CACHE_ENTRY ** xcache_entry)
{
  int error_code = NO_ERROR;
  LF_TRAN_ENTRY *t_entry = thread_get_tran_entry (thread_p, THREAD_TS_XCACHE);
  int cache_flag;

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
      /* ... */

      error_code = lf_hash_insert_given (t_entry, &xcache_Ht, &(*xcache_entry)->xasl_id, xcache_entry);
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
      /* Mark myself as reader. */
      do 
	{
	  cache_flag = ATOMIC_INC_32 (&(*xcache_entry)->xasl_id.cache_flag, 0);
	  if (cache_flag | XCACHE_ENTRY_MARK_DELETED)
	    {
	      /* Somebody already deleted the entry... Try again. */
	      break;
	    }
	} while (ATOMIC_CAS_32 (&(*xcache_entry)->xasl_id.cache_flag, cache_flag - 1, cache_flag));
      if (cache_flag | XCACHE_ENTRY_MARK_DELETED)
	{
	  /* Cache entry is deleted. It must have been found first, and then someone deleted it. */
	  continue;
	}
      else
	{
	  assert (cache_flag > 0);
	  break;
	}
    }
  assert (*xcache_entry != NULL);
  return NO_ERROR;
}

/* TODO more stuff
 * xcache_remove_by_class.
 * xcache_remove_when_full.
 */
