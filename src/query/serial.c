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
 * serial.c - Serial number handling routine
 */

#ident "$Id$"

#include "config.h"

#include <assert.h>
#include <errno.h>

#include "intl_support.h"
#include "error_code.h"
#include "db.h"
#include "memory_alloc.h"
#include "arithmetic.h"
#include "serial.h"
#include "query_evaluator.h"
#include "heap_file.h"
#include "page_buffer.h"
#include "log_manager.h"
#include "transaction_sr.h"
#include "replication.h"
#include "xserver_interface.h"
#include "transform.h"

#if !defined(SERVER_MODE)
#undef MUTEX_INIT
#define MUTEX_INIT(a)
#undef MUTEX_DESTROY
#define MUTEX_DESTROY(a)
#undef MUTEX_LOCK
#define MUTEX_LOCK(a, b)
#undef MUTEX_UNLOCK
#define MUTEX_UNLOCK(a)
#endif /* !SERVER_MODE */

/* attribute of db_serial class */
typedef enum
{
  SR_ATT_NAME_INDEX,
  SR_ATT_OWNER_INDEX,
  SR_ATT_CURRENT_VAL_INDEX,
  SR_ATT_INCREMENT_VAL_INDEX,
  SR_ATT_MAX_VAL_INDEX,
  SR_ATT_MIN_VAL_INDEX,
  SR_ATT_CYCLIC_INDEX,
  SR_ATT_STARTED_INDEX,
  SR_ATT_CLASS_NAME_INDEX,
  SR_ATT_ATT_NAME_INDEX,
  SR_ATT_CACHED_NUM_INDEX,
  SR_ATT_MAX_INDEX
} SR_ATTRIBUTES;

#define SR_ATT_NAME                     "name"
#define SR_ATT_OWNER                    "owner"
#define SR_ATT_CURRENT_VAL              "current_val"
#define SR_ATT_INCREMENT_VAL            "increment_val"
#define SR_ATT_MAX_VAL                  "max_val"
#define SR_ATT_MIN_VAL                  "min_val"
#define SR_ATT_CYCLIC                   "cyclic"
#define SR_ATT_STARTED                  "started"
#define SR_ATT_CLASS_NAME               "class_name"
#define SR_ATT_ATT_NAME                 "att_name"
#define SR_ATT_CACHED_NUM               "cached_num"

#define NCACHE_OBJECTS 100

#define NOT_FOUND -1

typedef struct serial_entry SERIAL_CACHE_ENTRY;
struct serial_entry
{
  OID oid;			/* serial object identifier */

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

  /* free list */
  struct serial_entry *next;
};

typedef struct serial_cache_area SERIAL_CACHE_AREA;
struct serial_cache_area
{
  SERIAL_CACHE_ENTRY *obj_area;
  struct serial_cache_area *next;
};

typedef struct serial_cache_pool SERIAL_CACHE_POOL;
struct serial_cache_pool
{
  MHT_TABLE *ht;		/* hash table of serial cache pool */

  SERIAL_CACHE_ENTRY *free_list;

  SERIAL_CACHE_AREA *area;

  OID db_serial_class_oid;
  MUTEX_T cache_pool_mutex;
};

SERIAL_CACHE_POOL serial_Cache_pool = { NULL, NULL, NULL,
  {NULL_PAGEID, NULL_SLOTID, NULL_VOLID}, MUTEX_INITIALIZER
};

ATTR_ID serial_Attrs_id[SR_ATT_MAX_INDEX];
int serial_Num_attrs = -1;

static int xserial_get_current_value_internal (THREAD_ENTRY * thread_p,
					       OID * serial_oidp,
					       DB_VALUE * result_num);
static int xserial_get_next_value_internal (THREAD_ENTRY * thread_p,
					    OID * serial_oidp,
					    DB_VALUE * result_num);
static int serial_get_next_cached_value (THREAD_ENTRY * thread_p,
					 SERIAL_CACHE_ENTRY * entry);
static int serial_update_cur_val_of_serial (THREAD_ENTRY * thread_p,
					    SERIAL_CACHE_ENTRY * entry);
static PAGE_PTR serial_fetch_serial_object (THREAD_ENTRY * thread_p,
					    RECDES * recdesc,
					    HEAP_CACHE_ATTRINFO * attr_info,
					    OID * serial_oidp);
static int serial_update_serial_object (THREAD_ENTRY * thread_p,
					PAGE_PTR pgptr, RECDES * recdesc,
					HEAP_CACHE_ATTRINFO * attr_info,
					OID * serial_oidp,
					DB_VALUE * key_val);
static int serial_get_nth_value (DB_VALUE * inc_val, DB_VALUE * cur_val,
				 DB_VALUE * min_val, DB_VALUE * max_val,
				 DB_VALUE * cyclic, int nth,
				 DB_VALUE * result_val);
static void serial_set_cache_entry (SERIAL_CACHE_ENTRY * entry,
				    DB_VALUE * inc_val, DB_VALUE * cur_val,
				    DB_VALUE * min_val, DB_VALUE * max_val,
				    DB_VALUE * started, DB_VALUE * cyclic,
				    DB_VALUE * last_val, int cached_num);
static void serial_clear_value (SERIAL_CACHE_ENTRY * entry);
static SERIAL_CACHE_ENTRY *serial_alloc_cache_entry (void);
static SERIAL_CACHE_AREA *serial_alloc_cache_area (int num);
static int serial_load_attribute_info_of_db_serial (THREAD_ENTRY * thread_p);
static ATTR_ID serial_get_attrid (THREAD_ENTRY * thread_p, int attr_index);

/*
 * xserial_get_current_value () -
 *   return: NO_ERROR, or ER_code
 *   oid_str_val(in)    :
 *   result_num(out)    :
 */
int
xserial_get_current_value (THREAD_ENTRY * thread_p,
			   const DB_VALUE * oid_str_val,
			   DB_VALUE * result_num)
{
  int ret = NO_ERROR;
  const char *oid_str = NULL;
  char *p;
  OID serial_oid;
  int cached_num;
  SERIAL_CACHE_ENTRY *entry;
#if defined(SERVER_MODE)
  int rc;
#endif /* SERVER_MODE */

  assert (oid_str_val != (DB_VALUE *) NULL);
  assert (result_num != (DB_VALUE *) NULL);

  oid_str = DB_GET_STRING (oid_str_val);
  if (oid_str == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENT, 0);
      return ER_OBJ_INVALID_ARGUMENT;
    }

  serial_oid.pageid = (int) strtol (oid_str, &p, 10);
  serial_oid.slotid = (int) strtol (p, &p, 10);
  serial_oid.volid = (short) strtol (p, &p, 10);
  cached_num = (int) strtol (p, &p, 10);

  assert (*p == '\0');		/* should exhaust the string */

  if (cached_num <= 1)
    {
      /* not used serial cache */
      ret = xserial_get_current_value_internal (thread_p, &serial_oid,
						result_num);
    }
  else
    {
      /* used serial cache */
      MUTEX_LOCK (rc, serial_Cache_pool.cache_pool_mutex);
      entry = (SERIAL_CACHE_ENTRY *) mht_get (serial_Cache_pool.ht,
					      &serial_oid);
      if (entry != NULL)
	{
	  pr_clone_value (&entry->cur_val, result_num);
	}
      else
	{
	  ret = xserial_get_current_value_internal (thread_p, &serial_oid,
						    result_num);
	}
      MUTEX_UNLOCK (serial_Cache_pool.cache_pool_mutex);
    }

  return ret;
}

/*
 * xserial_get_current_value_internal () -
 *   return: NO_ERROR, or ER_code
 *   serial_oidp(in)    :
 *   result_num(out)    :
 */
static int
xserial_get_current_value_internal (THREAD_ENTRY * thread_p,
				    OID * serial_oidp, DB_VALUE * result_num)
{
  int ret = NO_ERROR;
  OID serial_class_oid;
  VPID vpid;
  PAGE_PTR pgptr;
  SCAN_CODE scan;
  RECDES recdesc;
  HEAP_CACHE_ATTRINFO attr_info, *attr_info_p = NULL;
  ATTR_ID attr_id;
  DB_VALUE *cur_val;

  VPID_SET (&vpid, serial_oidp->volid, serial_oidp->pageid);

  /* lock and fetch page */
  pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ,
		     PGBUF_UNCONDITIONAL_LATCH);
  if (pgptr == NULL)
    {
      if (er_errid () == ER_PB_BAD_PAGEID)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HEAP_UNKNOWN_OBJECT,
		  3, serial_oidp->volid, serial_oidp->pageid,
		  serial_oidp->slotid);
	}
      goto exit_on_error;
    }

#if defined (DEBUG)
  /* check record type */
  if (spage_get_record_type (pgptr, serial_oid.slotid) == REC_UNKNOWN)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_HEAP_UNKNOWN_OBJECT, 3,
	      serial_oidp->volid, serial_oidp->pageid, serial_oidp->slotid);
      goto exit_on_error;
    }
#endif

  /* get record into record desc */
  scan = spage_get_record (pgptr, serial_oidp->slotid, &recdesc, PEEK);
  if (scan != S_SUCCESS)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_CANNOT_FETCH_SERIAL,
	      0);
      goto exit_on_error;
    }

  or_class_oid (&recdesc, &serial_class_oid);

  /* retrieve attribute */
  attr_id = serial_get_attrid (thread_p, SR_ATT_CURRENT_VAL_INDEX);
  assert (attr_id != NOT_FOUND);
  ret = heap_attrinfo_start (thread_p, &serial_class_oid, 1, &attr_id,
			     &attr_info);
  if (ret != NO_ERROR)
    {
      goto exit_on_error;
    }
  attr_info_p = &attr_info;

  ret = heap_attrinfo_read_dbvalues (thread_p, serial_oidp, &recdesc,
				     attr_info_p);
  if (ret != NO_ERROR)
    {
      goto exit_on_error;
    }

  cur_val = heap_attrinfo_access (attr_id, attr_info_p);
  PR_SHARE_VALUE (cur_val, result_num);
  heap_attrinfo_end (thread_p, attr_info_p);

  pgbuf_unfix_and_init (thread_p, pgptr);

  return NO_ERROR;

exit_on_error:

  if (pgptr != NULL)
    {
      pgbuf_unfix_and_init (thread_p, pgptr);
    }

  if (attr_info_p != NULL)
    {
      heap_attrinfo_end (thread_p, attr_info_p);
    }

  ret = (ret == NO_ERROR
	 && (ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;
  return ret;
}

/*
 * xserial_get_next_value () -
 *   return: NO_ERROR, or ER_status
 *   oid_str_val(in)    :
 *   result_num(out)     :
 */
int
xserial_get_next_value (THREAD_ENTRY * thread_p,
			const DB_VALUE * oid_str_val, DB_VALUE * result_num)
{
  int ret = NO_ERROR, granted;
  const char *oid_str = NULL;
  char *p;
  OID serial_oid;
  int cached_num;
  SERIAL_CACHE_ENTRY *entry;
#if defined (SERVER_MODE)
  int rc;
#endif /* SERVER_MODE */

  assert (oid_str_val != (DB_VALUE *) NULL);
  assert (result_num != (DB_VALUE *) NULL);

  if (!LOG_CHECK_LOG_APPLIER (thread_p))
    {
      CHECK_MODIFICATION_NO_RETURN (ret);
      if (ret != NO_ERROR)
	{
	  return ret;
	}
    }

  oid_str = DB_GET_STRING (oid_str_val);
  if (oid_str == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENT, 0);
      return ER_OBJ_INVALID_ARGUMENT;
    }

  serial_oid.pageid = (int) strtol (oid_str, &p, 10);
  serial_oid.slotid = (int) strtol (p, &p, 10);
  serial_oid.volid = (short) strtol (p, &p, 10);
  cached_num = (short) strtol (p, &p, 10);

  assert (*p == '\0');		/* should exhaust the string */

  if (cached_num <= 1)
    {
      /* not used serial cache */
      ret = xserial_get_next_value_internal (thread_p, &serial_oid,
					     result_num);
    }
  else
    {
      /* used serial cache */
      MUTEX_LOCK (rc, serial_Cache_pool.cache_pool_mutex);
      entry = (SERIAL_CACHE_ENTRY *) mht_get (serial_Cache_pool.ht,
					      &serial_oid);
      if (entry != NULL)
	{
	  ret = serial_get_next_cached_value (thread_p, entry);
	  if (ret == NO_ERROR)
	    {
	      pr_clone_value (&entry->cur_val, result_num);
	    }
	}
      else
	{
	  granted = lock_object (thread_p, &serial_oid,
				 &serial_Cache_pool.db_serial_class_oid,
				 X_LOCK, LK_UNCOND_LOCK);
	  if (granted != LK_GRANTED)
	    {
	      ret = er_errid ();
	    }
	  else
	    {
	      ret = xserial_get_next_value_internal (thread_p, &serial_oid,
						     result_num);
	      (void) lock_unlock_object (thread_p, &serial_oid,
					 &serial_Cache_pool.
					 db_serial_class_oid, X_LOCK, true);
	    }
	}
      MUTEX_UNLOCK (serial_Cache_pool.cache_pool_mutex);
    }

  return ret;
}

/*
 * serial_get_next_cached_value () -
 *   return: NO_ERROR, or ER_status
 *   entry(in/out)    :
 */
static int
serial_get_next_cached_value (THREAD_ENTRY * thread_p,
			      SERIAL_CACHE_ENTRY * entry)
{
  DB_VALUE cmp_result;
  DB_VALUE next_val;

  int error;

  numeric_db_value_compare (&entry->cur_val, &entry->last_cached_val,
			    &cmp_result);
  /* consumed all cached value */
  if (DB_GET_INT (&cmp_result) == 0)
    {
      /* entry is cached to number of cached_num */
      error = serial_get_nth_value (&entry->inc_val, &entry->cur_val,
				    &entry->min_val, &entry->max_val,
				    &entry->cyclic, entry->cached_num,
				    &entry->last_cached_val);
      if (error != NO_ERROR)
	{
	  return error;
	}

      /* cur_val of db_serial is updated to last_cached_val of entry */
      error = serial_update_cur_val_of_serial (thread_p, entry);
      if (error != NO_ERROR)
	{
	  return error;
	}
    }

  /* get next value */
  error = serial_get_nth_value (&entry->inc_val, &entry->cur_val,
				&entry->min_val, &entry->max_val,
				&entry->cyclic, 1, &next_val);
  if (error != NO_ERROR)
    {
      return error;
    }
  pr_clone_value (&next_val, &entry->cur_val);

  return NO_ERROR;
}

/*
 * serial_update_cur_val_of_serial () -
 *                cur_val of db_serial is updated to last_cached_val of entry
 *   return: NO_ERROR, or ER_status
 *   entry(in)    :
 */
static int
serial_update_cur_val_of_serial (THREAD_ENTRY * thread_p,
				 SERIAL_CACHE_ENTRY * entry)
{
  int ret = NO_ERROR;
  PAGE_PTR pgptr;
  RECDES recdesc;
  HEAP_CACHE_ATTRINFO attr_info, *attr_info_p = NULL;
  DB_VALUE *val;
  DB_VALUE key_val;
  ATTR_ID attrid;

  DB_MAKE_NULL (&key_val);

  if (!LOG_CHECK_LOG_APPLIER (thread_p))
    {
      CHECK_MODIFICATION_NO_RETURN (ret);
      if (ret != NO_ERROR)
	{
	  return ret;
	}
    }

  pgptr = serial_fetch_serial_object (thread_p, &recdesc, &attr_info,
				      &entry->oid);
  if (pgptr == NULL)
    {
      goto exit_on_error;
    }
  attr_info_p = &attr_info;

  attrid = serial_get_attrid (thread_p, SR_ATT_NAME_INDEX);
  assert (attrid != NOT_FOUND);
  val = heap_attrinfo_access (attrid, attr_info_p);
  pr_clone_value (val, &key_val);

  attrid = serial_get_attrid (thread_p, SR_ATT_CURRENT_VAL_INDEX);
  assert (attrid != NOT_FOUND);
  ret = heap_attrinfo_set (&entry->oid, attrid,
			   &entry->last_cached_val, attr_info_p);
  if (ret != NO_ERROR)
    {
      goto exit_on_error;
    }

  ret = serial_update_serial_object (thread_p, pgptr, &recdesc, attr_info_p,
				     &entry->oid, &key_val);
  if (ret != NO_ERROR)
    {
      goto exit_on_error;
    }

  pgbuf_set_dirty (thread_p, pgptr, FREE);
  pgptr = NULL;

  heap_attrinfo_end (thread_p, attr_info_p);

  pr_clear_value (&key_val);

  return NO_ERROR;

exit_on_error:

  pr_clear_value (&key_val);

  if (pgptr != NULL)
    {
      pgbuf_unfix_and_init (thread_p, pgptr);
    }

  if (attr_info_p != NULL)
    {
      heap_attrinfo_end (thread_p, attr_info_p);
    }

  return (ret == NO_ERROR
	  && (ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;
}

/*
 * xserial_get_next_value_internal () -
 *   return: NO_ERROR, or ER_status
 *   serial_oidp(in)    :
 *   result_num(out)    :
 */
static int
xserial_get_next_value_internal (THREAD_ENTRY * thread_p,
				 OID * serial_oidp, DB_VALUE * result_num)
{
  int ret = NO_ERROR;
  PAGE_PTR pgptr;
  RECDES recdesc;
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
  int cached_num;
  SERIAL_CACHE_ENTRY *entry = NULL;
  ATTR_ID attrid;

  DB_MAKE_NULL (&key_val);

  pgptr = serial_fetch_serial_object (thread_p, &recdesc, &attr_info,
				      serial_oidp);
  if (pgptr == NULL)
    {
      goto exit_on_error;
    }
  attr_info_p = &attr_info;

  attrid = serial_get_attrid (thread_p, SR_ATT_CACHED_NUM_INDEX);
  if (attrid == NOT_FOUND)
    {
      cached_num = 0;
    }
  else
    {
      val = heap_attrinfo_access (attrid, attr_info_p);
      cached_num = DB_GET_INT (val);
    }

  attrid = serial_get_attrid (thread_p, SR_ATT_NAME_INDEX);
  assert (attrid != NOT_FOUND);
  val = heap_attrinfo_access (attrid, attr_info_p);
  pr_clone_value (val, &key_val);

  attrid = serial_get_attrid (thread_p, SR_ATT_CURRENT_VAL_INDEX);
  assert (attrid != NOT_FOUND);
  val = heap_attrinfo_access (attrid, attr_info_p);
  PR_SHARE_VALUE (val, &cur_val);

  attrid = serial_get_attrid (thread_p, SR_ATT_INCREMENT_VAL_INDEX);
  assert (attrid != NOT_FOUND);
  val = heap_attrinfo_access (attrid, attr_info_p);
  PR_SHARE_VALUE (val, &inc_val);

  attrid = serial_get_attrid (thread_p, SR_ATT_MAX_VAL_INDEX);
  assert (attrid != NOT_FOUND);
  val = heap_attrinfo_access (attrid, attr_info_p);
  PR_SHARE_VALUE (val, &max_val);

  attrid = serial_get_attrid (thread_p, SR_ATT_MIN_VAL_INDEX);
  assert (attrid != NOT_FOUND);
  val = heap_attrinfo_access (attrid, attr_info_p);
  PR_SHARE_VALUE (val, &min_val);

  attrid = serial_get_attrid (thread_p, SR_ATT_CYCLIC_INDEX);
  assert (attrid != NOT_FOUND);
  val = heap_attrinfo_access (attrid, attr_info_p);
  PR_SHARE_VALUE (val, &cyclic);

  attrid = serial_get_attrid (thread_p, SR_ATT_STARTED_INDEX);
  assert (attrid != NOT_FOUND);
  val = heap_attrinfo_access (attrid, attr_info_p);
  PR_SHARE_VALUE (val, &started);

  DB_MAKE_NULL (&last_val);

  if (DB_GET_INT (&started) == 0)
    {
      DB_MAKE_INT (&started, 1);
      attrid = serial_get_attrid (thread_p, SR_ATT_STARTED_INDEX);
      assert (attrid != NOT_FOUND);
      ret = heap_attrinfo_set (serial_oidp, attrid, &started, attr_info_p);
      if (ret == NO_ERROR)
	{
	  PR_SHARE_VALUE (&cur_val, &next_val);
	  if (cached_num > 1)
	    {
	      ret = serial_get_nth_value (&inc_val, &cur_val, &min_val,
					  &max_val, &cyclic, cached_num - 1,
					  &last_val);
	    }
	}
    }
  else
    {
      if (cached_num > 1)
	{
	  ret = serial_get_nth_value (&inc_val, &cur_val, &min_val, &max_val,
				      &cyclic, cached_num, &last_val);
	}
      if (ret == NO_ERROR)
	{
	  ret = serial_get_nth_value (&inc_val, &cur_val, &min_val, &max_val,
				      &cyclic, 1, &next_val);
	}
    }

  if (ret != NO_ERROR)
    {
      goto exit_on_error;
    }

  /* Now update record */
  attrid = serial_get_attrid (thread_p, SR_ATT_CURRENT_VAL_INDEX);
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

  ret = serial_update_serial_object (thread_p, pgptr, &recdesc, attr_info_p,
				     serial_oidp, &key_val);
  if (ret != NO_ERROR)
    {
      goto exit_on_error;
    }

  pgbuf_set_dirty (thread_p, pgptr, FREE);
  pgptr = NULL;

  if (cached_num > 1)
    {
      entry = serial_alloc_cache_entry ();
      if (entry != NULL)
	{
	  COPY_OID (&entry->oid, serial_oidp);
	  if (mht_put (serial_Cache_pool.ht, &entry->oid, entry) == NULL)
	    {
	      OID_SET_NULL (&entry->oid);
	      entry->next = serial_Cache_pool.free_list;
	      serial_Cache_pool.free_list = entry;
	      entry = NULL;
	    }
	  else
	    {
	      PR_SHARE_VALUE (&next_val, &cur_val);
	      serial_set_cache_entry (entry, &inc_val, &cur_val,
				      &min_val, &max_val, &started, &cyclic,
				      &last_val, cached_num);
	    }
	}
    }

  /* copy result value */
  PR_SHARE_VALUE (&next_val, result_num);

  heap_attrinfo_end (thread_p, attr_info_p);

  pr_clear_value (&key_val);

  return NO_ERROR;

exit_on_error:

  pr_clear_value (&key_val);

  if (pgptr != NULL)
    {
      pgbuf_unfix_and_init (thread_p, pgptr);
    }

  if (attr_info_p != NULL)
    {
      heap_attrinfo_end (thread_p, attr_info_p);
    }

  return (ret == NO_ERROR
	  && (ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;
}

/*
 * serial_fetch_serial_object () -
 *
 *   return: PAGE_PTR
 *   recdesc(out)      :
 *   attr_info(out)    :
 *   serial_oidp(in)   :
 *
 *   Note:
 *    The caller must call heap_attrinfo_end and pgbuf_unfix
 *    after he is done with serial information.
 */
static PAGE_PTR
serial_fetch_serial_object (THREAD_ENTRY * thread_p, RECDES * recdesc,
			    HEAP_CACHE_ATTRINFO * attr_info,
			    OID * serial_oidp)
{
  PAGE_PTR pgptr = NULL;
  VPID vpid;
  SCAN_CODE scan;
  OID serial_class_oid;
  int ret;

  VPID_SET (&vpid, serial_oidp->volid, serial_oidp->pageid);
  /* lock and fetch page */
  pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
		     PGBUF_UNCONDITIONAL_LATCH);
  if (pgptr == NULL)
    {
      if (er_errid () == ER_PB_BAD_PAGEID)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HEAP_UNKNOWN_OBJECT,
		  3, serial_oidp->volid, serial_oidp->pageid,
		  serial_oidp->slotid);
	}
      goto exit_on_error;
    }

#if defined (DEBUG)
  /* check record type  */
  if (spage_get_record_type (pgptr, serial_oidp->slotid) == REC_UNKNOWN)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_HEAP_UNKNOWN_OBJECT, 3,
	      serial_oidp->volid, serial_oidp->pageid, serial_oidp->slotid);
      goto exit_on_error;
    }
#endif

  /* get record into record desc */
  scan = spage_get_record (pgptr, serial_oidp->slotid, recdesc, PEEK);
  if (scan != S_SUCCESS)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_QPROC_CANNOT_FETCH_SERIAL, 0);
      goto exit_on_error;
    }

  or_class_oid (recdesc, &serial_class_oid);

  /* retrieve attribute */
  ret = heap_attrinfo_start (thread_p, &serial_class_oid, -1, NULL,
			     attr_info);
  if (ret != NO_ERROR)
    {
      goto exit_on_error;
    }

  ret = heap_attrinfo_read_dbvalues (thread_p, serial_oidp, recdesc,
				     attr_info);
  if (ret != NO_ERROR)
    {
      heap_attrinfo_end (thread_p, attr_info);
      goto exit_on_error;
    }

  return pgptr;

exit_on_error:

  if (pgptr != NULL)
    {
      pgbuf_unfix_and_init (thread_p, pgptr);
    }
  return NULL;
}

/*
 * serial_update_serial_object () -
 *   return: NO_ERROR or error status
 *   pgptr(in)         :
 *   recdesc(in)       :
 *   attr_info(in)     :
 *   serial_oidp(in)   :
 *   key_val(in)       :
 */
static int
serial_update_serial_object (THREAD_ENTRY * thread_p, PAGE_PTR pgptr,
			     RECDES * recdesc,
			     HEAP_CACHE_ATTRINFO * attr_info,
			     OID * serial_oidp, DB_VALUE * key_val)
{
  RECDES new_recdesc;
  char copyarea_buf[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT];
  int new_copyarea_length;
  SCAN_CODE scan;
  LOG_DATA_ADDR addr;
  OID serial_class_oid;
  int ret;
  int sp_success;
  LOG_LSA lsa;

  /* need to start topop for replication
   * Replication will recognize and realize a special type of update for serial
   * by this top operation log record.
   */
  ret = xtran_server_start_topop (thread_p, &lsa);
  if (ret != NO_ERROR)
    {
      return ER_FAILED;
    }

  if (db_Enable_replications > 0 && !LOG_CHECK_LOG_APPLIER (thread_p))
    {
      repl_start_flush_mark (thread_p);
    }

  new_copyarea_length = DB_PAGESIZE;
  new_recdesc.data = PTR_ALIGN (copyarea_buf, MAX_ALIGNMENT);
  new_recdesc.area_size = DB_PAGESIZE;

  scan = heap_attrinfo_transform_to_disk (thread_p, attr_info, recdesc,
					  &new_recdesc);
  if (scan != S_SUCCESS)
    {
      assert (false);
      goto exit_on_error;
    }

  /* Log the changes */
  new_recdesc.type = recdesc->type;
  addr.offset = serial_oidp->slotid;
  addr.pgptr = pgptr;

#if defined (DEBUG)
  if (spage_is_updatable (thread_p, addr.pgptr, serial_oidp->slotid,
			  &new_recdesc) == false)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_QPROC_CANNOT_UPDATE_SERIAL, 0);
      goto exit_on_error;
    }
#endif

  log_append_redo_recdes (thread_p, RVHF_UPDATE, &addr, &new_recdesc);

  /* Now really update */
  sp_success = spage_update (thread_p, addr.pgptr, serial_oidp->slotid,
			     &new_recdesc);
  if (sp_success != SP_SUCCESS)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_QPROC_CANNOT_UPDATE_SERIAL, 0);
      goto exit_on_error;
    }

  or_class_oid (recdesc, &serial_class_oid);
  /* make replication log for the special type of update for serial */
  if (db_Enable_replications > 0
      && repl_class_is_replicated (&serial_class_oid)
      && !LOG_CHECK_LOG_APPLIER (thread_p))
    {
      repl_log_insert (thread_p, &serial_class_oid, serial_oidp,
		       LOG_REPLICATION_DATA, RVREPL_DATA_UPDATE, key_val,
		       REPL_INFO_TYPE_STMT_NORMAL);
      repl_add_update_lsa (thread_p, serial_oidp);
    }

  if (db_Enable_replications > 0 && !LOG_CHECK_LOG_APPLIER (thread_p))
    {
      repl_end_flush_mark (thread_p, false);
    }

  if (xtran_server_end_topop (thread_p, LOG_RESULT_TOPOP_COMMIT, &lsa)
      != TRAN_UNACTIVE_COMMITTED)
    {
      return ER_FAILED;
    }

  return NO_ERROR;

exit_on_error:

  xtran_server_end_topop (thread_p, LOG_RESULT_TOPOP_ABORT, &lsa);

  return ((ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;
}

/*
 * serial_get_nth_value () - get Nth next_value
 *   return: NO_ERROR, or ER_status
 *   inc_val(in)        :
 *   cur_val(in)        :
 *   min_val(in)        :
 *   max_val(in)        :
 *   cyclic(in)         :
 *   nth(in)            :
 *   result_val(out)    :
 */
static int
serial_get_nth_value (DB_VALUE * inc_val, DB_VALUE * cur_val,
		      DB_VALUE * min_val, DB_VALUE * max_val,
		      DB_VALUE * cyclic, int nth, DB_VALUE * result_val)
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
      numeric_coerce_int_to_num (nth, num);
      DB_MAKE_NUMERIC (&tmp_val, num, DB_MAX_NUMERIC_PRECISION, 0);
      numeric_db_value_mul (inc_val, &tmp_val, &add_val);
    }
  else
    {
      PR_SHARE_VALUE (inc_val, &add_val);
    }

  /* inc_val_flag (1 or 0) */
  if (inc_val_flag > 0)
    {
      ret = numeric_db_value_sub (max_val, &add_val, &tmp_val);
      if (ret != NO_ERROR)
	{
	  return ret;
	}
      ret = numeric_db_value_compare (cur_val, &tmp_val, &cmp_result);
      if (ret != NO_ERROR)
	{
	  return ret;
	}

      /* cur_val + inc_val * cached_num > max_val */
      if (DB_GET_INT (&cmp_result) > 0)
	{
	  if (DB_GET_INT (cyclic))
	    {
	      PR_SHARE_VALUE (min_val, result_val);
	    }
	  else
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_QPROC_SERIAL_RANGE_OVERFLOW, 0);
	      return ER_QPROC_SERIAL_RANGE_OVERFLOW;
	    }
	}
      else
	{
	  (void) numeric_db_value_add (cur_val, &add_val, result_val);
	}
    }
  else
    {
      ret = numeric_db_value_sub (min_val, &add_val, &tmp_val);
      if (ret != NO_ERROR)
	{
	  return ret;
	}
      ret = numeric_db_value_compare (cur_val, &tmp_val, &cmp_result);
      if (ret != NO_ERROR)
	{
	  return ret;
	}

      /* cur_val + inc_val * cached_num < min_val */
      if (DB_GET_INT (&cmp_result) < 0)
	{
	  if (DB_GET_INT (cyclic))
	    {
	      PR_SHARE_VALUE (max_val, result_val);
	    }
	  else
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_QPROC_SERIAL_RANGE_OVERFLOW, 0);
	      return ER_QPROC_SERIAL_RANGE_OVERFLOW;
	    }
	}
      else
	{
	  (void) numeric_db_value_add (cur_val, &add_val, result_val);
	}
    }

  return NO_ERROR;
}

/*
 * serial_initialize_cache_pool () -
 *   return: NO_ERROR, or ER_status
 */
int
serial_initialize_cache_pool (THREAD_ENTRY * thread_p)
{
  unsigned int i;

  if (serial_Cache_pool.ht != NULL)
    {
      serial_finalize_cache_pool ();
    }

  serial_Cache_pool.area = serial_alloc_cache_area (NCACHE_OBJECTS);
  if (serial_Cache_pool.area == NULL)
    {
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  serial_Cache_pool.free_list = serial_Cache_pool.area->obj_area;

  MUTEX_INIT (serial_Cache_pool.cache_pool_mutex);

  serial_Cache_pool.ht = mht_create ("Serial cache pool hash table",
				     NCACHE_OBJECTS * 8, oid_hash,
				     oid_compare_equals);
  if (serial_Cache_pool.ht == NULL)
    {
      serial_finalize_cache_pool ();
      return ER_FAILED;
    }

  for (i = 0; i < sizeof (serial_Attrs_id) / sizeof (ATTR_ID); i++)
    {
      serial_Attrs_id[i] = -1;
    }

  return NO_ERROR;
}

/*
 * serial_finalize_cache_pool () -
 *   return:
 */
void
serial_finalize_cache_pool (void)
{
  SERIAL_CACHE_AREA *tmp_area;

  serial_Cache_pool.free_list = NULL;

  if (serial_Cache_pool.ht != NULL)
    {
      mht_destroy (serial_Cache_pool.ht);
      serial_Cache_pool.ht = NULL;
    }

  while (serial_Cache_pool.area)
    {
      tmp_area = serial_Cache_pool.area;
      serial_Cache_pool.area = serial_Cache_pool.area->next;

      free_and_init (tmp_area->obj_area);
      free_and_init (tmp_area);
    }

  MUTEX_DESTROY (serial_Cache_pool.cache_pool_mutex);

  serial_Num_attrs = -1;
}

/*
 * serial_get_attrid () -
 *   return: attribute id or NOT_FOUND
 *
 *   attr_index(in) :
 */
static ATTR_ID
serial_get_attrid (THREAD_ENTRY * thread_p, int attr_index)
{
  if (serial_Num_attrs < 0)
    {
      if (serial_load_attribute_info_of_db_serial (thread_p) != NO_ERROR)
	{
	  return NOT_FOUND;
	}
    }

  if (attr_index < 0 || attr_index > serial_Num_attrs)
    {
      return NOT_FOUND;
    }

  return serial_Attrs_id[attr_index];
}

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
  const char *attr_name_p;
  LC_FIND_CLASSNAME status;

  serial_Num_attrs = -1;

  status = xlocator_find_class_oid (thread_p, CT_SERIAL_NAME,
				    &serial_Cache_pool.db_serial_class_oid,
				    NULL_LOCK);
  if (status == LC_CLASSNAME_ERROR || status == LC_CLASSNAME_DELETED)
    {
      return ER_FAILED;
    }

  if (heap_scancache_quick_start (&scan) != NO_ERROR)
    {
      return ER_FAILED;
    }
  if (heap_get (thread_p, &serial_Cache_pool.db_serial_class_oid,
		&class_record, &scan, PEEK, NULL_CHN) != S_SUCCESS)
    {
      return ER_FAILED;
    }

  error = heap_attrinfo_start (thread_p,
			       &serial_Cache_pool.db_serial_class_oid,
			       -1, NULL, &attr_info);
  if (error != NO_ERROR)
    {
      (void) heap_scancache_end (thread_p, &scan);
      return error;
    }

  for (i = 0; i < attr_info.num_values; i++)
    {
      attr_name_p = or_get_attrname (&class_record, i);
      if (attr_name_p == NULL)
	{
	  error = ER_FAILED;
	  goto exit_on_error;
	}

      if (strcmp (attr_name_p, SR_ATT_NAME) == 0)
	{
	  serial_Attrs_id[SR_ATT_NAME_INDEX] = i;
	}
      else if (strcmp (attr_name_p, SR_ATT_OWNER) == 0)
	{
	  serial_Attrs_id[SR_ATT_OWNER_INDEX] = i;
	}
      else if (strcmp (attr_name_p, SR_ATT_CURRENT_VAL) == 0)
	{
	  serial_Attrs_id[SR_ATT_CURRENT_VAL_INDEX] = i;
	}
      else if (strcmp (attr_name_p, SR_ATT_INCREMENT_VAL) == 0)
	{
	  serial_Attrs_id[SR_ATT_INCREMENT_VAL_INDEX] = i;
	}
      else if (strcmp (attr_name_p, SR_ATT_MAX_VAL) == 0)
	{
	  serial_Attrs_id[SR_ATT_MAX_VAL_INDEX] = i;
	}
      else if (strcmp (attr_name_p, SR_ATT_MIN_VAL) == 0)
	{
	  serial_Attrs_id[SR_ATT_MIN_VAL_INDEX] = i;
	}
      else if (strcmp (attr_name_p, SR_ATT_CYCLIC) == 0)
	{
	  serial_Attrs_id[SR_ATT_CYCLIC_INDEX] = i;
	}
      else if (strcmp (attr_name_p, SR_ATT_STARTED) == 0)
	{
	  serial_Attrs_id[SR_ATT_STARTED_INDEX] = i;
	}
      else if (strcmp (attr_name_p, SR_ATT_CLASS_NAME) == 0)
	{
	  serial_Attrs_id[SR_ATT_CLASS_NAME_INDEX] = i;
	}
      else if (strcmp (attr_name_p, SR_ATT_ATT_NAME) == 0)
	{
	  serial_Attrs_id[SR_ATT_ATT_NAME_INDEX] = i;
	}
      else if (strcmp (attr_name_p, SR_ATT_CACHED_NUM) == 0)
	{
	  serial_Attrs_id[SR_ATT_CACHED_NUM_INDEX] = i;
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
serial_set_cache_entry (SERIAL_CACHE_ENTRY * entry,
			DB_VALUE * inc_val, DB_VALUE * cur_val,
			DB_VALUE * min_val, DB_VALUE * max_val,
			DB_VALUE * started, DB_VALUE * cyclic,
			DB_VALUE * last_val, int cached_num)
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

/*
 * serial_alloc_cache_entry () -
 * return:
 */
static SERIAL_CACHE_ENTRY *
serial_alloc_cache_entry (void)
{
  SERIAL_CACHE_ENTRY *entry;
  SERIAL_CACHE_AREA *tmp_area;

  if (serial_Cache_pool.free_list == NULL)
    {
      tmp_area = serial_alloc_cache_area (NCACHE_OBJECTS);
      if (tmp_area == NULL)
	{
	  return NULL;
	}

      tmp_area->next = serial_Cache_pool.area;
      serial_Cache_pool.area = tmp_area;
      serial_Cache_pool.free_list = tmp_area->obj_area;
    }

  entry = serial_Cache_pool.free_list;
  serial_Cache_pool.free_list = serial_Cache_pool.free_list->next;

  return entry;
}

/*
 * xserial_decache () - decache cache entry of cache pool
 * return:
 * oidp(in) :
 */
void
xserial_decache (OID * oidp)
{
  SERIAL_CACHE_ENTRY *entry;
#if defined (SERVER_MODE)
  int rc;
#endif /* SERVER_MODE */

  MUTEX_LOCK (rc, serial_Cache_pool.cache_pool_mutex);
  entry = (SERIAL_CACHE_ENTRY *) mht_get (serial_Cache_pool.ht, oidp);
  if (entry != NULL)
    {
      mht_rem (serial_Cache_pool.ht, oidp, NULL, NULL);

      OID_SET_NULL (&entry->oid);
      serial_clear_value (entry);
      entry->next = serial_Cache_pool.free_list;
      serial_Cache_pool.free_list = entry;
    }
  MUTEX_UNLOCK (serial_Cache_pool.cache_pool_mutex);
}

/*
 * serial_alloc_cache_area () -
 * return:
 * num(in) :
 */
static SERIAL_CACHE_AREA *
serial_alloc_cache_area (int num)
{
  SERIAL_CACHE_AREA *tmp_area;
  int i;

  tmp_area = (SERIAL_CACHE_AREA *) malloc (sizeof (SERIAL_CACHE_AREA));
  if (tmp_area == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
	      1, sizeof (SERIAL_CACHE_AREA));
      return NULL;
    }
  tmp_area->next = NULL;

  tmp_area->obj_area = ((SERIAL_CACHE_ENTRY *)
			malloc (sizeof (SERIAL_CACHE_ENTRY) * num));
  if (tmp_area->obj_area == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
	      1, sizeof (SERIAL_CACHE_ENTRY) * num);
      free_and_init (tmp_area);
      return NULL;
    }

  /* make free list */
  for (i = 0; i < num - 1; i++)
    {
      tmp_area->obj_area[i].next = &tmp_area->obj_area[i + 1];
    }
  tmp_area->obj_area[i].next = NULL;

  return tmp_area;
}
