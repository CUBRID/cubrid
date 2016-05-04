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
#include "perf_monitor.h"

#define XCACHE_ENTRY_MARK_DELETED	    ((INT32) 0x80000000)
#define XCACHE_ENTRY_TO_BE_RECOMPILED	    ((INT32) 0x40000000)
#define XCACHE_ENTRY_WAS_RECOMPILED	    ((INT32) 0x20000000)
#define XCACHE_ENTRY_SKIP_TO_BE_RECOMPILED  ((INT32) 0x10000000)
#define XCACHE_ENTRY_FLAGS_MASK		    ((INT32) 0xFF000000)

#define XCACHE_ENTRY_FIX_COUNT_MASK	    ((INT32) 0x00FFFFFF)

#define XCACHE_PTR_TO_KEY(ptr) ((XASL_ID *) ptr)
#define XCACHE_PTR_TO_ENTRY(ptr) ((XASL_CACHE_ENTRY *) ptr)

/* xcache statistics. */
typedef struct xcache_stats XCACHE_STATS;
struct xcache_stats
{
  INT64 lookups;
  INT64 hits;
  INT64 miss;
  INT64 recompiles;
  INT64 failed_recompiles;
  INT64 deletes;
  INT64 cleanups;
  INT64 fix;
  INT64 unfix;
  INT64 inserts;
  INT64 found_at_insert;
};
#define XCACHE_STATS_INITIALIZER { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }

/* Structure to include all xasl cache global variable. It is easier to visualize the entire system when debugging. */
typedef struct xcache XCACHE;
struct xcache
{
  bool enabled;
  int soft_capacity;
  LF_HASH_TABLE ht;
  LF_FREELIST freelist;
  INT32 entry_count;
  bool removed_temp_vols;
  bool logging_enabled;

  XCACHE_STATS stats;
};
XCACHE xcache_Global =
  {
    false,
    0,
    LF_HASH_TABLE_INITIALIZER,
    LF_FREELIST_INITIALIZER,
    0,
    false,
    false,
    XCACHE_STATS_INITIALIZER
  };

/* Create macro's for xcache_Global fields to access them as if they were global variables. */
#define xcache_Enabled xcache_Global.enabled
#define xcache_Soft_capacity xcache_Global.soft_capacity
#define xcache_Ht xcache_Global.ht
#define xcache_Ht_freelist xcache_Global.freelist
#define xcache_Entry_count xcache_Global.entry_count
#define xcache_Temp_vols_were_removed xcache_Global.removed_temp_vols
#define xcache_Log xcache_Global.logging_enabled

/* Statistics */
#define XCACHE_STAT_GET(name) ATOMIC_INC_64 (&xcache_Global.stats.name, 0)
#define XCACHE_STAT_INC(name) ATOMIC_INC_64 (&xcache_Global.stats.name, 1)

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

    /* using mutex? */
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

#define XCACHE_ATOMIC_READ_CACHE_FLAG(xid) (ATOMIC_INC_32 (&(xid)->cache_flag, 0))
#define XCACHE_ATOMIC_TAS_CACHE_FLAG(xid, cf) (ATOMIC_TAS_32 (&(xid)->cache_flag, cf))
#define XCACHE_ATOMIC_CAS_CACHE_FLAG(xid, oldcf, newcf) (ATOMIC_CAS_32 (&(xid)->cache_flag, oldcf, newcf))

/* Logging macro's */
#define xcache_check_logging() (xcache_Log = prm_get_bool_value (PRM_ID_XASL_CACHE_LOGGING))
#define xcache_log(...) if (xcache_Log) _er_log_debug (ARG_FILE_LINE, "XASL CACHE: " __VA_ARGS__)
#define xcache_log_error(...) if (xcache_Log) _er_log_debug (ARG_FILE_LINE, "XASL CACHE ERROR: " __VA_ARGS__)

#define XCACHE_LOG_TRAN_TEXT		  "\t tran = %d \n"
#define XCACHE_LOG_TRAN_ARGS(thrd) LOG_FIND_THREAD_TRAN_INDEX (thrd)

#define XCACHE_LOG_ERROR_TEXT		  "\t error_code = %d \n"

#define XCACHE_LOG_ENTRY_PTR_TEXT	  "\t\t entry ptr = %p \n"

#define XCACHE_LOG_SHA1_TEXT		  "\t\t\t sha1 = %08x | %08x | %08x | %08x | %08x \n"
#define XCACHE_LOG_SHA1_ARGS(sha1) SHA1_AS_ARGS (sha1)

#define XCACHE_LOG_FVPID_TEXT		  "\t\t\t first vpid = %d|%d \n"
#define XCACHE_LOG_TEMPVFID_TEXT	  "\t\t\t temp vfid = %d|%d \n"
#define XCACHE_LOG_TIME_STORED_TEXT	  "\t\t\t time stored = %d sec, %d usec \n"
#define XCACHE_LOG_EXEINFO_TEXT		  "\t\t\t user text = %s \n"					  \
					  "\t\t\t plan text = %s \n"					  \
					  "\t\t\t hash text = %s \n"

#define XCACHE_LOG_XASL_ID_TEXT(msg)									  \
  "\t\t " msg ": \n"											  \
  XCACHE_LOG_SHA1_TEXT											  \
  XCACHE_LOG_FVPID_TEXT											  \
  XCACHE_LOG_TEMPVFID_TEXT										  \
  XCACHE_LOG_TIME_STORED_TEXT
#define XCACHE_LOG_XASL_ID_ARGS(xid)									  \
  SHA1_AS_ARGS (&(xid)->sha1),										  \
  VPID_AS_ARGS (&(xid)->first_vpid),									  \
  VFID_AS_ARGS (&(xid)->temp_vfid),									  \
  CACHE_TIME_AS_ARGS (&(xid)->time_stored)

#define XCACHE_LOG_ENTRY_TEXT(msg)	 								  \
  "\t " msg ": \n"											  \
  XCACHE_LOG_ENTRY_PTR_TEXT										  \
  XCACHE_LOG_XASL_ID_TEXT ("xasl_id")									  \
  "\t\t sql_info: \n"											  \
  XCACHE_LOG_EXEINFO_TEXT										  \
  "\t\t n_oids = %d \n"
#define XCACHE_LOG_ENTRY_ARGS(xent)									  \
  (xent),												  \
  XCACHE_LOG_XASL_ID_ARGS (&(xent)->xasl_id),								  \
  EXEINFO_AS_ARGS(&(xent)->sql_info),									  \
  (xent)->n_related_objects

#define XCACHE_LOG_OBJECT_TEXT		  "\t\t\t oid = %d|%d|%d \n"					  \
					  "\t\t\t lock = %s \n"						  \
					  "\t\t\t tcard = %d \n"
#define XCACHE_LOG_ENTRY_OBJECT_TEXT(msg)								  \
  "\t\t " msg ": \n"											  \
  XCACHE_LOG_OBJECT_TEXT
#define XCACHE_LOG_ENTRY_OBJECT_ARGS(xent, oidx)							  \
  OID_AS_ARGS (&(xent)->related_objects[oidx].oid),							  \
  LOCK_TO_LOCKMODE_STRING ((xent)->related_objects[oidx].lock),						  \
  (xent)->related_objects[oidx].tcard

static bool xcache_fix (XASL_ID * xid);
static bool xcache_entry_mark_deleted (THREAD_ENTRY * thread_p, XASL_CACHE_ENTRY * xcache_entry);

/*
 * xcache_initialize () - Initialize XASL cache.
 *
 * return : Error Code.
 */
int
xcache_initialize (void)
{
  int error_code = NO_ERROR;

  xcache_Enabled = false;

  xcache_check_logging ();
  
  xcache_Soft_capacity = prm_get_integer_value (PRM_ID_XASL_CACHE_MAX_ENTRIES);
  if (xcache_Soft_capacity <= 0)
    {
      xcache_log ("disabled.\n");
      return NO_ERROR;
    }

  error_code = lf_freelist_init (&xcache_Ht_freelist, 1, xcache_Soft_capacity, &xcache_Entry_descriptor, &xcache_Ts);
  if (error_code != NO_ERROR)
    {
      xcache_log_error ("could not init freelist.\n");
      return error_code;
    }
  
  error_code =
    lf_hash_init (&xcache_Ht, &xcache_Ht_freelist, xcache_Soft_capacity, &xcache_Entry_descriptor);
  if (error_code != NO_ERROR)
    {
      lf_freelist_destroy (&xcache_Ht_freelist);
      xcache_log_error ("could not init hash table.\n");
      return error_code;
    }

  xcache_Temp_vols_were_removed = false;

  xcache_log ("init successful.\n");

  xcache_Enabled = true;
  return NO_ERROR;
}

/*
 * xcache_finalize () - Finalize XASL cache.
 *
 * return : Void.
 */
void
xcache_finalize (void)
{
  if (!xcache_Enabled)
    {
      return;
    }

  xcache_check_logging ();
  xcache_log ("finalize.\n");

  lf_freelist_destroy (&xcache_Ht_freelist);
  lf_hash_destroy (&xcache_Ht);

  xcache_Enabled = false;
}

/*
 * xcache_entry_alloc () - Allocate an XASL cache entry.
 *
 * return : Pointer to allocated memory.
 */
static void *
xcache_entry_alloc (void)
{
  return malloc (sizeof (XASL_CACHE_ENTRY));
}

/*
 * xcache_entry_free () - Free an XASL cache entry.
 *
 * return     : NO_ERROR.
 * entry (in) : XASL cache entry pointer.
 */
static int
xcache_entry_free (void *entry)
{
  free (entry);
  return NO_ERROR;
}

/*
 * xcache_entry_init () - Initialize new XASL cache entry.
 *
 * return     : NO_ERROR.
 * entry (in) : XASL cache entry pointer.
 */
static int
xcache_entry_init (void *entry)
{
  XASL_CACHE_ENTRY *xcache_entry = XCACHE_PTR_TO_ENTRY (entry);
  /* Add here if anything should be initialized. */
  xcache_entry->related_objects = NULL;
  xcache_entry->ref_count = 0;

  xcache_entry->sql_info.sql_hash_text = NULL;
  xcache_entry->sql_info.sql_user_text = NULL;
  xcache_entry->sql_info.sql_plan_text = NULL;

  XASL_ID_SET_NULL (&xcache_entry->xasl_id);
  xcache_entry->free_data_on_uninit = false;
  xcache_entry->initialized = true;
  return NO_ERROR;
}

/*
 * xcache_entry_uninit () - Uninitialize XASL cache entry.
 *
 * return     : NO_ERROR.
 * entry (in) : XASL cache entry pointer.
 */
static int
xcache_entry_uninit (void *entry)
{
  XASL_CACHE_ENTRY *xcache_entry = XCACHE_PTR_TO_ENTRY (entry);
  THREAD_ENTRY *thread_p = NULL;

  /* No fixed count or this is claimed & retired immediately. */
  assert ((xcache_entry->xasl_id.cache_flag & XCACHE_ENTRY_FIX_COUNT_MASK) == 0
	  || ((xcache_entry->xasl_id.cache_flag & XCACHE_ENTRY_FIX_COUNT_MASK) == 1
	      && !xcache_entry->free_data_on_uninit));

  if (!xcache_entry->initialized)
    {
      /* Already uninitialized? */
      assert (false);
      return NO_ERROR;
    }

  if (xcache_entry->free_data_on_uninit)
    {
      xcache_log ("uninit an entry from cache and free its data: \n"
		  XCACHE_LOG_ENTRY_TEXT("xasl cache entry")
		  XCACHE_LOG_TRAN_TEXT,
		  XCACHE_LOG_ENTRY_ARGS (xcache_entry),
		  XCACHE_LOG_TRAN_ARGS (thread_p));

      if (xcache_entry->related_objects != NULL)
	{
	  free_and_init (xcache_entry->related_objects);
	}

      if (xcache_entry->sql_info.sql_hash_text != NULL)
	{
	  free_and_init (xcache_entry->sql_info.sql_hash_text);
	}

      if (XASL_ID_IS_NULL (&xcache_entry->xasl_id))
	{
	  assert (false);
	}
      else
	{
	  /* NOTE: During shutdown, some lock-free hash entries may remain in so called "retired list". They were not
	   *       uninitialized when xcache_finalize () was called, and are uninitialized much later, after temporary
	   *       volumes have been destroyed.
	   *       In this case, xcache_Temp_vols_were_removed should be true, and we have to skip destroying temp_vfid.
	   */
	  if (!xcache_Temp_vols_were_removed)
	    {
	      /* Destroy the temporary file used to store the XASL. */
	      (void) file_destroy (thread_get_thread_entry_info (), &xcache_entry->xasl_id.temp_vfid);
	    }
	  XASL_ID_SET_NULL (&xcache_entry->xasl_id);
	}
    }
  else
    {
      xcache_log ("uninit an entry without freeing its data: \n"
		  XCACHE_LOG_ENTRY_TEXT("xasl cache entry")
		  XCACHE_LOG_TRAN_TEXT,
		  XCACHE_LOG_ENTRY_ARGS (xcache_entry),
		  XCACHE_LOG_TRAN_ARGS (thread_p));
      xcache_entry->related_objects = NULL;
      xcache_entry->sql_info.sql_hash_text = NULL;
      xcache_entry->sql_info.sql_plan_text = NULL;
      xcache_entry->sql_info.sql_user_text = NULL;
      XASL_ID_SET_NULL (&xcache_entry->xasl_id);
    }
  xcache_entry->initialized = false;
  return NO_ERROR;
}

/*
 * xcache_copy_key () - Dummy copy key function; XASL cache entry and its key are initialized before being inserted.
 *
 * return    : NO_ERROR.
 * src (in)  : Dummy key source.
 * dest (in) : Dummy key destination.
 */
static int
xcache_copy_key (void *src, void *dest)
{
  /* Key is already set before insert. */
  XASL_ID *xid = (XASL_ID *) dest;
  THREAD_ENTRY *thread_p = NULL;

#if !defined (NDEBUG)
  assert (xid->cache_flag == 1);    /* One reader, no flags. */
#endif /* !NDEBUG */

  xcache_log ("dummy copy key call: \n"
	      XCACHE_LOG_XASL_ID_TEXT("key")
	      XCACHE_LOG_TRAN_TEXT,
	      XCACHE_LOG_XASL_ID_ARGS (xid),
	      XCACHE_LOG_TRAN_ARGS (thread_p));

  return NO_ERROR;
}

/*
 * xcache_compare_key () - XASL hash compare key function.
 *
 * return    : 0 for match, != 0 for no match.
 * key1 (in) : Lookup key.
 * key2 (in) : Entry key.
 */
static int
xcache_compare_key (void *key1, void *key2)
{
  XASL_ID *lookup_key = XCACHE_PTR_TO_KEY (key1);
  XASL_ID *entry_key = XCACHE_PTR_TO_KEY (key2);
  INT32 cache_flag;
  THREAD_ENTRY *thread_p = NULL;

  /* Compare key algorithm depends on sha1 and cache flags.
   * SHA-1 is generated hash based on query hash text.
   * If SHA-1 does not match, the entry does not belong to the same query (so clearly no match).
   *
   * Even if SHA-1 hash matches, the cache flags can still invalidate the entry.
   * 1. Marked deleted entry can only be found with the scope of deleting entry (the lookup key must also be marked as
   *	deleted.
   * 2. Was recompiled entry can never be found. They are followed by another entry with similar SHA-1 which will can
   *	be found.
   * 3. To be recompiled entry cannot be found by its recompiler - lookup key is marked as skip to be recompiled.
   * 4. When all previous flags do not invalidate entry, the thread looking for entry must also increment fix count.
   *	Incrementing fix count can fail if concurrent thread mark entry as deleted.
   */

  if (SHA1Compare (&lookup_key->sha1, &entry_key->sha1) != 0)
    {
      /* Not the same query. */
      
      xcache_log ("compare keys: sha1 mismatch\n"
		  "\t\t lookup key: \n" XCACHE_LOG_SHA1_TEXT
		  "\t\t  entry key: \n" XCACHE_LOG_SHA1_TEXT
		  XCACHE_LOG_TRAN_TEXT,
		  XCACHE_LOG_SHA1_ARGS (&lookup_key->sha1),
		  XCACHE_LOG_SHA1_ARGS (&entry_key->sha1),
		  XCACHE_LOG_TRAN_ARGS (thread_p));
      return -1;
    }
  /* SHA-1 hash matched. */
  /* Now we matched XASL_ID. */

  cache_flag = XCACHE_ATOMIC_READ_CACHE_FLAG (entry_key);
  if (cache_flag & XCACHE_ENTRY_MARK_DELETED)
    {
      /* The entry was marked for deletion. */
      if (lookup_key->cache_flag == cache_flag)
	{
	  /* The deleter found its entry. */
	  xcache_log ("compare keys: found for delete\n"
		      "\t\t lookup key: \n" XCACHE_LOG_SHA1_TEXT
		      "\t\t  entry key: \n" XCACHE_LOG_SHA1_TEXT
		      XCACHE_LOG_TRAN_TEXT,
		      XCACHE_LOG_SHA1_ARGS (&lookup_key->sha1),
		      XCACHE_LOG_SHA1_ARGS (&entry_key->sha1),
		      XCACHE_LOG_TRAN_ARGS (thread_p));
	  return 0;
	}
      else
	{
	  /* This is not the deleter. Ignore this entry - it will be removed from hash. */

	  xcache_log ("(tran=%d) compare keys: skip deleted\n"
		      "\t\t lookup key: \n" XCACHE_LOG_SHA1_TEXT
		      "\t\t  entry key: \n" XCACHE_LOG_SHA1_TEXT
		      XCACHE_LOG_TRAN_TEXT,
		      XCACHE_LOG_SHA1_ARGS (&lookup_key->sha1),
		      XCACHE_LOG_SHA1_ARGS (&entry_key->sha1),
		      XCACHE_LOG_TRAN_ARGS (thread_p));
	  return -1;
	}
    }
  
  if (cache_flag & XCACHE_ENTRY_WAS_RECOMPILED)
    {
      /* Go to another entry. */

      xcache_log ("compare keys: skip recompiled\n"
		  "\t\t lookup key: \n" XCACHE_LOG_SHA1_TEXT
		  "\t\t  entry key: \n" XCACHE_LOG_SHA1_TEXT
		  XCACHE_LOG_TRAN_TEXT,
		  XCACHE_LOG_SHA1_ARGS (&lookup_key->sha1),
		  XCACHE_LOG_SHA1_ARGS (&entry_key->sha1),
		  XCACHE_LOG_TRAN_ARGS (thread_p));
      return - 1;
    }

  if ((cache_flag & XCACHE_ENTRY_TO_BE_RECOMPILED) && (lookup_key->cache_flag & XCACHE_ENTRY_SKIP_TO_BE_RECOMPILED))
    {
      /* We are trying to insert a new entry to replace the entry to be recompiled. Skip this. */

      xcache_log ("compare keys: skip to be recompiled\n"
		  "\t\t lookup key: \n" XCACHE_LOG_SHA1_TEXT
		  "\t\t  entry key: \n" XCACHE_LOG_SHA1_TEXT
		  XCACHE_LOG_TRAN_TEXT,
		  XCACHE_LOG_SHA1_ARGS (&lookup_key->sha1),
		  XCACHE_LOG_SHA1_ARGS (&entry_key->sha1),
		  XCACHE_LOG_TRAN_ARGS (thread_p));
      return -1;
    }

  /* The entry is what we are looking for. One last step, we must increment read counter in cache flag. */
  if (!xcache_fix (entry_key))
    {
      /* Entry was deleted. */

      xcache_log ("compare keys: could not fix\n"
		  "\t\t lookup key: \n" XCACHE_LOG_SHA1_TEXT
		  "\t\t  entry key: \n" XCACHE_LOG_SHA1_TEXT
		  XCACHE_LOG_TRAN_TEXT,
		  XCACHE_LOG_SHA1_ARGS (&lookup_key->sha1),
		  XCACHE_LOG_SHA1_ARGS (&entry_key->sha1),
		  XCACHE_LOG_TRAN_ARGS (thread_p));
      return -1;
    }

  /* Successfully marked as reader. */
  xcache_log ("compare keys: key matched and fixed\n"
	      "\t\t lookup key: \n" XCACHE_LOG_SHA1_TEXT
	      "\t\t  entry key: \n" XCACHE_LOG_SHA1_TEXT
	      XCACHE_LOG_TRAN_TEXT,
	      XCACHE_LOG_SHA1_ARGS (&lookup_key->sha1),
	      XCACHE_LOG_SHA1_ARGS (&entry_key->sha1),
	      XCACHE_LOG_TRAN_ARGS (thread_p));
  return 0;
}

/*
 * xcache_hash_key () - Hash index based on key SHA-1.
 *
 * return		: Hash index.
 * key (in)	        : Key value.
 * hash_table_size (in) : Hash size.
 */
static unsigned int
xcache_hash_key (void *key, int hash_table_size)
{
  XASL_ID *xasl_id = XCACHE_PTR_TO_KEY (key);
  unsigned int hash_index = ((unsigned int) xasl_id->sha1.h[0]) % hash_table_size;

  xcache_log ("hash index: \n"
	      XCACHE_LOG_SHA1_TEXT
	      "\t\t hash index value = %d \n"
	      XCACHE_LOG_TRAN_TEXT,
	      XCACHE_LOG_SHA1_ARGS (&xasl_id->sha1));

  return hash_index;
}

/*
 * xcache_find_sha1 () - Lookup XASL cache by SHA-1.
 *
 * return	      : Error code.
 * thread_p (in)      : Thread entry.
 * sha1 (in)	      : SHA-1 hash.
 * xcache_entry (out) : XASL cache entry if found.
 */
int
xcache_find_sha1 (THREAD_ENTRY * thread_p, const SHA1Hash * sha1, XASL_CACHE_ENTRY ** xcache_entry)
{
  XASL_ID lookup_key;
  int error_code = NO_ERROR;
  LF_TRAN_ENTRY *t_entry = thread_get_tran_entry (thread_p, THREAD_TS_XCACHE);
  int oid_index;
  int lock_result;

  assert (xcache_entry != NULL && *xcache_entry == NULL);

  if (!xcache_Enabled)
    {
      return NO_ERROR;
    }

  xcache_check_logging ();

  XCACHE_STAT_INC (lookups);
  mnt_pc_lookup (thread_p);

  XASL_ID_SET_NULL (&lookup_key);
  lookup_key.sha1 = *sha1;

  error_code = lf_hash_find (t_entry, &xcache_Ht, &lookup_key, (void **) xcache_entry);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      xcache_log_error ("error finding cache entry: \n"
			XCACHE_LOG_SHA1_TEXT
			XCACHE_LOG_ERROR_TEXT
			XCACHE_LOG_TRAN_TEXT,
			XCACHE_LOG_SHA1_ARGS (&lookup_key.sha1),
			error_code,
			XCACHE_LOG_TRAN_ARGS (thread_p));

      return error_code;
    }
  if (*xcache_entry == NULL)
    {
      /* No match! */
      XCACHE_STAT_INC (miss);
      mnt_pc_miss (thread_p);
      xcache_log ("could not find cache entry: \n"
		  XCACHE_LOG_SHA1_TEXT
		  XCACHE_LOG_TRAN_TEXT,
		  XCACHE_LOG_SHA1_ARGS (&lookup_key.sha1),
		  XCACHE_LOG_TRAN_ARGS (thread_p));

      return NO_ERROR;
    }
  /* Found a match. */
  /* We have incremented fix count, we don't need lf_tran anymore. */
  lf_tran_end_with_mb (t_entry);

  /* Get lock on all classes in xasl cache entry. */
  for (oid_index = 0; oid_index < (*xcache_entry)->n_related_objects; oid_index++)
    {
      if ((*xcache_entry)->related_objects[oid_index].lock <= NULL_LOCK)
	{
	  /* No lock. */
	  continue;
	}
      lock_result =
	lock_scan (thread_p, &(*xcache_entry)->related_objects[oid_index].oid, LK_UNCOND_LOCK,
		   (*xcache_entry)->related_objects[oid_index].lock);
      if (lock_result != LK_GRANTED)
	{
	  ASSERT_ERROR_AND_SET (error_code);
	  xcache_unfix (thread_p, *xcache_entry);
	  *xcache_entry = NULL;
	  XCACHE_STAT_INC (miss);
	  mnt_pc_miss (thread_p);
	  xcache_log ("could not get cache entry because lock on oid failed: \n"
		      XCACHE_LOG_ENTRY_TEXT("entry")
		      XCACHE_LOG_ENTRY_OBJECT_TEXT("object that could not be locked")
		      XCACHE_LOG_TRAN_TEXT,
		      XCACHE_LOG_ENTRY_ARGS (*xcache_entry),
		      XCACHE_LOG_ENTRY_OBJECT_ARGS (*xcache_entry, oid_index),
		      XCACHE_LOG_TRAN_ARGS (thread_p));

	  return error_code;
	}
    }

  if ((*xcache_entry)->xasl_id.cache_flag & XCACHE_ENTRY_MARK_DELETED)
    {
      /* Someone has marked entry as deleted. */
      xcache_log ("could not get cache entry because it was deleted until locked: \n"
		  XCACHE_LOG_ENTRY_TEXT("entry")
		  XCACHE_LOG_TRAN_TEXT,
		  XCACHE_LOG_ENTRY_ARGS (*xcache_entry),
		  XCACHE_LOG_TRAN_ARGS (thread_p));

      xcache_unfix (thread_p, *xcache_entry);
      XCACHE_STAT_INC (miss);
      mnt_pc_miss (thread_p);
      *xcache_entry = NULL;
    }
  else
    {
      XCACHE_STAT_INC (hits);
      mnt_pc_hit (thread_p);
    }

  xcache_log ("found cache entry by sha1: \n"
	      XCACHE_LOG_ENTRY_TEXT("entry")
	      XCACHE_LOG_TRAN_TEXT,
	      XCACHE_LOG_ENTRY_ARGS (*xcache_entry),
	      XCACHE_LOG_TRAN_ARGS (thread_p));
  return NO_ERROR;
}

/*
 * xcache_find_xasl_id () - Find XASL cache entry by XASL_ID. Besides matching SHA-1, we have to match time_stored.
 *
 * return	     : NO_ERROR.
 * thread_p (in)     : Thread entry.
 * xid (in)	     : XASL_ID.
 * xcache_entry (in) : XASL cache entry if found.
 */
int
xcache_find_xasl_id (THREAD_ENTRY * thread_p, const XASL_ID * xid, XASL_CACHE_ENTRY ** xcache_entry)
{
  int error_code = NO_ERROR;

  assert (xid != NULL);
  assert (xcache_entry != NULL && *xcache_entry == NULL);

  error_code = xcache_find_sha1 (thread_p, &xid->sha1, xcache_entry);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }
  if (*xcache_entry == NULL)
    {
      /* No entry was found. */
      return NO_ERROR;
    }
  if ((*xcache_entry)->xasl_id.time_stored.sec != xid->time_stored.sec
      || (*xcache_entry)->xasl_id.time_stored.usec != xid->time_stored.usec)
    {
      /* We don't know if this XASL cache entry is good for us. We need to restart by recompiling. */
      xcache_log ("could not get cache entry because time_stored mismatch \n"
		  XCACHE_LOG_ENTRY_TEXT ("entry")
		  XCACHE_LOG_XASL_ID_TEXT ("lookup xasl_id")
		  XCACHE_LOG_TRAN_TEXT,
		  XCACHE_LOG_ENTRY_ARGS (*xcache_entry),
		  XCACHE_LOG_XASL_ID_ARGS (xid),
		  XCACHE_LOG_TRAN_ARGS (thread_p));
      xcache_unfix (thread_p, *xcache_entry);
      *xcache_entry = NULL;

      /* TODO:
       * The one reason we cannot accept this cache entry is because one of the referenced classes might have suffered
       * a schema change. Or maybe a serial may have been altered, although I am not sure this can actually affect our
       * plan.
       * Instead of using time_stored, we could find another way to identify if an XASL cache entry is still usable.
       * Something that could detect if classes have been modified (and maybe serials).
       */
    }
  else
    {
      xcache_log ("found cache entry by xasl_id: \n"
		  XCACHE_LOG_ENTRY_TEXT ("entry")
		  XCACHE_LOG_XASL_ID_TEXT ("lookup xasl_id")
		  XCACHE_LOG_TRAN_TEXT,
		  XCACHE_LOG_ENTRY_ARGS (*xcache_entry),
		  XCACHE_LOG_XASL_ID_ARGS (xid),
		  XCACHE_LOG_TRAN_ARGS (thread_p));
    }
  return NO_ERROR;
}

/*
 * xcache_fix () - Fix XASL cache entry by incrementing fix count in cache flag.
 *
 * return   : Error code.
 * xid (in) : XASL_ID.
 */
static bool
xcache_fix (XASL_ID * xid)
{
  INT32 cache_flag;

  assert (xid != NULL);

  XCACHE_STAT_INC (fix);

  do
    {
      cache_flag = XCACHE_ATOMIC_READ_CACHE_FLAG (xid);
#if defined (SA_MODE)
      assert ((cache_flag & XCACHE_ENTRY_FIX_COUNT_MASK) == 0);
#endif /* SA_MODE */
      if (cache_flag & XCACHE_ENTRY_MARK_DELETED)
	{
	  /* It was deleted. */
	  return false;
	}
    } while (!XCACHE_ATOMIC_CAS_CACHE_FLAG (xid, cache_flag, cache_flag + 1));

  /* Success */
  return true;
}

/*
 * xcache_unfix () - Unfix XASL cache entry by decrementing fix count in cache flag. If we are last to use entry
 *		     remove it from hash.
 *
 * return	     : Void.
 * thread_p (in)     : Thread entry.
 * xcache_entry (in) : XASL cache entry.
 */
void
xcache_unfix (THREAD_ENTRY * thread_p, XASL_CACHE_ENTRY * xcache_entry)
{
  INT32 cache_flag = 0;
  INT32 new_cache_flag = 0;
  LF_TRAN_ENTRY *t_entry = thread_get_tran_entry (thread_p, THREAD_TS_XCACHE);
  int error_code = NO_ERROR;
  int success = 0;
  struct timeval time_last_used;

  assert (xcache_entry != NULL);
  assert (xcache_Enabled);

  /* Mark last used. We need to do an atomic operation here. We cannot set both tv_sec and tv_usec and we don't have to.
   * Setting tv_sec is enough.
   */
  (void) gettimeofday (&time_last_used, NULL);
  ATOMIC_TAS_32 (&xcache_entry->time_last_used.tv_sec, time_last_used.tv_sec);

  XCACHE_STAT_INC (unfix);
  ATOMIC_INC_64 (&xcache_entry->ref_count, 1);

  /* Decrement the number of users. */
  do
    {
      cache_flag = XCACHE_ATOMIC_READ_CACHE_FLAG (&xcache_entry->xasl_id);
      new_cache_flag = cache_flag;

      /* There should be at least one fix. */
      assert ((new_cache_flag & XCACHE_ENTRY_FIX_COUNT_MASK) != 0);

      /* Unfix */
      new_cache_flag = cache_flag - 1;
      if (new_cache_flag == XCACHE_ENTRY_TO_BE_RECOMPILED)
	{
	  /* We are the last to have fixed to be recompiled entry. This is an invalid state.
	   * The recompiler should set the entry as "was recompiled" before unfixing it!
	   */
	  assert (false);
	  xcache_log_error ("unexpected cache_flag = XCACHE_ENTRY_TO_BE_RECOMPILED on unfix: \n"
			    XCACHE_LOG_ENTRY_TEXT("invalid entry")
			    XCACHE_LOG_TRAN_TEXT,
			    XCACHE_LOG_ENTRY_ARGS (xcache_entry),
			    XCACHE_LOG_TRAN_ARGS (thread_p));
	  /* Delete the entry. */
	  new_cache_flag = XCACHE_ENTRY_MARK_DELETED;
	}
      else if (new_cache_flag == XCACHE_ENTRY_MARK_DELETED)
	{
	  /* If entry is marked as deleted and we are the last thread to have fixed this entry, we must remove it. */
	}
      else if (new_cache_flag == XCACHE_ENTRY_WAS_RECOMPILED)
	{
	  /* This the last thread to have fixed the entry and we should mark it as deleted and remove it. */
	  new_cache_flag = XCACHE_ENTRY_MARK_DELETED;
	}
    } while (!XCACHE_ATOMIC_CAS_CACHE_FLAG (&xcache_entry->xasl_id, cache_flag, new_cache_flag));

  xcache_log ("unfix entry: \n"
	      XCACHE_LOG_ENTRY_TEXT ("entry")
	      XCACHE_LOG_TRAN_TEXT,
	      XCACHE_LOG_ENTRY_ARGS (xcache_entry),
	      XCACHE_LOG_TRAN_ARGS (thread_p));

  if (new_cache_flag == XCACHE_ENTRY_MARK_DELETED)
    {
      /* I am last user after object was marked as deleted. */
      xcache_log ("delete entry from hash after unfix: \n"
		  XCACHE_LOG_ENTRY_TEXT ("entry")
		  XCACHE_LOG_TRAN_TEXT,
		  XCACHE_LOG_ENTRY_ARGS (xcache_entry),
		  XCACHE_LOG_TRAN_ARGS (thread_p));
      error_code = lf_hash_delete (t_entry, &xcache_Ht, &xcache_entry->xasl_id, &success);
      if (error_code != NO_ERROR)
	{
	  /* Errors are not expected. */
	  assert (false);
	  return;
	}
      if (success == 0)
	{
	  /* Failure is not expected. */
	  assert (false);
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  return;
	}
    }
}

/*
 * xcache_entry_mark_deleted () - Mark XASL cache entry for delete.
 *
 * return	     : True if no one else is using the entry and can be removed from hash.
 * thread_p (in)     : Thread entry.
 * xcache_entry (in) : XASL cache entry.
 */
static bool
xcache_entry_mark_deleted (THREAD_ENTRY * thread_p, XASL_CACHE_ENTRY * xcache_entry)
{
  LF_TRAN_ENTRY *t_entry = thread_get_tran_entry (thread_p, THREAD_TS_XCACHE);
  INT32 cache_flag = 0;
  INT32 new_cache_flag;
  int error_code = NO_ERROR;
  int success = 0;

  /* Mark for delete. We must successfully set XCACHE_ENTRY_MARK_DELETED flag. */
  do
    {
      cache_flag = XCACHE_ATOMIC_READ_CACHE_FLAG (&xcache_entry->xasl_id);
      if (cache_flag & XCACHE_ENTRY_MARK_DELETED)
	{
	  /* Somebody else deleted? I don't think we can accept it. */
	  assert (false);
	  xcache_log_error ("tried to mark entry as deleted, but somebody else already marked it: \n"
			    XCACHE_LOG_ENTRY_TEXT ("entry")
			    XCACHE_LOG_TRAN_TEXT,
			    XCACHE_LOG_ENTRY_ARGS (xcache_entry),
			    XCACHE_LOG_TRAN_ARGS (thread_p));
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  return false;
	}
      if (cache_flag & XCACHE_ENTRY_TO_BE_RECOMPILED)
	{
	  /* Somebody is compiling the entry? I think the locks have been messed up. */
	  xcache_log_error ("tried to mark entry as deleted, but it was marked as to be recompiled: \n"
			    XCACHE_LOG_ENTRY_TEXT ("entry")
			    XCACHE_LOG_TRAN_TEXT,
			    XCACHE_LOG_ENTRY_ARGS (xcache_entry),
			    XCACHE_LOG_TRAN_ARGS (thread_p));
	  assert (false);
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  return false;
	}

      new_cache_flag = cache_flag;
      if (new_cache_flag & XCACHE_ENTRY_WAS_RECOMPILED)
	{
	  /* This can happen. Somebody recompiled the entry and it was not (yet) removed. We will replace the flag
	   * with XCACHE_ENTRY_MARK_DELETED. */
	  new_cache_flag &= ~XCACHE_ENTRY_WAS_RECOMPILED;
	}
      new_cache_flag = new_cache_flag | XCACHE_ENTRY_MARK_DELETED;
    } while (!XCACHE_ATOMIC_CAS_CACHE_FLAG (&xcache_entry->xasl_id, cache_flag, new_cache_flag));

  xcache_log ("marked entry as deleted: \n"
	      XCACHE_LOG_ENTRY_TEXT ("entry")
	      XCACHE_LOG_TRAN_TEXT,
	      XCACHE_LOG_ENTRY_ARGS (xcache_entry),
	      XCACHE_LOG_TRAN_ARGS (thread_p));

  XCACHE_STAT_INC (deletes);
  mnt_pc_delete (thread_p);
  ATOMIC_INC_32 (&xcache_Entry_count, -1);

  /* The entry can be deleted if the only fixer is this transaction. */
  return (new_cache_flag == XCACHE_ENTRY_MARK_DELETED);
}

/*
 * xcache_insert () - Insert or recompile XASL cache entry.
 *
 * return	      : Error code.
 * thread_p (in)      : Thread entry.
 * context (in)	      : Compile context (sql_info & recompile_xasl).
 * stream (in)	      : XASL stream.
 * oid (in)	      : User OID (unused). TODO: Remove me.
 * n_oid (in)	      : Related objects count.
 * class_oids (in)    : Related objects OID's.
 * class_locks (in)   : Related objects locks.
 * tcards (in)	      : Related objects cardinality.
 * xcache_entry (out) : XASL cache entry.
 */
int
xcache_insert (THREAD_ENTRY * thread_p, const COMPILE_CONTEXT * context, XASL_STREAM * stream, const OID * oid,
	       int n_oid, const OID * class_oids, const int * class_locks, const int *tcards,
	       XASL_CACHE_ENTRY ** xcache_entry)
{
  int error_code = NO_ERROR;
  LF_TRAN_ENTRY *t_entry = thread_get_tran_entry (thread_p, THREAD_TS_XCACHE);
  int inserted = 0;
  XASL_ID xid;
  INT32 cache_flag;
  INT32 new_cache_flag;
  XASL_CACHE_ENTRY *to_be_recompiled = NULL;
  XCACHE_RELATED_OBJECT *related_objects = NULL;
  char *sql_hash_text = NULL;
  char *sql_user_text = NULL;
  char *sql_plan_text = NULL;

  assert (xcache_entry != NULL && *xcache_entry == NULL);
  assert (stream != NULL);
  assert (stream->xasl_id != NULL && !XASL_ID_IS_NULL (stream->xasl_id));

  if (!xcache_Enabled)
    {
      return NO_ERROR;
    }

  xcache_check_logging ();

  XASL_ID_SET_NULL (&xid);
  xid.sha1 = context->sha1;

  XCACHE_STAT_INC (inserts);

  /* Allocate XASL cache entry data. */
  if (n_oid > 0)
    {
      int index;
      related_objects = (XCACHE_RELATED_OBJECT *) malloc (n_oid * sizeof (XCACHE_RELATED_OBJECT));
      if (related_objects == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
		  n_oid * sizeof (XCACHE_RELATED_OBJECT));
	  error_code = ER_OUT_OF_VIRTUAL_MEMORY;
	  goto error;
	}
      for (index = 0; index < n_oid; index++)
	{
	  related_objects[index].oid = class_oids[index];
	  related_objects[index].lock = class_locks[index];
	  related_objects[index].tcard = tcards[index];
	}
    }

  if (xcache_Log)
    {
      /* Do we want to add the strings even if xcache logging is not enabled?
       * E.g. to print them when XASL cache is dumped?
       */
      int sql_hash_text_len = 0, sql_user_text_len = 0, sql_plan_text_len = 0;
      char *strbuf = NULL;

      sql_hash_text_len = strlen (context->sql_hash_text) + 1;
      if (context->sql_user_text != NULL)
	{
	  sql_user_text_len = strlen (context->sql_user_text) + 1;
	}
      if (context->sql_plan_text != NULL)
	{
	  sql_plan_text_len = strlen (context->sql_plan_text) + 1;
	}
      strbuf = (char *) malloc (sql_hash_text_len + sql_user_text_len + sql_plan_text_len);
      if (strbuf == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
		  sql_hash_text_len + sql_user_text_len + sql_plan_text_len);
	  error_code = ER_OUT_OF_VIRTUAL_MEMORY;
	  goto error;
	}

      memcpy (strbuf, context->sql_hash_text, sql_hash_text_len);
      sql_hash_text = strbuf;
      strbuf += sql_hash_text_len;
      
      if (sql_user_text_len > 0)
	{
	  memcpy (strbuf, context->sql_user_text, sql_user_text_len);
	  sql_user_text = strbuf;
	  strbuf += sql_user_text_len;
	}
      
      if (sql_plan_text_len > 0)
	{
	  memcpy (strbuf, context->sql_plan_text, sql_plan_text_len);
	  sql_plan_text = strbuf;
	}
    }


  /* We need to do a loop here for recompile_xasl case. It will break after the first iteration if recompile_xasl flag
   * is false.
   *
   * When we want to recompile the XASL cache entry, we try to avoid blocking others from using existing cache entry.
   * If an entry exists, the recompiler mark it "to be recompiled". Concurrent transactions can find and use this
   * entry. After adding new entry, the original is marked as "was recompiled". This entry can no longer be found
   * but is still valid if it was previously obtained.
   *
   * Things get messy if there are at least two concurrent recompilers. We assume that this does not (or should not)
   * happen in real-world scenarios. But if it happens, we need to make it work.
   * Multiple recompilers can loop here several times. One entry can be recompiled by one thread at a time. Others will
   * loop until current recompiler finishes.
   */
  while (true)
    {
      /* Claim a new entry from freelist to initialize. */
      *xcache_entry = lf_freelist_claim (t_entry, &xcache_Ht_freelist);
      if (*xcache_entry == NULL)
	{
	  ASSERT_ERROR_AND_SET (error_code);
	  return error_code;
	}

      /* Initialize xcache_entry stuff. */
      XASL_ID_COPY (&(*xcache_entry)->xasl_id, stream->xasl_id);
      (*xcache_entry)->xasl_id.sha1 = context->sha1;
      (*xcache_entry)->xasl_id.cache_flag = 1;	  /* Start with fix count = 1. */
      (*xcache_entry)->n_related_objects = n_oid;
      (*xcache_entry)->related_objects = related_objects;
      (*xcache_entry)->sql_info.sql_hash_text = sql_hash_text;
      (*xcache_entry)->sql_info.sql_user_text = sql_user_text;
      (*xcache_entry)->sql_info.sql_plan_text = sql_plan_text;

      /* Now that new entry is initialized, we can try to insert it. */
      error_code = lf_hash_insert_given (t_entry, &xcache_Ht, &xid, (void **) xcache_entry, &inserted);
      if (error_code != NO_ERROR)
	{
	  xcache_log_error ("error inserting new entry: \n",
			    XCACHE_LOG_ENTRY_TEXT ("entry"),
			    XCACHE_LOG_ERROR_TEXT
			    XCACHE_LOG_TRAN_TEXT,
			    XCACHE_LOG_ENTRY_ARGS (*xcache_entry),
			    XCACHE_LOG_TRAN_ARGS (thread_p),
			    error_code);
	  ASSERT_ERROR ();
	  goto error;
	}
      assert (*xcache_entry != NULL);

      /* We have incremented fix count, we don't need lf_tran anymore. */
      lf_tran_end_with_mb (t_entry);

      if (inserted)
	{
	  if (!context->recompile_xasl)
	    {
	      /* This is a new entry. If recompile_xasl flag is true, then this replaces another cache entry. */
	      ATOMIC_INC_32 (&xcache_Entry_count, 1);
	    }
	  (*xcache_entry)->free_data_on_uninit = true;
	  mnt_pc_add (thread_p);
	}
      else
	{
	  XCACHE_STAT_INC (found_at_insert);
	}

      if (inserted || !context->recompile_xasl)
	{
	  /* The entry is accepted. */

	  if (to_be_recompiled != NULL)
	    {
	      assert (context->recompile_xasl);
	      /* Now that we inserted new cache entry, we can mark the old entry as recompiled. */
	      do
		{
		  cache_flag = XCACHE_ATOMIC_READ_CACHE_FLAG (&to_be_recompiled->xasl_id);
		  if ((cache_flag & XCACHE_ENTRY_FLAGS_MASK) != XCACHE_ENTRY_TO_BE_RECOMPILED)
		    {
		      /* Unexpected flags. */
		      assert (false);
		      xcache_log_error ("unexpected flag for entry to be recompiled: \n"
					XCACHE_LOG_ENTRY_TEXT ("entry to be recompiled"),
					"\t cache_flag = %d\n"
					XCACHE_LOG_TRAN_TEXT,
					XCACHE_LOG_ENTRY_ARGS (to_be_recompiled),
					cache_flag,
					XCACHE_LOG_TRAN_ARGS (thread_p));
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_XASLNODE, 0);
		      error_code = ER_QPROC_INVALID_XASLNODE;
		      goto error;
		    }
		  new_cache_flag = (cache_flag & XCACHE_ENTRY_FIX_COUNT_MASK) | XCACHE_ENTRY_WAS_RECOMPILED;
		} while (!XCACHE_ATOMIC_CAS_CACHE_FLAG (&to_be_recompiled->xasl_id, cache_flag, new_cache_flag));
	      /* We marked the entry as recompiled. */
	      xcache_log ("marked entry as recompiled: \n"
			  XCACHE_LOG_ENTRY_TEXT ("entry")
			  XCACHE_LOG_TRAN_TEXT,
			  XCACHE_LOG_ENTRY_ARGS (to_be_recompiled),
			  XCACHE_LOG_TRAN_ARGS (thread_p));
	      xcache_unfix (thread_p, to_be_recompiled);
	      to_be_recompiled = NULL;
	    }

	  xcache_log ("successful find or insert: \n"
		      XCACHE_LOG_ENTRY_TEXT ("entry found or inserted")
		      "\t found or inserted = %s \n"
		      "\t recompile xasl = %s \n"
		      XCACHE_LOG_TRAN_TEXT,
		      XCACHE_LOG_ENTRY_ARGS (*xcache_entry),
		      inserted ? "inserted" : "found",
		      context->recompile_xasl ? "true" : "false",
		      XCACHE_LOG_TRAN_ARGS (thread_p));
	  break;
	}

      assert (!inserted && context->recompile_xasl);
      assert (to_be_recompiled == NULL);
      /* We want to refresh the xasl cache entry, not to use existing. */
      /* Mark existing as to be recompiled. */
      do 
	{
	  cache_flag = XCACHE_ATOMIC_READ_CACHE_FLAG (&(*xcache_entry)->xasl_id);
	  if (cache_flag & XCACHE_ENTRY_MARK_DELETED)
	    {
	      /* Deleted? We certainly did not expect. */
	      assert (false);
	      xcache_log_error ("(recompile) entry is marked as deleted: \n"
				XCACHE_LOG_ENTRY_TEXT ("entry"),
				XCACHE_LOG_TRAN_TEXT,
				XCACHE_LOG_ENTRY_ARGS (*xcache_entry),
				XCACHE_LOG_TRAN_ARGS (thread_p));
	      xcache_unfix (thread_p, *xcache_entry);
	      *xcache_entry = NULL;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_XASLNODE, 0);
	      error_code = ER_QPROC_INVALID_XASLNODE;
	      goto error;
	    }
	  if (cache_flag & (XCACHE_ENTRY_TO_BE_RECOMPILED | XCACHE_ENTRY_WAS_RECOMPILED))
	    {
	      /* Somebody else recompiles this entry. Loop again. */
	      xcache_log ("(recompile) entry is recompiled by somebody else: \n"
			  XCACHE_LOG_ENTRY_TEXT ("entry"),
			  XCACHE_LOG_TRAN_TEXT,
			  XCACHE_LOG_ENTRY_ARGS (*xcache_entry),
			  XCACHE_LOG_TRAN_ARGS (thread_p));
	      XCACHE_STAT_INC (failed_recompiles);
	      xcache_unfix (thread_p, *xcache_entry);
	      *xcache_entry = NULL;
	      break;
	    }
	  /* Set XCACHE_ENTRY_TO_BE_RECOMPILED to be recompiled flag. */
	  new_cache_flag = cache_flag | XCACHE_ENTRY_TO_BE_RECOMPILED;
	} while (!XCACHE_ATOMIC_CAS_CACHE_FLAG (&(*xcache_entry)->xasl_id, cache_flag, new_cache_flag));

      if (*xcache_entry != NULL)
	{
	  /* We have marked this entry to be recompiled. We have to insert new and then we will mark it as recompiled.
	   */
	  to_be_recompiled = *xcache_entry;
	  *xcache_entry = NULL;
	  XCACHE_STAT_INC (recompiles);

	  xcache_log ("(recompile) we marked entry to be recompiled: \n"
		      XCACHE_LOG_ENTRY_TEXT ("entry")
		      XCACHE_LOG_TRAN_TEXT,
		      XCACHE_LOG_ENTRY_ARGS (to_be_recompiled),
		      XCACHE_LOG_TRAN_ARGS (thread_p));

	  /* We have to "inherit" the time_stored from this entry. The new XASL cache entry should be usable by anyone
	   * that cached this entry on client. Currently, xcache_find_xasl_id uses time_stored to match the entries.
	   */
	  stream->xasl_id->time_stored = to_be_recompiled->xasl_id.time_stored;

	  /* On next find or insert, we want to skip the to be recompiled entry. */
	  xid.cache_flag = XCACHE_ENTRY_SKIP_TO_BE_RECOMPILED;
	  /* We don't unfix yet. */
	}
      /* Try to insert again. */
    }

  if (!inserted)
    {
      /* Free allocated resources. */
      if (related_objects)
	{
	  free (related_objects);
	}
      if (sql_hash_text)
	{
	  free (sql_hash_text);
	}
    }

  /* Found or inserted entry. */
  assert (*xcache_entry != NULL);
  return NO_ERROR;

error:
  assert (error_code != NO_ERROR);
  ASSERT_ERROR ();
  if ((*xcache_entry) != NULL)
    {
      (void) lf_freelist_retire (t_entry, &xcache_Ht_freelist, *xcache_entry);
    }
  if (to_be_recompiled)
    {
      /* Remove to be recompiled flag. */
      do 
	{
	  cache_flag = XCACHE_ATOMIC_READ_CACHE_FLAG (&to_be_recompiled->xasl_id);
	  new_cache_flag = cache_flag & (~XCACHE_ENTRY_TO_BE_RECOMPILED);
	} while (!XCACHE_ATOMIC_CAS_CACHE_FLAG (&to_be_recompiled->xasl_id, cache_flag, new_cache_flag));
      xcache_unfix (thread_p, to_be_recompiled);
    }
  if (related_objects)
    {
      free (related_objects);
    }
  if (sql_hash_text)
    {
      free (sql_hash_text);
    }
  return error_code;
}

/*
 * xcache_remove_by_oid () - Remove all XASL cache entries related to given object.
 *
 * return	 : Void.
 * thread_p (in) : Thread entry.
 * oid (in)	 : Object ID.
 */
void
xcache_remove_by_oid (THREAD_ENTRY * thread_p, OID * oid)
{
#define XCACHE_DELETE_XIDS_SIZE 1024
  LF_HASH_TABLE_ITERATOR iter;
  LF_TRAN_ENTRY *t_entry = thread_get_tran_entry (thread_p, THREAD_TS_XCACHE);
  XASL_CACHE_ENTRY *xcache_entry = NULL;
  int oid_idx;
  bool can_delete = false;
  int success;
  XASL_ID delete_xids[XCACHE_DELETE_XIDS_SIZE];
  int n_delete_xids = 0;
  int xid_index = 0;
  bool finished = false;

  if (!xcache_Enabled)
    {
      return;
    }

  xcache_check_logging ();

  xcache_log ("remove all entries: \n"
	      "\t OID = %d|%d|%d \n"
	      XCACHE_LOG_TRAN_TEXT,
	      OID_AS_ARGS (oid),
	      XCACHE_LOG_TRAN_ARGS (thread_p));

  while (!finished)
    {
      /* Create iterator. */
      lf_hash_create_iterator (&iter, t_entry, &xcache_Ht);
      
      /* Iterate through hash, check entry OID's and if one matches the argument, mark the entry for delete and save
       * it in delete_xids buffer. We cannot delete them from hash while iterating, because the one lock-free
       * transaction can be used for one hash entry only.
       */
      while (true)
	{
	  xcache_entry = lf_hash_iterate (&iter);
	  if (xcache_entry == NULL)
	    {
	      finished = true;
	      break;
	    }
	  
	  for (oid_idx = 0; oid_idx < xcache_entry->n_related_objects; oid_idx++)
	    {
	      if (OID_EQ (&xcache_entry->related_objects[oid_idx].oid, oid))
		{
		  /* Mark entry as deleted. */
		  if (xcache_entry_mark_deleted (thread_p, xcache_entry))
		    {
		      /* Successfully marked for delete. Save it to delete after the iteration. */
		      delete_xids[n_delete_xids++] = xcache_entry->xasl_id;
		    }
		  break;
		}
	    }
	  
	  if (n_delete_xids == XCACHE_DELETE_XIDS_SIZE)
	    {
	      /* Full buffer. Interrupt iteration and we'll start over. */
	      lf_tran_end_with_mb (t_entry);

	      xcache_log ("xcache_remove_by_oid full buffer\n"
			  XCACHE_LOG_TRAN_TEXT, XCACHE_LOG_TRAN_ARGS (thread_p));

	      break;
	    }
	}

      /* Remove collected entries. */
      for (xid_index = 0; xid_index < n_delete_xids; xid_index++)
	{
	  if (lf_hash_delete (t_entry, &xcache_Ht, &delete_xids[xid_index], &success) != NO_ERROR)
	    {
	      assert (false);
	    }
	  if (success == 0)
	    {
	      /* I don't think this is expected. */
	      assert (false);
	    }
	}
      n_delete_xids = 0;
    }

#undef XCACHE_DELETE_XIDS_SIZE
}

/*
 * xcache_dump () - Dump XASL cache.
 *
 * return	 : Void.
 * thread_p (in) : Thread entry.
 * fp (out)	 : Output.
 */
void
xcache_dump (THREAD_ENTRY * thread_p, FILE * fp)
{
  LF_HASH_TABLE_ITERATOR iter;
  LF_TRAN_ENTRY *t_entry = thread_get_tran_entry (thread_p, THREAD_TS_XCACHE);
  XASL_CACHE_ENTRY *xcache_entry = NULL;
  int oid_index;

  assert (fp);

  fprintf (fp, "\n");

  if (!xcache_Enabled)
    {
      fprintf (fp, "XASL cache is disabled.\n");
      return;
    }

  /* NOTE: While dumping information, other threads are still free to modify the existing entries. */

  fprintf (fp, "XASL cache\n");
  fprintf (fp, "Stats: \n");
  fprintf (fp, "Max size:                   %d\n", xcache_Soft_capacity);
  fprintf (fp, "Current entry count:        %d\n", ATOMIC_INC_32 (&xcache_Entry_count, 0));
  fprintf (fp, "Lookups:                    %ld\n", XCACHE_STAT_GET (lookups));
  fprintf (fp, "Hits:                       %ld\n", XCACHE_STAT_GET (hits));
  fprintf (fp, "Miss:                       %ld\n", XCACHE_STAT_GET (miss));
  fprintf (fp, "Inserts:                    %ld\n", XCACHE_STAT_GET (inserts));
  fprintf (fp, "Found at insert:            %ld\n", XCACHE_STAT_GET (found_at_insert));
  fprintf (fp, "Recompiles:                 %ld\n", XCACHE_STAT_GET (recompiles));
  fprintf (fp, "Failed recompiles:          %ld\n", XCACHE_STAT_GET (failed_recompiles));
  fprintf (fp, "Deletes:                    %ld\n", XCACHE_STAT_GET (deletes));
  fprintf (fp, "Fix:                        %ld\n", XCACHE_STAT_GET (fix));
  fprintf (fp, "Unfix:                      %ld\n", XCACHE_STAT_GET (unfix));
  fprintf (fp, "Cleanups:                   %ld\n", XCACHE_STAT_GET (cleanups));
  /* add overflow, RT checks. */

  fprintf (fp, "\nEntries:\n");
  lf_hash_create_iterator (&iter, t_entry, &xcache_Ht);
  while ((xcache_entry = lf_hash_iterate (&iter)) != NULL)
    {
      fprintf (fp, "\n");
      fprintf (fp, "  XASL_ID = { \n");
      fprintf (fp, "              sha1 = { %08x %08x %08x %08x %08x }, \n",
	       SHA1_AS_ARGS (&xcache_entry->xasl_id.sha1));
      fprintf (fp, "              first_vpid = %d|%d, \n",
	       xcache_entry->xasl_id.first_vpid.volid, xcache_entry->xasl_id.first_vpid.pageid);
      fprintf (fp, "              temp_vfid = %d|%d, \n",
	       xcache_entry->xasl_id.temp_vfid.volid, xcache_entry->xasl_id.temp_vfid.fileid);
      fprintf (fp, "	          time_stored = %d sec, %d usec \n",
	       xcache_entry->xasl_id.time_stored.sec, xcache_entry->xasl_id.time_stored.usec);
      fprintf (fp, "            } \n");
      fprintf (fp, "  fix_count = %d \n",
	       XCACHE_ATOMIC_READ_CACHE_FLAG (&xcache_entry->xasl_id) & XCACHE_ENTRY_FIX_COUNT_MASK);
      fprintf (fp, "  cache flags = %08x \n",
	       XCACHE_ATOMIC_READ_CACHE_FLAG (&xcache_entry->xasl_id) & XCACHE_ENTRY_FLAGS_MASK);
      fprintf (fp, "  reference count = %ld \n", ATOMIC_INC_64 (&xcache_entry->ref_count, 0));

      fprintf (fp, "  OID_LIST (count = %d): \n", xcache_entry->n_related_objects);
      for (oid_index = 0; oid_index < xcache_entry->n_related_objects; oid_index++)
	{
	  fprintf (fp, "    OID = %d|%d|%d, LOCK = %s, TCARD = %8d \n",
		   OID_AS_ARGS (&xcache_entry->related_objects[oid_index].oid),
		   LOCK_TO_LOCKMODE_STRING (xcache_entry->related_objects[oid_index].lock),
		   xcache_entry->related_objects[oid_index].tcard);
	}
    }

  /* TODO: add more */
}

/*
 * xcache_can_entry_cache_list () - Can entry cache list files?
 *
 * return	     : True/false.
 * xcache_entry (in) : XASL cache entry.
 */
bool
xcache_can_entry_cache_list (XASL_CACHE_ENTRY * xcache_entry)
{
  if (!xcache_Enabled)
    {
      return false;
    }
  return (xcache_entry != NULL
	  && (XCACHE_ATOMIC_READ_CACHE_FLAG (&xcache_entry->xasl_id) & XCACHE_ENTRY_FLAGS_MASK) == 0);
}

/*
 * xcache_notify_removed_temp_vols () - Notify XASL cache system that temporary files have been removed. Any entries
 *				        that are uninitialized afterwards should not destroy temporary files.
 *
 * return    : Void.
 */
void
xcache_notify_removed_temp_vols (void)
{
  xcache_Temp_vols_were_removed = true;
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
 */
