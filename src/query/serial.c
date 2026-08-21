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
 * serial.c - Serial number handling routine
 */

#ident "$Id$"

#include "config.h"

#include <assert.h>
#include <errno.h>

#include "serial.h"
#include "storage_common.h"
#include "heap_file.h"
#include "log_append.hpp"
#include "numeric_opfunc.h"
#include "object_primitive.h"
#include "record_descriptor.hpp"
#include "server_interface.h"
#include "xserver_interface.h"
#include "slotted_page.h"
#include "dbtype.h"
#include "xasl_cache.h"
#include "thread_lockfree_hash_map.hpp"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

/* attribute of _db_serial class */
typedef enum
{
  SERIAL_ATTR_UNIQUE_NAME_INDEX,
  SERIAL_ATTR_NAME_INDEX,
  SERIAL_ATTR_OWNER_INDEX,
  SERIAL_ATTR_CURRENT_VAL_INDEX,
  SERIAL_ATTR_INCREMENT_VAL_INDEX,
  SERIAL_ATTR_MAX_VAL_INDEX,
  SERIAL_ATTR_MIN_VAL_INDEX,
  SERIAL_ATTR_CYCLIC_INDEX,
  SERIAL_ATTR_STARTED_INDEX,
  SERIAL_ATTR_CLASS_NAME_INDEX,
  SERIAL_ATTR_ATTR_NAME_INDEX,
  SERIAL_ATTR_CACHED_NUM_INDEX,
  SERIAL_ATTR_MAX_INDEX
} SR_ATTRIBUTES;


#define NCACHE_OBJECTS 100

#define NOT_FOUND -1

typedef struct serial_entry SERIAL_CACHE_ENTRY;
struct serial_entry
{
  OID oid;			/* serial object identifier (lock-free hash key) */

  /* latch-free stuff */
  SERIAL_CACHE_ENTRY *stack;	/* used in freelist */
  SERIAL_CACHE_ENTRY *next;	/* used in hash table */
  pthread_mutex_t mutex;	/* per-entry mutex (guards the value advance) */
  UINT64 del_id;		/* delete transaction ID (for lock free) */

  /* serial object values */
  DB_VALUE cur_val;
  DB_VALUE inc_val;
  DB_VALUE max_val;
  DB_VALUE min_val;
  DB_VALUE cyclic;
  DB_VALUE started;
  int cached_num;

  /* last cached value */
  DB_VALUE last_cached_val;

  // *INDENT-OFF*
  serial_entry ();
  ~serial_entry ();
  // *INDENT-ON*
};

/* _db_serial class OID; loaded once at boot, read-only afterwards. Used as the class OID
 * when X-locking a serial's OID on a cache-miss install. */
static OID db_serial_class_oid = OID_INITIALIZER;

/* Lock-free hash keyed by serial OID; a per-entry mutex guards each value advance so
 * independent serials never contend (same pattern as filter_pred_cache.c). */
/* Buckets are fixed at init (this map never rehashes), so size for the expected
 * distinct-cached-serial population to keep chains short (~64KB at load factor <= 1). */
#define SERIAL_CACHE_HASH_SIZE 8192

// *INDENT-OFF*
using serial_cache_hashmap_type = cubthread::lockfree_hashmap<OID, serial_entry>;
// *INDENT-ON*

static serial_cache_hashmap_type serial_Cache_hashmap;
static bool serial_Cache_initialized = false;

/* lock-free entry descriptor callbacks */
static void *serial_cache_entry_alloc (void);
static int serial_cache_entry_free (void *entry);
static int serial_cache_entry_init (void *entry);
static int serial_cache_entry_uninit (void *entry);
static int serial_cache_entry_key_copy (void *src, void *dest);
static int serial_cache_entry_key_compare (void *key1, void *key2);
static unsigned int serial_cache_entry_key_hash (void *key, int htsize);

static LF_ENTRY_DESCRIPTOR serial_Cache_entry_descriptor = {
  offsetof (SERIAL_CACHE_ENTRY, stack),
  offsetof (SERIAL_CACHE_ENTRY, next),
  offsetof (SERIAL_CACHE_ENTRY, del_id),
  offsetof (SERIAL_CACHE_ENTRY, oid),
  offsetof (SERIAL_CACHE_ENTRY, mutex),
  LF_EM_USING_MUTEX,
  LF_ENTRY_DESCRIPTOR_MAX_ALLOC,
  serial_cache_entry_alloc,
  serial_cache_entry_free,
  serial_cache_entry_init,
  serial_cache_entry_uninit,
  serial_cache_entry_key_copy,
  serial_cache_entry_key_compare,
  serial_cache_entry_key_hash,
  NULL				/* duplicates not accepted */
};

#if defined (SERVER_MODE)
BTID serial_Cached_btid = BTID_INITIALIZER;
#endif /* SERVER_MODE */

/* _db_serial attribute layout; loaded from the catalog once at boot (serial_initialize_cache_pool)
 * and read-only afterwards, so value-generation paths read it without any lock. */
ATTR_ID serial_Attrs_id[SERIAL_ATTR_MAX_INDEX];
int serial_Num_attrs = -1;

static int xserial_get_current_value_internal (THREAD_ENTRY * thread_p, DB_VALUE * result_num, const OID * serial_oidp);
static int xserial_get_next_value_internal (THREAD_ENTRY * thread_p, DB_VALUE * result_num, const OID * serial_oidp,
					    int num_alloc, SERIAL_CACHE_ENTRY * claimed_entry);
static int serial_get_next_cached_value (THREAD_ENTRY * thread_p, SERIAL_CACHE_ENTRY * entry, int num_alloc);
static int serial_store_cur_val_of_serial (THREAD_ENTRY * thread_p, SERIAL_CACHE_ENTRY * entry, DB_VALUE * store_val,
					   int num_alloc, bool log_supplemental);
static int serial_update_cur_val_of_serial (THREAD_ENTRY * thread_p, SERIAL_CACHE_ENTRY * entry, int num_alloc);
static int serial_flush_cur_val_of_serial (THREAD_ENTRY * thread_p, SERIAL_CACHE_ENTRY * entry);
static void serial_flush_entry_best_effort (THREAD_ENTRY * thread_p, SERIAL_CACHE_ENTRY * entry);
static int serial_update_serial_object (THREAD_ENTRY * thread_p, PAGE_PTR pgptr, RECDES * recdesc,
					HEAP_CACHE_ATTRINFO * attr_info, const OID * serial_class_oidp,
					const OID * serial_oidp, DB_VALUE * key_val);
static int serial_get_nth_value_internal (DB_VALUE * inc_val, DB_VALUE * cur_val, DB_VALUE * min_val,
					  DB_VALUE * max_val, DB_VALUE * cyclic, int nth, DB_VALUE * result_val,
					  bool clamp_block);
static int serial_get_nth_value (DB_VALUE * inc_val, DB_VALUE * cur_val, DB_VALUE * min_val, DB_VALUE * max_val,
				 DB_VALUE * cyclic, int nth, DB_VALUE * result_val);
static int serial_reserve_block_end (DB_VALUE * inc_val, DB_VALUE * cur_val, DB_VALUE * min_val, DB_VALUE * max_val,
				     DB_VALUE * cyclic, int nth, DB_VALUE * result_val);
static void serial_set_cache_entry (SERIAL_CACHE_ENTRY * entry, DB_VALUE * inc_val, DB_VALUE * cur_val,
				    DB_VALUE * min_val, DB_VALUE * max_val, DB_VALUE * started, DB_VALUE * cyclic,
				    DB_VALUE * last_val, int cached_num);
static void serial_clear_value (SERIAL_CACHE_ENTRY * entry);
static int serial_load_attribute_info_of_db_serial (THREAD_ENTRY * thread_p);
// *INDENT-OFF*
static int serial_get_attrid (THREAD_ENTRY * thread_p, int attr_index, ATTR_ID &attrid);
// *INDENT-ON*

/*
 * xserial_get_current_value () -
 *   return: NO_ERROR, or ER_code
 *   result_num(out)    :
 *   oid_p(in)    :
 *   cached_num(in)    :
 */
int
xserial_get_current_value (THREAD_ENTRY * thread_p, DB_VALUE * result_num, const OID * oid_p, int cached_num)
{
  int ret = NO_ERROR;
  SERIAL_CACHE_ENTRY *entry;

  assert (oid_p != NULL);
  assert (result_num != NULL);

  if (cached_num <= 1)
    {
      /* not used serial cache */
      ret = xserial_get_current_value_internal (thread_p, result_num, oid_p);
    }
  else
    {
      /* used serial cache: lock-free lookup, per-entry mutex for the value read */
      OID key = *oid_p;
      entry = serial_Cache_hashmap.find (thread_p, key);
      if (entry != NULL)
	{
	  pr_clone_value (&entry->cur_val, result_num);
	  pthread_mutex_unlock (&entry->mutex);
	}
      else
	{
	  ret = xserial_get_current_value_internal (thread_p, result_num, oid_p);
	}
    }

  return ret;
}

/*
 * xserial_get_cached_num () - read a serial's cache block size (cached_num) from its _db_serial row.
 *   return: NO_ERROR, or ER_code
 *   cached_num(out)  : the serial's cached_num (0 when the serial has no cached_num column)
 *   serial_oidp(in)  : OID of the _db_serial row
 *
 * The AUTO_INCREMENT insert path caches this on the attribute representation so it can pass the
 * serial's real cached_num to xserial_get_next_value without a per-row catalog lookup.
 */
int
xserial_get_cached_num (THREAD_ENTRY * thread_p, int *cached_num, const OID * serial_oidp)
{
  int ret = NO_ERROR;
  HEAP_SCANCACHE scan_cache;
  SCAN_CODE scan;
  RECDES recdesc = RECDES_INITIALIZER;
  HEAP_CACHE_ATTRINFO attr_info, *attr_info_p = NULL;
  ATTR_ID attrid;
  DB_VALUE *val;
  OID serial_class_oid;

  *cached_num = 0;

  oid_get_serial_oid (&serial_class_oid);
  heap_scancache_quick_start_with_class_oid (thread_p, &scan_cache, &serial_class_oid);

  /* get record into record desc */
  scan = heap_get_visible_version (thread_p, serial_oidp, &serial_class_oid, &recdesc, &scan_cache, PEEK, NULL_CHN);
  if (scan != S_SUCCESS)
    {
      if (er_errid () == ER_PB_BAD_PAGEID)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HEAP_UNKNOWN_OBJECT, 3, serial_oidp->volid, serial_oidp->pageid,
		  serial_oidp->slotid);
	}
      else
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_CANNOT_FETCH_SERIAL, 0);
	}
      goto exit_on_error;
    }

  /* retrieve the cached_num attribute */
  if (serial_get_attrid (thread_p, SERIAL_ATTR_CACHED_NUM_INDEX, attrid) != NO_ERROR)
    {
      goto exit_on_error;
    }
  if (attrid != NOT_FOUND)
    {
      ret = heap_attrinfo_start (thread_p, &serial_class_oid, 1, &attrid, &attr_info);
      if (ret != NO_ERROR)
	{
	  goto exit_on_error;
	}
      attr_info_p = &attr_info;

      ret = heap_attrinfo_read_dbvalues (thread_p, serial_oidp, &recdesc, attr_info_p);
      if (ret != NO_ERROR)
	{
	  goto exit_on_error;
	}

      val = heap_attrinfo_access (attrid, attr_info_p);
      *cached_num = db_get_int (val);

      heap_attrinfo_end (thread_p, attr_info_p);
      attr_info_p = NULL;
    }

  heap_scancache_end (thread_p, &scan_cache);

  return NO_ERROR;

exit_on_error:

  if (attr_info_p != NULL)
    {
      heap_attrinfo_end (thread_p, attr_info_p);
    }

  heap_scancache_end (thread_p, &scan_cache);

  ret = (ret == NO_ERROR && (ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;
  return ret;
}

/*
 * xserial_get_current_value_internal () -
 *   return: NO_ERROR, or ER_code
 *   result_num(out)    :
 *   serial_oidp(in)    :
 */
static int
xserial_get_current_value_internal (THREAD_ENTRY * thread_p, DB_VALUE * result_num, const OID * serial_oidp)
{
  int ret = NO_ERROR;
  HEAP_SCANCACHE scan_cache;
  SCAN_CODE scan;
  RECDES recdesc = RECDES_INITIALIZER;
  HEAP_CACHE_ATTRINFO attr_info, *attr_info_p = NULL;
  ATTR_ID attrid;
  DB_VALUE *cur_val;
  OID serial_class_oid;

  oid_get_serial_oid (&serial_class_oid);
  heap_scancache_quick_start_with_class_oid (thread_p, &scan_cache, &serial_class_oid);

  /* get record into record desc */
  scan = heap_get_visible_version (thread_p, serial_oidp, &serial_class_oid, &recdesc, &scan_cache, PEEK, NULL_CHN);
  if (scan != S_SUCCESS)
    {
      if (er_errid () == ER_PB_BAD_PAGEID)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HEAP_UNKNOWN_OBJECT, 3, serial_oidp->volid, serial_oidp->pageid,
		  serial_oidp->slotid);
	}
      else
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_CANNOT_FETCH_SERIAL, 0);
	}
      goto exit_on_error;
    }

  /* retrieve attribute */
  if (serial_get_attrid (thread_p, SERIAL_ATTR_CURRENT_VAL_INDEX, attrid) != NO_ERROR)
    {
      goto exit_on_error;
    }
  assert (attrid != NOT_FOUND);
  ret = heap_attrinfo_start (thread_p, oid_Serial_class_oid, 1, &attrid, &attr_info);
  if (ret != NO_ERROR)
    {
      goto exit_on_error;
    }

  attr_info_p = &attr_info;

  ret = heap_attrinfo_read_dbvalues (thread_p, serial_oidp, &recdesc, attr_info_p);
  if (ret != NO_ERROR)
    {
      goto exit_on_error;
    }

  cur_val = heap_attrinfo_access (attrid, attr_info_p);

  pr_share_value (cur_val, result_num);

  heap_attrinfo_end (thread_p, attr_info_p);

  heap_scancache_end (thread_p, &scan_cache);

  return NO_ERROR;

exit_on_error:

  if (attr_info_p != NULL)
    {
      heap_attrinfo_end (thread_p, attr_info_p);
    }

  heap_scancache_end (thread_p, &scan_cache);

  ret = (ret == NO_ERROR && (ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;
  return ret;
}

/*
 * xserial_get_next_value () -
 *   return: NO_ERROR, or ER_status
 *   result_num(out)     :
 *   oid_p(in)    :
 *   cached_num(in)    :
 *   num_alloc(in)    :
 *   is_auto_increment(in)    :
 *   force_set_last_insert_id(in):
 */
int
xserial_get_next_value (THREAD_ENTRY * thread_p, DB_VALUE * result_num, const OID * oid_p, int cached_num,
			int num_alloc, int is_auto_increment, bool force_set_last_insert_id)
{
  int ret = NO_ERROR;
  SERIAL_CACHE_ENTRY *entry;

  assert (oid_p != NULL);
  assert (result_num != NULL);

  CHECK_MODIFICATION_NO_RETURN (thread_p, ret);
  if (ret != NO_ERROR)
    {
      return ret;
    }

  if (num_alloc < 1)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_PARAMETER, 0);
      return ER_FAILED;
    }

  if (cached_num <= 1)
    {
      /* not used serial cache */
      ret = xserial_get_next_value_internal (thread_p, result_num, oid_p, num_alloc, NULL);
    }
  else
    {
      /* used serial cache: lock-free lookup, per-entry mutex for the value advance */
      OID key = *oid_p;
      entry = serial_Cache_hashmap.find (thread_p, key);
      if (entry != NULL)
	{
	  ret = serial_get_next_cached_value (thread_p, entry, num_alloc);
	  if (ret == NO_ERROR)
	    {
	      pr_clone_value (&entry->cur_val, result_num);
	    }
	  pthread_mutex_unlock (&entry->mutex);
	}
      else
	{
	  /* Cache miss: find_or_insert claims the entry (locked). The inserter is the initializer and
	   * loads one block; others wait on the entry mutex and reuse it. No OID X_LOCK (so no blocking
	   * lock while a query page is fixed); one block reserved (so a first-access burst leaves no gap). */
	  bool inserted = serial_Cache_hashmap.find_or_insert (thread_p, key, entry);
	  if (entry == NULL)
	    {
	      assert (er_errid () != NO_ERROR);
	      ret = er_errid ();
	    }
	  else if (inserted)
	    {
	      /* Initializer: on success internal fills+unlocks the entry (or drops it if the on-disk
	       * serial is no longer cached). On failure it leaves it locked, so drop the placeholder. */
	      ret = xserial_get_next_value_internal (thread_p, result_num, oid_p, num_alloc, entry);
	      if (ret != NO_ERROR)
		{
		  if (!serial_Cache_hashmap.erase_locked (thread_p, key, entry) && entry != NULL)
		    {
		      pthread_mutex_unlock (&entry->mutex);
		    }
		}
	    }
	  else
	    {
	      /* Another thread initialized the entry first; reuse it. */
	      ret = serial_get_next_cached_value (thread_p, entry, num_alloc);
	      if (ret == NO_ERROR)
		{
		  pr_clone_value (&entry->cur_val, result_num);
		}
	      pthread_mutex_unlock (&entry->mutex);
	    }
	}
    }

  if (ret == NO_ERROR && is_auto_increment == GENERATE_AUTO_INCREMENT)
    {
      /* we update current insert id for this session here */
      /* Note that we ignore an error during updating current insert id. */
      (void) xsession_set_cur_insert_id (thread_p, result_num, force_set_last_insert_id);
    }

  return ret;
}

/*
 * serial_get_next_cached_value () -
 *   return: NO_ERROR, or ER_status
 *   entry(in/out)    :
 *   num_alloc(in)    :
 */
static int
serial_get_next_cached_value (THREAD_ENTRY * thread_p, SERIAL_CACHE_ENTRY * entry, int num_alloc)
{
  DB_VALUE cmp_result;
  DB_VALUE next_val;
  int error, nturns;
  bool exhausted = false;

  assert (1 <= num_alloc);

  db_make_null (&next_val);

  /* check if cached numbers were already exhausted */
  if (num_alloc == 1)
    {
      error = numeric_db_value_compare (&entry->cur_val, &entry->last_cached_val, &cmp_result);
      if (error != NO_ERROR)
	{
	  return error;
	}
      if (db_get_int (&cmp_result) == 0)
	{
	  /* entry is cached to number of cached_num */
	  exhausted = true;
	}
    }
  else
    {
      error =
	serial_get_nth_value (&entry->inc_val, &entry->cur_val, &entry->min_val, &entry->max_val, &entry->cyclic,
			      num_alloc, &next_val);
      if (error != NO_ERROR)
	{
	  return error;
	}

      error = numeric_db_value_compare (&next_val, &entry->last_cached_val, &cmp_result);
      if (error != NO_ERROR)
	{
	  return error;
	}

      if (db_get_int (&cmp_result) >= 0)
	{
	  exhausted = true;
	}

    }

  /* consumed all cached value */
  if (exhausted == true)
    {
      DB_VALUE new_last_cached_val;

      db_make_null (&new_last_cached_val);

      nturns = CEIL_PTVDIV (num_alloc, entry->cached_num);

      error =
	serial_reserve_block_end (&entry->inc_val, &entry->last_cached_val, &entry->min_val, &entry->max_val,
				  &entry->cyclic, (nturns * entry->cached_num), &new_last_cached_val);

      if (error != NO_ERROR)
	{
	  return error;
	}

      error = numeric_db_value_compare (&new_last_cached_val, &entry->last_cached_val, &cmp_result);
      if (error != NO_ERROR)
	{
	  return error;
	}

      /* A reservation clamped to the range boundary can reserve nothing new; leave the entry and
       * _db_serial alone then, because the value generation below raises the range overflow anyway
       * and a durable write per failing call buys nothing. */
      if (db_get_int (&cmp_result) != 0)
	{
	  pr_clone_value (&new_last_cached_val, &entry->last_cached_val);

	  /* cur_val of _db_serial is updated to last_cached_val of entry */
	  error = serial_update_cur_val_of_serial (thread_p, entry, num_alloc);
	  if (error != NO_ERROR)
	    {
	      return error;
	    }
	}
    }

  /* get next value */
  if (num_alloc == 1)
    {
      error =
	serial_get_nth_value (&entry->inc_val, &entry->cur_val, &entry->min_val, &entry->max_val, &entry->cyclic,
			      num_alloc, &next_val);
      if (error != NO_ERROR)
	{
	  return error;
	}
    }

  pr_clone_value (&next_val, &entry->cur_val);

  return NO_ERROR;
}

/*
 * serial_store_cur_val_of_serial () - write store_val into the cur_val column of the entry's
 *                _db_serial row. Two callers want opposite values there, so the value is a
 *                parameter: see serial_update_cur_val_of_serial () and
 *                serial_flush_cur_val_of_serial ().
 *   return: NO_ERROR, or ER_status
 *   entry(in)            :
 *   store_val(in)        : the value to write into cur_val
 *   num_alloc(in)        : how many values this write hands out, reported to the supplemental
 *                          (CDC) log; ignored when log_supplemental is false
 *   log_supplemental(in) : append the supplemental (CDC) record for this write. The record is a
 *                          "SELECT SERIAL_NEXT_VALUE (name, num_alloc)" statement, so it may only
 *                          be appended by a write that actually hands values out. A write-back
 *                          hands out nothing and must pass false - see
 *                          serial_flush_cur_val_of_serial ().
 */
static int
serial_store_cur_val_of_serial (THREAD_ENTRY * thread_p, SERIAL_CACHE_ENTRY * entry, DB_VALUE * store_val,
				int num_alloc, bool log_supplemental)
{
  int ret = NO_ERROR;
  HEAP_SCANCACHE scan_cache;
  SCAN_CODE scan;
  RECDES recdesc = RECDES_INITIALIZER;
  HEAP_CACHE_ATTRINFO attr_info, *attr_info_p = NULL;
  DB_VALUE *val;
  DB_VALUE key_val;
  ATTR_ID attrid;
  OID serial_class_oid;

  db_make_null (&key_val);

  CHECK_MODIFICATION_NO_RETURN (thread_p, ret);
  if (ret != NO_ERROR)
    {
      return ret;
    }

  oid_get_serial_oid (&serial_class_oid);
  heap_scancache_quick_start_modify_with_class_oid (thread_p, &scan_cache, &serial_class_oid);

  scan = heap_get_visible_version (thread_p, &entry->oid, &serial_class_oid, &recdesc, &scan_cache, PEEK, NULL_CHN);
  if (scan != S_SUCCESS)
    {
      if (er_errid () == ER_PB_BAD_PAGEID)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HEAP_UNKNOWN_OBJECT, 3, entry->oid.volid, entry->oid.pageid,
		  entry->oid.slotid);
	}
      else
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_CANNOT_FETCH_SERIAL, 0);
	}
      goto exit_on_error;
    }

  /* retrieve attribute */
  ret = heap_attrinfo_start (thread_p, oid_Serial_class_oid, -1, NULL, &attr_info);
  if (ret != NO_ERROR)
    {
      goto exit_on_error;
    }

  ret = heap_attrinfo_read_dbvalues (thread_p, &entry->oid, &recdesc, &attr_info);
  if (ret != NO_ERROR)
    {
      heap_attrinfo_end (thread_p, &attr_info);
      goto exit_on_error;
    }

  attr_info_p = &attr_info;

  if (serial_get_attrid (thread_p, SERIAL_ATTR_UNIQUE_NAME_INDEX, attrid) != NO_ERROR)
    {
      goto exit_on_error;
    }
  assert (attrid != NOT_FOUND);
  val = heap_attrinfo_access (attrid, attr_info_p);
  pr_clone_value (val, &key_val);

  if (serial_get_attrid (thread_p, SERIAL_ATTR_CURRENT_VAL_INDEX, attrid) != NO_ERROR)
    {
      goto exit_on_error;
    }
  assert (attrid != NOT_FOUND);
  ret = heap_attrinfo_set (&entry->oid, attrid, store_val, attr_info_p);
  if (ret != NO_ERROR)
    {
      goto exit_on_error;
    }

  ret =
    serial_update_serial_object (thread_p, scan_cache.page_watcher.pgptr, &recdesc, attr_info_p, oid_Serial_class_oid,
				 &entry->oid, &key_val);
  if (ret != NO_ERROR)
    {
      goto exit_on_error;
    }

  if (log_supplemental && prm_get_integer_value (PRM_ID_SUPPLEMENTAL_LOG) > 0 && thread_p->no_supplemental_log == false)
    {
      LOG_TDES *tdes = LOG_FIND_CURRENT_TDES (thread_p);

      if (tdes)
	{
	  if (!tdes->has_supplemental_log)
	    {
	      log_append_supplemental_info (thread_p, LOG_SUPPLEMENT_TRAN_USER, strlen (tdes->client.get_db_user ()),
					    tdes->client.get_db_user ());

	      tdes->has_supplemental_log = true;
	    }
	}

      (void) log_append_supplemental_serial (thread_p, db_get_string (&key_val), num_alloc, NULL, &entry->oid);
    }

  pr_clear_value (&key_val);

  heap_attrinfo_end (thread_p, attr_info_p);

  heap_scancache_end (thread_p, &scan_cache);

  return NO_ERROR;

exit_on_error:

  pr_clear_value (&key_val);

  if (attr_info_p != NULL)
    {
      heap_attrinfo_end (thread_p, attr_info_p);
    }

  heap_scancache_end (thread_p, &scan_cache);

  return (ret == NO_ERROR && (ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;
}

/*
 * serial_update_cur_val_of_serial () - reserve the cache block durably: cur_val of _db_serial is
 *                advanced to last_cached_val of entry, so one write covers the whole block.
 *   return: NO_ERROR, or ER_status
 *   entry(in)    :
 */
static int
serial_update_cur_val_of_serial (THREAD_ENTRY * thread_p, SERIAL_CACHE_ENTRY * entry, int num_alloc)
{
  return serial_store_cur_val_of_serial (thread_p, entry, &entry->last_cached_val, num_alloc, true);
}

/*
 * serial_flush_cur_val_of_serial () - give the unissued tail of the reserved block back before the
 *                cache entry is dropped: cur_val of _db_serial is lowered to the entry's cur_val,
 *                the last value actually handed out.
 *   return: NO_ERROR, or ER_status
 *   entry(in)    :
 *
 * Reserving a block advances the durable cur_val to the end of the block, so whatever drops the
 * entry afterwards - a reset/rebase DDL, a DROP, or server shutdown - would otherwise strand the
 * values between the last one issued and the block end, and the next block would start past them.
 * Writing the real cur_val back first keeps the generator on its lattice; the values are only lost
 * when the entry disappears without running this (a crash).
 *
 * The write is deliberately not part of whatever transaction triggered the drop. A rolled back
 * reset DDL must still leave the lowered cur_val behind, because the values it covers were already
 * issued: value state is non-transactional, generation parameters are not.
 *
 * No supplemental (CDC) record is appended. That record is a "SELECT SERIAL_NEXT_VALUE (name, n)"
 * statement, which advances a consumer's serial by n: it reports the values a write hands out, not
 * the value the write stores. This one hands out nothing, so there is nothing to report - any n
 * would push the consumer past the master, and it would do so in the direction opposite to the
 * write. The values this write-back gives back were never reported as handed out either, so the
 * consumer stays level with the master without it.
 */
static int
serial_flush_cur_val_of_serial (THREAD_ENTRY * thread_p, SERIAL_CACHE_ENTRY * entry)
{
  DB_VALUE durable_val;
  DB_VALUE cmp_result;
  int error;

  if (DB_IS_NULL (&entry->cur_val) || DB_IS_NULL (&entry->last_cached_val))
    {
      /* nothing was issued from this entry yet */
      return NO_ERROR;
    }

  error = numeric_db_value_compare (&entry->cur_val, &entry->last_cached_val, &cmp_result);
  if (error != NO_ERROR)
    {
      return error;
    }

  if (db_get_int (&cmp_result) == 0)
    {
      /* the block was consumed exactly; the durable value already matches */
      return NO_ERROR;
    }

  /* Only hand the tail back while the durable value is still the block end this entry reserved.
   * A reset/rebase DDL writes cur_val itself and owns the value from then on; writing the stale
   * cached one over it would undo the DDL. Such a DDL decaches before it writes, so the flush it
   * triggers still runs - it just runs here, while the block end is what is on disk. */
  db_make_null (&durable_val);
  if (xserial_get_current_value_internal (thread_p, &durable_val, &entry->oid) != NO_ERROR)
    {
      /* the row is gone (dropped serial); nothing to write back */
      er_clear ();
      return NO_ERROR;
    }

  error = numeric_db_value_compare (&durable_val, &entry->last_cached_val, &cmp_result);
  pr_clear_value (&durable_val);
  if (error != NO_ERROR)
    {
      return error;
    }

  if (db_get_int (&cmp_result) != 0)
    {
      return NO_ERROR;
    }

  return serial_store_cur_val_of_serial (thread_p, entry, &entry->cur_val, 0, false);
}

/*
 * serial_flush_entry_best_effort () - write the entry's issued value back, and swallow a failure
 *                explicitly.
 *   return: void
 *   entry(in)    :
 *
 * Both callers drop the entry whatever happens here, and that is deliberate: they run because the
 * serial's definition changed or the object is gone, so keeping a stale entry would let the serial
 * keep serving values from a definition that no longer exists. A failed write-back only leaves the
 * gap this issue removes - bounded by cached_num, and the behavior before the write-back existed -
 * which is the lesser of the two.
 *
 * Clearing the error is the point of this wrapper. The failure is invisible to the caller either
 * way, but a set error area is not: xserial_decache () runs inside whatever DDL triggered it, and
 * er_errid () left behind here surfaces as that statement's failure. Same reason
 * xserial_get_cached_num ()'s fallback clears its own.
 */
static void
serial_flush_entry_best_effort (THREAD_ENTRY * thread_p, SERIAL_CACHE_ENTRY * entry)
{
  if (serial_flush_cur_val_of_serial (thread_p, entry) != NO_ERROR)
    {
      er_log_debug (ARG_FILE_LINE,
		    "serial cache write-back failed for (%d|%d|%d); the unissued tail of its block is lost\n",
		    OID_AS_ARGS (&entry->oid));
      er_clear ();
    }
}

/*
 * xserial_get_next_value_internal () -
 *   return: NO_ERROR, or ER_status
 *   result_num(out)    :
 *   serial_oidp(in)    :
 */
static int
xserial_get_next_value_internal (THREAD_ENTRY * thread_p, DB_VALUE * result_num, const OID * serial_oidp,
				 int num_alloc, SERIAL_CACHE_ENTRY * claimed_entry)
{
  int ret = NO_ERROR;
  HEAP_SCANCACHE scan_cache;
  SCAN_CODE scan;
  RECDES recdesc = RECDES_INITIALIZER;
  HEAP_CACHE_ATTRINFO attr_info, *attr_info_p = NULL;
  DB_VALUE *val = NULL;
  DB_VALUE cur_val;
  DB_VALUE inc_val;
  DB_VALUE max_val;
  DB_VALUE min_val;
  DB_VALUE cyclic;
  DB_VALUE started;
  DB_VALUE next_val;
  DB_VALUE key_val;
  DB_VALUE last_val;
  int cached_num, nturns;
  SERIAL_CACHE_ENTRY *entry = NULL;
  ATTR_ID attrid;
  OID serial_class_oid;

  bool is_started;

  db_make_null (&key_val);

  oid_get_serial_oid (&serial_class_oid);
  heap_scancache_quick_start_modify_with_class_oid (thread_p, &scan_cache, &serial_class_oid);

  scan = heap_get_visible_version (thread_p, serial_oidp, &serial_class_oid, &recdesc, &scan_cache, PEEK, NULL_CHN);
  if (scan != S_SUCCESS)
    {
      if (er_errid () == ER_PB_BAD_PAGEID)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HEAP_UNKNOWN_OBJECT, 3, serial_oidp->volid, serial_oidp->pageid,
		  serial_oidp->slotid);
	}
      else
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_CANNOT_FETCH_SERIAL, 0);
	}
      goto exit_on_error;
    }

  /* retrieve attribute */
  ret = heap_attrinfo_start (thread_p, oid_Serial_class_oid, -1, NULL, &attr_info);
  if (ret != NO_ERROR)
    {
      goto exit_on_error;
    }

  ret = heap_attrinfo_read_dbvalues (thread_p, serial_oidp, &recdesc, &attr_info);

  attr_info_p = &attr_info;

  if (serial_get_attrid (thread_p, SERIAL_ATTR_CACHED_NUM_INDEX, attrid) != NO_ERROR)
    {
      goto exit_on_error;
    }
  if (attrid == NOT_FOUND)
    {
      cached_num = 0;
    }
  else
    {
      val = heap_attrinfo_access (attrid, attr_info_p);
      cached_num = db_get_int (val);
    }

  if (serial_get_attrid (thread_p, SERIAL_ATTR_UNIQUE_NAME_INDEX, attrid) != NO_ERROR)
    {
      goto exit_on_error;
    }
  assert (attrid != NOT_FOUND);
  val = heap_attrinfo_access (attrid, attr_info_p);
  pr_clone_value (val, &key_val);

  if (serial_get_attrid (thread_p, SERIAL_ATTR_CURRENT_VAL_INDEX, attrid) != NO_ERROR)
    {
      goto exit_on_error;
    }
  assert (attrid != NOT_FOUND);
  val = heap_attrinfo_access (attrid, attr_info_p);
  pr_share_value (val, &cur_val);

  if (serial_get_attrid (thread_p, SERIAL_ATTR_INCREMENT_VAL_INDEX, attrid) != NO_ERROR)
    {
      goto exit_on_error;
    }
  assert (attrid != NOT_FOUND);
  val = heap_attrinfo_access (attrid, attr_info_p);
  pr_share_value (val, &inc_val);

  if (serial_get_attrid (thread_p, SERIAL_ATTR_MAX_VAL_INDEX, attrid) != NO_ERROR)
    {
      goto exit_on_error;
    }
  assert (attrid != NOT_FOUND);
  val = heap_attrinfo_access (attrid, attr_info_p);
  pr_share_value (val, &max_val);

  if (serial_get_attrid (thread_p, SERIAL_ATTR_MIN_VAL_INDEX, attrid) != NO_ERROR)
    {
      goto exit_on_error;
    }
  assert (attrid != NOT_FOUND);
  val = heap_attrinfo_access (attrid, attr_info_p);
  pr_share_value (val, &min_val);

  if (serial_get_attrid (thread_p, SERIAL_ATTR_CYCLIC_INDEX, attrid) != NO_ERROR)
    {
      goto exit_on_error;
    }
  assert (attrid != NOT_FOUND);
  val = heap_attrinfo_access (attrid, attr_info_p);
  pr_share_value (val, &cyclic);

  if (serial_get_attrid (thread_p, SERIAL_ATTR_STARTED_INDEX, attrid) != NO_ERROR)
    {
      goto exit_on_error;
    }
  assert (attrid != NOT_FOUND);
  val = heap_attrinfo_access (attrid, attr_info_p);
  pr_share_value (val, &started);

  db_make_null (&last_val);

  is_started = db_get_int (&started);

  if (db_get_int (&started) == 0)
    {
      /* This is the first time to generate the serial value. */
      db_make_int (&started, 1);
      if (serial_get_attrid (thread_p, SERIAL_ATTR_STARTED_INDEX, attrid) != NO_ERROR)
	{
	  goto exit_on_error;
	}
      assert (attrid != NOT_FOUND);
      ret = heap_attrinfo_set (serial_oidp, attrid, &started, attr_info_p);
      if (ret == NO_ERROR)
	{
	  pr_share_value (&cur_val, &next_val);
	  if (cached_num > 1)
	    {
	      assert (1 <= num_alloc);
	      nturns = CEIL_PTVDIV (num_alloc, cached_num);

	      ret =
		serial_reserve_block_end (&inc_val, &cur_val, &min_val, &max_val, &cyclic,
					  (nturns * (cached_num - 1)), &last_val);
	    }

	  num_alloc--;
	}
    }

  if (num_alloc > 0)
    {
      if (cached_num > 1)
	{
	  nturns = CEIL_PTVDIV (num_alloc, cached_num);

	  ret =
	    serial_reserve_block_end (&inc_val, &cur_val, &min_val, &max_val, &cyclic, (nturns * cached_num),
				      &last_val);
	}

      if (ret == NO_ERROR)
	{
	  ret = serial_get_nth_value (&inc_val, &cur_val, &min_val, &max_val, &cyclic, num_alloc, &next_val);
	}
    }

  if (ret != NO_ERROR)
    {
      goto exit_on_error;
    }

  /* Now update record */
  if (serial_get_attrid (thread_p, SERIAL_ATTR_CURRENT_VAL_INDEX, attrid) != NO_ERROR)
    {
      goto exit_on_error;
    }
  assert (attrid != NOT_FOUND);
  if (cached_num > 1 && !DB_IS_NULL (&last_val))
    {
      ret = heap_attrinfo_set (serial_oidp, attrid, &last_val, attr_info_p);
    }
  else
    {
      ret = heap_attrinfo_set (serial_oidp, attrid, &next_val, attr_info_p);
    }
  if (ret != NO_ERROR)
    {
      goto exit_on_error;
    }

  ret =
    serial_update_serial_object (thread_p, scan_cache.page_watcher.pgptr, &recdesc, attr_info_p, oid_Serial_class_oid,
				 serial_oidp, &key_val);
  if (ret != NO_ERROR)
    {
      goto exit_on_error;
    }

  if (prm_get_integer_value (PRM_ID_SUPPLEMENTAL_LOG) > 0 && thread_p->no_supplemental_log == false)
    {
      LOG_TDES *tdes = LOG_FIND_CURRENT_TDES (thread_p);

      if (tdes)
	{
	  if (!tdes->has_supplemental_log)
	    {
	      log_append_supplemental_info (thread_p, LOG_SUPPLEMENT_TRAN_USER, strlen (tdes->client.get_db_user ()),
					    tdes->client.get_db_user ());

	      tdes->has_supplemental_log = true;
	    }
	}

      (void) log_append_supplemental_serial (thread_p, db_get_string (&key_val), is_started ? num_alloc : num_alloc + 1,
					     NULL, serial_oidp);
    }

  /* copy result value */
  pr_share_value (&next_val, result_num);

  pr_clear_value (&key_val);

  heap_attrinfo_end (thread_p, attr_info_p);

  heap_scancache_end (thread_p, &scan_cache);

  if (claimed_entry != NULL)
    {
      /* Initializer's pre-claimed entry (owned here on success; caller cleans up only on error). */
      if (cached_num > 1)
	{
	  /* Fill and release it. */
	  pr_share_value (&next_val, &cur_val);
	  serial_set_cache_entry (claimed_entry, &inc_val, &cur_val, &min_val, &max_val, &started, &cyclic, &last_val,
				  cached_num);
	  pthread_mutex_unlock (&claimed_entry->mutex);
	}
      else
	{
	  /* On-disk serial is no longer cached (e.g. ALTER SERIAL ... NOCACHE after the plan compiled);
	   * drop the placeholder. erase_locked releases the mutex; on the rare mismatch, unlock it. */
	  OID key = *serial_oidp;
	  if (!serial_Cache_hashmap.erase_locked (thread_p, key, claimed_entry) && claimed_entry != NULL)
	    {
	      pthread_mutex_unlock (&claimed_entry->mutex);
	    }
	}
    }
  else if (cached_num > 1)
    {
      /* No pre-claimed entry (cached_num <= 1 plan whose on-disk cached_num was raised by
       * ALTER SERIAL ... CACHE). Install now; never overwrite an entry a concurrent thread won
       * -- that would reset a live cached counter and risk handing out duplicates. */
      OID key = *serial_oidp;
      bool inserted = serial_Cache_hashmap.find_or_insert (thread_p, key, entry);

      if (entry != NULL)
	{
	  if (inserted)
	    {
	      pr_share_value (&next_val, &cur_val);
	      serial_set_cache_entry (entry, &inc_val, &cur_val, &min_val, &max_val, &started, &cyclic, &last_val,
				      cached_num);
	    }
	  pthread_mutex_unlock (&entry->mutex);
	}
    }

  return NO_ERROR;

exit_on_error:

  pr_clear_value (&key_val);

  if (attr_info_p != NULL)
    {
      heap_attrinfo_end (thread_p, attr_info_p);
    }

  heap_scancache_end (thread_p, &scan_cache);

  return (ret == NO_ERROR && (ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;
}

/*
 * serial_update_serial_object () -
 *   return: NO_ERROR or error status
 *   pgptr(in)         :
 *   recdesc(in)       :
 *   attr_info(in)     :
 *   serial_class_oidp(in)   :
 *   serial_oidp(in)   :
 *   key_val(in)       :
 */
static int
serial_update_serial_object (THREAD_ENTRY * thread_p, PAGE_PTR pgptr, RECDES * recdesc, HEAP_CACHE_ATTRINFO * attr_info,
			     const OID * serial_class_oidp, const OID * serial_oidp, DB_VALUE * key_val)
{
  // *INDENT-OFF*
  cubmem::stack_block<IO_MAX_PAGE_SIZE> copyarea;
  // *INDENT-ON*
  record_descriptor new_recdesc;
  SCAN_CODE scan;
  LOG_DATA_ADDR addr;
  int ret;
  int sp_success;
  int tran_index;
  LOCK lock_mode = NULL_LOCK;

  assert_release (serial_class_oidp != NULL && !OID_ISNULL (serial_class_oidp));

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  lock_mode = lock_get_object_lock (serial_oidp, serial_class_oidp);

  /* need to start topop for replication Replication will recognize and realize a special type of update for serial by
   * this top operation log record. If lock_mode is X_LOCK means we created or altered the serial obj in an
   * uncommitted trans For this case, topop and flush mark are not used, since these may cause problem with replication
   * log. */
  if (lock_mode != X_LOCK)
    {
      log_sysop_start (thread_p);
    }

  if (lock_mode != X_LOCK && !LOG_CHECK_LOG_APPLIER (thread_p) && log_does_allow_replication () == true)
    {
      repl_start_flush_mark (thread_p);
    }

  new_recdesc.set_external_buffer (copyarea);

  scan = heap_attrinfo_transform_to_disk (thread_p, attr_info, recdesc, &new_recdesc);
  if (scan != S_SUCCESS)
    {
      assert (false);
      goto exit_on_error;
    }

  /* Log the changes */
  new_recdesc.set_type (recdesc->type);
  addr.offset = serial_oidp->slotid;
  addr.pgptr = pgptr;

  assert (spage_is_updatable (thread_p, addr.pgptr, serial_oidp->slotid, (int) new_recdesc.get_size ()));

  log_append_redo_recdes (thread_p, RVHF_UPDATE, &addr, &new_recdesc.get_recdes ());

  /* Now really update */
  sp_success = spage_update (thread_p, addr.pgptr, serial_oidp->slotid, &new_recdesc.get_recdes ());
  if (sp_success != SP_SUCCESS)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_CANNOT_UPDATE_SERIAL, 0);
      goto exit_on_error;
    }

  /* make replication log for the special type of update for serial */
  if (!LOG_CHECK_LOG_APPLIER (thread_p) && log_does_allow_replication () == true)
    {
      repl_log_insert (thread_p, serial_class_oidp, serial_oidp, LOG_REPLICATION_DATA, RVREPL_DATA_UPDATE, key_val,
		       REPL_INFO_TYPE_RBR_NORMAL);
      repl_add_update_lsa (thread_p, serial_oidp);

      if (lock_mode != X_LOCK)
	{
	  repl_end_flush_mark (thread_p, false);
	}
    }

  if (lock_mode != X_LOCK)
    {
      log_sysop_commit (thread_p);
    }

  return NO_ERROR;

exit_on_error:

  if (lock_mode != X_LOCK)
    {
      log_sysop_abort (thread_p);
    }

  ASSERT_ERROR_AND_SET (ret);
  return ret;
}

/*
 * serial_get_nth_value () - get Nth next_value, for handing the value out
 *   return: NO_ERROR, or ER_status
 *   inc_val(in)        :
 *   cur_val(in)        :
 *   min_val(in)        :
 *   max_val(in)        :
 *   cyclic(in)         :
 *   nth(in)            :
 *   result_val(out)    :
 *
 * A value past max_val (min_val for a negative increment) on a non-cyclic serial is out of
 * range, so this raises ER_QPROC_SERIAL_RANGE_OVERFLOW.
 */
static int
serial_get_nth_value (DB_VALUE * inc_val, DB_VALUE * cur_val, DB_VALUE * min_val, DB_VALUE * max_val, DB_VALUE * cyclic,
		      int nth, DB_VALUE * result_val)
{
  return serial_get_nth_value_internal (inc_val, cur_val, min_val, max_val, cyclic, nth, result_val, false);
}

/*
 * serial_reserve_block_end () - get Nth next_value, for reserving a cache block up to it
 *   return: NO_ERROR, or ER_status
 *   inc_val(in)        :
 *   cur_val(in)        :
 *   min_val(in)        :
 *   max_val(in)        :
 *   cyclic(in)         :
 *   nth(in)            :
 *   result_val(out)    :
 *
 * A block end past max_val (min_val for a negative increment) on a non-cyclic serial is
 * clamped to that boundary, so the values up to it stay reservable instead of the whole
 * block failing. The value generation still raises the overflow for a value past it.
 */
static int
serial_reserve_block_end (DB_VALUE * inc_val, DB_VALUE * cur_val, DB_VALUE * min_val, DB_VALUE * max_val,
			  DB_VALUE * cyclic, int nth, DB_VALUE * result_val)
{
  return serial_get_nth_value_internal (inc_val, cur_val, min_val, max_val, cyclic, nth, result_val, true);
}

/*
 * serial_get_nth_value_internal () - get Nth next_value
 *   return: NO_ERROR, or ER_status
 *   inc_val(in)        :
 *   cur_val(in)        :
 *   min_val(in)        :
 *   max_val(in)        :
 *   cyclic(in)         :
 *   nth(in)            :
 *   result_val(out)    :
 *   clamp_block(in)    : what to do when the Nth value leaves the range -- see the two
 *                        callers above, which is what a caller should use
 */
static int
serial_get_nth_value_internal (DB_VALUE * inc_val, DB_VALUE * cur_val, DB_VALUE * min_val, DB_VALUE * max_val,
			       DB_VALUE * cyclic, int nth, DB_VALUE * result_val, bool clamp_block)
{
  DB_VALUE tmp_val, cmp_result, add_val;
  unsigned char num[DB_NUMERIC_BUF_SIZE];
  int inc_val_flag;
  int ret;

  inc_val_flag = numeric_db_value_is_positive (inc_val);
  if (inc_val_flag < 0)
    {
      return ER_FAILED;
    }

  /* Now calculate next value */
  if (nth > 1)
    {
      numeric_coerce_int_to_num (nth, num, NULL);
      db_make_numeric (&tmp_val, num, DB_MAX_FIXED_NUMERIC_PRECISION, 0, DB_NUMERIC_BUF_SIZE, false, false);
      numeric_db_value_mul (inc_val, &tmp_val, &add_val);
      FLOAT_TO_FIXED_NUMERIC (&add_val);
    }
  else
    {
      pr_share_value (inc_val, &add_val);
    }

  /* inc_val_flag (1 or 0) */
  if (inc_val_flag > 0)
    {
      ret = numeric_db_value_sub (max_val, &add_val, &tmp_val);
      if (ret != NO_ERROR)
	{
	  return ret;
	}
      FLOAT_TO_FIXED_NUMERIC (&tmp_val);
      ret = numeric_db_value_compare (cur_val, &tmp_val, &cmp_result);
      if (ret != NO_ERROR)
	{
	  return ret;
	}

      /* cur_val + inc_val * cached_num > max_val */
      if (db_get_int (&cmp_result) > 0)
	{
	  if (db_get_int (cyclic))
	    {
	      pr_share_value (min_val, result_val);
	    }
	  else if (clamp_block)
	    {
	      /* Reserving a cache block would overshoot max_val; clamp it to the boundary so
	       * values up through max_val stay usable instead of failing early. */
	      pr_share_value (max_val, result_val);
	    }
	  else
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_SERIAL_RANGE_OVERFLOW, 0);
	      return ER_QPROC_SERIAL_RANGE_OVERFLOW;
	    }
	}
      else
	{
	  (void) numeric_db_value_add (cur_val, &add_val, result_val);
	  FLOAT_TO_FIXED_NUMERIC (result_val);
	}
    }
  else
    {
      ret = numeric_db_value_sub (min_val, &add_val, &tmp_val);
      if (ret != NO_ERROR)
	{
	  return ret;
	}
      FLOAT_TO_FIXED_NUMERIC (&tmp_val);
      ret = numeric_db_value_compare (cur_val, &tmp_val, &cmp_result);
      if (ret != NO_ERROR)
	{
	  return ret;
	}

      /* cur_val + inc_val * cached_num < min_val */
      if (db_get_int (&cmp_result) < 0)
	{
	  if (db_get_int (cyclic))
	    {
	      pr_share_value (max_val, result_val);
	    }
	  else if (clamp_block)
	    {
	      /* Reserving a cache block would undershoot min_val; clamp it to the boundary so
	       * values down through min_val stay usable instead of failing early. */
	      pr_share_value (min_val, result_val);
	    }
	  else
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_SERIAL_RANGE_OVERFLOW, 0);
	      return ER_QPROC_SERIAL_RANGE_OVERFLOW;
	    }
	}
      else
	{
	  (void) numeric_db_value_add (cur_val, &add_val, result_val);
	  FLOAT_TO_FIXED_NUMERIC (result_val);
	}
    }

  return NO_ERROR;
}

/*
 * serial_initialize_cache_pool () -
 *   return: NO_ERROR, or ER_status
 *   load_attr_info(in): load the _db_serial attribute layout now. Pass false only when restarting
 *                       from a backup, which has no client workspace.
 */
int
serial_initialize_cache_pool (THREAD_ENTRY * thread_p, bool load_attr_info)
{
  unsigned int i;
  const int freelist_block_count = 2;
  const int freelist_block_size = NCACHE_OBJECTS / freelist_block_count;

  if (serial_Cache_initialized)
    {
      serial_finalize_cache_pool ();
    }

  if (!load_attr_info)
    {
      /* Without a client workspace the load swizzles the object-domain owner OID and divides by
       * the empty MOP table in ws_mop (SIGFPE). restoredb/restoreslave generate no serial values,
       * so the cache pool is not built either; a later normal boot initializes everything. */
      return NO_ERROR;
    }

  serial_Cache_hashmap.init (serial_Cache_Ts, THREAD_TS_SERIAL_CACHE, SERIAL_CACHE_HASH_SIZE, freelist_block_size,
			     freelist_block_count, serial_Cache_entry_descriptor);
  serial_Cache_initialized = true;

  for (i = 0; i < sizeof (serial_Attrs_id) / sizeof (ATTR_ID); i++)
    {
      serial_Attrs_id[i] = -1;
    }

  /* Load the _db_serial attribute layout once, now that the catalog is available. */
  return serial_load_attribute_info_of_db_serial (thread_p);
}

/*
 * serial_flush_cache_pool () - write every cached serial's issued value back to _db_serial.
 *   return:
 *
 * Called during shutdown while the heap, log and buffer managers are still up (unlike
 * serial_finalize_cache_pool, which runs after the volumes are dismounted). Without this a clean
 * restart would resume past the end of each reserved block rather than at the last value issued,
 * which is what makes every standalone (csql -S) process consume a whole block.
 *
 * The caller must be on a transaction that may run system operations - each write opens one. The
 * system main transaction the rest of shutdown uses is not one of them; see xboot_shutdown_server.
 */
void
serial_flush_cache_pool (THREAD_ENTRY * thread_p)
{
  if (!serial_Cache_initialized)
    {
      return;
    }

  // *INDENT-OFF*
  serial_cache_hashmap_type::iterator it { thread_p, serial_Cache_hashmap };
  // *INDENT-ON*

  for (SERIAL_CACHE_ENTRY * entry = it.iterate (); entry != NULL; entry = it.iterate ())
    {
      serial_flush_entry_best_effort (thread_p, entry);
    }
}

/*
 * serial_finalize_cache_pool () -
 *   return:
 */
void
serial_finalize_cache_pool (void)
{
  if (serial_Cache_initialized)
    {
      serial_Cache_hashmap.destroy ();
      serial_Cache_initialized = false;
    }

  serial_Num_attrs = -1;
  OID_SET_NULL (&db_serial_class_oid);
}

/*
 * serial_get_attrid () -
 *   return: attribute id or NOT_FOUND
 *
 *   attr_index(in) :
 */
// *INDENT-OFF*
static int
serial_get_attrid (THREAD_ENTRY * thread_p, int attr_index, ATTR_ID &attrid)
{
  attrid = NOT_FOUND;

  /* serial_Attrs_id[] is read-only after boot, so no lock is needed here (see its declaration). */
  assert (serial_Num_attrs >= 0);
  if (attr_index >= 0 && attr_index <= serial_Num_attrs)
    {
      attrid = serial_Attrs_id[attr_index];
    }

  return NO_ERROR;
}
// *INDENT-ON*

/*
 * serial_load_attribute_info_of_db_serial () -
 *   return: NO_ERROR, or ER_status
 */
static int
serial_load_attribute_info_of_db_serial (THREAD_ENTRY * thread_p)
{
  HEAP_SCANCACHE scan;
  RECDES class_record;
  HEAP_CACHE_ATTRINFO attr_info;
  int i, error = NO_ERROR;
  char *attr_name_p, *string = NULL;
  int alloced_string = 0;

  serial_Num_attrs = -1;

  oid_get_serial_oid (&db_serial_class_oid);

  if (heap_scancache_quick_start_with_class_oid (thread_p, &scan, &db_serial_class_oid) != NO_ERROR)
    {
      return ER_FAILED;
    }
  if (heap_get_class_record (thread_p, &db_serial_class_oid, &class_record, &scan, PEEK) != S_SUCCESS)
    {
      heap_scancache_end (thread_p, &scan);
      return ER_FAILED;
    }

  error = heap_attrinfo_start (thread_p, &db_serial_class_oid, -1, NULL, &attr_info);
  if (error != NO_ERROR)
    {
      (void) heap_scancache_end (thread_p, &scan);
      return error;
    }

  for (i = 0; i < attr_info.num_values; i++)
    {
      string = NULL;
      alloced_string = 0;

      error = or_get_attrname (&class_record, i, &string, &alloced_string);
      if (error != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto exit_on_error;
	}

      attr_name_p = string;
      if (attr_name_p == NULL)
	{
	  error = ER_FAILED;
	  goto exit_on_error;
	}

      if (strcmp (attr_name_p, SERIAL_ATTR_UNIQUE_NAME) == 0)
	{
	  serial_Attrs_id[SERIAL_ATTR_UNIQUE_NAME_INDEX] = i;
	}
      else if (strcmp (attr_name_p, SERIAL_ATTR_NAME) == 0)
	{
	  serial_Attrs_id[SERIAL_ATTR_NAME_INDEX] = i;
	}
      else if (strcmp (attr_name_p, SERIAL_ATTR_OWNER) == 0)
	{
	  serial_Attrs_id[SERIAL_ATTR_OWNER_INDEX] = i;
	}
      else if (strcmp (attr_name_p, SERIAL_ATTR_CURRENT_VAL) == 0)
	{
	  serial_Attrs_id[SERIAL_ATTR_CURRENT_VAL_INDEX] = i;
	}
      else if (strcmp (attr_name_p, SERIAL_ATTR_INCREMENT_VAL) == 0)
	{
	  serial_Attrs_id[SERIAL_ATTR_INCREMENT_VAL_INDEX] = i;
	}
      else if (strcmp (attr_name_p, SERIAL_ATTR_MAX_VAL) == 0)
	{
	  serial_Attrs_id[SERIAL_ATTR_MAX_VAL_INDEX] = i;
	}
      else if (strcmp (attr_name_p, SERIAL_ATTR_MIN_VAL) == 0)
	{
	  serial_Attrs_id[SERIAL_ATTR_MIN_VAL_INDEX] = i;
	}
      else if (strcmp (attr_name_p, SERIAL_ATTR_CYCLIC) == 0)
	{
	  serial_Attrs_id[SERIAL_ATTR_CYCLIC_INDEX] = i;
	}
      else if (strcmp (attr_name_p, SERIAL_ATTR_STARTED) == 0)
	{
	  serial_Attrs_id[SERIAL_ATTR_STARTED_INDEX] = i;
	}
      else if (strcmp (attr_name_p, SERIAL_ATTR_CLASS_NAME) == 0)
	{
	  serial_Attrs_id[SERIAL_ATTR_CLASS_NAME_INDEX] = i;
	}
      else if (strcmp (attr_name_p, SERIAL_ATTR_ATTR_NAME) == 0)
	{
	  serial_Attrs_id[SERIAL_ATTR_ATTR_NAME_INDEX] = i;
	}
      else if (strcmp (attr_name_p, SERIAL_ATTR_CACHED_NUM) == 0)
	{
	  serial_Attrs_id[SERIAL_ATTR_CACHED_NUM_INDEX] = i;
	}

      if (string != NULL && alloced_string)
	{
	  db_private_free_and_init (NULL, string);
	}
    }

  serial_Num_attrs = attr_info.num_values;

  heap_attrinfo_end (thread_p, &attr_info);
  error = heap_scancache_end (thread_p, &scan);

  return error;

exit_on_error:

  heap_attrinfo_end (thread_p, &attr_info);
  (void) heap_scancache_end (thread_p, &scan);

  return error;
}

/*
 * serial_set_cache_entry () -
 *   return:
 *   entry(out)   :
 *   inc_val(in) :
 *   cur_val(in) :
 *   min_val(in) :
 *   max_val(in) :
 *   started(in) :
 *   cyclic(in)  :
 *   last_val(in):
 *   cached_num(in):
 */
static void
serial_set_cache_entry (SERIAL_CACHE_ENTRY * entry, DB_VALUE * inc_val, DB_VALUE * cur_val, DB_VALUE * min_val,
			DB_VALUE * max_val, DB_VALUE * started, DB_VALUE * cyclic, DB_VALUE * last_val, int cached_num)
{
  pr_clone_value (cur_val, &entry->cur_val);
  pr_clone_value (inc_val, &entry->inc_val);
  pr_clone_value (max_val, &entry->max_val);
  pr_clone_value (min_val, &entry->min_val);
  pr_clone_value (cyclic, &entry->cyclic);
  pr_clone_value (last_val, &entry->last_cached_val);
  pr_clone_value (started, &entry->started);
  entry->cached_num = cached_num;
}

/*
 * serial_clear_value () - clear all value of cache entry
 * return:
 * entry(in/out) :
 */
static void
serial_clear_value (SERIAL_CACHE_ENTRY * entry)
{
  pr_clear_value (&entry->cur_val);
  pr_clear_value (&entry->inc_val);
  pr_clear_value (&entry->max_val);
  pr_clear_value (&entry->min_val);
  pr_clear_value (&entry->cyclic);
  pr_clear_value (&entry->started);
  entry->cached_num = 0;
}

// *INDENT-OFF*
serial_entry::serial_entry ()
{
  pthread_mutex_init (&mutex, NULL);
}

serial_entry::~serial_entry ()
{
  pthread_mutex_destroy (&mutex);
}
// *INDENT-ON*

/*
 * serial_cache_entry_alloc () - allocate a serial cache entry.
 */
static void *
serial_cache_entry_alloc (void)
{
  SERIAL_CACHE_ENTRY *entry = (SERIAL_CACHE_ENTRY *) malloc (sizeof (SERIAL_CACHE_ENTRY));
  if (entry == NULL)
    {
      return NULL;
    }
  pthread_mutex_init (&entry->mutex, NULL);
  return entry;
}

/*
 * serial_cache_entry_free () - free a serial cache entry.
 */
static int
serial_cache_entry_free (void *entry)
{
  pthread_mutex_destroy (&((SERIAL_CACHE_ENTRY *) entry)->mutex);
  free (entry);
  return NO_ERROR;
}

/*
 * serial_cache_entry_init () - initialize a freshly claimed serial cache entry.
 */
static int
serial_cache_entry_init (void *entry)
{
  SERIAL_CACHE_ENTRY *ent = (SERIAL_CACHE_ENTRY *) entry;

  db_make_null (&ent->cur_val);
  db_make_null (&ent->inc_val);
  db_make_null (&ent->max_val);
  db_make_null (&ent->min_val);
  db_make_null (&ent->cyclic);
  db_make_null (&ent->started);
  db_make_null (&ent->last_cached_val);
  ent->cached_num = 0;
  return NO_ERROR;
}

/*
 * serial_cache_entry_uninit () - clear a retired serial cache entry's values.
 */
static int
serial_cache_entry_uninit (void *entry)
{
  SERIAL_CACHE_ENTRY *ent = (SERIAL_CACHE_ENTRY *) entry;

  serial_clear_value (ent);
  pr_clear_value (&ent->last_cached_val);
  return NO_ERROR;
}

/*
 * serial_cache_entry_key_copy () - copy a serial OID key into an entry.
 */
static int
serial_cache_entry_key_copy (void *src, void *dest)
{
  COPY_OID ((OID *) dest, (OID *) src);
  return NO_ERROR;
}

/*
 * serial_cache_entry_key_compare () - compare two serial OID keys; 0 if equal.
 */
static int
serial_cache_entry_key_compare (void *key1, void *key2)
{
  return oid_compare (key1, key2);
}

/*
 * serial_cache_entry_key_hash () - hash a serial OID key.
 */
static unsigned int
serial_cache_entry_key_hash (void *key, int htsize)
{
  return oid_hash (key, (unsigned int) htsize);
}

/*
 * xserial_decache () - decache cache entry of cache pool
 * return:
 * oidp(in) :
 */
void
xserial_decache (THREAD_ENTRY * thread_p, OID * oidp)
{
  xcache_remove_by_oid (thread_p, oidp);

  if (serial_Cache_initialized)
    {
      OID key = *oidp;
      SERIAL_CACHE_ENTRY *entry = serial_Cache_hashmap.find (thread_p, key);

      if (entry != NULL)
	{
	  /* Hand the unissued tail of the reserved block back before losing the entry, so the next
	   * block resumes at the last value actually issued instead of past the block end. */
	  serial_flush_entry_best_effort (thread_p, entry);

	  if (!serial_Cache_hashmap.erase_locked (thread_p, key, entry) && entry != NULL)
	    {
	      pthread_mutex_unlock (&entry->mutex);
	    }
	}
    }
}

#if defined (SERVER_MODE)
/*
 * serial_cache_index_btid () - Cache serial index BTID.
 *
 * return	 : Error Code.
 * thread_p (in) : Thread entry.
 *
 * NOTE that workspace manager is unavailable when restarting from backup.
 * It is possible to allow SA_MODE executables except restoredb to use the function,
 * however, it is better not to use it in SA_MODE for clarity.
 */
int
serial_cache_index_btid (THREAD_ENTRY * thread_p)
{
  int error_code = NO_ERROR;
  OID serial_oid = OID_INITIALIZER;

  /* Get serial class OID. */
  oid_get_serial_oid (&serial_oid);
  assert (!OID_ISNULL (&serial_oid));

  /* Now try to get index BTID. */
  error_code = heap_get_btid_from_index_name (thread_p, &serial_oid, "pk_db_serial_unique_name", &serial_Cached_btid);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }
  /* Safe guard: successfully read a non-NULL BTID. */
  assert (!BTID_IS_NULL (&serial_Cached_btid));
  return NO_ERROR;
}

/*
 * serial_get_index_btid () - Get serial index BTID.
 *
 * return      : Void.
 * output (in) : Serial index btid.
 *
 * NOTE that workspace manager is unavailable when restarting from backup.
 * It is possible to allow SA_MODE executables except restoredb to use the function,
 * however, it is better not to use it in SA_MODE for clarity.
 */
void
serial_get_index_btid (BTID * output)
{
  /* Safe guard: a non-NULL serial index BTID is cached. */
  assert (!BTID_IS_NULL (&serial_Cached_btid));
  /* Safe guard: output parameter for index BTID is not NULL. */
  assert (output != NULL);

  BTID_COPY (output, &serial_Cached_btid);
}
#endif /* SERVER_MODE */
